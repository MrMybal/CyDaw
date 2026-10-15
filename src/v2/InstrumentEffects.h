#pragma once

#include "DeviceHost.h"
#include "UiState.h"

#include <imgui.h>

#include <span>

// An instrument window's EFFETS tab (Prisme, Kick): the track's effects after
// the instrument, as CyDAW's devices. A row of buttons adds one after the
// others; each one there shows its knobs, and a cross takes it out. Devices
// shows the same chain.

namespace cydaw::v2::instrument_effects {

struct Choice {
    const char* key; // the device catalogue's
    const char* french;
    const char* english;
};

// `ownType`: the instrument card's type, never listed as an effect.
void draw(DeviceHost& studio, UiState& state, const TrackRef& track, const juce::String& ownType,
          const juce::String& instrumentName, std::span<const Choice> choices, ImU32 colour);

} // namespace cydaw::v2::instrument_effects
