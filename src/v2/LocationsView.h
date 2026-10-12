#pragma once

#include "UiState.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

// Options › Emplacements: where CyDAW keeps the sound banks, the files being
// downloaded, its cache, the Python companion, the sound models, My devices
// and the plug-ins' projects. Each folder can be changed (what CyDAW put in
// the old one taken along, on a thread, with its progress), opened, or set
// back to its default.

namespace cydaw::v2 {

class LocationsView final {
public:
    LocationsView(Studio& studio, UiState& state);
    // The window, a modal, opened when the state asks.
    void draw();

private:
    void choose(Location location);
    void apply(Location location, const juce::File& folder);
    void report(const juce::String& text, bool error);

    Studio& studio_;
    UiState& state_;
    bool takeContent_ { true };
    std::unique_ptr<juce::FileChooser> chooser_;
};

} // namespace cydaw::v2
