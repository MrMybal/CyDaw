#pragma once

#include "Studio.h"
#include "StudioEffectsView.h"
#include "UiState.h"

// Around a card's panes: the file the card holds (a "Baffle et micro" card's
// impulse response), what loading or dropping one does, for the panes that
// show it (StudioEffectsView.h: CardFile). Other cards hold none.

namespace cydaw::v2 {

class CardFileScope final {
public:
    CardFileScope(Studio& studio, UiState& state, const TrackRef& track, app::NodeGraphComponent& graph, std::uint64_t nodeId,
                  const juce::String& typeKey);
    ~CardFileScope();
    CardFileScope(const CardFileScope&) = delete;
    CardFileScope& operator=(const CardFileScope&) = delete;

private:
    visual::CardFile file_;
    bool shown_ {};
};

} // namespace cydaw::v2
