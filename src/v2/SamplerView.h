#pragma once

#include "UiState.h"

#include <imgui.h>
#include <juce_gui_basics/juce_gui_basics.h>

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
    [[nodiscard]] bool showsEditor(const TrackRef& track, std::uint64_t nodeId) const
    {
        return editor_ && editor_->track == track && editor_->nodeId == nodeId;
    }
    static constexpr const char* editorWindowId = "###samplerEditor";
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
    void drawWaveform(const Target& target, juce::var& json, int zone, ImVec2 size);

    Studio& studio_;
    UiState& state_;
    std::optional<Target> editor_;
    int selected_ { -1 };
    // The edit in progress (a field being dragged): committed when let go.
    juce::var working_;
    bool editing_ {};
    // The waveform's peaks for the zone shown, read once.
    struct Peaks {
        const void* sample {};
        int columns {};
        std::vector<float> low;
        std::vector<float> high;
    } peaks_;
    int draggedMarker_ { -1 };
    // The controls' tab to show on the next frame; the window to bring forward.
    bool showControls_ {};
    bool focusEditor_ {};
    bool placeHere_ {};
    std::unique_ptr<juce::FileChooser> chooser_;
};

} // namespace cydaw::v2
