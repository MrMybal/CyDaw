#pragma once

#include "Studio.h"
#include "UiState.h"

#include <imgui.h>

// An SFZ instrument's controls on a sampler or Batterie card: its
// articulations (keyswitches) to choose, its controllers as knobs (a
// microphone's level, a blend, the hi-hat's pedal), each one automated from
// its menu. A track's MIDI moves them too while it plays; the knobs show
// the card's.

namespace cydaw::v2::sampler_controls {

struct Count {
    int controls {};
    int articulations {};
};
// What a card's map has of them.
[[nodiscard]] Count count(app::NodeGraphComponent& graph, std::uint64_t nodeId);
// Draws them in the space left (rows of knobs that wrap); `colour` the card's.
void draw(Studio& studio, UiState& state, const TrackRef& track, app::NodeGraphComponent& graph, std::uint64_t nodeId,
          ImU32 colour);

} // namespace cydaw::v2::sampler_controls
