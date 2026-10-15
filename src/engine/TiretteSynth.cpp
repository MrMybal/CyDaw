// Tirette: a drawbar organ of the synth family. Nine drawbars (16' to 1')
// mix sine partials for each key, from one of three generators: spinning
// tone wheels (tempered partials folded back an octave at the top of the
// keyboard, each wheel a hair out of tune, a little leakage between them, a
// click as the key's contacts close one after another), transistor dividers
// (brighter, squarer partials) or pipes (pure ratios, each pipe speaking at
// its own pace, a breathy chiff on the attack). A percussion on the second
// or third harmonic that decays fast or slow; a scanner vibrato and its
// chorus; a tube-like preamp that overdrives asymmetrically, and a tone
// control; then a rotating speaker: a horn and a drum, each with its Doppler
// shift and its throb, heard by two microphones, slow or fast and the ramp
// between. Every voice turns its wheels, its rotors and its scanner where
// the song's clock puts them, so that two keys sharing a wheel add up in
// phase and a chord sounds through one cabinet.

#include "SynthEngines.h"
#include "SynthParts.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace cydaw::engine::synth::detail {

namespace {

using namespace parts;

// Tirette's settings after the shared voicing, in its table's order.
enum Tirette : int {
    bar1 = voicingFieldCount,
    bar2,
    bar3,
    bar4,
    bar5,
    bar6,
    bar7,
    bar8,
    bar9,
    organType,
    leakageAmount,
    clickAmount,
    percOn,
    percHarmonic,
    percDecay,
    percVolume,
    vibratoMode,
    vibratoRate,
    driveAmount,
    toneAmount,
    driveOutput,
    rotaryMode,
    rotaryWheel,
    rotaryAccel,
    rotaryBalance,
    rotaryDepth,
    rotaryWidth,
    rotaryMix,
    ampAttack,
    ampRelease,
    ampVelocity,
    tiretteCount
};

// The generators.
constexpr int tonewheel = 0;
constexpr int transistor = 1;
constexpr int pipe = 2;

// The nine drawbars' partials and, after them, the faint whine of a wheel
// four octaves up that leaks into a tone wheel's key.
constexpr std::size_t barCount = 9;
constexpr std::size_t partialCount = 10;
constexpr std::size_t whine = 9;

// Each partial's distance from the 8' in tempered semitones (the wheels and
// the dividers) and in pure ratios (the pipes).
constexpr std::array<double, partialCount> kSemitones { -12.0, 7.0, 0.0, 12.0, 19.0, 24.0, 28.0, 31.0, 36.0, 48.0 };
constexpr std::array<double, partialCount> kPipeSemitones { -12.0, 7.019550, 0.0, 12.0, 19.019550, 24.0, 27.863137, 31.019550, 36.0, 48.0 };
constexpr std::array<double, barCount> kRatios { 0.5, 1.5, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 8.0 };
// A drawbar's level for each of its nine positions: 3 dB a step.
constexpr std::array<double, barCount> kBarGains { 0.0, 0.0891, 0.1259, 0.1778, 0.2512, 0.3548, 0.5012, 0.7079, 1.0 };
// How long each pipe takes to speak (s): the low ones slower.
constexpr std::array<double, partialCount> kSpeech { 0.045, 0.035, 0.03, 0.024, 0.02, 0.017, 0.015, 0.013, 0.011, 0.011 };
// How far the scanner sweeps its delay for V1/C1, V2/C2, V3/C3 (s).
constexpr std::array<double, 3> kSweeps { 0.00010, 0.00020, 0.00036 };
// Where three drawbars out sit for one voice.
constexpr double kScale = 0.15;

double fraction(const double x) noexcept
{
    return x - std::floor(x);
}

// A number in -1..1 that a whole number always gives: a wheel's or a pipe's
// own small mistuning, the same every time it sounds.
double scatter(const int seed) noexcept
{
    auto n = static_cast<std::uint32_t>(seed) * 0x9e3779b1u;
    n ^= n >> 16;
    n *= 0x7feb352du;
    n ^= n >> 15;
    n *= 0x846ca68bu;
    n ^= n >> 16;
    return static_cast<double>(n) / 4'294'967'295.0 * 2.0 - 1.0;
}

// One partial's wave from its sine (and cosine): a wheel's plain sine, a
// divider's squarer one (its third and fifth), a pipe's with its octave.
double partialWave(const int type, const double s, const double c, const double first, const double second) noexcept
{
    if (type == transistor) {
        const auto s2 = s * s;
        return s * (1.0 + first * (3.0 - 4.0 * s2) + second * (5.0 - 20.0 * s2 + 16.0 * s2 * s2));
    }
    if (type == pipe)
        return s + first * 2.0 * s * c;
    return s;
}

} // namespace

std::vector<Section> tiretteSections()
{
    return { { "VOIX", "VOICES", 0xffb0b8c8u },
             { "TIRETTES OCTAVES", "OCTAVE DRAWBARS", 0xffeee3c8u },
             { "TIRETTES GRAVES", "SUB DRAWBARS", 0xffc4844cu },
             { "TIRETTES DE MUTATION", "MUTATION DRAWBARS", 0xff8e9ab4u },
             { "ORGUE", "ORGAN", 0xffffb04au },
             { "PERCUSSION", "PERCUSSION", 0xffff7a52u },
             { "VIBRATO", "VIBRATO", 0xffa58cffu },
             { "PRÉAMPLI", "PREAMP", 0xffff5f6du },
             { "CABINE TOURNANTE", "ROTARY SPEAKER", 0xff4fd1c5u },
             { "ENVELOPPE", "ENVELOPE", 0xff7ad0ffu } };
}

std::vector<Parameter> tiretteParameters()
{
    Table table;
    addVoicing(table, 16, 0.6f);
    // The nine drawbars in their order on the organ, each in its colour's
    // section: the subs, the octaves, the mutations.
    static const std::array<const char*, barCount> feet { "16'", "5⅓'", "8'", "4'", "2⅔'", "2'", "1⅗'", "1⅓'", "1'" };
    static const std::array<int, barCount> sections { 2, 2, 1, 1, 3, 1, 3, 3, 1 };
    static const std::array<int, barCount> defaults { 8, 8, 8, 0, 0, 0, 0, 0, 0 };
    for (std::size_t bar = 0; bar < barCount; ++bar) {
        table.section = sections[bar];
        table.whole("bar" + std::to_string(bar + 1), std::string("Tirette · ") + feet[bar], std::string("Drawbar · ") + feet[bar], {}, 0, 8,
                    defaults[bar]);
    }
    table.section = 4;
    table.choice("organ.type", "Orgue · type", "Organ · type",
                 { { "Roues phoniques", "Tonewheel" }, { "Transistors", "Transistor" }, { "Tuyaux", "Pipe" } }, tonewheel);
    table.add("leakage", "Fuite des roues", "Leakage", {}, 0.0f, 1.0f, 0.2f);
    table.add("click", "Clic de touche", "Key click", {}, 0.0f, 1.0f, 0.4f);
    table.section = 5;
    table.toggle("perc.on", "Percussion · active", "Percussion · on", false);
    table.choice("perc.harmonic", "Percussion · harmonique", "Percussion · harmonic", { { "Deuxième", "Second" }, { "Troisième", "Third" } },
                 1);
    table.choice("perc.decay", "Percussion · déclin", "Percussion · decay", { { "Rapide", "Fast" }, { "Lent", "Slow" } }, 0);
    table.choice("perc.volume", "Percussion · volume", "Percussion · volume", { { "Normal", "Normal" }, { "Doux", "Soft" } }, 0);
    table.section = 6;
    table.choice("vibrato.mode", "Vibrato · mode", "Vibrato · mode",
                 { { "Arrêt", "Off" }, { "V1", "V1" }, { "V2", "V2" }, { "V3", "V3" }, { "C1", "C1" }, { "C2", "C2" }, { "C3", "C3" } }, 6);
    table.add("vibrato.rate", "Vibrato · vitesse", "Vibrato · rate", "Hz", 4.0f, 9.0f, 6.9f, Kind::continuous, true);
    table.section = 7;
    table.add("drive.amount", "Saturation", "Overdrive", {}, 0.0f, 1.0f, 0.15f);
    table.add("tone", "Timbre", "Tone", {}, 0.0f, 1.0f, 0.75f);
    table.add("drive.output", "Sortie", "Output", "dB", -18.0f, 6.0f, 0.0f);
    table.section = 8;
    table.choice("rotary.mode", "Cabine · vitesse", "Rotary · speed", { { "Arrêt", "Off" }, { "Lente", "Slow" }, { "Rapide", "Fast" } }, 1);
    table.toggle("rotary.wheel", "Cabine · molette → rapide", "Rotary · wheel → fast", true);
    table.add("rotary.accel", "Cabine · accélération", "Rotary · acceleration", "s", 0.1f, 10.0f, 0.8f, Kind::continuous, true);
    table.add("rotary.balance", "Cabine · pavillon / tambour", "Rotary · horn / drum", {}, 0.0f, 1.0f, 0.5f);
    table.add("rotary.depth", "Cabine · profondeur", "Rotary · depth", {}, 0.0f, 1.0f, 0.6f);
    table.add("rotary.width", "Cabine · largeur", "Rotary · width", {}, 0.0f, 1.0f, 0.8f);
    table.add("rotary.mix", "Cabine · mélange", "Rotary · mix", {}, 0.0f, 1.0f, 1.0f);
    table.section = 9;
    table.add("amp.attack", "Volume · attaque", "Amp · attack", "s", 0.001f, 5.0f, 0.001f, Kind::continuous, true);
    table.add("amp.release", "Volume · relâchement", "Amp · release", "s", 0.005f, 8.0f, 0.015f, Kind::continuous, true);
    table.add("amp.velocity", "Volume · vélocité", "Amp · velocity", {}, 0.0f, 1.0f, 0.0f);
    return table.parameters;
}

namespace {

// A delay line read between its samples (cubic Hermite).
template <int Size>
struct Delay {
    static_assert((Size & (Size - 1)) == 0, "a power of two");
    std::array<float, static_cast<std::size_t>(Size)> buffer {};
    int write {};

    void clear() noexcept
    {
        buffer.fill(0.0f);
        write = 0;
    }
    void push(const double x) noexcept
    {
        write = (write + 1) & (Size - 1);
        buffer[static_cast<std::size_t>(write)] = static_cast<float>(x);
    }
    // What was pushed `samples` ago, a whole number of them.
    [[nodiscard]] double tap(const int samples) const noexcept
    {
        return static_cast<double>(buffer[static_cast<std::size_t>((write - std::clamp(samples, 0, Size - 1)) & (Size - 1))]);
    }
    // What was pushed `samples` ago (at least two).
    [[nodiscard]] double read(double samples) const noexcept
    {
        samples = std::clamp(samples, 2.0, static_cast<double>(Size - 4));
        const auto position = static_cast<double>(write) - samples;
        const auto whole = std::floor(position);
        const auto t = position - whole;
        const auto at = static_cast<int>(whole);
        const auto sample = [this](const int index) { return static_cast<double>(buffer[static_cast<std::size_t>(index & (Size - 1))]); };
        const auto before = sample(at - 1);
        const auto x0 = sample(at);
        const auto x1 = sample(at + 1);
        const auto x2 = sample(at + 2);
        const auto c1 = 0.5 * (x1 - before);
        const auto c2 = before - 2.5 * x0 + 2.0 * x1 - 0.5 * x2;
        const auto c3 = 0.5 * (x2 - before) + 1.5 * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }
};

// A band-pass of two poles (state variable, solved without delay), its gain
// one at its centre.
struct BandPass {
    double ic1 {};
    double ic2 {};
    double a1 {};
    double a2 {};
    double a3 {};
    double k { 1.0 };

    void reset() noexcept { ic1 = ic2 = 0.0; }
    void tune(const double hz, const double q, const double rate) noexcept
    {
        const auto g = std::tan(kPi * std::clamp(hz, 20.0, rate * 0.45) / rate);
        k = 1.0 / std::max(0.1, q);
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    double run(const double x) noexcept
    {
        const auto v3 = x - ic2;
        const auto v1 = a1 * ic1 + a2 * v3;
        const auto v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        return v1 * k;
    }
};

class TiretteEngine final : public Engine {
public:
    void prepare(const double sampleRate) override { rate_ = std::max(1.0, sampleRate); }

    void start(const Values& values, const int note, const float velocity, const double glideFrom, const double sampleRate) noexcept override
    {
        rate_ = std::max(1.0, sampleRate);
        target_ = static_cast<double>(note);
        pitch_ = glideFrom >= 0.0 && values[voicingGlide] > 0.0f ? glideFrom : target_;
        aimGlide(values);
        velocity_ = std::clamp(static_cast<double>(velocity), 0.0, 1.0);
        amp_.stage = Envelope::Stage::idle;
        amp_.level = 0.0;
        amp_.start();
        elapsed_ = 0.0;
        type_ = std::clamp(static_cast<int>(std::lround(values[static_cast<std::size_t>(organType)])), 0, 2);
        const auto click = std::clamp(static_cast<double>(values[static_cast<std::size_t>(clickAmount)]), 0.0, 1.0);
        // The key's nine contacts close one after another (a pipe speaks without them).
        const auto bounce = type_ == tonewheel ? 0.0025 * click : type_ == transistor ? 0.0008 * click : 0.0;
        for (auto& partial : partials_) {
            partial.c = 1.0;
            partial.s = 0.0;
            partial.gain = 0.0;
            partial.step = 0.0;
            partial.delay = bounce * 0.5 * (random() + 1.0);
        }
        // Where the partials are picked up: at the first block (its clock).
        phased_ = false;
        tunedPitch_ = std::numeric_limits<double>::quiet_NaN();
        percussion_ = 1.0;
        previousPercussion_ = 0.0;
        clickLevel_ = type_ == pipe ? 0.0 : 0.6 + 0.4 * velocity_;
        clickHz_ = 1'800.0 + 1'100.0 * (random() + 1.0);
        chiff_ = type_ == pipe ? 1.0 : 0.0;
        clickFilter_.reset();
        chiffFilter_.reset();
        scanner_.clear();
        horn_.clear();
        drum_.clear();
        dcState_ = 0.0;
        toneState_ = 0.0;
        crossover_ = {};
        previousGate_ = 0.0;
        fresh_ = true;
    }

    void retarget(const Values& values, const int note, const float velocity) noexcept override
    {
        target_ = static_cast<double>(note);
        if (values[voicingGlide] <= 0.0f)
            pitch_ = target_;
        aimGlide(values);
        velocity_ = std::clamp(static_cast<double>(velocity), 0.0, 1.0);
        // Another key under the same hand: its contacts click; the
        // percussion only strikes again once the keys were let go.
        if (type_ != pipe)
            clickLevel_ = std::max(clickLevel_, 0.7 * (0.6 + 0.4 * velocity_));
        if (amp_.stage == Envelope::Stage::release) {
            amp_.start();
            percussion_ = 1.0;
            if (type_ == pipe)
                chiff_ = 1.0;
        }
    }

    void release() noexcept override
    {
        amp_.release();
        // The contacts opening: a softer click.
        if (type_ != pipe)
            clickLevel_ = std::max(clickLevel_, 0.45);
    }

    void kill() noexcept override
    {
        amp_.stage = Envelope::Stage::idle;
        amp_.level = 0.0;
    }

    [[nodiscard]] bool active() const noexcept override { return amp_.stage != Envelope::Stage::idle; }
    [[nodiscard]] bool releasing() const noexcept override { return amp_.stage == Envelope::Stage::release; }
    [[nodiscard]] double currentPitch() const noexcept override { return active() ? pitch_ : -1.0; }

    bool render(const Values& values, const prism::Context& context, float* left, float* right, const int count, const double sampleRate,
                const double expressionPitch, const double expressionGain) noexcept override
    {
        if (!active())
            return false;
        rate_ = std::max(1.0, sampleRate);
        const auto v = [&values](const int index) { return static_cast<double>(values[static_cast<std::size_t>(index)]); };
        const auto choice = [&values](const int index, const int options) {
            return std::clamp(static_cast<int>(std::lround(values[static_cast<std::size_t>(index)])), 0, options - 1);
        };
        constexpr int step = 16;
        // What holds for the whole block.
        const auto type = choice(organType, 3);
        type_ = type;
        const auto percussionOn = v(percOn) >= 0.5;
        const auto percussionSoft = choice(percVolume, 2) == 1;
        const auto percussionIndex = static_cast<std::size_t>(choice(percHarmonic, 2) == 0 ? 3 : 4);
        const auto percussionStep = std::exp(-static_cast<double>(step) / ((choice(percDecay, 2) == 0 ? 0.15 : 0.45) * rate_));
        const auto percussionLevel = percussionOn ? (percussionSoft ? 0.5 : 1.0) : 0.0;
        // At its normal volume the percussion takes some of the drawbars' level.
        const auto barsLevel = percussionOn && !percussionSoft ? 0.71 : 1.0;
        const auto leakage = std::clamp(v(leakageAmount), 0.0, 1.0);
        std::array<double, partialCount> levels {};
        for (std::size_t bar = 0; bar < barCount; ++bar) {
            const auto position = std::clamp(static_cast<int>(std::lround(v(bar1 + static_cast<int>(bar)))), 0, 8);
            levels[bar] = kBarGains[static_cast<std::size_t>(position)] * barsLevel + leakage * 0.02;
        }
        levels[whine] = type == tonewheel ? leakage * 0.012 : 0.0;
        const auto click = std::clamp(v(clickAmount), 0.0, 1.0);
        // A contact closes over a few milliseconds without click, at once with it.
        const auto onsetBase = type == transistor ? 0.002 : 0.003;
        const auto onsetRamp = onsetBase + (0.0002 - onsetBase) * click;
        const auto velocityLevel = 1.0 - std::clamp(v(ampVelocity), 0.0, 1.0) * (1.0 - velocity_);
        const auto gateScale = velocityLevel * expressionGain * kScale;
        const auto outputLevel = v(voicingMaster) * std::pow(10.0, v(driveOutput) / 20.0);
        // Coefficients for a step of `step` samples.
        const auto coefficient = [this](const double seconds, const double share) {
            return std::exp(-static_cast<double>(step) / (std::max(1.0e-4, seconds * share) * rate_));
        };
        const auto attackStep = coefficient(v(ampAttack), 1.0 / 1.466);
        const auto releaseStep = coefficient(v(ampRelease), 1.0 / 4.6);
        const auto bend = static_cast<double>(context.pitchBend) * std::round(v(voicingBend));
        // The click (a burst of noise through a band-pass) and the pipes' chiff.
        const auto clickScale = 1.6 * click;
        const auto clickFall = std::exp(-1.0 / (0.0022 * rate_));
        const auto chiffScale = type == pipe ? 2.2 * (0.3 + 0.7 * click) : 0.0;
        const auto chiffFall = std::exp(-1.0 / (0.03 * rate_));
        clickFilter_.tune(clickHz_, 0.9, rate_);
        // The scanner: how far it sweeps; the chorus keeps the dry beside it.
        const auto vibrato = choice(vibratoMode, 7);
        const auto sweep = vibrato == 0 ? 0.0 : kSweeps[static_cast<std::size_t>((vibrato - 1) % 3)] * rate_;
        const auto chorus = vibrato >= 4;
        const auto scannerRate = std::clamp(v(vibratoRate), 0.1, 20.0);
        const auto scannerDelayAt = [this](const double reach) { return 0.00005 * rate_ + reach * (1.0 + std::sin(2.0 * kPi * scannerPhase_)); };
        // The preamp: a tube's curve, leaning to one side as it is pushed,
        // brought back so that a moderate level stays where it was.
        const auto drive = std::clamp(v(driveAmount), 0.0, 1.0);
        const auto driveGain = 0.05 + 10.0 * drive * std::sqrt(drive);
        const auto bias = 0.3 * drive;
        const auto biasTanh = std::tanh(bias);
        const auto driveNorm = 0.5 / std::max(1.0e-9, std::tanh(0.5 * driveGain + bias) - biasTanh);
        const auto dcFollow = 1.0 - std::exp(-2.0 * kPi * 12.0 / rate_);
        const auto toneHz = std::min(700.0 * std::pow(28.0, std::clamp(v(toneAmount), 0.0, 1.0)), rate_ * 0.45);
        const auto toneG = std::tan(kPi * toneHz / rate_);
        const auto toneA = toneG / (1.0 + toneG);
        // The rotating speaker: its rotors' speeds follow the switch (and the
        // wheel), the drum slower to get there than the horn.
        const auto mode = choice(rotaryMode, 3);
        const auto fast = mode == 2 || (mode == 1 && v(rotaryWheel) >= 0.5 && context.modWheel >= 0.5f);
        const auto hornAim = fast ? 6.8 : 0.8;
        const auto drumAim = fast ? 5.9 : 0.66;
        const auto accel = std::max(0.05, v(rotaryAccel));
        const auto hornFollow = 1.0 - std::exp(-static_cast<double>(step) / (accel / 3.0 * rate_));
        const auto drumFollow = 1.0 - std::exp(-static_cast<double>(step) / (accel * 1.5 * rate_));
        const auto depth = std::clamp(v(rotaryDepth), 0.0, 1.0);
        const auto balance = std::clamp(v(rotaryBalance), 0.0, 1.0);
        const auto hornLevel = std::min(1.0, 2.0 * balance);
        const auto drumLevel = std::min(1.0, 2.0 * (1.0 - balance));
        const auto micHalf = 0.5 * kPi * std::clamp(v(rotaryWidth), 0.0, 1.0);
        const auto wetAim = mode == 0 ? 0.0 : std::clamp(v(rotaryMix), 0.0, 1.0);
        const auto crossG = std::tan(kPi * std::min(800.0, rate_ * 0.45) / rate_);
        const auto crossA = crossG / (1.0 + crossG);
        const auto rotorCentre = 0.0012 * rate_;
        // The dry sound as late as the cabinet's (whole samples, read exactly):
        // mixed with it, no comb.
        const auto dryDelay = static_cast<int>(std::lround(rotorCentre));
        const auto hornSwing = depth * 0.00055 * rate_;
        const auto drumSwing = depth * 0.0003 * rate_;
        const auto hornThrob = depth * 0.65;
        const auto drumThrob = depth * 0.4;
        // Where the rotors put each microphone's delay and level: the horn's
        // left and right, then the drum's (delay, level for each).
        const auto rotorAt = [&](std::array<double, 8>& out) {
            const auto place = [&](const double angle, const double swing, const double throb, const std::size_t at) {
                for (std::size_t side = 0; side < 2; ++side) {
                    const auto facing = std::cos(angle + (side == 0 ? micHalf : -micHalf));
                    out[at + side * 2] = rotorCentre - swing * facing;
                    out[at + side * 2 + 1] = 1.0 - throb * 0.5 * (1.0 - facing);
                }
            };
            place(hornAngle_, hornSwing, hornThrob, 0);
            place(drumAngle_, drumSwing, drumThrob, 4);
        };
        if (fresh_) {
            fresh_ = false;
            // One cabinet for every voice: its rotors and its scanner where
            // the clock puts them, as if turning since it started.
            const auto clock = std::max(0.0, context.clockSeconds);
            hornSpeed_ = hornAim;
            drumSpeed_ = drumAim;
            hornAngle_ = 2.0 * kPi * fraction(hornSpeed_ * clock);
            drumAngle_ = 2.0 * kPi * fraction(drumSpeed_ * clock);
            scannerPhase_ = fraction(scannerRate * clock);
            scannerDelay_ = scannerDelayAt(sweep);
            wet_ = wetAim;
            previousOutput_ = outputLevel;
            rotorAt(rotor_);
        }

        for (int done = 0; done < count; done += step) {
            const auto length = std::min(step, count - done);
            const auto seconds = static_cast<double>(length) / rate_;
            const auto inverse = 1.0 / static_cast<double>(length);
            // The slow things, once a step: the gate, the glide, the partials' pitches and levels.
            const auto ampLevel = amp_.step(attackStep, 0.0, 1.0, releaseStep);
            if (!active())
                return false;
            elapsed_ += seconds;
            // The glide: the same time whatever the distance, at an even pace.
            if (glidePerSample_ > 0.0 && pitch_ != target_) {
                const auto move = glidePerSample_ * static_cast<double>(length);
                pitch_ = std::abs(target_ - pitch_) <= move ? target_ : pitch_ + (target_ > pitch_ ? move : -move);
            } else {
                pitch_ = target_;
            }
            const auto basePitch = pitch_ + expressionPitch + bend;
            if (basePitch != tunedPitch_ || type != tunedType_ || rate_ != tunedRate_) {
                tune(basePitch, type);
                tunedPitch_ = basePitch;
                tunedType_ = type;
                tunedRate_ = rate_;
            }
            if (!phased_) {
                phased_ = true;
                // Wheels (and dividers) that never stop: each partial picked
                // up where the clock has turned it, so that two keys sharing
                // a wheel add up in phase. Pipes are each their own.
                const auto clock = std::max(0.0, context.clockSeconds) + static_cast<double>(done) / rate_;
                for (auto& partial : partials_) {
                    const auto angle = type == pipe ? kPi * (random() + 1.0) : 2.0 * kPi * fraction(partial.hz * clock);
                    partial.c = std::cos(angle);
                    partial.s = std::sin(angle);
                }
            }
            std::array<double, partialCount> aims {};
            for (std::size_t k = 0; k < partialCount; ++k) {
                auto& partial = partials_[k];
                // Each drawbar's level as its contact closed or its pipe spoke.
                const auto onset = type == pipe ? 1.0 - std::exp(-elapsed_ / kSpeech[k])
                                                : std::clamp((elapsed_ - partial.delay) / onsetRamp, 0.0, 1.0);
                aims[k] = levels[k] * onset;
                partial.step = (aims[k] - partial.gain) * inverse;
                // Kept on its circle.
                const auto norm = 1.5 - 0.5 * (partial.c * partial.c + partial.s * partial.s);
                partial.c *= norm;
                partial.s *= norm;
            }
            // The percussion, fading from the key.
            const auto percussionFrom = previousPercussion_;
            percussion_ *= percussionStep;
            const auto percussionTo = percussion_ * percussionLevel;
            previousPercussion_ = percussionTo;
            const auto percussionInc = (percussionTo - percussionFrom) * inverse;
            auto percussionGain = percussionFrom;
            // The gate (the envelope, the velocity, the expression) eased across the step.
            const auto gateTo = ampLevel * gateScale;
            const auto gateInc = (gateTo - previousGate_) * inverse;
            auto gate = previousGate_;
            previousGate_ = gateTo;
            // The scanner's sweep.
            scannerPhase_ = fraction(scannerPhase_ + scannerRate * seconds);
            const auto scannerTo = scannerDelayAt(sweep);
            const auto scannerInc = (scannerTo - scannerDelay_) * inverse;
            // The rotors.
            hornSpeed_ += (hornAim - hornSpeed_) * hornFollow;
            drumSpeed_ += (drumAim - drumSpeed_) * drumFollow;
            hornAngle_ = std::fmod(hornAngle_ + 2.0 * kPi * hornSpeed_ * seconds, 2.0 * kPi);
            drumAngle_ = std::fmod(drumAngle_ + 2.0 * kPi * drumSpeed_ * seconds, 2.0 * kPi);
            std::array<double, 8> rotorTo {};
            rotorAt(rotorTo);
            std::array<double, 8> rotorInc {};
            for (std::size_t k = 0; k < rotorInc.size(); ++k)
                rotorInc[k] = (rotorTo[k] - rotor_[k]) * inverse;
            const auto wetTo = wet_ + (wetAim - wet_) * (1.0 - std::exp(-seconds / 0.02));
            const auto wetInc = (wetTo - wet_) * inverse;
            const auto outputInc = (outputLevel - previousOutput_) * inverse;
            const auto spinning = wet_ > 1.0e-4 || wetTo > 1.0e-4;
            showEnvelope(0, ampLevel);
            showEnvelope(1, percussionOn ? percussion_ : 0.0);
            showLfo(0, vibrato == 0 ? 0.0 : std::sin(2.0 * kPi * scannerPhase_), scannerPhase_);
            showLfo(1, std::cos(hornAngle_), hornAngle_ / (2.0 * kPi));
            showLfo(2, std::cos(drumAngle_), drumAngle_ / (2.0 * kPi));

            for (int i = 0; i < length; ++i) {
                // The partials, each a sine turned one sample on.
                auto sum = 0.0;
                auto percussionWave = 0.0;
                for (std::size_t k = 0; k < partialCount; ++k) {
                    auto& partial = partials_[k];
                    const auto c = partial.c * partial.cr - partial.s * partial.sr;
                    const auto s = partial.s * partial.cr + partial.c * partial.sr;
                    partial.c = c;
                    partial.s = s;
                    const auto wave = partialWave(type, s, c, partial.first, partial.second);
                    partial.gain += partial.step;
                    sum += partial.gain * wave;
                    if (k == percussionIndex)
                        percussionWave = wave;
                }
                percussionGain += percussionInc;
                auto x = sum + percussionWave * percussionGain;
                if (clickLevel_ > 1.0e-4) {
                    x += clickFilter_.run(random()) * clickLevel_ * clickScale;
                    clickLevel_ *= clickFall;
                }
                if (chiff_ > 1.0e-4) {
                    x += chiffFilter_.run(random()) * chiff_ * chiffScale;
                    chiff_ *= chiffFall;
                }
                gate += gateInc;
                x *= gate;
                // The scanner: a delay swept back and forth; its chorus the dry beside it.
                scanner_.push(x);
                scannerDelay_ += scannerInc;
                if (sweep > 0.0) {
                    const auto swept = scanner_.read(scannerDelay_);
                    x = chorus ? 0.6 * (x + swept) : swept;
                }
                // The preamp, its offset taken away, then the tone.
                x = (std::tanh(driveGain * x + bias) - biasTanh) * driveNorm;
                dcState_ += dcFollow * (x - dcState_);
                x -= dcState_;
                const auto toneV = (x - toneState_) * toneA;
                x = toneV + toneState_;
                toneState_ = x + toneV;
                // The cabinet: the lows to the drum, the rest to the horn.
                const auto firstV = (x - crossover_[0]) * crossA;
                const auto firstLow = firstV + crossover_[0];
                crossover_[0] = firstLow + firstV;
                const auto secondV = (firstLow - crossover_[1]) * crossA;
                const auto low = secondV + crossover_[1];
                crossover_[1] = low + secondV;
                horn_.push(x - low);
                drum_.push(low);
                for (std::size_t k = 0; k < rotor_.size(); ++k)
                    rotor_[k] += rotorInc[k];
                wet_ += wetInc;
                const auto dry = horn_.tap(dryDelay) + drum_.tap(dryDelay);
                auto outLeft = dry;
                auto outRight = dry;
                if (spinning) {
                    const auto spunLeft = hornLevel * horn_.read(rotor_[0]) * rotor_[1] + drumLevel * drum_.read(rotor_[4]) * rotor_[5];
                    const auto spunRight = hornLevel * horn_.read(rotor_[2]) * rotor_[3] + drumLevel * drum_.read(rotor_[6]) * rotor_[7];
                    outLeft = dry + wet_ * (spunLeft - dry);
                    outRight = dry + wet_ * (spunRight - dry);
                }
                previousOutput_ += outputInc;
                left[done + i] += static_cast<float>(outLeft * previousOutput_);
                right[done + i] += static_cast<float>(outRight * previousOutput_);
            }
            // Where the step ends, exactly.
            for (std::size_t k = 0; k < partialCount; ++k)
                partials_[k].gain = aims[k];
            scannerDelay_ = scannerTo;
            rotor_ = rotorTo;
            wet_ = wetTo;
            previousOutput_ = outputLevel;
        }
        return active();
    }

private:
    // The pace a glide from here to the note takes to last its time.
    void aimGlide(const Values& values) noexcept
    {
        const auto seconds = static_cast<double>(values[voicingGlide]);
        glidePerSample_ = seconds > 0.0 ? std::abs(target_ - pitch_) / (seconds * rate_) : 0.0;
    }

    // Each partial's frequency for the note at `pitch`: folded back past the
    // generator's top (and under its bottom), a wheel or a pipe a hair out
    // of tune, kept below the Nyquist; its harmonics as far as they fit.
    void tune(const double pitch, const int type) noexcept
    {
        const auto top = type == tonewheel ? 114.0 : type == transistor ? 117.0 : 120.0;
        const auto bottom = type == pipe ? 17.5 : 23.5;
        const auto fade = [this](const double hz) { return std::clamp((rate_ * 0.45 - hz) / (rate_ * 0.1), 0.0, 1.0); };
        for (std::size_t k = 0; k < partialCount; ++k) {
            auto& partial = partials_[k];
            auto note = std::clamp(pitch + (type == pipe ? kPipeSemitones[k] : kSemitones[k]), -60.0, 240.0);
            while (note > top + 0.5)
                note -= 12.0;
            while (note < bottom)
                note += 12.0;
            const auto wheel = static_cast<int>(std::lround(note));
            const auto cents = type == tonewheel ? 0.7 * scatter(wheel) : type == pipe ? 1.5 * scatter(wheel * 16 + static_cast<int>(k)) : 0.0;
            auto hz = 440.0 * std::pow(2.0, (note + cents / 100.0 - 69.0) / 12.0);
            while (hz > rate_ * 0.42)
                hz *= 0.5;
            const auto w = 2.0 * kPi * hz / rate_;
            partial.hz = hz;
            partial.cr = std::cos(w);
            partial.sr = std::sin(w);
            partial.first = type == transistor ? 0.28 * fade(3.0 * hz) : type == pipe ? 0.12 * fade(2.0 * hz) : 0.0;
            partial.second = type == transistor ? 0.14 * fade(5.0 * hz) : 0.0;
            if (k == 2)
                fundamental_ = hz;
        }
        // The chiff sings near the pipe's third harmonic.
        if (type == pipe)
            chiffFilter_.tune(std::clamp(fundamental_ * 3.0, 400.0, 7'000.0), 2.5, rate_);
    }

    double random() noexcept
    {
        seed_ = seed_ * 1'664'525u + 1'013'904'223u;
        return static_cast<double>(seed_ >> 8) / static_cast<double>(1u << 24) * 2.0 - 1.0;
    }

    struct Partial {
        double c { 1.0 };
        double s {};
        double cr { 1.0 };
        double sr {};
        double hz {};
        // A divider's third and fifth; a pipe's octave (first).
        double first {};
        double second {};
        double gain {};
        double step {};
        // When its contact closes after the key (s).
        double delay {};
    };

    double rate_ { 48'000.0 };
    double target_ {};
    double pitch_ {};
    double velocity_ { 1.0 };
    double elapsed_ {};
    double glidePerSample_ {};
    int type_ {};
    Envelope amp_;
    std::array<Partial, partialCount> partials_ {};
    double tunedPitch_ {};
    int tunedType_ { -1 };
    double tunedRate_ {};
    double fundamental_ { 261.6 };
    double percussion_ {};
    double previousPercussion_ {};
    double clickLevel_ {};
    double clickHz_ { 2'500.0 };
    double chiff_ {};
    BandPass clickFilter_;
    BandPass chiffFilter_;
    double previousGate_ {};
    double previousOutput_ {};
    // The scanner and the cabinet.
    Delay<512> scanner_;
    double scannerPhase_ {};
    double scannerDelay_ {};
    double dcState_ {};
    double toneState_ {};
    std::array<double, 2> crossover_ {};
    Delay<1024> horn_;
    Delay<1024> drum_;
    double hornSpeed_ {};
    double drumSpeed_ {};
    double hornAngle_ {};
    double drumAngle_ {};
    std::array<double, 8> rotor_ {};
    double wet_ {};
    bool fresh_ { true };
    bool phased_ {};
    // Each voice its own draw: their phases and clicks apart.
    std::uint32_t seed_ { nextSeed() };
};

} // namespace

std::unique_ptr<Engine> makeTirette()
{
    return std::make_unique<TiretteEngine>();
}

std::vector<float> tirettePicture(const Values& values, const std::string_view part, const int points)
{
    // The drawbars' partials summed over two periods of the 8' (one of the
    // 16'), as their generator makes them, filling the picture.
    if (part != "bars" || points < 2)
        return {};
    const auto type = std::clamp(static_cast<int>(std::lround(values[static_cast<std::size_t>(organType)])), 0, 2);
    std::array<double, barCount> gains {};
    std::array<double, barCount> ratios {};
    for (std::size_t bar = 0; bar < barCount; ++bar) {
        const auto position = std::clamp(static_cast<int>(std::lround(values[static_cast<std::size_t>(bar1) + bar])), 0, 8);
        gains[bar] = kBarGains[static_cast<std::size_t>(position)];
        ratios[bar] = type == pipe ? kRatios[bar] : std::pow(2.0, kSemitones[bar] / 12.0);
    }
    std::vector<float> result(static_cast<std::size_t>(points), 0.0f);
    auto peak = 0.0;
    for (int at = 0; at < points; ++at) {
        const auto phase = 2.0 * static_cast<double>(at) / static_cast<double>(points - 1);
        auto sum = 0.0;
        for (std::size_t bar = 0; bar < barCount; ++bar) {
            if (gains[bar] <= 0.0)
                continue;
            const auto angle = 2.0 * parts::kPi * phase * ratios[bar];
            sum += gains[bar] * partialWave(type, std::sin(angle), std::cos(angle), type == transistor ? 0.28 : type == pipe ? 0.12 : 0.0,
                                            type == transistor ? 0.14 : 0.0);
        }
        result[static_cast<std::size_t>(at)] = static_cast<float>(sum);
        peak = std::max(peak, std::abs(sum));
    }
    if (peak > 1.0e-6) {
        const auto scale = static_cast<float>(0.95 / peak);
        for (auto& sample : result)
            sample *= scale;
    }
    return result;
}

} // namespace cydaw::engine::synth::detail
