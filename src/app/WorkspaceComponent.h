#pragma once

#include "AutomationEditorComponent.h"
#include "AudioEditorComponent.h"
#include "ClipTimelineEditorComponent.h"
#include "AudioTrackController.h"
#include "ClipLauncherComponent.h"
#include "CyDawLookAndFeel.h"
#include "DeviceChainComponent.h"
#include "LibraryBrowserComponent.h"
#include "KnobSlider.h"
#include "Localization.h"
#include "MixerComponent.h"
#include "SampleExplorerComponent.h"
#include "SampleLibrary.h"
#include "NodeGraphComponent.h"
#include "PianoRollComponent.h"
#include "ArrangementPlayheadComponent.h"
#include "TrackLaneComponent.h"
#include "TimelineRulerComponent.h"
#include "VirtualBusManagerComponent.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <atomic>
#include <array>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace cydaw::app {

// The two shapes a project document repeats thousands of times, built with
// identifiers made once: setProperty("time", ...) looked "time" up in the
// string pool, under its lock, for every point of every lane.
[[nodiscard]] juce::var automationPointsToVar(
    const std::vector<AudioTrackController::AutomationPoint>& points);
[[nodiscard]] juce::var midiNotesToVar(
    const std::vector<AudioTrackController::MidiNote>& notes);

// The parts of a document that cost the most to build and depend on nothing
// live once copied. The autosave hands them to its writer thread as plain
// vectors and fills the document there; the objects filled are the
// document's own, made for it, so nothing shared with the workspace is
// touched off the message thread.
struct DeferredDocumentWork {
    struct Automation {
        juce::DynamicObject::Ptr owner;
        juce::Identifier property;
        std::vector<AudioTrackController::AutomationPoint> points;
    };
    struct Notes {
        juce::DynamicObject::Ptr owner;
        juce::Identifier property;
        std::vector<AudioTrackController::MidiNote> notes;
    };
    std::vector<Automation> automation;
    std::vector<Notes> notes;
    void apply() const;
};

class WorkspaceComponent final : public juce::Component,
                                 private juce::Timer,
                                 private juce::ChangeListener,
                                 private juce::KeyListener,
                                 private juce::ScrollBar::Listener,
                                 public juce::FileDragAndDropTarget {
public:
    explicit WorkspaceComponent(bool recoveryEnabled = true);
    ~WorkspaceComponent() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    void showNodeGraphWindow();
    void openProjectFile(const juce::File& file);
    // What the play button does; for --play on a snapshot run.
    void startPlayback();
    // The master output at zero, for a probe or a capture that plays.
    void silenceOutput();
    // Frames one card of the editor's graph; for --frame-node= on a snapshot
    // run, so a knob is close enough to read.
    void frameGraphNode(const juce::String& nodeTypeKey);
    // Samples the message thread every few milliseconds for a while and
    // reports the gaps: every hitch the user would feel, the worst of them
    // with the moment and what is known to have run then. The autosave is
    // fired inside the window, being the one periodic job. For --ui-hitch=.
    void startUiHitchProbe(double seconds,
                           std::function<void(juce::String)> onReport);
    // One user action, by name, for the probe to time: "zoom-in",
    // "zoom-out", "pan", "pan-back", "fit". Noted as action=<name>.
    void performProbeAction(const juce::String& action);
    [[nodiscard]] bool runDockingSelfTest();
    [[nodiscard]] bool toolSelectionIsReachable();
    [[nodiscard]] bool exportIsReachable();
    [[nodiscard]] bool runProjectGraphSelfTest();
    [[nodiscard]] bool runMasterExportSelfTest();
    [[nodiscard]] bool runProjectRecoverySelfTest();
    [[nodiscard]] bool runAudioRecordingSelfTest();
    // Writes a project holding the wavetable instrument and the precision EQ
    // on one MIDI track, so the pair can be played; see --demo-project=.
    [[nodiscard]] juce::String buildInstrumentFilterDemo(
        const juce::File& file);
    // Writes the bench's session — eight audio tracks with automation, a
    // MIDI track with four hundred notes behind the demo instrument, a
    // hundred and sixty cards on the first track — so the hitch probe can
    // open it; see --perf-project=.
    [[nodiscard]] juce::String buildPerformanceProject(
        const juce::File& file);
    // Writes one of the small projects that ship ready to play — "basse-et-
    // nappes", "arpege", "ambiance", "rythmique" — through the same calls
    // the application makes; see --template=name:path.
    [[nodiscard]] juce::String buildTemplateProject(const juce::String& name,
                                                    const juce::File& file);
    // Builds a realistic session and times the interactive surfaces. Returns a
    // one-line report; see --perf-self-test.
    [[nodiscard]] juce::String runPerformanceSelfTest();
    // Every button in the workspace, and whether a click at its centre would
    // actually reach it; see --button-audit.
    [[nodiscard]] juce::String auditInteractiveControls();
    // Where the last track selection spent its time, for the bench.
    // What the graph editor's layout callback costs, part by part.
    [[nodiscard]] juce::String layoutHandlerProfile() const
    {
        return layoutHandlerProfile_;
    }
    [[nodiscard]] juce::String selectTrackProfile() const
    {
        return selectTrackProfile_ + " || restoreTrack[" + restoreTrackProfile_
               + "] || modulation[" + restoreModulationProfile_ + "]";
    }
    [[nodiscard]] bool saveVisualSnapshot(const juce::File& destination);
    // One request from the control port — {"cmd": "status"} and the rest
    // of tools/mcp — answered as {ok, result} or {ok: false, error}.
    // Runs on the message thread. See WorkspaceComponentRemote.cpp.
    [[nodiscard]] juce::var handleRemoteCommand(const juce::var& request);
    void showEditorMode(int mode);
    void showBrowserCategory(int category);

private:
    juce::String layoutHandlerProfile_;
    bool nodeDeviceSurfaceRefreshPending_ {};
    juce::String selectTrackProfile_;
    // Where the autosave document's time goes, by section.
    juce::String projectDocumentProfile_;
    // The class libraries' deep clones, kept while the library is the same
    // object; see sharedNodeModulesCopy.
    mutable juce::var sharedNodeModulesClone_;
    mutable const void* sharedNodeModulesCloneOf_ {};
    mutable juce::var sharedModulationModulesClone_;
    mutable const void* sharedModulationModulesCloneOf_ {};
    class UiHitchProbe;
    class UiHitchPaintBracket;
    std::unique_ptr<UiHitchProbe> uiHitchProbe_;
    void noteUiHitchEvent(const juce::String& label);
    bool uiHitchLastPlaying_ {};
    juce::String restoreTrackProfile_;
    juce::String restoreModulationProfile_;

    class StaticPanel final : public juce::Component {
    public:
        StaticPanel(juce::String title, juce::String body, juce::Colour accent);
        void setTitle(juce::String title);
        void setBody(juce::String body);
        void paint(juce::Graphics& graphics) override;

    private:
        juce::String title_;
        juce::String body_;
        juce::Colour accent_;
    };
    class ArrangementCanvas final : public juce::Component {
    public:
        void paint(juce::Graphics& graphics) override;
    };
    class LiveLayoutResizerBar final
        : public juce::StretchableLayoutResizerBar {
    public:
        using juce::StretchableLayoutResizerBar::StretchableLayoutResizerBar;
        void setDragHandler(std::function<void()> handler);
        void mouseDrag(const juce::MouseEvent& event) override;

    private:
        std::function<void()> dragHandler_;
    };
    class MasterLane final : public juce::Component {
    public:
        void setLevels(const std::array<float, 2>& peak,
                       const std::array<float, 2>& rms);
        void refreshTranslations();
        void paint(juce::Graphics& graphics) override;

    private:
        juce::String title_ { "MASTER" };
        std::array<float, 2> peak_ {};
        std::array<float, 2> rms_ {};
    };
    class FloatingGraphWindow;
    class FloatingPanelWindow;
    struct ArrangementClipboardTrack {
        std::size_t relativeTrackIndex {};
        double firstStartSeconds {};
        std::vector<AudioTrackController::AudioClip> clips;
    };
    struct ModulationAssignment {
        juce::String binding;
        juce::String label;
        double baseValue {};
        double minimum {};
        double maximum { 1.0 };
        double amount { 0.25 };
        bool enabled {};
    };
    struct TrackModulationState {
        juce::var graph;
        std::vector<ModulationAssignment> assignments;
    };
    struct TrackPresentation {
        juce::String name;
        juce::Colour colour;
        int audioChannels { 2 };
    };
    struct LauncherAction {
        enum class Type { launchClip, stopTrack, launchScene };
        double timeSeconds {};
        Type type { Type::launchClip };
        std::size_t trackIndex {};
        int sceneIndex { -1 };
    };
    struct LauncherEditorContext {
        std::size_t trackIndex {};
        int sceneIndex { -1 };
    };
    enum class InspectorSelection {
        none,
        audioTrack,
        audioClip,
        launcherAudioClip,
        audioEvent,
        midiTrack,
        midiClip
    };
    enum class DockPanel { inspector, browser, editor, bottom };
    struct LayoutState {
        juce::String name;
        int inspectorWidth { 230 };
        int browserWidth { 280 };
        int bottomHeight { 230 };
        int editorMode { 1 };
        int bottomMode { 1 };
        bool inspectorVisible { true };
        bool browserVisible { true };
        bool bottomVisible { true };
        bool graphFloating {};
        bool inspectorFloating {};
        bool browserFloating {};
        bool editorFloating {};
        bool bottomFloating {};
        juce::Rectangle<int> inspectorWindowBounds { 120, 80, 320, 680 };
        juce::Rectangle<int> browserWindowBounds { 180, 100, 900, 720 };
        juce::Rectangle<int> editorWindowBounds { 80, 60, 1120, 720 };
        juce::Rectangle<int> bottomWindowBounds { 140, 120, 980, 380 };
        juce::Rectangle<int> graphWindowBounds { 160, 140, 980, 360 };
    };

    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void refreshTranslations();
    void chooseAudioFile();
    void loadAudioFile(const juce::File& file);
    void chooseMidiImportFile();
    void importMidiFile(const juce::File& file);
    void chooseMidiExportFile();
    void exportSelectedMidiClip(const juce::File& file);
    void chooseRecordingFile();
    void chooseAudioTakeFile();
    void recordSelectedAudioTake();
    void removeSelectedAudioTake();
    void refreshAudioTakeControls();
    void toggleRecording(const juce::File& destination = {});
    void finishAudioRecording(bool keepTake);
    void toggleLauncherRecording(std::size_t trackIndex, int sceneIndex);
    void finishLauncherRecording(bool keepTake);
    void insertLauncherSample(std::size_t trackIndex, int sceneIndex,
                              const juce::File& file);
    void refreshAudioTrackArmStates();
    void toggleMidiRecording();
    void undoMidiEdit();
    void redoMidiEdit();
    void chooseMasterExportFile();
    void startMasterExport(const juce::File& destination);
    void cancelMasterExport();
    void showAudioSettings();
    void saveAudioDevicePreferences();
    void chooseProjectSaveFile();
    void chooseProjectLoadFile();
    // Another application's project, read into this one: see
    // ProjectImport.h. The report says what came over and what did not.
    [[nodiscard]] juce::Result importExternalProject(const juce::File& file, juce::var& report);
    void chooseProjectImportFile();
    // Everything cleared, the tempo back to 120, and the one MIDI track
    // the application starts with when asked.
    void startEmptyProject(bool withInitialMidiTrack);
    void saveProject(const juce::File& file);
    void loadProject(const juce::File& file);
    // With `deferred`, the point and note arrays are left for the caller's
    // thread to fill; without it the document comes back complete.
    [[nodiscard]] juce::var createProjectDocument(
        const juce::File& pathBase,
        bool refreshPluginState,
        bool absoluteAssetPaths,
        DeferredDocumentWork* deferred = nullptr);
    void scheduleProjectAutosave();
    void finishProjectAutosave(const juce::Result& result);
    void offerProjectRecovery();
    [[nodiscard]] juce::Result writeProjectRecoveryNow();
    void discardProjectRecovery();
    void addTrackLane(std::size_t trackIndex);
    void addEmptyAudioTrack();
    void addEmptyMidiTrack();
    void showAddTrackMenu(juce::Component* target);
    [[nodiscard]] std::optional<std::uint64_t>
        launcherMidiTrackId(std::size_t launcherTrackIndex) const;
    [[nodiscard]] std::optional<AudioTrackController::MidiClip>
        launcherMidiClip(std::uint64_t trackId, int sceneIndex) const;
    void selectLauncherMidiTrack(std::uint64_t trackId);
    void clearTrackLanes();
    void rebuildTrackLanes();
    void moveArrangementTrack(std::size_t fromIndex,
                              std::size_t toIndex);
    void moveLauncherTrack(std::size_t fromIndex,
                           std::size_t toIndex);
    void showTrackContextMenu(std::size_t trackIndex,
                              juce::Component* target);
    void promptRenameTrack(std::size_t trackIndex);
    void layoutTrackLanes();
    void refreshTimelineRanges();
    // The same, only when the project's length changed: a note moved
    // inside its clip leaves every lane as it was.
    void refreshTimelineRangesIfLengthChanged();
    void refreshArrangementView();
    void zoomArrangement(double factor, double anchorSeconds);
    void panArrangement(double deltaSeconds);
    void fitArrangement();
    void scrollBarMoved(juce::ScrollBar* scrollBar,
                        double newRangeStart) override;
    void refreshMidiEditor();
    void refreshAutomationTargets();
    void refreshAutomationEditor();
    void refreshBottomAutomationEditor();
    [[nodiscard]] bool usesClipLocalAutomation() const noexcept;
    [[nodiscard]] std::uint64_t addContextAutomationPoint(
        double timeSeconds, float value);
    [[nodiscard]] bool updateContextAutomationPoint(
        std::uint64_t id, double timeSeconds, float value);
    [[nodiscard]] bool removeContextAutomationPoint(std::uint64_t id);
    [[nodiscard]] bool setContextAutomationPointCurve(
        std::uint64_t id, AudioTrackController::AutomationCurve curve,
        float amount);
    void refreshAudioEditor();
    void refreshClipTimelineEditor();
    void exportAudioSelection(std::size_t trackIndex,
                              const juce::File& source,
                              double sourceStartSeconds,
                              double sourceEndSeconds,
                              const juce::File& destination);
    void renderAudioSelection(AudioEditRequest request,
                              double sourceStartSeconds,
                              double sourceEndSeconds);
    void undoAudioEdit();
    void redoAudioEdit();
    void undoArrangementEdit();
    void redoArrangementEdit();
    void refreshTrackAfterAudioEdit(std::size_t trackIndex);
    void refreshClipLauncher();
    void refreshMixer();
    // A channel index counts the audio tracks first, then the MIDI tracks:
    // the order the launcher and the mixer show them in. Both views route
    // their edits through these, so they cannot drift apart.
    void applyChannelMix(std::size_t channelIndex, float gainDb, float pan);
    void applyChannelMute(std::size_t channelIndex, bool muted);
    void applyChannelSolo(std::size_t channelIndex, bool solo);
    void applyChannelArm(std::size_t channelIndex, bool armed);
    void applyMasterGain(float gainDb);
    void showTrackInputBusMenu(std::size_t trackIndex);
    // The device names under a channel: from the editor when the graph is
    // the one it shows, from the stored state otherwise.
    [[nodiscard]] juce::StringArray deviceNamesForTrack(std::size_t trackIndex,
                                                        int limit) const;
    [[nodiscard]] juce::StringArray deviceNamesForMidiTrack(
        std::uint64_t trackId, int limit) const;
    [[nodiscard]] juce::var overviewGraphStateForTrack(
        std::size_t trackIndex) const;
    [[nodiscard]] juce::var overviewGraphStateForMidiTrack(
        std::uint64_t trackId) const;
    [[nodiscard]] static juce::StringArray deviceNamesInGraph(
        const juce::var& graphState, int limit);
    void recordLauncherAction(LauncherAction action);
    void replayLauncherActions(double position);
    void setEditorMode(int mode);
    void setBottomPanelMode(int mode);
    void ensureTrackModulationState(std::size_t trackIndex);
    void saveCurrentModulationState();
    void restoreModulationStateForTrack(std::size_t trackIndex);
    void refreshModulationTargets();
    void refreshModulationAssignmentControls();
    void applyTrackModulations();
    void selectTrack(std::size_t trackIndex);
    void selectAudioClip(
        std::size_t trackIndex,
        std::uint64_t clipId,
        WaveformComponent::RegionSelectionMode mode =
            WaveformComponent::RegionSelectionMode::replace);
    void handleArrangementMarquee(
        std::size_t anchorTrack, double anchorSeconds,
        double currentSeconds, int screenY,
        WaveformComponent::MarqueePhase phase, bool additive);
    [[nodiscard]] bool moveArrangementSelection(
        std::size_t sourceTrack, std::uint64_t primaryClipId,
        double primaryStartSeconds, int screenY);
    void showArrangementMovePreview(
        std::size_t sourceTrack, std::uint64_t clipId,
        double startSeconds, int screenY, bool active);
    [[nodiscard]] std::size_t arrangementTrackAtScreenY(
        int screenY) const noexcept;
    void clearArrangementMarqueeSelection();
    void showArrangementRangeMenu();
    [[nodiscard]] bool editArrangementMarqueeRange(
        AudioTrackController::ArrangementRangeOperation operation);
    [[nodiscard]] bool deleteArrangementMarqueeSelection();
    [[nodiscard]] bool copyArrangementMarqueeSelection();
    [[nodiscard]] bool cutArrangementMarqueeSelection();
    [[nodiscard]] bool pasteArrangementMarqueeClipboard(
        std::size_t targetTrack, double timelineSeconds);
    [[nodiscard]] std::size_t arrangementMarqueeClipCount() const noexcept;
    void normaliseAudioClipSelection(
        const std::vector<AudioTrackController::AudioClip>& clips);
    void refreshArrangementGrid();
    void refreshArrangementInteractionMode();
    [[nodiscard]] int arrangementGridDivision() const noexcept;
    [[nodiscard]] bool arrangementSnappingEnabled() const noexcept;
    [[nodiscard]] double snapArrangementTime(double seconds) const noexcept;
    void setLoopRangeToAudioSelection();
    void resetLoopRangeToProject();
    void copyAudioClipSelection(std::size_t trackIndex,
                                const std::vector<std::uint64_t>& clipIds);
    [[nodiscard]] bool pasteAudioClipSelection(std::size_t trackIndex,
                                               double timelineSeconds);
    void handleAudioClipCommand(
        std::size_t trackIndex,
        std::uint64_t clipId,
        WaveformComponent::RegionCommand command,
        double timelineSeconds);
    void refreshSelectedTrack();
    // The inspector's gain, pan, mute and solo, and the chain's gain and
    // pan, read from the selected MIDI track: they follow it in every view.
    void refreshMidiTrackMixerControls();
    // Removes an audio track with everything the workspace keeps per index,
    // the way moveArrangementTrack moves them; for the control port.
    void removeArrangementTrack(std::size_t trackIndex);
    void detectSelectedClipTempo();
    void syncSelectedClipToTempo();
    void applySelectedClipStretchSettings();
    [[nodiscard]] std::optional<AudioTrackController::AudioEvent>
        selectedAudioEvent() const;
    void applySelectedAudioEventProperties();
    void refreshActiveClipEditorPreview();
    void beginContinuousArrangementEdit();
    void endContinuousArrangementEdit();
    void refreshDevicePluginChain();
    void refreshNodeDeviceSurface();
    // The same refresh, coalesced onto the next message-loop turn. Rebuilding
    // the device strip inside a graph edit cost ten of the twenty
    // milliseconds a node placement took, and nothing reads it in between.
    void scheduleNodeDeviceSurfaceRefresh();
    // True when a title was rewritten and the stored state replaced.
    bool applyTrackIoToGraphState(std::size_t trackIndex);
    // Whose graph the editor is showing. The state save writes the editor's
    // content into the slot of the *selection*; when the two disagree — a
    // project load changes the selection before the editor — writing would
    // put a stale graph over a real one, so the save checks this first.
    enum class EditorGraphOwner { none, track, midiTrack, virtualBus };
    EditorGraphOwner editorGraphOwner_ { EditorGraphOwner::none };
    std::uint64_t editorGraphOwnerId_ {};
    void setEditorGraphOwner(EditorGraphOwner owner, std::uint64_t id);
    // The stored state the editor mirrors — the object it was restored
    // from, or the one its content was just saved into — so a restore of
    // the same graph is skipped while nothing has been stored over it.
    const void* editorSyncedState_ {};
    [[nodiscard]] bool editorHoldsStoredGraph(EditorGraphOwner owner,
                                              std::uint64_t id,
                                              const juce::var& stored) const;
    [[nodiscard]] bool editorShowsSelection() const;
    void ensureTrackNodeGraphState(std::size_t trackIndex);
    void setTrackNodeGraphState(std::size_t trackIndex, const juce::var& state);
    void ensureMidiNodeGraphState(std::uint64_t trackId);
    void setMidiTrackNodeGraphState(std::uint64_t trackId, const juce::var& state);
    void restoreNodeGraphForMidiTrack(std::uint64_t trackId);
    [[nodiscard]] bool isMidiNodeGraphContext() const noexcept;
    void ensureVirtualBusNodeGraphState(std::uint64_t busId);
    void setVirtualBusNodeGraphState(std::uint64_t busId,
                                     const juce::var& state);
    void restoreNodeGraphForVirtualBus(std::uint64_t busId);
    void openVirtualBusGraph(std::uint64_t busId);
    [[nodiscard]] bool isVirtualBusNodeGraphContext() const noexcept;
    void saveCurrentNodeGraphState();
    void restoreNodeGraphForTrack(std::size_t trackIndex);
    void updateSharedNodeModules(const juce::var& graphState);
    void updateSharedModulationModules(const juce::var& graphState);
    // The merge and the injection, with the library to use handed in. The two
    // graphs keep separate libraries and share one description of what a
    // library is for.
    static void mergeSharedModules(juce::var& library, const juce::var& state);
    [[nodiscard]] static juce::var stateWithLibrary(const juce::var& library,
                                                    const juce::var& state,
                                                    const juce::var& fallback);
    // The merge above, for a state the editor will only read.
    [[nodiscard]] juce::var graphStateForEditor(const juce::var& state) const;
    [[nodiscard]] juce::var modulationStateForEditor(
        const juce::var& state) const;

    [[nodiscard]] juce::var graphStateWithSharedModules(const juce::var& state) const;
    // The same merge against a library copy the caller already made. A
    // document holds a dozen graphs and they were each deep-copying the same
    // library, which is where the autosave's pause came from.
    [[nodiscard]] juce::var graphStateWithSharedModules(
        const juce::var& state, const juce::var& libraryCopy) const;
    [[nodiscard]] juce::var sharedNodeModulesCopy() const;
    // The document's side of the same merge, for the value graphs that drive
    // modulation. Without it the file carried classes the editor ignored.
    [[nodiscard]] juce::var modulationStateWithSharedModules(
        const juce::var& state) const;
    [[nodiscard]] juce::var modulationStateWithSharedModules(
        const juce::var& state, const juce::var& libraryCopy) const;
    [[nodiscard]] juce::var sharedModulationModulesCopy() const;
    // What opening a project does with a track's stored modulation graph:
    // harvest the classes it carries, then keep it without them. Written as
    // one function so the loader and the suite cannot drift apart.
    void adoptTrackModulationGraph(std::size_t trackIndex,
                                   const juce::var& savedGraph);
    // The merge itself: a copy the document owns, with the library and the
    // fallback handed in. Both graphs use it.
    [[nodiscard]] static juce::var stateWithLibraryCopy(
        const juce::var& libraryCopy, const juce::var& state,
        const juce::var& fallback);
    // A state as it is worth keeping in memory: without the class bodies both
    // readers merge back in and ignore. Applied only after the library has
    // been harvested from the state, so nothing is lost.
    [[nodiscard]] static juce::var stateWithoutSharedModules(
        const juce::var& state);
    void publishNativeDspGraph();
    void publishNativeDspParameters();
    void onGraphParametersChanged();
    void scheduleModulationTargetsRefresh();
    bool controlValuesShown_ {};
    bool modulationTargetsRefreshPending_ {};
    // Where one card-knob notch goes, and where its publish goes inside that.
    juce::String graphNotchProfile_;
    juce::String publishProfile_;
    juce::String saveGraphStateProfile_;
    void applyMixerSettings();
    void applyClipSettings(bool snapStart = false);
    void configureLayout();
    void updateLayoutConstraints();
    void saveLayoutPreferences();
    [[nodiscard]] LayoutState captureLayoutState(juce::String name = {});
    [[nodiscard]] LayoutState factoryLayout(int presetId) const;
    void applyLayoutState(const LayoutState& state, bool recordHistory = true);
    void undoLayoutChange();
    void redoLayoutChange();
    void refreshLayoutHistoryButtons();
    [[nodiscard]] juce::var serialiseLayoutState(const LayoutState& state) const;
    [[nodiscard]] std::optional<LayoutState> parseLayoutState(const juce::var& value) const;
    void loadNamedLayouts();
    void saveNamedLayouts();
    void refreshLayoutSelector(int selectedId = 0);
    void showLayoutPanelsMenu();
    void promptSaveCurrentLayout();
    void deleteSelectedLayout();
    void togglePanelWindow(DockPanel panel);
    void setPanelFloating(DockPanel panel, bool shouldFloat);
    [[nodiscard]] bool isPanelFloating(DockPanel panel) const noexcept;
    void dockAllPanelWindows();
    void refreshFloatingPanelTitles();
    void showCommandPalette();
    void showFileMenu();
    void showEditMenu();
    void showViewMenu();
    void registerShortcutListener(juce::Component& component);
    bool keyPressed(const juce::KeyPress& key,
                    juce::Component* originatingComponent) override;
    void toggleGraphWindow();
    void dockGraphWindow();
    void showError(const juce::String& title, const juce::String& message);
    void refreshInspectorControls();
    void updateTrackDescription();
    void markProjectDirty();

    [[nodiscard]] static juce::String formatTime(double seconds);
    [[nodiscard]] static bool isSupportedAudioFile(const juce::File& file);
    [[nodiscard]] static bool isSupportedMidiFile(const juce::File& file);

    CyDawLookAndFeel lookAndFeel_;
    AudioTrackController audio_;
    VirtualBusManagerComponent virtualBusManager_ { audio_ };
    juce::MidiKeyboardComponent midiKeyboard_ { audio_.keyboardState(),
                                                juce::MidiKeyboardComponent::horizontalKeyboard };
    juce::AudioThumbnailCache thumbnailCache_ { 8 };
    PianoRollComponent pianoRoll_;
    AutomationEditorComponent automationEditor_;
    AutomationEditorComponent contextAutomationEditor_;
    AutomationEditorComponent bottomAutomationEditor_;
    AudioEditorComponent audioEditor_ { audio_.formatManager(), thumbnailCache_ };
    ClipTimelineEditorComponent clipTimelineEditor_ {
        audio_.formatManager(), thumbnailCache_ };
    ClipLauncherComponent clipLauncher_;
    MixerComponent mixer_;
    SampleLibrary sampleLibrary_;
    SampleExplorerComponent sampleExplorer_ { sampleLibrary_ };
    TimelineRulerComponent timelineRuler_;
    juce::Viewport trackViewport_;
    juce::ScrollBar timelineScrollBar_ { false };
    juce::TextButton timelineZoomOutButton_;
    juce::TextButton timelineFitButton_;
    juce::TextButton timelineZoomInButton_;
    juce::TextButton arrangementRangeButton_;
    juce::Component timelineActionBar_;
    juce::TextButton timelinePointerButton_;
    juce::TextButton timelineKnifeButton_;
    juce::TextButton timelineStretchButton_;
    juce::TextButton timelineZoomButton_;
    juce::TextButton timelineHandButton_;
    juce::ComboBox timelineSnapSelector_;
    juce::ComboBox timelineSnapTypeSelector_;
    juce::TextButton timelinePlayButton_;
    juce::TextButton timelineStopButton_;
    juce::TextButton addArrangementTrackButton_;
    juce::ToggleButton arrangementPlaybackButton_;
    juce::ToggleButton launcherActionRecordButton_;
    juce::ToggleButton launcherActionReplayButton_;
    juce::Label timelineModeLabel_;
    ArrangementCanvas trackContainer_;
    juce::Component inspectorDockSlot_;
    juce::Component inspectorPanelContainer_;
    juce::TextButton inspectorHideButton_;
    juce::TextButton inspectorShowButton_;
    juce::Component browserDockSlot_;
    juce::TextButton browserHideButton_;
    juce::TextButton browserShowButton_;
    juce::Component topWorkspaceArea_;
    juce::Component editorArea_;
    juce::Component editorDockSlot_;
    juce::Component centerArea_;
    juce::Component nodeAreaContainer_;
    juce::Component bottomDockSlot_;
    juce::TextButton bottomHideButton_;
    juce::TextButton bottomShowButton_;
    juce::StretchableLayoutManager horizontalLayout_;
    juce::StretchableLayoutManager verticalLayout_;
    juce::StretchableLayoutManager contextualEditorLayout_;
    juce::StretchableLayoutResizerBar browserResizer_ { &horizontalLayout_, 3, true };
    juce::StretchableLayoutResizerBar inspectorResizer_ { &horizontalLayout_, 1, true };
    LiveLayoutResizerBar nodeResizer_ { &verticalLayout_, 1, false };
    LiveLayoutResizerBar contextAutomationResizer_ {
        &contextualEditorLayout_, 1, false
    };
    juce::Component contextualPrimaryLayoutProxy_;
    juce::TextButton contextAutomationToggleButton_;
    juce::Label emptyTimelineLabel_;
    std::vector<std::unique_ptr<TrackLaneComponent>> trackLanes_;
    // The one playhead over the lanes; see ArrangementPlayheadComponent.
    ArrangementPlayheadComponent arrangementPlayhead_;
    void updateArrangementPlayhead(double position);
    std::vector<int> trackLaneHeights_;
    std::vector<std::uint8_t> trackAutomationVisible_;
    std::vector<TrackPresentation> trackPresentations_;
    std::vector<LauncherAction> launcherActions_;
    MasterLane masterLane_;

    juce::TextButton openAudioButton_;
    juce::TextButton fileMenuButton_;
    juce::TextButton editMenuButton_;
    juce::TextButton viewMenuButton_;
    juce::TextButton saveProjectButton_;
    juce::TextButton loadProjectButton_;
    juce::TextButton masterExportButton_;
    juce::TextButton audioSettingsButton_;
    juce::TextButton detachGraphButton_;
    juce::ComboBox layoutSelector_;
    juce::TextButton layoutPanelsButton_;
    juce::TextButton saveLayoutButton_;
    juce::TextButton deleteLayoutButton_;
    juce::TextButton undoLayoutButton_;
    juce::TextButton redoLayoutButton_;
    juce::TextButton commandSearchButton_;
    juce::TextButton launcherViewButton_;
    juce::TextButton arrangementViewButton_;
    juce::TextButton clipViewButton_;
    juce::TextButton graphEditViewButton_;
    juce::TextButton busViewButton_;
    juce::TextButton mixerViewButton_;
    juce::TextButton samplesViewButton_;
    // Toolbar furniture measured in resized() and drawn in paint(): the rules
    // between command groups, and the recessed wells the transport and the
    // clock sit in.
    std::vector<int> toolbarSeparators_;
    juce::Rectangle<int> transportWell_;
    juce::Rectangle<int> clockField_;
    juce::TextButton devicesPanelButton_;
    juce::TextButton graphPanelButton_;
    juce::TextButton modulationPanelButton_;
    juce::TextButton automationPanelButton_;
    juce::TextButton moduleScopeButton_;
    juce::TextButton trackScopeButton_;
    juce::ComboBox modulationTargetSelector_;
    juce::Slider modulationAmountSlider_;
    juce::ToggleButton modulationEnabledButton_;
    juce::ComboBox editorModeSelector_;
    juce::TextButton playButton_;
    juce::TextButton stopButton_;
    juce::TextButton recordButton_;
    juce::ToggleButton loopButton_;
    juce::ToggleButton muteButton_;
    juce::ToggleButton soloButton_;
    juce::ToggleButton armButton_;
    juce::ComboBox audioTakeSelector_;
    juce::TextButton addAudioTakeButton_;
    juce::TextButton recordAudioTakeButton_;
    juce::TextButton removeAudioTakeButton_;
    juce::TextButton detectTempoButton_;
    juce::TextButton syncTempoButton_;
    juce::ComboBox languageSelector_;
    juce::ComboBox snapSelector_;
    juce::ComboBox snapTypeSelector_;
    juce::ComboBox arrangementToolSelector_;
    juce::Slider tempoSlider_;

    juce::Label projectLabel_;
    juce::Label positionLabel_;
    juce::Label gainLabel_;
    juce::Label panLabel_;
    juce::Label sampleGainLabel_;
    juce::Label sampleFadeInLabel_;
    juce::Label sampleFadeOutLabel_;
    juce::Label clipStartLabel_;
    juce::Label sourceOffsetLabel_;
    juce::Label clipDurationLabel_;
    juce::Label clipStretchLabel_;
    juce::Label sourceTempoLabel_;
    juce::Label stretchModeLabel_;
    juce::Label audioTakeLabel_;
    juce::Label statusLabel_;
    KnobSlider gainSlider_;
    KnobSlider panSlider_;
    juce::Slider sampleGainSlider_;
    juce::Slider sampleFadeInSlider_;
    juce::Slider sampleFadeOutSlider_;
    juce::Slider clipStartSlider_;
    juce::Slider sourceOffsetSlider_;
    juce::Slider clipDurationSlider_;
    juce::Slider clipStretchSlider_;
    juce::Slider sourceTempoSlider_;
    juce::ComboBox stretchModeSelector_;

    LibraryBrowserComponent browser_;
    StaticPanel inspector_ { {}, {}, juce::Colour(0xffff8a00) };
    DeviceChainComponent deviceChain_;
    NodeGraphComponent nodeGraph_;
    NodeGraphComponent modulationGraph_;
    juce::var factoryNodeGraphState_;
    juce::var factoryVirtualBusNodeGraphState_;
    juce::var factoryMidiNodeGraphState_;
    juce::var sharedNodeModules_;
    // The same, for the value graphs that drive modulation.
    juce::var sharedModulationModules_;
    std::vector<juce::var> trackNodeGraphStates_;
    juce::var factoryModulationGraphState_;
    std::vector<TrackModulationState> trackModulationStates_;
    std::unordered_map<std::uint64_t, juce::var> midiNodeGraphStates_;
    std::unordered_map<std::uint64_t, juce::var>
        virtualBusNodeGraphStates_;

    std::unique_ptr<juce::FileChooser> chooser_;
    std::unique_ptr<FloatingGraphWindow> graphWindow_;
    std::unique_ptr<FloatingPanelWindow> inspectorWindow_;
    std::unique_ptr<FloatingPanelWindow> browserWindow_;
    std::unique_ptr<FloatingPanelWindow> editorWindow_;
    std::unique_ptr<FloatingPanelWindow> bottomWindow_;
    juce::ThreadPool audioEditPool_ {
        juce::ThreadPool::Options {}
            .withNumberOfThreads(1)
            .withThreadName("CyDAW audio edits")
    };
    juce::ThreadPool projectAutosavePool_ {
        juce::ThreadPool::Options {}
            .withNumberOfThreads(1)
            .withThreadName("CyDAW project autosave")
    };
    std::shared_ptr<std::atomic<bool>> audioEditCancellation_;
    std::atomic<bool> projectAutosaveInProgress_ {};
    juce::File projectRecoveryFile_;
    juce::int64 nextProjectAutosaveMs_ {};
    bool recoveryEnabled_ { true };
    bool recoveryPromptShown_ {};
    juce::File currentProjectFile_;
    juce::File currentRecordingFile_;
    std::optional<std::size_t> audioRecordingTargetTrack_;
    std::optional<std::uint64_t> audioRecordingTargetClipId_;
    double audioRecordingStartSeconds_ {};
    bool audioRecordingStartedPlayback_ {};
    bool audioRecordingPreviousLooping_ {};
    std::optional<std::size_t> launcherRecordingTrack_;
    std::optional<LauncherEditorContext> launcherEditorContext_;
    std::optional<LauncherEditorContext> launcherInspectorSelection_;
    InspectorSelection inspectorSelection_ { InspectorSelection::none };
    std::uint64_t selectedClipEventId_ {};
    double clipEditorCueSeconds_ {};
    int launcherRecordingScene_ { -1 };
    double launcherRecordingStartSeconds_ {};
    juce::File currentMasterExportFile_;
    juce::String masterExportStatus_;
    juce::String transientStatus_;
    juce::int64 transientStatusExpiryMs_ {};
    std::optional<std::size_t> selectedTrack_;
    std::uint64_t selectedAudioClipId_ {};
    std::uint64_t sourceEditorEventId_ {};
    std::vector<std::uint64_t> selectedAudioClipIds_;
    std::vector<std::vector<std::uint64_t>>
        arrangementMarqueeClipIdsByTrack_;
    std::vector<std::vector<std::uint64_t>>
        arrangementMarqueeBaseClipIdsByTrack_;
    bool arrangementMarqueeSelectionActive_ {};
    bool arrangementMarqueeDragging_ {};
    double arrangementTimeSelectionStartSeconds_ {};
    double arrangementTimeSelectionEndSeconds_ {};
    std::size_t arrangementTimeSelectionFirstTrack_ {};
    std::size_t arrangementTimeSelectionLastTrack_ {};
    std::vector<AudioTrackController::AudioClip> audioClipClipboard_;
    std::vector<ArrangementClipboardTrack>
        arrangementMarqueeClipboard_;
    double arrangementMarqueeClipboardStartSeconds_ {};
    double arrangementMarqueeClipboardDurationSeconds_ {};
    std::uint64_t selectedMidiTrackId_ {};
    std::uint64_t selectedMidiClipId_ {};
    std::uint64_t selectedVirtualBusGraphId_ {};
    std::size_t selectedPluginIndex_ {};
    juce::String automationTargetId_ { "gain" };
    // The bottom/context automation views use clip-local coordinates while
    // the engine stores automation on the global transport timeline.
    double contextualAutomationOffsetSeconds_ {};
    juce::ApplicationProperties layoutProperties_;
    std::vector<LayoutState> namedLayouts_;
    std::vector<LayoutState> layoutUndoStack_;
    std::vector<LayoutState> layoutRedoStack_;
    int inspectorLayoutWidth_ { 230 };
    int browserLayoutWidth_ { 280 };
    int bottomLayoutHeight_ { 230 };
    bool inspectorPanelVisible_ { true };
    bool browserPanelVisible_ { true };
    bool bottomPanelVisible_ { true };
    bool contextAutomationVisible_ { true };
    bool projectDirty_ {};
    bool audioEditInProgress_ {};
    bool continuousArrangementEditActive_ {};
    bool tempoDetectionInProgress_ {};
    int currentEditorMode_ { 1 };
    int editorModeBeforeGraph_ { 1 };
    int bottomPanelMode_ { 1 };
    int contextAutomationHeight_ { 150 };
    DeviceChainComponent::ViewScope deviceViewScope_ {
        DeviceChainComponent::ViewScope::track
    };
    double arrangementTimelineLengthSeconds_ { 1.0 };
    double arrangementViewStartSeconds_ {};
    double arrangementZoom_ { 1.0 };
    std::optional<std::size_t> arrangementMovePreviewTrack_;
    std::array<float, 2> masterPeak_ {};
    std::array<float, 2> masterRms_ {};
    std::array<float, 2> selectedTrackPeak_ {};
    std::array<float, 2> selectedTrackRms_ {};
    std::size_t nextLauncherReplayAction_ {};
    double lastLauncherReplayPosition_ {};
    bool replayingLauncherActions_ {};
    int uiTimerTick_ {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WorkspaceComponent)
};

} // namespace cydaw::app
