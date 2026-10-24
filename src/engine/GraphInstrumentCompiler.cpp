#include <cydaw/engine/GraphInstrumentCompiler.h>
#include <cydaw/engine/EffectCatalogue.h>
#include <cydaw/engine/MidiEffects.h>
#include <cydaw/graph/SoundSources.h>

#include <cydaw/graph/PrimitiveRegistry.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace cydaw::engine {
namespace {

using graph::GraphDefinition;
using graph::GraphNode;
using graph::NodeId;
using graph::PortId;

struct Endpoint {
    std::uint64_t nodeId {};
    PortId portId {};
};

struct ParameterTarget {
    std::uint64_t nodeId {};
    std::string key;
};

struct Expansion {
    std::unordered_map<PortId, std::vector<Endpoint>> inputs;
    std::unordered_map<PortId, std::vector<Endpoint>> outputs;
    std::unordered_map<std::string, std::vector<ParameterTarget>> parameters;
    // Wires straight across a definition, its input port to its output port.
    std::vector<std::pair<PortId, PortId>> passthroughs;
};

struct FlatNode {
    std::uint64_t id {};
    std::string processorKey;
    std::unordered_map<std::string, double> parameters;
};

struct FlatConnection {
    Endpoint source;
    Endpoint destination;
};

void addIssue(graph::ValidationResult& validation,
              const graph::ValidationCode code, std::string message,
              const std::optional<NodeId> nodeId = {})
{
    validation.issues.push_back({ graph::ValidationSeverity::error, code,
                                  std::move(message), nodeId, {} });
}

double parameterValue(const GraphNode& node,
                      const graph::NodeParameter& parameter)
{
    const auto overrideValue = node.parameterOverrides.find(parameter.key);
    return overrideValue == node.parameterOverrides.end()
               ? parameter.defaultValue : overrideValue->second;
}

class Flattener final {
public:
    Flattener(const graph::GraphLibrary& libraryValue,
              graph::ValidationResult& validationValue)
        : library(libraryValue), validation(validationValue),
          registry(graph::factoryPrimitiveRegistry())
    {
        for (const auto& reference : library.all())
            if (const auto* definition = library.resolve(reference))
                for (const auto& node : definition->nodes)
                    nextId = std::max(nextId, node.id + 1);
    }

    Expansion expand(const GraphDefinition& definition,
                     const bool preservePrimitiveIds)
    {
        std::unordered_map<NodeId, Expansion> expandedNodes;
        for (const auto& node : definition.nodes)
            expandedNodes.emplace(
                node.id, node.kind == graph::GraphNodeKind::primitive
                             ? expandPrimitive(node, preservePrimitiveIds)
                             : expandInstance(node));

        Expansion expansion;
        for (const auto& connection : definition.connections) {
            if (connection.source.nodeId == graph::boundaryNodeId
                && connection.destination.nodeId == graph::boundaryNodeId) {
                expansion.passthroughs.emplace_back(connection.source.portId, connection.destination.portId);
                continue;
            }
            if (connection.source.nodeId == graph::boundaryNodeId) {
                append(expansion.inputs[connection.source.portId],
                       endpoint(expandedNodes, connection.destination, true));
                continue;
            }
            if (connection.destination.nodeId == graph::boundaryNodeId) {
                append(expansion.outputs[connection.destination.portId],
                       endpoint(expandedNodes, connection.source, false));
                continue;
            }
            const auto sources = endpoint(expandedNodes, connection.source,
                                          false);
            const auto destinations = endpoint(expandedNodes,
                                               connection.destination, true);
            for (const auto& source : sources)
                for (const auto& destination : destinations)
                    connections.push_back({ source, destination });
        }
        for (const auto& parameter : definition.parameters) {
            const auto owner = expandedNodes.find(parameter.targetNodeId);
            if (owner == expandedNodes.end())
                continue;
            const auto targets = owner->second.parameters.find(
                parameter.targetParameterKey);
            if (targets != owner->second.parameters.end())
                expansion.parameters[parameter.key] = targets->second;
        }
        return expansion;
    }

    std::vector<FlatNode> nodes;
    std::vector<FlatConnection> connections;

private:
    static void append(std::vector<Endpoint>& destination,
                       const std::vector<Endpoint>& source)
    {
        destination.insert(destination.end(), source.begin(), source.end());
    }

    static std::vector<Endpoint> endpoint(
        const std::unordered_map<NodeId, Expansion>& expansions,
        const graph::GraphEndpoint value, const bool input)
    {
        const auto expansion = expansions.find(value.nodeId);
        if (expansion == expansions.end())
            return {};
        const auto& ports = input ? expansion->second.inputs
                                  : expansion->second.outputs;
        const auto port = ports.find(value.portId);
        return port == ports.end() ? std::vector<Endpoint> {}
                                   : port->second;
    }

    Expansion expandPrimitive(const GraphNode& node,
                              const bool preserveNodeId)
    {
        Expansion expansion;
        // Devices inside devices multiply: past this many nodes (no real
        // graph comes near) a crafted file would ask for billions.
        if (nodes.size() >= 65'536) {
            if (std::ranges::none_of(validation.issues, [](const auto& issue) { return issue.code == graph::ValidationCode::invalidRange
                                                                                         && issue.message == "The graph expands to too many nodes"; }))
                addIssue(validation, graph::ValidationCode::invalidRange, "The graph expands to too many nodes", node.id);
            return expansion;
        }
        auto primitiveValidation = registry.validateNode(node);
        validation.issues.insert(validation.issues.end(),
                                 primitiveValidation.issues.begin(),
                                 primitiveValidation.issues.end());
        if (!primitiveValidation)
            return expansion;
        FlatNode flat;
        flat.id = preserveNodeId ? node.id : nextId++;
        flat.processorKey = node.processorKey;
        for (const auto& parameter : node.parameters) {
            flat.parameters.emplace(parameter.key,
                                    parameterValue(node, parameter));
            expansion.parameters[parameter.key].push_back(
                { flat.id, parameter.key });
        }
        nodes.push_back(std::move(flat));
        for (const auto& port : node.ports) {
            auto& endpoints = port.direction == graph::PortDirection::input
                                  ? expansion.inputs[port.id]
                                  : expansion.outputs[port.id];
            endpoints.push_back({ nodes.back().id, port.id });
        }
        return expansion;
    }

    Expansion expandInstance(const GraphNode& node)
    {
        if (!node.graph.has_value())
            return {};
        const auto* nested = library.resolve(*node.graph);
        if (nested == nullptr) {
            addIssue(validation, graph::ValidationCode::missingDependency,
                     "Nested instrument Graph is unavailable: "
                         + node.graph->toString(), node.id);
            return {};
        }
        auto expansion = expand(*nested, false);
        // MIDI wired straight across the nested Graph (a thru device before
        // the voice): a transpose of nothing stands for the wire, which the
        // note path follows like any MIDI effect. Without it the notes
        // stopped there.
        for (const auto& [in, out] : expansion.passthroughs) {
            const auto port = std::ranges::find(nested->ports, in, &graph::GraphPort::id);
            if (port == nested->ports.end() || port->type != graph::PortType::midi)
                continue;
            FlatNode through;
            through.id = nextId++;
            through.processorKey = "core.midi-transpose";
            through.parameters.emplace("semitones", 0.0);
            nodes.push_back(std::move(through));
            expansion.inputs[in].push_back({ nodes.back().id, 1 });
            expansion.outputs[out].push_back({ nodes.back().id, 2 });
        }
        expansion.passthroughs.clear();
        for (const auto& [key, value] : node.parameterOverrides) {
            const auto descriptor = std::ranges::find(
                nested->parameters, key, &graph::PublishedParameter::key);
            if (descriptor == nested->parameters.end()
                || !std::isfinite(value)
                || value < descriptor->minimum || value > descriptor->maximum) {
                addIssue(validation, graph::ValidationCode::invalidRange,
                         "Nested instrument parameter override is invalid: "
                             + key,
                         node.id);
                continue;
            }
            const auto targets = expansion.parameters.find(key);
            if (targets == expansion.parameters.end())
                continue;
            for (const auto& target : targets->second) {
                const auto flat = std::ranges::find(nodes, target.nodeId,
                                                    &FlatNode::id);
                if (flat != nodes.end())
                    flat->parameters[target.key] = value;
            }
        }
        return expansion;
    }

    const graph::GraphLibrary& library;
    graph::ValidationResult& validation;
    const graph::PrimitiveRegistry& registry;
    std::uint64_t nextId { 1 };
};

const FlatNode* firstNode(const std::vector<FlatNode>& nodes,
                          const std::string_view processorKey)
{
    const auto found = std::ranges::find(nodes, processorKey,
                                         &FlatNode::processorKey);
    return found == nodes.end() ? nullptr : &*found;
}

// The same lookup against a predicate, so the callers that ask "any sound
// source" do not spell out the list a second time.
const FlatNode* firstMatchingNode(const std::vector<FlatNode>& nodes,
                                  bool (*matches)(std::string_view) noexcept)
{
    const auto found = std::ranges::find_if(
        nodes, [matches](const FlatNode& node) {
            return matches(node.processorKey);
        });
    return found == nodes.end() ? nullptr : &*found;
}

double value(const FlatNode& node, const std::string_view key,
             const double fallback)
{
    const auto found = node.parameters.find(std::string(key));
    return found == node.parameters.end() ? fallback : found->second;
}

bool reaches(const std::vector<FlatConnection>& connections,
             const std::uint64_t source, const std::uint64_t destination)
{
    if (source == destination)
        return true;
    std::vector<std::uint64_t> pending { source };
    std::set<std::uint64_t> visited;
    while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        if (!visited.insert(current).second)
            continue;
        for (const auto& connection : connections) {
            if (connection.source.nodeId != current)
                continue;
            if (connection.destination.nodeId == destination)
                return true;
            pending.push_back(connection.destination.nodeId);
        }
    }
    return false;
}

// How many cables from the nearest of `sources` to `destination` (a large
// number when none leads there).
int distanceFrom(const std::vector<FlatConnection>& connections, const std::vector<Endpoint>& sources,
                 const std::uint64_t destination)
{
    std::vector<std::pair<std::uint64_t, int>> pending;
    for (const auto& source : sources)
        pending.emplace_back(source.nodeId, 0);
    std::set<std::uint64_t> visited;
    for (std::size_t at = 0; at < pending.size(); ++at) {
        const auto [current, steps] = pending[at];
        if (current == destination)
            return steps;
        if (!visited.insert(current).second)
            continue;
        for (const auto& connection : connections)
            if (connection.source.nodeId == current)
                pending.emplace_back(connection.destination.nodeId, steps + 1);
    }
    return 1'000'000;
}

bool anyReach(const std::vector<FlatConnection>& connections,
              const std::vector<Endpoint>& sources,
              const std::uint64_t destination)
{
    return std::ranges::any_of(sources, [&](const Endpoint& source) {
        return reaches(connections, source.nodeId, destination);
    });
}

bool reachesAny(const std::vector<FlatConnection>& connections,
                const std::uint64_t source,
                const std::vector<Endpoint>& destinations)
{
    return std::ranges::any_of(destinations,
        [&](const Endpoint& destination) {
            return reaches(connections, source, destination.nodeId);
        });
}

} // namespace

GraphInstrumentCompileResult compileGraphInstrument(
    const graph::GraphLibrary& library, const graph::GraphReference& root)
{
    GraphInstrumentCompileResult result;
    const auto* definition = library.resolve(root);
    if (definition == nullptr) {
        addIssue(result.validation, graph::ValidationCode::missingDependency,
                 "Instrument Graph is unavailable: " + root.toString());
        return result;
    }
    graph::ValidationResult libraryValidation;
    if (!library.compile(&libraryValidation).has_value()) {
        result.validation = std::move(libraryValidation);
        return result;
    }

    Flattener flattener(library, result.validation);
    const auto rootExpansion = flattener.expand(*definition, true);
    result.plan.graphNodeCount = flattener.nodes.size()
        + definition->ports.size();
    if (!result.validation)
        return result;

    const auto* allocator = firstNode(flattener.nodes,
                                      "core.voice-allocator");
    // Whichever sound source the voice is built around; graph::SoundSources
    // is the one place that says which primitives qualify.
    const auto* oscillator = firstMatchingNode(
        flattener.nodes, graph::isPolyphonicVoiceSource);
    const auto* wavetable = firstNode(flattener.nodes, "core.wavetable");
    const auto* stringVoice = firstNode(flattener.nodes, "core.string");
    const auto* modelVoice = firstNode(flattener.nodes, "core.sound-model");
    // A Batterie is a sampler with a drum mix.
    const auto* drumVoice = firstNode(flattener.nodes, "core.batterie");
    const auto* samplerVoice = drumVoice != nullptr ? drumVoice : firstNode(flattener.nodes, "core.sampler");
    const auto* prismVoice = firstNode(flattener.nodes, "core.prism-synth");
    const auto* kickVoice = firstNode(flattener.nodes, "core.kick-synth");
    const auto* envelope = firstNode(flattener.nodes, "core.adsr");
    const auto* amplifier = firstNode(flattener.nodes, "core.gain");
    const auto* filter = firstNode(flattener.nodes, "core.lowpass");
    const auto containsInstrument = allocator != nullptr
                                    || firstMatchingNode(
                                           flattener.nodes,
                                           graph::impliesPolyphonicVoice)
                                           != nullptr
                                    || envelope != nullptr
                                    || amplifier != nullptr;
    if (!containsInstrument)
        return result;
    if (allocator == nullptr || oscillator == nullptr || envelope == nullptr
        || amplifier == nullptr || filter == nullptr) {
        addIssue(result.validation, graph::ValidationCode::invalidRole,
                 "Polyphonic Graph requires Voice Allocator, a sound source "
                 "(Oscillator, Wavetable or String), ADSR, Gain/VCA and "
                 "Low-pass nodes");
        return result;
    }

    std::vector<Endpoint> midiInputs;
    std::vector<Endpoint> audioOutputs;
    for (const auto& port : definition->ports) {
        if (port.type == graph::PortType::midi
            && port.direction == graph::PortDirection::input) {
            if (const auto targets = rootExpansion.inputs.find(port.id);
                targets != rootExpansion.inputs.end())
                midiInputs.insert(midiInputs.end(), targets->second.begin(),
                                  targets->second.end());
        }
        if (port.type == graph::PortType::audio
            && port.direction == graph::PortDirection::output) {
            if (const auto sources = rootExpansion.outputs.find(port.id);
                sources != rootExpansion.outputs.end())
                audioOutputs.insert(audioOutputs.end(), sources->second.begin(),
                                    sources->second.end());
        }
    }

    const auto signalFlow = anyReach(flattener.connections, midiInputs,
                                     allocator->id)
        && reaches(flattener.connections, allocator->id, oscillator->id)
        && reaches(flattener.connections, allocator->id, envelope->id)
        && reaches(flattener.connections, oscillator->id, amplifier->id)
        && reaches(flattener.connections, envelope->id, amplifier->id)
        && reaches(flattener.connections, amplifier->id, filter->id)
        && reachesAny(flattener.connections, filter->id, audioOutputs);
    if (!signalFlow) {
        addIssue(result.validation, graph::ValidationCode::invalidConnection,
                 "Polyphonic MIDI, pitch, gate, audio and envelope paths are "
                 "not connected to the published audio output");
        return result;
    }

    result.plan.enabled = true;
    result.plan.voiceCount = std::clamp(
        static_cast<int>(std::lround(value(*allocator, "voices", 16.0))),
        1, 64);
    result.plan.voiceMode = std::clamp(static_cast<int>(std::lround(value(*allocator, "mode", 0.0))), 0, 2);
    result.plan.glideSeconds = static_cast<float>(std::clamp(value(*allocator, "glide", 0.0), 0.0, 2'000.0) / 1'000.0);
    if (oscillator == wavetable && wavetable != nullptr) {
        result.plan.waveform = PolyOscillatorWaveform::wavetable;
        result.plan.wavetablePosition = static_cast<float>(std::clamp(
            value(*wavetable, "position", 0.35), 0.0, 1.0));
        result.plan.wavetableDetuneCents = static_cast<float>(std::clamp(
            value(*wavetable, "detune", 8.0), 0.0, 50.0));
    } else if (oscillator == modelVoice && modelVoice != nullptr) {
        // The model itself is the card's: the host attaches it.
        result.plan.waveform = PolyOscillatorWaveform::soundModel;
        for (std::size_t index = 0; index < soundModelSettingCount; ++index) {
            const auto& setting = soundModelSettings[index];
            result.plan.soundModelValues[index] = static_cast<float>(std::clamp(
                value(*modelVoice, std::string(setting.key), setting.defaultValue),
                static_cast<double>(setting.minimum), static_cast<double>(setting.maximum)));
        }
    } else if (oscillator == samplerVoice && samplerVoice != nullptr) {
        // The sample map itself is the card's: the host attaches it.
        result.plan.waveform = PolyOscillatorWaveform::sampler;
        for (std::size_t index = 0; index < samplerSettingCount; ++index) {
            const auto& setting = samplerSettings[index];
            result.plan.samplerValues[index] = static_cast<float>(std::clamp(
                value(*samplerVoice, std::string(setting.key), setting.defaultValue),
                static_cast<double>(setting.minimum), static_cast<double>(setting.maximum)));
        }
        result.plan.drumKit = samplerVoice == drumVoice;
        if (result.plan.drumKit) {
            result.plan.drumValues = drum::defaultValues();
            for (int index = 0; index < drum::valueCount; ++index) {
                const auto& setting = drum::parameter(index);
                result.plan.drumValues[static_cast<std::size_t>(index)]
                    = drum::clampValue(index, static_cast<float>(value(*samplerVoice, setting.key, setting.defaultValue)));
            }
        }
    } else if (oscillator == prismVoice && prismVoice != nullptr) {
        // Prisme: every setting by its key; the host resolves its tables.
        result.plan.waveform = PolyOscillatorWaveform::prism;
        for (int index = 0; index < prism::parameterCount; ++index) {
            const auto& setting = prism::parameter(index);
            result.plan.prismValues[static_cast<std::size_t>(index)]
                = prism::clampValue(index, static_cast<float>(value(*prismVoice, setting.key, setting.defaultValue)));
        }
    } else if (oscillator == kickVoice && kickVoice != nullptr) {
        // Kick: every setting by its key.
        result.plan.waveform = PolyOscillatorWaveform::kick;
        for (int index = 0; index < kick::parameterCount; ++index) {
            const auto& setting = kick::parameter(index);
            result.plan.kickValues[static_cast<std::size_t>(index)]
                = kick::clampValue(index, static_cast<float>(value(*kickVoice, setting.key, setting.defaultValue)));
        }
    } else if (const auto model = synth::modelForProcessor(oscillator->processorKey); model != synth::Model::none) {
        // A synth of the family: its model, every setting by its key.
        result.plan.waveform = PolyOscillatorWaveform::family;
        result.plan.synthModel = model;
        synth::warmUp(model);
        result.plan.synthValues = synth::defaultValues(model);
        for (int index = 0; index < synth::parameterCount(model); ++index) {
            const auto& setting = synth::parameter(model, index);
            result.plan.synthValues[static_cast<std::size_t>(index)]
                = synth::clampValue(model, index, static_cast<float>(value(*oscillator, setting.key, setting.defaultValue)));
        }
    } else if (const auto container = instrument_container::kindOfProcessor(oscillator->processorKey);
               container != instrument_container::Kind::none) {
        // An instrument container: its pads or layers play (the track's
        // engine), its voices nothing.
        result.plan.waveform = PolyOscillatorWaveform::rack;
        result.plan.containerKind = container;
        for (const auto& setting : instrument_container::settings) {
            const auto index = instrument_container::settingIndex(setting.key);
            result.plan.containerValues[static_cast<std::size_t>(index)]
                = instrument_container::clampValue(index, static_cast<float>(value(*oscillator, std::string(setting.key), setting.defaultValue)));
        }
    } else if (oscillator == stringVoice && stringVoice != nullptr) {
        result.plan.waveform = PolyOscillatorWaveform::string;
        result.plan.stringDamping = static_cast<float>(std::clamp(
            value(*stringVoice, "damping", 0.35), 0.0, 1.0));
        result.plan.stringDecaySeconds = static_cast<float>(std::clamp(
            value(*stringVoice, "decay", 4.0), 0.05, 20.0));
    } else {
        result.plan.waveform = static_cast<PolyOscillatorWaveform>(std::clamp(
            static_cast<int>(std::lround(value(*oscillator, "waveform", 0.0))),
            0, 3));
    }
    result.plan.oscillatorLevel = static_cast<float>(std::clamp(
        value(*oscillator, "level", 0.14), 0.0, 1.0));
    result.plan.attackSeconds = static_cast<float>(std::clamp(
        value(*envelope, "attack", 8.0) * 0.001, 0.0001, 30.0));
    result.plan.decaySeconds = static_cast<float>(std::clamp(
        value(*envelope, "decay", 120.0) * 0.001, 0.0001, 30.0));
    result.plan.sustainLevel = static_cast<float>(std::clamp(
        value(*envelope, "sustain", 0.72), 0.0, 1.0));
    result.plan.releaseSeconds = static_cast<float>(std::clamp(
        value(*envelope, "release", 280.0) * 0.001, 0.0001, 60.0));
    result.plan.filterCutoffHz = static_cast<float>(std::clamp(
        value(*filter, "cutoff", 12'000.0), 20.0, 20'000.0));
    result.plan.filterResonance = static_cast<float>(std::clamp(
        value(*filter, "resonance", 0.707), 0.1, 20.0));

    for (const auto& node : flattener.nodes) {
        if (!anyReach(flattener.connections, midiInputs, node.id)
            || !reaches(flattener.connections, node.id, allocator->id))
            continue;
        if (node.processorKey == "core.midi-transpose")
            result.plan.transposeSemitones += static_cast<int>(std::lround(
                value(node, "semitones", 0.0)));
        else if (node.processorKey == "core.midi-note-filter") {
            result.plan.minimumMidiNote = std::max(
                result.plan.minimumMidiNote,
                static_cast<int>(std::lround(
                    value(node, "minimum-note", 0.0))));
            result.plan.maximumMidiNote = std::min(
                result.plan.maximumMidiNote,
                static_cast<int>(std::lround(
                    value(node, "maximum-note", 127.0))));
        } else if (node.processorKey == "core.midi-velocity")
            result.plan.velocityScale *= static_cast<float>(
                value(node, "scale", 1.0));
    }
    // The MIDI effects on the way, in the order the notes meet them.
    {
        std::vector<std::pair<int, const FlatNode*>> chain;
        for (const auto& node : flattener.nodes) {
            const auto* effect = effectForProcessor(node.processorKey);
            const auto legacy = node.processorKey == "core.midi-arpeggiator";
            if ((effect == nullptr || !effect->midi) && !legacy)
                continue;
            if (!anyReach(flattener.connections, midiInputs, node.id) || !reaches(flattener.connections, node.id, allocator->id))
                continue;
            chain.emplace_back(distanceFrom(flattener.connections, midiInputs, node.id), &node);
        }
        std::ranges::stable_sort(chain, {}, &std::pair<int, const FlatNode*>::first);
        if (!chain.empty()) {
            auto effects = std::make_shared<MidiEffectsPlan>();
            for (const auto& [distance, node] : chain) {
                if (effects->count >= midiEffectMost)
                    break;
                const auto* effect = effectForProcessor(node->processorKey);
                if (effect == nullptr) {
                    effects->effects[effects->count++] = legacyArpeggiatorSettings(value(*node, "rate", 0.25), value(*node, "octaves", 1.0));
                    continue;
                }
                MidiEffectSettings settings;
                settings.kind = effect->kind;
                for (std::size_t index = 0; index < effect->parameters.size() && index < settings.values.size(); ++index) {
                    const auto& parameter = effect->parameters[index];
                    settings.values[index] = std::clamp(static_cast<float>(value(*node, parameter.key, parameter.defaultValue)),
                                                        parameter.minimum, parameter.maximum);
                }
                effects->effects[effects->count++] = settings;
            }
            result.plan.midiEffects = std::move(effects);
        }
    }
    result.plan.transposeSemitones = std::clamp(
        result.plan.transposeSemitones, -96, 96);
    result.plan.minimumMidiNote = std::clamp(
        result.plan.minimumMidiNote, 0, 127);
    result.plan.maximumMidiNote = std::clamp(
        result.plan.maximumMidiNote, result.plan.minimumMidiNote, 127);
    result.plan.velocityScale = std::clamp(
        result.plan.velocityScale, 0.0f, 4.0f);
    return result;
}

} // namespace cydaw::engine
