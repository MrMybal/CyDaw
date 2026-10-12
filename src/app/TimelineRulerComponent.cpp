#include "TimelineRulerComponent.h"
#include "PaintProfile.h"
#include "Localization.h"

#include <algorithm>
#include <cmath>

namespace cydaw::app {

void TimelineRulerComponent::setTimeline(const double lengthSeconds, const double tempoBpm)
{
    lengthSeconds_ = std::max(1.0, lengthSeconds);
    tempoBpm_ = juce::jlimit(20.0, 400.0, tempoBpm);
    visibleStartSeconds_ = juce::jlimit(
        0.0, std::max(0.0, lengthSeconds_ - 0.000001),
        visibleStartSeconds_);
    visibleEndSeconds_ = juce::jlimit(
        visibleStartSeconds_ + 0.000001, lengthSeconds_,
        visibleEndSeconds_);
    repaint();
}

void TimelineRulerComponent::setVisibleRange(const double startSeconds,
                                             const double endSeconds)
{
    visibleStartSeconds_ = juce::jlimit(
        0.0, std::max(0.0, lengthSeconds_ - 0.000001),
        startSeconds);
    visibleEndSeconds_ = juce::jlimit(
        visibleStartSeconds_ + 0.000001, lengthSeconds_, endSeconds);
    repaint();
}

void TimelineRulerComponent::setPlayhead(const double seconds)
{
    const auto next = std::max(0.0, seconds);
    if (std::abs(playheadSeconds_ - next) < 1.0e-9)
        return;
    // The ruler is repainted on every transport tick. Only the column the
    // playhead left and the one it moved to actually changed; the bar numbers
    // and tick marks either side of them did not.
    const auto column = [this](const double time) {
        auto strip = getLocalBounds();
        strip.setX(juce::roundToInt(xFromTime(time)) - 3);
        strip.setWidth(8);
        return strip.getIntersection(getLocalBounds());
    };
    const auto previous = column(playheadSeconds_);
    playheadSeconds_ = next;
    const auto moved = column(playheadSeconds_);
    if (previous.isEmpty() && moved.isEmpty())
        return;
    if (!previous.isEmpty() && !moved.isEmpty()
        && std::abs(previous.getX() - moved.getX()) <= previous.getWidth() * 4) {
        repaint(previous.getUnion(moved));
        return;
    }
    if (!previous.isEmpty())
        repaint(previous);
    if (!moved.isEmpty())
        repaint(moved);
}

void TimelineRulerComponent::setSeekHandler(std::function<void(double)> handler)
{
    seekHandler_ = std::move(handler);
}

void TimelineRulerComponent::setLoopRange(const double startSeconds,
                                          const double endSeconds,
                                          const bool enabled)
{
    loopStartSeconds_ = std::max(0.0, startSeconds);
    loopEndSeconds_ = std::max(loopStartSeconds_ + 0.000001,
                               endSeconds);
    loopEnabled_ = enabled;
    repaint();
}

void TimelineRulerComponent::setTimeSelection(
    const double startSeconds, const double endSeconds,
    const bool visible)
{
    timeSelectionStartSeconds_ = std::min(startSeconds, endSeconds);
    timeSelectionEndSeconds_ = std::max(startSeconds, endSeconds);
    timeSelectionVisible_ = visible;
    repaint();
}

void TimelineRulerComponent::setLoopRangeHandler(
    std::function<void(double, double)> handler)
{
    loopRangeHandler_ = std::move(handler);
}

void TimelineRulerComponent::setZoomHandler(
    std::function<void(double, double)> handler)
{
    zoomHandler_ = std::move(handler);
}

void TimelineRulerComponent::setPanHandler(
    std::function<void(double)> handler)
{
    panHandler_ = std::move(handler);
}

void TimelineRulerComponent::setNavigationTool(
    const NavigationTool tool)
{
    navigationTool_ = tool;
    setMouseCursor(tool == NavigationTool::hand
                       ? juce::MouseCursor::DraggingHandCursor
                       : tool == NavigationTool::zoom
                             ? juce::MouseCursor::CrosshairCursor
                             : juce::MouseCursor::NormalCursor);
}

void TimelineRulerComponent::paint(juce::Graphics& graphics)
{
    const paint_profile::Scoped paintScope(paint_profile::counters().ruler);
    graphics.fillAll(juce::Colour(0xff2b2e31));
    graphics.setColour(juce::Colour(0xff202225));
    graphics.fillRect(0, 0, trackHeaderWidth, getHeight());
    graphics.setColour(juce::Colour(0xff50545a));
    graphics.drawVerticalLine(trackHeaderWidth, 0.0f, static_cast<float>(getHeight()));
    graphics.drawHorizontalLine(getHeight() - 1, 0.0f, static_cast<float>(getWidth()));

    graphics.setColour(juce::Colour(0xffb6bbc0));
    graphics.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    graphics.drawText(tr("timeline.tracks"), 12, 0,
                      trackHeaderWidth - 20, getHeight(),
                      juce::Justification::centredLeft);

    const auto timelineWidth = std::max(1, getWidth() - trackHeaderWidth);
    const auto timelineLeft = static_cast<float>(trackHeaderWidth);
    const auto timelineRight = static_cast<float>(getWidth());
    if (timeSelectionVisible_
        && timeSelectionEndSeconds_ > visibleStartSeconds_
        && timeSelectionStartSeconds_ < visibleEndSeconds_) {
        const auto selectionStartX = juce::jlimit(
            timelineLeft, timelineRight,
            xFromTime(timeSelectionStartSeconds_));
        const auto selectionEndX = juce::jlimit(
            timelineLeft, timelineRight,
            xFromTime(timeSelectionEndSeconds_));
        if (selectionEndX > selectionStartX) {
            graphics.setColour(
                juce::Colour(0xff53d0c2).withAlpha(0.13f));
            graphics.fillRect(selectionStartX, 0.0f,
                              selectionEndX - selectionStartX,
                              static_cast<float>(getHeight()));
            graphics.setColour(
                juce::Colour(0xff8ef5de).withAlpha(0.76f));
            graphics.fillRect(selectionStartX, 0.0f,
                              selectionEndX - selectionStartX, 3.0f);
        }
    }
    const auto loopStartX = juce::jlimit(
        timelineLeft, timelineRight, xFromTime(loopStartSeconds_));
    const auto loopEndX = juce::jlimit(
        timelineLeft, timelineRight, xFromTime(loopEndSeconds_));
    if (loopEndX > loopStartX) {
        const auto loopColour = juce::Colour(0xffff8a00);
        graphics.setColour(loopColour.withAlpha(loopEnabled_ ? 0.13f : 0.055f));
        graphics.fillRect(loopStartX, 0.0f, loopEndX - loopStartX,
                          static_cast<float>(getHeight()));
        graphics.setColour(loopColour.withAlpha(loopEnabled_ ? 0.92f : 0.38f));
        graphics.fillRect(loopStartX, 0.0f, loopEndX - loopStartX, 5.0f);
    }
    const auto secondsPerBeat = 60.0 / tempoBpm_;
    const auto visibleDuration = std::max(
        0.000001, visibleEndSeconds_ - visibleStartSeconds_);
    const auto visibleBeats = visibleDuration / secondsPerBeat;
    auto beatStep = 1;
    while (static_cast<double>(timelineWidth) / std::max(1.0, visibleBeats / beatStep) < 32.0)
        beatStep *= 2;

    const auto firstBeat = std::max(
        0, static_cast<int>(std::floor(
               visibleStartSeconds_ / secondsPerBeat)));
    const auto lastBeat = static_cast<int>(std::ceil(
        visibleEndSeconds_ / secondsPerBeat));
    for (int beat = firstBeat; beat <= lastBeat; ++beat) {
        const auto seconds = static_cast<double>(beat) * secondsPerBeat;
        const auto x = xFromTime(seconds);
        const auto barLine = beat % 4 == 0;
        graphics.setColour(juce::Colour(barLine ? 0xff777c80 : 0xff4b4f53));
        const auto top = barLine ? 13.0f : 19.0f;
        graphics.drawVerticalLine(static_cast<int>(std::round(x)), top, static_cast<float>(getHeight()));
        if (beat % beatStep == 0) {
            const auto bar = beat / 4 + 1;
            const auto beatInBar = beat % 4 + 1;
            graphics.setColour(juce::Colour(barLine ? 0xffe0e2e4 : 0xffa6abb0));
            graphics.setFont(juce::FontOptions(barLine ? 10.5f : 9.0f, barLine ? juce::Font::bold
                                                                               : juce::Font::plain));
            graphics.drawText(juce::String(bar) + "." + juce::String(beatInBar),
                              static_cast<int>(x) + 4, 1, 48, 14, juce::Justification::centredLeft);
        }
    }

    if (loopEndX > loopStartX) {
        const auto loopColour = juce::Colour(0xffff8a00);
        graphics.setColour(loopColour.withAlpha(loopEnabled_ ? 0.95f : 0.42f));
        graphics.fillRect(loopStartX - 1.0f, 0.0f, 2.0f,
                          static_cast<float>(getHeight()));
        graphics.fillRect(loopEndX - 1.0f, 0.0f, 2.0f,
                          static_cast<float>(getHeight()));
        juce::Path startMarker;
        startMarker.addTriangle(loopStartX, 5.0f, loopStartX + 6.0f, 5.0f,
                                loopStartX, 11.0f);
        graphics.fillPath(startMarker);
        juce::Path endMarker;
        endMarker.addTriangle(loopEndX, 5.0f, loopEndX - 6.0f, 5.0f,
                              loopEndX, 11.0f);
        graphics.fillPath(endMarker);
    }

    if (playheadSeconds_ >= visibleStartSeconds_
        && playheadSeconds_ <= visibleEndSeconds_) {
        const auto playheadX = xFromTime(playheadSeconds_);
        graphics.setColour(juce::Colour(0xffff8a00));
        graphics.fillRect(playheadX - 1.0f, 0.0f, 2.0f,
                          static_cast<float>(getHeight()));
        juce::Path marker;
        marker.addTriangle(playheadX - 5.0f, 0.0f,
                           playheadX + 5.0f, 0.0f,
                           playheadX, 6.0f);
        graphics.fillPath(marker);
    }
}

void TimelineRulerComponent::mouseDown(const juce::MouseEvent& event)
{
    if (event.position.x < static_cast<float>(trackHeaderWidth))
        return;
    if (event.mods.isMiddleButtonDown()
        || (navigationTool_ == NavigationTool::hand
            && event.mods.isLeftButtonDown())) {
        dragMode_ = DragMode::pan;
        lastPanX_ = event.position.x;
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        return;
    }
    if (navigationTool_ == NavigationTool::zoom
        && event.mods.isLeftButtonDown()) {
        dragMode_ = DragMode::zoom;
        lastPanX_ = event.position.x;
        lastZoomScreenY_ = event.getScreenPosition().y;
        zoomAnchorSeconds_ = timeFromX(event.position.x);
        zoomGestureMoved_ = false;
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
        return;
    }
    const auto time = timeFromX(event.position.x);
    if (event.mods.isShiftDown()) {
        dragMode_ = DragMode::createLoop;
        loopDragAnchorSeconds_ = time;
        updateLoopRange(event.position.x);
        return;
    }
    const auto startDistance = std::abs(event.position.x
                                        - xFromTime(loopStartSeconds_));
    const auto endDistance = std::abs(event.position.x
                                      - xFromTime(loopEndSeconds_));
    if (startDistance <= 7.0f || endDistance <= 7.0f) {
        dragMode_ = startDistance <= endDistance ? DragMode::loopStart
                                                  : DragMode::loopEnd;
        updateLoopRange(event.position.x);
        return;
    }
    dragMode_ = DragMode::none;
    seek(event.position.x);
}

void TimelineRulerComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (dragMode_ == DragMode::pan) {
        const auto width = std::max(1, getWidth() - trackHeaderWidth);
        const auto visibleDuration = std::max(
            0.000001, visibleEndSeconds_ - visibleStartSeconds_);
        if (panHandler_)
            panHandler_(-static_cast<double>(event.position.x - lastPanX_)
                        / static_cast<double>(width) * visibleDuration);
        lastPanX_ = event.position.x;
        return;
    }
    if (dragMode_ == DragMode::zoom) {
        const auto screenY = event.getScreenPosition().y;
        const auto verticalDelta = screenY - lastZoomScreenY_;
        zoomGestureMoved_ = zoomGestureMoved_
                            || std::abs(verticalDelta) >= 1
                            || std::abs(event.position.x - lastPanX_) >= 1.0f;
        if (zoomHandler_ && verticalDelta != 0)
            zoomHandler_(std::pow(1.025, -static_cast<double>(verticalDelta)),
                         zoomAnchorSeconds_);
        const auto width = std::max(1, getWidth() - trackHeaderWidth);
        const auto visibleDuration = std::max(
            0.000001, visibleEndSeconds_ - visibleStartSeconds_);
        if (panHandler_)
            panHandler_(-static_cast<double>(event.position.x - lastPanX_)
                        / static_cast<double>(width) * visibleDuration);
        lastPanX_ = event.position.x;
        lastZoomScreenY_ = screenY;
        return;
    }
    if (dragMode_ == DragMode::none)
        seek(event.position.x);
    else
        updateLoopRange(event.position.x);
}

void TimelineRulerComponent::mouseUp(const juce::MouseEvent&)
{
    if (dragMode_ == DragMode::zoom && !zoomGestureMoved_ && zoomHandler_)
        zoomHandler_(2.0, zoomAnchorSeconds_);
    dragMode_ = DragMode::none;
    setNavigationTool(navigationTool_);
}

void TimelineRulerComponent::mouseWheelMove(
    const juce::MouseEvent& event,
    const juce::MouseWheelDetails& wheel)
{
    if (event.position.x < static_cast<float>(trackHeaderWidth))
        return;
    const auto visibleDuration = std::max(
        0.000001, visibleEndSeconds_ - visibleStartSeconds_);
    if (event.mods.isShiftDown()
        || std::abs(wheel.deltaX) > std::abs(wheel.deltaY)) {
        if (panHandler_) {
            const auto amount = std::abs(wheel.deltaX) > 0.0001f
                                    ? wheel.deltaX
                                    : wheel.deltaY;
            panHandler_(-static_cast<double>(amount)
                        * visibleDuration * 0.65);
        }
        return;
    }
    if (zoomHandler_ && std::abs(wheel.deltaY) > 0.0001f)
        zoomHandler_(wheel.deltaY > 0.0f ? 1.35 : 1.0 / 1.35,
                     timeFromX(event.position.x));
}

void TimelineRulerComponent::seek(const float x)
{
    if (x < static_cast<float>(trackHeaderWidth) || !seekHandler_)
        return;
    seekHandler_(timeFromX(x));
}

void TimelineRulerComponent::updateLoopRange(const float x)
{
    if (!loopRangeHandler_)
        return;
    const auto time = timeFromX(x);
    auto start = loopStartSeconds_;
    auto end = loopEndSeconds_;
    if (dragMode_ == DragMode::createLoop) {
        start = std::min(loopDragAnchorSeconds_, time);
        end = std::max(loopDragAnchorSeconds_, time);
    } else if (dragMode_ == DragMode::loopStart) {
        start = std::min(time, end - 0.000001);
    } else if (dragMode_ == DragMode::loopEnd) {
        end = std::max(time, start + 0.000001);
    }
    loopRangeHandler_(std::max(0.0, start),
                      std::max(start + 0.000001, end));
}

double TimelineRulerComponent::timeFromX(const float x) const noexcept
{
    const auto timelineWidth = std::max(1, getWidth() - trackHeaderWidth);
    const auto normalised = juce::jlimit(
        0.0, 1.0,
        static_cast<double>(x - static_cast<float>(trackHeaderWidth))
            / static_cast<double>(timelineWidth));
    return visibleStartSeconds_
           + normalised
                 * std::max(0.000001,
                            visibleEndSeconds_ - visibleStartSeconds_);
}

float TimelineRulerComponent::xFromTime(const double seconds) const noexcept
{
    const auto timelineWidth = std::max(1, getWidth() - trackHeaderWidth);
    const auto normalised = (seconds - visibleStartSeconds_)
                            / std::max(
                                0.000001,
                                visibleEndSeconds_ - visibleStartSeconds_);
    return static_cast<float>(trackHeaderWidth)
           + static_cast<float>(normalised)
                 * static_cast<float>(timelineWidth);
}

} // namespace cydaw::app
