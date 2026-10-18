#pragma once

#include "CurveEditor.h"
#include "Studio.h"
#include "UiState.h"

#include <imgui.h>

#include <map>
#include <optional>

// An audio clip's editor, in the clip editor panel, for a clip of the
// arrangement and a launcher slot's alike (as the Foundry lays it out):
//   the toolbar (listen or launch, undo, cut, duplicate, delete, a sound
//     added, the zoom, the takes);
//   the whole clip in small, the part seen framed;
//   the ruler: the clip's bar (where it starts and stops playing, its loop,
//     as Bitwig draws them: what lies before the start stays in the clip,
//     for its automation), bars and beats counted from where it starts;
//   its sounds (events) on their timeline, both channels drawn. Each one
//     moves (its body), shortens (its edges), fades (its top corners). A
//     double click in its lower half sets a stretch point, dragged then to
//     put that moment elsewhere (the speed it makes coloured along the
//     bottom); a double click in its upper half cuts it there. A right
//     click: everything that can be done to it;
//   beside them, Details (the clip: its length, play range, loop; the sound:
//     its file, its format, its place, its stretch, tempo, transposition,
//     gain, fades), Processes (as the Foundry: the selection in a sound, or
//     the sound whole) and History.
// An arrangement clip's takes have a view of their own (comping).

namespace cydaw::v2 {

class ImGuiHost;

// A clip's play range and loop as they play (unset ends: its length).
struct AudioClipBounds {
    double duration {};
    double playStart {};
    double playEnd {};
    double loopStart {};
    double loopEnd {};
    bool looping {};
};

class AudioEditorView final {
public:
    AudioEditorView(Studio& studio, UiState& state, WaveformCache& waveforms, ImGuiHost& host);
    void draw(const TrackRef& track, const app::AudioTrackController::AudioClip& clip);
    void drawLauncher(const TrackRef& track, int scene, const app::AudioTrackController::LauncherClip& clip);
    // Ctrl+Z on a launcher clip (the engine keeps the arrangement's): whether
    // there was something to take back.
    bool undoLauncher(bool redo);

private:
    using AudioClip = app::AudioTrackController::AudioClip;
    using LauncherClip = app::AudioTrackController::LauncherClip;
    using AudioEvent = app::AudioTrackController::AudioEvent;
    using StretchMode = app::AudioTrackController::StretchMode;
    using AutomationPoint = app::AudioTrackController::AutomationPoint;
    enum class Mode { clip, takes };
    enum class Drag { none, move, trimStart, trimEnd, fadeIn, fadeOut, marker, select, playStart, playEnd, loopStart, loopEnd, loopMove };

    // ---- The panel --------------------------------------------------------------------------
    void drawClip(const TrackRef& track, const AudioClip& clip);
    void drawToolbar(const TrackRef& track, const AudioClip& clip, const std::vector<AudioEvent>& events);
    void drawOverview(const AudioClip& clip, const std::vector<AudioEvent>& events, ImVec2 size);
    void drawRuler(const TrackRef& track, const AudioClip& clip, const std::vector<AudioEvent>& events, ImVec2 size);
    void drawEvents(const TrackRef& track, const AudioClip& clip, const std::vector<AudioEvent>& events, ImVec2 size);
    void eventMenu(const TrackRef& track, const AudioClip& clip, const std::vector<AudioEvent>& events);
    // The clip's automation lanes, under its sounds, on the same timeline;
    // each with its cell on the left (the value it automates).
    void drawLanes(const TrackRef& track, const AudioClip& clip, float headerLeft, float top, float height);
    // The lanes shown (the volume's at least, and any with points, the
    // shown sound's too).
    [[nodiscard]] std::vector<juce::String> laneRows(const AudioClip& clip, const std::vector<AudioEvent>& events);
    // A sound's own lanes ("sound.volume", "sound.pan", "sound.pitch"),
    // shown for the sound picked (or the only one): its key in
    // AudioEvent::lanes, the sound, the curve editor's key (one per sound).
    [[nodiscard]] static bool isSoundLane(const juce::String& name);
    [[nodiscard]] static juce::String soundKeyOf(const juce::String& name);
    [[nodiscard]] const AudioEvent* soundTarget(const std::vector<AudioEvent>& events) const;
    [[nodiscard]] juce::String editorKey(const juce::String& name, const std::vector<AudioEvent>& events) const;
    [[nodiscard]] static juce::String laneLabel(const std::vector<Studio::MasterLaneChoice>& choices, const juce::String& name);
    // The grid as seen: from 16 bars down to a sixteenth of a beat.
    [[nodiscard]] double gridStep() const;
    // The darker lines' step and the brighter ones'.
    [[nodiscard]] std::pair<double, double> gridLevels() const;
    void drawGrid(ImDrawList* list, ImVec2 min, ImVec2 max) const;
    // The wheel over the timeline: zoom around the pointer (Shift: along).
    void wheel(const AudioClip& clip, const std::vector<AudioEvent>& events);
    bool setLanes(const TrackRef& track, const AudioClip& clip, std::map<juce::String, std::vector<AutomationPoint>> lanes,
                  const juce::String& label);
    // A lane as the curve editor sees it, its points, and back.
    [[nodiscard]] curves::Lane laneDescription(const std::vector<Studio::MasterLaneChoice>& choices, const juce::String& name) const;
    [[nodiscard]] std::vector<curves::Point> curvePoints(const AudioClip& clip, const std::vector<AudioEvent>& events,
                                                        const juce::String& name) const;
    bool setCurve(const TrackRef& track, const AudioClip& clip, const std::vector<AudioEvent>& events, const juce::String& name,
                  const std::vector<curves::Point>& points);
    // The picked points' bar, under the toolbar: their time and value, or
    // the multi-edit.
    void drawCurveBar(const TrackRef& track, const AudioClip& clip);
    void drawSide(const TrackRef& track, const AudioClip& clip, const std::vector<AudioEvent>& events, ImVec2 size);
    void drawDetails(const TrackRef& track, const AudioClip& clip, const std::vector<AudioEvent>& events);
    void drawProcesses(const TrackRef& track, const AudioClip& clip, const std::vector<AudioEvent>& events);
    void drawHistory(const TrackRef& track, const AudioClip& clip);
    void drawTakes(const TrackRef& track, const AudioClip& clip);
    void handleKeys(const TrackRef& track, const AudioClip& clip, const std::vector<AudioEvent>& events);

    // ---- The view ---------------------------------------------------------------------------
    // The timeline shown: the clip's own seconds (0 its content's start).
    [[nodiscard]] float xOf(double seconds) const;
    [[nodiscard]] double secondsAt(float x) const;
    [[nodiscard]] double viewLength(const AudioClip& clip, const std::vector<AudioEvent>& events) const;
    // How far before the content's start the view goes (room to move the
    // start there).
    [[nodiscard]] double minScroll(const AudioClip& clip, const std::vector<AudioEvent>& events) const;
    // The bounds shown: the ones dragged on the ruler, else the clip's.
    [[nodiscard]] AudioClipBounds shownBounds(const AudioClip& clip) const;
    void fit(const AudioClip& clip, const std::vector<AudioEvent>& events);
    // A time on the beat grid, unless Shift is held.
    [[nodiscard]] double snap(double seconds) const;
    [[nodiscard]] double beatSeconds() const;
    void report(const juce::String& message, bool error);

    // ---- The clip, wherever it is -----------------------------------------------------------
    [[nodiscard]] AudioClip launcherView(const TrackRef& track, int scene, const LauncherClip& clip);
    [[nodiscard]] bool launcher() const noexcept { return scene_ >= 0; }
    // A clip of one sound as its only event (its id the clip's).
    [[nodiscard]] static AudioEvent soleEvent(const AudioClip& clip);
    [[nodiscard]] static std::vector<AudioEvent> eventsOf(const AudioClip& clip);
    // A launcher clip of one sound becomes a timeline of that event before
    // it is edited: the event's id then (0 when it could not be).
    std::uint64_t launcherEvent(const TrackRef& track, const AudioClip& clip, std::uint64_t event);
    bool updateEvent(const TrackRef& track, const AudioClip& clip, std::uint64_t event, double start, double offset, double duration,
                     double sourceDuration, double fadeIn, double fadeOut);
    bool setEventProperties(const TrackRef& track, const AudioClip& clip, std::uint64_t event, float gainDb, double tempo,
                            StretchMode mode);
    std::uint64_t splitEvent(const TrackRef& track, const AudioClip& clip, std::uint64_t event, double at);
    bool removeEvent(const TrackRef& track, const AudioClip& clip, std::uint64_t event);
    juce::Result replaceEvents(const TrackRef& track, const AudioClip& clip, std::vector<AudioEvent> events);
    std::uint64_t addMarker(const TrackRef& track, const AudioClip& clip, std::uint64_t event, double sourceSeconds, double eventSeconds);
    bool moveMarker(const TrackRef& track, const AudioClip& clip, std::uint64_t event, std::uint64_t marker, double eventSeconds);
    bool removeMarker(const TrackRef& track, const AudioClip& clip, std::uint64_t event, std::uint64_t marker);
    bool setBounds(const TrackRef& track, const AudioClip& clip, double duration, double playStart, double playEnd, double loopStart,
                   double loopEnd, bool looping);
    // Sounds and bounds put in together; anything before the content's
    // start moves everything later by as much (a launcher clip): one step.
    bool commitTimeline(const TrackRef& track, const AudioClip& clip, std::vector<AudioEvent> events, AudioClipBounds next,
                        const juce::String& label, bool eventsChanged = false);
    void addEvent(const TrackRef& track, const AudioClip& clip, const juce::File& file, double at);
    // A process on a sound, or on a part of it (seconds of its file).
    void process(const TrackRef& track, const AudioClip& clip, const AudioEvent& event, const app::AudioEditRequest& request,
                 const juce::String& label, std::optional<std::pair<double, double>> part);
    void duplicate(const TrackRef& track, const AudioClip& clip, const std::vector<AudioEvent>& events, std::uint64_t event);
    void fitToTempo(const TrackRef& track, const AudioClip& clip, const AudioEvent& event);
    // Where the selection falls in a sound: seconds of its file.
    [[nodiscard]] std::optional<std::pair<double, double>> selectionIn(const AudioEvent& event) const;

    // ---- A launcher clip's undo (the engine keeps none for them) ------------------------------
    void remember(const TrackRef& track, const juce::String& label);
    struct Step {
        TrackRef track;
        LauncherClip clip;
        juce::String label;
    };
    std::vector<Step> undo_;
    std::vector<Step> redo_;

    // ---- A sound file's format ----------------------------------------------------------------
    struct Format {
        double rate {};
        int channels {};
        int bits {};
        double length {};
    };
    [[nodiscard]] const Format& formatOf(const juce::File& file);
    std::map<juce::String, Format> formats_;

    Studio& studio_;
    UiState& state_;
    WaveformCache& waveforms_;
    ImGuiHost& host_;
    Mode mode_ { Mode::clip };

    // The launcher slot shown (-1: a clip of the arrangement) and its clip.
    int scene_ { -1 };
    LauncherClip launcherClip_;

    // The view: the first second shown, pixels per second (fitted anew for
    // another clip), the lane's left edge and width.
    std::uint64_t shownClip_ {};
    double scroll_ {};
    double pixelsPerSecond_ {};
    // The sounds' rows: their height (a part of the room they get) and how
    // far down they are shown, from the band beside them.
    float soundsZoom_ { 1.0f };
    float soundsScroll_ {};
    float laneLeft_ {};
    float laneWidth_ {};
    // The cursor and the selection (seconds of the clip; equal ends: none).
    double cursor_ {};
    double selectionStart_ {};
    double selectionEnd_ {};
    std::uint64_t selectedEvent_ {};
    // The sound whose playing mode the badge's menu changes.
    std::uint64_t modeMenuEvent_ {};
    // A drag: what is dragged, the event or point, where it was taken and
    // where it is shown (committed when let go: one undo step).
    Drag drag_ { Drag::none };
    std::uint64_t draggedEvent_ {};
    std::uint64_t draggedMarker_ {};
    double grab_ {};
    double originStart_ {};
    double originOffset_ {};
    double originDuration_ {};
    double originSourceDuration_ {};
    double originFade_ {};
    double sourceLength_ {};
    double shownStart_ {};
    double shownOffset_ {};
    double shownDuration_ {};
    double shownSourceDuration_ {};
    double shownValue_ {};
    // The clip's bounds while its bar is dragged.
    AudioClipBounds draft_;
    // The event and the place a right click was on.
    std::uint64_t menuEvent_ {};
    std::uint64_t menuMarker_ {};
    double menuAt_ {};
    // Hearing a sound or a part: where to stop.
    std::optional<double> previewEnd_;
    // A field dragged in Details: its edits make one undo step.
    bool fieldBatch_ {};
    bool batchRemembered_ {};
    // Where bars and beats count from (the clip's loop start).
    double gridOrigin_ {};
    // The lanes shown for a clip; a point dragged.
    std::map<std::uint64_t, std::vector<juce::String>> laneRows_;
    // Each lane's curve editor (its picked points), and the lane last clicked.
    std::map<juce::String, curves::Editor> curveEditors_;
    juce::String focusedLane_;
    // Processes.
    int process_ {};
    float gainDb_ { 3.0f };
    float normaliseDb_ { -1.0f };
    int fadeCurve_ {};
    // Takes: a passage being dragged over a take.
    int compLane_ { -1 };
    double compFrom_ {};
    double compTo_ {};
    std::unique_ptr<juce::FileChooser> chooser_;
};

} // namespace cydaw::v2
