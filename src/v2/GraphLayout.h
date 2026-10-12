#pragma once

#include "DeviceVisuals.h"
#include "NodeGraphComponent.h"

#include <optional>
#include <vector>

// The geometry of a card in the v2 graph editor, in world units, and the
// arrangement by signal flow both the editor and the session use.

namespace cydaw::v2::graph_layout {

// A stored position is a fraction of the v1 canvas; these are its world size.
inline constexpr float worldX = 1'080.0f;
inline constexpr float worldY = 620.0f;
inline constexpr float cardWidth = 212.0f;
// A card with a display (a filter's curve, an envelope) is wider.
inline constexpr float wideCardWidth = 262.0f;
inline constexpr float boundaryWidth = 132.0f;
inline constexpr float header = 30.0f;
inline constexpr float portRow = 18.0f;
inline constexpr float paneHeight = 96.0f;
inline constexpr float knobCellW = 50.0f;
inline constexpr float knobCellH = 56.0f;
inline constexpr int maxShownParameters = 12;

// What a card shows under its sockets: one display, when its parameters
// make one (the device panel's library decides), then the rest as knobs.
struct CardContent {
    std::vector<visual::Param> parameters;
    std::optional<visual::Pane> pane;
    std::vector<int> knobs;
    int hiddenKnobs {};
    int columns { 4 };
};
[[nodiscard]] CardContent content(const app::NodeGraphComponent::EditorNode& node);

[[nodiscard]] float width(const app::NodeGraphComponent::EditorNode& node);
// With every socket but the unconnected value inputs, which show only when
// wanted (a selected card, a control cable being drawn).
[[nodiscard]] float height(const app::NodeGraphComponent::EditorNode& node);
// With this many rows of sockets.
[[nodiscard]] float height(const app::NodeGraphComponent::EditorNode& node, int socketRows);
// The rows of sockets a card shows by default.
[[nodiscard]] int defaultSocketRows(const app::NodeGraphComponent::EditorNode& node);

// Columns by signal flow: inputs on the left, outputs on the right, each
// card one column after what feeds it; a modulator sits just before what it
// modulates. Records one undo step.
void arrange(app::NodeGraphComponent& graph);

} // namespace cydaw::v2::graph_layout
