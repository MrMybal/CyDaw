#pragma once

#include "UiState.h"

#include <imgui.h>

#include <optional>

// An audio clip's editor, in the clip editor panel. Four faces:
//   ÉCHANTILLON — its source large: a selection, and the edits that write a
//     new file (cut, copy, paste, silence, crop, insert silence, fades of four
//     shapes, normalize, gain, reverse, polarity, DC, channels swapped); the
//     history of its versions beside it (a click goes back to one; Ctrl+Z
//     undoes an edit).
//   PRISES — its takes stacked under its timeline, the passage each plays:
//     drag over a take to play it there (comping), a click on its name to
//     play it all; takes from a file, recording over the clip; the comp
//     flattened into one file.
//   ÉVÉNEMENTS — the sounds a clip is made of, on its own timeline: moved,
//     trimmed by their edges, cut at the cursor, removed, duplicated, their
//     gain, fades and stretch, edited in place; sounds added from a file or
//     dropped from the browser (a clip of one sound becomes one of several).
//   ÉTIREMENT — its stretch algorithm, its transposition, its tempo (found
//     in it, or the project's), warp markers on its waveform.

namespace cydaw::v2 {

class ImGuiHost;

class AudioEditorView final {
public:
    AudioEditorView(Studio& studio, UiState& state, WaveformCache& waveforms, ImGuiHost& host);
    void draw(const TrackRef& track, const app::AudioTrackController::AudioClip& clip);

private:
    using AudioClip = app::AudioTrackController::AudioClip;
    enum class Mode { sample, takes, stretch, events };

    void drawHeader(const TrackRef& track, const AudioClip& clip);
    void drawSample(const TrackRef& track, const AudioClip& clip);
    void drawSampleToolbar(const TrackRef& track, const AudioClip& clip);
    void drawHistory(const TrackRef& track, const AudioClip& clip, ImVec2 size);
    void drawTakes(const TrackRef& track, const AudioClip& clip);
    void drawStretch(const TrackRef& track, const AudioClip& clip);
    void drawEvents(const TrackRef& track, const AudioClip& clip);
    void drawEventsToolbar(const TrackRef& track, const AudioClip& clip,
                           const std::vector<app::AudioTrackController::AudioEvent>& events);
    // A face that does not apply to a clip made of events: why, and the way
    // to the events.
    [[nodiscard]] bool madeOfEvents(const AudioClip& clip);
    // A sound added to the clip at a place of its timeline.
    void addEvent(const TrackRef& track, const AudioClip& clip, const juce::File& file, double at);
    // A destructive edit of the selection (the clip's played part when none).
    void apply(const TrackRef& track, const AudioClip& clip, app::AudioEditOperation operation, const char* label,
               float levelDb = 0.0f);
    void report(const juce::String& message, bool error);
    // The selection, in seconds of the source, or the clip's played part.
    [[nodiscard]] std::pair<double, double> target(const AudioClip& clip) const;
    // The source waveform from `scroll_` at `pixelsPerSecond_` into a box.
    void drawSourceWave(const juce::File& file, double total, ImVec2 origin, ImVec2 size, ImU32 colour, double playedFrom,
                        double playedTo);
    // The view of a source: its band and its ruler, then the wave's box.
    // The wheel moves it (Ctrl: zooms). Returns the wave's box.
    std::pair<ImVec2, ImVec2> sourceView(const char* id, double total, float height);
    [[nodiscard]] float xOf(double seconds) const;
    [[nodiscard]] double secondsAt(float x) const;

    Studio& studio_;
    UiState& state_;
    WaveformCache& waveforms_;
    ImGuiHost& host_;
    Mode mode_ { Mode::sample };

    // The view: the first second shown and pixels per second (fitted anew
    // for another clip), and where the wave's box starts.
    std::uint64_t shownClip_ {};
    double scroll_ {};
    double pixelsPerSecond_ {};
    float waveLeft_ {};
    // The selection (seconds of the source; equal ends: a place to paste).
    double selectionStart_ {};
    double selectionEnd_ {};
    bool selecting_ {};
    double selectingFrom_ {};
    // Settings of the edits.
    float gainDb_ { 3.0f };
    float normaliseDb_ { -1.0f };
    int fadeCurve_ {};
    // Hearing the selection: its end, to stop there.
    std::optional<double> previewEnd_;

    // Takes: a passage being dragged over a take.
    int compLane_ { -1 };
    double compFrom_ {};
    double compTo_ {};
    std::unique_ptr<juce::FileChooser> chooser_;

    // Stretch: the warp marker in hand.
    std::uint64_t markerDragged_ {};

    // Events: the one selected, the cursor (seconds of the clip), and the
    // one in hand: what is done to it, where it was taken, and where it is
    // shown while dragged (committed when let go: one undo step).
    Mode shownMode_ { Mode::sample };
    std::uint64_t selectedEvent_ {};
    double eventCursor_ {};
    enum class EventDrag { none, move, trimStart, trimEnd };
    EventDrag eventDrag_ { EventDrag::none };
    std::uint64_t draggedEvent_ {};
    double dragGrab_ {};
    double dragStart_ {};
    double dragOffset_ {};
    double dragDuration_ {};
    double dragSourceDuration_ {};
    double dragOriginStart_ {};
    double dragOriginOffset_ {};
    double dragOriginDuration_ {};
    double dragOriginSourceDuration_ {};
    double dragSourceLength_ {};
    // A field of the selected event being dragged: its edits make one undo step.
    bool eventFieldBatch_ {};
    // Its transposition while its field is dragged (set when let go).
    float eventPitchEdit_ {};
    bool eventPitchEditing_ {};
};

} // namespace cydaw::v2
