#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

namespace cydaw::app {

// One palette for the whole application. Every component used to pick its own
// dark blue, so the chrome, the panels and the editors were each a slightly
// different shade and the accent colour meant something different in each.
// Roles, not shades: a surface is a surface everywhere, "live" is always teal,
// "selected" is always amber, and recording is the only red.
namespace theme {
constexpr juce::uint32 background = 0xff101217;
constexpr juce::uint32 chrome = 0xff161920;
constexpr juce::uint32 panel = 0xff181b22;
constexpr juce::uint32 sunken = 0xff0d1015;
constexpr juce::uint32 border = 0xff2d333e;
constexpr juce::uint32 borderStrong = 0xff3c4453;
constexpr juce::uint32 primaryText = 0xffe8ebf2;
constexpr juce::uint32 secondaryText = 0xff8e97a9;
constexpr juce::uint32 accent = 0xff75d6ac;
constexpr juce::uint32 selection = 0xffff8a00;
constexpr juce::uint32 record = 0xffff5964;
} // namespace theme

class CyDawLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    CyDawLookAndFeel();

    void drawButtonBackground(juce::Graphics& graphics,
                              juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool highlighted,
                              bool down) override;
    void drawButtonText(juce::Graphics& graphics,
                        juce::TextButton& button,
                        bool highlighted,
                        bool down) override;
    void drawToggleButton(juce::Graphics& graphics,
                          juce::ToggleButton& button,
                          bool highlighted,
                          bool down) override;
    void drawComboBox(juce::Graphics& graphics,
                      int width,
                      int height,
                      bool buttonDown,
                      int buttonX,
                      int buttonY,
                      int buttonWidth,
                      int buttonHeight,
                      juce::ComboBox& box) override;
    void drawLinearSlider(juce::Graphics& graphics,
                          int x,
                          int y,
                          int width,
                          int height,
                          float sliderPosition,
                          float minimumSliderPosition,
                          float maximumSliderPosition,
                          juce::Slider::SliderStyle style,
                          juce::Slider& slider) override;
    void drawRotarySlider(juce::Graphics& graphics,
                          int x,
                          int y,
                          int width,
                          int height,
                          float sliderPosition,
                          float rotaryStartAngle,
                          float rotaryEndAngle,
                          juce::Slider& slider) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    // Without these two, JUCE draws combo text at up to 16 px while the buttons
    // beside it sit at 11 px — the selectors looked like a different toolbar,
    // and their labels clipped long before the box was actually full.
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;

    // Label font shared by the chrome: toolbar buttons, panel tabs and section
    // headers. Uppercase set in bold eats horizontal space and reads as shouting
    // at this size, so the chrome uses a lighter weight with real tracking.
    [[nodiscard]] static juce::Font labelFont(float height, bool emphasised = false);

    // Chrome that carries no frame of its own: menu-bar entries, and glyph
    // buttons that sit inside a group which draws the frame for them. A frame
    // around every single control is what made the toolbar read as a wall of
    // identical boxes rather than as groups of related commands.
    static void setFlat(juce::Button& button, bool flat = true);
    [[nodiscard]] static bool isFlat(const juce::Button& button);

    // A button whose label is a symbol rather than a word. Symbols carry no
    // word shape to read, so they need the size the chrome's text does not.
    static void setGlyphButton(juce::Button& button);
    [[nodiscard]] static bool isGlyphButton(const juce::Button& button);

    // The accent bar every docked panel header carries. The inspector drew it
    // in amber and the browser in green, so two panels of the same kind looked
    // like parts of two different applications.
    static void drawPanelAccent(juce::Graphics& graphics, float x, float y);

private:
    static void drawActiveMarker(juce::Graphics&, const juce::Rectangle<float>& bounds);

public:
};

// Width a text button needs so its label is never clipped.
//
// Toolbar widths used to be hard-coded in pixels, tuned against the English
// strings. Every longer translation was then silently truncated — the interface
// was showing "AFFICH...", "CHAR...", and a browser button reduced to "+...".
// Measuring the label instead means a button is as wide as what it says, in
// every language.
// Accepts any Button: the chrome mixes plain actions with toggles that report
// state, and both have to be wide enough for what they say.
[[nodiscard]] int preferredTextButtonWidth(const juce::Button& button,
                                           int buttonHeight,
                                           int minimumWidth = 0,
                                           int horizontalPadding = 16);

// Same measurement for a combo box, which has to leave room for its arrow.
[[nodiscard]] int preferredComboBoxWidth(const juce::ComboBox& box,
                                         int boxHeight,
                                         int minimumWidth = 0);

} // namespace cydaw::app
