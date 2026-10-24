#include "DeviceOps.h"

#include "SoundModelFile.h"
#include "Widgets.h"

#include <cydaw/engine/PrismSynth.h>
#include <cydaw/engine/Convolver.h>
#include <cydaw/engine/WavetableShape.h>

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cydaw::v2::device_ops {

juce::String parameterKeyOf(const juce::String& binding, const std::uint64_t nodeId)
{
    const auto prefix = "node." + juce::String(static_cast<juce::int64>(nodeId)) + ".";
    return binding.startsWith(prefix) ? binding.substring(prefix.length()) : binding;
}

juce::var presetState(const app::NodeGraphComponent& graph, const std::uint64_t nodeId)
{
    for (const auto& node : graph.editorNodes()) {
        if (node.id != nodeId)
            continue;
        auto* knobs = new juce::DynamicObject();
        for (const auto& parameter : node.parameters)
            knobs->setProperty(parameterKeyOf(parameter.binding, nodeId), parameter.value);
        auto* state = new juce::DynamicObject();
        state->setProperty("type", node.typeKey);
        state->setProperty("parameters", juce::var(knobs));
        if (node.typeKey == "sampler")
            if (const auto map = graph.nodeSamplerJson(nodeId); map.isObject())
                state->setProperty("sampler", map);
        // A sampler's or a Batterie's controllers (its CONTRÔLES and MICROS:
        // the close, overhead and room microphones' balance), those set.
        if (const auto controls = graph.editorSamplerControls(nodeId))
            for (std::size_t number = 0; number < controls->controllers.size(); ++number)
                if (controls->controllers[number] >= 0.0f)
                    knobs->setProperty("sampler.cc" + juce::String(static_cast<int>(number)), controls->controllers[number]);
        if (node.typeKey == "sound-model")
            if (const auto model = graph.nodeSoundModelJson(nodeId); model.isObject())
                state->setProperty("soundModel", model);
        if (node.typeKey == "wavetable") {
            juce::Array<juce::var> harmonics;
            for (const auto& frame : graph.editorNodeWavetable(nodeId).harmonics)
                for (const auto amplitude : frame)
                    harmonics.add(amplitude);
            state->setProperty("wavetable", harmonics);
        }
        if (node.typeKey == "prism-synth") {
            if (const auto tables = graph.nodePrismTablesJson(nodeId); tables.isObject())
                state->setProperty("prismTables", tables);
            // Every value of the sound, its drawn LFOs' points too, which
            // are not knobs.
            if (const auto values = graph.editorPrismValues(nodeId))
                for (int index = 0; index < engine::prism::parameterCount; ++index)
                    knobs->setProperty("prism." + juce::String(engine::prism::parameter(index).key),
                                       (*values)[static_cast<std::size_t>(index)]);
        }
        if (node.typeKey == "cab-sim" || node.typeKey == "empreinte")
            if (const auto response = graph.nodeImpulseJson(nodeId); response.isObject())
                state->setProperty("impulse", response);
        return juce::var(state);
    }
    return {};
}

bool applyPreset(app::NodeGraphComponent& graph, const std::uint64_t nodeId, const juce::var& state, juce::String& error,
                 const std::function<bool(int kit, juce::String& error)>& setBatterieKit)
{
    const auto nodes = graph.editorNodes();
    const auto node = std::ranges::find_if(nodes, [nodeId](const auto& item) { return item.id == nodeId; });
    if (node == nodes.end()) {
        error = ui::Ls("Périphérique introuvable", "No such device");
        return false;
    }
    if (const auto type = state.getProperty("type", {}).toString(); type.isNotEmpty() && type != node->typeKey) {
        error = ui::Ls("Ce préréglage est celui d'un autre périphérique : ", "This preset is another device's: ") + type;
        return false;
    }
    // The card's own assets first (each its edit), then its knobs, which the
    // graph gathers into one undo step.
    if (const auto map = state.getProperty("sampler", {}); map.isObject() && !graph.setNodeSampler(nodeId, map, error))
        return false;
    if (const auto model = state.getProperty("soundModel", {}); model.isObject() && !graph.setNodeSoundModel(nodeId, model, error))
        return false;
    if (const auto* harmonics = state.getProperty("wavetable", {}).getArray();
        harmonics != nullptr && harmonics->size() == static_cast<int>(engine::WavetableShape::frameCount * engine::WavetableShape::harmonicCount)) {
        engine::WavetableShape shape;
        auto index = 0;
        for (auto& frame : shape.harmonics)
            for (auto& amplitude : frame)
                amplitude = static_cast<float>(static_cast<double>((*harmonics)[index++]));
        (void) graph.editorSetNodeWavetable(nodeId, shape);
    }
    // A cabinet preset brings its response, or none (the models then); an
    // Empreinte preset its place's, or none (its built-in spaces then).
    if (node->typeKey == "cab-sim" || node->typeKey == "empreinte") {
        const auto response = state.getProperty("impulse", {});
        if ((response.isObject() || graph.nodeImpulseJson(nodeId).isObject())
            && !graph.setNodeImpulse(nodeId, response.isObject() ? response : juce::var(), error))
            return false;
    }
    // A Prisme preset brings its own tables, or none (the factory's then).
    if (node->typeKey == "prism-synth") {
        const auto tables = state.getProperty("prismTables", {});
        if ((tables.isObject() || graph.nodePrismTablesJson(nodeId).isObject())
            && !graph.setNodePrismTables(nodeId, tables.isObject() ? tables : juce::var(), error))
            return false;
    }
    if (node->typeKey == "batterie")
        if (const auto* knobs = state.getProperty("parameters", {}).getDynamicObject())
            for (const auto& parameter : node->parameters)
                if (parameter.binding.endsWith(".batterie.kit.kit")) {
                    const auto key = juce::Identifier(parameterKeyOf(parameter.binding, nodeId));
                    const auto kit = knobs->hasProperty(key) ? static_cast<int>(std::lround(static_cast<double>(knobs->getProperty(key)))) : 0;
                    const auto loaded = graph.nodeSamplerJson(nodeId).getProperty("batterieKit", -1);
                    if (static_cast<int>(loaded) != kit && (!setBatterieKit || !setBatterieKit(kit, error)))
                        return false;
                    break;
                }
    // A sampler's or a Batterie's controllers, once its map or kit is in
    // place: those the preset sets, the others where the map starts them.
    if (graph.editorSamplerControls(nodeId))
        if (const auto* knobs = state.getProperty("parameters", {}).getDynamicObject()) {
            const auto prefix = "node." + juce::String(static_cast<juce::int64>(nodeId)) + ".sampler.cc";
            for (int number = 0; number < 128; ++number) {
                const auto key = juce::Identifier("sampler.cc" + juce::String(number));
                (void) graph.editorSetParameter(prefix + juce::String(number),
                                                knobs->hasProperty(key) ? static_cast<double>(knobs->getProperty(key)) : -1.0);
            }
        }
    // A Prisme sound is whole: each value the preset holds, its drawn LFOs'
    // points too, and the default for one it does not (nothing of the last
    // sound stays: its third oscillator, its spectral morphs).
    if (node->typeKey == "prism-synth")
        if (const auto* knobs = state.getProperty("parameters", {}).getDynamicObject()) {
            const auto prefix = "node." + juce::String(static_cast<juce::int64>(nodeId)) + ".prism.";
            for (int index = 0; index < engine::prism::parameterCount; ++index) {
                const auto& setting = engine::prism::parameter(index);
                const auto key = juce::Identifier("prism." + juce::String(setting.key));
                const auto value = knobs->hasProperty(key) ? static_cast<double>(knobs->getProperty(key))
                                                           : static_cast<double>(setting.defaultValue);
                (void) graph.editorSetParameter(prefix + juce::String(setting.key),
                                                engine::prism::clampValue(index, static_cast<float>(value)));
            }
        }
    if (const auto* knobs = state.getProperty("parameters", {}).getDynamicObject())
        for (const auto& parameter : node->parameters) {
            const auto key = juce::Identifier(parameterKeyOf(parameter.binding, nodeId));
            if (knobs->hasProperty(key))
                (void) graph.editorSetParameter(parameter.binding,
                                                std::clamp(static_cast<double>(knobs->getProperty(key)), parameter.minimum,
                                                           parameter.maximum));
        }
    return true;
}

namespace {

// The cycle a wavetable file says it is made of: Serum's "clm " chunk
// ("<!>2048 …"), 0 without one.
int declaredCycleLength(const juce::File& file)
{
    juce::FileInputStream input(file);
    if (!input.openedOk())
        return 0;
    juce::MemoryBlock head;
    (void) input.readIntoMemoryBlock(head, 1 << 16);
    const auto* bytes = static_cast<const char*>(head.getData());
    const auto size = static_cast<int>(head.getSize());
    for (int at = 0; at + 12 < size; ++at)
        if (std::memcmp(bytes + at, "clm ", 4) == 0 && std::memcmp(bytes + at + 8, "<!>", 3) == 0) {
            auto length = 0;
            for (auto digit = at + 11; digit < size && digit < at + 17 && bytes[digit] >= '0' && bytes[digit] <= '9'; ++digit)
                length = length * 10 + (bytes[digit] - '0');
            return length >= 16 && length <= 16'384 ? length : 0;
        }
    return 0;
}

// The period of a sound in its loudest part, by autocorrelation: the
// shortest lag nearly as alike as the best (no octave too low). 0 when no
// pitch stands out.
int detectedPeriod(const std::vector<float>& mono)
{
    constexpr int window = 8'192;
    constexpr int shortest = 16;
    constexpr int longest = 4'096;
    if (static_cast<int>(mono.size()) < window + longest)
        return 0;
    // The loudest stretch: a note's body, not its attack's silence.
    auto best = 0;
    auto bestEnergy = -1.0;
    for (int start = 0; start + window + longest <= static_cast<int>(mono.size()); start += window / 2) {
        auto energy = 0.0;
        for (int i = 0; i < window; ++i)
            energy += static_cast<double>(mono[static_cast<std::size_t>(start + i)]) * mono[static_cast<std::size_t>(start + i)];
        if (energy > bestEnergy) {
            bestEnergy = energy;
            best = start;
        }
    }
    if (bestEnergy <= 1.0e-6)
        return 0;
    const auto* x = mono.data() + best;
    std::vector<double> alike(static_cast<std::size_t>(longest + 1), 0.0);
    auto strongest = 0.0;
    for (int lag = shortest; lag <= longest; ++lag) {
        auto product = 0.0;
        auto a = 0.0;
        auto b = 0.0;
        for (int i = 0; i < window; ++i) {
            product += static_cast<double>(x[i]) * x[i + lag];
            a += static_cast<double>(x[i]) * x[i];
            b += static_cast<double>(x[i + lag]) * x[i + lag];
        }
        alike[static_cast<std::size_t>(lag)] = product / std::sqrt(std::max(1.0e-12, a * b));
        strongest = std::max(strongest, alike[static_cast<std::size_t>(lag)]);
    }
    if (strongest < 0.6)
        return 0;
    for (int lag = shortest + 1; lag < longest; ++lag) {
        const auto value = alike[static_cast<std::size_t>(lag)];
        if (value >= 0.92 * strongest && value >= alike[static_cast<std::size_t>(lag - 1)]
            && value >= alike[static_cast<std::size_t>(lag + 1)])
            return lag;
    }
    return 0;
}

// `length` samples from `start`, read again at 2048 (linear).
std::vector<float> cycleAt(const std::vector<float>& mono, const double start, const double length)
{
    std::vector<float> frame(static_cast<std::size_t>(engine::prism::Wavetable::frameLength));
    const auto last = static_cast<double>(mono.size()) - 1.0;
    for (std::size_t i = 0; i < frame.size(); ++i) {
        const auto position = std::clamp(start + length * static_cast<double>(i) / static_cast<double>(frame.size()), 0.0, last);
        const auto index = static_cast<std::size_t>(position);
        const auto next = std::min(index + 1, mono.size() - 1);
        const auto fraction = static_cast<float>(position - static_cast<double>(index));
        frame[i] = mono[index] + (mono[next] - mono[index]) * fraction;
    }
    return frame;
}


} // namespace

bool importPrismTable(app::NodeGraphComponent& graph, const std::uint64_t nodeId, const int oscillator, const juce::File& file,
                      juce::String& error)
{
    if (!graph.editorPrismValues(nodeId)) {
        error = ui::Ls("Pas de carte Prisme ici", "No Prisme card here");
        return false;
    }
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples < 16 || reader->numChannels == 0) {
        error = ui::Ls("Ce fichier ne se lit pas comme un son : ", "This file does not read as a sound: ") + file.getFileName();
        return false;
    }
    // Four million samples at most: a minute and a half, far more than a table.
    const auto length = static_cast<int>(std::min<juce::int64>(reader->lengthInSamples, 1 << 22));
    const auto channels = std::clamp(static_cast<int>(reader->numChannels), 1, 8);
    juce::AudioBuffer<float> buffer(channels, length);
    (void) reader->read(&buffer, 0, length, 0, true, channels > 1);
    std::vector<float> mono(static_cast<std::size_t>(length), 0.0f);
    for (int channel = 0; channel < channels; ++channel) {
        const auto* samples = buffer.getReadPointer(channel);
        for (int i = 0; i < length; ++i)
            mono[static_cast<std::size_t>(i)] += samples[i] / static_cast<float>(channels);
    }
    constexpr int mostFrames = 256;
    std::vector<std::vector<float>> frames;
    auto cycle = declaredCycleLength(file);
    if (cycle == 0) {
        for (const auto candidate : { 2'048, 1'024, 512, 256, 4'096 })
            if (length % candidate == 0 && length / candidate <= mostFrames) {
                cycle = candidate;
                break;
            }
    }
    if (cycle == 0 && length <= 4'096)
        cycle = length; // one cycle
    if (cycle > 0) {
        // A wavetable: its cycles one after the other (spread when too many).
        const auto count = std::max(1, length / cycle);
        const auto kept = std::min(count, mostFrames);
        for (int frame = 0; frame < kept; ++frame) {
            const auto index = kept > 1 ? static_cast<int>(std::lround(static_cast<double>(frame) * (count - 1) / (kept - 1))) : 0;
            frames.push_back(cycleAt(mono, static_cast<double>(index) * cycle, cycle));
        }
    } else {
        // Any sound: a cycle at its pitch (2048 samples without one) taken at
        // 64 places across it, each starting where the wave rises through 0.
        const auto period = detectedPeriod(mono);
        const auto span = period > 0 ? period : 2'048;
        constexpr int places = 64;
        for (int place = 0; place < places; ++place) {
            auto start = static_cast<int>(static_cast<double>(place) * (length - span - 2) / (places - 1));
            for (auto search = start; search < std::min(start + span, length - span - 1); ++search)
                if (mono[static_cast<std::size_t>(search)] <= 0.0f && mono[static_cast<std::size_t>(search + 1)] > 0.0f) {
                    start = search + 1;
                    break;
                }
            frames.push_back(cycleAt(mono, start, span));
        }
    }
    // A silent file makes no table.
    auto loudest = 0.0f;
    for (const auto& frame : frames)
        for (const auto sample : frame)
            loudest = std::max(loudest, std::abs(sample));
    if (frames.empty() || loudest < 1.0e-5f) {
        error = ui::Ls("Ce son est muet", "This sound is silent");
        return false;
    }
    const auto table = app::prismTableVar(file.getFileNameWithoutExtension(), file.getFileName(),
                                          engine::prism::Wavetable::frameLength, frames);
    if (!graph.setNodePrismTables(nodeId, app::withPrismTable(graph.nodePrismTablesJson(nodeId), oscillator, table), error))
        return false;
    return true;
}

bool clearPrismTable(app::NodeGraphComponent& graph, const std::uint64_t nodeId, const int oscillator)
{
    const auto tables = graph.nodePrismTablesJson(nodeId);
    if (!tables.isObject())
        return false;
    juce::String error;
    return graph.setNodePrismTables(nodeId, app::withPrismTable(tables, oscillator, {}), error);
}

bool loadImpulse(app::NodeGraphComponent& graph, const std::uint64_t nodeId, const juce::File& file, juce::String& error)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples < 4 || reader->numChannels == 0 || reader->sampleRate <= 0.0) {
        error = ui::Ls("Ce fichier ne se lit pas comme un son : ", "This file does not read as a sound: ") + file.getFileName();
        return false;
    }
    // A cabinet's two seconds at most at its own rate, a place's (Empreinte)
    // eight, never more than the project keeps; its first two channels.
    auto room = false;
    for (const auto& node : graph.editorNodes())
        if (node.id == nodeId)
            room = node.typeKey == "empreinte";
    const auto most = std::min(static_cast<juce::int64>(std::ceil(reader->sampleRate * (room ? 8.0 : 2.0))),
                               static_cast<juce::int64>(engine::maximumImpulseSamples) * 4);
    const auto length = static_cast<int>(std::min(reader->lengthInSamples, most));
    const auto channels = std::min(2, static_cast<int>(reader->numChannels));
    juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels), length);
    (void) reader->read(&buffer, 0, length, 0, true, true);
    // Its silences trimmed: before its first sound (a few samples kept) and
    // after its tail falls 80 dB under its peak.
    auto peak = 0.0f;
    for (int channel = 0; channel < channels; ++channel)
        peak = std::max(peak, buffer.getMagnitude(channel, 0, length));
    if (peak < 1.0e-6f) {
        error = ui::Ls("Cette réponse est muette", "This response is silent");
        return false;
    }
    const auto loudAt = [&](const int index, const float threshold) {
        for (int channel = 0; channel < channels; ++channel)
            if (std::abs(buffer.getSample(channel, index)) >= threshold)
                return true;
        return false;
    };
    auto first = 0;
    while (first < length - 1 && !loudAt(first, peak * 0.001f))
        ++first;
    first = std::max(0, first - 8);
    auto last = length - 1;
    while (last > first && !loudAt(last, peak * 0.0001f))
        --last;
    engine::ImpulseResponse response;
    response.name = file.getFileNameWithoutExtension().toStdString();
    response.sampleRate = reader->sampleRate;
    for (int channel = 0; channel < channels; ++channel)
        response.channels.emplace_back(buffer.getReadPointer(channel) + first, buffer.getReadPointer(channel) + last + 1);
    if (!graph.setNodeImpulse(nodeId, app::impulseVar(response, file.getFileName()), error))
        return false;
    return true;
}

} // namespace cydaw::v2::device_ops
