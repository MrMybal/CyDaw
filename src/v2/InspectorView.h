#pragma once

#include "Studio.h"
#include "UiState.h"

namespace cydaw::v2 {

class ImGuiHost;

// Properties of the selected track or clip.
class InspectorView final {
public:
    InspectorView(Studio& studio, UiState& state);
    void draw();

private:
    void drawModulation(const TrackRef& track);
    void drawBus(const TrackRef& busRef);

    Studio& studio_;
    UiState& state_;
};

} // namespace cydaw::v2
