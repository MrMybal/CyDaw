// The control port's commands, one function per verb, on the message
// thread. What tools/mcp/cydaw_mcp_server.py exposes to an agent: the
// project, the transport, the tracks and their notes, the devices and their
// knobs, a capture of the window, the export of the mix. Every answer is
// {ok: true, result: …} or {ok: false, error: "…"}.
#include "WorkspaceComponent.h"
#include "Localization.h"

#include "VirtualAudioBus.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>

namespace cydaw::app {

namespace {
juce::var object()
{
    return juce::var(new juce::DynamicObject());
}

void set(juce::var& target, const char* name, const juce::var& value)
{
    if (auto* dynamic = target.getDynamicObject())
        dynamic->setProperty(name, value);
}

juce::var ok(const juce::var& result)
{
    auto reply = object();
    set(reply, "ok", true);
    set(reply, "result", result);
    return reply;
}

juce::var fail(const juce::String& message)
{
    auto reply = object();
    set(reply, "ok", false);
    set(reply, "error", message);
    return reply;
}

// Identifiers travel as strings: a 64-bit id does not survive JSON's
// doubles.
juce::var idOf(const std::uint64_t id)
{
    return juce::String(static_cast<juce::int64>(id));
}

juce::var strings(const juce::StringArray& values)
{
    juce::Array<juce::var> out;
    for (const auto& value : values)
        out.add(value);
    return out;
}

juce::File fileFor(const juce::String& path)
{
    return juce::File::isAbsolutePath(path)
               ? juce::File(path)
               : juce::File::getCurrentWorkingDirectory().getChildFile(path);
}

juce::File templatesFolder()
{
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getSiblingFile("templates");
}

double number(const juce::var& request, const char* name, const double fallback)
{
    const auto value = request.getProperty(name, juce::var());
    return value.isVoid() ? fallback : static_cast<double>(value);
}

struct RemoteTrack {
    bool midi {};
    std::size_t index {};   // audio: the track index; MIDI: its position
    std::uint64_t id {};    // MIDI only
    std::size_t channel {}; // the mixer's and launcher's index
    bool bus {};            // a virtual bus — "master", or "bus:<name|id>"
    std::uint64_t busId {};
};

juce::String colourText(const juce::Colour colour)
{
    return "#" + colour.toDisplayString(false);
}

std::uint32_t colourFrom(const juce::String& text)
{
    auto hex = text.trim().trimCharactersAtStart("#");
    if (hex.length() == 6)
        hex = "ff" + hex;
    return hex.length() == 8 ? static_cast<std::uint32_t>(hex.getHexValue64()) : 0u;
}
} // namespace

void WorkspaceComponent::removeArrangementTrack(const std::size_t trackIndex)
{
    if (trackIndex >= audio_.trackCount() || audio_.isRecording())
        return;
    saveCurrentNodeGraphState();
    if (selectedTrack_ && *selectedTrack_ == trackIndex) {
        selectedTrack_.reset();
        selectedAudioClipId_ = 0;
        selectedAudioClipIds_.clear();
        setEditorGraphOwner(EditorGraphOwner::none, 0);
    }
    if (!audio_.removeAudioTrack(trackIndex))
        return;
    const auto eraseAt = [trackIndex](auto& values) {
        if (trackIndex < values.size())
            values.erase(values.begin() + static_cast<std::ptrdiff_t>(trackIndex));
    };
    eraseAt(trackPresentations_);
    eraseAt(trackNodeGraphStates_);
    eraseAt(trackModulationStates_);
    eraseAt(trackLaneHeights_);
    eraseAt(trackAutomationVisible_);
    const auto remap = [trackIndex](const std::size_t index) {
        return index > trackIndex ? index - 1 : index;
    };
    if (selectedTrack_)
        selectedTrack_ = remap(*selectedTrack_);
    if (launcherEditorContext_) {
        if (launcherEditorContext_->trackIndex == trackIndex)
            launcherEditorContext_.reset();
        else
            launcherEditorContext_->trackIndex = remap(launcherEditorContext_->trackIndex);
    }
    if (launcherInspectorSelection_) {
        if (launcherInspectorSelection_->trackIndex == trackIndex)
            launcherInspectorSelection_.reset();
        else
            launcherInspectorSelection_->trackIndex
                = remap(launcherInspectorSelection_->trackIndex);
    }
    std::erase_if(launcherActions_, [trackIndex](const auto& action) {
        return action.type != LauncherAction::Type::launchScene
               && action.trackIndex == trackIndex;
    });
    for (auto& action : launcherActions_)
        action.trackIndex = remap(action.trackIndex);
    rebuildTrackLanes();
    if (selectedTrack_ && *selectedTrack_ < audio_.trackCount()) {
        selectTrack(*selectedTrack_);
    } else if (audio_.trackCount() > 0) {
        selectTrack(std::min(trackIndex, audio_.trackCount() - 1));
    } else {
        // No audio track left: the editor shows the current MIDI track.
        const auto tracks = audio_.midiTracks();
        if (!tracks.empty()) {
            selectedMidiTrackId_ = tracks.front().id;
            restoreNodeGraphForMidiTrack(selectedMidiTrackId_);
            refreshMidiTrackMixerControls();
        }
        refreshSelectedTrack();
    }
    refreshNodeDeviceSurface();
    refreshClipLauncher();
    refreshTimelineRanges();
    refreshMixer();
    markProjectDirty();
}


juce::var WorkspaceComponent::handleRemoteCommand(const juce::var& request)
{
    const auto command = request.getProperty("cmd", juce::var()).toString();
    const auto text = [&request](const char* name) {
        return request.getProperty(name, juce::var()).toString();
    };
    const auto has = [&request](const char* name) {
        return !request.getProperty(name, juce::var()).isVoid();
    };

    // --- what the tracks look like, for status and for every answer that
    // changes them
    const auto describeTracks = [this] {
        juce::Array<juce::var> audioTracks;
        for (std::size_t index = 0; index < audio_.trackCount(); ++index) {
            const auto info = audio_.trackInfo(index);
            auto track = object();
            set(track, "index", static_cast<int>(index));
            set(track, "kind", "audio");
            set(track, "name", index < trackPresentations_.size()
                                   ? trackPresentations_[index].name
                                   : info.file.getFileNameWithoutExtension());
            set(track, "file", info.file.getFullPathName());
            set(track, "lengthSeconds", info.lengthSeconds);
            set(track, "gainDb", info.gainDb);
            set(track, "pan", info.pan);
            set(track, "muted", info.muted);
            set(track, "solo", info.solo);
            set(track, "armed", info.armed);
            set(track, "colour", index < trackPresentations_.size()
                                     ? colourText(trackPresentations_[index].colour)
                                     : juce::String());
            set(track, "inputBus", idOf(info.inputBusId));
            set(track, "clips", static_cast<int>(info.clips.size()));
            set(track, "devices", strings(deviceNamesForTrack(index, 16)));
            juce::Array<juce::var> plugins;
            for (std::size_t plugin = 0; plugin < info.plugins.size(); ++plugin) {
                auto entry = object();
                set(entry, "index", static_cast<int>(plugin));
                set(entry, "name", info.plugins[plugin].name);
                set(entry, "file", info.plugins[plugin].file.getFullPathName());
                set(entry, "running", info.plugins[plugin].running);
                set(entry, "latencySamples", info.plugins[plugin].latencySamples);
                plugins.add(entry);
            }
            set(track, "plugins", plugins);
            audioTracks.add(track);
        }
        juce::Array<juce::var> midiTracks;
        const auto tracks = audio_.midiTracks();
        for (std::size_t position = 0; position < tracks.size(); ++position) {
            const auto& info = tracks[position];
            auto track = object();
            set(track, "index", static_cast<int>(audio_.trackCount() + position));
            set(track, "kind", "midi");
            set(track, "id", idOf(info.id));
            set(track, "name", info.name);
            set(track, "gainDb", info.gainDb);
            set(track, "pan", info.pan);
            set(track, "muted", info.muted);
            set(track, "solo", info.solo);
            set(track, "armed", info.armed);
            set(track, "devices", strings(deviceNamesForMidiTrack(info.id, 16)));
            juce::Array<juce::var> clips;
            for (const auto& clip : info.clips) {
                auto entry = object();
                set(entry, "id", idOf(clip.id));
                set(entry, "name", clip.name);
                set(entry, "startBeats", clip.startBeats);
                set(entry, "lengthBeats", clip.lengthBeats);
                set(entry, "looping", clip.looping);
                set(entry, "scene", juce::roundToInt(clip.startBeats / 16.0));
                set(entry, "colour", colourText(juce::Colour(clip.colour)));
                set(entry, "notes", static_cast<int>(clip.notes.size()));
                clips.add(entry);
            }
            set(track, "clips", clips);
            midiTracks.add(track);
        }
        auto result = object();
        set(result, "audioTracks", audioTracks);
        set(result, "midiTracks", midiTracks);
        return result;
    };
    const auto status = [this, &describeTracks] {
        auto result = describeTracks();
        set(result, "project", currentProjectFile_.getFullPathName());
        set(result, "dirty", projectDirty_);
        set(result, "tempo", audio_.tempo());
        set(result, "playing", audio_.isPlaying());
        set(result, "position", audio_.position());
        set(result, "length", audio_.length());
        auto loop = object();
        set(loop, "start", audio_.loopStart());
        set(loop, "end", audio_.loopEnd());
        set(loop, "enabled", loopButton_.getToggleState());
        set(result, "loop", loop);
        set(result, "editorMode", currentEditorMode_);
        juce::Array<juce::var> buses;
        for (const auto& bus : audio_.virtualBuses()) {
            auto entry = object();
            set(entry, "id", idOf(bus.id));
            set(entry, "name", bus.name);
            set(entry, "direction", bus.direction == VirtualBusDirection::input
                                        ? "input" : "output");
            set(entry, "channels", bus.channels);
            set(entry, "physicalA", bus.physicalChannelA);
            set(entry, "physicalB", bus.physicalChannelB);
            set(entry, "destinationA", bus.destinationChannelA);
            set(entry, "destinationB", bus.destinationChannelB);
            set(entry, "gainDb", bus.gainDb);
            set(entry, "muted", bus.muted);
            set(entry, "monitor", bus.monitor);
            set(entry, "exposeToSystem", bus.exposeToSystem);
            buses.add(entry);
        }
        set(result, "buses", buses);
        set(result, "defaultInputBus", idOf(audio_.defaultInputBusId()));
        set(result, "launchQuantization", static_cast<int>(audio_.launchQuantization()));
        set(result, "masterGainDb", audio_.masterGainDb());
        set(result, "exporting", audio_.isMasterExporting());
        set(result, "selectedTrack",
            selectedTrack_ ? static_cast<int>(*selectedTrack_)
                           : selectedMidiTrackId_ != 0
                                 ? static_cast<int>(audio_.trackCount())
                                       + [this] {
                                             const auto tracks = audio_.midiTracks();
                                             for (std::size_t i = 0; i < tracks.size(); ++i)
                                                 if (tracks[i].id == selectedMidiTrackId_)
                                                     return static_cast<int>(i);
                                             return 0;
                                         }()
                                 : -1);
        return result;
    };

    // --- a track named by index (audio first, then MIDI) or by name
    const auto resolveTrack = [this](const juce::var& spec) -> std::optional<RemoteTrack> {
        const auto tracks = audio_.midiTracks();
        const auto byIndex = [this, &tracks](const int index) -> std::optional<RemoteTrack> {
            if (index < 0)
                return std::nullopt;
            const auto audioCount = static_cast<int>(audio_.trackCount());
            if (index < audioCount)
                return RemoteTrack { false, static_cast<std::size_t>(index), 0,
                                     static_cast<std::size_t>(index) };
            const auto position = static_cast<std::size_t>(index - audioCount);
            if (position < tracks.size())
                return RemoteTrack { true, position, tracks[position].id,
                                     static_cast<std::size_t>(index) };
            return std::nullopt;
        };
        if (spec.isInt() || spec.isInt64() || spec.isDouble())
            return byIndex(static_cast<int>(spec));
        const auto name = spec.toString().trim();
        if (name.isEmpty())
            return std::nullopt;
        // "master" is the output bus; "bus:<name>" or "bus:<id>" any bus.
        if (name.equalsIgnoreCase("master") || name.startsWithIgnoreCase("bus:")) {
            const auto wanted = name.startsWithIgnoreCase("bus:")
                                    ? name.fromFirstOccurrenceOf(":", false, false).trim()
                                    : juce::String();
            for (const auto& bus : audio_.virtualBuses()) {
                const auto matches = wanted.isEmpty()
                    ? bus.direction == VirtualBusDirection::output
                    : bus.name.equalsIgnoreCase(wanted)
                          || juce::String(static_cast<juce::int64>(bus.id)) == wanted;
                if (matches)
                    return RemoteTrack { false, 0, 0, 0, true, bus.id };
            }
            return std::nullopt;
        }
        if (name.containsOnly("0123456789"))
            return byIndex(name.getIntValue());
        std::optional<RemoteTrack> loose;
        auto looseMatches = 0;
        for (std::size_t index = 0; index < audio_.trackCount(); ++index) {
            const auto candidate = index < trackPresentations_.size()
                                       ? trackPresentations_[index].name
                                       : juce::String();
            if (candidate.equalsIgnoreCase(name))
                return RemoteTrack { false, index, 0, index };
            if (candidate.containsIgnoreCase(name)) {
                loose = RemoteTrack { false, index, 0, index };
                ++looseMatches;
            }
        }
        for (std::size_t position = 0; position < tracks.size(); ++position) {
            const auto channel = audio_.trackCount() + position;
            if (tracks[position].name.equalsIgnoreCase(name))
                return RemoteTrack { true, position, tracks[position].id, channel };
            if (tracks[position].name.containsIgnoreCase(name)) {
                loose = RemoteTrack { true, position, tracks[position].id, channel };
                ++looseMatches;
            }
        }
        return looseMatches == 1 ? loose : std::nullopt;
    };
    const auto trackOrFail = [&](std::optional<RemoteTrack>& track) -> juce::var {
        track = resolveTrack(request.getProperty("track", juce::var()));
        if (!track)
            return fail("no track matches \"" + text("track")
                        + "\"; status lists them by index and name, and"
                        " \"master\" or \"bus:<name>\" name a bus");
        return {};
    };
    // Puts a track's graph in front of the editor, as selecting it does.
    const auto showTrackGraph = [this](const RemoteTrack& track) {
        if (track.bus) {
            saveCurrentNodeGraphState();
            selectedTrack_.reset();
            selectedVirtualBusGraphId_ = track.busId;
            restoreNodeGraphForVirtualBus(track.busId);
            refreshNodeDeviceSurface();
            return;
        }
        if (track.midi) {
            selectedTrack_.reset();
            selectedMidiTrackId_ = track.id;
            restoreNodeGraphForMidiTrack(track.id);
            refreshMidiTrackMixerControls();
        } else {
            selectTrack(track.index);
        }
        refreshNodeDeviceSurface();
    };
    // Stores and publishes the editor's graph as the track's, after an edit.
    const auto commitGraph = [this](const RemoteTrack& track) {
        if (track.bus) {
            saveCurrentNodeGraphState();
            publishNativeDspParameters();
            const auto graph = nodeGraph_.nativeDspGraph();
            (void) audio_.setVirtualBusGraph(track.busId, graph);
            virtualBusManager_.refresh();
        } else if (track.midi) {
            setMidiTrackNodeGraphState(track.id, nodeGraph_.serialise());
        } else {
            saveCurrentNodeGraphState();
            publishNativeDspGraph();
        }
        refreshNodeDeviceSurface();
        refreshClipLauncher();
        refreshMixer();
        markProjectDirty();
    };
    // The editor's graph, compactly: cards with their published knobs, and
    // the cables between them.
    const auto graphSummary = [this](const RemoteTrack& track) {
        const auto state = nodeGraph_.serialise();
        juce::Array<juce::var> nodes;
        if (const auto* list = state.getProperty("nodes", juce::var()).getArray())
            for (const auto& node : *list) {
                auto entry = object();
                set(entry, "id", static_cast<int>(static_cast<juce::int64>(
                                     node.getProperty("id", 0))));
                set(entry, "type", node.getProperty("type", juce::var()).toString());
                set(entry, "title", node.getProperty("title", juce::var()).toString());
                juce::Array<juce::var> parameters;
                if (const auto* published = node.getProperty("interface", juce::var())
                                                .getProperty("parameters", juce::var())
                                                .getArray())
                    for (const auto& parameter : *published) {
                        auto knob = object();
                        set(knob, "key", parameter.getProperty("key", juce::var()).toString());
                        set(knob, "label", parameter.getProperty("label", juce::var()).toString());
                        set(knob, "value", parameter.getProperty("value", juce::var()));
                        parameters.add(knob);
                    }
                set(entry, "parameters", parameters);
                nodes.add(entry);
            }
        juce::Array<juce::var> connections;
        if (const auto* list = state.getProperty("connections", juce::var()).getArray())
            for (const auto& connection : *list) {
                auto entry = object();
                set(entry, "source", static_cast<int>(connection.getProperty("source", 0)));
                set(entry, "destination",
                    static_cast<int>(connection.getProperty("destination", 0)));
                set(entry, "kind", static_cast<int>(connection.getProperty("kind", 0)));
                connections.add(entry);
            }
        auto result = object();
        set(result, "nodes", nodes);
        set(result, "connections", connections);
        if (track.bus) {
            juce::StringArray names;
            for (const auto& node : nodes)
                names.add(node.getProperty("type", juce::var()).toString());
            set(result, "devices", strings(names));
        } else {
            set(result, "devices", strings(track.midi
                                               ? deviceNamesForMidiTrack(track.id, 32)
                                               : deviceNamesForTrack(track.index, 32)));
        }
        return result;
    };
    // The published key of a knob on the last card of a type.
    const auto knobKey = [this](const juce::String& typeKey, const juce::String& suffix) {
        const auto id = nodeGraph_.lastNodeIdOfType(typeKey);
        return id == 0 ? juce::String()
                       : "node." + juce::String(static_cast<juce::int64>(id)) + "." + suffix;
    };
    const auto midiInfo = [this](const std::uint64_t id)
        -> std::optional<AudioTrackController::MidiTrackInfo> {
        const auto tracks = audio_.midiTracks();
        const auto found = std::find_if(tracks.begin(), tracks.end(),
                                        [id](const auto& t) { return t.id == id; });
        return found != tracks.end() ? std::optional(*found) : std::nullopt;
    };
    const auto midiClipNamed = [](const AudioTrackController::MidiTrackInfo& info,
                                  const juce::String& clipName)
        -> std::optional<AudioTrackController::MidiClip> {
        if (info.clips.empty())
            return std::nullopt;
        if (clipName.isEmpty())
            return info.clips.front();
        for (const auto& clip : info.clips)
            if (clip.name.equalsIgnoreCase(clipName))
                return clip;
        return std::nullopt;
    };
    const auto describeAudioClips = [this](const std::size_t index) {
        juce::Array<juce::var> clips;
        for (const auto& clip : audio_.trackInfo(index).clips) {
            auto entry = object();
            set(entry, "id", idOf(clip.id));
            set(entry, "name", clip.name);
            set(entry, "start", clip.startSeconds);
            set(entry, "duration", clip.durationSeconds);
            set(entry, "offset", clip.sourceOffsetSeconds);
            set(entry, "sourceLength", clip.sourceLengthSeconds);
            set(entry, "fadeIn", clip.fadeInSeconds);
            set(entry, "fadeOut", clip.fadeOutSeconds);
            set(entry, "looping", clip.looping);
            clips.add(entry);
        }
        auto result = object();
        set(result, "clips", clips);
        return result;
    };
    const auto describeAutomation = [this](const RemoteTrack& track) {
        const auto points = track.midi ? audio_.midiAutomationPoints(track.id)
                                       : audio_.automationPoints(track.index);
        juce::Array<juce::var> entries;
        for (const auto& point : points) {
            auto entry = object();
            set(entry, "id", idOf(point.id));
            set(entry, "time", point.timeSeconds);
            set(entry, "valueDb", point.valueDb);
            entries.add(entry);
        }
        auto result = object();
        set(result, "points", entries);
        return result;
    };
    const auto refreshAfterArrangementEdit = [this] {
        refreshArrangementView();
        refreshTimelineRanges();
        refreshClipLauncher();
        markProjectDirty();
    };

    if (command == "ping") {
        auto result = object();
        set(result, "app", "CyDAW");
        set(result, "version", JUCE_APPLICATION_VERSION_STRING);
        return ok(result);
    }
    if (command == "status")
        return ok(status());

    if (command == "open_project") {
        const auto file = fileFor(text("path"));
        if (!file.existsAsFile() || !file.hasFileExtension("cydaw"))
            return fail("no .cydaw project at " + file.getFullPathName());
        loadProject(file);
        return ok(status());
    }
    if (command == "save_project") {
        auto file = has("path") ? fileFor(text("path")) : currentProjectFile_;
        if (file == juce::File {})
            return fail("the project has no file yet; give a path");
        file = file.withFileExtension("cydaw");
        saveProject(file);
        if (!file.existsAsFile())
            return fail("the project was not written to " + file.getFullPathName());
        auto result = object();
        set(result, "path", file.getFullPathName());
        return ok(result);
    }
    if (command == "list_templates" || command == "load_template") {
        juce::Array<juce::File> files;
        templatesFolder().findChildFiles(files, juce::File::findFiles, false,
                                         "*.cydaw");
        files.sort();
        if (command == "list_templates") {
            juce::Array<juce::var> entries;
            for (const auto& file : files) {
                auto entry = object();
                set(entry, "name", file.getFileNameWithoutExtension());
                set(entry, "path", file.getFullPathName());
                entries.add(entry);
            }
            auto result = object();
            set(result, "folder", templatesFolder().getFullPathName());
            set(result, "templates", entries);
            return ok(result);
        }
        const auto name = text("name").trim();
        juce::File chosen;
        auto looseMatches = 0;
        for (const auto& file : files) {
            const auto candidate = file.getFileNameWithoutExtension();
            if (candidate.equalsIgnoreCase(name)) {
                chosen = file;
                looseMatches = 1;
                break;
            }
            if (candidate.containsIgnoreCase(name)) {
                chosen = file;
                ++looseMatches;
            }
        }
        if (looseMatches != 1 || chosen == juce::File {})
            return fail("no single template matches \"" + name
                        + "\"; list_templates gives their names");
        loadProject(chosen);
        return ok(status());
    }

    if (command == "transport") {
        if (audio_.isMasterExporting())
            return fail("an export is running");
        if (has("position"))
            audio_.setPosition(std::max(0.0, number(request, "position", 0.0)));
        const auto action = text("action");
        if (action == "play" || (action == "toggle" && !audio_.isPlaying())) {
            if (!audio_.isPlaying())
                audio_.play();
        } else if (action == "stop" || action == "toggle") {
            audio_.stop();
        } else if (action == "rewind") {
            audio_.setPosition(0.0);
        } else if (action.isNotEmpty()) {
            return fail("unknown transport action \"" + action
                        + "\": play, stop, toggle, rewind");
        }
        refreshClipLauncher();
        auto result = object();
        set(result, "playing", audio_.isPlaying());
        set(result, "position", audio_.position());
        return ok(result);
    }
    if (command == "set_tempo") {
        const auto bpm = juce::jlimit(20.0, 300.0, number(request, "bpm", audio_.tempo()));
        audio_.setTempo(bpm);
        tempoSlider_.setValue(bpm, juce::dontSendNotification);
        pianoRoll_.setTempo(bpm);
        refreshTimelineRanges();
        markProjectDirty();
        auto result = object();
        set(result, "tempo", audio_.tempo());
        return ok(result);
    }
    if (command == "set_loop") {
        if (has("start") || has("end")) {
            const auto start = std::max(0.0, number(request, "start", audio_.loopStart()));
            const auto end = std::max(start + 0.01, number(request, "end", audio_.loopEnd()));
            audio_.setLoopRange(start, end);
        }
        if (has("enabled")) {
            const auto enabled = static_cast<bool>(request.getProperty("enabled", false));
            loopButton_.setToggleState(enabled, juce::dontSendNotification);
            audio_.setLooping(enabled);
        }
        refreshTimelineRanges();
        markProjectDirty();
        auto result = object();
        set(result, "start", audio_.loopStart());
        set(result, "end", audio_.loopEnd());
        set(result, "enabled", loopButton_.getToggleState());
        return ok(result);
    }
    if (command == "set_master_gain") {
        const auto gainDb = juce::jlimit(-60.0, 12.0, number(request, "gainDb", 0.0));
        audio_.setMasterGain(static_cast<float>(gainDb));
        refreshMixer();
        markProjectDirty();
        auto result = object();
        set(result, "masterGainDb", audio_.masterGainDb());
        return ok(result);
    }

    if (command == "add_midi_track") {
        if (audio_.isMidiRecording())
            return fail("a MIDI recording is running");
        saveCurrentNodeGraphState();
        auto name = text("name").trim();
        if (name.isEmpty())
            name = tr("midi.instrument") + " "
                   + juce::String(static_cast<int>(audio_.midiTracks().size() + 1));
        const auto id = audio_.addMidiTrack(name);
        if (id == 0)
            return fail("the MIDI track could not be added");
        for (const auto& track : audio_.midiTracks())
            if (track.id == id)
                for (const auto& clip : track.clips)
                    (void) audio_.removeMidiClip(id, clip.id);
        selectedMidiClipId_ = 0;
        selectedTrack_.reset();
        selectedMidiTrackId_ = id;
        (void) audio_.setLiveMidiTrack(id);
        restoreNodeGraphForMidiTrack(id);
        refreshMidiTrackMixerControls();
        refreshNodeDeviceSurface();
        refreshClipLauncher();
        refreshTimelineRanges();
        refreshMixer();
        markProjectDirty();
        auto result = object();
        set(result, "id", idOf(id));
        set(result, "index", static_cast<int>(audio_.trackCount()
                                              + audio_.midiTracks().size() - 1));
        set(result, "name", name);
        return ok(result);
    }
    if (command == "add_audio_track") {
        const auto file = fileFor(text("file"));
        if (!file.existsAsFile())
            return fail("no audio file at " + file.getFullPathName());
        const auto before = audio_.trackCount();
        loadAudioFile(file);
        if (audio_.trackCount() <= before)
            return fail("the file could not be opened as audio");
        auto result = object();
        set(result, "index", static_cast<int>(audio_.trackCount() - 1));
        set(result, "name", audio_.trackCount() - 1 < trackPresentations_.size()
                                ? trackPresentations_[audio_.trackCount() - 1].name
                                : file.getFileNameWithoutExtension());
        return ok(result);
    }
    if (command == "set_track") {
        std::optional<RemoteTrack> track;
        if (auto error = trackOrFail(track); !error.isVoid())
            return error;
        float gainDb {};
        float pan {};
        bool muted {};
        bool solo {};
        if (track->midi) {
            const auto tracks = audio_.midiTracks();
            gainDb = tracks[track->index].gainDb;
            pan = tracks[track->index].pan;
            muted = tracks[track->index].muted;
            solo = tracks[track->index].solo;
        } else {
            const auto info = audio_.trackInfo(track->index);
            gainDb = info.gainDb;
            pan = info.pan;
            muted = info.muted;
            solo = info.solo;
        }
        if (has("gainDb") || has("pan"))
            applyChannelMix(track->channel,
                            static_cast<float>(juce::jlimit(-60.0, 12.0, number(request, "gainDb", gainDb))),
                            static_cast<float>(juce::jlimit(-1.0, 1.0, number(request, "pan", pan))));
        if (has("muted"))
            applyChannelMute(track->channel,
                             static_cast<bool>(request.getProperty("muted", muted)));
        if (has("solo"))
            applyChannelSolo(track->channel,
                             static_cast<bool>(request.getProperty("solo", solo)));
        if (has("name")) {
            const auto name = text("name").trim();
            if (track->midi) {
                if (!audio_.setMidiTrackName(track->id, name))
                    return fail("the MIDI track could not be renamed");
                if (!selectedTrack_ && selectedMidiTrackId_ == track->id)
                    updateTrackDescription();
            } else if (name.isNotEmpty() && track->index < trackPresentations_.size()) {
                trackPresentations_[track->index].name = name;
                if (track->index < trackLanes_.size())
                    trackLanes_[track->index]->setPresentation(
                        name, trackPresentations_[track->index].colour);
            }
        }
        if (has("colour") && !track->midi && track->index < trackPresentations_.size()) {
            if (const auto colour = colourFrom(text("colour")); colour != 0) {
                trackPresentations_[track->index].colour = juce::Colour(colour);
                if (track->index < trackLanes_.size())
                    trackLanes_[track->index]->setPresentation(
                        trackPresentations_[track->index].name, juce::Colour(colour));
            }
        }
        if (has("inputBus") && !track->midi) {
            const auto spec = text("inputBus").trim();
            std::uint64_t busId = 0;
            for (const auto& bus : audio_.virtualBuses())
                if (bus.name.equalsIgnoreCase(spec)
                    || juce::String(static_cast<juce::int64>(bus.id)) == spec)
                    busId = bus.id;
            if (busId == 0 || !audio_.setTrackInputBus(track->index, busId))
                return fail("no input bus \"" + spec + "\"; status lists the buses");
        }
        if (!track->midi && selectedTrack_ && *selectedTrack_ == track->index)
            refreshSelectedTrack();
        if (track->midi && !selectedTrack_ && selectedMidiTrackId_ == track->id)
            refreshMidiTrackMixerControls();
        refreshMixer();
        refreshClipLauncher();
        markProjectDirty();
        return ok(describeTracks());
    }

    if (command == "add_notes" || command == "list_notes" || command == "clear_notes") {
        std::optional<RemoteTrack> track;
        if (auto error = trackOrFail(track); !error.isVoid())
            return error;
        if (!track->midi)
            return fail("\"" + text("track") + "\" is an audio track; notes go on MIDI tracks");
        const auto findTrack = [this, id = track->id] {
            const auto tracks = audio_.midiTracks();
            const auto found = std::find_if(tracks.begin(), tracks.end(),
                                            [id](const auto& t) { return t.id == id; });
            return found != tracks.end() ? std::optional(*found) : std::nullopt;
        };
        const auto clipName = text("clip").trim();
        const auto findClip = [&clipName](const AudioTrackController::MidiTrackInfo& info)
            -> std::optional<AudioTrackController::MidiClip> {
            if (info.clips.empty())
                return std::nullopt;
            if (clipName.isEmpty())
                return info.clips.front();
            for (const auto& clip : info.clips)
                if (clip.name.equalsIgnoreCase(clipName))
                    return clip;
            return std::nullopt;
        };
        if (command == "list_notes") {
            const auto info = findTrack();
            if (!info)
                return fail("the track is gone");
            juce::Array<juce::var> clips;
            for (const auto& clip : info->clips) {
                if (clipName.isNotEmpty() && !clip.name.equalsIgnoreCase(clipName))
                    continue;
                juce::Array<juce::var> notes;
                for (const auto& note : clip.notes) {
                    auto entry = object();
                    set(entry, "id", idOf(note.id));
                    set(entry, "note", note.noteNumber);
                    set(entry, "start", note.startBeats);
                    set(entry, "length", note.durationBeats);
                    set(entry, "velocity", note.velocity);
                    notes.add(entry);
                }
                auto entry = object();
                set(entry, "id", idOf(clip.id));
                set(entry, "name", clip.name);
                set(entry, "startBeats", clip.startBeats);
                set(entry, "lengthBeats", clip.lengthBeats);
                set(entry, "notes", notes);
                clips.add(entry);
            }
            auto result = object();
            set(result, "track", info->name);
            set(result, "clips", clips);
            return ok(result);
        }
        if (command == "clear_notes") {
            const auto info = findTrack();
            if (!info)
                return fail("the track is gone");
            auto cleared = 0;
            for (const auto& clip : info->clips)
                if (clipName.isEmpty() || clip.name.equalsIgnoreCase(clipName)) {
                    cleared += static_cast<int>(clip.notes.size());
                    audio_.clearMidiNotes(track->id, clip.id);
                }
            refreshMidiEditor();
            refreshClipLauncher();
            markProjectDirty();
            auto result = object();
            set(result, "cleared", cleared);
            return ok(result);
        }
        const auto* notes = request.getProperty("notes", juce::var()).getArray();
        if (notes == nullptr || notes->isEmpty())
            return fail("notes must be a non-empty array of {note, start, length, velocity}");
        auto info = findTrack();
        if (!info)
            return fail("the track is gone");
        auto clip = findClip(*info);
        if (!clip) {
            auto lastEnd = 0.0;
            for (const auto& note : *notes)
                lastEnd = std::max(lastEnd, number(note, "start", 0.0)
                                                + number(note, "length", 1.0));
            const auto lengthBeats = number(request, "lengthBeats",
                                            std::max(4.0, std::ceil(lastEnd / 4.0) * 4.0));
            const auto clipId = audio_.addMidiClip(
                track->id, clipName.isNotEmpty() ? clipName : tr("midi.clip") + " 1",
                0.0, lengthBeats);
            if (clipId == 0)
                return fail("the clip could not be created");
            info = findTrack();
            if (!info)
                return fail("the track is gone");
            for (const auto& candidate : info->clips)
                if (candidate.id == clipId)
                    clip = candidate;
        }
        if (!clip)
            return fail("no clip named \"" + clipName + "\" on that track");
        auto added = 0;
        for (const auto& note : *notes) {
            const auto number_ = static_cast<int>(number(note, "note", -1.0));
            if (number_ < 0 || number_ > 127)
                continue;
            if (audio_.addMidiNote(track->id, clip->id, number_,
                                   std::max(0.0, number(note, "start", 0.0)),
                                   std::max(0.01, number(note, "length", 1.0)),
                                   static_cast<float>(juce::jlimit(0.01, 1.0, number(note, "velocity", 0.8))))
                != 0)
                ++added;
        }
        if (selectedMidiTrackId_ == track->id)
            selectedMidiClipId_ = clip->id;
        refreshMidiEditor();
        refreshClipLauncher();
        refreshTimelineRanges();
        markProjectDirty();
        auto total = 0;
        if (const auto after = findTrack())
            for (const auto& candidate : after->clips)
                if (candidate.id == clip->id)
                    total = static_cast<int>(candidate.notes.size());
        auto result = object();
        set(result, "clip", clip->name);
        set(result, "clipId", idOf(clip->id));
        set(result, "added", added);
        set(result, "notesInClip", total);
        return ok(result);
    }

    if (command == "list_devices") {
        juce::Array<juce::var> entries;
        for (const auto& device : nodeGraph_.deviceCatalogue()) {
            auto entry = object();
            set(entry, "key", device.key);
            set(entry, "name", device.name);
            set(entry, "kind", device.kind);
            set(entry, "category", device.category);
            set(entry, "description", device.description);
            set(entry, "instrument", device.soundSource);
            set(entry, "input", device.input);
            set(entry, "output", device.output);
            entries.add(entry);
        }
        auto result = object();
        set(result, "devices", entries);
        set(result, "hint", "preset: replaces the track's graph (instruments go on MIDI"
                            " tracks); module and node: inserted before the output");
        return ok(result);
    }
    if (command == "insert_device") {
        std::optional<RemoteTrack> track;
        if (auto error = trackOrFail(track); !error.isVoid())
            return error;
        const auto key = text("key").trim();
        const auto catalogue = nodeGraph_.deviceCatalogue();
        const auto device = std::find_if(catalogue.begin(), catalogue.end(),
                                         [&key](const auto& entry) { return entry.key == key; });
        if (device == catalogue.end())
            return fail("no device with key \"" + key + "\"; list_devices gives them");
        if (!track->midi && device->soundSource)
            return fail("\"" + key + "\" is an instrument; put it on a MIDI track");
        if (track->bus && device->kind == "preset" && device->input != "audio")
            return fail("\"" + key + "\" does not process audio; a bus takes effects");
        showTrackGraph(*track);
        // A preset is the whole graph; a card with an input is spliced
        // before the output; a source — an LFO, an oscillator — is placed
        // free, to be cabled by modulate.
        auto applied = device->kind == "preset" ? nodeGraph_.insertFactoryDevice(key)
                       : device->input == "none" ? nodeGraph_.insertNode(key)
                                                 : nodeGraph_.insertNodeBeforeOutput(key);
        if (applied.failed() && device->kind != "preset")
            applied = nodeGraph_.insertFactoryDevice(key);
        if (applied.failed())
            return fail(key + ": " + applied.getErrorMessage());
        if (nodeGraph_.getWidth() <= 0 || nodeGraph_.getHeight() <= 0)
            nodeGraph_.setBounds(0, 0, 1'300, 780);
        nodeGraph_.arrangeNodes();
        nodeGraph_.frameGraph();
        commitGraph(*track);
        return ok(graphSummary(*track));
    }
    if (command == "list_parameters" || command == "set_parameter") {
        std::optional<RemoteTrack> track;
        if (auto error = trackOrFail(track); !error.isVoid())
            return error;
        showTrackGraph(*track);
        if (command == "set_parameter") {
            const auto key = text("key").trim();
            if (!has("value"))
                return fail("value is required");
            if (!nodeGraph_.setGraphInterfaceParameter(key, number(request, "value", 0.0)))
                return fail("no published parameter \"" + key
                            + "\" on that track; list_parameters gives the keys");
            publishNativeDspParameters();
            refreshNodeDeviceSurface();
            markProjectDirty();
        }
        juce::Array<juce::var> entries;
        for (const auto& parameter : nodeGraph_.graphInterfaceState().parameters) {
            auto entry = object();
            set(entry, "key", parameter.key);
            set(entry, "label", parameter.label);
            set(entry, "value", parameter.value);
            set(entry, "minimum", parameter.minimum);
            set(entry, "maximum", parameter.maximum);
            set(entry, "unit", parameter.unit);
            entries.add(entry);
        }
        auto result = object();
        set(result, "parameters", entries);
        return ok(result);
    }

    if (command == "set_view") {
        const auto mode = static_cast<int>(number(request, "mode", 1.0));
        if (mode < 1 || mode > 10)
            return fail("mode is 1 arrangement, 2 clip, 4 launcher, 7 graph, 8 bus, 9 mixer, 10 samples");
        setEditorMode(mode);
        auto result = object();
        set(result, "editorMode", currentEditorMode_);
        return ok(result);
    }
    if (command == "snapshot") {
        const auto file = fileFor(text("path")).withFileExtension("png");
        if (!saveVisualSnapshot(file))
            return fail("the capture could not be written to " + file.getFullPathName());
        auto result = object();
        set(result, "path", file.getFullPathName());
        set(result, "width", getWidth());
        set(result, "height", getHeight());
        return ok(result);
    }
    if (command == "export_mix") {
        if (audio_.isMasterExporting())
            return fail("an export is already running");
        const auto file = fileFor(text("path")).withFileExtension("wav");
        if (!file.getParentDirectory().createDirectory())
            return fail("no folder for " + file.getFullPathName());
        startMasterExport(file);
        auto result = object();
        set(result, "path", file.getFullPathName());
        set(result, "started", audio_.isMasterExporting());
        return ok(result);
    }
    if (command == "quit") {
        // The answer goes out first; the window closes right after.
        juce::Timer::callAfterDelay(300, [] {
            if (auto* application = juce::JUCEApplication::getInstance())
                application->systemRequestedQuit();
        });
        auto result = object();
        set(result, "quitting", true);
        return ok(result);
    }
    if (command == "import_project") {
        const auto file = fileFor(text("file"));
        if (!file.existsAsFile())
            return fail("no project file at " + file.getFullPathName());
        juce::var report;
        if (const auto imported = importExternalProject(file, report); imported.failed())
            return fail(imported.getErrorMessage());
        return ok(report);
    }
    if (command == "new_project") {
        startEmptyProject(true);
        return ok(status());
    }
    if (command == "remove_track") {
        std::optional<RemoteTrack> track;
        if (auto error = trackOrFail(track); !error.isVoid())
            return error;
        if (track->bus)
            return fail("remove_bus removes a bus");
        if (!track->midi) {
            if (audio_.isRecording())
                return fail("a recording is running");
            removeArrangementTrack(track->index);
            return ok(describeTracks());
        }
        if (audio_.isMidiRecording())
            return fail("a MIDI recording is running");
        const auto shown = !selectedTrack_ && selectedMidiTrackId_ == track->id;
        if (!audio_.removeMidiTrack(track->id))
            return fail("the track could not be removed");
        midiNodeGraphStates_.erase(track->id);
        if (shown) {
            selectedMidiTrackId_ = 0;
            selectedMidiClipId_ = 0;
            setEditorGraphOwner(EditorGraphOwner::none, 0);
            const auto tracks = audio_.midiTracks();
            if (!tracks.empty()) {
                selectedMidiTrackId_ = tracks.front().id;
                (void) audio_.setLiveMidiTrack(selectedMidiTrackId_);
                restoreNodeGraphForMidiTrack(selectedMidiTrackId_);
            } else {
                nodeGraph_.restore(factoryNodeGraphState_);
                nodeGraph_.setContextName({});
            }
            refreshNodeDeviceSurface();
        }
        if (!audio_.midiTracks().empty())
            refreshMidiEditor();
        refreshClipLauncher();
        refreshMixer();
        refreshTimelineRanges();
        markProjectDirty();
        return ok(describeTracks());
    }
    if (command == "get_track_graph") {
        std::optional<RemoteTrack> track;
        if (auto error = trackOrFail(track); !error.isVoid())
            return error;
        showTrackGraph(*track);
        return ok(graphSummary(*track));
    }
    if (command == "remove_device") {
        std::optional<RemoteTrack> track;
        if (auto error = trackOrFail(track); !error.isVoid())
            return error;
        showTrackGraph(*track);
        const auto nodeId = static_cast<std::uint64_t>(number(request, "nodeId", 0.0));
        const auto removed = nodeId != 0 ? nodeGraph_.removeNodeById(nodeId)
                                         : nodeGraph_.removeNodeByType(text("key").trim());
        if (!removed)
            return fail("no such card on that track; get_track_graph lists them");
        commitGraph(*track);
        return ok(graphSummary(*track));
    }
    if (command == "update_notes" || command == "remove_notes"
        || command == "set_midi_clip") {
        std::optional<RemoteTrack> track;
        if (auto error = trackOrFail(track); !error.isVoid())
            return error;
        if (!track->midi)
            return fail("\"" + text("track") + "\" is an audio track");
        auto info = midiInfo(track->id);
        if (!info)
            return fail("the track is gone");
        const auto clipName = text("clip").trim();
        if (command == "set_midi_clip") {
            const auto clip = midiClipNamed(*info, clipName);
            if (!clip)
                return fail("no clip on that track" + (clipName.isNotEmpty()
                                                          ? " named \"" + clipName + "\""
                                                          : juce::String()));
            const auto looping = has("looping")
                                     ? static_cast<bool>(request.getProperty("looping", false))
                                     : clip->looping;
            auto applied = true;
            if (has("startBeats"))
                applied = audio_.setMidiClipStartBeats(
                    track->id, clip->id, number(request, "startBeats", clip->startBeats));
            if (applied && (has("name") || has("colour")))
                applied = audio_.setMidiClipPresentation(
                    track->id, clip->id, text("name"), colourFrom(text("colour")));
            if (has("lengthBeats")) {
                const auto length = std::max(0.25, number(request, "lengthBeats", clip->lengthBeats));
                applied = audio_.setMidiClipTimelineBounds(
                    track->id, clip->id, length,
                    std::min(clip->playStartBeats, length - 0.25), length,
                    std::min(clip->loopStartBeats, length - 0.25), length, looping);
            } else if (has("looping")) {
                applied = audio_.setMidiClipLooping(track->id, clip->id, looping);
            }
            if (!applied)
                return fail("the clip refused those bounds");
            refreshMidiEditor();
            refreshClipLauncher();
            refreshTimelineRanges();
            markProjectDirty();
            auto result = object();
            if (const auto after = midiInfo(track->id))
                for (const auto& candidate : after->clips)
                    if (candidate.id == clip->id) {
                        set(result, "name", candidate.name);
                        set(result, "startBeats", candidate.startBeats);
                        set(result, "scene", juce::roundToInt(candidate.startBeats / 16.0));
                        set(result, "lengthBeats", candidate.lengthBeats);
                        set(result, "looping", candidate.looping);
                        set(result, "colour", colourText(juce::Colour(candidate.colour)));
                    }
            return ok(result);
        }
        auto touched = 0;
        if (command == "update_notes") {
            const auto* notes = request.getProperty("notes", juce::var()).getArray();
            if (notes == nullptr || notes->isEmpty())
                return fail("notes must be a non-empty array of {id, note, start, length, velocity}");
            for (const auto& change : *notes) {
                const auto noteId = static_cast<std::uint64_t>(
                    change.getProperty("id", juce::var()).toString().getLargeIntValue());
                for (const auto& clip : info->clips)
                    for (const auto& note : clip.notes)
                        if (note.id == noteId
                            && audio_.updateMidiNote(
                                track->id, clip.id, noteId,
                                juce::jlimit(0, 127, static_cast<int>(number(change, "note", note.noteNumber))),
                                std::max(0.0, number(change, "start", note.startBeats)),
                                std::max(0.01, number(change, "length", note.durationBeats)),
                                static_cast<float>(juce::jlimit(0.01, 1.0, number(change, "velocity", note.velocity)))))
                            ++touched;
            }
        } else {
            const auto* ids = request.getProperty("ids", juce::var()).getArray();
            const auto ranged = has("from") || has("to");
            if ((ids == nullptr || ids->isEmpty()) && !ranged)
                return fail("give ids, or a from/to range in beats");
            const auto from = number(request, "from", 0.0);
            const auto to = number(request, "to", 1.0e9);
            for (const auto& clip : info->clips) {
                if (clipName.isNotEmpty() && !clip.name.equalsIgnoreCase(clipName))
                    continue;
                for (const auto& note : clip.notes) {
                    auto remove = false;
                    if (ids != nullptr)
                        for (const auto& id : *ids)
                            if (static_cast<std::uint64_t>(id.toString().getLargeIntValue()) == note.id)
                                remove = true;
                    if (ranged && ids == nullptr && note.startBeats >= from && note.startBeats < to)
                        remove = true;
                    if (remove && audio_.removeMidiNote(track->id, clip.id, note.id))
                        ++touched;
                }
            }
        }
        refreshMidiEditor();
        refreshClipLauncher();
        markProjectDirty();
        auto result = object();
        set(result, command == "update_notes" ? "updated" : "removed", touched);
        return ok(result);
    }
    if (command == "remove_clip") {
        std::optional<RemoteTrack> track;
        if (auto error = trackOrFail(track); !error.isVoid())
            return error;
        if (track->midi) {
            const auto info = midiInfo(track->id);
            const auto clip = info ? midiClipNamed(*info, text("clip").trim()) : std::nullopt;
            if (!clip)
                return fail("no such clip on that track");
            if (!audio_.removeMidiClip(track->id, clip->id))
                return fail("the clip could not be removed");
            if (selectedMidiClipId_ == clip->id)
                selectedMidiClipId_ = 0;
            refreshMidiEditor();
            refreshClipLauncher();
            refreshTimelineRanges();
            markProjectDirty();
            return ok(describeTracks());
        }
        const auto clipId = static_cast<std::uint64_t>(text("clipId").getLargeIntValue());
        if (clipId == 0 || !audio_.removeAudioClip(track->index, clipId))
            return fail("no audio clip with that id; list_audio_clips gives them");
        refreshAfterArrangementEdit();
        return ok(describeAudioClips(track->index));
    }
    if (command == "list_audio_clips" || command == "add_audio_clip"
        || command == "move_audio_clip" || command == "duplicate_audio_clip") {
        std::optional<RemoteTrack> track;
        if (auto error = trackOrFail(track); !error.isVoid())
            return error;
        if (track->midi)
            return fail("\"" + text("track") + "\" is a MIDI track; audio clips go on audio tracks");
        if (command == "list_audio_clips")
            return ok(describeAudioClips(track->index));
        if (command == "add_audio_clip") {
            const auto info = audio_.trackInfo(track->index);
            const auto file = has("file") ? fileFor(text("file")) : juce::File {};
            if (file != juce::File {} && !file.existsAsFile())
                return fail("no audio file at " + file.getFullPathName());
            auto duration = number(request, "duration", 0.0);
            if (duration <= 0.0) {
                if (file != juce::File {}) {
                    const std::unique_ptr<juce::AudioFormatReader> reader(
                        audio_.formatManager().createReaderFor(file));
                    if (reader == nullptr)
                        return fail("the file could not be read as audio");
                    duration = static_cast<double>(reader->lengthInSamples)
                               / std::max(1.0, reader->sampleRate);
                } else {
                    duration = info.sourceLengthSeconds;
                }
            }
            if (duration <= 0.0)
                return fail("give a duration, or a file with some length");
            const auto clipId = audio_.addAudioClip(
                track->index, std::max(0.0, number(request, "start", 0.0)),
                std::max(0.0, number(request, "offset", 0.0)), duration,
                text("name"), 0xffff9b2f, file,
                std::max(0.0, number(request, "fadeIn", 0.0)),
                std::max(0.0, number(request, "fadeOut", 0.0)));
            if (clipId == 0)
                return fail("the clip could not be added");
            refreshAfterArrangementEdit();
            auto result = describeAudioClips(track->index);
            set(result, "clipId", idOf(clipId));
            return ok(result);
        }
        const auto clipId = static_cast<std::uint64_t>(text("clipId").getLargeIntValue());
        if (clipId == 0)
            return fail("clipId is required; list_audio_clips gives them");
        if (command == "move_audio_clip") {
            if (!audio_.moveAudioClip(track->index, clipId,
                                      std::max(0.0, number(request, "start", 0.0))))
                return fail("no audio clip with that id on that track");
            refreshAfterArrangementEdit();
            auto result = describeAudioClips(track->index);
            set(result, "clipId", idOf(clipId));
            set(result, "start", number(request, "start", 0.0));
            return ok(result);
        }
        const auto copy = audio_.duplicateAudioClip(track->index, clipId,
                                                    has("start") ? number(request, "start", 0.0) : -1.0);
        if (copy == 0)
            return fail("no audio clip with that id on that track");
        refreshAfterArrangementEdit();
        auto result = describeAudioClips(track->index);
        set(result, "clipId", idOf(copy));
        return ok(result);
    }
    if (command == "list_automation" || command == "add_automation"
        || command == "clear_automation") {
        std::optional<RemoteTrack> track;
        if (auto error = trackOrFail(track); !error.isVoid())
            return error;
        // A plugin parameter as the target, on an audio track: "plugin"
        // (its index) and "parameter" (index or name).
        std::optional<std::pair<std::size_t, std::uint32_t>> pluginTarget;
        if (has("plugin") && !track->midi) {
            const auto pluginIndex = static_cast<std::size_t>(number(request, "plugin", 0.0));
            std::vector<IsolatedPluginHost::ParameterSnapshot> parameters;
            if (audio_.refreshTrackPluginParameters(track->index, pluginIndex, parameters).failed())
                return fail("no plugin " + juce::String(static_cast<int>(pluginIndex)) + " on that track");
            const auto spec = request.getProperty("parameter", juce::var());
            for (const auto& parameter : parameters)
                if ((spec.isInt() || spec.isDouble() || spec.isInt64())
                        ? parameter.index == static_cast<std::uint32_t>(static_cast<int>(spec))
                        : parameter.name.equalsIgnoreCase(spec.toString().trim()))
                    pluginTarget = { pluginIndex, parameter.index };
            if (!pluginTarget)
                return fail("no parameter \"" + spec.toString() + "\" on that plugin");
        }
        const auto clear = [this, &track, &pluginTarget] {
            if (pluginTarget) {
                audio_.clearPluginAutomation(track->index, pluginTarget->first, pluginTarget->second);
            } else if (track->midi) {
                for (const auto& point : audio_.midiAutomationPoints(track->id))
                    (void) audio_.removeMidiAutomationPoint(track->id, point.id);
            } else {
                audio_.clearAutomation(track->index);
            }
        };
        const auto describePluginAutomation = [this, &track, &pluginTarget] {
            juce::Array<juce::var> entries;
            for (const auto& point : audio_.pluginAutomationPoints(
                     track->index, pluginTarget->first, pluginTarget->second)) {
                auto entry = object();
                set(entry, "id", idOf(point.id));
                set(entry, "time", point.timeSeconds);
                set(entry, "value", point.normalisedValue);
                entries.add(entry);
            }
            auto result = object();
            set(result, "points", entries);
            return result;
        };
        if (command == "clear_automation"
            || (command == "add_automation"
                && static_cast<bool>(request.getProperty("clear", false))))
            clear();
        auto added = 0;
        if (command == "add_automation") {
            const auto* points = request.getProperty("points", juce::var()).getArray();
            if (points == nullptr || points->isEmpty())
                return fail("points must be a non-empty array of {time, valueDb}");
            for (const auto& point : *points) {
                const auto time = std::max(0.0, number(point, "time", 0.0));
                std::uint64_t id = 0;
                if (pluginTarget) {
                    id = audio_.addPluginAutomationPoint(
                        track->index, pluginTarget->first, pluginTarget->second, {}, time,
                        static_cast<float>(juce::jlimit(0.0, 1.0, number(point, "value", 0.0))));
                } else {
                    const auto valueDb = static_cast<float>(
                        juce::jlimit(-60.0, 12.0, number(point, "valueDb", 0.0)));
                    id = track->midi
                             ? audio_.addMidiAutomationPoint(track->id, time, valueDb)
                             : audio_.addAutomationPoint(track->index, time, valueDb);
                }
                if (id != 0)
                    ++added;
            }
        }
        if (command != "list_automation") {
            if (!track->midi) {
                if (track->index < trackAutomationVisible_.size())
                    trackAutomationVisible_[track->index] = 1;
                layoutTrackLanes();
                refreshArrangementView();
            } else {
                refreshMidiEditor();
            }
            markProjectDirty();
        }
        auto result = pluginTarget ? describePluginAutomation() : describeAutomation(*track);
        if (command == "add_automation")
            set(result, "added", added);
        const auto list = result.getProperty("points", juce::var());
        set(result, "points", list.size());
        set(result, "list", list);
        return ok(result);
    }
    if (command == "modulate" || command == "remove_modulation") {
        std::optional<RemoteTrack> track;
        if (auto error = trackOrFail(track); !error.isVoid())
            return error;
        const auto target = text("target").trim();
        const auto parameter = text("parameter").trim();
        if (target.isEmpty() || parameter.isEmpty())
            return fail("target (a card's type key) and parameter (its knob's suffix) are required");
        showTrackGraph(*track);
        if (command == "remove_modulation") {
            if (!nodeGraph_.removeModulationForTest(target, parameter))
                return fail("nothing modulates " + target + "." + parameter);
            commitGraph(*track);
            return ok(graphSummary(*track));
        }
        auto source = text("source").trim();
        if (source.isEmpty())
            source = "lfo-core";
        if (const auto inserted = nodeGraph_.insertNode(source); inserted.failed())
            return fail(source + ": " + inserted.getErrorMessage());
        if (!nodeGraph_.modulateParameterForTest(source, target, parameter)) {
            (void) nodeGraph_.removeNodeByType(source);
            return fail("the cable did not reach " + target + "." + parameter
                        + "; get_track_graph gives the cards and their knobs");
        }
        auto result = object();
        set(result, "modulated", true);
        set(result, "source", source);
        set(result, "target", target);
        set(result, "parameter", parameter);
        if (has("rate") && source == "lfo-core") {
            const auto key = knobKey("lfo-core", "lfo.rate");
            if (key.isNotEmpty()
                && nodeGraph_.setGraphInterfaceParameter(key, number(request, "rate", 1.0)))
                set(result, "rate", number(request, "rate", 1.0));
        }
        if (has("minimum") || has("maximum")) {
            const auto low = knobKey("map-range", "map.outputMinimum");
            const auto high = knobKey("map-range", "map.outputMaximum");
            if (has("minimum") && low.isNotEmpty()
                && nodeGraph_.setGraphInterfaceParameter(low, number(request, "minimum", 0.0)))
                set(result, "minimum", number(request, "minimum", 0.0));
            if (has("maximum") && high.isNotEmpty()
                && nodeGraph_.setGraphInterfaceParameter(high, number(request, "maximum", 1.0)))
                set(result, "maximum", number(request, "maximum", 1.0));
        }
        if (nodeGraph_.getWidth() <= 0 || nodeGraph_.getHeight() <= 0)
            nodeGraph_.setBounds(0, 0, 1'300, 780);
        nodeGraph_.arrangeNodes();
        commitGraph(*track);
        set(result, "graph", graphSummary(*track));
        return ok(result);
    }
    if (command == "launch_scene" || command == "stop_launcher") {
        const auto scene = static_cast<int>(number(request, "scene", 0.0));
        auto launched = false;
        if (command == "launch_scene") {
            if (scene < 0)
                return fail("scene is 0 or more");
            launched = audio_.launchScene(scene);
        } else {
            audio_.stopLauncher();
        }
        for (const auto& midiTrack : audio_.midiTracks()) {
            const auto clip = command == "launch_scene"
                                  ? launcherMidiClip(midiTrack.id, scene)
                                  : std::nullopt;
            if (clip.has_value())
                launched = audio_.startMidiClipPreview(midiTrack.id, clip->id, scene)
                           || launched;
            else
                (void) audio_.stopMidiClipPreview(midiTrack.id);
        }
        if (launched) {
            arrangementPlaybackButton_.setToggleState(false, juce::dontSendNotification);
            refreshArrangementInteractionMode();
        }
        refreshClipLauncher();
        auto result = object();
        set(result, "launched", launched);
        set(result, "playing", audio_.isPlaying());
        return ok(result);
    }
    if (command == "launch_clip" || command == "stop_track") {
        std::optional<RemoteTrack> track;
        if (auto error = trackOrFail(track); !error.isVoid())
            return error;
        auto done = false;
        if (command == "launch_clip") {
            const auto scene = static_cast<int>(number(request, "scene", 0.0));
            if (track->midi) {
                const auto clip = launcherMidiClip(track->id, scene);
                done = clip.has_value()
                       && audio_.startMidiClipPreview(track->id, clip->id, scene);
            } else {
                done = audio_.launchClip(track->index, scene);
            }
            if (done) {
                arrangementPlaybackButton_.setToggleState(false, juce::dontSendNotification);
                refreshArrangementInteractionMode();
            }
        } else {
            done = track->midi ? audio_.stopMidiClipPreview(track->id)
                               : audio_.stopLauncherTrack(track->index, true);
        }
        refreshClipLauncher();
        auto result = object();
        set(result, command == "launch_clip" ? "launched" : "stopped", done);
        return ok(result);
    }
    if (command == "arm") {
        std::optional<RemoteTrack> track;
        if (auto error = trackOrFail(track); !error.isVoid())
            return error;
        const auto armed = static_cast<bool>(request.getProperty("armed", true));
        applyChannelArm(track->channel, armed);
        refreshClipLauncher();
        refreshMixer();
        auto result = object();
        if (track->midi) {
            const auto info = midiInfo(track->id);
            set(result, "armed", info ? info->armed : false);
        } else {
            set(result, "armed", audio_.trackInfo(track->index).armed);
        }
        return ok(result);
    }
    if (command == "record") {
        const auto mode = text("mode");
        const auto action = text("action");
        if (audio_.isMasterExporting())
            return fail("an export is running");
        if (mode == "midi") {
            if (action == "start" && !audio_.isMidiRecording()) {
                if (selectedMidiTrackId_ == 0)
                    return fail("select a MIDI track first (list_parameters or add_notes do)");
                setEditorMode(2);
                toggleMidiRecording();
            } else if (action == "stop" && audio_.isMidiRecording()) {
                toggleMidiRecording();
            }
        } else if (mode == "audio") {
            if (action == "start" && !audio_.hasRecordingSession()) {
                if (!has("file"))
                    return fail("file is required to start an audio recording");
                toggleRecording(fileFor(text("file")).withFileExtension("wav"));
            } else if (action == "stop" && audio_.hasRecordingSession()) {
                finishAudioRecording(true);
            }
        } else {
            return fail("mode is midi or audio");
        }
        auto result = object();
        set(result, "midiRecording", audio_.isMidiRecording());
        set(result, "audioRecording", audio_.isRecording());
        set(result, "recordingSession", audio_.hasRecordingSession());
        return ok(result);
    }
    if (command == "undo" || command == "redo") {
        const auto scope = text("scope");
        const auto undo = command == "undo";
        auto done = true;
        if (scope == "midi") {
            if (undo)
                undoMidiEdit();
            else
                redoMidiEdit();
        } else if (scope == "graph") {
            if (undo)
                nodeGraph_.undoForTest();
            else
                nodeGraph_.redoForTest();
            if (selectedTrack_)
                commitGraph({ false, *selectedTrack_, 0, *selectedTrack_ });
            else if (selectedMidiTrackId_ != 0)
                commitGraph({ true, 0, selectedMidiTrackId_, 0 });
        } else if (scope == "arrangement") {
            done = undo ? audio_.undoArrangementEdit() : audio_.redoArrangementEdit();
            refreshAfterArrangementEdit();
        } else {
            return fail("scope is midi, graph or arrangement");
        }
        auto result = object();
        set(result, "done", done);
        return ok(result);
    }
    if (command == "move_track") {
        std::optional<RemoteTrack> track;
        if (auto error = trackOrFail(track); !error.isVoid())
            return error;
        if (track->bus)
            return fail("buses do not move");
        const auto to = static_cast<int>(number(request, "to", -1.0));
        const auto audioCount = static_cast<int>(audio_.trackCount());
        const auto total = audioCount + static_cast<int>(audio_.midiTracks().size());
        if (to < 0 || to >= total)
            return fail("to is an index from 0 to " + juce::String(total - 1));
        // Audio tracks move among audio tracks, MIDI among MIDI: the
        // combined index keeps audio first.
        if (track->midi != (to >= audioCount))
            return fail(track->midi
                            ? "a MIDI track moves to an index from " + juce::String(audioCount)
                                  + " to " + juce::String(total - 1)
                            : "an audio track moves to an index from 0 to "
                                  + juce::String(audioCount - 1));
        moveLauncherTrack(track->channel, static_cast<std::size_t>(to));
        return ok(describeTracks());
    }
    if (command == "add_midi_clip") {
        std::optional<RemoteTrack> track;
        if (auto error = trackOrFail(track); !error.isVoid())
            return error;
        if (!track->midi)
            return fail("MIDI clips go on MIDI tracks");
        const auto colour = colourFrom(text("colour"));
        const auto clipId = audio_.addMidiClip(
            track->id, text("name").isNotEmpty() ? text("name") : tr("midi.clip"),
            std::max(0.0, number(request, "startBeats", 0.0)),
            std::max(0.25, number(request, "lengthBeats", 16.0)),
            static_cast<bool>(request.getProperty("looping", false)),
            colour != 0 ? colour : 0xff4fd3ae);
        if (clipId == 0)
            return fail("the clip could not be created");
        refreshMidiEditor();
        refreshClipLauncher();
        refreshTimelineRanges();
        markProjectDirty();
        auto result = object();
        set(result, "clipId", idOf(clipId));
        set(result, "scene", juce::roundToInt(number(request, "startBeats", 0.0) / 16.0));
        return ok(result);
    }
    if (command == "describe_node_type") {
        const auto description = nodeGraph_.describeNodeTypeForRemote(text("key").trim());
        if (description.isVoid())
            return fail("no node type \"" + text("key") + "\"; list_devices gives the keys");
        return ok(description);
    }
    if (command == "describe_node" || command == "add_node" || command == "connect"
        || command == "disconnect" || command == "set_node"
        || command == "set_node_parameter" || command == "expose_parameter"
        || command == "add_comment" || command == "remove_comment") {
        std::optional<RemoteTrack> track;
        if (auto error = trackOrFail(track); !error.isVoid())
            return error;
        showTrackGraph(*track);
        const auto nodeId = static_cast<std::uint64_t>(number(request, "nodeId", 0.0));
        if (command == "describe_node") {
            const auto description = nodeGraph_.describeNodeForRemote(nodeId);
            if (description.isVoid())
                return fail("no card with that id; get_track_graph lists them");
            return ok(description);
        }
        if (command == "add_node") {
            const auto position = has("x") && has("y")
                ? juce::Point<float>(static_cast<float>(number(request, "x", 0.5)),
                                     static_cast<float>(number(request, "y", 0.5)))
                : juce::Point<float>(-1'000.0f, -1'000.0f);
            const auto id = nodeGraph_.addNodeForRemote(text("key").trim(), position);
            if (id == 0)
                return fail("no node type \"" + text("key") + "\"; list_devices gives the keys");
            commitGraph(*track);
            auto result = nodeGraph_.describeNodeForRemote(id);
            set(result, "graph", graphSummary(*track));
            return ok(result);
        }
        if (command == "connect" || command == "disconnect") {
            const auto source = static_cast<std::uint64_t>(number(request, "source", 0.0));
            const auto destination = static_cast<std::uint64_t>(number(request, "destination", 0.0));
            if (command == "connect") {
                if (!nodeGraph_.connectNodesForRemote(
                        source, destination,
                        static_cast<std::uint64_t>(number(request, "sourcePort", 0.0)),
                        static_cast<std::uint64_t>(number(request, "destinationPort", 0.0))))
                    return fail("the cable was refused: check the ids with get_track_graph"
                                " and the ports with describe_node");
            } else if (nodeGraph_.disconnectNodesForRemote(source, destination) == 0) {
                return fail("no cable between those cards");
            }
            commitGraph(*track);
            return ok(graphSummary(*track));
        }
        if (command == "set_node") {
            std::optional<juce::Point<float>> position;
            if (has("x") && has("y"))
                position = juce::Point<float>(static_cast<float>(number(request, "x", 0.0)),
                                              static_cast<float>(number(request, "y", 0.0)));
            if (!nodeGraph_.setNodePresentationForRemote(nodeId, text("title"), position))
                return fail("no card with that id");
            commitGraph(*track);
            return ok(nodeGraph_.describeNodeForRemote(nodeId));
        }
        if (command == "set_node_parameter") {
            const auto key = "node." + juce::String(static_cast<juce::int64>(nodeId)) + "."
                             + text("parameter").trim();
            if (!has("value") || !nodeGraph_.setGraphInterfaceParameter(key, number(request, "value", 0.0)))
                return fail("no parameter \"" + text("parameter")
                            + "\" on that card; describe_node_type lists them");
            publishNativeDspParameters();
            refreshNodeDeviceSurface();
            markProjectDirty();
            auto result = object();
            set(result, "key", key);
            set(result, "value", number(request, "value", 0.0));
            return ok(result);
        }
        if (command == "expose_parameter") {
            const auto key = text("key").trim();
            const auto interface = nodeGraph_.graphInterfaceState();
            const auto current = std::find_if(
                interface.parameters.begin(), interface.parameters.end(),
                [&key](const auto& parameter) { return parameter.key == key; });
            if (current == interface.parameters.end())
                return fail("no published parameter \"" + key + "\"");
            const auto device = has("device") ? static_cast<bool>(request.getProperty("device", true))
                                              : current->showInDevice;
            const auto host = has("host") ? static_cast<bool>(request.getProperty("host", true))
                                          : current->publishToHost;
            if (!nodeGraph_.setPublishedParameterExposure(key, device, host))
                return fail("the exposure could not be changed");
            commitGraph(*track);
            auto result = object();
            set(result, "key", key);
            set(result, "device", device);
            set(result, "host", host);
            return ok(result);
        }
        if (command == "add_comment") {
            const auto id = nodeGraph_.addCommentForRemote(
                text("title"), text("text"),
                { static_cast<float>(number(request, "x", 0.1)),
                  static_cast<float>(number(request, "y", 0.1)) },
                { static_cast<float>(number(request, "width", 0.0)),
                  static_cast<float>(number(request, "height", 0.0)) });
            commitGraph(*track);
            auto result = object();
            set(result, "commentId", idOf(id));
            return ok(result);
        }
        const auto commentId = static_cast<std::uint64_t>(
            text("commentId").getLargeIntValue());
        if (!nodeGraph_.removeCommentForRemote(commentId))
            return fail("no comment with that id");
        commitGraph(*track);
        auto result = object();
        set(result, "removed", true);
        return ok(result);
    }
    if (command == "list_plugins") {
        juce::Array<juce::File> folders;
        if (has("folder"))
            folders.add(fileFor(text("folder")));
        folders.add(juce::File::getSpecialLocation(juce::File::commonApplicationDataDirectory)
                        .getSiblingFile("Program Files").getChildFile("Common Files")
                        .getChildFile("VST3"));
        folders.add(juce::File("C:/Program Files/Common Files/VST3"));
        folders.add(juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                        .getChildFile("Programs").getChildFile("Common").getChildFile("VST3"));
        juce::Array<juce::var> entries;
        juce::StringArray seen;
        for (const auto& folder : folders) {
            if (!folder.isDirectory())
                continue;
            juce::Array<juce::File> found;
            folder.findChildFiles(found, juce::File::findFilesAndDirectories, true, "*.vst3");
            for (const auto& plugin : found) {
                if (seen.contains(plugin.getFullPathName()))
                    continue;
                seen.add(plugin.getFullPathName());
                auto entry = object();
                set(entry, "name", plugin.getFileNameWithoutExtension());
                set(entry, "path", plugin.getFullPathName());
                entries.add(entry);
            }
        }
        auto result = object();
        set(result, "plugins", entries);
        return ok(result);
    }
    if (command == "add_plugin" || command == "remove_plugin" || command == "move_plugin"
        || command == "list_plugin_parameters" || command == "set_plugin_parameter"
        || command == "show_plugin_editor") {
        std::optional<RemoteTrack> track;
        if (auto error = trackOrFail(track); !error.isVoid())
            return error;
        if (track->midi || track->bus)
            return fail("plugins sit on audio tracks in this version");
        if (command == "add_plugin") {
            const auto file = fileFor(text("file"));
            if (!file.exists())
                return fail("no plugin at " + file.getFullPathName());
            std::size_t pluginIndex {};
            const auto added = audio_.setTrackPlugin(track->index, file, &pluginIndex);
            if (added.failed())
                return fail(added.getErrorMessage());
            selectedPluginIndex_ = pluginIndex;
            if (selectedTrack_ && *selectedTrack_ == track->index) {
                refreshSelectedTrack();
                refreshAutomationTargets();
            }
            refreshClipLauncher();
            markProjectDirty();
            auto result = object();
            set(result, "pluginIndex", static_cast<int>(pluginIndex));
            set(result, "tracks", describeTracks());
            return ok(result);
        }
        const auto info = audio_.trackInfo(track->index);
        const auto pluginIndex = static_cast<std::size_t>(number(request, "plugin", 0.0));
        if (pluginIndex >= info.plugins.size())
            return fail("no plugin " + juce::String(static_cast<int>(pluginIndex))
                        + " on that track; status lists them");
        if (command == "remove_plugin") {
            audio_.clearTrackPlugin(track->index, pluginIndex);
            if (selectedTrack_ && *selectedTrack_ == track->index) {
                refreshSelectedTrack();
                refreshAutomationTargets();
            }
            refreshClipLauncher();
            markProjectDirty();
            return ok(describeTracks());
        }
        if (command == "move_plugin") {
            const auto direction = number(request, "direction", 1.0) < 0.0 ? -1 : 1;
            if (!audio_.moveTrackPlugin(track->index, pluginIndex, direction))
                return fail("the plugin did not move");
            if (selectedTrack_ && *selectedTrack_ == track->index)
                refreshSelectedTrack();
            markProjectDirty();
            return ok(describeTracks());
        }
        if (command == "show_plugin_editor") {
            const auto shown = audio_.showTrackPluginEditor(track->index, pluginIndex);
            if (shown.failed())
                return fail(shown.getErrorMessage());
            auto result = object();
            set(result, "shown", true);
            return ok(result);
        }
        std::vector<IsolatedPluginHost::ParameterSnapshot> parameters;
        if (const auto refreshed = audio_.refreshTrackPluginParameters(
                track->index, pluginIndex, parameters);
            refreshed.failed())
            return fail(refreshed.getErrorMessage());
        if (command == "set_plugin_parameter") {
            const auto spec = request.getProperty("parameter", juce::var());
            std::optional<std::uint32_t> parameterIndex;
            if (spec.isInt() || spec.isInt64() || spec.isDouble())
                parameterIndex = static_cast<std::uint32_t>(static_cast<int>(spec));
            else
                for (const auto& parameter : parameters)
                    if (parameter.name.equalsIgnoreCase(spec.toString().trim()))
                        parameterIndex = parameter.index;
            if (!parameterIndex)
                return fail("no parameter \"" + spec.toString()
                            + "\"; list_plugin_parameters gives them");
            const auto value = static_cast<float>(juce::jlimit(0.0, 1.0, number(request, "value", 0.0)));
            if (const auto applied = audio_.setTrackPluginParameter(
                    track->index, pluginIndex, *parameterIndex, value);
                applied.failed())
                return fail(applied.getErrorMessage());
            markProjectDirty();
            auto result = object();
            set(result, "parameter", static_cast<int>(*parameterIndex));
            set(result, "value", value);
            return ok(result);
        }
        juce::Array<juce::var> entries;
        for (const auto& parameter : parameters) {
            auto entry = object();
            set(entry, "index", static_cast<int>(parameter.index));
            set(entry, "name", parameter.name);
            set(entry, "label", parameter.label);
            set(entry, "value", parameter.value);
            set(entry, "automatable", parameter.automatable);
            set(entry, "discrete", parameter.discrete);
            set(entry, "boolean", parameter.boolean);
            entries.add(entry);
        }
        auto result = object();
        set(result, "plugin", info.plugins[pluginIndex].name);
        set(result, "parameters", entries);
        return ok(result);
    }
    if (command == "set_audio_clip" || command == "stretch_audio_clip"
        || command == "split_audio_clip" || command == "crossfade_audio_clip"
        || command == "set_audio_clip_bounds" || command == "range_edit") {
        std::optional<RemoteTrack> track;
        if (auto error = trackOrFail(track); !error.isVoid())
            return error;
        if (track->midi || track->bus)
            return fail("\"" + text("track") + "\" is not an audio track");
        if (command == "range_edit") {
            const auto operation = text("operation");
            const auto kind = operation == "split" ? AudioTrackController::ArrangementRangeOperation::splitAtBoundaries
                              : operation == "crop" ? AudioTrackController::ArrangementRangeOperation::cropToRange
                              : operation == "silence" ? AudioTrackController::ArrangementRangeOperation::silenceRange
                              : std::optional<AudioTrackController::ArrangementRangeOperation> {};
            if (!kind)
                return fail("operation is split, crop or silence");
            std::vector<std::uint64_t> clipIds;
            if (const auto* ids = request.getProperty("clipIds", juce::var()).getArray())
                for (const auto& id : *ids)
                    clipIds.push_back(static_cast<std::uint64_t>(id.toString().getLargeIntValue()));
            if (clipIds.empty())
                for (const auto& clip : audio_.trackInfo(track->index).clips)
                    clipIds.push_back(clip.id);
            (void) audio_.editAudioClipRange(track->index, clipIds,
                                             std::max(0.0, number(request, "start", 0.0)),
                                             std::max(0.0, number(request, "end", 0.0)), *kind);
            refreshAfterArrangementEdit();
            return ok(describeAudioClips(track->index));
        }
        const auto clipId = static_cast<std::uint64_t>(text("clipId").getLargeIntValue());
        const auto clip = audio_.audioClip(track->index, clipId);
        if (!clip)
            return fail("no audio clip with that id; list_audio_clips gives them");
        if (command == "split_audio_clip") {
            const auto piece = audio_.splitAudioClip(track->index, clipId,
                                                     number(request, "at", clip->startSeconds));
            if (piece == 0)
                return fail("the clip did not split there");
            refreshAfterArrangementEdit();
            auto result = describeAudioClips(track->index);
            set(result, "newClipId", idOf(piece));
            return ok(result);
        }
        if (command == "crossfade_audio_clip") {
            if (!audio_.autoCrossfadeAudioClip(track->index, clipId))
                return fail("no neighbour to crossfade with");
            refreshAfterArrangementEdit();
            return ok(describeAudioClips(track->index));
        }
        if (command == "stretch_audio_clip") {
            if (!audio_.stretchAudioClip(track->index, clipId,
                                         std::max(0.01, number(request, "duration", clip->durationSeconds))))
                return fail("the clip could not be stretched");
            refreshAfterArrangementEdit();
            return ok(describeAudioClips(track->index));
        }
        if (command == "set_audio_clip_bounds") {
            const auto duration = std::max(0.01, number(request, "duration", clip->durationSeconds));
            if (!audio_.setAudioClipTimelineBounds(
                    track->index, clipId, duration,
                    std::max(0.0, number(request, "playStart", clip->playStartSeconds)),
                    std::max(0.0, number(request, "playEnd", duration)),
                    std::max(0.0, number(request, "loopStart", 0.0)),
                    std::max(0.0, number(request, "loopEnd", duration)),
                    static_cast<bool>(request.getProperty("looping", clip->looping))))
                return fail("the clip refused those bounds");
            refreshAfterArrangementEdit();
            return ok(describeAudioClips(track->index));
        }
        // set_audio_clip
        if (has("start") || has("offset") || has("duration") || has("fadeIn")
            || has("fadeOut") || has("sourceDuration"))
            if (!audio_.updateAudioClip(
                    track->index, clipId,
                    std::max(0.0, number(request, "start", clip->startSeconds)),
                    std::max(0.0, number(request, "offset", clip->sourceOffsetSeconds)),
                    std::max(0.01, number(request, "duration", clip->durationSeconds)),
                    number(request, "fadeIn", -1.0), number(request, "fadeOut", -1.0),
                    number(request, "sourceDuration", -1.0)))
                return fail("the clip refused those values");
        if (has("gainDb") || has("sourceTempo") || has("stretchMode")) {
            const auto modeText = text("stretchMode");
            const auto mode = modeText == "tonal" ? AudioTrackController::StretchMode::tonal
                              : modeText == "beats" ? AudioTrackController::StretchMode::beats
                              : modeText == "texture" ? AudioTrackController::StretchMode::texture
                              : modeText == "repitch" ? AudioTrackController::StretchMode::repitch
                              : clip->stretchMode;
            const auto tempo = number(request, "sourceTempo", clip->sourceTempoBpm);
            if (clip->events.empty()) {
                if (!audio_.setAudioClipStretchSettings(track->index, clipId, tempo, mode))
                    return fail("the stretch settings were refused");
            } else {
                for (const auto& event : clip->events)
                    (void) audio_.setAudioClipEventProperties(
                        track->index, clipId, event.id,
                        static_cast<float>(number(request, "gainDb", event.gainDb)),
                        tempo, mode);
            }
        }
        refreshAfterArrangementEdit();
        return ok(describeAudioClips(track->index));
    }
    if (command == "list_buses") {
        return ok(status().getProperty("buses", juce::var()).isVoid()
                      ? object()
                      : [&] {
                            auto result = object();
                            set(result, "buses", status().getProperty("buses", juce::var()));
                            set(result, "defaultInputBus", idOf(audio_.defaultInputBusId()));
                            return result;
                        }());
    }
    if (command == "add_bus" || command == "set_bus" || command == "remove_bus") {
        const auto find = [this](const juce::String& spec) -> std::optional<VirtualAudioBus> {
            for (const auto& bus : audio_.virtualBuses())
                if (bus.name.equalsIgnoreCase(spec.trim())
                    || juce::String(static_cast<juce::int64>(bus.id)) == spec.trim())
                    return bus;
            return std::nullopt;
        };
        std::optional<VirtualAudioBus> bus;
        if (command == "add_bus") {
            const auto direction = text("direction") == "input"
                                       ? VirtualBusDirection::input
                                       : VirtualBusDirection::output;
            const auto id = audio_.addVirtualBus(direction);
            if (id == 0)
                return fail("no more buses can be added");
            bus = audio_.virtualBus(id);
        } else {
            bus = find(text("bus"));
            if (!bus)
                return fail("no bus \"" + text("bus") + "\"; status lists them");
        }
        if (command == "remove_bus") {
            if (!audio_.removeVirtualBus(bus->id))
                return fail("that bus cannot be removed");
            virtualBusNodeGraphStates_.erase(bus->id);
            if (selectedVirtualBusGraphId_ == bus->id)
                selectedVirtualBusGraphId_ = 0;
        } else {
            auto updated = *bus;
            if (has("name") && text("name").trim().isNotEmpty())
                updated.name = text("name").trim();
            if (has("channels"))
                updated.channels = juce::jlimit(1, 2, static_cast<int>(number(request, "channels", 2.0)));
            if (has("physicalA"))
                updated.physicalChannelA = std::max(0, static_cast<int>(number(request, "physicalA", 0.0)));
            if (has("physicalB"))
                updated.physicalChannelB = std::max(0, static_cast<int>(number(request, "physicalB", 1.0)));
            if (has("destinationA"))
                updated.destinationChannelA = std::max(0, static_cast<int>(number(request, "destinationA", 0.0)));
            if (has("destinationB"))
                updated.destinationChannelB = std::max(0, static_cast<int>(number(request, "destinationB", 1.0)));
            if (has("gainDb"))
                updated.gainDb = static_cast<float>(juce::jlimit(-60.0, 12.0, number(request, "gainDb", 0.0)));
            if (has("muted"))
                updated.muted = static_cast<bool>(request.getProperty("muted", false));
            if (has("monitor"))
                updated.monitor = static_cast<bool>(request.getProperty("monitor", false));
            if (has("exposeToSystem"))
                updated.exposeToSystem = static_cast<bool>(request.getProperty("exposeToSystem", false));
            if (!audio_.updateVirtualBus(updated))
                return fail("the bus refused those settings");
        }
        virtualBusManager_.refresh();
        refreshMixer();
        markProjectDirty();
        auto result = object();
        set(result, "buses", status().getProperty("buses", juce::var()));
        if (command == "add_bus")
            set(result, "busId", idOf(bus->id));
        return ok(result);
    }
    if (command == "audio_settings" || command == "set_audio_device") {
        auto& manager = audio_.deviceManager();
        if (command == "set_audio_device") {
            if (has("type"))
                manager.setCurrentAudioDeviceType(text("type"), true);
            auto setup = manager.getAudioDeviceSetup();
            if (has("output")) {
                setup.outputDeviceName = text("output");
                setup.useDefaultOutputChannels = true;
            }
            if (has("input")) {
                setup.inputDeviceName = text("input");
                setup.useDefaultInputChannels = true;
            }
            if (has("sampleRate"))
                setup.sampleRate = number(request, "sampleRate", setup.sampleRate);
            if (has("bufferSize"))
                setup.bufferSize = static_cast<int>(number(request, "bufferSize", setup.bufferSize));
            const auto error = manager.setAudioDeviceSetup(setup, true);
            if (error.isNotEmpty())
                return fail(error);
        }
        auto result = object();
        const auto setup = manager.getAudioDeviceSetup();
        set(result, "type", manager.getCurrentAudioDeviceType());
        set(result, "output", setup.outputDeviceName);
        set(result, "input", setup.inputDeviceName);
        set(result, "sampleRate", setup.sampleRate);
        set(result, "bufferSize", setup.bufferSize);
        if (auto* device = manager.getCurrentAudioDevice()) {
            juce::Array<juce::var> rates;
            for (const auto rate : device->getAvailableSampleRates())
                rates.add(rate);
            juce::Array<juce::var> sizes;
            for (const auto size : device->getAvailableBufferSizes())
                sizes.add(size);
            set(result, "availableSampleRates", rates);
            set(result, "availableBufferSizes", sizes);
            set(result, "latencyMs", (device->getOutputLatencyInSamples()
                                       + device->getInputLatencyInSamples())
                                          / std::max(1.0, device->getCurrentSampleRate()) * 1000.0);
        }
        juce::Array<juce::var> types;
        for (auto* type : manager.getAvailableDeviceTypes()) {
            type->scanForDevices();
            auto entry = object();
            set(entry, "type", type->getTypeName());
            set(entry, "outputs", strings(type->getDeviceNames(false)));
            set(entry, "inputs", strings(type->getDeviceNames(true)));
            types.add(entry);
        }
        set(result, "types", types);
        return ok(result);
    }
    if (command == "import_midi") {
        const auto file = fileFor(text("file"));
        if (!file.existsAsFile())
            return fail("no MIDI file at " + file.getFullPathName());
        const auto before = audio_.midiTracks().size();
        importMidiFile(file);
        auto result = describeTracks();
        set(result, "tracksAdded", static_cast<int>(audio_.midiTracks().size() - before));
        return ok(result);
    }
    if (command == "export_midi") {
        std::optional<RemoteTrack> track;
        if (auto error = trackOrFail(track); !error.isVoid())
            return error;
        if (!track->midi)
            return fail("MIDI export takes a MIDI track");
        const auto info = midiInfo(track->id);
        const auto clip = info ? midiClipNamed(*info, text("clip").trim()) : std::nullopt;
        if (!clip)
            return fail("no such clip on that track");
        const auto file = fileFor(text("file")).withFileExtension("mid");
        if (!file.getParentDirectory().createDirectory())
            return fail("no folder for " + file.getFullPathName());
        if (const auto written = audio_.exportMidiClipToFile(track->id, clip->id, file);
            written.failed())
            return fail(written.getErrorMessage());
        auto result = object();
        set(result, "path", file.getFullPathName());
        set(result, "notes", static_cast<int>(clip->notes.size()));
        return ok(result);
    }
    if (command == "set_launch_quantization") {
        const auto value = text("value");
        using Quantization = AudioTrackController::LaunchQuantization;
        const auto quantization = value == "none" ? Quantization::none
                                  : value == "quarter" ? Quantization::quarterBeat
                                  : value == "half" ? Quantization::halfBeat
                                  : value == "beat" ? Quantization::oneBeat
                                  : value == "bar" ? Quantization::oneBar
                                  : std::optional<Quantization> {};
        if (!quantization)
            return fail("value is none, quarter, half, beat or bar");
        audio_.setLaunchQuantization(*quantization);
        refreshClipLauncher();
        markProjectDirty();
        auto result = object();
        set(result, "launchQuantization", static_cast<int>(audio_.launchQuantization()));
        return ok(result);
    }
    if (command == "samples_status" || command == "sample_folders" || command == "scan_samples"
        || command == "search_samples" || command == "classify_samples"
        || command == "set_sample" || command == "sample_cache"
        || command == "select_sample" || command == "preview_sample") {
        const auto describe = [this](const SampleRecord& record) {
            auto entry = object();
            set(entry, "path", record.path);
            set(entry, "name", record.name);
            set(entry, "folder", record.root);
            set(entry, "length", record.length);
            set(entry, "sampleRate", record.sampleRate);
            set(entry, "channels", record.channels);
            set(entry, "type", record.type);
            set(entry, "category", record.category);
            set(entry, "tags", strings(record.tags));
            set(entry, "key", record.key);
            set(entry, "keySource", record.keySource);
            set(entry, "keyConfidence", record.keyConfidence);
            set(entry, "bpm", record.bpm);
            set(entry, "bpmSource", record.bpmSource);
            set(entry, "bpmConfidence", record.bpmConfidence);
            set(entry, "peakDb", record.peakDb);
            set(entry, "classifier", record.classifier);
            set(entry, "favourite", record.favourite);
            set(entry, "rating", record.rating);
            return entry;
        };
        const auto statusOf = [this] {
            const auto progress = sampleLibrary_.progress();
            auto result = object();
            set(result, "samples", static_cast<int>(sampleLibrary_.samples().size()));
            set(result, "scanning", progress.scanning);
            set(result, "classifying", progress.classifying);
            set(result, "found", progress.found);
            set(result, "analysed", progress.analysed);
            set(result, "toAnalyse", progress.toAnalyse);
            set(result, "classified", progress.classified);
            set(result, "toClassify", progress.toClassify);
            set(result, "message", progress.message);
            juce::StringArray folders;
            for (const auto& folder : sampleLibrary_.folders())
                folders.add(folder.getFullPathName());
            set(result, "folders", strings(folders));
            set(result, "cacheFolder", sampleLibrary_.cacheFolder().getFullPathName());
            set(result, "autoRescanMinutes", sampleLibrary_.autoRescanMinutes());
            set(result, "classifierEnabled", sampleLibrary_.classifierEnabled());
            set(result, "categories", strings(sampleLibrary_.distinctCategories()));
            set(result, "types", strings(sampleLibrary_.distinctTypes()));
            set(result, "tags", strings(sampleLibrary_.distinctTags()));
            return result;
        };
        if (command == "sample_folders") {
            if (has("add")) {
                const auto folder = fileFor(text("add"));
                if (!folder.isDirectory())
                    return fail("no folder at " + folder.getFullPathName());
                sampleLibrary_.addFolder(folder);
            }
            if (has("remove"))
                sampleLibrary_.removeFolder(fileFor(text("remove")));
            if (has("autoRescanMinutes"))
                sampleLibrary_.setAutoRescanMinutes(
                    static_cast<int>(number(request, "autoRescanMinutes", 10.0)));
            return ok(statusOf());
        }
        if (command == "sample_cache") {
            if (has("folder")) {
                const auto folder = fileFor(text("folder"));
                (void) folder.createDirectory();
                if (!folder.isDirectory())
                    return fail("no folder at " + folder.getFullPathName());
                sampleLibrary_.setCacheFolder(folder);
            }
            sampleLibrary_.saveNow();
            return ok(statusOf());
        }
        if (command == "scan_samples") {
            sampleLibrary_.rescan(static_cast<bool>(request.getProperty("full", false)));
            return ok(statusOf());
        }
        if (command == "classify_samples") {
            if (has("enabled"))
                sampleLibrary_.setClassifierEnabled(
                    static_cast<bool>(request.getProperty("enabled", true)));
            if (has("python"))
                sampleLibrary_.setPythonCommand(text("python"));
            sampleLibrary_.classify(!static_cast<bool>(request.getProperty("all", false)));
            return ok(statusOf());
        }
        if (command == "set_sample") {
            const auto path = fileFor(text("path")).getFullPathName();
            if (sampleLibrary_.find(path) == nullptr)
                return fail("no sample " + path + " in the library");
            if (has("favourite"))
                sampleLibrary_.setFavourite(path, static_cast<bool>(request.getProperty("favourite", false)));
            if (has("rating"))
                sampleLibrary_.setRating(path, static_cast<int>(number(request, "rating", 0.0)));
            if (has("category"))
                sampleLibrary_.setCategory(path, text("category"));
            if (has("type"))
                sampleLibrary_.setType(path, text("type"));
            if (has("bpm"))
                sampleLibrary_.setBpm(path, number(request, "bpm", 0.0));
            if (has("tags")) {
                juce::StringArray tags;
                if (const auto* list = request.getProperty("tags", juce::var()).getArray())
                    for (const auto& tag : *list)
                        tags.add(tag.toString());
                else
                    tags.addTokens(text("tags"), ",;", "");
                sampleLibrary_.setTags(path, tags);
            }
            return ok(describe(*sampleLibrary_.find(path)));
        }
        if (command == "select_sample" || command == "preview_sample") {
            const auto path = fileFor(text("path")).getFullPathName();
            const auto* record = sampleLibrary_.find(path);
            if (command == "select_sample") {
                if (record == nullptr)
                    return fail("no sample " + path + " in the library");
                setEditorMode(10);
                sampleExplorer_.selectPath(path);
                return ok(describe(*record));
            }
            if (static_cast<bool>(request.getProperty("stop", false))) {
                audio_.stopSamplePreview();
            } else {
                if (!juce::File(path).existsAsFile())
                    return fail("no file at " + path);
                if (has("gainDb"))
                    audio_.setSamplePreviewGainDb(
                        static_cast<float>(juce::jlimit(-60.0, 6.0, number(request, "gainDb", -6.0))));
                if (const auto started = audio_.startSamplePreview(
                        juce::File(path), std::max(0.0, number(request, "start", 0.0)));
                    started.failed())
                    return fail(started.getErrorMessage());
            }
            auto result = object();
            set(result, "previewing", audio_.isSamplePreviewing());
            set(result, "position", audio_.samplePreviewPositionSeconds());
            return ok(result);
        }
        if (command == "search_samples") {
            const auto query = text("query").trim().toLowerCase();
            const auto category = text("category").trim().toLowerCase();
            const auto type = text("type").trim().toLowerCase();
            const auto tag = text("tag").trim().toLowerCase();
            const auto folder = text("folder").trim();
            const auto onlyFavourites = static_cast<bool>(request.getProperty("favourites", false));
            const auto minLength = number(request, "minLength", 0.0);
            const auto maxLength = number(request, "maxLength", 1.0e9);
            const auto limit = static_cast<int>(number(request, "limit", 100.0));
            juce::StringArray words;
            words.addTokens(query, " ", "");
            juce::Array<juce::var> entries;
            auto matched = 0;
            for (const auto& record : sampleLibrary_.samples()) {
                if (category.isNotEmpty() && record.category != category)
                    continue;
                if (type.isNotEmpty() && record.type != type)
                    continue;
                if (tag.isNotEmpty() && !record.tags.contains(tag))
                    continue;
                if (folder.isNotEmpty() && !record.root.containsIgnoreCase(folder))
                    continue;
                if (onlyFavourites && !record.favourite)
                    continue;
                if (record.length < minLength || record.length > maxLength)
                    continue;
                const auto haystack = (record.name + " " + record.category + " "
                                       + record.tags.joinIntoString(" ") + " " + record.key)
                                          .toLowerCase();
                auto all = true;
                for (const auto& word : words)
                    if (word.isNotEmpty() && !haystack.contains(word))
                        all = false;
                if (!all)
                    continue;
                ++matched;
                if (entries.size() < limit)
                    entries.add(describe(record));
            }
            auto result = object();
            set(result, "matched", matched);
            set(result, "samples", entries);
            return ok(result);
        }
        return ok(statusOf());
    }
    return fail("unknown command \"" + command + "\"");
}

} // namespace cydaw::app
