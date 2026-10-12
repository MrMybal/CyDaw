#include "CyDawLookAndFeel.h"
#include "KnobSlider.h"

#include <algorithm>

namespace cydaw::app {
namespace {
constexpr auto surface = 0xff292c30;
constexpr auto raised = 0xff393d41;
constexpr auto outline = 0xff50555a;
constexpr auto text = 0xffe8eaec;
constexpr auto mutedText = 0xff969ca2;
constexpr auto orange = 0xffff8a00;
// A toggled control reports state; it is not an alarm. Filling every active tab
// and chip with the same saturated orange used for transport and recording left
// the chrome with one colour meaning five different things. An active control
// now reads as raised, and carries a thin accent bar in the teal the rest of the
// application already uses for "this one is live".
constexpr auto active = 0xff333c45;
constexpr auto accent = theme::accent;
constexpr auto flatProperty = "cydawFlat";
constexpr auto glyphProperty = "cydawGlyph";
}

CyDawLookAndFeel::CyDawLookAndFeel()
{
    setColour(juce::TextButton::buttonColourId, juce::Colour(surface));
    setColour(juce::TextButton::buttonOnColourId, juce::Colour(active));
    setColour(juce::TextButton::textColourOffId, juce::Colour(text));
    setColour(juce::TextButton::textColourOnId, juce::Colour(0xfff4f7f9));
    setColour(juce::ToggleButton::textColourId, juce::Colour(text));
    setColour(juce::Label::textColourId, juce::Colour(text));
    setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff202225));
    setColour(juce::ComboBox::outlineColourId, juce::Colour(outline));
    setColour(juce::ComboBox::textColourId, juce::Colour(text));
    setColour(juce::ComboBox::arrowColourId, juce::Colour(mutedText));
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff24272a));
    setColour(juce::PopupMenu::textColourId, juce::Colour(text));
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(orange));
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colour(0xff111111));
    setColour(juce::Slider::backgroundColourId, juce::Colour(0xff151719));
    setColour(juce::Slider::trackColourId, juce::Colour(orange));
    setColour(juce::Slider::thumbColourId, juce::Colour(0xfff0f2f3));
    setColour(juce::Slider::textBoxTextColourId, juce::Colour(text));
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff1b1d20));
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0x00000000));
    setColour(juce::ScrollBar::thumbColourId, juce::Colour(0xff666b70));
}

void CyDawLookAndFeel::setFlat(juce::Button& button, const bool flat)
{
    button.getProperties().set(flatProperty, flat);
}

bool CyDawLookAndFeel::isFlat(const juce::Button& button)
{
    return static_cast<bool>(button.getProperties().getWithDefault(
        flatProperty, false));
}

void CyDawLookAndFeel::setGlyphButton(juce::Button& button)
{
    button.getProperties().set(glyphProperty, true);
}

bool CyDawLookAndFeel::isGlyphButton(const juce::Button& button)
{
    return static_cast<bool>(button.getProperties().getWithDefault(
        glyphProperty, false));
}

void CyDawLookAndFeel::drawPanelAccent(juce::Graphics& graphics, const float x,
                                       const float y)
{
    graphics.setColour(juce::Colour(theme::accent));
    graphics.fillRoundedRectangle({ x, y, 3.0f, 18.0f }, 1.5f);
}

void CyDawLookAndFeel::drawButtonBackground(juce::Graphics& graphics,
                                             juce::Button& button,
                                             const juce::Colour& backgroundColour,
                                             const bool highlighted,
                                             const bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    if (isFlat(button)) {
        // No frame at rest: the group around it supplies the structure. It
        // still has to answer the pointer, so it lights up under it — unless
        // it is unavailable. Lighting up under the cursor and again on the
        // press, then doing nothing, is exactly how a control that is merely
        // disabled reads as one that is broken.
        if ((button.isEnabled() && (down || highlighted))
            || button.getToggleState()) {
            graphics.setColour(juce::Colour(down ? 0xff3a424c
                                            : highlighted ? 0xff2c333c
                                                          : active));
            graphics.fillRoundedRectangle(bounds, 2.5f);
        }
        if (button.getToggleState() && button.isEnabled())
            drawActiveMarker(graphics, bounds);
        return;
    }
    auto colour = backgroundColour;
    if (!button.isEnabled())
        colour = colour.darker(0.35f);
    else if (down)
        colour = colour.brighter(0.18f);
    else if (highlighted)
        colour = colour.brighter(0.09f);

    graphics.setColour(colour);
    graphics.fillRoundedRectangle(bounds, 2.5f);
    graphics.setColour(colour == juce::Colour(orange) ? colour.brighter(0.18f)
                                                       : juce::Colour(outline));
    graphics.drawRoundedRectangle(bounds, 2.5f, 1.0f);
    if (button.getToggleState() && button.isEnabled())
        drawActiveMarker(graphics, bounds);
}

void CyDawLookAndFeel::drawActiveMarker(juce::Graphics& graphics,
                                        const juce::Rectangle<float>& bounds)
{
    // A 2 px bar along the top edge, inset so it follows the rounded corners.
    auto marker = bounds.reduced(3.0f, 0.0f).withHeight(2.0f).translated(0.0f, 1.5f);
    graphics.setColour(juce::Colour(accent));
    graphics.fillRoundedRectangle(marker, 1.0f);
}

void CyDawLookAndFeel::drawButtonText(juce::Graphics& graphics,
                                       juce::TextButton& button,
                                       const bool,
                                       const bool)
{
    graphics.setFont(getTextButtonFont(button, button.getHeight()));
    graphics.setColour(button.findColour(button.getToggleState()
                                             ? juce::TextButton::textColourOnId
                                             : juce::TextButton::textColourOffId)
                           .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.45f));
    graphics.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(5, 1),
                            juce::Justification::centred, 1, 0.75f);
}

void CyDawLookAndFeel::drawToggleButton(juce::Graphics& graphics,
                                         juce::ToggleButton& button,
                                         const bool highlighted,
                                         const bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    if (isFlat(button)) {
        if (down || highlighted || button.getToggleState()) {
            graphics.setColour(juce::Colour(down ? 0xff3a424c
                                            : highlighted ? 0xff2c333c
                                                          : active));
            graphics.fillRoundedRectangle(bounds, 2.5f);
        }
        if (button.getToggleState())
            drawActiveMarker(graphics, bounds);
    } else {
    auto colour = button.getToggleState() ? juce::Colour(active) : juce::Colour(surface);
    if (down || highlighted)
        colour = colour.brighter(down ? 0.16f : 0.08f);
    graphics.setColour(colour);
    graphics.fillRoundedRectangle(bounds, 2.5f);
    graphics.setColour(button.getToggleState() ? juce::Colour(accent).withAlpha(0.55f)
                                                : juce::Colour(outline));
    graphics.drawRoundedRectangle(bounds, 2.5f, 1.0f);
    if (button.getToggleState())
        drawActiveMarker(graphics, bounds);
    }
    graphics.setColour(button.getToggleState() ? juce::Colour(0xfff4f7f9) : juce::Colour(text));
    graphics.setFont(isGlyphButton(button)
                         ? labelFont(juce::jmin(
                               16.0f, static_cast<float>(button.getHeight()) * 0.56f))
                         : labelFont(10.0f, button.getToggleState()));
    graphics.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(5, 1),
                            juce::Justification::centred, 1, 0.75f);
}

void CyDawLookAndFeel::drawComboBox(juce::Graphics& graphics,
                                     const int width,
                                     const int height,
                                     const bool buttonDown,
                                     const int buttonX,
                                     const int buttonY,
                                     const int buttonWidth,
                                     const int buttonHeight,
                                     juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float>(0.5f, 0.5f,
                                                static_cast<float>(width - 1),
                                                static_cast<float>(height - 1));
    graphics.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    graphics.fillRoundedRectangle(bounds, 2.5f);
    graphics.setColour(box.findColour(juce::ComboBox::outlineColourId));
    graphics.drawRoundedRectangle(bounds, 2.5f, 1.0f);

    juce::Path arrow;
    const auto centreX = static_cast<float>(buttonX + buttonWidth / 2);
    const auto centreY = static_cast<float>(buttonY + buttonHeight / 2 + (buttonDown ? 1 : 0));
    arrow.addTriangle(centreX - 4.0f, centreY - 2.0f,
                      centreX + 4.0f, centreY - 2.0f,
                      centreX, centreY + 3.0f);
    graphics.setColour(box.findColour(juce::ComboBox::arrowColourId));
    graphics.fillPath(arrow);
}

void CyDawLookAndFeel::drawLinearSlider(juce::Graphics& graphics,
                                         const int x,
                                         const int y,
                                         const int width,
                                         const int height,
                                         const float sliderPosition,
                                         const float,
                                         const float,
                                         const juce::Slider::SliderStyle style,
                                         juce::Slider& slider)
{
    if (style == juce::Slider::LinearBar || style == juce::Slider::LinearBarVertical) {
        auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                              static_cast<float>(width), static_cast<float>(height));
        graphics.setColour(slider.findColour(juce::Slider::backgroundColourId));
        graphics.fillRoundedRectangle(bounds, 2.0f);
        graphics.setColour(slider.findColour(juce::Slider::trackColourId));
        if (style == juce::Slider::LinearBar) {
            const auto rail = bounds.removeFromBottom(2.5f).reduced(1.5f, 0.0f);
            graphics.fillRect(rail.withRight(
                juce::jlimit(rail.getX(), rail.getRight(), sliderPosition)));
        } else {
            const auto rail = bounds.removeFromLeft(2.5f).reduced(0.0f, 1.5f);
            graphics.fillRect(rail.withTop(
                juce::jlimit(rail.getY(), rail.getBottom(), sliderPosition)));
        }
        graphics.setColour(juce::Colour(outline));
        graphics.drawRoundedRectangle(
            juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                   static_cast<float>(width),
                                   static_cast<float>(height)).reduced(0.5f),
            2.0f, 1.0f);
        return;
    }
    juce::LookAndFeel_V4::drawLinearSlider(graphics, x, y, width, height, sliderPosition,
                                            sliderPosition, sliderPosition, style, slider);
}

void CyDawLookAndFeel::drawRotarySlider(juce::Graphics& graphics,
                                         const int x,
                                         const int y,
                                         const int width,
                                         const int height,
                                         const float sliderPosition,
                                         const float rotaryStartAngle,
                                         const float rotaryEndAngle,
                                         juce::Slider& slider)
{
    const auto radius = juce::jmin(static_cast<float>(width), static_cast<float>(height)) * 0.5f - 4.0f;
    const auto centre = juce::Point<float>(static_cast<float>(x) + static_cast<float>(width) * 0.5f,
                                           static_cast<float>(y) + static_cast<float>(height) * 0.5f);
    const auto angle = rotaryStartAngle + sliderPosition * (rotaryEndAngle - rotaryStartAngle);
    const auto dial = juce::Rectangle<float>(centre.x - radius, centre.y - radius,
                                              radius * 2.0f, radius * 2.0f);
    graphics.setColour(juce::Colour(0xff17191b));
    graphics.fillEllipse(dial);
    graphics.setColour(juce::Colour(raised));
    graphics.drawEllipse(dial, 2.0f);

    juce::Path arc;
    arc.addCentredArc(centre.x, centre.y, radius - 1.0f, radius - 1.0f, 0.0f,
                      rotaryStartAngle, angle, true);
    graphics.setColour(slider.findColour(juce::Slider::trackColourId));
    graphics.strokePath(arc, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

    // The modulation ring: where a control cable sweeps this knob, drawn
    // just outside the dial so the dial's own arc stays readable under it.
    if (const auto* knob = dynamic_cast<const KnobSlider*>(&slider);
        knob != nullptr && knob->modulationRange().has_value()) {
        const auto& range = *knob->modulationRange();
        const auto low = juce::jlimit(0.0, 1.0,
                                      slider.valueToProportionOfLength(
                                          range.getStart()));
        const auto high = juce::jlimit(0.0, 1.0,
                                       slider.valueToProportionOfLength(
                                           range.getEnd()));
        const auto from = rotaryStartAngle
                          + static_cast<float>(low) * (rotaryEndAngle - rotaryStartAngle);
        const auto to = rotaryStartAngle
                        + static_cast<float>(high) * (rotaryEndAngle - rotaryStartAngle);
        juce::Path ring;
        ring.addCentredArc(centre.x, centre.y, radius + 2.6f, radius + 2.6f,
                           0.0f, from, to, true);
        // Amber when the sweep is turned over, and heavier while the ring is
        // the thing being dragged.
        graphics.setColour((knob->modulationInverted() ? juce::Colour(0xffe0a85a)
                                                        : juce::Colour(0xff4fd3ae))
                               .withAlpha(0.9f));
        graphics.strokePath(ring, juce::PathStrokeType(
                                      knob->draggingModulationDepth() ? 3.4f : 2.4f,
                                      juce::PathStrokeType::curved,
                                      juce::PathStrokeType::rounded));
        // The modulation's position right now: a dot on the ring, where the
        // parameter actually is at this instant.
        if (knob->modulationPosition().has_value()) {
            const auto at = juce::jlimit(0.0, 1.0,
                                         slider.valueToProportionOfLength(
                                             *knob->modulationPosition()));
            const auto dotAngle = rotaryStartAngle
                                  + static_cast<float>(at)
                                        * (rotaryEndAngle - rotaryStartAngle);
            const auto orbit = radius + 2.6f;
            const auto dot = juce::Point<float>(
                centre.x + orbit * std::sin(dotAngle),
                centre.y - orbit * std::cos(dotAngle));
            graphics.setColour(juce::Colour(0xffe9fff7));
            graphics.fillEllipse(dot.x - 2.6f, dot.y - 2.6f, 5.2f, 5.2f);
        }
    }

    juce::Path pointer;
    pointer.addRoundedRectangle(-1.1f, -radius + 7.0f, 2.2f, radius * 0.46f, 1.0f);
    pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));
    graphics.setColour(slider.findColour(juce::Slider::thumbColourId));
    graphics.fillPath(pointer);
}

juce::Font CyDawLookAndFeel::labelFont(const float height, const bool emphasised)
{
    // Tracking is what makes small uppercase legible. Bold at 11 px turns the
    // letterforms into a solid block and costs roughly a fifth more width for
    // no gain in readability.
    auto font = juce::Font(juce::FontOptions(height,
                                             emphasised ? juce::Font::bold
                                                        : juce::Font::plain));
    return font.withExtraKerningFactor(emphasised ? 0.045f : 0.06f);
}

juce::Font CyDawLookAndFeel::getTextButtonFont(juce::TextButton& button,
                                               const int buttonHeight)
{
    // A toggled control is the one thing in the chrome that earns extra weight:
    // it is reporting state, not offering an action.
    if (isGlyphButton(button))
        return labelFont(juce::jmin(16.0f,
                                    static_cast<float>(buttonHeight) * 0.56f));
    return labelFont(juce::jmin(11.0f, static_cast<float>(buttonHeight) * 0.42f),
                     button.getToggleState());
}

juce::Font CyDawLookAndFeel::getComboBoxFont(juce::ComboBox& box)
{
    return labelFont(juce::jmin(11.0f, static_cast<float>(box.getHeight()) * 0.42f));
}

void CyDawLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    const auto font = getComboBoxFont(box);
    // The label gets exactly one line's worth of height, centred. Given the full
    // box height JUCE fits two lines into it and a long entry breaks mid-word,
    // which reads far worse than a single elided line.
    const auto line = juce::roundToInt(font.getHeight() * 1.3f);
    // 22 px is what the arrow drawn above actually needs; the stock 30 px stole
    // width from the text for nothing.
    label.setBounds(8, (box.getHeight() - line) / 2, box.getWidth() - 22, line);
    label.setFont(font);
    label.setMinimumHorizontalScale(1.0f);
}

juce::Font CyDawLookAndFeel::getPopupMenuFont()
{
    return labelFont(13.0f);
}

int preferredTextButtonWidth(const juce::Button& button,
                             const int buttonHeight,
                             const int minimumWidth,
                             const int horizontalPadding)
{
    // Toggles draw at a fixed 10 px and text buttons scale up to 11 px; measure
    // against the larger of the two so neither can clip.
    const auto height = juce::jmin(11.0f, static_cast<float>(buttonHeight) * 0.42f);
    const auto font = CyDawLookAndFeel::labelFont(height, button.getToggleState());
    const auto measured = juce::GlyphArrangement::getStringWidthInt(
        font, button.getButtonText());
    return std::max(minimumWidth, measured + horizontalPadding);
}

int preferredComboBoxWidth(const juce::ComboBox& box,
                           const int boxHeight,
                           const int minimumWidth)
{
    const auto height = juce::jmin(11.0f, static_cast<float>(boxHeight) * 0.42f);
    const auto font = CyDawLookAndFeel::labelFont(height, false);
    // The longest entry decides the width: a selector that fits only its current
    // value starts clipping as soon as the user picks a different one.
    auto widest = juce::GlyphArrangement::getStringWidthInt(font, box.getText());
    for (auto index = 0; index < box.getNumItems(); ++index)
        widest = std::max(widest, juce::GlyphArrangement::getStringWidthInt(
                                      font, box.getItemText(index)));
    return std::max(minimumWidth, widest + 34);
}

} // namespace cydaw::app
