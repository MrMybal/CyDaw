#pragma once

#include "UiState.h"

#include <imgui.h>

#include <map>
#include <memory>
#include <set>

// The clip being edited. For a MIDI clip, a piano roll: a keyboard that
// plays the instrument, notes added with a double click, moved and stretched
// on the grid, a velocity lane below. Its expression modes draw on the notes
// themselves, as Bitwig does: how a note's pitch moves along it (a scoop, a
// vibrato, a fall; a semitone a row) and its gain (in the lane below). For an
// audio clip, its waveform with its bounds, fades and gain.

namespace cydaw::v2 {

class ImGuiHost;
class AudioEditorView;

class ClipEditorView final {
public:
    ClipEditorView(Studio& studio, UiState& state, WaveformCache& waveforms, ImGuiHost& host);
    ~ClipEditorView();
    void draw();

private:
    using MidiNote = app::AudioTrackController::MidiNote;
    using MidiClip = app::AudioTrackController::MidiClip;
    using NotePoint = app::AudioTrackController::NoteExpressionPoint;

    enum class Drag { none, move, resize, box, velocity, pan, curve };
    // What the roll edits: the notes, or a curve drawn on them.
    enum class Expression { none, pitch, gain };
    // The ready-made curves the shapes menu adds to notes.
    enum class Shape { scoop, slide, vibrato, wideVibrato, fall, doit, swell, fade, accent, clearPitch, clearGain };

    void drawMidi(const TrackRef& track, const MidiClip& clip);
    void drawAudio(const TrackRef& track, const app::AudioTrackController::AudioClip& clip);
    void drawMidiToolbar(const TrackRef& track, const MidiClip& clip);
    void handleMidiKeys(const TrackRef& track, const MidiClip& clip);
    void drawMidiPopups(const TrackRef& track, const MidiClip& clip);
    // Adds a shape to the notes it is for (the one right-clicked, or the
    // selection), over what they have.
    void applyShape(const TrackRef& track, const MidiClip& clip, Shape shape);
    // Small random moves of the selection (or the clip): timing, velocity,
    // length.
    void humanise(const TrackRef& track, const MidiClip& clip);
    void playKey(int note, bool on);

    [[nodiscard]] float beatToX(double beat) const;
    [[nodiscard]] double xToBeat(float x) const;
    [[nodiscard]] float noteToY(int note) const;
    [[nodiscard]] int yToNote(float y) const;
    [[nodiscard]] double snapBeat(double beat) const;

    Studio& studio_;
    UiState& state_;
    WaveformCache& waveforms_;
    ImGuiHost& host_;
    // An audio clip's editor (its sample, its takes, its stretch).
    std::unique_ptr<AudioEditorView> audio_;

    // View.
    float pixelsPerBeat_ { 64.0f };
    float rowHeight_ { 13.0f };
    double scrollBeats_ {};
    float scrollY_ { -1.0f };
    double gridBeats_ { 0.25 };
    double lastLength_ { 0.25 };
    float defaultVelocity_ { 0.8f };
    ImVec2 gridMin_ {};
    ImVec2 gridMax_ {};
    std::uint64_t shownClip_ {};
    // The audio editor's view of its source: the first second shown, and
    // pixels per second (0: fit the source on its first showing).
    double audioScroll_ {};
    double audioPixelsPerSecond_ {};
    std::uint64_t audioShownClip_ {};

    // Editing.
    std::set<std::uint64_t> selection_;
    Drag drag_ { Drag::none };
    ImVec2 dragStart_ {};
    double dragStartBeat_ {};
    int dragStartNote_ {};
    std::map<std::uint64_t, MidiNote> origins_;
    std::uint64_t dragNote_ {};
    int auditioning_ { -1 };

    // Expressions.
    Expression expression_ { Expression::none };
    std::uint64_t curveNote_ {};
    int curvePoint_ { -1 };
    Expression curveKind_ { Expression::none };
    std::uint64_t shapeTarget_ {};
    bool openShapes_ {};
    float humanTiming_ { 12.0f };   // ms
    float humanVelocity_ { 12.0f }; // %
    float humanLength_ { 10.0f };   // %
    std::uint32_t humanSeed_ { 0x2545f491u };
};

} // namespace cydaw::v2
