#pragma once

#include "Studio.h"
#include "UiState.h"

#include <imgui.h>

#include <map>
#include <memory>
#include <optional>
#include <set>

// The clip being edited. For a MIDI clip, a piano roll: a keyboard that
// plays the instrument, notes added with a double click, moved and stretched
// on the grid, a velocity lane below; under its toolbar, the picked notes'
// bar (one note's values typed or dragged, several edited together with
// spreads, random, ramps, as Bitwig's inspector does). Its expression modes draw on the notes
// themselves, as Bitwig does: how a note's pitch moves along it (a scoop, a
// vibrato, a fall; a semitone a row) and its gain (in the lane below). For an
// audio clip, its waveform with its bounds, fades and gain.

namespace cydaw::v2 {

class ImGuiHost;
class AudioEditorView;
class MasterClipEditor;

class ClipEditorView final {
public:
    ClipEditorView(Studio& studio, UiState& state, WaveformCache& waveforms, ImGuiHost& host);
    ~ClipEditorView();
    void draw();
    // Ctrl+Z (Ctrl+Y) on the launcher's audio clip shown: whether it did.
    bool undoLauncherAudio(bool redo);

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
    void drawLauncherAudio(const TrackRef& track, int scene, const app::AudioTrackController::LauncherClip& clip);
    void drawMidiToolbar(const TrackRef& track, const MidiClip& clip);
    void drawNoteBar(const TrackRef& track, const MidiClip& clip);
    // A number of the picked note's, dragged or typed: kept aside while it
    // moves, given back when let go.
    std::optional<double> noteField(const char* id, int field, std::uint64_t note, double current, float speed, double minimum,
                                    double maximum, const char* format, float width);
    void handleMidiKeys(const TrackRef& track, const MidiClip& clip);
    // The velocity lane: a point a note, picked with it (a click, Ctrl, Shift,
    // a frame), the picked moved together (Shift: finely), drawn over with
    // Alt; their values read on them; copied and pasted onto other notes.
    void drawVelocityLane(const TrackRef& track, const MidiClip& clip, ImVec2 laneMin, ImVec2 laneMax, ImU32 colour);
    void copyVelocities(const MidiClip& clip);
    void pasteVelocities(const TrackRef& track, const MidiClip& clip);
    void nudgeVelocities(const TrackRef& track, const MidiClip& clip, float amount);
    // The picked notes copied (Ctrl+C), cut (Ctrl+X), pasted (Ctrl+V) under
    // the pointer, else after the last ones.
    void copyNotes(const MidiClip& clip);
    void pasteNotes(const TrackRef& track, const MidiClip& clip);
    void report(const juce::String& text);
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
    // A master's launcher clip (its automation lanes).
    std::unique_ptr<MasterClipEditor> master_;

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
    // The velocity lane's gesture; its keys once it was clicked.
    enum class VelocityDrag { none, move, frame, paint };
    VelocityDrag velocityDrag_ { VelocityDrag::none };
    bool velocityFocus_ {};
    bool velocityMoved_ {};
    float velocityDelta_ {};
    ImVec2 velocityFrom_ {};
    ImVec2 velocityLast_ {};
    double velocityFromBeat_ {};
    std::uint64_t velocityPressed_ {};
    std::map<std::uint64_t, MidiNote> velocityOrigins_;
    std::set<std::uint64_t> velocityBase_;
    // The pointer over the notes (a paste goes there), else where the next
    // paste goes.
    bool gridHovered_ {};
    double mouseBeat_ {};
    double pasteNext_ {};
    Drag drag_ { Drag::none };
    ImVec2 dragStart_ {};
    double dragStartBeat_ {};
    // Where a selection rectangle began, in the roll's own height (pixels
    // from its top note): it stays there while the roll scrolls under it.
    float dragStartY_ {};
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
    // The note bar: the field being dragged or typed, and the multi-edit's
    // moves and amounts.
    std::uint64_t editNote_ {};
    int editField_ { -1 };
    double editBuffer_ {};
    int pitchShift_ {};
    float startShift_ {};
    float lengthScale_ { 1.0f };
    float velocityShift_ {};
    float velocityScale_ { 1.0f };
    float velocityRandom_ { 15.0f }; // of 127
    float timingRandom_ { 10.0f };   // ms
    float velocityTilt_ { 30.0f };   // of 127
    float humanTiming_ { 12.0f };   // ms
    float humanVelocity_ { 12.0f }; // %
    float humanLength_ { 10.0f };   // %
    std::uint32_t humanSeed_ { 0x2545f491u };
};

} // namespace cydaw::v2
