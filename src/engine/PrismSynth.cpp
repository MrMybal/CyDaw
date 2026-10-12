#include <cydaw/engine/PrismSynth.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <map>
#include <mutex>
#include <numbers>

namespace cydaw::engine::prism {

namespace {

constexpr double twoPi = 2.0 * std::numbers::pi;

using Choices = std::vector<std::pair<std::string, std::string>>;

// How a time is spread over its knob: most of the travel for the short ones.
constexpr float timeSkew = 3.0f;

struct Built {
    std::vector<Parameter> parameters;
    std::vector<float> skews; // 0: linear or logarithmic as the parameter says
    std::map<std::string, int> byKey;
    std::vector<int> destinations;
};

Parameter make(std::string key, std::string french, std::string english, std::string unit, const float minimum,
               const float maximum, const float value, const Kind kind, const Group group, const int instance,
               const bool modulatable = false, const bool logarithmic = false)
{
    Parameter parameter;
    parameter.key = std::move(key);
    parameter.french = std::move(french);
    parameter.english = std::move(english);
    parameter.unit = std::move(unit);
    parameter.minimum = minimum;
    parameter.maximum = maximum;
    parameter.defaultValue = value;
    parameter.kind = kind;
    parameter.group = group;
    parameter.instance = instance;
    parameter.modulatable = modulatable;
    parameter.logarithmic = logarithmic;
    return parameter;
}

Parameter choice(std::string key, std::string french, std::string english, Choices options, const int value, const Group group,
                 const int instance)
{
    auto parameter = make(std::move(key), std::move(french), std::move(english), {}, 0.0f,
                          static_cast<float>(std::max<std::size_t>(1, options.size()) - 1), static_cast<float>(value),
                          Kind::choice, group, instance);
    parameter.choices = std::move(options);
    return parameter;
}

const char* const tableNames[] = {
    "Basic Shapes", "Analog PWM", "Harmonic Sweep", "Formant Vowels", "Sync Sweep", "Digital Steps", "Drawbar Organ",
    "FM Bells",     "Growl",      "Spectral Noise", "Square Build",   "Overtones",
};
constexpr int tableCount = static_cast<int>(sizeof(tableNames) / sizeof(tableNames[0]));

struct DivisionInfo {
    const char* name;
    double beats;
};
const DivisionInfo divisionTable[] = {
    { "8/1", 32.0 }, { "4/1", 16.0 },     { "2/1", 8.0 },   { "1/1", 4.0 },     { "1/2", 2.0 },        { "1/2T", 4.0 / 3.0 },
    { "1/4.", 1.5 }, { "1/4", 1.0 },      { "1/4T", 2.0 / 3.0 }, { "1/8.", 0.75 }, { "1/8", 0.5 },     { "1/8T", 1.0 / 3.0 },
    { "1/16.", 0.375 }, { "1/16", 0.25 }, { "1/16T", 1.0 / 6.0 }, { "1/32", 0.125 }, { "1/64", 0.0625 },
};
constexpr int divisionCount = static_cast<int>(sizeof(divisionTable) / sizeof(divisionTable[0]));

std::string groupName(const Group group, const int instance, const bool french)
{
    switch (group) {
    case Group::oscA: return "Osc A";
    case Group::oscB: return "Osc B";
    case Group::oscC: return "Osc C";
    case Group::sub: return "Sub";
    case Group::noise: return french ? "Bruit" : "Noise";
    case Group::filter: return french ? "Filtre" : "Filter";
    case Group::env: return "Env " + std::to_string(instance + 1);
    case Group::lfo: return "LFO " + std::to_string(instance + 1);
    case Group::voicing: return french ? "Voix" : "Voicing";
    case Group::macro: return "Macro " + std::to_string(instance + 1);
    case Group::matrix: return french ? "Modulation " + std::to_string(instance + 1) : "Mod " + std::to_string(instance + 1);
    }
    return {};
}

Built build()
{
    Built built;
    auto& list = built.parameters;
    list.resize(static_cast<std::size_t>(parameterCount));
    built.skews.assign(static_cast<std::size_t>(parameterCount), 0.0f);
    const auto set = [&list](const int index, Parameter parameter) { list[static_cast<std::size_t>(index)] = std::move(parameter); };
    const auto skew = [&built](const int index, const float value) { built.skews[static_cast<std::size_t>(index)] = value; };

    Choices tables;
    for (const auto* name : tableNames)
        tables.emplace_back(name, name);

    for (int osc = 0; osc < oscillatorCount; ++osc) {
        const auto prefix = std::string(osc == 0 ? "a." : osc == 1 ? "b." : "c.");
        const auto group = osc == 0 ? Group::oscA : osc == 1 ? Group::oscB : Group::oscC;
        // What its FM, AM and RM read: A reads B, B and C read A.
        const auto other = std::string(osc == 0 ? "B" : "A");
        const auto at = [osc](const int field) { return oscBase(osc) + field; };
        set(at(oscOn), make(prefix + "on", "Actif", "On", {}, 0, 1, osc == 0 ? 1.0f : 0.0f, Kind::toggle, group, osc));
        set(at(oscTable), choice(prefix + "table", "Table", "Table", tables, osc == 0 ? 0 : osc == 1 ? 2 : 4, group, osc));
        set(at(oscPosition), make(prefix + "position", "Position", "Position", "%", 0, 1, 0, Kind::continuous, group, osc, true));
        set(at(oscWarp), choice(prefix + "warp", "Déformation", "Warp",
                                { { "Aucune", "Off" },
                                  { "Sync", "Sync" },
                                  { "Bend +", "Bend +" },
                                  { "Bend -", "Bend -" },
                                  { "Bend ±", "Bend ±" },
                                  { "PWM", "PWM" },
                                  { "Asym +", "Asym +" },
                                  { "Asym -", "Asym -" },
                                  { "Miroir", "Mirror" },
                                  { "Inverser", "Flip" },
                                  { "Quantifier", "Quantize" },
                                  { "FM (" + other + ")", "FM (" + other + ")" },
                                  { "AM (" + other + ")", "AM (" + other + ")" },
                                  { "RM (" + other + ")", "RM (" + other + ")" } },
                                0, group, osc));
        set(at(oscWarpAmount), make(prefix + "warpAmount", "Quantité", "Amount", "%", 0, 1, 0, Kind::continuous, group, osc, true));
        set(at(oscUnison), make(prefix + "unison", "Unisson", "Unison", {}, 1, 16, 1, Kind::integer, group, osc));
        set(at(oscDetune), make(prefix + "detune", "Désaccord", "Detune", "%", 0, 1, 0.25f, Kind::continuous, group, osc, true));
        set(at(oscBlend), make(prefix + "blend", "Mélange", "Blend", "%", 0, 1, 0.75f, Kind::continuous, group, osc, true));
        set(at(oscWidth), make(prefix + "width", "Largeur", "Width", "%", 0, 1, 1, Kind::continuous, group, osc, true));
        set(at(oscOctave), make(prefix + "octave", "Octave", "Octave", {}, -4, 4, 0, Kind::integer, group, osc));
        set(at(oscSemi), make(prefix + "semi", "Demi-tons", "Semi", "st", -12, 12, 0, Kind::integer, group, osc));
        set(at(oscFine), make(prefix + "fine", "Fin", "Fine", "ct", -100, 100, 0, Kind::continuous, group, osc, true));
        set(at(oscCoarse), make(prefix + "coarse", "Hauteur", "Coarse", "st", -36, 36, 0, Kind::continuous, group, osc, true));
        set(at(oscPhase), make(prefix + "phase", "Phase", "Phase", "%", 0, 1, 0, Kind::continuous, group, osc, true));
        set(at(oscRandom), make(prefix + "random", "Phase aléatoire", "Random phase", "%", 0, 1, 1, Kind::continuous, group, osc));
        set(at(oscLevel), make(prefix + "level", "Niveau", "Level", "%", 0, 1, 0.75f, Kind::continuous, group, osc, true));
        set(at(oscPan), make(prefix + "pan", "Panoramique", "Pan", {}, -1, 1, 0, Kind::continuous, group, osc, true));
        set(at(oscToFilter), make(prefix + "toFilter", "Vers le filtre", "To filter", {}, 0, 1, 1, Kind::toggle, group, osc));
    }

    set(subBase + subOn, make("sub.on", "Actif", "On", {}, 0, 1, 0, Kind::toggle, Group::sub, 0));
    set(subBase + subShape, choice("sub.shape", "Forme", "Shape",
                                   { { "Sinus", "Sine" },
                                     { "Triangle", "Triangle" },
                                     { "Dent de scie", "Saw" },
                                     { "Carré", "Square" },
                                     { "Impulsion 25 %", "Pulse 25%" },
                                     { "Impulsion 10 %", "Pulse 10%" } },
                                   0, Group::sub, 0));
    set(subBase + subOctave, make("sub.octave", "Octave", "Octave", {}, -3, 2, -1, Kind::integer, Group::sub, 0));
    set(subBase + subLevel, make("sub.level", "Niveau", "Level", "%", 0, 1, 0.75f, Kind::continuous, Group::sub, 0, true));
    set(subBase + subPan, make("sub.pan", "Panoramique", "Pan", {}, -1, 1, 0, Kind::continuous, Group::sub, 0, true));
    set(subBase + subDirect, make("sub.direct", "Sortie directe", "Direct out", {}, 0, 1, 0, Kind::toggle, Group::sub, 0));

    set(noiseBase + noiseOn, make("noise.on", "Actif", "On", {}, 0, 1, 0, Kind::toggle, Group::noise, 0));
    set(noiseBase + noiseType, choice("noise.type", "Type", "Type",
                                      { { "Blanc", "White" },
                                        { "Rose", "Pink" },
                                        { "Brun", "Brown" },
                                        { "Numérique", "Digital" },
                                        { "Craquements", "Crackle" },
                                        { "Souffle", "Hiss" } },
                                      0, Group::noise, 0));
    set(noiseBase + noiseTone, make("noise.tone", "Couleur", "Tone", "%", 0, 1, 0.5f, Kind::continuous, Group::noise, 0, true));
    set(noiseBase + noisePitch, make("noise.pitch", "Vitesse", "Pitch", "%", 0, 1, 0.5f, Kind::continuous, Group::noise, 0, true));
    set(noiseBase + noiseLevel, make("noise.level", "Niveau", "Level", "%", 0, 1, 0.4f, Kind::continuous, Group::noise, 0, true));
    set(noiseBase + noisePan, make("noise.pan", "Panoramique", "Pan", {}, -1, 1, 0, Kind::continuous, Group::noise, 0, true));
    set(noiseBase + noiseToFilter, make("noise.toFilter", "Vers le filtre", "To filter", {}, 0, 1, 1, Kind::toggle, Group::noise, 0));

    set(filterBase + filterOn, make("filter.on", "Actif", "On", {}, 0, 1, 0, Kind::toggle, Group::filter, 0));
    set(filterBase + filterType, choice("filter.type", "Type", "Type",
                                        { { "Passe-bas 12", "Low pass 12" },
                                          { "Passe-bas 24", "Low pass 24" },
                                          { "Ladder", "Ladder" },
                                          { "Passe-haut 12", "High pass 12" },
                                          { "Passe-haut 24", "High pass 24" },
                                          { "Passe-bande 12", "Band pass 12" },
                                          { "Passe-bande 24", "Band pass 24" },
                                          { "Coupe-bande", "Notch" },
                                          { "Cloche", "Peak" },
                                          { "Peigne +", "Comb +" },
                                          { "Peigne -", "Comb -" },
                                          { "Formants", "Formant" } },
                                        1, Group::filter, 0));
    set(filterBase + filterCutoff,
        make("filter.cutoff", "Fréquence", "Cutoff", "Hz", 20, 20'000, 1'200, Kind::continuous, Group::filter, 0, true, true));
    set(filterBase + filterResonance,
        make("filter.resonance", "Résonance", "Resonance", "%", 0, 1, 0.15f, Kind::continuous, Group::filter, 0, true));
    set(filterBase + filterDrive, make("filter.drive", "Saturation", "Drive", "%", 0, 1, 0, Kind::continuous, Group::filter, 0, true));
    set(filterBase + filterVariation,
        make("filter.variation", "Variation", "Variation", "%", 0, 1, 0.5f, Kind::continuous, Group::filter, 0, true));
    set(filterBase + filterMix, make("filter.mix", "Mélange", "Mix", "%", 0, 1, 1, Kind::continuous, Group::filter, 0, true));
    set(filterBase + filterKeytrack,
        make("filter.keytrack", "Suivi du clavier", "Key track", "%", 0, 1, 0, Kind::continuous, Group::filter, 0, true));

    for (int env = 0; env < envelopeCount; ++env) {
        const auto prefix = "env" + std::to_string(env + 1) + ".";
        const auto at = [env](const int field) { return envBase(env) + field; };
        const auto first = env == 0;
        set(at(envAttack), make(prefix + "attack", "Attaque", "Attack", "s", 0, 20, first ? 0.001f : 0.01f, Kind::continuous, Group::env, env));
        set(at(envHold), make(prefix + "hold", "Maintien", "Hold", "s", 0, 10, 0, Kind::continuous, Group::env, env));
        set(at(envDecay), make(prefix + "decay", "Déclin", "Decay", "s", 0, 20, first ? 1.0f : 0.6f, Kind::continuous, Group::env, env));
        set(at(envSustain), make(prefix + "sustain", "Soutien", "Sustain", "%", 0, 1, first ? 1.0f : 0.0f, Kind::continuous, Group::env, env));
        set(at(envRelease),
            make(prefix + "release", "Relâchement", "Release", "s", 0, 20, first ? 0.03f : 0.3f, Kind::continuous, Group::env, env));
        set(at(envAttackCurve), make(prefix + "attackCurve", "Courbe d'attaque", "Attack curve", {}, -1, 1, 0, Kind::continuous, Group::env, env));
        set(at(envDecayCurve), make(prefix + "decayCurve", "Courbe de déclin", "Decay curve", {}, -1, 1, 0.6f, Kind::continuous, Group::env, env));
        set(at(envReleaseCurve),
            make(prefix + "releaseCurve", "Courbe de relâchement", "Release curve", {}, -1, 1, 0.6f, Kind::continuous, Group::env, env));
        for (const auto field : { envAttack, envHold, envDecay, envRelease })
            skew(at(field), timeSkew);
    }

    Choices divisions;
    for (const auto& division : divisionTable)
        divisions.emplace_back(division.name, division.name);
    for (int lfo = 0; lfo < lfoCount; ++lfo) {
        const auto prefix = "lfo" + std::to_string(lfo + 1) + ".";
        const auto at = [lfo](const int field) { return lfoBase(lfo) + field; };
        set(at(lfoShape), choice(prefix + "shape", "Forme", "Shape",
                                 { { "Sinus", "Sine" },
                                   { "Triangle", "Triangle" },
                                   { "Rampe montante", "Saw up" },
                                   { "Rampe descendante", "Saw down" },
                                   { "Carré", "Square" },
                                   { "Échantillonné", "Sample & hold" },
                                   { "Aléatoire lisse", "Smooth random" },
                                   { "Exponentielle", "Exponential" },
                                   { "Dessinée", "Drawn" } },
                                 1, Group::lfo, lfo));
        set(at(lfoMode), choice(prefix + "mode", "Mode", "Mode",
                                { { "Redéclenché", "Trigger" }, { "Enveloppe", "Envelope" }, { "Libre", "Free" } }, 0, Group::lfo, lfo));
        set(at(lfoSync), make(prefix + "sync", "Tempo", "Sync", {}, 0, 1, 1, Kind::toggle, Group::lfo, lfo));
        set(at(lfoRate), make(prefix + "rate", "Vitesse", "Rate", "Hz", 0.01f, 50, 2, Kind::continuous, Group::lfo, lfo, true, true));
        set(at(lfoDivision), choice(prefix + "division", "Division", "Division", divisions, 7, Group::lfo, lfo));
        set(at(lfoRise), make(prefix + "rise", "Montée", "Rise", "s", 0, 10, 0, Kind::continuous, Group::lfo, lfo));
        set(at(lfoDelay), make(prefix + "delay", "Retard", "Delay", "s", 0, 10, 0, Kind::continuous, Group::lfo, lfo));
        set(at(lfoPhase), make(prefix + "phase", "Phase", "Phase", "%", 0, 1, 0, Kind::continuous, Group::lfo, lfo));
        set(at(lfoSmooth), make(prefix + "smooth", "Lissage", "Smooth", "%", 0, 1, 0, Kind::continuous, Group::lfo, lfo));
        set(at(lfoUnipolar), make(prefix + "unipolar", "Unipolaire", "Unipolar", {}, 0, 1, 1, Kind::toggle, Group::lfo, lfo));
        skew(at(lfoRise), timeSkew);
        skew(at(lfoDelay), timeSkew);
        // Its drawn shape: a sine to begin with.
        for (int point = 0; point < drawPoints; ++point) {
            const auto number = std::to_string(point + 1);
            set(drawBase(lfo) + point,
                make(prefix + "draw" + number, "Point " + number, "Point " + number, {}, -1, 1,
                     static_cast<float>(std::sin(twoPi * point / drawPoints)), Kind::continuous, Group::lfo, lfo));
        }
    }

    set(voiceBase + voiceMode, choice("voice.mode", "Jeu", "Voicing",
                                      { { "Polyphonique", "Poly" }, { "Mono", "Mono" }, { "Legato", "Legato" } }, 0, Group::voicing, 0));
    set(voiceBase + voiceCount, make("voice.voices", "Voix", "Voices", {}, 1, 32, 16, Kind::integer, Group::voicing, 0));
    set(voiceBase + voiceGlide, make("voice.glide", "Glissando", "Glide", "s", 0, 5, 0, Kind::continuous, Group::voicing, 0));
    set(voiceBase + voiceGlideAlways,
        make("voice.glideAlways", "Glisser toujours", "Always glide", {}, 0, 1, 0, Kind::toggle, Group::voicing, 0));
    set(voiceBase + voiceBendRange, make("voice.bendRange", "Pitch bend", "Bend range", "st", 0, 24, 2, Kind::integer, Group::voicing, 0));
    set(voiceBase + voiceVelocity,
        make("voice.velocity", "Vélocité → niveau", "Velocity → level", "%", 0, 1, 0.7f, Kind::continuous, Group::voicing, 0));
    set(voiceBase + voiceMaster, make("voice.master", "Volume", "Master", "%", 0, 1, 0.75f, Kind::continuous, Group::voicing, 0, true));
    skew(voiceBase + voiceGlide, timeSkew);

    // Each oscillator's spectral morph; each LFO's stereo offset.
    for (int osc = 0; osc < oscillatorCount; ++osc) {
        const auto prefix = std::string(osc == 0 ? "a." : osc == 1 ? "b." : "c.");
        const auto group = osc == 0 ? Group::oscA : osc == 1 ? Group::oscB : Group::oscC;
        set(spectralBase(osc) + spectralMode, choice(prefix + "spectral", "Spectre", "Spectral",
                                                     { { "Aucun", "Off" },
                                                       { "Passe-bas", "Low pass" },
                                                       { "Passe-haut", "High pass" },
                                                       { "Impairs", "Odd" },
                                                       { "Pairs", "Even" },
                                                       { "Peigne", "Comb" },
                                                       { "Octave", "Octave" },
                                                       { "Formant", "Formant" } },
                                                     0, group, osc));
        set(spectralBase(osc) + spectralAmount,
            make(prefix + "spectralAmount", "Spectre · quantité", "Spectral amount", "%", 0, 1, 0.5f, Kind::continuous, group, osc, true));
    }
    for (int lfo = 0; lfo < lfoCount; ++lfo)
        set(lfoStereo(lfo), make("lfo" + std::to_string(lfo + 1) + ".stereo", "Stéréo", "Stereo", "%", 0, 1, 0, Kind::continuous,
                                 Group::lfo, lfo));

    for (int macro = 0; macro < macroCount; ++macro)
        set(macroBase + macro, make("macro" + std::to_string(macro + 1), "Macro " + std::to_string(macro + 1),
                                    "Macro " + std::to_string(macro + 1), "%", 0, 1, 0, Kind::continuous, Group::macro, macro));

    // The parameters a slot can move.
    built.destinations.push_back(-1);
    for (int index = 0; index < macroBase; ++index)
        if (list[static_cast<std::size_t>(index)].modulatable)
            built.destinations.push_back(index);
    for (int lfo = 0; lfo < lfoCount; ++lfo)
        built.destinations.push_back(lfoBase(lfo) + lfoRate);
    // Those added later come after (a slot written before them keeps its target).
    for (int field = 0; field < oscFieldCount; ++field)
        if (list[static_cast<std::size_t>(oscCBase + field)].modulatable)
            built.destinations.push_back(oscCBase + field);
    for (int osc = 0; osc < oscillatorCount; ++osc)
        built.destinations.push_back(spectralBase(osc) + spectralAmount);
    Choices destinationNames { { "Aucune", "None" } };
    for (std::size_t at = 1; at < built.destinations.size(); ++at) {
        const auto& target = list[static_cast<std::size_t>(built.destinations[at])];
        destinationNames.emplace_back(groupName(target.group, target.instance, true) + " · " + target.french,
                                      groupName(target.group, target.instance, false) + " · " + target.english);
    }
    const Choices sources {
        { "Aucune", "None" },      { "Env 1", "Env 1" },       { "Env 2", "Env 2" },   { "Env 3", "Env 3" },
        { "Env 4", "Env 4" },      { "LFO 1", "LFO 1" },       { "LFO 2", "LFO 2" },   { "LFO 3", "LFO 3" },
        { "LFO 4", "LFO 4" },      { "Vélocité", "Velocity" }, { "Note", "Note" },     { "Molette", "Mod wheel" },
        { "Aftertouch", "Aftertouch" }, { "Pitch bend", "Pitch bend" }, { "Macro 1", "Macro 1" }, { "Macro 2", "Macro 2" },
        { "Macro 3", "Macro 3" },  { "Macro 4", "Macro 4" },   { "Aléatoire", "Random" },
    };
    for (int slot = 0; slot < slotCount; ++slot) {
        const auto prefix = "m" + std::to_string(slot + 1) + ".";
        set(slotBase(slot) + slotSource, choice(prefix + "source", "Source", "Source", sources, 0, Group::matrix, slot));
        set(slotBase(slot) + slotDestination,
            choice(prefix + "destination", "Destination", "Destination", destinationNames, 0, Group::matrix, slot));
        set(slotBase(slot) + slotAmount, make(prefix + "amount", "Quantité", "Amount", "%", -1, 1, 0, Kind::continuous, Group::matrix, slot));
    }
    for (int index = 0; index < parameterCount; ++index)
        built.byKey[list[static_cast<std::size_t>(index)].key] = index;
    return built;
}

const Built& built()
{
    static const Built instance = build();
    return instance;
}

// ---- FFT (tables only, off the audio thread) ------------------------------------------

void fft(std::vector<std::complex<double>>& data, const bool inverse)
{
    const auto size = data.size();
    for (std::size_t i = 1, j = 0; i < size; ++i) {
        auto bit = size >> 1;
        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(data[i], data[j]);
    }
    for (std::size_t length = 2; length <= size; length <<= 1) {
        const auto angle = (inverse ? twoPi : -twoPi) / static_cast<double>(length);
        const std::complex<double> step(std::cos(angle), std::sin(angle));
        for (std::size_t start = 0; start < size; start += length) {
            std::complex<double> w(1.0, 0.0);
            for (std::size_t k = 0; k < length / 2; ++k) {
                const auto even = data[start + k];
                const auto odd = data[start + k + length / 2] * w;
                data[start + k] = even + odd;
                data[start + k + length / 2] = even - odd;
                w *= step;
            }
        }
    }
    if (inverse)
        for (auto& value : data)
            value /= static_cast<double>(size);
}

constexpr int frameLength = Wavetable::frameLength;

// A cycle from harmonic amplitudes and phases (index 1 is the fundamental).
std::vector<float> fromHarmonics(const std::vector<double>& amplitudes, const std::vector<double>& phases = {})
{
    std::vector<std::complex<double>> spectrum(static_cast<std::size_t>(frameLength));
    const auto top = std::min<std::size_t>(amplitudes.size(), frameLength / 2);
    for (std::size_t h = 1; h < top; ++h) {
        const auto phase = h < phases.size() ? phases[h] : 0.0;
        // A sine at phase 0: -i/2 at +h, +i/2 at -h, scaled for the inverse.
        const auto value = std::polar(amplitudes[h] * 0.5 * frameLength, phase - std::numbers::pi * 0.5);
        spectrum[h] = value;
        spectrum[frameLength - h] = std::conj(value);
    }
    fft(spectrum, true);
    std::vector<float> cycle(static_cast<std::size_t>(frameLength));
    for (int i = 0; i < frameLength; ++i)
        cycle[static_cast<std::size_t>(i)] = static_cast<float>(spectrum[static_cast<std::size_t>(i)].real());
    return cycle;
}

std::uint32_t nextRandom(std::uint32_t& state) noexcept
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

double random01(std::uint32_t& state) noexcept
{
    return static_cast<double>(nextRandom(state) >> 8) / 16'777'216.0;
}

std::vector<std::vector<float>> factoryFrames(const int index)
{
    std::vector<std::vector<float>> frames;
    const auto harmonics = [](const int count, auto&& amplitude) {
        std::vector<double> values(static_cast<std::size_t>(count + 1));
        for (int h = 1; h <= count; ++h)
            values[static_cast<std::size_t>(h)] = amplitude(h);
        return values;
    };
    switch (index) {
    case 0: // sine, triangle, saw, square
        frames.push_back(fromHarmonics(harmonics(1, [](int) { return 1.0; })));
        frames.push_back(fromHarmonics(harmonics(511, [](const int h) {
            return h % 2 == 1 ? ((h / 2) % 2 == 0 ? 1.0 : -1.0) / (h * h) : 0.0;
        })));
        frames.push_back(fromHarmonics(harmonics(1'000, [](const int h) { return 1.0 / h; })));
        frames.push_back(fromHarmonics(harmonics(1'000, [](const int h) { return h % 2 == 1 ? 1.0 / h : 0.0; })));
        break;
    case 1: // pulses, 50 % down to 3 %
        for (int frame = 0; frame < 16; ++frame) {
            const auto width = 0.5 - 0.47 * frame / 15.0;
            std::vector<float> cycle(static_cast<std::size_t>(frameLength));
            for (int i = 0; i < frameLength; ++i)
                cycle[static_cast<std::size_t>(i)] = static_cast<double>(i) / frameLength < width ? 1.0f : -1.0f;
            frames.push_back(std::move(cycle));
        }
        break;
    case 2: // a saw built harmonic by harmonic
        for (int frame = 0; frame < 32; ++frame) {
            const auto count = std::min(600, static_cast<int>(std::round(std::pow(1.24, frame))));
            frames.push_back(fromHarmonics(harmonics(count, [](const int h) { return 1.0 / h; })));
        }
        break;
    case 3: { // A, E, I, O, U and back to A
        const double vowels[6][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 270, 2290, 3010 },
                                      { 450, 800, 2830 },  { 325, 700, 2530 },  { 800, 1150, 2900 } };
        for (int frame = 0; frame < 32; ++frame) {
            const auto at = frame / 31.0 * 5.0;
            const auto from = std::min(4, static_cast<int>(at));
            const auto t = at - from;
            double formants[3];
            for (int f = 0; f < 3; ++f)
                formants[f] = vowels[from][f] + (vowels[from + 1][f] - vowels[from][f]) * t;
            constexpr double base = 110.0;
            frames.push_back(fromHarmonics(harmonics(180, [&formants](const int h) {
                const auto frequency = h * base;
                auto value = 0.0;
                const double gains[3] = { 1.0, 0.55, 0.3 };
                for (int f = 0; f < 3; ++f) {
                    const auto distance = (frequency - formants[f]) / (formants[f] * 0.12);
                    value += gains[f] * std::exp(-distance * distance);
                }
                return (0.04 + value) / std::sqrt(static_cast<double>(h));
            })));
        }
        break;
    }
    case 4: // hard sync, ratio 1 to 8
        for (int frame = 0; frame < 32; ++frame) {
            const auto ratio = 1.0 + 7.0 * frame / 31.0;
            std::vector<float> cycle(static_cast<std::size_t>(frameLength));
            for (int i = 0; i < frameLength; ++i) {
                const auto phase = static_cast<double>(i) / frameLength * ratio;
                cycle[static_cast<std::size_t>(i)] = static_cast<float>(2.0 * (phase - std::floor(phase)) - 1.0);
            }
            frames.push_back(std::move(cycle));
        }
        break;
    case 5: // a sine in fewer and fewer steps, then a saw's
        for (int frame = 0; frame < 16; ++frame) {
            const auto steps = std::max(2.0, std::pow(2.0, 6.0 - frame * 0.35));
            std::vector<float> cycle(static_cast<std::size_t>(frameLength));
            for (int i = 0; i < frameLength; ++i) {
                const auto phase = static_cast<double>(i) / frameLength;
                const auto value = frame < 8 ? std::sin(twoPi * phase) : 2.0 * phase - 1.0;
                cycle[static_cast<std::size_t>(i)] = static_cast<float>(std::round(value * steps) / steps);
            }
            frames.push_back(std::move(cycle));
        }
        break;
    case 6: { // drawbars: 8', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1'
        const int registrations[16][7] = {
            { 8, 0, 0, 0, 0, 0, 0 }, { 8, 8, 0, 0, 0, 0, 0 }, { 8, 8, 8, 0, 0, 0, 0 }, { 8, 6, 8, 4, 0, 0, 0 },
            { 8, 8, 8, 8, 0, 0, 0 }, { 8, 3, 8, 0, 4, 0, 0 }, { 8, 8, 6, 8, 3, 6, 0 }, { 8, 8, 8, 8, 8, 8, 8 },
            { 6, 8, 8, 6, 0, 0, 0 }, { 8, 0, 8, 0, 8, 0, 8 }, { 4, 8, 0, 8, 0, 8, 4 }, { 8, 8, 0, 0, 0, 0, 8 },
            { 8, 4, 2, 6, 2, 4, 8 }, { 8, 8, 8, 0, 0, 8, 8 }, { 5, 6, 8, 8, 6, 5, 4 }, { 8, 2, 8, 2, 8, 2, 8 },
        };
        const int partials[7] = { 1, 2, 3, 4, 5, 6, 8 };
        for (const auto& registration : registrations) {
            std::vector<double> amplitudes(9);
            for (int bar = 0; bar < 7; ++bar)
                amplitudes[static_cast<std::size_t>(partials[bar])] += registration[bar] / 8.0;
            frames.push_back(fromHarmonics(amplitudes));
        }
        break;
    }
    case 7: // phase modulation, ratio 3, index 0 to 7
        for (int frame = 0; frame < 32; ++frame) {
            const auto depth = 7.0 * frame / 31.0;
            std::vector<float> cycle(static_cast<std::size_t>(frameLength));
            for (int i = 0; i < frameLength; ++i) {
                const auto phase = static_cast<double>(i) / frameLength;
                cycle[static_cast<std::size_t>(i)] = static_cast<float>(std::sin(twoPi * phase + depth * std::sin(twoPi * 3.0 * phase)));
            }
            frames.push_back(std::move(cycle));
        }
        break;
    case 8: { // a resonance sweeping over odd harmonics
        std::uint32_t seed = 0x9e3779b9u;
        std::vector<double> phases(260);
        for (auto& phase : phases)
            phase = random01(seed) * twoPi;
        for (int frame = 0; frame < 32; ++frame) {
            const auto centre = 2.0 + 38.0 * frame / 31.0;
            frames.push_back(fromHarmonics(harmonics(256, [centre](const int h) {
                const auto distance = (h - centre) / (2.0 + centre * 0.15);
                const auto peak = 1.0 + 5.0 * std::exp(-distance * distance);
                return (h % 2 == 1 ? 1.0 : 0.45) * peak / h;
            }), phases));
        }
        break;
    }
    case 9: { // harmonics at random, a new draw every frame
        for (int frame = 0; frame < 16; ++frame) {
            std::uint32_t seed = 0x2545f491u + static_cast<std::uint32_t>(frame) * 7'919u;
            std::vector<double> amplitudes(300);
            std::vector<double> phases(300);
            for (std::size_t h = 1; h < amplitudes.size(); ++h) {
                amplitudes[h] = random01(seed) / std::sqrt(static_cast<double>(h));
                phases[h] = random01(seed) * twoPi;
            }
            frames.push_back(fromHarmonics(amplitudes, phases));
        }
        break;
    }
    case 10: // a square built odd harmonic by odd harmonic
        for (int frame = 0; frame < 16; ++frame) {
            const auto count = 2 * static_cast<int>(std::round(std::pow(1.42, frame))) - 1;
            frames.push_back(fromHarmonics(harmonics(std::min(count, 799), [](const int h) { return h % 2 == 1 ? 1.0 / h : 0.0; })));
        }
        break;
    case 11: // each harmonic alone, 1 to 32
        for (int frame = 0; frame < 32; ++frame) {
            std::vector<double> amplitudes(static_cast<std::size_t>(frame + 2));
            amplitudes[static_cast<std::size_t>(frame + 1)] = 1.0;
            frames.push_back(fromHarmonics(amplitudes));
        }
        break;
    default:
        frames.push_back(fromHarmonics({ 0.0, 1.0 }));
        break;
    }
    return frames;
}

// ---- Playing helpers ------------------------------------------------------------------

inline double bend(const double t, const double curve) noexcept
{
    const auto x = std::clamp(t, 0.0, 1.0);
    if (curve > 0.0)
        return 1.0 - std::pow(1.0 - x, 1.0 + curve * 4.0);
    if (curve < 0.0)
        return std::pow(x, 1.0 - curve * 4.0);
    return x;
}

inline void panGains(const double pan, double& left, double& right) noexcept
{
    const auto angle = (std::clamp(pan, -1.0, 1.0) + 1.0) * std::numbers::pi * 0.25;
    left = std::cos(angle) * std::numbers::sqrt2;
    right = std::sin(angle) * std::numbers::sqrt2;
}

inline double polyBlep(const double t, const double dt) noexcept
{
    if (t < dt) {
        const auto x = t / dt;
        return x + x - x * x - 1.0;
    }
    if (t > 1.0 - dt) {
        const auto x = (t - 1.0) / dt;
        return x * x + x + x + 1.0;
    }
    return 0.0;
}

struct Envelope {
    enum Stage { idle, attack, hold, decay, sustain, release };
    Stage stage { idle };
    double value {};
    double t {};
    double from {};

    void start() noexcept
    {
        stage = attack;
        t = 0.0;
        from = value;
    }
    void releaseNow() noexcept
    {
        if (stage == idle || stage == release)
            return;
        stage = release;
        t = 0.0;
        from = value;
    }
    double next(const float* p, const double dt) noexcept
    {
        const auto time = [](const float seconds) { return std::max(0.0, static_cast<double>(seconds)); };
        switch (stage) {
        case idle:
            value = 0.0;
            break;
        case attack: {
            const auto length = time(p[envAttack]);
            t = length < 1.0e-5 ? 1.0 : t + dt / length;
            value = from + (1.0 - from) * bend(t, p[envAttackCurve]);
            if (t >= 1.0) {
                value = 1.0;
                stage = hold;
                t = 0.0;
            }
            break;
        }
        case hold: {
            const auto length = time(p[envHold]);
            t = length < 1.0e-5 ? 1.0 : t + dt / length;
            value = 1.0;
            if (t >= 1.0) {
                stage = decay;
                t = 0.0;
            }
            break;
        }
        case decay: {
            const auto level = std::clamp(static_cast<double>(p[envSustain]), 0.0, 1.0);
            const auto length = time(p[envDecay]);
            t = length < 1.0e-5 ? 1.0 : t + dt / length;
            value = level + (1.0 - level) * (1.0 - bend(t, p[envDecayCurve]));
            if (t >= 1.0) {
                value = level;
                stage = sustain;
            }
            break;
        }
        case sustain:
            value = std::clamp(static_cast<double>(p[envSustain]), 0.0, 1.0);
            break;
        case release: {
            const auto length = time(p[envRelease]);
            t = length < 1.0e-5 ? 1.0 : t + dt / length;
            value = from * (1.0 - bend(t, p[envReleaseCurve]));
            if (t >= 1.0) {
                value = 0.0;
                stage = idle;
            }
            break;
        }
        }
        return value;
    }
};

struct LfoState {
    double phase {};
    double value {};
    double smoothed {};
    double previous {};
    double next {};
    double elapsed {};
    bool finished {};
};

double lfoWave(const LfoShape shape, const double phase, const LfoState& state, const float* drawn) noexcept
{
    switch (shape) {
    case LfoShape::drawn: return drawnLfo(drawn, phase);
    case LfoShape::sine: return std::sin(twoPi * phase);
    case LfoShape::triangle: return 1.0 - 4.0 * std::abs(phase - 0.5);
    case LfoShape::sawUp: return 2.0 * phase - 1.0;
    case LfoShape::sawDown: return 1.0 - 2.0 * phase;
    case LfoShape::square: return phase < 0.5 ? 1.0 : -1.0;
    case LfoShape::sampleHold: return state.previous;
    case LfoShape::smoothRandom: {
        const auto t = 0.5 - 0.5 * std::cos(std::numbers::pi * phase);
        return state.previous + (state.next - state.previous) * t;
    }
    case LfoShape::exponential: return 2.0 * std::exp(-5.0 * phase) - 1.0;
    case LfoShape::count: break;
    }
    return 0.0;
}

struct Svf {
    double ic1 {};
    double ic2 {};
    double a1 { 1.0 };
    double a2 {};
    double a3 {};
    double k { 1.4 };

    void set(const double cutoff, const double q, const double rate) noexcept
    {
        const auto g = std::tan(std::numbers::pi * std::clamp(cutoff, 10.0, rate * 0.45) / rate);
        k = 1.0 / std::clamp(q, 0.3, 40.0);
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    // Low, band, high.
    void process(const double input, double& low, double& band, double& high) noexcept
    {
        const auto v3 = input - ic2;
        const auto v1 = a1 * ic1 + a2 * v3;
        const auto v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        low = v2;
        band = v1;
        high = input - k * v1 - v2;
    }
    void reset() noexcept
    {
        ic1 = 0.0;
        ic2 = 0.0;
    }
};

struct FilterChannel {
    Svf first;
    Svf second;
    Svf formants[3];
    double ladder[4] {};
    double ladderOut {};
    std::vector<float> comb;
    int combWrite {};

    void reset() noexcept
    {
        first.reset();
        second.reset();
        for (auto& formant : formants)
            formant.reset();
        std::fill(std::begin(ladder), std::end(ladder), 0.0);
        ladderOut = 0.0;
        std::fill(comb.begin(), comb.end(), 0.0f);
        combWrite = 0;
    }
};

// What a block's modulation made of the settings a voice plays.
struct Block {
    double cutoff {};
    double q {};
    double drive {};
    double variation {};
    double mix {};
    double ladderG {};
    double ladderK {};
    double combDelay {};
    double combFeedback {};
    double formantGains[3] {};
};

} // namespace

// ---- Settings --------------------------------------------------------------------------------

const Parameter& parameter(const int index)
{
    return built().parameters[static_cast<std::size_t>(std::clamp(index, 0, parameterCount - 1))];
}

int parameterIndex(const std::string& key)
{
    const auto& byKey = built().byKey;
    const auto found = byKey.find(key);
    return found != byKey.end() ? found->second : -1;
}

PrismValues defaultValues()
{
    PrismValues values {};
    for (int index = 0; index < parameterCount; ++index)
        values[static_cast<std::size_t>(index)] = parameter(index).defaultValue;
    return values;
}

float clampValue(const int index, const float value)
{
    const auto& p = parameter(index);
    auto clamped = std::clamp(std::isfinite(value) ? value : p.defaultValue, p.minimum, p.maximum);
    if (p.kind != Kind::continuous)
        clamped = std::round(clamped);
    return clamped;
}

float normalise(const int index, const float value)
{
    const auto& p = parameter(index);
    const auto v = std::clamp(value, p.minimum, p.maximum);
    if (p.logarithmic && p.minimum > 0.0f)
        return std::log(v / p.minimum) / std::log(p.maximum / p.minimum);
    const auto linear = p.maximum > p.minimum ? (v - p.minimum) / (p.maximum - p.minimum) : 0.0f;
    const auto skew = built().skews[static_cast<std::size_t>(std::clamp(index, 0, parameterCount - 1))];
    return skew > 0.0f ? std::pow(linear, 1.0f / skew) : linear;
}

float denormalise(const int index, const float normalised)
{
    const auto& p = parameter(index);
    const auto n = std::clamp(normalised, 0.0f, 1.0f);
    if (p.logarithmic && p.minimum > 0.0f)
        return p.minimum * std::pow(p.maximum / p.minimum, n);
    const auto skew = built().skews[static_cast<std::size_t>(std::clamp(index, 0, parameterCount - 1))];
    const auto linear = skew > 0.0f ? std::pow(n, skew) : n;
    return p.minimum + linear * (p.maximum - p.minimum);
}

std::string displayValue(const int index, const float value, const bool french)
{
    const auto& p = parameter(index);
    char text[64];
    switch (p.kind) {
    case Kind::toggle:
        return value >= 0.5f ? (french ? "Oui" : "On") : (french ? "Non" : "Off");
    case Kind::choice: {
        const auto at = static_cast<std::size_t>(std::clamp(static_cast<int>(std::round(value)), 0,
                                                            static_cast<int>(p.choices.size()) - 1));
        return p.choices.empty() ? std::string() : (french ? p.choices[at].first : p.choices[at].second);
    }
    case Kind::integer:
        std::snprintf(text, sizeof(text), "%d%s%s", static_cast<int>(std::round(value)), p.unit.empty() ? "" : " ", p.unit.c_str());
        return text;
    case Kind::continuous:
        break;
    }
    if (p.unit == "Hz") {
        if (value >= 1'000.0f)
            std::snprintf(text, sizeof(text), "%.2f kHz", static_cast<double>(value) / 1'000.0);
        else
            std::snprintf(text, sizeof(text), value < 10.0f ? "%.2f Hz" : "%.0f Hz", static_cast<double>(value));
    } else if (p.unit == "s") {
        if (value < 1.0f)
            std::snprintf(text, sizeof(text), "%.1f ms", static_cast<double>(value) * 1'000.0);
        else
            std::snprintf(text, sizeof(text), "%.2f s", static_cast<double>(value));
    } else if (p.unit == "%") {
        std::snprintf(text, sizeof(text), "%.0f %%", static_cast<double>(value) * 100.0);
    } else if (p.key.ends_with(".pan")) {
        if (std::abs(value) < 0.005f)
            return french ? "C" : "C";
        std::snprintf(text, sizeof(text), "%s %.0f", value < 0.0f ? (french ? "G" : "L") : (french ? "D" : "R"),
                      std::abs(static_cast<double>(value)) * 100.0);
    } else if (p.unit.empty()) {
        std::snprintf(text, sizeof(text), "%.2f", static_cast<double>(value));
    } else {
        std::snprintf(text, sizeof(text), "%.1f %s", static_cast<double>(value), p.unit.c_str());
    }
    return text;
}

const std::vector<int>& destinations()
{
    return built().destinations;
}

double divisionBeats(const int division)
{
    return divisionTable[std::clamp(division, 0, divisionCount - 1)].beats;
}

// ---- Wavetables ------------------------------------------------------------------------------

WavetableRef buildWavetable(const std::string& name, const std::vector<std::vector<float>>& frames)
{
    auto table = std::make_shared<Wavetable>();
    table->name = name;
    table->frameCount = std::max(1, static_cast<int>(frames.size()));
    table->data.assign(static_cast<std::size_t>(table->frameCount) * Wavetable::levels * frameLength, 0.0f);
    std::vector<std::complex<double>> spectrum(static_cast<std::size_t>(frameLength));
    std::vector<std::complex<double>> work(static_cast<std::size_t>(frameLength));
    for (int frame = 0; frame < static_cast<int>(frames.size()); ++frame) {
        const auto& source = frames[static_cast<std::size_t>(frame)];
        if (source.empty())
            continue;
        // Resampled to 2048, its DC taken out.
        auto mean = 0.0;
        for (int i = 0; i < frameLength; ++i) {
            const auto at = static_cast<double>(i) * static_cast<double>(source.size()) / frameLength;
            const auto i0 = static_cast<std::size_t>(at) % source.size();
            const auto i1 = (i0 + 1) % source.size();
            const auto t = at - std::floor(at);
            const auto value = source[i0] + (source[i1] - source[i0]) * t;
            spectrum[static_cast<std::size_t>(i)] = value;
            mean += value;
        }
        mean /= frameLength;
        for (auto& value : spectrum)
            value -= mean;
        fft(spectrum, false);
        spectrum[0] = 0.0;
        // Normalised on the full-band frame's peak.
        auto peak = 0.0;
        for (int level = 0; level < Wavetable::levels; ++level) {
            const auto keep = std::max(1, (frameLength / 2 - 1) >> level);
            work = spectrum;
            for (int bin = keep + 1; bin <= frameLength - keep - 1; ++bin)
                work[static_cast<std::size_t>(bin)] = 0.0;
            fft(work, true);
            auto* out = table->data.data()
                        + (static_cast<std::size_t>(frame) * Wavetable::levels + static_cast<std::size_t>(level)) * frameLength;
            for (int i = 0; i < frameLength; ++i) {
                out[i] = static_cast<float>(work[static_cast<std::size_t>(i)].real());
                if (level == 0)
                    peak = std::max(peak, std::abs(work[static_cast<std::size_t>(i)].real()));
            }
        }
        if (peak > 1.0e-9) {
            auto* begin = table->data.data() + static_cast<std::size_t>(frame) * Wavetable::levels * frameLength;
            const auto scale = static_cast<float>(1.0 / peak);
            for (int i = 0; i < Wavetable::levels * frameLength; ++i)
                begin[i] *= scale;
        }
    }
    return table;
}

int factoryTableCount()
{
    return tableCount;
}

std::string factoryTableName(const int index)
{
    return tableNames[std::clamp(index, 0, tableCount - 1)];
}

WavetableRef factoryTable(const int index)
{
    static std::mutex lock;
    static std::array<WavetableRef, tableCount> cache;
    const auto at = std::clamp(index, 0, tableCount - 1);
    const std::lock_guard guard(lock);
    auto& table = cache[static_cast<std::size_t>(at)];
    if (table == nullptr)
        table = buildWavetable(tableNames[at], factoryFrames(at));
    return table;
}

TablesRef resolveTables(const PrismValues& values, const TablesRef& own)
{
    auto tables = std::make_shared<Tables>();
    for (int osc = 0; osc < oscillatorCount; ++osc) {
        if (own != nullptr && own->oscillator[static_cast<std::size_t>(osc)] != nullptr)
            tables->oscillator[static_cast<std::size_t>(osc)] = own->oscillator[static_cast<std::size_t>(osc)];
        else
            tables->oscillator[static_cast<std::size_t>(osc)]
                = factoryTable(static_cast<int>(std::round(values[static_cast<std::size_t>(oscBase(osc) + oscTable)])));
    }
    return tables;
}

// ---- The voice ---------------------------------------------------------------------------------

struct Voice::State {
    static constexpr int maximumUnison = 16;
    static constexpr int blockLength = 32;

    double rate { 48'000.0 };
    int note { -1 };
    double velocity {};
    double pitch {};       // semitones, gliding to `target`
    double target {};
    double glideStep {};   // semitones a sample
    double randomValue {};
    std::uint32_t seed { 0x1234567u };
    bool releasingNow {};

    Envelope envelopes[envelopeCount];
    LfoState lfos[lfoCount];

    struct Oscillator {
        double phases[maximumUnison] {};
        double previous {};
    };
    Oscillator oscillators[oscillatorCount];
    double subPhase {};
    // Noise: pink's filters, brown's integral, digital's held value.
    double pink[7] {};
    double brown {};
    double held {};
    double heldPhase {};
    double toneState[2] {};
    FilterChannel filters[2];

    std::array<float, parameterCount> modulation {};
    std::array<float, parameterCount> lastModulated {};
    double expressionPitch {};
    double expressionGain { 1.0 };

    double random() noexcept { return random01(seed); }
    double white() noexcept { return 2.0 * random01(seed) - 1.0; }
};

Voice::Voice() : state_(std::make_unique<State>()) {}
Voice::~Voice() = default;
Voice::Voice(Voice&&) noexcept = default;
Voice& Voice::operator=(Voice&&) noexcept = default;

void Voice::prepare(const double sampleRate)
{
    auto& s = *state_;
    s.rate = std::max(1.0, sampleRate);
    const auto length = static_cast<std::size_t>(std::ceil(s.rate / 15.0)) + 4;
    for (auto& filter : s.filters) {
        filter.comb.assign(length, 0.0f);
        filter.combWrite = 0;
    }
}

void Voice::start(const PrismValues& values, const int note, const float velocity, const double glideFrom,
                  const double sampleRate) noexcept
{
    auto& s = *state_;
    if (sampleRate > 0.0 && std::abs(sampleRate - s.rate) > 0.5 && s.filters[0].comb.empty())
        s.rate = sampleRate;
    s.note = note;
    s.velocity = std::clamp(static_cast<double>(velocity), 0.0, 1.0);
    s.target = note;
    s.pitch = glideFrom >= 0.0 ? glideFrom : static_cast<double>(note);
    const auto glide = static_cast<double>(values[voiceBase + voiceGlide]);
    s.glideStep = glide > 1.0e-4 ? std::abs(s.target - s.pitch) / (glide * s.rate) : 1.0e9;
    s.seed = 0x9e3779b9u ^ static_cast<std::uint32_t>(note * 7'919 + static_cast<int>(velocity * 1'000.0f))
             ^ static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(this) >> 4);
    s.randomValue = s.random();
    s.releasingNow = false;
    s.expressionPitch = 0.0;
    s.expressionGain = 1.0;
    for (auto& envelope : s.envelopes) {
        envelope.value = 0.0;
        envelope.start();
    }
    for (int lfo = 0; lfo < lfoCount; ++lfo) {
        auto& state = s.lfos[lfo];
        state = {};
        state.phase = values[static_cast<std::size_t>(lfoBase(lfo) + lfoPhase)];
        state.previous = s.white();
        state.next = s.white();
    }
    // The oscillators' unison voices start where their phase settings say.
    for (int osc = 0; osc < oscillatorCount; ++osc) {
        const auto randomness = static_cast<double>(values[static_cast<std::size_t>(oscBase(osc) + oscRandom)]);
        for (auto& phase : s.oscillators[osc].phases)
            phase = randomness * s.random();
        s.oscillators[osc].previous = 0.0;
    }
    s.subPhase = 0.0;
    for (auto& filter : s.filters)
        filter.reset();
    std::fill(std::begin(s.pink), std::end(s.pink), 0.0);
    s.brown = 0.0;
    s.toneState[0] = s.toneState[1] = 0.0;
}

void Voice::retarget(const PrismValues& values, const int note, const float velocity) noexcept
{
    auto& s = *state_;
    if (s.note < 0) {
        start(values, note, velocity, -1.0, s.rate);
        return;
    }
    s.note = note;
    s.target = note;
    const auto glide = static_cast<double>(values[voiceBase + voiceGlide]);
    s.glideStep = glide > 1.0e-4 ? std::abs(s.target - s.pitch) / (glide * s.rate) : 1.0e9;
    (void) velocity;
    // A key let go and pressed again while the release ran: the envelopes
    // come back from where they are.
    if (s.releasingNow) {
        s.releasingNow = false;
        for (auto& envelope : s.envelopes)
            envelope.start();
    }
}

void Voice::release() noexcept
{
    auto& s = *state_;
    s.releasingNow = true;
    for (auto& envelope : s.envelopes)
        envelope.releaseNow();
}

void Voice::kill() noexcept
{
    auto& s = *state_;
    s.note = -1;
    for (auto& envelope : s.envelopes) {
        envelope.stage = Envelope::idle;
        envelope.value = 0.0;
    }
}

bool Voice::active() const noexcept
{
    return state_->note >= 0 && state_->envelopes[0].stage != Envelope::idle;
}

bool Voice::releasing() const noexcept
{
    return state_->releasingNow;
}

int Voice::note() const noexcept
{
    return state_->note;
}

double Voice::currentPitch() const noexcept
{
    return state_->pitch;
}

void Voice::setExpression(const double pitchSemitones, const double gain) noexcept
{
    state_->expressionPitch = pitchSemitones;
    state_->expressionGain = gain;
}

float Voice::modulated(const int index) const noexcept
{
    return state_->lastModulated[static_cast<std::size_t>(std::clamp(index, 0, parameterCount - 1))];
}

bool Voice::render(const PrismValues& values, const Tables& tables, const Context& context, float* left, float* right,
                   const int count, const double sampleRate) noexcept
{
    auto& s = *state_;
    if (!active())
        return false;
    const auto rate = std::max(1.0, sampleRate);
    const auto dt = 1.0 / rate;
    const auto value = [&values](const int index) { return values[static_cast<std::size_t>(index)]; };
    const auto on = [&value](const int index) { return value(index) >= 0.5f; };
    const auto pick = [&value](const int index) { return static_cast<int>(std::round(value(index))); };

    // The parameters every sample reads, through the modulation.
    std::array<float, parameterCount> effective {};
    const auto& destinationList = destinations();

    for (int offset = 0; offset < count; offset += State::blockLength) {
        const auto length = std::min(State::blockLength, count - offset);

        // ---- Modulation, once a block ---------------------------------------------------
        double sources[static_cast<int>(Source::count)] {};
        for (int env = 0; env < envelopeCount; ++env)
            sources[static_cast<int>(Source::env1) + env] = s.envelopes[env].value;
        for (int lfo = 0; lfo < lfoCount; ++lfo)
            sources[static_cast<int>(Source::lfo1) + lfo] = s.lfos[lfo].value;
        sources[static_cast<int>(Source::velocity)] = s.velocity;
        sources[static_cast<int>(Source::note)] = std::clamp((s.pitch - 60.0) / 60.0, -1.0, 1.0);
        sources[static_cast<int>(Source::modWheel)] = context.modWheel;
        sources[static_cast<int>(Source::aftertouch)] = context.aftertouch;
        sources[static_cast<int>(Source::pitchBend)] = context.pitchBend;
        for (int macro = 0; macro < macroCount; ++macro)
            sources[static_cast<int>(Source::macro1) + macro] = value(macroBase + macro);
        sources[static_cast<int>(Source::random)] = s.randomValue;

        s.modulation.fill(0.0f);
        for (int slot = 0; slot < slotCount; ++slot) {
            const auto source = pick(slotBase(slot) + slotSource);
            const auto destination = pick(slotBase(slot) + slotDestination);
            const auto amount = value(slotBase(slot) + slotAmount);
            if (source <= 0 || source >= static_cast<int>(Source::count) || destination <= 0
                || destination >= static_cast<int>(destinationList.size()) || amount == 0.0f)
                continue;
            s.modulation[static_cast<std::size_t>(destinationList[static_cast<std::size_t>(destination)])]
                += amount * static_cast<float>(sources[source]);
        }
        for (std::size_t at = 1; at < destinationList.size(); ++at) {
            const auto index = destinationList[at];
            const auto normalised = std::clamp(normalise(index, value(index)) + s.modulation[static_cast<std::size_t>(index)], 0.0f, 1.0f);
            s.lastModulated[static_cast<std::size_t>(index)] = normalised;
            effective[static_cast<std::size_t>(index)] = denormalise(index, normalised);
        }
        const auto modulated = [&effective](const int index) { return static_cast<double>(effective[static_cast<std::size_t>(index)]); };

        // ---- Stereo LFOs: on the right, their cycle further on ---------------------------------
        const auto lfoValueAt = [&](const int lfo, double phase) {
            const auto base = lfoBase(lfo);
            const auto& state = s.lfos[lfo];
            const auto shape = static_cast<LfoShape>(std::clamp(pick(base + lfoShape), 0, static_cast<int>(LfoShape::count) - 1));
            phase -= std::floor(phase);
            auto output = lfoWave(shape, phase, state, values.data() + drawBase(lfo));
            if (on(base + lfoUnipolar))
                output = 0.5 * (output + 1.0);
            const auto delay = static_cast<double>(value(base + lfoDelay));
            const auto rise = static_cast<double>(value(base + lfoRise));
            auto amplitude = state.elapsed < delay ? 0.0 : 1.0;
            if (amplitude > 0.0 && rise > 1.0e-4)
                amplitude = std::min(1.0, (state.elapsed - delay) / rise);
            return output * amplitude;
        };
        double lfoRight[lfoCount] {};
        auto stereo = false;
        for (int lfo = 0; lfo < lfoCount; ++lfo) {
            const auto ahead = static_cast<double>(value(lfoStereo(lfo))) * 0.5;
            lfoRight[lfo] = s.lfos[lfo].value;
            if (ahead > 1.0e-4) {
                stereo = true;
                lfoRight[lfo] = lfoValueAt(lfo, s.lfos[lfo].phase + ahead);
            }
        }
        // A parameter as the right side has it: moved by the LFOs' right values.
        const auto modulatedRight = [&](const int index) -> double {
            if (!stereo)
                return modulated(index);
            auto shift = 0.0f;
            for (int slot = 0; slot < slotCount; ++slot) {
                const auto source = pick(slotBase(slot) + slotSource);
                if (source < static_cast<int>(Source::lfo1) || source > static_cast<int>(Source::lfo4))
                    continue;
                const auto destination = pick(slotBase(slot) + slotDestination);
                if (destination <= 0 || destination >= static_cast<int>(destinationList.size())
                    || destinationList[static_cast<std::size_t>(destination)] != index)
                    continue;
                const auto lfo = source - static_cast<int>(Source::lfo1);
                shift += value(slotBase(slot) + slotAmount) * static_cast<float>(lfoRight[lfo] - s.lfos[lfo].value);
            }
            if (shift == 0.0f)
                return modulated(index);
            const auto normalised
                = std::clamp(normalise(index, value(index)) + s.modulation[static_cast<std::size_t>(index)] + shift, 0.0f, 1.0f);
            return static_cast<double>(denormalise(index, normalised));
        };

        // ---- Pitch -------------------------------------------------------------------------
        if (s.pitch != s.target) {
            const auto step = s.glideStep * length;
            s.pitch = s.pitch < s.target ? std::min(s.target, s.pitch + step) : std::max(s.target, s.pitch - step);
        }
        const auto bendSemitones = static_cast<double>(context.pitchBend) * value(voiceBase + voiceBendRange);
        const auto notePitch = s.pitch + bendSemitones + s.expressionPitch;
        const auto toHz = [](const double semitones) { return 440.0 * std::pow(2.0, (semitones - 69.0) / 12.0); };

        // ---- Oscillators: what each reads this block ----------------------------------------
        struct OscBlock {
            bool on {};
            const Wavetable* table {};
            int frameA {};
            int frameB {};
            double frameT {};
            int level {};
            Warp warp { Warp::off };
            double amount {};
            int unison { 1 };
            double increments[State::maximumUnison] {};
            double gainsLeft[State::maximumUnison] {};
            double gainsRight[State::maximumUnison] {};
            double phaseOffset {};
            double levelLeft {};
            double levelRight {};
            bool toFilter { true };
            SpectralMode spectral { SpectralMode::off };
            double spectralAmount {};
            int spectralLevel {};      // low and high pass: the band limit read, and the next
            double spectralFraction {};
            int formantLevel {};       // formant: the band limit its faster reading needs
        } oscs[oscillatorCount];
        for (int osc = 0; osc < oscillatorCount; ++osc) {
            auto& o = oscs[osc];
            const auto base = oscBase(osc);
            const auto& table = tables.oscillator[static_cast<std::size_t>(osc)];
            o.on = on(base + oscOn) && table != nullptr && table->frameCount > 0;
            if (!o.on)
                continue;
            o.table = table.get();
            const auto position = modulated(base + oscPosition) * (table->frameCount - 1);
            o.frameA = std::clamp(static_cast<int>(position), 0, table->frameCount - 1);
            o.frameB = std::min(o.frameA + 1, table->frameCount - 1);
            o.frameT = position - o.frameA;
            o.warp = static_cast<Warp>(std::clamp(pick(base + oscWarp), 0, static_cast<int>(Warp::count) - 1));
            o.amount = modulated(base + oscWarpAmount);
            o.unison = std::clamp(pick(base + oscUnison), 1, State::maximumUnison);
            const auto semitones = notePitch + value(base + oscOctave) * 12.0 + value(base + oscSemi) + modulated(base + oscCoarse)
                                   + modulated(base + oscFine) / 100.0;
            const auto frequency = toHz(semitones);
            const auto detune = modulated(base + oscDetune);
            const auto spreadCents = std::pow(detune, 1.6) * 60.0;
            const auto width = modulated(base + oscWidth);
            const auto blend = modulated(base + oscBlend);
            auto highest = frequency;
            auto power = 0.0;
            for (int u = 0; u < o.unison; ++u) {
                const auto x = o.unison == 1 ? 0.0 : 2.0 * u / (o.unison - 1) - 1.0;
                const auto ratio = std::pow(2.0, x * spreadCents / 1'200.0);
                o.increments[u] = frequency * ratio / rate;
                highest = std::max(highest, frequency * ratio);
                // The middle voices keep their level; the outer ones follow the blend.
                const auto centre = o.unison == 1 || std::abs(x) < 1.0 / std::max(1, o.unison - 1) + 1.0e-9;
                const auto gain = o.unison == 1 ? 1.0 : (centre ? 1.0 - 0.5 * blend : blend);
                double gainLeft {};
                double gainRight {};
                panGains(x * width, gainLeft, gainRight);
                o.gainsLeft[u] = gain * gainLeft;
                o.gainsRight[u] = gain * gainRight;
                power += gain * gain;
            }
            const auto normalisation = 1.0 / std::sqrt(std::max(power, 1.0e-6));
            for (int u = 0; u < o.unison; ++u) {
                o.gainsLeft[u] *= normalisation;
                o.gainsRight[u] *= normalisation;
            }
            // The band limit no voice of it can alias past.
            const auto allowed = rate * 0.5 * 0.92 / std::max(highest, 1.0);
            auto level = 0;
            while (level < Wavetable::levels - 1 && ((Wavetable::frameLength / 2 - 1) >> level) > allowed)
                ++level;
            o.level = level;
            o.phaseOffset = modulated(base + oscPhase);
            double panLeft {};
            double panRight {};
            panGains(modulated(base + oscPan), panLeft, panRight);
            const auto level01 = modulated(base + oscLevel);
            o.levelLeft = level01 * panLeft;
            o.levelRight = level01 * panRight;
            o.toFilter = on(base + oscToFilter);
            o.spectral = static_cast<SpectralMode>(std::clamp(pick(spectralBase(osc) + spectralMode), 0, static_cast<int>(SpectralMode::count) - 1));
            o.spectralAmount = modulated(spectralBase(osc) + spectralAmount);
            const auto cut = o.spectralAmount * 9.0;
            o.spectralLevel = std::min(Wavetable::levels - 1, o.level + static_cast<int>(cut));
            o.spectralFraction = o.spectralLevel == Wavetable::levels - 1 ? 0.0 : cut - std::floor(cut);
            o.formantLevel = std::min(Wavetable::levels - 1, o.level + static_cast<int>(std::ceil(std::log2(1.0 + 3.0 * o.spectralAmount))));
        }
        // Which plays first: one that another reads (FM, AM, RM) goes before it.
        const auto reads = [&oscs](const int osc) {
            return oscs[osc].on && (oscs[osc].warp == Warp::fm || oscs[osc].warp == Warp::am || oscs[osc].warp == Warp::rm);
        };
        // C reads A: it plays last.
        const int order[oscillatorCount] = { reads(0) && !reads(1) ? 1 : 0, reads(0) && !reads(1) ? 0 : 1, 2 };

        // Sub.
        const auto subOnNow = on(subBase + subOn);
        const auto subWave = pick(subBase + subShape);
        const auto subIncrement = toHz(notePitch + value(subBase + subOctave) * 12.0) / rate;
        double subLeft {};
        double subRight {};
        panGains(modulated(subBase + subPan), subLeft, subRight);
        subLeft *= modulated(subBase + subLevel);
        subRight *= modulated(subBase + subLevel);
        const auto subToOutput = on(subBase + subDirect);

        // Noise.
        const auto noiseOnNow = on(noiseBase + noiseOn);
        const auto noiseKind = pick(noiseBase + noiseType);
        const auto tone = modulated(noiseBase + noiseTone);
        const auto noiseRate = std::pow(2.0, modulated(noiseBase + noisePitch) * 14.0) * 1.3 / rate;
        // The tone: a low pass below the middle, a high pass above.
        const auto toneCutoff = tone < 0.5 ? 150.0 * std::pow(130.0, tone * 2.0) : 40.0 * std::pow(250.0, (tone - 0.5) * 2.0);
        const auto toneCoefficient = 1.0 - std::exp(-twoPi * std::min(toneCutoff, rate * 0.45) / rate);
        double noiseLeft {};
        double noiseRight {};
        panGains(modulated(noiseBase + noisePan), noiseLeft, noiseRight);
        noiseLeft *= modulated(noiseBase + noiseLevel);
        noiseRight *= modulated(noiseBase + noiseLevel);
        const auto noiseFiltered = on(noiseBase + noiseToFilter);

        // Filter.
        const auto filterOnNow = on(filterBase + filterOn);
        const auto filterKind = static_cast<FilterType>(std::clamp(pick(filterBase + filterType), 0, static_cast<int>(FilterType::count) - 1));
        // A block a side: they differ when a stereo LFO moves the filter.
        Block blocks[2];
        if (filterOnNow) {
            for (int side = 0; side < 2; ++side) {
                auto& block = blocks[side];
                const auto sideValue = [&](const int index) { return side == 0 ? modulated(index) : modulatedRight(index); };
                const auto keytrack = sideValue(filterBase + filterKeytrack);
                block.cutoff = std::clamp(sideValue(filterBase + filterCutoff) * std::pow(2.0, (s.pitch - 60.0) / 12.0 * keytrack), 15.0,
                                          rate * 0.45);
                const auto resonance = sideValue(filterBase + filterResonance);
                block.q = 0.5 + resonance * resonance * 24.0;
                block.drive = sideValue(filterBase + filterDrive);
                block.variation = sideValue(filterBase + filterVariation);
                block.mix = sideValue(filterBase + filterMix);
                {
                    auto& channel = s.filters[side];
                    switch (filterKind) {
                    case FilterType::lowpass24:
                    case FilterType::highpass24:
                    case FilterType::bandpass24:
                        // Four poles: the resonance on the second pair only.
                        channel.first.set(block.cutoff, 0.54, rate);
                        channel.second.set(block.cutoff, 1.31 * (1.0 + resonance * resonance * 9.0), rate);
                        break;
                    case FilterType::peak:
                    case FilterType::notch:
                    case FilterType::lowpass12:
                    case FilterType::highpass12:
                    case FilterType::bandpass12:
                        channel.first.set(block.cutoff, std::max(0.5, block.q), rate);
                        break;
                    case FilterType::formant: {
                        // The vowel by the variation, its formants moved by the cutoff.
                        const double vowels[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 270, 2290, 3010 },
                                                      { 450, 800, 2830 },  { 325, 700, 2530 } };
                        const auto at = block.variation * 4.0;
                        const auto from = std::min(3, static_cast<int>(at));
                        const auto t = at - from;
                        const auto shift = std::clamp(block.cutoff / 1'000.0, 0.25, 4.0);
                        for (int f = 0; f < 3; ++f) {
                            const auto frequency = (vowels[from][f] + (vowels[from + 1][f] - vowels[from][f]) * t) * shift;
                            channel.formants[f].set(frequency, 4.0 + resonance * 16.0, rate);
                        }
                        break;
                    }
                    case FilterType::ladder:
                    case FilterType::combPlus:
                    case FilterType::combMinus:
                    case FilterType::count:
                        break;
                    }
                }
                const auto g = std::tan(std::numbers::pi * block.cutoff / rate);
                block.ladderG = g / (1.0 + g);
                block.ladderK = resonance * 3.9;
                block.combDelay = std::clamp(rate / block.cutoff, 1.0, std::max(1.0, static_cast<double>(s.filters[0].comb.size()) - 2.0));
                block.combFeedback = (filterKind == FilterType::combMinus ? -1.0 : 1.0) * resonance * 0.97;
                block.formantGains[0] = 1.0;
                block.formantGains[1] = 0.7;
                block.formantGains[2] = 0.4;
            }
        }
        const auto runFilter = [&](const int side, FilterChannel& channel, const double input) {
            const auto& block = blocks[side];
            const auto driveGain = 1.0 + block.drive * 12.0;
            const auto driveScale = 1.0 / std::sqrt(driveGain);
            auto x = input;
            if (block.drive > 0.001 && filterKind != FilterType::ladder)
                x = std::tanh(x * driveGain) * driveScale * 1.5;
            double low {};
            double band {};
            double high {};
            double y = x;
            switch (filterKind) {
            case FilterType::lowpass12:
                channel.first.process(x, low, band, high);
                y = low;
                break;
            case FilterType::highpass12:
                channel.first.process(x, low, band, high);
                y = high;
                break;
            case FilterType::bandpass12:
                channel.first.process(x, low, band, high);
                y = band * channel.first.k;
                break;
            case FilterType::notch:
                channel.first.process(x, low, band, high);
                y = x - channel.first.k * band;
                break;
            case FilterType::peak: {
                channel.first.process(x, low, band, high);
                const auto gain = std::pow(10.0, (block.variation - 0.5) * 36.0 / 20.0);
                y = x + (gain - 1.0) * channel.first.k * band;
                break;
            }
            case FilterType::lowpass24:
                channel.first.process(x, low, band, high);
                channel.second.process(low, low, band, high);
                y = low;
                break;
            case FilterType::highpass24:
                channel.first.process(x, low, band, high);
                channel.second.process(high, low, band, high);
                y = high;
                break;
            case FilterType::bandpass24:
                channel.first.process(x, low, band, high);
                channel.second.process(band * channel.first.k, low, band, high);
                y = band * channel.second.k;
                break;
            case FilterType::ladder: {
                auto stage = std::tanh((x - block.ladderK * channel.ladderOut) * (1.0 + block.drive * 4.0));
                for (auto& memory : channel.ladder) {
                    const auto v = (stage - memory) * block.ladderG;
                    const auto out = v + memory;
                    memory = out + v;
                    stage = out;
                }
                channel.ladderOut = stage;
                y = stage * (1.0 + block.ladderK * 0.35);
                break;
            }
            case FilterType::combPlus:
            case FilterType::combMinus: {
                auto& buffer = channel.comb;
                if (buffer.size() < 4) {
                    y = x;
                    break;
                }
                const auto size = static_cast<int>(buffer.size());
                auto read = static_cast<double>(channel.combWrite) - block.combDelay;
                while (read < 0.0)
                    read += size;
                const auto i0 = static_cast<int>(read) % size;
                const auto i1 = (i0 + 1) % size;
                const auto t = read - std::floor(read);
                const auto delayed = buffer[static_cast<std::size_t>(i0)] + (buffer[static_cast<std::size_t>(i1)] - buffer[static_cast<std::size_t>(i0)]) * t;
                y = x + block.combFeedback * delayed;
                buffer[static_cast<std::size_t>(channel.combWrite)] = static_cast<float>(std::clamp(y, -4.0, 4.0));
                channel.combWrite = (channel.combWrite + 1) % size;
                y *= 0.5;
                break;
            }
            case FilterType::formant: {
                y = 0.0;
                for (int f = 0; f < 3; ++f) {
                    channel.formants[f].process(x, low, band, high);
                    y += band * channel.formants[f].k * block.formantGains[f];
                }
                y *= 1.4;
                break;
            }
            case FilterType::count:
                break;
            }
            return input + (y - input) * block.mix;
        };

        // The level: env 1, the velocity, the master.
        const auto velocityAmount = static_cast<double>(value(voiceBase + voiceVelocity));
        const auto velocityGain = 1.0 - velocityAmount * (1.0 - s.velocity);
        const auto master = modulated(voiceBase + voiceMaster) * velocityGain * 0.5 * s.expressionGain;
        const auto masterRight = modulatedRight(voiceBase + voiceMaster) * velocityGain * 0.5 * s.expressionGain;

        // LFOs this block.
        double lfoIncrements[lfoCount] {};
        for (int lfo = 0; lfo < lfoCount; ++lfo) {
            const auto base = lfoBase(lfo);
            const auto hz = on(base + lfoSync) ? context.tempo / 60.0 / divisionBeats(pick(base + lfoDivision))
                                              : modulated(base + lfoRate);
            lfoIncrements[lfo] = hz / rate;
        }

        // ---- Samples ------------------------------------------------------------------------
        for (int i = 0; i < length; ++i) {
            // The envelopes and LFOs move every sample.
            for (int env = 0; env < envelopeCount; ++env)
                s.envelopes[env].next(values.data() + envBase(env), dt);
            for (int lfo = 0; lfo < lfoCount; ++lfo) {
                auto& state = s.lfos[lfo];
                const auto base = lfoBase(lfo);
                const auto mode = static_cast<LfoMode>(std::clamp(pick(base + lfoMode), 0, 2));
                const auto shape = static_cast<LfoShape>(std::clamp(pick(base + lfoShape), 0, static_cast<int>(LfoShape::count) - 1));
                double phase {};
                if (mode == LfoMode::free) {
                    const auto clock = context.clockSeconds + static_cast<double>(offset + i) * dt;
                    const auto cycles = clock * lfoIncrements[lfo] * rate + value(base + lfoPhase);
                    phase = cycles - std::floor(cycles);
                    if (phase < state.phase - 0.5) {
                        state.previous = state.next;
                        state.next = s.white();
                    }
                    state.phase = phase;
                } else if (!state.finished) {
                    state.phase += lfoIncrements[lfo];
                    if (state.phase >= 1.0) {
                        if (mode == LfoMode::envelope) {
                            state.phase = 1.0 - 1.0e-9;
                            state.finished = true;
                        } else {
                            state.phase -= std::floor(state.phase);
                            state.previous = state.next;
                            state.next = s.white();
                        }
                    }
                    phase = state.phase;
                } else {
                    phase = state.phase;
                }
                auto output = lfoWave(shape, phase, state, values.data() + drawBase(lfo));
                if (on(base + lfoUnipolar))
                    output = 0.5 * (output + 1.0);
                // Delay, then rise.
                state.elapsed += dt;
                const auto delay = static_cast<double>(value(base + lfoDelay));
                const auto rise = static_cast<double>(value(base + lfoRise));
                auto amplitude = state.elapsed < delay ? 0.0 : 1.0;
                if (amplitude > 0.0 && rise > 1.0e-4)
                    amplitude = std::min(1.0, (state.elapsed - delay) / rise);
                output *= amplitude;
                const auto smooth = static_cast<double>(value(base + lfoSmooth));
                if (smooth > 0.001) {
                    const auto coefficient = 1.0 - std::exp(-dt / (smooth * 0.08));
                    state.smoothed += (output - state.smoothed) * coefficient;
                    output = state.smoothed;
                } else {
                    state.smoothed = output;
                }
                state.value = output;
            }

            double filterLeft {};
            double filterRight {};
            double directLeft {};
            double directRight {};
            double played[oscillatorCount] { oscs[0].on ? s.oscillators[0].previous : 0.0, oscs[1].on ? s.oscillators[1].previous : 0.0,
                                             oscs[2].on ? s.oscillators[2].previous : 0.0 };
            for (const auto osc : order) {
                const auto& o = oscs[osc];
                if (!o.on)
                    continue;
                auto& state = s.oscillators[osc];
                const auto other = played[osc == 2 ? 0 : 1 - osc];
                const auto* a = o.table->frame(o.frameA, o.level);
                const auto* b = o.table->frame(o.frameB, o.level);
                double sumLeft {};
                double sumRight {};
                double mono {};
                for (int u = 0; u < o.unison; ++u) {
                    auto& phase = state.phases[u];
                    phase += o.increments[u];
                    if (phase >= 1.0)
                        phase -= std::floor(phase);
                    auto p = phase + o.phaseOffset;
                    p -= std::floor(p);
                    auto silent = false;
                    auto mirror = -1.0;
                    switch (o.warp) {
                    case Warp::sync: {
                        const auto q = p * (1.0 + o.amount * 15.0);
                        p = q - std::floor(q);
                        break;
                    }
                    case Warp::bendPlus: p = std::pow(p, 1.0 / (1.0 + o.amount * 6.0)); break;
                    case Warp::bendMinus: p = std::pow(p, 1.0 + o.amount * 6.0); break;
                    case Warp::bendBoth: {
                        const auto e = 1.0 + o.amount * 6.0;
                        p = p < 0.5 ? 0.5 * std::pow(2.0 * p, e) : 1.0 - 0.5 * std::pow(2.0 * (1.0 - p), e);
                        break;
                    }
                    case Warp::pwm: {
                        const auto width = 1.0 - o.amount * 0.95;
                        if (p < width)
                            p /= width;
                        else
                            silent = true;
                        break;
                    }
                    case Warp::asymPlus:
                    case Warp::asymMinus: {
                        const auto middle = 0.5 + (o.warp == Warp::asymPlus ? 0.49 : -0.49) * o.amount;
                        p = p < middle ? 0.5 * p / middle : 0.5 + 0.5 * (p - middle) / (1.0 - middle);
                        break;
                    }
                    case Warp::mirror: mirror = p < 0.5 ? 2.0 * p : 2.0 * (1.0 - p); break;
                    case Warp::fm: {
                        const auto q = p + o.amount * 1.5 * other;
                        p = q - std::floor(q);
                        break;
                    }
                    default: break;
                    }
                    auto sample = 0.0;
                    if (!silent) {
                        const auto readFrom = [&](const float* first, const float* second, const double at) {
                            const auto index = at * Wavetable::frameLength;
                            const auto i0 = static_cast<int>(index) & (Wavetable::frameLength - 1);
                            const auto i1 = (i0 + 1) & (Wavetable::frameLength - 1);
                            const auto t = index - std::floor(index);
                            const auto one = first[i0] + (first[i1] - first[i0]) * t;
                            const auto two = second[i0] + (second[i1] - second[i0]) * t;
                            return one + (two - one) * o.frameT;
                        };
                        const auto read = [&](const double at) { return readFrom(a, b, at); };
                        // The same frames at another band limit.
                        const auto readLevel = [&](const double at, const int level) {
                            const auto limit = std::clamp(level, 0, Wavetable::levels - 1);
                            return readFrom(o.table->frame(o.frameA, limit), o.table->frame(o.frameB, limit), at);
                        };
                        sample = read(p);
                        if (mirror >= 0.0)
                            sample += (read(mirror) - sample) * o.amount;
                        // The spectral morph: the table read again, elsewhere or with fewer harmonics.
                        switch (o.spectral) {
                        case SpectralMode::lowPass:
                        case SpectralMode::highPass: {
                            const auto low = readLevel(p, o.spectralLevel);
                            const auto lower = o.spectralFraction > 0.0 ? low + (readLevel(p, o.spectralLevel + 1) - low) * o.spectralFraction : low;
                            sample = o.spectral == SpectralMode::lowPass ? lower : sample - lower;
                            break;
                        }
                        case SpectralMode::odd: sample -= o.spectralAmount * 0.5 * (sample + read(p + 0.5)); break;
                        case SpectralMode::even: sample += o.spectralAmount * 0.5 * (read(p + 0.5) - sample); break;
                        case SpectralMode::comb: sample = 0.5 * (sample + read(p + o.spectralAmount * 0.5)); break;
                        case SpectralMode::octave: sample += o.spectralAmount * (readLevel(2.0 * p, o.level + 1) - sample); break;
                        case SpectralMode::formant:
                            sample = std::sin(std::numbers::pi * p) * readLevel(p * (1.0 + 3.0 * o.spectralAmount), o.formantLevel);
                            break;
                        default: break;
                        }
                    }
                    switch (o.warp) {
                    case Warp::flip:
                        if (p >= 1.0 - o.amount * 0.5)
                            sample = -sample;
                        break;
                    case Warp::quantise: {
                        const auto steps = std::pow(2.0, 1.0 + (1.0 - o.amount) * 7.0);
                        sample = std::round(sample * steps) / steps;
                        break;
                    }
                    case Warp::am: sample *= 1.0 - o.amount * 0.5 * (1.0 - other); break;
                    case Warp::rm: sample *= 1.0 + (other - 1.0) * o.amount; break;
                    default: break;
                    }
                    sumLeft += sample * o.gainsLeft[u];
                    sumRight += sample * o.gainsRight[u];
                    mono += sample;
                }
                state.previous = mono / o.unison;
                played[osc] = state.previous;
                const auto outLeft = sumLeft * o.levelLeft;
                const auto outRight = sumRight * o.levelRight;
                if (o.toFilter && filterOnNow) {
                    filterLeft += outLeft;
                    filterRight += outRight;
                } else {
                    directLeft += outLeft;
                    directRight += outRight;
                }
            }
            if (subOnNow) {
                s.subPhase += subIncrement;
                if (s.subPhase >= 1.0)
                    s.subPhase -= std::floor(s.subPhase);
                const auto t = s.subPhase;
                auto sample = 0.0;
                switch (subWave) {
                case 0: sample = std::sin(twoPi * t); break;
                case 1: sample = 1.0 - 4.0 * std::abs(t - 0.5); break;
                case 2: sample = 2.0 * t - 1.0 - polyBlep(t, subIncrement); break;
                default: {
                    const auto width = subWave == 3 ? 0.5 : subWave == 4 ? 0.25 : 0.1;
                    sample = t < width ? 1.0 : -1.0;
                    sample += polyBlep(t, subIncrement);
                    auto shifted = t + 1.0 - width;
                    shifted -= std::floor(shifted);
                    sample -= polyBlep(shifted, subIncrement);
                    break;
                }
                }
                if (subToOutput || !filterOnNow) {
                    directLeft += sample * subLeft;
                    directRight += sample * subRight;
                } else {
                    filterLeft += sample * subLeft;
                    filterRight += sample * subRight;
                }
            }
            if (noiseOnNow) {
                auto sample = 0.0;
                const auto white = s.white();
                switch (noiseKind) {
                case 1: { // pink (Paul Kellet's)
                    auto* b = s.pink;
                    b[0] = 0.99886 * b[0] + white * 0.0555179;
                    b[1] = 0.99332 * b[1] + white * 0.0750759;
                    b[2] = 0.96900 * b[2] + white * 0.1538520;
                    b[3] = 0.86650 * b[3] + white * 0.3104856;
                    b[4] = 0.55000 * b[4] + white * 0.5329522;
                    b[5] = -0.7616 * b[5] - white * 0.0168980;
                    sample = (b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + white * 0.5362) * 0.11;
                    b[6] = white * 0.115926;
                    break;
                }
                case 2:
                    s.brown = std::clamp(s.brown * 0.998 + white * 0.04, -1.0, 1.0);
                    sample = s.brown * 2.5;
                    break;
                case 3:
                    s.heldPhase += noiseRate;
                    if (s.heldPhase >= 1.0) {
                        s.heldPhase -= std::floor(s.heldPhase);
                        s.held = white;
                    }
                    sample = s.held;
                    break;
                case 4:
                    sample = s.random() < 0.002 + noiseRate * 0.05 ? white * 1.6 : 0.0;
                    break;
                case 5: {
                    // Hiss: white without its lows.
                    s.toneState[1] += (white - s.toneState[1]) * 0.08;
                    sample = (white - s.toneState[1]) * 0.8;
                    break;
                }
                default: sample = white * 0.7; break;
                }
                // The tone.
                s.toneState[0] += (sample - s.toneState[0]) * toneCoefficient;
                sample = tone < 0.5 ? s.toneState[0] : sample - s.toneState[0];
                if (noiseFiltered && filterOnNow) {
                    filterLeft += sample * noiseLeft;
                    filterRight += sample * noiseRight;
                } else {
                    directLeft += sample * noiseLeft;
                    directRight += sample * noiseRight;
                }
            }
            if (filterOnNow) {
                filterLeft = runFilter(0, s.filters[0], filterLeft);
                filterRight = runFilter(1, s.filters[1], filterRight);
            }
            const auto amplitude = s.envelopes[0].value * master;
            const auto amplitudeRight = s.envelopes[0].value * masterRight;
            left[offset + i] += static_cast<float>((filterLeft + directLeft) * amplitude);
            right[offset + i] += static_cast<float>((filterRight + directRight) * amplitudeRight);
        }
        if (s.envelopes[0].stage == Envelope::idle) {
            s.note = -1;
            return false;
        }
    }
    return true;
}

} // namespace cydaw::engine::prism
