#include "GraphLayout.h"

#include <algorithm>
#include <map>
#include <set>

namespace cydaw::v2::graph_layout {

using EditorNode = app::NodeGraphComponent::EditorNode;

namespace {

bool isInputBoundary(const EditorNode& node)
{
    return node.boundary && (node.typeKey == "midi-input" || node.typeKey == "audio-clips" || node.typeKey.endsWith("-input")
                             || node.typeKey.contains("-input-"));
}

bool isOutputBoundary(const EditorNode& node)
{
    return node.boundary && !isInputBoundary(node);
}

} // namespace

CardContent content(const EditorNode& node)
{
    CardContent result;
    result.parameters.reserve(node.parameters.size());
    for (const auto& parameter : node.parameters)
        result.parameters.push_back({ parameter.binding, parameter.label, parameter.unit, parameter.minimum, parameter.maximum,
                                      parameter.value, parameter.defaultValue });
    if (node.boundary || node.collapsed)
        return result;
    const auto surface = visual::inferSurface(result.parameters, false, false);
    std::vector<bool> shown(result.parameters.size(), false);
    if (!surface.panes.empty()) {
        result.pane = surface.panes.front();
        for (const auto index : result.pane->parameters)
            if (index >= 0)
                shown[static_cast<std::size_t>(index)] = true;
    }
    for (auto index = 0; index < static_cast<int>(result.parameters.size()); ++index)
        if (!shown[static_cast<std::size_t>(index)])
            result.knobs.push_back(index);
    if (static_cast<int>(result.knobs.size()) > maxShownParameters) {
        result.hiddenKnobs = static_cast<int>(result.knobs.size()) - maxShownParameters;
        result.knobs.resize(maxShownParameters);
    }
    result.columns = result.pane ? 5 : 4;
    return result;
}

float width(const EditorNode& node)
{
    if (node.boundary)
        return boundaryWidth;
    return !node.collapsed && content(node).pane ? wideCardWidth : cardWidth;
}

int defaultSocketRows(const EditorNode& node)
{
    auto inputs = 0;
    auto outputs = 0;
    for (const auto& port : node.ports) {
        if (port.exposure==0 || (port.exposure<0 && port.input && port.kind==2))
            continue;
        (port.input ? inputs : outputs)++;
    }
    return std::max(inputs, outputs);
}

float height(const EditorNode& node)
{
    return height(node, defaultSocketRows(node));
}

float height(const EditorNode& node, const int rows)
{
    if (node.collapsed)
        return header + 4.0f;
    const auto card = content(node);
    const auto knobRows = (static_cast<int>(card.knobs.size()) + card.columns - 1) / card.columns;
    return header + (rows > 0 ? rows * portRow + 8.0f : 0.0f) + (card.pane ? paneHeight + 8.0f : 0.0f)
           + knobRows * knobCellH + 8.0f;
}

void arrange(app::NodeGraphComponent& graph)
{
    const auto nodes = graph.editorNodes();
    const auto connections = graph.editorConnections();
    if (nodes.empty())
        return;
    std::map<std::uint64_t, const EditorNode*> byId;
    for (const auto& node : nodes)
        byId[node.id] = &node;
    std::map<std::uint64_t, std::vector<std::uint64_t>> incoming;
    std::map<std::uint64_t, std::vector<std::uint64_t>> outgoing;
    std::map<std::uint64_t, bool> feedsControlOnly;
    for (const auto& connection : connections) {
        if (!byId.contains(connection.source) || !byId.contains(connection.destination))
            continue;
        incoming[connection.destination].push_back(connection.source);
        outgoing[connection.source].push_back(connection.destination);
    }

    // Longest path from the sources, bounded so a cycle cannot loop forever.
    std::map<std::uint64_t, int> depth;
    for (const auto& node : nodes)
        depth[node.id] = 0;
    for (std::size_t pass = 0; pass < nodes.size() + 1; ++pass) {
        auto changed = false;
        for (const auto& connection : connections) {
            if (!depth.contains(connection.source) || !depth.contains(connection.destination))
                continue;
            const auto candidate = depth[connection.source] + 1;
            if (candidate > depth[connection.destination] && candidate < static_cast<int>(nodes.size()) + 1) {
                depth[connection.destination] = candidate;
                changed = true;
            }
        }
        if (!changed)
            break;
    }
    // Modulators and other sources that are not inputs: just before the
    // first card they feed.
    for (const auto& node : nodes) {
        if (isInputBoundary(node) || !incoming[node.id].empty() || outgoing[node.id].empty())
            continue;
        auto target = std::numeric_limits<int>::max();
        for (const auto destination : outgoing[node.id])
            target = std::min(target, depth[destination]);
        depth[node.id] = std::max(0, target - 1);
    }
    auto maximum = 0;
    for (const auto& node : nodes)
        if (!isOutputBoundary(node))
            maximum = std::max(maximum, depth[node.id]);
    for (const auto& node : nodes) {
        if (isInputBoundary(node))
            depth[node.id] = 0;
        else if (isOutputBoundary(node))
            depth[node.id] = maximum + 1;
        else
            depth[node.id] = std::max(depth[node.id], 1);
    }

    // Columns, each stacked around the same centre line; within a column the
    // signal cards first, then the rest, in their current vertical order.
    std::map<int, std::vector<const EditorNode*>> columns;
    for (const auto& node : nodes)
        columns[depth[node.id]].push_back(&node);
    const auto gapX = 60.0f;
    const auto gapY = 30.0f;
    const auto centreY = 320.0f;
    std::vector<std::pair<std::uint64_t, juce::Point<float>>> positions;
    auto x = 40.0f;
    for (auto& [column, members] : columns) {
        std::stable_sort(members.begin(), members.end(), [&](const EditorNode* a, const EditorNode* b) {
            const auto controlA = !incoming[a->id].empty() || outgoing[a->id].empty() ? 0 : 1;
            const auto controlB = !incoming[b->id].empty() || outgoing[b->id].empty() ? 0 : 1;
            if (controlA != controlB)
                return controlA < controlB;
            return a->position.y < b->position.y;
        });
        auto total = 0.0f;
        auto columnWidth = 0.0f;
        for (const auto* node : members) {
            total += height(*node) + gapY;
            columnWidth = std::max(columnWidth, width(*node));
        }
        auto y = centreY - (total - gapY) * 0.5f;
        for (const auto* node : members) {
            const auto nodeX = x + (columnWidth - width(*node)) * 0.5f;
            positions.push_back({ node->id, { nodeX / worldX, y / worldY } });
            y += height(*node) + gapY;
        }
        x += columnWidth + gapX;
    }
    graph.editorMoveNodes(positions, true);
}

} // namespace cydaw::v2::graph_layout
