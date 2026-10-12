#include "AudioEditorComponent.h"
#include "Localization.h"

#include <algorithm>
#include <cmath>

namespace cydaw::app {
namespace {
constexpr int toolbarHeight = 104;
constexpr double minimumSelectionSeconds = 0.001;
}

AudioEditorComponent::AudioEditorComponent(juce::AudioFormatManager& formatManager,
                                           juce::AudioThumbnailCache& cache)
    : thumbnail_(2'048, formatManager, cache)
{
    thumbnail_.addChangeListener(this);
    for (auto* component : { static_cast<juce::Component*>(&zoomOutButton_),
                             static_cast<juce::Component*>(&zoomInButton_),
                             static_cast<juce::Component*>(&fitButton_),
                             static_cast<juce::Component*>(&warpButton_),
                             static_cast<juce::Component*>(&trimButton_),
                             static_cast<juce::Component*>(&resetButton_),
                             static_cast<juce::Component*>(&normaliseButton_),
                             static_cast<juce::Component*>(&exportButton_),
                             static_cast<juce::Component*>(&fadeInButton_),
                             static_cast<juce::Component*>(&fadeOutButton_),
                             static_cast<juce::Component*>(&silenceButton_),
                             static_cast<juce::Component*>(&reverseButton_),
                             static_cast<juce::Component*>(&removeDcButton_),
                             static_cast<juce::Component*>(&invertButton_),
                             static_cast<juce::Component*>(&swapStereoButton_),
                             static_cast<juce::Component*>(&gainLabel_),
                             static_cast<juce::Component*>(&gainSlider_),
                             static_cast<juce::Component*>(&applyGainButton_),
                             static_cast<juce::Component*>(&undoButton_),
                             static_cast<juce::Component*>(&redoButton_),
                             static_cast<juce::Component*>(&cancelButton_),
                             static_cast<juce::Component*>(&statusLabel_) })
        addAndMakeVisible(component);

    zoomOutButton_.onClick = [this] {
        zoomBy(2.0, (viewStartSeconds_ + viewEndSeconds_) * 0.5);
    };
    zoomInButton_.onClick = [this] {
        zoomBy(0.5, (selectionStartSeconds_ + selectionEndSeconds_) * 0.5);
    };
    fitButton_.onClick = [this] { fitSelection(); };
    warpButton_.onClick = [this] { repaint(); };
    trimButton_.onClick = [this] {
        if (trimHandler_ && selectionEndSeconds_ - selectionStartSeconds_
                                >= minimumSelectionSeconds)
            trimHandler_(selectionStartSeconds_, selectionEndSeconds_);
    };
    resetButton_.onClick = [this] {
        if (resetHandler_)
            resetHandler_();
    };
    exportButton_.onClick = [this] {
        if (exportHandler_ && selectionEndSeconds_ - selectionStartSeconds_
                                  >= minimumSelectionSeconds)
            exportHandler_(selectionStartSeconds_, selectionEndSeconds_);
    };
    const auto process = [this](const AudioEditRequest request) {
        (void) processSelection(request);
    };
    fadeInButton_.onClick = [process] {
        process({ AudioEditOperation::fadeIn, 0.0f });
    };
    fadeOutButton_.onClick = [process] {
        process({ AudioEditOperation::fadeOut, 0.0f });
    };
    silenceButton_.onClick = [process] {
        process({ AudioEditOperation::silence, 0.0f });
    };
    reverseButton_.onClick = [process] {
        process({ AudioEditOperation::reverse, 0.0f });
    };
    normaliseButton_.onClick = [process] {
        process({ AudioEditOperation::normalisePeak, -1.0f });
    };
    removeDcButton_.onClick = [process] {
        process({ AudioEditOperation::removeDcOffset, 0.0f });
    };
    invertButton_.onClick = [process] {
        process({ AudioEditOperation::invertPolarity, 0.0f });
    };
    swapStereoButton_.onClick = [process] {
        process({ AudioEditOperation::swapStereo, 0.0f });
    };
    gainSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    gainSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 22);
    gainSlider_.setRange(-24.0, 24.0, 0.1);
    gainSlider_.setValue(0.0, juce::dontSendNotification);
    gainSlider_.setTextValueSuffix(" dB");
    gainSlider_.setDoubleClickReturnValue(true, 0.0);
    applyGainButton_.onClick = [this] {
        (void) applyCurrentGain();
    };
    undoButton_.onClick = [this] {
        if (undoHandler_ && !busy_)
            undoHandler_();
    };
    redoButton_.onClick = [this] {
        if (redoHandler_ && !busy_)
            redoHandler_();
    };
    cancelButton_.onClick = [this] {
        if (cancelHandler_ && busy_)
            cancelHandler_();
    };
    cancelButton_.setColour(juce::TextButton::buttonColourId,
                            juce::Colour(0xff6b2d35));
    statusLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffaeb6c5));
    statusLabel_.setJustificationType(juce::Justification::centredRight);
    statusLabel_.setFont(juce::FontOptions(10.5f));
    gainLabel_.setJustificationType(juce::Justification::centredRight);
    gainLabel_.setFont(juce::FontOptions(10.5f));
    refreshTranslations();
}

void AudioEditorComponent::setTrack(const juce::File& file,
                                    const double sourceLengthSeconds,
                                    const double clipStartSeconds,
                                    const double sourceOffsetSeconds,
                                    const double clipDurationSeconds,
                                    const double clipSourceDurationSeconds)
{
    const auto requestedSourceDuration = clipSourceDurationSeconds >= 0.0
                                             ? clipSourceDurationSeconds
                                             : clipDurationSeconds;
    const auto changedFile = file != file_;
    const auto changedRange = changedFile
                              || std::abs(sourceOffsetSeconds
                                          - clipSourceOffsetSeconds_) > 0.000001
                              || std::abs(requestedSourceDuration
                                          - clipSourceDurationSeconds_) > 0.000001;
    file_ = file;
    sourceLengthSeconds_ = std::max(0.0, sourceLengthSeconds);
    clipStartSeconds_ = std::max(0.0, clipStartSeconds);
    clipSourceOffsetSeconds_ = juce::jlimit(0.0, sourceLengthSeconds_,
                                            sourceOffsetSeconds);
    clipDurationSeconds_ = std::max(0.001, clipDurationSeconds);
    clipSourceDurationSeconds_ = juce::jlimit(
        0.0, std::max(0.0, sourceLengthSeconds_ - clipSourceOffsetSeconds_),
        requestedSourceDuration);
    if (changedFile) {
        thumbnail_.setSource(new juce::FileInputSource(file_));
        viewStartSeconds_ = 0.0;
        viewEndSeconds_ = std::max(minimumSelectionSeconds, sourceLengthSeconds_);
    }
    if (changedRange)
        updateSelection(clipSourceOffsetSeconds_,
                        clipSourceOffsetSeconds_ + clipSourceDurationSeconds_);
    updateButtonState();
    repaint();
}

void AudioEditorComponent::clear(const bool trackSelected)
{
    trackSelectedWhenEmpty_ = trackSelected;
    file_ = {};
    thumbnail_.clear();
    sourceLengthSeconds_ = 0.0;
    clipStartSeconds_ = 0.0;
    clipSourceOffsetSeconds_ = 0.0;
    clipDurationSeconds_ = 0.0;
    clipSourceDurationSeconds_ = 0.0;
    selectionStartSeconds_ = 0.0;
    selectionEndSeconds_ = 0.0;
    viewStartSeconds_ = 0.0;
    viewEndSeconds_ = 1.0;
    stretchMarkers_.clear();
    draggingStretchMarker_.reset();
    canUndo_ = false;
    canRedo_ = false;
    updateButtonState();
    updateStatus();
    repaint();
}

void AudioEditorComponent::setPlayhead(const double timelineSeconds)
{
    const auto next = std::max(0.0, timelineSeconds);
    if (std::abs(playheadTimelineSeconds_ - next) < 1.0e-9)
        return;
    // Thirty times a second, for a two-pixel line: the column the playhead
    // left and the one it moved to, not the waveform, the markers and the
    // ruler around them.
    const auto column = [this](const double timeline) {
        const auto source = clipSourceOffsetSeconds_
                            + (timeline - clipStartSeconds_)
                                  * clipSourceDurationSeconds_
                                  / std::max(0.001, clipDurationSeconds_);
        if (source < viewStartSeconds_ || source > viewEndSeconds_)
            return juce::Rectangle<int> {};
        auto strip = waveformBounds();
        strip.setX(juce::roundToInt(xAtSourceTime(source)) - 3);
        strip.setWidth(6);
        return strip.getIntersection(waveformBounds());
    };
    const auto previous = column(playheadTimelineSeconds_);
    playheadTimelineSeconds_ = next;
    const auto moved = column(playheadTimelineSeconds_);
    if (!previous.isEmpty())
        repaint(previous);
    if (!moved.isEmpty())
        repaint(moved);
}

void AudioEditorComponent::setStretchMarkers(
    std::vector<StretchMarker> markers, const double tempoBpm,
    const int gridDivision)
{
    stretchMarkers_ = std::move(markers);
    tempoBpm_ = juce::jlimit(20.0, 999.0, tempoBpm);
    gridDivision_ = juce::jlimit(1, 128, gridDivision);
    draggingStretchMarker_.reset();
    repaint();
}

void AudioEditorComponent::showStatus(juce::String message)
{
    statusLabel_.setText(std::move(message), juce::dontSendNotification);
}

void AudioEditorComponent::refreshTranslations()
{
    zoomOutButton_.setButtonText("-");
    zoomInButton_.setButtonText("+");
    fitButton_.setButtonText(tr("audioEditor.fit"));
    warpButton_.setButtonText(tr("audioEditor.warp"));
    trimButton_.setButtonText(tr("audioEditor.trim"));
    resetButton_.setButtonText(tr("audioEditor.reset"));
    normaliseButton_.setButtonText(tr("audioEditor.normalise"));
    exportButton_.setButtonText(tr("audioEditor.export"));
    fadeInButton_.setButtonText(tr("audioEditor.fadeIn"));
    fadeOutButton_.setButtonText(tr("audioEditor.fadeOut"));
    silenceButton_.setButtonText(tr("audioEditor.silence"));
    reverseButton_.setButtonText(tr("audioEditor.reverse"));
    removeDcButton_.setButtonText(tr("audioEditor.removeDc"));
    invertButton_.setButtonText(tr("audioEditor.invertPolarity"));
    swapStereoButton_.setButtonText(tr("audioEditor.swapStereo"));
    gainLabel_.setText(tr("audioEditor.gain"), juce::dontSendNotification);
    applyGainButton_.setButtonText(tr("audioEditor.applyGain"));
    undoButton_.setButtonText(tr("audioEditor.undo"));
    redoButton_.setButtonText(tr("audioEditor.redo"));
    cancelButton_.setButtonText(tr("audioEditor.cancel"));
    zoomOutButton_.setTooltip(tr("audioEditor.zoomOut"));
    zoomInButton_.setTooltip(tr("audioEditor.zoomIn"));
    fitButton_.setTooltip(tr("audioEditor.fitHelp"));
    warpButton_.setTooltip(tr("audioEditor.warpHelp"));
    trimButton_.setTooltip(tr("audioEditor.trimHelp"));
    resetButton_.setTooltip(tr("audioEditor.resetHelp"));
    normaliseButton_.setTooltip(tr("audioEditor.normaliseHelp"));
    exportButton_.setTooltip(tr("audioEditor.exportHelp"));
    fadeInButton_.setTooltip(tr("audioEditor.fadeInHelp"));
    fadeOutButton_.setTooltip(tr("audioEditor.fadeOutHelp"));
    silenceButton_.setTooltip(tr("audioEditor.silenceHelp"));
    reverseButton_.setTooltip(tr("audioEditor.reverseHelp"));
    removeDcButton_.setTooltip(tr("audioEditor.removeDcHelp"));
    invertButton_.setTooltip(tr("audioEditor.invertPolarityHelp"));
    swapStereoButton_.setTooltip(tr("audioEditor.swapStereoHelp"));
    gainSlider_.setTooltip(tr("audioEditor.gainHelp"));
    applyGainButton_.setTooltip(tr("audioEditor.applyGainHelp"));
    undoButton_.setTooltip(tr("audioEditor.undoHelp"));
    redoButton_.setTooltip(tr("audioEditor.redoHelp"));
    cancelButton_.setTooltip(tr("audioEditor.cancelHelp"));
    updateStatus();
}

void AudioEditorComponent::setSeekHandler(std::function<void(double)> handler)
{
    seekHandler_ = std::move(handler);
}

void AudioEditorComponent::setTrimHandler(std::function<void(double, double)> handler)
{
    trimHandler_ = std::move(handler);
}

void AudioEditorComponent::setResetHandler(std::function<void()> handler)
{
    resetHandler_ = std::move(handler);
}

void AudioEditorComponent::setExportHandler(std::function<void(double, double)> handler)
{
    exportHandler_ = std::move(handler);
}

void AudioEditorComponent::setProcessHandler(
    std::function<void(AudioEditRequest, double, double)> handler)
{
    processHandler_ = std::move(handler);
}

void AudioEditorComponent::setUndoHandler(std::function<void()> handler)
{
    undoHandler_ = std::move(handler);
}

void AudioEditorComponent::setRedoHandler(std::function<void()> handler)
{
    redoHandler_ = std::move(handler);
}

void AudioEditorComponent::setCancelHandler(std::function<void()> handler)
{
    cancelHandler_ = std::move(handler);
}

void AudioEditorComponent::setStretchMarkerAddHandler(
    std::function<void(double, double)> handler)
{
    stretchMarkerAddHandler_ = std::move(handler);
}

void AudioEditorComponent::setStretchMarkerUpdateHandler(
    std::function<void(std::uint64_t, double)> handler)
{
    stretchMarkerUpdateHandler_ = std::move(handler);
}

void AudioEditorComponent::setStretchMarkerRemoveHandler(
    std::function<void(std::uint64_t)> handler)
{
    stretchMarkerRemoveHandler_ = std::move(handler);
}

void AudioEditorComponent::setStretchMarkerSnapHandler(
    std::function<double(double)> handler)
{
    stretchMarkerSnapHandler_ = std::move(handler);
}

void AudioEditorComponent::setHistoryState(const bool canUndo, const bool canRedo)
{
    canUndo_ = canUndo;
    canRedo_ = canRedo;
    updateButtonState();
}

void AudioEditorComponent::setBusy(const bool busy, juce::String status)
{
    busy_ = busy;
    updateButtonState();
    if (status.isNotEmpty())
        showStatus(std::move(status));
}

bool AudioEditorComponent::processSelection(const AudioEditRequest request)
{
    if (!processHandler_ || busy_
        || selectionEndSeconds_ - selectionStartSeconds_
               < minimumSelectionSeconds)
        return false;
    processHandler_(request, selectionStartSeconds_, selectionEndSeconds_);
    return true;
}

bool AudioEditorComponent::applyCurrentGain()
{
    return processSelection(
        { AudioEditOperation::amplify,
          static_cast<float>(gainSlider_.getValue()) });
}

void AudioEditorComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(0xff11141a));
    graphics.setColour(juce::Colour(0xff242a34));
    graphics.fillRect(0, 0, getWidth(), toolbarHeight);
    const auto bounds = waveformBounds();
    graphics.setColour(juce::Colour(0xff171b22));
    graphics.fillRect(bounds);
    graphics.setColour(juce::Colour(0xff202631));
    graphics.fillRect(rulerBounds());
    graphics.setColour(juce::Colour(0xff303744));
    graphics.drawRect(bounds, 1);

    if (file_ == juce::File {} || sourceLengthSeconds_ <= 0.0) {
        graphics.setColour(juce::Colour(0xff778193));
        graphics.drawFittedText(tr(trackSelectedWhenEmpty_
                                       ? "audioEditor.emptyTrack"
                                       : "audioEditor.empty"),
                                bounds.reduced(30),
                                juce::Justification::centred, 2);
        return;
    }

    const auto viewLength = std::max(minimumSelectionSeconds,
                                     viewEndSeconds_ - viewStartSeconds_);
    graphics.setFont(juce::FontOptions(9.0f));
    for (int division = 0; division <= 8; ++division) {
        const auto x = bounds.getX() + bounds.getWidth() * division / 8;
        graphics.setColour(juce::Colour(division % 4 == 0 ? 0xff3d4552 : 0xff282e38));
        graphics.drawVerticalLine(x, static_cast<float>(bounds.getY()),
                                  static_cast<float>(bounds.getBottom()));
        const auto time = viewStartSeconds_ + viewLength * division / 8.0;
        graphics.setColour(juce::Colour(0xff7f8999));
        graphics.drawText(juce::String(time, 3) + " s", x + 3, bounds.getY() + 2,
                          72, 14, juce::Justification::centredLeft);
    }
    graphics.setColour(juce::Colour(0xff303744));
    graphics.drawHorizontalLine(bounds.getCentreY(), static_cast<float>(bounds.getX()),
                                static_cast<float>(bounds.getRight()));
    graphics.setColour(juce::Colour(0xff68b8ff));
    thumbnail_.drawChannels(graphics, bounds.reduced(2, 18), viewStartSeconds_,
                            viewEndSeconds_, 0.92f);

    if (warpButton_.getToggleState()) {
        const auto beatStep = 60.0 / tempoBpm_ * 4.0
                              / static_cast<double>(gridDivision_);
        graphics.setColour(juce::Colour(0x3853d0c2));
        for (auto time = 0.0; time <= clipDurationSeconds_ + 0.000001;
             time += beatStep) {
            const auto x = xAtTimelineTime(time);
            if (x >= bounds.getX() && x <= bounds.getRight())
                graphics.drawVerticalLine(
                    juce::roundToInt(x),
                    static_cast<float>(bounds.getY() + 18),
                    static_cast<float>(bounds.getBottom()));
        }
    }

    const auto clipLeft = xAtSourceTime(clipSourceOffsetSeconds_);
    const auto clipRight = xAtSourceTime(
        clipSourceOffsetSeconds_ + clipSourceDurationSeconds_);
    graphics.setColour(juce::Colour(0xff4ea5ff));
    graphics.drawRect(juce::Rectangle<float>(clipLeft, static_cast<float>(bounds.getY() + 1),
                                             std::max(1.0f, clipRight - clipLeft),
                                             static_cast<float>(bounds.getHeight() - 2)),
                      1.0f);

    const auto selectionLeft = xAtSourceTime(selectionStartSeconds_);
    const auto selectionRight = xAtSourceTime(selectionEndSeconds_);
    if (selectionRight > selectionLeft) {
        graphics.setColour(juce::Colour(0x38ff9f2f));
        graphics.fillRect(juce::Rectangle<float>(selectionLeft,
                                                 static_cast<float>(bounds.getY()),
                                                 selectionRight - selectionLeft,
                                                 static_cast<float>(bounds.getHeight())));
        graphics.setColour(juce::Colour(0xffff9f2f));
        graphics.drawVerticalLine(static_cast<int>(std::round(selectionLeft)),
                                  static_cast<float>(bounds.getY()),
                                  static_cast<float>(bounds.getBottom()));
        graphics.drawVerticalLine(static_cast<int>(std::round(selectionRight)),
                                  static_cast<float>(bounds.getY()),
                                  static_cast<float>(bounds.getBottom()));
    }

    const auto timelinePosition = playheadTimelineSeconds_ - clipStartSeconds_;
    const auto playheadSource = clipSourceOffsetSeconds_
                                + timelinePosition
                                      * clipSourceDurationSeconds_
                                      / std::max(0.001,
                                                 clipDurationSeconds_);
    if (playheadSource >= viewStartSeconds_ && playheadSource <= viewEndSeconds_) {
        graphics.setColour(juce::Colour(0xffff8a00));
        graphics.fillRect(xAtSourceTime(playheadSource) - 1.0f,
                          static_cast<float>(bounds.getY()), 2.0f,
                          static_cast<float>(bounds.getHeight()));
    }

    for (const auto& marker : stretchMarkers_) {
        const auto sourceX = xAtSourceTime(marker.sourceSeconds);
        const auto markerTimeline = draggingStretchMarker_
                                            && draggingStretchMarker_->id
                                                   == marker.id
                                        ? stretchMarkerPreviewTimeline_
                                        : marker.timelineSeconds;
        const auto targetX = xAtTimelineTime(markerTimeline);
        if ((sourceX < bounds.getX() || sourceX > bounds.getRight())
            && (targetX < bounds.getX() || targetX > bounds.getRight()))
            continue;
        graphics.setColour(juce::Colour(0xff53d0c2).withAlpha(0.62f));
        graphics.drawLine(sourceX,
                          static_cast<float>(bounds.getBottom() - 8),
                          targetX,
                          static_cast<float>(bounds.getY() + 22), 1.5f);
        graphics.setColour(juce::Colour(0xff9bf7e4));
        graphics.fillEllipse(sourceX - 4.0f,
                             static_cast<float>(bounds.getBottom() - 12),
                             8.0f, 8.0f);
        juce::Path handle;
        handle.addTriangle(targetX,
                           static_cast<float>(bounds.getY() + 15),
                           targetX - 7.0f,
                           static_cast<float>(bounds.getY() + 3),
                           targetX + 7.0f,
                           static_cast<float>(bounds.getY() + 3));
        graphics.fillPath(handle);
        graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        graphics.drawText(juce::String(markerTimeline, 3) + " s",
                          juce::roundToInt(targetX) + 7,
                          bounds.getY() + 3, 62, 14,
                          juce::Justification::centredLeft);
    }
}

void AudioEditorComponent::resized()
{
    auto toolbar = getLocalBounds().removeFromTop(toolbarHeight).reduced(5, 4);
    auto firstRow = toolbar.removeFromTop(31);
    auto secondRow = toolbar.removeFromTop(31);
    auto thirdRow = toolbar.removeFromTop(31);
    zoomOutButton_.setBounds(firstRow.removeFromLeft(30).reduced(1));
    zoomInButton_.setBounds(firstRow.removeFromLeft(30).reduced(1));
    fitButton_.setBounds(firstRow.removeFromLeft(62).reduced(2, 1));
    warpButton_.setBounds(firstRow.removeFromLeft(70).reduced(2, 1));
    trimButton_.setBounds(firstRow.removeFromLeft(112).reduced(2, 1));
    resetButton_.setBounds(firstRow.removeFromLeft(88).reduced(2, 1));
    exportButton_.setBounds(firstRow.removeFromLeft(118).reduced(2, 1));
    undoButton_.setBounds(firstRow.removeFromLeft(72).reduced(2, 1));
    redoButton_.setBounds(firstRow.removeFromLeft(72).reduced(2, 1));
    fadeInButton_.setBounds(secondRow.removeFromLeft(84).reduced(2, 1));
    fadeOutButton_.setBounds(secondRow.removeFromLeft(84).reduced(2, 1));
    silenceButton_.setBounds(secondRow.removeFromLeft(82).reduced(2, 1));
    reverseButton_.setBounds(secondRow.removeFromLeft(84).reduced(2, 1));
    normaliseButton_.setBounds(secondRow.removeFromLeft(126).reduced(2, 1));
    removeDcButton_.setBounds(secondRow.removeFromLeft(104).reduced(2, 1));
    invertButton_.setBounds(secondRow.removeFromLeft(92).reduced(2, 1));
    swapStereoButton_.setBounds(secondRow.removeFromLeft(104).reduced(2, 1));
    gainLabel_.setBounds(thirdRow.removeFromLeft(54).reduced(2, 1));
    gainSlider_.setBounds(thirdRow.removeFromLeft(190).reduced(2, 1));
    applyGainButton_.setBounds(thirdRow.removeFromLeft(104).reduced(2, 1));
    cancelButton_.setBounds(thirdRow.removeFromLeft(96).reduced(2, 1));
    statusLabel_.setBounds(thirdRow.reduced(8, 0));
}

void AudioEditorComponent::mouseDown(const juce::MouseEvent& event)
{
    if (!waveformBounds().contains(event.getPosition()))
        return;
    if (rulerBounds().contains(event.getPosition())
        && (event.mods.isLeftButtonDown()
            || event.mods.isMiddleButtonDown())) {
        navigatingRuler_ = true;
        rulerGestureMoved_ = false;
        rulerDragLast_ = event.position;
        rulerAnchorSeconds_ = sourceTimeAt(event.position.x);
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        return;
    }
    if (warpButton_.getToggleState()) {
        if (const auto* marker = stretchMarkerAt(event.position.x)) {
            if (event.mods.isPopupMenu()) {
                if (stretchMarkerRemoveHandler_)
                    stretchMarkerRemoveHandler_(marker->id);
                return;
            }
            if (event.mods.isLeftButtonDown()) {
                draggingStretchMarker_ = *marker;
                stretchMarkerPreviewTimeline_ = marker->timelineSeconds;
                repaint();
                return;
            }
        }
        if (event.mods.isLeftButtonDown()
            && event.getNumberOfClicks() >= 2
            && stretchMarkerAddHandler_) {
            const auto source = sourceTimeAt(event.position.x);
            auto timeline = (source - clipSourceOffsetSeconds_)
                            / std::max(0.001,
                                       clipSourceDurationSeconds_)
                            * clipDurationSeconds_;
            if (stretchMarkerSnapHandler_)
                timeline = stretchMarkerSnapHandler_(timeline);
            stretchMarkerAddHandler_(source, timeline);
            return;
        }
    }
    if (!event.mods.isLeftButtonDown())
        return;
    selectionAnchorSeconds_ = sourceTimeAt(event.position.x);
    draggingSelection_ = true;
    updateSelection(selectionAnchorSeconds_, selectionAnchorSeconds_);
    if (seekHandler_)
        seekHandler_(selectionAnchorSeconds_);
}

void AudioEditorComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (navigatingRuler_) {
        const auto delta = event.position - rulerDragLast_;
        if (std::abs(delta.x) >= 1.0f || std::abs(delta.y) >= 1.0f)
            rulerGestureMoved_ = true;
        if (std::abs(delta.y) >= 1.0f)
            zoomBy(std::pow(1.025, static_cast<double>(delta.y)),
                   rulerAnchorSeconds_);
        if (std::abs(delta.x) >= 1.0f) {
            const auto length = viewEndSeconds_ - viewStartSeconds_;
            auto start = viewStartSeconds_
                         - static_cast<double>(delta.x)
                               / std::max(1, waveformBounds().getWidth())
                               * length;
            start = juce::jlimit(
                0.0, std::max(0.0, sourceLengthSeconds_ - length), start);
            viewStartSeconds_ = start;
            viewEndSeconds_ = std::min(sourceLengthSeconds_, start + length);
            repaint();
        }
        rulerDragLast_ = event.position;
        return;
    }
    if (draggingStretchMarker_) {
        stretchMarkerPreviewTimeline_ = juce::jlimit(
            0.001, std::max(0.001, clipDurationSeconds_ - 0.001),
            timelineTimeAt(event.position.x));
        repaint();
        return;
    }
    if (draggingSelection_)
        updateSelection(selectionAnchorSeconds_, sourceTimeAt(event.position.x));
}

void AudioEditorComponent::mouseUp(const juce::MouseEvent& event)
{
    if (navigatingRuler_) {
        navigatingRuler_ = false;
        if (!rulerGestureMoved_ && seekHandler_)
            seekHandler_(sourceTimeAt(event.position.x));
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }
    if (draggingStretchMarker_) {
        auto timeline = stretchMarkerPreviewTimeline_;
        if (stretchMarkerSnapHandler_)
            timeline = stretchMarkerSnapHandler_(timeline);
        const auto markerId = draggingStretchMarker_->id;
        draggingStretchMarker_.reset();
        if (stretchMarkerUpdateHandler_)
            stretchMarkerUpdateHandler_(markerId, timeline);
        repaint();
        return;
    }
    if (!draggingSelection_)
        return;
    draggingSelection_ = false;
    updateSelection(selectionAnchorSeconds_, sourceTimeAt(event.position.x));
}

void AudioEditorComponent::mouseMove(const juce::MouseEvent& event)
{
    if (rulerBounds().contains(event.getPosition()))
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    else
        setMouseCursor(juce::MouseCursor::NormalCursor);
}

void AudioEditorComponent::mouseWheelMove(const juce::MouseEvent& event,
                                          const juce::MouseWheelDetails& wheel)
{
    if (!waveformBounds().contains(event.getPosition()) || sourceLengthSeconds_ <= 0.0)
        return;
    if (rulerBounds().contains(event.getPosition())
        && (event.mods.isShiftDown()
            || std::abs(wheel.deltaX) > std::abs(wheel.deltaY))) {
        const auto amount = std::abs(wheel.deltaX) > 0.0001f
                                ? wheel.deltaX : wheel.deltaY;
        const auto length = viewEndSeconds_ - viewStartSeconds_;
        const auto start = juce::jlimit(
            0.0, std::max(0.0, sourceLengthSeconds_ - length),
            viewStartSeconds_ - static_cast<double>(amount) * length * 0.65);
        viewStartSeconds_ = start;
        viewEndSeconds_ = std::min(sourceLengthSeconds_, start + length);
        repaint();
        return;
    }
    zoomBy(wheel.deltaY > 0.0f ? 0.72 : 1.38, sourceTimeAt(event.position.x));
}

void AudioEditorComponent::changeListenerCallback(juce::ChangeBroadcaster*)
{
    repaint();
}

juce::Rectangle<int> AudioEditorComponent::waveformBounds() const
{
    return getLocalBounds().withTrimmedTop(toolbarHeight).reduced(5);
}

juce::Rectangle<int> AudioEditorComponent::rulerBounds() const
{
    return waveformBounds().removeFromTop(20);
}

double AudioEditorComponent::sourceTimeAt(const float x) const noexcept
{
    const auto bounds = waveformBounds();
    if (bounds.getWidth() <= 0)
        return viewStartSeconds_;
    const auto amount = juce::jlimit(0.0, 1.0,
                                     static_cast<double>(x - bounds.getX())
                                         / static_cast<double>(bounds.getWidth()));
    return viewStartSeconds_ + amount * (viewEndSeconds_ - viewStartSeconds_);
}

float AudioEditorComponent::xAtSourceTime(const double seconds) const noexcept
{
    const auto bounds = waveformBounds();
    const auto length = std::max(minimumSelectionSeconds,
                                 viewEndSeconds_ - viewStartSeconds_);
    return static_cast<float>(bounds.getX())
           + static_cast<float>((seconds - viewStartSeconds_) / length)
                 * static_cast<float>(bounds.getWidth());
}

float AudioEditorComponent::xAtTimelineTime(const double seconds) const noexcept
{
    const auto nominalSource = clipSourceOffsetSeconds_
                               + seconds
                                     / std::max(0.001,
                                                clipDurationSeconds_)
                                         * clipSourceDurationSeconds_;
    return xAtSourceTime(nominalSource);
}

double AudioEditorComponent::timelineTimeAt(const float x) const noexcept
{
    return (sourceTimeAt(x) - clipSourceOffsetSeconds_)
           / std::max(0.001, clipSourceDurationSeconds_)
           * clipDurationSeconds_;
}

const AudioEditorComponent::StretchMarker*
AudioEditorComponent::stretchMarkerAt(const float x) const
{
    const StretchMarker* closest {};
    auto distance = 10.0f;
    for (const auto& marker : stretchMarkers_) {
        const auto candidate = std::abs(
            xAtTimelineTime(marker.timelineSeconds) - x);
        if (candidate <= distance) {
            distance = candidate;
            closest = &marker;
        }
    }
    return closest;
}

void AudioEditorComponent::updateSelection(const double first, const double second)
{
    selectionStartSeconds_ = juce::jlimit(0.0, sourceLengthSeconds_,
                                          std::min(first, second));
    selectionEndSeconds_ = juce::jlimit(0.0, sourceLengthSeconds_,
                                        std::max(first, second));
    updateButtonState();
    updateStatus();
    repaint();
}

void AudioEditorComponent::updateStatus()
{
    if (sourceLengthSeconds_ <= 0.0) {
        statusLabel_.setText({}, juce::dontSendNotification);
        return;
    }
    auto status = tr("audioEditor.selection");
    status = status.replace("{start}", juce::String(selectionStartSeconds_, 3));
    status = status.replace("{end}", juce::String(selectionEndSeconds_, 3));
    status = status.replace("{duration}",
                            juce::String(selectionEndSeconds_ - selectionStartSeconds_, 3));
    statusLabel_.setText(status, juce::dontSendNotification);
}

void AudioEditorComponent::zoomBy(const double factor, const double centreSeconds)
{
    if (sourceLengthSeconds_ <= 0.0)
        return;
    const auto currentLength = std::max(minimumSelectionSeconds,
                                        viewEndSeconds_ - viewStartSeconds_);
    const auto newLength = juce::jlimit(std::min(0.01, sourceLengthSeconds_),
                                        sourceLengthSeconds_,
                                        currentLength * factor);
    const auto ratio = (centreSeconds - viewStartSeconds_) / currentLength;
    auto start = centreSeconds - newLength * juce::jlimit(0.0, 1.0, ratio);
    start = juce::jlimit(0.0, std::max(0.0, sourceLengthSeconds_ - newLength), start);
    viewStartSeconds_ = start;
    viewEndSeconds_ = std::min(sourceLengthSeconds_, start + newLength);
    repaint();
}

void AudioEditorComponent::fitSelection()
{
    if (sourceLengthSeconds_ <= 0.0)
        return;
    if (selectionEndSeconds_ - selectionStartSeconds_ >= minimumSelectionSeconds) {
        const auto padding = std::max(0.01,
                                      (selectionEndSeconds_ - selectionStartSeconds_) * 0.08);
        viewStartSeconds_ = std::max(0.0, selectionStartSeconds_ - padding);
        viewEndSeconds_ = std::min(sourceLengthSeconds_, selectionEndSeconds_ + padding);
    } else {
        viewStartSeconds_ = 0.0;
        viewEndSeconds_ = sourceLengthSeconds_;
    }
    repaint();
}

void AudioEditorComponent::updateButtonState()
{
    const auto hasFile = file_ != juce::File {} && sourceLengthSeconds_ > 0.0;
    const auto validSelection = hasFile
                                && selectionEndSeconds_ - selectionStartSeconds_
                                       >= minimumSelectionSeconds;
    trimButton_.setEnabled(validSelection && !busy_);
    resetButton_.setEnabled(hasFile && !busy_);
    normaliseButton_.setEnabled(validSelection && !busy_);
    exportButton_.setEnabled(validSelection && !busy_);
    fadeInButton_.setEnabled(validSelection && !busy_);
    fadeOutButton_.setEnabled(validSelection && !busy_);
    silenceButton_.setEnabled(validSelection && !busy_);
    reverseButton_.setEnabled(validSelection && !busy_);
    removeDcButton_.setEnabled(validSelection && !busy_);
    invertButton_.setEnabled(validSelection && !busy_);
    swapStereoButton_.setEnabled(validSelection && !busy_);
    gainSlider_.setEnabled(validSelection && !busy_);
    applyGainButton_.setEnabled(validSelection && !busy_);
    undoButton_.setEnabled(canUndo_ && !busy_);
    redoButton_.setEnabled(canRedo_ && !busy_);
    cancelButton_.setEnabled(busy_);
}

} // namespace cydaw::app
