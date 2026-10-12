#pragma once

#include "AudioEdit.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <cstdint>
#include <optional>
#include <vector>

namespace cydaw::app {

class AudioEditorComponent final : public juce::Component,
                                   private juce::ChangeListener {
public:
    struct StretchMarker {
        std::uint64_t id {};
        double sourceSeconds {};
        double timelineSeconds {};
    };

    AudioEditorComponent(juce::AudioFormatManager& formatManager,
                         juce::AudioThumbnailCache& cache);

    void setTrack(const juce::File& file,
                  double sourceLengthSeconds,
                  double clipStartSeconds,
                  double sourceOffsetSeconds,
                  double clipDurationSeconds,
                  double clipSourceDurationSeconds = -1.0);
    // An empty editor says which of the two things is missing: a track,
    // or a region on the track that is already selected.
    void clear(bool trackSelected = false);
    void setPlayhead(double timelineSeconds);
    void setStretchMarkers(std::vector<StretchMarker> markers,
                           double tempoBpm, int gridDivision);
    void showStatus(juce::String message);
    void refreshTranslations();

    void setSeekHandler(std::function<void(double)> handler);
    void setTrimHandler(std::function<void(double, double)> handler);
    void setResetHandler(std::function<void()> handler);
    void setExportHandler(std::function<void(double, double)> handler);
    void setProcessHandler(
        std::function<void(AudioEditRequest, double, double)> handler);
    void setUndoHandler(std::function<void()> handler);
    void setRedoHandler(std::function<void()> handler);
    void setCancelHandler(std::function<void()> handler);
    void setStretchMarkerAddHandler(
        std::function<void(double, double)> handler);
    void setStretchMarkerUpdateHandler(
        std::function<void(std::uint64_t, double)> handler);
    void setStretchMarkerRemoveHandler(
        std::function<void(std::uint64_t)> handler);
    void setStretchMarkerSnapHandler(
        std::function<double(double)> handler);
    void setHistoryState(bool canUndo, bool canRedo);
    void setBusy(bool busy, juce::String status = {});
    [[nodiscard]] bool processSelection(AudioEditRequest request);
    [[nodiscard]] bool applyCurrentGain();

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    [[nodiscard]] juce::Rectangle<int> waveformBounds() const;
    [[nodiscard]] juce::Rectangle<int> rulerBounds() const;
    [[nodiscard]] double sourceTimeAt(float x) const noexcept;
    [[nodiscard]] float xAtSourceTime(double seconds) const noexcept;
    [[nodiscard]] float xAtTimelineTime(double seconds) const noexcept;
    [[nodiscard]] double timelineTimeAt(float x) const noexcept;
    [[nodiscard]] const StretchMarker* stretchMarkerAt(float x) const;
    void updateSelection(double first, double second);
    void updateStatus();
    void zoomBy(double factor, double centreSeconds);
    void fitSelection();
    void updateButtonState();

    juce::AudioThumbnail thumbnail_;
    bool trackSelectedWhenEmpty_ {};
    juce::File file_;
    double sourceLengthSeconds_ {};
    double clipStartSeconds_ {};
    double clipSourceOffsetSeconds_ {};
    double clipDurationSeconds_ {};
    double clipSourceDurationSeconds_ {};
    double playheadTimelineSeconds_ {};
    double selectionStartSeconds_ {};
    double selectionEndSeconds_ {};
    double selectionAnchorSeconds_ {};
    double viewStartSeconds_ {};
    double viewEndSeconds_ { 1.0 };
    bool draggingSelection_ {};
    bool navigatingRuler_ {};
    bool rulerGestureMoved_ {};
    juce::Point<float> rulerDragLast_;
    double rulerAnchorSeconds_ {};
    std::vector<StretchMarker> stretchMarkers_;
    std::optional<StretchMarker> draggingStretchMarker_;
    double stretchMarkerPreviewTimeline_ {};
    double tempoBpm_ { 120.0 };
    int gridDivision_ { 16 };

    juce::TextButton zoomOutButton_;
    juce::TextButton zoomInButton_;
    juce::TextButton fitButton_;
    juce::ToggleButton warpButton_;
    juce::TextButton trimButton_;
    juce::TextButton resetButton_;
    juce::TextButton normaliseButton_;
    juce::TextButton exportButton_;
    juce::TextButton fadeInButton_;
    juce::TextButton fadeOutButton_;
    juce::TextButton silenceButton_;
    juce::TextButton reverseButton_;
    juce::TextButton removeDcButton_;
    juce::TextButton invertButton_;
    juce::TextButton swapStereoButton_;
    juce::Label gainLabel_;
    juce::Slider gainSlider_;
    juce::TextButton applyGainButton_;
    juce::TextButton undoButton_;
    juce::TextButton redoButton_;
    juce::TextButton cancelButton_;
    juce::Label statusLabel_;

    std::function<void(double)> seekHandler_;
    std::function<void(double, double)> trimHandler_;
    std::function<void()> resetHandler_;
    std::function<void(double, double)> exportHandler_;
    std::function<void(AudioEditRequest, double, double)> processHandler_;
    std::function<void()> undoHandler_;
    std::function<void()> redoHandler_;
    std::function<void()> cancelHandler_;
    std::function<void(double, double)> stretchMarkerAddHandler_;
    std::function<void(std::uint64_t, double)>
        stretchMarkerUpdateHandler_;
    std::function<void(std::uint64_t)> stretchMarkerRemoveHandler_;
    std::function<double(double)> stretchMarkerSnapHandler_;
    bool canUndo_ {};
    bool canRedo_ {};
    bool busy_ {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEditorComponent)
};

} // namespace cydaw::app
