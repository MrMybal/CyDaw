#include "NativeDspGraphState.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace cydaw::app {
namespace {

juce::var serialiseParameters(
    const cydaw::engine::NativeDspNodeParameters& parameters)
{
    auto value = juce::DynamicObject::Ptr(new juce::DynamicObject());
#define CYDAW_WRITE_PARAMETER(name) value->setProperty(#name, parameters.name)
    CYDAW_WRITE_PARAMETER(driveDb);
    CYDAW_WRITE_PARAMETER(gainDb);
    CYDAW_WRITE_PARAMETER(cutoffHz);
    CYDAW_WRITE_PARAMETER(resonance);
    CYDAW_WRITE_PARAMETER(highpassCutoffHz);
    CYDAW_WRITE_PARAMETER(clipThreshold);
    CYDAW_WRITE_PARAMETER(stereoWidth);
    CYDAW_WRITE_PARAMETER(pan);
    CYDAW_WRITE_PARAMETER(oscillatorFrequencyHz);
    CYDAW_WRITE_PARAMETER(noiseLevel);
    CYDAW_WRITE_PARAMETER(delayMilliseconds);
    CYDAW_WRITE_PARAMETER(feedback);
    CYDAW_WRITE_PARAMETER(mix);
    CYDAW_WRITE_PARAMETER(roomSize);
    CYDAW_WRITE_PARAMETER(thresholdDb);
    CYDAW_WRITE_PARAMETER(ratio);
    CYDAW_WRITE_PARAMETER(tone);
    // An EQ band's, a wavetable's, a string's and a voice's: left out once,
    // they came back at their defaults in an exported plug-in.
    CYDAW_WRITE_PARAMETER(bandFrequencyHz);
    CYDAW_WRITE_PARAMETER(bandGainDb);
    CYDAW_WRITE_PARAMETER(bandQ);
    CYDAW_WRITE_PARAMETER(wavetablePosition);
    CYDAW_WRITE_PARAMETER(wavetableDetuneCents);
    CYDAW_WRITE_PARAMETER(stringDamping);
    CYDAW_WRITE_PARAMETER(stringDecaySeconds);
    CYDAW_WRITE_PARAMETER(voiceMode);
    CYDAW_WRITE_PARAMETER(glideMilliseconds);
    CYDAW_WRITE_PARAMETER(synthModel);
    CYDAW_WRITE_PARAMETER(oscillatorWaveform);
    CYDAW_WRITE_PARAMETER(oscillatorLevel);
    CYDAW_WRITE_PARAMETER(attackMilliseconds);
    CYDAW_WRITE_PARAMETER(decayMilliseconds);
    CYDAW_WRITE_PARAMETER(sustainLevel);
    CYDAW_WRITE_PARAMETER(releaseMilliseconds);
    CYDAW_WRITE_PARAMETER(modulationRateHz);
    CYDAW_WRITE_PARAMETER(modulationDepth);
    CYDAW_WRITE_PARAMETER(modulationOffset);
    CYDAW_WRITE_PARAMETER(modulationShape);
    CYDAW_WRITE_PARAMETER(valueOperand);
    CYDAW_WRITE_PARAMETER(valueInputA);
    CYDAW_WRITE_PARAMETER(valueInputB);
    CYDAW_WRITE_PARAMETER(valueMinimum);
    CYDAW_WRITE_PARAMETER(valueMaximum);
    CYDAW_WRITE_PARAMETER(mapInputMinimum);
    CYDAW_WRITE_PARAMETER(mapInputMaximum);
    CYDAW_WRITE_PARAMETER(mapOutputMinimum);
    CYDAW_WRITE_PARAMETER(mapOutputMaximum);
    CYDAW_WRITE_PARAMETER(followerAttackSeconds);
    CYDAW_WRITE_PARAMETER(followerReleaseSeconds);
    CYDAW_WRITE_PARAMETER(midiTranspose);
    CYDAW_WRITE_PARAMETER(midiMinimumNote);
    CYDAW_WRITE_PARAMETER(midiMaximumNote);
    CYDAW_WRITE_PARAMETER(midiVelocityScale);
    CYDAW_WRITE_PARAMETER(effect1);
    CYDAW_WRITE_PARAMETER(effect2);
    CYDAW_WRITE_PARAMETER(effect3);
    CYDAW_WRITE_PARAMETER(effect4);
    CYDAW_WRITE_PARAMETER(effect5);
    CYDAW_WRITE_PARAMETER(effect6);
    CYDAW_WRITE_PARAMETER(effect7);
    CYDAW_WRITE_PARAMETER(effect8);
#undef CYDAW_WRITE_PARAMETER
    writeEqBands(*value, parameters);
    return juce::var(value.get());
}

cydaw::engine::NativeDspNodeParameters restoreParameters(
    const juce::var& state)
{
    cydaw::engine::NativeDspNodeParameters parameters;
    const auto* value = state.getDynamicObject();
    if (value == nullptr)
        return parameters;
// A value that is no number (a corrupt state) keeps the default.
#define CYDAW_READ_PARAMETER(name)                                      \
    if (value->hasProperty(#name))                                      \
        if (const auto read = static_cast<double>(value->getProperty(#name)); std::isfinite(read)) \
            parameters.name = static_cast<float>(read)
    CYDAW_READ_PARAMETER(driveDb);
    CYDAW_READ_PARAMETER(gainDb);
    CYDAW_READ_PARAMETER(cutoffHz);
    CYDAW_READ_PARAMETER(resonance);
    CYDAW_READ_PARAMETER(highpassCutoffHz);
    CYDAW_READ_PARAMETER(clipThreshold);
    CYDAW_READ_PARAMETER(stereoWidth);
    CYDAW_READ_PARAMETER(pan);
    CYDAW_READ_PARAMETER(oscillatorFrequencyHz);
    CYDAW_READ_PARAMETER(noiseLevel);
    CYDAW_READ_PARAMETER(delayMilliseconds);
    CYDAW_READ_PARAMETER(feedback);
    CYDAW_READ_PARAMETER(mix);
    CYDAW_READ_PARAMETER(roomSize);
    CYDAW_READ_PARAMETER(thresholdDb);
    CYDAW_READ_PARAMETER(ratio);
    CYDAW_READ_PARAMETER(tone);
    CYDAW_READ_PARAMETER(bandFrequencyHz);
    CYDAW_READ_PARAMETER(bandGainDb);
    CYDAW_READ_PARAMETER(bandQ);
    CYDAW_READ_PARAMETER(wavetablePosition);
    CYDAW_READ_PARAMETER(wavetableDetuneCents);
    CYDAW_READ_PARAMETER(stringDamping);
    CYDAW_READ_PARAMETER(stringDecaySeconds);
    CYDAW_READ_PARAMETER(voiceMode);
    CYDAW_READ_PARAMETER(glideMilliseconds);
    CYDAW_READ_PARAMETER(synthModel);
    CYDAW_READ_PARAMETER(oscillatorWaveform);
    CYDAW_READ_PARAMETER(oscillatorLevel);
    CYDAW_READ_PARAMETER(attackMilliseconds);
    CYDAW_READ_PARAMETER(decayMilliseconds);
    CYDAW_READ_PARAMETER(sustainLevel);
    CYDAW_READ_PARAMETER(releaseMilliseconds);
    CYDAW_READ_PARAMETER(modulationRateHz);
    CYDAW_READ_PARAMETER(modulationDepth);
    CYDAW_READ_PARAMETER(modulationOffset);
    CYDAW_READ_PARAMETER(modulationShape);
    CYDAW_READ_PARAMETER(valueOperand);
    CYDAW_READ_PARAMETER(valueInputA);
    CYDAW_READ_PARAMETER(valueInputB);
    if (!value->hasProperty("valueInputB")
        && value->hasProperty("valueOperand"))
        parameters.valueInputB = parameters.valueOperand;
    CYDAW_READ_PARAMETER(valueMinimum);
    CYDAW_READ_PARAMETER(valueMaximum);
    CYDAW_READ_PARAMETER(mapInputMinimum);
    CYDAW_READ_PARAMETER(mapInputMaximum);
    CYDAW_READ_PARAMETER(mapOutputMinimum);
    CYDAW_READ_PARAMETER(mapOutputMaximum);
    CYDAW_READ_PARAMETER(followerAttackSeconds);
    CYDAW_READ_PARAMETER(followerReleaseSeconds);
    CYDAW_READ_PARAMETER(midiTranspose);
    CYDAW_READ_PARAMETER(midiMinimumNote);
    CYDAW_READ_PARAMETER(midiMaximumNote);
    CYDAW_READ_PARAMETER(midiVelocityScale);
    CYDAW_READ_PARAMETER(effect1);
    CYDAW_READ_PARAMETER(effect2);
    CYDAW_READ_PARAMETER(effect3);
    CYDAW_READ_PARAMETER(effect4);
    CYDAW_READ_PARAMETER(effect5);
    CYDAW_READ_PARAMETER(effect6);
    CYDAW_READ_PARAMETER(effect7);
    CYDAW_READ_PARAMETER(effect8);
#undef CYDAW_READ_PARAMETER
    readEqBands(*value, parameters);
    return parameters;
}

} // namespace

void writeEqBands(juce::DynamicObject& object, const cydaw::engine::NativeDspNodeParameters& parameters)
{
    if (std::all_of(parameters.eqBands.begin(), parameters.eqBands.end(), [](const float value) { return value == 0.0f; }))
        return;
    juce::Array<juce::var> bands;
    for (const auto value : parameters.eqBands)
        bands.add(value);
    object.setProperty("eqBands", bands);
}

void readEqBands(const juce::DynamicObject& object, cydaw::engine::NativeDspNodeParameters& parameters)
{
    const auto* bands = object.getProperty("eqBands").getArray();
    if (bands == nullptr)
        return;
    const auto count = std::min<std::size_t>(parameters.eqBands.size(), static_cast<std::size_t>(bands->size()));
    for (std::size_t index = 0; index < count; ++index) {
        const auto value = static_cast<float>(static_cast<double>((*bands)[static_cast<int>(index)]));
        parameters.eqBands[index] = std::isfinite(value) ? value : 0.0f;
    }
}

juce::var serialiseNativeDspGraphState(
    const cydaw::engine::NativeDspGraph& graph)
{
    auto root = juce::DynamicObject::Ptr(new juce::DynamicObject());
    root->setProperty("schema", 3);
    root->setProperty("outputNodeId",
                      static_cast<juce::int64>(graph.outputNodeId));
    juce::Array<juce::var> nodes;
    for (const auto& node : graph.nodes) {
        auto value = juce::DynamicObject::Ptr(new juce::DynamicObject());
        value->setProperty("id", static_cast<juce::int64>(node.id));
        value->setProperty("kind", static_cast<int>(node.kind));
        value->setProperty("parameters", serialiseParameters(node.parameters));
        if (node.inputBus != 0)
            value->setProperty("inputBus", static_cast<int>(node.inputBus));
        nodes.add(juce::var(value.get()));
    }
    root->setProperty("nodes", std::move(nodes));
    juce::Array<juce::var> connections;
    for (const auto& connection : graph.connections) {
        auto value = juce::DynamicObject::Ptr(new juce::DynamicObject());
        value->setProperty("source",
                           static_cast<juce::int64>(connection.source));
        value->setProperty("destination",
                           static_cast<juce::int64>(connection.destination));
        value->setProperty("sourcePort",
                           static_cast<juce::int64>(connection.sourcePort));
        value->setProperty("destinationPort",
                           static_cast<juce::int64>(connection.destinationPort));
        connections.add(juce::var(value.get()));
    }
    root->setProperty("connections", std::move(connections));
    juce::Array<juce::var> modulations;
    for (const auto& connection : graph.modulations) {
        auto value = juce::DynamicObject::Ptr(new juce::DynamicObject());
        value->setProperty("source",
                           static_cast<juce::int64>(connection.source));
        value->setProperty("destination",
                           static_cast<juce::int64>(connection.destination));
        value->setProperty("sourcePort",
                           static_cast<juce::int64>(connection.sourcePort));
        value->setProperty("destinationPort",
                           static_cast<juce::int64>(connection.destinationPort));
        value->setProperty("destinationParameterKey",
                           juce::String(connection.destinationParameterKey));
        if (connection.mode != 0) {
            value->setProperty("mode", static_cast<int>(connection.mode));
            value->setProperty("amount", static_cast<double>(connection.amount));
            value->setProperty("minimum", static_cast<double>(connection.minimum));
            value->setProperty("maximum", static_cast<double>(connection.maximum));
        }
        modulations.add(juce::var(value.get()));
    }
    root->setProperty("modulations", std::move(modulations));
    juce::Array<juce::var> bypasses;
    for(const auto& b:graph.bypasses){auto* v=new juce::DynamicObject();v->setProperty("node",static_cast<juce::int64>(b.node));v->setProperty("from",static_cast<juce::int64>(b.from));bypasses.add(juce::var(v));}
    root->setProperty("bypasses",bypasses);
    return juce::var(root.get());
}

std::optional<cydaw::engine::NativeDspGraph> restoreNativeDspGraphState(
    const juce::var& state, juce::String& error)
{
    const auto* root = state.getDynamicObject();
    const auto* nodeValues = root != nullptr
                                 ? root->getProperty("nodes").getArray()
                                 : nullptr;
    const auto* connectionValues = root != nullptr
                                       ? root->getProperty("connections").getArray()
                                       : nullptr;
    const auto schema = root != nullptr
        ? static_cast<int>(root->getProperty("schema")) : 0;
    const auto* modulationValues = root != nullptr && schema >= 3
        ? root->getProperty("modulations").getArray() : nullptr;
    if (root == nullptr || nodeValues == nullptr || connectionValues == nullptr
        || (schema >= 3 && modulationValues == nullptr)
        || schema < 1 || schema > 3) {
        error = "Unsupported native Graph state";
        return {};
    }
    if (nodeValues->isEmpty() || nodeValues->size() > 4'096
        || connectionValues->size() > 16'384
        || (modulationValues != nullptr && modulationValues->size() > 16'384)) {
        error = "Native Graph state exceeds the supported limits";
        return {};
    }

    cydaw::engine::NativeDspGraph graph;
    graph.outputNodeId = static_cast<std::uint64_t>(
        static_cast<juce::int64>(root->getProperty("outputNodeId")));
    std::unordered_set<std::uint64_t> nodeIds;
    for (const auto& item : *nodeValues) {
        const auto* value = item.getDynamicObject();
        const auto id = value != nullptr
                            ? static_cast<std::uint64_t>(static_cast<juce::int64>(
                                  value->getProperty("id")))
                            : 0;
        const auto kind = value != nullptr
                              ? static_cast<int>(value->getProperty("kind"))
                              : -1;
        if (id == 0 || !nodeIds.insert(id).second || kind < 0
            || kind > static_cast<int>(cydaw::engine::lastNativeDspNodeKind)) {
            error = "Native Graph state contains an invalid node";
            return {};
        }
        graph.nodes.push_back({
            id, static_cast<cydaw::engine::NativeDspNodeKind>(kind),
            restoreParameters(value->getProperty("parameters")) });
        graph.nodes.back().inputBus = static_cast<std::uint32_t>(
            juce::jlimit(0, 16, static_cast<int>(value->getProperty("inputBus"))));
    }
    for (const auto& item : *connectionValues) {
        const auto* value = item.getDynamicObject();
        const auto source = value != nullptr
                                ? static_cast<std::uint64_t>(
                                      static_cast<juce::int64>(
                                          value->getProperty("source")))
                                : 0;
        const auto destination = value != nullptr
                                     ? static_cast<std::uint64_t>(
                                           static_cast<juce::int64>(
                                               value->getProperty("destination")))
                                     : 0;
        const auto sourcePort = value != nullptr && schema >= 2
            ? static_cast<std::uint64_t>(static_cast<juce::int64>(
                  value->getProperty("sourcePort"))) : 0;
        const auto destinationPort = value != nullptr && schema >= 2
            ? static_cast<std::uint64_t>(static_cast<juce::int64>(
                  value->getProperty("destinationPort"))) : 0;
        if (!nodeIds.contains(source) || !nodeIds.contains(destination)) {
            error = "Native Graph state contains an invalid connection";
            return {};
        }
        graph.connections.push_back(
            { source, destination, sourcePort, destinationPort });
    }
    if (modulationValues != nullptr)
        for (const auto& item : *modulationValues) {
            const auto* value = item.getDynamicObject();
            const auto source = value != nullptr
                ? static_cast<std::uint64_t>(static_cast<juce::int64>(
                      value->getProperty("source"))) : 0;
            const auto destination = value != nullptr
                ? static_cast<std::uint64_t>(static_cast<juce::int64>(
                      value->getProperty("destination"))) : 0;
            if (!nodeIds.contains(source) || !nodeIds.contains(destination)) {
                error = "Native Graph state contains an invalid modulation";
                return {};
            }
            graph.modulations.push_back({
                source, destination,
                static_cast<std::uint64_t>(static_cast<juce::int64>(
                    value->getProperty("sourcePort"))),
                static_cast<std::uint64_t>(static_cast<juce::int64>(
                    value->getProperty("destinationPort"))),
                value->getProperty("destinationParameterKey").toString()
                    .toStdString()
            });
            if (value->hasProperty("mode")) {
                auto& restored = graph.modulations.back();
                restored.mode = static_cast<std::uint8_t>(juce::jlimit(0, 1, static_cast<int>(value->getProperty("mode"))));
                const auto finite = [value](const char* key, const float otherwise) {
                    const auto read = static_cast<double>(value->getProperty(key));
                    return std::isfinite(read) ? static_cast<float>(read) : otherwise;
                };
                restored.amount = finite("amount", 0.0f);
                restored.minimum = finite("minimum", 0.0f);
                restored.maximum = finite("maximum", 1.0f);
            }
        }
    if(const auto* bypasses=root->getProperty("bypasses").getArray())for(const auto& b:*bypasses){
        const auto node=static_cast<std::uint64_t>(static_cast<juce::int64>(b.getProperty("node",0)));
        const auto from=static_cast<std::uint64_t>(static_cast<juce::int64>(b.getProperty("from",0)));
        if(!nodeIds.contains(node)||(from&&!nodeIds.contains(from))){error="Invalid bypass route";return {};}
        graph.bypasses.push_back({node,from});
    }
    if (!nodeIds.contains(graph.outputNodeId)) {
        error = "Native Graph state has no valid output";
        return {};
    }
    error.clear();
    return graph;
}

} // namespace cydaw::app
