#pragma once

#include <cydaw/graph/GraphDefinition.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cydaw::graph {

enum class PluginExportTarget : std::uint8_t {
    vst3
};

struct PluginIdentity {
    std::string productName;
    std::string companyName;
    // Four printable ASCII characters. These become the stable JUCE/VST3
    // manufacturer and plug-in codes of a generated wrapper.
    std::string manufacturerCode;
    std::string pluginCode;
    SemanticVersion version;
};

enum class PluginBusMediaType : std::uint8_t {
    audio,
    midi
};

struct PluginBusDescriptor {
    PortId graphPortId {};
    std::string key;
    std::string displayName;
    PluginBusMediaType mediaType { PluginBusMediaType::audio };
    PortDirection direction { PortDirection::input };
    AudioPortType audioType { AudioPortType::unspecified };
    std::uint16_t channels {};
    bool main {};
};

struct PluginParameterDescriptor {
    std::uint32_t pluginParameterId {};
    ParameterId graphParameterId {};
    std::string key;
    std::string displayName;
    std::string unit;
    double minimum {};
    double maximum { 1.0 };
    double defaultValue {};
    bool automatable { true };
};

struct PluginExportPlan {
    PluginExportTarget target { PluginExportTarget::vst3 };
    PluginIdentity identity;
    GraphReference graph;
    GraphRole role { GraphRole::generic };
    std::string vst3Category;
    bool isSynth {};
    bool acceptsMidi {};
    bool producesMidi {};
    bool hasEditor {};
    std::vector<PluginBusDescriptor> buses;
    std::vector<PluginParameterDescriptor> parameters;
    std::uint64_t sourceFingerprint {};
};

enum class PluginExportIssueCode : std::uint8_t {
    invalidIdentity,
    missingGraph,
    invalidGraph,
    unsupportedRole,
    unsupportedBoundaryPort,
    invalidAudioFormat,
    missingAudioOutput,
    invalidParameterId,
    duplicateParameterId,
    compileFailure
};

struct PluginExportIssue {
    PluginExportIssueCode code { PluginExportIssueCode::invalidGraph };
    std::string message;
    std::optional<PortId> portId;
    std::optional<ParameterId> parameterId;
};

struct PluginExportResult {
    std::optional<PluginExportPlan> plan;
    std::vector<PluginExportIssue> issues;

    [[nodiscard]] bool ok() const noexcept;
    [[nodiscard]] explicit operator bool() const noexcept { return ok(); }
};

// Produces the format-neutral contract consumed by the generated JUCE/VST3
// wrapper. It does not compile source code or touch the file system, so it can
// also be used by a standalone editor or another host embedding CyDAW Graph.
[[nodiscard]] PluginExportResult planVst3Export(
    const GraphLibrary& library, const GraphReference& graph,
    const PluginIdentity& identity);

} // namespace cydaw::graph
