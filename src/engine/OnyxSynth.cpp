// Onyx: a phase-distortion and phase-modulation synth of the synth family.
// Four oscillators each read a cosine through a bent phase, eight ways of
// bending it (saw, square, pulse, double sine, saw-pulse, and three resonant
// ones where a windowed cosine sings above the note); how far it bends is a
// knob that the oscillator's own envelope, the velocity, the LFO and the
// wheel move. The four modulate one another's phase through a matrix (each
// itself too: feedback), each with its level, which its envelope may shape
// as well. Up to four unison voices across the stereo; one filter of seven
// kinds with its envelope, key tracking and drive; a volume envelope; an LFO
// to the pitch, the bends and the cutoff. Bright, digital, vocal.

#include "SynthEngines.h"
#include "SynthFilters.h"
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

constexpr int oscCount = 4;
constexpr auto oscSize = static_cast<std::size_t>(oscCount);

// One oscillator's settings, in its table's order.
enum Field : int {
    fieldMode,
    fieldAmount,
    fieldLevel,
    fieldRatio,
    fieldFine,
    fieldFixed,
    fieldHz,
    fieldPhase,
    fieldEnvAttack,
    fieldEnvDecay,
    fieldEnvSustain,
    fieldEnvRelease,
    fieldEnvDepth,
    fieldEnvLevel,
    fieldCount
};

// Onyx's settings after the shared voicing, in its table's order.
enum Onyx : int {
    osc1Mode = voicingFieldCount,
    oscLast = osc1Mode + oscCount * fieldCount - 1,
    pitchOctave,
    phaseRandom,
    unisonVoices,
    unisonDetune,
    unisonSpread,
    pm11,
    pmLast = pm11 + oscCount * oscCount - 1,
    filterType,
    filterCutoff,
    filterResonance,
    filterEnv,
    filterKey,
    filterDrive,
    ampAttack,
    ampDecay,
    ampSustain,
    ampRelease,
    ampVelocity,
    distortionVelocity,
    fenvAttack,
    fenvDecay,
    fenvSustain,
    fenvRelease,
    lfoShape,
    lfoRate,
    lfoSync,
    lfoDelay,
    lfoPitch,
    lfoDistortion,
    lfoCutoff,
    modWheel,
    onyxCount
};
static_assert(onyxCount == 107);
static_assert(onyxCount <= valueCapacity);

// The ways an oscillator bends its phase.
enum Mode : int { modeSaw, modeSquare, modePulse, modeDouble, modeSawPulse, modeResonance1, modeResonance2, modeResonance3, modeCount };

using Choices = std::vector<std::pair<std::string, std::string>>;

const Choices kModes { { "Dent de scie", "Saw" },         { "Carré", "Square" },          { "Impulsion", "Pulse" },
                       { "Double sinus", "Double Sine" }, { "Scie-impulsion", "Saw-Pulse" }, { "Résonance I", "Resonance I" },
                       { "Résonance II", "Resonance II" }, { "Résonance III", "Resonance III" } };
const Choices kFilters { { "Ladder", "Ladder" },        { "Passe-bas", "Low-pass" }, { "Passe-bande", "Band-pass" },
                         { "Passe-haut", "High-pass" }, { "Peigne", "Comb" },        { "Voyelle", "Vowel" },
                         { "Hurlement", "Scream" } };
const Choices kLfoShapes { { "Sinus", "Sine" },  { "Triangle", "Triangle" }, { "Dent de scie", "Saw" },
                           { "Carré", "Square" }, { "Aléatoire", "Random" } };

// cos(2 pi x), x in cycles: a polynomial on a quarter of the period (its
// error under 1e-6), no table to make.
inline double cosine(double x) noexcept
{
    x -= std::floor(x);
    if (x > 0.5)
        x = 1.0 - x;
    auto sign = 1.0;
    if (x > 0.25) {
        x = 0.5 - x;
        sign = -1.0;
    }
    const auto z = 2.0 * kPi * x;
    const auto z2 = z * z;
    return sign
           * (1.0 + z2 * (-1.0 / 2.0 + z2 * (1.0 / 24.0 + z2 * (-1.0 / 720.0 + z2 * (1.0 / 40'320.0 + z2 * (-1.0 / 3'628'800.0))))));
}

// How one oscillator bends its phase for a control step.
struct Shape {
    int mode { modeSaw };
    double knee { 0.5 }; // where the phase has run half its cycle (saw, square, saw-pulse)
    double up { 1.0 };   // 0.5 / knee
    double down { 1.0 }; // 0.5 / (1 - knee)
    double hold {};      // the part of the period the phase waits at its start (pulse, saw-pulse)
    double rest { 1.0 }; // 1 / (1 - hold)
    double top { 0.5 };  // double sine: how far the phase runs before it comes back
    double ratio { 1.0 }; // resonance: the windowed cosine's speed
    double offset {};    // its average over a period, taken away
};

// The shape an amount of bending makes (0: a plain cosine, 1: the sharpest),
// no corner narrower than a few samples at this increment (cycles a sample):
// the higher the note, the gentler its sharpest bend.
Shape shapeFor(const int mode, const double amount, const double increment) noexcept
{
    Shape shape;
    shape.mode = std::clamp(mode, 0, modeCount - 1);
    const auto a = std::clamp(amount, 0.0, 1.0);
    const auto narrowest = std::clamp(2.5 * increment, 1.0e-3, 0.5);
    switch (shape.mode) {
    case modeSaw:
    case modeSquare: shape.knee = std::clamp(0.5 * std::exp2(-6.0 * a), narrowest, 0.5); break;
    case modePulse:
        shape.hold = std::clamp(1.0 - std::exp2(-5.0 * a), 0.0, std::max(0.0, 1.0 - 2.0 * narrowest));
        shape.offset = shape.hold;
        break;
    case modeDouble: {
        shape.top = 0.5 + 0.35 * a;
        const auto turn = 2.0 * kPi * shape.top;
        shape.offset = std::sin(turn) / turn;
        break;
    }
    case modeSawPulse:
        shape.hold = std::clamp(0.5 * a, 0.0, std::max(0.0, 1.0 - 4.0 * narrowest));
        shape.knee = std::clamp(0.5 * std::exp2(-6.0 * a), std::min(0.5, narrowest / (1.0 - shape.hold)), 0.5);
        shape.offset = shape.hold;
        break;
    default:
        // Up to four octaves above the note, never past the highest a sample allows.
        shape.ratio = std::clamp(std::exp2(4.0 * a), 1.0, std::max(1.0, 0.45 / std::max(1.0e-9, increment)));
        shape.offset = shape.mode == modeResonance3 ? 0.75 : 0.5;
        break;
    }
    shape.up = 0.5 / shape.knee;
    shape.down = 0.5 / (1.0 - shape.knee);
    shape.rest = 1.0 / std::max(1.0e-6, 1.0 - shape.hold);
    return shape;
}

// One sample of a shape at t (0..1 of its period).
inline double sampleOf(const Shape& shape, const double t) noexcept
{
    switch (shape.mode) {
    case modeSaw: return cosine(t < shape.knee ? t * shape.up : 0.5 + (t - shape.knee) * shape.down);
    case modeSquare: {
        // Half a cycle quickly, a wait, the other half, a wait.
        const auto half = t < 0.5 ? 0.0 : 0.5;
        return cosine(half + std::min(0.5, (t - half) * shape.up));
    }
    case modePulse: return cosine(t < shape.hold ? 0.0 : (t - shape.hold) * shape.rest) - shape.offset;
    case modeDouble: return cosine((t < 0.5 ? t : 1.0 - t) * 2.0 * shape.top) - shape.offset;
    case modeSawPulse: {
        if (t < shape.hold)
            return 1.0 - shape.offset;
        const auto u = (t - shape.hold) * shape.rest;
        return cosine(u < shape.knee ? u * shape.up : 0.5 + (u - shape.knee) * shape.down) - shape.offset;
    }
    default: {
        // A faster cosine started again each period, faded by a window that
        // ends at nothing (a saw, a triangle, a trapezoid): no jump.
        const auto window = shape.mode == modeResonance1   ? 1.0 - t
                            : shape.mode == modeResonance2 ? 1.0 - std::abs(2.0 * t - 1.0)
                                                           : (t < 0.5 ? 1.0 : 2.0 - 2.0 * t);
        return 0.7 * (shape.offset - window * (1.0 - cosine(shape.ratio * t)));
    }
    }
}

// How far a phase modulation moves the phase it reaches (cycles): another
// oscillator's up to one cycle, an oscillator's own less (feedback turns to
// noise sooner). The knob's square: finer at small amounts.
double pmDepth(const double amount, const bool self) noexcept
{
    const auto a = std::clamp(amount, 0.0, 1.0);
    return a * a * (self ? 0.3 : 1.0);
}

} // namespace

std::vector<Section> onyxSections()
{
    return { { "VOIX", "VOICES", 0xffb8c0d0u },
             { "OSC 1", "OSC 1", 0xffff6fa8u },
             { "OSC 2", "OSC 2", 0xffffb04au },
             { "OSC 3", "OSC 3", 0xff5fd8ffu },
             { "OSC 4", "OSC 4", 0xff8dff8au },
             { "HAUTEUR ET UNISSON", "PITCH & UNISON", 0xffc8c8ffu },
             { "MODULATION DE PHASE", "PHASE MODULATION", 0xffd68cffu },
             { "FILTRE", "FILTER", 0xffff7a52u },
             { "ENVELOPPES", "ENVELOPES", 0xff7ad0ffu },
             { "LFO", "LFO", 0xffa58cffu } };
}

std::vector<Parameter> onyxParameters()
{
    Table table;
    // Its voices start in phase (the same attack each time): a chord's first
    // peaks add up, hence a lower volume than the others'.
    addVoicing(table, 16, 0.6f);
    struct Start {
        int mode;
        float amount;
        float level;
        int ratio;
        float fine;
        float depth;
    };
    static constexpr std::array<Start, oscSize> starts { { { modeSaw, 0.25f, 0.7f, 1, 0.0f, 0.35f },
                                                           { modeSaw, 0.2f, 0.45f, 1, 7.0f, 0.25f },
                                                           { modeResonance1, 0.4f, 0.0f, 2, 0.0f, 0.0f },
                                                           { modeDouble, 0.3f, 0.0f, 1, -7.0f, 0.0f } } };
    for (std::size_t osc = 0; osc < oscSize; ++osc) {
        const auto& start = starts[osc];
        const auto number = std::to_string(osc + 1);
        const auto key = "osc" + number + ".";
        const auto name = "Osc " + number + " · ";
        table.section = 1 + static_cast<int>(osc);
        table.choice(key + "mode", name + "distorsion", name + "distortion", kModes, start.mode);
        table.add(key + "amount", name + "quantité de distorsion", name + "distortion amount", {}, 0.0f, 1.0f, start.amount);
        table.add(key + "level", name + "niveau", name + "level", {}, 0.0f, 1.0f, start.level);
        table.whole(key + "ratio", name + "rapport", name + "ratio", {}, 1, 16, start.ratio);
        table.add(key + "fine", name + "fin", name + "fine", "ct", -50.0f, 50.0f, start.fine);
        table.toggle(key + "fixed", name + "fréquence fixe", name + "fixed frequency", false);
        table.add(key + "hz", name + "fréquence", name + "frequency", "Hz", 1.0f, 10'000.0f, 220.0f, Kind::continuous, true);
        table.whole(key + "phase", name + "phase", name + "phase", "°", 0, 360, 0);
        table.add(key + "env.attack", name + "enveloppe · attaque", name + "envelope · attack", "s", 0.001f, 10.0f, 0.002f, Kind::continuous,
                  true);
        table.add(key + "env.decay", name + "enveloppe · déclin", name + "envelope · decay", "s", 0.005f, 10.0f, 0.9f, Kind::continuous, true);
        table.add(key + "env.sustain", name + "enveloppe · maintien", name + "envelope · sustain", {}, 0.0f, 1.0f, 0.3f);
        table.add(key + "env.release", name + "enveloppe · relâchement", name + "envelope · release", "s", 0.005f, 12.0f, 0.5f,
                  Kind::continuous, true);
        table.add(key + "env.depth", name + "enveloppe → distorsion", name + "envelope → distortion", {}, -1.0f, 1.0f, start.depth);
        table.add(key + "env.level", name + "enveloppe → niveau", name + "envelope → level", {}, 0.0f, 1.0f, 0.0f);
    }
    table.section = 5;
    table.whole("octave", "Octave", "Octave", {}, -3, 3, 0);
    table.add("phase.random", "Phase aléatoire", "Random phase", {}, 0.0f, 1.0f, 0.0f);
    table.whole("unison.voices", "Unisson", "Unison", {}, 1, 4, 1);
    table.add("unison.detune", "Unisson · désaccord", "Unison · detune", "ct", 0.0f, 50.0f, 12.0f);
    table.add("unison.spread", "Unisson · largeur", "Unison · spread", {}, 0.0f, 1.0f, 0.7f);
    table.section = 6;
    for (std::size_t from = 0; from < oscSize; ++from)
        for (std::size_t to = 0; to < oscSize; ++to) {
            const auto source = std::to_string(from + 1);
            const auto target = std::to_string(to + 1);
            if (from == to)
                table.add("pm." + source + target, "Rétroaction osc " + source, "Feedback osc " + source, {}, 0.0f, 1.0f, 0.0f);
            else
                table.add("pm." + source + target, "Modulation de phase osc " + source + " → osc " + target,
                          "Phase modulation osc " + source + " → osc " + target, {}, 0.0f, 1.0f, 0.0f);
        }
    table.section = 7;
    table.choice("f.type", "Filtre · type", "Filter · type", kFilters, 1);
    table.add("f.cutoff", "Filtre · fréquence", "Filter · cutoff", "Hz", 20.0f, 20'000.0f, 7'000.0f, Kind::continuous, true);
    table.add("f.resonance", "Filtre · résonance", "Filter · resonance", {}, 0.0f, 1.0f, 0.1f);
    table.add("filter.env", "Filtre · enveloppe", "Filter · envelope", {}, -1.0f, 1.0f, 0.1f);
    table.add("filter.key", "Filtre · suivi du clavier", "Filter · key tracking", {}, 0.0f, 1.0f, 0.3f);
    table.add("filter.drive", "Filtre · saturation", "Filter · drive", {}, 0.0f, 1.0f, 0.0f);
    table.section = 8;
    table.add("amp.attack", "Volume · attaque", "Amp · attack", "s", 0.001f, 10.0f, 0.004f, Kind::continuous, true);
    table.add("amp.decay", "Volume · déclin", "Amp · decay", "s", 0.005f, 10.0f, 0.8f, Kind::continuous, true);
    table.add("amp.sustain", "Volume · maintien", "Amp · sustain", {}, 0.0f, 1.0f, 0.8f);
    table.add("amp.release", "Volume · relâchement", "Amp · release", "s", 0.005f, 12.0f, 0.35f, Kind::continuous, true);
    table.add("amp.velocity", "Volume · vélocité", "Amp · velocity", {}, 0.0f, 1.0f, 0.5f);
    table.add("distortion.velocity", "Vélocité → distorsion", "Velocity → distortion", {}, 0.0f, 1.0f, 0.3f);
    table.add("fenv.attack", "Filtre · attaque", "Filter · attack", "s", 0.001f, 10.0f, 0.002f, Kind::continuous, true);
    table.add("fenv.decay", "Filtre · déclin", "Filter · decay", "s", 0.005f, 10.0f, 0.6f, Kind::continuous, true);
    table.add("fenv.sustain", "Filtre · maintien", "Filter · sustain", {}, 0.0f, 1.0f, 0.3f);
    table.add("fenv.release", "Filtre · relâchement", "Filter · release", "s", 0.005f, 12.0f, 0.4f, Kind::continuous, true);
    table.section = 9;
    table.choice("lfo1.shape", "LFO · forme", "LFO · shape", kLfoShapes, 0);
    table.add("lfo1.rate", "LFO · vitesse", "LFO · rate", "Hz", 0.05f, 20.0f, 5.0f, Kind::continuous, true);
    table.whole("lfo1.sync", "LFO · synchro", "LFO · sync", {}, 0, 12, 0);
    table.add("lfo1.delay", "LFO · retard", "LFO · delay", "s", 0.0f, 3.0f, 0.3f);
    table.add("lfo1.pitch", "LFO → hauteur", "LFO → pitch", {}, 0.0f, 1.0f, 0.0f);
    table.add("lfo1.distortion", "LFO → distorsion", "LFO → distortion", {}, 0.0f, 1.0f, 0.0f);
    table.add("lfo1.cutoff", "LFO → filtre", "LFO → cutoff", {}, 0.0f, 1.0f, 0.0f);
    table.add("mod.wheel", "Molette → distorsion", "Wheel → distortion", {}, 0.0f, 1.0f, 0.4f);
    return table.parameters;
}

namespace {

// One oscillator's settings for a block.
struct Line {
    int mode { modeSaw };
    double amount {};
    double level {};
    double ratio { 1.0 };
    double hz { 220.0 };
    double phase {};
    double depth {};
    double toLevel {};
    bool fixed {};
    double attack {};
    double decay {};
    double sustain {};
    double release {};
};

void restart(Envelope& envelope) noexcept
{
    envelope.stage = Envelope::Stage::idle;
    envelope.level = 0.0;
    envelope.start();
}

class OnyxEngine final : public Engine {
public:
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
        restart(amp_);
        restart(filterEnvelope_);
        for (auto& envelope : distortion_)
            restart(envelope);
        for (auto& filter : filters_)
            filter.clear();
        dcIn_ = {};
        dcOut_ = {};
        elapsed_ = 0.0;
        lfoPhase_ = 0.0;
        lfoHeld_ = random();
        const auto scatter = std::clamp(static_cast<double>(values[phaseRandom]), 0.0, 1.0);
        for (std::size_t sub = 0; sub < subs_.size(); ++sub) {
            auto& unison = subs_[sub];
            for (std::size_t osc = 0; osc < oscSize; ++osc) {
                // The first voice where the phase knobs say (as far aside as
                // the randomness lets it); the others free.
                const auto drawn = 0.5 * (random() + 1.0);
                unison.phase[osc] = sub == 0 ? scatter * drawn : drawn;
                unison.last[osc] = 0.0;
                unison.previous[osc] = 0.0;
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
            for (auto& envelope : distortion_)
                envelope.start();
        }
    }

    void release() noexcept override
    {
        amp_.release();
        filterEnvelope_.release();
        for (auto& envelope : distortion_)
            envelope.release();
    }

    void kill() noexcept override
    {
        amp_.stage = Envelope::Stage::idle;
        amp_.level = 0.0;
        filterEnvelope_.stage = Envelope::Stage::idle;
        for (auto& envelope : distortion_) {
            envelope.stage = Envelope::Stage::idle;
            envelope.level = 0.0;
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
        const auto pick = [&values](const int index, const int options) {
            return std::clamp(static_cast<int>(std::lround(values[static_cast<std::size_t>(index)])), 0, options - 1);
        };
        constexpr int step = 16;
        // Coefficients for a step of `step` samples.
        const auto coefficient = [this](const double seconds, const double fraction) {
            return std::exp(-static_cast<double>(step) / (std::max(1.0e-4, seconds * fraction) * rate_));
        };
        // What holds for the whole block: the oscillators, the matrix, the rest.
        std::array<Line, oscSize> lines {};
        for (std::size_t osc = 0; osc < oscSize; ++osc) {
            const auto base = osc1Mode + static_cast<int>(osc) * fieldCount;
            auto& line = lines[osc];
            line.mode = pick(base + fieldMode, modeCount);
            line.amount = v(base + fieldAmount);
            line.level = std::clamp(v(base + fieldLevel), 0.0, 1.0);
            line.ratio = std::max(1.0, std::round(v(base + fieldRatio))) * std::exp2(v(base + fieldFine) / 1'200.0);
            line.fixed = v(base + fieldFixed) >= 0.5;
            line.hz = std::max(0.0, v(base + fieldHz));
            line.phase = v(base + fieldPhase) / 360.0;
            line.depth = v(base + fieldEnvDepth);
            line.toLevel = std::clamp(v(base + fieldEnvLevel), 0.0, 1.0);
            line.attack = coefficient(v(base + fieldEnvAttack), 1.0 / 1.466);
            line.decay = coefficient(v(base + fieldEnvDecay), 1.0 / 4.6);
            line.sustain = v(base + fieldEnvSustain);
            line.release = coefficient(v(base + fieldEnvRelease), 1.0 / 4.6);
        }
        std::array<std::array<double, oscSize>, oscSize> pmKnobs {}; // [from][to]
        for (std::size_t from = 0; from < oscSize; ++from)
            for (std::size_t to = 0; to < oscSize; ++to)
                pmKnobs[from][to] = pmDepth(v(pm11 + static_cast<int>(from * oscSize + to)), from == to);
        const auto octave = 12.0 * std::round(v(pitchOctave));
        const auto voices = std::clamp(static_cast<int>(std::lround(v(unisonVoices))), 1, 4);
        const auto detune = v(unisonDetune);
        const auto spread = v(unisonSpread);
        const auto kind = pick(filterType, MultiFilter::kindCount);
        const auto resonance = std::clamp(v(filterResonance), 0.0, 1.0);
        const auto driveAmount = std::clamp(v(filterDrive), 0.0, 1.0);
        const auto drive = 1.0 + 4.0 * driveAmount;
        const auto driveBack = 1.0 / (1.0 + 1.5 * driveAmount);
        const auto velocityLevel = 1.0 - v(ampVelocity) * (1.0 - velocity_);
        // Played softer, bent less and modulated less.
        const auto velocityTimbre = 1.0 - v(distortionVelocity) * (1.0 - velocity_);
        const auto master = v(voicingMaster);
        const auto ampAttackStep = coefficient(v(ampAttack), 1.0 / 1.466);
        const auto ampDecayStep = coefficient(v(ampDecay), 1.0 / 4.6);
        const auto ampReleaseStep = coefficient(v(ampRelease), 1.0 / 4.6);
        const auto filterAttackStep = coefficient(v(fenvAttack), 1.0 / 1.466);
        const auto filterDecayStep = coefficient(v(fenvDecay), 1.0 / 4.6);
        const auto filterReleaseStep = coefficient(v(fenvRelease), 1.0 / 4.6);
        const auto lfoHz = [&] {
            if (const auto* division = noteDivisionFor(values[static_cast<std::size_t>(lfoSync)]))
                return std::max(20.0, context.tempo) / 60.0 / division->beats;
            return v(lfoRate);
        }();
        const auto lfoKind = pick(lfoShape, 5);
        const auto bend = static_cast<double>(context.pitchBend) * std::round(v(voicingBend));
        // Up to an octave each way, finer at small amounts.
        const auto vibrato = 12.0 * v(lfoPitch) * v(lfoPitch);
        const auto wheel = static_cast<double>(context.modWheel) * v(modWheel);
        const auto dcCoefficient = 1.0 - 2.0 * kPi * 8.0 / rate_;

        for (int done = 0; done < count; done += step) {
            const auto length = std::min(step, count - done);
            const auto part = static_cast<double>(length) / static_cast<double>(step);
            // The slow things, once a step: envelopes, the LFO, the glide, the bends, the matrix, the pitches and the filter.
            const auto ampLevel = amp_.step(ampAttackStep, ampDecayStep, v(ampSustain), ampReleaseStep, part);
            const auto filterLevel = filterEnvelope_.step(filterAttackStep, filterDecayStep, v(fenvSustain), filterReleaseStep, part);
            std::array<double, oscSize> envelopes {};
            for (std::size_t osc = 0; osc < oscSize; ++osc) {
                const auto& line = lines[osc];
                envelopes[osc] = distortion_[osc].step(line.attack, line.decay, line.sustain, line.release, part);
            }
            if (!active())
                return false;
            const auto seconds = static_cast<double>(length) / rate_;
            elapsed_ += seconds;
            lfoPhase_ += lfoHz * seconds;
            if (lfoPhase_ >= 1.0) {
                lfoPhase_ -= std::floor(lfoPhase_);
                lfoHeld_ = random();
            }
            const auto fade = v(lfoDelay) > 0.0 ? std::min(1.0, elapsed_ / v(lfoDelay)) : 1.0;
            const auto lfo = lfoWave(lfoKind, lfoPhase_, lfoHeld_) * fade;
            // The glide: the same time whatever the distance, at an even pace.
            if (glidePerSample_ > 0.0 && pitch_ != target_) {
                const auto move = glidePerSample_ * static_cast<double>(length);
                pitch_ = std::abs(target_ - pitch_) <= move ? target_ : pitch_ + (target_ > pitch_ ? move : -move);
            } else {
                pitch_ = target_;
            }
            const auto basePitch = pitch_ + expressionPitch + bend + octave + lfo * vibrato;
            const auto noteHz = 440.0 * std::exp2((basePitch - 69.0) / 12.0);
            // Each oscillator's bend (its knob, its envelope, the LFO, the
            // wheel, the velocity) and its share (its envelope may shape it).
            std::array<Shape, oscSize> shapes {};
            std::array<double, oscSize> levels {};
            std::array<double, oscSize> shares {};
            for (std::size_t osc = 0; osc < oscSize; ++osc) {
                const auto& line = lines[osc];
                const auto index = osc1Mode + static_cast<int>(osc) * fieldCount;
                const auto amount = std::clamp(line.amount + line.depth * envelopes[osc] + lfo * v(lfoDistortion) + wheel, 0.0, 1.0) * velocityTimbre;
                const auto increment = (line.fixed ? line.hz : noteHz * line.ratio) / rate_;
                shapes[osc] = shapeFor(line.mode, amount, increment);
                shares[osc] = 1.0 - line.toLevel + line.toLevel * envelopes[osc];
                levels[osc] = line.level * shares[osc];
                show(index + fieldAmount, amount, line.amount);
                show(index + fieldLevel, levels[osc], line.level);
            }
            // The matrix: what each sends, as its share and the velocity say;
            // an oscillator neither heard nor sending is left out.
            std::array<std::array<double, oscSize>, oscSize> pm {};
            std::array<bool, oscSize> playing {};
            for (std::size_t from = 0; from < oscSize; ++from) {
                const auto scale = std::max(0.0, shares[from] * velocityTimbre);
                auto sends = false;
                for (std::size_t to = 0; to < oscSize; ++to) {
                    pm[from][to] = pmKnobs[from][to] * scale;
                    sends = sends || (to != from && pm[from][to] > 0.0);
                    const auto key = pm11 + static_cast<int>(from * oscSize + to);
                    show(key, v(key) * std::sqrt(scale), v(key));
                }
                playing[from] = levels[from] > 1.0e-6 || sends;
            }
            for (int sub = 0; sub < voices; ++sub) {
                auto& unison = subs_[static_cast<std::size_t>(sub)];
                const auto place = voices == 1 ? 0.0 : 2.0 * static_cast<double>(sub) / static_cast<double>(voices - 1) - 1.0;
                const auto apart = std::exp2(place * detune / 1'200.0);
                for (std::size_t osc = 0; osc < oscSize; ++osc) {
                    const auto& line = lines[osc];
                    unison.increment[osc] = std::clamp((line.fixed ? line.hz : noteHz * line.ratio) * apart / rate_, 0.0, 0.45);
                }
                const auto pan = place * spread;
                unison.left = std::cos((pan + 1.0) * kPi * 0.25);
                unison.right = std::sin((pan + 1.0) * kPi * 0.25);
            }
            // The cutoff: its knob, the envelope (6 octaves), the LFO (3), the key.
            const auto semitones = 12.0 * std::log2(std::max(20.0, v(filterCutoff))) + v(filterEnv) * filterLevel * 72.0 + lfo * v(lfoCutoff) * 36.0
                                   + v(filterKey) * (target_ - 60.0);
            const auto cutoff = std::clamp(std::exp2(semitones / 12.0), 20.0, std::min(20'000.0, rate_ * 0.45));
            const auto coefficients = MultiFilter::coefficients(kind, cutoff, resonance, rate_, filters_[0].combSize());
            showLfo(0, lfo, lfoPhase_);
            showEnvelope(0, ampLevel);
            showEnvelope(1, filterLevel);
            show(filterCutoff, cutoff, v(filterCutoff));
            const auto level = ampLevel * velocityLevel * master * expressionGain * 0.5 / std::sqrt(static_cast<double>(voices));
            // The level eased across the step: no steps heard on a quick attack.
            const auto levelFrom = previousLevel_;
            previousLevel_ = level;
            for (int i = 0; i < length; ++i) {
                const auto gain = levelFrom + (level - levelFrom) * static_cast<double>(i + 1) / static_cast<double>(length);
                auto sumLeft = 0.0;
                auto sumRight = 0.0;
                for (int sub = 0; sub < voices; ++sub) {
                    auto& unison = subs_[static_cast<std::size_t>(sub)];
                    // Each oscillator as the others hear it: its last two samples
                    // averaged (a feedback that does not whistle at Nyquist).
                    std::array<double, oscSize> heard {};
                    for (std::size_t osc = 0; osc < oscSize; ++osc)
                        heard[osc] = 0.5 * (unison.last[osc] + unison.previous[osc]);
                    std::array<double, oscSize> now {};
                    auto mixed = 0.0;
                    for (std::size_t to = 0; to < oscSize; ++to) {
                        if (playing[to]) {
                            auto t = unison.phase[to] + lines[to].phase + pm[0][to] * heard[0] + pm[1][to] * heard[1] + pm[2][to] * heard[2]
                                     + pm[3][to] * heard[3];
                            t -= std::floor(t);
                            now[to] = sampleOf(shapes[to], t);
                            mixed += levels[to] * now[to];
                        }
                        unison.phase[to] += unison.increment[to];
                        if (unison.phase[to] >= 1.0)
                            unison.phase[to] -= 1.0;
                    }
                    unison.previous = unison.last;
                    unison.last = now;
                    sumLeft += mixed * unison.left;
                    sumRight += mixed * unison.right;
                }
                std::array<double, 2> channel { sumLeft, sumRight };
                for (std::size_t side = 0; side < 2; ++side) {
                    auto x = channel[side];
                    // No offset left by a long hold or a feedback.
                    auto blocked = x - dcIn_[side] + dcCoefficient * dcOut_[side];
                    if (std::abs(blocked) < 1.0e-20)
                        blocked = 0.0;
                    dcIn_[side] = x;
                    dcOut_[side] = blocked;
                    x = blocked;
                    if (driveAmount > 1.0e-4)
                        x = std::tanh(x * drive) * driveBack;
                    x = filters_[side].process(kind, coefficients, x);
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

    struct Unison {
        std::array<double, oscSize> phase {};
        std::array<double, oscSize> increment {};
        std::array<double, oscSize> last {};
        std::array<double, oscSize> previous {};
        double left { 0.7071 };
        double right { 0.7071 };
    };

    double rate_ { 48'000.0 };
    double target_ {};
    double pitch_ {};
    double velocity_ { 1.0 };
    double elapsed_ {};
    Envelope amp_;
    Envelope filterEnvelope_;
    std::array<Envelope, oscSize> distortion_ {};
    std::array<MultiFilter, 2> filters_ {};
    std::array<double, 2> dcIn_ {};
    std::array<double, 2> dcOut_ {};
    double lfoPhase_ {};
    double lfoHeld_ {};
    std::array<Unison, 4> subs_ {};
    double previousLevel_ {};
    double glidePerSample_ {};
    // Each voice its own draw: its unison phases apart.
    std::uint32_t seed_ { nextSeed() };
};

} // namespace

std::unique_ptr<Engine> makeOnyx()
{
    return std::make_unique<OnyxEngine>();
}

std::vector<float> onyxPicture(const Values& values, const std::string_view part, const int points)
{
    // One oscillator ("osc1".."osc4") as it sounds among the others (its
    // bend, the phase modulations it receives), or what the four make with
    // their levels ("out"): two periods of a middle C (the octave knob
    // included) at 48 kHz, the envelopes at their peak.
    auto which = -1;
    if (part == "osc1")
        which = 0;
    else if (part == "osc2")
        which = 1;
    else if (part == "osc3")
        which = 2;
    else if (part == "osc4")
        which = 3;
    else if (part != "out")
        return {};
    if (points < 2)
        return {};
    const auto v = [&values](const int index) { return static_cast<double>(values[static_cast<std::size_t>(index)]); };
    constexpr double rate = 48'000.0;
    const auto noteHz = 261.6255653005986 * std::exp2(std::clamp(std::round(v(pitchOctave)), -3.0, 3.0));
    const auto period = std::clamp(static_cast<int>(std::lround(rate / noteHz)), 16, 4'096);
    std::array<Shape, oscSize> shapes {};
    std::array<double, oscSize> increments {};
    std::array<double, oscSize> offsets {};
    std::array<double, oscSize> levels {};
    for (std::size_t osc = 0; osc < oscSize; ++osc) {
        const auto base = osc1Mode + static_cast<int>(osc) * fieldCount;
        const auto ratio = std::max(1.0, std::round(v(base + fieldRatio))) * std::exp2(v(base + fieldFine) / 1'200.0);
        const auto hz = v(base + fieldFixed) >= 0.5 ? std::max(0.0, v(base + fieldHz)) : noteHz * ratio;
        increments[osc] = std::clamp(hz / rate, 0.0, 0.45);
        const auto mode = std::clamp(static_cast<int>(std::lround(v(base + fieldMode))), 0, modeCount - 1);
        shapes[osc] = shapeFor(mode, v(base + fieldAmount), increments[osc]);
        offsets[osc] = v(base + fieldPhase) / 360.0;
        levels[osc] = std::clamp(v(base + fieldLevel), 0.0, 1.0);
    }
    std::array<std::array<double, oscSize>, oscSize> pm {};
    for (std::size_t from = 0; from < oscSize; ++from)
        for (std::size_t to = 0; to < oscSize; ++to)
            pm[from][to] = pmDepth(v(pm11 + static_cast<int>(from * oscSize + to)), from == to);
    // A period to settle the feedbacks, then the two drawn.
    std::array<double, oscSize> phases {};
    std::array<double, oscSize> last {};
    std::array<double, oscSize> previous {};
    std::vector<double> wave(static_cast<std::size_t>(2 * period + 1));
    for (int at = 0; at <= 3 * period; ++at) {
        std::array<double, oscSize> heard {};
        for (std::size_t osc = 0; osc < oscSize; ++osc)
            heard[osc] = 0.5 * (last[osc] + previous[osc]);
        std::array<double, oscSize> now {};
        auto mixed = 0.0;
        for (std::size_t to = 0; to < oscSize; ++to) {
            auto t = phases[to] + offsets[to];
            for (std::size_t from = 0; from < oscSize; ++from)
                t += pm[from][to] * heard[from];
            t -= std::floor(t);
            now[to] = sampleOf(shapes[to], t);
            mixed += levels[to] * now[to];
            phases[to] += increments[to];
            phases[to] -= std::floor(phases[to]);
        }
        previous = last;
        last = now;
        if (at >= period)
            wave[static_cast<std::size_t>(at - period)] = which < 0 ? mixed : now[static_cast<std::size_t>(which)];
    }
    // The sum fills the picture; an oscillator keeps its own scale.
    auto scale = 1.0;
    if (which < 0) {
        auto peak = 0.0;
        for (const auto sample : wave)
            peak = std::max(peak, std::abs(sample));
        scale = peak > 1.0e-6 ? 1.0 / peak : 1.0;
    }
    std::vector<float> result(static_cast<std::size_t>(points));
    for (int at = 0; at < points; ++at) {
        const auto position = static_cast<double>(at) * static_cast<double>(2 * period) / static_cast<double>(points - 1);
        const auto lower = std::min(static_cast<std::size_t>(position), wave.size() - 2);
        const auto fraction = position - static_cast<double>(lower);
        result[static_cast<std::size_t>(at)] = static_cast<float>((wave[lower] + (wave[lower + 1] - wave[lower]) * fraction) * scale);
    }
    return result;
}

} // namespace cydaw::engine::synth::detail
