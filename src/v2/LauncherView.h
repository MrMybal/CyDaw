#pragma once

#include "Studio.h"
#include "UiState.h"

#include <imgui.h>

namespace cydaw::v2 {

class ImGuiHost;

// Scenes and clips launched live.
class LauncherView final {
public:
    LauncherView(Studio& studio, UiState& state);
    void draw();
    bool dropFiles(const juce::StringArray& files, ImVec2 screenPoint);
    // Ctrl+E, the slot menu: the selected empty slots' stop taken off (a
    // scene launched then leaves their track playing), or put back.
    void toggleSelectedStops();

private:
    void launchScene(int scene);
    void stopAll();
    // A slot of the master's column: its automation clip (launched by its
    // triangle, opened with a double click), or an empty slot where a double
    // click makes one.
    void masterSlot(int scene, ImVec2 min, ImVec2 max);
    // The sections over the strips, a track's cell each (the master's for
    // a null track where it has one).
    void devicesCell(const TrackRef& track, ImVec2 min, ImVec2 size);
    void sendsCell(const TrackRef& track, const std::vector<TrackRef>& buses, ImVec2 min, ImVec2 size);
    void ioCell(const TrackRef& track, ImVec2 min, ImVec2 size);
    // The column of toggles on the right: what the launcher shows.
    void optionsColumn(float height);

    Studio& studio_;
    UiState& state_;
    // How far the scenes are scrolled down (the names and the strips stay).
    float sceneScroll_ {};
    int drawnFrame_{-100};
    struct DropSlot { SlotRef slot; ImVec2 min, max; };
    std::vector<DropSlot> dropSlots_;
    ImVec2 dropMin_{}, dropMax_{};
};

} // namespace cydaw::v2
