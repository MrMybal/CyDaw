#pragma once

#include "Studio.h"
#include "UiState.h"

#include <imgui.h>

#include <cstdint>

// The card of a rack of pads ("drum-rack"): its sixteen keys as pads, four
// by four from the lowest at the bottom left; each pad its instrument and
// effects of their own (opened in the device panel), its level, pan, mute,
// solo and choke group. A pad is made by choosing its instrument, or by
// dropping a sound onto it.

namespace cydaw::v2 {

class RackView final {
public:
    RackView(Studio& studio, UiState& state) : studio_(studio), state_(state) {}

    [[nodiscard]] static bool handles(const juce::String& typeKey) { return typeKey == "drum-rack"; }
    [[nodiscard]] static float paneWidth();
    void drawPane(const TrackRef& track, ImVec2 at, ImVec2 size, ImU32 colour);

    // The instruments a pad can play, as a menu; true when one was chosen.
    static bool instrumentMenu(Studio& studio, const TrackRef& track, int note, std::optional<std::size_t> pad, juce::String& error);

private:
    void drawPadControls(const TrackRef& track, std::size_t pad, ImVec2 at, float width, ImU32 colour);
    void chooseSample(const TrackRef& track, int note);

    Studio& studio_;
    UiState& state_;
};

} // namespace cydaw::v2
