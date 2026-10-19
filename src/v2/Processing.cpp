#include "Processing.h"

#include "Locations.h"
#include "ModeProcessing.h"
#include "NativeDspGraphState.h"
#include "NativeDspRuntime.h"
#include "SamplerMapFile.h"
#include "SoundModelFile.h"
#include "Widgets.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace cydaw::v2::processing {
namespace {

constexpr auto format = "cydaw-processing";
// The input a chain step's mixer takes the sound on.
constexpr std::uint64_t chainInput = 1;

struct Builtin {
    const char* key;
    const char* french;
    const char* english;
    double minimum, maximum, step, value;
    std::vector<const char*> choices;
};

const std::vector<Builtin>& builtins(const StepType type)
{
    static const std::vector<Builtin> none;
    static const std::vector<Builtin> denoise {
        { "reduction", "Réduction (dB)", "Reduction (dB)", 0.0, 48.0, 0.5, 18.0, {} },
        { "sensitivity", "Sensibilité", "Sensitivity", 0.5, 4.0, 0.05, 1.5, {} },
        { "smoothing", "Lissage", "Smoothing", 0.0, 1.0, 0.01, 0.5, {} },
    };
    static const std::vector<Builtin> hum {
        { "fundamental", "Secteur", "Mains", 0.0, 2.0, 1.0, 0.0, { "Auto", "50 Hz", "60 Hz" } },
        { "harmonics", "Harmoniques", "Harmonics", 1.0, 16.0, 1.0, 6.0, {} },
        { "width", "Finesse (Q)", "Narrowness (Q)", 5.0, 100.0, 1.0, 30.0, {} },
    };
    static const std::vector<Builtin> declick {
        { "sensitivity", "Sensibilité", "Sensitivity", 0.0, 1.0, 0.01, 0.5, {} },
        { "maxClick", "Durée max (ms)", "Longest (ms)", 0.2, 10.0, 0.1, 2.0, {} },
    };
    static const std::vector<Builtin> highpass {
        { "cutoff", "Coupure (Hz)", "Cutoff (Hz)", 10.0, 2'000.0, 1.0, 80.0, {} },
        { "slope", "Pente", "Slope", 0.0, 1.0, 1.0, 0.0, { "12 dB/oct", "24 dB/oct" } },
    };
    static const std::vector<Builtin> lowpass {
        { "cutoff", "Coupure (Hz)", "Cutoff (Hz)", 500.0, 20'000.0, 10.0, 12'000.0, {} },
        { "slope", "Pente", "Slope", 0.0, 1.0, 1.0, 0.0, { "12 dB/oct", "24 dB/oct" } },
    };
    static const std::vector<Builtin> gain { { "db", "Gain (dB)", "Gain (dB)", -24.0, 24.0, 0.1, 0.0, {} } };
    static const std::vector<Builtin> normalise { { "peak", "Crête (dBFS)", "Peak (dBFS)", -24.0, 0.0, 0.1, -1.0, {} } };
    static const std::vector<Builtin> trim {
        { "threshold", "Seuil (dBFS)", "Threshold (dBFS)", -90.0, -20.0, 1.0, -60.0, {} },
        { "keep", "Marge (ms)", "Margin (ms)", 0.0, 500.0, 1.0, 20.0, {} },
    };
    switch (type) {
    case StepType::denoise: return denoise;
    case StepType::hum: return hum;
    case StepType::declick: return declick;
    case StepType::highpass: return highpass;
    case StepType::lowpass: return lowpass;
    case StepType::gain: return gain;
    case StepType::normalise: return normalise;
    case StepType::trim: return trim;
    case StepType::chain:
    case StepType::code: return none;
    }
    return none;
}

double settingOf(const Step& step, const juce::String& key, const double fallback)
{
    const auto it = step.settings.find(key);
    return it != step.settings.end() ? it->second : fallback;
}

// A value: the run's (by its id), else the step's own, else the default.
double valueOf(const Values& values, const int index, const Step& step, const juce::String& key, const double fallback)
{
    if (const auto it = values.find(juce::String(index) + "/" + key); it != values.end())
        return it->second;
    return settingOf(step, key, fallback);
}

const char* const starterCode = "desc:My effect\n"
                                "slider1:0<-24,24,0.1>Gain (dB)\n"
                                "\n"
                                "@slider\n"
                                "g = 10 ^ (slider1 / 20);\n"
                                "\n"
                                "@sample\n"
                                "spl0 *= g;\n"
                                "spl1 *= g;\n";

} // namespace

const char* keyOf(const StepType type)
{
    switch (type) {
    case StepType::chain: return "chain";
    case StepType::code: return "code";
    case StepType::denoise: return "denoise";
    case StepType::hum: return "hum";
    case StepType::declick: return "declick";
    case StepType::highpass: return "highpass";
    case StepType::lowpass: return "lowpass";
    case StepType::gain: return "gain";
    case StepType::normalise: return "normalise";
    case StepType::trim: return "trim";
    }
    return "gain";
}

std::optional<StepType> stepTypeOf(const juce::String& key)
{
    for (const auto type : { StepType::chain, StepType::code, StepType::denoise, StepType::hum, StepType::declick,
                             StepType::highpass, StepType::lowpass, StepType::gain, StepType::normalise, StepType::trim })
        if (key == keyOf(type))
            return type;
    return std::nullopt;
}

juce::String nameOf(const StepType type)
{
    switch (type) {
    case StepType::chain: return ui::Ls("Chaîne d'effets", "Effect chain");
    case StepType::code: return ui::Ls("Code (JSFX)", "Code (JSFX)");
    case StepType::denoise: return ui::Ls("Débruitage", "Noise reduction");
    case StepType::hum: return ui::Ls("Anti-ronflement", "Hum removal");
    case StepType::declick: return ui::Ls("Anti-clics", "Click removal");
    case StepType::highpass: return ui::Ls("Passe-haut", "High-pass");
    case StepType::lowpass: return ui::Ls("Passe-bas", "Low-pass");
    case StepType::gain: return ui::Ls("Gain", "Gain");
    case StepType::normalise: return ui::Ls("Normaliser", "Normalise");
    case StepType::trim: return ui::Ls("Couper les silences", "Trim silence");
    }
    return {};
}

juce::String hintOf(const StepType type)
{
    switch (type) {
    case StepType::chain:
        return ui::Ls("Des effets de CyDAW sur une piste à elle (égaliseur, compresseur, réverbe…), réglés comme sur une piste.",
                      "CyDAW's effects on a track of its own (equaliser, compressor, reverb…), set as on a track.");
    case StepType::code:
        return ui::Ls("Un effet écrit en JSFX (le langage d'effets de REAPER) : @init, @slider, @sample (spl0, spl1).",
                      "An effect written in JSFX (REAPER's effect language): @init, @slider, @sample (spl0, spl1).");
    case StepType::denoise:
        return ui::Ls("Le bruit de fond (souffle, ventilation) mesuré dans les moments les plus calmes et retiré partout.",
                      "The background noise (hiss, fans) measured in the quietest moments and taken out everywhere.");
    case StepType::hum:
        return ui::Ls("Le ronflement du secteur (50 ou 60 Hz) et ses harmoniques, coupés au plus étroit.",
                      "Mains hum (50 or 60 Hz) and its harmonics, notched out as narrowly as possible.");
    case StepType::declick:
        return ui::Ls("Les clics et craquements (vinyle, coupures) redessinés à partir du son autour.",
                      "Clicks and crackle (vinyl, dropouts) redrawn from the sound around them.");
    case StepType::highpass: return ui::Ls("Coupe les graves sous la fréquence.", "Cuts the lows under the frequency.");
    case StepType::lowpass: return ui::Ls("Coupe les aigus au-dessus de la fréquence.", "Cuts the highs above the frequency.");
    case StepType::gain: return ui::Ls("Plus fort ou moins fort.", "Louder or softer.");
    case StepType::normalise: return ui::Ls("La crête amenée au niveau choisi.", "The peak brought to the chosen level.");
    case StepType::trim:
        return ui::Ls("Le silence au début et à la fin coupé (toujours fait en dernier).",
                      "Silence at the start and the end cut (always done last).");
    }
    return {};
}

bool rings(const StepType type) { return type == StepType::chain || type == StepType::code; }

// ---- Files ----

namespace {

// A graph with the classes its cards use (and those they use), and its own
// inline ones: the rest is CyDAW's library, put back when it is restored.
juce::var withUsedModules(const juce::var& graphState)
{
    const auto* graph = graphState.getDynamicObject();
    const auto* modules = graph != nullptr ? graph->getProperty("modules").getArray() : nullptr;
    if (modules == nullptr)
        return graphState;
    std::set<juce::String> used;
    const auto addTypes = [&used](const juce::var& nodes) {
        if (const auto* list = nodes.getArray())
            for (const auto& node : *list)
                used.insert(node.getProperty("type", {}).toString());
    };
    addTypes(graph->getProperty("nodes"));
    juce::Array<juce::var> kept;
    std::set<juce::String> keptKeys;
    for (auto grew = true; grew;) {
        grew = false;
        for (const auto& module : *modules) {
            const auto key = module.getProperty("key", {}).toString();
            if (keptKeys.count(key) > 0)
                continue;
            if (static_cast<bool>(module.getProperty("inline", false)) || used.count(key) > 0) {
                kept.add(module);
                keptKeys.insert(key);
                addTypes(module.getProperty("nodes", {}));
                used.insert(module.getProperty("parentClassKey", {}).toString());
                grew = true;
            }
        }
    }
    auto shallow = juce::DynamicObject::Ptr(new juce::DynamicObject());
    for (const auto& property : graph->getProperties())
        shallow->setProperty(property.name, property.value);
    shallow->setProperty("modules", juce::var(kept));
    return juce::var(shallow.get());
}

// A chain step's mixer, its graphs with the classes they use only.
juce::var compactChain(const juce::var& chain)
{
    const auto* object = chain.getDynamicObject();
    if (object == nullptr)
        return chain;
    auto copy = juce::DynamicObject::Ptr(new juce::DynamicObject());
    for (const auto& property : object->getProperties())
        copy->setProperty(property.name, property.value);
    if (const auto* tracks = object->getProperty("tracks").getArray()) {
        juce::Array<juce::var> compact;
        for (const auto& track : *tracks) {
            const auto* trackObject = track.getDynamicObject();
            if (trackObject == nullptr) {
                compact.add(track);
                continue;
            }
            auto trackCopy = juce::DynamicObject::Ptr(new juce::DynamicObject());
            for (const auto& property : trackObject->getProperties())
                trackCopy->setProperty(property.name, property.value);
            trackCopy->setProperty("graph", withUsedModules(trackObject->getProperty("graph")));
            compact.add(juce::var(trackCopy.get()));
        }
        copy->setProperty("tracks", compact);
    }
    if (object->hasProperty("master"))
        copy->setProperty("master", withUsedModules(object->getProperty("master")));
    return juce::var(copy.get());
}

} // namespace

juce::var toJson(const Processing& processing)
{
    auto* root = new juce::DynamicObject();
    root->setProperty("format", format);
    root->setProperty("version", 1);
    root->setProperty("kind", processing.kind == Kind::midi ? "midi" : "audio");
    root->setProperty("name", processing.name);
    root->setProperty("description", processing.description);
    if (processing.kind == Kind::midi) {
        root->setProperty("script", processing.script);
    } else {
        juce::Array<juce::var> steps;
        for (const auto& step : processing.steps) {
            auto* item = new juce::DynamicObject();
            item->setProperty("type", keyOf(step.type));
            item->setProperty("enabled", step.enabled);
            auto* settings = new juce::DynamicObject();
            for (const auto& [key, value] : step.settings)
                settings->setProperty(key, value);
            item->setProperty("settings", juce::var(settings));
            if (step.type == StepType::code)
                item->setProperty("code", step.code);
            if (step.type == StepType::chain) {
                item->setProperty("chain", compactChain(step.chain));
                juce::Array<juce::var> published;
                for (const auto& knob : step.published) {
                    auto* entry = new juce::DynamicObject();
                    entry->setProperty("binding", knob.binding);
                    entry->setProperty("label", knob.label);
                    entry->setProperty("min", knob.minimum);
                    entry->setProperty("max", knob.maximum);
                    published.add(juce::var(entry));
                }
                item->setProperty("published", published);
            }
            steps.add(juce::var(item));
        }
        root->setProperty("steps", steps);
        auto* tail = new juce::DynamicObject();
        tail->setProperty("allow", processing.tail.allow);
        tail->setProperty("thresholdDb", processing.tail.thresholdDb);
        tail->setProperty("holdSeconds", processing.tail.holdSeconds);
        tail->setProperty("maxSeconds", processing.tail.maxSeconds);
        tail->setProperty("fadeMs", processing.tail.fadeMs);
        root->setProperty("tail", juce::var(tail));
    }
    return juce::var(root);
}

std::optional<Processing> fromJson(const juce::var& json, juce::String& error)
{
    if (json.getProperty("format", {}).toString() != format) {
        error = "not a CyDAW processing";
        return std::nullopt;
    }
    Processing processing;
    processing.kind = json.getProperty("kind", "audio").toString() == "midi" ? Kind::midi : Kind::audio;
    processing.name = json.getProperty("name", {}).toString();
    processing.description = json.getProperty("description", {}).toString();
    processing.script = json.getProperty("script", {}).toString();
    if (const auto* steps = json.getProperty("steps", {}).getArray())
        for (const auto& item : *steps) {
            const auto type = stepTypeOf(item.getProperty("type", {}).toString());
            if (!type)
                continue;
            Step step;
            step.type = *type;
            step.enabled = static_cast<bool>(item.getProperty("enabled", true));
            if (const auto* settings = item.getProperty("settings", {}).getDynamicObject())
                for (const auto& [key, value] : settings->getProperties())
                    step.settings[key.toString()] = static_cast<double>(value);
            step.code = item.getProperty("code", {}).toString();
            step.chain = item.getProperty("chain", {});
            if (const auto* published = item.getProperty("published", {}).getArray())
                for (const auto& knob : *published)
                    step.published.push_back({ knob.getProperty("binding", {}).toString(), knob.getProperty("label", {}).toString(),
                                               static_cast<double>(knob.getProperty("min", 0.0)),
                                               static_cast<double>(knob.getProperty("max", 1.0)) });
            processing.steps.push_back(std::move(step));
        }
    if (const auto tail = json.getProperty("tail", {}); tail.isObject()) {
        processing.tail.allow = static_cast<bool>(tail.getProperty("allow", true));
        processing.tail.thresholdDb = static_cast<float>(static_cast<double>(tail.getProperty("thresholdDb", -60.0)));
        processing.tail.holdSeconds = static_cast<float>(static_cast<double>(tail.getProperty("holdSeconds", 0.3)));
        processing.tail.maxSeconds = std::clamp(static_cast<float>(static_cast<double>(tail.getProperty("maxSeconds", 10.0))), 0.0f, 120.0f);
        processing.tail.fadeMs = static_cast<float>(static_cast<double>(tail.getProperty("fadeMs", 30.0)));
    }
    if (processing.name.trim().isEmpty())
        processing.name = ui::Ls("Sans nom", "Untitled");
    return processing;
}

std::optional<Processing> load(const juce::File& file, juce::String& error)
{
    auto processing = fromJson(juce::JSON::parse(file), error);
    if (processing)
        processing->file = file;
    return processing;
}

juce::Result save(const Processing& processing, const juce::File& file)
{
    (void) file.getParentDirectory().createDirectory();
    if (!file.replaceWithText(juce::JSON::toString(toJson(processing))))
        return juce::Result::fail(ui::Ls("Impossible d'écrire ", "Cannot write ") + file.getFullPathName());
    return juce::Result::ok();
}

juce::File userFolder() { return locations::folder(Location::processings); }
juce::File factoryFolder() { return app::factoryContentFolder().getChildFile("Processings"); }

std::vector<Processing> library(const std::optional<Kind> kind)
{
    std::vector<Processing> all;
    for (const auto& [folder, factory] : { std::pair { factoryFolder(), true }, std::pair { userFolder(), false } }) {
        if (!folder.isDirectory())
            continue;
        auto files = folder.findChildFiles(juce::File::findFiles, false, juce::String("*") + fileExtension);
        files.sort();
        for (const auto& file : files) {
            juce::String error;
            if (auto processing = load(file, error); processing && (!kind || processing->kind == *kind)) {
                processing->factory = factory;
                all.push_back(std::move(*processing));
            }
        }
    }
    return all;
}

std::optional<Processing> find(const juce::String& name, const std::optional<Kind> kind)
{
    // The person's first: theirs may hide one of the factory's with its name.
    auto all = library(kind);
    for (auto it = all.rbegin(); it != all.rend(); ++it)
        if (it->name.equalsIgnoreCase(name) || it->file.getFileNameWithoutExtension().equalsIgnoreCase(name))
            return *it;
    return std::nullopt;
}

// ---- Parameters ----

std::vector<Parameter> builtinSettings(const StepType type)
{
    std::vector<Parameter> parameters;
    for (const auto& item : builtins(type)) {
        Parameter parameter;
        parameter.id = item.key;
        parameter.label = ui::Ls(item.french, item.english);
        parameter.minimum = item.minimum;
        parameter.maximum = item.maximum;
        parameter.step = item.step;
        parameter.value = item.value;
        for (const auto* choice : item.choices)
            parameter.choices.add(juce::String::fromUTF8(choice));
        parameters.push_back(parameter);
    }
    return parameters;
}

Step newStep(const StepType type)
{
    Step step;
    step.type = type;
    for (const auto& item : builtins(type))
        step.settings[item.key] = item.value;
    if (type == StepType::code) {
        step.code = starterCode;
        for (const auto& slider : inspectJsfx(step.code).sliders)
            step.settings["slider" + juce::String(slider.index + 1)] = slider.defaultValue;
    }
    if (type == StepType::chain) {
        ModeProcessing mixer;
        mixer.ensureInput(chainInput, ui::Ls("Son", "Sound"));
        step.chain = mixer.serialise();
    }
    return step;
}

std::vector<Parameter> parametersOf(const Processing& processing)
{
    std::vector<Parameter> parameters;
    if (processing.kind == Kind::midi) {
        for (const auto& setting : noteScriptSettings(processing.script))
            parameters.push_back({ "param" + juce::String(setting.index + 1), setting.name, setting.minimum, setting.maximum,
                                   setting.step, setting.defaultValue, setting.choices });
        return parameters;
    }
    std::map<StepType, int> seen;
    for (std::size_t i = 0; i < processing.steps.size(); ++i) {
        const auto& step = processing.steps[i];
        if (!step.enabled)
            continue;
        const auto number = ++seen[step.type];
        auto prefix = nameOf(step.type);
        if (number > 1)
            prefix << " " << number;
        const auto id = juce::String(static_cast<int>(i)) + "/";
        if (step.type == StepType::code) {
            for (const auto& slider : inspectJsfx(step.code).sliders) {
                const auto key = "slider" + juce::String(slider.index + 1);
                parameters.push_back({ id + key, prefix + " \xc2\xb7 " + slider.name, slider.minimum, slider.maximum, slider.step,
                                       settingOf(step, key, slider.defaultValue), slider.choices });
            }
        } else if (step.type == StepType::chain) {
            for (const auto& knob : step.published)
                parameters.push_back({ id + knob.binding, prefix + " \xc2\xb7 " + knob.label, knob.minimum, knob.maximum, 0.0,
                                       settingOf(step, knob.binding, 0.5 * (knob.minimum + knob.maximum)), {} });
        } else {
            for (auto parameter : builtinSettings(step.type)) {
                parameter.value = settingOf(step, parameter.id, parameter.value);
                parameter.label = prefix + " \xc2\xb7 " + parameter.label;
                parameter.id = id + parameter.id;
                parameters.push_back(parameter);
            }
        }
    }
    return parameters;
}

// ---- Running ----

Prepared prepare(const Processing& processing, const Values& values)
{
    Prepared prepared;
    prepared.processing = processing;
    prepared.values = values;
    prepared.chains.resize(processing.steps.size());
    for (std::size_t i = 0; i < processing.steps.size(); ++i) {
        const auto& step = processing.steps[i];
        if (!step.enabled || step.type != StepType::chain || !step.chain.isObject())
            continue;
        ModeProcessing mixer;
        mixer.restore(step.chain);
        mixer.ensureInput(chainInput, ui::Ls("Son", "Sound"));
        for (const auto& knob : step.published) {
            auto* request = new juce::DynamicObject();
            request->setProperty("op", "parameter");
            request->setProperty("inputId", static_cast<juce::int64>(chainInput));
            request->setProperty("binding", knob.binding);
            request->setProperty("value", valueOf(values, static_cast<int>(i), step, knob.binding, 0.5 * (knob.minimum + knob.maximum)));
            (void) mixer.command(juce::var(request));
        }
        prepared.chains[i] = mixer.inputRuntimeState(chainInput);
    }
    return prepared;
}

namespace {

juce::Result renderChain(const juce::var& state, engine::repair::Channels& channels, const double rate, const std::int64_t frames,
                         const std::atomic<bool>* cancel)
{
    if (!state.isObject())
        return juce::Result::ok(); // an empty chain: the sound as it is
    juce::String error;
    auto native = app::restoreNativeDspGraphState(state.getProperty("native", {}), error);
    if (!native)
        return juce::Result::fail(ui::Ls("La chaîne ne se relit pas : ", "The chain does not restore: ") + error);
    app::attachImpulses(*native, state.getProperty("editor", {}));
    constexpr int block = 1'024;
    app::NativeDspRuntime chain;
    chain.prepare(rate, block, 2);
    if (const auto compiled = chain.setGraph(*native); compiled.failed())
        return juce::Result::fail(ui::Ls("La chaîne ne se compile pas : ", "The chain does not compile: ") + compiled.getErrorMessage());
    const auto latency = std::max(0, chain.latencySamples());
    const auto length = static_cast<std::int64_t>(channels.front().size());
    engine::repair::Channels rendered(2, std::vector<float>(static_cast<std::size_t>(frames), 0.0f));
    juce::AudioBuffer<float> buffer(2, block);
    const auto total = frames + latency;
    for (std::int64_t at = 0; at < total; at += block) {
        if (cancel != nullptr && cancel->load())
            return juce::Result::fail("cancelled");
        const auto n = static_cast<int>(std::min<std::int64_t>(block, total - at));
        for (int c = 0; c < 2; ++c) {
            auto* data = buffer.getWritePointer(c);
            std::fill_n(data, block, 0.0f);
            const auto& source = channels[std::min<std::size_t>(static_cast<std::size_t>(c), channels.size() - 1)];
            for (int i = 0; i < n; ++i)
                if (at + i < length)
                    data[i] = source[static_cast<std::size_t>(at + i)];
        }
        (void) chain.process(buffer, 0, n);
        for (int i = 0; i < n; ++i) {
            const auto out = at + i - latency;
            if (out >= 0 && out < frames)
                for (int c = 0; c < 2; ++c)
                    rendered[static_cast<std::size_t>(c)][static_cast<std::size_t>(out)] = buffer.getSample(c, i);
        }
    }
    channels = std::move(rendered);
    return juce::Result::ok();
}

} // namespace

AudioResult renderAudio(const Prepared& prepared, engine::repair::Channels channels, const double rate,
                        const std::function<void(double)>& progress, const std::atomic<bool>* cancel)
{
    namespace repair = engine::repair;
    AudioResult result;
    const auto& processing = prepared.processing;
    if (channels.empty() || channels.front().empty()) {
        result.error = ui::Ls("Pas de son", "No sound");
        return result;
    }
    const auto mono = channels.size() == 1;
    if (mono)
        channels.push_back(channels.front());
    channels.resize(2);
    const auto source = static_cast<std::int64_t>(channels.front().size());
    const auto ringing = std::any_of(processing.steps.begin(), processing.steps.end(),
                                     [](const Step& step) { return step.enabled && rings(step.type); });
    const auto tail = ringing && processing.tail.allow ? static_cast<std::int64_t>(processing.tail.maxSeconds * rate) : 0;
    const auto frames = source + tail;
    for (auto& channel : channels)
        channel.resize(static_cast<std::size_t>(frames), 0.0f);
    const auto enabled = static_cast<double>(std::max<std::size_t>(1, processing.steps.size()));
    std::vector<const Step*> trims;
    for (std::size_t i = 0; i < processing.steps.size(); ++i) {
        if (cancel != nullptr && cancel->load()) {
            result.error = "cancelled";
            return result;
        }
        if (progress)
            progress(static_cast<double>(i) / enabled);
        const auto& step = processing.steps[i];
        if (!step.enabled)
            continue;
        const auto index = static_cast<int>(i);
        const auto value = [&](const char* key, const double fallback) {
            return valueOf(prepared.values, index, step, key, fallback);
        };
        switch (step.type) {
        case StepType::chain:
            if (const auto done = renderChain(prepared.chains[i], channels, rate, frames, cancel); done.failed()) {
                result.error = done.getErrorMessage();
                return result;
            }
            break;
        case StepType::code: {
            std::map<int, double> sliders;
            for (const auto& [key, number] : step.settings)
                if (key.startsWith("slider"))
                    sliders[key.substring(6).getIntValue() - 1] = valueOf(prepared.values, index, step, key, number);
            for (const auto& [id, number] : prepared.values)
                if (id.startsWith(juce::String(index) + "/slider"))
                    sliders[id.fromFirstOccurrenceOf("/slider", false, false).getIntValue() - 1] = number;
            if (const auto done = renderJsfx(step.code, sliders, channels, rate, frames, cancel); done.failed()) {
                result.error = "JSFX: " + done.getErrorMessage();
                return result;
            }
            break;
        }
        case StepType::denoise:
            repair::denoise(channels, rate, { static_cast<float>(value("reduction", 18.0)), static_cast<float>(value("sensitivity", 1.5)),
                                              static_cast<float>(value("smoothing", 0.5)) });
            break;
        case StepType::hum: {
            const auto choice = static_cast<int>(std::lround(value("fundamental", 0.0)));
            const auto found = repair::removeHum(channels, rate, { choice == 1 ? 50.0f : choice == 2 ? 60.0f : 0.0f,
                                                                    static_cast<int>(std::lround(value("harmonics", 6.0))),
                                                                    static_cast<float>(value("width", 30.0)) });
            result.notes.add(ui::Ls("ronflement : ", "hum: ") + juce::String(found, 0) + " Hz");
            break;
        }
        case StepType::declick: {
            const auto mended = repair::declick(channels, rate, { static_cast<float>(value("sensitivity", 0.5)),
                                                                   static_cast<float>(value("maxClick", 2.0)) });
            result.notes.add(ui::Ls("clics : ", "clicks: ") + juce::String(mended));
            break;
        }
        case StepType::highpass:
            repair::highpass(channels, rate, static_cast<float>(value("cutoff", 80.0)), value("slope", 0.0) >= 0.5 ? 4 : 2);
            break;
        case StepType::lowpass:
            repair::lowpass(channels, rate, static_cast<float>(value("cutoff", 12'000.0)), value("slope", 0.0) >= 0.5 ? 4 : 2);
            break;
        case StepType::gain: repair::gain(channels, static_cast<float>(value("db", 0.0))); break;
        case StepType::normalise: repair::normalise(channels, static_cast<float>(value("peak", -1.0))); break;
        case StepType::trim: trims.push_back(&step); break;
        }
    }
    // Where it ends: its own length, or where its tail falls quiet.
    auto end = source;
    if (tail > 0) {
        end = repair::tailEnd(channels, source, rate, processing.tail, &result.endless);
        result.tailFrames = end - source;
    }
    repair::cutWithFade(channels, end, rate, end > source ? processing.tail.fadeMs : 0.0f);
    for (const auto* step : trims) {
        const auto index = static_cast<int>(step - processing.steps.data());
        result.trimmedStart += repair::trimSilence(channels, rate,
                                                   static_cast<float>(valueOf(prepared.values, index, *step, "threshold", -60.0)),
                                                   static_cast<float>(valueOf(prepared.values, index, *step, "keep", 20.0)));
    }
    // Mono in, mono out when nothing set the sides apart.
    if (mono && channels[0] == channels[1])
        channels.resize(1);
    if (progress)
        progress(1.0);
    result.ok = true;
    result.channels = std::move(channels);
    return result;
}

juce::Result readAudioFile(const juce::File& file, engine::repair::Channels& channels, double& rate)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return juce::Result::fail(ui::Ls("Fichier son illisible : ", "Unreadable sound file: ") + file.getFileName());
    if (reader->lengthInSamples > static_cast<juce::int64>(reader->sampleRate * 60.0 * 30.0))
        return juce::Result::fail(ui::Ls("Son trop long (30 minutes au plus)", "Sound too long (30 minutes at most)"));
    const auto frames = static_cast<int>(reader->lengthInSamples);
    const auto count = static_cast<int>(std::clamp(reader->numChannels, 1u, 2u));
    juce::AudioBuffer<float> buffer(count, frames);
    reader->read(&buffer, 0, frames, 0, true, count > 1);
    channels.assign(static_cast<std::size_t>(count), std::vector<float>(static_cast<std::size_t>(frames)));
    for (int c = 0; c < count; ++c)
        std::copy_n(buffer.getReadPointer(c), frames, channels[static_cast<std::size_t>(c)].begin());
    rate = reader->sampleRate;
    return juce::Result::ok();
}

juce::Result writeAudioFile(const juce::File& file, const engine::repair::Channels& channels, const double rate)
{
    const auto frames = channels.empty() ? 0 : static_cast<int>(channels.front().size());
    juce::AudioBuffer<float> buffer(std::max(1, static_cast<int>(channels.size())), frames);
    for (int c = 0; c < static_cast<int>(channels.size()); ++c)
        std::copy_n(channels[static_cast<std::size_t>(c)].data(), frames, buffer.getWritePointer(c));
    (void) file.getParentDirectory().createDirectory();
    (void) file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
    std::unique_ptr<juce::AudioFormatWriter> writer(
        stream != nullptr ? wav.createWriterFor(stream.get(), rate, static_cast<unsigned>(buffer.getNumChannels()), 24, {}, 0) : nullptr);
    if (writer == nullptr)
        return juce::Result::fail(ui::Ls("Impossible d'écrire ", "Cannot write ") + file.getFullPathName());
    (void) stream.release();
    return writer->writeFromAudioSampleBuffer(buffer, 0, frames)
               ? juce::Result::ok()
               : juce::Result::fail(ui::Ls("Impossible d'écrire ", "Cannot write ") + file.getFullPathName());
}

NoteScriptResult renderNotes(const Processing& processing, const Values& values, std::vector<ScriptNote> notes, const double tempo,
                             const double clipLength, const std::uint32_t seed)
{
    std::map<int, double> params;
    for (const auto& [id, value] : values)
        if (id.startsWith("param"))
            params[id.substring(5).getIntValue() - 1] = value;
    return runNoteScript(processing.script, std::move(notes), params, tempo, clipLength, seed);
}

} // namespace cydaw::v2::processing
