#pragma once

#include "PresetMenu.h"
#include "DeviceHost.h"
#include "SynthLayouts.h"
#include "EditorWindows.h"
#include "UiState.h"

#include <cydaw/engine/SynthFamily.h>

#include <imgui.h>

#include <optional>

// The faces of the synth family's models (Ambre and the ones after it), all
// drawn from their parameter tables and their layouts (SynthLayouts.h): the
// card in Devices (the model's panels small, side by side, each its picture
// and its knobs, as wide as its height lets them be), and the window: the
// model's panels in rows,
// each with its picture (the wave, the filter's curve dragged, the
// envelopes' points dragged, the LFOs' shapes) and its knobs; its presets;
// the effects after it.

namespace cydaw::v2 {

class SynthView final {
public:
    SynthView(DeviceHost& studio, UiState& state);

    // A card of the family (its type: "ambre-synth").
    [[nodiscard]] static bool handles(const juce::String& typeKey);
    // The card's width at that height (its panels side by side).
    [[nodiscard]] float paneWidth(app::NodeGraphComponent& graph, std::uint64_t nodeId, float height);
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
        engine::synth::Model model { engine::synth::Model::none };
        engine::synth::Values values {};
        // What its newest voice plays now, when one plays.
        bool live {};
        engine::VoiceLive heard {};
        // Its settings as they play now: the knobs, the automation lanes at
        // the playhead over them, the voice's own modulations over those.
        engine::synth::Values played {};
    };
    // Where a setting plays now when the automation or its voice's
    // modulations move it away from its knob.
    [[nodiscard]] std::optional<float> heard(int index) const;
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
    // full; its height. Drawn only when `draw`; those below `maxHeight` (0:
    // no limit) left out (clipped_ says so). Greyed controls side by side
    // waiting on the same setting carry one note over them all.
    float flow(const std::vector<synth_layout::Control>& controls, ImVec2 origin, float width, float diameter, bool draw,
               float maxHeight = 0.0f);
    [[nodiscard]] float pictureHeight(synth_layout::Picture picture) const;
    [[nodiscard]] float panelHeight(const synth_layout::Panel& panel, float width) const;
    void panel(const synth_layout::Panel& panel, float width, float height);
    [[nodiscard]] ImU32 sectionColour(int section) const;
    // The card's panels, each as narrow as its controls allow at that height,
    // its picture when a row of knobs still fits under it.
    struct CardSection {
        const synth_layout::Panel* panel {};
        float width {};
        bool pictured {};
    };
    [[nodiscard]] std::vector<CardSection> cardSections(float height);
    [[nodiscard]] float compactPictureHeight(synth_layout::Picture picture) const;
    [[nodiscard]] float cardTitleHeight() const;
    // The size of the names under the knobs (smaller on the card).
    [[nodiscard]] float labelScale() const;
    // Drawing the card (smaller choices); a flow that stopped short.
    bool compact_ {};
    bool clipped_ {};
    // The knob being drawn: its cell's width (its name's, on the card).
    float knobCell_ {};

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
    EditorWindows windows_ { "###synthEditor" };
    std::optional<Target> editor_;
    std::string windowId_;
    // One window; false once it is closed.
    bool drawEditorWindow(bool focus);
    // Why the control being drawn is greyed (what would make it act), shown
    // in its tooltip; empty while it acts. And the same, short, written on
    // the control itself: "si UNISSON" over "≥ 2".
    std::string greyedBecause_;
    std::string greyedWho_;
    std::string greyedWhen_;
    // What would make `control` act, into the three above.
    void describeCondition(const synth_layout::Control& control);
    // Writes greyedWho_ / greyedWhen_ on the choice or switch just drawn (a
    // knob writes "si …" under itself); not when its group has one note.
    void markGreyed(bool knob) const;
    bool quietGreyed_ {};
    // The picture being dragged (its widget, 0 none) and which point.
    ImGuiID dragging_ {};
    int draggingPoint_ { -1 };
};

} // namespace cydaw::v2
