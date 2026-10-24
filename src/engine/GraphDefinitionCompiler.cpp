#include <cydaw/engine/GraphDefinitionCompiler.h>

#include <cydaw/graph/PrimitiveRegistry.h>
#include <cydaw/engine/NativeDspParameters.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <ranges>
#include <string_view>
#include <tuple>
#include <unordered_map>

namespace cydaw::engine {
namespace {

using graph::GraphDefinition;
using graph::GraphNode;
using graph::GraphPort;
using graph::NodeId;
using graph::ParameterId;
using graph::PortId;

struct NativeEndpoint {
    std::uint64_t nodeId {};
    PortId portId {};
};

struct ControlEndpoint {
    std::uint64_t nodeId {};
    PortId portId {};
    // Non-empty only when this socket is a modulatable parameter of an audio
    // processor rather than an input of another control primitive.
    std::string parameterKey;
    // The parameter's range, for a cable that modulates it.
    double minimum {};
    double maximum { 1.0 };
};

struct PrimitiveParameterTarget {
    std::uint64_t nativeNodeId {};
    std::string processorKey;
    std::string parameterKey;
};

struct Expansion {
    std::unordered_map<PortId, std::vector<NativeEndpoint>> inputs;
    std::unordered_map<PortId, std::vector<NativeEndpoint>> outputs;
    std::unordered_map<PortId, std::vector<ControlEndpoint>> controlInputs;
    std::unordered_map<PortId, std::vector<ControlEndpoint>> controlOutputs;
    std::unordered_map<std::string,
                       std::vector<PrimitiveParameterTarget>> parameters;
    std::vector<std::pair<PortId, PortId>> boundaryPassthroughs;
};

void addIssue(graph::ValidationResult& validation,
              const graph::ValidationCode code, std::string message,
              const std::optional<NodeId> nodeId = {},
              const std::optional<PortId> portId = {})
{
    validation.issues.push_back({ graph::ValidationSeverity::error, code,
                                  std::move(message), nodeId, portId });
}

const GraphNode* findNode(const GraphDefinition& definition,
                          const NodeId nodeId)
{
    const auto found = std::ranges::find(definition.nodes, nodeId,
                                         &GraphNode::id);
    return found == definition.nodes.end() ? nullptr : &*found;
}

std::optional<NativeDspNodeKind> nativeKindFor(
    const std::string_view processorKey)
{
    return nativeDspKindForProcessor(processorKey);
}

std::optional<NativeDspNodeKind> controlKindFor(
    const std::string_view processorKey)
{
    return nativeDspControlKindForProcessor(processorKey);
}

std::string controlParameterForPort(const std::string_view processorKey,
                                   const std::string_view portKey)
{
    return std::string(nativeDspParameterKeyForPort(processorKey, portKey));
}

bool hasAudioPort(const GraphNode& node)
{
    return std::ranges::any_of(node.ports, [](const GraphPort& port) {
        return port.type == graph::PortType::audio;
    });
}

double parameterValue(const GraphNode& node, const std::string_view key,
                      const double fallback)
{
    if (const auto overrideValue = node.parameterOverrides.find(
            std::string(key)); overrideValue != node.parameterOverrides.end())
        return overrideValue->second;
    const auto parameter = std::ranges::find(node.parameters, key,
                                             &graph::NodeParameter::key);
    return parameter == node.parameters.end() ? fallback
                                               : parameter->defaultValue;
}

bool applyParameter(NativeDspNode& node, const std::string_view processorKey,
                    const std::string_view key, const double value)
{
    const auto numeric = static_cast<float>(value);
    if (processorKey == "core.drive" && key == "drive-db")
        node.parameters.driveDb = numeric;
    else if (processorKey == "core.gain" && key == "gain-db")
        node.parameters.gainDb = numeric;
    else if (processorKey == "core.lowpass" && key == "cutoff")
        node.parameters.cutoffHz = numeric;
    else if (processorKey == "core.lowpass" && key == "resonance")
        node.parameters.resonance = numeric;
    else if (processorKey == "core.highpass" && key == "cutoff")
        node.parameters.highpassCutoffHz = numeric;
    else if (processorKey == "core.hard-clip" && key == "threshold")
        node.parameters.clipThreshold = numeric;
    else if (processorKey == "core.stereo-width" && key == "width")
        node.parameters.stereoWidth = numeric;
    else if (processorKey == "core.wavetable" && key == "frequency")
        node.parameters.oscillatorFrequencyHz = numeric;
    else if (processorKey == "core.wavetable" && key == "position")
        node.parameters.wavetablePosition = numeric;
    else if (processorKey == "core.wavetable" && key == "level")
        node.parameters.oscillatorLevel = numeric;
    else if (processorKey == "core.wavetable" && key == "detune")
        node.parameters.wavetableDetuneCents = numeric;
    else if (processorKey == "core.string" && key == "frequency")
        node.parameters.oscillatorFrequencyHz = numeric;
    else if (processorKey == "core.string" && key == "damping")
        node.parameters.stringDamping = numeric;
    else if (processorKey == "core.string" && key == "decay")
        node.parameters.stringDecaySeconds = numeric;
    else if (processorKey == "core.peaking-eq" && key == "frequency")
        node.parameters.bandFrequencyHz = numeric;
    else if (processorKey == "core.peaking-eq" && key == "gain")
        node.parameters.bandGainDb = numeric;
    else if (processorKey == "core.peaking-eq" && key == "q")
        node.parameters.bandQ = numeric;
    else if (processorKey == "core.pan" && key == "pan")
        node.parameters.pan = numeric;
    else if (processorKey == "core.fixed-oscillator" && key == "frequency")
        node.parameters.oscillatorFrequencyHz = numeric;
    else if (processorKey == "core.fixed-oscillator" && key == "waveform")
        node.parameters.oscillatorWaveform = numeric;
    else if (processorKey == "core.fixed-oscillator" && key == "level")
        node.parameters.oscillatorLevel = numeric;
    else if (processorKey == "core.noise" && key == "level")
        node.parameters.noiseLevel = numeric;
    else if (processorKey == "core.delay" && key == "time")
        node.parameters.delayMilliseconds = numeric;
    else if (processorKey == "core.delay" && key == "feedback")
        node.parameters.feedback = numeric;
    else if (processorKey == "core.delay" && key == "mix")
        node.parameters.mix = numeric;
    else if (processorKey == "core.reverb" && key == "room-size")
        node.parameters.roomSize = numeric;
    else if (processorKey == "core.reverb" && key == "mix")
        node.parameters.mix = numeric;
    else if (processorKey == "core.compressor" && key == "threshold")
        node.parameters.thresholdDb = numeric;
    else if (processorKey == "core.compressor" && key == "ratio")
        node.parameters.ratio = numeric;
    else if (processorKey == "core.equalizer" && key == "tone")
        node.parameters.tone = numeric;
    else if ((processorKey == "core.constant" || processorKey == "core.macro")
             && key == "value")
        node.parameters.valueOperand = numeric;
    else if (processorKey == "core.lfo" && key == "rate")
        node.parameters.modulationRateHz = numeric;
    else if (processorKey == "core.lfo" && key == "depth")
        node.parameters.modulationDepth = numeric;
    else if (processorKey == "core.lfo" && key == "offset")
        node.parameters.modulationOffset = numeric;
    else if (processorKey == "core.lfo" && key == "shape")
        node.parameters.modulationShape = numeric;
    else if ((processorKey == "core.add" || processorKey == "core.subtract"
              || processorKey == "core.multiply" || processorKey == "core.divide"
              || processorKey == "core.modulo" || processorKey == "core.power"
              || processorKey == "core.minimum" || processorKey == "core.maximum"
              || processorKey.starts_with("core.compare-")
              || processorKey == "core.logic-and" || processorKey == "core.logic-or"
              || processorKey == "core.logic-xor")
             && key == "operand")
        node.parameters.valueOperand = numeric;
    else if (processorKey == "core.clamp" && key == "minimum")
        node.parameters.valueMinimum = numeric;
    else if (processorKey == "core.clamp" && key == "maximum")
        node.parameters.valueMaximum = numeric;
    else if (processorKey == "core.map-range" && key == "input-min")
        node.parameters.mapInputMinimum = numeric;
    else if (processorKey == "core.map-range" && key == "input-max")
        node.parameters.mapInputMaximum = numeric;
    else if (processorKey == "core.map-range" && key == "output-min")
        node.parameters.mapOutputMinimum = numeric;
    else if (processorKey == "core.map-range" && key == "output-max")
        node.parameters.mapOutputMaximum = numeric;
    else if (processorKey == "core.scale-offset" && key == "amount")
        node.parameters.modulationDepth = numeric;
    else if (processorKey == "core.scale-offset" && key == "offset")
        node.parameters.modulationOffset = numeric;
    else if (processorKey == "core.envelope-follower" && key == "attack")
        node.parameters.followerAttackSeconds = numeric;
    else if (processorKey == "core.envelope-follower" && key == "release")
        node.parameters.followerReleaseSeconds = numeric;
    else if (const auto* effect = effectForProcessor(processorKey)) {
        const auto* parameter = effectParameter(*effect, key);
        if (parameter == nullptr)
            return false;
        setEffectValue(node.parameters, *parameter, static_cast<float>(numeric));
    } else
        return false;
    return true;
}

bool applyControlDefault(NativeDspNode& node,
                         const ControlEndpoint& endpoint,
                         const double value)
{
    const auto numeric = static_cast<float>(value);
    if (endpoint.parameterKey.empty()) {
        if (endpoint.portId == 1)
            node.parameters.valueInputA = numeric;
        else if (endpoint.portId == 2)
            node.parameters.valueInputB = numeric;
        else
            return false;
        return true;
    }
    const auto field = nativeDspParameterField(node.kind,
                                              endpoint.parameterKey);
    if (field == nullptr) {
        const auto slot=nativeDspParameterSlot(node.kind,endpoint.parameterKey);
        if(slot<0||slot>=static_cast<int>(node.parameters.eqBands.size()))return false;
        node.parameters.eqBands[slot]=numeric;
    } else node.parameters.*field = numeric;
    return true;
}

class Compiler final {
public:
    Compiler(const graph::GraphLibrary& libraryValue,
             graph::ValidationResult& validationValue)
        : library(libraryValue), validation(validationValue),
          primitives(graph::factoryPrimitiveRegistry())
    {
        for (const auto& reference : library.all())
            if (const auto* definition = library.resolve(reference))
                for (const auto& node : definition->nodes)
                    nextNativeNodeId = std::max(nextNativeNodeId,
                                                node.id + 1);
    }

    Expansion expand(const GraphDefinition& definition,
                     const bool preservePrimitiveIds = false)
    {
        std::unordered_map<NodeId, Expansion> nodeExpansions;
        for (const auto& node : definition.nodes) {
            auto expansion = node.kind == graph::GraphNodeKind::primitive
                ? expandPrimitive(node, preservePrimitiveIds)
                : expandInstance(node);
            if (preservePrimitiveIds)
                for (const auto& [portId, endpoints] :
                     expansion.controlOutputs) {
                    (void) portId;
                    for (const auto& endpoint : endpoints)
                        controlTelemetryTargets.push_back(
                            { endpoint.nodeId, node.id });
                }
            nodeExpansions.emplace(node.id, std::move(expansion));
        }

        Expansion result;
        for (const auto& connection : definition.connections) {
            if (connection.source.nodeId == graph::boundaryNodeId
                && connection.destination.nodeId == graph::boundaryNodeId) {
                result.boundaryPassthroughs.emplace_back(
                    connection.source.portId,
                    connection.destination.portId);
                continue;
            }
            if (connection.source.nodeId == graph::boundaryNodeId) {
                const auto destination = endpoint(
                    nodeExpansions, connection.destination, true);
                append(result.inputs[connection.source.portId], destination);
                const auto controlDestination = controlEndpoint(
                    nodeExpansions, connection.destination, true);
                append(result.controlInputs[connection.source.portId],
                       controlDestination);
                continue;
            }
            if (connection.destination.nodeId == graph::boundaryNodeId) {
                const auto source = endpoint(
                    nodeExpansions, connection.source, false);
                append(result.outputs[connection.destination.portId], source);
                const auto controlSource = controlEndpoint(
                    nodeExpansions, connection.source, false);
                append(result.controlOutputs[connection.destination.portId],
                       controlSource);
                continue;
            }
            const auto sources = endpoint(nodeExpansions, connection.source,
                                          false);
            const auto destinations = endpoint(nodeExpansions,
                                               connection.destination, true);
            for (const auto& source : sources)
                for (const auto& destination : destinations)
                    native.connections.push_back(
                        { source.nodeId, destination.nodeId, source.portId,
                          destination.portId });
            const auto controlSources = controlEndpoint(
                nodeExpansions, connection.source, false);
            const auto controlDestinations = controlEndpoint(
                nodeExpansions, connection.destination, true);
            for (const auto& source : controlSources)
                for (const auto& destination : controlDestinations)
                    native.modulations.push_back(
                        { source.nodeId, destination.nodeId, source.portId,
                          destination.portId, destination.parameterKey,
                          destination.parameterKey.empty() ? std::uint8_t {} : connection.mode,
                          static_cast<float>(connection.amount),
                          static_cast<float>(destination.minimum),
                          static_cast<float>(destination.maximum) });
        }

        for (const auto& parameter : definition.parameters) {
            const auto nodeExpansion = nodeExpansions.find(
                parameter.targetNodeId);
            if (nodeExpansion == nodeExpansions.end())
                continue;
            const auto target = nodeExpansion->second.parameters.find(
                parameter.targetParameterKey);
            if (target != nodeExpansion->second.parameters.end())
                result.parameters[parameter.key] = target->second;
        }
        return result;
    }

    NativeDspGraph native;
    std::vector<CompiledControlTelemetryTarget> controlTelemetryTargets;

private:
    static void append(std::vector<NativeEndpoint>& destination,
                       const std::vector<NativeEndpoint>& source)
    {
        destination.insert(destination.end(), source.begin(), source.end());
    }

    static void append(std::vector<ControlEndpoint>& destination,
                       const std::vector<ControlEndpoint>& source)
    {
        destination.insert(destination.end(), source.begin(), source.end());
    }

    std::vector<NativeEndpoint> endpoint(
        const std::unordered_map<NodeId, Expansion>& expansions,
        const graph::GraphEndpoint graphEndpoint, const bool input)
    {
        const auto expansion = expansions.find(graphEndpoint.nodeId);
        if (expansion == expansions.end())
            return {};
        const auto& ports = input ? expansion->second.inputs
                                  : expansion->second.outputs;
        const auto port = ports.find(graphEndpoint.portId);
        return port == ports.end() ? std::vector<NativeEndpoint> {}
                                   : port->second;
    }

    std::vector<ControlEndpoint> controlEndpoint(
        const std::unordered_map<NodeId, Expansion>& expansions,
        const graph::GraphEndpoint graphEndpoint, const bool input)
    {
        const auto expansion = expansions.find(graphEndpoint.nodeId);
        if (expansion == expansions.end())
            return {};
        const auto& ports = input ? expansion->second.controlInputs
                                  : expansion->second.controlOutputs;
        const auto port = ports.find(graphEndpoint.portId);
        return port == ports.end() ? std::vector<ControlEndpoint> {}
                                   : port->second;
    }

    Expansion expandPrimitive(const GraphNode& node,
                              const bool preserveNodeId)
    {
        Expansion expansion;
        // Devices inside devices multiply: past this many nodes (no real
        // graph comes near) a crafted file would ask for billions.
        if (native.nodes.size() >= 65'536) {
            if (std::ranges::none_of(validation.issues, [](const auto& issue) { return issue.code == graph::ValidationCode::invalidRange
                                                                                         && issue.message == "The graph expands to too many nodes"; }))
                addIssue(validation, graph::ValidationCode::invalidRange, "The graph expands to too many nodes", node.id);
            return expansion;
        }
        const auto primitiveValidation = primitives.validateNode(node);
        validation.issues.insert(validation.issues.end(),
                                 primitiveValidation.issues.begin(),
                                 primitiveValidation.issues.end());
        if (!primitiveValidation)
            return expansion;

        const auto audioKind = nativeKindFor(node.processorKey);
        const auto controlKind = controlKindFor(node.processorKey);
        if (!audioKind.has_value() && !controlKind.has_value()) {
            if (!hasAudioPort(node))
                return expansion;
            addIssue(validation, graph::ValidationCode::missingDependency,
                     "Primitive has no native audio runtime: "
                         + node.processorKey,
                     node.id);
            return expansion;
        }

        NativeDspNode nativeNode;
        nativeNode.id = preserveNodeId ? node.id : nextNativeNodeId++;
        nativeNode.kind = audioKind.has_value() ? *audioKind : *controlKind;
        for (const auto& parameter : node.parameters)
            (void) applyParameter(nativeNode, node.processorKey,
                                  parameter.key,
                                  parameterValue(node, parameter.key,
                                                 parameter.defaultValue));
        auto valueInputIndex = 0;
        for (const auto& port : node.ports)
            if (port.direction == graph::PortDirection::input
                && (port.type == graph::PortType::value
                    || port.type == graph::PortType::trigger)) {
                if (valueInputIndex == 0)
                    nativeNode.parameters.valueInputA =
                        static_cast<float>(port.defaultValue);
                else if (valueInputIndex == 1)
                    nativeNode.parameters.valueInputB =
                        static_cast<float>(port.defaultValue);
                ++valueInputIndex;
            }
        native.nodes.push_back(nativeNode);

        for (const auto& port : node.ports) {
            if (port.type == graph::PortType::audio) {
                if (!audioKind.has_value())
                    continue;
                auto& endpoints = port.direction == graph::PortDirection::input
                                      ? expansion.inputs[port.id]
                                      : expansion.outputs[port.id];
                endpoints.push_back({ nativeNode.id, port.id });
                continue;
            }
            if (port.type != graph::PortType::value
                && port.type != graph::PortType::trigger)
                continue;
            if (port.direction == graph::PortDirection::output) {
                // A processor that measures something publishes it: the
                // follower its envelope, the compressor how far it is pulling
                // the signal down. Without this the reduction port existed in
                // the registry and reached nothing.
                if (controlKind.has_value()
                    || nativeNode.kind == NativeDspNodeKind::envelopeFollower
                    || nativeNode.kind == NativeDspNodeKind::compressor)
                    expansion.controlOutputs[port.id].push_back(
                        { nativeNode.id, port.id, {} });
                continue;
            }
            if (controlKind.has_value()) {
                expansion.controlInputs[port.id].push_back(
                    { nativeNode.id, port.id, {} });
                continue;
            }
            const auto parameterKey = controlParameterForPort(
                node.processorKey, port.key);
            if (!parameterKey.empty()) {
                const auto range = parameterRange(node.processorKey, parameterKey);
                expansion.controlInputs[port.id].push_back(
                    { nativeNode.id, port.id, parameterKey, range.first, range.second });
            }
        }
        for (const auto& parameter : node.parameters)
            expansion.parameters[parameter.key].push_back(
                { nativeNode.id, node.processorKey, parameter.key });
        return expansion;
    }

    Expansion expandInstance(const GraphNode& node)
    {
        Expansion expansion;
        if (!node.graph.has_value())
            return expansion;
        const auto* nested = library.resolve(*node.graph);
        if (nested == nullptr) {
            addIssue(validation, graph::ValidationCode::missingDependency,
                     "Nested Graph is unavailable: "
                         + node.graph->toString(), node.id);
            return expansion;
        }
        expansion = expand(*nested, false);
        // A wire straight across the nested Graph (its audio input to its
        // output: an empty device, a thru with a meter tapping it): a
        // passthrough node stands for it, for the parent's cables to reach.
        // Without it the instance had no way through and all after it fell
        // silent.
        for (const auto& [in, out] : expansion.boundaryPassthroughs) {
            const auto port = std::ranges::find(nested->ports, in, &graph::GraphPort::id);
            if (port == nested->ports.end() || port->type != graph::PortType::audio)
                continue;
            NativeDspNode through;
            through.id = nextNativeNodeId++;
            through.kind = NativeDspNodeKind::passthrough;
            native.nodes.push_back(through);
            expansion.inputs[in].push_back({ through.id, 1 });
            expansion.outputs[out].push_back({ through.id, 2 });
        }
        expansion.boundaryPassthroughs.clear();
        // A reusable Graph is a class, but every instance owns the fallback
        // value of its exposed Value/Trigger inputs. Apply these defaults to
        // the flattened native destinations before parent cables are added;
        // a connected cable then takes precedence at runtime.
        for (const auto& port : node.ports) {
            if (port.direction != graph::PortDirection::input
                || (port.type != graph::PortType::value
                    && port.type != graph::PortType::trigger))
                continue;
            const auto targets = expansion.controlInputs.find(port.id);
            if (targets == expansion.controlInputs.end())
                continue;
            for (const auto& target : targets->second) {
                const auto nativeNode = std::ranges::find(
                    native.nodes, target.nodeId, &NativeDspNode::id);
                if (nativeNode != native.nodes.end())
                    (void) applyControlDefault(*nativeNode, target,
                                               port.defaultValue);
            }
        }
        // A device's knob socket ("knob.<published key>"): a cable from
        // outside reaches the knob's own parameter, inside, wherever it is.
        // After the defaults above, which a knob's socket has none of.
        for (const auto& port : node.ports) {
            if (port.direction != graph::PortDirection::input || port.type != graph::PortType::value
                || !port.key.starts_with("knob."))
                continue;
            const auto targets = expansion.parameters.find(port.key.substr(5));
            if (targets == expansion.parameters.end())
                continue;
            auto& inputs = expansion.controlInputs[port.id];
            inputs.clear();
            for (const auto& target : targets->second) {
                // Only what the runtime can drive: a knob it cannot reach (a
                // compressor's attack, an LFO's rate) is left as set, rather
                // than the whole graph refused for one cable.
                using Kind = NativeDspNodeKind;
                const auto made = std::ranges::find(native.nodes, target.nativeNodeId, &NativeDspNode::id);
                if (made == native.nodes.end()
                    || (made->kind >= Kind::controlConstant && made->kind <= Kind::controlNot)
                    || (made->kind >= Kind::controlModulo && made->kind <= Kind::controlRound) || isModulatorKind(made->kind)
                    || (nativeDspParameterField(made->kind, target.parameterKey) == nullptr
                        && nativeDspParameterSlot(made->kind, target.parameterKey) < 0))
                    continue;
                const auto range = parameterRange(target.processorKey, target.parameterKey);
                inputs.push_back({ target.nativeNodeId, port.id, target.parameterKey, range.first, range.second });
            }
        }
        for (const auto& [key, value] : node.parameterOverrides) {
            const auto published = std::ranges::find(nested->parameters, key,
                                                     &graph::PublishedParameter::key);
            if (published == nested->parameters.end()
                || !std::isfinite(value)
                || value < published->minimum || value > published->maximum) {
                addIssue(validation, graph::ValidationCode::invalidRange,
                         "Nested Graph parameter override is invalid: " + key,
                         node.id);
                continue;
            }
            const auto targets = expansion.parameters.find(key);
            if (targets == expansion.parameters.end())
                continue;
            for (const auto& target : targets->second) {
                const auto nativeNode = std::ranges::find(
                    native.nodes, target.nativeNodeId, &NativeDspNode::id);
                if (nativeNode != native.nodes.end())
                    (void) applyParameter(*nativeNode, target.processorKey,
                                          target.parameterKey, value);
            }
        }
        return expansion;
    }

    // A primitive parameter's range, from the registry.
    [[nodiscard]] std::pair<double, double> parameterRange(const std::string& processorKey,
                                                           const std::string& parameterKey) const
    {
        if (const auto* descriptor = primitives.find(processorKey))
            for (const auto& parameter : descriptor->parameters)
                if (parameter.key == parameterKey)
                    return { parameter.minimum, parameter.maximum };
        return { 0.0, 1.0 };
    }

    const graph::GraphLibrary& library;
    graph::ValidationResult& validation;
    const graph::PrimitiveRegistry& primitives;
    std::uint64_t nextNativeNodeId { 1 };
};

} // namespace

GraphDefinitionCompileResult compileGraphDefinitionToNativeDsp(
    const graph::GraphLibrary& library, const graph::GraphReference& root)
{
    GraphDefinitionCompileResult result;
    const auto* definition = library.resolve(root);
    if (definition == nullptr) {
        addIssue(result.validation, graph::ValidationCode::missingDependency,
                 "Root Graph is unavailable: " + root.toString());
        return result;
    }

    graph::ValidationResult libraryValidation;
    if (!library.compile(&libraryValidation).has_value()) {
        result.validation = std::move(libraryValidation);
        return result;
    }

    Compiler compiler(library, result.validation);
    auto rootExpansion = compiler.expand(*definition, true);
    if (!result.validation)
        return result;

    std::uint64_t nextId = 1;
    for (const auto& node : compiler.native.nodes)
        nextId = std::max(nextId, node.id + 1);

    const auto audioInputs = std::ranges::count_if(
        definition->ports, [](const GraphPort& port) {
            return port.type == graph::PortType::audio
                   && port.direction == graph::PortDirection::input;
        });
    // Each audio input port's node, for the wires straight to an output.
    std::unordered_map<std::uint64_t, std::uint64_t> inputNodes;
    if (audioInputs > 0) {
        // Each audio input its own: the first the main one, the others
        // the extra inputs a track is routed into (a sidechain).
        std::uint32_t bus {};
        for (const auto& port : definition->ports) {
            if (port.type != graph::PortType::audio
                || port.direction != graph::PortDirection::input)
                continue;
            const auto inputNodeId = nextId++;
            NativeDspNode input { inputNodeId, NativeDspNodeKind::input, {} };
            input.inputBus = bus++;
            compiler.native.nodes.push_back(input);
            inputNodes[port.id] = inputNodeId;
            const auto targets = rootExpansion.inputs.find(port.id);
            if (targets == rootExpansion.inputs.end())
                continue;
            for (const auto& target : targets->second)
                compiler.native.connections.push_back(
                    { inputNodeId, target.nodeId, port.id, target.portId });
        }
    }

    const auto audioOutputs = std::ranges::count_if(
        definition->ports, [](const GraphPort& port) {
            return port.type == graph::PortType::audio
                   && port.direction == graph::PortDirection::output;
        });
    if (audioOutputs == 0) {
        const auto valueOutputs = std::ranges::count_if(
            definition->ports, [](const GraphPort& port) {
                return port.type == graph::PortType::value
                       && port.direction == graph::PortDirection::output;
            });
        if (definition->role != graph::GraphRole::modulator
            && valueOutputs == 0) {
            addIssue(result.validation, graph::ValidationCode::invalidRole,
                     "Graph has neither an audio nor a Value output");
            return result;
        }
        for (const auto& port : definition->ports) {
            if (port.type != graph::PortType::value
                || port.direction != graph::PortDirection::output)
                continue;
            const auto sources = rootExpansion.controlOutputs.find(port.id);
            if (sources == rootExpansion.controlOutputs.end()
                || sources->second.empty())
                addIssue(result.validation,
                         graph::ValidationCode::invalidConnection,
                         "Published Value output is not connected", {},
                         port.id);
        }
        if (!result.validation)
            return result;

        // The realtime control evaluator advances once per audio block. A
        // Value-only Graph therefore receives a private silent passthrough
        // plan as its clock. These nodes are an implementation detail: they
        // do not create public audio ports or change the Graph contract.
        const auto clockInputNodeId = nextId++;
        const auto clockOutputNodeId = nextId++;
        compiler.native.nodes.push_back(
            { clockInputNodeId, NativeDspNodeKind::input, {} });
        compiler.native.nodes.push_back(
            { clockOutputNodeId, NativeDspNodeKind::output, {} });
        compiler.native.connections.push_back(
            { clockInputNodeId, clockOutputNodeId });
        compiler.native.outputNodeId = clockOutputNodeId;
    } else {
        const auto outputNodeId = nextId++;
        compiler.native.nodes.push_back(
            { outputNodeId, NativeDspNodeKind::output, {} });
        compiler.native.outputNodeId = outputNodeId;
        for (const auto& port : definition->ports) {
            if (port.type != graph::PortType::audio
                || port.direction != graph::PortDirection::output)
                continue;
            const auto sources = rootExpansion.outputs.find(port.id);
            const auto direct = std::ranges::any_of(
                rootExpansion.boundaryPassthroughs,
                [&port](const auto& passthrough) {
                    return passthrough.second == port.id;
                });
            if ((sources == rootExpansion.outputs.end()
                 || sources->second.empty())
                && !direct) {
                addIssue(result.validation,
                         graph::ValidationCode::invalidConnection,
                         "Published audio output is not connected", {},
                         port.id);
                continue;
            }
            if (sources != rootExpansion.outputs.end())
                for (const auto& source : sources->second)
                    compiler.native.connections.push_back(
                        { source.nodeId, outputNodeId, source.portId,
                          port.id });
        }
        for (const auto& [sourcePort, destinationPort] :
             rootExpansion.boundaryPassthroughs)
            if (const auto input = inputNodes.find(sourcePort);
                input != inputNodes.end())
                compiler.native.connections.push_back(
                    { input->second, outputNodeId, sourcePort,
                      destinationPort });
    }

    for (const auto& parameter : definition->parameters) {
        const auto targets = rootExpansion.parameters.find(parameter.key);
        if (targets == rootExpansion.parameters.end())
            continue;
        for (const auto& target : targets->second)
            result.parameterTargets.push_back(
                { parameter.id, parameter.key, target.nativeNodeId,
                  target.parameterKey });
    }
    for (const auto& port : definition->ports) {
        if (port.type != graph::PortType::value
            || port.direction != graph::PortDirection::output)
            continue;
        const auto sources = rootExpansion.controlOutputs.find(port.id);
        if (sources == rootExpansion.controlOutputs.end())
            continue;
        for (const auto& source : sources->second)
            compiler.controlTelemetryTargets.push_back(
                { source.nodeId, port.id });
    }
    std::ranges::sort(
        compiler.controlTelemetryTargets,
        [](const auto& left, const auto& right) {
            return std::tie(left.nativeNodeId, left.authoredNodeId)
                   < std::tie(right.nativeNodeId, right.authoredNodeId);
        });
    compiler.controlTelemetryTargets.erase(
        std::ranges::unique(compiler.controlTelemetryTargets).begin(),
        compiler.controlTelemetryTargets.end());
    result.controlTelemetryTargets =
        std::move(compiler.controlTelemetryTargets);
    result.graph = std::move(compiler.native);
    return result;
}

} // namespace cydaw::engine
