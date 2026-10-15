// Saphir: a four-operator FM synthesiser of the synth family. Four operators
// (A, B, C, D), each one of twelve waves made of harmonics (a sine, two sines
// of fewer bits, a half and a rectified sine, a triangle, a soft saw and a
// soft square, an organ, a bell, a voice, a noise), tuned as a ratio of the
// note or to a fixed frequency, with its level, its own envelope (that can
// loop), its velocity, key and LFO sensitivities. Eleven algorithms route them
// (stacks, pairs, branches, all side by side), the top operator feeding back
// into itself. Then a filter of the family's seven kinds with its own envelope
// and a drive, a pitch envelope, an LFO (to the pitch, the filter and the
// operators' levels), and four global settings: a tone (how deep every
// modulation goes), a stereo spread (two copies detuned apart), a transpose and
// a time that stretches every envelope.

#include "SynthEngines.h"
#include "SynthFilters.h"
#include "SynthParts.h"

#include <cydaw/engine/EffectCatalogue.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cydaw::engine::synth::detail {

// Makes Saphir's waves (once, off the audio thread); its engine's prepare and
// its pictures ask for them too.
void saphirWarmUp();

namespace {

using namespace parts;

// One operator's settings, from its first.
enum OperatorField : int {
    oWave,
    oCoarse,
    oFine,
    oDetune,
    oFixed,
    oFrequency,
    oLevel,
    oVelocity,
    oKeyScale,
    oLfo,
    oAttack,
    oDecay,
    oSustain,
    oRelease,
    oLoop,
    operatorFieldCount
};

constexpr std::size_t operatorCount = 4;
constexpr int operatorA = voicingFieldCount + 2;

// Saphir's settings after the shared voicing, in its table's order: the
// algorithm, the four operators (A, B, C, D), then the rest.
enum Saphir : int {
    algorithmChoice = voicingFieldCount,
    feedbackAmount,
    fType = operatorA + 4 * operatorFieldCount,
    fCutoff,
    fResonance,
    filterDrive,
    filterEnv,
    filterKey,
    filterVelocity,
    fenvAttack,
    fenvDecay,
    fenvSustain,
    fenvRelease,
    penvAmount,
    penvAttack,
    penvDecay,
    lfoShape,
    lfoRate,
    lfoSync,
    lfoDelay,
    lfoAmount,
    lfoWheel,
    lfoPitch,
    lfoCutoff,
    toneAmount,
    spreadAmount,
    transposeAmount,
    timeScale,
    timeKey,
    saphirCount
};
static_assert(feedbackAmount + 1 == operatorA);
static_assert(saphirCount == 95);

constexpr int operatorBase(const std::size_t op) noexcept
{
    return operatorA + static_cast<int>(op) * operatorFieldCount;
}

// The top operator's feedback at its most, in cycles (past a saw, into noise).
constexpr double feedbackMost = 0.45;

using Choices = std::vector<std::pair<std::string, std::string>>;

const Choices kWaves { { "Sinus", "Sine" },
                       { "Sinus 4 bits", "Sine 4-bit" },
                       { "Sinus 8 bits", "Sine 8-bit" },
                       { "Demi-sinus", "Half Sine" },
                       { "Sinus redressé", "Abs Sine" },
                       { "Triangle", "Triangle" },
                       { "Dent de scie douce", "Soft Saw" },
                       { "Carré doux", "Soft Square" },
                       { "Orgue", "Organ" },
                       { "Cloche", "Bell" },
                       { "Voix", "Voice" },
                       { "Bruit", "Noise" } };
// Each algorithm named by its routing: an arrow modulates, "+" sums what
// modulates together, "·" parts the chains heard side by side.
const Choices kAlgorithms { { "D→C→B→A", "D→C→B→A" },
                            { "(C+D)→B→A", "(C+D)→B→A" },
                            { "(C→B)+D→A", "(C→B)+D→A" },
                            { "(D→C)+B→A", "(D→C)+B→A" },
                            { "D→(B+C)→A", "D→(B+C)→A" },
                            { "D→C→B · A", "D→C→B · A" },
                            { "D→C→(A, B)", "D→C→(A, B)" },
                            { "D→C · B→A", "D→C · B→A" },
                            { "D→(A, B, C)", "D→(A, B, C)" },
                            { "D→C · B · A", "D→C · B · A" },
                            { "A · B · C · D", "A · B · C · D" } };
const Choices kLoops { { "Aucune", "None" }, { "En boucle", "Loop" } };
const Choices kFilters { { "Ladder", "Ladder" },        { "Passe-bas", "Low-pass" }, { "Passe-bande", "Band-pass" },
                         { "Passe-haut", "High-pass" }, { "Peigne", "Comb" },        { "Voyelle", "Vowel" },
                         { "Hurlement", "Scream" } };
const Choices kLfoShapes { { "Sinus", "Sine" },   { "Triangle", "Triangle" }, { "Dent de scie", "Saw" },
                           { "Carré", "Square" }, { "Aléatoire", "Random" } };

constexpr int waveCount = 12;
constexpr int algorithmCount = 11;

// Who modulates each operator (bit 0 A, 1 B, 2 C, 3 D) and which are heard.
// A modulator is always above what it modulates: D first, A last.
struct Routing {
    std::array<std::uint8_t, operatorCount> modulators;
    std::uint8_t carriers;
};
constexpr std::array<Routing, algorithmCount> kRoutings { {
    { { 2, 4, 8, 0 }, 1 },  // D→C→B→A
    { { 2, 12, 0, 0 }, 1 }, // (C+D)→B→A
    { { 10, 4, 0, 0 }, 1 }, // (C→B)+D→A
    { { 6, 0, 8, 0 }, 1 },  // (D→C)+B→A
    { { 6, 8, 8, 0 }, 1 },  // D→(B+C)→A
    { { 0, 4, 8, 0 }, 3 },  // D→C→B · A
    { { 4, 4, 8, 0 }, 3 },  // D→C→(A, B)
    { { 2, 0, 8, 0 }, 5 },  // D→C · B→A
    { { 8, 8, 8, 0 }, 7 },  // D→(A, B, C)
    { { 0, 0, 8, 0 }, 7 },  // D→C · B · A
    { { 0, 0, 0, 0 }, 15 }, // A · B · C · D
} };

const Routing& routingOf(const Values& values) noexcept
{
    const auto index = std::clamp(static_cast<int>(std::lround(values[static_cast<std::size_t>(algorithmChoice)])), 0, algorithmCount - 1);
    return kRoutings[static_cast<std::size_t>(index)];
}

bool routes(const std::uint8_t mask, const std::size_t op) noexcept
{
    return ((static_cast<unsigned>(mask) >> op) & 1u) != 0u;
}

// ---- The waves -------------------------------------------------------------------------------------

// Each wave a single cycle made of up to 64 harmonics, at seven levels (the
// n-th keeps 64 >> n of them: none past the Nyquist as the note climbs).
constexpr int waveSize = 1'024;
constexpr int waveLevels = 7;
constexpr int harmonicMost = 64;
constexpr int analysisSize = 4'096;
constexpr std::size_t analysisMask = static_cast<std::size_t>(analysisSize) - 1;

// What a wave is made of: its harmonics' cosine and sine parts (the first unused).
struct Harmonics {
    std::array<double, harmonicMost + 1> cosine {};
    std::array<double, harmonicMost + 1> sine {};
};

// A shape measured over one period: its first harmonics. `sines` holds
// sin(2 pi k / analysisSize).
Harmonics measure(const std::vector<double>& shape, const std::vector<double>& sines)
{
    Harmonics result;
    for (std::size_t n = 1; n <= static_cast<std::size_t>(harmonicMost); ++n) {
        auto c = 0.0;
        auto s = 0.0;
        for (std::size_t k = 0; k < static_cast<std::size_t>(analysisSize); ++k) {
            const auto at = (n * k) & analysisMask;
            s += shape[k] * sines[at];
            c += shape[k] * sines[(at + static_cast<std::size_t>(analysisSize / 4)) & analysisMask];
        }
        result.cosine[n] = 2.0 * c / static_cast<double>(analysisSize);
        result.sine[n] = 2.0 * s / static_cast<double>(analysisSize);
    }
    return result;
}

Harmonics harmonicsOf(const int wave, const std::vector<double>& sines)
{
    Harmonics result;
    // A rounded top: the soft saw and the soft square.
    const auto soft = [](const double n) { return 1.0 / (1.0 + (n / 20.0) * (n / 20.0)); };
    const auto square = [](const double x) { return x * x; };
    switch (wave) {
    case 0: result.sine[1] = 1.0; break;
    case 1:
    case 2:
    case 3:
    case 4: {
        // Shapes measured: a sine of sixteen steps, one held thirty-two times a
        // period, a half sine, a rectified sine (an octave up).
        std::vector<double> shape(static_cast<std::size_t>(analysisSize));
        for (std::size_t k = 0; k < shape.size(); ++k) {
            const auto t = static_cast<double>(k) / static_cast<double>(analysisSize);
            const auto s = std::sin(2.0 * kPi * t);
            switch (wave) {
            case 1: shape[k] = std::clamp((std::floor(s * 8.0) + 0.5) / 8.0, -0.9375, 0.9375); break;
            case 2: shape[k] = std::round(std::sin(2.0 * kPi * (std::floor(t * 32.0) + 0.5) / 32.0) * 127.0) / 127.0; break;
            case 3: shape[k] = std::max(0.0, s); break;
            default: shape[k] = std::abs(s); break;
            }
        }
        return measure(shape, sines);
    }
    case 5:
        for (int n = 1; n <= harmonicMost; n += 2)
            result.sine[static_cast<std::size_t>(n)] = 8.0 / (kPi * kPi) * (((n - 1) / 2) % 2 == 0 ? 1.0 : -1.0) / square(static_cast<double>(n));
        break;
    case 6:
        for (int n = 1; n <= harmonicMost; ++n)
            result.sine[static_cast<std::size_t>(n)] = 2.0 / kPi / static_cast<double>(n) * soft(static_cast<double>(n));
        break;
    case 7:
        for (int n = 1; n <= harmonicMost; n += 2)
            result.sine[static_cast<std::size_t>(n)] = 4.0 / kPi / static_cast<double>(n) * soft(static_cast<double>(n));
        break;
    case 8: {
        // Nine drawbars, all out a little less as they climb.
        struct Bar {
            int harmonic;
            double amount;
        };
        static constexpr std::array<Bar, 9> bars { { { 1, 1.0 }, { 2, 0.75 }, { 3, 0.55 }, { 4, 0.5 }, { 6, 0.35 }, { 8, 0.3 },
                                                     { 10, 0.12 }, { 12, 0.1 }, { 16, 0.08 } } };
        for (const auto& bar : bars)
            result.sine[static_cast<std::size_t>(bar.harmonic)] = bar.amount;
        break;
    }
    case 9: {
        // Few partials, far apart: glassy.
        struct Partial {
            int harmonic;
            double amount;
        };
        static constexpr std::array<Partial, 9> partials { { { 1, 1.0 }, { 3, 0.55 }, { 4, 0.3 }, { 7, 0.45 }, { 10, 0.3 }, { 13, 0.2 },
                                                             { 17, 0.14 }, { 22, 0.1 }, { 28, 0.07 } } };
        for (const auto& partial : partials)
            result.sine[static_cast<std::size_t>(partial.harmonic)] = partial.amount;
        break;
    }
    case 10:
        // Three formants over a falling slope.
        for (int n = 1; n <= 48; ++n) {
            const auto x = static_cast<double>(n);
            result.sine[static_cast<std::size_t>(n)] = std::pow(x, -0.5)
                                                       * (0.15 + std::exp(-square((x - 3.0) / 1.2)) + 0.8 * std::exp(-square((x - 8.0) / 2.0))
                                                          + 0.4 * std::exp(-square((x - 15.0) / 3.0)));
        }
        break;
    default: {
        // Every harmonic, each its own strength and phase: a buzz near noise.
        std::uint32_t seed = 0x2545f491u;
        const auto draw = [&seed] {
            seed = seed * 1'664'525u + 1'013'904'223u;
            return static_cast<double>(seed >> 8) / static_cast<double>(1u << 24);
        };
        for (int n = 1; n <= harmonicMost; ++n) {
            const auto amount = (0.4 + 0.6 * draw()) / std::sqrt(static_cast<double>(n));
            const auto phase = 2.0 * kPi * draw();
            result.cosine[static_cast<std::size_t>(n)] = amount * std::cos(phase);
            result.sine[static_cast<std::size_t>(n)] = amount * std::sin(phase);
        }
        break;
    }
    }
    return result;
}

struct Waves {
    // [wave][level][waveSize + 1]: the last point the first again.
    std::vector<float> data;

    Waves() : data(static_cast<std::size_t>(waveCount) * waveLevels * (waveSize + 1), 0.0f)
    {
        std::vector<double> sines(static_cast<std::size_t>(analysisSize));
        for (std::size_t k = 0; k < sines.size(); ++k)
            sines[k] = std::sin(2.0 * kPi * static_cast<double>(k) / static_cast<double>(analysisSize));
        std::vector<double> cycle(static_cast<std::size_t>(waveSize));
        constexpr auto stride = static_cast<std::size_t>(analysisSize / waveSize);
        constexpr auto quarter = static_cast<std::size_t>(analysisSize / 4);
        for (int wave = 0; wave < waveCount; ++wave) {
            const auto made = harmonicsOf(wave, sines);
            auto scale = 1.0;
            for (int level = 0; level < waveLevels; ++level) {
                const auto kept = static_cast<std::size_t>(harmonicMost >> level);
                for (std::size_t i = 0; i < cycle.size(); ++i) {
                    auto x = 0.0;
                    for (std::size_t n = 1; n <= kept; ++n) {
                        const auto at = (n * i * stride) & analysisMask;
                        x += made.sine[n] * sines[at] + made.cosine[n] * sines[(at + quarter) & analysisMask];
                    }
                    cycle[i] = x;
                }
                // Each wave as loud as the others at its fullest.
                if (level == 0) {
                    auto peak = 0.0;
                    for (const auto x : cycle)
                        peak = std::max(peak, std::abs(x));
                    scale = peak > 1.0e-9 ? 1.0 / peak : 1.0;
                }
                auto* out = data.data() + (static_cast<std::size_t>(wave) * waveLevels + static_cast<std::size_t>(level)) * (waveSize + 1);
                for (std::size_t i = 0; i < cycle.size(); ++i)
                    out[i] = static_cast<float>(cycle[i] * scale);
                out[waveSize] = out[0];
            }
        }
    }

    [[nodiscard]] const float* table(const int wave, const int level) const noexcept
    {
        return data.data()
               + (static_cast<std::size_t>(std::clamp(wave, 0, waveCount - 1)) * waveLevels
                  + static_cast<std::size_t>(std::clamp(level, 0, waveLevels - 1)))
                     * (waveSize + 1);
    }
};

// The waves once made (none before: the engine's prepare makes them).
std::atomic<const Waves*> madeWaves { nullptr };

// The level for a phase increment (cycles a sample): the most harmonics under the Nyquist.
int waveLevelFor(const double increment) noexcept
{
    auto level = 0;
    while (level < waveLevels - 1 && static_cast<double>(harmonicMost >> level) * increment > 0.48)
        ++level;
    return level;
}

// One sample of a wave at any phase (in cycles), between its points.
double readWave(const float* table, double phase) noexcept
{
    phase -= std::floor(phase);
    const auto at = phase * static_cast<double>(waveSize);
    const auto index = std::min(static_cast<int>(at), waveSize - 1);
    const auto fraction = at - static_cast<double>(index);
    const auto a = static_cast<double>(table[index]);
    return a + (static_cast<double>(table[index + 1]) - a) * fraction;
}

// An operator's level knob (dB) as a gain: its bottom is silence.
double levelGain(const double decibels) noexcept
{
    return decibels <= -47.95 ? 0.0 : std::pow(10.0, std::min(decibels, 0.0) / 20.0);
}

// An operator's frequency over the note's when it follows the note (its
// ratio, its fine ratio, its detune).
double ratioOf(const Values& values, const int base) noexcept
{
    const auto at = [&values, base](const int field) { return static_cast<double>(values[static_cast<std::size_t>(base + field)]); };
    const auto coarse = std::round(at(oCoarse));
    return (coarse < 0.5 ? 0.5 : coarse) * (1.0 + std::clamp(std::round(at(oFine)), 0.0, 1'000.0) / 1'000.0) * std::exp2(at(oDetune) / 1'200.0);
}

// Its fixed frequency (Hz), detuned: the key, the glide, the bend and the
// vibrato leave it where it is.
double fixedOf(const Values& values, const int base) noexcept
{
    const auto at = [&values, base](const int field) { return static_cast<double>(values[static_cast<std::size_t>(base + field)]); };
    return std::max(0.0, at(oFrequency)) * std::exp2(at(oDetune) / 1'200.0);
}

} // namespace

void saphirWarmUp()
{
    static const Waves built;
    madeWaves.store(&built, std::memory_order_release);
}

std::vector<Section> saphirSections()
{
    return { { "VOIX", "VOICES", 0xffb8c0d0u },         { "ALGORITHME", "ALGORITHM", 0xff5b8cffu },
             { "OPÉRATEUR A", "OPERATOR A", 0xff6fa8ffu }, { "OPÉRATEUR B", "OPERATOR B", 0xff6ee0e8u },
             { "OPÉRATEUR C", "OPERATOR C", 0xffb48cffu }, { "OPÉRATEUR D", "OPERATOR D", 0xffff8ad0u },
             { "FILTRE", "FILTER", 0xffff7a6bu },        { "HAUTEUR", "PITCH", 0xfff5d061u },
             { "LFO", "LFO", 0xffa58cffu },              { "GLOBAL", "GLOBAL", 0xffd0d6e0u } };
}

std::vector<Parameter> saphirParameters()
{
    Table table;
    addVoicing(table, 16, 0.65f);
    table.section = 1;
    table.choice("algorithm", "Algorithme", "Algorithm", kAlgorithms, 0);
    table.add("feedback", "Rétroaction (D)", "Feedback (D)", {}, 0.0f, 1.0f, 0.0f);
    // Ready to play: A heard, B shaping it (a soft bell-like key), C and D silent.
    struct Start {
        int coarse;
        float level;
        float velocity;
        float decay;
        float sustain;
        float release;
    };
    static constexpr std::array<Start, operatorCount> starts { { { 1, 0.0f, 0.4f, 2.5f, 0.55f, 0.5f },
                                                                 { 1, -14.0f, 0.7f, 1.2f, 0.25f, 0.5f },
                                                                 { 3, -48.0f, 0.5f, 1.0f, 0.5f, 0.4f },
                                                                 { 1, -48.0f, 0.5f, 1.0f, 0.5f, 0.4f } } };
    for (std::size_t op = 0; op < operatorCount; ++op) {
        table.section = 2 + static_cast<int>(op);
        const std::string prefix = std::string(1, static_cast<char>('a' + static_cast<int>(op))) + ".";
        const std::string name = std::string(1, static_cast<char>('A' + static_cast<int>(op))) + " · ";
        const auto& start = starts[op];
        table.choice(prefix + "wave", name + "forme", name + "wave", kWaves, 0);
        table.whole(prefix + "coarse", name + "rapport", name + "ratio", {}, 0, 48, start.coarse);
        table.whole(prefix + "fine", name + "rapport fin (‰)", name + "fine ratio (‰)", {}, 0, 1'000, 0);
        table.add(prefix + "detune", name + "désaccord", name + "detune", "ct", -50.0f, 50.0f, 0.0f);
        table.toggle(prefix + "fixed", name + "fréquence fixe", name + "fixed frequency", false);
        table.add(prefix + "freq", name + "fréquence", name + "frequency", "Hz", 1.0f, 10'000.0f, 440.0f, Kind::continuous, true);
        table.add(prefix + "level", name + "niveau", name + "level", "dB", -48.0f, 0.0f, start.level);
        table.add(prefix + "velocity", name + "vélocité", name + "velocity", {}, 0.0f, 1.0f, start.velocity);
        table.add(prefix + "keyScale", name + "suivi du clavier", name + "key scaling", {}, -1.0f, 1.0f, 0.0f);
        table.add(prefix + "lfo", name + "LFO → niveau", name + "LFO → level", {}, 0.0f, 1.0f, 0.0f);
        table.add(prefix + "attack", name + "attaque", name + "attack", "s", 0.001f, 20.0f, 0.002f, Kind::continuous, true);
        table.add(prefix + "decay", name + "déclin", name + "decay", "s", 0.005f, 20.0f, start.decay, Kind::continuous, true);
        table.add(prefix + "sustain", name + "maintien", name + "sustain", {}, 0.0f, 1.0f, start.sustain);
        table.add(prefix + "release", name + "relâchement", name + "release", "s", 0.005f, 20.0f, start.release, Kind::continuous, true);
        table.choice(prefix + "loop", name + "boucle", name + "loop", kLoops, 0);
    }
    table.section = 6;
    table.choice("f.type", "Filtre · type", "Filter · type", kFilters, 1);
    table.add("f.cutoff", "Filtre · fréquence", "Filter · cutoff", "Hz", 20.0f, 20'000.0f, 10'000.0f, Kind::continuous, true);
    table.add("f.resonance", "Filtre · résonance", "Filter · resonance", {}, 0.0f, 1.0f, 0.1f);
    table.add("filter.drive", "Filtre · saturation", "Filter · drive", {}, 0.0f, 1.0f, 0.0f);
    table.add("filter.env", "Filtre · enveloppe", "Filter · envelope", {}, -1.0f, 1.0f, 0.0f);
    table.add("filter.key", "Filtre · suivi du clavier", "Filter · key tracking", {}, 0.0f, 1.0f, 0.0f);
    table.add("filter.velocity", "Filtre · vélocité", "Filter · velocity", {}, 0.0f, 1.0f, 0.0f);
    table.add("fenv.attack", "Env. filtre · attaque", "Filter env · attack", "s", 0.001f, 20.0f, 0.002f, Kind::continuous, true);
    table.add("fenv.decay", "Env. filtre · déclin", "Filter env · decay", "s", 0.005f, 20.0f, 0.6f, Kind::continuous, true);
    table.add("fenv.sustain", "Env. filtre · maintien", "Filter env · sustain", {}, 0.0f, 1.0f, 0.3f);
    table.add("fenv.release", "Env. filtre · relâchement", "Filter env · release", "s", 0.005f, 20.0f, 0.4f, Kind::continuous, true);
    table.section = 7;
    table.add("penv.amount", "Env. hauteur · quantité", "Pitch env · amount", "st", -48.0f, 48.0f, 0.0f);
    table.add("penv.attack", "Env. hauteur · attaque", "Pitch env · attack", "s", 0.001f, 10.0f, 0.001f, Kind::continuous, true);
    table.add("penv.decay", "Env. hauteur · déclin", "Pitch env · decay", "s", 0.005f, 10.0f, 0.15f, Kind::continuous, true);
    table.section = 8;
    table.choice("lfo1.shape", "LFO · forme", "LFO · shape", kLfoShapes, 0);
    table.add("lfo1.rate", "LFO · vitesse", "LFO · rate", "Hz", 0.05f, 30.0f, 5.0f, Kind::continuous, true);
    table.whole("lfo1.sync", "LFO · synchro", "LFO · sync", {}, 0, 12, 0);
    table.add("lfo1.delay", "LFO · retard", "LFO · delay", "s", 0.0f, 5.0f, 0.0f);
    table.add("lfo1.amount", "LFO · profondeur", "LFO · depth", {}, 0.0f, 1.0f, 1.0f);
    table.add("lfo1.wheel", "Molette → profondeur du LFO", "Wheel → LFO depth", {}, 0.0f, 1.0f, 0.0f);
    table.add("lfo1.pitch", "LFO → hauteur", "LFO → pitch", "st", 0.0f, 2.0f, 0.0f);
    table.add("lfo1.cutoff", "LFO → filtre", "LFO → cutoff", {}, 0.0f, 1.0f, 0.0f);
    table.section = 9;
    table.add("tone", "Timbre", "Tone", {}, -1.0f, 1.0f, 0.0f);
    table.add("spread", "Largeur stéréo", "Stereo spread", {}, 0.0f, 1.0f, 0.2f);
    table.whole("transpose", "Transposition", "Transpose", "st", -24, 24, 0);
    table.add("time", "Durées", "Time", {}, 0.25f, 4.0f, 1.0f, Kind::continuous, true);
    table.add("time.key", "Durées · suivi du clavier", "Time · key tracking", {}, 0.0f, 1.0f, 0.25f);
    return table.parameters;
}

namespace {

class SaphirEngine final : public Engine {
public:
    void prepare(const double sampleRate) override
    {
        rate_ = std::max(1.0, sampleRate);
        saphirWarmUp();
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
        for (auto* envelope : { &operators_[0], &operators_[1], &operators_[2], &operators_[3], &filterEnvelope_, &pitchEnvelope_ }) {
            envelope->stage = Envelope::Stage::idle;
            envelope->level = 0.0;
            envelope->start();
        }
        for (auto& filter : filters_)
            filter.clear();
        dcIn_ = {};
        dcOut_ = {};
        // Every operator from the start of its cycle: the same attack each note.
        for (auto& chain : chains_) {
            chain.phase = {};
            chain.feedback1 = 0.0;
            chain.feedback2 = 0.0;
        }
        gains_ = {};
        previousLevel_ = 0.0;
        elapsed_ = 0.0;
        lfoPhase_ = 0.0;
        lfoHeld_ = random();
        released_ = false;
        carriers_ = routingOf(values).carriers;
    }

    void retarget(const Values& values, const int note, const float velocity) noexcept override
    {
        target_ = static_cast<double>(note);
        if (values[voicingGlide] <= 0.0f)
            pitch_ = target_;
        aimGlide(values);
        velocity_ = std::clamp(static_cast<double>(velocity), 0.0, 1.0);
        if (released_) {
            for (auto& envelope : operators_)
                envelope.start();
            filterEnvelope_.start();
            released_ = false;
        }
    }

    void release() noexcept override
    {
        for (auto& envelope : operators_)
            envelope.release();
        filterEnvelope_.release();
        released_ = true;
    }

    void kill() noexcept override
    {
        for (auto* envelope : { &operators_[0], &operators_[1], &operators_[2], &operators_[3], &filterEnvelope_, &pitchEnvelope_ }) {
            envelope->stage = Envelope::Stage::idle;
            envelope->level = 0.0;
        }
        released_ = false;
    }

    // Playing while an operator that is heard still sounds.
    [[nodiscard]] bool active() const noexcept override
    {
        for (std::size_t op = 0; op < operatorCount; ++op)
            if (routes(carriers_, op) && operators_[op].stage != Envelope::Stage::idle)
                return true;
        return false;
    }
    [[nodiscard]] bool releasing() const noexcept override { return released_ && active(); }
    [[nodiscard]] double currentPitch() const noexcept override { return active() ? pitch_ : -1.0; }

    bool render(const Values& values, const prism::Context& context, float* left, float* right, const int count, const double sampleRate,
                const double expressionPitch, const double expressionGain) noexcept override
    {
        if (!active())
            return false;
        const auto* waves = madeWaves.load(std::memory_order_acquire);
        if (waves == nullptr) {
            // Not warmed up: nothing to play from.
            kill();
            return false;
        }
        rate_ = std::max(1.0, sampleRate);
        const auto v = [&values](const int index) { return static_cast<double>(values[static_cast<std::size_t>(index)]); };
        const auto choice = [&values](const int index, const int options) {
            return std::clamp(static_cast<int>(std::lround(values[static_cast<std::size_t>(index)])), 0, options - 1);
        };
        constexpr int step = 16;
        // The routing: what each operator takes from those above it, in
        // cycles at their full level (the tone deepens or softens them all).
        const auto& route = routingOf(values);
        carriers_ = route.carriers;
        const auto depth = std::exp2(2.0 * std::clamp(v(toneAmount), -1.0, 1.0));
        std::array<std::array<double, operatorCount>, operatorCount> matrix {};
        std::array<double, operatorCount> heard {};
        auto heardCount = 0;
        for (std::size_t to = 0; to < operatorCount; ++to) {
            for (std::size_t from = to + 1; from < operatorCount; ++from)
                matrix[to][from] = routes(route.modulators[to], from) ? depth : 0.0;
            if (routes(route.carriers, to)) {
                heard[to] = 1.0;
                ++heardCount;
            }
        }
        const auto feedback = feedbackMost * std::clamp(v(feedbackAmount), 0.0, 1.0);
        // Every envelope's time: the knob, the time, the key.
        const auto timeFactor = std::clamp(v(timeScale), 0.25, 4.0) * std::exp2(-std::clamp(v(timeKey), 0.0, 1.0) * (target_ - 60.0) / 24.0);
        const auto coefficient = [this, timeFactor](const double seconds, const double fraction) {
            return std::exp(-static_cast<double>(step) / (std::max(1.0e-4, seconds * timeFactor * fraction) * rate_));
        };
        // What holds for the whole block, each operator's.
        struct Setting {
            int wave;
            bool fixed;
            double ratio;
            double hz;
            double gain;
            double lfo;
            bool loop;
            double attackStep;
            double decayStep;
            double sustainLevel;
            double releaseStep;
        };
        std::array<Setting, operatorCount> settings {};
        for (std::size_t op = 0; op < operatorCount; ++op) {
            const auto base = operatorBase(op);
            auto& setting = settings[op];
            setting.wave = choice(base + oWave, waveCount);
            setting.fixed = v(base + oFixed) >= 0.5;
            setting.ratio = ratioOf(values, base);
            setting.hz = fixedOf(values, base);
            const auto velocityGain = 1.0 - std::clamp(v(base + oVelocity), 0.0, 1.0) * (1.0 - velocity_);
            const auto keyGain = std::min(2.0, std::pow(10.0, -std::clamp(v(base + oKeyScale), -1.0, 1.0) * (target_ - 60.0) * 0.5 / 20.0));
            setting.gain = levelGain(v(base + oLevel)) * velocityGain * keyGain;
            setting.lfo = std::clamp(v(base + oLfo), 0.0, 1.0);
            setting.loop = choice(base + oLoop, 2) == 1;
            setting.attackStep = coefficient(v(base + oAttack), 1.0 / 1.466);
            setting.decayStep = coefficient(v(base + oDecay), 1.0 / 4.6);
            setting.sustainLevel = std::clamp(v(base + oSustain), 0.0, 1.0);
            setting.releaseStep = coefficient(v(base + oRelease), 1.0 / 4.6);
        }
        const auto filterKind = choice(fType, MultiFilter::kindCount);
        const auto resonance = std::clamp(v(fResonance), 0.0, 1.0);
        const auto drive = std::clamp(v(filterDrive), 0.0, 1.0);
        const auto driveIn = 1.0 + 4.0 * drive;
        const auto driveOut = 1.0 / (1.0 + 1.5 * drive);
        const auto driveMix = std::min(1.0, drive * 10.0);
        const auto fenvAttackStep = coefficient(v(fenvAttack), 1.0 / 1.466);
        const auto fenvDecayStep = coefficient(v(fenvDecay), 1.0 / 4.6);
        const auto fenvReleaseStep = coefficient(v(fenvRelease), 1.0 / 4.6);
        const auto fenvSustainLevel = std::clamp(v(fenvSustain), 0.0, 1.0);
        const auto penvAttackStep = coefficient(v(penvAttack), 1.0 / 1.466);
        const auto penvDecayStep = coefficient(v(penvDecay), 1.0 / 4.6);
        const auto penvSemitones = v(penvAmount);
        const auto cutoffSemitones = 12.0 * std::log2(std::max(20.0, v(fCutoff))) + v(filterKey) * (target_ - 60.0)
                                     + v(filterVelocity) * (velocity_ - 1.0) * 36.0;
        const auto lfoHz = [&] {
            if (const auto* division = noteDivisionFor(values[static_cast<std::size_t>(lfoSync)]))
                return std::max(20.0, context.tempo) / 60.0 / division->beats;
            return v(lfoRate);
        }();
        const auto lfoShapeIndex = choice(lfoShape, 5);
        // The LFO's depth: its knob, more with the wheel.
        const auto lfoDepth = std::clamp(v(lfoAmount) + static_cast<double>(context.modWheel) * v(lfoWheel), 0.0, 1.0);
        const auto vibrato = v(lfoPitch) * lfoDepth;
        const auto lfoToCutoff = v(lfoCutoff) * lfoDepth;
        const auto lfoDelaySeconds = v(lfoDelay);
        const auto bend = static_cast<double>(context.pitchBend) * std::round(v(voicingBend));
        const auto transpose = std::round(v(transposeAmount));
        // The spread: a second copy of the operators, the two detuned apart and
        // opened across the stereo.
        const auto spread = std::clamp(v(spreadAmount), 0.0, 1.0);
        const auto stereo = spread > 1.0e-4;
        const auto chainCount = stereo ? std::size_t { 2 } : std::size_t { 1 };
        const auto spreadCents = 15.0 * spread;
        const auto width = std::sqrt(spread);
        const std::array<double, 2> into { 0.5 + 0.5 * width, 0.5 - 0.5 * width };
        const auto master = v(voicingMaster);
        const auto outputScale = 0.5 / std::sqrt(static_cast<double>(std::max(1, heardCount)));
        const auto dcCoefficient = std::exp(-2.0 * kPi * 5.0 / rate_);

        for (int done = 0; done < count; done += step) {
            const auto length = std::min(step, count - done);
            // The slow things once a step: the envelopes (a looping one starts
            // again as its decay ends), the LFO, the glide, the frequencies, the filter.
            std::array<double, operatorCount> levels {};
            for (std::size_t op = 0; op < operatorCount; ++op) {
                auto& envelope = operators_[op];
                const auto& setting = settings[op];
                levels[op] = envelope.step(setting.attackStep, setting.decayStep, setting.sustainLevel, setting.releaseStep);
                if (setting.loop && (envelope.stage == Envelope::Stage::decay || envelope.stage == Envelope::Stage::sustain)
                    && std::abs(envelope.level - setting.sustainLevel) < 0.02 * std::max(0.05, 1.0 - setting.sustainLevel))
                    envelope.stage = Envelope::Stage::attack;
                if (envelope.level < 1.0e-12 && envelope.stage != Envelope::Stage::attack)
                    envelope.level = 0.0;
            }
            const auto filterLevel = filterEnvelope_.step(fenvAttackStep, fenvDecayStep, fenvSustainLevel, fenvReleaseStep);
            const auto pitchLevel = pitchEnvelope_.step(penvAttackStep, penvDecayStep, 0.0, penvDecayStep);
            for (auto* envelope : { &filterEnvelope_, &pitchEnvelope_ })
                if (envelope->level < 1.0e-12 && envelope->stage != Envelope::Stage::attack)
                    envelope->level = 0.0;
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
            const auto lfo = lfoWave(lfoShapeIndex, lfoPhase_, lfoHeld_);
            // The glide: the same time whatever the distance, at an even pace.
            if (glidePerSample_ > 0.0 && pitch_ != target_) {
                const auto move = glidePerSample_ * static_cast<double>(length);
                pitch_ = std::abs(target_ - pitch_) <= move ? target_ : pitch_ + (target_ > pitch_ ? move : -move);
            } else {
                pitch_ = target_;
            }
            // The note's pitch moves the operators that follow it; the pitch
            // envelope moves them all, the fixed ones too.
            const auto envelopeShift = penvSemitones * pitchLevel;
            const auto notePitch = pitch_ + transpose + expressionPitch + bend + envelopeShift + lfo * fade * vibrato;
            for (std::size_t index = 0; index < chainCount; ++index) {
                auto& chain = chains_[index];
                const auto cents = stereo ? (index == 0 ? -spreadCents : spreadCents) : 0.0;
                const auto noteHz = 440.0 * std::exp2((notePitch + cents / 100.0 - 69.0) / 12.0);
                const auto shift = std::exp2((envelopeShift + cents / 100.0) / 12.0);
                for (std::size_t op = 0; op < operatorCount; ++op) {
                    const auto& setting = settings[op];
                    const auto hz = setting.fixed ? setting.hz * shift : noteHz * setting.ratio;
                    chain.increment[op] = std::clamp(hz / rate_, 0.0, 0.49);
                    chain.table[op] = waves->table(setting.wave, waveLevelFor(chain.increment[op]));
                }
            }
            // Each operator's gain: its envelope, its level, the velocity, the key, the LFO.
            std::array<double, operatorCount> targets {};
            auto heardLevel = 0.0;
            for (std::size_t op = 0; op < operatorCount; ++op) {
                const auto& setting = settings[op];
                const auto lfoGain = 1.0 - setting.lfo * lfoDepth * fade * 0.5 * (1.0 - lfo);
                targets[op] = std::clamp(levels[op] * setting.gain * lfoGain, 0.0, 2.0);
                if (heard[op] > 0.0)
                    heardLevel = std::max(heardLevel, levels[op]);
                const auto index = operatorBase(op) + oLevel;
                const auto knob = v(index);
                if (setting.lfo * lfoDepth * fade > 1.0e-6 && knob > -47.95)
                    show(index, std::clamp(knob + 20.0 * std::log10(std::max(1.0e-3, lfoGain)), -48.0, 0.0), knob);
                else
                    show(index, knob, knob);
            }
            // The cutoff: its knob, the key, the velocity, the envelope (6 octaves), the LFO (3).
            const auto cutoff = std::clamp(std::exp2((cutoffSemitones + v(filterEnv) * filterLevel * 72.0 + lfo * fade * lfoToCutoff * 36.0) / 12.0), 20.0,
                                           std::min(20'000.0, rate_ * 0.45));
            const auto coefficients = MultiFilter::coefficients(filterKind, cutoff, resonance, rate_, filters_[0].combSize());
            showLfo(0, lfo * fade, lfoPhase_);
            showEnvelope(0, heardLevel);
            showEnvelope(1, filterLevel);
            showEnvelope(2, pitchLevel);
            show(fCutoff, cutoff, v(fCutoff));
            const auto level = master * expressionGain * outputScale;
            // The gains eased across the step: no steps heard on a quick attack.
            const auto from = previousLevel_;
            previousLevel_ = level;
            const auto gainsFrom = gains_;
            gains_ = targets;
            const auto inverse = 1.0 / static_cast<double>(length);
            for (int i = 0; i < length; ++i) {
                const auto t = static_cast<double>(i + 1) * inverse;
                const auto gain = from + (level - from) * t;
                std::array<double, operatorCount> g {};
                for (std::size_t op = 0; op < operatorCount; ++op)
                    g[op] = gainsFrom[op] + (targets[op] - gainsFrom[op]) * t;
                std::array<double, 2> sides {};
                for (std::size_t index = 0; index < chainCount; ++index) {
                    auto& chain = chains_[index];
                    // D first (its own last two samples fed back), A last.
                    const auto top = readWave(chain.table[3], chain.phase[3] + feedback * 0.5 * (chain.feedback1 + chain.feedback2));
                    chain.feedback2 = chain.feedback1;
                    chain.feedback1 = top;
                    const auto d = top * g[3];
                    const auto c = readWave(chain.table[2], chain.phase[2] + matrix[2][3] * d) * g[2];
                    const auto b = readWave(chain.table[1], chain.phase[1] + matrix[1][2] * c + matrix[1][3] * d) * g[1];
                    const auto a = readWave(chain.table[0], chain.phase[0] + matrix[0][1] * b + matrix[0][2] * c + matrix[0][3] * d) * g[0];
                    const auto mixed = heard[0] * a + heard[1] * b + heard[2] * c + heard[3] * d;
                    for (std::size_t op = 0; op < operatorCount; ++op) {
                        chain.phase[op] += chain.increment[op];
                        if (chain.phase[op] >= 1.0)
                            chain.phase[op] -= 1.0;
                    }
                    if (stereo) {
                        sides[0] += mixed * into[index];
                        sides[1] += mixed * into[1 - index];
                    } else {
                        sides[0] = mixed;
                        sides[1] = mixed;
                    }
                }
                for (std::size_t side = 0; side < 2; ++side) {
                    auto x = sides[side];
                    if (driveMix > 0.0)
                        x += (std::tanh(x * driveIn) * driveOut - x) * driveMix;
                    x = filters_[side].process(filterKind, coefficients, x);
                    // No offset left by the half waves or the drive.
                    const auto blocked = x - dcIn_[side] + dcCoefficient * dcOut_[side];
                    dcIn_[side] = x;
                    dcOut_[side] = blocked;
                    sides[side] = blocked * gain;
                }
                left[done + i] += static_cast<float>(sides[0]);
                right[done + i] += static_cast<float>(sides[1]);
            }
            // Alone, the second copy follows the first: ready to part when the spread opens.
            if (!stereo)
                chains_[1] = chains_[0];
            for (auto& out : dcOut_)
                if (std::abs(out) < 1.0e-15)
                    out = 0.0;
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

    // One copy of the four operators: where each is in its cycle, how fast it
    // runs, the wave it reads; the top one's last two samples.
    struct Chain {
        std::array<double, operatorCount> phase {};
        std::array<double, operatorCount> increment {};
        std::array<const float*, operatorCount> table {};
        double feedback1 {};
        double feedback2 {};
    };

    double rate_ { 48'000.0 };
    double target_ {};
    double pitch_ {};
    double velocity_ { 1.0 };
    double elapsed_ {};
    std::array<Envelope, operatorCount> operators_ {};
    Envelope filterEnvelope_;
    Envelope pitchEnvelope_;
    std::array<MultiFilter, 2> filters_ {};
    std::array<double, 2> dcIn_ {};
    std::array<double, 2> dcOut_ {};
    std::array<Chain, 2> chains_ {};
    std::array<double, operatorCount> gains_ {};
    double lfoPhase_ {};
    double lfoHeld_ {};
    double previousLevel_ {};
    double glidePerSample_ {};
    std::uint8_t carriers_ { 1 };
    bool released_ {};
    // Each voice its own draw: its random LFO apart.
    std::uint32_t seed_ { nextSeed() };
};

} // namespace

std::unique_ptr<Engine> makeSaphir()
{
    return std::make_unique<SaphirEngine>();
}

std::vector<float> saphirPicture(const Values& values, const std::string_view part, const int points)
{
    // An operator alone ("a" … "d"): its wave over two periods of a C4, at its
    // ratio and its level (the top one with its feedback); "out": the four as
    // the algorithm routes them, their envelopes at the top.
    const auto which = part == "a"     ? 0
                       : part == "b"   ? 1
                       : part == "c"   ? 2
                       : part == "d"   ? 3
                       : part == "out" ? static_cast<int>(operatorCount)
                                       : -1;
    if (which < 0 || points < 2)
        return {};
    saphirWarmUp();
    const auto* waves = madeWaves.load(std::memory_order_acquire);
    if (waves == nullptr)
        return {};
    const auto v = [&values](const int index) { return static_cast<double>(values[static_cast<std::size_t>(index)]); };
    constexpr double noteHz = 261.6255653;
    constexpr double rate = 48'000.0;
    const auto& route = routingOf(values);
    const auto depth = std::exp2(2.0 * std::clamp(v(toneAmount), -1.0, 1.0));
    const auto feedback = feedbackMost * std::clamp(v(feedbackAmount), 0.0, 1.0);
    std::array<const float*, operatorCount> waveTables {};
    std::array<double, operatorCount> ratios {};
    std::array<double, operatorCount> gains {};
    auto heardCount = 0;
    for (std::size_t op = 0; op < operatorCount; ++op) {
        const auto base = operatorBase(op);
        ratios[op] = v(base + oFixed) >= 0.5 ? fixedOf(values, base) / noteHz : ratioOf(values, base);
        gains[op] = levelGain(v(base + oLevel));
        const auto wave = std::clamp(static_cast<int>(std::lround(v(base + oWave))), 0, waveCount - 1);
        waveTables[op] = waves->table(wave, waveLevelFor(std::min(0.49, noteHz * ratios[op] / rate)));
        if (routes(route.carriers, op))
            ++heardCount;
    }
    const auto heardScale = 1.0 / std::sqrt(static_cast<double>(std::max(1, heardCount)));
    // Note periods a point; a period run first, for the feedback to settle.
    const auto step = 2.0 / static_cast<double>(points - 1);
    const auto warm = std::max(8, points / 2);
    std::array<double, operatorCount> phase {};
    for (std::size_t op = 0; op < operatorCount; ++op) {
        phase[op] = -static_cast<double>(warm) * step * ratios[op];
        phase[op] -= std::floor(phase[op]);
    }
    auto feedback1 = 0.0;
    auto feedback2 = 0.0;
    std::vector<float> result(static_cast<std::size_t>(points));
    for (int at = -warm; at < points; ++at) {
        std::array<double, operatorCount> out {};
        const auto top = readWave(waveTables[3], phase[3] + feedback * 0.5 * (feedback1 + feedback2));
        feedback2 = feedback1;
        feedback1 = top;
        out[3] = top * gains[3];
        for (std::size_t op = operatorCount - 1; op-- > 0;) {
            auto modulation = 0.0;
            for (std::size_t from = op + 1; from < operatorCount; ++from)
                if (routes(route.modulators[op], from))
                    modulation += out[from] * depth;
            out[op] = readWave(waveTables[op], phase[op] + modulation) * gains[op];
        }
        if (at >= 0) {
            auto value = 0.0;
            if (which == 3) {
                value = out[3];
            } else if (which < 3) {
                const auto op = static_cast<std::size_t>(which);
                value = readWave(waveTables[op], phase[op]) * gains[op];
            } else {
                for (std::size_t op = 0; op < operatorCount; ++op)
                    if (routes(route.carriers, op))
                        value += out[op];
                value *= heardScale;
            }
            result[static_cast<std::size_t>(at)] = static_cast<float>(value);
        }
        for (std::size_t op = 0; op < operatorCount; ++op) {
            phase[op] += ratios[op] * step;
            phase[op] -= std::floor(phase[op]);
        }
    }
    return result;
}

} // namespace cydaw::engine::synth::detail
