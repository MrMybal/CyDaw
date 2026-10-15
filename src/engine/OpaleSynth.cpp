// Opale: the everyday polysynth of the synth family, subtractive, with deep
// oscillators and a creative mixer. Two oscillators, each a continuous shape
// from sine through triangle, saw and square to a narrow pulse (its steps
// smoothed by polyBLEP), a level, a sub an octave under it (sine to square),
// a hard sync against a hidden master at its own pitch, up to seven unison
// voices across the stereo and a phase free-running or restarted on each
// note; a mixer that joins them in six ways (mixed, ring and amplitude
// modulation, their difference, a digital sign, a wipe across the cycle, the
// larger of the two), with a noise of any colour, an analog drift, a drive and
// the voice's own output fed back; then one filter of the family's seven
// kinds and its drive; a filter envelope (that can also move the pitch or the
// sync), an amp envelope, one LFO and the mod wheel.

#include "SynthEngines.h"
#include "SynthFilters.h"
#include "SynthParts.h"

#include <cydaw/engine/EffectCatalogue.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cydaw::engine::synth::detail {

namespace {

using namespace parts;

// One oscillator's settings, in its table's order (after osc1Octave or osc2Octave).
enum OscField : int { oOctave, oPitch, oFine, oShape, oLevel, oSub, oSubShape, oSync, oUnison, oDetune, oSpread, oRetrigger, oscFieldCount };

// Opale's settings after the shared voicing, in its table's order.
enum Opale : int {
    osc1Octave = voicingFieldCount,
    osc2Octave = osc1Octave + oscFieldCount,
    mixMode = osc2Octave + oscFieldCount,
    mixBalance,
    noiseLevel,
    noiseColour,
    mixDrive,
    mixFeedback,
    driftAmount,
    pitchEnvAmount,
    pitchEnvTarget,
    filterType,
    filterCutoff,
    filterResonance,
    filterEnv,
    filterKey,
    filterVelocity,
    filterDrive,
    ampAttack,
    ampDecay,
    ampSustain,
    ampRelease,
    ampVelocity,
    fenvAttack,
    fenvDecay,
    fenvSustain,
    fenvRelease,
    lfoShape,
    lfoRate,
    lfoSync,
    lfoDelay,
    lfoPitch,
    lfoMorph,
    lfoBalance,
    lfoCutoff,
    lfoAmp,
    wheelVibrato,
    wheelCutoff,
    opaleCount
};
static_assert(opaleCount == 66);

// How the mixer joins the two oscillators; what the filter envelope's pitch moves.
enum MixMode : int { mixPlain, mixAm, mixNegative, mixSign, mixWipe, mixMax, mixModeCount };
enum PitchTarget : int { targetBoth, targetOsc2, targetSync, targetCount };

constexpr int maxUnison = 7;
// A square as loud as a saw, more or less; a narrow pulse a little louder (it is thinner).
constexpr double kSquareLevel = 0.65;
constexpr double kRoot2 = 1.4142135623730951;

using Choices = std::vector<std::pair<std::string, std::string>>;

const Choices kFeet { { "32'", "32'" }, { "16'", "16'" }, { "8'", "8'" }, { "4'", "4'" }, { "2'", "2'" } };
const Choices kMixModes { { "Mélange", "Mix" }, { "AM", "AM" },    { "Négatif", "Negative" },
                          { "Signe", "Sign" },  { "Volet", "Wipe" }, { "Max", "Max" } };
const Choices kTargets { { "Osc 1 + 2", "Osc 1 + 2" }, { "Osc 2", "Osc 2" }, { "Synchro", "Sync" } };
const Choices kFilters { { "Ladder", "Ladder" },        { "Passe-bas", "Low-pass" }, { "Passe-bande", "Band-pass" },
                         { "Passe-haut", "High-pass" }, { "Peigne", "Comb" },        { "Voyelle", "Vowel" },
                         { "Hurlement", "Scream" } };
const Choices kLfoShapes { { "Sinus", "Sine" }, { "Triangle", "Triangle" }, { "Dent de scie", "Saw" }, { "Carré", "Square" },
                           { "Aléatoire", "Random" } };

constexpr int oscBase(const std::size_t osc) noexcept
{
    return osc == 0 ? static_cast<int>(osc1Octave) : static_cast<int>(osc2Octave);
}

// A unison voice's place among the others, -1..1 (the middle for one alone).
double placeOf(const int voice, const int voices) noexcept
{
    return voices > 1 ? 2.0 * static_cast<double>(voice) / static_cast<double>(voices - 1) - 1.0 : 0.0;
}

double hertz(const double note) noexcept
{
    return 440.0 * std::pow(2.0, (note - 69.0) / 12.0);
}

// A shape: how much of each wave, all four in phase with the sine (so that
// their fundamentals add as the shape moves across them), and the pulse's width.
struct Shape {
    double sine {};
    double triangle {};
    double saw {};
    double pulse {};
    double width { 0.5 };
};

// The shape knob, 0..1: sine, triangle, saw, square, then a pulse narrowing.
Shape shapeOf(const double morph) noexcept
{
    Shape result;
    const auto at = std::clamp(morph, 0.0, 1.0) * 4.0;
    const auto segment = std::min(static_cast<int>(at), 3);
    const auto mix = at - static_cast<double>(segment);
    switch (segment) {
    case 0:
        result.sine = 1.0 - mix;
        result.triangle = mix;
        break;
    case 1:
        result.triangle = 1.0 - mix;
        result.saw = mix;
        break;
    case 2:
        result.saw = 1.0 - mix;
        result.pulse = mix * kSquareLevel;
        break;
    default:
        result.pulse = kSquareLevel + 0.3 * mix;
        result.width = 0.5 - 0.42 * mix;
        break;
    }
    return result;
}

// A sub's shape: sine to square.
Shape subShapeOf(const double morph) noexcept
{
    Shape result;
    const auto amount = std::clamp(morph, 0.0, 1.0);
    result.sine = 1.0 - amount;
    result.pulse = 0.8 * amount;
    return result;
}

// A shape at phase t (0..1), its steps left sharp: the sine and the triangle
// rise from nothing at 0, the saw from nothing to its top at 0.5 (where it
// falls), the pulse high until its width (its average kept at nothing).
double naive(const Shape& shape, const double t) noexcept
{
    auto y = 0.0;
    if (shape.sine > 0.0)
        y += shape.sine * std::sin(2.0 * kPi * t);
    if (shape.triangle > 0.0) {
        auto u = t + 0.75;
        if (u >= 1.0)
            u -= 1.0;
        y += shape.triangle * (4.0 * std::abs(u - 0.5) - 1.0);
    }
    if (shape.saw > 0.0) {
        auto u = t + 0.5;
        if (u >= 1.0)
            u -= 1.0;
        y += shape.saw * (2.0 * u - 1.0);
    }
    if (shape.pulse > 0.0)
        y += shape.pulse * ((t < shape.width ? 1.0 : -1.0) - (2.0 * shape.width - 1.0));
    return y;
}

// A step of `height` that fell `since` samples before this one (0..1): its
// polyBLEP correction, on the sample before (still open) and on this one.
void blep(const double height, const double since, double& before, double& after) noexcept
{
    const auto e = std::clamp(since, 0.0, 1.0);
    before += 0.5 * height * e * e;
    after -= 0.5 * height * (1.0 - e) * (1.0 - e);
}

// The steps a shape's phase crosses going from `from` (0..1) to `to` (less
// than one period further), starting `start` into the sample (0..1) at
// `increment` a sample.
void crossings(const Shape& shape, const double from, const double to, const double start, const double increment, double& before,
               double& after) noexcept
{
    if (to <= from || increment <= 0.0)
        return;
    const auto at = [&](const double phase, const double height) {
        if (phase > from && phase <= to)
            blep(height, 1.0 - (start + (phase - from) / increment), before, after);
    };
    if (shape.pulse > 0.0) {
        at(1.0, 2.0 * shape.pulse);
        at(shape.width, -2.0 * shape.pulse);
        at(shape.width + 1.0, -2.0 * shape.pulse);
    }
    if (shape.saw > 0.0) {
        at(0.5, -2.0 * shape.saw);
        at(1.5, -2.0 * shape.saw);
    }
}

// One wave: the phase heard, the hidden master that restarts it when synced,
// the sample before this one (its correction still open), its pitch and place.
struct Wave {
    double master {};
    double slave {};
    double pending {};
    double masterIncrement {};
    double slaveIncrement {};
    double left { 1.0 };
    double right { 1.0 };
    double drift {};
    double driftTarget {};
};

// One sample of a wave, a sample late: every step it takes (its own, and the
// restart when the master comes round) smoothed by polyBLEP on both sides.
double advance(Wave& wave, const Shape& shape, const bool synced) noexcept
{
    auto before = 0.0;
    auto after = 0.0;
    const auto increment = wave.slaveIncrement;
    if (!synced) {
        const auto to = wave.slave + increment;
        crossings(shape, wave.slave, to, 0.0, increment, before, after);
        wave.slave = to >= 1.0 ? to - 1.0 : to;
        // In step with it: a sync turned on starts from here.
        wave.master = wave.slave;
    } else {
        const auto masterIncrement = std::max(1.0e-9, wave.masterIncrement);
        wave.master += masterIncrement;
        if (wave.master >= 1.0) {
            wave.master -= 1.0;
            // The master came round `since` samples ago: the wave heard
            // runs there, then starts again from nothing.
            const auto since = std::clamp(wave.master / masterIncrement, 0.0, 1.0);
            const auto reach = 1.0 - since;
            const auto upTo = wave.slave + increment * reach;
            crossings(shape, wave.slave, upTo, 0.0, increment, before, after);
            blep(naive(shape, 0.0) - naive(shape, upTo - std::floor(upTo)), since, before, after);
            const auto rest = increment * since;
            crossings(shape, 0.0, rest, reach, increment, before, after);
            wave.slave = rest;
        } else {
            const auto to = wave.slave + increment;
            crossings(shape, wave.slave, to, 0.0, increment, before, after);
            wave.slave = to >= 1.0 ? to - 1.0 : to;
        }
    }
    const auto now = naive(shape, wave.slave) + after;
    const auto out = wave.pending + before;
    wave.pending = now;
    return out;
}

// The two oscillators joined: the balance goes from osc 1 alone to osc 2
// alone through the mode's own blend of both in the middle.
double mixOscillators(const int mode, const double a, const double heard, const double balance, const double reference) noexcept
{
    // Negative: the mix with osc 2 upside down (the same alone, their difference together).
    const auto b = mode == mixNegative ? -heard : heard;
    if (mode == mixWipe) {
        // Osc 2 across the middle of each of osc 1's cycles, the wider the
        // more the balance leans to it; its edges soft.
        constexpr double soft = 0.15;
        const auto triangle = 1.0 - std::abs(2.0 * reference - 1.0);
        const auto edge = balance * (1.0 + soft) - 0.5 * soft;
        const auto amount = std::clamp((triangle - (1.0 - edge)) / soft + 0.5, 0.0, 1.0);
        return a + (b - a) * amount;
    }
    double both {};
    switch (mode) {
    case mixAm: both = 1.6 * a * b; break;
    case mixSign: both = std::abs(a) * b / (std::abs(b) + 0.03); break;
    case mixMax: both = std::max(a, b); break;
    default: both = 0.7 * (a + b); break;
    }
    return balance <= 0.5 ? a + 2.0 * balance * (both - a) : both + (2.0 * balance - 1.0) * (b - both);
}

} // namespace

std::vector<Section> opaleSections()
{
    return { { "VOIX", "VOICES", 0xffb8c0d0u },         { "OSCILLATEURS", "OSCILLATORS", 0xfff2a6e8u },
             { "MÉLANGEUR", "MIXER", 0xffc4a3ffu },     { "FILTRE", "FILTER", 0xffff7f8fu },
             { "ENVELOPPES", "ENVELOPES", 0xff8fe3d0u }, { "LFO", "LFO", 0xffffd27au } };
}

std::vector<Parameter> opaleParameters()
{
    Table table;
    addVoicing(table, 16, 0.7f);
    table.section = 1;
    for (int index = 1; index <= 2; ++index) {
        const auto prefix = "osc" + std::to_string(index) + ".";
        const auto name = "Osc " + std::to_string(index) + " · ";
        table.choice(prefix + "octave", name + "octave", name + "octave", kFeet, 2);
        table.add(prefix + "pitch", name + "hauteur", name + "pitch", "st", -24.0f, 24.0f, 0.0f);
        table.add(prefix + "fine", name + "fin", name + "fine", "ct", -50.0f, 50.0f, index == 1 ? 0.0f : 7.0f);
        table.add(prefix + "shape", name + "forme", name + "shape", {}, 0.0f, 1.0f, 0.5f);
        table.add(prefix + "level", name + "niveau", name + "level", {}, 0.0f, 1.0f, 1.0f);
        table.add(prefix + "sub", name + "sub", name + "sub", {}, 0.0f, 1.0f, 0.0f);
        table.add(prefix + "subshape", name + "forme du sub", name + "sub shape", {}, 0.0f, 1.0f, 0.0f);
        table.add(prefix + "hardsync", name + "synchro", name + "hard sync", "st", 0.0f, 36.0f, 0.0f);
        table.whole(prefix + "unison", name + "unisson", name + "unison", {}, 1, maxUnison, 1);
        table.add(prefix + "detune", name + "désaccord", name + "detune", "ct", 0.0f, 100.0f, 20.0f);
        table.add(prefix + "spread", name + "largeur", name + "spread", {}, 0.0f, 1.0f, 0.7f);
        table.toggle(prefix + "retrigger", name + "phase redémarrée", name + "phase restart", false);
    }
    table.section = 2;
    table.choice("mix.mode", "Mélangeur · mode", "Mixer · mode", kMixModes, mixPlain);
    table.add("mix.balance", "Mélangeur · balance", "Mixer · balance", {}, 0.0f, 1.0f, 0.5f);
    table.add("noise.level", "Bruit", "Noise", {}, 0.0f, 1.0f, 0.0f);
    table.add("noise.colour", "Bruit · couleur", "Noise · colour", {}, -1.0f, 1.0f, 0.0f);
    table.add("mix.drive", "Mélangeur · saturation", "Mixer · drive", {}, 0.0f, 1.0f, 0.0f);
    table.add("mix.feedback", "Réinjection", "Feedback", {}, 0.0f, 1.0f, 0.0f);
    table.add("drift", "Dérive", "Drift", {}, 0.0f, 1.0f, 0.2f);
    table.add("pitchenv.amount", "Env. filtre → hauteur", "Filter env → pitch", "st", -24.0f, 24.0f, 0.0f);
    table.choice("pitchenv.target", "Env. filtre → hauteur · cible", "Filter env → pitch · target", kTargets, targetBoth);
    table.section = 3;
    table.choice("f.type", "Filtre · type", "Filter · type", kFilters, MultiFilter::ladder);
    table.add("f.cutoff", "Filtre · fréquence", "Filter · cutoff", "Hz", 20.0f, 20'000.0f, 2'200.0f, Kind::continuous, true);
    table.add("f.resonance", "Filtre · résonance", "Filter · resonance", {}, 0.0f, 1.0f, 0.2f);
    table.add("filter.env", "Filtre · enveloppe", "Filter · envelope", {}, -1.0f, 1.0f, 0.3f);
    table.add("filter.key", "Filtre · suivi du clavier", "Filter · key tracking", {}, 0.0f, 1.0f, 0.5f);
    table.add("filter.velocity", "Filtre · vélocité", "Filter · velocity", {}, 0.0f, 1.0f, 0.3f);
    table.add("filter.drive", "Filtre · saturation", "Filter · drive", {}, 0.0f, 1.0f, 0.0f);
    table.section = 4;
    table.add("amp.attack", "Volume · attaque", "Amp · attack", "s", 0.001f, 10.0f, 0.004f, Kind::continuous, true);
    table.add("amp.decay", "Volume · déclin", "Amp · decay", "s", 0.005f, 10.0f, 0.6f, Kind::continuous, true);
    table.add("amp.sustain", "Volume · maintien", "Amp · sustain", {}, 0.0f, 1.0f, 0.8f);
    table.add("amp.release", "Volume · relâchement", "Amp · release", "s", 0.005f, 12.0f, 0.35f, Kind::continuous, true);
    table.add("amp.velocity", "Volume · vélocité", "Amp · velocity", {}, 0.0f, 1.0f, 0.5f);
    table.add("fenv.attack", "Filtre · attaque", "Filter · attack", "s", 0.001f, 10.0f, 0.002f, Kind::continuous, true);
    table.add("fenv.decay", "Filtre · déclin", "Filter · decay", "s", 0.005f, 10.0f, 0.6f, Kind::continuous, true);
    table.add("fenv.sustain", "Filtre · maintien", "Filter · sustain", {}, 0.0f, 1.0f, 0.3f);
    table.add("fenv.release", "Filtre · relâchement", "Filter · release", "s", 0.005f, 12.0f, 0.4f, Kind::continuous, true);
    table.section = 5;
    table.choice("lfo1.shape", "LFO · forme", "LFO · shape", kLfoShapes, 1);
    table.add("lfo1.rate", "LFO · vitesse", "LFO · rate", "Hz", 0.05f, 20.0f, 5.0f, Kind::continuous, true);
    table.whole("lfo1.sync", "LFO · synchro", "LFO · sync", {}, 0, 12, 0);
    table.add("lfo1.delay", "LFO · retard", "LFO · delay", "s", 0.0f, 3.0f, 0.0f);
    table.add("lfo1.pitch", "LFO → hauteur", "LFO → pitch", {}, 0.0f, 1.0f, 0.0f);
    table.add("lfo1.morph", "LFO → formes", "LFO → shapes", {}, 0.0f, 1.0f, 0.0f);
    table.add("lfo1.balance", "LFO → balance", "LFO → balance", {}, 0.0f, 1.0f, 0.0f);
    table.add("lfo1.cutoff", "LFO → filtre", "LFO → cutoff", {}, 0.0f, 1.0f, 0.0f);
    table.add("lfo1.amp", "LFO → volume", "LFO → volume", {}, 0.0f, 1.0f, 0.0f);
    table.add("mod.vibrato", "Molette → vibrato", "Wheel → vibrato", {}, 0.0f, 1.0f, 0.4f);
    table.add("mod.cutoff", "Molette → filtre", "Wheel → cutoff", {}, -1.0f, 1.0f, 0.0f);
    return table.parameters;
}

namespace {

class OpaleEngine final : public Engine {
public:
    OpaleEngine()
    {
        // Free-running from the first note: each wave somewhere of its own.
        for (auto& waves : waves_)
            for (auto& wave : waves) {
                wave.slave = 0.5 * (random() + 1.0);
                wave.master = wave.slave;
                wave.driftTarget = random();
            }
        for (auto& sub : subs_) {
            sub.slave = 0.5 * (random() + 1.0);
            sub.master = sub.slave;
        }
    }

    void prepare(const double sampleRate) override
    {
        rate_ = std::max(1.0, sampleRate);
        for (auto& filter : filters_)
            filter.prepare(rate_);
    }

    void start(const Values& values, const int note, const float velocity, const double glideFrom, const double sampleRate) noexcept override
    {
        rate_ = std::max(1.0, sampleRate);
        target_ = static_cast<double>(note);
        pitch_ = glideFrom >= 0.0 && values[voicingGlide] > 0.0f ? glideFrom : target_;
        aimGlide(values);
        velocity_ = std::clamp(static_cast<double>(velocity), 0.0, 1.0);
        for (auto* envelope : { &amp_, &filterEnvelope_ }) {
            envelope->stage = Envelope::Stage::idle;
            envelope->level = 0.0;
            envelope->start();
        }
        for (auto& filter : filters_)
            filter.clear();
        dcIn_ = {};
        dcOut_ = {};
        last_ = {};
        noiseState_ = 0.0;
        elapsed_ = 0.0;
        lfoPhase_ = 0.0;
        lfoHeld_ = random();
        vibratoPhase_ = 0.0;
        balance_ = std::clamp(static_cast<double>(values[static_cast<std::size_t>(mixBalance)]), 0.0, 1.0);
        for (std::size_t osc = 0; osc < 2; ++osc) {
            const auto restart = values[static_cast<std::size_t>(oscBase(osc) + oRetrigger)] >= 0.5f;
            for (std::size_t voice = 0; voice < waves_[osc].size(); ++voice) {
                auto& wave = waves_[osc][voice];
                wave.drift = random() * 0.5;
                wave.driftTarget = random();
                if (restart) {
                    // The same start each note; the unison's others a fixed way apart.
                    const auto phase = static_cast<double>(voice) * 0.381966;
                    wave.slave = phase - std::floor(phase);
                    wave.master = wave.slave;
                    wave.pending = 0.0;
                }
            }
            if (restart) {
                subs_[osc].slave = 0.0;
                subs_[osc].master = 0.0;
                subs_[osc].pending = 0.0;
                if (osc == 0)
                    reference_ = 0.0;
            }
        }
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
            filterEnvelope_.start();
        }
    }

    void release() noexcept override
    {
        amp_.release();
        filterEnvelope_.release();
    }

    void kill() noexcept override
    {
        for (auto* envelope : { &amp_, &filterEnvelope_ }) {
            envelope->stage = Envelope::Stage::idle;
            envelope->level = 0.0;
        }
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
        // What holds for the whole block: each oscillator's tuning, unison and sub.
        std::array<double, 2> offsets {};
        std::array<int, 2> unisons {};
        std::array<double, 2> detunes {};
        std::array<double, 2> scales {};
        std::array<double, 2> levels {};
        std::array<double, 2> subLevels {};
        std::array<Shape, 2> subShapes {};
        for (std::size_t osc = 0; osc < 2; ++osc) {
            const auto base = oscBase(osc);
            offsets[osc] = 12.0 * static_cast<double>(choice(base + oOctave, 5) - 2) + v(base + oPitch) + v(base + oFine) / 100.0;
            unisons[osc] = std::clamp(static_cast<int>(std::lround(v(base + oUnison))), 1, maxUnison);
            detunes[osc] = std::clamp(v(base + oDetune), 0.0, 100.0);
            levels[osc] = std::clamp(v(base + oLevel), 0.0, 1.0);
            scales[osc] = levels[osc] / std::sqrt(static_cast<double>(unisons[osc]));
            subLevels[osc] = std::clamp(v(base + oSub), 0.0, 1.0);
            subShapes[osc] = subShapeOf(v(base + oSubShape));
            // Each unison voice its place across the stereo (the middle as loud as one voice alone).
            const auto spread = std::clamp(v(base + oSpread), 0.0, 1.0);
            for (int voice = 0; voice < unisons[osc]; ++voice) {
                auto& wave = waves_[osc][static_cast<std::size_t>(voice)];
                const auto pan = placeOf(voice, unisons[osc]) * spread;
                wave.left = std::cos((pan + 1.0) * kPi * 0.25) * kRoot2;
                wave.right = std::sin((pan + 1.0) * kPi * 0.25) * kRoot2;
            }
        }
        const auto mode = choice(mixMode, mixModeCount);
        // The noise: darkened by a low-pass (its lows brought up a little), or brightened by a high-pass.
        const auto colour = std::clamp(v(noiseColour), -1.0, 1.0);
        const auto bright = colour > 0.0;
        const auto noiseHz = std::min(rate_ * 0.45, bright ? 30.0 * std::pow(2.0, 7.5 * colour) : 18'000.0 * std::pow(2.0, 7.0 * colour));
        const auto noiseCoefficient = 1.0 - std::exp(-2.0 * kPi * noiseHz / rate_);
        const auto noiseGain = std::clamp(v(noiseLevel), 0.0, 1.0) * 0.6
                               * (bright ? 1.0 + 0.3 * colour : std::pow((2.0 - noiseCoefficient) / std::max(1.0e-6, noiseCoefficient), 0.3));
        const auto mixDriven = std::clamp(v(mixDrive), 0.0, 1.0);
        const auto mixGain = 1.0 + 6.0 * mixDriven;
        const auto mixBack = 1.0 / (1.0 + 1.5 * mixDriven);
        // The voice's own output back into the mixer: saturated, never more than it can hold.
        const auto feedback = std::clamp(v(mixFeedback), 0.0, 1.0) * 0.8;
        const auto drift = std::clamp(v(driftAmount), 0.0, 1.0);
        const auto pitchTarget = choice(pitchEnvTarget, targetCount);
        const auto kind = choice(filterType, MultiFilter::kindCount);
        const auto resonance = std::clamp(v(filterResonance), 0.0, 1.0);
        const auto filterDriven = std::clamp(v(filterDrive), 0.0, 1.0);
        const auto filterGain = 1.0 + 4.0 * filterDriven;
        const auto filterBack = 1.0 / (1.0 + 1.2 * filterDriven);
        const auto velocityLevel = 1.0 - v(ampVelocity) * (1.0 - velocity_);
        const auto velocityFilter = 1.0 - v(filterVelocity) * (1.0 - velocity_);
        const auto master = v(voicingMaster);
        // Coefficients for a step of `step` samples.
        const auto coefficient = [this](const double seconds, const double fraction) {
            return std::exp(-static_cast<double>(step) / (std::max(1.0e-4, seconds * fraction) * rate_));
        };
        const auto ampAttackStep = coefficient(v(ampAttack), 1.0 / 1.466);
        const auto ampDecayStep = coefficient(v(ampDecay), 1.0 / 4.6);
        const auto ampReleaseStep = coefficient(v(ampRelease), 1.0 / 4.6);
        const auto filterAttackStep = coefficient(v(fenvAttack), 1.0 / 1.466);
        const auto filterDecayStep = coefficient(v(fenvDecay), 1.0 / 4.6);
        const auto filterReleaseStep = coefficient(v(fenvRelease), 1.0 / 4.6);
        const auto lfoHz = [&]() -> double {
            if (const auto* division = noteDivisionFor(values[static_cast<std::size_t>(lfoSync)]))
                return std::max(20.0, context.tempo) / 60.0 / division->beats;
            return v(lfoRate);
        }();
        const auto lfoForm = choice(lfoShape, 5);
        const auto lfoDelaySeconds = v(lfoDelay);
        // Vibrato in semitones: fine near nothing, an octave at the top.
        const auto lfoPitchDepth = 12.0 * v(lfoPitch) * v(lfoPitch);
        const auto bend = static_cast<double>(context.pitchBend) * std::round(v(voicingBend));
        const auto wheel = std::clamp(static_cast<double>(context.modWheel), 0.0, 1.0);
        const auto dcCoefficient = std::exp(-2.0 * kPi * 8.0 / rate_);

        for (int done = 0; done < count; done += step) {
            const auto length = std::min(step, count - done);
            // The slow things, once a step: envelopes, the LFO, the glide, the pitches, the shapes and the filter.
            const auto ampLevel = amp_.step(ampAttackStep, ampDecayStep, v(ampSustain), ampReleaseStep);
            const auto filterLevel = filterEnvelope_.step(filterAttackStep, filterDecayStep, v(fenvSustain), filterReleaseStep);
            if (!active())
                return false;
            const auto seconds = static_cast<double>(length) / rate_;
            elapsed_ += seconds;
            lfoPhase_ += lfoHz * seconds;
            if (lfoPhase_ >= 1.0) {
                lfoPhase_ -= std::floor(lfoPhase_);
                lfoHeld_ = random();
            }
            const auto fade = lfoDelaySeconds > 0.0 ? std::min(1.0, elapsed_ / lfoDelaySeconds) : 1.0;
            const auto lfo = lfoWave(lfoForm, lfoPhase_, lfoHeld_) * fade;
            // The wheel's vibrato: its own sine, whatever the LFO does.
            vibratoPhase_ += 5.5 * seconds;
            vibratoPhase_ -= std::floor(vibratoPhase_);
            const auto vibrato = std::sin(2.0 * kPi * vibratoPhase_) * wheel * v(wheelVibrato);
            // The glide: the same time whatever the distance, at an even pace.
            if (glidePerSample_ > 0.0 && pitch_ != target_) {
                const auto move = glidePerSample_ * static_cast<double>(length);
                pitch_ = std::abs(target_ - pitch_) <= move ? target_ : pitch_ + (target_ > pitch_ ? move : -move);
            } else {
                pitch_ = target_;
            }
            const auto common = pitch_ + expressionPitch + bend + lfo * lfoPitchDepth + vibrato;
            const auto swept = filterLevel * v(pitchEnvAmount);
            // The balance: its knob and the LFO, eased (no steps heard as it moves).
            const auto aimed = std::clamp(v(mixBalance) + lfo * v(lfoBalance) * 0.5, 0.0, 1.0);
            balance_ += (aimed - balance_) * 0.3;
            const auto balance = balance_;
            std::array<Shape, 2> shapes {};
            std::array<bool, 2> synced {};
            std::array<bool, 2> playing {};
            for (std::size_t osc = 0; osc < 2; ++osc) {
                const auto base = oscBase(osc);
                const auto moved = pitchTarget == targetBoth || (pitchTarget == targetOsc2 && osc == 1);
                const auto pitch = common + offsets[osc] + (moved ? swept : 0.0);
                const auto morph = std::clamp(v(base + oShape) + lfo * v(lfoMorph) * 0.5, 0.0, 1.0);
                shapes[osc] = shapeOf(morph);
                const auto sync = std::clamp(v(base + oSync) + (pitchTarget == targetSync ? swept : 0.0), 0.0, 48.0);
                synced[osc] = sync > 0.01;
                const auto ratio = std::pow(2.0, sync / 12.0);
                // Not heard (its level nothing, or the balance all the other way): not played.
                playing[osc] = levels[osc] > 0.0 && (osc == 0 ? balance < 1.0 - 1.0e-4 : balance > 1.0e-4);
                const auto voices = unisons[osc];
                for (int voice = 0; voice < voices; ++voice) {
                    auto& wave = waves_[osc][static_cast<std::size_t>(voice)];
                    wave.drift += (wave.driftTarget - wave.drift) * 0.02;
                    if (random() > 0.985)
                        wave.driftTarget = random();
                    const auto cents = placeOf(voice, voices) * detunes[osc] + wave.drift * drift * 10.0;
                    const auto increment = std::clamp(hertz(pitch + cents / 100.0) / rate_, 1.0e-7, 0.45);
                    wave.masterIncrement = increment;
                    wave.slaveIncrement = synced[osc] ? std::min(0.45, increment * ratio) : increment;
                }
                // The sub: an octave under the oscillator's own pitch, in the middle.
                const auto own = hertz(pitch + waves_[osc][0].drift * drift * 0.1) / rate_;
                subs_[osc].masterIncrement = std::clamp(own * 0.5, 1.0e-7, 0.45);
                subs_[osc].slaveIncrement = subs_[osc].masterIncrement;
                if (osc == 0)
                    referenceIncrement_ = std::clamp(own, 1.0e-7, 0.45);
                show(base + oShape, morph, v(base + oShape));
                show(base + oSync, sync, v(base + oSync));
            }
            // The cutoff: its knob, the envelope (six octaves, as the velocity lets it), the
            // velocity, the LFO (three), the key, the wheel (four).
            const auto semitones = 12.0 * std::log2(std::max(20.0, v(filterCutoff))) + v(filterEnv) * filterLevel * 72.0 * velocityFilter
                                   + v(filterVelocity) * (velocity_ - 1.0) * 12.0 + lfo * v(lfoCutoff) * 36.0
                                   + v(filterKey) * (pitch_ - 60.0) + wheel * v(wheelCutoff) * 48.0;
            const auto cutoff = std::clamp(std::pow(2.0, semitones / 12.0), 20.0, std::min(20'000.0, rate_ * 0.45));
            const auto coefficients = MultiFilter::coefficients(kind, cutoff, resonance, rate_, filters_[0].combSize());
            showLfo(0, lfo, lfoPhase_);
            showEnvelope(0, ampLevel);
            showEnvelope(1, filterLevel);
            show(filterCutoff, cutoff, v(filterCutoff));
            show(mixBalance, aimed, v(mixBalance));
            const auto tremolo = 1.0 - v(lfoAmp) * 0.5 * (fade - lfo);
            const auto level = ampLevel * velocityLevel * master * tremolo * expressionGain * 0.45;
            // The level eased across the step: no steps heard on a quick attack.
            const auto from = previousLevel_;
            previousLevel_ = level;
            // Each sub as loud as its oscillator is in the balance.
            const std::array<double, 2> subGains { subLevels[0] * std::min(1.0, 2.0 * (1.0 - balance)),
                                                   subLevels[1] * std::min(1.0, 2.0 * balance) };
            for (int i = 0; i < length; ++i) {
                const auto gain = from + (level - from) * static_cast<double>(i + 1) / static_cast<double>(length);
                // The two oscillators, their unison across the stereo.
                std::array<std::array<double, 2>, 2> sound {};
                for (std::size_t osc = 0; osc < 2; ++osc) {
                    if (!playing[osc])
                        continue;
                    auto sumLeft = 0.0;
                    auto sumRight = 0.0;
                    for (int voice = 0; voice < unisons[osc]; ++voice) {
                        auto& wave = waves_[osc][static_cast<std::size_t>(voice)];
                        const auto x = advance(wave, shapes[osc], synced[osc]);
                        sumLeft += x * wave.left;
                        sumRight += x * wave.right;
                    }
                    sound[osc] = { sumLeft * scales[osc], sumRight * scales[osc] };
                }
                auto under = 0.0;
                for (std::size_t osc = 0; osc < 2; ++osc)
                    if (subGains[osc] > 0.0)
                        under += subGains[osc] * advance(subs_[osc], subShapes[osc], false);
                // Where osc 1 is in its cycle (the wipe follows it).
                reference_ += referenceIncrement_;
                if (reference_ >= 1.0)
                    reference_ -= 1.0;
                auto hiss = 0.0;
                if (noiseGain > 0.0) {
                    const auto white = random();
                    noiseState_ += (white - noiseState_) * noiseCoefficient;
                    hiss = (bright ? white - noiseState_ : noiseState_) * noiseGain;
                }
                std::array<double, 2> channel {};
                for (std::size_t side = 0; side < 2; ++side) {
                    auto x = mixOscillators(mode, sound[0][side], sound[1][side], balance, reference_) + under + hiss;
                    if (feedback > 0.0)
                        x += feedback * std::tanh(1.2 * last_[side]);
                    // No offset into the filter (a max, a sign, the feedback leave some).
                    const auto blocked = x - dcIn_[side] + dcCoefficient * dcOut_[side];
                    dcIn_[side] = x;
                    dcOut_[side] = blocked;
                    x = blocked;
                    if (mixDriven > 1.0e-3)
                        x = std::tanh(x * mixGain) * mixBack;
                    x = filters_[side].process(kind, coefficients, x);
                    if (filterDriven > 1.0e-3)
                        x = std::tanh(x * filterGain) * filterBack;
                    last_[side] = x;
                    channel[side] = x * gain;
                }
                left[done + i] += static_cast<float>(channel[0]);
                right[done + i] += static_cast<float>(channel[1]);
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

    // Each voice its own draw: their drifts and phases apart.
    std::uint32_t seed_ { nextSeed() };
    double rate_ { 48'000.0 };
    double target_ {};
    double pitch_ {};
    double velocity_ { 1.0 };
    double elapsed_ {};
    Envelope amp_;
    Envelope filterEnvelope_;
    std::array<std::array<Wave, maxUnison>, 2> waves_ {};
    std::array<Wave, 2> subs_ {};
    std::array<MultiFilter, 2> filters_ {};
    std::array<double, 2> dcIn_ {};
    std::array<double, 2> dcOut_ {};
    std::array<double, 2> last_ {};
    double noiseState_ {};
    double reference_ {};
    double referenceIncrement_ {};
    double balance_ { 0.5 };
    double lfoPhase_ {};
    double lfoHeld_ {};
    double vibratoPhase_ {};
    double previousLevel_ {};
    double glidePerSample_ {};
};

} // namespace

std::unique_ptr<Engine> makeOpale()
{
    return std::make_unique<OpaleEngine>();
}

std::vector<float> opalePicture(const Values& values, const std::string_view part, const int points)
{
    // One oscillator over two of its periods, as it plays (its shape, its
    // sync, its level) with its sub (one period of it).
    const auto index = part == "osc1" ? 0 : part == "osc2" ? 1 : -1;
    if (index < 0 || points < 2)
        return {};
    const auto base = oscBase(static_cast<std::size_t>(index));
    const auto v = [&values](const int at) { return static_cast<double>(values[static_cast<std::size_t>(at)]); };
    const auto shape = shapeOf(v(base + oShape));
    const auto sub = subShapeOf(v(base + oSubShape));
    const auto level = std::clamp(v(base + oLevel), 0.0, 1.0);
    const auto subLevel = std::clamp(v(base + oSub), 0.0, 1.0);
    const auto ratio = std::pow(2.0, std::clamp(v(base + oSync), 0.0, 48.0) / 12.0);
    std::vector<float> result(static_cast<std::size_t>(points));
    for (int at = 0; at < points; ++at) {
        const auto phase = 2.0 * static_cast<double>(at) / static_cast<double>(points - 1);
        const auto within = phase - std::floor(phase);
        const auto heard = within * ratio - std::floor(within * ratio);
        auto x = level * naive(shape, heard);
        if (subLevel > 0.0) {
            const auto half = 0.5 * phase;
            x += subLevel * naive(sub, half - std::floor(half));
        }
        result[static_cast<std::size_t>(at)] = static_cast<float>(x);
    }
    return result;
}

} // namespace cydaw::engine::synth::detail
