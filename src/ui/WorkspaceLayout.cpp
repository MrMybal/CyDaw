#include <cydaw/ui/WorkspaceLayout.h>

#include <functional>
#include <unordered_set>

namespace cydaw::ui {
namespace {
LayoutResult failure(const LayoutError error, std::string message) { return { error, std::move(message) }; }
}

void WorkspaceLayout::setMainRoot(const LayoutNodeId id) noexcept { mainRoot_ = id; }
void WorkspaceLayout::addNode(LayoutNode node) { nodes_.insert_or_assign(node.id, std::move(node)); }
void WorkspaceLayout::addFloatingWindow(FloatingWindowLayout window) { floatingWindows_.push_back(std::move(window)); }

LayoutResult WorkspaceLayout::validate() const
{
    if (mainRoot_ == 0 || !nodes_.contains(mainRoot_))
        return failure(LayoutError::missingRoot, "The main layout root does not exist");

    std::unordered_map<LayoutNodeId, std::size_t> parents;
    std::unordered_set<std::string> panelKeys;
    for (const auto& [id, node] : nodes_) {
        if (id == 0)
            return failure(LayoutError::missingNode, "Layout node id 0 is reserved");
        if (node.kind == LayoutNodeKind::split
            && (node.children.size() != 2 || node.splitRatio <= 0.05f || node.splitRatio >= 0.95f))
            return failure(LayoutError::invalidSplit, "A split needs two children and a usable ratio");
        if (node.kind == LayoutNodeKind::tabStack
            && (node.children.empty() || node.activeTab >= node.children.size()))
            return failure(LayoutError::invalidTabStack, "A tab stack needs children and a valid active tab");
        if (node.kind == LayoutNodeKind::panel && (!node.panel || !node.children.empty()))
            return failure(LayoutError::invalidPanel, "A panel must contain panel data and no child");
        if (node.kind != LayoutNodeKind::panel && node.panel)
            return failure(LayoutError::invalidPanel, "Only panel nodes may contain a panel instance");
        if (node.panel && !panelKeys.insert(node.panel->instanceKey).second)
            return failure(LayoutError::duplicatePanelInstance, "A panel instance appears more than once");

        for (const auto child : node.children) {
            if (!nodes_.contains(child))
                return failure(LayoutError::missingNode, "A layout child does not exist");
            if (++parents[child] > 1)
                return failure(LayoutError::sharedChild, "A layout node cannot have multiple parents");
        }
    }

    std::unordered_set<LayoutNodeId> visiting;
    std::unordered_set<LayoutNodeId> visited;
    const std::function<bool(LayoutNodeId)> walk = [&](const LayoutNodeId id) {
        if (visiting.contains(id)) return false;
        if (visited.contains(id)) return true;
        visiting.insert(id);
        for (const auto child : nodes_.at(id).children)
            if (!walk(child)) return false;
        visiting.erase(id);
        visited.insert(id);
        return true;
    };

    if (!walk(mainRoot_))
        return failure(LayoutError::cycleDetected, "The main window layout contains a cycle");
    for (const auto& window : floatingWindows_) {
        if (!nodes_.contains(window.rootNodeId))
            return failure(LayoutError::missingNode, "A floating window root does not exist");
        if (!walk(window.rootNodeId))
            return failure(LayoutError::cycleDetected, "A floating window layout contains a cycle");
    }
    if (visited.size() != nodes_.size())
        return failure(LayoutError::orphanNode, "The layout contains a node not owned by any window");
    return {};
}

LayoutNodeId WorkspaceLayout::mainRoot() const noexcept { return mainRoot_; }
const std::unordered_map<LayoutNodeId, LayoutNode>& WorkspaceLayout::nodes() const noexcept { return nodes_; }
const std::vector<FloatingWindowLayout>& WorkspaceLayout::floatingWindows() const noexcept { return floatingWindows_; }

} // namespace cydaw::ui

