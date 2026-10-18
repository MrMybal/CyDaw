#include "PluginFiles.h"
// The control port's commands for CyDAW 2, one function per verb, on the
// message thread. A port of CyDAW 1's WorkspaceComponentRemote.cpp: the same
// verbs, parameters, answers and messages, so tools/mcp/cydaw_mcp_server.py
// and tools/mcp/cydaw_mcp_check.py work unchanged. The session is the
// Studio; a track's graph is edited in place in its own GraphSlot and
// published to the engine after every edit; what the interface shows goes
// through RemoteUi.
#include "RemoteCommands.h"
#include "CustomInterface.h"
#include "InterfaceEditor.h"
#include "Presets.h"
#include "NativeDspGraphState.h"
#include "SamplerMapFile.h"
#include "SoundModelFile.h"
#include "GraphPolySynthRenderer.h"
#include "NativeDspRuntime.h"

#include "GraphLayout.h"
#include "Localization.h"
#include "Widgets.h"
#include "ProjectImport.h"
#include "VirtualAudioBus.h"

#include <cydaw/engine/ChannelLayout.h>
#include <cydaw/engine/KickSynth.h>
#include <cydaw/engine/EffectCatalogue.h>
#include <cydaw/engine/Modulators.h>
#include <cydaw/engine/DrumKit.h>
#include <cydaw/engine/SynthFamily.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <vector>

namespace cydaw::v2 {

using PresetMenuCapture = std::function<juce::var()>;
using PresetMenuApply = std::function<bool(const juce::var&, juce::String&)>;
namespace {

using Engine = app::AudioTrackController;

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

std::uint64_t idFrom(const juce::var& value)
{
    return static_cast<std::uint64_t>(value.toString().getLargeIntValue());
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
    return juce::File::isAbsolutePath(path) ? juce::File(path)
                                            : juce::File::getCurrentWorkingDirectory().getChildFile(path);
}

juce::File templatesFolder()
{
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("templates");
}

double numberOf(const juce::var& value, const char* name, const double fallback)
{
    const auto field = value.getProperty(name, juce::var());
    return field.isVoid() ? fallback : static_cast<double>(field);
}

juce::String colourText(const juce::uint32 argb)
{
    return "#" + juce::Colour(argb).toDisplayString(false);
}

std::uint32_t colourFrom(const juce::String& text)
{
    auto hex = text.trim().trimCharactersAtStart("#");
    if (hex.length() == 6)
        hex = "ff" + hex;
    return hex.length() == 8 ? static_cast<std::uint32_t>(hex.getHexValue64()) : 0u;
}

// A PNG's size, read from its header (IHDR follows the signature).
juce::Point<int> pngSize(const juce::File& file)
{
    juce::FileInputStream stream(file);
    std::array<std::uint8_t, 24> header {};
    if (!stream.openedOk() || stream.read(header.data(), static_cast<int>(header.size())) != static_cast<int>(header.size()))
        return {};
    const auto word = [&header](const std::size_t at) {
        return static_cast<int>((static_cast<std::uint32_t>(header[at]) << 24) | (static_cast<std::uint32_t>(header[at + 1]) << 16)
                                | (static_cast<std::uint32_t>(header[at + 2]) << 8) | static_cast<std::uint32_t>(header[at + 3]));
    };
    return { word(16), word(20) };
}

struct RemoteTrack {
    bool midi {};
    std::size_t index {};   // audio: the track index; MIDI: its position
    std::uint64_t id {};    // MIDI only
    std::size_t channel {}; // the combined index: audio tracks, then MIDI
    bool bus {};            // a virtual bus — "bus:<name|id>"
    std::uint64_t busId {};
    bool master {};         // the master track ("master"): its graph only

    [[nodiscard]] TrackRef ref() const noexcept
    {
        if (master)
            return Studio::masterTrack();
        return midi ? TrackRef { true, 0, id } : TrackRef { false, index, 0 };
    }
};

// The graph a command edits: a track's, or a bus's.
struct GraphTarget {
    GraphSlot* slot {};
    std::optional<TrackRef> track; // empty for a bus
    std::uint64_t busId {};
    // The device the workshop edits ("track": "workshop"), not a track's.
    bool workshop {};

    [[nodiscard]] app::NodeGraphComponent& graph() const noexcept { return slot->graph(); }
};

class Commands final {
public:
    Commands(Studio& studio, RemoteUi& ui, const juce::var& request)
        : studio_(studio),
          engine_(studio.engine()),
          ui_(ui),
          request_(request),
          command_(request.getProperty("cmd", juce::var()).toString())
    {
    }

    juce::var run();

private:
    // ---- The request -----------------------------------------------------------------
    [[nodiscard]] juce::String text(const char* name) const
    {
        return request_.getProperty(name, juce::var()).toString();
    }
    [[nodiscard]] bool has(const char* name) const
    {
        return !request_.getProperty(name, juce::var()).isVoid();
    }
    [[nodiscard]] double number(const char* name, const double fallback) const
    {
        return numberOf(request_, name, fallback);
    }
    [[nodiscard]] bool flag(const char* name, const bool fallback) const
    {
        return static_cast<bool>(request_.getProperty(name, fallback));
    }

    // ---- What the session looks like ---------------------------------------------------
    [[nodiscard]] juce::StringArray deviceNames(const TrackRef& track, const int limit) const
    {
        if (const auto* slot = studio_.graphFor(track))
            return slot->graph().deviceNames(limit);
        return {};
    }

    [[nodiscard]] juce::var describeTracks() const
    {
        juce::Array<juce::var> audioTracks;
        for (std::size_t index = 0; index < engine_.trackCount(); ++index) {
            const auto info = engine_.trackInfo(index);
            const TrackRef ref { false, index, 0 };
            auto name = studio_.trackName(ref);
            if (name.isEmpty())
                name = info.file.getFileNameWithoutExtension();
            auto entry = object();
            set(entry, "index", static_cast<int>(index));
            set(entry, "kind", "audio");
            set(entry, "name", name);
            set(entry, "file", info.file.getFullPathName());
            set(entry, "lengthSeconds", info.lengthSeconds);
            set(entry, "gainDb", info.gainDb);
            set(entry, "pan", info.pan);
            set(entry, "muted", info.muted);
            set(entry, "solo", info.solo);
            set(entry, "armed", info.armed);
            set(entry, "colour", colourText(studio_.trackColour(ref)));
            set(entry, "inputBus", idOf(info.inputBusId));
            set(entry, "inputAfterEffects", engine_.trackMixState(index).inputAfterEffects);
            set(entry, "channels", studio_.trackChannels(ref));
            set(entry, "format", juce::String(std::string(cydaw::engine::layoutKey(studio_.trackChannels(ref)))));
            set(entry, "clips", static_cast<int>(info.clips.size()));
            set(entry, "devices", strings(deviceNames(ref, 16)));
            juce::Array<juce::var> plugins;
            for (std::size_t plugin = 0; plugin < info.plugins.size(); ++plugin) {
                auto pluginEntry = object();
                set(pluginEntry, "index", static_cast<int>(plugin));
                set(pluginEntry, "name", info.plugins[plugin].name);
                set(pluginEntry, "file", info.plugins[plugin].file.getFullPathName());
                set(pluginEntry, "running", info.plugins[plugin].running);
                set(pluginEntry, "latencySamples", info.plugins[plugin].latencySamples);
                plugins.add(pluginEntry);
            }
            set(entry, "plugins", plugins);
            // Its devices' latency, and the wait the mix gives it.
            set(entry, "latencySamples", engine_.trackLatencySamples(index));
            set(entry, "compensationSamples", engine_.trackCompensationSamples(index));
            if (studio_.isBusTrack(ref))
                set(entry, "bus", true);
            set(entry, "outputs", outputsSummary(ref));
            if (const auto inputs = studio_.trackInputs(ref); !inputs.empty())
                set(entry, "inputs", inputsSummary(ref));
            audioTracks.add(entry);
        }
        juce::Array<juce::var> midiTracks;
        const auto tracks = engine_.midiTracks();
        for (std::size_t position = 0; position < tracks.size(); ++position) {
            const auto& info = tracks[position];
            const TrackRef ref { true, 0, info.id };
            auto entry = object();
            set(entry, "index", static_cast<int>(engine_.trackCount() + position));
            set(entry, "kind", "midi");
            set(entry, "id", idOf(info.id));
            set(entry, "outputs", outputsSummary(ref));
            if (const auto inputs = studio_.trackInputs(ref); !inputs.empty())
                set(entry, "inputs", inputsSummary(ref));
            set(entry, "name", info.name);
            set(entry, "gainDb", info.gainDb);
            set(entry, "pan", info.pan);
            set(entry, "muted", info.muted);
            set(entry, "solo", info.solo);
            set(entry, "armed", info.armed);
            set(entry, "colour", colourText(studio_.trackColour(ref)));
            set(entry, "devices", strings(deviceNames(ref, 16)));
            set(entry, "latencySamples", engine_.midiTrackLatencySamples(info.id));
            set(entry, "compensationSamples", engine_.midiTrackCompensationSamples(info.id));
            // The MIDI effects its notes go through, by type, in order.
            {
                juce::Array<juce::var> effects;
                if (const auto plan = engine_.midiTrackInstrumentGraph(info.id); plan.midiEffects != nullptr)
                    for (std::size_t index = 0; index < plan.midiEffects->count; ++index)
                        if (const auto* effect = engine::effectForKind(plan.midiEffects->effects[index].kind))
                            effects.add(juce::String(std::string(effect->typeKey)));
                set(entry, "midiEffects", effects);
            }
            juce::Array<juce::var> clips;
            for (const auto& clip : info.clips) {
                auto clipEntry = object();
                set(clipEntry, "id", idOf(clip.id));
                set(clipEntry, "name", clip.name);
                set(clipEntry, "startBeats", clip.startBeats);
                set(clipEntry, "lengthBeats", clip.lengthBeats);
                set(clipEntry, "looping", clip.looping);
                set(clipEntry, "scene", clip.launcherScene);
                set(clipEntry, "colour", colourText(clip.colour));
                set(clipEntry, "notes", static_cast<int>(clip.notes.size()));
                clips.add(clipEntry);
            }
            set(entry, "clips", clips);
            midiTracks.add(entry);
        }
        auto result = object();
        set(result, "audioTracks", audioTracks);
        set(result, "midiTracks", midiTracks);
        return result;
    }

    [[nodiscard]] juce::var describeBuses() const
    {
        juce::Array<juce::var> buses;
        for (const auto& bus : engine_.virtualBuses()) {
            auto entry = object();
            set(entry, "id", idOf(bus.id));
            set(entry, "name", bus.name);
            set(entry, "direction", bus.direction == app::VirtualBusDirection::input ? "input" : "output");
            set(entry, "channels", bus.channels);
            set(entry, "physicalA", bus.physicalChannelA);
            set(entry, "physicalB", bus.physicalChannelB);
            set(entry, "destinationA", bus.destinationChannelA);
            set(entry, "destinationB", bus.destinationChannelB);
            juce::Array<juce::var> deviceChannels;
            for (auto channel = 0; channel < bus.channels; ++channel)
                deviceChannels.add(bus.deviceChannel(channel));
            set(entry, "deviceChannels", deviceChannels);
            set(entry, "format", juce::String(std::string(cydaw::engine::layoutKey(bus.channels))));
            if (bus.direction == app::VirtualBusDirection::output)
                set(entry, "carriesMaster", bus.carriesMaster);
            // Its effects: the cards of its graph.
            juce::StringArray effects;
            if (const auto* slot = const_cast<Studio&>(studio_).busGraph(bus.id))
                for (const auto& node : slot->graph().editorNodes())
                    if (!node.boundary)
                        effects.add(node.title.isNotEmpty() ? node.title : node.typeKey);
            set(entry, "effects", strings(effects));
            set(entry, "gainDb", bus.gainDb);
            set(entry, "muted", bus.muted);
            set(entry, "monitor", bus.monitor);
            set(entry, "exposeToSystem", bus.exposeToSystem);
            buses.add(entry);
        }
        return buses;
    }

    [[nodiscard]] juce::var status() const
    {
        auto result = describeTracks();
        if (const auto opening = studio_.projectOpening(); opening.active) {
            auto state = object();
            set(state, "file", opening.file.getFullPathName());
            set(state, "progress", opening.progress);
            set(state, "doing", opening.doing);
            set(state, "reading", opening.reading);
            set(state, "seconds", opening.seconds);
            set(result, "opening", state);
        }
        set(result, "project", studio_.projectFile().getFullPathName());
        set(result, "dirty", studio_.isDirty());
        set(result, "tempo", engine_.tempo());
        set(result, "timeSignature", juce::String(engine_.timeSignatureNumerator()) + "/"
                                         + juce::String(engine_.timeSignatureDenominator()));
        set(result, "playing", engine_.isPlaying());
        set(result, "speakersSilenced", engine_.speakersSilenced());
        set(result, "position", engine_.position());
        set(result, "length", engine_.length());
        auto loop = object();
        set(loop, "start", engine_.loopStart());
        set(loop, "end", engine_.loopEnd());
        set(loop, "enabled", studio_.loopEnabled());
        set(result, "loop", loop);
        set(result, "editorMode", ui_.currentView());
        set(result, "buses", describeBuses());
        set(result, "defaultInputBus", idOf(engine_.defaultInputBusId()));
        set(result, "launchQuantization", static_cast<int>(engine_.launchQuantization()));
        set(result, "masterGainDb", engine_.masterGainDb());
        set(result, "masterMuted", engine_.isMasterMuted());
        set(result, "exporting", engine_.isMasterExporting());
        auto selected = -1;
        if (const auto current = ui_.selectedTrack()) {
            const auto all = studio_.tracks();
            for (std::size_t position = 0; position < all.size(); ++position)
                if (all[position] == *current)
                    selected = static_cast<int>(position);
        }
        set(result, "selectedTrack", selected);
        // What each track and the master put out now, in dB: is it playing,
        // does anything clip.
        const auto toDb = [](const float gain) {
            return gain > 1.0e-6f ? std::round(200.0 * std::log10(static_cast<double>(gain))) / 10.0 : -120.0;
        };
        const auto meterEntry = [&](const juce::String& name, const MeterState& meter) {
            auto entry = object();
            set(entry, "name", name);
            set(entry, "peakDb", toDb(std::max(meter.peak[0], meter.peak[1])));
            set(entry, "rmsDb", toDb(std::max(meter.rms[0], meter.rms[1])));
            return entry;
        };
        juce::Array<juce::var> meters;
        for (const auto& track : studio_.tracks())
            meters.add(meterEntry(studio_.trackName(track), studio_.trackMeter(track)));
        set(result, "meters", meters);
        set(result, "masterMeter", meterEntry("master", studio_.masterMeter()));
        // The latency compensation: the latest path into the master, in samples.
        set(result, "latencyCompensationSamples", engine_.mixLatencySamples());
        set(result, "language", ui::language());
        set(result, "translatedTexts", ui::translationCount());
        juce::StringArray languages;
        for (const auto& language : app::Localization::availableLanguages())
            languages.add(language.code);
        set(result, "languages", strings(languages));
        return result;
    }

    // ---- Tracks by index (audio first, then MIDI), by name, or a bus --------------
    [[nodiscard]] std::optional<RemoteTrack> resolveTrack(const juce::var& spec) const
    {
        const auto summaries = engine_.midiTrackSummaries();
        const auto audioCount = engine_.trackCount();
        const auto byIndex = [&summaries, audioCount](const int index) -> std::optional<RemoteTrack> {
            if (index < 0)
                return std::nullopt;
            const auto channel = static_cast<std::size_t>(index);
            if (channel < audioCount)
                return RemoteTrack { false, channel, 0, channel };
            const auto position = channel - audioCount;
            if (position < summaries.size())
                return RemoteTrack { true, position, summaries[position].id, channel };
            return std::nullopt;
        };
        if (spec.isInt() || spec.isInt64() || spec.isDouble())
            return byIndex(static_cast<int>(spec));
        const auto name = spec.toString().trim();
        if (name.isEmpty())
            return std::nullopt;
        // "master" is the master track (its devices); "bus:<name>" or
        // "bus:<id>" a bus, "bus:" alone the output one.
        if (name.equalsIgnoreCase("master")) {
            RemoteTrack master;
            master.master = true;
            return master;
        }
        if (name.startsWithIgnoreCase("bus:")) {
            const auto wanted = name.fromFirstOccurrenceOf(":", false, false).trim();
            for (const auto& bus : engine_.virtualBuses()) {
                const auto matches = wanted.isEmpty()
                                         ? bus.direction == app::VirtualBusDirection::output
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
        for (std::size_t index = 0; index < audioCount; ++index) {
            const auto candidate = studio_.trackName(TrackRef { false, index, 0 });
            if (candidate.equalsIgnoreCase(name))
                return RemoteTrack { false, index, 0, index };
            if (candidate.containsIgnoreCase(name)) {
                loose = RemoteTrack { false, index, 0, index };
                ++looseMatches;
            }
        }
        for (std::size_t position = 0; position < summaries.size(); ++position) {
            const auto channel = audioCount + position;
            if (summaries[position].name.equalsIgnoreCase(name))
                return RemoteTrack { true, position, summaries[position].id, channel };
            if (summaries[position].name.containsIgnoreCase(name)) {
                loose = RemoteTrack { true, position, summaries[position].id, channel };
                ++looseMatches;
            }
        }
        return looseMatches == 1 ? loose : std::nullopt;
    }

    // Fills `track` from the request, or returns the answer to give. Buses
    // pass only where the command says it takes them.
    juce::var trackOrFail(std::optional<RemoteTrack>& track, const bool busAllowed = false) const
    {
        track = resolveTrack(request_.getProperty("track", juce::var()));
        if (!track)
            return fail("no track matches \"" + text("track")
                        + "\"; status lists them by index and name, and"
                          " \"master\" or \"bus:<name>\" name a bus");
        if (track->bus && !busAllowed)
            return fail("\"" + text("track") + "\" is a bus; set_bus sets it, and the graph commands edit its graph");
        if (track->master && !busAllowed)
            return fail("the master is edited through its graph (insert_device, get_track_graph, list_parameters) and "
                        "set_master_gain");
        return {};
    }

    // ---- Graphs ------------------------------------------------------------------------
    // The graph a command edits. A track whose graph is edited is selected, as
    // CyDAW 1 put it in front of the editor; a bus nothing edited yet gets its
    // graph slot (the factory graph, as a project load gives it).
    [[nodiscard]] std::optional<GraphTarget> graphTarget(const RemoteTrack& track)
    {
        GraphTarget target;
        if (track.bus) {
            target.busId = track.busId;
            target.slot = studio_.busGraph(track.busId);
            if (target.slot != nullptr) {
                target.track = TrackRef::ofBus(track.busId);
                ui_.selectTrack(*target.track);
            }
        } else {
            target.track = track.ref();
            target.slot = studio_.graphFor(*target.track);
            if (target.slot != nullptr)
                ui_.selectTrack(*target.track);
        }
        if (track.master && !track.bus && target.slot != nullptr)
            target.track = Studio::masterTrack();
        if (target.slot == nullptr)
            return std::nullopt;
        return target;
    }

    [[nodiscard]] juce::var noGraph() const
    {
        return fail("\"" + text("track") + "\" has no graph");
    }

    // Publishes the edited graph to the engine. Structural edits notify the
    // studio, which publishes them itself; knob values do not, and a second
    // publication of the same shape only updates values.
    void commitGraph(const GraphTarget& target, const bool structural)
    {
        // The views' caches (taps, modulation targets) follow the revision.
        if (target.slot != nullptr)
            target.slot->touch();
        // A track's modulation graph: kept, never compiled for sound.
        if (target.slot != nullptr && target.slot->owner() == GraphSlot::Owner::modulation && target.track) {
            studio_.modulationGraphEdited(*target.track);
            return;
        }
        if (target.workshop) {
            // The device, then the track it is linked to, follow.
            studio_.commitWorkshop(structural);
            studio_.markDirty();
            return;
        }
        if (target.track)
            (void) studio_.publishGraph(*target.track, structural);
        else
            (void) engine_.setVirtualBusGraph(target.busId, target.graph().nativeDspGraph());
        studio_.markDirty();
    }

    // The graph, compactly: cards with their published knobs, and the cables
    // between them.
    // A track's outputs: where each goes ("master" or the track's name), at
    // what level.
    [[nodiscard]] juce::var outputsSummary(const TrackRef& track) const
    {
        juce::Array<juce::var> list;
        for (const auto& output : studio_.trackOutputs(track)) {
            auto entry = object();
            const auto to = studio_.trackForKey(output.destination);
            if (output.hardware != 0) {
                const auto bus = engine_.virtualBus(output.hardware);
                set(entry, "hardware", bus ? bus->name : juce::String("(gone)"));
                set(entry, "hardwareId", idOf(output.hardware));
                if (output.hardwareAfterEffects)
                    set(entry, "hardwareAfterEffects", true);
            } else {
                set(entry, "destination", to ? studio_.trackName(*to) : juce::String("master"));
            }
            set(entry, "gainDb", output.gainDb);
            if (to && output.input != 0)
                set(entry, "input", studio_.trackInputName(*to, output.input));
            if (output.preFader)
                set(entry, "preFader", true);
            list.add(entry);
        }
        return list;
    }

    // Its audio inputs beside the main one, by name.
    [[nodiscard]] juce::var inputsSummary(const TrackRef& track) const
    {
        juce::StringArray names;
        for (const auto& input : studio_.trackInputs(track))
            names.add(input.name);
        return strings(names);
    }

    // An input of `track` named by its name or its number (2 the first
    // beside the main one); 0 the main one, nothing when there is none.
    [[nodiscard]] std::optional<std::uint64_t> inputOf(const TrackRef& track, const juce::var& value) const
    {
        if (value.isVoid() || value.toString().isEmpty() || value.toString().equalsIgnoreCase("main"))
            return 0;
        const auto inputs = studio_.trackInputs(track);
        for (const auto& input : inputs)
            if (input.name.equalsIgnoreCase(value.toString()))
                return input.node;
        if (value.isInt() || value.isInt64() || value.isDouble() || value.toString().containsOnly("0123456789")) {
            const auto number = static_cast<int>(value.toString().getIntValue());
            if (number == 1)
                return 0;
            if (number >= 2 && number - 2 < static_cast<int>(inputs.size()))
                return inputs[static_cast<std::size_t>(number - 2)].node;
        }
        return std::nullopt;
    }

    [[nodiscard]] juce::var graphSummary(const GraphTarget& target) const
    {
        const auto state = target.graph().serialise();
        juce::Array<juce::var> nodes;
        if (const auto* list = state.getProperty("nodes", juce::var()).getArray())
            for (const auto& node : *list) {
                auto entry = object();
                set(entry, "id", static_cast<int>(static_cast<juce::int64>(node.getProperty("id", 0))));
                set(entry, "type", node.getProperty("type", juce::var()).toString());
                set(entry, "title", node.getProperty("title", juce::var()).toString());
                juce::Array<juce::var> parameters;
                if (const auto* published
                    = node.getProperty("interface", juce::var()).getProperty("parameters", juce::var()).getArray())
                    for (const auto& parameter : *published) {
                        auto knob = object();
                        set(knob, "key", parameter.getProperty("key", juce::var()).toString());
                        set(knob, "label", parameter.getProperty("label", juce::var()).toString());
                        set(knob, "value", parameter.getProperty("value", juce::var()));
                        parameters.add(knob);
                    }
                set(entry, "parameters", parameters);
                // A device made of a graph: open_device edits its inside.
                if (const auto module = target.graph().editorModuleOf(
                        static_cast<std::uint64_t>(static_cast<juce::int64>(node.getProperty("id", 0)))))
                    set(entry, "graphDevice", true);
                // A designed interface: device_window opens it.
                {
                    const auto id = static_cast<std::uint64_t>(static_cast<juce::int64>(node.getProperty("id", 0)));
                    for (const auto& card : target.graph().editorNodes())
                        if (card.id == id) {
                            // The name its card shows, in the interface's language.
                            set(entry, "name", card.title);
                            if (card.hasInterface)
                                set(entry, "window", true);
                        }
                }
                nodes.add(entry);
            }
        juce::Array<juce::var> connections;
        if (const auto* list = state.getProperty("connections", juce::var()).getArray())
            for (const auto& connection : *list) {
                auto entry = object();
                set(entry, "source", static_cast<int>(connection.getProperty("source", 0)));
                set(entry, "destination", static_cast<int>(connection.getProperty("destination", 0)));
                set(entry, "kind", static_cast<int>(connection.getProperty("kind", 0)));
                set(entry, "sourcePort", connection.getProperty("sourcePort", 0));
                set(entry, "destinationPort", connection.getProperty("destinationPort", 0));
                if (connection.hasProperty("mode")) {
                    set(entry, "mode", static_cast<int>(connection.getProperty("mode", 0)) == 1 ? "modulate" : "set");
                    set(entry, "amount", connection.getProperty("amount", 0.5));
                }
                connections.add(entry);
            }
        auto result = object();
        set(result, "nodes", nodes);
        set(result, "connections", connections);
        // The cards switched off, as the engine is told: the node whose
        // output is replaced and the one whose input passes (0: silence).
        juce::Array<juce::var> bypasses;
        for (const auto& bypass : target.graph().nativeDspGraph().bypasses) {
            juce::Array<juce::var> pair;
            pair.add(static_cast<juce::int64>(bypass.node));
            pair.add(static_cast<juce::int64>(bypass.from));
            bypasses.add(pair);
        }
        set(result, "bypasses", bypasses);
        if (!target.track) {
            juce::StringArray names;
            for (const auto& node : nodes)
                names.add(node.getProperty("type", juce::var()).toString());
            set(result, "devices", strings(names));
        } else {
            set(result, "devices", strings(target.graph().deviceNames(32)));
        }
        if (const auto error = target.slot->lastCompileError(); error.isNotEmpty())
            set(result, "compileError", error);
        if (target.workshop) {
            juce::StringArray trail;
            for (const auto& name : studio_.workshop().trail)
                trail.add(name);
            set(result, "workshopTrail", strings(trail));
        }
        return result;
    }

    // The published key of a knob on the last card of a type.
    [[nodiscard]] static juce::String knobKey(const app::NodeGraphComponent& graph, const juce::String& typeKey,
                                              const juce::String& suffix)
    {
        const auto id = graph.lastNodeIdOfType(typeKey);
        return id == 0 ? juce::String() : "node." + juce::String(static_cast<juce::int64>(id)) + "." + suffix;
    }

    // The graph whose catalogue answers list_devices and describe_node_type:
    // the selected track's, else any, else a fresh one.
    [[nodiscard]] app::NodeGraphComponent& catalogueGraph()
    {
        if (const auto selected = ui_.selectedTrack())
            if (auto* slot = studio_.graphFor(*selected))
                return slot->graph();
        for (const auto& track : studio_.tracks())
            if (auto* slot = studio_.graphFor(track))
                return slot->graph();
        for (auto& entry : studio_.busStates())
            if (entry.second.graph != nullptr)
                return entry.second.graph->graph();
        scratchGraph_ = std::make_unique<app::NodeGraphComponent>();
        return *scratchGraph_;
    }

    // ---- MIDI and audio clips ------------------------------------------------------------
    [[nodiscard]] std::optional<Engine::MidiTrackInfo> midiInfo(const std::uint64_t id) const
    {
        for (const auto& info : engine_.midiTracks())
            if (info.id == id)
                return info;
        return std::nullopt;
    }

    [[nodiscard]] static std::optional<Engine::MidiClip> midiClipNamed(const Engine::MidiTrackInfo& info,
                                                                       const juce::String& clipName)
    {
        if (info.clips.empty())
            return std::nullopt;
        if (clipName.isEmpty())
            return info.clips.front();
        for (const auto& clip : info.clips)
            if (clip.name.equalsIgnoreCase(clipName))
                return clip;
        return std::nullopt;
    }

    [[nodiscard]] juce::var describeAudioClips(const std::size_t index) const
    {
        juce::Array<juce::var> clips;
        if (const auto* list = engine_.audioClipsView(index))
            for (const auto& clip : *list) {
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
                set(entry, "sourceDuration", clip.sourceDurationSeconds);
                set(entry, "source", clip.sourceFile.getFullPathName());
                set(entry, "stretchMode", stretchModeName(clip.stretchMode));
                set(entry, "sourceTempo", clip.sourceTempoBpm);
                set(entry, "pitch", clip.pitchSemitones);
                set(entry, "takes", static_cast<int>(clip.takes.size()));
                set(entry, "activeTake", static_cast<int>(clip.activeTakeIndex));
                set(entry, "compPassages", static_cast<int>(clip.comp.size()));
                set(entry, "versions", static_cast<int>(clip.versions.size()));
                set(entry, "version", static_cast<int>(clip.version));
                clips.add(entry);
            }
        // The launcher's slots, by their scene.
        juce::Array<juce::var> launcher;
        for (const auto& clip : engine_.launcherClips(index)) {
            auto entry = object();
            set(entry, "scene", clip.sceneIndex);
            set(entry, "name", clip.name);
            set(entry, "duration", clip.durationSeconds);
            set(entry, "offset", clip.sourceOffsetSeconds);
            set(entry, "looping", clip.looping);
            set(entry, "loopStart", clip.loopStartSeconds);
            set(entry, "loopEnd", clip.loopEndSeconds);
            set(entry, "source", clip.sourceFile.getFullPathName());
            set(entry, "events", static_cast<int>(clip.usesEventTimeline ? clip.events.size() : 1));
            set(entry, "usesEvents", clip.usesEventTimeline);
            auto points = 0;
            for (const auto& event : clip.events)
                points += static_cast<int>(event.stretchMarkers.size());
            set(entry, "stretchPoints", points);
            set(entry, "playStart", clip.playStartSeconds);
            set(entry, "playEnd", clip.playEndSeconds);
            // Its sounds: their ids (clip_lanes event), modes and own lanes.
            juce::Array<juce::var> sounds;
            for (const auto& event : clip.events) {
                auto sound = object();
                set(sound, "id", idOf(event.id));
                set(sound, "name", event.name);
                set(sound, "start", event.startSeconds);
                set(sound, "duration", event.durationSeconds);
                set(sound, "stretchMode", stretchModeName(event.stretchMode));
                if (!event.lanes.empty())
                    set(sound, "lanes", lanesToVar(event.lanes));
                sounds.add(sound);
            }
            set(entry, "sounds", sounds);
            launcher.add(entry);
        }
        auto result = object();
        set(result, "clips", clips);
        set(result, "launcherClips", launcher);
        return result;
    }

    // The playing modes by name, and back (anything else: `fallback`).
    static juce::String stretchModeName(const Engine::StretchMode mode)
    {
        static const char* const names[] = { "repitch", "tonal", "beats", "texture", "raw", "complex" };
        return names[std::clamp(static_cast<int>(mode), 0, Engine::stretchModeCount - 1)];
    }
    static Engine::StretchMode stretchModeNamed(const juce::String& text, const Engine::StretchMode fallback)
    {
        const auto name = text.trim().toLowerCase();
        for (auto mode = 0; mode < Engine::stretchModeCount; ++mode)
            if (name == stretchModeName(static_cast<Engine::StretchMode>(mode)))
                return static_cast<Engine::StretchMode>(mode);
        return fallback;
    }
    // Lanes as {name: [[seconds, value], ...]}.
    static juce::var lanesToVar(const std::map<juce::String, std::vector<Engine::AutomationPoint>>& lanes)
    {
        auto shown = object();
        for (const auto& [name, points] : lanes) {
            juce::Array<juce::var> list;
            for (const auto& point : points)
                list.add(juce::Array<juce::var> { point.timeSeconds, static_cast<double>(point.valueDb) });
            set(shown, name.toRawUTF8(), list);
        }
        return shown;
    }

    [[nodiscard]] juce::var describeAutomation(const RemoteTrack& track) const
    {
        juce::Array<juce::var> entries;
        for (const auto& point : studio_.automationPoints(track.ref())) {
            auto entry = object();
            set(entry, "id", idOf(point.id));
            set(entry, "time", point.timeSeconds);
            set(entry, "valueDb", point.valueDb);
            describeShape(entry, point);
            entries.add(entry);
        }
        auto result = object();
        set(result, "points", entries);
        return result;
    }

    // A point's shape when it has one: shape (0 straight, 1 bent, 2 a
    // B-spline's, 3 a Bezier one with one tangent, 4 with two), bend, and
    // handles [inTime, inValue, outTime, outValue].
    static void describeShape(juce::var& entry, const Studio::AutomationPoint& point)
    {
        if (point.curve != Studio::AutomationCurve::linear) {
            set(entry, "shape", static_cast<int>(point.curve));
            set(entry, "bend", point.curveAmount);
        }
        if (point.handles != engine::CurveHandles {}) {
            juce::Array<juce::var> handles;
            for (const auto value : { point.handles.inTime, point.handles.inValue, point.handles.outTime, point.handles.outValue })
                handles.add(static_cast<double>(value));
            set(entry, "handles", handles);
        }
    }

    // The shapes a request's points asked for, put on the points just added.
    void applyShapes(const TrackRef& ref, const juce::String& lane, const std::vector<std::pair<std::uint64_t, juce::var>>& shaped)
    {
        if (shaped.empty())
            return;
        auto points = studio_.lanePoints(ref, lane);
        for (auto& point : points)
            for (const auto& [id, spec] : shaped)
                if (point.id == id) {
                    if (spec.hasProperty("shape"))
                        point.curve = static_cast<Studio::AutomationCurve>(engine::clampedCurveShape(static_cast<int>(spec.getProperty("shape", 0))));
                    if (spec.hasProperty("bend"))
                        point.curveAmount = juce::jlimit(-1.0f, 1.0f, static_cast<float>(static_cast<double>(spec.getProperty("bend", 0.0))));
                    if (const auto* handles = spec.getProperty("handles", juce::var()).getArray(); handles != nullptr && handles->size() >= 4)
                        point.handles = { static_cast<float>(static_cast<double>((*handles)[0])), static_cast<float>(static_cast<double>((*handles)[1])),
                                          static_cast<float>(static_cast<double>((*handles)[2])), static_cast<float>(static_cast<double>((*handles)[3])) };
                }
        studio_.setLanePoints(ref, lane, points);
    }

    [[nodiscard]] static bool asksShape(const juce::var& point)
    {
        return point.hasProperty("shape") || point.hasProperty("handles") || point.hasProperty("bend");
    }

    // ---- The commands -------------------------------------------------------------------
    juce::var onPing() const;
    juce::var onOpenProject();
    juce::var onSaveProject();
    juce::var onTemplates();
    juce::var onTransport();
    juce::var onSetTempo();
    juce::var onSetLoop();
    juce::var onSetMasterGain();
    juce::var onAddMidiTrack();
    juce::var onAddAudioTrack();
    juce::var onSetTrack();
    juce::var onNotes();
    juce::var onListDevices();
    juce::var onInsertDevice();
    juce::var onParameters();
    juce::var onSetView();
    juce::var onSnapshot();
    juce::var onExportMix();
    juce::var onQuit();
    juce::var onImportProject();
    juce::var onNewProject();
    juce::var onRemoveTrack();
    juce::var onGetTrackGraph();
    juce::var onSoundMatching();
    juce::var onSampler();
    juce::var onBanks();
    juce::var onPresets();
    juce::var onPrism();
    juce::var onKick();
    juce::var onSynth();
    juce::var onBatterie();
    juce::var onGroove();
    juce::var onLocations();
    juce::var onSlotStop();
    juce::var onMasterClip();
    juce::var onRemoveDevice();
    juce::var onGraphDevices();
    juce::var onWorkshop();
    [[nodiscard]] juce::var workshopStatus();
    // The graph a graph command edits: the workshop's device ("track":
    // "workshop" or "atelier"), a track's or a bus's. The answer to give
    // when there is none.
    juce::var targetOrFail(std::optional<GraphTarget>& target);
    juce::var onEditNotes();
    juce::var onRemoveClip();
    juce::var onAudioClips();
    juce::var onClipLanes();
    juce::var onAutomation();
    juce::var onModulate();
    juce::var onLaunchScene();
    juce::var onVoiceLive();
    juce::var onLaunchClip();
    juce::var onArm();
    juce::var onRecord();
    juce::var onUndoRedo();
    juce::var onMoveTrack();
    juce::var onAddMidiClip();
    juce::var onDescribeNodeType();
    juce::var onModulator();
    juce::var onContainer();
    juce::var onNodes();
    juce::var onListPlugins();
    juce::var onPlugins();
    juce::var onEditAudioClip();
    juce::var onListBuses();
    juce::var onBuses();
    juce::var onAudioSettings();
    juce::var onImportMidi();
    juce::var onExportMidi();
    juce::var onSetLaunchQuantization();
    juce::var onSamples();

    Studio& studio_;
    Engine& engine_;
    RemoteUi& ui_;
    const juce::var& request_;
    const juce::String command_;
    std::unique_ptr<app::NodeGraphComponent> scratchGraph_;
};

juce::var Commands::run()
{
    const auto& command = command_;
    if (command == "ping")
        return onPing();
    if (command == "status")
        return ok(status());
    // While a project opens, the session is not whole: only looking at it.
    if (studio_.openingProject() && command != "snapshot" && command != "quit" && command != "ui_input")
        return fail("a project is opening; try again once it is open (status: opening)");
    if (command == "open_project")
        return onOpenProject();
    if (command == "save_project")
        return onSaveProject();
    if (command == "list_templates" || command == "load_template")
        return onTemplates();
    if (command == "transport")
        return onTransport();
    if (command == "set_tempo")
        return onSetTempo();
    if (command == "set_loop")
        return onSetLoop();
    if (command == "set_master_gain")
        return onSetMasterGain();
    if (command == "add_midi_track")
        return onAddMidiTrack();
    if (command == "add_audio_track")
        return onAddAudioTrack();
    if (command == "add_bus_track") {
        // A bus: a track without clips that others are routed into.
        const auto track = studio_.addBusTrack(text("name"));
        if (!track)
            return fail("the bus could not be made");
        ui_.selectTrack(*track);
        auto result = object();
        set(result, "index", static_cast<int>(track->audioIndex));
        set(result, "name", studio_.trackName(*track));
        return ok(result);
    }
    if (command == "show_dialog") {
        // {name: "audio" | "soundcard" | "companion" | "sampler"}: a settings window opened, to
        // look at (sampler: the selected track's zone editor).
        const auto name = text("name");
        if (!ui_.showDialog(name))
            return fail("no dialog \"" + name + "\": audio, soundcard, companion, locations, banks, semantic, sampler, prism, kick "
                        "(the last three: the selected track's)");
        return ok(object());
    }
    if (command == "recreate_sound" || command == "sound_matching" || command == "load_sound_model"
        || command == "companion_info" || command == "readable_patch")
        return onSoundMatching();
    if (command == "factory_instruments" || command == "add_factory_instrument" || command == "new_sampler"
        || command == "load_sampler" || command == "sampler_map" || command == "sampler_status"
        || command == "sampler_file" || command == "sampler_controls")
        return onSampler();
    if (command == "banks")
        return onBanks();
    if (command == "presets")
        return onPresets();
    if (command == "prism")
        return onPrism();
    if (command == "kick")
        return onKick();
    if (command == "synth")
        return onSynth();
    if (command == "batterie")
        return onBatterie();
    if (command == "instrument_choices") {
        // What a pad, a layer or a Replacer can play: CyDAW's instruments
        // (catalogue keys), the factory's ("factory:<key>", with their
        // category, and whether their samples are still to download).
        juce::Array<juce::var> listed;
        for (const auto& choice : studio_.instrumentChoices()) {
            auto entry = object();
            set(entry, "key", choice.key);
            set(entry, "name", choice.name);
            set(entry, "category", choice.category);
            set(entry, "missing", choice.missing);
            listed.add(entry);
        }
        auto result = object();
        set(result, "instruments", listed);
        return ok(result);
    }
    if (command == "rack") {
        // {track, action: "list" | "add" | "instrument" | "mix" | "sample" | "remove" | "open" | "close", note, pad,
        // instrument, file, name, playAs, gainDb, pan, mute, solo, choke, keyLow, keyHigh, velocityLow,
        // velocityHigh}: an instrument container's pads (Drum Machine) or layers (Instrument Layer,
        // Selector: by "pad", their place; "add" makes the next). An instrument is a catalogue key or
        // "factory:<key>". Answers them all, the container's kind and the layer a selector plays.
        std::optional<RemoteTrack> track;
        if (auto error = trackOrFail(track); !error.isVoid())
            return error;
        const auto ref = track->ref();
        if (!ref.midi)
            return fail("a rack is an instrument track's");
        const auto action = text("action").isNotEmpty() ? text("action") : juce::String("list");
        const auto note = static_cast<int>(number("note", 36.0));
        std::optional<std::size_t> pad;
        if (has("pad"))
            pad = static_cast<std::size_t>(number("pad", 0.0));
        else
            pad = studio_.rackPadOfNote(ref, note);
        juce::String error;
        if (action == "add") {
            if (!studio_.addRackPad(ref, note, text("instrument"), error))
                return fail(error);
        } else if (action == "instrument") {
            if (!pad || !studio_.setRackPadInstrument(ref, *pad, text("instrument"), error))
                return fail(error.isNotEmpty() ? error : juce::String("no pad there"));
        } else if (action == "sample") {
            if (!studio_.loadRackPadSample(ref, note, fileFor(text("file")), error))
                return fail(error);
        } else if (action == "remove") {
            if (!pad || !studio_.removeRackPad(ref, *pad))
                return fail("no pad there");
        } else if (action == "mix") {
            const auto* current = pad ? studio_.rackPad(ref, *pad) : nullptr;
            if (current == nullptr)
                return fail("no pad there");
            auto mix = Studio::mixOf(*current);
            if (has("newNote"))
                mix.note = static_cast<int>(number("newNote", mix.note));
            if (has("name"))
                mix.name = text("name");
            if (has("playAs"))
                mix.playAs = static_cast<int>(number("playAs", mix.playAs));
            if (has("gainDb"))
                mix.gainDb = static_cast<float>(number("gainDb", mix.gainDb));
            if (has("pan"))
                mix.pan = static_cast<float>(number("pan", mix.pan));
            if (has("mute"))
                mix.muted = flag("mute", mix.muted);
            if (has("solo"))
                mix.solo = flag("solo", mix.solo);
            if (has("choke"))
                mix.choke = static_cast<int>(number("choke", mix.choke));
            if (has("keyLow"))
                mix.keyLow = static_cast<int>(number("keyLow", mix.keyLow));
            if (has("keyHigh"))
                mix.keyHigh = static_cast<int>(number("keyHigh", mix.keyHigh));
            if (has("velocityLow"))
                mix.velocityLow = static_cast<int>(number("velocityLow", mix.velocityLow));
            if (has("velocityHigh"))
                mix.velocityHigh = static_cast<int>(number("velocityHigh", mix.velocityHigh));
            (void) studio_.setRackPadMix(ref, *pad, mix);
        } else if (action == "open") {
            if (!pad || studio_.rackPad(ref, *pad) == nullptr)
                return fail("no pad there");
            ui_.selectTrack(ref);
            ui_.openRackPad(std::make_pair(ref.owner(), static_cast<int>(*pad)));
        } else if (action == "close") {
            ui_.openRackPad(std::nullopt);
        } else if (action != "list") {
            return fail("action is list, add, instrument, mix, sample, remove, open or close");
        }
        auto result = object();
        set(result, "rack", studio_.isRack(ref));
        set(result, "kind", juce::String(engine::instrument_container::name(studio_.containerKind(ref))));
        set(result, "padsKind", studio_.padsKindName(ref));
        set(result, "selected", studio_.selectedLayer(ref));
        juce::Array<juce::var> pads;
        for (std::size_t index = 0; index < studio_.rackPadCount(ref); ++index) {
            const auto* item = studio_.rackPad(ref, index);
            auto entry = object();
            set(entry, "pad", static_cast<int>(index));
            set(entry, "note", item->note);
            set(entry, "name", item->name);
            set(entry, "playAs", item->playAs);
            set(entry, "gainDb", item->gainDb);
            set(entry, "pan", item->pan);
            set(entry, "mute", item->muted);
            set(entry, "solo", item->solo);
            set(entry, "choke", item->choke);
            set(entry, "keyLow", item->keyLow);
            set(entry, "keyHigh", item->keyHigh);
            set(entry, "velocityLow", item->velocityLow);
            set(entry, "velocityHigh", item->velocityHigh);
            juce::Array<juce::var> devices;
            juce::Array<juce::var> cards;
            for (const auto& node : item->graph->graph().editorNodes())
                if (!node.boundary) {
                    devices.add(node.typeKey);
                    auto card = object();
                    set(card, "id", static_cast<juce::int64>(node.id));
                    set(card, "type", node.typeKey);
                    cards.add(card);
                }
            set(entry, "devices", devices);
            set(entry, "cards", cards);
            set(entry, "peak", studio_.rackPadPeak(ref, index));
            pads.add(juce::var(entry));
        }
        set(result, "pads", pads);
        return ok(result);
    }
    if (command == "groove")
        return onGroove();
    if (command == "locations")
        return onLocations();
    if (command == "instrument_export_check") {
        // {track, note, velocity, seconds}: the instrument as an exported
        // plug-in gets it — the state written into its project, restored as
        // its setStateInformation restores it (the plan, then the model and
        // the table from the graph), a note played offline by its renderer.
        // No device, no sound, no build.
        std::optional<RemoteTrack> track;
        if (auto error = trackOrFail(track); !error.isVoid())
            return error;
        const auto* slot = studio_.graphFor(track->ref());
        if (slot == nullptr)
            return fail("that track has no graph");
        const auto state = slot->graph().createVst3RuntimeState();
        // "stateOut": the state itself, for a host to hand to the plug-in.
        if (has("stateOut") && !fileFor(text("stateOut")).replaceWithText(juce::JSON::toString(state, true)))
            return fail("cannot write " + text("stateOut"));
        juce::String error;
        // As the plug-in's setStateInformation: the chain after the voices
        // first (it stops there when that does not restore or compile).
        const auto native = app::restoreNativeDspGraphState(state.getProperty("native", {}), error);
        if (!native)
            return fail("the exported chain does not restore: " + error);
        {
            app::NativeDspRuntime chain;
            chain.prepare(48'000.0, 512, 2);
            if (const auto compiled = chain.setGraph(*native); compiled.failed())
                return fail("the exported chain does not compile: " + compiled.getErrorMessage());
        }
        auto plan = app::restorePolySynthGraphPlan(state.getProperty("polySynth", {}), error);
        if (!plan)
            return fail("the exported plan does not restore: " + error);
        app::attachVoiceAssets(*plan, state.getProperty("editor", {}));
        // A sampler's samples are decoded in the background: the plug-in
        // waits for them as it restores, so does this.
        const auto samplesReady = plan->sampler == nullptr
                                  || app::SampleStore::instance().waitUntilReady(*plan->sampler, 60'000);
        constexpr auto rate = 48'000.0;
        constexpr auto block = 512;
        const auto samples = static_cast<int>(juce::jlimit(0.05, 30.0, number("seconds", 1.0)) * rate);
        app::GraphPolySynthRenderer renderer;
        renderer.prepare(rate);
        renderer.setPlan(*plan);
        juce::AudioBuffer<float> audio(2, samples);
        audio.clear();
        juce::AudioBuffer<float> scratch(2, block);
        // "notes": [[note, velocity, seconds, length], ...] played at their
        // times (let go after `length` seconds when given); else the one note
        // at the start. "controllers": [[number, value, seconds], ...].
        struct Hit {
            int note {};
            float velocity {};
            int at {};
            int off { -1 };
        };
        std::vector<Hit> hits;
        if (const auto* list = request_.getProperty("notes", {}).getArray())
            for (const auto& item : *list)
                if (const auto* fields = item.getArray(); fields != nullptr && fields->size() >= 3) {
                    Hit hit { juce::jlimit(0, 127, static_cast<int>((*fields)[0])),
                              static_cast<float>(juce::jlimit(0.0, 1.0, static_cast<double>((*fields)[1]))),
                              static_cast<int>(juce::jmax(0.0, static_cast<double>((*fields)[2])) * rate) };
                    if (fields->size() >= 4)
                        hit.off = hit.at + static_cast<int>(juce::jmax(0.0, static_cast<double>((*fields)[3])) * rate);
                    hits.push_back(hit);
                }
        if (hits.empty())
            hits.push_back({ juce::jlimit(0, 127, static_cast<int>(number("note", 69.0))), static_cast<float>(number("velocity", 0.8)), 0 });
        std::vector<std::array<int, 3>> moves;
        if (const auto* list = request_.getProperty("controllers", {}).getArray())
            for (const auto& item : *list)
                if (const auto* fields = item.getArray(); fields != nullptr && fields->size() >= 3)
                    moves.push_back({ juce::jlimit(0, 127, static_cast<int>((*fields)[0])), juce::jlimit(0, 127, static_cast<int>((*fields)[1])),
                                      static_cast<int>(juce::jmax(0.0, static_cast<double>((*fields)[2])) * rate) });
        // Faster than real time: a lane waits for frames still on the disk.
        const juce::ScopedValueSetter<bool> mayWait(engine::samplerMayWait, true);
        for (auto start = 0; start < samples; start += block) {
            const auto count = std::min(block, samples - start);
            juce::MidiBuffer midi;
            for (const auto& [controller, value, at] : moves)
                if (at >= start && at < start + count)
                    midi.addEvent(juce::MidiMessage::controllerEvent(1, controller, value), at - start);
            for (const auto& hit : hits) {
                if (hit.at >= start && hit.at < start + count)
                    midi.addEvent(juce::MidiMessage::noteOn(1, hit.note, hit.velocity), hit.at - start);
                if (hit.off >= start && hit.off < start + count)
                    midi.addEvent(juce::MidiMessage::noteOff(1, hit.note), hit.off - start);
            }
            scratch.clear();
            renderer.render(scratch, midi, 0, count);
            for (auto channel = 0; channel < 2; ++channel)
                audio.copyFrom(channel, start, scratch, channel, 0, count);
        }
        const auto settle = std::min(samples / 4, static_cast<int>(rate * 0.1));
        auto result = object();
        set(result, "waveform", static_cast<int>(plan->waveform));
        set(result, "soundModel", plan->soundModel != nullptr);
        if (plan->soundModel != nullptr)
            set(result, "description", app::describeSoundModel(*plan->soundModel));
        set(result, "wavetable", plan->wavetable != nullptr);
        set(result, "sampler", plan->sampler != nullptr);
        if (plan->sampler != nullptr) {
            set(result, "samplerZones", static_cast<int>(plan->sampler->zones.size()));
            set(result, "samplesReady", samplesReady);
            juce::Array<juce::var> samplerValues;
            for (std::size_t index = 0; index < cydaw::engine::samplerSettingCount; ++index)
                samplerValues.add(plan->samplerValues[index]);
            set(result, "samplerValues", samplerValues);
            set(result, "rmsRight", audio.getRMSLevel(1, settle, samples - settle));
        }
        set(result, "enabled", plan->enabled);
        juce::Array<juce::var> values;
        for (std::size_t index = 0; index < cydaw::engine::soundModelSettingCount; ++index)
            values.add(plan->soundModelValues[index]);
        set(result, "soundModelValues", values);
        set(result, "rms", audio.getRMSLevel(0, settle, samples - settle));
        // The last half second: a held note still sounding (a loop) or not.
        const auto tail = std::min(samples, static_cast<int>(rate * 0.5));
        set(result, "tailRms", audio.getRMSLevel(0, samples - tail, tail));
        set(result, "peakDb", juce::Decibels::gainToDecibels(audio.getMagnitude(0, 0, samples), -120.0f));
        // Its level every 50 ms (dB RMS): what happens over time.
        juce::Array<juce::var> windows;
        const auto window = static_cast<int>(rate * 0.05);
        for (auto at = 0; at + window <= samples; at += window)
            windows.add(juce::Decibels::gainToDecibels(audio.getRMSLevel(0, at, window), -120.0f));
        set(result, "windowsDb", windows);
        set(result, "stateBytes", static_cast<juce::int64>(juce::JSON::toString(state, true).getNumBytesAsUTF8()));
        return ok(result);
    }
    if (command == "track_modulation") {
        // {track, lfo: {rate, depth, offset, shape}, assign: {key: amount | false}}:
        // what moves the track's parameters. Its value graph (an LFO into its
        // output by default; edit it with the graph commands and "graph":
        // "modulation"), each parameter's assignment (a key as list_parameters
        // gives it, or its binding; an amount -1..1 enables it, false turns it
        // off). Answers the LFO, the output now, whether it runs, the targets.
        std::optional<RemoteTrack> track;
        if (auto error = trackOrFail(track); !error.isVoid())
            return error;
        const auto ref = track->ref();
        if (studio_.modulationGraphFor(ref) == nullptr)
            return fail("only an audio track has a modulation");
        if (const auto* lfo = request_.getProperty("lfo", {}).getDynamicObject()) {
            auto settings = studio_.modulationLfo(ref);
            const auto read = [lfo](const char* name, const double fallback) {
                const auto value = lfo->getProperty(name);
                return value.isVoid() ? fallback : static_cast<double>(value);
            };
            settings.rate = read("rate", settings.rate);
            settings.depth = read("depth", settings.depth);
            settings.offset = read("offset", settings.offset);
            settings.shape = static_cast<int>(std::lround(read("shape", settings.shape)));
            studio_.setModulationLfo(ref, settings);
        }
        juce::StringArray problems;
        if (const auto* assign = request_.getProperty("assign", {}).getDynamicObject())
            for (const auto& property : assign->getProperties()) {
                const auto key = property.name.toString();
                juce::String binding;
                for (const auto& target : studio_.modulationTargets(ref))
                    if (target.binding == key || target.binding.endsWith("." + key.fromFirstOccurrenceOf(".", false, false))
                        || target.binding.endsWith(key))
                        binding = target.binding;
                if (binding.isEmpty()) {
                    problems.add("no parameter \"" + key + "\" to modulate");
                    continue;
                }
                const auto off = property.value.isBool() && !static_cast<bool>(property.value);
                studio_.setModulationAssignment(ref, binding, !off, off ? 0.25 : static_cast<double>(property.value));
            }
        auto result = object();
        const auto lfo = studio_.modulationLfo(ref);
        auto settings = object();
        set(settings, "found", lfo.found);
        set(settings, "rate", lfo.rate);
        set(settings, "depth", lfo.depth);
        set(settings, "offset", lfo.offset);
        set(settings, "shape", lfo.shape);
        set(result, "lfo", settings);
        set(result, "output", studio_.modulationOutput(ref));
        set(result, "active", studio_.modulationActive(ref));
        juce::Array<juce::var> targets;
        for (const auto& target : studio_.modulationTargets(ref)) {
            auto entry = object();
            set(entry, "binding", target.binding);
            set(entry, "label", target.label);
            set(entry, "enabled", target.enabled);
            set(entry, "amount", target.amount);
            targets.add(entry);
        }
        set(result, "targets", targets);
        if (!problems.isEmpty())
            set(result, "problems", strings(problems));
        return ok(result);
    }
    if (command == "cab_impulse") {
        // {track, nodeId?, file | clear: true}: a "Baffle et micro" card's
        // impulse response (or an Empreinte's, a place's) loaded from a sound
        // file, or taken out. Answers
        // what it holds: {name, channels, frames, sampleRate} or nothing.
        std::optional<RemoteTrack> track;
        if (auto error = trackOrFail(track); !error.isVoid())
            return error;
        auto* slot = studio_.graphFor(track->ref());
        if (slot == nullptr)
            return fail("that track has no graph");
        auto nodeId = static_cast<std::uint64_t>(static_cast<juce::int64>(request_.getProperty("nodeId", 0)));
        if (nodeId == 0)
            for (const auto& node : slot->graph().editorNodes())
                if (node.typeKey == "cab-sim" || node.typeKey == "empreinte") {
                    nodeId = node.id;
                    break;
                }
        if (nodeId == 0)
            return fail("no Baffle et micro or Empreinte card on that track");
        if (has("file")) {
            juce::String error;
            if (!studio_.loadCabImpulse(track->ref(), nodeId, fileFor(text("file")), error))
                return fail(error);
        } else if (flag("clear", false)) {
            (void) studio_.clearCabImpulse(track->ref(), nodeId);
        }
        auto result = object();
        set(result, "nodeId", static_cast<juce::int64>(nodeId));
        if (const auto response = slot->graph().nodeImpulse(nodeId)) {
            set(result, "name", juce::String(response->name));
            set(result, "channels", static_cast<int>(response->channels.size()));
            set(result, "frames", static_cast<juce::int64>(response->length()));
            set(result, "sampleRate", response->sampleRate);
        }
        return ok(result);
    }
    if (command == "sidechain") {
        // {track, nodeId?, source | clear: true}: what the detector of a
        // compressor or a gate listens to, another track routed into it, or
        // its own sound. Answers {nodeId, source (name), input (node)}.
        std::optional<RemoteTrack> track;
        if (auto error = trackOrFail(track); !error.isVoid())
            return error;
        auto* slot = studio_.graphFor(track->ref());
        if (slot == nullptr)
            return fail("that track has no graph");
        auto nodeId = static_cast<std::uint64_t>(static_cast<juce::int64>(request_.getProperty("nodeId", 0)));
        if (nodeId == 0)
            for (const auto& node : slot->graph().editorNodes())
                if (studio_.hasSidechain(track->ref(), node.id)) {
                    nodeId = node.id;
                    break;
                }
        if (nodeId == 0 || !studio_.hasSidechain(track->ref(), nodeId))
            return fail("no device with a sidechain (Compresseur Pro, Porte) on that track");
        if (has("source")) {
            const auto source = resolveTrack(request_.getProperty("source", juce::var()));
            if (!source)
                return fail("no such source track");
            if (const auto set = studio_.setSidechainSource(track->ref(), nodeId, source->ref()); set.failed())
                return fail(set.getErrorMessage());
        } else if (flag("clear", false)) {
            if (const auto set = studio_.setSidechainSource(track->ref(), nodeId, std::nullopt); set.failed())
                return fail(set.getErrorMessage());
        }
        auto result = object();
        set(result, "nodeId", static_cast<juce::int64>(nodeId));
        if (const auto source = studio_.sidechainSource(track->ref(), nodeId)) {
            set(result, "source", studio_.trackName(*source));
            for (const auto& output : studio_.trackOutputs(*source))
                if (output.destination == studio_.routingKey(track->ref()) && output.input != 0)
                    set(result, "input", static_cast<juce::int64>(output.input));
        }
        return ok(result);
    }
    if (command == "effect_chain_check") {
        // {track, hz, amplitude, seconds}: the track's effects as an exported
        // plug-in gets them (the state written, restored, compiled), a sine
        // through them offline. No device, no sound. Answers the levels in
        // and out (dB RMS), the peak, and the second and third harmonics
        // against the fundamental (dB).
        std::optional<RemoteTrack> track;
        if (auto error = trackOrFail(track); !error.isVoid())
            return error;
        auto* slot = studio_.graphFor(track->ref());
        if (slot == nullptr)
            return fail("that track has no graph");
        std::optional<engine::NativeDspGraph> native;
        // "live": the graph as the track plays it (a Replacer's instrument
        // with it), not as an exported plug-in restores it.
        if (flag("live", false)) {
            native = slot->graph().nativeDspGraph();
            studio_.attachReplacerInstruments(track->ref(), *native);
        } else {
            const auto state = slot->graph().createVst3RuntimeState();
            juce::String error;
            native = app::restoreNativeDspGraphState(state.getProperty("native", {}), error);
            if (!native)
                return fail("the chain does not restore: " + error);
            // A cabinet's response, as the plug-in puts it back; each
            // Replacer's instrument.
            app::attachImpulses(*native, state.getProperty("editor", {}));
            app::attachReplacerInstruments(*native, state, true);
        }
        constexpr auto rate = 48'000.0;
        constexpr auto block = 512;
        app::NativeDspRuntime chain;
        chain.prepare(rate, block, 2);
        if (const auto compiled = chain.setGraph(*native); compiled.failed())
            return fail("the chain does not compile: " + compiled.getErrorMessage());
        const auto hz = juce::jlimit(20.0, 20'000.0, number("hz", 220.0));
        const auto amplitude = juce::jlimit(0.0, 1.0, number("amplitude", 0.3));
        // "clicks": a click of `amplitude` every quarter of a second instead
        // of the sine (transients, for a Replacer).
        const auto clicks = text("signal").trim() == "clicks";
        const auto samples = static_cast<int>(juce::jlimit(0.1, 10.0, number("seconds", 1.0)) * rate);
        std::vector<float> out;
        out.reserve(static_cast<std::size_t>(samples));
        juce::AudioBuffer<float> buffer(2, block);
        for (auto start = 0; start < samples; start += block) {
            const auto count = std::min(block, samples - start);
            for (auto i = 0; i < count; ++i) {
                const auto value = clicks ? ((start + i) % static_cast<int>(rate / 4.0) < 4 ? static_cast<float>(amplitude) : 0.0f)
                                          : static_cast<float>(
                                                amplitude * std::sin(2.0 * juce::MathConstants<double>::pi * hz * (start + i) / rate));
                buffer.setSample(0, i, value);
                buffer.setSample(1, i, value);
            }
            (void) chain.process(buffer, 0, count);
            out.insert(out.end(), buffer.getReadPointer(0), buffer.getReadPointer(0) + count);
        }
        // Past the first tenth of a second: what it settles to.
        const auto from = static_cast<std::size_t>(rate * 0.1);
        const auto level = [&](const double frequency) {
            const auto omega = 2.0 * juce::MathConstants<double>::pi * frequency / rate;
            const auto coefficient = 2.0 * std::cos(omega);
            double previous {};
            double before {};
            for (auto i = from; i < out.size(); ++i) {
                const auto now = static_cast<double>(out[i]) + coefficient * previous - before;
                before = previous;
                previous = now;
            }
            const auto power = previous * previous + before * before - coefficient * previous * before;
            return 20.0 * std::log10(std::max(1.0e-12, 2.0 * std::sqrt(std::max(0.0, power))
                                                            / static_cast<double>(std::max<std::size_t>(1, out.size() - from))));
        };
        auto energy = 0.0;
        auto peak = 0.0f;
        for (auto i = from; i < out.size(); ++i) {
            energy += static_cast<double>(out[i]) * out[i];
            peak = std::max(peak, std::abs(out[i]));
        }
        auto result = object();
        set(result, "inputDb", 20.0 * std::log10(amplitude / std::sqrt(2.0) + 1.0e-12));
        set(result, "outputDb", 10.0 * std::log10(energy / static_cast<double>(std::max<std::size_t>(1, out.size() - from)) + 1.0e-20));
        set(result, "peakDb", juce::Decibels::gainToDecibels(peak, -120.0f));
        const auto fundamental = level(hz);
        set(result, "fundamentalDb", fundamental);
        set(result, "secondDb", level(hz * 2.0) - fundamental);
        set(result, "thirdDb", level(hz * 3.0) - fundamental);
        set(result, "nodes", static_cast<int>(chain.audioNodeCount()));
        return ok(result);
    }
    if (command == "render_sound_model") {
        // {model: .cysound, out: .wav, note, velocity, seconds, held, settings: {key: value}}:
        // the voice played offline into a file, no device, no sound.
        juce::var json;
        juce::String error;
        const auto model = app::loadSoundModelFile(fileFor(text("model")), json, error);
        if (model == nullptr)
            return fail(error);
        auto values = cydaw::engine::defaultSoundModelValues();
        if (const auto* settings = request_.getProperty("settings", {}).getDynamicObject())
            for (const auto& property : settings->getProperties()) {
                const auto index = cydaw::engine::soundModelSettingIndex(property.name.toString().toStdString());
                if (index < 0)
                    return fail("no sound model setting \"" + property.name.toString() + "\"");
                values[static_cast<std::size_t>(index)] = static_cast<float>(static_cast<double>(property.value));
            }
        constexpr auto rate = 48'000.0;
        const auto zone = model->zones.front();
        const auto note = number("note", static_cast<double>(zone.rootNote));
        const auto velocity = number("velocity", static_cast<double>(zone.velocity));
        const auto samples = static_cast<int>(juce::jlimit(0.01, 60.0, number("seconds", 2.0)) * rate);
        const auto held = has("held") ? static_cast<int>(number("held", 1.0) * rate) : samples;
        auto voice = std::make_unique<cydaw::engine::SoundModelVoice>();
        voice->prepare(rate);
        voice->start(*model, values, note, velocity, rate);
        // In stereo (a model analysed from a stereo recording keeps its image).
        juce::AudioBuffer<float> audio(2, samples);
        auto ended = -1;
        for (int index = 0; index < samples; ++index) {
            if (index == held)
                voice->release();
            auto left = 0.0;
            auto right = 0.0;
            voice->nextFrame(model.get(), values, rate, left, right);
            audio.setSample(0, index, static_cast<float>(left));
            audio.setSample(1, index, static_cast<float>(right));
            if (ended < 0 && !voice->active())
                ended = index;
        }
        const auto out = fileFor(text("out"));
        out.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream(out.createOutputStream().release());
        if (stream == nullptr)
            return fail("cannot write " + out.getFullPathName());
        auto writer = wav.createWriterFor(
            stream, juce::AudioFormatWriterOptions().withSampleRate(rate).withNumChannels(2).withBitsPerSample(24));
        if (writer == nullptr)
            return fail("cannot write " + out.getFullPathName());
        writer->writeFromAudioSampleBuffer(audio, 0, samples);
        writer.reset();
        auto result = object();
        set(result, "out", out.getFullPathName());
        set(result, "peakDb", juce::Decibels::gainToDecibels(audio.getMagnitude(0, 0, samples), -120.0f));
        set(result, "endedAt", ended >= 0 ? ended / rate : -1.0);
        set(result, "description", app::describeSoundModel(*model));
        return ok(result);
    }
    if (command == "add_track_input" || command == "remove_track_input") {
        // {track, name}: an audio input beside its main one (a sidechain),
        // or that input gone (by name or number) with what is routed into it.
        std::optional<RemoteTrack> track;
        if (auto error = trackOrFail(track); !error.isVoid())
            return error;
        const auto ref = track->ref();
        if (track->master || track->bus)
            return fail("only a track has inputs to add (not the master, not a virtual bus)");
        if (command == "add_track_input") {
            if (!studio_.addTrackInput(ref, text("name")))
                return fail("no input could be added (3 at most beside the main one)");
        } else {
            const auto input = inputOf(ref, request_.getProperty("input", request_.getProperty("name", juce::var())));
            if (!input || *input == 0)
                return fail("no such input; status lists them");
            if (const auto removed = studio_.removeTrackInput(ref, *input); removed.failed())
                return fail(removed.getErrorMessage());
        }
        auto result = object();
        set(result, "track", studio_.trackName(ref));
        set(result, "inputs", inputsSummary(ref));
        return ok(result);
    }
    if (command == "routing") {
        // {track, outputs: [{destination: "master" | a track's name or index,
        //  gainDb, input: the destination's input by name or number, preFader}
        //  or {hardware: an output bus's name or id, gainDb, preFader}]}
        std::optional<RemoteTrack> track;
        if (auto error = trackOrFail(track); !error.isVoid())
            return error;
        const auto ref = track->ref();
        if (const auto* list = request_.getProperty("outputs", juce::var()).getArray()) {
            std::vector<TrackOutput> outputs;
            for (const auto& item : *list) {
                TrackOutput output;
                output.gainDb = static_cast<float>(static_cast<double>(item.getProperty("gainDb", 0.0)));
                const auto destination = item.getProperty("destination", "master");
                if (const auto hardware = item.getProperty("hardware", juce::var()); !hardware.isVoid()) {
                    const auto spec = hardware.toString().trim();
                    for (const auto& bus : engine_.virtualBuses())
                        if (bus.direction == app::VirtualBusDirection::output
                            && (bus.name.equalsIgnoreCase(spec) || juce::String(static_cast<juce::int64>(bus.id)) == spec))
                            output.hardware = bus.id;
                    if (output.hardware == 0)
                        return fail("no output bus \"" + spec + "\"; list_buses lists them");
                    output.hardwareAfterEffects = static_cast<bool>(item.getProperty("hardwareAfterEffects", false));
                } else if (!destination.toString().equalsIgnoreCase("master")) {
                    const auto resolved = resolveTrack(destination);
                    if (!resolved || resolved->bus || resolved->master)
                        return fail("no track \"" + destination.toString() + "\" to route into");
                    output.destination = studio_.routingKey(resolved->ref());
                    const auto input = inputOf(resolved->ref(), item.getProperty("input", juce::var()));
                    if (!input)
                        return fail("\"" + destination.toString() + "\" has no input \"" + item.getProperty("input", "").toString()
                                    + "\"; status lists its inputs");
                    output.input = *input;
                }
                output.preFader = static_cast<bool>(item.getProperty("preFader", false));
                outputs.push_back(output);
            }
            if (const auto applied = studio_.setTrackOutputs(ref, std::move(outputs)); applied.failed())
                return fail(applied.getErrorMessage());
        }
        auto result = object();
        set(result, "track", studio_.trackName(ref));
        set(result, "outputs", outputsSummary(ref));
        juce::StringArray senders;
        for (const auto& sender : studio_.trackSenders(ref))
            senders.add(studio_.trackName(sender));
        set(result, "senders", strings(senders));
        set(result, "bus", studio_.isBusTrack(ref));
        set(result, "inputs", inputsSummary(ref));
        return ok(result);
    }
    if (command == "set_track")
        return onSetTrack();
    if (command == "add_notes" || command == "list_notes" || command == "clear_notes")
        return onNotes();
    if (command == "list_devices")
        return onListDevices();
    if (command == "insert_device")
        return onInsertDevice();
    if (command == "list_parameters" || command == "set_parameter")
        return onParameters();
    if (command == "set_view")
        return onSetView();
    if (command == "set_language") {
        // The interface and the engine's texts together, as the menu does.
        const auto code = text("code").trim();
        if (code.isEmpty() || !app::Localization::instance().setLanguage(code))
            return fail("no language \"" + code + "\"; status lists them");
        ui::setLanguage(code);
        studio_.refreshTranslations();
        return ok(status());
    }
    if (command == "value_list" && has("plugin")) {
        // {track, plugin, open}: a plug-in card's list of values (every
        // parameter, searched, the last touched first).
        std::optional<RemoteTrack> track;
        if (auto error = trackOrFail(track); !error.isVoid())
            return error;
        const auto ref = track->ref();
        const auto plugin = static_cast<std::size_t>(number("plugin", 0.0));
        if (ref.midi || ref.master || plugin >= studio_.engine().trackPlugins(ref.audioIndex).size())
            return fail("no plug-in " + juce::String(static_cast<int>(plugin)) + " on that audio track");
        if (!ui_.showPluginValueList(ref, plugin, flag("open", true)))
            return fail("this interface shows no lists of values");
        auto result = object();
        set(result, "open", flag("open", true));
        return ok(result);
    }
    if (command == "value_list") {
        // {track, nodeId, open}: a card's list of values in the devices' strip
        // (with "plugin" in place of nodeId: a plug-in card's).
        std::optional<GraphTarget> target;
        if (auto error = targetOrFail(target); !error.isVoid())
            return error;
        if (!target->track)
            return fail("a list of values belongs to a track's card");
        const auto nodeId = static_cast<std::uint64_t>(number("nodeId", 0.0));
        if (!ui_.showValueList(*target->track, nodeId, flag("open", true)))
            return fail("this interface shows no lists of values");
        // Its values as the list shows them: the touched first (the last at
        // the top), then the others; "filter" as its search.
        auto& graph = target->slot->graph();
        const auto nodes = graph.editorNodes();
        const auto node = std::find_if(nodes.begin(), nodes.end(), [nodeId](const auto& item) { return item.id == nodeId; });
        if (node == nodes.end())
            return fail("no card " + juce::String(static_cast<juce::int64>(nodeId)) + " on that track");
        const auto filter = text("filter").trim();
        std::vector<std::pair<std::uint64_t, const app::NodeGraphComponent::EditorParameter*>> order;
        for (const auto& parameter : node->parameters)
            if ((filter.isEmpty() || parameter.label.containsIgnoreCase(filter) || parameter.binding.containsIgnoreCase(filter))
                && !parameter.binding.endsWith(".ownerLow") && !parameter.binding.endsWith(".ownerHigh"))
                order.emplace_back(graph.editorParameterTouch(parameter.binding), &parameter);
        std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        juce::Array<juce::var> values;
        for (const auto& [touch, parameter] : order) {
            auto entry = object();
            set(entry, "binding", parameter->binding);
            set(entry, "label", parameter->label);
            set(entry, "value", parameter->value);
            set(entry, "unit", parameter->unit);
            set(entry, "touched", touch != 0);
            values.add(entry);
        }
        auto result = object();
        set(result, "values", values);
        return ok(result);
    }
    if (command == "modulator")
        return onModulator();
    if (command == "container")
        return onContainer();
    if (command == "ui_spots") {
        // {record, filter}: where the knobs (knob:<binding>) and the
        // modulators' arrows (modulator-arrow:<id>) were last drawn, in the
        // coordinates ui_input takes; record false stops recording.
        ui::setSpotRecording(flag("record", true));
        const auto filter = text("filter");
        juce::Array<juce::var> spots;
        for (const auto& spot : ui::recordedSpots()) {
            if (filter.isNotEmpty() && !juce::String(spot.key).contains(filter))
                continue;
            auto entry = object();
            set(entry, "key", juce::String(spot.key));
            set(entry, "x", (spot.min.x + spot.max.x) * 0.5f);
            set(entry, "y", (spot.min.y + spot.max.y) * 0.5f);
            set(entry, "width", spot.max.x - spot.min.x);
            set(entry, "height", spot.max.y - spot.min.y);
            spots.add(entry);
        }
        auto result = object();
        set(result, "spots", spots);
        return ok(result);
    }
    if (command == "device_window") {
        // {track ("master" too), nodeId (0: the track's whole graph), open}
        std::optional<GraphTarget> target;
        if (auto error = targetOrFail(target); !error.isVoid())
            return error;
        if (!target->track)
            return fail("a device window belongs to a track's device");
        if (!ui_.showDeviceWindow(*target->track, static_cast<std::uint64_t>(number("nodeId", 0.0)), flag("open", true)))
            return fail("this interface shows no device windows");
        return ok(graphSummary(*target));
    }
    if (command == "layout") {
        // {"action": "list" | "save" | "load" | "delete", "name"}
        const auto action = text("action").trim().toLowerCase();
        const auto name = text("name").trim();
        if (action == "save" || action == "load") {
            const auto done = action == "save" ? ui_.saveLayoutAs(name) : ui_.loadLayout(name);
            if (done.failed())
                return fail(done.getErrorMessage());
        } else if (action == "delete") {
            if (!ui_.deleteLayout(name))
                return fail("no layout \"" + name + "\"");
        } else if (action.isNotEmpty() && action != "list") {
            return fail("action: list, save, load or delete");
        }
        auto result = object();
        set(result, "layouts", strings(ui_.layoutNames()));
        return ok(result);
    }
    if (command == "ui_input") {
        if (const auto error = ui_.injectInput(request_.getProperty("events", juce::var())); error.isNotEmpty())
            return fail(error);
        return ok(status());
    }
    if (command == "snapshot")
        return onSnapshot();
    if (command == "export_mix")
        return onExportMix();
    if (command == "quit")
        return onQuit();
    if (command == "import_project")
        return onImportProject();
    if (command == "new_project")
        return onNewProject();
    if (command == "remove_track")
        return onRemoveTrack();
    if (command == "get_track_graph")
        return onGetTrackGraph();
    if (command == "remove_device")
        return onRemoveDevice();
    if (command == "create_device" || command == "list_my_devices" || command == "insert_my_device")
        return onGraphDevices();
    if (command == "workshop")
        return onWorkshop();
    if (command == "update_notes" || command == "remove_notes" || command == "set_midi_clip")
        return onEditNotes();
    if (command == "remove_clip")
        return onRemoveClip();
    if (command == "list_audio_clips" || command == "add_audio_clip" || command == "move_audio_clip"
        || command == "duplicate_audio_clip")
        return onAudioClips();
    if (command == "clip_lanes")
        return onClipLanes();
    if (command == "list_automation" || command == "add_automation" || command == "clear_automation")
        return onAutomation();
    if (command == "modulate" || command == "remove_modulation")
        return onModulate();
    if (command == "launch_scene" || command == "stop_launcher")
        return onLaunchScene();
    if (command == "voice_live")
        return onVoiceLive();
    if (command == "slot_stop")
        return onSlotStop();
    if (command == "jsfx" || command == "dj" || command == "foundry" || command == "cinema" || command == "transport_sync"
        || command == "controllers") {
        // Their own actions ({"action": ...}); app_mode shows them.
        const auto answer = command == "jsfx" ? ui_.jsfx(request_) : command == "cinema" ? ui_.cinema(request_) : command == "transport_sync" ? ui_.transportSync(request_) : command == "controllers" ? ui_.controllers(request_) : command == "dj" ? ui_.dj(request_) : ui_.foundry(request_);
        if (!answer.isObject())
            return fail(answer.isString() ? answer.toString() : juce::String("no interface"));
        return ok(answer);
    }
    if (command == "app_mode") {
        const auto mode = ui_.appMode(text("mode").trim().toLowerCase());
        if (mode.isEmpty())
            return fail("mode: studio, dj, foundry, cinema or workshop");
        auto result = object();
        set(result, "mode", mode);
        return ok(result);
    }
    if (command == "theme") {
        const auto answer = ui_.themes(request_);
        if (!answer.isObject())
            return fail(answer.isString() ? answer.toString() : juce::String("no interface"));
        return ok(answer);
    }
    if (command == "computer_keyboard" || command == "midi_inputs") {
        const auto answer = ui_.midiInputs(command, request_);
        if (!answer.isObject())
            return fail(answer.isString() ? answer.toString() : juce::String("no interface"));
        return ok(answer);
    }
    if (command == "master_clip")
        return onMasterClip();
    if (command == "launch_clip" || command == "stop_track")
        return onLaunchClip();
    if (command == "arm")
        return onArm();
    if (command == "record")
        return onRecord();
    if (command == "undo" || command == "redo")
        return onUndoRedo();
    if (command == "move_track")
        return onMoveTrack();
    if (command == "add_midi_clip")
        return onAddMidiClip();
    if (command == "describe_node_type")
        return onDescribeNodeType();
    if (command == "describe_node" || command == "add_node" || command == "connect" || command == "disconnect"
        || command == "set_node" || command == "set_node_parameter" || command == "expose_parameter"
        || command == "add_comment" || command == "remove_comment" || command == "move_device")
        return onNodes();
    if (command == "list_plugins")
        return onListPlugins();
    if (command == "add_plugin" || command == "remove_plugin" || command == "move_plugin"
        || command == "list_plugin_programs" || command == "select_plugin_program"
        || command == "list_plugin_parameters" || command == "set_plugin_parameter" || command == "show_plugin_editor")
        return onPlugins();
    if (command == "set_audio_clip" || command == "stretch_audio_clip" || command == "split_audio_clip"
        || command == "crossfade_audio_clip" || command == "set_audio_clip_bounds" || command == "range_edit"
        || command == "edit_audio" || command == "copy_audio" || command == "audio_clip_history" || command == "audio_takes"
        || command == "audio_events")
        return onEditAudioClip();
    if (command == "list_buses")
        return onListBuses();
    if (command == "add_bus" || command == "set_bus" || command == "remove_bus")
        return onBuses();
    if (command == "audio_settings" || command == "set_audio_device" || command == "device_channels")
        return onAudioSettings();
    if (command == "import_midi")
        return onImportMidi();
    if (command == "export_midi")
        return onExportMidi();
    if (command == "set_launch_quantization")
        return onSetLaunchQuantization();
    if (command == "samples_status" || command == "sample_folders" || command == "scan_samples"
        || command == "search_samples" || command == "classify_samples" || command == "set_sample"
        || command == "sample_cache" || command == "select_sample" || command == "preview_sample"
        || command == "ai_models" || command == "semantic_index" || command == "browse_samples"
        || command == "library_window" || command == "library_perf")
        return onSamples();
    return fail("unknown command \"" + command + "\"");
}

// ---- Application and project --------------------------------------------------------------

juce::var Commands::onPing() const
{
    auto result = object();
    set(result, "app", "CyDAW");
    set(result, "interface", "CyDAW 2");
    if (auto* application = juce::JUCEApplicationBase::getInstance())
        set(result, "version", application->getApplicationVersion());
    return ok(result);
}

juce::var Commands::onOpenProject()
{
    const auto file = fileFor(text("path"));
    if (!file.existsAsFile() || !file.hasFileExtension("cydaw"))
        return fail("no .cydaw project at " + file.getFullPathName());
    if (studio_.isRecording())
        (void) studio_.stopRecording(false);
    // "background": as the person opens it, the interface drawing its bar;
    // status says how far it is ("opening").
    if (flag("background", false)) {
        if (!ui_.openProjectInSteps(file))
            return fail("could not start opening " + file.getFullPathName());
        auto result = object();
        set(result, "opening", true);
        return ok(result);
    }
    const auto loaded = studio_.loadProject(file);
    ui_.sessionReplaced();
    if (loaded.failed())
        return fail(loaded.getErrorMessage());
    auto result = status();
    if (!studio_.loadWarnings().isEmpty())
        set(result, "warnings", strings(studio_.loadWarnings()));
    return ok(result);
}

juce::var Commands::onSaveProject()
{
    auto file = has("path") ? fileFor(text("path")) : studio_.projectFile();
    if (file == juce::File {})
        return fail("the project has no file yet; give a path");
    file = file.withFileExtension("cydaw");
    if (const auto saved = studio_.saveProject(file); saved.failed())
        return fail(saved.getErrorMessage());
    if (!file.existsAsFile())
        return fail("the project was not written to " + file.getFullPathName());
    auto result = object();
    set(result, "path", file.getFullPathName());
    return ok(result);
}

juce::var Commands::onTemplates()
{
    const auto files = studio_.templates();
    if (command_ == "list_templates") {
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
        return fail("no single template matches \"" + name + "\"; list_templates gives their names");
    if (studio_.isRecording())
        (void) studio_.stopRecording(false);
    const auto loaded = studio_.loadProject(chosen);
    ui_.sessionReplaced();
    if (loaded.failed())
        return fail(loaded.getErrorMessage());
    return ok(status());
}

juce::var Commands::onImportProject()
{
    namespace importer = app::project_import;
    const auto file = fileFor(text("file"));
    if (!file.existsAsFile())
        return fail("no project file at " + file.getFullPathName());
    if (studio_.isRecording() || engine_.isRecording() || engine_.isMidiRecording())
        return fail("a recording is running");
    // "background": as the person imports it, the interface drawing its bar;
    // status says how far it is ("opening").
    if (flag("background", false)) {
        if (!ui_.importProjectInSteps(file))
            return fail("could not start importing " + file.getFullPathName());
        auto result = object();
        set(result, "opening", true);
        return ok(result);
    }
    // Read once here for the report; the studio reads it again to build the
    // session.
    importer::ImportedProject project;
    if (const auto read = importer::importProjectFile(file, project); read.failed())
        return fail(read.getErrorMessage());
    juce::String summary;
    const auto imported = studio_.importProject(file, summary);
    ui_.sessionReplaced();
    if (imported.failed())
        return fail(imported.getErrorMessage());

    // What the session now holds is what was imported.
    auto clipCount = 0;
    auto noteCount = 0;
    for (std::size_t index = 0; index < engine_.trackCount(); ++index) {
        if (const auto* clips = engine_.audioClipsView(index))
            clipCount += static_cast<int>(clips->size());
        clipCount += static_cast<int>(engine_.launcherClips(index).size());
    }
    const auto midiSummaries = engine_.midiTrackSummaries();
    for (const auto& midiTrack : midiSummaries)
        if (const auto* clips = engine_.midiClipsView(midiTrack.id)) {
            clipCount += static_cast<int>(clips->size());
            for (const auto& clip : *clips)
                noteCount += static_cast<int>(clip.notes.size());
        }

    auto report = object();
    set(report, "format", project.format);
    set(report, "application", project.application);
    set(report, "name", project.name);
    set(report, "source", file.getFullPathName());
    set(report, "tempo", project.tempo);
    set(report, "numerator", project.numerator);
    set(report, "denominator", project.denominator);
    set(report, "masterGainDb", project.masterGainDb);
    juce::Array<juce::var> tracks;
    juce::StringArray warnings(project.warnings);
    for (const auto& source : project.tracks) {
        auto entry = object();
        set(entry, "name", source.name);
        set(entry, "kind", source.midi ? "midi" : "audio");
        set(entry, "clips", static_cast<int>(source.midiClips.size() + source.audioClips.size()));
        auto notes = 0;
        for (const auto& clip : source.midiClips)
            notes += static_cast<int>(clip.notes.size());
        set(entry, "notes", notes);
        set(entry, "gainDb", source.gainDb);
        set(entry, "pan", source.pan);
        set(entry, "muted", source.muted);
        set(entry, "solo", source.solo);
        set(entry, "devices", strings(source.devices));
        tracks.add(entry);
        if (!source.devices.isEmpty())
            warnings.add("\"" + source.name + "\": devices not carried over: " + source.devices.joinIntoString(", "));
    }
    set(report, "tracks", tracks);
    set(report, "warnings", strings(warnings));
    set(report, "missingFiles", strings(project.missingFiles));
    auto counts = object();
    set(counts, "audioTracks", static_cast<int>(engine_.trackCount()));
    set(counts, "midiTracks", static_cast<int>(midiSummaries.size()));
    set(counts, "clips", clipCount);
    set(counts, "notes", noteCount);
    set(report, "imported", counts);
    set(report, "summary", summary);
    return ok(report);
}

juce::var Commands::onNewProject()
{
    // An empty project, then the one MIDI track a session starts with.
    if (studio_.isRecording())
        (void) studio_.stopRecording(false);
    if (engine_.isMidiRecording())
        engine_.stopMidiRecording(false);
    studio_.newProject();
    (void) studio_.addMidiTrack(app::tr("midi.instrument") + " 1", {});
    ui_.sessionReplaced();
    return ok(status());
}

juce::var Commands::onSetView()
{
    const auto mode = static_cast<int>(number("mode", 1.0));
    if (mode < 1 || mode > 11)
        return fail("mode is 1 arrangement, 2 clip, 4 launcher, 7 graph, 8 bus (the routing), 9 mixer, 10 samples, 11 routing");
    ui_.showView(mode);
    if (has("bus")) {
        const auto spec = text("bus").trim();
        std::uint64_t busId = 0;
        for (const auto& bus : engine_.virtualBuses())
            if (bus.name.equalsIgnoreCase(spec) || juce::String(static_cast<juce::int64>(bus.id)) == spec)
                busId = bus.id;
        if (busId == 0)
            return fail("no bus \"" + spec + "\"; list_buses lists them");
        ui_.selectTrack(TrackRef::ofBus(busId));
    }
    if (flag("catalogue", false))
        ui_.showDeviceCatalogue();
    // "graph": "modulation" or "sound": what the graph editor shows of the track.
    if (has("graph"))
        ui_.showModulationGraph(text("graph").equalsIgnoreCase("modulation"));
    // "clipId" (with "track"): that clip in the editor; an audio clip on a
    // face of its editor ("face": sample, takes, stretch).
    if (has("clipId")) {
        std::optional<RemoteTrack> track;
        if (auto error = trackOrFail(track); !error.isVoid())
            return error;
        const auto face = text("face").trim().toLowerCase();
        ui_.editClip(track->ref(), idFrom(request_.getProperty("clipId", {})),
                     face == "takes" ? 1 : face == "stretch" ? 2 : face == "events" ? 3 : 0);
    }
    // "launcherScene" (with "track"): an audio track's launcher clip in the editor.
    if (has("launcherScene")) {
        std::optional<RemoteTrack> track;
        if (auto error = trackOrFail(track); !error.isVoid())
            return error;
        const auto scene = static_cast<int>(number("launcherScene", 0.0));
        if (track->ref().midi || !engine_.launcherClip(track->ref().audioIndex, scene))
            return fail("no audio launcher clip in that slot");
        ui_.editLauncherClip(track->ref(), scene);
    }
    if (has("device") && !ui_.showDeviceEditor(static_cast<std::uint64_t>(number("device", 0.0)),
                                                static_cast<int>(number("pane", -1.0)), flag("detached", false)))
        return fail("no track selected to show that device for");
    auto result = object();
    set(result, "editorMode", ui_.currentView());
    return ok(result);
}

juce::var Commands::onSnapshot()
{
    const auto file = fileFor(text("path")).withFileExtension("png");
    (void) file.getParentDirectory().createDirectory();
    if (!ui_.snapshot(file) || !file.existsAsFile())
        return fail("the capture could not be written to " + file.getFullPathName());
    const auto size = pngSize(file);
    auto result = object();
    set(result, "path", file.getFullPathName());
    set(result, "width", size.x);
    set(result, "height", size.y);
    return ok(result);
}

juce::var Commands::onExportMix()
{
    if (engine_.isMasterExporting())
        return fail("an export is already running");
    const auto file = fileFor(text("path")).withFileExtension("wav");
    if (!file.getParentDirectory().createDirectory())
        return fail("no folder for " + file.getFullPathName());
    // Rendered in real time through the master; Studio::tick finishes it.
    if (const auto started = engine_.startMasterExport(file); started.failed())
        return fail(started.getErrorMessage());
    auto result = object();
    set(result, "path", file.getFullPathName());
    set(result, "started", engine_.isMasterExporting());
    return ok(result);
}

juce::var Commands::onQuit()
{
    // The answer goes out first; the interface quits right after.
    ui_.requestQuit();
    auto result = object();
    set(result, "quitting", true);
    return ok(result);
}

// ---- Transport -------------------------------------------------------------------------------

juce::var Commands::onTransport()
{
    if (engine_.isMasterExporting())
        return fail("an export is running");
    if (has("position"))
        engine_.setPosition(std::max(0.0, number("position", 0.0)));
    const auto action = text("action");
    if (action == "play" || (action == "toggle" && !engine_.isPlaying())) {
        if (!engine_.isPlaying())
            studio_.play();
    } else if (action == "stop" || action == "toggle") {
        if (studio_.isRecording())
            (void) studio_.stopRecording(true);
        studio_.stopPlayback();
    } else if (action == "rewind") {
        engine_.setPosition(0.0);
    } else if (action.isNotEmpty()) {
        return fail("unknown transport action \"" + action + "\": play, stop, toggle, rewind");
    }
    auto result = object();
    set(result, "playing", engine_.isPlaying());
    set(result, "position", engine_.position());
    return ok(result);
}

juce::var Commands::onSetTempo()
{
    const auto bpm = juce::jlimit(20.0, 300.0, number("bpm", engine_.tempo()));
    engine_.setTempo(bpm);
    // The metre: {"numerator": 5, "denominator": 4}.
    if (has("numerator") || has("denominator"))
        engine_.setTimeSignature(static_cast<int>(number("numerator", engine_.timeSignatureNumerator())),
                                 static_cast<int>(number("denominator", engine_.timeSignatureDenominator())));
    studio_.markDirty();
    auto result = object();
    set(result, "tempo", engine_.tempo());
    set(result, "numerator", engine_.timeSignatureNumerator());
    set(result, "denominator", engine_.timeSignatureDenominator());
    return ok(result);
}

juce::var Commands::onSetLoop()
{
    if (has("start") || has("end")) {
        const auto start = std::max(0.0, number("start", engine_.loopStart()));
        const auto end = std::max(start + 0.01, number("end", engine_.loopEnd()));
        // The range alone; the switch is "enabled".
        engine_.setLoopRange(start, end);
    }
    if (has("enabled"))
        studio_.setLoopEnabled(flag("enabled", false));
    studio_.markDirty();
    auto result = object();
    set(result, "start", engine_.loopStart());
    set(result, "end", engine_.loopEnd());
    set(result, "enabled", studio_.loopEnabled());
    return ok(result);
}

juce::var Commands::onSetMasterGain()
{
    // {gainDb, muted}: the master's fader, and its mute (a probe that must stay
    // silent whatever a project restores).
    const auto gainDb = juce::jlimit(-60.0, 12.0, number("gainDb", engine_.masterGainDb()));
    engine_.setMasterGain(static_cast<float>(gainDb));
    if (has("muted"))
        engine_.setMasterMuted(flag("muted", false));
    studio_.markDirty();
    auto result = object();
    set(result, "masterGainDb", engine_.masterGainDb());
    set(result, "masterMuted", engine_.isMasterMuted());
    return ok(result);
}

// ---- Tracks ------------------------------------------------------------------------------------

juce::var Commands::onAddMidiTrack()
{
    if (engine_.isMidiRecording())
        return fail("a MIDI recording is running");
    auto name = text("name").trim();
    if (name.isEmpty())
        name = app::tr("midi.instrument") + " "
               + juce::String(static_cast<int>(engine_.midiTrackSummaries().size() + 1));
    // "instrument": a factory instrument's device key ("prism-instrument"…).
    const auto track = studio_.addMidiTrack(name, text("instrument").trim());
    if (!track)
        return fail("the MIDI track could not be added");
    ui_.selectTrack(*track);
    auto result = object();
    set(result, "id", idOf(track->midiId));
    set(result, "index", static_cast<int>(engine_.trackCount() + engine_.midiTrackSummaries().size() - 1));
    set(result, "name", name);
    return ok(result);
}

juce::var Commands::onAddAudioTrack()
{
    const auto file = fileFor(text("file"));
    if (!file.existsAsFile())
        return fail("no audio file at " + file.getFullPathName());
    const auto track = studio_.addAudioTrack(file);
    if (!track)
        return fail("the file could not be opened as audio");
    ui_.selectTrack(*track);
    auto result = object();
    set(result, "index", static_cast<int>(track->audioIndex));
    const auto name = studio_.trackName(*track);
    set(result, "name", name.isNotEmpty() ? name : file.getFileNameWithoutExtension());
    return ok(result);
}

juce::var Commands::onSetTrack()
{
    std::optional<RemoteTrack> track;
    if (auto error = trackOrFail(track); !error.isVoid())
        return error;
    const auto ref = track->ref();
    const auto current = studio_.trackMix(ref);
    auto gainDb = current.gainDb;
    auto pan = current.pan;
    auto muted = current.muted;
    auto solo = current.solo;
    if (has("gainDb") || has("pan")) {
        gainDb = static_cast<float>(juce::jlimit(-60.0, 12.0, number("gainDb", current.gainDb)));
        pan = static_cast<float>(juce::jlimit(-1.0, 1.0, number("pan", current.pan)));
    }
    if (has("muted"))
        muted = flag("muted", current.muted);
    if (has("solo"))
        solo = flag("solo", current.solo);
    if (has("gainDb") || has("pan") || has("muted") || has("solo")) {
        if (track->midi) {
            engine_.setMidiTrackMixer(track->id, gainDb, pan, muted, solo);
        } else {
            engine_.setTrackMixer(track->index, gainDb, pan, muted);
            if (solo != current.solo)
                engine_.setTrackSolo(track->index, solo);
        }
        studio_.markDirty();
    }
    if (has("name")) {
        const auto name = text("name").trim();
        if (track->midi) {
            if (!engine_.setMidiTrackName(track->id, name))
                return fail("the MIDI track could not be renamed");
            studio_.markDirty();
        } else if (name.isNotEmpty()) {
            studio_.setTrackName(ref, name);
        }
    }
    if (has("colour"))
        if (const auto colour = colourFrom(text("colour")); colour != 0)
            studio_.setTrackColour(ref, colour);
    if (has("inputBus") && !track->midi) {
        const auto spec = text("inputBus").trim();
        std::uint64_t busId = 0;
        for (const auto& bus : engine_.virtualBuses())
            if (bus.name.equalsIgnoreCase(spec) || juce::String(static_cast<juce::int64>(bus.id)) == spec)
                busId = bus.id;
        if (busId == 0 || !engine_.setTrackInputBus(track->index, busId))
            return fail("no input bus \"" + spec + "\"; status lists the buses");
        studio_.markDirty();
    }
    if (has("inputAfterEffects") && !track->midi) {
        // What it records of its input: after the input's effects, or before.
        engine_.setTrackInputAfterEffects(track->index, flag("inputAfterEffects", true));
        studio_.markDirty();
    }
    if (has("format") || has("channels")) {
        // "mono", "stereo", "lcr", "quad", "5.0", "5.1", "7.0", "7.1", or 1 to 8.
        auto channels = has("channels") ? static_cast<int>(number("channels", 2.0)) : 0;
        if (has("format"))
            channels = cydaw::engine::layoutChannels(text("format").trim().toLowerCase().toStdString());
        if (channels < 1 || channels > cydaw::engine::maximumTrackChannels)
            return fail("format: mono, stereo, lcr, quad, 5.0, 5.1, 7.0 or 7.1 (channels 1 to 8)");
        if (const auto formatted = studio_.setTrackChannels(ref, channels); formatted.failed())
            return fail(formatted.getErrorMessage());
    }
    return ok(describeTracks());
}

juce::var Commands::onRemoveTrack()
{
    std::optional<RemoteTrack> track;
    if (auto error = trackOrFail(track, true); !error.isVoid())
        return error;
    if (track->bus)
        return fail("remove_bus removes a bus");
    if (track->master)
        return fail("the master stays");
    if (!track->midi && engine_.isRecording())
        return fail("a recording is running");
    if (track->midi && engine_.isMidiRecording())
        return fail("a MIDI recording is running");
    const auto selected = ui_.selectedTrack();
    const auto removed = track->ref();
    if (!studio_.removeTrack(removed))
        return fail("the track could not be removed");
    ui_.tracksChanged();
    // The selection follows: a neighbour for the removed track, one index
    // up for an audio track after it.
    if (selected) {
        std::optional<TrackRef> next;
        if (*selected == removed) {
            if (!removed.midi && engine_.trackCount() > 0) {
                next = TrackRef { false, std::min(removed.audioIndex, engine_.trackCount() - 1), 0 };
            } else if (const auto summaries = engine_.midiTrackSummaries(); !summaries.empty()) {
                next = TrackRef { true, 0, summaries.front().id };
            } else if (const auto all = studio_.tracks(); !all.empty()) {
                next = all.front();
            }
        } else if (!selected->midi && !removed.midi && selected->audioIndex > removed.audioIndex) {
            next = TrackRef { false, selected->audioIndex - 1, 0 };
        } else {
            next = selected;
        }
        if (next)
            ui_.selectTrack(*next);
    }
    return ok(describeTracks());
}

juce::var Commands::onMoveTrack()
{
    std::optional<RemoteTrack> track;
    if (auto error = trackOrFail(track, true); !error.isVoid())
        return error;
    if (track->bus || track->master)
        return fail("buses and the master do not move");
    const auto to = static_cast<int>(number("to", -1.0));
    const auto audioCount = static_cast<int>(engine_.trackCount());
    const auto total = audioCount + static_cast<int>(engine_.midiTrackSummaries().size());
    if (to < 0 || to >= total)
        return fail("to is an index from 0 to " + juce::String(total - 1));
    // Audio tracks move among audio tracks, MIDI among MIDI: the combined
    // index keeps audio first.
    if (track->midi != (to >= audioCount))
        return fail(track->midi ? "a MIDI track moves to an index from " + juce::String(audioCount) + " to "
                                      + juce::String(total - 1)
                                : "an audio track moves to an index from 0 to " + juce::String(audioCount - 1));
    const auto from = static_cast<int>(track->channel);
    const auto selected = ui_.selectedTrack();
    if (to != from && studio_.moveTrack(track->ref(), to - from) && !track->midi) {
        // Audio indices moved: the selection follows its track.
        ui_.tracksChanged();
        if (selected && !selected->midi) {
            auto index = static_cast<int>(selected->audioIndex);
            if (index == from)
                index = to;
            else if (from < to && index > from && index <= to)
                --index;
            else if (to < from && index >= to && index < from)
                ++index;
            ui_.selectTrack(TrackRef { false, static_cast<std::size_t>(index), 0 });
        }
    }
    return ok(describeTracks());
}

// ---- Notes and MIDI clips -------------------------------------------------------------------------

namespace {

// A note's expression, as [[position, value], ...] or [{position, value}, ...]
// (position 0..1 along the note; semitones for the pitch, dB for the gain).
std::vector<Engine::NoteExpressionPoint> expressionFrom(const juce::var& value)
{
    std::vector<Engine::NoteExpressionPoint> points;
    if (const auto* items = value.getArray())
        for (const auto& item : *items) {
            double position {};
            double level {};
            if (const auto* pair = item.getArray(); pair != nullptr && pair->size() >= 2) {
                position = static_cast<double>((*pair)[0]);
                level = static_cast<double>((*pair)[1]);
            } else if (item.isObject()) {
                position = static_cast<double>(item.getProperty("position", 0.0));
                level = static_cast<double>(item.getProperty("value", 0.0));
            } else {
                continue;
            }
            if (std::isfinite(position) && std::isfinite(level))
                points.push_back({ static_cast<float>(position), static_cast<float>(level) });
        }
    return points;
}

juce::var expressionVar(const std::vector<Engine::NoteExpressionPoint>& points)
{
    juce::Array<juce::var> values;
    for (const auto& point : points)
        values.add(juce::Array<juce::var> { point.position, point.value });
    return values;
}

} // namespace

juce::var Commands::onNotes()
{
    std::optional<RemoteTrack> track;
    if (auto error = trackOrFail(track); !error.isVoid())
        return error;
    if (!track->midi)
        return fail("\"" + text("track") + "\" is an audio track; notes go on MIDI tracks");
    const auto clipName = text("clip").trim();
    if (command_ == "list_notes") {
        const auto info = midiInfo(track->id);
        if (!info)
            return fail("the track is gone");
        juce::Array<juce::var> clips;
        for (const auto& clip : info->clips) {
            if (clipName.isNotEmpty() && !clip.name.equalsIgnoreCase(clipName))
                continue;
            juce::Array<juce::var> entries;
            for (const auto& note : clip.notes) {
                auto entry = object();
                set(entry, "id", idOf(note.id));
                set(entry, "note", note.noteNumber);
                set(entry, "start", note.startBeats);
                set(entry, "length", note.durationBeats);
                set(entry, "velocity", note.velocity);
                if (!note.pitch.empty())
                    set(entry, "pitch", expressionVar(note.pitch));
                if (!note.gain.empty())
                    set(entry, "gain", expressionVar(note.gain));
                entries.add(entry);
            }
            auto clipEntry = object();
            set(clipEntry, "id", idOf(clip.id));
            set(clipEntry, "name", clip.name);
            set(clipEntry, "startBeats", clip.startBeats);
            set(clipEntry, "lengthBeats", clip.lengthBeats);
            set(clipEntry, "notes", entries);
            clips.add(clipEntry);
        }
        auto result = object();
        set(result, "track", info->name);
        set(result, "clips", clips);
        return ok(result);
    }
    if (command_ == "clear_notes") {
        const auto info = midiInfo(track->id);
        if (!info)
            return fail("the track is gone");
        auto cleared = 0;
        for (const auto& clip : info->clips)
            if (clipName.isEmpty() || clip.name.equalsIgnoreCase(clipName)) {
                cleared += static_cast<int>(clip.notes.size());
                engine_.clearMidiNotes(track->id, clip.id);
            }
        studio_.markDirty();
        auto result = object();
        set(result, "cleared", cleared);
        return ok(result);
    }
    // add_notes: into the clip named, the first one, or a new one.
    const auto* list = request_.getProperty("notes", juce::var()).getArray();
    if (list == nullptr || list->isEmpty())
        return fail("notes must be a non-empty array of {note, start, length, velocity}");
    auto info = midiInfo(track->id);
    if (!info)
        return fail("the track is gone");
    auto clip = midiClipNamed(*info, clipName);
    if (!clip) {
        auto lastEnd = 0.0;
        for (const auto& note : *list)
            lastEnd = std::max(lastEnd, numberOf(note, "start", 0.0) + numberOf(note, "length", 1.0));
        const auto lengthBeats = number("lengthBeats", std::max(4.0, std::ceil(lastEnd / 4.0) * 4.0));
        const auto clipId = engine_.addMidiClip(track->id,
                                                clipName.isNotEmpty() ? clipName : app::tr("midi.clip") + " 1",
                                                0.0, lengthBeats);
        if (clipId == 0)
            return fail("the clip could not be created");
        info = midiInfo(track->id);
        if (!info)
            return fail("the track is gone");
        for (const auto& candidate : info->clips)
            if (candidate.id == clipId)
                clip = candidate;
    }
    if (!clip)
        return fail("no clip named \"" + clipName + "\" on that track");
    auto added = 0;
    for (const auto& note : *list) {
        const auto pitch = static_cast<int>(numberOf(note, "note", -1.0));
        if (pitch < 0 || pitch > 127)
            continue;
        const auto noteId = engine_.addMidiNote(track->id, clip->id, pitch, std::max(0.0, numberOf(note, "start", 0.0)),
                                                std::max(0.01, numberOf(note, "length", 1.0)),
                                                static_cast<float>(juce::jlimit(0.01, 1.0, numberOf(note, "velocity", 0.8))));
        if (noteId == 0)
            continue;
        ++added;
        if (note.hasProperty("pitch") || note.hasProperty("gain"))
            (void) engine_.setMidiNoteExpression(track->id, clip->id, noteId, expressionFrom(note.getProperty("pitch", juce::var())),
                                                 expressionFrom(note.getProperty("gain", juce::var())));
    }
    // The track written to is the one shown, as a MIDI recording expects.
    ui_.selectTrack(track->ref());
    studio_.markDirty();
    auto total = 0;
    if (const auto after = midiInfo(track->id))
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

juce::var Commands::onEditNotes()
{
    std::optional<RemoteTrack> track;
    if (auto error = trackOrFail(track); !error.isVoid())
        return error;
    if (!track->midi)
        return fail("\"" + text("track") + "\" is an audio track");
    const auto info = midiInfo(track->id);
    if (!info)
        return fail("the track is gone");
    const auto clipName = text("clip").trim();
    if (command_ == "set_midi_clip") {
        const auto clip = midiClipNamed(*info, clipName);
        if (!clip)
            return fail("no clip on that track"
                        + (clipName.isNotEmpty() ? " named \"" + clipName + "\"" : juce::String()));
        const auto looping = has("looping") ? flag("looping", false) : clip->looping;
        auto applied = true;
        // {scene}: into that launcher slot (-1: back into the arrangement,
        // at startBeats).
        if (has("scene")) {
            const auto scene = static_cast<int>(number("scene", -1.0));
            if (scene >= Engine::maximumLauncherScenes)
                return fail("scene is 0 to " + juce::String(Engine::maximumLauncherScenes - 1));
            if (scene >= 0 && scene != clip->launcherScene && studio_.slotHasClip(TrackRef { true, 0, track->id }, scene))
                return fail("that launcher slot holds a clip already");
            applied = engine_.setMidiClipLauncherScene(track->id, clip->id, std::max(-1, scene));
        }
        if (applied && has("startBeats"))
            applied = engine_.setMidiClipStartBeats(track->id, clip->id, number("startBeats", clip->startBeats));
        if (applied && (has("name") || has("colour")))
            applied = engine_.setMidiClipPresentation(track->id, clip->id, text("name"), colourFrom(text("colour")));
        if (has("lengthBeats")) {
            const auto length = std::max(0.25, number("lengthBeats", clip->lengthBeats));
            applied = engine_.setMidiClipTimelineBounds(track->id, clip->id, length,
                                                        std::min(clip->playStartBeats, length - 0.25), length,
                                                        std::min(clip->loopStartBeats, length - 0.25), length, looping);
        } else if (has("looping")) {
            applied = engine_.setMidiClipLooping(track->id, clip->id, looping);
        }
        if (!applied)
            return fail("the clip refused those bounds");
        studio_.markDirty();
        auto result = object();
        if (const auto after = midiInfo(track->id))
            for (const auto& candidate : after->clips)
                if (candidate.id == clip->id) {
                    set(result, "name", candidate.name);
                    set(result, "startBeats", candidate.startBeats);
                    set(result, "scene", candidate.launcherScene);
                    set(result, "lengthBeats", candidate.lengthBeats);
                    set(result, "looping", candidate.looping);
                    set(result, "colour", colourText(candidate.colour));
                }
        return ok(result);
    }
    auto touched = 0;
    if (command_ == "update_notes") {
        const auto* changes = request_.getProperty("notes", juce::var()).getArray();
        if (changes == nullptr || changes->isEmpty())
            return fail("notes must be a non-empty array of {id, note, start, length, velocity}");
        for (const auto& change : *changes) {
            const auto noteId = idFrom(change.getProperty("id", juce::var()));
            for (const auto& clip : info->clips)
                for (const auto& note : clip.notes)
                    if (note.id == noteId
                        && engine_.updateMidiNote(
                            track->id, clip.id, noteId,
                            juce::jlimit(0, 127, static_cast<int>(numberOf(change, "note", note.noteNumber))),
                            std::max(0.0, numberOf(change, "start", note.startBeats)),
                            std::max(0.01, numberOf(change, "length", note.durationBeats)),
                            static_cast<float>(juce::jlimit(0.01, 1.0, numberOf(change, "velocity", note.velocity))))) {
                        ++touched;
                        // An expression given replaces the note's ([]: none).
                        if (change.hasProperty("pitch") || change.hasProperty("gain"))
                            (void) engine_.setMidiNoteExpression(
                                track->id, clip.id, noteId,
                                change.hasProperty("pitch") ? expressionFrom(change.getProperty("pitch", juce::var())) : note.pitch,
                                change.hasProperty("gain") ? expressionFrom(change.getProperty("gain", juce::var())) : note.gain);
                    }
        }
    } else {
        const auto* ids = request_.getProperty("ids", juce::var()).getArray();
        const auto ranged = has("from") || has("to");
        if ((ids == nullptr || ids->isEmpty()) && !ranged)
            return fail("give ids, or a from/to range in beats");
        const auto from = number("from", 0.0);
        const auto to = number("to", 1.0e9);
        for (const auto& clip : info->clips) {
            if (clipName.isNotEmpty() && !clip.name.equalsIgnoreCase(clipName))
                continue;
            for (const auto& note : clip.notes) {
                auto remove = false;
                if (ids != nullptr)
                    for (const auto& id : *ids)
                        if (idFrom(id) == note.id)
                            remove = true;
                if (ranged && ids == nullptr && note.startBeats >= from && note.startBeats < to)
                    remove = true;
                if (remove && engine_.removeMidiNote(track->id, clip.id, note.id))
                    ++touched;
            }
        }
    }
    studio_.markDirty();
    auto result = object();
    set(result, command_ == "update_notes" ? "updated" : "removed", touched);
    return ok(result);
}

juce::var Commands::onAddMidiClip()
{
    std::optional<RemoteTrack> track;
    if (auto error = trackOrFail(track); !error.isVoid())
        return error;
    if (!track->midi)
        return fail("MIDI clips go on MIDI tracks");
    // {scene}: in that launcher slot; else in the arrangement at startBeats.
    const auto colour = colourFrom(text("colour"));
    const auto scene = has("scene") ? static_cast<int>(number("scene", -1.0)) : -1;
    if (scene >= Engine::maximumLauncherScenes)
        return fail("scene is 0 to " + juce::String(Engine::maximumLauncherScenes - 1));
    if (scene >= 0 && studio_.slotHasClip(TrackRef { true, 0, track->id }, scene))
        return fail("that launcher slot holds a clip already");
    const auto clipId = engine_.addMidiClip(track->id, text("name").isNotEmpty() ? text("name") : app::tr("midi.clip"),
                                            std::max(0.0, number("startBeats", 0.0)),
                                            std::max(0.25, number("lengthBeats", 16.0)), flag("looping", false),
                                            colour != 0 ? colour : 0xff4fd3aeu, std::max(-1, scene));
    if (clipId == 0)
        return fail("the clip could not be created");
    studio_.markDirty();
    auto result = object();
    set(result, "clipId", idOf(clipId));
    set(result, "scene", std::max(-1, scene));
    return ok(result);
}

juce::var Commands::onRemoveClip()
{
    std::optional<RemoteTrack> track;
    if (auto error = trackOrFail(track); !error.isVoid())
        return error;
    if (track->midi) {
        const auto info = midiInfo(track->id);
        const auto clip = info ? midiClipNamed(*info, text("clip").trim()) : std::nullopt;
        if (!clip)
            return fail("no such clip on that track");
        if (!engine_.removeMidiClip(track->id, clip->id))
            return fail("the clip could not be removed");
        studio_.markDirty();
        return ok(describeTracks());
    }
    const auto clipId = idFrom(request_.getProperty("clipId", juce::var()));
    if (clipId == 0 || !engine_.removeAudioClip(track->index, clipId))
        return fail("no audio clip with that id; list_audio_clips gives them");
    studio_.markDirty();
    return ok(describeAudioClips(track->index));
}

juce::var Commands::onExportMidi()
{
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
    if (const auto written = engine_.exportMidiClipToFile(track->id, clip->id, file); written.failed())
        return fail(written.getErrorMessage());
    auto result = object();
    set(result, "path", file.getFullPathName());
    set(result, "notes", static_cast<int>(clip->notes.size()));
    return ok(result);
}

juce::var Commands::onImportMidi()
{
    const auto file = fileFor(text("file"));
    if (!file.existsAsFile())
        return fail("no MIDI file at " + file.getFullPathName());
    Engine::MidiFileImportSummary summary;
    if (const auto imported = engine_.importMidiFile(file, summary, true); imported.failed())
        return fail(imported.getErrorMessage());
    // The new tracks get a graph and an instrument, as the File menu does.
    for (const auto id : summary.trackIds) {
        (void) studio_.adoptMidiTrack(id, {});
        (void) studio_.insertDevice(TrackRef { true, 0, id }, "opale-instrument");
    }
    studio_.markDirty();
    if (!summary.trackIds.empty())
        ui_.selectTrack(TrackRef { true, 0, summary.trackIds.front() });
    auto result = describeTracks();
    set(result, "tracksAdded", static_cast<int>(summary.trackIds.size()));
    return ok(result);
}

// ---- Devices and graphs -------------------------------------------------------------------------

juce::var Commands::onListDevices()
{
    juce::Array<juce::var> entries;
    for (const auto& device : catalogueGraph().deviceCatalogue()) {
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

juce::var Commands::onInsertDevice()
{
    const auto key = text("key").trim();
    std::optional<RemoteTrack> track;
    if (auto error = trackOrFail(track, true); !error.isVoid())
        return error;
    const auto target = graphTarget(*track);
    if (!target)
        return noGraph();
    auto& graph = target->graph();
    const auto catalogue = graph.deviceCatalogue();
    const auto device = std::find_if(catalogue.begin(), catalogue.end(),
                                     [&key](const auto& entry) { return entry.key == key; });
    if (device == catalogue.end())
        return fail("no device with key \"" + key + "\"; list_devices gives them");
    if (!track->midi && device->soundSource)
        return fail("\"" + key + "\" is an instrument; put it on a MIDI track");
    if (track->bus && device->kind == "preset" && device->input != "audio")
        return fail("\"" + key + "\" does not process audio; a bus takes effects");
    // A preset is the whole graph (an instrument's, on an instrument track,
    // takes the place of the one it plays, the effects after it kept); a
    // card with an input is spliced before the output; a source — an LFO,
    // an oscillator — is placed free, to be cabled by modulate.
    // A MIDI card (an arpeggiator, chords…) goes before the instrument,
    // after the MIDI cards already there.
    const auto notes = device->input == "midi" && device->output == "midi";
    if (notes && !track->midi)
        return fail("\"" + key + "\" works on notes; put it on a MIDI track");
    auto applied = device->kind == "preset"   ? (track->midi && device->soundSource ? graph.replaceInstrument(key)
                                                                                    : graph.insertFactoryDevice(key))
                   : notes                   ? graph.insertNodeBeforeInstrument(key)
                   : device->input == "none" ? graph.insertNode(key)
                                             : graph.insertNodeBeforeOutput(key);
    if (applied.failed() && device->kind != "preset")
        applied = graph.insertFactoryDevice(key);
    if (applied.failed())
        return fail(key + ": " + applied.getErrorMessage());
    graph_layout::arrange(graph);
    commitGraph(*target, true);
    return ok(graphSummary(*target));
}

juce::var Commands::onParameters()
{
    std::optional<RemoteTrack> track;
    if (auto error = trackOrFail(track, true); !error.isVoid())
        return error;
    const auto target = graphTarget(*track);
    if (!target)
        return noGraph();
    auto& graph = target->graph();
    if (command_ == "set_parameter") {
        const auto key = text("key").trim();
        if (!has("value"))
            return fail("value is required");
        if (!graph.setGraphInterfaceParameter(key, number("value", 0.0)))
            return fail("no published parameter \"" + key + "\" on that track; list_parameters gives the keys");
        commitGraph(*target, false);
    }
    juce::Array<juce::var> entries;
    const auto interfaceState = graph.graphInterfaceState();
    for (const auto& parameter : interfaceState.parameters) {
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

juce::var Commands::targetOrFail(std::optional<GraphTarget>& target)
{
    const auto named = text("track").trim();
    if (named.equalsIgnoreCase("workshop") || named.equalsIgnoreCase("atelier")) {
        auto* slot = studio_.workshopGraph();
        if (slot == nullptr)
            return fail("the workshop has no device; workshop {action: \"new\"} makes one");
        GraphTarget made;
        made.slot = slot;
        made.workshop = true;
        target = made;
        return {};
    }
    std::optional<RemoteTrack> track;
    if (auto error = trackOrFail(track, true); !error.isVoid())
        return error;
    target = graphTarget(*track);
    if (!target)
        return noGraph();
    // "graph": "modulation": the track's value graph (what moves its
    // parameters), edited as any other.
    if (text("graph").equalsIgnoreCase("modulation")) {
        auto* slot = studio_.modulationGraphFor(track->ref());
        if (slot == nullptr)
            return fail("only an audio track has a modulation graph");
        target->slot = slot;
    }
    return {};
}

// The sampler: factory instruments, sample maps, SFZ.
juce::var Commands::onSampler()
{
    const auto trackResult = [this](const TrackRef& track) {
        ui_.selectTrack(track);
        auto result = object();
        set(result, "track", studio_.trackName(track));
        const auto card = studio_.samplerCardOf(track);
        set(result, "nodeId", static_cast<juce::int64>(card));
        if (const auto* slot = studio_.graphFor(track))
            set(result, "description", app::describeSamplerMap(slot->graph().nodeSamplerJson(card)));
        return result;
    };
    if (command_ == "factory_instruments") {
        // The instruments CyDAW ships (Factory/catalogue.json).
        juce::Array<juce::var> list;
        for (const auto& instrument : studio_.factoryInstruments()) {
            auto item = object();
            set(item, "key", instrument.key);
            set(item, "name", instrument.name);
            set(item, "category", instrument.categoryName);
            set(item, "range", instrument.range);
            set(item, "map", instrument.map.getFullPathName());
            if (instrument.model != juce::File())
                set(item, "model", instrument.model.getFullPathName());
            // Its samples still to download, and from which banks.
            set(item, "missingFiles", instrument.missingFiles);
            set(item, "missingBytes", instrument.missingBytes);
            set(item, "banks", juce::var(instrument.banks));
            list.add(item);
        }
        auto result = object();
        set(result, "folder", app::factoryContentFolder().getFullPathName());
        set(result, "instruments", list);
        return ok(result);
    }
    if (command_ == "add_factory_instrument") {
        // {key, model, download, track}: a new instrument track playing it (its
        // samples, or its sound model), or on `track` in place of its
        // instrument (its effects kept); with `download`, its samples still
        // missing fetched too.
        juce::String error;
        const auto model = static_cast<bool>(request_.getProperty("model", false));
        if (!model && flag("download", false)) {
            juce::String ignored;
            (void) studio_.downloadFactoryInstrument(text("key"), ignored);
        }
        std::optional<TrackRef> onto;
        if (has("track")) {
            std::optional<RemoteTrack> target;
            if (auto problem = trackOrFail(target); !problem.isVoid())
                return problem;
            if (!target->midi)
                return fail("an instrument goes on an instrument (MIDI) track");
            onto = target->ref();
        }
        const auto track = studio_.addFactoryInstrument(text("key"), error, model, onto);
        if (!track)
            return fail(error);
        auto result = trackResult(*track);
        const auto missing = studio_.missingSamples(*track);
        set(result, "missingFiles", static_cast<int>(missing.files.size()));
        set(result, "missingBytes", missing.bytes);
        return ok(result);
    }
    if (command_ == "sampler_file") {
        // {file: .sfz | .cysampler, controllers: {"100": 127}}: the map it makes, read only
        // (no track, no sample decoded); an SFZ with these controllers at the start.
        juce::String error;
        const auto file = fileFor(text("file"));
        std::map<int, double> controllers;
        if (const auto* given = request_.getProperty("controllers", {}).getDynamicObject())
            for (const auto& [name, value] : given->getProperties())
                controllers[name.toString().getIntValue()] = static_cast<double>(value);
        const auto json = file.hasFileExtension("sfz") ? app::samplerMapFromSfz(file, error, controllers)
                                                       : app::loadSamplerMapFile(file, error);
        if (!json.isObject())
            return fail(error);
        auto result = object();
        set(result, "map", json);
        set(result, "description", app::describeSamplerMap(json));
        return ok(result);
    }
    if (command_ == "new_sampler") {
        // {files: [audio...]}: a new sampler track, the files mapped by their names and pitch.
        juce::Array<juce::File> files;
        if (const auto* list = request_.getProperty("files", {}).getArray())
            for (const auto& item : *list)
                files.add(fileFor(item.toString()));
        juce::String error;
        const auto track = studio_.newSamplerTrack(files, error);
        if (!track)
            return fail(error);
        return ok(trackResult(*track));
    }
    if (command_ == "load_sampler") {
        // {file: .sfz | .cysampler, track}: on that track's sampler, else a new track;
        // {file: a sound, track}: the one sample of that track's simple sampler.
        std::optional<TrackRef> on;
        if (has("track")) {
            std::optional<RemoteTrack> track;
            if (auto error = trackOrFail(track); !error.isVoid())
                return error;
            on = track->ref();
        }
        juce::String error;
        const auto track = studio_.loadSamplerFile(fileFor(text("file")), on, error);
        if (!track)
            return fail(error);
        return ok(trackResult(*track));
    }
    std::optional<RemoteTrack> track;
    if (auto error = trackOrFail(track); !error.isVoid())
        return error;
    const auto ref = track->ref();
    const auto card = has("nodeId") ? static_cast<std::uint64_t>(static_cast<juce::int64>(request_.getProperty("nodeId", 0)))
                                    : studio_.samplerCardOf(ref);
    const auto* slot = studio_.graphFor(ref);
    if (slot == nullptr || card == 0)
        return fail("that track has no sampler card");
    if (command_ == "sampler_map") {
        // {track, nodeId, map}: the card's zones; with "map", replaced (an edit, undoable).
        if (has("map")) {
            juce::String error;
            if (!studio_.setSamplerMap(ref, card, request_.getProperty("map", {}), static_cast<bool>(request_.getProperty("applySettings", false)),
                                       error))
                return fail(error);
        }
        auto result = object();
        set(result, "nodeId", static_cast<juce::int64>(card));
        set(result, "map", slot->graph().nodeSamplerJson(card));
        set(result, "description", app::describeSamplerMap(slot->graph().nodeSamplerJson(card)));
        return ok(result);
    }
    if (command_ == "sampler_controls") {
        // {track, nodeId, set: {"70": 100, "articulation": 25}}: an SFZ's controls on the card
        // (below 0: back where the instrument starts it), its articulations.
        const auto map = slot->graph().nodeSampler(card);
        auto* editable = studio_.graphFor(ref);
        if (map == nullptr || editable == nullptr)
            return fail("the sampler card holds no map");
        const auto prefix = "node." + juce::String(static_cast<juce::int64>(card)) + ".sampler.";
        juce::Array<juce::var> problems;
        if (const auto* given = request_.getProperty("set", {}).getDynamicObject())
            for (const auto& [name, value] : given->getProperties()) {
                const auto key = name.toString();
                const auto controller = key.isNotEmpty() && key.containsOnly("0123456789") && key.getIntValue() < 128;
                if ((key != "articulation" && !controller)
                    || !editable->graph().editorSetParameter(prefix + (controller ? "cc" + key : key), static_cast<double>(value)))
                    problems.add("not a control: " + key);
            }
        const auto values = slot->graph().editorSamplerControls(card);
        auto result = object();
        set(result, "nodeId", static_cast<juce::int64>(card));
        juce::Array<juce::var> controls;
        for (const auto& control : map->controls) {
            const auto number = static_cast<std::size_t>(std::clamp(control.controller, 0, 127));
            const auto onCard = values ? values->controllers[number] : -1.0f;
            auto item = object();
            set(item, "cc", static_cast<int>(number));
            set(item, "label", juce::String::fromUTF8(control.label.c_str()));
            set(item, "start", map->controllerStart[number]);
            set(item, "value", onCard >= 0.0f ? onCard : map->controllerStart[number]);
            set(item, "set", onCard >= 0.0f);
            controls.add(item);
        }
        set(result, "controls", controls);
        juce::Array<juce::var> articulations;
        for (int key = 0; key < 128; ++key)
            if (map->keyswitchKeys[static_cast<std::size_t>(key)]) {
                auto item = object();
                set(item, "key", key);
                for (const auto& [labelled, name] : map->keyswitchLabels)
                    if (labelled == key)
                        set(item, "label", juce::String::fromUTF8(name.c_str()));
                articulations.add(item);
            }
        set(result, "articulations", articulations);
        const auto chosen = values ? static_cast<int>(std::lround(values->articulation)) : -1;
        set(result, "articulation", engine::samplerKeyswitch(*map, chosen) ? chosen : map->keyswitchStart);
        auto releases = 0;
        auto ruled = 0;
        auto deferred = 0;
        for (const auto& zone : map->zones) {
            releases += zone.trigger == engine::SamplerTrigger::release ? 1 : 0;
            ruled += zone.rules.empty() ? 0 : 1;
            deferred += zone.sample != nullptr && zone.sample->asleep() ? 1 : 0;
        }
        set(result, "zones", static_cast<int>(map->zones.size()));
        set(result, "releaseZones", releases);
        set(result, "zonesWithRules", ruled);
        set(result, "deferred", deferred);
        set(result, "problems", problems);
        return ok(result);
    }
    // sampler_status {track, wait}: how far its samples are decoded (waiting up to `wait` seconds).
    const auto map = slot->graph().nodeSampler(card);
    if (map == nullptr)
        return fail("the sampler card holds no map");
    if (has("wait"))
        (void) app::SampleStore::instance().waitUntilReady(*map, static_cast<int>(number("wait", 10.0) * 1'000.0));
    auto ready = 0;
    auto failed = 0;
    auto deferred = 0;
    juce::int64 frames {};
    for (const auto& zone : map->zones) {
        if (zone.sample != nullptr && zone.sample->ready.load()) {
            ++ready;
            frames += zone.sample->frames;
        } else if (zone.sample == nullptr || zone.sample->failed.load()) {
            ++failed;
        } else if (zone.sample->asleep()) {
            ++deferred;
        }
    }
    const auto stats = app::SampleStore::instance().stats();
    auto result = object();
    set(result, "zones", static_cast<int>(map->zones.size()));
    set(result, "ready", ready);
    set(result, "failed", failed);
    // Zones the instrument does not reach with its settings now (a
    // microphone at zero, another articulation): decoded when they are.
    set(result, "deferred", deferred);
    set(result, "playable", ready + deferred + failed == static_cast<int>(map->zones.size()));
    // Its samples not downloaded yet.
    const auto missing = studio_.missingSamples(ref);
    set(result, "missingFiles", static_cast<int>(missing.files.size()));
    set(result, "missingBytes", missing.bytes);
    set(result, "seconds", static_cast<double>(frames) / 44'100.0);
    set(result, "storeLoaded", stats.loaded);
    set(result, "storePending", stats.pending);
    set(result, "storeMegabytes", static_cast<double>(stats.bytes) / 1.0e6);
    // Read from the disk: this map's long recordings (their heads in memory),
    // every track's: the rings playing now, the frames missed, rings refused.
    auto streamedZones = 0;
    juce::int64 heldBytes {};
    for (const auto& zone : map->zones)
        if (zone.sample != nullptr && zone.sample->ready.load() && zone.sample->streamed) {
            ++streamedZones;
            heldBytes += static_cast<juce::int64>(zone.sample->pcm.size() * sizeof(std::int16_t));
        }
    auto& streams = engine::SampleStreams::instance();
    set(result, "streamedZones", streamedZones);
    set(result, "streamedHeadMegabytes", static_cast<double>(heldBytes) / 1.0e6);
    set(result, "storeStreamed", stats.streamed);
    set(result, "streamsBusy", streams.busy());
    set(result, "streamUnderruns", static_cast<juce::int64>(streams.underruns.load()));
    set(result, "streamsRefused", static_cast<juce::int64>(streams.refused.load()));
    return ok(result);
}

// banks {action: list | download | cancel | remove | reload, bank, instrument, count, wait}:
// the sound banks CyDAW knows, what of them is here, their download.
juce::var Commands::onBanks()
{
    auto& banks = studio_.banks();
    const auto action = text("action").isNotEmpty() ? text("action") : juce::String("list");
    const auto bank = text("bank");
    if (action == "reload") {
        banks.loadRegistry();
        banks.forgetDisk();
    } else if (action == "download") {
        if (has("instrument")) {
            juce::String error;
            if (!studio_.downloadFactoryInstrument(text("instrument"), error))
                return fail(error);
        } else {
            if (banks.find(bank) == nullptr)
                return fail("no bank \"" + bank + "\"");
            const auto missing = banks.missingOfBank(bank);
            if (!missing.empty())
                banks.download(banks.find(bank)->title, missing);
        }
    } else if (action == "cancel") {
        banks.cancel();
    } else if (action == "remove") {
        juce::String error;
        if (!banks.remove(bank, error))
            return fail(error);
    } else if (action != "list") {
        return fail("unknown action \"" + action + "\"");
    }
    // Waiting for the downloads (their threads go on meanwhile).
    if (has("wait")) {
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + number("wait", 30.0) * 1'000.0;
        while (banks.busy() && juce::Time::getMillisecondCounterHiRes() < deadline)
            juce::Thread::sleep(20);
    }
    auto result = object();
    set(result, "folder", app::banksFolder().getFullPathName());
    set(result, "registry", banks.registryFile().getFullPathName());
    juce::Array<juce::var> list;
    for (const auto& entry : banks.banks()) {
        if (bank.isNotEmpty() && entry.id != bank && action == "list")
            continue;
        auto item = object();
        set(item, "id", entry.id);
        set(item, "title", entry.title);
        set(item, "author", entry.author);
        set(item, "licence", entry.licence);
        set(item, "repository", entry.repository);
        set(item, "generator", entry.generator);
        set(item, "files", entry.count);
        set(item, "bytes", entry.bytes);
        // What is here: counted now with `count`, else as last counted.
        if (flag("count", false)) {
            const auto missing = banks.missingOfBank(entry.id);
            set(item, "here", entry.count - static_cast<int>(missing.files.size()));
            set(item, "hereBytes", entry.bytes - missing.bytes);
        } else if (const auto disk = banks.onDisk(entry.id)) {
            set(item, "here", disk->count);
            set(item, "hereBytes", disk->bytes);
        }
        list.add(item);
    }
    set(result, "banks", list);
    const auto progress = banks.progress();
    auto running = object();
    set(running, "running", progress.running);
    set(running, "label", progress.label);
    set(running, "done", progress.done);
    set(running, "total", progress.total);
    set(running, "failed", progress.failed);
    set(running, "queued", progress.queued);
    set(running, "bytesDone", progress.bytesDone);
    set(running, "bytesTotal", progress.bytesTotal);
    set(running, "error", progress.error);
    set(result, "download", running);
    return ok(result);
}

// presets {track, nodeId | plugin, action: list | save | load | delete | rename | program, name,
// newName, index}: the presets of a track's card (nodeId), of its plug-in (plugin, an index), or
// of its whole chain (neither). A plug-in's own programs come too ("programs", "program"), and
// "program" chooses one (index).
// prism {track, nodeId?, init, set: {key: value}}: a Prisme card's settings by
// their keys ("a.position", "env2.decay", "m1.source"). A slot's source and
// destination may be given by name ("lfo1", "filter.cutoff"); init puts every
// setting back to its default first. Answers every value, the sources and the
// destinations by name.
juce::var Commands::onPrism()
{
    namespace prism = engine::prism;
    std::optional<RemoteTrack> track;
    if (auto error = trackOrFail(track, true); !error.isVoid())
        return error;
    auto* slot = studio_.graphFor(track->ref());
    if (slot == nullptr)
        return fail("that track has no graph");
    auto& graph = slot->graph();
    std::uint64_t nodeId = has("nodeId") ? static_cast<std::uint64_t>(static_cast<juce::int64>(request_.getProperty("nodeId", 0))) : 0;
    if (nodeId == 0)
        for (const auto& node : graph.editorNodes())
            if (node.typeKey == "prism-synth") {
                nodeId = node.id;
                break;
            }
    if (!graph.editorPrismValues(nodeId))
        return fail("no Prisme card on that track");
    // An oscillator's own table: a sound file made its table, or taken out.
    const auto oscillatorOf = [](const juce::var& value) {
        const auto text = value.toString().toLowerCase();
        return text == "b" || text == "1" ? 1 : 0;
    };
    if (const auto* import = request_.getProperty("importTable", {}).getDynamicObject()) {
        juce::String importError;
        if (!studio_.importPrismTable(track->ref(), nodeId, oscillatorOf(import->getProperty("osc")),
                                      juce::File(import->getProperty("file").toString()), importError))
            return fail(importError);
    }
    if (has("clearTable"))
        (void) studio_.clearPrismTable(track->ref(), nodeId, oscillatorOf(request_.getProperty("clearTable", {})));
    static const char* const sourceKeys[] = { "none",     "env1",     "env2",       "env3",      "env4",  "lfo1",   "lfo2",
                                              "lfo3",     "lfo4",     "velocity",   "note",      "modWheel", "aftertouch",
                                              "pitchBend", "macro1",  "macro2",     "macro3",    "macro4", "random" };
    static_assert(sizeof(sourceKeys) / sizeof(sourceKeys[0]) == static_cast<std::size_t>(prism::Source::count));
    const auto bindingOf = [nodeId](const int index) {
        return "node." + juce::String(static_cast<juce::int64>(nodeId)) + ".prism." + juce::String(prism::parameter(index).key);
    };
    const auto current = [&graph, nodeId] { return *graph.editorPrismValues(nodeId); };
    juce::StringArray problems;
    if (flag("init", false)) {
        const auto defaults = prism::defaultValues();
        const auto values = current();
        for (int index = 0; index < prism::parameterCount; ++index)
            if (values[static_cast<std::size_t>(index)] != defaults[static_cast<std::size_t>(index)])
                (void) graph.editorSetParameter(bindingOf(index), defaults[static_cast<std::size_t>(index)]);
    }
    if (const auto* settings = request_.getProperty("set", {}).getDynamicObject())
        for (const auto& property : settings->getProperties()) {
            const auto key = property.name.toString();
            const auto index = prism::parameterIndex(key.toStdString());
            if (index < 0) {
                problems.add("no setting \"" + key + "\"");
                continue;
            }
            auto number = 0.0;
            if (property.value.isString()) {
                const auto text = property.value.toString();
                auto found = -1;
                if (key.endsWith(".source")) {
                    for (int at = 0; at < static_cast<int>(prism::Source::count); ++at)
                        if (text == sourceKeys[at])
                            found = at;
                } else if (key.endsWith(".destination")) {
                    if (text == "none")
                        found = 0;
                    const auto& list = prism::destinations();
                    for (std::size_t at = 1; at < list.size(); ++at)
                        if (text == juce::String(prism::parameter(list[at]).key))
                            found = static_cast<int>(at);
                } else {
                    // A choice by its English name ("Low pass 24", "Saw up").
                    const auto& choices = prism::parameter(index).choices;
                    for (std::size_t at = 0; at < choices.size(); ++at)
                        if (text.equalsIgnoreCase(juce::String::fromUTF8(choices[at].second.c_str()))
                            || text.equalsIgnoreCase(juce::String::fromUTF8(choices[at].first.c_str())))
                            found = static_cast<int>(at);
                }
                if (found < 0) {
                    problems.add("\"" + text + "\" is not a value of " + key);
                    continue;
                }
                number = found;
            } else {
                number = static_cast<double>(property.value);
            }
            (void) graph.editorSetParameter(bindingOf(index), prism::clampValue(index, static_cast<float>(number)));
        }
    auto result = object();
    set(result, "nodeId", static_cast<juce::int64>(nodeId));
    auto values = object();
    const auto now = current();
    for (int index = 0; index < prism::parameterCount; ++index)
        if (auto* fields = values.getDynamicObject())
            fields->setProperty(juce::Identifier(prism::parameter(index).key), now[static_cast<std::size_t>(index)]);
    set(result, "values", values);
    juce::StringArray sources;
    for (const auto* key : sourceKeys)
        sources.add(key);
    set(result, "sources", strings(sources));
    juce::StringArray destinations;
    for (std::size_t at = 1; at < prism::destinations().size(); ++at)
        destinations.add(juce::String(prism::parameter(prism::destinations()[at]).key));
    set(result, "destinations", strings(destinations));
    juce::StringArray tables;
    for (int table = 0; table < prism::factoryTableCount(); ++table)
        tables.add(juce::String(prism::factoryTableName(table)));
    set(result, "tables", strings(tables));
    // The oscillators' own tables (imported): name and frames.
    auto own = object();
    if (const auto ownTables = graph.nodePrismTables(nodeId))
        for (int oscillator = 0; oscillator < prism::oscillatorCount; ++oscillator)
            if (const auto& table = ownTables->oscillator[static_cast<std::size_t>(oscillator)]) {
                auto entry = object();
                set(entry, "name", juce::String(table->name));
                set(entry, "frames", table->frameCount);
                set(own, oscillator == 0 ? "a" : "b", entry);
            }
    set(result, "ownTables", own);
    if (!problems.isEmpty())
        set(result, "problems", strings(problems));
    return ok(result);
}

// kick {track, nodeId?, init, set: {key: value}}: a Kick card's settings by
// their keys ("pitchStart", "decay"); a choice may be given by name ("Noise",
// "Tube"). init puts every setting back to its default first. Answers every
// value and what the kick is, measured off the speakers: its peak (dBFS), how
// long it lasts, the pitch it ends on.
juce::var Commands::onKick()
{
    namespace kick = engine::kick;
    std::optional<RemoteTrack> track;
    if (auto error = trackOrFail(track, true); !error.isVoid())
        return error;
    auto* slot = studio_.graphFor(track->ref());
    if (slot == nullptr)
        return fail("that track has no graph");
    auto& graph = slot->graph();
    std::uint64_t nodeId = has("nodeId") ? static_cast<std::uint64_t>(static_cast<juce::int64>(request_.getProperty("nodeId", 0))) : 0;
    if (nodeId == 0)
        for (const auto& node : graph.editorNodes())
            if (node.typeKey == "kick-synth") {
                nodeId = node.id;
                break;
            }
    if (!graph.editorKickValues(nodeId))
        return fail("no Kick card on that track");
    const auto bindingOf = [nodeId](const int index) {
        return "node." + juce::String(static_cast<juce::int64>(nodeId)) + ".kick." + juce::String(kick::parameter(index).key);
    };
    const auto current = [&graph, nodeId] { return *graph.editorKickValues(nodeId); };
    juce::StringArray problems;
    if (flag("init", false)) {
        const auto defaults = kick::defaultValues();
        const auto values = current();
        for (int index = 0; index < kick::parameterCount; ++index)
            if (values[static_cast<std::size_t>(index)] != defaults[static_cast<std::size_t>(index)])
                (void) graph.editorSetParameter(bindingOf(index), defaults[static_cast<std::size_t>(index)]);
    }
    if (const auto* settings = request_.getProperty("set", {}).getDynamicObject())
        for (const auto& property : settings->getProperties()) {
            const auto key = property.name.toString();
            const auto index = kick::parameterIndex(key.toStdString());
            if (index < 0) {
                problems.add("no setting \"" + key + "\"");
                continue;
            }
            auto number = 0.0;
            if (property.value.isString()) {
                const auto text = property.value.toString();
                auto found = -1;
                const auto& choices = kick::parameter(index).choices;
                for (std::size_t at = 0; at < choices.size(); ++at)
                    if (text.equalsIgnoreCase(juce::String::fromUTF8(choices[at].second.c_str()))
                        || text.equalsIgnoreCase(juce::String::fromUTF8(choices[at].first.c_str())))
                        found = static_cast<int>(at);
                if (found < 0) {
                    problems.add("\"" + text + "\" is not a value of " + key);
                    continue;
                }
                number = found;
            } else {
                number = static_cast<double>(property.value);
            }
            (void) graph.editorSetParameter(bindingOf(index), kick::clampValue(index, static_cast<float>(number)));
        }
    auto result = object();
    set(result, "nodeId", static_cast<juce::int64>(nodeId));
    auto values = object();
    const auto now = current();
    for (int index = 0; index < kick::parameterCount; ++index)
        if (auto* fields = values.getDynamicObject())
            fields->setProperty(juce::Identifier(kick::parameter(index).key), now[static_cast<std::size_t>(index)]);
    set(result, "values", values);
    // The kick as the voice plays it at C2, full velocity, rendered here.
    const auto samples = kick::renderKick(now, 48'000.0);
    auto peak = 0.0f;
    auto energy = 0.0;
    for (const auto x : samples) {
        peak = std::max(peak, std::abs(x));
        energy += static_cast<double>(x) * x;
    }
    set(result, "peakDb", 20.0 * std::log10(std::max(1.0e-9f, peak)));
    set(result, "rmsDb", 10.0 * std::log10(std::max(1.0e-18, energy / std::max<std::size_t>(1, samples.size()))));
    set(result, "lengthMs", kick::lengthSeconds(now) * 1'000.0);
    set(result, "endHz", static_cast<double>(now[static_cast<std::size_t>(kick::pitchEnd)]));
    if (!problems.isEmpty())
        set(result, "problems", strings(problems));
    return ok(result);
}

juce::var Commands::onSynth()
{
    namespace synth = engine::synth;
    std::optional<RemoteTrack> track;
    if (auto error = trackOrFail(track, true); !error.isVoid())
        return error;
    auto* slot = studio_.graphFor(track->ref());
    if (slot == nullptr)
        return fail("that track has no graph");
    auto& graph = slot->graph();
    // The card: by its id, or the track's first of the family (of that
    // model when one is named: "ambre").
    const auto wanted = has("model") ? synth::modelForPrefix(text("model").trim().toLowerCase().toStdString()) : synth::Model::none;
    if (has("model") && wanted == synth::Model::none)
        return fail("no synth model \"" + text("model") + "\"");
    std::uint64_t nodeId = has("nodeId") ? static_cast<std::uint64_t>(static_cast<juce::int64>(request_.getProperty("nodeId", 0))) : 0;
    if (nodeId == 0)
        for (const auto& node : graph.editorNodes()) {
            const auto model = synth::modelForType(node.typeKey.toStdString());
            if (model != synth::Model::none && (wanted == synth::Model::none || model == wanted)) {
                nodeId = node.id;
                break;
            }
        }
    const auto found = graph.editorSynthValues(nodeId);
    if (!found || found->first == synth::Model::none)
        return fail("no synth of the family on that track");
    const auto model = found->first;
    const auto& info = synth::info(model);
    const auto bindingOf = [nodeId, &info, model](const int index) {
        return "node." + juce::String(static_cast<juce::int64>(nodeId)) + "." + juce::String(info.prefix) + "."
               + juce::String(synth::parameter(model, index).key);
    };
    const auto current = [&graph, nodeId] { return graph.editorSynthValues(nodeId)->second; };
    juce::StringArray problems;
    if (flag("init", false)) {
        const auto defaults = synth::defaultValues(model);
        const auto values = current();
        for (int index = 0; index < synth::parameterCount(model); ++index)
            if (values[static_cast<std::size_t>(index)] != defaults[static_cast<std::size_t>(index)])
                (void) graph.editorSetParameter(bindingOf(index), defaults[static_cast<std::size_t>(index)]);
    }
    if (const auto* settings = request_.getProperty("set", {}).getDynamicObject())
        for (const auto& property : settings->getProperties()) {
            const auto key = property.name.toString();
            const auto index = synth::parameterIndex(model, key.toStdString());
            if (index < 0) {
                problems.add("no setting \"" + key + "\"");
                continue;
            }
            auto number = 0.0;
            if (property.value.isString()) {
                const auto text = property.value.toString();
                auto chosen = -1;
                const auto& choices = synth::parameter(model, index).choices;
                for (std::size_t at = 0; at < choices.size(); ++at)
                    if (text.equalsIgnoreCase(juce::String::fromUTF8(choices[at].second.c_str()))
                        || text.equalsIgnoreCase(juce::String::fromUTF8(choices[at].first.c_str())))
                        chosen = static_cast<int>(at);
                if (chosen < 0) {
                    problems.add("\"" + text + "\" is not a value of " + key);
                    continue;
                }
                number = chosen;
            } else if (property.value.isBool()) {
                number = static_cast<bool>(property.value) ? 1.0 : 0.0;
            } else {
                number = static_cast<double>(property.value);
            }
            (void) graph.editorSetParameter(bindingOf(index), synth::clampValue(model, index, static_cast<float>(number)));
        }
    auto result = object();
    set(result, "nodeId", static_cast<juce::int64>(nodeId));
    set(result, "model", juce::String(info.prefix));
    auto values = object();
    const auto now = current();
    for (int index = 0; index < synth::parameterCount(model); ++index)
        if (auto* fields = values.getDynamicObject())
            fields->setProperty(juce::Identifier(synth::parameter(model, index).key), now[static_cast<std::size_t>(index)]);
    set(result, "values", values);
    // Heard here, offline (nothing reaches the speakers): the notes held
    // `seconds`, then released and left to end.
    std::vector<int> notes;
    if (const auto* list = request_.getProperty("notes", {}).getArray())
        for (const auto& note : *list)
            notes.push_back(std::clamp(static_cast<int>(note), 0, 127));
    if (notes.empty())
        notes.push_back(std::clamp(static_cast<int>(number("note", 60.0)), 0, 127));
    constexpr auto rate = 48'000.0;
    const auto held = std::clamp(number("seconds", 1.0), 0.05, 10.0);
    const auto tail = std::clamp(number("tail", 2.0), 0.0, 20.0);
    const auto velocity = static_cast<float>(std::clamp(number("velocity", 1.0), 0.0, 1.0));
    const auto frames = static_cast<std::size_t>((held + tail) * rate);
    std::vector<float> left(frames, 0.0f);
    std::vector<float> right(frames, 0.0f);
    const engine::prism::Context context;
    synth::warmUp(model);
    for (const auto note : notes) {
        synth::Voice voice;
        voice.prepare(rate);
        voice.start(model, now, note, velocity, -1.0, rate);
        for (std::size_t at = 0; at < frames; at += 256) {
            if (static_cast<double>(at) >= held * rate && !voice.releasing())
                voice.release();
            const auto count = static_cast<int>(std::min<std::size_t>(256, frames - at));
            if (!voice.render(now, context, left.data() + at, right.data() + at, count, rate))
                break;
        }
    }
    auto peak = 0.0f;
    auto energy = 0.0;
    auto side = 0.0;
    std::size_t heldFrames = 0;
    for (std::size_t at = 0; at < frames; ++at) {
        peak = std::max({ peak, std::abs(left[at]), std::abs(right[at]) });
        if (static_cast<double>(at) < held * rate) {
            energy += 0.5 * (static_cast<double>(left[at]) * left[at] + static_cast<double>(right[at]) * right[at]);
            side += 0.25 * static_cast<double>(left[at] - right[at]) * (left[at] - right[at]);
            ++heldFrames;
        }
    }
    auto last = 0.0f;
    for (auto at = frames - std::min<std::size_t>(frames, static_cast<std::size_t>(0.1 * rate)); at < frames; ++at)
        last = std::max({ last, std::abs(left[at]), std::abs(right[at]) });
    const auto toDb = [](const double power) { return 10.0 * std::log10(std::max(1.0e-18, power)); };
    set(result, "peakDb", 20.0 * std::log10(std::max(1.0e-9f, peak)));
    set(result, "rmsDb", toDb(energy / static_cast<double>(std::max<std::size_t>(1, heldFrames))));
    // How much differs between the two sides (dB under the whole).
    set(result, "sideDb", toDb(side / static_cast<double>(std::max<std::size_t>(1, heldFrames)))
                              - toDb(energy / static_cast<double>(std::max<std::size_t>(1, heldFrames))));
    set(result, "endDb", 20.0 * std::log10(std::max(1.0e-9f, last)));
    if (!problems.isEmpty())
        set(result, "problems", strings(problems));
    return ok(result);
}

juce::var Commands::onBatterie()
{
    namespace drum = engine::drum;
    std::optional<RemoteTrack> track;
    if (auto error = trackOrFail(track, true); !error.isVoid())
        return error;
    auto* slot = studio_.graphFor(track->ref());
    if (slot == nullptr)
        return fail("that track has no graph");
    auto& graph = slot->graph();
    std::uint64_t nodeId = has("nodeId") ? static_cast<std::uint64_t>(static_cast<juce::int64>(request_.getProperty("nodeId", 0))) : 0;
    if (nodeId == 0)
        for (const auto& node : graph.editorNodes())
            if (node.typeKey == "batterie") {
                nodeId = node.id;
                break;
            }
    if (!graph.editorDrumValues(nodeId))
        return fail("no Batterie card on that track");
    const auto bindingOf = [nodeId](const int index) {
        return "node." + juce::String(static_cast<juce::int64>(nodeId)) + ".batterie." + juce::String(drum::parameter(index).key);
    };
    juce::StringArray problems;
    // A kit: by its number, its name or its instrument's key.
    if (has("kit")) {
        auto kit = -1;
        const auto wanted = request_.getProperty("kit", {});
        if (wanted.isString()) {
            for (std::size_t at = 0; at < drum::kits.size(); ++at)
                if (wanted.toString().equalsIgnoreCase(juce::String::fromUTF8(drum::kits[at].french))
                    || wanted.toString().equalsIgnoreCase(drum::kits[at].english) || wanted.toString() == drum::kits[at].instrument)
                    kit = static_cast<int>(at);
        } else {
            kit = static_cast<int>(wanted);
        }
        juce::String error;
        if (kit < 0 || kit >= static_cast<int>(drum::kits.size()))
            problems.add("no kit \"" + wanted.toString() + "\"");
        else if (!studio_.setBatterieKit(track->ref(), nodeId, kit, error))
            problems.add(error);
    }
    if (flag("init", false)) {
        const auto neutral = drum::neutralValues();
        const auto values = *graph.editorDrumValues(nodeId);
        for (int index = 0; index < drum::valueCount; ++index)
            if (index != drum::globalBase + drum::globalKit && values[static_cast<std::size_t>(index)] != neutral[static_cast<std::size_t>(index)])
                (void) graph.editorSetParameter(bindingOf(index), neutral[static_cast<std::size_t>(index)]);
    }
    if (const auto* settings = request_.getProperty("set", {}).getDynamicObject())
        for (const auto& property : settings->getProperties()) {
            const auto key = property.name.toString();
            const auto index = drum::parameterIndex(key.toStdString());
            if (index < 0 || index == drum::globalBase + drum::globalKit) {
                problems.add(index < 0 ? "no setting \"" + key + "\"" : "the kit is chosen by \"kit\"");
                continue;
            }
            const auto number = property.value.isBool() ? (static_cast<bool>(property.value) ? 1.0 : 0.0) : static_cast<double>(property.value);
            (void) graph.editorSetParameter(bindingOf(index), drum::clampValue(index, static_cast<float>(number)));
        }
    (void) studio_.publishGraph(track->ref(), true);
    auto result = object();
    set(result, "nodeId", static_cast<juce::int64>(nodeId));
    const auto now = *graph.editorDrumValues(nodeId);
    auto values = object();
    for (int index = 0; index < drum::valueCount; ++index)
        if (auto* fields = values.getDynamicObject())
            fields->setProperty(juce::Identifier(drum::parameter(index).key), now[static_cast<std::size_t>(index)]);
    set(result, "values", values);
    const auto kit = std::clamp(static_cast<int>(std::lround(now[static_cast<std::size_t>(drum::globalBase + drum::globalKit)])), 0,
                                static_cast<int>(drum::kits.size()) - 1);
    set(result, "kit", juce::String::fromUTF8(drum::kits[static_cast<std::size_t>(kit)].english));
    set(result, "kitInstrument", juce::String(drum::kits[static_cast<std::size_t>(kit)].instrument));
    set(result, "zones", graph.nodeSamplerJson(nodeId).getProperty("zones", {}).size());
    juce::StringArray kits;
    for (const auto& item : drum::kits)
        kits.add(item.english);
    set(result, "kits", strings(kits));
    juce::StringArray grooves;
    for (const auto& groove : drum::grooves())
        grooves.add(juce::String(groove.key));
    set(result, "grooves", strings(grooves));
    if (!problems.isEmpty())
        set(result, "problems", strings(problems));
    return ok(result);
}

juce::var Commands::onGroove()
{
    namespace drum = engine::drum;
    std::optional<RemoteTrack> track;
    if (auto error = trackOrFail(track, true); !error.isVoid())
        return error;
    if (!track->midi)
        return fail("grooves go on MIDI tracks");
    const auto groove = drum::grooveIndex(text("name").trim().toStdString());
    if (groove < 0)
        return fail("no groove \"" + text("name") + "\"; batterie lists them");
    auto fill = -1;
    if (has("fill")) {
        fill = drum::grooveIndex(text("fill").trim().toStdString());
        if (fill < 0)
            return fail("no fill \"" + text("fill") + "\"");
    }
    juce::String error;
    const auto clip = studio_.addGrooveClip(track->ref(), groove, static_cast<int>(number("bars", 4.0)), fill,
                                            has("startBeats") ? number("startBeats", 0.0) : -1.0, error);
    if (clip == 0)
        return fail(error);
    auto result = object();
    set(result, "clipId", idOf(clip));
    set(result, "hits", static_cast<int>(drum::grooveHits(groove, static_cast<int>(number("bars", 4.0)), fill).size()));
    return ok(result);
}

juce::var Commands::onPresets()
{
    std::optional<RemoteTrack> track;
    if (auto error = trackOrFail(track, true); !error.isVoid())
        return error;
    // "pad": a pad's or a layer's chain (an instrument container's).
    auto ref = track->ref();
    if (has("pad"))
        ref.pad = static_cast<int>(number("pad", -1.0));
    std::optional<Studio::PresetHolder> holder;
    std::optional<int> pluginIndex;
    PresetMenuCapture capture;
    PresetMenuApply apply;
    if (has("nodeId")) {
        const auto nodeId = static_cast<std::uint64_t>(static_cast<juce::int64>(request_.getProperty("nodeId", 0)));
        holder = studio_.devicePresetHolder(ref, nodeId);
        capture = [this, ref, nodeId] { return studio_.devicePresetState(ref, nodeId); };
        apply = [this, ref, nodeId](const juce::var& state, juce::String& error) {
            return studio_.applyDevicePreset(ref, nodeId, state, error);
        };
    } else if (has("plugin")) {
        const auto index = static_cast<int>(number("plugin", 0.0));
        pluginIndex = index;
        holder = studio_.pluginPresetHolder(ref, index);
        capture = [this, ref, index] { return studio_.pluginPresetState(ref, index); };
        apply = [this, ref, index](const juce::var& state, juce::String& error) {
            return studio_.applyPluginPreset(ref, index, state, error);
        };
    } else {
        holder = studio_.chainPresetHolder(ref);
        capture = [this, ref] { return studio_.chainPresetState(ref); };
        apply = [this, ref](const juce::var& state, juce::String& error) { return studio_.applyChainPreset(ref, state, error); };
    }
    if (!holder)
        return fail("nothing with presets there");
    const auto action = text("action").isNotEmpty() ? text("action") : juce::String("list");
    const auto name = text("name").trim();
    auto entries = presets::list(holder->kind, holder->target);
    const auto* entry = presets::find(entries, name);
    juce::String error;
    if (action == "save") {
        if (!presets::save(holder->kind, holder->target, name, capture(), error))
            return fail(error);
        studio_.setPresetName(holder->key, name);
    } else if (action == "load") {
        if (entry == nullptr)
            return fail("no preset \"" + name + "\"");
        const auto state = presets::read(*entry, holder->kind, error);
        if (!state.isObject() || !apply(state, error))
            return fail(error);
        studio_.setPresetName(holder->key, entry->name);
    } else if (action == "delete") {
        if (entry == nullptr || !presets::remove(*entry, error))
            return fail(entry == nullptr ? "no preset \"" + name + "\"" : error);
    } else if (action == "rename") {
        if (entry == nullptr || !presets::rename(*entry, text("newName"), error))
            return fail(entry == nullptr ? "no preset \"" + name + "\"" : error);
    } else if (action == "program" && pluginIndex) {
        juce::StringArray names;
        auto current = -1;
        const auto index = static_cast<int>(number("index", -1.0));
        (void) studio_.pluginPrograms(ref, *pluginIndex, names, current);
        if (index < 0 || index >= names.size())
            return fail("no program " + juce::String(index));
        if (!studio_.selectPluginProgram(ref, *pluginIndex, index, error))
            return fail(error);
        studio_.setPresetName(holder->key, names[index]);
    } else if (action != "list") {
        return fail("unknown action \"" + action + "\"");
    }
    entries = presets::list(holder->kind, holder->target);
    auto result = object();
    set(result, "kind", holder->kind);
    set(result, "target", holder->target);
    set(result, "current", studio_.presetName(holder->key));
    set(result, "folder", presets::userFolder(holder->kind, holder->target).getFullPathName());
    juce::Array<juce::var> list;
    for (const auto& item : entries) {
        auto value = object();
        set(value, "name", item.name);
        set(value, "factory", item.factory);
        list.add(value);
    }
    set(result, "presets", list);
    if (pluginIndex) {
        juce::StringArray names;
        auto current = -1;
        (void) studio_.pluginPrograms(ref, *pluginIndex, names, current);
        juce::Array<juce::var> programs;
        for (const auto& programName : names)
            programs.add(programName);
        set(result, "programs", programs);
        set(result, "program", current);
    }
    return ok(result);
}

// locations {set, folder, take, cancel}: where CyDAW keeps its banks, downloads,
// cache, companion, models, devices and exports; one changed (folder "" for its
// default), its content taken along unless `take` is false.
juce::var Commands::onLocations()
{
    if (flag("cancel", false))
        studio_.cancelLocationMove();
    if (has("set")) {
        const auto location = locations::fromKey(text("set"));
        if (!location.has_value())
            return fail("no location \"" + text("set") + "\"");
        const auto folder = text("folder").isNotEmpty() ? fileFor(text("folder")) : juce::File();
        juce::String error;
        if (!studio_.setLocation(*location, folder, flag("take", true), error))
            return fail(error);
    }
    auto result = object();
    juce::Array<juce::var> list;
    for (const auto location : allLocations) {
        auto item = object();
        set(item, "key", locations::key(location));
        set(item, "folder", locations::folder(location).getFullPathName());
        set(item, "default", locations::defaultFolder(location).getFullPathName());
        set(item, "isDefault", locations::isDefault(location));
        set(item, "movable", locations::movable(location));
        set(item, "items", locations::contentOf(location, locations::folder(location)).size());
        list.add(item);
    }
    set(result, "locations", list);
    set(result, "settings", locations::settingsFile().getFullPathName());
    const auto move = studio_.locationMove();
    auto moving = object();
    set(moving, "active", move.active);
    if (move.active) {
        set(moving, "location", locations::key(move.location));
        set(moving, "to", move.to.getFullPathName());
        set(moving, "done", move.progress.done);
        set(moving, "total", move.progress.total);
        set(moving, "current", move.progress.current);
    }
    set(result, "move", moving);
    // The sample index's folder, which follows the cache.
    set(result, "sampleCache", studio_.samples().cacheFolder().getFullPathName());
    return ok(result);
}

// Sound matching: recorded notes into a playable model (the companion).
juce::var Commands::onSoundMatching()
{
    const auto statusOf = [this] {
        const auto matching = studio_.soundMatching();
        auto value = object();
        set(value, "running", matching.running);
        set(value, "progress", matching.progress);
        set(value, "message", matching.message);
        if (matching.error.isNotEmpty())
            set(value, "error", matching.error);
        if (matching.description.isNotEmpty())
            set(value, "description", matching.description);
        if (matching.model != juce::File())
            set(value, "model", matching.model.getFullPathName());
        if (matching.track)
            set(value, "track", studio_.trackName(*matching.track));
        if (!matching.zones.isVoid())
            set(value, "zones", matching.zones);
        return value;
    };
    if (command_ == "companion_info") {
        // {python}: which Python runs it, and what it has (numpy, torch, CUDA).
        if (has("python"))
            studio_.companion().setPythonCommand(text("python"));
        juce::String error;
        auto info = studio_.companion().runNow({ "info" }, error);
        auto result = object();
        set(result, "python", studio_.companion().pythonCommand());
        set(result, "package", Companion::packageFolder().getFullPathName());
        if (info.isObject())
            set(result, "info", info);
        if (error.isNotEmpty())
            set(result, "error", error);
        return ok(result);
    }
    if (command_ == "recreate_sound") {
        // {files: [wav...], name, refine}: one zone per file; status with sound_matching.
        juce::Array<juce::File> files;
        if (const auto* list = request_.getProperty("files", {}).getArray())
            for (const auto& item : *list)
                files.add(fileFor(item.toString()));
        else if (has("file"))
            files.add(fileFor(text("file")));
        if (const auto started = studio_.recreateSound(files, text("name"), static_cast<bool>(request_.getProperty("refine", false)));
            started.failed())
            return fail(started.getErrorMessage());
        return ok(statusOf());
    }
    if (command_ == "load_sound_model") {
        // {file: .cysound, track}: on that track's sound-model card, else a new instrument.
        std::optional<TrackRef> on;
        if (has("track")) {
            std::optional<RemoteTrack> track;
            if (auto error = trackOrFail(track); !error.isVoid())
                return error;
            on = track->ref();
        }
        juce::String error;
        const auto track = studio_.loadSoundModel(fileFor(text("file")), on, error);
        if (!track)
            return fail(error);
        ui_.selectTrack(*track);
        auto result = object();
        set(result, "track", studio_.trackName(*track));
        set(result, "description", studio_.soundMatching().description);
        return ok(result);
    }
    if (command_ == "readable_patch") {
        // {track | file}: the model as a wavetable instrument on a new track.
        juce::var json;
        juce::String error;
        if (has("file")) {
            if (app::loadSoundModelFile(fileFor(text("file")), json, error) == nullptr)
                return fail(error);
        } else {
            std::optional<RemoteTrack> track;
            if (auto failure = trackOrFail(track); !failure.isVoid())
                return failure;
            json = studio_.soundModelJsonOf(track->ref());
            if (!json.isObject())
                return fail("that track has no sound model");
        }
        const auto made = studio_.buildReadablePatch(json, error);
        if (!made)
            return fail(error);
        ui_.selectTrack(*made);
        auto result = object();
        set(result, "track", studio_.trackName(*made));
        return ok(result);
    }
    // sound_matching: {cancel} or the status.
    if (static_cast<bool>(request_.getProperty("cancel", false)))
        studio_.cancelSoundMatching();
    return ok(statusOf());
}

juce::var Commands::onGetTrackGraph()
{
    std::optional<GraphTarget> target;
    if (auto error = targetOrFail(target); !error.isVoid())
        return error;
    auto summary = graphSummary(*target);
    // "native": what the engine runs, as compiled (nodes, cables, control
    // cables onto parameters), to tell a graph from what it became.
    if (static_cast<bool>(request_.getProperty("native", false)))
        if (auto* object = summary.getDynamicObject())
            object->setProperty("native", cydaw::app::serialiseNativeDspGraphState(target->graph().nativeDspGraph()));
    return ok(summary);
}

// Devices made of a graph: made from cards, kept in My devices and put
// back into any track.
juce::var Commands::onGraphDevices()
{
    if (command_ == "list_my_devices") {
        juce::Array<juce::var> devices;
        for (const auto& device : studio_.libraryDevices()) {
            auto entry = object();
            set(entry, "name", device.name);
            set(entry, "instrument", device.instrument);
            set(entry, "file", device.file.getFullPathName());
            if (device.description.isNotEmpty())
                set(entry, "description", device.description);
            devices.add(entry);
        }
        auto result = object();
        set(result, "folder", Studio::deviceLibraryFolder().getFullPathName());
        set(result, "devices", devices);
        return ok(result);
    }
    if (command_ == "create_device") {
        std::optional<GraphTarget> target;
        if (auto error = targetOrFail(target); !error.isVoid())
            return error;
        std::vector<std::uint64_t> ids;
        if (const auto* list = request_.getProperty("nodeIds", juce::var()).getArray())
            for (const auto& id : *list)
                ids.push_back(static_cast<std::uint64_t>(static_cast<juce::int64>(id)));
        juce::String error;
        const auto card = studio_.createDeviceFromSelection(*target->slot, ids, text("name").trim(), error);
        if (card == 0)
            return fail(error.isNotEmpty() ? error : juce::String("nodeIds: the cards of an audio chain to make one device of"));
        commitGraph(*target, true);
        auto result = graphSummary(*target);
        set(result, "nodeId", idOf(card));
        return ok(result);
    }
    // insert_my_device
    std::optional<RemoteTrack> track;
    if (auto error = trackOrFail(track); !error.isVoid())
        return error;
    const auto wanted = text("name").trim();
    for (const auto& device : studio_.libraryDevices())
        if (device.name.equalsIgnoreCase(wanted) || device.file.getFileNameWithoutExtension().equalsIgnoreCase(wanted)) {
            if (const auto inserted = studio_.insertLibraryDevice(track->ref(), device.file); inserted.failed())
                return fail(inserted.getErrorMessage());
            const auto target = graphTarget(*track);
            return target ? ok(graphSummary(*target)) : noGraph();
        }
    return fail("no device \"" + wanted + "\" in My devices; list_my_devices lists them");
}

juce::var Commands::workshopStatus()
{
    const auto state = studio_.workshop();
    auto result = object();
    set(result, "open", state.open);
    // The devices open, a tab each, and the simulator.
    juce::Array<juce::var> tabs;
    for (const auto& tab : studio_.workshopTabs()) {
        auto entry = object();
        set(entry, "id", static_cast<juce::int64>(tab.id));
        set(entry, "name", tab.name);
        set(entry, "instrument", tab.instrument);
        set(entry, "unsaved", tab.unsaved);
        set(entry, "linked", tab.linked);
        tabs.add(entry);
    }
    set(result, "tabs", tabs);
    set(result, "activeTab", static_cast<juce::int64>(studio_.activeWorkshopTab()));
    {
        const auto& settings = studio_.simulatorSettings();
        auto simulator = object();
        set(simulator, "active", studio_.simulatorActive());
        set(simulator, "playing", studio_.simulating());
        set(simulator, "seconds", studio_.simulationSeconds());
        set(simulator, "source", settings.source);
        set(simulator, "levelDb", settings.levelDb);
        set(simulator, "toneHz", settings.toneHz);
        set(simulator, "sample", settings.sample.getFullPathName());
        set(simulator, "pattern", settings.pattern);
        set(simulator, "rootNote", settings.rootNote);
        set(simulator, "outputDb", settings.outputDb);
        set(simulator, "side", juce::StringArray { "same", "silence", "kick" }[std::clamp(settings.sideSource, 0, 2)]);
        // Its output before the master, as its meter shows it.
        set(simulator, "levelDb", studio_.simulatorLevelDb());
        set(result, "simulator", simulator);
    }
    if (!state.open)
        return result;
    set(result, "name", state.name);
    set(result, "instrument", state.instrument);
    set(result, "unsaved", state.unsaved);
    {
        const auto [input, output] = studio_.workshopChannels();
        set(result, "inputChannels", input);
        set(result, "outputChannels", output);
    }
    if (state.file != juce::File())
        set(result, "file", state.file.getFullPathName());
    if (state.track)
        set(result, "track", studio_.trackName(*state.track));
    juce::StringArray trail;
    for (const auto& name : state.trail)
        trail.add(name);
    set(result, "trail", strings(trail));
    if (auto* root = studio_.workshopRoot()) {
        const auto info = root->graph().editorGraphInfo();
        set(result, "description", info.description);
        set(result, "author", info.author);
        juce::Array<juce::var> parameters;
        for (const auto& parameter : root->graph().editorGraphParameters()) {
            auto entry = object();
            set(entry, "key", parameter.binding);
            set(entry, "label", parameter.label);
            set(entry, "value", parameter.value);
            set(entry, "panel", parameter.showInDevice);
            set(entry, "vst3", parameter.publishToHost);
            parameters.add(entry);
        }
        set(result, "parameters", parameters);
        juce::StringArray programs;
        for (const auto& program : root->graph().editorPrograms())
            programs.add(program.name);
        set(result, "programs", strings(programs));
    }
    if (auto* slot = studio_.workshopGraph()) {
        GraphTarget target;
        target.slot = slot;
        target.workshop = true;
        set(result, "graph", graphSummary(target));
    }
    return result;
}

// The device workshop: {action: status | new | open | open_track | link |
// unlink | save | close | tab | close_tab | simulate | note | enter | leave |
// info | programs | vst3_check | vst3_export | vst3_status}.
juce::var Commands::onWorkshop()
{
    const auto action = text("action").trim().toLowerCase();
    ui_.showWorkshop();
    if (action.isEmpty() || action == "status")
        return ok(workshopStatus());
    if (action == "new") {
        const auto instrument = flag("instrument", false);
        auto name = text("name").trim();
        if (name.isEmpty())
            name = instrument ? "Instrument" : "Effect";
        studio_.newWorkshopDevice(instrument, name, static_cast<int>(number("inputChannels", 2.0)),
                                  static_cast<int>(number("outputChannels", 2.0)));
        return ok(workshopStatus());
    }
    if (action == "open") {
        const auto wanted = text("name").trim();
        juce::File file = has("file") ? fileFor(text("file")) : juce::File();
        if (file == juce::File())
            for (const auto& device : studio_.libraryDevices())
                if (device.name.equalsIgnoreCase(wanted) || device.file.getFileNameWithoutExtension().equalsIgnoreCase(wanted))
                    file = device.file;
        if (!file.existsAsFile())
            return fail("no device \"" + wanted + "\" in My devices; list_my_devices lists them");
        if (const auto opened = studio_.openWorkshopFile(file); opened.failed())
            return fail(opened.getErrorMessage());
        return ok(workshopStatus());
    }
    if (action == "open_track" || action == "link") {
        std::optional<RemoteTrack> track;
        if (auto error = trackOrFail(track); !error.isVoid())
            return error;
        const auto result = action == "link"
                                ? studio_.linkWorkshop(track->ref())
                                : studio_.openWorkshopFromTrack(track->ref(), static_cast<std::uint64_t>(number("nodeId", 0.0)));
        if (result.failed())
            return fail(result.getErrorMessage());
        return ok(workshopStatus());
    }
    if (!studio_.workshop().open)
        return fail("the workshop has no device; workshop {action: \"new\"} makes one");
    if (action == "tab" || action == "close_tab") {
        const auto id = static_cast<std::uint64_t>(number("id", 0.0));
        const auto tabs = studio_.workshopTabs();
        if (id != 0 && std::none_of(tabs.begin(), tabs.end(), [id](const auto& tab) { return tab.id == id; }))
            return fail("no tab " + juce::String(static_cast<juce::int64>(id)) + "; workshop status lists them");
        if (action == "tab")
            studio_.selectWorkshopTab(id);
        else
            studio_.closeWorkshopTab(id);
        return ok(workshopStatus());
    }
    if (action == "simulate") {
        // {play, source 0-7 | name, side (same | silence | kick), level, tone, sample, pattern 0-5 | name,
        //  root, velocity, output}
        auto settings = studio_.simulatorSettings();
        const auto indexOf = [](const juce::String& name, const juce::StringArray& names, const int fallback) {
            if (name.isEmpty())
                return fallback;
            if (name.containsOnly("0123456789"))
                return name.getIntValue();
            const auto found = names.indexOf(name, true);
            return found >= 0 ? found : fallback;
        };
        if (has("source"))
            settings.source = indexOf(text("source").trim(),
                                      { "none", "sine", "chords", "noise", "pink", "drums", "clicks", "sample" }, settings.source);
        if (has("side"))
            settings.sideSource = indexOf(text("side").trim(), { "same", "silence", "kick" }, settings.sideSource);
        if (has("level"))
            settings.levelDb = static_cast<float>(number("level", settings.levelDb));
        if (has("tone"))
            settings.toneHz = static_cast<float>(number("tone", settings.toneHz));
        if (has("sample")) {
            settings.sample = fileFor(text("sample"));
            if (!settings.sample.existsAsFile())
                return fail("no audio file \"" + text("sample") + "\"");
            if (!has("source"))
                settings.source = 7;
        }
        if (has("pattern"))
            settings.pattern = indexOf(text("pattern").trim(), { "none", "held", "chords", "arpeggio", "bass", "scale" },
                                       settings.pattern);
        if (has("root"))
            settings.rootNote = static_cast<int>(number("root", settings.rootNote));
        if (has("velocity"))
            settings.velocity = static_cast<float>(juce::jlimit(0.01, 1.0, number("velocity", settings.velocity)));
        if (has("output"))
            settings.outputDb = static_cast<float>(number("output", settings.outputDb));
        if (const auto applied = studio_.setSimulatorSettings(settings); applied.failed())
            return fail(applied.getErrorMessage());
        if (has("play"))
            studio_.setSimulating(flag("play", false));
        return ok(workshopStatus());
    }
    if (action == "note") {
        // A key of the simulator's keyboard: {note, on}.
        studio_.setSimulatorActive(true);
        studio_.simulatorNote(static_cast<int>(number("note", 60.0)), flag("on", true));
        return ok(workshopStatus());
    }
    if (action == "unlink") {
        studio_.unlinkWorkshop();
        return ok(workshopStatus());
    }
    if (action == "channels") {
        const auto now = studio_.workshopChannels();
        if (const auto set = studio_.setWorkshopChannels(static_cast<int>(number("inputChannels", now.first)),
                                                         static_cast<int>(number("outputChannels", now.second)));
            set.failed())
            return fail(set.getErrorMessage());
        return ok(workshopStatus());
    }
    if (action == "close") {
        studio_.closeWorkshop();
        return ok(workshopStatus());
    }
    if (action == "enter") {
        if (const auto entered = studio_.enterWorkshopDevice(static_cast<std::uint64_t>(number("nodeId", 0.0))); entered.failed())
            return fail(entered.getErrorMessage());
        return ok(workshopStatus());
    }
    if (action == "leave") {
        if (flag("all", false))
            studio_.leaveWorkshopDevices();
        else
            studio_.leaveWorkshopDevice();
        return ok(workshopStatus());
    }
    auto* root = studio_.workshopRoot();
    if (root == nullptr)
        return fail("the workshop has no device");
    auto& graph = root->graph();
    if(action=="runtime_state") {
        const auto state=graph.createVst3RuntimeState();
        if(state.isVoid()) {
            const auto runtime=graph.runtimeGraphExport();
            return fail(runtime.compilationResult.failed()?runtime.compilationResult.getErrorMessage():juce::String("Graph is not ready for native export"));
        }
        return ok(state);
    }
    if(action=="instrument_interface") {
        UiState state;InterfaceEditor editor(studio_,state);
        editor.setDeviceLayout(flag("compact",false));
        if(has("type")) {
            if(!editor.addInstrumentSection(*root,text("type"),text("panel")))return fail(editor.lastError().isNotEmpty()?editor.lastError():juce::String("unknown native instrument section"));
            return ok(app::serialiseModuleInterface(graph.editorInterface()));
        }
        if(!editor.addInstrumentInterface(*root,static_cast<std::uint64_t>(number("nodeId",0)),!has("panel"),text("panel")))
            return fail(editor.lastError().isNotEmpty()?editor.lastError():juce::String("no native instrument layout for this node or panel"));
        return ok(app::serialiseModuleInterface(graph.editorInterface()));
    }
    if(action=="presentation") {
        if(has("state")) {
            const auto document=request_.getProperty("state",{});
            if(const auto checked=app::validateModuleInterfaceDocument(document,graph.editorInterface());checked.failed())return fail(checked.getErrorMessage());
            const auto candidate=app::restoreModuleInterface(document);
            if(const auto result=graph.editorSetInterface(candidate,true);result.failed())return fail(result.getErrorMessage());
            root->touch();
        }
        return ok(app::serialiseModuleInterface(graph.editorInterface()));
    }
    if(action=="port_exposure") {
        const auto id=static_cast<std::uint64_t>(number("nodeId",0));
        const auto port=static_cast<std::uint64_t>(number("portId",0));
        if(!graph.editorSetPortExposure(id,port,flag("exposed",true)))return fail("unknown node or port");
        if(has("name")||has("category")) {
            for(const auto& n:graph.editorNodes())if(n.id==id)for(const auto& p:n.ports)if(p.id==port)
                graph.editorSetPortDetails(id,port,has("name")?text("name"):p.name,has("category")?text("category"):p.category);
        }
        return ok(graph.serialise());
    }
    if(action=="promote_value") {
        std::uint64_t created{};
        const auto mode=has("mode")?text("mode"):juce::String("input");
        if(mode!="input" && mode!="variable") return fail("mode must be input or variable");
        const auto result=graph.editorPromoteValue(static_cast<std::uint64_t>(number("nodeId",0)),
            static_cast<std::uint64_t>(number("portId",0)),text("binding"),mode=="input",created);
        if(result.failed())return fail(result.getErrorMessage());
        auto answer=object();set(answer,"nodeId",static_cast<juce::int64>(created));set(answer,"state",graph.serialise());
        return ok(answer);
    }
    if(action=="remove_boundary") {
        if(!graph.editorRemoveBoundary(static_cast<std::uint64_t>(number("nodeId",0))))return fail("unknown boundary");
        return ok(workshopStatus());
    }
    if(action=="component") {
        const auto source=has("source")?text("source"):CustomInterface::example();
        if(flag("build",false))CustomInterface::build(source);
        const auto job=CustomInterface::job(source);auto result=object();
        set(result,"source",source);set(result,"status",job->status.load());set(result,"log",CustomInterface::diagnostics(job));
        set(result,"library",job->file.getFullPathName());
        if(job->status.load()==2)set(result,"loadable",CustomInterface::renderer(source)!=nullptr);
        return ok(result);
    }
    if (action == "info") {
        auto info = graph.editorGraphInfo();
        if (has("name"))
            info.name = text("name");
        if (has("description"))
            info.description = text("description");
        if (has("author"))
            info.author = text("author");
        if (has("category"))
            info.category = text("category");
        studio_.setWorkshopInfo(info);
        // Which knobs it shows: {"parameter": key, "panel": bool, "vst3": bool, "label": text}.
        if (has("parameter")) {
            const auto key = text("parameter").trim();
            for (const auto& parameter : graph.editorGraphParameters())
                if (parameter.binding == key) {
                    (void) graph.setPublishedParameterExposure(key, flag("panel", parameter.showInDevice),
                                                               flag("vst3", parameter.publishToHost));
                    if (has("label"))
                        (void) graph.setGraphMemberMetadata(key, text("label"), parameter.description);
                }
        }
        return ok(workshopStatus());
    }
    // The device's programs (its presets, the exported VST3's programs):
    // {op: list | add | load | remove | rename, name, newName, runtime}; add
    // keeps the settings as they are (a name there is replaced); runtime:
    // the programs as the exported plug-in gets them.
    if (action == "programs") {
        auto* presetSlot = studio_.workshopGraph();
        if (presetSlot == nullptr)
            return fail("no graph open");
        auto& presetGraph = presetSlot->graph();
        auto programs = presetGraph.editorPrograms();
        const auto op = text("op").isNotEmpty() ? text("op") : juce::String("list");
        const auto name = text("name").trim();
        const auto found = std::find_if(programs.begin(), programs.end(), [&name](const auto& program) { return program.name == name; });
        if (op == "add") {
            if (name.isEmpty())
                return fail("a program needs a name");
            auto captured = presetGraph.editorCaptureProgram(name, flag("defaults", false));
            if (found != programs.end())
                *found = std::move(captured);
            else
                programs.push_back(std::move(captured));
            presetGraph.editorSetPrograms(programs);
        } else if (op == "load" || op == "remove" || op == "rename") {
            if (found == programs.end())
                return fail("no program \"" + name + "\"");
            if (op == "load") {
                (void) presetGraph.editorApplyProgram(*found);
            } else if (op == "remove") {
                programs.erase(found);
                presetGraph.editorSetPrograms(programs);
            } else {
                if (text("newName").trim().isEmpty())
                    return fail("newName is empty");
                if (std::any_of(programs.begin(), programs.end(), [&](const auto& other) { return &other != &*found && other.name == text("newName").trim(); }))
                    return fail("a program already has this name");
                found->name = text("newName").trim();
                presetGraph.editorSetPrograms(programs);
            }
        } else if (op != "list") {
            return fail("unknown op \"" + op + "\"");
        }
        auto result = object();
        juce::Array<juce::var> list;
        for (const auto& program : presetGraph.editorPrograms()) {
            auto entry = object();
            set(entry, "name", program.name);
            auto values = object();
            for (const auto& [key, value] : program.values)
                if (auto* fields = values.getDynamicObject())
                    fields->setProperty(juce::Identifier(key), value);
            set(entry, "values", values);
            list.add(entry);
        }
        set(result, "programs", list);
        if (flag("runtime", false))
            set(result, "runtime", presetGraph.createVst3RuntimeState().getProperty("programs", {}));
        return ok(result);
    }
    if (action == "device_export") {
        if (!has("file") || text("file").trim().isEmpty())
            return fail("device_export needs file (.cydawdevice)");
        const auto file = fileFor(text("file")).withFileExtension("cydawdevice");
        juce::StringArray dependencies;
        if (const auto saved = studio_.exportWorkshop(file, dependencies); saved.failed())
            return fail(saved.getErrorMessage());
        auto result = workshopStatus();
        set(result, "written", file.getFullPathName());
        set(result, "externalFiles", strings(dependencies));
        return ok(result);
    }
    if (action == "save") {
        juce::File written;
        if (const auto saved = studio_.saveWorkshop(written, flag("asNew", false)); saved.failed())
            return fail(saved.getErrorMessage());
        auto result = workshopStatus();
        set(result, "written", written.getFullPathName());
        return ok(result);
    }
    if (action == "plugin_export_batch") {
        const auto* formats=request_.getProperty("formats",{}).getArray();
        const auto* platforms=request_.getProperty("platforms",{}).getArray();
        if(!formats||formats->isEmpty()||!platforms||platforms->isEmpty())return fail("formats and platforms must be non-empty arrays");
        for(const auto& f:*formats)if(f!="VST3"&&f!="CLAP"&&f!="LV2")return fail("format is VST3, CLAP or LV2");
        for(const auto& p:*platforms)if(p!="Windows"&&p!="Linux")return fail("platform is Windows or Linux; macOS is deferred");
        auto identity=graph.pluginExportIdentity();
        if(has("productName"))identity.productName=text("productName").toStdString();
        if(has("companyName"))identity.companyName=text("companyName").toStdString();
        if(has("manufacturerCode"))identity.manufacturerCode=text("manufacturerCode").toStdString();
        if(has("pluginCode"))identity.pluginCode=text("pluginCode").toStdString();
        graph.setPluginExportIdentity(identity);
        if(!has("folder"))return fail("folder is required");
        auto* builder=ui_.vst3Build();
        if(!builder)return fail("this interface cannot build");
        if(builder->running())return fail("a build is already running");
        std::vector<Vst3Build::Job> jobs;
        juce::Array<juce::var> targets;
        for(const auto& p:*platforms)for(const auto& f:*formats) {
            const auto destination=fileFor(text("folder")).getChildFile(p.toString()).getChildFile(f.toString());
            const auto project=destination.getChildFile("projet");
            if(const auto made=graph.generateVst3Project(project,f.toString());made.failed())return fail(made.getErrorMessage());
            jobs.push_back({project,destination,juce::String(identity.productName),f.toString(),p.toString()});
            auto target=object();set(target,"platform",p);set(target,"format",f);set(target,"project",project.getFullPathName());targets.add(target);
        }
        if(flag("build",false)) {
            const auto tools=Vst3Build::findTools(has("sourceRoot")?fileFor(text("sourceRoot")):juce::File());
            if(!builder->startBatch(jobs,tools))return fail("could not start batch; check CMake/sourceRoot");
        }
        auto result=object();set(result,"targets",targets);return ok(result);
    }
    if (action == "vst3_check" || action == "vst3_export" || action == "clap_export" || action == "lv2_export" || action == "plugin_export") {
        if (has("productName") || has("companyName") || has("manufacturerCode") || has("pluginCode")) {
            auto identity = graph.pluginExportIdentity();
            if (has("productName"))
                identity.productName = text("productName").toStdString();
            if (has("companyName"))
                identity.companyName = text("companyName").toStdString();
            if (has("manufacturerCode"))
                identity.manufacturerCode = text("manufacturerCode").toStdString();
            if (has("pluginCode"))
                identity.pluginCode = text("pluginCode").toStdString();
            graph.setPluginExportIdentity(identity);
        }
        const auto plan = graph.planVst3Export();
        auto result = object();
        const auto identity = graph.pluginExportIdentity();
        set(result, "productName", juce::String(identity.productName));
        set(result, "manufacturerCode", juce::String(identity.manufacturerCode));
        set(result, "pluginCode", juce::String(identity.pluginCode));
        juce::StringArray issues;
        for (const auto& issue : plan.issues)
            issues.add(juce::String(issue.message));
        set(result, "issues", strings(issues));
        set(result, "ready", plan.plan.has_value() && plan.issues.empty());
        if (plan.plan) {
            set(result, "category", juce::String(plan.plan->vst3Category));
            set(result, "parameters", static_cast<int>(plan.plan->parameters.size()));
            set(result, "buses", static_cast<int>(plan.plan->buses.size()));
        }
        if (action == "vst3_check")
            return ok(result);
        // The project, into a folder given or Documents/CyDAW/Plugin Exports.
        auto folder = has("folder") ? fileFor(text("folder"))
                                    : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                          .getChildFile("CyDAW")
                                          .getChildFile("Plugin Exports")
                                          .getChildFile(juce::File::createLegalFileName(juce::String(identity.productName)));
        const auto format = action == "clap_export" ? juce::String("CLAP") : action == "lv2_export" ? juce::String("LV2") : request_.getProperty("format", "VST3").toString().toUpperCase();
        if (format != "VST3" && format != "CLAP" && format != "LV2") return fail("format is VST3, CLAP or LV2");
        const auto project = folder.getChildFile("projet");
        if (project.exists())
            (void) project.deleteRecursively();
        if (const auto made = graph.generateVst3Project(project, format); made.failed())
            return fail(made.getErrorMessage());
        set(result, "project", project.getFullPathName());
        // Built with CMake in the background when asked: vst3_status follows it.
        if (flag("build", false)) {
            auto* build = ui_.vst3Build();
            if (build == nullptr)
                return fail("this interface cannot build");
            const auto tools = Vst3Build::findTools(has("sourceRoot") ? fileFor(text("sourceRoot")) : juce::File());
            if (!tools.ready())
                return fail("building needs CMake and CyDAW's sources (sourceRoot)");
            if (!build->start(project, folder, juce::String(identity.productName), tools, format))
                return fail("a build is already running");
            set(result, "building", true);
        }
        return ok(result);
    }
    if (action == "vst3_status" || action == "vst3_cancel") {
        auto* build = ui_.vst3Build();
        if (build == nullptr)
            return fail("this interface cannot build");
        if (action == "vst3_cancel")
            build->cancel();
        static constexpr const char* phases[] = { "idle", "configuring", "building", "done", "failed", "cancelled" };
        auto result = object();
        set(result, "phase", juce::String(phases[static_cast<int>(build->phase())]));
        set(result, "seconds", build->elapsedSeconds());
        if (build->bundle() != juce::File())
            set(result, "bundle", build->bundle().getFullPathName());
        juce::Array<juce::var> targets;
        for(const auto& target:build->results()) {
            auto entry=object();set(entry,"platform",target.platform);set(entry,"format",target.format);
            set(entry,"file",target.file.getFullPathName());set(entry,"error",target.error);targets.add(entry);
        }
        set(result,"targets",targets);
        if (build->failure().isNotEmpty())
            set(result, "failure", build->failure());
        const auto log = build->log();
        juce::StringArray tail;
        for (auto index = std::max(0, log.size() - std::clamp(static_cast<int>(number("logLines",25)),1,400)); index < log.size(); ++index)
            tail.add(log[index]);
        set(result, "log", strings(tail));
        return ok(result);
    }
    return fail("unknown workshop action \"" + action + "\"");
}

juce::var Commands::onRemoveDevice()
{
    std::optional<GraphTarget> target;
    if (auto error = targetOrFail(target); !error.isVoid())
        return error;
    auto& graph = target->graph();
    const auto nodeId = static_cast<std::uint64_t>(number("nodeId", 0.0));
    // Its neighbours are joined, as the device panel's remove button does.
    const auto removed = nodeId != 0 ? graph.removeNodeFromChain(nodeId) : graph.removeNodeFromChainByType(text("key").trim());
    if (!removed)
        return fail("no such card on that track; get_track_graph lists them");
    commitGraph(*target, true);
    return ok(graphSummary(*target));
}

juce::var Commands::onModulate()
{
    std::optional<RemoteTrack> track;
    if (auto error = trackOrFail(track, true); !error.isVoid())
        return error;
    const auto targetType = text("target").trim();
    const auto parameter = text("parameter").trim();
    if (targetType.isEmpty() || parameter.isEmpty())
        return fail("target (a card's type key) and parameter (its knob's suffix) are required");
    const auto target = graphTarget(*track);
    if (!target)
        return noGraph();
    auto& graph = target->graph();
    if (command_ == "remove_modulation") {
        if (!graph.removeModulationForTest(targetType, parameter))
            return fail("nothing modulates " + targetType + "." + parameter);
        commitGraph(*target, true);
        return ok(graphSummary(*target));
    }
    auto source = text("source").trim();
    if (source.isEmpty())
        source = "lfo-core";
    if (const auto inserted = graph.insertNode(source); inserted.failed())
        return fail(source + ": " + inserted.getErrorMessage());
    if (!graph.modulateParameterForTest(source, targetType, parameter)) {
        (void) graph.removeNodeByType(source);
        commitGraph(*target, true);
        return fail("the cable did not reach " + targetType + "." + parameter
                    + "; get_track_graph gives the cards and their knobs");
    }
    graph_layout::arrange(graph);
    commitGraph(*target, true);
    auto result = object();
    set(result, "modulated", true);
    set(result, "source", source);
    set(result, "target", targetType);
    set(result, "parameter", parameter);
    // The sweep: the LFO's rate, the range node's outputs.
    auto knobsTurned = false;
    if (has("rate") && source == "lfo-core") {
        const auto key = knobKey(graph, "lfo-core", "lfo.rate");
        if (key.isNotEmpty() && graph.setGraphInterfaceParameter(key, number("rate", 1.0))) {
            set(result, "rate", number("rate", 1.0));
            knobsTurned = true;
        }
    }
    if (has("minimum") || has("maximum")) {
        const auto low = knobKey(graph, "map-range", "map.outputMinimum");
        const auto high = knobKey(graph, "map-range", "map.outputMaximum");
        if (has("minimum") && low.isNotEmpty() && graph.setGraphInterfaceParameter(low, number("minimum", 0.0))) {
            set(result, "minimum", number("minimum", 0.0));
            knobsTurned = true;
        }
        if (has("maximum") && high.isNotEmpty() && graph.setGraphInterfaceParameter(high, number("maximum", 1.0))) {
            set(result, "maximum", number("maximum", 1.0));
            knobsTurned = true;
        }
    }
    if (knobsTurned)
        commitGraph(*target, false);
    set(result, "graph", graphSummary(*target));
    return ok(result);
}

juce::var Commands::onModulator()
{
    // {track, action, ...}: the modulators of a track's cards (Modulators.h).
    //   add {nodeId, type}: one for the card nodeId ("lfo", "steps", "random",
    //     "adsr", "keytrack", "expressions", "macro", "button", "math",
    //     "follower"), its column shown;
    //   set {id, settings: {key: value}}: its settings (a choice by number or
    //     name: "sync": "1/8", "shape": "Triangle");
    //   modulate {id, binding | nodeId + parameter, amount}: it moves that knob
    //     (again: its amount); unmodulate {id, binding | nodeId + parameter};
    //   remove {id}; arm {id} (0: none); column {nodeId, open};
    //   list {nodeId?}: the modulators (their value now) and the modulations.
    std::optional<GraphTarget> target;
    if (auto error = targetOrFail(target); !error.isVoid())
        return error;
    if (!target->track)
        return fail("modulators belong to a track's cards");
    auto& graph = target->graph();
    const auto action = text("action").trim().toLowerCase();
    const auto id = static_cast<std::uint64_t>(number("id", 0.0));
    const auto nodeId = static_cast<std::uint64_t>(number("nodeId", 0.0));
    const auto nodes = graph.editorNodes();
    const auto isModulator = [&graph, &nodes](const std::uint64_t node) {
        const auto found = std::find_if(nodes.begin(), nodes.end(), [node](const auto& item) { return item.id == node; });
        if (found == nodes.end())
            return false;
        const auto* effect = engine::effectForType(found->typeKey.toStdString());
        return effect != nullptr && effect->control;
    };
    const auto bindingOf = [this]() -> juce::String {
        auto binding = text("binding").trim();
        if (binding.isEmpty() && has("nodeId"))
            binding = "node." + juce::String(static_cast<juce::int64>(number("nodeId", 0.0))) + "." + text("parameter").trim();
        return binding;
    };
    const auto describe = [&](const std::uint64_t modulator) {
        auto entry = object();
        // As the graph is now (it may have just been made).
        const auto now = graph.editorNodes();
        const auto found = std::find_if(now.begin(), now.end(), [modulator](const auto& item) { return item.id == modulator; });
        set(entry, "id", static_cast<juce::int64>(modulator));
        set(entry, "owner", static_cast<juce::int64>(graph.editorModulatorOwner(modulator)));
        if (found != now.end()) {
            set(entry, "type", found->typeKey);
            set(entry, "name", found->title);
            auto settings = object();
            for (const auto& parameter : found->parameters)
                if (!parameter.binding.endsWith(".ownerLow") && !parameter.binding.endsWith(".ownerHigh"))
                    if (const auto* setting = engine::effectParameterForSuffix(
                            parameter.binding.fromFirstOccurrenceOf(".", false, false).fromFirstOccurrenceOf(".", false, false).toStdString()))
                        set(settings, std::string(setting->key).c_str(), parameter.value);
            set(entry, "settings", settings);
        }
        if (const auto value = graph.editorControlValue(modulator))
            set(entry, "value", *value);
        juce::Array<juce::var> moves;
        for (const auto& modulation : graph.editorModulations())
            if (modulation.source == modulator) {
                auto move = object();
                set(move, "binding", modulation.binding);
                set(move, "nodeId", static_cast<juce::int64>(modulation.node));
                set(move, "amount", modulation.amount);
                set(move, "mode", modulation.mode == 1 ? "modulate" : "set");
                moves.add(move);
            }
        set(entry, "modulations", moves);
        return entry;
    };
    if (action == "add") {
        auto type = text("type").trim().toLowerCase().replaceCharacter(' ', '-');
        if (type == "keytrack" || type == "note")
            type = "mod-note";
        else if (type == "expressions" || type == "expression")
            type = "mod-expression";
        else if (type == "envelope-follower" || type == "follower")
            type = "mod-follower";
        else if (type == "mix")
            type = "mod-math";
        else if (!type.startsWith("mod-"))
            type = "mod-" + type;
        const auto* effect = engine::effectForType(type.toStdString());
        if (effect == nullptr || !effect->control)
            return fail("no modulator \"" + text("type") + "\": lfo, steps, random, adsr, keytrack, expressions, macro, button, "
                        "math, follower");
        const auto made = graph.editorAddModulator(nodeId, type);
        if (made == 0)
            return fail("no card " + juce::String(static_cast<juce::int64>(nodeId)) + " to add it to");
        commitGraph(*target, true);
        (void) ui_.showModulators(*target->track, nodeId, true, made);
        return ok(describe(made));
    }
    if (action == "column") {
        if (!ui_.showModulators(*target->track, nodeId, flag("open", true), 0))
            return fail("this interface shows no modulators");
        return ok(object());
    }
    if (action == "list") {
        juce::Array<juce::var> listed;
        for (const auto& node : nodes)
            if (isModulator(node.id) && (nodeId == 0 || graph.editorModulatorOwner(node.id) == nodeId))
                listed.add(describe(node.id));
        auto result = object();
        set(result, "modulators", listed);
        return ok(result);
    }
    if (!isModulator(id) && !(action == "arm" && id == 0))
        return fail("no modulator " + juce::String(static_cast<juce::int64>(id)) + " on that track; list gives them");
    if (action == "set") {
        const auto found = std::find_if(nodes.begin(), nodes.end(), [id](const auto& item) { return item.id == id; });
        const auto* effect = engine::effectForType(found->typeKey.toStdString());
        juce::StringArray problems;
        if (const auto* settings = request_.getProperty("settings", {}).getDynamicObject())
            for (const auto& property : settings->getProperties()) {
                const auto key = property.name.toString();
                const auto* setting = engine::effectParameter(*effect, key.toStdString());
                if (setting == nullptr) {
                    problems.add("no setting \"" + key + "\"");
                    continue;
                }
                auto value = static_cast<double>(property.value);
                if (property.value.isString()) {
                    // A choice by its name.
                    const auto wanted = property.value.toString().trim();
                    auto index = -1;
                    for (auto choice = 0; choice < 16 && index < 0; ++choice) {
                        const char* name = key == "sync" && choice < engine::modulator::divisionCount ? engine::modulator::divisionName(choice)
                                           : key == "shape" && choice < engine::modulator::shapeCount ? engine::modulator::shapeName(choice)
                                           : key == "mode" && choice < engine::modulator::mathModeCount ? engine::modulator::mathModeName(choice)
                                                                                                         : nullptr;
                        if (name != nullptr && wanted.equalsIgnoreCase(name))
                            index = choice;
                    }
                    if (index < 0) {
                        problems.add("no choice \"" + wanted + "\" for " + key);
                        continue;
                    }
                    value = index;
                }
                const auto binding = "node." + juce::String(static_cast<juce::int64>(id)) + "." + juce::String(std::string(setting->suffix));
                if (!graph.editorSetParameter(binding, value))
                    problems.add("\"" + key + "\" was refused");
            }
        commitGraph(*target, false);
        if (!problems.isEmpty())
            return fail(problems.joinIntoString("; "));
        return ok(describe(id));
    }
    if (action == "modulate" || action == "unmodulate") {
        const auto binding = bindingOf();
        if (binding.isEmpty())
            return fail("which knob: binding, or nodeId and parameter (its suffix, as list_parameters gives it)");
        if (action == "unmodulate") {
            if (!graph.editorUnmodulate(id, binding))
                return fail("that modulator does not move " + binding);
        } else if (const auto problem = graph.editorModulate(id, binding, static_cast<float>(number("amount", 0.25)));
                   problem.isNotEmpty()) {
            return fail(problem);
        }
        commitGraph(*target, action == "unmodulate");
        return ok(describe(id));
    }
    if (action == "remove") {
        if (!graph.editorRemoveNodes({ id }))
            return fail("the modulator was not removed");
        commitGraph(*target, true);
        return ok(object());
    }
    if (action == "arm") {
        if (!ui_.armModulator(*target->track, id))
            return fail("this interface arms no modulators");
        return ok(object());
    }
    return fail("action: add, set, modulate, unmodulate, remove, arm, column or list");
}

juce::var Commands::onContainer()
{
    // {track, action, ...}: the containers of a track's chain (Containers.h).
    //   insert {type, before}: fx-chain, fx-layer, fx-selector, mid-side-split,
    //     stereo-split, multiband-2, multiband-3, xy-fx, replacer; before a
    //     card (0: the chain's end);
    //   add_device {head, branch, key, before}: a device into a branch;
    //   move {nodeId, head (0: the main chain), branch, before}: a card (a
    //     container with all it holds) moved;
    //   add_branch {head}; remove_branch {head, branch}; remove {head};
    //   rename {head, branch, name}: a branch named by hand (empty: its usual name);
    //   show {head, branch}: the branch shown after the card;
    //   list: every container, its branches' cards (and the names given them).
    std::optional<GraphTarget> target;
    if (auto error = targetOrFail(target); !error.isVoid())
        return error;
    if (!target->track)
        return fail("containers belong to a track's chain");
    auto& graph = target->graph();
    const auto action = text("action").trim().toLowerCase();
    const auto head = static_cast<std::uint64_t>(number("head", 0.0));
    const auto branch = static_cast<int>(number("branch", 0.0));
    const auto before = static_cast<std::uint64_t>(number("before", 0.0));
    const auto describe = [&](const std::uint64_t id) {
        auto entry = object();
        const auto container = graph.editorContainer(id);
        if (!container)
            return entry;
        const auto nodes = graph.editorNodes();
        set(entry, "head", static_cast<juce::int64>(container->head));
        set(entry, "end", static_cast<juce::int64>(container->end));
        set(entry, "blend", static_cast<juce::int64>(container->blend));
        set(entry, "postBranch", container->postBranch);
        set(entry, "type", container->typeKey);
        juce::Array<juce::var> branches;
        for (const auto& cards : container->branches) {
            juce::Array<juce::var> listed;
            for (const auto card : cards) {
                auto item = object();
                set(item, "id", static_cast<juce::int64>(card));
                if (const auto found = std::find_if(nodes.begin(), nodes.end(), [card](const auto& node) { return node.id == card; });
                    found != nodes.end()) {
                    set(item, "type", found->typeKey);
                    set(item, "title", found->title);
                }
                listed.add(item);
            }
            branches.add(listed);
        }
        set(entry, "branches", branches);
        juce::Array<juce::var> names;
        for (int index = 0; index < static_cast<int>(container->branches.size()); ++index)
            names.add(graph.editorBranchName(id, index));
        set(entry, "names", names);
        if (container->typeKey == "replacer")
            set(entry, "instrument", studio_.replacerInstrumentName(*target->track, id));
        return entry;
    };
    const auto done = [&](const std::uint64_t id, const bool structural) {
        graph_layout::arrange(graph);
        commitGraph(*target, structural);
        return ok(describe(id));
    };
    if (action == "list") {
        juce::Array<juce::var> listed;
        for (const auto& node : graph.editorNodes())
            if (graph.editorIsContainer(node.id))
                listed.add(describe(node.id));
        auto result = object();
        set(result, "containers", listed);
        return ok(result);
    }
    if (action == "insert") {
        const auto type = text("type").trim().toLowerCase();
        const auto* effect = engine::effectForType(type.toStdString());
        if (effect == nullptr || !effect->container)
            return fail("no container \"" + type + "\": fx-chain, fx-layer, fx-selector, mid-side-split, stereo-split, multiband-2, "
                        "multiband-3, xy-fx, replacer");
        const auto made = graph.editorInsertContainer(type, before);
        if (made == 0)
            return fail("the container could not be inserted there");
        return done(made, true);
    }
    if (action == "add_device") {
        const auto key = text("key").trim();
        if (const auto added = studio_.insertDeviceInBranch(*target->track, key, head, branch, before); added.failed())
            return fail(added.getErrorMessage());
        return ok(describe(head));
    }
    if (action == "move") {
        if (!graph.editorMoveCard(static_cast<std::uint64_t>(number("nodeId", 0.0)), head, branch, before))
            return fail("that card cannot move there (inside itself, an instrument's voice, or it is already there)");
        return done(head, true);
    }
    if (!graph.editorIsContainer(head))
        return fail("no container " + juce::String(static_cast<juce::int64>(head)) + " on that track; list gives them");
    if (action == "add_branch") {
        if (!graph.editorAddBranch(head))
            return fail("this container takes no more branches");
        return done(head, true);
    }
    if (action == "remove_branch") {
        if (!graph.editorRemoveBranch(head, branch))
            return fail("that branch cannot be removed");
        return done(head, true);
    }
    if (action == "rename") {
        if (!graph.editorRenameBranch(head, branch, text("name")))
            return fail("no such branch");
        return done(head, false);
    }
    if (action == "remove") {
        if (!graph.editorRemoveContainer(head))
            return fail("the container was not removed");
        graph_layout::arrange(graph);
        commitGraph(*target, true);
        return ok(object());
    }
    if (action == "show") {
        if (!ui_.showContainerBranch(*target->track, head, branch))
            return fail("this interface shows no containers");
        return ok(describe(head));
    }
    // A Replacer's instrument: chosen (an instrument of the catalogue), its
    // chain opened in the strip (as a pad's; "open": false closes it).
    if (action == "instrument") {
        if (const auto chosen = studio_.setReplacerInstrument(*target->track, head, text("key").trim()); chosen.failed())
            return fail(chosen.getErrorMessage());
        auto result = describe(head);
        set(result, "instrument", studio_.replacerInstrumentName(*target->track, head));
        return ok(result);
    }
    if (action == "open_instrument") {
        const auto opened = studio_.replacerInstrument(*target->track, head);
        if (!opened)
            return fail("that Replacer has no instrument");
        if (!ui_.openPadChain(*target->track, flag("open", true) ? opened->pad : -1))
            return fail("this interface opens no chains");
        auto result = object();
        set(result, "pad", opened->pad);
        set(result, "instrument", studio_.replacerInstrumentName(*target->track, head));
        return ok(result);
    }
    return fail("action: list, insert, add_device, move, add_branch, remove_branch, rename, remove, show, instrument or "
                "open_instrument");
}

juce::var Commands::onDescribeNodeType()
{
    const auto description = catalogueGraph().describeNodeTypeForRemote(text("key").trim());
    if (description.isVoid())
        return fail("no node type \"" + text("key") + "\"; list_devices gives the keys");
    return ok(description);
}

juce::var Commands::onNodes()
{
    std::optional<GraphTarget> target;
    if (auto error = targetOrFail(target); !error.isVoid())
        return error;
    auto& graph = target->graph();
    const auto nodeId = static_cast<std::uint64_t>(number("nodeId", 0.0));
    if (command_ == "describe_node") {
        const auto description = graph.describeNodeForRemote(nodeId);
        if (description.isVoid())
            return fail("no card with that id; get_track_graph lists them");
        return ok(description);
    }
    if (command_ == "add_node") {
        const auto position = has("x") && has("y") ? juce::Point<float>(static_cast<float>(number("x", 0.5)),
                                                                        static_cast<float>(number("y", 0.5)))
                                                   : juce::Point<float>(-1'000.0f, -1'000.0f);
        const auto id = graph.addNodeForRemote(text("key").trim(), position);
        if (id == 0)
            return fail("no node type \"" + text("key") + "\"; list_devices gives the keys");
        commitGraph(*target, true);
        auto result = graph.describeNodeForRemote(id);
        set(result, "graph", graphSummary(*target));
        return ok(result);
    }
    if (command_ == "connect" || command_ == "disconnect") {
        const auto source = static_cast<std::uint64_t>(number("source", 0.0));
        const auto destination = static_cast<std::uint64_t>(number("destination", 0.0));
        if (command_ == "connect") {
            if (!graph.connectNodesForRemote(source, destination,
                                             static_cast<std::uint64_t>(number("sourcePort", 0.0)),
                                             static_cast<std::uint64_t>(number("destinationPort", 0.0))))
                return fail("the cable was refused: check the ids with get_track_graph"
                            " and the ports with describe_node");
            // A control cable onto a knob: "set" its value, or "modulate" it
            // around its value by amount (-1..1) of its range.
            if (has("mode") || has("amount")) {
                const auto port = static_cast<std::uint64_t>(number("destinationPort", 0.0));
                const auto cables = graph.editorConnections();
                const auto made = std::find_if(cables.rbegin(), cables.rend(), [&](const auto& cable) {
                    return cable.source == source && cable.destination == destination
                           && (port == 0 || cable.destinationPort == port);
                });
                if (made == cables.rend() || !made->drivesParameter) {
                    commitGraph(*target, true);
                    return fail("the cable is made, but it drives no knob: a mode is for a control cable onto a parameter");
                }
                const auto modeText = text("mode").trim().toLowerCase();
                const auto mode = has("mode") ? (modeText == "modulate" || modeText == "1" ? 1 : 0) : made->mode;
                (void) graph.editorSetConnectionMode(made->source, made->sourcePort, made->destination,
                                                     made->destinationPort, mode,
                                                     has("amount") ? static_cast<float>(number("amount", 0.5))
                                                                   : made->amount);
            }
        } else if (graph.disconnectNodesForRemote(source, destination) == 0) {
            return fail("no cable between those cards");
        }
        commitGraph(*target, true);
        return ok(graphSummary(*target));
    }
    if (command_ == "move_device") {
        // move_device {track, nodeId, before}: the device moved along its
        // chain, just before the card `before` (0 or none: last), as a drag
        // in the device panel does.
        const auto before = static_cast<std::uint64_t>(number("before", 0.0));
        if (!graph.editorMoveInChain(nodeId, before))
            return fail("that device cannot move there (an instrument's voice, a card the signal does not go through, or it is "
                        "already there)");
        graph_layout::arrange(graph);
        commitGraph(*target, true);
        return ok(graphSummary(*target));
    }
    if (command_ == "set_node") {
        std::optional<juce::Point<float>> position;
        if (has("x") && has("y"))
            position = juce::Point<float>(static_cast<float>(number("x", 0.0)), static_cast<float>(number("y", 0.0)));
        if (!graph.setNodePresentationForRemote(nodeId, text("title"), position))
            return fail("no card with that id");
        // Folded to its title in the device panel and the graph.
        if (has("collapsed"))
            (void) graph.editorSetCollapsed(nodeId, flag("collapsed", false));
        // Switched off: the sound goes past it (an instrument falls silent).
        if (has("bypassed"))
            (void) graph.editorSetBypassed(nodeId, flag("bypassed", false));
        commitGraph(*target, true);
        return ok(graph.describeNodeForRemote(nodeId));
    }
    if (command_ == "set_node_parameter") {
        const auto key = "node." + juce::String(static_cast<juce::int64>(nodeId)) + "." + text("parameter").trim();
        if (!has("value") || !graph.setGraphInterfaceParameter(key, number("value", 0.0)))
            return fail("no parameter \"" + text("parameter") + "\" on that card; describe_node_type lists them");
        commitGraph(*target, false);
        auto result = object();
        set(result, "key", key);
        set(result, "value", number("value", 0.0));
        return ok(result);
    }
    if (command_ == "expose_parameter") {
        const auto key = text("key").trim();
        const auto interfaceState = graph.graphInterfaceState();
        const auto current = std::find_if(interfaceState.parameters.begin(), interfaceState.parameters.end(),
                                          [&key](const auto& parameter) { return parameter.key == key; });
        if (current == interfaceState.parameters.end())
            return fail("no published parameter \"" + key + "\"");
        const auto device = has("device") ? flag("device", true) : current->showInDevice;
        const auto host = has("host") ? flag("host", true) : current->publishToHost;
        if (!graph.setPublishedParameterExposure(key, device, host))
            return fail("the exposure could not be changed");
        commitGraph(*target, true);
        auto result = object();
        set(result, "key", key);
        set(result, "device", device);
        set(result, "host", host);
        return ok(result);
    }
    if (command_ == "add_comment") {
        const auto id = graph.addCommentForRemote(
            text("title"), text("text"),
            { static_cast<float>(number("x", 0.1)), static_cast<float>(number("y", 0.1)) },
            { static_cast<float>(number("width", 0.0)), static_cast<float>(number("height", 0.0)) });
        commitGraph(*target, true);
        auto result = object();
        set(result, "commentId", idOf(id));
        return ok(result);
    }
    // remove_comment
    const auto commentId = idFrom(request_.getProperty("commentId", juce::var()));
    if (!graph.removeCommentForRemote(commentId))
        return fail("no comment with that id");
    commitGraph(*target, true);
    auto result = object();
    set(result, "removed", true);
    return ok(result);
}

// ---- Audio clips and automation ------------------------------------------------------------------

juce::var Commands::onClipLanes()
{
    // clip_lanes {track, scene | clipId, event?, lanes?: {name: [[seconds, value], ...]}}:
    // an audio clip's automation lanes ("volume" dB, "pan" -1..1, a card's
    // knob by its binding), read, or set lane by lane (an empty list removes
    // one); seconds of the clip's content. With event (a sound's id,
    // audio_events / list_audio_clips give them): that sound's own lanes
    // ("volume" dB, "pan" -1..1, "pitch" semitones), in its own seconds.
    std::optional<RemoteTrack> track;
    if (auto error = trackOrFail(track); !error.isVoid())
        return error;
    if (track->midi)
        return fail("clip lanes are on audio clips");
    const auto index = track->index;
    const auto launcher = has("scene");
    const auto scene = static_cast<int>(number("scene", -1.0));
    const auto clipId = idFrom(request_.getProperty("clipId", juce::var()));
    std::map<juce::String, std::vector<Engine::AutomationPoint>> lanes;
    if (has("event")) {
        // A sound's own lanes.
        const auto eventId = idFrom(request_.getProperty("event", juce::var()));
        std::vector<Engine::AudioEvent> events;
        if (launcher) {
            const auto clip = engine_.launcherClip(index, scene);
            if (!clip)
                return fail("no launcher clip in that slot");
            events = clip->events;
        } else {
            events = engine_.audioClipEvents(index, clipId);
        }
        auto sound = std::find_if(events.begin(), events.end(), [eventId](const auto& e) { return e.id == eventId; });
        if (sound == events.end())
            return fail("no sound " + idOf(eventId).toString() + " in that clip (a clip of one sound gets its sounds when edited)");
        if (const auto* given = request_.getProperty("lanes", juce::var()).getDynamicObject()) {
            for (const auto& property : given->getProperties()) {
                std::vector<Engine::AutomationPoint> points;
                std::uint64_t id = 1;
                if (const auto* list = property.value.getArray())
                    for (const auto& pair : *list)
                        if (const auto* values = pair.getArray(); values != nullptr && values->size() >= 2)
                            points.push_back({ id++, static_cast<double>((*values)[0]), static_cast<float>(static_cast<double>((*values)[1])) });
                if (points.empty())
                    sound->lanes.erase(property.name.toString());
                else
                    sound->lanes[property.name.toString()] = std::move(points);
            }
            const auto kept = sound->lanes;
            const auto done = launcher ? engine_.replaceLauncherClipEvents(index, scene, std::move(events))
                                       : engine_.replaceAudioClipEvents(index, clipId, std::move(events));
            if (done.failed())
                return fail(done.getErrorMessage());
            studio_.markDirty();
            auto result = object();
            set(result, "lanes", lanesToVar(kept));
            return ok(result);
        }
        auto result = object();
        set(result, "lanes", lanesToVar(sound->lanes));
        return ok(result);
    }
    if (launcher) {
        const auto clip = engine_.launcherClip(index, scene);
        if (!clip)
            return fail("no launcher clip in that slot");
        lanes = clip->lanes;
    } else {
        const auto* clips = engine_.audioClipsView(index);
        if (clips == nullptr)
            return fail("no audio clips on that track");
        const auto found = std::find_if(clips->begin(), clips->end(), [clipId](const auto& c) { return c.id == clipId; });
        if (found == clips->end())
            return fail("no audio clip " + idOf(clipId).toString() + "; list_audio_clips gives them");
        lanes = found->lanes;
    }
    if (const auto* given = request_.getProperty("lanes", juce::var()).getDynamicObject()) {
        for (const auto& property : given->getProperties()) {
            std::vector<Engine::AutomationPoint> points;
            std::uint64_t id = 1;
            if (const auto* list = property.value.getArray())
                for (const auto& pair : *list)
                    if (const auto* values = pair.getArray(); values != nullptr && values->size() >= 2)
                        points.push_back({ id++, static_cast<double>((*values)[0]), static_cast<float>(static_cast<double>((*values)[1])) });
            if (points.empty())
                lanes.erase(property.name.toString());
            else
                lanes[property.name.toString()] = std::move(points);
        }
        const auto done = launcher ? engine_.setLauncherClipLanes(index, scene, lanes) : engine_.setAudioClipLanes(index, clipId, lanes);
        if (!done)
            return fail("the lanes could not be set");
        studio_.publishParameterAutomation(track->ref());
        studio_.markDirty();
    }
    auto result = object();
    auto shown = object();
    for (const auto& [name, points] : lanes) {
        juce::Array<juce::var> list;
        for (const auto& point : points)
            list.add(juce::Array<juce::var> { point.timeSeconds, static_cast<double>(point.valueDb) });
        set(shown, name.toRawUTF8(), list);
    }
    set(result, "lanes", shown);
    return ok(result);
}

juce::var Commands::onAudioClips()
{
    std::optional<RemoteTrack> track;
    if (auto error = trackOrFail(track); !error.isVoid())
        return error;
    if (track->midi)
        return fail("\"" + text("track") + "\" is a MIDI track; audio clips go on audio tracks");
    const auto index = track->index;
    if (command_ == "list_audio_clips")
        return ok(describeAudioClips(index));
    if (command_ == "add_audio_clip") {
        const auto file = has("file") ? fileFor(text("file")) : juce::File {};
        if (file != juce::File {} && !file.existsAsFile())
            return fail("no audio file at " + file.getFullPathName());
        auto duration = number("duration", 0.0);
        if (duration <= 0.0) {
            if (file != juce::File {}) {
                const std::unique_ptr<juce::AudioFormatReader> reader(engine_.formatManager().createReaderFor(file));
                if (reader == nullptr)
                    return fail("the file could not be read as audio");
                duration = static_cast<double>(reader->lengthInSamples) / std::max(1.0, reader->sampleRate);
            } else {
                duration = engine_.trackInfo(index).sourceLengthSeconds;
            }
        }
        if (duration <= 0.0)
            return fail("give a duration, or a file with some length");
        const auto clipId = engine_.addAudioClip(index, std::max(0.0, number("start", 0.0)),
                                                 std::max(0.0, number("offset", 0.0)), duration, text("name"),
                                                 0xffff9b2fu, file, std::max(0.0, number("fadeIn", 0.0)),
                                                 std::max(0.0, number("fadeOut", 0.0)));
        if (clipId == 0)
            return fail("the clip could not be added");
        studio_.markDirty();
        auto result = describeAudioClips(index);
        set(result, "clipId", idOf(clipId));
        return ok(result);
    }
    const auto clipId = idFrom(request_.getProperty("clipId", juce::var()));
    if (clipId == 0)
        return fail("clipId is required; list_audio_clips gives them");
    if (command_ == "move_audio_clip") {
        if (!engine_.moveAudioClip(index, clipId, std::max(0.0, number("start", 0.0))))
            return fail("no audio clip with that id on that track");
        studio_.markDirty();
        auto result = describeAudioClips(index);
        set(result, "clipId", idOf(clipId));
        set(result, "start", number("start", 0.0));
        return ok(result);
    }
    // duplicate_audio_clip
    const auto copy = engine_.duplicateAudioClip(index, clipId, has("start") ? number("start", 0.0) : -1.0);
    if (copy == 0)
        return fail("no audio clip with that id on that track");
    studio_.markDirty();
    auto result = describeAudioClips(index);
    set(result, "clipId", idOf(copy));
    return ok(result);
}

juce::var Commands::onEditAudioClip()
{
    std::optional<RemoteTrack> track;
    if (auto error = trackOrFail(track, true); !error.isVoid())
        return error;
    if (track->midi || track->bus || track->master)
        return fail("\"" + text("track") + "\" is not an audio track");
    const auto index = track->index;
    if (command_ == "range_edit") {
        using Operation = Engine::ArrangementRangeOperation;
        const auto operation = text("operation");
        const auto kind = operation == "split"     ? std::optional<Operation>(Operation::splitAtBoundaries)
                          : operation == "crop"    ? std::optional<Operation>(Operation::cropToRange)
                          : operation == "silence" ? std::optional<Operation>(Operation::silenceRange)
                                                   : std::optional<Operation> {};
        if (!kind)
            return fail("operation is split, crop or silence");
        std::vector<std::uint64_t> clipIds;
        if (const auto* ids = request_.getProperty("clipIds", juce::var()).getArray())
            for (const auto& id : *ids)
                clipIds.push_back(idFrom(id));
        if (clipIds.empty())
            if (const auto* clips = engine_.audioClipsView(index))
                for (const auto& clip : *clips)
                    clipIds.push_back(clip.id);
        (void) engine_.editAudioClipRange(index, clipIds, std::max(0.0, number("start", 0.0)),
                                          std::max(0.0, number("end", 0.0)), *kind);
        studio_.markDirty();
        return ok(describeAudioClips(index));
    }
    const auto clipId = idFrom(request_.getProperty("clipId", juce::var()));
    const auto clip = engine_.audioClip(index, clipId);
    if (!clip)
        return fail("no audio clip with that id; list_audio_clips gives them");
    if (command_ == "edit_audio") {
        // {operation, start, length (seconds of its source), level?, curve?}:
        // a destructive edit of the clip's sound (kept in its history).
        const auto name = text("operation").trim().toLowerCase();
        using Op = app::AudioEditOperation;
        struct Kind {
            const char* key;
            Op operation;
            const char* label;
        };
        static const Kind kinds[] = {
            { "fade-in", Op::fadeIn, "Fondu entrant" },         { "fade-out", Op::fadeOut, "Fondu sortant" },
            { "silence", Op::silence, "Silence" },              { "reverse", Op::reverse, "Inverser" },
            { "gain", Op::amplify, "Gain" },                    { "normalize", Op::normalisePeak, "Normaliser" },
            { "dc", Op::removeDcOffset, "Retirer le continu" }, { "polarity", Op::invertPolarity, "Inverser la phase" },
            { "swap", Op::swapStereo, "Echanger les canaux" },  { "cut", Op::cut, "Couper" },
            { "crop", Op::crop, "Rogner" },                     { "insert-silence", Op::insertSilence, "Inserer du silence" },
            { "paste", Op::paste, "Coller" },
        };
        const Kind* kind = nullptr;
        for (const auto& candidate : kinds)
            if (name == candidate.key)
                kind = &candidate;
        if (kind == nullptr)
            return fail("operation is fade-in, fade-out, silence, reverse, gain, normalize, dc, polarity, swap, cut, crop, "
                        "insert-silence or paste");
        app::AudioEditRequest request;
        request.operation = kind->operation;
        request.levelDb = static_cast<float>(number("level", kind->operation == Op::normalisePeak ? -1.0 : 0.0));
        const auto curve = text("curve").trim().toLowerCase();
        request.curve = curve == "equal-power" ? app::FadeCurve::equalPower
                        : curve == "slow"      ? app::FadeCurve::slowStart
                        : curve == "fast"      ? app::FadeCurve::fastStart
                                               : app::FadeCurve::linear;
        const auto edited = studio_.editAudioClip(track->ref(), clipId, request, std::max(0.0, number("start", clip->sourceOffsetSeconds)),
                                                  std::max(0.0, number("length", clip->sourceDurationSeconds)),
                                                  juce::String::fromUTF8(kind->label));
        if (edited.failed())
            return fail(edited.getErrorMessage());
        return ok(describeAudioClips(index));
    }
    if (command_ == "copy_audio") {
        studio_.copyAudio(clip->sourceFile, std::max(0.0, number("start", clip->sourceOffsetSeconds)),
                          std::max(0.0, number("length", clip->sourceDurationSeconds)));
        auto result = object();
        set(result, "copied", studio_.audioClipboard().has_value());
        return ok(result);
    }
    if (command_ == "audio_clip_history") {
        // {version?}: the clip's versions (its destructive edits), one of
        // them played.
        if (has("version") && !engine_.setAudioClipVersion(index, clipId, static_cast<std::size_t>(std::max(0.0, number("version", 0.0)))))
            return fail("no such version");
        if (has("version"))
            studio_.markDirty();
        const auto now = engine_.audioClip(index, clipId);
        juce::Array<juce::var> versions;
        for (const auto& version : now->versions) {
            auto entry = object();
            set(entry, "label", version.label);
            set(entry, "file", version.file.getFullPathName());
            set(entry, "duration", version.durationSeconds);
            set(entry, "sourceLength", version.sourceLengthSeconds);
            versions.add(entry);
        }
        auto result = object();
        set(result, "versions", versions);
        set(result, "version", static_cast<int>(now->version));
        set(result, "duration", now->durationSeconds);
        return ok(result);
    }
    if (command_ == "audio_events") {
        // {action: list | add | move | trim | split | remove | duplicate | set
        //  | edit, eventId, file, start, end, at, gainDb, fadeIn, fadeOut,
        //  stretchMode, operation, level}: the sounds a clip is made of, on
        // its own timeline (seconds from its start). A clip of one sound
        // lists that sound; changing it makes the clip a timeline of events.
        const auto action = text("action").trim().toLowerCase();
        const auto eventId = idFrom(request_.getProperty("eventId", juce::var()));
        const auto events = engine_.audioClipEvents(index, clipId);
        const auto event = std::find_if(events.begin(), events.end(), [eventId](const auto& e) { return e.id == eventId; });
        const auto needsEvent = action == "move" || action == "trim" || action == "split" || action == "remove" || action == "duplicate"
                                || action == "set" || action == "edit";
        if (needsEvent && event == events.end())
            return fail("no event with that id in that clip; action list gives them");
        auto result = object();
        if (action == "add") {
            std::uint64_t added {};
            if (const auto done = engine_.addAudioClipEventFromFile(index, clipId, fileFor(text("file")), std::max(0.0, number("start", 0.0)),
                                                                    &added);
                done.failed())
                return fail(done.getErrorMessage());
            set(result, "eventId", idOf(added));
        } else if (action == "move") {
            if (!engine_.updateAudioClipEvent(index, clipId, eventId, std::max(0.0, number("start", event->startSeconds)),
                                              event->sourceOffsetSeconds, event->durationSeconds, event->sourceDurationSeconds))
                return fail("the event could not be moved");
        } else if (action == "trim") {
            // Its edges on the clip's timeline, its sound staying in place.
            const auto ratio = event->sourceDurationSeconds / std::max(1.0e-6, event->durationSeconds);
            const auto start = std::max(0.0, number("start", event->startSeconds));
            const auto stop = number("end", event->startSeconds + event->durationSeconds);
            const auto offset = event->sourceOffsetSeconds + (start - event->startSeconds) * ratio;
            if (offset < -1.0e-9 || stop - start < 0.001
                || offset + (stop - start) * ratio > std::max(event->sourceLengthSeconds, event->sourceOffsetSeconds + event->sourceDurationSeconds) + 1.0e-6)
                return fail("those edges go past the event's sound");
            if (!engine_.updateAudioClipEvent(index, clipId, eventId, start, std::max(0.0, offset), stop - start, (stop - start) * ratio))
                return fail("the event could not be trimmed");
        } else if (action == "split") {
            const auto made = engine_.splitAudioClipEvent(index, clipId, eventId, number("at", event->startSeconds));
            if (made == 0)
                return fail("the event does not split there");
            set(result, "eventId", idOf(made));
        } else if (action == "remove") {
            if (!engine_.removeAudioClipEvent(index, clipId, eventId))
                return fail("the event could not be removed");
        } else if (action == "duplicate") {
            auto all = events;
            auto copy = *event;
            copy.id = 0;
            copy.startSeconds = has("start") ? std::max(0.0, number("start", 0.0)) : event->startSeconds + event->durationSeconds;
            for (auto& marker : copy.stretchMarkers)
                marker.id = 0;
            all.push_back(copy);
            if (const auto done = engine_.replaceAudioClipEvents(index, clipId, std::move(all)); done.failed())
                return fail(done.getErrorMessage());
        } else if (action == "set") {
            if (has("gainDb") || has("stretchMode")) {
                const auto stretch = stretchModeNamed(text("stretchMode"), event->stretchMode);
                if (!engine_.setAudioClipEventProperties(index, clipId, eventId, static_cast<float>(number("gainDb", event->gainDb)),
                                                         event->sourceTempoBpm, stretch))
                    return fail("the event refused those settings");
            }
            if (has("pitch")) {
                // Its own transposition, its length kept; on the events as
                // they are now (the settings above may have changed them).
                auto all = engine_.audioClipEvents(index, clipId);
                for (auto& item : all)
                    if (item.id == eventId)
                        item.pitchSemitones = juce::jlimit(-48.0, 48.0, number("pitch", 0.0));
                if (const auto done = engine_.replaceAudioClipEvents(index, clipId, std::move(all)); done.failed())
                    return fail(done.getErrorMessage());
            }
            if ((has("fadeIn") || has("fadeOut"))
                && !engine_.updateAudioClipEvent(index, clipId, eventId, event->startSeconds, event->sourceOffsetSeconds,
                                                 event->durationSeconds, event->sourceDurationSeconds,
                                                 has("fadeIn") ? std::max(0.0, number("fadeIn", 0.0)) : -1.0,
                                                 has("fadeOut") ? std::max(0.0, number("fadeOut", 0.0)) : -1.0))
                return fail("the event refused those fades");
        } else if (action == "edit") {
            const auto operation = text("operation").trim().toLowerCase();
            app::AudioEditRequest request;
            request.operation = operation == "normalize"  ? app::AudioEditOperation::normalisePeak
                                : operation == "reverse"  ? app::AudioEditOperation::reverse
                                : operation == "polarity" ? app::AudioEditOperation::invertPolarity
                                : operation == "gain"     ? app::AudioEditOperation::amplify
                                                          : app::AudioEditOperation::removeDcOffset;
            if (operation != "normalize" && operation != "reverse" && operation != "polarity" && operation != "gain" && operation != "dc")
                return fail("operation is normalize, reverse, polarity, gain or dc");
            request.levelDb = static_cast<float>(number("level", operation == "normalize" ? -1.0 : 0.0));
            if (const auto done = studio_.editAudioEvent(track->ref(), clipId, eventId, request); done.failed())
                return fail(done.getErrorMessage());
        } else if (action.isNotEmpty() && action != "list") {
            return fail("action is list, add, move, trim, split, remove, duplicate, set or edit");
        }
        if (action.isNotEmpty() && action != "list")
            studio_.markDirty();
        const auto now = engine_.audioClip(index, clipId);
        juce::Array<juce::var> list;
        for (const auto& item : engine_.audioClipEvents(index, clipId)) {
            auto entry = object();
            set(entry, "id", idOf(item.id));
            set(entry, "name", item.name);
            set(entry, "file", item.sourceFile.getFullPathName());
            set(entry, "start", item.startSeconds);
            set(entry, "duration", item.durationSeconds);
            set(entry, "offset", item.sourceOffsetSeconds);
            set(entry, "sourceDuration", item.sourceDurationSeconds);
            set(entry, "gainDb", item.gainDb);
            set(entry, "fadeIn", item.fadeInSeconds);
            set(entry, "fadeOut", item.fadeOutSeconds);
            set(entry, "pitch", item.pitchSemitones);
            set(entry, "stretchMode", stretchModeName(item.stretchMode));
            if (!item.lanes.empty())
                set(entry, "lanes", lanesToVar(item.lanes));
            list.add(entry);
        }
        set(result, "events", list);
        set(result, "eventTimeline", now && now->usesEventTimeline);
        set(result, "duration", now ? now->durationSeconds : 0.0);
        return ok(result);
    }
    if (command_ == "audio_takes") {
        // {action: list | add | activate | remove | comp | flatten, file,
        //  shift, take, comp: [{start, end, take}]}: the clip's takes and the
        // passages it plays of each; flatten makes the comp one file.
        const auto action = text("action").trim().toLowerCase();
        if (action == "add") {
            std::vector<Engine::AudioTake> takes;
            takes.push_back({ {}, fileFor(text("file")), 0.0, number("shift", 0.0) });
            if (const auto added = engine_.addAudioTakes(index, clipId, std::move(takes), flag("activate", true)); added.failed())
                return fail(added.getErrorMessage());
        } else if (action == "activate") {
            if (!engine_.setActiveAudioTake(index, clipId, static_cast<std::size_t>(std::max(0.0, number("take", 0.0)))))
                return fail("no such take");
            (void) engine_.setAudioClipComp(index, clipId, {});
        } else if (action == "remove") {
            if (!engine_.removeAudioTake(index, clipId, static_cast<std::size_t>(std::max(0.0, number("take", 0.0)))))
                return fail("that take could not be removed");
        } else if (action == "comp") {
            std::vector<Engine::CompSegment> comp;
            if (const auto* parts = request_.getProperty("comp", {}).getArray())
                for (const auto& part : *parts)
                    comp.push_back({ static_cast<double>(part.getProperty("start", 0.0)), static_cast<double>(part.getProperty("end", 0.0)),
                                     static_cast<std::size_t>(std::max(0, static_cast<int>(part.getProperty("take", 0)))) });
            if (!engine_.setAudioClipComp(index, clipId, std::move(comp)))
                return fail("the passages were refused");
        } else if (action == "flatten") {
            if (const auto done = studio_.flattenAudioClipComp(track->ref(), clipId); done.failed())
                return fail(done.getErrorMessage());
        } else if (action.isNotEmpty() && action != "list") {
            return fail("action is list, add, activate, remove, comp or flatten");
        }
        if (action.isNotEmpty() && action != "list")
            studio_.markDirty();
        const auto now = engine_.audioClip(index, clipId);
        juce::Array<juce::var> takes;
        for (const auto& take : now->takes) {
            auto entry = object();
            set(entry, "name", take.name);
            set(entry, "file", take.sourceFile.getFullPathName());
            set(entry, "shift", take.shiftSeconds);
            set(entry, "length", take.sourceLengthSeconds);
            takes.add(entry);
        }
        juce::Array<juce::var> comp;
        for (const auto& part : now->comp) {
            auto entry = object();
            set(entry, "start", part.startSeconds);
            set(entry, "end", part.endSeconds);
            set(entry, "take", static_cast<int>(part.take));
            comp.add(entry);
        }
        auto result = object();
        set(result, "takes", takes);
        set(result, "activeTake", static_cast<int>(now->activeTakeIndex));
        set(result, "comp", comp);
        return ok(result);
    }
    if (command_ == "split_audio_clip") {
        const auto piece = engine_.splitAudioClip(index, clipId, number("at", clip->startSeconds));
        if (piece == 0)
            return fail("the clip did not split there");
        studio_.markDirty();
        auto result = describeAudioClips(index);
        set(result, "newClipId", idOf(piece));
        return ok(result);
    }
    if (command_ == "crossfade_audio_clip") {
        if (!engine_.autoCrossfadeAudioClip(index, clipId))
            return fail("no neighbour to crossfade with");
        studio_.markDirty();
        return ok(describeAudioClips(index));
    }
    if (command_ == "stretch_audio_clip") {
        if (!engine_.stretchAudioClip(index, clipId, std::max(0.01, number("duration", clip->durationSeconds))))
            return fail("the clip could not be stretched");
        studio_.markDirty();
        return ok(describeAudioClips(index));
    }
    if (command_ == "set_audio_clip_bounds") {
        const auto duration = std::max(0.01, number("duration", clip->durationSeconds));
        if (!engine_.setAudioClipTimelineBounds(index, clipId, duration,
                                                std::max(0.0, number("playStart", clip->playStartSeconds)),
                                                std::max(0.0, number("playEnd", duration)),
                                                std::max(0.0, number("loopStart", 0.0)),
                                                std::max(0.0, number("loopEnd", duration)),
                                                flag("looping", clip->looping)))
            return fail("the clip refused those bounds");
        studio_.markDirty();
        return ok(describeAudioClips(index));
    }
    // set_audio_clip
    if (has("start") || has("offset") || has("duration") || has("fadeIn") || has("fadeOut") || has("sourceDuration"))
        if (!engine_.updateAudioClip(index, clipId, std::max(0.0, number("start", clip->startSeconds)),
                                     std::max(0.0, number("offset", clip->sourceOffsetSeconds)),
                                     std::max(0.01, number("duration", clip->durationSeconds)), number("fadeIn", -1.0),
                                     number("fadeOut", -1.0), number("sourceDuration", -1.0)))
            return fail("the clip refused those values");
    if (has("pitch") && !engine_.setAudioClipPitch(index, clipId, number("pitch", 0.0)))
        return fail("the transposition was refused");
    // Warp markers: "clearMarkers", then "markers": [{source, timeline}]
    // (seconds of its source, seconds from the clip's start).
    if (flag("clearMarkers", false))
        for (const auto& marker : clip->stretchMarkers)
            (void) engine_.removeAudioClipStretchMarker(index, clipId, marker.id);
    if (const auto* markers = request_.getProperty("markers", juce::var()).getArray())
        for (const auto& marker : *markers)
            if (engine_.addAudioClipStretchMarker(index, clipId, static_cast<double>(marker.getProperty("source", 0.0)),
                                                  static_cast<double>(marker.getProperty("timeline", 0.0)))
                == 0)
                return fail("a warp marker was refused (inside the clip, in order with the others)");
    if (has("gainDb") || has("sourceTempo") || has("stretchMode")) {
        using StretchMode = Engine::StretchMode;
        const auto mode = stretchModeNamed(text("stretchMode"), clip->stretchMode);
        const auto tempo = number("sourceTempo", clip->sourceTempoBpm);
        if (clip->events.empty()) {
            if (!engine_.setAudioClipStretchSettings(index, clipId, tempo, mode))
                return fail("the stretch settings were refused");
        } else {
            for (const auto& event : clip->events)
                (void) engine_.setAudioClipEventProperties(index, clipId, event.id,
                                                           static_cast<float>(number("gainDb", event.gainDb)), tempo,
                                                           mode);
        }
    }
    studio_.markDirty();
    return ok(describeAudioClips(index));
}

juce::var Commands::onAutomation()
{
    std::optional<RemoteTrack> track;
    if (auto error = trackOrFail(track); !error.isVoid())
        return error;
    // A plugin parameter as the target, on an audio track: "plugin" (its
    // index) and "parameter" (index or name).
    std::optional<std::pair<std::size_t, std::uint32_t>> pluginTarget;
    juce::String pluginParameterName;
    if (has("plugin") && !track->midi) {
        const auto pluginIndex = static_cast<std::size_t>(number("plugin", 0.0));
        std::vector<app::IsolatedPluginHost::ParameterSnapshot> pluginParameters;
        if (engine_.refreshTrackPluginParameters(track->index, pluginIndex, pluginParameters).failed())
            return fail("no plugin " + juce::String(static_cast<int>(pluginIndex)) + " on that track");
        const auto spec = request_.getProperty("parameter", juce::var());
        for (const auto& parameter : pluginParameters)
            if ((spec.isInt() || spec.isDouble() || spec.isInt64())
                    ? parameter.index == static_cast<std::uint32_t>(static_cast<int>(spec))
                    : parameter.name.equalsIgnoreCase(spec.toString().trim())) {
                pluginTarget = std::make_pair(pluginIndex, parameter.index);
                pluginParameterName = parameter.name;
            }
        if (!pluginTarget)
            return fail("no parameter \"" + spec.toString() + "\" on that plugin");
    }
    const auto ref = track->ref();
    // A device's parameter as the target: "key", a published parameter of
    // the track's graph (list_parameters gives them), values in its units.
    if (has("key") && !pluginTarget) {
        const auto key = text("key").trim();
        const auto target = graphTarget(*track);
        if (!target)
            return noGraph();
        const auto parameters = target->graph().graphInterfaceState().parameters;
        const auto parameter = std::find_if(parameters.begin(), parameters.end(),
                                            [&key](const auto& item) { return item.key == key; });
        if (parameter == parameters.end())
            return fail("no published parameter \"" + key + "\" on that track; list_parameters gives the keys");
        if (!studio_.canAutomate(ref, key))
            return fail("\"" + key + "\" is a setting no lane can move (a voice card's other than its filter, table, level)");
        if (command_ == "clear_automation" || (command_ == "add_automation" && flag("clear", false)))
            studio_.setLanePoints(ref, key, {});
        auto added = 0;
        if (command_ == "add_automation") {
            const auto* points = request_.getProperty("points", juce::var()).getArray();
            if (points == nullptr || points->isEmpty())
                return fail("points must be a non-empty array of {time, value}");
            const auto low = std::min(parameter->minimum, parameter->maximum);
            const auto high = std::max(parameter->minimum, parameter->maximum);
            std::vector<std::pair<std::uint64_t, juce::var>> shaped;
            for (const auto& point : *points) {
                const auto id = studio_.addLanePoint(ref, key, std::max(0.0, numberOf(point, "time", 0.0)),
                                                     static_cast<float>(juce::jlimit(low, high, numberOf(point, "value", parameter->value))));
                if (id != 0) {
                    ++added;
                    if (numberOf(point, "curve", 0.0) != 0.0)
                        (void) studio_.setLanePointCurve(ref, key, id, Studio::AutomationCurve::bezier,
                                                         static_cast<float>(numberOf(point, "curve", 0.0)));
                    if (asksShape(point))
                        shaped.emplace_back(id, point);
                }
            }
            applyShapes(ref, key, shaped);
        }
        if (command_ != "list_automation") {
            studio_.setShownLane(ref, key);
            studio_.setAutomationVisible(ref, true);
            studio_.markDirty();
        }
        juce::Array<juce::var> entries;
        for (const auto& point : studio_.lanePoints(ref, key)) {
            auto entry = object();
            set(entry, "id", idOf(point.id));
            set(entry, "time", point.timeSeconds);
            set(entry, "value", point.valueDb);
            describeShape(entry, point);
            entries.add(entry);
        }
        auto result = object();
        set(result, "key", key);
        set(result, "label", parameter->label);
        if (command_ == "add_automation")
            set(result, "added", added);
        set(result, "points", entries.size());
        set(result, "list", entries);
        return ok(result);
    }
    if (command_ == "clear_automation" || (command_ == "add_automation" && flag("clear", false))) {
        if (pluginTarget)
            engine_.clearPluginAutomation(track->index, pluginTarget->first, pluginTarget->second);
        else
            studio_.setAutomationPoints(ref, {});
    }
    auto added = 0;
    if (command_ == "add_automation") {
        const auto* points = request_.getProperty("points", juce::var()).getArray();
        if (points == nullptr || points->isEmpty())
            return fail("points must be a non-empty array of {time, valueDb}");
        std::vector<std::pair<std::uint64_t, juce::var>> shaped;
        for (const auto& point : *points) {
            const auto time = std::max(0.0, numberOf(point, "time", 0.0));
            std::uint64_t id = 0;
            if (pluginTarget) {
                id = engine_.addPluginAutomationPoint(
                    track->index, pluginTarget->first, pluginTarget->second, pluginParameterName, time,
                    static_cast<float>(juce::jlimit(0.0, 1.0, numberOf(point, "value", 0.0))));
            } else {
                id = studio_.addAutomationPoint(
                    ref, time, static_cast<float>(juce::jlimit(-60.0, 12.0, numberOf(point, "valueDb", 0.0))));
            }
            if (id != 0)
                ++added;
            if (id != 0 && asksShape(point))
                shaped.emplace_back(id, point);
        }
        applyShapes(ref, pluginTarget ? Studio::pluginLaneName(pluginTarget->first, pluginTarget->second) : juce::String(), shaped);
    }
    if (command_ != "list_automation") {
        // The lane shows what was written.
        if (pluginTarget)
            studio_.rememberPluginParameterName(Studio::pluginLaneName(pluginTarget->first, pluginTarget->second),
                                                pluginParameterName);
        studio_.setShownLane(ref, pluginTarget ? Studio::pluginLaneName(pluginTarget->first, pluginTarget->second)
                                               : juce::String());
        studio_.setAutomationVisible(ref, true);
        studio_.markDirty();
    }
    juce::var result;
    if (pluginTarget) {
        juce::Array<juce::var> entries;
        for (const auto& point : engine_.pluginAutomationPoints(track->index, pluginTarget->first, pluginTarget->second)) {
            auto entry = object();
            set(entry, "id", idOf(point.id));
            set(entry, "time", point.timeSeconds);
            set(entry, "value", point.normalisedValue);
            entries.add(entry);
        }
        result = object();
        set(result, "points", entries);
    } else {
        result = describeAutomation(*track);
    }
    if (command_ == "add_automation")
        set(result, "added", added);
    const auto list = result.getProperty("points", juce::var());
    set(result, "points", list.size());
    set(result, "list", list);
    return ok(result);
}

// ---- Launcher, arming, recording, history --------------------------------------------------------

juce::var Commands::onLaunchScene()
{
    // launch_scene {scene}: as the launcher does it (an empty slot stops its
    // track unless its stop was taken off); {query:true} only reads what
    // plays, and where (seconds into each clip). stop_launcher: every clip.
    const auto scene = static_cast<int>(number("scene", 0.0));
    auto launched = false;
    if (command_ == "launch_scene" && has("query")) {
        // Read only.
    } else if (command_ == "launch_scene") {
        if (scene < 0)
            return fail("scene is 0 or more");
        for (const auto& track : studio_.tracks())
            launched = studio_.slotHasClip(track, scene) || launched;
        launched = studio_.masterClip(scene) != nullptr || launched;
        studio_.launchScene(scene);
    } else {
        studio_.stopAllClips();
    }
    auto result = object();
    set(result, "launched", launched);
    set(result, "playing", engine_.isPlaying());
    juce::Array<juce::var> playing;
    for (const auto& track : studio_.tracks()) {
        auto entry = object();
        set(entry, "track", studio_.trackName(track));
        set(entry, "scene", studio_.playingScene(track));
        set(entry, "pending", studio_.pendingScene(track));
        set(entry, "position", track.midi ? engine_.midiLauncherClipPositionSeconds(track.midiId)
                                          : engine_.launcherClipPositionSeconds(track.audioIndex));
        playing.add(entry);
    }
    set(result, "tracks", playing);
    set(result, "masterScene", studio_.playingMasterScene());
    set(result, "masterPendingScene", studio_.pendingMasterScene());
    return ok(result);
}

juce::var Commands::onVoiceLive()
{
    // voice_live {track}: what the track's newest voice plays now (a
    // Prisme's, a synth's of the family): the settings its own modulations
    // move, by key, where they put them; its LFOs and envelopes.
    std::optional<RemoteTrack> track;
    if (auto error = trackOrFail(track); !error.isVoid())
        return error;
    engine::VoiceLive live;
    const auto playing = studio_.voiceLive(track->ref(), live);
    auto result = object();
    set(result, "playing", playing);
    if (!playing)
        return ok(result);
    // The keys: the instrument card's model names its settings.
    std::function<juce::String(int)> keyOf = [](const int index) { return juce::String(index); };
    if (const auto* slot = studio_.graphFor(track->ref())) {
        for (const auto& card : slot->graph().editorNodes()) {
            if (const auto synth = slot->graph().editorSynthValues(card.id); synth && synth->first != engine::synth::Model::none) {
                const auto model = synth->first;
                keyOf = [model](const int index) {
                    return index < engine::synth::parameterCount(model) ? juce::String(engine::synth::parameter(model, index).key)
                                                                         : juce::String(index);
                };
                break;
            }
            if (slot->graph().editorPrismValues(card.id)) {
                keyOf = [](const int index) {
                    return index < engine::prism::parameterCount ? juce::String(engine::prism::parameter(index).key) : juce::String(index);
                };
                break;
            }
        }
    }
    auto moved = object();
    for (std::size_t index = 0; index < live.values.size(); ++index)
        if (std::isfinite(live.values[index]))
            if (auto* object = moved.getDynamicObject())
                object->setProperty(keyOf(static_cast<int>(index)), live.values[index]);
    set(result, "modulated", moved);
    const auto numbers = [](const auto& values) {
        juce::Array<juce::var> list;
        for (const auto value : values)
            list.add(std::isfinite(value) ? juce::var(value) : juce::var());
        return list;
    };
    set(result, "lfos", numbers(live.lfos));
    set(result, "lfoPhases", numbers(live.lfoPhases));
    set(result, "envelopes", numbers(live.envelopes));
    return ok(result);
}

juce::var Commands::onLaunchClip()
{
    std::optional<RemoteTrack> track;
    if (auto error = trackOrFail(track); !error.isVoid())
        return error;
    auto done = false;
    if (command_ == "launch_clip") {
        const auto scene = static_cast<int>(number("scene", 0.0));
        done = studio_.launchSlot(track->ref(), scene);
    } else {
        studio_.stopTrackClips(track->ref());
        done = true;
    }
    auto result = object();
    set(result, command_ == "launch_clip" ? "launched" : "stopped", done);
    set(result, "scene", studio_.playingScene(track->ref()));
    return ok(result);
}

juce::var Commands::onSlotStop()
{
    // slot_stop {track, scene, stops}: an empty slot's stop (a scene launched
    // stops the track there), read, or set with "stops".
    std::optional<RemoteTrack> track;
    if (auto error = trackOrFail(track); !error.isVoid())
        return error;
    const auto scene = static_cast<int>(number("scene", 0.0));
    if (scene < 0 || scene >= Engine::maximumLauncherScenes)
        return fail("scene is 0 to " + juce::String(Engine::maximumLauncherScenes - 1));
    if (has("stops"))
        studio_.setSlotStops(track->ref(), scene, flag("stops", true));
    auto result = object();
    set(result, "stops", studio_.slotStops(track->ref(), scene));
    set(result, "hasClip", studio_.slotHasClip(track->ref(), scene));
    return ok(result);
}

juce::var Commands::onMasterClip()
{
    // master_clip {scene, action}: the master's launcher clip of a scene,
    // its automation. action "set" (the default when anything is given to
    // set) makes it or changes it: name, lengthBeats, looping, and a lane's
    // points {lane, points:[[beats, value]...]} (none: the lane removed;
    // lane "master.gain" is its level in dB). "remove", "launch", "stop";
    // "lanes" lists what a lane can automate. Without any: read.
    const auto scene = static_cast<int>(number("scene", 0.0));
    if (scene < 0 || scene >= Engine::maximumLauncherScenes)
        return fail("scene is 0 to " + juce::String(Engine::maximumLauncherScenes - 1));
    auto action = text("action");
    if (action.isEmpty())
        action = has("name") || has("lengthBeats") || has("looping") || has("lane") ? "set" : "read";
    if (action == "set") {
        auto clip = studio_.masterClip(scene) != nullptr ? *studio_.masterClip(scene) : studio_.createMasterClip(scene);
        if (has("name"))
            clip.name = text("name");
        if (has("lengthBeats"))
            clip.lengthBeats = number("lengthBeats", clip.lengthBeats);
        if (has("looping"))
            clip.looping = flag("looping", clip.looping);
        if (has("lane")) {
            const auto lane = text("lane");
            const auto choices = studio_.masterLaneChoices();
            if (std::none_of(choices.begin(), choices.end(), [&lane](const auto& choice) { return choice.binding == lane; }))
                return fail("no such lane (action lanes lists them): " + lane);
            std::vector<MasterClip::Point> points;
            if (const auto* list = request_.getProperty("points", juce::var()).getArray())
                for (const auto& entry : *list) {
                    if (const auto* pair = entry.getArray(); pair != nullptr && pair->size() >= 2)
                        points.push_back({ static_cast<double>((*pair)[0]), static_cast<float>(static_cast<double>((*pair)[1])) });
                    else if (entry.isObject())
                        points.push_back({ numberOf(entry, "beats", 0.0), static_cast<float>(numberOf(entry, "value", 0.0)) });
                }
            if (points.empty())
                clip.lanes.erase(lane);
            else
                clip.lanes[lane] = std::move(points);
        }
        studio_.setMasterClip(scene, std::move(clip));
    } else if (action == "remove") {
        studio_.removeMasterClip(scene);
    } else if (action == "launch") {
        if (!studio_.launchMasterClip(scene))
            return fail("scene " + juce::String(scene) + " has no master clip");
    } else if (action == "stop") {
        studio_.stopMasterClip();
    } else if (action != "read" && action != "lanes") {
        return fail("action is set, remove, launch, stop, lanes or read");
    }
    auto result = object();
    set(result, "scene", scene);
    if (const auto* clip = studio_.masterClip(scene)) {
        set(result, "exists", true);
        set(result, "name", clip->name);
        set(result, "lengthBeats", clip->lengthBeats);
        set(result, "looping", clip->looping);
        juce::Array<juce::var> lanes;
        for (const auto& [binding, points] : clip->lanes) {
            auto lane = object();
            set(lane, "lane", binding);
            juce::Array<juce::var> list;
            for (const auto& point : points) {
                juce::Array<juce::var> pair;
                pair.add(point.beats);
                pair.add(static_cast<double>(point.value));
                list.add(pair);
            }
            set(lane, "points", list);
            lanes.add(lane);
        }
        set(result, "lanes", lanes);
    } else {
        set(result, "exists", false);
    }
    set(result, "playingScene", studio_.playingMasterScene());
    set(result, "pendingScene", studio_.pendingMasterScene());
    set(result, "progress", static_cast<double>(studio_.masterClipProgress()));
    set(result, "masterGainDb", static_cast<double>(engine_.masterGainDb()));
    set(result, "masterClockSeconds", engine_.masterClockSeconds());
    if (action == "lanes") {
        juce::Array<juce::var> choices;
        for (const auto& choice : studio_.masterLaneChoices()) {
            auto entry = object();
            set(entry, "lane", choice.binding);
            set(entry, "label", choice.label);
            set(entry, "unit", choice.unit);
            set(entry, "minimum", choice.minimum);
            set(entry, "maximum", choice.maximum);
            set(entry, "value", choice.value);
            choices.add(entry);
        }
        set(result, "choices", choices);
    }
    return ok(result);
}

juce::var Commands::onSetLaunchQuantization()
{
    using Quantization = Engine::LaunchQuantization;
    const auto value = text("value");
    const auto quantization = value == "none"      ? std::optional<Quantization>(Quantization::none)
                              : value == "quarter" ? std::optional<Quantization>(Quantization::quarterBeat)
                              : value == "half"    ? std::optional<Quantization>(Quantization::halfBeat)
                              : value == "beat"    ? std::optional<Quantization>(Quantization::oneBeat)
                              : value == "bar"     ? std::optional<Quantization>(Quantization::oneBar)
                                                   : std::optional<Quantization> {};
    if (!quantization)
        return fail("value is none, quarter, half, beat or bar");
    engine_.setLaunchQuantization(*quantization);
    studio_.markDirty();
    auto result = object();
    set(result, "launchQuantization", static_cast<int>(engine_.launchQuantization()));
    return ok(result);
}

juce::var Commands::onArm()
{
    std::optional<RemoteTrack> track;
    if (auto error = trackOrFail(track); !error.isVoid())
        return error;
    const auto armed = flag("armed", true);
    if (track->midi) {
        if (engine_.setMidiTrackArmed(track->id, armed))
            studio_.markDirty();
    } else if (!engine_.isRecording()) {
        // Arming is frozen while recording.
        engine_.setTrackArmed(track->index, armed);
        studio_.markDirty();
    }
    auto result = object();
    set(result, "armed", studio_.trackMix(track->ref()).armed);
    return ok(result);
}

juce::var Commands::onRecord()
{
    const auto mode = text("mode");
    const auto action = text("action");
    if (engine_.isMasterExporting())
        return fail("an export is running");
    if (mode != "midi" && mode != "audio")
        return fail("mode is midi or audio");
    const auto midi = mode == "midi";
    if (action == "start" && !studio_.isRecording()) {
        // The studio records the armed track first: refuse what would record
        // the other kind.
        auto midiArmed = false;
        for (const auto& summary : engine_.midiTrackSummaries())
            midiArmed = midiArmed || summary.armed;
        const auto audioArmed = engine_.armedAudioTrack().has_value();
        const auto selected = ui_.selectedTrack();
        std::optional<TrackRef> recorded;
        std::uint64_t clip {};
        if (midi) {
            if (audioArmed && !midiArmed)
                return fail("an audio track is armed; disarm it to record MIDI");
            if (!midiArmed && (!selected || !selected->midi))
                return fail("select a MIDI track first (list_parameters or add_notes do)");
            if (selected && selected->midi) {
                recorded = selected;
                // Into the track's first clip, as CyDAW 1's piano roll opens it.
                if (const auto* clips = engine_.midiClipsView(selected->midiId); clips != nullptr && !clips->empty())
                    clip = clips->front().id;
            }
            ui_.showView(2);
        } else {
            // The take goes where the studio keeps them (the project's
            // recordings folder); "file" is not used.
            if (midiArmed)
                return fail("a MIDI track is armed; disarm it to record audio");
            if (!audioArmed && (!selected || selected->midi))
                return fail("arm or select an audio track to record");
            if (selected && !selected->midi)
                recorded = selected;
        }
        if (const auto started = studio_.startRecording(recorded, clip); started.failed())
            return fail(started.getErrorMessage());
    } else if (action == "stop" && studio_.isRecording()) {
        const auto recording = studio_.recordingTrack();
        if (recording && recording->midi == midi)
            if (const auto stopped = studio_.stopRecording(true); stopped.failed())
                return fail(stopped.getErrorMessage());
    }
    auto result = object();
    set(result, "midiRecording", engine_.isMidiRecording());
    set(result, "audioRecording", engine_.isRecording());
    set(result, "recordingSession", engine_.hasRecordingSession());
    return ok(result);
}

juce::var Commands::onUndoRedo()
{
    const auto scope = text("scope");
    const auto undo = command_ == "undo";
    auto done = true;
    if (scope == "midi") {
        if (undo ? engine_.undoMidiEdit() : engine_.redoMidiEdit())
            studio_.markDirty();
    } else if (scope == "graph") {
        // The selected track's graph, as the editor shows it.
        if (const auto selected = ui_.selectedTrack())
            if (auto* slot = studio_.graphFor(*selected)) {
                const auto changed = undo ? slot->graph().editorUndo() : slot->graph().editorRedo();
                if (changed) {
                    (void) studio_.publishGraph(*selected, true);
                    studio_.markDirty();
                }
            }
    } else if (scope == "arrangement") {
        done = undo ? engine_.undoArrangementEdit() : engine_.redoArrangementEdit();
        studio_.markDirty();
    } else {
        return fail("scope is midi, graph or arrangement");
    }
    auto result = object();
    set(result, "done", done);
    return ok(result);
}

// ---- Plugins ------------------------------------------------------------------------------------

juce::var Commands::onListPlugins()
{
    auto folders=plugin_files::folders();
    if(has("folder"))folders.add(fileFor(text("folder")));
    juce::Array<juce::var> entries;
    juce::StringArray seen;
    for (const auto& folder : folders) {
        if (!folder.isDirectory())
            continue;
        juce::Array<juce::File> found;
        folder.findChildFiles(found, juce::File::findFilesAndDirectories, true, "*");
        for (const auto& plugin : found) {
            if (!plugin_files::candidate(plugin) || seen.contains(plugin.getFullPathName()))
                continue;
            seen.add(plugin.getFullPathName());
            auto entry = object();
            set(entry, "name", plugin.getFileNameWithoutExtension());
            set(entry, "path", plugin.getFullPathName());
            set(entry, "format", plugin_files::format(plugin));
            entries.add(entry);
        }
    }
    auto result = object();
    set(result, "plugins", entries);
    return ok(result);
}

juce::var Commands::onPlugins()
{
    std::optional<RemoteTrack> track;
    if (auto error = trackOrFail(track, true); !error.isVoid())
        return error;
    if (track->midi || track->bus || track->master)
        return fail("plugins sit on audio tracks in this version");
    const auto index = track->index;
    if (command_ == "add_plugin") {
        const auto file = fileFor(text("file"));
        if (!file.exists())
            return fail("no plugin at " + file.getFullPathName());
        std::size_t pluginIndex {};
        if (const auto added = engine_.setTrackPlugin(index, file, &pluginIndex); added.failed())
            return fail(added.getErrorMessage());
        studio_.markDirty();
        auto result = object();
        set(result, "pluginIndex", static_cast<int>(pluginIndex));
        set(result, "tracks", describeTracks());
        return ok(result);
    }
    const auto info = engine_.trackPlugins(index);
    const auto pluginIndex = static_cast<std::size_t>(number("plugin", 0.0));
    if (pluginIndex >= info.size())
        return fail("no plugin " + juce::String(static_cast<int>(pluginIndex)) + " on that track; status lists them");
    if(command_=="list_plugin_programs"||command_=="select_plugin_program") {
        if(command_=="select_plugin_program") {auto applied=engine_.setTrackPluginProgram(index,pluginIndex,int(number("program",0)));if(applied.failed())return fail(applied.getErrorMessage());studio_.markDirty();}
        juce::StringArray names;int current{};auto listed=engine_.getTrackPluginPrograms(index,pluginIndex,names,current);if(listed.failed())return fail(listed.getErrorMessage());
        auto result=object();juce::Array<juce::var> programs;for(const auto& name:names)programs.add(name);set(result,"programs",programs);set(result,"current",current);return ok(result);
    }
    if (command_ == "remove_plugin") {
        studio_.removePlugin(track->ref(), pluginIndex);
        return ok(describeTracks());
    }
    if (command_ == "move_plugin") {
        const auto direction = number("direction", 1.0) < 0.0 ? -1 : 1;
        if (!studio_.movePlugin(track->ref(), pluginIndex, direction))
            return fail("the plugin did not move");
        return ok(describeTracks());
    }
    if (command_ == "show_plugin_editor") {
        if (const auto shown = engine_.showTrackPluginEditor(index, pluginIndex); shown.failed())
            return fail(shown.getErrorMessage());
        auto result = object();
        set(result, "shown", true);
        return ok(result);
    }
    std::vector<app::IsolatedPluginHost::ParameterSnapshot> pluginParameters;
    if (const auto refreshed = engine_.refreshTrackPluginParameters(index, pluginIndex, pluginParameters);
        refreshed.failed())
        return fail(refreshed.getErrorMessage());
    if (command_ == "set_plugin_parameter") {
        const auto spec = request_.getProperty("parameter", juce::var());
        std::optional<std::uint32_t> parameterIndex;
        if (spec.isInt() || spec.isInt64() || spec.isDouble())
            parameterIndex = static_cast<std::uint32_t>(static_cast<int>(spec));
        else
            for (const auto& parameter : pluginParameters)
                if (parameter.name.equalsIgnoreCase(spec.toString().trim()))
                    parameterIndex = parameter.index;
        if (!parameterIndex)
            return fail("no parameter \"" + spec.toString() + "\"; list_plugin_parameters gives them");
        const auto value = static_cast<float>(juce::jlimit(0.0, 1.0, number("value", 0.0)));
        if (const auto applied = engine_.setTrackPluginParameter(index, pluginIndex, *parameterIndex, value);
            applied.failed())
            return fail(applied.getErrorMessage());
        studio_.markDirty();
        auto result = object();
        set(result, "parameter", static_cast<int>(*parameterIndex));
        set(result, "value", value);
        return ok(result);
    }
    // list_plugin_parameters {pin: [indices]}: the knobs its card shows set first.
    if (const auto* pin = request_.getProperty("pin", {}).getArray()) {
        std::vector<std::uint32_t> pins;
        for (const auto& value : *pin)
            pins.push_back(static_cast<std::uint32_t>(static_cast<int>(value)));
        studio_.setPluginPins(track->ref(), pluginIndex, std::move(pins));
    }
    juce::Array<juce::var> entries;
    for (const auto& parameter : pluginParameters) {
        auto entry = object();
        set(entry, "index", static_cast<int>(parameter.index));
        set(entry, "name", parameter.name);
        set(entry, "label", parameter.label);
        set(entry, "value", parameter.value);
        set(entry, "text", parameter.text);
        set(entry, "automatable", parameter.automatable);
        set(entry, "discrete", parameter.discrete);
        set(entry, "boolean", parameter.boolean);
        entries.add(entry);
    }
    auto result = object();
    set(result, "plugin", info[pluginIndex].name);
    set(result, "parameters", entries);
    if (const auto pins = studio_.pluginPins(track->ref(), pluginIndex)) {
        juce::Array<juce::var> pinned;
        for (const auto value : *pins)
            pinned.add(static_cast<int>(value));
        set(result, "pinned", pinned);
    }
    return ok(result);
}

// ---- Buses and the audio device -----------------------------------------------------------------

juce::var Commands::onListBuses()
{
    auto result = object();
    set(result, "buses", describeBuses());
    set(result, "defaultInputBus", idOf(engine_.defaultInputBusId()));
    return ok(result);
}

juce::var Commands::onBuses()
{
    const auto find = [this](const juce::String& spec) -> std::optional<app::VirtualAudioBus> {
        for (const auto& bus : engine_.virtualBuses())
            if (bus.name.equalsIgnoreCase(spec.trim()) || juce::String(static_cast<juce::int64>(bus.id)) == spec.trim())
                return bus;
        return std::nullopt;
    };
    std::optional<app::VirtualAudioBus> bus;
    if (command_ == "add_bus") {
        const auto direction = text("direction") == "input" ? app::VirtualBusDirection::input
                                                            : app::VirtualBusDirection::output;
        const auto id = studio_.addHardwareBus(direction);
        if (id == 0)
            return fail("no more buses can be added");
        bus = engine_.virtualBus(id);
        if (!bus)
            return fail("no more buses can be added");
    } else {
        bus = find(text("bus"));
        if (!bus)
            return fail("no bus \"" + text("bus") + "\"; status lists them");
    }
    if (command_ == "remove_bus") {
        // The tracks sent to it go back to their other outputs (the master).
        if (const auto removed = studio_.removeHardwareBus(bus->id); removed.failed())
            return fail(removed.getErrorMessage());
    } else {
        auto updated = *bus;
        if (has("name") && text("name").trim().isNotEmpty())
            updated.name = text("name").trim();
        if (has("channels"))
            updated.channels = juce::jlimit(1, app::VirtualAudioBus::maximumChannels, static_cast<int>(number("channels", 2.0)));
        if (has("format"))
            if (const auto channels = cydaw::engine::layoutChannels(text("format").trim().toLowerCase().toStdString()); channels > 0)
                updated.channels = channels;
        // The sound card's channels, one per bus channel (from 0).
        if (const auto* list = request_.getProperty("deviceChannels", juce::var()).getArray()) {
            if (!has("channels") && !has("format"))
                updated.channels = juce::jlimit(1, app::VirtualAudioBus::maximumChannels, list->size());
            for (int channel = 0; channel < std::min(list->size(), app::VirtualAudioBus::maximumChannels); ++channel)
                updated.setDeviceChannel(channel, std::max(0, static_cast<int>((*list)[channel])));
        }
        if (has("carriesMaster"))
            updated.carriesMaster = flag("carriesMaster", true);
        if (has("physicalA"))
            updated.physicalChannelA = std::max(0, static_cast<int>(number("physicalA", 0.0)));
        if (has("physicalB"))
            updated.physicalChannelB = std::max(0, static_cast<int>(number("physicalB", 1.0)));
        if (has("destinationA"))
            updated.destinationChannelA = std::max(0, static_cast<int>(number("destinationA", 0.0)));
        if (has("destinationB"))
            updated.destinationChannelB = std::max(0, static_cast<int>(number("destinationB", 1.0)));
        if (has("gainDb"))
            updated.gainDb = static_cast<float>(juce::jlimit(-60.0, 12.0, number("gainDb", 0.0)));
        if (has("muted"))
            updated.muted = flag("muted", false);
        if (has("monitor"))
            updated.monitor = flag("monitor", false);
        if (has("exposeToSystem"))
            updated.exposeToSystem = flag("exposeToSystem", false);
        if (const auto applied = studio_.updateHardwareBus(updated); applied.failed())
            return fail(applied.getErrorMessage());
    }
    studio_.markDirty();
    auto result = object();
    set(result, "buses", describeBuses());
    if (command_ == "add_bus")
        set(result, "busId", idOf(bus->id));
    return ok(result);
}

juce::var Commands::onAudioSettings()
{
    auto& manager = engine_.deviceManager();
    if (command_ == "device_channels" && (has("inputs") || has("outputs"))) {
        // {inputs, outputs}: the first so many of the device's channels open.
        const auto current = engine_.deviceChannels();
        const auto inputs = has("inputs") ? static_cast<int>(number("inputs", 2.0)) : current.activeInputs;
        const auto outputs = has("outputs") ? static_cast<int>(number("outputs", 2.0)) : current.activeOutputs;
        if (const auto applied = studio_.setDeviceChannelCounts(inputs, outputs); applied.failed())
            return fail(applied.getErrorMessage());
    }
    if (command_ == "set_audio_device") {
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
            setup.sampleRate = number("sampleRate", setup.sampleRate);
        if (has("bufferSize"))
            setup.bufferSize = static_cast<int>(number("bufferSize", setup.bufferSize));
        if (const auto error = manager.setAudioDeviceSetup(setup, true); error.isNotEmpty())
            return fail(error);
        studio_.saveAudioSetup();
    }
    auto result = object();
    const auto setup = manager.getAudioDeviceSetup();
    const auto channels = engine_.deviceChannels();
    set(result, "inputChannels", strings(channels.inputNames));
    set(result, "outputChannels", strings(channels.outputNames));
    set(result, "availableInputs", channels.availableInputs);
    set(result, "availableOutputs", channels.availableOutputs);
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
        set(result, "latencyMs", (device->getOutputLatencyInSamples() + device->getInputLatencyInSamples())
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

// ---- The sample library ---------------------------------------------------------------------------

juce::var Commands::onSamples()
{
    // An automated session's library starts when it is asked about.
    studio_.ensureSamples();
    auto& library = studio_.samples();
    auto& semantic = studio_.semantic();
    auto& models = studio_.aiModels();
    const auto describe = [&library](const app::SampleRecord& record) {
        auto entry = object();
        set(entry, "path", record.path);
        set(entry, "name", record.name);
        set(entry, "folder", record.root);
        set(entry, "source", record.source);
        set(entry, "sourceTitle", library.sourceTitle(record));
        set(entry, "instrument", record.instrument);
        set(entry, "family", record.category.isNotEmpty() ? app::SampleLibrary::familyOf(record.category) : juce::String("other"));
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
    const auto statusOf = [&library] {
        const auto progress = library.progress();
        auto result = object();
        set(result, "samples", static_cast<int>(library.samples().size()));
        set(result, "scanning", progress.scanning);
        set(result, "classifying", progress.classifying);
        set(result, "found", progress.found);
        set(result, "analysed", progress.analysed);
        set(result, "toAnalyse", progress.toAnalyse);
        set(result, "classified", progress.classified);
        set(result, "toClassify", progress.toClassify);
        set(result, "message", progress.message);
        juce::StringArray folders;
        for (const auto& folder : library.folders())
            folders.add(folder.getFullPathName());
        set(result, "folders", strings(folders));
        set(result, "cacheFolder", library.cacheFolder().getFullPathName());
        set(result, "autoRescanMinutes", library.autoRescanMinutes());
        set(result, "classifierEnabled", library.classifierEnabled());
        set(result, "categories", strings(library.distinctCategories()));
        set(result, "types", strings(library.distinctTypes()));
        set(result, "tags", strings(library.distinctTags()));
        set(result, "loaded", library.loaded());
        set(result, "includeBanks", library.includeBanks());
        juce::StringArray banks;
        for (const auto& bank : library.banks())
            banks.add(bank.id);
        set(result, "banks", strings(banks));
        // How many in each category, family and source.
        std::map<juce::String, int> categories, families, sources;
        for (const auto& record : library.samples()) {
            ++categories[record.category.isNotEmpty() ? record.category : juce::String("-")];
            ++families[record.category.isNotEmpty() ? app::SampleLibrary::familyOf(record.category) : juce::String("other")];
            ++sources[record.source.isNotEmpty() ? record.source : juce::String("folders")];
        }
        const auto counts = [](const std::map<juce::String, int>& map) {
            auto value = object();
            for (const auto& [key, count] : map)
                set(value, key.toRawUTF8(), count);
            return value;
        };
        set(result, "categoryCounts", counts(categories));
        // What the catalogue says of each bank as a whole.
        auto bankHints = object();
        if (const auto hints = library.hints())
            for (const auto& [key, hint] : *hints)
                if (key.startsWith("bank:")) {
                    auto entry = object();
                    set(entry, "category", hint.category);
                    set(entry, "instrument", hint.instrument);
                    set(bankHints, key.fromFirstOccurrenceOf("bank:", false, false).toRawUTF8(), entry);
                }
        set(result, "bankHints", bankHints);
        set(result, "familyCounts", counts(families));
        set(result, "sourceCounts", counts(sources));
        return result;
    };
    const auto semanticOf = [&semantic, &models] {
        auto result = object();
        const auto status = semantic.status();
        set(result, "model", semantic.activeModel());
        set(result, "available", semantic.available());
        set(result, "running", status.running);
        set(result, "loadingModel", status.loadingModel);
        set(result, "workerUp", status.workerUp);
        set(result, "device", status.device);
        set(result, "indexed", status.indexed);
        set(result, "total", status.total);
        set(result, "done", status.done);
        set(result, "pending", status.pending);
        set(result, "failed", status.failed);
        set(result, "classified", status.classified);
        set(result, "message", status.message);
        set(result, "error", status.error);
        set(result, "autoIndex", semantic.autoIndex());
        set(result, "autoCategories", semantic.autoCategories());
        const auto engine = semantic.engine();
        auto engineValue = object();
        set(engineValue, "checking", engine.checking);
        set(engineValue, "checked", engine.checked);
        set(engineValue, "ready", engine.ready);
        set(engineValue, "python", engine.python);
        set(engineValue, "executable", engine.executable);
        set(engineValue, "torch", engine.torch);
        set(engineValue, "transformers", engine.transformers);
        set(engineValue, "cuda", engine.cuda);
        set(engineValue, "gpu", engine.gpu);
        set(engineValue, "error", engine.error);
        set(result, "engine", engineValue);
        juce::ignoreUnused(models);
        return result;
    };
    const auto modelsOf = [&models, &semantic] {
        auto result = object();
        juce::Array<juce::var> list;
        for (const auto& model : models.models()) {
            auto entry = object();
            set(entry, "id", model.id);
            set(entry, "version", model.version);
            set(entry, "title", model.title);
            set(entry, "provider", model.provider);
            set(entry, "default", model.isDefault);
            set(entry, "validated", model.validated);
            set(entry, "description", model.describe());
            set(entry, "licence", model.licence);
            set(entry, "licenceUrl", model.licenceUrl);
            set(entry, "licencePage", model.licencePage);
            set(entry, "licenceNote", model.note());
            set(entry, "requiresAcceptance", model.requiresAcceptance);
            set(entry, "source", model.host);
            set(entry, "revision", model.revision);
            set(entry, "bytes", model.bytes());
            set(entry, "files", static_cast<int>(model.files.size()));
            set(entry, "installed", models.installed(model.id));
            set(entry, "active", semantic.activeModel() == model.id);
            set(entry, "folder", models.folderOf(model).getFullPathName());
            set(entry, "receipt", models.receiptOf(model.id));
            list.add(entry);
        }
        set(result, "models", list);
        set(result, "catalogue", models.catalogueFile().getFullPathName());
        juce::Array<juce::var> packages;
        for (const auto& package : models.enginePackages()) {
            auto entry = object();
            set(entry, "name", package.name);
            set(entry, "licence", package.licence);
            set(entry, "url", package.url);
            packages.add(entry);
        }
        set(result, "enginePackages", packages);
        const auto progress = models.progress();
        auto install = object();
        set(install, "running", progress.running);
        set(install, "id", progress.id);
        set(install, "step", progress.step);
        set(install, "file", progress.file);
        set(install, "done", progress.done);
        set(install, "total", progress.total);
        set(install, "error", progress.error);
        set(result, "install", install);
        return result;
    };
    if (command_ == "ai_models") {
        // {install: id, acceptLicence}: downloaded from its publisher. The
        // licence is the person's to accept: an agent passes acceptLicence only
        // when the person said so, having read it.
        if (has("install")) {
            juce::String error;
            if (!models.install(text("install"), flag("acceptLicence", false), error))
                return fail(error);
        }
        if (has("import")) {
            juce::String error;
            if (!models.importFrom(text("import"), fileFor(text("folder")), flag("acceptLicence", false), error))
                return fail(error);
        }
        if (flag("cancel", false))
            models.cancel();
        if (has("remove")) {
            juce::String error;
            if (!models.remove(text("remove"), error))
                return fail(error);
            semantic.modelsChanged();
        }
        if (has("use"))
            semantic.setActiveModel(text("use"));
        if (flag("checkEngine", false))
            semantic.checkEngine();
        auto result = modelsOf();
        set(result, "semantic", semanticOf());
        return ok(result);
    }
    if (command_ == "semantic_index") {
        if (has("autoIndex"))
            semantic.setAutoIndex(flag("autoIndex", true));
        if (has("autoCategories"))
            semantic.setAutoCategories(flag("autoCategories", true));
        if (flag("stop", false))
            semantic.stopIndexing();
        if (flag("start", false)) {
            if (semantic.activeModel().isEmpty())
                return fail("no model installed (ai_models)");
            semantic.startIndexing();
        }
        if (flag("classify", false))
            semantic.classifyNow();
        return ok(semanticOf());
    }
    if (command_ == "sample_folders") {
        if (has("includeBanks"))
            library.setIncludeBanks(flag("includeBanks", true));
        if (has("add")) {
            const auto folder = fileFor(text("add"));
            if (!folder.isDirectory())
                return fail("no folder at " + folder.getFullPathName());
            library.addFolder(folder);
        }
        if (has("remove"))
            library.removeFolder(fileFor(text("remove")));
        if (has("autoRescanMinutes"))
            library.setAutoRescanMinutes(static_cast<int>(number("autoRescanMinutes", 10.0)));
        return ok(statusOf());
    }
    if (command_ == "sample_cache") {
        if (has("folder")) {
            const auto folder = fileFor(text("folder"));
            (void) folder.createDirectory();
            if (!folder.isDirectory())
                return fail("no folder at " + folder.getFullPathName());
            library.setCacheFolder(folder);
        }
        library.saveNow();
        return ok(statusOf());
    }
    if (command_ == "scan_samples") {
        library.rescan(flag("full", false));
        return ok(statusOf());
    }
    if (command_ == "classify_samples" && !semantic.activeModel().isEmpty()) {
        // With a model installed, the semantic search's CLAP names them.
        semantic.classifyNow();
        auto result = statusOf();
        set(result, "semantic", semanticOf());
        return ok(result);
    }
    if (command_ == "classify_samples") {
        if (has("enabled"))
            library.setClassifierEnabled(flag("enabled", true));
        if (has("python"))
            library.setPythonCommand(text("python"));
        library.classify(!flag("all", false));
        return ok(statusOf());
    }
    if (command_ == "set_sample") {
        const auto path = fileFor(text("path")).getFullPathName();
        if (library.find(path) == nullptr)
            return fail("no sample " + path + " in the library");
        if (has("favourite"))
            library.setFavourite(path, flag("favourite", false));
        if (has("rating"))
            library.setRating(path, static_cast<int>(number("rating", 0.0)));
        if (has("category"))
            library.setCategory(path, text("category"));
        if (has("type"))
            library.setType(path, text("type"));
        if (has("bpm"))
            library.setBpm(path, number("bpm", 0.0));
        if (has("tags")) {
            juce::StringArray tags;
            if (const auto* list = request_.getProperty("tags", juce::var()).getArray())
                for (const auto& tag : *list)
                    tags.add(tag.toString());
            else
                tags.addTokens(text("tags"), ",;", "");
            library.setTags(path, tags);
        }
        return ok(describe(*library.find(path)));
    }
    if (command_ == "library_window" || command_ == "library_perf") {
        const auto answer = ui_.library(command_, request_);
        if (!answer.isObject())
            return fail("no interface");
        return ok(answer);
    }
    if (command_ == "browse_samples") {
        // The browser showing a search: {query, semantic, family, category, source}.
        if (flag("semantic", false) && semantic.activeModel().isEmpty())
            return fail("no semantic search model installed (ai_models)");
        ui_.browseSamples(text("query"), flag("semantic", false), text("family"), text("category"), text("source"));
        return ok(statusOf());
    }
    if (command_ == "preview_sample" && flag("query", false)) {
        // What is heard: the loop's place (0..1 of it) and the transport's,
        // of the same block.
        auto result = object();
        set(result, "previewing", engine_.isSamplePreviewing());
        set(result, "position", engine_.samplePreviewPositionSeconds());
        set(result, "loopBeats", engine_.samplePreviewLoopBeats());
        set(result, "transportPosition", engine_.position());
        set(result, "transportPlaying", engine_.isPlaying());
        set(result, "latencySeconds", static_cast<double>(engine_.mixLatencySamples()) / std::max(1.0, engine_.auditionSampleRate()));
        set(result, "tempo", engine_.tempo());
        return ok(result);
    }
    if (command_ == "select_sample" || command_ == "preview_sample") {
        const auto path = fileFor(text("path")).getFullPathName();
        const auto* record = library.find(path);
        if (command_ == "select_sample") {
            if (record == nullptr)
                return fail("no sample " + path + " in the library");
            ui_.selectSample(juce::File(path));
            return ok(describe(*record));
        }
        if (flag("stop", false)) {
            engine_.stopSamplePreview();
        } else {
            if (!juce::File(path).existsAsFile())
                return fail("no file at " + path);
            if (has("gainDb"))
                engine_.setSamplePreviewGainDb(static_cast<float>(juce::jlimit(-60.0, 6.0, number("gainDb", -6.0))));
            // atTempo: a loop with a tempo at the project's (as the library's switch).
            if (const auto started = studio_.auditionSample(path, std::max(0.0, number("start", 0.0)), flag("atTempo", false));
                started.failed())
                return fail(started.getErrorMessage());
        }
        auto result = object();
        set(result, "previewing", engine_.isSamplePreviewing());
        set(result, "position", engine_.samplePreviewPositionSeconds());
        set(result, "loopBeats", engine_.samplePreviewLoopBeats());
        set(result, "fromBpm", studio_.auditionTempoOf(path));
        set(result, "tempo", engine_.tempo());
        return ok(result);
    }
    if (command_ == "search_samples" && (has("semantic") || has("similar"))) {
        // By description or likeness: the model's answer, best first, through
        // the other filters.
        if (semantic.activeModel().isEmpty())
            return fail("no semantic search model installed (ai_models)");
        const auto limit = static_cast<int>(number("limit", 50.0));
        auto answer = has("similar")
                          ? semantic.similarNow(fileFor(text("similar")).getFullPathName(), std::max(limit * 4, 200), 120'000)
                          : semantic.searchNow(text("semantic"), std::max(limit * 4, 200), 120'000);
        if (answer.failed)
            return fail(answer.error);
        SemanticSearch::rerank(answer, library, std::max(limit * 4, 200));
        const auto category = text("category").trim().toLowerCase();
        const auto family = text("family").trim().toLowerCase();
        const auto source = text("source").trim();
        juce::Array<juce::var> entries;
        for (const auto& hit : answer.hits) {
            const auto* record = library.find(hit.path);
            if (record == nullptr)
                continue;
            if (category.isNotEmpty() && record->category != category)
                continue;
            if (family.isNotEmpty() && app::SampleLibrary::familyOf(record->category) != family)
                continue;
            if (source.isNotEmpty() && record->source != source)
                continue;
            auto entry = describe(*record);
            set(entry, "similarity", hit.similarity);
            set(entry, "score", hit.score);
            entries.add(entry);
            if (entries.size() >= limit)
                break;
        }
        auto result = object();
        set(result, "english", answer.english);
        set(result, "matched", entries.size());
        set(result, "samples", entries);
        return ok(result);
    }
    if (command_ == "search_samples") {
        const auto query = text("query").trim().toLowerCase();
        const auto category = text("category").trim().toLowerCase();
        const auto family = text("family").trim().toLowerCase();
        const auto source = text("source").trim();
        const auto instrument = text("instrument").trim().toLowerCase();
        const auto type = text("type").trim().toLowerCase();
        const auto tag = text("tag").trim().toLowerCase();
        const auto folder = text("folder").trim();
        const auto onlyFavourites = flag("favourites", false);
        const auto minLength = number("minLength", 0.0);
        const auto maxLength = number("maxLength", 1.0e9);
        const auto limit = static_cast<int>(number("limit", 100.0));
        juce::StringArray words;
        words.addTokens(query, " ", "");
        juce::Array<juce::var> entries;
        auto matched = 0;
        for (const auto& record : library.samples()) {
            if (category.isNotEmpty() && record.category != category)
                continue;
            if (family.isNotEmpty() && app::SampleLibrary::familyOf(record.category) != family)
                continue;
            if (source.isNotEmpty() && record.source != source)
                continue;
            if (instrument.isNotEmpty() && !record.instrument.toLowerCase().contains(instrument))
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
            const auto haystack = (record.name + " " + record.category + " " + record.tags.joinIntoString(" ") + " " + record.key
                                   + " " + record.instrument)
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

} // namespace

juce::var handleRemoteCommand(Studio& studio, RemoteUi& ui, const juce::var& request)
{
    return Commands(studio, ui, request).run();
}

} // namespace cydaw::v2
