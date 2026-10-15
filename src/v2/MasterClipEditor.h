#pragma once

#include "Studio.h"
#include "UiState.h"

#include <imgui.h>

// A launcher clip of the master (one a scene) in the editor: its name, its
// length and loop, and one lane at a time — the master's level or one of its
// devices' knobs — drawn as points over its beats. A click adds a point (and
// drags it), a double click or a right click on one removes it; the grid
// holds them unless Alt is down.

namespace cydaw::v2 {

class MasterClipEditor final {
public:
    MasterClipEditor(Studio& studio, UiState& state);
    void draw(int scene);

private:
    Studio& studio_;
    UiState& state_;
    // The lane shown (a binding, or Studio::masterGainLane), for the clip
    // of `shownScene_`.
    juce::String lane_;
    int shownScene_ { -1 };
    double gridBeats_ { 1.0 };
    // The point being dragged (its index in the lane), -1 none.
    int dragged_ { -1 };
    // The lanes' list narrowed to what is typed (a big master has many knobs).
    char filter_[64] {};
};

} // namespace cydaw::v2
