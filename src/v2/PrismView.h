#pragma once

#include "PresetMenu.h"
#include "DeviceHost.h"
#include "EditorWindows.h"
#include "UiState.h"

#include <cydaw/engine/PrismSynth.h>

#include <imgui.h>

#include <optional>

// Prisme's faces: a pane on its card in Devices (its macros and main knobs,
// a button to open it), and its own window, laid out as the big wavetable
// synths are — OSC (two wavetable oscillators, sub, noise, filter, the
// envelopes and LFOs, the macros), MATRIX (sixteen modulations), FX (the
// track's effects after it) and GLOBAL (voicing). A source dragged onto a
// knob (or a knob's right click) modulates it; a ring round the knob shows
// how far.

namespace cydaw::v2 {

class PrismView final {
public:
    PrismView(DeviceHost& studio, UiState& state);

    // The card's pane: as wide as this.
    [[nodiscard]] static float paneWidth();
    void drawPane(const TrackRef& track, app::NodeGraphComponent& graph, std::uint64_t nodeId, ImVec2 origin, ImVec2 size,
                  ImU32 colour);
    // `detached`: in a window of its own out of the studio's, as a plug-in's.
    // `here`: inside the studio's window, wherever it was last left (on a
    // screen of its own, or one unplugged since).
    void openEditor(const TrackRef& track, std::uint64_t nodeId, bool detached = false, bool here = false);
    [[nodiscard]] bool showsEditor(const TrackRef& track, std::uint64_t nodeId) const { return windows_.shows(track, nodeId); }
    // That instrument's window's id (each has its own window).
    [[nodiscard]] std::string editorWindowId(const TrackRef& track, std::uint64_t nodeId) const
    {
        return windows_.idOf(track, nodeId);
    }
    void drawEditor();

private:
    // The card being drawn, for the helpers below.
    struct Card {
        TrackRef track;
        app::NodeGraphComponent* graph {};
        std::uint64_t nodeId {};
        engine::prism::PrismValues values {};
        // The oscillators' own tables (imported waves), when it has any.
        engine::prism::TablesRef own;
        // What its newest voice plays now, when one plays.
        bool live {};
        engine::VoiceLive heard {};
        // Its settings as they play now: the knobs, the automation lanes at
        // the playhead over them, the voice's own modulations over those.
        engine::prism::PrismValues played {};
    };
    // Where a setting plays now when the automation or its voice's
    // modulations move it away from its knob.
    [[nodiscard]] std::optional<float> heard(int index) const;
    // The filter's curve drawn again where the modulations put it.
    bool drawingHeard_ {};
    // The table an oscillator plays: its own, else the factory one named.
    [[nodiscard]] engine::prism::WavetableRef tableOf(int osc) const;
    void chooseTableFile(int osc);
    [[nodiscard]] bool bind(const TrackRef& track, app::NodeGraphComponent& graph, std::uint64_t nodeId);
    [[nodiscard]] float value(int index) const;
    void set(int index, float value);
    // A knob on a parameter, its name under it (its value while touched).
    bool knob(int index, const char* label, float diameter = 30.0f);
    void toggle(int index, const char* label, ImVec2 size = ImVec2(0.0f, 0.0f));
    void choice(int index, float width);
    void integer(int index, const char* label, float width);
    // What modulates a parameter: menu, drop target, the ring.
    void modulationMenu(int index);
    void addModulation(int source, int index, float amount);
    [[nodiscard]] float modulationRange(int index, float& low, float& high) const;
    void sourceChip(int source, const char* label, ImVec2 size, bool selected, bool* clicked);

    void drawOscillator(int osc, float width, float height);
    void drawSubAndNoise(float width, float height);
    void drawFilter(float width, float height);
    void drawModulators(float width, float height);
    void drawMacros(float width, float height);
    void drawOscTab();
    void drawMatrixTab();
    void drawEffectsTab();
    void drawGlobalTab();

    void drawWavetable(int osc, ImVec2 origin, ImVec2 size, bool compact);
    void drawFilterResponse(ImVec2 origin, ImVec2 size);
    void drawEnvelope(int env, ImVec2 origin, ImVec2 size);
    void drawLfo(int lfo, ImVec2 origin, ImVec2 size);
    // A drawn LFO's display, edited: drag to draw its points, right click
    // for shapes ready made.
    void editDrawnLfo(int lfo, ImVec2 origin, ImVec2 size);

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
    EditorWindows windows_ { "###prismEditor" };
    std::optional<Target> editor_;
    std::string windowId_;
    // One window; false once it is closed.
    bool drawEditorWindow(bool focus);
    // Which envelope (0..3) or LFO (4..7) the modulation section shows.
    int modulator_ { 1 };
    // Which of B and C shows when the three do not fit side by side.
    int secondOscillator_ { 1 };
    // The point last drawn while the mouse is down (-1: none yet).
    int drawnLast_ { -1 };
    // The window to put back in the studio's window on its next frame.
    std::string placeHere_;
    float drawnLastValue_ {};
    std::unique_ptr<juce::FileChooser> chooser_;
};

} // namespace cydaw::v2
