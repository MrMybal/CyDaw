#pragma once

#include "Studio.h"
#include "UiState.h"

#include <functional>
#include <optional>

// The right-click menus every view shares: a track's (arrangement header,
// launcher header, mixer strip) and a launcher slot's (its clip, or the empty
// slot). A view asks through UiState::contextRequest; the menu opens where
// the pointer is, drawn by the interface over every panel. Renaming opens a
// small dialog of its own.

namespace cydaw::v2 {

class ContextMenus final {
public:
    ContextMenus(Studio& studio, UiState& state);
    void draw();

private:
    void trackMenu(const TrackRef& track);
    void slotMenu(const TrackRef& track, int scene);
    void colourMenu(const std::function<void(juce::uint32)>& apply);
    void renameDialog();
    void askRename(const juce::String& current);

    Studio& studio_;
    UiState& state_;
    std::optional<ContextRequest> shown_;
    TrackRef outputsTrack_;

    enum class Renaming { none, track, midiClip };
    Renaming renaming_ { Renaming::none };
    TrackRef renameTrack_;
    std::uint64_t renameClip_ {};
    char renameBuffer_[128] {};
    bool openRename_ {};
    bool focusRename_ {};
};

} // namespace cydaw::v2
