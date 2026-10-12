#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace cydaw::engine {

using NodeId = std::uint64_t;

enum class PortKind : std::uint8_t {
    audio,
    midi,
    control,
    transport
};

struct PortRef {
    NodeId nodeId {};
    std::uint32_t portIndex {};
    PortKind kind { PortKind::audio };

    [[nodiscard]] bool operator==(const PortRef&) const = default;
};

struct Connection {
    PortRef source;
    PortRef destination;

    [[nodiscard]] bool operator==(const Connection&) const = default;
};

struct NodeDescriptor {
    NodeId id {};
    std::string name;
    std::uint32_t audioInputs {};
    std::uint32_t audioOutputs {};
    std::uint32_t midiInputs {};
    std::uint32_t midiOutputs {};
    std::uint32_t controlInputs {};
    std::uint32_t controlOutputs {};
    std::uint32_t transportInputs {};
    std::uint32_t transportOutputs {};
};

enum class GraphError : std::uint8_t {
    none,
    invalidNodeId,
    nodeAlreadyExists,
    nodeNotFound,
    invalidPort,
    incompatiblePortKinds,
    duplicateConnection,
    connectionNotFound,
    cycleDetected
};

struct GraphResult {
    GraphError error { GraphError::none };
    std::string message;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == GraphError::none;
    }
};

struct CompiledGraph {
    std::uint64_t generation {};
    std::vector<NodeId> processingOrder;
    std::vector<Connection> connections;
};

struct CompileResult : GraphResult {
    std::optional<CompiledGraph> graph;
};

// Mutable project-side graph. It is compiled away from the real-time audio
// thread into an immutable processing plan.
class AudioGraph final {
public:
    [[nodiscard]] GraphResult addNode(NodeDescriptor node);
    [[nodiscard]] GraphResult removeNode(NodeId nodeId);
    [[nodiscard]] GraphResult connect(Connection connection);
    [[nodiscard]] GraphResult disconnect(const Connection& connection);

    [[nodiscard]] CompileResult compile() const;

    [[nodiscard]] const NodeDescriptor* findNode(NodeId nodeId) const noexcept;
    [[nodiscard]] const std::vector<Connection>& connections() const noexcept;
    [[nodiscard]] std::size_t nodeCount() const noexcept;
    [[nodiscard]] std::uint64_t generation() const noexcept;

private:
    [[nodiscard]] GraphResult validatePort(const PortRef& port, bool isOutput) const;

    std::unordered_map<NodeId, NodeDescriptor> nodes_;
    std::vector<Connection> connections_;
    std::uint64_t generation_ {};
};

} // namespace cydaw::engine
