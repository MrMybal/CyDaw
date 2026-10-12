#include "TrackModulation.h"

#include <cydaw/engine/EffectCatalogue.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <utility>

namespace cydaw::v2::modulation {
namespace {

double number(const juce::DynamicObject& object, const char* key, const double fallback)
{
    return object.hasProperty(key) ? static_cast<double>(object.getProperty(key)) : fallback;
}

} // namespace

std::vector<Assignment> readAssignments(const juce::var& array)
{
    std::vector<Assignment> out;
    if (const auto* values = array.getArray())
        for (const auto& value : *values)
            if (const auto* object = value.getDynamicObject()) {
                Assignment assignment;
                assignment.binding = object->getProperty("binding").toString();
                assignment.label = object->getProperty("label").toString();
                assignment.base = number(*object, "base", 0.0);
                assignment.minimum = number(*object, "minimum", 0.0);
                assignment.maximum = number(*object, "maximum", 1.0);
                assignment.amount = number(*object, "amount", 0.25);
                assignment.enabled = static_cast<bool>(object->getProperty("enabled"));
                if (assignment.binding.isNotEmpty())
                    out.push_back(std::move(assignment));
            }
    return out;
}

juce::var writeAssignments(const std::vector<Assignment>& assignments)
{
    juce::Array<juce::var> values;
    for (const auto& assignment : assignments) {
        auto object = juce::DynamicObject::Ptr(new juce::DynamicObject());
        object->setProperty("binding", assignment.binding);
        object->setProperty("label", assignment.label);
        object->setProperty("base", assignment.base);
        object->setProperty("minimum", assignment.minimum);
        object->setProperty("maximum", assignment.maximum);
        object->setProperty("amount", assignment.amount);
        object->setProperty("enabled", assignment.enabled);
        values.add(juce::var(object.get()));
    }
    return juce::var(values);
}

double evaluate(const juce::var& state, const double timeSeconds)
{
    struct ValueNode {
        std::uint64_t id {};
        juce::String type;
        float rate { 1.0f };
        float depth { 1.0f };
        float offset {};
        int shape {};
        float operand {};
        float minimum { -1.0f };
        float maximum { 1.0f };
    };
    struct ValueLink {
        std::uint64_t source {};
        std::uint64_t destination {};
    };
    const auto* root = state.getDynamicObject();
    if (root == nullptr)
        return 0.0;
    std::vector<ValueNode> nodes;
    std::vector<ValueLink> links;
    if (const auto* values = root->getProperty("nodes").getArray()) {
        for (const auto& value : *values) {
            const auto* object = value.getDynamicObject();
            if (object == nullptr)
                continue;
            ValueNode node;
            node.id = static_cast<std::uint64_t>(static_cast<juce::int64>(object->getProperty("id")));
            node.type = object->getProperty("type").toString();
            if (const auto* parameters = object->getProperty("parameters").getDynamicObject()) {
                node.rate = juce::jlimit(0.01f, 40.0f, static_cast<float>(number(*parameters, "modulationRateHz", 1.0)));
                node.depth = juce::jlimit(0.0f, 1.0f, static_cast<float>(number(*parameters, "modulationDepth", 1.0)));
                node.offset = juce::jlimit(-1.0f, 1.0f, static_cast<float>(number(*parameters, "modulationOffset", 0.0)));
                node.shape = juce::jlimit(0, 3, static_cast<int>(std::lround(number(*parameters, "modulationShape", 0.0))));
                node.operand = static_cast<float>(number(*parameters, "valueOperand", 0.0));
                node.minimum = static_cast<float>(number(*parameters, "valueMinimum", -1.0));
                node.maximum = static_cast<float>(number(*parameters, "valueMaximum", 1.0));
            }
            nodes.push_back(std::move(node));
        }
    }
    if (const auto* values = root->getProperty("connections").getArray())
        for (const auto& value : *values)
            if (const auto* object = value.getDynamicObject())
                links.push_back({ static_cast<std::uint64_t>(static_cast<juce::int64>(object->getProperty("source"))),
                                  static_cast<std::uint64_t>(static_cast<juce::int64>(object->getProperty("destination"))) });

    std::vector<std::uint64_t> visiting;
    std::function<double(std::uint64_t)> evaluateNode = [&](const std::uint64_t nodeId) -> double {
        if (std::find(visiting.begin(), visiting.end(), nodeId) != visiting.end())
            return 0.0;
        const auto found = std::find_if(nodes.begin(), nodes.end(), [nodeId](const auto& node) { return node.id == nodeId; });
        if (found == nodes.end())
            return 0.0;
        visiting.push_back(nodeId);
        const auto incoming = std::find_if(links.begin(), links.end(), [nodeId](const auto& link) { return link.destination == nodeId; });
        auto result = incoming != links.end() ? evaluateNode(incoming->source) : 0.0;
        const auto& type = found->type;
        if (type == "lfo") {
            const auto phase = std::fmod(std::max(0.0, timeSeconds) * found->rate, 1.0);
            auto waveform = std::sin(juce::MathConstants<double>::twoPi * phase);
            if (found->shape == 1)
                waveform = 1.0 - 4.0 * std::abs(phase - 0.5);
            else if (found->shape == 2)
                waveform = phase * 2.0 - 1.0;
            else if (found->shape == 3)
                waveform = phase < 0.5 ? 1.0 : -1.0;
            result = found->offset + waveform * found->depth;
        } else if (type == "macro") {
            result = found->offset;
        } else if (type == "control-math") {
            result = result * found->depth + found->offset;
        } else if (type == "value-constant") {
            result = found->operand;
        } else if (type == "value-add") {
            result += found->operand;
        } else if (type == "value-subtract") {
            result -= found->operand;
        } else if (type == "value-multiply") {
            result *= found->operand;
        } else if (type == "value-divide") {
            result = std::abs(found->operand) > 1.0e-12 ? result / found->operand : 0.0;
        } else if (type == "value-modulo") {
            result = std::abs(found->operand) > 1.0e-12 ? std::fmod(result, found->operand) : 0.0;
        } else if (type == "value-power") {
            result = std::pow(result, found->operand);
        } else if (type == "value-sine") {
            result = std::sin(result);
        } else if (type == "value-cosine") {
            result = std::cos(result);
        } else if (type == "value-square-root") {
            result = result >= 0.0 ? std::sqrt(result) : 0.0;
        } else if (type == "value-floor") {
            result = std::floor(result);
        } else if (type == "value-ceil") {
            result = std::ceil(result);
        } else if (type == "value-round") {
            result = std::round(result);
        } else if (type == "value-min") {
            result = std::min(result, static_cast<double>(found->operand));
        } else if (type == "value-max") {
            result = std::max(result, static_cast<double>(found->operand));
        } else if (type == "value-clamp") {
            result = juce::jlimit(static_cast<double>(std::min(found->minimum, found->maximum)),
                                  static_cast<double>(std::max(found->minimum, found->maximum)), result);
        } else if (type == "value-abs") {
            result = std::abs(result);
        } else if (type == "value-negate") {
            result = -result;
        } else if (type == "logic-greater") {
            result = result > found->operand ? 1.0 : 0.0;
        } else if (type == "logic-less") {
            result = result < found->operand ? 1.0 : 0.0;
        } else if (type == "logic-equal") {
            result = std::abs(result - found->operand) <= 1.0e-6 ? 1.0 : 0.0;
        } else if (type == "logic-and") {
            result = result > 0.5 && found->operand > 0.5 ? 1.0 : 0.0;
        } else if (type == "logic-or") {
            result = result > 0.5 || found->operand > 0.5 ? 1.0 : 0.0;
        } else if (type == "logic-xor") {
            result = (result > 0.5) != (found->operand > 0.5) ? 1.0 : 0.0;
        } else if (type == "logic-not") {
            result = result > 0.5 ? 0.0 : 1.0;
        }
        visiting.pop_back();
        return std::isfinite(result) ? result : 0.0;
    };
    const auto output = std::find_if(nodes.begin(), nodes.end(), [](const auto& node) { return node.type == "graph-value-output"; });
    if (output != nodes.end())
        return evaluateNode(output->id);
    const auto terminal = std::find_if(nodes.rbegin(), nodes.rend(), [](const auto& node) {
        return node.type == "lfo" || node.type == "macro" || node.type == "control-math" || node.type.startsWith("value-")
               || node.type.startsWith("logic-");
    });
    return terminal != nodes.rend() ? evaluateNode(terminal->id) : 0.0;
}

namespace {

// The native parameter a binding's suffix names, with its range: a field, or
// a catalogue parameter wherever it lives.
struct Field {
    float engine::NativeDspNodeParameters::* member {};
    float minimum {};
    float maximum {};
    const engine::EffectParameter* effect {};

    [[nodiscard]] float read(const engine::NativeDspNodeParameters& parameters) const noexcept
    {
        return effect != nullptr ? engine::effectValue(parameters, *effect) : parameters.*member;
    }
    void write(engine::NativeDspNodeParameters& parameters, const float value) const noexcept
    {
        if (effect != nullptr)
            engine::setEffectValue(parameters, *effect, value);
        else
            parameters.*member = value;
    }
};

std::optional<Field> fieldFor(const juce::String& suffix)
{
    using P = engine::NativeDspNodeParameters;
    if (suffix == "drive.db")
        return Field { &P::driveDb, 0.0f, 36.0f };
    if (suffix == "gain.db")
        return Field { &P::gainDb, -120.0f, 24.0f };
    if (suffix == "lowpass.cutoff" || suffix == "voiceFilter.cutoff")
        return Field { &P::cutoffHz, 20.0f, 20'000.0f };
    if (suffix == "lowpass.resonance" || suffix == "voiceFilter.resonance")
        return Field { &P::resonance, 0.1f, 20.0f };
    if (suffix == "highpass.cutoff")
        return Field { &P::highpassCutoffHz, 10.0f, 20'000.0f };
    if (suffix == "clip.threshold")
        return Field { &P::clipThreshold, 0.001f, 1.0f };
    if (suffix == "stereo.width")
        return Field { &P::stereoWidth, 0.0f, 2.0f };
    if (suffix == "oscillator.frequency")
        return Field { &P::oscillatorFrequencyHz, 20.0f, 20'000.0f };
    if (suffix == "noise.level")
        return Field { &P::noiseLevel, 0.0f, 1.0f };
    if (suffix == "delay.time")
        return Field { &P::delayMilliseconds, 1.0f, 2'000.0f };
    if (suffix == "delay.feedback")
        return Field { &P::feedback, 0.0f, 0.94f };
    if (suffix == "delay.mix" || suffix == "reverb.mix")
        return Field { &P::mix, 0.0f, 1.0f };
    if (suffix == "reverb.roomSize")
        return Field { &P::roomSize, 0.0f, 0.95f };
    if (suffix == "compressor.threshold")
        return Field { &P::thresholdDb, -60.0f, 0.0f };
    if (suffix == "compressor.ratio")
        return Field { &P::ratio, 1.0f, 20.0f };
    if (suffix == "eq.tone")
        return Field { &P::tone, -1.0f, 1.0f };
    if (const auto* effect = engine::effectParameterForSuffix(suffix.toStdString()))
        return Field { nullptr, effect->minimum, effect->maximum, effect };
    return std::nullopt;
}

// node.<id>.<suffix>: the node and the field.
template <typename Graph>
auto resolve(Graph& graph, const juce::String& binding)
    -> std::optional<std::pair<decltype(&graph.nodes.front().parameters), Field>>
{
    if (!binding.startsWith("node."))
        return std::nullopt;
    const auto separator = binding.indexOfChar(5, '.');
    if (separator < 0)
        return std::nullopt;
    const auto nodeId = static_cast<std::uint64_t>(binding.substring(5, separator).getLargeIntValue());
    const auto field = fieldFor(binding.substring(separator + 1));
    if (!field)
        return std::nullopt;
    for (auto& node : graph.nodes)
        if (node.id == nodeId)
            return std::make_pair(&node.parameters, *field);
    return std::nullopt;
}

} // namespace

std::optional<NativeTarget> nativeTargetFor(const juce::String& binding)
{
    if (!binding.startsWith("node."))
        return std::nullopt;
    const auto separator = binding.indexOfChar(5, '.');
    if (separator < 0)
        return std::nullopt;
    const auto field = fieldFor(binding.substring(separator + 1));
    if (!field)
        return std::nullopt;
    NativeTarget target;
    target.nodeId = static_cast<std::uint64_t>(binding.substring(5, separator).getLargeIntValue());
    target.field = field->effect != nullptr ? field->effect->field : field->member;
    target.slot = field->effect != nullptr && field->effect->field == nullptr ? field->effect->slot : -1;
    target.minimum = field->minimum;
    target.maximum = field->maximum;
    return target;
}

bool applyToNativeGraph(engine::NativeDspGraph& graph, const juce::String& binding, const double value)
{
    const auto found = resolve(graph, binding);
    if (!found)
        return false;
    auto& [parameters, field] = *found;
    field.write(*parameters, juce::jlimit(field.minimum, field.maximum, static_cast<float>(value)));
    return true;
}

std::optional<double> readFromNativeGraph(const engine::NativeDspGraph& graph, const juce::String& binding)
{
    const auto found = resolve(graph, binding);
    if (!found)
        return std::nullopt;
    const auto& [parameters, field] = *found;
    return static_cast<double>(field.read(*parameters));
}

LfoSettings firstLfo(const juce::var& valueGraph)
{
    LfoSettings settings;
    if (const auto* root = valueGraph.getDynamicObject())
        if (const auto* nodes = root->getProperty("nodes").getArray())
            for (const auto& value : *nodes)
                if (const auto* object = value.getDynamicObject(); object != nullptr && object->getProperty("type").toString() == "lfo") {
                    settings.found = true;
                    if (const auto* parameters = object->getProperty("parameters").getDynamicObject()) {
                        settings.rate = number(*parameters, "modulationRateHz", 1.0);
                        settings.depth = number(*parameters, "modulationDepth", 1.0);
                        settings.offset = number(*parameters, "modulationOffset", 0.0);
                        settings.shape = static_cast<int>(std::lround(number(*parameters, "modulationShape", 0.0)));
                    }
                    return settings;
                }
    return settings;
}

bool setFirstLfo(juce::var& valueGraph, const LfoSettings& settings)
{
    if (const auto* root = valueGraph.getDynamicObject())
        if (const auto* nodes = root->getProperty("nodes").getArray())
            for (const auto& value : *nodes)
                if (auto* object = value.getDynamicObject(); object != nullptr && object->getProperty("type").toString() == "lfo") {
                    auto* parameters = object->getProperty("parameters").getDynamicObject();
                    if (parameters == nullptr) {
                        object->setProperty("parameters", juce::var(new juce::DynamicObject()));
                        parameters = object->getProperty("parameters").getDynamicObject();
                    }
                    parameters->setProperty("modulationRateHz", juce::jlimit(0.01, 40.0, settings.rate));
                    parameters->setProperty("modulationDepth", juce::jlimit(0.0, 1.0, settings.depth));
                    parameters->setProperty("modulationOffset", juce::jlimit(-1.0, 1.0, settings.offset));
                    parameters->setProperty("modulationShape", juce::jlimit(0, 3, settings.shape));
                    return true;
                }
    return false;
}

} // namespace cydaw::v2::modulation
