#include "GraphLayout.h"
#include <cyui/modules/Nodes.h>

#include <algorithm>
#include <map>
#include <set>

namespace cydaw::v2::graph_layout {

using EditorNode = app::NodeGraphComponent::EditorNode;

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
    std::vector<cyui::Node> cards;
    std::vector<cyui::NodeLink> links;
    // CyDAW ports are local to each node; CyUI pins have global identities.
    std::map<std::pair<std::uint64_t, std::uint64_t>, cyui::ItemId> pins;
    cyui::ItemId next = 1;
    float maxWidth = 0, maxHeight = 0;
    for (const auto& node : nodes) {
        cyui::Node card;
        card.id = node.id;
        card.position = {node.position.x * worldX, node.position.y * worldY};
        for (const auto& port : node.ports) {
            const auto pin = next++;
            pins[{node.id, port.id}] = pin;
            (port.input ? card.inputs : card.outputs).push_back({pin, {}, port.kind});
        }
        maxWidth = std::max(maxWidth, width(node));
        maxHeight = std::max(maxHeight, height(node));
        cards.push_back(std::move(card));
    }
    for (const auto& connection : connections) {
        const auto from = pins.find({connection.source, connection.sourcePort});
        const auto to = pins.find({connection.destination, connection.destinationPort});
        if (from != pins.end() && to != pins.end()) links.push_back({next++, from->second, to->second});
    }
    std::vector<std::pair<std::uint64_t, juce::Point<float>>> positions;
    for (const auto& move : cyui::arrangeNodes(cards, links, {maxWidth + 60., maxHeight + 30.}))
        positions.push_back({move.id, {float((move.position.x + 40.) / worldX), float((move.position.y + 40.) / worldY)}});
    // One committed operation, through the application's existing Undo path.
    if (!positions.empty()) graph.editorMoveNodes(positions, true);
}

} // namespace cydaw::v2::graph_layout
