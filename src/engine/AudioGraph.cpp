#include <cydaw/engine/AudioGraph.h>

#include <algorithm>
#include <functional>
#include <queue>
#include <tuple>
#include <unordered_set>

namespace cydaw::engine {
namespace {

GraphResult failure(const GraphError error, std::string message)
{
    return { error, std::move(message) };
}

std::uint32_t portCount(const NodeDescriptor& node, const PortKind kind, const bool isOutput)
{
    switch (kind) {
        case PortKind::audio: return isOutput ? node.audioOutputs : node.audioInputs;
        case PortKind::midi: return isOutput ? node.midiOutputs : node.midiInputs;
        case PortKind::control: return isOutput ? node.controlOutputs : node.controlInputs;
        case PortKind::transport: return isOutput ? node.transportOutputs
                                                  : node.transportInputs;
    }

    return 0;
}

} // namespace

GraphResult AudioGraph::addNode(NodeDescriptor node)
{
    if (node.id == 0)
        return failure(GraphError::invalidNodeId, "Node id 0 is reserved");

    if (nodes_.contains(node.id))
        return failure(GraphError::nodeAlreadyExists, "A node with this id already exists");

    nodes_.emplace(node.id, std::move(node));
    ++generation_;
    return {};
}

GraphResult AudioGraph::removeNode(const NodeId nodeId)
{
    if (nodes_.erase(nodeId) == 0)
        return failure(GraphError::nodeNotFound, "Cannot remove an unknown node");

    std::erase_if(connections_, [nodeId](const Connection& connection) {
        return connection.source.nodeId == nodeId || connection.destination.nodeId == nodeId;
    });
    ++generation_;
    return {};
}

GraphResult AudioGraph::validatePort(const PortRef& port, const bool isOutput) const
{
    const auto* node = findNode(port.nodeId);
    if (node == nullptr)
        return failure(GraphError::nodeNotFound, "A connection references an unknown node");

    if (port.portIndex >= portCount(*node, port.kind, isOutput))
        return failure(GraphError::invalidPort, "A connection references an unavailable port");

    return {};
}

GraphResult AudioGraph::connect(Connection connection)
{
    if (const auto result = validatePort(connection.source, true); !result)
        return result;
    if (const auto result = validatePort(connection.destination, false); !result)
        return result;
    if (connection.source.kind != connection.destination.kind)
        return failure(GraphError::incompatiblePortKinds, "Port kinds must match");

    if (std::find(connections_.begin(), connections_.end(), connection) != connections_.end())
        return failure(GraphError::duplicateConnection, "This connection already exists");

    // Feedback will later be represented by an explicit delay node. Keeping the
    // project graph acyclic makes the first real-time scheduler deterministic.
    connections_.push_back(connection);
    const auto result = compile();
    if (!result) {
        connections_.pop_back();
        return { result.error, result.message };
    }

    ++generation_;
    return {};
}

GraphResult AudioGraph::connectAll(const std::vector<Connection>& connections)
{
    const auto before = connections_.size();
    const auto undo = [this, before](const GraphResult& result) {
        connections_.resize(before);
        return result;
    };
    for (const auto& connection : connections) {
        if (const auto result = validatePort(connection.source, true); !result)
            return undo(result);
        if (const auto result = validatePort(connection.destination, false); !result)
            return undo(result);
        if (connection.source.kind != connection.destination.kind)
            return undo(failure(GraphError::incompatiblePortKinds, "Port kinds must match"));
        connections_.push_back(connection);
    }
    // Duplicates, found once all are in (sorted copies, not each against all).
    auto sorted = std::vector<Connection>(connections_.begin(), connections_.end());
    const auto key = [](const Connection& value) {
        return std::tuple(value.source.nodeId, value.source.portIndex, static_cast<int>(value.source.kind),
                          value.destination.nodeId, value.destination.portIndex, static_cast<int>(value.destination.kind));
    };
    std::sort(sorted.begin(), sorted.end(), [&key](const Connection& a, const Connection& b) { return key(a) < key(b); });
    if (std::adjacent_find(sorted.begin(), sorted.end(), [&key](const Connection& a, const Connection& b) { return key(a) == key(b); })
        != sorted.end())
        return undo(failure(GraphError::duplicateConnection, "This connection already exists"));
    if (const auto result = compile(); !result)
        return undo({ result.error, result.message });
    ++generation_;
    return {};
}

GraphResult AudioGraph::disconnect(const Connection& connection)
{
    const auto position = std::find(connections_.begin(), connections_.end(), connection);
    if (position == connections_.end())
        return failure(GraphError::connectionNotFound, "Cannot remove an unknown connection");

    connections_.erase(position);
    ++generation_;
    return {};
}

CompileResult AudioGraph::compile() const
{
    std::unordered_map<NodeId, std::size_t> incomingEdges;
    std::unordered_map<NodeId, std::vector<NodeId>> outgoingEdges;
    incomingEdges.reserve(nodes_.size());
    outgoingEdges.reserve(nodes_.size());

    for (const auto& [nodeId, unused] : nodes_) {
        (void) unused;
        incomingEdges.emplace(nodeId, 0);
        outgoingEdges.emplace(nodeId, std::vector<NodeId> {});
    }

    // Several port connections between the same nodes are one scheduling edge.
    std::unordered_map<NodeId, std::unordered_set<NodeId>> uniqueEdges;
    for (const auto& connection : connections_) {
        if (uniqueEdges[connection.source.nodeId].insert(connection.destination.nodeId).second) {
            outgoingEdges[connection.source.nodeId].push_back(connection.destination.nodeId);
            ++incomingEdges[connection.destination.nodeId];
        }
    }

    std::priority_queue<NodeId, std::vector<NodeId>, std::greater<>> ready;
    for (const auto& [nodeId, incoming] : incomingEdges) {
        if (incoming == 0)
            ready.push(nodeId);
    }

    CompiledGraph compiled;
    compiled.generation = generation_;
    compiled.connections = connections_;
    compiled.processingOrder.reserve(nodes_.size());

    while (!ready.empty()) {
        const auto nodeId = ready.top();
        ready.pop();
        compiled.processingOrder.push_back(nodeId);

        for (const auto destination : outgoingEdges[nodeId]) {
            auto& incoming = incomingEdges[destination];
            if (--incoming == 0)
                ready.push(destination);
        }
    }

    if (compiled.processingOrder.size() != nodes_.size())
        return { { GraphError::cycleDetected, "The graph contains a cycle; insert an explicit delay node" }, std::nullopt };

    return { {}, std::move(compiled) };
}

const NodeDescriptor* AudioGraph::findNode(const NodeId nodeId) const noexcept
{
    const auto position = nodes_.find(nodeId);
    return position == nodes_.end() ? nullptr : &position->second;
}

const std::vector<Connection>& AudioGraph::connections() const noexcept
{
    return connections_;
}

std::size_t AudioGraph::nodeCount() const noexcept
{
    return nodes_.size();
}

std::uint64_t AudioGraph::generation() const noexcept
{
    return generation_;
}

} // namespace cydaw::engine
