#pragma once

#include "PresetMenu.h"
#include "Studio.h"
#include "SynthLayouts.h"
#include "UiState.h"

#include <cydaw/engine/SynthFamily.h>

#include <imgui.h>

#include <optional>

// The faces of the synth family's models (Ambre and the ones after it), all
// drawn from their parameter tables and their layouts (SynthLayouts.h): a
// pane on the card in Devices (a picture, one row of the knobs that matter
// most, a button to open it), and the window: the model's panels in rows,
// each with its picture (the wave, the filter's curve dragged, the
// envelopes' points dragged, the LFOs' shapes) and its knobs; its presets;
// the effects after it.

namespace cydaw::v2 {

class SynthView final {
public:
    SynthView(Studio& studio, UiState& state);

    // A card of the family (its type: "ambre-synth").
    [[nodiscard]] static bool handles(const juce::String& typeKey);
    [[nodiscard]] static float paneWidth();
    void drawPane(const TrackRef& track, app::NodeGraphComponent& graph, std::uint64_t nodeId, ImVec2 origin, ImVec2 size,
                  ImU32 colour);
    // `detached`: in a window of its own out of the studio's, as a plug-in's.
    void openEditor(const TrackRef& track, std::uint64_t nodeId, bool detached = false);
    [[nodiscard]] bool showsEditor(const TrackRef& track, std::uint64_t nodeId) const
    {
        return editor_ && editor_->track == track && editor_->nodeId == nodeId;
    }
    static constexpr const char* editorWindowId = "###synthEditor";
    void drawEditor();

private:
    struct Card {
        TrackRef track;
        app::NodeGraphComponent* graph {};
        std::uint64_t nodeId {};
        engine::synth::Model model { engine::synth::Model::none };
        engine::synth::Values values {};
    };
    [[nodiscard]] bool bind(const TrackRef& track, app::NodeGraphComponent& graph, std::uint64_t nodeId);
    [[nodiscard]] int indexOf(const char* key) const;
    [[nodiscard]] float value(int index) const;
    [[nodiscard]] float valueOf(const char* key) const;
    void set(int index, float value);
    [[nodiscard]] juce::String bindingOf(int index) const;
    [[nodiscard]] ImU32 colourOf(int index) const;

    // Whether a control's `onlyWhen` is met.
    [[nodiscard]] bool met(const synth_layout::Control& control) const;
    // One control: a knob, a choice or a toggle, as its kind is.
    [[nodiscard]] ImVec2 controlSize(const synth_layout::Control& control, float diameter) const;
    void control(const synth_layout::Control& control, float diameter);
    bool knob(int index, const char* label, float diameter);
    void choice(int index, float width);
    void toggle(int index, const char* label, float width);
    // The controls of a panel from left to right, onto the next line when
    // full; its height. Drawn only when `draw`.
    float flow(const std::vector<synth_layout::Control>& controls, ImVec2 origin, float width, float diameter, bool draw);
    [[nodiscard]] float pictureHeight(synth_layout::Picture picture) const;
    [[nodiscard]] float panelHeight(const synth_layout::Panel& panel, float width) const;
    void panel(const synth_layout::Panel& panel, float width, float height);

    // The pictures.
    void drawPicture(synth_layout::Picture picture, const char* key, const char* second, ImVec2 origin, ImVec2 size, bool interactive);
    void drawWave(ImDrawList* list, const char* key, ImVec2 origin, ImVec2 size, ImU32 colour);
    void drawFilter(ImDrawList* list, const char* key, const char* second, ImVec2 origin, ImVec2 size, bool interactive);
    void drawEnvelope(ImDrawList* list, const char* prefix, ImVec2 origin, ImVec2 size, bool interactive);
    void drawLfo(ImDrawList* list, const char* prefix, ImVec2 origin, ImVec2 size);
    void drawSteps(ImDrawList* list, const char* prefix, ImVec2 origin, ImVec2 size, bool interactive);
    void drawOrb(ImDrawList* list, const char* prefix, ImVec2 origin, ImVec2 size, bool interactive);
    // The filters' gain at `hz` (dB), as their settings draw it.
    [[nodiscard]] double filterDb(double hz) const;
    // The oscillators' sum at `phase` (in periods of the note).
    [[nodiscard]] double waveAt(double phase) const;

    void drawSynthTab(const synth_layout::Page& page);

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
    // The picture's point being dragged ("" none) and which.
    std::string dragging_;
    int draggingPoint_ { -1 };
};

} // namespace cydaw::v2
