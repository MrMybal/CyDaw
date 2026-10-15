#pragma once

#include "PresetMenu.h"
#include "Studio.h"
#include "UiState.h"

#include <memory>
#include <string>

// The windows of the devices made of a graph, as a plugin's: the interface
// drawn in the workshop (Interface), played, in a window of its own beside
// the studio, which can go onto another screen, its presets above it.
// Opened from a device's card (or a MIDI track's instrument), any number at
// once.

namespace cydaw::v2 {

class InterfaceEditor;

class PluginWindows final {
public:
    PluginWindows(Studio& studio, UiState& state);
    ~PluginWindows();
    void draw();
    // A window's ImGui id, the same from frame to frame ("###plugina0-3").
    [[nodiscard]] static std::string windowId(const PluginWindow& window);

private:
    Studio& studio_;
    UiState& state_;
    std::unique_ptr<InterfaceEditor> renderer_;
    PresetMenu presets_;
};

} // namespace cydaw::v2
