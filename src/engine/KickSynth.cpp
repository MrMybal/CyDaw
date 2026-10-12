#include <cydaw/engine/KickSynth.h>

#include <cydaw/engine/BiquadDesign.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace cydaw::engine::kick {

namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;

std::vector<Parameter> makeParameters()
{
    using G = Group;
    std::vector<Parameter> list(parameterCount);
    const auto set = [&list](const Value index, Parameter parameter) { list[static_cast<std::size_t>(index)] = std::move(parameter); };
    set(bodyShape, { "bodyShape", "Forme", "Shape", "", 0.0f, 3.0f, 0.0f, G::body, false,
                     { { "Sinus", "Sine" }, { "Triangle", "Triangle" }, { "Rond", "Round" }, { "Dur", "Hard" } } });
    set(bodyPhase, { "bodyPhase", "Phase", "Phase", "\xc2\xb0", 0.0f, 180.0f, 0.0f, G::body });
    set(pitchStart, { "pitchStart", "Hauteur de départ", "Start pitch", "Hz", 40.0f, 8'000.0f, 320.0f, G::body, true });
    set(pitchEnd, { "pitchEnd", "Hauteur de fin", "End pitch", "Hz", 20.0f, 400.0f, 52.0f, G::body, true });
    set(pitchTime, { "pitchTime", "Temps de chute", "Pitch time", "ms", 2.0f, 1'000.0f, 110.0f, G::body, true });
    set(pitchCurve, { "pitchCurve", "Courbe de chute", "Pitch curve", "", 0.5f, 16.0f, 4.0f, G::body, true });
    set(bodyLevel, { "bodyLevel", "Niveau du corps", "Body level", "dB", -48.0f, 6.0f, 0.0f, G::body });
    set(attack, { "attack", "Attaque", "Attack", "ms", 0.0f, 30.0f, 0.4f, G::amplitude });
    set(hold, { "hold", "Maintien", "Hold", "ms", 0.0f, 1'000.0f, 30.0f, G::amplitude });
    set(decay, { "decay", "Déclin", "Decay", "ms", 10.0f, 4'000.0f, 420.0f, G::amplitude, true });
    set(decayCurve, { "decayCurve", "Courbe du déclin", "Decay curve", "", 0.3f, 8.0f, 1.8f, G::amplitude, true });
    set(length, { "length", "Longueur", "Length", "ms", 50.0f, 6'000.0f, 1'500.0f, G::amplitude, true });
    set(clickType, { "clickType", "Clic", "Click", "", 0.0f, 3.0f, 1.0f, G::click, false,
                     { { "Aucun", "None" }, { "Bruit", "Noise" }, { "Clic", "Click" }, { "Blip", "Blip" } } });
    set(clickLevel, { "clickLevel", "Niveau du clic", "Click level", "dB", -48.0f, 6.0f, -10.0f, G::click });
    set(clickDecay, { "clickDecay", "Déclin du clic", "Click decay", "ms", 0.5f, 80.0f, 6.0f, G::click, true });
    set(clickTone, { "clickTone", "Ton du clic", "Click tone", "Hz", 500.0f, 16'000.0f, 4'500.0f, G::click, true });
    set(subLevel, { "subLevel", "Niveau du sub", "Sub level", "dB", -60.0f, 6.0f, -60.0f, G::sub });
    set(subDecay, { "subDecay", "Déclin du sub", "Sub decay", "ms", 20.0f, 6'000.0f, 600.0f, G::sub, true });
    set(subOctave, { "subOctave", "Octave du sub", "Sub octave", "", 0.0f, 1.0f, 0.0f, G::sub, false,
                     { { "Même", "Same" }, { "-1 octave", "-1 octave" } } });
    set(driveType, { "driveType", "Saturation", "Drive type", "", 0.0f, 3.0f, 0.0f, G::drive, false,
                     { { "Douce", "Soft" }, { "Dure", "Hard" }, { "Repli", "Fold" }, { "Lampe", "Tube" } } });
    set(drive, { "drive", "Drive", "Drive", "dB", 0.0f, 36.0f, 0.0f, G::drive });
    set(driveTone, { "driveTone", "Ton de la saturation", "Drive tone", "Hz", 500.0f, 20'000.0f, 20'000.0f, G::drive, true });
    set(lowCut, { "lowCut", "Coupe-bas", "Low cut", "Hz", 10.0f, 200.0f, 15.0f, G::tone, true });
    set(highCut, { "highCut", "Coupe-haut", "High cut", "Hz", 1'000.0f, 20'000.0f, 20'000.0f, G::tone, true });
    set(lowBoost, { "lowBoost", "Graves", "Low boost", "dB", 0.0f, 12.0f, 0.0f, G::tone });
    set(keyTrack, { "keyTrack", "Suivi de la note", "Key tracking", "", 0.0f, 1.0f, 1.0f, G::global, false,
                    { { "Non", "Off" }, { "Oui", "On" } } });
    set(velocity, { "velocity", "Vélocité", "Velocity", "%", 0.0f, 1.0f, 0.5f, G::global });
    set(output, { "output", "Sortie", "Output", "dB", -24.0f, 12.0f, -3.0f, G::global });
    return list;
}

const std::vector<Parameter>& parameters()
{
    static const auto list = makeParameters();
    return list;
}

double dbToGain(const double db) noexcept
{
    return std::pow(10.0, db / 20.0);
}

struct BiquadState {
    double z1 {};
    double z2 {};
};

double run(const BiquadCoefficients& c, BiquadState& s, const double x) noexcept
{
    const auto y = c.b0 * x + s.z1;
    s.z1 = c.b1 * x - c.a1 * y + s.z2;
    s.z2 = c.b2 * x - c.a2 * y;
    return y;
}

double bodyWave(const int shape, const double s) noexcept
{
    switch (shape) {
    case 1: return std::asin(std::clamp(s, -1.0, 1.0)) * 2.0 / std::numbers::pi;
    case 2: return std::tanh(2.5 * s) / std::tanh(2.5);
    case 3: return std::tanh(8.0 * s) / std::tanh(8.0);
    default: return s;
    }
}

double saturate(const int type, const double gain, const double x) noexcept
{
    switch (type) {
    case 1: return std::clamp(gain * x, -1.0, 1.0);
    case 2: return std::sin(std::numbers::pi * 0.5 * gain * x);
    case 3: {
        constexpr auto bias = 0.25;
        const auto top = std::tanh(gain + bias) - std::tanh(bias);
        return (std::tanh(gain * x + bias) - std::tanh(bias)) / std::max(1.0e-6, top);
    }
    default: return std::tanh(gain * x) / std::max(1.0e-6, std::tanh(std::max(1.0, gain)));
    }
}

int choice(const KickValues& values, const Value index)
{
    const auto& p = parameter(index);
    return std::clamp(static_cast<int>(std::lround(values[static_cast<std::size_t>(index)])), static_cast<int>(p.minimum),
                      static_cast<int>(p.maximum));
}

} // namespace

const Parameter& parameter(const int index)
{
    return parameters()[static_cast<std::size_t>(std::clamp(index, 0, parameterCount - 1))];
}

int parameterIndex(const std::string& key)
{
    const auto& list = parameters();
    for (std::size_t index = 0; index < list.size(); ++index)
        if (list[index].key == key)
            return static_cast<int>(index);
    return -1;
}

KickValues defaultValues()
{
    KickValues values {};
    for (int index = 0; index < parameterCount; ++index)
        values[static_cast<std::size_t>(index)] = parameter(index).defaultValue;
    return values;
}

float clampValue(const int index, const float value)
{
    const auto& p = parameter(index);
    if (!std::isfinite(value))
        return p.defaultValue;
    auto clamped = std::clamp(value, p.minimum, p.maximum);
    if (!p.choices.empty())
        clamped = std::round(clamped);
    return clamped;
}

std::string displayValue(const int index, const float value, const bool french)
{
    const auto& p = parameter(index);
    if (!p.choices.empty()) {
        const auto option = static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(value)), 0,
                                                                static_cast<int>(p.choices.size()) - 1));
        return french ? p.choices[option].first : p.choices[option].second;
    }
    char text[48];
    if (p.unit == "Hz")
        std::snprintf(text, sizeof(text), value >= 1'000.0f ? "%.2f kHz" : "%.0f Hz", value >= 1'000.0f ? value / 1'000.0f : value);
    else if (p.unit == "ms")
        std::snprintf(text, sizeof(text), value >= 1'000.0f ? "%.2f s" : value < 10.0f ? "%.1f ms" : "%.0f ms",
                      value >= 1'000.0f ? value / 1'000.0f : value);
    else if (p.unit == "dB")
        std::snprintf(text, sizeof(text), value <= p.minimum + 1.0e-3f && p.minimum <= -48.0f ? "-inf dB" : "%+.1f dB", value);
    else if (p.unit == "%")
        std::snprintf(text, sizeof(text), "%.0f %%", value * 100.0f);
    else if (!p.unit.empty())
        std::snprintf(text, sizeof(text), "%.0f %s", value, p.unit.c_str());
    else
        std::snprintf(text, sizeof(text), "%.2f", value);
    return text;
}

double pitchAt(const KickValues& values, const double seconds, const double tuning) noexcept
{
    const auto start = static_cast<double>(values[pitchStart]) * tuning;
    const auto end = static_cast<double>(values[pitchEnd]) * tuning;
    const auto time = std::max(0.001, static_cast<double>(values[pitchTime]) * 0.001);
    if (seconds >= time)
        return end;
    const auto rest = 1.0 - std::max(0.0, seconds) / time;
    return end + (start - end) * std::pow(rest, static_cast<double>(values[pitchCurve]));
}

double amplitudeAt(const KickValues& values, const double seconds) noexcept
{
    const auto a = static_cast<double>(values[attack]) * 0.001;
    const auto h = static_cast<double>(values[hold]) * 0.001;
    const auto d = std::max(0.001, static_cast<double>(values[decay]) * 0.001);
    const auto total = static_cast<double>(values[length]) * 0.001;
    auto envelope = 0.0;
    if (seconds < 0.0)
        envelope = 0.0;
    else if (seconds < a)
        envelope = seconds / a;
    else if (seconds < a + h)
        envelope = 1.0;
    else if (seconds < a + h + d)
        envelope = std::pow(1.0 - (seconds - a - h) / d, static_cast<double>(values[decayCurve]));
    // The length cuts it, with a 5 ms fade.
    if (seconds > total - 0.005)
        envelope *= std::clamp((total - seconds) / 0.005, 0.0, 1.0);
    return envelope;
}

double lengthSeconds(const KickValues& values) noexcept
{
    auto body = static_cast<double>(values[attack] + values[hold] + values[decay]) * 0.001;
    if (values[subLevel] > -59.5f)
        body = std::max(body, static_cast<double>(values[subDecay]) * 0.001);
    return std::min(body, static_cast<double>(values[length]) * 0.001);
}

struct Voice::State {
    bool active {};
    double time {};
    double phase {};
    double subPhase {};
    double tuning { 1.0 };
    double gain { 1.0 };
    double end {};
    std::uint32_t random { 0x9e3779b9u };
    BiquadCoefficients click;
    BiquadCoefficients lowCut;
    BiquadCoefficients highCut;
    BiquadCoefficients boost;
    BiquadCoefficients driveTone;
    BiquadState clickState;
    std::array<BiquadState, 2> lowCutState {};
    std::array<BiquadState, 2> highCutState {};
    BiquadState boostState;
    BiquadState driveToneState;
};

Voice::Voice() : state_(std::make_unique<State>()) {}
Voice::~Voice() = default;
Voice::Voice(Voice&&) noexcept = default;
Voice& Voice::operator=(Voice&&) noexcept = default;

void Voice::start(const KickValues& values, const int note, const float velocityValue, const double sampleRate) noexcept
{
    auto& s = *state_;
    const auto rate = std::max(1.0, sampleRate);
    const auto nyquist = rate * 0.45;
    s = State {};
    s.active = true;
    s.tuning = values[keyTrack] >= 0.5f ? std::pow(2.0, static_cast<double>(note - referenceNote) / 12.0) : 1.0;
    s.phase = static_cast<double>(values[bodyPhase]) / 360.0;
    const auto sensitivity = static_cast<double>(values[velocity]);
    s.gain = dbToGain(values[output]) * ((1.0 - sensitivity) + sensitivity * std::clamp(static_cast<double>(velocityValue), 0.0, 1.0));
    s.end = lengthSeconds(values) + 0.02;
    s.random = 0x9e3779b9u ^ static_cast<std::uint32_t>(note * 7919);
    s.click = designEqBand(EqBandType::bandPass, std::min(static_cast<double>(values[clickTone]), nyquist), 0.0, 0.7, 0, 0, rate);
    s.lowCut = designEqBand(EqBandType::lowCut, values[lowCut], 0.0, 0.707, 0, 0, rate);
    s.highCut = designEqBand(EqBandType::highCut, std::min(static_cast<double>(values[highCut]), nyquist), 0.0, 0.707, 0, 0, rate);
    s.boost = designEqBand(EqBandType::lowShelf, 70.0, values[lowBoost], 0.707, 0, 0, rate);
    s.driveTone = designEqBand(EqBandType::highCut, std::min(static_cast<double>(values[driveTone]), nyquist), 0.0, 0.707, 0, 0, rate);
}

void Voice::kill() noexcept
{
    state_->active = false;
}

bool Voice::active() const noexcept
{
    return state_->active;
}

bool Voice::render(const KickValues& values, float* left, float* right, const int count, const double sampleRate) noexcept
{
    auto& s = *state_;
    if (!s.active)
        return false;
    const auto rate = std::max(1.0, sampleRate);
    const auto step = 1.0 / rate;
    const auto shape = choice(values, bodyShape);
    const auto click = choice(values, clickType);
    const auto driveKind = choice(values, driveType);
    const auto bodyGain = dbToGain(values[bodyLevel]);
    const auto clickGain = click == 0 ? 0.0 : dbToGain(values[clickLevel]);
    const auto clickTime = std::max(0.0002, static_cast<double>(values[clickDecay]) * 0.001);
    const auto subGain = values[subLevel] <= -59.5f ? 0.0 : dbToGain(values[subLevel]);
    const auto subTime = std::max(0.01, static_cast<double>(values[subDecay]) * 0.001);
    const auto subFrequency = static_cast<double>(values[pitchEnd]) * s.tuning * (choice(values, subOctave) == 1 ? 0.5 : 1.0);
    const auto driveGain = dbToGain(values[drive]);
    const auto driven = values[drive] > 0.05f;
    const auto cutLow = values[lowCut] > 10.5f;
    const auto cutHigh = values[highCut] < 19'900.0f;
    const auto boosted = values[lowBoost] > 0.05f;
    const auto toned = driven && values[driveTone] < 19'900.0f;
    const auto clickPitch = static_cast<double>(values[clickTone]);
    const auto lengthLimit = static_cast<double>(values[length]) * 0.001;
    for (int index = 0; index < count; ++index) {
        const auto t = s.time;
        // The body: its wave at the falling pitch, under its envelope.
        const auto frequency = pitchAt(values, t, s.tuning);
        const auto body = bodyWave(shape, std::sin(kTwoPi * s.phase)) * amplitudeAt(values, t) * bodyGain;
        s.phase += frequency * step;
        s.phase -= std::floor(s.phase);
        // The sub: a sine at the end pitch with its own decay.
        auto sub = 0.0;
        if (subGain > 0.0) {
            const auto rise = std::min(1.0, t / 0.002);
            sub = std::sin(kTwoPi * s.subPhase) * subGain * rise * std::exp(std::log(0.001) * t / subTime);
            if (t > lengthLimit - 0.005)
                sub *= std::clamp((lengthLimit - t) / 0.005, 0.0, 1.0);
            s.subPhase += subFrequency * step;
            s.subPhase -= std::floor(s.subPhase);
        }
        // The click on top.
        auto hit = 0.0;
        if (clickGain > 0.0 && t < clickTime * 8.0) {
            const auto envelope = std::exp(-t / clickTime);
            if (click == 1) {
                s.random ^= s.random << 13;
                s.random ^= s.random >> 17;
                s.random ^= s.random << 5;
                const auto noise = static_cast<double>(s.random & 0xffffffu) / 8'388'607.5 - 1.0;
                hit = run(s.click, s.clickState, noise) * envelope * 1.6;
            } else if (click == 2) {
                hit = std::sin(kTwoPi * clickPitch * t) * std::exp(-t / (clickTime * 0.35));
            } else {
                const auto blip = clickPitch * 0.25 * (1.0 + 2.0 * std::exp(-t / (clickTime * 0.5)));
                hit = std::sin(kTwoPi * blip * t) * envelope;
            }
            hit *= clickGain;
        }
        auto x = body + sub + hit;
        if (driven) {
            x = saturate(driveKind, driveGain, x);
            if (toned)
                x = run(s.driveTone, s.driveToneState, x);
        }
        if (boosted)
            x = run(s.boost, s.boostState, x);
        if (cutLow)
            x = run(s.lowCut, s.lowCutState[1], run(s.lowCut, s.lowCutState[0], x));
        if (cutHigh)
            x = run(s.highCut, s.highCutState[1], run(s.highCut, s.highCutState[0], x));
        const auto sample = static_cast<float>(x * s.gain);
        left[index] += sample;
        right[index] += sample;
        s.time += step;
        if (s.time > s.end) {
            s.active = false;
            return false;
        }
    }
    return true;
}

std::vector<float> renderKick(const KickValues& values, const double sampleRate, const int note, const float velocityValue,
                              const double maximumSeconds)
{
    Voice voice;
    voice.start(values, note, velocityValue, sampleRate);
    const auto frames = static_cast<std::size_t>(std::min(maximumSeconds, lengthSeconds(values) + 0.02) * sampleRate);
    std::vector<float> left(frames, 0.0f);
    std::vector<float> right(frames, 0.0f);
    for (std::size_t at = 0; at < frames && voice.active(); at += 512) {
        const auto count = static_cast<int>(std::min<std::size_t>(512, frames - at));
        (void) voice.render(values, left.data() + at, right.data() + at, count, sampleRate);
    }
    return left;
}

} // namespace cydaw::engine::kick
