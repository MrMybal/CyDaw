#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace cydaw::app {

class ClipLauncherComponent final : public juce::Component,
                                    public juce::SettableTooltipClient,
                                    public juce::DragAndDropContainer,
                                    public juce::DragAndDropTarget,
                                    public juce::FileDragAndDropTarget {
public:
    ClipLauncherComponent();
    static constexpr int sceneCount = 8;
    [[nodiscard]] static bool selfTestInputRouting();

    struct Slot {
        juce::String name;
        std::uint32_t colour { 0xff4fd3ae };
        bool occupied {};
        bool active {};
        bool queued {};
        bool recordPending {};
        bool recording {};
        bool recordStopPending {};
        bool looping { true };
        bool stopQueued {};
        float playbackProgress {};
    };

    struct Track {
        juce::String name;
        bool midi {};
        std::array<Slot, sceneCount> slots;
        std::vector<juce::String> devices;
        float gainDb {};
        float pan {};
        bool muted {};
        bool solo {};
        bool armed {};
        bool selected {};
        int latencySamples {};
        std::array<float, 2> peak {};
        std::array<float, 2> rms {};
    };

    void setTracks(std::vector<Track> tracks);
    void setTrackMeterLevels(std::size_t trackIndex,
                             const std::array<float, 2>& peak,
                             const std::array<float, 2>& rms);
    void setMasterMeterLevels(const std::array<float, 2>& peak,
                              const std::array<float, 2>& rms);
    // Whether a meter reading differs from the one shown enough to draw.
    [[nodiscard]] static bool meterMoved(
        const std::array<float, 2>& shown,
        const std::array<float, 2>& next) noexcept;
    void setMasterGain(float gainDb);
    void setTrackPlaybackState(std::size_t trackIndex,
                               int activeScene,
                               int queuedScene,
                               float playbackProgress = 0.0f);
    void refreshTranslations();
    void setCaptureHandler(std::function<void(std::size_t, int)> handler);
    void setLaunchHandler(std::function<void(std::size_t, int)> handler);
    void setClearHandler(std::function<void(std::size_t, int)> handler);
    void setSlotOpenHandler(std::function<void(std::size_t, int)> handler);
    void setEmptySlotCreateHandler(
        std::function<void(std::size_t, int)> handler);
    void setSlotLoopHandler(
        std::function<void(std::size_t, int, bool)> handler);
    void setLaunchSceneHandler(std::function<void(int)> handler);
    void setQuantization(int quantizationIndex);
    void setQuantizationHandler(std::function<void(int)> handler);
    void setTrackSelectHandler(std::function<void(std::size_t)> handler);
    void setSlotSelectHandler(
        std::function<void(std::size_t, int)> handler);
    void setTrackStopHandler(std::function<void(std::size_t)> handler);
    void setTrackMoveHandler(
        std::function<void(std::size_t, std::size_t)> handler);
    void setAddTrackHandler(std::function<void()> handler);
    void setTrackContextHandler(
        std::function<void(std::size_t, juce::Component*)> handler);
    void setDeviceAddHandler(std::function<void(std::size_t)> handler);
    void setSampleDropHandler(
        std::function<void(std::size_t, int, const juce::File&)> handler);
    void setSlotBrowseHandler(
        std::function<void(std::size_t, int)> handler);
    void setClipTransferHandler(
        std::function<void(std::size_t, int, std::size_t, int, bool)>
            handler);
    void setDeviceDropHandler(
        std::function<void(std::size_t, const juce::var&)> handler);
    void setMixerHandler(
        std::function<void(std::size_t, float, float)> handler);
    void setMuteHandler(std::function<void(std::size_t, bool)> handler);
    void setSoloHandler(std::function<void(std::size_t, bool)> handler);
    void setArmHandler(std::function<void(std::size_t, bool)> handler);
    void setMasterMixerHandler(std::function<void(float)> handler);

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent&) override;
    bool isInterestedInDragSource(
        const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDragEnter(
        const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDragMove(
        const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDragExit(
        const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDropped(
        const juce::DragAndDropTarget::SourceDetails& details) override;
    bool isInterestedInFileDrag(
        const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files,
                       int x, int y) override;
    void fileDragMove(const juce::StringArray& files,
                      int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files,
                      int x, int y) override;

private:
    enum class HitKind {
        none,
        scene,
        slot,
        slotPlay,
        slotRecord,
        trackHeader,
        addTrack,
        addDevice,
        stopTrack,
        gain,
        masterGain,
        pan,
        mute,
        solo,
        arm
    };
    struct Hit {
        std::optional<std::size_t> trackIndex;
        int sceneIndex { -1 };
        HitKind kind { HitKind::none };
    };

    [[nodiscard]] Hit hitAt(juce::Point<int> position) const;
    [[nodiscard]] juce::Rectangle<int> sceneBounds(int sceneIndex) const;
    [[nodiscard]] juce::Rectangle<int> slotBounds(std::size_t trackIndex,
                                                  int sceneIndex) const;
    [[nodiscard]] juce::Rectangle<int> slotTriggerBounds(
        std::size_t trackIndex, int sceneIndex) const;
    [[nodiscard]] juce::Rectangle<int> trackBounds(std::size_t trackIndex) const;
    [[nodiscard]] juce::Rectangle<int> masterBounds() const;
    [[nodiscard]] juce::Rectangle<int> addTrackBounds() const;
    [[nodiscard]] juce::Rectangle<int> deviceBounds(std::size_t trackIndex) const;
    [[nodiscard]] juce::Rectangle<int> addDeviceBounds(std::size_t trackIndex) const;
    [[nodiscard]] juce::Rectangle<int> ioBounds(std::size_t trackIndex) const;
    [[nodiscard]] juce::Rectangle<int> stopTrackBounds(
        std::size_t trackIndex) const;
    [[nodiscard]] juce::Rectangle<int> mixerBounds(std::size_t trackIndex) const;
    [[nodiscard]] juce::Rectangle<int> gainBounds(std::size_t trackIndex) const;
    [[nodiscard]] juce::Rectangle<int> gainValueBounds(
        std::size_t trackIndex) const;
    [[nodiscard]] juce::Rectangle<int> meterBounds(
        std::size_t trackIndex) const;
    [[nodiscard]] juce::Rectangle<int> panBounds(std::size_t trackIndex) const;
    [[nodiscard]] std::array<juce::Rectangle<int>, 3> mixerButtonBounds(
        std::size_t trackIndex) const;
    [[nodiscard]] int sceneRowHeight() const;
    [[nodiscard]] int clipAreaBottom() const;
    [[nodiscard]] int trackColumnWidth() const;
    void updateMixerDrag(const juce::MouseEvent& event);
    void updateSampleDropTarget(juce::Point<int> position);
    void updateDeviceDropTarget(juce::Point<int> position);
    void clearSampleDropTarget();
    void dropSample(const juce::File& file);
    void dropLauncherClip(const juce::var& payload);
    void beginGainValueEdit(std::size_t trackIndex);
    void finishGainValueEdit(bool applyValue);

    std::vector<Track> tracks_;
    std::array<float, 2> masterPeak_ {};
    std::array<float, 2> masterRms_ {};
    float masterGainDb_ {};
    Hit hovered_;
    Hit dragHit_;
    Hit sampleDropTarget_;
    bool launcherClipDropTarget_ {};
    std::optional<std::size_t> deviceDropTarget_;
    std::optional<std::size_t> trackDragTarget_;
    juce::String emptyText_;
    juce::ComboBox quantizationSelector_;
    juce::TextEditor gainValueEditor_;
    std::optional<std::size_t> editedGainTrack_;
    std::function<void(std::size_t, int)> captureHandler_;
    std::function<void(std::size_t, int)> launchHandler_;
    std::function<void(std::size_t, int)> clearHandler_;
    std::function<void(std::size_t, int)> slotOpenHandler_;
    std::function<void(std::size_t, int)> emptySlotCreateHandler_;
    std::function<void(std::size_t, int, bool)> slotLoopHandler_;
    std::function<void(int)> launchSceneHandler_;
    std::function<void(int)> quantizationHandler_;
    std::function<void(std::size_t)> trackSelectHandler_;
    std::function<void(std::size_t, int)> slotSelectHandler_;
    std::function<void(std::size_t)> trackStopHandler_;
    std::function<void(std::size_t, std::size_t)> trackMoveHandler_;
    std::function<void()> addTrackHandler_;
    std::function<void(std::size_t, juce::Component*)>
        trackContextHandler_;
    std::function<void(std::size_t)> deviceAddHandler_;
    std::function<void(std::size_t, int, const juce::File&)>
        sampleDropHandler_;
    std::function<void(std::size_t, int)> slotBrowseHandler_;
    std::function<void(std::size_t, int, std::size_t, int, bool)>
        clipTransferHandler_;
    std::function<void(std::size_t, const juce::var&)>
        deviceDropHandler_;
    std::function<void(std::size_t, float, float)> mixerHandler_;
    std::function<void(std::size_t, bool)> muteHandler_;
    std::function<void(std::size_t, bool)> soloHandler_;
    std::function<void(std::size_t, bool)> armHandler_;
    std::function<void(float)> masterMixerHandler_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipLauncherComponent)
};

} // namespace cydaw::app
