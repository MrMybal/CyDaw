#pragma once

#include "AiModels.h"
#include "AudioTrackController.h"
#include "Companion.h"
#include "Locations.h"
#include "NodeGraphComponent.h"
#include "SampleLibrary.h"
#include "SemanticSearch.h"
#include "SoundBanks.h"
#include "AudioEdit.h"
#include "TrackModulation.h"

#include <juce_data_structures/juce_data_structures.h>

#include <array>
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
// Each track owns its graph model — a NodeGraphComponent that is never
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

// One track as the interface lists it. Audio tracks come first, then MIDI
// tracks, in the engine's own order.
struct TrackRef {
    bool midi {};
    std::size_t audioIndex {};   // audio tracks: the engine index
    std::uint64_t midiId {};     // MIDI tracks: the engine id
    // The master track: its devices and its fader, not one of the engine's
    // tracks (isValid() says no; its graph, name and mix answer).
    bool master {};
    // A virtual input or output of the software (its bus id): its effects
    // edited as a track's devices are. `master` is set too, so whatever is
    // not one of the engine's tracks leaves it alone as it does the master;
    // what is the master's own (its graph, name, mix, meter) asks for this
    // first.
    std::uint64_t bus {};

    [[nodiscard]] static TrackRef ofBus(const std::uint64_t id) noexcept
    {
        TrackRef track;
        track.master = true;
        track.bus = id;
        track.audioIndex = static_cast<std::size_t>(-1);
        return track;
    }
    [[nodiscard]] bool isMaster() const noexcept { return master && bus == 0; }
    [[nodiscard]] bool isBus() const noexcept { return bus != 0; }

    [[nodiscard]] bool operator==(const TrackRef& other) const noexcept
    {
        if (master || other.master)
            return master == other.master && bus == other.bus;
        return midi == other.midi && (midi ? midiId == other.midiId : audioIndex == other.audioIndex);
    }
};

class GraphSlot final {
public:
    // module: the inside of a device made of a graph, open for editing;
    // workshop: a device the workshop holds itself; modulation: a track's
    // value graph (what moves its parameters), never compiled for sound.
    enum class Owner { audioTrack, midiTrack, bus, module, workshop, master, modulation };
    GraphSlot(Owner owner, std::function<void(GraphSlot&, bool structural)> changed);
    ~GraphSlot();

    [[nodiscard]] app::NodeGraphComponent& graph() noexcept { return *graph_; }
    [[nodiscard]] const app::NodeGraphComponent& graph() const noexcept { return *graph_; }
    [[nodiscard]] Owner owner() const noexcept { return owner_; }
    // Bumped by every change, so a view can tell when to refresh caches.
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    // A change made around the editor's notifications (the control port's).
    void touch() noexcept { ++revision_; }
    // Restoring does not notify: the caller publishes once afterwards.
    void restore(const juce::var& stateWithLibrary);
    [[nodiscard]] juce::var serialise() const;
    [[nodiscard]] juce::String lastCompileError() const { return lastError_; }
    void setLastCompileError(juce::String error) { lastError_ = std::move(error); }

private:
    Owner owner_;
    std::unique_ptr<app::NodeGraphComponent> graph_;
    std::uint64_t revision_ { 1 };
    bool restoring_ {};
    juce::String lastError_;
};

// A track's device automation: each lane a published parameter of its
// graph, by binding, and its points in time order, values in the
// parameter's own units (the points' valueDb).
struct ParameterLanes {
    std::map<juce::String, std::vector<app::AudioTrackController::AutomationPoint>> lanes;
    // The lane the arrangement shows under the track: empty for the volume.
    juce::String shown;
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
    [[nodiscard]] bool operator==(const TrackOutput&) const = default;
};

// An audio input of a track beside its main one: other tracks are routed
// into it (a sidechain, a second source). Its graph node, its name.
struct TrackInput {
    std::uint64_t node {};
    juce::String name;
};

struct AudioTrackState {
    juce::String name;
    // The launcher's empty slots whose stop was taken off, a bit per scene.
    std::uint8_t launcherStopOff {};
    // Stable while tracks move: what routing names it by.
    std::uint64_t uid {};
    // A bus: a track without clips that other tracks are routed into.
    bool bus {};
    // Empty: the master alone.
    std::vector<TrackOutput> outputs;
    juce::uint32 colour {};       // 0xAARRGGBB
    int audioChannels { 2 };
    int laneHeight { 88 };
    bool automationVisible {};
    ParameterLanes parameterLanes;
    std::unique_ptr<GraphSlot> graph;
    // The value graph that modulates the track and its assignments, kept as
    // the project stores them.
    juce::var modulationGraph;
    juce::var modulationAssignments;
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
    std::map<juce::String, float> pluginModulationSent;
};

struct MidiTrackState {
    std::uint64_t id {};
    std::uint8_t launcherStopOff {};
    juce::uint32 colour {};
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

class Studio final {
public:
    Studio();
    ~Studio();

    // ---- Audio device -------------------------------------------------------
    // Starts the audio device from the saved setup (or the default one).
    // Returns an error message, empty on success.
    juce::String startAudio(bool restoreSavedDevice);
    void saveAudioSetup();
    [[nodiscard]] juce::String audioStatus() const;

    // ---- Ticking --------------------------------------------------------------
    // Called about 30 times a second: plugin health, MIDI recording, the
    // master export, the meters, the live values of the shown graph.
    void tick();

    [[nodiscard]] app::AudioTrackController& engine() noexcept { return engine_; }
    [[nodiscard]] const app::AudioTrackController& engine() const noexcept { return engine_; }
    // The sample library the browser shows (folders, the banks, cache,
    // analysis), and its optional semantic search with the models it runs.
    [[nodiscard]] app::SampleLibrary& samples() noexcept { return *samples_; }
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
    [[nodiscard]] juce::PropertiesFile& settings();

    // ---- Where things are kept (Options › Emplacements) -----------------------
    // The chosen folders put to use: the banks' samples, the sample index.
    void applyLocations();
    // `location` kept in `folder` from now on ({}: its default). With
    // `takeContent`, what CyDAW put in the old folder goes there first, on a
    // thread, and the change applies when it is done. False with `error`.
    bool setLocation(Location location, const juce::File& folder, bool takeContent, juce::String& error);
    struct LocationMove {
        bool active {};
        Location location {};
        juce::File to;
        locations::Move::Progress progress;
    };
    [[nodiscard]] LocationMove locationMove() const;
    void cancelLocationMove();

    // ---- Presets (Presets.h) ------------------------------------------------------
    // What has settings, as a preset holds it and puts it back:
    // a card of a track's chain (its knobs by key, a sampler's map, a model
    // card's model, a wavetable's table; one undo step), a hosted plug-in
    // (its whole state), a track's whole chain (an instrument and its effects).
    struct PresetHolder {
        juce::String kind;   // "device", "plugin", "chain"
        juce::String target; // the card's type, the plug-in's name, the chain's role
        juce::String key;    // which one it is in the session (its last preset's name)
    };
    [[nodiscard]] std::optional<PresetHolder> devicePresetHolder(const TrackRef& track, std::uint64_t nodeId) const;
    [[nodiscard]] std::optional<PresetHolder> pluginPresetHolder(const TrackRef& track, int pluginIndex) const;
    [[nodiscard]] std::optional<PresetHolder> chainPresetHolder(const TrackRef& track) const;
    [[nodiscard]] juce::var devicePresetState(const TrackRef& track, std::uint64_t nodeId) const;
    bool applyDevicePreset(const TrackRef& track, std::uint64_t nodeId, const juce::var& state, juce::String& error);

    // ---- Audio editing (a clip's own sound) -----------------------------------------
    // Where destructive edits' results are written: the project's folder
    // ("edits"), else Documents/CyDAW/Edits.
    [[nodiscard]] juce::File audioEditFolder() const;
    // A destructive edit of a clip's selection (seconds in its source):
    // rendered to a new file, which the clip then plays; its history keeps
    // every version. One undo step (Ctrl+Z).
    juce::Result editAudioClip(const TrackRef& track, std::uint64_t clipId, const app::AudioEditRequest& request,
                               double sourceStart, double sourceLength, const juce::String& label);
    // A clip's comp made into one file (its best passages joined, as they
    // play): the clip keeps that take alone. One undo step.
    juce::Result flattenAudioClipComp(const TrackRef& track, std::uint64_t clipId);
    // An event of a clip edited in place (normalize, gain, reverse... what
    // keeps its length): a new file, which the event then plays.
    juce::Result editAudioEvent(const TrackRef& track, std::uint64_t clipId, std::uint64_t eventId,
                                const app::AudioEditRequest& request);
    // A stretch of a source copied, for paste.
    struct AudioClipboard {
        juce::File file;
        double start {};
        double length {};
    };
    void copyAudio(const juce::File& file, double start, double length);
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
    bool importPrismTable(const TrackRef& track, std::uint64_t nodeId, int oscillator, const juce::File& file,
                          juce::String& error);
    // Back to the factory table its setting names.
    bool clearPrismTable(const TrackRef& track, std::uint64_t nodeId, int oscillator);

    // ---- A cabinet's impulse response ---------------------------------------------
    // A recorded cabinet (and its microphone) as a sound file, played by a
    // "Baffle et micro" card instead of its models: one or two channels, two
    // seconds at most, its silences before and after trimmed. Kept in the
    // card (project, presets, an exported plug-in). One undo step.
    bool loadCabImpulse(const TrackRef& track, std::uint64_t nodeId, const juce::File& file, juce::String& error);
    bool clearCabImpulse(const TrackRef& track, std::uint64_t nodeId);
    [[nodiscard]] juce::var pluginPresetState(const TrackRef& track, int pluginIndex);
    bool applyPluginPreset(const TrackRef& track, int pluginIndex, const juce::var& state, juce::String& error);
    // A hosted plug-in's own programs (its factory presets) and the current
    // one; choosing one (the plug-in's state then changes, as a preset's).
    bool pluginPrograms(const TrackRef& track, int pluginIndex, juce::StringArray& names, int& current);
    bool selectPluginProgram(const TrackRef& track, int pluginIndex, int program, juce::String& error);
    [[nodiscard]] juce::var chainPresetState(const TrackRef& track) const;
    bool applyChainPreset(const TrackRef& track, const juce::var& state, juce::String& error);
    // The preset last loaded or saved on a holder (by its key), shown on it.
    [[nodiscard]] juce::String presetName(const juce::String& holderKey) const;
    void setPresetName(const juce::String& holderKey, const juce::String& name);

    // ---- Sound banks -----------------------------------------------------------
    // The banks CyDAW knows, what of them is here, their downloads. A
    // download that ends loads again the samplers that play its banks.
    [[nodiscard]] SoundBanks& banks() noexcept { return banks_; }
    // Starts downloading what a factory instrument's samples still need;
    // false (with `error`) when nothing is to be had.
    bool downloadFactoryInstrument(const juce::String& key, juce::String& error);
    // The samples a track's sampler cards play that are not here yet.
    [[nodiscard]] SoundBanks::Missing missingSamples(const TrackRef& track);
    // The sounds a generated bank (CyDAW Electro) has that the samplers play
    // (one track's, or every track's) and that are not here yet: made now,
    // nothing to ask, nothing downloaded. False when none were missing.
    bool generateMissingSounds(std::optional<TrackRef> only = std::nullopt);

    // Messages for the status bar from work done in the background (a
    // download, a move), taken once; `second` true for a failure.
    [[nodiscard]] std::vector<std::pair<juce::String, bool>> takeNotices();

    // ---- Project --------------------------------------------------------------
    void newProject();
    juce::Result loadProject(const juce::File& file);
    // Opening a project without stopping the interface: the file read and
    // decoded on a thread of its own, then the tracks a few at a time
    // between frames. While it goes on, nothing else touches the session.
    struct ProjectOpening {
        bool active {};
        juce::File file;
        bool reading {};     // the file being read and decoded
        float progress {};   // 0..1 (an estimate while reading)
        juce::String doing;  // what is being done, for a person to read
        juce::int64 bytes {};
        double seconds {};
    };
    bool startOpeningProject(const juce::File& file);
    // About budgetMs of the work, once a frame; the outcome once the project
    // is open (or could not be), nothing while it goes on.
    std::optional<juce::Result> continueOpeningProject(double budgetMs);
    [[nodiscard]] ProjectOpening projectOpening() const;
    [[nodiscard]] bool openingProject() const noexcept { return opening_ != nullptr; }
    juce::Result saveProject(const juce::File& file);
    [[nodiscard]] const juce::File& projectFile() const noexcept { return projectFile_; }
    [[nodiscard]] juce::String projectName() const;
    [[nodiscard]] bool isDirty() const noexcept { return dirty_; }
    void markDirty() noexcept { dirty_ = true; }
    // "Don't save": the changes are dropped, with their automatic copy.
    void discardChanges();
    // What the last load could not bring back (missing audio, plugins…).
    [[nodiscard]] const juce::StringArray& loadWarnings() const noexcept { return loadWarnings_; }
    // The ready-to-play projects next to the executable.
    [[nodiscard]] juce::Array<juce::File> templates() const;
    // Another application's project (Ableton, Bitwig, Studio One, DAWproject).
    juce::Result importProject(const juce::File& file, juce::String& summary);

    // ---- Autosave and recovery -------------------------------------------------------
    // While the session has unsaved changes, a copy is written in the
    // background every 45 seconds; a clean save or quit removes it.
    [[nodiscard]] static juce::File recoveryFile();
    // A session left by a crash: when it was written, empty if none.
    [[nodiscard]] juce::String pendingRecoveryDescription() const;
    juce::Result restoreRecovery();
    void discardRecovery();
    // On quit: written when there are unsaved changes, removed otherwise.
    void finishRecovery();
    void setAutosaveEnabled(bool enabled) noexcept { autosaveEnabled_ = enabled; }

    // ---- Tracks -----------------------------------------------------------------
    [[nodiscard]] std::vector<TrackRef> tracks() const;
    [[nodiscard]] std::size_t trackCount() const;
    [[nodiscard]] bool isValid(const TrackRef& track) const;
    [[nodiscard]] juce::String trackName(const TrackRef& track) const;
    [[nodiscard]] juce::uint32 trackColour(const TrackRef& track) const;
    void setTrackName(const TrackRef& track, const juce::String& name);
    void setTrackColour(const TrackRef& track, juce::uint32 argb);
    [[nodiscard]] int laneHeight(const TrackRef& track) const;
    void setLaneHeight(const TrackRef& track, int height);

    // ---- Volume automation --------------------------------------------------------
    // The same calls for audio and instrument tracks; values in dB
    // (-60..+12), times in seconds, points kept in time order.
    using AutomationPoint = app::AudioTrackController::AutomationPoint;
    using AutomationCurve = app::AudioTrackController::AutomationCurve;
    [[nodiscard]] bool automationVisible(const TrackRef& track) const;
    // Showing the lane makes the track taller, as CyDAW 1 does.
    void setAutomationVisible(const TrackRef& track, bool visible);
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
    [[nodiscard]] std::vector<AutomationPoint> lanePoints(const TrackRef& track, const juce::String& lane) const;
    std::uint64_t addLanePoint(const TrackRef& track, const juce::String& lane, double seconds, float value);
    bool updateLanePoint(const TrackRef& track, const juce::String& lane, std::uint64_t id, double seconds, float value);
    bool setLanePointCurve(const TrackRef& track, const juce::String& lane, std::uint64_t id, AutomationCurve curve,
                           float amount);
    bool removeLanePoint(const TrackRef& track, const juce::String& lane, std::uint64_t id);
    // Replaces every point (undo, redo); none removes the lane.
    void setLanePoints(const TrackRef& track, const juce::String& lane, const std::vector<AutomationPoint>& points);
    // The parameters that have points, plugins' included.
    [[nodiscard]] std::vector<juce::String> automatedParameters(const TrackRef& track) const;
    // A plugin parameter's lane name, and back.
    [[nodiscard]] static juce::String pluginLaneName(std::size_t plugin, std::uint32_t parameter);
    [[nodiscard]] static bool parsePluginLane(const juce::String& lane, std::size_t& plugin, std::uint32_t& parameter);
    // A plugin parameter's name, as the interface last read it or as its
    // lane keeps it.
    void rememberPluginParameterName(const juce::String& lane, const juce::String& name);
    [[nodiscard]] juce::String pluginParameterName(const TrackRef& track, const juce::String& lane) const;
    // The lane the arrangement shows under the track.
    [[nodiscard]] juce::String shownLane(const TrackRef& track) const;
    void setShownLane(const TrackRef& track, const juce::String& lane);
    // Where a parameter's lane puts it at the playhead; none without points.
    [[nodiscard]] std::optional<float> laneValueNow(const TrackRef& track, const juce::String& lane) const;
    // Hands a track's lanes to the engine (after an edit, after loading).
    void publishParameterAutomation(const TrackRef& track);

    // ---- Track formats -----------------------------------------------------------------
    // An audio track's channels, 1 (mono) to 8 (7.1), as ChannelLayout.h lays
    // them out; an instrument track and the master are stereo. Its clips play
    // in it, its devices process every channel, and its sound converts into
    // what it is routed to.
    [[nodiscard]] int trackChannels(const TrackRef& track) const;
    juce::Result setTrackChannels(const TrackRef& track, int channels);
    // "Mono", "Stéréo", "Quad", "5.1"…
    [[nodiscard]] static juce::String formatName(int channels);

    // ---- The sound card's buses ---------------------------------------------------------
    // The inputs tracks record from and the outputs the master and the tracks
    // sent there come out of, as the engine keeps them; a change is saved
    // with the project.
    juce::Result updateHardwareBus(const app::VirtualAudioBus& bus);
    std::uint64_t addHardwareBus(app::VirtualBusDirection direction);
    juce::Result removeHardwareBus(std::uint64_t id);
    // The device's channels open (the first so many of each).
    juce::Result setDeviceChannelCounts(int inputs, int outputs);
    // A bus as the interface names it: "Micro", and whether it exists.
    [[nodiscard]] std::optional<app::VirtualAudioBus> hardwareBus(std::uint64_t id) const;

    // ---- The software's virtual inputs and outputs ---------------------------------------
    // They belong to the software, not to a project: every project assigns
    // them (a track records an input, before or after its effects; sends to
    // an output). Kept in VirtualIO.json beside the settings, their effects
    // with them; written a moment after a change.
    [[nodiscard]] static juce::File virtualIoFile();
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
    juce::Result setTrackOutputs(const TrackRef& track, std::vector<TrackOutput> outputs);
    // The tracks routed into this one.
    [[nodiscard]] std::vector<TrackRef> trackSenders(const TrackRef& track) const;
    // Whether `from` may be routed into `to`: another track, no loop.
    [[nodiscard]] bool canRoute(const TrackRef& from, const TrackRef& to) const;
    [[nodiscard]] bool isBusTrack(const TrackRef& track) const;
    std::optional<TrackRef> addBusTrack(const juce::String& name = {});
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
    std::optional<std::uint64_t> addTrackInput(const TrackRef& track, const juce::String& name = {});
    // The input goes, and what was routed into it.
    juce::Result removeTrackInput(const TrackRef& track, std::uint64_t node);
    // "Entrée 2", or the name it was given; the main one for 0.
    [[nodiscard]] juce::String trackInputName(const TrackRef& track, std::uint64_t node) const;
    // A device's sidechain (a compressor's, a gate's): what its detector
    // listens to instead of the sound it treats. Whether the device has one,
    // the track that feeds it (routed, before its fader, into an input of
    // this track wired to the device's sidechain socket), and choosing it:
    // the input is added and wired the first time, another source replaces
    // the last, none silences it.
    [[nodiscard]] bool hasSidechain(const TrackRef& track, std::uint64_t node) const;
    [[nodiscard]] std::optional<TrackRef> sidechainSource(const TrackRef& track, std::uint64_t node) const;
    juce::Result setSidechainSource(const TrackRef& track, std::uint64_t node, const std::optional<TrackRef>& source);

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
    void restoreMasterGraph(const juce::var& graphState);

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
        juce::String name;
        juce::File file;              // in My devices, once saved
        bool unsaved {};
        std::optional<TrackRef> track;
        std::vector<juce::String> trail; // the device, then the devices open inside it
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
    void newWorkshopDevice(bool instrument, const juce::String& name, int inputChannels = 2, int outputChannels = 2);
    // The device's audio input and output widths: the chain between takes
    // the input's; a mono and a stereo end meet through a converter before
    // the output. Why not, when it cannot.
    juce::Result setWorkshopChannels(int input, int output);
    // Its widths now; 0 for an end it does not have.
    [[nodiscard]] std::pair<int, int> workshopChannels();
    juce::Result openWorkshopFile(const juce::File& file);
    // A device of a track (a card made of a graph, or an instrument track's
    // whole graph when nodeId is 0), opened linked.
    juce::Result openWorkshopFromTrack(const TrackRef& track, std::uint64_t nodeId);
    // The device put on a track (an effect into its chain, an instrument as
    // its graph) and linked to it.
    juce::Result linkWorkshop(const TrackRef& track);
    void unlinkWorkshop();
    void closeWorkshop();
    // Into My devices: under its file, or a new file named after it.
    juce::Result saveWorkshop(juce::File& written, bool asNewFile);
    // Its name, author, description: the exported plugin's name follows
    // the device's until the person gives it another.
    void setWorkshopInfo(const app::NodeGraphComponent::EditorGraphInfo& info);
    juce::Result enterWorkshopDevice(std::uint64_t nodeId);
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
        juce::String name;
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
        juce::File sample;
        int pattern { static_cast<int>(SimulatorPattern::chords) };
        int rootNote { 60 };
        float velocity { 0.8f };
        float outputDb { 0.0f };
        // The device's other audio inputs: app::AudioTrackController::SimulationSideSource.
        int sideSource { 0 };
    };
    [[nodiscard]] const SimulatorSettings& simulatorSettings() const noexcept { return simulator_; }
    juce::Result setSimulatorSettings(const SimulatorSettings& settings);
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
                                            const juce::String& name, juce::String& error);

    // ---- Sound matching: a recorded note made a playable instrument ------------
    // The companion (Python) analyses one or more recorded notes (one zone
    // each) into a sound model, saved in Documents/CyDAW/Sound Models; a new
    // instrument track plays it. Runs in the background: soundMatching() says
    // how far it is, tick() applies the result.
    struct SoundMatching {
        bool running {};
        double progress {};
        juce::String message;
        juce::String error;
        juce::String description;    // the last model made
        juce::File model;            // its file
        std::optional<TrackRef> track; // the instrument that plays it
        juce::var zones;             // the companion's report: pitch, partials, how close
    };
    juce::Result recreateSound(const juce::Array<juce::File>& files, const juce::String& name = {}, bool refine = false);
    void cancelSoundMatching();
    [[nodiscard]] SoundMatching soundMatching() const { return soundMatching_; }
    // A model file played: on `track`'s sound-model card, or by a new
    // instrument track made around one. The track that plays it.
    std::optional<TrackRef> loadSoundModel(const juce::File& file, std::optional<TrackRef> track, juce::String& error);
    [[nodiscard]] static juce::File soundModelFolder();
    // The readable patch of a model: a wavetable instrument (its table the
    // partials' evolution, eight frames over the note), the analysed ADSR and
    // a low-pass, on a new instrument track: every knob a usual one.
    std::optional<TrackRef> buildReadablePatch(const juce::var& json, juce::String& error);
    // The JSON of `track`'s sound-model card, or void.
    [[nodiscard]] juce::var soundModelJsonOf(const TrackRef& track) const;
    [[nodiscard]] Companion& companion() noexcept { return companion_; }

    // ---- The sampler ----------------------------------------------------------------
    // Recorded notes played back as they are: a sampler card holds the zones
    // (SamplerMapFile.h: each file over its keys and velocities). The factory
    // instruments are maps CyDAW ships (pianos, harpsichords, harps,
    // violins), listed in the factory folder's catalogue.json.
    struct FactoryInstrument {
        juce::String key;
        juce::String name;         // in the interface's language
        juce::String category;     // its key ("pianos")
        juce::String categoryName; // in the interface's language
        juce::File map;
        juce::var settings;        // the sampler card's knobs it was made for
        juce::var envelope;        // attack and release, ms
        juce::var voicing;         // {mode: legato|mono, glide: ms}: one note at a time
        double filterHz {};        // the voice filter's cutoff, 0: open
        juce::String source;       // where its samples come from ("VCSL")
        juce::String bank;         // its bank's id
        juce::String licence;
        juce::String range;        // "A0 – C8"
        // The same notes as a sound model (the companion's analysis), when
        // one was made, and the sound-model card's settings for it.
        juce::File model;
        juce::var modelSettings;
        // Its samples still to download (Factory ships the maps, not the
        // samples), and the banks they come from.
        int missingFiles {};
        juce::int64 missingBytes {};
        juce::StringArray banks;
    };
    [[nodiscard]] std::vector<FactoryInstrument> factoryInstruments() const;
    // What a graph can take (presets, modules, nodes), as a fresh graph lists
    // them: made once, and again when the language changes.
    [[nodiscard]] const std::vector<app::NodeGraphComponent::DeviceCatalogueEntry>& deviceCatalogue();
    // A new instrument track playing it, its knobs as made: its samples on a
    // sampler, or (`model`) its sound model.
    std::optional<TrackRef> addFactoryInstrument(const juce::String& key, juce::String& error, bool model = false);
    // A new sampler track playing these files: their notes from their names
    // and their pitch, several on a note as velocity layers or round robins.
    std::optional<TrackRef> newSamplerTrack(const juce::Array<juce::File>& files, juce::String& error);
    // An SFZ or a .cysampler: on `track`'s sampler card, or a new track.
    std::optional<TrackRef> loadSamplerFile(const juce::File& file, std::optional<TrackRef> track, juce::String& error);
    // The first sampler card of a track's graph (top level, a Batterie's too), 0 when none.
    [[nodiscard]] std::uint64_t samplerCardOf(const TrackRef& track) const;
    // Batterie: a kit (drum::kits) loaded on its card (the kit's map, its
    // setting); a new MIDI track playing one.
    bool setBatterieKit(const TrackRef& track, std::uint64_t nodeId, int kit, juce::String& error);
    std::optional<TrackRef> addBatterieTrack(int kit, juce::String& error);
    // A groove (drum::grooves) as a MIDI clip on a MIDI track: `bars` of it from
    // `startBeats` (below 0: the playhead's bar), the last bar `fill` (-1:
    // none). The clip's id, 0 when it could not be made.
    std::uint64_t addGrooveClip(const TrackRef& track, int groove, int bars, int fill, double startBeats, juce::String& error);
    // A map on a card (an edit, files added): published, undoable. The map's
    // "settings" go to the card's knobs when `applySettings`.
    bool setSamplerMap(const TrackRef& track, std::uint64_t nodeId, const juce::var& json, bool applySettings,
                       juce::String& error);
    // The companion's last other job (info, setup), finished: for its window.
    [[nodiscard]] const std::optional<Companion::Status>& companionJob() const noexcept { return companionJob_; }

    // The user's devices, one file each in Documents/CyDAW/Devices, usable
    // in every project.
    struct LibraryDevice {
        juce::File file;
        juce::String name;
        bool instrument {};
        juce::String description;
    };
    [[nodiscard]] static juce::File deviceLibraryFolder();
    [[nodiscard]] std::vector<LibraryDevice> libraryDevices() const;
    // A library device put on a track: an effect into its chain, an
    // instrument as its graph.
    juce::Result insertLibraryDevice(const TrackRef& track, const juce::File& file);
    // To the recycle bin.
    juce::Result removeLibraryDevice(const juce::File& file);
    // Whether a parameter of the track's graph can follow a lane: a card of
    // its chain, a knob inside a module there, or a setting of its
    // instrument's voice (Prisme's, Kick's, a sampler's...).
    [[nodiscard]] bool canAutomate(const TrackRef& track, const juce::String& binding) const;
    // The binding as the native graph names it: a module's inner knob
    // ("node.12.node.4.lowpass.cutoff") on the node the compiler made for it
    // ("node.31.lowpass.cutoff"); any other binding as it is.
    [[nodiscard]] juce::String nativeBinding(const GraphSlot* slot, const juce::String& binding) const;

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
    // The scene a track plays (-1: none), and the one it waits to play.
    [[nodiscard]] int playingScene(const TrackRef& track) const;
    [[nodiscard]] int pendingScene(const TrackRef& track) const;
    // An empty slot's stop: on by default.
    [[nodiscard]] bool slotStops(const TrackRef& track, int scene) const;
    void setSlotStops(const TrackRef& track, int scene, bool stops);

    // An empty audio track, or one playing a file from a time.
    std::optional<TrackRef> addAudioTrack(const juce::File& file = {}, double startSeconds = 0.0);
    // A MIDI track with an instrument from the catalogue ("synth-keys"…), or
    // the empty instrument graph when the key is empty.
    std::optional<TrackRef> addMidiTrack(const juce::String& name = {}, const juce::String& instrumentKey = {});
    bool removeTrack(const TrackRef& track);
    bool moveTrack(const TrackRef& track, int delta);

    // ---- Graphs -----------------------------------------------------------------
    [[nodiscard]] GraphSlot* graphFor(const TrackRef& track);
    [[nodiscard]] const GraphSlot* graphFor(const TrackRef& track) const;
    [[nodiscard]] GraphSlot* busGraph(std::uint64_t busId);
    // A whole device from the catalogue: a preset replaces the graph, a
    // module or node is spliced before the output.
    juce::Result insertDevice(const TrackRef& track, const juce::String& key);
    // Recompiles and publishes a track's graph to the engine.
    juce::Result publishGraph(const TrackRef& track, bool structural);
    // The graph the live values are fed to (the one on screen).
    void setWatchedGraph(std::optional<TrackRef> track) { watched_ = track; }

    // ---- Modulation (audio tracks) ------------------------------------------------
    // A value graph (an LFO by default) moves the parameters the track's
    // graph publishes; each parameter has an assignment, off until enabled.
    // The list follows the graph's published parameters.
    [[nodiscard]] std::vector<modulation::Assignment> modulationTargets(const TrackRef& track);
    void setModulationAssignment(const TrackRef& track, const juce::String& binding, bool enabled, double amount);
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
    void togglePlay();
    void stop();
    void rewind();

    // ---- Recording ------------------------------------------------------------------
    // A MIDI track records into the clip given (or a new one at the
    // playhead); an audio track records a take from its input bus. The
    // track armed wins over the one given. Playback starts if it was stopped.
    juce::Result startRecording(std::optional<TrackRef> track, std::uint64_t midiClip);
    // Keeps or discards the take; the transport stops if recording started it.
    juce::Result stopRecording(bool keep);
    [[nodiscard]] bool isRecording() const noexcept;
    [[nodiscard]] std::optional<TrackRef> recordingTrack() const noexcept { return recordingTrack_; }

    // ---- Used by the project reader and writer (ProjectIo.cpp) -----------------
    [[nodiscard]] const juce::var& factoryAudioGraph() const noexcept { return factoryAudioGraph_; }
    [[nodiscard]] const juce::var& factoryMidiGraph() const noexcept { return factoryMidiGraph_; }
    [[nodiscard]] const juce::var& factoryBusGraph() const noexcept { return factoryAudioGraph_; }
    [[nodiscard]] const juce::var& factoryModulationGraph() const noexcept { return factoryModulationGraph_; }
    [[nodiscard]] juce::var& sharedNodeModules() noexcept { return sharedNodeModules_; }
    [[nodiscard]] juce::var& sharedModulationModules() noexcept { return sharedModulationModules_; }
    // The library merged into a stored state, ready to restore.
    [[nodiscard]] juce::var withLibrary(const juce::var& state, const juce::var& fallback) const;
    // The same with the modulation graphs' shared modules (the LFO's).
    [[nodiscard]] juce::var withModulationLibrary(const juce::var& state) const;
    // The state as projects store it: inline classes only.
    [[nodiscard]] static juce::var withoutSharedModules(const juce::var& state);
    static void mergeModules(juce::var& library, const juce::var& graphState);

    std::vector<AudioTrackState>& audioStates() noexcept { return audioTracks_; }
    std::map<std::uint64_t, MidiTrackState>& midiStates() noexcept { return midiTracks_; }
    std::map<std::uint64_t, BusState>& busStates() noexcept { return buses_; }
    // Graph slots for an engine track that already exists.
    AudioTrackState& adoptAudioTrack(std::size_t index, const juce::var& graphState);
    MidiTrackState& adoptMidiTrack(std::uint64_t id, const juce::var& graphState);
    BusState& adoptBus(std::uint64_t id, const juce::var& graphState);

    // Settings and layout values a project carries for the interface.
    juce::var interfaceState;

private:
    std::unique_ptr<GraphSlot> makeSlot(GraphSlot::Owner owner);
    void graphChanged(GraphSlot& slot, bool structural);
    [[nodiscard]] std::optional<TrackRef> trackOf(const GraphSlot& slot) const;
    void clearSession();
    void applyTrackModulations();
    std::vector<modulation::Assignment> syncModulationTargets(AudioTrackState& state);
    // A finished audio recording kept: loop passes as takes, a take of the
    // clip it was played over, or a clip of its own.
    juce::Result keepRecordedAudio(const TrackRef& track);
    static void applyBallistics(MeterState& meter, const float* peak, const float* rms, int channels);

    app::AudioTrackController engine_;
    std::unique_ptr<app::SampleLibrary> samples_;
    juce::ApplicationProperties properties_;
    MeterState masterMeter_;
    std::vector<MeterState> audioMeters_;
    std::map<std::uint64_t, MeterState> midiMeters_;

    // Whether a parameter of a track's graph glides on a logarithmic scale
    // (a frequency, a time), as its knob does.
    [[nodiscard]] bool logarithmicParameter(const TrackRef& track, const juce::String& binding) const;
    std::map<juce::String, juce::String> pluginParameterNames_;
    struct ModuleEdit {
        juce::String key;
        std::unique_ptr<GraphSlot> slot;
    };
    struct Workshop {
        std::uint64_t id {};
        // The device's graph when the workshop holds it; empty while an
        // instrument is linked (the track's graph is the device then).
        std::unique_ptr<GraphSlot> own;
        std::optional<TrackRef> track;
        juce::String moduleKey;       // a linked effect: its module in the track's graph
        juce::File file;
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
    [[nodiscard]] app::NodeGraphComponent* nestedParent(Workshop& tab, std::size_t level);
    // The simulator.
    SimulatorSettings simulator_;
    // Sound matching.
    Companion companion_;
    mutable SoundBanks banks_;
    AiModels aiModels_;
    std::unique_ptr<SemanticSearch> semantic_;
    std::vector<app::NodeGraphComponent::DeviceCatalogueEntry> deviceCatalogue_;
    int deviceCatalogueLanguage_ { -1 };
    bool samplesStarted_ {};
    bool samplesAutomatic_ {};
    // Work done off the message thread (the banks' catalogue read), and a
    // token its answer checks the studio is still there with.
    std::shared_ptr<int> alive_ { std::make_shared<int>(0) };
    juce::ThreadPool background_ { 1 };
    std::unique_ptr<locations::Move> locationMove_;
    std::vector<std::pair<juce::String, bool>> notices_;
    std::map<juce::String, juce::String> presetNames_;
    void reloadSamplers(const juce::StringArray& banks);
    SoundMatching soundMatching_;
    std::optional<Companion::Status> companionJob_;
    juce::File pendingModel_;
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
    [[nodiscard]] juce::var deviceDocument(app::NodeGraphComponent& graph, bool instrument) const;
    // A plugin identity of its own for a device that has none yet: named
    // after it, with a plugin code no other device is likely to share.
    static void giveOwnIdentity(app::NodeGraphComponent& graph, bool always);
    static juce::Result setDeviceChannels(app::NodeGraphComponent& graph, int input, int output);
    [[nodiscard]] static std::pair<std::uint64_t, std::uint64_t> audioEnds(const app::NodeGraphComponent& graph);
    [[nodiscard]] ParameterLanes* lanesOf(const TrackRef& track);
    [[nodiscard]] const ParameterLanes* lanesOf(const TrackRef& track) const;
    std::vector<AudioTrackState> audioTracks_;
    std::optional<AudioClipboard> audioClipboard_;
    bool recordTakesOnClips_ { true };
    std::map<std::uint64_t, MidiTrackState> midiTracks_;
    std::map<std::uint64_t, BusState> buses_;

    juce::var factoryAudioGraph_;
    juce::var factoryMidiGraph_;
    juce::var factoryModulationGraph_;
    juce::var sharedNodeModules_;
    juce::var sharedModulationModules_;

    juce::File projectFile_;
    bool dirty_ {};
    bool virtualIoDirty_ {};
    bool virtualIoSaving_ {};
    double virtualIoChangedAt_ {};
    juce::StringArray loadWarnings_;
    std::optional<TrackRef> watched_;
    bool loading_ {};
    // A project being opened in steps (startOpeningProject).
    struct Opening;
    std::unique_ptr<Opening> opening_;
    void beginApplying(const juce::File& file, const juce::var& document);
    juce::Result finishApplying(const juce::File& file, const juce::Result& applied);
    std::unique_ptr<project_io::DocumentLoad> applying_;
    bool loopEnabled_ {};
    void autosaveIfDue();
    bool autosaveEnabled_ { true };
    double lastAutosaveSeconds_ {};
    std::atomic<bool> autosaveWriting_ { false };
    // Recording.
    std::optional<TrackRef> recordingTrack_;
    juce::File recordingFile_;
    double recordingStart_ {};
    bool recordingStartedPlayback_ {};
    bool recordingLoopWasOn_ {};
};

// Project documents (ProjectIo.cpp): the same .cydaw files CyDAW 1 reads and
// writes, version 36.
namespace project_io {
[[nodiscard]] juce::var buildDocument(Studio& studio, const juce::File& projectFile, bool refreshPluginState);
// The virtual inputs and outputs with their effects, as VirtualIO.json keeps
// them, and back.
[[nodiscard]] juce::var virtualIoDocument(Studio& studio);
void applyVirtualIo(Studio& studio, const juce::var& document);
juce::Result applyDocument(Studio& studio, const juce::var& document, const juce::File& projectFile,
                           juce::StringArray& warnings);
// The same, a step at a time (the project's settings, a track, the master,
// the routing, a track's sound prepared…), so the interface can draw
// between them.
class DocumentLoad final {
public:
    DocumentLoad(Studio& studio, juce::var document, juce::File projectFile, juce::StringArray& warnings);
    ~DocumentLoad();
    // Does the next step; false once there is none left (then result()).
    bool next();
    [[nodiscard]] float progress() const noexcept;
    // What the next step does, for a person to read.
    [[nodiscard]] juce::String doing() const;
    [[nodiscard]] juce::Result result() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace project_io

} // namespace cydaw::v2
