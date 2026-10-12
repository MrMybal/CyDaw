#include "AutomationEditorComponent.h"
#include "PaintProfile.h"
#include "Localization.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace cydaw::app {
namespace {
constexpr float rulerHeight = 52.0f;
constexpr float labelWidth = 48.0f;
}

AutomationEditorComponent::AutomationEditorComponent()
{
    addAndMakeVisible(targetSelector_);
    addAndMakeVisible(curveSelector_);
    addAndMakeVisible(pointTimeLabel_);
    addAndMakeVisible(pointValueLabel_);
    addAndMakeVisible(pointTimeEditor_);
    addAndMakeVisible(pointValueEditor_);
    for (auto* label : { &pointTimeLabel_, &pointValueLabel_ }) {
        label->setJustificationType(juce::Justification::centredRight);
        label->setColour(juce::Label::textColourId,
                         juce::Colour(0xffaeb6c5));
        label->setFont(juce::FontOptions(10.0f, juce::Font::bold));
    }
    for (auto* editor : { &pointTimeEditor_, &pointValueEditor_ }) {
        editor->setJustification(juce::Justification::centredRight);
        editor->setInputRestrictions(16, "0123456789-.,");
        editor->setEnabled(false);
        editor->onReturnKey = [this] { applyPointEditors(); };
        editor->onFocusLost = [this] { applyPointEditors(); };
        editor->onEscapeKey = [this] { updatePointEditors(); };
    }
    curveSelector_.setSelectedId(1, juce::dontSendNotification);
    curveSelector_.setEnabled(false);
    curveSelector_.onChange = [this] {
        if (selectedPointId_ == 0 || !curveHandler_)
            return;
        const auto found = std::find_if(
            points_.begin(), points_.end(), [this](const auto& point) {
                return point.id == selectedPointId_;
            });
        if (found == points_.end())
            return;
        found->curveType = curveSelector_.getSelectedId() == 2 ? 1 : 0;
        if (found->curveType == 1
            && std::abs(found->curveAmount) < 0.0001f)
            found->curveAmount = 0.25f;
        curveHandler_(found->id, found->curveType, found->curveAmount);
        repaint();
    };
    targetSelector_.onChange = [this] {
        const auto selected = targetSelector_.getSelectedId();
        if (selected <= 0 || selected > static_cast<int>(targets_.size()))
            return;
        const auto& target = targets_[static_cast<std::size_t>(selected - 1)];
        minimumValue_ = target.minimumValue;
        maximumValue_ = target.maximumValue;
        valueSuffix_ = target.suffix;
        if (targetChangedHandler_)
            targetChangedHandler_(target.id);
        updatePointEditors();
        repaint();
    };
}

void AutomationEditorComponent::setPoints(std::vector<Point> points,
                                          const double durationSeconds)
{
    points_ = std::move(points);
    std::sort(points_.begin(), points_.end(), [](const auto& left,
                                                 const auto& right) {
        return left.timeSeconds < right.timeSeconds;
    });
    durationSeconds_ = std::max(0.001, durationSeconds);
    if (visibleEndSeconds_ <= visibleStartSeconds_
        || visibleEndSeconds_ > durationSeconds_ + 0.000001) {
        visibleStartSeconds_ = 0.0;
        visibleEndSeconds_ = durationSeconds_;
    }
    if (std::none_of(points_.begin(), points_.end(), [this](const auto& point) {
            return point.id == selectedPointId_;
        }))
        selectedPointId_ = 0;
    updateCurveSelector();
    updatePointEditors();
    repaint();
}

void AutomationEditorComponent::setMusicalGrid(
    const double tempoBpm, const int gridDivision, const bool enabled)
{
    tempoBpm_ = juce::jlimit(20.0, 400.0, tempoBpm);
    gridDivision_ = juce::jlimit(1, 128, gridDivision);
    musicalGridEnabled_ = enabled;
    repaint();
}

void AutomationEditorComponent::setTimeAxisInset(const float leftPixels)
{
    timeAxisInset_ = std::max(0.0f, leftPixels);
    repaint();
}

void AutomationEditorComponent::setVisibleRange(const double startSeconds,
                                                 const double endSeconds)
{
    visibleStartSeconds_ = juce::jlimit(
        0.0, durationSeconds_, std::max(0.0, startSeconds));
    visibleEndSeconds_ = juce::jlimit(
        visibleStartSeconds_ + 0.000001,
        std::max(visibleStartSeconds_ + 0.000001, durationSeconds_),
        std::max(visibleStartSeconds_ + 0.000001, endSeconds));
    repaint();
}

void AutomationEditorComponent::setCompactMode(const bool compact)
{
    compactMode_ = compact;
    resized();
    repaint();
}

void AutomationEditorComponent::setTargets(std::vector<Target> targets,
                                            const juce::String& selectedTargetId)
{
    targets_ = std::move(targets);
    targetSelector_.clear(juce::dontSendNotification);
    auto selectedId = 0;
    for (std::size_t index = 0; index < targets_.size(); ++index) {
        targetSelector_.addItem(targets_[index].name, static_cast<int>(index + 1));
        if (targets_[index].id == selectedTargetId)
            selectedId = static_cast<int>(index + 1);
    }
    if (selectedId == 0 && !targets_.empty())
        selectedId = 1;
    targetSelector_.setSelectedId(selectedId, juce::dontSendNotification);
    if (selectedId > 0) {
        const auto& target = targets_[static_cast<std::size_t>(selectedId - 1)];
        minimumValue_ = target.minimumValue;
        maximumValue_ = target.maximumValue;
        valueSuffix_ = target.suffix;
    }
    targetSelector_.setEnabled(targets_.size() > 1);
    updatePointEditors();
    repaint();
}

void AutomationEditorComponent::setPlayhead(const double seconds)
{
    const auto next = std::max(0.0, seconds);
    if (std::abs(playheadSeconds_ - next) < 1.0e-9)
        return;
    if (playheadDrawnByHost_) {
        playheadSeconds_ = next;
        return;
    }
    // Called thirty times a second by the transport, on three instances at
    // once. Repainting the whole lane rebuilt the curve through every point for
    // a line two pixels wide.
    const auto column = [this](const double time) {
        const auto graph = graphBounds();
        const auto duration = std::max(1.0e-9,
                                       visibleEndSeconds_ - visibleStartSeconds_);
        const auto x = graph.getX()
                       + static_cast<float>((time - visibleStartSeconds_) / duration)
                             * graph.getWidth();
        if (x < graph.getX() || x > graph.getRight())
            return juce::Rectangle<int> {};
        auto strip = getLocalBounds();
        strip.setX(juce::roundToInt(x) - 3);
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

void AutomationEditorComponent::setPlayheadDrawnByHost(const bool drawnByHost)
{
    playheadDrawnByHost_ = drawnByHost;
    repaint();
}

void AutomationEditorComponent::refreshTranslations()
{
    emptyMessage_ = tr("automation.empty");
    setTooltip(tr("automation.help"));
    const auto selectedCurve = juce::jlimit(
        1, 2, std::max(1, curveSelector_.getSelectedId()));
    curveSelector_.clear(juce::dontSendNotification);
    curveSelector_.addItem(tr("automation.curve.linear"), 1);
    curveSelector_.addItem(tr("automation.curve.bezier"), 2);
    curveSelector_.setSelectedId(selectedCurve,
                                 juce::dontSendNotification);
    curveSelector_.setTooltip(tr("automation.curve.help"));
    pointTimeLabel_.setText(tr("automation.pointTime"),
                            juce::dontSendNotification);
    pointValueLabel_.setText(tr("automation.pointValue"),
                             juce::dontSendNotification);
    pointTimeEditor_.setTooltip(tr("automation.pointTimeHelp"));
    pointValueEditor_.setTooltip(tr("automation.pointValueHelp"));
    updatePointEditors();
    repaint();
}

void AutomationEditorComponent::setAddHandler(std::function<void(double, float)> handler)
{
    addHandler_ = std::move(handler);
}

void AutomationEditorComponent::setUpdateHandler(
    std::function<void(std::uint64_t, double, float)> handler)
{
    updateHandler_ = std::move(handler);
}

void AutomationEditorComponent::setLiveUpdateHandler(
    std::function<void(std::uint64_t, double, float)> handler)
{
    liveUpdateHandler_ = std::move(handler);
}

void AutomationEditorComponent::setRemoveHandler(std::function<void(std::uint64_t)> handler)
{
    removeHandler_ = std::move(handler);
}

void AutomationEditorComponent::setCurveHandler(
    std::function<void(std::uint64_t, int, float)> handler)
{
    curveHandler_ = std::move(handler);
}

void AutomationEditorComponent::setLiveCurveHandler(
    std::function<void(std::uint64_t, int, float)> handler)
{
    liveCurveHandler_ = std::move(handler);
}

void AutomationEditorComponent::setSeekHandler(std::function<void(double)> handler)
{
    seekHandler_ = std::move(handler);
}

void AutomationEditorComponent::setTargetChangedHandler(
    std::function<void(const juce::String&)> handler)
{
    targetChangedHandler_ = std::move(handler);
}

void AutomationEditorComponent::setEditGestureHandlers(
    std::function<void()> beginHandler,
    std::function<void()> endHandler)
{
    editGestureBeginHandler_ = std::move(beginHandler);
    editGestureEndHandler_ = std::move(endHandler);
}

void AutomationEditorComponent::paint(juce::Graphics& graphics)
{
    const paint_profile::Scoped paintScope(paint_profile::counters().automation);
    graphics.fillAll(juce::Colour(0xff11141a));
    const auto graph = graphBounds();
    // What this paint is for. A playhead move asks for a six-pixel strip
    // thirty times a second, on every lane; drawing the whole ribbon for it
    // cost as much as drawing the ribbon.
    const auto clip = graphics.getClipBounds().toFloat();
    graphics.setColour(juce::Colour(0xff191d25));
    graphics.fillRect(graph);

    constexpr int valueDivisions = 6;
    // Built once rather than per division. Measured, this is not where this
    // component's time goes — hoisting it changed nothing — but constructing a
    // font inside a loop is not worth keeping either.
    static const juce::Font scaleFont { juce::FontOptions(9.0f) };
    const auto valueRange = std::max(0.0001f, maximumValue_ - minimumValue_);
    for (int division = 0; division <= valueDivisions; ++division) {
        const auto value = minimumValue_ + valueRange * static_cast<float>(division)
                                               / static_cast<float>(valueDivisions);
        const auto y = graph.getBottom() - static_cast<float>(division)
                                               / static_cast<float>(valueDivisions) * graph.getHeight();
        graphics.setColour(juce::Colour(std::abs(value) < 0.0001f ? 0xff4b5564 : 0xff2b323d));
        graphics.drawHorizontalLine(static_cast<int>(y), graph.getX(), graph.getRight());
        if (clip.getX() >= labelWidth)
            continue;
        graphics.setColour(juce::Colour(0xff8e97a9));
        graphics.setFont(scaleFont);
        const auto decimals = valueRange <= 2.0f ? 2 : 1;
        graphics.drawText(juce::String(value, decimals) + valueSuffix_, 2, static_cast<int>(y) - 8,
                          static_cast<int>(labelWidth) - 5, 16, juce::Justification::centredRight);
    }

    const auto visibleDuration = std::max(
        0.000001, visibleEndSeconds_ - visibleStartSeconds_);
    if (musicalGridEnabled_ && tempoBpm_ > 0.0 && gridDivision_ > 0) {
        const auto baseStep = 60.0 / tempoBpm_
                              * 4.0 / static_cast<double>(gridDivision_);
        auto visualMultiple = 1;
        while (baseStep * static_cast<double>(visualMultiple)
                   / visibleDuration * graph.getWidth() < 7.0f)
            visualMultiple *= 2;
        const auto visualStep = baseStep
                                * static_cast<double>(visualMultiple);
        const auto stepsPerBeat = std::max(1, gridDivision_ / 4);
        const auto firstStep = std::max(
            0, static_cast<int>(std::floor(
                   visibleStartSeconds_ / visualStep)));
        for (auto stepIndex = firstStep;
             static_cast<double>(stepIndex) * visualStep
                 <= visibleEndSeconds_ + visualStep * 0.5;
             ++stepIndex) {
            const auto baseIndex = stepIndex * visualMultiple;
            const auto barLine = baseIndex % gridDivision_ == 0;
            const auto beatLine = baseIndex % stepsPerBeat == 0;
            const auto x = graph.getX()
                           + static_cast<float>(
                               (static_cast<double>(stepIndex) * visualStep
                                - visibleStartSeconds_) / visibleDuration)
                                 * graph.getWidth();
            if (x < graph.getX() - 1.0f || x > graph.getRight() + 1.0f
                || x < clip.getX() - 1.0f || x > clip.getRight() + 1.0f)
                continue;
            graphics.setColour(
                barLine ? juce::Colour(0xff6a5a43).withAlpha(0.82f)
                        : beatLine
                              ? juce::Colour(0xff697079).withAlpha(0.74f)
                              : juce::Colour(0xff3b4045).withAlpha(0.62f));
            graphics.drawVerticalLine(juce::roundToInt(x), graph.getY(),
                                      graph.getBottom());
        }
    }

    if (!points_.empty()) {
        graphics.saveState();
        graphics.reduceClipRegion(graph.toNearestInt());
        juce::Path curve;
        // The lane is time-sorted, so the drawable window is the run of points
        // between the last one before the view and the first one after it —
        // both included, so the segments entering and leaving the view are
        // still drawn. Everything outside only ever added path segments that
        // the clip region threw away.
        //
        // The window is the clip's, not the view's: the strip a playhead
        // move repaints holds a handful of points, and the rest of the lane
        // only ever added path segments the clip threw away.
        const auto timeAt = [this, &graph, visibleDuration](const float x) {
            return visibleStartSeconds_
                   + static_cast<double>(
                         (juce::jlimit(graph.getX(), graph.getRight(), x)
                          - graph.getX())
                         / std::max(1.0f, graph.getWidth()))
                         * visibleDuration;
        };
        const auto clipStartSeconds = timeAt(clip.getX() - 6.0f);
        const auto clipEndSeconds = timeAt(clip.getRight() + 6.0f);
        const auto lowerBound = std::lower_bound(
            points_.begin(), points_.end(), clipStartSeconds,
            [](const Point& point, const double time) {
                return point.timeSeconds < time;
            });
        const auto upperBound = std::upper_bound(
            points_.begin(), points_.end(), clipEndSeconds,
            [](const double time, const Point& point) {
                return time < point.timeSeconds;
            });
        const auto firstIndex = static_cast<std::size_t>(std::distance(
            points_.begin(),
            lowerBound == points_.begin() ? lowerBound : std::prev(lowerBound)));
        const auto lastIndex = static_cast<std::size_t>(std::distance(
            points_.begin(),
            upperBound == points_.end() ? std::prev(upperBound) : upperBound));

        const auto first = pointPosition(points_[firstIndex]);
        curve.startNewSubPath(graph.getX(), first.y);
        curve.lineTo(first);
        for (auto index = firstIndex + 1; index <= lastIndex; ++index) {
            const auto previous = pointPosition(points_[index - 1]);
            const auto current = pointPosition(points_[index]);
            if (points_[index - 1].curveType == 1) {
                const auto handle = curveHandlePosition(
                    points_[index - 1], points_[index]);
                const auto middleY = (previous.y + current.y) * 0.5f;
                const auto bend = handle.y - middleY;
                curve.cubicTo(previous.x + (current.x - previous.x) / 3.0f,
                              previous.y + bend,
                              previous.x + (current.x - previous.x) * 2.0f / 3.0f,
                              current.y + bend,
                              current.x, current.y);
            } else {
                curve.lineTo(current);
            }
        }
        const auto last = pointPosition(points_[lastIndex]);
        curve.lineTo(graph.getRight(), last.y);
        graphics.setColour(juce::Colour(0xffefb45b));
        graphics.strokePath(curve, juce::PathStrokeType(2.0f));

        for (auto index = firstIndex; index <= lastIndex; ++index) {
            const auto& point = points_[index];
            const auto position = pointPosition(point);
            if (!graph.expanded(6.0f).contains(position)
                || !clip.expanded(6.0f).contains(position))
                continue;
            graphics.setColour(juce::Colour(point.id == selectedPointId_ ? 0xffffd08a : 0xffefb45b));
            graphics.fillEllipse(position.x - 5.0f, position.y - 5.0f, 10.0f, 10.0f);
            graphics.setColour(juce::Colour(0xff3c2d18));
            graphics.drawEllipse(position.x - 5.0f, position.y - 5.0f, 10.0f, 10.0f, 1.0f);
        }
        const auto selected = std::find_if(
            points_.begin(), points_.end(), [this](const auto& point) {
                return point.id == selectedPointId_;
            });
        if (selected != points_.end() && selected->curveType == 1) {
            const auto next = std::next(selected);
            if (next != points_.end()) {
                const auto origin = pointPosition(*selected);
                const auto handle = curveHandlePosition(*selected, *next);
                graphics.setColour(juce::Colour(0xff87c8ff));
                graphics.drawLine(origin.x, origin.y, handle.x, handle.y, 1.0f);
                graphics.fillEllipse(handle.x - 4.0f, handle.y - 4.0f,
                                     8.0f, 8.0f);
            }
        }
        graphics.restoreState();
    } else {
        graphics.setColour(juce::Colour(0xff7f889a));
        static const juce::Font emptyFont { juce::FontOptions(12.0f) };
        graphics.setFont(emptyFont);
        graphics.drawFittedText(emptyMessage_, graph.toNearestInt().reduced(30),
                                juce::Justification::centred, 2);
    }

    const auto playheadX = graph.getX()
                           + static_cast<float>(
                               (playheadSeconds_ - visibleStartSeconds_)
                               / visibleDuration)
                                 * graph.getWidth();
    if (!playheadDrawnByHost_ && playheadX >= graph.getX()
        && playheadX <= graph.getRight()) {
        graphics.setColour(juce::Colour(0xff4fd3ae));
        graphics.fillRect(playheadX, 0.0f, 2.0f, static_cast<float>(getHeight()));
    }
    graphics.setColour(juce::Colour(0xff303744));
    graphics.drawRect(getLocalBounds(), 1);
}

void AutomationEditorComponent::resized()
{
    const auto top = compactMode_ ? 3 : 5;
    const auto height = compactMode_ ? 20 : 22;
    const auto showPointEditors = getWidth() >= 650;
    targetSelector_.setBounds(40, top,
                              juce::jmax(110, getWidth() / 3 - 80),
                              height);
    curveSelector_.setBounds(targetSelector_.getRight() + 6,
                             top, 92, height);
    pointTimeLabel_.setVisible(showPointEditors);
    pointTimeEditor_.setVisible(showPointEditors);
    pointValueLabel_.setVisible(showPointEditors);
    pointValueEditor_.setVisible(showPointEditors);
    if (showPointEditors) {
        auto right = getWidth() - 8;
        pointValueEditor_.setBounds(right - 104, top, 104, height);
        right = pointValueEditor_.getX() - 4;
        pointValueLabel_.setBounds(right - 54, top, 54, height);
        right = pointValueLabel_.getX() - 10;
        pointTimeEditor_.setBounds(right - 104, top, 104, height);
        right = pointTimeEditor_.getX() - 4;
        pointTimeLabel_.setBounds(right - 54, top, 54, height);
    }
}

void AutomationEditorComponent::mouseDown(const juce::MouseEvent& event)
{
    if (event.position.y < (compactMode_ ? 28.0f : rulerHeight)) {
        if (seekHandler_)
            seekHandler_(timeAt(event.position.x));
        return;
    }

    for (std::size_t index = 0; index + 1 < points_.size(); ++index) {
        if (points_[index].curveType == 1
            && curveHandlePosition(points_[index], points_[index + 1])
                   .getDistanceFrom(event.position) <= 9.0f) {
            selectedPointId_ = points_[index].id;
            draggedCurvePoint_ = points_[index];
            draggedCurveChanged_ = false;
            updateCurveSelector();
            updatePointEditors();
            repaint();
            if (editGestureBeginHandler_)
                editGestureBeginHandler_();
            return;
        }
    }

    if (const auto* point = pointAt(event.position)) {
        selectedPointId_ = point->id;
        if (event.mods.isPopupMenu()) {
            if (removeHandler_)
                removeHandler_(point->id);
            selectedPointId_ = 0;
        } else {
            draggedPoint_ = *point;
            draggedPointChanged_ = false;
            dragStart_ = event.position;
            if (editGestureBeginHandler_)
                editGestureBeginHandler_();
        }
        updateCurveSelector();
        updatePointEditors();
        repaint();
        return;
    }
    selectedPointId_ = 0;
    updateCurveSelector();
    updatePointEditors();
    repaint();
}

void AutomationEditorComponent::mouseDoubleClick(
    const juce::MouseEvent& event)
{
    if (!graphBounds().contains(event.position) || pointAt(event.position))
        return;
    // JUCE's double-click callback may no longer report the physical button
    // as down. The gesture itself is sufficient and avoids silently placing
    // every attempted point at the first existing time.
    if (addHandler_)
        addHandler_(snappedTime(timeAt(event.position.x)),
                    valueAt(event.position.y));
}

void AutomationEditorComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (draggedCurvePoint_) {
        const auto found = std::find_if(
            points_.begin(), points_.end(), [this](const auto& point) {
                return point.id == draggedCurvePoint_->id;
            });
        if (found != points_.end()) {
            const auto next = std::next(found);
            if (next != points_.end()) {
                const auto firstPosition = pointPosition(*found);
                const auto secondPosition = pointPosition(*next);
                const auto middleY = (firstPosition.y + secondPosition.y) * 0.5f;
                found->curveAmount = juce::jlimit(
                    -1.0f, 1.0f,
                    (middleY - event.position.y)
                        / std::max(1.0f, graphBounds().getHeight() * 0.20f));
                draggedCurveChanged_ = true;
                pendingLiveEdit_ = PendingLiveEdit {
                    found->id, found->timeSeconds, found->value,
                    1, found->curveAmount
                };
                triggerAsyncUpdate();
            }
        }
        return;
    }
    if (!draggedPoint_)
        return;
    const auto graph = graphBounds();
    const auto timeDelta = static_cast<double>(event.position.x - dragStart_.x)
                           / graph.getWidth()
                           * (visibleEndSeconds_ - visibleStartSeconds_);
    const auto valueDelta = -(event.position.y - dragStart_.y) / graph.getHeight()
                            * (maximumValue_ - minimumValue_);
    const auto time = snappedTime(
        std::max(0.0, draggedPoint_->timeSeconds + timeDelta));
    const auto precision = (maximumValue_ - minimumValue_) <= 2.0f ? 1'000.0f : 10.0f;
    const auto value = std::round(juce::jlimit(minimumValue_, maximumValue_,
                                               draggedPoint_->value + valueDelta) * precision) / precision;
    const auto found = std::find_if(
        points_.begin(), points_.end(), [this](const auto& point) {
            return point.id == draggedPoint_->id;
        });
    if (found != points_.end()) {
        found->timeSeconds = juce::jlimit(0.0, durationSeconds_, time);
        found->value = value;
        draggedPointChanged_ = true;
        // Only the dragged point moved, so the lane is sorted except for that
        // one element. Sliding it back into place beats re-sorting the whole
        // lane on every mouse-move event.
        const auto index = static_cast<std::size_t>(
            std::distance(points_.begin(), found));
        auto position = index;
        while (position > 0
               && points_[position - 1].timeSeconds > points_[position].timeSeconds) {
            std::swap(points_[position - 1], points_[position]);
            --position;
        }
        while (position + 1 < points_.size()
               && points_[position + 1].timeSeconds < points_[position].timeSeconds) {
            std::swap(points_[position + 1], points_[position]);
            ++position;
        }
        const auto& moved = points_[position];
        pendingLiveEdit_ = PendingLiveEdit {
            moved.id, moved.timeSeconds, moved.value, -1, 0.0f
        };
        triggerAsyncUpdate();
    }
}

void AutomationEditorComponent::handleAsyncUpdate()
{
    const auto edit = std::exchange(pendingLiveEdit_, std::nullopt);
    if (!edit.has_value())
        return;
    updatePointEditors();
    repaint();
    if (edit->curveKind >= 0) {
        if (liveCurveHandler_)
            liveCurveHandler_(edit->id, edit->curveKind, edit->curveAmount);
        return;
    }
    if (liveUpdateHandler_)
        liveUpdateHandler_(edit->id, edit->timeSeconds, edit->value);
}

void AutomationEditorComponent::mouseUp(const juce::MouseEvent&)
{
    // Anything still queued has to land before the drag is committed.
    if (pendingLiveEdit_.has_value()) {
        cancelPendingUpdate();
        handleAsyncUpdate();
    }
    if (draggedPoint_ && draggedPointChanged_ && updateHandler_) {
        const auto found = std::find_if(
            points_.begin(), points_.end(), [this](const auto& point) {
                return point.id == draggedPoint_->id;
            });
        if (found != points_.end())
            updateHandler_(found->id, found->timeSeconds, found->value);
    }
    if (draggedCurvePoint_ && draggedCurveChanged_ && curveHandler_) {
        const auto found = std::find_if(
            points_.begin(), points_.end(), [this](const auto& point) {
                return point.id == draggedCurvePoint_->id;
            });
        if (found != points_.end())
            curveHandler_(found->id, 1, found->curveAmount);
    }
    if ((draggedPoint_ || draggedCurvePoint_)
        && editGestureEndHandler_)
        editGestureEndHandler_();
    draggedPoint_.reset();
    draggedCurvePoint_.reset();
    draggedPointChanged_ = false;
    draggedCurveChanged_ = false;
    updatePointEditors();
}

juce::Point<float> AutomationEditorComponent::curveHandlePosition(
    const Point& point, const Point& next) const
{
    const auto first = pointPosition(point);
    const auto second = pointPosition(next);
    return { (first.x + second.x) * 0.5f,
             (first.y + second.y) * 0.5f
                 - juce::jlimit(-1.0f, 1.0f, point.curveAmount)
                       * graphBounds().getHeight() * 0.20f };
}

void AutomationEditorComponent::updateCurveSelector()
{
    const auto found = std::find_if(
        points_.begin(), points_.end(), [this](const auto& point) {
            return point.id == selectedPointId_;
        });
    curveSelector_.setEnabled(found != points_.end());
    curveSelector_.setSelectedId(
        found != points_.end() && found->curveType == 1 ? 2 : 1,
        juce::dontSendNotification);
}

void AutomationEditorComponent::updatePointEditors()
{
    const auto found = std::find_if(
        points_.begin(), points_.end(), [this](const auto& point) {
            return point.id == selectedPointId_;
        });
    const auto selected = found != points_.end();
    updatingPointEditors_ = true;
    pointTimeEditor_.setEnabled(selected);
    pointValueEditor_.setEnabled(selected);
    if (selected) {
        pointTimeEditor_.setText(juce::String(found->timeSeconds, 6), false);
        const auto decimals = maximumValue_ - minimumValue_ <= 2.0f ? 3 : 2;
        pointValueEditor_.setText(juce::String(found->value, decimals), false);
    } else {
        pointTimeEditor_.setText({}, false);
        pointValueEditor_.setText({}, false);
    }
    updatingPointEditors_ = false;
}

void AutomationEditorComponent::applyPointEditors()
{
    if (updatingPointEditors_ || selectedPointId_ == 0)
        return;
    const auto found = std::find_if(
        points_.begin(), points_.end(), [this](const auto& point) {
            return point.id == selectedPointId_;
        });
    if (found == points_.end()) {
        updatePointEditors();
        return;
    }
    const auto parse = [](juce::String text) {
        return text.trim().replaceCharacter(',', '.').getDoubleValue();
    };
    const auto time = juce::jlimit(
        0.0, durationSeconds_, parse(pointTimeEditor_.getText()));
    const auto value = juce::jlimit(
        minimumValue_, maximumValue_,
        static_cast<float>(parse(pointValueEditor_.getText())));
    const auto changed = std::abs(found->timeSeconds - time) > 0.0000005
                         || std::abs(found->value - value) > 0.00005f;
    const auto id = found->id;
    found->timeSeconds = time;
    found->value = value;
    std::sort(points_.begin(), points_.end(), [](const auto& left,
                                                 const auto& right) {
        return left.timeSeconds < right.timeSeconds;
    });
    updatePointEditors();
    repaint();
    if (changed && updateHandler_)
        updateHandler_(id, time, value);
}

juce::Rectangle<float> AutomationEditorComponent::graphBounds() const
{
    const auto top = compactMode_ ? 28.0f : rulerHeight;
    // Audio clips use a zero inset, while MIDI uses the piano keyboard width.
    // This gives each automation lane the exact same x/time transform as its
    // owning editor at every zoom level. Value labels remain an overlay.
    return { timeAxisInset_, top,
             std::max(1.0f, static_cast<float>(getWidth())
                                - timeAxisInset_),
             std::max(1.0f, static_cast<float>(getHeight()) - top) };
}

juce::Point<float> AutomationEditorComponent::pointPosition(
    const Point& point) const
{
    const auto graph = graphBounds();
    const auto visibleDuration = std::max(
        0.000001, visibleEndSeconds_ - visibleStartSeconds_);
    return { graph.getX()
                 + static_cast<float>(
                     (point.timeSeconds - visibleStartSeconds_)
                     / visibleDuration)
                       * graph.getWidth(),
             graph.getBottom() - (point.value - minimumValue_)
                                     / std::max(0.0001f, maximumValue_ - minimumValue_)
                                     * graph.getHeight() };
}

const AutomationEditorComponent::Point*
AutomationEditorComponent::pointAt(const juce::Point<float> position) const
{
    for (auto iterator = points_.rbegin(); iterator != points_.rend(); ++iterator)
        if (pointPosition(*iterator).getDistanceFrom(position) <= 9.0f)
            return &*iterator;
    return nullptr;
}

double AutomationEditorComponent::timeAt(const float x) const
{
    const auto graph = graphBounds();
    return juce::jlimit(
        visibleStartSeconds_, visibleEndSeconds_,
        visibleStartSeconds_
            + static_cast<double>(x - graph.getX()) / graph.getWidth()
                  * (visibleEndSeconds_ - visibleStartSeconds_));
}

double AutomationEditorComponent::snappedTime(
    const double seconds) const noexcept
{
    const auto safe = juce::jlimit(0.0, durationSeconds_, seconds);
    if (!musicalGridEnabled_ || tempoBpm_ <= 0.0 || gridDivision_ <= 0)
        return safe;
    const auto interval = 60.0 / tempoBpm_
                          * 4.0 / static_cast<double>(gridDivision_);
    if (interval <= 0.0)
        return safe;
    return juce::jlimit(0.0, durationSeconds_,
                        std::round(safe / interval) * interval);
}

float AutomationEditorComponent::valueAt(const float y) const
{
    const auto graph = graphBounds();
    return juce::jlimit(minimumValue_, maximumValue_,
                        maximumValue_ - (y - graph.getY()) / graph.getHeight()
                                            * (maximumValue_ - minimumValue_));
}

} // namespace cydaw::app
