#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace cydaw::app {

class WaveformComponent final : public juce::Component,
                                public juce::SettableTooltipClient,
                                private juce::ChangeListener {
public:
    struct Region {
        struct StretchMarker {
            std::uint64_t id {};
            double sourceSeconds {};
            double timelineSeconds {};
        };

        std::uint64_t id {};
        juce::String name;
        double startSeconds {};
        double sourceOffsetSeconds {};
        double durationSeconds {};
        double sourceDurationSeconds {};
        juce::Colour colour { 0xffff9b2f };
        juce::File sourceFile;
        double sourceLengthSeconds {};
        double fadeInSeconds {};
        double fadeOutSeconds {};
        std::size_t activeTakeIndex {};
        std::size_t takeCount { 1 };
        std::vector<StretchMarker> stretchMarkers;
        // Used by the audio-clip editor. Arrangement regions keep the
        // defaults and therefore continue to occupy the complete track row.
        int laneIndex {};
        int laneCount { 1 };
        float gainDb {};
    };

    enum class RegionCommand {
        copy,
        cut,
        paste,
        loopSelection,
        duplicate,
        split,
        crossfade,
        remove
    };
    enum class RegionSelectionMode { replace, toggle, makePrimary };
    enum class MarqueePhase { begin, update, end };
    enum class EditingTool { pointer, knife, stretch, zoom, hand };
    enum class SnapMode { grid, relative, events };

    WaveformComponent(juce::AudioFormatManager& formatManager, juce::AudioThumbnailCache& cache);
    ~WaveformComponent() override;

    void setFile(const juce::File& file);
    void clear();
    void setPlayhead(double seconds);
    // The host draws the playhead itself, one line over every lane: this
    // lane keeps the time and paints nothing for it.
    void setPlayheadDrawnByHost(bool drawnByHost);
    // Where a timeline second falls on this lane, or -1 when out of view.
    [[nodiscard]] int playheadXForTime(double seconds) const noexcept;
    void setTimelineRange(double timelineLengthSeconds,
                          double clipStartSeconds,
                          double sourceOffsetSeconds,
                          double clipDurationSeconds);
    void setVisibleRange(double startSeconds, double endSeconds);
    void setTimeSelection(double startSeconds, double endSeconds,
                          bool trackIncluded, bool visible);
    void setRegions(double timelineLengthSeconds,
                    std::vector<Region> regions,
                    std::vector<std::uint64_t> selectedRegionIds,
                    std::uint64_t primaryRegionId);
    void setMovePreview(std::optional<Region> preview);
    void setMusicalGrid(double tempoBpm, int division,
                        bool snappingEnabled);
    void setEditingTool(EditingTool tool);
    void setSnapMode(SnapMode mode);
    // Arrangement clips open their local editor on double-click. Inside the
    // local editor the same gesture becomes zone-sensitive: the title strip
    // splits and the waveform body creates a warp anchor.
    void setLocalClipEditing(bool enabled);
    void setAccentColour(juce::Colour colour);
    void setSeekHandler(std::function<void(double)> handler);
    void setRegionSelectionHandler(
        std::function<void(std::uint64_t, RegionSelectionMode)> handler);
    void setRegionMoveHandler(
        std::function<void(std::uint64_t, double, int)> handler);
    void setRegionEditHandler(
        std::function<void(std::uint64_t, double, double, double,
                           double, double, double)> handler);
    void setRegionLiveEditHandler(
        std::function<void(std::uint64_t, double, double, double,
                           double, double, double)> handler);
    void setRegionDragPreviewHandler(
        std::function<void(std::uint64_t, double, int, bool)> handler);
    void setRegionCommandHandler(
        std::function<void(std::uint64_t, RegionCommand, double)> handler);
    void setRegionOpenHandler(
        std::function<void(std::uint64_t)> handler);
    void setStretchMarkerAddHandler(
        std::function<void(std::uint64_t, double, double)> handler);
    void setStretchMarkerUpdateHandler(
        std::function<void(std::uint64_t, std::uint64_t, double)> handler);
    void setStretchMarkerLiveUpdateHandler(
        std::function<void(std::uint64_t, std::uint64_t, double)> handler);
    void setStretchMarkerRemoveHandler(
        std::function<void(std::uint64_t, std::uint64_t)> handler);
    void setZoomHandler(
        std::function<void(double, double)> handler);
    void setPanHandler(std::function<void(double)> handler);
    void setVerticalPanHandler(std::function<void(int)> handler);
    void setMarqueeHandler(
        std::function<void(double, double, int, MarqueePhase, bool)> handler);
    void setEditGestureHandlers(std::function<void()> beginHandler,
                                std::function<void()> endHandler);

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override;

private:
    // A two-pixel line that moves over the lane. Keeping it in its own
    // component means the transport can advance without the waveform, the
    // musical grid and every clip being re-rendered behind it.
    // The playhead is painted by the lane itself. A child moved across it
    // asked JUCE for the region it left and the one it entered, two per lane
    // per tick, which JUCE keeps apart; one strip from the old position to
    // the new is one region.
    [[nodiscard]] juce::Rectangle<int> playheadStrip(double seconds) const;
    void updatePlayheadOverlay();

    enum class DragMode {
        none, move, trimLeft, trimRight, stretchRight, fadeIn, fadeOut,
        stretchMarker
    };

    struct RegionThumbnail {
        std::uint64_t id {};
        juce::File sourceFile;
        std::unique_ptr<juce::AudioThumbnail> thumbnail;
        // Created lazily when a stretched event is magnified far enough for
        // the normal overview thumbnail to become visible as coarse blocks.
        // It uses an independent JUCE cache because AudioThumbnailCache keys
        // entries by source only and would otherwise reload the 64-sample
        // overview data into this high-detail thumbnail.
        std::unique_ptr<juce::AudioThumbnail> detailThumbnail;
        // Warped clips cannot use AudioThumbnail::drawChannels segment by
        // segment: that API owns a single internal pixel cache and the
        // successive segments can be sampled against a different window
        // while zooming. Keep a raster generated from the canonical
        // timeline->source transform instead.
        juce::Image warpedWaveform;
        std::size_t warpedWaveformKey {};
    };

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void seekFromX(float x, bool bypassSnapping = false);
    [[nodiscard]] double timeFromX(float x) const noexcept;
    [[nodiscard]] float xFromTime(double seconds) const noexcept;
    [[nodiscard]] double visibleDuration() const noexcept;
    [[nodiscard]] Region* regionAtTime(double seconds) noexcept;
    [[nodiscard]] const Region* regionAtTime(double seconds) const noexcept;
    [[nodiscard]] Region* regionAtPosition(
        juce::Point<float> position) noexcept;
    [[nodiscard]] const Region* regionAtPosition(
        juce::Point<float> position) const noexcept;
    [[nodiscard]] juce::Rectangle<float> regionBounds(
        const Region& region) const noexcept;
    [[nodiscard]] bool isRegionSelected(
        std::uint64_t regionId) const noexcept;
    [[nodiscard]] double snappedTime(double seconds,
                                     bool bypassSnapping = false,
                                     std::optional<double> relativeAnchor = {}) const noexcept;
    [[nodiscard]] DragMode dragModeAt(const Region& region,
                                      juce::Point<float> position) const noexcept;
    void updateMouseCursor(juce::Point<float> position);
    void showRegionMenu(std::uint64_t regionId, double timelineSeconds);
    [[nodiscard]] juce::AudioThumbnail* thumbnailForRegion(
        std::uint64_t regionId) const noexcept;
    [[nodiscard]] const Region::StretchMarker* stretchMarkerAt(
        const Region& region, juce::Point<float> position) const noexcept;
    [[nodiscard]] double sourceTimeAt(
        const Region& region, double localTimelineSeconds) const noexcept;
    void drawWarpedWaveform(juce::Graphics& graphics,
                            RegionThumbnail& regionThumbnail,
                            const Region& region,
                            juce::Rectangle<int> clipBounds,
                            juce::Colour colour);
    void clearRegionThumbnails();

    juce::AudioFormatManager& formatManager_;
    juce::AudioThumbnailCache& thumbnailCache_;
    // Made when the first detail thumbnail is: a cache owns a thread, and
    // one per lane was eight threads started at every project load for
    // clips that are rarely magnified that far.
    std::unique_ptr<juce::AudioThumbnailCache> detailThumbnailCache_;
    juce::AudioThumbnail thumbnail_;
    std::vector<RegionThumbnail> regionThumbnails_;
    double playheadSeconds_ {};
    bool playheadDrawnByHost_ {};
    double timelineLengthSeconds_ {};
    double visibleStartSeconds_ {};
    double visibleEndSeconds_ { 1.0 };
    double clipStartSeconds_ {};
    double sourceOffsetSeconds_ {};
    double clipDurationSeconds_ {};
    std::vector<Region> regions_;
    std::optional<Region> movePreview_;
    std::vector<std::uint64_t> selectedRegionIds_;
    std::uint64_t selectedRegionId_ {};
    std::uint64_t draggedRegionId_ {};
    std::vector<std::pair<std::uint64_t, double>> draggedRegionStarts_;
    DragMode dragMode_ { DragMode::none };
    float dragStartX_ {};
    double draggedRegionStartSeconds_ {};
    double draggedRegionOffsetSeconds_ {};
    double draggedRegionDurationSeconds_ {};
    double draggedRegionSourceDurationSeconds_ {};
    double draggedRegionFadeInSeconds_ {};
    double draggedRegionFadeOutSeconds_ {};
    std::uint64_t draggedStretchMarkerId_ {};
    double draggedStretchMarkerTimelineSeconds_ {};
    double tempoBpm_ { 120.0 };
    int gridDivision_ { 16 };
    bool snappingEnabled_ { true };
    EditingTool editingTool_ { EditingTool::pointer };
    SnapMode snapMode_ { SnapMode::grid };
    bool localClipEditing_ {};
    bool arrangementDoubleClickHandled_ {};
    bool marqueeDragging_ {};
    bool panning_ {};
    bool zooming_ {};
    bool zoomGestureMoved_ {};
    float panDragLastX_ {};
    int panDragLastScreenY_ {};
    float zoomDragLastX_ {};
    int zoomDragLastScreenY_ {};
    double zoomAnchorSeconds_ {};
    bool timeSelectionVisible_ {};
    bool timeSelectionTrackIncluded_ {};
    double marqueeAnchorSeconds_ {};
    double timeSelectionStartSeconds_ {};
    double timeSelectionEndSeconds_ {};
    juce::Colour accentColour_ { 0xffff9b2f };
    std::function<void(double)> seekHandler_;
    std::function<void(std::uint64_t, RegionSelectionMode)>
        regionSelectionHandler_;
    std::function<void(std::uint64_t, double, int)> regionMoveHandler_;
    std::function<void(std::uint64_t, double, double, double,
                       double, double, double)> regionEditHandler_;
    std::function<void(std::uint64_t, double, double, double,
                       double, double, double)> regionLiveEditHandler_;
    std::function<void(std::uint64_t, double, int, bool)>
        regionDragPreviewHandler_;
    std::function<void(std::uint64_t, RegionCommand, double)>
        regionCommandHandler_;
    std::function<void(std::uint64_t)> regionOpenHandler_;
    std::function<void(std::uint64_t, double, double)>
        stretchMarkerAddHandler_;
    std::function<void(std::uint64_t, std::uint64_t, double)>
        stretchMarkerUpdateHandler_;
    std::function<void(std::uint64_t, std::uint64_t, double)>
        stretchMarkerLiveUpdateHandler_;
    std::function<void(std::uint64_t, std::uint64_t)>
        stretchMarkerRemoveHandler_;
    std::function<void(double, double)> zoomHandler_;
    std::function<void(double)> panHandler_;
    std::function<void(int)> verticalPanHandler_;
    std::function<void(double, double, int, MarqueePhase, bool)>
        marqueeHandler_;
    std::function<void()> editGestureBeginHandler_;
    std::function<void()> editGestureEndHandler_;
};

} // namespace cydaw::app
