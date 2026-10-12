// Mosaïque: a hybrid synth of the synth family. Two engines, each one of
// four kinds: analog (a shape that morphs from sine to square, its pulse
// width, a hard sync), wavetable (the family's tables), harmonic (up to
// thirty-two partials: their slope, odd and even, stretched apart like a
// piano's), modal (a mallet, a burst of noise or a bow striking eight
// resonant modes of wood, metal, glass, a string or a skin). A noise, up to
// four unison voices. Two filters of the family's seven kinds, in series or
// side by side, each engine sending to them as it is set; a saturation.
// Three envelopes, two LFOs, a drawn function, a random "Turing" sequence,
// four macros, and eight modulation slots.

#include "SynthEngines.h"
#include "SynthFilters.h"
#include "SynthParts.h"
#include "SynthTables.h"

#include <cydaw/engine/EffectCatalogue.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace cydaw::engine::synth::detail {

namespace {

using namespace parts;

// One engine's settings, from its first.
enum EngineField : int {
    eType,
    eOctave,
    eSemi,
    eFine,
    eLevel,
    ePan,
    eSend,
    eWave,
    ePw,
    eSync,
    eTable,
    ePosition,
    eMode,
    eWarp,
    ePartials,
    eBright,
    eParity,
    eStretch,
    eMaterial,
    eExciter,
    eDecay,
    eTone,
    eStrike,
    engineFieldCount
};

constexpr int engineA = voicingFieldCount;
constexpr int engineB = engineA + engineFieldCount;

// Mosaïque's settings after the two engines, in its table's order.
enum Mosaique : int {
    noiseColour = engineB + engineFieldCount,
    noiseLevel,
    noiseSend,
    unisonVoices,
    unisonDetune,
    unisonSpread,
    f1Type,
    f1Cutoff,
    f1Resonance,
    f2Type,
    f2Cutoff,
    f2Resonance,
    filterRouting,
    filterKey,
    filterVelocity,
    driveType,
    driveAmount,
    ampAttack,
    ampDecay,
    ampSustain,
    ampRelease,
    ampVelocity,
    env2Attack,
    env2Decay,
    env2Sustain,
    env2Release,
    env3Attack,
    env3Decay,
    env3Sustain,
    env3Release,
    lfo1Shape,
    lfo1Rate,
    lfo1Sync,
    lfo2Shape,
    lfo2Rate,
    lfo2Sync,
    fnPoints,
    fnSync,
    fnRate,
    fnSmooth,
    fnLoop,
    fnPoint1,
    fnPointLast = fnPoint1 + 15,
    turingSync,
    turingRate,
    turingChange,
    turingLength,
    macro1,
    macro4 = macro1 + 3,
    mod1Source,
    modLast = mod1Source + 3 * 8 - 1,
    mosaiqueCount
};
static_assert(mosaiqueCount == 141);

constexpr int slotCount = 8;
constexpr int pointCount = 16;
constexpr int partialCount = 32;
constexpr int modeCount = 8;
constexpr int unisonMost = 4;

enum EngineKind : int { analog, wavetable, harmonic, modal, kindCount };
enum Source : int {
    srcNone,
    srcEnv2,
    srcEnv3,
    srcLfo1,
    srcLfo2,
    srcFunction,
    srcTuring,
    srcVelocity,
    srcWheel,
    srcKey,
    srcRandom,
    srcPressure,
    srcMacro1,
    srcMacro2,
    srcMacro3,
    srcMacro4,
    sourceCount
};
enum Destination : int {
    dstNone,
    dstPitch,
    dstLevelA,
    dstLevelB,
    dstTimbre1A,
    dstTimbre2A,
    dstTimbre1B,
    dstTimbre2B,
    dstSendA,
    dstSendB,
    dstCutoff1,
    dstCutoff2,
    dstCutoffs,
    dstResonance,
    dstDrive,
    dstNoise,
    dstVolume,
    dstPan,
    dstDetune,
    destinationCount
};

using Choices = std::vector<std::pair<std::string, std::string>>;

const Choices kKinds { { "Analogique", "Analog" }, { "Tables", "Wavetable" }, { "Harmonique", "Harmonic" }, { "Modal", "Modal" } };
const Choices kTables { { "Classique", "Classic" }, { "Harmonique", "Harmonic" }, { "Voix", "Voices" }, { "Métal", "Metal" },
                        { "Plié", "Folded" },        { "Numérique", "Digital" },  { "Dents", "Teeth" }, { "Synchro", "Sync" } };
const Choices kModes { { "Direct", "Plain" },     { "Courbe", "Bend" },  { "Miroir", "Mirror" },        { "Formant", "Formant" },
                       { "Synchro", "Sync" },     { "Paliers", "Steps" }, { "Asymétrie", "Asymmetry" }, { "Pli", "Fold" } };
const Choices kMaterials { { "Bois", "Wood" }, { "Métal", "Metal" }, { "Verre", "Glass" }, { "Corde", "String" }, { "Peau", "Skin" } };
const Choices kExciters { { "Maillet", "Mallet" }, { "Souffle", "Burst" }, { "Archet", "Bow" } };
const Choices kFilters { { "Ladder", "Ladder" },          { "Passe-bas", "Low-pass" }, { "Passe-bande", "Band-pass" },
                         { "Passe-haut", "High-pass" },   { "Peigne", "Comb" },        { "Voyelle", "Vowel" },
                         { "Hurlement", "Scream" } };
const Choices kDrives { { "Douce", "Soft" }, { "Dure", "Hard" }, { "Plieuse", "Fold" }, { "Réducteur", "Crush" }, { "Redresseur", "Rectify" } };
const Choices kLfoShapes { { "Sinus", "Sine" },  { "Triangle", "Triangle" }, { "Dent de scie", "Saw" },
                           { "Carré", "Square" }, { "Aléatoire", "Random" } };
const Choices kSources { { "Aucune", "None" },      { "Env 2", "Env 2" },         { "Env 3", "Env 3" },      { "LFO 1", "LFO 1" },
                         { "LFO 2", "LFO 2" },      { "Fonction", "Function" },   { "Turing", "Turing" },    { "Vélocité", "Velocity" },
                         { "Molette", "Mod wheel" }, { "Clavier", "Key" },        { "Aléatoire", "Random" }, { "Pression", "Pressure" },
                         { "Macro 1", "Macro 1" },  { "Macro 2", "Macro 2" },     { "Macro 3", "Macro 3" },  { "Macro 4", "Macro 4" } };
const Choices kDestinations { { "Aucune", "None" },
                              { "Hauteur", "Pitch" },
                              { "A · niveau", "A · level" },
                              { "B · niveau", "B · level" },
                              { "A · timbre 1", "A · timbre 1" },
                              { "A · timbre 2", "A · timbre 2" },
                              { "B · timbre 1", "B · timbre 1" },
                              { "B · timbre 2", "B · timbre 2" },
                              { "A · envoi filtres", "A · filter send" },
                              { "B · envoi filtres", "B · filter send" },
                              { "Filtre 1 · fréquence", "Filter 1 · cutoff" },
                              { "Filtre 2 · fréquence", "Filter 2 · cutoff" },
                              { "Filtres · fréquence", "Filters · cutoff" },
                              { "Résonance", "Resonance" },
                              { "Saturation", "Drive" },
                              { "Bruit", "Noise" },
                              { "Volume", "Volume" },
                              { "Panoramique", "Pan" },
                              { "Désaccord", "Detune" } };

// The modes of each material: their frequencies (times the note's), how much
// faster the higher ones die.
struct Material {
    std::array<double, modeCount> ratios;
    double damping;
};
constexpr std::array<Material, 5> kMaterialModes { {
    { { 1.0, 3.99, 10.65, 2.76, 5.40, 8.93, 13.34, 18.64 }, 0.9 },   // wood: a marimba's bar
    { { 1.0, 2.76, 5.40, 8.93, 4.0, 13.34, 10.0, 18.64 }, 0.12 },    // metal: a free bar
    { { 1.0, 2.32, 4.25, 6.63, 9.38, 12.5, 1.61, 3.12 }, 0.3 },      // glass: a bowl
    { { 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0 }, 0.08 },            // a string
    { { 1.0, 1.59, 2.14, 2.30, 2.65, 2.92, 3.16, 3.50 }, 0.7 },      // a skin
} };

// An engine's shape in analog: sine, triangle, saw, square, a morph across them.
double analogSample(const double morph, const double phase, const double increment, const double width) noexcept
{
    const auto at = std::clamp(morph, 0.0, 1.0) * 3.0;
    const auto lower = std::min(static_cast<int>(at), 2);
    const auto mix = at - static_cast<double>(lower);
    const auto shape = [&](const int which) {
        switch (which) {
        case 0: return std::sin(2.0 * kPi * phase);
        case 1: return oscillator(2, phase, increment, width);
        case 2: return oscillator(0, phase, increment, width);
        default: return oscillator(1, phase, increment, width);
        }
    };
    const auto a = shape(lower);
    return mix > 1.0e-6 ? a + (shape(lower + 1) - a) * mix : a;
}

// A harmonic engine's partials: their amplitudes and their frequencies
// (times the note's), for its settings.
struct Spectrum {
    std::array<double, partialCount> amplitude {};
    std::array<double, partialCount> ratio {};
    int count {};
};

Spectrum spectrumOf(const int partials, const double bright, const double parity, const double stretch) noexcept
{
    Spectrum result;
    result.count = std::clamp(partials, 1, partialCount);
    const auto slope = 1.0 - std::clamp(bright, -1.0, 1.0);
    const auto even = std::clamp(1.0 + parity, 0.0, 1.0);
    const auto odd = std::clamp(1.0 - parity, 0.0, 1.0);
    const auto b = std::clamp(stretch, 0.0, 1.0) * 0.0008;
    auto power = 0.0;
    for (int index = 0; index < result.count; ++index) {
        const auto n = static_cast<double>(index + 1);
        auto amplitude = std::pow(n, -slope);
        if (index > 0)
            amplitude *= (index + 1) % 2 == 0 ? even : odd;
        result.amplitude[static_cast<std::size_t>(index)] = amplitude;
        result.ratio[static_cast<std::size_t>(index)] = n * std::sqrt(1.0 + b * n * n);
        power += amplitude * amplitude;
    }
    // As loud whatever the partials.
    const auto scale = std::sqrt(0.5 / std::max(1.0e-9, power));
    for (int index = 0; index < result.count; ++index)
        result.amplitude[static_cast<std::size_t>(index)] *= scale;
    return result;
}

// A modal engine's modes: their weights for a tone and a strike.
std::array<double, modeCount> modeWeights(const double tone, const double strike) noexcept
{
    std::array<double, modeCount> weights {};
    auto sum = 0.0;
    for (int k = 0; k < modeCount; ++k) {
        const auto brightness = std::pow(0.15 + 0.85 * std::clamp(tone, 0.0, 1.0), static_cast<double>(k));
        const auto place = 0.15 + 0.85 * std::abs(std::sin(kPi * static_cast<double>(k + 1) * (0.05 + 0.9 * std::clamp(strike, 0.0, 1.0))));
        weights[static_cast<std::size_t>(k)] = brightness * place;
        sum += weights[static_cast<std::size_t>(k)];
    }
    for (auto& weight : weights)
        weight *= 1.5 / std::max(1.0e-9, sum);
    return weights;
}

} // namespace

std::vector<Section> mosaiqueSections()
{
    return { { "VOIX", "VOICES", 0xffb8c0d0u },        { "MOTEUR A", "ENGINE A", 0xff6ee7c7u },   { "MOTEUR B", "ENGINE B", 0xffffb86bu },
             { "SOURCES", "SOURCES", 0xffd0d6e0u },    { "FILTRES", "FILTERS", 0xffff6b8bu },     { "ENVELOPPES", "ENVELOPES", 0xff8fd3ffu },
             { "LFO", "LFO", 0xffb98cffu },             { "FONCTION", "FUNCTION", 0xfff5e05cu },   { "ALÉATOIRE", "RANDOM", 0xffff9f6bu },
             { "MACROS", "MACROS", 0xfff2f2f2u },       { "MODULATIONS", "MODULATIONS", 0xffff8ad8u } };
}

std::vector<Parameter> mosaiqueParameters()
{
    Table table;
    addVoicing(table, 12, 0.7f);
    for (int index = 0; index < 2; ++index) {
        table.section = 1 + index;
        const std::string prefix = index == 0 ? "a." : "b.";
        const std::string name = index == 0 ? "A · " : "B · ";
        table.choice(prefix + "type", name + "type", name + "kind", kKinds, index == 0 ? analog : wavetable);
        table.whole(prefix + "octave", name + "octave", name + "octave", {}, -3, 3, 0);
        table.whole(prefix + "semi", name + "demi-tons", name + "semitones", "st", -12, 12, 0);
        table.add(prefix + "fine", name + "fin", name + "fine", "ct", -50.0f, 50.0f, index == 0 ? 0.0f : 6.0f);
        table.add(prefix + "level", name + "niveau", name + "level", {}, 0.0f, 1.0f, index == 0 ? 0.8f : 0.45f);
        table.add(prefix + "pan", name + "panoramique", name + "pan", {}, -1.0f, 1.0f, 0.0f);
        table.add(prefix + "send", name + "envoi filtre 1 → 2", name + "filter 1 → 2 send", {}, 0.0f, 1.0f, 0.0f);
        table.add(prefix + "wave", name + "forme", name + "shape", {}, 0.0f, 1.0f, 0.67f);
        table.add(prefix + "pw", name + "largeur", name + "width", {}, 0.05f, 0.95f, 0.5f);
        table.add(prefix + "sync", name + "synchro", name + "sync", "st", 0.0f, 24.0f, 0.0f);
        table.choice(prefix + "table", name + "table", name + "table", kTables, 1);
        table.add(prefix + "position", name + "position", name + "position", {}, 0.0f, 1.0f, 0.4f);
        table.choice(prefix + "mode", name + "mode", name + "mode", kModes, 0);
        table.add(prefix + "warp", name + "déformation", name + "warp", {}, 0.0f, 1.0f, 0.0f);
        table.whole(prefix + "partials", name + "partiels", name + "partials", {}, 1, partialCount, 16);
        table.add(prefix + "bright", name + "brillance", name + "brightness", {}, -1.0f, 1.0f, 0.0f);
        table.add(prefix + "parity", name + "pairs et impairs", name + "odd and even", {}, -1.0f, 1.0f, 0.0f);
        table.add(prefix + "stretch", name + "étirement", name + "stretch", {}, 0.0f, 1.0f, 0.0f);
        table.choice(prefix + "material", name + "matière", name + "material", kMaterials, 0);
        table.choice(prefix + "exciter", name + "excitateur", name + "exciter", kExciters, 0);
        table.add(prefix + "decay", name + "résonance", name + "ring", "s", 0.05f, 10.0f, 1.2f, Kind::continuous, true);
        table.add(prefix + "tone", name + "timbre", name + "tone", {}, 0.0f, 1.0f, 0.5f);
        table.add(prefix + "strike", name + "point de frappe", name + "strike point", {}, 0.0f, 1.0f, 0.3f);
    }
    table.section = 3;
    table.choice("noise.colour", "Bruit · couleur", "Noise · colour",
                 { { "Blanc", "White" }, { "Rose", "Pink" }, { "Crépitement", "Crackle" }, { "Souffle", "Breath" } }, 0);
    table.add("noise.level", "Bruit", "Noise", {}, 0.0f, 1.0f, 0.0f);
    table.add("noise.send", "Bruit · envoi filtre 1 → 2", "Noise · filter 1 → 2 send", {}, 0.0f, 1.0f, 0.0f);
    table.whole("unison.voices", "Unisson", "Unison", {}, 1, unisonMost, 1);
    table.add("unison.detune", "Unisson · désaccord", "Unison · detune", "ct", 0.0f, 50.0f, 12.0f);
    table.add("unison.spread", "Unisson · largeur", "Unison · spread", {}, 0.0f, 1.0f, 0.8f);
    table.section = 4;
    table.choice("f1.type", "Filtre 1 · type", "Filter 1 · type", kFilters, 0);
    table.add("f1.cutoff", "Filtre 1 · fréquence", "Filter 1 · cutoff", "Hz", 20.0f, 20'000.0f, 2'400.0f, Kind::continuous, true);
    table.add("f1.resonance", "Filtre 1 · résonance", "Filter 1 · resonance", {}, 0.0f, 1.0f, 0.2f);
    table.choice("f2.type", "Filtre 2 · type", "Filter 2 · type", kFilters, 1);
    table.add("f2.cutoff", "Filtre 2 · fréquence", "Filter 2 · cutoff", "Hz", 20.0f, 20'000.0f, 5'000.0f, Kind::continuous, true);
    table.add("f2.resonance", "Filtre 2 · résonance", "Filter 2 · resonance", {}, 0.0f, 1.0f, 0.1f);
    table.choice("filter.routing", "Filtres · montage", "Filters · routing", { { "Série", "Serial" }, { "Parallèle", "Parallel" } }, 1);
    table.add("filter.key", "Filtres · suivi du clavier", "Filters · key tracking", {}, 0.0f, 1.0f, 0.4f);
    table.add("filter.velocity", "Filtres · vélocité", "Filters · velocity", {}, 0.0f, 1.0f, 0.2f);
    table.choice("drive.type", "Saturation · type", "Drive · type", kDrives, 0);
    table.add("drive.amount", "Saturation", "Drive", {}, 0.0f, 1.0f, 0.0f);
    table.section = 5;
    table.add("amp.attack", "Volume · attaque", "Amp · attack", "s", 0.001f, 10.0f, 0.004f, Kind::continuous, true);
    table.add("amp.decay", "Volume · déclin", "Amp · decay", "s", 0.005f, 10.0f, 0.8f, Kind::continuous, true);
    table.add("amp.sustain", "Volume · maintien", "Amp · sustain", {}, 0.0f, 1.0f, 0.8f);
    table.add("amp.release", "Volume · relâchement", "Amp · release", "s", 0.005f, 12.0f, 0.4f, Kind::continuous, true);
    table.add("amp.velocity", "Volume · vélocité", "Amp · velocity", {}, 0.0f, 1.0f, 0.5f);
    for (int index = 2; index <= 3; ++index) {
        const auto prefix = "env" + std::to_string(index);
        const auto name = "Env " + std::to_string(index) + " · ";
        table.add(prefix + ".attack", name + "attaque", name + "attack", "s", 0.001f, 10.0f, index == 2 ? 0.002f : 0.6f, Kind::continuous, true);
        table.add(prefix + ".decay", name + "déclin", name + "decay", "s", 0.005f, 10.0f, index == 2 ? 0.6f : 2.0f, Kind::continuous, true);
        table.add(prefix + ".sustain", name + "maintien", name + "sustain", {}, 0.0f, 1.0f, index == 2 ? 0.2f : 0.5f);
        table.add(prefix + ".release", name + "relâchement", name + "release", "s", 0.005f, 12.0f, index == 2 ? 0.4f : 1.2f, Kind::continuous, true);
    }
    table.section = 6;
    table.choice("lfo1.shape", "LFO 1 · forme", "LFO 1 · shape", kLfoShapes, 0);
    table.add("lfo1.rate", "LFO 1 · vitesse", "LFO 1 · rate", "Hz", 0.05f, 30.0f, 3.0f, Kind::continuous, true);
    table.whole("lfo1.sync", "LFO 1 · synchro", "LFO 1 · sync", {}, 0, 12, 0);
    table.choice("lfo2.shape", "LFO 2 · forme", "LFO 2 · shape", kLfoShapes, 1);
    table.add("lfo2.rate", "LFO 2 · vitesse", "LFO 2 · rate", "Hz", 0.05f, 30.0f, 0.3f, Kind::continuous, true);
    table.whole("lfo2.sync", "LFO 2 · synchro", "LFO 2 · sync", {}, 0, 12, 0);
    table.section = 7;
    table.whole("fn.points", "Fonction · points", "Function · points", {}, 2, pointCount, 8);
    table.whole("fn.sync", "Fonction · synchro", "Function · sync", {}, 0, 12, 12);
    table.add("fn.rate", "Fonction · vitesse", "Function · rate", "Hz", 0.05f, 20.0f, 0.5f, Kind::continuous, true);
    table.add("fn.smooth", "Fonction · lissage", "Function · smoothing", {}, 0.0f, 1.0f, 1.0f);
    table.toggle("fn.loop", "Fonction · en boucle", "Function · looping", true);
    static constexpr std::array<float, pointCount> curve { 0.0f, 0.8f, 1.0f, 0.6f, 0.4f, 0.3f, 0.15f, 0.0f,
                                                           0.0f, 0.5f, 1.0f, 0.5f, 0.0f, 0.5f, 1.0f, 0.5f };
    for (int point = 0; point < pointCount; ++point)
        table.add("fn.point" + std::to_string(point + 1), "Fonction · point " + std::to_string(point + 1),
                  "Function · point " + std::to_string(point + 1), {}, 0.0f, 1.0f, curve[static_cast<std::size_t>(point)]);
    table.section = 8;
    table.whole("turing.sync", "Turing · synchro", "Turing · sync", {}, 0, 12, 3);
    table.add("turing.rate", "Turing · vitesse", "Turing · rate", "Hz", 0.1f, 30.0f, 4.0f, Kind::continuous, true);
    table.add("turing.change", "Turing · changement", "Turing · change", {}, 0.0f, 1.0f, 0.2f);
    table.whole("turing.length", "Turing · longueur", "Turing · length", {}, 2, 16, 8);
    table.section = 9;
    for (int macro = 1; macro <= 4; ++macro)
        table.add("macro" + std::to_string(macro), "Macro " + std::to_string(macro), "Macro " + std::to_string(macro), {}, 0.0f, 1.0f, 0.0f);
    table.section = 10;
    // Ready to play: envelope 2 opens the filters; the macros reach the
    // engines' timbres, the filters, the width and the drive.
    struct Default {
        int source;
        int destination;
        float amount;
    };
    static constexpr std::array<Default, slotCount> defaults { { { srcEnv2, dstCutoffs, 0.3f },
                                                                 { srcMacro1, dstTimbre1A, 0.5f },
                                                                 { srcMacro1, dstTimbre1B, 0.5f },
                                                                 { srcMacro2, dstCutoffs, 0.5f },
                                                                 { srcMacro3, dstDetune, 1.0f },
                                                                 { srcMacro4, dstDrive, 0.8f },
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

// One engine of one unison voice: whatever its kind needs.
struct EngineState {
    double phase {};
    double master {};
    double increment {};
    int level {};
    std::array<bool, partialCount> audible {};
    std::array<double, partialCount> cosine {};
    std::array<double, partialCount> sine {};
    std::array<double, partialCount> rotateCos {};
    std::array<double, partialCount> rotateSin {};
    std::array<double, modeCount> y1 {};
    std::array<double, modeCount> y2 {};
    std::array<double, modeCount> a1 {};
    std::array<double, modeCount> a2 {};
    std::array<double, modeCount> input {};
    double bowLow {};
    void reset(const double startPhase) noexcept
    {
        phase = startPhase;
        master = startPhase;
        for (int index = 0; index < partialCount; ++index) {
            cosine[static_cast<std::size_t>(index)] = 1.0;
            sine[static_cast<std::size_t>(index)] = 0.0;
        }
        y1 = {};
        y2 = {};
        bowLow = 0.0;
    }
};

class MosaiqueEngine final : public Engine {
public:
    void prepare(const double sampleRate) override
    {
        rate_ = std::max(1.0, sampleRate);
        for (auto& filter : filters_)
            for (auto& side : filter)
                side.prepare(rate_);
    }

    void start(const Values& values, const int note, const float velocity, const double glideFrom, const double sampleRate) noexcept override
    {
        rate_ = std::max(1.0, sampleRate);
        target_ = static_cast<double>(note);
        pitch_ = glideFrom >= 0.0 && values[voicingGlide] > 0.0f ? glideFrom : target_;
        aimGlide(values);
        velocity_ = std::clamp(static_cast<double>(velocity), 0.0, 1.0);
        for (auto* envelope : { &amp_, &env2_, &env3_ }) {
            envelope->stage = Envelope::Stage::idle;
            envelope->level = 0.0;
            envelope->start();
        }
        for (auto& filter : filters_)
            for (auto& side : filter)
                side.clear();
        dcIn_ = {};
        dcOut_ = {};
        held_ = {};
        holdCount_ = 0;
        pink_ = {};
        elapsed_ = 0.0;
        lfoPhases_ = { 0.0, 0.0 };
        lfoHeld_ = { random(), random() };
        noteRandom_ = random();
        turing_ = static_cast<std::uint32_t>(seed_ >> 8);
        turingClock_ = 0.0;
        exciteLeft_ = 0;
        exciteLength_ = 0;
        burstLeft_ = 0;
        for (auto& voice : subs_)
            for (auto& engine : voice.engines)
                engine.reset(0.5 * (random() + 1.0));
        // The mallet and the burst strike once, at the start.
        excite_ = true;
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
            for (auto* envelope : { &amp_, &env2_, &env3_ })
                envelope->start();
            excite_ = true;
        }
    }

    void release() noexcept override
    {
        amp_.release();
        env2_.release();
        env3_.release();
    }

    void kill() noexcept override
    {
        for (auto* envelope : { &amp_, &env2_, &env3_ }) {
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
        const std::array<int, 2> bases { engineA, engineB };
        std::array<int, 2> kinds {};
        std::array<double, 2> offsets {};
        for (std::size_t engine = 0; engine < 2; ++engine) {
            const auto base = bases[engine];
            kinds[engine] = choice(base + eType, kindCount);
            offsets[engine] = 12.0 * std::round(v(base + eOctave)) + std::round(v(base + eSemi)) + v(base + eFine) / 100.0;
        }
        const auto voices = std::clamp(static_cast<int>(std::lround(v(unisonVoices))), 1, unisonMost);
        const auto noiseKind = choice(noiseColour, 4);
        const std::array<int, 2> types { choice(f1Type, MultiFilter::kindCount), choice(f2Type, MultiFilter::kindCount) };
        const auto parallel = choice(filterRouting, 2) == 1;
        const auto driveKind = choice(driveType, 5);
        const auto velocityLevel = 1.0 - v(ampVelocity) * (1.0 - velocity_);
        const auto master = v(voicingMaster);
        const auto coefficient = [this](const double seconds, const double fraction) {
            return std::exp(-static_cast<double>(step) / (std::max(1.0e-4, seconds * fraction) * rate_));
        };
        struct Steps {
            double attack, decay, sustain, release;
        };
        const auto stepsOf = [&](const int attack) {
            return Steps { coefficient(v(attack), 1.0 / 1.466), coefficient(v(attack + 1), 1.0 / 4.6), v(attack + 2),
                           coefficient(v(attack + 3), 1.0 / 4.6) };
        };
        const auto ampSteps = stepsOf(ampAttack);
        const auto env2Steps = stepsOf(env2Attack);
        const auto env3Steps = stepsOf(env3Attack);
        const auto rateOf = [&](const int rateIndex, const int syncIndex) {
            if (const auto* division = noteDivisionFor(values[static_cast<std::size_t>(syncIndex)]))
                return std::max(20.0, context.tempo) / 60.0 / division->beats;
            return v(rateIndex);
        };
        const std::array<double, 2> lfoRates { rateOf(lfo1Rate, lfo1Sync), rateOf(lfo2Rate, lfo2Sync) };
        const std::array<int, 2> lfoShapes { choice(lfo1Shape, 5), choice(lfo2Shape, 5) };
        const auto functionRate = rateOf(fnRate, fnSync);
        const auto functionPoints = std::clamp(static_cast<int>(std::lround(v(fnPoints))), 2, pointCount);
        const auto functionSmooth = v(fnSmooth);
        const auto functionLoops = v(fnLoop) >= 0.5;
        const auto turingRateValue = rateOf(turingRate, turingSync);
        const auto turingBits = std::clamp(static_cast<int>(std::lround(v(turingLength))), 2, 16);
        const auto turingMask = (1u << static_cast<unsigned>(turingBits)) - 1u;
        const auto bend = static_cast<double>(context.pitchBend) * std::round(v(voicingBend));
        std::array<int, slotCount> slotSources {};
        std::array<int, slotCount> slotDestinations {};
        std::array<double, slotCount> slotAmounts {};
        for (int slot = 0; slot < slotCount; ++slot) {
            slotSources[static_cast<std::size_t>(slot)] = choice(mod1Source + 3 * slot, sourceCount);
            slotDestinations[static_cast<std::size_t>(slot)] = choice(mod1Source + 3 * slot + 1, destinationCount);
            slotAmounts[static_cast<std::size_t>(slot)] = v(mod1Source + 3 * slot + 2);
        }

        for (int done = 0; done < count; done += step) {
            const auto length = std::min(step, count - done);
            const auto ampLevel = amp_.step(ampSteps.attack, ampSteps.decay, ampSteps.sustain, ampSteps.release);
            const auto env2Level = env2_.step(env2Steps.attack, env2Steps.decay, env2Steps.sustain, env2Steps.release);
            const auto env3Level = env3_.step(env3Steps.attack, env3Steps.decay, env3Steps.sustain, env3Steps.release);
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
            // The function: its points one after the other, each eased into the next.
            const auto travelled = elapsed_ * functionRate;
            const auto cycle = functionLoops ? travelled - std::floor(travelled) : std::min(travelled, 0.999999);
            const auto along = cycle * static_cast<double>(functionLoops ? functionPoints : functionPoints - 1);
            const auto point = std::min(static_cast<int>(along), functionPoints - 1);
            const auto next = functionLoops ? (point + 1) % functionPoints : std::min(point + 1, functionPoints - 1);
            const auto within = along - static_cast<double>(point);
            const auto from = v(fnPoint1 + point);
            const auto to = v(fnPoint1 + next);
            const auto ease = functionSmooth > 1.0e-3 ? std::clamp((within - (1.0 - functionSmooth)) / functionSmooth, 0.0, 1.0) : 0.0;
            const auto functionValue = from + (to - from) * ease;
            // The Turing sequence: a shift register whose bits change now and then.
            turingClock_ += turingRateValue * seconds;
            while (turingClock_ >= 1.0) {
                turingClock_ -= 1.0;
                auto bit = (turing_ >> static_cast<unsigned>(turingBits - 1)) & 1u;
                if (0.5 * (random() + 1.0) < v(turingChange))
                    bit = random() > 0.0 ? 1u : 0u;
                turing_ = ((turing_ << 1u) | bit) & turingMask;
            }
            const auto turingValue = static_cast<double>(turing_ & turingMask) / static_cast<double>(turingMask);
            const std::array<double, sourceCount> sources { 0.0,
                                                            env2Level,
                                                            env3Level,
                                                            lfo[0],
                                                            lfo[1],
                                                            functionValue,
                                                            turingValue,
                                                            velocity_,
                                                            static_cast<double>(context.modWheel),
                                                            std::clamp((target_ - 60.0) / 24.0, -1.0, 1.0),
                                                            noteRandom_,
                                                            static_cast<double>(context.aftertouch),
                                                            v(macro1),
                                                            v(macro1 + 1),
                                                            v(macro1 + 2),
                                                            v(macro1 + 3) };
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
            // Each engine's state for this step: its level, its pan, its send, its timbres.
            for (std::size_t engine = 0; engine < 2; ++engine) {
                const auto base = bases[engine];
                auto& shape = shapes_[engine];
                const auto t1 = mods[engine == 0 ? dstTimbre1A : dstTimbre1B];
                const auto t2 = mods[engine == 0 ? dstTimbre2A : dstTimbre2B];
                shape.level = std::clamp(v(base + eLevel) + mods[engine == 0 ? dstLevelA : dstLevelB], 0.0, 1.0);
                const auto pan = std::clamp(v(base + ePan), -1.0, 1.0);
                shape.left = std::cos((pan + 1.0) * kPi * 0.25) * 1.41421356;
                shape.right = std::sin((pan + 1.0) * kPi * 0.25) * 1.41421356;
                shape.send = std::clamp(v(base + eSend) + mods[engine == 0 ? dstSendA : dstSendB], 0.0, 1.0);
                shape.morph = std::clamp(v(base + eWave) + t1, 0.0, 1.0);
                shape.width = std::clamp(v(base + ePw) + 0.45 * t2, 0.05, 0.95);
                shape.syncRatio = std::pow(2.0, std::max(0.0, v(base + eSync)) / 12.0);
                shape.table = choice(base + eTable, tables::sets);
                shape.position = std::clamp(v(base + ePosition) + t1, 0.0, 1.0);
                shape.mode = choice(base + eMode, tables::modes);
                shape.warp = std::clamp(v(base + eWarp) + t2, 0.0, 1.0);
                if (kinds[engine] == harmonic)
                    shape.spectrum = spectrumOf(static_cast<int>(std::lround(v(base + ePartials))), v(base + eBright) + t1, v(base + eParity) + t2,
                                                v(base + eStretch));
                if (kinds[engine] == modal) {
                    shape.material = choice(base + eMaterial, static_cast<int>(kMaterialModes.size()));
                    shape.exciter = choice(base + eExciter, 3);
                    shape.weights = modeWeights(v(base + eTone) + t1, v(base + eStrike) + t2);
                    shape.ring = v(base + eDecay);
                    shape.tone = std::clamp(v(base + eTone) + t1, 0.0, 1.0);
                }
            }
            // The mallet and the burst: how long they strike, from the tone.
            if (excite_) {
                excite_ = false;
                const auto tone = std::max(kinds[0] == modal ? shapes_[0].tone : 0.0, kinds[1] == modal ? shapes_[1].tone : 0.0);
                exciteLength_ = std::max(2, static_cast<int>((0.006 - 0.0055 * tone) * rate_));
                exciteLeft_ = exciteLength_;
                burstLeft_ = 4 * exciteLength_;
            }
            const auto detune = v(unisonDetune) * std::clamp(1.0 + mods[dstDetune], 0.0, 3.0);
            const auto spread = v(unisonSpread);
            for (int voice = 0; voice < voices; ++voice) {
                auto& unison = subs_[static_cast<std::size_t>(voice)];
                const auto offset = voices > 1 ? 2.0 * static_cast<double>(voice) / static_cast<double>(voices - 1) - 1.0 : 0.0;
                const auto note = basePitch + offset * detune / 100.0;
                const auto pan = std::clamp(offset * spread, -1.0, 1.0);
                unison.left = std::cos((pan + 1.0) * kPi * 0.25);
                unison.right = std::sin((pan + 1.0) * kPi * 0.25);
                for (std::size_t engine = 0; engine < 2; ++engine) {
                    auto& state = unison.engines[engine];
                    const auto& shape = shapes_[engine];
                    const auto hz = 440.0 * std::pow(2.0, (note + offsets[engine] - 69.0) / 12.0);
                    state.increment = std::min(0.45, hz / rate_);
                    state.level = tables::levelFor(state.increment, tables::warpStretch(shape.mode, shape.warp));
                    if (kinds[engine] == harmonic)
                        for (int partial = 0; partial < shape.spectrum.count; ++partial) {
                            const auto w = 2.0 * kPi * std::min(0.49, hz * shape.spectrum.ratio[static_cast<std::size_t>(partial)] / rate_);
                            state.rotateCos[static_cast<std::size_t>(partial)] = std::cos(w);
                            state.rotateSin[static_cast<std::size_t>(partial)] = std::sin(w);
                            // A partial past the top is left out.
                            state.audible[static_cast<std::size_t>(partial)]
                                = hz * shape.spectrum.ratio[static_cast<std::size_t>(partial)] < rate_ * 0.45;
                            // Kept on its circle.
                            auto& c = state.cosine[static_cast<std::size_t>(partial)];
                            auto& s = state.sine[static_cast<std::size_t>(partial)];
                            const auto norm = 1.0 / std::max(1.0e-9, std::sqrt(c * c + s * s));
                            c *= norm;
                            s *= norm;
                        }
                    if (kinds[engine] == modal) {
                        const auto& material = kMaterialModes[static_cast<std::size_t>(shape.material)];
                        for (int k = 0; k < modeCount; ++k) {
                            const auto modeHz = hz * material.ratios[static_cast<std::size_t>(k)];
                            const auto w = 2.0 * kPi * modeHz / rate_;
                            const auto audible = modeHz < rate_ * 0.45;
                            const auto seconds60 = shape.ring / (1.0 + material.damping * static_cast<double>(k));
                            const auto r = std::pow(10.0, -3.0 / std::max(1.0, seconds60 * rate_));
                            state.a1[static_cast<std::size_t>(k)] = audible ? 2.0 * r * std::cos(w) : 0.0;
                            state.a2[static_cast<std::size_t>(k)] = audible ? -r * r : 0.0;
                            state.input[static_cast<std::size_t>(k)] = audible ? std::sin(w) * shape.weights[static_cast<std::size_t>(k)] : 0.0;
                        }
                    }
                }
            }
            // The filters: their cutoffs as the knobs, the key, the velocity and the routings move them.
            const auto keyShift = v(filterKey) * (target_ - 60.0) + v(filterVelocity) * (velocity_ - 1.0) * 36.0;
            const auto cutoffOf = [&](const int knob, const double moved) {
                return std::clamp(std::pow(2.0, (12.0 * std::log2(std::max(20.0, v(knob))) + keyShift + 72.0 * moved) / 12.0), 20.0,
                                  std::min(20'000.0, rate_ * 0.45));
            };
            const std::array<MultiFilter::Coefficients, 2> filters {
                MultiFilter::coefficients(types[0], cutoffOf(f1Cutoff, mods[dstCutoff1] + mods[dstCutoffs]),
                                          std::clamp(v(f1Resonance) + mods[dstResonance], 0.0, 1.0), rate_, filters_[0][0].combSize()),
                MultiFilter::coefficients(types[1], cutoffOf(f2Cutoff, mods[dstCutoff2] + mods[dstCutoffs]),
                                          std::clamp(v(f2Resonance) + mods[dstResonance], 0.0, 1.0), rate_, filters_[1][0].combSize())
            };
            const auto noise = std::clamp(v(noiseLevel) + mods[dstNoise], 0.0, 1.0);
            const auto noiseSendValue = v(noiseSend);
            const auto drive = std::clamp(v(driveAmount) + mods[dstDrive], 0.0, 1.0);
            const auto pan = std::clamp(mods[dstPan], -1.0, 1.0);
            const std::array<double, 2> panGains { std::min(1.0, 1.0 - pan), std::min(1.0, 1.0 + pan) };
            const auto secondUsed = !parallel || shapes_[0].send > 0.0 || shapes_[1].send > 0.0 || (noise > 0.0 && noiseSendValue > 0.0);
            const auto level = ampLevel * velocityLevel * master * std::clamp(1.0 + mods[dstVolume], 0.0, 2.0) * expressionGain * 0.5
                               / std::sqrt(static_cast<double>(voices));
            const auto levelFrom = previousLevel_;
            previousLevel_ = level;
            for (int i = 0; i < length; ++i) {
                const auto t = static_cast<double>(i + 1) / static_cast<double>(length);
                const auto gain = levelFrom + (level - levelFrom) * t;
                // What each filter gets, on each side.
                std::array<std::array<double, 2>, 2> into {}; // [filter][side]
                auto excitation = 0.0;
                if (exciteLeft_ > 0) {
                    const auto elapsed = static_cast<double>(exciteLength_ - exciteLeft_);
                    excitation = std::sin(kPi * elapsed / static_cast<double>(exciteLength_)) / (0.64 * static_cast<double>(exciteLength_));
                    --exciteLeft_;
                }
                const auto white = random();
                // The burst: noise four times as long as a mallet's blow, fading.
                auto burst = 0.0;
                if (burstLeft_ > 0) {
                    burst = white * 3.0 * static_cast<double>(burstLeft_) / static_cast<double>(4 * exciteLength_)
                            / std::sqrt(static_cast<double>(4 * exciteLength_));
                    --burstLeft_;
                }
                for (int voice = 0; voice < voices; ++voice) {
                    auto& unison = subs_[static_cast<std::size_t>(voice)];
                    for (std::size_t engine = 0; engine < 2; ++engine) {
                        const auto& shape = shapes_[engine];
                        if (shape.level <= 0.0)
                            continue;
                        auto& state = unison.engines[engine];
                        auto x = 0.0;
                        switch (kinds[engine]) {
                        case analog: {
                            if (shape.syncRatio > 1.0001) {
                                const auto slave = state.master * shape.syncRatio;
                                x = analogSample(shape.morph, slave - std::floor(slave), std::min(0.45, state.increment * shape.syncRatio), shape.width);
                            } else {
                                x = analogSample(shape.morph, state.master, state.increment, shape.width);
                            }
                            state.master += state.increment;
                            state.master -= std::floor(state.master);
                            break;
                        }
                        case wavetable:
                            x = tables::sample(shape.table, shape.position, shape.mode, shape.warp, state.level, state.phase);
                            state.phase += state.increment;
                            state.phase -= std::floor(state.phase);
                            break;
                        case harmonic:
                            for (int partial = 0; partial < shape.spectrum.count; ++partial) {
                                const auto n = static_cast<std::size_t>(partial);
                                const auto c = state.cosine[n];
                                const auto s = state.sine[n];
                                state.cosine[n] = c * state.rotateCos[n] - s * state.rotateSin[n];
                                state.sine[n] = s * state.rotateCos[n] + c * state.rotateSin[n];
                                if (state.audible[n])
                                    x += shape.spectrum.amplitude[n] * s;
                            }
                            break;
                        default: {
                            // The exciter: a mallet's blow, a burst of noise, a bow's friction held.
                            auto push = 0.0;
                            if (shape.exciter == 0)
                                push = excitation;
                            else if (shape.exciter == 1)
                                push = burst;
                            else {
                                state.bowLow += (white - state.bowLow) * (0.05 + 0.4 * shape.tone);
                                push = state.bowLow * (releasing() ? 0.0 : 0.01);
                            }
                            for (int k = 0; k < modeCount; ++k) {
                                const auto mode = static_cast<std::size_t>(k);
                                const auto y = push * state.input[mode] + state.a1[mode] * state.y1[mode] + state.a2[mode] * state.y2[mode];
                                state.y2[mode] = state.y1[mode];
                                state.y1[mode] = y;
                                x += y;
                            }
                            // As loud as the other kinds.
                            x *= 3.0;
                            break;
                        }
                        }
                        x *= shape.level;
                        const auto sideLeft = x * unison.left * shape.left;
                        const auto sideRight = x * unison.right * shape.right;
                        into[0][0] += sideLeft * (1.0 - shape.send);
                        into[0][1] += sideRight * (1.0 - shape.send);
                        into[1][0] += sideLeft * shape.send;
                        into[1][1] += sideRight * shape.send;
                    }
                }
                if (noise > 0.0)
                    for (std::size_t side = 0; side < 2; ++side) {
                        const auto hiss = noiseSample(noiseKind, side) * noise * 0.7;
                        into[0][side] += hiss * (1.0 - noiseSendValue);
                        into[1][side] += hiss * noiseSendValue;
                    }
                std::array<double, 2> channel {};
                for (std::size_t side = 0; side < 2; ++side) {
                    auto x = 0.0;
                    if (parallel) {
                        x = filters_[0][side].process(types[0], filters[0], into[0][side]);
                        if (secondUsed)
                            x += filters_[1][side].process(types[1], filters[1], into[1][side]);
                    } else {
                        x = filters_[1][side].process(types[1], filters[1],
                                                      filters_[0][side].process(types[0], filters[0], into[0][side] + into[1][side]));
                    }
                    if (drive > 1.0e-4)
                        x = saturate(driveKind, drive, x, side);
                    const auto blocked = x - dcIn_[side] + 0.995 * dcOut_[side];
                    dcIn_[side] = x;
                    dcOut_[side] = blocked;
                    channel[side] = blocked * gain * panGains[side];
                }
                left[done + i] += static_cast<float>(channel[0]);
                right[done + i] += static_cast<float>(channel[1]);
            }
        }
        return active();
    }

private:
    // An engine's settings for this step.
    struct Shape {
        double level {};
        double left { 1.0 };
        double right { 1.0 };
        double send {};
        double morph {};
        double width { 0.5 };
        double syncRatio { 1.0 };
        int table {};
        double position {};
        int mode {};
        double warp {};
        Spectrum spectrum;
        int material {};
        int exciter {};
        std::array<double, modeCount> weights {};
        double ring { 1.0 };
        double tone { 0.5 };
    };

    struct Engines {
        std::array<EngineState, 2> engines {};
        double left { 0.7071 };
        double right { 0.7071 };
    };

    double saturate(const int kind, const double amount, const double x, const std::size_t side) noexcept
    {
        const auto k = 1.0 + 9.0 * amount;
        switch (kind) {
        case 0: return std::tanh(k * x) / std::tanh(k * 0.5) * 0.5;
        case 1: return std::clamp(k * x, -1.0, 1.0) / std::min(1.0, k * 0.5) * 0.5;
        case 2: return std::sin(k * x * kPi * 0.5);
        case 3: {
            const auto hold = 1 + static_cast<int>(amount * 15.0);
            if (side == 0 && ++holdCount_ >= hold)
                holdCount_ = 0;
            if (holdCount_ == 0) {
                const auto steps = std::pow(2.0, 15.0 - 13.0 * amount);
                held_[side] = std::round(x * steps) / steps;
            }
            return held_[side];
        }
        default: return x + amount * (std::abs(x) * 1.5 - x);
        }
    }

    double noiseSample(const int kind, const std::size_t side) noexcept
    {
        const auto white = random();
        switch (kind) {
        case 1: {
            auto& state = pink_[side];
            state[0] = 0.99765 * state[0] + white * 0.0990460;
            state[1] = 0.96300 * state[1] + white * 0.2965164;
            state[2] = 0.57000 * state[2] + white * 1.0526913;
            return (state[0] + state[1] + state[2] + white * 0.1848) * 0.25;
        }
        case 2: return std::abs(white) > 0.996 ? white * 4.0 : 0.0;
        case 3: {
            auto& state = pink_[side];
            state[0] += (white - state[0]) * 0.08;
            return state[0] * 3.0;
        }
        default: return white;
        }
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
    Envelope env3_;
    std::array<std::array<MultiFilter, 2>, 2> filters_ {}; // [filter][side]
    std::array<Shape, 2> shapes_ {};
    std::array<Engines, unisonMost> subs_ {};
    std::array<double, 2> dcIn_ {};
    std::array<double, 2> dcOut_ {};
    std::array<double, 2> held_ {};
    int holdCount_ {};
    std::array<std::array<double, 3>, 2> pink_ {};
    std::array<double, 2> lfoPhases_ {};
    std::array<double, 2> lfoHeld_ {};
    double noteRandom_ {};
    std::uint32_t turing_ {};
    double turingClock_ {};
    bool excite_ {};
    int exciteLeft_ {};
    int exciteLength_ {};
    int burstLeft_ {};
    double previousLevel_ {};
    double glidePerSample_ {};
    std::uint32_t seed_ { nextSeed() };
};

} // namespace

std::unique_ptr<Engine> makeMosaique()
{
    return std::make_unique<MosaiqueEngine>();
}

std::vector<float> mosaiquePicture(const Values& values, const std::string_view part, const int points)
{
    // One engine as it sounds: two periods of the note for a shape, the first
    // tens of milliseconds of the ring for a modal one.
    const auto engine = part == "a" ? 0 : part == "b" ? 1 : -1;
    if (engine < 0 || points < 2)
        return {};
    const auto base = engine == 0 ? engineA : engineB;
    const auto v = [&values](const int at) { return static_cast<double>(values[static_cast<std::size_t>(at)]); };
    const auto kind = std::clamp(static_cast<int>(std::lround(v(base + eType))), 0, kindCount - 1);
    std::vector<float> result(static_cast<std::size_t>(points));
    if (kind == modal) {
        constexpr double rate = 24'000.0;
        constexpr double hz = 220.0;
        const auto& material = kMaterialModes[static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(v(base + eMaterial))), 0, 4))];
        const auto weights = modeWeights(v(base + eTone), v(base + eStrike));
        const auto span = static_cast<int>(0.04 * rate);
        std::array<double, modeCount> y1 {};
        std::array<double, modeCount> y2 {};
        std::vector<double> ring(static_cast<std::size_t>(span));
        for (int at = 0; at < span; ++at) {
            auto x = 0.0;
            for (int k = 0; k < modeCount; ++k) {
                const auto index = static_cast<std::size_t>(k);
                const auto w = 2.0 * kPi * hz * material.ratios[index] / rate;
                const auto r = std::pow(10.0, -3.0 / std::max(1.0, v(base + eDecay) / (1.0 + material.damping * k) * rate));
                const auto y = (at == 0 ? std::sin(w) * weights[index] : 0.0) + 2.0 * r * std::cos(w) * y1[index] - r * r * y2[index];
                y2[index] = y1[index];
                y1[index] = y;
                x += y;
            }
            ring[static_cast<std::size_t>(at)] = 3.0 * x;
        }
        for (int at = 0; at < points; ++at)
            result[static_cast<std::size_t>(at)] = static_cast<float>(ring[static_cast<std::size_t>(at * (span - 1) / (points - 1))]);
        return result;
    }
    const auto spectrum = spectrumOf(static_cast<int>(std::lround(v(base + ePartials))), v(base + eBright), v(base + eParity), v(base + eStretch));
    const auto syncRatio = std::pow(2.0, std::max(0.0, v(base + eSync)) / 12.0);
    for (int at = 0; at < points; ++at) {
        const auto phase = 2.0 * static_cast<double>(at) / static_cast<double>(points - 1);
        const auto wrapped = phase - std::floor(phase);
        auto x = 0.0;
        switch (kind) {
        case analog: {
            const auto slave = wrapped * syncRatio;
            x = analogSample(v(base + eWave), syncRatio > 1.0001 ? slave - std::floor(slave) : wrapped, 0.0, v(base + ePw));
            break;
        }
        case wavetable:
            x = tables::sample(std::clamp(static_cast<int>(std::lround(v(base + eTable))), 0, tables::sets - 1), v(base + ePosition),
                               std::clamp(static_cast<int>(std::lround(v(base + eMode))), 0, tables::modes - 1), v(base + eWarp), 2, wrapped);
            break;
        default:
            for (int partial = 0; partial < spectrum.count; ++partial)
                x += spectrum.amplitude[static_cast<std::size_t>(partial)]
                     * std::sin(2.0 * kPi * phase * spectrum.ratio[static_cast<std::size_t>(partial)]);
            break;
        }
        result[static_cast<std::size_t>(at)] = static_cast<float>(x);
    }
    return result;
}

} // namespace cydaw::engine::synth::detail
