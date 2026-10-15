// Quartz: a four-operator FM synthesiser of the synth family, compact and
// immediate. Four sine operators, each at a ratio to the note (or at a
// frequency of its own) with its fine tuning, its own envelope, its share in
// the output and a depth for all it modulates; a free matrix where any
// operator modulates any other and itself (the diagonal: its feedback), and a
// noise that can shake each of them. All of it runs at twice the rate and is
// brought back down through a half-band filter; each modulation is held under
// what would fold back. Then a filter of seven kinds with its envelope, the
// volume's envelope, an LFO (pitch, depths, cutoff, volume) and the wheel
// (vibrato, depths).

#include "SynthEngines.h"
#include "SynthFilters.h"
#include "SynthParts.h"

#include <cydaw/engine/EffectCatalogue.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cydaw::engine::synth::detail {

namespace {

using namespace parts;

constexpr std::size_t operatorCount = 4;

// Each operator's settings in its table's order; then what it sends to
// operators 1 to 4 (to itself: its feedback).
enum Field : int {
    ratioField,
    fixedField,
    hzField,
    fineField,
    levelField,
    depthField,
    attackField,
    decayField,
    sustainField,
    releaseField,
    sendField,
    fieldCount = sendField + 4
};

// Quartz's settings after the shared voicing, in its table's order.
enum Quartz : int {
    operatorFirst = voicingFieldCount,
    operatorLast = operatorFirst + fieldCount * 4 - 1,
    noise1,
    noise4 = noise1 + 3,
    noiseTone,
    ratioSnap,
    velocityDepth,
    keyDepth,
    keyTime,
    fType,
    fCutoff,
    fResonance,
    filterDrive,
    filterEnv,
    filterKey,
    filterVelocity,
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
    lfoDepth,
    lfoCutoff,
    lfoAmp,
    wheelVibrato,
    wheelDepth,
    quartzCount
};
static_assert(quartzCount == 97);

// An operator's setting: operator 0..3, a field above.
constexpr int setting(const int op, const int field) noexcept
{
    return operatorFirst + op * fieldCount + field;
}

using Row = std::array<double, operatorCount>;
// What each operator hears from each: [to][from].
using Matrix = std::array<Row, operatorCount>;

// An amount of one at full depth: the index of a modulation (radians, the
// amount squared: finer near nothing), the most feedback (stable), the
// noise's.
constexpr double indexRange = 8.0;
constexpr double feedbackRange = kPi;
constexpr double noiseRange = 4.0;
// One voice's sum brought to about -1..1 at the usual volume.
constexpr double outputGain = 0.4;

using Choices = std::vector<std::pair<std::string, std::string>>;

const Choices kQuartzFilters { { "Ladder", "Ladder" },        { "Passe-bas", "Low-pass" }, { "Passe-bande", "Band-pass" },
                               { "Passe-haut", "High-pass" }, { "Peigne", "Comb" },        { "Voyelle", "Vowel" },
                               { "Hurlement", "Scream" } };
const Choices kQuartzLfoShapes { { "Sinus", "Sine" },   { "Triangle", "Triangle" }, { "Dent de scie", "Saw" },
                                 { "Carré", "Square" }, { "Aléatoire", "Random" } };

// sin(2 pi t) for any t: brought into -1/2..1/2, folded into -1/4..1/4, then
// a polynomial (its error under 1e-7).
inline double sineCycles(double t) noexcept
{
    const auto shifted = t + 0.5;
    auto whole = static_cast<double>(static_cast<std::int64_t>(shifted));
    if (whole > shifted)
        whole -= 1.0;
    t = shifted - whole - 0.5;
    if (t > 0.25)
        t = 0.5 - t;
    else if (t < -0.25)
        t = -0.5 - t;
    const auto x = t * (2.0 * kPi);
    const auto x2 = x * x;
    return x
           * (1.0
              + x2 * (-1.0 / 6.0 + x2 * (1.0 / 120.0 + x2 * (-1.0 / 5'040.0 + x2 * (1.0 / 362'880.0 + x2 * (-1.0 / 39'916'800.0))))));
}

// A ratio brought to a whole one; below one, to 1/2, 1/4 or 1/8.
inline double wholeRatio(const double ratio) noexcept
{
    if (ratio >= 1.0)
        return std::max(1.0, std::round(ratio));
    return std::exp2(std::round(std::log2(std::max(0.125, ratio))));
}

// The ratio an operator plays: its knob's, whole when the ratios are.
double playedRatio(const Values& values, const int op) noexcept
{
    const auto ratio = std::clamp(static_cast<double>(values[static_cast<std::size_t>(setting(op, ratioField))]), 0.125, 16.0);
    return values[static_cast<std::size_t>(ratioSnap)] >= 0.5f ? wholeRatio(ratio) : ratio;
}

// An operator's frequency: its ratio to the note, or its own; its fine tuning.
double operatorHz(const Values& values, const int op, const double noteHz) noexcept
{
    const auto at = [&values](const int index) { return static_cast<double>(values[static_cast<std::size_t>(index)]); };
    const auto fine = std::exp2(std::clamp(at(setting(op, fineField)), -100.0, 100.0) / 1'200.0);
    if (at(setting(op, fixedField)) >= 0.5)
        return std::clamp(at(setting(op, hzField)), 0.01, 24'000.0) * fine;
    return noteHz * playedRatio(values, op) * fine;
}

// Feedback grows harsh as it climbs: less of it the higher the operator.
inline double feedbackScale(const double hz) noexcept
{
    return 1.0 / (1.0 + std::max(0.0, hz) / 5'000.0);
}

// The four operators as they play: their phases (cycles), their paces, their
// last two outputs.
struct Operators {
    Row phase {};
    Row increment {};
    Row output {};
    Row older {};

    // One sample: the highest operator first, each hearing the ones already
    // played this sample and the others as they were the sample before; its
    // own feedback the mean of its last two outputs (steadier). Offsets in
    // cycles; `gains` each one's envelope.
    void play(const Matrix& into, const Row& feedback, const Row& noiseIndex, const double shake, const Row& gains) noexcept
    {
        for (std::size_t to = operatorCount; to-- > 0;) {
            auto offset = feedback[to] * 0.5 * (output[to] + older[to]) + noiseIndex[to] * shake;
            for (std::size_t from = 0; from < operatorCount; ++from)
                offset += into[to][from] * output[from];
            const auto value = gains[to] * sineCycles(phase[to] + offset);
            older[to] = output[to];
            output[to] = value;
            phase[to] += increment[to];
            if (phase[to] >= 1.0)
                phase[to] -= 1.0;
        }
    }
};

// The half-band low-pass that brings the doubled rate back down: 47 taps
// (Kaiser window), its centre one half, every other tap nothing; these are
// the odd ones out from the centre.
constexpr std::size_t halfBandLength = 47;
constexpr std::size_t halfBandCentre = 23;
constexpr std::size_t halfBandTaps = 12;

std::array<double, halfBandTaps> halfBand() noexcept
{
    const auto bessel = [](const double x) {
        auto sum = 1.0;
        auto term = 1.0;
        for (int k = 1; k < 32; ++k) {
            const auto ratio = x / (2.0 * static_cast<double>(k));
            term *= ratio * ratio;
            sum += term;
        }
        return sum;
    };
    constexpr double beta = 7.8;
    std::array<double, halfBandTaps> taps {};
    auto total = 0.0;
    for (std::size_t m = 0; m < halfBandTaps; ++m) {
        const auto distance = static_cast<double>(2 * m + 1);
        const auto along = distance / static_cast<double>(halfBandCentre);
        const auto window = bessel(beta * std::sqrt(std::max(0.0, 1.0 - along * along))) / bessel(beta);
        taps[m] = std::sin(kPi * distance * 0.5) / (kPi * distance) * window;
        total += 2.0 * taps[m];
    }
    // Its sum one: nothing lost at the bottom.
    for (auto& tap : taps)
        tap *= 0.5 / total;
    return taps;
}

struct Steps {
    double attack {};
    double decay {};
    double sustain {};
    double release {};
};

} // namespace

std::vector<Section> quartzSections()
{
    return { { "VOIX", "VOICES", 0xffc0b8c8u },           { "OPÉRATEUR 1", "OPERATOR 1", 0xffff8fc8u },
             { "OPÉRATEUR 2", "OPERATOR 2", 0xff8fd8ffu }, { "OPÉRATEUR 3", "OPERATOR 3", 0xffb6f28au },
             { "OPÉRATEUR 4", "OPERATOR 4", 0xffffc46bu }, { "FM", "FM", 0xffd7a8ffu },
             { "FILTRE", "FILTER", 0xffff6f7du },          { "ENVELOPPES", "ENVELOPES", 0xff7ae0d0u },
             { "MODULATION", "MODULATION", 0xffa58cffu } };
}

std::vector<Parameter> quartzParameters()
{
    Table table;
    addVoicing(table, 16, 0.6f);
    static constexpr std::array<float, operatorCount> ratios { 1.0f, 1.0f, 2.0f, 3.0f };
    static constexpr std::array<float, operatorCount> fixedHz { 110.0f, 440.0f, 1'200.0f, 3'200.0f };
    static constexpr std::array<float, operatorCount> levels { 0.8f, 0.0f, 0.0f, 0.0f };
    static constexpr std::array<float, operatorCount> decays { 1.5f, 1.2f, 1.0f, 0.8f };
    static constexpr std::array<float, operatorCount> sustains { 0.7f, 0.45f, 0.5f, 0.5f };
    for (std::size_t op = 0; op < operatorCount; ++op) {
        const auto number = std::to_string(op + 1);
        const auto key = "op" + number + ".";
        const auto name = "Op " + number + " · ";
        table.section = static_cast<int>(op) + 1;
        table.add(key + "ratio", name + "rapport", name + "ratio", {}, 0.125f, 16.0f, ratios[op], Kind::continuous, true);
        table.toggle(key + "fixed", name + "fréquence fixe", name + "fixed frequency", false);
        table.add(key + "hz", name + "fréquence", name + "frequency", "Hz", 0.5f, 12'000.0f, fixedHz[op], Kind::continuous, true);
        table.add(key + "fine", name + "fin", name + "fine", "ct", -100.0f, 100.0f, 0.0f);
        table.add(key + "level", name + "niveau", name + "level", {}, 0.0f, 1.0f, levels[op]);
        table.add(key + "depth", name + "profondeur", name + "depth", {}, 0.0f, 1.0f, 1.0f);
        table.add(key + "attack", name + "attaque", name + "attack", "s", 0.001f, 10.0f, 0.002f, Kind::continuous, true);
        table.add(key + "decay", name + "déclin", name + "decay", "s", 0.005f, 20.0f, decays[op], Kind::continuous, true);
        table.add(key + "sustain", name + "maintien", name + "sustain", {}, 0.0f, 1.0f, sustains[op]);
        table.add(key + "release", name + "relâchement", name + "release", "s", 0.005f, 20.0f, 0.4f, Kind::continuous, true);
        // What it sends to each operator: m.<from><to>.
        for (std::size_t to = 0; to < operatorCount; ++to) {
            const auto target = std::to_string(to + 1);
            const auto self = to == op;
            table.add("m." + number + target, "Matrice · " + number + " → " + target + (self ? " (réinjection)" : ""),
                      "Matrix · " + number + " → " + target + (self ? " (feedback)" : ""), {}, 0.0f, 1.0f,
                      op == 1 && to == 0 ? 0.35f : 0.0f);
        }
    }
    table.section = 5;
    for (std::size_t to = 0; to < operatorCount; ++to) {
        const auto target = std::to_string(to + 1);
        table.add("m.n" + target, "Matrice · bruit → " + target, "Matrix · noise → " + target, {}, 0.0f, 1.0f, 0.0f);
    }
    table.add("noise.tone", "Bruit · timbre", "Noise · tone", {}, 0.0f, 1.0f, 0.7f);
    table.toggle("ratio.snap", "Rapports entiers", "Whole ratios", true);
    table.add("velocity.depth", "Vélocité → profondeurs", "Velocity → depths", {}, 0.0f, 1.0f, 0.5f);
    table.add("key.depth", "Clavier → profondeurs", "Key → depths", {}, 0.0f, 1.0f, 0.3f);
    table.add("key.time", "Clavier → durées des opérateurs", "Key → operator times", {}, 0.0f, 1.0f, 0.0f);
    table.section = 6;
    table.choice("f.type", "Filtre · type", "Filter · type", kQuartzFilters, 1);
    table.add("f.cutoff", "Filtre · fréquence", "Filter · cutoff", "Hz", 20.0f, 20'000.0f, 9'000.0f, Kind::continuous, true);
    table.add("f.resonance", "Filtre · résonance", "Filter · resonance", {}, 0.0f, 1.0f, 0.1f);
    table.add("filter.drive", "Filtre · saturation", "Filter · drive", {}, 0.0f, 1.0f, 0.0f);
    table.add("filter.env", "Filtre · enveloppe", "Filter · envelope", {}, -1.0f, 1.0f, 0.0f);
    table.add("filter.key", "Filtre · suivi du clavier", "Filter · key tracking", {}, 0.0f, 1.0f, 0.3f);
    table.add("filter.velocity", "Filtre · vélocité", "Filter · velocity", {}, 0.0f, 1.0f, 0.2f);
    table.section = 7;
    table.add("amp.attack", "Volume · attaque", "Amp · attack", "s", 0.001f, 10.0f, 0.002f, Kind::continuous, true);
    table.add("amp.decay", "Volume · déclin", "Amp · decay", "s", 0.005f, 20.0f, 1.0f, Kind::continuous, true);
    table.add("amp.sustain", "Volume · maintien", "Amp · sustain", {}, 0.0f, 1.0f, 1.0f);
    table.add("amp.release", "Volume · relâchement", "Amp · release", "s", 0.005f, 20.0f, 0.35f, Kind::continuous, true);
    table.add("amp.velocity", "Volume · vélocité", "Amp · velocity", {}, 0.0f, 1.0f, 0.5f);
    table.add("fenv.attack", "Filtre · attaque", "Filter · attack", "s", 0.001f, 10.0f, 0.002f, Kind::continuous, true);
    table.add("fenv.decay", "Filtre · déclin", "Filter · decay", "s", 0.005f, 20.0f, 0.6f, Kind::continuous, true);
    table.add("fenv.sustain", "Filtre · maintien", "Filter · sustain", {}, 0.0f, 1.0f, 0.3f);
    table.add("fenv.release", "Filtre · relâchement", "Filter · release", "s", 0.005f, 20.0f, 0.4f, Kind::continuous, true);
    table.section = 8;
    table.choice("lfo1.shape", "LFO · forme", "LFO · shape", kQuartzLfoShapes, 0);
    table.add("lfo1.rate", "LFO · vitesse", "LFO · rate", "Hz", 0.05f, 20.0f, 5.0f, Kind::continuous, true);
    table.whole("lfo1.sync", "LFO · synchro", "LFO · sync", {}, 0, 12, 0);
    table.add("lfo1.delay", "LFO · retard", "LFO · delay", "s", 0.0f, 3.0f, 0.2f);
    table.add("lfo1.pitch", "LFO → hauteur", "LFO → pitch", {}, 0.0f, 1.0f, 0.0f);
    table.add("lfo1.depth", "LFO → profondeurs", "LFO → depths", {}, 0.0f, 1.0f, 0.0f);
    table.add("lfo1.cutoff", "LFO → filtre", "LFO → cutoff", {}, 0.0f, 1.0f, 0.0f);
    table.add("lfo1.amp", "LFO → volume", "LFO → volume", {}, 0.0f, 1.0f, 0.0f);
    table.add("wheel.vibrato", "Molette → vibrato", "Wheel → vibrato", {}, 0.0f, 1.0f, 0.4f);
    table.add("wheel.depth", "Molette → profondeurs", "Wheel → depths", {}, 0.0f, 1.0f, 0.3f);
    return table.parameters;
}

namespace {

class QuartzEngine final : public Engine {
public:
    QuartzEngine() : taps_(halfBand()) {}

    void prepare(const double sampleRate) override
    {
        rate_ = std::max(1.0, sampleRate);
        filter_.prepare(rate_);
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
        for (auto& envelope : envelopes_)
            restart(envelope);
        // Each note its operators in the same relation (the same attack every
        // time), from its own moment of their common cycle: the notes of a
        // chord do not all peak together.
        operators_ = {};
        startOffset_ = 0.5 * (random() + 1.0);
        gainsFrom_ = {};
        intoFrom_ = {};
        feedbackFrom_ = {};
        fresh_ = true;
        noise_ = 0.0;
        filter_.clear();
        history_ = {};
        write_ = 0;
        dcIn_ = 0.0;
        dcOut_ = 0.0;
        elapsed_ = 0.0;
        lfoPhase_ = 0.0;
        lfoHeld_ = random();
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
            for (auto& envelope : envelopes_)
                envelope.start();
        }
    }

    void release() noexcept override
    {
        amp_.release();
        filterEnvelope_.release();
        for (auto& envelope : envelopes_)
            envelope.release();
    }

    void kill() noexcept override
    {
        silence(amp_);
        silence(filterEnvelope_);
        for (auto& envelope : envelopes_)
            silence(envelope);
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
        const auto oversampled = 2.0 * rate_;
        const auto v = [&values](const int index) { return static_cast<double>(values[static_cast<std::size_t>(index)]); };
        const auto choice = [&values](const int index, const int options) {
            return std::clamp(static_cast<int>(std::lround(values[static_cast<std::size_t>(index)])), 0, options - 1);
        };
        constexpr int step = 16;
        // Coefficients for a step of `step` samples.
        const auto coefficient = [this](const double seconds, const double fraction) {
            return std::exp(-static_cast<double>(step) / (std::max(1.0e-4, seconds * fraction) * rate_));
        };
        const auto stepsOf = [&](const int attack, const double scale) {
            return Steps { coefficient(v(attack), 1.0 / 1.466), coefficient(v(attack + 1) * scale, 1.0 / 4.6),
                           std::clamp(v(attack + 2), 0.0, 1.0), coefficient(v(attack + 3) * scale, 1.0 / 4.6) };
        };
        // What holds for the whole block. The key: shorter operator envelopes
        // and lighter modulation up the keyboard, as struck things are.
        const auto timeScale = std::clamp(std::exp2(-std::clamp(v(keyTime), 0.0, 1.0) * (target_ - 60.0) / 12.0), 0.125, 4.0);
        const auto keyScale = std::clamp(std::exp2(-std::clamp(v(keyDepth), 0.0, 1.0) * (target_ - 60.0) / 24.0), 0.25, 2.0);
        const auto velocityScale = 1.0 - std::clamp(v(velocityDepth), 0.0, 1.0) * (1.0 - velocity_);
        const auto wheelScale = 1.0 + std::clamp(v(wheelDepth), 0.0, 1.0) * static_cast<double>(context.modWheel);
        std::array<Steps, operatorCount> steps {};
        Row levels {};
        Row depths {};
        Matrix amounts {}; // radians at full depth
        Row noiseIndex {};
        auto noisy = false;
        for (std::size_t op = 0; op < operatorCount; ++op) {
            const auto o = static_cast<int>(op);
            steps[op] = stepsOf(setting(o, attackField), timeScale);
            levels[op] = std::clamp(v(setting(o, levelField)), 0.0, 1.0);
            depths[op] = std::clamp(v(setting(o, depthField)), 0.0, 1.0) * velocityScale * keyScale * wheelScale;
            for (std::size_t to = 0; to < operatorCount; ++to) {
                const auto amount = std::clamp(v(setting(o, sendField + static_cast<int>(to))), 0.0, 1.0);
                amounts[to][op] = to == op ? feedbackRange * amount : indexRange * amount * amount;
            }
            const auto shaken = std::clamp(v(noise1 + o), 0.0, 1.0);
            noiseIndex[op] = noiseRange * shaken * shaken / (2.0 * kPi);
            noisy = noisy || shaken > 0.0;
        }
        // The noise darkened by its tone (100 Hz to 20 kHz), as strong whatever its tone.
        const auto noiseHz = 100.0 * std::pow(200.0, std::clamp(v(noiseTone), 0.0, 1.0));
        const auto noiseSmooth = 1.0 - std::exp(-2.0 * kPi * std::min(noiseHz, 0.45 * oversampled) / oversampled);
        const auto noiseLift = std::sqrt((2.0 - noiseSmooth) / noiseSmooth);
        const auto kind = choice(fType, MultiFilter::kindCount);
        const auto resonance = std::clamp(v(fResonance), 0.0, 1.0);
        const auto drive = std::clamp(v(filterDrive), 0.0, 1.0);
        const auto driveGain = 0.05 + 5.0 * drive;
        const auto driveBack = 1.0 / (std::tanh(driveGain) * (1.0 + drive));
        const auto velocityLevel = 1.0 - std::clamp(v(ampVelocity), 0.0, 1.0) * (1.0 - velocity_);
        const auto master = v(voicingMaster);
        const auto ampSteps = stepsOf(ampAttack, 1.0);
        const auto filterSteps = stepsOf(fenvAttack, 1.0);
        const auto lfoHz = [&] {
            if (const auto* division = noteDivisionFor(values[static_cast<std::size_t>(lfoSync)]))
                return std::max(20.0, context.tempo) / 60.0 / division->beats;
            return v(lfoRate);
        }();
        const auto lfoKind = choice(lfoShape, 5);
        const auto bend = static_cast<double>(context.pitchBend) * std::round(v(voicingBend));
        const auto vibrato = v(lfoPitch) + static_cast<double>(context.modWheel) * v(wheelVibrato);
        // No offset left (two operators at one ratio can leave one), nothing under 8 Hz.
        const auto dcPole = std::exp(-2.0 * kPi * 8.0 / rate_);
        // How far the sidebands may reach: past it they would fold back into what is heard.
        const auto carsonLimit = 0.6 * oversampled;

        for (int done = 0; done < count; done += step) {
            const auto length = std::min(step, count - done);
            // The slow things, once a step: envelopes, the LFO, the glide, the
            // operators' paces, the matrix, the filter.
            const auto ampLevel = amp_.step(ampSteps.attack, ampSteps.decay, ampSteps.sustain, ampSteps.release);
            const auto filterLevel = filterEnvelope_.step(filterSteps.attack, filterSteps.decay, filterSteps.sustain, filterSteps.release);
            Row envelope {};
            auto sounding = false;
            for (std::size_t op = 0; op < operatorCount; ++op) {
                auto& contour = envelopes_[op];
                envelope[op] = contour.step(steps[op].attack, steps[op].decay, steps[op].sustain, steps[op].release);
                if (envelope[op] < 1.0e-12) {
                    contour.level = 0.0;
                    envelope[op] = 0.0;
                }
                sounding = sounding || (levels[op] > 0.0 && (contour.stage == Envelope::Stage::attack || envelope[op] > 1.0e-5));
            }
            // Over once every operator heard has faded, even held.
            if (!active() || !sounding) {
                kill();
                return false;
            }
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
            const auto noteHz = 440.0 * std::exp2((pitch_ + expressionPitch + bend + lfo * vibrato - 69.0) / 12.0);
            const auto swing = std::max(0.0, 1.0 + std::clamp(v(lfoDepth), 0.0, 1.0) * lfo);
            Row hz {};
            Row gains {};
            for (std::size_t op = 0; op < operatorCount; ++op) {
                const auto o = static_cast<int>(op);
                hz[op] = operatorHz(values, o, noteHz);
                if (fresh_) {
                    const auto turns = hz[op] / noteHz * startOffset_;
                    operators_.phase[op] = turns - std::floor(turns);
                }
                const auto increment = hz[op] / oversampled;
                operators_.increment[op] = std::clamp(increment, 0.0, 0.49);
                // Near the top of what the doubled rate holds, an operator fades away (heard and modulating).
                gains[op] = envelope[op] * std::clamp((0.49 - increment) / 0.04, 0.0, 1.0);
                if (v(setting(o, fixedField)) < 0.5)
                    show(setting(o, ratioField), playedRatio(values, o), v(setting(o, ratioField)));
                show(setting(o, depthField), depths[op] * swing, v(setting(o, depthField)));
            }
            Matrix into {};
            Row feedback {};
            for (std::size_t from = 0; from < operatorCount; ++from) {
                const auto depth = std::clamp(depths[from] * swing, 0.0, 2.0);
                for (std::size_t to = 0; to < operatorCount; ++to) {
                    const auto radians = amounts[to][from] * depth;
                    if (to == from) {
                        feedback[to] = std::min(feedbackRange, radians * feedbackScale(hz[from])) / (2.0 * kPi);
                    } else {
                        // Its sidebands kept under the limit (Carson's rule).
                        const auto most = std::max(0.0, (carsonLimit - hz[to]) / std::max(1.0, hz[from]) - 1.0);
                        into[to][from] = std::min(radians, most) / (2.0 * kPi);
                    }
                }
            }
            if (fresh_) {
                intoFrom_ = into;
                feedbackFrom_ = feedback;
                fresh_ = false;
            }
            // The cutoff: its knob, the envelope (6 octaves), the LFO (3), the key, the velocity.
            const auto semitones = 12.0 * std::log2(std::max(20.0, v(fCutoff))) + v(filterEnv) * filterLevel * 72.0
                                   + lfo * std::clamp(v(lfoCutoff), 0.0, 1.0) * 36.0 + v(filterKey) * (target_ - 60.0)
                                   + v(filterVelocity) * (velocity_ - 1.0) * 36.0;
            const auto cutoff = std::clamp(std::exp2(semitones / 12.0), 20.0, std::min(20'000.0, rate_ * 0.45));
            const auto coefficients = MultiFilter::coefficients(kind, cutoff, resonance, rate_, filter_.combSize());
            showLfo(0, lfo, lfoPhase_);
            showEnvelope(0, ampLevel);
            showEnvelope(1, filterLevel);
            show(fCutoff, cutoff, v(fCutoff));
            const auto tremolo = 1.0 - std::clamp(v(lfoAmp), 0.0, 1.0) * 0.5 * (1.0 - lfo);
            const auto level = ampLevel * velocityLevel * master * tremolo * expressionGain * outputGain;
            // The envelopes, the matrix and the level eased across the step: no steps heard.
            const auto inverse = 1.0 / static_cast<double>(length);
            Row gainMove {};
            Row feedbackMove {};
            Matrix intoMove {};
            for (std::size_t to = 0; to < operatorCount; ++to) {
                gainMove[to] = (gains[to] - gainsFrom_[to]) * inverse;
                feedbackMove[to] = (feedback[to] - feedbackFrom_[to]) * inverse;
                for (std::size_t from = 0; from < operatorCount; ++from)
                    intoMove[to][from] = (into[to][from] - intoFrom_[to][from]) * inverse;
            }
            auto gainNow = gainsFrom_;
            auto feedbackNow = feedbackFrom_;
            auto intoNow = intoFrom_;
            const auto levelFrom = previousLevel_;
            previousLevel_ = level;
            for (int i = 0; i < length; ++i) {
                for (std::size_t to = 0; to < operatorCount; ++to) {
                    gainNow[to] += gainMove[to];
                    feedbackNow[to] += feedbackMove[to];
                    for (std::size_t from = 0; from < operatorCount; ++from)
                        intoNow[to][from] += intoMove[to][from];
                }
                // Two samples at the doubled rate, then one brought down.
                for (int half = 0; half < 2; ++half) {
                    auto shake = 0.0;
                    if (noisy) {
                        noise_ += (random() - noise_) * noiseSmooth;
                        shake = noise_ * noiseLift;
                    }
                    operators_.play(intoNow, feedbackNow, noiseIndex, shake, gainNow);
                    auto sum = 0.0;
                    for (std::size_t op = 0; op < operatorCount; ++op)
                        sum += levels[op] * operators_.output[op];
                    // Saturated before coming down: what it adds folds back less.
                    if (drive > 1.0e-4)
                        sum = std::tanh(driveGain * sum) * driveBack;
                    push(sum);
                }
                const auto x = decimate();
                const auto blocked = x - dcIn_ + dcPole * dcOut_;
                dcIn_ = x;
                dcOut_ = std::abs(blocked) < 1.0e-20 ? 0.0 : blocked;
                const auto filtered = filter_.process(kind, coefficients, dcOut_);
                const auto gain = levelFrom + (level - levelFrom) * static_cast<double>(i + 1) * inverse;
                const auto out = static_cast<float>(filtered * gain);
                left[done + i] += out;
                right[done + i] += out;
            }
            gainsFrom_ = gains;
            feedbackFrom_ = feedback;
            intoFrom_ = into;
        }
        return active();
    }

private:
    static void restart(Envelope& envelope) noexcept
    {
        envelope.stage = Envelope::Stage::idle;
        envelope.level = 0.0;
        envelope.start();
    }

    static void silence(Envelope& envelope) noexcept
    {
        envelope.stage = Envelope::Stage::idle;
        envelope.level = 0.0;
    }

    // A sample at the doubled rate into the half-band's history (kept twice
    // over: its last `halfBandLength` always in one piece).
    void push(const double x) noexcept
    {
        history_[write_] = x;
        history_[write_ + halfBandLength] = x;
        write_ = write_ + 1 == halfBandLength ? 0 : write_ + 1;
    }

    // One sample at the host's rate from the last ones at the doubled rate.
    [[nodiscard]] double decimate() const noexcept
    {
        const auto* window = history_.data() + write_; // the oldest first
        auto sum = 0.5 * window[halfBandCentre];
        for (std::size_t m = 0; m < halfBandTaps; ++m) {
            const auto distance = 2 * m + 1;
            sum += taps_[m] * (window[halfBandCentre - distance] + window[halfBandCentre + distance]);
        }
        return sum;
    }

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

    double rate_ { 48'000.0 };
    double target_ {};
    double pitch_ {};
    double velocity_ { 1.0 };
    double elapsed_ {};
    double glidePerSample_ {};
    Envelope amp_;
    Envelope filterEnvelope_;
    std::array<Envelope, operatorCount> envelopes_ {};
    Operators operators_ {};
    // Where in the note's period it starts (0..1).
    double startOffset_ {};
    // Where the last step left the eased values.
    Row gainsFrom_ {};
    Matrix intoFrom_ {};
    Row feedbackFrom_ {};
    bool fresh_ { true };
    double noise_ {};
    MultiFilter filter_;
    std::array<double, halfBandTaps> taps_ {};
    std::array<double, 2 * halfBandLength> history_ {};
    std::size_t write_ {};
    double dcIn_ {};
    double dcOut_ {};
    double lfoPhase_ {};
    double lfoHeld_ {};
    double previousLevel_ {};
    // Each voice its own draw.
    std::uint32_t seed_ { nextSeed() };
};

} // namespace

std::unique_ptr<Engine> makeQuartz()
{
    return std::make_unique<QuartzEngine>();
}

std::vector<float> quartzPicture(const Values& values, const std::string_view part, const int points)
{
    // An operator ("op1".."op4") as it sounds, modulated, or the sum heard
    // ("out"): two periods of a middle C, every operator at the top of its
    // envelope, as the knobs set them.
    static constexpr std::array<std::string_view, operatorCount + 1> names { "op1", "op2", "op3", "op4", "out" };
    auto which = names.size();
    for (std::size_t at = 0; at < names.size(); ++at)
        if (part == names[at])
            which = at;
    if (which == names.size() || points < 2)
        return {};
    const auto v = [&values](const int index) { return static_cast<double>(values[static_cast<std::size_t>(index)]); };
    constexpr double noteHz = 261.625565;
    // Four steps of the simulation for each point drawn.
    constexpr int substeps = 4;
    const auto total = (points - 1) * substeps;
    const auto rate = noteHz * static_cast<double>(total) / 2.0;
    Operators state;
    Row levels {};
    Row hz {};
    Row gains {};
    Row feedback {};
    Row noiseIndex {};
    Matrix into {};
    auto noisy = false;
    for (std::size_t op = 0; op < operatorCount; ++op) {
        const auto o = static_cast<int>(op);
        hz[op] = operatorHz(values, o, noteHz);
        state.increment[op] = std::min(hz[op] / rate, 0.49);
        levels[op] = std::clamp(v(setting(o, levelField)), 0.0, 1.0);
        gains[op] = 1.0;
        const auto shaken = std::clamp(v(noise1 + o), 0.0, 1.0);
        noiseIndex[op] = noiseRange * shaken * shaken / (2.0 * kPi);
        noisy = noisy || shaken > 0.0;
    }
    for (std::size_t from = 0; from < operatorCount; ++from) {
        const auto o = static_cast<int>(from);
        const auto depth = std::clamp(v(setting(o, depthField)), 0.0, 1.0);
        for (std::size_t to = 0; to < operatorCount; ++to) {
            const auto amount = std::clamp(v(setting(o, sendField + static_cast<int>(to))), 0.0, 1.0);
            if (to == from)
                feedback[to] = std::min(feedbackRange, feedbackRange * amount * depth * feedbackScale(hz[from])) / (2.0 * kPi);
            else
                into[to][from] = indexRange * amount * amount * depth / (2.0 * kPi);
        }
    }
    const auto noiseHz = 100.0 * std::pow(200.0, std::clamp(v(noiseTone), 0.0, 1.0));
    const auto smooth = 1.0 - std::exp(-2.0 * kPi * std::min(noiseHz, 0.45 * rate) / rate);
    const auto lift = std::sqrt((2.0 - smooth) / smooth);
    // The same noise every time it is drawn: a picture that holds still.
    std::uint32_t seed = 0x6d2b79f5u;
    auto hiss = 0.0;
    const auto tick = [&] {
        auto shake = 0.0;
        if (noisy) {
            seed = seed * 1'664'525u + 1'013'904'223u;
            const auto white = static_cast<double>(seed >> 8) / static_cast<double>(1u << 24) * 2.0 - 1.0;
            hiss += (white - hiss) * smooth;
            shake = hiss * lift;
        }
        state.play(into, feedback, noiseIndex, shake, gains);
    };
    const auto read = [&] {
        if (which < operatorCount)
            return state.output[which];
        auto sum = 0.0;
        for (std::size_t op = 0; op < operatorCount; ++op)
            sum += levels[op] * state.output[op];
        return sum;
    };
    // A period first: the feedback and the loops settled.
    for (int at = 0; at < total / 2; ++at)
        tick();
    std::vector<float> result(static_cast<std::size_t>(points));
    result[0] = static_cast<float>(read());
    for (std::size_t at = 1; at < result.size(); ++at) {
        for (int sub = 0; sub < substeps; ++sub)
            tick();
        result[at] = static_cast<float>(read());
    }
    return result;
}

} // namespace cydaw::engine::synth::detail
