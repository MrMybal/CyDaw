#include "AudioEditorView.h"

#include "Icons.h"
#include "ImGuiHost.h"
#include "Theme.h"
#include "Widgets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cydaw::v2 {

using ui::dp;
using ui::L;

namespace {

using AudioClip = app::AudioTrackController::AudioClip;
using AudioEvent = app::AudioTrackController::AudioEvent;
using StretchMode = app::AudioTrackController::StretchMode;
using Op = app::AudioEditOperation;

// Seconds of source per second of the clip's timeline.
double speedOf(const AudioClip& clip)
{
    const auto source = clip.sourceDurationSeconds > 0.0 ? clip.sourceDurationSeconds : clip.durationSeconds;
    return source / std::max(1.0e-6, clip.durationSeconds);
}

// A take's colour (the comp row and its lane).
ImU32 takeColour(const std::size_t take)
{
    static const ImU32 colours[] = { IM_COL32(255, 155, 47, 255), IM_COL32(90, 176, 240, 255), IM_COL32(82, 209, 162, 255),
                                     IM_COL32(199, 146, 234, 255), IM_COL32(232, 118, 90, 255), IM_COL32(232, 197, 71, 255),
                                     IM_COL32(240, 98, 146, 255),  IM_COL32(140, 158, 255, 255) };
    return colours[take % (sizeof(colours) / sizeof(colours[0]))];
}

// The passages a clip plays, as a comp (one passage of the active take
// when it has none).
std::vector<app::AudioTrackController::CompSegment> passagesOf(const AudioClip& clip)
{
    if (!clip.comp.empty())
        return clip.comp;
    return { { 0.0, clip.durationSeconds, clip.activeTakeIndex } };
}

// The playing modes in the order the menus give them.
constexpr StretchMode playModes[] { StretchMode::raw, StretchMode::repitch, StretchMode::tonal,
                                   StretchMode::beats, StretchMode::complex, StretchMode::texture };

const char* modeName(const StretchMode mode)
{
    switch (mode) {
    case StretchMode::raw: return L("Brut (jamais étiré)", "Raw (never stretched)");
    case StretchMode::repitch: return L("Vitesse (la hauteur suit)", "Repitch (the pitch follows)");
    case StretchMode::tonal: return L("Tonal (garde la hauteur)", "Tonal (keeps the pitch)");
    case StretchMode::beats: return L("Rythme (tranches aux attaques)", "Beats (slices at the attacks)");
    case StretchMode::complex: return L("Complexe (mixages)", "Complex (full mixes)");
    case StretchMode::texture: return L("Texture (nappe)", "Texture (pad)");
    }
    return "";
}

// On the sound's badge.
const char* modeShort(const StretchMode mode)
{
    switch (mode) {
    case StretchMode::raw: return L("Brut", "Raw");
    case StretchMode::repitch: return L("Vitesse", "Repitch");
    case StretchMode::tonal: return L("Tonal", "Tonal");
    case StretchMode::beats: return L("Rythme", "Beats");
    case StretchMode::complex: return L("Complexe", "Complex");
    case StretchMode::texture: return L("Texture", "Texture");
    }
    return "";
}

const char* modeHelp(const StretchMode mode)
{
    switch (mode) {
    case StretchMode::raw: return L("Joué à la vitesse de son fichier, jamais étiré : ni le tempo ni les points d'étirement ne le changent ; sa hauteur est la sienne. "
                                    "Comme Ableton sans Warp, Bitwig Raw.",
                                    "Played at its file's speed, never stretched: neither the tempo nor stretch points change it; its pitch is its own. "
                                    "Like Ableton with Warp off, Bitwig Raw.");
    case StretchMode::repitch: return L("Lu plus vite ou plus lentement : la hauteur suit, comme une bande. Comme Ableton Re-Pitch, Bitwig Repitch.",
                                        "Read faster or slower: the pitch follows, as a tape does. Like Ableton Re-Pitch, Bitwig Repitch.");
    case StretchMode::tonal: return L("Étiré en gardant la hauteur : voix, accords, nappes. Transposable. Comme Ableton Tones, Bitwig Stretch.",
                                      "Stretched keeping the pitch: voices, chords, pads. Can be transposed. Like Ableton Tones, Bitwig Stretch.");
    case StretchMode::beats: return L("Coupé aux attaques, chaque tranche jouée telle quelle : les batteries gardent leur punch. Garde la hauteur. "
                                      "Comme Ableton Beats, Bitwig Slices.",
                                      "Cut at the attacks, each slice played as it is: drums keep their punch. Keeps the pitch. Like Ableton Beats, Bitwig Slices.");
    case StretchMode::complex: return L("Étiré en gardant la hauteur, pour un mixage entier (batterie et accords ensemble) : fenêtres moyennes, reprises à chaque attaque. "
                                        "Transposable. Comme Ableton Complex, Bitwig Stretch HD.",
                                        "Stretched keeping the pitch, for a whole mix (drums and chords together): middle windows, started afresh at every attack. "
                                        "Can be transposed. Like Ableton Complex, Bitwig Stretch HD.");
    case StretchMode::texture: return L("Étale le son en nappe, pour étirer dix fois et plus. Garde la hauteur. Comme Ableton Texture.",
                                        "Smears the sound into a pad, to stretch ten times and more. Keeps the pitch. Like Ableton Texture.");
    }
    return "";
}

// Whether a mode's pitch is its speed's (no transposition, no pitch lane).
bool pitchFollowsSpeed(const StretchMode mode)
{
    return mode == StretchMode::repitch || mode == StretchMode::raw;
}

// The processes, as the Foundry lists them.
struct ProcessKind {
    Op op;
    const char* french;
    const char* english;
};
constexpr int processCount = 9;
const ProcessKind& processAt(const int index)
{
    static const ProcessKind kinds[processCount] = {
        { Op::amplify, "Volume", "Volume" },
        { Op::normalisePeak, "Normaliser", "Normalize" },
        { Op::fadeIn, "Fondu entrant", "Fade in" },
        { Op::fadeOut, "Fondu sortant", "Fade out" },
        { Op::invertPolarity, "Inverser la polarité", "Invert the polarity" },
        { Op::reverse, "Renverser", "Reverse" },
        { Op::removeDcOffset, "Retirer le décalage (DC)", "Remove the DC offset" },
        { Op::silence, "Rendre silencieux", "Silence" },
        { Op::swapStereo, "Échanger gauche et droite", "Swap left and right" },
    };
    return kinds[std::clamp(index, 0, processCount - 1)];
}

using Bounds = AudioClipBounds;

Bounds boundsOf(const AudioClip& clip)
{
    Bounds b;
    b.duration = std::max(0.001, clip.durationSeconds);
    b.playStart = std::clamp(clip.playStartSeconds, 0.0, b.duration - 0.001);
    b.playEnd = clip.playEndSeconds > b.playStart + 1.0e-6 ? std::min(clip.playEndSeconds, b.duration) : b.duration;
    b.loopStart = std::clamp(clip.loopStartSeconds, b.playStart, std::max(b.playStart, b.playEnd - 0.001));
    b.loopEnd = clip.loopEndSeconds > b.loopStart + 1.0e-6 ? std::min(clip.loopEndSeconds, b.playEnd) : b.playEnd;
    b.looping = clip.looping;
    return b;
}

// "-1.3", "1.1", "2": bars and beats counted from the loop's start (the bar
// before it is -1, as Bitwig counts).
juce::String barsBeats(const double beats, const int numerator, const bool withBeat)
{
    const auto whole = static_cast<long long>(std::floor(beats + 1.0e-6));
    const auto bar = static_cast<long long>(std::floor(static_cast<double>(whole) / numerator));
    const auto beat = whole - bar * numerator + 1;
    const auto shown = bar >= 0 ? bar + 1 : bar;
    return withBeat ? juce::String(shown) + "." + juce::String(beat) : juce::String(shown);
}

} // namespace

AudioEditorView::AudioEditorView(Studio& studio, UiState& state, WaveformCache& waveforms, ImGuiHost& host)
    : studio_(studio), state_(state), waveforms_(waveforms), host_(host)
{
}

void AudioEditorView::report(const juce::String& message, const bool error)
{
    state_.status = message;
    state_.statusIsError = error;
    state_.statusTime = ImGui::GetTime();
}

// ---- The view ---------------------------------------------------------------------------------

float AudioEditorView::xOf(const double seconds) const
{
    return laneLeft_ + static_cast<float>((seconds - scroll_) * pixelsPerSecond_);
}

double AudioEditorView::secondsAt(const float x) const
{
    return scroll_ + static_cast<double>(x - laneLeft_) / std::max(1.0e-9, pixelsPerSecond_);
}

double AudioEditorView::viewLength(const AudioClip& clip, const std::vector<AudioEvent>& events) const
{
    auto end = std::max(clip.durationSeconds, clip.playEndSeconds);
    for (const auto& event : events)
        end = std::max(end, event.startSeconds + event.durationSeconds);
    return std::max(0.05, end * 1.06);
}

double AudioEditorView::minScroll(const AudioClip& clip, const std::vector<AudioEvent>& events) const
{
    return -std::max(beatSeconds() * 8.0, viewLength(clip, events) * 0.5);
}

AudioClipBounds AudioEditorView::shownBounds(const AudioClip& clip) const
{
    const auto dragging = drag_ == Drag::playStart || drag_ == Drag::playEnd || drag_ == Drag::loopStart || drag_ == Drag::loopEnd
                          || drag_ == Drag::loopMove;
    return dragging ? draft_ : boundsOf(clip);
}

void AudioEditorView::fit(const AudioClip& clip, const std::vector<AudioEvent>& events)
{
    pixelsPerSecond_ = static_cast<double>(std::max(dp(80.0f), laneWidth_)) / viewLength(clip, events);
    scroll_ = 0.0;
}

double AudioEditorView::beatSeconds() const
{
    return 60.0 / std::max(1.0, studio_.engine().tempo());
}

double AudioEditorView::snap(const double seconds) const
{
    if (!ui::snapping(state_.audioClipSnap))
        return seconds;
    // On the grid as seen: finer when close, coarser when far.
    const auto step = gridStep();
    return gridOrigin_ + std::round((seconds - gridOrigin_) / step) * step;
}

// ---- The clip, wherever it is -------------------------------------------------------------------

const AudioEditorView::Format& AudioEditorView::formatOf(const juce::File& file)
{
    const auto key = file.getFullPathName();
    if (const auto found = formats_.find(key); found != formats_.end())
        return found->second;
    Format format;
    juce::AudioFormatManager manager;
    manager.registerBasicFormats();
    if (std::unique_ptr<juce::AudioFormatReader> reader(manager.createReaderFor(file)); reader != nullptr && reader->sampleRate > 0.0) {
        format.rate = reader->sampleRate;
        format.channels = static_cast<int>(reader->numChannels);
        format.bits = static_cast<int>(reader->bitsPerSample);
        format.length = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
    }
    return formats_[key] = format;
}

AudioEditorView::AudioClip AudioEditorView::launcherView(const TrackRef& track, const int scene, const LauncherClip& clip)
{
    AudioClip view;
    // An id out of the arrangement's, one per slot: another slot refits the view.
    view.id = (std::uint64_t { 1 } << 62) | (static_cast<std::uint64_t>(track.audioIndex) << 16) | static_cast<std::uint64_t>(scene);
    view.name = clip.name;
    view.sourceOffsetSeconds = clip.sourceOffsetSeconds;
    view.durationSeconds = clip.durationSeconds;
    view.sourceDurationSeconds = clip.durationSeconds;
    view.colour = clip.colour;
    view.sourceFile = clip.sourceFile;
    view.sourceLengthSeconds = clip.sourceFile == juce::File() ? 0.0 : formatOf(clip.sourceFile).length;
    view.events = clip.events;
    view.usesEventTimeline = clip.usesEventTimeline;
    view.playStartSeconds = clip.playStartSeconds;
    view.playEndSeconds = clip.playEndSeconds;
    view.loopStartSeconds = clip.loopStartSeconds;
    view.loopEndSeconds = clip.loopEndSeconds;
    view.looping = clip.looping;
    view.automation = clip.automation;
    view.lanes = clip.lanes;
    return view;
}

AudioEditorView::AudioEvent AudioEditorView::soleEvent(const AudioClip& clip)
{
    AudioEvent own;
    own.id = clip.id;
    own.name = clip.name.isNotEmpty() ? clip.name : clip.sourceFile.getFileNameWithoutExtension();
    own.sourceOffsetSeconds = clip.sourceOffsetSeconds;
    own.durationSeconds = clip.durationSeconds;
    own.sourceDurationSeconds = clip.sourceDurationSeconds > 0.0 ? clip.sourceDurationSeconds : clip.durationSeconds;
    own.colour = clip.colour;
    own.sourceFile = clip.sourceFile;
    own.sourceLengthSeconds = clip.sourceLengthSeconds;
    own.fadeInSeconds = clip.fadeInSeconds;
    own.fadeOutSeconds = clip.fadeOutSeconds;
    own.sourceTempoBpm = clip.sourceTempoBpm;
    own.stretchMode = clip.stretchMode;
    own.stretchMarkers = clip.stretchMarkers;
    own.pitchSemitones = clip.pitchSemitones;
    return own;
}

std::vector<AudioEditorView::AudioEvent> AudioEditorView::eventsOf(const AudioClip& clip)
{
    if (clip.usesEventTimeline)
        return clip.events;
    if (clip.sourceFile == juce::File())
        return {};
    return { soleEvent(clip) };
}

void AudioEditorView::remember(const TrackRef& track, const juce::String& label)
{
    if (!launcher())
        return;
    // A field dragged: one step for all it does.
    if (fieldBatch_) {
        if (batchRemembered_)
            return;
        batchRemembered_ = true;
    }
    undo_.push_back({ track, launcherClip_, label });
    if (undo_.size() > 64)
        undo_.erase(undo_.begin());
    redo_.clear();
}

bool AudioEditorView::undoLauncher(const bool redo)
{
    auto& from = redo ? redo_ : undo_;
    auto& to = redo ? undo_ : redo_;
    if (from.empty() || !launcher())
        return false;
    const auto step = from.back();
    from.pop_back();
    auto& engine = studio_.engine();
    if (const auto now = engine.launcherClip(step.track.audioIndex, step.clip.sceneIndex))
        to.push_back({ step.track, *now, step.label });
    if (!engine.restoreLauncherClip(step.track.audioIndex, step.clip))
        return false;
    studio_.markDirty();
    report((redo ? ui::Ls("Rétabli : ", "Redone: ") : ui::Ls("Annulé : ", "Undone: ")) + step.label, false);
    return true;
}

std::uint64_t AudioEditorView::launcherEvent(const TrackRef& track, const AudioClip& clip, const std::uint64_t event)
{
    if (clip.usesEventTimeline || event != clip.id)
        return event;
    auto sole = soleEvent(clip);
    sole.id = 0;
    auto& engine = studio_.engine();
    if (engine.replaceLauncherClipEvents(track.audioIndex, scene_, { sole }).failed())
        return 0;
    const auto made = engine.launcherClip(track.audioIndex, scene_);
    if (!made || made->events.empty())
        return 0;
    const auto id = made->events.front().id;
    if (selectedEvent_ == event)
        selectedEvent_ = id;
    return id;
}

bool AudioEditorView::updateEvent(const TrackRef& track, const AudioClip& clip, const std::uint64_t event, const double start,
                                  const double offset, const double duration, const double sourceDuration, const double fadeIn,
                                  const double fadeOut)
{
    auto& engine = studio_.engine();
    if (!launcher())
        return engine.updateAudioClipEvent(track.audioIndex, clip.id, event, start, offset, duration, sourceDuration, fadeIn, fadeOut);
    remember(track, ui::Ls("Son modifié", "Sound changed"));
    const auto id = launcherEvent(track, clip, event);
    return id != 0 && engine.updateLauncherClipEvent(track.audioIndex, scene_, id, start, offset, duration, sourceDuration, fadeIn, fadeOut);
}

bool AudioEditorView::setEventProperties(const TrackRef& track, const AudioClip& clip, const std::uint64_t event, const float gainDb,
                                         const double tempo, const StretchMode mode)
{
    auto& engine = studio_.engine();
    if (!launcher())
        return engine.setAudioClipEventProperties(track.audioIndex, clip.id, event, gainDb, tempo, mode);
    remember(track, ui::Ls("Réglages du son", "Sound settings"));
    const auto id = launcherEvent(track, clip, event);
    return id != 0 && engine.setLauncherClipEventProperties(track.audioIndex, scene_, id, gainDb, tempo, mode);
}

std::uint64_t AudioEditorView::splitEvent(const TrackRef& track, const AudioClip& clip, const std::uint64_t event, const double at)
{
    auto& engine = studio_.engine();
    if (!launcher())
        return engine.splitAudioClipEvent(track.audioIndex, clip.id, event, at);
    remember(track, ui::Ls("Son coupé", "Sound cut"));
    const auto id = launcherEvent(track, clip, event);
    return id != 0 ? engine.splitLauncherClipEvent(track.audioIndex, scene_, id, at) : 0;
}

bool AudioEditorView::removeEvent(const TrackRef& track, const AudioClip& clip, const std::uint64_t event)
{
    auto& engine = studio_.engine();
    if (!launcher())
        return engine.removeAudioClipEvent(track.audioIndex, clip.id, event);
    remember(track, ui::Ls("Son supprimé", "Sound deleted"));
    // The only sound of a clip of one: the clip empty.
    if (!clip.usesEventTimeline && event == clip.id)
        return engine.replaceLauncherClipEvents(track.audioIndex, scene_, {}).wasOk();
    return engine.removeLauncherClipEvent(track.audioIndex, scene_, event);
}

juce::Result AudioEditorView::replaceEvents(const TrackRef& track, const AudioClip& clip, std::vector<AudioEvent> events)
{
    auto& engine = studio_.engine();
    if (!launcher())
        return engine.replaceAudioClipEvents(track.audioIndex, clip.id, std::move(events));
    remember(track, ui::Ls("Sons modifiés", "Sounds changed"));
    // The sound of a clip of one gets an id of its own.
    for (auto& event : events)
        if (event.id == clip.id)
            event.id = 0;
    return engine.replaceLauncherClipEvents(track.audioIndex, scene_, std::move(events));
}

std::uint64_t AudioEditorView::addMarker(const TrackRef& track, const AudioClip& clip, const std::uint64_t event, const double sourceSeconds,
                                         const double eventSeconds)
{
    auto& engine = studio_.engine();
    if (!launcher())
        return engine.addAudioClipEventStretchMarker(track.audioIndex, clip.id, event, sourceSeconds, eventSeconds);
    remember(track, ui::Ls("Point d'étirement", "Stretch point"));
    const auto id = launcherEvent(track, clip, event);
    return id != 0 ? engine.addLauncherClipEventStretchMarker(track.audioIndex, scene_, id, sourceSeconds, eventSeconds) : 0;
}

bool AudioEditorView::moveMarker(const TrackRef& track, const AudioClip& clip, const std::uint64_t event, const std::uint64_t marker,
                                 const double eventSeconds)
{
    auto& engine = studio_.engine();
    if (!launcher())
        return engine.updateAudioClipEventStretchMarker(track.audioIndex, clip.id, event, marker, eventSeconds);
    remember(track, ui::Ls("Point d'étirement déplacé", "Stretch point moved"));
    const auto id = launcherEvent(track, clip, event);
    return id != 0 && engine.updateLauncherClipEventStretchMarker(track.audioIndex, scene_, id, marker, eventSeconds);
}

bool AudioEditorView::removeMarker(const TrackRef& track, const AudioClip& clip, const std::uint64_t event, const std::uint64_t marker)
{
    auto& engine = studio_.engine();
    if (!launcher())
        return engine.removeAudioClipEventStretchMarker(track.audioIndex, clip.id, event, marker);
    remember(track, ui::Ls("Point d'étirement retiré", "Stretch point removed"));
    const auto id = launcherEvent(track, clip, event);
    return id != 0 && engine.removeLauncherClipEventStretchMarker(track.audioIndex, scene_, id, marker);
}

bool AudioEditorView::setBounds(const TrackRef& track, const AudioClip& clip, const double duration, const double playStart,
                                const double playEnd, const double loopStart, const double loopEnd, const bool looping)
{
    auto& engine = studio_.engine();
    const auto length = std::max({ 0.02, duration, playEnd });
    if (!launcher())
        return engine.setAudioClipTimelineBounds(track.audioIndex, clip.id, length, playStart, playEnd, loopStart, loopEnd, looping);
    remember(track, ui::Ls("Plage du clip", "Clip range"));
    return engine.setLauncherClipTimelineBounds(track.audioIndex, scene_, length, playStart, playEnd, loopStart, loopEnd, looping);
}

bool AudioEditorView::commitTimeline(const TrackRef& track, const AudioClip& clip, std::vector<AudioEvent> events, AudioClipBounds next,
                                     const juce::String& label, const bool eventsChanged)
{
    auto& engine = studio_.engine();
    const auto index = track.audioIndex;
    // Anything before the content's start: everything moves later by as much.
    auto earliest = std::min(next.playStart, next.loopStart);
    for (const auto& event : events)
        earliest = std::min(earliest, event.startSeconds);
    const auto by = earliest < 0.0 ? -earliest : 0.0;
    if (by > 0.0 && !launcher()) {
        report(ui::Ls("Dans l'arrangement, le clip commence à sa place : déplacez le clip lui-même.",
                      "In the arrangement the clip starts at its place: move the clip itself."),
               true);
        return false;
    }
    next.playStart += by;
    next.playEnd += by;
    next.loopStart += by;
    next.loopEnd += by;
    next.duration = std::max({ 0.02, next.duration + by, next.playEnd });
    if (launcher()) {
        remember(track, label);
        if (by > 0.0 || eventsChanged) {
            for (auto& event : events) {
                event.startSeconds += by;
                if (event.id == clip.id)
                    event.id = 0;
            }
            if (const auto done = engine.replaceLauncherClipEvents(index, scene_, std::move(events)); done.failed()) {
                report(done.getErrorMessage(), true);
                return false;
            }
        }
        const auto done = engine.setLauncherClipTimelineBounds(index, scene_, next.duration, next.playStart, next.playEnd, next.loopStart,
                                                               next.loopEnd, next.looping);
        if (done && by > 0.0) {
            // The view stays on the music.
            scroll_ += by;
            cursor_ += by;
            selectionStart_ += by;
            selectionEnd_ += by;
        }
        return done;
    }
    if (eventsChanged)
        if (const auto done = engine.replaceAudioClipEvents(index, clip.id, std::move(events)); done.failed()) {
            report(done.getErrorMessage(), true);
            return false;
        }
    return engine.setAudioClipTimelineBounds(index, clip.id, next.duration, next.playStart, next.playEnd, next.loopStart, next.loopEnd,
                                             next.looping);
}

void AudioEditorView::addEvent(const TrackRef& track, const AudioClip& clip, const juce::File& file, const double at)
{
    if (!clip.usesEventTimeline && (clip.takes.size() > 1 || !clip.comp.empty())) {
        report(ui::Ls("Ce clip a plusieurs prises : mettez-les d'abord à plat (vue Prises)",
                      "This clip has several takes: flatten them first (Takes view)"),
               true);
        return;
    }
    std::uint64_t added {};
    auto& engine = studio_.engine();
    if (launcher()) {
        remember(track, ui::Ls("Son ajouté", "Sound added"));
        // A launcher clip of one sound keeps it as its first event.
        if (!clip.usesEventTimeline && clip.sourceFile != juce::File() && launcherEvent(track, clip, clip.id) == 0) {
            report(ui::Ls("Le clip n'a pas pu devenir une suite de sons", "The clip could not become a timeline of sounds"), true);
            return;
        }
    }
    const auto done = launcher() ? engine.addLauncherClipEventFromFile(track.audioIndex, scene_, file, std::max(0.0, at), &added)
                                 : engine.addAudioClipEventFromFile(track.audioIndex, clip.id, file, std::max(0.0, at), &added);
    if (done.failed()) {
        report(done.getErrorMessage(), true);
        return;
    }
    selectedEvent_ = added;
    studio_.markDirty();
    report(ui::Ls("Son ajouté au clip : ", "Sound added to the clip: ") + file.getFileName(), false);
}

void AudioEditorView::process(const TrackRef& track, const AudioClip& clip, const AudioEvent& event, const app::AudioEditRequest& request,
                              const juce::String& label, const std::optional<std::pair<double, double>> part)
{
    const auto start = part ? part->first : -1.0;
    const auto length = part ? part->second : 0.0;
    juce::Result done = juce::Result::ok();
    if (launcher()) {
        remember(track, label);
        const auto id = launcherEvent(track, clip, event.id);
        done = id != 0 ? studio_.editLauncherAudioEvent(track, scene_, id, request, start, length)
                       : juce::Result::fail(ui::Ls("Son introuvable", "No such sound"));
    } else if (!clip.usesEventTimeline && event.id == clip.id) {
        // The clip's own sound: a new version, the history kept.
        done = studio_.editAudioClip(track, clip.id, request, part ? part->first : event.sourceOffsetSeconds,
                                     part ? part->second : event.sourceDurationSeconds, label);
    } else {
        done = studio_.editAudioEvent(track, clip.id, event.id, request, start, length);
    }
    if (done.wasOk())
        studio_.markDirty();
    report(done.wasOk() ? label + ui::Ls(" : fait (Ctrl+Z l'annule)", ": done (Ctrl+Z undoes it)") : done.getErrorMessage(), done.failed());
}

void AudioEditorView::duplicate(const TrackRef& track, const AudioClip& clip, const std::vector<AudioEvent>& events, const std::uint64_t event)
{
    const auto found = std::find_if(events.begin(), events.end(), [event](const auto& e) { return e.id == event; });
    if (found == events.end())
        return;
    auto all = events;
    auto copy = *found;
    copy.id = 0;
    copy.startSeconds = found->startSeconds + found->durationSeconds;
    for (auto& marker : copy.stretchMarkers)
        marker.id = 0;
    all.push_back(copy);
    if (const auto done = replaceEvents(track, clip, std::move(all)); done.wasOk())
        studio_.markDirty();
    else
        report(done.getErrorMessage(), true);
}

void AudioEditorView::fitToTempo(const TrackRef& track, const AudioClip& clip, const AudioEvent& event)
{
    if (event.sourceTempoBpm <= 0.0) {
        report(ui::Ls("Le tempo du son n'est pas connu : détectez-le ou tapez-le", "The sound's tempo is not known: detect it or type it"), true);
        return;
    }
    const auto length = event.sourceDurationSeconds * event.sourceTempoBpm / std::max(1.0, studio_.engine().tempo());
    if (updateEvent(track, clip, event.id, event.startSeconds, event.sourceOffsetSeconds, length, event.sourceDurationSeconds,
                    std::min(event.fadeInSeconds, length * 0.5), std::min(event.fadeOutSeconds, length * 0.5)))
        studio_.markDirty();
}

std::optional<std::pair<double, double>> AudioEditorView::selectionIn(const AudioEvent& event) const
{
    if (selectionEnd_ - selectionStart_ < 1.0e-4)
        return std::nullopt;
    const auto a = std::max(selectionStart_, event.startSeconds);
    const auto b = std::min(selectionEnd_, event.startSeconds + event.durationSeconds);
    if (b - a < 1.0e-4)
        return std::nullopt;
    const auto s0 = app::AudioTrackController::eventSourceTime(event, a - event.startSeconds);
    const auto s1 = app::AudioTrackController::eventSourceTime(event, b - event.startSeconds);
    return std::make_pair(std::min(s0, s1), std::abs(s1 - s0));
}

// ---- The panel --------------------------------------------------------------------------------

void AudioEditorView::draw(const TrackRef& track, const AudioClip& clip)
{
    scene_ = -1;
    drawClip(track, clip);
}

void AudioEditorView::drawLauncher(const TrackRef& track, const int scene, const LauncherClip& clip)
{
    scene_ = scene;
    launcherClip_ = clip;
    drawClip(track, launcherView(track, scene, clip));
}

void AudioEditorView::drawClip(const TrackRef& track, const AudioClip& clip)
{
    if (const auto face = std::exchange(state_.audioEditorFace, -1); face >= 0)
        mode_ = face == 1 && !launcher() ? Mode::takes : Mode::clip;
    if (launcher())
        mode_ = Mode::clip;
    const auto events = eventsOf(clip);
    if (shownClip_ != clip.id) {
        shownClip_ = clip.id;
        pixelsPerSecond_ = 0.0;
        scroll_ = 0.0;
        cursor_ = 0.0;
        selectionStart_ = selectionEnd_ = 0.0;
        selectedEvent_ = events.size() == 1 ? events.front().id : 0;
        drag_ = Drag::none;
        compLane_ = -1;
        curveEditors_.clear();
        focusedLane_.clear();
    }
    if (std::none_of(events.begin(), events.end(), [this](const auto& e) { return e.id == selectedEvent_; }))
        selectedEvent_ = 0;
    gridOrigin_ = boundsOf(clip).loopStart;
    auto& engine = studio_.engine();
    // Hearing a sound or a part stops at its end.
    if (previewEnd_ && (!engine.isSamplePreviewing() || engine.samplePreviewPositionSeconds() >= *previewEnd_)) {
        engine.stopSamplePreview();
        previewEnd_.reset();
    }
    if (previewEnd_ || engine.isAudioClipPreviewing() || engine.isPlaying() || (launcher() && studio_.playingScene(track) == scene_))
        host_.wake(2);
    drawToolbar(track, clip, events);
    if (mode_ == Mode::takes) {
        drawTakes(track, clip);
        return;
    }
    // The automation points picked: their values, at the top.
    drawCurveBar(track, clip);
    handleKeys(track, clip, events);
    const auto available = ImGui::GetContentRegionAvail();
    const auto sideWidth = std::clamp(available.x * 0.3f, dp(240.0f), dp(330.0f));
    const auto mainWidth = std::max(dp(160.0f), available.x - sideWidth - dp(6.0f));
    if (ImGui::BeginChild("##audioMain", ImVec2(mainWidth, available.y), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        const auto& p = theme::palette();
        auto* list = ImGui::GetWindowDrawList();
        const auto start = ImGui::GetCursorScreenPos();
        const auto full = ImGui::GetContentRegionAvail();
        // A column of cells on the left, as Bitwig has: what each row is.
        const auto headerWidth = std::clamp(full.x * 0.14f, dp(120.0f), dp(170.0f));
        laneLeft_ = start.x + headerWidth;
        laneWidth_ = std::max(dp(80.0f), full.x - headerWidth);
        if (pixelsPerSecond_ <= 0.0)
            fit(clip, events);
        const auto wrapAt = laneLeft_ - ImGui::GetWindowPos().x - dp(6.0f);
        ImGui::SetCursorScreenPos(ImVec2(laneLeft_, start.y));
        drawOverview(clip, events, ImVec2(laneWidth_, dp(22.0f)));
        // The ruler, its cell saying the loop in bars.
        const auto rulerTop = start.y + dp(24.0f);
        const auto rulerTall = dp(50.0f);
        {
            const auto b = boundsOf(clip);
            const auto bar = beatSeconds() * std::max(1, studio_.engine().timeSignatureNumerator());
            list->AddRectFilled(ImVec2(start.x, rulerTop), ImVec2(laneLeft_ - dp(2.0f), rulerTop + rulerTall), p.bg1);
            ImGui::SetCursorScreenPos(ImVec2(start.x + dp(4.0f), rulerTop + dp(4.0f)));
            ImGui::PushTextWrapPos(wrapAt);
            ImGui::TextColored(theme::vec(p.textDim), "%s %.2f %s", b.looping ? L("Boucle :", "Loop:") : L("Joue :", "Plays:"),
                               (b.looping ? b.loopEnd - b.loopStart : b.playEnd - b.playStart) / std::max(1.0e-6, bar), L("mesures", "bars"));
            ImGui::TextColored(theme::vec(p.textFaint), "%s", L("En haut : la boucle ; en bas : début et fin", "Top: the loop; bottom: start and stop"));
            ImGui::PopTextWrapPos();
        }
        ImGui::SetCursorScreenPos(ImVec2(laneLeft_, rulerTop));
        drawRuler(track, clip, events, ImVec2(laneWidth_, rulerTall));
        // The sounds, then the clip's automation under them.
        const auto eventsTop = rulerTop + rulerTall;
        const auto left = start.y + full.y - eventsTop;
        const auto rows = static_cast<float>(laneRows(clip, events).size());
        // The lanes as tall as they need, or as dragged by the bar between
        // them and the sounds.
        const auto splitter = dp(6.0f);
        const auto automatic = std::clamp(dp(70.0f) * rows, std::min(dp(56.0f), left * 0.4f), left * 0.5f);
        const auto lanesTall = state_.audioClipLanesHeight > 0.0f
                                   ? std::clamp(dp(state_.audioClipLanesHeight), std::min(dp(40.0f), left * 0.3f),
                                                std::max(dp(40.0f), left - dp(64.0f)))
                                   : automatic;
        const auto eventsTall = std::max(dp(60.0f), left - lanesTall - splitter);
        list->AddRectFilled(ImVec2(start.x, eventsTop), ImVec2(laneLeft_ - dp(2.0f), eventsTop + eventsTall), p.bg1);
        ImGui::SetCursorScreenPos(ImVec2(start.x + dp(4.0f), eventsTop + dp(4.0f)));
        ui::sectionTitle((ui::Ls("SONS (", "SOUNDS (") + juce::String(static_cast<int>(events.size())) + ")").toRawUTF8());
        ImGui::SetCursorScreenPos(ImVec2(start.x + dp(4.0f), ImGui::GetCursorScreenPos().y));
        ImGui::PushTextWrapPos(wrapAt);
        ImGui::TextColored(theme::vec(p.textFaint), "%s", L("Double-clic en bas : point d'étirement. En haut : couper. Clic droit : menu. Molette : zoom (Maj : défiler).",
                                                            "Double click low: stretch point. High: cut. Right click: menu. Wheel: zoom (Shift: scroll)."));
        ImGui::PopTextWrapPos();
        ImGui::SetCursorScreenPos(ImVec2(laneLeft_, eventsTop));
        drawEvents(track, clip, events, ImVec2(laneWidth_, eventsTall));
        eventMenu(track, clip, events);
        {
            // The bar to drag: the lanes taller or shorter (a double click:
            // as tall as they need).
            const ImVec2 barMin(start.x, eventsTop + eventsTall);
            ImGui::SetCursorScreenPos(barMin);
            ImGui::InvisibleButton("##lanesSplitter", ImVec2(std::max(1.0f, full.x), splitter));
            const auto hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
            if (hot)
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            if (ImGui::IsItemActive() && ImGui::GetIO().MouseDelta.y != 0.0f)
                state_.audioClipLanesHeight = std::max(10.0f, (lanesTall - ImGui::GetIO().MouseDelta.y) / dp(1.0f));
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                state_.audioClipLanesHeight = 0.0f;
            ui::tooltip(L("Glisser : agrandir ou réduire les couloirs d'automation · double-clic : leur taille de base",
                          "Drag: make the automation lanes taller or shorter · double click: their usual size"));
            const auto y = barMin.y + splitter * 0.5f;
            list->AddLine(ImVec2(start.x, y), ImVec2(start.x + full.x, y), hot ? p.accent : p.line, hot ? dp(2.0f) : dp(1.0f));
            for (auto dot = -2; dot <= 2; ++dot)
                list->AddCircleFilled(ImVec2(start.x + full.x * 0.5f + static_cast<float>(dot) * dp(6.0f), y), dp(1.5f), hot ? p.accent : p.textFaint);
        }
        drawLanes(track, clip, start.x, eventsTop + eventsTall + splitter, lanesTall);
    }
    ImGui::EndChild();
    ImGui::SameLine(0.0f, dp(6.0f));
    drawSide(track, clip, events, ImVec2(sideWidth, available.y));
}

// ---- The toolbar ------------------------------------------------------------------------------

void AudioEditorView::drawToolbar(const TrackRef& track, const AudioClip& clip, const std::vector<AudioEvent>& events)
{
    const auto& p = theme::palette();
    auto& engine = studio_.engine();
    const auto height = ImGui::GetFrameHeight();
    const auto chip = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(chip.x, chip.y + height * 0.2f), ImVec2(chip.x + dp(4.0f), chip.y + height * 0.8f),
                                              theme::fromArgb(clip.colour != 0 ? clip.colour : 0xffff9b2fu), dp(2.0f));
    ImGui::Dummy(ImVec2(dp(6.0f), height));
    ImGui::SameLine(0.0f, dp(4.0f));
    ImGui::PushFont(theme::boldFont(), 0.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted((clip.name.isNotEmpty() ? clip.name : clip.sourceFile.getFileNameWithoutExtension()).toRawUTF8());
    ImGui::PopFont();
    ImGui::SameLine();
    const auto where = launcher() ? ui::Ls("lanceur, scène ", "launcher, scene ") + juce::String(scene_ + 1) : ui::Ls("arrangement", "arrangement");
    ImGui::TextDisabled("%s  |  %s", studio_.trackName(track).toRawUTF8(), where.toRawUTF8());
    ImGui::SameLine(0.0f, dp(14.0f));
    // Heard: a launcher clip from its slot, an arrangement one as the track plays it.
    if (launcher()) {
        const auto playing = studio_.playingScene(track) == scene_;
        if (ui::iconButton(playing ? icon::stop : icon::play,
                           playing ? L("Arrêter", "Stop") : L("Lancer le clip, comme le lanceur le joue", "Launch the clip, as the launcher plays it"),
                           playing, p.play)) {
            if (playing)
                studio_.stopTrackClips(track);
            else if (!studio_.launchSlot(track, scene_))
                report(ui::Ls("Le clip ne peut pas être lancé", "The clip cannot be launched"), true);
        }
    } else {
        const auto playing = engine.isAudioClipPreviewing();
        if (ui::iconButton(playing ? icon::stop : icon::play,
                           playing ? L("Arrêter l'écoute", "Stop listening")
                                   : L("Écouter le clip comme la piste le joue (étiré, transposé, ses prises)",
                                       "Hear the clip as the track plays it (stretched, transposed, its takes)"),
                           playing, p.play)) {
            if (playing)
                engine.stopAudioClipPreview();
            else if (const auto started = engine.startAudioClipPreview(track.audioIndex, clip.id, 0.0); started.failed())
                report(started.getErrorMessage(), true);
        }
    }
    ui::toolbarSeparator();
    const auto canUndo = launcher() ? !undo_.empty() : engine.canUndoArrangementEdit();
    const auto canRedo = launcher() ? !redo_.empty() : engine.canRedoArrangementEdit();
    ImGui::BeginDisabled(!canUndo);
    if (ui::iconButton(icon::undo, L("Annuler (Ctrl+Z)", "Undo (Ctrl+Z)"))) {
        if (launcher())
            (void) undoLauncher(false);
        else
            (void) engine.undoArrangementEdit();
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, dp(2.0f));
    ImGui::BeginDisabled(!canRedo);
    if (ui::iconButton(icon::redo, L("Rétablir (Ctrl+Y)", "Redo (Ctrl+Y)"))) {
        if (launcher())
            (void) undoLauncher(true);
        else
            (void) engine.redoArrangementEdit();
    }
    ImGui::EndDisabled();
    ui::toolbarSeparator();
    // On the sound picked.
    const auto selected = std::find_if(events.begin(), events.end(), [this](const auto& e) { return e.id == selectedEvent_; });
    const auto under = std::find_if(events.begin(), events.end(), [this](const auto& e) {
        return cursor_ > e.startSeconds + 1.0e-4 && cursor_ < e.startSeconds + e.durationSeconds - 1.0e-4;
    });
    ImGui::BeginDisabled(under == events.end());
    if (ui::iconButton(icon::trim, L("Couper le son au curseur (S)", "Cut the sound at the cursor (S)")) && under != events.end()) {
        if (const auto made = splitEvent(track, clip, under->id, cursor_); made != 0) {
            selectedEvent_ = made;
            studio_.markDirty();
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, dp(2.0f));
    ImGui::BeginDisabled(selected == events.end());
    if (ui::iconButton(icon::copy, L("Dupliquer le son, juste après lui (Ctrl+D)", "Duplicate the sound, right after it (Ctrl+D)"))
        && selected != events.end())
        duplicate(track, clip, events, selected->id);
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(icon::trash, L("Supprimer le son (Suppr)", "Delete the sound (Delete)")) && selected != events.end()
        && removeEvent(track, clip, selected->id)) {
        selectedEvent_ = 0;
        studio_.markDirty();
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(icon::add, L("Ajouter un son au curseur… (ou glissez-en un du navigateur)",
                                    "Add a sound at the cursor… (or drag one from the browser)"))) {
        const auto clipId = clip.id;
        const auto at = cursor_;
        const auto scene = scene_;
        chooser_ = std::make_unique<juce::FileChooser>(ui::Ls("Un son pour ce clip", "A sound for this clip"), juce::File(),
                                                       "*.wav;*.wave;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, track, clipId, at, scene](const juce::FileChooser& chosen) {
                                  const auto file = chosen.getResult();
                                  if (file == juce::File())
                                      return;
                                  scene_ = scene;
                                  if (scene >= 0) {
                                      if (const auto slot = studio_.engine().launcherClip(track.audioIndex, scene)) {
                                          launcherClip_ = *slot;
                                          addEvent(track, launcherView(track, scene, *slot), file, at);
                                      }
                                  } else if (const auto found = studio_.engine().audioClip(track.audioIndex, clipId)) {
                                      addEvent(track, *found, file, at);
                                  }
                              });
    }
    ui::toolbarSeparator();
    // The view.
    if (ui::iconButton(icon::zoomIn, L("Zoom (sur la sélection quand il y en a une)", "Zoom in (to the selection when there is one)"))) {
        if (selectionEnd_ - selectionStart_ > 1.0e-4) {
            pixelsPerSecond_ = static_cast<double>(laneWidth_) / (selectionEnd_ - selectionStart_);
            scroll_ = selectionStart_;
        } else {
            const auto centre = scroll_ + laneWidth_ * 0.5 / pixelsPerSecond_;
            pixelsPerSecond_ = std::min(96'000.0, pixelsPerSecond_ * 1.6);
            scroll_ = std::max(minScroll(clip, events), centre - laneWidth_ * 0.5 / pixelsPerSecond_);
        }
    }
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(icon::zoomOut, L("Dézoomer", "Zoom out"))) {
        const auto centre = scroll_ + laneWidth_ * 0.5 / pixelsPerSecond_;
        pixelsPerSecond_ = std::max(static_cast<double>(laneWidth_) / viewLength(clip, events) * 0.5, pixelsPerSecond_ / 1.6);
        scroll_ = std::max(minScroll(clip, events), centre - laneWidth_ * 0.5 / pixelsPerSecond_);
    }
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(icon::fullScreen, L("Tout voir", "See it all")))
        fit(clip, events);
    ui::toolbarSeparator();
    (void) ui::snapToggle("audioClipSnap", &state_.audioClipSnap);
    ImGui::SameLine(0.0f, dp(6.0f));
    // Its loop, at hand.
    const auto bounds = boundsOf(clip);
    auto looping = bounds.looping;
    if (ui::loopToggle("audioClipLoop", &looping,
                       L("Boucle : le clip boucle entre les bornes de sa boucle (la barre du haut)",
                         "Loop: the clip loops between its loop's ends (the bar on top)"))
        && setBounds(track, clip, bounds.duration, bounds.playStart, bounds.playEnd, bounds.loopStart, bounds.loopEnd, looping))
        studio_.markDirty();
    if (!launcher()) {
        ImGui::SameLine(0.0f, dp(6.0f));
        auto takes = mode_ == Mode::takes;
        char label[48];
        std::snprintf(label, sizeof(label), "%s (%d)", L("Prises", "Takes"), static_cast<int>(std::max<std::size_t>(1, clip.takes.size())));
        if (ui::toggle(label, &takes, p.accent, ImVec2(0.0f, 0.0f),
                       L("Les prises du clip, l'une sous l'autre : glissez sur une prise pour jouer ce passage-là (comping)",
                         "The clip's takes, one under the other: drag over a take to play that passage from it (comping)")))
            mode_ = takes ? Mode::takes : Mode::clip;
    }
    ImGui::SameLine(0.0f, dp(12.0f));
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", L("Double-clic en bas d'un son : point d'étirement · en haut : couper · clic droit : menu",
                                "Double click low on a sound: stretch point · high: cut · right click: menu"));
}

// ---- The whole clip, small --------------------------------------------------------------------

void AudioEditorView::drawOverview(const AudioClip& clip, const std::vector<AudioEvent>& events, const ImVec2 size)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto origin = ImGui::GetCursorScreenPos();
    const ImVec2 end(origin.x + size.x, origin.y + size.y);
    ImGui::InvisibleButton("##audioOverview", size);
    list->AddRectFilled(origin, end, p.bg0, dp(2.0f));
    const auto from = std::min(0.0, scroll_);
    const auto total = viewLength(clip, events) - from;
    const auto xAt = [&](const double t) { return origin.x + static_cast<float>((t - from) / total) * size.x; };
    const auto bounds = shownBounds(clip);
    list->AddRectFilled(ImVec2(xAt(bounds.playStart), origin.y), ImVec2(xAt(bounds.playEnd), end.y), theme::alpha(p.bg3, 0.5f));
    const auto centre = origin.y + size.y * 0.5f;
    for (const auto& event : events) {
        auto* thumbnail = waveforms_.get(event.sourceFile);
        if (thumbnail == nullptr)
            continue;
        const auto colour = theme::alpha(theme::fromArgb(event.colour != 0 ? event.colour : 0xff53d0c2u), 0.8f);
        for (auto x = std::max(origin.x, xAt(event.startSeconds)); x < std::min(end.x, xAt(event.startSeconds + event.durationSeconds));
             x += 1.0f) {
            const auto t0 = from + static_cast<double>(x - origin.x) / size.x * total - event.startSeconds;
            const auto t1 = from + static_cast<double>(x + 1.0f - origin.x) / size.x * total - event.startSeconds;
            const auto s0 = app::AudioTrackController::eventSourceTime(event, t0);
            const auto s1 = app::AudioTrackController::eventSourceTime(event, t1);
            float low = 0.0f;
            float high = 0.0f;
            thumbnail->getApproximateMinMax(std::min(s0, s1), std::max(s0, s1) + 1.0e-4, 0, low, high);
            list->AddRectFilled(ImVec2(x, centre - std::min(1.0f, high) * size.y * 0.46f), ImVec2(x + 1.0f, centre - std::max(-1.0f, low) * size.y * 0.46f + 1.0f),
                                colour);
        }
    }
    // The part seen.
    const auto x0 = xAt(scroll_);
    const auto x1 = xAt(scroll_ + laneWidth_ / std::max(1.0e-9, pixelsPerSecond_));
    list->AddRectFilled(ImVec2(x0, origin.y), ImVec2(std::max(x0 + 2.0f, x1), end.y), theme::alpha(p.accent, 0.12f));
    list->AddRect(ImVec2(x0, origin.y), ImVec2(std::max(x0 + 2.0f, x1), end.y), theme::alpha(p.accent, 0.7f));
    list->AddLine(ImVec2(xAt(cursor_), origin.y), ImVec2(xAt(cursor_), end.y), p.accent);
    if (ImGui::IsItemActive()) {
        const auto span = laneWidth_ / std::max(1.0e-9, pixelsPerSecond_);
        const auto at = from + std::clamp((ImGui::GetIO().MousePos.x - origin.x) / size.x, 0.0f, 1.0f) * total;
        scroll_ = std::clamp(at - span * 0.5, minScroll(clip, events), std::max(0.0, from + total - span * 0.5));
    }
    ui::tooltip(L("Tout le clip : glissez pour voir une autre partie", "The whole clip: drag to see another part"));
    list->AddRect(origin, end, p.line, dp(2.0f));
}

// ---- The ruler: the clip's bar, bars and beats -----------------------------------------------------

void AudioEditorView::drawRuler(const TrackRef& track, const AudioClip& clip, const std::vector<AudioEvent>& events, const ImVec2 size)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto top = ImGui::GetCursorScreenPos();
    const auto bandHeight = dp(12.0f);
    const auto loopHeight = dp(10.0f);
    const auto flagHeight = dp(14.0f);
    const ImVec2 barMin(top.x, top.y + bandHeight);
    const ImVec2 loopMax(top.x + size.x, barMin.y + loopHeight);
    const ImVec2 barMax(top.x + size.x, loopMax.y + flagHeight);
    const ImVec2 labelsMin(top.x, barMax.y);
    const ImVec2 end(top.x + size.x, top.y + size.y);
    const auto beat = beatSeconds();
    const auto numerator = std::max(1, studio_.engine().timeSignatureNumerator());
    const auto rulerDrag = drag_ == Drag::playStart || drag_ == Drag::playEnd || drag_ == Drag::loopStart || drag_ == Drag::loopEnd
                           || drag_ == Drag::loopMove;
    // The grab and zoom band (before the clip too: its start can move there).
    const auto fitted = static_cast<double>(size.x) / 600.0;
    if (ui::timelineBand("##audioBand", top, ImVec2(top.x + size.x, barMin.y), scroll_, pixelsPerSecond_, fitted, 96'000.0, beat, beat * numerator,
                         minScroll(clip, events)))
        pixelsPerSecond_ = 0.0;
    const auto shown = rulerDrag ? draft_ : boundsOf(clip);
    list->PushClipRect(barMin, end, true);
    list->AddRectFilled(barMin, barMax, p.bg1);
    // Before the clip's content: nothing yet.
    list->AddRectFilled(ImVec2(xOf(minScroll(clip, events) - 1000.0), barMin.y), ImVec2(xOf(0.0), barMax.y), theme::alpha(p.bg0, 0.8f));
    // The loop, on top: a bracket over its part (dim when off).
    const auto loopA = xOf(shown.loopStart);
    const auto loopB = xOf(shown.loopEnd);
    const auto loopColour = shown.looping ? p.accent : theme::alpha(p.textFaint, 0.5f);
    list->AddRectFilled(ImVec2(loopA, barMin.y + dp(2.0f)), ImVec2(loopB, loopMax.y - dp(2.0f)), theme::alpha(loopColour, shown.looping ? 0.9f : 0.35f));
    list->AddRectFilled(ImVec2(loopA, barMin.y + dp(1.0f)), ImVec2(loopA + dp(3.0f), loopMax.y), loopColour);
    list->AddRectFilled(ImVec2(loopB - dp(3.0f), barMin.y + dp(1.0f)), ImVec2(loopB, loopMax.y), loopColour);
    // Below: its content, where it plays, its flags (start pointing in, stop).
    list->AddRectFilled(ImVec2(xOf(0.0), loopMax.y + dp(4.0f)), ImVec2(xOf(std::max(shown.duration, shown.playEnd)), barMax.y - dp(4.0f)), p.bg3);
    const auto playColour = theme::fromArgb(clip.colour != 0 ? clip.colour : 0xffff9b2fu);
    const auto startX = xOf(shown.playStart);
    const auto stopX = xOf(shown.playEnd);
    list->AddRectFilled(ImVec2(startX, loopMax.y + dp(5.0f)), ImVec2(stopX, barMax.y - dp(5.0f)), theme::alpha(playColour, 0.65f));
    const auto flag = dp(9.0f);
    list->AddTriangleFilled(ImVec2(startX, loopMax.y + dp(1.0f)), ImVec2(startX, barMax.y - dp(1.0f)), ImVec2(startX + flag, (loopMax.y + barMax.y) * 0.5f), p.text);
    list->AddTriangleFilled(ImVec2(stopX, loopMax.y + dp(1.0f)), ImVec2(stopX - flag, (loopMax.y + barMax.y) * 0.5f), ImVec2(stopX, barMax.y - dp(1.0f)), p.text);
    // Bars and beats, counted from the loop's start (the clip's 1.1).
    list->AddRectFilled(labelsMin, end, p.bg1);
    ImGui::PushFont(nullptr, ui::currentFontBase() * 0.76f);
    const auto beatPixels = beat * pixelsPerSecond_;
    const auto showBeats = beatPixels >= dp(26.0f);
    const auto everyBars = beatPixels * numerator >= dp(40.0f) ? 1 : beatPixels * numerator * 4 >= dp(40.0f) ? 4 : 16;
    const auto firstBeat = std::floor((scroll_ - gridOrigin_) / beat);
    for (auto k = firstBeat; gridOrigin_ + k * beat < scroll_ + size.x / pixelsPerSecond_; k += 1.0) {
        const auto x = xOf(gridOrigin_ + k * beat);
        const auto whole = static_cast<long long>(std::llround(k));
        const auto onBar = ((whole % numerator) + numerator) % numerator == 0;
        const auto barNumber = static_cast<long long>(std::floor(static_cast<double>(whole) / numerator));
        if (onBar && ((barNumber % everyBars) + everyBars) % everyBars == 0) {
            list->AddLine(ImVec2(x, labelsMin.y), ImVec2(x, end.y), p.textFaint);
            list->AddText(ImVec2(x + dp(2.0f), labelsMin.y + dp(1.0f)), p.textDim, barsBeats(k, numerator, false).toRawUTF8());
        } else if (showBeats) {
            list->AddLine(ImVec2(x, end.y - dp(5.0f)), ImVec2(x, end.y), theme::alpha(p.textFaint, 0.6f));
            if (beatPixels >= dp(44.0f))
                list->AddText(ImVec2(x + dp(2.0f), labelsMin.y + dp(1.0f)), theme::alpha(p.textDim, 0.7f), barsBeats(k, numerator, true).toRawUTF8());
        }
    }
    ImGui::PopFont();
    if (selectionEnd_ - selectionStart_ > 1.0e-4)
        list->AddRectFilled(ImVec2(xOf(selectionStart_), end.y - dp(3.0f)), ImVec2(xOf(selectionEnd_), end.y), p.accent);
    list->PopClipRect();

    // The bar: the loop on its upper half, the flags on its lower half.
    ImGui::SetCursorScreenPos(barMin);
    ImGui::InvisibleButton("##clipBar", ImVec2(size.x, barMax.y - barMin.y), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const auto mouse = ImGui::GetIO().MousePos;
    const auto at = secondsAt(mouse.x);
    const auto near = [&](const float x) { return std::abs(mouse.x - x) <= dp(7.0f); };
    const auto onLoop = mouse.y < loopMax.y;
    if (ImGui::IsItemHovered() && !rulerDrag) {
        const char* tip = nullptr;
        if (onLoop)
            tip = near(loopA)                      ? L("Début de la boucle : glissez", "Loop start: drag")
                  : near(loopB)                    ? L("Fin de la boucle : glissez", "Loop end: drag")
                  : at > shown.loopStart && at < shown.loopEnd ? L("La boucle : glissez pour la déplacer ; clic droit : son menu",
                                                                   "The loop: drag to move it; right click: its menu")
                                                               : L("La boucle", "The loop");
        else
            tip = near(startX) ? L("Début : où le clip commence à jouer. Glissez-le, même avant le son.",
                                   "Start: where the clip starts playing. Drag it, even before the sound.")
                  : near(stopX) ? L("Fin : où le clip s'arrête. Glissez-la.", "Stop: where the clip stops. Drag it.")
                                : L("Début et fin du clip (drapeaux) ; la boucle au-dessus. Ce qui est avant le début reste dans le clip.",
                                    "The clip's start and stop (flags); its loop above. What lies before the start stays in the clip.");
        ui::tooltip(tip);
        if ((onLoop && (near(loopA) || near(loopB))) || (!onLoop && (near(startX) || near(stopX))))
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        else if (onLoop && at > shown.loopStart && at < shown.loopEnd)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    if (ImGui::IsItemActivated() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        grab_ = at;
        draft_ = boundsOf(clip);
        originStart_ = draft_.loopStart;
        originDuration_ = draft_.loopEnd - draft_.loopStart;
        if (onLoop && near(loopA) && !(near(loopB) && mouse.x > loopA + dp(3.0f)))
            drag_ = Drag::loopStart;
        else if (onLoop && near(loopB))
            drag_ = Drag::loopEnd;
        else if (onLoop && at > draft_.loopStart && at < draft_.loopEnd)
            drag_ = Drag::loopMove;
        else if (!onLoop && near(startX) && !(near(stopX) && mouse.x > startX + flag * 0.5f))
            drag_ = Drag::playStart;
        else if (!onLoop && near(stopX))
            drag_ = Drag::playEnd;
        else
            cursor_ = snap(at);
    }
    if (ImGui::IsItemActive() && rulerDrag) {
        // Before the clip's content only for a launcher clip (its content then
        // moves later); an arrangement clip's starts at its place.
        const auto floor = launcher() ? minScroll(clip, events) : 0.0;
        auto d = boundsOf(clip);
        const auto value = std::max(floor, snap(at));
        switch (drag_) {
        case Drag::playStart:
            d.playStart = std::min(value, d.playEnd - 0.001);
            if (d.loopStart < d.playStart)
                d.loopStart = d.playStart;
            if (d.loopEnd <= d.loopStart + 0.001)
                d.loopEnd = d.playEnd;
            break;
        case Drag::playEnd:
            d.playEnd = std::max(value, d.playStart + 0.001);
            d.duration = std::max(d.duration, d.playEnd);
            if (d.loopEnd > d.playEnd)
                d.loopEnd = d.playEnd;
            if (d.loopStart >= d.loopEnd - 0.001)
                d.loopStart = d.playStart;
            break;
        case Drag::loopStart:
            d.loopStart = std::min(value, d.loopEnd - 0.001);
            if (d.playStart > d.loopStart)
                d.playStart = d.loopStart;
            break;
        case Drag::loopEnd:
            d.loopEnd = std::max(value, d.loopStart + 0.001);
            if (d.playEnd < d.loopEnd) {
                d.playEnd = d.loopEnd;
                d.duration = std::max(d.duration, d.playEnd);
            }
            break;
        case Drag::loopMove: {
            const auto start = std::max(floor, snap(originStart_ + (at - grab_)));
            d.loopStart = start;
            d.loopEnd = start + originDuration_;
            d.playStart = std::min(d.playStart, d.loopStart);
            d.playEnd = std::max(d.playEnd, d.loopEnd);
            d.duration = std::max(d.duration, d.playEnd);
            break;
        }
        default: break;
        }
        draft_ = d;
    }
    if (ImGui::IsItemDeactivated() && rulerDrag) {
        drag_ = Drag::none;
        if (commitTimeline(track, clip, events, draft_, ui::Ls("Début, fin et boucle du clip", "The clip's start, stop and loop")))
            studio_.markDirty();
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        ImGui::OpenPopup("##clipBarMenu");
    if (ImGui::BeginPopup("##clipBarMenu")) {
        auto now = boundsOf(clip);
        const auto selected = selectionEnd_ - selectionStart_ > 1.0e-4;
        if (ImGui::MenuItem(L("Boucle", "Loop"), nullptr, now.looping)) {
            now.looping = !now.looping;
            if (commitTimeline(track, clip, events, now, ui::Ls("Boucle", "Loop")))
                studio_.markDirty();
        }
        if (ImGui::MenuItem(L("Boucle sur la sélection", "Loop the selection"), nullptr, false, selected)) {
            now.loopStart = selectionStart_;
            now.loopEnd = selectionEnd_;
            now.looping = true;
            now.playStart = std::min(now.playStart, now.loopStart);
            now.playEnd = std::max(now.playEnd, now.loopEnd);
            now.duration = std::max(now.duration, now.playEnd);
            if (commitTimeline(track, clip, events, now, ui::Ls("Boucle sur la sélection", "Loop the selection")))
                studio_.markDirty();
        }
        if (ImGui::MenuItem(L("Jouer la sélection seule", "Play the selection alone"), nullptr, false, selected)) {
            now.playStart = now.loopStart = selectionStart_;
            now.playEnd = now.loopEnd = selectionEnd_;
            now.duration = std::max(now.duration, now.playEnd);
            if (commitTimeline(track, clip, events, now, ui::Ls("Plage du clip", "Clip range")))
                studio_.markDirty();
        }
        if (ImGui::MenuItem(L("Début au curseur", "Start at the cursor"))) {
            now.playStart = std::min(cursor_, now.playEnd - 0.001);
            now.loopStart = std::max(now.loopStart, now.playStart);
            if (commitTimeline(track, clip, events, now, ui::Ls("Début du clip", "Clip start")))
                studio_.markDirty();
        }
        if (ImGui::MenuItem(L("Début et boucle au début du son", "Start and loop at the sound's start"))) {
            auto first = now.duration;
            for (const auto& event : events)
                first = std::min(first, event.startSeconds);
            now.playStart = now.loopStart = std::clamp(first, 0.0, now.playEnd - 0.001);
            if (commitTimeline(track, clip, events, now, ui::Ls("Début du clip", "Clip start")))
                studio_.markDirty();
        }
        if (ImGui::MenuItem(L("Tout le clip joue", "The whole clip plays"))) {
            now.playStart = 0.0;
            now.playEnd = now.duration;
            if (!now.looping) {
                now.loopStart = 0.0;
                now.loopEnd = now.duration;
            }
            if (commitTimeline(track, clip, events, now, ui::Ls("Plage du clip", "Clip range")))
                studio_.markDirty();
        }
        ImGui::EndPopup();
    }
    // The labels: a drag selects a stretch of time (to process it).
    ImGui::SetCursorScreenPos(labelsMin);
    ImGui::InvisibleButton("##rulerLabels", ImVec2(size.x, std::max(1.0f, end.y - labelsMin.y)));
    if (ImGui::IsItemActivated()) {
        drag_ = Drag::select;
        grab_ = snap(at);
        selectionStart_ = selectionEnd_ = grab_;
    }
    if (ImGui::IsItemActive() && drag_ == Drag::select) {
        const auto value = snap(at);
        selectionStart_ = std::min(grab_, value);
        selectionEnd_ = std::max(grab_, value);
    }
    if (ImGui::IsItemDeactivated() && drag_ == Drag::select) {
        drag_ = Drag::none;
        if (selectionEnd_ - selectionStart_ < 2.0 / pixelsPerSecond_) {
            cursor_ = grab_;
            selectionStart_ = selectionEnd_ = 0.0;
        }
    }
    ui::tooltip(L("Mesures et temps depuis le début de la boucle (1.1). Glisser : sélectionner un passage ; clic : le curseur.",
                  "Bars and beats from the loop's start (1.1). Drag: select a passage; click: the cursor."));
    ImGui::SetCursorScreenPos(ImVec2(top.x, end.y));
}

// ---- The sounds -------------------------------------------------------------------------------

void AudioEditorView::drawEvents(const TrackRef& track, const AudioClip& clip, const std::vector<AudioEvent>& events, const ImVec2 size)
{
    const auto& p = theme::palette();
    auto& engine = studio_.engine();
    const auto index = track.audioIndex;
    auto* list = ImGui::GetWindowDrawList();
    const auto origin = ImGui::GetCursorScreenPos();
    const ImVec2 end(origin.x + size.x, origin.y + size.y);
    const auto bounds = shownBounds(clip);
    const auto mouse = ImGui::GetIO().MousePos;
    const auto at = secondsAt(mouse.x);
    const auto floor = launcher() ? minScroll(clip, events) : 0.0;

    list->PushClipRect(origin, end, true);
    list->AddRectFilled(origin, end, p.bg0);
    // Before the content's start: room to move the start or a sound there.
    if (xOf(0.0) > origin.x)
        list->AddRectFilled(origin, ImVec2(xOf(0.0), end.y), theme::alpha(p.bg1, 0.9f));
    // What plays brighter; before the start and after the stop, dimmed.
    list->AddRectFilled(ImVec2(xOf(bounds.playStart), origin.y), ImVec2(xOf(bounds.playEnd), end.y), theme::alpha(p.bg2, 0.55f));
    if (bounds.looping)
        list->AddRectFilled(ImVec2(xOf(bounds.loopStart), origin.y), ImVec2(xOf(bounds.loopEnd), end.y), theme::alpha(p.accent, 0.035f));
    // The grid as seen (bars, beats, their parts), behind the sounds.
    drawGrid(list, origin, end);
    list->AddLine(ImVec2(xOf(bounds.playStart), origin.y), ImVec2(xOf(bounds.playStart), end.y), theme::alpha(p.text, 0.5f));
    list->AddLine(ImVec2(xOf(bounds.playEnd), origin.y), ImVec2(xOf(bounds.playEnd), end.y), theme::alpha(p.text, 0.5f));

    // Rows: a sound goes on the first row free at its start.
    std::vector<std::size_t> order(events.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::sort(order.begin(), order.end(), [&events](const std::size_t a, const std::size_t b) { return events[a].startSeconds < events[b].startSeconds; });
    std::vector<int> rowOf(events.size(), 0);
    std::vector<double> rowEnds;
    for (const auto i : order) {
        auto row = 0;
        while (row < static_cast<int>(rowEnds.size()) && rowEnds[static_cast<std::size_t>(row)] > events[i].startSeconds + 1.0e-6)
            ++row;
        if (row == static_cast<int>(rowEnds.size()))
            rowEnds.push_back(0.0);
        rowEnds[static_cast<std::size_t>(row)] = events[i].startSeconds + events[i].durationSeconds;
        rowOf[i] = row;
    }
    const auto rows = std::max<std::size_t>(1, rowEnds.size());
    const auto rowHeight = std::clamp(size.y / static_cast<float>(rows), dp(44.0f), dp(260.0f));
    const auto header = dp(14.0f);
    const auto footer = dp(10.0f);

    // Where a sound is drawn: as it is, or as the drag puts it.
    struct Shown {
        double start {};
        double offset {};
        double duration {};
        double sourceDuration {};
        double fadeIn {};
        double fadeOut {};
        std::vector<app::AudioTrackController::StretchMarker> markers;
    };
    const auto shownOf = [this](const AudioEvent& event) {
        Shown shown { event.startSeconds,    event.sourceOffsetSeconds, event.durationSeconds, event.sourceDurationSeconds,
                      event.fadeInSeconds,   event.fadeOutSeconds,      event.stretchMarkers };
        if (event.id != draggedEvent_)
            return shown;
        switch (drag_) {
        case Drag::move:
        case Drag::trimStart:
        case Drag::trimEnd:
            shown.start = shownStart_;
            shown.offset = shownOffset_;
            shown.duration = shownDuration_;
            shown.sourceDuration = shownSourceDuration_;
            break;
        case Drag::fadeIn: shown.fadeIn = shownValue_; break;
        case Drag::fadeOut: shown.fadeOut = shownValue_; break;
        case Drag::marker:
            for (auto& marker : shown.markers)
                if (marker.id == draggedMarker_)
                    marker.timelineSeconds = shownValue_;
            break;
        default: break;
        }
        return shown;
    };

    // What is under the pointer: a sound, and which part of it (or its
    // playing mode's badge).
    std::uint64_t hoveredBadge {};
    std::optional<std::size_t> hovered;
    auto hoveredPart = Drag::move;
    auto upperHalf = true;
    std::uint64_t hoveredMarker {};
    for (std::size_t i = 0; i < events.size(); ++i) {
        const auto& event = events[i];
        const auto shown = shownOf(event);
        const auto top = origin.y + rowHeight * static_cast<float>(rowOf[i]);
        const ImVec2 min(xOf(shown.start), top + dp(1.0f));
        const ImVec2 max(std::max(min.x + 2.0f, xOf(shown.start + shown.duration)), top + rowHeight - dp(1.0f));
        const auto colour = theme::fromArgb(event.colour != 0 ? event.colour : 0xff53d0c2u);
        const auto isSelected = event.id == selectedEvent_;
        // See-through: the grid shows behind it.
        list->AddRectFilled(min, max, theme::alpha(theme::mix(colour, p.bg0 | 0xff000000u, 0.5f), 0.4f), dp(3.0f));
        list->AddRectFilled(min, ImVec2(max.x, min.y + header), theme::alpha(theme::mix(colour, p.bg0 | 0xff000000u, 0.35f), 0.85f), dp(3.0f),
                            ImDrawFlags_RoundCornersTop);
        const auto waveTop = min.y + header;
        const auto waveBottom = max.y - footer;
        // Its wave, every channel, through its stretch points, at its gain.
        AudioEvent mapped = event;
        mapped.sourceOffsetSeconds = shown.offset;
        mapped.durationSeconds = shown.duration;
        mapped.sourceDurationSeconds = shown.sourceDuration;
        mapped.stretchMarkers = shown.markers;
        if (auto* thumbnail = waveforms_.get(event.sourceFile)) {
            if (!thumbnail->isFullyLoaded())
                host_.wake(2);
            const auto channels = std::clamp(thumbnail->getNumChannels(), 1, 2);
            const auto laneHeight = (waveBottom - waveTop) / static_cast<float>(channels);
            const auto gain = std::min(2.0f, juce::Decibels::decibelsToGain(event.gainDb));
            for (int channel = 0; channel < channels; ++channel) {
                const auto centre = waveTop + laneHeight * (static_cast<float>(channel) + 0.5f);
                const auto half = laneHeight * 0.46f;
                list->AddLine(ImVec2(std::max(min.x, origin.x), centre), ImVec2(std::min(max.x, end.x), centre), theme::alpha(colour, 0.35f));
                for (auto x = std::max(min.x, origin.x); x < std::min(max.x, end.x); x += 1.0f) {
                    const auto t0 = secondsAt(x) - shown.start;
                    const auto t1 = secondsAt(x + 1.0f) - shown.start;
                    const auto s0 = app::AudioTrackController::eventSourceTime(mapped, t0);
                    const auto s1 = app::AudioTrackController::eventSourceTime(mapped, t1);
                    float low = 0.0f;
                    float high = 0.0f;
                    thumbnail->getApproximateMinMax(std::min(s0, s1), std::max(s0, s1) + 1.0e-4, channel, low, high);
                    const auto inside = bounds.playStart <= shown.start + t0 && shown.start + t0 <= bounds.playEnd;
                    list->AddRectFilled(ImVec2(x, centre - std::min(1.0f, high * gain) * half), ImVec2(x + 1.0f, centre - std::max(-1.0f, low * gain) * half + 1.0f),
                                        inside ? theme::alpha(theme::brighter(colour, 0.15f), 0.85f) : theme::alpha(colour, 0.38f));
                }
            }
        }
        // Its speed along the bottom: slower red, faster blue, as stretched by its points.
        {
            std::vector<std::pair<double, double>> anchors { { 0.0, shown.offset } };
            for (const auto& marker : shown.markers)
                anchors.emplace_back(marker.timelineSeconds, marker.sourceSeconds);
            anchors.emplace_back(shown.duration, shown.offset + shown.sourceDuration);
            std::sort(anchors.begin(), anchors.end());
            const auto base = shown.sourceDuration / std::max(1.0e-6, shown.duration);
            for (std::size_t a = 1; a < anchors.size(); ++a) {
                const auto dt = anchors[a].first - anchors[a - 1].first;
                if (dt <= 1.0e-6)
                    continue;
                const auto ratio = (anchors[a].second - anchors[a - 1].second) / dt / std::max(1.0e-6, base);
                const auto tint = ratio < 0.985 ? p.record : ratio > 1.015 ? IM_COL32(80, 200, 230, 255) : theme::alpha(p.textFaint, 0.6f);
                list->AddLine(ImVec2(xOf(shown.start + anchors[a - 1].first), max.y - dp(3.0f)), ImVec2(xOf(shown.start + anchors[a].first), max.y - dp(3.0f)),
                              tint, dp(2.0f));
            }
        }
        // Its stretch points: a line, and a handle at the bottom.
        for (const auto& marker : shown.markers) {
            const auto x = xOf(shown.start + marker.timelineSeconds);
            const auto near = std::abs(mouse.x - x) <= dp(5.0f) && mouse.y >= waveTop && mouse.y <= max.y;
            if (near && mouse.y > (waveTop + waveBottom) * 0.5f)
                hoveredMarker = marker.id;
            const auto active = (drag_ == Drag::marker && marker.id == draggedMarker_) || near;
            list->AddLine(ImVec2(x, waveTop), ImVec2(x, max.y - dp(2.0f)), active ? p.text : theme::alpha(p.text, 0.45f), dp(1.0f));
            list->AddRectFilled(ImVec2(x - dp(4.0f), max.y - dp(9.0f)), ImVec2(x + dp(4.0f), max.y - dp(1.0f)), active ? p.text : theme::alpha(p.text, 0.75f),
                                dp(2.0f));
        }
        // Its fades, and their handles at the top corners.
        const auto fadeInX = xOf(shown.start + shown.fadeIn);
        const auto fadeOutX = xOf(shown.start + shown.duration - shown.fadeOut);
        if (shown.fadeIn > 0.0)
            list->AddLine(ImVec2(min.x, waveBottom), ImVec2(fadeInX, waveTop), theme::alpha(p.text, 0.6f));
        if (shown.fadeOut > 0.0)
            list->AddLine(ImVec2(fadeOutX, waveTop), ImVec2(max.x, waveBottom), theme::alpha(p.text, 0.6f));
        const auto handle = dp(4.0f);
        list->AddRectFilled(ImVec2(fadeInX - handle * 0.5f, waveTop), ImVec2(fadeInX + handle, waveTop + handle * 1.5f), theme::alpha(p.text, 0.8f));
        list->AddRectFilled(ImVec2(fadeOutX - handle, waveTop), ImVec2(fadeOutX + handle * 0.5f, waveTop + handle * 1.5f), theme::alpha(p.text, 0.8f));
        // Its name and gain, and how it plays: a badge at the right (a
        // click: the modes).
        ImGui::PushFont(nullptr, ui::currentFontBase() * 0.78f);
        auto label = event.name.isNotEmpty() ? event.name : event.sourceFile.getFileNameWithoutExtension();
        if (std::abs(event.gainDb) > 0.05f)
            label << "  " << (event.gainDb > 0 ? "+" : "") << juce::String(event.gainDb, 1) << " dB";
        if (!event.stretchMarkers.empty())
            label << juce::String::fromUTF8("  \xc2\xb7  ") << juce::String(static_cast<int>(event.stretchMarkers.size()))
                  << ui::Ls(" point(s)", " point(s)");
        const auto badgeText = modeShort(event.stretchMode);
        const auto badgeWidth = ImGui::CalcTextSize(badgeText).x + dp(10.0f);
        const auto withBadge = max.x - min.x > badgeWidth + dp(48.0f);
        ui::clippedText(list, ImVec2(min.x + dp(4.0f), min.y + dp(1.0f)), max.x - min.x - dp(8.0f) - (withBadge ? badgeWidth + dp(4.0f) : 0.0f),
                        p.text, label.toRawUTF8());
        if (withBadge) {
            const ImVec2 badgeMin(max.x - badgeWidth - dp(3.0f), min.y + dp(1.5f));
            const ImVec2 badgeMax(max.x - dp(3.0f), min.y + header - dp(1.0f));
            const auto over = mouse.x >= badgeMin.x && mouse.x <= badgeMax.x && mouse.y >= badgeMin.y && mouse.y <= badgeMax.y;
            if (over)
                hoveredBadge = event.id;
            list->AddRectFilled(badgeMin, badgeMax, over ? theme::alpha(p.text, 0.92f) : theme::alpha(p.bg0, 0.6f), dp(3.0f));
            list->AddText(ImVec2(badgeMin.x + dp(5.0f), badgeMin.y + (badgeMax.y - badgeMin.y - ImGui::GetFontSize()) * 0.5f),
                          over ? p.bg0 : p.text, badgeText);
        }
        ImGui::PopFont();
        list->AddRect(min, max, isSelected ? p.text : theme::alpha(colour, 0.9f), dp(3.0f), isSelected ? dp(1.8f) : dp(1.0f));
        // Under the pointer?
        if (mouse.x >= min.x - dp(3.0f) && mouse.x <= max.x + dp(3.0f) && mouse.y >= min.y && mouse.y <= max.y) {
            hovered = i;
            upperHalf = mouse.y < (waveTop + waveBottom) * 0.5f;
            const auto edge = std::min(dp(6.0f), (max.x - min.x) * 0.25f);
            if (hoveredMarker != 0)
                hoveredPart = Drag::marker;
            else if (mouse.y <= waveTop + handle * 2.5f && mouse.y >= waveTop - dp(2.0f) && std::abs(mouse.x - fadeInX) <= dp(6.0f))
                hoveredPart = Drag::fadeIn;
            else if (mouse.y <= waveTop + handle * 2.5f && mouse.y >= waveTop - dp(2.0f) && std::abs(mouse.x - fadeOutX) <= dp(6.0f))
                hoveredPart = Drag::fadeOut;
            else if (mouse.x < min.x + edge)
                hoveredPart = Drag::trimStart;
            else if (mouse.x > max.x - edge)
                hoveredPart = Drag::trimEnd;
            else
                hoveredPart = Drag::move;
        }
    }
    // The selection, the cursor, where it plays.
    if (selectionEnd_ - selectionStart_ > 1.0e-4)
        list->AddRectFilled(ImVec2(xOf(selectionStart_), origin.y), ImVec2(xOf(selectionEnd_), end.y), theme::alpha(p.accent, 0.18f));
    list->AddLine(ImVec2(xOf(cursor_), origin.y), ImVec2(xOf(cursor_), end.y), p.accent, dp(1.5f));
    std::optional<double> playhead;
    if (launcher() && studio_.playingScene(track) == scene_) {
        playhead = engine.launcherClipPositionSeconds(index);
    } else if (!launcher() && engine.isPlaying()) {
        const auto t = engine.position() - clip.startSeconds;
        if (t >= 0.0 && t <= clip.durationSeconds)
            playhead = bounds.playStart + t;
    }
    if (playhead)
        list->AddLine(ImVec2(xOf(*playhead), origin.y), ImVec2(xOf(*playhead), end.y), p.play, dp(1.5f));
    if (previewEnd_) {
        // Heard from its file: shown on the sound selected.
        const auto selected = std::find_if(events.begin(), events.end(), [this](const auto& e) { return e.id == selectedEvent_; });
        if (selected != events.end()) {
            const auto heard = engine.samplePreviewPositionSeconds();
            for (auto t = 0.0; t < selected->durationSeconds; t += 0.005)
                if (app::AudioTrackController::eventSourceTime(*selected, t) >= heard) {
                    list->AddLine(ImVec2(xOf(selected->startSeconds + t), origin.y), ImVec2(xOf(selected->startSeconds + t), end.y), p.play, dp(1.0f));
                    break;
                }
        }
    }
    list->PopClipRect();

    // Input.
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("##audioLane", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    if (ImGui::BeginDragDropTarget()) {
        if (const auto* payload = ImGui::AcceptDragDropPayload(kFilePayload); payload != nullptr && payload->DataSize > 1)
            addEvent(track, clip, juce::File(juce::String::fromUTF8(static_cast<const char*>(payload->Data))), std::max(0.0, snap(at)));
        ImGui::EndDragDropTarget();
    }
    const auto laneHovered = ImGui::IsItemHovered();
    if (laneHovered && hoveredBadge != 0)
        for (const auto& event : events)
            if (event.id == hoveredBadge)
                ui::tooltip((juce::String(modeName(event.stretchMode)) + "\n" + modeHelp(event.stretchMode)
                             + ui::Ls("\nClic : changer de mode", "\nClick: change the mode"))
                                .toRawUTF8());
    if (ImGui::BeginPopup("##soundMode")) {
        for (const auto& event : events)
            if (event.id == modeMenuEvent_) {
                ImGui::TextDisabled("%s", L("Lecture du son", "How the sound plays"));
                ImGui::Separator();
                for (const auto mode : playModes) {
                    if (ImGui::MenuItem(modeName(mode), nullptr, event.stretchMode == mode)
                        && setEventProperties(track, clip, event.id, event.gainDb, event.sourceTempoBpm, mode))
                        studio_.markDirty();
                    ui::tooltip(modeHelp(mode));
                }
            }
        ImGui::EndPopup();
    }
    // A double click: a stretch point (low on a sound) or a cut (high on it).
    if (laneHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hovered && hoveredPart == Drag::move && hoveredBadge == 0) {
        const auto& event = events[*hovered];
        if (!upperHalf && event.stretchMode == StretchMode::raw) {
            report(ui::Ls("Mode Brut : ce son n'est jamais étiré (changez de mode pour poser des points d'étirement)",
                          "Raw mode: this sound is never stretched (change its mode to place stretch points)"),
                   true);
        } else if (upperHalf) {
            if (const auto made = splitEvent(track, clip, event.id, std::clamp(snap(at), event.startSeconds + 0.002,
                                                                               event.startSeconds + event.durationSeconds - 0.002));
                made != 0) {
                selectedEvent_ = made;
                studio_.markDirty();
            }
        } else {
            const auto local = std::clamp(at - event.startSeconds, 0.002, event.durationSeconds - 0.002);
            if (addMarker(track, clip, event.id, app::AudioTrackController::eventSourceTime(event, local), local) != 0)
                studio_.markDirty();
            else
                report(ui::Ls("Pas de point d'étirement ici (trop près d'un autre)", "No stretch point here (too close to another)"), true);
        }
        drag_ = Drag::none;
    } else if (ImGui::IsItemActivated() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        grab_ = at;
        // The sounds take the keys back from the automation lanes.
        for (auto& [name, editor] : curveEditors_)
            editor.setFocused(false);
        focusedLane_.clear();
        if (hoveredBadge != 0) {
            // Its badge: the playing modes.
            selectedEvent_ = hoveredBadge;
            modeMenuEvent_ = hoveredBadge;
            drag_ = Drag::none;
            ImGui::OpenPopup("##soundMode");
        } else if (hovered && !ImGui::GetIO().KeyShift) {
            const auto& event = events[*hovered];
            selectedEvent_ = event.id;
            drag_ = hoveredPart;
            draggedEvent_ = event.id;
            draggedMarker_ = hoveredPart == Drag::marker ? hoveredMarker : 0;
            originStart_ = shownStart_ = event.startSeconds;
            originOffset_ = shownOffset_ = event.sourceOffsetSeconds;
            originDuration_ = shownDuration_ = event.durationSeconds;
            originSourceDuration_ = shownSourceDuration_ = event.sourceDurationSeconds;
            sourceLength_ = event.sourceLengthSeconds > 0.0 ? event.sourceLengthSeconds : event.sourceOffsetSeconds + event.sourceDurationSeconds;
            originFade_ = shownValue_ = hoveredPart == Drag::fadeIn ? event.fadeInSeconds : event.fadeOutSeconds;
            if (hoveredPart == Drag::marker)
                for (const auto& marker : event.stretchMarkers)
                    if (marker.id == hoveredMarker)
                        shownValue_ = marker.timelineSeconds;
        } else {
            // The background (or Alt over a sound): a stretch of time selected.
            drag_ = Drag::select;
            grab_ = std::max(0.0, snap(at));
            selectionStart_ = selectionEnd_ = grab_;
            if (!hovered)
                selectedEvent_ = 0;
        }
    }
    if (ImGui::IsItemActive() && drag_ != Drag::none) {
        const auto found = std::find_if(events.begin(), events.end(), [this](const AudioEvent& e) { return e.id == draggedEvent_; });
        // A magnet: the other sounds' edges, the cursor, the flags, the grid (Alt: no snap).
        const auto magnet = [&](const double t) {
            if (!ui::snapping(state_.audioClipSnap))
                return t;
            auto best = snap(t);
            auto distance = std::abs(best - t);
            const auto limit = static_cast<double>(dp(7.0f)) / std::max(1.0e-9, pixelsPerSecond_);
            const auto consider = [&](const double candidate) {
                if (std::abs(candidate - t) < std::min(distance, limit)) {
                    distance = std::abs(candidate - t);
                    best = candidate;
                }
            };
            consider(0.0);
            consider(cursor_);
            consider(bounds.playStart);
            consider(bounds.loopStart);
            consider(bounds.loopEnd);
            for (const auto& other : events)
                if (other.id != draggedEvent_) {
                    consider(other.startSeconds);
                    consider(other.startSeconds + other.durationSeconds);
                }
            return best;
        };
        const auto ratio = originSourceDuration_ / std::max(1.0e-6, originDuration_);
        const auto delta = at - grab_;
        switch (drag_) {
        case Drag::move: {
            auto start = magnet(std::max(floor, originStart_ + delta));
            const auto endSnapped = magnet(start + originDuration_);
            if (std::abs(endSnapped - (start + originDuration_)) > 1.0e-9 && std::abs(start - (originStart_ + delta)) < 1.0e-9)
                start = std::max(floor, endSnapped - originDuration_);
            shownStart_ = start;
            break;
        }
        case Drag::trimStart: {
            const auto earliest = std::max(floor, originStart_ - originOffset_ / std::max(1.0e-6, ratio));
            const auto start = std::clamp(magnet(originStart_ + delta), earliest, originStart_ + originDuration_ - 0.01);
            shownStart_ = start;
            shownOffset_ = std::max(0.0, originOffset_ + (start - originStart_) * ratio);
            shownDuration_ = originStart_ + originDuration_ - start;
            shownSourceDuration_ = shownDuration_ * ratio;
            break;
        }
        case Drag::trimEnd: {
            const auto longest = (sourceLength_ - originOffset_) / std::max(1.0e-6, ratio);
            const auto stop = std::clamp(magnet(originStart_ + originDuration_ + delta), originStart_ + 0.01, originStart_ + std::max(0.01, longest));
            shownDuration_ = stop - originStart_;
            shownSourceDuration_ = shownDuration_ * ratio;
            break;
        }
        case Drag::fadeIn:
            shownValue_ = std::clamp(at - originStart_, 0.0, originDuration_ * 0.5);
            break;
        case Drag::fadeOut:
            shownValue_ = std::clamp(originStart_ + originDuration_ - at, 0.0, originDuration_ * 0.5);
            break;
        case Drag::marker:
            if (found != events.end()) {
                // Between its neighbours.
                auto low = 0.002;
                auto high = found->durationSeconds - 0.002;
                const auto self = std::find_if(found->stretchMarkers.begin(), found->stretchMarkers.end(),
                                               [this](const auto& m) { return m.id == draggedMarker_; });
                if (self != found->stretchMarkers.end())
                    for (const auto& marker : found->stretchMarkers) {
                        if (marker.id == draggedMarker_)
                            continue;
                        if (marker.sourceSeconds < self->sourceSeconds)
                            low = std::max(low, marker.timelineSeconds + 0.005);
                        else
                            high = std::min(high, marker.timelineSeconds - 0.005);
                    }
                shownValue_ = std::clamp(at - found->startSeconds, low, std::max(low, high));
            }
            break;
        case Drag::select: {
            const auto value = std::max(0.0, snap(at));
            selectionStart_ = std::min(grab_, value);
            selectionEnd_ = std::max(grab_, value);
            break;
        }
        default: break;
        }
    }
    if (ImGui::IsItemDeactivated() && drag_ != Drag::none) {
        const auto found = std::find_if(events.begin(), events.end(), [this](const AudioEvent& e) { return e.id == draggedEvent_; });
        auto changed = false;
        switch (drag_) {
        case Drag::move:
        case Drag::trimStart:
        case Drag::trimEnd: {
            const auto moved = std::abs(shownStart_ - originStart_) > 1.0e-6 || std::abs(shownDuration_ - originDuration_) > 1.0e-6;
            if (moved && found != events.end() && shownStart_ < 0.0) {
                // Before the content's start: the content starts earlier.
                auto all = events;
                for (auto& one : all)
                    if (one.id == draggedEvent_) {
                        one.startSeconds = shownStart_;
                        one.sourceOffsetSeconds = shownOffset_;
                        one.durationSeconds = shownDuration_;
                        one.sourceDurationSeconds = shownSourceDuration_;
                        one.fadeInSeconds = std::min(one.fadeInSeconds, shownDuration_ * 0.5);
                        one.fadeOutSeconds = std::min(one.fadeOutSeconds, shownDuration_ * 0.5);
                    }
                changed = commitTimeline(track, clip, std::move(all), boundsOf(clip), ui::Ls("Son déplacé", "Sound moved"), true);
            } else if (moved && found != events.end()) {
                changed = updateEvent(track, clip, draggedEvent_, shownStart_, shownOffset_, shownDuration_, shownSourceDuration_,
                                      std::min(found->fadeInSeconds, shownDuration_ * 0.5), std::min(found->fadeOutSeconds, shownDuration_ * 0.5));
            }
            else if (!moved)
                cursor_ = std::max(0.0, snap(grab_));
            break;
        }
        case Drag::fadeIn:
        case Drag::fadeOut:
            if (found != events.end() && std::abs(shownValue_ - originFade_) > 1.0e-6)
                changed = updateEvent(track, clip, draggedEvent_, found->startSeconds, found->sourceOffsetSeconds, found->durationSeconds,
                                      found->sourceDurationSeconds, drag_ == Drag::fadeIn ? shownValue_ : -1.0, drag_ == Drag::fadeOut ? shownValue_ : -1.0);
            break;
        case Drag::marker:
            if (found != events.end())
                changed = moveMarker(track, clip, draggedEvent_, draggedMarker_, shownValue_);
            break;
        case Drag::select:
            if (selectionEnd_ - selectionStart_ < 2.0 / std::max(1.0e-9, pixelsPerSecond_)) {
                cursor_ = grab_;
                selectionStart_ = selectionEnd_ = 0.0;
            }
            break;
        default: break;
        }
        if (changed)
            studio_.markDirty();
        drag_ = Drag::none;
        draggedEvent_ = 0;
        draggedMarker_ = 0;
    }
    // A right click: its menu.
    if (laneHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        menuEvent_ = hovered ? events[*hovered].id : 0;
        menuMarker_ = hovered && hoveredPart == Drag::marker ? hoveredMarker : 0;
        menuAt_ = std::max(0.0, at);
        if (hovered)
            selectedEvent_ = menuEvent_;
        ImGui::OpenPopup("##audioEventMenu");
    }
    // The wheel zooms around the pointer (Shift: moves along).
    if (laneHovered)
        wheel(clip, events);
    // The pointer says what a press does.
    if (laneHovered && drag_ == Drag::none && hovered) {
        switch (hoveredPart) {
        case Drag::marker:
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            ui::tooltip(L("Point d'étirement : glissez pour caler ce moment ailleurs ; clic droit : le retirer",
                          "Stretch point: drag to put that moment elsewhere; right click: remove it"));
            break;
        case Drag::fadeIn:
        case Drag::fadeOut:
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            ui::tooltip(L("Fondu : glissez", "Fade: drag"));
            break;
        case Drag::trimStart:
        case Drag::trimEnd: ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW); break;
        default:
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ui::tooltip(upperHalf ? L("Glisser : déplacer · double-clic ici (en haut) : couper · Maj+glisser : sélectionner",
                                      "Drag: move · double click here (high): cut · Shift+drag: select")
                                  : L("Glisser : déplacer · double-clic ici (en bas) : point d'étirement · Maj+glisser : sélectionner",
                                      "Drag: move · double click here (low): stretch point · Shift+drag: select"));
            break;
        }
    } else if (drag_ == Drag::trimStart || drag_ == Drag::trimEnd || drag_ == Drag::marker || drag_ == Drag::fadeIn || drag_ == Drag::fadeOut) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    if (events.empty()) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x + dp(12.0f), origin.y + dp(12.0f)));
        ImGui::TextDisabled("%s", L("Ce clip est vide : glissez un son du navigateur ici, ou + dans la barre d'outils.",
                                    "This clip is empty: drag a sound from the browser here, or + in the toolbar."));
    }
}

void AudioEditorView::eventMenu(const TrackRef& track, const AudioClip& clip, const std::vector<AudioEvent>& events)
{
    if (!ImGui::BeginPopup("##audioEventMenu"))
        return;
    auto& engine = studio_.engine();
    const auto found = std::find_if(events.begin(), events.end(), [this](const auto& e) { return e.id == menuEvent_; });
    if (found == events.end()) {
        if (ImGui::MenuItem(L("Ajouter un son ici…", "Add a sound here…"))) {
            cursor_ = snap(menuAt_);
            const auto clipId = clip.id;
            const auto at = cursor_;
            const auto scene = scene_;
            chooser_ = std::make_unique<juce::FileChooser>(ui::Ls("Un son pour ce clip", "A sound for this clip"), juce::File(),
                                                           "*.wav;*.wave;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
            chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this, track, clipId, at, scene](const juce::FileChooser& chosen) {
                                      const auto file = chosen.getResult();
                                      if (file == juce::File())
                                          return;
                                      scene_ = scene;
                                      if (scene >= 0) {
                                          if (const auto slot = studio_.engine().launcherClip(track.audioIndex, scene)) {
                                              launcherClip_ = *slot;
                                              addEvent(track, launcherView(track, scene, *slot), file, at);
                                          }
                                      } else if (const auto again = studio_.engine().audioClip(track.audioIndex, clipId)) {
                                          addEvent(track, *again, file, at);
                                      }
                                  });
        }
        if (ImGui::MenuItem(L("Tout voir", "See it all")))
            fit(clip, events);
        ImGui::EndPopup();
        return;
    }
    const auto event = *found;
    ImGui::TextDisabled("%s", (event.name.isNotEmpty() ? event.name : event.sourceFile.getFileNameWithoutExtension()).toRawUTF8());
    ImGui::Separator();
    if (menuMarker_ != 0) {
        if (ImGui::MenuItem(L("Retirer ce point d'étirement", "Remove this stretch point")) && removeMarker(track, clip, event.id, menuMarker_))
            studio_.markDirty();
        ImGui::Separator();
    }
    const auto inside = menuAt_ > event.startSeconds + 0.002 && menuAt_ < event.startSeconds + event.durationSeconds - 0.002;
    if (ImGui::MenuItem(L("Couper ici", "Cut here"), L("double-clic en haut", "double click high"), false, inside)) {
        if (const auto made = splitEvent(track, clip, event.id, snap(menuAt_)); made != 0) {
            selectedEvent_ = made;
            studio_.markDirty();
        }
    }
    if (ImGui::MenuItem(L("Point d'étirement ici", "Stretch point here"), L("double-clic en bas", "double click low"), false, inside)) {
        const auto local = menuAt_ - event.startSeconds;
        if (addMarker(track, clip, event.id, app::AudioTrackController::eventSourceTime(event, local), local) != 0)
            studio_.markDirty();
    }
    if (ImGui::MenuItem(L("Dupliquer", "Duplicate"), "Ctrl+D"))
        duplicate(track, clip, events, event.id);
    if (ImGui::MenuItem(L("Supprimer", "Delete"), L("Suppr", "Delete")) && removeEvent(track, clip, event.id)) {
        selectedEvent_ = 0;
        studio_.markDirty();
    }
    ImGui::Separator();
    if (ImGui::MenuItem(L("Renverser (Reverse)", "Reverse"))) {
        app::AudioEditRequest request;
        request.operation = Op::reverse;
        process(track, clip, event, request, ui::Ls("Renverser", "Reverse"), selectionIn(event));
    }
    ui::tooltip(L("Le son joué à l'envers (la sélection seule s'il y en a une dans ce son)",
                  "The sound played backwards (the selection alone when there is one in this sound)"));
    if (!launcher() && !clip.usesEventTimeline && clip.versions.size() > 1
        && ImGui::MenuItem(L("Revenir au son d'origine", "Back to the original sound"))) {
        if (studio_.engine().setAudioClipVersion(track.audioIndex, clip.id, 0))
            studio_.markDirty();
    }
    ImGui::Separator();
    if (ImGui::BeginMenu(L("Lecture (étirement)", "Playing (stretch)"))) {
        for (const auto mode : playModes) {
            if (ImGui::MenuItem(modeName(mode), nullptr, event.stretchMode == mode)
                && setEventProperties(track, clip, event.id, event.gainDb, event.sourceTempoBpm, mode))
                studio_.markDirty();
            ui::tooltip(modeHelp(mode));
        }
        ImGui::Separator();
        if (ImGui::MenuItem(L("Détecter le tempo du son", "Detect the sound's tempo"))) {
            if (const auto tempo = app::AudioTrackController::detectAudioTempo(event.sourceFile)) {
                if (setEventProperties(track, clip, event.id, event.gainDb, tempo->bpm, event.stretchMode))
                    studio_.markDirty();
                report(ui::Ls("Tempo trouvé : ", "Tempo found: ") + juce::String(tempo->bpm, 2) + " BPM", false);
            } else {
                report(ui::Ls("Aucun tempo net dans ce son", "No clear tempo in this sound"), true);
            }
        }
        char follow[96];
        std::snprintf(follow, sizeof(follow), "%s (%.2f BPM)", L("Caler sur le tempo du projet", "Fit to the project's tempo"), engine.tempo());
        if (ImGui::MenuItem(follow, nullptr, false, event.sourceTempoBpm > 0.0))
            fitToTempo(track, clip, event);
        if (ImGui::MenuItem(L("Vitesse d'origine (100 %)", "Original speed (100 %)"))
            && updateEvent(track, clip, event.id, event.startSeconds, event.sourceOffsetSeconds, event.sourceDurationSeconds,
                           event.sourceDurationSeconds, std::min(event.fadeInSeconds, event.sourceDurationSeconds * 0.5),
                           std::min(event.fadeOutSeconds, event.sourceDurationSeconds * 0.5)))
            studio_.markDirty();
        if (ImGui::MenuItem(L("Effacer les points d'étirement", "Clear the stretch points"), nullptr, false, !event.stretchMarkers.empty())) {
            auto all = events;
            for (auto& one : all)
                if (one.id == event.id)
                    one.stretchMarkers.clear();
            if (replaceEvents(track, clip, std::move(all)).wasOk())
                studio_.markDirty();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(L("Traitements", "Processes"))) {
        const auto part = selectionIn(event);
        ImGui::TextDisabled("%s", part ? L("sur la sélection", "on the selection") : L("sur tout le son", "on the whole sound"));
        for (int i = 0; i < processCount; ++i) {
            const auto& kind = processAt(i);
            if (ImGui::MenuItem(L(kind.french, kind.english))) {
                app::AudioEditRequest request;
                request.operation = kind.op;
                request.levelDb = kind.op == Op::amplify ? gainDb_ : normaliseDb_;
                request.curve = static_cast<app::FadeCurve>(std::clamp(fadeCurve_, 0, 3));
                process(track, clip, event, request, juce::String::fromUTF8(L(kind.french, kind.english)), part);
            }
        }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem(L("Écouter le son", "Hear the sound"))) {
        if (engine.startSamplePreview(event.sourceFile, event.sourceOffsetSeconds).wasOk())
            previewEnd_ = event.sourceOffsetSeconds + event.sourceDurationSeconds;
    }
    if (ImGui::MenuItem(L("Montrer le fichier", "Show the file")))
        event.sourceFile.revealToUser();
    ImGui::EndPopup();
}

// ---- The clip's automation, under its sounds ----------------------------------------------------

bool AudioEditorView::setLanes(const TrackRef& track, const AudioClip& clip, std::map<juce::String, std::vector<AutomationPoint>> lanes,
                               const juce::String& label)
{
    auto& engine = studio_.engine();
    bool done {};
    if (launcher()) {
        remember(track, label);
        done = engine.setLauncherClipLanes(track.audioIndex, scene_, std::move(lanes));
    } else {
        done = engine.setAudioClipLanes(track.audioIndex, clip.id, std::move(lanes));
    }
    if (done) {
        studio_.publishParameterAutomation(track);
        studio_.markDirty();
    }
    return done;
}

std::vector<juce::String> AudioEditorView::laneRows(const AudioClip& clip, const std::vector<AudioEvent>& events)
{
    // One lane at least (the volume's), then those picked, then any with
    // points not shown yet (the clip's, the shown sound's): no automation
    // is ever hidden.
    auto& rows = laneRows_[clip.id];
    if (rows.empty())
        rows.push_back(Studio::clipVolumeLane);
    auto shown = rows;
    for (const auto& [name, points] : clip.lanes)
        if (!points.empty() && std::find(shown.begin(), shown.end(), name) == shown.end())
            shown.push_back(name);
    if (const auto* sound = soundTarget(events))
        for (const auto& [key, points] : sound->lanes) {
            const auto name = "sound." + key;
            if (!points.empty() && std::find(shown.begin(), shown.end(), name) == shown.end())
                shown.push_back(name);
        }
    return shown;
}

bool AudioEditorView::isSoundLane(const juce::String& name)
{
    return name.startsWith("sound.");
}

juce::String AudioEditorView::soundKeyOf(const juce::String& name)
{
    return name.substring(6);
}

const AudioEditorView::AudioEvent* AudioEditorView::soundTarget(const std::vector<AudioEvent>& events) const
{
    for (const auto& event : events)
        if (event.id == selectedEvent_)
            return &event;
    return events.size() == 1 ? &events.front() : nullptr;
}

juce::String AudioEditorView::editorKey(const juce::String& name, const std::vector<AudioEvent>& events) const
{
    if (!isSoundLane(name))
        return name;
    const auto* sound = soundTarget(events);
    return name + "@" + juce::String(static_cast<juce::int64>(sound != nullptr ? sound->id : 0));
}

juce::String AudioEditorView::laneLabel(const std::vector<Studio::MasterLaneChoice>& choices, const juce::String& name)
{
    if (isSoundLane(name)) {
        const auto key = soundKeyOf(name);
        return key == app::AudioTrackController::soundPanLane     ? ui::Ls("Son : panoramique", "Sound: pan")
               : key == app::AudioTrackController::soundPitchLane ? ui::Ls("Son : hauteur", "Sound: pitch")
                                                                   : ui::Ls("Son : volume", "Sound: volume");
    }
    for (const auto& choice : choices)
        if (choice.binding == name)
            return choice.label;
    return name;
}

std::pair<double, double> AudioEditorView::gridLevels() const
{
    // The finest level at least 8 dp apart (the darker lines) and the one
    // above it (the brighter): always a darker line between two bright ones.
    const auto beat = beatSeconds();
    const auto bar = beat * std::max(1, studio_.engine().timeSignatureNumerator());
    const double ladder[] { beat / 32.0, beat / 16.0, beat / 8.0, beat / 4.0, beat / 2.0, beat, bar, bar * 4.0, bar * 16.0, bar * 64.0 };
    constexpr auto count = sizeof(ladder) / sizeof(ladder[0]);
    auto minor = count - 2;
    for (std::size_t index = 0; index + 1 < count; ++index)
        if (ladder[index] * pixelsPerSecond_ >= dp(8.0f)) {
            minor = index;
            break;
        }
    return { ladder[minor], ladder[minor + 1] };
}

double AudioEditorView::gridStep() const
{
    return gridLevels().first;
}

void AudioEditorView::drawGrid(ImDrawList* list, const ImVec2 min, const ImVec2 max) const
{
    const auto& p = theme::palette();
    const auto [minor, major] = gridLevels();
    const auto bar = beatSeconds() * std::max(1, studio_.engine().timeSignatureNumerator());
    const auto onUnit = [](const double value, const double unit) { return std::abs(std::remainder(value, unit)) < unit * 1.0e-3; };
    const auto first = std::floor((secondsAt(min.x) - gridOrigin_) / minor);
    const auto last = std::ceil((secondsAt(max.x) - gridOrigin_) / minor);
    for (auto k = first; k <= last && k - first < 4'000.0; k += 1.0) {
        const auto offset = k * minor;
        const auto x = xOf(gridOrigin_ + offset);
        const auto onMajor = onUnit(offset, major);
        const auto alpha = !onMajor ? 0.11f : onUnit(offset, std::max(major, bar)) ? 0.46f : 0.3f;
        list->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), theme::alpha(p.text, alpha));
    }
}

void AudioEditorView::wheel(const AudioClip& clip, const std::vector<AudioEvent>& events)
{
    const auto& io = ImGui::GetIO();
    if (io.MouseWheel == 0.0f && io.MouseWheelH == 0.0f)
        return;
    const auto at = secondsAt(io.MousePos.x);
    if (io.MouseWheelH != 0.0f || io.KeyShift) {
        // Sideways (Shift, or a sideways wheel): moves along.
        const auto amount = io.MouseWheelH != 0.0f ? -io.MouseWheelH : io.MouseWheel;
        scroll_ = std::max(minScroll(clip, events), scroll_ - amount * 80.0 / pixelsPerSecond_);
    } else {
        // The wheel zooms around the pointer.
        pixelsPerSecond_ = std::clamp(pixelsPerSecond_ * (io.MouseWheel > 0.0f ? 1.2 : 1.0 / 1.2),
                                      static_cast<double>(laneWidth_) / viewLength(clip, events) * 0.25, 96'000.0);
        scroll_ = std::max(minScroll(clip, events), at - static_cast<double>(io.MousePos.x - laneLeft_) / pixelsPerSecond_);
    }
    ui::takeWheel();
}

curves::Lane AudioEditorView::laneDescription(const std::vector<Studio::MasterLaneChoice>& choices, const juce::String& name) const
{
    curves::Lane lane;
    const auto panText = [](const double value) {
        char text[48];
        std::snprintf(text, sizeof(text), std::abs(value) < 0.005 ? "C" : value < 0.0 ? "G %.0f %%" : "D %.0f %%", std::abs(value) * 100.0);
        return juce::String::fromUTF8(text);
    };
    if (isSoundLane(name)) {
        // A sound's own: its volume over its gain, its pan, its pitch over
        // its transposition.
        const auto key = soundKeyOf(name);
        lane.colour = theme::palette().accent;
        lane.fallback = 0.0;
        if (key == app::AudioTrackController::soundPanLane) {
            lane.minimum = -1.0;
            lane.maximum = 1.0;
            lane.formatValue = panText;
        } else if (key == app::AudioTrackController::soundPitchLane) {
            lane.minimum = -24.0;
            lane.maximum = 24.0;
            lane.formatValue = [](const double value) {
                char text[32];
                std::snprintf(text, sizeof(text), "%+.2f st", value);
                return juce::String(text);
            };
        } else {
            lane.minimum = -60.0;
            lane.maximum = 12.0;
            lane.formatValue = [](const double value) {
                char text[32];
                std::snprintf(text, sizeof(text), "%+.1f dB", value);
                return juce::String(text);
            };
        }
    } else {
        const Studio::MasterLaneChoice* choice = nullptr;
        for (const auto& option : choices)
            if (option.binding == name)
                choice = &option;
        lane.minimum = choice != nullptr ? choice->minimum : 0.0;
        lane.maximum = choice != nullptr ? std::max(choice->minimum + 1.0e-6, choice->maximum) : 1.0;
        lane.logarithmic = choice != nullptr && choice->logarithmic && lane.minimum > 0.0;
        lane.fallback = choice != nullptr ? choice->value : lane.minimum;
        lane.colour = theme::palette().modulation;
        const auto unit = choice != nullptr ? choice->unit : juce::String();
        if (name == Studio::clipPanLane)
            lane.formatValue = panText;
        else
            lane.formatValue = [unit](const double value) {
                char text[48];
                if (unit.isNotEmpty())
                    std::snprintf(text, sizeof(text), "%.3g %s", value, unit.toRawUTF8());
                else
                    std::snprintf(text, sizeof(text), "%.3g", value);
                return juce::String::fromUTF8(text);
            };
    }
    const auto origin = gridOrigin_;
    const auto beat = beatSeconds();
    const auto numerator = std::max(1, studio_.engine().timeSignatureNumerator());
    lane.formatTime = [origin, beat, numerator](const double seconds) {
        const auto beats = (seconds - origin) / std::max(1.0e-9, beat);
        const auto whole = std::floor(beats + 1.0e-6);
        const auto sixteenth = static_cast<int>(std::floor((beats - whole) * 4.0 + 1.0e-6)) + 1;
        return barsBeats(beats, numerator, true) + "." + juce::String(sixteenth) + "  (" + juce::String(seconds, 3) + " s)";
    };
    return lane;
}

std::vector<curves::Point> AudioEditorView::curvePoints(const AudioClip& clip, const std::vector<AudioEvent>& events, const juce::String& name) const
{
    std::vector<curves::Point> points;
    // A sound's points are its own seconds: on the clip's, from its start.
    const auto add = [&points](const std::vector<AutomationPoint>& from, const double offset) {
        for (const auto& point : from)
            points.push_back({ point.id, offset + point.timeSeconds, point.valueDb, static_cast<int>(point.curve), point.curveAmount, point.handles });
    };
    if (isSoundLane(name)) {
        if (const auto* sound = soundTarget(events))
            if (const auto found = sound->lanes.find(soundKeyOf(name)); found != sound->lanes.end())
                add(found->second, sound->startSeconds);
        return points;
    }
    if (const auto found = clip.lanes.find(name); found != clip.lanes.end())
        add(found->second, 0.0);
    return points;
}

bool AudioEditorView::setCurve(const TrackRef& track, const AudioClip& clip, const std::vector<AudioEvent>& events, const juce::String& name,
                               const std::vector<curves::Point>& points)
{
    // Back to the lane's points, their shapes and handles with them.
    const auto toLane = [&points](const double offset, const std::optional<double> length) {
        std::vector<AutomationPoint> lane;
        for (const auto& point : points)
            lane.push_back({ point.id, length ? std::clamp(point.time - offset, 0.0, *length) : point.time - offset, static_cast<float>(point.value),
                             static_cast<app::AudioTrackController::AutomationCurve>(engine::clampedCurveShape(point.shape)), point.bend,
                             point.handles });
        return lane;
    };
    if (isSoundLane(name)) {
        // The sound's: its seconds, inside it.
        const auto* sound = soundTarget(events);
        if (sound == nullptr)
            return false;
        const auto key = soundKeyOf(name);
        auto all = events;
        for (auto& one : all)
            if (one.id == sound->id) {
                auto lane = toLane(one.startSeconds, one.durationSeconds);
                if (lane.empty())
                    one.lanes.erase(key);
                else
                    one.lanes[key] = std::move(lane);
            }
        // A clip of one sound: that sound gets an id of its own.
        if (!clip.usesEventTimeline)
            for (auto& one : all)
                if (one.id == clip.id)
                    one.id = 0;
        if (const auto done = replaceEvents(track, clip, std::move(all)); done.failed()) {
            report(done.getErrorMessage(), true);
            return false;
        }
        studio_.markDirty();
        return true;
    }
    auto lanes = clip.lanes;
    auto lane = toLane(0.0, std::nullopt);
    if (lane.empty())
        lanes.erase(name);
    else
        lanes[name] = std::move(lane);
    return setLanes(track, clip, std::move(lanes), ui::Ls("Automation du clip", "Clip automation"));
}

void AudioEditorView::drawCurveBar(const TrackRef& track, const AudioClip& clip)
{
    const auto& p = theme::palette();
    const auto events = eventsOf(clip);
    // Its row stays, so the lanes do not move under the pointer when one
    // is clicked.
    const auto name = focusedLane_.upToFirstOccurrenceOf("@", false, false);
    const auto found = curveEditors_.find(focusedLane_);
    if (focusedLane_.isEmpty() || found == curveEditors_.end() || editorKey(name, events) != focusedLane_) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", L("Automation : cliquez un point d'un couloir pour lire et modifier ses valeurs, encadrez-en plusieurs pour les modifier ensemble.",
                                    "Automation: click a lane's point to read and edit its values, frame several to edit them together."));
        return;
    }
    const auto choices = studio_.clipLaneChoices(track);
    const auto lane = laneDescription(choices, name);
    auto label = laneLabel(choices, name);
    if (isSoundLane(name))
        if (const auto* sound = soundTarget(events))
            label << juce::String::fromUTF8(" \xc2\xb7 ") << (sound->name.isNotEmpty() ? sound->name : sound->sourceFile.getFileNameWithoutExtension());
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::vec(isSoundLane(name) ? p.accent : p.modulation), "%s", label.toRawUTF8());
    ImGui::SameLine(0.0f, dp(10.0f));
    auto points = curvePoints(clip, events, name);
    if (found->second.drawBar(points, lane, gridStep()))
        (void) setCurve(track, clip, events, name, points);
}

void AudioEditorView::drawLanes(const TrackRef& track, const AudioClip& clip, const float headerLeft, const float top, const float height)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto choices = studio_.clipLaneChoices(track);
    const auto events = eventsOf(clip);
    const auto* sound = soundTarget(events);
    const auto rows = laneRows(clip, events);
    const auto rowHeight = height / static_cast<float>(std::max<std::size_t>(1, rows.size()));
    const auto headerWidth = laneLeft_ - headerLeft;
    const auto bounds = shownBounds(clip);
    const auto soundName = sound == nullptr ? juce::String()
                           : sound->name.isNotEmpty() ? sound->name
                                                      : sound->sourceFile.getFileNameWithoutExtension();
    const juce::String soundLanes[] { juce::String("sound.") + app::AudioTrackController::soundVolumeLane,
                                      juce::String("sound.") + app::AudioTrackController::soundPanLane,
                                      juce::String("sound.") + app::AudioTrackController::soundPitchLane };
    const auto hasPoints = [&](const juce::String& name) {
        if (isSoundLane(name))
            return sound != nullptr && sound->lanes.contains(soundKeyOf(name)) && !sound->lanes.at(soundKeyOf(name)).empty();
        return clip.lanes.contains(name) && !clip.lanes.at(name).empty();
    };
    std::optional<std::size_t> removeRow;
    std::optional<std::pair<std::size_t, juce::String>> changeRow;
    curves::View base;
    base.xOf = [this](const double time) { return xOf(time); };
    base.timeAt = [this](const float x) { return secondsAt(x); };
    base.snap = [this](const double time) { return snap(time); };
    base.cursor = cursor_;
    for (std::size_t row = 0; row < rows.size(); ++row) {
        const auto& name = rows[row];
        const auto soundRow = isSoundLane(name);
        const auto key = editorKey(name, events);
        const auto lane = laneDescription(choices, name);
        auto points = curvePoints(clip, events, name);
        const auto rowTop = top + rowHeight * static_cast<float>(row);
        const ImVec2 min(laneLeft_, rowTop);
        const ImVec2 max(laneLeft_ + laneWidth_, rowTop + rowHeight - dp(2.0f));
        const auto label = laneLabel(choices, name);
        const auto valueAt = [&](const double time) { return curves::Editor::valueAt(points, lane, time); };

        // Its cell on the left: which value it automates, and what to do with it.
        ImGui::PushID(static_cast<int>(row));
        list->AddRectFilled(ImVec2(headerLeft, rowTop), ImVec2(laneLeft_ - dp(2.0f), rowTop + rowHeight - dp(2.0f)),
                            key == focusedLane_ ? p.bg2 : p.bg1);
        if (soundRow)
            list->AddRectFilled(ImVec2(headerLeft, rowTop), ImVec2(headerLeft + dp(3.0f), rowTop + rowHeight - dp(2.0f)), p.accent);
        ImGui::SetCursorScreenPos(ImVec2(headerLeft + dp(4.0f), rowTop + dp(3.0f)));
        ImGui::SetNextItemWidth(headerWidth - dp(10.0f));
        const auto preview = label + (points.empty() ? juce::String() : juce::String::fromUTF8("  \xe2\x97\x8f"));
        if (ImGui::BeginCombo("##laneParameter", preview.toRawUTF8(), ImGuiComboFlags_HeightLarge)) {
            ImGui::TextDisabled("%s", L("La piste, dans ce clip", "The track, in this clip"));
            for (const auto& option : choices) {
                const auto text = option.label + (hasPoints(option.binding) ? juce::String::fromUTF8("  \xe2\x97\x8f") : juce::String());
                if (ImGui::Selectable((text + "##" + option.binding).toRawUTF8(), option.binding == name))
                    changeRow = std::make_pair(row, option.binding);
            }
            if (choices.size() <= 2)
                ImGui::TextDisabled("%s", L("(les potards des cartes de la piste viennent ici)", "(the knobs of the track's cards come here)"));
            ImGui::Separator();
            ImGui::TextDisabled("%s", sound != nullptr ? (ui::Ls("Le son « ", "The sound \"") + soundName + ui::Ls(" »", "\"")).toRawUTF8()
                                                       : L("Un son (choisissez-en un)", "A sound (pick one)"));
            for (const auto& option : soundLanes) {
                const auto text = laneLabel(choices, option) + (hasPoints(option) ? juce::String::fromUTF8("  \xe2\x97\x8f") : juce::String());
                if (ImGui::Selectable((text + "##" + option).toRawUTF8(), option == name, sound != nullptr ? 0 : ImGuiSelectableFlags_Disabled))
                    changeRow = std::make_pair(row, option);
            }
            ImGui::EndCombo();
        }
        ui::tooltip(L("Ce que ce couloir automatise : une valeur de la piste dans le clip, ou le volume, le panoramique, la hauteur du son choisi "
                      "(ses points le suivent quand il bouge) · ● : il a des points",
                      "What this lane automates: a value of the track in the clip, or the picked sound's volume, pan, pitch (its points follow "
                      "it when it moves) · ●: it has points"));
        if (rowHeight >= dp(46.0f)) {
            ImGui::SetCursorScreenPos(ImVec2(headerLeft + dp(4.0f), rowTop + dp(3.0f) + ImGui::GetFrameHeightWithSpacing()));
            if (ImGui::SmallButton("+"))
                ImGui::OpenPopup("##addLaneRow");
            ui::tooltip(L("Un autre couloir d'automation", "Another automation lane"));
            ImGui::SameLine(0.0f, dp(2.0f));
            if (ImGui::SmallButton(juce::String::fromUTF8("\xc3\x97").toRawUTF8()))
                removeRow = row;
            ui::tooltip(L("Retirer ce couloir (ses points avec ; Ctrl+Z les rend)", "Remove this lane (its points too; Ctrl+Z gives them back)"));
            ImGui::SameLine(0.0f, dp(6.0f));
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(theme::vec(p.textDim), "%s", lane.formatValue(valueAt(cursor_)).toRawUTF8());
            ui::tooltip(L("Sa valeur au curseur", "Its value at the cursor"));
            if (ImGui::BeginPopup("##addLaneRow")) {
                for (const auto& option : choices) {
                    const auto shown = std::find(rows.begin(), rows.end(), option.binding) != rows.end();
                    if (ImGui::MenuItem(option.label.toRawUTF8(), nullptr, shown, !shown))
                        laneRows_[clip.id].push_back(option.binding);
                }
                ImGui::Separator();
                for (const auto& option : soundLanes) {
                    const auto shown = std::find(rows.begin(), rows.end(), option) != rows.end();
                    if (ImGui::MenuItem(laneLabel(choices, option).toRawUTF8(), nullptr, shown, !shown && sound != nullptr))
                        laneRows_[clip.id].push_back(option);
                }
                ImGui::EndPopup();
            }
        }
        // A sound's row: whose, and whether its mode hears a pitch lane.
        if (soundRow && rowHeight >= dp(64.0f)) {
            ImGui::SetCursorScreenPos(ImVec2(headerLeft + dp(6.0f), rowTop + dp(3.0f) + ImGui::GetFrameHeightWithSpacing() * 2.0f));
            ImGui::PushTextWrapPos(laneLeft_ - dp(4.0f));
            if (sound != nullptr && soundKeyOf(name) == app::AudioTrackController::soundPitchLane && pitchFollowsSpeed(sound->stretchMode))
                ImGui::TextColored(theme::vec(p.warn), "%s", L("Sans effet en Vitesse ou Brut : choisissez Tonal, Rythme, Complexe ou Texture",
                                                              "No effect with Repitch or Raw: pick Tonal, Beats, Complex or Texture"));
            else
                ImGui::TextColored(theme::vec(p.textFaint), "%s", soundName.toRawUTF8());
            ImGui::PopTextWrapPos();
        }

        // The lane: its background, then the curve editor over it.
        list->PushClipRect(min, max, true);
        list->AddRectFilled(min, max, p.bg0);
        if (xOf(0.0) > min.x)
            list->AddRectFilled(min, ImVec2(xOf(0.0), max.y), theme::alpha(p.bg1, 0.9f));
        list->AddRectFilled(ImVec2(xOf(bounds.playStart), min.y), ImVec2(xOf(bounds.playEnd), max.y), theme::alpha(p.bg2, 0.45f));
        if (bounds.looping)
            list->AddRectFilled(ImVec2(xOf(bounds.loopStart), min.y), ImVec2(xOf(bounds.loopEnd), max.y), theme::alpha(p.accent, 0.035f));
        drawGrid(list, min, max);
        // A sound's lane: outside the sound, dimmed.
        if (soundRow && sound != nullptr) {
            const auto from = xOf(sound->startSeconds);
            const auto to = xOf(sound->startSeconds + sound->durationSeconds);
            list->AddRectFilled(min, ImVec2(std::max(min.x, from), max.y), theme::alpha(p.bg0, 0.75f));
            list->AddRectFilled(ImVec2(std::min(max.x, to), min.y), max, theme::alpha(p.bg0, 0.75f));
            list->AddLine(ImVec2(from, min.y), ImVec2(from, max.y), theme::alpha(p.accent, 0.6f));
            list->AddLine(ImVec2(to, min.y), ImVec2(to, max.y), theme::alpha(p.accent, 0.6f));
        }
        ImGui::PushFont(nullptr, ui::currentFontBase() * 0.72f);
        const auto high = lane.formatValue(lane.maximum);
        const auto low = lane.formatValue(lane.minimum);
        list->AddText(ImVec2(max.x - ImGui::CalcTextSize(high.toRawUTF8()).x - dp(4.0f), min.y + dp(1.0f)), p.textFaint, high.toRawUTF8());
        list->AddText(ImVec2(max.x - ImGui::CalcTextSize(low.toRawUTF8()).x - dp(4.0f), max.y - dp(12.0f)), p.textFaint, low.toRawUTF8());
        ImGui::PopFont();
        list->AddLine(ImVec2(xOf(cursor_), min.y), ImVec2(xOf(cursor_), max.y), theme::alpha(p.accent, 0.7f));
        if (soundRow && sound == nullptr)
            ui::centredText(list, min, max, p.textFaint, L("Choisissez un son pour voir et dessiner ses couloirs", "Pick a sound to see and draw its lanes"));
        list->PopClipRect();
        if (!soundRow || sound != nullptr) {
            auto view = base;
            if (soundRow) {
                view.minimumTime = sound->startSeconds;
                view.maximumTime = sound->startSeconds + sound->durationSeconds;
            }
            auto& editor = curveEditors_[key];
            const auto wasFocused = editor.focused();
            if (editor.draw("##curve", min, max, points, lane, view))
                (void) setCurve(track, clip, events, name, points);
            // One lane takes the keys at a time: the one last clicked.
            if (editor.focused() && (!wasFocused || focusedLane_ != key)) {
                for (auto& [other, otherEditor] : curveEditors_)
                    if (other != key)
                        otherEditor.setFocused(false);
                focusedLane_ = key;
            }
        }
        if (ImGui::IsMouseHoveringRect(min, max) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
            wheel(clip, events);
        ImGui::PopID();
    }
    // A row's parameter changed or a row removed (after drawing all of them).
    auto& kept = laneRows_[clip.id];
    if (changeRow) {
        const auto [row, binding] = *changeRow;
        if (const auto other = std::find(kept.begin(), kept.end(), binding); other != kept.end() && row < kept.size())
            *other = kept[row];
        if (row < kept.size())
            kept[row] = binding;
        else
            kept.push_back(binding);
    }
    if (removeRow && *removeRow < rows.size()) {
        const auto name = rows[*removeRow];
        std::erase(kept, name);
        curveEditors_.erase(editorKey(name, events));
        if (focusedLane_ == editorKey(name, events))
            focusedLane_.clear();
        if (hasPoints(name)) {
            if (isSoundLane(name)) {
                (void) setCurve(track, clip, events, name, {});
            } else {
                auto lanes = clip.lanes;
                lanes.erase(name);
                (void) setLanes(track, clip, std::move(lanes), ui::Ls("Couloir retiré", "Lane removed"));
            }
        }
        if (kept.empty())
            kept.push_back(Studio::clipVolumeLane);
    }
    ImGui::SetCursorScreenPos(ImVec2(headerLeft, top + height));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

// ---- Keys -------------------------------------------------------------------------------------

void AudioEditorView::handleKeys(const TrackRef& track, const AudioClip& clip, const std::vector<AudioEvent>& events)
{
    const auto& io = ImGui::GetIO();
    // An automation lane clicked last has the keys (its points).
    if (!focusedLane_.isEmpty() || !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || io.WantTextInput)
        return;
    const auto selected = std::find_if(events.begin(), events.end(), [this](const AudioEvent& e) { return e.id == selectedEvent_; });
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && selected != events.end()) {
        if (removeEvent(track, clip, selected->id)) {
            selectedEvent_ = 0;
            studio_.markDirty();
        }
    } else if (ImGui::IsKeyPressed(ImGuiKey_S, false) && !io.KeyCtrl) {
        const auto target = std::find_if(events.begin(), events.end(), [this](const AudioEvent& e) {
            return cursor_ > e.startSeconds + 1.0e-4 && cursor_ < e.startSeconds + e.durationSeconds - 1.0e-4;
        });
        if (target != events.end())
            if (const auto made = splitEvent(track, clip, target->id, cursor_); made != 0) {
                selectedEvent_ = made;
                studio_.markDirty();
            }
    } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false) && selected != events.end()) {
        duplicate(track, clip, events, selected->id);
    } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
        selectionStart_ = 0.0;
        selectionEnd_ = clip.durationSeconds;
    } else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        selectionStart_ = selectionEnd_ = 0.0;
    }
}

// ---- Beside: details, processes, history ------------------------------------------------------------

void AudioEditorView::drawSide(const TrackRef& track, const AudioClip& clip, const std::vector<AudioEvent>& events, const ImVec2 size)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::palette().bg1);
    if (ImGui::BeginChild("##audioSide", size, ImGuiChildFlags_Borders)) {
        if (ImGui::BeginTabBar("##audioSideTabs")) {
            if (ImGui::BeginTabItem(L("Détails###details", "Details###details"))) {
                if (ImGui::BeginChild("##detailsScroll", ImVec2(0.0f, 0.0f)))
                    drawDetails(track, clip, events);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(L("Traitements###processes", "Processes###processes"))) {
                if (ImGui::BeginChild("##processesScroll", ImVec2(0.0f, 0.0f)))
                    drawProcesses(track, clip, events);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(L("Historique###history", "History###history"))) {
                drawHistory(track, clip);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void AudioEditorView::drawDetails(const TrackRef& track, const AudioClip& clip, const std::vector<AudioEvent>& events)
{
    const auto& p = theme::palette();
    auto& engine = studio_.engine();
    const auto labelWidth = dp(92.0f);
    // A field: one undo step for a drag of it.
    const auto field = [&](const char* label, const char* id, float* value, const float speed, const float minimum, const float maximum,
                           const char* format) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::vec(p.textDim), "%s", label);
        ImGui::SameLine(labelWidth);
        const auto edited = ui::dragNumber(id, value, speed, minimum, maximum, format, std::max(dp(60.0f), ImGui::GetContentRegionAvail().x));
        if (ImGui::IsItemActivated() && !fieldBatch_) {
            fieldBatch_ = true;
            batchRemembered_ = false;
            if (!launcher())
                engine.beginArrangementEditBatch();
        }
        if (ImGui::IsItemDeactivated() && fieldBatch_) {
            if (!launcher())
                (void) engine.endArrangementEditBatch();
            fieldBatch_ = false;
            studio_.markDirty();
        }
        return edited;
    };
    const auto bars = [&](const double seconds) {
        const auto beats = seconds / beatSeconds();
        const auto numerator = std::max(1, engine.timeSignatureNumerator());
        char text[48];
        std::snprintf(text, sizeof(text), "%.2f %s", beats / numerator, L("mesures", "bars"));
        return juce::String(text);
    };

    // ---- The clip ----
    ui::sectionTitle(L("CLIP", "CLIP"));
    const auto b = boundsOf(clip);
    auto duration = static_cast<float>(b.duration);
    auto playStart = static_cast<float>(b.playStart);
    auto playEnd = static_cast<float>(b.playEnd);
    auto loopStart = static_cast<float>(b.loopStart);
    auto loopEnd = static_cast<float>(b.loopEnd);
    auto looping = b.looping;
    auto bounds = false;
    if (!launcher()) {
        auto start = static_cast<float>(clip.startSeconds);
        auto fadeIn = static_cast<float>(clip.fadeInSeconds);
        auto fadeOut = static_cast<float>(clip.fadeOutSeconds);
        auto placed = field(L("Position", "Position"), "##clipStart", &start, 0.005f, 0.0f, 36'000.0f, "%.3f s");
        placed |= field(L("Fondu entrée", "Fade in"), "##clipFadeIn", &fadeIn, 0.002f, 0.0f, duration * 0.5f, "%.3f s");
        placed |= field(L("Fondu sortie", "Fade out"), "##clipFadeOut", &fadeOut, 0.002f, 0.0f, duration * 0.5f, "%.3f s");
        if (placed)
            (void) engine.updateAudioClip(track.audioIndex, clip.id, start, clip.sourceOffsetSeconds, clip.durationSeconds, fadeIn, fadeOut,
                                          clip.durationSeconds * speedOf(clip));
    }
    bounds |= field(L("Longueur", "Length"), "##clipLength", &duration, 0.005f, 0.02f, 36'000.0f, "%.3f s");
    ImGui::TextColored(theme::vec(p.textFaint), "%s", bars(duration).toRawUTF8());
    bounds |= field(L("Joue de", "Plays from"), "##playStart", &playStart, 0.005f, 0.0f, duration, "%.3f s");
    bounds |= field(L("à", "to"), "##playEnd", &playEnd, 0.005f, 0.0f, 36'000.0f, "%.3f s");
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::vec(p.textDim), "%s", L("Boucle", "Loop"));
    ImGui::SameLine(labelWidth);
    bounds |= ImGui::Checkbox("##looping", &looping);
    if (looping) {
        bounds |= field(L("de", "from"), "##loopStart", &loopStart, 0.005f, 0.0f, playEnd, "%.3f s");
        bounds |= field(L("à", "to"), "##loopEnd", &loopEnd, 0.005f, 0.0f, playEnd, "%.3f s");
        ImGui::TextColored(theme::vec(p.textFaint), "%s %s", L("Boucle de", "Loop of"), bars(loopEnd - loopStart).toRawUTF8());
    }
    if (bounds) {
        const auto length = std::max(static_cast<double>(duration), static_cast<double>(playEnd));
        const auto start = std::clamp(static_cast<double>(playStart), 0.0, length - 0.001);
        const auto stop = std::clamp(static_cast<double>(playEnd), start + 0.001, length);
        const auto from = std::clamp(static_cast<double>(loopStart), start, stop - 0.001);
        const auto to = std::clamp(static_cast<double>(loopEnd), from + 0.001, stop);
        (void) setBounds(track, clip, length, start, stop, from, to, looping);
        if (!ImGui::IsAnyItemActive())
            studio_.markDirty();
    }
    if (!launcher())
        ImGui::TextColored(theme::vec(p.textFaint), "%s", L("Dans l'arrangement, la boucle se règle ici ; elle joue avec la piste.",
                                                            "In the arrangement, the loop is set here; it plays with the track."));
    ImGui::Spacing();
    ImGui::Separator();

    // ---- The sound ----
    ui::sectionTitle(L("SON", "SOUND"));
    const auto found = std::find_if(events.begin(), events.end(), [this](const AudioEvent& e) { return e.id == selectedEvent_; });
    if (found == events.end()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", L("Cliquez un son pour voir son détail : son fichier, comment il est lu, étiré, transposé.",
                                    "Click a sound to see its detail: its file, how it is read, stretched, transposed."));
        ImGui::PopTextWrapPos();
        return;
    }
    const auto event = *found;
    ImGui::PushFont(theme::boldFont(), 0.0f);
    ImGui::TextUnformatted((event.name.isNotEmpty() ? event.name : event.sourceFile.getFileNameWithoutExtension()).toRawUTF8());
    ImGui::PopFont();
    const auto& format = formatOf(event.sourceFile);
    if (format.rate > 0.0)
        ImGui::TextColored(theme::vec(p.textDim), "%d Hz  \xc2\xb7  %s  \xc2\xb7  %d bits  \xc2\xb7  %.3f s", juce::roundToInt(format.rate),
                           format.channels == 1   ? L("mono", "mono")
                           : format.channels == 2 ? L("stéréo", "stereo")
                                                  : (juce::String(format.channels) + ui::Ls(" canaux", " channels")).toRawUTF8(),
                           format.bits, format.length);
    else
        ImGui::TextColored(theme::vec(p.record), "%s", L("Fichier introuvable ou illisible", "File missing or unreadable"));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::vec(p.textFaint), "%s", event.sourceFile.getFullPathName().toRawUTF8());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    auto start = static_cast<float>(event.startSeconds);
    auto length = static_cast<float>(event.durationSeconds);
    auto offset = static_cast<float>(event.sourceOffsetSeconds);
    const auto speed = event.sourceDurationSeconds / std::max(1.0e-6, event.durationSeconds);
    auto percent = static_cast<float>(speed * 100.0);
    auto placed = field(L("Début", "Start"), "##eventStart", &start, 0.005f, 0.0f, 36'000.0f, "%.3f s");
    placed |= field(L("Longueur", "Length"), "##eventLength", &length, 0.005f, 0.01f, 36'000.0f, "%.3f s");
    placed |= field(L("Dans le fichier", "In the file"), "##eventOffset", &offset, 0.005f, 0.0f, static_cast<float>(std::max(0.0, format.length)), "%.3f s");
    if (placed) {
        // Its speed kept: a longer sound reads more of its file.
        const auto newLength = std::max(0.01, static_cast<double>(length));
        (void) updateEvent(track, clip, event.id, std::max(0.0f, start), std::max(0.0f, offset), newLength, newLength * speed,
                           std::min(event.fadeInSeconds, newLength * 0.5), std::min(event.fadeOutSeconds, newLength * 0.5));
    }
    ImGui::BeginDisabled(event.stretchMode == StretchMode::raw);
    const auto speedChanged = field(L("Vitesse", "Speed"), "##eventSpeed", &percent, 0.2f, 5.0f, 2'000.0f, "%.1f %%");
    ImGui::EndDisabled();
    if (speedChanged) {
        // Stretched: the same part of its file over another length.
        const auto newLength = event.sourceDurationSeconds / std::max(0.05, static_cast<double>(percent) / 100.0);
        (void) updateEvent(track, clip, event.id, event.startSeconds, event.sourceOffsetSeconds, newLength, event.sourceDurationSeconds,
                           std::min(event.fadeInSeconds, newLength * 0.5), std::min(event.fadeOutSeconds, newLength * 0.5));
    }
    ui::tooltip(L("100 % : lu à sa vitesse ; au-dessus, plus vite (plus court) ; en dessous, plus lent",
                  "100 %: read at its own speed; above, faster (shorter); below, slower"));
    // Its stretch.
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::vec(p.textDim), "%s", L("Lecture", "Playing"));
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##eventMode", modeName(event.stretchMode))) {
        for (const auto mode : playModes) {
            if (ImGui::Selectable(modeName(mode), mode == event.stretchMode)
                && setEventProperties(track, clip, event.id, event.gainDb, event.sourceTempoBpm, mode))
                studio_.markDirty();
            ui::tooltip(modeHelp(mode));
        }
        ImGui::EndCombo();
    }
    ui::tooltip(modeHelp(event.stretchMode));
    auto tempo = static_cast<float>(event.sourceTempoBpm);
    if (field(L("Tempo du son", "Sound's tempo"), "##eventTempo", &tempo, 0.05f, 0.0f, 400.0f, tempo > 0.0f ? "%.2f BPM" : "?"))
        (void) setEventProperties(track, clip, event.id, event.gainDb, tempo, event.stretchMode);
    ImGui::SetCursorPosX(labelWidth);
    if (ImGui::SmallButton(L("Détecter", "Detect"))) {
        if (const auto detected = app::AudioTrackController::detectAudioTempo(event.sourceFile)) {
            if (setEventProperties(track, clip, event.id, event.gainDb, detected->bpm, event.stretchMode))
                studio_.markDirty();
            report(ui::Ls("Tempo trouvé : ", "Tempo found: ") + juce::String(detected->bpm, 2) + " BPM", false);
        } else {
            report(ui::Ls("Aucun tempo net dans ce son", "No clear tempo in this sound"), true);
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(event.sourceTempoBpm <= 0.0);
    if (ImGui::SmallButton(L("Caler sur le projet", "Fit to the project")))
        fitToTempo(track, clip, event);
    ImGui::EndDisabled();
    ui::tooltip(L("Sa longueur faite pour que son tempo devienne celui du projet", "Its length made so its tempo becomes the project's"));
    // Its transposition (set when the field is let go).
    ImGui::BeginDisabled(pitchFollowsSpeed(event.stretchMode));
    auto semitones = static_cast<float>(event.pitchSemitones);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::vec(p.textDim), "%s", L("Transposition", "Transpose"));
    ImGui::SameLine(labelWidth);
    (void) ui::dragNumber("##eventPitch", &semitones, 0.05f, -24.0f, 24.0f, "%+.2f st", std::max(dp(60.0f), ImGui::GetContentRegionAvail().x));
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        const auto pitch = std::round(static_cast<double>(semitones) * 100.0) / 100.0;
        if (!launcher() && !clip.usesEventTimeline) {
            if (engine.setAudioClipPitch(track.audioIndex, clip.id, pitch))
                studio_.markDirty();
        } else {
            auto all = events;
            for (auto& one : all)
                if (one.id == event.id)
                    one.pitchSemitones = pitch;
            if (const auto done = replaceEvents(track, clip, std::move(all)); done.wasOk())
                studio_.markDirty();
            else
                report(done.getErrorMessage(), true);
        }
    }
    ImGui::EndDisabled();
    if (pitchFollowsSpeed(event.stretchMode))
        ui::tooltip(L("En Vitesse et en Brut, la hauteur est celle de la vitesse : choisissez un autre mode pour transposer",
                      "With Repitch and Raw the pitch is the speed's: pick another mode to transpose"));
    // Its level and fades.
    auto gain = event.gainDb;
    if (field(L("Gain", "Gain"), "##eventGain", &gain, 0.1f, -60.0f, 24.0f, "%+.1f dB"))
        (void) setEventProperties(track, clip, event.id, gain, event.sourceTempoBpm, event.stretchMode);
    auto fadeIn = static_cast<float>(event.fadeInSeconds);
    auto fadeOut = static_cast<float>(event.fadeOutSeconds);
    const auto half = static_cast<float>(event.durationSeconds * 0.5);
    if (field(L("Fondu entrée", "Fade in"), "##eventFadeIn", &fadeIn, 0.002f, 0.0f, half, "%.3f s"))
        (void) updateEvent(track, clip, event.id, event.startSeconds, event.sourceOffsetSeconds, event.durationSeconds, event.sourceDurationSeconds,
                           fadeIn, -1.0);
    if (field(L("Fondu sortie", "Fade out"), "##eventFadeOut", &fadeOut, 0.002f, 0.0f, half, "%.3f s"))
        (void) updateEvent(track, clip, event.id, event.startSeconds, event.sourceOffsetSeconds, event.durationSeconds, event.sourceDurationSeconds,
                           -1.0, fadeOut);
    // Its stretch points.
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::vec(p.textDim), "%s", L("Points", "Points"));
    ImGui::SameLine(labelWidth);
    ImGui::Text("%d", static_cast<int>(event.stretchMarkers.size()));
    ImGui::SameLine();
    ImGui::BeginDisabled(event.stretchMarkers.empty());
    if (ImGui::SmallButton(L("Effacer", "Clear"))) {
        auto all = events;
        for (auto& one : all)
            if (one.id == event.id)
                one.stretchMarkers.clear();
        if (replaceEvents(track, clip, std::move(all)).wasOk())
            studio_.markDirty();
    }
    ImGui::EndDisabled();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::vec(p.textFaint), "%s", L("Double-clic en bas du son : un point d'étirement, à glisser ensuite. En bas, rouge : ralenti ; bleu : accéléré.",
                                                        "Double click low on the sound: a stretch point, to drag then. Along the bottom, red: slowed; blue: sped up."));
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (ImGui::Button(L("Écouter le son", "Hear the sound"), ImVec2(-1.0f, 0.0f))) {
        if (engine.startSamplePreview(event.sourceFile, event.sourceOffsetSeconds).wasOk())
            previewEnd_ = event.sourceOffsetSeconds + event.sourceDurationSeconds;
    }
}

void AudioEditorView::drawProcesses(const TrackRef& track, const AudioClip& clip, const std::vector<AudioEvent>& events)
{
    const auto& p = theme::palette();
    if (ImGui::BeginListBox("##audioProcesses", ImVec2(-1.0f, ImGui::GetTextLineHeightWithSpacing() * 9.4f))) {
        for (int i = 0; i < processCount; ++i)
            if (ImGui::Selectable(L(processAt(i).french, processAt(i).english), process_ == i))
                process_ = i;
        ImGui::EndListBox();
    }
    ImGui::Spacing();
    const auto& kind = processAt(process_);
    ImGui::PushFont(theme::boldFont(), 0.0f);
    ImGui::TextUnformatted(L(kind.french, kind.english));
    ImGui::PopFont();
    const auto found = std::find_if(events.begin(), events.end(), [this](const AudioEvent& e) { return e.id == selectedEvent_; });
    if (found == events.end()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(theme::vec(p.textFaint), "%s", L("Cliquez un son ; pour n'en traiter qu'une partie, sélectionnez-la (glisser sur la règle, ou Maj+glisser sur le son).",
                                                            "Click a sound; to process a part of it only, select it (drag on the ruler, or Shift+drag on the sound)."));
        ImGui::PopTextWrapPos();
        return;
    }
    const auto part = selectionIn(*found);
    const auto on = part ? ui::Ls("La sélection, ", "The selection, ") + juce::String(part->second, 3) + " s, "
                               + ui::Ls("de « ", "of « ") + found->name + ui::Ls(" »", " »")
                         : ui::Ls("Tout le son « ", "The whole sound « ") + found->name + ui::Ls(" »", " »");
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::vec(p.textFaint), "%s", on.toRawUTF8());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1.0f);
    switch (kind.op) {
    case Op::amplify: ImGui::SliderFloat("##processGain", &gainDb_, -48.0f, 24.0f, "%+.1f dB"); break;
    case Op::normalisePeak: ImGui::SliderFloat("##processNormalise", &normaliseDb_, -30.0f, 0.0f, L("crête %.1f dBFS", "peak %.1f dBFS")); break;
    case Op::fadeIn:
    case Op::fadeOut: {
        const char* curves[4] { L("Linéaire", "Linear"), L("Puissance égale", "Equal power"), L("Lente", "Slow"), L("Rapide", "Fast") };
        ImGui::Combo("##processCurve", &fadeCurve_, curves, 4);
        break;
    }
    default: break;
    }
    ImGui::Spacing();
    if (ui::primaryButton(L("Appliquer", "Apply"), ImVec2(-1.0f, 0.0f))) {
        app::AudioEditRequest request;
        request.operation = kind.op;
        request.levelDb = kind.op == Op::amplify ? gainDb_ : normaliseDb_;
        request.curve = static_cast<app::FadeCurve>(std::clamp(fadeCurve_, 0, 3));
        process(track, clip, *found, request, juce::String::fromUTF8(L(kind.french, kind.english)), part);
    }
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::vec(p.textFaint), "%s", L("Un traitement écrit un nouveau fichier ; l'original reste. Ctrl+Z l'annule.",
                                                        "A process writes a new file; the original stays. Ctrl+Z undoes it."));
    ImGui::PopTextWrapPos();
}

void AudioEditorView::drawHistory(const TrackRef& track, const AudioClip& clip)
{
    const auto& p = theme::palette();
    if (launcher()) {
        if (undo_.empty())
            ImGui::TextDisabled("%s", L("Aucune modification", "No change"));
        for (auto step = undo_.rbegin(); step != undo_.rend(); ++step)
            ImGui::BulletText("%s", step->label.toRawUTF8());
        ImGui::Spacing();
        ImGui::TextColored(theme::vec(p.textFaint), "%s", L("Ctrl+Z annule, Ctrl+Y rétablit.", "Ctrl+Z undoes, Ctrl+Y redoes."));
        return;
    }
    if (clip.versions.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", L("Aucune version. Les traitements d'un clip d'un seul son en gardent chaque version ici ; Ctrl+Z annule les autres.",
                                    "No version yet. The processes of a clip of one sound keep each version here; Ctrl+Z undoes the others."));
        ImGui::PopTextWrapPos();
        return;
    }
    for (std::size_t index = 0; index < clip.versions.size(); ++index) {
        const auto& version = clip.versions[index];
        ImGui::PushID(static_cast<int>(index));
        const auto current = index == clip.version;
        char text[160];
        std::snprintf(text, sizeof(text), "%d. %s", static_cast<int>(index), version.label.toRawUTF8());
        if (current)
            ImGui::PushStyleColor(ImGuiCol_Text, theme::vec(p.accent));
        if (ImGui::Selectable(text, current) && !current) {
            if (studio_.engine().setAudioClipVersion(track.audioIndex, clip.id, index))
                studio_.markDirty();
        }
        if (current)
            ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s\n%.3f s", version.file.getFileName().toRawUTF8(), version.durationSeconds);
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", L("Un clic rejoue une version ; un traitement après elle remplace les suivantes.",
                                "A click plays a version again; a process after it replaces the later ones."));
    ImGui::PopTextWrapPos();
}

// ---- PRISES ------------------------------------------------------------------------------------

void AudioEditorView::drawTakes(const TrackRef& track, const AudioClip& clip)
{
    const auto& p = theme::palette();
    auto& engine = studio_.engine();
    // A clip of several sounds has no takes of its own: each sound is set in the clip view.
    if (clip.usesEventTimeline) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", L("Ce clip est fait de plusieurs sons : il n'a pas de prises. Chaque son se règle dans la vue du clip.",
                                    "This clip is made of several sounds: it has no takes. Each sound is set in the clip view."));
        ImGui::PopTextWrapPos();
        return;
    }
    // Its takes: the clip's own source when it has none listed.
    struct TakeView {
        juce::String name;
        juce::File file;
        double shift {};
    };
    std::vector<TakeView> takes;
    for (const auto& take : clip.takes)
        takes.push_back({ take.name, take.sourceFile != juce::File {} ? take.sourceFile : clip.sourceFile, take.shiftSeconds });
    if (takes.empty())
        takes.push_back({ clip.name, clip.sourceFile, 0.0 });
    // The toolbar.
    if (ImGui::SmallButton(L("+ Prise depuis un fichier\xe2\x80\xa6", "+ Take from a file\xe2\x80\xa6"))) {
        const auto clipId = clip.id;
        chooser_ = std::make_unique<juce::FileChooser>(ui::Ls("Une prise pour ce clip", "A take for this clip"), juce::File(),
                                                       "*.wav;*.wave;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, track, clipId](const juce::FileChooser& chosen) {
                                  const auto file = chosen.getResult();
                                  if (file == juce::File())
                                      return;
                                  std::vector<app::AudioTrackController::AudioTake> added { { {}, file, 0.0, 0.0 } };
                                  const auto done = studio_.engine().addAudioTakes(track.audioIndex, clipId, std::move(added), true);
                                  report(done.wasOk() ? ui::Ls("Prise ajoutée : ", "Take added: ") + file.getFileName() : done.getErrorMessage(),
                                         done.failed());
                                  studio_.markDirty();
                              });
    }
    ui::tooltip(L("Un fichier comme nouvelle prise, calé sur le début du clip", "A file as a new take, lined up with the clip's start"));
    ImGui::SameLine();
    ImGui::BeginDisabled(clip.takes.size() < 2 || clip.comp.empty());
    if (ImGui::SmallButton(L("Mettre à plat", "Flatten"))) {
        const auto done = studio_.flattenAudioClipComp(track, clip.id);
        report(done.wasOk() ? ui::Ls("Prises mises à plat en un fichier (Ctrl+Z l'annule)", "Takes flattened into one file (Ctrl+Z undoes it)")
                            : done.getErrorMessage(),
               done.failed());
    }
    ImGui::EndDisabled();
    ui::tooltip(L("Les meilleurs passages réunis dans un seul fichier, comme ils jouent : le clip garde cette prise seule (les "
                  "éditions d'Échantillon s'y appliquent ensuite)",
                  "The best passages joined into one file, as they play: the clip keeps that take alone (the Sample "
                  "edits then apply to it)"));
    ImGui::SameLine();
    auto onClips = studio_.recordTakesOnClips();
    if (ImGui::Checkbox(L("Enregistrer sur un clip ajoute une prise", "Recording over a clip adds a take"), &onClips))
        studio_.setRecordTakesOnClips(onClips);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", L("En boucle, chaque tour devient une prise. Glissez sur une prise pour jouer ce passage-là ; un clic sur son nom la joue en entier.",
                                "Looped, each pass becomes a take. Drag over a take to play that passage from it; a click on its name plays it all."));

    const auto available = ImGui::GetContentRegionAvail();
    const auto nameWidth = dp(150.0f);
    const auto compHeight = dp(24.0f);
    const auto laneHeight = std::clamp((available.y - compHeight - dp(8.0f)) / static_cast<float>(takes.size()), dp(34.0f), dp(96.0f));
    const auto left = ImGui::GetCursorScreenPos().x + nameWidth;
    const auto width = std::max(dp(80.0f), available.x - nameWidth - dp(4.0f));
    const auto duration = std::max(0.01, clip.durationSeconds);
    const auto xAt = [&](const double t) { return left + static_cast<float>(t / duration) * width; };
    const auto tAt = [&](const float x) { return std::clamp(static_cast<double>(x - left) / width * duration, 0.0, duration); };
    const auto passages = passagesOf(clip);
    auto* list = ImGui::GetWindowDrawList();
    // The comp: which take plays where.
    {
        const auto top = ImGui::GetCursorScreenPos();
        list->AddText(ImVec2(top.x + dp(4.0f), top.y + dp(4.0f)), p.textDim, L("CE QUI JOUE", "WHAT PLAYS"));
        list->AddRectFilled(ImVec2(left, top.y + dp(2.0f)), ImVec2(left + width, top.y + compHeight - dp(2.0f)), p.bg0, dp(3.0f));
        for (const auto& passage : passages) {
            const auto colour = takeColour(passage.take);
            list->AddRectFilled(ImVec2(xAt(passage.startSeconds), top.y + dp(3.0f)), ImVec2(xAt(passage.endSeconds), top.y + compHeight - dp(3.0f)),
                                theme::alpha(colour, 0.85f), dp(2.0f));
            if (xAt(passage.endSeconds) - xAt(passage.startSeconds) > dp(30.0f)) {
                char text[16];
                std::snprintf(text, sizeof(text), "%d", static_cast<int>(passage.take) + 1);
                list->AddText(ImVec2(xAt(passage.startSeconds) + dp(4.0f), top.y + dp(5.0f)), IM_COL32(20, 20, 24, 255), text);
            }
        }
        ImGui::Dummy(ImVec2(available.x, compHeight));
    }
    // Each take: its waveform under the clip's timeline, bright where it plays.
    const auto speed = speedOf(clip);
    std::optional<std::size_t> removing;
    for (std::size_t index = 0; index < takes.size(); ++index) {
        const auto& take = takes[index];
        ImGui::PushID(static_cast<int>(index));
        const auto top = ImGui::GetCursorScreenPos();
        const auto colour = takeColour(index);
        // Its name: a click plays it all.
        const auto isActive = clip.comp.empty() && index == clip.activeTakeIndex;
        ImGui::PushStyleColor(ImGuiCol_Button, isActive ? theme::alpha(colour, 0.45f) : theme::alpha(colour, 0.15f));
        char name[160];
        std::snprintf(name, sizeof(name), "%d  %s##take", static_cast<int>(index) + 1, take.name.toRawUTF8());
        if (ImGui::Button(name, ImVec2(nameWidth - dp(6.0f), laneHeight - dp(4.0f)))) {
            if (engine.setActiveAudioTake(track.audioIndex, clip.id, index) || index == clip.activeTakeIndex) {
                (void) engine.setAudioClipComp(track.audioIndex, clip.id, {});
                studio_.markDirty();
            }
        }
        ImGui::PopStyleColor();
        ui::tooltip(L("Jouer cette prise en entier (clic droit : la retirer)", "Play this take all through (right click: remove it)"));
        if (ImGui::BeginPopupContextItem("##takeMenu")) {
            ImGui::BeginDisabled(takes.size() < 2);
            if (ImGui::MenuItem(L("Retirer cette prise", "Remove this take")))
                removing = index;
            ImGui::EndDisabled();
            ImGui::EndPopup();
        }
        // Its wave.
        const ImVec2 waveMin(left, top.y);
        const ImVec2 waveMax(left + width, top.y + laneHeight - dp(4.0f));
        list->AddRectFilled(waveMin, waveMax, p.bg0, dp(3.0f));
        auto* thumbnail = waveforms_.get(take.file);
        if (thumbnail != nullptr) {
            if (!thumbnail->isFullyLoaded())
                host_.wake(2);
            const auto centre = (waveMin.y + waveMax.y) * 0.5f;
            const auto half = (waveMax.y - waveMin.y) * 0.45f;
            for (auto x = 0.0f; x < width; x += 1.0f) {
                const auto t0 = static_cast<double>(x) / width * duration;
                const auto t1 = static_cast<double>(x + 1.0f) / width * duration;
                const auto s0 = clip.sourceOffsetSeconds + t0 * speed + take.shift;
                const auto s1 = clip.sourceOffsetSeconds + t1 * speed + take.shift;
                if (s1 <= 0.0)
                    continue;
                float low = 0.0f;
                float high = 0.0f;
                thumbnail->getApproximateMinMax(std::max(0.0, s0), s1, 0, low, high);
                auto plays = false;
                for (const auto& passage : passages)
                    plays = plays || (passage.take == index && t0 >= passage.startSeconds && t0 < passage.endSeconds);
                list->AddRectFilled(ImVec2(left + x, centre - high * half), ImVec2(left + x + 1.0f, centre - low * half + 1.0f),
                                    plays ? colour : theme::alpha(p.textFaint, 0.45f));
            }
        }
        // Dragging over it: that passage from this take.
        ImGui::SetCursorScreenPos(waveMin);
        ImGui::InvisibleButton("##lane", ImVec2(width, waveMax.y - waveMin.y));
        const auto mouseT = tAt(ImGui::GetIO().MousePos.x);
        if (ImGui::IsItemActivated()) {
            compLane_ = static_cast<int>(index);
            compFrom_ = compTo_ = mouseT;
        }
        if (compLane_ == static_cast<int>(index) && ImGui::IsItemActive())
            compTo_ = mouseT;
        if (compLane_ == static_cast<int>(index)) {
            const auto a = std::min(compFrom_, compTo_);
            const auto b = std::max(compFrom_, compTo_);
            list->AddRectFilled(ImVec2(xAt(a), waveMin.y), ImVec2(xAt(b), waveMax.y), theme::alpha(p.accent, 0.25f));
        }
        if (compLane_ == static_cast<int>(index) && ImGui::IsItemDeactivated()) {
            const auto a = std::min(compFrom_, compTo_);
            const auto b = std::max(compFrom_, compTo_);
            compLane_ = -1;
            if (b - a > 0.01) {
                // The passage taken from the others, given to this take.
                std::vector<app::AudioTrackController::CompSegment> next;
                for (const auto& passage : passages) {
                    if (passage.endSeconds <= a || passage.startSeconds >= b) {
                        next.push_back(passage);
                        continue;
                    }
                    if (passage.startSeconds < a)
                        next.push_back({ passage.startSeconds, a, passage.take });
                    if (passage.endSeconds > b)
                        next.push_back({ b, passage.endSeconds, passage.take });
                }
                next.push_back({ a, b, index });
                if (engine.setAudioClipComp(track.audioIndex, clip.id, std::move(next)))
                    studio_.markDirty();
            }
        }
        ImGui::SetCursorScreenPos(ImVec2(top.x, top.y + laneHeight));
        ImGui::PopID();
    }
    if (removing) {
        if (engine.removeAudioTake(track.audioIndex, clip.id, *removing)) {
            (void) engine.setAudioClipComp(track.audioIndex, clip.id, {});
            studio_.markDirty();
        }
    }
}

} // namespace cydaw::v2
