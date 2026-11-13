#include <cydaw/base/Utilities.h>
#include <cydaw/base/LegacyPluginState.h>
#include <cydaw/base/File.h>
#include "Studio.h"

#include "JsonValue.h"
#include "ProjectRecovery.h"
#include "Widgets.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <vector>

// The .cydaw documents of CyDAW 1 (WorkspaceComponent::createProjectDocument
// and loadProject) without the interface: what v1 kept in its views is kept
// in the Studio, and what it showed in dialogs becomes a load warning.
// Everything here runs on the message thread; a document built here shares
// objects with the session and must be cloned before another thread uses it.

namespace cydaw::v2::project_io {
namespace {

using Engine = app::AudioTrackController;

constexpr int documentVersion = 36;
static_assert(app::ProjectRecovery::maximumProjectVersion >= documentVersion);

namespace keys {
const cydaw::base::Identifier activeTake("activeTake");
const cydaw::base::Identifier amount("amount");
const cydaw::base::Identifier armed("armed");
const cydaw::base::Identifier arrangementPlayback("arrangementPlayback");
const cydaw::base::Identifier arrangementSnapMode("arrangementSnapMode");
const cydaw::base::Identifier arrangementSnapType("arrangementSnapType");
const cydaw::base::Identifier arrangementTool("arrangementTool");
const cydaw::base::Identifier audioChannels("audioChannels");
const cydaw::base::Identifier audioClips("audioClips");
const cydaw::base::Identifier audioFile("audioFile");
const cydaw::base::Identifier automation("automation");
const cydaw::base::Identifier automationVisible("automationVisible");
const cydaw::base::Identifier parameterAutomation("parameterAutomation");
const cydaw::base::Identifier shownAutomation("shownAutomation");
const cydaw::base::Identifier base("base");
const cydaw::base::Identifier binding("binding");
const cydaw::base::Identifier channels("channels");
const cydaw::base::Identifier clipDuration("clipDuration");
const cydaw::base::Identifier clipStart("clipStart");
const cydaw::base::Identifier clips("clips");
const cydaw::base::Identifier colour("colour");
const cydaw::base::Identifier connections("connections");
const cydaw::base::Identifier curve("curve");
// A Bezier point's handles: [inTime, inValue, outTime, outValue].
const cydaw::base::Identifier handles("handles");
const cydaw::base::Identifier curveAmount("curveAmount");
const cydaw::base::Identifier cutoffHz("cutoffHz");
const cydaw::base::Identifier destination("destination");
const cydaw::base::Identifier destinationA("destinationA");
const cydaw::base::Identifier destinationB("destinationB");
const cydaw::base::Identifier direction("direction");
const cydaw::base::Identifier driveDb("driveDb");
const cydaw::base::Identifier duration("duration");
const cydaw::base::Identifier durationBeats("durationBeats");
const cydaw::base::Identifier editorMode("editorMode");
const cydaw::base::Identifier enabled("enabled");
const cydaw::base::Identifier eventTimeline("eventTimeline");
const cydaw::base::Identifier events("events");
const cydaw::base::Identifier exposeToSystem("exposeToSystem");
const cydaw::base::Identifier factoryPreset("factoryPreset");
const cydaw::base::Identifier fadeIn("fadeIn");
const cydaw::base::Identifier fadeOut("fadeOut");
const cydaw::base::Identifier file("file");
const cydaw::base::Identifier filterCutoffHz("filterCutoffHz");
const cydaw::base::Identifier format("format");
const cydaw::base::Identifier gain("gain");
const cydaw::base::Identifier gainAutomation("gainAutomation");
const cydaw::base::Identifier clipLanes("lanes");
const cydaw::base::Identifier gainDb("gainDb");
const cydaw::base::Identifier id("id");
const cydaw::base::Identifier inline_("inline");
const cydaw::base::Identifier inputBusId("inputBusId");
const cydaw::base::Identifier key("key");
const cydaw::base::Identifier label("label");
const cydaw::base::Identifier laneHeight("laneHeight");
const cydaw::base::Identifier launchQuantization("launchQuantization");
const cydaw::base::Identifier launcherActionReplay("launcherActionReplay");
const cydaw::base::Identifier launcherActions("launcherActions");
const cydaw::base::Identifier launcherClips("launcherClips");
const cydaw::base::Identifier lengthBeats("lengthBeats");
const cydaw::base::Identifier loop("loop");
const cydaw::base::Identifier launcherScene("launcherScene");
const cydaw::base::Identifier loopEnd("loopEnd");
const cydaw::base::Identifier loopEndBeats("loopEndBeats");
const cydaw::base::Identifier loopRangeExplicit("loopRangeExplicit");
const cydaw::base::Identifier loopStart("loopStart");
const cydaw::base::Identifier loopStartBeats("loopStartBeats");
const cydaw::base::Identifier masterGainDb("masterGainDb");
const cydaw::base::Identifier masterMuted("masterMuted");
const cydaw::base::Identifier maximum("maximum");
const cydaw::base::Identifier midiEditorVisible("midiEditorVisible");
const cydaw::base::Identifier midiNotes("midiNotes");
const cydaw::base::Identifier midiTracks("midiTracks");
const cydaw::base::Identifier minimum("minimum");
const cydaw::base::Identifier modulationAssignments("modulationAssignments");
const cydaw::base::Identifier modulationGraph("modulationGraph");
const cydaw::base::Identifier modules("modules");
const cydaw::base::Identifier monitor("monitor");
const cydaw::base::Identifier mute("mute");
const cydaw::base::Identifier muted("muted");
const cydaw::base::Identifier name("name");
const cydaw::base::Identifier nodeGraph("nodeGraph");
const cydaw::base::Identifier nodes("nodes");
const cydaw::base::Identifier note("note");
const cydaw::base::Identifier notes("notes");
const cydaw::base::Identifier offset("offset");
const cydaw::base::Identifier pan("pan");
const cydaw::base::Identifier parameter("parameter");
const cydaw::base::Identifier parameters("parameters");
const cydaw::base::Identifier physicalA("physicalA");
const cydaw::base::Identifier physicalB("physicalB");
const cydaw::base::Identifier playEnd("playEnd");
const cydaw::base::Identifier playEndBeats("playEndBeats");
const cydaw::base::Identifier playStart("playStart");
const cydaw::base::Identifier playStartBeats("playStartBeats");
const cydaw::base::Identifier pluginFile("pluginFile");
const cydaw::base::Identifier pluginState("pluginState");
const cydaw::base::Identifier plugins("plugins");
const cydaw::base::Identifier pitch("pitch");
const cydaw::base::Identifier points("points");
const cydaw::base::Identifier position("position");
const cydaw::base::Identifier scene("scene");
const cydaw::base::Identifier sharedModulationModules("sharedModulationModules");
const cydaw::base::Identifier sharedNodeModules("sharedNodeModules");
const cydaw::base::Identifier solo("solo");
const cydaw::base::Identifier source("source");
const cydaw::base::Identifier sourceDuration("sourceDuration");
const cydaw::base::Identifier sourceOffset("sourceOffset");
const cydaw::base::Identifier sourceTempo("sourceTempo");
const cydaw::base::Identifier start("start");
const cydaw::base::Identifier startBeats("startBeats");
const cydaw::base::Identifier state("state");
const cydaw::base::Identifier stretchMarkers("stretchMarkers");
const cydaw::base::Identifier stretchMode("stretchMode");
const cydaw::base::Identifier takes("takes");
const cydaw::base::Identifier tempo("tempo");
const cydaw::base::Identifier time("time");
const cydaw::base::Identifier timeline("timeline");
const cydaw::base::Identifier track("track");
const cydaw::base::Identifier tracks("tracks");
const cydaw::base::Identifier type("type");
const cydaw::base::Identifier value("value");
const cydaw::base::Identifier valueDb("valueDb");
const cydaw::base::Identifier velocity("velocity");
const cydaw::base::Identifier version("version");
const cydaw::base::Identifier virtualBuses("virtualBuses");
const cydaw::base::Identifier workspaceLayout("workspaceLayout");
} // namespace keys

// ---- Values ------------------------------------------------------------------------

cydaw::base::Object::Ptr makeObject()
{
    return cydaw::base::Object::Ptr(new cydaw::base::Object());
}

double number(const cydaw::base::Object& object, const cydaw::base::Identifier& property)
{
    return static_cast<double>(object.getProperty(property));
}

double numberOr(const cydaw::base::Object& object, const cydaw::base::Identifier& property, const double fallback)
{
    return object.hasProperty(property) ? number(object, property) : fallback;
}

float toFloat(const cydaw::base::Value& value)
{
    return static_cast<float>(static_cast<double>(value));
}

std::uint32_t colourOf(const cydaw::base::Object& object)
{
    return static_cast<std::uint32_t>(static_cast<std::int64_t>(object.getProperty(keys::colour)));
}

std::uint64_t idOf(const cydaw::base::Value& value)
{
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(value));
}

Engine::AutomationCurve curveOf(const cydaw::base::Object& point)
{
    return static_cast<Engine::AutomationCurve>(engine::clampedCurveShape(static_cast<int>(point.getProperty(keys::curve))));
}

// A point's handles, written only when it has some.
void writeHandles(cydaw::base::Object& point, const engine::CurveHandles& handles)
{
    if (handles == engine::CurveHandles {})
        return;
    cydaw::base::Array<cydaw::base::Value> values;
    for (const auto value : { handles.inTime, handles.inValue, handles.outTime, handles.outValue })
        values.add(static_cast<double>(value));
    point.setProperty(keys::handles, values);
}

engine::CurveHandles handlesOf(const cydaw::base::Object& point)
{
    engine::CurveHandles handles;
    if (const auto* values = point.getProperty(keys::handles).getArray(); values != nullptr && values->size() >= 4) {
        const auto read = [values](const int index) {
            const auto value = static_cast<float>(static_cast<double>((*values)[index]));
            return std::isfinite(value) ? value : 0.0f;
        };
        handles = { read(0), read(1), read(2), read(3) };
    }
    return handles;
}

float curveAmountOf(const cydaw::base::Object& point)
{
    return point.hasProperty(keys::curveAmount) ? toFloat(point.getProperty(keys::curveAmount)) : 0.0f;
}

// v1 also takes an array: the node positions of the oldest projects.
bool isNodeGraphState(const cydaw::base::Value& state)
{
    return state.getDynamicObject() != nullptr || state.getArray() != nullptr;
}

// The mixed audio-and-synth default graph of early versions, replaced by the
// factory graph when it is read.
bool isLegacyMixedFactoryAudioGraph(const cydaw::base::Value& state)
{
    const auto* root = state.getDynamicObject();
    const auto* nodes = root != nullptr ? root->getProperty(keys::nodes).getArray() : nullptr;
    const auto* connections = root != nullptr ? root->getProperty(keys::connections).getArray() : nullptr;
    if (nodes == nullptr || connections == nullptr || nodes->size() != 8 || connections->size() != 7
        || root->getProperty(keys::factoryPreset).toString().isNotEmpty())
        return false;

    std::vector<std::pair<std::uint64_t, cydaw::base::Text>> typedNodes;
    typedNodes.reserve(8);
    cydaw::base::TextList nodeTypes;
    for (const auto& value : *nodes) {
        const auto* node = value.getDynamicObject();
        if (node == nullptr)
            return false;
        const auto type = node->getProperty(keys::type).toString();
        const auto nodeId = idOf(node->getProperty(keys::id));
        if (nodeId == 0 || type.isEmpty())
            return false;
        typedNodes.emplace_back(nodeId, type);
        nodeTypes.add(type);
        const auto* parameters = node->getProperty(keys::parameters).getDynamicObject();
        if (type == "drive"
            && (parameters == nullptr || std::abs(static_cast<double>(parameters->getProperty(keys::driveDb))) > 0.001))
            return false;
        if (type == "lowpass"
            && (parameters == nullptr
                || std::abs(static_cast<double>(parameters->getProperty(keys::cutoffHz)) - 20'000.0) > 0.001))
            return false;
    }
    nodeTypes.sort(true);
    cydaw::base::TextList expectedTypes { "audio-clips", "drive", "gain-pan", "lowpass",
                                      "master", "midi-input", "poly-sine", "spectrum" };
    expectedTypes.sort(true);
    if (nodeTypes != expectedTypes)
        return false;

    const auto typeForId = [&typedNodes](const std::uint64_t nodeId) {
        const auto found = std::find_if(typedNodes.begin(), typedNodes.end(),
                                        [nodeId](const auto& item) { return item.first == nodeId; });
        return found != typedNodes.end() ? found->second : cydaw::base::Text {};
    };
    cydaw::base::TextList edges;
    for (const auto& value : *connections) {
        const auto* connection = value.getDynamicObject();
        if (connection == nullptr)
            return false;
        const auto from = typeForId(idOf(connection->getProperty(keys::source)));
        const auto to = typeForId(idOf(connection->getProperty(keys::destination)));
        if (from.isEmpty() || to.isEmpty())
            return false;
        edges.add(from + "->" + to);
    }
    edges.sort(true);
    cydaw::base::TextList expectedEdges { "audio-clips->drive", "drive->lowpass",        "gain-pan->spectrum",
                                      "lowpass->gain-pan",  "midi-input->poly-sine", "poly-sine->master",
                                      "spectrum->master" };
    expectedEdges.sort(true);
    return edges == expectedEdges;
}

// ---- Class libraries -------------------------------------------------------------------

std::map<cydaw::base::Text, cydaw::base::Value> modulesByKey(const cydaw::base::Value& library)
{
    std::map<cydaw::base::Text, cydaw::base::Value> byKey;
    if (const auto* modules = library.getArray())
        for (const auto& module : *modules)
            if (const auto* object = module.getDynamicObject())
                byKey[object->getProperty(keys::key).toString()] = module;
    return byKey;
}

// The classes of a graph whose body differs from the library's, as a state
// Studio::mergeModules reads. A class only restored from the library is left
// out, so a graph merged after the one that edited a class cannot undo the edit.
cydaw::base::Value editedModules(const cydaw::base::Value& graphState, const std::map<cydaw::base::Text, cydaw::base::Value>& library)
{
    cydaw::base::Array<cydaw::base::Value> edited;
    if (const auto* graph = graphState.getDynamicObject())
        if (const auto* modules = graph->getProperty(keys::modules).getArray())
            for (const auto& module : *modules) {
                const auto* object = module.getDynamicObject();
                if (object == nullptr || static_cast<bool>(object->getProperty(keys::inline_)))
                    continue;
                const auto moduleKey = object->getProperty(keys::key).toString();
                if (moduleKey.isEmpty())
                    continue;
                const auto found = library.find(moduleKey);
                if (found == library.end() || !app::jsonValuesMatch(found->second, module))
                    edited.add(module);
            }
    auto state = makeObject();
    state->setProperty(keys::modules, cydaw::base::Value(edited));
    return cydaw::base::Value(state.get());
}

// ---- Writing -------------------------------------------------------------------------------

// Relative to the project's folder when the project has a file; another drive
// gives an absolute path, as getRelativePathFrom does.
cydaw::base::Text projectAssetPath(const cydaw::base::File& asset, const cydaw::base::File& projectFile)
{
    if (asset == cydaw::base::File {})
        return {};
    if (projectFile == cydaw::base::File {})
        return asset.getFullPathName();
    return asset.getRelativePathFrom(projectFile.getParentDirectory());
}

// A track's outputs: the master, or a track by its kind and its place
// among those of its kind, at a level.
cydaw::base::Value outputsToVar(Studio& studio, const TrackRef& track)
{
    cydaw::base::Array<cydaw::base::Value> list;
    const auto summaries = studio.engine().midiTrackSummaries();
    for (const auto& output : studio.trackOutputs(track)) {
        auto value = cydaw::base::Object::Ptr(new cydaw::base::Object());
        value->setProperty("gainDb", output.gainDb);
        if (output.preFader)
            value->setProperty("preFader", true);
        // The destination's input: its graph node, kept with the graph.
        if (output.input != 0)
            value->setProperty("input", static_cast<std::int64_t>(output.input));
        if (output.hardware != 0) {
            // A virtual output of the software, by its id and its name (the
            // software keeps them, not the project: another machine finds it
            // by name).
            value->setProperty("hardware", static_cast<std::int64_t>(output.hardware));
            if (const auto bus = studio.engine().virtualBus(output.hardware))
                value->setProperty("hardwareName", bus->name);
            if (output.hardwareAfterEffects)
                value->setProperty("hardwareAfterEffects", true);
        } else if (output.destination == 0) {
            value->setProperty("master", true);
        } else if (const auto to = studio.trackForKey(output.destination)) {
            value->setProperty("midi", to->midi);
            auto position = static_cast<int>(to->audioIndex);
            if (to->midi)
                for (std::size_t index = 0; index < summaries.size(); ++index)
                    if (summaries[index].id == to->midiId)
                        position = static_cast<int>(index);
            value->setProperty("position", position);
        } else {
            continue;
        }
        list.add(cydaw::base::Value(value.get()));
    }
    return list;
}

// The launcher's empty slots whose stop was taken off, by scene.
cydaw::base::Value stopsOffToVar(const Studio& studio, const TrackRef& track)
{
    cydaw::base::Array<cydaw::base::Value> scenes;
    for (auto scene = 0; scene < app::AudioTrackController::maximumLauncherScenes; ++scene)
        if (!studio.slotStops(track, scene))
            scenes.add(scene);
    return scenes;
}

// A project's virtual buses (its ids) as the software has them now: the
// same id under the same name, else the same name and direction, else none.
using BusIds = std::map<std::uint64_t, std::uint64_t>;

std::uint64_t softwareBus(const Studio& studio, const BusIds& ids, const std::uint64_t saved, const cydaw::base::Text& name,
                          const app::VirtualBusDirection direction)
{
    const auto buses = studio.engine().virtualBuses();
    const auto named = [&](const cydaw::base::Text& wanted) -> std::uint64_t {
        for (const auto& bus : buses)
            if (bus.direction == direction && bus.name.equalsIgnoreCase(wanted))
                return bus.id;
        return 0;
    };
    if (name.isNotEmpty()) {
        for (const auto& bus : buses)
            if (bus.id == saved && bus.direction == direction && bus.name.equalsIgnoreCase(name))
                return bus.id;
        if (const auto id = named(name))
            return id;
    }
    if (const auto found = ids.find(saved); found != ids.end())
        return found->second;
    // An older project, without its buses: its ids, when the software has them.
    if (ids.empty())
        for (const auto& bus : buses)
            if (bus.id == saved && bus.direction == direction)
                return bus.id;
    return 0;
}

BusIds mapProjectBuses(const Studio& studio, const cydaw::base::Object& root)
{
    BusIds ids;
    if (const auto* buses = root.getProperty(keys::virtualBuses).getArray())
        for (const auto& value : *buses) {
            const auto id = static_cast<std::uint64_t>(std::max<std::int64_t>(0, static_cast<std::int64_t>(value.getProperty(keys::id, 0))));
            if (id == 0)
                continue;
            const auto direction = static_cast<app::VirtualBusDirection>(
                cydaw::base::limit(0, 1, static_cast<int>(value.getProperty(keys::direction, 0))));
            ids[id] = softwareBus(studio, {}, id, value.getProperty(keys::name, cydaw::base::Value()).toString(), direction);
        }
    return ids;
}

// The routing read back once every track is there.
// The controllers' assignments name a track by its key ("track/<key>/volume",
// "param/<key>/<binding>"), which an opened project gives anew: saved, a
// track is named by where it is ("@midi:2", "@audio:0"), as routing does.
cydaw::base::Value portableAssignments(Studio& studio, const cydaw::base::Value& assignments)
{
    const auto summaries = studio.engine().midiTrackSummaries();
    cydaw::base::Array<cydaw::base::Value> out;
    if (const auto* list = assignments.getArray())
        for (const auto& item : *list) {
            auto parts = cydaw::base::TextList::fromTokens(item.getProperty("target", {}).toString(), "/", "");
            if (parts.size() >= 3 && (parts[0] == "track" || parts[0] == "param") && parts[1].containsOnly("0123456789")) {
                const auto track = studio.trackForKey(static_cast<std::uint64_t>(parts[1].getLargeIntValue()));
                if (!track)
                    continue;
                if (track->midi) {
                    const auto found = std::find_if(summaries.begin(), summaries.end(),
                                                    [&track](const auto& summary) { return summary.id == track->midiId; });
                    if (found == summaries.end())
                        continue;
                    parts.set(1, "@midi:" + cydaw::base::Text(static_cast<int>(found - summaries.begin())));
                } else {
                    parts.set(1, "@audio:" + cydaw::base::Text(static_cast<int>(track->audioIndex)));
                }
            }
            auto copy = cydaw::base::Value(item.getDynamicObject() != nullptr ? item.getDynamicObject()->clone().release() : new cydaw::base::Object());
            copy.getDynamicObject()->setProperty("target", parts.joinIntoString("/"));
            out.add(copy);
        }
    return out;
}

cydaw::base::Value localAssignments(Studio& studio, const cydaw::base::Value& saved)
{
    const auto summaries = studio.engine().midiTrackSummaries();
    cydaw::base::Array<cydaw::base::Value> out;
    if (const auto* list = saved.getArray())
        for (const auto& item : *list) {
            auto parts = cydaw::base::TextList::fromTokens(item.getProperty("target", {}).toString(), "/", "");
            if (parts.size() >= 3 && parts[1].startsWithChar('@')) {
                const auto position = parts[1].fromFirstOccurrenceOf(":", false, false).getIntValue();
                std::optional<TrackRef> track;
                if (parts[1].startsWith("@midi:") && position >= 0 && position < static_cast<int>(summaries.size()))
                    track = TrackRef { true, 0, summaries[static_cast<std::size_t>(position)].id };
                else if (parts[1].startsWith("@audio:") && position >= 0 && position < static_cast<int>(studio.engine().trackCount()))
                    track = TrackRef { false, static_cast<std::size_t>(position), 0 };
                if (!track)
                    continue;
                parts.set(1, cydaw::base::Text(static_cast<std::int64_t>(studio.routingKey(*track))));
            }
            auto copy = cydaw::base::Value(item.getDynamicObject() != nullptr ? item.getDynamicObject()->clone().release() : new cydaw::base::Object());
            copy.getDynamicObject()->setProperty("target", parts.joinIntoString("/"));
            out.add(copy);
        }
    return out;
}

void restoreRouting(Studio& studio, const cydaw::base::Object& root, const BusIds& busIds)
{
    auto& engine = studio.engine();
    const auto summaries = engine.midiTrackSummaries();
    const auto keyOf = [&](const cydaw::base::Value& value) -> std::optional<std::uint64_t> {
        if (static_cast<bool>(value.getProperty("master", false)))
            return 0;
        const auto position = static_cast<int>(value.getProperty("position", -1));
        if (static_cast<bool>(value.getProperty("midi", false))) {
            if (position >= 0 && position < static_cast<int>(summaries.size()))
                return studio.routingKey(TrackRef { true, 0, summaries[static_cast<std::size_t>(position)].id });
        } else if (position >= 0 && position < static_cast<int>(engine.trackCount())) {
            return studio.routingKey(TrackRef { false, static_cast<std::size_t>(position), 0 });
        }
        return std::nullopt;
    };
    const auto apply = [&](const TrackRef& track, const cydaw::base::Value& saved) {
        std::vector<TrackOutput> outputs;
        if (const auto* list = saved.getProperty("outputs", cydaw::base::Value()).getArray())
            for (const auto& item : *list)
                if (const auto hardware = static_cast<std::int64_t>(item.getProperty("hardware", 0)); hardware > 0) {
                    TrackOutput output;
                    output.hardware = softwareBus(studio, busIds, static_cast<std::uint64_t>(hardware),
                                                  item.getProperty("hardwareName", cydaw::base::Value()).toString(),
                                                  app::VirtualBusDirection::output);
                    output.gainDb = static_cast<float>(static_cast<double>(item.getProperty("gainDb", 0.0)));
                    output.preFader = static_cast<bool>(item.getProperty("preFader", false));
                    output.hardwareAfterEffects = static_cast<bool>(item.getProperty("hardwareAfterEffects", false));
                    if (const auto bus = engine.virtualBus(output.hardware);
                        output.hardware != 0 && bus && bus->direction == app::VirtualBusDirection::output)
                        outputs.push_back(output);
                } else if (const auto key = keyOf(item)) {
                    TrackOutput output;
                    output.destination = *key;
                    output.gainDb = static_cast<float>(static_cast<double>(item.getProperty("gainDb", 0.0)));
                    output.input = *key != 0 ? static_cast<std::uint64_t>(static_cast<std::int64_t>(item.getProperty("input", 0))) : 0;
                    output.preFader = static_cast<bool>(item.getProperty("preFader", false));
                    outputs.push_back(output);
                }
        if (outputs.size() == 1 && outputs.front() == TrackOutput {})
            outputs.clear();
        studio.restoreTrackRouting(track, std::move(outputs), static_cast<bool>(saved.getProperty("bus", false)));
        if (const auto* off = saved.getProperty("launcherStopOff", cydaw::base::Value()).getArray())
            for (const auto& scene : *off)
                studio.setSlotStops(track, static_cast<int>(scene), false);
    };
    if (const auto* tracks = root.getProperty(keys::tracks).getArray())
        for (int index = 0; index < tracks->size() && index < static_cast<int>(engine.trackCount()); ++index)
            apply(TrackRef { false, static_cast<std::size_t>(index), 0 }, (*tracks)[index]);
    if (const auto* tracks = root.getProperty(keys::midiTracks).getArray())
        for (int index = 0; index < tracks->size() && index < static_cast<int>(summaries.size()); ++index)
            apply(TrackRef { true, 0, summaries[static_cast<std::size_t>(index)].id }, (*tracks)[index]);
    // Its hybrid tracks: each track's companion (before the order: the
    // companions are not shown).
    if (const auto* hybrids = root.getProperty("hybrids").getArray())
        for (const auto& item : *hybrids) {
            const auto track = keyOf(item.getProperty("track", cydaw::base::Value()));
            const auto companion = keyOf(item.getProperty("companion", cydaw::base::Value()));
            if (track && companion)
                studio.restoreCompanion(*track, *companion, static_cast<bool>(item.getProperty("recordsAudio", false)));
        }
    // Its groups: each group's track, its tracks, folded or not.
    if (const auto* groups = root.getProperty("groups").getArray())
        for (const auto& item : *groups) {
            const auto group = keyOf(item.getProperty("group", cydaw::base::Value()));
            if (!group || *group == 0)
                continue;
            std::vector<std::uint64_t> members;
            if (const auto* list = item.getProperty("members", cydaw::base::Value()).getArray())
                for (const auto& member : *list)
                    if (const auto key = keyOf(member); key && *key != 0)
                        members.push_back(*key);
            studio.restoreGroup(*group, members, static_cast<bool>(item.getProperty("folded", false)));
        }
    // The order shown, every kind together (an older project: the audio
    // tracks, then the instruments).
    if (const auto* order = root.getProperty("trackOrder").getArray()) {
        std::vector<std::uint64_t> keys;
        for (const auto& item : *order)
            if (const auto key = keyOf(item); key && *key != 0)
                keys.push_back(*key);
        studio.setTrackOrder(std::move(keys));
    }
    studio.publishRouting();
}

cydaw::base::Value automationPointsToVar(const std::vector<Engine::AutomationPoint>& points)
{
    cydaw::base::Array<cydaw::base::Value> values;
    values.ensureStorageAllocated(static_cast<int>(points.size()));
    for (const auto& point : points) {
        auto value = makeObject();
        value->setProperty(keys::time, point.timeSeconds);
        value->setProperty(keys::valueDb, point.valueDb);
        value->setProperty(keys::curve, static_cast<int>(point.curve));
        value->setProperty(keys::curveAmount, point.curveAmount);
        writeHandles(*value, point.handles);
        values.add(cydaw::base::Value(value.get()));
    }
    return values;
}

// A track's device automation: [{binding, points: [{time, value, curve,
// curveAmount}]}].
cydaw::base::Value parameterLanesToVar(const ParameterLanes& lanes)
{
    cydaw::base::Array<cydaw::base::Value> values;
    for (const auto& [name, points] : lanes.lanes) {
        if (points.empty())
            continue;
        auto lane = makeObject();
        lane->setProperty(keys::binding, name);
        cydaw::base::Array<cydaw::base::Value> list;
        for (const auto& point : points) {
            auto item = makeObject();
            item->setProperty(keys::time, point.timeSeconds);
            item->setProperty(keys::value, point.valueDb);
            item->setProperty(keys::curve, static_cast<int>(point.curve));
            item->setProperty(keys::curveAmount, point.curveAmount);
            writeHandles(*item, point.handles);
            list.add(cydaw::base::Value(item.get()));
        }
        lane->setProperty(keys::points, list);
        values.add(cydaw::base::Value(lane.get()));
    }
    return values;
}

// A clip's automation lanes: [{binding, points:[{time, value, curve, curveAmount}]}].
cydaw::base::Value clipLanesToVar(const std::map<cydaw::base::Text, std::vector<Engine::AutomationPoint>>& lanes)
{
    ParameterLanes shaped;
    shaped.lanes = lanes;
    return parameterLanesToVar(shaped);
}

std::map<cydaw::base::Text, std::vector<Engine::AutomationPoint>> readClipLanes(const cydaw::base::Object& clip)
{
    std::map<cydaw::base::Text, std::vector<Engine::AutomationPoint>> lanes;
    const auto* list = clip.getProperty(keys::clipLanes).getArray();
    if (list == nullptr)
        return lanes;
    std::uint64_t nextId = 1;
    for (const auto& laneValue : *list) {
        const auto* lane = laneValue.getDynamicObject();
        if (lane == nullptr)
            continue;
        const auto name = lane->getProperty(keys::binding).toString();
        const auto* points = lane->getProperty(keys::points).getArray();
        if (name.isEmpty() || points == nullptr)
            continue;
        auto& stored = lanes[name];
        for (const auto& pointValue : *points)
            if (const auto* item = pointValue.getDynamicObject()) {
                Engine::AutomationPoint point;
                point.id = nextId++;
                point.timeSeconds = static_cast<double>(item->getProperty(keys::time));
                point.valueDb = static_cast<float>(static_cast<double>(item->getProperty(keys::value)));
                point.curve = curveOf(*item);
                point.curveAmount = static_cast<float>(static_cast<double>(item->getProperty(keys::curveAmount)));
                point.handles = handlesOf(*item);
                if (std::isfinite(point.valueDb) && std::isfinite(point.timeSeconds))
                    stored.push_back(point);
            }
        std::stable_sort(stored.begin(), stored.end(), [](const auto& a, const auto& b) { return a.timeSeconds < b.timeSeconds; });
        if (stored.empty())
            lanes.erase(name);
    }
    return lanes;
}

void readParameterLanes(const cydaw::base::Object& track, ParameterLanes& lanes)
{
    lanes = {};
    lanes.shown = track.getProperty(keys::shownAutomation).toString();
    if (const auto* list = track.getProperty(keys::parameterAutomation).getArray())
        for (const auto& laneValue : *list) {
            const auto* lane = laneValue.getDynamicObject();
            if (lane == nullptr)
                continue;
            const auto name = lane->getProperty(keys::binding).toString();
            const auto* points = lane->getProperty(keys::points).getArray();
            if (name.isEmpty() || points == nullptr)
                continue;
            auto& stored = lanes.lanes[name];
            for (const auto& pointValue : *points)
                if (const auto* item = pointValue.getDynamicObject()) {
                    Engine::AutomationPoint point;
                    point.id = lanes.nextId++;
                    point.timeSeconds = std::max(0.0, static_cast<double>(item->getProperty(keys::time)));
                    point.valueDb = static_cast<float>(static_cast<double>(item->getProperty(keys::value)));
                    point.curve = curveOf(*item);
                    point.curveAmount = static_cast<float>(static_cast<double>(item->getProperty(keys::curveAmount)));
                    point.handles = handlesOf(*item);
                    if (std::isfinite(point.valueDb) && std::isfinite(point.timeSeconds))
                        stored.push_back(point);
                }
            std::stable_sort(stored.begin(), stored.end(),
                             [](const auto& a, const auto& b) { return a.timeSeconds < b.timeSeconds; });
            if (stored.empty())
                lanes.lanes.erase(name);
        }
}

// A note's expression: [[position, value(, bend)], ...] (none: not
// written; the bend only when the way to the next point is bent).
cydaw::base::Value expressionToVar(const std::vector<Engine::NoteExpressionPoint>& points)
{
    cydaw::base::Array<cydaw::base::Value> values;
    for (const auto& point : points)
        values.add(point.bend != 0.0f ? cydaw::base::Array<cydaw::base::Value> { point.position, point.value, point.bend }
                                      : cydaw::base::Array<cydaw::base::Value> { point.position, point.value });
    return values;
}

std::vector<Engine::NoteExpressionPoint> expressionFromVar(const cydaw::base::Value& value)
{
    std::vector<Engine::NoteExpressionPoint> points;
    if (const auto* items = value.getArray())
        for (const auto& item : *items)
            if (const auto* pair = item.getArray(); pair != nullptr && pair->size() >= 2) {
                const auto position = static_cast<float>(static_cast<double>((*pair)[0]));
                const auto level = static_cast<float>(static_cast<double>((*pair)[1]));
                const auto bend = pair->size() >= 3 ? static_cast<float>(static_cast<double>((*pair)[2])) : 0.0f;
                if (std::isfinite(position) && std::isfinite(level))
                    points.push_back({ position, level, std::isfinite(bend) ? bend : 0.0f });
            }
    return points;
}

cydaw::base::Value midiNotesToVar(const std::vector<Engine::MidiNote>& notes)
{
    cydaw::base::Array<cydaw::base::Value> values;
    values.ensureStorageAllocated(static_cast<int>(notes.size()));
    for (const auto& noteInfo : notes) {
        auto value = makeObject();
        value->setProperty(keys::note, noteInfo.noteNumber);
        value->setProperty(keys::startBeats, noteInfo.startBeats);
        value->setProperty(keys::durationBeats, noteInfo.durationBeats);
        value->setProperty(keys::velocity, noteInfo.velocity);
        if (!noteInfo.pitch.empty())
            value->setProperty(keys::pitch, expressionToVar(noteInfo.pitch));
        if (!noteInfo.gain.empty())
            value->setProperty(keys::gain, expressionToVar(noteInfo.gain));
        if (!noteInfo.timbre.empty())
            value->setProperty("timbre", expressionToVar(noteInfo.timbre));
        if (!noteInfo.pressure.empty())
            value->setProperty("pressure", expressionToVar(noteInfo.pressure));
        if (!noteInfo.pan.empty())
            value->setProperty("pan", expressionToVar(noteInfo.pan));
        if (noteInfo.chance < 1.0f)
            value->setProperty("chance", noteInfo.chance);
        values.add(cydaw::base::Value(value.get()));
    }
    return values;
}

// A clip's markers carry no id; an event's do.
cydaw::base::Value stretchMarkersToVar(const std::vector<Engine::StretchMarker>& markers, const bool withIds)
{
    cydaw::base::Array<cydaw::base::Value> values;
    for (const auto& marker : markers) {
        auto value = makeObject();
        if (withIds)
            value->setProperty(keys::id, static_cast<std::int64_t>(marker.id));
        value->setProperty(keys::source, marker.sourceSeconds);
        value->setProperty(keys::timeline, marker.timelineSeconds);
        values.add(cydaw::base::Value(value.get()));
    }
    return values;
}

cydaw::base::Value eventsToVar(const std::vector<Engine::AudioEvent>& events, const cydaw::base::File& projectFile)
{
    cydaw::base::Array<cydaw::base::Value> values;
    for (const auto& event : events) {
        auto value = makeObject();
        value->setProperty(keys::id, static_cast<std::int64_t>(event.id));
        value->setProperty(keys::name, event.name);
        value->setProperty(keys::start, event.startSeconds);
        value->setProperty(keys::offset, event.sourceOffsetSeconds);
        value->setProperty(keys::duration, event.durationSeconds);
        value->setProperty(keys::sourceDuration, event.sourceDurationSeconds);
        value->setProperty(keys::colour, static_cast<std::int64_t>(event.colour));
        value->setProperty(keys::source, projectAssetPath(event.sourceFile, projectFile));
        value->setProperty(keys::fadeIn, event.fadeInSeconds);
        value->setProperty(keys::fadeOut, event.fadeOutSeconds);
        value->setProperty(keys::sourceTempo, event.sourceTempoBpm);
        value->setProperty(keys::gainDb, event.gainDb);
        value->setProperty(keys::stretchMode, static_cast<int>(event.stretchMode));
        value->setProperty(keys::stretchMarkers, stretchMarkersToVar(event.stretchMarkers, true));
        if (event.pitchSemitones != 0.0)
            value->setProperty("pitch", event.pitchSemitones);
        if (!event.lanes.empty())
            value->setProperty(keys::clipLanes, clipLanesToVar(event.lanes));
        values.add(cydaw::base::Value(value.get()));
    }
    return values;
}

cydaw::base::Value audioClipToVar(const Engine::AudioClip& clip, const cydaw::base::File& projectFile)
{
    auto value = makeObject();
    value->setProperty(keys::name, clip.name);
    value->setProperty(keys::start, clip.startSeconds);
    value->setProperty(keys::offset, clip.sourceOffsetSeconds);
    value->setProperty(keys::duration, clip.durationSeconds);
    value->setProperty(keys::playStart, clip.playStartSeconds);
    value->setProperty(keys::playEnd, clip.playEndSeconds);
    value->setProperty(keys::loopStart, clip.loopStartSeconds);
    value->setProperty(keys::loopEnd, clip.loopEndSeconds);
    value->setProperty(keys::loop, clip.looping);
    value->setProperty(keys::eventTimeline, clip.usesEventTimeline);
    value->setProperty(keys::sourceDuration, clip.sourceDurationSeconds);
    value->setProperty(keys::sourceTempo, clip.sourceTempoBpm);
    value->setProperty(keys::stretchMode, static_cast<int>(clip.stretchMode));
    value->setProperty(keys::stretchMarkers, stretchMarkersToVar(clip.stretchMarkers, false));
    value->setProperty(keys::events, eventsToVar(clip.events, projectFile));
    value->setProperty(keys::gainAutomation, automationPointsToVar(clip.automation));
    if (!clip.lanes.empty())
        value->setProperty(keys::clipLanes, clipLanesToVar(clip.lanes));
    value->setProperty(keys::fadeIn, clip.fadeInSeconds);
    value->setProperty(keys::fadeOut, clip.fadeOutSeconds);
    value->setProperty(keys::colour, static_cast<std::int64_t>(clip.colour));
    value->setProperty(keys::source, projectAssetPath(clip.sourceFile, projectFile));
    value->setProperty(keys::activeTake, static_cast<std::int64_t>(clip.activeTakeIndex));
    cydaw::base::Array<cydaw::base::Value> takes;
    for (const auto& take : clip.takes) {
        auto takeValue = makeObject();
        takeValue->setProperty(keys::name, take.name);
        takeValue->setProperty(keys::source, projectAssetPath(take.sourceFile, projectFile));
        if (take.shiftSeconds != 0.0)
            takeValue->setProperty("shift", take.shiftSeconds);
        takes.add(cydaw::base::Value(takeValue.get()));
    }
    value->setProperty(keys::takes, takes);
    // Transposed; its takes' passages; its destructive edits' history.
    if (clip.pitchSemitones != 0.0)
        value->setProperty("pitch", clip.pitchSemitones);
    if (!clip.comp.empty()) {
        cydaw::base::Array<cydaw::base::Value> comp;
        for (const auto& part : clip.comp) {
            auto partValue = makeObject();
            partValue->setProperty(keys::start, part.startSeconds);
            partValue->setProperty("end", part.endSeconds);
            partValue->setProperty("take", static_cast<std::int64_t>(part.take));
            comp.add(cydaw::base::Value(partValue.get()));
        }
        value->setProperty("comp", comp);
    }
    if (!clip.versions.empty()) {
        cydaw::base::Array<cydaw::base::Value> versions;
        for (const auto& version : clip.versions) {
            auto versionValue = makeObject();
            versionValue->setProperty(keys::label, version.label);
            versionValue->setProperty(keys::source, projectAssetPath(version.file, projectFile));
            versionValue->setProperty(keys::offset, version.sourceOffsetSeconds);
            versionValue->setProperty(keys::duration, version.durationSeconds);
            versionValue->setProperty(keys::sourceDuration, version.sourceDurationSeconds);
            versionValue->setProperty("sourceLength", version.sourceLengthSeconds);
            versions.add(cydaw::base::Value(versionValue.get()));
        }
        value->setProperty("versions", versions);
        value->setProperty("version", static_cast<std::int64_t>(clip.version));
    }
    return cydaw::base::Value(value.get());
}

cydaw::base::Value launcherClipToVar(const Engine::LauncherClip& clip, const cydaw::base::File& projectFile)
{
    auto value = makeObject();
    value->setProperty(keys::scene, clip.sceneIndex);
    value->setProperty(keys::name, clip.name);
    value->setProperty(keys::source, projectAssetPath(clip.sourceFile, projectFile));
    value->setProperty(keys::offset, clip.sourceOffsetSeconds);
    value->setProperty(keys::duration, clip.durationSeconds);
    value->setProperty(keys::loop, clip.looping);
    value->setProperty(keys::colour, static_cast<std::int64_t>(clip.colour));
    value->setProperty(keys::playStart, clip.playStartSeconds);
    value->setProperty(keys::playEnd, clip.playEndSeconds);
    value->setProperty(keys::loopStart, clip.loopStartSeconds);
    value->setProperty(keys::loopEnd, clip.loopEndSeconds);
    value->setProperty(keys::eventTimeline, clip.usesEventTimeline);
    value->setProperty(keys::events, eventsToVar(clip.events, projectFile));
    value->setProperty(keys::gainAutomation, automationPointsToVar(clip.automation));
    if (!clip.lanes.empty())
        value->setProperty(keys::clipLanes, clipLanesToVar(clip.lanes));
    return cydaw::base::Value(value.get());
}

cydaw::base::Value pluginsToVar(Engine& engine, const std::size_t trackIndex, const Engine::TrackInfo& info,
                       const bool refreshPluginState, const cydaw::base::File& projectFile)
{
    const auto lanes = engine.pluginAutomationLanes(trackIndex);
    cydaw::base::Array<cydaw::base::Value> plugins;
    for (std::size_t pluginIndex = 0; pluginIndex < info.plugins.size(); ++pluginIndex) {
        auto value = makeObject();
        value->setProperty(keys::file, projectAssetPath(info.plugins[pluginIndex].file, projectFile));
        cydaw::base::MemoryBlock pluginState;
        const auto stateResult = refreshPluginState
                                     ? engine.getTrackPluginState(trackIndex, pluginIndex, pluginState)
                                     : engine.getCachedTrackPluginState(trackIndex, pluginIndex, pluginState);
        if (stateResult.wasOk())
            value->setProperty(keys::state, pluginState.toBase64Encoding());
        cydaw::base::Array<cydaw::base::Value> automation;
        for (const auto& lane : lanes) {
            if (lane.pluginIndex != pluginIndex)
                continue;
            auto laneValue = makeObject();
            laneValue->setProperty(keys::parameter, static_cast<std::int64_t>(lane.parameterIndex));
            laneValue->setProperty(keys::name, lane.parameterName);
            cydaw::base::Array<cydaw::base::Value> points;
            points.ensureStorageAllocated(static_cast<int>(lane.points.size()));
            for (const auto& point : lane.points) {
                auto pointValue = makeObject();
                pointValue->setProperty(keys::time, point.timeSeconds);
                pointValue->setProperty(keys::value, point.normalisedValue);
                pointValue->setProperty(keys::curve, static_cast<int>(point.curve));
                pointValue->setProperty(keys::curveAmount, point.curveAmount);
                writeHandles(*pointValue, point.handles);
                points.add(cydaw::base::Value(pointValue.get()));
            }
            laneValue->setProperty(keys::points, points);
            automation.add(cydaw::base::Value(laneValue.get()));
        }
        value->setProperty(keys::automation, automation);
        plugins.add(cydaw::base::Value(value.get()));
    }
    return plugins;
}

const cydaw::base::Value& graphOr(const std::map<std::uint64_t, cydaw::base::Value>& graphs, const std::uint64_t ownerId,
                         const cydaw::base::Value& fallback)
{
    const auto found = graphs.find(ownerId);
    return found != graphs.end() && found->second.getDynamicObject() != nullptr ? found->second : fallback;
}

cydaw::base::Value interfaceValue(const Studio& studio, const cydaw::base::Identifier& property, const cydaw::base::Value& fallback)
{
    if (const auto* ui = studio.interfaceState.getDynamicObject(); ui != nullptr && ui->hasProperty(property))
        return ui->getProperty(property);
    return fallback;
}

// ---- Reading -------------------------------------------------------------------------------

void warn(cydaw::base::TextList& warnings, const cydaw::base::Text& line)
{
    (void) warnings.addIfNotAlreadyThere(line);
}

cydaw::base::Text missingAudio(const cydaw::base::File& audio)
{
    return "Missing audio file: " + (audio == cydaw::base::File {} ? cydaw::base::Text("(no path)") : audio.getFullPathName());
}

// A stored path: absolute, or relative to the project's folder. A file
// written on Windows separates its folders with backslashes.
cydaw::base::File resolveAsset(const cydaw::base::Text& path, const cydaw::base::File& projectFile, const cydaw::base::File& fallback = {})
{
    if (path.isEmpty())
        return fallback;
    auto normalised = path;
    if (cydaw::base::File::getSeparatorChar() != '\\')
        normalised = normalised.replaceCharacter('\\', '/');
    if (cydaw::base::File::isAbsolutePath(normalised))
        return cydaw::base::File(normalised);
    const auto folder = projectFile == cydaw::base::File {} ? cydaw::base::File::getCurrentWorkingDirectory()
                                                     : projectFile.getParentDirectory();
    return folder.getChildFile(normalised);
}

// A volume lane as saved: its points, their shapes and handles.
std::vector<Engine::AutomationPoint> readGainPoints(const cydaw::base::Value& points)
{
    std::vector<Engine::AutomationPoint> read;
    if (const auto* values = points.getArray())
        for (const auto& value : *values)
            if (const auto* point = value.getDynamicObject()) {
                Engine::AutomationPoint item;
                item.timeSeconds = number(*point, keys::time);
                item.valueDb = toFloat(point->getProperty(keys::valueDb));
                item.curve = curveOf(*point);
                item.curveAmount = curveAmountOf(*point);
                item.handles = handlesOf(*point);
                if (std::isfinite(item.timeSeconds) && std::isfinite(item.valueDb))
                    read.push_back(item);
            }
    return read;
}

template <typename Add, typename SetCurve>
void restoreGainPoints(const cydaw::base::Value& points, Add&& add, SetCurve&& setCurve)
{
    const auto* values = points.getArray();
    if (values == nullptr)
        return;
    for (const auto& value : *values) {
        const auto* point = value.getDynamicObject();
        if (point == nullptr)
            continue;
        const std::uint64_t pointId = add(number(*point, keys::time), toFloat(point->getProperty(keys::valueDb)));
        if (pointId != 0 && point->hasProperty(keys::curve))
            setCurve(pointId, curveOf(*point), curveAmountOf(*point));
    }
}

// False when an event is unreadable or its file is missing: v1 then leaves
// the clip's events as they are.
bool readEvents(const cydaw::base::Array<cydaw::base::Value>& saved, const cydaw::base::File& fallbackSource, const cydaw::base::File& projectFile,
                cydaw::base::TextList& warnings, std::vector<Engine::AudioEvent>& events)
{
    events.reserve(static_cast<std::size_t>(saved.size()));
    for (const auto& value : saved) {
        const auto* event = value.getDynamicObject();
        if (event == nullptr)
            return false;
        const auto sourceFile = resolveAsset(event->getProperty(keys::source).toString(), projectFile, fallbackSource);
        if (!sourceFile.existsAsFile()) {
            warn(warnings, missingAudio(sourceFile));
            return false;
        }
        Engine::AudioEvent restored;
        restored.id = idOf(event->getProperty(keys::id));
        restored.name = event->getProperty(keys::name).toString();
        restored.startSeconds = number(*event, keys::start);
        restored.sourceOffsetSeconds = number(*event, keys::offset);
        restored.durationSeconds = number(*event, keys::duration);
        restored.sourceDurationSeconds = numberOr(*event, keys::sourceDuration, restored.durationSeconds);
        restored.colour = colourOf(*event);
        restored.sourceFile = sourceFile;
        restored.fadeInSeconds = number(*event, keys::fadeIn);
        restored.fadeOutSeconds = number(*event, keys::fadeOut);
        restored.sourceTempoBpm = number(*event, keys::sourceTempo);
        restored.gainDb = event->hasProperty(keys::gainDb) ? toFloat(event->getProperty(keys::gainDb)) : 0.0f;
        restored.stretchMode = static_cast<Engine::StretchMode>(
            cydaw::base::limit(0, Engine::stretchModeCount - 1, static_cast<int>(event->getProperty(keys::stretchMode))));
        restored.pitchSemitones = numberOr(*event, "pitch", 0.0);
        restored.lanes = readClipLanes(*event);
        if (const auto* markers = event->getProperty(keys::stretchMarkers).getArray())
            for (const auto& markerValue : *markers)
                if (const auto* marker = markerValue.getDynamicObject())
                    restored.stretchMarkers.push_back({ idOf(marker->getProperty(keys::id)),
                                                        number(*marker, keys::source),
                                                        number(*marker, keys::timeline) });
        events.push_back(std::move(restored));
    }
    return true;
}

// The modulation assignments in v1's own shape.
cydaw::base::Value readAssignments(const cydaw::base::Value& saved)
{
    const auto* values = saved.getArray();
    if (values == nullptr)
        return {};
    cydaw::base::Array<cydaw::base::Value> assignments;
    for (const auto& value : *values) {
        const auto* stored = value.getDynamicObject();
        if (stored == nullptr)
            continue;
        auto assignment = makeObject();
        assignment->setProperty(keys::binding, stored->getProperty(keys::binding).toString());
        assignment->setProperty(keys::label, stored->getProperty(keys::label).toString());
        assignment->setProperty(keys::base, number(*stored, keys::base));
        assignment->setProperty(keys::minimum, number(*stored, keys::minimum));
        assignment->setProperty(keys::maximum, number(*stored, keys::maximum));
        assignment->setProperty(keys::amount, number(*stored, keys::amount));
        assignment->setProperty(keys::enabled, static_cast<bool>(stored->getProperty(keys::enabled)));
        assignments.add(cydaw::base::Value(assignment.get()));
    }
    return assignments;
}

// Clamped and in time order, as v1 holds them.
cydaw::base::Value readLauncherActions(const cydaw::base::Value& saved)
{
    struct Action {
        double time {};
        int type {};
        std::int64_t track {};
        int scene {};
    };
    std::vector<Action> actions;
    if (const auto* values = saved.getArray())
        for (const auto& value : *values)
            if (const auto* action = value.getDynamicObject())
                actions.push_back({ std::max(0.0, number(*action, keys::time)),
                                    cydaw::base::limit(0, 2, static_cast<int>(action->getProperty(keys::type))),
                                    std::max<std::int64_t>(0, static_cast<std::int64_t>(action->getProperty(keys::track))),
                                    static_cast<int>(action->getProperty(keys::scene)) });
    std::stable_sort(actions.begin(), actions.end(),
                     [](const Action& first, const Action& second) { return first.time < second.time; });
    cydaw::base::Array<cydaw::base::Value> values;
    for (const auto& action : actions) {
        auto value = makeObject();
        value->setProperty(keys::time, action.time);
        value->setProperty(keys::type, action.type);
        value->setProperty(keys::track, action.track);
        value->setProperty(keys::scene, action.scene);
        values.add(cydaw::base::Value(value.get()));
    }
    return values;
}

void restoreAudioClip(Engine& engine, const std::size_t trackIndex, const cydaw::base::Object& clip,
                      const cydaw::base::File& trackFile, const cydaw::base::File& projectFile, cydaw::base::TextList& warnings)
{
    const auto clipSource = resolveAsset(clip.getProperty(keys::source).toString(), projectFile, trackFile);
    if (!clipSource.existsAsFile()) {
        warn(warnings, missingAudio(clipSource));
        return;
    }
    const auto startSeconds = number(clip, keys::start);
    const auto offsetSeconds = number(clip, keys::offset);
    const auto durationSeconds = number(clip, keys::duration);
    const auto fadeInSeconds = number(clip, keys::fadeIn);
    const auto fadeOutSeconds = number(clip, keys::fadeOut);
    const auto clipId = engine.addAudioClip(trackIndex, startSeconds, offsetSeconds, durationSeconds,
                                            clip.getProperty(keys::name).toString(), colourOf(clip), clipSource,
                                            fadeInSeconds, fadeOutSeconds);
    if (clipId != 0) {
        (void) engine.updateAudioClip(trackIndex, clipId, startSeconds, offsetSeconds, durationSeconds, fadeInSeconds,
                                      fadeOutSeconds, numberOr(clip, keys::sourceDuration, durationSeconds));
        const auto stretchMode = cydaw::base::limit(0, Engine::stretchModeCount - 1, clip.hasProperty(keys::stretchMode)
                                                        ? static_cast<int>(clip.getProperty(keys::stretchMode))
                                                        : 0);
        (void) engine.setAudioClipStretchSettings(trackIndex, clipId, numberOr(clip, keys::sourceTempo, 0.0),
                                                  static_cast<Engine::StretchMode>(stretchMode));
        if (const auto* markers = clip.getProperty(keys::stretchMarkers).getArray())
            for (const auto& markerValue : *markers)
                if (const auto* marker = markerValue.getDynamicObject())
                    (void) engine.addAudioClipStretchMarker(trackIndex, clipId, number(*marker, keys::source),
                                                            number(*marker, keys::timeline));
    }

    const auto* savedTakes = clip.getProperty(keys::takes).getArray();
    if (clipId != 0 && savedTakes != nullptr && !savedTakes->isEmpty()) {
        std::vector<Engine::AudioTake> takes;
        takes.reserve(static_cast<std::size_t>(savedTakes->size()));
        auto allTakesAvailable = true;
        for (const auto& takeValue : *savedTakes) {
            const auto* take = takeValue.getDynamicObject();
            if (take == nullptr) {
                allTakesAvailable = false;
                break;
            }
            const auto takeFile = resolveAsset(take->getProperty(keys::source).toString(), projectFile);
            if (!takeFile.existsAsFile()) {
                warn(warnings, missingAudio(takeFile));
                allTakesAvailable = false;
                break;
            }
            takes.push_back({ take->getProperty(keys::name).toString(), takeFile, 0.0, numberOr(*take, "shift", 0.0) });
        }
        if (allTakesAvailable && !takes.empty()) {
            const auto activeTake = static_cast<std::size_t>(cydaw::base::limit<std::int64_t>(
                0, static_cast<std::int64_t>(takes.size() - 1),
                static_cast<std::int64_t>(clip.getProperty(keys::activeTake))));
            if (const auto replaced = engine.replaceAudioTakes(trackIndex, clipId, std::move(takes), activeTake);
                replaced.failed())
                warn(warnings, "Audio takes could not be restored: " + replaced.getErrorMessage());
        }
    }
    if (clipId == 0)
        return;
    if (const auto pitch = numberOr(clip, "pitch", 0.0); pitch != 0.0)
        (void) engine.setAudioClipPitch(trackIndex, clipId, pitch);
    if (const auto* comp = clip.getProperty("comp").getArray()) {
        std::vector<Engine::CompSegment> parts;
        for (const auto& part : *comp)
            if (const auto* object = part.getDynamicObject())
                parts.push_back({ number(*object, keys::start), numberOr(*object, "end", 0.0),
                                  static_cast<std::size_t>(std::max<std::int64_t>(0, static_cast<std::int64_t>(object->getProperty("take")))) });
        (void) engine.setAudioClipComp(trackIndex, clipId, std::move(parts));
    }
    if (const auto* versions = clip.getProperty("versions").getArray()) {
        std::vector<Engine::SourceVersion> history;
        for (const auto& version : *versions)
            if (const auto* object = version.getDynamicObject()) {
                const auto file = resolveAsset(object->getProperty(keys::source).toString(), projectFile);
                if (!file.existsAsFile())
                    continue; // an edit's file gone: that version too
                history.push_back({ object->getProperty(keys::label).toString(), file, number(*object, keys::offset),
                                    number(*object, keys::duration), number(*object, keys::sourceDuration),
                                    numberOr(*object, "sourceLength", 0.0) });
            }
        engine.restoreAudioClipHistory(trackIndex, clipId, std::move(history),
                                       static_cast<std::size_t>(std::max<std::int64_t>(0, static_cast<std::int64_t>(clip.getProperty("version")))));
    }

    // A clip whose sounds were all removed stays empty, not its source again.
    if (const auto* savedEvents = clip.getProperty(keys::events).getArray();
        savedEvents != nullptr && savedEvents->isEmpty() && static_cast<bool>(clip.getProperty(keys::eventTimeline)))
        (void) engine.replaceAudioClipEvents(trackIndex, clipId, {});
    if (const auto* savedEvents = clip.getProperty(keys::events).getArray();
        savedEvents != nullptr && !savedEvents->isEmpty()) {
        std::vector<Engine::AudioEvent> events;
        if (readEvents(*savedEvents, clipSource, projectFile, warnings, events) && !events.empty())
            if (const auto replaced = engine.replaceAudioClipEvents(trackIndex, clipId, std::move(events));
                replaced.failed())
                warn(warnings, "Audio events could not be restored: " + replaced.getErrorMessage());
    }
    const auto playStart = numberOr(clip, keys::playStart, 0.0);
    const auto playEnd = numberOr(clip, keys::playEnd, durationSeconds);
    const auto loopStart = numberOr(clip, keys::loopStart, playStart);
    const auto loopEnd = numberOr(clip, keys::loopEnd, playEnd);
    (void) engine.setAudioClipTimelineBounds(trackIndex, clipId, durationSeconds, playStart, playEnd, loopStart, loopEnd,
                                             static_cast<bool>(clip.getProperty(keys::loop)));
    restoreGainPoints(
        clip.getProperty(keys::gainAutomation),
        [&](const double seconds, const float valueDb) {
            return engine.addAudioClipAutomationPoint(trackIndex, clipId, seconds, valueDb);
        },
        [&](const std::uint64_t pointId, const Engine::AutomationCurve curve, const float amount) {
            (void) engine.setAudioClipAutomationPointCurve(trackIndex, clipId, pointId, curve, amount);
        });
    if (auto lanes = readClipLanes(clip); !lanes.empty())
        (void) engine.setAudioClipLanes(trackIndex, clipId, std::move(lanes));
}

void restorePlugin(Engine& engine, const std::size_t trackIndex, const cydaw::base::Text& path,
                   const cydaw::base::Text& stateBase64, const cydaw::base::Value& automationLanes, const cydaw::base::File& projectFile,
                   cydaw::base::TextList& warnings)
{
    if (path.isEmpty())
        return;
    // A plugin may be a bundle, a folder.
    const auto pluginFile = resolveAsset(path, projectFile);
    if (!pluginFile.exists()) {
        warn(warnings, "Missing plugin: " + pluginFile.getFullPathName());
        return;
    }
    std::size_t pluginIndex {};
    if (const auto inserted = engine.setTrackPlugin(trackIndex, pluginFile, &pluginIndex); inserted.failed()) {
        warn(warnings, "Plugin could not be loaded (" + pluginFile.getFileName() + "): " + inserted.getErrorMessage());
        return;
    }
    if (stateBase64.isNotEmpty()) {
        cydaw::base::MemoryBlock pluginState;
        if (cydaw::base::decodeLegacyStateBytes(stateBase64, pluginState))
            if (const auto restored = engine.setTrackPluginState(trackIndex, pluginIndex, pluginState);
                restored.failed())
                warn(warnings, "Plugin state could not be restored (" + pluginFile.getFileName()
                                   + "): " + restored.getErrorMessage());
    }
    const auto* lanes = automationLanes.getArray();
    if (lanes == nullptr)
        return;
    for (const auto& laneValue : *lanes) {
        const auto* lane = laneValue.getDynamicObject();
        if (lane == nullptr)
            continue;
        const auto parameterIndex = static_cast<std::uint32_t>(static_cast<std::int64_t>(lane->getProperty(keys::parameter)));
        const auto parameterName = lane->getProperty(keys::name).toString();
        const auto* points = lane->getProperty(keys::points).getArray();
        if (points == nullptr)
            continue;
        std::vector<Engine::PluginAutomationPoint> restored;
        for (const auto& pointValue : *points)
            if (const auto* point = pointValue.getDynamicObject())
                restored.push_back({ 0, number(*point, keys::time), toFloat(point->getProperty(keys::value)), curveOf(*point),
                                     curveAmountOf(*point), handlesOf(*point) });
        if (!restored.empty())
            engine.replacePluginAutomationPoints(trackIndex, pluginIndex, parameterIndex, parameterName, std::move(restored));
    }
}

void restoreLauncherClip(Engine& engine, const std::size_t trackIndex, const cydaw::base::Object& clip,
                         const cydaw::base::File& trackFile, const cydaw::base::File& projectFile, cydaw::base::TextList& warnings)
{
    const auto sceneIndex = static_cast<int>(clip.getProperty(keys::scene));
    const auto clipName = clip.getProperty(keys::name).toString();
    const auto durationSeconds = std::max(0.001, number(clip, keys::duration));
    const auto looping = static_cast<bool>(clip.getProperty(keys::loop));
    const auto clipColour = colourOf(clip);
    const auto clipSource = resolveAsset(clip.getProperty(keys::source).toString(), projectFile, trackFile);
    const auto* savedEvents = clip.getProperty(keys::events).getArray();
    const auto usesTimeline = static_cast<bool>(clip.getProperty(keys::eventTimeline)) || savedEvents != nullptr;
    auto created = false;
    if (usesTimeline)
        created = engine.createEmptyLauncherClip(trackIndex, sceneIndex, clipName, durationSeconds, clipColour);
    else if (clipSource.existsAsFile())
        created = engine.setLauncherClip(trackIndex, sceneIndex, clipName, number(clip, keys::offset), durationSeconds,
                                         looping, clipColour, clipSource);
    else
        warn(warnings, missingAudio(clipSource));
    if (!created)
        return;

    const auto playStart = numberOr(clip, keys::playStart, 0.0);
    const auto playEnd = numberOr(clip, keys::playEnd, durationSeconds);
    const auto loopStart = numberOr(clip, keys::loopStart, playStart);
    const auto loopEnd = numberOr(clip, keys::loopEnd, playEnd);
    (void) engine.setLauncherClipTimelineBounds(trackIndex, sceneIndex, durationSeconds, playStart, playEnd, loopStart,
                                                loopEnd, looping);
    restoreGainPoints(
        clip.getProperty(keys::gainAutomation),
        [&](const double seconds, const float valueDb) {
            return engine.addLauncherClipAutomationPoint(trackIndex, sceneIndex, seconds, valueDb);
        },
        [&](const std::uint64_t pointId, const Engine::AutomationCurve curve, const float amount) {
            (void) engine.setLauncherClipAutomationPointCurve(trackIndex, sceneIndex, pointId, curve, amount);
        });
    if (auto lanes = readClipLanes(clip); !lanes.empty())
        (void) engine.setLauncherClipLanes(trackIndex, sceneIndex, std::move(lanes));
    if (!usesTimeline || savedEvents == nullptr)
        return;
    std::vector<Engine::AudioEvent> events;
    if (readEvents(*savedEvents, clipSource, projectFile, warnings, events))
        if (const auto replaced = engine.replaceLauncherClipEvents(trackIndex, sceneIndex, std::move(events));
            replaced.failed())
            warn(warnings, "Launcher clip events could not be restored: " + replaced.getErrorMessage());
}

// One entry of "tracks", or the root of the original single-track format.
struct SavedAudioTrack {
    cydaw::base::Text audioPath;
    cydaw::base::Text name;
    std::int64_t colour {};
    int channels { 2 };
    float gainDb {};
    float pan {};
    bool muted {};
    bool solo {};
    bool armed {};
    double clipStart {};
    double sourceOffset {};
    double clipDuration {};
    float driveDb {};
    float filterCutoffHz { 20'000.0f };
    cydaw::base::Value plugins;
    cydaw::base::Text legacyPluginPath;
    cydaw::base::Text legacyPluginState;
    cydaw::base::Value automation;
    cydaw::base::Value audioClips;
    cydaw::base::Value launcherClips;
    cydaw::base::Value nodeGraph;
    cydaw::base::Value modulationGraph;
    cydaw::base::Value modulationAssignments;
    // The track itself, for its automation lanes.
    cydaw::base::Value parameterLanes;
};

SavedAudioTrack readSavedTrack(const cydaw::base::Object& track)
{
    SavedAudioTrack saved;
    saved.audioPath = track.getProperty(keys::audioFile).toString();
    saved.name = track.getProperty(keys::name).toString();
    saved.colour = static_cast<std::int64_t>(track.getProperty(keys::colour));
    saved.channels = track.hasProperty(keys::audioChannels) ? static_cast<int>(track.getProperty(keys::audioChannels)) : 2;
    saved.gainDb = toFloat(track.getProperty(keys::gainDb));
    saved.pan = toFloat(track.getProperty(keys::pan));
    saved.muted = static_cast<bool>(track.getProperty(keys::mute));
    saved.solo = static_cast<bool>(track.getProperty(keys::solo));
    saved.armed = static_cast<bool>(track.getProperty(keys::armed));
    saved.clipStart = number(track, keys::clipStart);
    saved.sourceOffset = number(track, keys::sourceOffset);
    saved.clipDuration = number(track, keys::clipDuration);
    saved.driveDb = toFloat(track.getProperty(keys::driveDb));
    saved.filterCutoffHz = toFloat(track.getProperty(keys::filterCutoffHz));
    saved.plugins = track.getProperty(keys::plugins);
    saved.legacyPluginPath = track.getProperty(keys::pluginFile).toString();
    saved.legacyPluginState = track.getProperty(keys::pluginState).toString();
    saved.automation = track.getProperty(keys::gainAutomation);
    saved.audioClips = track.getProperty(keys::audioClips);
    saved.launcherClips = track.getProperty(keys::launcherClips);
    saved.nodeGraph = track.getProperty(keys::nodeGraph);
    saved.modulationGraph = track.getProperty(keys::modulationGraph);
    saved.modulationAssignments = track.getProperty(keys::modulationAssignments);
    saved.parameterLanes = cydaw::base::Value(const_cast<cydaw::base::Object*>(&track));
    return saved;
}

// v1's addSavedTrack. Returns the engine index of the track made.
std::optional<std::size_t> restoreAudioTrack(Studio& studio, const SavedAudioTrack& saved,
                                             const cydaw::base::Value& legacyNodeGraph, const cydaw::base::File& projectFile,
                                             cydaw::base::TextList& warnings)
{
    auto& engine = studio.engine();
    const auto audioFile = resolveAsset(saved.audioPath, projectFile);
    // v1 dropped a track whose file was missing; it is kept, empty, so its
    // mixer, plugins, graph and index survive.
    auto useFile = saved.audioPath.isNotEmpty();
    if (useFile && !audioFile.existsAsFile()) {
        warn(warnings, missingAudio(audioFile));
        useFile = false;
    }
    std::size_t trackIndex {};
    auto created = useFile ? engine.addAudioTrack(audioFile, &trackIndex) : engine.addEmptyAudioTrack(&trackIndex);
    if (created.failed() && useFile) {
        warn(warnings, "Audio file could not be opened (" + audioFile.getFullPathName() + "): " + created.getErrorMessage());
        created = engine.addEmptyAudioTrack(&trackIndex);
    }
    if (created.failed()) {
        warn(warnings, "Audio track \"" + saved.name + "\" could not be created: " + created.getErrorMessage());
        return std::nullopt;
    }

    // The graph: the track's own, else the project's legacy one; the early
    // mixed default graph gives way to the factory graph.
    auto graphState = isNodeGraphState(saved.nodeGraph) ? saved.nodeGraph : legacyNodeGraph;
    if (isLegacyMixedFactoryAudioGraph(graphState)) {
        Studio::mergeModules(studio.sharedNodeModules(), graphState);
        graphState = cydaw::base::Value();
    }
    if (graphState.getDynamicObject() == nullptr)
        graphState = cydaw::base::Value();
    {
        auto& state = studio.adoptAudioTrack(trackIndex, graphState);
        if (saved.name.trim().isNotEmpty())
            state.name = saved.name.trim();
        else
            state.name = "Audio " + cydaw::base::Text(static_cast<int>(trackIndex + 1));
        if (saved.colour != 0)
            state.colour = static_cast<std::uint32_t>(saved.colour);
        state.audioChannels = cydaw::base::limit(1, 8, saved.channels > 0 ? saved.channels : 2);
        // Its format before its clips: their decks are made in it once.
        if (state.audioChannels != engine.trackChannelCount(trackIndex))
            if (const auto formatted = engine.setTrackChannelCount(trackIndex, state.audioChannels); formatted.failed())
                warn(warnings, "Audio track \"" + state.name + "\" keeps its format: " + formatted.getErrorMessage());
        state.audioChannels = engine.trackChannelCount(trackIndex);
    }

    engine.setTrackMixer(trackIndex, saved.gainDb, saved.pan, saved.muted);
    engine.setTrackSolo(trackIndex, saved.solo);
    if (saved.armed)
        engine.setTrackArmed(trackIndex, true);
    if (const auto* clips = saved.audioClips.getArray()) {
        for (const auto& clip : engine.audioClips(trackIndex))
            (void) engine.removeAudioClip(trackIndex, clip.id);
        for (const auto& clipValue : *clips)
            if (const auto* clip = clipValue.getDynamicObject())
                restoreAudioClip(engine, trackIndex, *clip, audioFile, projectFile, warnings);
    } else {
        // The single clip of projects older than multi-clip tracks.
        const auto info = engine.trackInfo(trackIndex);
        const auto safeDuration = saved.clipDuration > 0.0
                                      ? saved.clipDuration
                                      : std::max(0.001, info.sourceLengthSeconds - saved.sourceOffset);
        engine.setTrackClip(trackIndex, saved.clipStart, saved.sourceOffset, safeDuration);
    }
    engine.clearArrangementEditHistory(trackIndex);
    engine.setTrackEffects(trackIndex, saved.driveDb, saved.filterCutoffHz > 0.0f ? saved.filterCutoffHz : 20'000.0f);

    if (const auto* plugins = saved.plugins.getArray(); plugins != nullptr && !plugins->isEmpty()) {
        for (const auto& pluginValue : *plugins)
            if (const auto* plugin = pluginValue.getDynamicObject())
                restorePlugin(engine, trackIndex, plugin->getProperty(keys::file).toString(),
                              plugin->getProperty(keys::state).toString(), plugin->getProperty(keys::automation),
                              projectFile, warnings);
    } else {
        restorePlugin(engine, trackIndex, saved.legacyPluginPath, saved.legacyPluginState, {}, projectFile, warnings);
    }
    if (auto points = readGainPoints(saved.automation); !points.empty())
        engine.replaceAutomationPoints(trackIndex, std::move(points));
    if (const auto* clips = saved.launcherClips.getArray())
        for (const auto& clipValue : *clips)
            if (const auto* clip = clipValue.getDynamicObject())
                restoreLauncherClip(engine, trackIndex, *clip, audioFile, projectFile, warnings);

    // The modulation graph's classes join the library; the track keeps its own.
    auto& state = studio.audioStates()[trackIndex];
    if (saved.modulationGraph.getDynamicObject() != nullptr) {
        Studio::mergeModules(studio.sharedModulationModules(), saved.modulationGraph);
        state.modulationGraph = Studio::withoutSharedModules(app::cloneJsonValue(saved.modulationGraph));
        state.modulationSlot.reset(); // made again from it when needed
    }
    state.modulationAssignments = readAssignments(saved.modulationAssignments);
    // Each loaded plug-in's pinned knobs (a plug-in that did not load is
    // skipped: the next saved one is matched by its file).
    state.pluginPins.clear();
    if (const auto* plugins = saved.plugins.getArray()) {
        const auto loaded = engine.trackPlugins(trackIndex);
        auto savedAt = 0;
        for (std::size_t at = 0; at < loaded.size(); ++at) {
            std::optional<std::vector<std::uint32_t>> pins;
            for (; savedAt < plugins->size(); ++savedAt) {
                const auto& entry = (*plugins)[savedAt];
                if (resolveAsset(entry.getProperty(keys::file, {}).toString(), projectFile).getFileName() != loaded[at].file.getFileName())
                    continue;
                if (const auto* pinned = entry.getProperty("pinned", {}).getArray()) {
                    std::vector<std::uint32_t> list;
                    for (const auto& pin : *pinned)
                        list.push_back(static_cast<std::uint32_t>(static_cast<int>(pin)));
                    pins = std::move(list);
                }
                ++savedAt;
                break;
            }
            state.pluginPins.push_back(std::move(pins));
        }
    }
    if (const auto* object = saved.parameterLanes.getDynamicObject())
        readParameterLanes(*object, state.parameterLanes);
    return trackIndex;
}

// The virtual buses of a list, their graphs (with their classes) beside.
std::vector<app::VirtualAudioBus> readBuses(Studio& studio, const cydaw::base::Value& list, std::map<std::uint64_t, cydaw::base::Value>& graphs)
{
    std::vector<app::VirtualAudioBus> restored;
    if (const auto* buses = list.getArray()) {
        restored.reserve(static_cast<std::size_t>(buses->size()));
        for (const auto& value : *buses) {
            const auto* saved = value.getDynamicObject();
            if (saved == nullptr)
                continue;
            app::VirtualAudioBus bus;
            bus.id = static_cast<std::uint64_t>(std::max<std::int64_t>(0, static_cast<std::int64_t>(saved->getProperty(keys::id))));
            if (bus.id == 0)
                continue;
            bus.name = saved->getProperty(keys::name).toString();
            bus.direction = static_cast<app::VirtualBusDirection>(
                cydaw::base::limit(0, 1, static_cast<int>(saved->getProperty(keys::direction))));
            bus.channels = cydaw::base::limit(1, app::VirtualAudioBus::maximumChannels, static_cast<int>(saved->getProperty(keys::channels)));
            if (const auto* more = saved->getProperty("moreChannels").getArray())
                for (int index = 0; index < std::min(more->size(), static_cast<int>(bus.moreChannels.size())); ++index)
                    bus.moreChannels[static_cast<std::size_t>(index)] = std::max(0, static_cast<int>((*more)[index]));
            bus.carriesMaster = !saved->hasProperty("carriesMaster") || static_cast<bool>(saved->getProperty("carriesMaster"));
            bus.physicalChannelA = std::max(0, static_cast<int>(saved->getProperty(keys::physicalA)));
            bus.physicalChannelB = std::max(0, static_cast<int>(saved->getProperty(keys::physicalB)));
            bus.destinationChannelA = std::max(0, static_cast<int>(saved->getProperty(keys::destinationA)));
            bus.destinationChannelB = std::max(0, static_cast<int>(saved->getProperty(keys::destinationB)));
            bus.gainDb = cydaw::base::limit(-60.0f, 24.0f, toFloat(saved->getProperty(keys::gainDb)));
            bus.muted = static_cast<bool>(saved->getProperty(keys::muted));
            bus.monitor = static_cast<bool>(saved->getProperty(keys::monitor));
            bus.exposeToSystem = static_cast<bool>(saved->getProperty(keys::exposeToSystem));
            const auto graph = saved->getProperty(keys::nodeGraph);
            if (isNodeGraphState(graph)) {
                // The file's classes join the library; the bus keeps its own.
                Studio::mergeModules(studio.sharedNodeModules(), graph);
                graphs[bus.id] = Studio::withoutSharedModules(app::cloneJsonValue(graph));
            }
            restored.push_back(std::move(bus));
        }
    }
    return restored;
}

// Every bus with its graph; `withClasses`: the classes its graph uses kept
// in it (a file of its own), rather than in the document's library.
cydaw::base::Value busesToVar(Studio& studio, const std::map<std::uint64_t, cydaw::base::Value>& busGraphs, const bool withClasses)
{
    auto& engine = studio.engine();
    cydaw::base::Array<cydaw::base::Value> buses;
    for (const auto& bus : engine.virtualBuses()) {
        auto value = makeObject();
        value->setProperty(keys::id, static_cast<std::int64_t>(bus.id));
        value->setProperty(keys::name, bus.name);
        value->setProperty(keys::direction, static_cast<int>(bus.direction));
        value->setProperty(keys::channels, bus.channels);
        value->setProperty(keys::physicalA, bus.physicalChannelA);
        value->setProperty(keys::physicalB, bus.physicalChannelB);
        value->setProperty(keys::destinationA, bus.destinationChannelA);
        value->setProperty(keys::destinationB, bus.destinationChannelB);
        if (bus.channels > 2) {
            cydaw::base::Array<cydaw::base::Value> more;
            for (const auto channel : bus.moreChannels)
                more.add(channel);
            value->setProperty("moreChannels", more);
        }
        if (!bus.carriesMaster)
            value->setProperty("carriesMaster", false);
        value->setProperty(keys::gainDb, bus.gainDb);
        value->setProperty(keys::muted, bus.muted);
        value->setProperty(keys::monitor, bus.monitor);
        const auto& graph = graphOr(busGraphs, bus.id, studio.factoryBusGraph());
        value->setProperty(keys::nodeGraph, withClasses ? app::cloneJsonValue(graph) : Studio::withoutSharedModules(graph));
        value->setProperty(keys::exposeToSystem, bus.exposeToSystem);
        buses.add(cydaw::base::Value(value.get()));
    }
    return buses;
}

void restoreMidiClip(Engine& engine, const std::uint64_t trackId, const cydaw::base::Object& clip)
{
    const auto length = std::max(0.25, number(clip, keys::lengthBeats));
    const auto looping = static_cast<bool>(clip.getProperty(keys::loop));
    const auto clipId = engine.addMidiClip(trackId, clip.getProperty(keys::name).toString(),
                                           number(clip, keys::startBeats), length, looping, colourOf(clip),
                                           clip.hasProperty(keys::launcherScene) ? static_cast<int>(clip.getProperty(keys::launcherScene))
                                                                                 : -1);
    if (clipId == 0)
        return;
    const auto playStart = numberOr(clip, keys::playStartBeats, 0.0);
    const auto playEnd = numberOr(clip, keys::playEndBeats, length);
    const auto loopStart = numberOr(clip, keys::loopStartBeats, playStart);
    const auto loopEnd = numberOr(clip, keys::loopEndBeats, playEnd);
    (void) engine.setMidiClipTimelineBounds(trackId, clipId, length, playStart, playEnd, loopStart, loopEnd, looping);
    if (const auto* notes = clip.getProperty(keys::notes).getArray())
        for (const auto& noteValue : *notes)
            if (const auto* note = noteValue.getDynamicObject())
            {
                const auto noteId = engine.addMidiNote(trackId, clipId, static_cast<int>(note->getProperty(keys::note)),
                                                       number(*note, keys::startBeats), number(*note, keys::durationBeats),
                                                       toFloat(note->getProperty(keys::velocity)));
                if (noteId != 0 && (note->hasProperty(keys::pitch) || note->hasProperty(keys::gain)))
                    (void) engine.setMidiNoteExpression(trackId, clipId, noteId, expressionFromVar(note->getProperty(keys::pitch)),
                                                        expressionFromVar(note->getProperty(keys::gain)));
                // Its timbre, pressure, pan and chance.
                using Curve = Engine::NoteCurve;
                for (const auto& [name, curve] : { std::pair { "timbre", Curve::timbre }, std::pair { "pressure", Curve::pressure },
                                                   std::pair { "pan", Curve::pan } })
                    if (noteId != 0 && note->hasProperty(name))
                        (void) engine.setMidiNoteCurve(trackId, clipId, noteId, curve, expressionFromVar(note->getProperty(name)));
                if (noteId != 0 && note->hasProperty("chance"))
                    (void) engine.setMidiNoteChance(trackId, clipId, noteId, toFloat(note->getProperty("chance")));
            }
    restoreGainPoints(
        clip.getProperty(keys::gainAutomation),
        [&](const double seconds, const float valueDb) {
            return engine.addMidiClipAutomationPoint(trackId, clipId, seconds, valueDb);
        },
        [&](const std::uint64_t pointId, const Engine::AutomationCurve curve, const float amount) {
            (void) engine.setMidiClipAutomationPointCurve(trackId, clipId, pointId, curve, amount);
        });
}

// Returns the armed track's id, 0 when none is.
// One MIDI track of the file; its id when it is armed, else 0.
std::uint64_t restoreMidiTrack(Studio& studio, const cydaw::base::Object& saved, cydaw::base::TextList& warnings)
{
    auto& engine = studio.engine();
    const auto trackName = saved.getProperty(keys::name).toString();
    const auto trackId = engine.addMidiTrack(trackName);
    if (trackId == 0) {
        warn(warnings, "MIDI track \"" + trackName + "\" could not be created");
        return 0;
    }
    // The engine gives a new track a first clip; the file lists them all.
    if (const auto* clips = engine.midiClipsView(trackId)) {
        const auto first = *clips;
        for (const auto& clip : first)
            (void) engine.removeMidiClip(trackId, clip.id);
    }
    if (const auto* clips = saved.getProperty(keys::clips).getArray())
        for (const auto& clipValue : *clips)
            if (const auto* clip = clipValue.getDynamicObject())
                restoreMidiClip(engine, trackId, *clip);
    engine.setMidiTrackMixer(trackId, toFloat(saved.getProperty(keys::gainDb)), toFloat(saved.getProperty(keys::pan)),
                             static_cast<bool>(saved.getProperty(keys::mute)), static_cast<bool>(saved.getProperty(keys::solo)));
    // What it hears (kept as it was before it was saved: every keyboard
    // and the computer's), where its notes also go.
    if (const auto* inputs = saved.getProperty("midiInputs").getArray()) {
        std::vector<cydaw::base::Text> heard;
        for (const auto& input : *inputs)
            heard.push_back(input.toString());
        engine.setMidiTrackInputs(trackId, std::move(heard));
    }
    if (const auto output = saved.getProperty("midiOutput").toString(); output.isNotEmpty())
        engine.setMidiTrackOutput(trackId, output);
    if (auto points = readGainPoints(saved.getProperty(keys::gainAutomation)); !points.empty())
        engine.replaceMidiAutomationPoints(trackId, std::move(points));
    const auto graph = saved.getProperty(keys::nodeGraph);
    auto& state = studio.adoptMidiTrack(trackId, graph.getDynamicObject() != nullptr ? graph : cydaw::base::Value());
    // CyDAW 2's own keys; CyDAW 1 ignores them.
    if (const auto savedColour = colourOf(saved); savedColour != 0)
        state.colour = savedColour;
    if (saved.hasProperty(keys::laneHeight))
        state.laneHeight = cydaw::base::limit(40, 420, static_cast<int>(saved.getProperty(keys::laneHeight)));
    state.automationVisible = static_cast<bool>(saved.getProperty(keys::automationVisible));
    readParameterLanes(saved, state.parameterLanes);
    if (const auto pads = saved.getProperty("rackPads"); pads.isArray())
        studio.restoreRackPads(TrackRef { true, 0, trackId }, pads, saved.getProperty("padsKind").toString());
    // Every armed track armed again (CyDAW 2 arms several).
    if (static_cast<bool>(saved.getProperty(keys::armed))) {
        (void) studio.engine().setMidiTrackArmed(trackId, true);
        return trackId;
    }
    return 0;
}

// A file without MIDI tracks: older than them, one instrument holding the
// notes it kept; newer, none.
std::uint64_t restoreLegacyMidi(Studio& studio, const cydaw::base::Object& root)
{
    auto& engine = studio.engine();
    const auto* legacyNotes = root.getProperty(keys::midiNotes).getArray();
    if (root.hasProperty(keys::midiTracks) && (legacyNotes == nullptr || legacyNotes->isEmpty()))
        return 0;
    const auto trackId = engine.addMidiTrack("Instrument 1");
    if (trackId == 0)
        return 0;
    if (legacyNotes != nullptr)
        for (const auto& value : *legacyNotes)
            if (const auto* note = value.getDynamicObject())
                (void) engine.addMidiNote(static_cast<int>(note->getProperty(keys::note)),
                                          number(*note, keys::startBeats), number(*note, keys::durationBeats),
                                          toFloat(note->getProperty(keys::velocity)));
    (void) studio.adoptMidiTrack(trackId, cydaw::base::Value());
    return trackId;
}

// Values v1 applied to its views, kept for the interface under the same keys.
void restoreInterfaceState(Studio& studio, const cydaw::base::Object& root)
{
    if (studio.interfaceState.getDynamicObject() == nullptr)
        studio.interfaceState = cydaw::base::Value(new cydaw::base::Object());
    auto& ui = *studio.interfaceState.getDynamicObject();
    const auto integerOr = [&root](const cydaw::base::Identifier& property, const int fallback) {
        return root.hasProperty(property) ? static_cast<int>(root.getProperty(property)) : fallback;
    };
    ui.setProperty(keys::arrangementSnapMode, cydaw::base::limit(1, 5, integerOr(keys::arrangementSnapMode, 4)));
    ui.setProperty(keys::arrangementSnapType, cydaw::base::limit(1, 3, integerOr(keys::arrangementSnapType, 1)));
    ui.setProperty(keys::arrangementTool, cydaw::base::limit(1, 5, integerOr(keys::arrangementTool, 1)));
    ui.setProperty(keys::launcherActionReplay, root.hasProperty(keys::launcherActionReplay)
                                                   && static_cast<bool>(root.getProperty(keys::launcherActionReplay)));
    ui.setProperty(keys::launcherActions, readLauncherActions(root.getProperty(keys::launcherActions)));
    auto editorMode = static_cast<int>(root.getProperty(keys::editorMode));
    if (editorMode == 0)
        editorMode = static_cast<bool>(root.getProperty(keys::midiEditorVisible)) ? 2 : 1;
    ui.setProperty(keys::editorMode, editorMode);
    // The engine has no getter for its loop switch: the file's is kept here.
    ui.setProperty(keys::loop, static_cast<bool>(root.getProperty(keys::loop)));
    if (root.hasProperty(keys::workspaceLayout))
        ui.setProperty(keys::workspaceLayout, root.getProperty(keys::workspaceLayout));
    else
        ui.removeProperty(keys::workspaceLayout);
}

} // namespace

// ---- Saving ----------------------------------------------------------------------------------

cydaw::base::Value buildDocument(Studio& studio, const cydaw::base::File& projectFile, const bool refreshPluginState)
{
    auto& engine = studio.engine();

    // Every live graph serialised once. A class edited in one of them joins
    // the library first, so the library written below carries the edit.
    const auto nodeLibrary = modulesByKey(studio.sharedNodeModules());
    const auto absorb = [&studio, &nodeLibrary](const cydaw::base::Value& graphState) {
        Studio::mergeModules(studio.sharedNodeModules(), editedModules(graphState, nodeLibrary));
    };
    std::vector<cydaw::base::Value> trackGraphs(engine.trackCount());
    for (std::size_t index = 0; index < trackGraphs.size(); ++index)
        if (const auto* slot = studio.graphFor(TrackRef { false, index, 0 })) {
            trackGraphs[index] = slot->serialise();
            absorb(trackGraphs[index]);
        }
    std::map<std::uint64_t, cydaw::base::Value> midiGraphs;
    for (const auto& [trackId, midiState] : studio.midiStates())
        if (midiState.graph != nullptr) {
            midiGraphs[trackId] = midiState.graph->serialise();
            absorb(midiGraphs[trackId]);
        }
    cydaw::base::Value masterGraph;
    if (const auto* slot = studio.graphFor(Studio::masterTrack())) {
        masterGraph = slot->serialise();
        absorb(masterGraph);
    }
    std::map<std::uint64_t, cydaw::base::Value> busGraphs;
    for (const auto& [busId, busState] : studio.busStates())
        if (busState.graph != nullptr) {
            busGraphs[busId] = busState.graph->serialise();
            absorb(busGraphs[busId]);
        }
    const auto modulationLibrary = modulesByKey(studio.sharedModulationModules());
    for (const auto& audioState : studio.audioStates())
        Studio::mergeModules(studio.sharedModulationModules(),
                             editedModules(audioState.modulationGraph, modulationLibrary));

    auto document = makeObject();
    document->setProperty(keys::format, "CyDAW Project");
    document->setProperty(keys::version, documentVersion);

    // The software's virtual buses as they are: a project names them (a
    // track's input, an output sent to), it does not bring them back.
    document->setProperty(keys::virtualBuses, busesToVar(studio, busGraphs, false));

    cydaw::base::Array<cydaw::base::Value> tracks;
    auto& audioStates = studio.audioStates();
    for (std::size_t index = 0; index < engine.trackCount(); ++index) {
        const auto info = engine.trackInfo(index);
        const auto* state = index < audioStates.size() ? &audioStates[index] : nullptr;
        auto track = makeObject();
        track->setProperty(keys::audioFile, projectAssetPath(info.file, projectFile));
        track->setProperty(keys::name, state != nullptr && state->name.isNotEmpty()
                                           ? state->name
                                           : "Audio " + cydaw::base::Text(static_cast<int>(index + 1)));
        track->setProperty(keys::colour, static_cast<std::int64_t>(
                                             state != nullptr ? state->colour
                                                              : studio.trackColour(TrackRef { false, index, 0 })));
        track->setProperty(keys::audioChannels, state != nullptr ? state->audioChannels : 2);
        track->setProperty(keys::gainDb, info.gainDb);
        track->setProperty(keys::pan, info.pan);
        track->setProperty(keys::mute, info.muted);
        track->setProperty(keys::solo, info.solo);
        track->setProperty(keys::armed, info.armed);
        track->setProperty(keys::inputBusId, static_cast<std::int64_t>(info.inputBusId));
        if (const auto bus = engine.virtualBus(info.inputBusId))
            track->setProperty("inputBusName", bus->name);
        if (!engine.trackMixState(index).inputAfterEffects)
            track->setProperty("inputAfterEffects", false);
        if (state != nullptr && state->bus)
            track->setProperty("bus", true);
        track->setProperty("outputs", outputsToVar(studio, TrackRef { false, index, 0 }));
        if (state != nullptr && state->launcherStopOff.any())
            track->setProperty("launcherStopOff", stopsOffToVar(studio, TrackRef { false, index, 0 }));
        track->setProperty(keys::laneHeight, state != nullptr ? state->laneHeight : 88);
        track->setProperty(keys::automationVisible, state != nullptr && state->automationVisible);
        track->setProperty(keys::clipStart, info.clipStartSeconds);
        track->setProperty(keys::sourceOffset, info.sourceOffsetSeconds);
        track->setProperty(keys::clipDuration, info.clipDurationSeconds);
        cydaw::base::Array<cydaw::base::Value> audioClips;
        for (const auto& clip : info.clips)
            audioClips.add(audioClipToVar(clip, projectFile));
        track->setProperty(keys::audioClips, audioClips);
        track->setProperty(keys::driveDb, info.driveDb);
        track->setProperty(keys::filterCutoffHz, info.filterCutoffHz);
        // Inline classes only; the library is written once, at the top level.
        track->setProperty(keys::nodeGraph,
                           Studio::withoutSharedModules(trackGraphs[index].getDynamicObject() != nullptr
                                                            ? trackGraphs[index]
                                                            : studio.factoryAudioGraph()));
        const auto hasModulationGraph = state != nullptr && state->modulationGraph.getDynamicObject() != nullptr;
        track->setProperty(keys::modulationGraph,
                           Studio::withoutSharedModules(hasModulationGraph ? state->modulationGraph
                                                                           : studio.factoryModulationGraph()));
        track->setProperty(keys::modulationAssignments,
                           state != nullptr && state->modulationAssignments.isArray()
                               ? state->modulationAssignments
                               : cydaw::base::Value(cydaw::base::Array<cydaw::base::Value>()));
        {
            auto plugins = pluginsToVar(engine, index, info, refreshPluginState, projectFile);
            // The knobs pinned on each plug-in's card.
            if (auto* list = plugins.getArray(); list != nullptr && state != nullptr)
                for (int at = 0; at < list->size() && at < static_cast<int>(state->pluginPins.size()); ++at)
                    if (const auto& pins = state->pluginPins[static_cast<std::size_t>(at)])
                        if (auto* plugin = (*list)[at].getDynamicObject()) {
                            cydaw::base::Array<cydaw::base::Value> pinned;
                            for (const auto pin : *pins)
                                pinned.add(static_cast<int>(pin));
                            plugin->setProperty("pinned", pinned);
                        }
            track->setProperty(keys::plugins, plugins);
        }
        track->setProperty(keys::gainAutomation, automationPointsToVar(engine.automationPoints(index)));
        if (state != nullptr) {
            track->setProperty(keys::parameterAutomation, parameterLanesToVar(state->parameterLanes));
            track->setProperty(keys::shownAutomation, state->parameterLanes.shown);
        }
        cydaw::base::Array<cydaw::base::Value> launcherClips;
        for (const auto& clip : engine.launcherClips(index))
            launcherClips.add(launcherClipToVar(clip, projectFile));
        track->setProperty(keys::launcherClips, launcherClips);
        tracks.add(cydaw::base::Value(track.get()));
    }
    document->setProperty(keys::tracks, tracks);

    cydaw::base::Array<cydaw::base::Value> midiTracks;
    for (const auto& info : engine.midiTracks()) {
        const auto found = studio.midiStates().find(info.id);
        const auto* state = found != studio.midiStates().end() ? &found->second : nullptr;
        auto track = makeObject();
        track->setProperty("outputs", outputsToVar(studio, TrackRef { true, 0, info.id }));
        if (state != nullptr && state->launcherStopOff.any())
            track->setProperty("launcherStopOff", stopsOffToVar(studio, TrackRef { true, 0, info.id }));
        track->setProperty(keys::name, info.name);
        track->setProperty(keys::gainDb, info.gainDb);
        track->setProperty(keys::pan, info.pan);
        track->setProperty(keys::mute, info.muted);
        track->setProperty(keys::solo, info.solo);
        track->setProperty(keys::armed, info.armed);
        // What it hears, where its notes also go.
        {
            cydaw::base::Array<cydaw::base::Value> inputs;
            for (const auto& input : engine.midiTrackInputs(info.id))
                inputs.add(input);
            track->setProperty("midiInputs", inputs);
            if (const auto output = engine.midiTrackOutput(info.id); output.isNotEmpty())
                track->setProperty("midiOutput", output);
        }
        // CyDAW 2's own keys; CyDAW 1 ignores them.
        track->setProperty(keys::colour, static_cast<std::int64_t>(
                                             state != nullptr ? state->colour
                                                              : studio.trackColour(TrackRef { true, 0, info.id })));
        track->setProperty(keys::laneHeight, state != nullptr ? state->laneHeight : 88);
        track->setProperty(keys::automationVisible, state != nullptr && state->automationVisible);
        track->setProperty(keys::nodeGraph,
                           Studio::withoutSharedModules(graphOr(midiGraphs, info.id, studio.factoryMidiGraph())));
        track->setProperty(keys::gainAutomation, automationPointsToVar(engine.midiAutomationPoints(info.id)));
        if (studio.rackPadCount(TrackRef { true, 0, info.id }) > 0) {
            track->setProperty("rackPads", studio.rackPadsState(TrackRef { true, 0, info.id }));
            // The container they were made for (a drum machine, layers).
            track->setProperty("padsKind", studio.padsKindName(TrackRef { true, 0, info.id }));
        }
        if (state != nullptr) {
            track->setProperty(keys::parameterAutomation, parameterLanesToVar(state->parameterLanes));
            track->setProperty(keys::shownAutomation, state->parameterLanes.shown);
        }
        cydaw::base::Array<cydaw::base::Value> clips;
        for (const auto& clipInfo : info.clips) {
            auto clip = makeObject();
            clip->setProperty(keys::name, clipInfo.name);
            clip->setProperty(keys::startBeats, clipInfo.startBeats);
            clip->setProperty(keys::lengthBeats, clipInfo.lengthBeats);
            clip->setProperty(keys::loop, clipInfo.looping);
            clip->setProperty(keys::playStartBeats, clipInfo.playStartBeats);
            clip->setProperty(keys::playEndBeats, clipInfo.playEndBeats);
            clip->setProperty(keys::loopStartBeats, clipInfo.loopStartBeats);
            clip->setProperty(keys::loopEndBeats, clipInfo.loopEndBeats);
            clip->setProperty(keys::colour, static_cast<std::int64_t>(clipInfo.colour));
            if (clipInfo.launcherScene >= 0)
                clip->setProperty(keys::launcherScene, clipInfo.launcherScene);
            clip->setProperty(keys::notes, midiNotesToVar(clipInfo.notes));
            clip->setProperty(keys::gainAutomation, automationPointsToVar(clipInfo.automation));
            clips.add(cydaw::base::Value(clip.get()));
        }
        track->setProperty(keys::clips, clips);
        midiTracks.add(cydaw::base::Value(track.get()));
    }
    document->setProperty(keys::midiTracks, midiTracks);
    {
        // The order shown: each track by its kind and its place among them.
        const auto summaries = engine.midiTrackSummaries();
        cydaw::base::Array<cydaw::base::Value> order;
        for (const auto& track : studio.tracks()) {
            auto value = makeObject();
            value->setProperty("midi", track.midi);
            auto position = static_cast<int>(track.audioIndex);
            if (track.midi)
                for (std::size_t index = 0; index < summaries.size(); ++index)
                    if (summaries[index].id == track.midiId)
                        position = static_cast<int>(index);
            value->setProperty("position", position);
            order.add(cydaw::base::Value(value.get()));
        }
        document->setProperty("trackOrder", order);
        // The groups: each group's track and its tracks, by kind and place.
        const auto placeOf = [&](const TrackRef& track) {
            auto value = makeObject();
            value->setProperty("midi", track.midi);
            auto position = static_cast<int>(track.audioIndex);
            if (track.midi)
                for (std::size_t index = 0; index < summaries.size(); ++index)
                    if (summaries[index].id == track.midiId)
                        position = static_cast<int>(index);
            value->setProperty("position", position);
            return cydaw::base::Value(value.get());
        };
        cydaw::base::Array<cydaw::base::Value> groups;
        for (const auto& track : studio.tracks())
            if (studio.isGroupTrack(track)) {
                auto value = makeObject();
                value->setProperty("group", placeOf(track));
                cydaw::base::Array<cydaw::base::Value> members;
                for (const auto& member : studio.groupMembers(track))
                    members.add(placeOf(member));
                value->setProperty("members", members);
                if (studio.isFolded(track))
                    value->setProperty("folded", true);
                groups.add(cydaw::base::Value(value.get()));
            }
        if (!groups.isEmpty())
            document->setProperty("groups", groups);
        // The hybrid tracks: each track and its companion, by kind and place.
        cydaw::base::Array<cydaw::base::Value> hybrids;
        for (const auto& track : studio.tracks())
            if (const auto companion = studio.companionOf(track)) {
                auto value = makeObject();
                value->setProperty("track", placeOf(track));
                value->setProperty("companion", placeOf(*companion));
                if (studio.recordsHybridAudio(track))
                    value->setProperty("recordsAudio", true);
                hybrids.add(cydaw::base::Value(value.get()));
            }
        if (!hybrids.isEmpty())
            document->setProperty("hybrids", hybrids);
    }

    // The first MIDI clip's notes, for projects read by CyDAW 0.7.
    document->setProperty(keys::midiNotes, midiNotesToVar(engine.midiNotes()));
    document->setProperty(keys::tempo, engine.tempo());
    document->setProperty("timeSignatureNumerator", engine.timeSignatureNumerator());
    document->setProperty("timeSignatureDenominator", engine.timeSignatureDenominator());
    document->setProperty(keys::masterGainDb, engine.masterGainDb());
    document->setProperty(keys::masterMuted, engine.isMasterMuted());
    if (masterGraph.getDynamicObject() != nullptr)
        document->setProperty("masterGraph", Studio::withoutSharedModules(masterGraph));
    // The controllers' assignments that belong to this project.
    if (const auto* assignments = studio.controllerAssignments().getArray(); assignments != nullptr && !assignments->isEmpty())
        document->setProperty("controllerAssignments", portableAssignments(studio, studio.controllerAssignments()));
    document->setProperty(keys::arrangementSnapMode,
                          static_cast<int>(interfaceValue(studio, keys::arrangementSnapMode, 4)));
    document->setProperty(keys::arrangementSnapType,
                          static_cast<int>(interfaceValue(studio, keys::arrangementSnapType, 1)));
    document->setProperty(keys::arrangementTool, static_cast<int>(interfaceValue(studio, keys::arrangementTool, 1)));
    document->setProperty(keys::launchQuantization, static_cast<int>(engine.launchQuantization()));
    // The master's launcher clips: by scene, their lanes' points in beats.
    {
        cydaw::base::Array<cydaw::base::Value> masterClips;
        for (const auto& [scene, clip] : studio.masterClips()) {
            auto* entry = new cydaw::base::Object();
            entry->setProperty("scene", scene);
            entry->setProperty("name", clip.name);
            entry->setProperty("colour", static_cast<std::int64_t>(clip.colour));
            entry->setProperty("lengthBeats", clip.lengthBeats);
            entry->setProperty("looping", clip.looping);
            cydaw::base::Array<cydaw::base::Value> lanes;
            for (const auto& [binding, points] : clip.lanes) {
                cydaw::base::Array<cydaw::base::Value> list;
                for (const auto& point : points) {
                    cydaw::base::Array<cydaw::base::Value> pair;
                    pair.add(point.beats);
                    pair.add(static_cast<double>(point.value));
                    if (point.shape != 0 || point.handles != engine::CurveHandles {})
                        for (const auto value : { static_cast<float>(point.shape), point.bend, point.handles.inTime, point.handles.inValue,
                                                  point.handles.outTime, point.handles.outValue })
                            pair.add(static_cast<double>(value));
                    list.add(pair);
                }
                auto* lane = new cydaw::base::Object();
                lane->setProperty("binding", binding);
                lane->setProperty("points", list);
                lanes.add(cydaw::base::Value(lane));
            }
            entry->setProperty("lanes", lanes);
            masterClips.add(cydaw::base::Value(entry));
        }
        if (!masterClips.isEmpty())
            document->setProperty("masterClips", masterClips);
    }
    document->setProperty(keys::arrangementPlayback, engine.arrangementPlaybackEnabled());
    document->setProperty(keys::launcherActionReplay,
                          static_cast<bool>(interfaceValue(studio, keys::launcherActionReplay, false)));
    const auto launcherActions = interfaceValue(studio, keys::launcherActions, {});
    document->setProperty(keys::launcherActions,
                          launcherActions.isArray() ? launcherActions : cydaw::base::Value(cydaw::base::Array<cydaw::base::Value>()));
    document->setProperty(keys::editorMode, static_cast<int>(interfaceValue(studio, keys::editorMode, 1)));
    // The loop switch as the file had it; a session that never loaded one
    // follows the transport, which loops when it holds a loop range.
    document->setProperty(keys::loop, studio.loopEnabled());
    document->setProperty(keys::loopRangeExplicit, engine.hasExplicitLoopRange());
    document->setProperty(keys::loopStart, engine.loopStart());
    document->setProperty(keys::loopEnd, engine.loopEnd());
    document->setProperty(keys::position, engine.position());
    // The compatibility field v1 recovers edited classes from: a whole graph
    // with the whole library.
    document->setProperty(keys::nodeGraph, studio.withLibrary(trackGraphs.empty() ? cydaw::base::Value() : trackGraphs.front(),
                                                              studio.factoryAudioGraph()));
    auto nodeModules = makeObject();
    nodeModules->setProperty(keys::modules, studio.sharedNodeModules());
    document->setProperty(keys::sharedNodeModules, cydaw::base::Value(nodeModules.get()));
    auto modulationModules = makeObject();
    modulationModules->setProperty(keys::modules, studio.sharedModulationModules());
    document->setProperty(keys::sharedModulationModules, cydaw::base::Value(modulationModules.get()));
    if (const auto layout = interfaceValue(studio, keys::workspaceLayout, {}); !layout.isVoid())
        document->setProperty(keys::workspaceLayout, layout);
    return cydaw::base::Value(document.get());
}

// ---- Loading ---------------------------------------------------------------------------------

namespace {

// One audio track of the file, with its input.
void restoreAudioEntry(Studio& studio, const cydaw::base::Object& track, const cydaw::base::Value& legacyNodeGraph,
                       const BusIds& busIds, const cydaw::base::File& projectFile, cydaw::base::TextList& warnings)
{
    auto& engine = studio.engine();
    const auto index = restoreAudioTrack(studio, readSavedTrack(track), legacyNodeGraph, projectFile, warnings);
    if (!index.has_value())
        return;
    if (track.hasProperty(keys::inputBusId)) {
        const auto saved = static_cast<std::uint64_t>(
            std::max<std::int64_t>(0, static_cast<std::int64_t>(track.getProperty(keys::inputBusId))));
        const auto bus = softwareBus(studio, busIds, saved, track.getProperty("inputBusName").toString(),
                                     app::VirtualBusDirection::input);
        if (bus != 0)
            (void) engine.setTrackInputBus(*index, bus);
        else if (track.hasProperty("inputBusName"))
            warn(warnings, "The input \"" + track.getProperty("inputBusName").toString()
                               + "\" is not among this software's: the track records the first input");
    }
    engine.setTrackInputAfterEffects(*index, !track.hasProperty("inputAfterEffects")
                                                 || static_cast<bool>(track.getProperty("inputAfterEffects")));
    auto& state = studio.audioStates()[*index];
    state.automationVisible = static_cast<bool>(track.getProperty(keys::automationVisible));
    state.laneHeight = track.hasProperty(keys::laneHeight)
                           ? cydaw::base::limit(40, 420, static_cast<int>(track.getProperty(keys::laneHeight)))
                           : state.automationVisible ? 172 : 88;
}

cydaw::base::Text nameOf(const cydaw::base::Value& track)
{
    return track.getProperty(keys::name, {}).toString();
}

} // namespace

// The steps: the project's settings, each audio track, the master, each
// MIDI track, the routing, each track's sound prepared, the end.
struct DocumentLoad::State {
    enum class Stage { prelude, audio, middle, midi, routing, publish, finish, done };

    Studio& studio;
    cydaw::base::Value document;
    cydaw::base::File projectFile;
    cydaw::base::TextList& warnings;
    const cydaw::base::Object* root {};
    Stage stage { Stage::prelude };
    std::size_t index {};
    const cydaw::base::Array<cydaw::base::Value>* audioTracks {};
    const cydaw::base::Array<cydaw::base::Value>* midiTracks {};
    cydaw::base::Value legacyNodeGraph;
    BusIds busIds;
    double tempo { 120.0 };
    std::uint64_t armedTrack {};
    std::vector<TrackRef> toPublish;
    int total { 1 };
    int finished {};
    cydaw::base::Result result { cydaw::base::Result::ok() };

    State(Studio& owner, cydaw::base::Value doc, cydaw::base::File file, cydaw::base::TextList& lines)
        : studio(owner), document(std::move(doc)), projectFile(std::move(file)), warnings(lines)
    {
        root = document.getDynamicObject();
        if (root == nullptr)
            return;
        audioTracks = root->getProperty(keys::tracks).getArray();
        midiTracks = root->getProperty(keys::midiTracks).getArray();
        const auto audioCount = audioTracks != nullptr ? audioTracks->size() : 1;
        const auto midiCount = midiTracks != nullptr && !midiTracks->isEmpty() ? midiTracks->size() : 1;
        // Restoring a track weighs about four times preparing it; the rest,
        // a step each.
        total = 4 + 5 * (audioCount + midiCount);
    }
};

DocumentLoad::DocumentLoad(Studio& studio, cydaw::base::Value document, cydaw::base::File projectFile, cydaw::base::TextList& warnings)
    : state_(std::make_unique<State>(studio, std::move(document), std::move(projectFile), warnings))
{
}

DocumentLoad::~DocumentLoad() = default;

float DocumentLoad::progress() const noexcept
{
    return state_->stage == State::Stage::done ? 1.0f
                                               : cydaw::base::limit(0.0f, 1.0f, static_cast<float>(state_->finished)
                                                                              / static_cast<float>(std::max(1, state_->total)));
}

cydaw::base::Text DocumentLoad::doing() const
{
    auto& s = *state_;
    using Stage = State::Stage;
    const auto count = [](const cydaw::base::Array<cydaw::base::Value>* list) { return list != nullptr ? list->size() : 0; };
    const auto ofTotal = [](const std::size_t at, const int all) {
        return cydaw::base::Text(static_cast<int>(at) + 1) + "/" + cydaw::base::Text(std::max(1, all));
    };
    switch (s.stage) {
    case Stage::prelude:
        return ui::Ls("Réglages du projet", "The project's settings");
    case Stage::audio:
        return ui::Ls("Piste audio ", "Audio track ") + ofTotal(s.index, count(s.audioTracks))
               + (s.audioTracks != nullptr && s.index < static_cast<std::size_t>(s.audioTracks->size())
                      ? " : " + nameOf(s.audioTracks->getReference(static_cast<int>(s.index)))
                      : cydaw::base::Text());
    case Stage::middle:
        return ui::Ls("Le master", "The master");
    case Stage::midi:
        return ui::Ls("Piste ", "Track ") + ofTotal(s.index, count(s.midiTracks))
               + (s.midiTracks != nullptr && s.index < static_cast<std::size_t>(s.midiTracks->size())
                      ? " : " + nameOf(s.midiTracks->getReference(static_cast<int>(s.index)))
                      : cydaw::base::Text());
    case Stage::routing:
        return ui::Ls("Le routage", "The routing");
    case Stage::publish:
        return ui::Ls("Son préparé : piste ", "Sound prepared: track ") + ofTotal(s.index, static_cast<int>(s.toPublish.size()))
               + (s.index < s.toPublish.size() ? " : " + s.studio.trackName(s.toPublish[s.index]) : cydaw::base::Text());
    case Stage::finish:
    case Stage::done:
        break;
    }
    return ui::Ls("Fin", "Done");
}

cydaw::base::Result DocumentLoad::result() const
{
    return state_->result;
}

bool DocumentLoad::next()
{
    auto& s = *state_;
    using Stage = State::Stage;
    auto& studio = s.studio;
    auto& engine = studio.engine();
    s.finished += s.stage == Stage::audio || s.stage == Stage::midi ? 4 : 1;
    switch (s.stage) {
    case Stage::prelude: {
        if (s.root == nullptr || s.root->getProperty(keys::format).toString() != "CyDAW Project") {
            s.result = cydaw::base::Result::fail("not a CyDAW project");
            s.stage = Stage::done;
            return false;
        }
        const auto& root = *s.root;
        // The class libraries, written once at the top level; older files carry
        // the library in every graph, merged as each graph is read.
        Studio::mergeModules(studio.sharedNodeModules(), root.getProperty(keys::sharedNodeModules));
        Studio::mergeModules(studio.sharedModulationModules(), root.getProperty(keys::sharedModulationModules));
        engine.stopSamplePreview();
        // Clip-local MIDI automation is stored in seconds: the tempo comes first.
        const auto projectTempo = number(root, keys::tempo);
        // The metre (4/4 for a project from before it).
        engine.setTimeSignature(root.hasProperty("timeSignatureNumerator") ? static_cast<int>(root.getProperty("timeSignatureNumerator")) : 4,
                                root.hasProperty("timeSignatureDenominator") ? static_cast<int>(root.getProperty("timeSignatureDenominator")) : 4);
        s.tempo = projectTempo > 0.0 ? projectTempo : 120.0;
        engine.setTempo(s.tempo);
        engine.setLaunchQuantization(
            root.hasProperty(keys::launchQuantization)
                ? static_cast<Engine::LaunchQuantization>(
                      cydaw::base::limit(0, 4, static_cast<int>(root.getProperty(keys::launchQuantization))))
                : Engine::LaunchQuantization::oneBar);
        // The master's launcher clips.
        {
            std::map<int, MasterClip> masterClips;
            if (const auto* list = root.getProperty("masterClips").getArray())
                for (const auto& entry : *list) {
                    const auto scene = static_cast<int>(entry.getProperty("scene", -1));
                    if (scene < 0 || scene >= Engine::maximumLauncherScenes)
                        continue;
                    MasterClip clip;
                    clip.name = entry.getProperty("name", cydaw::base::Text()).toString();
                    clip.colour = static_cast<std::uint32_t>(static_cast<std::int64_t>(entry.getProperty("colour", static_cast<std::int64_t>(0xffffb84d))));
                    clip.lengthBeats = std::clamp(static_cast<double>(entry.getProperty("lengthBeats", 16.0)), 0.25, 4'096.0);
                    clip.looping = static_cast<bool>(entry.getProperty("looping", true));
                    if (const auto* lanes = entry.getProperty("lanes", cydaw::base::Value()).getArray())
                        for (const auto& lane : *lanes) {
                            const auto binding = lane.getProperty("binding", cydaw::base::Text()).toString();
                            std::vector<MasterClip::Point> points;
                            if (const auto* values = lane.getProperty("points", cydaw::base::Value()).getArray())
                                for (const auto& value : *values)
                                    if (const auto* pair = value.getArray(); pair != nullptr && pair->size() >= 2) {
                                        MasterClip::Point point { static_cast<double>((*pair)[0]), static_cast<float>(static_cast<double>((*pair)[1])) };
                                        if (pair->size() >= 8) {
                                            const auto at = [pair](const int index) { return static_cast<float>(static_cast<double>((*pair)[index])); };
                                            point.shape = engine::clampedCurveShape(static_cast<int>((*pair)[2]));
                                            point.bend = std::clamp(at(3), -1.0f, 1.0f);
                                            point.handles = { at(4), at(5), at(6), at(7) };
                                        }
                                        points.push_back(point);
                                    }
                            std::sort(points.begin(), points.end(), [](const auto& a, const auto& b) { return a.beats < b.beats; });
                            if (binding.isNotEmpty() && !points.empty())
                                clip.lanes[binding] = std::move(points);
                        }
                    masterClips[scene] = std::move(clip);
                }
            studio.setMasterClips(std::move(masterClips));
        }
        // The virtual inputs and outputs are the software's: the project's are
        // only names to find them by.
        s.busIds = mapProjectBuses(studio, root);
        s.legacyNodeGraph = root.getProperty(keys::nodeGraph);
        if (isNodeGraphState(s.legacyNodeGraph))
            Studio::mergeModules(studio.sharedNodeModules(), s.legacyNodeGraph);
        // One arrangement rebuild per track once every clip is back.
        engine.beginArrangementBatch();
        s.stage = Stage::audio;
        s.index = 0;
        return true;
    }
    case Stage::audio: {
        if (s.audioTracks != nullptr) {
            if (s.index < static_cast<std::size_t>(s.audioTracks->size())) {
                if (const auto* track = s.audioTracks->getReference(static_cast<int>(s.index)).getDynamicObject())
                    restoreAudioEntry(studio, *track, s.legacyNodeGraph, s.busIds, s.projectFile, s.warnings);
                ++s.index;
            }
            if (s.index < static_cast<std::size_t>(s.audioTracks->size()))
                return true;
        } else {
            // The original single-track format.
            SavedAudioTrack saved;
            saved.audioPath = s.root->getProperty(keys::audioFile).toString();
            saved.gainDb = toFloat(s.root->getProperty(keys::gainDb));
            saved.pan = toFloat(s.root->getProperty(keys::pan));
            saved.muted = static_cast<bool>(s.root->getProperty(keys::mute));
            saved.nodeGraph = s.legacyNodeGraph;
            (void) restoreAudioTrack(studio, saved, s.legacyNodeGraph, s.projectFile, s.warnings);
        }
        s.stage = Stage::middle;
        return true;
    }
    case Stage::middle: {
        const auto& root = *s.root;
        if (const auto rebuilt = engine.endArrangementBatch(); rebuilt.failed())
            warn(s.warnings, "The arrangement could not be rebuilt: " + rebuilt.getErrorMessage());
        engine.setTempo(s.tempo);
        engine.setMasterGain(root.hasProperty(keys::masterGainDb) ? toFloat(root.getProperty(keys::masterGainDb)) : 0.0f);
        engine.setMasterMuted(static_cast<bool>(root.getProperty(keys::masterMuted)));
        // The master's devices; a project from before them gets the limiter.
        studio.restoreMasterGraph(root.getProperty("masterGraph"));
        engine.setArrangementPlaybackEnabled(!root.hasProperty(keys::arrangementPlayback)
                                             || static_cast<bool>(root.getProperty(keys::arrangementPlayback)));
        restoreInterfaceState(studio, root);
        s.stage = Stage::midi;
        s.index = 0;
        return true;
    }
    case Stage::midi: {
        if (s.midiTracks != nullptr && !s.midiTracks->isEmpty()) {
            if (s.index < static_cast<std::size_t>(s.midiTracks->size())) {
                if (const auto* track = s.midiTracks->getReference(static_cast<int>(s.index)).getDynamicObject())
                    if (const auto armed = restoreMidiTrack(studio, *track, s.warnings); armed != 0)
                        s.armedTrack = armed;
                ++s.index;
            }
            if (s.index < static_cast<std::size_t>(s.midiTracks->size()))
                return true;
        } else {
            s.armedTrack = restoreLegacyMidi(studio, *s.root);
        }
        s.stage = Stage::routing;
        return true;
    }
    case Stage::routing: {
        restoreRouting(studio, *s.root, s.busIds);
        // The controllers' assignments, the tracks they name there now.
        studio.setControllerAssignments(localAssignments(studio, s.root->getProperty("controllerAssignments")), true);
        // Every graph published once, now that all are restored.
        for (std::size_t index = 0; index < engine.trackCount(); ++index)
            s.toPublish.push_back(TrackRef { false, index, 0 });
        for (const auto& summary : engine.midiTrackSummaries())
            s.toPublish.push_back(TrackRef { true, 0, summary.id });
        s.stage = Stage::publish;
        s.index = 0;
        return true;
    }
    case Stage::publish: {
        if (s.index < s.toPublish.size())
            (void) studio.publishGraph(s.toPublish[s.index++], true);
        if (s.index < s.toPublish.size())
            return true;
        s.stage = Stage::finish;
        return true;
    }
    case Stage::finish: {
        const auto& root = *s.root;
        const auto midiSummaries = engine.midiTrackSummaries();
        if (!midiSummaries.empty())
            (void) engine.setLiveMidiTrack(s.armedTrack != 0 ? s.armedTrack : midiSummaries.front().id);
        if (root.hasProperty(keys::loopRangeExplicit) && static_cast<bool>(root.getProperty(keys::loopRangeExplicit)))
            engine.setLoopRange(number(root, keys::loopStart), number(root, keys::loopEnd));
        else
            engine.resetLoopRange();
        studio.setLoopEnabled(static_cast<bool>(root.getProperty(keys::loop)));
        engine.setPosition(number(root, keys::position));
        s.stage = Stage::done;
        return false;
    }
    case Stage::done:
        break;
    }
    return false;
}

cydaw::base::Result applyDocument(Studio& studio, const cydaw::base::Value& document, const cydaw::base::File& projectFile,
                           cydaw::base::TextList& warnings)
{
    DocumentLoad load(studio, document, projectFile, warnings);
    while (load.next()) {
    }
    return load.result();
}

cydaw::base::Value virtualIoDocument(Studio& studio)
{
    std::map<std::uint64_t, cydaw::base::Value> graphs;
    for (const auto& [busId, busState] : studio.busStates())
        if (busState.graph != nullptr)
            graphs[busId] = busState.graph->serialise();
    auto document = makeObject();
    document->setProperty("format", "CyDAW Virtual IO");
    document->setProperty("version", 1);
    document->setProperty(keys::virtualBuses, busesToVar(studio, graphs, true));
    return cydaw::base::Value(document.get());
}

void applyVirtualIo(Studio& studio, const cydaw::base::Value& document)
{
    auto& engine = studio.engine();
    std::map<std::uint64_t, cydaw::base::Value> graphs;
    auto buses = readBuses(studio, document.getProperty(keys::virtualBuses, cydaw::base::Value()), graphs);
    if (buses.empty())
        return;
    engine.replaceVirtualBuses(std::move(buses));
    studio.busStates().clear();
    for (const auto& bus : engine.virtualBuses()) {
        const auto found = graphs.find(bus.id);
        auto& state = studio.adoptBus(bus.id, found != graphs.end() ? found->second : cydaw::base::Value());
        (void) engine.setVirtualBusGraph(bus.id, state.graph->graph().nativeDspGraph());
    }
}

} // namespace cydaw::v2::project_io
