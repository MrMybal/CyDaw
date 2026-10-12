// Nébuleuse: an atmospheric synth of the synth family. Two layers, each a
// synthesis one (the family's wavetables) or a granular one: grains taken
// from one of seven textures it makes itself (a choir, glass, wind, strings,
// bells, rain, a drone), their size, their density, how far they stray, how
// they scan the texture. Each layer plays its note with a chord around it
// (its "harmonia"), brighter or darker, filtered, drifting. A filter of the
// family's kinds after them; two envelopes, two LFOs, the orb (a point moved
// in a circle, by hand or by itself) and eight modulation slots.

#include "SynthEngines.h"
#include "SynthFilters.h"
#include "SynthParts.h"
#include "SynthTables.h"

#include <cydaw/engine/EffectCatalogue.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace cydaw::engine::synth::detail {

namespace {

using namespace parts;

// One layer's settings, from its first.
enum LayerField : int {
    lSource,
    lLevel,
    lPan,
    lOctave,
    lSemi,
    lFine,
    lTable,
    lPosition,
    lTexture,
    lScan,
    lTravel,
    lGrain,
    lDensity,
    lSpray,
    lScatter,
    lReverse,
    lHarmonia,
    lHarmoniaLevel,
    lTimbre,
    lCutoff,
    lResonance,
    lDrift,
    layerFieldCount
};

constexpr int layerA = voicingFieldCount;
constexpr int layerB = layerA + layerFieldCount;

enum Nebuleuse : int {
    fType = layerB + layerFieldCount,
    fCutoff,
    fResonance,
    filterKey,
    filterVelocity,
    ampAttack,
    ampDecay,
    ampSustain,
    ampRelease,
    ampVelocity,
    env2Attack,
    env2Decay,
    env2Sustain,
    env2Release,
    lfo1Shape,
    lfo1Rate,
    lfo1Sync,
    lfo2Shape,
    lfo2Rate,
    lfo2Sync,
    orbX,
    orbY,
    orbMotion,
    orbRate,
    mod1Source,
    modLast = mod1Source + 3 * 8 - 1,
    nebuleuseCount
};
static_assert(nebuleuseCount == 98);

constexpr int slotCount = 8;
constexpr int grainCount = 24;
constexpr int textureCount = 7;
constexpr int chordMost = 3;

enum Source : int { srcNone, srcEnv2, srcLfo1, srcLfo2, srcOrbX, srcOrbY, srcVelocity, srcWheel, srcKey, srcRandom, srcPressure, srcDrift, sourceCount };
enum Destination : int {
    dstNone,
    dstPitch,
    dstLevelA,
    dstLevelB,
    dstScanA,
    dstScanB,
    dstPositionA,
    dstPositionB,
    dstGrainA,
    dstGrainB,
    dstDensityA,
    dstDensityB,
    dstTimbreA,
    dstTimbreB,
    dstCutoff,
    dstResonance,
    dstVolume,
    dstPan,
    dstHarmonia,
    destinationCount
};

using Choices = std::vector<std::pair<std::string, std::string>>;

const Choices kSourcesOfLayer { { "Synthèse", "Synthesis" }, { "Granulaire", "Granular" } };
const Choices kTables { { "Classique", "Classic" }, { "Harmonique", "Harmonic" }, { "Voix", "Voices" }, { "Métal", "Metal" },
                        { "Plié", "Folded" },        { "Numérique", "Digital" },  { "Dents", "Teeth" }, { "Synchro", "Sync" } };
const Choices kTextures { { "Chœur", "Choir" },     { "Verre", "Glass" },   { "Vent", "Wind" }, { "Cordes", "Strings" },
                          { "Cloches", "Bells" },   { "Pluie", "Rain" },    { "Drone", "Drone" } };
const Choices kChords { { "Aucune", "None" },          { "Octave", "Octave" },        { "Quinte", "Fifth" },
                        { "Octave et quinte", "Octave and fifth" }, { "Majeur", "Major" }, { "Mineur", "Minor" },
                        { "Sus 4", "Sus 4" },          { "Septième", "Seventh" },     { "Neuvième", "Ninth" },
                        { "Grave", "Sub" },            { "Large", "Wide" } };
const Choices kFilters { { "Ladder", "Ladder" },          { "Passe-bas", "Low-pass" }, { "Passe-bande", "Band-pass" },
                         { "Passe-haut", "High-pass" },   { "Peigne", "Comb" },        { "Voyelle", "Vowel" },
                         { "Hurlement", "Scream" } };
const Choices kLfoShapes { { "Sinus", "Sine" },  { "Triangle", "Triangle" }, { "Dent de scie", "Saw" },
                           { "Carré", "Square" }, { "Aléatoire", "Random" } };
const Choices kSources { { "Aucune", "None" },        { "Env 2", "Env 2" },      { "LFO 1", "LFO 1" },     { "LFO 2", "LFO 2" },
                         { "Orbe X", "Orb X" },       { "Orbe Y", "Orb Y" },     { "Vélocité", "Velocity" },
                         { "Molette", "Mod wheel" },  { "Clavier", "Key" },      { "Aléatoire", "Random" }, { "Pression", "Pressure" },
                         { "Dérive", "Drift" } };
const Choices kDestinations { { "Aucune", "None" },
                              { "Hauteur", "Pitch" },
                              { "A · niveau", "A · level" },
                              { "B · niveau", "B · level" },
                              { "A · lecture", "A · scan" },
                              { "B · lecture", "B · scan" },
                              { "A · position", "A · position" },
                              { "B · position", "B · position" },
                              { "A · grain", "A · grain" },
                              { "B · grain", "B · grain" },
                              { "A · densité", "A · density" },
                              { "B · densité", "B · density" },
                              { "A · timbre", "A · timbre" },
                              { "B · timbre", "B · timbre" },
                              { "Filtre · fréquence", "Filter · cutoff" },
                              { "Résonance", "Resonance" },
                              { "Volume", "Volume" },
                              { "Panoramique", "Pan" },
                              { "Harmonia", "Harmonia" } };

// The chords a layer plays around its note (semitones; up to three).
struct Chord {
    std::array<double, chordMost> intervals;
    int count;
};
constexpr std::array<Chord, 11> kChordIntervals { {
    { { 0.0, 0.0, 0.0 }, 0 },     { { 12.0, 0.0, 0.0 }, 1 },  { { 7.0, 0.0, 0.0 }, 1 },  { { 7.0, 12.0, 0.0 }, 2 },
    { { 4.0, 7.0, 0.0 }, 2 },     { { 3.0, 7.0, 0.0 }, 2 },   { { 5.0, 7.0, 0.0 }, 2 },  { { 4.0, 7.0, 10.0 }, 3 },
    { { 4.0, 7.0, 14.0 }, 3 },    { { -12.0, 0.0, 0.0 }, 1 }, { { -12.0, 7.0, 12.0 }, 3 },
} };

// ---- The textures ----------------------------------------------------------------------------

constexpr double textureRate = 48'000.0;
constexpr double textureSeconds = 3.0;
constexpr double textureRoot = 130.8128; // C3: a pitched texture plays its note there
constexpr std::size_t textureLength = static_cast<std::size_t>(textureRate * textureSeconds);

class Textures {
public:
    Textures()
    {
        for (int index = 0; index < textureCount; ++index) {
            auto& samples = data_[static_cast<std::size_t>(index)];
            samples.assign(textureLength + 1, 0.0f);
            make(index, samples);
            auto peak = 1.0e-9f;
            for (const auto x : samples)
                peak = std::max(peak, std::abs(x));
            for (auto& x : samples)
                x *= 0.9f / peak;
            samples[textureLength] = samples[0];
        }
    }
    [[nodiscard]] const std::vector<float>& texture(const int index) const noexcept
    {
        return data_[static_cast<std::size_t>(std::clamp(index, 0, textureCount - 1))];
    }

private:
    std::uint32_t seed_ { 0x2545f491u };
    double random() noexcept
    {
        seed_ = seed_ * 1'664'525u + 1'013'904'223u;
        return static_cast<double>(seed_ >> 8) / static_cast<double>(1u << 24) * 2.0 - 1.0;
    }

    void make(const int index, std::vector<float>& out)
    {
        const auto n = textureLength;
        const auto at = [](const std::size_t i) { return static_cast<double>(i) / textureRate; };
        switch (index) {
        case 0: {
            // A choir: three voices a little apart, a vibrato each, the vowel moving from A to O.
            constexpr int harmonics = 24;
            for (int singer = 0; singer < 3; ++singer) {
                const auto detune = std::pow(2.0, (static_cast<double>(singer) - 1.0) * 9.0 / 1'200.0);
                std::array<double, harmonics> phases {};
                const auto offset = static_cast<double>(singer) * 1.7;
                for (std::size_t i = 0; i < n; ++i) {
                    const auto t = at(i);
                    const auto hz = textureRoot * detune * (1.0 + 0.006 * std::sin(2.0 * kPi * 5.2 * t + offset));
                    const auto vowel = 0.5 - 0.5 * std::cos(kPi * t / textureSeconds);
                    const std::array<double, 3> formants { 800.0 - 350.0 * vowel, 1'150.0 - 350.0 * vowel, 2'900.0 - 100.0 * vowel };
                    auto sum = 0.0;
                    for (int h = 0; h < harmonics; ++h) {
                        const auto f = hz * static_cast<double>(h + 1);
                        auto weight = 0.0;
                        for (std::size_t k = 0; k < 3; ++k)
                            weight += (k == 0 ? 1.0 : k == 1 ? 0.6 : 0.25) / (1.0 + std::pow((f - formants[k]) / 90.0, 2.0));
                        phases[static_cast<std::size_t>(h)] += f / textureRate;
                        sum += weight / std::sqrt(static_cast<double>(h + 1)) * std::sin(2.0 * kPi * phases[static_cast<std::size_t>(h)]);
                    }
                    out[i] += static_cast<float>(sum + 0.02 * random());
                }
            }
            break;
        }
        case 1: {
            // Glass: sparkles struck here and there, each four ringing partials.
            static constexpr std::array<double, 4> ratios { 1.0, 2.32, 4.25, 6.63 };
            for (auto start = 0.0; start < textureSeconds; start += 0.08 + 0.15 * (random() + 1.0) * 0.5) {
                const auto base = textureRoot * 2.0 * (random() > 0.3 ? 1.0 : 2.0);
                const auto gain = 0.4 + 0.3 * random();
                const auto first = static_cast<std::size_t>(start * textureRate);
                for (std::size_t k = 0; k < ratios.size(); ++k) {
                    const auto hz = base * ratios[k];
                    const auto decay = 0.9 / (1.0 + static_cast<double>(k));
                    for (std::size_t i = first; i < n && static_cast<double>(i - first) / textureRate < 4.0 * decay; ++i) {
                        const auto t = static_cast<double>(i - first) / textureRate;
                        out[i] += static_cast<float>(gain / (1.0 + static_cast<double>(k)) * std::exp(-t / decay) * std::sin(2.0 * kPi * hz * t));
                    }
                }
            }
            break;
        }
        case 2: {
            // Wind: noise in two bands that drift.
            Filter low;
            Filter high;
            for (std::size_t i = 0; i < n; ++i) {
                const auto t = at(i);
                const auto x = random();
                const auto one = 450.0 + 350.0 * std::sin(2.0 * kPi * 0.21 * t);
                const auto two = 1'400.0 + 700.0 * std::sin(2.0 * kPi * 0.13 * t + 1.0);
                const auto a = low.variable(x, std::tan(kPi * one / textureRate), 0.75, 1, false);
                const auto b = high.variable(x, std::tan(kPi * two / textureRate), 0.6, 1, false);
                out[i] = static_cast<float>(a + 0.6 * b);
            }
            break;
        }
        case 3: {
            // Strings: five saws apart, slowly moving, softened.
            static constexpr std::array<double, 5> cents { -12.0, -5.0, 0.0, 6.0, 11.0 };
            std::array<double, 5> phases {};
            auto soft = 0.0;
            for (std::size_t i = 0; i < n; ++i) {
                const auto t = at(i);
                auto sum = 0.0;
                for (std::size_t v = 0; v < cents.size(); ++v) {
                    const auto hz = textureRoot * std::pow(2.0, (cents[v] + 4.0 * std::sin(2.0 * kPi * (0.3 + 0.07 * static_cast<double>(v)) * t)) / 1'200.0);
                    const auto dt = hz / textureRate;
                    sum += oscillator(0, phases[v], dt, 0.5);
                    phases[v] += dt;
                    phases[v] -= std::floor(phases[v]);
                }
                soft += (sum - soft) * 0.25;
                out[i] = static_cast<float>(soft);
            }
            break;
        }
        case 4: {
            // Bells: four strikes, their partials as a church bell's.
            static constexpr std::array<double, 7> ratios { 0.5, 1.0, 1.19, 1.56, 2.0, 2.51, 3.0 };
            for (int strike = 0; strike < 4; ++strike) {
                const auto first = static_cast<std::size_t>(static_cast<double>(strike) * 0.75 * textureRate);
                const auto base = textureRoot * 2.0;
                for (std::size_t k = 0; k < ratios.size(); ++k) {
                    const auto hz = base * ratios[k];
                    const auto decay = 2.5 / ratios[k];
                    for (std::size_t i = first; i < n; ++i) {
                        const auto t = static_cast<double>(i - first) / textureRate;
                        out[i] += static_cast<float>(0.3 * std::exp(-t / decay) * std::sin(2.0 * kPi * hz * t));
                    }
                }
            }
            break;
        }
        case 5: {
            // Rain: drops (a little falling chirp each), on a soft hiss.
            auto hiss = 0.0;
            for (std::size_t i = 0; i < n; ++i) {
                hiss += (random() - hiss) * 0.05;
                out[i] = static_cast<float>(0.15 * hiss);
            }
            for (int drop = 0; drop < 600; ++drop) {
                const auto first = static_cast<std::size_t>((random() + 1.0) * 0.5 * static_cast<double>(n - 1));
                const auto hz = 1'500.0 + 2'500.0 * (random() + 1.0) * 0.5;
                const auto gain = 0.2 + 0.2 * random();
                auto phase = 0.0;
                for (std::size_t i = first; i < n && i < first + 600; ++i) {
                    const auto t = static_cast<double>(i - first) / textureRate;
                    phase += hz * (1.0 - 25.0 * t) / textureRate;
                    out[i] += static_cast<float>(gain * std::exp(-t / 0.004) * std::sin(2.0 * kPi * phase));
                }
            }
            break;
        }
        default: {
            // A drone: two FM voices, their brightness breathing.
            auto carrier = 0.0;
            auto modulator = 0.0;
            auto upper = 0.0;
            auto upperModulator = 0.0;
            for (std::size_t i = 0; i < n; ++i) {
                const auto t = at(i);
                const auto index1 = 1.5 + 1.2 * std::sin(2.0 * kPi * 0.2 * t);
                const auto index2 = 0.8 + 0.6 * std::sin(2.0 * kPi * 0.31 * t + 2.0);
                modulator += textureRoot * 1.5 / textureRate;
                carrier += textureRoot / textureRate;
                upperModulator += textureRoot * 2.0 * 1.003 / textureRate;
                upper += textureRoot * 2.0 / textureRate;
                const auto one = std::sin(2.0 * kPi * carrier + index1 * std::sin(2.0 * kPi * modulator));
                const auto two = std::sin(2.0 * kPi * upper + index2 * std::sin(2.0 * kPi * upperModulator));
                out[i] = static_cast<float>(one + 0.5 * two);
            }
            break;
        }
        }
    }

    std::array<std::vector<float>, textureCount> data_;
};

// The textures once made (none before: a granular layer is silent until then).
std::atomic<const Textures*> madeTextures { nullptr };

// A grain's window: a raised cosine, as a table.
const std::array<float, 1'025>& window()
{
    static const auto built = [] {
        std::array<float, 1'025> table {};
        for (std::size_t i = 0; i < table.size(); ++i)
            table[i] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / 1'024.0));
        return table;
    }();
    return built;
}

} // namespace

void nebuleuseWarmUp()
{
    tables::prepare();
    (void) window();
    static const Textures built;
    madeTextures.store(&built, std::memory_order_release);
}

std::vector<Section> nebuleuseSections()
{
    return { { "VOIX", "VOICES", 0xffb8c0d0u },        { "COUCHE A", "LAYER A", 0xff9ad5ffu },  { "COUCHE B", "LAYER B", 0xffd7a6ffu },
             { "FILTRE", "FILTER", 0xffff7f9fu },      { "ENVELOPPES", "ENVELOPES", 0xff8fe3c0u }, { "LFO", "LFO", 0xffb98cffu },
             { "ORBE", "ORB", 0xfff4d06fu },           { "MODULATIONS", "MODULATIONS", 0xffff8ad8u } };
}

std::vector<Parameter> nebuleuseParameters()
{
    Table table;
    addVoicing(table, 12, 0.7f);
    for (int index = 0; index < 2; ++index) {
        table.section = 1 + index;
        const std::string prefix = index == 0 ? "a." : "b.";
        const std::string name = index == 0 ? "A · " : "B · ";
        table.choice(prefix + "source", name + "source", name + "source", kSourcesOfLayer, index == 0 ? 1 : 0);
        table.add(prefix + "level", name + "niveau", name + "level", {}, 0.0f, 1.0f, index == 0 ? 0.8f : 0.35f);
        table.add(prefix + "pan", name + "panoramique", name + "pan", {}, -1.0f, 1.0f, 0.0f);
        table.whole(prefix + "octave", name + "octave", name + "octave", {}, -3, 3, 0);
        table.whole(prefix + "semi", name + "demi-tons", name + "semitones", "st", -12, 12, 0);
        table.add(prefix + "fine", name + "fin", name + "fine", "ct", -50.0f, 50.0f, index == 0 ? 0.0f : 5.0f);
        table.choice(prefix + "table", name + "table", name + "table", kTables, 0);
        table.add(prefix + "position", name + "position", name + "position", {}, 0.0f, 1.0f, 0.3f);
        table.choice(prefix + "texture", name + "texture", name + "texture", kTextures, index == 0 ? 0 : 6);
        table.add(prefix + "scan", name + "lecture", name + "scan", {}, 0.0f, 1.0f, 0.3f);
        table.add(prefix + "travel", name + "défilement", name + "travel", {}, -1.0f, 1.0f, 0.1f);
        table.add(prefix + "grain", name + "taille des grains", name + "grain size", "s", 0.01f, 0.5f, 0.12f, Kind::continuous, true);
        table.add(prefix + "density", name + "densité", name + "density", "Hz", 2.0f, 80.0f, 18.0f, Kind::continuous, true);
        table.add(prefix + "spray", name + "dispersion", name + "spray", {}, 0.0f, 1.0f, 0.2f);
        table.add(prefix + "scatter", name + "écart de hauteur", name + "pitch scatter", {}, 0.0f, 1.0f, 0.05f);
        table.add(prefix + "reverse", name + "à l'envers", name + "reverse", {}, 0.0f, 1.0f, 0.0f);
        table.choice(prefix + "harmonia", name + "harmonia", name + "harmonia", kChords, 0);
        table.add(prefix + "harmoniaLevel", name + "harmonia · niveau", name + "harmonia · level", {}, 0.0f, 1.0f, 0.5f);
        table.add(prefix + "timbre", name + "timbre", name + "timbre", {}, -1.0f, 1.0f, 0.0f);
        table.add(prefix + "cutoff", name + "filtre", name + "filter", "Hz", 20.0f, 20'000.0f, 12'000.0f, Kind::continuous, true);
        table.add(prefix + "resonance", name + "résonance", name + "resonance", {}, 0.0f, 1.0f, 0.1f);
        table.add(prefix + "drift", name + "dérive", name + "drift", {}, 0.0f, 1.0f, 0.2f);
    }
    table.section = 3;
    table.choice("f.type", "Filtre · type", "Filter · type", kFilters, 1);
    table.add("f.cutoff", "Filtre · fréquence", "Filter · cutoff", "Hz", 20.0f, 20'000.0f, 8'000.0f, Kind::continuous, true);
    table.add("f.resonance", "Filtre · résonance", "Filter · resonance", {}, 0.0f, 1.0f, 0.1f);
    table.add("filter.key", "Filtre · suivi du clavier", "Filter · key tracking", {}, 0.0f, 1.0f, 0.3f);
    table.add("filter.velocity", "Filtre · vélocité", "Filter · velocity", {}, 0.0f, 1.0f, 0.2f);
    table.section = 4;
    table.add("amp.attack", "Volume · attaque", "Amp · attack", "s", 0.001f, 10.0f, 0.25f, Kind::continuous, true);
    table.add("amp.decay", "Volume · déclin", "Amp · decay", "s", 0.005f, 10.0f, 1.5f, Kind::continuous, true);
    table.add("amp.sustain", "Volume · maintien", "Amp · sustain", {}, 0.0f, 1.0f, 0.85f);
    table.add("amp.release", "Volume · relâchement", "Amp · release", "s", 0.005f, 12.0f, 1.8f, Kind::continuous, true);
    table.add("amp.velocity", "Volume · vélocité", "Amp · velocity", {}, 0.0f, 1.0f, 0.4f);
    table.add("env2.attack", "Env 2 · attaque", "Env 2 · attack", "s", 0.001f, 10.0f, 1.0f, Kind::continuous, true);
    table.add("env2.decay", "Env 2 · déclin", "Env 2 · decay", "s", 0.005f, 10.0f, 3.0f, Kind::continuous, true);
    table.add("env2.sustain", "Env 2 · maintien", "Env 2 · sustain", {}, 0.0f, 1.0f, 0.5f);
    table.add("env2.release", "Env 2 · relâchement", "Env 2 · release", "s", 0.005f, 12.0f, 2.0f, Kind::continuous, true);
    table.section = 5;
    table.choice("lfo1.shape", "LFO 1 · forme", "LFO 1 · shape", kLfoShapes, 0);
    table.add("lfo1.rate", "LFO 1 · vitesse", "LFO 1 · rate", "Hz", 0.02f, 20.0f, 0.3f, Kind::continuous, true);
    table.whole("lfo1.sync", "LFO 1 · synchro", "LFO 1 · sync", {}, 0, 12, 0);
    table.choice("lfo2.shape", "LFO 2 · forme", "LFO 2 · shape", kLfoShapes, 1);
    table.add("lfo2.rate", "LFO 2 · vitesse", "LFO 2 · rate", "Hz", 0.02f, 20.0f, 0.08f, Kind::continuous, true);
    table.whole("lfo2.sync", "LFO 2 · synchro", "LFO 2 · sync", {}, 0, 12, 0);
    table.section = 6;
    table.add("orb.x", "Orbe · X", "Orb · X", {}, -1.0f, 1.0f, 0.0f);
    table.add("orb.y", "Orbe · Y", "Orb · Y", {}, -1.0f, 1.0f, 0.0f);
    table.add("orb.motion", "Orbe · mouvement", "Orb · motion", {}, 0.0f, 1.0f, 0.3f);
    table.add("orb.rate", "Orbe · vitesse", "Orb · rate", "Hz", 0.01f, 4.0f, 0.07f, Kind::continuous, true);
    table.section = 7;
    // Ready to drift: the orb scans layer A and colours layer B, opens the
    // filter and the harmonia; LFO 1 breathes the grains.
    struct Default {
        int source;
        int destination;
        float amount;
    };
    static constexpr std::array<Default, slotCount> defaults { { { srcOrbX, dstScanA, 0.3f },
                                                                 { srcOrbY, dstCutoff, 0.3f },
                                                                 { srcOrbX, dstTimbreB, 0.5f },
                                                                 { srcOrbY, dstHarmonia, 0.4f },
                                                                 { srcLfo1, dstGrainA, 0.15f },
                                                                 { srcNone, dstNone, 0.0f },
                                                                 { srcNone, dstNone, 0.0f },
                                                                 { srcNone, dstNone, 0.0f } } };
    for (int slot = 1; slot <= slotCount; ++slot) {
        const auto prefix = "mod" + std::to_string(slot);
        const auto name = "Mod " + std::to_string(slot) + " · ";
        const auto& start = defaults[static_cast<std::size_t>(slot - 1)];
        table.choice(prefix + ".source", name + "source", name + "source", kSources, start.source);
        table.choice(prefix + ".dest", name + "destination", name + "destination", kDestinations, start.destination);
        table.add(prefix + ".amount", name + "quantité", name + "amount", {}, -1.0f, 1.0f, start.amount);
    }
    return table.parameters;
}

namespace {

struct Grain {
    double position {};
    double increment {};
    double windowStep {};
    double windowAt {};
    double left {};
    double right {};
    bool active {};
};

// One layer of one voice.
struct Layer {
    std::array<Grain, grainCount> grains {};
    double grainClock {};
    double scan {};
    std::array<double, 1 + chordMost> phases {};
    std::array<int, 1 + chordMost> levels {};
    std::array<double, 1 + chordMost> increments {};
    double tone {};
    std::array<Filter, 2> filters {};
    double drift {};
    double driftTarget {};
    double driftClock {};
};

class NebuleuseEngine final : public Engine {
public:
    void prepare(const double sampleRate) override
    {
        rate_ = std::max(1.0, sampleRate);
        for (auto& side : filters_)
            side.prepare(rate_);
    }

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
        for (auto& side : filters_)
            side.clear();
        toneRight_ = {};
        for (std::size_t index = 0; index < layers_.size(); ++index) {
            auto& layer = layers_[index];
            for (auto& grain : layer.grains)
                grain.active = false;
            layer.grainClock = 1.0;
            layer.scan = static_cast<double>(values[static_cast<std::size_t>((index == 0 ? layerA : layerB) + lScan)]);
            for (auto& phase : layer.phases)
                phase = 0.5 * (random() + 1.0);
            layer.tone = 0.0;
            for (auto& filter : layer.filters)
                filter.reset();
            layer.drift = 0.0;
            layer.driftTarget = random();
            layer.driftClock = 0.0;
        }
        elapsed_ = 0.0;
        lfoPhases_ = { 0.0, 0.0 };
        lfoHeld_ = { random(), random() };
        noteRandom_ = random();
        driftSource_ = 0.0;
        driftSourceTarget_ = random();
        previousLevel_ = 0.0;
    }

    void retarget(const Values& values, const int note, const float velocity) noexcept override
    {
        target_ = static_cast<double>(note);
        if (values[voicingGlide] <= 0.0f)
            pitch_ = target_;
        aimGlide(values);
        velocity_ = std::clamp(static_cast<double>(velocity), 0.0, 1.0);
        if (amp_.stage == Envelope::Stage::release)
            for (auto* envelope : { &amp_, &env2_ })
                envelope->start();
    }

    void release() noexcept override
    {
        amp_.release();
        env2_.release();
    }

    void kill() noexcept override
    {
        for (auto* envelope : { &amp_, &env2_ }) {
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
        const auto* textures = madeTextures.load(std::memory_order_acquire);
        const std::array<int, 2> bases { layerA, layerB };
        const auto filterKind = choice(fType, MultiFilter::kindCount);
        const auto velocityLevel = 1.0 - v(ampVelocity) * (1.0 - velocity_);
        const auto master = v(voicingMaster);
        const auto coefficient = [this](const double seconds, const double fraction) {
            return std::exp(-static_cast<double>(step) / (std::max(1.0e-4, seconds * fraction) * rate_));
        };
        const auto ampAttackStep = coefficient(v(ampAttack), 1.0 / 1.466);
        const auto ampDecayStep = coefficient(v(ampDecay), 1.0 / 4.6);
        const auto ampReleaseStep = coefficient(v(ampRelease), 1.0 / 4.6);
        const auto env2AttackStep = coefficient(v(env2Attack), 1.0 / 1.466);
        const auto env2DecayStep = coefficient(v(env2Decay), 1.0 / 4.6);
        const auto env2ReleaseStep = coefficient(v(env2Release), 1.0 / 4.6);
        const auto rateOf = [&](const int rateIndex, const int syncIndex) {
            if (const auto* division = noteDivisionFor(values[static_cast<std::size_t>(syncIndex)]))
                return std::max(20.0, context.tempo) / 60.0 / division->beats;
            return v(rateIndex);
        };
        const std::array<double, 2> lfoRates { rateOf(lfo1Rate, lfo1Sync), rateOf(lfo2Rate, lfo2Sync) };
        const std::array<int, 2> lfoShapes { choice(lfo1Shape, 5), choice(lfo2Shape, 5) };
        const auto bend = static_cast<double>(context.pitchBend) * std::round(v(voicingBend));
        std::array<int, slotCount> slotSources {};
        std::array<int, slotCount> slotDestinations {};
        std::array<double, slotCount> slotAmounts {};
        for (int slot = 0; slot < slotCount; ++slot) {
            slotSources[static_cast<std::size_t>(slot)] = choice(mod1Source + 3 * slot, sourceCount);
            slotDestinations[static_cast<std::size_t>(slot)] = choice(mod1Source + 3 * slot + 1, destinationCount);
            slotAmounts[static_cast<std::size_t>(slot)] = v(mod1Source + 3 * slot + 2);
        }
        const auto& hann = window();

        for (int done = 0; done < count; done += step) {
            const auto length = std::min(step, count - done);
            const auto ampLevel = amp_.step(ampAttackStep, ampDecayStep, v(ampSustain), ampReleaseStep);
            const auto env2Level = env2_.step(env2AttackStep, env2DecayStep, v(env2Sustain), env2ReleaseStep);
            if (!active())
                return false;
            const auto seconds = static_cast<double>(length) / rate_;
            elapsed_ += seconds;
            std::array<double, 2> lfo {};
            for (std::size_t index = 0; index < 2; ++index) {
                lfoPhases_[index] += lfoRates[index] * seconds;
                if (lfoPhases_[index] >= 1.0) {
                    lfoPhases_[index] -= std::floor(lfoPhases_[index]);
                    lfoHeld_[index] = random();
                }
                lfo[index] = lfoWave(lfoShapes[index], lfoPhases_[index], lfoHeld_[index]);
            }
            // The orb: its point, moved round a circle as fast as it is told
            // (on the song's clock: every voice's orb in the same place).
            const auto clock = context.clockSeconds > 0.0 ? context.clockSeconds : elapsed_;
            const auto turn = 2.0 * kPi * v(orbRate) * clock;
            const auto orbXValue = std::clamp(v(orbX) + v(orbMotion) * std::cos(turn), -1.0, 1.0);
            const auto orbYValue = std::clamp(v(orbY) + v(orbMotion) * std::sin(turn), -1.0, 1.0);
            // Drift: a slow wander, a new aim twice a second.
            driftSource_ += (driftSourceTarget_ - driftSource_) * std::min(1.0, seconds * 2.0);
            if (std::abs(driftSourceTarget_ - driftSource_) < 0.02)
                driftSourceTarget_ = random();
            const std::array<double, sourceCount> sources { 0.0,
                                                            env2Level,
                                                            lfo[0],
                                                            lfo[1],
                                                            orbXValue,
                                                            orbYValue,
                                                            velocity_,
                                                            static_cast<double>(context.modWheel),
                                                            std::clamp((target_ - 60.0) / 24.0, -1.0, 1.0),
                                                            noteRandom_,
                                                            static_cast<double>(context.aftertouch),
                                                            driftSource_ };
            std::array<double, destinationCount> mods {};
            for (int slot = 0; slot < slotCount; ++slot)
                mods[static_cast<std::size_t>(slotDestinations[static_cast<std::size_t>(slot)])]
                    += sources[static_cast<std::size_t>(slotSources[static_cast<std::size_t>(slot)])] * slotAmounts[static_cast<std::size_t>(slot)];
            mods[dstNone] = 0.0;
            if (glidePerSample_ > 0.0 && pitch_ != target_) {
                const auto move = glidePerSample_ * static_cast<double>(length);
                pitch_ = std::abs(target_ - pitch_) <= move ? target_ : pitch_ + (target_ > pitch_ ? move : -move);
            } else {
                pitch_ = target_;
            }
            const auto basePitch = pitch_ + expressionPitch + bend + 24.0 * mods[dstPitch];
            // Each layer this step: what it plays, how loud, where.
            for (std::size_t index = 0; index < 2; ++index) {
                const auto base = bases[index];
                auto& layer = layers_[index];
                auto& shape = shapes_[index];
                const auto second = index == 1;
                shape.granular = choice(base + lSource, 2) == 1;
                shape.level = std::clamp(v(base + lLevel) + mods[second ? dstLevelB : dstLevelA], 0.0, 1.0);
                const auto pan = std::clamp(v(base + lPan), -1.0, 1.0);
                shape.left = std::cos((pan + 1.0) * kPi * 0.25) * 1.41421356;
                shape.right = std::sin((pan + 1.0) * kPi * 0.25) * 1.41421356;
                shape.chord = kChordIntervals[static_cast<std::size_t>(choice(base + lHarmonia, static_cast<int>(kChordIntervals.size())))];
                shape.chordLevel = std::clamp(v(base + lHarmoniaLevel) + mods[dstHarmonia], 0.0, 1.0);
                shape.timbre = std::clamp(v(base + lTimbre) + mods[second ? dstTimbreB : dstTimbreA], -1.0, 1.0);
                // The drift: a wander of up to a third of a semitone.
                layer.driftClock += seconds;
                layer.drift += (layer.driftTarget - layer.drift) * std::min(1.0, seconds * 1.5);
                if (layer.driftClock > 0.6) {
                    layer.driftClock = 0.0;
                    layer.driftTarget = random();
                }
                const auto note = basePitch + 12.0 * std::round(v(base + lOctave)) + std::round(v(base + lSemi)) + v(base + lFine) / 100.0
                                  + layer.drift * v(base + lDrift) * 0.35;
                const auto hz = 440.0 * std::pow(2.0, (note - 69.0) / 12.0);
                shape.hz = hz;
                // The tone's corner: down to 400 Hz dark, up from 3 kHz bright.
                shape.toneCoefficient = 1.0 - std::exp(-2.0 * kPi * (shape.timbre < 0.0 ? 400.0 + 6'000.0 * (1.0 + shape.timbre) : 3'000.0) / rate_);
                const auto cutoff = std::clamp(v(base + lCutoff), 20.0, std::min(20'000.0, rate_ * 0.45));
                shape.g = std::tan(kPi * cutoff / rate_);
                shape.resonance = v(base + lResonance);
                if (!shape.granular) {
                    shape.table = choice(base + lTable, tables::sets);
                    shape.position = std::clamp(v(base + lPosition) + mods[second ? dstPositionB : dstPositionA], 0.0, 1.0);
                    for (int voice = 0; voice <= shape.chord.count; ++voice) {
                        const auto ratio = voice == 0 ? 1.0 : std::pow(2.0, shape.chord.intervals[static_cast<std::size_t>(voice - 1)] / 12.0);
                        layer.increments[static_cast<std::size_t>(voice)] = std::min(0.45, hz * ratio / rate_);
                        layer.levels[static_cast<std::size_t>(voice)] = tables::levelFor(layer.increments[static_cast<std::size_t>(voice)], 1.0);
                    }
                } else {
                    // The scan moves on by itself as the travel says (a tenth of the texture a second at most).
                    layer.scan += v(base + lTravel) * 0.1 * seconds;
                    layer.scan -= std::floor(layer.scan);
                    shape.scan = layer.scan + mods[second ? dstScanB : dstScanA];
                    shape.scan -= std::floor(shape.scan);
                    shape.grainSamples = std::clamp(v(base + lGrain) * std::pow(4.0, mods[second ? dstGrainB : dstGrainA]), 0.005, 1.0) * rate_;
                    shape.density = std::clamp(v(base + lDensity) * std::pow(4.0, mods[second ? dstDensityB : dstDensityA]), 0.5, 200.0);
                    shape.spray = v(base + lSpray);
                    shape.scatter = v(base + lScatter);
                    shape.reverse = v(base + lReverse);
                    shape.texture = choice(base + lTexture, textureCount);
                    shape.pitchRatio = hz / textureRoot * (textureRate / rate_);
                    // As loud however many grains overlap (and as a synthesis layer: their window halves them).
                    shape.grainGain = 2.5 / std::sqrt(std::max(1.0, shape.density * shape.grainSamples / rate_));
                }
            }
            const auto keyShift = v(filterKey) * (target_ - 60.0) + v(filterVelocity) * (velocity_ - 1.0) * 36.0;
            const auto cutoff = std::clamp(std::pow(2.0, (12.0 * std::log2(std::max(20.0, v(fCutoff))) + keyShift + 72.0 * mods[dstCutoff]) / 12.0), 20.0,
                                           std::min(20'000.0, rate_ * 0.45));
            const auto coefficients = MultiFilter::coefficients(filterKind, cutoff, std::clamp(v(fResonance) + mods[dstResonance], 0.0, 1.0), rate_,
                                                                filters_[0].combSize());
            const auto pan = std::clamp(mods[dstPan], -1.0, 1.0);
            const std::array<double, 2> panGains { std::min(1.0, 1.0 - pan), std::min(1.0, 1.0 + pan) };
            const auto level = ampLevel * velocityLevel * master * std::clamp(1.0 + mods[dstVolume], 0.0, 2.0) * expressionGain * 0.5;
            const auto levelFrom = previousLevel_;
            previousLevel_ = level;
            for (int i = 0; i < length; ++i) {
                const auto t = static_cast<double>(i + 1) / static_cast<double>(length);
                const auto gain = levelFrom + (level - levelFrom) * t;
                std::array<double, 2> channel {};
                for (std::size_t index = 0; index < 2; ++index) {
                    auto& layer = layers_[index];
                    const auto& shape = shapes_[index];
                    if (shape.level <= 0.0)
                        continue;
                    std::array<double, 2> out {};
                    if (!shape.granular) {
                        // The note and its chord.
                        auto x = 0.0;
                        for (int voice = 0; voice <= shape.chord.count; ++voice) {
                            const auto at = static_cast<std::size_t>(voice);
                            x += (voice == 0 ? 1.0 : shape.chordLevel)
                                 * tables::sample(shape.table, shape.position, 0, 0.0, layer.levels[at], layer.phases[at]);
                            layer.phases[at] += layer.increments[at];
                            layer.phases[at] -= std::floor(layer.phases[at]);
                        }
                        out = { x, x };
                    } else if (textures != nullptr) {
                        out = grains(layer, shape, textures->texture(shape.texture), hann);
                    }
                    for (std::size_t side = 0; side < 2; ++side) {
                        // Darker or brighter, then the layer's own filter.
                        auto x = out[side];
                        auto& tone = side == 0 ? layer.tone : toneRight_[index];
                        tone += (x - tone) * shape.toneCoefficient;
                        x = shape.timbre < 0.0 ? tone : x + (x - tone) * shape.timbre * 1.5;
                        x = layer.filters[side].variable(x, shape.g, shape.resonance, 0, false);
                        channel[side] += x * shape.level * (side == 0 ? shape.left : shape.right);
                    }
                }
                for (std::size_t side = 0; side < 2; ++side) {
                    const auto x = filters_[side].process(filterKind, coefficients, channel[side]);
                    channel[side] = x * gain * panGains[side];
                }
                left[done + i] += static_cast<float>(channel[0]);
                right[done + i] += static_cast<float>(channel[1]);
            }
        }
        return active();
    }

private:
    // A layer's settings this step.
    struct Shape {
        bool granular {};
        double level {};
        double left { 1.0 };
        double right { 1.0 };
        Chord chord { { 0.0, 0.0, 0.0 }, 0 };
        double chordLevel {};
        double timbre {};
        double toneCoefficient { 1.0 };
        double g {};
        double resonance {};
        double hz { 440.0 };
        int table {};
        double position {};
        int texture {};
        double scan {};
        double grainSamples { 4'800.0 };
        double density { 18.0 };
        double spray {};
        double scatter {};
        double reverse {};
        double pitchRatio { 1.0 };
        double grainGain { 1.0 };
    };

    // One sample of a granular layer: new grains as the density asks, each
    // read from the texture through its window, panned where it fell.
    std::array<double, 2> grains(Layer& layer, const Shape& shape, const std::vector<float>& texture, const std::array<float, 1'025>& hann) noexcept
    {
        const auto size = static_cast<double>(textureLength);
        layer.grainClock += shape.density / rate_;
        if (layer.grainClock >= 1.0) {
            layer.grainClock -= 1.0 + 0.3 * random() * 0.5;
            for (auto& grain : layer.grains)
                if (!grain.active) {
                    // Its pitch: the note, a note of the chord now and then, a little apart.
                    auto semitones = shape.scatter * shape.scatter * 12.0 * random();
                    if (shape.chord.count > 0 && 0.5 * (random() + 1.0) < shape.chordLevel * 0.75) {
                        const auto pick = std::min(shape.chord.count - 1, static_cast<int>(0.5 * (random() + 1.0) * shape.chord.count));
                        semitones += shape.chord.intervals[static_cast<std::size_t>(pick)];
                    }
                    const auto backwards = 0.5 * (random() + 1.0) < shape.reverse;
                    grain.increment = shape.pitchRatio * std::pow(2.0, semitones / 12.0) * (backwards ? -1.0 : 1.0);
                    auto start = shape.scan + shape.spray * 0.25 * random();
                    start -= std::floor(start);
                    grain.position = start * size;
                    grain.windowAt = 0.0;
                    grain.windowStep = 1'024.0 / std::max(8.0, shape.grainSamples);
                    const auto pan = 0.7 * random();
                    grain.left = std::cos((pan + 1.0) * kPi * 0.25) * 1.41421356;
                    grain.right = std::sin((pan + 1.0) * kPi * 0.25) * 1.41421356;
                    grain.active = true;
                    break;
                }
        }
        std::array<double, 2> out {};
        for (auto& grain : layer.grains) {
            if (!grain.active)
                continue;
            auto position = grain.position;
            if (position < 0.0)
                position += size * std::ceil(-position / size);
            position = std::fmod(position, size);
            const auto lower = static_cast<std::size_t>(position);
            const auto fraction = static_cast<float>(position - static_cast<double>(lower));
            const auto sample = texture[lower] + (texture[lower + 1] - texture[lower]) * fraction;
            const auto at = static_cast<std::size_t>(grain.windowAt);
            const auto shaped = static_cast<double>(sample) * static_cast<double>(hann[std::min<std::size_t>(at, 1'024)]);
            out[0] += shaped * grain.left;
            out[1] += shaped * grain.right;
            grain.position += grain.increment;
            grain.windowAt += grain.windowStep;
            if (grain.windowAt >= 1'024.0)
                grain.active = false;
        }
        out[0] *= shape.grainGain;
        out[1] *= shape.grainGain;
        return out;
    }

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
    Envelope amp_;
    Envelope env2_;
    std::array<Layer, 2> layers_ {};
    std::array<Shape, 2> shapes_ {};
    std::array<double, 2> toneRight_ {};
    std::array<MultiFilter, 2> filters_ {};
    std::array<double, 2> lfoPhases_ {};
    std::array<double, 2> lfoHeld_ {};
    double noteRandom_ {};
    double driftSource_ {};
    double driftSourceTarget_ {};
    double previousLevel_ {};
    double glidePerSample_ {};
    std::uint32_t seed_ { nextSeed() };
};

} // namespace

std::unique_ptr<Engine> makeNebuleuse()
{
    return std::make_unique<NebuleuseEngine>();
}

std::vector<float> nebuleusePicture(const Values& values, const std::string_view part, const int points)
{
    // A synthesis layer: its wave over two periods. A granular one: its
    // texture from end to end, as loud as it is at each place.
    const auto layer = part == "a" ? 0 : part == "b" ? 1 : -1;
    if (layer < 0 || points < 2)
        return {};
    const auto base = layer == 0 ? layerA : layerB;
    const auto v = [&values](const int at) { return static_cast<double>(values[static_cast<std::size_t>(at)]); };
    std::vector<float> result(static_cast<std::size_t>(points));
    if (std::lround(v(base + lSource)) == 1) {
        const auto* textures = madeTextures.load(std::memory_order_acquire);
        if (textures == nullptr)
            return result;
        const auto& texture = textures->texture(static_cast<int>(std::lround(v(base + lTexture))));
        for (int at = 0; at < points; ++at) {
            const auto from = static_cast<std::size_t>(at) * textureLength / static_cast<std::size_t>(points);
            const auto to = static_cast<std::size_t>(at + 1) * textureLength / static_cast<std::size_t>(points);
            auto peak = 0.0f;
            for (auto i = from; i < to; ++i)
                peak = std::max(peak, std::abs(texture[i]));
            result[static_cast<std::size_t>(at)] = peak;
        }
        return result;
    }
    const auto set = std::clamp(static_cast<int>(std::lround(v(base + lTable))), 0, tables::sets - 1);
    for (int at = 0; at < points; ++at) {
        const auto phase = 2.0 * static_cast<double>(at) / static_cast<double>(points - 1);
        result[static_cast<std::size_t>(at)] = static_cast<float>(tables::sample(set, v(base + lPosition), 0, 0.0, 2, phase));
    }
    return result;
}

} // namespace cydaw::engine::synth::detail
