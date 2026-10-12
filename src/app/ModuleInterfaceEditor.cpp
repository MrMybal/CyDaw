#include "ModuleInterfaceEditor.h"
#include "PaintProfile.h"

#include "JsonValue.h"
#include "KnobSlider.h"
#include "Localization.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <set>

namespace cydaw::app {

namespace jsonKeys {
// Property names as identifiers made once: an Identifier built from a
// literal is a string-pool lookup, and a restore or a save of a large
// graph made tens of thousands of them.
static const juce::Identifier automatable("automatable");
static const juce::Identifier binding("binding");
static const juce::Identifier controls("controls");
static const juce::Identifier defaultValue("defaultValue");
static const juce::Identifier description("description");
static const juce::Identifier deviceControls("deviceControls");
static const juce::Identifier extraBindings("extraBindings");
static const juce::Identifier gridColumns("gridColumns");
static const juce::Identifier gridRows("gridRows");
static const juce::Identifier windowWidth("windowWidth");
static const juce::Identifier windowHeight("windowHeight");
static const juce::Identifier height("height");
static const juce::Identifier hostParameterId("hostParameterId");
static const juce::Identifier id("id");
static const juce::Identifier key("key");
static const juce::Identifier kind("kind");
static const juce::Identifier label("label");
static const juce::Identifier maximum("maximum");
static const juce::Identifier minimum("minimum");
static const juce::Identifier modulatable("modulatable");
static const juce::Identifier nextControlId("nextControlId");
static const juce::Identifier parameters("parameters");
static const juce::Identifier portChannels("portChannels");
static const juce::Identifier portKind("portKind");
static const juce::Identifier publishToHost("publishToHost");
static const juce::Identifier quaternaryBinding("quaternaryBinding");
static const juce::Identifier schema("schema");
static const juce::Identifier secondaryBinding("secondaryBinding");
static const juce::Identifier showInDevice("showInDevice");
static const juce::Identifier snapToGrid("snapToGrid");
static const juce::Identifier tertiaryBinding("tertiaryBinding");
static const juce::Identifier unit("unit");
static const juce::Identifier value("value");
static const juce::Identifier width("width");
static const juce::Identifier x("x");
static const juce::Identifier y("y");
} // namespace jsonKeys


double moduleParameterDefaultValue(
    const ModulePublishedParameter& parameter) noexcept
{
    return std::isfinite(parameter.defaultValue)
               && parameter.defaultValue >= parameter.minimum
               && parameter.defaultValue <= parameter.maximum
        ? parameter.defaultValue : parameter.value;
}

std::vector<double> moduleSelectorValues(
    const ModulePublishedParameter& parameter)
{
    if (parameter.maximum <= parameter.minimum)
        return { parameter.minimum };
    const auto roundedMinimum = std::round(parameter.minimum);
    const auto roundedMaximum = std::round(parameter.maximum);
    const auto integerRange = std::abs(parameter.minimum - roundedMinimum) < 1.0e-6
                              && std::abs(parameter.maximum - roundedMaximum) < 1.0e-6
                              && roundedMaximum - roundedMinimum <= 31.0;
    const auto count = integerRange
                           ? static_cast<std::size_t>(roundedMaximum
                                                       - roundedMinimum + 1.0)
                           : std::size_t { 5 };
    std::vector<double> values;
    values.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto normalised = count > 1
            ? static_cast<double>(index) / static_cast<double>(count - 1)
            : 0.0;
        values.push_back(parameter.minimum
                         + (parameter.maximum - parameter.minimum)
                               * normalised);
    }
    return values;
}

std::size_t closestModuleSelectorValue(
    const std::vector<double>& values, const double value) noexcept
{
    if (values.empty())
        return 0;
    auto closest = std::size_t {};
    auto distance = std::abs(values.front() - value);
    for (std::size_t index = 1; index < values.size(); ++index) {
        const auto candidate = std::abs(values[index] - value);
        if (candidate < distance) {
            closest = index;
            distance = candidate;
        }
    }
    return closest;
}

juce::String moduleSelectorValueLabel(
    const ModulePublishedParameter& parameter, const double value)
{
    const auto integer = std::abs(value - std::round(value)) < 1.0e-6;
    auto label = juce::String(value, integer ? 0 : 2);
    if (parameter.unit.isNotEmpty())
        label += " " + parameter.unit;
    return label;
}

ModuleXyPadComponent::ModuleXyPadComponent()
{
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
}

void ModuleXyPadComponent::setNormalisedValues(
    const double x, const double y, const juce::NotificationType notification)
{
    const auto nextX = juce::jlimit(0.0, 1.0, x);
    const auto nextY = juce::jlimit(0.0, 1.0, y);
    if (std::abs(nextX - x_) < 1.0e-9
        && std::abs(nextY - y_) < 1.0e-9)
        return;
    x_ = nextX;
    y_ = nextY;
    repaint();
    if (notification != juce::dontSendNotification && onValueChange)
        onValueChange(x_, y_);
}

void ModuleXyPadComponent::paint(juce::Graphics& graphics)
{
    const auto area = getLocalBounds().toFloat().reduced(1.0f);
    graphics.setColour(juce::Colour(0xff111419));
    graphics.fillRoundedRectangle(area, 4.0f);
    graphics.setColour(juce::Colour(0xff353c43));
    for (int division = 1; division < 4; ++division) {
        const auto x = area.getX() + area.getWidth()
                                      * static_cast<float>(division) / 4.0f;
        const auto y = area.getY() + area.getHeight()
                                      * static_cast<float>(division) / 4.0f;
        graphics.drawVerticalLine(juce::roundToInt(x), area.getY(),
                                  area.getBottom());
        graphics.drawHorizontalLine(juce::roundToInt(y), area.getX(),
                                    area.getRight());
    }
    const auto point = juce::Point<float>(
        area.getX() + static_cast<float>(x_) * area.getWidth(),
        area.getBottom() - static_cast<float>(y_) * area.getHeight());
    graphics.setColour(juce::Colour(0xff75d6ac).withAlpha(0.55f));
    graphics.drawVerticalLine(juce::roundToInt(point.x), area.getY(),
                              area.getBottom());
    graphics.drawHorizontalLine(juce::roundToInt(point.y), area.getX(),
                                area.getRight());
    graphics.setColour(juce::Colour(0xff75d6ac));
    graphics.fillEllipse(point.x - 5.0f, point.y - 5.0f, 10.0f, 10.0f);
    graphics.setColour(juce::Colour(0xffdffaf0));
    graphics.drawEllipse(point.x - 5.0f, point.y - 5.0f, 10.0f, 10.0f, 1.0f);
    graphics.setColour(juce::Colour(0xff94a0aa)
                           .withMultipliedAlpha(isEnabled() ? 1.0f : 0.45f));
    graphics.setFont(juce::FontOptions(8.0f, juce::Font::bold));
    graphics.drawText("X", area.toNearestInt().removeFromBottom(12)
                               .removeFromRight(12),
                      juce::Justification::centred, false);
    graphics.drawText("Y", area.toNearestInt().removeFromTop(12)
                               .removeFromLeft(12),
                      juce::Justification::centred, false);
    if (hasKeyboardFocus(true)) {
        graphics.setColour(juce::Colour(0xffff8a00));
        graphics.drawRoundedRectangle(area.reduced(0.5f), 4.0f, 1.5f);
    }
}

void ModuleXyPadComponent::updateFromPosition(const juce::Point<float> position)
{
    const auto area = getLocalBounds().toFloat().reduced(1.0f);
    if (area.isEmpty())
        return;
    setNormalisedValues(
        (position.x - area.getX()) / area.getWidth(),
        1.0 - (position.y - area.getY()) / area.getHeight(),
        juce::sendNotification);
}

void ModuleXyPadComponent::mouseDown(const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    dragging_ = true;
    if (onGestureStart)
        onGestureStart();
    updateFromPosition(event.position);
}

void ModuleXyPadComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (dragging_)
        updateFromPosition(event.position);
}

void ModuleXyPadComponent::mouseUp(const juce::MouseEvent&)
{
    if (!dragging_)
        return;
    dragging_ = false;
    if (onGestureEnd)
        onGestureEnd();
}

void ModuleXyPadComponent::mouseDoubleClick(const juce::MouseEvent&)
{
    if (onReset)
        onReset();
}

bool ModuleXyPadComponent::keyPressed(const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::homeKey) {
        if (onReset)
            onReset();
        return true;
    }
    const auto step = key.getModifiers().isShiftDown() ? 0.05 : 0.01;
    auto x = x_;
    auto y = y_;
    if (key.getKeyCode() == juce::KeyPress::leftKey) x -= step;
    else if (key.getKeyCode() == juce::KeyPress::rightKey) x += step;
    else if (key.getKeyCode() == juce::KeyPress::upKey) y += step;
    else if (key.getKeyCode() == juce::KeyPress::downKey) y -= step;
    else return false;
    if (onGestureStart)
        onGestureStart();
    setNormalisedValues(x, y, juce::sendNotification);
    if (onGestureEnd)
        onGestureEnd();
    return true;
}

void ModuleXyPadComponent::focusGained(juce::Component::FocusChangeType)
{
    repaint();
}

void ModuleXyPadComponent::focusLost(juce::Component::FocusChangeType)
{
    repaint();
}

ModuleEnvelopeComponent::ModuleEnvelopeComponent()
{
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
}

void ModuleEnvelopeComponent::setNormalisedValues(
    const double attack, const double decay, const double sustain,
    const double release, const juce::NotificationType notification)
{
    const std::array next {
        juce::jlimit(0.0, 1.0, attack),
        juce::jlimit(0.0, 1.0, decay),
        juce::jlimit(0.0, 1.0, sustain),
        juce::jlimit(0.0, 1.0, release)
    };
    if (std::equal(next.begin(), next.end(), values_.begin(),
                   [](const auto left, const auto right) {
                       return std::abs(left - right) < 1.0e-9;
                   }))
        return;
    values_ = next;
    repaint();
    if (notification != juce::dontSendNotification && onValueChange)
        onValueChange(values_[0], values_[1], values_[2], values_[3]);
}

std::array<juce::Point<float>, 4>
ModuleEnvelopeComponent::handlePositions() const
{
    const auto area = getLocalBounds().toFloat().reduced(8.0f, 7.0f);
    const auto x = [&area](const double normalised) {
        return area.getX() + static_cast<float>(normalised) * area.getWidth();
    };
    const auto sustainY = area.getBottom()
        - static_cast<float>(values_[2]) * area.getHeight();
    return {{
        { x(0.05 + values_[0] * 0.25), area.getY() },
        { x(0.34 + values_[1] * 0.18), sustainY },
        { x(0.66), sustainY },
        { x(0.71 + values_[3] * 0.27), area.getBottom() }
    }};
}

void ModuleEnvelopeComponent::paint(juce::Graphics& graphics)
{
    const auto area = getLocalBounds().toFloat().reduced(1.0f);
    graphics.setColour(juce::Colour(0xff111419));
    graphics.fillRoundedRectangle(area, 4.0f);
    graphics.setColour(juce::Colour(0xff30373e));
    for (int division = 1; division < 8; ++division) {
        const auto x = area.getX() + area.getWidth()
            * static_cast<float>(division) / 8.0f;
        graphics.drawVerticalLine(juce::roundToInt(x), area.getY(),
                                  area.getBottom());
    }
    for (int division = 1; division < 4; ++division) {
        const auto y = area.getY() + area.getHeight()
            * static_cast<float>(division) / 4.0f;
        graphics.drawHorizontalLine(juce::roundToInt(y), area.getX(),
                                    area.getRight());
    }
    const auto handles = handlePositions();
    juce::Path envelope;
    envelope.startNewSubPath(area.getX() + 4.0f, area.getBottom() - 7.0f);
    for (const auto point : handles)
        envelope.lineTo(point);
    graphics.setColour(juce::Colour(0xff75d6ac).withAlpha(0.16f));
    auto fill = envelope;
    fill.lineTo(handles.back().x, area.getBottom() - 7.0f);
    fill.closeSubPath();
    graphics.fillPath(fill);
    graphics.setColour(juce::Colour(0xff75d6ac));
    graphics.strokePath(envelope, juce::PathStrokeType(2.0f));
    static constexpr std::array labels { "A", "D", "S", "R" };
    graphics.setFont(juce::FontOptions(8.0f, juce::Font::bold));
    for (std::size_t index = 0; index < handles.size(); ++index) {
        graphics.setColour(index == static_cast<std::size_t>(activeHandle_)
                               && hasKeyboardFocus(true)
                           ? juce::Colour(0xffff8a00)
                           : juce::Colour(0xff75d6ac));
        graphics.fillEllipse(handles[index].x - 4.5f,
                             handles[index].y - 4.5f, 9.0f, 9.0f);
        graphics.setColour(juce::Colour(0xffdffaf0));
        graphics.drawText(labels[index],
                          juce::Rectangle<int>(
                              juce::roundToInt(handles[index].x - 8.0f),
                              juce::roundToInt(handles[index].y + 5.0f),
                              16, 11),
                          juce::Justification::centred, false);
    }
    if (hasKeyboardFocus(true)) {
        graphics.setColour(juce::Colour(0xffff8a00));
        graphics.drawRoundedRectangle(area.reduced(0.5f), 4.0f, 1.5f);
    }
}

void ModuleEnvelopeComponent::updateActiveHandle(
    const juce::Point<float> position)
{
    const auto area = getLocalBounds().toFloat().reduced(8.0f, 7.0f);
    if (area.isEmpty())
        return;
    switch (activeHandle_) {
    case 0:
        values_[0] = juce::jlimit(0.0, 1.0,
            ((position.x - area.getX()) / area.getWidth() - 0.05) / 0.25);
        break;
    case 1:
        values_[1] = juce::jlimit(0.0, 1.0,
            ((position.x - area.getX()) / area.getWidth() - 0.34) / 0.18);
        break;
    case 2:
        values_[2] = juce::jlimit(0.0, 1.0,
            1.0 - (position.y - area.getY()) / area.getHeight());
        break;
    case 3:
        values_[3] = juce::jlimit(0.0, 1.0,
            ((position.x - area.getX()) / area.getWidth() - 0.71) / 0.27);
        break;
    default: break;
    }
    repaint();
    if (onValueChange)
        onValueChange(values_[0], values_[1], values_[2], values_[3]);
}

void ModuleEnvelopeComponent::mouseDown(const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    const auto handles = handlePositions();
    activeHandle_ = 0;
    auto closest = handles.front().getDistanceFrom(event.position);
    for (std::size_t index = 1; index < handles.size(); ++index) {
        const auto distance = handles[index].getDistanceFrom(event.position);
        if (distance < closest) {
            closest = distance;
            activeHandle_ = static_cast<int>(index);
        }
    }
    dragging_ = true;
    if (onGestureStart)
        onGestureStart();
    updateActiveHandle(event.position);
}

void ModuleEnvelopeComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (dragging_)
        updateActiveHandle(event.position);
}

void ModuleEnvelopeComponent::mouseUp(const juce::MouseEvent&)
{
    if (!dragging_)
        return;
    dragging_ = false;
    if (onGestureEnd)
        onGestureEnd();
}

void ModuleEnvelopeComponent::mouseDoubleClick(const juce::MouseEvent&)
{
    if (onReset)
        onReset();
}

bool ModuleEnvelopeComponent::keyPressed(const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::homeKey) {
        if (onReset)
            onReset();
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::tabKey) {
        activeHandle_ = (activeHandle_ + 1) % 4;
        repaint();
        return true;
    }
    const auto step = key.getModifiers().isShiftDown() ? 0.05 : 0.01;
    auto delta = 0.0;
    if (key.getKeyCode() == juce::KeyPress::leftKey
        || key.getKeyCode() == juce::KeyPress::downKey)
        delta = -step;
    else if (key.getKeyCode() == juce::KeyPress::rightKey
             || key.getKeyCode() == juce::KeyPress::upKey)
        delta = step;
    else
        return false;
    if (onGestureStart)
        onGestureStart();
    values_[static_cast<std::size_t>(activeHandle_)] = juce::jlimit(
        0.0, 1.0,
        values_[static_cast<std::size_t>(activeHandle_)] + delta);
    repaint();
    if (onValueChange)
        onValueChange(values_[0], values_[1], values_[2], values_[3]);
    if (onGestureEnd)
        onGestureEnd();
    return true;
}

void ModuleEnvelopeComponent::focusGained(juce::Component::FocusChangeType)
{
    repaint();
}

void ModuleEnvelopeComponent::focusLost(juce::Component::FocusChangeType)
{
    repaint();
}

ModuleFilterResponseComponent::ModuleFilterResponseComponent()
{
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
}

void ModuleFilterResponseComponent::setParameterRanges(
    const double minimumCutoff, const double maximumCutoff,
    const double minimumResonance, const double maximumResonance)
{
    minimumCutoff_ = std::max(1.0, minimumCutoff);
    maximumCutoff_ = std::max(minimumCutoff_ + 1.0, maximumCutoff);
    minimumResonance_ = minimumResonance;
    maximumResonance_ = std::max(minimumResonance_ + 0.001,
                                 maximumResonance);
    cutoff_ = juce::jlimit(minimumCutoff_, maximumCutoff_, cutoff_);
    resonance_ = juce::jlimit(minimumResonance_, maximumResonance_,
                              resonance_);
    repaint();
}

void ModuleFilterResponseComponent::setValues(
    const double cutoff, const double resonance,
    const juce::NotificationType notification)
{
    const auto nextCutoff = juce::jlimit(minimumCutoff_, maximumCutoff_,
                                         cutoff);
    const auto nextResonance = juce::jlimit(minimumResonance_,
                                            maximumResonance_, resonance);
    if (std::abs(nextCutoff - cutoff_) < 1.0e-9
        && std::abs(nextResonance - resonance_) < 1.0e-9)
        return;
    cutoff_ = nextCutoff;
    resonance_ = nextResonance;
    repaint();
    if (notification != juce::dontSendNotification && onValueChange)
        onValueChange(cutoff_, resonance_);
}

double ModuleFilterResponseComponent::normalisedCutoff() const noexcept
{
    const auto denominator = std::log(maximumCutoff_ / minimumCutoff_);
    return denominator > 0.0
        ? juce::jlimit(0.0, 1.0,
              std::log(cutoff_ / minimumCutoff_) / denominator)
        : 0.0;
}

double ModuleFilterResponseComponent::normalisedResonance() const noexcept
{
    return maximumResonance_ > minimumResonance_
        ? juce::jlimit(0.0, 1.0,
              (resonance_ - minimumResonance_)
                  / (maximumResonance_ - minimumResonance_))
        : 0.0;
}

void ModuleFilterResponseComponent::paint(juce::Graphics& graphics)
{
    const auto area = getLocalBounds().toFloat().reduced(1.0f);
    const auto plot = area.reduced(7.0f, 8.0f);
    graphics.setColour(juce::Colour(0xff111419));
    graphics.fillRoundedRectangle(area, 4.0f);

    graphics.setColour(juce::Colour(0xff30373e));
    for (int division = 1; division < 8; ++division) {
        const auto x = plot.getX() + plot.getWidth()
            * static_cast<float>(division) / 8.0f;
        graphics.drawVerticalLine(juce::roundToInt(x), plot.getY(),
                                  plot.getBottom());
    }
    for (int division = 1; division < 4; ++division) {
        const auto y = plot.getY() + plot.getHeight()
            * static_cast<float>(division) / 4.0f;
        graphics.drawHorizontalLine(juce::roundToInt(y), plot.getX(),
                                    plot.getRight());
    }

    juce::Path response;
    constexpr auto minimumDb = -48.0;
    constexpr auto maximumDb = 12.0;
    const auto points = std::max(32, juce::roundToInt(plot.getWidth()));
    for (int index = 0; index < points; ++index) {
        const auto normalised = static_cast<double>(index)
            / static_cast<double>(points - 1);
        const auto frequency = minimumCutoff_
            * std::pow(maximumCutoff_ / minimumCutoff_, normalised);
        const auto db = juce::jlimit(
            minimumDb, maximumDb, magnitudeDb(cutoff_, resonance_, frequency));
        const auto x = plot.getX() + static_cast<float>(normalised)
            * plot.getWidth();
        const auto y = plot.getBottom()
            - static_cast<float>((db - minimumDb) / (maximumDb - minimumDb))
                * plot.getHeight();
        if (index == 0) response.startNewSubPath(x, y);
        else response.lineTo(x, y);
    }
    auto fill = response;
    fill.lineTo(plot.getRight(), plot.getBottom());
    fill.lineTo(plot.getX(), plot.getBottom());
    fill.closeSubPath();
    graphics.setColour(juce::Colour(0xff75d6ac).withAlpha(0.16f));
    graphics.fillPath(fill);
    graphics.setColour(juce::Colour(0xff75d6ac));
    graphics.strokePath(response, juce::PathStrokeType(2.0f));

    const auto handle = juce::Point<float>(
        plot.getX() + static_cast<float>(normalisedCutoff()) * plot.getWidth(),
        plot.getBottom() - static_cast<float>(normalisedResonance())
            * plot.getHeight());
    graphics.setColour(juce::Colour(0xffff8a00).withAlpha(0.45f));
    graphics.drawVerticalLine(juce::roundToInt(handle.x), plot.getY(),
                              plot.getBottom());
    graphics.setColour(juce::Colour(0xffff8a00));
    graphics.fillEllipse(handle.x - 5.0f, handle.y - 5.0f, 10.0f, 10.0f);
    graphics.setColour(juce::Colour(0xfffff2df));
    graphics.drawEllipse(handle.x - 5.0f, handle.y - 5.0f, 10.0f, 10.0f,
                         1.0f);

    graphics.setFont(juce::FontOptions(8.5f, juce::Font::bold));
    graphics.setColour(juce::Colour(0xffaab4bd));
    graphics.drawText(juce::String(cutoff_, cutoff_ < 1000.0 ? 0 : 1)
                          + " Hz  Q " + juce::String(resonance_, 2),
                      area.toNearestInt().removeFromBottom(13),
                      juce::Justification::centred, false);
    if (hasKeyboardFocus(true)) {
        graphics.setColour(juce::Colour(0xffff8a00));
        graphics.drawRoundedRectangle(area.reduced(0.5f), 4.0f, 1.5f);
    }
}

double ModuleFilterResponseComponent::magnitudeDb(const double cutoff,
                                                  const double resonance,
                                                  const double frequency)
{
    const auto q = std::max(0.05, resonance);
    const auto ratio = frequency / std::max(1.0, cutoff);
    const auto squared = ratio * ratio;
    const auto denominator = std::sqrt(
        (1.0 - squared) * (1.0 - squared) + squared / (q * q));
    return 20.0 * std::log10(1.0 / std::max(1.0e-9, denominator));
}

void ModuleFilterResponseComponent::updateFromPosition(
    const juce::Point<float> position)
{
    const auto plot = getLocalBounds().toFloat().reduced(8.0f, 9.0f);
    if (plot.isEmpty())
        return;
    const auto x = juce::jlimit(0.0, 1.0,
        static_cast<double>((position.x - plot.getX()) / plot.getWidth()));
    const auto y = juce::jlimit(0.0, 1.0,
        static_cast<double>(1.0f
            - (position.y - plot.getY()) / plot.getHeight()));
    setValues(minimumCutoff_ * std::pow(maximumCutoff_ / minimumCutoff_, x),
              minimumResonance_
                  + (maximumResonance_ - minimumResonance_) * y,
              juce::sendNotification);
}

void ModuleFilterResponseComponent::mouseDown(const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    dragging_ = true;
    if (onGestureStart) onGestureStart();
    updateFromPosition(event.position);
}

void ModuleFilterResponseComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (dragging_) updateFromPosition(event.position);
}

void ModuleFilterResponseComponent::mouseUp(const juce::MouseEvent&)
{
    if (!dragging_) return;
    dragging_ = false;
    if (onGestureEnd) onGestureEnd();
}

void ModuleFilterResponseComponent::mouseDoubleClick(const juce::MouseEvent&)
{
    if (onReset) onReset();
}

bool ModuleFilterResponseComponent::keyPressed(const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::homeKey) {
        if (onReset) onReset();
        return true;
    }
    const auto step = key.getModifiers().isShiftDown() ? 0.05 : 0.01;
    auto x = normalisedCutoff();
    auto y = normalisedResonance();
    if (key.getKeyCode() == juce::KeyPress::leftKey) x -= step;
    else if (key.getKeyCode() == juce::KeyPress::rightKey) x += step;
    else if (key.getKeyCode() == juce::KeyPress::upKey) y += step;
    else if (key.getKeyCode() == juce::KeyPress::downKey) y -= step;
    else return false;
    if (onGestureStart) onGestureStart();
    setValues(minimumCutoff_ * std::pow(maximumCutoff_ / minimumCutoff_,
                                        juce::jlimit(0.0, 1.0, x)),
              minimumResonance_ + (maximumResonance_ - minimumResonance_)
                  * juce::jlimit(0.0, 1.0, y),
              juce::sendNotification);
    if (onGestureEnd) onGestureEnd();
    return true;
}

void ModuleFilterResponseComponent::focusGained(
    juce::Component::FocusChangeType)
{
    repaint();
}

void ModuleFilterResponseComponent::focusLost(
    juce::Component::FocusChangeType)
{
    repaint();
}

ModuleEqCurveComponent::ModuleEqCurveComponent()
{
    setOpaque(true);
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void ModuleEqCurveComponent::setSurroundColour(const juce::Colour colour)
{
    surround_ = colour;
    repaint();
}

void ModuleEqCurveComponent::setBandRanges(const double minimumFrequency,
                                           const double maximumFrequency,
                                           const double minimumGain,
                                           const double maximumGain,
                                           const double minimumQ,
                                           const double maximumQ)
{
    minimumFrequency_ = std::max(1.0, std::min(minimumFrequency,
                                               maximumFrequency - 1.0));
    maximumFrequency_ = std::max(minimumFrequency_ + 1.0, maximumFrequency);
    minimumGain_ = std::min(minimumGain, maximumGain - 0.1);
    maximumGain_ = std::max(minimumGain_ + 0.1, maximumGain);
    minimumQ_ = std::max(0.01, std::min(minimumQ, maximumQ - 0.01));
    maximumQ_ = std::max(minimumQ_ + 0.01, maximumQ);
    repaint();
}

void ModuleEqCurveComponent::setBands(std::vector<Band> bands,
                                      const juce::NotificationType notification)
{
    bands_ = std::move(bands);
    for (auto& band : bands_) {
        band.frequency = juce::jlimit(minimumFrequency_, maximumFrequency_,
                                      band.frequency);
        band.gainDb = juce::jlimit(minimumGain_, maximumGain_, band.gainDb);
        band.q = juce::jlimit(minimumQ_, maximumQ_, band.q);
    }
    if (selected_ && *selected_ >= bands_.size())
        selected_.reset();
    if (notification != juce::dontSendNotification && onBandChange)
        for (std::size_t index = 0; index < bands_.size(); ++index)
            notifyBand(index);
    repaint();
}

double ModuleEqCurveComponent::magnitudeDb(const std::vector<Band>& bands,
                                           const double frequency)
{
    // The analogue peaking response of each band, summed in decibels. This is
    // the shape the ear reads off the curve; the sampled biquad in the engine
    // follows it everywhere the display has room to show a bell.
    auto total = 0.0;
    for (const auto& band : bands) {
        if (band.frequency <= 0.0)
            continue;
        const auto amplitude = std::pow(10.0, band.gainDb / 40.0);
        const auto q = std::max(0.01, band.q);
        const auto ratio = frequency / band.frequency;
        const auto offset = 1.0 - ratio * ratio;
        const auto numerator = offset * offset
                               + std::pow(amplitude * ratio / q, 2.0);
        const auto denominator = offset * offset
                                 + std::pow(ratio / (amplitude * q), 2.0);
        if (denominator > 1.0e-12)
            total += 10.0 * std::log10(std::max(1.0e-12,
                                                numerator / denominator));
    }
    return total;
}

float ModuleEqCurveComponent::frequencyToX(const double frequency,
                                           const juce::Rectangle<float> area,
                                           const double minimumFrequency,
                                           const double maximumFrequency)
{
    const auto span = std::log(maximumFrequency / minimumFrequency);
    const auto position = span > 1.0e-9
        ? std::log(std::max(minimumFrequency, frequency) / minimumFrequency)
              / span
        : 0.0;
    return area.getX()
           + static_cast<float>(juce::jlimit(0.0, 1.0, position))
                 * area.getWidth();
}

float ModuleEqCurveComponent::gainToY(const double gainDb,
                                      const juce::Rectangle<float> area,
                                      const double minimumGain,
                                      const double maximumGain)
{
    const auto span = maximumGain - minimumGain;
    const auto position = span > 1.0e-9 ? (maximumGain - gainDb) / span : 0.5;
    return area.getY()
           + static_cast<float>(juce::jlimit(0.0, 1.0, position))
                 * area.getHeight();
}

juce::Rectangle<float> ModuleEqCurveComponent::plotBounds() const
{
    return getLocalBounds().toFloat().reduced(4.0f, 3.0f);
}

juce::Point<float> ModuleEqCurveComponent::bandPosition(
    const std::size_t index) const
{
    const auto area = plotBounds();
    const auto& band = bands_[index];
    return { frequencyToX(band.frequency, area, minimumFrequency_,
                          maximumFrequency_),
             gainToY(band.gainDb, area, minimumGain_, maximumGain_) };
}

std::optional<std::size_t> ModuleEqCurveComponent::bandAt(
    const juce::Point<float> position) const
{
    std::optional<std::size_t> found;
    auto best = 14.0f;
    for (std::size_t index = 0; index < bands_.size(); ++index) {
        const auto distance = bandPosition(index).getDistanceFrom(position);
        if (distance < best) {
            best = distance;
            found = index;
        }
    }
    return found;
}

void ModuleEqCurveComponent::notifyBand(const std::size_t index)
{
    if (onBandChange && index < bands_.size())
        onBandChange(index, bands_[index].frequency, bands_[index].gainDb,
                     bands_[index].q);
}

void ModuleEqCurveComponent::paint(juce::Graphics& graphics)
{
    const auto area = plotBounds();
    graphics.fillAll(surround_);
    graphics.setColour(juce::Colour(0xff0d1014));
    graphics.fillRoundedRectangle(getLocalBounds().toFloat(), 2.0f);

    graphics.setColour(juce::Colour(0xff232a33));
    for (const auto decade : { 100.0, 1000.0, 10000.0 }) {
        const auto x = frequencyToX(decade, area, minimumFrequency_,
                                    maximumFrequency_);
        graphics.drawVerticalLine(juce::roundToInt(x), area.getY(),
                                  area.getBottom());
    }
    for (auto gain = minimumGain_ + 6.0; gain < maximumGain_; gain += 6.0) {
        const auto y = gainToY(gain, area, minimumGain_, maximumGain_);
        graphics.setColour(std::abs(gain) < 0.001
                               ? juce::Colour(0xff3d4753)
                               : juce::Colour(0xff1d232b));
        graphics.drawHorizontalLine(juce::roundToInt(y), area.getX(),
                                    area.getRight());
    }

    juce::Path curve;
    const auto steps = std::max(24, juce::roundToInt(area.getWidth()));
    for (auto step = 0; step <= steps; ++step) {
        const auto position = static_cast<double>(step) / steps;
        const auto frequency = minimumFrequency_
            * std::pow(maximumFrequency_ / minimumFrequency_, position);
        const auto y = gainToY(magnitudeDb(bands_, frequency), area,
                               minimumGain_, maximumGain_);
        const auto x = area.getX()
                       + static_cast<float>(position) * area.getWidth();
        if (step == 0)
            curve.startNewSubPath(x, y);
        else
            curve.lineTo(x, y);
    }
    // The spectrum first, under everything: a filled shape on the plot's
    // own frequency axis, full height for a full-scale signal, so a band's
    // dot sits on top of the energy it is shaping.
    if (spectrum_.size() >= 2) {
        juce::Path spectrum;
        spectrum.startNewSubPath(area.getX(), area.getBottom());
        for (std::size_t bin = 0; bin < spectrum_.size(); ++bin) {
            const auto x = area.getX()
                           + area.getWidth() * static_cast<float>(bin)
                                 / static_cast<float>(spectrum_.size() - 1);
            const auto y = area.getBottom()
                           - juce::jlimit(0.0f, 1.0f, spectrum_[bin])
                                 * area.getHeight();
            spectrum.lineTo(x, y);
        }
        spectrum.lineTo(area.getRight(), area.getBottom());
        spectrum.closeSubPath();
        graphics.setColour(juce::Colour(0xff5fa8ff).withAlpha(0.16f));
        graphics.fillPath(spectrum);
        graphics.setColour(juce::Colour(0xff5fa8ff).withAlpha(0.42f));
        graphics.strokePath(spectrum, juce::PathStrokeType(1.0f));
    }

    auto filled = curve;
    const auto zeroY = gainToY(0.0, area, minimumGain_, maximumGain_);
    filled.lineTo(area.getRight(), zeroY);
    filled.lineTo(area.getX(), zeroY);
    filled.closeSubPath();
    graphics.setColour(juce::Colour(0x2275d6ac));
    graphics.fillPath(filled);
    graphics.setColour(juce::Colour(0xff75d6ac));
    graphics.strokePath(curve, juce::PathStrokeType(1.6f));

    for (std::size_t index = 0; index < bands_.size(); ++index) {
        const auto centre = bandPosition(index);
        const auto active = selected_ == index;
        const auto radius = active ? 5.5f : 4.0f;
        graphics.setColour(juce::Colour(0xff0d1014));
        graphics.fillEllipse(centre.x - radius - 1.0f,
                             centre.y - radius - 1.0f,
                             (radius + 1.0f) * 2.0f, (radius + 1.0f) * 2.0f);
        graphics.setColour(active ? juce::Colour(0xffffa229)
                                  : juce::Colour(0xffe6ebef));
        graphics.fillEllipse(centre.x - radius, centre.y - radius,
                             radius * 2.0f, radius * 2.0f);
    }

    if (selected_ && *selected_ < bands_.size()) {
        const auto& band = bands_[*selected_];
        graphics.setColour(juce::Colour(0xffb9c1ca));
        graphics.setFont(juce::FontOptions(8.0f));
        graphics.drawText(juce::String(band.frequency, 0) + " Hz   "
                              + juce::String(band.gainDb, 1) + " dB   Q "
                              + juce::String(band.q, 2),
                          getLocalBounds().reduced(5, 3),
                          juce::Justification::bottomLeft, false);
    }
    if (hasKeyboardFocus(false)) {
        graphics.setColour(juce::Colour(0xffff8a00).withAlpha(0.75f));
        graphics.drawRoundedRectangle(
            getLocalBounds().toFloat().reduced(0.5f), 2.0f, 1.0f);
    }
}

void ModuleEqCurveComponent::setSpectrum(const float* const bins,
                                         const std::size_t count)
{
    if (bins == nullptr || count == 0) {
        if (!spectrum_.empty()) {
            spectrum_.clear();
            repaint(plotBounds().toNearestInt());
        }
        return;
    }
    spectrum_.assign(bins, bins + count);
    repaint(plotBounds().toNearestInt());
}

double ModuleEqCurveComponent::spectrumPeakHzForTest() const
{
    if (spectrum_.size() < 2)
        return 0.0;
    const auto peak = static_cast<std::size_t>(std::distance(
        spectrum_.begin(),
        std::max_element(spectrum_.begin(), spectrum_.end())));
    const auto position = static_cast<double>(peak)
                          / static_cast<double>(spectrum_.size() - 1);
    return minimumFrequency_
           * std::pow(maximumFrequency_ / minimumFrequency_, position);
}

void ModuleEqCurveComponent::mouseDown(const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    selected_ = bandAt(event.position);
    if (!selected_)
        return;
    dragging_ = true;
    if (onGestureStart)
        onGestureStart();
    repaint();
}

void ModuleEqCurveComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (!dragging_ || !selected_ || *selected_ >= bands_.size())
        return;
    const auto area = plotBounds();
    const auto x = juce::jlimit(area.getX(), area.getRight(), event.position.x);
    const auto y = juce::jlimit(area.getY(), area.getBottom(), event.position.y);
    const auto position = area.getWidth() > 1.0f
        ? static_cast<double>(x - area.getX()) / area.getWidth() : 0.0;
    auto& band = bands_[*selected_];
    band.frequency = minimumFrequency_
        * std::pow(maximumFrequency_ / minimumFrequency_, position);
    const auto gainPosition = area.getHeight() > 1.0f
        ? static_cast<double>(y - area.getY()) / area.getHeight() : 0.5;
    band.gainDb = maximumGain_ - gainPosition * (maximumGain_ - minimumGain_);
    notifyBand(*selected_);
    repaint();
}

void ModuleEqCurveComponent::mouseUp(const juce::MouseEvent&)
{
    if (!dragging_)
        return;
    dragging_ = false;
    if (onGestureEnd)
        onGestureEnd();
}

void ModuleEqCurveComponent::mouseDoubleClick(const juce::MouseEvent& event)
{
    const auto index = bandAt(event.position);
    if (!index || *index >= bands_.size())
        return;
    bands_[*index].gainDb = 0.0;
    notifyBand(*index);
    repaint();
}

void ModuleEqCurveComponent::mouseWheelMove(
    const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const auto index = selected_ ? selected_ : bandAt(event.position);
    if (!index || *index >= bands_.size() || wheel.deltaY == 0.0f)
        return;
    auto& band = bands_[*index];
    band.q = juce::jlimit(minimumQ_, maximumQ_,
                          band.q * (wheel.deltaY > 0.0f ? 1.12 : 1.0 / 1.12));
    notifyBand(*index);
    repaint();
}

bool ModuleEqCurveComponent::keyPressed(const juce::KeyPress& key)
{
    if (bands_.empty())
        return false;
    if (key.getKeyCode() == juce::KeyPress::tabKey) {
        selected_ = selected_ ? (*selected_ + 1) % bands_.size()
                              : std::optional<std::size_t>(0);
        repaint();
        return true;
    }
    if (!selected_ || *selected_ >= bands_.size())
        return false;
    auto& band = bands_[*selected_];
    const auto fine = key.getModifiers().isShiftDown();
    if (key.getKeyCode() == juce::KeyPress::leftKey)
        band.frequency = juce::jlimit(minimumFrequency_, maximumFrequency_,
                                      band.frequency / (fine ? 1.01 : 1.06));
    else if (key.getKeyCode() == juce::KeyPress::rightKey)
        band.frequency = juce::jlimit(minimumFrequency_, maximumFrequency_,
                                      band.frequency * (fine ? 1.01 : 1.06));
    else if (key.getKeyCode() == juce::KeyPress::upKey)
        band.gainDb = juce::jlimit(minimumGain_, maximumGain_,
                                   band.gainDb + (fine ? 0.2 : 1.0));
    else if (key.getKeyCode() == juce::KeyPress::downKey)
        band.gainDb = juce::jlimit(minimumGain_, maximumGain_,
                                   band.gainDb - (fine ? 0.2 : 1.0));
    else
        return false;
    notifyBand(*selected_);
    repaint();
    return true;
}

void ModuleEqCurveComponent::focusGained(juce::Component::FocusChangeType)
{
    repaint();
}

void ModuleEqCurveComponent::focusLost(juce::Component::FocusChangeType)
{
    repaint();
}

ModuleDynamicsResponseComponent::ModuleDynamicsResponseComponent()
{
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
}

void ModuleDynamicsResponseComponent::setParameterRanges(
    const double minimumThreshold, const double maximumThreshold,
    const double minimumRatio, const double maximumRatio)
{
    minimumThreshold_ = minimumThreshold;
    maximumThreshold_ = std::max(minimumThreshold_ + 0.001,
                                 maximumThreshold);
    minimumRatio_ = std::max(0.01, minimumRatio);
    maximumRatio_ = std::max(minimumRatio_ + 0.001, maximumRatio);
    threshold_ = juce::jlimit(minimumThreshold_, maximumThreshold_,
                              threshold_);
    ratio_ = juce::jlimit(minimumRatio_, maximumRatio_, ratio_);
    repaint();
}

void ModuleDynamicsResponseComponent::setValues(
    const double threshold, const double ratio,
    const juce::NotificationType notification)
{
    const auto nextThreshold = juce::jlimit(
        minimumThreshold_, maximumThreshold_, threshold);
    const auto nextRatio = juce::jlimit(minimumRatio_, maximumRatio_, ratio);
    if (std::abs(nextThreshold - threshold_) < 1.0e-9
        && std::abs(nextRatio - ratio_) < 1.0e-9)
        return;
    threshold_ = nextThreshold;
    ratio_ = nextRatio;
    repaint();
    if (notification != juce::dontSendNotification && onValueChange)
        onValueChange(threshold_, ratio_);
}

juce::Rectangle<float> ModuleDynamicsResponseComponent::plotBounds() const
{
    return getLocalBounds().toFloat().reduced(8.0f, 9.0f);
}

double ModuleDynamicsResponseComponent::outputDbForInputDb(
    const double threshold, const double ratio, const double inputDb) noexcept
{
    return inputDb <= threshold
        ? inputDb
        : threshold + (inputDb - threshold) / std::max(0.01, ratio);
}

double ModuleDynamicsResponseComponent::outputForInput(
    const double inputDb) const noexcept
{
    return outputDbForInputDb(threshold_, ratio_, inputDb);
}

juce::Point<float> ModuleDynamicsResponseComponent::thresholdHandle() const
{
    const auto plot = plotBounds();
    const auto normalised = static_cast<float>(
        (threshold_ - minimumThreshold_)
        / (maximumThreshold_ - minimumThreshold_));
    return { plot.getX() + normalised * plot.getWidth(),
             plot.getBottom() - normalised * plot.getHeight() };
}

juce::Point<float> ModuleDynamicsResponseComponent::ratioHandle() const
{
    const auto plot = plotBounds();
    const auto inputDb = std::min(maximumThreshold_, threshold_ + 18.0);
    const auto outputDb = outputForInput(inputDb);
    const auto x = static_cast<float>((inputDb - minimumThreshold_)
                                      / (maximumThreshold_ - minimumThreshold_));
    const auto y = static_cast<float>((outputDb - minimumThreshold_)
                                      / (maximumThreshold_ - minimumThreshold_));
    return { plot.getX() + x * plot.getWidth(),
             plot.getBottom() - y * plot.getHeight() };
}

void ModuleDynamicsResponseComponent::paint(juce::Graphics& graphics)
{
    const auto area = getLocalBounds().toFloat().reduced(1.0f);
    const auto plot = plotBounds();
    graphics.setColour(juce::Colour(0xff111419));
    graphics.fillRoundedRectangle(area, 4.0f);

    graphics.setColour(juce::Colour(0xff30373e));
    for (int division = 1; division < 6; ++division) {
        const auto normalised = static_cast<float>(division) / 6.0f;
        graphics.drawVerticalLine(
            juce::roundToInt(plot.getX() + normalised * plot.getWidth()),
            plot.getY(), plot.getBottom());
        graphics.drawHorizontalLine(
            juce::roundToInt(plot.getBottom()
                             - normalised * plot.getHeight()),
            plot.getX(), plot.getRight());
    }

    juce::Path unity;
    unity.startNewSubPath(plot.getBottomLeft());
    unity.lineTo(plot.getTopRight());
    graphics.setColour(juce::Colour(0xff56616a));
    graphics.strokePath(unity, juce::PathStrokeType(1.0f));

    juce::Path response;
    constexpr auto points = 96;
    for (int index = 0; index < points; ++index) {
        const auto normalised = static_cast<double>(index)
                                / static_cast<double>(points - 1);
        const auto inputDb = minimumThreshold_
            + normalised * (maximumThreshold_ - minimumThreshold_);
        const auto outputDb = outputForInput(inputDb);
        const auto x = plot.getX() + static_cast<float>(normalised)
                                      * plot.getWidth();
        const auto yNormalised = static_cast<float>(
            (outputDb - minimumThreshold_)
            / (maximumThreshold_ - minimumThreshold_));
        const auto y = plot.getBottom() - yNormalised * plot.getHeight();
        if (index == 0) response.startNewSubPath(x, y);
        else response.lineTo(x, y);
    }
    auto fill = response;
    fill.lineTo(plot.getRight(), plot.getBottom());
    fill.lineTo(plot.getX(), plot.getBottom());
    fill.closeSubPath();
    graphics.setColour(juce::Colour(0xffffa33a).withAlpha(0.13f));
    graphics.fillPath(fill);
    graphics.setColour(juce::Colour(0xffffa33a));
    graphics.strokePath(response, juce::PathStrokeType(2.0f));

    const std::array handles { thresholdHandle(), ratioHandle() };
    for (std::size_t index = 0; index < handles.size(); ++index) {
        const auto handle = handles[index];
        graphics.setColour(index == static_cast<std::size_t>(activeHandle_)
                               ? juce::Colour(0xffff8a00)
                               : juce::Colour(0xff75d6ac));
        graphics.fillEllipse(handle.x - 5.0f, handle.y - 5.0f,
                             10.0f, 10.0f);
        graphics.setColour(juce::Colour(0xfffff2df));
        graphics.drawEllipse(handle.x - 5.0f, handle.y - 5.0f,
                             10.0f, 10.0f, 1.0f);
    }

    graphics.setFont(juce::FontOptions(8.5f, juce::Font::bold));
    graphics.setColour(juce::Colour(0xffaab4bd));
    graphics.drawText(juce::String(threshold_, 1) + " dB   "
                          + juce::String(ratio_, 2) + ":1",
                      area.toNearestInt().removeFromBottom(13),
                      juce::Justification::centred, false);
    if (hasKeyboardFocus(true)) {
        graphics.setColour(juce::Colour(0xffff8a00));
        graphics.drawRoundedRectangle(area.reduced(0.5f), 4.0f, 1.5f);
    }
}

void ModuleDynamicsResponseComponent::updateFromPosition(
    const juce::Point<float> position)
{
    const auto plot = plotBounds();
    if (plot.isEmpty())
        return;
    if (activeHandle_ == 0) {
        const auto normalised = juce::jlimit(
            0.0, 1.0,
            static_cast<double>((position.x - plot.getX())
                                / plot.getWidth()));
        setValues(minimumThreshold_
                      + normalised
                            * (maximumThreshold_ - minimumThreshold_),
                  ratio_, juce::sendNotification);
        return;
    }

    const auto inputDb = std::min(maximumThreshold_, threshold_ + 18.0);
    const auto inputAboveThreshold = inputDb - threshold_;
    if (inputAboveThreshold <= 1.0e-6)
        return;
    const auto yNormalised = juce::jlimit(
        0.0, 1.0,
        static_cast<double>(1.0f
            - (position.y - plot.getY()) / plot.getHeight()));
    const auto outputDb = minimumThreshold_
        + yNormalised * (maximumThreshold_ - minimumThreshold_);
    const auto outputAboveThreshold = std::max(
        inputAboveThreshold / maximumRatio_, outputDb - threshold_);
    setValues(threshold_, inputAboveThreshold / outputAboveThreshold,
              juce::sendNotification);
}

void ModuleDynamicsResponseComponent::mouseDown(const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    activeHandle_ = event.position.getDistanceFrom(thresholdHandle())
                            <= event.position.getDistanceFrom(ratioHandle())
        ? 0 : 1;
    dragging_ = true;
    if (onGestureStart) onGestureStart();
    updateFromPosition(event.position);
    repaint();
}

void ModuleDynamicsResponseComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (dragging_) updateFromPosition(event.position);
}

void ModuleDynamicsResponseComponent::mouseUp(const juce::MouseEvent&)
{
    if (!dragging_) return;
    dragging_ = false;
    if (onGestureEnd) onGestureEnd();
}

void ModuleDynamicsResponseComponent::mouseDoubleClick(
    const juce::MouseEvent&)
{
    if (onReset) onReset();
}

bool ModuleDynamicsResponseComponent::keyPressed(const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::homeKey) {
        if (onReset) onReset();
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::tabKey) {
        activeHandle_ = activeHandle_ == 0 ? 1 : 0;
        repaint();
        return true;
    }
    const auto fast = key.getModifiers().isShiftDown();
    auto threshold = threshold_;
    auto ratio = ratio_;
    const auto direction = key.getKeyCode() == juce::KeyPress::rightKey
                               || key.getKeyCode() == juce::KeyPress::upKey
        ? 1.0
        : key.getKeyCode() == juce::KeyPress::leftKey
                  || key.getKeyCode() == juce::KeyPress::downKey
            ? -1.0 : 0.0;
    if (direction == 0.0)
        return false;
    if (activeHandle_ == 0)
        threshold += direction * (fast ? 3.0 : 0.5);
    else
        ratio += direction * (fast ? 1.0 : 0.1);
    if (onGestureStart) onGestureStart();
    setValues(threshold, ratio, juce::sendNotification);
    if (onGestureEnd) onGestureEnd();
    return true;
}

void ModuleDynamicsResponseComponent::focusGained(
    juce::Component::FocusChangeType)
{
    repaint();
}

void ModuleDynamicsResponseComponent::focusLost(
    juce::Component::FocusChangeType)
{
    repaint();
}
namespace {

juce::String controlKindKey(const ModuleControlKind kind)
{
    switch (kind) {
        case ModuleControlKind::knob: return "knob";
        case ModuleControlKind::fader: return "fader";
        case ModuleControlKind::waveform: return "waveform";
        case ModuleControlKind::spectrum: return "spectrum";
        case ModuleControlKind::levelMeter: return "level-meter";
        case ModuleControlKind::text: return "text";
        case ModuleControlKind::inputPort: return "input-port";
        case ModuleControlKind::outputPort: return "output-port";
        case ModuleControlKind::toggle: return "toggle";
        case ModuleControlKind::envelope: return "envelope";
        case ModuleControlKind::group: return "group";
        case ModuleControlKind::selector: return "selector";
        case ModuleControlKind::xyPad: return "xy-pad";
        case ModuleControlKind::valueDisplay: return "value-display";
        case ModuleControlKind::filterResponse: return "filter-response";
        case ModuleControlKind::dynamicsResponse: return "dynamics-response";
        case ModuleControlKind::eqCurve: return "eq-curve";
        case ModuleControlKind::wavetable: return "wavetable";
    }
    return "knob";
}

ModuleControlKind controlKindFromKey(const juce::String& key)
{
    if (key == "fader") return ModuleControlKind::fader;
    if (key == "waveform") return ModuleControlKind::waveform;
    if (key == "spectrum") return ModuleControlKind::spectrum;
    if (key == "level-meter") return ModuleControlKind::levelMeter;
    if (key == "text") return ModuleControlKind::text;
    if (key == "input-port") return ModuleControlKind::inputPort;
    if (key == "output-port") return ModuleControlKind::outputPort;
    if (key == "toggle") return ModuleControlKind::toggle;
    if (key == "envelope") return ModuleControlKind::envelope;
    if (key == "group") return ModuleControlKind::group;
    if (key == "selector") return ModuleControlKind::selector;
    if (key == "xy-pad") return ModuleControlKind::xyPad;
    if (key == "value-display") return ModuleControlKind::valueDisplay;
    if (key == "filter-response") return ModuleControlKind::filterResponse;
    if (key == "dynamics-response") return ModuleControlKind::dynamicsResponse;
    if (key == "eq-curve") return ModuleControlKind::eqCurve;
    if (key == "wavetable") return ModuleControlKind::wavetable;
    return ModuleControlKind::knob;
}

bool isParameterControl(const ModuleControlKind kind)
{
    return kind == ModuleControlKind::knob || kind == ModuleControlKind::fader
           || kind == ModuleControlKind::toggle
           || kind == ModuleControlKind::selector
           || kind == ModuleControlKind::xyPad
           || kind == ModuleControlKind::envelope
           || kind == ModuleControlKind::valueDisplay
           || kind == ModuleControlKind::filterResponse
           || kind == ModuleControlKind::dynamicsResponse
           || kind == ModuleControlKind::eqCurve
           || kind == ModuleControlKind::wavetable;
}

bool isAutomaticDeviceParameterControl(const ModuleControlKind kind)
{
    return isParameterControl(kind) && kind != ModuleControlKind::xyPad
           && kind != ModuleControlKind::envelope
           && kind != ModuleControlKind::valueDisplay
           && kind != ModuleControlKind::filterResponse
           && kind != ModuleControlKind::dynamicsResponse
           && kind != ModuleControlKind::eqCurve
           && kind != ModuleControlKind::wavetable;
}

bool isPortControl(const ModuleControlKind kind)
{
    return kind == ModuleControlKind::inputPort
           || kind == ModuleControlKind::outputPort;
}

} // namespace

std::vector<ModuleInterfaceControl>& moduleInterfaceControls(
    ModuleInterfaceState& state, const ModuleInterfaceLayoutMode mode) noexcept
{
    return mode == ModuleInterfaceLayoutMode::device
               ? state.deviceControls : state.controls;
}

const std::vector<ModuleInterfaceControl>& moduleInterfaceControls(
    const ModuleInterfaceState& state,
    const ModuleInterfaceLayoutMode mode) noexcept
{
    return mode == ModuleInterfaceLayoutMode::device
               ? state.deviceControls : state.controls;
}

void rebuildAutomaticDeviceControls(ModuleInterfaceState& state)
{
    auto previous = std::move(state.deviceControls);
    state.deviceControls.clear();
    const auto exposedCount = static_cast<std::size_t>(std::count_if(
        state.parameters.begin(), state.parameters.end(),
        [](const auto& parameter) { return parameter.showInDevice; }));
    if (exposedCount == 0)
        return;

    const auto columns = std::min<std::size_t>(6, exposedCount);
    const auto rows = (exposedCount + columns - 1) / columns;
    const auto cellWidth = 1.0f / static_cast<float>(columns);
    const auto cellHeight = 1.0f / static_cast<float>(rows);
    std::size_t visibleIndex {};
    for (const auto& parameter : state.parameters) {
        if (!parameter.showInDevice)
            continue;
        const auto existing = std::find_if(
            previous.begin(), previous.end(), [&parameter](const auto& control) {
                return isAutomaticDeviceParameterControl(control.kind)
                       && control.parameterBinding == parameter.key;
            });
        ModuleInterfaceControl control;
        control.id = existing != previous.end()
                         ? existing->id : state.nextControlId++;
        const auto authored = std::find_if(
            state.controls.begin(), state.controls.end(),
            [&parameter](const auto& control) {
                return isAutomaticDeviceParameterControl(control.kind)
                       && control.parameterBinding == parameter.key;
            });
        control.kind = existing != previous.end()
                           && isAutomaticDeviceParameterControl(existing->kind)
                           ? existing->kind
                           : authored != state.controls.end()
                                 && (authored->kind == ModuleControlKind::toggle
                                     || authored->kind
                                            == ModuleControlKind::selector)
                               ? authored->kind
                               : ModuleControlKind::knob;
        control.label = parameter.label;
        control.parameterBinding = parameter.key;
        const auto column = visibleIndex % columns;
        const auto row = visibleIndex / columns;
        control.x = static_cast<float>(column) * cellWidth + 0.025f;
        control.y = static_cast<float>(row) * cellHeight + 0.04f;
        control.width = std::max(0.08f, cellWidth - 0.05f);
        control.height = std::max(0.18f, cellHeight - 0.08f);
        state.deviceControls.push_back(std::move(control));
        ++visibleIndex;
    }
}

juce::var serialiseModuleInterface(const ModuleInterfaceState& state)
{
    auto root = juce::DynamicObject::Ptr(new juce::DynamicObject());
    root->setProperty(jsonKeys::schema, std::max(16, static_cast<int>(state.schema)));
    root->setProperty(jsonKeys::nextControlId, static_cast<juce::int64>(state.nextControlId));
    root->setProperty(jsonKeys::snapToGrid, state.snapToGrid);
    root->setProperty(jsonKeys::gridColumns, static_cast<int>(state.gridColumns));
    root->setProperty(jsonKeys::gridRows, static_cast<int>(state.gridRows));
    if (state.windowWidth != 0 && state.windowHeight != 0) {
        root->setProperty(jsonKeys::windowWidth, static_cast<int>(state.windowWidth));
        root->setProperty(jsonKeys::windowHeight, static_cast<int>(state.windowHeight));
    }

    juce::Array<juce::var> parameters;
    for (const auto& parameter : state.parameters) {
        // Every field the object below is built from, and nothing else: a
        // field left out here would let a stale object through, and the
        // with-and-without-cache comparison in the suite is what catches it.
        auto signature = std::uint64_t { 1469598103934665603ull };
        const auto mix = [&signature](const std::uint64_t bits) {
            signature ^= bits;
            signature *= 1099511628211ull;
        };
        const auto mixString = [&mix](const juce::String& text) {
            mix(static_cast<std::uint64_t>(text.hashCode64()));
            mix(static_cast<std::uint64_t>(text.length()));
        };
        const auto mixDouble = [&mix](const double value) {
            mix(std::bit_cast<std::uint64_t>(value));
        };
        mixString(parameter.key);
        mixString(parameter.label);
        mixString(parameter.unit);
        mixDouble(parameter.minimum);
        mixDouble(parameter.maximum);
        mixDouble(parameter.value);
        mixDouble(parameter.defaultValue);
        mix(parameter.showInDevice);
        mix(parameter.hostParameterId);
        mix(parameter.publishToHost);
        mix(parameter.automatable);
        mix(parameter.modulatable);
        mixString(parameter.description);
        if (!parameter.serialised.isVoid()
            && parameter.serialisedSignature == signature) {
            parameters.add(parameter.serialised);
            continue;
        }
        auto value = juce::DynamicObject::Ptr(new juce::DynamicObject());
        value->setProperty(jsonKeys::key, parameter.key);
        value->setProperty(jsonKeys::label, parameter.label);
        value->setProperty(jsonKeys::unit, parameter.unit);
        value->setProperty(jsonKeys::minimum, parameter.minimum);
        value->setProperty(jsonKeys::maximum, parameter.maximum);
        value->setProperty(jsonKeys::value, parameter.value);
        value->setProperty(jsonKeys::defaultValue,
                           moduleParameterDefaultValue(parameter));
        value->setProperty(jsonKeys::showInDevice, parameter.showInDevice);
        value->setProperty(
            "hostParameterId",
            static_cast<juce::int64>(
                parameter.hostParameterId != 0
                    ? parameter.hostParameterId
                    : stableModuleParameterId(parameter.key)));
        value->setProperty(jsonKeys::publishToHost, parameter.publishToHost);
        value->setProperty(jsonKeys::automatable, parameter.automatable);
        value->setProperty(jsonKeys::modulatable, parameter.modulatable);
        value->setProperty(jsonKeys::description, parameter.description);
        parameter.serialised = juce::var(value.get());
        parameter.serialisedSignature = signature;
        parameters.add(parameter.serialised);
    }
    root->setProperty(jsonKeys::parameters, parameters);

    const auto serialiseControls = [](const auto& source) {
        juce::Array<juce::var> controls;
        for (const auto& control : source) {
            auto signature = std::uint64_t { 1469598103934665603ull };
            const auto mix = [&signature](const std::uint64_t bits) {
                signature ^= bits;
                signature *= 1099511628211ull;
            };
            const auto mixString = [&mix](const juce::String& text) {
                mix(static_cast<std::uint64_t>(text.hashCode64()));
                mix(static_cast<std::uint64_t>(text.length()));
            };
            const auto mixFloat = [&mix](const float value) {
                mix(std::bit_cast<std::uint32_t>(value));
            };
            mix(control.id);
            mix(static_cast<std::uint64_t>(control.kind));
            mixString(control.label);
            mixString(control.parameterBinding);
            mixString(control.secondaryParameterBinding);
            mixString(control.tertiaryParameterBinding);
            mixString(control.quaternaryParameterBinding);
            mix(control.extraParameterBindings.size());
            for (const auto& binding : control.extraParameterBindings)
                mixString(binding);
            mixFloat(control.x);
            mixFloat(control.y);
            mixFloat(control.width);
            mixFloat(control.height);
            mix(control.portKind);
            mix(control.portChannels);
            if (!control.serialised.isVoid()
                && control.serialisedSignature == signature) {
                controls.add(control.serialised);
                continue;
            }
            auto value = juce::DynamicObject::Ptr(new juce::DynamicObject());
            value->setProperty(jsonKeys::id, static_cast<juce::int64>(control.id));
            value->setProperty(jsonKeys::kind, controlKindKey(control.kind));
            value->setProperty(jsonKeys::label, control.label);
            value->setProperty(jsonKeys::binding, control.parameterBinding);
            value->setProperty(jsonKeys::secondaryBinding,
                               control.secondaryParameterBinding);
            value->setProperty(jsonKeys::tertiaryBinding,
                               control.tertiaryParameterBinding);
            value->setProperty(jsonKeys::quaternaryBinding,
                               control.quaternaryParameterBinding);
            if (!control.extraParameterBindings.empty()) {
                juce::Array<juce::var> extras;
                for (const auto& binding : control.extraParameterBindings)
                    extras.add(binding);
                value->setProperty(jsonKeys::extraBindings, extras);
            }
            value->setProperty(jsonKeys::x, control.x);
            value->setProperty(jsonKeys::y, control.y);
            value->setProperty(jsonKeys::width, control.width);
            value->setProperty(jsonKeys::height, control.height);
            value->setProperty(jsonKeys::portKind, static_cast<int>(control.portKind));
            value->setProperty(jsonKeys::portChannels,
                               static_cast<int>(control.portChannels));
            control.serialised = juce::var(value.get());
            control.serialisedSignature = signature;
            controls.add(control.serialised);
        }
        return controls;
    };
    // Keep the historical key for schema-1 readers. In schema 2 it is the
    // unconstrained graph layout.
    root->setProperty(jsonKeys::controls, serialiseControls(state.controls));
    root->setProperty(jsonKeys::deviceControls, serialiseControls(state.deviceControls));
    return juce::var(root.get());
}

void dropSerialisedPresentation(ModuleInterfaceState& state)
{
    for (auto& parameter : state.parameters)
        parameter.serialised = juce::var();
    for (auto* controls : { &state.controls, &state.deviceControls })
        for (auto& control : *controls)
            control.serialised = juce::var();
}

ModuleInterfaceState restoreModuleInterface(const juce::var& value)
{
    ModuleInterfaceState state;
    const auto* root = value.getDynamicObject();
    if (root == nullptr)
        return state;

    const auto sourceSchema = static_cast<std::uint32_t>(std::max(
        1, static_cast<int>(root->getProperty(jsonKeys::schema))));
    state.schema = 16;
    state.snapToGrid = sourceSchema < 6
                           || !root->hasProperty(jsonKeys::snapToGrid)
                           || static_cast<bool>(root->getProperty(jsonKeys::snapToGrid));
    state.gridColumns = static_cast<std::uint8_t>(juce::jlimit(
        4, 64, root->hasProperty(jsonKeys::gridColumns)
                   ? static_cast<int>(root->getProperty(jsonKeys::gridColumns)) : 24));
    state.gridRows = static_cast<std::uint8_t>(juce::jlimit(
        4, 64, root->hasProperty(jsonKeys::gridRows)
                   ? static_cast<int>(root->getProperty(jsonKeys::gridRows)) : 16));
    if (root->hasProperty(jsonKeys::windowWidth) && root->hasProperty(jsonKeys::windowHeight)) {
        state.windowWidth = static_cast<std::uint16_t>(juce::jlimit(
            240, 4'096, static_cast<int>(root->getProperty(jsonKeys::windowWidth))));
        state.windowHeight = static_cast<std::uint16_t>(juce::jlimit(
            160, 2'160, static_cast<int>(root->getProperty(jsonKeys::windowHeight))));
    }
    std::set<juce::String> parameterKeys;
    if (const auto* parameters = root->getProperty(jsonKeys::parameters).getArray()) {
        for (const auto& parameterValue : *parameters) {
            const auto* object = parameterValue.getDynamicObject();
            if (object == nullptr)
                continue;
            ModulePublishedParameter parameter;
            parameter.key = object->getProperty(jsonKeys::key).toString();
            parameter.label = object->getProperty(jsonKeys::label).toString();
            parameter.unit = object->getProperty(jsonKeys::unit).toString();
            parameter.minimum = static_cast<double>(object->getProperty(jsonKeys::minimum));
            parameter.maximum = static_cast<double>(object->getProperty(jsonKeys::maximum));
            parameter.value = static_cast<double>(object->getProperty(jsonKeys::value));
            parameter.defaultValue = object->hasProperty(jsonKeys::defaultValue)
                ? static_cast<double>(object->getProperty(jsonKeys::defaultValue))
                : parameter.value;
            parameter.showInDevice = !object->hasProperty(jsonKeys::showInDevice)
                                     || static_cast<bool>(
                                         object->getProperty(jsonKeys::showInDevice));
            parameter.hostParameterId = object->hasProperty(jsonKeys::hostParameterId)
                ? static_cast<std::uint32_t>(static_cast<juce::int64>(
                      object->getProperty(jsonKeys::hostParameterId)))
                : stableModuleParameterId(parameter.key);
            parameter.publishToHost = !object->hasProperty(jsonKeys::publishToHost)
                || static_cast<bool>(object->getProperty(jsonKeys::publishToHost));
            parameter.automatable = !object->hasProperty(jsonKeys::automatable)
                || static_cast<bool>(object->getProperty(jsonKeys::automatable));
            parameter.modulatable = !object->hasProperty(jsonKeys::modulatable)
                || static_cast<bool>(object->getProperty(jsonKeys::modulatable));
            parameter.description = object->getProperty(jsonKeys::description).toString();
            if (parameter.key.isEmpty() || parameterKeys.contains(parameter.key)
                || parameter.minimum > parameter.maximum)
                continue;
            parameter.value = juce::jlimit(parameter.minimum, parameter.maximum,
                                            parameter.value);
            parameter.defaultValue = juce::jlimit(
                parameter.minimum, parameter.maximum,
                parameter.defaultValue);
            parameterKeys.insert(parameter.key);
            state.parameters.push_back(std::move(parameter));
        }
    }

    std::set<std::uint64_t> controlIds;
    std::uint64_t highestId {};
    const auto restoreControls = [&parameterKeys, &controlIds, &highestId,
                                  sourceSchema](
                                     const juce::var& value,
                                     std::vector<ModuleInterfaceControl>& destination) {
      if (const auto* controls = value.getArray()) {
        for (const auto& controlValue : *controls) {
            const auto* object = controlValue.getDynamicObject();
            if (object == nullptr)
                continue;
            ModuleInterfaceControl control;
            control.id = static_cast<std::uint64_t>(
                static_cast<juce::int64>(object->getProperty(jsonKeys::id)));
            if (control.id == 0 || controlIds.contains(control.id))
                continue;
            control.kind = controlKindFromKey(object->getProperty(jsonKeys::kind).toString());
            // A two-axis control did not exist before schema 11. If an asset
            // carries an inconsistent old schema marker, preserve its primary
            // parameter as a usable knob instead of restoring an invalid pad.
            if (sourceSchema < 11
                && control.kind == ModuleControlKind::xyPad)
                control.kind = ModuleControlKind::knob;
            if (sourceSchema < 14
                && control.kind == ModuleControlKind::filterResponse)
                control.kind = ModuleControlKind::knob;
            if (sourceSchema < 15
                && control.kind == ModuleControlKind::dynamicsResponse)
                control.kind = ModuleControlKind::knob;
            if (sourceSchema < 16
                && control.kind == ModuleControlKind::eqCurve)
                control.kind = ModuleControlKind::knob;
            control.label = object->getProperty(jsonKeys::label).toString();
            control.parameterBinding = object->getProperty(jsonKeys::binding).toString();
            control.secondaryParameterBinding = sourceSchema >= 11
                ? object->getProperty(jsonKeys::secondaryBinding).toString()
                : juce::String {};
            control.tertiaryParameterBinding = sourceSchema >= 13
                ? object->getProperty(jsonKeys::tertiaryBinding).toString()
                : juce::String {};
            control.quaternaryParameterBinding = sourceSchema >= 13
                ? object->getProperty(jsonKeys::quaternaryBinding).toString()
                : juce::String {};
            if (sourceSchema >= 16)
                if (const auto* extras =
                        object->getProperty(jsonKeys::extraBindings).getArray())
                    for (const auto& binding : *extras)
                        control.extraParameterBindings.push_back(
                            binding.toString());
            control.x = juce::jlimit(0.0f, 1.0f,
                                    static_cast<float>(static_cast<double>(object->getProperty(jsonKeys::x))));
            control.y = juce::jlimit(0.0f, 1.0f,
                                    static_cast<float>(static_cast<double>(object->getProperty(jsonKeys::y))));
            // These floors have to admit everything layoutPublishedInterface
            // can generate. They were the taller of the two, so a dense knob
            // grid came back from a reload half a row taller than it was
            // saved, overlapping the row beneath it.
            control.width = juce::jlimit(0.04f, 1.0f,
                                        static_cast<float>(static_cast<double>(object->getProperty(jsonKeys::width))));
            control.height = juce::jlimit(0.05f, 1.0f,
                                         static_cast<float>(static_cast<double>(object->getProperty(jsonKeys::height))));
            control.portKind = static_cast<std::uint8_t>(juce::jlimit(
                0, 3, static_cast<int>(object->getProperty(jsonKeys::portKind))));
            const auto defaultChannels = control.portKind == 0 ? 2 : 1;
            control.portChannels = static_cast<std::uint8_t>(juce::jlimit(
                1, 8, sourceSchema >= 7 && object->hasProperty(jsonKeys::portChannels)
                          ? static_cast<int>(
                                object->getProperty(jsonKeys::portChannels))
                          : defaultChannels));
            // Before schema 8, the field only described Graph ports. Level
            // meters were always painted as stereo even though their stored
            // aggregate default was one channel.
            if (sourceSchema < 8
                && control.kind == ModuleControlKind::levelMeter)
                control.portChannels = 2;
            control.x = std::min(control.x, 1.0f - control.width);
            control.y = std::min(control.y, 1.0f - control.height);
            if (isParameterControl(control.kind)
                && !parameterKeys.contains(control.parameterBinding))
                control.parameterBinding.clear();
            if ((control.kind == ModuleControlKind::xyPad
                 || control.kind == ModuleControlKind::filterResponse
                 || control.kind == ModuleControlKind::dynamicsResponse)
                && !parameterKeys.contains(
                    control.secondaryParameterBinding))
                control.secondaryParameterBinding.clear();
            if (control.kind == ModuleControlKind::eqCurve) {
                // Three bindings per band, all distinct and all published.
                // Anything else is not a curve, so it falls back to a plot.
                const auto& extras = control.extraParameterBindings;
                const std::set<juce::String> unique(extras.begin(),
                                                    extras.end());
                const auto valid = !extras.empty() && extras.size() % 3 == 0
                    && unique.size() == extras.size()
                    && std::all_of(extras.begin(), extras.end(),
                                   [&parameterKeys](const auto& binding) {
                                       return parameterKeys.contains(binding);
                                   });
                if (!valid)
                    control.kind = ModuleControlKind::spectrum;
            }
            if (control.kind == ModuleControlKind::envelope) {
                const auto valid = sourceSchema >= 13
                    && parameterKeys.contains(control.parameterBinding)
                    && parameterKeys.contains(control.secondaryParameterBinding)
                    && parameterKeys.contains(control.tertiaryParameterBinding)
                    && parameterKeys.contains(control.quaternaryParameterBinding);
                std::set<juce::String> bindings {
                    control.parameterBinding,
                    control.secondaryParameterBinding,
                    control.tertiaryParameterBinding,
                    control.quaternaryParameterBinding
                };
                if (!valid || bindings.size() != 4)
                    control.kind = ModuleControlKind::waveform;
            }
            else if (!isParameterControl(control.kind)
                     && !isPortControl(control.kind))
                control.parameterBinding.clear();
            controlIds.insert(control.id);
            highestId = std::max(highestId, control.id);
            destination.push_back(std::move(control));
        }
      }
    };
    restoreControls(root->getProperty(jsonKeys::controls), state.controls);
    if (sourceSchema >= 2)
        restoreControls(root->getProperty(jsonKeys::deviceControls),
                        state.deviceControls);
    if (sourceSchema < 2 && state.deviceControls.empty()) {
        state.deviceControls.reserve(state.controls.size());
        for (const auto& graphControl : state.controls) {
            auto deviceControl = graphControl;
            deviceControl.id = ++highestId;
            deviceControl.x = juce::jlimit(0.0f, 1.0f - deviceControl.width,
                                           deviceControl.x);
            deviceControl.y = juce::jlimit(0.0f, 1.0f - deviceControl.height,
                                           deviceControl.y);
            controlIds.insert(deviceControl.id);
            state.deviceControls.push_back(std::move(deviceControl));
        }
    }
    state.nextControlId = std::max<std::uint64_t>(
        highestId + 1,
        static_cast<std::uint64_t>(static_cast<juce::int64>(
            root->getProperty(jsonKeys::nextControlId))));
    if (sourceSchema < 3) {
        for (auto& parameter : state.parameters)
            parameter.showInDevice = std::any_of(
                state.deviceControls.begin(), state.deviceControls.end(),
                [&parameter](const auto& control) {
                    return isParameterControl(control.kind)
                           && control.parameterBinding == parameter.key;
                });
    }
    rebuildAutomaticDeviceControls(state);
    return state;
}

juce::Result validateModuleInterface(const ModuleInterfaceState& state)
{
    if (state.gridColumns < 4 || state.gridColumns > 64
        || state.gridRows < 4 || state.gridRows > 64)
        return juce::Result::fail("Module interface grid dimensions are invalid");
    std::set<juce::String> parameterKeys;
    std::set<std::uint32_t> hostParameterIds;
    for (const auto& parameter : state.parameters) {
        if (parameter.key.isEmpty() || parameterKeys.contains(parameter.key))
            return juce::Result::fail("Published module parameters need unique stable keys");
        if (parameter.minimum > parameter.maximum
            || parameter.value < parameter.minimum
            || parameter.value > parameter.maximum)
            return juce::Result::fail("Published module parameter range is invalid");
        const auto hostParameterId = parameter.hostParameterId != 0
            ? parameter.hostParameterId
            : stableModuleParameterId(parameter.key);
        if (hostParameterId == 0
            || (parameter.publishToHost
                && !hostParameterIds.insert(hostParameterId).second))
            return juce::Result::fail(
                "Host-published module parameters need unique non-zero stable IDs");
        parameterKeys.insert(parameter.key);
    }
    std::set<std::uint64_t> controlIds;
    const auto validateControls = [&parameterKeys, &controlIds](
                                      const auto& controls) -> juce::Result {
      for (const auto& control : controls) {
        if (control.id == 0 || controlIds.contains(control.id))
            return juce::Result::fail("Module interface controls need unique non-zero IDs");
        if (control.x < 0.0f || control.y < 0.0f
            || control.width <= 0.0f || control.height <= 0.0f
            || control.x + control.width > 1.001f
            || control.y + control.height > 1.001f)
            return juce::Result::fail("Module interface control bounds are invalid");
        if (isParameterControl(control.kind)
            && !parameterKeys.contains(control.parameterBinding))
            return juce::Result::fail("Module interface control binding is missing");
        if ((control.kind == ModuleControlKind::xyPad
             || control.kind == ModuleControlKind::filterResponse
             || control.kind == ModuleControlKind::dynamicsResponse)
            && (!parameterKeys.contains(control.secondaryParameterBinding)
                || control.secondaryParameterBinding
                       == control.parameterBinding))
            return juce::Result::fail(
                "Module interface two-parameter control needs two different bindings");
        if (control.kind == ModuleControlKind::eqCurve) {
            const auto& extras = control.extraParameterBindings;
            const std::set<juce::String> unique(extras.begin(), extras.end());
            if (extras.empty() || extras.size() % 3 != 0
                || unique.size() != extras.size()
                || !std::all_of(extras.begin(), extras.end(),
                                [&parameterKeys](const auto& binding) {
                                    return parameterKeys.contains(binding);
                                }))
                return juce::Result::fail(
                    "Module EQ curve needs three distinct bindings per band");
        }
        if (control.kind == ModuleControlKind::envelope) {
            const std::set<juce::String> bindings {
                control.parameterBinding,
                control.secondaryParameterBinding,
                control.tertiaryParameterBinding,
                control.quaternaryParameterBinding
            };
            if (bindings.size() != 4
                || !parameterKeys.contains(control.secondaryParameterBinding)
                || !parameterKeys.contains(control.tertiaryParameterBinding)
                || !parameterKeys.contains(control.quaternaryParameterBinding))
                return juce::Result::fail(
                    "Module envelope needs four different bindings");
        }
        if (isPortControl(control.kind)
            && (control.portChannels < 1 || control.portChannels > 8
                || (control.portKind != 0 && control.portChannels != 1)))
            return juce::Result::fail(
                "Module interface port channel contract is invalid");
        if (control.kind == ModuleControlKind::levelMeter
            && (control.portChannels < 1 || control.portChannels > 8))
            return juce::Result::fail(
                "Module interface meter channel format is invalid");
        controlIds.insert(control.id);
      }
      return juce::Result::ok();
    };
    if (const auto graphResult = validateControls(state.controls);
        graphResult.failed())
        return graphResult;
    if (const auto deviceResult = validateControls(state.deviceControls);
        deviceResult.failed())
        return deviceResult;
    return juce::Result::ok();
}

std::uint32_t stableModuleParameterId(
    const juce::String& parameterKey) noexcept
{
    // FNV-1a is deliberately independent of UI order and platform. The high
    // bit stays clear for hosts that internally use signed parameter IDs.
    auto hash = std::uint32_t { 2'166'136'261u };
    const auto utf8 = parameterKey.toRawUTF8();
    for (auto index = std::size_t {}; utf8[index] != 0; ++index) {
        hash ^= static_cast<std::uint8_t>(utf8[index]);
        hash *= 16'777'619u;
    }
    hash &= 0x7fffffffu;
    return hash != 0 ? hash : 1u;
}

ModuleInterfaceEditorComponent::ModuleInterfaceEditorComponent(ModuleInterfaceState state)
    : state_(std::move(state))
{
    setSize(1'080, 640);
    setWantsKeyboardFocus(true);
    addAndMakeVisible(titleLabel_);
    addAndMakeVisible(editButton_);
    addAndMakeVisible(addButton_);
    addAndMakeVisible(graphModeButton_);
    addAndMakeVisible(deviceModeButton_);
    addAndMakeVisible(snapButton_);
    addAndMakeVisible(gridButton_);
    addAndMakeVisible(undoButton_);
    addAndMakeVisible(redoButton_);
    addAndMakeVisible(copyToDeviceButton_);
    addAndMakeVisible(hintLabel_);
    addAndMakeVisible(inspectorTitleLabel_);
    addAndMakeVisible(inspectorKindLabel_);
    addAndMakeVisible(inspectorBindingLabel_);
    addAndMakeVisible(controlPrimaryBindingCaption_);
    addAndMakeVisible(controlSecondaryBindingCaption_);
    addAndMakeVisible(controlTertiaryBindingCaption_);
    addAndMakeVisible(controlQuaternaryBindingCaption_);
    addAndMakeVisible(controlPrimaryBindingSelector_);
    addAndMakeVisible(controlSecondaryBindingSelector_);
    addAndMakeVisible(controlTertiaryBindingSelector_);
    addAndMakeVisible(controlQuaternaryBindingSelector_);
    addAndMakeVisible(controlLabelCaption_);
    addAndMakeVisible(controlXCaption_);
    addAndMakeVisible(controlYCaption_);
    addAndMakeVisible(controlWidthCaption_);
    addAndMakeVisible(controlHeightCaption_);
    addAndMakeVisible(controlLabelEditor_);
    addAndMakeVisible(controlXEditor_);
    addAndMakeVisible(controlYEditor_);
    addAndMakeVisible(controlWidthEditor_);
    addAndMakeVisible(controlHeightEditor_);
    addAndMakeVisible(applyInspectorButton_);
    titleLabel_.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    titleLabel_.setColour(juce::Label::textColourId, juce::Colour(0xfff0f2f4));
    hintLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff9299a1));
    hintLabel_.setFont(juce::FontOptions(9.5f));
    hintLabel_.setJustificationType(juce::Justification::centredRight);
    inspectorTitleLabel_.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    inspectorTitleLabel_.setColour(juce::Label::textColourId,
                                   juce::Colour(0xffffa33a));
    inspectorKindLabel_.setColour(juce::Label::textColourId,
                                  juce::Colour(0xffd8dde3));
    inspectorBindingLabel_.setColour(juce::Label::textColourId,
                                     juce::Colour(0xff9299a1));
    for (auto* caption : { &controlPrimaryBindingCaption_,
                           &controlSecondaryBindingCaption_,
                           &controlTertiaryBindingCaption_,
                           &controlQuaternaryBindingCaption_,
                           &controlLabelCaption_, &controlXCaption_,
                           &controlYCaption_, &controlWidthCaption_,
                           &controlHeightCaption_ }) {
        caption->setFont(juce::FontOptions(9.0f, juce::Font::bold));
        caption->setColour(juce::Label::textColourId,
                           juce::Colour(0xff9299a1));
    }
    for (auto* editor : { &controlLabelEditor_, &controlXEditor_,
                          &controlYEditor_, &controlWidthEditor_,
                          &controlHeightEditor_ }) {
        editor->setColour(juce::TextEditor::backgroundColourId,
                          juce::Colour(0xff111419));
        editor->setColour(juce::TextEditor::outlineColourId,
                          juce::Colour(0xff3c434b));
        editor->setColour(juce::TextEditor::focusedOutlineColourId,
                          juce::Colour(0xffff8a00));
        editor->setSelectAllWhenFocused(true);
        editor->onReturnKey = [this] { applyInspectorChanges(); };
    }
    for (auto* editor : { &controlXEditor_, &controlYEditor_,
                          &controlWidthEditor_, &controlHeightEditor_ })
        editor->setInputRestrictions(8, "0123456789.,-");
    editButton_.onClick = [this] {
        rebuildSliders();
        refreshTranslations();
        refreshInspector();
        repaint();
    };
    addButton_.onClick = [this] { showAddMenu(); };
    graphModeButton_.setClickingTogglesState(true);
    deviceModeButton_.setClickingTogglesState(true);
    graphModeButton_.setRadioGroupId(91);
    deviceModeButton_.setRadioGroupId(91);
    graphModeButton_.setToggleState(true, juce::dontSendNotification);
    graphModeButton_.onClick = [this] {
        setLayoutMode(ModuleInterfaceLayoutMode::graph);
    };
    deviceModeButton_.onClick = [this] {
        setLayoutMode(ModuleInterfaceLayoutMode::device);
    };
    snapButton_.setClickingTogglesState(true);
    snapButton_.setToggleState(state_.snapToGrid, juce::dontSendNotification);
    snapButton_.onClick = [this] {
        state_.snapToGrid = snapButton_.getToggleState();
        notifyStateChanged();
        repaint();
    };
    gridButton_.onClick = [this] { showGridMenu(); };
    undoButton_.onClick = [this] { undoInterfaceChange(); };
    redoButton_.onClick = [this] { redoInterfaceChange(); };
    copyToDeviceButton_.onClick = [this] { showDeviceParametersMenu(); };
    applyInspectorButton_.onClick = [this] { applyInspectorChanges(); };
    controlPrimaryBindingSelector_.onChange = [this] {
        if (refreshingInspector_)
            return;
        auto* control = findControl(selectedControlId_);
        const auto index = controlPrimaryBindingSelector_.getSelectedItemIndex();
        if (control == nullptr || !isParameterControl(control->kind)
            || index < 0
            || static_cast<std::size_t>(index) >= state_.parameters.size())
            return;
        const auto key = state_.parameters[static_cast<std::size_t>(index)].key;
        if (control->kind == ModuleControlKind::envelope
            && (key == control->secondaryParameterBinding
                || key == control->tertiaryParameterBinding
                || key == control->quaternaryParameterBinding)) {
            refreshInspector();
            return;
        }
        control->parameterBinding = key;
        if ((control->kind == ModuleControlKind::xyPad
             || control->kind == ModuleControlKind::filterResponse
             || control->kind == ModuleControlKind::dynamicsResponse)
            && control->secondaryParameterBinding
                   == control->parameterBinding)
            for (const auto& parameter : state_.parameters)
                if (parameter.key != control->parameterBinding) {
                    control->secondaryParameterBinding = parameter.key;
                    break;
                }
        rebuildSliders();
        refreshInspector();
        notifyStateChanged();
        repaint();
    };
    controlSecondaryBindingSelector_.onChange = [this] {
        if (refreshingInspector_)
            return;
        auto* control = findControl(selectedControlId_);
        const auto index = controlSecondaryBindingSelector_.getSelectedItemIndex();
        if (control == nullptr
            || (control->kind != ModuleControlKind::xyPad
                && control->kind != ModuleControlKind::filterResponse
                && control->kind != ModuleControlKind::dynamicsResponse
                && control->kind != ModuleControlKind::envelope)
            || index < 0
            || static_cast<std::size_t>(index) >= state_.parameters.size())
            return;
        const auto& key = state_.parameters[static_cast<std::size_t>(index)].key;
        if (key == control->parameterBinding
            || (control->kind == ModuleControlKind::envelope
                && (key == control->tertiaryParameterBinding
                    || key == control->quaternaryParameterBinding))) {
            refreshInspector();
            return;
        }
        control->secondaryParameterBinding = key;
        rebuildSliders();
        refreshInspector();
        notifyStateChanged();
        repaint();
    };
    controlTertiaryBindingSelector_.onChange = [this] {
        if (refreshingInspector_)
            return;
        auto* control = findControl(selectedControlId_);
        const auto index = controlTertiaryBindingSelector_.getSelectedItemIndex();
        if (control == nullptr || control->kind != ModuleControlKind::envelope
            || index < 0
            || static_cast<std::size_t>(index) >= state_.parameters.size())
            return;
        const auto& key = state_.parameters[static_cast<std::size_t>(index)].key;
        if (key == control->parameterBinding
            || key == control->secondaryParameterBinding
            || key == control->quaternaryParameterBinding) {
            refreshInspector();
            return;
        }
        control->tertiaryParameterBinding = key;
        rebuildSliders();
        refreshInspector();
        notifyStateChanged();
        repaint();
    };
    controlQuaternaryBindingSelector_.onChange = [this] {
        if (refreshingInspector_)
            return;
        auto* control = findControl(selectedControlId_);
        const auto index = controlQuaternaryBindingSelector_.getSelectedItemIndex();
        if (control == nullptr || control->kind != ModuleControlKind::envelope
            || index < 0
            || static_cast<std::size_t>(index) >= state_.parameters.size())
            return;
        const auto& key = state_.parameters[static_cast<std::size_t>(index)].key;
        if (key == control->parameterBinding
            || key == control->secondaryParameterBinding
            || key == control->tertiaryParameterBinding) {
            refreshInspector();
            return;
        }
        control->quaternaryParameterBinding = key;
        rebuildSliders();
        refreshInspector();
        notifyStateChanged();
        repaint();
    };
    rebuildSliders();
    committedState_ = serialiseModuleInterface(state_);
    refreshTranslations();
    refreshInspector();
    refreshHistoryButtons();
}

void ModuleInterfaceEditorComponent::refreshTranslations()
{
    titleLabel_.setText(tr("module.ui.title"), juce::dontSendNotification);
    editButton_.setButtonText(tr("module.ui.edit"));
    addButton_.setButtonText(tr("module.ui.addControl"));
    graphModeButton_.setButtonText(tr("module.ui.graphFree"));
    deviceModeButton_.setButtonText(tr("module.ui.deviceFit"));
    snapButton_.setButtonText(tr("module.ui.snap"));
    gridButton_.setButtonText(
        tr("module.ui.gridSize", "columns", juce::String(state_.gridColumns))
            .replace("{rows}", juce::String(state_.gridRows)));
    undoButton_.setButtonText(tr("module.ui.undo"));
    redoButton_.setButtonText(tr("module.ui.redo"));
    copyToDeviceButton_.setButtonText(tr("module.ui.deviceParameters"));
    inspectorTitleLabel_.setText(tr("module.ui.inspector"),
                                 juce::dontSendNotification);
    controlPrimaryBindingCaption_.setText(tr("module.ui.primaryBinding"),
                                           juce::dontSendNotification);
    controlSecondaryBindingCaption_.setText(tr("module.ui.secondaryBinding"),
                                             juce::dontSendNotification);
    controlTertiaryBindingCaption_.setText(tr("module.ui.tertiaryBinding"),
                                            juce::dontSendNotification);
    controlQuaternaryBindingCaption_.setText(tr("module.ui.quaternaryBinding"),
                                              juce::dontSendNotification);
    controlLabelCaption_.setText(tr("module.ui.controlLabel"),
                                 juce::dontSendNotification);
    controlXCaption_.setText(tr("module.ui.controlX"),
                             juce::dontSendNotification);
    controlYCaption_.setText(tr("module.ui.controlY"),
                             juce::dontSendNotification);
    controlWidthCaption_.setText(tr("module.ui.controlWidth"),
                                 juce::dontSendNotification);
    controlHeightCaption_.setText(tr("module.ui.controlHeight"),
                                  juce::dontSendNotification);
    applyInspectorButton_.setButtonText(
        tr("module.ui.applyControlProperties"));
    editButton_.setTooltip(tr("module.ui.editHelp"));
    addButton_.setTooltip(tr("module.ui.addControlHelp"));
    graphModeButton_.setTooltip(tr("module.ui.graphFreeHelp"));
    deviceModeButton_.setTooltip(tr("module.ui.deviceFitHelp"));
    snapButton_.setTooltip(tr("module.ui.snapHelp"));
    gridButton_.setTooltip(tr("module.ui.gridSizeHelp"));
    undoButton_.setTooltip(tr("module.ui.undoHelp"));
    redoButton_.setTooltip(tr("module.ui.redoHelp"));
    copyToDeviceButton_.setTooltip(tr("module.ui.deviceParametersHelp"));
    inspectorTitleLabel_.setTooltip(tr("module.ui.inspectorHelp"));
    applyInspectorButton_.setTooltip(tr("module.ui.inspectorHelp"));
    hintLabel_.setText(tr(editButton_.getToggleState()
                              ? "module.ui.editHint"
                              : "module.ui.performHint"),
                       juce::dontSendNotification);
    refreshInspector();
    repaint();
}

void ModuleInterfaceEditorComponent::setState(ModuleInterfaceState state)
{
    state_ = std::move(state);
    undoStates_.clear();
    redoStates_.clear();
    historyGestureActive_ = false;
    historyGestureStart_ = juce::var();
    snapButton_.setToggleState(state_.snapToGrid, juce::dontSendNotification);
    rebuildAutomaticDeviceControls(state_);
    committedState_ = serialiseModuleInterface(state_);
    selectedControlId_ = 0;
    rebuildSliders();
    refreshInspector();
    refreshHistoryButtons();
    resized();
    repaint();
}

const ModuleInterfaceState& ModuleInterfaceEditorComponent::state() const noexcept
{
    return state_;
}

void ModuleInterfaceEditorComponent::setStateChangedHandler(
    std::function<void(const ModuleInterfaceState&)> handler)
{
    stateChangedHandler_ = std::move(handler);
}

void ModuleInterfaceEditorComponent::setParameterChangedHandler(
    std::function<void(const juce::String&, double)> handler)
{
    parameterChangedHandler_ = std::move(handler);
}

bool ModuleInterfaceEditorComponent::setParameterValue(
    const juce::String& key, const double value)
{
    auto* parameter = findParameter(key);
    if (parameter == nullptr || !std::isfinite(value))
        return false;
    const auto safeValue = juce::jlimit(parameter->minimum,
                                        parameter->maximum, value);
    if (std::abs(parameter->value - safeValue) <= 1.0e-9)
        return false;
    parameter->value = safeValue;
    for (auto& entry : sliders_)
        if (entry.binding == key)
            entry.slider->setValue(safeValue, juce::dontSendNotification);
    const auto middle = parameter->minimum
        + (parameter->maximum - parameter->minimum) * 0.5;
    for (auto& entry : buttons_)
        if (entry.binding == key)
            entry.button->setToggleState(safeValue > middle,
                                          juce::dontSendNotification);
    for (auto& entry : combos_)
        if (entry.binding == key)
            entry.combo->setSelectedItemIndex(
                static_cast<int>(closestModuleSelectorValue(
                    entry.values, safeValue)),
                juce::dontSendNotification);
    synchroniseXyPads(key);
    synchroniseEnvelopes(key);
    synchroniseFilterResponses(key);
    synchroniseDynamicsResponses(key);
    repaint();
    return true;
}

void ModuleInterfaceEditorComponent::setPortCreationHandler(
    std::function<void(bool, std::uint8_t, std::uint8_t)> handler)
{
    portCreationHandler_ = std::move(handler);
}

ModuleAnalysisDisplayComponent::ModuleAnalysisDisplayComponent(
    const ModuleControlKind kind, const std::size_t channels)
    : kind_(kind), channels_(juce::jlimit<std::size_t>(1, peak_.size(), channels))
{
    setOpaque(true);
    setInterceptsMouseClicks(false, false);
}

void ModuleAnalysisDisplayComponent::setWaveform(const float* samples,
                                                 const std::size_t count)
{
    waveform_.assign(samples, samples + count);
    active_ = true;
    repaint();
}

void ModuleAnalysisDisplayComponent::setSpectrum(const float* bins,
                                                 const std::size_t count)
{
    for (std::size_t bin = 0; bin < spectrum_.size(); ++bin)
        spectrum_[bin] = bin < count ? bins[bin] : 0.0f;
    active_ = true;
    repaint();
}

void ModuleAnalysisDisplayComponent::setLevels(
    const std::array<float, cydaw::engine::AudioAnalysisFrame::maximumChannels>& peak,
    const std::array<float, cydaw::engine::AudioAnalysisFrame::maximumChannels>& rms,
    const std::size_t channelCount)
{
    peak_ = peak;
    rms_ = rms;
    channelCount_ = channelCount;
    active_ = true;
    repaint();
}

void ModuleAnalysisDisplayComponent::clear()
{
    waveform_.clear();
    spectrum_.fill(0.0f);
    peak_.fill(0.0f);
    rms_.fill(0.0f);
    active_ = false;
    repaint();
}

void ModuleAnalysisDisplayComponent::paint(juce::Graphics& graphics)
{
    // Opaque: the frame's own fill under the rounded display, so the
    // corners look as they did when the surface drew this.
    const auto display = getLocalBounds().toFloat();
    graphics.fillAll(juce::Colour(0xff292d32));
    graphics.setColour(juce::Colour(0xff101317));
    graphics.fillRoundedRectangle(display, 3.0f);
    if (kind_ == ModuleControlKind::waveform) {
        if (active_ && waveform_.size() > 1) {
            juce::Path path;
            for (std::size_t sample = 0; sample < waveform_.size(); ++sample) {
                const auto x = display.getX()
                               + static_cast<float>(sample)
                                     / static_cast<float>(waveform_.size() - 1)
                                     * display.getWidth();
                const auto y = display.getCentreY()
                               - waveform_[sample] * display.getHeight() * 0.45f;
                if (sample == 0)
                    path.startNewSubPath(x, y);
                else
                    path.lineTo(x, y);
            }
            graphics.setColour(juce::Colour(0xff64d7ff));
            graphics.strokePath(path, juce::PathStrokeType(1.2f));
        }
    } else if (kind_ == ModuleControlKind::spectrum) {
        const auto barWidth = display.getWidth()
                              / static_cast<float>(spectrum_.size());
        for (std::size_t bin = 0; bin < spectrum_.size(); ++bin) {
            const auto height = spectrum_[bin] * display.getHeight();
            graphics.setColour(juce::Colour(0xff6cb8ff)
                                   .withAlpha(0.35f + spectrum_[bin] * 0.6f));
            graphics.fillRect(display.getX() + static_cast<float>(bin) * barWidth,
                              display.getBottom() - height,
                              std::max(1.0f, barWidth - 1.0f), height);
        }
    } else {
        const auto gap = channels_ > 4 ? 1.0f : 2.0f;
        const auto meterWidth = std::max(
            2.0f, (display.getWidth() - gap * static_cast<float>(channels_ - 1))
                      / static_cast<float>(channels_));
        for (std::size_t channel = 0; channel < channels_; ++channel) {
            auto meter = juce::Rectangle<float>(
                display.getX() + static_cast<float>(channel) * (meterWidth + gap),
                display.getY(), meterWidth, display.getHeight());
            graphics.setColour(juce::Colour(0xff101317));
            graphics.fillRoundedRectangle(meter, 2.0f);
            const auto level = juce::jlimit(
                0.0f, 1.0f, channel < channelCount_ ? rms_[channel] : 0.0f);
            const auto fill = meter.withTrimmedTop(meter.getHeight() * (1.0f - level));
            graphics.setColour(channel < channelCount_ && peak_[channel] > 0.98f
                                   ? juce::Colour(0xffff5757)
                                   : juce::Colour(0xff63d98a));
            graphics.fillRoundedRectangle(fill, 2.0f);
        }
    }
}

void ModuleInterfaceEditorComponent::setAnalysisFrame(
    const cydaw::engine::AudioAnalysisFrame& frame)
{
    analysisActive_ = true;
    analysisChannelCount_ = juce::jlimit<std::size_t>(
        1, peak_.size(), static_cast<std::size_t>(frame.channelCount));
    waveformCount_ = std::min<std::size_t>(frame.waveformCount, waveform_.size());
    std::copy_n(frame.waveform.begin(), waveformCount_, waveform_.begin());
    lastFrame_ = frame;
    for (std::size_t channel = 0; channel < peak_.size(); ++channel) {
        const auto active = channel < analysisChannelCount_;
        peak_[channel] = std::max(active ? frame.peak[channel] : 0.0f,
                                  peak_[channel] * 0.82f);
        rms_[channel] = rms_[channel] * 0.68f
                        + (active ? frame.rms[channel] : 0.0f) * 0.32f;
    }
    updateSpectrum();
    // The displays repaint themselves; the surface is not touched.
    for (auto& entry : analysisDisplays_) {
        const auto* control = findControl(entry.controlId);
        if (control == nullptr || entry.display == nullptr)
            continue;
        if (control->kind == ModuleControlKind::waveform)
            entry.display->setWaveform(waveform_.data(), waveformCount_);
        else if (control->kind == ModuleControlKind::spectrum)
            entry.display->setSpectrum(spectrum_.data(), spectrum_.size());
        else
            entry.display->setLevels(peak_, rms_, analysisChannelCount_);
    }
}

void ModuleInterfaceEditorComponent::clearAnalysis() noexcept
{
    analysisActive_ = false;
    waveformCount_ = 0;
    waveform_.fill(0.0f);
    spectrum_.fill(0.0f);
    peak_.fill(0.0f);
    rms_.fill(0.0f);
    analysisChannelCount_ = 2;
    for (auto& entry : analysisDisplays_)
        if (entry.display != nullptr)
            entry.display->clear();
    repaint();
}

void ModuleInterfaceEditorComponent::setCompactMode(const bool compact)
{
    compactMode_ = compact;
    titleLabel_.setVisible(!compact);
    editButton_.setVisible(!compact);
    addButton_.setVisible(!compact);
    graphModeButton_.setVisible(!compact);
    deviceModeButton_.setVisible(!compact);
    snapButton_.setVisible(!compact);
    gridButton_.setVisible(!compact);
    undoButton_.setVisible(!compact);
    redoButton_.setVisible(!compact);
    copyToDeviceButton_.setVisible(!compact);
    hintLabel_.setVisible(!compact);
    const std::array<juce::Component*, 14> inspectorComponents {
        &inspectorTitleLabel_, &inspectorKindLabel_, &inspectorBindingLabel_,
        &controlLabelCaption_, &controlXCaption_, &controlYCaption_,
        &controlWidthCaption_, &controlHeightCaption_, &controlLabelEditor_,
        &controlXEditor_, &controlYEditor_, &controlWidthEditor_,
        &controlHeightEditor_, &applyInspectorButton_
    };
    for (auto* component : inspectorComponents)
        component->setVisible(!compact);
    if (compact) {
        editButton_.setToggleState(false, juce::dontSendNotification);
        layoutMode_ = ModuleInterfaceLayoutMode::device;
        rebuildAutomaticDeviceControls(state_);
    }
    rebuildSliders();
    refreshHistoryButtons();
    resized();
    repaint();
}

void ModuleInterfaceEditorComponent::setPerformanceMode(const bool performance)
{
    // Compact mode's chrome-less surface, with the drawn layout instead of
    // the compact strip.
    performanceMode_ = performance;
    setCompactMode(performance);
    if (performance) {
        layoutMode_ = ModuleInterfaceLayoutMode::graph;
        selectedControlId_ = 0;
        rebuildSliders();
        resized();
        repaint();
    }
}

void ModuleInterfaceEditorComponent::setLayoutMode(
    const ModuleInterfaceLayoutMode mode)
{
    layoutMode_ = mode;
    if (mode == ModuleInterfaceLayoutMode::device) {
        rebuildAutomaticDeviceControls(state_);
        editButton_.setToggleState(false, juce::dontSendNotification);
    }
    graphModeButton_.setToggleState(mode == ModuleInterfaceLayoutMode::graph,
                                    juce::dontSendNotification);
    deviceModeButton_.setToggleState(mode == ModuleInterfaceLayoutMode::device,
                                     juce::dontSendNotification);
    editButton_.setEnabled(mode == ModuleInterfaceLayoutMode::graph);
    addButton_.setEnabled(mode == ModuleInterfaceLayoutMode::graph);
    snapButton_.setEnabled(mode == ModuleInterfaceLayoutMode::graph);
    gridButton_.setEnabled(mode == ModuleInterfaceLayoutMode::graph);
    undoButton_.setEnabled(mode == ModuleInterfaceLayoutMode::graph
                           && !undoStates_.empty());
    redoButton_.setEnabled(mode == ModuleInterfaceLayoutMode::graph
                           && !redoStates_.empty());
    copyToDeviceButton_.setVisible(!compactMode_);
    selectedControlId_ = 0;
    draggingControl_ = false;
    resizingControl_ = false;
    rebuildSliders();
    refreshInspector();
    resized();
    refreshTranslations();
    repaint();
}

ModuleInterfaceLayoutMode ModuleInterfaceEditorComponent::layoutMode() const noexcept
{
    return layoutMode_;
}

juce::Rectangle<int> ModuleInterfaceEditorComponent::canvasBounds() const
{
    auto available = compactMode_
                         ? getLocalBounds().reduced(4)
                         : getLocalBounds().withTrimmedTop(70)
                               .withTrimmedBottom(24).withTrimmedRight(240)
                               .reduced(8, 6);
    if (layoutMode_ == ModuleInterfaceLayoutMode::graph || compactMode_)
        return available;
    constexpr auto deviceAspect = 1.75;
    const auto targetHeight = juce::jmin(
        available.getHeight(),
        juce::roundToInt(static_cast<double>(available.getWidth()) / deviceAspect));
    const auto targetWidth = juce::jmin(
        available.getWidth(),
        juce::roundToInt(static_cast<double>(targetHeight) * deviceAspect));
    return available.withSizeKeepingCentre(targetWidth, targetHeight);
}

juce::Rectangle<int> ModuleInterfaceEditorComponent::inspectorBounds() const
{
    if (compactMode_)
        return {};
    return getLocalBounds().withTrimmedTop(70).withTrimmedBottom(24)
        .removeFromRight(232).reduced(6);
}

juce::Rectangle<float> ModuleInterfaceEditorComponent::controlBounds(
    const ModuleInterfaceControl& control) const
{
    if (compactMode_ && !performanceMode_)
        if (const auto cell = compactCells_.find(control.id);
            cell != compactCells_.end())
            return cell->second;
    const auto canvas = canvasBounds().toFloat();
    return { canvas.getX() + control.x * canvas.getWidth(),
             canvas.getY() + control.y * canvas.getHeight(),
             control.width * canvas.getWidth(),
             control.height * canvas.getHeight() };
}

namespace {
// A knob's cell in the compact surface: wide enough for a caption of ten
// characters at the caption's size, and for the knob under it.
constexpr auto compactCellWidth = 88;
// One row this far; the row is what a person reads across.
constexpr auto compactSingleRowCells = 16;

[[nodiscard]] int compactSpan(const ModuleControlKind kind) noexcept
{
    switch (kind) {
        case ModuleControlKind::eqCurve: return 3;
        case ModuleControlKind::envelope:
        case ModuleControlKind::xyPad:
        case ModuleControlKind::filterResponse:
        case ModuleControlKind::dynamicsResponse:
        case ModuleControlKind::waveform:
        case ModuleControlKind::spectrum:
        case ModuleControlKind::levelMeter:
        case ModuleControlKind::group: return 2;
        default: return 1;
    }
}
} // namespace

ModuleInterfaceEditorComponent::CompactPlan
ModuleInterfaceEditorComponent::compactPlan() const
{
    CompactPlan plan;
    std::vector<const ModuleInterfaceControl*> ordered;
    for (const auto& control : activeControls())
        if (control.kind != ModuleControlKind::inputPort
            && control.kind != ModuleControlKind::outputPort)
            ordered.push_back(&control);
    // The stored geometry keeps the order — rows top to bottom, left to
    // right — and nothing else.
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const auto* a, const auto* b) {
                         const auto rowA = std::lround(a->y * 100.0f);
                         const auto rowB = std::lround(b->y * 100.0f);
                         return rowA != rowB ? rowA < rowB : a->x < b->x;
                     });
    auto total = 0;
    for (const auto* control : ordered)
        total += compactSpan(control->kind);
    if (total == 0)
        return plan;
    plan.rows = total <= compactSingleRowCells ? 1 : 2;
    plan.columns = (total + plan.rows - 1) / plan.rows;
    for (;; ++plan.columns) {
        plan.cells.clear();
        auto row = 0;
        auto column = 0;
        auto fits = true;
        for (const auto* control : ordered) {
            const auto span = std::min(compactSpan(control->kind), plan.columns);
            if (column + span > plan.columns) {
                ++row;
                column = 0;
            }
            if (row >= plan.rows) {
                fits = false;
                break;
            }
            plan.cells.push_back({ control->id, row, column, span });
            column += span;
        }
        if (fits)
            return plan;
    }
}

void ModuleInterfaceEditorComponent::layoutCompactCells()
{
    compactCells_.clear();
    if (!compactMode_)
        return;
    const auto plan = compactPlan();
    if (plan.cells.empty())
        return;
    const auto canvas = canvasBounds().toFloat();
    const auto cellWidth = canvas.getWidth() / static_cast<float>(plan.columns);
    const auto cellHeight = canvas.getHeight() / static_cast<float>(plan.rows);
    for (const auto& cell : plan.cells)
        compactCells_[cell.id] = juce::Rectangle<float>(
            canvas.getX() + static_cast<float>(cell.column) * cellWidth,
            canvas.getY() + static_cast<float>(cell.row) * cellHeight,
            static_cast<float>(cell.span) * cellWidth, cellHeight)
            .reduced(2.0f, 2.0f);
}

int ModuleInterfaceEditorComponent::preferredCompactWidth() const
{
    const auto plan = compactPlan();
    return std::max(1, plan.columns) * compactCellWidth + 8;
}

ModuleInterfaceControl* ModuleInterfaceEditorComponent::findControl(const std::uint64_t id)
{
    auto& controls = activeControls();
    const auto found = std::find_if(controls.begin(), controls.end(),
                                    [id](const auto& control) { return control.id == id; });
    return found == controls.end() ? nullptr : &*found;
}

const ModuleInterfaceControl* ModuleInterfaceEditorComponent::findControl(
    const std::uint64_t id) const
{
    const auto& controls = activeControls();
    const auto found = std::find_if(controls.begin(), controls.end(),
                                    [id](const auto& control) { return control.id == id; });
    return found == controls.end() ? nullptr : &*found;
}

ModulePublishedParameter* ModuleInterfaceEditorComponent::findParameter(
    const juce::String& key)
{
    const auto found = std::find_if(state_.parameters.begin(), state_.parameters.end(),
                                    [&key](const auto& parameter) { return parameter.key == key; });
    return found == state_.parameters.end() ? nullptr : &*found;
}

const ModulePublishedParameter* ModuleInterfaceEditorComponent::findParameter(
    const juce::String& key) const
{
    const auto found = std::find_if(state_.parameters.begin(), state_.parameters.end(),
                                    [&key](const auto& parameter) { return parameter.key == key; });
    return found == state_.parameters.end() ? nullptr : &*found;
}

juce::String ModuleInterfaceEditorComponent::controlTooltip(
    const ModuleInterfaceControl& control) const
{
    juce::String tooltip = control.label;
    const auto kindKey = [&control] {
        switch (control.kind) {
            case ModuleControlKind::knob: return "module.ui.knob";
            case ModuleControlKind::fader: return "module.ui.fader";
            case ModuleControlKind::waveform: return "module.ui.waveform";
            case ModuleControlKind::spectrum: return "module.ui.spectrum";
            case ModuleControlKind::levelMeter: return "module.ui.meter";
            case ModuleControlKind::text: return "module.ui.text";
            case ModuleControlKind::inputPort: return "module.ui.inputPort";
            case ModuleControlKind::outputPort: return "module.ui.outputPort";
            case ModuleControlKind::toggle: return "module.ui.toggle";
            case ModuleControlKind::envelope: return "module.ui.envelope";
            case ModuleControlKind::group: return "module.ui.group";
            case ModuleControlKind::selector: return "module.ui.selector";
            case ModuleControlKind::xyPad: return "module.ui.xyPad";
            case ModuleControlKind::valueDisplay: return "module.ui.valueDisplay";
            case ModuleControlKind::filterResponse: return "module.ui.filterResponse";
            case ModuleControlKind::dynamicsResponse: return "module.ui.dynamicsResponse";
            case ModuleControlKind::eqCurve: return "module.ui.eqCurve";
            case ModuleControlKind::wavetable: return "module.ui.wavetable";
        }
        return "module.ui.text";
    }();
    if (tooltip.isEmpty())
        tooltip = tr(kindKey);
    else
        tooltip += "\n" + tr(kindKey);

    if (isParameterControl(control.kind)) {
        if (const auto* parameter = findParameter(control.parameterBinding)) {
            if (parameter->description.isNotEmpty())
                tooltip += "\n" + parameter->description;
            tooltip += "\n" + tr("module.ui.bindingValue", "binding",
                                    parameter->key);
            if (control.kind == ModuleControlKind::xyPad
                || control.kind == ModuleControlKind::filterResponse
                || control.kind == ModuleControlKind::dynamicsResponse) {
                const auto* secondary = findParameter(
                    control.secondaryParameterBinding);
                tooltip += "\n" + tr(
                    "module.ui.secondaryBindingValue", "binding",
                    secondary != nullptr ? secondary->key
                                         : control.secondaryParameterBinding);
                tooltip += "\n" + tr(
                    control.kind == ModuleControlKind::filterResponse
                        ? "module.ui.filterResponseGestureHelp"
                    : control.kind == ModuleControlKind::dynamicsResponse
                        ? "module.ui.dynamicsResponseGestureHelp"
                        : "module.ui.xyPadGestureHelp");
            } else if (control.kind == ModuleControlKind::envelope) {
                tooltip += "\nD: " + control.secondaryParameterBinding
                    + " | S: " + control.tertiaryParameterBinding
                    + " | R: " + control.quaternaryParameterBinding;
                tooltip += "\n" + tr("module.ui.envelopeGestureHelp");
            } else if (control.kind == ModuleControlKind::selector) {
                tooltip += "\n" + tr("module.ui.selectorGestureHelp");
            }
        }
    } else if (isPortControl(control.kind)) {
        static constexpr std::array<const char*, 4> portKeys {
            "nodes.port.audio", "nodes.port.midi",
            "nodes.port.control", "nodes.port.transport"
        };
        const auto type = control.portKind == 0
            ? tr("nodes.audioFormat." + juce::String(juce::jlimit(
                  1, 8, static_cast<int>(control.portChannels))))
            : tr(portKeys[juce::jlimit(
                  0, 3, static_cast<int>(control.portKind))]);
        tooltip += "\n" + tr("module.ui.typedPortHelp", "type",
                                type);
    } else if (control.kind == ModuleControlKind::levelMeter) {
        tooltip += "\n" + tr("module.ui.typedMeterHelp", "type",
            tr("nodes.audioFormat." + juce::String(juce::jlimit(
                1, 8, static_cast<int>(control.portChannels)))));
    } else {
        tooltip += "\n" + tr("module.ui.visualControlHelp");
    }
    if (editButton_.getToggleState())
        tooltip += "\n" + tr("module.ui.visualEditGestureHelp");
    return tooltip;
}

std::uint64_t ModuleInterfaceEditorComponent::controlAt(
    const juce::Point<float> position) const
{
    const auto& controls = activeControls();
    for (auto control = controls.rbegin(); control != controls.rend(); ++control)
        if (controlBounds(*control).contains(position))
            return control->id;
    return 0;
}

std::vector<ModuleInterfaceControl>&
ModuleInterfaceEditorComponent::activeControls() noexcept
{
    return moduleInterfaceControls(state_, layoutMode_);
}

const std::vector<ModuleInterfaceControl>&
ModuleInterfaceEditorComponent::activeControls() const noexcept
{
    return moduleInterfaceControls(state_, layoutMode_);
}

void ModuleInterfaceEditorComponent::rebuildSliders()
{
    // Sliders outlive a rebuild: a knob whose control and binding are
    // unchanged is fitted again rather than remade. Construction — the
    // Slider, its text box, their colours — was most of what a surface of
    // hundreds cost at every change of view; the fitting is cheap. What is
    // not claimed again is destroyed with this map.
    std::unordered_map<std::uint64_t, SliderEntry> keptSliders;
    for (auto& entry : sliders_)
        if (entry.slider != nullptr)
            keptSliders.insert_or_assign(entry.controlId, std::move(entry));
    sliders_.clear();
    buttons_.clear();
    combos_.clear();
    xyPads_.clear();
    envelopes_.clear();
    filterResponses_.clear();
    eqCurves_.clear();
    dynamicsResponses_.clear();
    analysisDisplays_.clear();
    for (const auto& control : activeControls())
        if (control.kind == ModuleControlKind::waveform
            || control.kind == ModuleControlKind::spectrum
            || control.kind == ModuleControlKind::levelMeter) {
            auto display = std::make_unique<ModuleAnalysisDisplayComponent>(
                control.kind,
                static_cast<std::size_t>(juce::jlimit(
                    1, 8, static_cast<int>(control.portChannels))));
            addAndMakeVisible(*display);
            analysisDisplays_.push_back({ control.id, std::move(display) });
        }
    for (const auto& control : activeControls()) {
        if (!isParameterControl(control.kind))
            continue;
        const auto* parameter = findParameter(control.parameterBinding);
        if (parameter == nullptr)
            continue;
        if (control.kind == ModuleControlKind::valueDisplay)
            continue;
        if (control.kind == ModuleControlKind::envelope) {
            const std::array bindings {
                control.parameterBinding,
                control.secondaryParameterBinding,
                control.tertiaryParameterBinding,
                control.quaternaryParameterBinding
            };
            std::array<const ModulePublishedParameter*, 4> parameters {};
            bool valid = true;
            for (std::size_t index = 0; index < bindings.size(); ++index) {
                parameters[index] = findParameter(bindings[index]);
                valid = valid && parameters[index] != nullptr;
            }
            if (!valid)
                continue;
            const auto normalise = [](const ModulePublishedParameter& value) {
                return value.maximum > value.minimum
                    ? juce::jlimit(0.0, 1.0,
                        (value.value - value.minimum)
                            / (value.maximum - value.minimum))
                    : 0.0;
            };
            auto envelope = std::make_unique<ModuleEnvelopeComponent>();
            envelope->setNormalisedValues(
                normalise(*parameters[0]), normalise(*parameters[1]),
                normalise(*parameters[2]), normalise(*parameters[3]),
                juce::dontSendNotification);
            envelope->setTooltip(controlTooltip(control));
            envelope->setInterceptsMouseClicks(
                !editButton_.getToggleState(), false);
            envelope->addMouseListener(this, true);
            envelope->onGestureStart = [this] { beginHistoryGesture(); };
            envelope->onValueChange =
                [this, bindings](const double attack, const double decay,
                                 const double sustain, const double release) {
                    const std::array normalised {
                        attack, decay, sustain, release
                    };
                    for (std::size_t index = 0; index < bindings.size(); ++index) {
                        auto* changed = findParameter(bindings[index]);
                        if (changed == nullptr)
                            continue;
                        changed->value = changed->minimum
                            + (changed->maximum - changed->minimum)
                                  * normalised[index];
                        for (auto& slider : sliders_)
                            if (slider.binding == changed->key)
                                slider.slider->setValue(
                                    changed->value,
                                    juce::dontSendNotification);
                        const auto middle = changed->minimum
                            + (changed->maximum - changed->minimum) * 0.5;
                        for (auto& button : buttons_)
                            if (button.binding == changed->key)
                                button.button->setToggleState(
                                    changed->value > middle,
                                    juce::dontSendNotification);
                        for (auto& combo : combos_)
                            if (combo.binding == changed->key)
                                combo.combo->setSelectedItemIndex(
                                    static_cast<int>(closestModuleSelectorValue(
                                        combo.values, changed->value)),
                                    juce::dontSendNotification);
                        if (parameterChangedHandler_)
                            parameterChangedHandler_(changed->key,
                                                     changed->value);
                    }
                    synchroniseXyPads();
                    synchroniseEnvelopes();
                    synchroniseFilterResponses();
                    synchroniseDynamicsResponses();
                    scheduleParameterStateCommit();
                };
            envelope->onGestureEnd = [this] {
                flushParameterStateCommit();
                endHistoryGesture();
            };
            std::array<double, 4> defaults {};
            for (std::size_t index = 0; index < parameters.size(); ++index)
                defaults[index] = parameters[index]->maximum
                        > parameters[index]->minimum
                    ? (moduleParameterDefaultValue(*parameters[index])
                       - parameters[index]->minimum)
                          / (parameters[index]->maximum
                             - parameters[index]->minimum)
                    : 0.0;
            const auto controlId = control.id;
            envelope->onReset = [this, controlId, defaults] {
                const auto found = std::ranges::find_if(
                    envelopes_, [controlId](const auto& entry) {
                        return entry.controlId == controlId;
                    });
                if (found == envelopes_.end())
                    return;
                beginHistoryGesture();
                found->envelope->setNormalisedValues(
                    defaults[0], defaults[1], defaults[2], defaults[3],
                    juce::sendNotification);
                flushParameterStateCommit();
                endHistoryGesture();
            };
            addAndMakeVisible(*envelope);
            envelopes_.push_back({ control.id, bindings,
                                   std::move(envelope) });
            continue;
        }
        if (control.kind == ModuleControlKind::eqCurve) {
            const auto& bindings = control.extraParameterBindings;
            if (bindings.empty() || bindings.size() % 3 != 0)
                continue;
            std::vector<ModuleEqCurveComponent::Band> bands;
            auto minimumFrequency = 20.0;
            auto maximumFrequency = 20'000.0;
            auto minimumGain = -30.0;
            auto maximumGain = 30.0;
            auto minimumQ = 0.1;
            auto maximumQ = 18.0;
            auto complete = true;
            for (std::size_t index = 0; index < bindings.size(); index += 3) {
                const auto* frequency = findParameter(bindings[index]);
                const auto* gain = findParameter(bindings[index + 1]);
                const auto* q = findParameter(bindings[index + 2]);
                if (frequency == nullptr || gain == nullptr || q == nullptr) {
                    complete = false;
                    break;
                }
                minimumFrequency = std::max(1.0, frequency->minimum);
                maximumFrequency = std::max(minimumFrequency + 1.0,
                                            frequency->maximum);
                minimumGain = gain->minimum;
                maximumGain = gain->maximum;
                minimumQ = std::max(0.01, q->minimum);
                maximumQ = std::max(minimumQ + 0.01, q->maximum);
                bands.push_back({ frequency->value, gain->value, q->value });
            }
            if (!complete)
                continue;
            auto curve = std::make_unique<ModuleEqCurveComponent>();
            curve->setBandRanges(minimumFrequency, maximumFrequency,
                                 minimumGain, maximumGain, minimumQ,
                                 maximumQ);
            curve->setBands(bands, juce::dontSendNotification);
            curve->setTooltip(controlTooltip(control));
            curve->setInterceptsMouseClicks(!editButton_.getToggleState(),
                                            false);
            curve->addMouseListener(this, true);
            const auto curveBindings = bindings;
            curve->onGestureStart = [this] { beginHistoryGesture(); };
            curve->onBandChange =
                [this, curveBindings](const std::size_t band,
                                      const double frequency,
                                      const double gain, const double q) {
                    if (band * 3 + 2 >= curveBindings.size())
                        return;
                    const std::array values { frequency, gain, q };
                    for (std::size_t offset = 0; offset < 3; ++offset) {
                        auto* target = findParameter(
                            curveBindings[band * 3 + offset]);
                        if (target == nullptr)
                            continue;
                        target->value = juce::jlimit(
                            target->minimum, target->maximum, values[offset]);
                        for (auto& slider : sliders_)
                            if (slider.binding == target->key)
                                slider.slider->setValue(
                                    target->value,
                                    juce::dontSendNotification);
                        if (parameterChangedHandler_)
                            parameterChangedHandler_(target->key,
                                                     target->value);
                    }
                    scheduleParameterStateCommit();
                };
            curve->onGestureEnd = [this] { endHistoryGesture(); };
            addAndMakeVisible(*curve);
            eqCurves_.push_back({ control.id, curveBindings,
                                  std::move(curve) });
            continue;
        }
        if (control.kind == ModuleControlKind::filterResponse) {
            const auto* resonance = findParameter(
                control.secondaryParameterBinding);
            if (resonance == nullptr || resonance->key == parameter->key)
                continue;
            auto response = std::make_unique<ModuleFilterResponseComponent>();
            response->setParameterRanges(
                parameter->minimum, parameter->maximum,
                resonance->minimum, resonance->maximum);
            response->setValues(parameter->value, resonance->value,
                                juce::dontSendNotification);
            response->setTooltip(controlTooltip(control));
            response->setInterceptsMouseClicks(
                !editButton_.getToggleState(), false);
            response->addMouseListener(this, true);
            const auto cutoffBinding = control.parameterBinding;
            const auto resonanceBinding = control.secondaryParameterBinding;
            response->onGestureStart = [this] { beginHistoryGesture(); };
            response->onValueChange =
                [this, cutoffBinding, resonanceBinding](
                    const double cutoff, const double q) {
                    auto* cutoffParameter = findParameter(cutoffBinding);
                    auto* resonanceParameter = findParameter(resonanceBinding);
                    if (cutoffParameter == nullptr
                        || resonanceParameter == nullptr)
                        return;
                    cutoffParameter->value = juce::jlimit(
                        cutoffParameter->minimum, cutoffParameter->maximum,
                        cutoff);
                    resonanceParameter->value = juce::jlimit(
                        resonanceParameter->minimum,
                        resonanceParameter->maximum, q);
                    for (const auto* changed : {
                             cutoffParameter, resonanceParameter }) {
                        for (auto& slider : sliders_)
                            if (slider.binding == changed->key)
                                slider.slider->setValue(
                                    changed->value,
                                    juce::dontSendNotification);
                        for (auto& combo : combos_)
                            if (combo.binding == changed->key)
                                combo.combo->setSelectedItemIndex(
                                    static_cast<int>(
                                        closestModuleSelectorValue(
                                            combo.values, changed->value)),
                                    juce::dontSendNotification);
                        if (parameterChangedHandler_)
                            parameterChangedHandler_(changed->key,
                                                     changed->value);
                    }
                    synchroniseXyPads();
                    synchroniseFilterResponses();
                    synchroniseEnvelopes();
                    synchroniseDynamicsResponses();
                    scheduleParameterStateCommit();
                };
            response->onGestureEnd = [this] {
                flushParameterStateCommit();
                endHistoryGesture();
            };
            const auto cutoffDefault = moduleParameterDefaultValue(*parameter);
            const auto resonanceDefault = moduleParameterDefaultValue(*resonance);
            const auto controlId = control.id;
            response->onReset = [this, controlId, cutoffDefault,
                                 resonanceDefault] {
                const auto found = std::ranges::find_if(
                    filterResponses_, [controlId](const auto& entry) {
                        return entry.controlId == controlId;
                    });
                if (found == filterResponses_.end())
                    return;
                beginHistoryGesture();
                found->response->setValues(cutoffDefault, resonanceDefault,
                                           juce::sendNotification);
                flushParameterStateCommit();
                endHistoryGesture();
            };
            addAndMakeVisible(*response);
            filterResponses_.push_back(
                { control.id, cutoffBinding, resonanceBinding,
                  std::move(response) });
            continue;
        }
        if (control.kind == ModuleControlKind::dynamicsResponse) {
            const auto* ratio = findParameter(
                control.secondaryParameterBinding);
            if (ratio == nullptr || ratio->key == parameter->key)
                continue;
            auto response = std::make_unique<ModuleDynamicsResponseComponent>();
            response->setParameterRanges(
                parameter->minimum, parameter->maximum,
                ratio->minimum, ratio->maximum);
            response->setValues(parameter->value, ratio->value,
                                juce::dontSendNotification);
            response->setTooltip(controlTooltip(control));
            response->setInterceptsMouseClicks(
                !editButton_.getToggleState(), false);
            response->addMouseListener(this, true);
            const auto thresholdBinding = control.parameterBinding;
            const auto ratioBinding = control.secondaryParameterBinding;
            response->onGestureStart = [this] { beginHistoryGesture(); };
            response->onValueChange =
                [this, thresholdBinding, ratioBinding](
                    const double threshold, const double ratioValue) {
                    auto* thresholdParameter = findParameter(thresholdBinding);
                    auto* ratioParameter = findParameter(ratioBinding);
                    if (thresholdParameter == nullptr
                        || ratioParameter == nullptr)
                        return;
                    thresholdParameter->value = juce::jlimit(
                        thresholdParameter->minimum,
                        thresholdParameter->maximum, threshold);
                    ratioParameter->value = juce::jlimit(
                        ratioParameter->minimum, ratioParameter->maximum,
                        ratioValue);
                    for (const auto* changed : {
                             thresholdParameter, ratioParameter }) {
                        for (auto& slider : sliders_)
                            if (slider.binding == changed->key)
                                slider.slider->setValue(
                                    changed->value,
                                    juce::dontSendNotification);
                        for (auto& combo : combos_)
                            if (combo.binding == changed->key)
                                combo.combo->setSelectedItemIndex(
                                    static_cast<int>(
                                        closestModuleSelectorValue(
                                            combo.values, changed->value)),
                                    juce::dontSendNotification);
                        if (parameterChangedHandler_)
                            parameterChangedHandler_(changed->key,
                                                     changed->value);
                    }
                    synchroniseXyPads();
                    synchroniseEnvelopes();
                    synchroniseFilterResponses();
                    synchroniseDynamicsResponses();
                    scheduleParameterStateCommit();
                };
            response->onGestureEnd = [this] {
                flushParameterStateCommit();
                endHistoryGesture();
            };
            const auto thresholdDefault = moduleParameterDefaultValue(*parameter);
            const auto ratioDefault = moduleParameterDefaultValue(*ratio);
            const auto controlId = control.id;
            response->onReset = [this, controlId, thresholdDefault,
                                 ratioDefault] {
                const auto found = std::ranges::find_if(
                    dynamicsResponses_, [controlId](const auto& entry) {
                        return entry.controlId == controlId;
                    });
                if (found == dynamicsResponses_.end())
                    return;
                beginHistoryGesture();
                found->response->setValues(thresholdDefault, ratioDefault,
                                           juce::sendNotification);
                flushParameterStateCommit();
                endHistoryGesture();
            };
            addAndMakeVisible(*response);
            dynamicsResponses_.push_back(
                { control.id, thresholdBinding, ratioBinding,
                  std::move(response) });
            continue;
        }
        if (control.kind == ModuleControlKind::xyPad) {
            const auto* secondary = findParameter(
                control.secondaryParameterBinding);
            if (secondary == nullptr
                || secondary->key == parameter->key)
                continue;
            const auto normalise = [](const ModulePublishedParameter& value) {
                return value.maximum > value.minimum
                    ? juce::jlimit(0.0, 1.0,
                        (value.value - value.minimum)
                            / (value.maximum - value.minimum))
                    : 0.0;
            };
            auto pad = std::make_unique<ModuleXyPadComponent>();
            pad->setNormalisedValues(normalise(*parameter),
                                     normalise(*secondary),
                                     juce::dontSendNotification);
            pad->setTooltip(controlTooltip(control));
            pad->setInterceptsMouseClicks(
                !editButton_.getToggleState(), false);
            pad->addMouseListener(this, true);
            const auto xBinding = control.parameterBinding;
            const auto yBinding = control.secondaryParameterBinding;
            pad->onGestureStart = [this] { beginHistoryGesture(); };
            pad->onValueChange =
                [this, xBinding, yBinding](const double x,
                                           const double y) {
                    auto* xParameter = findParameter(xBinding);
                    auto* yParameter = findParameter(yBinding);
                    if (xParameter == nullptr || yParameter == nullptr)
                        return;
                    xParameter->value = xParameter->minimum
                        + (xParameter->maximum - xParameter->minimum) * x;
                    yParameter->value = yParameter->minimum
                        + (yParameter->maximum - yParameter->minimum) * y;
                    for (const auto* changed : { xParameter, yParameter }) {
                        for (auto& slider : sliders_)
                            if (slider.binding == changed->key)
                                slider.slider->setValue(
                                    changed->value,
                                    juce::dontSendNotification);
                        const auto middle = changed->minimum
                            + (changed->maximum - changed->minimum) * 0.5;
                        for (auto& button : buttons_)
                            if (button.binding == changed->key)
                                button.button->setToggleState(
                                    changed->value > middle,
                                    juce::dontSendNotification);
                        for (auto& combo : combos_)
                            if (combo.binding == changed->key)
                                combo.combo->setSelectedItemIndex(
                                    static_cast<int>(
                                        closestModuleSelectorValue(
                                            combo.values, changed->value)),
                                    juce::dontSendNotification);
                        if (parameterChangedHandler_)
                            parameterChangedHandler_(changed->key,
                                                     changed->value);
                    }
                    synchroniseXyPads();
                    scheduleParameterStateCommit();
                };
            pad->onGestureEnd = [this] {
                flushParameterStateCommit();
                endHistoryGesture();
            };
            const auto xDefault = moduleParameterDefaultValue(*parameter);
            const auto yDefault = moduleParameterDefaultValue(*secondary);
            pad->onReset = [this, xBinding, yBinding, xDefault, yDefault] {
                auto* xParameter = findParameter(xBinding);
                auto* yParameter = findParameter(yBinding);
                if (xParameter == nullptr || yParameter == nullptr)
                    return;
                beginHistoryGesture();
                const auto x = xParameter->maximum > xParameter->minimum
                    ? (xDefault - xParameter->minimum)
                          / (xParameter->maximum - xParameter->minimum)
                    : 0.0;
                const auto y = yParameter->maximum > yParameter->minimum
                    ? (yDefault - yParameter->minimum)
                          / (yParameter->maximum - yParameter->minimum)
                    : 0.0;
                const auto found = std::ranges::find_if(
                    xyPads_, [&xBinding, &yBinding](const auto& entry) {
                        return entry.xBinding == xBinding
                               && entry.yBinding == yBinding;
                    });
                if (found != xyPads_.end())
                    found->pad->setNormalisedValues(
                        x, y, juce::sendNotification);
                flushParameterStateCommit();
                endHistoryGesture();
            };
            addAndMakeVisible(*pad);
            xyPads_.push_back({ control.id, xBinding, yBinding,
                                std::move(pad) });
            continue;
        }
        if (control.kind == ModuleControlKind::toggle) {
            auto button = std::make_unique<juce::ToggleButton>();
            const auto middle = parameter->minimum
                                + (parameter->maximum - parameter->minimum)
                                      * 0.5;
            button->setToggleState(parameter->value > middle,
                                   juce::dontSendNotification);
            button->setTooltip(controlTooltip(control));
            button->setInterceptsMouseClicks(
                !editButton_.getToggleState(), false);
            const auto binding = control.parameterBinding;
            button->onClick = [this, binding, buttonPointer = button.get()] {
                auto* changed = findParameter(binding);
                if (changed == nullptr)
                    return;
                changed->value = buttonPointer->getToggleState()
                                     ? changed->maximum : changed->minimum;
                for (auto& slider : sliders_)
                    if (slider.binding == binding)
                        slider.slider->setValue(changed->value,
                                                juce::dontSendNotification);
                for (auto& combo : combos_)
                    if (combo.binding == binding)
                        combo.combo->setSelectedItemIndex(
                            static_cast<int>(closestModuleSelectorValue(
                                combo.values, changed->value)),
                            juce::dontSendNotification);
                synchroniseXyPads(binding);
                synchroniseEnvelopes(binding);
                synchroniseFilterResponses(binding);
                synchroniseDynamicsResponses(binding);
                if (parameterChangedHandler_)
                    parameterChangedHandler_(binding, changed->value);
                scheduleParameterStateCommit();
            };
            addAndMakeVisible(*button);
            buttons_.push_back(
                { control.id, control.parameterBinding, std::move(button) });
            continue;
        }
        if (control.kind == ModuleControlKind::selector) {
            auto combo = std::make_unique<juce::ComboBox>();
            auto values = moduleSelectorValues(*parameter);
            for (std::size_t index = 0; index < values.size(); ++index)
                combo->addItem(moduleSelectorValueLabel(*parameter, values[index]),
                               static_cast<int>(index + 1));
            combo->setSelectedItemIndex(
                static_cast<int>(closestModuleSelectorValue(
                    values, parameter->value)),
                juce::dontSendNotification);
            combo->setTooltip(controlTooltip(control));
            combo->setInterceptsMouseClicks(
                !editButton_.getToggleState(), false);
            combo->addMouseListener(this, true);
            const auto binding = control.parameterBinding;
            combo->onChange = [this, binding, comboPointer = combo.get(),
                               valuesCopy = values] {
                auto* changed = findParameter(binding);
                const auto selected = comboPointer->getSelectedItemIndex();
                if (changed == nullptr || selected < 0
                    || static_cast<std::size_t>(selected) >= valuesCopy.size())
                    return;
                changed->value = valuesCopy[static_cast<std::size_t>(selected)];
                for (auto& slider : sliders_)
                    if (slider.binding == binding)
                        slider.slider->setValue(changed->value,
                                                juce::dontSendNotification);
                const auto middle = changed->minimum
                    + (changed->maximum - changed->minimum) * 0.5;
                for (auto& button : buttons_)
                    if (button.binding == binding)
                        button.button->setToggleState(changed->value > middle,
                                                      juce::dontSendNotification);
                synchroniseXyPads(binding);
                synchroniseEnvelopes(binding);
                synchroniseFilterResponses(binding);
                synchroniseDynamicsResponses(binding);
                if (parameterChangedHandler_)
                    parameterChangedHandler_(binding, changed->value);
                scheduleParameterStateCommit();
            };
            addAndMakeVisible(*combo);
            combos_.push_back({ control.id, control.parameterBinding,
                                std::move(values), std::move(combo) });
            continue;
        }
        std::unique_ptr<juce::Slider> slider;
        const auto wantsKnob = control.kind == ModuleControlKind::knob;
        if (const auto kept = keptSliders.find(control.id);
            kept != keptSliders.end() && kept->second.slider != nullptr
            && kept->second.binding == control.parameterBinding
            && (dynamic_cast<KnobSlider*>(kept->second.slider.get()) != nullptr)
                   == wantsKnob)
            slider = std::move(kept->second.slider);
        if (slider == nullptr)
            slider = wantsKnob
                         ? std::unique_ptr<juce::Slider>(
                               std::make_unique<KnobSlider>())
                         : std::make_unique<juce::Slider>(
                               juce::Slider::LinearVertical,
                               juce::Slider::NoTextBox);
        if (auto* knob = dynamic_cast<KnobSlider*>(slider.get())) {
            knob->setDefaultValue(moduleParameterDefaultValue(*parameter));
            knob->setModulationRange(std::nullopt);
        }
        // Every fitting below is set afresh; the skew is the one a kept
        // slider would otherwise carry over.
        slider->setSkewFactor(1.0);
        slider->setRange(parameter->minimum, parameter->maximum, 0.0);
        slider->setNumDecimalPlacesToDisplay(
            parameter->unit == "Hz" || parameter->unit == "ms"
                || parameter->unit == "dB"
                ? 1
                : (parameter->maximum <= 1.0 && parameter->minimum >= -1.0
                       ? 3
                       : 2));
        if (parameter->minimum > 0.0 && parameter->maximum / parameter->minimum > 100.0)
            slider->setSkewFactorFromMidPoint(
                std::sqrt(parameter->minimum * parameter->maximum));
        slider->setValue(parameter->value, juce::dontSendNotification);
        slider->setDoubleClickReturnValue(
            true, moduleParameterDefaultValue(*parameter));
        slider->setTextValueSuffix(parameter->unit.isNotEmpty()
                                       ? " " + parameter->unit : juce::String {});
        const auto span = parameter->maximum - parameter->minimum;
        const auto decimals = span <= 4.0 ? 3 : span <= 100.0 ? 2 : 1;
        const auto formatted = [parameter, decimals](const double value) {
            return juce::String(value, decimals)
                + (parameter->unit.isNotEmpty()
                       ? " " + parameter->unit : juce::String {});
        };
        auto tooltip = control.label.isNotEmpty() ? control.label : parameter->label;
        if (parameter->description.isNotEmpty())
            tooltip += "\n" + parameter->description;
        tooltip += "\n" + tr("nodes.parameter.tooltipCurrent", "value",
                                formatted(parameter->value));
        tooltip += "\n"
            + tr("nodes.parameter.tooltipRange", "minimum",
                 formatted(parameter->minimum))
                  .replace("{maximum}", formatted(parameter->maximum));
        tooltip += "\n" + tr("nodes.parameter.tooltipDefault", "value",
                                formatted(moduleParameterDefaultValue(*parameter)));
        tooltip += "\n" + tr("module.ui.parameterCapabilities", "device",
                                parameter->showInDevice ? tr("common.yes")
                                                        : tr("common.no"))
                  .replace("{host}", parameter->publishToHost ? tr("common.yes")
                                                               : tr("common.no"))
                  .replace("{automation}", parameter->automatable ? tr("common.yes")
                                                                   : tr("common.no"))
                  .replace("{modulation}", parameter->modulatable ? tr("common.yes")
                                                                   : tr("common.no"));
        tooltip += "\n" + tr("module.ui.parameterGestureHelp");
        slider->setTooltip(tooltip);
        slider->setColour(juce::Slider::rotarySliderFillColourId,
                          juce::Colour(0xffff8a00));
        slider->setColour(juce::Slider::trackColourId,
                          juce::Colour(0xffff8a00));
        // The text box last: a Slider remakes its text box — a Label, its
        // colours looked up one by one — on every colour it is given, so
        // the colours go in while it has none.
        slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 76, 18);
        slider->setInterceptsMouseClicks(!editButton_.getToggleState(), false);
        slider->addMouseListener(this, true);
        const auto controlId = control.id;
        const auto binding = control.parameterBinding;
        slider->onValueChange = [this, controlId, binding, sliderPtr = slider.get()] {
            juce::ignoreUnused(controlId);
            if (auto* parameterValue = findParameter(binding)) {
                parameterValue->value = sliderPtr->getValue();
                const auto middle = parameterValue->minimum
                    + (parameterValue->maximum - parameterValue->minimum) * 0.5;
                for (auto& button : buttons_)
                    if (button.binding == binding)
                        button.button->setToggleState(
                            parameterValue->value > middle,
                            juce::dontSendNotification);
                for (auto& combo : combos_)
                    if (combo.binding == binding)
                        combo.combo->setSelectedItemIndex(
                            static_cast<int>(closestModuleSelectorValue(
                                combo.values, parameterValue->value)),
                            juce::dontSendNotification);
                synchroniseXyPads(binding);
                synchroniseEnvelopes(binding);
                synchroniseFilterResponses(binding);
                synchroniseDynamicsResponses(binding);
                if (parameterChangedHandler_)
                    parameterChangedHandler_(binding, parameterValue->value);
                scheduleParameterStateCommit();
            }
        };
        slider->onDragStart = [this] { beginHistoryGesture(); };
        slider->onDragEnd = [this] {
            flushParameterStateCommit();
            endHistoryGesture();
        };
        addAndMakeVisible(*slider);
        sliders_.push_back(
            { control.id, control.parameterBinding, std::move(slider) });
    }
    resized();
}

void ModuleInterfaceEditorComponent::synchroniseXyPads(
    const juce::String& binding)
{
    for (auto& entry : xyPads_) {
        if (binding.isNotEmpty() && entry.xBinding != binding
            && entry.yBinding != binding)
            continue;
        const auto* xParameter = findParameter(entry.xBinding);
        const auto* yParameter = findParameter(entry.yBinding);
        if (xParameter == nullptr || yParameter == nullptr)
            continue;
        const auto normalise = [](const ModulePublishedParameter& value) {
            return value.maximum > value.minimum
                ? juce::jlimit(0.0, 1.0,
                    (value.value - value.minimum)
                        / (value.maximum - value.minimum))
                : 0.0;
        };
        entry.pad->setNormalisedValues(
            normalise(*xParameter), normalise(*yParameter),
            juce::dontSendNotification);
    }
}

void ModuleInterfaceEditorComponent::synchroniseEnvelopes(
    const juce::String& binding)
{
    const auto normalise = [](const ModulePublishedParameter& value) {
        return value.maximum > value.minimum
            ? juce::jlimit(0.0, 1.0,
                (value.value - value.minimum)
                    / (value.maximum - value.minimum))
            : 0.0;
    };
    for (auto& entry : envelopes_) {
        if (binding.isNotEmpty()
            && std::ranges::find(entry.bindings, binding)
                   == entry.bindings.end())
            continue;
        std::array<const ModulePublishedParameter*, 4> parameters {};
        bool valid = true;
        for (std::size_t index = 0; index < entry.bindings.size(); ++index) {
            parameters[index] = findParameter(entry.bindings[index]);
            valid = valid && parameters[index] != nullptr;
        }
        if (!valid)
            continue;
        entry.envelope->setNormalisedValues(
            normalise(*parameters[0]), normalise(*parameters[1]),
            normalise(*parameters[2]), normalise(*parameters[3]),
            juce::dontSendNotification);
    }
}

void ModuleInterfaceEditorComponent::synchroniseEqCurves(
    const juce::String& binding)
{
    for (auto& entry : eqCurves_) {
        if (binding.isNotEmpty()
            && std::find(entry.bindings.begin(), entry.bindings.end(), binding)
                   == entry.bindings.end())
            continue;
        std::vector<ModuleEqCurveComponent::Band> bands;
        auto complete = true;
        for (std::size_t index = 0; index + 2 < entry.bindings.size();
             index += 3) {
            const auto* frequency = findParameter(entry.bindings[index]);
            const auto* gain = findParameter(entry.bindings[index + 1]);
            const auto* q = findParameter(entry.bindings[index + 2]);
            if (frequency == nullptr || gain == nullptr || q == nullptr) {
                complete = false;
                break;
            }
            bands.push_back({ frequency->value, gain->value, q->value });
        }
        if (complete)
            entry.curve->setBands(std::move(bands),
                                  juce::dontSendNotification);
    }
}

void ModuleInterfaceEditorComponent::synchroniseFilterResponses(
    const juce::String& binding)
{
    for (auto& entry : filterResponses_) {
        if (binding.isNotEmpty() && entry.cutoffBinding != binding
            && entry.resonanceBinding != binding)
            continue;
        const auto* cutoff = findParameter(entry.cutoffBinding);
        const auto* resonance = findParameter(entry.resonanceBinding);
        if (cutoff == nullptr || resonance == nullptr)
            continue;
        entry.response->setParameterRanges(
            cutoff->minimum, cutoff->maximum,
            resonance->minimum, resonance->maximum);
        entry.response->setValues(cutoff->value, resonance->value,
                                  juce::dontSendNotification);
    }
}

void ModuleInterfaceEditorComponent::synchroniseDynamicsResponses(
    const juce::String& binding)
{
    for (auto& entry : dynamicsResponses_) {
        if (binding.isNotEmpty() && entry.thresholdBinding != binding
            && entry.ratioBinding != binding)
            continue;
        const auto* threshold = findParameter(entry.thresholdBinding);
        const auto* ratio = findParameter(entry.ratioBinding);
        if (threshold == nullptr || ratio == nullptr)
            continue;
        entry.response->setParameterRanges(
            threshold->minimum, threshold->maximum,
            ratio->minimum, ratio->maximum);
        entry.response->setValues(threshold->value, ratio->value,
                                  juce::dontSendNotification);
    }
}

void ModuleInterfaceEditorComponent::addControl(const ModuleControlKind kind,
                                                 const juce::String& binding,
                                                 const juce::String& label,
                                                 const std::uint8_t portKind,
                                                 const std::uint8_t portChannels,
                                                 const juce::String& secondaryBinding,
                                                 const juce::String& tertiaryBinding,
                                                 const juce::String& quaternaryBinding)
{
    auto& controls = activeControls();
    const auto index = controls.size();
    const auto column = static_cast<float>(index % 4);
    const auto row = static_cast<float>((index / 4) % 3);
    const auto wide = kind == ModuleControlKind::waveform
                      || kind == ModuleControlKind::spectrum
                      || kind == ModuleControlKind::envelope
                      || kind == ModuleControlKind::group
                      || kind == ModuleControlKind::xyPad
                      || kind == ModuleControlKind::valueDisplay
                      || kind == ModuleControlKind::filterResponse
                      || kind == ModuleControlKind::dynamicsResponse;
    const auto port = kind == ModuleControlKind::inputPort
                      || kind == ModuleControlKind::outputPort;
    ModuleInterfaceControl control;
    control.id = state_.nextControlId++;
    control.kind = kind;
    control.parameterBinding = binding;
    control.secondaryParameterBinding = secondaryBinding;
    control.tertiaryParameterBinding = tertiaryBinding;
    control.quaternaryParameterBinding = quaternaryBinding;
    control.label = label;
    control.portKind = portKind;
    control.portChannels = portKind == 0
        ? static_cast<std::uint8_t>(juce::jlimit(
              1, 8, static_cast<int>(portChannels)))
        : std::uint8_t { 1 };
    control.x = 0.04f + column * 0.24f;
    control.y = 0.05f + row * 0.31f;
    control.width = port ? 0.13f : (wide ? 0.44f : 0.20f);
    control.height = port ? 0.17f
                          : (kind == ModuleControlKind::xyPad
                             || kind == ModuleControlKind::filterResponse
                             || kind == ModuleControlKind::dynamicsResponse
                                 ? 0.34f
                          : (kind == ModuleControlKind::text
                             || kind == ModuleControlKind::toggle
                             || kind == ModuleControlKind::selector
                             || kind == ModuleControlKind::valueDisplay
                                 ? 0.16f : 0.25f));
    if (kind == ModuleControlKind::inputPort)
        control.x = 0.0f;
    else if (kind == ModuleControlKind::outputPort)
        control.x = 1.0f - control.width;
    control.x = std::min(control.x, 1.0f - control.width);
    control.y = std::min(control.y, 1.0f - control.height);
    controls.push_back(std::move(control));
    selectedControlId_ = controls.back().id;
    rebuildSliders();
    notifyStateChanged();
    repaint();
}

void ModuleInterfaceEditorComponent::showAddMenu()
{
    juce::PopupMenu menu;
    juce::PopupMenu parameterControls;
    auto itemId = 100;
    for (const auto& parameter : state_.parameters) {
        juce::PopupMenu controlKinds;
        controlKinds.addItem(itemId++, tr("module.ui.knob"));
        controlKinds.addItem(itemId++, tr("module.ui.fader"));
        controlKinds.addItem(itemId++, tr("module.ui.toggle"));
        controlKinds.addItem(itemId++, tr("module.ui.selector"));
        parameterControls.addSubMenu(parameter.label, controlKinds);
    }
    if (!state_.parameters.empty())
        menu.addSubMenu(tr("module.ui.parameterControls"), parameterControls);
    if (state_.parameters.size() >= 2)
        menu.addItem(80, tr("module.ui.addXyPad"));
    if (state_.parameters.size() >= 2)
        menu.addItem(81, tr("module.ui.addFilterResponse"));
    if (state_.parameters.size() >= 2)
        menu.addItem(82, tr("module.ui.addDynamicsResponse"));

    juce::PopupMenu visualisers;
    for (std::size_t index = 0; index < state_.parameters.size(); ++index)
        visualisers.addItem(
            6000 + static_cast<int>(index),
            tr("module.ui.addValueDisplay", "name",
               state_.parameters[index].label));
    if (!state_.parameters.empty())
        visualisers.addSeparator();
    visualisers.addItem(1000, tr("module.ui.waveform"));
    visualisers.addItem(1001, tr("module.ui.spectrum"));
    visualisers.addItem(1002, tr("module.ui.meter"));
    visualisers.addItem(1004, tr("module.ui.envelope"),
                        state_.parameters.size() >= 4);
    menu.addSubMenu(tr("module.ui.visualisers"), visualisers);

    juce::PopupMenu layoutElements;
    layoutElements.addItem(1003, tr("module.ui.text"));
    layoutElements.addItem(1005, tr("module.ui.group"));
    menu.addSubMenu(tr("module.ui.layoutElements"), layoutElements);

    juce::PopupMenu ports;
    juce::PopupMenu audioInputs;
    juce::PopupMenu audioOutputs;
    for (int channels = 1; channels <= 8; ++channels) {
        audioInputs.addItem(1200 + channels - 1,
                            tr("nodes.audioFormat." + juce::String(channels)));
        audioOutputs.addItem(1210 + channels - 1,
                             tr("nodes.audioFormat." + juce::String(channels)));
    }
    ports.addSubMenu(tr("module.ui.portAudioInput"), audioInputs);
    ports.addSubMenu(tr("module.ui.portAudioOutput"), audioOutputs);
    ports.addItem(1102, tr("module.ui.portMidiInput"));
    ports.addItem(1103, tr("module.ui.portMidiOutput"));
    ports.addItem(1104, tr("module.ui.portValueInput"));
    ports.addItem(1105, tr("module.ui.portValueOutput"));
    ports.addItem(1106, tr("module.ui.portTransportInput"));
    ports.addItem(1107, tr("module.ui.portTransportOutput"));
    menu.addSubMenu(tr("module.ui.ports"), ports);
    juce::Component::SafePointer<ModuleInterfaceEditorComponent> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&addButton_),
                       [safeThis](const int result) {
        if (safeThis == nullptr || result == 0)
            return;
        if (result == 80 && safeThis->state_.parameters.size() >= 2) {
            const auto& x = safeThis->state_.parameters[0];
            const auto& y = safeThis->state_.parameters[1];
            safeThis->addControl(ModuleControlKind::xyPad, x.key,
                                 tr("module.ui.xyPad"), 0, 1, y.key);
        } else if (result == 81
                   && safeThis->state_.parameters.size() >= 2) {
            const auto cutoff = std::ranges::find_if(
                safeThis->state_.parameters, [](const auto& parameter) {
                    return parameter.key.containsIgnoreCase("cutoff")
                        || parameter.unit == "Hz";
                });
            const auto resonance = std::ranges::find_if(
                safeThis->state_.parameters, [](const auto& parameter) {
                    return parameter.key.containsIgnoreCase("resonance")
                        || parameter.key.endsWithIgnoreCase(".q")
                        || parameter.label.equalsIgnoreCase("q");
                });
            const auto& first = cutoff != safeThis->state_.parameters.end()
                ? *cutoff : safeThis->state_.parameters[0];
            const auto fallback = std::ranges::find_if(
                safeThis->state_.parameters, [&first](const auto& parameter) {
                    return parameter.key != first.key;
                });
            const auto& second = resonance != safeThis->state_.parameters.end()
                    && resonance->key != first.key
                ? *resonance : *fallback;
            safeThis->addControl(ModuleControlKind::filterResponse,
                                 first.key,
                                 tr("module.ui.filterResponse"), 0, 1,
                                 second.key);
        } else if (result == 82
                   && safeThis->state_.parameters.size() >= 2) {
            const auto threshold = std::ranges::find_if(
                safeThis->state_.parameters, [](const auto& parameter) {
                    return parameter.key.containsIgnoreCase("threshold")
                        || parameter.label.containsIgnoreCase("threshold")
                        || parameter.label.containsIgnoreCase("seuil");
                });
            const auto ratio = std::ranges::find_if(
                safeThis->state_.parameters, [](const auto& parameter) {
                    return parameter.key.containsIgnoreCase("ratio")
                        || parameter.label.containsIgnoreCase("ratio");
                });
            const auto& first = threshold != safeThis->state_.parameters.end()
                ? *threshold : safeThis->state_.parameters[0];
            const auto fallback = std::ranges::find_if(
                safeThis->state_.parameters, [&first](const auto& parameter) {
                    return parameter.key != first.key;
                });
            const auto& second = ratio != safeThis->state_.parameters.end()
                    && ratio->key != first.key
                ? *ratio : *fallback;
            safeThis->addControl(ModuleControlKind::dynamicsResponse,
                                 first.key,
                                 tr("module.ui.dynamicsResponse"), 0, 1,
                                 second.key);
        } else if (result >= 6000) {
            const auto parameterIndex = static_cast<std::size_t>(result - 6000);
            if (parameterIndex < safeThis->state_.parameters.size()) {
                const auto& parameter = safeThis->state_.parameters[parameterIndex];
                safeThis->addControl(ModuleControlKind::valueDisplay,
                                     parameter.key, parameter.label);
            }
        } else if (result >= 100 && result < 1000) {
            const auto parameterIndex = static_cast<std::size_t>((result - 100) / 4);
            if (parameterIndex < safeThis->state_.parameters.size()) {
                const auto& parameter = safeThis->state_.parameters[parameterIndex];
                const auto requested = (result - 100) % 4;
                safeThis->addControl(requested == 0
                                         ? ModuleControlKind::knob
                                     : requested == 1
                                         ? ModuleControlKind::fader
                                     : requested == 2
                                         ? ModuleControlKind::toggle
                                         : ModuleControlKind::selector,
                                     parameter.key, parameter.label);
            }
        } else if (result == 1000) {
            safeThis->addControl(ModuleControlKind::waveform, {},
                                 tr("module.ui.waveform"));
        } else if (result == 1001) {
            safeThis->addControl(ModuleControlKind::spectrum, {},
                                 tr("module.ui.spectrum"));
        } else if (result == 1002) {
            safeThis->addControl(ModuleControlKind::levelMeter, {},
                                 tr("module.ui.meter"), 0, 2);
        } else if (result == 1003) {
            safeThis->addControl(ModuleControlKind::text, {},
                                 tr("module.ui.textDefault"));
        } else if (result == 1004
                   && safeThis->state_.parameters.size() >= 4) {
            safeThis->addControl(
                ModuleControlKind::envelope,
                safeThis->state_.parameters[0].key,
                tr("module.ui.envelope"), 0, 1,
                safeThis->state_.parameters[1].key,
                safeThis->state_.parameters[2].key,
                safeThis->state_.parameters[3].key);
        } else if (result == 1005) {
            safeThis->addControl(ModuleControlKind::group, {},
                                 tr("module.ui.groupDefault"));
        } else if ((result >= 1200 && result <= 1207)
                   || (result >= 1210 && result <= 1217)) {
            const auto input = result < 1210;
            const auto channels = static_cast<std::uint8_t>(
                input ? result - 1199 : result - 1209);
            if (safeThis->portCreationHandler_)
                safeThis->portCreationHandler_(input, 0, channels);
            else
                safeThis->addControl(
                    input ? ModuleControlKind::inputPort
                          : ModuleControlKind::outputPort,
                    {}, input ? tr("module.ui.inputPort")
                              : tr("module.ui.outputPort"),
                    0, channels);
        } else if (result >= 1102 && result <= 1107) {
            const auto offset = result - 1100;
            const auto input = offset % 2 == 0;
            const auto portKind = static_cast<std::uint8_t>(offset / 2);
            if (safeThis->portCreationHandler_) {
                safeThis->portCreationHandler_(input, portKind, 1);
            } else {
                safeThis->addControl(
                    input ? ModuleControlKind::inputPort
                          : ModuleControlKind::outputPort,
                    {}, input ? tr("module.ui.inputPort")
                              : tr("module.ui.outputPort"),
                    portKind, 1);
            }
        }
    });
}

void ModuleInterfaceEditorComponent::showGridMenu()
{
    struct GridPreset {
        int columns;
        int rows;
    };
    constexpr std::array presets {
        GridPreset { 8, 6 }, GridPreset { 12, 8 }, GridPreset { 16, 12 },
        GridPreset { 24, 16 }, GridPreset { 32, 24 }, GridPreset { 48, 32 },
        GridPreset { 64, 48 }
    };
    juce::PopupMenu menu;
    for (std::size_t index = 0; index < presets.size(); ++index) {
        const auto& preset = presets[index];
        menu.addItem(4000 + static_cast<int>(index),
                     juce::String(preset.columns) + " x "
                         + juce::String(preset.rows),
                     true,
                     state_.gridColumns == preset.columns
                         && state_.gridRows == preset.rows);
    }
    juce::Component::SafePointer<ModuleInterfaceEditorComponent> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&gridButton_),
                       [safeThis, presets](const int result) {
        if (safeThis == nullptr || result < 4000
            || result >= 4000 + static_cast<int>(presets.size()))
            return;
        const auto& preset = presets[static_cast<std::size_t>(result - 4000)];
        safeThis->state_.gridColumns = static_cast<std::uint8_t>(preset.columns);
        safeThis->state_.gridRows = static_cast<std::uint8_t>(preset.rows);
        safeThis->refreshTranslations();
        safeThis->notifyStateChanged();
        safeThis->repaint();
    });
}

void ModuleInterfaceEditorComponent::showControlMenu(const std::uint64_t controlId)
{
    const auto* control = findControl(controlId);
    if (control == nullptr)
        return;
    juce::PopupMenu menu;
    juce::PopupMenu parameterKinds;
    parameterKinds.addItem(1, tr("module.ui.knob"), true,
                           control->kind == ModuleControlKind::knob);
    parameterKinds.addItem(2, tr("module.ui.fader"), true,
                           control->kind == ModuleControlKind::fader);
    parameterKinds.addItem(9, tr("module.ui.toggle"), true,
                           control->kind == ModuleControlKind::toggle);
    parameterKinds.addItem(12, tr("module.ui.selector"), true,
                           control->kind == ModuleControlKind::selector);
    parameterKinds.addItem(13, tr("module.ui.xyPad"),
                           state_.parameters.size() >= 2,
                           control->kind == ModuleControlKind::xyPad);
    menu.addSubMenu(tr("module.ui.parameterControls"), parameterKinds);

    juce::PopupMenu visualKinds;
    visualKinds.addItem(3, tr("module.ui.waveform"), true,
                        control->kind == ModuleControlKind::waveform);
    visualKinds.addItem(4, tr("module.ui.spectrum"), true,
                        control->kind == ModuleControlKind::spectrum);
    visualKinds.addItem(5, tr("module.ui.meter"), true,
                        control->kind == ModuleControlKind::levelMeter);
    visualKinds.addItem(10, tr("module.ui.envelope"),
                        state_.parameters.size() >= 4,
                        control->kind == ModuleControlKind::envelope);
    visualKinds.addItem(14, tr("module.ui.valueDisplay"),
                        !state_.parameters.empty(),
                        control->kind == ModuleControlKind::valueDisplay);
    visualKinds.addItem(15, tr("module.ui.filterResponse"),
                        state_.parameters.size() >= 2,
                        control->kind == ModuleControlKind::filterResponse);
    visualKinds.addItem(16, tr("module.ui.dynamicsResponse"),
                        state_.parameters.size() >= 2,
                        control->kind == ModuleControlKind::dynamicsResponse);
    menu.addSubMenu(tr("module.ui.visualisers"), visualKinds);

    juce::PopupMenu layoutKinds;
    layoutKinds.addItem(6, tr("module.ui.text"), true,
                        control->kind == ModuleControlKind::text);
    layoutKinds.addItem(11, tr("module.ui.group"), true,
                        control->kind == ModuleControlKind::group);
    menu.addSubMenu(tr("module.ui.layoutElements"), layoutKinds);

    juce::PopupMenu portKinds;
    portKinds.addItem(7, tr("module.ui.inputPort"), true,
                      control->kind == ModuleControlKind::inputPort);
    portKinds.addItem(8, tr("module.ui.outputPort"), true,
                      control->kind == ModuleControlKind::outputPort);
    menu.addSubMenu(tr("module.ui.ports"), portKinds);
    if (!state_.parameters.empty()) {
        juce::PopupMenu bindings;
        for (std::size_t index = 0; index < state_.parameters.size(); ++index)
            bindings.addItem(100 + static_cast<int>(index), state_.parameters[index].label,
                             true, control->parameterBinding == state_.parameters[index].key);
        menu.addSubMenu(tr("module.ui.binding"), bindings);
        if (control->kind == ModuleControlKind::xyPad
            || control->kind == ModuleControlKind::filterResponse
            || control->kind == ModuleControlKind::dynamicsResponse) {
            juce::PopupMenu secondaryBindings;
            for (std::size_t index = 0; index < state_.parameters.size(); ++index)
                secondaryBindings.addItem(
                    5000 + static_cast<int>(index),
                    state_.parameters[index].label,
                    state_.parameters[index].key != control->parameterBinding,
                    control->secondaryParameterBinding
                        == state_.parameters[index].key);
            menu.addSubMenu(tr("module.ui.secondaryBinding"),
                            secondaryBindings);
        }
    }
    if (isPortControl(control->kind)) {
        const auto authoredBoundary = control->parameterBinding.isNotEmpty();
        juce::PopupMenu kinds;
        kinds.addItem(2000, tr("nodes.port.audio"), !authoredBoundary,
                      control->portKind == 0);
        kinds.addItem(2001, tr("nodes.port.midi"), !authoredBoundary,
                      control->portKind == 1);
        kinds.addItem(2002, tr("nodes.port.control"), !authoredBoundary,
                      control->portKind == 2);
        kinds.addItem(2003, tr("nodes.port.transport"), !authoredBoundary,
                      control->portKind == 3);
        menu.addSubMenu(tr("module.ui.portType"), kinds);
        if (control->portKind == 0) {
            juce::PopupMenu formats;
            for (int channels = 1; channels <= 8; ++channels)
                formats.addItem(
                    2100 + channels,
                    tr("nodes.audioFormat." + juce::String(channels)),
                    !authoredBoundary,
                    control->portChannels == channels);
            menu.addSubMenu(tr("nodes.audioFormat"), formats);
        }
    } else if (control->kind == ModuleControlKind::levelMeter) {
        juce::PopupMenu formats;
        for (int channels = 1; channels <= 8; ++channels)
            formats.addItem(
                2100 + channels,
                tr("nodes.audioFormat." + juce::String(channels)), true,
                control->portChannels == channels);
        menu.addSubMenu(tr("nodes.audioFormat"), formats);
    }
    if (isParameterControl(control->kind)) {
        if (const auto* parameter = findParameter(control->parameterBinding)) {
            menu.addSeparator();
            menu.addItem(3000, tr("module.ui.showInDevice"), true,
                         parameter->showInDevice);
            menu.addItem(3001, tr("module.ui.publishToHost"), true,
                         parameter->publishToHost);
            menu.addItem(3002, tr("module.ui.automatable"),
                         parameter->publishToHost, parameter->automatable);
            menu.addItem(3003, tr("module.ui.modulatable"), true,
                         parameter->modulatable);
        }
    }
    menu.addSeparator();
    menu.addItem(3100, tr("module.ui.controlProperties"),
                 layoutMode_ == ModuleInterfaceLayoutMode::graph);
    menu.addItem(3101, tr("module.ui.duplicateControl"),
                 layoutMode_ == ModuleInterfaceLayoutMode::graph);
    juce::PopupMenu orderMenu;
    orderMenu.addItem(3102, tr("module.ui.bringToFront"),
                      layoutMode_ == ModuleInterfaceLayoutMode::graph);
    orderMenu.addItem(3103, tr("module.ui.sendToBack"),
                      layoutMode_ == ModuleInterfaceLayoutMode::graph);
    menu.addSubMenu(tr("module.ui.controlOrder"), orderMenu,
                    layoutMode_ == ModuleInterfaceLayoutMode::graph);
    menu.addItem(999, tr("module.ui.delete"));
    juce::Component::SafePointer<ModuleInterfaceEditorComponent> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                       [safeThis, controlId](const int result) {
        if (safeThis == nullptr || result == 0)
            return;
        auto* selected = safeThis->findControl(controlId);
        if (selected == nullptr)
            return;
        if (result >= 1 && result <= 16) {
            const std::array kinds {
                ModuleControlKind::knob, ModuleControlKind::fader,
                ModuleControlKind::waveform, ModuleControlKind::spectrum,
                ModuleControlKind::levelMeter, ModuleControlKind::text,
                ModuleControlKind::inputPort, ModuleControlKind::outputPort,
                ModuleControlKind::toggle, ModuleControlKind::envelope,
                ModuleControlKind::group, ModuleControlKind::selector,
                ModuleControlKind::xyPad, ModuleControlKind::valueDisplay,
                ModuleControlKind::filterResponse,
                ModuleControlKind::dynamicsResponse
            };
            selected->kind = kinds[static_cast<std::size_t>(result - 1)];
            if (isParameterControl(selected->kind)) {
                if (safeThis->findParameter(selected->parameterBinding) == nullptr
                    && !safeThis->state_.parameters.empty())
                    selected->parameterBinding = safeThis->state_.parameters.front().key;
                if (selected->kind == ModuleControlKind::envelope) {
                    if (safeThis->state_.parameters.size() < 4) {
                        selected->kind = ModuleControlKind::waveform;
                        selected->parameterBinding.clear();
                    } else {
                        selected->parameterBinding = safeThis->state_.parameters[0].key;
                        selected->secondaryParameterBinding = safeThis->state_.parameters[1].key;
                        selected->tertiaryParameterBinding = safeThis->state_.parameters[2].key;
                        selected->quaternaryParameterBinding = safeThis->state_.parameters[3].key;
                    }
                } else if (selected->kind == ModuleControlKind::xyPad
                           || selected->kind
                                  == ModuleControlKind::filterResponse
                           || selected->kind
                                  == ModuleControlKind::dynamicsResponse) {
                    const auto invalidSecondary =
                        safeThis->findParameter(
                            selected->secondaryParameterBinding) == nullptr
                        || selected->secondaryParameterBinding
                               == selected->parameterBinding;
                    if (invalidSecondary)
                        for (const auto& parameter : safeThis->state_.parameters)
                            if (parameter.key != selected->parameterBinding) {
                                selected->secondaryParameterBinding = parameter.key;
                                break;
                            }
                } else {
                    selected->secondaryParameterBinding.clear();
                    selected->tertiaryParameterBinding.clear();
                    selected->quaternaryParameterBinding.clear();
                }
            } else if (!isPortControl(selected->kind)) {
                selected->parameterBinding.clear();
                selected->secondaryParameterBinding.clear();
                selected->tertiaryParameterBinding.clear();
                selected->quaternaryParameterBinding.clear();
            }
        } else if (result >= 100 && result < 999) {
            const auto index = static_cast<std::size_t>(result - 100);
            if (index < safeThis->state_.parameters.size()) {
                const auto key = safeThis->state_.parameters[index].key;
                if (selected->kind == ModuleControlKind::envelope
                    && (key == selected->secondaryParameterBinding
                        || key == selected->tertiaryParameterBinding
                        || key == selected->quaternaryParameterBinding))
                    return;
                selected->parameterBinding = key;
                selected->label = safeThis->state_.parameters[index].label;
                if (!isParameterControl(selected->kind))
                    selected->kind = ModuleControlKind::knob;
                if ((selected->kind == ModuleControlKind::xyPad
                     || selected->kind == ModuleControlKind::filterResponse
                     || selected->kind == ModuleControlKind::dynamicsResponse)
                    && selected->secondaryParameterBinding
                           == selected->parameterBinding)
                    for (const auto& parameter : safeThis->state_.parameters)
                        if (parameter.key != selected->parameterBinding) {
                            selected->secondaryParameterBinding = parameter.key;
                            break;
                        }
            }
        } else if (result >= 5000) {
            const auto index = static_cast<std::size_t>(result - 5000);
            if (index < safeThis->state_.parameters.size()
                && safeThis->state_.parameters[index].key
                       != selected->parameterBinding)
                selected->secondaryParameterBinding =
                    safeThis->state_.parameters[index].key;
        } else if (result >= 2000 && result <= 2003) {
            selected->portKind = static_cast<std::uint8_t>(result - 2000);
            selected->portChannels = selected->portKind == 0 ? 2 : 1;
        } else if (result >= 2101 && result <= 2108) {
            selected->portKind = 0;
            selected->portChannels = static_cast<std::uint8_t>(result - 2100);
        } else if (result == 3000) {
            if (auto* parameter = safeThis->findParameter(
                    selected->parameterBinding)) {
                parameter->showInDevice = !parameter->showInDevice;
                rebuildAutomaticDeviceControls(safeThis->state_);
            }
        } else if (result >= 3001 && result <= 3003) {
            if (auto* parameter = safeThis->findParameter(
                    selected->parameterBinding)) {
                if (result == 3001)
                    parameter->publishToHost = !parameter->publishToHost;
                else if (result == 3002 && parameter->publishToHost)
                    parameter->automatable = !parameter->automatable;
                else if (result == 3003)
                    parameter->modulatable = !parameter->modulatable;
            }
        } else if (result == 3100) {
            safeThis->showControlProperties(controlId);
            return;
        } else if (result == 3101) {
            auto copy = *selected;
            copy.id = safeThis->state_.nextControlId++;
            copy.x = juce::jlimit(
                0.0f, 1.0f - copy.width,
                copy.x + 1.0f / static_cast<float>(safeThis->state_.gridColumns));
            copy.y = juce::jlimit(
                0.0f, 1.0f - copy.height,
                copy.y + 1.0f / static_cast<float>(safeThis->state_.gridRows));
            safeThis->activeControls().push_back(copy);
            safeThis->selectedControlId_ = copy.id;
        } else if (result == 3102 || result == 3103) {
            auto& controls = safeThis->activeControls();
            const auto found = std::find_if(
                controls.begin(), controls.end(),
                [controlId](const auto& value) { return value.id == controlId; });
            if (found != controls.end()) {
                auto moved = *found;
                controls.erase(found);
                if (result == 3102)
                    controls.push_back(std::move(moved));
                else
                    controls.insert(controls.begin(), std::move(moved));
            }
        } else if (result == 999) {
            auto& controls = safeThis->activeControls();
            std::erase_if(controls,
                          [controlId](const auto& value) { return value.id == controlId; });
            safeThis->selectedControlId_ = 0;
        }
        safeThis->rebuildSliders();
        safeThis->notifyStateChanged();
        safeThis->repaint();
    });
}

void ModuleInterfaceEditorComponent::showControlProperties(
    const std::uint64_t controlId)
{
    const auto* control = findControl(controlId);
    if (control == nullptr || layoutMode_ != ModuleInterfaceLayoutMode::graph)
        return;

    auto* dialog = new juce::AlertWindow(
        tr("module.ui.controlPropertiesTitle"),
        tr("module.ui.controlPropertiesPrompt"),
        juce::MessageBoxIconType::NoIcon);
    const auto percent = [](const float value) {
        return juce::String(value * 100.0f, 2);
    };
    dialog->addTextEditor("label", control->label,
                          tr("module.ui.controlLabel"));
    dialog->addTextEditor("x", percent(control->x),
                          tr("module.ui.controlX"));
    dialog->addTextEditor("y", percent(control->y),
                          tr("module.ui.controlY"));
    dialog->addTextEditor("width", percent(control->width),
                          tr("module.ui.controlWidth"));
    dialog->addTextEditor("height", percent(control->height),
                          tr("module.ui.controlHeight"));
    dialog->addButton(tr("module.ui.applyControlProperties"), 1,
                      juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton(tr("layout.cancel"), 0,
                      juce::KeyPress(juce::KeyPress::escapeKey));

    const juce::Component::SafePointer<ModuleInterfaceEditorComponent> safeThis(this);
    dialog->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [safeThis, dialog, controlId](const int result) {
                if (safeThis == nullptr || result != 1)
                    return;
                auto* selected = safeThis->findControl(controlId);
                if (selected == nullptr)
                    return;
                const auto parsePercent = [dialog](const juce::String& key,
                                                   const float fallback) {
                    const auto text = dialog->getTextEditorContents(key)
                                          .trim().replaceCharacter(',', '.');
                    return text.isNotEmpty() ? static_cast<float>(text.getDoubleValue() / 100.0)
                                             : fallback;
                };
                const auto minimumWidth = 0.04f;
                const auto minimumHeight = 0.06f;
                selected->label = dialog->getTextEditorContents("label")
                                      .trim().substring(0, 128);
                selected->width = juce::jlimit(
                    minimumWidth, 1.0f,
                    parsePercent("width", selected->width));
                selected->height = juce::jlimit(
                    minimumHeight, 1.0f,
                    parsePercent("height", selected->height));
                selected->x = juce::jlimit(
                    0.0f, 1.0f - selected->width,
                    parsePercent("x", selected->x));
                selected->y = juce::jlimit(
                    0.0f, 1.0f - selected->height,
                    parsePercent("y", selected->y));
                safeThis->rebuildSliders();
                safeThis->notifyStateChanged();
                safeThis->repaint();
            }),
        true);
}

void ModuleInterfaceEditorComponent::showDeviceParametersMenu()
{
    juce::PopupMenu menu;
    for (std::size_t index = 0; index < state_.parameters.size(); ++index)
        menu.addItem(4000 + static_cast<int>(index),
                     state_.parameters[index].label, true,
                     state_.parameters[index].showInDevice);
    if (state_.parameters.empty())
        menu.addItem(1, tr("module.ui.noDeviceParameters"), false, false);
    juce::Component::SafePointer<ModuleInterfaceEditorComponent> safeThis(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&copyToDeviceButton_),
        [safeThis](const int result) {
            if (safeThis == nullptr || result < 4000)
                return;
            const auto index = static_cast<std::size_t>(result - 4000);
            if (index >= safeThis->state_.parameters.size())
                return;
            auto& parameter = safeThis->state_.parameters[index];
            parameter.showInDevice = !parameter.showInDevice;
            rebuildAutomaticDeviceControls(safeThis->state_);
            safeThis->rebuildSliders();
            safeThis->notifyStateChanged();
            safeThis->repaint();
        });
}

void ModuleInterfaceEditorComponent::refreshInspector()
{
    refreshingInspector_ = true;
    const auto* control = findControl(selectedControlId_);
    const auto envelopeControl = control != nullptr
        && control->kind == ModuleControlKind::envelope;
    controlPrimaryBindingCaption_.setText(
        tr(envelopeControl ? "module.ui.attackBinding"
                           : "module.ui.primaryBinding"),
        juce::dontSendNotification);
    controlSecondaryBindingCaption_.setText(
        tr(envelopeControl ? "module.ui.decayBinding"
                           : "module.ui.secondaryBinding"),
        juce::dontSendNotification);
    const auto editable = control != nullptr
                          && layoutMode_ == ModuleInterfaceLayoutMode::graph
                          && editButton_.getToggleState();
    for (auto* editor : { &controlLabelEditor_, &controlXEditor_,
                          &controlYEditor_, &controlWidthEditor_,
                          &controlHeightEditor_ })
        editor->setEnabled(editable);
    applyInspectorButton_.setEnabled(editable);
    controlPrimaryBindingSelector_.setEnabled(
        editable && control != nullptr && isParameterControl(control->kind));
    controlSecondaryBindingSelector_.setEnabled(
        editable && control != nullptr
        && (control->kind == ModuleControlKind::xyPad
            || control->kind == ModuleControlKind::filterResponse
            || control->kind == ModuleControlKind::dynamicsResponse
            || control->kind == ModuleControlKind::envelope));
    controlTertiaryBindingSelector_.setEnabled(
        editable && envelopeControl);
    controlQuaternaryBindingSelector_.setEnabled(
        editable && envelopeControl);
    controlSecondaryBindingCaption_.setVisible(
        control != nullptr
        && (control->kind == ModuleControlKind::xyPad
            || control->kind == ModuleControlKind::filterResponse
            || control->kind == ModuleControlKind::dynamicsResponse
            || control->kind == ModuleControlKind::envelope));
    controlSecondaryBindingSelector_.setVisible(
        control != nullptr
        && (control->kind == ModuleControlKind::xyPad
            || control->kind == ModuleControlKind::filterResponse
            || control->kind == ModuleControlKind::dynamicsResponse
            || control->kind == ModuleControlKind::envelope));
    controlTertiaryBindingCaption_.setVisible(
        envelopeControl);
    controlTertiaryBindingSelector_.setVisible(
        envelopeControl);
    controlQuaternaryBindingCaption_.setVisible(
        envelopeControl);
    controlQuaternaryBindingSelector_.setVisible(
        envelopeControl);
    controlPrimaryBindingCaption_.setVisible(
        control != nullptr && isParameterControl(control->kind));
    controlPrimaryBindingSelector_.setVisible(
        control != nullptr && isParameterControl(control->kind));

    controlPrimaryBindingSelector_.clear(juce::dontSendNotification);
    controlSecondaryBindingSelector_.clear(juce::dontSendNotification);
    controlTertiaryBindingSelector_.clear(juce::dontSendNotification);
    controlQuaternaryBindingSelector_.clear(juce::dontSendNotification);
    for (std::size_t index = 0; index < state_.parameters.size(); ++index) {
        const auto itemId = static_cast<int>(index + 1);
        controlPrimaryBindingSelector_.addItem(state_.parameters[index].label,
                                               itemId);
        controlSecondaryBindingSelector_.addItem(state_.parameters[index].label,
                                                 itemId);
        controlTertiaryBindingSelector_.addItem(state_.parameters[index].label,
                                                itemId);
        controlQuaternaryBindingSelector_.addItem(state_.parameters[index].label,
                                                  itemId);
    }

    if (control == nullptr) {
        inspectorKindLabel_.setText(tr("module.ui.noControlSelected"),
                                    juce::dontSendNotification);
        inspectorBindingLabel_.setText(tr("module.ui.inspectorHelp"),
                                       juce::dontSendNotification);
        for (auto* editor : { &controlLabelEditor_, &controlXEditor_,
                              &controlYEditor_, &controlWidthEditor_,
                              &controlHeightEditor_ })
            editor->clear();
        controlPrimaryBindingSelector_.setSelectedId(
            0, juce::dontSendNotification);
        controlSecondaryBindingSelector_.setSelectedId(
            0, juce::dontSendNotification);
        controlTertiaryBindingSelector_.setSelectedId(
            0, juce::dontSendNotification);
        controlQuaternaryBindingSelector_.setSelectedId(
            0, juce::dontSendNotification);
        refreshingInspector_ = false;
        resized();
        return;
    }

    const auto kind = [control] {
        switch (control->kind) {
            case ModuleControlKind::knob: return tr("module.ui.knob");
            case ModuleControlKind::fader: return tr("module.ui.fader");
            case ModuleControlKind::waveform: return tr("module.ui.waveform");
            case ModuleControlKind::spectrum: return tr("module.ui.spectrum");
            case ModuleControlKind::levelMeter: return tr("module.ui.meter");
            case ModuleControlKind::text: return tr("module.ui.text");
            case ModuleControlKind::inputPort: return tr("module.ui.inputPort");
            case ModuleControlKind::outputPort: return tr("module.ui.outputPort");
            case ModuleControlKind::toggle: return tr("module.ui.toggle");
            case ModuleControlKind::envelope: return tr("module.ui.envelope");
            case ModuleControlKind::group: return tr("module.ui.group");
            case ModuleControlKind::selector: return tr("module.ui.selector");
            case ModuleControlKind::xyPad: return tr("module.ui.xyPad");
            case ModuleControlKind::valueDisplay: return tr("module.ui.valueDisplay");
            case ModuleControlKind::filterResponse: return tr("module.ui.filterResponse");
            case ModuleControlKind::dynamicsResponse: return tr("module.ui.dynamicsResponse");
        }
        return tr("module.ui.text");
    }();
    inspectorKindLabel_.setText(
        tr("module.ui.controlType", "type", kind),
        juce::dontSendNotification);
    auto bindingText = control->parameterBinding.isNotEmpty()
        ? tr("module.ui.controlBinding", "binding",
             control->parameterBinding)
        : tr("module.ui.controlBindingNone");
    if (control->kind == ModuleControlKind::xyPad
        || control->kind == ModuleControlKind::filterResponse
        || control->kind == ModuleControlKind::dynamicsResponse)
        bindingText += "\n" + tr("module.ui.secondaryBindingValue", "binding",
                                    control->secondaryParameterBinding);
    else if (control->kind == ModuleControlKind::envelope)
        bindingText += "\nA: " + control->parameterBinding
            + " | D: " + control->secondaryParameterBinding
            + " | S: " + control->tertiaryParameterBinding
            + " | R: " + control->quaternaryParameterBinding;
    inspectorBindingLabel_.setText(bindingText, juce::dontSendNotification);
    for (std::size_t index = 0; index < state_.parameters.size(); ++index) {
        const auto itemId = static_cast<int>(index + 1);
        if (state_.parameters[index].key == control->parameterBinding)
            controlPrimaryBindingSelector_.setSelectedId(
                itemId, juce::dontSendNotification);
        if (state_.parameters[index].key
            == control->secondaryParameterBinding)
            controlSecondaryBindingSelector_.setSelectedId(
                itemId, juce::dontSendNotification);
        if (state_.parameters[index].key
            == control->tertiaryParameterBinding)
            controlTertiaryBindingSelector_.setSelectedId(
                itemId, juce::dontSendNotification);
        if (state_.parameters[index].key
            == control->quaternaryParameterBinding)
            controlQuaternaryBindingSelector_.setSelectedId(
                itemId, juce::dontSendNotification);
    }
    const auto percent = [](const float value) {
        return juce::String(value * 100.0f, 2);
    };
    controlLabelEditor_.setText(control->label, false);
    controlXEditor_.setText(percent(control->x), false);
    controlYEditor_.setText(percent(control->y), false);
    controlWidthEditor_.setText(percent(control->width), false);
    controlHeightEditor_.setText(percent(control->height), false);
    refreshingInspector_ = false;
    resized();
}

void ModuleInterfaceEditorComponent::applyInspectorChanges()
{
    if (refreshingInspector_ || layoutMode_ != ModuleInterfaceLayoutMode::graph
        || !editButton_.getToggleState())
        return;
    auto* control = findControl(selectedControlId_);
    if (control == nullptr)
        return;
    const auto parsePercent = [](juce::TextEditor& editor,
                                 const float fallback) {
        const auto text = editor.getText().trim().replaceCharacter(',', '.');
        return text.isNotEmpty()
                   ? static_cast<float>(text.getDoubleValue() / 100.0)
                   : fallback;
    };
    control->label = controlLabelEditor_.getText().trim().substring(0, 128);
    control->width = juce::jlimit(
        0.04f, 1.0f, parsePercent(controlWidthEditor_, control->width));
    control->height = juce::jlimit(
        0.06f, 1.0f, parsePercent(controlHeightEditor_, control->height));
    control->x = juce::jlimit(
        0.0f, 1.0f - control->width,
        parsePercent(controlXEditor_, control->x));
    control->y = juce::jlimit(
        0.0f, 1.0f - control->height,
        parsePercent(controlYEditor_, control->y));
    rebuildSliders();
    refreshInspector();
    notifyStateChanged();
    resized();
    repaint();
}

void ModuleInterfaceEditorComponent::scheduleParameterStateCommit()
{
    parameterStateCommitPending_ = true;
    if (parameterStateCommitScheduled_)
        return;
    parameterStateCommitScheduled_ = true;
    const juce::Component::SafePointer<ModuleInterfaceEditorComponent> safeThis(this);
    juce::Timer::callAfterDelay(33, [safeThis] {
        if (safeThis == nullptr)
            return;
        safeThis->parameterStateCommitScheduled_ = false;
        safeThis->flushParameterStateCommit();
    });
}

void ModuleInterfaceEditorComponent::flushParameterStateCommit()
{
    if (!parameterStateCommitPending_)
        return;
    parameterStateCommitPending_ = false;
    notifyStateChanged();
}

void ModuleInterfaceEditorComponent::beginHistoryGesture()
{
    if (historyGestureActive_ || restoringHistory_)
        return;
    historyGestureActive_ = true;
    historyGestureStart_ = committedState_;
}

void ModuleInterfaceEditorComponent::endHistoryGesture()
{
    if (!historyGestureActive_)
        return;
    historyGestureActive_ = false;
    auto current = serialiseModuleInterface(state_);
    if (!historyGestureStart_.isVoid()
        && !jsonValuesMatch(historyGestureStart_, current)) {
        const auto& previous = historyGestureStart_;
        if (!previous.isVoid()) {
            if (undoStates_.size() >= 128)
                undoStates_.erase(undoStates_.begin());
            undoStates_.push_back(restoreModuleInterface(previous));
            redoStates_.clear();
        }
    }
    historyGestureStart_ = juce::var();
    committedState_ = std::move(current);
    refreshHistoryButtons();
}

void ModuleInterfaceEditorComponent::undoInterfaceChange()
{
    flushParameterStateCommit();
    endHistoryGesture();
    if (undoStates_.empty())
        return;
    redoStates_.push_back(state_);
    auto previous = std::move(undoStates_.back());
    undoStates_.pop_back();
    restoreHistoryState(std::move(previous));
}

void ModuleInterfaceEditorComponent::redoInterfaceChange()
{
    flushParameterStateCommit();
    endHistoryGesture();
    if (redoStates_.empty())
        return;
    undoStates_.push_back(state_);
    auto next = std::move(redoStates_.back());
    redoStates_.pop_back();
    restoreHistoryState(std::move(next));
}

void ModuleInterfaceEditorComponent::restoreHistoryState(
    ModuleInterfaceState state)
{
    restoringHistory_ = true;
    state_ = std::move(state);
    selectedControlId_ = 0;
    draggingControl_ = false;
    resizingControl_ = false;
    resizeEdges_ = 0;
    snapButton_.setToggleState(state_.snapToGrid,
                               juce::dontSendNotification);
    committedState_ = serialiseModuleInterface(state_);
    rebuildSliders();
    refreshTranslations();
    refreshInspector();
    refreshHistoryButtons();
    resized();
    repaint();
    if (stateChangedHandler_)
        stateChangedHandler_(state_);
    restoringHistory_ = false;
}

void ModuleInterfaceEditorComponent::refreshHistoryButtons()
{
    const auto editable = !compactMode_
        && layoutMode_ == ModuleInterfaceLayoutMode::graph;
    undoButton_.setEnabled(editable && !undoStates_.empty());
    redoButton_.setEnabled(editable && !redoStates_.empty());
}

void ModuleInterfaceEditorComponent::notifyStateChanged()
{
    auto current = serialiseModuleInterface(state_);
    if (!restoringHistory_ && !historyGestureActive_
        && !committedState_.isVoid()
        && !jsonValuesMatch(committedState_, current)) {
        const auto& previous = committedState_;
        if (!previous.isVoid()) {
            if (undoStates_.size() >= 128)
                undoStates_.erase(undoStates_.begin());
            undoStates_.push_back(restoreModuleInterface(previous));
            redoStates_.clear();
        }
    }
    committedState_ = std::move(current);
    refreshHistoryButtons();
    if (stateChangedHandler_)
        stateChangedHandler_(state_);
}

void ModuleInterfaceEditorComponent::updateSpectrum()
{
    if (waveformCount_ < 4) {
        spectrum_.fill(0.0f);
        return;
    }
    // The same computation the graph uses, from the frame it was handed:
    // one place, calibrated in hertz.
    std::array<float, 32> fresh {};
    cydaw::engine::computeLogSpectrum(lastFrame_, 20.0, 20'000.0, fresh.data(),
                                      fresh.size());
    for (std::size_t bin = 0; bin < spectrum_.size(); ++bin)
        spectrum_[bin] = std::max(fresh[std::min(bin, fresh.size() - 1)],
                                  spectrum_[bin] * 0.76f);
    if (!eqCurves_.empty()) {
        std::array<float, 96> eqSpectrum {};
        cydaw::engine::computeLogSpectrum(lastFrame_, 20.0, 20'000.0,
                                          eqSpectrum.data(), eqSpectrum.size());
        for (auto& entry : eqCurves_)
            if (entry.curve != nullptr && entry.curve->isVisible())
                entry.curve->setSpectrum(eqSpectrum.data(), eqSpectrum.size());
    }
}

const juce::GlyphArrangement& ModuleInterfaceEditorComponent::cachedLabel(
    const ModuleInterfaceControl& control, const juce::Rectangle<int> area)
{
    static const juce::Font font { juce::FontOptions(10.0f, juce::Font::bold) };
    auto& cached = labelCache_[control.id];
    if (cached.text != control.label || cached.area != area) {
        cached.text = control.label;
        cached.area = area;
        cached.glyphs.clear();
        cached.glyphs.addFittedText(
            font, control.label, static_cast<float>(area.getX()),
            static_cast<float>(area.getY()), static_cast<float>(area.getWidth()),
            static_cast<float>(area.getHeight()), juce::Justification::centred,
            1, 0.75f);
    }
    return cached.glyphs;
}

void ModuleInterfaceEditorComponent::paint(juce::Graphics& graphics)
{
    const paint_profile::Scoped paintScope(paint_profile::counters().device);
    graphics.fillAll(juce::Colour(0xff17191c));
    auto canvas = canvasBounds().toFloat();
    graphics.setColour(juce::Colour(0xff202328));
    graphics.fillRoundedRectangle(canvas, 5.0f);
    if (editButton_.getToggleState()
        && layoutMode_ == ModuleInterfaceLayoutMode::graph) {
        const auto columns = static_cast<int>(state_.gridColumns);
        const auto rows = static_cast<int>(state_.gridRows);
        for (int column = 1; column < columns; ++column) {
            graphics.setColour(column % 4 == 0 ? juce::Colour(0xff353a40)
                                               : juce::Colour(0xff292d32));
            const auto x = canvas.getX() + canvas.getWidth()
                                           * static_cast<float>(column)
                                           / static_cast<float>(columns);
            graphics.drawVerticalLine(juce::roundToInt(x), canvas.getY(),
                                      canvas.getBottom());
        }
        for (int row = 1; row < rows; ++row) {
            graphics.setColour(row % 4 == 0 ? juce::Colour(0xff353a40)
                                            : juce::Colour(0xff292d32));
            const auto y = canvas.getY() + canvas.getHeight()
                                           * static_cast<float>(row)
                                           / static_cast<float>(rows);
            graphics.drawHorizontalLine(juce::roundToInt(y), canvas.getX(),
                                        canvas.getRight());
        }
    }

    const auto clip = graphics.getClipBounds().toFloat();
    for (const auto& control : activeControls()) {
        // A plugin's window shows no graph sockets.
        if (performanceMode_ && isPortControl(control.kind))
            continue;
        const auto bounds = controlBounds(control);
        // A meter's repaint is a meter's paint, not every control's.
        if (!bounds.expanded(8.0f).intersects(clip))
            continue;
        const auto port = control.kind == ModuleControlKind::inputPort
                          || control.kind == ModuleControlKind::outputPort;
        if (!port) {
            graphics.setColour(juce::Colour(0xff292d32));
            graphics.fillRoundedRectangle(
                bounds, control.kind == ModuleControlKind::group ? 7.0f : 4.0f);
        }
        graphics.setColour(control.id == selectedControlId_
                               ? juce::Colour(0xffff8a00)
                               : (port ? juce::Colour(0xff67b9ff)
                                       : juce::Colour(0xff4d535a)));
        if (!port)
            graphics.drawRoundedRectangle(bounds, 4.0f,
                                          control.id == selectedControlId_ ? 2.0f : 1.0f);
        graphics.setColour(juce::Colour(0xffe8ebee));
        cachedLabel(control, bounds.toNearestInt().reduced(6, 3).removeFromTop(18))
            .draw(graphics);
        // The "shown in the device" badge is an editing aid; a plugin's
        // window has none.
        if (layoutMode_ == ModuleInterfaceLayoutMode::graph && !performanceMode_
            && isParameterControl(control.kind)) {
            if (const auto* parameter = findParameter(control.parameterBinding);
                parameter != nullptr && parameter->showInDevice) {
                const auto badge = bounds.toNearestInt().reduced(3)
                                       .removeFromTop(15).removeFromRight(15);
                graphics.setColour(juce::Colour(0xffff8a00));
                graphics.fillEllipse(badge.toFloat());
                graphics.setColour(juce::Colour(0xff17191c));
                graphics.setFont(juce::FontOptions(8.0f, juce::Font::bold));
                graphics.drawText("D", badge, juce::Justification::centred,
                                  false);
            }
        }
        auto display = bounds.reduced(7.0f).withTrimmedTop(18.0f);
        if (port) {
            const auto input = control.kind == ModuleControlKind::inputPort;
            const auto centre = juce::Point<float>(
                input ? bounds.getX() : bounds.getRight(), bounds.getCentreY());
            const std::array<juce::Colour, 4> colours {
                juce::Colour(0xff67b9ff), juce::Colour(0xffd47be8),
                juce::Colour(0xff75d6ac), juce::Colour(0xffffc65c)
            };
            const auto colour = colours[std::min<std::size_t>(
                control.portKind, colours.size() - 1)];
            graphics.setColour(juce::Colour(0xff101214));
            graphics.fillEllipse(centre.x - 8.0f, centre.y - 8.0f, 16.0f, 16.0f);
            graphics.setColour(colour);
            graphics.fillEllipse(centre.x - 5.0f, centre.y - 5.0f, 10.0f, 10.0f);
            graphics.drawLine(centre.x + (input ? 7.0f : -7.0f), centre.y,
                              bounds.getCentreX(), centre.y, 1.5f);
            const auto format = control.portKind == 0
                ? control.portChannels == 1 ? juce::String("MONO")
                  : control.portChannels == 2 ? juce::String("ST")
                    : juce::String(static_cast<int>(control.portChannels))
                          + " CH"
                : control.portKind == 1 ? juce::String("MIDI")
                  : control.portKind == 2 ? juce::String("VALUE")
                                          : juce::String("TRANSPORT");
            graphics.setFont(juce::FontOptions(8.0f, juce::Font::bold));
            graphics.drawFittedText(
                format, bounds.toNearestInt().reduced(7, 3).removeFromBottom(13),
                juce::Justification::centred, 1, 0.72f);
        } else if (control.kind == ModuleControlKind::waveform
                   || control.kind == ModuleControlKind::spectrum
                   || control.kind == ModuleControlKind::levelMeter) {
            // Drawn by its own child; see ModuleAnalysisDisplayComponent.
        } else if (control.kind == ModuleControlKind::valueDisplay) {
            graphics.setColour(juce::Colour(0xff101317));
            graphics.fillRoundedRectangle(display, 3.0f);
            if (const auto* parameter = findParameter(control.parameterBinding)) {
                const auto normalised = parameter->maximum > parameter->minimum
                    ? juce::jlimit(0.0, 1.0,
                        (parameter->value - parameter->minimum)
                            / (parameter->maximum - parameter->minimum))
                    : 0.0;
                auto bar = display.reduced(5.0f).removeFromBottom(4.0f);
                graphics.setColour(juce::Colour(0xff273039));
                graphics.fillRoundedRectangle(bar, 2.0f);
                graphics.setColour(juce::Colour(0xff75d6ac));
                graphics.fillRoundedRectangle(
                    bar.withWidth(bar.getWidth()
                                  * static_cast<float>(normalised)),
                    2.0f);
                const auto span = parameter->maximum - parameter->minimum;
                const auto decimals = span <= 4.0 ? 3 : span <= 100.0 ? 2 : 1;
                const auto value = juce::String(parameter->value, decimals)
                    + (parameter->unit.isNotEmpty()
                           ? " " + parameter->unit : juce::String {});
                graphics.setColour(juce::Colour(0xffdffaf0));
                graphics.setFont(juce::FontOptions(
                    juce::jlimit(10.0f, 18.0f,
                        display.getHeight() * 0.38f),
                    juce::Font::bold));
                graphics.drawFittedText(value, display.toNearestInt().reduced(4),
                                        juce::Justification::centred, 1, 0.75f);
            }
        } else if (control.kind == ModuleControlKind::toggle) {
            // The native ToggleButton child paints the interactive switch.
        } else if (control.kind == ModuleControlKind::selector) {
            // The native ComboBox child paints the interactive selector.
        } else if (control.kind == ModuleControlKind::xyPad) {
            // The native XY pad child paints the two-dimensional controller.
        } else if (control.kind == ModuleControlKind::filterResponse) {
            // The native response child paints the interactive filter curve.
        } else if (control.kind == ModuleControlKind::dynamicsResponse) {
            // The native response child paints the dynamics transfer curve.
        } else if (control.kind == ModuleControlKind::envelope) {
            graphics.setColour(juce::Colour(0xff101317));
            graphics.fillRoundedRectangle(display, 3.0f);
            juce::Path envelope;
            const std::array<juce::Point<float>, 5> points {
                juce::Point<float> { display.getX(), display.getBottom() },
                { display.getX() + display.getWidth() * 0.20f,
                  display.getY() },
                { display.getX() + display.getWidth() * 0.43f,
                  display.getY() + display.getHeight() * 0.34f },
                { display.getX() + display.getWidth() * 0.73f,
                  display.getY() + display.getHeight() * 0.34f },
                { display.getRight(), display.getBottom() }
            };
            envelope.startNewSubPath(points.front());
            for (std::size_t index = 1; index < points.size(); ++index)
                envelope.lineTo(points[index]);
            graphics.setColour(juce::Colour(0xffffa33a));
            graphics.strokePath(envelope, juce::PathStrokeType(1.5f));
        } else if (control.kind == ModuleControlKind::group) {
            graphics.setColour(juce::Colour(0xff3a4047));
            graphics.drawRoundedRectangle(display, 5.0f, 1.0f);
        } else if (control.kind == ModuleControlKind::text) {
            graphics.setColour(juce::Colour(0xffabb1b8));
            graphics.setFont(juce::FontOptions(12.0f));
            graphics.drawFittedText(control.label, display.toNearestInt(),
                                    juce::Justification::centred, 2, 0.8f);
        }
        if (editButton_.getToggleState()
            && control.id == selectedControlId_) {
            graphics.setColour(juce::Colour(0xffff8a00));
            constexpr auto handleSize = 7.0f;
            const std::array<juce::Point<float>, 8> handles {
                bounds.getTopLeft(),
                { bounds.getCentreX(), bounds.getY() },
                bounds.getTopRight(),
                { bounds.getX(), bounds.getCentreY() },
                { bounds.getRight(), bounds.getCentreY() },
                bounds.getBottomLeft(),
                { bounds.getCentreX(), bounds.getBottom() },
                bounds.getBottomRight()
            };
            for (const auto handle : handles) {
                graphics.setColour(juce::Colour(0xff17191c));
                graphics.fillEllipse(handle.x - handleSize * 0.7f,
                                     handle.y - handleSize * 0.7f,
                                     handleSize * 1.4f,
                                     handleSize * 1.4f);
                graphics.setColour(juce::Colour(0xffff8a00));
                graphics.fillEllipse(handle.x - handleSize * 0.5f,
                                     handle.y - handleSize * 0.5f,
                                     handleSize, handleSize);
            }
        }
    }
    graphics.setColour(juce::Colour(0xff3f444a));
    graphics.drawRoundedRectangle(canvas, 5.0f, 1.0f);
    if (!compactMode_) {
        const auto inspector = inspectorBounds().toFloat();
        graphics.setColour(juce::Colour(0xff202328));
        graphics.fillRoundedRectangle(inspector, 5.0f);
        graphics.setColour(juce::Colour(0xff3f444a));
        graphics.drawRoundedRectangle(inspector, 5.0f, 1.0f);
    }
    if (!compactMode_ && selectedControlId_ != 0) {
        if (const auto* selected = findControl(selectedControlId_)) {
            const auto percent = [](const float value) {
                return juce::String(value * 100.0f, 1) + "%";
            };
            auto detail = tr("module.ui.selectionGeometry", "name",
                             selected->label.isNotEmpty()
                                 ? selected->label : tr("module.ui.text"));
            detail = detail.replace("{x}", percent(selected->x))
                           .replace("{y}", percent(selected->y))
                           .replace("{width}", percent(selected->width))
                           .replace("{height}", percent(selected->height));
            if (selected->parameterBinding.isNotEmpty())
                detail += "  |  " + tr("module.ui.bindingValue", "binding",
                                         selected->parameterBinding);
            graphics.setColour(juce::Colour(0xffaeb5bd));
            graphics.setFont(juce::FontOptions(10.0f));
            graphics.drawFittedText(
                detail, getLocalBounds().removeFromBottom(22).reduced(10, 2),
                juce::Justification::centredLeft, 1, 0.8f);
        }
    }
}

void ModuleInterfaceEditorComponent::resized()
{
    // Frames move with the layout; the labels are laid out again for them.
    labelCache_.clear();
    if (compactMode_) {
        if (!performanceMode_)
            layoutCompactCells();
        for (auto& entry : sliders_)
            if (const auto* control = findControl(entry.controlId)) {
                const auto cell = controlBounds(*control).toNearestInt();
                // A short row has no room for a value box under the knob:
                // the value shows while the knob is dragged, and in the
                // tooltip.
                const auto tall = cell.getHeight() >= 72;
                entry.slider->setTextBoxStyle(
                    tall ? juce::Slider::TextBoxBelow : juce::Slider::NoTextBox,
                    false, 76, 18);
                entry.slider->setPopupDisplayEnabled(!tall, false,
                                                     getTopLevelComponent());
                entry.slider->setBounds(cell.reduced(4, 3).withTrimmedTop(12));
            }
        for (auto& entry : buttons_)
            if (const auto* control = findControl(entry.controlId))
                entry.button->setBounds(controlBounds(*control).toNearestInt()
                                            .reduced(8, 5).withTrimmedTop(14));
        for (auto& entry : combos_)
            if (const auto* control = findControl(entry.controlId))
                entry.combo->setBounds(controlBounds(*control).toNearestInt()
                                           .reduced(6, 5).withTrimmedTop(14));
        for (auto& entry : xyPads_)
            if (const auto* control = findControl(entry.controlId))
                entry.pad->setBounds(controlBounds(*control).toNearestInt()
                                         .reduced(6, 5).withTrimmedTop(14));
        for (auto& entry : envelopes_)
            if (const auto* control = findControl(entry.controlId))
                entry.envelope->setBounds(
                    controlBounds(*control).toNearestInt()
                        .reduced(6, 5).withTrimmedTop(14));
        for (auto& entry : filterResponses_)
            if (const auto* control = findControl(entry.controlId))
                entry.response->setBounds(
                    controlBounds(*control).toNearestInt()
                        .reduced(6, 5).withTrimmedTop(14));
        for (auto& entry : eqCurves_)
            if (const auto* control = findControl(entry.controlId))
                entry.curve->setBounds(
                    controlBounds(*control).toNearestInt()
                        .reduced(6, 5).withTrimmedTop(14));
        for (auto& entry : dynamicsResponses_)
            if (const auto* control = findControl(entry.controlId))
                entry.response->setBounds(
                    controlBounds(*control).toNearestInt()
                        .reduced(6, 5).withTrimmedTop(14));
        for (auto& entry : analysisDisplays_)
            if (const auto* control = findControl(entry.controlId))
                entry.display->setBounds(controlBounds(*control).toNearestInt()
                                             .reduced(7).withTrimmedTop(18));
        return;
    }
    auto toolbar = getLocalBounds().removeFromTop(68).reduced(8, 4);
    auto modeRow = toolbar.removeFromTop(28);
    titleLabel_.setBounds(modeRow.removeFromLeft(150));
    graphModeButton_.setBounds(modeRow.removeFromLeft(120).reduced(3, 0));
    deviceModeButton_.setBounds(modeRow.removeFromLeft(140).reduced(3, 0));
    hintLabel_.setBounds(modeRow.reduced(4, 0));
    toolbar.removeFromTop(4);
    auto actionRow = toolbar.removeFromTop(28);
    editButton_.setBounds(actionRow.removeFromLeft(120).reduced(3, 0));
    addButton_.setBounds(actionRow.removeFromLeft(104).reduced(3, 0));
    snapButton_.setBounds(actionRow.removeFromLeft(72).reduced(3, 0));
    gridButton_.setBounds(actionRow.removeFromLeft(96).reduced(3, 0));
    copyToDeviceButton_.setBounds(actionRow.removeFromLeft(150).reduced(3, 0));
    undoButton_.setBounds(actionRow.removeFromLeft(74).reduced(3, 0));
    redoButton_.setBounds(actionRow.removeFromLeft(74).reduced(3, 0));

    auto inspector = inspectorBounds().reduced(10, 8);
    inspectorTitleLabel_.setBounds(inspector.removeFromTop(24));
    inspectorKindLabel_.setBounds(inspector.removeFromTop(24));
    inspectorBindingLabel_.setBounds(inspector.removeFromTop(42));
    inspector.removeFromTop(8);
    const auto placeField = [&inspector](juce::Label& caption,
                                         juce::TextEditor& editor) {
        caption.setBounds(inspector.removeFromTop(16));
        editor.setBounds(inspector.removeFromTop(24));
        inspector.removeFromTop(3);
    };
    if (controlPrimaryBindingSelector_.isVisible()) {
        controlPrimaryBindingCaption_.setBounds(inspector.removeFromTop(16));
        controlPrimaryBindingSelector_.setBounds(inspector.removeFromTop(24));
        inspector.removeFromTop(3);
    }
    if (controlSecondaryBindingSelector_.isVisible()) {
        controlSecondaryBindingCaption_.setBounds(inspector.removeFromTop(16));
        controlSecondaryBindingSelector_.setBounds(inspector.removeFromTop(24));
        inspector.removeFromTop(3);
    }
    if (controlTertiaryBindingSelector_.isVisible()) {
        controlTertiaryBindingCaption_.setBounds(inspector.removeFromTop(16));
        controlTertiaryBindingSelector_.setBounds(inspector.removeFromTop(24));
        inspector.removeFromTop(3);
    }
    if (controlQuaternaryBindingSelector_.isVisible()) {
        controlQuaternaryBindingCaption_.setBounds(inspector.removeFromTop(16));
        controlQuaternaryBindingSelector_.setBounds(inspector.removeFromTop(24));
        inspector.removeFromTop(3);
    }
    placeField(controlLabelCaption_, controlLabelEditor_);
    placeField(controlXCaption_, controlXEditor_);
    placeField(controlYCaption_, controlYEditor_);
    placeField(controlWidthCaption_, controlWidthEditor_);
    placeField(controlHeightCaption_, controlHeightEditor_);
    applyInspectorButton_.setBounds(inspector.removeFromTop(28));
    for (auto& entry : sliders_)
        if (const auto* control = findControl(entry.controlId))
            entry.slider->setBounds(controlBounds(*control).toNearestInt()
                                        .reduced(8, 5).withTrimmedTop(16));
    for (auto& entry : buttons_)
        if (const auto* control = findControl(entry.controlId))
            entry.button->setBounds(controlBounds(*control).toNearestInt()
                                        .reduced(10, 6).withTrimmedTop(16));
    for (auto& entry : combos_)
        if (const auto* control = findControl(entry.controlId))
            entry.combo->setBounds(controlBounds(*control).toNearestInt()
                                       .reduced(8, 6).withTrimmedTop(16));
    for (auto& entry : xyPads_)
        if (const auto* control = findControl(entry.controlId))
            entry.pad->setBounds(controlBounds(*control).toNearestInt()
                                     .reduced(8, 6).withTrimmedTop(16));
    for (auto& entry : envelopes_)
        if (const auto* control = findControl(entry.controlId))
            entry.envelope->setBounds(controlBounds(*control).toNearestInt()
                                          .reduced(8, 6).withTrimmedTop(16));
    for (auto& entry : filterResponses_)
        if (const auto* control = findControl(entry.controlId))
            entry.response->setBounds(controlBounds(*control).toNearestInt()
                                          .reduced(8, 6).withTrimmedTop(16));
    for (auto& entry : eqCurves_)
        if (const auto* control = findControl(entry.controlId))
            entry.curve->setBounds(controlBounds(*control).toNearestInt()
                                       .reduced(8, 6).withTrimmedTop(16));
    for (auto& entry : dynamicsResponses_)
        if (const auto* control = findControl(entry.controlId))
            entry.response->setBounds(controlBounds(*control).toNearestInt()
                                          .reduced(8, 6).withTrimmedTop(16));
    for (auto& entry : analysisDisplays_)
        if (const auto* control = findControl(entry.controlId))
            entry.display->setBounds(controlBounds(*control).toNearestInt()
                                         .reduced(7).withTrimmedTop(18));
}

void ModuleInterfaceEditorComponent::mouseDown(const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    const auto local = event.getEventRelativeTo(this);
    const auto id = controlAt(local.position);
    if (local.mods.isPopupMenu()) {
        if (id != 0) {
            selectedControlId_ = id;
            refreshInspector();
            repaint();
            showControlMenu(id);
        }
        return;
    }
    if (id == 0) {
        selectedControlId_ = 0;
        refreshInspector();
        repaint();
        return;
    }
    selectedControlId_ = id;
    refreshInspector();
    repaint();
    if (!editButton_.getToggleState())
        return;
    if (const auto* control = findControl(id)) {
        const auto bounds = controlBounds(*control);
        const std::array<std::pair<juce::Point<float>, int>, 8> handles {{
            { bounds.getTopLeft(), 1 | 4 },
            { { bounds.getCentreX(), bounds.getY() }, 4 },
            { bounds.getTopRight(), 2 | 4 },
            { { bounds.getX(), bounds.getCentreY() }, 1 },
            { { bounds.getRight(), bounds.getCentreY() }, 2 },
            { bounds.getBottomLeft(), 1 | 8 },
            { { bounds.getCentreX(), bounds.getBottom() }, 8 },
            { bounds.getBottomRight(), 2 | 8 }
        }};
        resizeEdges_ = 0;
        auto closestDistance = 11.0f;
        for (const auto& [handle, edges] : handles) {
            const auto distance = local.position.getDistanceFrom(handle);
            if (distance < closestDistance) {
                closestDistance = distance;
                resizeEdges_ = edges;
            }
        }
        resizingControl_ = resizeEdges_ != 0;
        resizeStartControl_ = *control;
        dragOffset_ = local.position - bounds.getPosition();
        draggingControl_ = !resizingControl_;
        if (draggingControl_ || resizingControl_)
            beginHistoryGesture();
    }
    repaint();
}

void ModuleInterfaceEditorComponent::mouseMove(const juce::MouseEvent& event)
{
    const auto id = controlAt(event.getEventRelativeTo(this).position);
    if (id == hoveredControlId_)
        return;
    hoveredControlId_ = id;
    if (const auto* control = findControl(id))
        setTooltip(controlTooltip(*control));
    else
        setTooltip({});
}

void ModuleInterfaceEditorComponent::mouseExit(const juce::MouseEvent&)
{
    hoveredControlId_ = 0;
    setTooltip({});
}

void ModuleInterfaceEditorComponent::mouseDrag(const juce::MouseEvent& event)
{
    if ((!draggingControl_ && !resizingControl_)
        || !editButton_.getToggleState())
        return;
    auto* control = findControl(selectedControlId_);
    if (control == nullptr)
        return;
    const auto local = event.getEventRelativeTo(this);
    const auto canvas = canvasBounds().toFloat();
    const auto useSnap = state_.snapToGrid
                         && layoutMode_ == ModuleInterfaceLayoutMode::graph
                         && !local.mods.isAltDown();
    const auto snap = [useSnap](const float value, const std::uint8_t divisions) {
        return useSnap ? std::round(value * static_cast<float>(divisions))
                             / static_cast<float>(divisions)
                       : value;
    };
    if (resizingControl_) {
        const auto minimumWidth = layoutMode_ == ModuleInterfaceLayoutMode::graph
                                      ? 0.04f : 0.07f;
        const auto minimumHeight = layoutMode_ == ModuleInterfaceLayoutMode::graph
                                       ? 0.06f : 0.12f;
        const auto pointerX = snap(juce::jlimit(
            0.0f, 1.0f,
            (local.position.x - canvas.getX()) / canvas.getWidth()),
            state_.gridColumns);
        const auto pointerY = snap(juce::jlimit(
            0.0f, 1.0f,
            (local.position.y - canvas.getY()) / canvas.getHeight()),
            state_.gridRows);
        const auto startRight = resizeStartControl_.x
                                + resizeStartControl_.width;
        const auto startBottom = resizeStartControl_.y
                                 + resizeStartControl_.height;
        if ((resizeEdges_ & 1) != 0) {
            control->x = juce::jlimit(
                0.0f, startRight - minimumWidth, pointerX);
            control->width = startRight - control->x;
        } else if ((resizeEdges_ & 2) != 0) {
            const auto right = juce::jlimit(
                resizeStartControl_.x + minimumWidth, 1.0f, pointerX);
            control->x = resizeStartControl_.x;
            control->width = right - control->x;
        }
        if ((resizeEdges_ & 4) != 0) {
            control->y = juce::jlimit(
                0.0f, startBottom - minimumHeight, pointerY);
            control->height = startBottom - control->y;
        } else if ((resizeEdges_ & 8) != 0) {
            const auto bottom = juce::jlimit(
                resizeStartControl_.y + minimumHeight, 1.0f, pointerY);
            control->y = resizeStartControl_.y;
            control->height = bottom - control->y;
        }
    } else {
        const auto position = local.position - dragOffset_;
        control->x = juce::jlimit(
            0.0f, 1.0f - control->width,
            snap((position.x - canvas.getX()) / canvas.getWidth(),
                 state_.gridColumns));
        control->y = juce::jlimit(
            0.0f, 1.0f - control->height,
            snap((position.y - canvas.getY()) / canvas.getHeight(),
                 state_.gridRows));
    }
    resized();
    refreshInspector();
    repaint();
}

void ModuleInterfaceEditorComponent::mouseUp(const juce::MouseEvent&)
{
    if (!draggingControl_ && !resizingControl_)
        return;
    draggingControl_ = false;
    resizingControl_ = false;
    resizeEdges_ = 0;
    notifyStateChanged();
    endHistoryGesture();
    refreshInspector();
}

bool ModuleInterfaceEditorComponent::keyPressed(const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown()
        && key.getKeyCode() == 'Z') {
        if (key.getModifiers().isShiftDown())
            redoInterfaceChange();
        else
            undoInterfaceChange();
        return true;
    }
    if (key.getModifiers().isCommandDown()
        && key.getKeyCode() == 'Y') {
        redoInterfaceChange();
        return true;
    }
    if (!editButton_.getToggleState()
        || layoutMode_ != ModuleInterfaceLayoutMode::graph
        || selectedControlId_ == 0)
        return false;

    auto& controls = activeControls();
    const auto found = std::find_if(
        controls.begin(), controls.end(), [this](const auto& control) {
            return control.id == selectedControlId_;
        });
    if (found == controls.end())
        return false;

    if (key.getKeyCode() == juce::KeyPress::deleteKey
        || key.getKeyCode() == juce::KeyPress::backspaceKey) {
        controls.erase(found);
        selectedControlId_ = 0;
    } else if (key.getModifiers().isCommandDown()
               && key.getKeyCode() == 'D') {
        auto copy = *found;
        copy.id = state_.nextControlId++;
        copy.x = juce::jlimit(
            0.0f, 1.0f - copy.width,
            copy.x + 1.0f / static_cast<float>(state_.gridColumns));
        copy.y = juce::jlimit(
            0.0f, 1.0f - copy.height,
            copy.y + 1.0f / static_cast<float>(state_.gridRows));
        controls.push_back(copy);
        selectedControlId_ = copy.id;
    } else {
        const auto keyCode = key.getKeyCode();
        if (keyCode != juce::KeyPress::leftKey
            && keyCode != juce::KeyPress::rightKey
            && keyCode != juce::KeyPress::upKey
            && keyCode != juce::KeyPress::downKey)
            return false;
        const auto fine = key.getModifiers().isAltDown();
        const auto dx = fine ? 0.001f
                             : 1.0f / static_cast<float>(state_.gridColumns);
        const auto dy = fine ? 0.001f
                             : 1.0f / static_cast<float>(state_.gridRows);
        if (keyCode == juce::KeyPress::leftKey)
            found->x = juce::jlimit(0.0f, 1.0f - found->width, found->x - dx);
        else if (keyCode == juce::KeyPress::rightKey)
            found->x = juce::jlimit(0.0f, 1.0f - found->width, found->x + dx);
        else if (keyCode == juce::KeyPress::upKey)
            found->y = juce::jlimit(0.0f, 1.0f - found->height, found->y - dy);
        else
            found->y = juce::jlimit(0.0f, 1.0f - found->height, found->y + dy);
    }

    rebuildSliders();
    refreshInspector();
    notifyStateChanged();
    repaint();
    return true;
}

ModuleWavetableComponent::ModuleWavetableComponent()
{
    setWantsKeyboardFocus(false);
    setShape(cydaw::engine::defaultWavetableShape(),
             juce::dontSendNotification);
}

void ModuleWavetableComponent::setShape(
    const cydaw::engine::WavetableShape& shape,
    const juce::NotificationType notification)
{
    shape_ = shape;
    rebuildCurves();
    repaint();
    if (notification != juce::dontSendNotification && onShapeChanged)
        onShapeChanged(shape_);
}

void ModuleWavetableComponent::setMorphPosition(const float position)
{
    const auto clamped = juce::jlimit(0.0f, 1.0f, position);
    if (std::abs(clamped - morphPosition_) < 0.0005f)
        return;
    morphPosition_ = clamped;
    repaint();
}

void ModuleWavetableComponent::selectFrame(const std::size_t frame)
{
    const auto clamped = std::min(frame,
                                  cydaw::engine::WavetableShape::frameCount - 1);
    if (clamped == selectedFrame_)
        return;
    selectedFrame_ = clamped;
    repaint();
}

void ModuleWavetableComponent::applyPreset(const Preset preset,
                                          const bool allFrames)
{
    if (preset == Preset::builtIn) {
        const auto built = cydaw::engine::defaultWavetableShape();
        if (allFrames)
            shape_ = built;
        else
            shape_.harmonics[selectedFrame_] = built.harmonics[selectedFrame_];
        rebuildCurves();
        repaint();
        if (onShapeChanged)
            onShapeChanged(shape_);
        return;
    }
    // Applied to every frame, the brightness still has to change across the
    // table or the morph would have nothing to travel through. The frame
    // index decides how many partials survive, exactly as the built-in bank
    // does — what changes is which family they come from.
    static constexpr std::array<std::size_t,
                                cydaw::engine::WavetableShape::frameCount>
        partials { 1, 2, 3, 5, 8, 14, 24, 40 };
    const auto shapeFrame = [&preset](
        cydaw::engine::WavetableShape::HarmonicRow& row,
        const std::size_t limit) {
        row = {};
        for (std::size_t harmonic = 0;
             harmonic < cydaw::engine::WavetableShape::harmonicCount
             && harmonic < limit;
             ++harmonic) {
            const auto index = static_cast<float>(harmonic + 1);
            switch (preset) {
            case Preset::sine:
                row[harmonic] = harmonic == 0 ? 1.0f : 0.0f;
                break;
            case Preset::sawtooth:
                row[harmonic] = 1.0f / index;
                break;
            case Preset::square:
                row[harmonic] = harmonic % 2 == 0 ? 1.0f / index : 0.0f;
                break;
            default:
                row[harmonic] = harmonic % 2 == 0
                    ? (harmonic % 4 == 0 ? 1.0f : -1.0f) / (index * index)
                    : 0.0f;
                break;
            }
        }
    };
    if (allFrames)
        for (std::size_t frame = 0;
             frame < cydaw::engine::WavetableShape::frameCount; ++frame)
            shapeFrame(shape_.harmonics[frame], partials[frame]);
    else
        shapeFrame(shape_.harmonics[selectedFrame_],
                   cydaw::engine::WavetableShape::harmonicCount);
    rebuildCurves();
    repaint();
    if (onShapeChanged)
        onShapeChanged(shape_);
}

void ModuleWavetableComponent::showPresetMenu()
{
    const std::array<std::pair<Preset, const char*>, 5> shapes { {
        { Preset::sine, "module.ui.wavetableSine" },
        { Preset::sawtooth, "module.ui.wavetableSaw" },
        { Preset::square, "module.ui.wavetableSquare" },
        { Preset::triangle, "module.ui.wavetableTriangle" },
        { Preset::builtIn, "module.ui.wavetableBuiltIn" } } };
    juce::PopupMenu menu;
    juce::PopupMenu everyFrame;
    juce::PopupMenu thisFrame;
    for (std::size_t index = 0; index < shapes.size(); ++index) {
        everyFrame.addItem(static_cast<int>(index) + 1,
                           tr(shapes[index].second));
        thisFrame.addItem(static_cast<int>(index) + 101,
                          tr(shapes[index].second));
    }
    // The whole table first: it is what someone reaches for far more often
    // than one frame.
    menu.addSubMenu(tr("module.ui.wavetableAllFrames"), everyFrame);
    menu.addSubMenu(tr("module.ui.wavetableThisFrame"), thisFrame);
    if (onImportRequested) {
        menu.addSeparator();
        menu.addItem(201, tr("module.ui.wavetableImport"));
    }
    juce::Component::SafePointer<ModuleWavetableComponent> safeThis(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(this),
        [safeThis, shapes](const int result) {
            if (safeThis == nullptr || result == 0)
                return;
            if (result == 201) {
                if (safeThis->onImportRequested)
                    safeThis->onImportRequested();
                return;
            }
            const auto all = result < 100;
            const auto index = static_cast<std::size_t>(
                (all ? result : result - 100) - 1);
            if (index >= shapes.size())
                return;
            if (safeThis->onGestureStart)
                safeThis->onGestureStart();
            safeThis->applyPreset(shapes[index].first, all);
            if (safeThis->onGestureEnd)
                safeThis->onGestureEnd();
        });
}

void ModuleWavetableComponent::rebuildCurves()
{
    for (std::size_t frame = 0;
         frame < cydaw::engine::WavetableShape::frameCount; ++frame)
        cydaw::engine::renderWavetableFrame(shape_.harmonics[frame],
                                            curves_[frame].data(),
                                            curves_[frame].size());
    drawn_ = curves_[selectedFrame_];
}

void ModuleWavetableComponent::resized()
{
    repaint();
}

juce::Rectangle<float> ModuleWavetableComponent::waveArea() const
{
    auto bounds = getLocalBounds().toFloat().reduced(4.0f, 3.0f);
    bounds.removeFromBottom(14.0f);
    return bounds;
}

juce::Rectangle<float> ModuleWavetableComponent::frameStrip() const
{
    auto bounds = getLocalBounds().toFloat().reduced(4.0f, 3.0f);
    return bounds.removeFromBottom(12.0f);
}

int ModuleWavetableComponent::frameAtX(const float x) const
{
    const auto strip = frameStrip();
    if (strip.getWidth() <= 1.0f)
        return 0;
    const auto slot = (x - strip.getX()) / strip.getWidth()
                      * static_cast<float>(
                          cydaw::engine::WavetableShape::frameCount);
    return juce::jlimit(
        0, static_cast<int>(cydaw::engine::WavetableShape::frameCount) - 1,
        static_cast<int>(slot));
}

void ModuleWavetableComponent::drawInto(const juce::Point<float> point)
{
    const auto area = waveArea();
    if (area.getWidth() <= 1.0f || area.getHeight() <= 1.0f)
        return;
    const auto normalisedX = juce::jlimit(
        0.0f, 0.9999f, (point.x - area.getX()) / area.getWidth());
    const auto index = static_cast<int>(
        normalisedX * static_cast<float>(drawResolution));
    const auto value = juce::jlimit(
        -1.0f, 1.0f,
        (1.0f - 2.0f * (point.y - area.getY()) / area.getHeight())
            / editScale);
    // A fast drag reports a handful of points across the whole width. Joining
    // them keeps the drawn cycle continuous instead of leaving the old shape
    // showing through the gaps.
    if (lastDrawIndex_ >= 0 && lastDrawIndex_ != index) {
        const auto from = std::min(lastDrawIndex_, index);
        const auto to = std::max(lastDrawIndex_, index);
        const auto startValue = drawn_[static_cast<std::size_t>(lastDrawIndex_)];
        for (auto step = from; step <= to; ++step) {
            const auto blend = static_cast<float>(step - from)
                               / static_cast<float>(std::max(1, to - from));
            drawn_[static_cast<std::size_t>(step)]
                = lastDrawIndex_ < index
                      ? startValue + (value - startValue) * blend
                      : value + (startValue - value) * blend;
        }
    }
    drawn_[static_cast<std::size_t>(index)] = value;
    lastDrawIndex_ = index;
}

void ModuleWavetableComponent::commitDrawnFrame()
{
    shape_.harmonics[selectedFrame_]
        = cydaw::engine::analyseWavetableFrame(drawn_.data(), drawn_.size());
    cydaw::engine::renderWavetableFrame(shape_.harmonics[selectedFrame_],
                                        curves_[selectedFrame_].data(),
                                        curves_[selectedFrame_].size());
    repaint();
    if (onShapeChanged)
        onShapeChanged(shape_);
}

void ModuleWavetableComponent::mouseDown(const juce::MouseEvent& event)
{
    const auto point = event.position;
    if (event.mods.isPopupMenu()) {
        showPresetMenu();
        return;
    }
    if (frameStrip().contains(point)) {
        selectFrame(static_cast<std::size_t>(frameAtX(point.x)));
        drawn_ = curves_[selectedFrame_];
        return;
    }
    if (!waveArea().contains(point))
        return;
    drawing_ = true;
    lastDrawIndex_ = -1;
    if (onGestureStart)
        onGestureStart();
    drawInto(point);
    commitDrawnFrame();
}

void ModuleWavetableComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (!drawing_)
        return;
    drawInto(event.position);
    commitDrawnFrame();
}

void ModuleWavetableComponent::mouseUp(const juce::MouseEvent&)
{
    if (!drawing_)
        return;
    drawing_ = false;
    lastDrawIndex_ = -1;
    if (onGestureEnd)
        onGestureEnd();
}

void ModuleWavetableComponent::paint(juce::Graphics& graphics)
{
    const auto bounds = getLocalBounds().toFloat();
    graphics.setColour(juce::Colour(0xff11151a));
    graphics.fillRoundedRectangle(bounds, 4.0f);

    const auto area = waveArea();
    if (area.getWidth() <= 2.0f || area.getHeight() <= 2.0f)
        return;

    const auto middle = area.getCentreY();
    graphics.setColour(juce::Colour(0xff20262d));
    graphics.drawHorizontalLine(static_cast<int>(middle), area.getX(),
                                area.getRight());

    // Distance from the frame being edited decides everything about how a
    // frame is drawn: how far back it sits, how narrow it gets, how loud it
    // looks and how present it is. That is what turns eight overlapping
    // curves into a stack you can count.
    const auto count = cydaw::engine::WavetableShape::frameCount;
    const auto furthest = std::max(
        selectedFrame_, count - 1 - selectedFrame_);
    const auto step = area.getHeight() * 0.42f
                      / static_cast<float>(std::max<std::size_t>(furthest, 1));
    const auto pathFor = [&area, middle](const Curve& curve, const float lift,
                                         const float shift, const float inset,
                                         const float scale) {
        juce::Path path;
        const auto left = area.getX() + inset + shift;
        const auto width = std::max(4.0f, area.getWidth() - inset * 2.0f);
        for (std::size_t index = 0; index < curve.size(); ++index) {
            const auto x = left + width * static_cast<float>(index)
                                      / static_cast<float>(curve.size() - 1);
            const auto y = middle - lift
                           - curve[index] * area.getHeight() * 0.5f * scale;
            if (index == 0)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        }
        return path;
    };

    const auto morphFrame = juce::jlimit(
        0.0f, static_cast<float>(count - 1),
        morphPosition_ * static_cast<float>(count - 1));
    const auto morphIndex = static_cast<std::size_t>(std::lround(morphFrame));

    // Furthest first, so nearer frames cover the ones behind them.
    std::array<std::size_t, cydaw::engine::WavetableShape::frameCount> order {};
    for (std::size_t index = 0; index < count; ++index)
        order[index] = index;
    std::sort(order.begin(), order.end(),
              [this](const std::size_t left, const std::size_t right) {
                  const auto distance = [this](const std::size_t frame) {
                      return frame > selectedFrame_ ? frame - selectedFrame_
                                                    : selectedFrame_ - frame;
                  };
                  return distance(left) > distance(right);
              });

    for (const auto frame : order) {
        const auto signedDepth = static_cast<float>(frame)
                                 - static_cast<float>(selectedFrame_);
        const auto distance = std::abs(signedDepth);
        const auto fade = distance
                          / static_cast<float>(std::max<std::size_t>(furthest,
                                                                     1));
        if (frame == selectedFrame_)
            continue;
        const auto lift = -signedDepth * step;
        // Sideways as well as back. Depth read from a vertical step alone is
        // ambiguous — it looks like one wave wobbling — but a stack of sheets
        // sliding across each other is unmistakable.
        const auto shift = -signedDepth * area.getWidth() * 0.030f;
        const auto inset = fade * area.getWidth() * 0.055f;
        const auto scale = 0.30f - fade * 0.11f;
        const auto colour = frame == morphIndex
                                ? juce::Colour(0xff4fd3ae)
                                : juce::Colour(0xff9d91ff);
        graphics.setColour(colour.withAlpha(0.52f - fade * 0.30f));
        graphics.strokePath(pathFor(curves_[frame], lift, shift, inset, scale),
                            juce::PathStrokeType(frame == morphIndex ? 1.6f
                                                                     : 1.1f));
    }
    // The frame being edited: flat, full width, in front. Drawing happens in
    // this frame's own rectangle, so it is the one that must not be skewed.
    if (selectedFrame_ == morphIndex) {
        graphics.setColour(juce::Colour(0xff4fd3ae).withAlpha(0.35f));
        graphics.strokePath(
            pathFor(curves_[selectedFrame_], 0.0f, 0.0f, 0.0f,
                    editScale + 0.04f),
            juce::PathStrokeType(4.0f));
    }
    graphics.setColour(juce::Colour(0xffcfc6ff));
    graphics.strokePath(
        pathFor(curves_[selectedFrame_], 0.0f, 0.0f, 0.0f, editScale),
        juce::PathStrokeType(2.0f));

    const auto strip = frameStrip();
    const auto slot = strip.getWidth() / static_cast<float>(count);
    for (std::size_t frame = 0; frame < count; ++frame) {
        const auto cell = juce::Rectangle<float>(
            strip.getX() + slot * static_cast<float>(frame), strip.getY(),
            slot, strip.getHeight()).reduced(1.0f, 2.0f);
        const auto reading = std::abs(static_cast<float>(frame) - morphFrame)
                             < 0.5f;
        graphics.setColour(frame == selectedFrame_
                               ? juce::Colour(0xffcfc6ff)
                               : (reading ? juce::Colour(0xff4fd3ae)
                                          : juce::Colour(0xff2b323b)));
        graphics.fillRoundedRectangle(cell, 1.5f);
    }
}

} // namespace cydaw::app
