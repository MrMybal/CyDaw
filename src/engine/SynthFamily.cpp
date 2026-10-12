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
    case Model::mosaique: detail::tables::prepare(); break;
    case Model::nebuleuse: detail::nebuleuseWarmUp(); break;
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

std::vector<float> picture(const Model model, const Values& values, const std::string_view part, const int points)
{
    switch (model) {
    case Model::colosse: return detail::colossePicture(values, part, points);
    case Model::mosaique: return detail::mosaiquePicture(values, part, points);
    case Model::nebuleuse: return detail::nebuleusePicture(values, part, points);
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

    State()
    {
        engines[static_cast<std::size_t>(Model::ambre)] = detail::makeAmbre();
        engines[static_cast<std::size_t>(Model::colosse)] = detail::makeColosse();
        engines[static_cast<std::size_t>(Model::mosaique)] = detail::makeMosaique();
        engines[static_cast<std::size_t>(Model::nebuleuse)] = detail::makeNebuleuse();
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

} // namespace cydaw::engine::synth
