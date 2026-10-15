#pragma once

#include "EditorWindows.h"
#include "Studio.h"
#include "UiState.h"

#include <imgui.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <map>
#include <memory>
#include <vector>

// The sampler in the interface. On its card in the device panel: its zones
// over the keyboard and the velocities, what it holds, and files dropped or
// dragged onto it added to it. Its editor window: every zone in a table and
// on the map, the selected one's keys, velocities, tuning, level and pan,
// and its waveform with the start, end and loop to drag; files added, an SFZ
// read or written, the keys spread again around the roots.

namespace cydaw::v2 {

class SamplerView final {
public:
    SamplerView(Studio& studio, UiState& state);
    ~SamplerView();

    // The pane on a sampler card (the device panel draws it).
    void drawPane(const TrackRef& track, app::NodeGraphComponent& graph, std::uint64_t nodeId, ImVec2 at, ImVec2 size,
                  ImU32 colour);
    // The editor window, while one is open (every frame).
    void drawEditor();
    // `detached`: in a window of its own out of the studio's, as a plug-in's;
    // `controls`: on its controls' tab (an SFZ's articulations and knobs);
    // `here`: in the studio's window, wherever it was left.
    void openEditor(const TrackRef& track, std::uint64_t nodeId, bool detached = false, bool controls = false,
                    bool here = false);
    [[nodiscard]] bool showsEditor(const TrackRef& track, std::uint64_t nodeId) const { return windows_.shows(track, nodeId); }
    // That instrument's window's id (each has its own window).
    [[nodiscard]] std::string editorWindowId(const TrackRef& track, std::uint64_t nodeId) const
    {
        return windows_.idOf(track, nodeId);
    }
    // Files onto a card: added to its zones (mapped by their names, their
    // pitch); an SFZ or a .cysampler replaces them.
    void addFiles(const TrackRef& track, std::uint64_t nodeId, const juce::StringArray& paths);

private:
    struct Target {
        TrackRef track;
        std::uint64_t nodeId {};
    };
    [[nodiscard]] juce::var currentMap(const Target& target) const;
    void commit(const Target& target, const juce::var& json, bool applySettings = false);
    void report(const juce::String& message, bool error = false);
    void chooseFiles(const Target& target);
    void chooseSfz(const Target& target);
    void exportSfz(const Target& target);
    // The zones over the keys (across) and the velocities (up); returns the
    // zone under a click, or -1.
    int drawMap(const juce::var& json, ImVec2 at, ImVec2 size, int selected, ImU32 colour, bool interactive);
    void drawZoneFields(const Target& target, juce::var& json, int zone);
    // The waveform's peaks for the zone shown, read once.
    struct Peaks {
        const void* sample {};
        int columns {};
        std::vector<float> low;
        std::vector<float> high;
    };
    // A zone's waveform with its markers to drag (start, end, loop); the
    // edit in progress kept in `working` until let go. `compact`: on a card.
    void drawWaveform(const Target& target, juce::var& json, int zone, ImVec2 size, Peaks& peaks, juce::var& working,
                      bool& editing, bool compact);
    // The simple sampler's card: its one sample's waveform and root.
    void drawSimplePane(const TrackRef& track, app::NodeGraphComponent& graph, std::uint64_t nodeId, ImVec2 at, ImVec2 size);

    Studio& studio_;
    UiState& state_;
    // Its windows (one for each instrument opened), and the one being drawn:
    // its instrument, its id.
    EditorWindows windows_ { "###samplerEditor" };
    std::optional<Target> editor_;
    std::string windowId_;
    // One window; false once it is closed.
    bool drawEditorWindow(bool focus);
    // What each window keeps of its own, by its id (moved into the members
    // above while it is drawn).
    struct WindowState {
        int selected {};
        bool editing {};
        juce::var working;
        Peaks peaks;
        bool showControls {};
    };
    std::map<std::string, WindowState> windowStates_;
    int selected_ { -1 };
    // The edit in progress (a field being dragged): committed when let go.
    juce::var working_;
    bool editing_ {};
    Peaks peaks_;
    // The simple card's own edit (its markers, its root), apart from the
    // editor window's.
    struct PaneEdit {
        std::optional<Target> target;
        juce::var working;
        bool editing {};
        Peaks peaks;
    } pane_;
    int draggedMarker_ { -1 };
    // The controls' tab to show on the next frame; the window to bring forward.
    bool showControls_ {};
    // The window to put back in the studio's window on its next frame.
    std::string placeHere_;
    std::unique_ptr<juce::FileChooser> chooser_;
};

} // namespace cydaw::v2
