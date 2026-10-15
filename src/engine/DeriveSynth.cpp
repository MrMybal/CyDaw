// Dérive: a simple, warm and slightly unstable analog-style synth of the
// synth family. Two oscillators: the first of seven waves, each with its own
// shape control (a sine that bites, a triangle that leans, a shark's tooth, a
// saturated sine, a saw doubled against itself, a pulse and its width, a
// square that opens gaps), the second of five, tuned and detuned; a noise.
// Each voice drifts on its own: slow random walks of its pitches and its
// cutoff, and its parts a little off as an old voice's are. One voice, two
// across the stereo or four in unison. A high-pass, then a low-pass of two
// slopes (four poles in a loop, or two that saturate as they ring); either
// oscillator may skip the filter. An amp envelope; a second envelope that can
// cycle by itself (its rate, its tilt, its hold); one LFO of seven shapes;
// three modulation slots.

#include "SynthEngines.h"
#include "SynthParts.h"

#include <cydaw/engine/EffectCatalogue.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cydaw::engine::synth::detail {

namespace {

using namespace parts;

// Dérive's settings after the shared voicing, in its table's order.
enum Derive : int {
    unisonMode = voicingFieldCount,
    unisonThickness,
    osc1Wave,
    osc1Shape,
    osc1Octave,
    osc2Wave,
    osc2Octave,
    osc2Semi,
    osc2Detune,
    osc1Level,
    osc2Level,
    noiseLevel,
    driftAmount,
    osc1Filter,
    osc2Filter,
    filterType,
    filterCutoff,
    filterResonance,
    filterHighPass,
    filterKey,
    filterEnv,
    filterLfo,
    ampAttack,
    ampDecay,
    ampSustain,
    ampRelease,
    ampVelocity,
    env2Attack,
    env2Decay,
    env2Sustain,
    env2Release,
    env2Cycle,
    env2Rate,
    env2Sync,
    env2Tilt,
    env2Hold,
    lfo1Shape,
    lfo1Rate,
    lfo1Sync,
    lfo1Retrigger,
    lfo1Amount,
    mod1Source,
    modLast = mod1Source + 3 * 3 - 1,
    deriveCount
};
static_assert(deriveCount == 56);

constexpr std::size_t slotCount = 3;
constexpr std::size_t copyLimit = 4;

// The waves, as oscillator 1 lists them.
enum Wave : int { waveSine, waveTriangle, waveShark, waveSaturated, waveSaw, wavePulse, waveRectangle, waveCount };
// Oscillator 2's among them, and the shape each plays with (it has no shape knob).
constexpr std::array<int, 5> kSecondWaves { waveSine, waveTriangle, waveSaturated, waveSaw, waveRectangle };
constexpr std::array<double, 5> kSecondShapes { 0.0, 0.0, 0.4, 0.0, 0.0 };

// The LFO's shapes: the family's first five (as its pictures draw them), then two more.
enum LfoShape : int { lfoSine, lfoTriangle, lfoSawDown, lfoSquare, lfoSampleHold, lfoSawUp, lfoWander, lfoShapeCount };

// Where a modulation slot reads, where it sends.
enum Source : int { srcEnv2, srcLfo, srcVelocity, srcWheel, srcPressure, srcKey, sourceCount };
enum Destination : int { dstPitch, dstShape1, dstDetune2, dstNoise, dstCutoff, dstResonance, dstAmp, dstLfoRate, dstLfoAmount, destinationCount };

using Choices = std::vector<std::pair<std::string, std::string>>;

const Choices kStacks { { "Simple", "Single" }, { "Stéréo", "Stereo" }, { "Unisson", "Unison" } };
const Choices kFirstWaves { { "Sinus", "Sine" },          { "Triangle", "Triangle" },     { "Dent de requin", "Shark Tooth" },
                            { "Saturée", "Saturated" },   { "Dent de scie", "Saw" },      { "Impulsion", "Pulse" },
                            { "Rectangle", "Rectangle" } };
const Choices kSecondWaveNames { { "Sinus", "Sine" },
                                 { "Triangle", "Triangle" },
                                 { "Saturée", "Saturated" },
                                 { "Dent de scie", "Saw" },
                                 { "Rectangle", "Rectangle" } };
// In the family's filter kinds' order (a ladder, then a two-pole low-pass): its picture draws them so.
const Choices kFilterTypes { { "Passe-bas II 24 dB", "Low-pass II 24 dB" }, { "Passe-bas I 12 dB", "Low-pass I 12 dB" } };
const Choices kLfoShapes { { "Sinus", "Sine" },
                           { "Triangle", "Triangle" },
                           { "Scie descendante", "Saw Down" },
                           { "Carré", "Square" },
                           { "Paliers aléatoires", "Sample & Hold" },
                           { "Scie montante", "Saw Up" },
                           { "Errance", "Wander" } };
const Choices kSources { { "Enveloppe 2", "Envelope 2" }, { "LFO", "LFO" },           { "Vélocité", "Velocity" },
                         { "Molette", "Mod Wheel" },      { "Pression", "Pressure" }, { "Clavier", "Key" } };
const Choices kDestinations { { "Hauteur", "Pitch" },
                              { "Osc 1 · forme", "Osc 1 Shape" },
                              { "Osc 2 · désaccord", "Osc 2 Detune" },
                              { "Bruit", "Noise" },
                              { "Fréquence", "Cutoff" },
                              { "Résonance", "Resonance" },
                              { "Volume", "Amp" },
                              { "LFO · vitesse", "LFO Rate" },
                              { "LFO · quantité", "LFO Amount" } };

// A rounded clip, near enough tanh's shape and cheap: 0 at 0, ±1 from ±3 on.
double softTanh(const double x) noexcept
{
    const auto c = std::clamp(x, -3.0, 3.0);
    return c * (27.0 + c * c) / (27.0 + 9.0 * c * c);
}

// The input of the two-pole filter rounded a little, as a transistor stage does.
double rounded(const double x) noexcept
{
    return x / std::sqrt(1.0 + 0.25 * x * x);
}

// One oscillator's wave as its shape makes it, for a step.
struct Shaping {
    int wave { waveSaw };
    double a {}; // the sine's bite, the triangle's or the tooth's peak, the drive, the second saw's offset, the width
    double b {}; // what keeps it in -1..1, or its average at nothing
};

// Its narrowest parts a few samples wide at least, its drive held back up
// high: no more aliasing than it must.
Shaping shapingFor(const int wave, const double shape, const double dt) noexcept
{
    const auto amount = std::clamp(shape, 0.0, 1.0);
    const auto narrowest = std::min(0.5, 4.0 * dt);
    const auto thinnest = std::min(0.5, 2.0 * dt);
    Shaping result;
    result.wave = std::clamp(wave, 0, waveCount - 1);
    switch (result.wave) {
    case waveSine: result.a = 1.6 * amount; break;
    case waveTriangle: result.a = std::clamp(0.5 - 0.45 * amount, narrowest, 0.5); break;
    case waveShark:
        result.a = std::clamp(0.5 - 0.42 * amount, narrowest, 0.5);
        result.b = result.a / 3.0;
        break;
    case waveSaturated:
        result.a = std::min(1.0 + 7.0 * amount, std::max(1.0, 0.08 / std::max(1.0e-6, dt)));
        result.b = 1.0 / softTanh(result.a);
        break;
    case waveSaw:
        result.a = 0.5 * amount;
        result.b = 1.0 / (1.0 - 0.5 * amount);
        break;
    case wavePulse: result.a = std::clamp(0.5 - 0.45 * amount, thinnest, 0.5); break;
    default: result.a = std::clamp(0.5 - 0.4 * amount, thinnest, 0.5); break;
    }
    return result;
}

// A pulse from 0 to 1 over the start of the period (its edges smoothed).
double raisedPulse(const double t, const double dt, const double width) noexcept
{
    auto end = t - width;
    if (end < 0.0)
        end += 1.0;
    return (t < width ? 1.0 : 0.0) + 0.5 * polyBlep(t, dt) - 0.5 * polyBlep(end, dt);
}

// One sample of a wave at phase t (0..1), dt its step.
double waveAt(const Shaping& s, const double t, const double dt) noexcept
{
    switch (s.wave) {
    case waveSine: {
        // Its bite: the sine moving its own phase, brighter as it bites.
        const auto angle = 2.0 * kPi * t;
        return s.a > 0.0 ? std::sin(angle + s.a * std::sin(angle)) : std::sin(angle);
    }
    case waveTriangle: return t < s.a ? -1.0 + 2.0 * t / s.a : 1.0 - 2.0 * (t - s.a) / (1.0 - s.a);
    case waveShark: {
        // A rise curved as a capacitor charges, a straight fall.
        if (t < s.a) {
            const auto u = 1.0 - t / s.a;
            return 1.0 - 2.0 * u * u - s.b;
        }
        return 1.0 - 2.0 * (t - s.a) / (1.0 - s.a) - s.b;
    }
    case waveSaturated: return softTanh(s.a * std::sin(2.0 * kPi * t)) * s.b;
    case waveSaw: {
        const auto one = 2.0 * t - 1.0 - polyBlep(t, dt);
        if (s.a <= 0.0)
            return one;
        // A second saw a little ahead: their sum hollows as they part.
        auto ahead = t + s.a;
        if (ahead >= 1.0)
            ahead -= 1.0;
        const auto two = 2.0 * ahead - 1.0 - polyBlep(ahead, dt);
        return 0.5 * (one + two) * s.b;
    }
    case wavePulse: return oscillator(1, t, dt, s.a);
    default: {
        // Up, a gap, down, a gap: a square whose halves narrow.
        auto half = t + 0.5;
        if (half >= 1.0)
            half -= 1.0;
        return raisedPulse(t, dt, s.a) - raisedPulse(half, dt, s.a);
    }
    }
}

// The LFO's value at its phase: held (sample & hold), or wandering from one
// random point to the next.
double lfoValue(const int shape, const double phase, const double held, const double from, const double to) noexcept
{
    switch (shape) {
    case lfoSampleHold: return held;
    case lfoSawUp: return 2.0 * phase - 1.0;
    case lfoWander: return from + (to - from) * (0.5 - 0.5 * std::cos(kPi * phase));
    default: return lfoWave(shape, phase, held);
    }
}

// The cycling envelope's level at its phase: a rise, a hold at the top, a fall.
double cycleLevel(const double phase, const double rise, const double hold, const double fall) noexcept
{
    if (phase < rise) {
        const auto u = 1.0 - phase / rise;
        return 1.0 - u * u;
    }
    if (phase < rise + hold)
        return 1.0;
    const auto u = std::clamp((phase - rise - hold) / std::max(1.0e-6, fall), 0.0, 1.0);
    return (1.0 - u) * (1.0 - u);
}

// The gentle filter: two poles, its ringing saturated so that it sings
// without running away.
struct TwoPole {
    double low {};
    double band {};
    void reset() noexcept { low = band = 0.0; }
    double process(const double x, const double a1, const double a2, const double a3) noexcept
    {
        const auto v3 = x - low;
        const auto v1 = a1 * band + a2 * v3;
        const auto v2 = low + a2 * band + a3 * v3;
        band = 2.0 * v1 - band;
        low = 2.0 * v2 - low;
        band /= std::sqrt(1.0 + 0.15 * band * band);
        return v2;
    }
};

} // namespace

std::vector<Section> deriveSections()
{
    return { { "VOIX", "VOICES", 0xffc4b8aeu },     { "OSCILLATEURS", "OSCILLATORS", 0xffff8f6bu },
             { "MÉLANGE", "MIXER", 0xffffc46bu },   { "FILTRE", "FILTER", 0xffff6b8fu },
             { "ENVELOPPES", "ENVELOPES", 0xff6bd6c4u }, { "LFO", "LFO", 0xffb48cffu },
             { "MODULATIONS", "MODULATIONS", 0xff9ad46bu } };
}

std::vector<Parameter> deriveParameters()
{
    Table table;
    addVoicing(table, 12, 0.7f);
    table.choice("unison.mode", "Stéréo / unisson", "Stereo / unison", kStacks, 1);
    table.add("unison.thickness", "Épaisseur", "Thickness", {}, 0.0f, 1.0f, 0.3f);
    table.section = 1;
    table.choice("osc1.wave", "Osc 1 · onde", "Osc 1 · wave", kFirstWaves, waveSaw);
    table.add("osc1.shape", "Osc 1 · forme", "Osc 1 · shape", {}, 0.0f, 1.0f, 0.0f);
    table.whole("osc1.octave", "Osc 1 · octave", "Osc 1 · octave", {}, -2, 2, 0);
    table.choice("osc2.wave", "Osc 2 · onde", "Osc 2 · wave", kSecondWaveNames, 3);
    table.whole("osc2.octave", "Osc 2 · octave", "Osc 2 · octave", {}, -2, 2, 0);
    table.whole("osc2.semi", "Osc 2 · demi-tons", "Osc 2 · semitones", "st", -12, 12, 0);
    table.add("osc2.detune", "Osc 2 · désaccord", "Osc 2 · detune", "ct", -50.0f, 50.0f, 6.0f);
    table.section = 2;
    table.add("osc1.level", "Osc 1 · niveau", "Osc 1 · level", {}, 0.0f, 1.0f, 0.8f);
    table.add("osc2.level", "Osc 2 · niveau", "Osc 2 · level", {}, 0.0f, 1.0f, 0.5f);
    table.add("noise.level", "Bruit", "Noise", {}, 0.0f, 1.0f, 0.0f);
    table.add("drift", "Dérive", "Drift", {}, 0.0f, 1.0f, 0.35f);
    table.toggle("osc1.filter", "Osc 1 → filtre", "Osc 1 → filter", true);
    table.toggle("osc2.filter", "Osc 2 → filtre", "Osc 2 → filter", true);
    table.section = 3;
    table.choice("f.type", "Filtre · type", "Filter · type", kFilterTypes, 0);
    table.add("f.cutoff", "Fréquence", "Cutoff", "Hz", 20.0f, 20'000.0f, 1'400.0f, Kind::continuous, true);
    table.add("f.resonance", "Résonance", "Resonance", {}, 0.0f, 1.0f, 0.15f);
    table.add("filter.hpf", "Passe-haut", "High-pass", "Hz", 20.0f, 2'000.0f, 20.0f, Kind::continuous, true);
    table.add("filter.key", "Suivi du clavier", "Key tracking", {}, 0.0f, 1.0f, 0.5f);
    table.add("filter.env", "Env 2 → filtre", "Env 2 → cutoff", {}, -1.0f, 1.0f, 0.3f);
    table.add("filter.lfo", "LFO → filtre", "LFO → cutoff", {}, 0.0f, 1.0f, 0.0f);
    table.section = 4;
    table.add("amp.attack", "Volume · attaque", "Amp · attack", "s", 0.001f, 10.0f, 0.005f, Kind::continuous, true);
    table.add("amp.decay", "Volume · déclin", "Amp · decay", "s", 0.005f, 10.0f, 0.8f, Kind::continuous, true);
    table.add("amp.sustain", "Volume · maintien", "Amp · sustain", {}, 0.0f, 1.0f, 0.75f);
    table.add("amp.release", "Volume · relâchement", "Amp · release", "s", 0.005f, 12.0f, 0.45f, Kind::continuous, true);
    table.add("amp.velocity", "Volume · vélocité", "Amp · velocity", {}, 0.0f, 1.0f, 0.4f);
    table.add("env2.attack", "Env 2 · attaque", "Env 2 · attack", "s", 0.001f, 10.0f, 0.003f, Kind::continuous, true);
    table.add("env2.decay", "Env 2 · déclin", "Env 2 · decay", "s", 0.005f, 10.0f, 0.6f, Kind::continuous, true);
    table.add("env2.sustain", "Env 2 · maintien", "Env 2 · sustain", {}, 0.0f, 1.0f, 0.3f);
    table.add("env2.release", "Env 2 · relâchement", "Env 2 · release", "s", 0.005f, 12.0f, 0.5f, Kind::continuous, true);
    table.toggle("env2.cycle", "Env 2 · cyclique", "Env 2 · cycling", false);
    table.add("env2.rate", "Env 2 · vitesse", "Env 2 · rate", "Hz", 0.05f, 30.0f, 2.0f, Kind::continuous, true);
    table.whole("env2.sync", "Env 2 · synchro", "Env 2 · sync", {}, 0, 12, 0);
    table.add("env2.tilt", "Env 2 · pente", "Env 2 · tilt", {}, -1.0f, 1.0f, 0.0f);
    table.add("env2.hold", "Env 2 · tenue", "Env 2 · hold", {}, 0.0f, 1.0f, 0.0f);
    table.section = 5;
    table.choice("lfo1.shape", "LFO · forme", "LFO · shape", kLfoShapes, lfoSine);
    table.add("lfo1.rate", "LFO · vitesse", "LFO · rate", "Hz", 0.02f, 30.0f, 5.0f, Kind::continuous, true);
    table.whole("lfo1.sync", "LFO · synchro", "LFO · sync", {}, 0, 12, 0);
    table.toggle("lfo1.retrigger", "LFO · redémarre à chaque note", "LFO · retrigger", false);
    table.add("lfo1.amount", "LFO · quantité", "LFO · amount", {}, 0.0f, 1.0f, 0.0f);
    table.section = 6;
    // By default: the wheel brings the LFO in (a vibrato), the velocity opens the filter.
    static constexpr std::array<int, slotCount> sources { srcLfo, srcVelocity, srcWheel };
    static constexpr std::array<int, slotCount> destinations { dstPitch, dstCutoff, dstLfoAmount };
    static constexpr std::array<float, slotCount> amounts { 0.1f, 0.15f, 1.0f };
    for (std::size_t slot = 0; slot < slotCount; ++slot) {
        const auto number = std::to_string(slot + 1);
        const auto prefix = "mod" + number;
        const auto name = "Mod " + number + " · ";
        table.choice(prefix + ".source", name + "source", name + "source", kSources, sources[slot]);
        table.choice(prefix + ".dest", name + "destination", name + "destination", kDestinations, destinations[slot]);
        table.add(prefix + ".amount", name + "quantité", name + "amount", {}, -1.0f, 1.0f, amounts[slot]);
    }
    return table.parameters;
}

namespace {

class DeriveEngine final : public Engine {
public:
    DeriveEngine() noexcept
    {
        // Each voice a little off in its own way, as the parts of an old one differ.
        for (auto& copy : copies_)
            for (auto& tolerance : copy.tolerance)
                tolerance = random();
        cutoffTolerance_ = random();
        timeTolerance_ = random();
    }

    void prepare(const double sampleRate) override { rate_ = std::max(1.0, sampleRate); }

    void start(const Values& values, const int note, const float velocity, const double glideFrom, const double sampleRate) noexcept override
    {
        rate_ = std::max(1.0, sampleRate);
        target_ = static_cast<double>(note);
        pitch_ = glideFrom >= 0.0 && values[voicingGlide] > 0.0f ? glideFrom : target_;
        aimGlide(values);
        velocity_ = std::clamp(static_cast<double>(velocity), 0.0, 1.0);
        for (auto* envelope : { &amp_, &env2_ }) {
            envelope->stage = Envelope::Stage::idle;
            envelope->level = 0.0;
            envelope->start();
        }
        cyclePhase_ = 0.0;
        for (auto& filter : ladders_)
            filter.reset();
        for (auto& filter : twoPoles_)
            filter.reset();
        highPass_ = {};
        lfoPhase_ = 0.0;
        lfoFresh_ = true;
        lfoHeld_ = random();
        wanderFrom_ = random();
        wanderTo_ = random();
        lfoLast_ = 0.0;
        // Free-running oscillators: each note finds them anywhere. Their
        // drifts go on from where they were (a voice's own wandering).
        for (auto& copy : copies_)
            for (auto& phase : copy.phase)
                phase = 0.5 * (random() + 1.0);
        previousLevel_ = 0.0;
    }

    void retarget(const Values& values, const int note, const float velocity) noexcept override
    {
        target_ = static_cast<double>(note);
        if (values[voicingGlide] <= 0.0f)
            pitch_ = target_;
        aimGlide(values);
        velocity_ = std::clamp(static_cast<double>(velocity), 0.0, 1.0);
        if (amp_.stage == Envelope::Stage::release) {
            amp_.start();
            env2_.start();
        }
    }

    void release() noexcept override
    {
        amp_.release();
        env2_.release();
    }

    void kill() noexcept override
    {
        amp_.stage = Envelope::Stage::idle;
        amp_.level = 0.0;
        env2_.stage = Envelope::Stage::idle;
        env2_.level = 0.0;
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
        const auto stack = choice(unisonMode, 3);
        const auto copies = stack == 0 ? 1 : stack == 1 ? 2 : 4;
        const auto thickness = std::clamp(v(unisonThickness), 0.0, 1.0);
        const auto spreadCents = stack == 2 ? 20.0 : 8.0;
        // Each copy's share: the filter driven as hard whatever their number.
        const auto share = 1.0 / std::sqrt(static_cast<double>(copies));
        const auto firstWave = choice(osc1Wave, waveCount);
        const auto secondIndex = static_cast<std::size_t>(choice(osc2Wave, static_cast<int>(kSecondWaves.size())));
        const auto secondWave = kSecondWaves[secondIndex];
        const auto secondShape = kSecondShapes[secondIndex];
        const auto firstOffset = 12.0 * std::round(v(osc1Octave));
        const auto secondOffset = 12.0 * std::round(v(osc2Octave)) + std::round(v(osc2Semi));
        const auto firstLevel = v(osc1Level);
        const auto secondLevel = v(osc2Level);
        const auto firstThrough = v(osc1Filter) >= 0.5 ? 1.0 : 0.0;
        const auto secondThrough = v(osc2Filter) >= 0.5 ? 1.0 : 0.0;
        const auto wander = std::clamp(v(driftAmount), 0.0, 1.0);
        const auto steep = choice(filterType, 2) == 0;
        const auto velocityLevel = 1.0 - v(ampVelocity) * (1.0 - velocity_);
        const auto master = v(voicingMaster);
        // Each voice's envelopes a little quicker or slower than another's.
        const auto timeScale = 1.0 + 0.12 * wander * timeTolerance_;
        const auto coefficient = [this, timeScale](const double seconds, const double fraction) {
            return std::exp(-static_cast<double>(step) / (std::max(1.0e-4, seconds * timeScale * fraction) * rate_));
        };
        const auto ampAttackStep = coefficient(v(ampAttack), 1.0 / 1.466);
        const auto ampDecayStep = coefficient(v(ampDecay), 1.0 / 4.6);
        const auto ampReleaseStep = coefficient(v(ampRelease), 1.0 / 4.6);
        const auto env2AttackStep = coefficient(v(env2Attack), 1.0 / 1.466);
        const auto env2DecayStep = coefficient(v(env2Decay), 1.0 / 4.6);
        const auto env2ReleaseStep = coefficient(v(env2Release), 1.0 / 4.6);
        // A shorter last step (a block that is not a whole number of steps) moves them only as far.
        const auto partOf = [](const double stepCoefficient, const double fraction) {
            return fraction < 1.0 ? std::pow(stepCoefficient, fraction) : stepCoefficient;
        };
        const auto paceOf = [&](const int rateIndex, const int syncIndex) {
            if (const auto* division = noteDivisionFor(values[static_cast<std::size_t>(syncIndex)]))
                return std::max(20.0, context.tempo) / 60.0 / division->beats;
            return v(rateIndex);
        };
        // The cycle: a rise, a hold, a fall; the tilt gives the rise more or less of it.
        const auto cycling = v(env2Cycle) >= 0.5;
        const auto cycleHz = paceOf(env2Rate, env2Sync);
        const auto holdShare = std::clamp(v(env2Hold), 0.0, 0.95);
        const auto riseShare = (1.0 - holdShare) * std::clamp(0.5 + 0.49 * v(env2Tilt), 0.01, 0.99);
        const auto fallShare = 1.0 - holdShare - riseShare;
        const auto lfoShape = choice(lfo1Shape, lfoShapeCount);
        const auto lfoSynced = noteDivisionFor(values[static_cast<std::size_t>(lfo1Sync)]) != nullptr;
        const auto lfoBaseHz = paceOf(lfo1Rate, lfo1Sync);
        const auto retrigger = v(lfo1Retrigger) >= 0.5;
        const auto bend = static_cast<double>(context.pitchBend) * std::round(v(voicingBend));
        const auto hpf = v(filterHighPass);
        const auto hpfCoefficient = hpf > 20.5 ? std::exp(-2.0 * kPi * std::min(hpf, rate_ * 0.45) / rate_) : 0.0;
        std::array<int, slotCount> slotSources {};
        std::array<int, slotCount> slotDestinations {};
        std::array<double, slotCount> slotAmounts {};
        for (std::size_t slot = 0; slot < slotCount; ++slot) {
            const auto base = static_cast<int>(mod1Source) + 3 * static_cast<int>(slot);
            slotSources[slot] = choice(base, sourceCount);
            slotDestinations[slot] = choice(base + 1, destinationCount);
            slotAmounts[slot] = std::clamp(v(base + 2), -1.0, 1.0);
        }
        // The drift: slow random walks (most of a second to forget), eased.
        const auto walkDecay = std::exp(-static_cast<double>(step) / (0.9 * rate_));
        const auto walkScale = 0.5 * std::sqrt(3.0 * (1.0 - walkDecay * walkDecay));
        const auto walkEase = 1.0 - std::exp(-static_cast<double>(step) / (0.12 * rate_));
        const auto wheel = static_cast<double>(context.modWheel);
        const auto pressure = static_cast<double>(context.aftertouch);
        const auto key = (target_ - 60.0) / 36.0;

        for (int done = 0; done < count; done += step) {
            const auto length = std::min(step, count - done);
            const auto seconds = static_cast<double>(length) / rate_;
            const auto fraction = static_cast<double>(length) / static_cast<double>(step);
            // The slow things, once a step: envelopes, the LFO, the routings, glide, drift, pitches, the filter.
            const auto ampLevel = amp_.step(partOf(ampAttackStep, fraction), partOf(ampDecayStep, fraction), v(ampSustain),
                                            partOf(ampReleaseStep, fraction));
            const auto stagesLevel = env2_.step(partOf(env2AttackStep, fraction), partOf(env2DecayStep, fraction), v(env2Sustain),
                                                partOf(env2ReleaseStep, fraction));
            if (!active())
                return false;
            auto env2Level = stagesLevel;
            if (cycling) {
                cyclePhase_ += cycleHz * seconds;
                cyclePhase_ -= std::floor(cyclePhase_);
                env2Level = cycleLevel(cyclePhase_, riseShare, holdShare, fallShare);
            }
            // What moves the LFO itself reads it as it was a step ago.
            std::array<double, sourceCount> sources { env2Level, lfoLast_, velocity_, wheel, pressure, key };
            auto rateShift = 0.0;
            auto depthShift = 0.0;
            for (std::size_t slot = 0; slot < slotCount; ++slot) {
                const auto moved = sources[static_cast<std::size_t>(slotSources[slot])] * slotAmounts[slot];
                if (slotDestinations[slot] == dstLfoRate)
                    rateShift += moved;
                else if (slotDestinations[slot] == dstLfoAmount)
                    depthShift += moved;
            }
            const auto lfoHz = std::clamp(lfoBaseHz * std::exp2(4.0 * std::clamp(rateShift, -1.5, 1.5)), 0.001, 80.0);
            if (lfoFresh_) {
                lfoFresh_ = false;
                // Free: where one LFO running on the song's clock would be, every voice's together.
                if (!retrigger) {
                    const auto turns = context.clockSeconds * lfoHz;
                    lfoPhase_ = context.clockSeconds > 0.0 ? turns - std::floor(turns) : 0.5 * (random() + 1.0);
                }
            }
            lfoPhase_ += lfoHz * seconds;
            if (lfoPhase_ >= 1.0) {
                lfoPhase_ -= std::floor(lfoPhase_);
                lfoHeld_ = random();
                wanderFrom_ = wanderTo_;
                wanderTo_ = random();
            }
            const auto lfoDepth = std::clamp(v(lfo1Amount) + depthShift, 0.0, 1.0);
            const auto lfo = lfoValue(lfoShape, lfoPhase_, lfoHeld_, wanderFrom_, wanderTo_) * lfoDepth;
            lfoLast_ = lfo;
            sources[srcLfo] = lfo;
            // The slots: the pitch on a curve (fine near nothing, two octaves at the end).
            std::array<double, destinationCount> mods {};
            auto pitchShift = 0.0;
            for (std::size_t slot = 0; slot < slotCount; ++slot) {
                const auto reading = sources[static_cast<std::size_t>(slotSources[slot])];
                const auto amount = slotAmounts[slot];
                if (slotDestinations[slot] == dstPitch)
                    pitchShift += reading * 24.0 * amount * std::abs(amount);
                else
                    mods[static_cast<std::size_t>(slotDestinations[slot])] += reading * amount;
            }
            // The glide: the same time whatever the distance, at an even pace.
            if (glidePerSample_ > 0.0 && pitch_ != target_) {
                const auto move = glidePerSample_ * static_cast<double>(length);
                pitch_ = std::abs(target_ - pitch_) <= move ? target_ : pitch_ + (target_ > pitch_ ? move : -move);
            } else {
                pitch_ = target_;
            }
            const auto basePitch = pitch_ + expressionPitch + bend + pitchShift;
            const auto firstShape = std::clamp(v(osc1Shape) + mods[dstShape1], 0.0, 1.0);
            const auto secondDetune = std::clamp(v(osc2Detune) + 100.0 * mods[dstDetune2], -100.0, 100.0);
            for (int index = 0; index < copies; ++index) {
                auto& copy = copies_[static_cast<std::size_t>(index)];
                // Its drift: each oscillator's own slow walk, and its own tolerance.
                for (std::size_t osc = 0; osc < 2; ++osc) {
                    copy.walk[osc] = copy.walk[osc] * walkDecay + random() * walkScale;
                    copy.eased[osc] += (copy.walk[osc] - copy.eased[osc]) * walkEase;
                }
                const auto place = copies == 1 ? 0.0 : 2.0 * static_cast<double>(index) / static_cast<double>(copies - 1) - 1.0;
                const auto spread = place * thickness * spreadCents;
                const auto firstCents = spread + wander * (4.0 * copy.tolerance[0] + 12.0 * copy.eased[0]);
                const auto secondCents = spread + secondDetune + wander * (4.0 * copy.tolerance[1] + 12.0 * copy.eased[1]);
                copy.increment[0] = std::clamp(440.0 * std::exp2((basePitch + firstOffset + firstCents / 100.0 - 69.0) / 12.0) / rate_, 0.0, 0.45);
                copy.increment[1] = std::clamp(440.0 * std::exp2((basePitch + secondOffset + secondCents / 100.0 - 69.0) / 12.0) / rate_, 0.0, 0.45);
                copy.shaping[0] = shapingFor(firstWave, firstShape, copy.increment[0]);
                copy.shaping[1] = shapingFor(secondWave, secondShape, copy.increment[1]);
                const auto pan = place * thickness;
                copy.left = std::cos((pan + 1.0) * kPi * 0.25) * share;
                copy.right = std::sin((pan + 1.0) * kPi * 0.25) * share;
            }
            // The cutoff: its knob, envelope 2 (six octaves), the LFO (three), the key, the slots, the drift.
            cutoffWalk_ = cutoffWalk_ * walkDecay + random() * walkScale;
            cutoffEased_ += (cutoffWalk_ - cutoffEased_) * walkEase;
            const auto semitones = 12.0 * std::log2(std::max(20.0, v(filterCutoff))) + v(filterEnv) * env2Level * 72.0
                                   + v(filterLfo) * lfo * 36.0 + v(filterKey) * (pitch_ - 60.0) + 72.0 * mods[dstCutoff]
                                   + wander * (cutoffTolerance_ + 4.0 * cutoffEased_);
            const auto cutoff = std::clamp(std::exp2(semitones / 12.0), 20.0, std::min(20'000.0, rate_ * 0.45));
            const auto g = std::tan(kPi * cutoff / rate_);
            const auto resonance = std::clamp(v(filterResonance) + mods[dstResonance], 0.0, 1.0);
            const auto damping = std::max(0.02, 2.0 - 1.98 * resonance);
            const auto a1 = 1.0 / (1.0 + g * (g + damping));
            const auto a2 = g * a1;
            const auto a3 = g * a2;
            const auto noise = std::clamp(v(noiseLevel) + mods[dstNoise], 0.0, 1.0);
            showLfo(0, lfo, lfoPhase_);
            showEnvelope(0, ampLevel);
            showEnvelope(1, env2Level);
            show(filterCutoff, cutoff, v(filterCutoff));
            show(filterResonance, resonance, v(filterResonance));
            show(osc1Shape, firstShape, v(osc1Shape));
            show(osc2Detune, std::clamp(secondDetune, -50.0, 50.0), v(osc2Detune));
            show(noiseLevel, noise, v(noiseLevel));
            show(lfo1Amount, lfoDepth, v(lfo1Amount));
            if (!lfoSynced)
                show(lfo1Rate, std::clamp(lfoHz, 0.02, 30.0), v(lfo1Rate));
            const auto level = ampLevel * velocityLevel * master * std::clamp(1.0 + mods[dstAmp], 0.0, 2.0) * expressionGain * 0.5;
            // The level eased across the step: no steps heard on a quick attack.
            const auto from = previousLevel_;
            previousLevel_ = level;
            for (int i = 0; i < length; ++i) {
                const auto gain = from + (level - from) * static_cast<double>(i + 1) / static_cast<double>(length);
                std::array<double, 2> wet {};
                std::array<double, 2> dry {};
                for (int index = 0; index < copies; ++index) {
                    auto& copy = copies_[static_cast<std::size_t>(index)];
                    const auto one = waveAt(copy.shaping[0], copy.phase[0], copy.increment[0]) * firstLevel;
                    const auto two = waveAt(copy.shaping[1], copy.phase[1], copy.increment[1]) * secondLevel;
                    const auto filtered = one * firstThrough + two * secondThrough;
                    const auto direct = one + two - filtered;
                    wet[0] += filtered * copy.left;
                    wet[1] += filtered * copy.right;
                    dry[0] += direct * copy.left;
                    dry[1] += direct * copy.right;
                    for (std::size_t osc = 0; osc < 2; ++osc) {
                        copy.phase[osc] += copy.increment[osc];
                        if (copy.phase[osc] >= 1.0)
                            copy.phase[osc] -= 1.0;
                    }
                }
                if (noise > 0.0) {
                    const auto hiss = random() * noise * 0.7;
                    wet[0] += hiss;
                    wet[1] += hiss;
                }
                for (std::size_t side = 0; side < 2; ++side) {
                    auto x = wet[side];
                    if (hpfCoefficient > 0.0) {
                        highPass_[side] = x + hpfCoefficient * (highPass_[side] - x);
                        x -= highPass_[side];
                    }
                    x = steep ? ladders_[side].ladder(x, g, resonance) : twoPoles_[side].process(rounded(x), a1, a2, a3);
                    wet[side] = (x + dry[side]) * gain;
                }
                left[done + i] += static_cast<float>(wet[0]);
                right[done + i] += static_cast<float>(wet[1]);
            }
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

    double random() noexcept
    {
        seed_ = seed_ * 1'664'525u + 1'013'904'223u;
        return static_cast<double>(seed_ >> 8) / static_cast<double>(1u << 24) * 2.0 - 1.0;
    }

    // One copy of the voice (one alone, two across the stereo, four in unison).
    struct Copy {
        std::array<double, 2> phase {};
        std::array<double, 2> increment {};
        std::array<Shaping, 2> shaping {};
        std::array<double, 2> tolerance {};
        std::array<double, 2> walk {};
        std::array<double, 2> eased {};
        double left { 0.7071 };
        double right { 0.7071 };
    };

    double rate_ { 48'000.0 };
    double target_ {};
    double pitch_ {};
    double velocity_ { 1.0 };
    Envelope amp_;
    Envelope env2_;
    double cyclePhase_ {};
    std::array<Filter, 2> ladders_ {};
    std::array<TwoPole, 2> twoPoles_ {};
    std::array<double, 2> highPass_ {};
    double lfoPhase_ {};
    double lfoHeld_ {};
    double wanderFrom_ {};
    double wanderTo_ {};
    double lfoLast_ {};
    bool lfoFresh_ {};
    std::array<Copy, copyLimit> copies_ {};
    double cutoffTolerance_ {};
    double cutoffWalk_ {};
    double cutoffEased_ {};
    double timeTolerance_ {};
    double previousLevel_ {};
    double glidePerSample_ {};
    // Each voice its own draw: their drifts and phases apart.
    std::uint32_t seed_ { nextSeed() };
};

} // namespace

std::unique_ptr<Engine> makeDerive()
{
    return std::make_unique<DeriveEngine>();
}

std::vector<float> derivePicture(const Values& values, const std::string_view part, const int points)
{
    // One oscillator over two of its periods, its wave as its shape makes it.
    const auto which = part == "osc1" ? 0 : part == "osc2" ? 1 : -1;
    if (which < 0 || points < 2)
        return {};
    const auto pick = [&values](const int index, const int options) {
        return std::clamp(static_cast<int>(std::lround(values[static_cast<std::size_t>(index)])), 0, options - 1);
    };
    constexpr double dt = 1.0e-4;
    Shaping shaping;
    if (which == 0) {
        shaping = shapingFor(pick(osc1Wave, waveCount), static_cast<double>(values[static_cast<std::size_t>(osc1Shape)]), dt);
    } else {
        const auto second = static_cast<std::size_t>(pick(osc2Wave, static_cast<int>(kSecondWaves.size())));
        shaping = shapingFor(kSecondWaves[second], kSecondShapes[second], dt);
    }
    std::vector<float> result(static_cast<std::size_t>(points));
    for (int at = 0; at < points; ++at) {
        const auto phase = 2.0 * static_cast<double>(at) / static_cast<double>(points - 1);
        result[static_cast<std::size_t>(at)] = static_cast<float>(waveAt(shaping, phase - std::floor(phase), dt));
    }
    return result;
}

} // namespace cydaw::engine::synth::detail
