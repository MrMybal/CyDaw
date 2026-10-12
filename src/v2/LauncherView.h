#pragma once

#include "UiState.h"

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

    Studio& studio_;
    UiState& state_;
    // How far the scenes are scrolled down (the names and the strips stay).
    float sceneScroll_ {};
};

} // namespace cydaw::v2
