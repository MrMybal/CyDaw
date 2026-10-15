#pragma once

#include "Studio.h"
#include "UiState.h"

// The mixer: one narrow strip per track — colour, name, the devices it
// carries, pan, fader with its meter, mute, solo, arm — and the master.

namespace cydaw::v2 {

// A strip, drawn at the cursor, of any width (its controls centred): the
// mixer's, and the launcher's under each track column.
namespace mixer_strip {
[[nodiscard]] float naturalWidth();
void track(Studio& studio, UiState& state, const TrackRef& track, int index, float width, float height);
// Clicking its name picks the master: its devices in the panels.
void master(Studio& studio, UiState& state, float width, float height);
} // namespace mixer_strip

class MixerView final {
public:
    MixerView(Studio& studio, UiState& state);
    void draw();

private:
    Studio& studio_;
    UiState& state_;
};

} // namespace cydaw::v2
