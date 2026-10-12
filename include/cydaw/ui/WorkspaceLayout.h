#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace cydaw::ui {

using LayoutNodeId = std::uint64_t;
enum class PanelKind : std::uint8_t { arrangement, clipLauncher, mixer, browser, inspector, nodeGraph, sampleEditor, moduleEditor, interfaceDesigner, pluginEditor, analyzer };
enum class LayoutNodeKind : std::uint8_t { split, tabStack, panel };
enum class SplitAxis : std::uint8_t { horizontal, vertical };

struct PanelInstance {
    std::string instanceKey;
    std::string title;
    PanelKind kind { PanelKind::arrangement };
    bool detachable { true };
    bool closeable { true };
};

struct LayoutNode {
    LayoutNodeId id {};
    LayoutNodeKind kind { LayoutNodeKind::panel };
    std::vector<LayoutNodeId> children;
    std::optional<PanelInstance> panel;
    SplitAxis splitAxis { SplitAxis::horizontal };
    float splitRatio { 0.5f };
    std::size_t activeTab {};
};

struct FloatingWindowLayout {
    std::string windowKey;
    LayoutNodeId rootNodeId {};
    int x {};
    int y {};
    int width { 900 };
    int height { 600 };
    bool alwaysOnTop {};
};

enum class LayoutError : std::uint8_t { none, missingRoot, missingNode, duplicatePanelInstance, invalidSplit, invalidTabStack, invalidPanel, sharedChild, cycleDetected, orphanNode };
struct LayoutResult {
    LayoutError error { LayoutError::none };
    std::string message;
    [[nodiscard]] explicit operator bool() const noexcept { return error == LayoutError::none; }
};

class WorkspaceLayout final {
public:
    void setMainRoot(LayoutNodeId id) noexcept;
    void addNode(LayoutNode node);
    void addFloatingWindow(FloatingWindowLayout window);
    [[nodiscard]] LayoutResult validate() const;
    [[nodiscard]] LayoutNodeId mainRoot() const noexcept;
    [[nodiscard]] const std::unordered_map<LayoutNodeId, LayoutNode>& nodes() const noexcept;
    [[nodiscard]] const std::vector<FloatingWindowLayout>& floatingWindows() const noexcept;

private:
    LayoutNodeId mainRoot_ {};
    std::unordered_map<LayoutNodeId, LayoutNode> nodes_;
    std::vector<FloatingWindowLayout> floatingWindows_;
};

} // namespace cydaw::ui

