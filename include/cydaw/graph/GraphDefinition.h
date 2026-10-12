#pragma once

#include <cstdint>
#include <compare>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace cydaw::graph {

using NodeId = std::uint64_t;
using PortId = std::uint64_t;
using ParameterId = std::uint64_t;
using ControlId = std::uint64_t;
using InstanceId = std::uint64_t;

inline constexpr NodeId boundaryNodeId = 0;

struct SemanticVersion {
    std::uint16_t major { 1 };
    std::uint16_t minor {};
    std::uint16_t patch {};

    [[nodiscard]] bool operator==(const SemanticVersion&) const = default;
    [[nodiscard]] auto operator<=>(const SemanticVersion&) const = default;
    [[nodiscard]] std::string toString() const;
};

struct GraphReference {
    std::string id;
    SemanticVersion version;

    [[nodiscard]] bool operator==(const GraphReference&) const = default;
    [[nodiscard]] std::string toString() const;
};

enum class GraphRole : std::uint8_t {
    generic,
    audioEffect,
    instrument,
    midiEffect,
    modulator,
    analyzer,
    utility
};

enum class GraphScope : std::uint8_t {
    factory,
    user,
    project,
    inlineTrack,
    inlineBus
};

enum class PortType : std::uint8_t {
    audio,
    midi,
    value,
    trigger,
    transport,
    analysis
};

// Audio topology is part of the port type, not a display hint. `unspecified`
// only exists to migrate schema-1 assets; every newly authored audio port is
// expected to carry one of the exact layouts below.
enum class AudioPortType : std::uint8_t {
    unspecified,
    mono,
    stereo,
    channels3,
    quad,
    channels5,
    channels6,
    channels7,
    channels8
};

enum class PortDirection : std::uint8_t {
    input,
    output
};

enum class PortRate : std::uint8_t {
    audio,
    control,
    event
};

struct GraphPort {
    PortId id {};
    std::string key;
    std::string displayName;
    PortType type { PortType::value };
    AudioPortType audioType { AudioPortType::unspecified };
    PortDirection direction { PortDirection::input };
    PortRate rate { PortRate::control };
    std::uint16_t channels { 1 };
    bool optional {};
    bool acceptsMultipleConnections {};
    double defaultValue {};

    [[nodiscard]] bool operator==(const GraphPort&) const = default;
};

struct NodeParameter {
    std::string key;
    double minimum {};
    double maximum { 1.0 };
    double defaultValue {};

    [[nodiscard]] bool operator==(const NodeParameter&) const = default;
};

enum class GraphNodeKind : std::uint8_t {
    primitive,
    graphInstance
};

struct GraphNode {
    NodeId id {};
    std::string displayName;
    std::string description;
    GraphNodeKind kind { GraphNodeKind::primitive };
    std::string processorKey;
    std::optional<GraphReference> graph;
    std::vector<GraphPort> ports;
    std::vector<NodeParameter> parameters;
    std::unordered_map<std::string, double> parameterOverrides;
    bool breaksFeedbackCycle {};

    [[nodiscard]] bool operator==(const GraphNode&) const = default;
};

struct GraphEndpoint {
    NodeId nodeId { boundaryNodeId };
    PortId portId {};

    [[nodiscard]] bool operator==(const GraphEndpoint&) const = default;
};

struct GraphConnection {
    GraphEndpoint source;
    GraphEndpoint destination;
    // For a control cable onto a parameter: 0 sets it, 1 modulates it by
    // `amount` of its range (see NativeDspModulationConnection).
    std::uint8_t mode {};
    double amount { 0.5 };

    [[nodiscard]] bool operator==(const GraphConnection&) const = default;
};

struct PublishedParameter {
    ParameterId id {};
    std::string key;
    std::string displayName;
    std::string unit;
    double minimum {};
    double maximum { 1.0 };
    double defaultValue {};
    NodeId targetNodeId {};
    std::string targetParameterKey;
    bool automatable { true };
    bool modulatable { true };
    bool visibleInDevice { true };
    bool exportedToHost { true };

    [[nodiscard]] bool operator==(const PublishedParameter&) const = default;
};

enum class InterfaceLayoutMode : std::uint8_t {
    device,
    freeform
};

enum class InterfaceControlKind : std::uint8_t {
    knob,
    fader,
    toggle,
    selector,
    xyPad,
    envelope,
    keyboard,
    waveform,
    spectrum,
    levelMeter,
    text,
    group,
    inputPort,
    outputPort,
    valueDisplay,
    filterResponse,
    dynamicsResponse
};

struct InterfaceControl {
    ControlId id {};
    InterfaceControlKind kind { InterfaceControlKind::knob };
    std::string label;
    std::optional<ParameterId> parameterBinding;
    std::optional<PortId> portBinding;
    float x {};
    float y {};
    float width { 100.0f };
    float height { 100.0f };
    std::int32_t zOrder {};
    // Optional second axis for genuinely two-dimensional controls. Appended
    // to preserve existing aggregate initialisers and authored control ids.
    std::optional<ParameterId> secondaryParameterBinding;
    // Additional stable bindings used by multi-parameter controls. An ADSR
    // envelope uses all four bindings in A/D/S/R order.
    std::optional<ParameterId> tertiaryParameterBinding;
    std::optional<ParameterId> quaternaryParameterBinding;

    [[nodiscard]] bool operator==(const InterfaceControl&) const = default;
};

struct GraphInterface {
    std::string id;
    std::string displayName;
    InterfaceLayoutMode mode { InterfaceLayoutMode::device };
    float width { 640.0f };
    float height { 240.0f };
    std::vector<InterfaceControl> controls;

    [[nodiscard]] bool operator==(const GraphInterface&) const = default;
};

struct GraphDefinition {
    GraphReference reference;
    std::uint32_t schemaVersion { 1 };
    std::string displayName;
    std::string description;
    GraphRole role { GraphRole::generic };
    GraphScope scope { GraphScope::project };
    std::vector<std::string> tags;
    std::vector<GraphPort> ports;
    std::vector<PublishedParameter> parameters;
    std::vector<GraphNode> nodes;
    std::vector<GraphConnection> connections;
    std::vector<GraphInterface> interfaces;

    [[nodiscard]] bool operator==(const GraphDefinition&) const = default;
};

struct GraphInstance {
    InstanceId id {};
    GraphReference definition;
    std::unordered_map<ParameterId, double> parameterOverrides;
    bool bypassed {};
    std::optional<GraphDefinition> embeddedDefinition;
};

enum class ValidationSeverity : std::uint8_t {
    warning,
    error
};

enum class ValidationCode : std::uint8_t {
    invalidIdentity,
    duplicateId,
    duplicateKey,
    invalidRange,
    invalidPort,
    invalidBinding,
    invalidConnection,
    incompatiblePorts,
    multipleInputConnections,
    missingDependency,
    dependencyCycle,
    processingCycle,
    invalidRole,
    invalidInterface
};

struct ValidationIssue {
    ValidationSeverity severity { ValidationSeverity::error };
    ValidationCode code { ValidationCode::invalidIdentity };
    std::string message;
    std::optional<NodeId> nodeId;
    std::optional<PortId> portId;
};

struct ValidationResult {
    std::vector<ValidationIssue> issues;

    [[nodiscard]] bool ok() const noexcept;
    [[nodiscard]] explicit operator bool() const noexcept { return ok(); }
};

struct CompiledDefinition {
    GraphReference reference;
    std::vector<NodeId> processingOrder;
    // Runtime fingerprint deliberately excludes names and interface layout so
    // moving a knob never invalidates a compiled DSP plan.
    std::uint64_t fingerprint {};
    // Full authored asset fingerprint used by generated plug-ins, manifests
    // and content caches.
    std::uint64_t assetFingerprint {};
};

struct CompiledLibrary {
    std::vector<GraphReference> loadOrder;
    std::vector<CompiledDefinition> definitions;
    std::uint64_t fingerprint {};
};

class GraphLibrary final {
public:
    [[nodiscard]] ValidationResult install(GraphDefinition definition,
                                           bool replaceExisting = false);
    [[nodiscard]] ValidationResult installBatch(
        std::vector<GraphDefinition> definitions,
        bool replaceExisting = false);
    [[nodiscard]] bool remove(const GraphReference& reference);
    // Whether this exact definition is installed, as it stands. Installing
    // one the library already holds changes nothing, and costs nothing.
    [[nodiscard]] bool holds(const GraphDefinition& definition) const;
    [[nodiscard]] const GraphDefinition* resolve(
        const GraphReference& reference) const noexcept;
    [[nodiscard]] std::vector<GraphReference> versions(
        const std::string& id) const;
    [[nodiscard]] std::vector<GraphReference> all() const;
    [[nodiscard]] ValidationResult validate(
        const GraphDefinition& definition) const;
    [[nodiscard]] ValidationResult validate(
        const GraphInstance& instance) const;
    [[nodiscard]] std::optional<CompiledLibrary> compile(
        ValidationResult* validation = nullptr) const;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    [[nodiscard]] std::string keyFor(const GraphReference& reference) const;
    // What compile() establishes about one definition — its verdict, its
    // fingerprints, its processing order — kept until that definition, or
    // one it depends on, is installed again or removed. A library whose
    // root is the only thing that changes revalidates the root, not the
    // sixty-four classes behind it, on every compile.
    struct Compiled {
        ValidationResult validation;
        std::uint64_t fingerprint {};
        std::uint64_t assetFingerprint {};
        std::vector<NodeId> processingOrder;
    };
    [[nodiscard]] const Compiled& compiled(const std::string& key,
                                           const GraphDefinition& definition) const;
    void forget(const std::string& key);

    std::unordered_map<std::string, GraphDefinition> definitions_;
    mutable std::unordered_map<std::string, Compiled> compiled_;
};

[[nodiscard]] GraphRole inferGraphRole(const GraphDefinition& definition);
[[nodiscard]] const char* toString(GraphRole role) noexcept;
[[nodiscard]] const char* toString(GraphScope scope) noexcept;
[[nodiscard]] const char* toString(PortType type) noexcept;
[[nodiscard]] const char* toString(AudioPortType type) noexcept;
[[nodiscard]] AudioPortType audioPortTypeForChannels(
    std::uint16_t channels) noexcept;
[[nodiscard]] std::uint16_t channelCount(AudioPortType type) noexcept;

} // namespace cydaw::graph
