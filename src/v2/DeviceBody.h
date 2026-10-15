#pragma once

#include "DeviceHost.h"
#include "DeviceVisuals.h"
#include "PresetMenu.h"
#include "UiState.h"

#include <imgui.h>

// A device's large view, inside a window its caller opened (a device's
// window in the studio, a plug-in's window once exported): its presets,
// its displays (all of them at once, one large, or the interface drawn for
// it in the workshop) and its controls under them.

namespace cydaw::v2 {

class InterfaceEditor;

struct DeviceBody {
    std::vector<visual::Param> parameters;
    visual::Surface surface;
    // The interface drawn for it in the workshop, when it has one.
    app::ModuleInterfaceState interface;
    bool designed {};
    [[nodiscard]] bool empty() const noexcept { return surface.panes.empty() && !designed; }
};

// What a card shows large. `span`: where it runs in the engine (its sound
// tapped), none when it is not.
[[nodiscard]] DeviceBody deviceBodyOf(app::NodeGraphComponent& graph, const app::NodeGraphComponent::EditorNode& node,
                                      const app::NodeGraphComponent::EditorNativeSpan* span);

// The card's view in the space left in the current window. `pane`: the one
// shown (-2 the drawn interface, -1 all displays at once, 0… one display),
// kept by the caller. `detachId`: the window's id for the button that takes
// it out of the studio's window (none in a plug-in).
void drawDeviceBody(DeviceBody& body, int& pane, DeviceHost& host, UiState& state, PresetMenu& presets, InterfaceEditor& designed,
                    const TrackRef& track, app::NodeGraphComponent& graph, const app::NodeGraphComponent::EditorNode& node,
                    const visual::Live& live, const char* detachId);

} // namespace cydaw::v2
