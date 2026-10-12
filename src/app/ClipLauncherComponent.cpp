#include "ClipLauncherComponent.h"
#include "PaintProfile.h"
#include "LibraryDragPayload.h"
#include "Localization.h"
#include "MixerStripDrawing.h"

#include <algorithm>
#include <cmath>

namespace cydaw::app {
using mixer_strip::dbForFaderAmount;
using mixer_strip::drawFaderAndMeter;
using mixer_strip::faderAmountForDb;
using mixer_strip::faderYForDb;
using mixer_strip::meterAmountForGain;
namespace {
constexpr int headerHeight = 38;
constexpr int sceneHeaderWidth = 82;
constexpr int gap = 2;

bool isSupportedAudioSample(const juce::File& file)
{
    const auto extension = file.getFileExtension().toLowerCase();
    return file.existsAsFile()
           && (extension == ".wav" || extension == ".aif"
               || extension == ".aiff" || extension == ".flac"
               || extension == ".ogg" || extension == ".mp3");
}

bool isSupportedPluginBundle(const juce::File& file)
{
    const auto extension = file.getFileExtension().toLowerCase();
    return file.exists()
           && (extension == ".vst3" || extension == ".dll"
               || extension == ".so");
}

}

ClipLauncherComponent::ClipLauncherComponent()
{
    addAndMakeVisible(quantizationSelector_);
    quantizationSelector_.onChange = [this] {
        if (quantizationHandler_)
            quantizationHandler_(std::max(0, quantizationSelector_.getSelectedId() - 1));
    };
    addChildComponent(gainValueEditor_);
    gainValueEditor_.setJustification(juce::Justification::centred);
    gainValueEditor_.setInputRestrictions(8, "-+0123456789.,");
    gainValueEditor_.setColour(juce::TextEditor::backgroundColourId,
                               juce::Colour(0xff111419));
    gainValueEditor_.setColour(juce::TextEditor::outlineColourId,
                               juce::Colour(0xffff8a00));
    gainValueEditor_.setColour(juce::TextEditor::textColourId,
                               juce::Colour(0xfff2f4f7));
    gainValueEditor_.onReturnKey = [this] { finishGainValueEdit(true); };
    gainValueEditor_.onEscapeKey = [this] { finishGainValueEdit(false); };
    gainValueEditor_.onFocusLost = [this] { finishGainValueEdit(true); };
}

bool ClipLauncherComponent::selfTestInputRouting()
{
    ClipLauncherComponent launcher;
    launcher.setBounds(0, 0, 1'000, 700);
    Track track;
    track.name = "MIDI test";
    track.midi = true;
    track.armed = true;
    auto secondTrack = track;
    secondTrack.name = "MIDI second";
    secondTrack.armed = false;
    launcher.setTracks({ track, secondTrack });

    auto created = false;
    auto addedTrack = false;
    auto stoppedTrack = false;
    auto movedTrack = false;
    launcher.setEmptySlotCreateHandler(
        [&created](const std::size_t trackIndex, const int sceneIndex) {
            created = trackIndex == 0 && sceneIndex == 0;
        });
    launcher.setAddTrackHandler([&addedTrack] { addedTrack = true; });
    launcher.setTrackStopHandler(
        [&stoppedTrack](const std::size_t trackIndex) {
            stoppedTrack = trackIndex == 0;
        });
    launcher.setTrackMoveHandler(
        [&movedTrack](const std::size_t fromIndex,
                      const std::size_t toIndex) {
            movedTrack = fromIndex == 0 && toIndex == 1;
        });
    const auto now = juce::Time::getCurrentTime();
    const auto source = juce::Desktop::getInstance().getMainMouseSource();
    const auto trigger = launcher.slotTriggerBounds(0, 0)
                             .getCentre().toFloat();
    launcher.mouseDoubleClick(juce::MouseEvent(
        source, trigger,
        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        &launcher, &launcher, now, trigger, now, 2, false));

    const auto add = launcher.addTrackBounds().getCentre().toFloat();
    launcher.mouseDown(juce::MouseEvent(
        source, add,
        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        &launcher, &launcher, now, add, now, 1, false));
    const auto stop = launcher.stopTrackBounds(0).getCentre().toFloat();
    launcher.mouseDown(juce::MouseEvent(
        source, stop,
        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        &launcher, &launcher, now, stop, now, 1, false));

    const auto dragStart = launcher.trackBounds(0).getCentre().toFloat();
    const auto dragEnd = launcher.trackBounds(1).getCentre().toFloat();
    launcher.mouseDown(juce::MouseEvent(
        source, dragStart,
        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        &launcher, &launcher, now, dragStart, now, 1, false));
    launcher.mouseDrag(juce::MouseEvent(
        source, dragEnd,
        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        &launcher, &launcher, now, dragStart, now, 1, true));
    launcher.mouseUp(juce::MouseEvent(
        source, dragEnd, juce::ModifierKeys {},
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        &launcher, &launcher, now, dragStart, now, 1, true));
    return created && addedTrack && stoppedTrack && movedTrack;
}

void ClipLauncherComponent::setTracks(std::vector<Track> tracks)
{
    for (std::size_t index = 0; index < tracks.size()
                                && index < tracks_.size(); ++index) {
        tracks[index].peak = tracks_[index].peak;
        tracks[index].rms = tracks_[index].rms;
    }
    tracks_ = std::move(tracks);
    repaint();
}

void ClipLauncherComponent::setTrackMeterLevels(
    const std::size_t trackIndex, const std::array<float, 2>& peak,
    const std::array<float, 2>& rms)
{
    if (trackIndex >= tracks_.size())
        return;
    auto& track = tracks_[trackIndex];
    // Silence stays silent: a strip is repainted only when its meter moves.
    if (!meterMoved(track.peak, peak) && !meterMoved(track.rms, rms))
        return;
    track.peak = peak;
    track.rms = rms;
    repaint(mixerBounds(trackIndex));
}

bool ClipLauncherComponent::meterMoved(const std::array<float, 2>& shown,
                                       const std::array<float, 2>& next) noexcept
{
    return std::abs(shown[0] - next[0]) > 0.002f
           || std::abs(shown[1] - next[1]) > 0.002f;
}

void ClipLauncherComponent::setTrackPlaybackState(
    const std::size_t trackIndex, const int activeScene,
    const int queuedScene, const float playbackProgress)
{
    if (trackIndex >= tracks_.size())
        return;

    auto changed = false;
    for (int sceneIndex = 0; sceneIndex < sceneCount; ++sceneIndex) {
        auto& slot = tracks_[trackIndex]
                         .slots[static_cast<std::size_t>(sceneIndex)];
        const auto active = slot.occupied && activeScene == sceneIndex;
        const auto queued = slot.occupied && queuedScene == sceneIndex;
        const auto stopQueued = slot.occupied && active
                                && queuedScene == -2;
        const auto progress = active
                                  ? juce::jlimit(0.0f, 1.0f,
                                                 playbackProgress)
                                  : 0.0f;
        changed = changed || slot.active != active || slot.queued != queued
                  || slot.stopQueued != stopQueued
                  || std::abs(slot.playbackProgress - progress) > 0.002f;
        slot.active = active;
        slot.queued = queued;
        slot.stopQueued = stopQueued;
        slot.playbackProgress = progress;
    }

    if (changed) {
        const auto column = trackBounds(trackIndex);
        repaint(column.withTop(headerHeight)
                    .withBottom(clipAreaBottom()));
    }
}

void ClipLauncherComponent::setMasterMeterLevels(
    const std::array<float, 2>& peak, const std::array<float, 2>& rms)
{
    if (!meterMoved(masterPeak_, peak) && !meterMoved(masterRms_, rms))
        return;
    masterPeak_ = peak;
    masterRms_ = rms;
    repaint(mixerBounds(tracks_.size()));
}

void ClipLauncherComponent::setMasterGain(const float gainDb)
{
    masterGainDb_ = juce::jlimit(-60.0f, 12.0f, gainDb);
    repaint(masterBounds());
}

void ClipLauncherComponent::refreshTranslations()
{
    emptyText_ = tr("launcher.emptySlot");
    setTooltip(tr("launcher.help"));
    const auto selected = std::max(1, quantizationSelector_.getSelectedId());
    quantizationSelector_.clear(juce::dontSendNotification);
    quantizationSelector_.addItem(tr("launcher.quantization.off"), 1);
    quantizationSelector_.addItem(tr("launcher.quantization.quarter"), 2);
    quantizationSelector_.addItem(tr("launcher.quantization.half"), 3);
    quantizationSelector_.addItem(tr("launcher.quantization.beat"), 4);
    quantizationSelector_.addItem(tr("launcher.quantization.bar"), 5);
    quantizationSelector_.setSelectedId(selected, juce::dontSendNotification);
    quantizationSelector_.setTooltip(tr("launcher.quantization.help"));
    gainValueEditor_.setTooltip(tr("launcher.gainValueHelp"));
    repaint();
}

void ClipLauncherComponent::setCaptureHandler(
    std::function<void(std::size_t, int)> handler)
{
    captureHandler_ = std::move(handler);
}

void ClipLauncherComponent::setLaunchHandler(
    std::function<void(std::size_t, int)> handler)
{
    launchHandler_ = std::move(handler);
}

void ClipLauncherComponent::setClearHandler(
    std::function<void(std::size_t, int)> handler)
{
    clearHandler_ = std::move(handler);
}

void ClipLauncherComponent::setSlotOpenHandler(
    std::function<void(std::size_t, int)> handler)
{
    slotOpenHandler_ = std::move(handler);
}

void ClipLauncherComponent::setEmptySlotCreateHandler(
    std::function<void(std::size_t, int)> handler)
{
    emptySlotCreateHandler_ = std::move(handler);
}

void ClipLauncherComponent::setSlotLoopHandler(
    std::function<void(std::size_t, int, bool)> handler)
{
    slotLoopHandler_ = std::move(handler);
}

void ClipLauncherComponent::setLaunchSceneHandler(std::function<void(int)> handler)
{
    launchSceneHandler_ = std::move(handler);
}

void ClipLauncherComponent::setQuantization(const int quantizationIndex)
{
    quantizationSelector_.setSelectedId(juce::jlimit(0, 4, quantizationIndex) + 1,
                                        juce::dontSendNotification);
}

void ClipLauncherComponent::setQuantizationHandler(std::function<void(int)> handler)
{
    quantizationHandler_ = std::move(handler);
}

void ClipLauncherComponent::setTrackSelectHandler(
    std::function<void(std::size_t)> handler)
{
    trackSelectHandler_ = std::move(handler);
}

void ClipLauncherComponent::setSlotSelectHandler(
    std::function<void(std::size_t, int)> handler)
{
    slotSelectHandler_ = std::move(handler);
}

void ClipLauncherComponent::setTrackStopHandler(
    std::function<void(std::size_t)> handler)
{
    trackStopHandler_ = std::move(handler);
}

void ClipLauncherComponent::setTrackMoveHandler(
    std::function<void(std::size_t, std::size_t)> handler)
{
    trackMoveHandler_ = std::move(handler);
}

void ClipLauncherComponent::setAddTrackHandler(
    std::function<void()> handler)
{
    addTrackHandler_ = std::move(handler);
}

void ClipLauncherComponent::setTrackContextHandler(
    std::function<void(std::size_t, juce::Component*)> handler)
{
    trackContextHandler_ = std::move(handler);
}

void ClipLauncherComponent::setDeviceAddHandler(
    std::function<void(std::size_t)> handler)
{
    deviceAddHandler_ = std::move(handler);
}

void ClipLauncherComponent::setSampleDropHandler(
    std::function<void(std::size_t, int, const juce::File&)> handler)
{
    sampleDropHandler_ = std::move(handler);
}

void ClipLauncherComponent::setSlotBrowseHandler(
    std::function<void(std::size_t, int)> handler)
{
    slotBrowseHandler_ = std::move(handler);
}

void ClipLauncherComponent::setClipTransferHandler(
    std::function<void(std::size_t, int, std::size_t, int, bool)> handler)
{
    clipTransferHandler_ = std::move(handler);
}

void ClipLauncherComponent::setDeviceDropHandler(
    std::function<void(std::size_t, const juce::var&)> handler)
{
    deviceDropHandler_ = std::move(handler);
}

void ClipLauncherComponent::setMixerHandler(
    std::function<void(std::size_t, float, float)> handler)
{
    mixerHandler_ = std::move(handler);
}

void ClipLauncherComponent::setMuteHandler(
    std::function<void(std::size_t, bool)> handler)
{
    muteHandler_ = std::move(handler);
}

void ClipLauncherComponent::setSoloHandler(
    std::function<void(std::size_t, bool)> handler)
{
    soloHandler_ = std::move(handler);
}

void ClipLauncherComponent::setArmHandler(
    std::function<void(std::size_t, bool)> handler)
{
    armHandler_ = std::move(handler);
}

void ClipLauncherComponent::setMasterMixerHandler(
    std::function<void(float)> handler)
{
    masterMixerHandler_ = std::move(handler);
}

void ClipLauncherComponent::paint(juce::Graphics& graphics)
{
    const paint_profile::Scoped paintScope(paint_profile::counters().launcher);
    // A meter's tick dirties one strip; the grid, the headers and the other
    // columns are not drawn for it.
    const auto clip = graphics.getClipBounds();
    graphics.fillAll(juce::Colour(0xff11141a));
    graphics.setColour(juce::Colour(0xff242a34));
    graphics.fillRect(0, 0, getWidth(), headerHeight);
    graphics.setColour(juce::Colour(0xffe8ebf2));
    graphics.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    graphics.drawText(tr("launcher.scenes"), 7, 0, sceneHeaderWidth - 12,
                      16, juce::Justification::centredLeft);

    for (std::size_t trackIndex = 0; trackIndex < tracks_.size(); ++trackIndex) {
        auto header = trackBounds(trackIndex);
        header.setY(0);
        header.setHeight(headerHeight);
        if (!clip.intersects(header))
            continue;
        graphics.setColour(juce::Colour(tracks_[trackIndex].selected
                                            ? 0xff32363d : 0xff1c2028));
        graphics.fillRoundedRectangle(header.reduced(1).toFloat(), 3.0f);
        graphics.setColour(tracks_[trackIndex].selected
                               ? juce::Colour(0xffff8a00)
                               : juce::Colour(0xff59616d));
        graphics.fillRect(header.getX() + 1, 1, header.getWidth() - 2, 3);
        graphics.setColour(juce::Colour(0xffcbd1dc));
        const auto typeLabel = tracks_[trackIndex].midi
                                   ? juce::String("MIDI  ")
                                   : juce::String("AUDIO  ");
        graphics.drawFittedText(typeLabel + tracks_[trackIndex].name,
                                header.reduced(6).withTrimmedTop(4),
                                juce::Justification::centredLeft, 1, 0.72f);
    }

    const auto addTrack = addTrackBounds();
    if (!addTrack.isEmpty()) {
        const auto hovered = hovered_.kind == HitKind::addTrack;
        graphics.setColour(juce::Colour(hovered ? 0xff343b46
                                                : 0xff20252d));
        graphics.fillRoundedRectangle(addTrack.reduced(1).toFloat(), 3.0f);
        graphics.setColour(juce::Colour(hovered ? 0xffffad45
                                                : 0xff8f99a8));
        graphics.setFont(juce::FontOptions(17.0f, juce::Font::bold));
        graphics.drawText("+", addTrack,
                          juce::Justification::centred, false);
    }

    for (int sceneIndex = 0; sceneIndex < sceneCount; ++sceneIndex) {
        const auto scene = sceneBounds(sceneIndex);
        if (clip.intersects(scene)) {
            const auto sceneHovered = !hovered_.trackIndex
                                      && hovered_.sceneIndex == sceneIndex;
            graphics.setColour(juce::Colour(sceneHovered ? 0xff394350 : 0xff252b35));
            graphics.fillRoundedRectangle(scene.reduced(1).toFloat(), 3.0f);
            graphics.setColour(juce::Colour(0xff9ca6b6));
            graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
            graphics.drawText(juce::String::fromUTF8("▶  ")
                                  + tr("launcher.scene", "count",
                                       juce::String(sceneIndex + 1)),
                              scene.reduced(6), juce::Justification::centredLeft);
        }

        for (std::size_t trackIndex = 0; trackIndex < tracks_.size(); ++trackIndex) {
            const auto bounds = slotBounds(trackIndex, sceneIndex);
            if (!clip.intersects(bounds))
                continue;
            const auto& slot = tracks_[trackIndex].slots[static_cast<std::size_t>(sceneIndex)];
            const auto trigger = slotTriggerBounds(trackIndex, sceneIndex);
            const auto hovered = hovered_.trackIndex == trackIndex
                                 && hovered_.sceneIndex == sceneIndex;
            auto background = slot.occupied ? juce::Colour(slot.colour)
                                            : juce::Colour(0xff1a1e25);
            if (slot.recording)
                background = juce::Colour(0xff5a2028);
            else if (slot.recordPending || slot.recordStopPending)
                background = juce::Colour(0xff49331d);
            if (slot.occupied && !slot.active)
                background = background.withMultipliedBrightness(0.62f);
            if (hovered)
                background = background.brighter(0.16f);
            graphics.setColour(background);
            graphics.fillRoundedRectangle(bounds.reduced(1).toFloat(), 3.0f);
            graphics.setColour(slot.active ? juce::Colour(0xffffffff)
                                           : slot.queued ? juce::Colour(0xffffb04a)
                                                         : juce::Colour(0xff414957));
            graphics.drawRoundedRectangle(bounds.reduced(1).toFloat(), 3.0f,
                                           slot.active || slot.queued ? 2.0f : 1.0f);
            const auto triggerHovered = hovered
                                        && (hovered_.kind == HitKind::slotPlay
                                            || hovered_.kind
                                                   == HitKind::slotRecord);
            if (slot.occupied) {
                graphics.setColour(slot.active
                                       ? juce::Colour(0xfff2f4f7)
                                       : triggerHovered
                                             ? juce::Colour(0xffffb04a)
                                             : juce::Colour(0xff303640));
                graphics.fillRoundedRectangle(trigger.toFloat(), 2.0f);
                juce::Path play;
                play.startNewSubPath(static_cast<float>(trigger.getX() + 5),
                                     static_cast<float>(trigger.getY() + 4));
                play.lineTo(static_cast<float>(trigger.getRight() - 4),
                            static_cast<float>(trigger.getCentreY()));
                play.lineTo(static_cast<float>(trigger.getX() + 5),
                            static_cast<float>(trigger.getBottom() - 4));
                play.closeSubPath();
                graphics.setColour(slot.active ? juce::Colour(0xff15191e)
                                               : juce::Colour(0xfff3f5f7));
                graphics.fillPath(play);
                graphics.setColour(juce::Colour(0xfff5f7fa));
                graphics.setFont(juce::FontOptions(10.0f,
                                                    juce::Font::bold));
                graphics.drawFittedText(
                    slot.name,
                    bounds.withTrimmedLeft(trigger.getWidth() + 5)
                        .withTrimmedRight(42)
                        .reduced(3, 2),
                    juce::Justification::centredLeft, 2, 0.72f);
                if (slot.looping && !slot.queued && !slot.stopQueued) {
                    graphics.setColour(juce::Colour(0xffeaf0f7));
                    graphics.setFont(juce::FontOptions(11.0f,
                                                        juce::Font::bold));
                    graphics.drawText(juce::String::fromUTF8("↻"),
                                      bounds.getRight() - 18,
                                      bounds.getY(), 15,
                                      bounds.getHeight(),
                                      juce::Justification::centred);
                }
                if (slot.queued || slot.stopQueued) {
                    auto state = bounds.withTrimmedLeft(
                        std::max(0, bounds.getWidth() - 42)).reduced(2, 4);
                    graphics.setColour(juce::Colour(
                        slot.stopQueued ? 0xffff7077 : 0xffffbd62));
                    graphics.fillRoundedRectangle(state.toFloat(), 2.0f);
                    graphics.setColour(juce::Colour(0xff171a20));
                    graphics.setFont(juce::FontOptions(7.5f,
                                                        juce::Font::bold));
                    graphics.drawFittedText(
                        tr(slot.stopQueued ? "launcher.queuedStop"
                                           : "launcher.queuedPlay"),
                        state.reduced(2), juce::Justification::centred,
                        1, 0.65f);
                }
            } else {
                graphics.setColour(
                    slot.recording ? juce::Colour(0xffff5964)
                    : slot.recordPending || slot.recordStopPending
                        ? juce::Colour(0xffffb04a)
                        : triggerHovered ? juce::Colour(0xffff727c)
                                         : juce::Colour(0xff59616b));
                if (tracks_[trackIndex].armed) {
                    const auto indicator = trigger.toFloat().reduced(3.0f);
                    if (slot.recordPending)
                        graphics.drawEllipse(indicator, 2.0f);
                    else
                        graphics.fillEllipse(indicator);
                    if (slot.recording || slot.recordStopPending) {
                        graphics.setColour(juce::Colour(0xfff8f9fb));
                        graphics.fillRect(
                            indicator.withSizeKeepingCentre(5.0f, 5.0f));
                    } else {
                        graphics.setColour(juce::Colour(0xff20242a));
                        graphics.drawEllipse(indicator, 1.0f);
                    }
                } else {
                    const auto idle = trigger.withSizeKeepingCentre(8, 8);
                    graphics.fillRect(idle);
                }
                if (slot.recordPending || slot.recording
                    || slot.recordStopPending) {
                    graphics.setColour(juce::Colour(0xfff5f7fa));
                    graphics.setFont(juce::FontOptions(8.5f,
                                                        juce::Font::bold));
                    graphics.drawFittedText(
                        tr(slot.recordStopPending
                               ? "launcher.recordStopPending"
                               : slot.recording
                                     ? "launcher.recording"
                                     : "launcher.recordPending"),
                        bounds.withTrimmedLeft(trigger.getWidth() + 5)
                            .reduced(3, 2),
                        juce::Justification::centredLeft, 1, 0.72f);
                }
            }
            if (slot.active) {
                auto progress = bounds.reduced(2).removeFromBottom(3);
                progress.setWidth(juce::roundToInt(
                    static_cast<float>(progress.getWidth())
                    * slot.playbackProgress));
                graphics.setColour(juce::Colour(0xffffffff).withAlpha(0.86f));
                graphics.fillRoundedRectangle(progress.toFloat(), 1.5f);
                graphics.setColour(juce::Colour(0xffffffff));
                graphics.fillEllipse(static_cast<float>(bounds.getRight() - 12),
                                     static_cast<float>(bounds.getY() + 5), 6.0f, 6.0f);
            } else if (slot.queued) {
                graphics.setColour(juce::Colour(0xffffb04a));
                graphics.drawEllipse(static_cast<float>(bounds.getRight() - 13),
                                     static_cast<float>(bounds.getY() + 4), 8.0f, 8.0f,
                                     1.5f);
            }
            if (sampleDropTarget_.trackIndex == trackIndex
                && sampleDropTarget_.sceneIndex == sceneIndex) {
                const auto dropColour = launcherClipDropTarget_
                                            ? juce::Colour(0xffffb04a)
                                            : juce::Colour(0xff4fd3ae);
                graphics.setColour(dropColour.withAlpha(0.18f));
                graphics.fillRoundedRectangle(bounds.reduced(1).toFloat(),
                                               3.0f);
                graphics.setColour(dropColour.brighter(0.18f));
                graphics.drawRoundedRectangle(bounds.reduced(1).toFloat(),
                                               3.0f, 2.5f);
            }
        }
    }

    for (std::size_t trackIndex = 0; trackIndex < tracks_.size(); ++trackIndex) {
        const auto& track = tracks_[trackIndex];
        const auto devices = deviceBounds(trackIndex);
        const auto io = ioBounds(trackIndex);
        const auto trackStop = stopTrackBounds(trackIndex);
        const auto mixer = mixerBounds(trackIndex);
        if (clip.intersects(devices)) {
            graphics.setColour(juce::Colour(0xff20242a));
            graphics.fillRect(devices);
            graphics.setColour(juce::Colour(0xff363c45));
            graphics.drawRect(devices, 1);
            if (deviceDropTarget_ == trackIndex) {
                graphics.setColour(juce::Colour(0xff8b7cf6).withAlpha(0.16f));
                graphics.fillRect(devices.reduced(1));
                graphics.setColour(juce::Colour(0xffb2a7ff));
                graphics.drawRect(devices.reduced(1), 2);
            }

            auto deviceRows = devices.reduced(5, 4);
            const auto addBounds = addDeviceBounds(trackIndex);
            deviceRows.setBottom(addBounds.getY() - 2);
            const auto maximumRows = std::max(1, deviceRows.getHeight() / 18);
            for (int index = 0;
                 index < static_cast<int>(track.devices.size())
                     && index < maximumRows; ++index) {
                const auto row = deviceRows.removeFromTop(18);
                graphics.setColour(juce::Colour(0xff30353c));
                graphics.fillRoundedRectangle(row.toFloat(), 2.0f);
                graphics.setColour(juce::Colour(0xffffc857));
                graphics.fillEllipse(static_cast<float>(row.getX() + 4),
                                     static_cast<float>(row.getCentreY() - 3),
                                     6.0f, 6.0f);
                graphics.setColour(juce::Colour(0xffdce1e7));
                graphics.setFont(juce::FontOptions(8.5f, juce::Font::bold));
                graphics.drawFittedText(
                    track.devices[static_cast<std::size_t>(index)],
                    row.withTrimmedLeft(14).reduced(2, 0),
                    juce::Justification::centredLeft, 1, 0.7f);
            }
            graphics.setColour(hovered_.trackIndex == trackIndex
                                       && hovered_.kind == HitKind::addDevice
                                   ? juce::Colour(0xffffa229)
                                   : juce::Colour(0xff8f969f));
            graphics.setFont(juce::FontOptions(15.0f, juce::Font::bold));
            graphics.drawText("+", addBounds, juce::Justification::centred,
                              false);
            if (track.latencySamples > 0) {
                graphics.setColour(juce::Colour(0xff8f969f));
                graphics.setFont(juce::FontOptions(7.5f));
                graphics.drawText(juce::String(track.latencySamples) + " smp",
                                  devices.withTop(devices.getBottom() - 12),
                                  juce::Justification::centredRight, false);
            }
        }

        if (clip.intersects(io)) {
            graphics.setColour(juce::Colour(0xff292d33));
            graphics.fillRect(io);
            graphics.setColour(juce::Colour(0xffaab1ba));
            graphics.setFont(juce::FontOptions(8.2f));
            auto ioLine = io.reduced(5, 2);
            graphics.drawFittedText(track.midi ? tr("node.midiInput")
                                                : tr("launcher.allInputs"),
                                    ioLine.removeFromTop(ioLine.getHeight() / 2),
                                    juce::Justification::centredLeft, 1, 0.68f);
            graphics.drawFittedText(tr("launcher.masterOutput"), ioLine,
                                    juce::Justification::centredLeft, 1, 0.68f);
        }
        if (clip.intersects(trackStop)) {
            const auto hasPlayback = std::any_of(
                track.slots.begin(), track.slots.end(), [](const auto& slot) {
                    return slot.active || slot.queued || slot.stopQueued
                           || slot.recording || slot.recordPending;
                });
            graphics.setColour(juce::Colour(
                hovered_.trackIndex == trackIndex
                        && hovered_.kind == HitKind::stopTrack
                    ? 0xffff6c73
                    : hasPlayback ? 0xffa5444c : 0xff3a414b));
            graphics.fillRoundedRectangle(trackStop.toFloat(), 2.0f);
            graphics.setColour(juce::Colour(0xfff4f6f8));
            graphics.fillRect(trackStop.withSizeKeepingCentre(7, 7));
        }

        if (!clip.intersects(mixer))
            continue;
        graphics.setColour(juce::Colour(track.selected ? 0xff292d33 : 0xff22262b));
        graphics.fillRect(mixer);
        if (track.selected) {
            graphics.setColour(juce::Colour(0xffff8a00));
            graphics.drawRect(mixer, 1);
        }

        const auto pan = panBounds(trackIndex).toFloat();
        graphics.setColour(juce::Colour(0xff101318));
        graphics.fillEllipse(pan);
        graphics.setColour(juce::Colour(0xff6d747d));
        graphics.drawEllipse(pan, 1.2f);
        const auto angle = juce::jmap(track.pan, -1.0f, 1.0f,
                                     -2.35f, 2.35f);
        const auto centre = pan.getCentre();
        graphics.setColour(juce::Colour(0xfff0f2f4));
        graphics.drawLine(centre.x, centre.y,
                          centre.x + std::sin(angle) * pan.getWidth() * 0.36f,
                          centre.y - std::cos(angle) * pan.getHeight() * 0.36f,
                          1.4f);

        const auto gainValue = gainValueBounds(trackIndex);
        graphics.setColour(juce::Colour(0xff171b20));
        graphics.fillRoundedRectangle(gainValue.toFloat(), 2.0f);
        graphics.setColour(track.selected ? juce::Colour(0xffffa229)
                                          : juce::Colour(0xffd6dbe2));
        graphics.setFont(juce::FontOptions(8.2f, juce::Font::bold));
        graphics.drawText(juce::String(track.gainDb, 1) + " dB",
                          gainValue, juce::Justification::centred, false);
        drawFaderAndMeter(graphics, gainBounds(trackIndex),
                          meterBounds(trackIndex), track.gainDb,
                          track.peak, track.rms,
                          track.selected ? juce::Colour(0xffff8a00)
                                         : juce::Colour(0xff4f9ed8));

        const auto buttons = mixerButtonBounds(trackIndex);
        const std::array<juce::String, 3> labels { "M", "S", {} };
        const std::array<bool, 3> active { track.muted, track.solo, track.armed };
        for (std::size_t index = 0; index < buttons.size(); ++index) {
            graphics.setColour(active[index]
                                   ? (index == 2 ? juce::Colour(0xffff5964)
                                                 : juce::Colour(0xffffa229))
                                   : juce::Colour(0xff3a3f46));
            graphics.fillRoundedRectangle(buttons[index].toFloat(), 2.0f);
            graphics.setColour(active[index] ? juce::Colour(0xff151719)
                                              : juce::Colour(0xffe3e6ea));
            if (index == 2) {
                const auto indicator = buttons[index].toFloat()
                                           .withSizeKeepingCentre(8.0f, 8.0f);
                graphics.fillEllipse(indicator);
            } else {
                graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
                graphics.drawText(labels[index], buttons[index],
                                  juce::Justification::centred, false);
            }
        }
    }

    if (trackDragTarget_ && dragHit_.kind == HitKind::trackHeader
        && dragHit_.trackIndex && *trackDragTarget_ < tracks_.size()) {
        const auto target = trackBounds(*trackDragTarget_);
        graphics.setColour(juce::Colour(0xff58c9ff));
        graphics.fillRect(target.getX() - 2, 0, 4, getHeight());
        graphics.setColour(juce::Colour(0xffbcecff));
        graphics.drawRect(target.reduced(1), 2);
    }


    const auto master = masterBounds();
    if (master.getX() < getWidth() && clip.intersects(master)) {
        auto masterHeader = master.withHeight(headerHeight);
        graphics.setColour(juce::Colour(0xff1c2028));
        graphics.fillRoundedRectangle(masterHeader.reduced(1).toFloat(), 3.0f);
        graphics.setColour(juce::Colour(0xff72d66b));
        graphics.fillRect(masterHeader.getX() + 1, 1,
                          masterHeader.getWidth() - 2, 3);
        graphics.setColour(juce::Colour(0xffd8dde4));
        graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        graphics.drawFittedText(tr("track.master"), masterHeader.reduced(6),
                                juce::Justification::centredLeft, 1, 0.75f);

        for (int sceneIndex = 0; sceneIndex < sceneCount; ++sceneIndex) {
            const auto row = juce::Rectangle<int>(
                master.getX(), headerHeight + sceneIndex * sceneRowHeight(),
                master.getWidth() - gap, sceneRowHeight() - gap);
            graphics.setColour(juce::Colour(0xff171b21));
            graphics.fillRoundedRectangle(row.reduced(1).toFloat(), 3.0f);
            graphics.setColour(juce::Colour(0xff333a44));
            graphics.drawRoundedRectangle(row.reduced(1).toFloat(), 3.0f, 1.0f);
        }

        const auto devices = deviceBounds(tracks_.size());
        graphics.setColour(juce::Colour(0xff20242a));
        graphics.fillRect(devices);
        graphics.setColour(juce::Colour(0xff363c45));
        graphics.drawRect(devices, 1);
        graphics.setColour(juce::Colour(0xff72d66b));
        graphics.fillEllipse(static_cast<float>(devices.getX() + 8),
                             static_cast<float>(devices.getY() + 10),
                             7.0f, 7.0f);
        graphics.setColour(juce::Colour(0xffdce1e7));
        graphics.setFont(juce::FontOptions(8.5f, juce::Font::bold));
        graphics.drawFittedText(tr("track.masterBus"),
                                devices.reduced(7).withTrimmedLeft(12),
                                juce::Justification::centredTop, 2, 0.70f);

        const auto io = ioBounds(tracks_.size());
        graphics.setColour(juce::Colour(0xff292d33));
        graphics.fillRect(io);
        graphics.setColour(juce::Colour(0xffaab1ba));
        graphics.setFont(juce::FontOptions(8.2f));
        graphics.drawFittedText(tr("launcher.masterOutput"), io.reduced(5),
                                juce::Justification::centredLeft, 1, 0.7f);

        const auto mixer = mixerBounds(tracks_.size());
        graphics.setColour(juce::Colour(0xff22262b));
        graphics.fillRect(mixer);
        const auto masterIndex = tracks_.size();
        const auto gainValue = gainValueBounds(masterIndex);
        graphics.setColour(juce::Colour(0xff171b20));
        graphics.fillRoundedRectangle(gainValue.toFloat(), 2.0f);
        graphics.setColour(juce::Colour(0xff9ce493));
        graphics.setFont(juce::FontOptions(8.2f, juce::Font::bold));
        graphics.drawText(juce::String(masterGainDb_, 1) + " dB",
                          gainValue, juce::Justification::centred, false);
        drawFaderAndMeter(graphics, gainBounds(masterIndex),
                          meterBounds(masterIndex), masterGainDb_,
                          masterPeak_, masterRms_, juce::Colour(0xff72d66b));
        graphics.setColour(juce::Colour(0xff72d66b));
        graphics.setFont(juce::FontOptions(8.5f, juce::Font::bold));
        graphics.drawText(tr("track.master"),
                          mixer.reduced(6).removeFromBottom(20),
                          juce::Justification::centred, false);
    }

    graphics.setColour(juce::Colour(0xff303744));
    graphics.drawRect(getLocalBounds(), 1);
}

void ClipLauncherComponent::resized()
{
    quantizationSelector_.setBounds(5, 17, sceneHeaderWidth - 10, 21);
    if (editedGainTrack_)
        gainValueEditor_.setBounds(gainValueBounds(*editedGainTrack_));
}

void ClipLauncherComponent::mouseDown(const juce::MouseEvent& event)
{
    const auto hit = hitAt(event.getPosition());
    dragHit_ = {};
    if (hit.kind == HitKind::none)
        return;

    if (hit.kind == HitKind::scene) {
        if (event.mods.isLeftButtonDown() && launchSceneHandler_)
            launchSceneHandler_(hit.sceneIndex);
        return;
    }

    if (hit.kind == HitKind::addTrack) {
        if (event.mods.isLeftButtonDown() && addTrackHandler_)
            addTrackHandler_();
        return;
    }

    if (hit.kind == HitKind::masterGain) {
        if (hit.trackIndex
            && gainValueBounds(*hit.trackIndex).contains(event.getPosition())) {
            beginGainValueEdit(*hit.trackIndex);
            return;
        }
        dragHit_ = hit;
        updateMixerDrag(event);
        return;
    }

    if (!hit.trackIndex)
        return;
    const auto trackIndex = *hit.trackIndex;
    if (trackIndex >= tracks_.size())
        return;

    if (hit.kind == HitKind::trackHeader && event.mods.isPopupMenu()) {
        if (trackContextHandler_)
            trackContextHandler_(trackIndex, this);
        return;
    }

    if (trackSelectHandler_)
        trackSelectHandler_(trackIndex);

    switch (hit.kind) {
    case HitKind::trackHeader:
        if (event.mods.isLeftButtonDown()) {
            dragHit_ = hit;
            trackDragTarget_ = trackIndex;
        }
        return;
    case HitKind::addDevice:
        if (event.mods.isLeftButtonDown() && deviceAddHandler_)
            deviceAddHandler_(trackIndex);
        return;
    case HitKind::mute:
        tracks_[trackIndex].muted = !tracks_[trackIndex].muted;
        if (muteHandler_)
            muteHandler_(trackIndex, tracks_[trackIndex].muted);
        repaint(mixerBounds(trackIndex));
        return;
    case HitKind::solo:
        tracks_[trackIndex].solo = !tracks_[trackIndex].solo;
        if (soloHandler_)
            soloHandler_(trackIndex, tracks_[trackIndex].solo);
        repaint(mixerBounds(trackIndex));
        return;
    case HitKind::arm:
        tracks_[trackIndex].armed = !tracks_[trackIndex].armed;
        if (armHandler_)
            armHandler_(trackIndex, tracks_[trackIndex].armed);
        repaint(mixerBounds(trackIndex));
        return;
    case HitKind::stopTrack:
        if (event.mods.isLeftButtonDown() && trackStopHandler_)
            trackStopHandler_(trackIndex);
        return;
    case HitKind::gain:
        if (gainValueBounds(trackIndex).contains(event.getPosition())) {
            beginGainValueEdit(trackIndex);
            return;
        }
        dragHit_ = hit;
        updateMixerDrag(event);
        return;
    case HitKind::pan:
        dragHit_ = hit;
        updateMixerDrag(event);
        return;
    default:
        break;
    }

    if ((hit.kind != HitKind::slot && hit.kind != HitKind::slotPlay
         && hit.kind != HitKind::slotRecord)
        || hit.sceneIndex < 0)
        return;
    const auto slotWasOccupied = tracks_[trackIndex]
                                     .slots[static_cast<std::size_t>(
                                         hit.sceneIndex)].occupied;
    if (slotWasOccupied && slotSelectHandler_)
        slotSelectHandler_(trackIndex, hit.sceneIndex);
    if (trackIndex >= tracks_.size())
        return;
    const auto& slot = tracks_[trackIndex].slots[static_cast<std::size_t>(hit.sceneIndex)];
    if (event.mods.isPopupMenu()) {
        juce::PopupMenu menu;
        menu.addItem(1, tr("launcher.browseSlot"));
        if (slot.occupied) {
            menu.addItem(2, tr("launcher.openEditor"));
            menu.addItem(3, tr("launcher.loopClip"), true,
                         slot.looping);
            menu.addSeparator();
            menu.addItem(4, tr("launcher.clearSlot"));
        }
        const auto safe = juce::Component::SafePointer<
            ClipLauncherComponent>(this);
        const auto popupPosition = event.getScreenPosition();
        menu.showMenuAsync(
            juce::PopupMenu::Options().withTargetScreenArea(
                { popupPosition.x, popupPosition.y, 1, 1 }),
            [safe, trackIndex, sceneIndex = hit.sceneIndex,
             wasLooping = slot.looping](const int result) {
                if (safe == nullptr)
                    return;
                if (result == 1 && safe->slotBrowseHandler_)
                    safe->slotBrowseHandler_(trackIndex, sceneIndex);
                else if (result == 2 && safe->slotOpenHandler_)
                    safe->slotOpenHandler_(trackIndex, sceneIndex);
                else if (result == 3 && safe->slotLoopHandler_)
                    safe->slotLoopHandler_(trackIndex, sceneIndex,
                                           !wasLooping);
                else if (result == 4 && safe->clearHandler_)
                    safe->clearHandler_(trackIndex, sceneIndex);
            });
    } else if (slot.occupied && hit.kind == HitKind::slotPlay) {
        if (launchHandler_)
            launchHandler_(trackIndex, hit.sceneIndex);
    } else if (!slot.occupied && tracks_[trackIndex].armed
               && hit.kind == HitKind::slotRecord && captureHandler_) {
        captureHandler_(trackIndex, hit.sceneIndex);
    } else if (slot.occupied && hit.kind == HitKind::slot
               && event.mods.isLeftButtonDown()) {
        dragHit_ = hit;
    } else if (!slot.occupied && !tracks_[trackIndex].armed
               && event.mods.isLeftButtonDown() && trackStopHandler_) {
        trackStopHandler_(trackIndex);
    }
}

void ClipLauncherComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (dragHit_.kind == HitKind::trackHeader && dragHit_.trackIndex
        && event.getDistanceFromDragStart() >= 5) {
        const auto source = *dragHit_.trackIndex;
        if (source < tracks_.size()) {
            const auto width = std::max(1, trackColumnWidth());
            auto target = static_cast<std::size_t>(juce::jlimit(
                0, static_cast<int>(tracks_.size()) - 1,
                (event.getPosition().x - sceneHeaderWidth + width / 2)
                    / width));
            if (tracks_[target].midi != tracks_[source].midi)
                target = source;
            if (trackDragTarget_ != target) {
                trackDragTarget_ = target;
                repaint();
            }
        }
        return;
    }
    if (dragHit_.kind == HitKind::slot && dragHit_.trackIndex
        && dragHit_.sceneIndex >= 0 && event.getDistanceFromDragStart() >= 5
        && !isDragAndDropActive()) {
        const auto trackIndex = *dragHit_.trackIndex;
        const auto sceneIndex = dragHit_.sceneIndex;
        if (trackIndex < tracks_.size()
            && tracks_[trackIndex]
                   .slots[static_cast<std::size_t>(sceneIndex)].occupied) {
            const auto preview = createComponentSnapshot(
                slotBounds(trackIndex, sceneIndex));
            startDragging(
                makeLauncherClipDragPayload(trackIndex, sceneIndex), this,
                juce::ScaledImage(preview), false, nullptr, &event.source);
        }
        dragHit_ = {};
        return;
    }
    updateMixerDrag(event);
}

void ClipLauncherComponent::mouseUp(const juce::MouseEvent&)
{
    const auto source = dragHit_.trackIndex;
    const auto target = trackDragTarget_;
    dragHit_ = {};
    trackDragTarget_.reset();
    if (source && target && *source != *target && trackMoveHandler_)
        trackMoveHandler_(*source, *target);
    repaint();
}

void ClipLauncherComponent::mouseDoubleClick(const juce::MouseEvent& event)
{
    const auto hit = hitAt(event.getPosition());
    if ((hit.kind == HitKind::slot || hit.kind == HitKind::slotPlay
         || hit.kind == HitKind::slotRecord)
        && hit.trackIndex && hit.sceneIndex >= 0
        && *hit.trackIndex < tracks_.size()) {
        const auto occupied = tracks_[*hit.trackIndex]
                                  .slots[static_cast<std::size_t>(
                                      hit.sceneIndex)].occupied;
        if (occupied && slotOpenHandler_)
            slotOpenHandler_(*hit.trackIndex, hit.sceneIndex);
        else if (!occupied && emptySlotCreateHandler_)
            emptySlotCreateHandler_(*hit.trackIndex, hit.sceneIndex);
        return;
    }
    if (hit.kind == HitKind::pan && hit.trackIndex
        && *hit.trackIndex < tracks_.size()) {
        auto& track = tracks_[*hit.trackIndex];
        track.pan = 0.0f;
        if (mixerHandler_)
            mixerHandler_(*hit.trackIndex, track.gainDb, track.pan);
        repaint(mixerBounds(*hit.trackIndex));
        return;
    }
    if ((hit.kind == HitKind::gain || hit.kind == HitKind::masterGain)
        && hit.trackIndex)
        beginGainValueEdit(*hit.trackIndex);
}

void ClipLauncherComponent::mouseMove(const juce::MouseEvent& event)
{
    hovered_ = hitAt(event.getPosition());
    switch (hovered_.kind) {
        case HitKind::scene:
            setTooltip(tr("launcher.scenePlayHelp"));
            break;
        case HitKind::slotPlay:
            setTooltip(tr("launcher.clipPlayHelp"));
            break;
        case HitKind::slotRecord:
            setTooltip(tr("launcher.slotRecordHelp"));
            break;
        case HitKind::slot:
            setTooltip(hovered_.trackIndex
                               && *hovered_.trackIndex < tracks_.size()
                               && !tracks_[*hovered_.trackIndex]
                                       .slots[static_cast<std::size_t>(
                                           std::max(0, hovered_.sceneIndex))]
                                       .occupied
                           ? tr("launcher.emptyStopHelp")
                           : tr("launcher.slotHelp"));
            break;
        case HitKind::addTrack:
            setTooltip(tr("track.addHelp"));
            break;
        case HitKind::addDevice:
            setTooltip(tr("launcher.addDeviceHelp"));
            break;
        case HitKind::stopTrack:
            setTooltip(tr("launcher.stopTrackHelp"));
            break;
        case HitKind::trackHeader:
            setTooltip(tr("launcher.moveTrackHelp"));
            break;
        case HitKind::gain:
        case HitKind::masterGain:
            setTooltip(tr("launcher.gainValueHelp"));
            break;
        default:
            setTooltip(tr("launcher.help"));
            break;
    }
    repaint();
}

void ClipLauncherComponent::mouseExit(const juce::MouseEvent&)
{
    hovered_ = {};
    repaint();
}

bool ClipLauncherComponent::isInterestedInDragSource(
    const juce::DragAndDropTarget::SourceDetails& details)
{
    return sampleFileFromDragPayload(details.description).has_value()
           || launcherClipFromDragPayload(details.description).has_value()
           || factoryDeviceFromDragPayload(details.description).has_value()
           || pluginFileFromDragPayload(details.description).has_value();
}

void ClipLauncherComponent::itemDragEnter(
    const juce::DragAndDropTarget::SourceDetails& details)
{
    const auto isDevice =
        factoryDeviceFromDragPayload(details.description).has_value()
        || pluginFileFromDragPayload(details.description).has_value();
    if (isDevice)
        updateDeviceDropTarget(details.localPosition);
    else {
        launcherClipDropTarget_ =
            launcherClipFromDragPayload(details.description).has_value();
        updateSampleDropTarget(details.localPosition);
    }
}

void ClipLauncherComponent::itemDragMove(
    const juce::DragAndDropTarget::SourceDetails& details)
{
    const auto isDevice =
        factoryDeviceFromDragPayload(details.description).has_value()
        || pluginFileFromDragPayload(details.description).has_value();
    if (isDevice)
        updateDeviceDropTarget(details.localPosition);
    else {
        launcherClipDropTarget_ =
            launcherClipFromDragPayload(details.description).has_value();
        updateSampleDropTarget(details.localPosition);
    }
}

void ClipLauncherComponent::itemDragExit(
    const juce::DragAndDropTarget::SourceDetails&)
{
    clearSampleDropTarget();
}

void ClipLauncherComponent::itemDropped(
    const juce::DragAndDropTarget::SourceDetails& details)
{
    const auto isDevice =
        factoryDeviceFromDragPayload(details.description).has_value()
        || pluginFileFromDragPayload(details.description).has_value();
    if (isDevice) {
        updateDeviceDropTarget(details.localPosition);
        if (deviceDropTarget_ && deviceDropHandler_)
            deviceDropHandler_(*deviceDropTarget_, details.description);
    } else {
        updateSampleDropTarget(details.localPosition);
    }
    if (!isDevice) {
        if (const auto file = sampleFileFromDragPayload(details.description))
            dropSample(*file);
        else
            dropLauncherClip(details.description);
    }
    clearSampleDropTarget();
}

bool ClipLauncherComponent::isInterestedInFileDrag(
    const juce::StringArray& files)
{
    return files.size() == 1
           && (isSupportedAudioSample(juce::File(files[0]))
               || isSupportedPluginBundle(juce::File(files[0])));
}

void ClipLauncherComponent::fileDragEnter(
    const juce::StringArray& files, const int x, const int y)
{
    if (files.size() == 1
        && isSupportedPluginBundle(juce::File(files[0])))
        updateDeviceDropTarget({ x, y });
    else
        updateSampleDropTarget({ x, y });
}

void ClipLauncherComponent::fileDragMove(
    const juce::StringArray& files, const int x, const int y)
{
    fileDragEnter(files, x, y);
}

void ClipLauncherComponent::fileDragExit(const juce::StringArray&)
{
    clearSampleDropTarget();
}

void ClipLauncherComponent::filesDropped(
    const juce::StringArray& files, const int x, const int y)
{
    if (files.size() == 1) {
        const auto file = juce::File(files[0]);
        if (isSupportedPluginBundle(file)) {
            updateDeviceDropTarget({ x, y });
            if (deviceDropTarget_ && deviceDropHandler_)
                deviceDropHandler_(*deviceDropTarget_,
                                   makePluginDragPayload(file));
        } else {
            updateSampleDropTarget({ x, y });
            dropSample(file);
        }
    }
    clearSampleDropTarget();
}

void ClipLauncherComponent::updateSampleDropTarget(
    const juce::Point<int> position)
{
    const auto hadDeviceTarget = deviceDropTarget_.has_value();
    deviceDropTarget_.reset();
    auto target = hitAt(position);
    const auto isSlot = target.trackIndex.has_value()
                        && *target.trackIndex < tracks_.size()
                        && target.sceneIndex >= 0
                        && (target.kind == HitKind::slot
                            || target.kind == HitKind::slotPlay
                            || target.kind == HitKind::slotRecord);
    if (!isSlot)
        target = {};
    if (!hadDeviceTarget
        && sampleDropTarget_.trackIndex == target.trackIndex
        && sampleDropTarget_.sceneIndex == target.sceneIndex
        && sampleDropTarget_.kind == target.kind)
        return;
    sampleDropTarget_ = target;
    repaint();
}

void ClipLauncherComponent::updateDeviceDropTarget(
    const juce::Point<int> position)
{
    std::optional<std::size_t> target;
    for (std::size_t trackIndex = 0; trackIndex < tracks_.size();
         ++trackIndex)
        if (deviceBounds(trackIndex).contains(position)) {
            target = trackIndex;
            break;
        }
    const auto changed = deviceDropTarget_ != target
                         || sampleDropTarget_.trackIndex.has_value()
                         || launcherClipDropTarget_;
    deviceDropTarget_ = target;
    sampleDropTarget_ = {};
    launcherClipDropTarget_ = false;
    if (changed)
        repaint();
}

void ClipLauncherComponent::clearSampleDropTarget()
{
    if (!sampleDropTarget_.trackIndex && !launcherClipDropTarget_
        && !deviceDropTarget_)
        return;
    sampleDropTarget_ = {};
    launcherClipDropTarget_ = false;
    deviceDropTarget_.reset();
    repaint();
}

void ClipLauncherComponent::dropSample(const juce::File& file)
{
    if (!sampleDropTarget_.trackIndex || sampleDropTarget_.sceneIndex < 0
        || !isSupportedAudioSample(file) || !sampleDropHandler_)
        return;
    sampleDropHandler_(*sampleDropTarget_.trackIndex,
                       sampleDropTarget_.sceneIndex, file);
}

void ClipLauncherComponent::dropLauncherClip(const juce::var& payload)
{
    const auto source = launcherClipFromDragPayload(payload);
    if (!source || !sampleDropTarget_.trackIndex
        || sampleDropTarget_.sceneIndex < 0 || !clipTransferHandler_)
        return;
    clipTransferHandler_(
        source->trackIndex, source->sceneIndex,
        *sampleDropTarget_.trackIndex, sampleDropTarget_.sceneIndex,
        juce::ModifierKeys::getCurrentModifiersRealtime().isCommandDown());
}

ClipLauncherComponent::Hit ClipLauncherComponent::hitAt(const juce::Point<int> position) const
{
    if (position.y < 0 || position.y >= getHeight())
        return {};

    if (addTrackBounds().contains(position))
        return { {}, -1, HitKind::addTrack };

    if (position.y >= headerHeight && position.y < clipAreaBottom()) {
        const auto sceneIndex = juce::jlimit(
            0, sceneCount - 1,
            (position.y - headerHeight) / sceneRowHeight());
        if (position.x < sceneHeaderWidth)
            return { {}, sceneIndex, HitKind::scene };
        for (std::size_t trackIndex = 0; trackIndex < tracks_.size(); ++trackIndex) {
            if (!slotBounds(trackIndex, sceneIndex).contains(position))
                continue;
            if (slotTriggerBounds(trackIndex, sceneIndex).contains(position)) {
                const auto& slot = tracks_[trackIndex]
                                       .slots[static_cast<std::size_t>(
                                           sceneIndex)];
                if (slot.occupied)
                    return { trackIndex, sceneIndex, HitKind::slotPlay };
                if (tracks_[trackIndex].armed)
                    return { trackIndex, sceneIndex, HitKind::slotRecord };
            }
            return { trackIndex, sceneIndex, HitKind::slot };
        }
        return {};
    }

    const auto masterIndex = tracks_.size();
    if (masterBounds().contains(position)
        && (gainBounds(masterIndex).expanded(24, 3).contains(position)
            || gainValueBounds(masterIndex).contains(position)))
        return { masterIndex, -1, HitKind::masterGain };

    for (std::size_t trackIndex = 0; trackIndex < tracks_.size(); ++trackIndex) {
        if (!trackBounds(trackIndex).contains(position))
            continue;
        if (position.y < headerHeight)
            return { trackIndex, -1, HitKind::trackHeader };
        if (addDeviceBounds(trackIndex).contains(position))
            return { trackIndex, -1, HitKind::addDevice };
        if (stopTrackBounds(trackIndex).contains(position))
            return { trackIndex, -1, HitKind::stopTrack };
        if (gainBounds(trackIndex).expanded(24, 3).contains(position)
            || gainValueBounds(trackIndex).contains(position))
            return { trackIndex, -1, HitKind::gain };
        if (panBounds(trackIndex).expanded(3).contains(position))
            return { trackIndex, -1, HitKind::pan };
        const auto buttons = mixerButtonBounds(trackIndex);
        if (buttons[0].contains(position))
            return { trackIndex, -1, HitKind::mute };
        if (buttons[1].contains(position))
            return { trackIndex, -1, HitKind::solo };
        if (buttons[2].contains(position))
            return { trackIndex, -1, HitKind::arm };
        return { trackIndex, -1, HitKind::trackHeader };
    }
    return {};
}

juce::Rectangle<int> ClipLauncherComponent::sceneBounds(const int sceneIndex) const
{
    const auto rowHeight = sceneRowHeight();
    return { 0, headerHeight + sceneIndex * rowHeight,
             sceneHeaderWidth - gap, rowHeight - gap };
}

juce::Rectangle<int> ClipLauncherComponent::slotBounds(const std::size_t trackIndex,
                                                       const int sceneIndex) const
{
    const auto column = trackBounds(trackIndex);
    const auto rowHeight = sceneRowHeight();
    return { column.getX(),
             headerHeight + sceneIndex * rowHeight,
             column.getWidth() - gap, rowHeight - gap };
}

juce::Rectangle<int> ClipLauncherComponent::slotTriggerBounds(
    const std::size_t trackIndex, const int sceneIndex) const
{
    auto bounds = slotBounds(trackIndex, sceneIndex).reduced(3);
    const auto side = juce::jlimit(12, 18, bounds.getHeight());
    return bounds.removeFromLeft(side).withSizeKeepingCentre(side, side);
}

juce::Rectangle<int> ClipLauncherComponent::trackBounds(
    const std::size_t trackIndex) const
{
    if (trackIndex == tracks_.size())
        return masterBounds();
    const auto columnWidth = trackColumnWidth();
    const auto x = sceneHeaderWidth + static_cast<int>(trackIndex) * columnWidth;
    return { x, 0, columnWidth, getHeight() };
}

juce::Rectangle<int> ClipLauncherComponent::masterBounds() const
{
    const auto width = trackColumnWidth();
    return { std::max(sceneHeaderWidth, getWidth() - width), 0,
             width, getHeight() };
}

juce::Rectangle<int> ClipLauncherComponent::addTrackBounds() const
{
    const auto width = trackColumnWidth();
    const auto x = sceneHeaderWidth
                   + static_cast<int>(tracks_.size()) * width;
    const auto available = masterBounds().getX() - x;
    if (available < 24)
        return {};
    return { x + 2, 3, std::min(width - 4, available - 4),
             headerHeight - 6 };
}

juce::Rectangle<int> ClipLauncherComponent::deviceBounds(
    const std::size_t trackIndex) const
{
    auto column = trackBounds(trackIndex);
    const auto top = clipAreaBottom();
    const auto remaining = std::max(1, getHeight() - top);
    const auto height = juce::jlimit(52, 100,
                                     juce::roundToInt(remaining * 0.28f));
    return { column.getX(), top, column.getWidth() - gap,
             std::min(height, remaining) };
}

juce::Rectangle<int> ClipLauncherComponent::addDeviceBounds(
    const std::size_t trackIndex) const
{
    auto bounds = deviceBounds(trackIndex).reduced(3, 2);
    return bounds.removeFromBottom(std::min(22, bounds.getHeight()));
}

juce::Rectangle<int> ClipLauncherComponent::ioBounds(
    const std::size_t trackIndex) const
{
    const auto devices = deviceBounds(trackIndex);
    const auto column = trackBounds(trackIndex);
    const auto available = std::max(0, getHeight() - devices.getBottom());
    return { column.getX(), devices.getBottom(), column.getWidth() - gap,
             std::min(30, available) };
}

juce::Rectangle<int> ClipLauncherComponent::stopTrackBounds(
    const std::size_t trackIndex) const
{
    auto bounds = ioBounds(trackIndex).reduced(4, 4);
    const auto side = std::min(22, bounds.getHeight());
    return bounds.removeFromRight(side).withSizeKeepingCentre(side, side);
}

juce::Rectangle<int> ClipLauncherComponent::mixerBounds(
    const std::size_t trackIndex) const
{
    const auto io = ioBounds(trackIndex);
    const auto column = trackBounds(trackIndex);
    return { column.getX(), io.getBottom(), column.getWidth() - gap,
             std::max(0, getHeight() - io.getBottom()) };
}

juce::Rectangle<int> ClipLauncherComponent::panBounds(
    const std::size_t trackIndex) const
{
    const auto mixer = mixerBounds(trackIndex);
    const auto diameter = juce::jlimit(22, 28,
        std::min(mixer.getWidth() / 3, mixer.getHeight() / 5));
    return { mixer.getX() + 6, mixer.getY() + 5,
             diameter, diameter };
}

juce::Rectangle<int> ClipLauncherComponent::gainBounds(
    const std::size_t trackIndex) const
{
    const auto mixer = mixerBounds(trackIndex);
    const auto top = mixer.getY() + 36;
    const auto bottom = std::max(top + 1, mixer.getBottom() - 34);
    return { mixer.getX() + 31, top, 11, bottom - top };
}

juce::Rectangle<int> ClipLauncherComponent::gainValueBounds(
    const std::size_t trackIndex) const
{
    const auto mixer = mixerBounds(trackIndex);
    const auto isMaster = trackIndex == tracks_.size();
    return isMaster
               ? juce::Rectangle<int>(mixer.getX() + 6, mixer.getY() + 6,
                                      mixer.getWidth() - 12, 21)
               : juce::Rectangle<int>(panBounds(trackIndex).getRight() + 5,
                                      mixer.getY() + 6,
                                      std::max(24, mixer.getRight()
                                                       - panBounds(trackIndex).getRight()
                                                       - 11),
                                      21);
}

juce::Rectangle<int> ClipLauncherComponent::meterBounds(
    const std::size_t trackIndex) const
{
    const auto mixer = mixerBounds(trackIndex);
    const auto fader = gainBounds(trackIndex);
    return { fader.getRight() + 16, fader.getY(),
             std::max(12, mixer.getRight() - fader.getRight() - 21),
             fader.getHeight() };
}

std::array<juce::Rectangle<int>, 3> ClipLauncherComponent::mixerButtonBounds(
    const std::size_t trackIndex) const
{
    auto row = mixerBounds(trackIndex).reduced(5, 4).removeFromBottom(25);
    const auto buttonWidth = std::max(1, (row.getWidth() - 4) / 3);
    std::array<juce::Rectangle<int>, 3> result;
    for (auto& bounds : result) {
        bounds = row.removeFromLeft(buttonWidth);
        row.removeFromLeft(std::min(2, row.getWidth()));
    }
    return result;
}

int ClipLauncherComponent::sceneRowHeight() const
{
    const auto remaining = std::max(1, getHeight() - headerHeight);
    const auto reserve = juce::jlimit(220, 370,
                                      juce::roundToInt(remaining * 0.56f));
    return juce::jlimit(17, 30,
                        std::max(1, (remaining - reserve) / sceneCount));
}

int ClipLauncherComponent::clipAreaBottom() const
{
    return headerHeight + sceneCount * sceneRowHeight();
}

int ClipLauncherComponent::trackColumnWidth() const
{
    const auto availableWidth = std::max(1, getWidth() - sceneHeaderWidth);
    const auto visibleColumns = std::max(8,
        static_cast<int>(tracks_.size()) + 1);
    return juce::jlimit(88, 140,
                        std::max(1, availableWidth / visibleColumns));
}

void ClipLauncherComponent::updateMixerDrag(const juce::MouseEvent& event)
{
    if (!dragHit_.trackIndex)
        return;
    const auto trackIndex = *dragHit_.trackIndex;

    if (dragHit_.kind == HitKind::masterGain) {
        if (trackIndex != tracks_.size())
            return;
        const auto fader = gainBounds(trackIndex);
        const auto amount = juce::jlimit(
            0.0f, 1.0f,
            static_cast<float>(fader.getBottom() - event.getPosition().y)
                / static_cast<float>(std::max(1, fader.getHeight())));
        masterGainDb_ = dbForFaderAmount(amount);
        if (masterMixerHandler_)
            masterMixerHandler_(masterGainDb_);
        repaint(masterBounds());
        return;
    }

    if (trackIndex >= tracks_.size())
        return;

    auto& track = tracks_[trackIndex];
    if (dragHit_.kind == HitKind::gain) {
        const auto fader = gainBounds(trackIndex);
        const auto amount = juce::jlimit(
            0.0f, 1.0f,
            static_cast<float>(fader.getBottom() - event.getPosition().y)
                / static_cast<float>(std::max(1, fader.getHeight())));
        track.gainDb = dbForFaderAmount(amount);
    } else if (dragHit_.kind == HitKind::pan) {
        const auto column = trackBounds(trackIndex).reduced(6, 0);
        const auto amount = juce::jlimit(
            0.0f, 1.0f,
            static_cast<float>(event.getPosition().x - column.getX())
                / static_cast<float>(std::max(1, column.getWidth())));
        track.pan = amount * 2.0f - 1.0f;
    } else {
        return;
    }

    if (mixerHandler_)
        mixerHandler_(trackIndex, track.gainDb, track.pan);
    repaint(mixerBounds(trackIndex));
}

void ClipLauncherComponent::beginGainValueEdit(const std::size_t trackIndex)
{
    if (trackIndex > tracks_.size())
        return;

    editedGainTrack_ = trackIndex;
    const auto gainDb = trackIndex == tracks_.size()
                            ? masterGainDb_
                            : tracks_[trackIndex].gainDb;
    gainValueEditor_.setBounds(gainValueBounds(trackIndex));
    gainValueEditor_.setText(juce::String(gainDb, 1), false);
    gainValueEditor_.setVisible(true);
    gainValueEditor_.toFront(false);
    gainValueEditor_.grabKeyboardFocus();
    gainValueEditor_.selectAll();
}

void ClipLauncherComponent::finishGainValueEdit(const bool applyValue)
{
    if (!editedGainTrack_)
        return;

    const auto trackIndex = *editedGainTrack_;
    editedGainTrack_.reset();

    auto text = gainValueEditor_.getText().trim().replace(",", ".");
    const auto hasNumericValue = text.isNotEmpty() && text != "-" && text != "+"
                                 && text != ".";
    if (applyValue && hasNumericValue) {
        const auto gainDb = juce::jlimit(
            -60.0f, 12.0f, static_cast<float>(text.getDoubleValue()));
        if (trackIndex == tracks_.size()) {
            masterGainDb_ = gainDb;
            if (masterMixerHandler_)
                masterMixerHandler_(gainDb);
            repaint(masterBounds());
        } else if (trackIndex < tracks_.size()) {
            auto& track = tracks_[trackIndex];
            track.gainDb = gainDb;
            if (mixerHandler_)
                mixerHandler_(trackIndex, track.gainDb, track.pan);
            repaint(mixerBounds(trackIndex));
        }
    }

    gainValueEditor_.setVisible(false);
    grabKeyboardFocus();
}

} // namespace cydaw::app
