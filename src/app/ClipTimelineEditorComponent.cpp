#include "ClipTimelineEditorComponent.h"
#include "Localization.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace cydaw::app {
namespace {
bool isAudioFile(const juce::File& file)
{
    const auto extension = file.getFileExtension().toLowerCase();
    return extension == ".wav" || extension == ".wave"
           || extension == ".aif" || extension == ".aiff"
           || extension == ".flac" || extension == ".ogg"
           || extension == ".mp3";
}
}

ClipTimelineEditorComponent::ClipTimelineEditorComponent(
    juce::AudioFormatManager& formatManager,
    juce::AudioThumbnailCache& cache)
    : waveform_(formatManager, cache)
{
    addAndMakeVisible(titleLabel_);
    addAndMakeVisible(helpLabel_);
    addAndMakeVisible(addAudioButton_);
    addAndMakeVisible(openSourceButton_);
    addAndMakeVisible(playButton_);
    addAndMakeVisible(stopButton_);
    addAndMakeVisible(returnButton_);
    addAndMakeVisible(spacePlayModeSelector_);
    addAndMakeVisible(loopButton_);
    addAndMakeVisible(snapSelector_);
    addAndMakeVisible(snapTypeSelector_);
    for (auto* label : { &durationLabel_, &startLabel_, &endLabel_,
                         &loopStartLabel_, &loopEndLabel_ })
        addAndMakeVisible(*label);
    for (auto* editor : { &durationEditor_, &startEditor_, &endEditor_,
                          &loopStartEditor_, &loopEndEditor_ }) {
        addAndMakeVisible(*editor);
        editor->setJustification(juce::Justification::centred);
        editor->setInputRestrictions(12, "0123456789.,-");
        editor->onReturnKey = [this] { applyTimelineBoundsFromEditors(); };
        editor->onFocusLost = [this] { applyTimelineBoundsFromEditors(); };
    }
    addAndMakeVisible(waveform_);

    titleLabel_.setFont(juce::FontOptions(12.5f, juce::Font::bold));
    titleLabel_.setColour(juce::Label::textColourId,
                          juce::Colour(0xfff2f4f7));
    helpLabel_.setFont(juce::FontOptions(10.0f));
    helpLabel_.setColour(juce::Label::textColourId,
                         juce::Colour(0xff9ca6b4));
    helpLabel_.setJustificationType(juce::Justification::centredLeft);

    waveform_.setLocalClipEditing(true);
    waveform_.setEditingTool(WaveformComponent::EditingTool::pointer);
    waveform_.setSnapMode(WaveformComponent::SnapMode::grid);
    waveform_.setSeekHandler([this](const double seconds) {
        if (seekHandler_)
            seekHandler_(seconds);
    });
    waveform_.setRegionSelectionHandler(
        [this](const std::uint64_t id,
               const WaveformComponent::RegionSelectionMode mode) {
            juce::ignoreUnused(mode);
            selectedEventId_ = id;
            openSourceButton_.setEnabled(id != 0);
            if (eventSelectionHandler_)
                eventSelectionHandler_(id);
            rebuildRegions();
        });
    waveform_.setRegionMoveHandler(
        [this](const std::uint64_t id, const double start, int) {
            if (auto* event = eventById(id); event != nullptr
                && eventEditHandler_)
                eventEditHandler_(id, start, event->sourceOffsetSeconds,
                                  event->durationSeconds,
                                  event->sourceDurationSeconds,
                                  event->fadeInSeconds,
                                  event->fadeOutSeconds);
        });
    waveform_.setRegionEditHandler(
        [this](const std::uint64_t id, const double start,
               const double offset, const double duration,
               const double sourceDuration, const double fadeIn,
               const double fadeOut) {
            if (eventEditHandler_)
                eventEditHandler_(id, start, offset, duration,
                                  sourceDuration, fadeIn, fadeOut);
        });
    waveform_.setRegionLiveEditHandler(
        [this](const std::uint64_t id, const double start,
               const double offset, const double duration,
               const double sourceDuration, const double fadeIn,
               const double fadeOut) {
            if (eventLiveEditHandler_)
                eventLiveEditHandler_(id, start, offset, duration,
                                      sourceDuration, fadeIn, fadeOut);
        });
    waveform_.setRegionCommandHandler(
        [this](const std::uint64_t id,
               const WaveformComponent::RegionCommand command,
               const double time) {
            if (eventCommandHandler_)
                eventCommandHandler_(id, command, time);
        });
    waveform_.setStretchMarkerAddHandler(
        [this](const std::uint64_t eventId, const double source,
               const double timeline) {
            if (stretchMarkerAddHandler_)
                stretchMarkerAddHandler_(eventId, source, timeline);
        });
    waveform_.setStretchMarkerUpdateHandler(
        [this](const std::uint64_t eventId,
               const std::uint64_t markerId, const double timeline) {
            if (stretchMarkerUpdateHandler_)
                stretchMarkerUpdateHandler_(eventId, markerId, timeline);
        });
    waveform_.setStretchMarkerLiveUpdateHandler(
        [this](const std::uint64_t eventId,
               const std::uint64_t markerId, const double timeline) {
            if (stretchMarkerLiveUpdateHandler_)
                stretchMarkerLiveUpdateHandler_(eventId, markerId,
                                                timeline);
        });
    waveform_.setStretchMarkerRemoveHandler(
        [this](const std::uint64_t eventId,
               const std::uint64_t markerId) {
            if (stretchMarkerRemoveHandler_)
                stretchMarkerRemoveHandler_(eventId, markerId);
        });
    waveform_.setZoomHandler(
        [this](const double factor, const double anchor) {
            zoom(factor, anchor);
        });
    waveform_.setPanHandler(
        [this](const double delta) { pan(delta); });

    snapSelector_.onChange = [this] { updateSnapControls(); };
    snapTypeSelector_.onChange = [this] { updateSnapControls(); };
    playButton_.onClick = [this] { triggerPlayPause(); };
    stopButton_.onClick = [this] {
        if (transportHandler_)
            transportHandler_(TransportCommand::stop);
    };
    returnButton_.onClick = [this] {
        if (transportHandler_)
            transportHandler_(TransportCommand::returnToStart);
    };
    loopButton_.onClick = [this] {
        if (!updatingTimelineControls_)
            applyTimelineBoundsFromEditors();
    };

    addAudioButton_.onClick = [this] {
        fileChooser_ = std::make_unique<juce::FileChooser>(
            tr("clipEditor.addAudio"), juce::File {},
            "*.wav;*.wave;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        const auto flags = juce::FileBrowserComponent::openMode
                           | juce::FileBrowserComponent::canSelectFiles;
        fileChooser_->launchAsync(flags, [this](const juce::FileChooser& chooser) {
            const auto file = chooser.getResult();
            if (file.existsAsFile() && audioDropHandler_)
                audioDropHandler_(file,
                                  snappedLocalTime(playheadSeconds_));
        });
    };
    openSourceButton_.onClick = [this] {
        if (selectedEventId_ != 0 && openSourceHandler_)
            openSourceHandler_(selectedEventId_);
    };
    refreshTranslations();
    clear();
}

void ClipTimelineEditorComponent::setClip(
    juce::String name, const double durationSeconds,
    std::vector<AudioTrackController::AudioEvent> events,
    const double tempoBpm, const int gridDivision,
    const bool snapEnabled, const double playStartSeconds,
    const double playEndSeconds, const double loopStartSeconds,
    const double loopEndSeconds, const bool looping)
{
    const auto keepView = clipName_ == name && clipName_.isNotEmpty();
    const auto previousWidth = visibleEndSeconds_ - visibleStartSeconds_;
    clipName_ = std::move(name);
    durationSeconds_ = std::max(0.001, durationSeconds);
    events_ = std::move(events);
    tempoBpm_ = tempoBpm;
    playStartSeconds_ = juce::jlimit(
        0.0, std::max(0.0, durationSeconds_ - 0.001), playStartSeconds);
    playEndSeconds_ = juce::jlimit(
        playStartSeconds_ + 0.001, durationSeconds_,
        playEndSeconds > playStartSeconds_ ? playEndSeconds
                                           : durationSeconds_);
    loopStartSeconds_ = juce::jlimit(
        playStartSeconds_, std::max(playStartSeconds_,
                                    playEndSeconds_ - 0.001),
        loopStartSeconds);
    loopEndSeconds_ = juce::jlimit(
        loopStartSeconds_ + 0.001, playEndSeconds_,
        loopEndSeconds > loopStartSeconds_ ? loopEndSeconds
                                           : playEndSeconds_);
    const auto twoBars = 8.0 * 60.0 / std::max(20.0, tempoBpm_);
    displayDurationSeconds_ = durationSeconds_
                              + std::max(twoBars,
                                         durationSeconds_ * 0.25);
    updatingTimelineControls_ = true;
    loopButton_.setToggleState(looping, juce::dontSendNotification);
    updatingTimelineControls_ = false;
    if (!keepView) {
        updatingSnapControls_ = true;
        const auto snapId = !snapEnabled ? 1
                            : gridDivision <= 4 ? 2
                            : gridDivision <= 8 ? 3
                            : gridDivision <= 16 ? 4 : 5;
        snapSelector_.setSelectedId(snapId,
                                    juce::dontSendNotification);
        if (snapTypeSelector_.getSelectedId() == 0)
            snapTypeSelector_.setSelectedId(1,
                                            juce::dontSendNotification);
        updatingSnapControls_ = false;
    }
    updateSnapControls();
    if (std::none_of(events_.begin(), events_.end(), [this](const auto& event) {
            return event.id == selectedEventId_;
        }))
        selectedEventId_ = events_.empty() ? 0 : events_.front().id;
    if (keepView) {
        const auto width = juce::jlimit(
            0.02, displayDurationSeconds_, previousWidth);
        visibleStartSeconds_ = juce::jlimit(
            0.0, std::max(0.0, displayDurationSeconds_ - width),
            visibleStartSeconds_);
        visibleEndSeconds_ = visibleStartSeconds_ + width;
    } else {
        visibleStartSeconds_ = 0.0;
        visibleEndSeconds_ = displayDurationSeconds_;
    }
    titleLabel_.setText(tr("editor.clip") + "  /  " + clipName_,
                        juce::dontSendNotification);
    openSourceButton_.setEnabled(selectedEventId_ != 0);
    refreshTimelineControls();
    rebuildRegions();
}

void ClipTimelineEditorComponent::clear()
{
    clipName_.clear();
    events_.clear();
    selectedEventId_ = 0;
    durationSeconds_ = 1.0;
    displayDurationSeconds_ = 1.0;
    playStartSeconds_ = 0.0;
    playEndSeconds_ = 1.0;
    loopStartSeconds_ = 0.0;
    loopEndSeconds_ = 1.0;
    playheadSeconds_ = 0.0;
    setTransportState(false);
    visibleStartSeconds_ = 0.0;
    visibleEndSeconds_ = 1.0;
    titleLabel_.setText(tr("editor.clip"), juce::dontSendNotification);
    openSourceButton_.setEnabled(false);
    waveform_.clear();
    waveform_.setRegions(1.0, {}, {}, 0);
    refreshTimelineControls();
}

void ClipTimelineEditorComponent::setPlayhead(const double localSeconds)
{
    if (localSeconds < 0.0) {
        waveform_.setPlayhead(-1.0);
        return;
    }
    playheadSeconds_ = juce::jlimit(
        0.0, displayDurationSeconds_, localSeconds);
    waveform_.setPlayhead(playheadSeconds_);
}

void ClipTimelineEditorComponent::setTransportState(const bool playing)
{
    previewPlaying_ = playing;
    playButton_.setButtonText(juce::String::fromUTF8(
        playing ? "❚❚" : "▶"));
    playButton_.setColour(
        juce::TextButton::buttonColourId,
        playing ? juce::Colour(0xffff8a00) : juce::Colour(0xff303640));
    playButton_.setTooltip(playing ? tr("transport.pause")
                                   : tr("transport.play"));
}

void ClipTimelineEditorComponent::triggerPlayPause()
{
    if (!transportHandler_)
        return;
    if (!previewPlaying_
        && spacePlayModeSelector_.getSelectedId() == 2)
        transportHandler_(TransportCommand::returnToStart);
    transportHandler_(TransportCommand::play);
}

double ClipTimelineEditorComponent::visibleStartSeconds() const noexcept
{
    return visibleStartSeconds_;
}

double ClipTimelineEditorComponent::visibleEndSeconds() const noexcept
{
    return visibleEndSeconds_;
}

double ClipTimelineEditorComponent::timelineLengthSeconds() const noexcept
{
    return displayDurationSeconds_;
}

void ClipTimelineEditorComponent::refreshTranslations()
{
    const auto spaceMode = juce::jlimit(
        1, 2, spacePlayModeSelector_.getSelectedId() == 0
                  ? 1 : spacePlayModeSelector_.getSelectedId());
    spacePlayModeSelector_.clear(juce::dontSendNotification);
    spacePlayModeSelector_.addItem(tr("clipEditor.spaceResume"), 1);
    spacePlayModeSelector_.addItem(tr("clipEditor.spaceRestart"), 2);
    spacePlayModeSelector_.setSelectedId(spaceMode,
                                         juce::dontSendNotification);
    spacePlayModeSelector_.setTooltip(tr("clipEditor.spaceModeHelp"));
    addAudioButton_.setButtonText(tr("clipEditor.addAudio"));
    addAudioButton_.setTooltip(tr("clipEditor.addAudioHelp"));
    openSourceButton_.setButtonText(tr("clipEditor.editSource"));
    openSourceButton_.setTooltip(tr("clipEditor.editSourceHelp"));
    helpLabel_.setText(tr("clipEditor.help"), juce::dontSendNotification);
    playButton_.setButtonText(juce::String::fromUTF8(
        previewPlaying_ ? "❚❚" : "▶"));
    stopButton_.setButtonText("■");
    returnButton_.setButtonText("|◀");
    loopButton_.setButtonText(tr("transport.loop"));
    playButton_.setTooltip(previewPlaying_ ? tr("transport.pause")
                                           : tr("transport.play"));
    stopButton_.setTooltip(tr("transport.stop"));
    returnButton_.setTooltip(tr("clipEditor.returnToStart"));
    loopButton_.setTooltip(tr("clipEditor.loopHelp"));
    durationLabel_.setText(tr("clipEditor.duration"),
                           juce::dontSendNotification);
    startLabel_.setText(tr("clipEditor.start"), juce::dontSendNotification);
    endLabel_.setText(tr("clipEditor.end"), juce::dontSendNotification);
    loopStartLabel_.setText(tr("clipEditor.loopStart"),
                            juce::dontSendNotification);
    loopEndLabel_.setText(tr("clipEditor.loopEnd"),
                          juce::dontSendNotification);
    waveform_.setTooltip(tr("clipEditor.help"));
    const auto selectedSnap = juce::jlimit(
        1, 5, snapSelector_.getSelectedId() == 0
                  ? 4 : snapSelector_.getSelectedId());
    const auto selectedType = juce::jlimit(
        1, 3, snapTypeSelector_.getSelectedId() == 0
                  ? 1 : snapTypeSelector_.getSelectedId());
    updatingSnapControls_ = true;
    snapSelector_.clear(juce::dontSendNotification);
    snapSelector_.addItem(tr("snap.off"), 1);
    snapSelector_.addItem("1/4", 2);
    snapSelector_.addItem("1/8", 3);
    snapSelector_.addItem("1/16", 4);
    snapSelector_.addItem("1/32", 5);
    snapSelector_.setSelectedId(selectedSnap,
                                juce::dontSendNotification);
    snapTypeSelector_.clear(juce::dontSendNotification);
    snapTypeSelector_.addItem(tr("snap.type.grid"), 1);
    snapTypeSelector_.addItem(tr("snap.type.relative"), 2);
    snapTypeSelector_.addItem(tr("snap.type.events"), 3);
    snapTypeSelector_.setSelectedId(selectedType,
                                    juce::dontSendNotification);
    snapSelector_.setTooltip(tr("snap.help"));
    snapTypeSelector_.setTooltip(tr("snap.type.help"));
    updatingSnapControls_ = false;
    updateSnapControls();
    titleLabel_.setText(clipName_.isEmpty()
                            ? tr("editor.clip")
                            : tr("editor.clip") + "  /  " + clipName_,
                        juce::dontSendNotification);
}

void ClipTimelineEditorComponent::setSeekHandler(
    std::function<void(double)> handler)
{ seekHandler_ = std::move(handler); }

void ClipTimelineEditorComponent::setEventEditHandler(
    std::function<void(std::uint64_t, double, double, double, double,
                       double, double)> handler)
{ eventEditHandler_ = std::move(handler); }

void ClipTimelineEditorComponent::setEventLiveEditHandler(
    std::function<void(std::uint64_t, double, double, double, double,
                       double, double)> handler)
{ eventLiveEditHandler_ = std::move(handler); }

void ClipTimelineEditorComponent::setEventCommandHandler(
    std::function<void(std::uint64_t, WaveformComponent::RegionCommand,
                       double)> handler)
{ eventCommandHandler_ = std::move(handler); }

void ClipTimelineEditorComponent::setStretchMarkerAddHandler(
    std::function<void(std::uint64_t, double, double)> handler)
{ stretchMarkerAddHandler_ = std::move(handler); }

void ClipTimelineEditorComponent::setStretchMarkerUpdateHandler(
    std::function<void(std::uint64_t, std::uint64_t, double)> handler)
{ stretchMarkerUpdateHandler_ = std::move(handler); }

void ClipTimelineEditorComponent::setStretchMarkerLiveUpdateHandler(
    std::function<void(std::uint64_t, std::uint64_t, double)> handler)
{ stretchMarkerLiveUpdateHandler_ = std::move(handler); }

void ClipTimelineEditorComponent::setStretchMarkerRemoveHandler(
    std::function<void(std::uint64_t, std::uint64_t)> handler)
{ stretchMarkerRemoveHandler_ = std::move(handler); }

void ClipTimelineEditorComponent::setAudioDropHandler(
    std::function<void(const juce::File&, double)> handler)
{ audioDropHandler_ = std::move(handler); }

void ClipTimelineEditorComponent::setOpenSourceHandler(
    std::function<void(std::uint64_t)> handler)
{ openSourceHandler_ = std::move(handler); }

void ClipTimelineEditorComponent::setEventSelectionHandler(
    std::function<void(std::uint64_t)> handler)
{ eventSelectionHandler_ = std::move(handler); }

void ClipTimelineEditorComponent::setTimelineBoundsHandler(
    std::function<void(double, double, double, double, double, bool)> handler)
{ timelineBoundsHandler_ = std::move(handler); }

void ClipTimelineEditorComponent::setTransportHandler(
    std::function<void(TransportCommand)> handler)
{ transportHandler_ = std::move(handler); }

void ClipTimelineEditorComponent::setViewChangedHandler(
    std::function<void(double, double)> handler)
{
    viewChangedHandler_ = std::move(handler);
}

void ClipTimelineEditorComponent::setEditGestureHandlers(
    std::function<void()> beginHandler,
    std::function<void()> endHandler)
{
    waveform_.setEditGestureHandlers(std::move(beginHandler),
                                     std::move(endHandler));
}

void ClipTimelineEditorComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(0xff15191f));
    graphics.setColour(juce::Colour(0xff303844));
    graphics.drawRect(getLocalBounds(), 1);
    graphics.setColour(juce::Colour(0xff20252c));
    graphics.fillRect(rulerBounds_);
    graphics.setColour(juce::Colour(0xff414956));
    graphics.drawHorizontalLine(rulerBounds_.getBottom() - 1,
                                static_cast<float>(rulerBounds_.getX()),
                                static_cast<float>(rulerBounds_.getRight()));
    const auto beatDuration = 60.0 / std::max(20.0, tempoBpm_);
    const auto visibleDuration = std::max(
        0.000001, visibleEndSeconds_ - visibleStartSeconds_);
    const auto firstBeat = static_cast<int>(
        std::floor(visibleStartSeconds_ / beatDuration));
    const auto lastBeat = static_cast<int>(
        std::ceil(visibleEndSeconds_ / beatDuration));
    graphics.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    for (auto beat = firstBeat; beat <= lastBeat; ++beat) {
        const auto time = static_cast<double>(beat) * beatDuration;
        const auto x = rulerBounds_.getX()
                       + juce::roundToInt(
                           (time - visibleStartSeconds_)
                           / visibleDuration
                           * static_cast<double>(rulerBounds_.getWidth()));
        const auto barBeat = beat >= 0 ? beat % 4 : (4 + beat % 4) % 4;
        graphics.setColour(barBeat == 0 ? juce::Colour(0xffff9b2f)
                                        : juce::Colour(0xff68717e));
        graphics.drawVerticalLine(x,
                                  static_cast<float>(rulerBounds_.getY()
                                                     + (barBeat == 0 ? 3 : 10)),
                                  static_cast<float>(rulerBounds_.getBottom()));
        if (barBeat == 0)
            graphics.drawText(
                juce::String(beat / 4 + 1) + ".1",
                x + 4, rulerBounds_.getY(), 44,
                rulerBounds_.getHeight(),
                juce::Justification::centredLeft, false);
    }
    const auto timeToX = [this](const double seconds) {
        const auto visible = std::max(
            0.000001, visibleEndSeconds_ - visibleStartSeconds_);
        return rulerBounds_.getX() + juce::roundToInt(
            (seconds - visibleStartSeconds_) / visible
            * static_cast<double>(rulerBounds_.getWidth()));
    };
    const auto drawDurationBound = [&graphics, &timeToX, this](
                                       const double seconds,
                                       const juce::Colour colour,
                                       const juce::String& text) {
        if (seconds < visibleStartSeconds_ || seconds > visibleEndSeconds_)
            return;
        const auto x = timeToX(seconds);
        graphics.setColour(colour);
        graphics.drawVerticalLine(x, static_cast<float>(rulerBounds_.getY()),
                                  static_cast<float>(getHeight()));
        graphics.fillRect(x - 2, rulerBounds_.getY(), 5, 6);
        graphics.setFont(juce::FontOptions(8.5f, juce::Font::bold));
        graphics.drawText(text, x + 5, rulerBounds_.getY(), 52, 11,
                          juce::Justification::centredLeft, false);
    };
    const auto drawRange = [&graphics, &timeToX, this](
                               const double start, const double end,
                               const int y, const juce::Colour colour,
                               const juce::String& startText,
                               const juce::String& endText) {
        if (end < visibleStartSeconds_ || start > visibleEndSeconds_)
            return;
        const auto startX = timeToX(start);
        const auto endX = timeToX(end);
        graphics.setColour(colour.withAlpha(0.78f));
        graphics.fillRect(std::min(startX, endX), y - 1,
                          std::max(1, std::abs(endX - startX)), 3);
        const auto drawHandle = [&graphics, colour, y](
                                    const int x,
                                    const juce::String& text,
                                    const bool alignRight) {
            graphics.setColour(colour);
            graphics.fillRoundedRectangle(
                juce::Rectangle<float>(static_cast<float>(x - 6),
                                       static_cast<float>(y - 5),
                                       13.0f, 11.0f), 2.0f);
            graphics.setColour(juce::Colour(0xff101419));
            graphics.setFont(juce::FontOptions(7.5f,
                                                juce::Font::bold));
            graphics.drawFittedText(
                text,
                alignRight ? juce::Rectangle<int>(x - 42, y - 6, 34, 12)
                           : juce::Rectangle<int>(x + 9, y - 6, 34, 12),
                alignRight ? juce::Justification::centredRight
                           : juce::Justification::centredLeft,
                1, 0.75f);
        };
        if (start >= visibleStartSeconds_ && start <= visibleEndSeconds_)
            drawHandle(startX, startText, false);
        if (end >= visibleStartSeconds_ && end <= visibleEndSeconds_)
            drawHandle(endX, endText, true);
    };
    drawDurationBound(0.0, juce::Colour(0xff66717f), "0");
    drawDurationBound(durationSeconds_, juce::Colour(0xffff9b2f),
                      tr("clipEditor.end"));
    drawRange(playStartSeconds_, playEndSeconds_,
              rulerBounds_.getY() + 7, juce::Colour(0xff59d4bd),
              tr("clipEditor.boundStartShort"),
              tr("clipEditor.boundEndShort"));
    if (loopButton_.getToggleState()) {
        drawRange(loopStartSeconds_, loopEndSeconds_,
                  rulerBounds_.getBottom() - 6,
                  juce::Colour(0xffb58cff),
                  tr("clipEditor.loopStartShort"),
                  tr("clipEditor.loopEndShort"));
    }
    if (events_.empty()) {
        graphics.setColour(juce::Colour(0xff7f8997));
        graphics.setFont(juce::FontOptions(13.0f));
        graphics.drawFittedText(tr("clipEditor.empty"),
                                getLocalBounds().reduced(30),
                                juce::Justification::centred, 2);
    }
}

void ClipTimelineEditorComponent::paintOverChildren(
    juce::Graphics& graphics)
{
    if (rulerBounds_.isEmpty() || waveform_.getBounds().isEmpty())
        return;
    const auto visible = std::max(
        0.000001, visibleEndSeconds_ - visibleStartSeconds_);
    const auto xFor = [this, visible](const double seconds) {
        return waveform_.getX() + juce::roundToInt(
            (seconds - visibleStartSeconds_) / visible
            * static_cast<double>(waveform_.getWidth()));
    };
    const auto wave = waveform_.getBounds();
    const auto startX = xFor(playStartSeconds_);
    const auto endX = xFor(playEndSeconds_);
    graphics.setColour(juce::Colour(0xff0b0e12).withAlpha(0.38f));
    if (startX > wave.getX())
        graphics.fillRect(wave.withRight(
            juce::jlimit(wave.getX(), wave.getRight(), startX)));
    if (endX < wave.getRight())
        graphics.fillRect(wave.withLeft(
            juce::jlimit(wave.getX(), wave.getRight(), endX)));
    const auto drawBoundary = [&graphics, &wave](
                                  const int x,
                                  const juce::Colour colour,
                                  const float thickness) {
        if (x < wave.getX() - 2 || x > wave.getRight() + 2)
            return;
        graphics.setColour(colour);
        graphics.drawVerticalLine(x, static_cast<float>(wave.getY()),
                                  static_cast<float>(wave.getBottom()));
        if (thickness > 1.0f)
            graphics.drawVerticalLine(x + 1,
                                      static_cast<float>(wave.getY()),
                                      static_cast<float>(wave.getBottom()));
    };
    drawBoundary(startX, juce::Colour(0xff59d4bd), 2.0f);
    drawBoundary(endX, juce::Colour(0xff59d4bd), 2.0f);
    if (loopButton_.getToggleState()) {
        const auto loopStartX = xFor(loopStartSeconds_);
        const auto loopEndX = xFor(loopEndSeconds_);
        drawBoundary(loopStartX, juce::Colour(0xffb58cff), 1.0f);
        drawBoundary(loopEndX, juce::Colour(0xffb58cff), 1.0f);
        graphics.setColour(juce::Colour(0xffb58cff));
        graphics.fillRect(std::min(loopStartX, loopEndX), wave.getY(),
                          std::max(1, std::abs(loopEndX - loopStartX)), 3);
    }
}

void ClipTimelineEditorComponent::resized()
{
    auto area = getLocalBounds().reduced(5);
    auto toolbar = area.removeFromTop(32);
    openSourceButton_.setBounds(toolbar.removeFromRight(94).reduced(2));
    addAudioButton_.setBounds(toolbar.removeFromRight(94).reduced(2));
    snapTypeSelector_.setBounds(
        toolbar.removeFromRight(96).reduced(2));
    snapSelector_.setBounds(toolbar.removeFromRight(72).reduced(2));
    loopButton_.setBounds(toolbar.removeFromRight(68).reduced(2));
    spacePlayModeSelector_.setBounds(
        toolbar.removeFromRight(104).reduced(2));
    returnButton_.setBounds(toolbar.removeFromRight(34).reduced(2));
    stopButton_.setBounds(toolbar.removeFromRight(30).reduced(2));
    playButton_.setBounds(toolbar.removeFromRight(30).reduced(2));
    titleLabel_.setBounds(toolbar.reduced(6, 0));
    auto boundsRow = area.removeFromTop(30);
    const auto setPair = [&boundsRow](juce::Label& label,
                                      juce::TextEditor& editor,
                                      const int width) {
        auto pair = boundsRow.removeFromLeft(width);
        label.setBounds(pair.removeFromLeft(width / 2).reduced(2));
        editor.setBounds(pair.reduced(2, 3));
    };
    setPair(durationLabel_, durationEditor_, 148);
    setPair(startLabel_, startEditor_, 118);
    setPair(endLabel_, endEditor_, 118);
    setPair(loopStartLabel_, loopStartEditor_, 142);
    setPair(loopEndLabel_, loopEndEditor_, 142);
    helpLabel_.setBounds(boundsRow.reduced(7, 0));
    rulerBounds_ = area.removeFromTop(24);
    waveform_.setBounds(area.reduced(2));
}

void ClipTimelineEditorComponent::mouseDown(const juce::MouseEvent& event)
{
    if (!rulerBounds_.contains(event.getPosition())
        || (!event.mods.isLeftButtonDown()
            && !event.mods.isMiddleButtonDown()))
        return;
    if (event.mods.isLeftButtonDown()) {
        draggedBoundHandle_ = boundHandleAt(event.getPosition());
        if (draggedBoundHandle_ != BoundHandle::none) {
            rulerGestureMoved_ = false;
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            return;
        }
    }
    navigatingRuler_ = true;
    rulerGestureMoved_ = false;
    rulerDragLast_ = event.position;
    rulerAnchorSeconds_ = localTimeAtX(
        juce::roundToInt(event.position.x));
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
}

void ClipTimelineEditorComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (draggedBoundHandle_ != BoundHandle::none) {
        rulerGestureMoved_ = true;
        dragBoundHandle(draggedBoundHandle_, snappedLocalTime(
            localTimeAtX(juce::roundToInt(event.position.x))));
        return;
    }
    if (!navigatingRuler_)
        return;
    const auto delta = event.position - rulerDragLast_;
    if (std::abs(delta.x) >= 1.0f || std::abs(delta.y) >= 1.0f)
        rulerGestureMoved_ = true;
    if (std::abs(delta.y) >= 1.0f)
        zoom(std::pow(1.025, -static_cast<double>(delta.y)),
             rulerAnchorSeconds_);
    if (std::abs(delta.x) >= 1.0f && rulerBounds_.getWidth() > 0)
        pan(-static_cast<double>(delta.x)
            / static_cast<double>(rulerBounds_.getWidth())
            * (visibleEndSeconds_ - visibleStartSeconds_));
    rulerDragLast_ = event.position;
}

void ClipTimelineEditorComponent::mouseUp(const juce::MouseEvent& event)
{
    if (draggedBoundHandle_ != BoundHandle::none) {
        dragBoundHandle(draggedBoundHandle_, snappedLocalTime(
            localTimeAtX(juce::roundToInt(event.position.x))));
        publishTimelineBounds();
        draggedBoundHandle_ = BoundHandle::none;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }
    if (!navigatingRuler_)
        return;
    navigatingRuler_ = false;
    if (!rulerGestureMoved_ && seekHandler_)
        seekHandler_(snappedLocalTime(
            localTimeAtX(juce::roundToInt(event.position.x))));
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void ClipTimelineEditorComponent::mouseMove(const juce::MouseEvent& event)
{
    if (boundHandleAt(event.getPosition()) != BoundHandle::none)
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    else
        setMouseCursor(rulerBounds_.contains(event.getPosition())
                           ? juce::MouseCursor::DraggingHandCursor
                           : juce::MouseCursor::NormalCursor);
}

void ClipTimelineEditorComponent::mouseWheelMove(
    const juce::MouseEvent& event,
    const juce::MouseWheelDetails& wheel)
{
    if (!rulerBounds_.contains(event.getPosition()))
        return;
    if (event.mods.isShiftDown()
        || std::abs(wheel.deltaX) > std::abs(wheel.deltaY)) {
        const auto amount = std::abs(wheel.deltaX) > 0.0001f
                                ? wheel.deltaX : wheel.deltaY;
        pan(-static_cast<double>(amount)
            * (visibleEndSeconds_ - visibleStartSeconds_) * 0.65);
        return;
    }
    if (std::abs(wheel.deltaY) > 0.0001f)
        zoom(wheel.deltaY > 0.0f ? 1.35 : 1.0 / 1.35,
             localTimeAtX(juce::roundToInt(event.position.x)));
}

bool ClipTimelineEditorComponent::isInterestedInFileDrag(
    const juce::StringArray& files)
{
    return std::any_of(files.begin(), files.end(), [](const auto& path) {
        return isAudioFile(juce::File(path));
    });
}

void ClipTimelineEditorComponent::filesDropped(
    const juce::StringArray& files, const int x, int)
{
    if (!audioDropHandler_)
        return;
    auto time = snappedLocalTime(localTimeAtX(x));
    for (const auto& path : files) {
        const auto file = juce::File(path);
        if (!isAudioFile(file))
            continue;
        audioDropHandler_(file, time);
        time += 0.02;
    }
}

AudioTrackController::AudioEvent*
ClipTimelineEditorComponent::eventById(const std::uint64_t id) noexcept
{
    const auto found = std::find_if(
        events_.begin(), events_.end(),
        [id](const auto& event) { return event.id == id; });
    return found != events_.end() ? &*found : nullptr;
}

void ClipTimelineEditorComponent::rebuildRegions()
{
    std::vector<WaveformComponent::Region> regions;
    regions.reserve(events_.size());
    for (std::size_t index = 0; index < events_.size(); ++index) {
        const auto& event = events_[index];
        WaveformComponent::Region region;
        region.id = event.id;
        region.name = event.name;
        region.startSeconds = event.startSeconds;
        region.sourceOffsetSeconds = event.sourceOffsetSeconds;
        region.durationSeconds = event.durationSeconds;
        region.sourceDurationSeconds = event.sourceDurationSeconds;
        region.colour = juce::Colour(event.colour);
        region.sourceFile = event.sourceFile;
        region.sourceLengthSeconds = event.sourceLengthSeconds;
        region.gainDb = event.gainDb;
        region.fadeInSeconds = event.fadeInSeconds;
        region.fadeOutSeconds = event.fadeOutSeconds;
        for (const auto& marker : event.stretchMarkers)
            region.stretchMarkers.push_back(
                { marker.id, marker.sourceSeconds,
                  marker.timelineSeconds });
        region.laneIndex = 0;
        region.laneCount = 1;
        regions.push_back(std::move(region));
    }
    waveform_.setRegions(displayDurationSeconds_, std::move(regions),
                         selectedEventId_ == 0
                             ? std::vector<std::uint64_t> {}
                             : std::vector<std::uint64_t> { selectedEventId_ },
                         selectedEventId_);
    waveform_.setVisibleRange(visibleStartSeconds_, visibleEndSeconds_);
    waveform_.setMusicalGrid(tempoBpm_, gridDivision_, snapEnabled_);
    const auto snapMode = snapTypeSelector_.getSelectedId() == 2
                              ? WaveformComponent::SnapMode::relative
                          : snapTypeSelector_.getSelectedId() == 3
                              ? WaveformComponent::SnapMode::events
                              : WaveformComponent::SnapMode::grid;
    waveform_.setSnapMode(snapMode);
    repaint();
}

void ClipTimelineEditorComponent::updateSnapControls()
{
    if (updatingSnapControls_)
        return;
    snapEnabled_ = snapSelector_.getSelectedId() != 1;
    switch (snapSelector_.getSelectedId()) {
        case 2: gridDivision_ = 4; break;
        case 3: gridDivision_ = 8; break;
        case 5: gridDivision_ = 32; break;
        case 4:
        default: gridDivision_ = 16; break;
    }
    rebuildRegions();
}

void ClipTimelineEditorComponent::zoom(
    const double factor, const double anchorSeconds)
{
    const auto current = visibleEndSeconds_ - visibleStartSeconds_;
    const auto next = juce::jlimit(
        0.02, displayDurationSeconds_,
        current / std::max(0.05, factor));
    const auto ratio = current > 0.0
                           ? juce::jlimit(0.0, 1.0,
                               (anchorSeconds - visibleStartSeconds_)
                                   / current)
                           : 0.5;
    visibleStartSeconds_ = juce::jlimit(
        0.0, std::max(0.0, displayDurationSeconds_ - next),
        anchorSeconds - ratio * next);
    visibleEndSeconds_ = visibleStartSeconds_ + next;
    waveform_.setVisibleRange(visibleStartSeconds_, visibleEndSeconds_);
    repaint(rulerBounds_);
    if (viewChangedHandler_)
        viewChangedHandler_(visibleStartSeconds_, visibleEndSeconds_);
}

void ClipTimelineEditorComponent::pan(const double deltaSeconds)
{
    const auto width = visibleEndSeconds_ - visibleStartSeconds_;
    visibleStartSeconds_ = juce::jlimit(
        0.0, std::max(0.0, displayDurationSeconds_ - width),
        visibleStartSeconds_ + deltaSeconds);
    visibleEndSeconds_ = visibleStartSeconds_ + width;
    waveform_.setVisibleRange(visibleStartSeconds_, visibleEndSeconds_);
    repaint(rulerBounds_);
    if (viewChangedHandler_)
        viewChangedHandler_(visibleStartSeconds_, visibleEndSeconds_);
}

double ClipTimelineEditorComponent::localTimeAtX(const int x) const noexcept
{
    const auto bounds = waveform_.getBounds();
    if (bounds.getWidth() <= 0)
        return 0.0;
    return juce::jlimit(
        visibleStartSeconds_, visibleEndSeconds_,
        visibleStartSeconds_
            + static_cast<double>(x - bounds.getX())
                  / static_cast<double>(bounds.getWidth())
                  * (visibleEndSeconds_ - visibleStartSeconds_));
}

ClipTimelineEditorComponent::BoundHandle
ClipTimelineEditorComponent::boundHandleAt(
    const juce::Point<int> position) const noexcept
{
    if (!rulerBounds_.contains(position)
        || rulerBounds_.getWidth() <= 0)
        return BoundHandle::none;
    const auto visible = std::max(
        0.000001, visibleEndSeconds_ - visibleStartSeconds_);
    const auto xFor = [this, visible](const double seconds) {
        return rulerBounds_.getX() + juce::roundToInt(
            (seconds - visibleStartSeconds_) / visible
            * static_cast<double>(rulerBounds_.getWidth()));
    };
    const auto closest = [&position, &xFor](
                             const double first,
                             const BoundHandle firstHandle,
                             const double second,
                             const BoundHandle secondHandle) {
        const auto firstDistance = std::abs(position.x - xFor(first));
        const auto secondDistance = std::abs(position.x - xFor(second));
        const auto minimum = std::min(firstDistance, secondDistance);
        if (minimum > 12)
            return BoundHandle::none;
        return firstDistance <= secondDistance ? firstHandle
                                               : secondHandle;
    };
    if (position.y < rulerBounds_.getCentreY())
        return closest(playStartSeconds_, BoundHandle::playStart,
                       playEndSeconds_, BoundHandle::playEnd);
    if (loopButton_.getToggleState())
        return closest(loopStartSeconds_, BoundHandle::loopStart,
                       loopEndSeconds_, BoundHandle::loopEnd);
    return BoundHandle::none;
}

void ClipTimelineEditorComponent::dragBoundHandle(
    const BoundHandle handle, const double seconds)
{
    switch (handle) {
        case BoundHandle::playStart:
            playStartSeconds_ = juce::jlimit(
                0.0, std::max(0.0, playEndSeconds_ - 0.001), seconds);
            break;
        case BoundHandle::playEnd:
            playEndSeconds_ = juce::jlimit(
                playStartSeconds_ + 0.001, durationSeconds_, seconds);
            break;
        case BoundHandle::loopStart:
            loopStartSeconds_ = juce::jlimit(
                playStartSeconds_,
                std::max(playStartSeconds_, loopEndSeconds_ - 0.001),
                seconds);
            break;
        case BoundHandle::loopEnd:
            loopEndSeconds_ = juce::jlimit(
                loopStartSeconds_ + 0.001, playEndSeconds_, seconds);
            break;
        case BoundHandle::none:
            return;
    }
    loopStartSeconds_ = juce::jlimit(
        playStartSeconds_, std::max(playStartSeconds_,
                                    playEndSeconds_ - 0.001),
        loopStartSeconds_);
    loopEndSeconds_ = juce::jlimit(
        loopStartSeconds_ + 0.001, playEndSeconds_, loopEndSeconds_);
    refreshTimelineControls();
    // The bounds overlay belongs to this editor, not to the audio events.
    // Rebuilding every thumbnail on each pointer pixel made loop handles lag
    // and could make stretch anchors appear to jump while dragging.
    repaint();
}

void ClipTimelineEditorComponent::publishTimelineBounds()
{
    if (timelineBoundsHandler_)
        timelineBoundsHandler_(durationSeconds_, playStartSeconds_,
                               playEndSeconds_, loopStartSeconds_,
                               loopEndSeconds_,
                               loopButton_.getToggleState());
}

double ClipTimelineEditorComponent::snappedLocalTime(
    const double seconds) const noexcept
{
    const auto safe = juce::jlimit(0.0, displayDurationSeconds_, seconds);
    if (!snapEnabled_ || tempoBpm_ <= 0.0 || gridDivision_ <= 0)
        return safe;
    const auto interval = 60.0 / tempoBpm_ * 4.0
                          / static_cast<double>(gridDivision_);
    if (snapTypeSelector_.getSelectedId() == 3 && !events_.empty()) {
        auto nearest = safe;
        auto distance = interval;
        for (const auto& event : events_)
            for (const auto edge : {
                     event.startSeconds,
                     event.startSeconds + event.durationSeconds }) {
                const auto candidateDistance = std::abs(edge - safe);
                if (candidateDistance < distance) {
                    distance = candidateDistance;
                    nearest = edge;
                }
            }
        if (distance < interval)
            return nearest;
    }
    return juce::jlimit(
        0.0, displayDurationSeconds_,
        std::round(safe / interval) * interval);
}

double ClipTimelineEditorComponent::snapTime(
    const double seconds) const noexcept
{
    return snappedLocalTime(seconds);
}

void ClipTimelineEditorComponent::applyTimelineBoundsFromEditors()
{
    if (updatingTimelineControls_)
        return;
    const auto parse = [](const juce::TextEditor& editor,
                          const double fallback) {
        auto text = editor.getText().trim().replaceCharacter(',', '.');
        return text.isNotEmpty() ? text.getDoubleValue() : fallback;
    };
    auto duration = juce::jlimit(
        0.001, 86'400.0, parse(durationEditor_, durationSeconds_));
    auto start = juce::jlimit(
        0.0, std::max(0.0, duration - 0.001),
        parse(startEditor_, playStartSeconds_));
    auto end = juce::jlimit(
        start + 0.001, duration,
        parse(endEditor_, std::max(start + 0.001, playEndSeconds_)));
    auto loopStart = juce::jlimit(
        start, std::max(start, end - 0.001),
        parse(loopStartEditor_, loopStartSeconds_));
    auto loopEnd = juce::jlimit(
        loopStart + 0.001, end,
        parse(loopEndEditor_, std::max(loopStart + 0.001,
                                      loopEndSeconds_)));
    durationSeconds_ = duration;
    playStartSeconds_ = start;
    playEndSeconds_ = end;
    loopStartSeconds_ = loopStart;
    loopEndSeconds_ = loopEnd;
    const auto twoBars = 8.0 * 60.0 / std::max(20.0, tempoBpm_);
    displayDurationSeconds_ = durationSeconds_
                              + std::max(twoBars,
                                         durationSeconds_ * 0.25);
    visibleEndSeconds_ = juce::jlimit(
        visibleStartSeconds_ + 0.02, displayDurationSeconds_,
        visibleEndSeconds_);
    refreshTimelineControls();
    rebuildRegions();
    publishTimelineBounds();
}

void ClipTimelineEditorComponent::refreshTimelineControls()
{
    updatingTimelineControls_ = true;
    const auto format = [](const double value) {
        return juce::String(value, 3);
    };
    durationEditor_.setText(format(durationSeconds_), false);
    startEditor_.setText(format(playStartSeconds_), false);
    endEditor_.setText(format(playEndSeconds_), false);
    loopStartEditor_.setText(format(loopStartSeconds_), false);
    loopEndEditor_.setText(format(loopEndSeconds_), false);
    loopStartLabel_.setEnabled(loopButton_.getToggleState());
    loopEndLabel_.setEnabled(loopButton_.getToggleState());
    loopStartEditor_.setEnabled(loopButton_.getToggleState());
    loopEndEditor_.setEnabled(loopButton_.getToggleState());
    updatingTimelineControls_ = false;
}

} // namespace cydaw::app
