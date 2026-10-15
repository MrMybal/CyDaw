#pragma once

#include "DeviceVisuals.h"

#include <functional>
#include <vector>

// The studio effects' displays (engine/ProEffectsDesign.h): Réverbe Pro,
// Limiteur Pro and Filtre Pro on their cards and in their editor windows,
// and the control panel of Compresseur Pro's window. Their panes take every
// parameter of the effect, in its catalogue's order.

namespace cydaw::v2::visual {

// The file a card holds (a cabinet's impulse response), for the panes that
// show it; set around a card's panes by whoever draws it (CardFiles.h).
struct CardFile {
    juce::String name; // empty: none loaded
    // Its frequency response: dB at cardFileCurvePoints frequencies spaced
    // evenly in octaves from 20 Hz to 20 kHz, its middle band at 0 dB.
    const std::vector<float>* curveDb {};
    std::function<void()> load;
    std::function<void()> clear;
};
inline constexpr int cardFileCurvePoints = 160;
// Null: the card being drawn holds none.
void setCardFile(const CardFile* file);

// A card's display inside min..max; true when a parameter changed.
bool studioPane(const Pane& pane, std::vector<Param>& parameters, ImVec2 min, ImVec2 max, ImU32 colour, const Live& live,
                const Setter& set);

// The editor window: the display large, and every control.
bool studioLargePane(const Pane& pane, std::vector<Param>& parameters, ImVec2 position, ImVec2 size, ImU32 colour,
                     const Live& live, const Setter& set);

// Compresseur Pro's controls under its display in the editor window: its
// styles, channels, options and every knob.
bool compressorStudioPanel(const Pane& pane, std::vector<Param>& parameters, ImVec2 origin, float width, ImU32 colour,
                           const Live& live, const Setter& set);
[[nodiscard]] float compressorStudioPanelHeight();

// The controls of a device without a panel of its own, under its display
// in its window: a switch, a choice as buttons named by its values (a list
// when they are many), the rest as knobs, large; the display's in a first
// section, the device's others in a second. Returns the height it takes;
// with `draw` false it only measures.
float nativeControls(std::vector<Param>& parameters, const std::vector<int>& main, const std::vector<int>& others, const char* mainTitle,
                     ImVec2 origin, float width, ImU32 colour, const Live& live, const Setter& set, bool draw, bool* changed);

// A control small, on a device's card: a switch, a list under its name, or
// a knob with its name and its value (a click on it types one in). Its size
// at that knob size; drawn at `origin`, `width` wide.
[[nodiscard]] ImVec2 compactControlSize(const Param& parameter, float diameter);
bool compactControl(Param& parameter, ImVec2 origin, float width, ImU32 colour, const Live& live, const Setter& set,
                    float diameter);

// Sections of controls large (a device's window), side by side as they fit,
// onto more rows when they do not: each its title, its choices, its knobs.
// Returns the height; drawn only when `draw`.
struct ControlGroup {
    juce::String title;
    std::vector<int> indices;
};
float controlGroups(std::vector<Param>& parameters, const std::vector<ControlGroup>& groups, ImVec2 origin, float width,
                    ImU32 colour, const Live& live, const Setter& set, bool draw, bool* changed);

// Which of a studio pane's parameters its card shows as fields (the amp's:
// those of the channel chosen).
[[nodiscard]] std::vector<int> studioCardFields(const Pane& pane, std::vector<Param>& parameters);

// A value as these effects show it ("Opto", "4x", "Série"…); empty when the
// parameter is none of theirs.
[[nodiscard]] juce::String studioFormat(const Param& parameter, double value);

// A parameter of theirs that takes whole steps (a style, a type, a switch).
[[nodiscard]] bool studioStepped(const Param& parameter);

// How wide an added effect's display is for its height.
[[nodiscard]] float addedPaneFactor(PaneKind kind);

} // namespace cydaw::v2::visual
