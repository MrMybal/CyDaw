#pragma once

#include "DeviceSignals.h"
#include "PresetMenu.h"
#include "UiState.h"

#include <string>

// Devices' displays in windows of their own, as large as each window is
// made: the parametric equaliser with its band panel, the studio effects,
// and any other display with its sound beside it and its controls under it.
// Opened by the corner button of a display (in the device panel or the
// graph), several at once as plug-ins' windows; each docks or floats like
// the panels, or goes out of the studio's window altogether (its header's
// button), onto another screen.

namespace cydaw::v2 {

class DeviceEditorView final {
public:
    DeviceEditorView(Studio& studio, UiState& state, DeviceSignals& signals);
    void draw();

    // A device's window id, the same from frame to frame ("###device-a0-3").
    [[nodiscard]] static std::string windowId(const TrackRef& track, std::uint64_t nodeId);

private:
    // One window; false once it is closed or its device gone.
    bool drawWindow(ExpandedPane& expanded, int order);

    Studio& studio_;
    UiState& state_;
    DeviceSignals& signals_;
    PresetMenu presets_;
};

} // namespace cydaw::v2
