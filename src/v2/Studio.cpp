#include "Studio.h"

#include <cydaw/engine/Containers.h>
#include <cydaw/engine/EffectCatalogue.h>

#include "DeviceOps.h"
#include "GraphPolySynthRenderer.h"
#include "DeviceTransfer.h"
#include "GraphPresetFile.h"

#include "Locations.h"
#include "SamplerMapFile.h"
#include "SoundModelFile.h"
#include "Widgets.h"

#include "GraphLayout.h"
#include "JsonValue.h"
#include "ImportedInstruments.h"
#include "ProjectImport.h"
#include "ProjectRecovery.h"

#include <cydaw/engine/DrumKit.h>
#include <cydaw/engine/ChannelLayout.h>
#include <cydaw/engine/SynthFamily.h>
#include <cydaw/engine/SynthMatch.h>

#include <mutex>
#include <thread>

#include <algorithm>
#include <cstring>
#include <set>
#include <unordered_set>
#include <unordered_map>

namespace cydaw::v2 {
namespace {

bool isGraphState(const juce::var& state)
{
    return state.getDynamicObject() != nullptr;
}

juce::String keyOf(const juce::var& module)
{
    const auto* object = module.getDynamicObject();
    return object != nullptr ? object->getProperty("key").toString() : juce::String();
}

bool isInline(const juce::var& module)
{
    const auto* object = module.getDynamicObject();
    return object != nullptr && static_cast<bool>(object->getProperty("inline"));
}

// Track colours of a fresh session, as ARGB.
constexpr juce::uint32 kTrackColours[] = {
    0xfff0a43a, 0xff5ab0f0, 0xff52d1a2, 0xffc792ea, 0xffe8765a, 0xff7fd1e8,
    0xffe8c547, 0xff9ccc65, 0xfff06292, 0xff8c9eff, 0xff4dd0b5, 0xffffb74d
};

juce::uint32 defaultColour(const std::size_t index)
{
    return kTrackColours[index % (sizeof(kTrackColours) / sizeof(kTrackColours[0]))];
}

juce::Result reverseEvents(std::vector<app::AudioTrackController::AudioEvent>& events, double duration, const juce::File& folder)
{
    std::vector<juce::File> made;
    for (auto& event : events) {
        const auto file = folder.getNonexistentChildFile(event.sourceFile.getFileNameWithoutExtension() + " ~reverse", ".wav", false);
        app::AudioEditRequest request;
        request.operation = app::AudioEditOperation::reverse;
        const auto length = event.sourceDurationSeconds > 0 ? event.sourceDurationSeconds : event.durationSeconds;
        const auto result = app::AudioTrackController::renderAudioEdit(event.sourceFile, event.sourceOffsetSeconds, length, request, file);
        if (result.failed()) {
            for (const auto& written : made) (void)written.deleteFile();
            return result;
        }
        made.push_back(file);
        event.sourceFile = file;
        event.startSeconds = std::max(0.0, duration - event.startSeconds - event.durationSeconds);
        std::swap(event.fadeInSeconds, event.fadeOutSeconds);
        for (auto& marker : event.stretchMarkers) {
            marker.sourceSeconds = 2 * event.sourceOffsetSeconds + length - marker.sourceSeconds;
            marker.timelineSeconds = event.durationSeconds - marker.timelineSeconds;
        }
        std::reverse(event.stretchMarkers.begin(), event.stretchMarkers.end());
    }
    return juce::Result::ok();
}

} // namespace

// ---- Studio -----------------------------------------------------------------------

// The synth patch being searched for (matchSynth), on a thread of its own.
struct Studio::SynthSearch {
    std::thread thread;
    std::atomic<bool> cancel { false };
    std::atomic<bool> done { false };
    std::atomic<double> progress { 0.0 };
    std::mutex mutex;
    juce::String message;
    engine::synth_match::Result result;
    engine::synth::Model model { engine::synth::Model::opale };
    juce::String name;
};

Studio::Studio(const bool auxiliary) : auxiliary_(auxiliary)
{
    // The same settings file as CyDAW 1, so the audio device carries over.
    juce::PropertiesFile::Options options;
    options.applicationName = "CyDAW";
    options.folderName = "CyDAW";
    options.filenameSuffix = auxiliary ? ".processing.settings" : ".layout.settings";
    options.osxLibrarySubFolder = "Application Support";
    properties_.setStorageParameters(options);
    // The engine starts with one MIDI track; a session starts empty.
    engine_.clearMidiTracks();
    // Audio an import makes (two mono recordings made one stereo file), when
    // the imported project's folder takes none.
    app::project_import::setImportedAudioFolder(locations::folder(Location::cache).getChildFile("Imported Audio"));
    // Tracks are armed by hand, several at once; picking one arms nothing.
    engine_.setArmFollowsMidiInput(false);
    // Started once the banks' catalogue is read (startSamples).
    samples_ = std::make_unique<app::SampleLibrary>(false);
    semantic_ = std::make_unique<SemanticSearch>(*samples_, aiModels_, companion_);
    applyLocations();
    if (!auxiliary) banks_.loadRegistry();
    // The factory instruments' maps read ahead (26 MB of zones): their list
    // then opens at once.
    if (!auxiliary) {
        std::vector<juce::File> maps;
        const auto folder = app::factoryContentFolder();
        const auto catalogue = juce::JSON::parse(folder.getChildFile("catalogue.json"));
        if (const auto* list = catalogue.getProperty("instruments", {}).getArray())
            for (const auto& item : *list)
                if (const auto map = folder.getChildFile(item.getProperty("map", {}).toString()); map.existsAsFile())
                    maps.push_back(map);
        banks_.preloadMaps(std::move(maps));
    }

    // The factory graphs and the class library every graph refers to.
    {
        app::NodeGraphComponent factory;
        factoryAudioGraph_ = app::cloneJsonValue(factory.serialise());
        if (const auto* object = factoryAudioGraph_.getDynamicObject())
            sharedNodeModules_ = app::cloneJsonValue(object->getProperty("modules"));
        factory.initialiseEmptyMidiTrackGraph();
        factoryMidiGraph_ = app::cloneJsonValue(factory.serialise());
    }
    {
        app::NodeGraphComponent modulation;
        modulation.initialiseValueModulationGraph();
        factoryModulationGraph_ = app::cloneJsonValue(modulation.serialise());
        if (const auto* object = factoryModulationGraph_.getDynamicObject())
            sharedModulationModules_ = app::cloneJsonValue(object->getProperty("modules"));
    }
    resetMasterGraph();
}

void Studio::resetMasterGraph()
{
    restoreMasterGraph({});
}

void Studio::restoreMasterGraph(const juce::var& graphState)
{
    {
        const juce::ScopedValueSetter<bool> quiet(loading_, true);
        masterGraph_ = makeSlot(GraphSlot::Owner::master);
        if (isGraphState(graphState)) {
            mergeModules(sharedNodeModules_, graphState);
            masterGraph_->restore(withLibrary(graphState, factoryAudioGraph_));
        } else {
            // A limiter keeps the mix under 0 dB.
            masterGraph_->restore(withLibrary(juce::var(), factoryAudioGraph_));
            (void) masterGraph_->graph().insertNodeBeforeOutput("limiter");
            graph_layout::arrange(masterGraph_->graph());
        }
    }
    (void) publishGraph(masterTrack(), true);
}

// The file read and decoded on its own thread; what it made is shared with
// that thread until it says it is done (it may outlive the opening).
struct Studio::ImportBuild {
    app::project_import::ImportedProject project;
    // 0 the session cleared, 1 the tracks, 2 their instruments and graphs.
    int phase {};
    std::size_t next {};
    std::vector<TrackRef> tracks;
    // The source track each made track came from (its devices).
    std::vector<std::pair<TrackRef, std::size_t>> made;
    [[nodiscard]] float progress() const
    {
        const auto count = std::max<std::size_t>(1, phase <= 1 ? project.tracks.size() : tracks.size());
        const auto part = static_cast<float>(std::min(next, count)) / static_cast<float>(count);
        return phase == 0 ? 0.0f : phase == 1 ? 0.6f * part : 0.6f + 0.4f * part;
    }
};

struct Studio::Opening {
    struct Read {
        std::atomic<bool> done {};
        juce::var document;
        juce::Result result { juce::Result::ok() };
        juce::StringArray missingModels;
    };
    juce::File file;
    std::shared_ptr<Read> read;
    double started {};
    juce::int64 bytes {};
    // An import: the project read on its thread, then made in steps.
    struct ImportRead {
        std::atomic<bool> done {};
        app::project_import::ImportedProject project;
        juce::Result result { juce::Result::ok() };
    };
    std::shared_ptr<ImportRead> imported;
    std::unique_ptr<ImportBuild> build;
};

Studio::~Studio()
{
    if (synthSearch_ != nullptr) {
        synthSearch_->cancel = true;
        if (synthSearch_->thread.joinable())
            synthSearch_->thread.join();
    }
    alive_.reset();
    background_.removeAllJobs(true, 10'000);
    semantic_.reset();
    while (autosaveWriting_.load())
        juce::Thread::sleep(5);
    engine_.stop();
    if (samples_ != nullptr && !auxiliary_)
        samples_->saveNow();
    clearSession();
}

juce::PropertiesFile& Studio::settings()
{
    return *properties_.getUserSettings();
}

juce::String Studio::startAudio(const bool restoreSavedDevice)
{
    std::unique_ptr<juce::XmlElement> saved;
    if (restoreSavedDevice)
        saved = juce::parseXML(settings().getValue("audioDeviceState"));
    return engine_.initialiseAudio(saved.get());
}

void Studio::saveAudioSetup()
{
    if (auto xml = engine_.deviceManager().createStateXml()) {
        settings().setValue("audioDeviceState", xml->toString());
        settings().saveIfNeeded();
    }
}

juce::String Studio::audioStatus() const
{
    return const_cast<app::AudioTrackController&>(engine_).audioDeviceDescription();
}

// ---- Library helpers ---------------------------------------------------------------

void Studio::mergeModules(juce::var& library, const juce::var& graphState)
{
    const auto* graph = graphState.getDynamicObject();
    if (graph == nullptr)
        return;
    const auto* values = graph->getProperty("modules").getArray();
    const auto* current = library.getArray();
    if (values == nullptr || (values->isEmpty() && current != nullptr && !current->isEmpty()))
        return;
    juce::Array<juce::var> merged;
    if (current != nullptr)
        merged.addArray(*current);
    auto changed = false;
    for (const auto& incoming : *values) {
        const auto key = keyOf(incoming);
        if (key.isEmpty() || isInline(incoming))
            continue;
        auto found = false;
        for (auto& existing : merged)
            if (keyOf(existing) == key) {
                found = true;
                if (existing.getDynamicObject() != incoming.getDynamicObject()) {
                    existing = incoming;
                    changed = true;
                }
                break;
            }
        if (!found) {
            merged.add(incoming);
            changed = true;
        }
    }
    if (changed)
        library = juce::var(merged);
}

juce::var Studio::withoutSharedModules(const juce::var& state)
{
    const auto* graph = state.getDynamicObject();
    if (graph == nullptr)
        return state;
    const auto* modules = graph->getProperty("modules").getArray();
    if (modules == nullptr)
        return state;
    juce::Array<juce::var> kept;
    for (const auto& value : *modules)
        if (isInline(value))
            kept.add(value);
    if (kept.size() == modules->size())
        return state;
    auto shallow = juce::DynamicObject::Ptr(new juce::DynamicObject());
    for (const auto& property : graph->getProperties())
        shallow->setProperty(property.name, property.value);
    shallow->setProperty("modules", juce::var(kept));
    return juce::var(shallow.get());
}

juce::var Studio::withModulationLibrary(const juce::var& state) const
{
    const auto& source = isGraphState(state) ? state : factoryModulationGraph_;
    const auto* graph = source.getDynamicObject();
    if (graph == nullptr)
        return source;
    auto shallow = juce::DynamicObject::Ptr(new juce::DynamicObject());
    for (const auto& property : graph->getProperties())
        shallow->setProperty(property.name, property.value);
    juce::Array<juce::var> merged;
    if (const auto* shared = sharedModulationModules_.getArray())
        merged.addArray(*shared);
    if (const auto* local = graph->getProperty("modules").getArray())
        for (const auto& module : *local) {
            if (!isInline(module))
                continue;
            auto replaced = false;
            for (auto& existing : merged)
                if (keyOf(existing) == keyOf(module)) {
                    existing = module;
                    replaced = true;
                    break;
                }
            if (!replaced)
                merged.add(module);
        }
    shallow->setProperty("modules", juce::var(merged));
    return juce::var(shallow.get());
}

juce::var Studio::withLibrary(const juce::var& state, const juce::var& fallback) const
{
    const auto& source = isGraphState(state) ? state : fallback;
    const auto* graph = source.getDynamicObject();
    if (graph == nullptr)
        return source;
    auto shallow = juce::DynamicObject::Ptr(new juce::DynamicObject());
    for (const auto& property : graph->getProperties())
        shallow->setProperty(property.name, property.value);
    juce::Array<juce::var> merged;
    if (const auto* shared = sharedNodeModules_.getArray())
        merged.addArray(*shared);
    if (const auto* local = graph->getProperty("modules").getArray())
        for (const auto& module : *local) {
            if (!isInline(module))
                continue;
            const auto key = keyOf(module);
            auto replaced = false;
            for (auto& existing : merged)
                if (keyOf(existing) == key) {
                    existing = module;
                    replaced = true;
                    break;
                }
            if (!replaced)
                merged.add(module);
        }
    shallow->setProperty("modules", juce::var(merged));
    return juce::var(shallow.get());
}

// ---- Graph slots ----------------------------------------------------------------------

std::unique_ptr<GraphSlot> Studio::makeSlot(const GraphSlot::Owner owner)
{
    return std::make_unique<GraphSlot>(owner, [this](GraphSlot& slot, const bool structural) {
        graphChanged(slot, structural);
    });
}

AudioTrackState& Studio::adoptAudioTrack(const std::size_t index, const juce::var& graphState)
{
    while (audioTracks_.size() <= index) {
        AudioTrackState state;
        state.uid = nextTrackUid_++;
        state.name = "Audio " + juce::String(static_cast<int>(audioTracks_.size() + 1));
        state.colour = defaultColour(audioTracks_.size() + midiTracks_.size());
        audioTracks_.push_back(std::move(state));
    }
    auto& state = audioTracks_[index];
    state.graph = makeSlot(GraphSlot::Owner::audioTrack);
    if (isGraphState(graphState))
        mergeModules(sharedNodeModules_, graphState);
    state.graph->restore(withLibrary(graphState, factoryAudioGraph_));
    mergeModules(sharedNodeModules_, state.graph->serialise());
    // A track made from a multichannel file has its format already.
    state.audioChannels = engine_.trackChannelCount(index);
    audioMeters_.resize(audioTracks_.size());
    return state;
}

MidiTrackState& Studio::adoptMidiTrack(const std::uint64_t id, const juce::var& graphState)
{
    auto& state = midiTracks_[id];
    state.id = id;
    if (state.colour == 0)
        state.colour = defaultColour(audioTracks_.size() + midiTracks_.size() - 1);
    state.graph = makeSlot(GraphSlot::Owner::midiTrack);
    if (isGraphState(graphState))
        mergeModules(sharedNodeModules_, graphState);
    state.graph->restore(withLibrary(graphState, factoryMidiGraph_));
    mergeModules(sharedNodeModules_, state.graph->serialise());
    return state;
}

BusState& Studio::adoptBus(const std::uint64_t id, const juce::var& graphState)
{
    auto& state = buses_[id];
    state.id = id;
    state.graph = makeSlot(GraphSlot::Owner::bus);
    if (isGraphState(graphState))
        mergeModules(sharedNodeModules_, graphState);
    state.graph->restore(withLibrary(graphState, factoryAudioGraph_));
    return state;
}

GraphSlot* Studio::graphFor(const TrackRef& track)
{
    if (track.pad >= replacerPadBase) {
        auto* slot = replacerSlot(track);
        return slot != nullptr ? slot->graph.get() : nullptr;
    }
    if (track.isBus())
        return busGraph(track.bus);
    if (track.master)
        return masterGraph_.get();
    if (track.midi) {
        const auto found = midiTracks_.find(track.midiId);
        if (found == midiTracks_.end())
            return nullptr;
        if (track.pad >= 0)
            return static_cast<std::size_t>(track.pad) < found->second.pads.size() ? found->second.pads[static_cast<std::size_t>(track.pad)].graph.get()
                                                                                   : nullptr;
        return found->second.graph.get();
    }
    return track.audioIndex < audioTracks_.size() ? audioTracks_[track.audioIndex].graph.get() : nullptr;
}

const GraphSlot* Studio::graphFor(const TrackRef& track) const
{
    return const_cast<Studio*>(this)->graphFor(track);
}

GraphSlot* Studio::busGraph(const std::uint64_t busId)
{
    if (const auto found = buses_.find(busId); found != buses_.end())
        return found->second.graph.get();
    // A bus has its effects from the start: none yet.
    if (!engine_.virtualBus(busId))
        return nullptr;
    return adoptBus(busId, {}).graph.get();
}

std::optional<TrackRef> Studio::trackOf(const GraphSlot& slot) const
{
    if (masterGraph_.get() == &slot)
        return masterTrack();
    for (const auto& [id, bus] : buses_)
        if (bus.graph.get() == &slot)
            return TrackRef::ofBus(id);
    for (std::size_t index = 0; index < audioTracks_.size(); ++index)
        if (audioTracks_[index].graph.get() == &slot)
            return TrackRef { false, index, 0 };
    for (std::size_t index = 0; index < replacerSlots_.size(); ++index)
        if (replacerSlots_[index].graph.get() == &slot) {
            auto track = replacerSlots_[index].owner;
            track.pad = replacerPadBase + static_cast<int>(index);
            return track;
        }
    for (const auto& [id, state] : midiTracks_) {
        if (state.graph.get() == &slot)
            return TrackRef { true, 0, id };
        for (std::size_t pad = 0; pad < state.pads.size(); ++pad)
            if (state.pads[pad].graph.get() == &slot) {
                TrackRef track { true, 0, id };
                track.pad = static_cast<int>(pad);
                return track;
            }
    }
    return std::nullopt;
}

void Studio::graphChanged(GraphSlot& slot, const bool structural)
{
    if (loading_)
        return;
    // A track's modulation: written back as the project keeps it; it moves
    // the parameters again from the next tick.
    if (slot.owner() == GraphSlot::Owner::modulation) {
        for (auto& state : audioTracks_)
            if (state.modulationSlot.get() == &slot) {
                state.modulationGraph = withoutSharedModules(slot.serialise());
                state.modulationAppliedAt = -1.0;
                state.modulationAppliedRevision = 0;
                dirty_ = true;
            }
        return;
    }
    const auto owner = trackOf(slot);
    // A bus's effects belong to the software, not to the project.
    if (owner && owner->isBus())
        virtualIoChanged();
    else
        dirty_ = true;
    if (const auto track = owner) {
        // The instrument the workshop edits is this track's graph.
        if (workshop_ && workshop_->own == nullptr && workshop_->track && *workshop_->track == *track)
            workshop_->unsaved = true;
        (void) publishGraph(*track, structural);
        return;
    }
    for (auto& [id, bus] : buses_)
        if (bus.graph.get() == &slot)
            (void) engine_.setVirtualBusGraph(id, slot.graph().nativeDspGraph());
}

// ---- Routing between tracks ------------------------------------------------------------------

namespace {
constexpr std::uint64_t kMidiRoutingBit = 1ull << 62;
} // namespace

std::uint64_t Studio::routingKey(const TrackRef& track) const
{
    if (track.master)
        return 0;
    if (track.midi)
        return track.midiId | kMidiRoutingBit;
    return track.audioIndex < audioTracks_.size() ? audioTracks_[track.audioIndex].uid : 0;
}

std::optional<TrackRef> Studio::trackForKey(const std::uint64_t key) const
{
    if (key == 0)
        return std::nullopt;
    if ((key & kMidiRoutingBit) != 0) {
        const auto id = key & ~kMidiRoutingBit;
        if (midiTracks_.contains(id))
            return TrackRef { true, 0, id };
        return std::nullopt;
    }
    for (std::size_t index = 0; index < audioTracks_.size() && index < engine_.trackCount(); ++index)
        if (audioTracks_[index].uid == key)
            return TrackRef { false, index, 0 };
    return std::nullopt;
}

std::vector<TrackOutput>* Studio::outputsOf(const TrackRef& track)
{
    if (track.master)
        return nullptr;
    if (track.midi) {
        const auto found = midiTracks_.find(track.midiId);
        return found != midiTracks_.end() ? &found->second.outputs : nullptr;
    }
    return track.audioIndex < audioTracks_.size() ? &audioTracks_[track.audioIndex].outputs : nullptr;
}

std::vector<TrackOutput> Studio::trackOutputs(const TrackRef& track) const
{
    const auto* outputs = const_cast<Studio*>(this)->outputsOf(track);
    if (outputs == nullptr || outputs->empty())
        return { TrackOutput {} };
    return *outputs;
}

int Studio::trackChannels(const TrackRef& track) const
{
    if (track.isBus()) {
        const auto bus = engine_.virtualBus(track.bus);
        return bus ? bus->channels : 2;
    }
    if (track.master || track.midi || !isValid(track))
        return 2;
    return engine_.trackChannelCount(track.audioIndex);
}

juce::String Studio::formatName(const int channels)
{
    switch (cydaw::engine::clampTrackChannels(channels)) {
    case 1: return "Mono";
    case 2: return ui::Ls("Stéréo", "Stereo");
    case 3: return "LCR";
    case 4: return "Quad";
    default: return juce::String(std::string(cydaw::engine::layoutKey(channels)));
    }
}

juce::Result Studio::setTrackChannels(const TrackRef& track, const int channels)
{
    if (track.master || track.midi)
        return juce::Result::fail(ui::Ls("Le master et les pistes instrument restent en stéréo",
                                         "The master and instrument tracks stay stereo"));
    if (!isValid(track))
        return juce::Result::fail(ui::Ls("Piste introuvable", "No such track"));
    const auto count = cydaw::engine::clampTrackChannels(channels);
    if (const auto result = engine_.setTrackChannelCount(track.audioIndex, count); result.failed())
        return result;
    auto& state = audioTracks_[track.audioIndex];
    state.audioChannels = count;
    // The graph's ends say the width too (the runtime takes it anyway).
    if (state.graph != nullptr) {
        auto& graph = state.graph->graph();
        const auto [in, out] = audioEnds(graph);
        for (const auto node : { in, out })
            if (node != 0)
                (void) graph.setGraphMemberType("node." + juce::String(static_cast<juce::int64>(node)),
                                                cydaw::engine::PortKind::audio, count);
        (void) publishGraph(track, true);
    }
    publishRouting();
    dirty_ = true;
    return juce::Result::ok();
}

juce::Result Studio::updateHardwareBus(const app::VirtualAudioBus& bus)
{
    if (!engine_.updateVirtualBus(bus))
        return juce::Result::fail(ui::Ls("Ce bus a refusé ces réglages", "That bus refused those settings"));
    // Tracks sent to an output bus follow its new width.
    publishRouting();
    virtualIoChanged();
    return juce::Result::ok();
}

std::uint64_t Studio::addHardwareBus(const app::VirtualBusDirection direction)
{
    const auto id = engine_.addVirtualBus(direction);
    if (id != 0) {
        (void) busGraph(id);
        virtualIoChanged();
    }
    return id;
}

std::optional<app::VirtualAudioBus> Studio::hardwareBus(const std::uint64_t id) const
{
    return engine_.virtualBus(id);
}

juce::File Studio::virtualIoFile()
{
    // CYDAW_VIRTUAL_IO_FILE: another file (a test's), read and written even
    // by an automated session.
    if (const auto other = juce::SystemStats::getEnvironmentVariable("CYDAW_VIRTUAL_IO_FILE", {}); other.isNotEmpty())
        return juce::File(other);
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("CyDAW")
        .getChildFile("VirtualIO.json");
}

void Studio::loadVirtualIo()
{
    const auto file = virtualIoFile();
    if (!file.existsAsFile())
        return;
    const auto document = juce::JSON::parse(file);
    if (document.getProperty("format", juce::var()).toString() != "CyDAW Virtual IO")
        return;
    const juce::ScopedValueSetter<bool> guard(loading_, true);
    project_io::applyVirtualIo(*this, document);
    virtualIoDirty_ = false;
}

void Studio::flushVirtualIo()
{
    if (!virtualIoDirty_ || !virtualIoSaving_)
        return;
    virtualIoDirty_ = false;
    const auto file = virtualIoFile();
    (void) file.getParentDirectory().createDirectory();
    (void) file.replaceWithText(juce::JSON::toString(project_io::virtualIoDocument(*this)));
}

juce::Result Studio::removeHardwareBus(const std::uint64_t id)
{
    if (!engine_.removeVirtualBus(id))
        return juce::Result::fail(ui::Ls("Ce bus ne peut pas être retiré (il en faut au moins un de chaque sens)",
                                         "That bus cannot be removed (one of each direction must stay)"));
    buses_.erase(id);
    virtualIoChanged();
    // Tracks sent to it go back to the master.
    const auto strip = [id](std::vector<TrackOutput>& outputs) {
        std::erase_if(outputs, [id](const TrackOutput& output) { return output.hardware == id; });
    };
    for (auto& state : audioTracks_)
        strip(state.outputs);
    for (auto& [key, state] : midiTracks_)
        strip(state.outputs);
    publishRouting();
    dirty_ = true;
    return juce::Result::ok();
}

juce::Result Studio::setDeviceChannelCounts(const int inputs, const int outputs)
{
    const auto result = engine_.setDeviceChannelCounts(inputs, outputs);
    if (result.wasOk())
        saveAudioSetup();
    return result;
}

bool Studio::canRoute(const TrackRef& from, const TrackRef& to) const
{
    if (from.master || to.master || from == to || !isValid(from) || !isValid(to))
        return false;
    // No loop: nothing `to` reaches may be `from`.
    const auto fromKey = routingKey(from);
    std::vector<std::uint64_t> pending { routingKey(to) };
    std::vector<std::uint64_t> seen;
    while (!pending.empty()) {
        const auto key = pending.back();
        pending.pop_back();
        if (key == fromKey)
            return false;
        if (std::find(seen.begin(), seen.end(), key) != seen.end())
            continue;
        seen.push_back(key);
        if (const auto track = trackForKey(key))
            for (const auto& output : trackOutputs(*track))
                if (output.destination != 0)
                    pending.push_back(output.destination);
    }
    return true;
}

juce::Result Studio::setTrackOutputs(const TrackRef& track, std::vector<TrackOutput> outputs)
{
    auto* stored = outputsOf(track);
    if (stored == nullptr || !isValid(track))
        return juce::Result::fail(ui::Ls("Piste introuvable", "No such track"));
    for (auto& output : outputs) {
        output.gainDb = juce::jlimit(-60.0f, 12.0f, output.gainDb);
        if (output.hardware != 0) {
            const auto bus = engine_.virtualBus(output.hardware);
            if (!bus || bus->direction != app::VirtualBusDirection::output)
                return juce::Result::fail(ui::Ls("Cette sortie de la carte son n'existe plus", "That sound card output is gone"));
            output.destination = 0;
            output.input = 0;
            continue;
        }
        if (output.destination == 0) {
            output.input = 0;
            continue;
        }
        const auto destination = trackForKey(output.destination);
        if (!destination)
            return juce::Result::fail(ui::Ls("Cette piste de destination n'existe plus", "That destination track is gone"));
        if (!canRoute(track, *destination))
            return juce::Result::fail(ui::Ls("Cette sortie ferait une boucle : ", "This output would make a loop: ")
                                      + trackName(track) + " -> " + trackName(*destination));
        if (output.input != 0) {
            const auto inputs = trackInputs(*destination);
            if (std::none_of(inputs.begin(), inputs.end(), [&output](const TrackInput& input) { return input.node == output.input; }))
                return juce::Result::fail(ui::Ls("Cette entrée de ", "This input of ") + trackName(*destination)
                                          + ui::Ls(" n'existe plus", " is gone"));
        }
    }
    // The master alone is the default: kept empty.
    if (outputs.size() == 1 && outputs.front() == TrackOutput {})
        outputs.clear();
    *stored = std::move(outputs);
    publishRouting();
    dirty_ = true;
    return juce::Result::ok();
}

std::vector<TrackRef> Studio::trackSenders(const TrackRef& track) const
{
    std::vector<TrackRef> senders;
    const auto key = routingKey(track);
    if (key == 0 && !track.master)
        return senders;
    for (const auto& candidate : tracks())
        for (const auto& output : trackOutputs(candidate))
            if (track.master ? output.destination == 0 && output.hardware == 0 : output.destination == key) {
                senders.push_back(candidate);
                break;
            }
    return senders;
}

bool Studio::isBusTrack(const TrackRef& track) const
{
    return !track.master && !track.midi && track.audioIndex < audioTracks_.size() && audioTracks_[track.audioIndex].bus;
}

std::optional<TrackRef> Studio::addBusTrack(const juce::String& name)
{
    std::size_t index {};
    if (engine_.addEmptyAudioTrack(&index).failed())
        return std::nullopt;
    auto& state = adoptAudioTrack(index, {});
    state.bus = true;
    auto buses = 0;
    for (const auto& other : audioTracks_)
        buses += other.bus ? 1 : 0;
    state.name = name.trim().isNotEmpty() ? name.trim() : "Bus " + juce::String(buses);
    const TrackRef track { false, index, 0 };
    (void) publishGraph(track, true);
    publishRouting();
    dirty_ = true;
    return track;
}

void Studio::restoreTrackRouting(const TrackRef& track, std::vector<TrackOutput> outputs, const bool bus)
{
    if (auto* stored = outputsOf(track))
        *stored = std::move(outputs);
    if (!track.midi && track.audioIndex < audioTracks_.size())
        audioTracks_[track.audioIndex].bus = bus;
}

void Studio::forgetDestination(const std::uint64_t key)
{
    if (key == 0)
        return;
    const auto strip = [key](std::vector<TrackOutput>& outputs) {
        std::erase_if(outputs, [key](const TrackOutput& output) { return output.destination == key; });
    };
    for (auto& state : audioTracks_)
        strip(state.outputs);
    for (auto& [id, state] : midiTracks_)
        strip(state.outputs);
}

std::vector<TrackInput> Studio::trackInputs(const TrackRef& track) const
{
    std::vector<TrackInput> inputs;
    const auto* slot = track.master ? nullptr : graphFor(track);
    if (slot == nullptr)
        return inputs;
    // An instrument's sound is its main input: each audio input of its
    // graph is another (the graph knows).
    const auto& graph = slot->graph();
    const auto buses = graph.audioInputBuses();
    const auto nodes = graph.editorNodes();
    for (const auto& [node, bus] : buses) {
        if (bus > maximumTrackInputs)
            break;
        TrackInput input { node, {} };
        for (const auto& candidate : nodes)
            if (candidate.id == node)
                input.name = candidate.alias.trim();
        if (input.name.isEmpty())
            input.name = ui::Ls("Entrée ", "Input ") + juce::String(static_cast<int>(bus) + 1);
        inputs.push_back(std::move(input));
    }
    return inputs;
}

std::optional<std::uint64_t> Studio::addTrackInput(const TrackRef& track, const juce::String& name)
{
    auto* slot = track.master ? nullptr : graphFor(track);
    if (slot == nullptr || trackInputs(track).size() >= maximumTrackInputs)
        return std::nullopt;
    // On the left, under the inputs there.
    auto& graph = slot->graph();
    auto left = 1.0f;
    auto lowest = 0.0f;
    auto found = false;
    for (const auto& node : graph.editorNodes()) {
        left = std::min(left, node.position.x);
        if (node.boundary && (node.typeKey.contains("-input") || node.typeKey == "audio-clips")) {
            lowest = std::max(lowest, node.position.y);
            found = true;
        }
    }
    const juce::Point<float> at(std::max(0.0f, left), found ? std::min(0.92f, lowest + 0.16f) : 0.3f);
    const auto id = graph.editorAddNode("graph-audio-input-2", at);
    if (id == 0)
        return std::nullopt;
    const auto count = static_cast<int>(trackInputs(track).size());
    (void) graph.editorSetAlias(id, name.trim().isNotEmpty() ? name.trim()
                                                             : ui::Ls("Entrée ", "Input ") + juce::String(count + 1));
    dirty_ = true;
    (void) publishGraph(track, true);
    return id;
}

juce::Result Studio::removeTrackInput(const TrackRef& track, const std::uint64_t node)
{
    auto* slot = track.master ? nullptr : graphFor(track);
    const auto inputs = trackInputs(track);
    if (slot == nullptr || std::none_of(inputs.begin(), inputs.end(), [node](const TrackInput& input) { return input.node == node; }))
        return juce::Result::fail(ui::Ls("Cette entrée n'existe pas", "No such input"));
    if (!slot->graph().editorRemoveAudioInput(node))
        return juce::Result::fail(ui::Ls("Cette entrée n'a pas pu être retirée", "This input could not be removed"));
    // What was routed into it goes with it.
    const auto key = routingKey(track);
    const auto strip = [key, node](std::vector<TrackOutput>& outputs) {
        std::erase_if(outputs, [key, node](const TrackOutput& output) { return output.destination == key && output.input == node; });
    };
    for (auto& state : audioTracks_)
        strip(state.outputs);
    for (auto& [id, state] : midiTracks_)
        strip(state.outputs);
    dirty_ = true;
    (void) publishGraph(track, true);
    publishRouting();
    return juce::Result::ok();
}

juce::String Studio::trackInputName(const TrackRef& track, const std::uint64_t node) const
{
    if (node == 0)
        return ui::Ls("entrée principale", "main input");
    for (const auto& input : trackInputs(track))
        if (input.node == node)
            return input.name;
    return ui::Ls("(entrée disparue)", "(input gone)");
}

// ---- Sidechains ----------------------------------------------------------------------------

std::optional<std::uint64_t> Studio::sidechainPort(const TrackRef& track, const std::uint64_t node) const
{
    const auto* slot = track.master ? nullptr : graphFor(track);
    if (slot == nullptr)
        return std::nullopt;
    for (const auto& card : slot->graph().editorNodes())
        if (card.id == node)
            for (const auto& port : card.ports)
                if (port.input && port.kind == 0 && port.key == "sidechain-in")
                    return port.id;
    return std::nullopt;
}

std::optional<std::uint64_t> Studio::sidechainInput(const TrackRef& track, const std::uint64_t node) const
{
    const auto port = sidechainPort(track, node);
    if (!port)
        return std::nullopt;
    const auto inputs = trackInputs(track);
    for (const auto& connection : graphFor(track)->graph().editorConnections())
        if (connection.destination == node && connection.destinationPort == *port
            && std::any_of(inputs.begin(), inputs.end(), [&connection](const TrackInput& input) { return input.node == connection.source; }))
            return connection.source;
    return std::nullopt;
}

bool Studio::hasSidechain(const TrackRef& track, const std::uint64_t node) const
{
    return sidechainPort(track, node).has_value();
}

std::optional<TrackRef> Studio::sidechainSource(const TrackRef& track, const std::uint64_t node) const
{
    const auto input = sidechainInput(track, node);
    if (!input)
        return std::nullopt;
    const auto key = routingKey(track);
    for (const auto& candidate : tracks())
        if (!candidate.master && !(candidate == track))
            for (const auto& output : trackOutputs(candidate))
                if (output.destination == key && output.input == *input)
                    return candidate;
    return std::nullopt;
}

juce::Result Studio::setSidechainSource(const TrackRef& track, const std::uint64_t node, const std::optional<TrackRef>& source)
{
    const auto port = sidechainPort(track, node);
    if (!port)
        return juce::Result::fail(ui::Ls("Ce périphérique n'a pas de sidechain", "This device has no sidechain"));
    if (source && (source->master || *source == track || !isValid(*source) || !canRoute(*source, track)))
        return juce::Result::fail(ui::Ls("Cette piste ne peut pas servir de sidechain ici (elle ferait une boucle)",
                                         "That track cannot feed this sidechain (it would make a loop)"));
    auto input = sidechainInput(track, node);
    const auto key = routingKey(track);
    // What fed it stops: one source at a time.
    if (input)
        for (const auto& candidate : tracks()) {
            if (candidate.master || candidate == track)
                continue;
            auto outputs = trackOutputs(candidate);
            const auto before = outputs.size();
            std::erase_if(outputs, [key, &input](const TrackOutput& output) { return output.destination == key && output.input == *input; });
            if (outputs.size() != before)
                (void) setTrackOutputs(candidate, outputs);
        }
    if (!source) {
        dirty_ = true;
        return juce::Result::ok();
    }
    if (!input) {
        // An input of the track, wired to the socket.
        input = addTrackInput(track, ui::Ls("Sidechain", "Sidechain"));
        if (!input)
            return juce::Result::fail(ui::Ls("Cette piste a déjà toutes ses entrées (3)", "This track has all its inputs already (3)"));
        auto& graph = graphFor(track)->graph();
        std::uint64_t out {};
        for (const auto& card : graph.editorNodes())
            if (card.id == *input)
                for (const auto& candidate : card.ports)
                    if (!candidate.input && candidate.kind == 0 && out == 0)
                        out = candidate.id;
        if (const auto refused = graph.editorConnect(*input, out, node, *port); refused.isNotEmpty()) {
            (void) removeTrackInput(track, *input);
            return juce::Result::fail(refused);
        }
        (void) publishGraph(track, true);
    }
    // Before its fader: the kick ducks the bass however loud it is mixed.
    auto outputs = trackOutputs(*source);
    outputs.push_back({ key, 0.0f, *input, true });
    const auto result = setTrackOutputs(*source, outputs);
    dirty_ = true;
    return result;
}

void Studio::publishRouting()
{
    std::vector<app::AudioTrackController::TrackRouting> routing;
    publishedInputs_.clear();
    const auto every = allTracks();
    for (const auto& track : every) {
        std::vector<std::uint64_t> nodes;
        for (const auto& input : trackInputs(track))
            nodes.push_back(input.node);
        if (!nodes.empty())
            publishedInputs_[routingKey(track)] = std::move(nodes);
    }
    const auto routeOf = [](const TrackRef& track) {
        app::AudioTrackController::TrackRoute route;
        route.midi = track.midi;
        route.index = track.audioIndex;
        route.id = track.midiId;
        return route;
    };
    for (const auto& track : every) {
        app::AudioTrackController::TrackRouting entry;
        entry.track = routeOf(track);
        for (const auto& output : trackOutputs(track)) {
            app::AudioTrackController::TrackOutputRoute out;
            out.gainDb = output.gainDb;
            out.preFader = output.preFader;
            out.hardwareBus = output.hardware;
            out.hardwareAfterEffects = output.hardwareAfterEffects;
            if (output.hardware == 0 && output.destination != 0) {
                const auto destination = trackForKey(output.destination);
                if (!destination)
                    continue;
                out.destination = routeOf(*destination);
                if (output.input != 0) {
                    // Its input's place in the destination's graph; an
                    // input gone takes nothing.
                    const auto found = publishedInputs_.find(output.destination);
                    if (found == publishedInputs_.end())
                        continue;
                    const auto at = std::find(found->second.begin(), found->second.end(), output.input);
                    if (at == found->second.end())
                        continue;
                    out.inputBus = static_cast<std::uint32_t>(at - found->second.begin()) + 1;
                }
            }
            entry.outputs.push_back(out);
        }
        routing.push_back(std::move(entry));
    }
    if (const auto applied = engine_.setTrackRouting(routing); applied.failed()) {
        // A loop read from a file: every track to the master, rather than none.
        for (auto& entry : routing)
            entry.outputs.clear();
        (void) engine_.setTrackRouting(routing);
    }
}

juce::Result Studio::publishGraph(const TrackRef& track, const bool structural)
{
    // A replacer's instrument: compiled, kept on its card, its track's graph
    // published again (the replacer plays the new plan at once).
    if (track.pad >= replacerPadBase) {
        auto* replacer = replacerSlot(track);
        if (replacer == nullptr)
            return juce::Result::fail(ui::Ls("Instrument introuvable", "No such instrument"));
        compileReplacer(*replacer);
        dirty_ = true;
        return publishGraph(replacer->owner, false);
    }
    auto* slot = graphFor(track);
    if (slot == nullptr)
        return juce::Result::fail(ui::Ls("Piste introuvable", "No such track"));
    auto& graph = slot->graph();
    auto result = juce::Result::ok();
    if (track.isBus()) {
        // A virtual input's or output's effects.
        auto native = graph.nativeDspGraph();
        attachReplacerInstruments(track, native);
        result = engine_.updateVirtualBusGraphParameters(track.bus, native);
        if (result.failed())
            result = engine_.setVirtualBusGraph(track.bus, native);
        graph.setDspCompilationStatus(result, native.nodes.size());
        virtualIoChanged();
        return result;
    }
    if (track.master) {
        // Into the master's output, before its fader.
        auto native = graph.nativeDspGraph();
        attachReplacerInstruments(track, native);
        result = engine_.updateMasterNativeDspParameters(native);
        if (result.failed())
            result = engine_.setMasterNativeDspGraph(native);
        graph.setDspCompilationStatus(result, native.nodes.size());
    } else if (!track.midi) {
        auto native = graph.nativeDspGraph();
        attachReplacerInstruments(track, native);
        if (track.audioIndex < audioTracks_.size() && audioTracks_[track.audioIndex].modulationActive)
            modulateNativeGraph(audioTracks_[track.audioIndex], native);
        // Values only when the shape is the same; the engine refuses a
        // changed shape and the graph is compiled again.
        // A structural change may still keep the shape (a card moved); the
        // engine tells, and the graph is compiled again only when it differs.
        result = engine_.updateTrackNativeDspParameters(track.audioIndex, native);
        if (result.failed())
            result = engine_.setTrackNativeDspGraph(track.audioIndex, native);
        juce::ignoreUnused(structural);
        graph.setDspCompilationStatus(result, native.nodes.size());
    } else if (track.pad >= 0) {
        // A pad of a rack: its instrument and its effects, played by its key.
        engine::PolySynthGraphPlan plan;
        const auto compiled = graph.compilePolySynthGraph(plan);
        auto chain = graph.nativeDspGraph();
        attachReplacerInstruments(track, chain);
        result = engine_.setMidiTrackRackPad(track.midiId, static_cast<std::size_t>(track.pad), plan, chain);
        if (compiled.failed())
            result = compiled;
        publishRackPadMix(track.owner(), static_cast<std::size_t>(track.pad));
        slot->setLastCompileError(result.failed() ? result.getErrorMessage() : juce::String());
        return result;
    } else {
        engine::PolySynthGraphPlan plan;
        const auto compiled = graph.compilePolySynthGraph(plan);
        if (compiled.wasOk())
            (void) engine_.setMidiTrackInstrumentGraph(track.midiId, plan);
        // Its pads or layers sound only while its instrument is their container.
        publishPadMixes(track);
        engine_.setMidiTrackVoiceModulations(track.midiId, graph.editorVoiceModulations());
        auto chain = graph.nativeDspGraph();
        attachReplacerInstruments(track, chain);
        result = engine_.updateMidiTrackEffectParameters(track.midiId, chain);
        if (result.failed())
            result = engine_.setMidiTrackEffectGraph(track.midiId, chain);
        if (compiled.failed())
            result = compiled;
    }
    slot->setLastCompileError(result.failed() ? result.getErrorMessage() : juce::String());
    // The nodes the compiler makes inside a module can take other ids when
    // the graph's shape changes: their lanes follow.
    if (structural && !track.master)
        publishParameterAutomation(track);
    // Its inputs changed (one added, one removed in the graph): what is
    // routed into them follows.
    if (structural && !track.master) {
        std::vector<std::uint64_t> nodes;
        for (const auto& input : trackInputs(track))
            nodes.push_back(input.node);
        const auto published = publishedInputs_.find(routingKey(track));
        const auto before = published != publishedInputs_.end() ? published->second : std::vector<std::uint64_t> {};
        if (nodes != before)
            publishRouting();
    }
    return result;
}

// ---- A Replacer's instrument ------------------------------------------------------------------

bool Studio::isReplacerPad(const TrackRef& track) const
{
    return track.pad >= replacerPadBase
           && static_cast<std::size_t>(track.pad - replacerPadBase) < replacerSlots_.size();
}

Studio::ReplacerSlot* Studio::replacerSlot(const TrackRef& track)
{
    if (!isReplacerPad(track))
        return nullptr;
    return &replacerSlots_[static_cast<std::size_t>(track.pad - replacerPadBase)];
}

Studio::ReplacerSlot* Studio::replacerSlotFor(const TrackRef& owner, const std::uint64_t head, const bool make)
{
    for (auto& slot : replacerSlots_)
        if (slot.head == head && slot.owner == owner && slot.owner.bus == owner.bus && slot.owner.master == owner.master)
            return &slot;
    if (!make)
        return nullptr;
    // Made from what its card keeps.
    auto* ownerSlot = graphFor(owner);
    if (ownerSlot == nullptr)
        return nullptr;
    const auto kept = ownerSlot->graph().nodeInstrumentJson(head);
    if (!kept.getProperty("graph", juce::var()).isObject())
        return nullptr;
    ReplacerSlot slot;
    slot.owner = owner;
    slot.head = head;
    slot.name = kept.getProperty("name", juce::var()).toString();
    slot.graph = makeSlot(GraphSlot::Owner::midiTrack);
    slot.graph->restore(withLibrary(kept.getProperty("graph", juce::var()), factoryMidiGraph_));
    replacerSlots_.push_back(std::move(slot));
    auto& made = replacerSlots_.back();
    engine::PolySynthGraphPlan plan;
    if (made.graph->graph().compilePolySynthGraph(plan).wasOk())
        made.plan = std::make_shared<const engine::PolySynthGraphPlan>(std::move(plan));
    return &made;
}

void Studio::compileReplacer(ReplacerSlot& slot)
{
    engine::PolySynthGraphPlan plan;
    const auto compiled = slot.graph->graph().compilePolySynthGraph(plan);
    slot.plan = compiled.wasOk() ? std::make_shared<const engine::PolySynthGraphPlan>(std::move(plan)) : nullptr;
    slot.graph->setLastCompileError(compiled.failed() ? compiled.getErrorMessage() : juce::String());
    // Kept on the card: saved with the track.
    if (auto* ownerSlot = graphFor(slot.owner)) {
        auto kept = juce::DynamicObject::Ptr(new juce::DynamicObject());
        kept->setProperty("name", slot.name);
        kept->setProperty("graph", withoutSharedModules(slot.graph->serialise()));
        // Its plan, compiled: what an exported plug-in plays.
        if (slot.plan != nullptr)
            kept->setProperty("plan", app::serialisePolySynthGraphPlan(*slot.plan));
        (void) ownerSlot->graph().setNodeInstrumentJson(slot.head, juce::var(kept.get()));
    }
}

std::optional<TrackRef> Studio::replacerInstrument(const TrackRef& owner, const std::uint64_t head)
{
    auto* slot = replacerSlotFor(owner, head, true);
    if (slot == nullptr)
        return std::nullopt;
    auto track = owner;
    track.pad = replacerPadBase + static_cast<int>(slot - replacerSlots_.data());
    return track;
}

juce::String Studio::replacerInstrumentName(const TrackRef& owner, const std::uint64_t head)
{
    const auto* slot = replacerSlotFor(owner, head, true);
    return slot != nullptr ? slot->name : juce::String();
}

juce::String Studio::padName(const TrackRef& track) const
{
    if (track.pad >= replacerPadBase) {
        if (!isReplacerPad(track))
            return {};
        const auto& slot = replacerSlots_[static_cast<std::size_t>(track.pad - replacerPadBase)];
        return slot.name.isNotEmpty() ? slot.name : juce::String("Instrument");
    }
    if (const auto* pad = track.pad >= 0 ? rackPad(track.owner(), static_cast<std::size_t>(track.pad)) : nullptr)
        return pad->name;
    return trackName(track);
}

juce::Result Studio::setReplacerInstrument(const TrackRef& owner, const std::uint64_t head, const juce::String& instrumentKey)
{
    auto* ownerSlot = graphFor(owner);
    if (ownerSlot == nullptr)
        return juce::Result::fail(ui::Ls("Piste introuvable", "No such track"));
    const auto container = ownerSlot->graph().editorContainer(head);
    if (!container || container->typeKey != "replacer")
        return juce::Result::fail("no Replacer " + juce::String(static_cast<juce::int64>(head)) + " on that track");
    auto* slot = replacerSlotFor(owner, head, true);
    if (slot == nullptr) {
        ReplacerSlot made;
        made.owner = owner;
        made.head = head;
        made.graph = makeSlot(GraphSlot::Owner::midiTrack);
        made.graph->restore(withLibrary(juce::var(), factoryMidiGraph_));
        replacerSlots_.push_back(std::move(made));
        slot = &replacerSlots_.back();
    }
    auto ref = owner;
    ref.pad = replacerPadBase + static_cast<int>(slot - replacerSlots_.data());
    // An instrument of CyDAW's or of the factory's (its kit for a Batterie,
    // its samples for a sampler), named in English (the card's).
    juce::String error;
    const auto name = applyInstrument(ref, instrumentKey, error);
    if (!name)
        return juce::Result::fail(error);
    // The slots may have moved (made again from their cards).
    slot = replacerSlotFor(owner, head, true);
    if (slot == nullptr)
        return juce::Result::fail("no such Replacer");
    slot->name = *name;
    // Its sound is now the instrument.
    if (const auto* effect = engine::effectForType("replacer"))
        if (const auto* generator = engine::effectParameter(*effect, "generator"))
            (void) ownerSlot->graph().editorSetParameter(
                "node." + juce::String(static_cast<juce::int64>(head)) + "." + juce::String(std::string(generator->suffix)),
                engine::container::instrumentGenerator);
    dirty_ = true;
    return publishGraph(ref, true);
}

void Studio::attachReplacerInstruments(const TrackRef& track, engine::NativeDspGraph& native)
{
    for (auto& node : native.nodes)
        if (node.kind == engine::NativeDspNodeKind::replacer)
            if (const auto* slot = replacerSlotFor(track, node.id, true))
                node.instrument = slot->plan;
}

juce::Result Studio::insertDeviceInBranch(const TrackRef& track, const juce::String& key, const std::uint64_t head, const int branch,
                                          const std::uint64_t before)
{
    auto* slot = graphFor(track);
    if (slot == nullptr)
        return juce::Result::fail(ui::Ls("Piste introuvable", "No such track"));
    auto& graph = slot->graph();
    const auto catalogue = graph.deviceCatalogue();
    const auto device = std::find_if(catalogue.begin(), catalogue.end(), [&key](const auto& entry) { return entry.key == key; });
    if (device == catalogue.end())
        return juce::Result::fail("no device \"" + key + "\"");
    if (device->soundSource || device->input != "audio" || device->output != "audio")
        return juce::Result::fail("\"" + device->name + "\" " + ui::Ls("ne traite pas le son : pas dans un conteneur",
                                                                      "does not process sound: not in a container"));
    if (graph.editorInsertInBranch(head, branch, key, before) == 0)
        return juce::Result::fail(ui::Ls("Le périphérique n'a pas pu entrer dans cette branche", "The device could not go into that branch"));
    graph_layout::arrange(graph);
    dirty_ = true;
    return publishGraph(track, true);
}

juce::Result Studio::insertDeviceBefore(const TrackRef& track, const juce::String& key, const std::uint64_t before)
{
    std::vector<std::uint64_t> had;
    if (const auto* slot = graphFor(track))
        for (const auto& node : slot->graph().editorNodes())
            had.push_back(node.id);
    const auto result = insertDevice(track, key);
    return result.failed() || before == 0 ? result : placeNewCard(track, had, before);
}

juce::Result Studio::insertLibraryDeviceBefore(const TrackRef& track, const juce::File& file, const std::uint64_t before)
{
    std::vector<std::uint64_t> had;
    if (const auto* slot = graphFor(track))
        for (const auto& node : slot->graph().editorNodes())
            had.push_back(node.id);
    const auto result = insertLibraryDevice(track, file);
    return result.failed() || before == 0 ? result : placeNewCard(track, had, before);
}

juce::Result Studio::placeNewCard(const TrackRef& track, const std::vector<std::uint64_t>& had, const std::uint64_t before)
{
    auto* slot = graphFor(track);
    const auto known = [&had](const std::uint64_t id) { return std::find(had.begin(), had.end(), id) != had.end(); };
    if (slot == nullptr || !known(before))
        return juce::Result::ok();
    auto& graph = slot->graph();
    // The card made: the new one on the chain itself (a container's head,
    // its end and branches inside it). Only one: else left where it went.
    const auto inside = graph.editorContainerMembers();
    std::uint64_t made {};
    for (const auto& node : graph.editorNodes())
        if (!node.boundary && !known(node.id) && !inside.contains(node.id)) {
            if (made != 0)
                return juce::Result::ok();
            made = node.id;
        }
    if (made == 0 || !graph.editorMoveCard(made, 0, -1, before))
        return juce::Result::ok();
    graph_layout::arrange(graph);
    dirty_ = true;
    return publishGraph(track, true);
}

juce::Result Studio::insertDevice(const TrackRef& track, const juce::String& key)
{
    auto* slot = graphFor(track);
    if (slot == nullptr)
        return juce::Result::fail(ui::Ls("Piste introuvable", "No such track"));
    auto& graph = slot->graph();
    const auto catalogue = graph.deviceCatalogue();
    const auto device = std::find_if(catalogue.begin(), catalogue.end(),
                                     [&key](const auto& entry) { return entry.key == key; });
    if (device == catalogue.end())
        return juce::Result::fail("no device \"" + key + "\"");
    if (!track.midi && device->soundSource)
        return juce::Result::fail("\"" + device->name + "\" is an instrument");
    // A whole chain of effects takes every card's place: the track's preset
    // does that, never an added device.
    if (device->kind == "preset" && !device->soundSource)
        return juce::Result::fail("\"" + device->name + "\" "
                                  + ui::Ls("est une chaîne entière : le bouton Preset de piste (au-dessus des entrées de la piste, "
                                           "dans Périphériques) la met à la place de toute la chaîne",
                                           "is a whole chain: the Track preset button (above the track's inputs, in Devices) "
                                           "puts it in place of the whole chain"));
    // An instrument container's card: the instrument that is it (its pads or layers made).
    if (const auto kind = engine::instrument_container::kindOfType(key.toStdString());
        track.midi && track.pad < 0 && kind != engine::instrument_container::Kind::none)
        return replaceInstrument(track, engine::instrument_container::instrumentOf(kind), engine::instrument_container::name(kind));
    // An instrument on an instrument track: in place of the one it plays,
    // the effects after it kept.
    if (track.midi && device->kind == "preset" && device->soundSource)
        return replaceInstrument(track, key, device->name);
    // A synth's own card taken from the modules (Opale, Prisme, Kick, a
    // Batterie): alone it has no voices for the notes, and nothing wires it
    // in. On an instrument track, the whole instrument it is the heart of
    // goes in place of the one it plays (a missing plug-in's too).
    if (const auto instrument = track.midi && device->kind != "preset" ? instrumentOfCard(key) : juce::String(); instrument.isNotEmpty())
        if (const auto whole = std::find_if(catalogue.begin(), catalogue.end(),
                                            [&instrument](const auto& entry) { return entry.key == instrument; });
            whole != catalogue.end())
            return replaceInstrument(track, instrument, whole->englishName.isNotEmpty() ? whole->englishName : whole->name);
    // A container: its head and end, its branches empty, before the output.
    if (const auto* effect = engine::effectForType(key.toStdString()); effect != nullptr && effect->container) {
        if (graph.editorInsertContainer(key, 0) == 0)
            return juce::Result::fail("the container could not be added");
        graph_layout::arrange(graph);
        dirty_ = true;
        return publishGraph(track, true);
    }
    // A MIDI card (an arpeggiator, chords…) goes before the instrument.
    const auto notes = device->input == "midi" && device->output == "midi";
    if (notes && !track.midi)
        return juce::Result::fail("\"" + device->name + "\" " + ui::Ls("joue sur les notes : une piste instrument", "works on notes: an instrument track"));
    auto applied = device->kind == "preset" ? graph.insertFactoryDevice(key)
                   : notes                  ? graph.insertNodeBeforeInstrument(key)
                   : device->input == "none" ? graph.insertNode(key)
                                             : graph.insertNodeBeforeOutput(key);
    if (applied.failed() && device->kind != "preset")
        applied = graph.insertFactoryDevice(key);
    if (applied.failed())
        return applied;
    graph_layout::arrange(graph);
    dirty_ = true;
    // A Batterie comes with a kit: the first, if its card has none yet.
    for (const auto& node : graph.editorNodes())
        if (node.typeKey == "batterie" && !graph.nodeSamplerJson(node.id).isObject()) {
            juce::String error;
            if (!setBatterieKit(track, node.id, 0, error))
                return juce::Result::fail(error);
        }
    return publishGraph(track, true);
}

juce::String Studio::instrumentOfCard(const juce::String& key)
{
    if (const auto model = engine::synth::modelForType(key.toStdString()); model != engine::synth::Model::none)
        return juce::String(engine::synth::info(model).instrument);
    if (key == "prism-synth")
        return "prism-instrument";
    if (key == "kick-synth")
        return "kick-instrument";
    if (key == "batterie")
        return "batterie-instrument";
    return {};
}

std::uint64_t Studio::addMissingPlugin(const TrackRef& track, const juce::String& name, const juce::String& vendor,
                                       const juce::String& format, const bool instrument)
{
    auto* slot = graphFor(track);
    if (slot == nullptr || (instrument && !track.midi))
        return 0;
    auto& graph = slot->graph();
    const auto type = instrument ? juce::String("missing-instrument") : juce::String("missing-plugin");
    // The card made now: the one of its type that was not there before.
    std::set<std::uint64_t> before;
    for (const auto& node : graph.editorNodes())
        if (node.typeKey == type)
            before.insert(node.id);
    const auto placed = instrument ? graph.insertFactoryDevice(type) : graph.insertNodeBeforeOutput(type);
    if (placed.failed())
        return 0;
    std::uint64_t id {};
    for (const auto& node : graph.editorNodes())
        if (node.typeKey == type && !before.contains(node.id))
            id = node.id;
    if (id == 0)
        return 0;
    (void) graph.editorSetAlias(id, name.isNotEmpty() ? name : juce::String("Plugin"));
    auto* about = new juce::DynamicObject();
    about->setProperty("name", name);
    about->setProperty("vendor", vendor);
    about->setProperty("format", format);
    about->setProperty("instrument", instrument);
    (void) graph.editorSetDescription(id, juce::JSON::toString(juce::var(about), true));
    graph_layout::arrange(graph);
    dirty_ = true;
    return id;
}

int Studio::setSynthSettings(const TrackRef& track, const std::vector<std::pair<std::string, float>>& settings)
{
    namespace synth = engine::synth;
    auto* slot = graphFor(track);
    if (slot == nullptr)
        return 0;
    auto& graph = slot->graph();
    for (const auto& node : graph.editorNodes()) {
        const auto model = synth::modelForType(node.typeKey.toStdString());
        if (model == synth::Model::none)
            continue;
        const auto prefix = "node." + juce::String(static_cast<juce::int64>(node.id)) + "." + juce::String(synth::info(model).prefix) + ".";
        auto set = 0;
        for (const auto& [key, value] : settings)
            if (const auto index = synth::parameterIndex(model, key); index >= 0)
                set += graph.editorSetParameter(prefix + juce::String(key), synth::clampValue(model, index, value)) ? 1 : 0;
        dirty_ = true;
        return set;
    }
    return 0;
}

juce::Result Studio::replaceInstrument(const TrackRef& track, const juce::String& key, const juce::String& name)
{
    auto* slot = graphFor(track);
    // An instrument track's, a pad's, a Replacer's (on any track).
    if (slot == nullptr || (!track.midi && !isReplacerPad(track)))
        return juce::Result::fail(ui::Ls("Piste instrument introuvable", "No such instrument track"));
    auto& graph = slot->graph();
    // Its name, when it was what it played (or the default one), follows.
    const auto before = trackName(track);
    // Only a replaced instrument renames it (a new track keeps its name).
    auto replacing = false;
    auto namedAfterInstrument = before.startsWith("Inst ") || before.startsWith("Instrument ");
    for (const auto& node : graph.editorNodes()) {
        if (!graph.editorIsVoiceNode(node.id) || node.typeKey == "voice-allocator" || node.typeKey == "envelope"
            || node.typeKey == "gain" || node.typeKey == "voice-filter")
            continue;
        replacing = true;
        // The card's name, or the sounds it was given (a factory instrument's map, a model).
        const auto mapName = graph.nodeSamplerJson(node.id).getProperty("name", juce::var()).toString();
        namedAfterInstrument = namedAfterInstrument || before.equalsIgnoreCase(node.title) || before.equalsIgnoreCase(node.typeTitle)
                               || (mapName.isNotEmpty() && before.equalsIgnoreCase(mapName)) || before.endsWith(ui::Ls(" (modèle)", " (model)"));
    }
    // Sampler maps and sound models are the instrument's: they go with it.
    if (const auto applied = graph.replaceInstrument(key); applied.failed())
        return applied;
    // A synth's own card left on its own (no cable in or out: taken from the
    // modules before, it never played): the instrument now there replaces it.
    {
        const auto cables = graph.editorConnections();
        std::vector<std::uint64_t> loose;
        for (const auto& node : graph.editorNodes())
            if (!node.boundary && instrumentOfCard(node.typeKey).isNotEmpty()
                && std::none_of(cables.begin(), cables.end(), [&node](const auto& cable) {
                       return cable.source == node.id || cable.destination == node.id;
                   }))
                loose.push_back(node.id);
        if (!loose.empty())
            (void) graph.editorRemoveNodes(loose);
    }
    graph_layout::arrange(graph);
    dirty_ = true;
    for (const auto& node : graph.editorNodes())
        if (node.typeKey == "batterie" && !graph.nodeSamplerJson(node.id).isObject()) {
            juce::String error;
            if (!setBatterieKit(track, node.id, 0, error))
                return juce::Result::fail(error);
        }
    if (replacing && namedAfterInstrument && name.isNotEmpty() && track.pad < 0)
        setTrackName(track, name);
    // An instrument container made: its first pads or layers, when it has
    // none of its kind (a drum machine's pads are no layers).
    if (const auto kind = engine::instrument_container::kindOfInstrument(key.toStdString());
        track.pad < 0 && kind != engine::instrument_container::Kind::none) {
        if (const auto found = midiTracks_.find(track.midiId); found != midiTracks_.end()) {
            if (found->second.padsKind != kind && !found->second.pads.empty())
                clearPads(track);
            found->second.padsKind = kind;
        }
        const auto published = publishGraph(track, true);
        if (rackPadCount(track) == 0) {
            if (kind == engine::instrument_container::Kind::drum)
                fillDefaultRack(track);
            else
                fillDefaultLayers(track);
        }
        return published;
    }
    return publishGraph(track, true);
}

// ---- Tracks ---------------------------------------------------------------------------

std::vector<TrackRef> Studio::tracks() const
{
    // Every track, by its key; the hybrid tracks' companions are not shown.
    std::unordered_set<std::uint64_t> companions;
    for (const auto& [owner, companion] : companions_)
        companions.insert(companion);
    std::vector<std::pair<std::uint64_t, TrackRef>> every;
    every.reserve(engine_.trackCount() + midiTracks_.size());
    for (std::size_t index = 0; index < engine_.trackCount(); ++index)
        if (const auto key = routingKey(TrackRef { false, index, 0 }); key == 0 || !companions.contains(key))
            every.emplace_back(key, TrackRef { false, index, 0 });
    for (const auto& summary : engine_.midiTrackSummaries())
        if (const auto key = routingKey(TrackRef { true, 0, summary.id }); !companions.contains(key))
            every.emplace_back(key, TrackRef { true, 0, summary.id });
    // In the order shown: those gone dropped, the new ones at the end.
    std::unordered_map<std::uint64_t, TrackRef> byKey;
    for (const auto& [key, track] : every)
        if (key != 0)
            byKey.emplace(key, track);
    std::vector<std::uint64_t> order;
    order.reserve(every.size());
    std::unordered_set<std::uint64_t> placed;
    for (const auto key : trackOrder_)
        if (byKey.contains(key) && placed.insert(key).second)
            order.push_back(key);
    for (const auto& [key, track] : every)
        if (key != 0 && placed.insert(key).second)
            order.push_back(key);
    trackOrder_ = order;
    std::vector<TrackRef> out;
    out.reserve(every.size());
    for (const auto key : order)
        out.push_back(byKey.at(key));
    // A track without a key yet (being made): at the very end.
    for (const auto& [key, track] : every)
        if (key == 0)
            out.push_back(track);
    return out;
}

void Studio::setTrackOrder(std::vector<std::uint64_t> keys)
{
    trackOrder_ = std::move(keys);
    (void) tracks();
}

std::vector<TrackRef> Studio::shownTracks() const
{
    auto shown = tracks();
    std::erase_if(shown, [this](const TrackRef& track) { return hiddenByFold(track); });
    return shown;
}

// ---- Hybrid tracks -----------------------------------------------------------------------

namespace {
// A track's launcher clip in a scene (further down, with the launcher's).
const app::AudioTrackController::MidiClip* midiClipInScene(const app::AudioTrackController& engine, std::uint64_t trackId, int scene);
} // namespace

std::optional<TrackRef> Studio::companionOf(const TrackRef& track) const
{
    if (track.master)
        return std::nullopt;
    const auto found = companions_.find(routingKey(track.owner()));
    return found != companions_.end() ? trackForKey(found->second) : std::nullopt;
}

bool Studio::isCompanion(const TrackRef& track) const
{
    if (track.master)
        return false;
    const auto key = routingKey(track.owner());
    return key != 0 && std::any_of(companions_.begin(), companions_.end(), [key](const auto& entry) { return entry.second == key; });
}

TrackRef Studio::shownTrackOf(const TrackRef& track) const
{
    if (track.master)
        return track;
    const auto key = routingKey(track.owner());
    if (key != 0)
        for (const auto& [owner, companion] : companions_)
            if (companion == key)
                if (const auto found = trackForKey(owner))
                    return *found;
    return track;
}

std::optional<TrackRef> Studio::partFor(const TrackRef& track, const bool midi) const
{
    if (track.master || !isValid(track))
        return std::nullopt;
    if (track.midi == midi)
        return track.owner();
    if (const auto companion = companionOf(track); companion && companion->midi == midi)
        return companion;
    return std::nullopt;
}

std::optional<TrackRef> Studio::makePartFor(const TrackRef& track, const bool midi)
{
    if (const auto part = partFor(track, midi))
        return part;
    const auto owner = track.owner();
    if (owner.master || !isValid(owner) || isBusTrack(owner) || isCompanion(owner))
        return std::nullopt;
    const auto ownerKey = routingKey(owner);
    if (ownerKey == 0)
        return std::nullopt;
    const auto companion = midi ? addMidiTrack(trackName(owner), "opale-instrument") : addAudioTrack({}, 0.0);
    if (!companion)
        return std::nullopt;
    companions_[ownerKey] = routingKey(*companion);
    setTrackName(*companion, trackName(owner));
    setTrackColour(*companion, trackColour(owner));
    // Its sound into the track's main input; its solo the track's.
    (void) setTrackOutputs(*companion, { TrackOutput { ownerKey, 0.0f } });
    auto mix = trackMix(*companion);
    mix.solo = trackMix(owner).solo;
    setTrackMix(*companion, mix);
    (void) tracks();
    publishRouting();
    dirty_ = true;
    return companion;
}

std::optional<TrackRef> Studio::slotPart(const TrackRef& track, const int scene) const
{
    if (track.master || !isValid(track))
        return std::nullopt;
    const auto has = [this, scene](const TrackRef& part) {
        return part.midi ? midiClipInScene(engine_, part.midiId, scene) != nullptr : engine_.launcherClip(part.audioIndex, scene).has_value();
    };
    if (has(track))
        return track;
    if (const auto companion = companionOf(track); companion && has(*companion))
        return companion;
    return std::nullopt;
}

std::vector<TrackRef> Studio::allTracks() const
{
    auto all = tracks();
    for (const auto& [owner, companion] : companions_)
        if (const auto track = trackForKey(companion); track && trackForKey(owner))
            all.push_back(*track);
    return all;
}

void Studio::restoreCompanion(const std::uint64_t track, const std::uint64_t companion, const bool recordsAudio)
{
    if (track != 0 && companion != 0 && track != companion) {
        companions_[track] = companion;
        if (recordsAudio)
            hybridAudioRecords_.insert(track);
    }
}

bool Studio::recordsHybridAudio(const TrackRef& track) const
{
    return !track.master && hybridAudioRecords_.contains(routingKey(track.owner()));
}

void Studio::setRecordsHybridAudio(const TrackRef& track, const bool records)
{
    const auto key = routingKey(track.owner());
    if (key == 0)
        return;
    if (records)
        hybridAudioRecords_.insert(key);
    else
        hybridAudioRecords_.erase(key);
    dirty_ = true;
}

std::optional<TrackRef> Studio::newSlotAudioClip(const TrackRef& track, const int scene)
{
    if (scene < 0 || scene >= app::AudioTrackController::maximumLauncherScenes)
        return std::nullopt;
    const auto part = makePartFor(track, false);
    if (!part)
        return std::nullopt;
    // One clip a slot: a MIDI clip there goes.
    if (const auto there = slotPart(track, scene); there && there->midi)
        if (const auto* clip = midiClipInScene(engine_, there->midiId, scene))
            (void) engine_.removeMidiClip(there->midiId, clip->id);
    if (engine_.launcherClip(part->audioIndex, scene))
        return part;
    const auto bar = 60.0 / std::max(1.0, engine_.tempo()) * std::max(1.0, static_cast<double>(engine_.beatsPerBar()));
    if (!engine_.createEmptyLauncherClip(part->audioIndex, scene, ui::Ls("Clip audio", "Audio clip"), bar, trackColour(track)))
        return std::nullopt;
    dirty_ = true;
    return part;
}

std::optional<std::pair<TrackRef, std::uint64_t>> Studio::newSlotMidiClip(const TrackRef& track, const int scene)
{
    if (scene < 0 || scene >= app::AudioTrackController::maximumLauncherScenes)
        return std::nullopt;
    const auto part = makePartFor(track, true);
    if (!part)
        return std::nullopt;
    // One clip a slot: an audio clip there goes.
    if (const auto there = slotPart(track, scene); there && !there->midi)
        (void) engine_.clearLauncherClip(there->audioIndex, scene);
    if (const auto* clip = midiClipInScene(engine_, part->midiId, scene))
        return std::make_pair(*part, clip->id);
    const auto id = engine_.addMidiClip(part->midiId, {}, 0.0, 4.0, true, trackColour(track), scene);
    if (id == 0)
        return std::nullopt;
    dirty_ = true;
    return std::make_pair(*part, id);
}

juce::Result Studio::placeSlotAudio(const TrackRef& track, const int scene, const juce::File& file)
{
    if (scene < 0 || scene >= app::AudioTrackController::maximumLauncherScenes)
        return juce::Result::fail(ui::Ls("Pas de case ici", "No slot there"));
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (!reader || reader->sampleRate <= 0)
        return juce::Result::fail(ui::Ls("Fichier audio illisible : ", "Unreadable audio file: ") + file.getFileName());
    const auto part = makePartFor(track, false);
    if (!part)
        return juce::Result::fail(ui::Ls("Cette piste ne prend pas de clip audio", "That track takes no audio clip"));
    // One clip a slot: a MIDI clip there goes.
    if (const auto there = slotPart(track, scene); there && there->midi)
        if (const auto* clip = midiClipInScene(engine_, there->midiId, scene))
            (void) engine_.removeMidiClip(there->midiId, clip->id);
    if (!engine_.setLauncherClip(part->audioIndex, scene, file.getFileNameWithoutExtension(), 0,
                                 static_cast<double>(reader->lengthInSamples) / reader->sampleRate, true, trackColour(track), file))
        return juce::Result::fail(ui::Ls("Le clip n'a pas pu être posé", "The clip could not be placed"));
    dirty_ = true;
    return juce::Result::ok();
}

// ---- Groups ----------------------------------------------------------------------------------

bool Studio::isGroupTrack(const TrackRef& track) const
{
    return isBusTrack(track) && groups_.contains(routingKey(track));
}

std::optional<TrackRef> Studio::groupOf(const TrackRef& track) const
{
    const auto found = groupOf_.find(routingKey(track));
    if (found == groupOf_.end() || !groups_.contains(found->second))
        return std::nullopt;
    return trackForKey(found->second);
}

std::vector<TrackRef> Studio::groupMembers(const TrackRef& group) const
{
    std::vector<TrackRef> members;
    const auto key = routingKey(group);
    if (key == 0)
        return members;
    for (const auto& track : tracks())
        if (const auto found = groupOf_.find(routingKey(track)); found != groupOf_.end() && found->second == key)
            members.push_back(track);
    return members;
}

bool Studio::isFolded(const TrackRef& group) const
{
    return foldedGroups_.contains(routingKey(group));
}

void Studio::setFolded(const TrackRef& group, const bool folded)
{
    if (!isGroupTrack(group))
        return;
    if (folded)
        foldedGroups_.insert(routingKey(group));
    else
        foldedGroups_.erase(routingKey(group));
    dirty_ = true;
}

bool Studio::hiddenByFold(const TrackRef& track) const
{
    const auto group = groupOf(track);
    return group && isFolded(*group);
}

bool Studio::groupHasClips(const TrackRef& group, const int scene) const
{
    const auto members = groupMembers(group);
    return std::any_of(members.begin(), members.end(), [&](const TrackRef& track) { return slotHasClip(track, scene); });
}

bool Studio::groupPlaysScene(const TrackRef& group, const int scene) const
{
    const auto members = groupMembers(group);
    return std::any_of(members.begin(), members.end(), [&](const TrackRef& track) { return playingScene(track) == scene; });
}

bool Studio::groupWaitsScene(const TrackRef& group, const int scene) const
{
    const auto members = groupMembers(group);
    return std::any_of(members.begin(), members.end(), [&](const TrackRef& track) { return pendingScene(track) == scene; });
}

std::optional<TrackRef> Studio::groupTracks(const std::vector<TrackRef>& picked)
{
    // In the order shown; never the master, a group, or a track twice.
    std::vector<TrackRef> members;
    for (const auto& track : tracks())
        if (std::find(picked.begin(), picked.end(), track) != picked.end() && !track.master && !isGroupTrack(track))
            members.push_back(track);
    if (members.empty())
        return std::nullopt;
    const auto group = addBusTrack(ui::Ls("Groupe ", "Group ") + juce::String(static_cast<int>(groups_.size() + 1)));
    if (!group)
        return std::nullopt;
    const auto groupKey = routingKey(*group);
    groups_.insert(groupKey);
    setTrackColour(*group, trackColour(members.front()));
    std::vector<std::uint64_t> memberKeys;
    for (const auto& member : members) {
        // Its sound into the group (its sends kept).
        auto outputs = trackOutputs(member);
        if (outputs.empty())
            outputs.push_back({});
        outputs.front().destination = groupKey;
        outputs.front().input = 0;
        outputs.front().hardware = 0;
        (void) setTrackOutputs(member, outputs);
        groupOf_[routingKey(member)] = groupKey;
        memberKeys.push_back(routingKey(member));
    }
    // Shown where its first track was: it, then its tracks together.
    (void) tracks();
    const auto first = std::find(trackOrder_.begin(), trackOrder_.end(), memberKeys.front());
    auto at = static_cast<std::ptrdiff_t>(std::distance(trackOrder_.begin(), first));
    std::vector<std::uint64_t> order;
    for (std::ptrdiff_t index = 0; index < static_cast<std::ptrdiff_t>(trackOrder_.size()); ++index) {
        const auto key = trackOrder_[static_cast<std::size_t>(index)];
        if (key == groupKey || std::find(memberKeys.begin(), memberKeys.end(), key) != memberKeys.end()) {
            if (index < at)
                --at;
            continue;
        }
        order.push_back(key);
    }
    at = std::clamp<std::ptrdiff_t>(at, 0, static_cast<std::ptrdiff_t>(order.size()));
    order.insert(order.begin() + at, groupKey);
    order.insert(order.begin() + at + 1, memberKeys.begin(), memberKeys.end());
    trackOrder_ = std::move(order);
    dirty_ = true;
    return group;
}

bool Studio::ungroup(const TrackRef& group)
{
    if (!isGroupTrack(group))
        return false;
    const auto key = routingKey(group);
    for (const auto& member : groupMembers(group)) {
        auto outputs = trackOutputs(member);
        for (auto& output : outputs)
            if (output.destination == key) {
                output.destination = 0;
                output.input = 0;
            }
        (void) setTrackOutputs(member, outputs);
        groupOf_.erase(routingKey(member));
    }
    groups_.erase(key);
    foldedGroups_.erase(key);
    return removeTrack(group);
}

juce::Result Studio::moveLauncherClip(const TrackRef& from, const int fromScene, const TrackRef& to, const int toScene, const bool copy)
{
    if ((from == to && fromScene == toScene) || from.master || to.master || !isValid(from) || !isValid(to))
        return juce::Result::ok();
    // The clip's own track (a hybrid track's companion holds the other
    // kind), and its kind's place on the destination, made if needed.
    const auto source = slotPart(from, fromScene);
    if (!source)
        return juce::Result::fail(ui::Ls("Pas de clip dans cette case", "No clip in that slot"));
    const auto target = makePartFor(to, source->midi);
    if (!target)
        return juce::Result::fail(source->midi ? ui::Ls("Cette piste ne prend pas de clip MIDI", "That track takes no MIDI clip")
                                               : ui::Ls("Cette piste ne prend pas de clip audio", "That track takes no audio clip"));
    // What the slot had goes, whatever its kind.
    if (const auto there = slotPart(to, toScene)) {
        if (there->midi) {
            if (const auto* clip = midiClipInScene(engine_, there->midiId, toScene))
                (void) engine_.removeMidiClip(there->midiId, clip->id);
        } else if (there->midi != source->midi) {
            (void) engine_.clearLauncherClip(there->audioIndex, toScene);
        }
    }
    if (source->midi) {
        const auto* clip = midiClipInScene(engine_, source->midiId, fromScene);
        if (clip == nullptr)
            return juce::Result::fail(ui::Ls("Pas de clip dans cette case", "No clip in that slot"));
        const auto moved = *clip;
        if (!copy && *source == *target) {
            (void) engine_.setMidiClipLauncherScene(source->midiId, moved.id, toScene);
        } else {
            const auto id = engine_.addMidiClip(target->midiId, moved.name, 0.0, moved.lengthBeats, moved.looping, moved.colour, toScene);
            if (id == 0)
                return juce::Result::fail(ui::Ls("Le clip n'a pas pu être créé", "The clip could not be made"));
            (void) engine_.setMidiClipTimelineBounds(target->midiId, id, moved.lengthBeats, moved.playStartBeats, moved.playEndBeats,
                                                     moved.loopStartBeats, moved.loopEndBeats, moved.looping);
            for (const auto& note : moved.notes)
                (void) engine_.copyMidiNote(target->midiId, id, note, note.startBeats);
            // Its own gain envelope goes with it.
            for (const auto& point : moved.automation)
                if (const auto pointId = engine_.addMidiClipAutomationPoint(target->midiId, id, point.timeSeconds, point.valueDb))
                    (void) engine_.setMidiClipAutomationPointCurve(target->midiId, id, pointId, point.curve, point.curveAmount);
            if (!copy)
                (void) engine_.removeMidiClip(source->midiId, moved.id);
        }
    } else if (!engine_.transferLauncherClip(source->audioIndex, fromScene, target->audioIndex, toScene, copy)) {
        return juce::Result::fail(ui::Ls("Le clip n'a pas pu être déplacé", "The clip could not be moved"));
    }
    dirty_ = true;
    return juce::Result::ok();
}

void Studio::restoreGroup(const std::uint64_t group, const std::vector<std::uint64_t>& members, const bool folded)
{
    if (group == 0)
        return;
    groups_.insert(group);
    for (const auto member : members)
        if (member != 0)
            groupOf_[member] = group;
    if (folded)
        foldedGroups_.insert(group);
}

std::size_t Studio::trackCount() const
{
    return engine_.trackCount() + midiTracks_.size();
}

bool Studio::isValid(const TrackRef& track) const
{
    return track.midi ? midiTracks_.contains(track.midiId) : track.audioIndex < engine_.trackCount();
}

juce::String Studio::trackName(const TrackRef& track) const
{
    if (track.isBus()) {
        const auto bus = engine_.virtualBus(track.bus);
        return bus ? bus->name : juce::String();
    }
    if (track.master)
        return "Master";
    // A hybrid track's companion: the track's name.
    const auto shown = shownTrackOf(track.owner());
    const auto ownName = !(shown == track.owner()) ? std::optional<juce::String>(trackName(shown)) : std::nullopt;
    if (track.midi) {
        for (const auto& summary : engine_.midiTrackSummaries())
            if (summary.id == track.midiId) {
                // A pad: the rack's name, then its own.
                if (const auto* pad = track.pad >= 0 ? rackPad(track.owner(), static_cast<std::size_t>(track.pad)) : nullptr)
                    return ownName.value_or(summary.name) + juce::String::fromUTF8(" › ") + pad->name;
                return ownName.value_or(summary.name);
            }
        return {};
    }
    if (ownName)
        return *ownName;
    return track.audioIndex < audioTracks_.size() ? audioTracks_[track.audioIndex].name : juce::String();
}

juce::uint32 Studio::trackColour(const TrackRef& track) const
{
    if (track.isBus()) {
        const auto bus = engine_.virtualBus(track.bus);
        return bus && bus->direction == app::VirtualBusDirection::output ? 0xff66d18fu : 0xff4fb8ffu;
    }
    if (track.master)
        return 0xffffa726;
    if (const auto shown = shownTrackOf(track.owner()); !(shown == track.owner()))
        return trackColour(shown);
    if (track.midi) {
        const auto found = midiTracks_.find(track.midiId);
        return found != midiTracks_.end() ? found->second.colour : 0xff52d1a2;
    }
    return track.audioIndex < audioTracks_.size() ? audioTracks_[track.audioIndex].colour : 0xff5ab0f0;
}

void Studio::setTrackName(const TrackRef& track, const juce::String& name)
{
    if (track.isBus()) {
        if (auto bus = engine_.virtualBus(track.bus); bus && name.trim().isNotEmpty()) {
            bus->name = name.trim();
            (void) updateHardwareBus(*bus);
        }
        return;
    }
    if (track.master)
        return;
    if (name.trim().isEmpty())
        return;
    if (track.midi)
        (void) engine_.setMidiTrackName(track.midiId, name.trim());
    else if (track.audioIndex < audioTracks_.size())
        audioTracks_[track.audioIndex].name = name.trim();
    dirty_ = true;
}

void Studio::setTrackColour(const TrackRef& track, const juce::uint32 argb)
{
    if (track.master)
        return;
    if (track.midi) {
        if (const auto found = midiTracks_.find(track.midiId); found != midiTracks_.end())
            found->second.colour = argb;
    } else if (track.audioIndex < audioTracks_.size()) {
        audioTracks_[track.audioIndex].colour = argb;
    }
    dirty_ = true;
}

int Studio::laneHeight(const TrackRef& track) const
{
    if (track.master)
        return 88;
    if (track.midi) {
        const auto found = midiTracks_.find(track.midiId);
        return found != midiTracks_.end() ? found->second.laneHeight : 88;
    }
    return track.audioIndex < audioTracks_.size() ? audioTracks_[track.audioIndex].laneHeight : 88;
}

void Studio::setLaneHeight(const TrackRef& track, const int height)
{
    if (track.master)
        return;
    const auto clamped = juce::jlimit(40, 420, height);
    if (track.midi) {
        if (const auto found = midiTracks_.find(track.midiId); found != midiTracks_.end())
            found->second.laneHeight = clamped;
    } else if (track.audioIndex < audioTracks_.size()) {
        audioTracks_[track.audioIndex].laneHeight = clamped;
    }
}

// ---- Volume automation ---------------------------------------------------------------------------

bool Studio::automationVisible(const TrackRef& track) const
{
    if (track.master)
        return false;
    if (track.midi) {
        const auto found = midiTracks_.find(track.midiId);
        return found != midiTracks_.end() && found->second.automationVisible;
    }
    return track.audioIndex < audioTracks_.size() && audioTracks_[track.audioIndex].automationVisible;
}

void Studio::setAutomationVisible(const TrackRef& track, const bool visible)
{
    if (track.master)
        return;
    bool* flag = nullptr;
    int* height = nullptr;
    if (track.midi) {
        if (const auto found = midiTracks_.find(track.midiId); found != midiTracks_.end()) {
            flag = &found->second.automationVisible;
            height = &found->second.laneHeight;
        }
    } else if (track.audioIndex < audioTracks_.size()) {
        flag = &audioTracks_[track.audioIndex].automationVisible;
        height = &audioTracks_[track.audioIndex].laneHeight;
    }
    if (flag == nullptr || *flag == visible)
        return;
    *flag = visible;
    if (visible && *height < 150)
        *height = 172;
    else if (!visible && *height == 172)
        *height = 88;
    dirty_ = true;
}

std::vector<Studio::AutomationPoint> Studio::automationPoints(const TrackRef& track) const
{
    if (track.master)
        return {};
    if (!isValid(track))
        return {};
    return track.midi ? engine_.midiAutomationPoints(track.midiId) : engine_.automationPoints(track.audioIndex);
}

std::uint64_t Studio::addAutomationPoint(const TrackRef& track, const double seconds, const float valueDb)
{
    if (track.master)
        return 0;
    if (!isValid(track))
        return 0;
    dirty_ = true;
    return track.midi ? engine_.addMidiAutomationPoint(track.midiId, seconds, valueDb)
                      : engine_.addAutomationPoint(track.audioIndex, seconds, valueDb);
}

bool Studio::updateAutomationPoint(const TrackRef& track, const std::uint64_t id, const double seconds, const float valueDb)
{
    if (track.master)
        return false;
    if (!isValid(track))
        return false;
    dirty_ = true;
    return track.midi ? engine_.updateMidiAutomationPoint(track.midiId, id, seconds, valueDb)
                      : engine_.updateAutomationPoint(track.audioIndex, id, seconds, valueDb);
}

bool Studio::setAutomationPointCurve(const TrackRef& track, const std::uint64_t id, const AutomationCurve curve,
                                     const float amount)
{
    if (track.master)
        return false;
    if (!isValid(track))
        return false;
    dirty_ = true;
    return track.midi ? engine_.setMidiAutomationPointCurve(track.midiId, id, curve, amount)
                      : engine_.setAutomationPointCurve(track.audioIndex, id, curve, amount);
}

bool Studio::removeAutomationPoint(const TrackRef& track, const std::uint64_t id)
{
    if (track.master)
        return false;
    if (!isValid(track))
        return false;
    dirty_ = true;
    return track.midi ? engine_.removeMidiAutomationPoint(track.midiId, id)
                      : engine_.removeAutomationPoint(track.audioIndex, id);
}

void Studio::setAutomationPoints(const TrackRef& track, const std::vector<AutomationPoint>& points)
{
    if (track.master)
        return;
    if (!isValid(track))
        return;
    // The whole lane at once: its points keep their ids, shapes and handles.
    if (track.midi)
        engine_.replaceMidiAutomationPoints(track.midiId, points);
    else
        engine_.replaceAutomationPoints(track.audioIndex, points);
    dirty_ = true;
}

// ---- Automation lanes -----------------------------------------------------------------------------

ParameterLanes* Studio::lanesOf(const TrackRef& track)
{
    if (track.master)
        return nullptr;
    if (track.midi) {
        const auto found = midiTracks_.find(track.midiId);
        return found != midiTracks_.end() ? &found->second.parameterLanes : nullptr;
    }
    return track.audioIndex < audioTracks_.size() ? &audioTracks_[track.audioIndex].parameterLanes : nullptr;
}

const ParameterLanes* Studio::lanesOf(const TrackRef& track) const
{
    return const_cast<Studio*>(this)->lanesOf(track);
}

juce::String Studio::pluginLaneName(const std::size_t plugin, const std::uint32_t parameter)
{
    return "plugin:" + juce::String(static_cast<int>(plugin)) + ":" + juce::String(static_cast<juce::int64>(parameter));
}

bool Studio::parsePluginLane(const juce::String& lane, std::size_t& plugin, std::uint32_t& parameter)
{
    if (!lane.startsWith("plugin:"))
        return false;
    const auto rest = lane.substring(7);
    const auto colon = rest.indexOfChar(':');
    if (colon <= 0)
        return false;
    plugin = static_cast<std::size_t>(rest.substring(0, colon).getIntValue());
    parameter = static_cast<std::uint32_t>(rest.substring(colon + 1).getLargeIntValue());
    return true;
}

void Studio::rememberPluginParameterName(const juce::String& lane, const juce::String& name)
{
    if (name.isNotEmpty())
        pluginParameterNames_[lane] = name;
}

juce::String Studio::pluginParameterName(const TrackRef& track, const juce::String& lane) const
{
    if (track.master)
        return {};
    if (const auto found = pluginParameterNames_.find(lane); found != pluginParameterNames_.end())
        return found->second;
    std::size_t plugin {};
    std::uint32_t parameter {};
    if (!track.midi && parsePluginLane(lane, plugin, parameter)) {
        for (const auto& item : engine_.pluginAutomationLanes(track.audioIndex))
            if (item.pluginIndex == plugin && item.parameterIndex == parameter && item.parameterName.isNotEmpty())
                return item.parameterName;
        return "#" + juce::String(static_cast<juce::int64>(parameter) + 1);
    }
    return {};
}

std::vector<Studio::AutomationPoint> Studio::lanePoints(const TrackRef& track, const juce::String& lane) const
{
    if (lane.isEmpty())
        return automationPoints(track);
    std::size_t plugin {};
    std::uint32_t parameter {};
    if (parsePluginLane(lane, plugin, parameter)) {
        std::vector<AutomationPoint> points;
        if (!track.midi && isValid(track))
            for (const auto& point : engine_.pluginAutomationPoints(track.audioIndex, plugin, parameter))
                points.push_back({ point.id, point.timeSeconds, point.normalisedValue, point.curve, point.curveAmount, point.handles });
        return points;
    }
    const auto* lanes = lanesOf(track);
    if (lanes == nullptr)
        return {};
    const auto found = lanes->lanes.find(lane);
    return found != lanes->lanes.end() ? found->second : std::vector<AutomationPoint> {};
}

namespace {

void sortPoints(std::vector<Studio::AutomationPoint>& points)
{
    std::stable_sort(points.begin(), points.end(),
                     [](const auto& a, const auto& b) { return a.timeSeconds < b.timeSeconds; });
}

} // namespace

std::uint64_t Studio::addLanePoint(const TrackRef& track, const juce::String& lane, const double seconds, const float value)
{
    if (lane.isEmpty())
        return addAutomationPoint(track, seconds, value);
    std::size_t plugin {};
    std::uint32_t parameter {};
    if (parsePluginLane(lane, plugin, parameter)) {
        if (track.midi || !isValid(track))
            return 0;
        dirty_ = true;
        return engine_.addPluginAutomationPoint(track.audioIndex, plugin, parameter, pluginParameterName(track, lane),
                                                std::max(0.0, seconds), std::clamp(value, 0.0f, 1.0f));
    }
    auto* lanes = lanesOf(track);
    if (lanes == nullptr || !std::isfinite(value))
        return 0;
    auto& points = lanes->lanes[lane];
    AutomationPoint point;
    point.id = lanes->nextId++;
    point.timeSeconds = std::max(0.0, seconds);
    point.valueDb = value;
    points.push_back(point);
    sortPoints(points);
    publishParameterAutomation(track);
    dirty_ = true;
    return point.id;
}

bool Studio::updateLanePoint(const TrackRef& track, const juce::String& lane, const std::uint64_t id, const double seconds,
                             const float value)
{
    if (lane.isEmpty())
        return updateAutomationPoint(track, id, seconds, value);
    std::size_t plugin {};
    std::uint32_t parameter {};
    if (parsePluginLane(lane, plugin, parameter)) {
        if (track.midi || !isValid(track))
            return false;
        dirty_ = true;
        return engine_.updatePluginAutomationPoint(track.audioIndex, plugin, parameter, id, std::max(0.0, seconds),
                                                   std::clamp(value, 0.0f, 1.0f));
    }
    auto* lanes = lanesOf(track);
    if (lanes == nullptr)
        return false;
    const auto found = lanes->lanes.find(lane);
    if (found == lanes->lanes.end())
        return false;
    for (auto& point : found->second)
        if (point.id == id) {
            point.timeSeconds = std::max(0.0, seconds);
            point.valueDb = value;
            sortPoints(found->second);
            publishParameterAutomation(track);
            dirty_ = true;
            return true;
        }
    return false;
}

bool Studio::setLanePointCurve(const TrackRef& track, const juce::String& lane, const std::uint64_t id,
                               const AutomationCurve curve, const float amount)
{
    if (lane.isEmpty())
        return setAutomationPointCurve(track, id, curve, amount);
    std::size_t plugin {};
    std::uint32_t parameter {};
    if (parsePluginLane(lane, plugin, parameter)) {
        if (track.midi || !isValid(track))
            return false;
        dirty_ = true;
        return engine_.setPluginAutomationPointCurve(track.audioIndex, plugin, parameter, id, curve, amount);
    }
    auto* lanes = lanesOf(track);
    if (lanes == nullptr)
        return false;
    const auto found = lanes->lanes.find(lane);
    if (found == lanes->lanes.end())
        return false;
    for (auto& point : found->second)
        if (point.id == id) {
            point.curve = curve;
            point.curveAmount = std::clamp(amount, -1.0f, 1.0f);
            publishParameterAutomation(track);
            dirty_ = true;
            return true;
        }
    return false;
}

bool Studio::removeLanePoint(const TrackRef& track, const juce::String& lane, const std::uint64_t id)
{
    if (lane.isEmpty())
        return removeAutomationPoint(track, id);
    std::size_t plugin {};
    std::uint32_t parameter {};
    if (parsePluginLane(lane, plugin, parameter)) {
        if (track.midi || !isValid(track))
            return false;
        dirty_ = true;
        return engine_.removePluginAutomationPoint(track.audioIndex, plugin, parameter, id);
    }
    auto* lanes = lanesOf(track);
    if (lanes == nullptr)
        return false;
    const auto found = lanes->lanes.find(lane);
    if (found == lanes->lanes.end())
        return false;
    const auto before = found->second.size();
    std::erase_if(found->second, [id](const auto& point) { return point.id == id; });
    const auto removed = found->second.size() < before;
    if (found->second.empty())
        lanes->lanes.erase(found);
    publishParameterAutomation(track);
    dirty_ = true;
    return removed;
}

void Studio::setLanePoints(const TrackRef& track, const juce::String& lane, const std::vector<AutomationPoint>& points)
{
    if (lane.isEmpty()) {
        setAutomationPoints(track, points);
        return;
    }
    std::size_t plugin {};
    std::uint32_t parameter {};
    if (parsePluginLane(lane, plugin, parameter)) {
        if (track.midi || !isValid(track))
            return;
        std::vector<app::AudioTrackController::PluginAutomationPoint> converted;
        for (const auto& point : points)
            converted.push_back({ point.id, point.timeSeconds, point.valueDb, point.curve, point.curveAmount, point.handles });
        engine_.replacePluginAutomationPoints(track.audioIndex, plugin, parameter, pluginParameterName(track, lane),
                                              std::move(converted));
        dirty_ = true;
        return;
    }
    auto* lanes = lanesOf(track);
    if (lanes == nullptr)
        return;
    if (points.empty())
        lanes->lanes.erase(lane);
    else {
        auto& stored = lanes->lanes[lane];
        stored = points;
        for (auto& point : stored) {
            if (point.id == 0)
                point.id = lanes->nextId++;
            lanes->nextId = std::max(lanes->nextId, point.id + 1);
        }
        sortPoints(stored);
    }
    publishParameterAutomation(track);
    dirty_ = true;
}

std::vector<juce::String> Studio::automatedParameters(const TrackRef& track) const
{
    std::vector<juce::String> bindings;
    if (const auto* lanes = lanesOf(track))
        for (const auto& [binding, points] : lanes->lanes)
            if (!points.empty())
                bindings.push_back(binding);
    if (!track.midi && isValid(track))
        for (const auto& lane : engine_.pluginAutomationLanes(track.audioIndex))
            if (!lane.points.empty())
                bindings.push_back(pluginLaneName(lane.pluginIndex, lane.parameterIndex));
    return bindings;
}

juce::String Studio::shownLane(const TrackRef& track) const
{
    const auto* lanes = lanesOf(track);
    return lanes != nullptr ? lanes->shown : juce::String();
}

void Studio::setShownLane(const TrackRef& track, const juce::String& lane)
{
    if (auto* lanes = lanesOf(track); lanes != nullptr && lanes->shown != lane) {
        lanes->shown = lane;
        dirty_ = true;
    }
}

bool Studio::logarithmicParameter(const TrackRef& track, const juce::String& binding) const
{
    if (const auto* slot = graphFor(track))
        for (const auto& parameter : slot->graph().editorGraphParameters())
            if (parameter.binding == binding)
                return parameter.minimum > 0.0
                       && (parameter.unit == "Hz" || parameter.unit == "ms" || parameter.unit == "s");
    return false;
}

std::optional<float> Studio::laneValueNow(const TrackRef& track, const juce::String& lane) const
{
    const auto points = lanePoints(track, lane);
    if (points.empty())
        return std::nullopt;
    std::vector<app::NativeDspRuntime::AutomationPoint> engine;
    engine.reserve(points.size());
    for (const auto& point : points)
        engine.push_back({ point.timeSeconds, point.valueDb, static_cast<int>(point.curve), point.curveAmount, point.handles });
    return app::NativeDspRuntime::automationValueAt(engine, engine_.position(),
                                                    lane.isNotEmpty() && !lane.startsWith("plugin:")
                                                        && logarithmicParameter(track, lane));
}

std::string Studio::learnPrefix(const TrackRef& track) const
{
    // A bus of the software or a rack's pad: not a target (yet).
    if (track.bus != 0 || track.pad >= 0 || (!track.master && !isValid(track)))
        return {};
    const auto key = routingKey(track);
    return "param/" + (track.master ? std::string("master") : std::to_string(key)) + "/";
}

bool Studio::canAutomate(const TrackRef& track, const juce::String& binding) const
{
    if (track.master)
        return false;
    std::size_t plugin {};
    std::uint32_t parameter {};
    if (parsePluginLane(binding, plugin, parameter))
        return !track.midi && isValid(track) && plugin < engine_.trackPlugins(track.audioIndex).size();
    const auto* slot = graphFor(track);
    if (slot == nullptr)
        return false;
    if (track.midi && slot->graph().editorVoiceParameterFor(binding))
        return true;
    const auto target = modulation::nativeTargetFor(nativeBinding(slot, binding));
    // An instrument's voice cards play in the voice engine, not the chain.
    return target.has_value() && !(track.midi && slot->graph().editorIsVoiceNode(target->nodeId));
}

juce::String Studio::nativeBinding(const GraphSlot* slot, const juce::String& binding) const
{
    if (slot != nullptr)
        if (const auto inner = slot->graph().editorNativeNodeFor(binding))
            return "node." + juce::String(static_cast<juce::int64>(inner->first)) + "." + inner->second;
    return binding;
}

namespace {

using LanePoint = app::AudioTrackController::AutomationPoint;

// Where a lane stands at a time: its first value before it, its last after
// it (none: `fallback`).
float laneValueAt(const std::vector<LanePoint>& points, const double time, const float fallback)
{
    if (points.empty())
        return fallback;
    return static_cast<float>(engine::curveValueAt(
        points,
        [](const LanePoint& point) {
            return engine::CurveKnot { point.timeSeconds, point.valueDb, static_cast<int>(point.curve), point.curveAmount, point.handles };
        },
        time));
}

struct ClipSpan {
    double start {};
    double end {};
    const std::vector<LanePoint>* points {};
};

// A track's lane with its arrangement clips' lanes laid over their spans:
// the clip's points (from its start) inside, the track's own outside.
std::vector<LanePoint> mergedLane(const std::vector<LanePoint>& own, const float fallback, const std::vector<ClipSpan>& spans)
{
    constexpr auto edge = 0.0005;
    std::vector<LanePoint> out;
    for (const auto& point : own)
        if (std::none_of(spans.begin(), spans.end(), [&point](const ClipSpan& span) {
                return point.timeSeconds >= span.start - edge && point.timeSeconds <= span.end + edge;
            }))
            out.push_back(point);
    for (const auto& span : spans) {
        const auto before = laneValueAt(own, span.start, fallback);
        const auto after = laneValueAt(own, span.end, fallback);
        out.push_back({ 0, span.start - edge, before });
        out.push_back({ 0, span.start, laneValueAt(*span.points, 0.0, before) });
        for (const auto& point : *span.points) {
            const auto time = span.start + point.timeSeconds;
            if (time > span.start && time < span.end)
                out.push_back({ 0, time, point.valueDb, point.curve, point.curveAmount, point.handles });
        }
        out.push_back({ 0, span.end, laneValueAt(*span.points, span.end - span.start, before) });
        out.push_back({ 0, span.end + edge, after });
    }
    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.timeSeconds < b.timeSeconds; });
    std::uint64_t id = 1;
    for (auto& point : out)
        point.id = id++;
    return out;
}

} // namespace

void Studio::followClipLanes()
{
    if (opening_ != nullptr)
        return;
    // A launcher clip with lanes started or stopped, an arrangement clip
    // with lanes moved: the track's automation published anew.
    const auto hasLanes = [this](const std::size_t index, const int scene) {
        if (scene < 0)
            return false;
        const auto clip = engine_.launcherClip(index, scene);
        return clip && !clip->lanes.empty();
    };
    for (std::size_t index = 0; index < audioTracks_.size() && index < engine_.trackCount(); ++index) {
        auto& state = audioTracks_[index];
        const auto scene = engine_.activeLauncherScene(index);
        auto signature = 0.0;
        if (const auto* clips = engine_.audioClipsView(index))
            for (const auto& clip : *clips)
                if (!clip.lanes.empty())
                    signature += clip.startSeconds * 3.1 + clip.durationSeconds * 7.7 + static_cast<double>(clip.lanes.size()) + 1.0;
        const auto moved = std::abs(signature - state.clipLaneSignature) > 1.0e-9;
        if (scene == state.clipLaneScene && !moved)
            continue;
        const auto before = state.clipLaneScene;
        state.clipLaneScene = scene;
        state.clipLaneSignature = signature;
        if (moved || hasLanes(index, scene) || hasLanes(index, before)) {
            TrackRef track;
            track.audioIndex = index;
            publishParameterAutomation(track);
        }
    }
}

std::vector<Studio::MasterLaneChoice> Studio::clipLaneChoices(const TrackRef& track) const
{
    std::vector<MasterLaneChoice> choices;
    if (!isValid(track) || track.master)
        return choices;
    choices.push_back({ clipVolumeLane, ui::Ls("Volume", "Volume"), "dB", -60.0, 12.0, 0.0 });
    if (!track.midi)
        choices.push_back({ clipPanLane, ui::Ls("Panoramique", "Pan"), "", -1.0, 1.0, engine_.trackInfo(track.audioIndex).pan });
    if (const auto* slot = graphFor(track))
        for (const auto& node : slot->graph().editorNodes()) {
            if (node.boundary)
                continue;
            for (const auto& parameter : node.parameters)
                if (canAutomate(track, parameter.binding) && !parameter.binding.startsWith("plugin:"))
                    choices.push_back({ parameter.binding, node.title + juce::String::fromUTF8(" \xc2\xb7 ") + parameter.label, parameter.unit,
                                        parameter.minimum, parameter.maximum, parameter.value, logarithmicParameter(track, parameter.binding) });
        }
    return choices;
}

void Studio::publishParameterAutomation(const TrackRef& track)
{
    const auto* stored = lanesOf(track);
    if (stored == nullptr || !isValid(track))
        return;
    std::vector<app::NativeDspRuntime::AutomationLane> out;
    std::vector<app::AudioTrackController::VoiceAutomationLane> voices;
    const auto* slot = graphFor(track);
    // Its clips' own lanes: those of the launcher clip that plays (the
    // track's clock is then that clip's own time) in place of its own; else
    // its arrangement clips', laid over their spans.
    auto lanes = *stored;
    if (!track.midi && !track.master) {
        std::optional<std::vector<LanePoint>> volume;
        std::vector<LanePoint> pan;
        const auto scene = engine_.activeLauncherScene(track.audioIndex);
        const auto playing = scene >= 0 ? engine_.launcherClip(track.audioIndex, scene) : std::nullopt;
        const auto knob = [&](const juce::String& binding) {
            if (slot != nullptr)
                for (const auto& parameter : slot->graph().editorGraphParameters())
                    if (parameter.binding == binding)
                        return static_cast<float>(parameter.value);
            return 0.0f;
        };
        if (playing && !playing->lanes.empty()) {
            for (const auto& [name, points] : playing->lanes) {
                if (name == clipVolumeLane)
                    volume = points;
                else if (name == clipPanLane)
                    pan = points;
                else
                    lanes.lanes[name] = points;
            }
        } else if (const auto* clips = engine_.audioClipsView(track.audioIndex)) {
            std::map<juce::String, std::vector<ClipSpan>> byLane;
            for (const auto& clip : *clips)
                for (const auto& [name, points] : clip.lanes)
                    if (!points.empty())
                        byLane[name].push_back({ clip.startSeconds, clip.startSeconds + clip.durationSeconds, &points });
            for (const auto& [name, spans] : byLane) {
                if (name == clipVolumeLane)
                    volume = mergedLane(engine_.automationPoints(track.audioIndex), 0.0f, spans);
                else if (name == clipPanLane)
                    pan = mergedLane({}, engine_.trackInfo(track.audioIndex).pan, spans);
                else
                    lanes.lanes[name] = mergedLane(lanes.lanes[name], knob(name), spans);
            }
        }
        engine_.setTrackClipAutomation(track.audioIndex, std::move(volume), std::move(pan));
        if (track.audioIndex < audioTracks_.size())
            audioTracks_[track.audioIndex].clipLaneScene = scene;
    }
    for (const auto& [binding, points] : lanes.lanes) {
        if (points.empty())
            continue;
        const auto toEngine = [&points](const float minimum, const float maximum) {
            std::vector<app::NativeDspRuntime::AutomationPoint> converted;
            for (const auto& point : points)
                converted.push_back({ point.timeSeconds, std::clamp(point.valueDb, minimum, maximum), static_cast<int>(point.curve),
                                      point.curveAmount, point.handles });
            return converted;
        };
        if (track.midi && slot != nullptr)
            if (const auto voice = slot->graph().editorVoiceParameterFor(binding)) {
                // Kept in the knob's range: an instrument's own settings
                // reach its voices as they are.
                auto minimum = -std::numeric_limits<float>::max();
                auto maximum = std::numeric_limits<float>::max();
                for (const auto& parameter : slot->graph().editorGraphParameters())
                    if (parameter.binding == binding) {
                        minimum = static_cast<float>(parameter.minimum);
                        maximum = static_cast<float>(parameter.maximum);
                    }
                voices.push_back({ voice->target, voice->index, logarithmicParameter(track, binding), toEngine(minimum, maximum), minimum, maximum });
                continue;
            }
        const auto target = modulation::nativeTargetFor(nativeBinding(slot, binding));
        if (!target || !canAutomate(track, binding))
            continue;
        app::NativeDspRuntime::AutomationLane lane;
        lane.nodeId = target->nodeId;
        lane.field = target->field;
        lane.slot = target->slot;
        lane.logarithmic = logarithmicParameter(track, binding);
        lane.minimum = target->minimum;
        lane.maximum = target->maximum;
        for (const auto& point : points)
            lane.points.push_back({ point.timeSeconds, std::clamp(point.valueDb, target->minimum, target->maximum),
                                    static_cast<int>(point.curve), point.curveAmount, point.handles });
        out.push_back(std::move(lane));
    }
    if (track.midi) {
        engine_.setMidiTrackNativeAutomation(track.midiId, std::move(out));
        engine_.setMidiTrackVoiceAutomation(track.midiId, std::move(voices));
    } else
        engine_.setTrackNativeAutomation(track.audioIndex, std::move(out));
}

// ---- The device workshop ---------------------------------------------------------------------------

std::unique_ptr<GraphSlot> Studio::makeWorkshopSlot()
{
    // The tab that holds the slot, whichever is in front.
    return std::make_unique<GraphSlot>(GraphSlot::Owner::workshop, [this](GraphSlot& slot, const bool structural) {
        for (auto& tab : workshops_)
            if (tab->own.get() == &slot)
                workshopRootChanged(*tab, structural);
    });
}

GraphSlot* Studio::rootOf(Workshop& tab)
{
    if (tab.own != nullptr)
        return tab.own.get();
    return tab.track ? graphFor(*tab.track) : nullptr;
}

GraphSlot* Studio::workshopRoot()
{
    return workshop_ != nullptr ? rootOf(*workshop_) : nullptr;
}

bool Studio::workshopAtRisk() const noexcept
{
    return std::ranges::any_of(workshops_, [](const auto& tab) { return tab->unsaved && !tab->track; });
}

void Studio::openWorkshopTab(Workshop made)
{
    made.id = nextWorkshopId_++;
    workshops_.push_back(std::make_unique<Workshop>(std::move(made)));
    workshop_ = workshops_.back().get();
    publishSimulation(true);
}

std::vector<Studio::WorkshopTab> Studio::workshopTabs() const
{
    std::vector<WorkshopTab> tabs;
    for (const auto& tab : workshops_) {
        WorkshopTab entry;
        entry.id = tab->id;
        entry.instrument = tab->instrument;
        entry.unsaved = tab->unsaved;
        entry.linked = tab->track.has_value();
        if (auto* root = const_cast<Studio*>(this)->rootOf(*tab))
            entry.name = root->graph().editorGraphName().trim();
        if (entry.name.isEmpty() && tab->track)
            entry.name = trackName(*tab->track);
        if (entry.name.isEmpty())
            entry.name = tab->instrument ? ui::Ls("Instrument", "Instrument") : ui::Ls("Effet", "Effect");
        tabs.push_back(std::move(entry));
    }
    return tabs;
}

std::uint64_t Studio::activeWorkshopTab() const noexcept
{
    return workshop_ != nullptr ? workshop_->id : 0;
}

void Studio::selectWorkshopTab(const std::uint64_t id)
{
    for (auto& tab : workshops_)
        if (tab->id == id && tab.get() != workshop_) {
            workshop_ = tab.get();
            publishSimulation(true);
        }
}

bool Studio::workshopTabAtRisk(const std::uint64_t id) const
{
    for (const auto& tab : workshops_)
        if (tab->id == id)
            return tab->unsaved && !tab->track;
    return false;
}

void Studio::closeWorkshopTab(const std::uint64_t id)
{
    const auto target = id != 0 ? id : activeWorkshopTab();
    const auto found = std::ranges::find_if(workshops_, [target](const auto& tab) { return tab->id == target; });
    if (found == workshops_.end())
        return;
    const auto wasFront = found->get() == workshop_;
    const auto index = static_cast<std::size_t>(found - workshops_.begin());
    workshops_.erase(found);
    if (wasFront) {
        workshop_ = workshops_.empty() ? nullptr : workshops_[std::min(index, workshops_.size() - 1)].get();
        simulatedSlot_ = nullptr;
        if (workshop_ == nullptr)
            setSimulating(false);
        else
            publishSimulation(true);
    }
}

GraphSlot* Studio::workshopGraph()
{
    if (!workshop_)
        return nullptr;
    if (!workshop_->nested.empty())
        return workshop_->nested.back().slot.get();
    return workshopRoot();
}

Studio::WorkshopState Studio::workshop() const
{
    WorkshopState state;
    if (!workshop_)
        return state;
    state.open = true;
    state.instrument = workshop_->instrument;
    state.file = workshop_->file;
    state.unsaved = workshop_->unsaved;
    state.track = workshop_->track;
    if (const auto* root = const_cast<Studio*>(this)->workshopRoot())
        state.name = root->graph().editorGraphName().trim();
    if (state.name.isEmpty() && workshop_->track)
        state.name = trackName(*workshop_->track);
    state.trail.push_back(state.name);
    for (const auto& level : workshop_->nested)
        state.trail.push_back(level.slot->graph().editorGraphName());
    return state;
}

void Studio::refreshTranslations()
{
    const auto refresh = [](GraphSlot* slot) {
        if (slot == nullptr)
            return;
        slot->graph().refreshTranslations();
        slot->touch();
    };
    for (auto& track : audioTracks_)
        refresh(track.graph.get());
    for (auto& [id, track] : midiTracks_)
        refresh(track.graph.get());
    for (auto& [id, bus] : buses_)
        refresh(bus.graph.get());
    for (auto& tab : workshops_) {
        refresh(tab->own.get());
        for (auto& level : tab->nested)
            refresh(level.slot.get());
    }
}

void Studio::workshopRootChanged(Workshop& tab, const bool structural)
{
    if (loading_)
        return;
    tab.unsaved = true;
    // A linked effect: its module in the track's graph follows, and the
    // track's graph publishes itself.
    if (tab.track && tab.moduleKey.isNotEmpty() && tab.own != nullptr) {
        if (auto* slot = graphFor(*tab.track))
            slot->graph().editorApplyModuleGraphState(tab.moduleKey, tab.own->graph().serialise(), structural);
        dirty_ = true;
    }
}

app::NodeGraphComponent* Studio::nestedParent(Workshop& tab, const std::size_t level)
{
    if (level >= tab.nested.size())
        return nullptr;
    if (level == 0) {
        auto* root = rootOf(tab);
        return root != nullptr ? &root->graph() : nullptr;
    }
    return &tab.nested[level - 1].slot->graph();
}

void Studio::nestedChanged(Workshop& tab, const std::size_t level, GraphSlot& slot, const bool structural)
{
    if (loading_ || level >= tab.nested.size() || tab.nested[level].slot.get() != &slot)
        return;
    // Into the level around it, which tells its own graph: the device, then
    // the track it is linked to, follow.
    if (auto* parent = nestedParent(tab, level))
        parent->editorApplyModuleGraphState(tab.nested[level].key, slot.graph().serialise(), structural);
    tab.unsaved = true;
}

void Studio::giveOwnIdentity(app::NodeGraphComponent& graph, const bool always)
{
    auto identity = graph.pluginExportIdentity();
    // The factory graph's identity, which every new graph starts with.
    if (!always && identity.productName != "CyDAW Graph")
        return;
    identity.productName = graph.editorGraphName().trim().toStdString();
    if (identity.productName.empty())
        identity.productName = "CyDAW Device";
    identity.companyName = "CyDAW User";
    identity.manufacturerCode = "CYDW";
    // A letter, then three letters or digits: about a million codes.
    static constexpr char letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static constexpr char symbols[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    auto& random = juce::Random::getSystemRandom();
    std::string code;
    do {
        code = letters[random.nextInt(26)];
        for (auto index = 0; index < 3; ++index)
            code += symbols[random.nextInt(36)];
    } while (code == identity.manufacturerCode);
    identity.pluginCode = code;
    identity.version = {};
    graph.setPluginExportIdentity(identity);
}

void Studio::setWorkshopInfo(const app::NodeGraphComponent::EditorGraphInfo& info)
{
    auto* root = workshopRoot();
    if (root == nullptr)
        return;
    auto& graph = root->graph();
    const auto before = graph.editorGraphInfo().name;
    auto identity = graph.pluginExportIdentity();
    graph.editorSetGraphInfo(info);
    const auto after = graph.editorGraphInfo().name;
    if (after != before && (juce::String(identity.productName) == before || identity.productName == "CyDAW Graph")) {
        identity.productName = after.toStdString();
        graph.setPluginExportIdentity(identity);
    }
}

std::pair<std::uint64_t, std::uint64_t> Studio::audioEnds(const app::NodeGraphComponent& graph)
{
    std::uint64_t input {};
    std::uint64_t output {};
    for (const auto& node : graph.editorNodes()) {
        if (input == 0 && node.typeKey.startsWith("graph-audio-input-"))
            input = node.id;
        else if (output == 0 && (node.typeKey.startsWith("graph-audio-output-") || node.typeKey == "master"))
            output = node.id;
    }
    return { input, output };
}

juce::Result Studio::setDeviceChannels(app::NodeGraphComponent& graph, const int input, const int output)
{
    using Kind = cydaw::engine::PortKind;
    const auto [in, out] = audioEnds(graph);
    if (in == 0 || out == 0 || graph.editorAudioChannels(out, 0, true) == 0)
        return juce::Result::fail(ui::Ls("Ce périphérique n'a pas d'entrée et de sortie audio à régler",
                                         "This device has no audio input and output to set"));
    const auto inKey = "node." + juce::String(static_cast<juce::int64>(in));
    const auto outKey = "node." + juce::String(static_cast<juce::int64>(out));
    const auto widths = [&graph, in = in, out = out] {
        return std::make_pair(graph.editorAudioChannels(in, 0, false), graph.editorAudioChannels(out, 0, true));
    };
    if (input == output) {
        // A converter before the output has no use any more: what fed it
        // feeds the output again.
        std::optional<app::NodeGraphComponent::EditorConnection> converted;
        for (const auto& connection : graph.editorConnections())
            if (connection.destination == out && connection.kind == 0)
                for (const auto& node : graph.editorNodes())
                    if (node.id == connection.source && (node.typeKey == "mono-to-stereo" || node.typeKey == "stereo-to-mono"))
                        converted = connection;
        if (converted) {
            std::optional<app::NodeGraphComponent::EditorConnection> before;
            for (const auto& connection : graph.editorConnections())
                if (connection.destination == converted->source && connection.kind == 0)
                    before = connection;
            (void) graph.editorRemoveNodes({ converted->source });
            (void) graph.setGraphMemberType(inKey, Kind::audio, input);
            (void) graph.setGraphMemberType(outKey, Kind::audio, output);
            if (before)
                if (const auto error = graph.editorConnect(before->source, before->sourcePort, out, converted->destinationPort);
                    error.isNotEmpty())
                    return juce::Result::fail(error);
        }
        (void) graph.setGraphMemberType(inKey, Kind::audio, input);
        (void) graph.setGraphMemberType(outKey, Kind::audio, output);
        if (widths() != std::make_pair(input, output))
            return juce::Result::fail(ui::Ls("Une carte entre l'entrée et la sortie garde une autre largeur",
                                             "A card between the input and the output keeps another width"));
        return juce::Result::ok();
    }
    if (!((input == 1 && output == 2) || (input == 2 && output == 1)))
        return juce::Result::fail(ui::Ls("Entre l'entrée et la sortie, seuls mono → stéréo et stéréo → mono se convertissent",
                                         "Between the input and the output, only mono to stereo and stereo to mono convert"));
    // What feeds the output goes through a converter, unless it is one
    // already of the right kind.
    std::optional<app::NodeGraphComponent::EditorConnection> feeding;
    for (const auto& connection : graph.editorConnections())
        if (connection.destination == out && connection.kind == 0)
            feeding = connection;
    const juce::String converterKey = input == 1 ? "mono-to-stereo" : "stereo-to-mono";
    auto converterFeeds = false;
    if (feeding)
        for (const auto& node : graph.editorNodes())
            if (node.id == feeding->source && node.typeKey == converterKey)
                converterFeeds = true;
    if (converterFeeds) {
        (void) graph.setGraphMemberType(inKey, Kind::audio, input);
        (void) graph.setGraphMemberType(outKey, Kind::audio, output);
    } else {
        if (feeding)
            (void) graph.editorDisconnect(feeding->source, feeding->sourcePort, feeding->destination, feeding->destinationPort);
        (void) graph.setGraphMemberType(inKey, Kind::audio, input);
        (void) graph.setGraphMemberType(outKey, Kind::audio, output);
        if (feeding) {
            juce::Point<float> at { 0.5f, 0.5f };
            for (const auto& node : graph.editorNodes())
                if (node.id == out)
                    at = { std::max(0.02f, node.position.x - 0.16f), node.position.y + 0.12f };
            const auto converter = graph.editorAddNode(converterKey, at);
            if (converter == 0)
                return juce::Result::fail(ui::Ls("Le convertisseur n'a pas pu être ajouté", "The converter could not be added"));
            if (const auto error = graph.editorConnect(feeding->source, feeding->sourcePort, converter, 1); error.isNotEmpty())
                return juce::Result::fail(error);
            if (const auto error = graph.editorConnect(converter, 2, out, feeding->destinationPort); error.isNotEmpty())
                return juce::Result::fail(error);
        }
    }
    if (widths() != std::make_pair(input, output))
        return juce::Result::fail(ui::Ls("Une carte entre l'entrée et la sortie garde une autre largeur",
                                         "A card between the input and the output keeps another width"));
    return juce::Result::ok();
}

juce::Result Studio::setWorkshopChannels(const int input, const int output)
{
    auto* root = workshopRoot();
    if (root == nullptr)
        return juce::Result::fail(ui::Ls("L'atelier n'a pas de périphérique", "The workshop has no device"));
    if (workshop_->instrument)
        return juce::Result::fail(ui::Ls("Un instrument sort en stéréo", "An instrument plays in stereo"));
    auto result = setDeviceChannels(root->graph(), std::clamp(input, 1, 8), std::clamp(output, 1, 8));
    graph_layout::arrange(root->graph());
    return result;
}

std::pair<int, int> Studio::workshopChannels()
{
    auto* root = workshopRoot();
    if (root == nullptr)
        return {};
    const auto [in, out] = audioEnds(root->graph());
    return { in != 0 ? root->graph().editorAudioChannels(in, 0, false) : 0,
             out != 0 ? root->graph().editorAudioChannels(out, 0, true) : 0 };
}

void Studio::newWorkshopDevice(const bool instrument, const juce::String& name, const int inputChannels, const int outputChannels)
{
    Workshop made;
    made.instrument = instrument;
    made.own = makeWorkshopSlot();
    made.own->restore(instrument ? factoryMidiGraph_ : factoryAudioGraph_);
    auto& graph = made.own->graph();
    // An instrument starts from a playable voice, an effect from its input
    // wired to its output.
    if (instrument)
        (void) graph.insertFactoryDevice("table-synth");
    graph.editorSetGraphName(name);
    giveOwnIdentity(graph, true);
    if (!instrument && (inputChannels != 2 || outputChannels != 2))
        (void) setDeviceChannels(graph, std::clamp(inputChannels, 1, 8), std::clamp(outputChannels, 1, 8));
    graph_layout::arrange(graph);
    made.unsaved = true;
    openWorkshopTab(std::move(made));
}

juce::Result Studio::openWorkshopFile(const juce::File& file)
{
    if (file.getSize() > 16 * 1024 * 1024)
        return juce::Result::fail(ui::Ls("Trop gros pour un périphérique : ", "Too large for a device: ") + file.getFullPathName());
    // Already open: its tab comes forward.
    for (auto& tab : workshops_)
        if (tab->file == file) {
            selectWorkshopTab(tab->id);
            return juce::Result::ok();
        }
    const auto document = juce::JSON::parse(file);
    const auto* root = document.getDynamicObject();
    if (root == nullptr || root->getProperty("format").toString() != "CyDAW Device")
        return juce::Result::fail(ui::Ls("Pas un périphérique CyDAW : ", "Not a CyDAW device: ") + file.getFullPathName());
    const auto graphState = root->getProperty("graph");
    const auto schema = static_cast<int>(graphState.getProperty("schema", 0));
    if (static_cast<int>(root->getProperty("schema")) != 1 || !graphState.isObject()
        || graphState.getProperty("nodes", {}).getArray() == nullptr || schema < 1 || schema > app::nodeGraphStateSchema)
        return juce::Result::fail(ui::Ls("Version de périphérique incompatible ou graphe invalide : ",
                                         "Incompatible device version or invalid graph: ") + file.getFullPathName());
    Workshop opened;
    opened.instrument = root->getProperty("role").toString() == "instrument";
    opened.own = makeWorkshopSlot();
    opened.own->restore(withLibrary(root->getProperty("graph"), opened.instrument ? factoryMidiGraph_ : factoryAudioGraph_));
    if (opened.own->graph().editorGraphName().trim().isEmpty())
        opened.own->graph().editorSetGraphName(root->getProperty("name").toString());
    giveOwnIdentity(opened.own->graph(), false);
    opened.file = file;
    openWorkshopTab(std::move(opened));
    return juce::Result::ok();
}

juce::Result Studio::openWorkshopFromTrack(const TrackRef& track, const std::uint64_t nodeId)
{
    if (track.master)
        return juce::Result::fail(ui::Ls("Les périphériques du master s'ouvrent dans le graphe du master",
                                         "The master's devices open in the master's graph"));
    auto* slot = graphFor(track);
    if (slot == nullptr)
        return juce::Result::fail(ui::Ls("Piste introuvable", "No such track"));
    Workshop opened;
    opened.track = track;
    if (nodeId == 0) {
        if (!track.midi)
            return juce::Result::fail(ui::Ls("Le graphe d'une piste audio n'est pas un périphérique : ouvrez l'un de ses périphériques", "An audio track's graph is not a device: open one of its devices"));
        // Already open: its tab comes forward.
        for (auto& tab : workshops_)
            if (tab->track && *tab->track == track && tab->own == nullptr) {
                selectWorkshopTab(tab->id);
                return juce::Result::ok();
            }
        // The instrument is the track's graph: edited directly.
        opened.instrument = true;
        openWorkshopTab(std::move(opened));
        return juce::Result::ok();
    }
    auto& graph = slot->graph();
    const auto module = graph.editorModuleOf(nodeId);
    if (!module)
        return juce::Result::fail(ui::Ls("Cette carte n'est pas un périphérique fait d'un graphe", "This card is not a device made of a graph"));
    for (auto& tab : workshops_)
        if (tab->track && *tab->track == track && tab->moduleKey == module->key) {
            selectWorkshopTab(tab->id);
            return juce::Result::ok();
        }
    // A shared module becomes the track's own copy first, so other tracks
    // never change with it.
    const auto key = graph.editorMakeModuleOwn(nodeId);
    const auto state = key.isNotEmpty() ? graph.editorModuleGraphState(key) : juce::var();
    if (state.getDynamicObject() == nullptr)
        return juce::Result::fail(ui::Ls("Le graphe du périphérique n'a pas pu être ouvert", "The device's graph could not be opened"));
    opened.instrument = module->role == cydaw::graph::GraphRole::instrument;
    opened.moduleKey = key;
    opened.own = makeWorkshopSlot();
    opened.own->restore(state);
    if (opened.own->graph().editorGraphName().trim().isEmpty())
        opened.own->graph().editorSetGraphName(module->name);
    giveOwnIdentity(opened.own->graph(), false);
    openWorkshopTab(std::move(opened));
    return juce::Result::ok();
}

juce::Result Studio::linkWorkshop(const TrackRef& track)
{
    if (track.master)
        return juce::Result::fail(ui::Ls("Le master se teste depuis le studio", "The master is tried from the studio"));
    if (!workshop_)
        return juce::Result::fail(ui::Ls("L'atelier n'a pas de périphérique", "The workshop has no device"));
    if (workshop_->track && *workshop_->track == track)
        return juce::Result::ok();
    unlinkWorkshop();
    auto* slot = graphFor(track);
    if (slot == nullptr || workshop_->own == nullptr)
        return juce::Result::fail(ui::Ls("Piste introuvable", "No such track"));
    commitWorkshop(false);
    auto& graph = slot->graph();
    const auto name = workshop_->own->graph().editorGraphName();
    if (workshop_->instrument) {
        if (!track.midi)
            return juce::Result::fail(ui::Ls("Un instrument va sur une piste MIDI", "An instrument goes on a MIDI track"));
        // Another tab editing that track's instrument keeps a copy of it.
        for (auto& tab : workshops_)
            if (tab.get() != workshop_ && tab->track && *tab->track == track && tab->own == nullptr)
                unlinkTab(*tab);
        // The track plays the instrument: its graph becomes the device's.
        leaveWorkshopDevices();
        slot->restore(withLibrary(workshop_->own->graph().serialise(), factoryMidiGraph_));
        workshop_->own.reset();
        workshop_->track = track;
        dirty_ = true;
        return publishGraph(track, true);
    }
    const auto card = graph.editorInsertModuleFromState(withLibrary(workshop_->own->graph().serialise(), factoryAudioGraph_),
                                                        cydaw::graph::GraphRole::audioEffect, name);
    const auto module = card != 0 ? graph.editorModuleOf(card) : std::nullopt;
    if (!module)
        return juce::Result::fail(ui::Ls("Le périphérique n'a pas pu entrer dans la chaîne", "The device could not be put in the chain"));
    graph_layout::arrange(graph);
    workshop_->track = track;
    workshop_->moduleKey = module->key;
    dirty_ = true;
    return publishGraph(track, true);
}

void Studio::unlinkWorkshop()
{
    if (workshop_ != nullptr)
        unlinkTab(*workshop_);
}

void Studio::unlinkTab(Workshop& tab)
{
    if (!tab.track)
        return;
    if (tab.own == nullptr) {
        // The instrument was the track's graph: the workshop keeps a copy.
        tab.nested.clear();
        auto copy = makeWorkshopSlot();
        if (auto* slot = graphFor(*tab.track))
            copy->restore(withLibrary(slot->graph().serialise(), factoryMidiGraph_));
        else
            copy->restore(factoryMidiGraph_);
        if (copy->graph().editorGraphName().trim().isEmpty())
            copy->graph().editorSetGraphName(trackName(*tab.track));
        tab.own = std::move(copy);
        if (&tab == workshop_)
            publishSimulation(true);
    }
    tab.track.reset();
    tab.moduleKey.clear();
}

void Studio::closeWorkshop()
{
    closeWorkshopTab(0);
}

juce::var Studio::deviceDocument(app::NodeGraphComponent& graph, const bool instrument) const
{
    const auto info = graph.editorGraphInfo();
    auto document = juce::DynamicObject::Ptr(new juce::DynamicObject());
    document->setProperty("format", "CyDAW Device");
    document->setProperty("schema", 1);
    document->setProperty("name", info.name);
    document->setProperty("role", instrument ? "instrument" : "audio-effect");
    document->setProperty("description", info.description);
    document->setProperty("author", info.author);
    document->setProperty("modified", juce::Time::getCurrentTime().toISO8601(true));
    // The device's own graph; the factory modules it uses come back from the
    // application's library when it is opened.
    document->setProperty("graph", withoutSharedModules(graph.serialise()));
    return juce::var(document.get());
}

juce::Result Studio::saveWorkshop(juce::File& written, const bool asNewFile)
{
    auto* root = workshopRoot();
    if (root == nullptr)
        return juce::Result::fail(ui::Ls("L'atelier n'a pas de périphérique", "The workshop has no device"));
    commitWorkshop(false);
    auto& graph = root->graph();
    auto name = graph.editorGraphName().trim();
    if (name.isEmpty()) {
        name = workshop_->track ? trackName(*workshop_->track) : juce::String("Device");
        graph.editorSetGraphName(name);
    }
    auto target = workshop_->file;
    if (asNewFile || target == juce::File()) {
        const auto folder = deviceLibraryFolder();
        if (const auto made = folder.createDirectory(); made.failed())
            return made;
        auto base = juce::File::createLegalFileName(name).trim();
        if (base.isEmpty())
            base = "Device";
        // Never over another device: a number tells them apart.
        target = folder.getChildFile(base + ".cydawdevice");
        for (auto number = 2; target.exists() && target != workshop_->file && number < 1'000; ++number)
            target = folder.getChildFile(base + " " + juce::String(number) + ".cydawdevice");
    }
    if (!target.replaceWithText(juce::JSON::toString(deviceDocument(graph, workshop_->instrument), false)))
        return juce::Result::fail(ui::Ls("Impossible d'écrire ", "Could not write ") + target.getFullPathName());
    workshop_->file = target;
    workshop_->unsaved = false;
    written = target;
    return juce::Result::ok();
}

juce::Result Studio::exportWorkshop(const juce::File& file, juce::StringArray& externalFiles)
{
    externalFiles.clear();
    auto* slot = workshopRoot();
    if (slot == nullptr)
        return juce::Result::fail(ui::Ls("L'atelier n'a pas de périphérique", "The workshop has no device"));
    if (!file.hasFileExtension("cydawdevice"))
        return juce::Result::fail(ui::Ls("Utilisez l'extension .cydawdevice", "Use the .cydawdevice extension"));
    commitWorkshop(false);
    auto document = deviceDocument(slot->graph(), workshop_->instrument);
    auto* root = document.getDynamicObject();
    const auto graph = device_transfer::portableGraph(slot->graph().serialise());
    root->setProperty("graph", graph);
    root->setProperty("portableGraph", true);
    root->setProperty("createdBy", "CyDAW 2");
    device_transfer::externalFiles(graph, externalFiles);
    juce::Array<juce::var> dependencies;
    for (const auto& path : externalFiles)
        dependencies.add(path);
    root->setProperty("externalFiles", dependencies);
    const auto json = juce::JSON::toString(document, false);
    if (json.getNumBytesAsUTF8() > 16 * 1024 * 1024)
        return juce::Result::fail(ui::Ls("Le périphérique dépasse la limite de 16 Mio", "The device exceeds the 16 MiB limit"));
    if (const auto made = file.getParentDirectory().createDirectory(); made.failed())
        return made;
    if (!file.replaceWithText(json))
        return juce::Result::fail(ui::Ls("Impossible d'écrire ", "Could not write ") + file.getFullPathName());
    return juce::Result::ok();
}

juce::Result Studio::enterWorkshopDevice(const std::uint64_t nodeId)
{
    auto* shown = workshopGraph();
    if (shown == nullptr)
        return juce::Result::fail(ui::Ls("L'atelier n'a pas de périphérique", "The workshop has no device"));
    auto& parent = shown->graph();
    if (!parent.editorModuleOf(nodeId))
        return juce::Result::fail(ui::Ls("Cette carte n'est pas un périphérique fait d'un graphe", "This card is not a device made of a graph"));
    const auto key = parent.editorMakeModuleOwn(nodeId);
    const auto state = key.isNotEmpty() ? parent.editorModuleGraphState(key) : juce::var();
    if (state.getDynamicObject() == nullptr)
        return juce::Result::fail(ui::Ls("Le graphe du périphérique n'a pas pu être ouvert", "The device's graph could not be opened"));
    const auto level = workshop_->nested.size();
    auto* owner = workshop_;
    auto slot = std::make_unique<GraphSlot>(GraphSlot::Owner::module, [this, owner, level](GraphSlot& changed, const bool structural) {
        nestedChanged(*owner, level, changed, structural);
    });
    slot->restore(state);
    workshop_->nested.push_back({ key, std::move(slot) });
    return juce::Result::ok();
}

void Studio::leaveWorkshopDevice()
{
    if (workshop_ && !workshop_->nested.empty())
        workshop_->nested.pop_back();
}

void Studio::leaveWorkshopDevices()
{
    if (workshop_)
        workshop_->nested.clear();
}

void Studio::commitWorkshop(const bool structural)
{
    if (!workshop_)
        return;
    for (auto level = workshop_->nested.size(); level > 0; --level)
        if (auto* parent = nestedParent(*workshop_, level - 1))
            parent->editorApplyModuleGraphState(workshop_->nested[level - 1].key,
                                                workshop_->nested[level - 1].slot->graph().serialise(), structural);
    if (workshop_->own != nullptr)
        workshopRootChanged(*workshop_, structural);
    else if (workshop_->track)
        (void) publishGraph(*workshop_->track, structural);
}

void Studio::checkWorkshopLink()
{
    std::vector<std::uint64_t> gone;
    for (auto& tab : workshops_) {
        if (!tab->track)
            continue;
        if (!isValid(*tab->track)) {
            tab->track.reset();
            tab->moduleKey.clear();
            // An instrument that was the track's graph went with it.
            if (tab->own == nullptr)
                gone.push_back(tab->id);
            continue;
        }
        if (tab->moduleKey.isEmpty())
            continue;
        // The device taken out of the track: the workshop keeps its copy.
        auto* slot = graphFor(*tab->track);
        if (slot == nullptr)
            continue;
        auto& graph = slot->graph();
        auto present = false;
        for (const auto& node : graph.editorNodes())
            if (node.composite)
                if (const auto module = graph.editorModuleOf(node.id); module && module->key == tab->moduleKey)
                    present = true;
        if (!present) {
            tab->track.reset();
            tab->moduleKey.clear();
        }
    }
    for (const auto id : gone)
        closeWorkshopTab(id);
}

// ---- The workshop's simulator ------------------------------------------------------------------

bool Studio::simulating() const noexcept
{
    return engine_.isSimulationPlaying();
}

double Studio::simulationSeconds() const noexcept
{
    return engine_.simulationSeconds();
}

bool Studio::simulatesInstrument() const noexcept
{
    return workshop_ != nullptr && workshop_->instrument;
}

void Studio::setSimulatorActive(const bool active)
{
    if (active == simulatorActive_)
        return;
    simulatorActive_ = active;
    if (active) {
        publishSimulation(true);
        publishSimulatorPattern();
        (void) setSimulatorSettings(simulator_);
    }
    engine_.setSimulationActive(active);
}

void Studio::setSimulating(const bool playing)
{
    if (playing) {
        if (workshopRoot() == nullptr)
            return;
        // The workshop's play is the simulation's: the tracks stop.
        if (isRecording())
            return;
        if (engine_.isPlaying())
            engine_.stop();
        if (!simulatorActive_)
            setSimulatorActive(true);
        publishSimulation(true);
    }
    engine_.setSimulationPlaying(playing);
}

void Studio::simulatorNote(const int noteNumber, const bool on)
{
    if (!simulatorActive_)
        return;
    if (on)
        engine_.simulationNoteOn(noteNumber, simulator_.velocity);
    else
        engine_.simulationNoteOff(noteNumber);
}

float Studio::simulatorLevelDb()
{
    const auto peak = engine_.takeSimulationPeak();
    const auto db = peak > 0.0f ? juce::Decibels::gainToDecibels(peak, -120.0f) : -120.0f;
    // Rises at once, falls 24 dB a second, however often it is asked.
    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const auto elapsed = std::clamp(now - simulatorLevelTime_, 0.0, 1.0);
    simulatorLevelTime_ = now;
    simulatorLevel_ = std::max(db, simulatorLevel_ - static_cast<float>(elapsed * 24.0));
    return simulatorLevel_;
}

juce::Result Studio::setSimulatorSettings(const SimulatorSettings& settings)
{
    const auto patternChanged = settings.pattern != simulator_.pattern || settings.rootNote != simulator_.rootNote
                                || settings.velocity != simulator_.velocity;
    const auto sampleChanged = settings.sample != simulator_.sample;
    simulator_ = settings;
    simulator_.source = std::clamp(simulator_.source, 0, static_cast<int>(app::AudioTrackController::SimulationSource::sample));
    simulator_.pattern = std::clamp(simulator_.pattern, 0, static_cast<int>(SimulatorPattern::scale));
    simulator_.rootNote = std::clamp(simulator_.rootNote, 24, 96);
    engine_.setSimulationSource(static_cast<app::AudioTrackController::SimulationSource>(simulator_.source), simulator_.levelDb,
                                simulator_.toneHz);
    engine_.setSimulationOutputGainDb(simulator_.outputDb);
    simulator_.sideSource = std::clamp(simulator_.sideSource, 0, static_cast<int>(app::AudioTrackController::SimulationSideSource::kick));
    engine_.setSimulationSideSource(static_cast<app::AudioTrackController::SimulationSideSource>(simulator_.sideSource));
    if (patternChanged)
        publishSimulatorPattern();
    if (sampleChanged && simulator_.sample != juce::File()) {
        if (const auto loaded = engine_.setSimulationSample(simulator_.sample); loaded.failed())
            return juce::Result::fail(ui::Ls("Ce fichier audio ne s'ouvre pas : ", "This audio file does not open: ")
                                      + simulator_.sample.getFileName());
    }
    return juce::Result::ok();
}

void Studio::publishSimulatorPattern()
{
    using Note = app::AudioTrackController::SimulationNote;
    std::vector<Note> notes;
    const auto root = simulator_.rootNote;
    const auto velocity = simulator_.velocity;
    // I, vi, IV, V around the root, a bar each.
    static constexpr int chords[4][3] = { { 0, 4, 7 }, { -3, 0, 4 }, { -7, -3, 0 }, { -5, -1, 2 } };
    auto length = 16.0;
    switch (static_cast<SimulatorPattern>(simulator_.pattern)) {
    case SimulatorPattern::none:
        break;
    case SimulatorPattern::held:
        notes.push_back({ root, 0.0, 3.9, velocity });
        length = 4.0;
        break;
    case SimulatorPattern::chords:
        for (auto bar = 0; bar < 4; ++bar)
            for (const auto interval : chords[bar])
                notes.push_back({ root + interval, bar * 4.0, 3.8, velocity });
        break;
    case SimulatorPattern::arpeggio:
        for (auto bar = 0; bar < 4; ++bar) {
            static constexpr int order[8] = { 0, 1, 2, 3, 2, 1, 0, 1 };
            for (auto step = 0; step < 16; ++step) {
                const auto degree = order[step % 8];
                const auto note = degree == 3 ? root + chords[bar][0] + 12 : root + chords[bar][degree];
                notes.push_back({ note, bar * 4.0 + step * 0.25, 0.22, velocity * (step % 4 == 0 ? 1.0f : 0.8f) });
            }
        }
        break;
    case SimulatorPattern::bass:
        for (auto bar = 0; bar < 4; ++bar) {
            static constexpr int line[8] = { 0, 0, 12, 0, 0, 12, 0, 7 };
            const auto base = root - 12 + chords[bar][0];
            for (auto step = 0; step < 8; ++step)
                notes.push_back({ base + line[step], bar * 4.0 + step * 0.5, 0.4, velocity * (step % 2 == 0 ? 1.0f : 0.75f) });
        }
        break;
    case SimulatorPattern::scale: {
        static constexpr int steps[16] = { 0, 2, 4, 5, 7, 9, 11, 12, 11, 9, 7, 5, 4, 2, 0, -1 };
        for (auto step = 0; step < 16; ++step)
            notes.push_back({ root + steps[step], step * 0.5, 0.45, velocity });
        length = 8.0;
        break;
    }
    }
    engine_.setSimulationPattern(std::move(notes), length);
}

void Studio::publishSimulation(const bool force)
{
    auto* root = workshopRoot();
    if (root == nullptr || !simulatorActive_)
        return;
    if (!force && root == simulatedSlot_ && root->revision() == simulatedRevision_)
        return;
    const auto sameDevice = root == simulatedSlot_ && simulatedInstrument_ == workshop_->instrument;
    simulatedSlot_ = root;
    simulatedRevision_ = root->revision();
    simulatedInstrument_ = workshop_->instrument;
    auto& graph = root->graph();
    const auto chain = graph.nativeDspGraph();
    if (workshop_->instrument) {
        engine::PolySynthGraphPlan plan;
        (void) graph.compilePolySynthGraph(plan);
        (void) engine_.setSimulationInstrument(plan, chain, graph.editorVoiceModulations());
        return;
    }
    // Values only when the shape held; compiled again otherwise.
    auto result = sameDevice ? engine_.updateSimulationParameters(chain) : juce::Result::fail("new device");
    if (result.failed()) {
        const auto [input, output] = audioEnds(graph);
        juce::ignoreUnused(input);
        const auto mono = output != 0 && graph.editorAudioChannels(output, 0, true) == 1;
        (void) engine_.setSimulationEffect(chain, mono);
    }
}

std::uint64_t Studio::createDeviceFromSelection(GraphSlot& slot, const std::vector<std::uint64_t>& ids,
                                                const juce::String& name, juce::String& error)
{
    const auto card = slot.graph().editorCreateModuleFromSelection(ids, name, error);
    if (card != 0)
        dirty_ = true;
    return card;
}

// ---- Sound matching ------------------------------------------------------------------

juce::File Studio::soundModelFolder()
{
    return locations::folder(Location::soundModels);
}

juce::Result Studio::recreateSound(const juce::Array<juce::File>& files, const juce::String& name, const bool refine,
                                   const juce::StringArray& options)
{
    if (soundMatching_.running)
        return juce::Result::fail(ui::Ls("Une analyse est déjà en cours", "An analysis is already running"));
    juce::StringArray arguments { "analyse" };
    for (const auto& file : files) {
        if (!file.existsAsFile())
            return juce::Result::fail(ui::Ls("Fichier introuvable : ", "No such file: ") + file.getFullPathName());
        if (!file.hasFileExtension("wav"))
            return juce::Result::fail(ui::Ls("Seuls les fichiers WAV sont analysés pour l'instant : ",
                                             "Only WAV files are analysed for now: ")
                                      + file.getFileName());
        arguments.add(file.getFullPathName());
    }
    if (files.isEmpty())
        return juce::Result::fail(ui::Ls("Aucun son à recréer", "No sound to recreate"));
    const auto title = name.trim().isNotEmpty() ? name.trim() : files.getFirst().getFileNameWithoutExtension();
    const auto folder = soundModelFolder();
    (void) folder.createDirectory();
    pendingModel_ = folder.getNonexistentChildFile(juce::File::createLegalFileName(title), ".cysound", false);
    arguments.addArray({ "--out", pendingModel_.getFullPathName(), "--name", title });
    if (refine)
        arguments.add("--refine");
    arguments.addArray(options);
    if (!companion_.start(arguments))
        return juce::Result::fail(ui::Ls("Le compagnon est occupé", "The companion is busy"));
    soundMatching_ = {};
    soundMatching_.running = true;
    soundMatching_.message = ui::Ls("Analyse de ", "Analysing ") + title;
    return juce::Result::ok();
}

void Studio::cancelSoundMatching()
{
    if (synthSearch_ != nullptr) {
        synthSearch_->cancel = true;
        return;
    }
    companion_.cancel();
}

// ---- A synth patch for a recorded note ----

juce::Result Studio::matchSynth(const juce::File& file, const juce::String& synth, const int note, const int evaluations)
{
    namespace synths = engine::synth;
    namespace match = engine::synth_match;
    if (soundMatching_.running)
        return juce::Result::fail(ui::Ls("Une analyse est déjà en cours", "An analysis is already running"));
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        return juce::Result::fail(ui::Ls("Fichier audio illisible : ", "Unreadable audio file: ") + file.getFileName());
    const auto length = static_cast<int>(std::min<juce::int64>(reader->lengthInSamples, static_cast<juce::int64>(reader->sampleRate * 6.0)));
    juce::AudioBuffer<float> buffer(static_cast<int>(std::max(1u, reader->numChannels)), length);
    reader->read(&buffer, 0, length, 0, true, true);
    std::vector<float> mono(static_cast<std::size_t>(length), 0.0f);
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int index = 0; index < length; ++index)
            mono[static_cast<std::size_t>(index)] += buffer.getSample(channel, index) / static_cast<float>(buffer.getNumChannels());
    auto target = match::prepareTarget(mono, reader->sampleRate, note);
    if (target.samples.size() < 4'096)
        return juce::Result::fail(ui::Ls("Le son est trop court ou silencieux", "The sound is too short or silent"));
    std::vector<synths::Model> models;
    if (synth.equalsIgnoreCase("opale") || synth.equalsIgnoreCase("auto"))
        models.push_back(synths::Model::opale);
    if (synth.equalsIgnoreCase("quartz") || synth.equalsIgnoreCase("auto"))
        models.push_back(synths::Model::quartz);
    if (models.empty())
        return juce::Result::fail("synth is opale, quartz or auto");
    if (synthSearch_ != nullptr && synthSearch_->thread.joinable())
        synthSearch_->thread.join();
    synthSearch_ = std::make_unique<SynthSearch>();
    synthSearch_->name = file.getFileNameWithoutExtension();
    soundMatching_ = {};
    soundMatching_.running = true;
    soundMatching_.message = ui::Ls("Recherche d'un patch pour ", "Searching a patch for ") + synthSearch_->name;
    auto* search = synthSearch_.get();
    const auto budget = std::max(200, evaluations);
    search->thread = std::thread([search, target = std::move(target), models, budget] {
        auto best = match::Result {};
        auto bestModel = models.front();
        auto first = true;
        for (std::size_t index = 0; index < models.size() && !search->cancel.load(); ++index) {
            match::Options options;
            options.model = models[index];
            options.evaluations = budget;
            options.cancel = &search->cancel;
            const auto modelName = juce::String(synths::info(models[index]).english);
            options.progress = [search, index, count = models.size(), modelName](const double done, const double loss) {
                search->progress = (static_cast<double>(index) + std::clamp(done, 0.0, 1.0)) / static_cast<double>(count);
                const std::lock_guard lock(search->mutex);
                search->message = modelName + ui::Ls(" : écart ", ": distance ") + juce::String(loss, 3);
                return !search->cancel.load();
            };
            const auto found = match::match(target, options);
            if (!found.settings.empty() && (first || found.loss < best.loss)) {
                best = found;
                bestModel = models[index];
                first = false;
            }
        }
        {
            const std::lock_guard lock(search->mutex);
            search->result = best;
            search->model = bestModel;
        }
        search->done = true;
    });
    return juce::Result::ok();
}

void Studio::finishSynthSearch()
{
    namespace synths = engine::synth;
    auto search = std::move(synthSearch_);
    if (search->thread.joinable())
        search->thread.join();
    soundMatching_.running = false;
    soundMatching_.progress = 1.0;
    if (search->cancel.load()) {
        soundMatching_.error = ui::Ls("Recherche annulée", "Search cancelled");
        return;
    }
    const auto& result = search->result;
    if (result.settings.empty()) {
        soundMatching_.error = ui::Ls("Aucun patch trouvé", "No patch found");
        return;
    }
    const auto instrument = juce::String(synths::info(search->model).instrument);
    const auto track = addMidiTrack(search->name, instrument);
    if (!track) {
        soundMatching_.error = ui::Ls("La piste n'a pas pu être créée", "The track could not be made");
        return;
    }
    (void) setSynthSettings(*track, result.settings);
    (void) publishGraph(*track, true);
    // The level that matched, on its fader.
    auto mix = trackMix(*track);
    mix.gainDb = static_cast<float>(std::clamp(result.gainDb, -24.0, 12.0));
    setTrackMix(*track, mix);
    soundMatching_.track = track;
    soundMatching_.description = search->name + " (" + juce::String(synths::info(search->model).english) + ")";
    auto* report = new juce::DynamicObject();
    report->setProperty("synth", juce::String(synths::info(search->model).english));
    report->setProperty("loss", result.loss);
    report->setProperty("gainDb", result.gainDb);
    report->setProperty("evaluations", result.evaluations);
    juce::Array<juce::var> zones;
    zones.add(juce::var(report));
    soundMatching_.zones = zones;
    dirty_ = true;
}

void Studio::applySoundMatching(const Companion::Status& finished)
{
    soundMatching_.running = false;
    soundMatching_.progress = finished.progress;
    if (finished.failed) {
        soundMatching_.error = finished.error;
        return;
    }
    soundMatching_.zones = finished.result.getProperty("zones", {});
    juce::String error;
    if (const auto track = loadSoundModel(pendingModel_, std::nullopt, error)) {
        soundMatching_.track = track;
        soundMatching_.model = pendingModel_;
    } else {
        soundMatching_.error = error;
    }
}

std::optional<TrackRef> Studio::loadSoundModel(const juce::File& file, std::optional<TrackRef> track, juce::String& error)
{
    juce::var json;
    const auto model = app::loadSoundModelFile(file, json, error);
    if (model == nullptr)
        return std::nullopt;
    const auto cardOf = [this](const TrackRef& on) -> std::uint64_t {
        if (const auto* slot = graphFor(on))
            for (const auto& node : slot->graph().editorNodes())
                if (node.typeKey == "sound-model")
                    return node.id;
        return 0;
    };
    if (!track || cardOf(*track) == 0) {
        const auto name = json.getProperty("name", {}).toString();
        track = addMidiTrack(name.isNotEmpty() ? name : file.getFileNameWithoutExtension(), "sound-model-synth");
        if (!track) {
            error = ui::Ls("La piste instrument n'a pas pu être créée", "The instrument track could not be made");
            return std::nullopt;
        }
    }
    auto* slot = graphFor(*track);
    const auto card = cardOf(*track);
    if (slot == nullptr || card == 0) {
        error = ui::Ls("Cet instrument n'a pas de carte « modèle de son »", "This instrument has no sound-model card");
        return std::nullopt;
    }
    if (!slot->graph().setNodeSoundModel(card, json, error))
        return std::nullopt;
    (void) publishGraph(*track, true);
    soundMatching_.description = app::describeSoundModel(*model);
    dirty_ = true;
    return track;
}

juce::var Studio::soundModelJsonOf(const TrackRef& track) const
{
    // The one the instrument plays, inside a device too.
    if (const auto* slot = graphFor(track))
        return slot->graph().voiceSoundModelJson();
    return {};
}

std::optional<TrackRef> Studio::buildReadablePatch(const juce::var& json, juce::String& error)
{
    const auto model = app::soundModelFromVar(json, error);
    if (model == nullptr)
        return std::nullopt;
    const auto& zone = model->zones.front();
    const auto musical = json.getProperty("musical", {});
    const auto name = json.getProperty("name", {}).toString();
    const auto track = addMidiTrack((name.isNotEmpty() ? name : juce::String("Sound")) + ui::Ls(" (lisible)", " (readable)"),
                                    "table-synth");
    if (!track) {
        error = ui::Ls("La piste instrument n'a pas pu être créée", "The instrument track could not be made");
        return std::nullopt;
    }
    auto* slot = graphFor(*track);
    if (slot == nullptr) {
        error = "no graph";
        return std::nullopt;
    }
    auto& graph = slot->graph();

    // The table: the partials over the loud part of the note, eight frames.
    engine::WavetableShape shape;
    auto loudest = 0.0f;
    auto peakFrame = 0;
    std::vector<float> energy(static_cast<std::size_t>(zone.frames), 0.0f);
    for (int frame = 0; frame < zone.frames; ++frame) {
        for (int k = 0; k < zone.harmonicCount; ++k) {
            const auto a = zone.harmonicAt(frame, k);
            energy[static_cast<std::size_t>(frame)] += a * a;
        }
        if (energy[static_cast<std::size_t>(frame)] > loudest) {
            loudest = energy[static_cast<std::size_t>(frame)];
            peakFrame = frame;
        }
    }
    auto lastLoud = peakFrame;
    for (int frame = zone.frames - 1; frame > peakFrame; --frame)
        if (energy[static_cast<std::size_t>(frame)] > loudest * 1.0e-4f) {
            lastLoud = frame;
            break;
        }
    for (std::size_t row = 0; row < engine::WavetableShape::frameCount; ++row) {
        const auto frame = std::clamp(static_cast<int>(std::lround(lastLoud * static_cast<double>(row) / (engine::WavetableShape::frameCount - 1))),
                                      0, zone.frames - 1);
        auto peak = 1.0e-9f;
        for (std::size_t k = 0; k < engine::WavetableShape::harmonicCount && static_cast<int>(k) < zone.harmonicCount; ++k)
            peak = std::max(peak, zone.harmonicAt(frame, static_cast<int>(k)));
        for (std::size_t k = 0; k < engine::WavetableShape::harmonicCount; ++k)
            shape.harmonics[row][k] = static_cast<int>(k) < zone.harmonicCount
                ? zone.harmonicAt(frame, static_cast<int>(k)) / peak
                : 0.0f;
    }
    (void) graph.setNodeWavetable("wavetable", shape);
    // Built now: the instrument is published right after.
    graph.rebuildWavetableBanksForTest();

    // The knobs: the table read where the note is loudest, the analysed
    // envelope, a low-pass well above the partials' own fall.
    const auto number = [&musical](const char* key, const double fallback) {
        const auto value = musical.getProperty(key, {});
        return value.isVoid() || value.isUndefined() ? fallback : static_cast<double>(value);
    };
    const auto sustainDb = number("sustainDb", -120.0);
    const std::map<juce::String, double> settings {
        { "wavetable.position", lastLoud > 0 ? juce::jlimit(0.0, 1.0, static_cast<double>(peakFrame) / lastLoud) : 0.0 },
        { "wavetable.detune", 0.0 },
        { "wavetable.level", 0.4 },
        { "wavetable.frequency", static_cast<double>(zone.f0) },
        { "envelope.attack", juce::jlimit(1.0, 5'000.0, number("attackSeconds", 0.005) * 1'000.0) },
        { "envelope.decay", juce::jlimit(1.0, 5'000.0, number("decaySeconds", 0.3) * 1'000.0) },
        { "envelope.sustain", sustainDb <= -100.0 ? 0.0 : juce::jlimit(0.0, 1.0, std::pow(10.0, sustainDb / 20.0)) },
        { "envelope.release", juce::jlimit(5.0, 5'000.0, number("releaseSeconds", 0.3) * 1'000.0) },
        { "voiceFilter.cutoff", juce::jlimit(200.0, 18'000.0, number("cutoffHz", 9'000.0) * 2.0) },
    };
    for (const auto& node : graph.editorNodes())
        for (const auto& parameter : node.parameters)
            for (const auto& [suffix, value] : settings)
                if (parameter.binding.endsWith("." + suffix))
                    (void) graph.editorSetParameter(parameter.binding, value);
    (void) publishGraph(*track, true);
    dirty_ = true;
    return track;
}

// ---- The launcher ------------------------------------------------------------------

namespace {

const app::AudioTrackController::MidiClip* midiClipInScene(const app::AudioTrackController& engine,
                                                           const std::uint64_t trackId, const int scene)
{
    if (const auto* clips = engine.midiClipsView(trackId))
        for (const auto& clip : *clips)
            if (clip.launcherScene == scene)
                return &clip;
    return nullptr;
}

} // namespace

bool Studio::voiceLive(const TrackRef& track, engine::VoiceLive& out) const
{
    return track.midi && !track.master && engine_.midiTrackVoiceLive(track.midiId, out);
}

bool Studio::slotHasClip(const TrackRef& track, const int scene) const
{
    return slotPart(track, scene).has_value();
}

bool Studio::launchSlot(const TrackRef& track, const int scene)
{
    if (track.master || !isValid(track))
        return false;
    if (isGroupTrack(track)) {
        // Its tracks' clips there; a track without one stops, if its slot
        // has its stop.
        auto any = false;
        for (const auto& member : groupMembers(track))
            if (slotHasClip(member, scene))
                any = launchSlot(member, scene) || any;
            else if (slotStops(member, scene))
                stopTrackClips(member);
        return any;
    }
    if (!engine_.isPlaying()) {
        // Stopped: it starts with the clips that played at the stop.
        if (!slotHasClip(track, scene))
            return false;
        forgetClipAtStop(track);
        clipsAtStop_.emplace_back(track, scene);
        play();
        return true;
    }
    const auto part = slotPart(track, scene);
    if (!part)
        return false;
    // A hybrid track plays one clip at a time: the other kind's stops.
    if (const auto companion = companionOf(track)) {
        const auto other = *part == track ? *companion : track;
        if (partPlayingScene(other) >= 0 || partPendingScene(other) >= 0)
            stopPart(other);
    }
    if (part->midi) {
        const auto* clip = midiClipInScene(engine_, part->midiId, scene);
        return clip != nullptr && engine_.startMidiClipPreview(part->midiId, clip->id, scene);
    }
    return engine_.launchClip(part->audioIndex, scene);
}

void Studio::stopTrackClips(const TrackRef& track)
{
    if (track.master || !isValid(track))
        return;
    if (isGroupTrack(track))
        for (const auto& member : groupMembers(track))
            stopTrackClips(member);
    forgetClipAtStop(track);
    stopPart(track);
    if (const auto companion = companionOf(track))
        stopPart(*companion);
}

void Studio::stopPart(const TrackRef& part)
{
    if (part.master || !isValid(part))
        return;
    if (part.midi)
        (void) engine_.stopMidiClipPreview(part.midiId);
    else
        (void) engine_.stopLauncherTrack(part.audioIndex, true);
}

int Studio::partPlayingScene(const TrackRef& part) const
{
    if (part.master || !isValid(part))
        return -1;
    return part.midi ? engine_.activeMidiLauncherScene(part.midiId) : engine_.activeLauncherScene(part.audioIndex);
}

int Studio::partPendingScene(const TrackRef& part) const
{
    if (part.master || part.midi || !isValid(part))
        return -1;
    return engine_.pendingLauncherScene(part.audioIndex);
}

void Studio::launchScene(const int scene)
{
    if (!engine_.isPlaying()) {
        // Stopped: the scene's clips start together, with those that
        // played at the stop on the tracks it leaves alone.
        auto any = false;
        if (masterClips_.contains(scene)) {
            masterAtStop_ = scene;
            any = true;
        } else {
            masterAtStop_ = -1;
        }
        for (const auto& track : tracks()) {
            if (slotHasClip(track, scene)) {
                forgetClipAtStop(track);
                clipsAtStop_.emplace_back(track, scene);
                any = true;
            } else if (slotStops(track, scene)) {
                forgetClipAtStop(track);
            }
        }
        if (any)
            play();
        return;
    }
    for (const auto& track : tracks()) {
        if (slotHasClip(track, scene))
            (void) launchSlot(track, scene);
        else if (slotStops(track, scene) && playingScene(track) >= 0)
            stopTrackClips(track);
    }
    // The master: its clip in the scene, or none (an empty slot stops it).
    if (masterClips_.contains(scene))
        (void) launchMasterClip(scene);
    else if (masterPlaying_ >= 0)
        stopMasterClip();
}

void Studio::stopAllClips()
{
    clipsAtStop_.clear();
    stopMasterClip();
    engine_.stopLauncher();
    for (const auto& summary : engine_.midiTrackSummaries())
        (void) engine_.stopMidiClipPreview(summary.id);
}

int Studio::playingScene(const TrackRef& track) const
{
    // A hybrid track's: its own clip's, else its companion's.
    if (const auto own = partPlayingScene(track); own >= 0)
        return own;
    if (const auto companion = companionOf(track))
        return partPlayingScene(*companion);
    return -1;
}

int Studio::pendingScene(const TrackRef& track) const
{
    if (!engine_.isPlaying())
        for (const auto& [stopped, scene] : clipsAtStop_)
            if (stopped == track)
                return scene;
    if (const auto own = partPendingScene(track); own >= 0)
        return own;
    if (const auto companion = companionOf(track))
        return partPendingScene(*companion);
    return -1;
}

bool Studio::slotStops(const TrackRef& track, const int scene) const
{
    if (scene < 0 || scene >= app::AudioTrackController::maximumLauncherScenes || track.master)
        return true;
    if (track.midi) {
        if (const auto found = midiTracks_.find(track.midiId); found != midiTracks_.end())
            return !found->second.launcherStopOff.test(static_cast<std::size_t>(scene));
    } else if (track.audioIndex < audioTracks_.size()) {
        return !audioTracks_[track.audioIndex].launcherStopOff.test(static_cast<std::size_t>(scene));
    }
    return true;
}

void Studio::setSlotStops(const TrackRef& track, const int scene, const bool stops)
{
    if (scene < 0 || scene >= app::AudioTrackController::maximumLauncherScenes || track.master)
        return;
    std::bitset<app::AudioTrackController::maximumLauncherScenes>* off {};
    if (track.midi) {
        if (const auto found = midiTracks_.find(track.midiId); found != midiTracks_.end())
            off = &found->second.launcherStopOff;
    } else if (track.audioIndex < audioTracks_.size()) {
        off = &audioTracks_[track.audioIndex].launcherStopOff;
    }
    if (off == nullptr)
        return;
    off->set(static_cast<std::size_t>(scene), !stops);
    dirty_ = true;
}

int Studio::launcherSceneCount() const
{
    auto last = masterClips_.empty() ? -1 : masterClips_.rbegin()->first;
    for (std::size_t index = 0; index < engine_.trackCount(); ++index)
        for (const auto& clip : engine_.launcherClips(index))
            last = std::max(last, clip.sceneIndex);
    for (const auto& summary : engine_.midiTrackSummaries())
        if (const auto* clips = engine_.midiClipsView(summary.id))
            for (const auto& clip : *clips)
                last = std::max(last, clip.launcherScene);
    const auto stopsOff = [&last](const auto& off) {
        for (auto scene = static_cast<int>(off.size()) - 1; scene > last; --scene)
            if (off.test(static_cast<std::size_t>(scene))) {
                last = scene;
                return;
            }
    };
    for (const auto& state : audioTracks_)
        stopsOff(state.launcherStopOff);
    for (const auto& [id, state] : midiTracks_)
        stopsOff(state.launcherStopOff);
    return std::clamp(last + 3, 8, static_cast<int>(app::AudioTrackController::maximumLauncherScenes));
}

// ---- The master's launcher clips -------------------------------------------------------

const MasterClip* Studio::masterClip(const int scene) const
{
    const auto found = masterClips_.find(scene);
    return found != masterClips_.end() ? &found->second : nullptr;
}

MasterClip& Studio::createMasterClip(const int scene)
{
    auto& clip = masterClips_[std::clamp(scene, 0, app::AudioTrackController::maximumLauncherScenes - 1)];
    if (clip.name.isEmpty())
        clip.name = "Master " + juce::String(scene + 1);
    dirty_ = true;
    return clip;
}

void Studio::removeMasterClip(const int scene)
{
    if (masterPlaying_ == scene)
        stopMasterClip();
    if (masterAtStop_ == scene)
        masterAtStop_ = -1;
    if (masterClips_.erase(scene) > 0)
        dirty_ = true;
}

void Studio::setMasterClip(const int scene, MasterClip clip)
{
    for (auto& [binding, points] : clip.lanes)
        std::sort(points.begin(), points.end(), [](const auto& a, const auto& b) { return a.beats < b.beats; });
    clip.lengthBeats = std::clamp(clip.lengthBeats, 0.25, 4'096.0);
    masterClips_[scene] = std::move(clip);
    dirty_ = true;
    if (masterPlaying_ == scene)
        publishMasterClip();
}

void Studio::setMasterClips(std::map<int, MasterClip> clips)
{
    stopMasterClip();
    masterAtStop_ = -1;
    masterClips_ = std::move(clips);
}

bool Studio::launchMasterClip(const int scene)
{
    if (!masterClips_.contains(scene))
        return false;
    if (!engine_.isPlaying()) {
        // As the tracks' clips: it starts with the transport.
        masterAtStop_ = scene;
        play();
        return true;
    }
    // From the next quantisation boundary, as the tracks' clips launch.
    using Quantization = app::AudioTrackController::LaunchQuantization;
    const auto tempo = std::max(20.0, engine_.tempo());
    const auto now = engine_.masterClockSeconds();
    auto quantum = 0.0;
    switch (engine_.launchQuantization()) {
    case Quantization::quarterBeat: quantum = 0.25; break;
    case Quantization::halfBeat: quantum = 0.5; break;
    case Quantization::oneBeat: quantum = 1.0; break;
    case Quantization::oneBar: quantum = std::max(1.0, static_cast<double>(engine_.beatsPerBar())); break;
    case Quantization::none: break;
    }
    auto start = now;
    if (quantum > 0.0)
        start = std::ceil(now * tempo / 60.0 / quantum - 1.0e-6) * quantum * 60.0 / tempo;
    masterPlaying_ = scene;
    masterStartSeconds_ = start;
    publishMasterClip();
    return true;
}

void Studio::stopMasterClip()
{
    masterAtStop_ = -1;
    if (masterPlaying_ < 0)
        return;
    masterPlaying_ = -1;
    publishMasterClip();
}

float Studio::masterClipProgress() const
{
    const auto* clip = masterClip(masterPlaying_);
    if (clip == nullptr || !engine_.isPlaying())
        return 0.0f;
    const auto length = clip->lengthBeats * 60.0 / std::max(20.0, engine_.tempo());
    const auto elapsed = engine_.masterClockSeconds() - masterStartSeconds_;
    if (elapsed < 0.0 || length <= 0.0)
        return 0.0f;
    return static_cast<float>(clip->looping ? std::fmod(elapsed, length) / length : std::min(1.0, elapsed / length));
}

std::vector<Studio::MasterLaneChoice> Studio::masterLaneChoices() const
{
    std::vector<MasterLaneChoice> choices;
    choices.push_back({ masterGainLane, ui::Ls("Volume du master", "Master level"), "dB", -60.0, 12.0, engine_.masterGainDb() });
    if (const auto* slot = graphFor(masterTrack()))
        for (const auto& node : slot->graph().editorNodes()) {
            if (node.boundary)
                continue;
            for (const auto& parameter : node.parameters)
                if (modulation::nativeTargetFor(nativeBinding(slot, parameter.binding)))
                    choices.push_back({ parameter.binding, node.title + juce::String::fromUTF8(" \xc2\xb7 ") + parameter.label,
                                        parameter.unit, parameter.minimum, parameter.maximum, parameter.value,
                                        logarithmicParameter(masterTrack(), parameter.binding) });
        }
    return choices;
}

float Studio::masterLaneValueAt(const std::vector<MasterClip::Point>& points, const double beats, const bool logarithmic)
{
    if (points.empty())
        return 0.0f;
    const auto onScale = logarithmic && std::all_of(points.begin(), points.end(), [](const MasterClip::Point& point) { return point.value > 0.0f; });
    return static_cast<float>(engine::curveValueAt(
        points, [](const MasterClip::Point& point) { return engine::CurveKnot { point.beats, point.value, point.shape, point.bend, point.handles }; },
        beats, onScale));
}

void Studio::publishMasterClip()
{
    std::vector<app::NativeDspRuntime::AutomationLane> lanes;
    const auto tempo = std::max(20.0, engine_.tempo());
    masterPublishedTempo_ = tempo;
    if (const auto* clip = masterClip(masterPlaying_)) {
        const auto* slot = graphFor(masterTrack());
        const auto secondsPerBeat = 60.0 / tempo;
        for (const auto& [binding, points] : clip->lanes) {
            if (binding == masterGainLane || points.empty())
                continue;
            const auto target = modulation::nativeTargetFor(nativeBinding(slot, binding));
            if (!target)
                continue;
            app::NativeDspRuntime::AutomationLane lane;
            lane.nodeId = target->nodeId;
            lane.field = target->field;
            lane.slot = target->slot;
            lane.logarithmic = logarithmicParameter(masterTrack(), binding);
            lane.minimum = target->minimum;
            lane.maximum = target->maximum;
            for (const auto& point : points) {
                // Its handles' beats in seconds too.
                auto handles = point.handles;
                handles.inTime = static_cast<float>(handles.inTime * secondsPerBeat);
                handles.outTime = static_cast<float>(handles.outTime * secondsPerBeat);
                lane.points.push_back({ point.beats * secondsPerBeat, std::clamp(point.value, target->minimum, target->maximum), point.shape,
                                        point.bend, handles });
            }
            lane.clipStartSeconds = masterStartSeconds_;
            lane.clipLengthSeconds = clip->lengthBeats * secondsPerBeat;
            lane.clipLooping = clip->looping;
            lanes.push_back(std::move(lane));
        }
    }
    engine_.setMasterNativeAutomation(std::move(lanes));
    // The level back where it was when no clip moves it.
    if (masterClip(masterPlaying_) == nullptr || !masterClip(masterPlaying_)->lanes.contains(masterGainLane))
        if (const auto before = std::exchange(masterGainBeforeClip_, std::nullopt))
            engine_.setMasterGain(*before);
}

void Studio::followMasterClip()
{
    const auto* clip = masterClip(masterPlaying_);
    if (clip == nullptr)
        return;
    // Another tempo: the clip's beats are other seconds.
    if (std::abs(std::max(20.0, engine_.tempo()) - masterPublishedTempo_) > 1.0e-6)
        publishMasterClip();
    // Its level lane moves the master's fader, as the screen shows.
    const auto lane = clip->lanes.find(masterGainLane);
    if (lane == clip->lanes.end() || lane->second.empty() || !engine_.isPlaying())
        return;
    const auto secondsPerBeat = 60.0 / std::max(20.0, engine_.tempo());
    auto beats = (engine_.masterClockSeconds() - masterStartSeconds_) / secondsPerBeat;
    if (beats < 0.0)
        return;
    beats = clip->looping ? std::fmod(beats, clip->lengthBeats) : std::min(beats, clip->lengthBeats);
    const auto value = masterLaneValueAt(lane->second, beats, false);
    if (!masterGainBeforeClip_)
        masterGainBeforeClip_ = engine_.masterGainDb();
    engine_.setMasterGain(std::clamp(value, -60.0f, 12.0f));
}

// ---- Where things are kept ------------------------------------------------------------

void Studio::applyLocations()
{
    app::setBanksFolder(locations::folder(Location::banks));
    samples_->useCacheFolder(locations::folder(Location::cache).getChildFile("samples"));
    banks_.forgetDisk();
    if (samplesStarted_)
        syncSampleBanks();
    if (semantic_ != nullptr)
        semantic_->modelsChanged();
}

namespace {

// What the factory catalogue says of the banks' files: the instrument each
// plays in, its category, that it is one note of it; and of each bank as a
// whole ("bank:<id>", weak), for the files no instrument plays: the category
// most of its files have, its instrument when it has one only; a kit's,
// what its name says or a percussion; a bank no instrument plays, what its
// name or its description (the registry's) says.
std::map<juce::String, app::SampleHint> sampleHintsOf(const juce::File& factory, const juce::File& banks, const juce::File& registry,
                                                      const bool french)
{
    std::map<juce::String, app::SampleHint> hints;
    struct BankSummary {
        std::map<juce::String, int> categories;
        juce::StringArray instruments;
        bool kit {};
        int files {};
    };
    std::map<juce::String, BankSummary> summaries;
    const auto catalogue = juce::JSON::parse(factory.getChildFile("catalogue.json").loadFileAsString());
    const auto* list = catalogue.getProperty("instruments", {}).getArray();
    if (list == nullptr)
        return hints;
    for (const auto& item : *list) {
        const auto names = item.getProperty("names", {});
        const auto name = names.getProperty(french ? "fr" : "en", names.getProperty("en", item.getProperty("key", {}))).toString();
        const auto category = item.getProperty("category", {}).toString();
        const auto map = factory.getChildFile(item.getProperty("map", {}).toString());
        if (!map.existsAsFile())
            continue;
        juce::String error;
        const auto json = app::loadSamplerMapFile(map, error);
        const auto* zones = json.getProperty("zones", {}).getArray();
        if (zones == nullptr)
            continue;
        // A kit plays drums, an instrument its notes.
        const auto kit = category == "drums" || category == "electro-drums";
        for (const auto& zone : *zones) {
            const auto stored = zone.getProperty("file", {}).toString();
            const auto bank = app::bankOfSamplePath(stored);
            if (bank.isEmpty())
                continue;
            const auto file = banks.getChildFile(bank).getChildFile(app::pathInBank(stored));
            const auto key = app::SampleLibrary::hintKey(file.getFullPathName());
            auto& summary = summaries[bank];
            summary.instruments.addIfNotAlreadyThere(name);
            summary.kit = summary.kit || kit;
            if (hints.contains(key))
                continue;
            const auto relative = file.getParentDirectory().getRelativePathFrom(banks.getChildFile(bank));
            app::SampleHint hint;
            hint.category = app::SampleLibrary::categoryFromCatalogue(category, file.getFileNameWithoutExtension(), relative);
            hint.instrument = name;
            hint.note = !kit;
            ++summary.categories[hint.category];
            ++summary.files;
            hints[key] = std::move(hint);
        }
    }
    // The registry's titles and descriptions (in English, the words the
    // rules know).
    std::map<juce::String, std::pair<juce::String, juce::String>> described;
    const auto registryJson = juce::JSON::parse(registry.loadFileAsString());
    if (const auto* registered = registryJson.getProperty("banks", {}).getArray())
        for (const auto& bank : *registered) {
            const auto description = bank.getProperty("description", {});
            described[bank.getProperty("id", {}).toString()]
                = { bank.getProperty("title", {}).toString(),
                    description.isString() ? description.toString() : description.getProperty("en", {}).toString() };
        }
    for (const auto& [bank, entry] : described)
        summaries.try_emplace(bank);
    for (const auto& [bank, summary] : summaries) {
        app::SampleHint hint;
        hint.weak = true;
        hint.note = !summary.kit && !summary.instruments.isEmpty();
        if (summary.instruments.size() == 1)
            hint.instrument = summary.instruments[0];
        const auto found = described.find(bank);
        const auto title = found != described.end() ? found->second.first : juce::String();
        const auto description = found != described.end() ? found->second.second : juce::String();
        if (summary.kit) {
            // A kit's files differ one from the next: what its name says when
            // it is a drum, a percussion otherwise.
            const auto named = app::SampleLibrary::categoryFromName(title, bank);
            hint.category = app::SampleLibrary::familyOf(named) == "drums" && named != "drums" ? named : juce::String("perc");
        } else {
            for (const auto& [category, count] : summary.categories)
                if (category.isNotEmpty() && count * 10 >= summary.files * 6)
                    hint.category = category;
            if (hint.category.isEmpty() && summary.instruments.isEmpty()) {
                hint.category = app::SampleLibrary::categoryFromName(title, bank);
                if (hint.category.isEmpty())
                    hint.category = app::SampleLibrary::categoryFromName(description, {});
            }
        }
        if (hint.category.isNotEmpty() || hint.instrument.isNotEmpty())
            hints["bank:" + bank] = std::move(hint);
    }
    return hints;
}

} // namespace

void Studio::syncSampleBanks()
{
    std::vector<app::SampleLibrary::Bank> list;
    for (const auto& bank : banks_.banks())
        if (const auto folder = banks_.folderOf(bank.id); folder.isDirectory())
            list.push_back({ bank.id, bank.title.isNotEmpty() ? bank.title : bank.id, folder });
    samples_->setBanks(std::move(list));
}

void Studio::startSamples(const bool automatic)
{
    samplesAutomatic_ = automatic;
    semantic_->setAutomatic(automatic);
    if (automatic)
        ensureSamples();
}

void Studio::ensureSamples()
{
    if (samplesStarted_)
        return;
    samplesStarted_ = true;
    syncSampleBanks();
    // The catalogue read off the message thread, then the library started.
    background_.addJob([alive = std::weak_ptr<int>(alive_), this, factory = app::factoryContentFolder(),
                        banks = locations::folder(Location::banks), registry = banks_.registryFile(), french = ui::french()] {
        auto hints = sampleHintsOf(factory, banks, registry, french);
        juce::MessageManager::callAsync([alive, this, hints = std::move(hints)]() mutable {
            if (alive.expired())
                return;
            samples_->setHints(std::move(hints));
            samples_->start();
        });
    });
}

bool Studio::setLocation(const Location location, const juce::File& folder, const bool takeContent, juce::String& error)
{
    if (locationMove_ != nullptr) {
        error = ui::Ls("Un déplacement est déjà en cours", "A move is already running");
        return false;
    }
    if (location == Location::banks && banks_.busy()) {
        error = ui::Ls("Un téléchargement est en cours", "A download is running");
        return false;
    }
    const auto target = folder == juce::File {} ? locations::defaultFolder(location) : folder;
    const auto current = locations::folder(location);
    if (target != current) {
        if (takeContent && target.isAChildOf(current)) {
            error = ui::Ls("Le nouveau dossier est dans l'ancien", "The new folder is inside the old one");
            return false;
        }
        if (target.createDirectory().failed()) {
            error = ui::Ls("Impossible de créer ", "Cannot make ") + target.getFullPathName();
            return false;
        }
        if (takeContent && locations::movable(location) && !locations::contentOf(location, current).isEmpty()) {
            locationMove_ = std::make_unique<locations::Move>(location, current, target);
            return true;
        }
    }
    locations::set(location, folder);
    applyLocations();
    return true;
}

Studio::LocationMove Studio::locationMove() const
{
    LocationMove move;
    if (locationMove_ != nullptr) {
        move.active = true;
        move.location = locationMove_->location();
        move.to = locationMove_->destination();
        move.progress = locationMove_->progress();
    }
    return move;
}

void Studio::cancelLocationMove()
{
    pendingLocations_.clear();
    if (locationMove_ != nullptr)
        locationMove_->cancel();
}

bool Studio::setAllLocations(const juce::File& root, const bool creations, const bool takeContent, juce::String& error)
{
    if (locationMove_ != nullptr || !pendingLocations_.empty()) {
        error = ui::Ls("Un déplacement est déjà en cours", "A move is already running");
        return false;
    }
    if (root == juce::File {} || root.createDirectory().failed()) {
        error = ui::Ls("Impossible de créer ", "Cannot make ") + root.getFullPathName();
        return false;
    }
    for (const auto location : allLocations)
        if (locations::downloaded(location) || creations)
            if (const auto target = locations::inside(location, root); target != locations::folder(location))
                pendingLocations_.emplace_back(location, target);
    pendingTake_ = takeContent;
    nextLocation();
    return true;
}

// The next location of setAllLocations, once no move runs; one refused ends them.
void Studio::nextLocation()
{
    while (locationMove_ == nullptr && !pendingLocations_.empty()) {
        const auto [location, folder] = pendingLocations_.front();
        pendingLocations_.erase(pendingLocations_.begin());
        juce::String error;
        if (!setLocation(location, folder, pendingTake_, error)) {
            notices_.emplace_back(ui::Ls("Emplacement non changé : ", "Location not changed: ") + error, true);
            pendingLocations_.clear();
        }
    }
}

std::vector<std::pair<juce::String, bool>> Studio::takeNotices()
{
    std::vector<std::pair<juce::String, bool>> taken;
    taken.swap(notices_);
    return taken;
}

// ---- Presets ------------------------------------------------------------------------------

std::optional<Studio::PresetHolder> Studio::devicePresetHolder(const TrackRef& track, const std::uint64_t nodeId) const
{
    const auto* slot = graphFor(track);
    if (slot == nullptr)
        return std::nullopt;
    for (const auto& node : slot->graph().editorNodes())
        if (node.id == nodeId)
            return PresetHolder { "device", node.typeKey,
                                  juce::String(static_cast<juce::int64>(routingKey(track))) + "/device/"
                                      + juce::String(static_cast<juce::int64>(nodeId)) };
    return std::nullopt;
}

std::optional<Studio::PresetHolder> Studio::pluginPresetHolder(const TrackRef& track, const int pluginIndex) const
{
    if (track.midi || track.master || !isValid(track))
        return std::nullopt;
    const auto plugins = engine_.trackPlugins(track.audioIndex);
    if (pluginIndex < 0 || pluginIndex >= static_cast<int>(plugins.size()))
        return std::nullopt;
    return PresetHolder { "plugin", plugins[static_cast<std::size_t>(pluginIndex)].name,
                          juce::String(static_cast<juce::int64>(routingKey(track))) + "/plugin/" + juce::String(pluginIndex) };
}

std::optional<Studio::PresetHolder> Studio::chainPresetHolder(const TrackRef& track) const
{
    if (graphFor(track) == nullptr)
        return std::nullopt;
    const auto role = track.isBus() ? "bus" : track.master ? "master" : track.midi ? "instrument" : "audio";
    return PresetHolder { "chain", role, juce::String(static_cast<juce::int64>(routingKey(track))) + "/chain" };
}

juce::var Studio::devicePresetState(const TrackRef& track, const std::uint64_t nodeId) const
{
    const auto* slot = graphFor(track);
    if (slot == nullptr)
        return {};
    auto state = device_ops::presetState(slot->graph(), nodeId);
    auto* object = state.getDynamicObject();
    if (object == nullptr)
        return state;
    // A container: all it holds (its branches' cards, their modulators).
    if (slot->graph().editorIsContainer(nodeId))
        if (const auto whole = slot->graph().editorContainerState(nodeId); whole.isObject())
            object->setProperty("container", whole);
    // An instrument container: its pads or layers, each its instrument and effects.
    if (track.midi && track.pad < 0 && rackPadCount(track) > 0)
        for (const auto& node : slot->graph().editorNodes())
            if (node.id == nodeId && engine::instrument_container::isContainerType(node.typeKey.toStdString())) {
                object->setProperty("pads", rackPadsState(track));
                object->setProperty("padsKind", padsKindName(track));
            }
    return state;
}

bool Studio::applyDevicePreset(const TrackRef& track, const std::uint64_t nodeId, const juce::var& state,
                               juce::String& error)
{
    auto* slot = graphFor(track);
    if (slot == nullptr) {
        error = ui::Ls("Piste introuvable", "No such track");
        return false;
    }
    // A container's: all it holds, in place of what it holds (its knobs with them).
    if (const auto whole = state.getProperty("container", {}); whole.isObject() && slot->graph().editorIsContainer(nodeId)) {
        // Laid where its head is (one undo step: no tidying after it).
        if (!slot->graph().editorApplyContainerState(nodeId, whole)) {
            error = ui::Ls("Ce préréglage est celui d'un autre conteneur", "This preset is another container's");
            return false;
        }
        dirty_ = true;
        return publishGraph(track, true).wasOk();
    }
    const auto kit = [this, &track, nodeId](const int number, juce::String& problem) {
        return setBatterieKit(track, nodeId, number, problem);
    };
    if (!device_ops::applyPreset(slot->graph(), nodeId, state, error, kit))
        return false;
    dirty_ = true;
    // An instrument container's pads or layers.
    if (const auto pads = state.getProperty("pads", {}); pads.isArray() && track.midi && track.pad < 0 && isRack(track)) {
        (void) publishGraph(track, true);
        restoreRackPads(track, pads, state.getProperty("padsKind", {}).toString());
        return true;
    }
    return publishGraph(track, true).wasOk();
}


bool Studio::importPrismTable(const TrackRef& track, const std::uint64_t nodeId, const int oscillator, const juce::File& file,
                              juce::String& error)
{
    auto* slot = graphFor(track);
    if (slot == nullptr) {
        error = ui::Ls("Pas de carte Prisme ici", "No Prisme card here");
        return false;
    }
    if (!device_ops::importPrismTable(slot->graph(), nodeId, oscillator, file, error))
        return false;
    dirty_ = true;
    return true;
}

bool Studio::loadCabImpulse(const TrackRef& track, const std::uint64_t nodeId, const juce::File& file, juce::String& error)
{
    auto* slot = graphFor(track);
    if (slot == nullptr) {
        error = ui::Ls("Piste introuvable", "No such track");
        return false;
    }
    if (!device_ops::loadImpulse(slot->graph(), nodeId, file, error))
        return false;
    dirty_ = true;
    return true;
}

bool Studio::clearCabImpulse(const TrackRef& track, const std::uint64_t nodeId)
{
    auto* slot = graphFor(track);
    if (slot == nullptr || !slot->graph().nodeImpulseJson(nodeId).isObject())
        return false;
    juce::String error;
    if (!slot->graph().setNodeImpulse(nodeId, {}, error))
        return false;
    dirty_ = true;
    return true;
}

std::optional<std::vector<std::uint32_t>> Studio::pluginPins(const TrackRef& track, const std::size_t plugin) const
{
    if (track.master || track.midi || track.audioIndex >= audioTracks_.size())
        return std::nullopt;
    const auto& pins = audioTracks_[track.audioIndex].pluginPins;
    return plugin < pins.size() ? pins[plugin] : std::nullopt;
}

void Studio::setPluginPins(const TrackRef& track, const std::size_t plugin, std::vector<std::uint32_t> pins)
{
    if (track.master || track.midi || track.audioIndex >= audioTracks_.size())
        return;
    auto& all = audioTracks_[track.audioIndex].pluginPins;
    if (all.size() <= plugin)
        all.resize(plugin + 1);
    all[plugin] = std::move(pins);
    // The modulation's list of parameters follows the pins.
    audioTracks_[track.audioIndex].modulationSyncedRevision = 0;
    dirty_ = true;
}

namespace {
// "plugin:<p>:<param>" moved to another plug-in index (or nothing to drop).
juce::String movedPluginBinding(const juce::String& binding, const std::size_t from, const std::optional<std::size_t> to)
{
    if (!binding.startsWith("plugin:"))
        return binding;
    const auto rest = binding.fromFirstOccurrenceOf("plugin:", false, false);
    const auto plugin = static_cast<std::size_t>(rest.upToFirstOccurrenceOf(":", false, false).getLargeIntValue());
    if (plugin != from)
        return binding;
    return to ? "plugin:" + juce::String(static_cast<juce::int64>(*to)) + ":" + rest.fromFirstOccurrenceOf(":", false, false)
              : juce::String();
}
} // namespace

void Studio::removePlugin(const TrackRef& track, const std::size_t plugin)
{
    if (track.master || track.midi || track.audioIndex >= audioTracks_.size())
        return;
    const auto count = engine_.trackPlugins(track.audioIndex).size();
    engine_.clearTrackPlugin(track.audioIndex, plugin);
    auto& state = audioTracks_[track.audioIndex];
    if (plugin < state.pluginPins.size())
        state.pluginPins.erase(state.pluginPins.begin() + static_cast<std::ptrdiff_t>(plugin));
    // Its modulations go; the ones after it move down.
    auto assignments = modulation::readAssignments(state.modulationAssignments);
    std::vector<modulation::Assignment> kept;
    for (auto& assignment : assignments) {
        assignment.binding = movedPluginBinding(assignment.binding, plugin, std::nullopt);
        for (auto after = plugin + 1; after < count; ++after)
            assignment.binding = movedPluginBinding(assignment.binding, after, after - 1);
        if (assignment.binding.isNotEmpty())
            kept.push_back(assignment);
    }
    state.modulationAssignments = modulation::writeAssignments(kept);
    state.modulationSyncedRevision = 0;
    dirty_ = true;
}

bool Studio::movePlugin(const TrackRef& track, const std::size_t plugin, const int direction)
{
    if (track.master || track.midi || track.audioIndex >= audioTracks_.size())
        return false;
    const auto count = engine_.trackPlugins(track.audioIndex).size();
    const auto other = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(plugin) + (direction < 0 ? -1 : 1));
    if (plugin >= count || other >= count || !engine_.moveTrackPlugin(track.audioIndex, plugin, direction < 0 ? -1 : 1))
        return false;
    auto& state = audioTracks_[track.audioIndex];
    if (state.pluginPins.size() < count)
        state.pluginPins.resize(count);
    std::swap(state.pluginPins[plugin], state.pluginPins[other]);
    auto assignments = modulation::readAssignments(state.modulationAssignments);
    for (auto& assignment : assignments) {
        const auto first = movedPluginBinding(assignment.binding, plugin, other);
        assignment.binding = first != assignment.binding ? first : movedPluginBinding(assignment.binding, other, plugin);
    }
    state.modulationAssignments = modulation::writeAssignments(assignments);
    state.modulationSyncedRevision = 0;
    dirty_ = true;
    return true;
}

bool Studio::clearPrismTable(const TrackRef& track, const std::uint64_t nodeId, const int oscillator)
{
    auto* slot = graphFor(track);
    if (slot == nullptr || !device_ops::clearPrismTable(slot->graph(), nodeId, oscillator))
        return false;
    dirty_ = true;
    return true;
}

juce::var Studio::pluginPresetState(const TrackRef& track, const int pluginIndex)
{
    const auto holder = pluginPresetHolder(track, pluginIndex);
    if (!holder)
        return {};
    juce::MemoryBlock block;
    if (engine_.getTrackPluginState(track.audioIndex, static_cast<std::size_t>(pluginIndex), block).failed()
        && engine_.getCachedTrackPluginState(track.audioIndex, static_cast<std::size_t>(pluginIndex), block).failed())
        return {};
    const auto plugins = engine_.trackPlugins(track.audioIndex);
    auto* state = new juce::DynamicObject();
    state->setProperty("plugin", holder->target);
    state->setProperty("file", plugins[static_cast<std::size_t>(pluginIndex)].file.getFileName());
    state->setProperty("state", block.toBase64Encoding());
    return juce::var(state);
}

bool Studio::applyPluginPreset(const TrackRef& track, const int pluginIndex, const juce::var& state, juce::String& error)
{
    const auto holder = pluginPresetHolder(track, pluginIndex);
    if (!holder) {
        error = ui::Ls("Plug-in introuvable", "No such plug-in");
        return false;
    }
    if (const auto plugin = state.getProperty("plugin", {}).toString(); plugin.isNotEmpty() && plugin != holder->target) {
        error = ui::Ls("Ce préréglage est celui d'un autre plug-in : ", "This preset is another plug-in's: ") + plugin;
        return false;
    }
    juce::MemoryBlock block;
    if (!block.fromBase64Encoding(state.getProperty("state", {}).toString())) {
        error = ui::Ls("Préréglage illisible", "Unreadable preset");
        return false;
    }
    if (const auto result = engine_.setTrackPluginState(track.audioIndex, static_cast<std::size_t>(pluginIndex), block);
        result.failed()) {
        error = result.getErrorMessage();
        return false;
    }
    dirty_ = true;
    return true;
}

bool Studio::pluginPrograms(const TrackRef& track, const int pluginIndex, juce::StringArray& names, int& current)
{
    names.clear();
    current = -1;
    if (!pluginPresetHolder(track, pluginIndex))
        return false;
    return engine_.getTrackPluginPrograms(track.audioIndex, static_cast<std::size_t>(pluginIndex), names, current).wasOk();
}

bool Studio::selectPluginProgram(const TrackRef& track, const int pluginIndex, const int program, juce::String& error)
{
    if (!pluginPresetHolder(track, pluginIndex)) {
        error = ui::Ls("Plug-in introuvable", "No such plug-in");
        return false;
    }
    if (const auto result = engine_.setTrackPluginProgram(track.audioIndex, static_cast<std::size_t>(pluginIndex), program);
        result.failed()) {
        error = result.getErrorMessage();
        return false;
    }
    dirty_ = true;
    return true;
}

juce::var Studio::chainPresetState(const TrackRef& track) const
{
    const auto* slot = graphFor(track);
    if (slot == nullptr)
        return {};
    auto* state = new juce::DynamicObject();
    state->setProperty("graph", withoutSharedModules(slot->serialise()));
    // An instrument container's pads or layers go with it.
    if (track.midi && track.pad < 0 && isRack(track) && rackPadCount(track) > 0) {
        state->setProperty("pads", rackPadsState(track));
        state->setProperty("padsKind", padsKindName(track));
    }
    return juce::var(state);
}

bool Studio::applyChainPreset(const TrackRef& track, const juce::var& state, juce::String& error)
{
    auto* slot = graphFor(track);
    const auto graph = state.getProperty("graph", {});
    if (slot == nullptr || !graph.isObject()) {
        error = ui::Ls("Préréglage illisible", "Unreadable preset");
        return false;
    }
    if (workshop_ && workshop_->track && *workshop_->track == track && workshop_->own == nullptr)
        unlinkWorkshop();
    slot->restore(withLibrary(graph, track.midi ? factoryMidiGraph_ : factoryAudioGraph_));
    dirty_ = true;
    if (const auto result = publishGraph(track, true); result.failed()) {
        error = result.getErrorMessage();
        return false;
    }
    // An instrument container's pads or layers came with it; a container
    // without (a chain saved empty) starts with its first ones.
    if (track.midi && track.pad < 0) {
        if (const auto pads = state.getProperty("pads", {}); pads.isArray())
            restoreRackPads(track, pads, state.getProperty("padsKind", {}).toString());
        else if (const auto kind = containerKind(track); kind != engine::instrument_container::Kind::none) {
            if (const auto found = midiTracks_.find(track.midiId); found != midiTracks_.end() && found->second.padsKind != kind) {
                clearPads(track);
                found->second.padsKind = kind;
            }
            if (rackPadCount(track) == 0) {
                if (kind == engine::instrument_container::Kind::drum)
                    fillDefaultRack(track);
                else
                    fillDefaultLayers(track);
            }
            publishPadMixes(track);
        }
    }
    return true;
}

std::vector<std::pair<juce::String, juce::String>> Studio::factoryChains(const TrackRef& track) const
{
    std::vector<std::pair<juce::String, juce::String>> chains;
    const auto* slot = graphFor(track);
    if (slot == nullptr || track.midi)
        return chains;
    for (const auto& device : slot->graph().deviceCatalogue())
        if (device.kind == "preset" && !device.soundSource && device.input == "audio" && device.output == "audio")
            chains.emplace_back(device.key, device.name);
    std::sort(chains.begin(), chains.end(), [](const auto& a, const auto& b) { return a.second.compareNatural(b.second) < 0; });
    return chains;
}

juce::Result Studio::applyFactoryChain(const TrackRef& track, const juce::String& key)
{
    auto* slot = graphFor(track);
    if (slot == nullptr)
        return juce::Result::fail(ui::Ls("Piste introuvable", "No such track"));
    const auto chains = factoryChains(track);
    if (std::none_of(chains.begin(), chains.end(), [&key](const auto& chain) { return chain.first == key; }))
        return juce::Result::fail(ui::Ls("Pas une chaîne de CyDAW pour cette piste : ", "Not one of CyDAW's chains for this track: ") + key);
    if (workshop_ && workshop_->track && *workshop_->track == track && workshop_->own == nullptr)
        unlinkWorkshop();
    auto& graph = slot->graph();
    if (const auto applied = graph.insertFactoryDevice(key); applied.failed())
        return applied;
    graph_layout::arrange(graph);
    dirty_ = true;
    return publishGraph(track, true);
}

juce::String Studio::presetName(const juce::String& holderKey) const
{
    const auto found = presetNames_.find(holderKey);
    return found != presetNames_.end() ? found->second : juce::String();
}

void Studio::setPresetName(const juce::String& holderKey, const juce::String& name)
{
    presetNames_[holderKey] = name;
}

// ---- Sound banks ------------------------------------------------------------------------

bool Studio::downloadFactoryInstrument(const juce::String& key, juce::String& error)
{
    for (const auto& instrument : factoryInstruments())
        if (instrument.key == key) {
            const auto missing = banks_.missingForFile(instrument.map, true);
            if (missing.empty()) {
                error = missing.unknown.isEmpty()
                            ? ui::Ls("Ses échantillons sont déjà là", "Its samples are already here")
                            : ui::Ls("Ses échantillons ne sont dans aucune banque connue", "Its samples are in no known bank");
                return false;
            }
            banks_.download(instrument.name, missing);
            return true;
        }
    error = ui::Ls("Instrument d'usine introuvable : ", "No such factory instrument: ") + key;
    return false;
}

SoundBanks::Missing Studio::missingSamples(const TrackRef& track)
{
    SoundBanks::Missing all;
    std::set<juce::String> seen;
    if (auto* slot = graphFor(track))
        for (const auto& node : slot->graph().editorNodes()) {
            if (node.typeKey != "sampler" && node.typeKey != "batterie")
                continue;
            const auto missing = banks_.missingFor(slot->graph().nodeSamplerJson(node.id), true);
            for (const auto& wanted : missing.files)
                if (seen.insert(wanted.bank + "/" + wanted.file.path).second) {
                    all.files.push_back(wanted);
                    all.bytes += wanted.file.size;
                }
            for (const auto& bank : missing.banks)
                all.banks.addIfNotAlreadyThere(bank);
            for (const auto& unknown : missing.unknown)
                all.unknown.addIfNotAlreadyThere(unknown);
        }
    return all;
}

bool Studio::generateMissingSounds(const std::optional<TrackRef> only)
{
    auto all = only ? std::vector<TrackRef> { *only } : allTracks();
    if (!only)
        all.push_back(masterTrack());
    // Only the zones of generated banks: the disk asked for them alone.
    juce::Array<juce::var> zones;
    std::set<juce::String> seen;
    for (const auto& track : all) {
        auto* slot = graphFor(track);
        if (slot == nullptr)
            continue;
        for (const auto& node : slot->graph().editorNodes()) {
            if (node.typeKey != "sampler" && node.typeKey != "batterie")
                continue;
            const auto json = slot->graph().nodeSamplerJson(node.id);
            if (const auto* list = json.getProperty("zones", {}).getArray())
                for (const auto& zone : *list) {
                    const auto file = zone.getProperty("file", {}).toString();
                    const auto* bank = banks_.find(app::bankOfSamplePath(file));
                    if (bank != nullptr && bank->generator.isNotEmpty() && seen.insert(file).second) {
                        auto* item = new juce::DynamicObject();
                        item->setProperty("file", file);
                        zones.add(juce::var(item));
                    }
                }
        }
    }
    if (zones.isEmpty())
        return false;
    auto* json = new juce::DynamicObject();
    json->setProperty("zones", zones);
    const auto missing = banks_.missingFor(juce::var(json), true);
    if (missing.empty())
        return false;
    const auto* first = banks_.find(missing.banks[0]);
    banks_.download(first != nullptr ? first->title : missing.banks[0], missing);
    return true;
}

void Studio::reloadSamplers(const juce::StringArray& banks)
{
    auto all = allTracks();
    all.push_back(masterTrack());
    for (const auto& track : all) {
        auto* slot = graphFor(track);
        if (slot == nullptr)
            continue;
        auto& graph = slot->graph();
        std::vector<std::uint64_t> cards;
        for (const auto& node : graph.editorNodes())
            if (node.typeKey == "sampler" || node.typeKey == "batterie")
                cards.push_back(node.id);
        auto changed = false;
        for (const auto id : cards) {
            const auto json = graph.nodeSamplerJson(id);
            auto plays = false;
            if (const auto* zones = json.getProperty("zones", {}).getArray())
                for (const auto& zone : *zones)
                    plays = plays || banks.contains(app::bankOfSamplePath(zone.getProperty("file", {}).toString()));
            if (!plays)
                continue;
            // The same map again: its samples asked of the store anew, now
            // that they are here. Not an edit of the project.
            const juce::ScopedValueSetter<bool> quiet(loading_, true);
            juce::String error;
            changed = graph.setNodeSampler(id, json, error) || changed;
        }
        if (changed)
            (void) publishGraph(track, true);
    }
}

// ---- The sampler ------------------------------------------------------------------

std::vector<Studio::FactoryInstrument> Studio::factoryInstruments() const
{
    // Kept until the disk or the language changes; a list still waiting for
    // maps or bank counts made again now and then.
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (factoryCacheGeneration_ == banks_.diskGeneration() && factoryCacheLanguage_ == ui::language()
        && (!factoryCachePending_ || now - factoryCacheTime_ < 1'500.0))
        return factoryCache_;
    std::vector<FactoryInstrument> instruments;
    auto pending = false;
    const auto folder = app::factoryContentFolder();
    const auto catalogue = juce::JSON::parse(folder.getChildFile("catalogue.json").loadFileAsString());
    const auto language = ui::french() ? "fr" : "en";
    const auto categories = catalogue.getProperty("categories", {});
    if (const auto* list = catalogue.getProperty("instruments", {}).getArray())
        for (const auto& item : *list) {
            FactoryInstrument instrument;
            instrument.key = item.getProperty("key", {}).toString();
            const auto names = item.getProperty("names", {});
            instrument.name = names.getProperty(language, names.getProperty("en", instrument.key)).toString();
            instrument.category = item.getProperty("category", {}).toString();
            const auto category = categories.getProperty(instrument.category, {});
            instrument.categoryName = category.getProperty(language, category.getProperty("en", instrument.category)).toString();
            instrument.englishName = names.getProperty("en", instrument.key).toString();
            instrument.englishCategory = category.getProperty("en", instrument.category).toString();
            instrument.map = folder.getChildFile(item.getProperty("map", {}).toString());
            instrument.settings = item.getProperty("settings", {});
            instrument.envelope = item.getProperty("envelope", {});
            instrument.voicing = item.getProperty("voicing", {});
            instrument.filterHz = static_cast<double>(item.getProperty("filter", 0.0));
            instrument.source = item.getProperty("source", {}).toString();
            instrument.bank = item.getProperty("bank", {}).toString();
            instrument.licence = item.getProperty("licence", {}).toString();
            if (const auto* range = item.getProperty("range", {}).getArray(); range != nullptr && range->size() == 2)
                instrument.range = (*range)[0].toString() + juce::String::fromUTF8(" \xe2\x80\x93 ") + (*range)[1].toString();
            if (const auto model = item.getProperty("model", {}).toString(); model.isNotEmpty())
                if (const auto file = folder.getChildFile(model); file.existsAsFile()) {
                    instrument.model = file;
                    instrument.modelSettings = item.getProperty("modelSettings", {});
                }
            if (instrument.key.isEmpty() || !instrument.map.existsAsFile())
                continue;
            const auto missing = banks_.missingForFile(instrument.map);
            pending = pending || missing.pending;
            instrument.missingFiles = static_cast<int>(missing.files.size());
            instrument.missingBytes = missing.bytes;
            instrument.banks = missing.banks;
            instruments.push_back(std::move(instrument));
        }
    factoryCache_ = instruments;
    factoryCacheGeneration_ = banks_.diskGeneration();
    factoryCacheLanguage_ = ui::language();
    factoryCachePending_ = pending;
    factoryCacheTime_ = now;
    return instruments;
}

const std::vector<app::NodeGraphComponent::DeviceCatalogueEntry>& Studio::deviceCatalogue()
{
    if (deviceCatalogueLanguage_ != ui::languageRevision() || deviceCatalogue_.empty()) {
        deviceCatalogueLanguage_ = ui::languageRevision();
        // A track's graph lists what a fresh one does; one is made only when
        // there is no track.
        if (!midiTracks_.empty()) {
            if (const auto* slot = graphFor(TrackRef { true, 0, midiTracks_.begin()->first }))
                deviceCatalogue_ = slot->graph().deviceCatalogue();
        }
        if (deviceCatalogue_.empty()) {
            app::NodeGraphComponent graph;
            graph.initialiseEmptyMidiTrackGraph();
            deviceCatalogue_ = graph.deviceCatalogue();
        }
    }
    return deviceCatalogue_;
}

std::uint64_t Studio::samplerCardOf(const TrackRef& track) const
{
    if (const auto* slot = graphFor(track))
        for (const auto& node : slot->graph().editorNodes())
            if (node.typeKey == "sampler" || node.typeKey == "batterie")
                return node.id;
    return 0;
}

bool Studio::setBatterieKit(const TrackRef& track, const std::uint64_t nodeId, int kit, juce::String& error)
{
    kit = std::clamp(kit, 0, static_cast<int>(engine::drum::kits.size()) - 1);
    const auto key = juce::String(engine::drum::kits[static_cast<std::size_t>(kit)].instrument);
    const auto instruments = factoryInstruments();
    const auto found = std::ranges::find_if(instruments, [&key](const FactoryInstrument& item) { return item.key == key; });
    if (found == instruments.end()) {
        error = ui::Ls("Kit introuvable : ", "No such kit: ") + key;
        return false;
    }
    auto json = app::loadSamplerMapFile(found->map, error);
    if (!json.isObject())
        return false;
    if (auto* object = json.getDynamicObject()) {
        object->setProperty("name", found->name);
        // Which of Batterie's kits it is (a preset of another kit loads it again).
        object->setProperty("batterieKit", kit);
    }
    if (!setSamplerMap(track, nodeId, json, true, error))
        return false;
    if (auto* slot = graphFor(track)) {
        const auto prefix = "node." + juce::String(static_cast<juce::int64>(nodeId)) + ".";
        (void) slot->graph().editorSetParameter(prefix + "batterie.kit.kit", static_cast<double>(kit));
        // Another kit has other microphones: none of the last kit's stays on
        // the card, each starts where the new kit's map puts it.
        if (const auto controls = slot->graph().editorSamplerControls(nodeId))
            for (std::size_t number = 0; number < controls->controllers.size(); ++number)
                if (controls->controllers[number] >= 0.0f)
                    (void) slot->graph().editorSetParameter(prefix + "sampler.cc" + juce::String(static_cast<int>(number)), -1.0);
    }
    (void) generateMissingSounds(track);
    return publishGraph(track, true).wasOk();
}

std::uint64_t Studio::addGrooveClip(const TrackRef& track, const int groove, const int bars, const int fill, double startBeats,
                                    juce::String& error)
{
    const auto& list = engine::drum::grooves();
    if (!track.midi || groove < 0 || groove >= static_cast<int>(list.size())) {
        error = ui::Ls("Pas de motif ou pas de piste MIDI", "No groove or no MIDI track");
        return 0;
    }
    const auto count = std::clamp(bars, 1, 64);
    if (startBeats < 0.0) {
        // The playhead's bar.
        const auto beats = engine_.position() * engine_.tempo() / 60.0;
        startBeats = std::floor(beats / 4.0) * 4.0;
    }
    const auto& played = list[static_cast<std::size_t>(groove)];
    const auto name = juce::String::fromUTF8((ui::french() ? played.french : played.english).c_str());
    const auto clip = engine_.addMidiClip(track.midiId, name, startBeats, 4.0 * count, false, 0xffe8c06au);
    if (clip == 0) {
        error = ui::Ls("Le clip n'a pas pu être créé", "The clip could not be made");
        return 0;
    }
    // On a Batterie: each hit on a key its kit plays.
    std::optional<std::array<bool, 128>> keys;
    if (const auto* slot = graphFor(track))
        for (const auto& node : slot->graph().editorNodes())
            if (node.typeKey == "batterie") {
                std::array<bool, 128> has {};
                if (const auto* zones = slot->graph().nodeSamplerJson(node.id).getProperty("zones", {}).getArray())
                    for (const auto& zone : *zones) {
                        const auto root = static_cast<int>(zone.getProperty("root", 60));
                        const auto* range = zone.getProperty("keys", {}).getArray();
                        const auto low = std::clamp(range != nullptr && range->size() == 2 ? static_cast<int>((*range)[0]) : root, 0, 127);
                        const auto high = std::clamp(range != nullptr && range->size() == 2 ? static_cast<int>((*range)[1]) : root, low, 127);
                        for (int key = low; key <= high; ++key)
                            has[static_cast<std::size_t>(key)] = true;
                    }
                keys = has;
                break;
            }
    for (const auto& hit : engine::drum::grooveHits(groove, count, fill)) {
        const auto note = keys ? engine::drum::keyInKit(hit.note, *keys) : hit.note;
        if (note >= 0)
            (void) engine_.addMidiNote(track.midiId, clip, note, hit.beat, 0.25, hit.velocity);
    }
    markDirty();
    return clip;
}

std::optional<TrackRef> Studio::addBatterieTrack(const int kit, juce::String& error)
{
    const auto track = addMidiTrack("Batterie", "batterie-instrument");
    if (!track) {
        error = ui::Ls("La piste batterie n'a pas pu être créée", "The drum track could not be made");
        return std::nullopt;
    }
    // The first kit came with it.
    if (kit != 0 && !setBatterieKit(*track, samplerCardOf(*track), kit, error))
        return std::nullopt;
    return track;
}

bool Studio::setSamplerMap(const TrackRef& track, const std::uint64_t nodeId, const juce::var& json,
                           const bool applySettings, juce::String& error)
{
    auto* slot = graphFor(track);
    if (slot == nullptr) {
        error = ui::Ls("Piste introuvable", "No such track");
        return false;
    }
    auto& graph = slot->graph();
    if (!graph.setNodeSampler(nodeId, json, error))
        return false;
    if (applySettings)
        if (const auto* settings = json.getProperty("settings", {}).getDynamicObject())
            for (const auto& [key, value] : settings->getProperties()) {
                const auto index = engine::samplerSettingIndex(key.toString().toStdString());
                if (index < 0)
                    continue;
                const auto suffix = juce::String(std::string(engine::samplerSettings[static_cast<std::size_t>(index)].suffix));
                for (const auto& node : graph.editorNodes())
                    if (node.id == nodeId)
                        for (const auto& parameter : node.parameters)
                            if (parameter.binding.endsWith("." + suffix))
                                (void) graph.editorSetParameter(parameter.binding, static_cast<double>(value));
            }
    (void) publishGraph(track, true);
    dirty_ = true;
    return true;
}

std::optional<TrackRef> Studio::loadSamplerFile(const juce::File& file, std::optional<TrackRef> track, juce::String& error)
{
    // A sound onto a simple sampler: its one sample.
    if (track && file.hasFileExtension("wav;aif;aiff;flac;ogg;mp3;wv")) {
        const auto card = samplerCardOf(*track);
        const auto* slot = graphFor(*track);
        const auto controls = slot != nullptr && card != 0 ? slot->graph().editorSamplerControls(card) : std::nullopt;
        if (!controls || !controls->simple) {
            error = ui::Ls("Cette piste n'a pas d'échantillonneur simple", "That track has no simple sampler");
            return std::nullopt;
        }
        if (!setSamplerMap(*track, card, app::simpleSamplerMap(file), false, error))
            return std::nullopt;
        return track;
    }
    const auto json = app::loadSamplerMapFile(file, error);
    if (!json.isObject())
        return std::nullopt;
    if (!track || samplerCardOf(*track) == 0) {
        const auto name = json.getProperty("name", {}).toString();
        track = addMidiTrack(name.isNotEmpty() ? name : file.getFileNameWithoutExtension(), "sampler-instrument");
        if (!track) {
            error = ui::Ls("La piste instrument n'a pas pu être créée", "The instrument track could not be made");
            return std::nullopt;
        }
    }
    if (!setSamplerMap(*track, samplerCardOf(*track), json, true, error))
        return std::nullopt;
    return track;
}

std::optional<TrackRef> Studio::newSamplerTrack(const juce::Array<juce::File>& files, juce::String& error)
{
    if (files.isEmpty()) {
        error = ui::Ls("Aucun fichier", "No file");
        return std::nullopt;
    }
    const auto name = files.size() == 1 ? files[0].getFileNameWithoutExtension()
                                        : files[0].getParentDirectory().getFileName();
    const auto json = app::samplerMapFromFiles(files, name);
    const auto track = addMidiTrack(name, "sampler-instrument");
    if (!track) {
        error = ui::Ls("La piste instrument n'a pas pu être créée", "The instrument track could not be made");
        return std::nullopt;
    }
    if (!setSamplerMap(*track, samplerCardOf(*track), json, true, error))
        return std::nullopt;
    return track;
}

std::optional<TrackRef> Studio::addFactoryInstrument(const juce::String& key, juce::String& error, const bool model,
                                                     std::optional<TrackRef> onto)
{
    if (onto && (!onto->midi || !isValid(*onto)))
        onto.reset();
    const auto instruments = factoryInstruments();
    const auto found = std::ranges::find_if(instruments, [&key](const FactoryInstrument& item) { return item.key == key; });
    if (found == instruments.end()) {
        error = ui::Ls("Instrument d'usine introuvable : ", "No such factory instrument: ") + key;
        return std::nullopt;
    }
    if (model) {
        // The sound model: a sound-model synth, its card's settings as made.
        if (found->model == juce::File()) {
            error = ui::Ls("Pas de modèle de son pour ", "No sound model for ") + found->name;
            return std::nullopt;
        }
        if (onto) {
            if (const auto replaced = replaceInstrument(*onto, "sound-model-synth", found->name + ui::Ls(" (modèle)", " (model)"));
                replaced.failed()) {
                error = replaced.getErrorMessage();
                return std::nullopt;
            }
        }
        const auto track = loadSoundModel(found->model, onto, error);
        if (!track)
            return std::nullopt;
        if (!onto)
            setTrackName(*track, found->name + ui::Ls(" (modèle)", " (model)"));
        if (auto* slot = graphFor(*track))
            if (const auto* settings = found->modelSettings.getDynamicObject()) {
                auto& graph = slot->graph();
                for (const auto& [name, value] : settings->getProperties()) {
                    const auto index = engine::soundModelSettingIndex(name.toString().toStdString());
                    if (index < 0)
                        continue;
                    const auto suffix = juce::String(std::string(engine::soundModelSettings[static_cast<std::size_t>(index)].suffix));
                    for (const auto& node : graph.editorNodes())
                        for (const auto& parameter : node.parameters)
                            if (parameter.binding.endsWith("." + suffix))
                                (void) graph.editorSetParameter(parameter.binding, static_cast<double>(value));
                }
                (void) publishGraph(*track, true);
            }
        return track;
    }
    std::optional<TrackRef> track = onto;
    if (!track)
        track = addMidiTrack(found->name, "sampler-instrument");
    if (!track) {
        error = ui::Ls("La piste instrument n'a pas pu être créée", "The instrument track could not be made");
        return std::nullopt;
    }
    if (!applyFactoryInstrument(*track, *found, error))
        return std::nullopt;
    return track;
}

bool Studio::applyFactoryInstrument(const TrackRef& target, const FactoryInstrument& instrument, juce::String& error)
{
    const auto* found = &instrument;
    auto json = app::loadSamplerMapFile(found->map, error);
    if (!json.isObject())
        return false;
    if (auto* object = json.getDynamicObject())
        object->setProperty("name", found->name);
    if (const auto replaced = replaceInstrument(target, "sampler-instrument", found->name); replaced.failed()) {
        error = replaced.getErrorMessage();
        return false;
    }
    const auto* track = &target;
    // The envelope it was made for: its attack, and the release a key lets go with.
    if (auto* slot = graphFor(*track)) {
        auto& graph = slot->graph();
        for (const auto& node : graph.editorNodes())
            for (const auto& parameter : node.parameters) {
                if (parameter.binding.endsWith(".envelope.attack") && found->envelope.hasProperty("attack"))
                    (void) graph.editorSetParameter(parameter.binding, static_cast<double>(found->envelope.getProperty("attack", 1.0)));
                if (parameter.binding.endsWith(".envelope.release") && found->envelope.hasProperty("release"))
                    (void) graph.editorSetParameter(parameter.binding, static_cast<double>(found->envelope.getProperty("release", 350.0)));
                // A darker one (the felt piano): its voice filter.
                if (parameter.binding.endsWith(".voiceFilter.cutoff") && found->filterHz > 0.0)
                    (void) graph.editorSetParameter(parameter.binding, found->filterHz);
                // One note at a time (a saxophone, a flute): its playing and glide.
                if (parameter.binding.endsWith(".voice.mode") && found->voicing.hasProperty("mode")) {
                    const auto mode = found->voicing.getProperty("mode", {}).toString();
                    (void) graph.editorSetParameter(parameter.binding, mode == "legato" ? 2.0 : mode == "mono" ? 1.0 : 0.0);
                }
                if (parameter.binding.endsWith(".voice.glide") && found->voicing.hasProperty("glide"))
                    (void) graph.editorSetParameter(parameter.binding, static_cast<double>(found->voicing.getProperty("glide", 0.0)));
            }
    }
    if (!setSamplerMap(*track, samplerCardOf(*track), json, true, error))
        return false;
    // A kit of sounds CyDAW makes: made now if they are not here.
    (void) generateMissingSounds(*track);
    return true;
}

std::vector<Studio::InstrumentChoice> Studio::instrumentChoices()
{
    std::vector<InstrumentChoice> choices;
    for (const auto& entry : deviceCatalogue())
        if (entry.soundSource && entry.kind == "preset"
            && engine::instrument_container::kindOfInstrument(entry.key.toStdString()) == engine::instrument_container::Kind::none)
            choices.push_back({ entry.key, entry.englishName.isNotEmpty() ? entry.englishName : entry.name, {}, false });
    std::sort(choices.begin(), choices.end(), [](const auto& a, const auto& b) { return a.name.compareNatural(b.name) < 0; });
    for (const auto& instrument : factoryInstruments())
        choices.push_back({ "factory:" + instrument.key, instrument.englishName, instrument.englishCategory, instrument.missingFiles > 0 });
    return choices;
}

std::optional<juce::String> Studio::applyInstrument(const TrackRef& target, const juce::String& key, juce::String& error)
{
    if (key.startsWith("factory:")) {
        const auto wanted = key.fromFirstOccurrenceOf("factory:", false, false);
        const auto instruments = factoryInstruments();
        const auto found = std::ranges::find_if(instruments, [&wanted](const FactoryInstrument& item) { return item.key == wanted; });
        if (found == instruments.end()) {
            error = "no factory instrument \"" + wanted + "\"";
            return std::nullopt;
        }
        // Its samples fetched when they are not here: it plays once they are.
        if (found->missingFiles > 0) {
            juce::String ignored;
            (void) downloadFactoryInstrument(found->key, ignored);
        }
        if (!applyFactoryInstrument(target, *found, error))
            return std::nullopt;
        return found->englishName;
    }
    const auto& catalogue = deviceCatalogue();
    const auto device = std::find_if(catalogue.begin(), catalogue.end(), [&key](const auto& entry) { return entry.key == key; });
    if (device == catalogue.end() || !device->soundSource) {
        error = "no instrument \"" + key + "\"";
        return std::nullopt;
    }
    // A pad holds no container of its own.
    if (target.pad >= 0 && engine::instrument_container::kindOfInstrument(key.toStdString()) != engine::instrument_container::Kind::none) {
        error = "a pad or a layer holds no instrument container";
        return std::nullopt;
    }
    const auto name = device->englishName.isNotEmpty() ? device->englishName : device->name;
    if (const auto replaced = replaceInstrument(target, key, name); replaced.failed()) {
        error = replaced.getErrorMessage();
        return std::nullopt;
    }
    return name;
}

juce::File Studio::deviceLibraryFolder()
{
    return locations::folder(Location::devices);
}

std::vector<Studio::LibraryDevice> Studio::libraryDevices() const
{
    std::vector<LibraryDevice> devices;
    const auto folder = deviceLibraryFolder();
    if (!folder.isDirectory())
        return devices;
    for (const auto& file : folder.findChildFiles(juce::File::findFiles, false, "*.cydawdevice")) {
        if (file.getSize() > 16 * 1024 * 1024)
            continue;
        const auto document = juce::JSON::parse(file);
        const auto* root = document.getDynamicObject();
        if (root == nullptr || root->getProperty("format").toString() != "CyDAW Device")
            continue;
        LibraryDevice device;
        device.file = file;
        device.name = root->getProperty("name").toString();
        if (device.name.isEmpty())
            device.name = file.getFileNameWithoutExtension();
        device.instrument = root->getProperty("role").toString() == "instrument";
        device.description = root->getProperty("description").toString();
        devices.push_back(std::move(device));
    }
    std::sort(devices.begin(), devices.end(),
              [](const auto& a, const auto& b) { return a.name.compareNatural(b.name) < 0; });
    return devices;
}

juce::Result Studio::insertLibraryDevice(const TrackRef& track, const juce::File& file)
{
    auto* slot = graphFor(track);
    if (slot == nullptr)
        return juce::Result::fail(ui::Ls("Piste introuvable", "No such track"));
    const auto document = juce::JSON::parse(file);
    const auto* root = document.getDynamicObject();
    if (root == nullptr || root->getProperty("format").toString() != "CyDAW Device")
        return juce::Result::fail(ui::Ls("Pas un périphérique CyDAW : ", "Not a CyDAW device: ") + file.getFullPathName());
    const auto instrument = root->getProperty("role").toString() == "instrument";
    if (instrument && !track.midi)
        return juce::Result::fail(ui::Ls("C'est un instrument : ", "It is an instrument: ") + root->getProperty("name").toString());
    if (workshop_ && workshop_->track && *workshop_->track == track && workshop_->own == nullptr && instrument)
        unlinkWorkshop();
    if (instrument) {
        // An instrument is the track's whole graph, as a factory instrument is.
        slot->restore(withLibrary(root->getProperty("graph"), factoryMidiGraph_));
        dirty_ = true;
        return publishGraph(track, true);
    }
    // The factory modules it uses, from the library, beside its own.
    const auto state = withLibrary(root->getProperty("graph"), factoryAudioGraph_);
    auto& graph = slot->graph();
    const auto card = graph.editorInsertModuleFromState(state, cydaw::graph::GraphRole::audioEffect,
                                                        root->getProperty("name").toString());
    if (card == 0)
        return juce::Result::fail(ui::Ls("Le périphérique n'a pas pu entrer dans la chaîne", "The device could not be put in the chain"));
    graph_layout::arrange(graph);
    dirty_ = true;
    return publishGraph(track, true);
}

juce::Result Studio::removeLibraryDevice(const juce::File& file)
{
    if (file.getParentDirectory() != deviceLibraryFolder() || !file.hasFileExtension("cydawdevice"))
        return juce::Result::fail(ui::Ls("Pas un périphérique de Mes périphériques : ", "Not a device of My devices: ") + file.getFullPathName());
    if (!file.moveToTrash())
        return juce::Result::fail(ui::Ls("Impossible de mettre à la corbeille : ", "Could not move to the recycle bin: ") + file.getFullPathName());
    if (workshop_ && workshop_->file == file) {
        workshop_->file = juce::File();
        workshop_->unsaved = true;
    }
    return juce::Result::ok();
}

Studio::Mix Studio::trackMix(const TrackRef& track) const
{
    if (track.isBus()) {
        const auto bus = engine_.virtualBus(track.bus);
        return bus ? Mix { bus->gainDb, 0.0f, bus->muted, false, false } : Mix {};
    }
    if (track.master)
        return { engine_.masterGainDb(), 0.0f, engine_.isMasterMuted(), false, false };
    if (track.midi) {
        for (const auto& summary : engine_.midiTrackSummaries())
            if (summary.id == track.midiId)
                return { summary.gainDb, summary.pan, summary.muted, summary.solo, summary.armed };
        return {};
    }
    const auto state = engine_.trackMixState(track.audioIndex);
    return { state.gainDb, state.pan, state.muted, state.solo, state.armed };
}

void Studio::setTrackMix(const TrackRef& track, const Mix& mix)
{
    if (track.isBus()) {
        if (auto bus = engine_.virtualBus(track.bus)) {
            bus->gainDb = mix.gainDb;
            bus->muted = mix.muted;
            (void) updateHardwareBus(*bus);
        }
        return;
    }
    if (track.master) {
        engine_.setMasterGain(mix.gainDb);
        engine_.setMasterMuted(mix.muted);
        dirty_ = true;
        return;
    }
    if (track.midi) {
        engine_.setMidiTrackMixer(track.midiId, mix.gainDb, mix.pan, mix.muted, mix.solo);
        (void) engine_.setMidiTrackArmed(track.midiId, mix.armed);
    } else {
        engine_.setTrackMixer(track.audioIndex, mix.gainDb, mix.pan, mix.muted);
        engine_.setTrackSolo(track.audioIndex, mix.solo);
        engine_.setTrackArmed(track.audioIndex, mix.armed);
    }
    // A hybrid track's companion is soloed with it (it is upstream: a solo
    // would silence it).
    if (const auto companion = companionOf(track))
        if (auto other = trackMix(*companion); other.solo != mix.solo) {
            other.solo = mix.solo;
            setTrackMix(*companion, other);
        }
    dirty_ = true;
}

std::optional<TrackRef> Studio::addAudioTrack(const juce::File& file, const double startSeconds)
{
    std::size_t index {};
    const auto result = file.existsAsFile() ? engine_.addAudioTrack(file, &index, startSeconds)
                                            : engine_.addEmptyAudioTrack(&index);
    if (result.failed())
        return std::nullopt;
    auto& state = adoptAudioTrack(index, {});
    if (file.existsAsFile())
        state.name = file.getFileNameWithoutExtension();
    const TrackRef track { false, index, 0 };
    (void) publishGraph(track, true);
    dirty_ = true;
    return track;
}

std::optional<TrackRef> Studio::addMidiTrack(const juce::String& name, const juce::String& instrumentKey)
{
    auto trackName = name.trim();
    if (trackName.isEmpty())
        trackName = "Inst " + juce::String(static_cast<int>(midiTracks_.size() + 1));
    const auto id = engine_.addMidiTrack(trackName);
    if (id == 0)
        return std::nullopt;
    // The engine gives every new track a first clip; a v2 track starts empty.
    if (const auto* clips = engine_.midiClipsView(id)) {
        const auto copy = *clips;
        for (const auto& clip : copy)
            (void) engine_.removeMidiClip(id, clip.id);
    }
    adoptMidiTrack(id, {});
    const TrackRef track { true, 0, id };
    if (instrumentKey.isNotEmpty())
        (void) insertDevice(track, instrumentKey);
    else
        (void) publishGraph(track, true);
    (void) engine_.setLiveMidiTrack(id);
    dirty_ = true;
    return track;
}

bool Studio::removeTrack(const TrackRef& track)
{
    if (track.master)
        return false;
    // A hybrid track's companion goes with it (first: it is of the other
    // kind, the track keeps its place).
    if (const auto companion = companionOf(track)) {
        companions_.erase(routingKey(track));
        hybridAudioRecords_.erase(routingKey(track));
        (void) removeTrack(*companion);
    }
    std::erase_if(companions_, [key = routingKey(track)](const auto& entry) { return entry.second == key; });
    ++trackRefsSerial_;
    // The tracks after it move: the replacers' instruments are made again
    // from their cards, by the tracks they are on now.
    replacerSlots_.clear();
    // The indices of the tracks after it move.
    clipsAtStop_.clear();
    const auto removedKey = routingKey(track);
    struct Republish {
        Studio& studio;
        std::uint64_t key;
        ~Republish()
        {
            studio.forgetDestination(key);
            studio.publishRouting();
        }
    } republish { *this, removedKey };
    // The workshop keeps the device of a track that goes; a later audio
    // track it is linked to moves down one place.
    if (workshop_ && workshop_->track) {
        if (*workshop_->track == track)
            unlinkWorkshop();
        else if (!track.midi && !workshop_->track->midi && workshop_->track->audioIndex > track.audioIndex)
            --workshop_->track->audioIndex;
    }
    if (track.midi) {
        if (!engine_.removeMidiTrack(track.midiId))
            return false;
        midiTracks_.erase(track.midiId);
        midiMeters_.erase(track.midiId);
    } else {
        if (!engine_.removeAudioTrack(track.audioIndex))
            return false;
        if (track.audioIndex < audioTracks_.size())
            audioTracks_.erase(audioTracks_.begin() + static_cast<std::ptrdiff_t>(track.audioIndex));
        if (track.audioIndex < audioMeters_.size())
            audioMeters_.erase(audioMeters_.begin() + static_cast<std::ptrdiff_t>(track.audioIndex));
    }
    dirty_ = true;
    return true;
}

bool Studio::moveTrack(const TrackRef& track, const int delta)
{
    // In the order shown, whatever their kinds (the engine keeps its own).
    if (track.master || delta == 0)
        return false;
    (void) tracks();
    const auto key = routingKey(track);
    const auto found = std::find(trackOrder_.begin(), trackOrder_.end(), key);
    if (key == 0 || found == trackOrder_.end())
        return false;
    const auto from = static_cast<int>(std::distance(trackOrder_.begin(), found));
    const auto to = juce::jlimit(0, static_cast<int>(trackOrder_.size()) - 1, from + delta);
    if (to == from)
        return false;
    trackOrder_.erase(trackOrder_.begin() + from);
    trackOrder_.insert(trackOrder_.begin() + to, key);
    dirty_ = true;
    return true;
}

// ---- Project ------------------------------------------------------------------------------

void Studio::clearSession()
{
    ++trackRefsSerial_;
    // The next project opens heard (its own mute, if any, comes with it).
    engine_.setMasterMuted(false);
    trackOrder_.clear();
    groupOf_.clear();
    groups_.clear();
    foldedGroups_.clear();
    companions_.clear();
    hybridAudioRecords_.clear();
    // A device linked to a track of the project closed stays in the
    // workshop, as its own copy: every tab's.
    for (auto& tab : workshops_)
        unlinkTab(*tab);
    engine_.stop();
    engine_.stopLauncher();
    clipsAtStop_.clear();
    masterClips_.clear();
    masterPlaying_ = -1;
    masterAtStop_ = -1;
    masterGainBeforeClip_.reset();
    engine_.setMasterNativeAutomation({});
    engine_.stopSamplePreview();
    engine_.clearTracks();
    engine_.clearMidiTracks();
    audioTracks_.clear();
    midiTracks_.clear();
    replacerSlots_.clear();
    // The virtual inputs and outputs stay: they are the software's.
    audioMeters_.clear();
    midiMeters_.clear();
    interfaceState = juce::var();
}

void Studio::newProject()
{
    clearSession();
    resetMasterGraph();
    setControllerAssignments({}, true);
    if (const auto* factory = factoryAudioGraph_.getDynamicObject())
        sharedNodeModules_ = app::cloneJsonValue(factory->getProperty("modules"));
    engine_.setTempo(120.0);
    engine_.setTimeSignature(4, 4);
    engine_.resetLoopRange();
    engine_.setLooping(false);
    loopEnabled_ = false;
    engine_.setPosition(0.0);
    engine_.setArrangementPlaybackEnabled(true);
    projectFile_ = juce::File {};
    loadWarnings_.clear();
    dirty_ = false;
}

juce::Result Studio::loadProject(const juce::File& file)
{
    juce::var document;
    if (const auto read = app::ProjectRecovery::readDocument(file, document); read.failed())
        return read;
    if (document.getProperty("format", juce::var()).toString() != "CyDAW Project")
        return juce::Result::fail("not a CyDAW project: " + file.getFullPathName());
    juce::StringArray missingModels;
    app::expandFactoryModelReferences(document, missingModels);
    beginApplying(file, document);
    noteMissingModels(missingModels);
    while (applying_->next()) {
    }
    return finishApplying(file, applying_->result());
}

void Studio::beginApplying(const juce::File& file, const juce::var& document)
{
    loading_ = true;
    clearSession();
    loadWarnings_.clear();
    applying_ = std::make_unique<project_io::DocumentLoad>(*this, document, file, loadWarnings_);
}

void Studio::noteMissingModels(const juce::StringArray& missing)
{
    for (const auto& stored : missing)
        loadWarnings_.add(ui::Ls("Modèle de son d'usine introuvable : ", "Factory sound model not found: ")
                          + stored.fromFirstOccurrenceOf(":", false, false));
}

juce::Result Studio::finishApplying(const juce::File& file, const juce::Result& applied)
{
    applying_.reset();
    loading_ = false;
    for (const auto& track : allTracks())
        publishParameterAutomation(track);
    // Projects shipped next to the application are opened as new work: a
    // plain save must not overwrite the template.
    const auto templatesFolder = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                                     .getSiblingFile("templates");
    projectFile_ = file.isAChildOf(templatesFolder) ? juce::File {} : file;
    dirty_ = projectFile_ == juce::File {};
    // Sounds CyDAW makes that its samplers play: made if they are not here.
    if (applied.wasOk())
        (void) generateMissingSounds();
    return applied;
}

bool Studio::startOpeningProject(const juce::File& file)
{
    if (opening_ != nullptr || !file.existsAsFile())
        return false;
    engine_.stop();
    engine_.stopLauncher();
    auto opening = std::make_unique<Opening>();
    opening->file = file;
    opening->bytes = file.getSize();
    opening->started = juce::Time::getMillisecondCounterHiRes() * 0.001;
    opening->read = std::make_shared<Opening::Read>();
    auto read = opening->read;
    juce::Thread::launch([read, file] {
        juce::var document;
        auto result = app::ProjectRecovery::readDocument(file, document);
        if (result.wasOk() && document.getProperty("format", juce::var()).toString() != "CyDAW Project")
            result = juce::Result::fail("not a CyDAW project: " + file.getFullPathName());
        if (result.wasOk())
            app::expandFactoryModelReferences(document, read->missingModels);
        read->document = std::move(document);
        read->result = result;
        read->done.store(true, std::memory_order_release);
    });
    opening_ = std::move(opening);
    return true;
}

bool Studio::startImportingProject(const juce::File& file)
{
    if (opening_ != nullptr || !file.exists())
        return false;
    engine_.stop();
    engine_.stopLauncher();
    importSummary_.clear();
    auto opening = std::make_unique<Opening>();
    opening->file = file;
    opening->bytes = file.isDirectory() ? 0 : file.getSize();
    opening->started = juce::Time::getMillisecondCounterHiRes() * 0.001;
    opening->imported = std::make_shared<Opening::ImportRead>();
    auto imported = opening->imported;
    juce::Thread::launch([imported, file] {
        imported->result = app::project_import::importProjectFile(file, imported->project);
        imported->done.store(true, std::memory_order_release);
    });
    opening_ = std::move(opening);
    return true;
}

std::optional<juce::Result> Studio::continueOpeningProject(const double budgetMs)
{
    if (opening_ == nullptr)
        return std::nullopt;
    auto& opening = *opening_;
    if (opening.imported != nullptr) {
        if (!opening.imported->done.load(std::memory_order_acquire))
            return std::nullopt;
        if (opening.imported->result.failed()) {
            const auto failed = opening.imported->result;
            opening_.reset();
            return failed;
        }
        if (opening.build == nullptr) {
            opening.build = std::make_unique<ImportBuild>();
            opening.build->project = std::move(opening.imported->project);
        }
        const auto until = juce::Time::getMillisecondCounterHiRes() + budgetMs;
        do {
            if (!importStep(*opening.build)) {
                importSummary_ = describeImportedProject(opening.build->project);
                opening_.reset();
                return juce::Result::ok();
            }
        } while (juce::Time::getMillisecondCounterHiRes() < until);
        return std::nullopt;
    }
    if (applying_ == nullptr) {
        if (!opening.read->done.load(std::memory_order_acquire))
            return std::nullopt;
        if (opening.read->result.failed()) {
            const auto failed = opening.read->result;
            opening_.reset();
            return failed;
        }
        // The session goes now, the project's tracks come in the next frames.
        beginApplying(opening.file, opening.read->document);
        noteMissingModels(opening.read->missingModels);
        opening.read->document = juce::var();
        return std::nullopt;
    }
    const auto until = juce::Time::getMillisecondCounterHiRes() + budgetMs;
    do {
        if (!applying_->next()) {
            const auto file = opening.file;
            const auto applied = finishApplying(file, applying_->result());
            opening_.reset();
            return applied;
        }
    } while (juce::Time::getMillisecondCounterHiRes() < until);
    return std::nullopt;
}

Studio::ProjectOpening Studio::projectOpening() const
{
    ProjectOpening state;
    if (opening_ == nullptr)
        return state;
    state.active = true;
    state.file = opening_->file;
    state.bytes = opening_->bytes;
    state.seconds = juce::Time::getMillisecondCounterHiRes() * 0.001 - opening_->started;
    if (opening_->imported != nullptr) {
        state.import = true;
        if (opening_->build == nullptr) {
            // Reading the other software's file: the first fifth of the bar.
            state.reading = true;
            const auto expected = std::max(0.5, static_cast<double>(opening_->bytes) / 20.0e6);
            state.progress = static_cast<float>(0.2 * (1.0 - std::exp(-state.seconds / expected)));
            state.doing = ui::Ls("Lecture du projet", "Reading the project");
        } else {
            const auto& build = *opening_->build;
            state.progress = 0.2f + 0.8f * build.progress();
            const auto count = build.phase <= 1 ? build.project.tracks.size() : build.tracks.size();
            state.doing = (build.phase <= 1 ? ui::Ls("Pistes : ", "Tracks: ") : ui::Ls("Instruments et effets : ", "Instruments and effects: "))
                          + juce::String(static_cast<int>(std::min(build.next, count))) + " / " + juce::String(static_cast<int>(count));
        }
        return state;
    }
    if (applying_ == nullptr) {
        // Reading and decoding: about 40 MB a second, the first fifth of the bar.
        state.reading = true;
        const auto expected = std::max(0.3, static_cast<double>(opening_->bytes) / 40.0e6);
        state.progress = static_cast<float>(0.2 * (1.0 - std::exp(-state.seconds / expected)));
        state.doing = ui::Ls("Lecture du fichier", "Reading the file");
    } else {
        state.progress = 0.2f + 0.8f * applying_->progress();
        state.doing = applying_->doing();
    }
    return state;
}

juce::Result Studio::saveProject(const juce::File& file)
{
    // A take being recorded goes on: the notes played so far are saved, and
    // the session is still to be saved once it ends.
    const auto recording = isRecording();
    if (engine_.isMidiRecording())
        (void) engine_.pollMidiRecording();
    const auto document = app::withFactoryModelReferences(project_io::buildDocument(*this, file, true));
    const auto written = app::ProjectRecovery::writeDocumentAtomically(file, document);
    if (written.failed())
        return written;
    projectFile_ = file;
    dirty_ = recording;
    discardRecovery();
    return juce::Result::ok();
}

juce::String Studio::projectName() const
{
    return projectFile_ == juce::File {} ? juce::String() : projectFile_.getFileNameWithoutExtension();
}

juce::Array<juce::File> Studio::templates() const
{
    juce::Array<juce::File> files;
    juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getSiblingFile("templates")
        .findChildFiles(files, juce::File::findFiles, false, "*.cydaw");
    files.sort();
    return files;
}

juce::Result Studio::importProject(const juce::File& file, juce::String& summary)
{
    // The same steps as the staged import, one after the other.
    ImportBuild build;
    if (const auto read = app::project_import::importProjectFile(file, build.project); read.failed())
        return read;
    while (importStep(build)) {
    }
    summary = describeImportedProject(build.project);
    importSummary_ = summary;
    return juce::Result::ok();
}

bool Studio::importStep(ImportBuild& build)
{
    auto& project = build.project;
    if (build.phase == 0) {
        // The session cleared, the project's tempo.
        loading_ = true;
        newProject();
        engine_.setTempo(project.tempo);
        engine_.beginArrangementBatch();
        build.phase = 1;
        build.next = 0;
        return true;
    }
    if (build.phase == 1) {
        if (build.next >= project.tracks.size()) {
            (void) engine_.endArrangementBatch();
            if (std::abs(project.masterGainDb) > 0.01f)
                engine_.setMasterGain(project.masterGainDb);
            loading_ = false;
            build.tracks.clear();
            for (const auto& [track, source] : build.made)
                build.tracks.push_back(track);
            build.phase = 2;
            build.next = 0;
            return true;
        }
        const auto sourceIndex = build.next++;
        const auto& source = project.tracks[sourceIndex];
        if (source.midi || !source.midiClips.empty()) {
            const auto id = engine_.addMidiTrack(source.name);
            if (id != 0) {
                build.made.push_back({ TrackRef { true, 0, id }, sourceIndex });
                if (const auto* clips = engine_.midiClipsView(id)) {
                    const auto copy = *clips;
                    for (const auto& clip : copy)
                        (void) engine_.removeMidiClip(id, clip.id);
                }
                engine_.setMidiTrackMixer(id, source.gainDb, source.pan, source.muted, source.solo);
                for (const auto& clip : source.midiClips) {
                    // A launcher clip in its slot (its scene), an arrangement
                    // clip where it sits.
                    if (clip.launcherScene >= app::AudioTrackController::maximumLauncherScenes)
                        continue;
                    const auto clipId = engine_.addMidiClip(id, clip.name, std::max(0.0, clip.startBeats),
                                                            std::max(0.25, clip.lengthBeats), clip.looping,
                                                            clip.colour != 0 ? clip.colour : 0xff4fd3aeu,
                                                            std::max(-1, clip.launcherScene));
                    for (const auto& note : clip.notes)
                        (void) engine_.addMidiNote(id, clipId, note.note, note.startBeats, note.lengthBeats, note.velocity);
                }
                auto& state = adoptMidiTrack(id, {});
                if (source.colour != 0)
                    state.colour = source.colour;
            }
        }
        if ((!source.midi || !source.audioClips.empty()) && !(source.midi && source.audioClips.empty())) {
            std::size_t index {};
            if (engine_.addEmptyAudioTrack(&index).wasOk()) {
                build.made.push_back({ TrackRef { false, index, 0 }, sourceIndex });
                auto& state = adoptAudioTrack(index, {});
                state.name = source.midi ? source.name + " (audio)" : source.name;
                if (source.colour != 0)
                    state.colour = source.colour;
                engine_.setTrackMixer(index, source.gainDb, source.pan, source.muted);
                engine_.setTrackSolo(index, source.solo);
                for (const auto& clip : source.audioClips) {
                    if (!clip.file.existsAsFile())
                        continue;
                    if (clip.launcherScene >= 0) {
                        if (clip.launcherScene < app::AudioTrackController::maximumLauncherScenes)
                            (void) engine_.setLauncherClip(index, clip.launcherScene, clip.name, clip.sourceOffsetSeconds,
                                                           std::max(0.01, clip.durationSeconds), clip.looping,
                                                           clip.colour != 0 ? clip.colour : 0xffff9b2fu, clip.file);
                        continue;
                    }
                    const auto clipId = engine_.addAudioClip(index, clip.startSeconds, clip.sourceOffsetSeconds,
                                                             std::max(0.01, clip.durationSeconds), clip.name,
                                                             clip.colour != 0 ? clip.colour : 0xffff9b2fu, clip.file,
                                                             clip.fadeInSeconds, clip.fadeOutSeconds);
                    if (clipId != 0 && clip.sourceDurationSeconds > 0.0)
                        (void) engine_.updateAudioClip(index, clipId, clip.startSeconds, clip.sourceOffsetSeconds,
                                                       std::max(0.01, clip.durationSeconds), -1.0, -1.0,
                                                       clip.sourceDurationSeconds);
                }
            }
        }
        return true;
    }
    // Each track's instrument and plug-ins, and its graph published. A
    // plug-in keeps its place as a card saying it is missing (CyDAW does not
    // run the other application's plug-ins in its chains); an instrument of
    // the other application's own becomes CyDAW's that stands for it (its
    // polysynth Opale, its FM synth Quartz…), its settings carried over
    // where they are known.
    if (build.next < build.made.size()) {
        const auto [track, sourceIndex] = build.made[build.next++];
        if (isValid(track)) {
            const auto& chain = project.tracks[sourceIndex].chain;
            const auto audioPart = !track.midi && project.tracks[sourceIndex].midi;
            if (track.midi) {
                const auto instrument = std::find_if(chain.begin(), chain.end(), [](const auto& device) { return device.instrument; });
                if (instrument != chain.end() && instrument->plugin) {
                    (void) addMissingPlugin(track, instrument->name, instrument->vendor, instrument->format, true);
                } else if (const auto equivalent = instrument != chain.end() ? imported::equivalentOf(*instrument, project.format)
                                                                             : std::optional<imported::Equivalent> {}) {
                    if (insertDevice(track, equivalent->instrument).failed())
                        (void) insertDevice(track, equivalent->fallback);
                    else if (!equivalent->settings.empty())
                        (void) setSynthSettings(track, equivalent->settings);
                    // A sampler's sound, when the other application's played one.
                    if (instrument->sample.existsAsFile()
                        && (equivalent->instrument == "simple-sampler-instrument" || equivalent->instrument == "sampler-instrument")) {
                        juce::String error;
                        if (!loadSamplerFile(instrument->sample, track, error))
                            project.warnings.add(trackName(track) + ": " + error);
                    }
                } else {
                    (void) insertDevice(track, "opale-instrument");
                }
            }
            if (!audioPart)
                for (const auto& device : chain)
                    if (device.plugin && !device.instrument && !device.noteEffect)
                        (void) addMissingPlugin(track, device.name, device.vendor, device.format, false);
            (void) publishGraph(track, true);
        }
        return true;
    }
    projectFile_ = juce::File {};
    dirty_ = true;
    return false;
}

// ---- Autosave and recovery ------------------------------------------------------------------------

juce::File Studio::recoveryFile()
{
    // Its own file: CyDAW 1 keeps last-session.cydaw-recovery.
    return app::ProjectRecovery::defaultRecoveryFile().getSiblingFile("last-session-v2.cydaw-recovery");
}

bool Studio::ownsRecovery() const
{
    if (recoveryLock_ == nullptr) {
        recoveryLock_ = std::make_unique<juce::InterProcessLock>("CyDAW2-recovery");
        ownsRecovery_ = recoveryLock_->enter(0);
    }
    return ownsRecovery_;
}

juce::String Studio::pendingRecoveryDescription() const
{
    // A session that keeps nothing (automated runs) leaves it for the next.
    if (auxiliary_ || !autosaveEnabled_ || !ownsRecovery())
        return {};
    const auto file = recoveryFile();
    if (!file.existsAsFile())
        return {};
    return file.getLastModificationTime().formatted("%d/%m/%Y %H:%M");
}

juce::Result Studio::restoreRecovery()
{
    const auto file = recoveryFile();
    juce::var document;
    if (const auto read = app::ProjectRecovery::readDocument(file, document); read.failed())
        return read;
    const auto original = app::ProjectRecovery::originalProject(document);
    const auto known = original != juce::File {} && (original.existsAsFile() || original.getParentDirectory().isDirectory());
    // Its sounds' paths were written from the project's folder, not the
    // recovery's: read from there, or the clips beside the project are lost.
    const auto base = known ? original : file;
    juce::StringArray missingModels;
    app::expandFactoryModelReferences(document, missingModels);
    beginApplying(base, document);
    noteMissingModels(missingModels);
    while (applying_->next()) {
    }
    const auto loaded = finishApplying(base, applying_->result());
    if (loaded.failed())
        return loaded;
    projectFile_ = known ? original : juce::File {};
    dirty_ = true;
    return juce::Result::ok();
}

void Studio::discardRecovery()
{
    // A session that writes none (automated runs) deletes none either.
    if (auxiliary_ || !autosaveEnabled_ || !ownsRecovery()) return;
    // A copy being written now would come back after this.
    while (autosaveWriting_.load())
        juce::Thread::sleep(5);
    (void) app::ProjectRecovery::discard(recoveryFile());
}

void Studio::discardChanges()
{
    dirty_ = false;
    discardRecovery();
}

void Studio::autosaveIfDue()
{
    if (auxiliary_) return;
    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (!autosaveEnabled_ || !dirty_ || isRecording() || autosaveWriting_.load() || now - lastAutosaveSeconds_ < 45.0
        || !ownsRecovery())
        return;
    lastAutosaveSeconds_ = now;
    // Built here, copied whole so the writer thread shares nothing with the
    // session, written in the background.
    auto document = app::cloneJsonValue(app::withFactoryModelReferences(project_io::buildDocument(*this, projectFile_, false)));
    document = app::ProjectRecovery::makeRecoveryDocument(document, projectFile_);
    autosaveWriting_.store(true);
    juce::Thread::launch([this, document] {
        (void) app::ProjectRecovery::writeDocumentAtomically(recoveryFile(), document, false);
        autosaveWriting_.store(false);
    });
}

void Studio::finishRecovery()
{
    while (autosaveWriting_.load())
        juce::Thread::sleep(5);
    if (!autosaveEnabled_ || !ownsRecovery())
        return;
    if (dirty_ && trackCount() > 0) {
        auto document = app::withFactoryModelReferences(project_io::buildDocument(*this, projectFile_, false));
        document = app::ProjectRecovery::makeRecoveryDocument(document, projectFile_);
        (void) app::ProjectRecovery::writeDocumentAtomically(recoveryFile(), document, false);
    } else {
        discardRecovery();
    }
}

// ---- Ticking and meters ----------------------------------------------------------------------

void Studio::applyBallistics(MeterState& meter, const float* peak, const float* rms, const int channels)
{
    // Up to stereo, each side its channel; wider, each side the loudest of
    // its speakers, a centre (or an LFE) on both.
    std::array<float, 2> sidePeak {};
    std::array<float, 2> sideRms {};
    if (channels <= 2) {
        for (auto channel = 0; channel < 2; ++channel) {
            const auto source = std::min(channel, std::max(0, channels - 1));
            sidePeak[static_cast<std::size_t>(channel)] = channels > 0 ? peak[source] : 0.0f;
            sideRms[static_cast<std::size_t>(channel)] = channels > 0 ? rms[source] : 0.0f;
        }
    } else {
        for (auto channel = 0; channel < std::min(channels, 8); ++channel) {
            const auto side = cydaw::engine::speakerSide(cydaw::engine::speakerRole(channels, channel));
            for (auto at = 0; at < 2; ++at)
                if (side == 0 || side == (at == 0 ? -1 : 1)) {
                    sidePeak[static_cast<std::size_t>(at)] = std::max(sidePeak[static_cast<std::size_t>(at)], peak[channel]);
                    sideRms[static_cast<std::size_t>(at)] = std::max(sideRms[static_cast<std::size_t>(at)], rms[channel]);
                }
        }
    }
    for (auto channel = 0; channel < 2; ++channel) {
        auto& peakValue = meter.peak[static_cast<std::size_t>(channel)];
        auto& rmsValue = meter.rms[static_cast<std::size_t>(channel)];
        peakValue = std::max(sidePeak[static_cast<std::size_t>(channel)], peakValue * 0.86f);
        rmsValue = rmsValue * 0.72f + sideRms[static_cast<std::size_t>(channel)] * 0.28f;
    }
    if (channels > 0)
        meter.channels = std::min(channels, 8);
    for (auto channel = 0; channel < 8; ++channel) {
        const auto value = channel < channels ? peak[channel] : 0.0f;
        auto& held = meter.channelPeak[static_cast<std::size_t>(channel)];
        held = std::max(value, held * 0.86f);
    }
}

MeterState Studio::trackMeter(const TrackRef& track) const
{
    if (track.isBus()) {
        MeterState meter;
        const auto levels = engine_.virtualBusMeterLevels(track.bus);
        meter.peak = levels;
        meter.rms = levels;
        meter.channelPeak[0] = levels[0];
        meter.channelPeak[1] = levels[1];
        return meter;
    }
    if (track.master)
        return masterMeter_;
    if (track.midi) {
        const auto found = midiMeters_.find(track.midiId);
        return found != midiMeters_.end() ? found->second : MeterState {};
    }
    return track.audioIndex < audioMeters_.size() ? audioMeters_[track.audioIndex] : MeterState {};
}

void Studio::tick()
{
    engine_.pollPluginWorkers();
    followMasterClip();
    followClipLanes();
    // A loop auditioned at the tempo keeps in step with the transport.
    engine_.followTempoPreview();
    if (opening_ != nullptr)
        return;
    // A location's content moved: its new folder from now on.
    if (locationMove_ != nullptr)
        if (const auto progress = locationMove_->progress(); progress.finished) {
            const auto location = locationMove_->location();
            const auto to = locationMove_->destination();
            locationMove_.reset();
            if (progress.error.isNotEmpty()) {
                notices_.emplace_back(ui::Ls("Déplacement impossible : ", "Could not move: ") + progress.error, true);
                pendingLocations_.clear();
            } else if (progress.cancelled) {
                notices_.emplace_back(ui::Ls("Déplacement annulé : rien n'a changé", "Move cancelled: nothing changed"), false);
                pendingLocations_.clear();
            } else {
                locations::set(location, to);
                applyLocations();
                notices_.emplace_back(ui::Ls("Déplacé dans ", "Moved to ") + to.getFullPathName(), false);
                if (progress.kept > 0)
                    notices_.emplace_back(juce::String(progress.kept)
                                              + ui::Ls(" fichier(s) laissé(s) dans l'ancien dossier : un fichier différent du même nom était déjà dans le nouveau",
                                                       " file(s) left in the old folder: a different file of the same name was already in the new one"),
                                          true);
            }
        }
    nextLocation();
    // Downloads ended: the samplers that play their banks load again.
    for (const auto& done : banks_.takeFinished()) {
        reloadSamplers(done.banks);
        // The banks' sounds are in the sample browser: the new ones too.
        if (samplesStarted_ && done.files > 0) {
            syncSampleBanks();
            samples_->rescan(false);
        }
        if (done.cancelled)
            notices_.emplace_back(ui::Ls("Téléchargement annulé : ", "Download cancelled: ") + done.label, false);
        else if (done.failed > 0)
            notices_.emplace_back(done.label + ui::Ls(" : ", ": ") + juce::String(done.failed)
                                      + ui::Ls(" fichier(s) non téléchargé(s) — ", " file(s) not downloaded — ") + done.error,
                                  true);
        else
            notices_.emplace_back(done.label + (done.generated ? ui::Ls(" : généré", ": generated") : ui::Ls(" : téléchargé", ": downloaded")),
                                  false);
    }
    // The virtual inputs and outputs written a second after their last change.
    if (virtualIoDirty_) {
        const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        if (virtualIoChangedAt_ <= 0.0)
            virtualIoChangedAt_ = now;
        else if (now - virtualIoChangedAt_ > 1.0) {
            flushVirtualIo();
            virtualIoChangedAt_ = 0.0;
        }
    }
    // A synth patch search: how far, and its end.
    if (synthSearch_ != nullptr) {
        soundMatching_.progress = synthSearch_->progress.load();
        {
            const std::lock_guard lock(synthSearch_->mutex);
            if (synthSearch_->message.isNotEmpty())
                soundMatching_.message = synthSearch_->message;
        }
        if (synthSearch_->done.load())
            finishSynthSearch();
    }
    if (soundMatching_.running && companion_.status().command == "analyse") {
        const auto status = companion_.status();
        soundMatching_.progress = status.progress;
        soundMatching_.message = status.message;
    }
    if (const auto finished = companion_.takeFinished()) {
        if (finished->command == "analyse")
            applySoundMatching(*finished);
        else
            companionJob_ = finished;
        // The environment made again: what it has for the search, asked again.
        if (finished->command == "setup")
            semantic_->checkEngine();
    }
    // A model installed (or not): the search follows.
    if (const auto installed = aiModels_.takeFinished()) {
        if (installed->ok) {
            notices_.emplace_back(ui::Ls("Modèle installé : ", "Model installed: ") + installed->id, false);
            semantic_->modelsChanged();
        } else if (installed->cancelled) {
            notices_.emplace_back(ui::Ls("Installation du modèle annulée", "Model install cancelled"), false);
        } else {
            notices_.emplace_back(ui::Ls("Installation du modèle impossible : ", "Could not install the model: ") + installed->error,
                                  true);
        }
    }
    if (engine_.isMidiRecording()) {
        (void) engine_.pollMidiRecording();
        if (!engine_.isPlaying())
            engine_.stopMidiRecording(true);
    }
    if (engine_.isMasterExporting() && engine_.masterExportComplete())
        (void) engine_.finishMasterExport();
    autosaveIfDue();
    applyTrackModulations();

    engine::AudioAnalysisFrame frame;
    const auto drain = [&frame](MeterState& meter, auto&& pop) {
        auto fresh = false;
        while (pop(frame))
            fresh = true;
        applyBallistics(meter, frame.peak.data(), frame.rms.data(), fresh ? static_cast<int>(frame.channelCount) : 0);
    };
    drain(masterMeter_, [this](engine::AudioAnalysisFrame& f) { return engine_.popMasterAnalysisFrame(f); });
    audioMeters_.resize(engine_.trackCount());
    for (std::size_t index = 0; index < engine_.trackCount(); ++index)
        drain(audioMeters_[index],
              [this, index](engine::AudioAnalysisFrame& f) { return engine_.popTrackAnalysisFrame(index, f); });
    for (auto& [id, state] : midiTracks_)
        drain(midiMeters_[id], [this, id = id](engine::AudioAnalysisFrame& f) {
            return engine_.popMidiTrackAnalysisFrame(id, f);
        });

    if (simulatorActive_) {
        publishSimulation(false);
        if (engine_.isSimulationPlaying())
            if (auto* root = workshopRoot())
                root->graph().setControlValues(engine_.simulationControlValues());
    }
    // Live values (LFOs, envelopes, the modulators) for the graph on screen:
    // they run with the transport stopped too (notes played live).
    if (watched_)
        if (auto* slot = graphFor(*watched_))
            slot->graph().setControlValues(watched_->master ? engine_.masterControlValues()
                                           : watched_->midi ? engine_.midiTrackControlValues(watched_->midiId)
                                                            : engine_.trackControlValues(watched_->audioIndex));
}

// ---- Modulation ----------------------------------------------------------------------------------

std::vector<modulation::Assignment> Studio::syncModulationTargets(AudioTrackState& state)
{
    auto stored = modulation::readAssignments(state.modulationAssignments);
    const auto trackIndex = static_cast<std::size_t>(&state - audioTracks_.data());
    const auto plugins = trackIndex < engine_.trackCount() ? engine_.trackPlugins(trackIndex) : std::vector<app::AudioTrackController::TrackInfo::PluginInfo> {};
    if (state.graph == nullptr
        || (state.modulationSyncedRevision == state.graph->revision() && state.modulationSyncedPlugins == plugins.size()))
        return stored;
    std::vector<modulation::Assignment> next;
    const auto parameters = state.graph->graph().editorGraphParameters();
    // An equaliser band's switch, shape and slope are not for an LFO to
    // sweep, nor is anything of a band that is off, nor its dynamics but
    // its range and threshold while they are on: eight bands would
    // otherwise bury the list under 96 rows.
    const auto sweepable = [&parameters](const juce::String& binding) {
        // Which band is heard alone is a held button's, not a modulator's;
        // the card a modulator belongs to is no setting.
        if (binding.endsWith("eqpro.listen") || binding.endsWith(".ownerLow") || binding.endsWith(".ownerHigh"))
            return false;
        const auto at = binding.lastIndexOf("eqpro.b");
        if (at < 0)
            return true;
        const auto rest = binding.substring(at + 7);
        const auto band = rest.initialSectionContainingOnly("0123456789");
        const auto value = rest.substring(band.length());
        const auto dynamics = value == "Range" || value == "Threshold";
        if (value != "Frequency" && value != "Gain" && value != "Q" && !dynamics)
            return false;
        const auto valueOf = [&parameters, &binding, at, &band](const char* name) {
            const auto key = binding.substring(0, at) + "eqpro.b" + band + name;
            for (const auto& parameter : parameters)
                if (parameter.binding == key)
                    return parameter.value;
            return 0.0;
        };
        if (valueOf("Enabled") < 0.5 || (dynamics && valueOf("Dynamic") < 0.5))
            return false;
        // Cuts, notches and band-passes have no gain.
        const auto type = static_cast<int>(std::lround(valueOf("Type")));
        return value != "Gain" || type < 3 || type > 6;
    };
    for (const auto& parameter : parameters) {
        modulation::Assignment assignment { parameter.binding, parameter.label, parameter.value,
                                            parameter.minimum, parameter.maximum, 0.25, false };
        const auto existing = std::find_if(stored.begin(), stored.end(), [&parameter](const auto& value) {
            return value.binding == parameter.binding;
        });
        // One already in use stays, whatever its band does.
        if (!sweepable(parameter.binding) && (existing == stored.end() || !existing->enabled))
            continue;
        if (existing != stored.end()) {
            assignment.amount = existing->amount;
            assignment.enabled = existing->enabled;
        }
        next.push_back(std::move(assignment));
    }
    // The hosted plug-ins' parameters its cards show (pinned, else the
    // first automatable ones), 0..1 as the plug-in takes them. One that
    // runs keeps the value it had when it was enabled as its base.
    for (std::size_t plugin = 0; plugin < plugins.size(); ++plugin) {
        std::vector<app::IsolatedPluginHost::ParameterSnapshot> parameters;
        if (!plugins[plugin].running || engine_.refreshTrackPluginParameters(trackIndex, plugin, parameters).failed())
            continue;
        std::vector<std::uint32_t> shown;
        if (plugin < state.pluginPins.size() && state.pluginPins[plugin])
            shown = *state.pluginPins[plugin];
        else
            for (const auto& parameter : parameters)
                if (parameter.automatable && parameter.name.isNotEmpty() && !parameter.name.startsWithIgnoreCase("MIDI CC")
                    && !parameter.name.equalsIgnoreCase("Bypass") && shown.size() < 8)
                    shown.push_back(parameter.index);
        for (const auto index : shown) {
            const auto found = std::find_if(parameters.begin(), parameters.end(), [index](const auto& p) { return p.index == index; });
            if (found == parameters.end())
                continue;
            const auto binding = pluginLaneName(plugin, index);
            modulation::Assignment assignment { binding, plugins[plugin].name + juce::String::fromUTF8(" \xc2\xb7 ") + found->name,
                                                found->value, 0.0, 1.0, 0.25, false };
            const auto existing = std::find_if(stored.begin(), stored.end(), [&binding](const auto& value) { return value.binding == binding; });
            if (existing != stored.end()) {
                assignment.amount = existing->amount;
                assignment.enabled = existing->enabled;
                if (existing->enabled)
                    assignment.base = existing->base;
            }
            next.push_back(std::move(assignment));
        }
    }
    state.modulationAssignments = modulation::writeAssignments(next);
    state.modulationSyncedRevision = state.graph->revision();
    state.modulationSyncedPlugins = plugins.size();
    return next;
}

std::vector<modulation::Assignment> Studio::modulationTargets(const TrackRef& track)
{
    if (track.master)
        return {};
    if (track.midi || track.audioIndex >= audioTracks_.size())
        return {};
    return syncModulationTargets(audioTracks_[track.audioIndex]);
}

void Studio::setModulationAssignment(const TrackRef& track, const juce::String& binding, const bool enabled,
                                     const double amount)
{
    if (track.master)
        return;
    if (track.midi || track.audioIndex >= audioTracks_.size())
        return;
    auto& state = audioTracks_[track.audioIndex];
    auto assignments = syncModulationTargets(state);
    for (auto& assignment : assignments)
        if (assignment.binding == binding) {
            // A plug-in's parameter: its value now is what it moves around
            // (and what it goes back to when turned off).
            std::size_t plugin {};
            std::uint32_t parameterIndex {};
            if (enabled && !assignment.enabled && parsePluginLane(binding, plugin, parameterIndex)) {
                std::vector<app::IsolatedPluginHost::ParameterSnapshot> parameters;
                if (engine_.refreshTrackPluginParameters(track.audioIndex, plugin, parameters).wasOk())
                    for (const auto& parameter : parameters)
                        if (parameter.index == parameterIndex)
                            assignment.base = parameter.value;
            }
            if (!enabled && assignment.enabled && parsePluginLane(binding, plugin, parameterIndex)) {
                (void) engine_.setTrackPluginParameter(track.audioIndex, plugin, parameterIndex, static_cast<float>(assignment.base));
                state.pluginModulationSent.erase(binding);
            }
            assignment.enabled = enabled;
            assignment.amount = juce::jlimit(-1.0, 1.0, amount);
        }
    state.modulationAssignments = modulation::writeAssignments(assignments);
    if (enabled && state.modulationGraph.getDynamicObject() == nullptr && state.modulationSlot == nullptr)
        state.modulationGraph = withoutSharedModules(app::cloneJsonValue(factoryModulationGraph_));
    state.modulationAppliedAt = -1.0;
    dirty_ = true;
}

namespace {

// The first LFO card of a value graph and where its four settings are.
struct LfoCard {
    std::uint64_t id {};
    juce::String rate, depth, offset, shape; // bindings
    double rateValue { 1.0 }, depthValue { 1.0 }, offsetValue {}, shapeValue {};
};

std::optional<LfoCard> firstLfoCard(const app::NodeGraphComponent& graph)
{
    for (const auto& node : graph.editorNodes()) {
        if (node.typeKey != "lfo" && node.typeKey != "lfo-core")
            continue;
        LfoCard card;
        card.id = node.id;
        for (const auto& parameter : node.parameters) {
            const auto key = parameter.binding.fromLastOccurrenceOf(".", false, false).toLowerCase();
            if (key.contains("rate")) {
                card.rate = parameter.binding;
                card.rateValue = parameter.value;
            } else if (key.contains("depth")) {
                card.depth = parameter.binding;
                card.depthValue = parameter.value;
            } else if (key.contains("offset")) {
                card.offset = parameter.binding;
                card.offsetValue = parameter.value;
            } else if (key.contains("shape")) {
                card.shape = parameter.binding;
                card.shapeValue = parameter.value;
            }
        }
        return card;
    }
    return std::nullopt;
}

} // namespace

GraphSlot* Studio::modulationGraphFor(const TrackRef& track)
{
    if (track.master || track.midi || track.audioIndex >= audioTracks_.size())
        return nullptr;
    auto& state = audioTracks_[track.audioIndex];
    if (state.modulationSlot == nullptr) {
        state.modulationSlot = makeSlot(GraphSlot::Owner::modulation);
        state.modulationSlot->restore(withModulationLibrary(state.modulationGraph));
    }
    return state.modulationSlot.get();
}

void Studio::modulationGraphEdited(const TrackRef& track)
{
    if (track.master || track.midi || track.audioIndex >= audioTracks_.size())
        return;
    if (auto& state = audioTracks_[track.audioIndex]; state.modulationSlot != nullptr)
        graphChanged(*state.modulationSlot, true);
}

modulation::LfoSettings Studio::modulationLfo(const TrackRef& track) const
{
    if (track.master)
        return {};
    if (track.midi || track.audioIndex >= audioTracks_.size())
        return {};
    // Its cards, when they are made (they may differ from the saved JSON's
    // knobs: a device's own settings).
    if (const auto& slot = audioTracks_[track.audioIndex].modulationSlot) {
        modulation::LfoSettings settings;
        if (const auto card = firstLfoCard(slot->graph())) {
            settings.found = true;
            settings.rate = card->rateValue;
            settings.depth = card->depthValue;
            settings.offset = card->offsetValue;
            settings.shape = static_cast<int>(std::lround(card->shapeValue));
        }
        return settings;
    }
    const auto& graph = audioTracks_[track.audioIndex].modulationGraph;
    return modulation::firstLfo(graph.getDynamicObject() != nullptr ? graph : factoryModulationGraph_);
}

void Studio::setModulationLfo(const TrackRef& track, const modulation::LfoSettings& settings)
{
    auto* slot = modulationGraphFor(track);
    if (slot == nullptr)
        return;
    auto& graph = slot->graph();
    const auto card = firstLfoCard(graph);
    if (!card)
        return;
    const auto setting = [&graph](const juce::String& binding, const double now, const double wanted) {
        if (binding.isNotEmpty() && std::abs(now - wanted) > 1.0e-9)
            (void) graph.editorSetParameter(binding, wanted);
    };
    setting(card->rate, card->rateValue, juce::jlimit(0.01, 40.0, settings.rate));
    setting(card->depth, card->depthValue, juce::jlimit(0.0, 1.0, settings.depth));
    setting(card->offset, card->offsetValue, juce::jlimit(-1.0, 1.0, settings.offset));
    setting(card->shape, card->shapeValue, static_cast<double>(juce::jlimit(0, 3, settings.shape)));
}

double Studio::modulationOutput(const TrackRef& track) const
{
    if (track.master)
        return 0.0;
    return !track.midi && track.audioIndex < audioTracks_.size() ? audioTracks_[track.audioIndex].modulationOutput : 0.0;
}

bool Studio::modulationActive(const TrackRef& track) const
{
    if (track.master)
        return false;
    return !track.midi && track.audioIndex < audioTracks_.size() && audioTracks_[track.audioIndex].modulationActive;
}

// As CyDAW 1 does it, about thirty times a second: the value graph at the
// playhead, each enabled assignment moving its parameter around its base.
void Studio::modulateNativeGraph(AudioTrackState& state, cydaw::engine::NativeDspGraph& graph)
{
    for (const auto& assignment : syncModulationTargets(state)) {
        if (!assignment.enabled || assignment.binding.startsWith("plugin:"))
            continue;
        const auto range = assignment.maximum - assignment.minimum;
        const auto value = juce::jlimit(std::min(assignment.minimum, assignment.maximum),
                                        std::max(assignment.minimum, assignment.maximum),
                                        assignment.base + state.modulationOutput * assignment.amount * range);
        (void) modulation::applyToNativeGraph(graph, nativeBinding(state.graph.get(), assignment.binding), value);
    }
}

void Studio::applyTrackModulations()
{
    const auto time = engine_.position();
    const auto playing = engine_.isPlaying();
    for (std::size_t index = 0; index < audioTracks_.size() && index < engine_.trackCount(); ++index) {
        auto& state = audioTracks_[index];
        auto anyEnabled = false;
        if (const auto* values = state.modulationAssignments.getArray())
            for (const auto& value : *values)
                if (const auto* object = value.getDynamicObject();
                    object != nullptr && static_cast<bool>(object->getProperty("enabled"))) {
                    anyEnabled = true;
                    break;
                }
        if (!anyEnabled) {
            if (state.modulationActive) {
                // Back to the values the graph holds.
                state.modulationActive = false;
                state.modulationOutput = 0.0;
                (void) publishGraph(TrackRef { false, index, 0 }, false);
            }
            state.pluginModulationSent.clear();
            continue;
        }
        const auto revision = state.graph != nullptr ? state.graph->revision() : 0;
        if (!playing && state.modulationActive && time == state.modulationAppliedAt
            && revision == state.modulationAppliedRevision)
            continue;
        const auto assignments = syncModulationTargets(state);
        auto graph = engine_.nativeDspGraph(index);
        if (graph.nodes.empty())
            continue;
        // The value graph's cards say what it is now (every node type a
        // modulation graph has, a device's own settings included).
        auto* values = modulationGraphFor(TrackRef { false, index, 0 });
        const auto output = values != nullptr ? juce::jlimit(-4.0, 4.0, values->graph().evaluateValueOutput(time))
                                              : modulation::evaluate(state.modulationGraph, time);
        auto changed = false;
        for (const auto& assignment : assignments) {
            const auto range = assignment.maximum - assignment.minimum;
            const auto value = juce::jlimit(std::min(assignment.minimum, assignment.maximum),
                                            std::max(assignment.minimum, assignment.maximum),
                                            assignment.base + (assignment.enabled ? output * assignment.amount * range : 0.0));
            // A hosted plug-in's parameter: sent to its process when it moved.
            if (assignment.binding.startsWith("plugin:")) {
                if (!assignment.enabled)
                    continue;
                std::size_t plugin {};
                std::uint32_t parameterIndex {};
                if (parsePluginLane(assignment.binding, plugin, parameterIndex)) {
                    const auto [entry, made] = state.pluginModulationSent.try_emplace(assignment.binding, -1.0f);
                    if (std::abs(entry->second - static_cast<float>(value)) > 1.0e-4f
                        && engine_.setTrackPluginParameter(index, plugin, parameterIndex, static_cast<float>(value)).wasOk())
                        entry->second = static_cast<float>(value);
                }
                continue;
            }
            changed = modulation::applyToNativeGraph(graph, nativeBinding(state.graph.get(), assignment.binding), value) || changed;
        }
        if (changed)
            (void) engine_.updateTrackNativeDspParameters(index, graph);
        state.modulationActive = true;
        state.modulationOutput = output;
        state.modulationAppliedAt = time;
        state.modulationAppliedRevision = revision;
    }
}

// ---- Transport -----------------------------------------------------------------------------------

void Studio::setLoopEnabled(const bool enabled)
{
    if (enabled && !engine_.hasExplicitLoopRange()) {
        const auto bar = 4.0 * 60.0 / std::max(1.0, engine_.tempo());
        const auto start = std::floor(engine_.position() / bar) * bar;
        engine_.setLoopRange(start, start + 4.0 * bar);
    }
    loopEnabled_ = enabled;
    engine_.setLooping(enabled);
    dirty_ = true;
}

void Studio::setLoopRange(const double startSeconds, const double endSeconds)
{
    engine_.setLoopRange(std::min(startSeconds, endSeconds), std::max(startSeconds, endSeconds));
    loopEnabled_ = true;
    engine_.setLooping(true);
    dirty_ = true;
}

void Studio::togglePlay()
{
    if (isRecording()) {
        (void) stopRecording(true);
        return;
    }
    if (engine_.isPlaying())
        stopPlayback();
    else
        play();
}

void Studio::play()
{
    if (engine_.isPlaying())
        return;
    std::vector<app::AudioTrackController::ClipToRestart> clips;
    for (const auto& [track, scene] : clipsAtStop_) {
        // A hybrid track's slot: its clip on whichever part holds it.
        const auto part = slotPart(track, scene);
        if (!part)
            continue;
        app::AudioTrackController::ClipToRestart clip;
        clip.midi = part->midi;
        clip.trackIndex = part->audioIndex;
        clip.midiTrackId = part->midiId;
        clip.sceneIndex = scene;
        if (part->midi)
            clip.midiClipId = midiClipInScene(engine_, part->midiId, scene)->id;
        clips.push_back(clip);
    }
    clipsAtStop_.clear();
    if (!engine_.restartLauncherClips(clips))
        engine_.play();
    // The master's clip that played at the stop, from its beginning.
    if (const auto scene = std::exchange(masterAtStop_, -1); scene >= 0 && masterClips_.contains(scene)) {
        masterPlaying_ = scene;
        masterStartSeconds_ = engine_.masterClockSeconds();
        publishMasterClip();
    }
}

void Studio::stopPlayback()
{
    if (engine_.isPlaying()) {
        clipsAtStop_.clear();
        for (const auto& track : tracks()) {
            const auto playing = playingScene(track);
            const auto scene = playing >= 0 ? playing : pendingScene(track);
            if (scene >= 0)
                clipsAtStop_.emplace_back(track, scene);
        }
        if (masterPlaying_ >= 0) {
            masterAtStop_ = masterPlaying_;
            masterPlaying_ = -1;
            publishMasterClip();
        }
    }
    engine_.stop();
}

void Studio::forgetClipAtStop(const TrackRef& track)
{
    std::erase_if(clipsAtStop_, [&track](const auto& entry) { return entry.first == track; });
}

void Studio::stop()
{
    if (isRecording()) {
        (void) stopRecording(true);
        return;
    }
    if (engine_.isPlaying())
        stopPlayback();
    else
        engine_.setPosition(0.0);
}

bool Studio::isRecording() const noexcept
{
    return recordingTrack_.has_value();
}

juce::File Studio::newRecordingFile(const TrackRef& track) const
{
    auto folder = projectFile_ != juce::File {} ? projectFile_.getParentDirectory().getChildFile("recordings")
                                                : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                                      .getChildFile("CyDAW")
                                                      .getChildFile("Recordings");
    folder.createDirectory();
    return folder.getNonexistentChildFile(trackName(track) + " " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S"), ".wav",
                                          false);
}

void Studio::startCompanionRecording(const TrackRef& owner, const TrackRef& companion)
{
    if (companion.midi) {
        // An audio track's notes: a clip from the bar, kept if notes came.
        const auto bar = std::max(1.0, static_cast<double>(engine_.beatsPerBar()));
        const auto beats = engine_.position() * engine_.tempo() / 60.0;
        const auto clip = engine_.addMidiClip(companion.midiId, {}, std::floor(beats / bar) * bar, bar * 4.0, false, trackColour(owner));
        if (clip == 0)
            return;
        (void) engine_.setLiveMidiTrack(companion.midiId);
        if (engine_.startMidiRecording(companion.midiId, clip, true, app::AudioTrackController::MidiRecordQuantization::none).failed()) {
            (void) engine_.removeMidiClip(companion.midiId, clip);
            return;
        }
        companionRecording_ = companion;
        companionRecordClip_ = clip;
        return;
    }
    // An instrument track's sound: only with an audio input chosen for it.
    if (!recordsHybridAudio(owner))
        return;
    armedBeforeCompanion_ = engine_.armedAudioTrack();
    engine_.setTrackArmed(companion.audioIndex, true);
    recordingFile_ = newRecordingFile(owner);
    if (engine_.startRecording(recordingFile_).failed()) {
        if (armedBeforeCompanion_)
            engine_.setTrackArmed(*armedBeforeCompanion_, true);
        else
            engine_.setTrackArmed(companion.audioIndex, false);
        armedBeforeCompanion_.reset();
        return;
    }
    recordingStart_ = engine_.position();
    companionRecording_ = companion;
    companionRecordClip_ = 0;
}

void Studio::stopCompanionRecording(const bool keep)
{
    const auto companion = std::exchange(companionRecording_, std::nullopt);
    if (!companion || !isValid(*companion))
        return;
    if (companion->midi) {
        engine_.stopMidiRecording(keep);
        // No note played: no clip.
        auto notes = false;
        if (const auto* clips = engine_.midiClipsView(companion->midiId))
            for (const auto& clip : *clips)
                notes = notes || (clip.id == companionRecordClip_ && !clip.notes.empty());
        if (!keep || !notes)
            (void) engine_.removeMidiClip(companion->midiId, companionRecordClip_);
        return;
    }
    engine_.stopRecording();
    if (keep)
        (void) keepRecordedAudio(*companion);
    else
        (void) recordingFile_.deleteFile();
    if (armedBeforeCompanion_)
        engine_.setTrackArmed(*armedBeforeCompanion_, true);
    else
        engine_.setTrackArmed(companion->audioIndex, false);
    armedBeforeCompanion_.reset();
}

juce::Result Studio::startRecording(std::optional<TrackRef> track, const std::uint64_t midiClip, const bool withCompanion)
{
    if (isRecording())
        return juce::Result::fail("already recording");
    if (track && track->master)
        track.reset();
    // An armed track wins: the one given when it is armed, else the first
    // armed (MIDI is recorded into one track at a time).
    if (!track || !isValid(*track) || !trackMix(*track).armed) {
        std::optional<TrackRef> armed;
        if (const auto audio = engine_.armedAudioTrack())
            armed = TrackRef { false, *audio, 0 };
        for (const auto& summary : engine_.midiTrackSummaries())
            if (summary.armed && !armed)
                armed = TrackRef { true, 0, summary.id };
        if (armed)
            track = armed;
    }
    if (!track || !isValid(*track))
        return juce::Result::fail(juce::String::fromUTF8("Sélectionnez ou armez une piste à enregistrer."));

    if (track->midi) {
        auto clip = midiClip;
        auto clipBelongs = false;
        if (const auto* clips = engine_.midiClipsView(track->midiId))
            for (const auto& candidate : *clips)
                clipBelongs = clipBelongs || candidate.id == clip;
        if (!clipBelongs) {
            // A new clip from the bar under the playhead, four bars long.
            const auto beats = engine_.position() * engine_.tempo() / 60.0;
            clip = engine_.addMidiClip(track->midiId, {}, std::floor(beats / engine_.beatsPerBar()) * engine_.beatsPerBar(),
                                       engine_.beatsPerBar() * 4.0, false, trackColour(*track));
        }
        (void) engine_.setLiveMidiTrack(track->midiId);
        const auto started = engine_.startMidiRecording(track->midiId, clip, true,
                                                        app::AudioTrackController::MidiRecordQuantization::none);
        if (started.failed())
            return started;
    } else {
        engine_.setTrackArmed(track->audioIndex, true);
        recordingFile_ = newRecordingFile(*track);
        const auto started = engine_.startRecording(recordingFile_);
        if (started.failed())
            return started;
        recordingStart_ = engine_.position();
    }
    // A hybrid track: its other part too (an audio track's notes, an
    // instrument track's sound when it has an audio input).
    if (withCompanion)
        if (const auto companion = companionOf(*track))
            startCompanionRecording(*track, *companion);
    recordingTrack_ = track;
    recordingLoopWasOn_ = loopEnabled_;
    // Audio goes on looping: each pass becomes a take of one clip.
    if (loopEnabled_ && track->midi)
        engine_.setLooping(false);
    recordingStartedPlayback_ = !engine_.isPlaying();
    if (recordingStartedPlayback_)
        engine_.play();
    dirty_ = true;
    return juce::Result::ok();
}

juce::Result Studio::recordIntoSlot(const TrackRef& owner, const int scene, const std::optional<bool> midi)
{
    if (slotRecording_)
        return stopSlotRecording(true);
    if (isRecording())
        return juce::Result::fail(ui::Ls("Un enregistrement est déjà en cours", "Already recording"));
    if (owner.master || owner.isBus() || !isValid(owner) || isBusTrack(owner) || !trackMix(owner).armed)
        return juce::Result::fail(ui::Ls("Armez la piste pour enregistrer dans sa case", "Arm the track to record into its slot"));
    if (slotHasClip(owner, scene))
        return juce::Result::fail(ui::Ls("La case a déjà un clip", "The slot already has a clip"));
    // The kind recorded: the track's own, or the other one into its
    // companion (made: the track becomes hybrid), armed for the recording.
    const auto part = midi ? makePartFor(owner, *midi) : std::optional<TrackRef>(owner);
    if (!part)
        return juce::Result::fail(ui::Ls("Cette piste n'enregistre pas ce genre de clip", "That track records no clip of that kind"));
    const auto& track = *part;
    slotRecordArmedPart_ = !(track == owner);
    armedBeforeSlotRecording_ = engine_.armedAudioTrack();
    if (slotRecordArmedPart_) {
        auto mix = trackMix(track);
        mix.armed = true;
        setTrackMix(track, mix);
    }
    const auto bar = std::max(1.0, static_cast<double>(engine_.beatsPerBar()));
    const auto beats = engine_.position() * engine_.tempo() / 60.0;
    // Playing: from the next bar; stopped: from the bar it is on.
    const auto start = engine_.isPlaying() ? std::ceil(beats / bar - 1.0e-9) * bar : std::floor(beats / bar + 1.0e-9) * bar;
    if (track.midi) {
        // Recorded as a clip of the arrangement from that bar (its notes
        // placed from its start), put in the slot when it stops (a launcher
        // clip has no place in time).
        const auto clip = engine_.addMidiClip(track.midiId, trackName(track), start, bar * 64.0, false, trackColour(track), -1);
        if (clip == 0)
            return juce::Result::fail(ui::Ls("Le clip n'a pas pu être créé", "The clip could not be made"));
        if (const auto started = startRecording(track, clip, false); started.failed()) {
            (void) engine_.removeMidiClip(track.midiId, clip);
            disarmSlotRecordingPart(track);
            return started;
        }
        slotRecordClip_ = clip;
    } else if (const auto started = startRecording(track, 0, false); started.failed()) {
        disarmSlotRecordingPart(track);
        return started;
    }
    slotRecording_ = std::make_pair(track, scene);
    slotRecordStartBeats_ = start;
    dirty_ = true;
    return juce::Result::ok();
}

juce::Result Studio::stopSlotRecording(const bool keep)
{
    if (!slotRecording_)
        return juce::Result::ok();
    const auto [track, scene] = *slotRecording_;
    slotRecording_.reset();
    // A companion armed for it: as it was.
    struct Disarm {
        Studio& studio;
        TrackRef part;
        ~Disarm() { studio.disarmSlotRecordingPart(part); }
    } disarm { *this, track };
    const auto secondsPerBeat = 60.0 / std::max(1.0, engine_.tempo());
    const auto bar = std::max(1.0, static_cast<double>(engine_.beatsPerBar()));
    const auto endBeats = engine_.position() / secondsPerBeat;
    const auto length = std::max(bar, std::ceil((endBeats - slotRecordStartBeats_) / bar - 1.0e-9) * bar);
    // The transport goes on: the clip plays on from there.
    recordingStartedPlayback_ = false;
    auto result = juce::Result::ok();
    if (track.midi) {
        result = stopRecording(keep);
        if (!keep) {
            (void) engine_.removeMidiClip(track.midiId, slotRecordClip_);
            return result;
        }
        (void) engine_.setMidiClipTimelineBounds(track.midiId, slotRecordClip_, length, 0.0, length, 0.0, length, true);
        (void) engine_.setMidiClipLauncherScene(track.midiId, slotRecordClip_, scene);
    } else {
        recordingTrack_.reset();
        engine_.stopRecording();
        if (recordingLoopWasOn_)
            engine_.setLooping(true);
        if (!keep) {
            (void) recordingFile_.deleteFile();
            return result;
        }
        // The file from where the recording started: the clip from the bar.
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        double recorded {};
        if (const std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(recordingFile_));
            reader != nullptr && reader->sampleRate > 0.0)
            recorded = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
        const auto offset = std::max(0.0, slotRecordStartBeats_ * secondsPerBeat - recordingStart_);
        if (recorded - offset <= 0.01)
            return juce::Result::fail(ui::Ls("Rien n'a été enregistré", "Nothing was recorded"));
        const auto duration = std::min(length * secondsPerBeat, recorded - offset);
        if (!engine_.setLauncherClip(track.audioIndex, scene, trackName(track), offset, duration, true, trackColour(track), recordingFile_))
            return juce::Result::fail(ui::Ls("Le clip n'a pas pu être créé", "The clip could not be made"));
    }
    dirty_ = true;
    // Launched on its track (a companion's clip stops the track's other one).
    (void) launchSlot(shownTrackOf(track), scene);
    return result;
}

void Studio::disarmSlotRecordingPart(const TrackRef& part)
{
    if (!std::exchange(slotRecordArmedPart_, false) || !isValid(part))
        return;
    if (part.midi) {
        auto mix = trackMix(part);
        mix.armed = false;
        setTrackMix(part, mix);
    } else if (armedBeforeSlotRecording_) {
        engine_.setTrackArmed(*armedBeforeSlotRecording_, true);
    } else {
        engine_.setTrackArmed(part.audioIndex, false);
    }
    armedBeforeSlotRecording_.reset();
}

juce::Result Studio::stopRecording(const bool keep)
{
    // Recording into a slot: that slot's clip made (the transport stopped
    // or the record button let go).
    if (slotRecording_)
        return stopSlotRecording(keep);
    if (!recordingTrack_)
        return juce::Result::ok();
    const auto track = *recordingTrack_;
    recordingTrack_.reset();
    auto result = juce::Result::ok();
    if (track.midi) {
        engine_.stopMidiRecording(keep);
    } else {
        engine_.stopRecording();
        if (keep) {
            result = keepRecordedAudio(track);
        } else {
            (void) recordingFile_.deleteFile();
        }
    }
    stopCompanionRecording(keep);
    if (recordingStartedPlayback_)
        engine_.stop();
    if (recordingLoopWasOn_)
        engine_.setLooping(true);
    recordingStartedPlayback_ = false;
    return result;
}

// What a recording becomes: passes of the loop as takes of one clip; over
// a clip, a new take of it; otherwise a clip of its own.
juce::Result Studio::keepRecordedAudio(const TrackRef& track)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    double length {};
    if (const std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(recordingFile_));
        reader != nullptr && reader->sampleRate > 0.0)
        length = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
    if (length <= 0.0)
        return juce::Result::fail("Nothing was recorded");
    const auto index = track.audioIndex;
    const auto name = trackName(track);
    const auto unstretched = [](const app::AudioTrackController::AudioClip& clip) {
        return clip.events.empty() && !clip.usesEventTimeline && clip.stretchMarkers.empty()
               && std::abs(clip.sourceDurationSeconds - clip.durationSeconds) < 1.0e-6;
    };
    // Looped: every pass a take, the last one played.
    const auto loopStart = engine_.loopStart();
    const auto loopLength = engine_.loopEnd() - loopStart;
    if (recordingLoopWasOn_ && loopLength > 0.05 && recordingStart_ < engine_.loopEnd()) {
        const auto firstFull = recordingStart_ <= loopStart ? loopStart - recordingStart_ : engine_.loopEnd() - recordingStart_;
        auto passes = static_cast<int>(std::floor((length - firstFull) / loopLength));
        if (length - firstFull - passes * loopLength > loopLength * 0.5)
            ++passes;
        if (passes >= 2) {
            std::optional<app::AudioTrackController::AudioClip> host;
            for (const auto& clip : engine_.audioClips(index))
                if (unstretched(clip) && clip.startSeconds <= loopStart + 0.01
                    && clip.startSeconds + clip.durationSeconds >= loopStart + loopLength - 0.01)
                    host = clip;
            auto first = 0;
            if (!host) {
                const auto clipId = engine_.addAudioClip(index, loopStart, firstFull, loopLength, name, trackColour(track), recordingFile_);
                if (clipId == 0)
                    return juce::Result::fail("The loop's clip could not be made");
                host = engine_.audioClip(index, clipId);
                first = 1;
            }
            if (!host)
                return juce::Result::fail("The loop's clip could not be made");
            std::vector<app::AudioTrackController::AudioTake> takes;
            const auto hostLocalLoop = loopStart - host->startSeconds;
            for (auto pass = first; pass < passes; ++pass)
                takes.push_back({ name + " " + juce::String(pass + 1), recordingFile_, 0.0,
                                  firstFull + pass * loopLength - hostLocalLoop - host->sourceOffsetSeconds });
            dirty_ = true;
            return engine_.addAudioTakes(index, host->id, std::move(takes), true);
        }
    }
    // Over a clip: a take of it, where it was played from.
    std::optional<app::AudioTrackController::AudioClip> over;
    auto best = 0.0;
    for (const auto& clip : engine_.audioClips(index)) {
        const auto overlap = std::min(clip.startSeconds + clip.durationSeconds, recordingStart_ + length)
                             - std::max(clip.startSeconds, recordingStart_);
        if (unstretched(clip) && recordingStart_ >= clip.startSeconds - 0.05
            && recordingStart_ < clip.startSeconds + clip.durationSeconds && overlap > best) {
            best = overlap;
            over = clip;
        }
    }
    if (over && recordTakesOnClips_) {
        const auto localStart = recordingStart_ - over->startSeconds;
        std::vector<app::AudioTrackController::AudioTake> takes;
        takes.push_back({ name + " " + juce::String(static_cast<int>(std::max<std::size_t>(1, over->takes.size())) + 1), recordingFile_,
                          0.0, -localStart - over->sourceOffsetSeconds });
        dirty_ = true;
        return engine_.addAudioTakes(index, over->id, std::move(takes), true);
    }
    app::AudioTrackController::RecordedTakeResult take;
    const auto result = engine_.commitRecordedTake(recordingFile_, index, recordingStart_, take);
    if (result.wasOk() && take.createdTrack)
        adoptAudioTrack(take.trackIndex, {});
    return result;
}

// ---- Audio editing --------------------------------------------------------------------------

juce::File Studio::audioEditFolder() const
{
    return projectFile_ != juce::File {} ? projectFile_.getParentDirectory().getChildFile("edits")
                                         : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                               .getChildFile("CyDAW")
                                               .getChildFile("Edits");
}

juce::Result Studio::editAudioClip(const TrackRef& track, const std::uint64_t clipId, const app::AudioEditRequest& request,
                                   const double sourceStart, const double sourceLength, const juce::String& label)
{
    if (track.midi || track.master || !isValid(track))
        return juce::Result::fail(ui::Ls("Pas une piste audio", "Not an audio track"));
    if (const auto problem = engine_.audioClipEditProblem(track.audioIndex, clipId, request.operation); problem.isNotEmpty())
        return juce::Result::fail(problem);
    const auto clip = engine_.audioClip(track.audioIndex, clipId);
    if (!clip)
        return juce::Result::fail(ui::Ls("Clip introuvable", "No such clip"));
    auto request2 = request;
    if (request.operation == app::AudioEditOperation::paste) {
        if (!audioClipboard_)
            return juce::Result::fail(ui::Ls("Rien à coller : copiez d'abord un passage", "Nothing to paste: copy a passage first"));
        request2.pasteFile = audioClipboard_->file;
        request2.pasteStartSeconds = audioClipboard_->start;
        request2.pasteDurationSeconds = audioClipboard_->length;
    }
    auto folder = audioEditFolder();
    (void) folder.createDirectory();
    const auto base = clip->sourceFile.getFileNameWithoutExtension().upToLastOccurrenceOf(" ~", false, false);
    const auto destination = folder.getNonexistentChildFile(base + " ~" + juce::Time::getCurrentTime().formatted("%H%M%S"), ".wav", false);
    const auto rendered = app::AudioTrackController::renderAudioEdit(clip->sourceFile, sourceStart, sourceLength, request2, destination);
    if (rendered.failed())
        return rendered;
    const auto committed = engine_.commitAudioClipSourceEdit(track.audioIndex, clipId, destination, label, request.operation, sourceStart,
                                                             request.operation == app::AudioEditOperation::paste ? audioClipboard_->length
                                                                                                                  : sourceLength);
    if (committed.failed()) {
        (void) destination.deleteFile();
        return committed;
    }
    dirty_ = true;
    return juce::Result::ok();
}

// ---- Processings ------------------------------------------------------------------------

juce::Result Studio::readAudioClipSound(const TrackRef& track, const std::uint64_t clipId, engine::repair::Channels& channels,
                                        double& rate) const
{
    if (track.midi || track.master || !isValid(track))
        return juce::Result::fail(ui::Ls("Pas une piste audio", "Not an audio track"));
    const auto clip = engine_.audioClip(track.audioIndex, clipId);
    if (!clip)
        return juce::Result::fail(ui::Ls("Clip introuvable", "No such clip"));
    if (const auto problem = engine_.audioClipEditProblem(track.audioIndex, clipId, app::AudioEditOperation::amplify); problem.isNotEmpty())
        return juce::Result::fail(problem);
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(clip->sourceFile));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return juce::Result::fail(ui::Ls("Fichier illisible : ", "Unreadable file: ") + clip->sourceFile.getFileName());
    if (reader->lengthInSamples > static_cast<juce::int64>(reader->sampleRate * 60.0 * 30.0))
        return juce::Result::fail(ui::Ls("Son trop long (30 minutes au plus)", "Sound too long (30 minutes at most)"));
    const auto frames = static_cast<int>(reader->lengthInSamples);
    const auto count = static_cast<int>(std::clamp(reader->numChannels, 1u, 2u));
    juce::AudioBuffer<float> buffer(count, frames);
    reader->read(&buffer, 0, frames, 0, true, count > 1);
    channels.assign(static_cast<std::size_t>(count), std::vector<float>(static_cast<std::size_t>(frames)));
    for (int c = 0; c < count; ++c)
        std::copy_n(buffer.getReadPointer(c), frames, channels[static_cast<std::size_t>(c)].begin());
    rate = reader->sampleRate;
    return juce::Result::ok();
}

juce::Result Studio::applyAudioProcessing(const TrackRef& track, const std::uint64_t clipId, const processing::AudioResult& result,
                                          const double rate, const std::int64_t sourceFrames, const juce::String& label)
{
    if (track.midi || track.master || !isValid(track))
        return juce::Result::fail(ui::Ls("Pas une piste audio", "Not an audio track"));
    const auto clip = engine_.audioClip(track.audioIndex, clipId);
    if (!clip)
        return juce::Result::fail(ui::Ls("Clip introuvable", "No such clip"));
    if (!result.ok || result.channels.empty() || result.channels.front().empty())
        return juce::Result::fail(result.error.isNotEmpty() ? result.error : ui::Ls("Pas de son", "No sound"));
    auto folder = audioEditFolder();
    (void) folder.createDirectory();
    const auto base = clip->sourceFile.getFileNameWithoutExtension().upToLastOccurrenceOf(" ~", false, false);
    const auto destination = folder.getNonexistentChildFile(base + " ~" + juce::Time::getCurrentTime().formatted("%H%M%S"), ".wav", false);
    {
        const auto frames = static_cast<int>(result.channels.front().size());
        juce::AudioBuffer<float> buffer(static_cast<int>(result.channels.size()), frames);
        for (int c = 0; c < buffer.getNumChannels(); ++c)
            std::copy_n(result.channels[static_cast<std::size_t>(c)].data(), frames, buffer.getWritePointer(c));
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream(destination.createOutputStream());
        std::unique_ptr<juce::AudioFormatWriter> writer(
            stream != nullptr ? wav.createWriterFor(stream.get(), rate, static_cast<unsigned>(buffer.getNumChannels()), 24, {}, 0) : nullptr);
        if (writer == nullptr)
            return juce::Result::fail(ui::Ls("Impossible d'écrire ", "Cannot write ") + destination.getFullPathName());
        (void) stream.release();
        if (!writer->writeFromAudioSampleBuffer(buffer, 0, frames))
            return juce::Result::fail(ui::Ls("Impossible d'écrire ", "Cannot write ") + destination.getFullPathName());
    }
    // How the clip follows: shorter (trimmed: what is left), longer (its tail
    // past the source's end, heard when the clip played to that end).
    const auto frames = static_cast<std::int64_t>(result.channels.front().size());
    auto operation = app::AudioEditOperation::amplify;
    auto selectionStart = 0.0;
    auto selection = 0.0;
    if (result.trimmedStart > 0 || frames < sourceFrames) {
        operation = app::AudioEditOperation::crop;
        selectionStart = static_cast<double>(result.trimmedStart) / rate;
        selection = static_cast<double>(frames) / rate;
    } else if (frames > sourceFrames) {
        operation = app::AudioEditOperation::insertSilence;
        selectionStart = static_cast<double>(sourceFrames) / rate;
        selection = static_cast<double>(frames - sourceFrames) / rate;
    }
    const auto committed = engine_.commitAudioClipSourceEdit(track.audioIndex, clipId, destination, label, operation, selectionStart, selection);
    if (committed.failed()) {
        (void) destination.deleteFile();
        return committed;
    }
    dirty_ = true;
    return juce::Result::ok();
}

std::vector<processing::ScriptNote> Studio::midiClipNotes(const TrackRef& track, const std::uint64_t clipId, double& clipLength) const
{
    std::vector<processing::ScriptNote> notes;
    clipLength = 0.0;
    if (!track.midi || !isValid(track))
        return notes;
    if (const auto* clips = engine_.midiClipsView(track.midiId))
        for (const auto& clip : *clips)
            if (clip.id == clipId)
                clipLength = clip.lengthBeats;
    const auto source = midiPreview_ && midiPreview_->trackId == track.midiId && midiPreview_->clipId == clipId
                            ? midiPreview_->notes
                            : engine_.midiNotes(track.midiId, clipId);
    for (std::size_t i = 0; i < source.size(); ++i) {
        const auto& note = source[i];
        notes.push_back({ note.noteNumber, note.startBeats, note.durationBeats, note.velocity, note.chance, static_cast<int>(i) });
    }
    return notes;
}

juce::Result Studio::setMidiClipNotes(const TrackRef& track, const std::uint64_t clipId, const std::vector<processing::ScriptNote>& notes,
                                      const bool undoable)
{
    if (!track.midi || !isValid(track))
        return juce::Result::fail(ui::Ls("Pas une piste MIDI", "Not a MIDI track"));
    // The notes as they were (before any preview): each processed one carries
    // its source's expressions.
    if (!midiPreview_ || midiPreview_->trackId != track.midiId || midiPreview_->clipId != clipId) {
        restoreMidiClipNotes(track, clipId);
        midiPreview_ = MidiPreview { track.midiId, clipId, engine_.midiNotes(track.midiId, clipId) };
    }
    const auto& original = midiPreview_->notes;
    std::vector<app::AudioTrackController::MidiNote> made;
    made.reserve(notes.size());
    for (const auto& note : notes) {
        app::AudioTrackController::MidiNote out;
        if (note.source >= 0 && note.source < static_cast<int>(original.size()))
            out = original[static_cast<std::size_t>(note.source)];
        out.id = 0;
        out.noteNumber = note.pitch;
        out.startBeats = note.start;
        out.durationBeats = note.length;
        out.velocity = note.velocity;
        out.chance = note.chance;
        made.push_back(std::move(out));
    }
    if (undoable) {
        // Back to the notes as they were, then the change as one step.
        const auto before = original;
        midiPreview_.reset();
        (void) engine_.replaceMidiNotes(track.midiId, clipId, before);
        if (!engine_.beginMidiEdit(track.midiId, clipId))
            return juce::Result::fail(ui::Ls("Clip introuvable", "No such clip"));
        const auto replaced = engine_.replaceMidiNotes(track.midiId, clipId, std::move(made));
        (void) engine_.endMidiEdit();
        if (!replaced)
            return juce::Result::fail(ui::Ls("Les notes n'ont pas pu être changées", "The notes could not be changed"));
        dirty_ = true;
        return juce::Result::ok();
    }
    if (!engine_.replaceMidiNotes(track.midiId, clipId, std::move(made)))
        return juce::Result::fail(ui::Ls("Les notes n'ont pas pu être changées", "The notes could not be changed"));
    return juce::Result::ok();
}

void Studio::restoreMidiClipNotes(const TrackRef& track, const std::uint64_t clipId)
{
    if (!midiPreview_)
        return;
    if (track.midi && midiPreview_->trackId == track.midiId && midiPreview_->clipId == clipId)
        (void) engine_.replaceMidiNotes(midiPreview_->trackId, midiPreview_->clipId, midiPreview_->notes);
    else
        (void) engine_.replaceMidiNotes(midiPreview_->trackId, midiPreview_->clipId, midiPreview_->notes);
    midiPreview_.reset();
}

juce::Result Studio::reverseLauncherClip(const TrackRef& track, int scene)
{
    if(track.midi || track.master || !isValid(track))return juce::Result::fail("No audio track");
    const auto clip=engine_.launcherClip(track.audioIndex,scene);if(!clip)return juce::Result::fail("No launcher clip");
    auto events=clip->events;if(events.empty())return juce::Result::fail(ui::Ls("Ce clip est vide","This clip is empty"));
    const auto folder=audioEditFolder();if(const auto result=folder.createDirectory();result.failed())return result;
    if(const auto result=reverseEvents(events,clip->durationSeconds,folder);result.failed())return result;
    const auto result=engine_.replaceLauncherClipEvents(track.audioIndex,scene,std::move(events));if(result.wasOk())markDirty();return result;
}
juce::Result Studio::reverseAudioClip(const TrackRef& track, std::uint64_t id)
{
    if(track.midi || track.master || !isValid(track))return juce::Result::fail("No audio track");
    const auto clip=engine_.audioClip(track.audioIndex,id);if(!clip)return juce::Result::fail("No audio clip");
    if(!clip->usesEventTimeline && clip->events.empty()) {
        app::AudioEditRequest request;request.operation=app::AudioEditOperation::reverse;
        return editAudioClip(track,id,request,clip->sourceOffsetSeconds,clip->sourceDurationSeconds>0?clip->sourceDurationSeconds:clip->durationSeconds,ui::Ls("Inverser","Reverse"));
    }
    auto events=engine_.audioClipEvents(track.audioIndex,id);
    const auto folder=audioEditFolder();if(const auto result=folder.createDirectory();result.failed())return result;
    if(const auto result=reverseEvents(events,clip->durationSeconds,folder);result.failed())return result;
    const auto result=engine_.replaceAudioClipEvents(track.audioIndex,id,std::move(events));if(result.wasOk())markDirty();return result;
}

juce::Result Studio::flattenAudioClipComp(const TrackRef& track, const std::uint64_t clipId)
{
    if (track.midi || track.master || !isValid(track))
        return juce::Result::fail(ui::Ls("Pas une piste audio", "Not an audio track"));
    const auto clip = engine_.audioClip(track.audioIndex, clipId);
    if (!clip)
        return juce::Result::fail(ui::Ls("Clip introuvable", "No such clip"));
    if (clip->takes.size() < 2 || clip->comp.empty())
        return juce::Result::fail(ui::Ls("Ce clip joue une seule prise : rien à mettre à plat",
                                         "This clip plays one take: nothing to flatten"));
    auto folder = audioEditFolder();
    (void) folder.createDirectory();
    const auto base = (clip->name.isNotEmpty() ? clip->name : clip->sourceFile.getFileNameWithoutExtension())
                          .upToLastOccurrenceOf(" ~", false, false);
    const auto destination = folder.getNonexistentChildFile(base + " ~comp " + juce::Time::getCurrentTime().formatted("%H%M%S"), ".wav", false);
    if (const auto rendered = app::AudioTrackController::renderAudioClipComp(*clip, destination); rendered.failed())
        return rendered;
    const auto committed = engine_.commitAudioClipCompFlatten(track.audioIndex, clipId, destination,
                                                              ui::Ls("Mise à plat des prises", "Takes flattened"));
    if (committed.failed()) {
        (void) destination.deleteFile();
        return committed;
    }
    dirty_ = true;
    return juce::Result::ok();
}

juce::Result Studio::editAudioEvent(const TrackRef& track, const std::uint64_t clipId, const std::uint64_t eventId,
                                    const app::AudioEditRequest& request, const double sourceStart, const double sourceLength)
{
    if (track.midi || track.master || !isValid(track))
        return juce::Result::fail(ui::Ls("Pas une piste audio", "Not an audio track"));
    if (app::changesLength(request.operation))
        return juce::Result::fail(ui::Ls("Un événement se raccourcit ou se coupe dans la vue des événements",
                                         "An event is shortened or cut in the events view"));
    std::optional<app::AudioTrackController::AudioEvent> event;
    for (const auto& candidate : engine_.audioClipEvents(track.audioIndex, clipId))
        if (candidate.id == eventId)
            event = candidate;
    if (!event)
        return juce::Result::fail(ui::Ls("Événement introuvable", "No such event"));
    auto folder = audioEditFolder();
    (void) folder.createDirectory();
    const auto base = event->sourceFile.getFileNameWithoutExtension().upToLastOccurrenceOf(" ~", false, false);
    const auto destination = folder.getNonexistentChildFile(base + " ~" + juce::Time::getCurrentTime().formatted("%H%M%S"), ".wav", false);
    // What it plays of its file (or the part asked), edited; the rest of
    // the file as it is.
    const auto partial = sourceStart >= 0.0 && sourceLength > 0.0;
    const auto rendered = app::AudioTrackController::renderAudioEdit(event->sourceFile, partial ? sourceStart : event->sourceOffsetSeconds,
                                                                     partial ? sourceLength : event->sourceDurationSeconds, request,
                                                                     destination);
    if (rendered.failed())
        return rendered;
    const auto committed = engine_.commitAudioClipEventEdit(track.audioIndex, clipId, eventId, destination);
    if (committed.failed()) {
        (void) destination.deleteFile();
        return committed;
    }
    dirty_ = true;
    return juce::Result::ok();
}

juce::Result Studio::editLauncherAudioEvent(const TrackRef& track, const int scene, const std::uint64_t eventId,
                                            const app::AudioEditRequest& request, const double sourceStart, const double sourceLength)
{
    if (track.midi || track.master || !isValid(track))
        return juce::Result::fail(ui::Ls("Pas une piste audio", "Not an audio track"));
    if (app::changesLength(request.operation))
        return juce::Result::fail(ui::Ls("Un événement se raccourcit ou se coupe dans la vue des événements",
                                         "An event is shortened or cut in the events view"));
    const auto clip = engine_.launcherClip(track.audioIndex, scene);
    if (!clip)
        return juce::Result::fail(ui::Ls("Clip du lanceur introuvable", "No such launcher clip"));
    auto events = clip->events;
    const auto event = std::find_if(events.begin(), events.end(), [eventId](const auto& candidate) { return candidate.id == eventId; });
    if (event == events.end())
        return juce::Result::fail(ui::Ls("Événement introuvable", "No such event"));
    auto folder = audioEditFolder();
    (void) folder.createDirectory();
    const auto base = event->sourceFile.getFileNameWithoutExtension().upToLastOccurrenceOf(" ~", false, false);
    const auto destination = folder.getNonexistentChildFile(base + " ~" + juce::Time::getCurrentTime().formatted("%H%M%S"), ".wav", false);
    // What it plays of its file (or the part asked), edited; the rest of
    // the file as it is: its place in the file does not move.
    const auto partial = sourceStart >= 0.0 && sourceLength > 0.0;
    const auto rendered = app::AudioTrackController::renderAudioEdit(event->sourceFile, partial ? sourceStart : event->sourceOffsetSeconds,
                                                                     partial ? sourceLength : event->sourceDurationSeconds, request,
                                                                     destination);
    if (rendered.failed())
        return rendered;
    event->sourceFile = destination;
    const auto replaced = engine_.replaceLauncherClipEvents(track.audioIndex, scene, std::move(events));
    if (replaced.failed()) {
        (void) destination.deleteFile();
        return replaced;
    }
    dirty_ = true;
    return juce::Result::ok();
}

void Studio::copyAudio(const juce::File& file, const double start, const double length)
{
    if (file.existsAsFile() && length > 0.0)
        audioClipboard_ = AudioClipboard { file, std::max(0.0, start), length };
}

void Studio::rewind()
{
    engine_.setPosition(0.0);
}

juce::Result Studio::auditionSample(const juce::String& path, const double startSeconds, const bool atTempo)
{
    app::AudioTrackController::SamplePreviewTempo tempo;
    // A loop of a sure tempo: named for it, or typed in, or heard clearly
    // in a loop that is not one held note of an instrument (a recorder's long
    // note has a tempo the detector guesses).
    const auto sureLoop = [](const app::SampleRecord& record) {
        if (record.bpm <= 0.0)
            return false;
        if (record.bpmSource == "name" || record.bpmSource == "user")
            return true;
        return record.type == "loop" && record.bpmConfidence >= 0.5f
               && app::SampleLibrary::familyOf(record.category) != "instruments";
    };
    if (atTempo)
        if (const auto* record = samples_->find(path); record != nullptr && sureLoop(*record)) {
            tempo.sourceBpm = record->bpm;
            tempo.algorithm = app::SampleLibrary::familyOf(record->category) == "drums"
                                  ? static_cast<int>(app::AudioTrackController::StretchMode::beats)
                                  : static_cast<int>(app::AudioTrackController::StretchMode::tonal);
        }
    return engine_.startSamplePreview(juce::File(path), std::max(0.0, startSeconds), tempo);
}

double Studio::auditionTempoOf(const juce::String& path) const
{
    if (engine_.samplePreviewLoopBeats() <= 0)
        return 0.0;
    const auto* record = samples_->find(path);
    return record != nullptr ? record->bpm : 0.0;
}

// ---- A rack of pads ----------------------------------------------------------------------

engine::instrument_container::Kind Studio::containerKind(const TrackRef& track) const
{
    const auto* slot = track.midi ? graphFor(track.owner()) : nullptr;
    if (slot == nullptr)
        return engine::instrument_container::Kind::none;
    for (const auto& node : slot->graph().editorNodes())
        if (const auto kind = engine::instrument_container::kindOfType(node.typeKey.toStdString());
            kind != engine::instrument_container::Kind::none)
            return kind;
    return engine::instrument_container::Kind::none;
}

bool Studio::isRack(const TrackRef& track) const
{
    return containerKind(track) != engine::instrument_container::Kind::none;
}

int Studio::selectedLayer(const TrackRef& track) const
{
    return track.midi ? engine_.midiTrackSelectedLayer(track.midiId) : -1;
}

Studio::RackPadMix Studio::mixOf(const RackPadState& pad)
{
    return { pad.note,   pad.name,   pad.playAs,      pad.gainDb,      pad.pan,          pad.muted,
             pad.solo,   pad.choke,  pad.keyLow,      pad.keyHigh,     pad.velocityLow,  pad.velocityHigh };
}

std::size_t Studio::rackPadCount(const TrackRef& track) const
{
    const auto found = track.midi ? midiTracks_.find(track.midiId) : midiTracks_.end();
    return found != midiTracks_.end() ? found->second.pads.size() : 0;
}

const RackPadState* Studio::rackPad(const TrackRef& track, const std::size_t pad) const
{
    const auto found = track.midi ? midiTracks_.find(track.midiId) : midiTracks_.end();
    return found != midiTracks_.end() && pad < found->second.pads.size() ? &found->second.pads[pad] : nullptr;
}

RackPadState* Studio::rackPadState(const TrackRef& track)
{
    const auto found = track.midi ? midiTracks_.find(track.midiId) : midiTracks_.end();
    return found != midiTracks_.end() && track.pad >= 0 && static_cast<std::size_t>(track.pad) < found->second.pads.size()
               ? &found->second.pads[static_cast<std::size_t>(track.pad)]
               : nullptr;
}

std::optional<std::size_t> Studio::rackPadOfNote(const TrackRef& track, const int note) const
{
    for (std::size_t pad = 0; pad < rackPadCount(track); ++pad)
        if (rackPad(track, pad)->note == note)
            return pad;
    return std::nullopt;
}

void Studio::publishRackPadMix(const TrackRef& track, const std::size_t pad)
{
    publishRackPadMix(track, pad, containerKind(track));
}

void Studio::publishRackPadMix(const TrackRef& track, const std::size_t pad, const engine::instrument_container::Kind kind)
{
    using Kind = engine::instrument_container::Kind;
    const auto* state = rackPad(track, pad);
    const auto found = track.midi ? midiTracks_.find(track.midiId) : midiTracks_.end();
    if (state == nullptr || found == midiTracks_.end())
        return;
    // A pad sounds only while its track's instrument is the container it was made for.
    const auto playing = kind != Kind::none && kind == found->second.padsKind;
    const auto drum = kind == Kind::drum;
    app::AudioTrackController::PadMix mix;
    mix.note = !playing ? -1 : drum ? state->note : 0;
    mix.playAs = drum ? state->playAs : -1;
    mix.gainDb = state->gainDb;
    mix.pan = state->pan;
    mix.muted = state->muted;
    mix.solo = state->solo;
    mix.choke = drum ? state->choke : 0;
    if (!drum) {
        mix.keyLow = state->keyLow;
        mix.keyHigh = state->keyHigh;
        mix.velocityLow = state->velocityLow;
        mix.velocityHigh = state->velocityHigh;
    }
    engine_.setMidiTrackRackPadMix(track.midiId, pad, mix);
}

void Studio::publishPadMixes(const TrackRef& track)
{
    const auto kind = containerKind(track);
    for (std::size_t pad = 0; pad < rackPadCount(track); ++pad)
        publishRackPadMix(track.owner(), pad, kind);
}

void Studio::clearPads(const TrackRef& track)
{
    const auto found = track.midi ? midiTracks_.find(track.midiId) : midiTracks_.end();
    if (found == midiTracks_.end())
        return;
    for (std::size_t pad = 0; pad < found->second.pads.size(); ++pad)
        engine_.setMidiTrackRackPadMix(track.midiId, pad, {});
    found->second.pads.clear();
    dirty_ = true;
}

std::optional<std::size_t> Studio::addRackPad(const TrackRef& track, const int note, const juce::String& instrumentKey, juce::String& error)
{
    const auto found = track.midi ? midiTracks_.find(track.midiId) : midiTracks_.end();
    if (found == midiTracks_.end()) {
        error = ui::Ls("Piste instrument introuvable", "No such instrument track");
        return std::nullopt;
    }
    auto& pads = found->second.pads;
    // The first ones are made for the track's container.
    if (const auto kind = containerKind(track); pads.empty() && kind != engine::instrument_container::Kind::none)
        found->second.padsKind = kind;
    const auto layers = found->second.padsKind != engine::instrument_container::Kind::drum;
    if (pads.size() >= (layers ? static_cast<std::size_t>(engine::instrument_container::maximumLayers) : app::AudioTrackController::maximumRackPads)) {
        error = layers ? ui::Ls("Le conteneur a déjà toutes ses couches", "The container has all its layers already")
                       : ui::Ls("La Drum Machine a déjà tous ses pads", "The Drum Machine has all its pads already");
        return std::nullopt;
    }
    RackPadState pad;
    pad.note = std::clamp(note, 0, 127);
    pad.graph = makeSlot(GraphSlot::Owner::midiTrack);
    pad.graph->restore(withLibrary(juce::var(), factoryMidiGraph_));
    pads.push_back(std::move(pad));
    const auto index = pads.size() - 1;
    const auto fallback = layers ? juce::String("opale-instrument") : juce::String("kick-instrument");
    if (!setRackPadInstrument(track.owner(), index, instrumentKey.isNotEmpty() ? instrumentKey : fallback, error)) {
        // The slots of the pads may have moved: looked for again.
        if (const auto again = midiTracks_.find(track.midiId); again != midiTracks_.end() && again->second.pads.size() == index + 1)
            again->second.pads.pop_back();
        engine_.setMidiTrackRackPadMix(track.midiId, index, {});
        return std::nullopt;
    }
    dirty_ = true;
    return index;
}

std::optional<std::size_t> Studio::addLayer(const TrackRef& track, const juce::String& instrumentKey, juce::String& error)
{
    return addRackPad(track, 0, instrumentKey, error);
}

bool Studio::setRackPadInstrument(const TrackRef& track, const std::size_t pad, const juce::String& instrumentKey, juce::String& error)
{
    auto ref = track.owner();
    ref.pad = static_cast<int>(pad);
    if (rackPadState(ref) == nullptr) {
        error = ui::Ls("Pad introuvable", "No such pad");
        return false;
    }
    const auto name = applyInstrument(ref, instrumentKey, error);
    if (!name)
        return false;
    auto* state = rackPadState(ref);
    if (state == nullptr)
        return false;
    // Drums play the key they are given; a pitched instrument plays C4.
    state->playAs = instrumentKey == "silex-instrument" || instrumentKey == "kick-instrument" || instrumentKey == "batterie-instrument" ? -1 : 60;
    state->name = *name;
    publishRackPadMix(track.owner(), pad);
    dirty_ = true;
    return true;
}

bool Studio::removeRackPad(const TrackRef& track, const std::size_t pad)
{
    const auto found = track.midi ? midiTracks_.find(track.midiId) : midiTracks_.end();
    if (found == midiTracks_.end() || pad >= found->second.pads.size())
        return false;
    auto& pads = found->second.pads;
    pads.erase(pads.begin() + static_cast<std::ptrdiff_t>(pad));
    // The pads after it move down a place: the engine's follow.
    for (auto index = pad; index < pads.size(); ++index) {
        auto ref = track.owner();
        ref.pad = static_cast<int>(index);
        (void) publishGraph(ref, true);
    }
    engine_.setMidiTrackRackPadMix(track.midiId, pads.size(), {});
    dirty_ = true;
    return true;
}

bool Studio::setRackPadMix(const TrackRef& track, const std::size_t pad, const RackPadMix& mix)
{
    auto ref = track.owner();
    ref.pad = static_cast<int>(pad);
    auto* state = rackPadState(ref);
    if (state == nullptr)
        return false;
    state->note = std::clamp(mix.note, 0, 127);
    if (mix.name.trim().isNotEmpty())
        state->name = mix.name.trim();
    state->playAs = std::clamp(mix.playAs, -1, 127);
    state->gainDb = std::clamp(mix.gainDb, -60.0f, 12.0f);
    state->pan = std::clamp(mix.pan, -1.0f, 1.0f);
    state->muted = mix.muted;
    state->solo = mix.solo;
    state->choke = std::clamp(mix.choke, 0, 8);
    state->keyLow = std::clamp(mix.keyLow, 0, 127);
    state->keyHigh = std::clamp(mix.keyHigh, state->keyLow, 127);
    state->velocityLow = std::clamp(mix.velocityLow, 1, 127);
    state->velocityHigh = std::clamp(mix.velocityHigh, state->velocityLow, 127);
    publishRackPadMix(track.owner(), pad);
    dirty_ = true;
    return true;
}

bool Studio::loadRackPadSample(const TrackRef& track, const int note, const juce::File& file, juce::String& error)
{
    auto pad = rackPadOfNote(track, note);
    if (!pad)
        pad = addRackPad(track, note, "simple-sampler-instrument", error);
    else if (!setRackPadInstrument(track, *pad, "simple-sampler-instrument", error))
        return false;
    if (!pad)
        return false;
    auto ref = track.owner();
    ref.pad = static_cast<int>(*pad);
    if (!loadSamplerFile(file, ref, error))
        return false;
    // At its own pitch: the note it plays is its root.
    auto* state = rackPadState(ref);
    if (const auto card = samplerCardOf(ref); state != nullptr && card != 0) {
        const auto json = state->graph->graph().nodeSamplerJson(card);
        if (const auto* zones = json.getProperty("zones", {}).getArray(); zones != nullptr && !zones->isEmpty())
            state->playAs = static_cast<int>(zones->getReference(0).getProperty("root", 60));
        state->name = file.getFileNameWithoutExtension();
        publishRackPadMix(track.owner(), *pad);
    }
    dirty_ = true;
    return true;
}

float Studio::rackPadPeak(const TrackRef& track, const std::size_t pad) const
{
    return track.midi ? engine_.midiTrackRackPadPeak(track.midiId, pad) : 0.0f;
}

void Studio::fillDefaultRack(const TrackRef& track)
{
    struct Piece {
        int note;
        const char* french;
        const char* english;
        const char* instrument;
        int choke;
    };
    static constexpr Piece pieces[] {
        { 36, "Grosse caisse", "Kick", "kick-instrument", 0 },
        { 38, "Caisse claire", "Snare", "silex-instrument", 0 },
        { 39, "Clap", "Clap", "silex-instrument", 0 },
        { 42, "Charleston fermé", "Closed Hat", "silex-instrument", 1 },
        { 46, "Charleston ouvert", "Open Hat", "silex-instrument", 1 },
    };
    for (const auto& piece : pieces) {
        juce::String error;
        if (const auto pad = addRackPad(track, piece.note, piece.instrument, error)) {
            RackPadMix mix;
            mix.note = piece.note;
            // Inside a module: English.
            mix.name = piece.english;
            mix.playAs = -1;
            mix.choke = piece.choke;
            (void) setRackPadMix(track, *pad, mix);
        }
    }
}

void Studio::fillDefaultLayers(const TrackRef& track)
{
    // A layer: an electric piano over a pad; a selector: three synths to
    // choose from.
    const auto selector = containerKind(track) == engine::instrument_container::Kind::selector;
    const auto keys = selector ? std::vector<juce::String> { "opale-instrument", "jade-instrument", "ambre-instrument" }
                               : std::vector<juce::String> { "jade-instrument", "nebuleuse-instrument" };
    for (const auto& key : keys) {
        juce::String error;
        if (const auto layer = addLayer(track, key, error); layer && !selector && key == "nebuleuse-instrument") {
            // The pad under the piano.
            if (const auto* state = rackPad(track, *layer)) {
                auto mix = mixOf(*state);
                mix.gainDb = -6.0f;
                (void) setRackPadMix(track, *layer, mix);
            }
        }
    }
}

juce::String Studio::padsKindName(const TrackRef& track) const
{
    const auto found = track.midi ? midiTracks_.find(track.midiId) : midiTracks_.end();
    if (found == midiTracks_.end())
        return {};
    switch (found->second.padsKind) {
    case engine::instrument_container::Kind::layer: return "layer";
    case engine::instrument_container::Kind::selector: return "selector";
    default: return "drum";
    }
}

juce::var Studio::rackPadsState(const TrackRef& track) const
{
    juce::Array<juce::var> pads;
    for (std::size_t index = 0; index < rackPadCount(track); ++index) {
        const auto* pad = rackPad(track, index);
        auto* item = new juce::DynamicObject();
        item->setProperty("note", pad->note);
        item->setProperty("name", pad->name);
        item->setProperty("playAs", pad->playAs);
        item->setProperty("gainDb", pad->gainDb);
        item->setProperty("pan", pad->pan);
        item->setProperty("muted", pad->muted);
        item->setProperty("solo", pad->solo);
        item->setProperty("choke", pad->choke);
        item->setProperty("keyLow", pad->keyLow);
        item->setProperty("keyHigh", pad->keyHigh);
        item->setProperty("velocityLow", pad->velocityLow);
        item->setProperty("velocityHigh", pad->velocityHigh);
        item->setProperty("graph", withoutSharedModules(pad->graph->serialise()));
        pads.add(juce::var(item));
    }
    return pads;
}

void Studio::restoreRackPads(const TrackRef& track, const juce::var& state, const juce::String& kind)
{
    const auto found = track.midi ? midiTracks_.find(track.midiId) : midiTracks_.end();
    const auto* items = state.getArray();
    if (found == midiTracks_.end() || items == nullptr)
        return;
    // What they were made for: as written, else the track's container, else a drum machine's.
    using Kind = engine::instrument_container::Kind;
    const auto made = kind == "layer" ? Kind::layer : kind == "selector" ? Kind::selector : kind == "drum" ? Kind::drum : containerKind(track);
    // The ones there before go (silenced).
    for (std::size_t pad = items->size(); pad < found->second.pads.size(); ++pad)
        engine_.setMidiTrackRackPadMix(track.midiId, pad, {});
    found->second.padsKind = made != Kind::none ? made : Kind::drum;
    const auto limit = found->second.padsKind == Kind::drum ? app::AudioTrackController::maximumRackPads
                                                            : static_cast<std::size_t>(engine::instrument_container::maximumLayers);
    auto& pads = found->second.pads;
    pads.clear();
    for (const auto& item : *items) {
        if (pads.size() >= limit)
            break;
        RackPadState pad;
        pad.note = std::clamp(static_cast<int>(item.getProperty("note", 36)), 0, 127);
        pad.name = item.getProperty("name", {}).toString();
        pad.playAs = std::clamp(static_cast<int>(item.getProperty("playAs", -1)), -1, 127);
        pad.gainDb = static_cast<float>(static_cast<double>(item.getProperty("gainDb", 0.0)));
        pad.pan = static_cast<float>(static_cast<double>(item.getProperty("pan", 0.0)));
        pad.muted = static_cast<bool>(item.getProperty("muted", false));
        pad.solo = static_cast<bool>(item.getProperty("solo", false));
        pad.choke = std::clamp(static_cast<int>(item.getProperty("choke", 0)), 0, 8);
        pad.keyLow = std::clamp(static_cast<int>(item.getProperty("keyLow", 0)), 0, 127);
        pad.keyHigh = std::clamp(static_cast<int>(item.getProperty("keyHigh", 127)), pad.keyLow, 127);
        pad.velocityLow = std::clamp(static_cast<int>(item.getProperty("velocityLow", 1)), 1, 127);
        pad.velocityHigh = std::clamp(static_cast<int>(item.getProperty("velocityHigh", 127)), pad.velocityLow, 127);
        pad.graph = makeSlot(GraphSlot::Owner::midiTrack);
        const auto graphState = item.getProperty("graph", {});
        if (isGraphState(graphState))
            mergeModules(sharedNodeModules_, graphState);
        pad.graph->restore(withLibrary(graphState, factoryMidiGraph_));
        pads.push_back(std::move(pad));
    }
    for (std::size_t index = 0; index < pads.size(); ++index) {
        auto ref = track.owner();
        ref.pad = static_cast<int>(index);
        (void) publishGraph(ref, true);
    }
}

} // namespace cydaw::v2
