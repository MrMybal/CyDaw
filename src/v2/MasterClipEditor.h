#pragma once

#include "CurveEditor.h"
#include "Studio.h"
#include "UiState.h"

#include <imgui.h>

#include <cstdint>
#include <optional>
#include <vector>

// A launcher clip of the master (one a scene) in the editor: its name, its
// length and loop, and one lane at a time — the master's level or one of its
// devices' knobs — in the curve editor every lane uses (CurveEditor.h), over
// its beats, the bar of its picked points under the toolbar.

namespace cydaw::v2 {

class MasterClipEditor final {
public:
    MasterClipEditor(Studio& studio, UiState& state);
    void draw(int scene);
    // Ctrl+Z on the clip shown: whether there was something to take back.
    bool undo(bool redo);

private:
    Studio& studio_;
    UiState& state_;
    // The lane shown (a binding, or Studio::masterGainLane), for the clip
    // of `shownScene_`.
    juce::String lane_;
    int shownScene_ { -1 };
    double gridBeats_ { 1.0 };
    // The lane's curve editor (its picked points).
    curves::Editor curve_;
    // The lanes' list narrowed to what is typed (a big master has many knobs).
    char filter_[64] {};
    // Its own history: the clip as it was before each gesture (a drag is
    // one step, kept once the mouse is let go), by scene.
    struct Step {
        int scene {};
        MasterClip clip;
    };
    std::vector<Step> undo_;
    std::vector<Step> redo_;
    std::optional<Step> gesture_;
    std::uint64_t refsSerial_ {};
};

} // namespace cydaw::v2
