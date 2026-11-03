#pragma once
#include <cyui/modules/Inspector.h>

#include "Studio.h"
#include "UiState.h"

namespace cydaw::v2 {

class ImGuiHost;

// Properties of the selected track or clip.
class InspectorView final {
public:
    InspectorView(Studio& studio, UiState& state);
    void draw();
    void finishFrame();

private:
    void drawModulation(const TrackRef& track);
    void drawBus(const TrackRef& busRef);

    cyui::InspectorState masterInspector_;
    std::optional<TrackRef> masterOwner_;
    std::pair<float, bool> masterObservedValues_ {};
    bool masterObserved_ {};
    std::uint64_t masterSerial_ {}, masterRevision_ {};
    int masterFrame_ {-1};
    Studio& studio_;
    UiState& state_;
};

} // namespace cydaw::v2
