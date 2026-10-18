#pragma once

#include <imgui.h>
#include <juce_core/juce_core.h>

#include <cstdint>
#include <string>
#include <vector>

// Compact controls drawn for CyDAW 2: knobs, faders, meters, toggles and icon
// buttons. Each one behaves the same everywhere — drag vertically to change,
// Shift for fine steps, the wheel nudges, a double click restores the
// default, and the value shows while the pointer is over the control.

namespace cydaw::v2::ui {

// The current language. Interface strings are written inline in French and
// English; another language translates the English from
// i18n/v2/<code>.json beside the program ({"English": "translation"}),
// keeping the English for what it does not have.
[[nodiscard]] bool french();
// While one lives, the texts drawn are a module's (a device's card or window,
// an instrument's, a node of the graph, a plug-in's window): English,
// whatever the language (french() false, L() its English).
class ModuleText final {
public:
    ModuleText() noexcept;
    ~ModuleText();
    ModuleText(const ModuleText&) = delete;
    ModuleText& operator=(const ModuleText&) = delete;
};
[[nodiscard]] const char* L(const char* fr, const char* en);
// The same, as a juce::String (inline text is UTF-8).
[[nodiscard]] inline juce::String Ls(const char* fr, const char* en)
{
    return juce::String::fromUTF8(L(fr, en));
}
void setFrench(bool french);
void setLanguage(const juce::String& code);
[[nodiscard]] juce::String language();
// How many interface texts the current language translates (0 in French and
// English, which are inline).
[[nodiscard]] int translationCount();
// Bumped by every change of language (and of the node names' language), so
// that what caches translated names takes them again.
[[nodiscard]] int languageRevision();
void bumpLanguageRevision();

// "label###id": a visible label that may change (language, value) on a
// stable identity.
[[nodiscard]] std::string labelId(const char* label, const char* id);

enum KnobFlags : int {
    knobNone = 0,
    knobBipolar = 1 << 0,     // the arc starts at the centre (pan, detune)
    knobLogarithmic = 1 << 1, // frequency-like ranges
    knobNoLabel = 1 << 2,
    knobInteger = 1 << 3
};

struct KnobStyle {
    float diameter { 30.0f };
    ImU32 colour { 0 };        // 0: the accent
    const char* label {};      // under the knob
    const char* format { "%.2f" };
    const char* unit {};
    // Shown instead of the formatted value (a plug-in's own text).
    const char* valueText {};
    int flags { knobNone };
    float modulation { -1.0f };// a live position (0..1) shown as a dot, <0 for none
    // A setting with this many steps across its course (a number of voices):
    // the knob lands on them, a drag gathering what it needs to reach the
    // next; 0 for one that takes any value.
    int steps {};
    // The room its label has, centred under it (0: the knob's own width); a
    // label wider than that is drawn a little smaller.
    float labelWidth {};
};

// The pointer driven by the control port (tests): knobs then follow its
// movement as given, instead of holding the system's pointer still.
void setScriptedPointer(bool scripted);
// For the item just submitted while it is held (a knob, a value dragged):
// how far the pointer moved this frame. The system's pointer is hidden and
// put back where the drag began, so it never stops at a screen's edge and
// is where it was when let go. `key` keeps where the drag began.
[[nodiscard]] ImVec2 heldMovement(ImGuiID key);

// How fast a drag or the wheel moves a value now: slowly with Ctrl held
// (fine), quickly with Shift, else at its usual pace (1).
[[nodiscard]] float valueSpeed();

// A number typed in. Called each frame for a value's text: `open` (the
// caller saw a click on it, without a drag) opens a field over
// [min, max] holding the value; Enter sets what was typed (a comma is a
// decimal point, a "k" multiplies by a thousand), Escape or a click
// elsewhere leaves it. `scale`: what the text shows of the value (100 for
// a fraction shown in percent). True, with `value` set, when a number was
// typed.
bool valueEntry(ImGuiID id, ImVec2 min, ImVec2 max, double& value, double minimum, double maximum, bool open,
                double scale = 1.0, const char* unit = nullptr);
// The same over the text of a value just drawn: an invisible button of
// `size` at the cursor (where a Dummy would have gone), a click on it
// opens the field.
bool valueEntryButton(const char* id, ImVec2 size, double& value, double minimum, double maximum, double scale = 1.0,
                      const char* unit = nullptr);
// Whether a field is open: the keyboard then belongs to it.
[[nodiscard]] bool valueEntryOpen();

// The wheel this frame went to a knob, a fader or a display under the
// pointer: a view that scrolls with the wheel leaves it to them.
void takeWheel();
[[nodiscard]] bool wheelTaken();

// Returns true while the value changes. The default is restored with a
// double click; Ctrl drags slowly, Shift quickly; a click on its number
// (without a drag) lets it be typed.
bool knob(const char* id, float* value, float minimum, float maximum, float defaultValue,
          const KnobStyle& style = {});

// A vertical fader in decibels with its scale; returns true while it changes.
bool fader(const char* id, float* gainDb, float minimumDb, float maximumDb, ImVec2 size,
           ImU32 colour = 0);

// Peak and RMS of up to two channels, in linear gain. Vertical or not.
void meter(const char* id, ImVec2 size, float peakLeft, float peakRight, float rmsLeft, float rmsRight,
           bool vertical = true);
[[nodiscard]] float meterPosition(float gainDb);

// A small rounded toggle: M, S, R, an icon, a word.
bool toggle(const char* label, bool* value, ImU32 onColour, ImVec2 size = ImVec2(0.0f, 0.0f),
            const char* tooltip = nullptr);
// A toggle showing a little drawing rather than a word, its name in its
// tooltip: `picture` paints it in the box it is given, in the colour given.
using Picture = void (*)(ImDrawList* list, ImVec2 min, ImVec2 max, ImU32 colour);
bool pictureToggle(const char* id, bool* value, ImU32 onColour, Picture picture, const char* tooltip, float width = 0.0f);
// The pictures the editors share.
namespace picture {
void magnet(ImDrawList* list, ImVec2 min, ImVec2 max, ImU32 colour);
void loop(ImDrawList* list, ImVec2 min, ImVec2 max, ImU32 colour);
} // namespace picture
// The magnet of an editor (snapping on the grid), the same everywhere; Alt
// held does the other during a gesture.
bool snapToggle(const char* id, bool* on);
// A clip's loop, the same in every editor (its tooltip says what loops).
bool loopToggle(const char* id, bool* on, const char* tooltip);
// Whether a magnet holds now: on, unless Alt is held (or off, Alt held).
[[nodiscard]] bool snapping(bool on);

// ---- Controllers: learning ------------------------------------------------------------------
// What a controller can move, drawn this frame: a target (ControlTargets.h),
// its name, where it is on screen. In learn mode the controllers tool shows
// them all over the interface; a click picks one.
struct LearnSpot {
    std::string key;
    std::string label;
    ImVec2 min;
    ImVec2 max;
    ImGuiID viewport {};
};
// The target the knobs, faders and toggles drawn inside it drive: they add
// themselves. Anything else adds itself with learnItem() right after it.
class LearnScope {
public:
    LearnScope(std::string key, std::string label);
    ~LearnScope();
    LearnScope(const LearnScope&) = delete;
    LearnScope& operator=(const LearnScope&) = delete;
};
// The last item drawn, for the scope's target or the one given.
void learnItem();
void learnItem(const std::string& key, const std::string& label);
// The track whose cards are drawn now: `prefix` names its knobs' targets
// ("param/<track>/"), empty where nothing can be learnt (a plug-in's window).
class LearnTrackScope {
public:
    LearnTrackScope(std::string prefix, std::string trackName);
    ~LearnTrackScope();
    LearnTrackScope(const LearnTrackScope&) = delete;
    LearnTrackScope& operator=(const LearnTrackScope&) = delete;
};
// A card's knob, by its binding, on the track being drawn (or `prefix`'s).
class LearnParameterScope {
public:
    LearnParameterScope(const juce::String& binding, const juce::String& label, const std::string& prefix = {},
                        const juce::String& trackName = {});
    ~LearnParameterScope();
    LearnParameterScope(const LearnParameterScope&) = delete;
    LearnParameterScope& operator=(const LearnParameterScope&) = delete;
};
void setLearnMode(bool on);
[[nodiscard]] bool learnMode();

// ---- Modulators: dropped on knobs, drawn around them ------------------------------------------
// A modulator dragged by its arrow carries its node id under this name.
inline constexpr const char* modulatorPayload = "CYDAW_MODULATOR";
// What moves a knob: a modulator, by how much of the knob's range (-1..1).
struct KnobModulation {
    std::uint64_t source {};
    float amount {};
    ImU32 colour {};
    std::string name;
};
// The modulations of the cards being drawn, for the knobs drawn inside a
// LearnParameterScope: a modulator dropped on one, the rings around them, a
// drag that sets the armed modulator's amount. Set by the device panel
// around what it draws (ModulationHostScope); none elsewhere.
class ModulationHost {
public:
    virtual ~ModulationHost() = default;
    virtual void modulationsOf(const std::string& binding, std::vector<KnobModulation>& into) = 0;
    // The modulator a knob's drag sets the amount of; 0: the drag turns the knob.
    [[nodiscard]] virtual std::uint64_t armed() const = 0;
    virtual void dropped(const std::string& binding, std::uint64_t source) = 0;
    // 0 takes the modulation away.
    virtual void setAmount(const std::string& binding, std::uint64_t source, float amount) = 0;
};
class ModulationHostScope {
public:
    explicit ModulationHostScope(ModulationHost* host);
    ~ModulationHostScope();
    ModulationHostScope(const ModulationHostScope&) = delete;
    ModulationHostScope& operator=(const ModulationHostScope&) = delete;

private:
    ModulationHost* previous_ {};
};
[[nodiscard]] ModulationHost* modulationHost();
// The knob being drawn's binding (LearnParameterScope), empty outside one.
[[nodiscard]] const std::string& scopedBinding();

// ---- Where things are drawn, for tests driving the pointer ------------------------------------
// While on, each knob drawn records where it is ("knob:<binding>"), and what
// calls recordSpot() the item just drawn, in the main window's coordinates
// (those ui_input takes); the latest drawing wins.
void setSpotRecording(bool on);
void recordSpot(const std::string& key);
[[nodiscard]] std::vector<LearnSpot> recordedSpots();
// The frame's spots, for the overlay (taken: the next frame starts empty).
[[nodiscard]] std::vector<LearnSpot> takeLearnSpots();
// A flat icon button, highlighted when active.
bool iconButton(const char* icon, const char* tooltip, bool active = false, ImU32 activeColour = 0,
                ImVec2 size = ImVec2(0.0f, 0.0f));
// A button with a filled accent background for the main action of a panel.
bool primaryButton(const char* label, ImVec2 size = ImVec2(0.0f, 0.0f));
// A thin vertical bar between toolbar groups.
void toolbarSeparator();
// A delayed tooltip for the last item.
void tooltip(const char* text);
// Text wrapped at `screenX` (ImGui::PushTextWrapPos takes the window's own
// space, not the screen's); ImGui::PopTextWrapPos() after.
void pushTextWrapAt(float screenX);
// The band over a timeline to grab: left and right move through it, down
// zooms in, up zooms out, the point grabbed staying under the pointer. True
// on a double click (show everything). `position` is the first unit shown,
// `pixelsPerUnit` the zoom; ticks every `tick` units, longer every `strong`.
bool timelineBand(const char* id, ImVec2 min, ImVec2 max, double& position, double& pixelsPerUnit, double minimumZoom,
                  double maximumZoom, double tick, double strong, double minimumPosition = 0.0);
// The same beside rows (tracks, keys): up and down move through them,
// toward them (left) zooms in, away zooms out. `scroll` in pixels, the
// content's heights in proportion to `zoom`. True on a double click.
bool rowsBand(const char* id, ImVec2 min, ImVec2 max, float& scroll, float& zoom, float minimumZoom, float maximumZoom);
// A drag field styled for numbers (tempo, positions).
bool dragNumber(const char* id, float* value, float speed, float minimum, float maximum,
                const char* format, float width);
// Section title inside a panel.
void sectionTitle(const char* text);
// A coloured square, as the track colour in lists; returns true when clicked.
bool colourChip(const char* id, ImU32 colour, ImVec2 size);
// Text centred in a rectangle.
void centredText(ImDrawList* list, ImVec2 min, ImVec2 max, ImU32 colour, const char* text);
// A centred title and message filling the rest of a panel.
void emptyState(const char* title, const char* message);
// A size for people: "6,2 Go", "312 Mo" ("6.2 GB", "312 MB").
[[nodiscard]] juce::String sizeText(juce::int64 bytes);
// Text clipped to a width with an ellipsis.
void clippedText(ImDrawList* list, ImVec2 position, float maxWidth, ImU32 colour, const char* text);

// The current font size before the global scales, for PushFont(nullptr, …).
[[nodiscard]] float currentFontBase();

// ---- Windows of their own -------------------------------------------------
// A window that leaves the main one for a window of its own on the desktop,
// as a plug-in's editor does (another screen, beside the studio), and comes
// back; by its ImGui id ("###prismEditor"). Call before its Begin: it is
// placed when it goes or comes, and kept out while out.
void beforeDetachable(const char* id);
// A plug-in's window: an instrument's window fills it (no title, no cross,
// nothing to take out); set once by the plug-in.
void setEmbedded(bool embedded);
[[nodiscard]] bool embedded();
// ImGui::Begin for an instrument's or a device's window, filling the
// plug-in's window when embedded.
bool beginEditorWindow(const char* name, bool* open, ImGuiWindowFlags flags);
// The button of its header that sends it out or brings it back; true when
// pressed.
bool detachButton(const char* id, ImVec2 size = ImVec2(0.0f, 0.0f));
[[nodiscard]] bool isDetached(const char* id);
void setDetached(const char* id, bool detached);

[[nodiscard]] inline float dp(const float value)
{
    return value * ImGui::GetStyle().FontScaleDpi;
}

} // namespace cydaw::v2::ui
