#pragma once

#include "DeviceHost.h"
#include "UiState.h"

#include <imgui.h>

#include <functional>
#include <map>
#include <string>
#include <set>
#include <vector>

// The interface of a device made of a graph: the window its VST3 opens (and
// what the device shows), drawn on a grid. Controls — knobs, faders,
// switches, selectors, an XY pad, value read-outs, labels, frames, filter,
// dynamics and EQ curves, an envelope, scopes and meters — are dropped from
// the palette or made from a published setting, moved and sized with the
// mouse, and bound to the device's published settings. Play mode turns the
// knobs instead of moving them.
//
// It edits the root graph's own interface (NodeGraphComponent::
// editorSetInterface): a plugin exported from the workshop opens exactly
// this layout at this size.

namespace cydaw::v2 {

class InterfaceEditor final {
public:
    InterfaceEditor(DeviceHost& studio, UiState& state);

    // The window, edited (or played), filling the space given.
    void drawCanvas(GraphSlot& slot);
    void setDeviceLayout(bool device);
    [[nodiscard]] bool deviceLayout() const noexcept { return deviceLayout_; }
    [[nodiscard]] const juce::String& lastError() const noexcept { return error_; }
    void drawCode(GraphSlot& slot);
    void drawLayers(GraphSlot& slot);
    void drawNativePalette(GraphSlot& slot);
    bool addInstrumentSection(GraphSlot& slot,const juce::String& type,const juce::String& panelId);
    bool addInstrumentInterface(GraphSlot& slot,std::uint64_t node,bool whole,const juce::String& panelId={});
    // What can go on it: the kinds of controls, and the published settings.
    void drawPalette(GraphSlot& slot);
    // The control picked, or the window when none is.
    void drawProperties(GraphSlot& slot);
    // A device's window as its plugin shows it: played, never edited,
    // filling `size` (its proportions kept). `setValue` turns a setting.
    // `id` keeps each window's gesture apart.
    void drawPlayed(const char* id, const app::ModuleInterfaceState& state, ImVec2 size,
                    const std::function<void(const juce::String&, double)>& setValue,
                    const engine::AudioAnalysisFrame* analysis = nullptr);

    static constexpr const char* kindPayload = "CYDAW_UI_KIND";
    static constexpr const char* parameterPayload = "CYDAW_UI_PARAMETER";

private:
    using Control = app::ModuleInterfaceControl;
    using Kind = app::ModuleControlKind;
    enum class Drag { none, move, resize, play };

    void pull(GraphSlot& slot, bool force = false);
    bool push(GraphSlot& slot, bool commit);
    [[nodiscard]] Control* find(std::uint64_t id);
    [[nodiscard]] const app::ModulePublishedParameter* parameter(const juce::String& key) const;
    [[nodiscard]] double value01(const juce::String& key) const;
    void setValue01(GraphSlot& slot, const juce::String& key, double position);
    // A new control of a kind, its bindings chosen among the published
    // settings (`binding` first when given); its id, 0 and why not.
    std::uint64_t add(GraphSlot& slot, Kind kind, ImVec2 at, const juce::String& binding);
    void autoLayout(GraphSlot& slot);
    [[nodiscard]] std::uint64_t hit(ImVec2 screen) const;
    void drawControl(ImDrawList* list, const Control& control, ImVec2 min, ImVec2 max) const;
    [[nodiscard]] float snap(float value, int cells) const;
    [[nodiscard]] ImVec2 windowSize() const;
    void report(const juce::String& message);

    DeviceHost& studio_;
    UiState& state_;
    app::ModuleInterfaceState interface_;
    const void* slot_ {};
    std::uint64_t revision_ {};
    std::uint64_t selected_ {};
    Drag drag_ { Drag::none };
    Control dragOrigin_;
    ImVec2 dragStart_ {};
    double playStart_ {};
    bool play_ {};
    bool marquee_ {};
    std::set<std::uint64_t> marqueeSelection_;
    bool deviceLayout_ {};
    bool codeMode_ {};
    bool renderingPlayed_ {};
    std::function<void(const juce::String&,double)> componentSet_;
    float zoom_ { 1.0f };
    bool actualSize_ {};
    int libraryInstrument_ {4};
    ImVec2 pan_ {};
    juce::String page_;
    std::set<std::uint64_t> selection_;
    std::map<std::uint64_t, Control> dragOrigins_;
    std::vector<char> code_ = std::vector<char>(1'048'576, 0);
    std::vector<char> cpp_ = std::vector<char>(131'072, 0);
    juce::String codeError_;
    std::map<std::string,std::vector<char>> designText_;
    const cydaw::engine::AudioAnalysisFrame* analysis_ {};
    [[nodiscard]] bool visible(const Control& control) const;
    void styleProperties(GraphSlot& slot, Control& control);
    void drawPageBar();
    void drawWindowProperties(GraphSlot& slot);
    [[nodiscard]] ImU32 surfaceColour(const char* key, ImU32 fallback) const;
    ImVec2 canvasMin_ {};
    ImVec2 canvasMax_ {};
    juce::String error_;
    double errorTime_ {};
    std::vector<char> label_ = std::vector<char>(128, 0);
    // The played windows' gestures.
    struct Played {
        std::uint64_t control {};
        bool dragging {};
        ImVec2 start {};
        double startValue {};
    };
    std::map<std::string, Played> played_;
};

} // namespace cydaw::v2
