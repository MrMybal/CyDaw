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

    Studio& studio_;
    UiState& state_;
    // How far the scenes are scrolled down (the names and the strips stay).
    float sceneScroll_ {};
};

} // namespace cydaw::v2
