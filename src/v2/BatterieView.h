#pragma once

#include "PresetMenu.h"
#include "Studio.h"
#include "UiState.h"

#include <cydaw/engine/DrumKit.h>

#include <imgui.h>

#include <optional>

// Batterie's faces: a pane on its card in Devices (the kit drawn, the pieces'
// levels, a button to open it), and its window: the kit drawn large (a click
// picks a piece), the piece's settings; a console with a strip a piece; the
// grooves, put on the track as MIDI clips; the effects after it.

namespace cydaw::v2 {

class BatterieView final {
public:
    BatterieView(Studio& studio, UiState& state);

    [[nodiscard]] static bool handles(const juce::String& typeKey) { return typeKey == "batterie"; }
    [[nodiscard]] static float paneWidth();
    void drawPane(const TrackRef& track, app::NodeGraphComponent& graph, std::uint64_t nodeId, ImVec2 origin, ImVec2 size,
                  ImU32 colour);
    // `detached`: in a window of its own out of the studio's, as a plug-in's;
    // `mics`: on its kit's controls.
    void openEditor(const TrackRef& track, std::uint64_t nodeId, bool detached = false, bool mics = false);
    [[nodiscard]] bool showsEditor(const TrackRef& track, std::uint64_t nodeId) const
    {
        return editor_ && editor_->track == track && editor_->nodeId == nodeId;
    }
    static constexpr const char* editorWindowId = "###batterieEditor";
    void drawEditor();

private:
    struct Card {
        TrackRef track;
        app::NodeGraphComponent* graph {};
        std::uint64_t nodeId {};
        engine::drum::Values values {};
    };
    [[nodiscard]] bool bind(const TrackRef& track, app::NodeGraphComponent& graph, std::uint64_t nodeId);
    [[nodiscard]] float value(int index) const;
    void set(int index, float value);
    bool knob(int index, const char* label, float diameter = 32.0f);
    void toggle(int index, const char* label, ImVec2 size);
    void kitChoice(float width);

    // The kit drawn; a click picks a piece when `interactive`.
    void drawKit(ImVec2 origin, ImVec2 size, bool interactive);
    void drawPiecePanel();
    void drawKitTab();
    void drawConsoleTab();
    void drawGroovesTab();

    Studio& studio_;
    UiState& state_;
    PresetMenu presets_;
    Card card_;
    struct Target {
        TrackRef track;
        std::uint64_t nodeId {};
    };
    std::optional<Target> editor_;
    bool focusEditor_ {};
    bool showMics_ {};
    engine::drum::Piece selected_ { engine::drum::Piece::snare };
    // The grooves tab: the one chosen, how many bars, the fill to end on.
    int groove_ {};
    int bars_ { 4 };
    int fill_ { -1 };
    juce::String grooveReport_;
};

} // namespace cydaw::v2
