#include <cydaw/graph/GraphPluginExport.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace cydaw::graph {
namespace {

bool isFourCharacterCode(const std::string& code)
{
    return code.size() == 4
           && std::all_of(code.begin(), code.end(), [](const unsigned char value) {
                  return std::isalnum(value) != 0;
              });
}

bool isExportableRole(const GraphRole role)
{
    return role == GraphRole::audioEffect || role == GraphRole::instrument
           || role == GraphRole::analyzer || role == GraphRole::utility;
}

std::string categoryForRole(const GraphRole role)
{
    switch (role) {
    case GraphRole::instrument: return "Instrument|Synth";
    case GraphRole::analyzer: return "Fx|Analyzer";
    case GraphRole::utility: return "Fx|Tools";
    case GraphRole::audioEffect: return "Fx";
    default: return {};
    }
}

void addIssue(PluginExportResult& result, const PluginExportIssueCode code,
              std::string message, const std::optional<PortId> portId = {},
              const std::optional<ParameterId> parameterId = {})
{
    result.issues.push_back(
        { code, std::move(message), portId, parameterId });
}

} // namespace

bool PluginExportResult::ok() const noexcept
{
    return plan.has_value() && issues.empty();
}

PluginExportResult planVst3Export(const GraphLibrary& library,
                                  const GraphReference& graph,
                                  const PluginIdentity& identity)
{
    PluginExportResult result;
    if (identity.productName.empty() || identity.companyName.empty()
        || !isFourCharacterCode(identity.manufacturerCode)
        || !isFourCharacterCode(identity.pluginCode))
        addIssue(result, PluginExportIssueCode::invalidIdentity,
                 "A VST3 export requires product/company names and two distinct four-character alphanumeric codes");
    if (identity.manufacturerCode == identity.pluginCode)
        addIssue(result, PluginExportIssueCode::invalidIdentity,
                 "The VST3 manufacturer and plug-in codes must be distinct");

    const auto* definition = library.resolve(graph);
    if (definition == nullptr) {
        addIssue(result, PluginExportIssueCode::missingGraph,
                 "The requested Graph definition is not installed in the export library");
        return result;
    }

    const auto validation = library.validate(*definition);
    for (const auto& issue : validation.issues)
        if (issue.severity == ValidationSeverity::error)
            addIssue(result, PluginExportIssueCode::invalidGraph, issue.message,
                     issue.portId);

    auto role = definition->role;
    if (role == GraphRole::generic)
        role = inferGraphRole(*definition);
    if (!isExportableRole(role))
        addIssue(result, PluginExportIssueCode::unsupportedRole,
                 "The first VST3 backend exports instruments, audio effects, analyzers and utilities; this Graph role needs a dedicated host adapter");

    PluginExportPlan plan;
    plan.identity = identity;
    plan.graph = graph;
    plan.role = role;
    plan.vst3Category = categoryForRole(role);
    plan.isSynth = role == GraphRole::instrument;
    plan.hasEditor = !definition->interfaces.empty();

    auto audioOutputCount = std::size_t {};
    auto midiInputCount = std::size_t {};
    auto midiOutputCount = std::size_t {};
    for (const auto& port : definition->ports) {
        if (port.type == PortType::audio) {
            const auto channels = channelCount(port.audioType);
            if (port.audioType == AudioPortType::unspecified || channels == 0
                || channels != port.channels) {
                addIssue(result, PluginExportIssueCode::invalidAudioFormat,
                         "Every exported audio bus must use an exact Mono, Stereo or 3-8 channel type",
                         port.id);
                continue;
            }
            plan.buses.push_back({ port.id, port.key, port.displayName,
                                   PluginBusMediaType::audio, port.direction,
                                   port.audioType, channels,
                                   port.direction == PortDirection::input
                                       ? std::none_of(
                                             plan.buses.begin(), plan.buses.end(),
                                             [](const auto& bus) {
                                                 return bus.mediaType
                                                            == PluginBusMediaType::audio
                                                        && bus.direction
                                                               == PortDirection::input;
                                             })
                                       : audioOutputCount == 0 });
            if (port.direction == PortDirection::output)
                ++audioOutputCount;
        } else if (port.type == PortType::midi) {
            auto& directionCount = port.direction == PortDirection::input
                                       ? midiInputCount : midiOutputCount;
            plan.buses.push_back({ port.id, port.key, port.displayName,
                                   PluginBusMediaType::midi, port.direction,
                                   AudioPortType::unspecified, 0,
                                   directionCount++ == 0 });
            plan.acceptsMidi = plan.acceptsMidi
                               || port.direction == PortDirection::input;
            plan.producesMidi = plan.producesMidi
                                || port.direction == PortDirection::output;
        } else if (port.type == PortType::value
                   || port.type == PortType::trigger) {
            addIssue(result, PluginExportIssueCode::unsupportedBoundaryPort,
                     "Public Value and Trigger ports must be exposed as published parameters before VST3 export",
                     port.id);
        }
        // Transport is supplied by the host process context. Analysis is a
        // bounded UI telemetry stream. Neither becomes a VST3 audio/event bus.
    }
    if (audioOutputCount == 0)
        addIssue(result, PluginExportIssueCode::missingAudioOutput,
                 "The VST3 backend requires at least one exact audio output bus");

    std::unordered_set<std::uint32_t> parameterIds;
    for (const auto& parameter : definition->parameters) {
        if (!parameter.exportedToHost)
            continue;
        if (parameter.id == 0
            || parameter.id
                   > static_cast<ParameterId>(
                       std::numeric_limits<std::uint32_t>::max())) {
            addIssue(result, PluginExportIssueCode::invalidParameterId,
                     "Published Graph parameter IDs exported to VST3 must fit in a stable non-zero 32-bit value",
                     {}, parameter.id);
            continue;
        }
        const auto pluginId = static_cast<std::uint32_t>(parameter.id);
        if (!parameterIds.insert(pluginId).second) {
            addIssue(result, PluginExportIssueCode::duplicateParameterId,
                     "Two published Graph parameters map to the same VST3 parameter ID",
                     {}, parameter.id);
            continue;
        }
        plan.parameters.push_back(
            { pluginId, parameter.id, parameter.key, parameter.displayName,
              parameter.unit, parameter.minimum, parameter.maximum,
              parameter.defaultValue, parameter.automatable });
    }

    ValidationResult compileValidation;
    const auto compiled = library.compile(&compileValidation);
    if (!compiled.has_value()) {
        addIssue(result, PluginExportIssueCode::compileFailure,
                 "The Graph library could not produce an immutable export plan");
    } else {
        const auto compiledDefinition = std::find_if(
            compiled->definitions.begin(), compiled->definitions.end(),
            [&graph](const auto& candidate) {
                return candidate.reference == graph;
            });
        if (compiledDefinition == compiled->definitions.end())
            addIssue(result, PluginExportIssueCode::compileFailure,
                     "The compiled Graph library does not contain the requested root definition");
        else
            plan.sourceFingerprint = compiledDefinition->assetFingerprint;
    }

    if (result.issues.empty())
        result.plan = std::move(plan);
    return result;
}

} // namespace cydaw::graph
