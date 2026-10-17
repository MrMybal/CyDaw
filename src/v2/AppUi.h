#pragma once

#include "RemoteCommands.h"
#include "Studio.h"
#include "UiState.h"

#include <cydaw/engine/SynthFamily.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <string>

// The whole interface of CyDAW 2, drawn every frame: the menu, the transport
// bar, the docked panels and the status line.

namespace cydaw::v2 {

class ImGuiHost;
class ArrangementView;
class GraphEditor;
class MixerView;
class RoutingView;
class SoundCardView;
class LocationsView;
class BanksView;
class SemanticSearchView;
class DevicesView;
class SamplerView;
class PrismView;
class KickView;
class SynthView;
class BatterieView;
class RackView;
class ClipEditorView;
class BrowserView;
class LibraryWindow;
class SampleCatalog;
class InspectorView;
class LauncherView;
class DeviceSignals;
class DeviceEditorView;
class WorkshopView;
class DjView;
class FoundryView;
class PluginWindows;
class Vst3Build;
class ContextMenus;
class ComputerKeyboard;
class ThemesView;
class TransportSync;
class Controllers;
class JsfxView;

class AppUi final : public RemoteUi {
public:
    AppUi(Studio& studio, ImGuiHost& host);
    ~AppUi() override;

    // Called once the ImGui context exists: layout, settings.
    void initialise();
    // Draws one frame.
    void frame();
    // Files dropped from the system, at a point of the window.
    void filesDropped(const juce::StringArray& files, float x, float y);

    // Asks the application to quit (menu, shortcut, window close); it asks
    // askBeforeQuit() first when there are unsaved changes.
    std::function<void()> onQuit;
    // Quits without asking (after the question, the control port).
    std::function<void()> onQuitNow;
    // "Save the changes?" before quitting.
    void askBeforeQuit();
    // The window's title changed (project name, unsaved changes).
    std::function<void(const juce::String&)> onTitle;

    [[nodiscard]] juce::File layoutFile() const;
    void resetLayout();
    // Automated sessions (probes, the MCP check) read the user's layout but
    // never write it.
    void setLayoutSaving(bool enabled) noexcept { layoutSaving_ = enabled; }
    void notify(const juce::String& message, bool error = false);
    // Sound matching: the notes to analyse, a model to load, and the result
    // followed from frame to frame.
    void chooseSoundToRecreate();
    void chooseSoundModel();
    // The sampler: a new track from samples, from an SFZ; a factory instrument.
    void chooseSamplerFiles();
    // A new MIDI track playing Prisme, its window open.
    void newPrismTrack();
    void newKickTrack();
    // A new MIDI track playing a synth of the family (its model), its window open.
    void newSynthTrack(engine::synth::Model model);
    // A new MIDI track playing Batterie (the first kit), its window open.
    void newBatterieTrack();
    void chooseSamplerInstrument();
    void addFactoryInstrument(const juce::String& key, bool model = false);
    void drawFactoryInstrumentsMenu();
    void loadSoundModelFile(const juce::File& file);
    void followSoundMatching();
    bool soundMatchingRunning_ {};
    // Probes: a panel in front (1 arrangement, 2 launcher, 3 devices, 4 graph,
    // 5 mixer, 6 editor) and the n-th track selected.
    void showPanel(int panel, int trackIndex);
    void showAudioSettings() { showAudioSettings_ = true; }
    bool showDialog(const juce::String& name) override
    {
        if (name == "transport-sync")
            showTransportSync_ = true;
        else if (name == "audio")
            showAudioSettings_ = true;
        else if (name == "soundcard")
            showAudioSettings_ = showSoundCard_ = true;
        else if (name == "companion")
            showCompanion_ = true;
        else if (name == "locations")
            state_.openLocations = true;
        else if (name == "banks")
            state_.showBanks = true;
        else if (name == "semantic")
            state_.showSemantic = true;
        else if (name == "library") {
            state_.showLibrary = true;
            state_.libraryFocus = true;
        }
        else if (name == "sampler" || name == "sampler-controls")
            return openSamplerEditor(name == "sampler-controls");
        else if (name == "prism" || name == "prism-here")
            return openPrismEditor(name == "prism-here");
        else if (name == "kick")
            return openKickEditor();
        else if (name == "batterie" || name == "batterie-mics")
            return openBatterieEditor(name == "batterie-mics");
        else if (engine::synth::modelForPrefix(name.toStdString()) != engine::synth::Model::none)
            return openSynthEditor(engine::synth::modelForPrefix(name.toStdString()));
        else
            return false;
        return true;
    }
    // The selected track's sampler card in its zone editor (`controls`: on
    // its controls' tab); false without one.
    bool openSamplerEditor(bool controls = false);
    // The selected track's Prisme in its window; false without one.
    bool openPrismEditor(bool here = false);
    // The selected track's Kick in its window; false without one.
    bool openKickEditor();
    // The selected track's synth of that model in its window; false without one.
    bool openSynthEditor(engine::synth::Model model);
    // The selected track's Batterie in its window (`mics`: on its kit's
    // controls); false without one.
    bool openBatterieEditor(bool mics = false);
    // A panel in a window of its own, beside the main one (probe:
    // --detach=<1..6>, the panel numbers of showPanel).
    void detachPanel(int panel);

    // File commands, also used by the command line.
    void newProject();
    void openProject(const juce::File& file);
    // At once, the interface waiting (the command line: what follows needs it open).
    void openProjectNow(const juce::File& file);
    void chooseOpenProject();
    void save();
    void chooseSaveAs();
    void chooseImportProject();
    void chooseExportMix();
    void chooseImportMidi();

    // ---- Control port (RemoteCommands) ----------------------------------------------
    void showView(int view) override;
    [[nodiscard]] int currentView() const override { return currentView_; }
    bool snapshot(const juce::File& png) override;
    void sessionReplaced() override;
    bool openProjectInSteps(const juce::File& file) override;
    // A project of another software, imported in steps (its bar shown), its
    // summary at the end.
    bool importProjectInSteps(const juce::File& file) override;
    bool importing_ {};
    void tracksChanged() override;
    void requestQuit() override;
    [[nodiscard]] std::optional<TrackRef> selectedTrack() const override { return state_.selectedTrack; }
    void selectTrack(const TrackRef& track) override { state_.selectTrack(track); }
    void selectSample(const juce::File& file) override;
    void browseSamples(const juce::String& query, bool semantic, const juce::String& family, const juce::String& category,
                       const juce::String& source) override;
    juce::var library(const juce::String& command, const juce::var& request) override;
    juce::var midiInputs(const juce::String& command, const juce::var& request) override;
    juce::var themes(const juce::var& request) override;
    juce::var jsfx(const juce::var&) override;
    juce::var transportSync(const juce::var& request) override;
    juce::var controllers(const juce::var& request) override;
    juce::var dj(const juce::var& request) override;
    juce::var foundry(const juce::var& request) override;
    juce::var cinema(const juce::var& request) override;
    juce::String appMode(const juce::String& mode) override;
    bool showDeviceEditor(std::uint64_t nodeId, int pane, bool detached = false) override;
    void showWorkshop() override { state_.bringToFront = Panel::workshop; }
    void showDeviceCatalogue() override { state_.openDeviceCatalogue = true; }
    void showModulationGraph(const bool modulation) override { state_.graphShowsModulation = modulation; }
    void openRackPad(const std::optional<std::pair<TrackRef, int>>& pad) override { state_.openRackPad = pad; }
    void editClip(const TrackRef& track, const std::uint64_t clipId, const int face) override
    {
        state_.selectedTrack = track;
        state_.editedClip = ClipRef { track, clipId };
        state_.audioEditorFace = face;
        state_.bringToFront = Panel::editor;
    }
    bool showValueList(const TrackRef& track, std::uint64_t nodeId, bool open) override
    {
        const auto key = std::make_pair(studio_.routingKey(track) + (track.pad >= 0 ? static_cast<std::uint64_t>(track.pad + 1) << 48 : 0),
                                        nodeId);
        if (open)
            state_.valueLists.insert(key);
        else
            state_.valueLists.erase(key);
        state_.selectedTrack = track;
        return true;
    }
    bool showDeviceWindow(const TrackRef& track, std::uint64_t nodeId, bool open) override
    {
        const PluginWindow window { track, nodeId };
        if (state_.pluginWindowOpen(window) != open)
            state_.togglePluginWindow(window);
        return true;
    }
    Vst3Build* vst3Build() override { return vst3Build_.get(); }
    juce::String injectInput(const juce::var& events) override;
    // The scripted mouse, between the system's input and the frame.
    void applyScriptedInput();
    // A scripted key pressed, let go on the next event.
    int pendingKeyUp_ {};
    // Named layouts, in CyDAW/Layouts beside the current one.
    [[nodiscard]] juce::StringArray layoutNames() const override;
    juce::Result saveLayoutAs(const juce::String& name) override;
    juce::Result loadLayout(const juce::String& name) override;
    bool deleteLayout(const juce::String& name) override;
    // Between frames: a layout asked for is put in place (Dear ImGui reads
    // a layout only outside a frame), then the scripted mouse.
    void beforeNewFrame();

private:
    // Runs the action once the unsaved changes are saved or dropped, or
    // at once when there are none.
    void whenSaved(std::function<void()> action);
    void drawUnsavedPrompt();
    void drawFoundryPrompt();
    void chooseSaveAsThen(std::function<void()> then);
    void drawMenuBar();
    void drawTransportBar();
    void drawStatusBar();
    void drawDockSpace();
    void drawWindows();
    void toggleBottomPanel();
    void toggleSidePanel(Panel panel);
    void resetUiState();
    void drawDialogs();
    void buildDefaultLayout(unsigned int dockspaceId);
    void handleShortcuts();
    void saveLayoutIfNeeded();
    void undo();
    void redo();
    void toggleRecording();
    void drawAudioSettings();
    // The audio settings' MIDI tab: the devices, the computer's keyboard.
    void drawMidiInputs();
    void openMidiInputs()
    {
        showAudioSettings_ = true;
        selectMidiTab_ = true;
    }
    // Its device tab: driver, devices, rate, buffer.
    void drawAudioDevice();
    // Options › Compagnon Python: which Python, what it has, and setting up
    // its own environment (numpy, PyTorch CPU or CUDA) on a click.
    void drawCompanionWindow();
    bool showCompanion_ {};
    juce::String nvidiaGpu_;
    bool companionInfoAsked_ {};
    char companionPython_[512] {};
    void drawShortcuts();
    void drawLayoutNamePrompt();
    [[nodiscard]] juce::File layoutsFolder() const;
    [[nodiscard]] juce::File namedLayoutFile(const juce::String& name) const;

    Studio& studio_;
    ImGuiHost& host_;
    UiState state_;
    WaveformCache waveforms_;
    std::unique_ptr<DeviceSignals> signals_;
    std::unique_ptr<ArrangementView> arrangement_;
    std::unique_ptr<GraphEditor> graph_;
    std::unique_ptr<MixerView> mixer_;
    std::unique_ptr<RoutingView> routing_;
    std::unique_ptr<SoundCardView> soundCard_;
    std::unique_ptr<LocationsView> locations_;
    std::unique_ptr<BanksView> banks_;
    std::unique_ptr<SemanticSearchView> semanticView_;
    std::unique_ptr<DevicesView> devices_;
    std::unique_ptr<SamplerView> sampler_;
    std::unique_ptr<PrismView> prism_;
    std::unique_ptr<KickView> kick_;
    std::unique_ptr<SynthView> synth_;
    std::unique_ptr<BatterieView> batterie_;
    std::unique_ptr<RackView> rack_;
    std::unique_ptr<DeviceEditorView> deviceEditor_;
    std::unique_ptr<Vst3Build> vst3Build_;
    std::unique_ptr<ContextMenus> contextMenus_;
    // The Plugin workshop mode: the whole window is the workshop.
    void resolveMode();
    void drawWorkshopMode();
    void drawModeSwitch();
    std::unique_ptr<WorkshopView> workshop_;
    std::unique_ptr<DjView> dj_;
    std::unique_ptr<FoundryView> foundry_;
    std::unique_ptr<FoundryView> cinema_;
    std::unique_ptr<TransportSync> transportSync_;
    std::unique_ptr<Controllers> controllers_;
    bool showControllers_{};
    std::unique_ptr<JsfxView> jsfx_;
    bool showTransportSync_{};
    // The devices' windows, as plugins' (their designed interfaces).
    std::unique_ptr<PluginWindows> pluginWindows_;
    std::unique_ptr<ClipEditorView> clipEditor_;
    std::unique_ptr<SampleCatalog> catalog_;
    std::unique_ptr<BrowserView> browser_;
    std::unique_ptr<LibraryWindow> library_;
    std::unique_ptr<InspectorView> inspector_;
    std::unique_ptr<LauncherView> launcher_;
    std::unique_ptr<juce::FileChooser> chooser_;

    bool inspectorVisible_ { true };
    bool browserVisible_ { true };
    bool layoutBuilt_ {};
    bool resetLayoutRequested_ {};
    bool showDemo_ {};
    bool showAbout_ {};
    // A project opening in steps (Studio::startOpeningProject): its window
    // takes the place of the panels until it is open.
    juce::File opening_;
    void continueOpening();
    void drawOpening();
    void projectOpened(const juce::File& file, const juce::Result& result);
    // The logo, at about `pixels` wide (0 until the graphics device exists).
    [[nodiscard]] unsigned long long logo(float pixels);
    bool showWarnings_ {};
    bool showAudioSettings_ {};
    bool selectMidiTab_ {};
    bool selectMidiTabNow_ {};
    // The computer's keys as a MIDI keyboard.
    std::unique_ptr<ComputerKeyboard> computerKeyboard_;
    // The colour themes.
    std::unique_ptr<ThemesView> themes_;
    // The MIDI devices, asked again now and then (asking is slow).
    juce::Array<juce::MidiDeviceInfo> midiDevices_;
    double midiDevicesTime_ { -100.0 };
    // The audio settings open on the card's inputs and outputs.
    bool showSoundCard_ {};
    bool selectSoundCardTab_ {};
    bool showShortcuts_ {};
    bool recoveryChecked_ {};
    juce::String recoveryWhen_;
    juce::StringArray dialogLines_;
    juce::String dialogTitle_;
    double lastLayoutSave_ {};
    int focusFrames_ {};
    std::optional<TrackRef> liveTrack_;
    juce::String title_;
    int currentView_ { 1 };
    // A panel to take out of the dock into a window of its own, and for how
    // many more frames to insist (the dock places windows a frame late).
    Panel detaching_ { Panel::none };
    int detachFrames_ {};
    bool layoutSaving_ { true };
    // A layout to load before the next frame, and the name being typed for
    // one to save.
    juce::String pendingLayout_;
    bool askLayoutName_ {};
    char layoutName_[64] {};
    Panel lastFocused_ { Panel::arrangement };
    std::function<void()> pendingAction_;
    // Quitting with the Foundry's files not saved: what follows once the
    // person chose.
    std::function<void()> foundryAction_;
    bool foundryPromptRequested_ {};
    bool unsavedPromptRequested_ {};
    // The control port's pointer (ui_input): in use, where, the event for
    // the next frame, whether Shift is held.
    bool scriptedMouse_ {};
    float scriptedX_ {};
    float scriptedY_ {};
    juce::var scriptedEvent_;
    bool scriptedShift_ {};
};

} // namespace cydaw::v2
