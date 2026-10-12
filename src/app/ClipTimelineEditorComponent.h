#pragma once

#include "AudioTrackController.h"
#include "WaveformComponent.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace cydaw::app {

class ClipTimelineEditorComponent final : public juce::Component,
                                          public juce::FileDragAndDropTarget {
public:
    enum class TransportCommand { play, stop, returnToStart };

    ClipTimelineEditorComponent(juce::AudioFormatManager& formatManager,
                                juce::AudioThumbnailCache& cache);

    void setClip(juce::String name, double durationSeconds,
                 std::vector<AudioTrackController::AudioEvent> events,
                 double tempoBpm, int gridDivision, bool snapEnabled,
                 double playStartSeconds, double playEndSeconds,
                 double loopStartSeconds, double loopEndSeconds,
                 bool looping);
    void clear();
    void setPlayhead(double localSeconds);
    void setTransportState(bool playing);
    void triggerPlayPause();
    [[nodiscard]] double visibleStartSeconds() const noexcept;
    [[nodiscard]] double visibleEndSeconds() const noexcept;
    [[nodiscard]] double timelineLengthSeconds() const noexcept;
    [[nodiscard]] int gridDivision() const noexcept { return gridDivision_; }
    [[nodiscard]] bool snappingEnabled() const noexcept { return snapEnabled_; }
    [[nodiscard]] double snapTime(double seconds) const noexcept;
    void refreshTranslations();

    void setSeekHandler(std::function<void(double)> handler);
    void setEventEditHandler(
        std::function<void(std::uint64_t, double, double, double, double,
                           double, double)> handler);
    void setEventLiveEditHandler(
        std::function<void(std::uint64_t, double, double, double, double,
                           double, double)> handler);
    void setEventCommandHandler(
        std::function<void(std::uint64_t, WaveformComponent::RegionCommand,
                           double)> handler);
    void setStretchMarkerAddHandler(
        std::function<void(std::uint64_t, double, double)> handler);
    void setStretchMarkerUpdateHandler(
        std::function<void(std::uint64_t, std::uint64_t, double)> handler);
    void setStretchMarkerLiveUpdateHandler(
        std::function<void(std::uint64_t, std::uint64_t, double)> handler);
    void setStretchMarkerRemoveHandler(
        std::function<void(std::uint64_t, std::uint64_t)> handler);
    void setAudioDropHandler(
        std::function<void(const juce::File&, double)> handler);
    void setOpenSourceHandler(
        std::function<void(std::uint64_t)> handler);
    void setEventSelectionHandler(
        std::function<void(std::uint64_t)> handler);
    void setTimelineBoundsHandler(
        std::function<void(double, double, double, double, double, bool)>
            handler);
    void setTransportHandler(
        std::function<void(TransportCommand)> handler);
    void setViewChangedHandler(
        std::function<void(double, double)> handler);
    void setEditGestureHandlers(std::function<void()> beginHandler,
                                std::function<void()> endHandler);

    void paint(juce::Graphics& graphics) override;
    void paintOverChildren(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override;
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    [[nodiscard]] AudioTrackController::AudioEvent* eventById(
        std::uint64_t id) noexcept;
    void rebuildRegions();
    void zoom(double factor, double anchorSeconds);
    void pan(double deltaSeconds);
    void updateSnapControls();
    void applyTimelineBoundsFromEditors();
    void refreshTimelineControls();
    void publishTimelineBounds();
    enum class BoundHandle {
        none, playStart, playEnd, loopStart, loopEnd
    };
    [[nodiscard]] BoundHandle boundHandleAt(
        juce::Point<int> position) const noexcept;
    void dragBoundHandle(BoundHandle handle, double seconds);
    [[nodiscard]] double localTimeAtX(int x) const noexcept;
    [[nodiscard]] double snappedLocalTime(double seconds) const noexcept;

    WaveformComponent waveform_;
    juce::Label titleLabel_;
    juce::Label helpLabel_;
    juce::TextButton addAudioButton_;
    juce::TextButton openSourceButton_;
    juce::TextButton playButton_;
    juce::TextButton stopButton_;
    juce::TextButton returnButton_;
    juce::ComboBox spacePlayModeSelector_;
    juce::ToggleButton loopButton_;
    juce::ComboBox snapSelector_;
    juce::ComboBox snapTypeSelector_;
    juce::Label durationLabel_;
    juce::Label startLabel_;
    juce::Label endLabel_;
    juce::Label loopStartLabel_;
    juce::Label loopEndLabel_;
    juce::TextEditor durationEditor_;
    juce::TextEditor startEditor_;
    juce::TextEditor endEditor_;
    juce::TextEditor loopStartEditor_;
    juce::TextEditor loopEndEditor_;
    std::unique_ptr<juce::FileChooser> fileChooser_;
    juce::String clipName_;
    std::vector<AudioTrackController::AudioEvent> events_;
    std::uint64_t selectedEventId_ {};
    double durationSeconds_ { 1.0 };
    double displayDurationSeconds_ { 1.0 };
    double playStartSeconds_ {};
    double playEndSeconds_ { 1.0 };
    double loopStartSeconds_ {};
    double loopEndSeconds_ { 1.0 };
    double playheadSeconds_ {};
    double visibleStartSeconds_ {};
    double visibleEndSeconds_ { 1.0 };
    double tempoBpm_ { 120.0 };
    int gridDivision_ { 16 };
    bool snapEnabled_ { true };
    juce::Rectangle<int> rulerBounds_;
    bool updatingSnapControls_ {};
    bool updatingTimelineControls_ {};
    bool previewPlaying_ {};
    bool navigatingRuler_ {};
    bool rulerGestureMoved_ {};
    BoundHandle draggedBoundHandle_ { BoundHandle::none };
    juce::Point<float> rulerDragLast_;
    double rulerAnchorSeconds_ {};

    std::function<void(double)> seekHandler_;
    std::function<void(std::uint64_t, double, double, double, double,
                       double, double)> eventEditHandler_;
    std::function<void(std::uint64_t, double, double, double, double,
                       double, double)> eventLiveEditHandler_;
    std::function<void(std::uint64_t, WaveformComponent::RegionCommand,
                       double)> eventCommandHandler_;
    std::function<void(std::uint64_t, double, double)>
        stretchMarkerAddHandler_;
    std::function<void(std::uint64_t, std::uint64_t, double)>
        stretchMarkerUpdateHandler_;
    std::function<void(std::uint64_t, std::uint64_t, double)>
        stretchMarkerLiveUpdateHandler_;
    std::function<void(std::uint64_t, std::uint64_t)>
        stretchMarkerRemoveHandler_;
    std::function<void(const juce::File&, double)> audioDropHandler_;
    std::function<void(std::uint64_t)> openSourceHandler_;
    std::function<void(std::uint64_t)> eventSelectionHandler_;
    std::function<void(double, double, double, double, double, bool)>
        timelineBoundsHandler_;
    std::function<void(TransportCommand)> transportHandler_;
    std::function<void(double, double)> viewChangedHandler_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(
        ClipTimelineEditorComponent)
};

} // namespace cydaw::app
