#pragma once

#include "Localization.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <cmath>
#include <functional>
#include <memory>
#include <optional>

namespace cydaw::app {

// Common interaction contract for every rotary control in CyDAW.
// Vertical movement is the default; the per-control context menu offers a
// horizontal alternative without adding permanent UI chrome.
class KnobSlider : public juce::Slider {
public:
    // Built without a text box: JUCE's default constructor makes one (a
    // Label with its colours and font) that the owner's setTextBoxStyle
    // then throws away and remakes, and a surface of hundreds of knobs is
    // rebuilt at every change of view.
    KnobSlider()
        : juce::Slider(juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox)
    {
        setVelocityBasedMode(false);
        // Keep the same physical drag distance even when the graph is zoomed
        // out and the child component becomes smaller.
        setMouseDragSensitivity(180);
    }

    void setDefaultValue(const double value)
    {
        defaultValue_ = value;
        setDoubleClickReturnValue(true, defaultValue_);
    }

    [[nodiscard]] double defaultValue() const noexcept
    {
        return defaultValue_;
    }

    // The range a modulation sweeps this knob through, in the knob's own
    // units, drawn as a ring around the dial. Empty when nothing modulates it.
    // Inverted, the sweep runs the other way — the source's high end lands
    // on the ring's low end — and the ring says so with its colour.
    void setModulationRange(const std::optional<juce::Range<double>>& range,
                            const bool inverted = false)
    {
        if (modulationRange_ == range && modulationInverted_ == inverted)
            return;
        modulationRange_ = range;
        modulationInverted_ = inverted;
        repaint();
    }

    [[nodiscard]] bool modulationInverted() const noexcept
    {
        return modulationInverted_;
    }

    // Half the ring, signed: negative when the sweep is turned over.
    [[nodiscard]] double modulationDepth() const noexcept
    {
        if (!modulationRange_.has_value())
            return 0.0;
        const auto half = modulationRange_->getLength() * 0.5;
        return modulationInverted_ ? -half : half;
    }

    // Dragging the ring itself sets the depth: up widens the sweep, down
    // narrows it, and through zero it turns over. The knob's value is not
    // touched. The three hooks bracket the gesture the way a knob drag's do.
    std::function<void()> onModulationDepthDragStart;
    std::function<void(double)> onModulationDepthDrag;
    std::function<void()> onModulationDepthDragEnd;
    // The ring's menu: the modulation goes, cable and range node together.
    std::function<void()> onModulationRemove;

    // Turning the sweep over is a depth gesture with the sign flipped, so
    // it is one undo entry like a drag on the ring.
    void invertModulation()
    {
        if (!modulationRange_.has_value() || !onModulationDepthDrag)
            return;
        beginDepthDrag();
        onModulationDepthDrag(-depthAtDragStart_);
        endDepthDrag();
    }

    // What the ring says about itself: the depth in the knob's units, and
    // whether the sweep is turned over.
    [[nodiscard]] juce::String modulationDepthReadout() const
    {
        auto text = tr("knob.modulationDepth", "depth",
                       juce::String(std::abs(modulationDepth()),
                                    getNumDecimalPlacesToDisplay())
                           + getTextValueSuffix());
        if (modulationInverted_)
            text << " \u00b7 " << tr("knob.modulationInverted");
        return text;
    }

    // The parameter's tooltip is the knob's; the ring has its own while the
    // mouse is over it, and hands the knob's back afterwards.
    void setTooltip(const juce::String& tooltip) override
    {
        baseTooltip_ = tooltip;
        juce::SettableTooltipClient::setTooltip(tooltip);
    }

    [[nodiscard]] bool depthBubbleShownForTest() const noexcept
    {
        return depthBubble_ != nullptr && depthBubble_->isVisible();
    }

    // Whether a point lies on the ring as the look and feel draws it: just
    // outside the dial, a few pixels either side.
    [[nodiscard]] bool hitsModulationRing(const juce::Point<float> position)
    {
        if (!modulationRange_.has_value())
            return false;
        const auto area = getLookAndFeel().getSliderLayout(*this)
                              .sliderBounds.toFloat();
        const auto radius = std::min(area.getWidth(), area.getHeight()) * 0.5f
                            - 4.0f;
        const auto distance = position.getDistanceFrom(area.getCentre());
        return std::abs(distance - (radius + 2.6f)) <= 5.0f;
    }

    [[nodiscard]] bool draggingModulationDepth() const noexcept
    {
        return draggingDepth_;
    }

    // A ring drag without a mouse, for the suite: the same arithmetic the
    // drag runs, from press to release.
    void dragModulationRingForTest(const float deltaY, const bool fine = false)
    {
        beginDepthDrag();
        dragDepthBy(deltaY, fine);
        endDepthDrag();
    }

    // Where the ring is, for the suite's hit test: a point on it and the
    // dial's centre, which is not.
    [[nodiscard]] std::pair<juce::Point<float>, juce::Point<float>>
    modulationRingPointsForTest()
    {
        const auto area = getLookAndFeel().getSliderLayout(*this)
                              .sliderBounds.toFloat();
        const auto radius = std::min(area.getWidth(), area.getHeight()) * 0.5f
                            - 4.0f;
        return { area.getCentre().translated(0.0f, -(radius + 2.6f)),
                 area.getCentre() };
    }

    void mouseMove(const juce::MouseEvent& event) override
    {
        const auto onRing = onModulationDepthDrag
                            && hitsModulationRing(event.position);
        setMouseCursor(onRing ? juce::MouseCursor::UpDownResizeCursor
                              : juce::MouseCursor::NormalCursor);
        juce::SettableTooltipClient::setTooltip(
            onRing ? modulationDepthReadout() + "\n" + tr("knob.ringHint")
                   : baseTooltip_);
        juce::Slider::mouseMove(event);
    }

    void mouseExit(const juce::MouseEvent& event) override
    {
        juce::SettableTooltipClient::setTooltip(baseTooltip_);
        juce::Slider::mouseExit(event);
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (!draggingDepth_) {
            juce::Slider::mouseDrag(event);
            return;
        }
        dragDepthBy(event.position.y - depthDragStart_.y,
                    event.mods.isShiftDown());
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        if (!draggingDepth_) {
            juce::Slider::mouseUp(event);
            return;
        }
        endDepthDrag();
    }

    [[nodiscard]] const std::optional<juce::Range<double>>&
    modulationRange() const noexcept
    {
        return modulationRange_;
    }

    // Where the modulation is right now, in the knob's units: a dot running
    // along the ring. Arrives thirty times a second while the sound runs,
    // and clears when it stops.
    void setModulationPosition(const std::optional<double>& position)
    {
        if (modulationPosition_.has_value() == position.has_value()
            && (!position.has_value()
                || std::abs(*modulationPosition_ - *position) < 1.0e-4))
            return;
        modulationPosition_ = position;
        repaint();
    }

    [[nodiscard]] const std::optional<double>& modulationPosition() const noexcept
    {
        return modulationPosition_;
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (onModulationDepthDrag && hitsModulationRing(event.position)) {
            if (event.mods.isPopupMenu()) {
                showRingMenu();
                return;
            }
            depthDragStart_ = event.position;
            beginDepthDrag();
            return;
        }
        if (!event.mods.isPopupMenu()) {
            juce::Slider::mouseDown(event);
            return;
        }

        juce::PopupMenu menu;
        const auto horizontal = getSliderStyle()
                                == juce::Slider::RotaryHorizontalDrag;
        menu.addItem(1, tr("knob.dragVertical"), true, !horizontal);
        menu.addItem(2, tr("knob.dragHorizontal"), true, horizontal);
        const juce::Component::SafePointer<KnobSlider> safe(this);
        menu.showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(this),
            [safe](const int result) {
                if (safe == nullptr || result == 0)
                    return;
                safe->setSliderStyle(
                    result == 2 ? juce::Slider::RotaryHorizontalDrag
                                : juce::Slider::RotaryVerticalDrag);
            });
    }

private:
    void showRingMenu()
    {
        juce::PopupMenu menu;
        menu.addItem(1, tr("knob.removeModulation"),
                     static_cast<bool>(onModulationRemove));
        menu.addItem(2, tr("knob.invertModulation"));
        const juce::Component::SafePointer<KnobSlider> safe(this);
        menu.showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(this),
            [safe](const int result) {
                if (safe == nullptr)
                    return;
                if (result == 1 && safe->onModulationRemove)
                    safe->onModulationRemove();
                else if (result == 2)
                    safe->invertModulation();
            });
    }

    // The readout, in a bubble beside the knob, for as long as the gesture
    // lasts and a moment after. One bubble, moved and reworded, not one per
    // mouse event.
    void showDepthBubble(const int millisecondsBeforeHiding)
    {
        auto* host = getTopLevelComponent();
        if (host == nullptr)
            return;
        if (depthBubble_ == nullptr) {
            depthBubble_ = std::make_unique<juce::BubbleMessageComponent>();
            depthBubble_->setAlwaysOnTop(true);
        }
        if (depthBubble_->getParentComponent() != host)
            host->addChildComponent(*depthBubble_);
        juce::AttributedString text(modulationDepthReadout());
        text.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        text.setColour(juce::Colour(0xffe9fff7));
        depthBubble_->showAt(this, text, millisecondsBeforeHiding, false, false);
    }

    void beginDepthDrag()
    {
        draggingDepth_ = true;
        depthAtDragStart_ = modulationDepth();
        if (onModulationDepthDragStart)
            onModulationDepthDragStart();
        showDepthBubble(-1);
        repaint();
    }

    // The same distance as the knob's own drag for the whole of its range,
    // so the ring and the dial answer the hand alike; shift for a tenth.
    void dragDepthBy(const float deltaY, const bool fine)
    {
        const auto span = getRange().getLength();
        const auto pixels = std::max(1, getMouseDragSensitivity());
        const auto depth = juce::jlimit(
            -span, span,
            depthAtDragStart_
                - static_cast<double>(deltaY) / pixels * span * (fine ? 0.1 : 1.0));
        if (onModulationDepthDrag)
            onModulationDepthDrag(depth);
        showDepthBubble(-1);
    }

    void endDepthDrag()
    {
        draggingDepth_ = false;
        if (onModulationDepthDragEnd)
            onModulationDepthDragEnd();
        showDepthBubble(650);
        repaint();
    }

    double defaultValue_ {};
    std::optional<juce::Range<double>> modulationRange_;
    bool modulationInverted_ {};
    std::optional<double> modulationPosition_;
    bool draggingDepth_ {};
    double depthAtDragStart_ {};
    juce::Point<float> depthDragStart_;
    juce::String baseTooltip_;
    std::unique_ptr<juce::BubbleMessageComponent> depthBubble_;
};

} // namespace cydaw::app
