#include <cydaw/engine/KickSynth.h>
#include <cydaw/engine/PrismSynth.h>
#include <cydaw/graph/PrimitiveRegistry.h>

#include <cydaw/engine/Containers.h>
#include <cydaw/engine/EffectCatalogue.h>

#include <algorithm>
#include <cmath>
#include <ranges>
#include <string>
#include <unordered_set>

namespace cydaw::graph {
namespace {

GraphPort port(const PortId id, std::string key, const PortType type,
               const PortDirection direction, const PortRate rate,
               const std::uint16_t channels = 1,
               const bool multiple = false,
               const double defaultValue = 0.0)
{
    GraphPort value;
    value.id = id;
    value.key = std::move(key);
    value.displayName = value.key;
    value.type = type;
    value.audioType = type == PortType::audio
                          ? audioPortTypeForChannels(channels)
                          : AudioPortType::unspecified;
    value.direction = direction;
    value.rate = rate;
    value.channels = channels;
    value.acceptsMultipleConnections = multiple;
    value.defaultValue = defaultValue;
    return value;
}

NodeParameter parameter(std::string key, const double minimum,
                        const double maximum, const double defaultValue)
{
    return { std::move(key), minimum, maximum, defaultValue };
}

void issue(ValidationResult& result, const ValidationCode code,
           std::string message, const NodeId nodeId = 0,
           const PortId portId = 0)
{
    result.issues.push_back({ ValidationSeverity::error, code,
                              std::move(message),
                              nodeId == 0 ? std::optional<NodeId> {}
                                          : std::optional<NodeId> { nodeId },
                              portId == 0 ? std::optional<PortId> {}
                                          : std::optional<PortId> { portId } });
}

bool descriptorIsValid(const PrimitiveDescriptor& descriptor)
{
    if (descriptor.key.empty() || descriptor.displayName.empty())
        return false;
    std::unordered_set<PortId> portIds;
    std::unordered_set<std::string> portKeys;
    for (const auto& item : descriptor.ports)
        if (item.id == 0 || item.key.empty()
            || !portIds.insert(item.id).second
            || !portKeys.insert(item.key).second || item.channels == 0)
            return false;
    std::unordered_set<std::string> parameterKeys;
    for (const auto& item : descriptor.parameters)
        if (item.key.empty() || !parameterKeys.insert(item.key).second
            || !std::isfinite(item.minimum) || !std::isfinite(item.maximum)
            || item.minimum > item.maximum
            || item.defaultValue < item.minimum
            || item.defaultValue > item.maximum)
            return false;
    return true;
}

PrimitiveDescriptor primitive(std::string key, std::string name,
                              const PrimitiveCategory category,
                              std::vector<GraphPort> ports,
                              std::vector<NodeParameter> parameters = {},
                              const bool stateful = false,
                              const bool polyphonic = false,
                              const bool feedbackBoundary = false,
                              const bool variableAudioChannels = false)
{
    return { std::move(key), std::move(name), category, std::move(ports),
             std::move(parameters), stateful, polyphonic, feedbackBoundary,
             variableAudioChannels };
}

// An instrument's settings past its frequency and level (its first two
// parameters) each get a socket of their own, optional (hidden in the graph
// until shown), from a high id in their order: a modulator plugged there
// moves that knob.
PrimitiveDescriptor withSettingSockets(PrimitiveDescriptor descriptor)
{
    for (std::size_t index = 2; index < descriptor.parameters.size(); ++index) {
        auto socket = port(static_cast<PortId>(2'048 + index - 2), descriptor.parameters[index].key, PortType::value,
                           PortDirection::input, PortRate::control, 1, true);
        socket.defaultValue = descriptor.parameters[index].defaultValue;
        socket.optional = true;
        descriptor.ports.push_back(std::move(socket));
    }
    return descriptor;
}

} // namespace

ValidationResult PrimitiveRegistry::install(PrimitiveDescriptor descriptor,
                                            const bool replaceExisting)
{
    ValidationResult result;
    if (!descriptorIsValid(descriptor)) {
        issue(result, ValidationCode::invalidIdentity,
              "Primitive descriptor is incomplete or inconsistent");
        return result;
    }
    if (descriptors_.contains(descriptor.key) && !replaceExisting) {
        issue(result, ValidationCode::duplicateKey,
              "Primitive key already registered: " + descriptor.key);
        return result;
    }
    descriptors_.insert_or_assign(descriptor.key, std::move(descriptor));
    return result;
}

const PrimitiveDescriptor* PrimitiveRegistry::find(
    const std::string& key) const noexcept
{
    const auto found = descriptors_.find(key);
    return found == descriptors_.end() ? nullptr : &found->second;
}

std::vector<const PrimitiveDescriptor*> PrimitiveRegistry::all() const
{
    std::vector<const PrimitiveDescriptor*> result;
    result.reserve(descriptors_.size());
    for (const auto& [key, descriptor] : descriptors_) {
        (void) key;
        result.push_back(&descriptor);
    }
    std::ranges::sort(result, {}, &PrimitiveDescriptor::key);
    return result;
}

std::vector<const PrimitiveDescriptor*> PrimitiveRegistry::category(
    const PrimitiveCategory categoryValue) const
{
    auto result = all();
    std::erase_if(result, [categoryValue](const auto* descriptor) {
        return descriptor->category != categoryValue;
    });
    return result;
}

ValidationResult PrimitiveRegistry::validateNode(const GraphNode& node) const
{
    ValidationResult result;
    if (node.kind != GraphNodeKind::primitive) {
        issue(result, ValidationCode::invalidIdentity,
              "A graph instance is not a primitive node", node.id);
        return result;
    }
    const auto* descriptor = find(node.processorKey);
    if (descriptor == nullptr) {
        issue(result, ValidationCode::missingDependency,
              "Unknown primitive: " + node.processorKey, node.id);
        return result;
    }
    for (const auto& expected : descriptor->ports) {
        const auto actual = std::ranges::find(node.ports, expected.id,
                                              &GraphPort::id);
        if (actual == node.ports.end()) {
            if(expected.optional)continue;
            issue(result, ValidationCode::invalidPort,
                  "Missing primitive port: " + expected.key,
                  node.id, expected.id);
            continue;
        }
        const auto compatibleChannels = descriptor->supportsVariableAudioChannels
                && expected.type == PortType::audio
            ? actual->channels >= 1 && actual->channels <= 8
                  && actual->audioType
                         == audioPortTypeForChannels(actual->channels)
            : actual->channels == expected.channels
                  && (expected.type != PortType::audio
                      || actual->audioType == expected.audioType);
        if (actual->key != expected.key || actual->type != expected.type
            || actual->direction != expected.direction
            || actual->rate != expected.rate
            || !compatibleChannels)
            issue(result, ValidationCode::incompatiblePorts,
                  "Primitive port contract changed: " + expected.key,
                  node.id, expected.id);
    }
    if (descriptor->supportsVariableAudioChannels) {
        auto exactChannels = std::uint16_t {};
        for (const auto& item : node.ports) {
            if (item.type != PortType::audio)
                continue;
            if (exactChannels == 0)
                exactChannels = item.channels;
            else if (item.channels != exactChannels)
                issue(result, ValidationCode::incompatiblePorts,
                      "All audio ports of this primitive must use the same exact channel type",
                      node.id, item.id);
        }
    }
    if (std::ranges::any_of(node.ports,[&](const auto& port){return std::ranges::find(descriptor->ports,port.id,&GraphPort::id)==descriptor->ports.end();}))
        issue(result, ValidationCode::invalidPort,
              "Primitive node exposes undeclared ports", node.id);
    for (const auto& expected : descriptor->parameters) {
        const auto actual = std::ranges::find(node.parameters, expected.key,
                                              &NodeParameter::key);
        if (actual == node.parameters.end()) {
            issue(result, ValidationCode::invalidBinding,
                  "Missing primitive parameter: " + expected.key, node.id);
            continue;
        }
        if (actual->minimum != expected.minimum
            || actual->maximum != expected.maximum)
            issue(result, ValidationCode::invalidRange,
                  "Primitive parameter contract changed: " + expected.key,
                  node.id);
    }
    for (const auto& [key, value] : node.parameterOverrides) {
        const auto expected = std::ranges::find(descriptor->parameters, key,
                                                &NodeParameter::key);
        if (expected == descriptor->parameters.end() || !std::isfinite(value)
            || value < expected->minimum || value > expected->maximum)
            issue(result, ValidationCode::invalidRange,
                  "Primitive parameter override is invalid: " + key,
                  node.id);
    }
    return result;
}

std::size_t PrimitiveRegistry::size() const noexcept
{
    return descriptors_.size();
}

PrimitiveRegistry createFactoryPrimitiveRegistry()
{
    using Type = PortType;
    using Direction = PortDirection;
    using Rate = PortRate;
    using Category = PrimitiveCategory;
    PrimitiveRegistry registry;
    const auto add = [&registry](PrimitiveDescriptor descriptor) {
        (void) registry.install(std::move(descriptor));
    };

    add(primitive("core.constant", "Constant", Category::math,
        { port(1, "value", Type::value, Direction::output, Rate::control) },
        { parameter("value", -1'000'000.0, 1'000'000.0, 0.0) }));
    add(primitive("core.add", "Add", Category::math,
        { port(1, "a", Type::value, Direction::input, Rate::control),
          port(2, "b", Type::value, Direction::input, Rate::control),
          port(3, "sum", Type::value, Direction::output, Rate::control) },
        { parameter("operand", -1'000'000.0, 1'000'000.0, 0.0) }));
    add(primitive("core.multiply", "Multiply", Category::math,
        { port(1, "a", Type::value, Direction::input, Rate::control,
               1, false, 1.0),
          port(2, "b", Type::value, Direction::input, Rate::control,
               1, false, 1.0),
          port(3, "product", Type::value, Direction::output, Rate::control) },
        { parameter("operand", -1'000'000.0, 1'000'000.0, 1.0) }));
    add(primitive("core.subtract", "Subtract", Category::math,
        { port(1, "a", Type::value, Direction::input, Rate::control),
          port(2, "b", Type::value, Direction::input, Rate::control),
          port(3, "difference", Type::value, Direction::output, Rate::control) },
        { parameter("operand", -1'000'000.0, 1'000'000.0, 0.0) }));
    add(primitive("core.divide", "Divide", Category::math,
        { port(1, "numerator", Type::value, Direction::input, Rate::control),
          port(2, "denominator", Type::value, Direction::input, Rate::control,
               1, false, 1.0),
          port(3, "quotient", Type::value, Direction::output, Rate::control) },
        { parameter("operand", -1'000'000.0, 1'000'000.0, 1.0) }));
    add(primitive("core.modulo", "Modulo", Category::math,
        { port(1, "a", Type::value, Direction::input, Rate::control),
          port(2, "b", Type::value, Direction::input, Rate::control,
               1, false, 1.0),
          port(3, "remainder", Type::value, Direction::output,
               Rate::control) },
        { parameter("operand", -1'000'000.0, 1'000'000.0, 1.0) }));
    add(primitive("core.power", "Power", Category::math,
        { port(1, "base", Type::value, Direction::input, Rate::control),
          port(2, "exponent", Type::value, Direction::input, Rate::control,
               1, false, 1.0),
          port(3, "result", Type::value, Direction::output, Rate::control) },
        { parameter("operand", -32.0, 32.0, 1.0) }));
    add(primitive("core.sine", "Sine", Category::math,
        { port(1, "radians", Type::value, Direction::input, Rate::control),
          port(2, "result", Type::value, Direction::output, Rate::control) }));
    add(primitive("core.cosine", "Cosine", Category::math,
        { port(1, "radians", Type::value, Direction::input, Rate::control),
          port(2, "result", Type::value, Direction::output, Rate::control) }));
    add(primitive("core.square-root", "Square Root", Category::math,
        { port(1, "value", Type::value, Direction::input, Rate::control),
          port(2, "result", Type::value, Direction::output, Rate::control) }));
    add(primitive("core.floor", "Floor", Category::math,
        { port(1, "value", Type::value, Direction::input, Rate::control),
          port(2, "result", Type::value, Direction::output, Rate::control) }));
    add(primitive("core.ceil", "Ceil", Category::math,
        { port(1, "value", Type::value, Direction::input, Rate::control),
          port(2, "result", Type::value, Direction::output, Rate::control) }));
    add(primitive("core.round", "Round", Category::math,
        { port(1, "value", Type::value, Direction::input, Rate::control),
          port(2, "result", Type::value, Direction::output, Rate::control) }));
    add(primitive("core.minimum", "Minimum", Category::math,
        { port(1, "a", Type::value, Direction::input, Rate::control),
          port(2, "b", Type::value, Direction::input, Rate::control),
          port(3, "minimum", Type::value, Direction::output, Rate::control) },
        { parameter("operand", -1'000'000.0, 1'000'000.0, 0.0) }));
    add(primitive("core.maximum", "Maximum", Category::math,
        { port(1, "a", Type::value, Direction::input, Rate::control),
          port(2, "b", Type::value, Direction::input, Rate::control),
          port(3, "maximum", Type::value, Direction::output, Rate::control) },
        { parameter("operand", -1'000'000.0, 1'000'000.0, 0.0) }));
    add(primitive("core.clamp", "Clamp", Category::math,
        { port(1, "value", Type::value, Direction::input, Rate::control),
          port(2, "clamped", Type::value, Direction::output, Rate::control) },
        { parameter("minimum", -1'000'000.0, 1'000'000.0, -1.0),
          parameter("maximum", -1'000'000.0, 1'000'000.0, 1.0) }));
    add(primitive("core.absolute", "Absolute", Category::math,
        { port(1, "value", Type::value, Direction::input, Rate::control),
          port(2, "absolute", Type::value, Direction::output, Rate::control) }));
    add(primitive("core.negate", "Negate", Category::math,
        { port(1, "value", Type::value, Direction::input, Rate::control),
          port(2, "negated", Type::value, Direction::output, Rate::control) }));
    add(primitive("core.compare", "Compare", Category::math,
        { port(1, "a", Type::value, Direction::input, Rate::control),
          port(2, "b", Type::value, Direction::input, Rate::control),
          port(3, "result", Type::trigger, Direction::output, Rate::event) },
        { parameter("operation", 0.0, 5.0, 0.0),
          parameter("epsilon", 0.0, 1.0, 0.000001) }));
    add(primitive("core.logic", "Boolean Logic", Category::math,
        { port(1, "a", Type::trigger, Direction::input, Rate::event),
          port(2, "b", Type::trigger, Direction::input, Rate::event),
          port(3, "result", Type::trigger, Direction::output, Rate::event) },
        { parameter("operation", 0.0, 3.0, 0.0) }));
    const auto addComparator = [&add](
                                   const char* key, const char* name) {
        add(primitive(key, name, Category::math,
            { port(1, "a", Type::value, Direction::input, Rate::control),
              port(2, "b", Type::value, Direction::input, Rate::control),
              port(3, "result", Type::trigger, Direction::output, Rate::event) },
            { parameter("operand", -1'000'000.0, 1'000'000.0, 0.0) }));
    };
    addComparator("core.compare-greater", "Greater Than");
    addComparator("core.compare-less", "Less Than");
    addComparator("core.compare-equal", "Equal");
    const auto addBoolean = [&add](const char* key,
                                          const char* name,
                                          const bool unary) {
        auto ports = std::vector<GraphPort> {
            port(1, "a", Type::trigger, Direction::input, Rate::event)
        };
        if (!unary)
            ports.push_back(port(2, "b", Type::trigger,
                                 Direction::input, Rate::event));
        ports.push_back(port(unary ? 2 : 3, "result", Type::trigger,
                             Direction::output, Rate::event));
        add(primitive(key, name, Category::math, std::move(ports),
            unary ? std::vector<NodeParameter> {}
                  : std::vector<NodeParameter> {
                        parameter("operand", 0.0, 1.0, 0.0) }));
    };
    addBoolean("core.logic-and", "AND", false);
    addBoolean("core.logic-or", "OR", false);
    addBoolean("core.logic-xor", "XOR", false);
    addBoolean("core.logic-not", "NOT", true);
    add(primitive("core.map-range", "Map Range", Category::math,
        { port(1, "value", Type::value, Direction::input, Rate::control),
          port(2, "mapped", Type::value, Direction::output, Rate::control) },
        { parameter("input-min", -1.0, 1.0, 0.0),
          parameter("input-max", -1.0, 1.0, 1.0),
          parameter("output-min", -1'000.0, 1'000.0, 0.0),
          parameter("output-max", -1'000.0, 1'000.0, 1.0) }));
    add(primitive("core.scale-offset", "Scale / Offset", Category::math,
        { port(1, "value", Type::value, Direction::input, Rate::control),
          port(2, "result", Type::value, Direction::output, Rate::control) },
        { parameter("amount", 0.0, 1.0, 1.0),
          parameter("offset", -1.0, 1.0, 0.0) }));
    add(primitive("core.macro", "Macro", Category::modulation,
        { port(1, "value", Type::value, Direction::output, Rate::control) },
        { parameter("value", -1.0, 1.0, 0.0) }));
    add(primitive("core.voice-allocator", "Voice Allocator", Category::voice,
        { port(1, "midi", Type::midi, Direction::input, Rate::event),
          port(2, "pitch", Type::value, Direction::output, Rate::control),
          port(3, "gate", Type::trigger, Direction::output, Rate::event),
          port(4, "velocity", Type::value, Direction::output, Rate::control) },
        { parameter("voices", 1.0, 64.0, 16.0),
          parameter("mode", 0.0, 2.0, 0.0),
          parameter("glide", 0.0, 2000.0, 0.0) }, true, true));
    add(primitive("core.oscillator", "Oscillator", Category::generator,
        { port(1, "pitch", Type::value, Direction::input, Rate::control),
          port(2, "reset", Type::trigger, Direction::input, Rate::event),
          port(3, "audio", Type::audio, Direction::output, Rate::audio, 2) },
        { parameter("waveform", 0.0, 4.0, 0.0),
          parameter("detune", -48.0, 48.0, 0.0),
          parameter("level", 0.0, 1.0, 0.8) }, true, true, false, true));
    add(primitive("core.wavetable", "Wavetable", Category::generator,
        { port(1, "audio-out", Type::audio, Direction::output, Rate::audio, 2),
          port(2, "frequency", Type::value, Direction::input, Rate::control),
          port(3, "position", Type::value, Direction::input, Rate::control),
          port(4, "level", Type::value, Direction::input, Rate::control),
          port(5, "detune", Type::value, Direction::input, Rate::control) },
        { parameter("frequency", 20.0, 8'000.0, 220.0),
          parameter("position", 0.0, 1.0, 0.35),
          parameter("level", 0.0, 1.0, 0.20),
          parameter("detune", 0.0, 50.0, 8.0) },
        true, true, false, true));
    add(primitive("core.string", "String", Category::generator,
        { port(1, "audio-in", Type::audio, Direction::input, Rate::audio, 2),
          port(2, "audio-out", Type::audio, Direction::output, Rate::audio, 2),
          port(3, "frequency", Type::value, Direction::input, Rate::control),
          port(4, "damping", Type::value, Direction::input, Rate::control),
          port(5, "decay", Type::value, Direction::input, Rate::control) },
        { parameter("frequency", 20.0, 4'000.0, 220.0),
          parameter("damping", 0.0, 1.0, 0.35),
          parameter("decay", 0.05, 20.0, 4.0) },
        true, true, false, true));
    // A recorded note made playable (SoundModel.h): the model is the card's,
    // these are how it plays.
    {
        std::vector<NodeParameter> modelParameters { parameter("frequency", 20.0, 8'000.0, 220.0),
                                                     parameter("level", 0.0, 1.0, 1.0) };
        for (const auto& setting : cydaw::engine::soundModelSettings)
            modelParameters.push_back(parameter(std::string(setting.key), setting.minimum, setting.maximum,
                                                setting.defaultValue));
        add(withSettingSockets(primitive("core.sound-model", "Sound Model", Category::generator,
            { port(1, "audio-out", Type::audio, Direction::output, Rate::audio, 2),
              port(2, "frequency", Type::value, Direction::input, Rate::control),
              port(3, "level", Type::value, Direction::input, Rate::control) },
            std::move(modelParameters), true, true, false, true)));
    }
    // Recorded notes played back as they are (Sampler.h): the sample map is
    // the card's, these are how it plays.
    {
        std::vector<NodeParameter> samplerParameters { parameter("frequency", 20.0, 8'000.0, 220.0),
                                                       parameter("level", 0.0, 1.0, 1.0) };
        for (const auto& setting : cydaw::engine::samplerSettings)
            samplerParameters.push_back(parameter(std::string(setting.key), setting.minimum, setting.maximum,
                                                  setting.defaultValue));
        // Batterie: a sampler playing a drum kit, mixed piece by piece.
        auto drumParameters = samplerParameters;
        for (const auto& setting : cydaw::engine::drum::parameters())
            drumParameters.push_back(parameter(setting.key, setting.minimum, setting.maximum, setting.defaultValue));
        add(withSettingSockets(primitive("core.sampler", "Sampler", Category::generator,
            { port(1, "audio-out", Type::audio, Direction::output, Rate::audio, 2),
              port(2, "frequency", Type::value, Direction::input, Rate::control),
              port(3, "level", Type::value, Direction::input, Rate::control) },
            std::move(samplerParameters), true, true, false, true)));
        // A rack of pads: each pad an instrument and its effects of its own
        // (the track's engine plays them); its own voices are silent.
        add(primitive("core.drum-rack", "Pad Rack", Category::generator,
            { port(1, "audio-out", Type::audio, Direction::output, Rate::audio, 2),
              port(2, "frequency", Type::value, Direction::input, Rate::control),
              port(3, "level", Type::value, Direction::input, Rate::control) },
            {}, true, true, false, true));
        add(withSettingSockets(primitive("core.batterie", "Batterie", Category::generator,
            { port(1, "audio-out", Type::audio, Direction::output, Rate::audio, 2),
              port(2, "frequency", Type::value, Direction::input, Rate::control),
              port(3, "level", Type::value, Direction::input, Rate::control) },
            std::move(drumParameters), true, true, false, true)));
    }
    // Prisme (PrismSynth.h): a whole wavetable synthesiser per voice, its
    // settings by their keys ("a.position", "env2.decay").
    {
        std::vector<NodeParameter> prismParameters { parameter("frequency", 20.0, 8'000.0, 220.0),
                                                     parameter("level", 0.0, 1.0, 1.0) };
        for (int index = 0; index < cydaw::engine::prism::parameterCount; ++index) {
            const auto& setting = cydaw::engine::prism::parameter(index);
            prismParameters.push_back(parameter(setting.key, setting.minimum, setting.maximum, setting.defaultValue));
        }
        add(withSettingSockets(primitive("core.prism-synth", "Prisme", Category::generator,
            { port(1, "audio-out", Type::audio, Direction::output, Rate::audio, 2),
              port(2, "frequency", Type::value, Direction::input, Rate::control),
              port(3, "level", Type::value, Direction::input, Rate::control) },
            std::move(prismParameters), true, true, false, true)));
    }
    // Kick (KickSynth.h): a kick drum per note, its settings by their keys
    // ("pitchStart", "decay").
    {
        std::vector<NodeParameter> kickParameters { parameter("frequency", 20.0, 8'000.0, 220.0),
                                                    parameter("level", 0.0, 1.0, 1.0) };
        for (int index = 0; index < cydaw::engine::kick::parameterCount; ++index) {
            const auto& setting = cydaw::engine::kick::parameter(index);
            kickParameters.push_back(parameter(setting.key, setting.minimum, setting.maximum, setting.defaultValue));
        }
        add(withSettingSockets(primitive("core.kick-synth", "Kick", Category::generator,
            { port(1, "audio-out", Type::audio, Direction::output, Rate::audio, 2),
              port(2, "frequency", Type::value, Direction::input, Rate::control),
              port(3, "level", Type::value, Direction::input, Rate::control) },
            std::move(kickParameters), true, true, false, true)));
    }
    // The synth family (SynthFamily.h): each model a primitive, its settings
    // by their keys ("osc1.wave", "filter.cutoff").
    for (int index = 1; index < cydaw::engine::synth::modelCount; ++index) {
        const auto model = static_cast<cydaw::engine::synth::Model>(index);
        const auto& info = cydaw::engine::synth::info(model);
        if (info.parameters.empty())
            continue;
        std::vector<NodeParameter> settings { parameter("frequency", 20.0, 8'000.0, 220.0), parameter("level", 0.0, 1.0, 1.0) };
        for (const auto& setting : info.parameters)
            settings.push_back(parameter(setting.key, setting.minimum, setting.maximum, setting.defaultValue));
        add(withSettingSockets(primitive(info.processorKey, info.english, Category::generator,
            { port(1, "audio-out", Type::audio, Direction::output, Rate::audio, 2),
              port(2, "frequency", Type::value, Direction::input, Rate::control),
              port(3, "level", Type::value, Direction::input, Rate::control) },
            std::move(settings), true, true, false, true)));
    }
    add(primitive("core.fixed-oscillator", "Fixed Oscillator", Category::generator,
        { port(1, "audio", Type::audio, Direction::output, Rate::audio, 2),
          port(2, "frequency", Type::value, Direction::input, Rate::control),
          port(3, "level", Type::value, Direction::input, Rate::control) },
        { parameter("frequency", 20.0, 20'000.0, 220.0),
          parameter("waveform", 0.0, 4.0, 0.0),
          parameter("level", 0.0, 1.0, 0.8) }, true, false, false, true));
    add(primitive("core.noise", "Noise", Category::generator,
        { port(1, "audio", Type::audio, Direction::output, Rate::audio, 2),
          port(2, "level", Type::value, Direction::input, Rate::control) },
        { parameter("colour", 0.0, 2.0, 0.0),
          parameter("level", 0.0, 1.0, 0.25) }, true, true, false, true));
    add(primitive("core.adsr", "ADSR", Category::envelope,
        { port(1, "gate", Type::trigger, Direction::input, Rate::event),
          port(2, "envelope", Type::value, Direction::output, Rate::control) },
        { parameter("attack", 0.1, 30'000.0, 10.0),
          parameter("decay", 0.1, 30'000.0, 200.0),
          parameter("sustain", 0.0, 1.0, 0.8),
          parameter("release", 0.1, 60'000.0, 500.0) }, true, true));
    add(primitive("core.gain", "Gain", Category::mixer,
        { port(1, "audio-in", Type::audio, Direction::input, Rate::audio, 2),
          port(2, "gain", Type::value, Direction::input, Rate::control),
          port(3, "audio-out", Type::audio, Direction::output, Rate::audio, 2) },
        { parameter("gain-db", -120.0, 24.0, 0.0) },
        false, false, false, true));
    add(primitive("core.drive", "Drive", Category::dynamics,
        { port(1, "audio-in", Type::audio, Direction::input, Rate::audio, 2),
          port(2, "audio-out", Type::audio, Direction::output, Rate::audio, 2),
          port(3, "drive", Type::value, Direction::input, Rate::control) },
        { parameter("drive-db", 0.0, 36.0, 0.0) },
        false, false, false, true));
    add(primitive("core.audio-sum", "Audio Sum", Category::mixer,
        { port(1, "audio-in", Type::audio, Direction::input, Rate::audio, 2, true),
          port(2, "audio-out", Type::audio, Direction::output, Rate::audio, 2) },
        {}, false, false, false, true));
    add(primitive("core.pan", "Pan", Category::mixer,
        { port(1, "audio-in", Type::audio, Direction::input, Rate::audio, 2),
          port(2, "pan", Type::value, Direction::input, Rate::control),
          port(3, "audio-out", Type::audio, Direction::output, Rate::audio, 2) },
        { parameter("pan", -1.0, 1.0, 0.0) },
        false, false, false, true));
    add(primitive("core.mixer-4", "Mixer 4", Category::mixer,
        { port(1, "in-1", Type::audio, Direction::input, Rate::audio, 2),
          port(2, "in-2", Type::audio, Direction::input, Rate::audio, 2),
          port(3, "in-3", Type::audio, Direction::input, Rate::audio, 2),
          port(4, "in-4", Type::audio, Direction::input, Rate::audio, 2),
          port(5, "audio-out", Type::audio, Direction::output, Rate::audio, 2) },
        {}, false, false, false, true));
    add(primitive("core.lowpass", "Low-pass", Category::filter,
        { port(1, "audio-in", Type::audio, Direction::input, Rate::audio, 2),
          port(2, "cutoff", Type::value, Direction::input, Rate::control),
          port(3, "audio-out", Type::audio, Direction::output, Rate::audio, 2),
          port(4, "resonance", Type::value, Direction::input, Rate::control) },
        { parameter("cutoff", 20.0, 20'000.0, 1'000.0),
          parameter("resonance", 0.1, 20.0, 0.707) },
        true, true, false, true));
    add(primitive("core.highpass", "High-pass", Category::filter,
        { port(1, "audio-in", Type::audio, Direction::input, Rate::audio, 2),
          port(2, "cutoff", Type::value, Direction::input, Rate::control),
          port(3, "audio-out", Type::audio, Direction::output, Rate::audio, 2) },
        { parameter("cutoff", 10.0, 20'000.0, 80.0) },
        true, false, false, true));
    add(primitive("core.hard-clip", "Hard Clip", Category::dynamics,
        { port(1, "audio-in", Type::audio, Direction::input, Rate::audio, 2),
          port(2, "audio-out", Type::audio, Direction::output, Rate::audio, 2),
          port(3, "threshold", Type::value, Direction::input, Rate::control) },
        { parameter("threshold", 0.001, 1.0, 0.9) },
        false, false, false, true));
    add(primitive("core.stereo-width", "Stereo Width", Category::mixer,
        { port(1, "audio-in", Type::audio, Direction::input, Rate::audio, 2),
          port(2, "audio-out", Type::audio, Direction::output, Rate::audio, 2),
          port(3, "width", Type::value, Direction::input, Rate::control) },
        { parameter("width", 0.0, 2.0, 1.0) },
        false, false, false, true));
    add(primitive("core.dc-blocker", "DC Blocker", Category::filter,
        { port(1, "audio-in", Type::audio, Direction::input, Rate::audio, 2),
          port(2, "audio-out", Type::audio, Direction::output, Rate::audio, 2) },
        {}, true, false, false, true));
    add(primitive("core.delay", "Delay", Category::delay,
        { port(1, "audio-in", Type::audio, Direction::input, Rate::audio, 2),
          port(2, "audio-out", Type::audio, Direction::output, Rate::audio, 2),
          port(3, "time", Type::value, Direction::input, Rate::control),
          port(4, "feedback", Type::value, Direction::input, Rate::control),
          port(5, "mix", Type::value, Direction::input, Rate::control) },
        { parameter("time", 1.0, 10'000.0, 250.0),
          parameter("feedback", 0.0, 0.999, 0.25),
          parameter("mix", 0.0, 1.0, 0.25) }, true, false, true, true));
    add(primitive("core.compressor", "Compressor", Category::dynamics,
        { port(1, "audio-in", Type::audio, Direction::input, Rate::audio, 2),
          port(2, "sidechain", Type::audio, Direction::input, Rate::audio, 2),
          port(3, "audio-out", Type::audio, Direction::output, Rate::audio, 2),
          port(4, "threshold", Type::value, Direction::input, Rate::control),
          port(5, "ratio", Type::value, Direction::input, Rate::control),
          port(6, "reduction", Type::value, Direction::output,
               Rate::control) },
        { parameter("threshold", -80.0, 0.0, -18.0),
          parameter("ratio", 1.0, 100.0, 4.0),
          parameter("attack", 0.0001, 1.0, 0.01),
          parameter("release", 0.001, 5.0, 0.1) },
        true, false, false, true));
    add(primitive("core.reverb", "Reverb", Category::delay,
        { port(1, "audio-in", Type::audio, Direction::input, Rate::audio, 2),
          port(2, "audio-out", Type::audio, Direction::output, Rate::audio, 2),
          port(3, "room-size", Type::value, Direction::input, Rate::control),
          port(4, "mix", Type::value, Direction::input, Rate::control) },
        { parameter("room-size", 0.0, 0.95, 0.5),
          parameter("mix", 0.0, 1.0, 0.25) },
        true, false, false, true));
    add(primitive("core.equalizer", "Equalizer", Category::filter,
        { port(1, "audio-in", Type::audio, Direction::input, Rate::audio, 2),
          port(2, "audio-out", Type::audio, Direction::output, Rate::audio, 2),
          port(3, "tone", Type::value, Direction::input, Rate::control) },
        { parameter("tone", -1.0, 1.0, 0.0) },
        true, false, false, true));
    add(primitive("core.peaking-eq", "EQ Band", Category::filter,
        { port(1, "audio-in", Type::audio, Direction::input, Rate::audio, 2),
          port(2, "audio-out", Type::audio, Direction::output, Rate::audio, 2),
          port(3, "frequency", Type::value, Direction::input, Rate::control),
          port(4, "gain", Type::value, Direction::input, Rate::control),
          port(5, "q", Type::value, Direction::input, Rate::control) },
        { parameter("frequency", 20.0, 20'000.0, 1'000.0),
          parameter("gain", -30.0, 30.0, 0.0),
          parameter("q", 0.1, 18.0, 1.0) },
        true, true, false, true));
    add(primitive("core.lfo", "LFO", Category::modulation,
        { port(1, "reset", Type::trigger, Direction::input, Rate::event),
          port(2, "value", Type::value, Direction::output, Rate::control) },
        { parameter("rate", 0.001, 100.0, 1.0),
          parameter("shape", 0.0, 4.0, 0.0),
          parameter("depth", 0.0, 1.0, 1.0),
          parameter("offset", -1.0, 1.0, 0.0) }, true));
    add(primitive("core.midi-transpose", "MIDI Transpose", Category::midi,
        { port(1, "midi-in", Type::midi, Direction::input, Rate::event),
          port(2, "midi-out", Type::midi, Direction::output, Rate::event) },
        { parameter("semitones", -48.0, 48.0, 0.0) }));
    add(primitive("core.midi-arpeggiator", "Arpeggiator", Category::midi,
        { port(1, "midi-in", Type::midi, Direction::input, Rate::event),
          port(2, "midi-out", Type::midi, Direction::output, Rate::event) },
        { parameter("rate", 0.03125, 4.0, 0.25),
          parameter("octaves", 1.0, 8.0, 1.0) }, true));
    add(primitive("core.midi-note-filter", "MIDI Note Filter", Category::midi,
        { port(1, "midi-in", Type::midi, Direction::input, Rate::event),
          port(2, "midi-out", Type::midi, Direction::output, Rate::event) },
        { parameter("minimum-note", 0.0, 127.0, 0.0),
          parameter("maximum-note", 0.0, 127.0, 127.0) }));
    add(primitive("core.midi-velocity", "MIDI Velocity", Category::midi,
        { port(1, "midi-in", Type::midi, Direction::input, Rate::event),
          port(2, "midi-out", Type::midi, Direction::output, Rate::event) },
        { parameter("scale", 0.0, 2.0, 1.0) }));
    add(primitive("core.transport-reader", "Transport", Category::transport,
        { port(1, "transport", Type::transport, Direction::input, Rate::event),
          port(2, "tempo", Type::value, Direction::output, Rate::control),
          port(3, "beat", Type::value, Direction::output, Rate::control),
          port(4, "playing", Type::trigger, Direction::output, Rate::event) }));
    add(primitive("core.transport-tempo", "Transport Tempo", Category::transport,
        { port(1, "transport", Type::transport, Direction::input, Rate::event),
          port(2, "tempo", Type::value, Direction::output, Rate::control) }));
    add(primitive("core.transport-beat", "Transport Beat", Category::transport,
        { port(1, "transport", Type::transport, Direction::input, Rate::event),
          port(2, "beat", Type::value, Direction::output, Rate::control) }));
    add(primitive("core.transport-playing", "Transport Playing", Category::transport,
        { port(1, "transport", Type::transport, Direction::input, Rate::event),
          port(2, "playing", Type::trigger, Direction::output, Rate::event) }));
    add(primitive("core.envelope-follower", "Envelope Follower", Category::analysis,
        { port(1, "audio-in", Type::audio, Direction::input, Rate::audio, 2),
          port(2, "envelope", Type::value, Direction::output, Rate::control) },
        { parameter("attack", 0.0001, 1.0, 0.01),
          parameter("release", 0.001, 5.0, 0.1) },
        true, false, false, true));
    add(primitive("core.spectrum-probe", "Spectrum Probe", Category::analysis,
        { port(1, "audio-in", Type::audio, Direction::input, Rate::audio, 2),
          port(2, "audio-out", Type::audio, Direction::output, Rate::audio, 2),
          port(3, "spectrum", Type::analysis, Direction::output, Rate::control) },
        {}, true, false, false, true));
    add(primitive("core.level-probe", "Level Probe", Category::analysis,
        { port(1, "audio-in", Type::audio, Direction::input, Rate::audio, 2),
          port(2, "audio-out", Type::audio, Direction::output, Rate::audio, 2),
          port(3, "level", Type::analysis, Direction::output, Rate::control) },
        {}, true, false, false, true));
    add(primitive("core.waveform-probe", "Waveform Probe", Category::analysis,
        { port(1, "audio-in", Type::audio, Direction::input, Rate::audio, 2),
          port(2, "audio-out", Type::audio, Direction::output, Rate::audio, 2),
          port(3, "waveform", Type::analysis, Direction::output, Rate::control) },
        {}, true, false, false, true));
    add(primitive("core.value-monitor", "Value Monitor", Category::analysis,
        { port(1, "value-in", Type::value, Direction::input, Rate::control),
          port(2, "value-out", Type::value, Direction::output, Rate::control),
          port(3, "analysis", Type::analysis, Direction::output, Rate::control) }));
    add(primitive("core.midi-monitor", "MIDI Monitor", Category::analysis,
        { port(1, "midi-in", Type::midi, Direction::input, Rate::event),
          port(2, "midi-out", Type::midi, Direction::output, Rate::event),
          port(3, "events", Type::analysis, Direction::output, Rate::event) }));
    // A container's end: the dry sound and each branch in, the mix out.
    {
        std::vector<GraphPort> ports { port(static_cast<PortId>(engine::container::endDryPort), "dry-in", Type::audio, Direction::input,
                                            Rate::audio, 2) };
        for (int branch = 0; branch < engine::container::maximumBranches; ++branch) {
            auto in = port(static_cast<PortId>(engine::container::branchPort(branch)), "branch-" + std::to_string(branch + 1) + "-in",
                           Type::audio, Direction::input, Rate::audio, 2, true);
            in.optional = true;
            ports.push_back(std::move(in));
        }
        ports.push_back(port(static_cast<PortId>(engine::container::endOutputPort), "audio-out", Type::audio, Direction::output,
                             Rate::audio, 2));
        add(primitive("core.container-end", "Container End", Category::utility, std::move(ports), {}, true, false, false, true));
    }
    // The effect catalogue: an audio input and output, then one value input
    // per parameter a cable can drive, in the catalogue's order.
    for (const auto& effect : engine::effectCatalogue) {
        // A MIDI effect: notes in, notes out, its values knobs only.
        if (effect.midi) {
            std::vector<NodeParameter> values;
            for (const auto& item : effect.parameters)
                if (!item.key.empty())
                    values.push_back(parameter(std::string(item.key), item.minimum, item.maximum, item.defaultValue));
            add(primitive(std::string(effect.processorKey), std::string(effect.displayName), Category::midi,
                          { port(1, "midi-in", Type::midi, Direction::input, Rate::event),
                            port(2, "midi-out", Type::midi, Direction::output, Rate::event) },
                          std::move(values), true));
            continue;
        }
        // A modulator: a value out, two values in (what a math modulator
        // combines; the others leave them unplugged), its settings knobs.
        if (effect.control) {
            std::vector<NodeParameter> values;
            for (const auto& item : effect.parameters)
                if (!item.key.empty())
                    values.push_back(parameter(std::string(item.key), item.minimum, item.maximum, item.defaultValue));
            auto a = port(1, "a", Type::value, Direction::input, Rate::control);
            auto b = port(2, "b", Type::value, Direction::input, Rate::control);
            a.optional = true;
            b.optional = true;
            add(primitive(std::string(effect.processorKey), std::string(effect.displayName), Category::modulation,
                          { std::move(a), std::move(b), port(3, "value", Type::value, Direction::output, Rate::control) },
                          std::move(values), true));
            continue;
        }
        // A container's head (Containers.h): the dry sound out to its end,
        // then a branch output each.
        std::vector<GraphPort> ports {
            port(1, "audio-in", Type::audio, Direction::input, Rate::audio, effect.inputChannels),
            port(2, effect.container ? "dry-out" : "audio-out", Type::audio, Direction::output, Rate::audio, effect.outputChannels)
        };
        if (effect.container)
            for (int branch = 0; branch < engine::container::maximumBranches; ++branch) {
                auto out = port(static_cast<PortId>(engine::container::branchPort(branch)), "branch-" + std::to_string(branch + 1),
                                Type::audio, Direction::output, Rate::audio, effect.outputChannels, true);
                out.optional = true;
                ports.push_back(std::move(out));
            }
        std::vector<NodeParameter> parameters;
        PortId next = 3;
        for (const auto& item : effect.parameters) {
            if (item.key.empty())
                continue;
            if (item.modulatable())
                // Several modulators can move one knob.
                ports.push_back(port(next++, std::string(item.key), Type::value, Direction::input, Rate::control, 1, true));
            parameters.push_back(parameter(std::string(item.key), item.minimum, item.maximum, item.defaultValue));
        }
        // Additional settings keep deterministic high IDs, so extending the
        // catalogue never shifts the legacy sockets or sidechain endpoint.
        for(std::size_t i=0;i<effect.parameters.size();++i) {
            const auto& item=effect.parameters[i];
            if(item.key.empty()||item.modulatable())continue;
            auto extra=port(1024+i,std::string(item.key),Type::value,Direction::input,Rate::control,1,true,item.defaultValue);
            extra.optional=true;ports.push_back(std::move(extra));
        }
        // Its own key: "sidechain" is already the compressor's detector
        // filter, and two ports of one key make the whole primitive invalid.
        if (effect.sidechain)
            ports.push_back(port(static_cast<PortId>(engine::effectSidechainPortId), "sidechain-in", Type::audio,
                                 Direction::input, Rate::audio, effect.inputChannels));
        const auto category = effect.group == engine::EffectGroup::filter     ? Category::filter
                              : effect.group == engine::EffectGroup::dynamics ? Category::dynamics
                              : effect.group == engine::EffectGroup::channels ? Category::utility
                              : effect.group == engine::EffectGroup::analysis ? Category::analysis
                                                                               : Category::delay;
        // A converter keeps its widths; every other effect takes the width
        // of what it is wired to.
        add(primitive(std::string(effect.processorKey), std::string(effect.displayName), category, std::move(ports),
                      std::move(parameters), true, false, false, !effect.convertsChannels()));
    }
    return registry;
}

const PrimitiveRegistry& factoryPrimitiveRegistry()
{
    static const auto registry = createFactoryPrimitiveRegistry();
    return registry;
}

const char* toString(const PrimitiveCategory category) noexcept
{
    switch (category) {
    case PrimitiveCategory::io: return "io";
    case PrimitiveCategory::generator: return "generator";
    case PrimitiveCategory::voice: return "voice";
    case PrimitiveCategory::envelope: return "envelope";
    case PrimitiveCategory::filter: return "filter";
    case PrimitiveCategory::dynamics: return "dynamics";
    case PrimitiveCategory::delay: return "delay";
    case PrimitiveCategory::mixer: return "mixer";
    case PrimitiveCategory::midi: return "midi";
    case PrimitiveCategory::modulation: return "modulation";
    case PrimitiveCategory::math: return "math";
    case PrimitiveCategory::transport: return "transport";
    case PrimitiveCategory::analysis: return "analysis";
    case PrimitiveCategory::utility: return "utility";
    }
    return "utility";
}

} // namespace cydaw::graph
