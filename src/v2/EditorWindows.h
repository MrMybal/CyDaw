#pragma once

#include "StudioTypes.h"
#include "Widgets.h"

#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

// The windows one kind of instrument has open (Prisme's, Kick's, a synth's
// of the family…): one for each instrument opened, or only the last one when
// the option says one at a time (UiState::manyInstrumentWindows). Each has an
// id of its own, so that each keeps its place and its size and goes out of
// the studio's window on its own.

namespace cydaw::v2 {

class EditorWindows final {
public:
    explicit EditorWindows(const char* base) : base_(base) {}

    // That instrument's window opened (brought forward when it is): the
    // others closed unless `many`. Its id.
    std::string open(const TrackRef& track, const std::uint64_t nodeId, const bool many)
    {
        const auto found = std::find_if(windows_.begin(), windows_.end(),
                                        [&](const Window& window) { return window.track == track && window.nodeId == nodeId; });
        if (found != windows_.end()) {
            found->focus = true;
        } else {
            if (!many)
                windows_.clear();
            windows_.push_back({ track, nodeId, true });
        }
        if (!many)
            std::erase_if(windows_, [&](const Window& window) { return !(window.track == track && window.nodeId == nodeId); });
        return idOf(track, nodeId);
    }

    [[nodiscard]] bool shows(const TrackRef& track, const std::uint64_t nodeId) const
    {
        return std::any_of(windows_.begin(), windows_.end(),
                           [&](const Window& window) { return window.track == track && window.nodeId == nodeId; });
    }

    // "###synthEditor" then the instrument: its track and its card.
    [[nodiscard]] std::string idOf(const TrackRef& track, const std::uint64_t nodeId) const
    {
        return base_ + "-" + (track.midi ? "m" + std::to_string(track.midiId) : "a" + std::to_string(track.audioIndex))
               + (track.master ? "-master-" + std::to_string(track.bus) : std::string()) + "-" + std::to_string(nodeId);
    }

    // Each window drawn by `draw(track, nodeId, focus)`; one it returns false
    // for is closed (by its cross, or its instrument gone).
    template <typename Draw>
    void each(Draw&& draw)
    {
        for (std::size_t at = 0; at < windows_.size();) {
            const auto window = windows_[at];
            windows_[at].focus = false;
            const auto keep = draw(window.track, window.nodeId, window.focus);
            // What it drew may have opened or closed another: found again.
            const auto now = std::find_if(windows_.begin(), windows_.end(), [&](const Window& other) {
                return other.track == window.track && other.nodeId == window.nodeId;
            });
            if (now == windows_.end())
                continue;
            if (!keep) {
                windows_.erase(now);
                continue;
            }
            at = static_cast<std::size_t>(now - windows_.begin()) + 1;
        }
    }

    [[nodiscard]] bool empty() const noexcept { return windows_.empty(); }

    // Where a window appears the first time (before its Begin): each one a
    // step lower and to the right of the one opened before it, not on it.
    void placeFirst(const TrackRef& track, const std::uint64_t nodeId) const
    {
        const auto found = std::find_if(windows_.begin(), windows_.end(),
                                        [&](const Window& window) { return window.track == track && window.nodeId == nodeId; });
        const auto order = static_cast<float>(found == windows_.end() ? 0 : (found - windows_.begin()) % 8);
        const auto* main = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(main->Pos.x + ui::dp(60.0f) + order * ui::dp(32.0f), main->Pos.y + ui::dp(60.0f) + order * ui::dp(32.0f)),
                                ImGuiCond_FirstUseEver);
    }

private:
    struct Window {
        TrackRef track;
        std::uint64_t nodeId {};
        bool focus {};
    };
    std::string base_;
    std::vector<Window> windows_;
};

} // namespace cydaw::v2
