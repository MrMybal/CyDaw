#pragma once

#include "Studio.h"
#include "UiState.h"

#include <imgui.h>

// The "+" that adds a track, wherever tracks are listed (the launcher after
// its last column, the mixer after its last strip, the arrangement's
// toolbar): a menu of what the track can be, an instrument track with
// Opale first, the instruments one by one after.

namespace cydaw::v2::add_track {

// The menu's items, inside a popup already open; the track made is chosen.
void items(Studio& studio, UiState& state);

// A "+" button of that size opening the menu. `tooltip` says where it adds.
void button(Studio& studio, UiState& state, const char* id, ImVec2 size);

} // namespace cydaw::v2::add_track
