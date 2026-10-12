#include "WaveformComponent.h"
#include "PaintProfile.h"
#include "Localization.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace cydaw::app {
namespace {
// Smaller buckets keep the source-time shape stable at high zoom and while a
// short event is trimmed. The previous 512-sample buckets visibly jumped when
// the destination rectangle changed width.
constexpr int thumbnailResolutionSamples = 64;
constexpr int detailThumbnailResolutionSamples = 4;
// Below roughly four milliseconds of source audio per screen pixel the
// overview cache becomes visibly stepped. At that point a separate detail
// cache is populated asynchronously and substituted as soon as the visible
// source range is available.
constexpr double detailThresholdSecondsPerPixel = 0.004;
}

WaveformComponent::WaveformComponent(juce::AudioFormatManager& formatManager, juce::AudioThumbnailCache& cache)
    : formatManager_(formatManager), thumbnailCache_(cache),
      thumbnail_(thumbnailResolutionSamples, formatManager, cache)
{
    thumbnail_.addChangeListener(this);
    // With the lane cached, a playhead move re-renders one strip of it and
    // blits the rest.
    setBufferedToImage(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus(true);
}

WaveformComponent::~WaveformComponent()
{
    clearRegionThumbnails();
    thumbnail_.removeChangeListener(this);
}

void WaveformComponent::setFile(const juce::File& file)
{
    playheadSeconds_ = 0.0;
    regions_.clear();
    selectedRegionIds_.clear();
    selectedRegionId_ = 0;
    clearRegionThumbnails();
    thumbnail_.setSource(new juce::FileInputSource(file));
    repaint();
}

void WaveformComponent::clear()
{
    thumbnail_.clear();
    playheadSeconds_ = 0.0;
    regions_.clear();
    selectedRegionIds_.clear();
    selectedRegionId_ = 0;
    clearRegionThumbnails();
    repaint();
}

void WaveformComponent::setPlayhead(const double seconds)
{
    if (std::abs(playheadSeconds_ - seconds) < 1.0e-9)
        return;
    if (playheadDrawnByHost_) {
        playheadSeconds_ = seconds;
        return;
    }
    const auto previous = playheadStrip(playheadSeconds_);
    playheadSeconds_ = seconds;
    const auto moved = playheadStrip(playheadSeconds_);
    if (previous.isEmpty())
        repaint(moved);
    else if (moved.isEmpty())
        repaint(previous);
    else
        repaint(previous.getUnion(moved));
}

void WaveformComponent::setPlayheadDrawnByHost(const bool drawnByHost)
{
    playheadDrawnByHost_ = drawnByHost;
    repaint();
}

int WaveformComponent::playheadXForTime(const double seconds) const noexcept
{
    return playheadStrip(seconds).isEmpty()
               ? -1
               : juce::roundToInt(xFromTime(seconds));
}

juce::Rectangle<int> WaveformComponent::playheadStrip(const double seconds) const
{
    if (seconds < visibleStartSeconds_ || seconds > visibleEndSeconds_
        || getWidth() <= 0 || getHeight() <= 0)
        return {};
    return juce::Rectangle<int>(juce::roundToInt(xFromTime(seconds)) - 3, 0, 8,
                                getHeight())
        .getIntersection(getLocalBounds());
}

void WaveformComponent::resized()
{
    updatePlayheadOverlay();
}

void WaveformComponent::updatePlayheadOverlay()
{
    const auto strip = playheadStrip(playheadSeconds_);
    if (!strip.isEmpty())
        repaint(strip);
}

void WaveformComponent::setTimelineRange(const double timelineLengthSeconds,
                                         const double clipStartSeconds,
                                         const double sourceOffsetSeconds,
                                         const double clipDurationSeconds)
{
    timelineLengthSeconds_ = std::max(0.0, timelineLengthSeconds);
    clipStartSeconds_ = std::max(0.0, clipStartSeconds);
    sourceOffsetSeconds_ = std::max(0.0, sourceOffsetSeconds);
    clipDurationSeconds_ = std::max(0.0, clipDurationSeconds);
    regions_.clear();
    selectedRegionIds_.clear();
    clearRegionThumbnails();
    if (clipDurationSeconds_ > 0.0)
        regions_.push_back({ 0, {}, clipStartSeconds_, sourceOffsetSeconds_,
                             clipDurationSeconds_, clipDurationSeconds_,
                             accentColour_ });
    selectedRegionId_ = 0;
    repaint();
}

void WaveformComponent::setMovePreview(
    std::optional<Region> preview)
{
    movePreview_ = std::move(preview);
    repaint();
}

void WaveformComponent::setVisibleRange(const double startSeconds,
                                        const double endSeconds)
{
    const auto timelineEnd = std::max(0.000001, timelineLengthSeconds_);
    visibleStartSeconds_ = juce::jlimit(
        0.0, std::max(0.0, timelineEnd - 0.000001), startSeconds);
    visibleEndSeconds_ = juce::jlimit(
        visibleStartSeconds_ + 0.000001, timelineEnd, endSeconds);
    updatePlayheadOverlay();
    repaint();
}

void WaveformComponent::setTimeSelection(const double startSeconds,
                                         const double endSeconds,
                                         const bool trackIncluded,
                                         const bool visible)
{
    timeSelectionStartSeconds_ = std::min(startSeconds, endSeconds);
    timeSelectionEndSeconds_ = std::max(startSeconds, endSeconds);
    timeSelectionTrackIncluded_ = trackIncluded;
    timeSelectionVisible_ = visible;
    repaint();
}

void WaveformComponent::setRegions(const double timelineLengthSeconds,
                                   std::vector<Region> regions,
                                   std::vector<std::uint64_t> selectedRegionIds,
                                   const std::uint64_t primaryRegionId)
{
    timelineLengthSeconds_ = std::max(0.0, timelineLengthSeconds);
    auto previous = std::move(regionThumbnails_);
    regionThumbnails_.clear();
    regionThumbnails_.reserve(regions.size());
    for (const auto& region : regions) {
        const auto found = std::find_if(
            previous.begin(), previous.end(), [&region](const auto& value) {
                return value.id == region.id
                       && value.sourceFile == region.sourceFile
                       && value.thumbnail != nullptr;
            });
        if (found != previous.end()) {
            regionThumbnails_.push_back(std::move(*found));
            continue;
        }
        if (region.sourceFile.existsAsFile()) {
            auto value = RegionThumbnail {
                region.id, region.sourceFile,
                std::make_unique<juce::AudioThumbnail>(
                    thumbnailResolutionSamples, formatManager_,
                    thumbnailCache_) };
            value.thumbnail->addChangeListener(this);
            value.thumbnail->setSource(
                new juce::FileInputSource(region.sourceFile));
            regionThumbnails_.push_back(std::move(value));
        }
    }
    for (auto& value : previous)
        if (value.thumbnail != nullptr) {
            value.thumbnail->removeChangeListener(this);
            if (value.detailThumbnail != nullptr)
                value.detailThumbnail->removeChangeListener(this);
        }
    regions_ = std::move(regions);
    selectedRegionIds.erase(
        std::remove_if(selectedRegionIds.begin(), selectedRegionIds.end(),
                       [this](const auto id) {
                           return id == 0
                                  || std::none_of(
                                      regions_.begin(), regions_.end(),
                                      [id](const auto& region) {
                                          return region.id == id;
                                      });
                       }),
        selectedRegionIds.end());
    std::sort(selectedRegionIds.begin(), selectedRegionIds.end());
    selectedRegionIds.erase(
        std::unique(selectedRegionIds.begin(), selectedRegionIds.end()),
        selectedRegionIds.end());
    selectedRegionIds_ = std::move(selectedRegionIds);
    selectedRegionId_ = primaryRegionId;
    if (!isRegionSelected(selectedRegionId_))
        selectedRegionId_ = selectedRegionIds_.empty()
                                ? 0
                                : selectedRegionIds_.front();
    repaint();
}

void WaveformComponent::setMusicalGrid(const double tempoBpm,
                                       const int division,
                                       const bool snappingEnabled)
{
    tempoBpm_ = juce::jlimit(20.0, 400.0, tempoBpm);
    gridDivision_ = juce::jlimit(1, 64, division);
    snappingEnabled_ = snappingEnabled;
    repaint();
}

void WaveformComponent::setEditingTool(const EditingTool tool)
{
    editingTool_ = tool;
    updateMouseCursor(getMouseXYRelative().toFloat());
}

void WaveformComponent::setSnapMode(const SnapMode mode)
{
    snapMode_ = mode;
}

void WaveformComponent::setLocalClipEditing(const bool enabled)
{
    localClipEditing_ = enabled;
    repaint();
}

void WaveformComponent::setAccentColour(const juce::Colour colour)
{
    accentColour_ = colour;
    repaint();
}

void WaveformComponent::setSeekHandler(std::function<void(double)> handler)
{
    seekHandler_ = std::move(handler);
}

void WaveformComponent::setRegionSelectionHandler(
    std::function<void(std::uint64_t, RegionSelectionMode)> handler)
{
    regionSelectionHandler_ = std::move(handler);
}

void WaveformComponent::setRegionMoveHandler(
    std::function<void(std::uint64_t, double, int)> handler)
{
    regionMoveHandler_ = std::move(handler);
}

void WaveformComponent::setRegionEditHandler(
    std::function<void(std::uint64_t, double, double, double,
                       double, double, double)> handler)
{
    regionEditHandler_ = std::move(handler);
}

void WaveformComponent::setRegionLiveEditHandler(
    std::function<void(std::uint64_t, double, double, double,
                       double, double, double)> handler)
{
    regionLiveEditHandler_ = std::move(handler);
}

void WaveformComponent::setRegionDragPreviewHandler(
    std::function<void(std::uint64_t, double, int, bool)> handler)
{
    regionDragPreviewHandler_ = std::move(handler);
}

void WaveformComponent::setRegionCommandHandler(
    std::function<void(std::uint64_t, RegionCommand, double)> handler)
{
    regionCommandHandler_ = std::move(handler);
}

void WaveformComponent::setRegionOpenHandler(
    std::function<void(std::uint64_t)> handler)
{
    regionOpenHandler_ = std::move(handler);
}

void WaveformComponent::setStretchMarkerAddHandler(
    std::function<void(std::uint64_t, double, double)> handler)
{
    stretchMarkerAddHandler_ = std::move(handler);
}

void WaveformComponent::setStretchMarkerUpdateHandler(
    std::function<void(std::uint64_t, std::uint64_t, double)> handler)
{
    stretchMarkerUpdateHandler_ = std::move(handler);
}

void WaveformComponent::setStretchMarkerLiveUpdateHandler(
    std::function<void(std::uint64_t, std::uint64_t, double)> handler)
{
    stretchMarkerLiveUpdateHandler_ = std::move(handler);
}

void WaveformComponent::setStretchMarkerRemoveHandler(
    std::function<void(std::uint64_t, std::uint64_t)> handler)
{
    stretchMarkerRemoveHandler_ = std::move(handler);
}

void WaveformComponent::setZoomHandler(
    std::function<void(double, double)> handler)
{
    zoomHandler_ = std::move(handler);
}

void WaveformComponent::setPanHandler(std::function<void(double)> handler)
{
    panHandler_ = std::move(handler);
}

void WaveformComponent::setVerticalPanHandler(
    std::function<void(int)> handler)
{
    verticalPanHandler_ = std::move(handler);
}

void WaveformComponent::setMarqueeHandler(
    std::function<void(double, double, int, MarqueePhase, bool)> handler)
{
    marqueeHandler_ = std::move(handler);
}

void WaveformComponent::setEditGestureHandlers(
    std::function<void()> beginHandler,
    std::function<void()> endHandler)
{
    editGestureBeginHandler_ = std::move(beginHandler);
    editGestureEndHandler_ = std::move(endHandler);
}

void WaveformComponent::paint(juce::Graphics& graphics)
{
    const paint_profile::Scoped paintScope(paint_profile::counters().waveform);
    const auto bounds = getLocalBounds().toFloat();
    auto timelineLength = timelineLengthSeconds_;
    for (const auto& region : regions_)
        timelineLength = std::max(timelineLength,
                                  region.startSeconds + region.durationSeconds);
    graphics.fillAll(juce::Colour(0xff24272a));
    const auto viewDuration = visibleDuration();
    const auto drawMusicalGrid = [this, &graphics, viewDuration](
                                      const juce::Rectangle<int> area,
                                      const float alphaScale) {
        if (area.isEmpty() || tempoBpm_ <= 0.0 || gridDivision_ <= 0)
            return;
        const auto baseStep = 60.0 / tempoBpm_
                              * 4.0 / static_cast<double>(gridDivision_);
        auto visualMultiple = 1;
        while (baseStep * static_cast<double>(visualMultiple)
                   / viewDuration * static_cast<double>(getWidth())
               < 7.0)
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
            const auto x = xFromTime(
                static_cast<double>(stepIndex) * visualStep);
            const auto pixel = juce::roundToInt(x);
            if (pixel < area.getX() || pixel >= area.getRight())
                continue;
            graphics.setColour((
                barLine ? juce::Colour(0xff6a5a43).withAlpha(0.82f)
                        : beatLine
                              ? juce::Colour(0xff697079).withAlpha(0.74f)
                              : juce::Colour(0xff3b4045).withAlpha(
                                    snappingEnabled_ ? 0.62f : 0.38f))
                                   .withMultipliedAlpha(alphaScale));
            graphics.drawVerticalLine(pixel,
                                      static_cast<float>(area.getY()),
                                      static_cast<float>(area.getBottom()));
        }
    };
    if (timelineLength > 0.0 && tempoBpm_ > 0.0 && gridDivision_ > 0) {
        drawMusicalGrid(getLocalBounds().getIntersection(
                            graphics.getClipBounds()), 1.0f);
    } else {
        graphics.setColour(juce::Colour(0xff34383b));
        for (int x = 0; x < getWidth(); x += 64)
            graphics.drawVerticalLine(
                x, 0.0f, static_cast<float>(getHeight()));
    }
    graphics.setColour(juce::Colour(0xff2c333f));
    if (localClipEditing_) {
        auto laneCount = 1;
        for (const auto& region : regions_)
            laneCount = std::max(laneCount, region.laneCount);
        const auto laneHeight = static_cast<float>(getHeight())
                                / static_cast<float>(laneCount);
        for (auto lane = 1; lane < laneCount; ++lane)
            graphics.drawHorizontalLine(
                juce::roundToInt(static_cast<float>(lane) * laneHeight),
                0.0f, static_cast<float>(getWidth()));
    } else {
        graphics.drawHorizontalLine(getHeight() / 2, 0.0f,
                                    static_cast<float>(getWidth()));
    }

    if (timeSelectionVisible_ && timeSelectionTrackIncluded_
        && timeSelectionEndSeconds_ > visibleStartSeconds_
        && timeSelectionStartSeconds_ < visibleEndSeconds_) {
        const auto selectionStart = std::max(
            timeSelectionStartSeconds_, visibleStartSeconds_);
        const auto selectionEnd = std::min(
            timeSelectionEndSeconds_, visibleEndSeconds_);
        const auto selectionLeft = xFromTime(selectionStart);
        const auto selectionRight = xFromTime(selectionEnd);
        const auto selectionBounds = juce::Rectangle<float>(
            selectionLeft, 0.0f,
            std::max(1.0f, selectionRight - selectionLeft),
            bounds.getHeight());
        graphics.setColour(juce::Colour(0xff53d0c2).withAlpha(0.13f));
        graphics.fillRect(selectionBounds);
        graphics.setColour(juce::Colour(0xff8ef5de).withAlpha(0.72f));
        graphics.drawVerticalLine(juce::roundToInt(selectionLeft),
                                  0.0f, bounds.getHeight());
        graphics.drawVerticalLine(juce::roundToInt(selectionRight),
                                  0.0f, bounds.getHeight());
    }

    if (timelineLength > 0.0
        && (!regions_.empty() || movePreview_.has_value())) {
        for (const auto& region : regions_) {
            if (region.startSeconds + region.durationSeconds
                    <= visibleStartSeconds_
                || region.startSeconds >= visibleEndSeconds_)
                continue;
            const auto fullClipBounds = regionBounds(region);
            const auto clipBoundsFloat = fullClipBounds.getIntersection(bounds);
            const auto clipBounds = clipBoundsFloat.getSmallestIntegerContainer();
            // The part of the clip this paint is for. A playhead move asks
            // for a two-pixel strip thirty times a second, and the
            // thumbnail draws a column for every pixel of the area it is
            // handed — it was handed the whole clip.
            const auto drawArea = clipBounds.getIntersection(
                graphics.getClipBounds());
            if (drawArea.isEmpty())
                continue;
            const auto selected = isRegionSelected(region.id);
            const auto primary = region.id == selectedRegionId_;
            graphics.setColour(region.colour.darker(selected ? 0.58f : 0.72f));
            graphics.fillRoundedRectangle(clipBounds.toFloat(), 3.0f);
            // The clip background must not hide the musical timing grid.
            // Redrawing it before the waveform keeps the grid visually
            // behind the audio while remaining readable in silent areas.
            drawMusicalGrid(drawArea, selected ? 0.58f : 0.42f);
            const auto playbackRate = region.sourceDurationSeconds > 0.0
                                          ? region.sourceDurationSeconds
                                                / std::max(0.000001,
                                                           region.durationSeconds)
                                          : 1.0;
            auto* regionThumbnail = thumbnailForRegion(region.id);
            if (regionThumbnail == nullptr
                && thumbnail_.getTotalLength() > 0.0)
                regionThumbnail = &thumbnail_;
            if (regionThumbnail != nullptr
                && regionThumbnail->getTotalLength() > 0.0) {
                graphics.setColour(region.colour.brighter(0.36f));
                // The clip's visible span runs linearly across clipBounds;
                // the part under drawArea is the same span, cut to it.
                const auto localStart = std::max(
                    0.0, visibleStartSeconds_ - region.startSeconds);
                const auto localEnd = std::min(
                    region.durationSeconds,
                    visibleEndSeconds_ - region.startSeconds);
                const auto localAt = [&clipBounds, localStart, localEnd](
                                         const int x) {
                    const auto span = std::max(1, clipBounds.getWidth());
                    return localStart
                           + static_cast<double>(x - clipBounds.getX())
                                 / static_cast<double>(span)
                                 * (localEnd - localStart);
                };
                const auto drawThumbnailWithin = [&] {
                    regionThumbnail->drawChannels(
                        graphics, drawArea,
                        juce::jlimit(0.0, regionThumbnail->getTotalLength(),
                                     sourceTimeAt(region,
                                                  localAt(drawArea.getX()))),
                        juce::jlimit(0.0, regionThumbnail->getTotalLength(),
                                     sourceTimeAt(region,
                                                  localAt(drawArea.getRight()))),
                        1.0f);
                };
                if (localClipEditing_) {
                    const auto cached = std::find_if(
                        regionThumbnails_.begin(), regionThumbnails_.end(),
                        [&region](const auto& value) {
                            return value.id == region.id
                                   && value.thumbnail != nullptr;
                        });
                    if (cached != regionThumbnails_.end())
                        drawWarpedWaveform(
                            graphics, *cached, region, clipBounds,
                            region.colour.brighter(0.36f));
                    else
                        drawThumbnailWithin();
                } else {
                    drawThumbnailWithin();
                }
            }
            if (localClipEditing_) {
                auto titleStrip = clipBounds;
                titleStrip.setHeight(std::min(20, clipBounds.getHeight()));
                graphics.setColour(region.colour.darker(0.38f)
                                       .withAlpha(0.94f));
                graphics.fillRect(titleStrip);
                graphics.setColour(juce::Colours::white.withAlpha(0.18f));
                graphics.drawHorizontalLine(titleStrip.getBottom() - 1,
                    static_cast<float>(titleStrip.getX()),
                    static_cast<float>(titleStrip.getRight()));
                if (clipBounds.getHeight() >= 42) {
                    auto stretchStrip = clipBounds;
                    stretchStrip.setY(clipBounds.getBottom() - 14);
                    stretchStrip.setHeight(14);
                    graphics.setColour(juce::Colour(0xff183b39)
                                           .withAlpha(selected ? 0.92f
                                                               : 0.72f));
                    graphics.fillRect(stretchStrip);
                    graphics.setColour(juce::Colour(0xff53d0c2)
                                           .withAlpha(0.74f));
                    graphics.drawHorizontalLine(
                        stretchStrip.getY(),
                        static_cast<float>(stretchStrip.getX()),
                        static_cast<float>(stretchStrip.getRight()));
                    if (stretchStrip.getWidth() >= 92) {
                        graphics.setFont(juce::FontOptions(
                            8.0f, juce::Font::bold));
                        graphics.drawFittedText(
                            "STRETCH", stretchStrip.reduced(5, 0),
                            juce::Justification::centredRight, 1);
                    }
                }
            }
            if (clipBounds.getWidth() >= 54 && region.name.isNotEmpty()
                && drawArea.intersects(clipBounds.withHeight(15))) {
                graphics.setColour(juce::Colours::white.withAlpha(0.86f));
                graphics.setFont(juce::FontOptions(9.5f, juce::Font::bold));
                graphics.drawFittedText(region.name,
                                        clipBounds.withHeight(15).reduced(4, 0),
                                        juce::Justification::centredLeft, 1);
            }
            if (region.takeCount > 1 && clipBounds.getWidth() >= 76) {
                const auto badge = juce::Rectangle<int>(
                    clipBounds.getRight() - 43, clipBounds.getY() + 2,
                    39, 14);
                graphics.setColour(juce::Colour(0xff16191d).withAlpha(0.82f));
                graphics.fillRoundedRectangle(badge.toFloat(), 3.0f);
                graphics.setColour(juce::Colour(0xffffb24a));
                graphics.setFont(juce::FontOptions(8.5f, juce::Font::bold));
                graphics.drawFittedText(
                    "T" + juce::String(static_cast<int>(
                               region.activeTakeIndex + 1))
                        + "/" + juce::String(static_cast<int>(
                                    region.takeCount)),
                    badge, juce::Justification::centred, 1);
            }
            if (std::abs(playbackRate - 1.0) > 0.001
                && clipBounds.getWidth() >= 72) {
                const auto stretchBadge = juce::Rectangle<int>(
                    clipBounds.getRight() - 52,
                    clipBounds.getBottom() - 16, 48, 13);
                graphics.setColour(
                    juce::Colour(0xff10211f).withAlpha(0.88f));
                graphics.fillRoundedRectangle(stretchBadge.toFloat(), 3.0f);
                graphics.setColour(juce::Colour(0xff53d0c2));
                graphics.setFont(juce::FontOptions(8.0f,
                                                   juce::Font::bold));
                graphics.drawFittedText(
                    "ST " + juce::String(playbackRate, 2) + "x",
                    stretchBadge, juce::Justification::centred, 1);
            }
            if (localClipEditing_) {
                for (const auto& marker : region.stretchMarkers) {
                    if (marker.timelineSeconds <= 0.0
                        || marker.timelineSeconds >= region.durationSeconds)
                        continue;
                    const auto markerTime = region.startSeconds
                                            + marker.timelineSeconds;
                    if (markerTime < visibleStartSeconds_
                        || markerTime > visibleEndSeconds_)
                        continue;
                    const auto markerX = xFromTime(markerTime);
                    graphics.setColour(juce::Colour(0xff58c9ff)
                                           .withAlpha(selected ? 0.95f
                                                               : 0.72f));
                    graphics.drawVerticalLine(
                        juce::roundToInt(markerX),
                        static_cast<float>(clipBounds.getY() + 16),
                        clipBounds.getBottom() - 2.0f);
                    juce::Path anchor;
                    anchor.addTriangle(markerX - 5.0f,
                                       clipBounds.getBottom() - 1.0f,
                                       markerX + 5.0f,
                                       clipBounds.getBottom() - 1.0f,
                                       markerX,
                                       clipBounds.getBottom() - 9.0f);
                    graphics.fillPath(anchor);
                }
            }
            graphics.setColour(primary ? juce::Colour(0xffffb24a)
                                       : selected ? juce::Colour(0xffffd18a)
                                                  : region.colour.brighter(0.1f));
            graphics.drawRoundedRectangle(clipBounds.toFloat(), 3.0f,
                                          primary ? 2.4f
                                                  : selected ? 1.8f : 1.0f);
            const auto duration = std::max(0.000001, region.durationSeconds);
            const auto fadeInX = fullClipBounds.getX()
                                 + fullClipBounds.getWidth()
                                       * static_cast<float>(
                                           juce::jlimit(0.0, 1.0,
                                                       region.fadeInSeconds
                                                           / duration));
            const auto fadeOutX = fullClipBounds.getRight()
                                  - fullClipBounds.getWidth()
                                        * static_cast<float>(
                                            juce::jlimit(0.0, 1.0,
                                                        region.fadeOutSeconds
                                                            / duration));
            graphics.setColour(juce::Colours::white.withAlpha(
                selected ? 0.78f : 0.42f));
            if (region.fadeInSeconds > 0.0)
                graphics.drawLine(fullClipBounds.getX(),
                                  fullClipBounds.getBottom(), fadeInX,
                                  fullClipBounds.getY(), 1.3f);
            if (region.fadeOutSeconds > 0.0)
                graphics.drawLine(fadeOutX, fullClipBounds.getY(),
                                  fullClipBounds.getRight(),
                                  fullClipBounds.getBottom(), 1.3f);
        }
        for (std::size_t firstIndex = 0; firstIndex < regions_.size();
             ++firstIndex) {
            for (std::size_t secondIndex = firstIndex + 1;
                 secondIndex < regions_.size(); ++secondIndex) {
                const auto* outgoing = &regions_[firstIndex];
                const auto* incoming = &regions_[secondIndex];
                if (incoming->startSeconds < outgoing->startSeconds)
                    std::swap(outgoing, incoming);
                const auto overlapStart = std::max(outgoing->startSeconds,
                                                   incoming->startSeconds);
                const auto overlapEnd = std::min(
                    outgoing->startSeconds + outgoing->durationSeconds,
                    incoming->startSeconds + incoming->durationSeconds);
                if (overlapEnd <= overlapStart
                    || outgoing->fadeOutSeconds <= 0.0
                    || incoming->fadeInSeconds <= 0.0
                    || overlapEnd <= visibleStartSeconds_
                    || overlapStart >= visibleEndSeconds_)
                    continue;
                const auto visibleOverlapStart = std::max(
                    overlapStart, visibleStartSeconds_);
                const auto visibleOverlapEnd = std::min(
                    overlapEnd, visibleEndSeconds_);
                const auto overlapLeft = xFromTime(visibleOverlapStart);
                const auto overlapWidth = std::max(
                    1.0f, xFromTime(visibleOverlapEnd) - overlapLeft);
                auto crossfadeBounds = juce::Rectangle<float>(
                    overlapLeft, 4.0f, overlapWidth,
                    std::max(1.0f, bounds.getHeight() - 8.0f));
                graphics.setColour(juce::Colour(0xff101318).withAlpha(0.46f));
                graphics.fillRect(crossfadeBounds);
                graphics.setColour(outgoing->colour.brighter(0.45f));
                graphics.drawLine(crossfadeBounds.getX(),
                                  crossfadeBounds.getY(),
                                  crossfadeBounds.getRight(),
                                  crossfadeBounds.getBottom(), 1.8f);
                graphics.setColour(incoming->colour.brighter(0.45f));
                graphics.drawLine(crossfadeBounds.getX(),
                                  crossfadeBounds.getBottom(),
                                  crossfadeBounds.getRight(),
                                  crossfadeBounds.getY(), 1.8f);
                graphics.setColour(juce::Colours::white.withAlpha(0.82f));
                graphics.drawRect(crossfadeBounds, 1.0f);
                if (crossfadeBounds.getWidth() >= 48.0f) {
                    graphics.setFont(juce::FontOptions(8.5f,
                                                       juce::Font::bold));
                    auto labelBounds = crossfadeBounds
                                           .getSmallestIntegerContainer()
                                           .reduced(4, 0)
                                           .withHeight(14);
                    labelBounds.setCentre(labelBounds.getCentreX(),
                                          static_cast<int>(
                                              crossfadeBounds.getCentreY()));
                    graphics.drawFittedText(
                        tr("audioClip.crossfadeShort"),
                        labelBounds,
                        juce::Justification::centred, 1);
                }
            }
        }
        const auto selected = std::find_if(
            regions_.begin(), regions_.end(), [this](const auto& region) {
                return region.id == selectedRegionId_;
            });
        if (selected != regions_.end()
            && selected->startSeconds + selected->durationSeconds
                   > visibleStartSeconds_
            && selected->startSeconds < visibleEndSeconds_) {
            const auto selectedBounds = regionBounds(*selected);
            const auto duration = std::max(0.000001,
                                           selected->durationSeconds);
            const auto fadeInX = selectedBounds.getX()
                                 + selectedBounds.getWidth()
                                       * static_cast<float>(juce::jlimit(
                                           0.0, 1.0,
                                           selected->fadeInSeconds / duration));
            const auto fadeOutX = selectedBounds.getRight()
                                  - selectedBounds.getWidth()
                                        * static_cast<float>(juce::jlimit(
                                            0.0, 1.0,
                                            selected->fadeOutSeconds / duration));
            graphics.setColour(juce::Colours::white.withAlpha(0.95f));
            graphics.fillEllipse(fadeInX - 4.5f,
                                 selectedBounds.getY() - 2.0f,
                                 9.0f, 9.0f);
            graphics.fillEllipse(fadeOutX - 4.5f,
                                 selectedBounds.getY() - 2.0f,
                                 9.0f, 9.0f);
            graphics.setColour(juce::Colour(0xffff9b2f));
            graphics.drawEllipse(fadeInX - 4.5f,
                                 selectedBounds.getY() - 2.0f,
                                 9.0f, 9.0f, 1.5f);
            graphics.drawEllipse(fadeOutX - 4.5f,
                                 selectedBounds.getY() - 2.0f,
                                 9.0f, 9.0f, 1.5f);
            graphics.fillRect(selectedBounds.getX(),
                              selectedBounds.getCentreY() - 8.0f,
                              2.0f, 16.0f);
            graphics.fillRect(selectedBounds.getRight() - 2.0f,
                              selectedBounds.getCentreY() - 8.0f,
                              2.0f, 16.0f);
            const auto stretchHandle = juce::Rectangle<float>(
                selectedBounds.getRight() - 13.0f,
                selectedBounds.getBottom() - 13.0f, 13.0f, 13.0f);
            graphics.setColour(juce::Colour(0xff53d0c2));
            graphics.fillRect(stretchHandle);
            graphics.setColour(juce::Colour(0xff101518));
            graphics.drawLine(stretchHandle.getX() + 2.0f,
                              stretchHandle.getBottom() - 2.0f,
                              stretchHandle.getRight() - 2.0f,
                              stretchHandle.getY() + 2.0f, 1.3f);
        }
        if (movePreview_.has_value()) {
            const auto previewBounds = regionBounds(*movePreview_)
                                           .getIntersection(bounds);
            if (!previewBounds.isEmpty()) {
                graphics.setColour(
                    movePreview_->colour.withAlpha(0.28f));
                graphics.fillRoundedRectangle(previewBounds, 4.0f);
                graphics.setColour(juce::Colour(0xff8ef5de));
                graphics.drawRoundedRectangle(previewBounds, 4.0f, 2.0f);
                graphics.setFont(juce::FontOptions(9.0f,
                                                   juce::Font::bold));
                graphics.drawFittedText(
                    movePreview_->name,
                    previewBounds.getSmallestIntegerContainer().reduced(5),
                    juce::Justification::centredLeft, 1);
            }
        }
    } else {
        graphics.setColour(juce::Colour(0xff7f889a));
        graphics.drawFittedText(tr("waveform.empty"),
                                getLocalBounds().reduced(20), juce::Justification::centred, 2);
    }

    if (!playheadDrawnByHost_ && !playheadStrip(playheadSeconds_).isEmpty()) {
        graphics.setColour(juce::Colour(0xffff8a00));
        graphics.fillRect(juce::roundToInt(xFromTime(playheadSeconds_)) - 1, 0,
                          2, getHeight());
    }
    graphics.setColour(juce::Colour(0xff303744));
    graphics.drawRect(getLocalBounds(), 1);
}

void WaveformComponent::mouseDown(const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    // JUCE also calls mouseDoubleClick after this event. Avoid starting a
    // move/trim gesture on the second press; the double-click handler owns the
    // contextual action.
    if (event.getNumberOfClicks() >= 2 && event.mods.isLeftButtonDown()) {
        if (!localClipEditing_) {
            if (const auto* region = regionAtPosition(event.position)) {
                arrangementDoubleClickHandled_ = true;
                if (regionOpenHandler_)
                    regionOpenHandler_(region->id);
            }
        }
        return;
    }
    if (event.mods.isMiddleButtonDown()
        || (editingTool_ == EditingTool::hand
            && event.mods.isLeftButtonDown()
            && !event.mods.isPopupMenu())) {
        panning_ = true;
        panDragLastX_ = event.position.x;
        panDragLastScreenY_ = event.getScreenPosition().y;
        draggedRegionId_ = 0;
        dragMode_ = DragMode::none;
        marqueeDragging_ = false;
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        return;
    }
    if (editingTool_ == EditingTool::zoom
        && event.mods.isLeftButtonDown()
        && !event.mods.isPopupMenu()) {
        zooming_ = true;
        zoomGestureMoved_ = false;
        zoomDragLastX_ = event.position.x;
        zoomDragLastScreenY_ = event.getScreenPosition().y;
        zoomAnchorSeconds_ = timeFromX(event.position.x);
        draggedRegionId_ = 0;
        dragMode_ = DragMode::none;
        marqueeDragging_ = false;
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
        return;
    }
    const auto timelineSeconds = timeFromX(event.position.x);
    auto* region = regionAtPosition(event.position);
    if (region != nullptr) {
        if (localClipEditing_) {
            if (const auto* marker = stretchMarkerAt(*region,
                                                     event.position)) {
                const auto regionId = region->id;
                const auto markerId = marker->id;
                const auto markerTimeline = marker->timelineSeconds;
                if (event.mods.isPopupMenu()) {
                    if (stretchMarkerRemoveHandler_)
                        stretchMarkerRemoveHandler_(regionId, markerId);
                    return;
                }
                if (event.mods.isLeftButtonDown()) {
                    if (regionSelectionHandler_)
                        regionSelectionHandler_(
                            regionId, RegionSelectionMode::replace);
                    draggedRegionId_ = regionId;
                    draggedStretchMarkerId_ = markerId;
                    draggedStretchMarkerTimelineSeconds_
                        = markerTimeline;
                    dragMode_ = DragMode::stretchMarker;
                    dragStartX_ = event.position.x;
                    if (editGestureBeginHandler_)
                        editGestureBeginHandler_();
                    return;
                }
            }
        }
        const auto hitRegionId = region->id;
        const auto wasSelected = isRegionSelected(hitRegionId);
        const auto mode = event.mods.isCommandDown()
                              ? RegionSelectionMode::toggle
                              : wasSelected
                                        && selectedRegionIds_.size() > 1
                                    ? RegionSelectionMode::makePrimary
                                    : RegionSelectionMode::replace;
        if (regionSelectionHandler_)
            regionSelectionHandler_(hitRegionId, mode);
        region = nullptr;
        for (auto& candidate : regions_)
            if (candidate.id == hitRegionId) {
                region = &candidate;
                break;
            }
        if (region == nullptr || !isRegionSelected(hitRegionId)) {
            draggedRegionId_ = 0;
            dragMode_ = DragMode::none;
            repaint();
            return;
        }
        if (event.mods.isPopupMenu()) {
            showRegionMenu(region->id, timelineSeconds);
            repaint();
            return;
        }
        if (editingTool_ == EditingTool::knife) {
            if (regionCommandHandler_)
                regionCommandHandler_(region->id, RegionCommand::split,
                                      snappedTime(timelineSeconds,
                                                  event.mods.isAltDown()));
            draggedRegionId_ = 0;
            dragMode_ = DragMode::none;
            repaint();
            return;
        }
        draggedRegionId_ = region->id;
        dragMode_ = editingTool_ == EditingTool::stretch
                        ? DragMode::stretchRight
                        : dragModeAt(*region, event.position);
        if (dragMode_ == DragMode::none)
            dragMode_ = DragMode::move;
        dragStartX_ = event.position.x;
        draggedRegionStartSeconds_ = region->startSeconds;
        draggedRegionOffsetSeconds_ = region->sourceOffsetSeconds;
        draggedRegionDurationSeconds_ = region->durationSeconds;
        draggedRegionSourceDurationSeconds_
            = region->sourceDurationSeconds > 0.0
                  ? region->sourceDurationSeconds
                  : region->durationSeconds;
        draggedRegionFadeInSeconds_ = region->fadeInSeconds;
        draggedRegionFadeOutSeconds_ = region->fadeOutSeconds;
        draggedRegionStarts_.clear();
        if (dragMode_ == DragMode::move) {
            for (const auto& candidate : regions_)
                if (isRegionSelected(candidate.id))
                    draggedRegionStarts_.push_back(
                        { candidate.id, candidate.startSeconds });
        }
        if (draggedRegionStarts_.empty())
            draggedRegionStarts_.push_back(
                { region->id, region->startSeconds });
        if (editGestureBeginHandler_)
            editGestureBeginHandler_();
    } else {
        draggedRegionId_ = 0;
        dragMode_ = DragMode::none;
        draggedRegionStarts_.clear();
        if (event.mods.isLeftButtonDown()
            && !event.mods.isPopupMenu()) {
            marqueeDragging_ = true;
            marqueeAnchorSeconds_ = timelineSeconds;
            if (marqueeHandler_)
                marqueeHandler_(marqueeAnchorSeconds_, timelineSeconds,
                                event.getScreenPosition().y,
                                MarqueePhase::begin,
                                event.mods.isCommandDown());
        }
    }
    if (dragMode_ == DragMode::move || region == nullptr)
        seekFromX(event.position.x, event.mods.isAltDown());
    repaint();
}

bool WaveformComponent::keyPressed(const juce::KeyPress& key)
{
    // A time selection may span several lane components. Let the workspace
    // handle its shortcuts so copy, delete and range edits stay atomic across
    // every selected track instead of being reduced to this lane.
    if (timeSelectionVisible_)
        return false;
    if (selectedRegionId_ == 0 || !regionCommandHandler_)
        return false;
    if (key.getModifiers().isCommandDown()
        && !key.getModifiers().isShiftDown()
        && key.getKeyCode() == 'C') {
        regionCommandHandler_(selectedRegionId_, RegionCommand::copy,
                              playheadSeconds_);
        return true;
    }
    if (key.getModifiers().isCommandDown()
        && !key.getModifiers().isShiftDown()
        && key.getKeyCode() == 'X') {
        regionCommandHandler_(selectedRegionId_, RegionCommand::cut,
                              playheadSeconds_);
        return true;
    }
    if (key.getModifiers().isCommandDown()
        && !key.getModifiers().isShiftDown()
        && key.getKeyCode() == 'V') {
        regionCommandHandler_(selectedRegionId_, RegionCommand::paste,
                              snappedTime(playheadSeconds_));
        return true;
    }
    if (key.getModifiers().isCommandDown()
        && !key.getModifiers().isShiftDown()
        && key.getKeyCode() == 'D') {
        regionCommandHandler_(selectedRegionId_, RegionCommand::duplicate,
                              snappedTime(playheadSeconds_));
        return true;
    }
    if (key.getModifiers().isCommandDown()
        && key.getModifiers().isShiftDown()
        && key.getKeyCode() == 'X') {
        regionCommandHandler_(selectedRegionId_, RegionCommand::split,
                              snappedTime(playheadSeconds_));
        return true;
    }
    if (!key.getModifiers().isCommandDown()
        && key.getKeyCode() == juce::KeyPress::deleteKey) {
        regionCommandHandler_(selectedRegionId_, RegionCommand::remove,
                              playheadSeconds_);
        return true;
    }
    return false;
}

void WaveformComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (panning_) {
        if (panHandler_ && getWidth() > 0) {
            const auto deltaPixels = event.position.x - panDragLastX_;
            const auto visibleDuration = std::max(
                0.000001, visibleEndSeconds_ - visibleStartSeconds_);
            panHandler_(-static_cast<double>(deltaPixels)
                        / static_cast<double>(getWidth())
                        * visibleDuration);
        }
        const auto screenY = event.getScreenPosition().y;
        if (verticalPanHandler_ && screenY != panDragLastScreenY_)
            verticalPanHandler_(screenY - panDragLastScreenY_);
        panDragLastX_ = event.position.x;
        panDragLastScreenY_ = screenY;
        return;
    }
    if (zooming_) {
        const auto screenY = event.getScreenPosition().y;
        const auto verticalDelta = screenY - zoomDragLastScreenY_;
        zoomGestureMoved_ = zoomGestureMoved_
                            || std::abs(verticalDelta) >= 1
                            || std::abs(event.position.x - zoomDragLastX_)
                                   >= 1.0f;
        if (zoomHandler_ && verticalDelta != 0)
            zoomHandler_(std::pow(1.025, -static_cast<double>(verticalDelta)),
                         zoomAnchorSeconds_);
        if (panHandler_ && getWidth() > 0) {
            const auto horizontalDelta = event.position.x - zoomDragLastX_;
            const auto visibleDuration = std::max(
                0.000001, visibleEndSeconds_ - visibleStartSeconds_);
            panHandler_(-static_cast<double>(horizontalDelta)
                        / static_cast<double>(getWidth())
                        * visibleDuration);
        }
        zoomDragLastX_ = event.position.x;
        zoomDragLastScreenY_ = screenY;
        return;
    }
    if (marqueeDragging_) {
        if (marqueeHandler_)
            marqueeHandler_(marqueeAnchorSeconds_,
                            timeFromX(event.position.x),
                            event.getScreenPosition().y,
                            MarqueePhase::update,
                            event.mods.isCommandDown());
        return;
    }
    if (draggedRegionId_ == 0 || timelineLengthSeconds_ <= 0.0
        || getWidth() <= 0) {
        seekFromX(event.position.x, event.mods.isAltDown());
        return;
    }
    Region* region = nullptr;
    for (auto& candidate : regions_)
        if (candidate.id == draggedRegionId_) {
            region = &candidate;
            break;
        }
    if (region == nullptr)
        return;
    const auto rawDelta = static_cast<double>(event.position.x - dragStartX_)
                          / static_cast<double>(getWidth())
                          * std::max(0.000001,
                                     visibleEndSeconds_
                                         - visibleStartSeconds_);
    // Preserve sub-millisecond pointer precision. Musical quantisation is
    // applied explicitly by snappedTime(); an unconditional 1 ms rounding
    // made markers and event edges visibly step at high zoom levels.
    const auto delta = rawDelta;
    constexpr double minimumDuration = 0.001;
    if (dragMode_ == DragMode::move) {
        auto groupMinimumStart = draggedRegionStartSeconds_;
        for (const auto& [id, start] : draggedRegionStarts_) {
            juce::ignoreUnused(id);
            groupMinimumStart = std::min(groupMinimumStart, start);
        }
        const auto snappedStart = snappedTime(
            draggedRegionStartSeconds_ + delta,
            event.mods.isAltDown(), draggedRegionStartSeconds_);
        const auto groupDelta = std::max(
            snappedStart - draggedRegionStartSeconds_,
            -groupMinimumStart);
        for (const auto& [id, start] : draggedRegionStarts_)
            for (auto& candidate : regions_)
                if (candidate.id == id) {
                    candidate.startSeconds = start + groupDelta;
                    break;
                }
        if (regionDragPreviewHandler_)
            regionDragPreviewHandler_(
                region->id, region->startSeconds,
                event.getScreenPosition().y, true);
    } else if (dragMode_ == DragMode::trimLeft) {
        const auto sourcePerTimeline = draggedRegionSourceDurationSeconds_
                                       / std::max(minimumDuration,
                                                  draggedRegionDurationSeconds_);
        const auto minimumDelta = -std::min(
            draggedRegionStartSeconds_,
            draggedRegionOffsetSeconds_
                / std::max(0.000001, sourcePerTimeline));
        const auto maximumDelta = draggedRegionDurationSeconds_
                                  - minimumDuration;
        const auto targetStart = snappedTime(
            draggedRegionStartSeconds_ + delta,
            event.mods.isAltDown(), draggedRegionStartSeconds_);
        const auto safeDelta = juce::jlimit(
            minimumDelta, maximumDelta,
            targetStart - draggedRegionStartSeconds_);
        region->startSeconds = draggedRegionStartSeconds_ + safeDelta;
        region->sourceOffsetSeconds = draggedRegionOffsetSeconds_
                                      + safeDelta * sourcePerTimeline;
        region->durationSeconds = draggedRegionDurationSeconds_ - safeDelta;
        region->sourceDurationSeconds = draggedRegionSourceDurationSeconds_
                                        - safeDelta * sourcePerTimeline;
        region->fadeInSeconds = std::min(draggedRegionFadeInSeconds_,
                                         region->durationSeconds);
        region->fadeOutSeconds = std::min(draggedRegionFadeOutSeconds_,
                                          region->durationSeconds);
    } else if (dragMode_ == DragMode::trimRight) {
        auto original = *region;
        original.durationSeconds = draggedRegionDurationSeconds_;
        original.sourceOffsetSeconds = draggedRegionOffsetSeconds_;
        original.sourceDurationSeconds
            = draggedRegionSourceDurationSeconds_;
        const auto originalSourceEnd = draggedRegionOffsetSeconds_
                                       + draggedRegionSourceDurationSeconds_;
        auto lastTimeline = 0.0;
        auto lastSource = draggedRegionOffsetSeconds_;
        for (const auto& marker : original.stretchMarkers) {
            if (marker.timelineSeconds <= lastTimeline
                || marker.timelineSeconds >= draggedRegionDurationSeconds_)
                continue;
            lastTimeline = marker.timelineSeconds;
            lastSource = marker.sourceSeconds;
        }
        const auto finalSourceRate = std::max(
            0.000001,
            (originalSourceEnd - lastSource)
                / std::max(0.000001,
                           draggedRegionDurationSeconds_ - lastTimeline));
        const auto maximumDuration = std::max(
            minimumDuration,
            draggedRegionDurationSeconds_
                + (region->sourceLengthSeconds - originalSourceEnd)
                      / finalSourceRate);
        const auto targetEnd = snappedTime(
            draggedRegionStartSeconds_ + draggedRegionDurationSeconds_
                + delta,
            event.mods.isAltDown(),
            draggedRegionStartSeconds_ + draggedRegionDurationSeconds_);
        region->durationSeconds = juce::jlimit(
            minimumDuration, maximumDuration,
            targetEnd - draggedRegionStartSeconds_);
        const auto newSourceEnd
            = region->durationSeconds <= draggedRegionDurationSeconds_
                  ? sourceTimeAt(original, region->durationSeconds)
                  : originalSourceEnd
                        + (region->durationSeconds
                           - draggedRegionDurationSeconds_)
                              * finalSourceRate;
        region->sourceDurationSeconds = std::max(
            minimumDuration,
            std::min(region->sourceLengthSeconds, newSourceEnd)
                - draggedRegionOffsetSeconds_);
        region->fadeInSeconds = std::min(draggedRegionFadeInSeconds_,
                                         region->durationSeconds);
        region->fadeOutSeconds = std::min(draggedRegionFadeOutSeconds_,
                                          region->durationSeconds);
    } else if (dragMode_ == DragMode::stretchRight) {
        const auto targetEnd = snappedTime(
            draggedRegionStartSeconds_ + draggedRegionDurationSeconds_
                + delta,
            event.mods.isAltDown(),
            draggedRegionStartSeconds_ + draggedRegionDurationSeconds_);
        region->durationSeconds = juce::jlimit(
            minimumDuration, 86'400.0,
            targetEnd - draggedRegionStartSeconds_);
        region->sourceDurationSeconds
            = draggedRegionSourceDurationSeconds_;
        region->fadeInSeconds = std::min(draggedRegionFadeInSeconds_,
                                         region->durationSeconds);
        region->fadeOutSeconds = std::min(draggedRegionFadeOutSeconds_,
                                          region->durationSeconds);
    } else if (dragMode_ == DragMode::fadeIn) {
        const auto targetFade = snappedTime(
            draggedRegionFadeInSeconds_ + delta,
            event.mods.isAltDown(), draggedRegionFadeInSeconds_);
        region->fadeInSeconds = juce::jlimit(
            0.0, region->durationSeconds,
            targetFade);
    } else if (dragMode_ == DragMode::fadeOut) {
        const auto targetFade = snappedTime(
            draggedRegionFadeOutSeconds_ - delta,
            event.mods.isAltDown(), draggedRegionFadeOutSeconds_);
        region->fadeOutSeconds = juce::jlimit(
            0.0, region->durationSeconds,
            targetFade);
    } else if (dragMode_ == DragMode::stretchMarker) {
        const auto marker = std::find_if(
            region->stretchMarkers.begin(), region->stretchMarkers.end(),
            [this](const auto& value) {
                return value.id == draggedStretchMarkerId_;
            });
        if (marker != region->stretchMarkers.end()) {
            const auto position = static_cast<std::size_t>(
                std::distance(region->stretchMarkers.begin(), marker));
            const auto minimum = position == 0
                                     ? 0.001
                                     : region->stretchMarkers[position - 1]
                                               .timelineSeconds
                                           + 0.001;
            const auto maximum = position + 1
                                         == region->stretchMarkers.size()
                                     ? region->durationSeconds - 0.001
                                     : region->stretchMarkers[position + 1]
                                               .timelineSeconds
                                           - 0.001;
            const auto target = snappedTime(
                                    region->startSeconds
                                        + draggedStretchMarkerTimelineSeconds_
                                        + delta,
                                    event.mods.isAltDown(),
                                    region->startSeconds
                                        + draggedStretchMarkerTimelineSeconds_)
                                - region->startSeconds;
            marker->timelineSeconds = juce::jlimit(minimum, maximum,
                                                   target);
        }
    }
    repaint();
    // Publish the current gesture value without rebuilding the editor. The
    // regular handlers are still invoked on mouse-up to commit the edit and
    // refresh the surrounding UI/history.
    if (dragMode_ == DragMode::stretchMarker) {
        if (stretchMarkerLiveUpdateHandler_) {
            const auto marker = std::find_if(
                region->stretchMarkers.begin(),
                region->stretchMarkers.end(), [this](const auto& value) {
                    return value.id == draggedStretchMarkerId_;
                });
            if (marker != region->stretchMarkers.end())
                stretchMarkerLiveUpdateHandler_(
                    region->id, marker->id, marker->timelineSeconds);
        }
    } else if (regionLiveEditHandler_) {
        regionLiveEditHandler_(region->id, region->startSeconds,
                               region->sourceOffsetSeconds,
                               region->durationSeconds,
                               region->sourceDurationSeconds,
                               region->fadeInSeconds,
                               region->fadeOutSeconds);
    }
}

void WaveformComponent::mouseUp(const juce::MouseEvent& event)
{
    if (panning_) {
        panning_ = false;
        updateMouseCursor(getMouseXYRelative().toFloat());
        return;
    }
    if (zooming_) {
        zooming_ = false;
        if (!zoomGestureMoved_ && zoomHandler_)
            zoomHandler_(2.0, zoomAnchorSeconds_);
        updateMouseCursor(getMouseXYRelative().toFloat());
        return;
    }
    if (marqueeDragging_) {
        marqueeDragging_ = false;
        if (marqueeHandler_)
            marqueeHandler_(marqueeAnchorSeconds_,
                            timeFromX(event.position.x),
                            event.getScreenPosition().y,
                            MarqueePhase::end,
                            event.mods.isCommandDown());
        updateMouseCursor(getMouseXYRelative().toFloat());
        return;
    }
    if (draggedRegionId_ != 0 && dragMode_ != DragMode::none) {
        const auto found = std::find_if(
            regions_.begin(), regions_.end(), [this](const auto& region) {
                return region.id == draggedRegionId_;
            });
        if (found != regions_.end()
            && dragMode_ == DragMode::stretchMarker
            && stretchMarkerUpdateHandler_) {
            const auto marker = std::find_if(
                found->stretchMarkers.begin(),
                found->stretchMarkers.end(), [this](const auto& value) {
                    return value.id == draggedStretchMarkerId_;
                });
            if (marker != found->stretchMarkers.end())
                stretchMarkerUpdateHandler_(found->id, marker->id,
                                            marker->timelineSeconds);
        } else if (found != regions_.end() && dragMode_ == DragMode::move
            && regionMoveHandler_) {
            if (regionDragPreviewHandler_)
                regionDragPreviewHandler_(found->id, found->startSeconds,
                                          event.getScreenPosition().y,
                                          false);
            regionMoveHandler_(found->id, found->startSeconds,
                               event.getScreenPosition().y);
        } else if (found != regions_.end() && regionEditHandler_)
            regionEditHandler_(found->id, found->startSeconds,
                               found->sourceOffsetSeconds,
                               found->durationSeconds,
                               found->sourceDurationSeconds,
                               found->fadeInSeconds, found->fadeOutSeconds);
        if (editGestureEndHandler_)
            editGestureEndHandler_();
    }
    draggedRegionId_ = 0;
    draggedStretchMarkerId_ = 0;
    dragMode_ = DragMode::none;
    draggedRegionStarts_.clear();
    updateMouseCursor(getMouseXYRelative().toFloat());
}

void WaveformComponent::mouseMove(const juce::MouseEvent& event)
{
    updateMouseCursor(event.position);
}

void WaveformComponent::mouseExit(const juce::MouseEvent&)
{
    if (dragMode_ == DragMode::none && !panning_ && !zooming_)
        setMouseCursor(juce::MouseCursor::NormalCursor);
}

void WaveformComponent::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu())
        return;
    const auto* region = regionAtPosition(event.position);
    if (region == nullptr)
        return;
    const auto regionCopy = *region;
    if (!localClipEditing_) {
        if (arrangementDoubleClickHandled_) {
            arrangementDoubleClickHandled_ = false;
            return;
        }
        // Opening first is important: selecting an event may synchronously
        // refresh its lane, which used to destroy the component before JUCE
        // could deliver the second half of the double-click gesture.
        if (regionOpenHandler_)
            regionOpenHandler_(regionCopy.id);
        return;
    }
    if (regionSelectionHandler_)
        regionSelectionHandler_(regionCopy.id,
                                RegionSelectionMode::replace);
    const auto bounds = regionBounds(regionCopy);
    const auto timeline = snappedTime(timeFromX(event.position.x),
                                      event.mods.isAltDown());
    if (event.position.y <= bounds.getY() + 18.0f) {
        if (regionCommandHandler_)
            regionCommandHandler_(regionCopy.id, RegionCommand::split,
                                  timeline);
    } else if (stretchMarkerAddHandler_) {
        const auto localTimeline = juce::jlimit(
            0.001,
            std::max(0.001, regionCopy.durationSeconds - 0.001),
            timeline - regionCopy.startSeconds);
        stretchMarkerAddHandler_(
            regionCopy.id, sourceTimeAt(regionCopy, localTimeline),
            localTimeline);
    } else if (regionOpenHandler_)
        regionOpenHandler_(regionCopy.id);
}

void WaveformComponent::mouseWheelMove(
    const juce::MouseEvent& event,
    const juce::MouseWheelDetails& wheel)
{
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
void WaveformComponent::changeListenerCallback(juce::ChangeBroadcaster*) { repaint(); }

juce::AudioThumbnail* WaveformComponent::thumbnailForRegion(
    const std::uint64_t regionId) const noexcept
{
    const auto found = std::find_if(
        regionThumbnails_.begin(), regionThumbnails_.end(),
        [regionId](const auto& value) { return value.id == regionId; });
    return found != regionThumbnails_.end() ? found->thumbnail.get() : nullptr;
}

void WaveformComponent::clearRegionThumbnails()
{
    for (auto& value : regionThumbnails_)
        if (value.thumbnail != nullptr) {
            value.thumbnail->removeChangeListener(this);
            if (value.detailThumbnail != nullptr)
                value.detailThumbnail->removeChangeListener(this);
        }
    regionThumbnails_.clear();
}

void WaveformComponent::seekFromX(const float x,
                                  const bool bypassSnapping)
{
    if (timelineLengthSeconds_ > 0.0 && seekHandler_)
        seekHandler_(snappedTime(timeFromX(x), bypassSnapping));
}

double WaveformComponent::timeFromX(const float x) const noexcept
{
    if (timelineLengthSeconds_ <= 0.0 || getWidth() <= 0)
        return 0.0;
    return juce::jlimit(
        0.0, timelineLengthSeconds_,
        visibleStartSeconds_
            + static_cast<double>(x / static_cast<float>(getWidth()))
                  * visibleDuration());
}

float WaveformComponent::xFromTime(const double seconds) const noexcept
{
    if (getWidth() <= 0)
        return 0.0f;
    return static_cast<float>(
        (seconds - visibleStartSeconds_) / visibleDuration()
        * static_cast<double>(getWidth()));
}

double WaveformComponent::visibleDuration() const noexcept
{
    return std::max(0.000001,
                    visibleEndSeconds_ - visibleStartSeconds_);
}

WaveformComponent::Region* WaveformComponent::regionAtTime(
    const double seconds) noexcept
{
    for (auto region = regions_.rbegin(); region != regions_.rend(); ++region)
        if (seconds >= region->startSeconds
            && seconds <= region->startSeconds + region->durationSeconds)
            return &*region;
    return nullptr;
}

const WaveformComponent::Region* WaveformComponent::regionAtTime(
    const double seconds) const noexcept
{
    for (auto region = regions_.rbegin(); region != regions_.rend(); ++region)
        if (seconds >= region->startSeconds
            && seconds <= region->startSeconds + region->durationSeconds)
            return &*region;
    return nullptr;
}

WaveformComponent::Region* WaveformComponent::regionAtPosition(
    const juce::Point<float> position) noexcept
{
    for (auto region = regions_.rbegin(); region != regions_.rend(); ++region)
        if (regionBounds(*region).expanded(2.0f).contains(position))
            return &*region;
    return nullptr;
}

const WaveformComponent::Region* WaveformComponent::regionAtPosition(
    const juce::Point<float> position) const noexcept
{
    for (auto region = regions_.rbegin(); region != regions_.rend(); ++region)
        if (regionBounds(*region).expanded(2.0f).contains(position))
            return &*region;
    return nullptr;
}

juce::Rectangle<float> WaveformComponent::regionBounds(
    const Region& region) const noexcept
{
    if (getWidth() <= 0)
        return {};
    const auto left = xFromTime(region.startSeconds);
    const auto right = xFromTime(region.startSeconds
                                 + region.durationSeconds);
    const auto regionWidth = std::max(1.0f, right - left);
    if (localClipEditing_) {
        const auto laneCount = std::max(1, region.laneCount);
        const auto lane = juce::jlimit(0, laneCount - 1,
                                      region.laneIndex);
        const auto laneHeight = static_cast<float>(getHeight())
                                / static_cast<float>(laneCount);
        return { left, static_cast<float>(lane) * laneHeight + 4.0f,
                 regionWidth, std::max(1.0f, laneHeight - 8.0f) };
    }
    return { left, 4.0f, regionWidth,
             std::max(1.0f, static_cast<float>(getHeight()) - 8.0f) };
}

bool WaveformComponent::isRegionSelected(
    const std::uint64_t regionId) const noexcept
{
    return regionId != 0
           && std::find(selectedRegionIds_.begin(), selectedRegionIds_.end(),
                        regionId)
                  != selectedRegionIds_.end();
}

const WaveformComponent::Region::StretchMarker*
WaveformComponent::stretchMarkerAt(
    const Region& region, const juce::Point<float> position) const noexcept
{
    if (!localClipEditing_ || getWidth() <= 0)
        return nullptr;
    const auto bounds = regionBounds(region);
    if (!bounds.expanded(4.0f).contains(position))
        return nullptr;
    for (const auto& marker : region.stretchMarkers) {
        const auto markerX = xFromTime(region.startSeconds
                                      + marker.timelineSeconds);
        if (std::abs(position.x - markerX) <= 7.0f)
            return &marker;
    }
    return nullptr;
}

double WaveformComponent::sourceTimeAt(
    const Region& region, const double localTimelineSeconds) const noexcept
{
    struct Point { double timeline; double source; };
    std::vector<Point> points;
    points.reserve(region.stretchMarkers.size() + 2);
    points.push_back({ 0.0, region.sourceOffsetSeconds });
    for (const auto& marker : region.stretchMarkers)
        if (marker.timelineSeconds > 0.0
            && marker.timelineSeconds < region.durationSeconds)
            points.push_back(
                { marker.timelineSeconds, marker.sourceSeconds });
    points.push_back(
        { region.durationSeconds,
          region.sourceOffsetSeconds
              + std::max(0.001, region.sourceDurationSeconds) });
    std::sort(points.begin(), points.end(),
              [](const auto& left, const auto& right) {
                  return left.timeline < right.timeline;
              });
    const auto safe = juce::jlimit(0.0, region.durationSeconds,
                                   localTimelineSeconds);
    for (std::size_t index = 1; index < points.size(); ++index) {
        if (safe > points[index].timeline)
            continue;
        const auto amount = (safe - points[index - 1].timeline)
                            / std::max(0.000001,
                                points[index].timeline
                                    - points[index - 1].timeline);
        return points[index - 1].source
               + amount * (points[index].source
                           - points[index - 1].source);
    }
    return region.sourceOffsetSeconds + region.sourceDurationSeconds;
}

void WaveformComponent::drawWarpedWaveform(
    juce::Graphics& graphics,
    RegionThumbnail& regionThumbnail,
    const Region& region,
    const juce::Rectangle<int> clipBounds,
    const juce::Colour colour)
{
    auto* thumbnail = regionThumbnail.thumbnail.get();
    if (thumbnail == nullptr || clipBounds.isEmpty()
        || thumbnail->getNumChannels() <= 0)
        return;

    // A canonical warped raster can take several milliseconds to rebuild at a
    // wide zoom level. During a direct edit, reuse the last exact raster and
    // scale it as a lightweight preview. Handles and bounds therefore track the
    // pointer at display speed; mouse-up invalidates/rebuilds the exact view via
    // the committed region data.
    const auto lightweightDragPreview
        = region.id == draggedRegionId_
          && dragMode_ != DragMode::none
          && regionThumbnail.warpedWaveform.isValid();
    if (lightweightDragPreview) {
        const auto& image = regionThumbnail.warpedWaveform;
        const auto segmentedStretch
            = dragMode_ == DragMode::stretchMarker
              || dragMode_ == DragMode::stretchRight;
        if (segmentedStretch) {
            auto originalRegion = region;
            originalRegion.startSeconds = draggedRegionStartSeconds_;
            originalRegion.durationSeconds = draggedRegionDurationSeconds_;
            for (auto& marker : originalRegion.stretchMarkers)
                if (marker.id == draggedStretchMarkerId_)
                    marker.timelineSeconds
                        = draggedStretchMarkerTimelineSeconds_;
            const auto originalClipBounds = regionBounds(originalRegion)
                .getIntersection(getLocalBounds().toFloat())
                .getSmallestIntegerContainer();

            struct PreviewPoint {
                double originalTimeline {};
                double currentTimeline {};
            };
            std::vector<PreviewPoint> points;
            points.reserve(region.stretchMarkers.size() + 2);
            points.push_back({ 0.0, 0.0 });
            for (const auto& marker : region.stretchMarkers) {
                const auto originalTimeline
                    = marker.id == draggedStretchMarkerId_
                          ? draggedStretchMarkerTimelineSeconds_
                          : marker.timelineSeconds;
                points.push_back(
                    { originalTimeline, marker.timelineSeconds });
            }
            points.push_back({ draggedRegionDurationSeconds_,
                               region.durationSeconds });

            auto drewSegment = false;
            for (std::size_t index = 1; index < points.size(); ++index) {
                const auto originalLeft = xFromTime(
                    draggedRegionStartSeconds_
                    + points[index - 1].originalTimeline);
                const auto originalRight = xFromTime(
                    draggedRegionStartSeconds_
                    + points[index].originalTimeline);
                const auto currentLeft = xFromTime(
                    region.startSeconds
                    + points[index - 1].currentTimeline);
                const auto currentRight = xFromTime(
                    region.startSeconds
                    + points[index].currentTimeline);
                const auto currentWidth = currentRight - currentLeft;
                if (currentWidth <= 0.01f || originalRight <= originalLeft)
                    continue;

                const auto targetLeft = std::max(
                    currentLeft, static_cast<float>(clipBounds.getX()));
                const auto targetRight = std::min(
                    currentRight, static_cast<float>(clipBounds.getRight()));
                if (targetRight <= targetLeft)
                    continue;

                const auto startAmount
                    = (targetLeft - currentLeft) / currentWidth;
                const auto endAmount
                    = (targetRight - currentLeft) / currentWidth;
                const auto sourceLeft = originalLeft
                    + startAmount * (originalRight - originalLeft)
                    - static_cast<float>(originalClipBounds.getX());
                const auto sourceRight = originalLeft
                    + endAmount * (originalRight - originalLeft)
                    - static_cast<float>(originalClipBounds.getX());

                const auto targetX = juce::roundToInt(targetLeft);
                const auto targetEnd = juce::roundToInt(targetRight);
                const auto sourceX = juce::jlimit(
                    0, image.getWidth(), juce::roundToInt(sourceLeft));
                const auto sourceEnd = juce::jlimit(
                    0, image.getWidth(), juce::roundToInt(sourceRight));
                if (targetEnd <= targetX || sourceEnd <= sourceX)
                    continue;
                graphics.drawImage(
                    image, targetX, clipBounds.getY(),
                    targetEnd - targetX, clipBounds.getHeight(),
                    sourceX, 0, sourceEnd - sourceX, image.getHeight());
                drewSegment = true;
            }
            if (drewSegment)
                return;
        }

        graphics.drawImage(
            image, clipBounds.getX(), clipBounds.getY(),
            clipBounds.getWidth(), clipBounds.getHeight(),
            0, 0, image.getWidth(), image.getHeight());
        return;
    }

    const auto regionEnd = region.startSeconds + region.durationSeconds;
    const auto visibleTimelineStart = std::max(
        region.startSeconds, visibleStartSeconds_);
    const auto visibleTimelineEnd = std::min(regionEnd, visibleEndSeconds_);
    const auto visibleSourceStart = sourceTimeAt(
        region, visibleTimelineStart - region.startSeconds);
    const auto visibleSourceEnd = sourceTimeAt(
        region, visibleTimelineEnd - region.startSeconds);
    const auto sourceSecondsPerPixel = std::abs(
        visibleSourceEnd - visibleSourceStart)
        / static_cast<double>(std::max(1, clipBounds.getWidth()));
    const auto wantsDetail = sourceSecondsPerPixel
                             < detailThresholdSecondsPerPixel;
    auto usingDetail = false;
    if (wantsDetail && regionThumbnail.detailThumbnail == nullptr) {
        if (detailThumbnailCache_ == nullptr)
            detailThumbnailCache_
                = std::make_unique<juce::AudioThumbnailCache>(8);
        regionThumbnail.detailThumbnail
            = std::make_unique<juce::AudioThumbnail>(
                detailThumbnailResolutionSamples, formatManager_,
                *detailThumbnailCache_);
        regionThumbnail.detailThumbnail->addChangeListener(this);
        regionThumbnail.detailThumbnail->setSource(
            new juce::FileInputSource(regionThumbnail.sourceFile));
    }
    if (wantsDetail && regionThumbnail.detailThumbnail != nullptr) {
        const auto* detail = regionThumbnail.detailThumbnail.get();
        const auto sourceLength = std::max(0.000001,
                                           detail->getTotalLength());
        const auto requiredProgress = juce::jlimit(
            0.0, 1.0,
            std::max(visibleSourceStart, visibleSourceEnd) / sourceLength);
        if (detail->getNumChannels() > 0
            && detail->getProportionComplete() + 0.000001
                   >= requiredProgress) {
            thumbnail = regionThumbnail.detailThumbnail.get();
            usingDetail = true;
        }
    }

    // Every cache input is expressed in canonical time. In particular, the
    // stretch marker's screen position and the source sample represented by
    // that pixel both pass through xFromTime/timeFromX and sourceTimeAt.
    // This makes zooming a pure change of view: it cannot slide the waveform
    // underneath a marker.
    std::size_t key = 0xcbf29ce484222325ULL;
    const auto combine = [&key](const auto value) {
        key ^= std::hash<std::decay_t<decltype(value)>> {}(value)
               + 0x9e3779b97f4a7c15ULL + (key << 6U) + (key >> 2U);
    };
    combine(clipBounds.getX());
    combine(clipBounds.getWidth());
    combine(clipBounds.getHeight());
    combine(getWidth());
    combine(visibleStartSeconds_);
    combine(visibleEndSeconds_);
    combine(region.startSeconds);
    combine(region.sourceOffsetSeconds);
    combine(region.durationSeconds);
    combine(region.sourceDurationSeconds);
    combine(region.gainDb);
    combine(region.fadeInSeconds);
    combine(region.fadeOutSeconds);
    combine(colour.getARGB());
    combine(wantsDetail);
    combine(usingDetail);
    combine(static_cast<std::int64_t>(thumbnail->getNumSamplesFinished()));
    for (const auto& marker : region.stretchMarkers) {
        combine(marker.id);
        combine(marker.sourceSeconds);
        combine(marker.timelineSeconds);
    }

    if (!regionThumbnail.warpedWaveform.isValid()
        || regionThumbnail.warpedWaveformKey != key
        || regionThumbnail.warpedWaveform.getWidth()
               != clipBounds.getWidth()
        || regionThumbnail.warpedWaveform.getHeight()
               != clipBounds.getHeight()) {
        regionThumbnail.warpedWaveform = juce::Image(
            juce::Image::ARGB, clipBounds.getWidth(),
            clipBounds.getHeight(), true);
        juce::Graphics imageGraphics(regionThumbnail.warpedWaveform);
        imageGraphics.setColour(colour);

        const auto sourceLength = thumbnail->getTotalLength();
        const auto channels = thumbnail->getNumChannels();
        for (int channel = 0; channel < channels; ++channel) {
            const auto top = juce::roundToInt(
                static_cast<double>(channel * clipBounds.getHeight())
                / static_cast<double>(channels));
            const auto bottom = juce::roundToInt(
                static_cast<double>((channel + 1) * clipBounds.getHeight())
                / static_cast<double>(channels));
            const auto middle = 0.5f * static_cast<float>(top + bottom);
            const auto scale = static_cast<float>(bottom - top) * 0.5f;
            juce::RectangleList<float> waveform;
            waveform.ensureStorageAllocated(clipBounds.getWidth());

            for (int localX = 0; localX < clipBounds.getWidth();
                 ++localX) {
                const auto componentX = clipBounds.getX() + localX;
                const auto timelineStart = std::max(
                    region.startSeconds,
                    timeFromX(static_cast<float>(componentX)));
                const auto timelineEnd = std::min(
                    regionEnd,
                    timeFromX(static_cast<float>(componentX + 1)));
                if (timelineEnd <= timelineStart)
                    continue;

                auto sourceStart = juce::jlimit(
                    0.0, sourceLength,
                    sourceTimeAt(region,
                                 timelineStart - region.startSeconds));
                auto sourceEnd = juce::jlimit(
                    0.0, sourceLength,
                    sourceTimeAt(region,
                                 timelineEnd - region.startSeconds));
                if (sourceEnd < sourceStart)
                    std::swap(sourceStart, sourceEnd);
                if (sourceEnd <= sourceStart)
                    continue;

                float minimum = 0.0f;
                float maximum = 0.0f;
                thumbnail->getApproximateMinMax(
                    sourceStart, sourceEnd, channel, minimum, maximum);
                if (minimum == 0.0f && maximum == 0.0f)
                    continue;
                const auto localTimeline = 0.5
                    * (timelineStart + timelineEnd)
                    - region.startSeconds;
                auto envelope = juce::Decibels::decibelsToGain(
                    region.gainDb, -100.0f);
                if (region.fadeInSeconds > 0.0)
                    envelope *= static_cast<float>(juce::jlimit(
                        0.0, 1.0,
                        localTimeline / region.fadeInSeconds));
                if (region.fadeOutSeconds > 0.0)
                    envelope *= static_cast<float>(juce::jlimit(
                        0.0, 1.0,
                        (region.durationSeconds - localTimeline)
                            / region.fadeOutSeconds));
                minimum *= envelope;
                maximum *= envelope;
                const auto waveformTop = std::max(
                    static_cast<float>(top), middle - maximum * scale - 0.3f);
                const auto waveformBottom = std::min(
                    static_cast<float>(bottom), middle - minimum * scale + 0.3f);
                waveform.addWithoutMerging({
                    static_cast<float>(localX), waveformTop, 1.0f,
                    std::max(0.6f, waveformBottom - waveformTop) });
            }
            imageGraphics.fillRectList(waveform);
        }
        regionThumbnail.warpedWaveformKey = key;
    }

    graphics.drawImageAt(regionThumbnail.warpedWaveform,
                         clipBounds.getX(), clipBounds.getY());
}

double WaveformComponent::snappedTime(
    const double seconds,
    const bool bypassSnapping,
    const std::optional<double> relativeAnchor) const noexcept
{
    const auto safe = std::max(0.0, seconds);
    if (!snappingEnabled_ || bypassSnapping || tempoBpm_ <= 0.0
        || gridDivision_ <= 0)
        return safe;
    if (snapMode_ == SnapMode::events) {
        auto closest = safe;
        auto closestDistance = std::numeric_limits<double>::max();
        for (const auto& region : regions_) {
            if (region.id == draggedRegionId_)
                continue;
            for (const auto edge : { region.startSeconds,
                                     region.startSeconds
                                         + region.durationSeconds }) {
                const auto distance = std::abs(edge - safe);
                if (distance < closestDistance) {
                    closest = edge;
                    closestDistance = distance;
                }
            }
        }
        const auto pixelThreshold = getWidth() > 0
            ? std::max(0.001, visibleEndSeconds_ - visibleStartSeconds_)
                  / static_cast<double>(getWidth()) * 12.0
            : 0.0;
        return closestDistance <= pixelThreshold ? std::max(0.0, closest)
                                                  : safe;
    }
    const auto interval = 60.0 / tempoBpm_
                          * 4.0 / static_cast<double>(gridDivision_);
    if (interval <= 0.0)
        return safe;
    if (snapMode_ == SnapMode::relative && relativeAnchor.has_value()) {
        const auto anchor = std::max(0.0, *relativeAnchor);
        return std::max(0.0, anchor
                                 + std::round((safe - anchor) / interval)
                                       * interval);
    }
    return std::max(0.0, std::round(safe / interval) * interval);
}

WaveformComponent::DragMode WaveformComponent::dragModeAt(
    const Region& region,
    const juce::Point<float> position) const noexcept
{
    const auto bounds = regionBounds(region);
    if (!bounds.expanded(6.0f, 2.0f).contains(position))
        return DragMode::none;
    const auto duration = std::max(0.000001, region.durationSeconds);
    const auto fadeInX = bounds.getX()
                         + bounds.getWidth()
                               * static_cast<float>(juce::jlimit(
                                   0.0, 1.0,
                                   region.fadeInSeconds / duration));
    const auto fadeOutX = bounds.getRight()
                          - bounds.getWidth()
                                * static_cast<float>(juce::jlimit(
                                    0.0, 1.0,
                                    region.fadeOutSeconds / duration));
    if (position.y <= bounds.getY() + 15.0f) {
        if (std::abs(position.x - fadeInX) <= 8.0f)
            return DragMode::fadeIn;
        if (std::abs(position.x - fadeOutX) <= 8.0f)
            return DragMode::fadeOut;
    }
    // In the local clip timeline the complete lower strip performs
    // non-destructive stretching. The upper strip/body moves the event and
    // the side edges in the centre trim it.
    if (localClipEditing_
        && position.y >= bounds.getBottom() - 14.0f)
        return DragMode::stretchRight;
    // Arrangement keeps a compact lower-right stretch handle.
    if (!localClipEditing_
        && position.x >= bounds.getRight() - 18.0f
        && position.y >= bounds.getBottom() - 14.0f)
        return DragMode::stretchRight;
    const auto inTrimBand = position.y >= bounds.getY() + 16.0f
                            && position.y <= bounds.getBottom() - 14.0f;
    if (inTrimBand && std::abs(position.x - bounds.getX()) <= 7.0f)
        return DragMode::trimLeft;
    if (inTrimBand && std::abs(position.x - bounds.getRight()) <= 7.0f)
        return DragMode::trimRight;
    return bounds.contains(position) ? DragMode::move : DragMode::none;
}

void WaveformComponent::updateMouseCursor(const juce::Point<float> position)
{
    if (editingTool_ == EditingTool::hand) {
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        return;
    }
    if (editingTool_ == EditingTool::zoom
        || editingTool_ == EditingTool::knife) {
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
        return;
    }
    if (editingTool_ == EditingTool::stretch) {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        return;
    }
    const auto* region = regionAtPosition(position);
    if (region == nullptr) {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }
    const auto mode = dragModeAt(*region, position);
    if (mode == DragMode::trimLeft || mode == DragMode::trimRight
        || mode == DragMode::stretchRight)
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    else if (mode == DragMode::fadeIn || mode == DragMode::fadeOut)
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    else if (mode == DragMode::move)
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    else
        setMouseCursor(juce::MouseCursor::NormalCursor);
}

void WaveformComponent::showRegionMenu(const std::uint64_t regionId,
                                       const double timelineSeconds)
{
    juce::PopupMenu menu;
    menu.addSectionHeader(tr("audioClip.menu"));
    menu.addItem(1, tr("audioClip.copy"));
    menu.addItem(2, tr("audioClip.cut"));
    menu.addItem(3, tr("audioClip.pasteHere"));
    menu.addSeparator();
    menu.addItem(4, tr("audioClip.duplicate"));
    menu.addItem(5, tr("audioClip.split"));
    menu.addItem(6, tr("audioClip.crossfade"));
    menu.addItem(7, tr("loop.setToSelection"));
    menu.addSeparator();
    menu.addItem(8, tr("audioClip.delete"));
    const juce::Component::SafePointer<WaveformComponent> safeThis(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(this),
        [safeThis, regionId, timelineSeconds](const int result) {
            auto* component = safeThis.getComponent();
            if (component == nullptr || result == 0
                || !component->regionCommandHandler_)
                return;
            const auto command = result == 1 ? RegionCommand::copy
                                 : result == 2 ? RegionCommand::cut
                                 : result == 3 ? RegionCommand::paste
                                 : result == 4 ? RegionCommand::duplicate
                                 : result == 5 ? RegionCommand::split
                                 : result == 6 ? RegionCommand::crossfade
                                 : result == 7 ? RegionCommand::loopSelection
                                               : RegionCommand::remove;
            component->regionCommandHandler_(regionId, command,
                                              component->snappedTime(
                                                  timelineSeconds));
        });
}

} // namespace cydaw::app
