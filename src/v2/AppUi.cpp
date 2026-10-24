#include "JsfxView.h"
#include "AppUi.h"

#include "ArrangementView.h"
#include "BrowserView.h"
#include "ClipEditorView.h"
#include "DeviceEditorView.h"
#include "DeviceSignals.h"
#include "DevicesView.h"
#include "GraphEditor.h"
#include "Localization.h"
#include "Vst3Build.h"
#include "ContextMenus.h"
#include "WorkshopView.h"
#include "DjView.h"
#include "FoundryView.h"
#include "ProcessingView.h"
#include "TransportSync.h"
#include "Controllers.h"
#include "ControlTargets.h"
#include "../app/DjEngine.h"
#include "../app/FoundryPlayer.h"
#include "PluginWindows.h"
#include "SamplerView.h"
#include "PrismView.h"
#include "KickView.h"
#include "SynthView.h"
#include "BatterieView.h"
#include "RackView.h"
#include "Icons.h"
#include "ImGuiHost.h"
#include "InspectorView.h"
#include "LauncherView.h"
#include "MixerView.h"
#include "RoutingView.h"
#include "SoundCardView.h"
#include "LocationsView.h"
#include "BanksView.h"
#include "SemanticSearchView.h"
#include "LibraryWindow.h"
#include "ComputerKeyboard.h"
#include "MidiDevices.h"
#include "SystemLoad.h"
#include "ThemesView.h"
#include "SampleCatalog.h"
#include "Theme.h"
#include "Widgets.h"
#include "BottomPanelControls.h"

#include "ProjectImport.h"
#include "CyDAWBranding.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <cstdio>

namespace cydaw::v2 {

using ui::dp;
using ui::L;
juce::var AppUi::jsfx(const juce::var& request){return jsfx_->command(request);}

namespace {

constexpr const char* kArrangement = "arrangement";
constexpr const char* kLauncher = "launcher";
constexpr const char* kDevices = "devices";
constexpr const char* kGraph = "graph";
constexpr const char* kMixer = "mixer";
constexpr const char* kRouting = "routing";
constexpr const char* kEditor = "editor";
constexpr const char* kBrowser = "browser";
constexpr const char* kInspector = "inspector";

std::string windowName(const char* label, const char* id)
{
    return ui::labelId(label, id);
}

std::string arrangementName() { return windowName(L("Arrangement", "Arrangement"), kArrangement); }
std::string launcherName() { return windowName(L("Lanceur", "Launcher"), kLauncher); }
std::string devicesName() { return windowName(L("Périphériques", "Devices"), kDevices); }
std::string graphName() { return windowName(L("Graphe", "Graph"), kGraph); }
std::string mixerName() { return windowName(L("Mixeur", "Mixer"), kMixer); }
std::string routingName() { return windowName(L("Routage", "Routing"), kRouting); }
std::string editorName() { return windowName(L("Éditeur", "Editor"), kEditor); }
std::string browserName() { return windowName(L("Navigateur", "Browser"), kBrowser); }
std::string inspectorName() { return windowName(L("Inspecteur", "Inspector"), kInspector); }

// CyDAW 1's view numbers for the panels.
int viewOf(const Panel panel)
{
    switch (panel) {
    case Panel::launcher: return 4;
    case Panel::editor: return 2;
    case Panel::graph:
    case Panel::devices: return 7;
    case Panel::mixer: return 9;
    case Panel::browser: return 10;
    default: return 1;
    }
}

// Raised when the default layout changes, so that it reaches saved layouts once.
constexpr int kLayoutVersion = 4;

juce::File layoutVersionFile(const juce::File& layout)
{
    return layout.withFileExtension("version");
}

} // namespace

AppUi::AppUi(Studio& studio, ImGuiHost& host)
    : studio_(studio), host_(host), waveforms_(studio.engine().formatManager())
{
    arrangement_ = std::make_unique<ArrangementView>(studio_, state_, waveforms_, host_);
    computerKeyboard_ = std::make_unique<ComputerKeyboard>(studio_);
    themes_ = std::make_unique<ThemesView>(studio_, state_);
    themes_->applySaved();
    state_.manyInstrumentWindows = studio_.settings().getBoolValue("manyInstrumentWindows", true);
    inspectorVisible_ = studio_.settings().getBoolValue("inspectorPanelVisible", true);
    browserVisible_ = studio_.settings().getBoolValue("browserPanelVisible", true);
    for (int i = 0; i < 5; ++i)
        state_.bottomPanelsVisible[i] = studio_.settings().getBoolValue("bottomPanelVisible" + juce::String(i), true);
    {
        // The displays' taps: the track's chain in the engine (or the simulator's).
        auto* session = &studio_;
        DeviceSignals::Source source;
        source.setTaps = [session](const bool simulation, const TrackRef& track, const DeviceSignals::Source::Taps& taps) {
            auto& engine = session->engine();
            if (simulation)
                engine.setSimulationTaps(taps);
            else if (track.isBus())
                engine.setVirtualBusTaps(track.bus, taps);
            else if (track.master)
                engine.setMasterNodeTaps(taps);
            else if (track.midi)
                engine.setMidiTrackNodeTaps(track.midiId, taps);
            else
                engine.setTrackNodeTaps(track.audioIndex, taps);
        };
        source.readTap = [session](const bool simulation, const TrackRef& track, const std::uint64_t nodeId, const bool input,
                                  engine::AudioAnalysisFrame& frame) {
            auto& engine = session->engine();
            if (simulation)
                return engine.readSimulationTap(nodeId, input, frame);
            if (track.isBus())
                return engine.readVirtualBusTap(track.bus, nodeId, input, frame);
            if (track.master)
                return engine.readMasterNodeTap(nodeId, input, frame);
            return track.midi ? engine.readMidiTrackNodeTap(track.midiId, nodeId, input, frame)
                              : engine.readTrackNodeTap(track.audioIndex, nodeId, input, frame);
        };
        source.readings = [session](const TrackRef& track, const std::uint64_t nodeId, std::vector<float>& readings) {
            auto& engine = session->engine();
            if (track.master)
                engine.masterNodeReadings(nodeId, readings);
            else if (track.midi)
                engine.midiTrackNodeReadings(track.midiId, nodeId, readings);
            else
                engine.trackNodeReadings(track.audioIndex, nodeId, readings);
        };
        source.peaks = [session](const TrackRef& track, const std::vector<std::uint64_t>& nodes, std::vector<float>& left,
                                std::vector<float>& right) {
            auto& engine = session->engine();
            if (track.master)
                engine.masterNodePeaks(nodes, left, right);
            else if (track.midi)
                engine.midiTrackNodePeaks(track.midiId, nodes, left, right);
            else
                engine.trackNodePeaks(track.audioIndex, nodes, left, right);
        };
        source.tempo = [session] { return session->engine().tempo(); };
        source.stillThere = [session](const TrackRef& track) {
            return track.isMaster() || session->isValid(track) || (track.isBus() && session->hardwareBus(track.bus));
        };
        signals_ = std::make_unique<DeviceSignals>(std::move(source));
    }
    graph_ = std::make_unique<GraphEditor>(studio_, state_, host_, *signals_);
    mixer_ = std::make_unique<MixerView>(studio_, state_);
    routing_ = std::make_unique<RoutingView>(studio_, state_);
    soundCard_ = std::make_unique<SoundCardView>(studio_, state_);
    locations_ = std::make_unique<LocationsView>(studio_, state_);
    banks_ = std::make_unique<BanksView>(studio_, state_);
    semanticView_ = std::make_unique<SemanticSearchView>(studio_, state_);
    devices_ = std::make_unique<DevicesView>(studio_, state_, *signals_);
    sampler_ = std::make_unique<SamplerView>(studio_, state_);
    devices_->setSamplerView(sampler_.get());
    prism_ = std::make_unique<PrismView>(studio_, state_);
    devices_->setPrismView(prism_.get());
    kick_ = std::make_unique<KickView>(studio_, state_);
    devices_->setKickView(kick_.get());
    synth_ = std::make_unique<SynthView>(studio_, state_);
    devices_->setSynthView(synth_.get());
    batterie_ = std::make_unique<BatterieView>(studio_, state_);
    devices_->setBatterieView(batterie_.get());
    rack_ = std::make_unique<RackView>(studio_, state_);
    devices_->setRackView(rack_.get());
    deviceEditor_ = std::make_unique<DeviceEditorView>(studio_, state_, *signals_);
    vst3Build_ = std::make_unique<Vst3Build>();
    contextMenus_ = std::make_unique<ContextMenus>(studio_, state_);
    workshop_ = std::make_unique<WorkshopView>(studio_, state_, host_, *signals_, *vst3Build_);
    dj_ = std::make_unique<DjView>(studio_, state_, host_);
    foundry_ = std::make_unique<FoundryView>(studio_, state_, host_);
    cinema_ = std::make_unique<FoundryView>(studio_, state_, host_, true);
    processing_ = std::make_unique<ProcessingView>(studio_, state_, host_);
    transportSync_ = std::make_unique<TransportSync>(studio_.settings(),studio_.engine().deviceManager(),studio_.keepsSettings(),
        [this](int target) {
            TransportSync::State s;
            auto& e=studio_.engine();
            if(target==0){s.playing=e.isPlaying();s.seconds=e.position();s.bpm=e.tempo();
                s.numerator=e.timeSignatureNumerator();s.denominator=e.timeSignatureDenominator();s.available=!e.isMasterExporting();}
            else if(target==1)s=foundry_->transportState();
            else if(target==6)s=cinema_->transportState();
            else {const auto d=e.dj().deck(target-2);s.playing=d.playing;s.bpm=d.playedBpm>0?d.playedBpm:(d.bpm>0?d.bpm:120);
                s.seconds=std::max(0.0,(d.position-d.firstBeat)*d.bpm/s.bpm);s.available=d.state==app::DjEngine::LoadState::ready;}
            return s;
        },[this](int target,const TransportSync::Update& u) {
            auto& e=studio_.engine();
            if(target==0){if(e.isMasterExporting())return;
                if(u.tempo){e.setTempo(u.state.bpm);
                    if(e.timeSignatureNumerator()!=u.state.numerator||e.timeSignatureDenominator()!=u.state.denominator)
                        e.setTimeSignature(u.state.numerator,u.state.denominator);}
                if(u.position)e.setPosition(u.state.seconds);
                if(u.play){if(u.state.playing)studio_.play();else {if(studio_.isRecording())(void)studio_.stopRecording(true);studio_.stopPlayback();}}}
            else if(target==1)foundry_->applyTransport(u);
            else if(target==6)cinema_->applyTransport(u);
            else {const int index=target-2;const auto d=e.dj().deck(index);
                if(d.state!=app::DjEngine::LoadState::ready)return;
                if(u.tempo&&d.bpm>0){e.dj().setSync(index,false);e.dj().setTempo(index,u.state.bpm/d.bpm);}
                if(u.position)e.dj().seek(index,d.firstBeat+u.state.seconds*(u.tempo?u.state.bpm:(d.playedBpm>0?d.playedBpm:d.bpm))/std::max(20.0,d.bpm));
                if(u.play)e.dj().setPlaying(index,u.state.playing);
            }
            host_.wake();
        });
    controllers_ = std::make_unique<Controllers>(studio_, state_, host_, studio_.keepsSettings());
    pluginWindows_ = std::make_unique<PluginWindows>(studio_, state_);
    jsfx_ = std::make_unique<JsfxView>(studio_, state_);
    clipEditor_ = std::make_unique<ClipEditorView>(studio_, state_, waveforms_, host_);
    catalog_ = std::make_unique<SampleCatalog>(studio_.samples());
    browser_ = std::make_unique<BrowserView>(studio_, state_, host_, *catalog_);
    library_ = std::make_unique<LibraryWindow>(studio_, state_, host_, *catalog_);
    inspector_ = std::make_unique<InspectorView>(studio_, state_);
    launcher_ = std::make_unique<LauncherView>(studio_, state_);
}

AppUi::~AppUi()
{
    controllers_.reset();
    transportSync_.reset();
    if (studio_.keepsSettings()) {
        for (int i = 0; i < 5; ++i)
            studio_.settings().setValue("bottomPanelVisible" + juce::String(i), state_.bottomPanelsVisible[i]);
        studio_.settings().setValue("inspectorPanelVisible", inspectorVisible_);
        studio_.settings().setValue("browserPanelVisible", browserVisible_);
    }
    // The layout as it is when the window closes.
    if (layoutSaving_ && ImGui::GetCurrentContext() != nullptr) {
        size_t size = 0;
        const auto* data = ImGui::SaveIniSettingsToMemory(&size);
        if (size > 0) {
            const auto file = layoutFile();
            file.getParentDirectory().createDirectory();
            file.replaceWithData(data, size);
            (void) layoutVersionFile(file).replaceWithText(juce::String(kLayoutVersion));
        }
    }
}

juce::File AppUi::layoutFile() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("CyDAW")
        .getChildFile("CyDAW2.layout.ini");
}

void AppUi::initialise()
{
    const auto file = layoutFile();
    if (file.existsAsFile() && layoutVersionFile(file).loadFileAsString().trim().getIntValue() == kLayoutVersion) {
        const auto text = file.loadFileAsString();
        ImGui::LoadIniSettingsFromMemory(text.toRawUTF8(), static_cast<size_t>(text.getNumBytesAsUTF8()));
        layoutBuilt_ = true;
    }
}

void AppUi::resetLayout()
{
    resetLayoutRequested_ = true;
}

juce::File AppUi::layoutsFolder() const
{
    return layoutFile().getParentDirectory().getChildFile("Layouts");
}

juce::File AppUi::namedLayoutFile(const juce::String& name) const
{
    const auto cleaned = juce::File::createLegalFileName(name.trim()).substring(0, 64).trim();
    return cleaned.isEmpty() ? juce::File() : layoutsFolder().getChildFile(cleaned + ".ini");
}

juce::StringArray AppUi::layoutNames() const
{
    juce::StringArray names;
    for (const auto& file : layoutsFolder().findChildFiles(juce::File::findFiles, false, "*.ini"))
        names.add(file.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

juce::Result AppUi::saveLayoutAs(const juce::String& name)
{
    const auto file = namedLayoutFile(name);
    if (file == juce::File())
        return juce::Result::fail(ui::Ls("Donnez un nom à la disposition", "Give the layout a name"));
    if (ImGui::GetCurrentContext() == nullptr)
        return juce::Result::fail("no interface");
    size_t size = 0;
    const auto* data = ImGui::SaveIniSettingsToMemory(&size);
    if (size == 0 || !file.getParentDirectory().createDirectory() || !file.replaceWithData(data, size))
        return juce::Result::fail(ui::Ls("La disposition n'a pas pu être écrite : ", "The layout could not be written: ")
                                  + file.getFullPathName());
    notify(ui::Ls("Disposition enregistrée : ", "Layout saved: ") + file.getFileNameWithoutExtension());
    return juce::Result::ok();
}

juce::Result AppUi::loadLayout(const juce::String& name)
{
    const auto file = namedLayoutFile(name);
    if (!file.existsAsFile())
        return juce::Result::fail(ui::Ls("Aucune disposition « ", "No layout \"") + name
                                  + ui::Ls(" »", "\""));
    pendingLayout_ = file.loadFileAsString();
    if (pendingLayout_.isEmpty())
        return juce::Result::fail(ui::Ls("Cette disposition est vide", "This layout is empty"));
    notify(ui::Ls("Disposition : ", "Layout: ") + file.getFileNameWithoutExtension());
    host_.wake();
    return juce::Result::ok();
}

bool AppUi::deleteLayout(const juce::String& name)
{
    const auto file = namedLayoutFile(name);
    // To the recycle bin rather than gone.
    return file.existsAsFile() && file.moveToTrash();
}

void AppUi::beforeNewFrame()
{
    if (pendingLayout_.isNotEmpty() && ImGui::GetCurrentContext() != nullptr) {
        const auto text = std::exchange(pendingLayout_, juce::String());
        ImGui::LoadIniSettingsFromMemory(text.toRawUTF8(), text.getNumBytesAsUTF8());
        // The focused panel would take its tab back from the layout.
        ImGui::SetWindowFocus(nullptr);
        layoutBuilt_ = true;
        resetLayoutRequested_ = false;
        // The layout in use follows, as after any change of the panels.
        ImGui::GetIO().WantSaveIniSettings = true;
    }
    applyScriptedInput();
}

void AppUi::drawLayoutNamePrompt()
{
    if (askLayoutName_) {
        ImGui::OpenPopup("##layoutName");
        askLayoutName_ = false;
    }
    if (!ImGui::BeginPopupModal("##layoutName", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
        return;
    ImGui::PushFont(theme::boldFont(), ImGui::GetStyle().FontSizeBase * 1.1f);
    ImGui::TextUnformatted(L("Enregistrer la disposition", "Save the layout"));
    ImGui::PopFont();
    ImGui::TextDisabled("%s", L("Les panneaux, leur place et les fenêtres détachées, autres écrans compris.",
                                "The panels, their places and the detached windows, other screens included."));
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(dp(320.0f));
    const auto entered = ImGui::InputTextWithHint("##name", L("Nom (Mixage, Deux écrans…)", "Name (Mixing, Two screens…)"),
                                                  layoutName_, sizeof(layoutName_), ImGuiInputTextFlags_EnterReturnsTrue);
    const auto name = juce::String::fromUTF8(layoutName_).trim();
    const auto exists = name.isNotEmpty() && namedLayoutFile(name).existsAsFile();
    if (exists)
        ImGui::TextColored(theme::vec(theme::palette().record), "%s",
                           L("Une disposition porte déjà ce nom : elle sera remplacée.",
                             "A layout already has this name: it will be replaced."));
    ImGui::Spacing();
    ImGui::BeginDisabled(name.isEmpty());
    const auto save = ui::primaryButton(L("Enregistrer", "Save"), ImVec2(dp(110.0f), 0.0f)) || (entered && name.isNotEmpty());
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(L("Annuler", "Cancel"), ImVec2(dp(90.0f), 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();
    if (save) {
        if (const auto saved = saveLayoutAs(name); saved.failed())
            notify(saved.getErrorMessage(), true);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void AppUi::chooseSoundToRecreate(const juce::String& kind)
{
    const auto synth = kind == "synth";
    chooser_ = std::make_unique<juce::FileChooser>(
        synth ? L("La note dont chercher le patch (une note isolée)", "The note to find a patch for (one isolated note)")
              : L("Les notes à recréer (une note isolée par fichier, ou un enregistrement de plusieurs notes)",
                  "The notes to recreate (one isolated note per file, or a recording of several notes)"),
        juce::File {}, synth ? "*.wav;*.aif;*.aiff;*.flac" : "*.wav");
    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                       | (synth ? 0 : juce::FileBrowserComponent::canSelectMultipleItems);
    chooser_->launchAsync(flags, [this, kind, synth](const juce::FileChooser& chooser) {
        const auto files = chooser.getResults();
        if (files.isEmpty())
            return;
        if (synth) {
            if (const auto started = studio_.matchSynth(files.getFirst()); started.failed())
                notify(started.getErrorMessage(), true);
            else
                notify(ui::Ls("Recherche d'un patch de synthé…", "Searching a synth patch…"));
            return;
        }
        juce::StringArray options;
        if (kind != "auto")
            options.addArray({ "--kind", kind });
        if (const auto started = studio_.recreateSound(files, {}, refineRecreated_, options); started.failed())
            notify(started.getErrorMessage(), true);
        else
            notify(ui::Ls("Analyse du son en cours…", "Analysing the sound…"));
    });
}

bool AppUi::openSamplerEditor(const bool controls)
{
    if (!state_.selectedTrack)
        return false;
    const auto card = studio_.samplerCardOf(*state_.selectedTrack);
    if (card == 0)
        return false;
    // Asked by name (the control port): in the studio's window, in front.
    sampler_->openEditor(*state_.selectedTrack, card, false, controls, true);
    return true;
}

bool AppUi::openPrismEditor(const bool here)
{
    if (!state_.selectedTrack)
        return false;
    if (auto* slot = studio_.graphFor(*state_.selectedTrack))
        for (const auto& node : slot->graph().editorNodes())
            if (node.typeKey == "prism-synth") {
                prism_->openEditor(*state_.selectedTrack, node.id, false, here);
                return true;
            }
    return false;
}

bool AppUi::openKickEditor()
{
    if (!state_.selectedTrack)
        return false;
    if (auto* slot = studio_.graphFor(*state_.selectedTrack))
        for (const auto& node : slot->graph().editorNodes())
            if (node.typeKey == "kick-synth") {
                kick_->openEditor(*state_.selectedTrack, node.id);
                return true;
            }
    return false;
}

bool AppUi::openSynthEditor(const engine::synth::Model model)
{
    if (!state_.selectedTrack)
        return false;
    const auto typeKey = juce::String(engine::synth::info(model).typeKey);
    if (auto* slot = studio_.graphFor(*state_.selectedTrack))
        for (const auto& node : slot->graph().editorNodes())
            if (node.typeKey == typeKey) {
                synth_->openEditor(*state_.selectedTrack, node.id);
                return true;
            }
    return false;
}

bool AppUi::openBatterieEditor(const bool mics)
{
    if (!state_.selectedTrack)
        return false;
    if (auto* slot = studio_.graphFor(*state_.selectedTrack))
        for (const auto& node : slot->graph().editorNodes())
            if (node.typeKey == "batterie") {
                batterie_->openEditor(*state_.selectedTrack, node.id, false, mics);
                return true;
            }
    return false;
}

void AppUi::newBatterieTrack()
{
    juce::String error;
    const auto track = studio_.addBatterieTrack(0, error);
    if (!track) {
        notify(error.isNotEmpty() ? error : ui::Ls("Batterie n'a pas pu être créée", "Batterie could not be made"), true);
        return;
    }
    state_.selectTrack(*track);
    if (auto* slot = studio_.graphFor(*track))
        for (const auto& node : slot->graph().editorNodes())
            if (node.typeKey == "batterie") {
                batterie_->openEditor(*track, node.id);
                break;
            }
}

void AppUi::newSynthTrack(const engine::synth::Model model)
{
    const auto& info = engine::synth::info(model);
    const auto name = juce::String::fromUTF8(info.french.c_str());
    const auto track = studio_.addMidiTrack(name, juce::String(info.instrument));
    if (!track) {
        notify(ui::french() ? name + juce::String::fromUTF8(" n'a pas pu être créé") : name + " could not be made", true);
        return;
    }
    state_.selectTrack(*track);
    // Its window opens on it.
    if (auto* slot = studio_.graphFor(*track))
        for (const auto& node : slot->graph().editorNodes())
            if (node.typeKey == juce::String(info.typeKey)) {
                synth_->openEditor(*track, node.id);
                break;
            }
}

void AppUi::newKickTrack()
{
    const auto track = studio_.addMidiTrack("Kick", "kick-instrument");
    if (!track) {
        notify(ui::Ls("Kick n'a pas pu être créé", "Kick could not be made"), true);
        return;
    }
    state_.selectTrack(*track);
    if (auto* slot = studio_.graphFor(*track))
        for (const auto& node : slot->graph().editorNodes())
            if (node.typeKey == "kick-synth") {
                kick_->openEditor(*track, node.id);
                break;
            }
}

void AppUi::newPrismTrack()
{
    const auto track = studio_.addMidiTrack("Prisme", "prism-instrument");
    if (!track) {
        notify(ui::Ls("Prisme n'a pas pu être créé", "Prisme could not be made"), true);
        return;
    }
    state_.selectTrack(*track);
    // Its window opens on it.
    if (auto* slot = studio_.graphFor(*track))
        for (const auto& node : slot->graph().editorNodes())
            if (node.typeKey == "prism-synth") {
                prism_->openEditor(*track, node.id);
                break;
            }
}

void AppUi::chooseSamplerFiles()
{
    chooser_ = std::make_unique<juce::FileChooser>(L("Les échantillons du sampler", "The sampler's samples"), juce::File {},
                                                   "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this](const juce::FileChooser& chooser) {
                              const auto files = chooser.getResults();
                              if (files.isEmpty())
                                  return;
                              juce::String error;
                              if (const auto track = studio_.newSamplerTrack(files, error)) {
                                  state_.selectTrack(*track);
                                  state_.bringToFront = Panel::devices;
                                  notify(ui::Ls("Sampler : ", "Sampler: ") + studio_.trackName(*track));
                              } else {
                                  notify(error, true);
                              }
                          });
}

void AppUi::chooseSamplerInstrument()
{
    chooser_ = std::make_unique<juce::FileChooser>(L("Un instrument SFZ ou une carte .cysampler", "An SFZ instrument or a .cysampler map"),
                                                   juce::File {}, "*.sfz;*.cysampler");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this](const juce::FileChooser& chooser) {
                              const auto file = chooser.getResult();
                              if (file == juce::File())
                                  return;
                              juce::String error;
                              if (const auto track = studio_.loadSamplerFile(file, std::nullopt, error)) {
                                  state_.selectTrack(*track);
                                  state_.bringToFront = Panel::devices;
                                  notify(ui::Ls("Sampler : ", "Sampler: ") + studio_.trackName(*track));
                              } else {
                                  notify(error, true);
                              }
                          });
}

void AppUi::addFactoryInstrument(const juce::String& key, const bool model)
{
    juce::String error;
    if (const auto track = studio_.addFactoryInstrument(key, error, model)) {
        state_.selectTrack(*track);
        notify(studio_.trackName(*track));
    } else {
        notify(error, true);
    }
}

void AppUi::drawFactoryInstrumentsMenu()
{
    const auto instruments = studio_.factoryInstruments();
    if (!ImGui::BeginMenu(L("Instruments d'usine", "Factory instruments"), !instruments.empty())) {
        if (instruments.empty())
            ui::tooltip(L("Le dossier Factory n'est pas à côté de CyDAW (tools/factory_content le construit).",
                          "The Factory folder is not beside CyDAW (tools/factory_content builds it)."));
        return;
    }
    // By category, as the catalogue lists them.
    juce::StringArray categories;
    for (const auto& instrument : instruments)
        categories.addIfNotAlreadyThere(instrument.category);
    for (const auto& category : categories) {
        const auto first = std::ranges::find_if(instruments, [&category](const auto& item) { return item.category == category; });
        if (ImGui::BeginMenu(ui::labelId(first->categoryName.toRawUTF8(), category.toRawUTF8()).c_str())) {
            for (const auto& instrument : instruments)
                if (instrument.category == category) {
                    if (ImGui::MenuItem(instrument.name.toRawUTF8(), instrument.range.toRawUTF8()))
                        addFactoryInstrument(instrument.key);
                    ui::tooltip(L("Les notes enregistrées, jouées telles quelles par un sampler.",
                                  "The recorded notes, played as they are by a sampler."));
                }
            // The same instruments as sound models: lighter, every knob of a
            // model (brightness, inharmonicity, formants…).
            auto models = false;
            for (const auto& instrument : instruments)
                if (instrument.category == category && instrument.model != juce::File()) {
                    if (!models) {
                        ImGui::Separator();
                        ImGui::TextDisabled("%s", L("Modèles de son", "Sound models"));
                        models = true;
                    }
                    if (ImGui::MenuItem(ui::labelId(instrument.name.toRawUTF8(), (instrument.key + "-model").toRawUTF8()).c_str()))
                        addFactoryInstrument(instrument.key, true);
                    ui::tooltip(L("Les mêmes notes analysées en harmoniques et bruit : un modèle de son, chaque réglage du "
                                  "modèle à portée (brillance, inharmonicité, formants…).",
                                  "The same notes analysed into harmonics and noise: a sound model, every setting of the "
                                  "model at hand (brightness, inharmonicity, formants…)."));
                }
            ImGui::EndMenu();
        }
    }
    ImGui::EndMenu();
}

void AppUi::chooseSoundModel()
{
    chooser_ = std::make_unique<juce::FileChooser>(L("Un modèle de son", "A sound model"), Studio::soundModelFolder(),
                                                   "*.cysound");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this](const juce::FileChooser& chooser) {
                              const auto file = chooser.getResult();
                              if (file == juce::File())
                                  return;
                              loadSoundModelFile(file);
                          });
}

void AppUi::loadSoundModelFile(const juce::File& file)
{
    juce::String error;
    if (const auto track = studio_.loadSoundModel(file, std::nullopt, error)) {
        state_.selectTrack(*track);
        notify(ui::Ls("Modèle de son : ", "Sound model: ") + studio_.soundMatching().description);
    } else {
        notify(error, true);
    }
}

// A recreated sound ready: its track chosen, what it is said.
void AppUi::followSoundMatching()
{
    const auto matching = studio_.soundMatching();
    if (matching.running) {
        soundMatchingRunning_ = true;
        return;
    }
    if (!soundMatchingRunning_)
        return;
    soundMatchingRunning_ = false;
    if (matching.error.isNotEmpty()) {
        notify(ui::Ls("La recréation a échoué : ", "Recreating the sound failed: ") + matching.error, true);
        return;
    }
    if (matching.track)
        state_.selectTrack(*matching.track);
    auto line = ui::Ls("Instrument recréé : ", "Instrument recreated: ") + matching.description;
    // A synth patch found: how close it came (0: the very sound).
    if (const auto* zones = matching.zones.getArray();
        zones != nullptr && zones->size() == 1 && zones->getFirst().hasProperty("synth")) {
        line << ui::Ls(" — écart ", " — distance ") << juce::String(static_cast<double>(zones->getFirst().getProperty("loss", 0.0)), 2);
        notify(line);
        return;
    }
    if (const auto* zones = matching.zones.getArray(); zones != nullptr && !zones->isEmpty()) {
        auto error = 0.0;
        for (const auto& zone : *zones)
            error += static_cast<double>(zone.getProperty("bandErrorDb", 0.0));
        line << ui::Ls(" — écart moyen ", " — mean difference ") << juce::String(error / zones->size(), 1) << " dB";
    }
    notify(line);
}

void AppUi::notify(const juce::String& message, const bool error)
{
    state_.status = message;
    state_.statusIsError = error;
    state_.statusTime = ImGui::GetCurrentContext() != nullptr ? ImGui::GetTime() : 0.0;
    host_.wake();
}

void AppUi::detachPanel(const int panel)
{
    static constexpr Panel panels[] = { Panel::arrangement, Panel::launcher, Panel::devices,
                                        Panel::graph, Panel::mixer, Panel::editor };
    if (panel >= 1 && panel <= 6) {
        detaching_ = panels[panel - 1];
        detachFrames_ = 0;
    }
}

void AppUi::showPanel(const int panel, const int trackIndex)
{
    static constexpr Panel panels[] = { Panel::arrangement, Panel::launcher, Panel::devices,
                                        Panel::graph, Panel::mixer, Panel::editor };
    if (panel >= 1 && panel <= 6)
        state_.bringToFront = panels[panel - 1];
    const auto tracks = studio_.tracks();
    if (trackIndex >= 0 && trackIndex < static_cast<int>(tracks.size()))
        state_.selectedTrack = tracks[static_cast<std::size_t>(trackIndex)];
    // The editor opens the selected track's first clip.
    if (panel == 6 && state_.selectedTrack && !state_.selectedTrack->master) {
        const auto& track = *state_.selectedTrack;
        if (track.midi) {
            if (const auto* clips = studio_.engine().midiClipsView(track.midiId); clips != nullptr && !clips->empty())
                state_.editedClip = ClipRef { track, clips->front().id };
        } else if (const auto* clips = studio_.engine().audioClipsView(track.audioIndex); clips != nullptr && !clips->empty()) {
            state_.editedClip = ClipRef { track, clips->front().id };
        }
    }
}

void AppUi::saveLayoutIfNeeded()
{
    auto& io = ImGui::GetIO();
    const auto now = ImGui::GetTime();
    if (!io.WantSaveIniSettings || now - lastLayoutSave_ < 2.0)
        return;
    if (!layoutSaving_) {
        io.WantSaveIniSettings = false;
        return;
    }
    size_t size = 0;
    const auto* data = ImGui::SaveIniSettingsToMemory(&size);
    const auto file = layoutFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithData(data, size);
    (void) layoutVersionFile(file).replaceWithText(juce::String(kLayoutVersion));
    io.WantSaveIniSettings = false;
    lastLayoutSave_ = now;
}

// ---- File commands ---------------------------------------------------------------------

void AppUi::newProject()
{
    studio_.newProject();
    resetUiState();
    waveforms_.clear();
    notify(ui::Ls("Nouveau projet", "New project"));
}

void AppUi::openProject(const juce::File& file)
{
    // In steps: the interface keeps drawing, a bar says how far it is.
    if (!openProjectInSteps(file))
        notify(ui::Ls("Impossible d'ouvrir ", "Could not open ") + file.getFullPathName(), true);
}

void AppUi::openProjectNow(const juce::File& file)
{
    projectOpened(file, studio_.loadProject(file));
}

bool AppUi::openProjectInSteps(const juce::File& file)
{
    if (studio_.openingProject() || !studio_.startOpeningProject(file))
        return false;
    opening_ = file;
    host_.wake();
    return true;
}

void AppUi::continueOpening()
{
    // A share of each frame: the bar moves, the window answers.
    if (const auto result = studio_.continueOpeningProject(24.0)) {
        if (!std::exchange(importing_, false)) {
            projectOpened(opening_, *result);
            return;
        }
        // An import: its summary, what it could not bring.
        const auto file = std::exchange(opening_, juce::File {});
        resetUiState();
        waveforms_.clear();
        if (result->failed()) {
            notify(result->getErrorMessage(), true);
            return;
        }
        if (const auto tracks = studio_.tracks(); !tracks.empty())
            state_.selectedTrack = tracks.front();
        dialogTitle_ = ui::Ls("Projet importé", "Project imported");
        dialogLines_ = juce::StringArray::fromLines(studio_.importSummary());
        showWarnings_ = true;
        notify(ui::Ls("Importé : ", "Imported: ") + file.getFileName());
    }
}

bool AppUi::importProjectInSteps(const juce::File& file)
{
    if (studio_.openingProject() || !studio_.startImportingProject(file)) {
        notify(ui::Ls("Impossible d'importer ", "Could not import ") + file.getFullPathName(), true);
        return false;
    }
    opening_ = file;
    importing_ = true;
    host_.wake();
    return true;
}

void AppUi::projectOpened(const juce::File& file, const juce::Result& result)
{
    opening_ = juce::File {};
    const auto selectedBefore = state_.pixelsPerSecond;
    resetUiState();
    state_.pixelsPerSecond = selectedBefore;
    waveforms_.clear();
    if (result.failed()) {
        notify(result.getErrorMessage(), true);
        return;
    }
    const auto tracks = studio_.tracks();
    if (!tracks.empty())
        state_.selectedTrack = tracks.front();
    notify(ui::Ls("Projet ouvert : ", "Project opened: ") + file.getFileNameWithoutExtension());
    if (!studio_.loadWarnings().isEmpty()) {
        dialogTitle_ = ui::Ls("Ce qui n'a pas pu être chargé", "What could not be loaded");
        dialogLines_ = studio_.loadWarnings();
        showWarnings_ = true;
    }
}

void AppUi::chooseOpenProject()
{
    chooser_ = std::make_unique<juce::FileChooser>(L("Ouvrir un projet CyDAW", "Open a CyDAW project"), juce::File {},
                                                   "*.cydaw");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this](const juce::FileChooser& chooser) {
                              const auto file = chooser.getResult();
                              if (file.existsAsFile())
                                  openProject(file);
                          });
}

void AppUi::save()
{
    if (studio_.projectFile() == juce::File {}) {
        chooseSaveAs();
        return;
    }
    const auto result = studio_.saveProject(studio_.projectFile());
    notify(result.wasOk() ? ui::Ls("Enregistré : ", "Saved: ") + studio_.projectFile().getFileName()
                          : result.getErrorMessage(),
           result.failed());
}

void AppUi::chooseSaveAs()
{
    chooseSaveAsThen({});
}

void AppUi::chooseSaveAsThen(std::function<void()> then)
{
    chooser_ = std::make_unique<juce::FileChooser>(L("Enregistrer le projet", "Save the project"), studio_.projectFile(),
                                                   "*.cydaw");
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, then = std::move(then)](const juce::FileChooser& chooser) {
                              auto file = chooser.getResult();
                              if (file == juce::File {})
                                  return;
                              file = file.withFileExtension(".cydaw");
                              const auto result = studio_.saveProject(file);
                              notify(result.wasOk() ? ui::Ls("Enregistré : ", "Saved: ") + file.getFileName()
                                                    : result.getErrorMessage(),
                                     result.failed());
                              if (result.wasOk() && then)
                                  then();
                          });
}

void AppUi::chooseImportProject()
{
    chooser_ = std::make_unique<juce::FileChooser>(
        L("Importer un projet (Live, Bitwig, Studio One, Reaper, FL Studio, Ardour, LMMS, Zrythm, DAWproject, AAF, MIDI et pistes audio)",
          "Import a project (Live, Bitwig, Studio One, Reaper, FL Studio, Ardour, LMMS, Zrythm, DAWproject, AAF, MIDI and stems)"),
        juce::File {}, app::project_import::supportedProjectWildcard());
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this](const juce::FileChooser& chooser) {
                              const auto file = chooser.getResult();
                              if (file.existsAsFile())
                                  importProjectInSteps(file);
                          });
}

void AppUi::chooseExportMix()
{
    const auto suggested = studio_.projectFile() != juce::File {} ? studio_.projectFile().withFileExtension(".wav")
                                                                  : juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                                                                        .getChildFile("CyDAW mix.wav");
    chooser_ = std::make_unique<juce::FileChooser>(L("Exporter le mix", "Export the mix"), suggested, "*.wav");
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this](const juce::FileChooser& chooser) {
                              const auto file = chooser.getResult();
                              if (file == juce::File {})
                                  return;
                              // Rendered in real time through the master, with two seconds of tail.
                              const auto started = studio_.engine().startMasterExport(file.withFileExtension(".wav"));
                              notify(started.wasOk() ? ui::Ls("Export en cours…", "Exporting…") : started.getErrorMessage(),
                                     started.failed());
                          });
}

void AppUi::chooseImportMidi()
{
    chooser_ = std::make_unique<juce::FileChooser>(L("Importer un fichier MIDI", "Import a MIDI file"), juce::File {}, "*.mid;*.midi");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this](const juce::FileChooser& chooser) {
                              const auto file = chooser.getResult();
                              if (!file.existsAsFile())
                                  return;
                              app::AudioTrackController::MidiFileImportSummary summary;
                              const auto imported = studio_.engine().importMidiFile(file, summary, false);
                              if (imported.failed()) {
                                  notify(imported.getErrorMessage(), true);
                                  return;
                              }
                              // The new tracks get a graph and an instrument.
                              for (const auto id : summary.trackIds) {
                                  studio_.adoptMidiTrack(id, {});
                                  (void) studio_.insertDevice(TrackRef { true, 0, id }, "opale-instrument");
                              }
                              studio_.markDirty();
                              notify(juce::String(static_cast<int>(summary.noteCount)) + ui::Ls(" notes importées", " notes imported"));
                          });
}

void AppUi::filesDropped(const juce::StringArray& files, const float x, const float y)
{
    // A project opening step by step: a track added now would land between
    // its tracks and shift their routing.
    if (studio_.openingProject())
        return;
    if (state_.mode == AppMode::dj && dj_->dropFiles(files))
        return;
    if (state_.mode == AppMode::cinema && cinema_->dropFiles(files, x, y))
        return;
    if (state_.mode == AppMode::foundry && foundry_->dropFiles(files, x, y))
        return;
    if (state_.mode == AppMode::processing && processing_->dropFiles(files))
        return;
    // Onto a sampler: its zones. A project opens; a sound model, an SFZ
    // become instruments; audio lands where it is dropped.
    if (const auto& drop = state_.samplerDrop; drop && x >= drop->x0 && x < drop->x1 && y >= drop->y0 && y < drop->y1) {
        sampler_->addFiles(drop->track, drop->nodeId, files);
        return;
    }
    for (const auto& path : files) {
        const juce::File file(path);
        if (file.hasFileExtension("jsfx")) {state_.jsfxFileToOpen=file;state_.showJsfx=true;return;}
        if (file.hasFileExtension(".cysound")) {
            loadSoundModelFile(file);
            return;
        }
        if (file.hasFileExtension("sfz;cysampler")) {
            juce::String error;
            if (const auto track = studio_.loadSamplerFile(file, std::nullopt, error))
                state_.selectTrack(*track);
            else
                notify(error, true);
            return;
        }
        if (file.hasFileExtension(".cydaw")) {
            whenSaved([this, file] { openProject(file); });
            return;
        }
        for (const auto& extension : app::project_import::supportedProjectExtensions())
            if (file.hasFileExtension(extension)) {
                // As from the menu: the project open is saved first if the
                // person wants, then the other one comes in, its bar shown.
                whenSaved([this, file] { importProjectInSteps(file); });
                return;
            }
    }
    if (launcher_->dropFiles(files, ImVec2(x, y)))
        return;
    if (!arrangement_->dropFiles(files, ImVec2(x, y)))
        if (const auto track = studio_.addAudioTrack(juce::File(files[0])))
            state_.selectedTrack = track;
}

// ---- Undo --------------------------------------------------------------------------------

void AppUi::toggleRecording()
{
    if (studio_.isRecording()) {
        const auto result = studio_.stopRecording(true);
        notify(result.wasOk() ? ui::Ls("Prise enregistrée", "Take recorded") : result.getErrorMessage(), result.failed());
        return;
    }
    const auto clip = state_.editedClip && state_.selectedTrack && state_.editedClip->track == *state_.selectedTrack
                          ? state_.editedClip->clipId
                          : std::uint64_t {};
    const auto result = studio_.startRecording(state_.selectedTrack, clip);
    notify(result.wasOk() ? ui::Ls("Enregistrement…", "Recording…") : result.getErrorMessage(), result.failed());
}

void AppUi::undo()
{
    if(state_.mode==AppMode::cinema){if(!cinema_->undoProcessing())cinema_->undo();return;}
    auto& engine = studio_.engine();
    if (state_.mode == AppMode::foundry) {
        if(!foundry_->undoProcessing())(void) foundry_->undo();
        return;
    }
    if (state_.mode == AppMode::dj) { (void)dj_->undo();return; }
    if (state_.focused == Panel::workshop || state_.inWorkshop()) {
        if (auto* slot = studio_.workshopGraph())
            (void) slot->graph().editorUndo();
        return;
    }
    if (state_.focused == Panel::graph || state_.focused == Panel::devices) {
        if (state_.selectedTrack)
            if (auto* slot = studio_.graphFor(*state_.selectedTrack); slot != nullptr && slot->graph().editorUndo())
                return;
    }
    if (state_.focused == Panel::editor && clipEditor_->undoLauncherAudio(false))
        return;
    // A master clip open: its history, or nothing (never another edit's).
    if (state_.focused == Panel::editor && !state_.editedClip && state_.editedMasterScene) {
        (void) clipEditor_->undoMasterClip(false);
        return;
    }
    if (state_.focused == Panel::editor && engine.canUndoMidiEdit()) {
        (void) engine.undoMidiEdit();
        return;
    }
    if (state_.focused == Panel::arrangement && arrangement_->undoAutomation())
        return;
    if (state_.focused == Panel::arrangement && arrangement_->undoMidiClips())
        return;
    if (engine.canUndoArrangementEdit())
        (void) engine.undoArrangementEdit();
    else if (engine.canUndoMidiEdit())
        (void) engine.undoMidiEdit();
}

void AppUi::redo()
{
    if(state_.mode==AppMode::cinema){if(!cinema_->undoProcessing(true))cinema_->redo();return;}
    auto& engine = studio_.engine();
    if (state_.mode == AppMode::foundry) {
        if(!foundry_->undoProcessing(true))(void) foundry_->redo();
        return;
    }
    if (state_.mode == AppMode::dj) { (void)dj_->undo(true);return; }
    if (state_.focused == Panel::workshop || state_.inWorkshop()) {
        if (auto* slot = studio_.workshopGraph())
            (void) slot->graph().editorRedo();
        return;
    }
    if (state_.focused == Panel::graph || state_.focused == Panel::devices) {
        if (state_.selectedTrack)
            if (auto* slot = studio_.graphFor(*state_.selectedTrack); slot != nullptr && slot->graph().editorRedo())
                return;
    }
    if (state_.focused == Panel::editor && clipEditor_->undoLauncherAudio(true))
        return;
    if (state_.focused == Panel::editor && !state_.editedClip && state_.editedMasterScene) {
        (void) clipEditor_->undoMasterClip(true);
        return;
    }
    if (state_.focused == Panel::editor && engine.canRedoMidiEdit()) {
        (void) engine.redoMidiEdit();
        return;
    }
    if (state_.focused == Panel::arrangement && !engine.canRedoArrangementEdit() && arrangement_->redoMidiClips())
        return;
    if (state_.focused == Panel::arrangement && !engine.canRedoArrangementEdit() && arrangement_->redoAutomation())
        return;
    if (engine.canRedoArrangementEdit())
        (void) engine.redoArrangementEdit();
    else if (engine.canRedoMidiEdit())
        (void) engine.redoMidiEdit();
}

// ---- Frame ----------------------------------------------------------------------------------

void AppUi::frame()
{
    // A project opening: its window only, until it is open.
    if (studio_.openingProject()) {
        continueOpening();
        if (studio_.openingProject()) {
            drawOpening();
            host_.setAnimating(true);
            return;
        }
    }
    if (refsSerial_ != studio_.trackRefsSerial()) {
        refsSerial_ = studio_.trackRefsSerial();
        state_.forgetTrackRefs();
    }
    // Where a sampler pane is, said again by the panel that draws it.
    state_.samplerDrop.reset();
    if (state_.focused != lastFocused_) {
        lastFocused_ = state_.focused;
        currentView_ = viewOf(state_.focused);
    }
    // The master stays picked: it is no engine track, but it is there.
    if (state_.selectedTrack
        && ((!state_.selectedTrack->master && !studio_.isValid(*state_.selectedTrack))
            || (state_.selectedTrack->isBus() && !studio_.hardwareBus(state_.selectedTrack->bus))))
        state_.selectedTrack.reset();
    if (!state_.selectedTrack) {
        const auto tracks = studio_.tracks();
        if (!tracks.empty())
            state_.selectedTrack = tracks.front();
    }
    studio_.setWatchedGraph(state_.selectedTrack);
    // A hybrid track's companion is never picked itself: its track is.
    if (state_.selectedTrack && !state_.selectedTrack->master && studio_.isCompanion(*state_.selectedTrack))
        state_.selectTrack(studio_.shownTrackOf(*state_.selectedTrack));
    // The keyboard and MIDI input play the selected instrument (an audio
    // track's companion instrument, when it is hybrid).
    auto live = state_.selectedTrack;
    if (live && !live->master && !live->midi)
        if (const auto companion = studio_.companionOf(*live); companion && companion->midi)
            live = companion;
    if (live != liveTrack_) {
        liveTrack_ = live;
        if (liveTrack_ && liveTrack_->midi)
            (void) studio_.engine().setLiveMidiTrack(liveTrack_->midiId);
    }
    {
        const auto name = studio_.projectName().isNotEmpty() ? studio_.projectName() : ui::Ls("Sans titre", "Untitled");
        const auto title = name + (studio_.isDirty() ? " *" : "") + " - CyDAW 2";
        if (title != title_) {
            title_ = title;
            if (onTitle)
                onTitle(title);
        }
    }

    // The computer's keys a controllers' device listens to go to it first,
    // and no further.
    controllers_->pollKeys();
    // The computer's keys play the picked instrument track, before the
    // shortcuts can take them; not where letters are shortcuts of their own
    // (the graph, the workshop).
    computerKeyboard_->update(state_.selectedTrack && state_.selectedTrack->midi && state_.mode == AppMode::studio
                              && state_.focused != Panel::graph);
    // A MIDI keyboard plugged in for the first time plays at once, as in
    // Bitwig; one switched off in the MIDI inputs stays off.
    if (ImGui::GetTime() - midiDevicesTime_ > 2.0) {
        midiDevicesTime_ = ImGui::GetTime();
        midiDevices_ = juce::MidiInput::getAvailableDevices();
        auto& settings = studio_.settings();
        auto known = juce::StringArray::fromLines(settings.getValue("knownMidiInputs"));
        auto changed = false;
        // An automated session leaves the person's devices as they are.
        for (const auto& device : midiDevices_)
            if (studio_.keepsSettings() && !known.contains(device.identifier)) {
                known.add(device.identifier);
                studio_.engine().deviceManager().setMidiInputDeviceEnabled(device.identifier, true);
                changed = true;
            }
        if (changed) {
            settings.setValue("knownMidiInputs", known.joinIntoString(juce::newLine));
            studio_.saveAudioSetup();
        }
    }
    cinema_->tickCinemaVideo();
    handleShortcuts();
    drawMenuBar();
    drawTransportBar();
    followSoundMatching();
    // What finished in the background: a download, a folder moved.
    for (const auto& [message, failed] : studio_.takeNotices())
        notify(message, failed);
    drawStatusBar();
    drawDockSpace();
    drawWindows();
    if (state_.mode != AppMode::dj)
        dj_->tickPersistence();
    pluginWindows_->draw();
    jsfx_->draw();
    JsfxView::drawQuitPrompts();
    contextMenus_->draw();
    drawDialogs();

    if (showDemo_)
        ImGui::ShowDemoWindow(&showDemo_);
    // Learn mode: over every window, last.
    controllers_->drawLearnOverlay();
    // The workshop's device plays in the simulator while the workshop shows it.
    studio_.setSimulatorActive(state_.inWorkshop() && studio_.workshopRoot() != nullptr);
    host_.setAnimating(studio_.engine().isPlaying() || studio_.engine().isSamplePreviewing() || studio_.simulating()
                       || studio_.engine().dj().anyPlaying() || studio_.engine().foundryPlayer().playing() || studio_.engine().cinemaPlayer().playing()
                       || studio_.engine().processingPlayer().playing());
    saveLayoutIfNeeded();
}

unsigned long long AppUi::logo(const float pixels)
{
    // A texture per size, drawn from the 512 pixel logo with care.
    auto size = 32;
    while (size < 512 && static_cast<float>(size) < pixels)
        size *= 2;
    const auto key = "logo-" + juce::String(size);
    static const auto source = juce::ImageCache::getFromMemory(cydaw::branding::cydawlogo512_png,
                                                               cydaw::branding::cydawlogo512_pngSize);
    if (!source.isValid())
        return 0;
    return host_.texture(key, size == source.getWidth() ? source
                                                        : source.rescaled(size, size, juce::Graphics::highResamplingQuality));
}

void AppUi::drawOpening()
{
    const auto& p = theme::palette();
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, p.bg0);
    ImGui::Begin("##opening", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking
                     | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleColor();
    const auto opening = studio_.projectOpening();
    auto* list = ImGui::GetWindowDrawList();
    const auto area = ImGui::GetContentRegionAvail();
    const auto origin = ImGui::GetCursorScreenPos();
    const auto width = std::min(dp(520.0f), area.x - dp(40.0f));
    const auto logoSize = dp(112.0f);
    const auto blockHeight = logoSize + dp(150.0f);
    const ImVec2 centre(origin.x + area.x * 0.5f, origin.y + std::max(dp(20.0f), (area.y - blockHeight) * 0.42f));
    // The logo, then what is opening.
    if (const auto texture = logo(logoSize * ImGui::GetIO().DisplayFramebufferScale.x)) {
        list->AddImage(ImTextureRef(static_cast<ImTextureID>(texture)), ImVec2(centre.x - logoSize * 0.5f, centre.y),
                       ImVec2(centre.x + logoSize * 0.5f, centre.y + logoSize));
    }
    auto y = centre.y + logoSize + dp(18.0f);
    ImGui::PushFont(theme::boldFont(), ImGui::GetStyle().FontSizeBase * 1.25f);
    const auto title = (opening.import ? ui::Ls("Importation de ", "Importing ") : ui::Ls("Ouverture de ", "Opening "))
                       + opening.file.getFileNameWithoutExtension();
    ui::centredText(list, ImVec2(centre.x - width * 0.5f, y), ImVec2(centre.x + width * 0.5f, y + ImGui::GetFontSize()), p.text,
                    title.toRawUTF8());
    y += ImGui::GetFontSize() + dp(14.0f);
    ImGui::PopFont();
    // The bar: a stripe moving while the file is read, then how far the tracks are.
    const ImVec2 barMin(centre.x - width * 0.5f, y);
    const ImVec2 barMax(centre.x + width * 0.5f, y + dp(10.0f));
    list->AddRectFilled(barMin, barMax, p.bg2, dp(5.0f));
    const auto filled = barMin.x + (barMax.x - barMin.x) * std::clamp(opening.progress, 0.0f, 1.0f);
    list->AddRectFilled(barMin, ImVec2(std::max(barMin.x + dp(10.0f), filled), barMax.y), p.accent, dp(5.0f));
    if (opening.reading) {
        const auto phase = static_cast<float>(std::fmod(ImGui::GetTime() * 0.6, 1.0));
        const auto stripe = dp(90.0f);
        const auto x = barMin.x + (barMax.x - barMin.x + stripe) * phase - stripe;
        list->PushClipRect(barMin, barMax, true);
        list->AddRectFilled(ImVec2(x, barMin.y), ImVec2(x + stripe, barMax.y), theme::alpha(p.accent, 0.45f), dp(5.0f));
        list->PopClipRect();
    }
    y = barMax.y + dp(10.0f);
    // What it does, how much, how long.
    char numbers[96];
    std::snprintf(numbers, sizeof(numbers), "%d %%  ·  %.1f Mo  ·  %.1f s", static_cast<int>(opening.progress * 100.0f + 0.5f),
                  static_cast<double>(opening.bytes) / 1.0e6, opening.seconds);
    ui::centredText(list, ImVec2(barMin.x, y), ImVec2(barMax.x, y + ImGui::GetFontSize()), p.textDim, opening.doing.toRawUTF8());
    y += ImGui::GetFontSize() + dp(6.0f);
    ui::centredText(list, ImVec2(barMin.x, y), ImVec2(barMax.x, y + ImGui::GetFontSize()), p.textFaint, numbers);
    ImGui::End();
}

void AppUi::resolveMode()
{
    // Asked for (a panel's request, the port), the workshop, the DJ mix or
    // the Foundry takes the whole window; a studio panel asked for gives it
    // back.
    if (state_.bringToFront == Panel::workshop || state_.bringToFront == Panel::dj || state_.bringToFront == Panel::foundry || state_.bringToFront == Panel::cinema
        || state_.bringToFront == Panel::processing) {
        state_.mode = state_.bringToFront == Panel::workshop       ? AppMode::workshop
                      : state_.bringToFront == Panel::dj           ? AppMode::dj
                      : state_.bringToFront == Panel::cinema       ? AppMode::cinema
                      : state_.bringToFront == Panel::processing   ? AppMode::processing
                                                                   : AppMode::foundry;
        state_.bringToFront = Panel::none;
    } else if (state_.mode != AppMode::studio && state_.bringToFront != Panel::none) {
        state_.mode = AppMode::studio;
    }
}

juce::var AppUi::controllers(const juce::var& request)
{
    if (request.getProperty("action", "").toString() == "show" || static_cast<bool>(request.getProperty("show", false)))
        showControllers_ = true;
    return controllers_->command(request);
}

juce::var AppUi::transportSync(const juce::var& request)
{
    if(static_cast<bool>(request.getProperty("show",false)))showTransportSync_=true;
    return transportSync_->command(request);
}

juce::var AppUi::dj(const juce::var& request)
{
    if (static_cast<bool>(request.getProperty("show", false)))
        state_.mode = AppMode::dj;
    return dj_->command(request);
}

juce::var AppUi::foundry(const juce::var& request)
{
    if (static_cast<bool>(request.getProperty("show", false)))
        state_.mode = AppMode::foundry;
    return foundry_->command(request);
}

juce::var AppUi::processing(const juce::var& request)
{
    return processing_->command(request);
}

bool AppUi::openProcessing(const TrackRef& track, const std::uint64_t clipId)
{
    state_.processingRequest = ClipRef { track, clipId };
    return true;
}

juce::var AppUi::cinema(const juce::var& request)
{
    if(static_cast<bool>(request.getProperty("show",false)))state_.mode=AppMode::cinema;
    return cinema_->command(request);
}

juce::String AppUi::appMode(const juce::String& mode)
{
    static constexpr std::pair<const char*, AppMode> modes[] { { "studio", AppMode::studio },
                                                                { "dj", AppMode::dj },
                                                                { "foundry", AppMode::foundry },
                                                                { "workshop", AppMode::workshop }, { "cinema", AppMode::cinema },
                                                                { "processing", AppMode::processing } };
    if (mode.isNotEmpty()) {
        auto found = false;
        for (const auto& [name, value] : modes)
            if (mode == name) {
                state_.mode = value;
                found = true;
            }
        if (!found)
            return {};
    }
    for (const auto& [name, value] : modes)
        if (value == state_.mode)
            return name;
    return "studio";
}

void AppUi::drawWorkshopMode()
{
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    constexpr auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
                           | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus
                           | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    const auto* name = state_.mode == AppMode::dj ? "##djMode" : state_.mode == AppMode::foundry ? "##foundryMode" : state_.mode == AppMode::cinema ? "##cinemaMode"
                       : state_.mode == AppMode::processing ? "##processingMode" : "##workshopMode";
    if (ImGui::Begin(name, nullptr, flags)) {
        if (state_.mode == AppMode::dj) {
            dj_->draw();
        } else if (state_.mode == AppMode::foundry) {
            foundry_->draw();
        } else if(state_.mode==AppMode::cinema){
            state_.focused=Panel::cinema;cinema_->draw();
        } else if (state_.mode == AppMode::processing) {
            processing_->draw();
        } else {
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
                state_.focused = Panel::workshop;
            workshop_->draw();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void AppUi::drawModeSwitch()
{
    const auto& p = theme::palette();
    const auto button = [&](const char* label, const bool active, const char* tooltip) {
        ImGui::PushStyleColor(ImGuiCol_Button, active ? p.accentDim : p.bg2);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? p.accent : p.bg3);
        ImGui::PushStyleColor(ImGuiCol_Text, active ? p.text : p.textDim);
        const auto clicked = ImGui::Button(label);
        ImGui::PopStyleColor(3);
        ui::tooltip(tooltip);
        return clicked;
    };
    if (button("Studio", state_.mode == AppMode::studio, L("Pistes, arrangement, lanceur, mixage", "Tracks, arrangement, launcher, mixing")))
        state_.mode = AppMode::studio;
    ImGui::SameLine(0.0f, dp(2.0f));
    if (button(L("Mix DJ", "DJ mix"), state_.mode == AppMode::dj,
               L("Mixer des morceaux comme un DJ : platines, synchro, boucles, hot cues, table de mixage, collection (F10)",
                 "Mix tracks as a DJ does: decks, sync, loops, hot cues, a mixer, a collection (F10)")))
        state_.mode = AppMode::dj;
    ImGui::SameLine(0.0f, dp(2.0f));
    if (button(L("Cinéma", "Cinema"), state_.mode==AppMode::cinema,
            L("Son à l'image, pistes virtuelles et transport externe", "Sound to picture, virtual tracks and external transport")))
        state_.mode=AppMode::cinema;
    ImGui::SameLine(0.0f,dp(2));
    if (button(L("Fonderie", "Foundry"), state_.mode == AppMode::foundry,
               L("Éditer des fichiers son : couper, coller, traiter, analyser, enregistrer (F11)",
                 "Edit sound files: cut, paste, process, analyse, save (F11)")))
        state_.mode = AppMode::foundry;
    ImGui::SameLine(0.0f, dp(2.0f));
    if (button(L("Traitements", "Processings"), state_.mode == AppMode::processing,
               L("Créer ses traitements audio (chaîne d'effets, code JSFX, débruitage…) et MIDI (scripts de notes), "
                 "les essayer avant / après, puis les appliquer aux clips",
                 "Make your own audio processings (effect chain, JSFX code, noise reduction…) and MIDI ones (note "
                 "scripts), try them before / after, then apply them to clips")))
        state_.mode = AppMode::processing;
    ImGui::SameLine(0.0f, dp(2.0f));
    if (button(L("Atelier Plugin", "Plugin workshop"), state_.inWorkshop(),
               L("Fabriquer ses propres effets et instruments avec un graphe, et les exporter en VST3 (F7)",
                 "Build your own effects and instruments with a graph, and export them as VST3 (F7)")))
        state_.mode = AppMode::workshop;
}

void AppUi::resetUiState()
{
    const auto visibility = state_.bottomPanelsVisible;
    state_ = UiState {};
    state_.bottomPanelsVisible = visibility;
    refsSerial_ = studio_.trackRefsSerial();
}

void AppUi::toggleBottomPanel()
{
    if (state_.inWorkshop())
        return;
    state_.setBottomPanelVisible(!state_.bottomPanelVisible());
    if (!state_.bottomPanelVisible()) {
        if (UiState::isBottomPanel(state_.bringToFront))
            state_.bringToFront = Panel::none;
        if (UiState::isBottomPanel(state_.focused))
            state_.focused = state_.mode == AppMode::studio ? Panel::arrangement
                             : state_.mode == AppMode::dj ? Panel::dj : state_.mode == AppMode::cinema ? Panel::cinema : Panel::foundry;
    }
    host_.wake(3);
}

void AppUi::toggleSidePanel(const Panel panel)
{
    if (panel != Panel::inspector && panel != Panel::browser)
        return;
    auto& visible = panel == Panel::inspector ? inspectorVisible_ : browserVisible_;
    visible = !visible;
    if (!visible) {
        if (state_.bringToFront == panel) {
            state_.bringToFront = Panel::none;
            focusFrames_ = 0;
        }
        if (state_.focused == panel)
            state_.focused = Panel::arrangement;
    }
    host_.wake(3);
}

void AppUi::drawWindows()
{
    if (state_.mode != AppMode::studio) {
        drawWorkshopMode();
        return;
    }
    if (UiState::isBottomPanel(state_.bringToFront))
        state_.setBottomPanelVisible(true);
    if (state_.bringToFront == Panel::browser)
        browserVisible_ = true;
    if (state_.bringToFront == Panel::inspector)
        inspectorVisible_ = true;
    const auto panel = [this](const std::string& name, const Panel which, auto&& body) {
        if ((UiState::isBottomPanel(which) && !state_.bottomPanelVisible())
            || (which == Panel::browser && !browserVisible_) || (which == Panel::inspector && !inspectorVisible_))
            return;
        // Out of the dock, in a window of its own beside the main one, which
        // stays a window even when moved back over it.
        if (detaching_ == which) {
            const auto* main = ImGui::GetMainViewport();
            ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
            ImGui::SetNextWindowPos(ImVec2(main->Pos.x + main->Size.x * 0.55f, main->Pos.y + main->Size.y * 0.25f),
                                    ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(main->Size.x * 0.42f, main->Size.y * 0.5f), ImGuiCond_Always);
            ImGuiWindowClass ownWindow;
            ownWindow.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
            ImGui::SetNextWindowClass(&ownWindow);
            if (++detachFrames_ >= 3) {
                detaching_ = Panel::none;
                detachFrames_ = 0;
            }
        }
        // Repeated for a few frames: on the first ones the dock may not have
        // placed the window yet, and the focus would go to another tab.
        if (state_.bringToFront == which) {
            ImGui::SetNextWindowFocus();
            if (++focusFrames_ >= 3) {
                state_.bringToFront = Panel::none;
                focusFrames_ = 0;
            }
        }
        if (ImGui::Begin(name.c_str())) {
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
                state_.focused = which;
            body();
        }
        ImGui::End();
    };
    panel(arrangementName(), Panel::arrangement, [this] { arrangement_->draw(); });
    panel(launcherName(), Panel::launcher, [this] { launcher_->draw(); });
    panel(devicesName(), Panel::devices, [this] { devices_->draw(); });
    panel(graphName(), Panel::graph, [this] { graph_->draw(); });
    panel(mixerName(), Panel::mixer, [this] { mixer_->draw(); });
    panel(routingName(), Panel::routing, [this] { routing_->draw(); });
    panel(editorName(), Panel::editor, [this] { clipEditor_->draw(); });
    panel(browserName(), Panel::browser, [this] { browser_->draw(); });
    panel(inspectorName(), Panel::inspector, [this] { inspector_->draw(); });
    if (state_.bottomPanelVisible()) {
        std::vector<ImGuiDockNode*> amended;
        bool hide = false;
        for (const auto& name : { devicesName(), graphName(), mixerName(), routingName(), editorName() }) {
            const auto* window = ImGui::FindWindowByName(name.c_str());
            auto* node = window != nullptr ? window->DockNode : nullptr;
            if (node == nullptr || std::find(amended.begin(), amended.end(), node) != amended.end())
                continue;
            amended.push_back(node);
            if (ImGui::DockNodeBeginAmendTabBar(node)) {
                hide = ui::bottomPanelHideTab() || hide;
                ImGui::DockNodeEndAmendTabBar();
            }
        }
        if (hide)
            toggleBottomPanel();
    }
    for (const auto which : { Panel::inspector, Panel::browser }) {
        if (!(which == Panel::inspector ? inspectorVisible_ : browserVisible_))
            continue;
        const auto name = which == Panel::inspector ? inspectorName() : browserName();
        const auto* window = ImGui::FindWindowByName(name.c_str());
        auto* node = window != nullptr ? window->DockNode : nullptr;
        if (node != nullptr && ImGui::DockNodeBeginAmendTabBar(node)) {
            const auto hide = which == Panel::inspector
                ? ui::panelHideTab(L("Masquer  <###hideInspector", "Hide  <###hideInspector"),
                                   L("Masquer l'inspecteur", "Hide the inspector"))
                : ui::panelHideTab(L("Masquer  >###hideBrowser", "Hide  >###hideBrowser"),
                                   L("Masquer le navigateur", "Hide the browser"));
            ImGui::DockNodeEndAmendTabBar();
            if (hide)
                toggleSidePanel(which);
        }
    }
    deviceEditor_->draw();
}

void AppUi::drawAudioSettings()
{
    if (state_.openSoundCard) {
        state_.openSoundCard = false;
        showAudioSettings_ = showSoundCard_ = true;
    }
    if (showAudioSettings_) {
        ImGui::OpenPopup("##audioSettings");
        showAudioSettings_ = false;
        selectSoundCardTab_ = showSoundCard_;
        showSoundCard_ = false;
        selectMidiTabNow_ = std::exchange(selectMidiTab_, false);
    }
    // Large: the card's inputs and outputs are tables of their own.
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(std::min(dp(1040.0f), viewport->WorkSize.x * 0.92f),
                                    std::min(dp(680.0f), viewport->WorkSize.y * 0.88f)),
                             ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("##audioSettings", nullptr, ImGuiWindowFlags_NoTitleBar))
        return;
    ImGui::PushFont(theme::boldFont(), ImGui::GetStyle().FontSizeBase * 1.1f);
    ImGui::TextUnformatted(L("Réglages audio", "Audio settings"));
    ImGui::PopFont();
    ImGui::Separator();
    // The device, then the card's inputs and outputs (the routing panel shows
    // a track's own).
    const auto footer = ImGui::GetFrameHeightWithSpacing() * 2.0f + ImGui::GetTextLineHeightWithSpacing();
    if (ImGui::BeginTabBar("##audioTabs")) {
        if (ImGui::BeginTabItem(ui::labelId(L("Périphérique", "Device"), "audioDevice").c_str())) {
            ImGui::BeginChild("##device", ImVec2(std::min(dp(620.0f), ImGui::GetContentRegionAvail().x), -footer));
            drawAudioDevice();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        const auto cardTab = selectSoundCardTab_ ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        selectSoundCardTab_ = false;
        if (ImGui::BeginTabItem(ui::labelId(L("Entrées et sorties de la carte", "Card inputs and outputs"), "audioCard").c_str(),
                                nullptr, cardTab)) {
            ImGui::BeginChild("##card", ImVec2(0.0f, -footer));
            ImGui::PushFont(nullptr, ui::currentFontBase() * 1.12f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(dp(8.0f), dp(6.0f)));
            soundCard_->draw();
            ImGui::PopStyleVar();
            ImGui::PopFont();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        // Asked for (the Options menu, the keyboard's right click): in front.
        const auto midiTab = std::exchange(selectMidiTabNow_, false) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        if (ImGui::BeginTabItem(ui::labelId(L("Entrées MIDI", "MIDI inputs"), "audioMidi").c_str(), nullptr, midiTab)) {
            ImGui::BeginChild("##midi", ImVec2(0.0f, -footer));
            drawMidiInputs();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::Spacing();
    ImGui::TextDisabled("%s", studio_.audioStatus().toRawUTF8());
    ImGui::Spacing();
    if (ui::primaryButton(L("Fermer", "Close"), ImVec2(dp(100.0f), 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)
        || state_.closeAudioSettings) {
        state_.closeAudioSettings = false;
        studio_.saveAudioSetup();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void AppUi::drawMidiInputs()
{
    const auto& p = theme::palette();
    auto& manager = studio_.engine().deviceManager();
    auto& keyboard = *computerKeyboard_;
    ui::sectionTitle(L("Claviers et contrôleurs MIDI", "MIDI keyboards and controllers"));
    if (midiDevices_.isEmpty())
        ImGui::TextColored(theme::vec(p.textFaint), "%s",
                           L("Aucun appareil MIDI branché (ils apparaissent ici dès qu'ils le sont).",
                             "No MIDI device plugged in (they show here as soon as they are)."));
    for (const auto& device : midiDevices_) {
        auto on = manager.isMidiInputDeviceEnabled(device.identifier);
        if (ImGui::Checkbox(ui::labelId(device.name.toRawUTF8(), device.identifier.toRawUTF8()).c_str(), &on)) {
            manager.setMidiInputDeviceEnabled(device.identifier, on);
            if (studio_.keepsSettings())
                studio_.saveAudioSetup();
        }
    }
    ImGui::TextColored(theme::vec(p.textDim), "%s",
                       L("Ils jouent la piste instrument choisie (ou les pistes armées).",
                         "They play the picked instrument track (or the armed tracks)."));
    ImGui::Spacing();
    ImGui::Spacing();

    ui::sectionTitle(L("Clavier de l'ordinateur", "Computer keyboard"));
    auto enabled = keyboard.enabled();
    if (ImGui::Checkbox(L("Jouer des notes avec le clavier de l'ordinateur (Ctrl+K)", "Play notes with the computer keyboard (Ctrl+K)"),
                        &enabled))
        keyboard.setEnabled(enabled);
    ImGui::BeginDisabled(!enabled);
    auto octave = keyboard.octave();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::vec(p.textDim), "%s", L("Octave", "Octave"));
    ImGui::SameLine(dp(110.0f));
    ImGui::SetNextItemWidth(dp(140.0f));
    if (ImGui::SliderInt("##keyboardOctave", &octave, -1, 8))
        keyboard.setOctave(octave);
    auto velocity = keyboard.velocity();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::vec(p.textDim), "%s", L("Vélocité", "Velocity"));
    ImGui::SameLine(dp(110.0f));
    ImGui::SetNextItemWidth(dp(140.0f));
    if (ImGui::SliderInt("##keyboardVelocity", &velocity, 1, 127))
        keyboard.setVelocity(velocity);

    // Its keys drawn as a piano: what each one plays, as this keyboard has it.
    ImGui::Spacing();
    const auto origin = ImGui::GetCursorScreenPos();
    auto* list = ImGui::GetWindowDrawList();
    const auto white = dp(40.0f);
    const auto height = dp(84.0f);
    const auto blackHeight = dp(50.0f);
    static constexpr int whiteOf[] = { 0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6 };
    const auto held = keyboard.heldNotes();
    const auto base = (keyboard.octave() + 1) * 12;
    const auto isHeld = [&held, base](const int semitone) { return std::find(held.begin(), held.end(), base + semitone) != held.end(); };
    const auto whiteIndex = [](const int semitone) { return (semitone / 12) * 7 + whiteOf[semitone % 12]; };
    for (const auto& key : keyboard.keys()) {
        if (key.semitone < 0 || whiteOf[key.semitone % 12] < 0)
            continue;
        const auto x = origin.x + static_cast<float>(whiteIndex(key.semitone)) * white;
        const ImVec2 min(x, origin.y);
        const ImVec2 max(x + white - dp(2.0f), origin.y + height);
        list->AddRectFilled(min, max, isHeld(key.semitone) ? p.accent : theme::mix(p.text, p.bg3, 0.12f), dp(3.0f));
        char name[16];
        const auto note = base + key.semitone;
        static const char* namesFr[] = { "Do", "Do#", "Ré", "Ré#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" };
        static const char* namesEn[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        std::snprintf(name, sizeof(name), "%s%d", (ui::french() ? namesFr : namesEn)[note % 12], note / 12 - 1);
        ui::centredText(list, ImVec2(min.x, max.y - dp(34.0f)), ImVec2(max.x, max.y - dp(18.0f)), p.bg0, key.label.c_str());
        ui::centredText(list, ImVec2(min.x, max.y - dp(18.0f)), ImVec2(max.x, max.y - dp(2.0f)), theme::mix(p.bg0, p.text, 0.4f), name);
    }
    for (const auto& key : keyboard.keys()) {
        if (key.semitone < 0 || whiteOf[key.semitone % 12] >= 0)
            continue;
        const auto x = origin.x + static_cast<float>(whiteIndex(key.semitone - 1) + 1) * white - white * 0.32f;
        const ImVec2 min(x, origin.y);
        const ImVec2 max(x + white * 0.62f, origin.y + blackHeight);
        list->AddRectFilled(min, max, isHeld(key.semitone) ? p.accent : p.bg0, dp(3.0f));
        list->AddRect(min, max, p.line, dp(3.0f));
        ui::centredText(list, ImVec2(min.x, max.y - dp(20.0f)), ImVec2(max.x, max.y - dp(4.0f)), p.text, key.label.c_str());
    }
    ImGui::Dummy(ImVec2(white * 11.0f, height + dp(6.0f)));
    std::string octaveKeys, velocityKeys;
    for (const auto& key : keyboard.keys()) {
        if (key.semitone == -1 || key.semitone == -2)
            octaveKeys += (octaveKeys.empty() ? "" : " / ") + key.label;
        if (key.semitone == -3 || key.semitone == -4)
            velocityKeys += (velocityKeys.empty() ? "" : " / ") + key.label;
    }
    ImGui::TextColored(theme::vec(p.textDim), "%s %s    %s %s", L("Octave − / + :", "Octave − / +:"), octaveKeys.c_str(),
                       L("Vélocité − / + :", "Velocity − / +:"), velocityKeys.c_str());
    ImGui::EndDisabled();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::vec(p.textFaint), "%s",
                       L("Il joue la piste instrument choisie, comme un clavier MIDI. Ses touches passent avant les raccourcis "
                         "d'une seule lettre (S, L…) tant qu'une piste instrument est choisie ; Ctrl et Alt gardent les leurs. "
                         "Dans le graphe et l'atelier, les lettres restent des raccourcis.",
                         "It plays the picked instrument track, as a MIDI keyboard does. Its keys come before the one-letter "
                         "shortcuts (S, L…) while an instrument track is picked; Ctrl and Alt keep theirs. In the graph and "
                         "the workshop, letters stay shortcuts."));
    ImGui::PopTextWrapPos();
}

juce::var AppUi::themes(const juce::var& request)
{
    if (request.hasProperty("create")) {
        const auto from = request.getProperty("from", themes_->currentKey()).toString();
        const auto key = themes_->createFrom(from, request.getProperty("create", juce::var()).toString());
        if (key.isEmpty())
            return juce::var("no theme \"" + from + "\" to make one from");
        (void) themes_->choose(key);
    }
    for (const auto* field : { "key", "name" })
        if (request.hasProperty(field) && !request.hasProperty("colours")) {
            const auto wanted = request.getProperty(field, juce::var()).toString();
            auto found = false;
            for (const auto& entry : themes_->themes())
                if (entry.key == wanted || entry.name == wanted)
                    found = themes_->choose(entry.key);
            if (!found)
                return juce::var("no theme \"" + wanted + "\"");
        }
    if (request.hasProperty("colours")) {
        const auto key = request.getProperty("key", themes_->currentKey()).toString();
        const theme::Theme* target = nullptr;
        for (const auto& entry : themes_->themes())
            if (entry.key == key)
                target = &entry;
        if (target == nullptr || target->builtIn)
            return juce::var("only a theme of one's own changes its colours");
        const auto palette = theme::paletteFromJson(request.getProperty("colours", juce::var()), target->palette);
        if (!themes_->save(key, target->name, palette))
            return juce::var("the theme could not be written");
        (void) themes_->choose(key);
    }
    auto* result = new juce::DynamicObject();
    result->setProperty("current", themes_->currentKey());
    juce::Array<juce::var> list;
    for (const auto& entry : themes_->themes()) {
        auto* item = new juce::DynamicObject();
        item->setProperty("key", entry.key);
        item->setProperty("name", entry.name);
        item->setProperty("builtIn", entry.builtIn);
        if (entry.key == themes_->currentKey())
            item->setProperty("colours", theme::paletteToJson(entry.palette));
        list.add(juce::var(item));
    }
    result->setProperty("themes", list);
    result->setProperty("folder", ThemesView::folder().getFullPathName());
    return juce::var(result);
}

juce::var AppUi::midiInputs(const juce::String& command, const juce::var& request)
{
    auto& keyboard = *computerKeyboard_;
    auto& manager = studio_.engine().deviceManager();
    auto* result = new juce::DynamicObject();
    if (command == "computer_keyboard") {
        if (request.hasProperty("enabled"))
            keyboard.setEnabled(static_cast<bool>(request.getProperty("enabled", true)));
        if (request.hasProperty("octave"))
            keyboard.setOctave(static_cast<int>(request.getProperty("octave", 4)));
        if (request.hasProperty("velocity"))
            keyboard.setVelocity(static_cast<int>(request.getProperty("velocity", 100)));
        result->setProperty("enabled", keyboard.enabled());
        result->setProperty("active", keyboard.active());
        result->setProperty("octave", keyboard.octave());
        result->setProperty("velocity", keyboard.velocity());
        juce::Array<juce::var> held;
        for (const auto note : keyboard.heldNotes())
            held.add(note);
        result->setProperty("held", held);
        juce::Array<juce::var> keys;
        for (const auto& key : keyboard.keys()) {
            auto* entry = new juce::DynamicObject();
            entry->setProperty("label", juce::String(key.label));
            entry->setProperty("semitone", key.semitone);
            keys.add(juce::var(entry));
        }
        result->setProperty("keys", keys);
        return juce::var(result);
    }
    const auto devices = juce::MidiInput::getAvailableDevices();
    if (request.hasProperty("device")) {
        const auto wanted = request.getProperty("device", juce::var()).toString();
        auto found = false;
        for (const auto& device : devices)
            if (device.identifier == wanted || device.name == wanted) {
                manager.setMidiInputDeviceEnabled(device.identifier, static_cast<bool>(request.getProperty("enabled", true)));
                if (studio_.keepsSettings())
                    studio_.saveAudioSetup();
                found = true;
            }
        if (!found)
            return juce::var("no MIDI device \"" + wanted + "\"");
    }
    juce::Array<juce::var> list;
    for (const auto& device : devices) {
        auto* entry = new juce::DynamicObject();
        entry->setProperty("name", device.name);
        entry->setProperty("identifier", device.identifier);
        entry->setProperty("enabled", manager.isMidiInputDeviceEnabled(device.identifier));
        list.add(juce::var(entry));
    }
    result->setProperty("devices", list);
    result->setProperty("computerKeyboard", keyboard.enabled());
    return juce::var(result);
}

void AppUi::drawAudioDevice()
{
    auto& manager = studio_.engine().deviceManager();
    const auto row = [](const char* label) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::vec(theme::palette().textDim), "%s", label);
        ImGui::SameLine(dp(130.0f));
        ImGui::SetNextItemWidth(-1.0f);
    };
    auto changed = juce::String();
    // Driver type.
    const auto& types = manager.getAvailableDeviceTypes();
    auto currentType = 0;
    std::vector<std::string> typeNames;
    for (auto index = 0; index < types.size(); ++index) {
        typeNames.push_back(types[index]->getTypeName().toStdString());
        if (types[index]->getTypeName() == manager.getCurrentAudioDeviceType())
            currentType = index;
    }
    std::vector<const char*> typeItems;
    for (const auto& name : typeNames)
        typeItems.push_back(name.c_str());
    row(L("Pilote", "Driver"));
    if (ImGui::Combo("##driver", &currentType, typeItems.data(), static_cast<int>(typeItems.size())))
        manager.setCurrentAudioDeviceType(types[currentType]->getTypeName(), true);
    auto* type = manager.getCurrentDeviceTypeObject();
    auto setup = manager.getAudioDeviceSetup();
    const auto deviceCombo = [&](const char* label, const char* id, const bool input, juce::String& name) {
        if (type == nullptr)
            return false;
        type->scanForDevices();
        const auto names = type->getDeviceNames(input);
        std::vector<std::string> items { L("(aucun)", "(none)") };
        auto current = 0;
        for (auto index = 0; index < names.size(); ++index) {
            items.push_back(names[index].toStdString());
            if (names[index] == name)
                current = index + 1;
        }
        std::vector<const char*> pointers;
        for (const auto& item : items)
            pointers.push_back(item.c_str());
        row(label);
        if (ImGui::Combo(id, &current, pointers.data(), static_cast<int>(pointers.size()))) {
            name = current == 0 ? juce::String() : names[current - 1];
            return true;
        }
        return false;
    };
    auto setupChanged = deviceCombo(L("Sortie", "Output"), "##output", false, setup.outputDeviceName);
    if (type != nullptr && type->hasSeparateInputsAndOutputs())
        setupChanged |= deviceCombo(L("Entrée", "Input"), "##input", true, setup.inputDeviceName);
    if (auto* device = manager.getCurrentAudioDevice()) {
        const auto rates = device->getAvailableSampleRates();
        std::vector<std::string> rateNames;
        auto currentRate = 0;
        for (auto index = 0; index < rates.size(); ++index) {
            rateNames.push_back(juce::String(juce::roundToInt(rates[index])).toStdString() + " Hz");
            if (std::abs(rates[index] - device->getCurrentSampleRate()) < 1.0)
                currentRate = index;
        }
        std::vector<const char*> rateItems;
        for (const auto& name : rateNames)
            rateItems.push_back(name.c_str());
        row(L("Fréquence", "Sample rate"));
        if (ImGui::Combo("##rate", &currentRate, rateItems.data(), static_cast<int>(rateItems.size()))) {
            setup.sampleRate = rates[currentRate];
            setupChanged = true;
        }
        const auto sizes = device->getAvailableBufferSizes();
        std::vector<std::string> sizeNames;
        auto currentSize = 0;
        for (auto index = 0; index < sizes.size(); ++index) {
            const auto ms = sizes[index] * 1000.0 / std::max(1.0, device->getCurrentSampleRate());
            sizeNames.push_back(juce::String(sizes[index]).toStdString() + " (" + juce::String(ms, 1).toStdString() + " ms)");
            if (sizes[index] == device->getCurrentBufferSizeSamples())
                currentSize = index;
        }
        std::vector<const char*> sizeItems;
        for (const auto& name : sizeNames)
            sizeItems.push_back(name.c_str());
        row(L("Tampon", "Buffer"));
        if (ImGui::Combo("##buffer", &currentSize, sizeItems.data(), static_cast<int>(sizeItems.size()))) {
            setup.bufferSize = sizes[currentSize];
            setupChanged = true;
        }
        row(L("Latence", "Latency"));
        ImGui::TextDisabled("%s %d / %s %d %s", L("entrée", "in"), device->getInputLatencyInSamples(), L("sortie", "out"),
                            device->getOutputLatencyInSamples(), L("échantillons", "samples"));
        if (device->hasControlPanel()) {
            ImGui::Dummy(ImVec2(dp(126.0f), 0.0f));
            ImGui::SameLine();
            if (ImGui::Button(L("Panneau du pilote…", "Driver control panel…")))
                device->showControlPanel();
        }
    } else {
        ImGui::TextColored(theme::vec(theme::palette().record), "%s", L("Aucun périphérique ouvert.", "No device open."));
    }
    if (setupChanged) {
        changed = manager.setAudioDeviceSetup(setup, true);
        studio_.saveAudioSetup();
        if (changed.isNotEmpty())
            notify(changed, true);
    }
}

void AppUi::drawShortcuts()
{
    if (showShortcuts_) {
        ImGui::OpenPopup("##shortcuts");
        showShortcuts_ = false;
    }
    if (!ImGui::BeginPopupModal("##shortcuts", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::PushFont(theme::boldFont(), ImGui::GetStyle().FontSizeBase * 1.1f);
    ImGui::TextUnformatted(L("Raccourcis", "Shortcuts"));
    ImGui::PopFont();
    ImGui::Separator();
    const char* rows[][2] = {
        { "Espace", "Lecture / pause" }, { "R", "Enregistrer" }, { "L", "Boucle" }, { "Origine", "Retour au début" },
        { "Ctrl+Z / Ctrl+Y", "Annuler / rétablir (dans le panneau actif)" }, { "Ctrl+S", "Enregistrer le projet" },
        { "F1 … F7", "Arrangement, lanceur, périphériques, graphe, mixeur, éditeur, atelier de périphériques" },
        { "F8", "Routage : formats des pistes, entrées et sorties, carte son" },
        { "Atelier", "Double-clic sur un périphérique fait d'un graphe : l'ouvrir ; Ctrl+G réunit les cartes choisies ; Échap remonte" },
        { "Arrangement", "Ctrl+molette zoom, Maj+molette défiler, S scinder, Ctrl+D dupliquer, Suppr" },
        { "Graphe", "Tab ou double-clic : chercher un nœud ; câble lâché dans le vide : nœuds compatibles ; F cadrer, A ranger, Alt+clic coupe un câble" },
        { "Piano roll", "Double-clic ajoute/supprime, clic droit efface, flèches déplacent, Q quantifie, Alt+molette hauteur" },
    };
    if (ImGui::BeginTable("##keys", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        for (const auto& line : rows) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(theme::vec(theme::palette().accent), "%s", line[0]);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(line[1]);
        }
        ImGui::EndTable();
    }
    if (ui::primaryButton("OK", ImVec2(dp(90.0f), 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void AppUi::drawCompanionWindow()
{
    auto& companion = studio_.companion();
    if (showCompanion_) {
        showCompanion_ = false;
        ImGui::OpenPopup("##companion");
        companionInfoAsked_ = false;
        companionAccepted_.clear();
        const auto python = companion.pythonCommand();
        python.copyToUTF8(companionPython_, sizeof(companionPython_));
        // An NVIDIA card makes the CUDA build worth its size.
        nvidiaGpu_.clear();
        juce::ChildProcess smi;
        if (smi.start(juce::StringArray { "nvidia-smi", "--query-gpu=name,memory.total", "--format=csv,noheader" },
                      juce::ChildProcess::wantStdOut)) {
            const auto output = smi.readAllProcessOutput().trim();
            if (smi.waitForProcessToFinish(3'000) && smi.getExitCode() == 0)
                nvidiaGpu_ = output.upToFirstOccurrenceOf("\n", false, false).trim();
        }
    }
    // On the main window, centred, never taller than it: its body scrolls,
    // OK stays in sight.
    const auto* main = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(std::min(dp(760.0f), main->WorkSize.x * 0.9f), std::min(dp(1000.0f), main->WorkSize.y * 0.88f)),
                             ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(main->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("##companion", nullptr, ImGuiWindowFlags_NoTitleBar))
        return;
    ImGui::BeginChild("##companionBody", ImVec2(0.0f, -(ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y)));
    const auto& p = theme::palette();
    const auto status = companion.status();
    if (!companionInfoAsked_ && !status.running) {
        companionInfoAsked_ = companion.start({ "info" });
    }
    ImGui::PushFont(theme::boldFont(), ImGui::GetStyle().FontSizeBase * 1.1f);
    ImGui::TextUnformatted(L("Compagnon Python", "Python companion"));
    ImGui::PopFont();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::vec(p.textDim), "%s",
                       L("Il analyse les sons à recréer (Piste › Recréer un son…). La lecture, elle, est du DSP pur : "
                         "rien de Python ne tourne pendant que l'instrument joue.",
                         "It analyses the sounds to recreate (Track › Recreate a sound…). Playing is pure DSP: nothing "
                         "of Python runs while the instrument plays."));
    ImGui::PopTextWrapPos();
    ImGui::Separator();
    const auto row = [&p](const char* label) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::vec(p.textDim), "%s", label);
        ImGui::SameLine(dp(150.0f));
    };
    row(L("Interprète", "Interpreter"));
    ImGui::SetNextItemWidth(dp(300.0f));
    ImGui::InputText("##python", companionPython_, sizeof(companionPython_));
    ImGui::SameLine();
    if (ImGui::Button(L("Utiliser", "Use"))) {
        companion.setPythonCommand(juce::String::fromUTF8(companionPython_));
        companionInfoAsked_ = false;
    }
    ui::tooltip(L("Vide : son propre environnement s'il existe, sinon « python » du PATH.",
                  "Empty: its own environment when made, else \"python\" from the PATH."));
    // What the last check said.
    const auto& job = studio_.companionJob();
    const auto info = job && job->command == "info" && !job->failed ? job->result : juce::var();
    const auto setupInfo = job && job->command == "setup" && !job->failed ? job->result.getProperty("info", {}) : juce::var();
    const auto shown = info.isObject() ? info : setupInfo;
    row("Python");
    if (shown.isObject())
        ImGui::TextUnformatted((shown.getProperty("python", {}).toString() + "   " + shown.getProperty("executable", {}).toString()).toRawUTF8());
    else if (job && job->failed && job->command == "info")
        ImGui::TextColored(theme::vec(p.record), "%s", (ui::Ls("introuvable : ", "not found: ") + job->error).toRawUTF8());
    else
        ImGui::TextDisabled("%s", L("vérification…", "checking…"));
    const auto field = [&](const char* label, const char* key, const char* missing) {
        row(label);
        const auto value = shown.isObject() ? shown.getProperty(key, {}) : juce::var();
        if (value.isVoid() || value.toString().isEmpty() || value.toString() == "null")
            ImGui::TextColored(theme::vec(p.textFaint), "%s", missing);
        else
            ImGui::TextUnformatted(value.toString().toRawUTF8());
    };
    field("numpy", "numpy", L("absent (nécessaire à l'analyse)", "missing (the analysis needs it)"));
    field("PyTorch", "torch", L("absent (pour l'affinage, facultatif)", "missing (for the refinement, optional)"));
    row("CREPE");
    if (shown.isObject() && static_cast<bool>(shown.getProperty("crepe", false)))
        ImGui::TextColored(theme::vec(p.play), "%s", L("oui", "yes"));
    else
        ImGui::TextColored(theme::vec(p.textFaint), "%s", L("absent (hauteur des longs enregistrements, facultatif)",
                                                           "missing (pitch of long recordings, optional)"));
    row("CUDA");
    if (shown.isObject() && static_cast<bool>(shown.getProperty("cuda", false)))
        ImGui::TextColored(theme::vec(p.play), "%s", (ui::Ls("oui — ", "yes — ") + shown.getProperty("gpu", {}).toString()).toRawUTF8());
    else
        ImGui::TextColored(theme::vec(p.textFaint), "%s", L("non", "no"));
    row(L("Carte graphique", "Graphics card"));
    ImGui::TextUnformatted(nvidiaGpu_.isNotEmpty() ? nvidiaGpu_.toRawUTF8() : L("aucune carte NVIDIA détectée", "no NVIDIA card found"));
    ImGui::Separator();

    // Its own environment and what can go in it, each with its licences:
    // nothing is downloaded but on a click, and only into its folder.
    const auto environment = Companion::environmentFolder();
    ImGui::TextColored(theme::vec(p.textDim), "%s",
                       (ui::Ls("Son environnement : ", "Its environment: ") + environment.getFullPathName()).toRawUTF8());
    ImGui::SameLine();
    if (ImGui::SmallButton(L("Changer…", "Change…")))
        state_.openLocations = true;
    ui::tooltip(L("Options › Emplacements : tout ce qui est installé ici (et ses fichiers temporaires) reste dans ce dossier.",
                  "Options › Locations: everything installed here (and its temporary files) stays in this folder."));
    const auto catalogue = Companion::componentCatalogue();
    const auto french = ui::french();
    const auto text = [french](const juce::var& value) {
        return value.isObject() ? value.getProperty(french ? "fr" : "en", {}).toString() : value.toString();
    };
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + dp(620.0f));
    ImGui::TextColored(theme::vec(p.textFaint), "%s", text(catalogue.getProperty("note", {})).toRawUTF8());
    ImGui::PopTextWrapPos();
    const auto hasTorch = Companion::componentInstalled("torch-cpu", shown) || Companion::componentInstalled("torch-cuda", shown);
    const auto listed = catalogue.getProperty("components", {});
    if (const auto* components = listed.getArray()) {
        for (const auto& item : *components) {
            const auto id = item.getProperty("id", {}).toString();
            ImGui::PushID(id.toRawUTF8());
            ImGui::Separator();
            ImGui::PushFont(theme::boldFont(), 0.0f);
            ImGui::TextUnformatted(text(item.getProperty("title", {})).toRawUTF8());
            ImGui::PopFont();
            ImGui::SameLine();
            if (Companion::componentInstalled(id, shown))
                ImGui::TextColored(theme::vec(p.play), "%s", L("installé", "installed"));
            else
                ImGui::TextColored(theme::vec(p.textFaint), "%s", L("absent", "missing"));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + dp(620.0f));
            ImGui::TextColored(theme::vec(p.textDim), "%s", text(item.getProperty("description", {})).toRawUTF8());
            juce::StringArray sources;
            if (const auto* list = item.getProperty("sources", {}).getArray())
                for (const auto& source : *list)
                    sources.add(source.toString());
            ImGui::TextColored(theme::vec(p.textFaint), "%s",
                               (ui::Ls("Taille : ", "Size: ") + text(item.getProperty("size", {})) + ui::Ls("  ·  depuis ", "  ·  from ")
                                + sources.joinIntoString(", "))
                                   .toRawUTF8());
            ImGui::PopTextWrapPos();
            // Its licences: every package it brings, each with its text.
            const auto* packages = item.getProperty("packages", {}).getArray();
            const auto count = packages != nullptr ? packages->size() : 0;
            if (ImGui::TreeNode((ui::Ls("Licences (", "Licences (") + juce::String(count) + ")").toRawUTF8())) {
                for (int index = 0; index < count; ++index) {
                    const auto& package = packages->getReference(index);
                    ImGui::PushID(index);
                    ImGui::TextUnformatted((package.getProperty("name", {}).toString() + "  \xe2\x80\x94  "
                                            + package.getProperty("licence", {}).toString())
                                               .toRawUTF8());
                    ImGui::SameLine();
                    const auto url = package.getProperty("url", {}).toString();
                    if (ImGui::SmallButton(L("Lire", "Read")))
                        (void) juce::URL(url).launchInDefaultBrowser();
                    ui::tooltip(url.toRawUTF8());
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
            // A licence that asks to be accepted: ticked by the person, never kept.
            const auto asks = static_cast<bool>(item.getProperty("requiresAcceptance", false));
            if (asks) {
                auto ticked = companionAccepted_.contains(id);
                if (ImGui::Checkbox(L("J'ai lu ces licences et je les accepte", "I have read these licences and accept them"), &ticked)) {
                    if (ticked)
                        companionAccepted_.addIfNotAlreadyThere(id);
                    else
                        companionAccepted_.removeString(id);
                }
            }
            const auto needsGpu = static_cast<bool>(item.getProperty("gpu", false)) && nvidiaGpu_.isEmpty();
            const auto needsTorch = item.getProperty("requires", {}).toString() == "torch" && !hasTorch;
            ImGui::BeginDisabled(status.running || needsGpu || needsTorch || (asks && !companionAccepted_.contains(id)));
            if (ImGui::Button(Companion::componentInstalled(id, shown) ? L("Réinstaller", "Reinstall") : L("Installer", "Install"))) {
                juce::String error;
                if (!companion.install(id, companionAccepted_.contains(id), error))
                    notify(error, true);
            }
            ImGui::EndDisabled();
            if (needsGpu)
                ui::tooltip(L("Aucune carte NVIDIA détectée.", "No NVIDIA card found."));
            else if (needsTorch)
                ui::tooltip(L("PyTorch d'abord (processeur ou carte graphique).", "PyTorch first (processor or graphics card)."));
            ImGui::PopID();
        }
    }
    ImGui::Separator();
    ImGui::BeginDisabled(status.running);
    if (ImGui::Button(L("Vérifier", "Check")))
        companionInfoAsked_ = false;
    ImGui::EndDisabled();
    if (ImGui::SmallButton(L("Pas de Python ? python.org", "No Python? python.org")))
        (void) juce::URL("https://www.python.org/downloads/windows/").launchInDefaultBrowser();
    ui::tooltip(L("Installez Python 3.10 ou plus récent, puis « Installer » sur la base.", "Install Python 3.10 or newer, then \"Install\" the base."));

    // What it is doing.
    if (status.running && status.command != "info") {
        ImGui::Separator();
        ImGui::TextColored(theme::vec(p.accent), "%s %d %%", status.command.toRawUTF8(),
                           static_cast<int>(std::round(status.progress * 100.0)));
        ImGui::SameLine();
        if (ImGui::SmallButton(L("Arrêter", "Stop")))
            companion.cancel();
        host_.wake(2);
    }
    const auto& log = status.running ? status.log : (job ? job->log : juce::StringArray());
    if (!log.isEmpty() && (status.running || (job && job->command == "setup"))) {
        ImGui::BeginChild("##companionLog", ImVec2(dp(540.0f), dp(120.0f)), ImGuiChildFlags_Borders);
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.8f);
        for (const auto& line : log)
            ImGui::TextUnformatted(line.toRawUTF8());
        ImGui::SetScrollHereY(1.0f);
        ImGui::PopFont();
        ImGui::EndChild();
    }
    if (job && job->failed && job->command == "setup")
        ImGui::TextColored(theme::vec(p.record), "%s", job->error.toRawUTF8());
    if (job && !job->failed && job->command == "setup")
        ImGui::TextColored(theme::vec(p.play), "%s", L("Environnement prêt.", "Environment ready."));
    ImGui::EndChild();
    ImGui::Separator();
    if (ui::primaryButton("OK", ImVec2(dp(90.0f), 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void AppUi::drawDialogs()
{
    drawAudioSettings();
    transportSync_->draw(showTransportSync_);
    controllers_->draw(showControllers_);
    drawCompanionWindow();
    locations_->draw();
    // A clip's menu asked for a processing on it: the window that applies one.
    if (state_.processingRequest) {
        const auto clip = *state_.processingRequest;
        state_.processingRequest.reset();
        processing_->openApply(clip);
    }
    processing_->drawApply();
    banks_->draw();
    themes_->draw();
    semanticView_->draw();
    library_->draw();
    sampler_->drawEditor();
    prism_->drawEditor();
    kick_->drawEditor();
    synth_->drawEditor();
    batterie_->drawEditor();
    drawShortcuts();
    drawUnsavedPrompt();
    drawFoundryPrompt();
    // A session a crash left behind: offered once, at the start.
    if (!recoveryChecked_) {
        recoveryChecked_ = true;
        recoveryWhen_ = studio_.pendingRecoveryDescription();
        if (recoveryWhen_.isNotEmpty())
            ImGui::OpenPopup("##recovery");
    }
    if (ImGui::BeginPopupModal("##recovery", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::PushFont(theme::boldFont(), ImGui::GetStyle().FontSizeBase * 1.1f);
        ImGui::TextUnformatted(L("Session non enregistrée retrouvée", "Unsaved session found"));
        ImGui::PopFont();
        ImGui::TextDisabled("%s %s", L("Copie automatique du", "Automatic copy from"), recoveryWhen_.toRawUTF8());
        ImGui::Spacing();
        if (ui::primaryButton(L("Restaurer", "Restore"), ImVec2(dp(110.0f), 0.0f))) {
            const auto result = studio_.restoreRecovery();
            resetUiState();
            waveforms_.clear();
            notify(result.wasOk() ? ui::Ls("Session restaurée", "Session restored") : result.getErrorMessage(), result.failed());
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(L("Ignorer", "Discard"), ImVec2(dp(110.0f), 0.0f))) {
            studio_.discardRecovery();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    drawLayoutNamePrompt();
    if (showAbout_) {
        ImGui::OpenPopup("##about");
        showAbout_ = false;
    }
    if (ImGui::BeginPopupModal("##about", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        if (const auto texture = logo(dp(96.0f) * ImGui::GetIO().DisplayFramebufferScale.x)) {
            ImGui::Image(ImTextureRef(static_cast<ImTextureID>(texture)), ImVec2(dp(96.0f), dp(96.0f)));
            ImGui::SameLine(0.0f, dp(14.0f));
        }
        ImGui::BeginGroup();
        ImGui::PushFont(theme::boldFont(), ImGui::GetStyle().FontSizeBase * 1.4f);
        ImGui::TextUnformatted("CyDAW 2");
        ImGui::PopFont();
        ImGui::TextDisabled("%s", L("Nouvelle interface : Dear ImGui sur Direct3D 11.", "New interface: Dear ImGui on Direct3D 11."));
        ImGui::TextDisabled("%s", host_.rendererDescription().toRawUTF8());
        ImGui::TextDisabled("FFmpeg : LGPL-3.0-or-later — Licenses/FFmpeg");
        ImGui::Spacing();
        if (ui::primaryButton("OK", ImVec2(dp(90.0f), 0.0f)))
            ImGui::CloseCurrentPopup();
        ImGui::EndGroup();
        ImGui::EndPopup();
    }
    if (showWarnings_) {
        ImGui::OpenPopup("##messages");
        showWarnings_ = false;
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(dp(360.0f), 0.0f), ImVec2(dp(760.0f), dp(520.0f)));
    if (ImGui::BeginPopupModal("##messages", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ImGui::PushFont(theme::boldFont(), ImGui::GetStyle().FontSizeBase * 1.1f);
        ImGui::TextUnformatted(dialogTitle_.toRawUTF8());
        ImGui::PopFont();
        ImGui::Separator();
        ImGui::BeginChild("##lines", ImVec2(dp(640.0f), std::min(dp(420.0f), dp(18.0f) * static_cast<float>(dialogLines_.size() + 1))),
                          ImGuiChildFlags_None);
        for (const auto& line : dialogLines_)
            ImGui::TextWrapped("%s", line.toRawUTF8());
        ImGui::EndChild();
        if (ui::primaryButton("OK", ImVec2(dp(90.0f), 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Enter))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ---- Unsaved changes -----------------------------------------------------------------------------

void AppUi::whenSaved(std::function<void()> action)
{
    if (!studio_.isDirty()) {
        action();
        return;
    }
    pendingAction_ = std::move(action);
    unsavedPromptRequested_ = true;
    host_.wake();
}

void AppUi::askBeforeQuit()
{
    // The workshop's device first, the Foundry's files, then the project.
    const auto project = [this] {
        whenSaved([this] {
            if (onQuitNow)
                onQuitNow();
        });
    };
    const auto files = [this, project] {
        if (foundry_->unsaved().isEmpty()&&cinema_->unsaved().isEmpty()) {
            project();
            return;
        }
        foundryAction_ = project;
        foundryPromptRequested_ = true;
        host_.wake();
    };
    const auto workspaces=[this,files]{if(!workshop_->askBeforeQuit(files))files();};
    if(!JsfxView::askBeforeQuit(workspaces))workspaces();
}

void AppUi::drawFoundryPrompt()
{
    if (foundryPromptRequested_) {
        foundryPromptRequested_ = false;
        ImGui::OpenPopup("##foundryUnsaved");
    }
    if (!ImGui::BeginPopupModal("##foundryUnsaved", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
        return;
    ImGui::PushFont(theme::boldFont(), ImGui::GetStyle().FontSizeBase * 1.1f);
    ImGui::TextUnformatted(L("Des sessions de Fonderie ou de Cinéma ne sont pas enregistrées", "Some Foundry or Cinema sessions are not saved"));
    ImGui::PopFont();
    for (const auto& name : foundry_->unsaved())
        ImGui::BulletText("%s", name.toRawUTF8());
    for(const auto& name:cinema_->unsaved())ImGui::BulletText("Cinéma : %s",name.toRawUTF8());
    ImGui::Spacing();
    const ImVec2 button(dp(170.0f), 0.0f);
    if (ui::primaryButton(L("Revenir aux sessions", "Back to sessions"), button) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        foundryAction_ = {};
        state_.mode = cinema_->unsaved().isEmpty()?AppMode::foundry:AppMode::cinema;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(L("Quitter sans les enregistrer", "Quit without saving them"), button)) {
        auto action = std::move(foundryAction_);
        foundryAction_ = {};
        ImGui::CloseCurrentPopup();
        if (action)
            action();
    }
    ImGui::EndPopup();
}

void AppUi::drawUnsavedPrompt()
{
    if (unsavedPromptRequested_) {
        unsavedPromptRequested_ = false;
        ImGui::OpenPopup("##unsaved");
    }
    if (!ImGui::BeginPopupModal("##unsaved", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
        return;
    ImGui::PushFont(theme::boldFont(), ImGui::GetStyle().FontSizeBase * 1.1f);
    ImGui::Text("%s « %s » ?", L("Enregistrer les modifications de", "Save the changes to"), studio_.projectName().toRawUTF8());
    ImGui::PopFont();
    ImGui::TextDisabled("%s", L("Sinon, elles seront perdues.", "Otherwise they are lost."));
    ImGui::Spacing();
    const ImVec2 button(dp(130.0f), 0.0f);
    if (ui::primaryButton(L("Enregistrer", "Save"), button) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
        auto action = std::move(pendingAction_);
        pendingAction_ = {};
        ImGui::CloseCurrentPopup();
        if (studio_.projectFile() == juce::File {}) {
            chooseSaveAsThen(std::move(action));
        } else {
            const auto result = studio_.saveProject(studio_.projectFile());
            notify(result.wasOk() ? ui::Ls("Enregistré : ", "Saved: ") + studio_.projectFile().getFileName()
                                  : result.getErrorMessage(),
                   result.failed());
            if (result.wasOk() && action)
                action();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(L("Ne pas enregistrer", "Don't save"), button)) {
        auto action = std::move(pendingAction_);
        pendingAction_ = {};
        ImGui::CloseCurrentPopup();
        studio_.discardChanges();
        if (action)
            action();
    }
    ImGui::SameLine();
    if (ImGui::Button(L("Annuler", "Cancel"), button) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        pendingAction_ = {};
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// ---- Control port --------------------------------------------------------------------------------

void AppUi::showView(const int view)
{
    auto panel = Panel::arrangement;
    switch (view) {
    case 2:
    case 6: panel = Panel::editor; break;
    case 3: panel = Panel::devices; break;
    case 4: panel = Panel::launcher; break;
    case 7: panel = Panel::graph; break;
    case 8:
    case 11: panel = Panel::routing; break;
    case 9: panel = Panel::mixer; break;
    case 10: panel = Panel::browser; break;
    default: break;
    }
    state_.bringToFront = panel;
    focusFrames_ = 0;
    currentView_ = view;
    host_.wake();
}

juce::String AppUi::injectInput(const juce::var& events)
{
    const auto* list = events.getArray();
    if (list == nullptr)
        return "events must be a list";
    if (!host_.isReady())
        return "the window is not ready";
    for (const auto& event : *list) {
        const auto type = event.getProperty("type", juce::var()).toString();
        if (type != "move" && type != "down" && type != "up" && type != "wheel" && type != "wait" && type != "release"
            && type != "text" && type != "key" && type != "file_drop")
            return "unknown event type \"" + type + "\"";
    }
    for (const auto& event : *list) {
        const auto type = event.getProperty("type", juce::var()).toString();
        if(type=="file_drop") {
            host_.renderNow();
            juce::StringArray files;
            if(const auto* paths=event.getProperty("files",{}).getArray())for(const auto& path:*paths)files.add(path.toString());
            if(files.isEmpty())return "file_drop requires files";
            const auto* viewport=ImGui::GetMainViewport();
            filesDropped(files,viewport->Pos.x+static_cast<float>(static_cast<double>(event.getProperty("x",0))),viewport->Pos.y+static_cast<float>(static_cast<double>(event.getProperty("y",0))));
            host_.renderNow();continue;
        }
        if (type == "release") {
            scriptedMouse_ = false;
            scriptedShift_ = false;
            ImGui::GetIO().AddKeyEvent(ImGuiMod_Shift, false);
            host_.renderNow();
            continue;
        }
        scriptedMouse_ = true;
        scriptedEvent_ = event;
        const auto frames = type == "wait" ? juce::jlimit(1, 600, static_cast<int>(event.getProperty("frames", 1))) : 1;
        for (auto frame = 0; frame < frames; ++frame)
            host_.renderNow();
    }
    scriptedEvent_ = juce::var();
    return {};
}

void AppUi::applyScriptedInput()
{
    // Knobs follow the scripted pointer's moves rather than the system's.
    ui::setScriptedPointer(scriptedMouse_);
    if (!scriptedMouse_)
        return;
    auto& io = ImGui::GetIO();
    const auto event = std::exchange(scriptedEvent_, juce::var());
    if (const auto* object = event.getDynamicObject()) {
        if (object->hasProperty("x") && object->hasProperty("y")) {
            scriptedX_ = static_cast<float>(static_cast<double>(object->getProperty("x")));
            scriptedY_ = static_cast<float>(static_cast<double>(object->getProperty("y")));
        }
        if (object->hasProperty("shift") && static_cast<bool>(object->getProperty("shift")) != scriptedShift_) {
            scriptedShift_ = static_cast<bool>(object->getProperty("shift"));
            io.AddKeyEvent(ImGuiMod_Shift, scriptedShift_);
        }
    }
    // With viewports, ImGui's coordinates are the screen's, and the window
    // under the pointer is the system's: the main one, for the scripted
    // pointer, wherever the real one is.
    const auto* viewport = ImGui::GetMainViewport();
    io.AddMousePosEvent(viewport->Pos.x + scriptedX_, viewport->Pos.y + scriptedY_);
    io.AddMouseViewportEvent(viewport->ID);
    const auto type = event.getProperty("type", juce::var()).toString();
    const auto button = juce::jlimit(0, 2, static_cast<int>(event.getProperty("button", 0)));
    if (type == "down")
        io.AddMouseButtonEvent(button, true);
    else if (type == "up")
        io.AddMouseButtonEvent(button, false);
    else if (type == "wheel")
        io.AddMouseWheelEvent(static_cast<float>(static_cast<double>(event.getProperty("dx", 0.0))),
                              static_cast<float>(static_cast<double>(event.getProperty("dy", 0.0))));
    // Typing: {"type":"text","text":"1.5k"}; a key pressed and let go:
    // {"type":"key","key":"enter"} (enter, escape, tab, backspace, delete,
    // space, the arrows up, down, left, right, a letter or a digit; ctrl,
    // shift, alt down with "down": true, up with false).
    else if (type == "text")
        io.AddInputCharactersUTF8(event.getProperty("text", juce::var()).toString().toRawUTF8());
    else if (type == "key") {
        const auto name = event.getProperty("key", juce::var()).toString().toLowerCase();
        const auto key = name == "enter"       ? ImGuiKey_Enter
                         : name == "escape"    ? ImGuiKey_Escape
                         : name == "tab"       ? ImGuiKey_Tab
                         : name == "backspace" ? ImGuiKey_Backspace
                         : name == "delete"    ? ImGuiKey_Delete
                         : name == "space"     ? ImGuiKey_Space
                         : name == "up"        ? ImGuiKey_UpArrow
                         : name == "down"      ? ImGuiKey_DownArrow
                         : name == "left"      ? ImGuiKey_LeftArrow
                         : name == "right"     ? ImGuiKey_RightArrow
                         : name == "ctrl"      ? ImGuiMod_Ctrl
                         : name == "shift"     ? ImGuiMod_Shift
                         : name == "alt"       ? ImGuiMod_Alt
                         : name.length() == 1 && name[0] >= 'a' && name[0] <= 'z'
                             ? static_cast<ImGuiKey>(ImGuiKey_A + (name[0] - 'a'))
                         : name.length() == 1 && name[0] >= '0' && name[0] <= '9'
                             ? static_cast<ImGuiKey>(ImGuiKey_0 + (name[0] - '0'))
                         : name == ";"         ? ImGuiKey_Semicolon
                                               : ImGuiKey_None;
        if (key != ImGuiKey_None) {
            if (event.getDynamicObject() != nullptr && event.getDynamicObject()->hasProperty("down"))
                io.AddKeyEvent(key, static_cast<bool>(event.getProperty("down", true)));
            else {
                io.AddKeyEvent(key, true);
                pendingKeyUp_ = static_cast<int>(key);
            }
        }
    } else if (pendingKeyUp_ != 0) {
        io.AddKeyEvent(static_cast<ImGuiKey>(pendingKeyUp_), false);
        pendingKeyUp_ = 0;
    }
}

bool AppUi::showDeviceEditor(const std::uint64_t nodeId, const int pane, const bool detached)
{
    if (!state_.selectedTrack)
        return false;
    if (nodeId == 0) {
        state_.expandedPanes.clear();
        return true;
    }
    state_.openExpandedPane(ExpandedPane { *state_.selectedTrack, nodeId, std::max(-1, pane) });
    // Out of the studio's window, or back into it.
    ui::setDetached(DeviceEditorView::windowId(*state_.selectedTrack, nodeId).c_str(), detached);
    host_.wake();
    return true;
}

bool AppUi::snapshot(const juce::File& png)
{
    if (!host_.isReady())
        return false;
    // A panel brought to the front takes a few frames to settle.
    for (auto frame = 0; frame < 3; ++frame)
        host_.renderNow();
    // The frame is written while it renders; the host reports it later.
    png.deleteFile();
    host_.captureNextFrame(png);
    host_.renderNow();
    return png.existsAsFile();
}

void AppUi::sessionReplaced()
{
    const auto zoom = state_.pixelsPerSecond;
    resetUiState();
    state_.pixelsPerSecond = zoom;
    waveforms_.clear();
    const auto tracks = studio_.tracks();
    if (!tracks.empty())
        state_.selectedTrack = tracks.front();
    host_.wake();
}

void AppUi::tracksChanged()
{
    state_.selectedClips.clear();
    state_.editedClip.reset();
    if (state_.selectedTrack
        && ((!state_.selectedTrack->master && !studio_.isValid(*state_.selectedTrack))
            || (state_.selectedTrack->isBus() && !studio_.hardwareBus(state_.selectedTrack->bus))))
        state_.selectedTrack.reset();
    host_.wake();
}

void AppUi::requestQuit()
{
    // After the answer has gone out.
    juce::Timer::callAfterDelay(300, [] {
        if (juce::JUCEApplicationBase::getInstance() != nullptr)
            juce::JUCEApplicationBase::quit();
    });
}

void AppUi::selectSample(const juce::File& file)
{
    state_.bringToFront = Panel::browser;
    focusFrames_ = 0;
    currentView_ = 10;
    browser_->selectSample(file);
}

juce::var AppUi::library(const juce::String& command, const juce::var& request)
{
    static const char* facets[] = { "category", "group", "source", "type", "key", "tempo", "length" };
    static const std::pair<const char*, int> columns[] = {
        { "favourite", SampleCatalog::colFavourite }, { "name", SampleCatalog::colName },     { "category", SampleCatalog::colCategory },
        { "group", SampleCatalog::colGroup },         { "source", SampleCatalog::colSource }, { "type", SampleCatalog::colType },
        { "length", SampleCatalog::colLength },       { "bpm", SampleCatalog::colBpm },       { "key", SampleCatalog::colKey },
        { "channels", SampleCatalog::colChannels },   { "rate", SampleCatalog::colRate },     { "score", SampleCatalog::colScore },
    };
    auto* result = new juce::DynamicObject();
    juce::var answer(result);
    if (command == "library_perf") {
        // The catalog built over as many sounds as asked, then queried the
        // ways the window does.
        const auto count = juce::jlimit(1, 1'000'000, static_cast<int>(request.getProperty("items", 50'000)));
        const auto started = juce::Time::getMillisecondCounterHiRes();
        const auto data = catalog_->synthetic(count);
        result->setProperty("items", static_cast<int>(data->items.size()));
        result->setProperty("buildMs", data->buildMs);
        result->setProperty("syntheticMs", juce::Time::getMillisecondCounterHiRes() - started);
        auto* queries = new juce::DynamicObject();
        const auto time = [&](const char* name, const SampleCatalog::Query& query) {
            // The slowest of three.
            auto slowest = 0.0;
            auto rows = 0;
            for (auto run = 0; run < 3; ++run) {
                const auto answered = SampleCatalog::run(*data, query);
                slowest = std::max(slowest, answered.ms);
                rows = answered.matched;
            }
            auto* entry = new juce::DynamicObject();
            entry->setProperty("ms", slowest);
            entry->setProperty("rows", rows);
            queries->setProperty(name, juce::var(entry));
        };
        SampleCatalog::Query all;
        time("all", all);
        SampleCatalog::Query words;
        words.text = "kick 1";
        time("words", words);
        SampleCatalog::Query facet;
        facet.selected[SampleCatalog::category].assign(data->values[SampleCatalog::category].size(), 0);
        if (!facet.selected[SampleCatalog::category].empty())
            facet.selected[SampleCatalog::category][0] = 1;
        time("facet", facet);
        auto both = facet;
        both.text = "a";
        both.selected[SampleCatalog::type].assign(data->values[SampleCatalog::type].size(), 1);
        time("facetsAndWords", both);
        SampleCatalog::Query byLength;
        byLength.sortColumn = SampleCatalog::colLength;
        byLength.ascending = false;
        time("sortLength", byLength);
        SampleCatalog::Query byCategory;
        byCategory.sortColumn = SampleCatalog::colCategory;
        time("sortCategory", byCategory);
        result->setProperty("queries", juce::var(queries));
        return answer;
    }
    // library_window
    if (request.hasProperty("show")) {
        state_.showLibrary = static_cast<bool>(request.getProperty("show", true));
        state_.libraryFocus = state_.showLibrary;
    }
    if (request.hasProperty("tab")) {
        const auto tab = request.getProperty("tab", {}).toString();
        state_.libraryTab = tab == "instruments" ? 1 : tab == "effects" ? 2 : 0;
    }
    if (request.hasProperty("detached"))
        ui::setDetached("###library", static_cast<bool>(request.getProperty("detached", false)));
    if (request.hasProperty("query") || request.hasProperty("filters") || request.hasProperty("sort")) {
        std::vector<std::pair<int, juce::StringArray>> chosen;
        if (const auto* filters = request.getProperty("filters", {}).getDynamicObject())
            for (auto facet = 0; facet < SampleCatalog::facetCount; ++facet)
                if (const auto* keys = filters->getProperty(facets[facet]).getArray()) {
                    juce::StringArray list;
                    for (const auto& key : *keys)
                        list.add(key.toString());
                    chosen.emplace_back(facet, list);
                }
        auto sort = static_cast<int>(SampleCatalog::colName);
        const auto sortName = request.getProperty("sort", "name").toString();
        for (const auto& [name, column] : columns)
            if (sortName == name)
                sort = column;
        library_->setSampleQuery(request.getProperty("query", {}).toString(), static_cast<bool>(request.getProperty("semantic", false)),
                                 chosen, sort, static_cast<bool>(request.getProperty("ascending", sort == SampleCatalog::colName)));
    }
    const auto probe = library_->probe();
    result->setProperty("shown", probe.shown);
    result->setProperty("tab", probe.tab == 1 ? "instruments" : probe.tab == 2 ? "effects" : "samples");
    result->setProperty("detached", ui::isDetached("###library"));
    result->setProperty("rows", probe.rows);
    result->setProperty("total", probe.total);
    result->setProperty("queryMs", probe.queryMs);
    result->setProperty("buildMs", probe.buildMs);
    result->setProperty("extractMs", probe.extractMs);
    result->setProperty("frameMs", probe.frameMs);
    auto* facetCounts = new juce::DynamicObject();
    for (const auto& [name, values] : probe.facets) {
        auto* counts = new juce::DynamicObject();
        for (const auto& [key, count] : values)
            counts->setProperty(key, count);
        facetCounts->setProperty(name, juce::var(counts));
    }
    result->setProperty("facets", juce::var(facetCounts));
    juce::Array<juce::var> names;
    for (const auto& name : probe.names)
        names.add(name);
    result->setProperty("names", names);
    return answer;
}

void AppUi::browseSamples(const juce::String& query, const bool semantic, const juce::String& family, const juce::String& category,
                          const juce::String& source)
{
    state_.bringToFront = Panel::browser;
    focusFrames_ = 0;
    currentView_ = 10;
    browser_->browse(query, semantic, family, category, source);
}

void AppUi::handleShortcuts()
{
    auto& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Q, false) && onQuit)
        onQuit();
    // In the Foundry, the files are its sound files.
    const auto foundry = state_.mode == AppMode::foundry||state_.mode==AppMode::cinema;
    auto* sessionView=state_.mode==AppMode::cinema?cinema_.get():foundry_.get();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        if (foundry)
            sessionView->save(io.KeyShift);
        else if (io.KeyShift)
            chooseSaveAs();
        else
            save();
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
        if (foundry)
            sessionView->chooseOpen();
        else
            whenSaved([this] { chooseOpenProject(); });
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) {
        if (foundry)
            sessionView->newDocument();
        else
            whenSaved([this] { newProject(); });
    }
    if (io.WantTextInput)
        return;
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_J, false))
        toggleBottomPanel();
    if (ImGui::IsKeyPressed(ImGuiKey_F10, false))
        state_.mode = state_.mode == AppMode::dj ? AppMode::studio : AppMode::dj;
    if (ImGui::IsKeyPressed(ImGuiKey_F11, false))
        state_.mode = state_.mode == AppMode::foundry ? AppMode::studio : AppMode::foundry;
    // The DJ mix and the Foundry have their own keys (the Foundry's view
    // reads them); the studio's transport keys stay out of them.
    const auto ownKeys = state_.mode == AppMode::dj || foundry;
    // Ctrl+G: the picked tracks gathered in a group.
    if (!ownKeys && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_G, false) && state_.selectedTrack && !state_.selectedTrack->master) {
        const auto picked = state_.pickedTracks.empty() ? std::vector<TrackRef> { *state_.selectedTrack } : state_.pickedTracks;
        if (const auto group = studio_.groupTracks(picked)) {
            state_.selectTrack(*group);
            notify(ui::Ls("Groupe créé : ", "Group made: ") + studio_.trackName(*group), false);
        }
    }
    // Ctrl+M: learn a controller's assignment (click a control, move one).
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_M, false))
        controllers_->setLearning(!controllers_->learning());
    // Ctrl+K: the computer's keys as a MIDI keyboard, or not.
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_K, false)) {
        computerKeyboard_->setEnabled(!computerKeyboard_->enabled());
        notify(computerKeyboard_->enabled() ? ui::Ls("Clavier de l'ordinateur : il joue les notes", "Computer keyboard: it plays notes")
                                            : ui::Ls("Clavier de l'ordinateur : éteint", "Computer keyboard: off"),
               false);
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, true))
        io.KeyShift ? redo() : undo();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, true))
        redo();
    if (!ownKeys && ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
        // In the workshop, play is the simulation's.
        if (state_.inWorkshop())
            studio_.setSimulating(!studio_.simulating());
        else
            studio_.togglePlay();
    }
    if (!ownKeys && ImGui::IsKeyPressed(ImGuiKey_Home, false))
        studio_.rewind();
    if (!ownKeys && !io.KeyCtrl && !state_.inWorkshop() && ImGui::IsKeyPressed(ImGuiKey_R, false))
        toggleRecording();
    if (!ownKeys && !io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_L, false)) {
        studio_.setLoopEnabled(!studio_.loopEnabled());
    }
    // Function keys bring the main panels forward.
    if(state_.mode==AppMode::cinema){
        const std::pair<ImGuiKey,int> tools[]={{ImGuiKey_F3,0},{ImGuiKey_F4,1},{ImGuiKey_F5,2},{ImGuiKey_F8,3},{ImGuiKey_F6,4}};
        for(const auto& [key,panel]:tools)if(ImGui::IsKeyPressed(key,false))cinema_->showToolsPanel(panel);
    }
    if (state_.mode!=AppMode::cinema&&ImGui::IsKeyPressed(ImGuiKey_F1, false))
        state_.bringToFront = Panel::arrangement;
    if (state_.mode!=AppMode::cinema&&ImGui::IsKeyPressed(ImGuiKey_F2, false))
        state_.bringToFront = Panel::launcher;
    if (state_.mode!=AppMode::cinema&&ImGui::IsKeyPressed(ImGuiKey_F3, false))
        state_.bringToFront = Panel::devices;
    if (state_.mode!=AppMode::cinema&&ImGui::IsKeyPressed(ImGuiKey_F4, false))
        state_.bringToFront = Panel::graph;
    if (state_.mode!=AppMode::cinema&&ImGui::IsKeyPressed(ImGuiKey_F5, false))
        state_.bringToFront = Panel::mixer;
    if (state_.mode!=AppMode::cinema&&ImGui::IsKeyPressed(ImGuiKey_F6, false))
        state_.bringToFront = Panel::editor;
    if (ImGui::IsKeyPressed(ImGuiKey_F7, false))
        state_.mode = state_.inWorkshop() ? AppMode::studio : AppMode::workshop;
    if (state_.mode!=AppMode::cinema&&ImGui::IsKeyPressed(ImGuiKey_F8, false))
        state_.bringToFront = Panel::routing;
    // The library's big window, on the sounds unless it is open.
    if (ImGui::IsKeyPressed(ImGuiKey_F9, false)) {
        state_.showLibrary = !state_.showLibrary;
        state_.libraryFocus = state_.showLibrary;
        state_.libraryPick = false;
        state_.libraryTarget.reset();
    }
    if (io.InputQueueCharacters.contains('?'))
        showShortcuts_ = true;
}

void AppUi::drawMenuBar()
{
    if (!ImGui::BeginMainMenuBar())
        return;
    {
        const auto height = ImGui::GetFrameHeight() * 0.86f;
        if (const auto texture = logo(height * ImGui::GetIO().DisplayFramebufferScale.x)) {
            const auto at = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + (ImGui::GetFrameHeight() - height) * 0.5f));
            ImGui::Image(ImTextureRef(static_cast<ImTextureID>(texture)), ImVec2(height, height));
            ImGui::SameLine(0.0f, dp(5.0f));
            ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, at.y));
        }
    }
    ImGui::PushFont(theme::boldFont(), 0.0f);
    ImGui::TextColored(theme::vec(theme::palette().accent), "CyDAW");
    ImGui::PopFont();
    ImGui::SameLine(0.0f, dp(10.0f));
    if (ImGui::BeginMenu(ui::labelId(L("Fichier", "File"), "menuFile").c_str())) {
        auto* fileView=state_.mode==AppMode::cinema?cinema_.get():state_.mode==AppMode::foundry?foundry_.get():nullptr;
        if (ImGui::MenuItem(L("Nouveau projet", "New project"), "Ctrl+N")){
            if(fileView)fileView->newDocument();else whenSaved([this] { newProject(); });
        }
        if (ImGui::MenuItem(L("Ouvrir…", "Open…"), "Ctrl+O")){
            if(fileView)fileView->chooseOpen();else whenSaved([this] { chooseOpenProject(); });
        }
        if (ImGui::BeginMenu(L("Modèles", "Templates"))) {
            const auto templates = studio_.templates();
            for (const auto& file : templates)
                if (ImGui::MenuItem(file.getFileNameWithoutExtension().toRawUTF8()))
                    whenSaved([this, file] { openProject(file); });
            if (templates.isEmpty())
                ImGui::TextDisabled("%s", L("Aucun modèle à côté de l'application", "No templates next to the application"));
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(L("Enregistrer", "Save"), "Ctrl+S")){
            if(fileView)fileView->save(false);else save();
        }
        if (ImGui::MenuItem(L("Enregistrer sous…", "Save as…"), "Ctrl+Maj+S")){
            if(fileView)fileView->save(true);else chooseSaveAs();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(L("Importer un projet…", "Import a project…")))
            whenSaved([this] { chooseImportProject(); });
        if (ImGui::MenuItem(L("Importer un fichier MIDI…", "Import a MIDI file…")))
            chooseImportMidi();
        const auto exporting = studio_.engine().isMasterExporting();
        if (ImGui::MenuItem(exporting ? L("Annuler l'export", "Cancel the export") : L("Exporter le mix (WAV)…", "Export the mix (WAV)…"))) {
            if (exporting)
                studio_.engine().cancelMasterExport();
            else if(fileView)fileView->chooseExport();
            else
                chooseExportMix();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(L("Quitter", "Quit"), "Ctrl+Q") && onQuit)
            onQuit();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(ui::labelId(L("Édition", "Edit"), "menuEdit").c_str())) {
        if (ImGui::MenuItem(L("Annuler", "Undo"), "Ctrl+Z"))
            undo();
        if (ImGui::MenuItem(L("Rétablir", "Redo"), "Ctrl+Y"))
            redo();
        ImGui::Separator();
        if (ImGui::MenuItem(L("Dupliquer", "Duplicate"), "Ctrl+D"))
            arrangement_->duplicateSelection();
        if (ImGui::MenuItem(L("Scinder à la tête de lecture", "Split at the playhead"), "S"))
            arrangement_->splitSelectionAtPlayhead();
        if (ImGui::MenuItem(L("Supprimer", "Delete"), "Suppr"))
            arrangement_->deleteSelection();
        if (ImGui::MenuItem(L("Tout sélectionner", "Select all"), "Ctrl+A"))
            arrangement_->selectAll();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(ui::labelId(L("Piste", "Track"), "menuTrack").c_str())) {
        if (ImGui::MenuItem(L("Nouvelle piste audio", "New audio track")))
            if (const auto track = studio_.addAudioTrack())
                state_.selectedTrack = track;
        if (ImGui::MenuItem(L("Nouvelle piste instrument", "New instrument track")))
            if (const auto track = studio_.addMidiTrack({}, "opale-instrument"))
                state_.selectedTrack = track;
        if (ImGui::MenuItem(L("Nouvelle piste bus", "New bus track")))
            if (const auto track = studio_.addBusTrack())
                state_.selectedTrack = track;
        ui::tooltip(L("Une piste sans clips où router d'autres pistes : groupe, réverbération partagée…",
                      "A track without clips to route other tracks into: a group, a shared reverb…"));
        ImGui::Separator();
        if (ImGui::BeginMenu(L("Recréer un son (enregistrement → instrument)", "Recreate a sound (recording → instrument)"),
                             !studio_.soundMatching().running)) {
            if (ImGui::MenuItem(L("Automatique…", "Automatic…")))
                chooseSoundToRecreate("auto");
            ui::tooltip(L("Une ou plusieurs notes enregistrées (WAV) analysées en partiels et bruit : un instrument qui les "
                          "rejoue à toutes les hauteurs et vélocités. Plusieurs notes donnent plusieurs zones ; un enregistrement "
                          "de plusieurs notes est découpé. Les sons de cloche ou de marimba gardent leurs partiels non harmoniques ; "
                          "une corde pincée est rejouée en corde quand elle sonne aussi juste.",
                          "One or more recorded notes (WAV) analysed into partials and noise: an instrument that plays them at "
                          "any pitch and velocity. Several notes make several zones; a recording of several notes is split. Bell "
                          "or marimba sounds keep their inharmonic partials; a plucked string is played as a string when it "
                          "sounds as true."));
            if (ImGui::MenuItem(L("Corde pincée…", "Plucked string…")))
                chooseSoundToRecreate("string");
            ui::tooltip(L("Guitare, harpe, kalimba… : une corde simulée (Karplus-Strong) avec la caisse mesurée sur la note. "
                          "Joue comme une vraie corde, un peu moins fidèle au timbre que les partiels mesurés.",
                          "Guitar, harp, kalimba…: a simulated string (Karplus-Strong) with the body measured on the note. "
                          "Plays like a real string, a little less true to the timbre than the measured partials."));
            if (ImGui::MenuItem(L("Partiels mesurés…", "Measured partials…")))
                chooseSoundToRecreate("partials");
            ui::tooltip(L("Toujours les partiels et le bruit mesurés, sans chercher de corde.",
                          "Always the measured partials and noise, never a string."));
            if (ImGui::MenuItem(L("Patch de synthé (Opale ou Quartz)…", "Synth patch (Opale or Quartz)…")))
                chooseSoundToRecreate("synth");
            ui::tooltip(L("Cherche les réglages d'Opale (soustractif) ou de Quartz (FM) qui imitent la note, en écoutant des "
                          "milliers de candidats : un patch que l'on peut ensuite retoucher comme n'importe quel autre.",
                          "Searches the settings of Opale (subtractive) or Quartz (FM) that imitate the note, hearing thousands "
                          "of candidates: a patch to tweak afterwards like any other."));
            ImGui::Separator();
            ImGui::MenuItem(L("Affiner (plus lent)", "Refine (slower)"), nullptr, &refineRecreated_);
            ui::tooltip(L("Après l'analyse, les partiels, le bruit et l'attaque sont ajustés à l'oreille (PyTorch) : plus "
                          "fidèle, plus long.",
                          "After the analysis, the partials, noise and attack are adjusted by ear (PyTorch): truer, longer."));
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(L("Charger un modèle de son…", "Load a sound model…")))
            chooseSoundModel();
        ui::tooltip(L("Un modèle déjà analysé (.cysound, dans Documents/CyDAW/Sound Models) sur une nouvelle piste instrument.",
                      "A model analysed before (.cysound, in Documents/CyDAW/Sound Models) on a new instrument track."));
        ImGui::Separator();
        if (ImGui::MenuItem(L("Nouveau sampler (échantillons)…", "New sampler (samples)…")))
            chooseSamplerFiles();
        if (ImGui::MenuItem(L("Nouveau Prisme (synthé à tables d'ondes)", "New Prisme (wavetable synth)")))
            newPrismTrack();
        if (ImGui::MenuItem(L("Nouveau Kick (synthé de grosse caisse)", "New Kick (kick drum synth)")))
            newKickTrack();
        ui::tooltip(L("Un instrument qui ne fait que des kicks : un corps dont la hauteur tombe, un clic, un sub, une "
                      "saturation. La note le joue (C2 tel quel, les autres plus haut ou plus bas).",
                      "An instrument for kicks alone: a body whose pitch falls, a click, a sub, a drive. The note plays "
                      "it (C2 as set, the others higher or lower)."));
        ui::tooltip(L("Des échantillons joués tels qu'ils sont, chacun sur ses touches : la note vient du nom du fichier "
                      "(C4, A#2…) ou de la hauteur entendue ; plusieurs fichiers sur une note font des couches de vélocité.",
                      "Samples played as they are, each over its keys: the note comes from the file's name (C4, A#2…) or "
                      "the pitch heard; several files on one note make velocity layers."));
        // The synth family: one entry per model.
        for (int index = 1; index < engine::synth::modelCount; ++index) {
            const auto model = static_cast<engine::synth::Model>(index);
            const auto& info = engine::synth::info(model);
            if (info.parameters.empty())
                continue;
            const auto label = juce::String::fromUTF8(ui::french() ? "Nouveau " : "New ")
                               + juce::String::fromUTF8((ui::french() ? info.french : info.english).c_str());
            if (ImGui::MenuItem(label.toRawUTF8()))
                newSynthTrack(model);
            ui::tooltip((ui::french() ? info.frenchHint : info.englishHint).c_str());
        }
        if (ImGui::MenuItem(L("Nouvelle Batterie (kits acoustiques)", "New Batterie (acoustic kits)")))
            newBatterieTrack();
        ui::tooltip(L("Une batterie acoustique jouée élément par élément : six kits libres, une console (niveau, "
                      "panoramique, accord, durée, muet, solo pour chaque élément), des motifs à poser sur la piste.",
                      "An acoustic drum kit played piece by piece: six free kits, a console (level, pan, tuning, decay, "
                      "mute, solo for each piece), grooves to put on the track."));
        if (ImGui::MenuItem(L("Ouvrir un instrument SFZ…", "Open an SFZ instrument…")))
            chooseSamplerInstrument();
        ui::tooltip(L("Un instrument SFZ (le format libre des samplers) ou une carte .cysampler, sur une nouvelle piste.",
                      "An SFZ instrument (the samplers' open format) or a .cysampler map, on a new track."));
        drawFactoryInstrumentsMenu();
        ImGui::Separator();
        const auto canDelete = state_.selectedTrack.has_value();
        if (ImGui::MenuItem(L("Supprimer la piste sélectionnée", "Delete the selected track"), nullptr, false, canDelete)) {
            state_.selectedClips.clear();
            state_.editedClip.reset();
            if (studio_.removeTrack(*state_.selectedTrack))
                state_.selectedTrack.reset();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(ui::labelId(L("Affichage", "View"), "menuView").c_str())) {
        if(ImGui::MenuItem(L("Éditeur de scripts JSFX…", "JSFX script editor…"),nullptr,state_.showJsfx))state_.showJsfx=!state_.showJsfx;
        if (ImGui::MenuItem(L("Panneau du bas", "Bottom panel"), "Ctrl+J", state_.bottomPanelVisible(), !state_.inWorkshop()))
            toggleBottomPanel();
        if (ImGui::MenuItem(L("Inspecteur (gauche)", "Inspector (left)"), nullptr, inspectorVisible_, state_.mode == AppMode::studio))
            toggleSidePanel(Panel::inspector);
        if (ImGui::MenuItem(L("Navigateur (droite)", "Browser (right)"), nullptr, browserVisible_, state_.mode == AppMode::studio))
            toggleSidePanel(Panel::browser);
        ImGui::Separator();
        if (ImGui::MenuItem("Arrangement", "F1"))
            state_.bringToFront = Panel::arrangement;
        if (ImGui::MenuItem(L("Lanceur", "Launcher"), "F2"))
            state_.bringToFront = Panel::launcher;
        if (ImGui::MenuItem(L("Périphériques", "Devices"), "F3"))
            {if(state_.mode==AppMode::cinema)cinema_->showToolsPanel(0);else state_.bringToFront = Panel::devices;}
        if (ImGui::MenuItem(L("Graphe", "Graph"), "F4"))
            {if(state_.mode==AppMode::cinema)cinema_->showToolsPanel(1);else state_.bringToFront = Panel::graph;}
        if (ImGui::MenuItem(L("Mixeur", "Mixer"), "F5"))
            {if(state_.mode==AppMode::cinema)cinema_->showToolsPanel(2);else state_.bringToFront = Panel::mixer;}
        if (ImGui::MenuItem(L("Éditeur", "Editor"), "F6"))
            {if(state_.mode==AppMode::cinema)cinema_->showToolsPanel(4);else state_.bringToFront = Panel::editor;}
        if (ImGui::MenuItem(L("Routage", "Routing"), "F8"))
            {if(state_.mode==AppMode::cinema)cinema_->showToolsPanel(3);else state_.bringToFront = Panel::routing;}
        if (ImGui::MenuItem(L("Bibliothèque (grande fenêtre)", "Library (big window)"), "F9", state_.showLibrary)) {
            state_.showLibrary = !state_.showLibrary;
            state_.libraryFocus = state_.showLibrary;
            state_.libraryPick = false;
            state_.libraryTarget.reset();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(L("Mix DJ (plein écran)", "DJ mix (whole window)"), "F10", state_.mode == AppMode::dj))
            state_.mode = state_.mode == AppMode::dj ? AppMode::studio : AppMode::dj;
        if (ImGui::MenuItem(L("Cinéma (son à l'image)", "Cinema (sound to picture)"),nullptr,state_.mode==AppMode::cinema))state_.mode=AppMode::cinema;
        if (ImGui::MenuItem(L("Fonderie (plein écran)", "Foundry (whole window)"), "F11", state_.mode == AppMode::foundry))
            state_.mode = state_.mode == AppMode::foundry ? AppMode::studio : AppMode::foundry;
        if (ImGui::MenuItem(L("Traitements (plein écran)", "Processings (whole window)"), nullptr, state_.mode == AppMode::processing))
            state_.mode = state_.mode == AppMode::processing ? AppMode::studio : AppMode::processing;
        if (ImGui::MenuItem(L("Atelier Plugin (plein écran)", "Plugin workshop (whole window)"), "F7", state_.inWorkshop()))
            state_.mode = state_.inWorkshop() ? AppMode::studio : AppMode::workshop;
        ImGui::Separator();
        // A panel in a window of its own, to put on another screen. Dragging
        // a panel's tab out of the window does the same.
        if (ImGui::BeginMenu(L("Détacher dans une fenêtre", "Detach into a window"))) {
            static constexpr struct {
                Panel panel;
                const char* fr;
                const char* en;
            } panels[] = { { Panel::arrangement, "Arrangement", "Arrangement" }, { Panel::launcher, "Lanceur", "Launcher" },
                           { Panel::devices, "Périphériques", "Devices" },    { Panel::graph, "Graphe", "Graph" },
                           { Panel::mixer, "Mixeur", "Mixer" },               { Panel::editor, "Éditeur", "Editor" },
                           { Panel::routing, "Routage", "Routing" },
                           { Panel::browser, "Navigateur", "Browser" },       { Panel::inspector, "Inspecteur", "Inspector" } };
            for (const auto& entry : panels)
                if (ImGui::MenuItem(L(entry.fr, entry.en))) {
                    detaching_ = entry.panel;
                    detachFrames_ = 0;
                }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(L("Réinitialiser la disposition", "Reset layout")))
            resetLayout();
        // Layouts kept by name: one for mixing, one for two screens…
        if (ImGui::BeginMenu(L("Dispositions", "Layouts"))) {
            if (ImGui::MenuItem(L("Enregistrer la disposition…", "Save the layout…"))) {
                layoutName_[0] = 0;
                askLayoutName_ = true;
            }
            ui::tooltip(L("Les panneaux, leur place et les fenêtres détachées (autres écrans compris).",
                          "The panels, their places and the detached windows (other screens included)."));
            const auto names = layoutNames();
            ImGui::Separator();
            if (names.isEmpty())
                ImGui::TextDisabled("%s", L("Aucune disposition enregistrée", "No saved layout"));
            for (const auto& name : names)
                if (ImGui::MenuItem(name.toRawUTF8()))
                    if (const auto loaded = loadLayout(name); loaded.failed())
                        notify(loaded.getErrorMessage(), true);
            if (!names.isEmpty()) {
                ImGui::Separator();
                if (ImGui::BeginMenu(L("Supprimer", "Delete"))) {
                    for (const auto& name : names)
                        if (ImGui::MenuItem(name.toRawUTF8()) && !deleteLayout(name))
                            notify(ui::Ls("La disposition n'a pas pu être supprimée", "The layout could not be deleted"), true);
                    ImGui::EndMenu();
                }
            }
            ImGui::EndMenu();
        }
        ImGui::MenuItem(L("Démo Dear ImGui", "Dear ImGui demo"), nullptr, &showDemo_);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(ui::labelId(L("Options", "Options"), "menuOptions").c_str())) {
        if (ImGui::MenuItem(L("Réglages audio…", "Audio settings…")))
            showAudioSettings_ = true;
        if (ImGui::MenuItem(L("Entrées et sorties de la carte son…", "Sound card inputs and outputs…")))
            showAudioSettings_ = showSoundCard_ = true;
        if (ImGui::MenuItem(L("Synchronisation du transport…", "Transport synchronization…")))
            showTransportSync_ = true;
        if (ImGui::MenuItem(L("Contrôleurs MIDI / OSC…", "MIDI / OSC controllers…")))
            showControllers_ = true;
        ui::tooltip(L("Appareils MIDI, OSC et plug-ins d'application ; appareils virtuels ; assignations et apprentissage.",
                      "MIDI, OSC and application plug-in devices; virtual devices; assignments and learning."));
        if (ImGui::MenuItem(L("Apprendre une assignation", "Learn an assignment"), "Ctrl+M", controllers_->learning()))
            controllers_->setLearning(!controllers_->learning());
        if (ImGui::MenuItem(L("Entrées MIDI…", "MIDI inputs…")))
            openMidiInputs();
        ui::tooltip(L("Les claviers et contrôleurs MIDI branchés, et le clavier de l'ordinateur comme clavier MIDI.",
                      "The MIDI keyboards and controllers plugged in, and the computer's keyboard as a MIDI keyboard."));
        if (ImGui::MenuItem(L("Clavier de l'ordinateur (MIDI)", "Computer keyboard (MIDI)"), "Ctrl+K", computerKeyboard_->enabled()))
            computerKeyboard_->setEnabled(!computerKeyboard_->enabled());
        ui::tooltip(L("Les canaux ouverts de la carte, les bus d'entrée que les pistes enregistrent et les bus de sortie "
                      "qui jouent le master et les pistes envoyées directement.",
                      "The card's open channels, the input buses tracks record and the output buses that play the master "
                      "and the tracks sent there directly."));
        if (ImGui::MenuItem(L("Banques de sons…", "Sound banks…"), nullptr, state_.showBanks))
            state_.showBanks = !state_.showBanks;
        ui::tooltip(L("Les banques d'échantillons des instruments d'usine : licence, provenance, téléchargement.",
                      "The factory instruments' sample banks: licence, source, download."));
        if (ImGui::MenuItem(L("Recherche sémantique des sons (IA)…", "Semantic sound search (AI)…"), nullptr, state_.showSemantic))
            state_.showSemantic = !state_.showSemantic;
        ui::tooltip(L("Optionnelle : un modèle CLAP pour chercher les sons par description et par ressemblance ; son "
                      "installation, sa licence, l'index de vos sons.",
                      "Optional: a CLAP model to search sounds by description and likeness; its install, its licence, "
                      "your sounds' index."));
        if (ImGui::MenuItem(L("Emplacements…", "Locations…")))
            state_.openLocations = true;
        ui::tooltip(L("Où CyDAW range les banques, les téléchargements, le cache, le compagnon, les modèles et les "
                      "périphériques.",
                      "Where CyDAW keeps the banks, the downloads, the cache, the companion, the models and the devices."));
        if (ImGui::MenuItem(L("Compagnon Python (analyse des sons)…", "Python companion (sound analysis)…")))
            showCompanion_ = true;
        ui::tooltip(L("Le Python qui analyse les sons à recréer : lequel, ce qu'il a, et l'installation de numpy et "
                      "PyTorch (CPU ou CUDA) dans un environnement à part.",
                      "The Python that analyses the sounds to recreate: which one, what it has, and installing numpy "
                      "and PyTorch (CPU or CUDA) in an environment of its own."));
        if (ImGui::BeginMenu(L("Thème", "Theme"))) {
            // Colours only: the built-in ones, then the person's own.
            auto own = false;
            for (const auto& entry : themes_->themes()) {
                if (!entry.builtIn && !own) {
                    own = true;
                    ImGui::Separator();
                }
                if (ImGui::MenuItem(entry.name.toRawUTF8(), nullptr, entry.key == themes_->currentKey()))
                    (void) themes_->choose(entry.key);
            }
            ImGui::Separator();
            if (ImGui::MenuItem(L("Thèmes et couleurs…", "Themes and colours…"), nullptr, state_.showThemes))
                state_.showThemes = !state_.showThemes;
            ui::tooltip(L("Choisir un thème, ou en faire un à soi à partir d'un autre et changer ses couleurs.",
                          "Pick a theme, or make one of your own from another and change its colours."));
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(L("Plusieurs fenêtres d'instrument", "Several instrument windows"), nullptr, state_.manyInstrumentWindows)) {
            state_.manyInstrumentWindows = !state_.manyInstrumentWindows;
            if (studio_.keepsSettings())
                studio_.settings().setValue("manyInstrumentWindows", state_.manyInstrumentWindows);
        }
        ui::tooltip(L("Ouvrir la fenêtre d'un instrument garde celles des autres ouvertes (une chacun) ; décoché, elle "
                      "remplace la dernière.",
                      "Opening an instrument's window keeps the others' open (one each); unchecked, it replaces the "
                      "last one."));
        if (ImGui::BeginMenu(L("Langue", "Language"))) {
            // The interface and the engine's texts (node names, messages)
            // change together.
            for (const auto& language : app::Localization::availableLanguages())
                if (ImGui::MenuItem(language.name.toRawUTF8(), language.code.toRawUTF8(), ui::language() == language.code)
                    && app::Localization::instance().setLanguage(language.code)) {
                    ui::setLanguage(language.code);
                    studio_.refreshTranslations();
                }
            ImGui::Separator();
            ImGui::TextDisabled("%s", L("Une langue de plus : i18n/<code>.json et i18n/v2/<code>.json",
                                        "One more language: i18n/<code>.json and i18n/v2/<code>.json"));
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(ui::labelId(L("Aide", "Help"), "menuHelp").c_str())) {
        if (ImGui::MenuItem(L("Raccourcis clavier", "Keyboard shortcuts"), "?"))
            showShortcuts_ = true;
        if (ImGui::MenuItem(L("À propos de CyDAW 2", "About CyDAW 2")))
            showAbout_ = true;
        ImGui::EndMenu();
    }
    // The project's name, centred.
    const auto name = studio_.projectName().isNotEmpty() ? studio_.projectName() : ui::Ls("Sans titre", "Untitled");
    const auto title = name + (studio_.isDirty() ? " *" : "");
    const auto width = ImGui::CalcTextSize(title.toRawUTF8()).x;
    ImGui::SameLine((ImGui::GetWindowWidth() - width) * 0.5f);
    ImGui::TextColored(theme::vec(theme::palette().textDim), "%s", title.toRawUTF8());
    ImGui::EndMainMenuBar();
}

void AppUi::drawTransportBar()
{
    const auto& p = theme::palette();
    auto& engine = studio_.engine();
    const auto height = ImGui::GetFrameHeight() + dp(14.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(dp(8.0f), dp(7.0f)));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, p.bg1);
    if (ImGui::BeginViewportSideBar("##transport", ImGui::GetMainViewport(), ImGuiDir_Up, height,
                                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings)) {
        // In the workshop the transport plays the simulation, not the tracks.
        const auto simulation = state_.inWorkshop();
        const auto playing = simulation ? studio_.simulating() : engine.isPlaying();
        drawModeSwitch();
        ui::toolbarSeparator();
        const auto own = state_.mode == AppMode::dj || state_.mode == AppMode::foundry || state_.mode==AppMode::cinema;
        if (own) {
            if (state_.mode == AppMode::dj)
                dj_->drawTransport();
            else if(state_.mode==AppMode::cinema)
                cinema_->drawTransport();
            else
                foundry_->drawTransport();
        }
        if (!own) {
            if (ui::iconButton(icon::previous, L("Au début (Origine)", "To the start (Home)"))) {
                if (simulation && playing)
                    studio_.setSimulating(true);
                else if (!simulation)
                    studio_.rewind();
            }
            ImGui::SameLine(0.0f, dp(2.0f));
            // What a controller can move (learn mode), not in the workshop's simulation.
            const auto learnKey = [simulation](const char* what) { return simulation ? std::string() : control_targets::forTransport(what); };
            {
            const ui::LearnScope learnPlay(learnKey("play"), ui::Ls("Transport · Lecture / pause", "Transport · Play / pause").toStdString());
            if (ui::iconButton(playing ? icon::pause : icon::play,
                               simulation ? L("Lancer la simulation : le périphérique seul, avec les sources de test (Espace)",
                                              "Run the simulation: the device on its own, with the test sources (Space)")
                                          : L("Lecture / pause (Espace)", "Play / pause (Space)"),
                               playing, p.play)) {
                if (simulation)
                    studio_.setSimulating(!playing);
                else
                    studio_.togglePlay();
            }
            }
            ImGui::SameLine(0.0f, dp(2.0f));
            {
            const ui::LearnScope learnStop(learnKey("stop"), "Transport · Stop");
            if (ui::iconButton(icon::stop, L("Stop (deux fois : retour au début)", "Stop (twice: back to the start)"))) {
                if (simulation)
                    studio_.setSimulating(false);
                else
                    studio_.stop();
            }
            }
            const auto recording = studio_.isRecording();
            if (!state_.inWorkshop() || recording) {
                ImGui::SameLine(0.0f, dp(2.0f));
                const ui::LearnScope learnRecord(learnKey("record"), ui::Ls("Transport · Enregistrer", "Transport · Record").toStdString());
                if (ui::iconButton(icon::record, L("Enregistrer la piste armée ou sélectionnée (R)", "Record the armed or selected track (R)"),
                                   recording, p.record))
                    toggleRecording();
            }
            if (!simulation) {
                ImGui::SameLine(0.0f, dp(2.0f));
                const ui::LearnScope learnLoop(learnKey("loop"), ui::Ls("Transport · Boucle", "Transport · Loop").toStdString());
                if (ui::iconButton(icon::loop, L("Boucle (L)", "Loop (L)"), studio_.loopEnabled()))
                    studio_.setLoopEnabled(!studio_.loopEnabled());
            }

            ui::toolbarSeparator();
            const auto seconds = simulation ? studio_.simulationSeconds() : engine.position();
            const auto tempo = std::max(1.0, engine.tempo());
            const auto beats = seconds * tempo / 60.0;
            // Bars as the metre counts them; beats in its own value (an eighth in 7/8).
            const auto barLength = engine.beatsPerBar();
            const auto bar = static_cast<int>(std::floor(beats / barLength + 1.0e-9)) + 1;
            const auto inBar = beats - (bar - 1) * barLength;
            const auto beatLength = 4.0 / engine.timeSignatureDenominator();
            const auto beat = static_cast<int>(std::floor(inBar / beatLength + 1.0e-9)) + 1;
            const auto sixteenth = static_cast<int>(std::fmod(beats * 4.0, 4.0)) + 1;
            char position[64];
            std::snprintf(position, sizeof(position), "%3d.%d.%d", bar, beat, sixteenth);
            char clock[64];
            const auto minutes = static_cast<int>(seconds / 60.0);
            std::snprintf(clock, sizeof(clock), "%02d:%06.3f", minutes, seconds - minutes * 60.0);
            ImGui::PushFont(theme::monoFont(), ImGui::GetStyle().FontSizeBase * 1.15f);
            const auto origin = ImGui::GetCursorScreenPos();
            const ImVec2 box(dp(190.0f), ImGui::GetFrameHeight());
            auto* list = ImGui::GetWindowDrawList();
            list->AddRectFilled(origin, ImVec2(origin.x + box.x, origin.y + box.y), p.bg0, ImGui::GetStyle().FrameRounding);
            list->AddText(ImVec2(origin.x + dp(8.0f), origin.y + (box.y - ImGui::GetFontSize()) * 0.5f), playing ? p.play : p.text,
                          position);
            list->AddText(ImVec2(origin.x + dp(92.0f), origin.y + (box.y - ImGui::GetFontSize()) * 0.5f), p.textDim, clock);
            ImGui::Dummy(box);
            ImGui::PopFont();

            ui::toolbarSeparator();
            auto bpm = static_cast<float>(engine.tempo());
            if (ui::dragNumber("##tempo", &bpm, 0.1f, 20.0f, 300.0f, "%.2f BPM", dp(96.0f))) {
                engine.setTempo(bpm);
                studio_.markDirty();
            }
            ui::learnItem(learnKey("tempo"), "Transport · Tempo");
            ui::tooltip(L("Tempo : glisser, ou double-clic pour taper une valeur", "Tempo: drag, or double click to type"));
            ImGui::SameLine();
            {
                // The metre: a click opens its choice.
                char metre[16];
                std::snprintf(metre, sizeof(metre), "%d/%d###metre", engine.timeSignatureNumerator(), engine.timeSignatureDenominator());
                ImGui::PushStyleColor(ImGuiCol_Button, p.bg1);
                if (ImGui::Button(metre))
                    ImGui::OpenPopup("##metrePopup");
                ImGui::PopStyleColor();
                ui::tooltip(L("Signature rythmique (temps par mesure)", "Time signature (beats per bar)"));
                if (ImGui::BeginPopup("##metrePopup")) {
                    auto numerator = engine.timeSignatureNumerator();
                    auto denominator = engine.timeSignatureDenominator();
                    ImGui::TextDisabled("%s", L("Temps par mesure", "Beats per bar"));
                    ImGui::SetNextItemWidth(dp(120.0f));
                    if (ImGui::InputInt("##numerator", &numerator, 1, 1)) {
                        engine.setTimeSignature(numerator, denominator);
                        studio_.markDirty();
                    }
                    ImGui::TextDisabled("%s", L("Valeur du temps", "Beat value"));
                    for (const auto value : { 2, 4, 8, 16 }) {
                        char label[8];
                        std::snprintf(label, sizeof(label), "%d", value);
                        if (ImGui::RadioButton(label, denominator == value)) {
                            engine.setTimeSignature(numerator, value);
                            studio_.markDirty();
                        }
                        ImGui::SameLine();
                    }
                    ImGui::NewLine();
                    ImGui::Separator();
                    for (const auto& preset : { std::pair { 4, 4 }, std::pair { 3, 4 }, std::pair { 5, 4 }, std::pair { 6, 8 },
                                                std::pair { 7, 8 }, std::pair { 12, 8 } }) {
                        char label[8];
                        std::snprintf(label, sizeof(label), "%d/%d", preset.first, preset.second);
                        if (ImGui::Selectable(label, numerator == preset.first && denominator == preset.second,
                                              ImGuiSelectableFlags_None, ImVec2(dp(56.0f), 0.0f))) {
                            engine.setTimeSignature(preset.first, preset.second);
                            studio_.markDirty();
                        }
                    }
                    ImGui::EndPopup();
                }
            }

            // The computer's keys as a MIDI keyboard: on or off, its octave; a
            // right click opens the MIDI inputs.
            {
                ui::toolbarSeparator();
                auto& keyboard = *computerKeyboard_;
                const auto& p = theme::palette();
                if (ui::iconButton(icon::keyboard,
                                   keyboard.enabled() ? L("Clavier de l'ordinateur : il joue la piste instrument choisie (Ctrl+K). "
                                                          "Clic droit : les entrées MIDI.",
                                                          "Computer keyboard: it plays the picked instrument track (Ctrl+K). "
                                                          "Right click: the MIDI inputs.")
                                                      : L("Clavier de l'ordinateur éteint (Ctrl+K). Clic droit : les entrées MIDI.",
                                                          "Computer keyboard off (Ctrl+K). Right click: the MIDI inputs."),
                                   keyboard.enabled(), p.accent))
                    keyboard.setEnabled(!keyboard.enabled());
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                    openMidiInputs();
                if (keyboard.enabled()) {
                    ImGui::SameLine(0.0f, dp(4.0f));
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextColored(theme::vec(keyboard.active() ? p.text : p.textFaint), "Oct %d", keyboard.octave());
                    ui::tooltip(keyboard.active() ? L("L'octave de sa première touche", "Its first key's octave")
                                                  : L("Choisissez une piste instrument pour jouer", "Pick an instrument track to play"));
                }
            }

        }
        ui::toolbarSeparator();
        transportSync_->drawButton(showTransportSync_);
        ImGui::SameLine();
        controllers_->drawButton(showControllers_);
        const auto right = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x;
        drawSystemStatus(right - dp(262.0f));
        ImGui::SameLine(right - dp(262.0f));
        const auto& meter = studio_.masterMeter();
        ImGui::TextDisabled("MASTER");
        ImGui::SameLine();
        const auto meterPos = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(meterPos.x, meterPos.y + ImGui::GetFrameHeight() * 0.22f));
        ui::meter("##masterMeter", ImVec2(dp(120.0f), ImGui::GetFrameHeight() * 0.56f), meter.peak[0], meter.peak[1],
                  meter.rms[0], meter.rms[1], false);
        ImGui::SameLine();
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, meterPos.y));
        auto master = engine.masterGainDb();
        if (ui::dragNumber("##masterGain", &master, 0.1f, -60.0f, 12.0f, "%.1f dB", dp(70.0f)))
            engine.setMasterGain(master);
        ui::tooltip(L("Volume général", "Master volume"));
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void AppUi::drawSystemStatus(const float endX)
{
    const auto& p = theme::palette();
    auto& engine = studio_.engine();
    // A message in or out: the lights move, the interface draws them.
    const auto traffic = static_cast<std::uint64_t>(engine.midiInActivity()) + engine.midiOutActivity() + transportSync_->receivedCount()
                         + transportSync_->sentCount();
    if (traffic != lastTraffic_) {
        lastTraffic_ = traffic;
        host_.wake(24);
    }
    const auto load = system_load::read();
    auto& devices = engine.deviceManager();
    auto* audio = devices.getCurrentAudioDevice();
    const auto midiIns = midi_devices::inputs().size();
    const auto midiOuts = midi_devices::outputs().size();
    const auto plugged = midiIns + midiOuts + (audio != nullptr ? 1 : 0);
    const auto dsp = devices.getCpuUsage() * 100.0;
    char count[16], cpu[24], ram[24], dspText[24];
    std::snprintf(count, sizeof(count), "%d", plugged);
    // Always a percent (the memory: of the machine's), each with its bar.
    if (load.cpuPercent >= 0.0)
        std::snprintf(cpu, sizeof(cpu), "CPU %.0f%%", load.cpuPercent);
    else
        std::snprintf(cpu, sizeof(cpu), "CPU --%%");
    if (load.memoryPercent >= 0.0)
        std::snprintf(ram, sizeof(ram), load.memoryPercent < 10.0 ? "RAM %.1f%%" : "RAM %.0f%%", load.memoryPercent);
    else
        std::snprintf(ram, sizeof(ram), "RAM --%%");
    std::snprintf(dspText, sizeof(dspText), "DSP %.0f%%", dsp);
    // How much memory, said in its tooltip.
    const auto sizeText = [](const double bytes) {
        char text[32];
        std::snprintf(text, sizeof(text), bytes >= 1.0e9 ? "%.1f %s" : "%.0f %s", bytes >= 1.0e9 ? bytes / 1.0e9 : bytes / 1.0e6,
                      bytes >= 1.0e9 ? L("Go", "GB") : L("Mo", "MB"));
        return juce::String(text);
    };

    ImGui::PushFont(nullptr, ui::currentFontBase() * 0.8f);
    const auto light = dp(7.0f);
    const auto inner = dp(3.0f);
    const auto gap = dp(12.0f);
    const auto textWidth = [](const char* text) { return ImGui::CalcTextSize(text).x; };
    const auto pairWidth = [&](const char* label) { return textWidth(label) + inner + light + inner + light; };
    const auto barWidth = dp(26.0f);
    const auto barHeight = dp(5.0f);
    const auto width = pairWidth("MIDI") + gap + pairWidth("SYNC") + gap + textWidth(icon::connect) + inner + textWidth(count) + gap
                       + textWidth(cpu) + gap + textWidth(ram) + gap + textWidth(dspText) + 3.0f * (inner + barWidth);
    // Only where the toolbar leaves it room.
    const auto start = endX - width - dp(14.0f);
    if (ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + dp(12.0f) > start) {
        ImGui::PopFont();
        return;
    }
    ImGui::SameLine(start);
    const auto origin = ImGui::GetCursorScreenPos();
    const auto height = ImGui::GetFrameHeight();
    auto* list = ImGui::GetWindowDrawList();
    const auto textY = origin.y + (height - ImGui::GetFontSize()) * 0.5f;
    auto x = origin.x;
    const auto hovering = [&](const float from, const float to) {
        return ImGui::IsMouseHoveringRect(ImVec2(from, origin.y), ImVec2(to, origin.y + height)) && ImGui::IsWindowHovered();
    };
    const auto lightAt = [&](const float level, const ImU32 colour) {
        const ImVec2 min(x, origin.y + (height - light) * 0.5f);
        list->AddRectFilled(min, ImVec2(min.x + light, min.y + light), theme::mix(p.bg0 | 0xff000000u, colour, 0.12f + 0.88f * level), dp(1.5f));
        list->AddRect(min, ImVec2(min.x + light, min.y + light), theme::alpha(colour, 0.55f), dp(1.5f));
        x += light + inner;
    };
    const auto label = [&](const char* text, const ImU32 colour) {
        list->AddText(ImVec2(x, textY), colour, text);
        x += textWidth(text) + inner;
    };
    // MIDI: in (green), out (orange).
    {
        const auto from = x;
        label("MIDI", p.textFaint);
        lightAt(midi_devices::activity("top:midiIn", engine.midiInActivity()), p.play);
        lightAt(midi_devices::activity("top:midiOut", engine.midiOutActivity()), p.accent);
        if (hovering(from, x)) {
            juce::String tip = ui::Ls("MIDI : la lumière verte s'allume quand un message arrive (un clavier, un contrôleur, le clavier de "
                                      "l'ordinateur), l'orange quand une piste envoie ses notes à une sortie.",
                                      "MIDI: the green light comes on when a message arrives (a keyboard, a controller, the computer "
                                      "keyboard), the orange one when a track sends its notes to an output.");
            tip << "\n" << juce::String(static_cast<int>(midiIns)) << ui::Ls(" entrée(s), ", " input(s), ") << juce::String(static_cast<int>(midiOuts))
                << ui::Ls(" sortie(s) branchée(s).", " output(s) plugged in.");
            ui::tooltip(tip.toRawUTF8());
        }
        x += gap - inner;
    }
    // The transport's sync: in, out.
    {
        const auto from = x;
        const auto status = transportSync_->statusText();
        label("SYNC", status.isNotEmpty() ? p.textDim : p.textFaint);
        lightAt(midi_devices::activity("top:syncIn", static_cast<std::uint32_t>(transportSync_->receivedCount())), p.play);
        lightAt(midi_devices::activity("top:syncOut", static_cast<std::uint32_t>(transportSync_->sentCount())), p.accent);
        if (hovering(from, x))
            ui::tooltip(status.isNotEmpty()
                            ? (ui::Ls("Synchronisation : ", "Sync: ") + status
                               + ui::Ls("\nVert : ce qu'elle reçoit ; orange : ce qu'elle envoie.", "\nGreen: what it receives; orange: what it sends."))
                                  .toRawUTF8()
                            : L("Synchronisation éteinte (le bouton SYNC la règle).", "Sync off (the SYNC button sets it)."));
        x += gap - inner;
    }
    // The devices plugged in.
    {
        const auto from = x;
        label(icon::connect, p.textDim);
        label(count, p.textDim);
        if (hovering(from, x)) {
            juce::String tip = ui::Ls("Périphériques branchés", "Devices plugged in");
            tip << "\n" << ui::Ls("Carte son : ", "Sound card: ")
                << (audio != nullptr ? audio->getName() + "  (" + juce::String(audio->getCurrentSampleRate() / 1'000.0, 1) + " kHz)"
                                     : ui::Ls("aucune", "none"));
            const auto names = [](const juce::Array<juce::MidiDeviceInfo>& found) {
                juce::StringArray list;
                for (const auto& device : found)
                    list.add(device.name);
                return list.isEmpty() ? ui::Ls("aucune", "none") : list.joinIntoString(", ");
            };
            tip << "\n" << ui::Ls("Entrées MIDI : ", "MIDI inputs: ") << names(midi_devices::inputs());
            tip << "\n" << ui::Ls("Sorties MIDI : ", "MIDI outputs: ") << names(midi_devices::outputs());
            ui::tooltip(tip.toRawUTF8());
        }
        x += gap - inner;
    }
    // What CyDAW takes of the computer: a percent and a bar, green, then
    // orange, then red as it fills.
    const auto reading = [&](const char* text, const double percent, const double orange, const double red, const char* tip) {
        const auto from = x;
        const auto level = percent < 0.0 ? 0.0 : std::clamp(percent / 100.0, 0.0, 1.0);
        const auto colour = percent >= red ? p.record : percent >= orange ? p.warn : p.play;
        list->AddText(ImVec2(x, textY), percent >= orange ? colour : p.textDim, text);
        x += textWidth(text) + inner;
        const ImVec2 barMin(x, origin.y + (height - barHeight) * 0.5f);
        list->AddRectFilled(barMin, ImVec2(barMin.x + barWidth, barMin.y + barHeight), p.bg0, dp(1.5f));
        if (level > 0.0)
            list->AddRectFilled(barMin, ImVec2(barMin.x + std::max(dp(1.5f), barWidth * static_cast<float>(level)), barMin.y + barHeight), colour,
                                dp(1.5f));
        list->AddRect(barMin, ImVec2(barMin.x + barWidth, barMin.y + barHeight), theme::alpha(p.line, 0.8f), dp(1.5f));
        x += barWidth;
        if (hovering(from, x))
            ui::tooltip(tip);
        x += gap;
    };
    reading(cpu, load.cpuPercent, 60.0, 85.0,
            L("Processeur : la part de l'ordinateur (tous ses cœurs) que CyDAW utilise", "Processor: the share of the computer (all its cores) CyDAW uses"));
    const auto ramTip = load.memoryBytes >= 0.0 && load.totalMemoryBytes > 0.0
                            ? ui::Ls("Mémoire : ce que CyDAW occupe, ", "Memory: what CyDAW holds, ") + sizeText(load.memoryBytes)
                                  + ui::Ls(" sur ", " of ") + sizeText(load.totalMemoryBytes)
                            : ui::Ls("Mémoire : ce que CyDAW occupe", "Memory: what CyDAW holds");
    reading(ram, load.memoryPercent, 60.0, 85.0, ramTip.toRawUTF8());
    reading(dspText, dsp, 70.0, 90.0,
            L("Charge audio : la part du temps de chaque bloc que le calcul du son prend ; au-delà de 100 %, le son craque",
              "Audio load: the share of each block's time the sound takes to compute; past 100 %, the sound crackles"));
    ImGui::Dummy(ImVec2(width, height));
    ImGui::PopFont();
}

void AppUi::drawStatusBar()
{
    const auto& p = theme::palette();
    const auto height = ImGui::GetFontSize() + dp(8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(dp(8.0f), dp(4.0f)));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, p.bg0);
    if (ImGui::BeginViewportSideBar("##status", ImGui::GetMainViewport(), ImGuiDir_Down, height,
                                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.86f);
        const auto age = ImGui::GetTime() - state_.statusTime;
        if (state_.status.isNotEmpty() && age < 8.0)
            ImGui::TextColored(theme::vec(state_.statusIsError ? p.record : p.textDim), "%s", state_.status.toRawUTF8());
        else
            ImGui::TextColored(theme::vec(p.textFaint), "%s", L("Prêt", "Ready"));
        // A sound being recreated: how far, and a way to stop it.
        if (const auto matching = studio_.soundMatching(); matching.running) {
            ImGui::SameLine(0.0f, dp(18.0f));
            ImGui::TextColored(theme::vec(p.accent), "%s %d %%  %s", L("Recréation du son", "Recreating the sound"),
                               static_cast<int>(std::round(matching.progress * 100.0)), matching.message.toRawUTF8());
            ImGui::SameLine(0.0f, dp(6.0f));
            if (ImGui::SmallButton(L("Annuler", "Cancel")))
                studio_.cancelSoundMatching();
            host_.wake(2);
        }
        if (studio_.engine().isMasterExporting()) {
            ImGui::SameLine(0.0f, dp(18.0f));
            ImGui::TextColored(theme::vec(p.accent), "%s %d %%", L("Export", "Export"),
                               static_cast<int>(std::round(studio_.engine().masterExportProgress() * 100.0)));
            host_.wake(2);
        }
        ImGui::SameLine(0.0f, dp(18.0f));
        ImGui::TextColored(theme::vec(p.textFaint), "%s", studio_.audioStatus().toRawUTF8());
        char text[64];
        std::snprintf(text, sizeof(text), "%.0f i/s", ImGui::GetIO().Framerate);
        ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - ImGui::CalcTextSize(text).x);
        ImGui::TextColored(theme::vec(p.textFaint), "%s", text);
        ImGui::PopFont();
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void AppUi::drawDockSpace()
{
    resolveMode();
    // The studio's panels wait, as they are, while another mode has the window.
    if (state_.mode != AppMode::studio)
        return;
    auto* viewport = ImGui::GetMainViewport();
    // Slim edge handles keep hidden side panels reachable without a menu.
    for (const auto which : { Panel::inspector, Panel::browser }) {
        if (which == Panel::inspector ? inspectorVisible_ : browserVisible_)
            continue;
        const auto left = which == Panel::inspector;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(dp(4.0f), dp(4.0f)));
        const auto stripWidth = ImGui::GetFrameHeight() + dp(8.0f);
        if (ImGui::BeginViewportSideBar(left ? "##inspectorRestore" : "##browserRestore", viewport,
                                       left ? ImGuiDir_Left : ImGuiDir_Right, stripWidth,
                                       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings
                                       | ImGuiWindowFlags_NoFocusOnAppearing)) {
            if (ui::primaryButton(left ? ">" : "<", ImVec2(stripWidth - dp(8.0f), ImGui::GetFrameHeight())))
                toggleSidePanel(which);
            ui::tooltip(left ? L("Afficher l'inspecteur", "Show the inspector")
                             : L("Afficher le navigateur", "Show the browser"));
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }
    if (!state_.bottomPanelVisible()) {
        // Keep a small reopen handle below the central area, beside the browser.
        float left = viewport->WorkPos.x + viewport->WorkSize.x * 0.14f;
        float width = viewport->WorkSize.x * 0.65f;
        const auto* window = ImGui::FindWindowByName(arrangementName().c_str());
        if (window != nullptr && window->DockNode != nullptr) {
            left = window->DockNode->Pos.x;
            width = window->DockNode->Size.x;
        }
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        if (ImGui::BeginViewportSideBar("##studioBottomHandle", viewport, ImGuiDir_Down, ui::bottomPanelRestoreHeight(),
                                       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings
                                       | ImGuiWindowFlags_NoFocusOnAppearing)) {
            ImGui::SetCursorPosX(std::clamp(left - viewport->Pos.x, 0.0f, viewport->Size.x - dp(120.0f)));
            if (ui::bottomPanelRestoreBar(std::min(width, ImGui::GetContentRegionAvail().x)))
                toggleBottomPanel();
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }
    const auto dockspaceId = ImGui::GetID("CyDAW2Dockspace");
    if (resetLayoutRequested_ || !layoutBuilt_ || ImGui::DockBuilderGetNode(dockspaceId) == nullptr) {
        buildDefaultLayout(dockspaceId);
        resetLayoutRequested_ = false;
        layoutBuilt_ = true;
    }
    ImGui::DockSpaceOverViewport(dockspaceId, viewport, ImGuiDockNodeFlags_None);
}

void AppUi::buildDefaultLayout(const unsigned int dockspaceId)
{
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

    ImGuiID centre = dockspaceId;
    const auto left = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, 0.14f, nullptr, &centre);
    const auto right = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.21f, nullptr, &centre);
    // The launcher carries its own mixer, under its columns; the mixer panel
    // sits with the devices, the graph and the editor.
    const auto bottom = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, 0.4f, nullptr, &centre);

    ImGui::DockBuilderDockWindow(inspectorName().c_str(), left);
    ImGui::DockBuilderDockWindow(browserName().c_str(), right);
    ImGui::DockBuilderDockWindow(arrangementName().c_str(), centre);
    ImGui::DockBuilderDockWindow(launcherName().c_str(), centre);
    ImGui::DockBuilderDockWindow(devicesName().c_str(), bottom);
    ImGui::DockBuilderDockWindow(graphName().c_str(), bottom);
    ImGui::DockBuilderDockWindow(mixerName().c_str(), bottom);
    ImGui::DockBuilderDockWindow(routingName().c_str(), bottom);
    ImGui::DockBuilderDockWindow(editorName().c_str(), bottom);
    ImGui::DockBuilderFinish(dockspaceId);
    if (state_.bringToFront == Panel::none)
        state_.bringToFront = Panel::arrangement;
}

} // namespace cydaw::v2
