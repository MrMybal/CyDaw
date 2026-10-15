#include <cydaw/engine/SynthFamily.h>

#include <cydaw/engine/EffectCatalogue.h>

#include "SynthEngines.h"
#include "SynthTables.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>

namespace cydaw::engine::synth {

namespace detail {

void addVoicing(Table& table, const int voices, const float master)
{
    table.section = 0;
    table.choice("voice.mode", "Jeu", "Play", { { "Poly", "Poly" }, { "Mono", "Mono" }, { "Legato", "Legato" } }, 0);
    table.whole("voice.count", "Voix", "Voices", {}, 1, 32, voices);
    table.add("voice.glide", "Glissando", "Glide", "s", 0.0f, 2.0f, 0.0f);
    table.toggle("voice.glideAlways", "Toujours glisser", "Always glide", false);
    table.whole("voice.bend", "Pitch bend", "Pitch bend", "st", 0, 24, 2);
    table.add("voice.master", "Volume", "Volume", {}, 0.0f, 1.0f, master);
}

} // namespace detail

namespace {

struct Catalogue {
    std::array<ModelInfo, modelCount> models;
    std::array<std::unordered_map<std::string, int>, modelCount> indices;
};

const Catalogue& catalogue()
{
    static const Catalogue built = [] {
        Catalogue result;
        result.models[0].model = Model::none;
        auto& ambre = result.models[static_cast<std::size_t>(Model::ambre)];
        ambre.model = Model::ambre;
        ambre.typeKey = "ambre-synth";
        ambre.prefix = "ambre";
        ambre.processorKey = "core.ambre-synth";
        ambre.instrument = "ambre-instrument";
        ambre.french = "Ambre";
        ambre.english = "Ambre";
        ambre.frenchHint = "Un synthé analogique : trois oscillateurs, quatre filtres de caractère, des enveloppes et des LFO qui respirent";
        ambre.englishHint = "An analog synth: three oscillators, four filters of character, envelopes and LFOs that breathe";
        ambre.parameters = detail::ambreParameters();
        ambre.sections = detail::ambreSections();
        auto& colosse = result.models[static_cast<std::size_t>(Model::colosse)];
        colosse.model = Model::colosse;
        colosse.typeKey = "colosse-synth";
        colosse.prefix = "colosse";
        colosse.processorKey = "core.colosse-synth";
        colosse.instrument = "colosse-instrument";
        colosse.french = "Colosse";
        colosse.english = "Colosse";
        colosse.frenchHint = "Un synthé moderne et massif : deux oscillateurs à tables d'ondes déformables, deux filtres, une séquence et six modulations libres";
        colosse.englishHint = "A big modern synth: two warpable wavetable oscillators, two filters, a step sequence and six free modulations";
        colosse.parameters = detail::colosseParameters();
        colosse.sections = detail::colosseSections();
        auto& mosaique = result.models[static_cast<std::size_t>(Model::mosaique)];
        mosaique.model = Model::mosaique;
        mosaique.typeKey = "mosaique-synth";
        mosaique.prefix = "mosaique";
        mosaique.processorKey = "core.mosaique-synth";
        mosaique.instrument = "mosaique-instrument";
        mosaique.french = "Mosaïque";
        mosaique.english = "Mosaïque";
        mosaique.frenchHint = "Un synthé hybride : deux moteurs (analogique, tables, harmonique, modal), deux filtres, une fonction dessinée, de l'aléatoire et quatre macros";
        mosaique.englishHint = "A hybrid synth: two engines (analog, wavetable, harmonic, modal), two filters, a drawn function, randomness and four macros";
        mosaique.parameters = detail::mosaiqueParameters();
        mosaique.sections = detail::mosaiqueSections();
        auto& nebuleuse = result.models[static_cast<std::size_t>(Model::nebuleuse)];
        nebuleuse.model = Model::nebuleuse;
        nebuleuse.typeKey = "nebuleuse-synth";
        nebuleuse.prefix = "nebuleuse";
        nebuleuse.processorKey = "core.nebuleuse-synth";
        nebuleuse.instrument = "nebuleuse-instrument";
        nebuleuse.french = "Nébuleuse";
        nebuleuse.english = "Nébuleuse";
        nebuleuse.frenchHint = "Un synthé d'atmosphères : deux couches (synthèse ou grains de sept textures), des accords, l'orbe qui fait tout bouger";
        nebuleuse.englishHint = "A synth of atmospheres: two layers (synthesis or grains of seven textures), chords, the orb that moves it all";
        nebuleuse.parameters = detail::nebuleuseParameters();
        nebuleuse.sections = detail::nebuleuseSections();
        auto& opale = result.models[static_cast<std::size_t>(Model::opale)];
        opale.model = Model::opale;
        opale.typeKey = "opale-synth";
        opale.prefix = "opale";
        opale.processorKey = "core.opale-synth";
        opale.instrument = "opale-instrument";
        opale.french = "Opale";
        opale.english = "Opale";
        opale.frenchHint = "Le synthé polyphonique de tous les jours : deux oscillateurs profonds (formes continues, sub, synchro, unisson), un mélangeur créatif à six modes, un filtre de sept sortes";
        opale.englishHint = "The everyday polysynth: two deep oscillators (continuous shapes, sub, sync, unison), a creative six-mode mixer, a filter of seven kinds";
        opale.parameters = detail::opaleParameters();
        opale.sections = detail::opaleSections();
        auto& onyx = result.models[static_cast<std::size_t>(Model::onyx)];
        onyx.model = Model::onyx;
        onyx.typeKey = "onyx-synth";
        onyx.prefix = "onyx";
        onyx.processorKey = "core.onyx-synth";
        onyx.instrument = "onyx-instrument";
        onyx.french = "Onyx";
        onyx.english = "Onyx";
        onyx.frenchHint = "Un synthé à distorsion de phase : quatre oscillateurs aux phases pliées (huit formes, dont trois résonantes) qui se modulent entre eux, un filtre de sept sortes ; brillant, numérique, vocal";
        onyx.englishHint = "A phase-distortion synth: four oscillators with bent phases (eight shapes, three of them resonant) modulating one another, a filter of seven kinds; bright, digital, vocal";
        onyx.parameters = detail::onyxParameters();
        onyx.sections = detail::onyxSections();
        auto& tirette = result.models[static_cast<std::size_t>(Model::tirette)];
        tirette.model = Model::tirette;
        tirette.typeKey = "tirette-synth";
        tirette.prefix = "tirette";
        tirette.processorKey = "core.tirette-synth";
        tirette.instrument = "tirette-instrument";
        tirette.french = "Tirette";
        tirette.english = "Tirette";
        tirette.frenchHint = "Un orgue à tirettes : neuf tirettes, roues phoniques, transistors ou tuyaux, percussion, vibrato et chorus, saturation et cabine tournante";
        tirette.englishHint = "A drawbar organ: nine drawbars, tone wheels, transistors or pipes, percussion, vibrato and chorus, overdrive and a rotary speaker";
        tirette.parameters = detail::tiretteParameters();
        tirette.sections = detail::tiretteSections();
        auto& alliage = result.models[static_cast<std::size_t>(Model::alliage)];
        alliage.model = Model::alliage;
        alliage.typeKey = "alliage-synth";
        alliage.prefix = "alliage";
        alliage.processorKey = "core.alliage-synth";
        alliage.instrument = "alliage-instrument";
        alliage.french = "Alliage";
        alliage.english = "Alliage";
        alliage.frenchHint = "Un synthé à modules : un oscillateur parmi huit (essaim, table d'ondes, impulsion, FM, synchro, additif, bande de bruit…), un filtre parmi sept, deux enveloppes et un LFO : vite des sons très différents";
        alliage.englishHint = "A pick-and-mix synth: one oscillator of eight kinds (swarm, wavetable, pulse, FM, sync, additive, noise band…), one filter of seven, two envelopes and an LFO: very different sounds, fast";
        alliage.parameters = detail::alliageParameters();
        alliage.sections = detail::alliageSections();
        auto& quartz = result.models[static_cast<std::size_t>(Model::quartz)];
        quartz.model = Model::quartz;
        quartz.typeKey = "quartz-synth";
        quartz.prefix = "quartz";
        quartz.processorKey = "core.quartz-synth";
        quartz.instrument = "quartz-instrument";
        quartz.french = "Quartz";
        quartz.english = "Quartz";
        quartz.frenchHint = "Un synthé FM à quatre opérateurs : une matrice libre où chacun module les autres et lui-même, du bruit, une enveloppe par opérateur, un filtre de sept sortes";
        quartz.englishHint = "A four-operator FM synth: a free matrix where each operator modulates the others and itself, noise, an envelope per operator, a filter of seven kinds";
        quartz.parameters = detail::quartzParameters();
        quartz.sections = detail::quartzSections();
        auto& argile = result.models[static_cast<std::size_t>(Model::argile)];
        argile.model = Model::argile;
        argile.typeKey = "argile-synth";
        argile.prefix = "argile";
        argile.processorKey = "core.argile-synth";
        argile.instrument = "argile-instrument";
        argile.french = "Argile";
        argile.english = "Argile";
        argile.frenchHint = "Un synthé analogique classique, tout en double : deux oscillateurs avec leur sub, deux filtres de huit types en parallèle ou en série, deux amplis avec leur enveloppe et leur panoramique, deux LFO";
        argile.englishHint = "A classic analog synth with two of everything: two oscillators with their subs, two filters of eight types side by side or in series, two amplifiers each with its own envelope and pan, two LFOs";
        argile.parameters = detail::argileParameters();
        argile.sections = detail::argileSections();
        auto& saphir = result.models[static_cast<std::size_t>(Model::saphir)];
        saphir.model = Model::saphir;
        saphir.typeKey = "saphir-synth";
        saphir.prefix = "saphir";
        saphir.processorKey = "core.saphir-synth";
        saphir.instrument = "saphir-instrument";
        saphir.french = "Saphir";
        saphir.english = "Saphir";
        saphir.frenchHint = "Un synthé FM à quatre opérateurs : onze algorithmes, douze formes d'onde, des enveloppes qui bouclent, un filtre, une enveloppe de hauteur et un LFO";
        saphir.englishHint = "A four-operator FM synth: eleven algorithms, twelve waves, looping envelopes, a filter, a pitch envelope and an LFO";
        saphir.parameters = detail::saphirParameters();
        saphir.sections = detail::saphirSections();
        auto& derive = result.models[static_cast<std::size_t>(Model::derive)];
        derive.model = Model::derive;
        derive.typeKey = "derive-synth";
        derive.prefix = "derive";
        derive.processorKey = "core.derive-synth";
        derive.instrument = "derive-instrument";
        derive.french = "Dérive";
        derive.english = "Dérive";
        derive.frenchHint = "Un synthé analogique simple et chaleureux : deux oscillateurs, une dérive vivante par voix, une enveloppe qui peut tourner en boucle, trois modulations";
        derive.englishHint = "A simple, warm analog synth: two oscillators, a living drift on each voice, an envelope that can cycle, three modulations";
        derive.parameters = detail::deriveParameters();
        derive.sections = detail::deriveSections();
        auto& bronze = result.models[static_cast<std::size_t>(Model::bronze)];
        bronze.model = Model::bronze;
        bronze.typeKey = "bronze-synth";
        bronze.prefix = "bronze";
        bronze.processorKey = "core.bronze-synth";
        bronze.instrument = "bronze-instrument";
        bronze.french = "Bronze";
        bronze.english = "Bronze";
        bronze.frenchHint = "Des objets frappés, frottés ou soufflés : un maillet et un bruit qui excitent deux résonateurs (poutre, marimba, membrane, plaque, corde, tuyau, tube), en série ou côte à côte";
        bronze.englishHint = "Struck, rubbed and blown objects: a mallet and a noise exciting two resonators (beam, marimba, membrane, plate, string, pipe, tube), in series or side by side";
        bronze.parameters = detail::bronzeParameters();
        bronze.sections = detail::bronzeSections();
        auto& jade = result.models[static_cast<std::size_t>(Model::jade)];
        jade.model = Model::jade;
        jade.typeKey = "jade-synth";
        jade.prefix = "jade";
        jade.processorKey = "core.jade-synth";
        jade.instrument = "jade-instrument";
        jade.french = "Jade";
        jade.english = "Jade";
        jade.frenchHint = "Un piano électrique modélisé : lames et barres de ton, anches ou cordes frappées, lues par leur micro (cloche, aboiement, grognement), avec trémolo panoramique et chorus";
        jade.englishHint = "A modelled electric piano: tines and tone bars, reeds or struck strings, read by their pickup (bell, bark, growl), with auto-pan tremolo and chorus";
        jade.parameters = detail::jadeParameters();
        jade.sections = detail::jadeSections();
        auto& silex = result.models[static_cast<std::size_t>(Model::silex)];
        silex.model = Model::silex;
        silex.typeKey = "silex-synth";
        silex.prefix = "silex";
        silex.processorKey = "core.silex-synth";
        silex.instrument = "silex-instrument";
        silex.french = "Silex";
        silex.english = "Silex";
        silex.frenchHint = "Une boîte à rythmes dont chaque son est synthétisé : grosse caisse, caisse claire, clap, charlestons, toms, cymbales et percussions, chacun ses réglages ; trois caractères (analogique, numérique, lo-fi)";
        silex.englishHint = "A drum machine whose every sound is synthesised: kick, snare, clap, hi-hats, toms, cymbals and percussion, each with its own settings; three characters (analog, digital, lo-fi)";
        silex.parameters = detail::silexParameters();
        silex.sections = detail::silexSections();
        for (std::size_t model = 0; model < result.models.size(); ++model)
            for (int index = 0; index < static_cast<int>(result.models[model].parameters.size()); ++index)
                result.indices[model][result.models[model].parameters[static_cast<std::size_t>(index)].key] = index;
        return result;
    }();
    return built;
}

const Parameter& fallbackParameter()
{
    static const Parameter none {};
    return none;
}

} // namespace

const ModelInfo& info(const Model model)
{
    const auto index = std::clamp(static_cast<int>(model), 0, modelCount - 1);
    return catalogue().models[static_cast<std::size_t>(index)];
}

Model modelForType(const std::string_view typeKey)
{
    for (const auto& model : catalogue().models)
        if (model.model != Model::none && model.typeKey == typeKey)
            return model.model;
    return Model::none;
}

Model modelForPrefix(const std::string_view prefix)
{
    for (const auto& model : catalogue().models)
        if (model.model != Model::none && model.prefix == prefix)
            return model.model;
    return Model::none;
}

Model modelForProcessor(const std::string_view processorKey)
{
    for (const auto& model : catalogue().models)
        if (model.model != Model::none && model.processorKey == processorKey)
            return model.model;
    return Model::none;
}

Model modelOf(const float stored) noexcept
{
    const auto index = static_cast<int>(std::lround(stored));
    if (index <= 0 || index >= modelCount || info(static_cast<Model>(index)).parameters.empty())
        return Model::none;
    return static_cast<Model>(index);
}

void warmUp(const Model model)
{
    switch (model) {
    case Model::colosse:
    case Model::mosaique:
    case Model::alliage: detail::tables::prepare(); break;
    case Model::nebuleuse: detail::nebuleuseWarmUp(); break;
    case Model::saphir: detail::saphirWarmUp(); break;
    default: break;
    }
}

int parameterCount(const Model model)
{
    return static_cast<int>(info(model).parameters.size());
}

const Parameter& parameter(const Model model, const int index)
{
    const auto& parameters = info(model).parameters;
    if (index < 0 || index >= static_cast<int>(parameters.size()))
        return fallbackParameter();
    return parameters[static_cast<std::size_t>(index)];
}

int parameterIndex(const Model model, const std::string& key)
{
    const auto& indices = catalogue().indices[static_cast<std::size_t>(std::clamp(static_cast<int>(model), 0, modelCount - 1))];
    const auto found = indices.find(key);
    return found == indices.end() ? -1 : found->second;
}

Values defaultValues(const Model model)
{
    Values values {};
    const auto& parameters = info(model).parameters;
    for (std::size_t index = 0; index < parameters.size() && index < values.size(); ++index)
        values[index] = parameters[index].defaultValue;
    return values;
}

float clampValue(const Model model, const int index, const float value)
{
    const auto& setting = parameter(model, index);
    if (!std::isfinite(value))
        return setting.defaultValue;
    auto clamped = std::clamp(value, setting.minimum, setting.maximum);
    if (setting.kind != Kind::continuous)
        clamped = std::round(clamped);
    return clamped;
}

float normalise(const Model model, const int index, const float value)
{
    const auto& setting = parameter(model, index);
    const auto clamped = std::clamp(value, setting.minimum, setting.maximum);
    if (setting.logarithmic && setting.minimum > 0.0f)
        return static_cast<float>(std::log(clamped / setting.minimum) / std::log(setting.maximum / setting.minimum));
    return setting.maximum > setting.minimum ? (clamped - setting.minimum) / (setting.maximum - setting.minimum) : 0.0f;
}

float denormalise(const Model model, const int index, const float normalised)
{
    const auto& setting = parameter(model, index);
    const auto t = std::clamp(normalised, 0.0f, 1.0f);
    if (setting.logarithmic && setting.minimum > 0.0f)
        return static_cast<float>(setting.minimum * std::pow(setting.maximum / setting.minimum, t));
    return setting.minimum + (setting.maximum - setting.minimum) * t;
}

std::string displayValue(const Model model, const int index, const float value, const bool french)
{
    const auto& setting = parameter(model, index);
    char text[64];
    switch (setting.kind) {
    case Kind::choice: {
        const auto at = static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(value)), 0,
                                                            std::max(0, static_cast<int>(setting.choices.size()) - 1)));
        return at < setting.choices.size() ? (french ? setting.choices[at].first : setting.choices[at].second) : std::string {};
    }
    case Kind::toggle: return value >= 0.5f ? (french ? "Oui" : "On") : (french ? "Non" : "Off");
    case Kind::integer:
        // A sync value names its note.
        if (setting.key.ends_with(".sync")) {
            if (const auto* division = noteDivisionFor(value))
                return std::string(division->name);
            return french ? "Libre" : "Free";
        }
        std::snprintf(text, sizeof(text), "%d%s%s", static_cast<int>(std::lround(value)), setting.unit.empty() ? "" : " ", setting.unit.c_str());
        return text;
    case Kind::continuous: break;
    }
    if (setting.unit == "Hz" && value >= 1'000.0f)
        std::snprintf(text, sizeof(text), "%.2f kHz", value / 1'000.0f);
    else if (setting.unit == "Hz")
        std::snprintf(text, sizeof(text), value < 10.0f ? "%.2f Hz" : value < 100.0f ? "%.1f Hz" : "%.0f Hz", value);
    else if (setting.unit == "s" && value < 1.0f)
        std::snprintf(text, sizeof(text), value < 0.01f ? "%.1f ms" : "%.0f ms", value * 1'000.0f);
    else if (setting.unit == "s")
        std::snprintf(text, sizeof(text), "%.2f s", value);
    else if (setting.unit == "ct" || setting.unit == "st" || setting.unit == "dB")
        std::snprintf(text, sizeof(text), "%+.1f %s", value, setting.unit.c_str());
    else if (setting.minimum >= 0.0f && setting.maximum <= 1.0f)
        std::snprintf(text, sizeof(text), "%.0f %%", value * 100.0f);
    else if (setting.minimum >= -1.0f && setting.maximum <= 1.0f)
        std::snprintf(text, sizeof(text), "%+.0f %%", value * 100.0f);
    else
        std::snprintf(text, sizeof(text), "%.2f%s%s", value, setting.unit.empty() ? "" : " ", setting.unit.c_str());
    return text;
}

double filterDb(const Model model, const Values& values, const double hz)
{
    if (model == Model::argile)
        return detail::argileFilterDb(values, hz);
    return std::numeric_limits<double>::quiet_NaN();
}

std::vector<float> picture(const Model model, const Values& values, const std::string_view part, const int points)
{
    switch (model) {
    case Model::colosse: return detail::colossePicture(values, part, points);
    case Model::mosaique: return detail::mosaiquePicture(values, part, points);
    case Model::nebuleuse: return detail::nebuleusePicture(values, part, points);
    case Model::opale: return detail::opalePicture(values, part, points);
    case Model::onyx: return detail::onyxPicture(values, part, points);
    case Model::tirette: return detail::tirettePicture(values, part, points);
    case Model::alliage: return detail::alliagePicture(values, part, points);
    case Model::quartz: return detail::quartzPicture(values, part, points);
    case Model::argile: return detail::argilePicture(values, part, points);
    case Model::saphir: return detail::saphirPicture(values, part, points);
    case Model::derive: return detail::derivePicture(values, part, points);
    case Model::bronze: return detail::bronzePicture(values, part, points);
    case Model::jade: return detail::jadePicture(values, part, points);
    case Model::silex: return detail::silexPicture(values, part, points);
    default: return {};
    }
}

// ---- The voice ----------------------------------------------------------------------------------

struct Voice::State {
    // One engine a model, made before any note (nothing allocated to play).
    std::array<std::unique_ptr<detail::Engine>, modelCount> engines;
    Model model { Model::none };
    double expressionPitch {};
    double expressionGain { 1.0 };
    // What the playing engine plays, for an interface to show.
    VoiceLive live;

    State()
    {
        engines[static_cast<std::size_t>(Model::ambre)] = detail::makeAmbre();
        engines[static_cast<std::size_t>(Model::colosse)] = detail::makeColosse();
        engines[static_cast<std::size_t>(Model::mosaique)] = detail::makeMosaique();
        engines[static_cast<std::size_t>(Model::nebuleuse)] = detail::makeNebuleuse();
        engines[static_cast<std::size_t>(Model::opale)] = detail::makeOpale();
        engines[static_cast<std::size_t>(Model::onyx)] = detail::makeOnyx();
        engines[static_cast<std::size_t>(Model::tirette)] = detail::makeTirette();
        engines[static_cast<std::size_t>(Model::alliage)] = detail::makeAlliage();
        engines[static_cast<std::size_t>(Model::quartz)] = detail::makeQuartz();
        engines[static_cast<std::size_t>(Model::argile)] = detail::makeArgile();
        engines[static_cast<std::size_t>(Model::saphir)] = detail::makeSaphir();
        engines[static_cast<std::size_t>(Model::derive)] = detail::makeDerive();
        engines[static_cast<std::size_t>(Model::bronze)] = detail::makeBronze();
        engines[static_cast<std::size_t>(Model::jade)] = detail::makeJade();
        engines[static_cast<std::size_t>(Model::silex)] = detail::makeSilex();
        live.clear();
        for (auto& engine : engines)
            if (engine != nullptr)
                engine->showInto(&live);
    }
    [[nodiscard]] detail::Engine* engine() const noexcept { return engines[static_cast<std::size_t>(model)].get(); }
};

Voice::Voice() : state_(std::make_unique<State>()) {}
Voice::~Voice() = default;
Voice::Voice(Voice&&) noexcept = default;
Voice& Voice::operator=(Voice&&) noexcept = default;

void Voice::prepare(const double sampleRate)
{
    for (auto& engine : state_->engines)
        if (engine != nullptr)
            engine->prepare(sampleRate);
}

void Voice::start(const Model model, const Values& values, const int note, const float velocity, const double glideFrom,
                  const double sampleRate) noexcept
{
    if (auto* playing = state_->engine(); playing != nullptr && state_->model != model)
        playing->kill();
    state_->model = model;
    state_->expressionPitch = 0.0;
    state_->expressionGain = 1.0;
    state_->live.clear();
    if (auto* engine = state_->engine())
        engine->start(values, note, velocity, glideFrom, sampleRate);
}

void Voice::retarget(const Values& values, const int note, const float velocity) noexcept
{
    if (auto* engine = state_->engine())
        engine->retarget(values, note, velocity);
}

void Voice::release() noexcept
{
    if (auto* engine = state_->engine())
        engine->release();
}

void Voice::kill() noexcept
{
    if (auto* engine = state_->engine())
        engine->kill();
}

bool Voice::active() const noexcept
{
    const auto* engine = state_->engine();
    return engine != nullptr && engine->active();
}

bool Voice::releasing() const noexcept
{
    const auto* engine = state_->engine();
    return engine != nullptr && engine->releasing();
}

double Voice::currentPitch() const noexcept
{
    const auto* engine = state_->engine();
    return engine != nullptr ? engine->currentPitch() : -1.0;
}

void Voice::setExpression(const double pitchSemitones, const double gain) noexcept
{
    state_->expressionPitch = pitchSemitones;
    state_->expressionGain = gain;
}

bool Voice::render(const Values& values, const prism::Context& context, float* left, float* right, const int count,
                   const double sampleRate) noexcept
{
    auto* engine = state_->engine();
    if (engine == nullptr || !engine->active())
        return false;
    return engine->render(values, context, left, right, count, sampleRate, state_->expressionPitch, state_->expressionGain);
}

const VoiceLive& Voice::live() const noexcept
{
    return state_->live;
}

} // namespace cydaw::engine::synth
