#pragma once

#include "AutomationEditorComponent.h"
#include "WaveformComponent.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <cstddef>
#include <cstdint>
#include <array>
#include <functional>
#include <optional>
#include <vector>

namespace cydaw::app {

class TrackLaneComponent final : public juce::Component,
                                 public juce::FileDragAndDropTarget,
                                 public juce::DragAndDropTarget,
                                 public juce::SettableTooltipClient {
public:
    TrackLaneComponent(juce::AudioFormatManager& formatManager, juce::AudioThumbnailCache& cache);

    void configure(std::size_t trackIndex, const juce::File& file,
                   juce::String displayName, juce::Colour colour,
                   bool muted, bool solo, bool armed);
    void setPresentation(juce::String displayName, juce::Colour colour);
    void setSelected(bool selected);
    void setPlayhead(double seconds);
    // Where a timeline second falls across this lane, in the lane's own
    // coordinates, or -1 when it is out of view. The host's one playhead
    // is placed from it.
    [[nodiscard]] int playheadXForTime(const double seconds) const noexcept
    {
        const auto x = waveform_.playheadXForTime(seconds);
        return x < 0 ? -1 : waveform_.getX() + x;
    }
    void setTimelineRange(double timelineLengthSeconds,
                          double clipStartSeconds,
                          double sourceOffsetSeconds,
                          double clipDurationSeconds);
    void setVisibleRange(double startSeconds, double endSeconds);
    void setTimeSelection(double startSeconds, double endSeconds,
                          bool trackIncluded, bool visible);
    void setTimelineRegions(
        double timelineLengthSeconds,
        std::vector<WaveformComponent::Region> regions,
        std::vector<std::uint64_t> selectedRegionIds,
        std::uint64_t primaryRegionId);
    void setMusicalGrid(double tempoBpm, int division,
                        bool snappingEnabled);
    void setEditingTool(WaveformComponent::EditingTool tool);
    void setSnapMode(WaveformComponent::SnapMode mode);
    void setMixerState(bool muted, bool solo, bool armed);
    // The two level bars, in local coordinates. Shared by the paint and the
    // invalidation so a meter tick cannot repaint anything wider than itself.
    [[nodiscard]] juce::Rectangle<int> meterBounds() const noexcept;
    void setMeterLevels(const std::array<float, 2>& peak,
                        const std::array<float, 2>& rms);
    void setMovePreview(std::optional<WaveformComponent::Region> preview);
    void setAutomationPoints(
        std::vector<AutomationEditorComponent::Point> points,
        double durationSeconds, double visibleStartSeconds,
        double visibleEndSeconds);
    void setAutomationVisible(bool visible);
    [[nodiscard]] bool isAutomationVisible() const noexcept;
    void setTrackDropTarget(bool target);
    void setArmEnabled(bool enabled);
    void refreshTranslations();

    void setSelectionHandler(std::function<void(std::size_t)> handler);
    void setSeekHandler(std::function<void(std::size_t, double)> handler);
    void setMuteHandler(std::function<void(std::size_t, bool)> handler);
    void setSoloHandler(std::function<void(std::size_t, bool)> handler);
    void setArmHandler(std::function<void(std::size_t, bool)> handler);
    void setRegionSelectionHandler(
        std::function<void(std::size_t, std::uint64_t,
                           WaveformComponent::RegionSelectionMode)> handler);
    void setRegionMoveHandler(
        std::function<void(std::size_t, std::uint64_t, double, int)> handler);
    void setRegionDragPreviewHandler(
        std::function<void(std::size_t, std::uint64_t, double, int, bool)>
            handler);
    void setRegionEditHandler(
        std::function<void(std::size_t, std::uint64_t, double, double,
                           double, double, double, double)> handler);
    void setRegionCommandHandler(
        std::function<void(std::size_t, std::uint64_t,
                           WaveformComponent::RegionCommand, double)> handler);
    void setRegionOpenHandler(
        std::function<void(std::size_t, std::uint64_t)> handler);
    void setAudioDropHandler(
        std::function<void(std::size_t, const juce::File&, double)> handler);
    void setZoomHandler(
        std::function<void(double, double)> handler);
    void setPanHandler(std::function<void(double)> handler);
    void setVerticalPanHandler(std::function<void(int)> handler);
    void setMarqueeHandler(
        std::function<void(std::size_t, double, double, int,
                           WaveformComponent::MarqueePhase, bool)> handler);
    void setContextMenuHandler(
        std::function<void(std::size_t, juce::Component*)> handler);
    void setAutomationAddHandler(
        std::function<void(std::size_t, double, float)> handler);
    void setAutomationUpdateHandler(
        std::function<void(std::size_t, std::uint64_t, double, float)> handler);
    void setAutomationRemoveHandler(
        std::function<void(std::size_t, std::uint64_t)> handler);
    void setAutomationVisibilityHandler(
        std::function<void(std::size_t, bool)> handler);
    void setLaneHeightHandler(
        std::function<void(std::size_t, int)> handler);
    void setTrackReorderHandler(
        std::function<void(std::size_t, int, bool)> handler);

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    // A sample dragged from the explorer or the browser lands like a file.
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragEnter(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;
    void fileDragEnter(const juce::StringArray&, int, int) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    void select();

    WaveformComponent waveform_;
    AutomationEditorComponent automationEditor_;
    juce::Label nameLabel_;
    juce::ToggleButton muteButton_;
    juce::ToggleButton soloButton_;
    juce::ToggleButton armButton_;
    juce::ToggleButton automationButton_;
    juce::File file_;
    juce::String displayName_;
    juce::Colour accentColour_ { 0xffff9b2f };
    std::size_t trackIndex_ {};
    double timelineLengthSeconds_ { 1.0 };
    double visibleStartSeconds_ {};
    double visibleEndSeconds_ { 1.0 };
    bool selected_ {};
    bool automationVisible_ {};
    bool audioDragHover_ {};
    bool moveTargetPreview_ {};
    bool trackDropTarget_ {};
    bool resizingLane_ {};
    bool reorderingTrack_ {};
    int resizeStartHeight_ {};
    int dragStartScreenY_ {};
    std::array<float, 2> meterPeak_ {};
    std::array<float, 2> meterRms_ {};

    std::function<void(std::size_t)> selectionHandler_;
    std::function<void(std::size_t, double)> seekHandler_;
    std::function<void(std::size_t, bool)> muteHandler_;
    std::function<void(std::size_t, bool)> soloHandler_;
    std::function<void(std::size_t, bool)> armHandler_;
    std::function<void(std::size_t, std::uint64_t,
                       WaveformComponent::RegionSelectionMode)>
        regionSelectionHandler_;
    std::function<void(std::size_t, std::uint64_t, double, int)>
        regionMoveHandler_;
    std::function<void(std::size_t, std::uint64_t, double, int, bool)>
        regionDragPreviewHandler_;
    std::function<void(std::size_t, std::uint64_t, double, double,
                       double, double, double, double)> regionEditHandler_;
    std::function<void(std::size_t, std::uint64_t,
                       WaveformComponent::RegionCommand, double)>
        regionCommandHandler_;
    std::function<void(std::size_t, std::uint64_t)> regionOpenHandler_;
    std::function<void(std::size_t, const juce::File&, double)>
        audioDropHandler_;
    std::function<void(double, double)> zoomHandler_;
    std::function<void(double)> panHandler_;
    std::function<void(int)> verticalPanHandler_;
    std::function<void(std::size_t, double, double, int,
                       WaveformComponent::MarqueePhase, bool)>
        marqueeHandler_;
    std::function<void(std::size_t, juce::Component*)>
        contextMenuHandler_;
    std::function<void(std::size_t, double, float)> automationAddHandler_;
    std::function<void(std::size_t, std::uint64_t, double, float)>
        automationUpdateHandler_;
    std::function<void(std::size_t, std::uint64_t)>
        automationRemoveHandler_;
    std::function<void(std::size_t, bool)>
        automationVisibilityHandler_;
    std::function<void(std::size_t, int)> laneHeightHandler_;
    std::function<void(std::size_t, int, bool)> trackReorderHandler_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackLaneComponent)
};

} // namespace cydaw::app
