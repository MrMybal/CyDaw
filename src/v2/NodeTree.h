#pragma once

#include "Studio.h"

#include <functional>
#include <vector>

// The node types as a tree, several levels deep: Audio › Effets ›
// Modulation, MIDI, Contrôle › Maths… The catalogue's flat categories become
// branches, and the large ones are split further, so a long catalogue stays
// easy to walk. Shared by the workshop's palette and the graph's node search.

namespace cydaw::v2::node_tree {

using Type = app::NodeGraphComponent::EditorNodeType;

// Where a node type sits, outermost first, in the language shown.
[[nodiscard]] std::vector<juce::String> pathOf(const Type& type);
// The same, as one line: "Audio › Effets › Modulation".
[[nodiscard]] juce::String pathText(const Type& type);

struct Branch {
    juce::String name;
    std::vector<Branch> children;
    std::vector<const Type*> leaves;
    int count {}; // the leaves below, at every level
};

// The types given, as a tree: branches in the tree's own order, leaves by
// title.
[[nodiscard]] Branch build(const std::vector<const Type*>& types);

// Draws the tree's branches as tree nodes (the first level open, all of them
// when `openAll`); `leaf` draws one type's row, its ImGui id pushed.
void draw(const Branch& root, bool openAll, const std::function<void(const Type&)>& leaf);

} // namespace cydaw::v2::node_tree
