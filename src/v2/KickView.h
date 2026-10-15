#pragma once

#include "PresetMenu.h"
#include "DeviceHost.h"
#include "EditorWindows.h"
#include "UiState.h"

#include <cydaw/engine/KickSynth.h>

#include <imgui.h>

#include <optional>
#include <vector>

// Kick's faces: a pane on its card in Devices (its picture and the knobs that
// matter most, a button to open it), and its own window: the kick drawn as it
// plays — its wave, its pitch falling, its envelope — with handles to drag
// the pitch and the envelope, and its sections (body, amplitude, click, sub,
// drive, tone, global) as rows of knobs.

namespace cydaw::v2 {

class KickView final {
public:
    KickView(DeviceHost& studio, UiState& state);

    // The card's pane: as wide as this.
    [[nodiscard]] static float paneWidth();
    void drawPane(const TrackRef& track, app::NodeGraphComponent& graph, std::uint64_t nodeId, ImVec2 origin, ImVec2 size,
                  ImU32 colour);
    // `detached`: in a window of its own out of the studio's, as a plug-in's.
    void openEditor(const TrackRef& track, std::uint64_t nodeId, bool detached = false);
    [[nodiscard]] bool showsEditor(const TrackRef& track, std::uint64_t nodeId) const { return windows_.shows(track, nodeId); }
    // That instrument's window's id (each has its own window).
    [[nodiscard]] std::string editorWindowId(const TrackRef& track, std::uint64_t nodeId) const
    {
        return windows_.idOf(track, nodeId);
    }
    void drawEditor();

private:
    struct Card {
        TrackRef track;
        app::NodeGraphComponent* graph {};
        std::uint64_t nodeId {};
        engine::kick::KickValues values {};
    };
    [[nodiscard]] bool bind(const TrackRef& track, app::NodeGraphComponent& graph, std::uint64_t nodeId);
    [[nodiscard]] float value(int index) const;
    void set(int index, float value);
    bool knob(int index, const char* label, float diameter = 32.0f);
    void choice(int index, float width);
    void toggle(int index, const char* label);

    // The kick drawn: its wave, its pitch, its envelope; handles to drag
    // when `interactive`.
    void drawDisplay(ImVec2 origin, ImVec2 size, bool interactive);
    // The kick as the voice plays it, again only when a setting moved.
    const std::vector<float>& rendered();
    void section(const char* id, const char* title, float width, float height, void (KickView::*body)());
    void drawBody();
    void drawAmplitude();
    void drawClick();
    void drawSub();
    void drawDrive();
    void drawTone();
    void drawGlobal();
    // The KICK tab: the picture, then the sections.
    void drawKickTab();

    DeviceHost& studio_;
    UiState& state_;
    PresetMenu presets_;
    Card card_;
    struct Target {
        TrackRef track;
        std::uint64_t nodeId {};
    };
    // Its windows (one for each instrument opened), and the one being drawn:
    // its instrument, its id.
    EditorWindows windows_ { "###kickEditor" };
    std::optional<Target> editor_;
    std::string windowId_;
    // One window; false once it is closed.
    bool drawEditorWindow(bool focus);
    // The pictures' samples and the settings they were made with: a few,
    // for a card and a window of two tracks drawn in the same frame.
    struct Picture {
        engine::kick::KickValues values {};
        std::vector<float> samples;
        int used {};
    };
    std::vector<Picture> pictures_;
    int frame_ {};
    // The handle being dragged (-1: none) and the time the picture spanned
    // when it was taken (the span follows the kick, not under the pointer).
    int dragging_ { -1 };
    double dragSpan_ {};
};

} // namespace cydaw::v2
