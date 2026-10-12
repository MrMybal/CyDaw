#pragma once

#include "AudioTrackController.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace cydaw::app {

class PianoRollComponent final : public juce::Component,
                                 public juce::SettableTooltipClient {
public:
    enum class TransportCommand { play, stop, returnToStart };

    struct ContextItem {
        std::uint64_t id {};
        juce::String name;
    };

    PianoRollComponent();
    void setNotes(std::vector<AudioTrackController::MidiNote> notes);
    void setContext(std::vector<ContextItem> tracks,
                    std::uint64_t selectedTrackId,
                    std::vector<ContextItem> clips,
                    std::uint64_t selectedClipId,
                    double clipStartBeats,
                    double clipLengthBeats,
                    double playStartBeats,
                    double playEndBeats,
                    double loopStartBeats,
                    double loopEndBeats,
                    bool clipLooping,
                    float gainDb,
                    float pan,
                    bool muted,
                    bool solo);
    void setTempo(double beatsPerMinute);
    void setPlayhead(double seconds);
    void setTransportState(bool playing);
    void triggerPlayPause();
    void setRecordingState(bool recording,
                           std::size_t recordedNotes,
                           int droppedEvents);
    void setHistoryState(bool canUndo, bool canRedo);
    [[nodiscard]] bool recordOverdubEnabled() const noexcept;
    [[nodiscard]] AudioTrackController::MidiRecordQuantization
        recordQuantization() const noexcept;
    [[nodiscard]] std::size_t selectedNoteCount() const noexcept;
    [[nodiscard]] double visibleStartBeat() const noexcept;
    [[nodiscard]] double visibleEndBeat() const noexcept;
    [[nodiscard]] int gridDivision() const noexcept;
    [[nodiscard]] bool snappingEnabled() const noexcept;
    void selectAllNotes();
    [[nodiscard]] bool copySelectedNotes();
    [[nodiscard]] bool cutSelectedNotes();
    [[nodiscard]] bool pasteNotes();
    [[nodiscard]] bool duplicateSelectedNotes();
    [[nodiscard]] bool deleteSelectedNotes();
    void refreshTranslations();

    void setAddHandler(std::function<void(int, double, double, float)> handler);
    void setNotesEditHandler(
        std::function<void(std::vector<AudioTrackController::MidiNote>)>
            handler);
    void setSeekHandler(std::function<void(double)> handler);
    void setTrackSelectionHandler(std::function<void(std::uint64_t)> handler);
    void setClipSelectionHandler(std::function<void(std::uint64_t)> handler);
    void setAddTrackHandler(std::function<void()> handler);
    void setAddClipHandler(std::function<void()> handler);
    void setImportMidiHandler(std::function<void()> handler);
    void setExportMidiHandler(std::function<void()> handler);
    void setMixerHandler(std::function<void(float, float, bool, bool)> handler);
    void setUndoHandler(std::function<void()> handler);
    void setRedoHandler(std::function<void()> handler);
    void setEditGestureHandlers(std::function<void()> beginHandler,
                                std::function<void()> endHandler);
    void setViewChangedHandler(
        std::function<void(double, double)> handler);
    void setClipBoundsHandler(
        std::function<void(double, double, double, double, double, bool)>
            handler);
    void setTransportHandler(
        std::function<void(TransportCommand)> handler);

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    enum class GestureMode {
        none,
        move,
        resize,
        noteData,
        marquee
    };

    enum class NoteDataLane { velocity = 1, duration = 2 };
    enum class BoundHandle {
        none, clipLength, playStart, playEnd, loopStart, loopEnd
    };

    [[nodiscard]] juce::Rectangle<float> gridBounds() const;
    [[nodiscard]] juce::Rectangle<float> velocityBounds() const;
    [[nodiscard]] juce::Rectangle<float> noteBounds(const AudioTrackController::MidiNote& note) const;
    [[nodiscard]] const AudioTrackController::MidiNote* noteAt(juce::Point<float> point) const;
    [[nodiscard]] const AudioTrackController::MidiNote* velocityNoteAt(
        juce::Point<float> point) const;
    [[nodiscard]] bool isSelected(std::uint64_t noteId) const noexcept;
    [[nodiscard]] bool isResizeHandle(
        const AudioTrackController::MidiNote& note,
        juce::Point<float> point) const noexcept;
    void normaliseSelection();
    void selectOnly(std::uint64_t noteId);
    void toggleSelection(std::uint64_t noteId);
    void updateStatusLabel();
    void beginGesture(GestureMode mode, juce::Point<float> point);
    void finishGesture();
    [[nodiscard]] bool applyNotes(
        std::vector<AudioTrackController::MidiNote> notes);
    [[nodiscard]] bool insertClipboardAt(double destinationBeat);
    [[nodiscard]] bool nudgeSelected(double beatDelta,
                                     int noteDelta,
                                     double durationDelta,
                                     float velocityDelta);
    void showNoteMenu();
    [[nodiscard]] double beatAt(float x) const;
    [[nodiscard]] int noteNumberAt(float y) const;
    [[nodiscard]] double snapBeat(
        double beat,
        std::optional<double> relativeAnchor = {}) const;
    [[nodiscard]] double snapStepBeats() const noexcept;
    void zoomTimeline(double factor, double anchorBeat);
    void panTimeline(double deltaBeats);
    void panPitches(int noteDelta);
    void zoomPitches(int noteDelta, int anchorNote);
    void updateSnapControls();
    void applyClipBoundsFromEditors();
    void refreshClipBoundsControls();
    void normaliseClipBounds();
    void refreshContentExtent(bool preserveFullView);
    [[nodiscard]] BoundHandle boundHandleAt(
        juce::Point<float> position) const noexcept;
    void dragBoundHandle(BoundHandle handle, double beat);
    void publishClipBounds();

    std::vector<AudioTrackController::MidiNote> notes_;
    double tempoBpm_ { 120.0 };
    double playheadSeconds_ {};
    double visibleBeats_ { 16.0 };
    double visibleStartBeat_ {};
    double contentBeats_ { 16.0 };
    double clipStartBeats_ {};
    double clipLengthBeats_ { 16.0 };
    double playStartBeats_ {};
    double playEndBeats_ { 16.0 };
    double loopStartBeats_ {};
    double loopEndBeats_ { 16.0 };
    bool clipLooping_ {};
    std::uint64_t currentTrackId_ {};
    std::uint64_t currentClipId_ {};
    std::vector<std::uint64_t> selectedNoteIds_;
    std::vector<std::uint64_t> selectionAtGestureStart_;
    std::vector<AudioTrackController::MidiNote> gestureNotes_;
    std::vector<AudioTrackController::MidiNote> noteClipboard_;
    GestureMode gestureMode_ { GestureMode::none };
    juce::Point<float> dragStart_;
    juce::Point<float> marqueeAnchor_;
    juce::Rectangle<float> marqueeBounds_;
    bool pendingGridClick_ {};
    bool additiveMarquee_ {};
    int pendingNoteNumber_ { 60 };
    double pendingStartBeat_ {};
    juce::String emptyMessage_;
    juce::ComboBox noteDataSelector_;
    juce::ComboBox trackSelector_;
    juce::ComboBox clipSelector_;
    juce::TextButton addTrackButton_;
    juce::TextButton addClipButton_;
    juce::Slider gainSlider_;
    juce::Slider panSlider_;
    juce::ToggleButton muteButton_;
    juce::ToggleButton soloButton_;
    juce::ComboBox recordQuantizationSelector_;
    juce::ComboBox editSnapSelector_;
    juce::ComboBox editSnapTypeSelector_;
    juce::ToggleButton overdubButton_;
    juce::ToggleButton clipLoopButton_;
    juce::TextButton playButton_;
    juce::TextButton stopButton_;
    juce::TextButton returnButton_;
    juce::ComboBox spacePlayModeSelector_;
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
    juce::TextButton importMidiButton_;
    juce::TextButton exportMidiButton_;
    juce::TextButton undoButton_;
    juce::TextButton redoButton_;
    juce::Label recordingStatusLabel_;
    std::vector<ContextItem> trackItems_;
    std::vector<ContextItem> clipItems_;
    bool updatingContext_ {};
    bool recording_ {};
    std::size_t recordedNoteCount_ {};
    int droppedRecordingEvents_ {};
    bool canUndo_ {};
    bool canRedo_ {};
    bool updatingSnapControls_ {};
    bool updatingClipBoundsControls_ {};
    bool previewPlaying_ {};
    bool navigatingRuler_ {};
    bool navigatingGrid_ {};
    bool navigatingPitches_ {};
    bool rulerGestureMoved_ {};
    BoundHandle draggedBoundHandle_ { BoundHandle::none };
    juce::Point<float> rulerDragLast_;
    double rulerAnchorBeat_ {};
    float pitchDragAccumulator_ {};
    int visibleLowestNote_ { 36 };
    int visibleNoteCount_ { 49 };

    std::function<void(int, double, double, float)> addHandler_;
    std::function<void(std::vector<AudioTrackController::MidiNote>)>
        notesEditHandler_;
    std::function<void(double)> seekHandler_;
    std::function<void(std::uint64_t)> trackSelectionHandler_;
    std::function<void(std::uint64_t)> clipSelectionHandler_;
    std::function<void()> addTrackHandler_;
    std::function<void()> addClipHandler_;
    std::function<void()> importMidiHandler_;
    std::function<void()> exportMidiHandler_;
    std::function<void(float, float, bool, bool)> mixerHandler_;
    std::function<void()> undoHandler_;
    std::function<void()> redoHandler_;
    std::function<void()> beginEditHandler_;
    std::function<void()> endEditHandler_;
    std::function<void(double, double)> viewChangedHandler_;
    std::function<void(double, double, double, double, double, bool)>
        clipBoundsHandler_;
    std::function<void(TransportCommand)> transportHandler_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollComponent)
};

} // namespace cydaw::app
