#pragma once

#include "Studio.h"
#include "UiState.h"

#include <imgui.h>

#include <map>
#include <optional>
#include <set>
#include <vector>

// The node graph of the selected track, edited directly.
//
// Cards show their sockets and their parameters as knobs; cables are curves
// in the colour of what they carry. A cable dropped on empty canvas opens the
// node search with only the nodes that accept it, and the chosen node is
// connected on arrival. Tab (or a double click) searches nodes at the
// pointer; the wheel zooms around it, the middle button pans, F frames, A
// arranges by signal flow.

namespace cydaw::v2 {

class ImGuiHost;
class DeviceSignals;

class GraphEditor final {
public:
    // track: the selected track's graph. workshop: the device the workshop
    // edits, the devices inside it opened in place.
    enum class Mode { track, workshop };
    GraphEditor(Studio& studio, UiState& state, ImGuiHost& host, DeviceSignals& signals, Mode mode = Mode::track);
    void draw();
    // A card of that type: in the middle of the view (the palette's click),
    // a little aside from the last one so they do not pile up.
    void addNode(const juce::String& typeKey);
    // What a palette drags onto the canvas: the node type's key.
    static constexpr const char* nodeTypePayload = "CYDAW_NODE_TYPE";

private:
    using EditorNode = app::NodeGraphComponent::EditorNode;
    using EditorPort = app::NodeGraphComponent::EditorPort;
    using EditorConnection = app::NodeGraphComponent::EditorConnection;
    using EditorNodeType = app::NodeGraphComponent::EditorNodeType;

    struct View {
        ImVec2 scroll {};      // world position at the canvas origin
        float zoom { 1.0f };
        bool framed {};
        // Until the person zooms or pans, the view follows the panel's size.
        bool touched {};
        ImVec2 framedSize {};
        // A layout from CyDAW 1 too loose to read is arranged once.
        bool tidied {};
    };
    struct NodeLayout {
        const EditorNode* node {};
        ImVec2 min;            // screen
        ImVec2 max;
        float headerHeight {};
        std::vector<std::pair<const EditorPort*, ImVec2>> inputs;   // socket centres, screen
        std::vector<std::pair<const EditorPort*, ImVec2>> outputs;
        float bodyTop {};      // under the sockets, screen
    };
    struct PendingWire {
        std::uint64_t node {};
        std::uint64_t port {};
        bool fromOutput { true };
        int kind {};
        ImVec2 anchor;
    };

    [[nodiscard]] bool inWorkshop() const noexcept { return mode_ == Mode::workshop; }
    // The selected track's graph shown: its sound, or its modulation (the
    // value graph that moves its parameters; audio tracks).
    [[nodiscard]] GraphSlot* trackSlot(const TrackRef& track);
    bool showingModulation_ {};
    // The workshop's device as the simulator plays it: its cards' displays
    // and knobs come alive (not the devices opened inside it).
    [[nodiscard]] bool simulatedHere(const GraphSlot& slot);
    bool simulated_ {};
    [[nodiscard]] Panel panel() const noexcept { return inWorkshop() ? Panel::workshop : Panel::graph; }
    void drawToolbar(GraphSlot& slot, const std::optional<TrackRef>& track);
    void drawCanvas(GraphSlot& slot, const std::optional<TrackRef>& track);
    void layoutNodes(const std::vector<EditorNode>& nodes, const std::vector<EditorConnection>& connections,
                     std::vector<NodeLayout>& out) const;
    void drawNode(app::NodeGraphComponent& graph, const NodeLayout& layout, ImDrawList* list);
    // The devices menu: the cards picked made one device, My devices, the
    // workshop.
    void newDeviceMenu(GraphSlot& slot, const std::optional<TrackRef>& track);
    // The device's inputs and outputs: audio (mono, stereo, up to 8),
    // MIDI, control values. Each is a card at the edge of the graph and a
    // socket on the device's card outside.
    void ioMenu(GraphSlot& slot);
    void addBoundary(GraphSlot& slot, const juce::String& typeKey, bool input);
    // A cable's menu (right click on it): a control cable onto a knob sets
    // it or modulates it around its value, by a depth; any cable goes.
    void cableMenu(GraphSlot& slot);
    [[nodiscard]] std::optional<EditorConnection> wireAt(const std::vector<EditorConnection>& connections,
                                                         const std::vector<NodeLayout>& layouts, ImVec2 mouse) const;
    void groupSelection(GraphSlot& slot);
    void drawWires(ImDrawList* list, const std::vector<EditorConnection>& connections,
                   const std::vector<NodeLayout>& layouts);
    void drawComments(app::NodeGraphComponent& graph, ImDrawList* list);
    void drawMinimap(ImDrawList* list, const std::vector<NodeLayout>& layouts);
    void searchPopup(GraphSlot& slot, const std::optional<TrackRef>& track);
    void nodeContextMenu(GraphSlot& slot);
    void handleKeys(GraphSlot& slot);
    // Returns the zoom that fits.
    float frameAll(const std::vector<EditorNode>& nodes, bool selectionOnly);
    void openSearch(ImVec2 screenPosition, std::optional<PendingWire> wire);

    [[nodiscard]] ImVec2 worldToScreen(ImVec2 world) const;
    [[nodiscard]] ImVec2 screenToWorld(ImVec2 screen) const;
    [[nodiscard]] float scale() const;
    [[nodiscard]] static ImVec2 fractionToWorld(juce::Point<float> fraction);
    [[nodiscard]] static juce::Point<float> worldToFraction(ImVec2 world);
    [[nodiscard]] static ImU32 kindColour(int kind);

    Studio& studio_;
    UiState& state_;
    ImGuiHost& host_;
    DeviceSignals& signals_;

    std::map<const void*, View> views_;
    View* view_ {};
    ImVec2 canvasMin_ {};
    ImVec2 canvasMax_ {};
    const void* currentGraph_ {};

    std::set<std::uint64_t> selection_;
    std::optional<EditorConnection> selectedWire_;
    std::optional<PendingWire> wire_;
    bool movingNodes_ {};
    std::map<std::uint64_t, ImVec2> moveOrigins_;     // world
    ImVec2 moveStartMouse_ {};
    bool boxSelecting_ {};
    // The view held by the hand: a drag begun on the empty canvas.
    bool grabbing_ {};
    ImVec2 boxStart_ {};
    bool panning_ {};

    // Node search.
    bool openSearch_ {};
    ImVec2 searchScreen_ {};
    ImVec2 searchWorld_ {};
    std::optional<PendingWire> searchWire_;
    char searchText_[96] {};
    int searchHighlight_ {};
    bool focusSearch_ {};

    // Renaming a card.
    std::optional<std::uint64_t> renaming_;
    char renameBuffer_[96] {};
    bool focusRename_ {};

    std::optional<std::uint64_t> contextNode_;
    ImGuiID nodeMenuId_ {};
    ImGuiID searchMenuId_ {};
    bool showMinimap_ { true };
    // A card just let go of, to be spliced into the cable under it.
    std::optional<std::uint64_t> droppedNode_;
    // The cable a card being dragged would be spliced into.
    std::optional<EditorConnection> spliceTarget_;
    void findSpliceTarget(const std::vector<EditorConnection>& connections, const std::vector<NodeLayout>& layouts);
    void splice(app::NodeGraphComponent& graph, const std::vector<NodeLayout>& layouts);
    bool knobHovered_ {};
    juce::String lastError_;
    double lastErrorTime_ {};
    Mode mode_;
    int addedInPlace_ {};
    // The cards as last drawn (screen), for a new card to find room.
    std::vector<std::pair<ImVec2, ImVec2>> cardRects_;
    // A device made of a graph to open, once this frame is drawn.
    std::optional<std::uint64_t> pendingEnter_;
    ImGuiID deviceMenuId_ {};
    ImGuiID ioMenuId_ {};
    ImGuiID cableMenuId_ {};
    // A menu asked for from inside the canvas, opened from the panel itself
    // once the canvas is drawn: opened from the canvas's own window it was
    // closed straight away.
    ImGuiID pendingPopup_ {};
    std::optional<EditorConnection> cableMenuWire_;
    float cableAmount_ { 0.5f };
};

} // namespace cydaw::v2
