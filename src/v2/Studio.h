#include <cydaw/base/Platform.h>
#include <cydaw/base/Utilities.h>
#include <cydaw/base/Thread.h>
#include <cydaw/base/Properties.h>
#include <cydaw/base/File.h>
#pragma once

#include "AiModels.h"
#include "AudioTrackController.h"
#include "Companion.h"
#include "Locations.h"
#include "GraphDocument.h"
#include "SampleLibrary.h"
#include "SemanticSearch.h"
#include "SoundBanks.h"
#include "AudioEdit.h"
#include "DeviceHost.h"
#include "TrackModulation.h"
#include "Processing.h"


#include <array>
#include <bitset>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

// The session of CyDAW 2, without any interface: the engine, the tracks with
// what the engine does not keep (names, colours, graphs), the project file,
// the settings and the meters the interface reads. Everything here runs on
// the message thread.
//
// Each track owns its graph model — a GraphDocument that is never
// shown, used for its model, its catalogue and its compiler. An edit made on
// it (by the v2 graph editor, the device panel, the control port) calls back
// here, and the result is published to the engine at once.

namespace cydaw::v2 {

struct MeterState {
    // The left side's speakers and the right side's (a centre on both).
    std::array<float, 2> peak {};
    std::array<float, 2> rms {};
    // Each channel of its format, for the routing's meters.
    std::array<float, 8> channelPeak {};
    int channels { 2 };
};

// A launcher clip of the master (one a scene): its devices' knobs, and its
// level, automated from when it is launched, over its length, looping.
struct MasterClip {
    cydaw::base::Text name;
    std::uint32_t colour { 0xffffb84d };
    double lengthBeats { 16.0 };
    bool looping { true };
    struct Point {
        double beats {};
        float value {};
        // Its shape (engine::CurveShape), bend and handles (their time in
        // beats).
        int shape {};
        float bend {};
        engine::CurveHandles handles {};
        // Its editor's (0: none yet, one is given when it is edited).
        std::uint64_t id {};
    };
    // By binding (a master device's knob, or Studio::masterGainLane for its
    // level): points in time order, beats from the clip's start, in the
    // knob's own units.
    std::map<cydaw::base::Text, std::vector<Point>> lanes;
};

// A track's device automation: each lane a published parameter of its
// graph, by binding, and its points in time order, values in the
// parameter's own units (the points' valueDb).
struct ParameterLanes {
    std::map<cydaw::base::Text, std::vector<app::AudioTrackController::AutomationPoint>> lanes;
    // The lane the arrangement shows under the track: empty for the volume.
    cydaw::base::Text shown;
    std::uint64_t nextId { 1 };
};

// One of a track's outputs: where its sound goes (a track's routing key,
// 0 the master), at what level.
struct TrackOutput {
    std::uint64_t destination {};
    float gainDb {};
    // Which of the destination's inputs: 0 its main one, else the graph node
    // of another (a sidechain), as trackInputs() lists them.
    std::uint64_t input {};
    // Taken before its own fader: what it sends does not follow the fader.
    bool preFader {};
    // A virtual output of the software (its bus id) instead: straight there,
    // not through the master; the destination is then 0.
    std::uint64_t hardware {};
    // Into it after its effects (skipping them), rather than through them.
    bool hardwareAfterEffects {};
    std::uint32_t instrumentBus {};
    [[nodiscard]] bool operator==(const TrackOutput&) const = default;
};

// An audio input of a track beside its main one: other tracks are routed
// into it (a sidechain, a second source). Its graph node, its name.
struct TrackInput {
    std::uint64_t node {};
    cydaw::base::Text name;
};

struct AudioTrackState {
    cydaw::base::Text name;
    // The launcher's empty slots whose stop was taken off, a bit per scene.
    std::bitset<app::AudioTrackController::maximumLauncherScenes> launcherStopOff;
    // Stable while tracks move: what routing names it by.
    std::uint64_t uid {};
    // A bus: a track without clips that other tracks are routed into.
    bool bus {};
    // Empty: the master alone.
    std::vector<TrackOutput> outputs;
    std::uint32_t colour {};       // 0xAARRGGBB
    int audioChannels { 2 };
    int laneHeight { 88 };
    bool automationVisible {};
    ParameterLanes parameterLanes;
    // Runtime only: the launcher scene and the arrangement clips its clips'
    // lanes were last published for.
    int clipLaneScene { -1 };
    double clipLaneSignature {};
    std::unique_ptr<GraphSlot> graph;
    // The value graph that modulates the track and its assignments, kept as
    // the project stores them.
    cydaw::base::Value modulationGraph;
    cydaw::base::Value modulationAssignments;
    // Runtime only: the graph revision the assignments follow, and what the
    // modulation last applied.
    std::uint64_t modulationSyncedRevision {};
    std::uint64_t modulationAppliedRevision {};
    double modulationAppliedAt { -1.0 };
    double modulationOutput {};
    bool modulationActive {};
    // The value graph as cards, made when it is first edited or played;
    // its changes are written back into modulationGraph.
    std::unique_ptr<GraphSlot> modulationSlot;
    // Each hosted plug-in's knobs on its card, as pinned (by plug-in index;
    // empty: never picked, the card shows its first parameters).
    std::vector<std::optional<std::vector<std::uint32_t>>> pluginPins;
    // Runtime only: how many plug-ins the modulation's list was made with,
    // and what the modulation last sent each plug-in parameter it moves.
    std::size_t modulationSyncedPlugins {};
    std::map<cydaw::base::Text, float> pluginModulationSent;
};

// A pad of a Drum Machine, a layer of an Instrument Layer or Selector
// (InstrumentContainers.h): the key that plays a pad, its name, the note it
// plays (-1: its key's own), its level, pan, mute, solo and choke group, a
// layer's keys and velocities, and its graph (an instrument and its effects).
struct RackPadState {
    int note { 36 };
    cydaw::base::Text name;
    int playAs { -1 };
    float gainDb {};
    float pan {};
    bool muted {};
    bool solo {};
    int choke {};
    int keyLow {};
    int keyHigh { 127 };
    int velocityLow { 1 };
    int velocityHigh { 127 };
    std::unique_ptr<GraphSlot> graph;
};

struct MidiTrackState {
    std::uint64_t id {};
    // Its instrument container's pads or layers, and the container they were
    // made for (they sound while the track's instrument is that one).
    std::vector<RackPadState> pads;
    engine::instrument_container::Kind padsKind { engine::instrument_container::Kind::drum };
    std::bitset<app::AudioTrackController::maximumLauncherScenes> launcherStopOff;
    std::uint32_t colour {};
    std::vector<TrackOutput> outputs;
    int laneHeight { 88 };
    bool automationVisible {};
    ParameterLanes parameterLanes;
    std::unique_ptr<GraphSlot> graph;
};

struct BusState {
    std::uint64_t id {};
    std::unique_ptr<GraphSlot> graph;
};

namespace project_io {
class DocumentLoad;
}

class Studio final : public DeviceHost {
    friend class ModeProcessing;
public:
    explicit Studio(bool auxiliary = false);
    ~Studio();

    // ---- Audio device -------------------------------------------------------
    // Starts the audio device from the saved setup (or the default one).
    // Returns an error message, empty on success.
    cydaw::base::Text startAudio(bool restoreSavedDevice);
    void saveAudioSetup();
    [[nodiscard]] cydaw::base::Text audioStatus() const;

    // ---- Ticking --------------------------------------------------------------
    // Called about 30 times a second: plugin health, MIDI recording, the
    // master export, the meters, the live values of the shown graph.
    void tick();

    [[nodiscard]] app::AudioTrackController& engine() noexcept { return engine_; }
    [[nodiscard]] const app::AudioTrackController& engine() const noexcept { return engine_; }
    // The sample library the browser shows (folders, the banks, cache,
    // analysis), and its optional semantic search with the models it runs.
    [[nodiscard]] app::SampleLibrary& samples() noexcept { return *samples_; }
    // A sample heard from the library: a loop with a tempo, at the project's
    // when `atTempo` (stretched as its family asks: drums by their attacks,
    // the rest keeping their tone), anything else as it is.
    cydaw::base::Result auditionSample(const cydaw::base::Text& path, double startSeconds, bool atTempo);
    // The tempo the sample auditioned now was taken at, 0 when as it is.
    [[nodiscard]] double auditionTempoOf(const cydaw::base::Text& path) const;
    [[nodiscard]] AiModels& aiModels() noexcept { return aiModels_; }
    [[nodiscard]] SemanticSearch& semantic() noexcept { return *semantic_; }
    // The library started: the banks' catalogue read (on a thread of its
    // own), then the cache, then the scans. `automatic`: the semantic search
    // indexes new sounds by itself too. Automated sessions start it when
    // something asks for samples (ensureSamples).
    void startSamples(bool automatic);
    void ensureSamples();
    // The banks on this machine given to the library (a download ended, the
    // banks' folder moved).
    void syncSampleBanks();
    [[nodiscard]] cydaw::base::PropertiesFile& settings();

    // ---- Where things are kept (Options › Emplacements) -----------------------
    // The chosen folders put to use: the banks' samples, the sample index.
    void applyLocations();
    // `location` kept in `folder` from now on ({}: its default). With
    // `takeContent`, what CyDAW put in the old folder goes there first, on a
    // thread, and the change applies when it is done. False with `error`.
    bool setLocation(Location location, const cydaw::base::File& folder, bool takeContent, cydaw::base::Text& error);
    struct LocationMove {
        bool active {};
        Location location {};
        cydaw::base::File to;
        locations::Move::Progress progress;
    };
    [[nodiscard]] LocationMove locationMove() const;
    void cancelLocationMove();
    // Every location CyDAW downloads, builds and installs into, in `root`
    // (each in its own folder there: Banks, Downloads, Cache, companion,
    // Models), one move after another; with `creations`, what the person makes
    // too (sound models, devices, presets, exports, mixes, Foundry files).
    // False with `error` when a move runs already.
    bool setAllLocations(const cydaw::base::File& root, bool creations, bool takeContent, cydaw::base::Text& error);
    // The locations still to change after the move that runs.
    [[nodiscard]] int pendingLocations() const noexcept { return static_cast<int>(pendingLocations_.size()); }

    // ---- Presets (Presets.h) ------------------------------------------------------
    // What has settings, as a preset holds it and puts it back:
    // a card of a track's chain (its knobs by key, a sampler's map, a model
    // card's model, a wavetable's table; one undo step), a hosted plug-in
    // (its whole state), a track's whole chain (an instrument and its effects).
    using PresetHolder = v2::PresetHolder;
    [[nodiscard]] std::optional<PresetHolder> devicePresetHolder(const TrackRef& track, std::uint64_t nodeId) const override;
    [[nodiscard]] std::optional<PresetHolder> pluginPresetHolder(const TrackRef& track, int pluginIndex) const;
    [[nodiscard]] std::optional<PresetHolder> chainPresetHolder(const TrackRef& track) const;
    [[nodiscard]] cydaw::base::Value devicePresetState(const TrackRef& track, std::uint64_t nodeId) const override;
    bool applyDevicePreset(const TrackRef& track, std::uint64_t nodeId, const cydaw::base::Value& state, cydaw::base::Text& error) override;
    cydaw::base::Result addDevicePreset(const TrackRef& track, const cydaw::base::Text& key,
                                       const cydaw::base::Value& state, std::uint64_t before = 0);
    cydaw::base::Result addFactoryChain(const TrackRef& track, const cydaw::base::Text& key, std::uint64_t before = 0);
    cydaw::base::Result addEffectGraph(const TrackRef& track, const cydaw::base::Value& graph,
                                      const cydaw::base::Text& name, std::uint64_t before = 0);

    // ---- Audio editing (a clip's own sound) -----------------------------------------
    // Where destructive edits' results are written: the project's folder
    // ("edits"), else Documents/CyDAW/Edits.
    [[nodiscard]] cydaw::base::File audioEditFolder() const;
    // ---- Processings (Processing.h, the Traitements mode) ----------------------------
    // An audio clip's sound (its source, the take it plays) to process.
    cydaw::base::Result readAudioClipSound(const TrackRef& track, std::uint64_t clipId, engine::repair::Channels& channels,
                                    double& rate) const;
    // A processing's result (of that whole sound) as the clip's new version
    // (its history keeps the others; one undo step): the clip longer when the
    // processing rang past the end it played to, shorter when it trimmed.
    cydaw::base::Result applyAudioProcessing(const TrackRef& track, std::uint64_t clipId, const processing::AudioResult& result,
                                      double rate, std::int64_t sourceFrames, const cydaw::base::Text& label);
    // A MIDI clip's notes (and its length, in beats) for a note script.
    [[nodiscard]] std::vector<processing::ScriptNote> midiClipNotes(const TrackRef& track, std::uint64_t clipId,
                                                                    double& clipLength) const;
    // Its notes made the processed ones (each keeping what it carries: its
    // expressions). With `undoable`, one undo step; without, a preview to put
    // back (restoreMidiClipNotes).
    cydaw::base::Result setMidiClipNotes(const TrackRef& track, std::uint64_t clipId, const std::vector<processing::ScriptNote>& notes,
                                  bool undoable);
    void restoreMidiClipNotes(const TrackRef& track, std::uint64_t clipId);

    // A destructive edit of a clip's selection (seconds in its source):
    // rendered to a new file, which the clip then plays; its history keeps
    // every version. One undo step (Ctrl+Z).
    cydaw::base::Result editAudioClip(const TrackRef& track, std::uint64_t clipId, const app::AudioEditRequest& request,
                               double sourceStart, double sourceLength, const cydaw::base::Text& label);
    // A clip's comp made into one file (its best passages joined, as they
    // play): the clip keeps that take alone. One undo step.
    cydaw::base::Result reverseAudioClip(const TrackRef& track, std::uint64_t id);
    cydaw::base::Result reverseLauncherClip(const TrackRef& track, int scene);
    cydaw::base::Result flattenAudioClipComp(const TrackRef& track, std::uint64_t clipId);
    // An event of a clip edited in place (normalize, gain, reverse... what
    // keeps its length): a new file, which the event then plays.
    // `sourceStart` and `sourceLength` (seconds of its file) edit a part of
    // what it plays; a negative start: all it plays.
    cydaw::base::Result editAudioEvent(const TrackRef& track, std::uint64_t clipId, std::uint64_t eventId,
                                const app::AudioEditRequest& request, double sourceStart = -1.0, double sourceLength = 0.0);
    // The same for an event of a launcher slot's audio clip.
    cydaw::base::Result editLauncherAudioEvent(const TrackRef& track, int scene, std::uint64_t eventId, const app::AudioEditRequest& request,
                                        double sourceStart = -1.0, double sourceLength = 0.0);
    // A stretch of a source copied, for paste.
    struct AudioClipboard {
        cydaw::base::File file;
        double start {};
        double length {};
    };
    void copyAudio(const cydaw::base::File& file, double start, double length);
    [[nodiscard]] const std::optional<AudioClipboard>& audioClipboard() const noexcept { return audioClipboard_; }
    // Recording over a clip makes a take of it (else a clip of its own).
    [[nodiscard]] bool recordTakesOnClips() const noexcept { return recordTakesOnClips_; }
    void setRecordTakesOnClips(const bool takes) noexcept { recordTakesOnClips_ = takes; }

    // ---- Hosted plug-ins (VST3) ----------------------------------------------------
    // The parameters a plug-in's card shows as knobs, as pinned (nullopt:
    // never picked; the card then shows its first automatable ones). Saved
    // with the project.
    [[nodiscard]] std::optional<std::vector<std::uint32_t>> pluginPins(const TrackRef& track, std::size_t plugin) const;
    void setPluginPins(const TrackRef& track, std::size_t plugin, std::vector<std::uint32_t> pins);
    // A plug-in taken out, or moved one place (direction -1, +1): its pins
    // and the modulations of its parameters follow it.
    void removePlugin(const TrackRef& track, std::size_t plugin);
    bool movePlugin(const TrackRef& track, std::size_t plugin, int direction);

    // ---- Prisme's own tables ------------------------------------------------------
    // A sound file made the table of a Prisme card's oscillator (0 A, 1 B),
    // played instead of the factory one: a wavetable file (cycles of 2048
    // samples, or the length its "clm " chunk gives), a short file as one
    // cycle, any other sound cut into cycles at its pitch. Kept in the card
    // (the project, presets, an exported plug-in). One undo step.
    bool importPrismTable(const TrackRef& track, std::uint64_t nodeId, int oscillator, const cydaw::base::File& file,
                          cydaw::base::Text& error) override;
    // Back to the factory table its setting names.
    bool clearPrismTable(const TrackRef& track, std::uint64_t nodeId, int oscillator) override;

    // ---- A cabinet's impulse response ---------------------------------------------
    // A recorded cabinet (and its microphone) as a sound file, played by a
    // "Baffle et micro" card instead of its models: one or two channels, two
    // seconds at most, its silences before and after trimmed. Kept in the
    // card (project, presets, an exported plug-in). One undo step.
    bool loadCabImpulse(const TrackRef& track, std::uint64_t nodeId, const cydaw::base::File& file, cydaw::base::Text& error) override;
    bool clearCabImpulse(const TrackRef& track, std::uint64_t nodeId) override;
    [[nodiscard]] cydaw::base::Value pluginPresetState(const TrackRef& track, int pluginIndex);
    bool applyPluginPreset(const TrackRef& track, int pluginIndex, const cydaw::base::Value& state, cydaw::base::Text& error);
    // A hosted plug-in's own programs (its factory presets) and the current
    // one; choosing one (the plug-in's state then changes, as a preset's).
    bool pluginPrograms(const TrackRef& track, int pluginIndex, cydaw::base::TextList& names, int& current);
    bool selectPluginProgram(const TrackRef& track, int pluginIndex, int program, cydaw::base::Text& error);
    [[nodiscard]] cydaw::base::Value chainPresetState(const TrackRef& track) const;
    bool applyChainPreset(const TrackRef& track, const cydaw::base::Value& state, cydaw::base::Text& error);
    // CyDAW's whole chains of effects a track can take in place of its own
    // (an audio track's, a bus's, the master's; none for an instrument
    // track), as key and name, and one of them applied: every card replaced.
    // The track's preset button has them; a "+" never does.
    [[nodiscard]] std::vector<std::pair<cydaw::base::Text, cydaw::base::Text>> factoryChains(const TrackRef& track) const;
    cydaw::base::Result applyFactoryChain(const TrackRef& track, const cydaw::base::Text& key);
    // The preset last loaded or saved on a holder (by its key), shown on it.
    [[nodiscard]] cydaw::base::Text presetName(const cydaw::base::Text& holderKey) const override;
    void setPresetName(const cydaw::base::Text& holderKey, const cydaw::base::Text& name) override;

    // ---- Sound banks -----------------------------------------------------------
    // The banks CyDAW knows, what of them is here, their downloads. A
    // download that ends loads again the samplers that play its banks.
    [[nodiscard]] SoundBanks& banks() noexcept { return banks_; }
    // Starts downloading what a factory instrument's samples still need;
    // false (with `error`) when nothing is to be had.
    bool downloadFactoryInstrument(const cydaw::base::Text& key, cydaw::base::Text& error);
    // The samples a track's sampler cards play that are not here yet.
    [[nodiscard]] SoundBanks::Missing missingSamples(const TrackRef& track);
    // The sounds a generated bank (CyDAW Electro) has that the samplers play
    // (one track's, or every track's) and that are not here yet: made now,
    // nothing to ask, nothing downloaded. False when none were missing.
    bool generateMissingSounds(std::optional<TrackRef> only = std::nullopt);

    // Messages for the status bar from work done in the background (a
    // download, a move), taken once; `second` true for a failure.
    [[nodiscard]] std::vector<std::pair<cydaw::base::Text, bool>> takeNotices();

    // ---- Project --------------------------------------------------------------
    void newProject();
    cydaw::base::Result loadProject(const cydaw::base::File& file);
    // Opening a project without stopping the interface: the file read and
    // decoded on a thread of its own, then the tracks a few at a time
    // between frames. While it goes on, nothing else touches the session.
    struct ProjectOpening {
        bool active {};
        cydaw::base::File file;
        bool reading {};     // the file being read and decoded
        float progress {};   // 0..1 (an estimate while reading)
        cydaw::base::Text doing;  // what is being done, for a person to read
        std::int64_t bytes {};
        double seconds {};
        // A project of another software being imported (not a .cydaw).
        bool import {};
    };
    bool startOpeningProject(const cydaw::base::File& file);
    // The same for a project of another software (Bitwig, Ableton, Studio
    // One, DAWproject): read on a thread, its tracks made a few a frame.
    // What it brought (importSummary) once done.
    bool startImportingProject(const cydaw::base::File& file);
    [[nodiscard]] const cydaw::base::Text& importSummary() const noexcept { return importSummary_; }
    // About budgetMs of the work, once a frame; the outcome once the project
    // is open (or could not be), nothing while it goes on.
    std::optional<cydaw::base::Result> continueOpeningProject(double budgetMs);
    [[nodiscard]] ProjectOpening projectOpening() const;
    [[nodiscard]] bool openingProject() const noexcept { return opening_ != nullptr; }
    cydaw::base::Result saveProject(const cydaw::base::File& file);
    [[nodiscard]] const cydaw::base::File& projectFile() const noexcept { return projectFile_; }
    [[nodiscard]] cydaw::base::Text projectName() const;
    [[nodiscard]] bool isDirty() const noexcept { return dirty_; }
    void markDirty() noexcept { dirty_ = true; }
    // "Don't save": the changes are dropped, with their automatic copy.
    void discardChanges();
    // What the last load could not bring back (missing audio, plugins…).
    [[nodiscard]] const cydaw::base::TextList& loadWarnings() const noexcept { return loadWarnings_; }
    // The ready-to-play projects next to the executable.
    [[nodiscard]] cydaw::base::Array<cydaw::base::File> templates() const;
    // Another application's project (Ableton, Bitwig, Studio One, DAWproject).
    cydaw::base::Result importProject(const cydaw::base::File& file, cydaw::base::Text& summary);

    // ---- Autosave and recovery -------------------------------------------------------
    // While the session has unsaved changes, a copy is written in the
    // background every 45 seconds; a clean save or quit removes it.
    [[nodiscard]] static cydaw::base::File recoveryFile();
    // A session left by a crash: when it was written, empty if none.
    [[nodiscard]] cydaw::base::Text pendingRecoveryDescription() const;
    cydaw::base::Result restoreRecovery();
    void discardRecovery();
    // On quit: written when there are unsaved changes, removed otherwise.
    void finishRecovery();
    void setAutosaveEnabled(bool enabled) noexcept { autosaveEnabled_ = enabled; }
    // A person's session (not an automated one): what it changes of the
    // settings is kept.
    [[nodiscard]] bool keepsSettings() const noexcept { return autosaveEnabled_; }

    // ---- Tracks -----------------------------------------------------------------
    // Every track in the order shown: one order for every kind, a new
    // track at its end. Set from a project (its tracks' routing keys).
    [[nodiscard]] std::vector<TrackRef> tracks() const;
    void setTrackOrder(std::vector<std::uint64_t> keys);
    // The tracks shown: those a folded group hides left out.
    [[nodiscard]] std::vector<TrackRef> shownTracks() const;

    // ---- Groups ---------------------------------------------------------------
    // Tracks gathered under a group track, as Bitwig's: a bus their sound
    // goes to (then the master), shown before them, folding them away; its
    // launcher slots launch theirs. Made from the tracks given (in the order
    // shown), undone with its bus taken away (they go to the master again).
    std::optional<TrackRef> groupTracks(const std::vector<TrackRef>& tracks);
    bool ungroup(const TrackRef& group);
    [[nodiscard]] bool isGroupTrack(const TrackRef& track) const;
    // A bus beside the master (an effect's): a bus track that is no group.
    [[nodiscard]] bool isEffectBus(const TrackRef& track) const { return isBusTrack(track) && !isGroupTrack(track); }
    [[nodiscard]] std::optional<TrackRef> groupOf(const TrackRef& track) const;
    [[nodiscard]] std::vector<TrackRef> groupMembers(const TrackRef& group) const;
    [[nodiscard]] bool isFolded(const TrackRef& group) const;
    void setFolded(const TrackRef& group, bool folded);
    [[nodiscard]] bool hiddenByFold(const TrackRef& track) const;
    // A group's slot: whether one of its tracks has a clip there, plays it,
    // waits to.
    [[nodiscard]] bool groupHasClips(const TrackRef& group, int scene) const;
    [[nodiscard]] bool groupPlaysScene(const TrackRef& group, int scene) const;
    [[nodiscard]] bool groupWaitsScene(const TrackRef& group, int scene) const;
    // A launcher clip dragged to another slot, of a track of its kind
    // (copy: it stays where it was too); the slot it lands on loses what it
    // had.
    cydaw::base::Result moveLauncherClip(const TrackRef& from, int fromScene, const TrackRef& to, int toScene, bool copy);
    // From a project: a group track and its tracks (routing keys).
    void restoreGroup(std::uint64_t group, const std::vector<std::uint64_t>& members, bool folded);

    // ---- Hybrid tracks --------------------------------------------------------------
    // Every track takes clips of both kinds, as Bitwig's: those of the other
    // kind live on a companion track the interface never lists, routed into
    // the track's main input (an audio companion into an instrument track,
    // mixed after its instrument; an instrument companion, its instrument and
    // its notes, into an audio track). It is made with the first such clip,
    // goes with its track, takes its name and colour, follows its solo.
    [[nodiscard]] std::optional<TrackRef> companionOf(const TrackRef& track) const;
    [[nodiscard]] bool isCompanion(const TrackRef& track) const;
    // The track a companion belongs to; any other track itself.
    [[nodiscard]] TrackRef shownTrackOf(const TrackRef& track) const;
    // Where a track's clips of a kind live: itself or its companion; none
    // yet (or a bus, a group: their own kind only).
    [[nodiscard]] std::optional<TrackRef> partFor(const TrackRef& track, bool midi) const;
    // The same, its companion made when it has none (an instrument
    // companion gets the default instrument).
    std::optional<TrackRef> makePartFor(const TrackRef& track, bool midi);
    // What holds a slot's clip: the track or its companion.
    [[nodiscard]] std::optional<TrackRef> slotPart(const TrackRef& track, int scene) const;
    // Every track, the companions with them: what plays, what is published.
    [[nodiscard]] std::vector<TrackRef> allTracks() const;
    // From a project (routing keys).
    void restoreCompanion(std::uint64_t track, std::uint64_t companion, bool recordsAudio = false);
    // A hybrid instrument track records its sound too (its transport
    // recording): only once an audio input was chosen for it (none at
    // first: never the microphone unasked).
    [[nodiscard]] bool recordsHybridAudio(const TrackRef& track) const;
    void setRecordsHybridAudio(const TrackRef& track, bool records);
    // A new MIDI clip, a bar of 4/4 looping, in a slot of any track (an audio
    // track's companion holds it): that track and the clip's id. What the
    // slot had goes.
    std::optional<std::pair<TrackRef, std::uint64_t>> newSlotMidiClip(const TrackRef& track, int scene);
    // An audio file put into a slot of any track (an instrument track's
    // companion holds it), what the slot had replaced.
    cydaw::base::Result placeSlotAudio(const TrackRef& track, int scene, const cydaw::base::File& file);
    // A new empty audio clip, a bar long, looping, in a slot of any track
    // (an instrument track's companion holds it): that track. What the slot
    // had goes.
    std::optional<TrackRef> newSlotAudioClip(const TrackRef& track, int scene);

    // An armed track's empty slot recorded into (its round button): a new
    // clip there from the next bar (the bar it is on when stopped), its MIDI
    // notes or its audio input; the same button stops it, the clip then as
    // long as what was played (whole bars), looping, launched.
    // `midi`: the kind recorded (a hybrid track records the other one into
    // its companion); none: the track's own.
    cydaw::base::Result recordIntoSlot(const TrackRef& track, int scene, std::optional<bool> midi = std::nullopt);
    cydaw::base::Result stopSlotRecording(bool keep = true);
    [[nodiscard]] std::optional<std::pair<TrackRef, int>> slotRecording() const { return slotRecording_; }
    [[nodiscard]] std::size_t trackCount() const;
    [[nodiscard]] bool isValid(const TrackRef& track) const override;
    [[nodiscard]] cydaw::base::Text trackName(const TrackRef& track) const override;
    [[nodiscard]] std::uint32_t trackColour(const TrackRef& track) const;
    void setTrackName(const TrackRef& track, const cydaw::base::Text& name);
    void setTrackColour(const TrackRef& track, std::uint32_t argb);
    ParameterLanes& masterArrangementLanes() noexcept { return masterArrangementLanes_; }
    const ParameterLanes& masterArrangementLanes() const noexcept { return masterArrangementLanes_; }
    [[nodiscard]] int laneHeight(const TrackRef& track) const;
    void setLaneHeight(const TrackRef& track, int height);

    // ---- Volume automation --------------------------------------------------------
    // The same calls for audio and instrument tracks; values in dB
    // (-60..+12), times in seconds, points kept in time order.
    using AutomationPoint = app::AudioTrackController::AutomationPoint;
    using AutomationCurve = app::AudioTrackController::AutomationCurve;
    [[nodiscard]] bool automationVisible(const TrackRef& track) const;
    // Showing the lane makes the track taller, as CyDAW 1 does.
    void setAutomationVisible(const TrackRef& track, bool visible) override;
    [[nodiscard]] std::vector<AutomationPoint> automationPoints(const TrackRef& track) const;
    std::uint64_t addAutomationPoint(const TrackRef& track, double seconds, float valueDb);
    bool updateAutomationPoint(const TrackRef& track, std::uint64_t id, double seconds, float valueDb);
    bool setAutomationPointCurve(const TrackRef& track, std::uint64_t id, AutomationCurve curve, float amount);
    bool removeAutomationPoint(const TrackRef& track, std::uint64_t id);
    // Replaces every point (undo, redo).
    void setAutomationPoints(const TrackRef& track, const std::vector<AutomationPoint>& points);

    // ---- Automation lanes ---------------------------------------------------------
    // A track's lanes by name: "" its volume (dB), a parameter of its graph
    // by binding ("node.4.eqpro.b2Gain") in the parameter's own units, or a
    // plugin's parameter ("plugin:<plugin>:<parameter>", audio tracks) from
    // 0 to 1; the value is in the points' valueDb. The engine reads a lane
    // at each block of the track's chain.
    [[nodiscard]] std::vector<AutomationPoint> lanePoints(const TrackRef& track, const cydaw::base::Text& lane) const;
    std::uint64_t addLanePoint(const TrackRef& track, const cydaw::base::Text& lane, double seconds, float value);
    bool updateLanePoint(const TrackRef& track, const cydaw::base::Text& lane, std::uint64_t id, double seconds, float value);
    bool setLanePointCurve(const TrackRef& track, const cydaw::base::Text& lane, std::uint64_t id, AutomationCurve curve,
                           float amount);
    bool removeLanePoint(const TrackRef& track, const cydaw::base::Text& lane, std::uint64_t id);
    // Replaces every point (undo, redo); none removes the lane.
    void setLanePoints(const TrackRef& track, const cydaw::base::Text& lane, const std::vector<AutomationPoint>& points);
    // Whether a lane has points; its points all removed.
    [[nodiscard]] bool hasLane(const TrackRef& track, const cydaw::base::Text& lane) const override { return !lanePoints(track, lane).empty(); }
    void clearLane(const TrackRef& track, const cydaw::base::Text& lane) override { setLanePoints(track, lane, {}); }
    // The parameters that have points, plugins' included.
    [[nodiscard]] std::vector<cydaw::base::Text> automatedParameters(const TrackRef& track) const override;
    // A plugin parameter's lane name, and back.
    [[nodiscard]] static cydaw::base::Text pluginLaneName(std::size_t plugin, std::uint32_t parameter);
    [[nodiscard]] static bool parsePluginLane(const cydaw::base::Text& lane, std::size_t& plugin, std::uint32_t& parameter);
    // A plugin parameter's name, as the interface last read it or as its
    // lane keeps it.
    void rememberPluginParameterName(const cydaw::base::Text& lane, const cydaw::base::Text& name);
    [[nodiscard]] cydaw::base::Text pluginParameterName(const TrackRef& track, const cydaw::base::Text& lane) const;
    // The lane the arrangement shows under the track.
    [[nodiscard]] cydaw::base::Text shownLane(const TrackRef& track) const;
    void setShownLane(const TrackRef& track, const cydaw::base::Text& lane) override;
    // Where a parameter's lane puts it at the playhead; none without points.
    [[nodiscard]] std::optional<float> laneValueNow(const TrackRef& track, const cydaw::base::Text& lane) const override;
    // Hands a track's lanes to the engine (after an edit, after loading).
    void publishParameterAutomation(const TrackRef& track);

    // ---- Track formats -----------------------------------------------------------------
    // An audio track's channels, 1 (mono) to 8 (7.1), as ChannelLayout.h lays
    // them out; an instrument track and the master are stereo. Its clips play
    // in it, its devices process every channel, and its sound converts into
    // what it is routed to.
    [[nodiscard]] int trackChannels(const TrackRef& track) const;
    cydaw::base::Result setTrackChannels(const TrackRef& track, int channels);
    // "Mono", "Stéréo", "Quad", "5.1"…
    [[nodiscard]] static cydaw::base::Text formatName(int channels);

    // ---- The sound card's buses ---------------------------------------------------------
    // The inputs tracks record from and the outputs the master and the tracks
    // sent there come out of, as the engine keeps them; a change is saved
    // with the project.
    cydaw::base::Result updateHardwareBus(const app::VirtualAudioBus& bus);
    std::uint64_t addHardwareBus(app::VirtualBusDirection direction);
    cydaw::base::Result removeHardwareBus(std::uint64_t id);
    // The device's channels open (the first so many of each).
    cydaw::base::Result setDeviceChannelCounts(int inputs, int outputs);
    // A bus as the interface names it: "Micro", and whether it exists.
    [[nodiscard]] std::optional<app::VirtualAudioBus> hardwareBus(std::uint64_t id) const;

    // ---- The software's virtual inputs and outputs ---------------------------------------
    // They belong to the software, not to a project: every project assigns
    // them (a track records an input, before or after its effects; sends to
    // an output). Kept in VirtualIO.json beside the settings, their effects
    // with them; written a moment after a change.
    [[nodiscard]] static cydaw::base::File virtualIoFile();
    void loadVirtualIo();
    // Automated sessions leave the file alone.
    void setVirtualIoSaving(bool saving) noexcept { virtualIoSaving_ = saving; }
    void flushVirtualIo();
    void virtualIoChanged() noexcept { virtualIoDirty_ = true; }

    // ---- Routing between tracks -------------------------------------------------------
    // Every track has outputs: the master by default, and as many more as
    // wanted, into other tracks (their sound joins the destination's own,
    // before its devices), each at its level. A bus is a track without clips
    // that others are routed into.
    //
    // A track as the routing names it, stable while tracks move; 0 is the
    // master.
    [[nodiscard]] std::uint64_t routingKey(const TrackRef& track) const;
    [[nodiscard]] std::optional<TrackRef> trackForKey(std::uint64_t key) const;
    // Never empty: the master when nothing else is set.
    [[nodiscard]] std::vector<TrackOutput> trackOutputs(const TrackRef& track) const;
    // Fails, changing nothing, on a loop or a track that is not there.
    cydaw::base::Result setTrackOutputs(const TrackRef& track, std::vector<TrackOutput> outputs);
    // The tracks routed into this one.
    [[nodiscard]] std::vector<TrackRef> trackSenders(const TrackRef& track) const;
    // Whether `from` may be routed into `to`: another track, no loop.
    [[nodiscard]] bool canRoute(const TrackRef& from, const TrackRef& to) const;
    [[nodiscard]] bool isBusTrack(const TrackRef& track) const;
    std::optional<TrackRef> addBusTrack(const cydaw::base::Text& name = {});
    cydaw::base::Result addInstrumentReturns(const TrackRef& track);
    cydaw::base::Result renderArrangement(const cydaw::base::File& destination, double from, double to,
                                          bool stems, std::optional<TrackRef> onlyTrack = {});
    cydaw::base::Result collectSessionMedia(const cydaw::base::File& destinationFolder);
    // A project's routing, as read (no checks), then published.
    void restoreTrackRouting(const TrackRef& track, std::vector<TrackOutput> outputs, bool bus);
    // Hands every track's outputs to the engine.
    void publishRouting();
    // Its audio inputs beside its main one, in its graph's order: an input
    // node of the graph each, to wire where it is needed (the sidechain of
    // a compressor).
    static constexpr std::size_t maximumTrackInputs = 3;
    [[nodiscard]] std::vector<TrackInput> trackInputs(const TrackRef& track) const;
    // Its graph's new input node, or nothing (the master, no room left).
    std::optional<std::uint64_t> addTrackInput(const TrackRef& track, const cydaw::base::Text& name = {});
    // The input goes, and what was routed into it.
    cydaw::base::Result removeTrackInput(const TrackRef& track, std::uint64_t node);
    // "Entrée 2", or the name it was given; the main one for 0.
    [[nodiscard]] cydaw::base::Text trackInputName(const TrackRef& track, std::uint64_t node) const;
    // A device's sidechain (a compressor's, a gate's): what its detector
    // listens to instead of the sound it treats. Whether the device has one,
    // the track that feeds it (routed, before its fader, into an input of
    // this track wired to the device's sidechain socket), and choosing it:
    // the input is added and wired the first time, another source replaces
    // the last, none silences it.
    [[nodiscard]] bool hasSidechain(const TrackRef& track, std::uint64_t node) const;
    [[nodiscard]] std::optional<TrackRef> sidechainSource(const TrackRef& track, std::uint64_t node) const;
    cydaw::base::Result setSidechainSource(const TrackRef& track, std::uint64_t node, const std::optional<TrackRef>& source);

    // ---- The master track ------------------------------------------------------------
    // The tracks mixed go through its devices, then its fader: a graph of
    // its own, a limiter to begin with. TrackRef::master names it to the
    // device panel and the graph.
    [[nodiscard]] static TrackRef masterTrack() noexcept
    {
        TrackRef track;
        track.master = true;
        // No engine track's index: isValid() says no.
        track.audioIndex = static_cast<std::size_t>(-1);
        return track;
    }
    // Back to the default: the limiter alone.
    void resetMasterGraph();
    // A project's (empty: the default), published.
    void restoreMasterGraph(const cydaw::base::Value& graphState);

    // ---- The device workshop --------------------------------------------------------
    // One device made of a graph, an effect or an instrument, edited on its
    // own as a plugin is: its graph, its name and description, the knobs it
    // publishes, its VST3 identity. It is saved into My devices, exported as
    // a VST3, and put into tracks.
    //
    // Linked to a track, every change is heard there at once: an effect is a
    // device of the track's chain (a module of its graph, kept in step); an
    // instrument is the track's whole graph, which the workshop then edits
    // directly. Devices inside the device open in place, level by level.
    struct WorkshopState {
        bool open {};
        bool instrument {};
        cydaw::base::Text name;
        cydaw::base::File file;              // in My devices, once saved
        bool unsaved {};
        std::optional<TrackRef> track;
        std::vector<cydaw::base::Text> trail; // the device, then the devices open inside it
    };
    [[nodiscard]] WorkshopState workshop() const;
    // After a change of language (or of the node names' language): every
    // graph's cards take their names again.
    void refreshTranslations();
    // A device in the workshop that is neither saved nor on a track: closing
    // would lose it. Any tab's.
    [[nodiscard]] bool workshopAtRisk() const noexcept;
    // The graph the workshop shows (the innermost device open), and the
    // device's own.
    [[nodiscard]] GraphSlot* workshopGraph();
    [[nodiscard]] GraphSlot* workshopRoot();
    // An effect's input and output widths: 1 mono, 2 stereo, up to 8.
    void newWorkshopDevice(bool instrument, const cydaw::base::Text& name, int inputChannels = 2, int outputChannels = 2);
    // The device's audio input and output widths: the chain between takes
    // the input's; a mono and a stereo end meet through a converter before
    // the output. Why not, when it cannot.
    cydaw::base::Result setWorkshopChannels(int input, int output);
    // Its widths now; 0 for an end it does not have.
    [[nodiscard]] std::pair<int, int> workshopChannels();
    cydaw::base::Result openWorkshopFile(const cydaw::base::File& file);
    // A device of a track (a card made of a graph, or an instrument track's
    // whole graph when nodeId is 0), opened linked.
    cydaw::base::Result openWorkshopFromTrack(const TrackRef& track, std::uint64_t nodeId);
    // The device put on a track (an effect into its chain, an instrument as
    // its graph) and linked to it.
    cydaw::base::Result linkWorkshop(const TrackRef& track);
    void unlinkWorkshop();
    void closeWorkshop();
    // Into My devices: under its file, or a new file named after it.
    cydaw::base::Result saveWorkshop(cydaw::base::File& written, bool asNewFile);
    // A portable graph snapshot, independent of the local device library.
    // Does not change the working tab's save destination or dirty flag.
    cydaw::base::Result exportWorkshop(const cydaw::base::File& file, cydaw::base::TextList& externalFiles);
    // Its name, author, description: the exported plugin's name follows
    // the device's until the person gives it another.
    void setWorkshopInfo(const app::GraphDocument::EditorGraphInfo& info);
    cydaw::base::Result enterWorkshopDevice(std::uint64_t nodeId);
    void leaveWorkshopDevice();
    void leaveWorkshopDevices();
    // After an edit made outside the editor (the control port): the levels
    // open carried out to the device, and to its track.
    void commitWorkshop(bool structural);
    // A linked track whose device is gone (removed, the project closed)
    // lets the workshop keep its own copy.
    void checkWorkshopLink();

    // The workshop keeps several devices open, one tab each; everything
    // above acts on the one in front. Opening a device opens a tab (or brings
    // its own forward).
    struct WorkshopTab {
        std::uint64_t id {};
        cydaw::base::Text name;
        bool instrument {};
        bool unsaved {};
        bool linked {};
    };
    [[nodiscard]] std::vector<WorkshopTab> workshopTabs() const;
    [[nodiscard]] std::uint64_t activeWorkshopTab() const noexcept;
    void selectWorkshopTab(std::uint64_t id);
    // That tab (the one in front for 0) closed; its neighbour comes forward.
    void closeWorkshopTab(std::uint64_t id = 0);
    [[nodiscard]] bool workshopTabAtRisk(std::uint64_t id) const;

    // ---- The workshop's simulator ---------------------------------------------------
    // The device in front tried on its own, with the transport of the
    // workshop: test signals into an effect, a looped pattern, the on-screen
    // keyboard or a MIDI keyboard into an instrument. The tracks stay
    // stopped; the master's gain applies.
    enum class SimulatorPattern : int { none, held, chords, arpeggio, bass, scale };
    struct SimulatorSettings {
        int source { 2 };          // app::AudioTrackController::SimulationSource
        float levelDb { -12.0f };
        float toneHz { 220.0f };
        cydaw::base::File sample;
        int pattern { static_cast<int>(SimulatorPattern::chords) };
        int rootNote { 60 };
        float velocity { 0.8f };
        float outputDb { 0.0f };
        // The device's other audio inputs: app::AudioTrackController::SimulationSideSource.
        int sideSource { 0 };
    };
    [[nodiscard]] const SimulatorSettings& simulatorSettings() const noexcept { return simulator_; }
    cydaw::base::Result setSimulatorSettings(const SimulatorSettings& settings);
    // On while the workshop shows a device (the keyboard plays, tails ring).
    void setSimulatorActive(bool active);
    [[nodiscard]] bool simulatorActive() const noexcept { return simulatorActive_; }
    void setSimulating(bool playing);
    [[nodiscard]] bool simulating() const noexcept;
    [[nodiscard]] double simulationSeconds() const noexcept;
    void simulatorNote(int noteNumber, bool on);
    // The output's level for a meter (dB, with a fall).
    [[nodiscard]] float simulatorLevelDb();
    // Whether the device in front plays notes (an instrument) rather than
    // hearing a sound.
    [[nodiscard]] bool simulatesInstrument() const noexcept;

    // Cards of an audio chain made one device, in any graph. The new card,
    // 0 and why not.
    std::uint64_t createDeviceFromSelection(GraphSlot& slot, const std::vector<std::uint64_t>& ids,
                                            const cydaw::base::Text& name, cydaw::base::Text& error);

    // ---- Sound matching: a recorded note made a playable instrument ------------
    // The companion (Python) analyses one or more recorded notes (one zone
    // each) into a sound model, saved in Documents/CyDAW/Sound Models; a new
    // instrument track plays it. Runs in the background: soundMatching() says
    // how far it is, tick() applies the result.
    struct SoundMatching {
        bool running {};
        double progress {};
        cydaw::base::Text message;
        cydaw::base::Text error;
        cydaw::base::Text description;    // the last model made
        cydaw::base::File model;            // its file
        std::optional<TrackRef> track; // the instrument that plays it
        cydaw::base::Value zones;             // the companion's report: pitch, partials, how close
    };
    // `options`: more of the companion's arguments ("--kind", "string"...).
    cydaw::base::Result recreateSound(const cydaw::base::Array<cydaw::base::File>& files, const cydaw::base::Text& name = {}, bool refine = false,
                               const cydaw::base::TextList& options = {});
    // A synth patch found for a recorded note, as Synplant's Genopatch: the
    // settings of Opale (subtractive) or Quartz (FM) whose sound comes closest
    // ("auto": both, the closer kept), searched on threads of their own by
    // playing candidates through the synth's own voice (SynthMatch.h). Its
    // progress and its end are soundMatching()'s, as an analysis's; a new
    // instrument track plays the patch found, at the level that matches.
    // `note` < 0: the recording's pitch; `evaluations`: the candidates heard per synth.
    cydaw::base::Result matchSynth(const cydaw::base::File& file, const cydaw::base::Text& synth = "auto", int note = -1, int evaluations = 3'000);
    void cancelSoundMatching();
    [[nodiscard]] SoundMatching soundMatching() const { return soundMatching_; }
    // A model file played: on `track`'s sound-model card, or by a new
    // instrument track made around one. The track that plays it.
    std::optional<TrackRef> loadSoundModel(const cydaw::base::File& file, std::optional<TrackRef> track, cydaw::base::Text& error);
    [[nodiscard]] static cydaw::base::File soundModelFolder();
    // The readable patch of a model: a wavetable instrument (its table the
    // partials' evolution, eight frames over the note), the analysed ADSR and
    // a low-pass, on a new instrument track: every knob a usual one.
    std::optional<TrackRef> buildReadablePatch(const cydaw::base::Value& json, cydaw::base::Text& error);
    // The JSON of `track`'s sound-model card, or void.
    [[nodiscard]] cydaw::base::Value soundModelJsonOf(const TrackRef& track) const;
    [[nodiscard]] Companion& companion() noexcept { return companion_; }

    // ---- The sampler ----------------------------------------------------------------
    // Recorded notes played back as they are: a sampler card holds the zones
    // (SamplerMapFile.h: each file over its keys and velocities). The factory
    // instruments are maps CyDAW ships (pianos, harpsichords, harps,
    // violins), listed in the factory folder's catalogue.json.
    struct FactoryInstrument {
        cydaw::base::Text key;
        cydaw::base::Text name;         // in the interface's language
        cydaw::base::Text category;     // its key ("pianos")
        cydaw::base::Text categoryName; // in the interface's language
        cydaw::base::Text englishName;  // inside a module
        cydaw::base::Text englishCategory;
        cydaw::base::File map;
        cydaw::base::Value settings;        // the sampler card's knobs it was made for
        cydaw::base::Value envelope;        // attack and release, ms
        cydaw::base::Value voicing;         // {mode: legato|mono, glide: ms}: one note at a time
        double filterHz {};        // the voice filter's cutoff, 0: open
        cydaw::base::Text source;       // where its samples come from ("VCSL")
        cydaw::base::Text bank;         // its bank's id
        cydaw::base::Text licence;
        cydaw::base::Text range;        // "A0 – C8"
        // The same notes as a sound model (the companion's analysis), when
        // one was made, and the sound-model card's settings for it.
        cydaw::base::File model;
        cydaw::base::Value modelSettings;
        // Its samples still to download (Factory ships the maps, not the
        // samples), and the banks they come from.
        int missingFiles {};
        std::int64_t missingBytes {};
        cydaw::base::TextList banks;
    };
    [[nodiscard]] std::vector<FactoryInstrument> factoryInstruments() const;
    // What a graph can take (presets, modules, nodes), as a fresh graph lists
    // them: made once, and again when the language changes.
    [[nodiscard]] const std::vector<app::GraphDocument::DeviceCatalogueEntry>& deviceCatalogue();
    // A new instrument track playing it, its knobs as made: its samples on a
    // sampler, or (`model`) its sound model.
    // On a new track, or `onto` an instrument track (its instrument
    // replaced, its effects kept).
    std::optional<TrackRef> addFactoryInstrument(const cydaw::base::Text& key, cydaw::base::Text& error, bool model = false,
                                                 std::optional<TrackRef> onto = std::nullopt);
    // An instrument track's instrument changed for another (a device key of
    // the catalogue, an instrument preset): the effects after it, their
    // automation and modulators stay. Named after it when its name was its
    // instrument's (or the default one).
    cydaw::base::Result replaceInstrument(const TrackRef& track, const cydaw::base::Text& key, const cydaw::base::Text& name = {});

    // ---- A Replacer's instrument (Containers.h) ----------------------------------------------
    // Its graph (an instrument, as a track's) is addressed as a pad of the
    // track the Replacer is on: replacerPadBase and up. Kept on the card.
    static constexpr int replacerPadBase = 1'000;
    [[nodiscard]] bool isReplacerPad(const TrackRef& track) const;
    // Where a replacer's instrument is (made from what its card keeps); none
    // when the card holds no instrument.
    [[nodiscard]] std::optional<TrackRef> replacerInstrument(const TrackRef& owner, std::uint64_t head);
    [[nodiscard]] cydaw::base::Text replacerInstrumentName(const TrackRef& owner, std::uint64_t head);
    // An instrument of the catalogue for a replacer: made, kept on its card,
    // played at each transient (its sound set to Instrument).
    cydaw::base::Result setReplacerInstrument(const TrackRef& owner, std::uint64_t head, const cydaw::base::Text& instrumentKey);
    // The chain shown for a pad (a rack's, a replacer's): its name.
    [[nodiscard]] cydaw::base::Text padName(const TrackRef& track) const;
    // Each replacer's instrument into a graph being published.
    void attachReplacerInstruments(const TrackRef& track, engine::NativeDspGraph& native);

    // ---- Instruments a pad, a layer or a Replacer plays -------------------------
    // CyDAW's instruments (catalogue keys) and the factory's sampled ones
    // ("factory:<key>", by their category), named in English (a module's).
    struct InstrumentChoice {
        cydaw::base::Text key;
        cydaw::base::Text name;
        cydaw::base::Text category; // empty: CyDAW's own
        bool missing {};       // a factory instrument whose samples are not here yet
    };
    [[nodiscard]] std::vector<InstrumentChoice> instrumentChoices();
    // One of them onto a graph: a track's, a pad's, a layer's, a
    // Replacer's (its samples fetched when they are not here). Its name.
    std::optional<cydaw::base::Text> applyInstrument(const TrackRef& target, const cydaw::base::Text& key, cydaw::base::Text& error);

    // ---- Instrument containers (InstrumentContainers.h) ---------------------------------
    // A Drum Machine ("drum-rack" card), an Instrument Layer, an Instrument
    // Selector: the track's instrument card; each pad or layer an instrument
    // and its effects of their own (its graph: the track's TrackRef with
    // `pad` set), the card saying how they hear the track's notes.
    [[nodiscard]] engine::instrument_container::Kind containerKind(const TrackRef& track) const;
    [[nodiscard]] bool isRack(const TrackRef& track) const;
    // The layer an Instrument Selector plays now (0-based; -1: none).
    [[nodiscard]] int selectedLayer(const TrackRef& track) const;
    [[nodiscard]] std::size_t rackPadCount(const TrackRef& track) const;
    [[nodiscard]] const RackPadState* rackPad(const TrackRef& track, std::size_t pad) const;
    [[nodiscard]] std::optional<std::size_t> rackPadOfNote(const TrackRef& track, int note) const;
    // A pad on `note` playing an instrument ("silex-instrument"…, or
    // "factory:<key>"): its index. A layer's `note` is not used.
    std::optional<std::size_t> addRackPad(const TrackRef& track, int note, const cydaw::base::Text& instrumentKey, cydaw::base::Text& error);
    // A layer playing an instrument (Instrument Layer or Selector): its index.
    std::optional<std::size_t> addLayer(const TrackRef& track, const cydaw::base::Text& instrumentKey, cydaw::base::Text& error);
    bool removeRackPad(const TrackRef& track, std::size_t pad);
    bool setRackPadInstrument(const TrackRef& track, std::size_t pad, const cydaw::base::Text& instrumentKey, cydaw::base::Text& error);
    struct RackPadMix {
        int note { 36 };
        cydaw::base::Text name;
        int playAs { -1 };
        float gainDb {};
        float pan {};
        bool muted {};
        bool solo {};
        int choke {};
        int keyLow {};
        int keyHigh { 127 };
        int velocityLow { 1 };
        int velocityHigh { 127 };
    };
    [[nodiscard]] static RackPadMix mixOf(const RackPadState& pad);
    bool setRackPadMix(const TrackRef& track, std::size_t pad, const RackPadMix& mix);
    // A sound onto the pad of `note` (made when there is none): a simple
    // sampler playing it at its own pitch.
    bool loadRackPadSample(const TrackRef& track, int note, const cydaw::base::File& file, cydaw::base::Text& error);
    [[nodiscard]] float rackPadPeak(const TrackRef& track, std::size_t pad) const;
    // The rack a new one starts with: a kick, a snare, a clap, two hats.
    void fillDefaultRack(const TrackRef& track);
    // The layers a new Instrument Layer or Selector starts with.
    void fillDefaultLayers(const TrackRef& track);
    // Its pads as a project keeps them, and back; the container they were
    // made for, by name ("drum", "layer", "selector").
    [[nodiscard]] cydaw::base::Value rackPadsState(const TrackRef& track) const;
    [[nodiscard]] cydaw::base::Text padsKindName(const TrackRef& track) const;
    void restoreRackPads(const TrackRef& track, const cydaw::base::Value& state, const cydaw::base::Text& kind = {});
    // A plug-in that is not here, its place kept on the track: a card that
    // says so (its name, its maker, its format); an instrument's is silent,
    // an effect's lets the sound through. Its card.
    std::uint64_t addMissingPlugin(const TrackRef& track, const cydaw::base::Text& name, const cydaw::base::Text& vendor,
                                   const cydaw::base::Text& format, bool instrument);
    // The track's first synth of the family given these settings (by the
    // model's keys; a choice by its index): how many were set.
    int setSynthSettings(const TrackRef& track, const std::vector<std::pair<std::string, float>>& settings);
    // A new sampler track playing these files: their notes from their names
    // and their pitch, several on a note as velocity layers or round robins.
    std::optional<TrackRef> newSamplerTrack(const cydaw::base::Array<cydaw::base::File>& files, cydaw::base::Text& error);
    // An SFZ or a .cysampler: on `track`'s sampler card, or a new track.
    std::optional<TrackRef> loadSamplerFile(const cydaw::base::File& file, std::optional<TrackRef> track, cydaw::base::Text& error);
    // The first sampler card of a track's graph (top level, a Batterie's too), 0 when none.
    [[nodiscard]] std::uint64_t samplerCardOf(const TrackRef& track) const;
    // Batterie: a kit (drum::kits) loaded on its card (the kit's map, its
    // setting); a new MIDI track playing one.
    bool setBatterieKit(const TrackRef& track, std::uint64_t nodeId, int kit, cydaw::base::Text& error);
    std::optional<TrackRef> addBatterieTrack(int kit, cydaw::base::Text& error);
    // A groove (drum::grooves) as a MIDI clip on a MIDI track: `bars` of it from
    // `startBeats` (below 0: the playhead's bar), the last bar `fill` (-1:
    // none). The clip's id, 0 when it could not be made.
    std::uint64_t addGrooveClip(const TrackRef& track, int groove, int bars, int fill, double startBeats, cydaw::base::Text& error);
    // A map on a card (an edit, files added): published, undoable. The map's
    // "settings" go to the card's knobs when `applySettings`.
    bool setSamplerMap(const TrackRef& track, std::uint64_t nodeId, const cydaw::base::Value& json, bool applySettings,
                       cydaw::base::Text& error);
    // The companion's last other job (info, setup), finished: for its window.
    [[nodiscard]] const std::optional<Companion::Status>& companionJob() const noexcept { return companionJob_; }

    // The user's devices, one file each in Documents/CyDAW/Devices, usable
    // in every project.
    struct LibraryDevice {
        cydaw::base::File file;
        cydaw::base::Text name;
        bool instrument {};
        cydaw::base::Text description;
    };
    [[nodiscard]] static cydaw::base::File deviceLibraryFolder();
    [[nodiscard]] std::vector<LibraryDevice> libraryDevices() const;
    // A library device put on a track: an effect into its chain, an
    // instrument as its graph.
    cydaw::base::Result insertLibraryDevice(const TrackRef& track, const cydaw::base::File& file);
    // To the recycle bin.
    cydaw::base::Result removeLibraryDevice(const cydaw::base::File& file);
    // Whether a parameter of the track's graph can follow a lane: a card of
    // its chain, a knob inside a module there, or a setting of its
    // instrument's voice (Prisme's, Kick's, a sampler's...).
    [[nodiscard]] bool canAutomate(const TrackRef& track, const cydaw::base::Text& binding) const override;
    [[nodiscard]] std::string learnPrefix(const TrackRef& track) const override;
    // Whether its knob turns on a logarithmic scale (a frequency, a time):
    // a controller turns it the same way.
    [[nodiscard]] bool parameterIsLogarithmic(const TrackRef& track, const cydaw::base::Text& binding) const
    {
        return logarithmicParameter(track, binding);
    }
    // The controllers' assignments kept with the project (Controllers.cpp
    // reads and writes them, ProjectIo saves them); the revision moves when
    // a project is opened or made.
    [[nodiscard]] const cydaw::base::Value& controllerAssignments() const noexcept { return controllerAssignments_; }
    void setControllerAssignments(const cydaw::base::Value& assignments, bool fromProject)
    {
        controllerAssignments_ = assignments;
        if (fromProject)
            ++controllerAssignmentsRevision_;
        else
            markDirty();
    }
    [[nodiscard]] std::uint64_t controllerAssignmentsRevision() const noexcept { return controllerAssignmentsRevision_; }
    // The binding as the native graph names it: a module's inner knob
    // ("node.12.node.4.lowpass.cutoff") on the node the compiler made for it
    // ("node.31.lowpass.cutoff"); any other binding as it is.
    [[nodiscard]] cydaw::base::Text nativeBinding(const GraphSlot* slot, const cydaw::base::Text& binding) const;

    struct Mix {
        float gainDb {};
        float pan {};
        bool muted {};
        bool solo {};
        bool armed {};
    };
    [[nodiscard]] Mix trackMix(const TrackRef& track) const;
    void setTrackMix(const TrackRef& track, const Mix& mix);

    // ---- The launcher -----------------------------------------------------------------
    // A clip launched (its slot's), a track's clips stopped. A scene: each
    // track plays its clip in that row; a track with an empty slot there
    // stops, unless that slot's stop was taken off (it goes on playing).
    bool launchSlot(const TrackRef& track, int scene);
    void stopTrackClips(const TrackRef& track);
    void launchScene(int scene);
    void stopAllClips();
    [[nodiscard]] bool slotHasClip(const TrackRef& track, int scene) const;
    // What a MIDI track's newest voice plays now (a Prisme's, a synth's of
    // the family): its modulated settings, LFOs and envelopes. False when
    // none of theirs plays.
    bool voiceLive(const TrackRef& track, engine::VoiceLive& out) const override;
    // The scene a track plays (-1: none), and the one it waits to play.
    [[nodiscard]] int playingScene(const TrackRef& track) const;
    // How many scenes the launcher shows: those that hold a clip or a
    // stop taken off, two more to fill, eight at least.
    [[nodiscard]] int launcherSceneCount() const;

    // ---- The master's launcher clips: automation, one a scene ----
    // The master's level, as a lane of its clips.
    static constexpr const char* masterGainLane = "master.gain";
    [[nodiscard]] const std::map<int, MasterClip>& masterClips() const { return masterClips_; }
    [[nodiscard]] const MasterClip* masterClip(int scene) const;
    // A clip made in an empty slot (or the one there), its name "Master n".
    MasterClip& createMasterClip(int scene);
    void removeMasterClip(int scene);
    // A clip edited (its lanes, length, loop): heard at once if it plays.
    void setMasterClip(int scene, MasterClip clip);
    // A project's clips, the playing one stopped.
    void setMasterClips(std::map<int, MasterClip> clips);
    // From the next launch quantisation on the master's clock (with the
    // transport stopped: when it plays).
    bool launchMasterClip(int scene);
    void stopMasterClip();
    [[nodiscard]] int playingMasterScene() const { return masterPlaying_; }
    // Remembered at the stop, to start again from its beginning.
    [[nodiscard]] int pendingMasterScene() const { return engine_.isPlaying() ? -1 : masterAtStop_; }
    // How far into its length the playing clip is (0..1).
    [[nodiscard]] float masterClipProgress() const;
    // What a master clip can automate: its level, then its devices' knobs.
    struct MasterLaneChoice {
        cydaw::base::Text binding;
        cydaw::base::Text label;
        cydaw::base::Text unit;
        double minimum {};
        double maximum { 1.0 };
        double value {};
        // A frequency or a time: drawn and glided on its logarithmic scale.
        bool logarithmic {};
    };
    [[nodiscard]] std::vector<MasterLaneChoice> masterLaneChoices() const;
    // ---- A clip's own automation (an audio clip's lanes) ----
    // Its volume over the fader (dB), its pan (-1..1), its devices' knobs.
    static constexpr const char* clipVolumeLane = "volume";
    static constexpr const char* clipPanLane = "pan";
    [[nodiscard]] std::vector<MasterLaneChoice> clipLaneChoices(const TrackRef& track) const;
    // Each tick: a clip with lanes that starts, stops or moves republishes
    // its track's automation.
    void followClipLanes();
    // Where a clip's lane stands at a place of it (in beats from its start):
    // the first point's value before it, the last one's after it.
    [[nodiscard]] static float masterLaneValueAt(const std::vector<MasterClip::Point>& points, double beats, bool logarithmic);
    [[nodiscard]] int pendingScene(const TrackRef& track) const;
    // An empty slot's stop: on by default.
    [[nodiscard]] bool slotStops(const TrackRef& track, int scene) const;
    void setSlotStops(const TrackRef& track, int scene, bool stops);

    // An empty audio track, or one playing a file from a time.
    std::optional<TrackRef> addAudioTrack(const cydaw::base::File& file = {}, double startSeconds = 0.0);
    // A MIDI track with an instrument from the catalogue ("synth-keys"…), or
    // the empty instrument graph when the key is empty.
    std::optional<TrackRef> addMidiTrack(const cydaw::base::Text& name = {}, const cydaw::base::Text& instrumentKey = {});
    bool removeTrack(const TrackRef& track);
    cydaw::base::Result bounceMidiTrack(const TrackRef& track, bool preFader, double tailSeconds);
    cydaw::base::Result freezeMidiTrack(const TrackRef& track, double tailSeconds);
    [[nodiscard]] bool bouncingMidi() const noexcept { return pendingMidiBounce_.has_value(); }
    [[nodiscard]] std::optional<TrackRef> midiSourceOfBounce(const TrackRef& track) const;
    [[nodiscard]] std::optional<TrackRef> returnToMidi(const TrackRef& track);
    void restoreBounceLink(std::uint64_t rendered, std::uint64_t source, bool wasMuted, bool frozen = false);
    [[nodiscard]] bool bounceSourceWasMuted(const TrackRef& rendered) const;
    [[nodiscard]] bool isFrozenBounce(const TrackRef& rendered) const;
    bool moveTrack(const TrackRef& track, int delta);
    // Counts what makes a TrackRef kept aside stale (a track removed, the
    // session cleared): the undo histories holding refs drop theirs.
    [[nodiscard]] std::uint64_t trackRefsSerial() const noexcept { return trackRefsSerial_; }

    // ---- Graphs -----------------------------------------------------------------
    [[nodiscard]] GraphSlot* graphFor(const TrackRef& track) override;
    [[nodiscard]] const GraphSlot* graphFor(const TrackRef& track) const override;
    [[nodiscard]] GraphSlot* busGraph(std::uint64_t busId);
    // A whole device from the catalogue: a preset replaces the graph, a
    // module or node is spliced before the output.
    cydaw::base::Result insertDevice(const TrackRef& track, const cydaw::base::Text& key) override;
    // A device (a container too) into a branch of a container, before the
    // card `before` (0: at the branch's end).
    cydaw::base::Result insertDeviceInBranch(const TrackRef& track, const cydaw::base::Text& key, std::uint64_t head, int branch,
                                      std::uint64_t before = 0);
    // A device, or a library device, put on the chain before the card
    // `before` (0: where it goes by itself). One that cannot go there (a
    // sound before an instrument) stays where it went.
    cydaw::base::Result insertDeviceBefore(const TrackRef& track, const cydaw::base::Text& key, std::uint64_t before);
    // The whole instrument a synth's own card is the heart of ("opale-synth"
    // → "opale-instrument"): what an instrument track gets for that card.
    // Empty for any other card.
    [[nodiscard]] static cydaw::base::Text instrumentOfCard(const cydaw::base::Text& key);
    cydaw::base::Result insertLibraryDeviceBefore(const TrackRef& track, const cydaw::base::File& file, std::uint64_t before);
    // Recompiles and publishes a track's graph to the engine.
    cydaw::base::Result publishGraph(const TrackRef& track, bool structural);
    // The graph the live values are fed to (the one on screen).
    void setWatchedGraph(std::optional<TrackRef> track) { watched_ = track; }

    // ---- Modulation (audio tracks) ------------------------------------------------
    // A value graph (an LFO by default) moves the parameters the track's
    // graph publishes; each parameter has an assignment, off until enabled.
    // The list follows the graph's published parameters.
    [[nodiscard]] std::vector<modulation::Assignment> modulationTargets(const TrackRef& track);
    void setModulationAssignment(const TrackRef& track, const cydaw::base::Text& binding, bool enabled, double amount);
    [[nodiscard]] modulation::LfoSettings modulationLfo(const TrackRef& track) const;
    void setModulationLfo(const TrackRef& track, const modulation::LfoSettings& settings);
    // The modulation's last value (-1..1) and whether one is running.
    [[nodiscard]] double modulationOutput(const TrackRef& track) const;
    [[nodiscard]] bool modulationActive(const TrackRef& track) const;
    // The value graph as cards to edit (LFOs, macros, arithmetic, logic,
    // ranges), made on first use from what the project holds (an LFO into
    // the output by default). Null for anything but an audio track.
    [[nodiscard]] GraphSlot* modulationGraphFor(const TrackRef& track);
    // Its cards changed without telling (the control port's edits): kept,
    // and played from the next tick.
    void modulationGraphEdited(const TrackRef& track);

    // ---- Meters -----------------------------------------------------------------
    [[nodiscard]] const MeterState& masterMeter() const noexcept { return masterMeter_; }
    [[nodiscard]] MeterState trackMeter(const TrackRef& track) const;

    // ---- Transport ----------------------------------------------------------------
    // The loop switch, which the engine does not report. Turning it on
    // without a range loops the four bars from the playhead's bar.
    [[nodiscard]] bool loopEnabled() const noexcept { return loopEnabled_; }
    void setLoopEnabled(bool enabled);
    void setLoopRange(double startSeconds, double endSeconds);
    // Stopping remembers the clips the launcher plays; play starts them
    // again from their start (the launcher shows them waiting).
    void togglePlay();
    void play();
    // Stops where it is; stop() stops, or goes back to the start when stopped.
    void stopPlayback();
    void stop();
    void rewind();

    // ---- Recording ------------------------------------------------------------------
    // A MIDI track records into the clip given (or a new one at the
    // playhead); an audio track records a take from its input bus. The
    // track armed wins over the one given. Playback starts if it was stopped.
    // `withCompanion`: a hybrid track's other part records with it (its
    // notes; its sound when an audio input was chosen for it). A slot's
    // recording takes one kind alone.
    cydaw::base::Result startRecording(std::optional<TrackRef> track, std::uint64_t midiClip, bool withCompanion = true);
    // Keeps or discards the take; the transport stops if recording started it.
    cydaw::base::Result stopRecording(bool keep);
    [[nodiscard]] bool isRecording() const noexcept;
    [[nodiscard]] std::optional<TrackRef> recordingTrack() const noexcept { return recordingTrack_; }

    // ---- Used by the project reader and writer (ProjectIo.cpp) -----------------
    [[nodiscard]] const cydaw::base::Value& factoryAudioGraph() const noexcept { return factoryAudioGraph_; }
    [[nodiscard]] const cydaw::base::Value& factoryMidiGraph() const noexcept { return factoryMidiGraph_; }
    [[nodiscard]] const cydaw::base::Value& factoryBusGraph() const noexcept { return factoryAudioGraph_; }
    [[nodiscard]] const cydaw::base::Value& factoryModulationGraph() const noexcept { return factoryModulationGraph_; }
    [[nodiscard]] cydaw::base::Value& sharedNodeModules() noexcept { return sharedNodeModules_; }
    [[nodiscard]] cydaw::base::Value& sharedModulationModules() noexcept { return sharedModulationModules_; }
    // The library merged into a stored state, ready to restore.
    [[nodiscard]] cydaw::base::Value withLibrary(const cydaw::base::Value& state, const cydaw::base::Value& fallback) const;
    // The same with the modulation graphs' shared modules (the LFO's).
    [[nodiscard]] cydaw::base::Value withModulationLibrary(const cydaw::base::Value& state) const;
    // The state as projects store it: inline classes only.
    [[nodiscard]] static cydaw::base::Value withoutSharedModules(const cydaw::base::Value& state);
    static void mergeModules(cydaw::base::Value& library, const cydaw::base::Value& graphState);

    std::vector<AudioTrackState>& audioStates() noexcept { return audioTracks_; }
    std::map<std::uint64_t, MidiTrackState>& midiStates() noexcept { return midiTracks_; }
    std::map<std::uint64_t, BusState>& busStates() noexcept { return buses_; }
    // Graph slots for an engine track that already exists.
    AudioTrackState& adoptAudioTrack(std::size_t index, const cydaw::base::Value& graphState);
    MidiTrackState& adoptMidiTrack(std::uint64_t id, const cydaw::base::Value& graphState);
    // A pad's mix sent to the engine (silent when its track's instrument is
    // not the container it was made for); every pad's at once.
    void publishRackPadMix(const TrackRef& track, std::size_t pad);
    void publishRackPadMix(const TrackRef& track, std::size_t pad, engine::instrument_container::Kind kind);
    void publishPadMixes(const TrackRef& track);
    // Its pads gone (the engine's silenced).
    void clearPads(const TrackRef& track);
    // A factory instrument's samples onto a graph (a track's, a pad's, a Replacer's).
    bool applyFactoryInstrument(const TrackRef& target, const FactoryInstrument& instrument, cydaw::base::Text& error);
    [[nodiscard]] RackPadState* rackPadState(const TrackRef& track);
    BusState& adoptBus(std::uint64_t id, const cydaw::base::Value& graphState);

    // Settings and layout values a project carries for the interface.
    cydaw::base::Value interfaceState;

private:
    // The tracks' order on screen, by routing key (kept up to date by
    // tracks(): one gone is dropped, one new comes at the end).
    mutable std::vector<std::uint64_t> trackOrder_;
    std::uint64_t trackRefsSerial_ {};
    // The groups: each track's group (routing keys), the groups, the folded.
    std::map<std::uint64_t, std::uint64_t> groupOf_;
    std::set<std::uint64_t> groups_;
    std::set<std::uint64_t> foldedGroups_;
    // The hybrid tracks: each track's companion (routing keys), and the
    // instrument tracks whose audio part records.
    std::map<std::uint64_t, std::uint64_t> companions_;
    std::set<std::uint64_t> hybridAudioRecords_;
    struct PendingMidiBounce {
        std::uint64_t source {};
        cydaw::base::File file;
        double start {};
        bool preFader {};
        bool frozen {};
    };
    struct MidiBounceLink {
        std::uint64_t source {};
        bool wasMuted {};
        bool frozen {};
    };
    std::optional<PendingMidiBounce> pendingMidiBounce_;
    std::map<std::uint64_t, MidiBounceLink> midiBounces_;
    void finishMidiBounce(const cydaw::base::Result& result);
    // A hybrid track's other part recording with it: the companion, its MIDI
    // clip (0: its sound), the audio track armed before it.
    std::optional<TrackRef> companionRecording_;
    std::uint64_t companionRecordClip_ {};
    std::optional<std::size_t> armedBeforeCompanion_;
    void startCompanionRecording(const TrackRef& owner, const TrackRef& companion);
    void stopCompanionRecording(bool keep);
    [[nodiscard]] cydaw::base::File newRecordingFile(const TrackRef& track) const;
    // A track's own clips alone (not its companion's): the scene playing,
    // the one waiting, stopping them.
    [[nodiscard]] int partPlayingScene(const TrackRef& part) const;
    [[nodiscard]] int partPendingScene(const TrackRef& part) const;
    void stopPart(const TrackRef& part);
    // The slot being recorded into, from what beat, its MIDI clip.
    std::optional<std::pair<TrackRef, int>> slotRecording_;
    // A companion armed for a slot's recording, and the audio track armed
    // before it (audio arming is one track at a time): put back after.
    bool slotRecordArmedPart_ {};
    std::optional<std::size_t> armedBeforeSlotRecording_;
    void disarmSlotRecordingPart(const TrackRef& part);
    double slotRecordStartBeats_ {};
    std::uint64_t slotRecordClip_ {};
    std::unique_ptr<GraphSlot> makeSlot(GraphSlot::Owner owner);
    // The one card a device made on the chain (the ids before are `had`),
    // moved before `before`.
    cydaw::base::Result placeNewCard(const TrackRef& track, const std::vector<std::uint64_t>& had, std::uint64_t before);
    void graphChanged(GraphSlot& slot, bool structural);
    [[nodiscard]] std::optional<TrackRef> trackOf(const GraphSlot& slot) const;
    void clearSession();
    void applyTrackModulations();
    std::vector<modulation::Assignment> syncModulationTargets(AudioTrackState& state);
    // A track modulator's targets in a graph about to be published, where
    // the modulator last left them (not back on their knobs for a moment).
    void modulateNativeGraph(AudioTrackState& state, cydaw::engine::NativeDspGraph& graph);
    // A finished audio recording kept: loop passes as takes, a take of the
    // clip it was played over, or a clip of its own.
    cydaw::base::Result keepRecordedAudio(const TrackRef& track);
    static void applyBallistics(MeterState& meter, const float* peak, const float* rms, int channels);

    app::AudioTrackController engine_;
    std::unique_ptr<app::SampleLibrary> samples_;
    cydaw::base::ApplicationProperties properties_;
    MeterState masterMeter_;
    std::vector<MeterState> audioMeters_;
    std::map<std::uint64_t, MeterState> midiMeters_;

    // Whether a parameter of a track's graph glides on a logarithmic scale
    // (a frequency, a time), as its knob does.
    [[nodiscard]] bool logarithmicParameter(const TrackRef& track, const cydaw::base::Text& binding) const;
    cydaw::base::Value controllerAssignments_;
    std::uint64_t controllerAssignmentsRevision_ { 1 };
    std::map<cydaw::base::Text, cydaw::base::Text> pluginParameterNames_;
    struct ModuleEdit {
        cydaw::base::Text key;
        std::unique_ptr<GraphSlot> slot;
    };
    struct Workshop {
        std::uint64_t id {};
        // The device's graph when the workshop holds it; empty while an
        // instrument is linked (the track's graph is the device then).
        std::unique_ptr<GraphSlot> own;
        std::optional<TrackRef> track;
        cydaw::base::Text moduleKey;       // a linked effect: its module in the track's graph
        cydaw::base::File file;
        bool unsaved {};
        bool instrument {};
        std::vector<ModuleEdit> nested;
    };
    // Every open device, and the one in front (null when none is open).
    std::unique_ptr<GraphSlot> masterGraph_;
    std::uint64_t nextTrackUid_ { 1 };
    [[nodiscard]] std::vector<TrackOutput>* outputsOf(const TrackRef& track);
    // Outputs into a track that went: removed (the master takes over).
    void forgetDestination(std::uint64_t key);
    std::vector<std::unique_ptr<Workshop>> workshops_;
    Workshop* workshop_ {};
    std::uint64_t nextWorkshopId_ { 1 };
    void openWorkshopTab(Workshop made);
    [[nodiscard]] GraphSlot* rootOf(Workshop& tab);
    void unlinkTab(Workshop& tab);
    // The input node wired into a device's sidechain socket, and that socket.
    [[nodiscard]] std::optional<std::uint64_t> sidechainInput(const TrackRef& track, std::uint64_t node) const;
    [[nodiscard]] std::optional<std::uint64_t> sidechainPort(const TrackRef& track, std::uint64_t node) const;
    std::unique_ptr<GraphSlot> makeWorkshopSlot();
    void workshopRootChanged(Workshop& tab, bool structural);
    void nestedChanged(Workshop& tab, std::size_t level, GraphSlot& slot, bool structural);
    [[nodiscard]] app::GraphDocument* nestedParent(Workshop& tab, std::size_t level);
    // The simulator.
    SimulatorSettings simulator_;
    // Sound matching.
    Companion companion_;
    mutable SoundBanks banks_;
    // The factory instruments as last listed (factoryInstruments).
    mutable std::vector<FactoryInstrument> factoryCache_;
    mutable int factoryCacheGeneration_ { -1 };
    mutable cydaw::base::Text factoryCacheLanguage_;
    mutable bool factoryCachePending_ {};
    mutable double factoryCacheTime_ {};
    AiModels aiModels_;
    std::unique_ptr<SemanticSearch> semantic_;
    std::vector<app::GraphDocument::DeviceCatalogueEntry> deviceCatalogue_;
    int deviceCatalogueLanguage_ { -1 };
    bool samplesStarted_ {};
    bool samplesAutomatic_ {};
    // Work done off the message thread (the banks' catalogue read), and a
    // token its answer checks the studio is still there with.
    std::shared_ptr<int> alive_ { std::make_shared<int>(0) };
    cydaw::base::ThreadPool background_ { 1 };
    std::unique_ptr<locations::Move> locationMove_;
    std::vector<std::pair<Location, cydaw::base::File>> pendingLocations_;
    bool pendingTake_ { true };
    void nextLocation();
    std::vector<std::pair<cydaw::base::Text, bool>> notices_;
    std::map<cydaw::base::Text, cydaw::base::Text> presetNames_;
    void reloadSamplers(const cydaw::base::TextList& banks);
    SoundMatching soundMatching_;
    // A MIDI clip's notes while a processing is heard on it (put back unless applied).
    struct MidiPreview {
        std::uint64_t trackId {};
        std::uint64_t clipId {};
        std::vector<app::AudioTrackController::MidiNote> notes;
    };
    std::optional<MidiPreview> midiPreview_;
    // The synth patch being searched for (matchSynth): its thread, its end.
    struct SynthSearch;
    std::unique_ptr<SynthSearch> synthSearch_;
    void finishSynthSearch();
    std::optional<Companion::Status> companionJob_;
    cydaw::base::File pendingModel_;
    void applySoundMatching(const Companion::Status& finished);
    bool simulatorActive_ {};
    const GraphSlot* simulatedSlot_ {};
    // Each track's extra input nodes as the routing last took them: a
    // graph changing them routes again.
    std::map<std::uint64_t, std::vector<std::uint64_t>> publishedInputs_;
    std::uint64_t simulatedRevision_ {};
    bool simulatedInstrument_ {};
    float simulatorLevel_ { -120.0f };
    double simulatorLevelTime_ {};
    void publishSimulation(bool force);
    void publishSimulatorPattern();
    [[nodiscard]] cydaw::base::Value deviceDocument(app::GraphDocument& graph, bool instrument) const;
    // A plugin identity of its own for a device that has none yet: named
    // after it, with a plugin code no other device is likely to share.
    static void giveOwnIdentity(app::GraphDocument& graph, bool always);
    static cydaw::base::Result setDeviceChannels(app::GraphDocument& graph, int input, int output);
    [[nodiscard]] static std::pair<std::uint64_t, std::uint64_t> audioEnds(const app::GraphDocument& graph);
    [[nodiscard]] ParameterLanes* lanesOf(const TrackRef& track);
    [[nodiscard]] const ParameterLanes* lanesOf(const TrackRef& track) const;
    std::vector<AudioTrackState> audioTracks_;
    std::optional<AudioClipboard> audioClipboard_;
    bool recordTakesOnClips_ { true };
    std::map<std::uint64_t, MidiTrackState> midiTracks_;
    // The replacers' instruments: whose card, its graph, what it plays.
    struct ReplacerSlot {
        TrackRef owner;
        std::uint64_t head {};
        std::unique_ptr<GraphSlot> graph;
        std::shared_ptr<const engine::PolySynthGraphPlan> plan;
        cydaw::base::Text name;
    };
    std::vector<ReplacerSlot> replacerSlots_;
    ReplacerSlot* replacerSlot(const TrackRef& track);
    ReplacerSlot* replacerSlotFor(const TrackRef& owner, std::uint64_t head, bool make);
    void compileReplacer(ReplacerSlot& slot);
    std::map<std::uint64_t, BusState> buses_;

    cydaw::base::Value factoryAudioGraph_;
    cydaw::base::Value factoryMidiGraph_;
    cydaw::base::Value factoryModulationGraph_;
    cydaw::base::Value sharedNodeModules_;
    cydaw::base::Value sharedModulationModules_;

    cydaw::base::File projectFile_;
    bool auxiliary_ {};
    bool dirty_ {};
    bool virtualIoDirty_ {};
    bool virtualIoSaving_ {};
    double virtualIoChangedAt_ {};
    cydaw::base::TextList loadWarnings_;
    std::optional<TrackRef> watched_;
    bool loading_ {};
    // A project being opened in steps (startOpeningProject).
    struct Opening;
    std::unique_ptr<Opening> opening_;
    // An imported project made into the session in steps (its tracks, then
    // their instruments and graphs).
    struct ImportBuild;
    // One step of it; false once it is done.
    bool importStep(ImportBuild& build);
    cydaw::base::Text importSummary_;
    void beginApplying(const cydaw::base::File& file, const cydaw::base::Value& document);
    // The factory models a project names that could not be read, among its warnings.
    void noteMissingModels(const cydaw::base::TextList& missing);
    cydaw::base::Result finishApplying(const cydaw::base::File& file, const cydaw::base::Result& applied);
    std::unique_ptr<project_io::DocumentLoad> applying_;
    bool loopEnabled_ {};
    void autosaveIfDue();
    bool autosaveEnabled_ { true };
    double lastAutosaveSeconds_ {};
    std::atomic<bool> autosaveWriting_ { false };
    // One recovery file for every CyDAW 2 open: the first to start owns it,
    // the others neither offer, write nor delete it (one closing cleanly
    // would wipe the copy of another that crashes).
    [[nodiscard]] bool ownsRecovery() const;
    mutable std::unique_ptr<cydaw::base::InterProcessLock> recoveryLock_;
    mutable bool ownsRecovery_ {};
    // Recording.
    std::optional<TrackRef> recordingTrack_;
    cydaw::base::File recordingFile_;
    double recordingStart_ {};
    bool recordingStartedPlayback_ {};
    bool recordingLoopWasOn_ {};
    // The clips playing when the transport stopped (track, scene).
    std::vector<std::pair<TrackRef, int>> clipsAtStop_;
    // The master's clips, the one playing (and since when on the master's
    // clock), the one remembered at a stop, the level before it moved it.
    ParameterLanes masterArrangementLanes_;
    bool masterAutomationVisible_ {};
    int masterLaneHeight_ { 88 };
    std::map<int, MasterClip> masterClips_;
    int masterPlaying_ { -1 };
    int masterAtStop_ { -1 };
    double masterStartSeconds_ {};
    double masterPublishedTempo_ {};
    std::optional<float> masterGainBeforeClip_;
    void publishMasterClip();
    void followMasterClip();
    void forgetClipAtStop(const TrackRef& track);
};

// Project documents (ProjectIo.cpp): the same .cydaw files CyDAW 1 reads and
// writes, version 36.
namespace project_io {
[[nodiscard]] cydaw::base::Value buildDocument(Studio& studio, const cydaw::base::File& projectFile, bool refreshPluginState);
// The virtual inputs and outputs with their effects, as VirtualIO.json keeps
// them, and back.
[[nodiscard]] cydaw::base::Value virtualIoDocument(Studio& studio);
void applyVirtualIo(Studio& studio, const cydaw::base::Value& document);
cydaw::base::Result applyDocument(Studio& studio, const cydaw::base::Value& document, const cydaw::base::File& projectFile,
                           cydaw::base::TextList& warnings);
// The same, a step at a time (the project's settings, a track, the master,
// the routing, a track's sound prepared…), so the interface can draw
// between them.
class DocumentLoad final {
public:
    DocumentLoad(Studio& studio, cydaw::base::Value document, cydaw::base::File projectFile, cydaw::base::TextList& warnings);
    ~DocumentLoad();
    // Does the next step; false once there is none left (then result()).
    bool next();
    [[nodiscard]] float progress() const noexcept;
    // What the next step does, for a person to read.
    [[nodiscard]] cydaw::base::Text doing() const;
    [[nodiscard]] cydaw::base::Result result() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace project_io

} // namespace cydaw::v2
