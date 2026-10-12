#include "TrackLaneComponent.h"
#include "LibraryDragPayload.h"
#include "PaintProfile.h"
#include "Localization.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace cydaw::app {
namespace {
constexpr std::array<juce::uint32, 8> trackColours {
    0xffff9b2f, 0xff66b5ff, 0xffd67cf2, 0xff74c96b,
    0xffffd15a, 0xff53d0c2, 0xffff7373, 0xff9d91ff
};

bool isAudioFile(const juce::File& file)
{
    const auto extension = file.getFileExtension().toLowerCase();
    return extension == ".wav" || extension == ".wave"
           || extension == ".aif" || extension == ".aiff"
           || extension == ".flac" || extension == ".ogg"
           || extension == ".mp3";
}
}

TrackLaneComponent::TrackLaneComponent(juce::AudioFormatManager& formatManager,
                                       juce::AudioThumbnailCache& cache)
    : waveform_(formatManager, cache)
{
    addAndMakeVisible(nameLabel_);
    addAndMakeVisible(muteButton_);
    addAndMakeVisible(soloButton_);
    addAndMakeVisible(armButton_);
    addAndMakeVisible(automationButton_);
    addAndMakeVisible(waveform_);
    addChildComponent(automationEditor_);
    // The playhead is the host's, one line over every lane; and the ribbon
    // is cached like the waveform, so what the line uncovers is blitted.
    waveform_.setPlayheadDrawnByHost(true);
    automationEditor_.setPlayheadDrawnByHost(true);
    automationEditor_.setBufferedToImage(true);

    automationEditor_.setCompactMode(true);
    automationEditor_.setTargets(
        { { "gain", tr("automation.trackGain"), -60.0f, 12.0f, " dB" } },
        "gain");
    automationEditor_.setAddHandler([this](const double time,
                                            const float value) {
        if (automationAddHandler_)
            automationAddHandler_(trackIndex_, time, value);
    });
    automationEditor_.setUpdateHandler(
        [this](const std::uint64_t id, const double time,
               const float value) {
            if (automationUpdateHandler_)
                automationUpdateHandler_(trackIndex_, id, time, value);
        });
    automationEditor_.setRemoveHandler([this](const std::uint64_t id) {
        if (automationRemoveHandler_)
            automationRemoveHandler_(trackIndex_, id);
    });
    automationEditor_.setSeekHandler([this](const double seconds) {
        select();
        if (seekHandler_)
            seekHandler_(trackIndex_, seconds);
    });

    nameLabel_.setInterceptsMouseClicks(false, false);
    nameLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffe8ebf2));
    nameLabel_.setFont(juce::FontOptions(11.5f, juce::Font::bold));
    nameLabel_.setJustificationType(juce::Justification::centredLeft);

    waveform_.setSeekHandler([this](const double seconds) {
        select();
        if (seekHandler_)
            seekHandler_(trackIndex_, seconds);
    });
    waveform_.setRegionSelectionHandler(
        [this](const std::uint64_t regionId,
               const WaveformComponent::RegionSelectionMode mode) {
        select();
        if (regionSelectionHandler_)
            regionSelectionHandler_(trackIndex_, regionId, mode);
    });
    waveform_.setRegionMoveHandler(
        [this](const std::uint64_t regionId, const double startSeconds,
               const int screenY) {
            select();
            if (regionMoveHandler_)
                regionMoveHandler_(trackIndex_, regionId, startSeconds,
                                   screenY);
        });
    waveform_.setRegionDragPreviewHandler(
        [this](const std::uint64_t regionId, const double startSeconds,
               const int screenY, const bool active) {
            if (regionDragPreviewHandler_)
                regionDragPreviewHandler_(trackIndex_, regionId,
                                          startSeconds, screenY, active);
        });
    waveform_.setRegionEditHandler(
        [this](const std::uint64_t regionId, const double startSeconds,
               const double sourceOffsetSeconds, const double durationSeconds,
               const double sourceDurationSeconds,
               const double fadeInSeconds, const double fadeOutSeconds) {
            select();
            if (regionEditHandler_)
                regionEditHandler_(trackIndex_, regionId, startSeconds,
                                   sourceOffsetSeconds, durationSeconds,
                                   sourceDurationSeconds,
                                   fadeInSeconds, fadeOutSeconds);
        });
    waveform_.setRegionCommandHandler(
        [this](const std::uint64_t regionId,
               const WaveformComponent::RegionCommand command,
               const double timelineSeconds) {
            select();
            if (regionCommandHandler_)
                regionCommandHandler_(trackIndex_, regionId, command,
                                      timelineSeconds);
        });
    waveform_.setRegionOpenHandler(
        [this](const std::uint64_t regionId) {
            select();
            if (regionOpenHandler_)
                regionOpenHandler_(trackIndex_, regionId);
        });
    waveform_.setZoomHandler(
        [this](const double factor, const double anchorSeconds) {
            if (zoomHandler_)
                zoomHandler_(factor, anchorSeconds);
        });
    waveform_.setPanHandler([this](const double deltaSeconds) {
        if (panHandler_)
            panHandler_(deltaSeconds);
    });
    waveform_.setVerticalPanHandler([this](const int deltaPixels) {
        if (verticalPanHandler_)
            verticalPanHandler_(deltaPixels);
    });
    waveform_.setMarqueeHandler(
        [this](const double anchorSeconds, const double currentSeconds,
               const int screenY,
               const WaveformComponent::MarqueePhase phase,
               const bool additive) {
            if (phase == WaveformComponent::MarqueePhase::begin)
                select();
            if (marqueeHandler_)
                marqueeHandler_(trackIndex_, anchorSeconds,
                                 currentSeconds, screenY, phase,
                                 additive);
        });
    muteButton_.onClick = [this] {
        select();
        if (muteHandler_)
            muteHandler_(trackIndex_, muteButton_.getToggleState());
    };
    soloButton_.onClick = [this] {
        select();
        if (soloHandler_)
            soloHandler_(trackIndex_, soloButton_.getToggleState());
    };
    armButton_.onClick = [this] {
        select();
        if (armHandler_)
            armHandler_(trackIndex_, armButton_.getToggleState());
    };
    automationButton_.onClick = [this] {
        setAutomationVisible(automationButton_.getToggleState());
        if (automationVisibilityHandler_)
            automationVisibilityHandler_(trackIndex_, automationVisible_);
    };
    armButton_.setColour(juce::ToggleButton::tickColourId,
                         juce::Colour(0xffff5864));
    refreshTranslations();
}

void TrackLaneComponent::configure(const std::size_t trackIndex,
                                   const juce::File& file,
                                   juce::String displayName,
                                   const juce::Colour colour,
                                   const bool muted,
                                   const bool solo,
                                   const bool armed)
{
    trackIndex_ = trackIndex;
    file_ = file;
    displayName_ = std::move(displayName);
    accentColour_ = colour.isTransparent()
                        ? juce::Colour(
                              trackColours[trackIndex % trackColours.size()])
                        : colour;
    waveform_.setAccentColour(accentColour_);
    waveform_.setFile(file);
    setMixerState(muted, solo, armed);
    refreshTranslations();
}

void TrackLaneComponent::setPresentation(juce::String displayName,
                                         const juce::Colour colour)
{
    displayName_ = std::move(displayName);
    if (!colour.isTransparent())
        accentColour_ = colour;
    waveform_.setAccentColour(accentColour_);
    refreshTranslations();
    repaint();
}

void TrackLaneComponent::setSelected(const bool selected)
{
    if (selected_ != selected) {
        selected_ = selected;
        repaint();
    }
}

void TrackLaneComponent::setPlayhead(const double seconds)
{
    waveform_.setPlayhead(seconds);
    automationEditor_.setPlayhead(seconds);
}

void TrackLaneComponent::setTimelineRange(const double timelineLengthSeconds,
                                          const double clipStartSeconds,
                                          const double sourceOffsetSeconds,
                                          const double clipDurationSeconds)
{
    timelineLengthSeconds_ = std::max(1.0, timelineLengthSeconds);
    waveform_.setTimelineRange(timelineLengthSeconds,
                               clipStartSeconds,
                               sourceOffsetSeconds,
                               clipDurationSeconds);
}

void TrackLaneComponent::setVisibleRange(const double startSeconds,
                                         const double endSeconds)
{
    visibleStartSeconds_ = std::max(0.0, startSeconds);
    visibleEndSeconds_ = std::max(visibleStartSeconds_ + 0.000001,
                                  endSeconds);
    waveform_.setVisibleRange(visibleStartSeconds_, visibleEndSeconds_);
    automationEditor_.setVisibleRange(visibleStartSeconds_,
                                      visibleEndSeconds_);
}

void TrackLaneComponent::setTimeSelection(const double startSeconds,
                                          const double endSeconds,
                                          const bool trackIncluded,
                                          const bool visible)
{
    waveform_.setTimeSelection(startSeconds, endSeconds,
                               trackIncluded, visible);
}

void TrackLaneComponent::setTimelineRegions(
    const double timelineLengthSeconds,
    std::vector<WaveformComponent::Region> regions,
    std::vector<std::uint64_t> selectedRegionIds,
    const std::uint64_t primaryRegionId)
{
    timelineLengthSeconds_ = std::max(1.0, timelineLengthSeconds);
    waveform_.setRegions(timelineLengthSeconds, std::move(regions),
                         std::move(selectedRegionIds), primaryRegionId);
}

void TrackLaneComponent::setMusicalGrid(const double tempoBpm,
                                        const int division,
                                        const bool snappingEnabled)
{
    waveform_.setMusicalGrid(tempoBpm, division, snappingEnabled);
}

void TrackLaneComponent::setEditingTool(
    const WaveformComponent::EditingTool tool)
{
    waveform_.setEditingTool(tool);
}

void TrackLaneComponent::setSnapMode(
    const WaveformComponent::SnapMode mode)
{
    waveform_.setSnapMode(mode);
}

void TrackLaneComponent::setMixerState(const bool muted, const bool solo,
                                       const bool armed)
{
    muteButton_.setToggleState(muted, juce::dontSendNotification);
    soloButton_.setToggleState(solo, juce::dontSendNotification);
    armButton_.setToggleState(armed, juce::dontSendNotification);
}

juce::Rectangle<int> TrackLaneComponent::meterBounds() const noexcept
{
    constexpr auto meterTop = 27;
    const auto meterHeight = std::max(12, getHeight() - meterTop - 7);
    return { 164, meterTop, 19, meterHeight };
}

void TrackLaneComponent::setMeterLevels(
    const std::array<float, 2>& peak,
    const std::array<float, 2>& rms)
{
    for (std::size_t channel = 0; channel < meterPeak_.size(); ++channel) {
        meterPeak_[channel] = std::max(
            juce::jlimit(0.0f, 1.5f, peak[channel]),
            meterPeak_[channel] * 0.86f);
        meterRms_[channel] = meterRms_[channel] * 0.72f
                             + juce::jlimit(0.0f, 1.5f, rms[channel])
                                   * 0.28f;
    }
    // Meters tick continuously during playback. Repainting the whole lane for
    // them dragged the waveform, the clips and the automation lane along with
    // two six-pixel bars.
    repaint(meterBounds());
}

void TrackLaneComponent::setMovePreview(
    std::optional<WaveformComponent::Region> preview)
{
    moveTargetPreview_ = preview.has_value();
    waveform_.setMovePreview(std::move(preview));
    repaint();
}

void TrackLaneComponent::setAutomationPoints(
    std::vector<AutomationEditorComponent::Point> points,
    const double durationSeconds, const double visibleStartSeconds,
    const double visibleEndSeconds)
{
    automationEditor_.setPoints(std::move(points), durationSeconds);
    automationEditor_.setVisibleRange(visibleStartSeconds,
                                      visibleEndSeconds);
}

void TrackLaneComponent::setAutomationVisible(const bool visible)
{
    if (automationVisible_ == visible)
        return;
    automationVisible_ = visible;
    automationButton_.setToggleState(visible, juce::dontSendNotification);
    automationEditor_.setVisible(visible);
    resized();
    repaint();
}

bool TrackLaneComponent::isAutomationVisible() const noexcept
{
    return automationVisible_;
}

void TrackLaneComponent::setTrackDropTarget(const bool target)
{
    if (trackDropTarget_ == target)
        return;
    trackDropTarget_ = target;
    repaint();
}

void TrackLaneComponent::setArmEnabled(const bool enabled)
{
    armButton_.setEnabled(enabled);
}

void TrackLaneComponent::refreshTranslations()
{
    auto name = displayName_.trim();
    if (name.isEmpty()) {
        name = tr("track.label");
        name = name.replace(
            "{index}", juce::String(static_cast<int>(trackIndex_ + 1)));
        name = name.replace(
            "{file}", file_ == juce::File {} ? tr("track.noFile")
                                               : file_.getFileName());
    } else if (file_ != juce::File {}) {
        name += "\n" + file_.getFileName();
    }
    nameLabel_.setText(name, juce::dontSendNotification);
    muteButton_.setButtonText(tr("track.muteShort"));
    soloButton_.setButtonText(tr("track.soloShort"));
    armButton_.setButtonText(tr("track.armShort"));
    automationButton_.setButtonText("A");
    muteButton_.setTooltip(tr("track.mute"));
    soloButton_.setTooltip(tr("track.solo"));
    armButton_.setTooltip(tr("track.arm"));
    automationButton_.setTooltip(tr("automation.toggleTrack"));
    waveform_.setTooltip(tr("track.audioDropHelp"));
    automationEditor_.refreshTranslations();
    automationEditor_.setTooltip(tr("automation.inlineHelp"));
    setTooltip(tr("track.dragReorderHelp"));
}

void TrackLaneComponent::setSelectionHandler(std::function<void(std::size_t)> handler)
{
    selectionHandler_ = std::move(handler);
}

void TrackLaneComponent::setSeekHandler(std::function<void(std::size_t, double)> handler)
{
    seekHandler_ = std::move(handler);
}

void TrackLaneComponent::setMuteHandler(std::function<void(std::size_t, bool)> handler)
{
    muteHandler_ = std::move(handler);
}

void TrackLaneComponent::setSoloHandler(std::function<void(std::size_t, bool)> handler)
{
    soloHandler_ = std::move(handler);
}

void TrackLaneComponent::setArmHandler(
    std::function<void(std::size_t, bool)> handler)
{
    armHandler_ = std::move(handler);
}

void TrackLaneComponent::setRegionSelectionHandler(
    std::function<void(std::size_t, std::uint64_t,
                       WaveformComponent::RegionSelectionMode)> handler)
{
    regionSelectionHandler_ = std::move(handler);
}

void TrackLaneComponent::setRegionMoveHandler(
    std::function<void(std::size_t, std::uint64_t, double, int)> handler)
{
    regionMoveHandler_ = std::move(handler);
}

void TrackLaneComponent::setRegionDragPreviewHandler(
    std::function<void(std::size_t, std::uint64_t, double, int, bool)>
        handler)
{
    regionDragPreviewHandler_ = std::move(handler);
}

void TrackLaneComponent::setRegionEditHandler(
    std::function<void(std::size_t, std::uint64_t, double, double,
                       double, double, double, double)> handler)
{
    regionEditHandler_ = std::move(handler);
}

void TrackLaneComponent::setRegionCommandHandler(
    std::function<void(std::size_t, std::uint64_t,
                       WaveformComponent::RegionCommand, double)> handler)
{
    regionCommandHandler_ = std::move(handler);
}

void TrackLaneComponent::setRegionOpenHandler(
    std::function<void(std::size_t, std::uint64_t)> handler)
{
    regionOpenHandler_ = std::move(handler);
}

void TrackLaneComponent::setAudioDropHandler(
    std::function<void(std::size_t, const juce::File&, double)> handler)
{
    audioDropHandler_ = std::move(handler);
}

void TrackLaneComponent::setZoomHandler(
    std::function<void(double, double)> handler)
{
    zoomHandler_ = std::move(handler);
}

void TrackLaneComponent::setPanHandler(
    std::function<void(double)> handler)
{
    panHandler_ = std::move(handler);
}

void TrackLaneComponent::setVerticalPanHandler(
    std::function<void(int)> handler)
{
    verticalPanHandler_ = std::move(handler);
}

void TrackLaneComponent::setMarqueeHandler(
    std::function<void(std::size_t, double, double, int,
                       WaveformComponent::MarqueePhase, bool)> handler)
{
    marqueeHandler_ = std::move(handler);
}

void TrackLaneComponent::setContextMenuHandler(
    std::function<void(std::size_t, juce::Component*)> handler)
{
    contextMenuHandler_ = std::move(handler);
}

void TrackLaneComponent::setAutomationAddHandler(
    std::function<void(std::size_t, double, float)> handler)
{
    automationAddHandler_ = std::move(handler);
}

void TrackLaneComponent::setAutomationUpdateHandler(
    std::function<void(std::size_t, std::uint64_t, double, float)> handler)
{
    automationUpdateHandler_ = std::move(handler);
}

void TrackLaneComponent::setAutomationRemoveHandler(
    std::function<void(std::size_t, std::uint64_t)> handler)
{
    automationRemoveHandler_ = std::move(handler);
}

void TrackLaneComponent::setAutomationVisibilityHandler(
    std::function<void(std::size_t, bool)> handler)
{
    automationVisibilityHandler_ = std::move(handler);
}

void TrackLaneComponent::setLaneHeightHandler(
    std::function<void(std::size_t, int)> handler)
{
    laneHeightHandler_ = std::move(handler);
}

void TrackLaneComponent::setTrackReorderHandler(
    std::function<void(std::size_t, int, bool)> handler)
{
    trackReorderHandler_ = std::move(handler);
}

void TrackLaneComponent::paint(juce::Graphics& graphics)
{
    const paint_profile::Scoped paintScope(paint_profile::counters().lane);
    const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    graphics.setColour(juce::Colour(selected_ ? 0xff35404a : 0xff2a2d30));
    graphics.fillRoundedRectangle(bounds, 2.0f);
    graphics.setColour(selected_ ? accentColour_.withAlpha(0.88f)
                                 : juce::Colour(0xff44484c));
    graphics.drawRoundedRectangle(bounds, 2.0f, selected_ ? 2.0f : 1.0f);
    graphics.setColour(accentColour_);
    graphics.fillRect(1, 2, selected_ ? 6 : 3, getHeight() - 4);
    if (selected_)
        graphics.fillRect(7, 2, 177, 3);

    const auto meters = meterBounds();
    for (std::size_t channel = 0; channel < 2; ++channel) {
        const auto x = meters.getX() + static_cast<int>(channel) * 9;
        const auto meter = juce::Rectangle<int>(x, meters.getY(), 6,
                                                meters.getHeight());
        graphics.setColour(juce::Colour(0xff111418));
        graphics.fillRoundedRectangle(meter.toFloat(), 1.5f);
        const auto db = juce::Decibels::gainToDecibels(
            std::max(0.000001f, meterRms_[channel]), -60.0f);
        const auto amount = juce::jlimit(0.0f, 1.0f,
                                        (db + 60.0f) / 60.0f);
        auto level = meter;
        level.removeFromTop(juce::roundToInt(
            static_cast<float>(meter.getHeight()) * (1.0f - amount)));
        const auto colour = meterPeak_[channel] >= 1.0f
                                ? juce::Colour(0xffff4f4f)
                                : amount > 0.82f
                                      ? juce::Colour(0xffffc13b)
                                      : juce::Colour(0xff72d15e);
        graphics.setColour(colour);
        graphics.fillRoundedRectangle(level.toFloat(), 1.5f);
    }
    if (audioDragHover_) {
        graphics.setColour(juce::Colour(0xff53d0c2).withAlpha(0.18f));
        graphics.fillRoundedRectangle(waveform_.getBounds().toFloat(), 3.0f);
        graphics.setColour(juce::Colour(0xff8ef5de));
        graphics.drawRoundedRectangle(waveform_.getBounds().toFloat(), 3.0f,
                                      2.0f);
    }
    if (moveTargetPreview_) {
        graphics.setColour(juce::Colour(0xff53d0c2).withAlpha(0.22f));
        graphics.fillRoundedRectangle(bounds, 2.0f);
        graphics.setColour(juce::Colour(0xff8ef5de));
        graphics.drawRoundedRectangle(bounds, 2.0f, 2.0f);
    }
    if (trackDropTarget_) {
        graphics.setColour(juce::Colour(0xffff9b2f));
        graphics.fillRect(1, 0, getWidth() - 2, 3);
    }
    graphics.setColour(juce::Colour(0xff5a616b));
    graphics.fillRect(190, getHeight() - 4, getWidth() - 194, 1);
}

void TrackLaneComponent::resized()
{
    auto area = getLocalBounds().reduced(2);
    area.removeFromBottom(5);
    auto header = area.removeFromLeft(186).reduced(8, 4);
    header.removeFromRight(25);
    auto buttons = header.removeFromBottom(24);
    muteButton_.setBounds(buttons.removeFromLeft(32).reduced(1));
    soloButton_.setBounds(buttons.removeFromLeft(32).reduced(1));
    armButton_.setBounds(buttons.removeFromLeft(32).reduced(1));
    automationButton_.setBounds(buttons.removeFromLeft(32).reduced(1));
    nameLabel_.setBounds(header);
    auto automation = juce::Rectangle<int> {};
    if (automationVisible_) {
        const auto automationHeight = juce::jlimit(
            54, 126,
            juce::roundToInt(static_cast<float>(area.getHeight()) * 0.44f));
        automation = area.removeFromBottom(automationHeight);
    }
    waveform_.setBounds(area.reduced(2));
    automationEditor_.setBounds(automationVisible_
                                    ? automation.reduced(2, 1)
                                    : juce::Rectangle<int> {});
}

void TrackLaneComponent::mouseDown(const juce::MouseEvent& event)
{
    select();
    if (event.mods.isPopupMenu() && contextMenuHandler_)
        contextMenuHandler_(trackIndex_, this);
    if (!event.mods.isLeftButtonDown())
        return;
    dragStartScreenY_ = event.getScreenY();
    if (event.position.y >= static_cast<float>(getHeight() - 7)) {
        resizingLane_ = true;
        resizeStartHeight_ = getHeight();
    } else if (event.position.x < 186.0f) {
        reorderingTrack_ = true;
    }
}

void TrackLaneComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (resizingLane_) {
        if (laneHeightHandler_)
            laneHeightHandler_(trackIndex_, juce::jlimit(
                82, 420,
                resizeStartHeight_ + event.getScreenY() - dragStartScreenY_));
        return;
    }
    if (reorderingTrack_ && trackReorderHandler_
        && std::abs(event.getScreenY() - dragStartScreenY_) >= 4)
        trackReorderHandler_(trackIndex_, event.getScreenY(), true);
}

void TrackLaneComponent::mouseUp(const juce::MouseEvent& event)
{
    if (reorderingTrack_ && trackReorderHandler_)
        trackReorderHandler_(trackIndex_, event.getScreenY(), false);
    resizingLane_ = false;
    reorderingTrack_ = false;
}

bool TrackLaneComponent::isInterestedInFileDrag(
    const juce::StringArray& files)
{
    return files.size() == 1 && isAudioFile(juce::File(files[0]));
}

bool TrackLaneComponent::isInterestedInDragSource(const SourceDetails& details)
{
    const auto file = sampleFileFromDragPayload(details.description);
    return file.has_value() && isAudioFile(*file);
}

void TrackLaneComponent::itemDragEnter(const SourceDetails&)
{
    audioDragHover_ = true;
    repaint();
}

void TrackLaneComponent::itemDragExit(const SourceDetails&)
{
    audioDragHover_ = false;
    repaint();
}

void TrackLaneComponent::itemDropped(const SourceDetails& details)
{
    if (const auto file = sampleFileFromDragPayload(details.description))
        filesDropped(juce::StringArray { file->getFullPathName() },
                     details.localPosition.x, details.localPosition.y);
}

void TrackLaneComponent::fileDragEnter(const juce::StringArray&, int, int)
{
    audioDragHover_ = true;
    repaint();
}

void TrackLaneComponent::fileDragExit(const juce::StringArray&)
{
    audioDragHover_ = false;
    repaint();
}

void TrackLaneComponent::filesDropped(const juce::StringArray& files,
                                      const int x,
                                      int)
{
    audioDragHover_ = false;
    repaint();
    if (files.size() != 1 || !audioDropHandler_)
        return;
    const auto waveformWidth = std::max(1, waveform_.getWidth());
    const auto relativeX = juce::jlimit(0, waveformWidth,
                                       x - waveform_.getX());
    const auto startSeconds = visibleStartSeconds_
                              + static_cast<double>(relativeX)
                                    / static_cast<double>(waveformWidth)
                                    * std::max(
                                        0.000001,
                                        visibleEndSeconds_
                                            - visibleStartSeconds_);
    select();
    audioDropHandler_(trackIndex_, juce::File(files[0]), startSeconds);
}

void TrackLaneComponent::select()
{
    if (selectionHandler_)
        selectionHandler_(trackIndex_);
}

} // namespace cydaw::app
