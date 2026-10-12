#include "WorkspaceComponent.h"
#include "MessageThreadSampler.h"
#include "PaintProfile.h"
#include "LibraryDragPayload.h"
#include "ProjectRecovery.h"
#include "JsonValue.h"
#include "WorkspaceComponentInternal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace cydaw::app {

namespace jsonKeys {
// Property names as identifiers made once: an Identifier built from a
// literal is a string-pool lookup, and a restore or a save of a large
// graph made tens of thousands of them.
static const juce::Identifier activeTake("activeTake");
static const juce::Identifier amount("amount");
static const juce::Identifier armed("armed");
static const juce::Identifier arrangementPlayback("arrangementPlayback");
static const juce::Identifier arrangementSnapMode("arrangementSnapMode");
static const juce::Identifier arrangementSnapType("arrangementSnapType");
static const juce::Identifier arrangementTool("arrangementTool");
static const juce::Identifier audioChannels("audioChannels");
static const juce::Identifier audioClips("audioClips");
static const juce::Identifier audioFile("audioFile");
static const juce::Identifier automation("automation");
static const juce::Identifier base("base");
static const juce::Identifier binding("binding");
static const juce::Identifier bottomFloating("bottomFloating");
static const juce::Identifier bottomHeight("bottomHeight");
static const juce::Identifier bottomMode("bottomMode");
static const juce::Identifier bottomVisible("bottomVisible");
static const juce::Identifier bottomWindowBounds("bottomWindowBounds");
static const juce::Identifier browserFloating("browserFloating");
static const juce::Identifier browserVisible("browserVisible");
static const juce::Identifier browserWidth("browserWidth");
static const juce::Identifier browserWindowBounds("browserWindowBounds");
static const juce::Identifier channels("channels");
static const juce::Identifier clipDuration("clipDuration");
static const juce::Identifier clipStart("clipStart");
static const juce::Identifier clips("clips");
static const juce::Identifier colour("colour");
static const juce::Identifier connections("connections");
static const juce::Identifier curve("curve");
static const juce::Identifier curveAmount("curveAmount");
static const juce::Identifier cutoffHz("cutoffHz");
static const juce::Identifier destination("destination");
static const juce::Identifier destinationA("destinationA");
static const juce::Identifier destinationB("destinationB");
static const juce::Identifier direction("direction");
static const juce::Identifier driveDb("driveDb");
static const juce::Identifier duration("duration");
static const juce::Identifier durationBeats("durationBeats");
static const juce::Identifier editorFloating("editorFloating");
static const juce::Identifier editorMode("editorMode");
static const juce::Identifier editorWindowBounds("editorWindowBounds");
static const juce::Identifier enabled("enabled");
static const juce::Identifier eventTimeline("eventTimeline");
static const juce::Identifier events("events");
static const juce::Identifier exposeToSystem("exposeToSystem");
static const juce::Identifier factoryPreset("factoryPreset");
static const juce::Identifier fadeIn("fadeIn");
static const juce::Identifier fadeOut("fadeOut");
static const juce::Identifier file("file");
static const juce::Identifier filterCutoffHz("filterCutoffHz");
static const juce::Identifier format("format");
static const juce::Identifier gainAutomation("gainAutomation");
static const juce::Identifier gainDb("gainDb");
static const juce::Identifier graphFloating("graphFloating");
static const juce::Identifier graphWindowBounds("graphWindowBounds");
static const juce::Identifier height("height");
static const juce::Identifier id("id");
static const juce::Identifier inline_("inline");
static const juce::Identifier inputBusId("inputBusId");
static const juce::Identifier inspectorFloating("inspectorFloating");
static const juce::Identifier inspectorVisible("inspectorVisible");
static const juce::Identifier inspectorWidth("inspectorWidth");
static const juce::Identifier inspectorWindowBounds("inspectorWindowBounds");
static const juce::Identifier key("key");
static const juce::Identifier label("label");
static const juce::Identifier laneHeight("laneHeight");
static const juce::Identifier launchQuantization("launchQuantization");
static const juce::Identifier launcherActionReplay("launcherActionReplay");
static const juce::Identifier launcherActions("launcherActions");
static const juce::Identifier launcherClips("launcherClips");
static const juce::Identifier lengthBeats("lengthBeats");
static const juce::Identifier loop("loop");
static const juce::Identifier loopEnd("loopEnd");
static const juce::Identifier loopEndBeats("loopEndBeats");
static const juce::Identifier loopRangeExplicit("loopRangeExplicit");
static const juce::Identifier loopStart("loopStart");
static const juce::Identifier loopStartBeats("loopStartBeats");
static const juce::Identifier masterGainDb("masterGainDb");
static const juce::Identifier maximum("maximum");
static const juce::Identifier midiEditorVisible("midiEditorVisible");
static const juce::Identifier midiNotes("midiNotes");
static const juce::Identifier midiTracks("midiTracks");
static const juce::Identifier minimum("minimum");
static const juce::Identifier modulationAssignments("modulationAssignments");
static const juce::Identifier modulationGraph("modulationGraph");
static const juce::Identifier modules("modules");
static const juce::Identifier monitor("monitor");
static const juce::Identifier mute("mute");
static const juce::Identifier muted("muted");
static const juce::Identifier name("name");
static const juce::Identifier nodeGraph("nodeGraph");
static const juce::Identifier nodes("nodes");
static const juce::Identifier note("note");
static const juce::Identifier notes("notes");
static const juce::Identifier offset("offset");
static const juce::Identifier pan("pan");
static const juce::Identifier parameter("parameter");
static const juce::Identifier parameters("parameters");
static const juce::Identifier physicalA("physicalA");
static const juce::Identifier physicalB("physicalB");
static const juce::Identifier playEnd("playEnd");
static const juce::Identifier playEndBeats("playEndBeats");
static const juce::Identifier playStart("playStart");
static const juce::Identifier playStartBeats("playStartBeats");
static const juce::Identifier pluginFile("pluginFile");
static const juce::Identifier pluginState("pluginState");
static const juce::Identifier plugins("plugins");
static const juce::Identifier points("points");
static const juce::Identifier position("position");
static const juce::Identifier scene("scene");
static const juce::Identifier schema("schema");
static const juce::Identifier sharedModulationModules("sharedModulationModules");
static const juce::Identifier sharedNodeModules("sharedNodeModules");
static const juce::Identifier solo("solo");
static const juce::Identifier source("source");
static const juce::Identifier sourceDuration("sourceDuration");
static const juce::Identifier sourceOffset("sourceOffset");
static const juce::Identifier sourceTempo("sourceTempo");
static const juce::Identifier start("start");
static const juce::Identifier startBeats("startBeats");
static const juce::Identifier state("state");
static const juce::Identifier stretchMarkers("stretchMarkers");
static const juce::Identifier stretchMode("stretchMode");
static const juce::Identifier takes("takes");
static const juce::Identifier tempo("tempo");
static const juce::Identifier time("time");
static const juce::Identifier timeline("timeline");
static const juce::Identifier title("title");
static const juce::Identifier track("track");
static const juce::Identifier tracks("tracks");
static const juce::Identifier type("type");
static const juce::Identifier value("value");
static const juce::Identifier valueDb("valueDb");
static const juce::Identifier velocity("velocity");
static const juce::Identifier version("version");
static const juce::Identifier virtualBuses("virtualBuses");
static const juce::Identifier width("width");
static const juce::Identifier workspaceLayout("workspaceLayout");
static const juce::Identifier x("x");
static const juce::Identifier y("y");
} // namespace jsonKeys


namespace document_ids {
const juce::Identifier time("time");
const juce::Identifier valueDb("valueDb");
const juce::Identifier value("value");
const juce::Identifier curve("curve");
const juce::Identifier curveAmount("curveAmount");
const juce::Identifier note("note");
const juce::Identifier startBeats("startBeats");
const juce::Identifier durationBeats("durationBeats");
const juce::Identifier velocity("velocity");
const juce::Identifier gainAutomation("gainAutomation");
const juce::Identifier notes("notes");
const juce::Identifier midiNotes("midiNotes");
} // namespace document_ids

juce::var automationPointsToVar(
    const std::vector<AudioTrackController::AutomationPoint>& points)
{
    juce::Array<juce::var> automation;
    automation.ensureStorageAllocated(static_cast<int>(points.size()));
    for (const auto& point : points) {
        auto value = juce::DynamicObject::Ptr(new juce::DynamicObject());
        value->setProperty(document_ids::time, point.timeSeconds);
        value->setProperty(document_ids::valueDb, point.valueDb);
        value->setProperty(document_ids::curve, static_cast<int>(point.curve));
        value->setProperty(document_ids::curveAmount, point.curveAmount);
        automation.add(juce::var(value.get()));
    }
    return automation;
}

juce::var midiNotesToVar(
    const std::vector<AudioTrackController::MidiNote>& notes)
{
    juce::Array<juce::var> values;
    values.ensureStorageAllocated(static_cast<int>(notes.size()));
    for (const auto& noteInfo : notes) {
        auto note = juce::DynamicObject::Ptr(new juce::DynamicObject());
        note->setProperty(document_ids::note, noteInfo.noteNumber);
        note->setProperty(document_ids::startBeats, noteInfo.startBeats);
        note->setProperty(document_ids::durationBeats, noteInfo.durationBeats);
        note->setProperty(document_ids::velocity, noteInfo.velocity);
        values.add(juce::var(note.get()));
    }
    return values;
}

void DeferredDocumentWork::apply() const
{
    for (const auto& entry : automation)
        entry.owner->setProperty(entry.property,
                                 automationPointsToVar(entry.points));
    for (const auto& entry : notes)
        entry.owner->setProperty(entry.property, midiNotesToVar(entry.notes));
}

namespace {
using detail::deleteRecursivelyWithRetry;
using detail::isNodeGraphState;

// Shades come from the shared palette; this file used to carry its own copy
// of each, which is how the panels and the editors drifted apart.
constexpr auto background = theme::background;
constexpr auto panelBackground = theme::panel;
constexpr auto border = theme::border;
constexpr auto primaryText = theme::primaryText;
constexpr auto secondaryText = theme::secondaryText;
// The chrome is 52 px tall and its controls are 30 px. Controls that fill the
// bar edge to edge are what made it read as one solid wall.
constexpr auto toolbarHeight = 52;
constexpr auto statusBarHeight = 24;
constexpr auto launcherClipEditorEventId =
    std::numeric_limits<std::uint64_t>::max();
constexpr std::array<juce::uint32, 8> trackAccentColours {
    0xffff9b2f, 0xff66b5ff, 0xffd67cf2, 0xff74c96b,
    0xffffd15a, 0xff53d0c2, 0xffff7373, 0xff9d91ff
};

juce::Colour defaultTrackColour(const std::size_t index)
{
    return juce::Colour(
        trackAccentColours[index % trackAccentColours.size()]);
}

juce::String pluginAutomationTargetId(const std::size_t pluginIndex,
                                      const std::uint32_t parameterIndex)
{
    return "plugin/" + juce::String(pluginIndex) + "/" + juce::String(parameterIndex);
}

bool parsePluginAutomationTarget(const juce::String& id,
                                 std::size_t& pluginIndex,
                                 std::uint32_t& parameterIndex)
{
    const auto parts = juce::StringArray::fromTokens(id, "/", {});
    if (parts.size() != 3 || parts[0] != "plugin")
        return false;
    const auto parsedPlugin = parts[1].getLargeIntValue();
    const auto parsedParameter = parts[2].getLargeIntValue();
    if (parsedPlugin < 0 || parsedParameter < 0)
        return false;
    pluginIndex = static_cast<std::size_t>(parsedPlugin);
    parameterIndex = static_cast<std::uint32_t>(parsedParameter);
    return true;
}

bool isLegacyMixedFactoryAudioGraph(const juce::var& state)
{
    const auto* root = state.getDynamicObject();
    const auto* nodes = root != nullptr
                            ? root->getProperty(jsonKeys::nodes).getArray() : nullptr;
    const auto* connections = root != nullptr
                                  ? root->getProperty(jsonKeys::connections).getArray()
                                  : nullptr;
    if (nodes == nullptr || connections == nullptr || nodes->size() != 8
        || connections->size() != 7
        || root->getProperty(jsonKeys::factoryPreset).toString().isNotEmpty())
        return false;

    std::vector<std::pair<std::uint64_t, juce::String>> typedNodes;
    typedNodes.reserve(8);
    juce::StringArray nodeTypes;
    for (const auto& value : *nodes) {
        const auto* node = value.getDynamicObject();
        if (node == nullptr)
            return false;
        const auto type = node->getProperty(jsonKeys::type).toString();
        const auto id = static_cast<std::uint64_t>(static_cast<juce::int64>(
            node->getProperty(jsonKeys::id)));
        if (id == 0 || type.isEmpty())
            return false;
        typedNodes.emplace_back(id, type);
        nodeTypes.add(type);
        const auto* parameters = node->getProperty(jsonKeys::parameters)
                                     .getDynamicObject();
        if (type == "drive"
            && (parameters == nullptr
                || std::abs(static_cast<double>(
                       parameters->getProperty(jsonKeys::driveDb))) > 0.001))
            return false;
        if (type == "lowpass"
            && (parameters == nullptr
                || std::abs(static_cast<double>(
                       parameters->getProperty(jsonKeys::cutoffHz)) - 20'000.0)
                       > 0.001))
            return false;
    }
    nodeTypes.sort(true);
    juce::StringArray expectedTypes {
        "audio-clips", "drive", "gain-pan", "lowpass", "master",
        "midi-input", "poly-sine", "spectrum"
    };
    expectedTypes.sort(true);
    if (nodeTypes != expectedTypes)
        return false;

    const auto typeForId = [&typedNodes](const std::uint64_t id) {
        const auto found = std::find_if(
            typedNodes.begin(), typedNodes.end(), [id](const auto& item) {
                return item.first == id;
            });
        return found != typedNodes.end() ? found->second : juce::String {};
    };
    juce::StringArray edges;
    for (const auto& value : *connections) {
        const auto* connection = value.getDynamicObject();
        if (connection == nullptr)
            return false;
        const auto source = typeForId(static_cast<std::uint64_t>(
            static_cast<juce::int64>(connection->getProperty(jsonKeys::source))));
        const auto destination = typeForId(static_cast<std::uint64_t>(
            static_cast<juce::int64>(connection->getProperty(jsonKeys::destination))));
        if (source.isEmpty() || destination.isEmpty())
            return false;
        edges.add(source + "->" + destination);
    }
    edges.sort(true);
    juce::StringArray expectedEdges {
        "audio-clips->drive", "drive->lowpass", "gain-pan->spectrum",
        "lowpass->gain-pan", "midi-input->poly-sine",
        "poly-sine->master", "spectrum->master"
    };
    expectedEdges.sort(true);
    return edges == expectedEdges;
}

double evaluateValueGraphState(const juce::var& state,
                               const double timeSeconds)
{
    struct ValueNode {
        std::uint64_t id {};
        juce::String type;
        float rate { 1.0f };
        float depth { 1.0f };
        float offset {};
        int shape {};
        float operand {};
        float minimum { -1.0f };
        float maximum { 1.0f };
    };
    struct ValueLink {
        std::uint64_t source {};
        std::uint64_t destination {};
    };

    const auto* root = state.getDynamicObject();
    if (root == nullptr)
        return 0.0;
    std::vector<ValueNode> nodes;
    std::vector<ValueLink> links;
    if (const auto* values = root->getProperty(jsonKeys::nodes).getArray()) {
        nodes.reserve(static_cast<std::size_t>(values->size()));
        for (const auto& value : *values) {
            const auto* object = value.getDynamicObject();
            if (object == nullptr)
                continue;
            ValueNode node;
            node.id = static_cast<std::uint64_t>(static_cast<juce::int64>(
                object->getProperty(jsonKeys::id)));
            node.type = object->getProperty(jsonKeys::type).toString();
            if (const auto* parameters
                = object->getProperty(jsonKeys::parameters).getDynamicObject()) {
                const auto read = [parameters](const char* key,
                                               const double fallback) {
                    return parameters->hasProperty(key)
                               ? static_cast<double>(parameters->getProperty(key))
                               : fallback;
                };
                node.rate = juce::jlimit(
                    0.01f, 40.0f,
                    static_cast<float>(read("modulationRateHz", 1.0)));
                node.depth = juce::jlimit(
                    0.0f, 1.0f,
                    static_cast<float>(read("modulationDepth", 1.0)));
                node.offset = juce::jlimit(
                    -1.0f, 1.0f,
                    static_cast<float>(read("modulationOffset", 0.0)));
                node.shape = juce::jlimit(
                    0, 3, static_cast<int>(std::lround(
                              read("modulationShape", 0.0))));
                node.operand = static_cast<float>(read("valueOperand", 0.0));
                node.minimum = static_cast<float>(read("valueMinimum", -1.0));
                node.maximum = static_cast<float>(read("valueMaximum", 1.0));
            }
            nodes.push_back(std::move(node));
        }
    }
    if (const auto* values = root->getProperty(jsonKeys::connections).getArray()) {
        links.reserve(static_cast<std::size_t>(values->size()));
        for (const auto& value : *values)
            if (const auto* object = value.getDynamicObject())
                links.push_back({
                    static_cast<std::uint64_t>(static_cast<juce::int64>(
                        object->getProperty(jsonKeys::source))),
                    static_cast<std::uint64_t>(static_cast<juce::int64>(
                        object->getProperty(jsonKeys::destination))) });
    }

    std::vector<std::uint64_t> visiting;
    std::function<double(std::uint64_t)> evaluate;
    evaluate = [&](const std::uint64_t nodeId) -> double {
        if (std::find(visiting.begin(), visiting.end(), nodeId)
            != visiting.end())
            return 0.0;
        const auto found = std::find_if(
            nodes.begin(), nodes.end(), [nodeId](const auto& node) {
                return node.id == nodeId;
            });
        if (found == nodes.end())
            return 0.0;
        visiting.push_back(nodeId);

        const auto incoming = std::find_if(
            links.begin(), links.end(), [nodeId](const auto& link) {
                return link.destination == nodeId;
            });
        auto result = incoming != links.end() ? evaluate(incoming->source) : 0.0;
        if (found->type == "lfo") {
            const auto phase = std::fmod(
                std::max(0.0, timeSeconds) * found->rate, 1.0);
            auto waveform = std::sin(
                juce::MathConstants<double>::twoPi * phase);
            if (found->shape == 1)
                waveform = 1.0 - 4.0 * std::abs(phase - 0.5);
            else if (found->shape == 2)
                waveform = phase * 2.0 - 1.0;
            else if (found->shape == 3)
                waveform = phase < 0.5 ? 1.0 : -1.0;
            result = found->offset + waveform * found->depth;
        } else if (found->type == "macro") {
            result = found->offset;
        } else if (found->type == "control-math") {
            result = result * found->depth + found->offset;
        } else if (found->type == "value-constant") {
            result = found->operand;
        } else if (found->type == "value-add") {
            result += found->operand;
        } else if (found->type == "value-subtract") {
            result -= found->operand;
        } else if (found->type == "value-multiply") {
            result *= found->operand;
        } else if (found->type == "value-divide") {
            result = std::abs(found->operand) > 1.0e-12
                         ? result / found->operand : 0.0;
        } else if (found->type == "value-modulo") {
            result = std::abs(found->operand) > 1.0e-12
                         ? std::fmod(result, found->operand) : 0.0;
        } else if (found->type == "value-power") {
            result = std::pow(result, found->operand);
        } else if (found->type == "value-sine") {
            result = std::sin(result);
        } else if (found->type == "value-cosine") {
            result = std::cos(result);
        } else if (found->type == "value-square-root") {
            result = result >= 0.0 ? std::sqrt(result) : 0.0;
        } else if (found->type == "value-floor") {
            result = std::floor(result);
        } else if (found->type == "value-ceil") {
            result = std::ceil(result);
        } else if (found->type == "value-round") {
            result = std::round(result);
        } else if (found->type == "value-min") {
            result = std::min(result, static_cast<double>(found->operand));
        } else if (found->type == "value-max") {
            result = std::max(result, static_cast<double>(found->operand));
        } else if (found->type == "value-clamp") {
            result = juce::jlimit(
                static_cast<double>(std::min(found->minimum, found->maximum)),
                static_cast<double>(std::max(found->minimum, found->maximum)),
                result);
        } else if (found->type == "value-abs") {
            result = std::abs(result);
        } else if (found->type == "value-negate") {
            result = -result;
        } else if (found->type == "logic-greater") {
            result = result > found->operand ? 1.0 : 0.0;
        } else if (found->type == "logic-less") {
            result = result < found->operand ? 1.0 : 0.0;
        } else if (found->type == "logic-equal") {
            result = std::abs(result - found->operand) <= 1.0e-6 ? 1.0 : 0.0;
        } else if (found->type == "logic-and") {
            result = result > 0.5 && found->operand > 0.5 ? 1.0 : 0.0;
        } else if (found->type == "logic-or") {
            result = result > 0.5 || found->operand > 0.5 ? 1.0 : 0.0;
        } else if (found->type == "logic-xor") {
            result = (result > 0.5) != (found->operand > 0.5) ? 1.0 : 0.0;
        } else if (found->type == "logic-not") {
            result = result > 0.5 ? 0.0 : 1.0;
        }
        visiting.pop_back();
        return std::isfinite(result) ? result : 0.0;
    };

    const auto output = std::find_if(
        nodes.begin(), nodes.end(), [](const auto& node) {
            return node.type == "graph-value-output";
        });
    if (output != nodes.end())
        return evaluate(output->id);
    const auto terminal = std::find_if(
        nodes.rbegin(), nodes.rend(), [](const auto& node) {
            return node.type == "lfo" || node.type == "macro"
                   || node.type == "control-math"
                   || node.type.startsWith("value-")
                   || node.type.startsWith("logic-");
        });
    return terminal != nodes.rend() ? evaluate(terminal->id) : 0.0;
}

bool applyModulatedNativeParameter(cydaw::engine::NativeDspGraph& graph,
                                   const juce::String& binding,
                                   const double value)
{
    if (!binding.startsWith("node."))
        return false;
    const auto separator = binding.indexOfChar(5, '.');
    if (separator < 0)
        return false;
    const auto nodeId = static_cast<std::uint64_t>(
        binding.substring(5, separator).getLargeIntValue());
    const auto suffix = binding.substring(separator + 1);
    const auto found = std::find_if(
        graph.nodes.begin(), graph.nodes.end(), [nodeId](const auto& node) {
            return node.id == nodeId;
        });
    if (found == graph.nodes.end())
        return false;
    auto& parameter = found->parameters;
    if (suffix == "drive.db")
        parameter.driveDb = juce::jlimit(0.0f, 36.0f, static_cast<float>(value));
    else if (suffix == "gain.db")
        parameter.gainDb = juce::jlimit(-120.0f, 24.0f,
                                        static_cast<float>(value));
    else if (suffix == "lowpass.cutoff" || suffix == "voiceFilter.cutoff")
        parameter.cutoffHz = juce::jlimit(20.0f, 20'000.0f,
                                          static_cast<float>(value));
    else if (suffix == "lowpass.resonance"
             || suffix == "voiceFilter.resonance")
        parameter.resonance = juce::jlimit(
            0.1f, 20.0f, static_cast<float>(value));
    else if (suffix == "highpass.cutoff")
        parameter.highpassCutoffHz = juce::jlimit(
            10.0f, 20'000.0f, static_cast<float>(value));
    else if (suffix == "clip.threshold")
        parameter.clipThreshold = juce::jlimit(
            0.001f, 1.0f, static_cast<float>(value));
    else if (suffix == "stereo.width")
        parameter.stereoWidth = juce::jlimit(
            0.0f, 2.0f, static_cast<float>(value));
    else if (suffix == "oscillator.frequency")
        parameter.oscillatorFrequencyHz = juce::jlimit(
            20.0f, 20'000.0f, static_cast<float>(value));
    else if (suffix == "noise.level")
        parameter.noiseLevel = juce::jlimit(0.0f, 1.0f,
                                            static_cast<float>(value));
    else if (suffix == "delay.time")
        parameter.delayMilliseconds = juce::jlimit(
            1.0f, 2'000.0f, static_cast<float>(value));
    else if (suffix == "delay.feedback")
        parameter.feedback = juce::jlimit(0.0f, 0.94f,
                                          static_cast<float>(value));
    else if (suffix == "delay.mix" || suffix == "reverb.mix")
        parameter.mix = juce::jlimit(0.0f, 1.0f,
                                     static_cast<float>(value));
    else if (suffix == "reverb.roomSize")
        parameter.roomSize = juce::jlimit(0.0f, 0.95f,
                                          static_cast<float>(value));
    else if (suffix == "compressor.threshold")
        parameter.thresholdDb = juce::jlimit(-60.0f, 0.0f,
                                              static_cast<float>(value));
    else if (suffix == "compressor.ratio")
        parameter.ratio = juce::jlimit(1.0f, 20.0f,
                                       static_cast<float>(value));
    else if (suffix == "eq.tone")
        parameter.tone = juce::jlimit(-1.0f, 1.0f,
                                      static_cast<float>(value));
    else
        return false;
    return true;
}

juce::var serialiseBounds(const juce::Rectangle<int> bounds)
{
    auto value = juce::DynamicObject::Ptr(new juce::DynamicObject());
    value->setProperty(jsonKeys::x, bounds.getX());
    value->setProperty(jsonKeys::y, bounds.getY());
    value->setProperty(jsonKeys::width, bounds.getWidth());
    value->setProperty(jsonKeys::height, bounds.getHeight());
    return juce::var(value.get());
}

juce::Rectangle<int> parseBounds(const juce::var& value,
                                 const juce::Rectangle<int> fallback)
{
    const auto* object = value.getDynamicObject();
    if (object == nullptr)
        return fallback;
    const auto width = static_cast<int>(object->getProperty(jsonKeys::width));
    const auto height = static_cast<int>(object->getProperty(jsonKeys::height));
    if (width <= 0 || height <= 0)
        return fallback;
    return { static_cast<int>(object->getProperty(jsonKeys::x)),
             static_cast<int>(object->getProperty(jsonKeys::y)), width, height };
}

juce::Rectangle<int> constrainToDisplays(juce::Rectangle<int> bounds)
{
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    if (const auto* display = displays.getDisplayForRect(bounds))
        return bounds.constrainedWithin(display->userBounds.toNearestInt());
    return bounds;
}

void applyTooltipFallbacks(juce::Component& component)
{
    for (auto* child : component.getChildren()) {
        if (auto* button = dynamic_cast<juce::Button*>(child)) {
            if (button->getTooltip().isEmpty()) {
                auto text = button->getButtonText().trim();
                if (text.isEmpty())
                    text = button->getName().trim();
                if (text.isNotEmpty())
                    button->setTooltip(text);
            }
        } else if (auto* combo = dynamic_cast<juce::ComboBox*>(child)) {
            if (combo->getTooltip().isEmpty()
                && combo->getText().isNotEmpty())
                combo->setTooltip(combo->getText());
        } else if (auto* slider = dynamic_cast<juce::Slider*>(child)) {
            if (slider->getTooltip().isEmpty()
                && slider->getName().isNotEmpty())
                slider->setTooltip(slider->getName());
        }
        applyTooltipFallbacks(*child);
    }
}


juce::String projectAssetPath(const juce::File& asset,
                              const juce::File& pathBase,
                              const bool absolutePaths)
{
    if (asset == juce::File {})
        return {};
    if (absolutePaths || pathBase == juce::File {})
        return asset.getFullPathName();
    return asset.getRelativePathFrom(pathBase.getParentDirectory());
}


class ProjectAutosaveJob final : public juce::ThreadPoolJob {
public:
    ProjectAutosaveJob(juce::File destination,
                       juce::var document,
                       std::unique_ptr<DeferredDocumentWork> deferred,
                       std::function<void(juce::Result)> completion)
        : ThreadPoolJob("CyDAW project autosave"),
          destination_(std::move(destination)),
          document_(std::move(document)),
          deferred_(std::move(deferred)),
          completion_(std::move(completion))
    {
    }

    JobStatus runJob() override
    {
        // The arrays the message thread left for this thread to build.
        if (deferred_ != nullptr && !shouldExit())
            deferred_->apply();
        auto result = shouldExit()
                          ? juce::Result::fail("Autosave cancelled")
                          : ProjectRecovery::writeDocumentAtomically(
                                destination_, document_, false);
        if (completion_) {
            auto completion = std::move(completion_);
            juce::MessageManager::callAsync(
                [completion = std::move(completion), result]() mutable {
                    completion(result);
                });
        }
        return jobHasFinished;
    }

private:
    juce::File destination_;
    juce::var document_;
    std::unique_ptr<DeferredDocumentWork> deferred_;
    std::function<void(juce::Result)> completion_;
};

struct CommandPaletteEntry final {
    juce::String title;
    juce::String category;
    juce::String keywords;
    juce::String shortcut;
    std::function<void()> action;
};

class CommandPaletteComponent final : public juce::Component,
                                      private juce::ListBoxModel,
                                      private juce::KeyListener {
public:
    CommandPaletteComponent(std::vector<CommandPaletteEntry> entries,
                            juce::String placeholder,
                            juce::String emptyMessage,
                            juce::String help)
        : entries_(std::move(entries)), emptyMessage_(std::move(emptyMessage))
    {
        setSize(590, 430);
        addAndMakeVisible(search_);
        addAndMakeVisible(list_);
        addAndMakeVisible(help_);
        search_.setTextToShowWhenEmpty(std::move(placeholder), juce::Colour(0xff737b88));
        search_.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff12151b));
        search_.setColour(juce::TextEditor::textColourId, juce::Colour(0xfff0f2f7));
        search_.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xffff8a00));
        search_.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(0xffffa531));
        search_.setFont(juce::FontOptions(15.0f));
        search_.setSelectAllWhenFocused(true);
        search_.onTextChange = [this] { applyFilter(); };
        search_.addKeyListener(this);
        list_.setRowHeight(42);
        list_.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff171a20));
        list_.setColour(juce::ListBox::outlineColourId, juce::Colour(0xff343a44));
        list_.setOutlineThickness(1);
        help_.setText(std::move(help), juce::dontSendNotification);
        help_.setColour(juce::Label::textColourId, juce::Colour(0xff838b98));
        help_.setFont(juce::FontOptions(10.0f));
        help_.setJustificationType(juce::Justification::centredLeft);
        applyFilter();
    }

    void focusSearch() { search_.grabKeyboardFocus(); }

    void paint(juce::Graphics& graphics) override
    {
        graphics.fillAll(juce::Colour(0xff11141a));
        graphics.setColour(juce::Colour(0xff363c47));
        graphics.drawRect(getLocalBounds(), 1);
        if (filtered_.empty()) {
            graphics.setColour(juce::Colour(0xff89919d));
            graphics.setFont(juce::FontOptions(13.0f));
            graphics.drawText(emptyMessage_, list_.getBounds(),
                              juce::Justification::centred, false);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(12);
        search_.setBounds(area.removeFromTop(40));
        area.removeFromTop(8);
        help_.setBounds(area.removeFromBottom(24));
        area.removeFromBottom(5);
        list_.setBounds(area);
    }

private:
    int getNumRows() override { return static_cast<int>(filtered_.size()); }

    void paintListBoxItem(const int row,
                          juce::Graphics& graphics,
                          const int width,
                          const int height,
                          const bool selected) override
    {
        if (row < 0 || row >= static_cast<int>(filtered_.size()))
            return;
        const auto& entry = entries_[filtered_[static_cast<std::size_t>(row)]];
        if (selected) {
            graphics.setColour(juce::Colour(0xffff8a00).withAlpha(0.22f));
            graphics.fillRect(0, 0, width, height);
            graphics.setColour(juce::Colour(0xffff8a00));
            graphics.fillRect(0, 0, 3, height);
        }
        auto bounds = juce::Rectangle<int>(0, 0, width, height).reduced(12, 3);
        auto right = bounds.removeFromRight(138);
        graphics.setColour(juce::Colour(0xfff0f2f5));
        graphics.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        graphics.drawFittedText(entry.title, bounds.removeFromTop(21),
                                juce::Justification::centredLeft, 1, 0.8f);
        graphics.setColour(juce::Colour(0xff89919d));
        graphics.setFont(juce::FontOptions(9.5f));
        graphics.drawFittedText(entry.category, bounds,
                                juce::Justification::centredLeft, 1, 0.8f);
        graphics.setColour(juce::Colour(0xffaeb5c0));
        graphics.setFont(juce::FontOptions(10.0f));
        graphics.drawFittedText(entry.shortcut, right,
                                juce::Justification::centredRight, 1, 0.8f);
        graphics.setColour(juce::Colour(0xff292e37));
        graphics.drawHorizontalLine(height - 1, 12.0f, static_cast<float>(width - 12));
    }

    void listBoxItemDoubleClicked(const int row, const juce::MouseEvent&) override
    {
        list_.selectRow(row);
        executeSelected();
    }

    bool keyPressed(const juce::KeyPress& key, juce::Component*) override
    {
        if (key.getKeyCode() == juce::KeyPress::downKey) {
            list_.selectRow(std::min(getNumRows() - 1, list_.getSelectedRow() + 1));
            return true;
        }
        if (key.getKeyCode() == juce::KeyPress::upKey) {
            list_.selectRow(std::max(0, list_.getSelectedRow() - 1));
            return true;
        }
        if (key.getKeyCode() == juce::KeyPress::returnKey) {
            executeSelected();
            return true;
        }
        if (key.getKeyCode() == juce::KeyPress::escapeKey) {
            if (auto* callout = findParentComponentOfClass<juce::CallOutBox>())
                callout->dismiss();
            return true;
        }
        return false;
    }

    void applyFilter()
    {
        filtered_.clear();
        const auto query = search_.getText().trim().toLowerCase();
        const auto terms = juce::StringArray::fromTokens(query, " ", "\"");
        for (std::size_t index = 0; index < entries_.size(); ++index) {
            const auto& entry = entries_[index];
            const auto searchable = (entry.title + " " + entry.category + " "
                                     + entry.keywords + " " + entry.shortcut).toLowerCase();
            auto matches = true;
            for (const auto& term : terms)
                if (term.isNotEmpty() && !searchable.contains(term)) {
                    matches = false;
                    break;
                }
            if (matches)
                filtered_.push_back(index);
        }
        list_.updateContent();
        list_.selectRow(filtered_.empty() ? -1 : 0);
        repaint();
    }

    void executeSelected()
    {
        const auto row = list_.getSelectedRow();
        if (row < 0 || row >= static_cast<int>(filtered_.size()))
            return;
        auto action = entries_[filtered_[static_cast<std::size_t>(row)]].action;
        if (auto* callout = findParentComponentOfClass<juce::CallOutBox>())
            callout->dismiss();
        if (action)
            juce::MessageManager::callAsync(std::move(action));
    }

    std::vector<CommandPaletteEntry> entries_;
    std::vector<std::size_t> filtered_;
    juce::String emptyMessage_;
    juce::TextEditor search_;
    juce::ListBox list_ { "CyDAW Command Palette", this };
    juce::Label help_;
};

} // namespace

class WorkspaceComponent::FloatingGraphWindow final : public juce::DocumentWindow {
public:
    explicit FloatingGraphWindow(std::function<void()> closeHandler)
        : DocumentWindow("CyDAW Node Graph", juce::Colour(0xff111318), closeButton),
          closeHandler_(std::move(closeHandler))
    {
        setUsingNativeTitleBar(true);
        setResizable(true, false);
        setResizeLimits(900, 560, 3840, 2160);
    }

    void closeButtonPressed() override
    {
        if (closeHandler_)
            closeHandler_();
    }

private:
    std::function<void()> closeHandler_;
};

WorkspaceComponent::StaticPanel::StaticPanel(juce::String title, juce::String body, juce::Colour accent)
    : title_(std::move(title)), body_(std::move(body)), accent_(accent)
{
}

void WorkspaceComponent::StaticPanel::setTitle(juce::String title)
{
    title_ = std::move(title);
    repaint();
}

void WorkspaceComponent::StaticPanel::setBody(juce::String body)
{
    body_ = std::move(body);
    repaint();
}

void WorkspaceComponent::StaticPanel::paint(juce::Graphics& graphics)
{
    const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    graphics.setColour(juce::Colour(panelBackground));
    graphics.fillRoundedRectangle(bounds, 5.0f);
    graphics.setColour(juce::Colour(border));
    graphics.drawRoundedRectangle(bounds, 5.0f, 1.0f);
    CyDawLookAndFeel::drawPanelAccent(graphics, 10.0f, 10.0f);
    graphics.setColour(juce::Colour(primaryText));
    graphics.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    graphics.drawText(title_, 22, 8, getWidth() - 32, 23, juce::Justification::centredLeft);
    const auto information = juce::Rectangle<int>(
        10, 36, std::max(0, getWidth() - 20), 76);
    graphics.setColour(juce::Colour(0xff11151b));
    graphics.fillRoundedRectangle(information.toFloat(), 3.0f);
    graphics.setColour(juce::Colour(0xff343b45));
    graphics.drawRoundedRectangle(information.toFloat(), 3.0f, 1.0f);
    graphics.setColour(juce::Colour(secondaryText));
    graphics.setFont(juce::FontOptions(10.5f));
    graphics.drawFittedText(body_, information.reduced(7, 5),
                            juce::Justification::topLeft, 5, 0.82f);
}

void WorkspaceComponent::ArrangementCanvas::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(0xff24272a));
    graphics.setColour(juce::Colour(0xff34383b));
    for (int x = TimelineRulerComponent::trackHeaderWidth; x < getWidth(); x += 64)
        graphics.drawVerticalLine(x, 0.0f, static_cast<float>(getHeight()));
    graphics.setColour(juce::Colour(0xff4a4e52));
    graphics.drawVerticalLine(TimelineRulerComponent::trackHeaderWidth, 0.0f,
                              static_cast<float>(getHeight()));
}

void WorkspaceComponent::LiveLayoutResizerBar::setDragHandler(
    std::function<void()> handler)
{
    dragHandler_ = std::move(handler);
}

void WorkspaceComponent::LiveLayoutResizerBar::mouseDrag(
    const juce::MouseEvent& event)
{
    juce::StretchableLayoutResizerBar::mouseDrag(event);
    if (dragHandler_)
        dragHandler_();
}

void WorkspaceComponent::MasterLane::setLevels(
    const std::array<float, 2>& peak,
    const std::array<float, 2>& rms)
{
    for (std::size_t channel = 0; channel < peak_.size(); ++channel) {
        peak_[channel] = std::max(peak[channel], peak_[channel] * 0.86f);
        rms_[channel] = rms_[channel] * 0.72f + rms[channel] * 0.28f;
    }
    repaint();
}

void WorkspaceComponent::MasterLane::refreshTranslations()
{
    title_ = tr("track.master");
    repaint();
}

void WorkspaceComponent::MasterLane::paint(juce::Graphics& graphics)
{
    const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    graphics.setColour(juce::Colour(0xff202629));
    graphics.fillRoundedRectangle(bounds, 3.0f);
    graphics.setColour(juce::Colour(0xff80c65a));
    graphics.drawRoundedRectangle(bounds, 3.0f, 1.5f);
    graphics.fillRect(1, 2, 5, getHeight() - 4);

    auto header = getLocalBounds().removeFromLeft(
        TimelineRulerComponent::trackHeaderWidth);
    graphics.setColour(juce::Colour(0xff2b3435));
    graphics.fillRect(header.reduced(6, 4));
    graphics.setColour(juce::Colour(0xffedf4ed));
    graphics.setFont(juce::FontOptions(11.5f, juce::Font::bold));
    graphics.drawText(title_, header.reduced(14, 0).withTrimmedRight(40),
                      juce::Justification::centredLeft, false);

    const auto meterHeight = std::max(16, getHeight() - 16);
    for (std::size_t channel = 0; channel < 2; ++channel) {
        const auto meter = juce::Rectangle<int>(
            TimelineRulerComponent::trackHeaderWidth - 34
                + static_cast<int>(channel) * 10,
            8, 7, meterHeight);
        graphics.setColour(juce::Colour(0xff111418));
        graphics.fillRoundedRectangle(meter.toFloat(), 1.5f);
        const auto db = juce::Decibels::gainToDecibels(
            std::max(0.000001f, rms_[channel]), -60.0f);
        const auto amount = juce::jlimit(0.0f, 1.0f,
                                        (db + 60.0f) / 60.0f);
        auto level = meter;
        level.removeFromTop(juce::roundToInt(
            static_cast<float>(meter.getHeight()) * (1.0f - amount)));
        graphics.setColour(peak_[channel] >= 1.0f
                               ? juce::Colour(0xffff4f4f)
                               : amount > 0.82f
                                     ? juce::Colour(0xffffc13b)
                                     : juce::Colour(0xff72d15e));
        graphics.fillRoundedRectangle(level.toFloat(), 1.5f);
    }

    auto bus = getLocalBounds().withTrimmedLeft(
        TimelineRulerComponent::trackHeaderWidth + 3).reduced(5, 8);
    graphics.setColour(juce::Colour(0xff15191c));
    graphics.fillRoundedRectangle(bus.toFloat(), 3.0f);
    graphics.setColour(juce::Colour(0xff7f8a8d));
    graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    graphics.drawText(tr("track.masterBus"), bus.reduced(10, 0),
                      juce::Justification::centredLeft, false);
}

WorkspaceComponent::WorkspaceComponent(const bool recoveryEnabled)
    : projectRecoveryFile_(ProjectRecovery::defaultRecoveryFile()),
      recoveryEnabled_(recoveryEnabled)
{
    factoryNodeGraphState_ = cloneJsonValue(nodeGraph_.serialise());
    factoryVirtualBusNodeGraphState_ = cloneJsonValue(
        factoryNodeGraphState_);
    if (const auto* factory = factoryNodeGraphState_.getDynamicObject())
        sharedNodeModules_ = cloneJsonValue(factory->getProperty(jsonKeys::modules));
    nodeGraph_.initialiseEmptyMidiTrackGraph();
    factoryMidiNodeGraphState_ = cloneJsonValue(nodeGraph_.serialise());
    nodeGraph_.restore(factoryNodeGraphState_);
    // A project starts without an implicit instrument track. MIDI tracks are
    // created explicitly from the same + Track control as audio tracks.
    audio_.clearMidiTracks();

    setLookAndFeel(&lookAndFeel_);
    setOpaque(true);
    setWantsKeyboardFocus(true);

    for (auto* component : { static_cast<juce::Component*>(&topWorkspaceArea_),
                             static_cast<juce::Component*>(&nodeResizer_),
                             static_cast<juce::Component*>(&bottomDockSlot_),
                             static_cast<juce::Component*>(&bottomShowButton_),
                             static_cast<juce::Component*>(&fileMenuButton_),
                             static_cast<juce::Component*>(&editMenuButton_),
                             static_cast<juce::Component*>(&viewMenuButton_),
                             static_cast<juce::Component*>(&openAudioButton_),
                             static_cast<juce::Component*>(&saveProjectButton_),
                             static_cast<juce::Component*>(&loadProjectButton_),
                             static_cast<juce::Component*>(&masterExportButton_),
                             static_cast<juce::Component*>(&audioSettingsButton_),
                             static_cast<juce::Component*>(&detachGraphButton_),
                             static_cast<juce::Component*>(&layoutSelector_),
                             static_cast<juce::Component*>(&layoutPanelsButton_),
                             static_cast<juce::Component*>(&saveLayoutButton_),
                             static_cast<juce::Component*>(&deleteLayoutButton_),
                             static_cast<juce::Component*>(&undoLayoutButton_),
                             static_cast<juce::Component*>(&redoLayoutButton_),
                             static_cast<juce::Component*>(&commandSearchButton_),
                             static_cast<juce::Component*>(&launcherViewButton_),
                             static_cast<juce::Component*>(&arrangementViewButton_),
                             static_cast<juce::Component*>(&clipViewButton_),
                             static_cast<juce::Component*>(&graphEditViewButton_),
                             static_cast<juce::Component*>(&busViewButton_),
                             static_cast<juce::Component*>(&mixerViewButton_),
                             static_cast<juce::Component*>(&samplesViewButton_),
                             static_cast<juce::Component*>(&editorModeSelector_),
                             static_cast<juce::Component*>(&playButton_),
                             static_cast<juce::Component*>(&stopButton_),
                             static_cast<juce::Component*>(&recordButton_),
                             static_cast<juce::Component*>(&loopButton_),
                             static_cast<juce::Component*>(&languageSelector_),
                             static_cast<juce::Component*>(&snapSelector_),
                             static_cast<juce::Component*>(&snapTypeSelector_),
                             static_cast<juce::Component*>(&arrangementToolSelector_),
                             static_cast<juce::Component*>(&tempoSlider_),
                             static_cast<juce::Component*>(&projectLabel_),
                             static_cast<juce::Component*>(&positionLabel_),
                             static_cast<juce::Component*>(&statusLabel_) })
        addAndMakeVisible(component);

    for (auto* component : {
             static_cast<juce::Component*>(&browserDockSlot_),
             static_cast<juce::Component*>(&inspectorDockSlot_),
             static_cast<juce::Component*>(&centerArea_),
             static_cast<juce::Component*>(&browserResizer_),
             static_cast<juce::Component*>(&inspectorResizer_) })
        topWorkspaceArea_.addAndMakeVisible(component);

    inspectorDockSlot_.addAndMakeVisible(inspectorPanelContainer_);
    inspectorDockSlot_.addAndMakeVisible(inspectorHideButton_);
    inspectorPanelContainer_.addAndMakeVisible(inspector_);
    for (auto* component : { static_cast<juce::Component*>(&gainLabel_),
                             static_cast<juce::Component*>(&panLabel_),
                             static_cast<juce::Component*>(&sampleGainLabel_),
                             static_cast<juce::Component*>(&sampleFadeInLabel_),
                             static_cast<juce::Component*>(&sampleFadeOutLabel_),
                             static_cast<juce::Component*>(&clipStartLabel_),
                             static_cast<juce::Component*>(&sourceOffsetLabel_),
                             static_cast<juce::Component*>(&clipDurationLabel_),
                             static_cast<juce::Component*>(&clipStretchLabel_),
                             static_cast<juce::Component*>(&sourceTempoLabel_),
                             static_cast<juce::Component*>(&stretchModeLabel_),
                             static_cast<juce::Component*>(&audioTakeLabel_),
                             static_cast<juce::Component*>(&gainSlider_),
                             static_cast<juce::Component*>(&panSlider_),
                             static_cast<juce::Component*>(&sampleGainSlider_),
                             static_cast<juce::Component*>(&sampleFadeInSlider_),
                             static_cast<juce::Component*>(&sampleFadeOutSlider_),
                             static_cast<juce::Component*>(&clipStartSlider_),
                             static_cast<juce::Component*>(&sourceOffsetSlider_),
                             static_cast<juce::Component*>(&clipDurationSlider_),
                             static_cast<juce::Component*>(&clipStretchSlider_),
                             static_cast<juce::Component*>(&sourceTempoSlider_),
                             static_cast<juce::Component*>(&stretchModeSelector_),
                             static_cast<juce::Component*>(&detectTempoButton_),
                             static_cast<juce::Component*>(&syncTempoButton_),
                             static_cast<juce::Component*>(&audioTakeSelector_),
                             static_cast<juce::Component*>(&addAudioTakeButton_),
                             static_cast<juce::Component*>(&recordAudioTakeButton_),
                             static_cast<juce::Component*>(&removeAudioTakeButton_),
                             static_cast<juce::Component*>(&muteButton_),
                             static_cast<juce::Component*>(&soloButton_),
                             static_cast<juce::Component*>(&armButton_) })
        inspectorPanelContainer_.addAndMakeVisible(component);
    browserDockSlot_.addAndMakeVisible(browser_);
    browserDockSlot_.addAndMakeVisible(browserHideButton_);
    addAndMakeVisible(inspectorShowButton_);
    addAndMakeVisible(browserShowButton_);
    centerArea_.addAndMakeVisible(editorDockSlot_);
    nodeResizer_.setDragHandler([this] {
        if (bottomPanelVisible_
            && !isPanelFloating(DockPanel::bottom)
            && !isPanelFloating(DockPanel::editor))
            bottomLayoutHeight_ = juce::jlimit(
                180, 460,
                verticalLayout_.getItemCurrentAbsoluteSize(2));
        resized();
    });
    editorDockSlot_.addAndMakeVisible(editorArea_);
    bottomDockSlot_.addAndMakeVisible(nodeAreaContainer_);
    nodeAreaContainer_.addAndMakeVisible(bottomHideButton_);
    editorArea_.addAndMakeVisible(timelineActionBar_);
    for (auto* component : {
             static_cast<juce::Component*>(&timelinePointerButton_),
             static_cast<juce::Component*>(&timelineKnifeButton_),
             static_cast<juce::Component*>(&timelineStretchButton_),
             static_cast<juce::Component*>(&timelineZoomButton_),
             static_cast<juce::Component*>(&timelineHandButton_),
             static_cast<juce::Component*>(&timelineSnapSelector_),
             static_cast<juce::Component*>(&timelineSnapTypeSelector_),
             static_cast<juce::Component*>(&timelinePlayButton_),
             static_cast<juce::Component*>(&timelineStopButton_),
             static_cast<juce::Component*>(&addArrangementTrackButton_),
             static_cast<juce::Component*>(&arrangementPlaybackButton_),
             static_cast<juce::Component*>(&launcherActionRecordButton_),
             static_cast<juce::Component*>(&launcherActionReplayButton_),
             static_cast<juce::Component*>(&timelineModeLabel_) })
        timelineActionBar_.addAndMakeVisible(component);
    editorArea_.addAndMakeVisible(timelineRuler_);
    editorArea_.addAndMakeVisible(trackViewport_);
    editorArea_.addAndMakeVisible(timelineScrollBar_);
    editorArea_.addAndMakeVisible(timelineZoomOutButton_);
    editorArea_.addAndMakeVisible(timelineFitButton_);
    editorArea_.addAndMakeVisible(timelineZoomInButton_);
    editorArea_.addAndMakeVisible(arrangementRangeButton_);
    editorArea_.addChildComponent(pianoRoll_);
    editorArea_.addChildComponent(automationEditor_);
    editorArea_.addChildComponent(contextAutomationEditor_);
    editorArea_.addChildComponent(contextAutomationResizer_);
    editorArea_.addChildComponent(contextAutomationToggleButton_);
    editorArea_.addChildComponent(audioEditor_);
    editorArea_.addChildComponent(clipTimelineEditor_);
    editorArea_.addChildComponent(clipLauncher_);
    editorArea_.addChildComponent(virtualBusManager_);
    editorArea_.addChildComponent(mixer_);
    editorArea_.addChildComponent(sampleExplorer_);
    sampleExplorer_.setPreviewHandlers(
        [this](const juce::File& file, const double startSeconds) {
            return audio_.startSamplePreview(file, startSeconds);
        },
        [this] { audio_.stopSamplePreview(); },
        [this] { return audio_.isSamplePreviewing(); },
        [this] { return audio_.samplePreviewPositionSeconds(); },
        [this](const float gainDb) { audio_.setSamplePreviewGainDb(gainDb); });
    sampleExplorer_.setOpenHandlers(
        [this](const juce::File& file) {
            audio_.stopSamplePreview();
            loadAudioFile(file);
            setEditorMode(1);
        },
        [this](const juce::File& file) {
            audio_.stopSamplePreview();
            if (!selectedTrack_ || *selectedTrack_ >= audio_.trackCount()) {
                loadAudioFile(file);
                setEditorMode(1);
                return;
            }
            std::uint64_t clipId {};
            const auto added = audio_.addAudioClipFromFile(
                *selectedTrack_, file, audio_.position(), &clipId);
            if (added.failed()) {
                showError(tr("error.openAudioTitle"), added.getErrorMessage());
                return;
            }
            refreshArrangementView();
            refreshTimelineRanges();
            refreshClipLauncher();
            markProjectDirty();
            setEditorMode(1);
        });
    nodeAreaContainer_.addAndMakeVisible(deviceChain_);
    nodeAreaContainer_.addChildComponent(nodeGraph_);
    nodeAreaContainer_.addChildComponent(modulationGraph_);
    nodeAreaContainer_.addChildComponent(bottomAutomationEditor_);
    nodeAreaContainer_.addChildComponent(midiKeyboard_);
    nodeAreaContainer_.addAndMakeVisible(devicesPanelButton_);
    nodeAreaContainer_.addAndMakeVisible(graphPanelButton_);
    nodeAreaContainer_.addAndMakeVisible(modulationPanelButton_);
    nodeAreaContainer_.addAndMakeVisible(automationPanelButton_);
    nodeAreaContainer_.addAndMakeVisible(moduleScopeButton_);
    nodeAreaContainer_.addAndMakeVisible(trackScopeButton_);
    nodeAreaContainer_.addChildComponent(modulationTargetSelector_);
    nodeAreaContainer_.addChildComponent(modulationAmountSlider_);
    nodeAreaContainer_.addChildComponent(modulationEnabledButton_);
    contextAutomationResizer_.setDragHandler([this] {
        if (contextAutomationVisible_)
            contextAutomationHeight_ = juce::jlimit(
                90, std::max(90, editorArea_.getHeight() - 140),
                contextualEditorLayout_.getItemCurrentAbsoluteSize(2));
        resized();
    });
    trackViewport_.setViewedComponent(&trackContainer_, false);
    trackViewport_.setScrollBarsShown(true, false);
    trackViewport_.setScrollBarThickness(10);
    timelineScrollBar_.setAutoHide(false);
    timelineScrollBar_.addListener(this);
    trackContainer_.addAndMakeVisible(emptyTimelineLabel_);
    trackContainer_.addAndMakeVisible(masterLane_);
    trackContainer_.addChildComponent(arrangementPlayhead_);
    emptyTimelineLabel_.setColour(juce::Label::textColourId, juce::Colour(secondaryText));
    emptyTimelineLabel_.setJustificationType(juce::Justification::centred);
    emptyTimelineLabel_.setFont(juce::FontOptions(13.0f));
    midiKeyboard_.setAvailableRange(24, 96);
    midiKeyboard_.setKeyWidth(20.0f);
    midiKeyboard_.setScrollButtonsVisible(true);
    midiKeyboard_.setWantsKeyboardFocus(false);
    timelineRuler_.setSeekHandler([this](const double seconds) {
        audio_.setPosition(snapArrangementTime(seconds));
    });
    timelineRuler_.setZoomHandler(
        [this](const double factor, const double anchorSeconds) {
            zoomArrangement(factor, anchorSeconds);
        });
    timelineRuler_.setPanHandler([this](const double deltaSeconds) {
        panArrangement(deltaSeconds);
    });
    timelineRuler_.setLoopRangeHandler(
        [this](const double rawStart, const double rawEnd) {
            auto start = snapArrangementTime(rawStart);
            auto end = snapArrangementTime(rawEnd);
            const auto minimumDuration = arrangementSnappingEnabled()
                                             ? 60.0 / audio_.tempo() * 4.0
                                                   / static_cast<double>(
                                                       arrangementGridDivision())
                                             : 0.001;
            if (end <= start)
                end = start + std::max(0.001, minimumDuration);
            audio_.setLoopRange(start, end);
            if (!loopButton_.getToggleState()) {
                loopButton_.setToggleState(true, juce::dontSendNotification);
                audio_.setLooping(true);
            }
            refreshTimelineRanges();
            markProjectDirty();
        });
    tempoSlider_.setSliderStyle(juce::Slider::LinearBar);
    tempoSlider_.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 84, 24);
    tempoSlider_.setRange(40.0, 240.0, 0.1);
    tempoSlider_.setValue(audio_.tempo(), juce::dontSendNotification);
    tempoSlider_.setTextValueSuffix(" BPM");
    // The tempo is a setting, not a transport action. Sharing the transport's
    // orange made a static value the loudest thing on the toolbar.
    tempoSlider_.setColour(juce::Slider::trackColourId,
                           juce::Colour(theme::accent).withAlpha(0.75f));
    snapSelector_.setSelectedId(4, juce::dontSendNotification);
    snapTypeSelector_.setSelectedId(1, juce::dontSendNotification);
    arrangementToolSelector_.setSelectedId(1, juce::dontSendNotification);
    timelineSnapSelector_.setSelectedId(4, juce::dontSendNotification);
    timelineSnapTypeSelector_.setSelectedId(1, juce::dontSendNotification);
    arrangementPlaybackButton_.setToggleState(true,
                                               juce::dontSendNotification);
    arrangementPlaybackButton_.setClickingTogglesState(true);
    launcherActionRecordButton_.setClickingTogglesState(true);
    launcherActionReplayButton_.setClickingTogglesState(true);
    timelineModeLabel_.setColour(juce::Label::textColourId,
                                 juce::Colour(secondaryText));
    timelineModeLabel_.setFont(juce::FontOptions(9.5f,
                                                 juce::Font::bold));
    timelineModeLabel_.setJustificationType(
        juce::Justification::centredRight);
    configureLayout();

    projectLabel_.setColour(juce::Label::textColourId, juce::Colour(primaryText));
    projectLabel_.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    positionLabel_.setText("00:00:00.000", juce::dontSendNotification);
    positionLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffefb45b));
    positionLabel_.setJustificationType(juce::Justification::centred);
    positionLabel_.setFont(juce::FontOptions(15.0f));
    // A menu entry is not a command button. Giving the three of them the same
    // frame as SAVE and EXPORT MIX is what made the row read as one wall.
    for (auto* menu : { &fileMenuButton_, &editMenuButton_, &viewMenuButton_ })
        CyDawLookAndFeel::setFlat(*menu);
    // In a tab strip only the current tab is raised; boxing all six made them
    // six buttons that happened to sit together.
    for (auto* tab : { &launcherViewButton_, &arrangementViewButton_,
                       &clipViewButton_, &graphEditViewButton_,
                       &busViewButton_, &mixerViewButton_, &samplesViewButton_ })
        CyDawLookAndFeel::setFlat(*tab);
    // The transport well behind these four is their frame. Play and record
    // carry their meaning in the glyph rather than in a filled box.
    for (auto* button : { static_cast<juce::Button*>(&playButton_),
                          static_cast<juce::Button*>(&stopButton_),
                          static_cast<juce::Button*>(&recordButton_),
                          static_cast<juce::Button*>(&loopButton_) }) {
        CyDawLookAndFeel::setFlat(*button);
        CyDawLookAndFeel::setGlyphButton(*button);
    }
    statusLabel_.setColour(juce::Label::textColourId, juce::Colour(secondaryText));
    statusLabel_.setFont(juce::FontOptions(10.0f));

    for (auto* label : { &gainLabel_, &panLabel_, &sampleGainLabel_,
                         &sampleFadeInLabel_, &sampleFadeOutLabel_,
                         &clipStartLabel_,
                         &sourceOffsetLabel_, &clipDurationLabel_,
                         &clipStretchLabel_, &sourceTempoLabel_,
                         &stretchModeLabel_,
                         &audioTakeLabel_ }) {
        label->setColour(juce::Label::textColourId, juce::Colour(secondaryText));
        label->setJustificationType(juce::Justification::centred);
        label->setFont(juce::FontOptions(10.0f, juce::Font::bold));
    }

    gainSlider_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 18);
    gainSlider_.setRange(-60.0, 12.0, 0.1);
    gainSlider_.setValue(0.0, juce::dontSendNotification);
    gainSlider_.setDefaultValue(0.0);
    gainSlider_.setTextValueSuffix(" dB");
    panSlider_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 18);
    panSlider_.setRange(-1.0, 1.0, 0.01);
    panSlider_.setValue(0.0, juce::dontSendNotification);
    panSlider_.setDefaultValue(0.0);

    sampleGainSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    sampleGainSlider_.setTextBoxStyle(juce::Slider::TextBoxRight,
                                      false, 68, 20);
    sampleGainSlider_.setRange(-60.0, 24.0, 0.1);
    sampleGainSlider_.setDoubleClickReturnValue(true, 0.0);
    sampleGainSlider_.setTextValueSuffix(" dB");
    sampleGainSlider_.onValueChange = [this] {
        applySelectedAudioEventProperties();
    };
    sampleGainSlider_.onDragStart = [this] {
        beginContinuousArrangementEdit();
    };
    sampleGainSlider_.onDragEnd = [this] {
        applySelectedAudioEventProperties();
        endContinuousArrangementEdit();
    };
    for (auto* slider : { &sampleFadeInSlider_, &sampleFadeOutSlider_ }) {
        slider->setSliderStyle(juce::Slider::LinearHorizontal);
        slider->setTextBoxStyle(juce::Slider::TextBoxRight,
                                false, 68, 20);
        slider->setRange(0.0, 60.0, 0.001);
        slider->setTextValueSuffix(" s");
        slider->onValueChange = [this, slider] {
            juce::ignoreUnused(slider);
            applyClipSettings(false);
        };
        slider->onDragStart = [this] {
            beginContinuousArrangementEdit();
        };
        slider->onDragEnd = [this] {
            applyClipSettings(false);
            endContinuousArrangementEdit();
        };
    }

    for (auto* slider : { &clipStartSlider_, &sourceOffsetSlider_,
                          &clipDurationSlider_, &clipStretchSlider_ }) {
        slider->setSliderStyle(juce::Slider::LinearHorizontal);
        slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 68, 20);
        slider->setTextValueSuffix(" s");
    }
    clipStartSlider_.setRange(0.0, 600.0, 0.001);
    sourceOffsetSlider_.setRange(0.0, 1.0, 0.001);
    clipDurationSlider_.setRange(0.001, 1.0, 0.001);
    clipStretchSlider_.setRange(0.001, 86'400.0, 0.001);
    sourceTempoSlider_.setSliderStyle(juce::Slider::LinearBar);
    sourceTempoSlider_.setTextBoxStyle(juce::Slider::TextBoxRight,
                                       false, 68, 20);
    sourceTempoSlider_.setRange(0.0, 400.0, 0.1);
    sourceTempoSlider_.setValue(0.0, juce::dontSendNotification);
    sourceTempoSlider_.setTextValueSuffix(" BPM");
    sourceTempoSlider_.textFromValueFunction = [](const double value) {
        return value > 0.0 ? juce::String(value, 1) + " BPM"
                           : juce::String("-- BPM");
    };
    sourceTempoSlider_.valueFromTextFunction = [](juce::String text) {
        return text.retainCharacters("0123456789.,")
            .replaceCharacter(',', '.').getDoubleValue();
    };
    sourceTempoSlider_.updateText();
    stretchModeSelector_.setSelectedId(1, juce::dontSendNotification);

    audioTakeSelector_.onChange = [this] {
        if (!selectedTrack_ || selectedAudioClipId_ == 0)
            return;
        const auto takeIndex = audioTakeSelector_.getSelectedItemIndex();
        if (takeIndex < 0
            || !audio_.setActiveAudioTake(
                *selectedTrack_, selectedAudioClipId_,
                static_cast<std::size_t>(takeIndex)))
            return;
        refreshSelectedTrack();
        refreshTimelineRanges();
        markProjectDirty();
    };
    addAudioTakeButton_.onClick = [this] { chooseAudioTakeFile(); };
    recordAudioTakeButton_.onClick = [this] { recordSelectedAudioTake(); };
    removeAudioTakeButton_.onClick = [this] { removeSelectedAudioTake(); };
    detectTempoButton_.onClick = [this] { detectSelectedClipTempo(); };
    syncTempoButton_.onClick = [this] { syncSelectedClipToTempo(); };
    sourceTempoSlider_.onValueChange = [this] {
        applySelectedClipStretchSettings();
    };
    sourceTempoSlider_.onDragStart = [this] {
        beginContinuousArrangementEdit();
    };
    sourceTempoSlider_.onDragEnd = [this] {
        applySelectedClipStretchSettings();
        endContinuousArrangementEdit();
    };
    stretchModeSelector_.onChange = [this] {
        applySelectedClipStretchSettings();
    };

    languageSelector_.onChange = [this] {
        const auto code = languageSelector_.getSelectedId() == 2 ? juce::String("fr") : juce::String("en");
        (void) Localization::instance().setLanguage(code);
    };
    layoutSelector_.onChange = [this] {
        const auto id = layoutSelector_.getSelectedId();
        if (id >= 1 && id <= 4) {
            applyLayoutState(factoryLayout(id));
            return;
        }
        if (id >= 100) {
            const auto index = static_cast<std::size_t>(id - 100);
            if (index < namedLayouts_.size())
                applyLayoutState(namedLayouts_[index]);
        }
        deleteLayoutButton_.setEnabled(id >= 100);
    };
    layoutPanelsButton_.onClick = [this] { showLayoutPanelsMenu(); };
    fileMenuButton_.onClick = [this] { showFileMenu(); };
    editMenuButton_.onClick = [this] { showEditMenu(); };
    viewMenuButton_.onClick = [this] { showViewMenu(); };
    inspectorHideButton_.onClick = [this] {
        auto state = captureLayoutState();
        state.inspectorVisible = false;
        applyLayoutState(state);
    };
    inspectorShowButton_.onClick = [this] {
        auto state = captureLayoutState();
        state.inspectorVisible = true;
        state.inspectorFloating = false;
        applyLayoutState(state);
    };
    browserHideButton_.onClick = [this] {
        auto state = captureLayoutState();
        state.browserVisible = false;
        applyLayoutState(state);
    };
    browserShowButton_.onClick = [this] {
        auto state = captureLayoutState();
        state.browserVisible = true;
        state.browserFloating = false;
        applyLayoutState(state);
    };
    bottomHideButton_.onClick = [this] {
        auto state = captureLayoutState();
        state.bottomVisible = false;
        applyLayoutState(state);
    };
    bottomShowButton_.onClick = [this] {
        auto state = captureLayoutState();
        state.bottomVisible = true;
        state.bottomFloating = false;
        applyLayoutState(state);
    };
    contextAutomationToggleButton_.onClick = [this] {
        contextAutomationVisible_ = !contextAutomationVisible_;
        contextAutomationToggleButton_.setButtonText(
            contextAutomationVisible_ ? juce::String::fromUTF8("⌄")
                                      : juce::String::fromUTF8("⌃"));
        contextAutomationToggleButton_.setTooltip(
            contextAutomationVisible_ ? tr("automation.hideContext")
                                      : tr("automation.showContext"));
        contextAutomationToggleButton_.setButtonText(
            contextAutomationVisible_ ? "AUTO v" : "AUTO ^");
        resized();
    };
    saveLayoutButton_.onClick = [this] { promptSaveCurrentLayout(); };
    deleteLayoutButton_.onClick = [this] { deleteSelectedLayout(); };
    undoLayoutButton_.onClick = [this] { undoLayoutChange(); };
    redoLayoutButton_.onClick = [this] { redoLayoutChange(); };
    commandSearchButton_.onClick = [this] { showCommandPalette(); };
    Localization::instance().addChangeListener(this);
    refreshTranslations();

    openAudioButton_.onClick = [this] { chooseAudioFile(); };
    browser_.setAudioOpenHandler([this](const juce::File& file) { loadAudioFile(file); });
    browser_.setSamplePreviewHandlers(
        [this](const juce::File& file, const double startSeconds) {
            return audio_.startSamplePreview(file, startSeconds);
        },
        [this] { audio_.stopSamplePreview(); },
        [this] { return audio_.isSamplePreviewing(); },
        [this] { return audio_.samplePreviewPositionSeconds(); },
        [this](const float gainDb) {
            audio_.setSamplePreviewGainDb(gainDb);
        });
    browser_.setPluginInsertHandler([this](const juce::File& file) {
        if (!selectedTrack_) {
            showError(tr("error.pluginInsertTitle"), tr("error.pluginNoTrack"));
            return;
        }
        std::size_t pluginIndex {};
        const auto result = audio_.setTrackPlugin(*selectedTrack_, file, &pluginIndex);
        if (result.failed()) {
            showError(tr("error.pluginInsertTitle"), result.getErrorMessage());
            return;
        }
        selectedPluginIndex_ = pluginIndex;
        refreshSelectedTrack();
        refreshAutomationTargets();
        markProjectDirty();
    });
    // The browser lists every factory graph the editor knows; it used to
    // carry a list of its own that stopped at eleven.
    {
        std::vector<LibraryBrowserComponent::FactoryGraphPreset> presets;
        for (const auto& summary : nodeGraph_.factoryPresetSummaries())
            presets.push_back({ summary.key, summary.translationKey,
                                summary.soundSource });
        browser_.setFactoryGraphPresets(std::move(presets));
    }
    browser_.setFactoryDeviceInsertHandler(
        [this](const juce::String& deviceKey) {
            if (!isMidiNodeGraphContext()
                && !isVirtualBusNodeGraphContext() && !selectedTrack_) {
                showError(tr("error.deviceInsertTitle"),
                          tr("error.pluginNoTrack"));
                return;
            }
            const auto result = nodeGraph_.insertFactoryDevice(deviceKey);
            if (result.failed()) {
                showError(tr("error.deviceInsertTitle"),
                          result.getErrorMessage());
                return;
            }
            refreshNodeDeviceSurface();
            refreshClipLauncher();
            markProjectDirty();
        });
    browser_.setGraphPresetFileLoadHandler(
        [this](const juce::File& file) {
            if (!isMidiNodeGraphContext()
                && !isVirtualBusNodeGraphContext() && !selectedTrack_) {
                showError(tr("error.deviceInsertTitle"),
                          tr("error.pluginNoTrack"));
                return;
            }
            const auto result = nodeGraph_.loadUserGraphPreset(file);
            if (result.failed()) {
                showError(tr("error.deviceInsertTitle"),
                          result.getErrorMessage());
                return;
            }
            refreshNodeDeviceSurface();
            refreshClipLauncher();
            markProjectDirty();
        });
    nodeGraph_.setUserGraphPresetSavedHandler(
        [this] { browser_.refreshUserGraphPresets(); });
    saveProjectButton_.onClick = [this] { chooseProjectSaveFile(); };
    loadProjectButton_.onClick = [this] { chooseProjectLoadFile(); };
    masterExportButton_.onClick = [this] {
        if (audio_.isMasterExporting())
            cancelMasterExport();
        else
            chooseMasterExportFile();
    };
    audioSettingsButton_.onClick = [this] { showAudioSettings(); };
    detachGraphButton_.onClick = [this] { toggleGraphWindow(); };
    // The panel tabs below the workspace are the same kind of control as the
    // view tabs above it, and now look like it. So are the five arrangement
    // tools, which pick one of five modes rather than doing five things.
    for (auto* tab : { &devicesPanelButton_, &graphPanelButton_,
                       &modulationPanelButton_, &automationPanelButton_,
                       &moduleScopeButton_, &trackScopeButton_,
                       &timelinePointerButton_, &timelineKnifeButton_,
                       &timelineStretchButton_, &timelineZoomButton_,
                       &timelineHandButton_ })
        CyDawLookAndFeel::setFlat(*tab);
    devicesPanelButton_.onClick = [this] { setBottomPanelMode(1); };
    graphPanelButton_.onClick = [this] { setEditorMode(7); };
    modulationPanelButton_.onClick = [this] { setBottomPanelMode(3); };
    automationPanelButton_.onClick = [this] { setBottomPanelMode(4); };
    moduleScopeButton_.onClick = [this] {
        deviceViewScope_ = DeviceChainComponent::ViewScope::module;
        deviceChain_.setViewScope(deviceViewScope_);
        moduleScopeButton_.setToggleState(true, juce::dontSendNotification);
        trackScopeButton_.setToggleState(false, juce::dontSendNotification);
    };
    trackScopeButton_.onClick = [this] {
        deviceViewScope_ = DeviceChainComponent::ViewScope::track;
        deviceChain_.setViewScope(deviceViewScope_);
        moduleScopeButton_.setToggleState(false, juce::dontSendNotification);
        trackScopeButton_.setToggleState(true, juce::dontSendNotification);
    };
    deviceChain_.setViewScope(deviceViewScope_);
    trackScopeButton_.setToggleState(true, juce::dontSendNotification);
    modulationGraph_.initialiseValueModulationGraph();
    factoryModulationGraphState_ = modulationGraph_.serialise();
    modulationAmountSlider_.setSliderStyle(juce::Slider::LinearBar);
    modulationAmountSlider_.setTextBoxStyle(
        juce::Slider::TextBoxRight, false, 58, 20);
    modulationAmountSlider_.setRange(-1.0, 1.0, 0.01);
    modulationAmountSlider_.setValue(0.25, juce::dontSendNotification);
    modulationTargetSelector_.onChange = [this] {
        refreshModulationAssignmentControls();
    };
    modulationAmountSlider_.onValueChange = [this] {
        if (!selectedTrack_ || *selectedTrack_ >= trackModulationStates_.size())
            return;
        const auto index = modulationTargetSelector_.getSelectedItemIndex();
        auto& assignments = trackModulationStates_[*selectedTrack_].assignments;
        if (index < 0 || index >= static_cast<int>(assignments.size()))
            return;
        assignments[static_cast<std::size_t>(index)].amount
            = modulationAmountSlider_.getValue();
        markProjectDirty();
    };
    modulationEnabledButton_.onClick = [this] {
        if (!selectedTrack_ || *selectedTrack_ >= trackModulationStates_.size())
            return;
        const auto index = modulationTargetSelector_.getSelectedItemIndex();
        auto& assignments = trackModulationStates_[*selectedTrack_].assignments;
        if (index < 0 || index >= static_cast<int>(assignments.size()))
            return;
        assignments[static_cast<std::size_t>(index)].enabled
            = modulationEnabledButton_.getToggleState();
        markProjectDirty();
    };
    launcherViewButton_.onClick = [this] { setEditorMode(4); };
    arrangementViewButton_.onClick = [this] { setEditorMode(1); };
    clipViewButton_.onClick = [this] {
        if (launcherEditorContext_ || selectedAudioClipId_ != 0)
            setEditorMode(6);
        else if (selectedMidiTrackId_ != 0 && selectedMidiClipId_ != 0)
            setEditorMode(2);
    };
    graphEditViewButton_.onClick = [this] { setEditorMode(7); };
    busViewButton_.onClick = [this] { setEditorMode(8); };
    mixerViewButton_.onClick = [this] { setEditorMode(9); };
    samplesViewButton_.onClick = [this] { setEditorMode(10); };
    virtualBusManager_.setChangedHandler([this] { markProjectDirty(); });
    virtualBusManager_.setOpenGraphHandler([this](const std::uint64_t busId) {
        openVirtualBusGraph(busId);
    });
    editorModeSelector_.onChange = [this] { setEditorMode(editorModeSelector_.getSelectedId()); };
    playButton_.onClick = [this] {
        if (audio_.isMasterExporting())
            return;
        noteUiHitchEvent(audio_.isPlaying() ? "stop" : "play");
        if (audio_.isPlaying())
            audio_.stop();
        else
            audio_.play();
        refreshClipLauncher();
    };
    stopButton_.onClick = [this] {
        if (audio_.isMasterExporting()) {
            cancelMasterExport();
            return;
        }
        const auto wasMidiRecording = audio_.isMidiRecording();
        if (wasMidiRecording)
            audio_.stopMidiRecording(true);
        const auto wasAudioRecording = audio_.hasRecordingSession();
        if (wasAudioRecording)
            finishAudioRecording(true);
        audio_.stop();
        if (wasMidiRecording) {
            refreshMidiEditor();
            markProjectDirty();
        }
        refreshClipLauncher();
    };
    recordButton_.onClick = [this] {
        if (audio_.isMasterExporting())
            return;
        if (editorModeSelector_.getSelectedId() == 2
            || audio_.isMidiRecording()) {
            toggleMidiRecording();
            return;
        }
        if (audio_.hasRecordingSession())
            toggleRecording();
        else {
            audioRecordingTargetClipId_.reset();
            chooseRecordingFile();
        }
    };
    loopButton_.onClick = [this] {
        audio_.setLooping(loopButton_.getToggleState());
        refreshTimelineRanges();
        markProjectDirty();
    };
    timelineZoomOutButton_.onClick = [this] {
        zoomArrangement(1.0 / 1.5,
                        arrangementViewStartSeconds_
                            + arrangementTimelineLengthSeconds_
                                  / arrangementZoom_ * 0.5);
    };
    timelineFitButton_.onClick = [this] { fitArrangement(); };
    timelineZoomInButton_.onClick = [this] {
        zoomArrangement(1.5,
                        arrangementViewStartSeconds_
                            + arrangementTimelineLengthSeconds_
                                  / arrangementZoom_ * 0.5);
    };
    arrangementRangeButton_.onClick = [this] {
        showArrangementRangeMenu();
    };
    muteButton_.onClick = [this] {
        applyMixerSettings();
        markProjectDirty();
    };
    soloButton_.onClick = [this] {
        if (currentEditorMode_ == 2 && selectedMidiTrackId_ != 0)
            applyMixerSettings();
        else if (selectedTrack_)
            audio_.setTrackSolo(*selectedTrack_, soloButton_.getToggleState());
        if (selectedTrack_ && *selectedTrack_ < trackLanes_.size())
            trackLanes_[*selectedTrack_]->setMixerState(muteButton_.getToggleState(),
                                                         soloButton_.getToggleState(),
                                                         armButton_.getToggleState());
        refreshMixer();
        markProjectDirty();
    };
    armButton_.onClick = [this] {
        if (currentEditorMode_ == 2 && selectedMidiTrackId_ != 0) {
            if (!audio_.isMidiRecording())
                (void) audio_.setMidiTrackArmed(
                    selectedMidiTrackId_, armButton_.getToggleState());
            refreshMidiEditor();
            markProjectDirty();
            return;
        }
        if (!selectedTrack_ || audio_.isRecording())
            return;
        audio_.setTrackArmed(*selectedTrack_, armButton_.getToggleState());
        refreshAudioTrackArmStates();
        markProjectDirty();
    };
    gainSlider_.onValueChange = [this] {
        applyMixerSettings();
        markProjectDirty();
    };
    panSlider_.onValueChange = [this] {
        applyMixerSettings();
        markProjectDirty();
    };
    clipStartSlider_.onValueChange = [this] {
        applyClipSettings(true);
        markProjectDirty();
    };
    sourceOffsetSlider_.onValueChange = [this] {
        applyClipSettings();
        markProjectDirty();
    };
    clipDurationSlider_.onValueChange = [this] {
        // A sample's source duration is not musical time. Only a complete
        // arrangement clip uses this field as its timeline duration.
        applyClipSettings(
            inspectorSelection_ != InspectorSelection::audioEvent);
        markProjectDirty();
    };
    const auto applyClipStretch = [this] {
        if (!selectedTrack_)
            return;
        auto timelineDuration = clipStretchSlider_.getValue();
        if (inspectorSelection_ == InspectorSelection::audioEvent
            && currentEditorMode_ == 6) {
            if (clipTimelineEditor_.snappingEnabled())
                timelineDuration = std::max(
                    0.001,
                    clipTimelineEditor_.snapTime(timelineDuration));
        } else if (arrangementSnappingEnabled()) {
            timelineDuration = std::max(
                0.001, snapArrangementTime(timelineDuration));
        }
        clipStretchSlider_.setValue(timelineDuration,
                                    juce::dontSendNotification);
        if (inspectorSelection_ == InspectorSelection::audioEvent) {
            applyClipSettings(false);
            return;
        }
        if (selectedAudioClipId_ == 0)
            return;
        if (!audio_.stretchAudioClip(*selectedTrack_,
                                     selectedAudioClipId_,
                                     timelineDuration))
            return;
        refreshActiveClipEditorPreview();
        refreshTimelineRanges();
        refreshAutomationEditor();
        refreshAudioEditor();
        updateTrackDescription();
        markProjectDirty();
    };
    clipStretchSlider_.onValueChange = [this, applyClipStretch] {
        // Keep pointer drags responsive. Text entry and keyboard changes still
        // apply immediately; a mouse drag is committed by onDragEnd below.
        if (!clipStretchSlider_.isMouseButtonDown())
            applyClipStretch();
    };
    for (auto* slider : { &clipStartSlider_, &sourceOffsetSlider_,
                          &clipDurationSlider_, &clipStretchSlider_ })
        slider->onDragStart = [this] {
            beginContinuousArrangementEdit();
        };
    const auto finishClipStartDrag = [this] {
        applyClipSettings(true);
        markProjectDirty();
        endContinuousArrangementEdit();
    };
    const auto finishClipValueDrag = [this] {
        applyClipSettings(false);
        markProjectDirty();
        endContinuousArrangementEdit();
    };
    clipStartSlider_.onDragEnd = finishClipStartDrag;
    sourceOffsetSlider_.onDragEnd = finishClipValueDrag;
    clipDurationSlider_.onDragEnd = [this] {
        applyClipSettings(
            inspectorSelection_ != InspectorSelection::audioEvent);
        markProjectDirty();
        endContinuousArrangementEdit();
    };
    clipStretchSlider_.onDragEnd = [this, applyClipStretch] {
        applyClipStretch();
        endContinuousArrangementEdit();
    };
    tempoSlider_.onValueChange = [this] {
        audio_.setTempo(tempoSlider_.getValue());
        pianoRoll_.setTempo(audio_.tempo());
        refreshTimelineRanges();
        markProjectDirty();
    };
    snapSelector_.onChange = [this] {
        refreshArrangementGrid();
        refreshSelectedTrack();
        markProjectDirty();
    };
    snapTypeSelector_.onChange = [this] {
        refreshArrangementInteractionMode();
        markProjectDirty();
    };
    arrangementToolSelector_.onChange = [this] {
        refreshArrangementInteractionMode();
        markProjectDirty();
    };
    const auto selectTimelineTool = [this](const int toolId) {
        arrangementToolSelector_.setSelectedId(toolId,
                                                juce::sendNotificationSync);
    };
    timelinePointerButton_.onClick = [selectTimelineTool] {
        selectTimelineTool(1);
    };
    timelineKnifeButton_.onClick = [selectTimelineTool] {
        selectTimelineTool(2);
    };
    timelineStretchButton_.onClick = [selectTimelineTool] {
        selectTimelineTool(3);
    };
    timelineZoomButton_.onClick = [selectTimelineTool] {
        selectTimelineTool(4);
    };
    timelineHandButton_.onClick = [selectTimelineTool] {
        selectTimelineTool(5);
    };
    timelineSnapSelector_.onChange = [this] {
        snapSelector_.setSelectedId(timelineSnapSelector_.getSelectedId(),
                                    juce::sendNotificationSync);
    };
    timelineSnapTypeSelector_.onChange = [this] {
        snapTypeSelector_.setSelectedId(
            timelineSnapTypeSelector_.getSelectedId(),
            juce::sendNotificationSync);
    };
    timelinePlayButton_.onClick = [this] { playButton_.triggerClick(); };
    timelineStopButton_.onClick = [this] { stopButton_.triggerClick(); };
    addArrangementTrackButton_.onClick = [this] {
        showAddTrackMenu(&addArrangementTrackButton_);
    };
    arrangementPlaybackButton_.onClick = [this] {
        if (!arrangementPlaybackButton_.getToggleState()) {
            setEditorMode(4);
        } else {
            audio_.setArrangementPlaybackEnabled(true);
            refreshArrangementInteractionMode();
        }
        markProjectDirty();
    };
    launcherActionRecordButton_.onClick = [this] {
        if (launcherActionRecordButton_.getToggleState()) {
            launcherActionReplayButton_.setToggleState(
                false, juce::dontSendNotification);
            nextLauncherReplayAction_ = 0;
        }
        refreshArrangementInteractionMode();
    };
    launcherActionReplayButton_.onClick = [this] {
        if (launcherActionReplayButton_.getToggleState()) {
            launcherActionRecordButton_.setToggleState(
                false, juce::dontSendNotification);
            lastLauncherReplayPosition_ = audio_.position();
            nextLauncherReplayAction_ = static_cast<std::size_t>(
                std::distance(
                    launcherActions_.begin(),
                    std::lower_bound(
                        launcherActions_.begin(), launcherActions_.end(),
                        lastLauncherReplayPosition_,
                        [](const LauncherAction& action,
                           const double time) {
                            return action.timeSeconds < time;
                        })));
        }
        refreshArrangementInteractionMode();
    };
    pianoRoll_.setAddHandler([this](const int note, const double start, const double duration, const float velocity) {
        if (selectedMidiTrackId_ == 0 || selectedMidiClipId_ == 0)
            return;
        (void) audio_.addMidiNote(selectedMidiTrackId_, selectedMidiClipId_,
                                  note, start, duration, velocity);
        refreshMidiEditor();
        refreshTimelineRangesIfLengthChanged();
        markProjectDirty();
    });
    pianoRoll_.setNotesEditHandler([this](
        std::vector<AudioTrackController::MidiNote> notes) {
        if (!audio_.replaceMidiNotes(selectedMidiTrackId_,
                                     selectedMidiClipId_,
                                     std::move(notes)))
            return;
        refreshMidiEditor();
        // Every step of a note drag comes through here; the lanes are
        // rebuilt only when the notes pushed the project's end.
        refreshTimelineRangesIfLengthChanged();
        markProjectDirty();
    });
    pianoRoll_.setSeekHandler([this](const double localSeconds) {
        clipEditorCueSeconds_ = std::max(0.0, localSeconds);
        pianoRoll_.setPlayhead(clipEditorCueSeconds_);
        bottomAutomationEditor_.setPlayhead(clipEditorCueSeconds_);
        contextAutomationEditor_.setPlayhead(clipEditorCueSeconds_);
        if (selectedMidiTrackId_ != 0 && selectedMidiClipId_ != 0
            && audio_.isMidiClipEditorPreviewing(
                selectedMidiTrackId_, selectedMidiClipId_))
            (void) audio_.seekMidiClipEditorPreview(
                selectedMidiTrackId_, selectedMidiClipId_,
                clipEditorCueSeconds_);
    });
    pianoRoll_.setTrackSelectionHandler([this](const std::uint64_t trackId) {
        if (audio_.isMidiRecording())
            return;
        if (selectedMidiTrackId_ != 0)
            (void) audio_.stopMidiClipEditorPreview(selectedMidiTrackId_);
        saveCurrentNodeGraphState();
        selectedMidiTrackId_ = trackId;
        selectedMidiClipId_ = 0;
        clipEditorCueSeconds_ = 0.0;
        launcherInspectorSelection_.reset();
        inspectorSelection_ = InspectorSelection::midiTrack;
        (void) audio_.setLiveMidiTrack(trackId);
        refreshMidiEditor();
        restoreNodeGraphForMidiTrack(trackId);
    });
    pianoRoll_.setClipSelectionHandler([this](const std::uint64_t clipId) {
        if (audio_.isMidiRecording())
            return;
        (void) audio_.stopMidiClipEditorPreview(selectedMidiTrackId_);
        selectedMidiClipId_ = clipId;
        clipEditorCueSeconds_ = 0.0;
        inspectorSelection_ = InspectorSelection::midiClip;
        refreshMidiEditor();
    });
    pianoRoll_.setAddTrackHandler([this] {
        if (audio_.isMidiRecording())
            return;
        saveCurrentNodeGraphState();
        const auto count = audio_.midiTracks().size() + 1;
        selectedMidiTrackId_ = audio_.addMidiTrack(
            tr("midi.instrument") + " " + juce::String(static_cast<int>(count)));
        selectedMidiClipId_ = 0;
        clipEditorCueSeconds_ = 0.0;
        refreshMidiEditor();
        restoreNodeGraphForMidiTrack(selectedMidiTrackId_);
        markProjectDirty();
    });
    pianoRoll_.setAddClipHandler([this] {
        if (audio_.isMidiRecording() || selectedMidiTrackId_ == 0)
            return;
        const auto tracks = audio_.midiTracks();
        const auto found = std::find_if(tracks.begin(), tracks.end(), [this](const auto& track) {
            return track.id == selectedMidiTrackId_;
        });
        const auto count = found != tracks.end() ? found->clips.size() + 1 : 1;
        selectedMidiClipId_ = audio_.addMidiClip(
            selectedMidiTrackId_,
            tr("midi.clip") + " " + juce::String(static_cast<int>(count)));
        clipEditorCueSeconds_ = 0.0;
        refreshMidiEditor();
        markProjectDirty();
    });
    pianoRoll_.setImportMidiHandler([this] { chooseMidiImportFile(); });
    pianoRoll_.setExportMidiHandler([this] { chooseMidiExportFile(); });
    pianoRoll_.setMixerHandler([this](const float gainDb,
                                      const float pan,
                                      const bool muted,
                                      const bool solo) {
        if (selectedMidiTrackId_ == 0)
            return;
        audio_.setMidiTrackMixer(selectedMidiTrackId_, gainDb, pan, muted, solo);
        markProjectDirty();
    });
    pianoRoll_.setUndoHandler([this] { undoMidiEdit(); });
    pianoRoll_.setRedoHandler([this] { redoMidiEdit(); });
    pianoRoll_.setEditGestureHandlers(
        [this] {
            if (selectedMidiTrackId_ != 0 && selectedMidiClipId_ != 0)
                (void) audio_.beginMidiEdit(selectedMidiTrackId_,
                                            selectedMidiClipId_);
        },
        [this] {
            if (audio_.endMidiEdit())
                markProjectDirty();
            refreshMidiEditor();
        });
    pianoRoll_.setViewChangedHandler([this](double, double) {
        if (currentEditorMode_ == 2)
            refreshBottomAutomationEditor();
    });
    pianoRoll_.setClipBoundsHandler(
        [this](const double length, const double playStart,
               const double playEnd, const double loopStart,
               const double loopEnd, const bool looping) {
            if (selectedMidiTrackId_ == 0 || selectedMidiClipId_ == 0)
                return;
            if (!audio_.setMidiClipTimelineBounds(
                    selectedMidiTrackId_, selectedMidiClipId_, length,
                    playStart, playEnd, loopStart, loopEnd, looping))
                return;
            refreshClipLauncher();
            refreshBottomAutomationEditor();
            updateTrackDescription();
            markProjectDirty();
        });
    pianoRoll_.setTransportHandler(
        [this](const PianoRollComponent::TransportCommand command) {
            if (selectedMidiTrackId_ == 0 || selectedMidiClipId_ == 0)
                return;
            const auto tracks = audio_.midiTracks();
            const auto track = std::find_if(
                tracks.begin(), tracks.end(), [this](const auto& value) {
                    return value.id == selectedMidiTrackId_;
                });
            if (track == tracks.end())
                return;
            const auto clip = std::find_if(
                track->clips.begin(), track->clips.end(),
                [this](const auto& value) {
                    return value.id == selectedMidiClipId_;
                });
            if (clip == track->clips.end())
                return;
            const auto secondsPerBeat = 60.0
                                        / std::max(20.0, audio_.tempo());
            const auto playStart = clip->playStartBeats * secondsPerBeat;
            const auto playEndBeat = clip->playEndBeats
                                             > clip->playStartBeats
                                         ? clip->playEndBeats
                                         : clip->lengthBeats;
            const auto playEnd = playEndBeat * secondsPerBeat;
            const auto previewing = audio_.isMidiClipEditorPreviewing(
                selectedMidiTrackId_, selectedMidiClipId_);
            if (command == PianoRollComponent::TransportCommand::play) {
                if (previewing) {
                    clipEditorCueSeconds_
                        = audio_.midiClipEditorPreviewPositionSeconds(
                            selectedMidiTrackId_);
                    (void) audio_.stopMidiClipEditorPreview(
                        selectedMidiTrackId_);
                } else {
                    if (clipEditorCueSeconds_ >= playEnd - 0.000001)
                        clipEditorCueSeconds_ = playStart;
                    clipEditorCueSeconds_ = juce::jlimit(
                        playStart, playEnd, clipEditorCueSeconds_);
                    (void) audio_.startMidiClipEditorPreview(
                        selectedMidiTrackId_, selectedMidiClipId_,
                        clipEditorCueSeconds_);
                }
            } else {
                if (previewing)
                    clipEditorCueSeconds_
                        = audio_.midiClipEditorPreviewPositionSeconds(
                            selectedMidiTrackId_);
                (void) audio_.stopMidiClipEditorPreview(
                    selectedMidiTrackId_);
                if (command
                    == PianoRollComponent::TransportCommand::returnToStart)
                    clipEditorCueSeconds_ = playStart;
            }
            pianoRoll_.setPlayhead(clipEditorCueSeconds_);
            pianoRoll_.setTransportState(
                audio_.isMidiClipEditorPreviewing(
                    selectedMidiTrackId_, selectedMidiClipId_));
            bottomAutomationEditor_.setPlayhead(clipEditorCueSeconds_);
            contextAutomationEditor_.setPlayhead(clipEditorCueSeconds_);
        });
    automationEditor_.setAddHandler([this](const double time, const float value) {
        if (!selectedTrack_)
            return;
        std::size_t pluginIndex {};
        std::uint32_t parameterIndex {};
        if (parsePluginAutomationTarget(automationTargetId_, pluginIndex, parameterIndex)) {
            (void) audio_.addPluginAutomationPoint(*selectedTrack_, pluginIndex,
                                                    parameterIndex, {}, time,
                                                    value / 100.0f);
        } else {
            (void) audio_.addAutomationPoint(*selectedTrack_, time, value);
        }
        refreshAutomationEditor();
        refreshBottomAutomationEditor();
        markProjectDirty();
    });
    automationEditor_.setUpdateHandler([this](const std::uint64_t id,
                                               const double time,
                                               const float value) {
        if (!selectedTrack_)
            return;
        std::size_t pluginIndex {};
        std::uint32_t parameterIndex {};
        if (parsePluginAutomationTarget(automationTargetId_, pluginIndex, parameterIndex)) {
            (void) audio_.updatePluginAutomationPoint(*selectedTrack_, pluginIndex,
                                                       parameterIndex, id, time,
                                                       value / 100.0f);
        } else {
            (void) audio_.updateAutomationPoint(*selectedTrack_, id, time, value);
        }
        refreshAutomationEditor();
        refreshBottomAutomationEditor();
        markProjectDirty();
    });
    automationEditor_.setLiveUpdateHandler(
        [this](const std::uint64_t id, const double time,
               const float value) {
            if (!selectedTrack_)
                return;
            std::size_t pluginIndex {};
            std::uint32_t parameterIndex {};
            if (parsePluginAutomationTarget(
                    automationTargetId_, pluginIndex, parameterIndex))
                (void) audio_.updatePluginAutomationPoint(
                    *selectedTrack_, pluginIndex, parameterIndex, id,
                    time, value / 100.0f);
            else
                (void) audio_.updateAutomationPoint(
                    *selectedTrack_, id, time, value);
        });
    automationEditor_.setRemoveHandler([this](const std::uint64_t id) {
        if (!selectedTrack_)
            return;
        std::size_t pluginIndex {};
        std::uint32_t parameterIndex {};
        if (parsePluginAutomationTarget(automationTargetId_, pluginIndex, parameterIndex))
            (void) audio_.removePluginAutomationPoint(*selectedTrack_, pluginIndex,
                                                       parameterIndex, id);
        else
            (void) audio_.removeAutomationPoint(*selectedTrack_, id);
        refreshAutomationEditor();
        refreshBottomAutomationEditor();
        markProjectDirty();
    });
    automationEditor_.setSeekHandler([this](const double seconds) { audio_.setPosition(seconds); });
    automationEditor_.setTargetChangedHandler([this](const juce::String& targetId) {
        automationTargetId_ = targetId;
        refreshAutomationEditor();
        refreshBottomAutomationEditor();
    });
    bottomAutomationEditor_.setCompactMode(true);
    bottomAutomationEditor_.setAddHandler(
        [this](const double time, const float value) {
            if (addContextAutomationPoint(time, value) == 0)
                return;
            refreshActiveClipEditorPreview();
            refreshAutomationEditor();
            refreshBottomAutomationEditor();
            markProjectDirty();
        });
    bottomAutomationEditor_.setUpdateHandler(
        [this](const std::uint64_t id, const double time,
               const float value) {
            if (!updateContextAutomationPoint(id, time, value))
                return;
            refreshActiveClipEditorPreview();
            refreshAutomationEditor();
            refreshBottomAutomationEditor();
            markProjectDirty();
        });
    bottomAutomationEditor_.setLiveUpdateHandler(
        [this](const std::uint64_t id, const double time,
               const float value) {
            if (updateContextAutomationPoint(id, time, value))
                refreshActiveClipEditorPreview();
        });
    bottomAutomationEditor_.setRemoveHandler(
        [this](const std::uint64_t id) {
            if (!removeContextAutomationPoint(id))
                return;
            refreshActiveClipEditorPreview();
            refreshAutomationEditor();
            refreshBottomAutomationEditor();
            markProjectDirty();
        });
    bottomAutomationEditor_.setSeekHandler(
        [this](const double seconds) {
            if (currentEditorMode_ == 2 && selectedMidiTrackId_ != 0
                && selectedMidiClipId_ != 0) {
                clipEditorCueSeconds_ = std::max(0.0, seconds);
                pianoRoll_.setPlayhead(clipEditorCueSeconds_);
                if (audio_.isMidiClipEditorPreviewing(
                        selectedMidiTrackId_, selectedMidiClipId_))
                    (void) audio_.seekMidiClipEditorPreview(
                        selectedMidiTrackId_, selectedMidiClipId_,
                        clipEditorCueSeconds_);
                return;
            }
            audio_.setPosition(seconds + contextualAutomationOffsetSeconds_);
        });
    bottomAutomationEditor_.setTargetChangedHandler(
        [this](const juce::String& targetId) {
            automationTargetId_ = targetId;
            refreshAutomationEditor();
            refreshBottomAutomationEditor();
        });
    contextAutomationEditor_.setCompactMode(true);
    contextAutomationEditor_.setAddHandler(
        [this](const double time, const float value) {
            if (addContextAutomationPoint(time, value) == 0)
                return;
            refreshActiveClipEditorPreview();
            refreshAutomationEditor();
            refreshBottomAutomationEditor();
            markProjectDirty();
        });
    contextAutomationEditor_.setUpdateHandler(
        [this](const std::uint64_t id, const double time,
               const float value) {
            if (!updateContextAutomationPoint(id, time, value))
                return;
            refreshActiveClipEditorPreview();
            refreshAutomationEditor();
            refreshBottomAutomationEditor();
            markProjectDirty();
        });
    contextAutomationEditor_.setLiveUpdateHandler(
        [this](const std::uint64_t id, const double time,
               const float value) {
            if (updateContextAutomationPoint(id, time, value))
                refreshActiveClipEditorPreview();
        });
    contextAutomationEditor_.setRemoveHandler(
        [this](const std::uint64_t id) {
            if (!removeContextAutomationPoint(id))
                return;
            refreshActiveClipEditorPreview();
            refreshAutomationEditor();
            refreshBottomAutomationEditor();
            markProjectDirty();
        });
    contextAutomationEditor_.setSeekHandler(
        [this](const double seconds) {
            if (currentEditorMode_ == 2 && selectedMidiTrackId_ != 0
                && selectedMidiClipId_ != 0) {
                clipEditorCueSeconds_ = std::max(0.0, seconds);
                pianoRoll_.setPlayhead(clipEditorCueSeconds_);
                if (audio_.isMidiClipEditorPreviewing(
                        selectedMidiTrackId_, selectedMidiClipId_))
                    (void) audio_.seekMidiClipEditorPreview(
                        selectedMidiTrackId_, selectedMidiClipId_,
                        clipEditorCueSeconds_);
                return;
            }
            audio_.setPosition(seconds + contextualAutomationOffsetSeconds_);
        });
    contextAutomationEditor_.setTargetChangedHandler(
        [this](const juce::String& targetId) {
            automationTargetId_ = targetId;
            refreshAutomationEditor();
            refreshBottomAutomationEditor();
        });
    automationEditor_.setCurveHandler(
        [this](const std::uint64_t id, const int curveType,
               const float amount) {
            if (!selectedTrack_)
                return;
            const auto curve = curveType == 1
                ? AudioTrackController::AutomationCurve::bezier
                : AudioTrackController::AutomationCurve::linear;
            std::size_t pluginIndex {};
            std::uint32_t parameterIndex {};
            const auto changed = parsePluginAutomationTarget(
                                     automationTargetId_, pluginIndex,
                                     parameterIndex)
                ? audio_.setPluginAutomationPointCurve(
                      *selectedTrack_, pluginIndex, parameterIndex, id,
                      curve, amount)
                : audio_.setAutomationPointCurve(
                      *selectedTrack_, id, curve, amount);
            if (changed) {
                refreshAutomationEditor();
                refreshBottomAutomationEditor();
                markProjectDirty();
            }
        });
    automationEditor_.setLiveCurveHandler(
        [this](const std::uint64_t id, const int curveType,
               const float amount) {
            if (!selectedTrack_)
                return;
            const auto curve = curveType == 1
                ? AudioTrackController::AutomationCurve::bezier
                : AudioTrackController::AutomationCurve::linear;
            std::size_t pluginIndex {};
            std::uint32_t parameterIndex {};
            if (parsePluginAutomationTarget(
                    automationTargetId_, pluginIndex, parameterIndex))
                (void) audio_.setPluginAutomationPointCurve(
                    *selectedTrack_, pluginIndex, parameterIndex, id,
                    curve, amount);
            else
                (void) audio_.setAutomationPointCurve(
                    *selectedTrack_, id, curve, amount);
        });
    const auto installContextCurveHandler =
        [this](AutomationEditorComponent& editor) {
        editor.setCurveHandler(
            [this](const std::uint64_t id, const int curveType,
                   const float amount) {
                const auto curve = curveType == 1
                    ? AudioTrackController::AutomationCurve::bezier
                    : AudioTrackController::AutomationCurve::linear;
                const auto changed = setContextAutomationPointCurve(
                    id, curve, amount);
                if (changed) {
                    refreshActiveClipEditorPreview();
                    refreshAutomationEditor();
                    refreshBottomAutomationEditor();
                    markProjectDirty();
                }
            });
    };
    installContextCurveHandler(bottomAutomationEditor_);
    installContextCurveHandler(contextAutomationEditor_);
    const auto installContextLiveCurveHandler =
        [this](AutomationEditorComponent& editor) {
            editor.setLiveCurveHandler(
                [this](const std::uint64_t id, const int curveType,
                       const float amount) {
                    const auto curve = curveType == 1
                        ? AudioTrackController::AutomationCurve::bezier
                        : AudioTrackController::AutomationCurve::linear;
                    if (setContextAutomationPointCurve(id, curve, amount))
                        refreshActiveClipEditorPreview();
                });
        };
    installContextLiveCurveHandler(bottomAutomationEditor_);
    installContextLiveCurveHandler(contextAutomationEditor_);
    for (auto* editor : { &automationEditor_, &bottomAutomationEditor_,
                          &contextAutomationEditor_ })
        editor->setEditGestureHandlers(
            [this] { beginContinuousArrangementEdit(); },
            [this] { endContinuousArrangementEdit(); });
    clipTimelineEditor_.setEditGestureHandlers(
        [this] { beginContinuousArrangementEdit(); },
        [this] { endContinuousArrangementEdit(); });
    clipTimelineEditor_.setSeekHandler([this](const double localSeconds) {
        clipEditorCueSeconds_ = std::max(0.0, localSeconds);
        clipTimelineEditor_.setPlayhead(clipEditorCueSeconds_);
        if (launcherEditorContext_) {
            const auto context = *launcherEditorContext_;
            if (audio_.activeLauncherScene(context.trackIndex)
                == context.sceneIndex)
                (void) audio_.seekLauncherClip(
                    context.trackIndex, context.sceneIndex,
                    clipEditorCueSeconds_);
            return;
        }
        if (!selectedTrack_)
            return;
        const auto clip = audio_.audioClip(*selectedTrack_,
                                           selectedAudioClipId_);
        if (clip.has_value())
            audio_.setPosition(clip->startSeconds + localSeconds);
    });
    clipTimelineEditor_.setEventEditHandler(
        [this](const std::uint64_t eventId, const double start,
               const double offset, const double duration,
               const double sourceDuration, const double fadeIn,
               const double fadeOut) {
            if (launcherEditorContext_) {
                const auto context = *launcherEditorContext_;
                if (!audio_.updateLauncherClipEvent(
                        context.trackIndex, context.sceneIndex, eventId,
                        start, offset, duration, sourceDuration,
                        fadeIn, fadeOut))
                    return;
                refreshActiveClipEditorPreview();
                refreshClipLauncher();
                refreshClipTimelineEditor();
                refreshSelectedTrack();
                markProjectDirty();
                return;
            }
            if (!selectedTrack_)
                return;
            if (!audio_.updateAudioClipEvent(
                    *selectedTrack_, selectedAudioClipId_, eventId,
                    start, offset, duration, sourceDuration,
                    fadeIn, fadeOut))
                return;
            refreshActiveClipEditorPreview();
            refreshTimelineRanges();
            refreshSelectedTrack();
            refreshClipTimelineEditor();
            markProjectDirty();
        });
    // Region drags deliberately remain visual-only until mouse-up. Rebuilding
    // readers, stretch plans and preview buses for every mouse pixel stalls the
    // message thread even when transport is stopped. The regular edit handler
    // above commits the final value and updates audio once at gesture end.
    clipTimelineEditor_.setEventCommandHandler(
        [this](const std::uint64_t eventId,
               const WaveformComponent::RegionCommand command,
               const double localSeconds) {
            if (launcherEditorContext_) {
                const auto context = *launcherEditorContext_;
                auto changed = false;
                if (command == WaveformComponent::RegionCommand::split)
                    changed = audio_.splitLauncherClipEvent(
                                  context.trackIndex, context.sceneIndex,
                                  eventId, localSeconds) != 0;
                else if (command == WaveformComponent::RegionCommand::remove
                         || command
                                == WaveformComponent::RegionCommand::cut)
                    changed = audio_.removeLauncherClipEvent(
                        context.trackIndex, context.sceneIndex, eventId);
                if (changed) {
                    refreshClipLauncher();
                    refreshClipTimelineEditor();
                    markProjectDirty();
                }
                return;
            }
            if (!selectedTrack_)
                return;
            auto changed = false;
            if (command == WaveformComponent::RegionCommand::split)
                changed = audio_.splitAudioClipEvent(
                              *selectedTrack_, selectedAudioClipId_,
                              eventId, localSeconds) != 0;
            else if (command == WaveformComponent::RegionCommand::remove
                     || command == WaveformComponent::RegionCommand::cut)
                changed = audio_.removeAudioClipEvent(
                    *selectedTrack_, selectedAudioClipId_, eventId);
            if (!changed)
                return;
            refreshTimelineRanges();
            refreshSelectedTrack();
            refreshClipTimelineEditor();
            markProjectDirty();
        });
    clipTimelineEditor_.setStretchMarkerAddHandler(
        [this](const std::uint64_t eventId, const double sourceSeconds,
            const double localSeconds) {
            if (launcherEditorContext_) {
                const auto context = *launcherEditorContext_;
                if (audio_.addLauncherClipEventStretchMarker(
                        context.trackIndex, context.sceneIndex, eventId,
                        sourceSeconds, localSeconds) == 0)
                    return;
                refreshClipTimelineEditor();
                markProjectDirty();
                return;
            }
            if (!selectedTrack_)
                return;
            if (audio_.addAudioClipEventStretchMarker(
                    *selectedTrack_, selectedAudioClipId_, eventId,
                    sourceSeconds, localSeconds) == 0)
                return;
            refreshTimelineRanges();
            refreshClipTimelineEditor();
            markProjectDirty();
        });
    clipTimelineEditor_.setStretchMarkerUpdateHandler(
        [this](const std::uint64_t eventId,
               const std::uint64_t markerId,
               const double localSeconds) {
            if (launcherEditorContext_) {
                const auto context = *launcherEditorContext_;
                if (!audio_.updateLauncherClipEventStretchMarker(
                        context.trackIndex, context.sceneIndex, eventId,
                        markerId, localSeconds))
                    return;
                refreshActiveClipEditorPreview();
                refreshClipTimelineEditor();
                markProjectDirty();
                return;
            }
            if (!selectedTrack_
                || !audio_.updateAudioClipEventStretchMarker(
                    *selectedTrack_, selectedAudioClipId_, eventId,
                    markerId, localSeconds))
                return;
            refreshActiveClipEditorPreview();
            refreshTimelineRanges();
            refreshClipTimelineEditor();
            markProjectDirty();
        });
    // Stretch-marker drags follow the same rule: the marker itself follows the
    // pointer immediately, while the expensive audio plan is committed once.
    clipTimelineEditor_.setStretchMarkerRemoveHandler(
        [this](const std::uint64_t eventId,
               const std::uint64_t markerId) {
            if (launcherEditorContext_) {
                const auto context = *launcherEditorContext_;
                if (!audio_.removeLauncherClipEventStretchMarker(
                        context.trackIndex, context.sceneIndex, eventId,
                        markerId))
                    return;
                refreshClipTimelineEditor();
                markProjectDirty();
                return;
            }
            if (!selectedTrack_
                || !audio_.removeAudioClipEventStretchMarker(
                    *selectedTrack_, selectedAudioClipId_, eventId,
                    markerId))
                return;
            refreshTimelineRanges();
            refreshClipTimelineEditor();
            markProjectDirty();
        });
    clipTimelineEditor_.setAudioDropHandler(
        [this](const juce::File& file, const double localSeconds) {
            if (launcherEditorContext_) {
                const auto context = *launcherEditorContext_;
                const auto result = audio_.addLauncherClipEventFromFile(
                    context.trackIndex, context.sceneIndex, file,
                    std::max(0.0, localSeconds));
                if (result.failed()) {
                    showError(tr("error.openAudioTitle"),
                              result.getErrorMessage());
                    return;
                }
                refreshClipLauncher();
                refreshClipTimelineEditor();
                markProjectDirty();
                return;
            }
            if (!selectedTrack_)
                return;
            const auto result = audio_.addAudioClipEventFromFile(
                *selectedTrack_, selectedAudioClipId_, file,
                std::max(0.0, localSeconds));
            if (result.failed()) {
                showError(tr("error.openAudioTitle"),
                          result.getErrorMessage());
                return;
            }
            refreshTimelineRanges();
            refreshClipTimelineEditor();
            markProjectDirty();
        });
    clipTimelineEditor_.setOpenSourceHandler(
        [this](const std::uint64_t eventId) {
            sourceEditorEventId_ = eventId;
            setEditorMode(5);
            refreshAudioEditor();
        });
    clipTimelineEditor_.setEventSelectionHandler(
        [this](const std::uint64_t eventId) {
            selectedClipEventId_ = eventId;
            if (eventId != 0)
                inspectorSelection_ = InspectorSelection::audioEvent;
            else if (launcherEditorContext_)
                inspectorSelection_ = InspectorSelection::launcherAudioClip;
            else if (selectedAudioClipId_ != 0)
                inspectorSelection_ = InspectorSelection::audioClip;
            // Keep the inspector body and its editable controls on the same
            // selection. Otherwise the labels changed while the sliders kept
            // their disabled/default values from the previous selection.
            refreshSelectedTrack();
        });
    clipTimelineEditor_.setTimelineBoundsHandler(
        [this](const double duration, const double playStart,
               const double playEnd, const double loopStart,
               const double loopEnd, const bool looping) {
            auto changed = false;
            if (launcherEditorContext_) {
                const auto context = *launcherEditorContext_;
                changed = audio_.setLauncherClipTimelineBounds(
                    context.trackIndex, context.sceneIndex, duration,
                    playStart, playEnd, loopStart, loopEnd, looping);
                if (changed)
                    refreshClipLauncher();
            } else if (selectedTrack_ && selectedAudioClipId_ != 0) {
                changed = audio_.setAudioClipTimelineBounds(
                    *selectedTrack_, selectedAudioClipId_, duration,
                    playStart, playEnd, loopStart, loopEnd, looping);
                if (changed) {
                    refreshTimelineRanges();
                    refreshSelectedTrack();
                }
            }
            if (!changed)
                return;
            refreshClipTimelineEditor();
            markProjectDirty();
        });
    clipTimelineEditor_.setViewChangedHandler([this](double, double) {
        if (currentEditorMode_ == 6)
            refreshBottomAutomationEditor();
    });
    clipTimelineEditor_.setTransportHandler(
        [this](const ClipTimelineEditorComponent::TransportCommand command) {
            if (launcherEditorContext_) {
                const auto context = *launcherEditorContext_;
                const auto clip = audio_.launcherClip(
                    context.trackIndex, context.sceneIndex);
                if (!clip.has_value())
                    return;
                if (command
                    == ClipTimelineEditorComponent::TransportCommand::play) {
                    if (audio_.activeLauncherScene(context.trackIndex)
                        == context.sceneIndex) {
                        clipEditorCueSeconds_
                            = audio_.launcherClipPositionSeconds(
                                context.trackIndex);
                        (void) audio_.stopLauncherTrack(
                            context.trackIndex, false);
                    } else
                        (void) audio_.startLauncherClipPreview(
                            context.trackIndex, context.sceneIndex,
                            clipEditorCueSeconds_);
                } else {
                    if (command
                            == ClipTimelineEditorComponent::
                                   TransportCommand::stop
                        && audio_.activeLauncherScene(
                               context.trackIndex)
                               == context.sceneIndex)
                        clipEditorCueSeconds_
                            = audio_.launcherClipPositionSeconds(
                                context.trackIndex);
                    (void) audio_.stopLauncherTrack(context.trackIndex,
                                                    false);
                    if (command == ClipTimelineEditorComponent::
                                       TransportCommand::returnToStart) {
                        clipTimelineEditor_.setPlayhead(
                            clip->playStartSeconds);
                        clipEditorCueSeconds_ = clip->playStartSeconds;
                    }
                }
                refreshClipLauncher();
                return;
            }
            if (!selectedTrack_ || selectedAudioClipId_ == 0)
                return;
            const auto clip = audio_.audioClip(*selectedTrack_,
                                                selectedAudioClipId_);
            if (!clip.has_value())
                return;
            if (command
                == ClipTimelineEditorComponent::TransportCommand::play) {
                if (audio_.isAudioClipPreviewing()) {
                    clipEditorCueSeconds_
                        = audio_.audioClipPreviewPositionSeconds();
                    audio_.stopAudioClipPreview();
                } else
                    (void) audio_.startAudioClipPreview(
                        *selectedTrack_, selectedAudioClipId_,
                        clipEditorCueSeconds_);
            } else {
                if (command
                        == ClipTimelineEditorComponent::
                               TransportCommand::stop
                    && audio_.isAudioClipPreviewing())
                    clipEditorCueSeconds_
                        = audio_.audioClipPreviewPositionSeconds();
                audio_.stopAudioClipPreview();
                if (command == ClipTimelineEditorComponent::
                                   TransportCommand::returnToStart) {
                    clipTimelineEditor_.setPlayhead(
                        clip->playStartSeconds);
                    clipEditorCueSeconds_ = clip->playStartSeconds;
                }
            }
        });
    audioEditor_.setSeekHandler([this](const double sourceSeconds) {
        if (launcherEditorContext_) {
            const auto context = *launcherEditorContext_;
            const auto clip = audio_.launcherClip(
                context.trackIndex, context.sceneIndex);
            if (!clip.has_value())
                return;
            const auto event = std::find_if(
                clip->events.begin(), clip->events.end(),
                [this](const auto& value) {
                    return value.id == sourceEditorEventId_;
                });
            if (event != clip->events.end())
                (void) audio_.startSamplePreview(event->sourceFile,
                                                 sourceSeconds);
            return;
        }
        if (!selectedTrack_)
            return;
        const auto clip = audio_.audioClip(*selectedTrack_, selectedAudioClipId_);
        if (!clip.has_value())
            return;
        if (sourceEditorEventId_ != 0) {
            const auto events = audio_.audioClipEvents(
                *selectedTrack_, selectedAudioClipId_);
            const auto event = std::find_if(
                events.begin(), events.end(), [this](const auto& value) {
                    return value.id == sourceEditorEventId_;
                });
            if (event == events.end())
                return;
            const auto rate = event->sourceDurationSeconds
                              / std::max(0.001, event->durationSeconds);
            audio_.setPosition(std::max(
                0.0, clip->startSeconds + event->startSeconds
                         + (sourceSeconds - event->sourceOffsetSeconds)
                               / std::max(0.000001, rate)));
            return;
        }
        const auto rate = clip->sourceDurationSeconds
                          / std::max(0.001, clip->durationSeconds);
        audio_.setPosition(std::max(
            0.0, clip->startSeconds
                     + (sourceSeconds - clip->sourceOffsetSeconds)
                           / std::max(0.000001, rate)));
    });
    audioEditor_.setStretchMarkerAddHandler(
        [this](const double sourceSeconds,
               const double timelineSeconds) {
            if (!selectedTrack_ || launcherEditorContext_)
                return;
            const auto marker = sourceEditorEventId_ != 0
                ? audio_.addAudioClipEventStretchMarker(
                    *selectedTrack_, selectedAudioClipId_,
                    sourceEditorEventId_, sourceSeconds, timelineSeconds)
                : audio_.addAudioClipStretchMarker(
                    *selectedTrack_, selectedAudioClipId_, sourceSeconds,
                    timelineSeconds);
            if (marker == 0)
                return;
            refreshTimelineRanges();
            refreshAudioEditor();
            markProjectDirty();
        });
    audioEditor_.setStretchMarkerUpdateHandler(
        [this](const std::uint64_t markerId,
               const double timelineSeconds) {
            if (!selectedTrack_ || launcherEditorContext_)
                return;
            const auto updated = sourceEditorEventId_ != 0
                ? audio_.updateAudioClipEventStretchMarker(
                    *selectedTrack_, selectedAudioClipId_,
                    sourceEditorEventId_, markerId, timelineSeconds)
                : audio_.updateAudioClipStretchMarker(
                    *selectedTrack_, selectedAudioClipId_, markerId,
                    timelineSeconds);
            if (!updated)
                return;
            refreshTimelineRanges();
            refreshAudioEditor();
            markProjectDirty();
        });
    audioEditor_.setStretchMarkerRemoveHandler(
        [this](const std::uint64_t markerId) {
            if (!selectedTrack_ || launcherEditorContext_)
                return;
            const auto removed = sourceEditorEventId_ != 0
                ? audio_.removeAudioClipEventStretchMarker(
                    *selectedTrack_, selectedAudioClipId_,
                    sourceEditorEventId_, markerId)
                : audio_.removeAudioClipStretchMarker(
                    *selectedTrack_, selectedAudioClipId_, markerId);
            if (!removed)
                return;
            refreshTimelineRanges();
            refreshAudioEditor();
            markProjectDirty();
        });
    audioEditor_.setStretchMarkerSnapHandler(
        [this](const double relativeSeconds) {
            if (!selectedTrack_ || launcherEditorContext_)
                return relativeSeconds;
            const auto clip = audio_.audioClip(
                *selectedTrack_, selectedAudioClipId_);
            if (!clip.has_value())
                return relativeSeconds;
            if (sourceEditorEventId_ != 0) {
                const auto events = audio_.audioClipEvents(
                    *selectedTrack_, selectedAudioClipId_);
                const auto event = std::find_if(
                    events.begin(), events.end(), [this](const auto& value) {
                        return value.id == sourceEditorEventId_;
                    });
                if (event != events.end())
                    return juce::jlimit(
                        0.001,
                        std::max(0.001,
                                 event->durationSeconds - 0.001),
                        snapArrangementTime(
                            clip->startSeconds + event->startSeconds
                            + relativeSeconds)
                            - clip->startSeconds
                            - event->startSeconds);
            }
            return juce::jlimit(
                0.001, std::max(0.001, clip->durationSeconds - 0.001),
                snapArrangementTime(clip->startSeconds + relativeSeconds)
                    - clip->startSeconds);
        });
    audioEditor_.setTrimHandler([this](const double sourceStart,
                                       const double sourceEnd) {
        if (launcherEditorContext_ && sourceEnd > sourceStart) {
            const auto context = *launcherEditorContext_;
            const auto clip = audio_.launcherClip(
                context.trackIndex, context.sceneIndex);
            if (!clip.has_value())
                return;
            const auto event = std::find_if(
                clip->events.begin(), clip->events.end(),
                [this](const auto& value) {
                    return value.id == sourceEditorEventId_;
                });
            if (event == clip->events.end())
                return;
            const auto rate = event->sourceDurationSeconds
                              / std::max(0.001, event->durationSeconds);
            const auto sourceDuration = sourceEnd - sourceStart;
            if (audio_.updateLauncherClipEvent(
                    context.trackIndex, context.sceneIndex, event->id,
                    event->startSeconds, sourceStart,
                    sourceDuration / std::max(0.000001, rate),
                    sourceDuration, -1.0, -1.0)) {
                refreshClipLauncher();
                refreshClipTimelineEditor();
                refreshAudioEditor();
                markProjectDirty();
            }
            return;
        }
        if (!selectedTrack_ || sourceEnd <= sourceStart)
            return;
        const auto clip = audio_.audioClip(*selectedTrack_, selectedAudioClipId_);
        if (!clip.has_value())
            return;
        if (sourceEditorEventId_ != 0) {
            const auto events = audio_.audioClipEvents(
                *selectedTrack_, selectedAudioClipId_);
            const auto event = std::find_if(
                events.begin(), events.end(), [this](const auto& value) {
                    return value.id == sourceEditorEventId_;
                });
            if (event == events.end())
                return;
            const auto rate = event->sourceDurationSeconds
                              / std::max(0.001, event->durationSeconds);
            const auto sourceDuration = sourceEnd - sourceStart;
            (void) audio_.updateAudioClipEvent(
                *selectedTrack_, selectedAudioClipId_, event->id,
                event->startSeconds, sourceStart,
                sourceDuration / std::max(0.000001, rate),
                sourceDuration, -1.0, -1.0);
            refreshSelectedTrack();
            refreshTimelineRanges();
            refreshClipTimelineEditor();
            markProjectDirty();
            return;
        }
        const auto rate = clip->sourceDurationSeconds
                          / std::max(0.001, clip->durationSeconds);
        const auto sourceDuration = sourceEnd - sourceStart;
        (void) audio_.updateAudioClip(*selectedTrack_, clip->id,
                                     clip->startSeconds, sourceStart,
                                     sourceDuration
                                         / std::max(0.000001, rate),
                                     -1.0, -1.0, sourceDuration);
        refreshSelectedTrack();
        refreshTimelineRanges();
        markProjectDirty();
    });
    audioEditor_.setResetHandler([this] {
        if (launcherEditorContext_) {
            const auto context = *launcherEditorContext_;
            const auto clip = audio_.launcherClip(
                context.trackIndex, context.sceneIndex);
            if (!clip.has_value())
                return;
            const auto event = std::find_if(
                clip->events.begin(), clip->events.end(),
                [this](const auto& value) {
                    return value.id == sourceEditorEventId_;
                });
            if (event == clip->events.end())
                return;
            auto reader = std::unique_ptr<juce::AudioFormatReader>(
                audio_.formatManager().createReaderFor(event->sourceFile));
            if (reader == nullptr || reader->sampleRate <= 0.0)
                return;
            const auto length = static_cast<double>(reader->lengthInSamples)
                                / reader->sampleRate;
            if (audio_.updateLauncherClipEvent(
                    context.trackIndex, context.sceneIndex, event->id,
                    event->startSeconds, 0.0, length, length,
                    -1.0, -1.0)) {
                refreshClipLauncher();
                refreshClipTimelineEditor();
                refreshAudioEditor();
                markProjectDirty();
            }
            return;
        }
        if (!selectedTrack_)
            return;
        const auto clip = audio_.audioClip(*selectedTrack_, selectedAudioClipId_);
        if (!clip.has_value())
            return;
        if (sourceEditorEventId_ != 0) {
            const auto events = audio_.audioClipEvents(
                *selectedTrack_, selectedAudioClipId_);
            const auto event = std::find_if(
                events.begin(), events.end(), [this](const auto& value) {
                    return value.id == sourceEditorEventId_;
                });
            if (event == events.end())
                return;
            (void) audio_.updateAudioClipEvent(
                *selectedTrack_, selectedAudioClipId_, event->id,
                event->startSeconds, 0.0, event->sourceLengthSeconds,
                event->sourceLengthSeconds, -1.0, -1.0);
            refreshSelectedTrack();
            refreshTimelineRanges();
            refreshClipTimelineEditor();
            refreshAudioEditor();
            markProjectDirty();
            return;
        }
        (void) audio_.updateAudioClip(*selectedTrack_, clip->id,
                                     clip->startSeconds, 0.0,
                                     clip->sourceLengthSeconds,
                                     -1.0, -1.0,
                                     clip->sourceLengthSeconds);
        refreshSelectedTrack();
        refreshTimelineRanges();
        markProjectDirty();
    });
    audioEditor_.setExportHandler([this](const double sourceStart,
                                          const double sourceEnd) {
        if (!selectedTrack_ || sourceEnd <= sourceStart)
            return;
        const auto trackIndex = *selectedTrack_;
        const auto clip = audio_.audioClip(trackIndex, selectedAudioClipId_);
        if (!clip.has_value())
            return;
        auto source = clip->sourceFile;
        if (sourceEditorEventId_ != 0) {
            const auto events = audio_.audioClipEvents(
                trackIndex, selectedAudioClipId_);
            const auto event = std::find_if(
                events.begin(), events.end(), [this](const auto& value) {
                    return value.id == sourceEditorEventId_;
                });
            if (event != events.end())
                source = event->sourceFile;
        }
        const auto suggested = source.getSiblingFile(
            source.getFileNameWithoutExtension() + "-selection.wav");
        chooser_ = std::make_unique<juce::FileChooser>(tr("dialog.exportSelection"),
                                                       suggested, "*.wav");
        chooser_->launchAsync(juce::FileBrowserComponent::saveMode
                                  | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, trackIndex, source, sourceStart, sourceEnd](
                                  const juce::FileChooser& chooser) {
                                  auto destination = chooser.getResult();
                                  if (destination == juce::File {})
                                      return;
                                  destination = destination.withFileExtension("wav");
                                  exportAudioSelection(
                                      trackIndex, source, sourceStart, sourceEnd,
                                      destination);
                              });
    });
    audioEditor_.setProcessHandler(
        [this](const AudioEditRequest request,
               const double sourceStart,
               const double sourceEnd) {
            renderAudioSelection(request, sourceStart, sourceEnd);
        });
    audioEditor_.setUndoHandler([this] { undoAudioEdit(); });
    audioEditor_.setRedoHandler([this] { redoAudioEdit(); });
    audioEditor_.setCancelHandler([this] {
        if (audioEditCancellation_ == nullptr)
            return;
        audioEditCancellation_->store(true, std::memory_order_release);
        audioEditor_.showStatus(tr("audioEditor.cancelling"));
    });
    clipLauncher_.setCaptureHandler([this](const std::size_t trackIndex,
                                            const int sceneIndex) {
        toggleLauncherRecording(trackIndex, sceneIndex);
    });
    clipLauncher_.setLaunchHandler([this](const std::size_t trackIndex,
                                           const int sceneIndex) {
        if (const auto midiTrack = launcherMidiTrackId(trackIndex)) {
            const auto clip = launcherMidiClip(*midiTrack, sceneIndex);
            if (clip.has_value()
                && audio_.startMidiClipPreview(
                    *midiTrack, clip->id, sceneIndex)) {
                arrangementPlaybackButton_.setToggleState(
                    false, juce::dontSendNotification);
                refreshArrangementInteractionMode();
                refreshClipLauncher();
            }
            return;
        }
        if (audio_.launchClip(trackIndex, sceneIndex)) {
            arrangementPlaybackButton_.setToggleState(
                false, juce::dontSendNotification);
            refreshArrangementInteractionMode();
            recordLauncherAction({ audio_.position(),
                                   LauncherAction::Type::launchClip,
                                   trackIndex, sceneIndex });
            refreshClipLauncher();
        }
    });
    clipLauncher_.setTrackStopHandler([this](const std::size_t trackIndex) {
        if (const auto midiTrack = launcherMidiTrackId(trackIndex)) {
            (void) audio_.stopMidiClipPreview(*midiTrack);
            refreshClipLauncher();
            return;
        }
        if (audio_.stopLauncherTrack(trackIndex, true)) {
            recordLauncherAction({ audio_.position(),
                                   LauncherAction::Type::stopTrack,
                                   trackIndex, -1 });
            refreshClipLauncher();
        }
    });
    clipLauncher_.setTrackMoveHandler(
        [this](const std::size_t fromIndex, const std::size_t toIndex) {
            moveLauncherTrack(fromIndex, toIndex);
        });
    clipLauncher_.setClearHandler([this](const std::size_t trackIndex,
                                          const int sceneIndex) {
        if (const auto midiTrack = launcherMidiTrackId(trackIndex)) {
            const auto clip = launcherMidiClip(*midiTrack, sceneIndex);
            if (clip.has_value()
                && audio_.removeMidiClip(*midiTrack, clip->id)) {
                if (selectedMidiClipId_ == clip->id)
                    selectedMidiClipId_ = 0;
                refreshClipLauncher();
                markProjectDirty();
            }
            return;
        }
        if (audio_.clearLauncherClip(trackIndex, sceneIndex)) {
            if (launcherEditorContext_
                && launcherEditorContext_->trackIndex == trackIndex
                && launcherEditorContext_->sceneIndex == sceneIndex)
                launcherEditorContext_.reset();
            if (launcherInspectorSelection_
                && launcherInspectorSelection_->trackIndex == trackIndex
                && launcherInspectorSelection_->sceneIndex == sceneIndex) {
                launcherInspectorSelection_.reset();
                inspectorSelection_ = InspectorSelection::audioTrack;
                updateTrackDescription();
            }
            refreshClipLauncher();
            markProjectDirty();
        }
    });
    clipLauncher_.setSlotLoopHandler(
        [this](const std::size_t trackIndex, const int sceneIndex,
               const bool looping) {
            if (const auto midiTrack = launcherMidiTrackId(trackIndex)) {
                const auto clip = launcherMidiClip(*midiTrack, sceneIndex);
                if (clip.has_value()
                    && audio_.setMidiClipLooping(*midiTrack, clip->id,
                                                 looping)) {
                    refreshClipLauncher();
                    markProjectDirty();
                }
                return;
            }
            const auto clips = audio_.launcherClips(trackIndex);
            const auto found = std::find_if(
                clips.begin(), clips.end(),
                [sceneIndex](const auto& clip) {
                    return clip.sceneIndex == sceneIndex;
                });
            if (found == clips.end()
                || !audio_.setLauncherClipTimelineBounds(
                    trackIndex, sceneIndex, found->durationSeconds,
                    found->playStartSeconds, found->playEndSeconds,
                    found->loopStartSeconds, found->loopEndSeconds,
                    looping))
                return;
            refreshClipLauncher();
            markProjectDirty();
        });
    clipLauncher_.setSlotOpenHandler(
        [this](const std::size_t trackIndex, const int sceneIndex) {
            if (const auto midiTrack = launcherMidiTrackId(trackIndex)) {
                const auto clip = launcherMidiClip(*midiTrack,
                                                   sceneIndex);
                if (!clip.has_value())
                    return;
                selectLauncherMidiTrack(*midiTrack);
                selectedMidiClipId_ = clip->id;
                inspectorSelection_ = InspectorSelection::midiClip;
                launcherEditorContext_.reset();
                setEditorMode(2);
                refreshMidiEditor();
                if (editorWindow_ != nullptr)
                    editorWindow_->toFront(true);
                return;
            }
            const auto clips = audio_.launcherClips(trackIndex);
            const auto found = std::find_if(
                clips.begin(), clips.end(),
                [sceneIndex](const auto& clip) {
                    return clip.sceneIndex == sceneIndex;
                });
            if (found == clips.end())
                return;
            selectTrack(trackIndex);
            selectedAudioClipId_ = 0;
            selectedAudioClipIds_.clear();
            launcherEditorContext_ = LauncherEditorContext {
                trackIndex, sceneIndex
            };
            launcherInspectorSelection_ = launcherEditorContext_;
            inspectorSelection_ = InspectorSelection::launcherAudioClip;
            selectedClipEventId_ = 0;
            clipEditorCueSeconds_ = found->playStartSeconds;
            sourceEditorEventId_ = 0;
            setEditorMode(6);
            refreshClipTimelineEditor();
            updateTrackDescription();
            if (editorWindow_ != nullptr)
                editorWindow_->toFront(true);
        });
    clipLauncher_.setEmptySlotCreateHandler(
        [this](const std::size_t trackIndex, const int sceneIndex) {
            if (const auto midiTrack = launcherMidiTrackId(trackIndex)) {
                const auto clipId = audio_.addMidiClip(
                    *midiTrack,
                    tr("midi.clip") + " "
                        + juce::String(sceneIndex + 1),
                    static_cast<double>(sceneIndex) * 16.0, 16.0,
                    true, trackAccentColours[
                        (trackIndex - audio_.trackCount())
                        % trackAccentColours.size()]);
                if (clipId == 0)
                    return;
                selectLauncherMidiTrack(*midiTrack);
                selectedMidiClipId_ = clipId;
                inspectorSelection_ = InspectorSelection::midiClip;
                setEditorMode(2);
                refreshMidiEditor();
                markProjectDirty();
                if (editorWindow_ != nullptr)
                    editorWindow_->toFront(true);
                return;
            }
            const auto duration = 16.0 * 60.0
                                  / std::max(20.0, audio_.tempo());
            static constexpr std::array<std::uint32_t, 8> colours {
                0xffff8a00, 0xfff5c542, 0xff7ddc71, 0xff4fd3ae,
                0xff4ea5ff, 0xff8b7cf6, 0xffd46ee8, 0xffff6f7d
            };
            if (!audio_.createEmptyLauncherClip(
                    trackIndex, sceneIndex,
                    tr("editor.clip") + " "
                        + juce::String(sceneIndex + 1),
                    duration, colours[trackIndex % colours.size()]))
                return;
            selectTrack(trackIndex);
            selectedAudioClipId_ = 0;
            selectedAudioClipIds_.clear();
            launcherEditorContext_ = LauncherEditorContext {
                trackIndex, sceneIndex
            };
            launcherInspectorSelection_ = launcherEditorContext_;
            inspectorSelection_ = InspectorSelection::launcherAudioClip;
            selectedClipEventId_ = 0;
            clipEditorCueSeconds_ = 0.0;
            sourceEditorEventId_ = 0;
            setEditorMode(6);
            refreshClipLauncher();
            refreshClipTimelineEditor();
            updateTrackDescription();
            markProjectDirty();
            if (editorWindow_ != nullptr)
                editorWindow_->toFront(true);
        });
    clipLauncher_.setLaunchSceneHandler([this](const int sceneIndex) {
        auto launched = audio_.launchScene(sceneIndex);
        for (const auto& midiTrack : audio_.midiTracks()) {
            const auto clip = launcherMidiClip(midiTrack.id,
                                               sceneIndex);
            if (clip.has_value())
                launched = audio_.startMidiClipPreview(
                               midiTrack.id, clip->id, sceneIndex)
                           || launched;
            else
                (void) audio_.stopMidiClipPreview(midiTrack.id);
        }
        if (launched) {
            arrangementPlaybackButton_.setToggleState(
                false, juce::dontSendNotification);
            refreshArrangementInteractionMode();
            recordLauncherAction({ audio_.position(),
                                   LauncherAction::Type::launchScene,
                                   0, sceneIndex });
            refreshClipLauncher();
        }
    });
    clipLauncher_.setQuantizationHandler([this](const int quantizationIndex) {
        audio_.setLaunchQuantization(
            static_cast<AudioTrackController::LaunchQuantization>(quantizationIndex));
        refreshClipLauncher();
        markProjectDirty();
    });
    clipLauncher_.setTrackSelectHandler([this](const std::size_t trackIndex) {
        if (const auto midiTrack = launcherMidiTrackId(trackIndex)) {
            selectLauncherMidiTrack(*midiTrack);
            return;
        }
        selectTrack(trackIndex);
        refreshClipLauncher();
    });
    clipLauncher_.setSlotSelectHandler(
        [this](const std::size_t trackIndex, const int sceneIndex) {
            if (const auto midiTrack = launcherMidiTrackId(trackIndex)) {
                const auto clip = launcherMidiClip(*midiTrack, sceneIndex);
                if (!clip.has_value())
                    return;
                selectLauncherMidiTrack(*midiTrack);
                selectedMidiClipId_ = clip->id;
                inspectorSelection_ = InspectorSelection::midiClip;
                updateTrackDescription();
                return;
            }
            if (trackIndex >= audio_.trackCount()
                || !audio_.launcherClip(trackIndex, sceneIndex).has_value())
                return;
            selectTrack(trackIndex);
            launcherInspectorSelection_ = LauncherEditorContext {
                trackIndex, sceneIndex
            };
            inspectorSelection_ = InspectorSelection::launcherAudioClip;
            selectedClipEventId_ = 0;
            updateTrackDescription();
        });
    clipLauncher_.setAddTrackHandler([this] {
        // The launcher fills most of the window. Anchoring a popup to that
        // whole component can place it far from the small + button, so use
        // the actual mouse position here.
        showAddTrackMenu(nullptr);
    });
    clipLauncher_.setTrackContextHandler(
        [this](const std::size_t trackIndex, juce::Component* target) {
            if (launcherMidiTrackId(trackIndex))
                return;
            showTrackContextMenu(trackIndex, target);
        });
    clipLauncher_.setDeviceAddHandler([this](const std::size_t trackIndex) {
        if (const auto midiTrack = launcherMidiTrackId(trackIndex))
            selectLauncherMidiTrack(*midiTrack);
        else
            selectTrack(trackIndex);
        setBottomPanelMode(1);
        if (!browserPanelVisible_) {
            auto state = captureLayoutState();
            state.browserVisible = true;
            applyLayoutState(state);
        }
        browser_.focusForDeviceInsertion();
        refreshClipLauncher();
    });
    clipLauncher_.setSampleDropHandler(
        [this](const std::size_t trackIndex, const int sceneIndex,
               const juce::File& file) {
            insertLauncherSample(trackIndex, sceneIndex, file);
        });
    clipLauncher_.setClipTransferHandler(
        [this](const std::size_t sourceTrack, const int sourceScene,
               const std::size_t targetTrack, const int targetScene,
               const bool copy) {
            if (!audio_.transferLauncherClip(
                    sourceTrack, sourceScene, targetTrack, targetScene,
                    copy))
                return;
            selectTrack(targetTrack);
            transientStatus_ = tr(
                copy ? "status.launcherClipCopied"
                     : "status.launcherClipMoved",
                "scene", juce::String(targetScene + 1));
            transientStatusExpiryMs_ =
                juce::Time::currentTimeMillis() + 5'000;
            refreshClipLauncher();
            markProjectDirty();
        });
    clipLauncher_.setDeviceDropHandler(
        [this](const std::size_t trackIndex, const juce::var& payload) {
            if (trackIndex >= audio_.trackCount())
                return;
            selectTrack(trackIndex);
            setBottomPanelMode(1);
            if (const auto deviceKey =
                    factoryDeviceFromDragPayload(payload)) {
                const auto result = nodeGraph_.insertFactoryDevice(*deviceKey);
                if (result.failed()) {
                    showError(tr("error.deviceInsertTitle"),
                              result.getErrorMessage());
                    return;
                }
                transientStatus_ = tr("status.deviceDropped", "device",
                                      *deviceKey);
                refreshNodeDeviceSurface();
            } else if (const auto pluginFile =
                           pluginFileFromDragPayload(payload)) {
                std::size_t pluginIndex {};
                const auto result = audio_.setTrackPlugin(
                    trackIndex, *pluginFile, &pluginIndex);
                if (result.failed()) {
                    showError(tr("error.pluginInsertTitle"),
                              result.getErrorMessage());
                    return;
                }
                selectedPluginIndex_ = pluginIndex;
                transientStatus_ = tr("status.pluginDropped", "plugin",
                                      pluginFile->getFileNameWithoutExtension());
                refreshAutomationTargets();
            } else {
                return;
            }
            transientStatusExpiryMs_ =
                juce::Time::currentTimeMillis() + 5'000;
            refreshSelectedTrack();
            refreshClipLauncher();
            markProjectDirty();
        });
    clipLauncher_.setSlotBrowseHandler(
        [this](const std::size_t trackIndex, const int sceneIndex) {
            selectTrack(trackIndex);
            if (!browserPanelVisible_) {
                auto state = captureLayoutState();
                state.browserVisible = true;
                applyLayoutState(state);
            }
            const auto safe = juce::Component::SafePointer<
                WorkspaceComponent>(this);
            const auto targetLabel = tr(
                "launcher.audioTrack", "index",
                juce::String(trackIndex + 1))
                + " / "
                + tr("launcher.scene", "count",
                     juce::String(sceneIndex + 1));
            browser_.focusForSampleInsertion(
                [safe, trackIndex, sceneIndex](const juce::File& file) {
                    if (safe != nullptr)
                        safe->insertLauncherSample(trackIndex, sceneIndex,
                                                   file);
                }, targetLabel);
            refreshClipLauncher();
        });
    clipLauncher_.setMixerHandler(
        [this](const std::size_t trackIndex, const float gainDb,
               const float pan) { applyChannelMix(trackIndex, gainDb, pan); });
    clipLauncher_.setMasterMixerHandler(
        [this](const float gainDb) { applyMasterGain(gainDb); });
    clipLauncher_.setMuteHandler(
        [this](const std::size_t trackIndex, const bool muted) {
            applyChannelMute(trackIndex, muted);
        });
    clipLauncher_.setSoloHandler(
        [this](const std::size_t trackIndex, const bool solo) {
            applyChannelSolo(trackIndex, solo);
        });
    clipLauncher_.setArmHandler(
        [this](const std::size_t trackIndex, const bool armed) {
            applyChannelArm(trackIndex, armed);
        });
    mixer_.setMixHandler(
        [this](const std::size_t channelIndex, const float gainDb,
               const float pan) {
            applyChannelMix(channelIndex, gainDb, pan);
        });
    mixer_.setMasterGainHandler(
        [this](const float gainDb) { applyMasterGain(gainDb); });
    mixer_.setMuteHandler(
        [this](const std::size_t channelIndex, const bool muted) {
            applyChannelMute(channelIndex, muted);
        });
    mixer_.setSoloHandler(
        [this](const std::size_t channelIndex, const bool solo) {
            applyChannelSolo(channelIndex, solo);
        });
    mixer_.setArmHandler(
        [this](const std::size_t channelIndex, const bool armed) {
            applyChannelArm(channelIndex, armed);
        });
    mixer_.setSelectHandler([this](const std::size_t channelIndex) {
        if (const auto midiTrack = launcherMidiTrackId(channelIndex))
            selectLauncherMidiTrack(*midiTrack);
        else
            selectTrack(channelIndex);
        refreshMixer();
    });
    mixer_.setAddTrackHandler([this] { showAddTrackMenu(nullptr); });
    mixer_.setMoveHandler(
        [this](const std::size_t fromIndex, const std::size_t toIndex) {
            moveLauncherTrack(fromIndex, toIndex);
        });
    mixer_.setContextHandler([this](const std::size_t channelIndex) {
        if (launcherMidiTrackId(channelIndex))
            return;
        // The mixer fills the editor area; anchoring to it would put the
        // menu far from the strip, so it opens at the mouse instead.
        showTrackContextMenu(channelIndex, nullptr);
    });
    mixer_.setInputHandler([this](const std::size_t channelIndex) {
        showTrackInputBusMenu(channelIndex);
    });
    mixer_.setDevicesOpenHandler([this](const std::size_t channelIndex) {
        if (const auto midiTrack = launcherMidiTrackId(channelIndex))
            selectLauncherMidiTrack(*midiTrack);
        else
            selectTrack(channelIndex);
        setEditorMode(7);
    });
    nodeGraph_.setLayoutChangedHandler([this] {
        auto mark = juce::Time::getHighResolutionTicks();
        const auto since = [&mark] {
            const auto now = juce::Time::getHighResolutionTicks();
            const auto elapsed = juce::Time::highResolutionTicksToSeconds(
                                     now - mark) * 1000.0;
            mark = now;
            return juce::String(elapsed, 2);
        };
        publishNativeDspGraph();
        const auto publish = since();
        scheduleNodeDeviceSurfaceRefresh();
        const auto devices = since();
        refreshModulationTargets();
        const auto modulation = since();
        markProjectDirty();
        layoutHandlerProfile_ = "publish=" + publish + " devices=" + devices
                                + " modulation=" + modulation
                                + " dirty=" + since();
    });
    nodeGraph_.setParametersChangedHandler([this] {
        onGraphParametersChanged();
    });
    modulationGraph_.setLayoutChangedHandler([this] {
        saveCurrentModulationState();
        markProjectDirty();
    });
    modulationGraph_.setParametersChangedHandler([this] {
        saveCurrentModulationState();
        markProjectDirty();
    });
    publishNativeDspGraph();
    refreshNodeDeviceSurface();
    deviceChain_.setNodeParameterChangedHandler(
        [this](const juce::String& binding, const double value) {
            if (!nodeGraph_.setGraphInterfaceParameter(binding, value))
                return;
            publishNativeDspParameters();
            markProjectDirty();
        });
    deviceChain_.setNodeOpenHandler([this] {
        nodeGraph_.showGraphInterfaceEditor();
    });
    deviceChain_.setDeviceAddHandler([this] {
        if (!browserPanelVisible_) {
            auto state = captureLayoutState();
            state.browserVisible = true;
            applyLayoutState(state);
        }
        browser_.focusForDeviceInsertion();
    });
    nodeGraph_.setEffectChangedHandler([this](const float driveDb, const float cutoffHz) {
        if (!selectedTrack_)
            return;
        audio_.setTrackEffects(*selectedTrack_, driveDb, cutoffHz);
        const auto info = audio_.trackInfo(*selectedTrack_);
        deviceChain_.setParameters(driveDb, cutoffHz, info.gainDb, info.pan, true);
        markProjectDirty();
    });
    deviceChain_.setEffectChangedHandler([this](const float driveDb, const float cutoffHz) {
        if (!selectedTrack_)
            return;
        audio_.setTrackEffects(*selectedTrack_, driveDb, cutoffHz);
        nodeGraph_.setEffectParameters(driveDb, cutoffHz, true);
        markProjectDirty();
    });
    deviceChain_.setMixerChangedHandler([this](const float gainDb, const float pan) {
        if (!selectedTrack_ && selectedMidiTrackId_ == 0)
            return;
        gainSlider_.setValue(gainDb, juce::dontSendNotification);
        panSlider_.setValue(pan, juce::dontSendNotification);
        applyMixerSettings();
        markProjectDirty();
    });
    deviceChain_.setPluginSelectHandler([this](const std::size_t pluginIndex) {
        selectedPluginIndex_ = pluginIndex;
        refreshDevicePluginChain();
    });
    deviceChain_.setPluginRemoveHandler([this](const std::size_t pluginIndex) {
        if (!selectedTrack_)
            return;
        audio_.clearTrackPlugin(*selectedTrack_, pluginIndex);
        if (selectedPluginIndex_ > 0)
            --selectedPluginIndex_;
        refreshSelectedTrack();
        automationTargetId_ = "gain";
        refreshAutomationTargets();
        markProjectDirty();
    });
    deviceChain_.setPluginOpenHandler([this](const std::size_t pluginIndex) {
        if (!selectedTrack_)
            return;
        const auto result = audio_.showTrackPluginEditor(*selectedTrack_, pluginIndex);
        if (result.failed())
            showError(tr("error.pluginEditorTitle"), result.getErrorMessage());
    });
    deviceChain_.setPluginMoveHandler([this](const std::size_t pluginIndex, const int direction) {
        if (!selectedTrack_ || !audio_.moveTrackPlugin(*selectedTrack_, pluginIndex, direction))
            return;
        selectedPluginIndex_ = static_cast<std::size_t>(
            static_cast<std::ptrdiff_t>(pluginIndex) + (direction < 0 ? -1 : 1));
        refreshSelectedTrack();
        automationTargetId_ = "gain";
        refreshAutomationTargets();
        markProjectDirty();
    });
    const auto savedAudioState = recoveryEnabled_
                                     ? juce::parseXML(
                                           layoutProperties_.getUserSettings()
                                               ->getValue("audioDeviceState"))
                                     : nullptr;
    const auto audioError = audio_.initialiseAudio(savedAudioState.get());
    if (audioError.isNotEmpty())
        statusLabel_.setText("Audio error: " + audioError, juce::dontSendNotification);
    else
        statusLabel_.setText(audio_.audioDeviceDescription(), juce::dontSendNotification);

    recordButton_.setColour(juce::TextButton::textColourOffId,
                            juce::Colour(theme::record));
    armButton_.setColour(juce::ToggleButton::tickColourId,
                         juce::Colour(0xffff5864));
    playButton_.setColour(juce::TextButton::textColourOffId,
                          juce::Colour(0xffffa229));
    devicesPanelButton_.setClickingTogglesState(false);
    graphPanelButton_.setClickingTogglesState(false);
    modulationPanelButton_.setClickingTogglesState(false);
    setBottomPanelMode(bottomPanelMode_);
    refreshSelectedTrack();
    refreshMidiEditor();
    refreshAutomationTargets();
    refreshClipLauncher();
    registerShortcutListener(*this);
    nextProjectAutosaveMs_ = juce::Time::currentTimeMillis() + 30'000;
    startTimerHz(30);
    if (recoveryEnabled_) {
        juce::Component::SafePointer<WorkspaceComponent> safeThis(this);
        juce::Timer::callAfterDelay(500, [safeThis] {
            if (safeThis != nullptr)
                safeThis->offerProjectRecovery();
        });
    }
}

WorkspaceComponent::~WorkspaceComponent()
{
    stopTimer();
    if (audio_.hasRecordingSession())
        finishAudioRecording(false);
    if (recoveryEnabled_)
        saveAudioDevicePreferences();
    (void) projectAutosavePool_.removeAllJobs(true, 10'000);
    projectAutosaveInProgress_.store(false, std::memory_order_release);
    if (recoveryEnabled_) {
        if (projectDirty_)
            (void) writeProjectRecoveryNow();
        else
            discardProjectRecovery();
    }
    timelineScrollBar_.removeListener(this);
    if (audio_.isMasterExporting())
        audio_.cancelMasterExport();
    if (audioEditCancellation_ != nullptr)
        audioEditCancellation_->store(true, std::memory_order_release);
    (void) audioEditPool_.removeAllJobs(true, 10'000);
    Localization::instance().removeChangeListener(this);
    audio_.stopRecording();
    saveLayoutPreferences();
    dockAllPanelWindows();
    dockGraphWindow();
    trackViewport_.setViewedComponent(nullptr, false);
    setLookAndFeel(nullptr);
}

void WorkspaceComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(background));
    graphics.setColour(juce::Colour(theme::chrome));
    graphics.fillRect(getLocalBounds().removeFromTop(toolbarHeight));
    graphics.fillRect(getLocalBounds().removeFromBottom(statusBarHeight));

    // The transport and the clock read as instruments set into the bar rather
    // than as four more buttons in the row.
    const auto well = [&graphics](const juce::Rectangle<int>& bounds) {
        if (bounds.isEmpty())
            return;
        graphics.setColour(juce::Colour(0xff0a0c10));
        graphics.fillRoundedRectangle(bounds.toFloat(), 4.0f);
        graphics.setColour(juce::Colour(0xff2b323d));
        graphics.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 4.0f, 1.0f);
    };
    well(transportWell_);
    well(clockField_);

    graphics.setColour(juce::Colour(0xff272d36));
    for (const auto x : toolbarSeparators_)
        graphics.fillRect(x, 15, 1, toolbarHeight - 30);

    graphics.setColour(juce::Colour(border));
    graphics.drawHorizontalLine(toolbarHeight, 0.0f,
                                static_cast<float>(getWidth()));
    graphics.drawHorizontalLine(getHeight() - statusBarHeight, 0.0f,
                                static_cast<float>(getWidth()));
}

void WorkspaceComponent::resized()
{
    auto area = getLocalBounds();
    auto toolbar = area.removeFromTop(toolbarHeight).reduced(8, 11);
    const auto controlHeight = toolbar.getHeight();
    toolbarSeparators_.clear();
    // A rule between groups, not a gap: the eye needs a boundary to read the
    // row as "menus, then views, then commands" rather than as one long list.
    const auto separateLeft = [this, &toolbar] {
        toolbar.removeFromLeft(5);
        toolbarSeparators_.push_back(toolbar.removeFromLeft(1).getX());
        toolbar.removeFromLeft(5);
    };
    const auto separateRight = [this, &toolbar] {
        toolbar.removeFromRight(5);
        toolbarSeparators_.push_back(toolbar.removeFromRight(1).getX());
        toolbar.removeFromRight(5);
    };
    // Every width below is measured from the label it has to hold. Hard-coded
    // pixel widths were tuned against the English strings and silently clipped
    // every longer translation.
    const auto fit = [controlHeight](const juce::TextButton& button,
                                     const int minimum = 0) {
        return preferredTextButtonWidth(button, controlHeight, minimum);
    };
    const auto fitBox = [controlHeight](const juce::ComboBox& box,
                                        const int minimum = 0) {
        return preferredComboBoxWidth(box, controlHeight, minimum);
    };
    // The native title bar already identifies the application. Keeping the
    // project label in this compact action row shifted every primary command
    // away from the left edge and broke alignment with the workspace below.
    projectLabel_.setVisible(false);
    projectLabel_.setBounds({});
    for (auto* menu : { &fileMenuButton_, &editMenuButton_, &viewMenuButton_ })
        menu->setBounds(toolbar.removeFromLeft(fit(*menu)).reduced(1, 0));
    separateLeft();

    const auto showToolbarUtilities = getWidth() >= 1'750;
    audioSettingsButton_.setVisible(showToolbarUtilities);
    languageSelector_.setVisible(showToolbarUtilities);
    commandSearchButton_.setVisible(showToolbarUtilities);
    if (showToolbarUtilities) {
        audioSettingsButton_.setBounds(
            toolbar.removeFromRight(fit(audioSettingsButton_)).reduced(3, 0));
        languageSelector_.setBounds(
            toolbar.removeFromRight(fitBox(languageSelector_)).reduced(3, 0));
        commandSearchButton_.setBounds(
            toolbar.removeFromRight(fit(commandSearchButton_)).reduced(3, 0));
        separateRight();
    }
    const auto compactToolbar = getWidth() < 2'050;
    for (auto* button : { &saveLayoutButton_, &deleteLayoutButton_,
                          &undoLayoutButton_, &redoLayoutButton_ })
        button->setVisible(!compactToolbar);
    if (!compactToolbar) {
        redoLayoutButton_.setBounds(toolbar.removeFromRight(28).reduced(2, 0));
        undoLayoutButton_.setBounds(toolbar.removeFromRight(28).reduced(2, 0));
        deleteLayoutButton_.setBounds(toolbar.removeFromRight(28).reduced(2, 0));
        saveLayoutButton_.setBounds(
            toolbar.removeFromRight(fit(saveLayoutButton_)).reduced(2, 0));
    }
    layoutPanelsButton_.setBounds(
        toolbar.removeFromRight(fit(layoutPanelsButton_)).reduced(2, 0));
    layoutSelector_.setBounds(
        toolbar.removeFromRight(fitBox(layoutSelector_, 120)).reduced(3, 0));
    separateRight();
    const auto narrowToolbar = getWidth() < 1'560;
    detachGraphButton_.setVisible(!narrowToolbar);
    if (!narrowToolbar)
        detachGraphButton_.setBounds(
            toolbar.removeFromRight(fit(detachGraphButton_)).reduced(3, 0));
    if (!narrowToolbar)
        separateRight();

    // Transport glyphs are square by design: they read as one instrument, not
    // as four labels of different lengths. The well painted behind them says
    // the same thing to the eye.
    toolbar.removeFromRight(4);
    loopButton_.setBounds(toolbar.removeFromRight(40).reduced(2, 1));
    recordButton_.setBounds(toolbar.removeFromRight(40).reduced(2, 1));
    stopButton_.setBounds(toolbar.removeFromRight(40).reduced(2, 1));
    playButton_.setBounds(toolbar.removeFromRight(40).reduced(2, 1));
    transportWell_ = playButton_.getBounds()
                         .getUnion(loopButton_.getBounds())
                         .expanded(5, 3);
    toolbar.removeFromRight(9);
    tempoSlider_.setBounds(toolbar.removeFromRight(96).reduced(5, 0));
    separateRight();
    // In the arrangement the five tool buttons below already show and set the
    // tool, and they show which one is active. Keeping the selector here as
    // well put the same state on screen twice and pushed the clock off the bar.
    const auto toolButtonsVisible = currentEditorMode_ == 1;
    arrangementToolSelector_.setVisible(!toolButtonsVisible);
    if (!toolButtonsVisible)
        arrangementToolSelector_.setBounds(
            toolbar.removeFromRight(fitBox(arrangementToolSelector_)).reduced(3, 0));
    snapTypeSelector_.setBounds(
        toolbar.removeFromRight(fitBox(snapTypeSelector_)).reduced(3, 0));
    snapSelector_.setBounds(
        toolbar.removeFromRight(fitBox(snapSelector_)).reduced(3, 0));
    separateRight();
    // The clock belongs with the transport, not stretched across whatever gap
    // the rest of the bar happens to leave. It gets a field of its own width,
    // reserved before anything on the left can claim the room.
    clockField_ = toolbar.removeFromRight(
        std::min(150, std::max(0, toolbar.getWidth())));
    positionLabel_.setBounds(clockField_.reduced(6, 0));

    // What is left belongs to the left-hand groups, in order of importance.
    // Their widths are measured rather than assumed: a pixel threshold is
    // tuned against one language, and the French labels are wider.
    const std::array<juce::TextButton*, 7> viewTabs {
        &launcherViewButton_, &arrangementViewButton_, &clipViewButton_,
        &graphEditViewButton_, &busViewButton_, &mixerViewButton_,
        &samplesViewButton_
    };
    auto tabsWidth = 11;
    for (const auto* tab : viewTabs)
        tabsWidth += fit(*tab);
    const auto showWorkspaceTabs = toolbar.getWidth() >= tabsWidth;
    editorModeSelector_.setVisible(!showWorkspaceTabs);
    for (auto* tab : viewTabs)
        tab->setVisible(showWorkspaceTabs);
    if (showWorkspaceTabs) {
        for (auto* tab : viewTabs)
            tab->setBounds(toolbar.removeFromLeft(fit(*tab)).reduced(2, 0));
        separateLeft();
    } else {
        // The compact selector stands in for the tabs where they stand, so
        // the navigation does not move to the other end of the bar when the
        // window narrows.
        editorModeSelector_.setBounds(
            toolbar.removeFromLeft(fitBox(editorModeSelector_, 130)).reduced(2, 0));
        separateLeft();
    }

    // Open, save, load and export are all in the File menu and on
    // Ctrl+O / S / L / E. They are shortcuts, so they are the first thing to
    // give up their room — the navigation is not.
    const std::array<juce::TextButton*, 4> fileActions {
        &openAudioButton_, &saveProjectButton_, &loadProjectButton_,
        &masterExportButton_
    };
    auto fileActionsWidth = 51;
    for (const auto* button : fileActions)
        fileActionsWidth += fit(*button);
    const auto showFileActions = toolbar.getWidth() >= fileActionsWidth;
    for (auto* button : fileActions)
        button->setVisible(showFileActions);
    if (showFileActions) {
        for (auto* button : fileActions)
            button->setBounds(toolbar.removeFromLeft(fit(*button)).reduced(2, 0));
        separateLeft();
    }

    auto statusArea = area.removeFromBottom(statusBarHeight);
    statusLabel_.setBounds(statusArea.reduced(10, 0));
    area.reduce(4, 4);

    juce::Component* verticalComponents[] = {
        &topWorkspaceArea_, &nodeResizer_, &bottomDockSlot_
    };
    verticalLayout_.layOutComponents(verticalComponents,
                                      3,
                                      area.getX(), area.getY(), area.getWidth(), area.getHeight(),
                                      true, true);

    juce::Component* horizontalComponents[] = {
        &inspectorDockSlot_, &inspectorResizer_, &centerArea_, &browserResizer_,
        &browserDockSlot_
    };
    horizontalLayout_.layOutComponents(horizontalComponents,
                                        5,
                                        0, 0, topWorkspaceArea_.getWidth(),
                                        topWorkspaceArea_.getHeight(),
                                        false, true);

    if (inspectorPanelContainer_.getParentComponent() == &inspectorDockSlot_)
        inspectorPanelContainer_.setBounds(
            inspectorDockSlot_.getLocalBounds().withTrimmedRight(30));
    inspectorHideButton_.setBounds(
        inspectorDockSlot_.getWidth() - 27, 7, 22, 22);
    if (browser_.getParentComponent() == &browserDockSlot_)
        browser_.setBounds(
            browserDockSlot_.getLocalBounds().withTrimmedRight(30));
    browserHideButton_.setBounds(
        browserDockSlot_.getWidth() - 27, 7, 22, 22);
    inspectorShowButton_.setBounds(4, topWorkspaceArea_.getY() + 4, 22, 46);
    browserShowButton_.setBounds(getWidth() - 26,
                                 topWorkspaceArea_.getY() + 4, 22, 46);
    bottomShowButton_.setBounds(
        5, std::max(0, area.getBottom() - 22), 46, 20);
    editorDockSlot_.setBounds(centerArea_.getLocalBounds());
    if (editorArea_.getParentComponent() == &editorDockSlot_)
        editorArea_.setBounds(editorDockSlot_.getLocalBounds());
    if (nodeAreaContainer_.getParentComponent() == &bottomDockSlot_)
        nodeAreaContainer_.setBounds(bottomDockSlot_.getLocalBounds());

    inspector_.setBounds(inspectorPanelContainer_.getLocalBounds());
    auto inspectorControls = inspectorPanelContainer_.getLocalBounds().reduced(12);
    inspectorControls.removeFromTop(112);
    if (gainSlider_.isVisible()) {
        auto knobs = inspectorControls.removeFromTop(92);
        const auto knobWidth = knobs.getWidth() / 2;
        auto gainArea = knobs.removeFromLeft(knobWidth);
        auto panArea = knobs;
        gainLabel_.setBounds(gainArea.removeFromTop(18));
        panLabel_.setBounds(panArea.removeFromTop(18));
        gainSlider_.setBounds(gainArea.reduced(4, 0));
        panSlider_.setBounds(panArea.reduced(4, 0));
        auto mixerButtons = inspectorControls.removeFromTop(32).reduced(3, 2);
        const auto mixerButtonWidth = mixerButtons.getWidth() / 3;
        muteButton_.setBounds(mixerButtons.removeFromLeft(mixerButtonWidth).reduced(2, 0));
        soloButton_.setBounds(mixerButtons.removeFromLeft(mixerButtonWidth).reduced(2, 0));
        armButton_.setBounds(mixerButtons.reduced(2, 0));
        inspectorControls.removeFromTop(5);
    }
    const auto layoutClipSlider = [&inspectorControls](juce::Label& label,
                                                       juce::Slider& slider) {
        label.setBounds(inspectorControls.removeFromTop(14));
        slider.setBounds(inspectorControls.removeFromTop(25));
        inspectorControls.removeFromTop(2);
    };
    if (sampleGainSlider_.isVisible())
        layoutClipSlider(sampleGainLabel_, sampleGainSlider_);
    if (sampleFadeInSlider_.isVisible()) {
        layoutClipSlider(sampleFadeInLabel_, sampleFadeInSlider_);
        layoutClipSlider(sampleFadeOutLabel_, sampleFadeOutSlider_);
    }
    layoutClipSlider(clipStartLabel_, clipStartSlider_);
    layoutClipSlider(sourceOffsetLabel_, sourceOffsetSlider_);
    layoutClipSlider(clipDurationLabel_, clipDurationSlider_);
    layoutClipSlider(clipStretchLabel_, clipStretchSlider_);
    sourceTempoLabel_.setBounds(inspectorControls.removeFromTop(14));
    auto tempoRow = inspectorControls.removeFromTop(25);
    detectTempoButton_.setBounds(
        tempoRow.removeFromRight(66).reduced(2, 0));
    sourceTempoSlider_.setBounds(tempoRow.reduced(1, 0));
    inspectorControls.removeFromTop(2);
    stretchModeLabel_.setBounds(inspectorControls.removeFromTop(14));
    auto stretchModeRow = inspectorControls.removeFromTop(25);
    syncTempoButton_.setBounds(
        stretchModeRow.removeFromRight(66).reduced(2, 0));
    stretchModeSelector_.setBounds(stretchModeRow.reduced(1, 0));
    inspectorControls.removeFromTop(3);
    audioTakeLabel_.setBounds(inspectorControls.removeFromTop(14));
    auto takeSelectorRow = inspectorControls.removeFromTop(25);
    audioTakeSelector_.setBounds(takeSelectorRow);
    inspectorControls.removeFromTop(4);
    auto takeButtons = inspectorControls.removeFromTop(28);
    const auto takeButtonWidth = takeButtons.getWidth() / 3;
    addAudioTakeButton_.setBounds(
        takeButtons.removeFromLeft(takeButtonWidth).reduced(2, 0));
    recordAudioTakeButton_.setBounds(
        takeButtons.removeFromLeft(takeButtonWidth).reduced(2, 0));
    removeAudioTakeButton_.setBounds(takeButtons.reduced(2, 0));

    auto nodeArea = nodeAreaContainer_.getLocalBounds();
    auto panelHeader = nodeArea.removeFromTop(28);
    // The panel navigation belongs to one predictable group on the left:
    // collapse, scope, then the content tabs.  Keeping scope actions at the
    // far right made them look unrelated to the device panel they control.
    bottomHideButton_.setBounds(
        panelHeader.removeFromLeft(28).reduced(3, 2));
    moduleScopeButton_.setVisible(bottomPanelMode_ == 1);
    trackScopeButton_.setVisible(bottomPanelMode_ == 1);
    if (bottomPanelMode_ == 1) {
        const auto headerHeight = panelHeader.getHeight();
        moduleScopeButton_.setBounds(
            panelHeader.removeFromLeft(preferredTextButtonWidth(
                moduleScopeButton_, headerHeight)).reduced(3, 2));
        trackScopeButton_.setBounds(
            panelHeader.removeFromLeft(preferredTextButtonWidth(
                trackScopeButton_, headerHeight)).reduced(3, 2));
        panelHeader.removeFromLeft(6);
    }
    for (auto* tab : { &devicesPanelButton_, &graphPanelButton_,
                       &modulationPanelButton_, &automationPanelButton_ })
        tab->setBounds(panelHeader.removeFromLeft(preferredTextButtonWidth(
            *tab, panelHeader.getHeight())).reduced(3, 2));
    if (bottomPanelMode_ == 3) {
        modulationEnabledButton_.setBounds(
            panelHeader.removeFromRight(72).reduced(3, 2));
        modulationAmountSlider_.setBounds(
            panelHeader.removeFromRight(154).reduced(3, 2));
        modulationTargetSelector_.setBounds(panelHeader.reduced(3, 2));
    }
    if (bottomPanelMode_ == 2 && currentEditorMode_ != 7
        && graphWindow_ == nullptr
        && nodeGraph_.getParentComponent() == &nodeAreaContainer_) {
        auto keyboardArea = nodeArea.removeFromBottom(58);
        nodeGraph_.setBounds(nodeArea.reduced(2));
        midiKeyboard_.setBounds(keyboardArea.reduced(8, 3));
    } else if (bottomPanelMode_ == 3) {
        modulationGraph_.setBounds(nodeArea.reduced(2));
    } else if (bottomPanelMode_ == 4) {
        bottomAutomationEditor_.setBounds(nodeArea.reduced(2));
    } else {
        deviceChain_.setBounds(nodeArea.reduced(2));
    }

    auto editorBounds = editorArea_.getLocalBounds();
    if (currentEditorMode_ == 1) {
        timelineActionBar_.setBounds(editorBounds.removeFromTop(32));
        auto actions = timelineActionBar_.getLocalBounds().reduced(4, 3);
        const auto actionHeight = actions.getHeight();
        // Transport and grid live in the header row. Mirroring them here showed
        // the same state twice — these controls only ever forwarded to their
        // counterparts above — so this row is now strictly what is specific to
        // editing the arrangement: the tools, and what you do to tracks.
        for (auto* mirrored : { &timelinePlayButton_, &timelineStopButton_ })
            mirrored->setVisible(false);
        for (auto* mirrored : { &timelineSnapSelector_, &timelineSnapTypeSelector_ })
            mirrored->setVisible(false);

        for (auto* button : { &timelinePointerButton_, &timelineKnifeButton_,
                              &timelineStretchButton_, &timelineZoomButton_,
                              &timelineHandButton_ })
            button->setBounds(actions.removeFromLeft(31).reduced(1, 0));
        actions.removeFromLeft(10);
        addArrangementTrackButton_.setBounds(
            actions.removeFromLeft(preferredTextButtonWidth(
                addArrangementTrackButton_, actionHeight)).reduced(2, 0));
        actions.removeFromLeft(10);
        for (auto* button : { &arrangementPlaybackButton_,
                              &launcherActionRecordButton_,
                              &launcherActionReplayButton_ })
            button->setBounds(actions.removeFromLeft(preferredTextButtonWidth(
                *button, actionHeight)).reduced(2, 0));
        timelineModeLabel_.setBounds(actions.reduced(10, 0));
    }
    timelineRuler_.setBounds(editorBounds.removeFromTop(30));
    arrangementRangeButton_.setBounds(
        TimelineRulerComponent::trackHeaderWidth - 100,
        timelineRuler_.getY() + 4, 94, 22);
    auto navigation = editorBounds.removeFromBottom(18);
    auto zoomControls = navigation.removeFromRight(82);
    timelineZoomOutButton_.setBounds(
        zoomControls.removeFromLeft(24).reduced(1));
    timelineFitButton_.setBounds(
        zoomControls.removeFromLeft(34).reduced(1));
    timelineZoomInButton_.setBounds(zoomControls.reduced(1));
    timelineScrollBar_.setBounds(
        navigation.withTrimmedLeft(
            std::min(TimelineRulerComponent::trackHeaderWidth,
                     navigation.getWidth())));
    trackViewport_.setBounds(editorBounds);
    auto contextualEditorBounds = editorArea_.getLocalBounds();
    auto primaryEditorBounds = contextualEditorBounds;
    const auto contextualMode = currentEditorMode_ == 2
                                || currentEditorMode_ == 6;
    if (contextualMode && contextAutomationVisible_) {
        const auto maximumAutomationHeight = std::max(
            90, contextualEditorBounds.getHeight() - 140);
        contextAutomationHeight_ = juce::jlimit(
            90, maximumAutomationHeight, contextAutomationHeight_);
        contextualEditorLayout_.setItemLayout(
            0, 120.0, -1.0, -1.0);
        contextualEditorLayout_.setItemLayout(1, 6.0, 6.0, 6.0);
        contextualEditorLayout_.setItemLayout(
            2, 90.0, static_cast<double>(maximumAutomationHeight),
            static_cast<double>(contextAutomationHeight_));
        juce::Component* contextualComponents[] = {
            &contextualPrimaryLayoutProxy_, &contextAutomationResizer_,
            &contextAutomationEditor_
        };
        contextualEditorLayout_.layOutComponents(
            contextualComponents, 3, contextualEditorBounds.getX(),
            contextualEditorBounds.getY(), contextualEditorBounds.getWidth(),
            contextualEditorBounds.getHeight(), true, true);
        primaryEditorBounds = contextualPrimaryLayoutProxy_.getBounds();
        contextAutomationResizer_.setVisible(true);
        contextAutomationEditor_.setVisible(true);
    } else {
        contextAutomationEditor_.setBounds({});
        contextAutomationResizer_.setBounds({});
        contextAutomationResizer_.setVisible(false);
    }
    contextAutomationToggleButton_.setVisible(contextualMode);
    contextAutomationToggleButton_.setBounds(
        7,
        contextAutomationVisible_ && contextualMode
            ? contextAutomationEditor_.getY() + 3
            : std::max(3, contextualEditorBounds.getBottom() - 25),
        58, 21);
    // The clip/MIDI editor is added after this overlay, so explicitly keep
    // the persistent automation reveal control above the editor surface.
    contextAutomationToggleButton_.toFront(false);
    if (currentEditorMode_ == 7
        && nodeGraph_.getParentComponent() == &editorArea_)
        nodeGraph_.setBounds(contextualEditorBounds.reduced(2));
    pianoRoll_.setBounds(primaryEditorBounds);
    clipTimelineEditor_.setBounds(primaryEditorBounds);
    automationEditor_.setBounds(contextualEditorBounds);
    audioEditor_.setBounds(contextualEditorBounds);
    clipLauncher_.setBounds(contextualEditorBounds);
    virtualBusManager_.setBounds(contextualEditorBounds);
    mixer_.setBounds(contextualEditorBounds);
    sampleExplorer_.setBounds(contextualEditorBounds);
    layoutTrackLanes();
}

bool WorkspaceComponent::keyPressed(const juce::KeyPress& key)
{
    if (dynamic_cast<juce::TextEditor*>(
            juce::Component::getCurrentlyFocusedComponent()) != nullptr)
        return false;
    if (key.getKeyCode() == juce::KeyPress::escapeKey
        && audio_.hasRecordingSession()) {
        finishAudioRecording(false);
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'K') {
        showCommandPalette();
        return true;
    }
    if (key.getModifiers().isCommandDown()
        && key.getModifiers().isShiftDown()
        && !key.getModifiers().isAltDown()
        && key.getKeyCode() == 'I') {
        editorModeSelector_.setSelectedId(2, juce::sendNotificationSync);
        chooseMidiImportFile();
        return true;
    }
    if (key.getModifiers().isCommandDown()
        && key.getModifiers().isShiftDown()
        && !key.getModifiers().isAltDown()
        && key.getKeyCode() == 'E') {
        editorModeSelector_.setSelectedId(2, juce::sendNotificationSync);
        chooseMidiExportFile();
        return true;
    }
    if (!key.getModifiers().isCommandDown()
        && !key.getModifiers().isShiftDown()
        && !key.getModifiers().isAltDown()
        && key.getKeyCode() == 'R') {
        recordButton_.triggerClick();
        return true;
    }
    if (key.getModifiers().isCommandDown()
        && key.getKeyCode() >= '1' && key.getKeyCode() <= '9') {
        editorModeSelector_.setSelectedId(key.getKeyCode() - '0', juce::sendNotificationSync);
        return true;
    }
    // The workspace is registered as a key listener on every nested control.
    // Dispatch MIDI editing here first so shortcuts remain available when a
    // selector or mixer control, rather than the piano-roll canvas, has focus.
    // PianoRollComponent deliberately returns false for undo/redo so the
    // project-level history routing below remains authoritative.
    if (editorModeSelector_.getSelectedId() == 2
        && pianoRoll_.keyPressed(key))
        return true;
    if (editorModeSelector_.getSelectedId() == 5
        && key.getModifiers().isCommandDown()
        && key.getModifiers().isShiftDown()
        && !key.getModifiers().isAltDown()) {
        if (key.getKeyCode() == 'N') {
            (void) audioEditor_.processSelection(
                { AudioEditOperation::normalisePeak, -1.0f });
            return true;
        }
        if (key.getKeyCode() == 'G') {
            (void) audioEditor_.applyCurrentGain();
            return true;
        }
        if (key.getKeyCode() == 'R') {
            (void) audioEditor_.processSelection(
                { AudioEditOperation::reverse, 0.0f });
            return true;
        }
        if (key.getKeyCode() == 'D') {
            (void) audioEditor_.processSelection(
                { AudioEditOperation::removeDcOffset, 0.0f });
            return true;
        }
    }
    if (editorModeSelector_.getSelectedId() == 5
        && !key.getModifiers().isCommandDown()
        && !key.getModifiers().isShiftDown()
        && key.getKeyCode() == juce::KeyPress::deleteKey) {
        (void) audioEditor_.processSelection(
            { AudioEditOperation::silence, 0.0f });
        return true;
    }
    if (key.getModifiers().isCommandDown()
        && !key.getModifiers().isAltDown()
        && editorModeSelector_.getSelectedId() == 1
        && (key.getKeyCode() == '+' || key.getKeyCode() == '=')) {
        timelineZoomInButton_.triggerClick();
        return true;
    }
    if (key.getModifiers().isCommandDown()
        && !key.getModifiers().isAltDown()
        && editorModeSelector_.getSelectedId() == 1
        && key.getKeyCode() == '-') {
        timelineZoomOutButton_.triggerClick();
        return true;
    }
    if (key.getModifiers().isCommandDown()
        && !key.getModifiers().isAltDown()
        && editorModeSelector_.getSelectedId() == 1
        && key.getKeyCode() == '0') {
        fitArrangement();
        return true;
    }
    if (key.getModifiers().isCommandDown()
        && !key.getModifiers().isShiftDown()
        && !key.getModifiers().isAltDown()
        && key.getKeyCode() == 'C'
        && editorModeSelector_.getSelectedId() == 1) {
        if (arrangementMarqueeSelectionActive_
            && arrangementMarqueeClipCount() > 0)
            (void) copyArrangementMarqueeSelection();
        else if (selectedTrack_ && selectedAudioClipId_ != 0)
            handleAudioClipCommand(*selectedTrack_, selectedAudioClipId_,
                                   WaveformComponent::RegionCommand::copy,
                                   audio_.position());
        return true;
    }
    if (key.getModifiers().isCommandDown()
        && !key.getModifiers().isShiftDown()
        && !key.getModifiers().isAltDown()
        && key.getKeyCode() == 'X'
        && editorModeSelector_.getSelectedId() == 1) {
        if (arrangementMarqueeSelectionActive_
            && arrangementMarqueeClipCount() > 0)
            (void) cutArrangementMarqueeSelection();
        else if (selectedTrack_ && selectedAudioClipId_ != 0)
            handleAudioClipCommand(*selectedTrack_, selectedAudioClipId_,
                                   WaveformComponent::RegionCommand::cut,
                                   audio_.position());
        return true;
    }
    if (key.getModifiers().isCommandDown()
        && !key.getModifiers().isShiftDown()
        && !key.getModifiers().isAltDown()
        && key.getKeyCode() == 'V'
        && editorModeSelector_.getSelectedId() == 1) {
        if (selectedTrack_
            && pasteAudioClipSelection(*selectedTrack_, audio_.position())) {
            refreshTimelineRanges();
            refreshSelectedTrack();
            refreshAutomationEditor();
            refreshClipLauncher();
            markProjectDirty();
        }
        return true;
    }
    if (key.getModifiers().isCommandDown()
        && !key.getModifiers().isShiftDown()
        && key.getKeyCode() == 'A'
        && editorModeSelector_.getSelectedId() == 1) {
        if (selectedTrack_ && *selectedTrack_ < audio_.trackCount()) {
            clearArrangementMarqueeSelection();
            const auto clips = audio_.audioClips(*selectedTrack_);
            selectedAudioClipIds_.clear();
            selectedAudioClipIds_.reserve(clips.size());
            for (const auto& clip : clips)
                selectedAudioClipIds_.push_back(clip.id);
            if (selectedAudioClipIds_.empty())
                selectedAudioClipId_ = 0;
            if (!selectedAudioClipIds_.empty()
                && std::find(selectedAudioClipIds_.begin(),
                             selectedAudioClipIds_.end(),
                             selectedAudioClipId_)
                       == selectedAudioClipIds_.end())
                selectedAudioClipId_ = selectedAudioClipIds_.front();
            refreshTimelineRanges();
            refreshSelectedTrack();
        }
        return true;
    }
    if (key.getModifiers().isCommandDown()
        && !key.getModifiers().isShiftDown()
        && key.getKeyCode() == 'D'
        && editorModeSelector_.getSelectedId() == 1) {
        if (selectedTrack_ && selectedAudioClipId_ != 0)
            handleAudioClipCommand(*selectedTrack_, selectedAudioClipId_,
                                   WaveformComponent::RegionCommand::duplicate,
                                   audio_.position());
        return true;
    }
    if (key.getModifiers().isCommandDown()
        && key.getModifiers().isShiftDown()
        && key.getKeyCode() == 'X'
        && editorModeSelector_.getSelectedId() == 1) {
        if (arrangementMarqueeSelectionActive_
            && arrangementMarqueeClipCount() > 0)
            (void) editArrangementMarqueeRange(
                AudioTrackController::ArrangementRangeOperation::
                    splitAtBoundaries);
        else if (selectedTrack_ && selectedAudioClipId_ != 0)
            handleAudioClipCommand(*selectedTrack_, selectedAudioClipId_,
                                   WaveformComponent::RegionCommand::split,
                                   audio_.position());
        return true;
    }
    if (key.getModifiers().isCommandDown()
        && key.getModifiers().isShiftDown()
        && key.getKeyCode() == 'T'
        && editorModeSelector_.getSelectedId() == 1) {
        (void) editArrangementMarqueeRange(
            AudioTrackController::ArrangementRangeOperation::cropToRange);
        return true;
    }
    if (!key.getModifiers().isCommandDown()
        && key.getModifiers().isShiftDown()
        && key.getKeyCode() == juce::KeyPress::deleteKey
        && editorModeSelector_.getSelectedId() == 1) {
        (void) editArrangementMarqueeRange(
            AudioTrackController::ArrangementRangeOperation::silenceRange);
        return true;
    }
    if (!key.getModifiers().isCommandDown()
        && !key.getModifiers().isShiftDown()
        && key.getKeyCode() == juce::KeyPress::deleteKey
        && editorModeSelector_.getSelectedId() == 1) {
        if (deleteArrangementMarqueeSelection())
            return true;
        if (selectedTrack_ && selectedAudioClipId_ != 0)
            handleAudioClipCommand(*selectedTrack_, selectedAudioClipId_,
                                   WaveformComponent::RegionCommand::remove,
                                   audio_.position());
        return true;
    }
    if (key == juce::KeyPress::spaceKey) {
        if (currentEditorMode_ == 6
            && (launcherEditorContext_.has_value()
                || (selectedTrack_.has_value()
                    && selectedAudioClipId_ != 0)))
            clipTimelineEditor_.triggerPlayPause();
        else
            playButton_.triggerClick();
        return true;
    }
    if (key.getModifiers().isCommandDown()
        && key.getModifiers().isAltDown()
        && key.getKeyCode() == 'Z') {
        if (key.getModifiers().isShiftDown())
            redoLayoutChange();
        else
            undoLayoutChange();
        return true;
    }
    if (key.getModifiers().isCommandDown()
        && key.getModifiers().isAltDown()
        && key.getKeyCode() == 'Y') {
        redoLayoutChange();
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'Z') {
        const auto mode = editorModeSelector_.getSelectedId();
        if (mode == 1) {
            if (key.getModifiers().isShiftDown())
                redoArrangementEdit();
            else
                undoArrangementEdit();
            return true;
        }
        if (mode == 5) {
            if (key.getModifiers().isShiftDown())
                redoAudioEdit();
            else
                undoAudioEdit();
            return true;
        }
        if (mode == 2) {
            if (key.getModifiers().isShiftDown())
                redoMidiEdit();
            else
                undoMidiEdit();
            return true;
        }
        return false;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'Y') {
        const auto mode = editorModeSelector_.getSelectedId();
        if (mode == 1) {
            redoArrangementEdit();
            return true;
        }
        if (mode == 5) {
            redoAudioEdit();
            return true;
        }
        if (mode == 2) {
            redoMidiEdit();
            return true;
        }
        return false;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'S') {
        chooseProjectSaveFile();
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'O') {
        chooseAudioFile();
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'L') {
        chooseProjectLoadFile();
        return true;
    }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'E') {
        masterExportButton_.triggerClick();
        return true;
    }
    return false;
}

void WorkspaceComponent::registerShortcutListener(juce::Component& component)
{
    if (&component != this)
        component.addKeyListener(this);
    for (auto childIndex = 0; childIndex < component.getNumChildComponents();
         ++childIndex)
        if (auto* child = component.getChildComponent(childIndex))
            registerShortcutListener(*child);
}

bool WorkspaceComponent::keyPressed(const juce::KeyPress& key,
                                    juce::Component*)
{
    return keyPressed(key);
}

bool WorkspaceComponent::isInterestedInFileDrag(const juce::StringArray& files)
{
    return files.size() == 1
           && (isSupportedAudioFile(juce::File(files[0]))
               || isSupportedMidiFile(juce::File(files[0])));
}

void WorkspaceComponent::filesDropped(const juce::StringArray& files, int, int)
{
    if (files.isEmpty())
        return;
    const auto file = juce::File(files[0]);
    if (isSupportedMidiFile(file))
        importMidiFile(file);
    else
        loadAudioFile(file);
}

void WorkspaceComponent::showNodeGraphWindow()
{
    if (graphWindow_ == nullptr)
        toggleGraphWindow();
}

void WorkspaceComponent::showCommandPalette()
{
    const auto safe = juce::Component::SafePointer<WorkspaceComponent>(this);
    const auto invoke = [safe](auto action) {
        return [safe, action = std::move(action)] {
            if (safe != nullptr)
                action(*safe);
        };
    };
    const auto file = tr("command.category.file");
    const auto transport = tr("command.category.transport");
    const auto views = tr("command.category.views");
    const auto workspace = tr("command.category.workspace");
    const auto audioEdit = tr("command.category.audioEdit");
    const auto settings = tr("command.category.settings");
    const auto language = tr("command.category.language");
#if JUCE_MAC
    const auto commandKey = juce::String::fromUTF8("⌘");
#else
    const auto commandKey = juce::String("Ctrl+");
#endif
    std::vector<CommandPaletteEntry> entries {
        { tr("toolbar.openAudio"), file, "open import audio wav aiff flac mp3 ouvrir importer", commandKey + "O",
          invoke([](auto& owner) { owner.chooseAudioFile(); }) },
        { tr("midi.importFile"), file,
          "open import midi mid smf sequence ouvrir importer fichier sequence",
          commandKey + "Shift+I",
          invoke([](auto& owner) {
              owner.editorModeSelector_.setSelectedId(
                  2, juce::sendNotificationSync);
              owner.chooseMidiImportFile();
          }) },
        { tr("midi.exportFile"), file,
          "export midi clip mid smf sequence exporter fichier",
          commandKey + "Shift+E",
          invoke([](auto& owner) {
              owner.editorModeSelector_.setSelectedId(
                  2, juce::sendNotificationSync);
              owner.chooseMidiExportFile();
          }) },
        { tr("toolbar.save"), file, "save project sauvegarder sauver projet", commandKey + "S",
          invoke([](auto& owner) { owner.chooseProjectSaveFile(); }) },
        { tr("toolbar.load"), file, "load open project charger ouvrir projet", commandKey + "L",
          invoke([](auto& owner) { owner.chooseProjectLoadFile(); }) },
        { tr("toolbar.exportMix"), file,
          "export mix master bounce render wav exporter mixage rendu stereo", commandKey + "E",
          invoke([](auto& owner) { owner.masterExportButton_.triggerClick(); }) },
        { tr("transport.play"), transport, "play pause lecture transport", "Space",
          invoke([](auto& owner) { owner.playButton_.triggerClick(); }) },
        { tr("transport.stop"), transport, "stop rewind arret transport", {},
          invoke([](auto& owner) { owner.stopButton_.triggerClick(); }) },
        { tr("transport.recordAudio"), transport,
          "record audio take input capture wav enregistrer prise entree", "R",
          invoke([](auto& owner) {
              owner.editorModeSelector_.setSelectedId(
                  1, juce::sendNotificationSync);
              owner.recordButton_.triggerClick();
          }) },
        { tr("track.toggleArm"), transport,
          "arm audio track record enable armer piste prise", {},
          invoke([](auto& owner) { owner.armButton_.triggerClick(); }) },
        { tr("audioTake.add"), transport,
          "add import alternate audio take comping ajouter importer prise alternative", {},
          invoke([](auto& owner) { owner.chooseAudioTakeFile(); }) },
        { tr("audioTake.record"), transport,
          "record alternate audio take comping enregistrer prise alternative", {},
          invoke([](auto& owner) { owner.recordSelectedAudioTake(); }) },
        { tr("audioTake.remove"), transport,
          "delete remove active audio take comping supprimer prise active", {},
          invoke([](auto& owner) { owner.removeSelectedAudioTake(); }) },
        { tr("transport.recordMidi"), transport,
          "record midi capture notes controller enregistrer controleur", "R",
          invoke([](auto& owner) {
              owner.editorModeSelector_.setSelectedId(
                  2, juce::sendNotificationSync);
              owner.recordButton_.triggerClick();
          }) },
        { tr("transport.loop"), transport, "loop boucle cycle transport", {},
          invoke([](auto& owner) { owner.loopButton_.triggerClick(); }) },
        { tr("loop.setToSelection"), transport,
          "loop selected audio regions cycle selection boucle regions selectionnees", {},
          invoke([](auto& owner) { owner.setLoopRangeToAudioSelection(); }) },
        { tr("loop.resetRange"), transport,
          "reset loop range full project cycle reinitialiser plage boucle projet", {},
          invoke([](auto& owner) { owner.resetLoopRangeToProject(); }) },
        { tr("editor.arrange"), views, "arrange timeline tracks arrangement pistes", commandKey + "1",
          invoke([](auto& owner) { owner.editorModeSelector_.setSelectedId(1, juce::sendNotificationSync); }) },
        { tr("editor.midi"), views, "midi piano roll notes editor editeur", commandKey + "2",
          invoke([](auto& owner) { owner.editorModeSelector_.setSelectedId(2, juce::sendNotificationSync); }) },
        { tr("editor.automation"), views, "automation courbe parameters parametres", commandKey + "3",
          invoke([](auto& owner) { owner.editorModeSelector_.setSelectedId(3, juce::sendNotificationSync); }) },
        { tr("editor.launcher"), views, "launcher clips scenes lanceur", commandKey + "4",
          invoke([](auto& owner) { owner.editorModeSelector_.setSelectedId(4, juce::sendNotificationSync); }) },
        { tr("editor.audio"), views, "audio sample waveform goldwave editor editeur", commandKey + "5",
          invoke([](auto& owner) { owner.editorModeSelector_.setSelectedId(5, juce::sendNotificationSync); }) },
        { tr("editor.mixer"), views, "mixer console faders levels mixage tranches", commandKey + "9",
          invoke([](auto& owner) { owner.setEditorMode(9); }) },
        { tr("audioEditor.fadeIn"), audioEdit,
          "audio sample fade in fondu entree selection", {},
          invoke([](auto& owner) {
              (void) owner.audioEditor_.processSelection(
                  { AudioEditOperation::fadeIn, 0.0f });
          }) },
        { tr("audioEditor.fadeOut"), audioEdit,
          "audio sample fade out fondu sortie selection", {},
          invoke([](auto& owner) {
              (void) owner.audioEditor_.processSelection(
                  { AudioEditOperation::fadeOut, 0.0f });
          }) },
        { tr("audioEditor.silence"), audioEdit,
          "audio sample silence clear effacer selection", "Delete",
          invoke([](auto& owner) {
              (void) owner.audioEditor_.processSelection(
                  { AudioEditOperation::silence, 0.0f });
          }) },
        { tr("audioEditor.reverse"), audioEdit,
          "audio sample reverse inverser selection", commandKey + "Shift+R",
          invoke([](auto& owner) {
              (void) owner.audioEditor_.processSelection(
                  { AudioEditOperation::reverse, 0.0f });
          }) },
        { tr("audioEditor.normalise"), audioEdit,
          "audio sample normalize normalise peak pic minus one db selection",
          commandKey + "Shift+N",
          invoke([](auto& owner) {
              (void) owner.audioEditor_.processSelection(
                  { AudioEditOperation::normalisePeak, -1.0f });
          }) },
        { tr("audioEditor.removeDc"), audioEdit,
          "audio sample remove dc offset retirer continu selection",
          commandKey + "Shift+D",
          invoke([](auto& owner) {
              (void) owner.audioEditor_.processSelection(
                  { AudioEditOperation::removeDcOffset, 0.0f });
          }) },
        { tr("audioEditor.invertPolarity"), audioEdit,
          "audio sample invert polarity phase inverser polarite selection", {},
          invoke([](auto& owner) {
              (void) owner.audioEditor_.processSelection(
                  { AudioEditOperation::invertPolarity, 0.0f });
          }) },
        { tr("audioEditor.swapStereo"), audioEdit,
          "audio sample swap stereo left right echanger gauche droite selection", {},
          invoke([](auto& owner) {
              (void) owner.audioEditor_.processSelection(
                  { AudioEditOperation::swapStereo, 0.0f });
          }) },
        { tr("audioEditor.applyGain"), audioEdit,
          "audio sample gain amplify amplification db selection",
          commandKey + "Shift+G",
          invoke([](auto& owner) {
              (void) owner.audioEditor_.applyCurrentGain();
          }) },
        { tr("timeline.zoomIn"), views,
          "zoom in arrangement timeline horizontal agrandir", commandKey + "+",
          invoke([](auto& owner) { owner.timelineZoomInButton_.triggerClick(); }) },
        { tr("timeline.zoomOut"), views,
          "zoom out arrangement timeline horizontal reduire", commandKey + "-",
          invoke([](auto& owner) { owner.timelineZoomOutButton_.triggerClick(); }) },
        { tr("timeline.fit"), views,
          "fit full arrangement timeline project ajuster projet entier", commandKey + "0",
          invoke([](auto& owner) { owner.fitArrangement(); }) },
        { tr("arrangement.selectAll"), workspace,
          "select all audio regions clips selectionner toutes regions", commandKey + "A",
          invoke([](auto& owner) {
              if (!owner.selectedTrack_)
                  return;
              owner.clearArrangementMarqueeSelection();
              const auto clips = owner.audio_.audioClips(*owner.selectedTrack_);
              owner.selectedAudioClipIds_.clear();
              for (const auto& clip : clips)
                  owner.selectedAudioClipIds_.push_back(clip.id);
              if (!owner.selectedAudioClipIds_.empty())
                  owner.selectedAudioClipId_ = owner.selectedAudioClipIds_.front();
              else
                  owner.selectedAudioClipId_ = 0;
              owner.refreshTimelineRanges();
              owner.refreshSelectedTrack();
          }) },
        { tr("snap.toggle"), workspace,
          "snap grid magnetic magnetique grille toggle", {},
          invoke([](auto& owner) {
              owner.snapSelector_.setSelectedId(
                  owner.arrangementSnappingEnabled() ? 1 : 4,
                  juce::sendNotificationSync);
          }) },
        { tr("snap.quarter"), workspace, "snap grid 1/4 quarter noire grille", {},
          invoke([](auto& owner) { owner.snapSelector_.setSelectedId(2, juce::sendNotificationSync); }) },
        { tr("snap.eighth"), workspace, "snap grid 1/8 eighth croche grille", {},
          invoke([](auto& owner) { owner.snapSelector_.setSelectedId(3, juce::sendNotificationSync); }) },
        { tr("snap.sixteenth"), workspace, "snap grid 1/16 sixteenth double croche grille", {},
          invoke([](auto& owner) { owner.snapSelector_.setSelectedId(4, juce::sendNotificationSync); }) },
        { tr("snap.thirtySecond"), workspace, "snap grid 1/32 thirty-second triple croche grille", {},
          invoke([](auto& owner) { owner.snapSelector_.setSelectedId(5, juce::sendNotificationSync); }) },
        { tr("audioClip.copy"), workspace,
          "copy audio region clip clipboard copier presse papiers", commandKey + "C",
          invoke([](auto& owner) {
              if (owner.arrangementMarqueeSelectionActive_
                  && owner.arrangementMarqueeClipCount() > 0)
                  (void) owner.copyArrangementMarqueeSelection();
              else if (owner.selectedTrack_ && owner.selectedAudioClipId_ != 0)
                  owner.handleAudioClipCommand(
                      *owner.selectedTrack_, owner.selectedAudioClipId_,
                      WaveformComponent::RegionCommand::copy,
                      owner.audio_.position());
          }) },
        { tr("audioClip.cut"), workspace,
          "cut audio region clip clipboard couper presse papiers", commandKey + "X",
          invoke([](auto& owner) {
              if (owner.arrangementMarqueeSelectionActive_
                  && owner.arrangementMarqueeClipCount() > 0)
                  (void) owner.cutArrangementMarqueeSelection();
              else if (owner.selectedTrack_ && owner.selectedAudioClipId_ != 0)
                  owner.handleAudioClipCommand(
                      *owner.selectedTrack_, owner.selectedAudioClipId_,
                      WaveformComponent::RegionCommand::cut,
                      owner.audio_.position());
          }) },
        { tr("audioClip.paste"), workspace,
          "paste audio region clip clipboard coller presse papiers", commandKey + "V",
          invoke([](auto& owner) {
              if (owner.selectedTrack_
                  && owner.pasteAudioClipSelection(
                      *owner.selectedTrack_, owner.audio_.position())) {
                  owner.refreshTimelineRanges();
                  owner.refreshSelectedTrack();
                  owner.refreshAutomationEditor();
                  owner.refreshClipLauncher();
                  owner.markProjectDirty();
              }
          }) },
        { tr("audioClip.duplicate"), workspace,
          "duplicate audio region clip dupliquer arrangement", commandKey + "D",
          invoke([](auto& owner) {
              if (owner.selectedTrack_ && owner.selectedAudioClipId_ != 0)
                  owner.handleAudioClipCommand(
                      *owner.selectedTrack_, owner.selectedAudioClipId_,
                      WaveformComponent::RegionCommand::duplicate,
                      owner.audio_.position());
          }) },
        { tr("arrangement.splitRange"), workspace,
          "split time range boundaries multitrack decouper plage bornes multipiste",
          commandKey + "Shift+X",
          invoke([](auto& owner) {
              (void) owner.editArrangementMarqueeRange(
                  AudioTrackController::ArrangementRangeOperation::
                      splitAtBoundaries);
          }) },
        { tr("arrangement.cropRange"), workspace,
          "crop trim regions to time range multitrack rogner recadrer plage multipiste",
          commandKey + "Shift+T",
          invoke([](auto& owner) {
              (void) owner.editArrangementMarqueeRange(
                  AudioTrackController::ArrangementRangeOperation::
                      cropToRange);
          }) },
        { tr("arrangement.silenceRange"), workspace,
          "silence clear time range gap multitrack rendre silencieux plage multipiste",
          "Shift+Delete",
          invoke([](auto& owner) {
              (void) owner.editArrangementMarqueeRange(
                  AudioTrackController::ArrangementRangeOperation::
                      silenceRange);
          }) },
        { tr("audioClip.split"), workspace,
          "split cut audio region clip decouper couper cursor curseur", {},
          invoke([](auto& owner) {
              if (owner.selectedTrack_ && owner.selectedAudioClipId_ != 0)
                  owner.handleAudioClipCommand(
                      *owner.selectedTrack_, owner.selectedAudioClipId_,
                      WaveformComponent::RegionCommand::split,
                      owner.audio_.position());
          }) },
        { tr("audioClip.crossfade"), workspace,
          "crossfade audio region clip fondu enchaine chevauchement", {},
          invoke([](auto& owner) {
              if (owner.selectedTrack_ && owner.selectedAudioClipId_ != 0)
                  owner.handleAudioClipCommand(
                      *owner.selectedTrack_, owner.selectedAudioClipId_,
                      WaveformComponent::RegionCommand::crossfade,
                      owner.audio_.position());
          }) },
        { tr("arrangement.undo"), workspace,
          "undo arrangement region clip annuler", commandKey + "Z",
          invoke([](auto& owner) { owner.undoArrangementEdit(); }) },
        { tr("arrangement.redo"), workspace,
          "redo arrangement region clip retablir", commandKey + "Shift+Z",
          invoke([](auto& owner) { owner.redoArrangementEdit(); }) },
        { tr("midi.undo"), workspace,
          "undo midi piano roll note take annuler prise", commandKey + "Z",
          invoke([](auto& owner) {
              owner.editorModeSelector_.setSelectedId(
                  2, juce::sendNotificationSync);
              owner.undoMidiEdit();
          }) },
        { tr("midi.redo"), workspace,
          "redo midi piano roll note take retablir prise",
          commandKey + "Shift+Z",
          invoke([](auto& owner) {
              owner.editorModeSelector_.setSelectedId(
                  2, juce::sendNotificationSync);
              owner.redoMidiEdit();
          }) },
        { tr("midi.selectAll"), workspace,
          "select all midi piano roll notes selectionner toutes les notes",
          commandKey + "A",
          invoke([](auto& owner) {
              owner.editorModeSelector_.setSelectedId(
                  2, juce::sendNotificationSync);
              owner.pianoRoll_.selectAllNotes();
          }) },
        { tr("midi.copy"), workspace,
          "copy midi piano roll notes clipboard copier notes presse papiers",
          commandKey + "C",
          invoke([](auto& owner) {
              owner.editorModeSelector_.setSelectedId(
                  2, juce::sendNotificationSync);
              (void) owner.pianoRoll_.copySelectedNotes();
          }) },
        { tr("midi.cut"), workspace,
          "cut midi piano roll notes clipboard couper notes presse papiers",
          commandKey + "X",
          invoke([](auto& owner) {
              owner.editorModeSelector_.setSelectedId(
                  2, juce::sendNotificationSync);
              (void) owner.pianoRoll_.cutSelectedNotes();
          }) },
        { tr("midi.paste"), workspace,
          "paste midi piano roll notes clipboard coller notes presse papiers",
          commandKey + "V",
          invoke([](auto& owner) {
              owner.editorModeSelector_.setSelectedId(
                  2, juce::sendNotificationSync);
              (void) owner.pianoRoll_.pasteNotes();
          }) },
        { tr("midi.duplicate"), workspace,
          "duplicate midi piano roll notes dupliquer notes",
          commandKey + "D",
          invoke([](auto& owner) {
              owner.editorModeSelector_.setSelectedId(
                  2, juce::sendNotificationSync);
              (void) owner.pianoRoll_.duplicateSelectedNotes();
          }) },
        { tr("midi.delete"), workspace,
          "delete remove midi piano roll notes supprimer effacer notes",
          "Delete",
          invoke([](auto& owner) {
              owner.editorModeSelector_.setSelectedId(
                  2, juce::sendNotificationSync);
              (void) owner.pianoRoll_.deleteSelectedNotes();
          }) },
        { tr("audioClip.delete"), workspace,
          "delete remove audio region clip supprimer effacer", "Delete",
          invoke([](auto& owner) {
              if (owner.deleteArrangementMarqueeSelection())
                  return;
              if (owner.selectedTrack_ && owner.selectedAudioClipId_ != 0)
                  owner.handleAudioClipCommand(
                      *owner.selectedTrack_, owner.selectedAudioClipId_,
                      WaveformComponent::RegionCommand::remove,
                      owner.audio_.position());
          }) },
        { tr("bottom.devices"), views, "devices effects instruments plugins chaine peripheriques", {},
          invoke([](auto& owner) { owner.setBottomPanelMode(1); }) },
        { tr("bottom.nodeGraph"), views, "node graph blueprint nodal graphe dsp", {},
          invoke([](auto& owner) { owner.setEditorMode(7); }) },
        { graphWindow_ != nullptr ? tr("toolbar.dockGraph") : tr("toolbar.floatGraph"),
          workspace, "float dock detach window graph fenetre detacher ancrer", {},
          invoke([](auto& owner) { owner.toggleGraphWindow(); }) },
        { tr("layout.production"), workspace, "layout production workspace disposition", {},
          invoke([](auto& owner) { owner.applyLayoutState(owner.factoryLayout(1)); owner.refreshLayoutSelector(1); }) },
        { tr("layout.editing"), workspace, "layout audio editing workspace disposition", {},
          invoke([](auto& owner) { owner.applyLayoutState(owner.factoryLayout(2)); owner.refreshLayoutSelector(2); }) },
        { tr("layout.modular"), workspace, "layout modular node graph workspace disposition nodal", {},
          invoke([](auto& owner) { owner.applyLayoutState(owner.factoryLayout(3)); owner.refreshLayoutSelector(3); }) },
        { tr("layout.focus"), workspace, "layout focus concentration workspace disposition", {},
          invoke([](auto& owner) { owner.applyLayoutState(owner.factoryLayout(4)); owner.refreshLayoutSelector(4); }) },
        { tr("command.showAllPanels"), workspace,
          "show panels inspector browser bottom afficher panneaux inspecteur navigateur", {},
          invoke([](auto& owner) {
              auto state = owner.captureLayoutState();
              state.inspectorVisible = true;
              state.browserVisible = true;
              state.bottomVisible = true;
              owner.applyLayoutState(state);
          }) },
        { tr("toolbar.audio"), settings, "audio midi asio wasapi jack devices settings reglages", {},
          invoke([](auto& owner) { owner.showAudioSettings(); }) },
        { tr("language.english"), language, "english anglais language langue", {},
          invoke([](auto&) { (void) Localization::instance().setLanguage("en"); }) },
        { tr("language.french"), language, "french francais français language langue", {},
          invoke([](auto&) { (void) Localization::instance().setLanguage("fr"); }) }
    };

    auto palette = std::make_unique<CommandPaletteComponent>(
        std::move(entries), tr("command.placeholder"), tr("command.noResults"),
        tr("command.help"));
    auto* palettePointer = palette.get();
    juce::CallOutBox::launchAsynchronously(
        std::move(palette), commandSearchButton_.getScreenBounds(), nullptr);
    juce::Timer::callAfterDelay(20, [pointer = juce::Component::SafePointer<CommandPaletteComponent>(palettePointer)] {
        if (pointer != nullptr)
            pointer->focusSearch();
    });
}

// The arrangement tool can be chosen from the toolbar selector or, in the
// arrangement itself, from the five tool buttons that show which one is active.
// Exactly one of the two is on screen; this asserts that whichever it is, it is
// laid out and explains itself.
bool WorkspaceComponent::exportIsReachable()
{
    // Exporting the mix lives in the File menu and on Ctrl+E; its toolbar
    // button is a shortcut that gives its room to the view tabs on a narrow
    // window. What has to hold is that the command is reachable.
    if (masterExportButton_.isVisible())
        return masterExportButton_.getWidth() > 0
               && masterExportButton_.getTooltip().isNotEmpty();
    return fileMenuButton_.isVisible() && fileMenuButton_.getWidth() > 0
           && fileMenuButton_.getTooltip().isNotEmpty();
}

bool WorkspaceComponent::toolSelectionIsReachable()
{
    if (arrangementToolSelector_.isVisible())
        return arrangementToolSelector_.getWidth() > 0
               && arrangementToolSelector_.getTooltip().isNotEmpty();
    const std::array<juce::TextButton*, 5> tools {
        &timelinePointerButton_, &timelineKnifeButton_,
        &timelineStretchButton_, &timelineZoomButton_, &timelineHandButton_
    };
    return std::all_of(tools.begin(), tools.end(),
                       [](juce::TextButton* button) {
                           return button->isVisible()
                                  && button->getWidth() > 0
                                  && button->getTooltip().isNotEmpty();
                       });
}

bool WorkspaceComponent::saveVisualSnapshot(const juce::File& destination)
{
    if (destination == juce::File {}
        || !destination.getParentDirectory().createDirectory())
        return false;
    auto image = createComponentSnapshot(getLocalBounds(), true, 1.0f);
    if (!image.isValid())
        return false;
    auto output = destination.createOutputStream();
    if (output == nullptr || !output->setPosition(0)
        || output->truncate().failed())
        return false;
    juce::PNGImageFormat png;
    return png.writeImageToStream(image, *output);
}

void WorkspaceComponent::timerCallback()
{
    const auto tickStart = juce::Time::getMillisecondCounterHiRes();
    // Sections, for the probe's note when the tick is slow.
    auto tickMark = tickStart;
    juce::String tickSections;
    const auto tickSection = [this, &tickMark, &tickSections](const char* name) {
        if (uiHitchProbe_ == nullptr)
            return;
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (now - tickMark >= 0.5)
            tickSections << name << "=" << juce::String(now - tickMark, 1) << " ";
        tickMark = now;
    };
    if (uiHitchProbe_ != nullptr && uiHitchLastPlaying_ != audio_.isPlaying()) {
        uiHitchLastPlaying_ = audio_.isPlaying();
        noteUiHitchEvent(uiHitchLastPlaying_ ? "transport-running"
                                             : "transport-stopped");
    }
    ++uiTimerTick_;
    applyTrackModulations();
    tickSection("modulations");
    // Whichever chain the editor shows — an instrument's, an audio track's,
    // a bus's — reports its control nodes' values every block; the graph
    // turns them into the dot on each modulated knob's ring. Only while the
    // editor shows the selection, or a dot could land on another graph's
    // knob of the same id. Cleared once when the transport stops, so the
    // dots do not sit where the sound last was.
    if (audio_.isPlaying() && editorShowsSelection()) {
        nodeGraph_.setControlValues(
            isVirtualBusNodeGraphContext()
                ? audio_.virtualBusControlValues(selectedVirtualBusGraphId_)
            : isMidiNodeGraphContext()
                ? audio_.midiTrackControlValues(selectedMidiTrackId_)
                : audio_.trackControlValues(*selectedTrack_));
        controlValuesShown_ = true;
    } else if (controlValuesShown_) {
        nodeGraph_.setControlValues({});
        controlValuesShown_ = false;
    }
    tickSection("controls");
    // Not in the middle of a gesture: building the document costs tens of
    // milliseconds on this thread, and a fader under the mouse would jump.
    if (recoveryEnabled_ && projectDirty_
        && juce::Time::currentTimeMillis() >= nextProjectAutosaveMs_
        && !projectAutosaveInProgress_.load(std::memory_order_acquire)
        && !juce::ModifierKeys::getCurrentModifiersRealtime()
                .isAnyMouseButtonDown())
        scheduleProjectAutosave();
    tickSection("autosave");
    audio_.pollPluginWorkers();
    tickSection("workers");
    if (launcherRecordingTrack_.has_value()
        && audio_.recordingComplete())
        finishLauncherRecording(true);
    const auto midiRecordingChanged = audio_.pollMidiRecording();
    auto midiRecordingStopped = false;
    if (audio_.isMidiRecording() && !audio_.isPlaying()) {
        audio_.stopMidiRecording(true);
        midiRecordingStopped = true;
    }
    if (midiRecordingChanged || midiRecordingStopped) {
        refreshMidiEditor();
        markProjectDirty();
    } else {
        pianoRoll_.setRecordingState(
            audio_.isMidiRecording(), audio_.midiRecordingNoteCount(),
            audio_.droppedMidiRecordingEvents());
        pianoRoll_.setHistoryState(audio_.canUndoMidiEdit(),
                                   audio_.canRedoMidiEdit());
    }
    if (audio_.isMasterExporting() && audio_.masterExportComplete()) {
        const auto completedFile = currentMasterExportFile_;
        const auto result = audio_.finishMasterExport();
        currentMasterExportFile_ = {};
        playButton_.setEnabled(true);
        recordButton_.setEnabled(true);
        loopButton_.setEnabled(true);
        masterExportButton_.setButtonText(tr("toolbar.exportMix"));
        masterExportButton_.setTooltip(tr("masterExport.help"));
        refreshClipLauncher();
        if (result.failed()) {
            masterExportStatus_ = tr("status.masterExportFailed");
            showError(tr("error.masterExportTitle"), result.getErrorMessage());
        } else {
            masterExportStatus_ = tr("status.masterExported", "file",
                                     completedFile.getFileName());
        }
    } else if (currentMasterExportFile_ != juce::File {}
               && !audio_.isMasterExporting()) {
        audio_.cancelMasterExport();
        currentMasterExportFile_ = {};
        masterExportStatus_ = tr("status.masterExportFailed");
        playButton_.setEnabled(true);
        recordButton_.setEnabled(true);
        loopButton_.setEnabled(true);
        masterExportButton_.setButtonText(tr("toolbar.exportMix"));
        masterExportButton_.setTooltip(tr("masterExport.help"));
        showError(tr("error.masterExportTitle"),
                  tr("error.masterExportInterrupted"));
    }
    tickSection("recording");
    auto selectedFrameReceived = false;
    for (std::size_t trackIndex = 0;
         trackIndex < audio_.trackCount(); ++trackIndex) {
        cydaw::engine::AudioAnalysisFrame frame;
        cydaw::engine::AudioAnalysisFrame latest;
        auto received = false;
        while (audio_.popTrackAnalysisFrame(trackIndex, frame)) {
            latest = frame;
            received = true;
        }
        const std::array<float, 2> meterPeak {
            received ? latest.peak[0] : 0.0f,
            received ? latest.peak[1] : 0.0f
        };
        const std::array<float, 2> meterRms {
            received ? latest.rms[0] : 0.0f,
            received ? latest.rms[1] : 0.0f
        };
        if (trackIndex < trackLanes_.size())
            trackLanes_[trackIndex]->setMeterLevels(meterPeak, meterRms);
        clipLauncher_.setTrackMeterLevels(trackIndex, meterPeak, meterRms);
        mixer_.setChannelMeterLevels(trackIndex, meterPeak, meterRms);
        clipLauncher_.setTrackPlaybackState(
            trackIndex, audio_.activeLauncherScene(trackIndex),
            audio_.pendingLauncherScene(trackIndex),
            audio_.launcherClipProgress(trackIndex));
        if (!isMidiNodeGraphContext()
            && selectedTrack_ && *selectedTrack_ == trackIndex) {
            if (received) {
                nodeGraph_.setAnalysisFrame(latest);
                deviceChain_.setNodeAnalysisFrame(latest);
                selectedFrameReceived = true;
                for (std::size_t channel = 0; channel < 2; ++channel) {
                    selectedTrackPeak_[channel] = std::max(
                        latest.peak[channel],
                        selectedTrackPeak_[channel] * 0.86f);
                    selectedTrackRms_[channel]
                        = selectedTrackRms_[channel] * 0.72f
                          + latest.rms[channel] * 0.28f;
                }
            }
        }
    }
    tickSection("trackFrames");
    {
        const auto midiTracks = audio_.midiTracks();
        const auto mixerShowing = mixer_.isVisible();
        // The launcher's strips meter their MIDI tracks as the mixer's do;
        // they read silence while the mixer alone was fed.
        const auto launcherShowing = clipLauncher_.isVisible();
        for (std::size_t index = 0; index < midiTracks.size(); ++index) {
            const auto trackId = midiTracks[index].id;
            const auto channelIndex = audio_.trackCount() + index;
            clipLauncher_.setTrackPlaybackState(
                channelIndex,
                audio_.activeMidiLauncherScene(trackId),
                -1,
                audio_.midiLauncherClipProgress(trackId));
            // A track's analysis ring is drained once per tick, and what
            // comes out feeds every view that shows it.
            const auto selectedInGraph
                = isMidiNodeGraphContext() && trackId == selectedMidiTrackId_;
            if (!selectedInGraph && !mixerShowing && !launcherShowing)
                continue;
            cydaw::engine::AudioAnalysisFrame frame;
            cydaw::engine::AudioAnalysisFrame latest;
            auto received = false;
            while (audio_.popMidiTrackAnalysisFrame(trackId, frame)) {
                latest = frame;
                received = true;
            }
            const std::array<float, 2> midiPeak {
                received ? latest.peak[0] : 0.0f,
                received ? latest.peak[1] : 0.0f
            };
            const std::array<float, 2> midiRms {
                received ? latest.rms[0] : 0.0f,
                received ? latest.rms[1] : 0.0f
            };
            if (mixerShowing)
                mixer_.setChannelMeterLevels(channelIndex, midiPeak, midiRms);
            if (launcherShowing)
                clipLauncher_.setTrackMeterLevels(channelIndex, midiPeak,
                                                  midiRms);
            if (!selectedInGraph || !received)
                continue;
            nodeGraph_.setAnalysisFrame(latest);
            deviceChain_.setNodeAnalysisFrame(latest);
            selectedFrameReceived = true;
            for (std::size_t channel = 0; channel < 2; ++channel) {
                selectedTrackPeak_[channel] = std::max(
                    latest.peak[channel],
                    selectedTrackPeak_[channel] * 0.86f);
                selectedTrackRms_[channel]
                    = selectedTrackRms_[channel] * 0.72f
                      + latest.rms[channel] * 0.28f;
            }
        }
    }
    if (!selectedFrameReceived) {
        for (std::size_t channel = 0; channel < 2; ++channel) {
            selectedTrackPeak_[channel] *= 0.86f;
            selectedTrackRms_[channel] *= 0.72f;
        }
    }
    if (!selectedTrack_ && !isMidiNodeGraphContext()
        && nodeGraph_.hasAnalysisFrame()) {
        nodeGraph_.clearAnalysis();
        deviceChain_.clearNodeAnalysis();
    }
    tickSection("midiFrames");
    cydaw::engine::AudioAnalysisFrame masterFrame;
    cydaw::engine::AudioAnalysisFrame latestMaster;
    auto masterReceived = false;
    while (audio_.popMasterAnalysisFrame(masterFrame)) {
        latestMaster = masterFrame;
        masterReceived = true;
    }
    for (std::size_t channel = 0; channel < 2; ++channel) {
        if (masterReceived) {
            masterPeak_[channel] = std::max(
                latestMaster.peak[channel], masterPeak_[channel] * 0.86f);
            masterRms_[channel] = masterRms_[channel] * 0.72f
                                  + latestMaster.rms[channel] * 0.28f;
        } else {
            masterPeak_[channel] *= 0.86f;
            masterRms_[channel] *= 0.72f;
        }
    }
    clipLauncher_.setMasterMeterLevels(masterPeak_, masterRms_);
    mixer_.setMasterMeterLevels(masterPeak_, masterRms_);
    masterLane_.setLevels(masterPeak_, masterRms_);
    deviceChain_.setMeterLevels(selectedTrackPeak_, selectedTrackRms_,
                                masterPeak_, masterRms_);
    tickSection("meters");
    const auto position = audio_.position();
    replayLauncherActions(position);
    if (audio_.isPlaying()
        && editorModeSelector_.getSelectedId() == 1
        && arrangementZoom_ > 1.000001) {
        const auto visibleDuration = arrangementTimelineLengthSeconds_
                                     / arrangementZoom_;
        const auto visibleEnd = arrangementViewStartSeconds_
                                + visibleDuration;
        if (position < arrangementViewStartSeconds_
            || position > visibleEnd) {
            arrangementViewStartSeconds_ = position
                                           - visibleDuration * 0.1;
            refreshArrangementView();
        }
    }
    positionLabel_.setText(formatTime(position), juce::dontSendNotification);
    for (auto& lane : trackLanes_)
        lane->setPlayhead(position);
    updateArrangementPlayhead(position);
    timelineRuler_.setPlayhead(position);
    if (currentEditorMode_ != 2)
        pianoRoll_.setPlayhead(position);
    automationEditor_.setPlayhead(position);
    const auto contextualPlayhead = position
                                    - contextualAutomationOffsetSeconds_;
    bottomAutomationEditor_.setPlayhead(contextualPlayhead);
    contextAutomationEditor_.setPlayhead(contextualPlayhead);
    audioEditor_.setPlayhead(position);
    if (currentEditorMode_ == 2 && selectedMidiTrackId_ != 0
        && selectedMidiClipId_ != 0) {
        const auto previewing = audio_.isMidiClipEditorPreviewing(
            selectedMidiTrackId_, selectedMidiClipId_);
        const auto previewPosition
            = audio_.midiClipEditorPreviewPositionSeconds(
                selectedMidiTrackId_);
        if (previewPosition >= 0.0)
            clipEditorCueSeconds_ = previewPosition;
        pianoRoll_.setPlayhead(clipEditorCueSeconds_);
        pianoRoll_.setTransportState(previewing);
        bottomAutomationEditor_.setPlayhead(clipEditorCueSeconds_);
        contextAutomationEditor_.setPlayhead(clipEditorCueSeconds_);
    } else if (launcherEditorContext_) {
        const auto context = *launcherEditorContext_;
        const auto active = audio_.activeLauncherScene(
                                context.trackIndex)
                            == context.sceneIndex;
        if (active)
            clipEditorCueSeconds_ = audio_.launcherClipPositionSeconds(
                context.trackIndex);
        clipTimelineEditor_.setPlayhead(clipEditorCueSeconds_);
        clipTimelineEditor_.setTransportState(active);
        // The clip automation lane follows the clip transport, not the
        // ever-running global clock. When stopped this deliberately keeps
        // the last clip-local position frozen.
        bottomAutomationEditor_.setPlayhead(clipEditorCueSeconds_);
        contextAutomationEditor_.setPlayhead(clipEditorCueSeconds_);
    } else if (selectedTrack_ && selectedAudioClipId_ != 0) {
        const auto clip = audio_.audioClip(*selectedTrack_,
                                           selectedAudioClipId_);
        const auto previewing = audio_.isAudioClipPreviewing();
        if (previewing)
            clipEditorCueSeconds_
                = audio_.audioClipPreviewPositionSeconds();
        clipTimelineEditor_.setPlayhead(
            previewing ? clipEditorCueSeconds_
                       : clip.has_value() ? clipEditorCueSeconds_ : -1.0);
        clipTimelineEditor_.setTransportState(previewing);
        if (clip.has_value()) {
            bottomAutomationEditor_.setPlayhead(
                clipEditorCueSeconds_);
            contextAutomationEditor_.setPlayhead(
                clipEditorCueSeconds_);
        }
    }
    tickSection("playheads");
    if (uiTimerTick_ % 6 == 0)
        refreshDevicePluginChain();
    playButton_.setButtonText(audio_.isPlaying() ? juce::String::fromUTF8("❚❚")
                                                  : juce::String::fromUTF8("▶"));
    timelinePlayButton_.setButtonText(
        audio_.isPlaying() ? juce::String::fromUTF8("❚❚")
                           : juce::String::fromUTF8("▶"));
    timelinePlayButton_.setTooltip(
        audio_.isPlaying() ? tr("transport.pause") : tr("transport.play"));
    recordButton_.setButtonText(
        (audio_.isRecording() || audio_.isMidiRecording())
            ? juce::String::fromUTF8("■")
            : juce::String::fromUTF8("●"));
    recordButton_.setTooltip(
        audio_.isMidiRecording()
            ? tr("transport.stopMidiRecording")
            : audio_.isRecording()
                  ? tr("transport.stopRecording")
                  : editorModeSelector_.getSelectedId() == 2
                        ? tr("transport.recordMidi")
                        : tr("transport.record"));

    tickSection("buttons");
    auto status = audio_.audioDeviceDescription();
    if (audio_.isMasterExporting()) {
        const auto percent = juce::roundToInt(audio_.masterExportProgress() * 100.0);
        const auto progress = juce::String(percent) + "%";
        masterExportButton_.setButtonText(tr("toolbar.cancelExport") + " " + progress);
        status = tr("status.masterExporting", "percent", progress) + "  |  " + status;
    } else if (masterExportStatus_.isNotEmpty()) {
        status = masterExportStatus_ + "  |  " + status;
    }
    if (transientStatus_.isNotEmpty()) {
        if (juce::Time::currentTimeMillis() <= transientStatusExpiryMs_)
            status = transientStatus_ + "  |  " + status;
        else
            transientStatus_.clear();
    }
    if (audio_.isRecording()) {
        auto recordingStatus = launcherRecordingTrack_.has_value()
                                   ? tr(audio_.recordingStopPending()
                                            ? "status.launcherStopPending"
                                            : audio_.recordingStarted()
                                                  ? "status.launcherRecording"
                                                  : "status.launcherRecordPending")
                                   : audioRecordingTargetClipId_.has_value()
                                         ? tr("status.recordingAlternativeTake")
                                         : audioRecordingTargetTrack_.has_value()
                                               ? tr("status.recordingToTrack")
                                               : tr("status.recordingToNewTrack");
        recordingStatus = recordingStatus.replace(
            "{track}", juce::String(static_cast<int>(
                           launcherRecordingTrack_.value_or(
                               audioRecordingTargetTrack_.value_or(0)) + 1)));
        recordingStatus = recordingStatus.replace(
            "{scene}", juce::String(launcherRecordingScene_ + 1));
        recordingStatus = recordingStatus.replace(
            "{time}", formatTime(
                          launcherRecordingTrack_.has_value()
                              ? launcherRecordingStartSeconds_
                              : audioRecordingStartSeconds_));
        status = recordingStatus + "  |  " + status;
    }
    if (audio_.isMidiRecording())
        status = tr("status.midiRecording", "count",
                    juce::String(static_cast<int>(
                        audio_.midiRecordingNoteCount())))
                 + "  |  " + status;
    if (audio_.droppedRecordingBlocks() > 0)
        status += "  |  " + tr("status.dropped") + " " + juce::String(audio_.droppedRecordingBlocks());
    if (audio_.mixLatencySamples() > 0)
        status += "  |  " + tr("status.pdc", "count", juce::String(audio_.mixLatencySamples()));
    if (audio_.isMasterExporting() && audio_.droppedMasterExportBlocks() > 0)
        status += "  |  " + tr("status.masterExportDropped", "count",
                                juce::String(audio_.droppedMasterExportBlocks()));
    statusLabel_.setText(status, juce::dontSendNotification);
    // A tick that took longer than a frame's share is worth a note.
    if (uiHitchProbe_ != nullptr) {
        tickSection("status");
        const auto tickMs = juce::Time::getMillisecondCounterHiRes() - tickStart;
        if (tickMs > 6.0)
            noteUiHitchEvent("tick=" + juce::String(tickMs, 1) + "{"
                             + tickSections.trim() + "}");
    }
}

void WorkspaceComponent::changeListenerCallback(juce::ChangeBroadcaster*)
{
    refreshTranslations();
}

void WorkspaceComponent::refreshTranslations()
{
    fileMenuButton_.setButtonText(tr("menu.file"));
    editMenuButton_.setButtonText(tr("menu.edit"));
    viewMenuButton_.setButtonText(tr("menu.view"));
    fileMenuButton_.setTooltip(tr("menu.fileHelp"));
    editMenuButton_.setTooltip(tr("menu.editHelp"));
    viewMenuButton_.setTooltip(tr("menu.viewHelp"));
    openAudioButton_.setButtonText(tr("toolbar.openAudio"));
    saveProjectButton_.setButtonText(tr("toolbar.save"));
    loadProjectButton_.setButtonText(tr("toolbar.load"));
    masterExportButton_.setButtonText(audio_.isMasterExporting()
                                          ? tr("toolbar.cancelExport")
                                          : tr("toolbar.exportMix"));
    masterExportButton_.setTooltip(audio_.isMasterExporting()
                                       ? tr("masterExport.cancelHelp")
                                       : tr("masterExport.help"));
    audioSettingsButton_.setButtonText(tr("toolbar.audio"));
    detachGraphButton_.setButtonText(graphWindow_ != nullptr ? tr("toolbar.dockGraph")
                                                              : tr("toolbar.floatGraph"));
    saveLayoutButton_.setButtonText(tr("layout.save"));
    layoutPanelsButton_.setButtonText(tr("layout.windowsShort"));
    deleteLayoutButton_.setButtonText(juce::String::fromUTF8("×"));
    undoLayoutButton_.setButtonText(juce::String::fromUTF8("↶"));
    redoLayoutButton_.setButtonText(juce::String::fromUTF8("↷"));
    commandSearchButton_.setButtonText(tr("command.searchShort"));
    saveLayoutButton_.setTooltip(tr("layout.saveHelp"));
    layoutPanelsButton_.setTooltip(tr("layout.panelsHelp"));
    inspectorHideButton_.setButtonText("<");
    inspectorShowButton_.setButtonText(">");
    browserHideButton_.setButtonText(">");
    browserShowButton_.setButtonText("<");
    bottomHideButton_.setButtonText(juce::String::fromUTF8("⌄"));
    bottomShowButton_.setButtonText(juce::String::fromUTF8("⌃"));
    contextAutomationToggleButton_.setButtonText(
        contextAutomationVisible_ ? juce::String::fromUTF8("⌄")
                                  : juce::String::fromUTF8("⌃"));
    inspectorHideButton_.setTooltip(tr("layout.hideInspector"));
    inspectorShowButton_.setTooltip(tr("layout.showInspector"));
    browserHideButton_.setTooltip(tr("layout.hideBrowser"));
    browserShowButton_.setTooltip(tr("layout.showBrowser"));
    bottomHideButton_.setTooltip(tr("layout.hideBottom"));
    bottomShowButton_.setTooltip(tr("layout.showBottom"));
    contextAutomationToggleButton_.setTooltip(
        contextAutomationVisible_ ? tr("automation.hideContext")
                                  : tr("automation.showContext"));
    contextAutomationToggleButton_.setButtonText(
        contextAutomationVisible_ ? "AUTO v" : "AUTO ^");
    deleteLayoutButton_.setTooltip(tr("layout.deleteHelp"));
    undoLayoutButton_.setTooltip(tr("layout.undoHelp"));
    redoLayoutButton_.setTooltip(tr("layout.redoHelp"));
#if JUCE_MAC
    commandSearchButton_.setTooltip(tr("command.searchHelp", "shortcut", "Cmd+K"));
#else
    commandSearchButton_.setTooltip(tr("command.searchHelp", "shortcut", "Ctrl+K"));
#endif
    refreshLayoutHistoryButtons();
    refreshFloatingPanelTitles();
    refreshLayoutSelector(layoutSelector_.getSelectedId());
    const auto selectedEditorMode = juce::jlimit(
        1, 9, currentEditorMode_);
    editorModeSelector_.clear(juce::dontSendNotification);
    editorModeSelector_.addItem(tr("editor.arrange"), 1);
    editorModeSelector_.addItem(tr("editor.midi"), 2);
    editorModeSelector_.addItem(tr("editor.automation"), 3);
    editorModeSelector_.addItem(tr("editor.launcher"), 4);
    editorModeSelector_.addItem(tr("editor.audio"), 5);
    editorModeSelector_.addItem(tr("editor.clip"), 6);
    editorModeSelector_.addItem(tr("editor.graphEdit"), 7);
    editorModeSelector_.addItem(tr("editor.buses"), 8);
    editorModeSelector_.addItem(tr("editor.mixer"), 9);
    editorModeSelector_.addItem(tr("editor.samples"), 10);
    editorModeSelector_.setSelectedId(selectedEditorMode, juce::dontSendNotification);
    launcherViewButton_.setButtonText(tr("editor.launcher"));
    arrangementViewButton_.setButtonText(tr("editor.arrange"));
    clipViewButton_.setButtonText(tr("editor.clipShort"));
    graphEditViewButton_.setButtonText(tr("editor.graphEdit"));
    busViewButton_.setButtonText(tr("editor.buses"));
    mixerViewButton_.setButtonText(tr("editor.mixer"));
    samplesViewButton_.setButtonText(tr("editor.samples"));
    samplesViewButton_.setTooltip(tr("editor.samplesHelp"));
    sampleExplorer_.refreshTranslations();
    launcherViewButton_.setTooltip(tr("editor.launcherHelp"));
    arrangementViewButton_.setTooltip(tr("editor.arrangeHelp"));
    clipViewButton_.setTooltip(tr("editor.clipHelp"));
    graphEditViewButton_.setTooltip(tr("editor.graphEditHelp"));
    busViewButton_.setTooltip(tr("editor.busesHelp"));
    mixerViewButton_.setTooltip(tr("editor.mixerHelp"));
    const auto selectedSnap = juce::jlimit(
        1, 5, snapSelector_.getSelectedId() == 0
                  ? 4
                  : snapSelector_.getSelectedId());
    snapSelector_.clear(juce::dontSendNotification);
    snapSelector_.addItem(tr("snap.off"), 1);
    snapSelector_.addItem("1/4", 2);
    snapSelector_.addItem("1/8", 3);
    snapSelector_.addItem("1/16", 4);
    snapSelector_.addItem("1/32", 5);
    snapSelector_.setSelectedId(selectedSnap, juce::dontSendNotification);
    snapSelector_.setTooltip(tr("snap.help"));
    const auto selectedSnapType = juce::jlimit(
        1, 3, snapTypeSelector_.getSelectedId() == 0
                  ? 1
                  : snapTypeSelector_.getSelectedId());
    snapTypeSelector_.clear(juce::dontSendNotification);
    snapTypeSelector_.addItem(tr("snap.type.grid"), 1);
    snapTypeSelector_.addItem(tr("snap.type.relative"), 2);
    snapTypeSelector_.addItem(tr("snap.type.events"), 3);
    snapTypeSelector_.setSelectedId(selectedSnapType,
                                    juce::dontSendNotification);
    snapTypeSelector_.setTooltip(tr("snap.type.help"));
    const auto selectedTool = juce::jlimit(
        1, 5, arrangementToolSelector_.getSelectedId() == 0
                  ? 1
                  : arrangementToolSelector_.getSelectedId());
    arrangementToolSelector_.clear(juce::dontSendNotification);
    arrangementToolSelector_.addItem(tr("tool.pointer"), 1);
    arrangementToolSelector_.addItem(tr("tool.knife"), 2);
    arrangementToolSelector_.addItem(tr("tool.stretch"), 3);
    arrangementToolSelector_.addItem(tr("tool.zoom"), 4);
    arrangementToolSelector_.addItem(tr("tool.hand"), 5);
    arrangementToolSelector_.setSelectedId(selectedTool,
                                            juce::dontSendNotification);
    arrangementToolSelector_.setTooltip(tr("tool.help"));
    timelineSnapSelector_.clear(juce::dontSendNotification);
    timelineSnapSelector_.addItem(tr("snap.off"), 1);
    timelineSnapSelector_.addItem("1/4", 2);
    timelineSnapSelector_.addItem("1/8", 3);
    timelineSnapSelector_.addItem("1/16", 4);
    timelineSnapSelector_.addItem("1/32", 5);
    timelineSnapSelector_.setSelectedId(selectedSnap,
                                         juce::dontSendNotification);
    timelineSnapSelector_.setTooltip(tr("snap.help"));
    timelineSnapTypeSelector_.clear(juce::dontSendNotification);
    timelineSnapTypeSelector_.addItem(tr("snap.type.grid"), 1);
    timelineSnapTypeSelector_.addItem(tr("snap.type.relative"), 2);
    timelineSnapTypeSelector_.addItem(tr("snap.type.events"), 3);
    timelineSnapTypeSelector_.setSelectedId(selectedSnapType,
                                             juce::dontSendNotification);
    timelineSnapTypeSelector_.setTooltip(tr("snap.type.help"));
    timelinePointerButton_.setButtonText(juce::String::fromUTF8("↖"));
    timelineKnifeButton_.setButtonText(juce::String::fromUTF8("✂"));
    timelineStretchButton_.setButtonText(juce::String::fromUTF8("↔"));
    timelineZoomButton_.setButtonText(juce::String::fromUTF8("⊕"));
    timelineHandButton_.setButtonText(juce::String::fromUTF8("✋"));
    timelinePointerButton_.setTooltip(tr("tool.pointer"));
    timelineKnifeButton_.setTooltip(tr("tool.knife"));
    timelineStretchButton_.setTooltip(tr("tool.stretch"));
    timelineZoomButton_.setTooltip(tr("tool.zoom"));
    timelineHandButton_.setTooltip(tr("tool.hand"));
    timelinePlayButton_.setButtonText(
        audio_.isPlaying() ? juce::String::fromUTF8("❚❚")
                           : juce::String::fromUTF8("▶"));
    timelinePlayButton_.setTooltip(
        audio_.isPlaying() ? tr("transport.pause") : tr("transport.play"));
    timelineStopButton_.setButtonText(juce::String::fromUTF8("■"));
    timelineStopButton_.setTooltip(tr("transport.stopAtPlayhead"));
    addArrangementTrackButton_.setButtonText(tr("track.addShort"));
    addArrangementTrackButton_.setTooltip(tr("track.addHelp"));
    arrangementPlaybackButton_.setButtonText(
        tr("transport.arrangementShort"));
    arrangementPlaybackButton_.setTooltip(
        tr("transport.arrangementHelp"));
    launcherActionRecordButton_.setButtonText(
        tr("launcher.actionsRecordShort"));
    launcherActionRecordButton_.setTooltip(
        tr("launcher.actionsRecordHelp"));
    launcherActionReplayButton_.setButtonText(
        tr("launcher.actionsReplayShort"));
    launcherActionReplayButton_.setTooltip(
        tr("launcher.actionsReplayHelp"));
    tempoSlider_.setTooltip(tr("transport.tempo"));
    playButton_.setButtonText(audio_.isPlaying() ? juce::String::fromUTF8("❚❚")
                                                  : juce::String::fromUTF8("▶"));
    playButton_.setTooltip(audio_.isPlaying() ? tr("transport.pause") : tr("transport.play"));
    stopButton_.setButtonText(juce::String::fromUTF8("■"));
    stopButton_.setTooltip(tr("transport.stop"));
    recordButton_.setButtonText(
        (audio_.isRecording() || audio_.isMidiRecording())
            ? juce::String::fromUTF8("■")
            : juce::String::fromUTF8("●"));
    recordButton_.setTooltip(
        audio_.isMidiRecording()
            ? tr("transport.stopMidiRecording")
            : audio_.isRecording()
                  ? tr("transport.stopRecording")
                  : editorModeSelector_.getSelectedId() == 2
                        ? tr("transport.recordMidi")
                        : tr("transport.record"));
    loopButton_.setButtonText(juce::String::fromUTF8("↻"));
    loopButton_.setTooltip(tr("transport.loop"));
    timelineRuler_.setTooltip(tr("loop.rangeHelp"));
    timelineRuler_.repaint();
    timelineZoomOutButton_.setButtonText("-");
    timelineZoomOutButton_.setTooltip(tr("timeline.zoomOutHelp"));
    timelineFitButton_.setButtonText(
        arrangementZoom_ > 1.000001
            ? juce::String(arrangementZoom_, 1) + "x"
            : tr("timeline.fitShort"));
    timelineFitButton_.setTooltip(tr("timeline.fitHelp"));
    timelineZoomInButton_.setButtonText("+");
    timelineZoomInButton_.setTooltip(tr("timeline.zoomInHelp"));
    arrangementRangeButton_.setButtonText(tr("arrangement.rangeMenu"));
    arrangementRangeButton_.setTooltip(tr("arrangement.rangeHelp"));
    devicesPanelButton_.setButtonText(tr("bottom.devices"));
    graphPanelButton_.setButtonText(tr("bottom.nodeGraph"));
    modulationPanelButton_.setButtonText(tr("bottom.modulation"));
    automationPanelButton_.setButtonText(tr("bottom.trackAutomation"));
    automationPanelButton_.setTooltip(tr("bottom.trackAutomationHelp"));
    moduleScopeButton_.setButtonText(tr("bottom.scopeModule"));
    trackScopeButton_.setButtonText(tr("bottom.scopeTrack"));
    moduleScopeButton_.setTooltip(tr("bottom.scopeModuleHelp"));
    trackScopeButton_.setTooltip(tr("bottom.scopeTrackHelp"));
    modulationTargetSelector_.setTextWhenNothingSelected(
        tr("modulation.chooseTarget"));
    modulationTargetSelector_.setTooltip(tr("modulation.targetHelp"));
    modulationAmountSlider_.setTooltip(tr("modulation.amountHelp"));
    modulationEnabledButton_.setButtonText(tr("modulation.enableShort"));
    modulationEnabledButton_.setTooltip(tr("modulation.enableHelp"));
    muteButton_.setButtonText(tr("track.mute"));
    soloButton_.setButtonText(tr("track.solo"));
    armButton_.setButtonText(tr("track.arm"));
    armButton_.setTooltip(tr("track.armHelp"));
    gainLabel_.setText(tr("mixer.gain"), juce::dontSendNotification);
    panLabel_.setText(tr("mixer.pan"), juce::dontSendNotification);
    sampleGainLabel_.setText(tr("sample.gain"),
                             juce::dontSendNotification);
    sampleGainSlider_.setTooltip(tr("sample.gainHelp"));
    sampleFadeInLabel_.setText(tr("sample.fadeIn"),
                               juce::dontSendNotification);
    sampleFadeOutLabel_.setText(tr("sample.fadeOut"),
                                juce::dontSendNotification);
    sampleFadeInSlider_.setTooltip(tr("sample.fadeInHelp"));
    sampleFadeOutSlider_.setTooltip(tr("sample.fadeOutHelp"));
    clipStartLabel_.setText(tr("clip.start"), juce::dontSendNotification);
    sourceOffsetLabel_.setText(tr("clip.offset"), juce::dontSendNotification);
    clipDurationLabel_.setText(tr("clip.duration"), juce::dontSendNotification);
    clipStretchLabel_.setText(tr("clip.stretch"), juce::dontSendNotification);
    clipStretchSlider_.setTooltip(tr("clip.stretchHelp"));
    sourceTempoLabel_.setText(tr("clip.sourceTempo"),
                              juce::dontSendNotification);
    sourceTempoSlider_.setTooltip(tr("clip.sourceTempoHelp"));
    detectTempoButton_.setButtonText(tr("clip.detectTempoShort"));
    detectTempoButton_.setTooltip(tr("clip.detectTempoHelp"));
    syncTempoButton_.setButtonText(tr("clip.syncTempoShort"));
    syncTempoButton_.setTooltip(tr("clip.syncTempoHelp"));
    stretchModeLabel_.setText(tr("clip.stretchMode"),
                              juce::dontSendNotification);
    const auto selectedStretchMode = juce::jlimit(
        1, 4, stretchModeSelector_.getSelectedId() == 0
                  ? 1
                  : stretchModeSelector_.getSelectedId());
    stretchModeSelector_.clear(juce::dontSendNotification);
    stretchModeSelector_.addItem(tr("clip.stretchMode.repitch"), 1);
    stretchModeSelector_.addItem(tr("clip.stretchMode.tonal"), 2);
    stretchModeSelector_.addItem(tr("clip.stretchMode.beats"), 3);
    stretchModeSelector_.addItem(tr("clip.stretchMode.texture"), 4);
    stretchModeSelector_.setSelectedId(selectedStretchMode,
                                       juce::dontSendNotification);
    stretchModeSelector_.setTooltip(tr("clip.stretchModeHelp"));
    audioTakeLabel_.setText(tr("audioTake.label"),
                            juce::dontSendNotification);
    addAudioTakeButton_.setButtonText(tr("audioTake.addShort"));
    addAudioTakeButton_.setTooltip(tr("audioTake.addHelp"));
    recordAudioTakeButton_.setButtonText(tr("audioTake.recordShort"));
    recordAudioTakeButton_.setTooltip(tr("audioTake.recordHelp"));
    removeAudioTakeButton_.setButtonText(tr("audioTake.removeShort"));
    removeAudioTakeButton_.setTooltip(tr("audioTake.removeHelp"));
    browser_.refreshTranslations();
    inspector_.setTitle(tr("inspector.title"));
    nodeGraph_.refreshTranslations();
    modulationGraph_.refreshTranslations();
    deviceChain_.refreshTranslations();
    pianoRoll_.refreshTranslations();
    automationEditor_.refreshTranslations();
    contextAutomationEditor_.refreshTranslations();
    bottomAutomationEditor_.refreshTranslations();
    audioEditor_.refreshTranslations();
    clipTimelineEditor_.refreshTranslations();
    clipLauncher_.refreshTranslations();
    virtualBusManager_.refreshTranslations();
    mixer_.refreshTranslations();
    refreshAutomationTargets();
    emptyTimelineLabel_.setText(tr("timeline.empty"), juce::dontSendNotification);
    for (auto& lane : trackLanes_)
        lane->refreshTranslations();
    masterLane_.refreshTranslations();
    refreshArrangementInteractionMode();

    languageSelector_.clear(juce::dontSendNotification);
    languageSelector_.addItem(tr("language.english"), 1);
    languageSelector_.addItem(tr("language.french"), 2);
    languageSelector_.setSelectedId(Localization::instance().languageCode() == "fr" ? 2 : 1,
                                    juce::dontSendNotification);
    languageSelector_.setTooltip(tr("language.label"));

    updateTrackDescription();

    const auto projectName = currentProjectFile_ == juce::File {} ? tr("app.untitled")
                                                                   : currentProjectFile_.getFileNameWithoutExtension();
    projectLabel_.setText("CyDAW  /  " + projectName + (projectDirty_ ? " *" : ""), juce::dontSendNotification);
    applyTooltipFallbacks(*this);
}

void WorkspaceComponent::chooseAudioFile()
{
    chooser_ = std::make_unique<juce::FileChooser>(tr("dialog.openAudio"), juce::File {}, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this](const juce::FileChooser& chooser) {
                              const auto file = chooser.getResult();
                              if (file.existsAsFile())
                                  loadAudioFile(file);
                          });
}

void WorkspaceComponent::loadAudioFile(const juce::File& file)
{
    std::size_t trackIndex {};
    const auto result = audio_.addAudioTrack(file, &trackIndex);
    if (result.failed()) {
        showError(tr("error.openAudioTitle"), result.getErrorMessage());
        return;
    }
    audio_.setLooping(loopButton_.getToggleState());
    addTrackLane(trackIndex);
    refreshClipLauncher();
    selectTrack(trackIndex);
    markProjectDirty();
}

void WorkspaceComponent::addEmptyAudioTrack()
{
    std::size_t trackIndex {};
    const auto result = audio_.addEmptyAudioTrack(&trackIndex);
    if (result.failed()) {
        showError(tr("error.openAudioTitle"), result.getErrorMessage());
        return;
    }
    addTrackLane(trackIndex);
    selectTrack(trackIndex);
    refreshClipLauncher();
    refreshTimelineRanges();
    markProjectDirty();
}

void WorkspaceComponent::addEmptyMidiTrack()
{
    if (audio_.isMidiRecording())
        return;
    saveCurrentNodeGraphState();
    const auto count = audio_.midiTracks().size() + 1;
    selectedMidiTrackId_ = audio_.addMidiTrack(
        tr("midi.instrument") + " "
        + juce::String(static_cast<int>(count)));
    const auto tracks = audio_.midiTracks();
    const auto created = std::find_if(
        tracks.begin(), tracks.end(), [this](const auto& track) {
            return track.id == selectedMidiTrackId_;
        });
    if (created != tracks.end())
        for (const auto& clip : created->clips)
            (void) audio_.removeMidiClip(selectedMidiTrackId_, clip.id);
    selectedMidiClipId_ = 0;
    selectedTrack_.reset();
    (void) audio_.setLiveMidiTrack(selectedMidiTrackId_);
    restoreNodeGraphForMidiTrack(selectedMidiTrackId_);
    if (currentEditorMode_ == 4) {
        refreshClipLauncher();
    } else {
        setEditorMode(2);
        refreshMidiEditor();
    }
    refreshTimelineRanges();
    markProjectDirty();
}

std::optional<std::uint64_t> WorkspaceComponent::launcherMidiTrackId(
    const std::size_t launcherTrackIndex) const
{
    if (launcherTrackIndex < audio_.trackCount())
        return std::nullopt;
    const auto tracks = audio_.midiTracks();
    const auto index = launcherTrackIndex - audio_.trackCount();
    return index < tracks.size()
               ? std::optional<std::uint64_t>(tracks[index].id)
               : std::nullopt;
}

std::optional<AudioTrackController::MidiClip>
WorkspaceComponent::launcherMidiClip(const std::uint64_t trackId,
                                     const int sceneIndex) const
{
    const auto tracks = audio_.midiTracks();
    const auto track = std::find_if(
        tracks.begin(), tracks.end(), [trackId](const auto& value) {
            return value.id == trackId;
        });
    if (track == tracks.end())
        return std::nullopt;
    const auto clip = std::find_if(
        track->clips.begin(), track->clips.end(),
        [sceneIndex](const auto& value) {
            return juce::roundToInt(value.startBeats / 16.0)
                   == sceneIndex;
        });
    return clip != track->clips.end()
               ? std::optional<AudioTrackController::MidiClip>(*clip)
               : std::nullopt;
}

void WorkspaceComponent::selectLauncherMidiTrack(
    const std::uint64_t trackId)
{
    saveCurrentNodeGraphState();
    selectedTrack_.reset();
    selectedAudioClipId_ = 0;
    selectedAudioClipIds_.clear();
    selectedMidiTrackId_ = trackId;
    launcherInspectorSelection_.reset();
    inspectorSelection_ = InspectorSelection::midiTrack;
    (void) audio_.setLiveMidiTrack(trackId);
    restoreNodeGraphForMidiTrack(trackId);
    refreshNodeDeviceSurface();
    refreshMidiTrackMixerControls();
    refreshClipLauncher();
    updateTrackDescription();
}

void WorkspaceComponent::refreshMidiTrackMixerControls()
{
    if (selectedTrack_ || selectedMidiTrackId_ == 0)
        return;
    const auto tracks = audio_.midiTracks();
    const auto track = std::find_if(
        tracks.begin(), tracks.end(), [this](const auto& candidate) {
            return candidate.id == selectedMidiTrackId_;
        });
    if (track == tracks.end())
        return;
    for (auto* control : { static_cast<juce::Component*>(&gainSlider_),
                           static_cast<juce::Component*>(&panSlider_),
                           static_cast<juce::Component*>(&muteButton_),
                           static_cast<juce::Component*>(&soloButton_) })
        control->setEnabled(true);
    gainSlider_.setValue(track->gainDb, juce::dontSendNotification);
    panSlider_.setValue(track->pan, juce::dontSendNotification);
    muteButton_.setToggleState(track->muted, juce::dontSendNotification);
    soloButton_.setToggleState(track->solo, juce::dontSendNotification);
    deviceChain_.setParameters(0.0f, 20'000.0f, track->gainDb, track->pan,
                               true);
}

void WorkspaceComponent::showAddTrackMenu(juce::Component* target)
{
    juce::PopupMenu menu;
    menu.addItem(1, tr("track.typeAudio"));
    menu.addItem(2, tr("midi.track"));
    const auto safe = juce::Component::SafePointer<WorkspaceComponent>(this);
    auto options = juce::PopupMenu::Options();
    if (target != nullptr)
        options = options.withTargetComponent(target);
    menu.showMenuAsync(options, [safe](const int result) {
        if (safe == nullptr)
            return;
        if (result == 1)
            safe->addEmptyAudioTrack();
        else if (result == 2)
            safe->addEmptyMidiTrack();
    });
}

void WorkspaceComponent::chooseMidiImportFile()
{
    chooser_ = std::make_unique<juce::FileChooser>(
        tr("dialog.importMidi"), juce::File {}, "*.mid;*.midi");
    chooser_->launchAsync(
        juce::FileBrowserComponent::openMode
            | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& chooser) {
            const auto file = chooser.getResult();
            if (file.existsAsFile())
                importMidiFile(file);
        });
}

void WorkspaceComponent::importMidiFile(const juce::File& file)
{
    AudioTrackController::MidiFileImportSummary summary;
    const auto result = audio_.importMidiFile(file, summary, true);
    if (result.failed()) {
        showError(tr("error.midiImportTitle"), result.getErrorMessage());
        return;
    }
    if (summary.trackIds.empty())
        return;
    selectedMidiTrackId_ = summary.trackIds.front();
    selectedMidiClipId_ = 0;
    const auto tracks = audio_.midiTracks();
    const auto selectedTrack = std::find_if(
        tracks.begin(), tracks.end(), [this](const auto& track) {
            return track.id == selectedMidiTrackId_;
        });
    if (selectedTrack != tracks.end() && !selectedTrack->clips.empty())
        selectedMidiClipId_ = selectedTrack->clips.front().id;
    tempoSlider_.setValue(audio_.tempo(), juce::dontSendNotification);
    editorModeSelector_.setSelectedId(2, juce::sendNotificationSync);
    refreshMidiEditor();
    refreshTimelineRanges();
    transientStatus_ = tr(
        "status.midiImported", "tracks",
        juce::String(static_cast<int>(summary.trackIds.size())));
    transientStatus_ = transientStatus_.replace(
        "{notes}", juce::String(static_cast<int>(summary.noteCount)));
    transientStatusExpiryMs_ = juce::Time::currentTimeMillis() + 5'000;
    markProjectDirty();
}

void WorkspaceComponent::chooseMidiExportFile()
{
    if (selectedMidiTrackId_ == 0 || selectedMidiClipId_ == 0) {
        showError(tr("error.midiExportTitle"),
                  tr("error.midiExportNoClip"));
        return;
    }
    auto suggestedName = tr("midi.clip");
    const auto tracks = audio_.midiTracks();
    const auto track = std::find_if(
        tracks.begin(), tracks.end(), [this](const auto& candidate) {
            return candidate.id == selectedMidiTrackId_;
        });
    if (track != tracks.end()) {
        const auto clip = std::find_if(
            track->clips.begin(), track->clips.end(), [this](const auto& item) {
                return item.id == selectedMidiClipId_;
            });
        if (clip != track->clips.end())
            suggestedName = clip->name;
    }
    const auto suggested = juce::File::getSpecialLocation(
                               juce::File::userDocumentsDirectory)
                               .getChildFile(
                                   juce::File::createLegalFileName(
                                       suggestedName)
                                   + ".mid");
    chooser_ = std::make_unique<juce::FileChooser>(
        tr("dialog.exportMidi"), suggested, "*.mid");
    chooser_->launchAsync(
        juce::FileBrowserComponent::saveMode
            | juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& chooser) {
            const auto file = chooser.getResult();
            if (file != juce::File {})
                exportSelectedMidiClip(file.withFileExtension("mid"));
        });
}

void WorkspaceComponent::exportSelectedMidiClip(const juce::File& file)
{
    const auto result = audio_.exportMidiClipToFile(
        selectedMidiTrackId_, selectedMidiClipId_, file);
    if (result.failed()) {
        showError(tr("error.midiExportTitle"), result.getErrorMessage());
        return;
    }
    transientStatus_ = tr("status.midiExported", "file",
                          file.getFileName());
    transientStatusExpiryMs_ = juce::Time::currentTimeMillis() + 5'000;
}

void WorkspaceComponent::chooseAudioTakeFile()
{
    if (!selectedTrack_ || selectedAudioClipId_ == 0
        || audio_.isRecording())
        return;
    const auto trackIndex = *selectedTrack_;
    const auto clipId = selectedAudioClipId_;
    const auto clip = audio_.audioClip(trackIndex, clipId);
    if (!clip.has_value())
        return;
    chooser_ = std::make_unique<juce::FileChooser>(
        tr("dialog.addAudioTake"), clip->sourceFile.getParentDirectory(),
        "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser_->launchAsync(
        juce::FileBrowserComponent::openMode
            | juce::FileBrowserComponent::canSelectFiles,
        [this, trackIndex, clipId](const juce::FileChooser& chooser) {
            const auto file = chooser.getResult();
            if (!file.existsAsFile())
                return;
            const auto result = audio_.addAudioTake(
                trackIndex, clipId, file, true, false);
            if (result.failed()) {
                showError(tr("error.audioTakeTitle"),
                          result.getErrorMessage());
                return;
            }
            selectTrack(trackIndex);
            selectAudioClip(trackIndex, clipId);
            refreshTimelineRanges();
            transientStatus_ = tr("status.audioTakeAdded", "file",
                                  file.getFileName());
            transientStatusExpiryMs_ =
                juce::Time::currentTimeMillis() + 5'000;
            markProjectDirty();
        });
}

void WorkspaceComponent::recordSelectedAudioTake()
{
    if (!selectedTrack_ || selectedAudioClipId_ == 0
        || audio_.isRecording() || audio_.isMasterExporting())
        return;
    const auto clip = audio_.audioClip(*selectedTrack_, selectedAudioClipId_);
    if (!clip.has_value())
        return;
    audio_.setTrackArmed(*selectedTrack_, true);
    refreshAudioTrackArmStates();
    audio_.setPosition(clip->startSeconds);
    audioRecordingTargetClipId_ = clip->id;
    chooseRecordingFile();
}

void WorkspaceComponent::removeSelectedAudioTake()
{
    if (!selectedTrack_ || selectedAudioClipId_ == 0
        || audio_.isRecording())
        return;
    const auto clip = audio_.audioClip(*selectedTrack_, selectedAudioClipId_);
    if (!clip.has_value() || clip->takes.size() <= 1)
        return;
    if (!audio_.removeAudioTake(*selectedTrack_, clip->id,
                                clip->activeTakeIndex))
        return;
    refreshSelectedTrack();
    refreshTimelineRanges();
    transientStatus_ = tr("status.audioTakeRemoved");
    transientStatusExpiryMs_ = juce::Time::currentTimeMillis() + 5'000;
    markProjectDirty();
}

void WorkspaceComponent::refreshAudioTakeControls()
{
    const auto clip = selectedTrack_
        ? audio_.audioClip(*selectedTrack_, selectedAudioClipId_)
        : std::optional<AudioTrackController::AudioClip> {};
    audioTakeSelector_.clear(juce::dontSendNotification);
    if (!clip.has_value()) {
        audioTakeSelector_.setTextWhenNothingSelected(
            tr("audioTake.none"));
        audioTakeSelector_.setEnabled(false);
        addAudioTakeButton_.setEnabled(false);
        recordAudioTakeButton_.setEnabled(false);
        removeAudioTakeButton_.setEnabled(false);
        return;
    }
    for (std::size_t index = 0; index < clip->takes.size(); ++index) {
        auto name = clip->takes[index].name;
        if (name.isEmpty())
            name = tr("audioTake.defaultName", "count",
                      juce::String(static_cast<int>(index + 1)));
        audioTakeSelector_.addItem(
            juce::String(static_cast<int>(index + 1)) + "  " + name,
            static_cast<int>(index + 1));
    }
    audioTakeSelector_.setSelectedItemIndex(
        static_cast<int>(clip->activeTakeIndex),
        juce::dontSendNotification);
    const auto enabled = !audio_.isRecording();
    audioTakeSelector_.setEnabled(enabled && clip->takes.size() > 1);
    addAudioTakeButton_.setEnabled(
        enabled
        && clip->takes.size()
               < AudioTrackController::maximumAudioTakesPerClip);
    recordAudioTakeButton_.setEnabled(
        enabled
        && clip->takes.size()
               < AudioTrackController::maximumAudioTakesPerClip);
    removeAudioTakeButton_.setEnabled(enabled && clip->takes.size() > 1);
}

void WorkspaceComponent::chooseRecordingFile()
{
    auto recordingFolder = currentProjectFile_ != juce::File {}
                               ? currentProjectFile_.getParentDirectory()
                                     .getChildFile("Recordings")
                               : juce::File::getSpecialLocation(
                                     juce::File::userDocumentsDirectory)
                                     .getChildFile("CyDAW Recordings");
    const auto timestamp = juce::Time::getCurrentTime().formatted(
        "%Y%m%d-%H%M%S");
    const auto suggested = recordingFolder.getNonexistentChildFile(
        "Take-" + timestamp, ".wav", false);
    chooser_ = std::make_unique<juce::FileChooser>(
        tr("dialog.recordAudio"), suggested, "*.wav");
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode,
                          [this](const juce::FileChooser& chooser) {
                              auto file = chooser.getResult();
                              if (file == juce::File {}) {
                                  audioRecordingTargetClipId_.reset();
                                  refreshAudioTakeControls();
                                  return;
                              }
                              file = file.withFileExtension("wav");
                              if (file.exists())
                                  file = file.getNonexistentSibling(false);
                              toggleRecording(file);
                          });
}

void WorkspaceComponent::toggleRecording(const juce::File& destination)
{
    if (audio_.hasRecordingSession()) {
        finishAudioRecording(true);
        return;
    }

    if (destination == juce::File {})
        return;

    auto targetTrack = audio_.armedAudioTrack();
    if (!targetTrack.has_value() && selectedTrack_
        && *selectedTrack_ < audio_.trackCount()) {
        audio_.setTrackArmed(*selectedTrack_, true);
        targetTrack = selectedTrack_;
        refreshAudioTrackArmStates();
        markProjectDirty();
    }

    if (audioRecordingTargetClipId_.has_value()
        && (!targetTrack.has_value()
            || !audio_.audioClip(*targetTrack,
                                 *audioRecordingTargetClipId_).has_value())) {
        audioRecordingTargetClipId_.reset();
        showError(tr("error.audioTakeTitle"),
                  tr("error.audioTakeClip"));
        refreshAudioTakeControls();
        return;
    }
    if (audioRecordingTargetClipId_.has_value()) {
        const auto targetClip = audio_.audioClip(
            *targetTrack, *audioRecordingTargetClipId_);
        audio_.setPosition(targetClip->startSeconds);
    }

    const auto result = audio_.startRecording(destination);
    if (result.failed()) {
        audioRecordingTargetClipId_.reset();
        showError(tr("error.recordTitle"), result.getErrorMessage());
        refreshAudioTakeControls();
        return;
    }
    currentRecordingFile_ = destination;
    audioRecordingTargetTrack_ = targetTrack;
    audioRecordingStartSeconds_ = audio_.position();
    audioRecordingStartedPlayback_ = !audio_.isPlaying();
    audioRecordingPreviousLooping_ = loopButton_.getToggleState();
    if (audioRecordingPreviousLooping_) {
        loopButton_.setToggleState(false, juce::dontSendNotification);
        audio_.setLooping(false);
    }
    if (audioRecordingStartedPlayback_)
        audio_.play();
    playButton_.setEnabled(false);
    loopButton_.setEnabled(false);
    refreshAudioTrackArmStates();
    refreshSelectedTrack();
}

void WorkspaceComponent::toggleLauncherRecording(
    const std::size_t trackIndex, const int sceneIndex)
{
    if (trackIndex >= audio_.trackCount()
        || sceneIndex < 0
        || sceneIndex >= ClipLauncherComponent::sceneCount
        || audio_.isMasterExporting() || audio_.isMidiRecording())
        return;

    if (launcherRecordingTrack_.has_value()) {
        if (*launcherRecordingTrack_ != trackIndex
            || launcherRecordingScene_ != sceneIndex)
            return;
        if (!audio_.recordingStarted()) {
            finishLauncherRecording(false);
            return;
        }
        if (!audio_.recordingStopPending()) {
            audio_.scheduleRecordingStop(
                audio_.launcherBoundaryDelaySeconds());
            refreshClipLauncher();
        }
        return;
    }
    if (audio_.hasRecordingSession())
        return;

    const auto info = audio_.trackInfo(trackIndex);
    if (!info.armed)
        return;

    auto recordingFolder = currentProjectFile_ != juce::File {}
                               ? currentProjectFile_.getParentDirectory()
                                     .getChildFile("Recordings")
                               : juce::File::getSpecialLocation(
                                     juce::File::userDocumentsDirectory)
                                     .getChildFile("CyDAW Recordings");
    if (recordingFolder.createDirectory().failed()) {
        showError(tr("error.recordTitle"),
                  tr("error.recordFile", "file",
                     recordingFolder.getFullPathName()));
        return;
    }
    const auto timestamp = juce::Time::getCurrentTime().formatted(
        "%Y%m%d-%H%M%S");
    const auto destination = recordingFolder.getNonexistentChildFile(
        "Launcher-" + timestamp + "-S" + juce::String(sceneIndex + 1),
        ".wav", false);

    const auto startedPlayback = !audio_.isPlaying();
    if (startedPlayback)
        audio_.play();
    const auto delay = audio_.launcherBoundaryDelaySeconds();
    const auto result = audio_.startRecording(destination, delay);
    if (result.failed()) {
        if (startedPlayback)
            audio_.stop();
        showError(tr("error.recordTitle"), result.getErrorMessage());
        return;
    }

    currentRecordingFile_ = destination;
    launcherRecordingTrack_ = trackIndex;
    launcherRecordingScene_ = sceneIndex;
    launcherRecordingStartSeconds_ = audio_.position() + delay;
    playButton_.setEnabled(false);
    loopButton_.setEnabled(false);
    refreshAudioTrackArmStates();
    refreshClipLauncher();
}

void WorkspaceComponent::insertLauncherSample(
    const std::size_t trackIndex, const int sceneIndex,
    const juce::File& file)
{
    if (trackIndex >= audio_.trackCount()
        || sceneIndex < 0
        || sceneIndex >= ClipLauncherComponent::sceneCount
        || !isSupportedAudioFile(file) || !file.existsAsFile())
        return;

    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        audio_.formatManager().createReaderFor(file));
    if (reader == nullptr || reader->sampleRate <= 0.0
        || reader->lengthInSamples <= 0) {
        showError(tr("error.openAudioTitle"),
                  tr("error.openAudio", "file", file.getFullPathName()));
        return;
    }

    const auto duration = static_cast<double>(reader->lengthInSamples)
                          / reader->sampleRate;
    static constexpr std::array<std::uint32_t, 8> colours {
        0xffff8a00, 0xfff5c542, 0xff7ddc71, 0xff4fd3ae,
        0xff4ea5ff, 0xff8b7cf6, 0xffd46ee8, 0xffff6f7d
    };
    if (!audio_.setLauncherClip(
            trackIndex, sceneIndex, file.getFileNameWithoutExtension(),
            0.0, duration, true,
            colours[trackIndex % colours.size()], file)) {
        showError(tr("error.openAudioTitle"),
                  tr("error.openAudio", "file", file.getFullPathName()));
        return;
    }

    selectTrack(trackIndex);
    transientStatus_ = tr("status.launcherSampleInserted", "file",
                          file.getFileName());
    transientStatusExpiryMs_ = juce::Time::currentTimeMillis() + 5'000;
    refreshClipLauncher();
    markProjectDirty();
}

void WorkspaceComponent::finishLauncherRecording(const bool keepTake)
{
    if (!launcherRecordingTrack_.has_value()
        || !audio_.hasRecordingSession())
        return;

    const auto destination = currentRecordingFile_;
    const auto trackIndex = *launcherRecordingTrack_;
    const auto sceneIndex = launcherRecordingScene_;
    const auto droppedBlocks = audio_.droppedRecordingBlocks();
    audio_.stopRecording();
    currentRecordingFile_ = {};
    launcherRecordingTrack_.reset();
    launcherRecordingScene_ = -1;
    launcherRecordingStartSeconds_ = 0.0;
    playButton_.setEnabled(true);
    loopButton_.setEnabled(true);
    refreshAudioTrackArmStates();

    if (!keepTake) {
        if (destination.existsAsFile())
            (void) destination.deleteFile();
        transientStatus_ = tr("status.launcherRecordingCancelled");
        transientStatusExpiryMs_ = juce::Time::currentTimeMillis() + 5'000;
        refreshClipLauncher();
        return;
    }

    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        audio_.formatManager().createReaderFor(destination));
    if (reader == nullptr || reader->sampleRate <= 0.0
        || reader->lengthInSamples <= 0) {
        transientStatus_ = tr("status.recordingRetained", "file",
                              destination.getFullPathName());
        transientStatusExpiryMs_ = juce::Time::currentTimeMillis() + 10'000;
        showError(tr("error.recordTitle"),
                  tr("error.recordedTakeInvalid", "file",
                     destination.getFileName()));
        refreshClipLauncher();
        return;
    }
    const auto duration = static_cast<double>(reader->lengthInSamples)
                          / reader->sampleRate;
    static constexpr std::array<std::uint32_t, 8> colours {
        0xffff8a00, 0xfff5c542, 0xff7ddc71, 0xff4fd3ae,
        0xff4ea5ff, 0xff8b7cf6, 0xffd46ee8, 0xffff6f7d
    };
    const auto name = destination.getFileNameWithoutExtension();
    if (!audio_.setLauncherClip(
            trackIndex, sceneIndex, name, 0.0, duration, true,
            colours[trackIndex % colours.size()], destination)) {
        transientStatus_ = tr("status.recordingRetained", "file",
                              destination.getFullPathName());
        transientStatusExpiryMs_ = juce::Time::currentTimeMillis() + 10'000;
        showError(tr("error.recordTitle"),
                  tr("error.recordedTakeInvalid", "file",
                     destination.getFileName()));
        refreshClipLauncher();
        return;
    }

    (void) audio_.launchClip(trackIndex, sceneIndex);
    transientStatus_ = tr(
        droppedBlocks > 0 ? "status.launcherRecordedDropped"
                          : "status.launcherRecorded",
        "scene", juce::String(sceneIndex + 1));
    transientStatus_ = transientStatus_.replace(
        "{count}", juce::String(droppedBlocks));
    transientStatusExpiryMs_ = juce::Time::currentTimeMillis() + 8'000;
    refreshClipLauncher();
    markProjectDirty();
}

void WorkspaceComponent::finishAudioRecording(const bool keepTake)
{
    if (launcherRecordingTrack_.has_value()) {
        finishLauncherRecording(keepTake);
        return;
    }
    if (!audio_.isRecording())
        return;

    const auto destination = currentRecordingFile_;
    const auto droppedBlocks = audio_.droppedRecordingBlocks();
    audio_.stopRecording();
    if (audioRecordingStartedPlayback_)
        audio_.stop();
    loopButton_.setToggleState(audioRecordingPreviousLooping_,
                               juce::dontSendNotification);
    audio_.setLooping(audioRecordingPreviousLooping_);
    playButton_.setEnabled(true);
    loopButton_.setEnabled(true);

    currentRecordingFile_ = {};
    const auto targetTrack = audioRecordingTargetTrack_;
    const auto targetClipId = audioRecordingTargetClipId_;
    audioRecordingTargetTrack_.reset();
    audioRecordingTargetClipId_.reset();
    audioRecordingStartedPlayback_ = false;
    audioRecordingPreviousLooping_ = false;
    refreshAudioTrackArmStates();

    if (!keepTake) {
        if (destination.existsAsFile())
            (void) destination.deleteFile();
        transientStatus_ = tr("status.recordingCancelled");
        transientStatusExpiryMs_ = juce::Time::currentTimeMillis() + 5'000;
        refreshSelectedTrack();
        return;
    }

    AudioTrackController::RecordedTakeResult take;
    const auto commit = audio_.commitRecordedTake(
        destination, targetTrack, audioRecordingStartSeconds_, take,
        targetClipId);
    if (commit.failed()) {
        transientStatus_ = tr("status.recordingRetained", "file",
                              destination.getFullPathName());
        transientStatusExpiryMs_ = juce::Time::currentTimeMillis() + 10'000;
        showError(tr("error.recordTitle"), commit.getErrorMessage());
        refreshSelectedTrack();
        return;
    }

    if (take.createdTrack)
        addTrackLane(take.trackIndex);
    selectTrack(take.trackIndex);
    selectAudioClip(take.trackIndex, take.clipId);
    refreshAudioTrackArmStates();
    refreshTimelineRanges();
    refreshAutomationEditor();
    refreshAudioEditor();
    refreshClipLauncher();
    transientStatus_ = tr(
        targetClipId.has_value()
            ? (droppedBlocks > 0
                   ? "status.recordedAlternativeTakeDropped"
                   : "status.recordedAlternativeTake")
            : (droppedBlocks > 0 ? "status.recordedTakeDropped"
                                 : "status.recordedTake"),
        "file", destination.getFileName());
    transientStatus_ = transientStatus_.replace(
        "{time}", formatTime(take.startSeconds));
    transientStatus_ = transientStatus_.replace(
        "{count}", juce::String(droppedBlocks));
    transientStatusExpiryMs_ = juce::Time::currentTimeMillis() + 8'000;
    markProjectDirty();
}

void WorkspaceComponent::refreshAudioTrackArmStates()
{
    for (std::size_t index = 0; index < trackLanes_.size()
                                && index < audio_.trackCount(); ++index) {
        const auto info = audio_.trackInfo(index);
        trackLanes_[index]->setMixerState(
            info.muted, info.solo, info.armed);
        trackLanes_[index]->setArmEnabled(!audio_.isRecording());
    }
    if (selectedTrack_ && *selectedTrack_ < audio_.trackCount()) {
        armButton_.setToggleState(
            audio_.trackInfo(*selectedTrack_).armed,
            juce::dontSendNotification);
    } else {
        armButton_.setToggleState(false, juce::dontSendNotification);
    }
    armButton_.setEnabled(selectedTrack_.has_value()
                          && !audio_.isRecording());
    refreshMixer();
}

void WorkspaceComponent::toggleMidiRecording()
{
    if (audio_.isMidiRecording()) {
        audio_.stopMidiRecording(true);
        refreshMidiEditor();
        markProjectDirty();
        return;
    }
    if (selectedMidiTrackId_ == 0 || selectedMidiClipId_ == 0)
        refreshMidiEditor();
    const auto result = audio_.startMidiRecording(
        selectedMidiTrackId_, selectedMidiClipId_,
        pianoRoll_.recordOverdubEnabled(), pianoRoll_.recordQuantization());
    if (result.failed()) {
        showError(tr("error.midiRecordTitle"), result.getErrorMessage());
        return;
    }
    if (!audio_.isPlaying())
        audio_.play();
    refreshMidiEditor();
}

void WorkspaceComponent::undoMidiEdit()
{
    if (!audio_.undoMidiEdit())
        return;
    refreshMidiEditor();
    refreshTimelineRanges();
    markProjectDirty();
}

void WorkspaceComponent::redoMidiEdit()
{
    if (!audio_.redoMidiEdit())
        return;
    refreshMidiEditor();
    refreshTimelineRanges();
    markProjectDirty();
}

void WorkspaceComponent::chooseMasterExportFile()
{
    auto name = currentProjectFile_ == juce::File {}
                    ? juce::String("CyDAW Mix")
                    : currentProjectFile_.getFileNameWithoutExtension() + " Mix";
    auto suggested = juce::File::getSpecialLocation(
                         juce::File::userDocumentsDirectory)
                         .getChildFile(name + ".wav");
    chooser_ = std::make_unique<juce::FileChooser>(
        tr("dialog.exportMix"), suggested, "*.wav");
    chooser_->launchAsync(
        juce::FileBrowserComponent::saveMode
            | juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& chooser) {
            auto file = chooser.getResult();
            if (file != juce::File {})
                startMasterExport(file.withFileExtension("wav"));
        });
}

void WorkspaceComponent::startMasterExport(const juce::File& destination)
{
    const auto result = audio_.startMasterExport(destination);
    if (result.failed()) {
        showError(tr("error.masterExportTitle"), result.getErrorMessage());
        return;
    }
    currentMasterExportFile_ = destination;
    masterExportStatus_.clear();
    masterExportButton_.setButtonText(tr("toolbar.cancelExport"));
    masterExportButton_.setTooltip(tr("masterExport.cancelHelp"));
    playButton_.setEnabled(false);
    recordButton_.setEnabled(false);
    loopButton_.setEnabled(false);
}

void WorkspaceComponent::cancelMasterExport()
{
    audio_.cancelMasterExport();
    currentMasterExportFile_ = {};
    masterExportStatus_ = tr("status.masterExportCancelled");
    masterExportButton_.setButtonText(tr("toolbar.exportMix"));
    masterExportButton_.setTooltip(tr("masterExport.help"));
    playButton_.setEnabled(true);
    recordButton_.setEnabled(true);
    loopButton_.setEnabled(true);
    refreshClipLauncher();
}

void WorkspaceComponent::showAudioSettings()
{
    if (audio_.isRecording())
        finishAudioRecording(true);
    juce::DialogWindow::LaunchOptions options;
    auto* selector = new juce::AudioDeviceSelectorComponent(audio_.deviceManager(), 0, 32, 1, 32,
                                                             true, true, true, false);
    selector->setSize(680, 560);
    options.content.setOwned(selector);
    options.dialogTitle = tr("dialog.audioSettings");
    options.dialogBackgroundColour = juce::Colour(0xff171a21);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.componentToCentreAround = this;
    options.launchAsync();
}

void WorkspaceComponent::saveAudioDevicePreferences()
{
    auto* settings = layoutProperties_.getUserSettings();
    if (settings == nullptr)
        return;
    if (auto state = audio_.deviceManager().createStateXml()) {
        settings->setValue("audioDeviceState", state->toString());
        settings->saveIfNeeded();
    }
}

void WorkspaceComponent::chooseProjectSaveFile()
{
    auto suggested = currentProjectFile_;
    if (suggested == juce::File {})
        suggested = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Untitled.cydaw");
    chooser_ = std::make_unique<juce::FileChooser>(tr("dialog.saveProject"), suggested, "*.cydaw");
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this](const juce::FileChooser& chooser) {
                              auto file = chooser.getResult();
                              if (file != juce::File {})
                                  saveProject(file.withFileExtension("cydaw"));
                          });
}

void WorkspaceComponent::chooseProjectLoadFile()
{
    chooser_ = std::make_unique<juce::FileChooser>(tr("dialog.openProject"), juce::File {}, "*.cydaw");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this](const juce::FileChooser& chooser) {
                              const auto file = chooser.getResult();
                              if (file.existsAsFile())
                                  loadProject(file);
                          });
}

void WorkspaceComponent::frameGraphNode(const juce::String& nodeTypeKey)
{
    nodeGraph_.frameNode(nodeTypeKey);
}

class WorkspaceComponent::UiHitchProbe final : public juce::Timer {
public:
    UiHitchProbe(const double seconds,
                 std::function<void(juce::String)> onReport)
        : durationMs_(seconds * 1000.0), onReport_(std::move(onReport))
    {
        start_ = last_ = juce::Time::getMillisecondCounterHiRes();
        gaps_.reserve(static_cast<std::size_t>(seconds * 300.0) + 16);
        // Three missed beats of this timer, and the sampler looks.
        sampler_.start(12.0);
        startTimer(4);
    }

    void note(const juce::String& label)
    {
        notes_.push_back({ juce::Time::getMillisecondCounterHiRes() - start_,
                           label });
    }

    // Every paint of the run, however short: thirty small ones a second
    // add up to a share of the thread no single note shows.
    void addPaint(const paint_profile::Counters& counters, const double ms)
    {
        paintTotals_ += counters;
        paintMs_ += ms;
        ++paintCount_;
    }

    void timerCallback() override
    {
        sampler_.beat();
        const auto now = juce::Time::getMillisecondCounterHiRes();
        gaps_.push_back({ now - start_, now - last_ });
        last_ = now;
        if (now - start_ < durationMs_)
            return;
        stopTimer();
        const auto stacks = sampler_.stop();
        // The report's receiver may drop this probe: nothing of it is
        // touched after the call.
        const auto report = onReport_;
        report(describe() + " " + stacks);
    }

private:
    struct Gap {
        double atMs {};
        double ms {};
    };
    struct Note {
        double atMs {};
        juce::String label;
    };

    [[nodiscard]] juce::String describe() const
    {
        if (gaps_.empty())
            return "no samples";
        auto sorted = gaps_;
        std::sort(sorted.begin(), sorted.end(),
                  [](const Gap& a, const Gap& b) { return a.ms < b.ms; });
        const auto percentile = [&sorted](const double fraction) {
            const auto index = static_cast<std::size_t>(
                std::min<double>(static_cast<double>(sorted.size() - 1),
                                 fraction * static_cast<double>(sorted.size())));
            return sorted[index].ms;
        };
        const auto over = [&sorted](const double threshold) {
            return static_cast<int>(std::count_if(
                sorted.begin(), sorted.end(),
                [threshold](const Gap& gap) { return gap.ms > threshold; }));
        };
        juce::String text;
        text << "samples=" << static_cast<int>(gaps_.size())
             << " p50=" << juce::String(percentile(0.5), 1)
             << " p95=" << juce::String(percentile(0.95), 1)
             << " p99=" << juce::String(percentile(0.99), 1)
             << " max=" << juce::String(sorted.back().ms, 1)
             << " over20=" << over(20.0) << " over50=" << over(50.0)
             << " over100=" << over(100.0) << " worst[";
        for (std::size_t index = 0; index < std::min<std::size_t>(5, sorted.size());
             ++index) {
            const auto& gap = sorted[sorted.size() - 1 - index];
            text << (index == 0 ? "" : " ") << juce::String(gap.ms, 1) << "@"
                 << juce::String(gap.atMs / 1000.0, 2) << "s";
        }
        text << "] notes[";
        for (std::size_t index = 0; index < notes_.size(); ++index)
            text << (index == 0 ? "" : " ") << notes_[index].label << "@"
                 << juce::String(notes_[index].atMs / 1000.0, 2) << "s";
        const auto wallMs = std::max(
            1.0, juce::Time::getMillisecondCounterHiRes() - start_);
        text << "] paints[n=" << paintCount_ << " ms=" << juce::String(paintMs_, 0)
             << " (" << juce::String(100.0 * paintMs_ / wallMs, 1)
             << "% of " << juce::String(wallMs / 1000.0, 1) << "s) "
             << paint_profile::describe(paintTotals_) << "] (ms)";
        return text;
    }

    double durationMs_ {};
    double start_ {};
    double last_ {};
    std::vector<Gap> gaps_;
    std::vector<Note> notes_;
    std::function<void(juce::String)> onReport_;
    MessageThreadSampler sampler_;
    paint_profile::Counters paintTotals_ {};
    double paintMs_ {};
    int paintCount_ {};
};

// Times every paint of the workspace's subtree, for the probe. The
// paint()/paintOverChildren pair it replaces missed any paint whose region
// lay wholly inside an opaque child — the launcher, the audio editor —
// since JUCE then skips the parent's paint, and the probe saw a gap with
// no note. A cached-image hook is called for every paint, without
// exception, and paints straight through.
class WorkspaceComponent::UiHitchPaintBracket final
    : public juce::CachedComponentImage {
public:
    explicit UiHitchPaintBracket(WorkspaceComponent& owner) : owner_(owner) {}

    void paint(juce::Graphics& graphics) override
    {
        const auto start = juce::Time::getMillisecondCounterHiRes();
        const auto dirty = graphics.getClipBounds();
        owner_.paintEntireComponent(graphics, false);
        const auto paintMs = juce::Time::getMillisecondCounterHiRes() - start;
        if (owner_.uiHitchProbe_ != nullptr)
            owner_.uiHitchProbe_->addPaint(paint_profile::counters(), paintMs);
        // With the region that was dirty: a slow paint of a strip and a
        // slow paint of the whole window are different problems.
        if (paintMs > 6.0)
            owner_.noteUiHitchEvent("paint=" + juce::String(paintMs, 1) + "["
                                    + paint_profile::describeAndReset()
                                    + " clipBox=" + juce::String(dirty.getWidth())
                                    + "x" + juce::String(dirty.getHeight()) + "@"
                                    + juce::String(dirty.getX()) + ","
                                    + juce::String(dirty.getY()) + "]");
        else
            paint_profile::counters() = paint_profile::Counters {};
    }

    bool invalidateAll() override { return true; }
    bool invalidate(const juce::Rectangle<int>&) override { return true; }
    void releaseResources() override {}

private:
    WorkspaceComponent& owner_;
};

void WorkspaceComponent::startUiHitchProbe(
    const double seconds, std::function<void(juce::String)> onReport)
{
    // Silent: the engine runs, the room does not hear it.
    silenceOutput();
    // The autosave two seconds in, so the window holds the one periodic
    // job the message thread runs on its own. Recovery is off in a test
    // run, so it is switched on here, into a file of the probe's own.
    recoveryEnabled_ = true;
    projectRecoveryFile_ = juce::File::getSpecialLocation(
                               juce::File::tempDirectory)
                               .getChildFile("cydaw-hitch-probe.cydaw-recovery");
    projectDirty_ = true;
    nextProjectAutosaveMs_ = juce::Time::currentTimeMillis() + 2'000;
    juce::Component::SafePointer<WorkspaceComponent> safeThis(this);
    uiHitchProbe_ = std::make_unique<UiHitchProbe>(
        seconds, [safeThis, onReport = std::move(onReport)](juce::String report) {
            if (safeThis != nullptr) {
                safeThis->setCachedComponentImage(nullptr);
                safeThis->uiHitchProbe_.reset();
            }
            onReport(std::move(report));
        });
    setCachedComponentImage(new UiHitchPaintBracket(*this));
}

void WorkspaceComponent::noteUiHitchEvent(const juce::String& label)
{
    if (uiHitchProbe_ != nullptr)
        uiHitchProbe_->note(label);
}

void WorkspaceComponent::performProbeAction(const juce::String& action)
{
    noteUiHitchEvent("action=" + action);
    const auto anchor = audio_.position();
    if (action == "zoom-in")
        zoomArrangement(1.5, anchor);
    else if (action == "zoom-out")
        zoomArrangement(1.0 / 1.5, anchor);
    else if (action == "pan")
        panArrangement(2.0);
    else if (action == "pan-back")
        panArrangement(-2.0);
    else if (action == "fit")
        fitArrangement();
    // The clip editor's keys: select every note, then nudge the selection a
    // quarter beat — the path one step of a note drag takes.
    else if (action == "graph-insert") {
        // A card spliced before the output of the graph shown, with what
        // the edit's publication cost, as the bench reports it.
        (void) nodeGraph_.insertNodeBeforeOutput("gain");
        noteUiHitchEvent("inserted[" + nodeGraph_.layoutChangeProfile()
                         + " || export=" + nodeGraph_.exportCacheProfile() + "]");
    } else if (action == "roll-last-clip") {
        // The clip editor opens on a track's first clip; the one the probe
        // wants is the last.
        const auto tracks = audio_.midiTracks();
        const auto track = std::find_if(
            tracks.begin(), tracks.end(), [this](const auto& candidate) {
                return candidate.id == selectedMidiTrackId_;
            });
        if (track != tracks.end() && !track->clips.empty()) {
            selectedMidiClipId_ = track->clips.back().id;
            refreshMidiEditor();
        }
    } else if (action == "roll-select-all")
        noteUiHitchEvent(juce::String("handled=")
                         + (pianoRoll_.keyPressed(juce::KeyPress(
                                'A', juce::ModifierKeys::commandModifier, 'A'))
                                ? "yes" : "no"));
    else if (action == "roll-nudge" || action == "roll-nudge-back")
        noteUiHitchEvent(juce::String("handled=")
                         + (pianoRoll_.keyPressed(juce::KeyPress(
                                action == "roll-nudge" ? juce::KeyPress::rightKey
                                                       : juce::KeyPress::leftKey))
                                ? "yes" : "no"));
}

void WorkspaceComponent::silenceOutput()
{
    audio_.silenceMasterOutput();
}

void WorkspaceComponent::startPlayback()
{
    if (!audio_.isMasterExporting() && !audio_.isPlaying())
        playButton_.triggerClick();
}

void WorkspaceComponent::openProjectFile(const juce::File& file)
{
    if (file.existsAsFile()
        && file.hasFileExtension("cydaw"))
        loadProject(file);
}

void WorkspaceComponent::saveProject(const juce::File& file)
{
    if (audio_.isRecording())
        finishAudioRecording(true);
    if (audio_.isMidiRecording()) {
        audio_.stopMidiRecording(true);
        refreshMidiEditor();
    }
    const auto document = createProjectDocument(file, true, false);
    const auto result = ProjectRecovery::writeDocumentAtomically(file,
                                                                  document);
    if (result.failed()) {
        showError(tr("error.saveProjectTitle"),
                  tr("error.saveProject") + "\n" + result.getErrorMessage());
        return;
    }
    (void) projectAutosavePool_.removeAllJobs(true, 10'000);
    projectAutosaveInProgress_.store(false, std::memory_order_release);
    currentProjectFile_ = file;
    projectDirty_ = false;
    discardProjectRecovery();
    projectLabel_.setText("CyDAW  /  " + file.getFileNameWithoutExtension(),
                          juce::dontSendNotification);
}

juce::var WorkspaceComponent::createProjectDocument(
    const juce::File& pathBase, const bool refreshPluginState,
    const bool absoluteAssetPaths, DeferredDocumentWork* const deferred)
{
    const auto automationProperty
        = [deferred](const juce::DynamicObject::Ptr& owner,
                     const juce::Identifier& property,
                     std::vector<AudioTrackController::AutomationPoint> points) {
              // The property is placed now, empty, so the writer's fill
              // lands where a save would have put it: same document, same
              // order, same bytes.
              if (deferred != nullptr) {
                  owner->setProperty(property, juce::var());
                  deferred->automation.push_back({ owner, property,
                                                   std::move(points) });
              } else {
                  owner->setProperty(property, automationPointsToVar(points));
              }
          };
    const auto notesProperty
        = [deferred](const juce::DynamicObject::Ptr& owner,
                     const juce::Identifier& property,
                     std::vector<AudioTrackController::MidiNote> notes) {
              if (deferred != nullptr) {
                  owner->setProperty(property, juce::var());
                  deferred->notes.push_back({ owner, property,
                                              std::move(notes) });
              } else {
                  owner->setProperty(property, midiNotesToVar(notes));
              }
          };
    auto profileMark = juce::Time::getHighResolutionTicks();
    const auto profileSince = [&profileMark] {
        const auto now = juce::Time::getHighResolutionTicks();
        const auto ms = juce::Time::highResolutionTicksToSeconds(
                            now - profileMark) * 1000.0;
        profileMark = now;
        return ms;
    };
    auto profileClips = 0.0;
    auto profileGraphs = 0.0;
    auto profileModulation = 0.0;
    auto profilePlugins = 0.0;
    auto profileAutomation = 0.0;
    auto profileLauncher = 0.0;
    saveCurrentNodeGraphState();
    saveCurrentModulationState();
    const auto profileSave = profileSince();
    auto state = juce::DynamicObject::Ptr(new juce::DynamicObject());
    state->setProperty(jsonKeys::format, "CyDAW Project");
    state->setProperty(jsonKeys::version, 36);

    // One deep copy of the class library for the whole document. Made once
    // per graph below, it was the bulk of a nine-hundred-millisecond pause on
    // the message thread every time the autosave fired.
    const auto sharedLibrary = sharedNodeModulesCopy();
    const auto sharedModulationLibrary = sharedModulationModulesCopy();
    const auto profileLibrary = profileSince();

    juce::Array<juce::var> virtualBuses;
    for (const auto& bus : audio_.virtualBuses()) {
        ensureVirtualBusNodeGraphState(bus.id);
        auto value = juce::DynamicObject::Ptr(new juce::DynamicObject());
        value->setProperty(jsonKeys::id, static_cast<juce::int64>(bus.id));
        value->setProperty(jsonKeys::name, bus.name);
        value->setProperty(jsonKeys::direction, static_cast<int>(bus.direction));
        value->setProperty(jsonKeys::channels, bus.channels);
        value->setProperty(jsonKeys::physicalA, bus.physicalChannelA);
        value->setProperty(jsonKeys::physicalB, bus.physicalChannelB);
        value->setProperty(jsonKeys::destinationA, bus.destinationChannelA);
        value->setProperty(jsonKeys::destinationB, bus.destinationChannelB);
        value->setProperty(jsonKeys::gainDb, bus.gainDb);
        value->setProperty(jsonKeys::muted, bus.muted);
        value->setProperty(jsonKeys::monitor, bus.monitor);
        value->setProperty(
            "nodeGraph",
            stateWithoutSharedModules(virtualBusNodeGraphStates_[bus.id]));
        value->setProperty(jsonKeys::exposeToSystem, bus.exposeToSystem);
        virtualBuses.add(juce::var(value.get()));
    }
    state->setProperty(jsonKeys::virtualBuses, virtualBuses);
    const auto profileBuses = profileSince();

    juce::Array<juce::var> tracks;
    // The selected track's merged graph, kept for the compatibility field
    // at the end: it was merged — and the state cloned — a second time.
    juce::var selectedTrackGraph;
    for (std::size_t index = 0; index < audio_.trackCount(); ++index) {
        ensureTrackNodeGraphState(index);
        ensureTrackModulationState(index);
        const auto info = audio_.trackInfo(index);
        (void) profileSince();
        auto track = juce::DynamicObject::Ptr(new juce::DynamicObject());
        track->setProperty(jsonKeys::audioFile, projectAssetPath(
            info.file, pathBase, absoluteAssetPaths));
        const auto presentation = index < trackPresentations_.size()
                                      ? trackPresentations_[index]
                                      : TrackPresentation {
                                            tr("launcher.audioTrack", "index",
                                               juce::String(static_cast<int>(index + 1))),
                                            defaultTrackColour(index) };
        track->setProperty(jsonKeys::name, presentation.name);
        track->setProperty(jsonKeys::colour, static_cast<juce::int64>(
                                         presentation.colour.getARGB()));
        track->setProperty(jsonKeys::audioChannels, presentation.audioChannels);
        track->setProperty(jsonKeys::gainDb, info.gainDb);
        track->setProperty(jsonKeys::pan, info.pan);
        track->setProperty(jsonKeys::mute, info.muted);
        track->setProperty(jsonKeys::solo, info.solo);
        track->setProperty(jsonKeys::armed, info.armed);
        track->setProperty(jsonKeys::inputBusId,
                           static_cast<juce::int64>(info.inputBusId));
        track->setProperty(
            "laneHeight",
            index < trackLaneHeights_.size()
                ? trackLaneHeights_[index] : 88);
        track->setProperty(
            "automationVisible",
            index < trackAutomationVisible_.size()
                && trackAutomationVisible_[index] != 0);
        track->setProperty(jsonKeys::clipStart, info.clipStartSeconds);
        track->setProperty(jsonKeys::sourceOffset, info.sourceOffsetSeconds);
        track->setProperty(jsonKeys::clipDuration, info.clipDurationSeconds);
        juce::Array<juce::var> audioClips;
        for (const auto& clip : info.clips) {
            auto value = juce::DynamicObject::Ptr(new juce::DynamicObject());
            value->setProperty(jsonKeys::name, clip.name);
            value->setProperty(jsonKeys::start, clip.startSeconds);
            value->setProperty(jsonKeys::offset, clip.sourceOffsetSeconds);
            value->setProperty(jsonKeys::duration, clip.durationSeconds);
            value->setProperty(jsonKeys::playStart, clip.playStartSeconds);
            value->setProperty(jsonKeys::playEnd, clip.playEndSeconds);
            value->setProperty(jsonKeys::loopStart, clip.loopStartSeconds);
            value->setProperty(jsonKeys::loopEnd, clip.loopEndSeconds);
            value->setProperty(jsonKeys::loop, clip.looping);
            value->setProperty(jsonKeys::eventTimeline, clip.usesEventTimeline);
            value->setProperty(jsonKeys::sourceDuration,
                               clip.sourceDurationSeconds);
            value->setProperty(jsonKeys::sourceTempo, clip.sourceTempoBpm);
            value->setProperty(jsonKeys::stretchMode,
                               static_cast<int>(clip.stretchMode));
            juce::Array<juce::var> stretchMarkers;
            for (const auto& marker : clip.stretchMarkers) {
                auto markerValue = juce::DynamicObject::Ptr(
                    new juce::DynamicObject());
                markerValue->setProperty(jsonKeys::source, marker.sourceSeconds);
                markerValue->setProperty(jsonKeys::timeline,
                                         marker.timelineSeconds);
                stretchMarkers.add(juce::var(markerValue.get()));
            }
            value->setProperty(jsonKeys::stretchMarkers, stretchMarkers);
            juce::Array<juce::var> clipEvents;
            for (const auto& event : clip.events) {
                auto eventValue = juce::DynamicObject::Ptr(
                    new juce::DynamicObject());
                eventValue->setProperty(
                    "id", static_cast<juce::int64>(event.id));
                eventValue->setProperty(jsonKeys::name, event.name);
                eventValue->setProperty(jsonKeys::start, event.startSeconds);
                eventValue->setProperty(jsonKeys::offset,
                                        event.sourceOffsetSeconds);
                eventValue->setProperty(jsonKeys::duration,
                                        event.durationSeconds);
                eventValue->setProperty(jsonKeys::sourceDuration,
                                        event.sourceDurationSeconds);
                eventValue->setProperty(
                    "colour", static_cast<juce::int64>(event.colour));
                eventValue->setProperty(
                    "source", projectAssetPath(
                        event.sourceFile, pathBase, absoluteAssetPaths));
                eventValue->setProperty(jsonKeys::fadeIn, event.fadeInSeconds);
                eventValue->setProperty(jsonKeys::fadeOut, event.fadeOutSeconds);
                eventValue->setProperty(jsonKeys::sourceTempo,
                                        event.sourceTempoBpm);
                eventValue->setProperty(jsonKeys::gainDb, event.gainDb);
                eventValue->setProperty(
                    "stretchMode", static_cast<int>(event.stretchMode));
                juce::Array<juce::var> eventMarkers;
                for (const auto& marker : event.stretchMarkers) {
                    auto markerValue = juce::DynamicObject::Ptr(
                        new juce::DynamicObject());
                    markerValue->setProperty(
                        "id", static_cast<juce::int64>(marker.id));
                    markerValue->setProperty(jsonKeys::source,
                                             marker.sourceSeconds);
                    markerValue->setProperty(jsonKeys::timeline,
                                             marker.timelineSeconds);
                    eventMarkers.add(juce::var(markerValue.get()));
                }
                eventValue->setProperty(jsonKeys::stretchMarkers, eventMarkers);
                clipEvents.add(juce::var(eventValue.get()));
            }
            value->setProperty(jsonKeys::events, clipEvents);
            automationProperty(value, document_ids::gainAutomation,
                               clip.automation);
            value->setProperty(jsonKeys::fadeIn, clip.fadeInSeconds);
            value->setProperty(jsonKeys::fadeOut, clip.fadeOutSeconds);
            value->setProperty(jsonKeys::colour, static_cast<juce::int64>(clip.colour));
            value->setProperty(
                "source",
                projectAssetPath(clip.sourceFile, pathBase,
                                 absoluteAssetPaths));
            value->setProperty(
                "activeTake", static_cast<juce::int64>(clip.activeTakeIndex));
            juce::Array<juce::var> takes;
            for (const auto& take : clip.takes) {
                auto takeValue = juce::DynamicObject::Ptr(
                    new juce::DynamicObject());
                takeValue->setProperty(jsonKeys::name, take.name);
                takeValue->setProperty(
                    "source",
                    projectAssetPath(take.sourceFile, pathBase,
                                     absoluteAssetPaths));
                takes.add(juce::var(takeValue.get()));
            }
            value->setProperty(jsonKeys::takes, takes);
            audioClips.add(juce::var(value.get()));
        }
        track->setProperty(jsonKeys::audioClips, audioClips);
        profileClips += profileSince();
        track->setProperty(jsonKeys::driveDb, info.driveDb);
        track->setProperty(jsonKeys::filterCutoffHz, info.filterCutoffHz);
        // Only the graph's own inline classes; the library is written once,
        // at the top level. The compatibility field at the end still carries
        // the selected track's graph merged, so one merge, not one a track.
        track->setProperty(jsonKeys::nodeGraph, stateWithoutSharedModules(
                                             trackNodeGraphStates_[index]));
        if (selectedTrack_ && *selectedTrack_ == index)
            selectedTrackGraph = graphStateWithSharedModules(
                trackNodeGraphStates_[index], sharedLibrary);
        profileGraphs += profileSince();
        track->setProperty(
            "modulationGraph",
            stateWithoutSharedModules(trackModulationStates_[index].graph));
        juce::Array<juce::var> modulationAssignments;
        for (const auto& assignment
             : trackModulationStates_[index].assignments) {
            auto value = juce::DynamicObject::Ptr(new juce::DynamicObject());
            value->setProperty(jsonKeys::binding, assignment.binding);
            value->setProperty(jsonKeys::label, assignment.label);
            value->setProperty(jsonKeys::base, assignment.baseValue);
            value->setProperty(jsonKeys::minimum, assignment.minimum);
            value->setProperty(jsonKeys::maximum, assignment.maximum);
            value->setProperty(jsonKeys::amount, assignment.amount);
            value->setProperty(jsonKeys::enabled, assignment.enabled);
            modulationAssignments.add(juce::var(value.get()));
        }
        track->setProperty(jsonKeys::modulationAssignments, modulationAssignments);
        profileModulation += profileSince();
        const auto pluginAutomation = audio_.pluginAutomationLanes(index);
        juce::Array<juce::var> plugins;
        for (std::size_t pluginIndex = 0; pluginIndex < info.plugins.size(); ++pluginIndex) {
            const auto& plugin = info.plugins[pluginIndex];
            auto value = juce::DynamicObject::Ptr(new juce::DynamicObject());
            value->setProperty(jsonKeys::file, projectAssetPath(
                plugin.file, pathBase, absoluteAssetPaths));
            juce::MemoryBlock pluginState;
            const auto pluginStateResult = refreshPluginState
                ? audio_.getTrackPluginState(index, pluginIndex, pluginState)
                : audio_.getCachedTrackPluginState(index, pluginIndex,
                                                   pluginState);
            if (pluginStateResult.wasOk())
                value->setProperty(jsonKeys::state, pluginState.toBase64Encoding());
            juce::Array<juce::var> automationLanes;
            for (const auto& lane : pluginAutomation) {
                if (lane.pluginIndex != pluginIndex)
                    continue;
                auto laneValue = juce::DynamicObject::Ptr(new juce::DynamicObject());
                laneValue->setProperty(jsonKeys::parameter, static_cast<juce::int64>(lane.parameterIndex));
                laneValue->setProperty(jsonKeys::name, lane.parameterName);
                juce::Array<juce::var> points;
                points.ensureStorageAllocated(
                    static_cast<int>(lane.points.size()));
                for (const auto& point : lane.points) {
                    auto pointValue = juce::DynamicObject::Ptr(new juce::DynamicObject());
                    pointValue->setProperty(document_ids::time, point.timeSeconds);
                    pointValue->setProperty(document_ids::value,
                                            point.normalisedValue);
                    pointValue->setProperty(document_ids::curve,
                                            static_cast<int>(point.curve));
                    pointValue->setProperty(document_ids::curveAmount,
                                            point.curveAmount);
                    points.add(juce::var(pointValue.get()));
                }
                laneValue->setProperty(jsonKeys::points, points);
                automationLanes.add(juce::var(laneValue.get()));
            }
            value->setProperty(jsonKeys::automation, automationLanes);
            plugins.add(juce::var(value.get()));
        }
        track->setProperty(jsonKeys::plugins, plugins);
        profilePlugins += profileSince();
        automationProperty(track, document_ids::gainAutomation,
                           audio_.automationPoints(index));
        profileAutomation += profileSince();
        juce::Array<juce::var> launcherClips;
        for (const auto& clip : audio_.launcherClips(index)) {
            auto value = juce::DynamicObject::Ptr(new juce::DynamicObject());
            value->setProperty(jsonKeys::scene, clip.sceneIndex);
            value->setProperty(jsonKeys::name, clip.name);
            value->setProperty(
                "source",
                projectAssetPath(clip.sourceFile, pathBase,
                                 absoluteAssetPaths));
            value->setProperty(jsonKeys::offset, clip.sourceOffsetSeconds);
            value->setProperty(jsonKeys::duration, clip.durationSeconds);
            value->setProperty(jsonKeys::loop, clip.looping);
            value->setProperty(jsonKeys::colour, static_cast<juce::int64>(clip.colour));
            value->setProperty(jsonKeys::playStart, clip.playStartSeconds);
            value->setProperty(jsonKeys::playEnd, clip.playEndSeconds);
            value->setProperty(jsonKeys::loopStart, clip.loopStartSeconds);
            value->setProperty(jsonKeys::loopEnd, clip.loopEndSeconds);
            value->setProperty(jsonKeys::eventTimeline, clip.usesEventTimeline);
            juce::Array<juce::var> launcherEvents;
            for (const auto& event : clip.events) {
                auto eventValue = juce::DynamicObject::Ptr(
                    new juce::DynamicObject());
                eventValue->setProperty(
                    "id", static_cast<juce::int64>(event.id));
                eventValue->setProperty(jsonKeys::name, event.name);
                eventValue->setProperty(jsonKeys::start, event.startSeconds);
                eventValue->setProperty(jsonKeys::offset,
                                        event.sourceOffsetSeconds);
                eventValue->setProperty(jsonKeys::duration,
                                        event.durationSeconds);
                eventValue->setProperty(jsonKeys::sourceDuration,
                                        event.sourceDurationSeconds);
                eventValue->setProperty(
                    "colour", static_cast<juce::int64>(event.colour));
                eventValue->setProperty(
                    "source", projectAssetPath(
                        event.sourceFile, pathBase, absoluteAssetPaths));
                eventValue->setProperty(jsonKeys::fadeIn, event.fadeInSeconds);
                eventValue->setProperty(jsonKeys::fadeOut, event.fadeOutSeconds);
                eventValue->setProperty(jsonKeys::sourceTempo,
                                        event.sourceTempoBpm);
                eventValue->setProperty(jsonKeys::gainDb, event.gainDb);
                eventValue->setProperty(
                    "stretchMode", static_cast<int>(event.stretchMode));
                juce::Array<juce::var> eventMarkers;
                for (const auto& marker : event.stretchMarkers) {
                    auto markerValue = juce::DynamicObject::Ptr(
                        new juce::DynamicObject());
                    markerValue->setProperty(
                        "id", static_cast<juce::int64>(marker.id));
                    markerValue->setProperty(jsonKeys::source,
                                             marker.sourceSeconds);
                    markerValue->setProperty(jsonKeys::timeline,
                                             marker.timelineSeconds);
                    eventMarkers.add(juce::var(markerValue.get()));
                }
                eventValue->setProperty(jsonKeys::stretchMarkers, eventMarkers);
                launcherEvents.add(juce::var(eventValue.get()));
            }
            value->setProperty(jsonKeys::events, launcherEvents);
            automationProperty(value, document_ids::gainAutomation,
                               clip.automation);
            launcherClips.add(juce::var(value.get()));
        }
        track->setProperty(jsonKeys::launcherClips, launcherClips);
        profileLauncher += profileSince();
        tracks.add(juce::var(track.get()));
    }
    state->setProperty(jsonKeys::tracks, tracks);
    (void) profileSince();

    juce::Array<juce::var> midiTracks;
    for (const auto& trackInfo : audio_.midiTracks()) {
        ensureMidiNodeGraphState(trackInfo.id);
        auto track = juce::DynamicObject::Ptr(new juce::DynamicObject());
        track->setProperty(jsonKeys::name, trackInfo.name);
        track->setProperty(jsonKeys::gainDb, trackInfo.gainDb);
        track->setProperty(jsonKeys::pan, trackInfo.pan);
        track->setProperty(jsonKeys::mute, trackInfo.muted);
        track->setProperty(jsonKeys::solo, trackInfo.solo);
        track->setProperty(jsonKeys::armed, trackInfo.armed);
        track->setProperty(jsonKeys::nodeGraph, stateWithoutSharedModules(
                                             midiNodeGraphStates_[trackInfo.id]));
        automationProperty(track, document_ids::gainAutomation,
                           audio_.midiAutomationPoints(trackInfo.id));
        juce::Array<juce::var> clips;
        for (const auto& clipInfo : trackInfo.clips) {
            auto clip = juce::DynamicObject::Ptr(new juce::DynamicObject());
            clip->setProperty(jsonKeys::name, clipInfo.name);
            clip->setProperty(jsonKeys::startBeats, clipInfo.startBeats);
            clip->setProperty(jsonKeys::lengthBeats, clipInfo.lengthBeats);
            clip->setProperty(jsonKeys::loop, clipInfo.looping);
            clip->setProperty(jsonKeys::playStartBeats, clipInfo.playStartBeats);
            clip->setProperty(jsonKeys::playEndBeats, clipInfo.playEndBeats);
            clip->setProperty(jsonKeys::loopStartBeats, clipInfo.loopStartBeats);
            clip->setProperty(jsonKeys::loopEndBeats, clipInfo.loopEndBeats);
            clip->setProperty(jsonKeys::colour, static_cast<juce::int64>(clipInfo.colour));
            notesProperty(clip, document_ids::notes, clipInfo.notes);
            automationProperty(clip, document_ids::gainAutomation,
                               clipInfo.automation);
            clips.add(juce::var(clip.get()));
        }
        track->setProperty(jsonKeys::clips, clips);
        midiTracks.add(juce::var(track.get()));
    }
    state->setProperty(jsonKeys::midiTracks, midiTracks);
    const auto profileMidi = profileSince();

    // Keep the original field so projects remain readable by CyDAW 0.7.
    notesProperty(state, document_ids::midiNotes, audio_.midiNotes());
    state->setProperty(jsonKeys::tempo, audio_.tempo());
    state->setProperty(jsonKeys::masterGainDb, audio_.masterGainDb());
    state->setProperty(jsonKeys::arrangementSnapMode,
                       snapSelector_.getSelectedId());
    state->setProperty(jsonKeys::arrangementSnapType,
                       snapTypeSelector_.getSelectedId());
    state->setProperty(jsonKeys::arrangementTool,
                       arrangementToolSelector_.getSelectedId());
    state->setProperty(jsonKeys::launchQuantization,
                       static_cast<int>(audio_.launchQuantization()));
    state->setProperty(jsonKeys::arrangementPlayback,
                       arrangementPlaybackButton_.getToggleState());
    state->setProperty(jsonKeys::launcherActionReplay,
                       launcherActionReplayButton_.getToggleState());
    juce::Array<juce::var> launcherActions;
    for (const auto& action : launcherActions_) {
        auto value = juce::DynamicObject::Ptr(new juce::DynamicObject());
        value->setProperty(jsonKeys::time, action.timeSeconds);
        value->setProperty(jsonKeys::type, static_cast<int>(action.type));
        value->setProperty(jsonKeys::track, static_cast<juce::int64>(
                                      action.trackIndex));
        value->setProperty(jsonKeys::scene, action.sceneIndex);
        launcherActions.add(juce::var(value.get()));
    }
    state->setProperty(jsonKeys::launcherActions, launcherActions);
    state->setProperty(jsonKeys::editorMode, editorModeSelector_.getSelectedId());
    state->setProperty(jsonKeys::loop, loopButton_.getToggleState());
    state->setProperty(jsonKeys::loopRangeExplicit,
                       audio_.hasExplicitLoopRange());
    state->setProperty(jsonKeys::loopStart, audio_.loopStart());
    state->setProperty(jsonKeys::loopEnd, audio_.loopEnd());
    state->setProperty(jsonKeys::position, audio_.position());
    if (selectedTrackGraph.isVoid())
        state->setProperty(jsonKeys::nodeGraph,
                           graphStateWithSharedModules(factoryNodeGraphState_,
                                                       sharedLibrary));
    else
        state->setProperty(jsonKeys::nodeGraph, selectedTrackGraph);
    // The class libraries once, at the top level; the graphs above carry
    // only their inline classes. Eight tracks and two buses each carrying
    // the same thirty-two class bodies made a thirteen-megabyte file of a
    // two-megabyte project, and a load that spent a third of its time
    // parsing it. A file older than this carries the library inside every
    // graph, and the loader merges those as it always did.
    auto nodeLibrary = juce::DynamicObject::Ptr(new juce::DynamicObject());
    nodeLibrary->setProperty(jsonKeys::modules, sharedLibrary);
    state->setProperty(jsonKeys::sharedNodeModules, juce::var(nodeLibrary.get()));
    auto modulationLibrary = juce::DynamicObject::Ptr(new juce::DynamicObject());
    modulationLibrary->setProperty(jsonKeys::modules, sharedModulationLibrary);
    state->setProperty(jsonKeys::sharedModulationModules,
                       juce::var(modulationLibrary.get()));
    state->setProperty(jsonKeys::workspaceLayout,
                       serialiseLayoutState(captureLayoutState("Project")));
    const auto profileTail = profileSince();
    projectDocumentProfile_
        = "save=" + juce::String(profileSave, 2) + "{" + saveGraphStateProfile_
          + "}"
          + " library=" + juce::String(profileLibrary, 2)
          + " buses=" + juce::String(profileBuses, 2)
          + " clips=" + juce::String(profileClips, 2)
          + " graphs=" + juce::String(profileGraphs, 2)
          + " modulation=" + juce::String(profileModulation, 2)
          + " plugins=" + juce::String(profilePlugins, 2)
          + " automation=" + juce::String(profileAutomation, 2)
          + " launcher=" + juce::String(profileLauncher, 2)
          + " midi=" + juce::String(profileMidi, 2)
          + " tail=" + juce::String(profileTail, 2);

    return juce::var(state.get());
}

void WorkspaceComponent::scheduleProjectAutosave()
{
    if (!recoveryEnabled_ || !projectDirty_
        || projectAutosaveInProgress_.exchange(
            true, std::memory_order_acq_rel))
        return;
    nextProjectAutosaveMs_ = juce::Time::currentTimeMillis() + 30'000;
    noteUiHitchEvent("autosave-begin");
    // The point and note arrays are built by the writer, not here.
    auto deferred = std::make_unique<DeferredDocumentWork>();
    auto document = createProjectDocument(
        currentProjectFile_ == juce::File {} ? projectRecoveryFile_
                                              : currentProjectFile_,
        false, true, deferred.get());
    document = ProjectRecovery::makeRecoveryDocument(
        std::move(document), currentProjectFile_);
    noteUiHitchEvent("autosave-queued[" + projectDocumentProfile_ + "]");
    juce::Component::SafePointer<WorkspaceComponent> safeThis(this);
    projectAutosavePool_.addJob(
        new ProjectAutosaveJob(
            projectRecoveryFile_, std::move(document), std::move(deferred),
            [safeThis](juce::Result result) {
                if (safeThis != nullptr)
                    safeThis->finishProjectAutosave(result);
            }),
        true);
}

void WorkspaceComponent::finishProjectAutosave(const juce::Result& result)
{
    noteUiHitchEvent("autosave-written");
    projectAutosaveInProgress_.store(false, std::memory_order_release);
    transientStatus_ = result.wasOk()
                           ? tr("status.projectAutosaved", "time",
                                juce::Time::getCurrentTime().toString(
                                    false, true, false, true))
                           : tr("status.projectAutosaveFailed");
    transientStatusExpiryMs_ = juce::Time::currentTimeMillis() + 5'000;
}

juce::Result WorkspaceComponent::writeProjectRecoveryNow()
{
    if (!recoveryEnabled_ || !projectDirty_)
        return juce::Result::ok();
    auto document = createProjectDocument(
        currentProjectFile_ == juce::File {} ? projectRecoveryFile_
                                              : currentProjectFile_,
        false, true);
    document = ProjectRecovery::makeRecoveryDocument(
        std::move(document), currentProjectFile_);
    return ProjectRecovery::writeDocumentAtomically(projectRecoveryFile_,
                                                     document);
}

void WorkspaceComponent::discardProjectRecovery()
{
    (void) ProjectRecovery::discard(projectRecoveryFile_);
}

void WorkspaceComponent::offerProjectRecovery()
{
    if (!recoveryEnabled_ || recoveryPromptShown_
        || !projectRecoveryFile_.existsAsFile())
        return;
    recoveryPromptShown_ = true;
    juce::var recovery;
    const auto result = ProjectRecovery::readDocument(projectRecoveryFile_,
                                                       recovery);
    if (result.failed() || !ProjectRecovery::isRecoveryDocument(recovery)) {
        transientStatus_ = tr("status.projectRecoveryInvalid");
        transientStatusExpiryMs_ = juce::Time::currentTimeMillis() + 8'000;
        return;
    }
    const auto original = ProjectRecovery::originalProject(recovery);
    const auto projectName = original == juce::File {}
                                 ? tr("app.untitled")
                                 : original.getFileNameWithoutExtension();
    const auto timestamp = ProjectRecovery::recoveryTimestamp(recovery)
                               .toString(true, true, true, true);
    auto message = tr("recovery.message", "project", projectName);
    message = message.replace("{time}", timestamp);
    auto options = juce::MessageBoxOptions()
                       .withIconType(juce::MessageBoxIconType::WarningIcon)
                       .withTitle(tr("recovery.title"))
                       .withMessage(message)
                       .withButton(tr("recovery.restore"))
                       .withButton(tr("recovery.discard"));
    juce::Component::SafePointer<WorkspaceComponent> safeThis(this);
    juce::AlertWindow::showAsync(
        options, [safeThis](const int selection) {
            if (safeThis == nullptr)
                return;
            if (selection == 1)
                safeThis->loadProject(safeThis->projectRecoveryFile_);
            else if (selection == 2)
                safeThis->discardProjectRecovery();
        });
}

void WorkspaceComponent::loadProject(const juce::File& file)
{
    // Timed by stage, for the self-test log: what opening a project costs,
    // and where. Cheap enough to always run.
    auto loadMark = juce::Time::getHighResolutionTicks();
    juce::String loadStages;
    const auto loadStage = [&loadMark, &loadStages](const char* name) {
        const auto now = juce::Time::getHighResolutionTicks();
        loadStages << name << "="
                   << juce::String(juce::Time::highResolutionTicksToSeconds(
                                       now - loadMark) * 1000.0, 1)
                   << " ";
        loadMark = now;
    };
    const auto parsed = juce::JSON::parse(file);
    loadStage("parse");
    auto* state = parsed.getDynamicObject();
    if (state == nullptr || state->getProperty(jsonKeys::format).toString() != "CyDAW Project") {
        showError(tr("error.openProjectTitle"), tr("error.openProject"));
        return;
    }

    if (audio_.hasRecordingSession())
        finishAudioRecording(false);

    // The class libraries, written once at the top level; every graph below
    // is read against them. A file older than that carries the library in
    // each graph, and those are merged as they are read, as before.
    updateSharedNodeModules(state->getProperty(jsonKeys::sharedNodeModules));
    updateSharedModulationModules(
        state->getProperty(jsonKeys::sharedModulationModules));

    (void) projectAutosavePool_.removeAllJobs(true, 10'000);
    projectAutosaveInProgress_.store(false, std::memory_order_release);
    audio_.stopSamplePreview();

    // Clip-local MIDI automation is stored in seconds, so establish the
    // project tempo before recreating and clamping MIDI clip contents.
    const auto projectTempo = static_cast<double>(
        state->getProperty(jsonKeys::tempo));
    audio_.setTempo(projectTempo > 0.0 ? projectTempo : 120.0);

    if (state->hasProperty(jsonKeys::launchQuantization))
        audio_.setLaunchQuantization(static_cast<AudioTrackController::LaunchQuantization>(
            static_cast<int>(state->getProperty(jsonKeys::launchQuantization))));
    else
        audio_.setLaunchQuantization(AudioTrackController::LaunchQuantization::oneBar);

    audio_.clearTracks();
    audio_.clearMidiTracks();
    selectedVirtualBusGraphId_ = 0;
    virtualBusNodeGraphStates_.clear();
    std::vector<VirtualAudioBus> restoredBuses;
    if (const auto* buses = state->getProperty(jsonKeys::virtualBuses).getArray()) {
        restoredBuses.reserve(static_cast<std::size_t>(buses->size()));
        for (const auto& value : *buses) {
            const auto* saved = value.getDynamicObject();
            if (saved == nullptr)
                continue;
            VirtualAudioBus bus;
            bus.id = static_cast<std::uint64_t>(std::max<juce::int64>(
                0, static_cast<juce::int64>(saved->getProperty(jsonKeys::id))));
            if (bus.id == 0)
                continue;
            bus.name = saved->getProperty(jsonKeys::name).toString();
            bus.direction = static_cast<VirtualBusDirection>(juce::jlimit(
                0, 1, static_cast<int>(saved->getProperty(jsonKeys::direction))));
            bus.channels = juce::jlimit(
                1, 2, static_cast<int>(saved->getProperty(jsonKeys::channels)));
            bus.physicalChannelA = std::max(
                0, static_cast<int>(saved->getProperty(jsonKeys::physicalA)));
            bus.physicalChannelB = std::max(
                0, static_cast<int>(saved->getProperty(jsonKeys::physicalB)));
            bus.destinationChannelA = std::max(
                0, static_cast<int>(saved->getProperty(jsonKeys::destinationA)));
            bus.destinationChannelB = std::max(
                0, static_cast<int>(saved->getProperty(jsonKeys::destinationB)));
            bus.gainDb = juce::jlimit(
                -60.0f, 24.0f, static_cast<float>(static_cast<double>(
                    saved->getProperty(jsonKeys::gainDb))));
            bus.muted = static_cast<bool>(saved->getProperty(jsonKeys::muted));
            bus.monitor = static_cast<bool>(saved->getProperty(jsonKeys::monitor));
            bus.exposeToSystem = static_cast<bool>(
                saved->getProperty(jsonKeys::exposeToSystem));
            const auto graphState = saved->getProperty(jsonKeys::nodeGraph);
            if (isNodeGraphState(graphState)) {
                // The file's class bodies join the library; the stored
                // state keeps only what is its own.
                updateSharedNodeModules(graphState);
                virtualBusNodeGraphStates_[bus.id]
                    = stateWithoutSharedModules(cloneJsonValue(graphState));
            }
            restoredBuses.push_back(std::move(bus));
        }
    }
    if (restoredBuses.empty())
        audio_.resetVirtualBuses();
    else
        audio_.replaceVirtualBuses(std::move(restoredBuses));
    for (const auto& bus : audio_.virtualBuses()) {
        ensureVirtualBusNodeGraphState(bus.id);
        setVirtualBusNodeGraphState(
            bus.id, virtualBusNodeGraphStates_[bus.id]);
    }
    launcherActions_.clear();
    nextLauncherReplayAction_ = 0;
    lastLauncherReplayPosition_ = 0.0;
    midiNodeGraphStates_.clear();
    trackModulationStates_.clear();
    selectedMidiTrackId_ = 0;
    selectedMidiClipId_ = 0;
    selectedClipEventId_ = 0;
    clearTrackLanes();
    const auto legacyNodeGraph = state->getProperty(jsonKeys::nodeGraph);
    if (isNodeGraphState(legacyNodeGraph))
        updateSharedNodeModules(legacyNodeGraph);

    const auto addSavedTrack = [this, &file, legacyNodeGraph](const juce::String& audioPath,
                                             const juce::String& savedName,
                                             const juce::int64 savedColour,
                                             const int savedChannels,
                                             const float gainDb,
                                             const float pan,
                                             const bool muted,
                                             const bool solo,
                                             const bool armed,
                                             const double clipStart,
                                             const double sourceOffset,
                                             const double clipDuration,
                                             const float driveDb,
                                             const float filterCutoffHz,
                                             const juce::var& savedPlugins,
                                             const juce::String& legacyPluginPath,
                                             const juce::String& legacyPluginStateBase64,
                                              const juce::var& automation,
                                              const juce::var& savedAudioClips,
                                              const juce::var& savedLauncherClips,
                                             const juce::var& savedNodeGraph,
                                             const juce::var& savedModulationGraph,
                                             const juce::var& savedModulationAssignments) {
        auto audioFile = audioPath.isNotEmpty()
                             ? juce::File(audioPath) : juce::File {};
        if (audioPath.isNotEmpty()
            && !juce::File::isAbsolutePath(audioPath))
            audioFile = file.getParentDirectory().getChildFile(audioPath);
        if (audioPath.isNotEmpty() && !audioFile.existsAsFile()) {
            showError(tr("error.missingAudioTitle"),
                      tr("error.missingAudio", "file", audioFile.getFullPathName()));
            return;
        }

        std::size_t trackIndex {};
        const auto result = audioPath.isEmpty()
                                ? audio_.addEmptyAudioTrack(&trackIndex)
                                : audio_.addAudioTrack(audioFile, &trackIndex);
        if (result.failed()) {
            showError(tr("error.openAudioTitle"), result.getErrorMessage());
            return;
        }
        while (trackPresentations_.size() <= trackIndex) {
            const auto index = trackPresentations_.size();
            trackPresentations_.push_back({
                tr("launcher.audioTrack", "index",
                   juce::String(static_cast<int>(index + 1))),
                defaultTrackColour(index)
            });
        }
        trackPresentations_[trackIndex] = {
            savedName.trim().isNotEmpty()
                ? savedName.trim()
                : tr("launcher.audioTrack", "index",
                     juce::String(static_cast<int>(trackIndex + 1))),
            savedColour != 0
                ? juce::Colour(static_cast<juce::uint32>(savedColour))
                : defaultTrackColour(trackIndex),
            juce::jlimit(1, 16, savedChannels > 0 ? savedChannels : 2)
        };
        audio_.setTrackMixer(trackIndex, gainDb, pan, muted);
        audio_.setTrackSolo(trackIndex, solo);
        if (armed)
            audio_.setTrackArmed(trackIndex, true);
        const auto addedInfo = audio_.trackInfo(trackIndex);
        const auto safeDuration = clipDuration > 0.0
                                      ? clipDuration
                                      : std::max(0.001, addedInfo.sourceLengthSeconds - sourceOffset);
        if (const auto* clips = savedAudioClips.getArray()) {
            const auto initialClips = audio_.audioClips(trackIndex);
            for (const auto& clip : initialClips)
                (void) audio_.removeAudioClip(trackIndex, clip.id);
            for (const auto& clipValue : *clips) {
                if (const auto* clip = clipValue.getDynamicObject()) {
                    auto clipSource = audioFile;
                    const auto savedSource = clip->getProperty(jsonKeys::source).toString();
                    if (savedSource.isNotEmpty()) {
                        clipSource = juce::File(savedSource);
                        if (!juce::File::isAbsolutePath(savedSource))
                            clipSource = file.getParentDirectory().getChildFile(
                                savedSource);
                    }
                    if (!clipSource.existsAsFile()) {
                        showError(tr("error.missingAudioTitle"),
                                  tr("error.missingAudio", "file",
                                     clipSource.getFullPathName()));
                        continue;
                    }
                    const auto restoredClipId = audio_.addAudioClip(
                        trackIndex,
                        static_cast<double>(clip->getProperty(jsonKeys::start)),
                        static_cast<double>(clip->getProperty(jsonKeys::offset)),
                        static_cast<double>(clip->getProperty(jsonKeys::duration)),
                        clip->getProperty(jsonKeys::name).toString(),
                        static_cast<std::uint32_t>(static_cast<juce::int64>(
                            clip->getProperty(jsonKeys::colour))),
                        clipSource,
                        static_cast<double>(clip->getProperty(jsonKeys::fadeIn)),
                        static_cast<double>(clip->getProperty(jsonKeys::fadeOut)));
                    if (restoredClipId != 0) {
                        const auto savedSourceDuration = clip->hasProperty(
                            juce::Identifier("sourceDuration"))
                            ? static_cast<double>(
                                  clip->getProperty(jsonKeys::sourceDuration))
                            : static_cast<double>(
                                  clip->getProperty(jsonKeys::duration));
                        (void) audio_.updateAudioClip(
                            trackIndex, restoredClipId,
                            static_cast<double>(clip->getProperty(jsonKeys::start)),
                            static_cast<double>(clip->getProperty(jsonKeys::offset)),
                            static_cast<double>(clip->getProperty(jsonKeys::duration)),
                            static_cast<double>(clip->getProperty(jsonKeys::fadeIn)),
                            static_cast<double>(clip->getProperty(jsonKeys::fadeOut)),
                            savedSourceDuration);
                        const auto savedSourceTempo = clip->hasProperty(
                            juce::Identifier("sourceTempo"))
                            ? static_cast<double>(
                                  clip->getProperty(jsonKeys::sourceTempo))
                            : 0.0;
                        const auto savedStretchMode = juce::jlimit(
                            0, 3,
                            clip->hasProperty(
                                juce::Identifier("stretchMode"))
                                ? static_cast<int>(
                                      clip->getProperty(jsonKeys::stretchMode))
                                : 0);
                        (void) audio_.setAudioClipStretchSettings(
                            trackIndex, restoredClipId, savedSourceTempo,
                            static_cast<AudioTrackController::StretchMode>(
                                savedStretchMode));
                        if (const auto* markers = clip->getProperty(
                                "stretchMarkers").getArray()) {
                            for (const auto& markerValue : *markers)
                                if (const auto* marker
                                    = markerValue.getDynamicObject())
                                    (void) audio_.addAudioClipStretchMarker(
                                        trackIndex, restoredClipId,
                                        static_cast<double>(
                                            marker->getProperty(jsonKeys::source)),
                                        static_cast<double>(
                                            marker->getProperty(jsonKeys::timeline)));
                        }
                    }
                    const auto* savedTakes = clip->getProperty(jsonKeys::takes).getArray();
                    if (restoredClipId != 0 && savedTakes != nullptr
                        && !savedTakes->isEmpty()) {
                        std::vector<AudioTrackController::AudioTake> takes;
                        takes.reserve(static_cast<std::size_t>(savedTakes->size()));
                        bool allTakesAvailable = true;
                        for (const auto& takeValue : *savedTakes) {
                            const auto* take = takeValue.getDynamicObject();
                            if (take == nullptr) {
                                allTakesAvailable = false;
                                break;
                            }
                            const auto takePath =
                                take->getProperty(jsonKeys::source).toString();
                            auto takeFile = juce::File(takePath);
                            if (!juce::File::isAbsolutePath(takePath))
                                takeFile = file.getParentDirectory().getChildFile(
                                    takePath);
                            if (!takeFile.existsAsFile()) {
                                showError(
                                    tr("error.missingAudioTitle"),
                                    tr("error.missingAudio", "file",
                                       takeFile.getFullPathName()));
                                allTakesAvailable = false;
                                break;
                            }
                            takes.push_back({
                                take->getProperty(jsonKeys::name).toString(),
                                takeFile,
                                0.0
                            });
                        }
                        if (allTakesAvailable && !takes.empty()) {
                            const auto activeTake = static_cast<std::size_t>(
                                juce::jlimit<juce::int64>(
                                    0,
                                    static_cast<juce::int64>(takes.size() - 1),
                                    static_cast<juce::int64>(
                                        clip->getProperty(jsonKeys::activeTake))));
                            const auto takesResult = audio_.replaceAudioTakes(
                                trackIndex, restoredClipId, std::move(takes),
                                activeTake);
                            if (takesResult.failed())
                                showError(tr("error.openAudioTitle"),
                                          takesResult.getErrorMessage());
                        }
                    }
                    if (restoredClipId != 0) {
                        if (const auto* savedEvents
                            = clip->getProperty(jsonKeys::events).getArray();
                            savedEvents != nullptr
                            && !savedEvents->isEmpty()) {
                            std::vector<AudioTrackController::AudioEvent>
                                events;
                            events.reserve(static_cast<std::size_t>(
                                savedEvents->size()));
                            auto eventsAvailable = true;
                            for (const auto& eventValue : *savedEvents) {
                                const auto* event
                                    = eventValue.getDynamicObject();
                                if (event == nullptr) {
                                    eventsAvailable = false;
                                    break;
                                }
                                const auto path
                                    = event->getProperty(jsonKeys::source)
                                          .toString();
                                auto source = path.isNotEmpty()
                                                  ? juce::File(path)
                                                  : clipSource;
                                if (path.isNotEmpty()
                                    && !juce::File::isAbsolutePath(path))
                                    source = file.getParentDirectory()
                                                 .getChildFile(path);
                                if (!source.existsAsFile()) {
                                    showError(
                                        tr("error.missingAudioTitle"),
                                        tr("error.missingAudio", "file",
                                           source.getFullPathName()));
                                    eventsAvailable = false;
                                    break;
                                }
                                AudioTrackController::AudioEvent restored;
                                restored.id = static_cast<std::uint64_t>(
                                    static_cast<juce::int64>(
                                        event->getProperty(jsonKeys::id)));
                                restored.name
                                    = event->getProperty(jsonKeys::name).toString();
                                restored.startSeconds = static_cast<double>(
                                    event->getProperty(jsonKeys::start));
                                restored.sourceOffsetSeconds
                                    = static_cast<double>(
                                        event->getProperty(jsonKeys::offset));
                                restored.durationSeconds
                                    = static_cast<double>(
                                        event->getProperty(jsonKeys::duration));
                                restored.sourceDurationSeconds
                                    = event->hasProperty(
                                          juce::Identifier(
                                              "sourceDuration"))
                                          ? static_cast<double>(
                                                event->getProperty(
                                                    "sourceDuration"))
                                          : restored.durationSeconds;
                                restored.colour = static_cast<std::uint32_t>(
                                    static_cast<juce::int64>(
                                        event->getProperty(jsonKeys::colour)));
                                restored.sourceFile = source;
                                restored.fadeInSeconds
                                    = static_cast<double>(
                                        event->getProperty(jsonKeys::fadeIn));
                                restored.fadeOutSeconds
                                    = static_cast<double>(
                                        event->getProperty(jsonKeys::fadeOut));
                                restored.sourceTempoBpm
                                    = static_cast<double>(
                                        event->getProperty(jsonKeys::sourceTempo));
                                restored.gainDb
                                    = event->hasProperty(
                                          juce::Identifier("gainDb"))
                                          ? static_cast<float>(
                                                static_cast<double>(
                                                    event->getProperty(
                                                        "gainDb")))
                                          : 0.0f;
                                restored.stretchMode
                                    = static_cast<
                                        AudioTrackController::StretchMode>(
                                        juce::jlimit(
                                            0, 3, static_cast<int>(
                                                event->getProperty(
                                                    "stretchMode"))));
                                if (const auto* markers
                                    = event->getProperty(
                                               "stretchMarkers")
                                          .getArray())
                                    for (const auto& markerValue
                                         : *markers)
                                        if (const auto* marker
                                            = markerValue
                                                  .getDynamicObject())
                                            restored.stretchMarkers
                                                .push_back({
                                                    static_cast<
                                                        std::uint64_t>(
                                                        static_cast<
                                                            juce::int64>(
                                                            marker
                                                                ->getProperty(
                                                                    "id"))),
                                                    static_cast<double>(
                                                        marker->getProperty(
                                                            "source")),
                                                    static_cast<double>(
                                                        marker->getProperty(
                                                            "timeline")) });
                                events.push_back(std::move(restored));
                            }
                            if (eventsAvailable && !events.empty()) {
                                const auto eventResult
                                    = audio_.replaceAudioClipEvents(
                                        trackIndex, restoredClipId,
                                        std::move(events));
                                if (eventResult.failed())
                                    showError(tr("error.openAudioTitle"),
                                              eventResult
                                                  .getErrorMessage());
                            }
                        }
                        const auto playStart = clip->hasProperty(
                            juce::Identifier("playStart"))
                                                   ? static_cast<double>(
                                                         clip->getProperty(
                                                             "playStart"))
                                                   : 0.0;
                        const auto playEnd = clip->hasProperty(
                            juce::Identifier("playEnd"))
                                                 ? static_cast<double>(
                                                       clip->getProperty(
                                                           "playEnd"))
                                                 : static_cast<double>(
                                                       clip->getProperty(
                                                           "duration"));
                        const auto loopStart = clip->hasProperty(
                            juce::Identifier("loopStart"))
                                                   ? static_cast<double>(
                                                         clip->getProperty(
                                                             "loopStart"))
                                                   : playStart;
                        const auto loopEnd = clip->hasProperty(
                            juce::Identifier("loopEnd"))
                                                 ? static_cast<double>(
                                                       clip->getProperty(
                                                           "loopEnd"))
                                                 : playEnd;
                        (void) audio_.setAudioClipTimelineBounds(
                            trackIndex, restoredClipId,
                            static_cast<double>(
                                clip->getProperty(jsonKeys::duration)),
                            playStart, playEnd, loopStart, loopEnd,
                            static_cast<bool>(clip->getProperty(jsonKeys::loop)));
                        if (const auto* points = clip->getProperty(
                                "gainAutomation").getArray()) {
                            for (const auto& pointValue : *points) {
                                const auto* point
                                    = pointValue.getDynamicObject();
                                if (point == nullptr)
                                    continue;
                                const auto pointId
                                    = audio_.addAudioClipAutomationPoint(
                                        trackIndex, restoredClipId,
                                        static_cast<double>(
                                            point->getProperty(jsonKeys::time)),
                                        static_cast<float>(
                                            static_cast<double>(
                                                point->getProperty(
                                                    "valueDb"))));
                                if (pointId != 0 && point->hasProperty(
                                        juce::Identifier("curve")))
                                    (void) audio_
                                        .setAudioClipAutomationPointCurve(
                                            trackIndex, restoredClipId,
                                            pointId,
                                            static_cast<
                                                AudioTrackController::
                                                    AutomationCurve>(
                                                juce::jlimit(
                                                    0, 1,
                                                    static_cast<int>(
                                                        point->getProperty(
                                                            "curve")))),
                                            point->hasProperty(
                                                juce::Identifier(
                                                    "curveAmount"))
                                                ? static_cast<float>(
                                                      static_cast<double>(
                                                          point->getProperty(
                                                              "curveAmount")))
                                                : 0.0f);
                            }
                        }
                    }
                }
            }
        } else {
            audio_.setTrackClip(trackIndex, clipStart, sourceOffset, safeDuration);
        }
        audio_.clearArrangementEditHistory(trackIndex);
        audio_.setTrackEffects(trackIndex,
                               driveDb,
                               filterCutoffHz > 0.0f ? filterCutoffHz : 20'000.0f);
        const auto loadPlugin = [this, &file, trackIndex](const juce::String& pluginPath,
                                                          const juce::String& pluginStateBase64,
                                                          const juce::var& automationLanes) {
            if (pluginPath.isEmpty())
                return;
            auto pluginFile = juce::File(pluginPath);
            if (!juce::File::isAbsolutePath(pluginPath))
                pluginFile = file.getParentDirectory().getChildFile(pluginPath);
            if (pluginFile.exists()) {
                std::size_t pluginIndex {};
                const auto pluginResult = audio_.setTrackPlugin(trackIndex, pluginFile, &pluginIndex);
                if (pluginResult.failed()) {
                    showError(tr("error.pluginInsertTitle"), pluginResult.getErrorMessage());
                } else {
                    if (pluginStateBase64.isNotEmpty()) {
                        juce::MemoryBlock pluginState;
                        if (pluginState.fromBase64Encoding(pluginStateBase64)) {
                            const auto stateResult = audio_.setTrackPluginState(trackIndex, pluginIndex,
                                                                                pluginState);
                            if (stateResult.failed())
                                showError(tr("error.pluginStateTitle"), stateResult.getErrorMessage());
                        }
                    }
                    if (const auto* lanes = automationLanes.getArray()) {
                        for (const auto& laneValue : *lanes) {
                            const auto* lane = laneValue.getDynamicObject();
                            if (lane == nullptr)
                                continue;
                            const auto parameterIndex = static_cast<std::uint32_t>(
                                static_cast<juce::int64>(lane->getProperty(jsonKeys::parameter)));
                            const auto parameterName = lane->getProperty(jsonKeys::name).toString();
                            if (const auto* points = lane->getProperty(jsonKeys::points).getArray()) {
                                for (const auto& pointValue : *points)
                                    if (const auto* point = pointValue.getDynamicObject()) {
                                        const auto pointId = audio_.addPluginAutomationPoint(
                                            trackIndex, pluginIndex, parameterIndex, parameterName,
                                            static_cast<double>(point->getProperty(jsonKeys::time)),
                                            static_cast<float>(static_cast<double>(
                                                point->getProperty(jsonKeys::value))));
                                        if (pointId != 0 && point->hasProperty(
                                                juce::Identifier("curve")))
                                            (void) audio_.setPluginAutomationPointCurve(
                                                trackIndex, pluginIndex,
                                                parameterIndex, pointId,
                                                static_cast<AudioTrackController::AutomationCurve>(
                                                    juce::jlimit(0, 1, static_cast<int>(
                                                        point->getProperty(jsonKeys::curve)))),
                                                point->hasProperty(juce::Identifier(
                                                    "curveAmount"))
                                                    ? static_cast<float>(
                                                          static_cast<double>(
                                                              point->getProperty(
                                                                  "curveAmount")))
                                                    : 0.0f);
                                    }
                            }
                        }
                    }
                }
            } else {
                showError(tr("error.pluginInsertTitle"),
                          tr("error.missingPlugin", "file", pluginFile.getFullPathName()));
            }
        };
        if (const auto* plugins = savedPlugins.getArray(); plugins != nullptr && !plugins->isEmpty()) {
            for (const auto& pluginValue : *plugins)
                if (const auto* plugin = pluginValue.getDynamicObject())
                    loadPlugin(plugin->getProperty(jsonKeys::file).toString(),
                               plugin->getProperty(jsonKeys::state).toString(),
                               plugin->getProperty(jsonKeys::automation));
        } else {
            loadPlugin(legacyPluginPath, legacyPluginStateBase64, {});
        }
        if (const auto* points = automation.getArray()) {
            for (const auto& value : *points)
                if (const auto* point = value.getDynamicObject()) {
                    const auto pointId = audio_.addAutomationPoint(
                        trackIndex,
                        static_cast<double>(point->getProperty(jsonKeys::time)),
                        static_cast<float>(static_cast<double>(point->getProperty(jsonKeys::valueDb))));
                    if (pointId != 0 && point->hasProperty(
                            juce::Identifier("curve")))
                        (void) audio_.setAutomationPointCurve(
                            trackIndex, pointId,
                            static_cast<AudioTrackController::AutomationCurve>(
                                juce::jlimit(0, 1, static_cast<int>(
                                    point->getProperty(jsonKeys::curve)))),
                            point->hasProperty(juce::Identifier(
                                "curveAmount"))
                                ? static_cast<float>(static_cast<double>(
                                      point->getProperty(jsonKeys::curveAmount)))
                                : 0.0f);
                }
        }
        if (const auto* clips = savedLauncherClips.getArray()) {
            for (const auto& clipValue : *clips) {
                if (const auto* clip = clipValue.getDynamicObject()) {
                    const auto scene = static_cast<int>(
                        clip->getProperty(jsonKeys::scene));
                    const auto name = clip->getProperty(jsonKeys::name).toString();
                    const auto duration = std::max(
                        0.001, static_cast<double>(
                                   clip->getProperty(jsonKeys::duration)));
                    const auto looping = static_cast<bool>(
                        clip->getProperty(jsonKeys::loop));
                    const auto colour = static_cast<std::uint32_t>(
                        static_cast<juce::int64>(
                            clip->getProperty(jsonKeys::colour)));
                    auto launcherSource = audioFile;
                    const auto savedSource =
                        clip->getProperty(jsonKeys::source).toString();
                    if (savedSource.isNotEmpty()) {
                        launcherSource = juce::File(savedSource);
                        if (!juce::File::isAbsolutePath(savedSource))
                            launcherSource = file.getParentDirectory()
                                                 .getChildFile(savedSource);
                    }
                    const auto* savedEvents = clip->getProperty(
                        "events").getArray();
                    const auto usesTimeline
                        = static_cast<bool>(clip->getProperty(
                              "eventTimeline"))
                          || savedEvents != nullptr;
                    auto created = false;
                    if (usesTimeline) {
                        created = audio_.createEmptyLauncherClip(
                            trackIndex, scene, name, duration, colour);
                    } else if (launcherSource.existsAsFile()) {
                        created = audio_.setLauncherClip(
                            trackIndex, scene, name,
                            static_cast<double>(
                                clip->getProperty(jsonKeys::offset)),
                            duration, looping, colour, launcherSource);
                    } else {
                        showError(tr("error.missingAudioTitle"),
                                  tr("error.missingAudio", "file",
                                     launcherSource.getFullPathName()));
                    }
                    if (!created)
                        continue;
                    const auto playStart = clip->hasProperty(
                        juce::Identifier("playStart"))
                                               ? static_cast<double>(
                                                     clip->getProperty(
                                                         "playStart"))
                                               : 0.0;
                    const auto playEnd = clip->hasProperty(
                        juce::Identifier("playEnd"))
                                             ? static_cast<double>(
                                                   clip->getProperty(
                                                       "playEnd"))
                                             : duration;
                    const auto loopStart = clip->hasProperty(
                        juce::Identifier("loopStart"))
                                               ? static_cast<double>(
                                                     clip->getProperty(
                                                         "loopStart"))
                                               : playStart;
                    const auto loopEnd = clip->hasProperty(
                        juce::Identifier("loopEnd"))
                                             ? static_cast<double>(
                                                   clip->getProperty(
                                                       "loopEnd"))
                                             : playEnd;
                    (void) audio_.setLauncherClipTimelineBounds(
                        trackIndex, scene, duration, playStart, playEnd,
                        loopStart, loopEnd, looping);
                    if (const auto* points = clip->getProperty(
                            "gainAutomation").getArray()) {
                        for (const auto& pointValue : *points) {
                            const auto* point
                                = pointValue.getDynamicObject();
                            if (point == nullptr)
                                continue;
                            const auto pointId
                                = audio_.addLauncherClipAutomationPoint(
                                    trackIndex, scene,
                                    static_cast<double>(
                                        point->getProperty(jsonKeys::time)),
                                    static_cast<float>(static_cast<double>(
                                        point->getProperty(jsonKeys::valueDb))));
                            if (pointId != 0 && point->hasProperty(
                                    juce::Identifier("curve")))
                                (void) audio_
                                    .setLauncherClipAutomationPointCurve(
                                        trackIndex, scene, pointId,
                                        static_cast<
                                            AudioTrackController::
                                                AutomationCurve>(
                                            juce::jlimit(
                                                0, 1,
                                                static_cast<int>(
                                                    point->getProperty(
                                                        "curve")))),
                                        point->hasProperty(
                                            juce::Identifier(
                                                "curveAmount"))
                                            ? static_cast<float>(
                                                  static_cast<double>(
                                                      point->getProperty(
                                                          "curveAmount")))
                                            : 0.0f);
                        }
                    }
                    if (!usesTimeline || savedEvents == nullptr)
                        continue;
                    std::vector<AudioTrackController::AudioEvent> events;
                    auto eventsAvailable = true;
                    events.reserve(static_cast<std::size_t>(
                        savedEvents->size()));
                    for (const auto& eventValue : *savedEvents) {
                        const auto* event = eventValue.getDynamicObject();
                        if (event == nullptr) {
                            eventsAvailable = false;
                            break;
                        }
                        const auto eventPath = event->getProperty(
                            "source").toString();
                        auto eventSource = eventPath.isNotEmpty()
                                               ? juce::File(eventPath)
                                               : launcherSource;
                        if (eventPath.isNotEmpty()
                            && !juce::File::isAbsolutePath(eventPath))
                            eventSource = file.getParentDirectory()
                                              .getChildFile(eventPath);
                        if (!eventSource.existsAsFile()) {
                            showError(
                                tr("error.missingAudioTitle"),
                                tr("error.missingAudio", "file",
                                   eventSource.getFullPathName()));
                            eventsAvailable = false;
                            break;
                        }
                        AudioTrackController::AudioEvent restored;
                        restored.id = static_cast<std::uint64_t>(
                            static_cast<juce::int64>(
                                event->getProperty(jsonKeys::id)));
                        restored.name = event->getProperty(jsonKeys::name)
                                            .toString();
                        restored.startSeconds = static_cast<double>(
                            event->getProperty(jsonKeys::start));
                        restored.sourceOffsetSeconds
                            = static_cast<double>(
                                event->getProperty(jsonKeys::offset));
                        restored.durationSeconds = static_cast<double>(
                            event->getProperty(jsonKeys::duration));
                        restored.sourceDurationSeconds
                            = event->hasProperty(juce::Identifier(
                                  "sourceDuration"))
                                  ? static_cast<double>(event->getProperty(
                                        "sourceDuration"))
                                  : restored.durationSeconds;
                        restored.colour = static_cast<std::uint32_t>(
                            static_cast<juce::int64>(
                                event->getProperty(jsonKeys::colour)));
                        restored.sourceFile = eventSource;
                        restored.fadeInSeconds = static_cast<double>(
                            event->getProperty(jsonKeys::fadeIn));
                        restored.fadeOutSeconds = static_cast<double>(
                            event->getProperty(jsonKeys::fadeOut));
                        restored.sourceTempoBpm = static_cast<double>(
                            event->getProperty(jsonKeys::sourceTempo));
                        restored.gainDb = event->hasProperty(
                            juce::Identifier("gainDb"))
                            ? static_cast<float>(static_cast<double>(
                                  event->getProperty(jsonKeys::gainDb)))
                            : 0.0f;
                        restored.stretchMode = static_cast<
                            AudioTrackController::StretchMode>(
                            juce::jlimit(0, 3, static_cast<int>(
                                event->getProperty(jsonKeys::stretchMode))));
                        if (const auto* markers = event->getProperty(
                                "stretchMarkers").getArray())
                            for (const auto& markerValue : *markers)
                                if (const auto* marker
                                    = markerValue.getDynamicObject())
                                    restored.stretchMarkers.push_back({
                                        static_cast<std::uint64_t>(
                                            static_cast<juce::int64>(
                                                marker->getProperty(jsonKeys::id))),
                                        static_cast<double>(
                                            marker->getProperty(jsonKeys::source)),
                                        static_cast<double>(
                                            marker->getProperty(
                                                "timeline")) });
                        events.push_back(std::move(restored));
                    }
                    if (eventsAvailable) {
                        const auto eventResult
                            = audio_.replaceLauncherClipEvents(
                                trackIndex, scene, std::move(events));
                        if (eventResult.failed())
                            showError(tr("error.openAudioTitle"),
                                      eventResult.getErrorMessage());
                    }
                }
            }
        }
        setTrackNodeGraphState(trackIndex,
                               isNodeGraphState(savedNodeGraph)
                                   ? savedNodeGraph
                                   : legacyNodeGraph);
        ensureTrackModulationState(trackIndex);
        auto& modulation = trackModulationStates_[trackIndex];
        if (savedModulationGraph.getDynamicObject() != nullptr)
            adoptTrackModulationGraph(trackIndex, savedModulationGraph);
        modulation.assignments.clear();
        if (const auto* assignments = savedModulationAssignments.getArray()) {
            for (const auto& assignmentValue : *assignments) {
                const auto* assignment = assignmentValue.getDynamicObject();
                if (assignment == nullptr)
                    continue;
                modulation.assignments.push_back({
                    assignment->getProperty(jsonKeys::binding).toString(),
                    assignment->getProperty(jsonKeys::label).toString(),
                    static_cast<double>(assignment->getProperty(jsonKeys::base)),
                    static_cast<double>(assignment->getProperty(jsonKeys::minimum)),
                    static_cast<double>(assignment->getProperty(jsonKeys::maximum)),
                    static_cast<double>(assignment->getProperty(jsonKeys::amount)),
                    static_cast<bool>(assignment->getProperty(jsonKeys::enabled)) });
            }
        }
        addTrackLane(trackIndex);
    };

    // One arrangement rebuild per track once every clip is back, not one
    // per clip restored: each rebuild waited for its read-ahead buffers.
    loadStage("setup");
    audio_.beginArrangementBatch();
    const auto savedTracks = state->getProperty(jsonKeys::tracks);
    if (const auto* tracks = savedTracks.getArray()) {
        for (const auto& value : *tracks) {
            if (const auto* track = value.getDynamicObject()) {
                const auto previousTrackCount = audio_.trackCount();
                addSavedTrack(track->getProperty(jsonKeys::audioFile).toString(),
                              track->getProperty(jsonKeys::name).toString(),
                              static_cast<juce::int64>(track->getProperty(jsonKeys::colour)),
                              track->hasProperty(jsonKeys::audioChannels)
                                  ? static_cast<int>(track->getProperty(
                                        "audioChannels"))
                                  : 2,
                              static_cast<float>(static_cast<double>(track->getProperty(jsonKeys::gainDb))),
                              static_cast<float>(static_cast<double>(track->getProperty(jsonKeys::pan))),
                              static_cast<bool>(track->getProperty(jsonKeys::mute)),
                              static_cast<bool>(track->getProperty(jsonKeys::solo)),
                              static_cast<bool>(track->getProperty(jsonKeys::armed)),
                              static_cast<double>(track->getProperty(jsonKeys::clipStart)),
                              static_cast<double>(track->getProperty(jsonKeys::sourceOffset)),
                              static_cast<double>(track->getProperty(jsonKeys::clipDuration)),
                              static_cast<float>(static_cast<double>(track->getProperty(jsonKeys::driveDb))),
                              static_cast<float>(static_cast<double>(track->getProperty(jsonKeys::filterCutoffHz))),
                              track->getProperty(jsonKeys::plugins),
                              track->getProperty(jsonKeys::pluginFile).toString(),
                              track->getProperty(jsonKeys::pluginState).toString(),
                              track->getProperty(jsonKeys::gainAutomation),
                              track->getProperty(jsonKeys::audioClips),
                              track->getProperty(jsonKeys::launcherClips),
                              track->getProperty(jsonKeys::nodeGraph),
                              track->getProperty(jsonKeys::modulationGraph),
                              track->getProperty(jsonKeys::modulationAssignments));
                if (audio_.trackCount() > previousTrackCount) {
                    const auto index = audio_.trackCount() - 1;
                    if (track->hasProperty(
                            juce::Identifier("inputBusId")))
                        (void) audio_.setTrackInputBus(
                            index, static_cast<std::uint64_t>(
                                std::max<juce::int64>(
                                    0, static_cast<juce::int64>(
                                           track->getProperty(
                                               "inputBusId")))));
                    const auto automationVisible
                        = static_cast<bool>(track->getProperty(
                            "automationVisible"));
                    const auto laneHeight = track->hasProperty(
                        juce::Identifier("laneHeight"))
                        ? juce::jlimit(82, 420, static_cast<int>(
                              track->getProperty(jsonKeys::laneHeight)))
                        : automationVisible ? 172 : 88;
                    if (index < trackLaneHeights_.size())
                        trackLaneHeights_[index] = laneHeight;
                    if (index < trackAutomationVisible_.size())
                        trackAutomationVisible_[index]
                            = automationVisible ? 1 : 0;
                    if (index < trackLanes_.size())
                        trackLanes_[index]->setAutomationVisible(
                            automationVisible);
                }
            }
            layoutTrackLanes();
        }
    } else {
        // Backward compatibility with the original single-track project format.
        addSavedTrack(state->getProperty(jsonKeys::audioFile).toString(),
                      {},
                      0,
                      2,
                      static_cast<float>(static_cast<double>(state->getProperty(jsonKeys::gainDb))),
                      static_cast<float>(static_cast<double>(state->getProperty(jsonKeys::pan))),
                      static_cast<bool>(state->getProperty(jsonKeys::mute)),
                      false,
                      false,
                      0.0,
                      0.0,
                      0.0,
                      0.0f,
                      20'000.0f,
                      {},
                      {},
                      {},
                      {},
                      {},
                      {},
                      legacyNodeGraph,
                      {},
                      {});
    }
    loadStage("tracks");
    if (const auto rebuilt = audio_.endArrangementBatch(); rebuilt.failed())
        showError(tr("error.openAudioTitle"), rebuilt.getErrorMessage());
    loadStage("arrangements");
    virtualBusManager_.refresh();

    const auto savedTempo = static_cast<double>(state->getProperty(jsonKeys::tempo));
    audio_.setTempo(savedTempo > 0.0 ? savedTempo : 120.0);
    audio_.setMasterGain(
        state->hasProperty(jsonKeys::masterGainDb)
            ? static_cast<float>(static_cast<double>(
                  state->getProperty(jsonKeys::masterGainDb)))
            : 0.0f);
    const auto arrangementPlayback = !state->hasProperty(
                                         "arrangementPlayback")
                                     || static_cast<bool>(state->getProperty(
                                         "arrangementPlayback"));
    arrangementPlaybackButton_.setToggleState(
        arrangementPlayback, juce::dontSendNotification);
    audio_.setArrangementPlaybackEnabled(arrangementPlayback);
    launcherActionRecordButton_.setToggleState(
        false, juce::dontSendNotification);
    launcherActionReplayButton_.setToggleState(
        state->hasProperty(jsonKeys::launcherActionReplay)
            && static_cast<bool>(state->getProperty(
                "launcherActionReplay")),
        juce::dontSendNotification);
    const auto savedLauncherActions = state->getProperty(jsonKeys::launcherActions);
    if (const auto* actions = savedLauncherActions.getArray()) {
        launcherActions_.reserve(static_cast<std::size_t>(actions->size()));
        for (const auto& value : *actions) {
            const auto* action = value.getDynamicObject();
            if (action == nullptr)
                continue;
            launcherActions_.push_back({
                std::max(0.0, static_cast<double>(
                                  action->getProperty(jsonKeys::time))),
                static_cast<LauncherAction::Type>(juce::jlimit(
                    0, 2, static_cast<int>(action->getProperty(jsonKeys::type)))),
                static_cast<std::size_t>(std::max<juce::int64>(
                    0, static_cast<juce::int64>(
                           action->getProperty(jsonKeys::track)))),
                static_cast<int>(action->getProperty(jsonKeys::scene)) });
        }
        std::stable_sort(
            launcherActions_.begin(), launcherActions_.end(),
            [](const LauncherAction& first,
               const LauncherAction& second) {
                return first.timeSeconds < second.timeSeconds;
            });
    }
    const auto savedSnapMode = state->hasProperty(jsonKeys::arrangementSnapMode)
                                   ? static_cast<int>(state->getProperty(
                                         "arrangementSnapMode"))
                                   : 4;
    snapSelector_.setSelectedId(juce::jlimit(1, 5, savedSnapMode),
                                juce::dontSendNotification);
    snapTypeSelector_.setSelectedId(
        juce::jlimit(1, 3,
                     state->hasProperty(jsonKeys::arrangementSnapType)
                         ? static_cast<int>(state->getProperty(
                               "arrangementSnapType"))
                         : 1),
        juce::dontSendNotification);
    arrangementToolSelector_.setSelectedId(
        juce::jlimit(1, 5,
                     state->hasProperty(jsonKeys::arrangementTool)
                         ? static_cast<int>(state->getProperty(
                               "arrangementTool"))
                         : 1),
        juce::dontSendNotification);
    refreshArrangementGrid();
    const auto savedMidiTracks = state->getProperty(jsonKeys::midiTracks);
    auto savedArmedTrackId = std::uint64_t {};
    if (const auto* tracks = savedMidiTracks.getArray(); tracks != nullptr && !tracks->isEmpty()) {
        for (const auto& trackValue : *tracks) {
            const auto* savedTrack = trackValue.getDynamicObject();
            if (savedTrack == nullptr)
                continue;
            const auto trackId = audio_.addMidiTrack(savedTrack->getProperty(jsonKeys::name).toString());
            const auto createdTracks = audio_.midiTracks();
            const auto created = std::find_if(createdTracks.begin(), createdTracks.end(),
                                              [trackId](const auto& track) {
                                                  return track.id == trackId;
                                              });
            if (created != createdTracks.end())
                for (const auto& clip : created->clips)
                    (void) audio_.removeMidiClip(trackId, clip.id);

            if (const auto* clips = savedTrack->getProperty(jsonKeys::clips).getArray()) {
                for (const auto& clipValue : *clips) {
                    const auto* savedClip = clipValue.getDynamicObject();
                    if (savedClip == nullptr)
                        continue;
                    const auto clipId = audio_.addMidiClip(
                        trackId,
                        savedClip->getProperty(jsonKeys::name).toString(),
                        static_cast<double>(savedClip->getProperty(jsonKeys::startBeats)),
                        std::max(0.25, static_cast<double>(savedClip->getProperty(jsonKeys::lengthBeats))),
                        static_cast<bool>(savedClip->getProperty(jsonKeys::loop)),
                        static_cast<std::uint32_t>(static_cast<juce::int64>(
                            savedClip->getProperty(jsonKeys::colour))));
                    const auto clipLength = std::max(
                        0.25, static_cast<double>(
                                  savedClip->getProperty(jsonKeys::lengthBeats)));
                    const auto playStart = savedClip->hasProperty(
                        juce::Identifier("playStartBeats"))
                        ? static_cast<double>(savedClip->getProperty(
                              "playStartBeats"))
                        : 0.0;
                    const auto playEnd = savedClip->hasProperty(
                        juce::Identifier("playEndBeats"))
                        ? static_cast<double>(savedClip->getProperty(
                              "playEndBeats"))
                        : clipLength;
                    const auto loopStart = savedClip->hasProperty(
                        juce::Identifier("loopStartBeats"))
                        ? static_cast<double>(savedClip->getProperty(
                              "loopStartBeats"))
                        : playStart;
                    const auto loopEnd = savedClip->hasProperty(
                        juce::Identifier("loopEndBeats"))
                        ? static_cast<double>(savedClip->getProperty(
                              "loopEndBeats"))
                        : playEnd;
                    (void) audio_.setMidiClipTimelineBounds(
                        trackId, clipId, clipLength, playStart, playEnd,
                        loopStart, loopEnd,
                        static_cast<bool>(savedClip->getProperty(jsonKeys::loop)));
                    if (const auto* notes = savedClip->getProperty(jsonKeys::notes).getArray()) {
                        for (const auto& noteValue : *notes)
                            if (const auto* note = noteValue.getDynamicObject())
                                (void) audio_.addMidiNote(
                                    trackId, clipId,
                                    static_cast<int>(note->getProperty(jsonKeys::note)),
                                    static_cast<double>(note->getProperty(jsonKeys::startBeats)),
                                    static_cast<double>(note->getProperty(jsonKeys::durationBeats)),
                                    static_cast<float>(static_cast<double>(
                                        note->getProperty(jsonKeys::velocity))));
                    }
                    if (const auto* points = savedClip->getProperty(
                            "gainAutomation").getArray()) {
                        for (const auto& pointValue : *points) {
                            const auto* point
                                = pointValue.getDynamicObject();
                            if (point == nullptr)
                                continue;
                            const auto pointId
                                = audio_.addMidiClipAutomationPoint(
                                    trackId, clipId,
                                    static_cast<double>(
                                        point->getProperty(jsonKeys::time)),
                                    static_cast<float>(static_cast<double>(
                                        point->getProperty(jsonKeys::valueDb))));
                            if (pointId != 0 && point->hasProperty(
                                    juce::Identifier("curve")))
                                (void) audio_
                                    .setMidiClipAutomationPointCurve(
                                        trackId, clipId, pointId,
                                        static_cast<
                                            AudioTrackController::
                                                AutomationCurve>(
                                            juce::jlimit(
                                                0, 1,
                                                static_cast<int>(
                                                    point->getProperty(
                                                        "curve")))),
                                        point->hasProperty(
                                            juce::Identifier(
                                                "curveAmount"))
                                            ? static_cast<float>(
                                                  static_cast<double>(
                                                      point->getProperty(
                                                          "curveAmount")))
                                            : 0.0f);
                        }
                    }
                }
            }
            if (audio_.midiTracks().back().clips.empty())
                (void) audio_.addMidiClip(trackId, tr("midi.clip") + " 1");
            audio_.setMidiTrackMixer(
                trackId,
                static_cast<float>(static_cast<double>(savedTrack->getProperty(jsonKeys::gainDb))),
                static_cast<float>(static_cast<double>(savedTrack->getProperty(jsonKeys::pan))),
                static_cast<bool>(savedTrack->getProperty(jsonKeys::mute)),
                static_cast<bool>(savedTrack->getProperty(jsonKeys::solo)));
            if (const auto* automation = savedTrack->getProperty(
                    "gainAutomation").getArray()) {
                for (const auto& value : *automation)
                    if (const auto* point = value.getDynamicObject()) {
                        const auto pointId = audio_.addMidiAutomationPoint(
                            trackId,
                            static_cast<double>(
                                point->getProperty(jsonKeys::time)),
                            static_cast<float>(static_cast<double>(
                                point->getProperty(jsonKeys::valueDb))));
                        if (pointId != 0 && point->hasProperty(
                                juce::Identifier("curve")))
                            (void) audio_.setMidiAutomationPointCurve(
                                trackId, pointId,
                                static_cast<AudioTrackController::AutomationCurve>(
                                    juce::jlimit(0, 1, static_cast<int>(
                                        point->getProperty(jsonKeys::curve)))),
                                point->hasProperty(juce::Identifier(
                                    "curveAmount"))
                                    ? static_cast<float>(
                                          static_cast<double>(
                                              point->getProperty(
                                                  "curveAmount")))
                                    : 0.0f);
                    }
            }
            setMidiTrackNodeGraphState(trackId,
                                       savedTrack->getProperty(jsonKeys::nodeGraph));
            if (static_cast<bool>(savedTrack->getProperty(jsonKeys::armed)))
                savedArmedTrackId = trackId;
        }
    } else {
        const auto trackId = audio_.addMidiTrack(tr("midi.instrument") + " 1");
        const auto savedMidiNotes = state->getProperty(jsonKeys::midiNotes);
        if (const auto* notes = savedMidiNotes.getArray()) {
            for (const auto& value : *notes)
                if (const auto* note = value.getDynamicObject())
                    (void) audio_.addMidiNote(
                        static_cast<int>(note->getProperty(jsonKeys::note)),
                        static_cast<double>(note->getProperty(jsonKeys::startBeats)),
                        static_cast<double>(note->getProperty(jsonKeys::durationBeats)),
                        static_cast<float>(static_cast<double>(note->getProperty(jsonKeys::velocity))));
        }
        ensureMidiNodeGraphState(trackId);
        savedArmedTrackId = trackId;
    }
    loadStage("midi");
    if (audio_.midiTracks().empty())
        savedArmedTrackId = audio_.addMidiTrack(tr("midi.instrument") + " 1");
    if (savedArmedTrackId == 0)
        savedArmedTrackId = audio_.midiTracks().front().id;
    (void) audio_.setLiveMidiTrack(savedArmedTrackId);

    if (state->hasProperty(jsonKeys::loopRangeExplicit)
        && static_cast<bool>(state->getProperty(jsonKeys::loopRangeExplicit))) {
        audio_.setLoopRange(
            static_cast<double>(state->getProperty(jsonKeys::loopStart)),
            static_cast<double>(state->getProperty(jsonKeys::loopEnd)));
    } else {
        audio_.resetLoopRange();
    }
    loopButton_.setToggleState(static_cast<bool>(state->getProperty(jsonKeys::loop)), juce::dontSendNotification);
    audio_.setLooping(loopButton_.getToggleState());
    audio_.setPosition(static_cast<double>(state->getProperty(jsonKeys::position)));
    loadStage("transport");
    if (auto layout = parseLayoutState(state->getProperty(jsonKeys::workspaceLayout)))
        applyLayoutState(*layout);
    loadStage("layout");
    if (audio_.trackCount() > 0) {
        selectTrack(0);
    } else if (selectedMidiTrackId_ != 0) {
        // A project of MIDI tracks alone: the editor shows the current MIDI
        // track's graph. Showing the project-level legacy graph here — a
        // passthrough — and then publishing it wrote that passthrough over
        // the track's instrument, which is how a saved synth came back
        // silent.
        restoreNodeGraphForMidiTrack(selectedMidiTrackId_);
    } else {
        nodeGraph_.restore(graphStateWithSharedModules(legacyNodeGraph));
        setEditorGraphOwner(EditorGraphOwner::none, 0);
        publishNativeDspGraph();
    }
    loadStage("graph");
    refreshMidiEditor();
    // A project without audio tracks still has a current MIDI track once the
    // MIDI editor is refreshed; the inspector followed the absent audio one,
    // so the launcher and the mixer showed a selection it did not describe.
    if (audio_.trackCount() == 0 && selectedMidiTrackId_ != 0) {
        inspectorSelection_ = InspectorSelection::midiTrack;
        updateTrackDescription();
    }
    refreshClipLauncher();
    auto editorMode = static_cast<int>(state->getProperty(jsonKeys::editorMode));
    if (editorMode == 0)
        editorMode = static_cast<bool>(state->getProperty(jsonKeys::midiEditorVisible)) ? 2 : 1;
    setEditorMode(editorMode);
    loadStage("views");
    if (ProjectRecovery::isRecoveryDocument(parsed)) {
        currentProjectFile_ = ProjectRecovery::originalProject(parsed);
        projectDirty_ = true;
        markProjectDirty();
        transientStatus_ = tr("status.projectRecovered");
        transientStatusExpiryMs_ = juce::Time::currentTimeMillis() + 8'000;
    } else {
        currentProjectFile_ = file;
        projectDirty_ = false;
        projectLabel_.setText(
            "CyDAW  /  " + file.getFileNameWithoutExtension(),
            juce::dontSendNotification);
    }
    nextProjectAutosaveMs_ = juce::Time::currentTimeMillis() + 30'000;
    juce::Logger::writeToLog("project load stages: " + loadStages.trim());
}

void WorkspaceComponent::addTrackLane(const std::size_t trackIndex)
{
    ensureTrackNodeGraphState(trackIndex);
    ensureTrackModulationState(trackIndex);
    const auto info = audio_.trackInfo(trackIndex);
    while (trackLaneHeights_.size() <= trackIndex)
        trackLaneHeights_.push_back(88);
    while (trackAutomationVisible_.size() <= trackIndex)
        trackAutomationVisible_.push_back(0);
    while (trackPresentations_.size() <= trackIndex) {
        const auto index = trackPresentations_.size();
        trackPresentations_.push_back({
            tr("launcher.audioTrack", "index",
               juce::String(static_cast<int>(index + 1))),
            defaultTrackColour(index)
        });
    }
    const auto& presentation = trackPresentations_[trackIndex];
    auto lane = std::make_unique<TrackLaneComponent>(audio_.formatManager(), thumbnailCache_);
    lane->configure(trackIndex, info.file, presentation.name,
                    presentation.colour, info.muted, info.solo, info.armed);
    lane->setAutomationVisible(trackAutomationVisible_[trackIndex] != 0);
    lane->setMusicalGrid(audio_.tempo(), arrangementGridDivision(),
                         arrangementSnappingEnabled());
    lane->setSelectionHandler([this](const std::size_t index) { selectTrack(index); });
    lane->setSeekHandler([this](const std::size_t index, const double seconds) {
        selectTrack(index);
        audio_.setPosition(seconds);
    });
    lane->setZoomHandler(
        [this](const double factor, const double anchorSeconds) {
            zoomArrangement(factor, anchorSeconds);
        });
    lane->setPanHandler([this](const double deltaSeconds) {
        panArrangement(deltaSeconds);
    });
    lane->setVerticalPanHandler([this](const int deltaPixels) {
        const auto maximumY = std::max(
            0, trackContainer_.getHeight() - trackViewport_.getHeight());
        trackViewport_.setViewPosition(
            trackViewport_.getViewPositionX(),
            juce::jlimit(0, maximumY,
                         trackViewport_.getViewPositionY() - deltaPixels));
    });
    lane->setMarqueeHandler(
        [this](const std::size_t trackIndex,
               const double anchorSeconds,
               const double currentSeconds, const int screenY,
               const WaveformComponent::MarqueePhase phase,
               const bool additive) {
            handleArrangementMarquee(
                trackIndex, anchorSeconds, currentSeconds,
                screenY, phase, additive);
        });
    lane->setMuteHandler([this](const std::size_t index, const bool muted) {
        const auto state = audio_.trackInfo(index);
        audio_.setTrackMixer(index, state.gainDb, state.pan, muted);
        if (selectedTrack_ == index)
            muteButton_.setToggleState(muted, juce::dontSendNotification);
        refreshMixer();
        markProjectDirty();
    });
    lane->setSoloHandler([this](const std::size_t index, const bool solo) {
        audio_.setTrackSolo(index, solo);
        if (selectedTrack_ == index)
            soloButton_.setToggleState(solo, juce::dontSendNotification);
        refreshMixer();
        markProjectDirty();
    });
    lane->setArmHandler([this](const std::size_t index, const bool armed) {
        if (audio_.isRecording()) {
            refreshAudioTrackArmStates();
            return;
        }
        audio_.setTrackArmed(index, armed);
        refreshAudioTrackArmStates();
        markProjectDirty();
    });
    lane->setRegionSelectionHandler(
        [this](const std::size_t index, const std::uint64_t clipId,
               const WaveformComponent::RegionSelectionMode mode) {
            selectAudioClip(index, clipId, mode);
        });
    lane->setRegionMoveHandler(
        [this](const std::size_t index, const std::uint64_t clipId,
               const double startSeconds, const int screenY) {
            (void) moveArrangementSelection(
                index, clipId, startSeconds, screenY);
        });
    lane->setRegionDragPreviewHandler(
        [this](const std::size_t index, const std::uint64_t clipId,
               const double startSeconds, const int screenY,
               const bool active) {
            showArrangementMovePreview(index, clipId, startSeconds,
                                       screenY, active);
        });
    lane->setRegionEditHandler(
        [this](const std::size_t index, const std::uint64_t clipId,
               const double startSeconds, const double sourceOffsetSeconds,
               const double durationSeconds,
               const double sourceDurationSeconds,
               const double fadeInSeconds,
               const double fadeOutSeconds) {
            if (!audio_.updateAudioClip(index, clipId, startSeconds,
                                        sourceOffsetSeconds, durationSeconds,
                                        fadeInSeconds, fadeOutSeconds,
                                        sourceDurationSeconds))
                return;
            selectedTrack_ = index;
            selectedAudioClipId_ = clipId;
            refreshTimelineRanges();
            refreshSelectedTrack();
            refreshAutomationEditor();
            refreshAudioEditor();
            markProjectDirty();
        });
    lane->setRegionCommandHandler(
        [this](const std::size_t index, const std::uint64_t clipId,
               const WaveformComponent::RegionCommand command,
               const double timelineSeconds) {
            handleAudioClipCommand(index, clipId, command, timelineSeconds);
        });
    lane->setRegionOpenHandler(
        [this](const std::size_t index, const std::uint64_t clipId) {
            launcherEditorContext_.reset();
            selectAudioClip(index, clipId);
            if (const auto clip = audio_.audioClip(index, clipId))
                clipEditorCueSeconds_ = clip->playStartSeconds;
            const auto safe = juce::Component::SafePointer<
                WorkspaceComponent>(this);
            juce::MessageManager::callAsync([safe] {
                if (safe == nullptr)
                    return;
                safe->sourceEditorEventId_ = 0;
                safe->setEditorMode(6);
                safe->refreshClipTimelineEditor();
                if (safe->editorWindow_ != nullptr)
                    safe->editorWindow_->toFront(true);
            });
        });
    lane->setContextMenuHandler(
        [this](const std::size_t index, juce::Component* target) {
            showTrackContextMenu(index, target);
        });
    lane->setAutomationAddHandler(
        [this](const std::size_t index, const double time,
               const float value) {
            (void) audio_.addAutomationPoint(index, time, value);
            refreshTimelineRanges();
            if (selectedTrack_ == index)
                refreshAutomationEditor();
            markProjectDirty();
        });
    lane->setAutomationUpdateHandler(
        [this](const std::size_t index, const std::uint64_t id,
               const double time, const float value) {
            (void) audio_.updateAutomationPoint(index, id, time, value);
            refreshTimelineRanges();
            if (selectedTrack_ == index)
                refreshAutomationEditor();
            markProjectDirty();
        });
    lane->setAutomationRemoveHandler(
        [this](const std::size_t index, const std::uint64_t id) {
            (void) audio_.removeAutomationPoint(index, id);
            refreshTimelineRanges();
            if (selectedTrack_ == index)
                refreshAutomationEditor();
            markProjectDirty();
        });
    lane->setAutomationVisibilityHandler(
        [this](const std::size_t index, const bool visible) {
            if (index >= trackAutomationVisible_.size()
                || index >= trackLaneHeights_.size())
                return;
            trackAutomationVisible_[index] = visible ? 1 : 0;
            trackLaneHeights_[index] = visible
                                           ? std::max(172,
                                               trackLaneHeights_[index])
                                           : std::min(96,
                                               trackLaneHeights_[index]);
            layoutTrackLanes();
            markProjectDirty();
        });
    lane->setLaneHeightHandler(
        [this](const std::size_t index, const int height) {
            if (index >= trackLaneHeights_.size())
                return;
            trackLaneHeights_[index] = juce::jlimit(82, 420, height);
            layoutTrackLanes();
        });
    lane->setTrackReorderHandler(
        [this](const std::size_t fromIndex, const int screenY,
               const bool active) {
            for (auto& candidate : trackLanes_)
                candidate->setTrackDropTarget(false);
            if (trackLanes_.empty() || audio_.isRecording())
                return;
            const auto target = arrangementTrackAtScreenY(screenY);
            if (target < trackLanes_.size() && active) {
                trackLanes_[target]->setTrackDropTarget(true);
                return;
            }
            if (!active && target < trackLanes_.size()
                && target != fromIndex)
                moveArrangementTrack(fromIndex, target);
        });
    lane->setAudioDropHandler(
        [this](const std::size_t index, const juce::File& source,
               const double startSeconds) {
            std::uint64_t clipId {};
            const auto result = audio_.addAudioClipFromFile(
                index, source, snapArrangementTime(startSeconds), &clipId);
            if (result.failed()) {
                showError(tr("error.openAudioTitle"),
                          result.getErrorMessage());
                return;
            }
            selectAudioClip(index, clipId);
            refreshTimelineRanges();
            refreshAudioEditor();
            refreshAutomationEditor();
            markProjectDirty();
        });
    trackContainer_.addAndMakeVisible(*lane);
    registerShortcutListener(*lane);
    trackLanes_.push_back(std::move(lane));
    refreshArrangementInteractionMode();
    emptyTimelineLabel_.setVisible(false);
    layoutTrackLanes();
    refreshTimelineRanges();
}

void WorkspaceComponent::clearTrackLanes()
{
    for (auto& lane : trackLanes_)
        trackContainer_.removeChildComponent(lane.get());
    trackLanes_.clear();
    trackLaneHeights_.clear();
    trackAutomationVisible_.clear();
    trackPresentations_.clear();
    trackNodeGraphStates_.clear();
    sharedNodeModules_ = juce::var {};
    if (const auto* factory = factoryNodeGraphState_.getDynamicObject())
        sharedNodeModules_ = cloneJsonValue(factory->getProperty(jsonKeys::modules));
    nodeGraph_.restore(factoryNodeGraphState_);
    // Nobody's selection: the owner it had belonged to a track just
    // cleared, and its stored state is gone.
    setEditorGraphOwner(EditorGraphOwner::none, 0);
    nodeGraph_.setContextName({});
    selectedTrack_.reset();
    selectedAudioClipId_ = 0;
    selectedAudioClipIds_.clear();
    selectedMidiTrackId_ = 0;
    selectedMidiClipId_ = 0;
    selectedClipEventId_ = 0;
    launcherInspectorSelection_.reset();
    inspectorSelection_ = InspectorSelection::none;
    clearArrangementMarqueeSelection();
    selectedPluginIndex_ = 0;
    automationTargetId_ = "gain";
    emptyTimelineLabel_.setVisible(true);
    layoutTrackLanes();
    refreshSelectedTrack();
    refreshAutomationTargets();
    refreshClipLauncher();
}

void WorkspaceComponent::rebuildTrackLanes()
{
    for (auto& lane : trackLanes_)
        trackContainer_.removeChildComponent(lane.get());
    trackLanes_.clear();
    for (std::size_t index = 0; index < audio_.trackCount(); ++index)
        addTrackLane(index);
    refreshTimelineRanges();
    refreshAudioTrackArmStates();
}

void WorkspaceComponent::moveArrangementTrack(
    const std::size_t fromIndex, const std::size_t toIndex)
{
    if (fromIndex >= audio_.trackCount() || toIndex >= audio_.trackCount()
        || fromIndex == toIndex || audio_.isRecording())
        return;
    saveCurrentNodeGraphState();
    if (!audio_.moveAudioTrack(fromIndex, toIndex))
        return;

    const auto moveItem = [fromIndex, toIndex](auto& values) {
        if (fromIndex >= values.size() || toIndex >= values.size())
            return;
        auto value = std::move(values[fromIndex]);
        values.erase(values.begin()
                     + static_cast<std::ptrdiff_t>(fromIndex));
        values.insert(values.begin()
                          + static_cast<std::ptrdiff_t>(toIndex),
                      std::move(value));
    };
    moveItem(trackPresentations_);
    moveItem(trackNodeGraphStates_);
    moveItem(trackModulationStates_);
    moveItem(trackLaneHeights_);
    moveItem(trackAutomationVisible_);

    const auto remap = [fromIndex, toIndex](const std::size_t index) {
        if (index == fromIndex)
            return toIndex;
        if (fromIndex < toIndex && index > fromIndex && index <= toIndex)
            return index - 1;
        if (fromIndex > toIndex && index >= toIndex && index < fromIndex)
            return index + 1;
        return index;
    };
    if (selectedTrack_)
        selectedTrack_ = remap(*selectedTrack_);
    if (launcherEditorContext_)
        launcherEditorContext_->trackIndex = remap(
            launcherEditorContext_->trackIndex);
    if (launcherInspectorSelection_)
        launcherInspectorSelection_->trackIndex = remap(
            launcherInspectorSelection_->trackIndex);
    for (auto& action : launcherActions_)
        action.trackIndex = remap(action.trackIndex);

    const auto selectedAfterMove = selectedTrack_;
    rebuildTrackLanes();
    if (selectedAfterMove && *selectedAfterMove < audio_.trackCount())
        selectTrack(*selectedAfterMove);
    refreshClipLauncher();
    markProjectDirty();
}

void WorkspaceComponent::moveLauncherTrack(
    const std::size_t fromIndex, const std::size_t toIndex)
{
    if (fromIndex == toIndex || audio_.isRecording())
        return;
    const auto audioCount = audio_.trackCount();
    const auto midiCount = audio_.midiTracks().size();
    if (fromIndex < audioCount && toIndex < audioCount) {
        moveArrangementTrack(fromIndex, toIndex);
        return;
    }
    if (fromIndex < audioCount || toIndex < audioCount)
        return;
    const auto fromMidi = fromIndex - audioCount;
    const auto toMidi = toIndex - audioCount;
    if (fromMidi >= midiCount || toMidi >= midiCount
        || !audio_.moveMidiTrack(fromMidi, toMidi))
        return;
    refreshClipLauncher();
    updateTrackDescription();
    markProjectDirty();
}

void WorkspaceComponent::showTrackContextMenu(
    const std::size_t trackIndex, juce::Component* target)
{
    if (trackIndex >= audio_.trackCount()
        || trackIndex >= trackPresentations_.size())
        return;

    selectTrack(trackIndex);
    const auto info = audio_.trackInfo(trackIndex);
    juce::PopupMenu menu;
    menu.addSectionHeader(trackPresentations_[trackIndex].name);
    menu.addItem(1, tr("track.rename"));

    juce::PopupMenu colours;
    static constexpr std::array<const char*, 8> colourKeys {
        "colour.orange", "colour.blue", "colour.purple", "colour.green",
        "colour.yellow", "colour.cyan", "colour.red", "colour.violet"
    };
    for (std::size_t index = 0; index < trackAccentColours.size(); ++index)
        colours.addItem(100 + static_cast<int>(index),
                        tr(colourKeys[index]), true,
                        trackPresentations_[trackIndex].colour
                            == juce::Colour(trackAccentColours[index]));
    menu.addSubMenu(tr("track.colour"), colours);
    menu.addSeparator();
    menu.addSectionHeader(tr("track.configuration"));
    menu.addItem(2, tr("track.mute"), true, info.muted);
    menu.addItem(3, tr("track.solo"), true, info.solo);
    menu.addItem(4, tr("track.arm"), !audio_.isRecording(), info.armed);
    menu.addSeparator();
    juce::PopupMenu channelLayout;
    const auto currentChannels = trackPresentations_[trackIndex].audioChannels;
    channelLayout.addItem(20, tr("track.channelsMono"), true,
                          currentChannels == 1);
    channelLayout.addItem(21, tr("track.channelsStereo"), true,
                          currentChannels == 2);
    channelLayout.addItem(22, tr("track.channelsCount", "count", "4"), true,
                          currentChannels == 4);
    channelLayout.addItem(23, tr("track.channelsCount", "count", "6"), true,
                          currentChannels == 6);
    channelLayout.addItem(24, tr("track.channelsCount", "count", "8"), true,
                          currentChannels == 8);
    menu.addSubMenu(tr("track.channelLayout"), channelLayout);
    menu.addItem(5, tr("track.typeAudio"), false, false);
    menu.addItem(6, tr("track.routeMaster"), false, false);

    const juce::Component::SafePointer<WorkspaceComponent> safeThis(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(target),
        [safeThis, trackIndex](const int result) {
            auto* const workspace = safeThis.getComponent();
            if (workspace == nullptr || result == 0
                || trackIndex >= workspace->audio_.trackCount()
                || trackIndex >= workspace->trackPresentations_.size())
                return;
            if (result == 1) {
                workspace->promptRenameTrack(trackIndex);
                return;
            }
            if (result >= 100 && result < 108) {
                auto& presentation
                    = workspace->trackPresentations_[trackIndex];
                presentation.colour = juce::Colour(
                    trackAccentColours[static_cast<std::size_t>(result - 100)]);
                if (trackIndex < workspace->trackLanes_.size())
                    workspace->trackLanes_[trackIndex]->setPresentation(
                        presentation.name, presentation.colour);
                workspace->refreshClipLauncher();
                workspace->markProjectDirty();
                return;
            }
            if (result >= 20 && result <= 24) {
                static constexpr std::array<int, 5> channelCounts {
                    1, 2, 4, 6, 8
                };
                auto& presentation
                    = workspace->trackPresentations_[trackIndex];
                presentation.audioChannels = channelCounts[
                    static_cast<std::size_t>(result - 20)];
                workspace->saveCurrentNodeGraphState();
                workspace->applyTrackIoToGraphState(trackIndex);
                workspace->restoreNodeGraphForTrack(trackIndex);
                workspace->refreshNodeDeviceSurface();
                workspace->markProjectDirty();
                return;
            }

            const auto state = workspace->audio_.trackInfo(trackIndex);
            if (result == 2)
                workspace->audio_.setTrackMixer(
                    trackIndex, state.gainDb, state.pan, !state.muted);
            else if (result == 3)
                workspace->audio_.setTrackSolo(trackIndex, !state.solo);
            else if (result == 4)
                workspace->audio_.setTrackArmed(trackIndex, !state.armed);
            else
                return;
            workspace->refreshAudioTrackArmStates();
            workspace->refreshSelectedTrack();
            workspace->refreshClipLauncher();
            workspace->markProjectDirty();
        });
}

void WorkspaceComponent::promptRenameTrack(const std::size_t trackIndex)
{
    if (trackIndex >= trackPresentations_.size())
        return;
    auto* dialog = new juce::AlertWindow(
        tr("track.renameTitle"), tr("track.renamePrompt"),
        juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor("name", trackPresentations_[trackIndex].name,
                          tr("track.name"));
    dialog->addButton(tr("track.rename"), 1,
                      juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton(tr("layout.cancel"), 0,
                      juce::KeyPress(juce::KeyPress::escapeKey));
    const juce::Component::SafePointer<WorkspaceComponent> safeThis(this);
    dialog->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [safeThis, dialog, trackIndex](const int result) {
                auto* const workspace = safeThis.getComponent();
                if (workspace == nullptr || result != 1
                    || trackIndex >= workspace->trackPresentations_.size())
                    return;
                const auto name
                    = dialog->getTextEditorContents("name").trim();
                if (name.isEmpty())
                    return;
                auto& presentation
                    = workspace->trackPresentations_[trackIndex];
                presentation.name = name;
                if (trackIndex < workspace->trackLanes_.size())
                    workspace->trackLanes_[trackIndex]->setPresentation(
                        presentation.name, presentation.colour);
                workspace->refreshClipLauncher();
                workspace->updateTrackDescription();
                workspace->markProjectDirty();
            }),
        true);
}

void WorkspaceComponent::layoutTrackLanes()
{
    constexpr int laneGap = 2;
    constexpr int masterHeight = 54;
    const auto contentWidth = juce::jmax(520, trackViewport_.getMaximumVisibleWidth());
    auto requiredHeight = masterHeight + 16;
    for (std::size_t index = 0; index < trackLanes_.size(); ++index)
        requiredHeight += (index < trackLaneHeights_.size()
                               ? trackLaneHeights_[index]
                               : 88)
                          + laneGap;
    const auto contentHeight = juce::jmax(trackViewport_.getHeight(),
                                         requiredHeight);
    trackContainer_.setSize(contentWidth, contentHeight);
    emptyTimelineLabel_.setBounds(trackContainer_.getLocalBounds().reduced(20));

    int y = 4;
    for (std::size_t index = 0; index < trackLanes_.size(); ++index) {
        auto& lane = trackLanes_[index];
        const auto laneHeight = index < trackLaneHeights_.size()
                                    ? trackLaneHeights_[index]
                                    : 88;
        lane->setBounds(4, y, contentWidth - 8, laneHeight);
        y += laneHeight + laneGap;
    }
    const auto masterY = std::max(y + 4,
                                  contentHeight - masterHeight - 4);
    masterLane_.setBounds(4, masterY, contentWidth - 8, masterHeight);
    updateArrangementPlayhead(audio_.position());
}

void WorkspaceComponent::updateArrangementPlayhead(const double position)
{
    if (trackLanes_.empty()) {
        arrangementPlayhead_.setPosition(-1, 0);
        return;
    }
    const auto& lane = *trackLanes_.front();
    const auto x = lane.playheadXForTime(position);
    arrangementPlayhead_.setPosition(x < 0 ? -1 : lane.getX() + x,
                                     trackContainer_.getHeight());
}

void WorkspaceComponent::refreshTimelineRangesIfLengthChanged()
{
    const auto contentLength = std::max(1.0, audio_.length());
    const auto barDuration = 60.0 / std::max(1.0, audio_.tempo()) * 4.0;
    const auto editingTail = std::max(barDuration * 2.0,
                                      contentLength * 0.15);
    if (std::abs(contentLength + editingTail
                 - arrangementTimelineLengthSeconds_) < 1.0e-6)
        return;
    refreshTimelineRanges();
}

void WorkspaceComponent::refreshTimelineRanges()
{
    const auto contentLength = std::max(1.0, audio_.length());
    const auto barDuration = 60.0 / std::max(1.0, audio_.tempo()) * 4.0;
    // Keep a useful drop/stretch area after the last event. Without this tail,
    // a clip ending at the project boundary has no handle room on the right.
    const auto editingTail = std::max(barDuration * 2.0,
                                      contentLength * 0.15);
    const auto timelineLength = contentLength + editingTail;
    arrangementTimelineLengthSeconds_ = timelineLength;
    timelineRuler_.setTimeline(timelineLength, audio_.tempo());
    timelineRuler_.setLoopRange(audio_.loopStart(), audio_.loopEnd(),
                                loopButton_.getToggleState());
    timelineRuler_.setTimeSelection(
        arrangementTimeSelectionStartSeconds_,
        arrangementTimeSelectionEndSeconds_,
        arrangementMarqueeSelectionActive_);
    arrangementRangeButton_.setEnabled(
        arrangementMarqueeSelectionActive_
        && arrangementMarqueeClipCount() > 0
        && arrangementTimeSelectionEndSeconds_
                   - arrangementTimeSelectionStartSeconds_
               >= 0.001);
    for (std::size_t index = 0; index < trackLanes_.size() && index < audio_.trackCount(); ++index) {
        const auto info = audio_.trackInfo(index);
        std::vector<WaveformComponent::Region> regions;
        regions.reserve(info.clips.size());
        for (const auto& clip : info.clips)
            regions.push_back({ clip.id, clip.name, clip.startSeconds,
                                clip.sourceOffsetSeconds,
                                clip.durationSeconds,
                                clip.sourceDurationSeconds,
                                juce::Colour(clip.colour),
                                clip.sourceFile,
                                clip.sourceLengthSeconds,
                                clip.fadeInSeconds,
                                clip.fadeOutSeconds,
                                clip.activeTakeIndex,
                                clip.takes.size() });
        trackLanes_[index]->setMusicalGrid(
            audio_.tempo(), arrangementGridDivision(),
            arrangementSnappingEnabled());
        const auto selectedIds = arrangementMarqueeSelectionActive_
                                     && index
                                            < arrangementMarqueeClipIdsByTrack_
                                                  .size()
                                 ? arrangementMarqueeClipIdsByTrack_[index]
                                 : selectedTrack_ == index
                                       ? selectedAudioClipIds_
                                       : std::vector<std::uint64_t> {};
        const auto primaryId = selectedTrack_ == index
                                       && std::find(
                                              selectedIds.begin(),
                                              selectedIds.end(),
                                              selectedAudioClipId_)
                                              != selectedIds.end()
                                   ? selectedAudioClipId_
                                   : 0;
        trackLanes_[index]->setTimelineRegions(
            timelineLength, std::move(regions), selectedIds,
            primaryId);
        trackLanes_[index]->setTimeSelection(
            arrangementTimeSelectionStartSeconds_,
            arrangementTimeSelectionEndSeconds_,
            arrangementMarqueeSelectionActive_
                && index >= arrangementTimeSelectionFirstTrack_
                && index <= arrangementTimeSelectionLastTrack_,
            arrangementMarqueeSelectionActive_);
        std::vector<AutomationEditorComponent::Point> automationPoints;
        const auto automation = audio_.automationPoints(index);
        automationPoints.reserve(automation.size());
        for (const auto& point : automation)
            automationPoints.push_back(
                { point.id, point.timeSeconds, point.valueDb });
        trackLanes_[index]->setAutomationPoints(
            std::move(automationPoints), timelineLength,
            arrangementViewStartSeconds_,
            arrangementViewStartSeconds_
                + timelineLength / arrangementZoom_);
    }
    refreshArrangementView();
}

void WorkspaceComponent::refreshArrangementView()
{
    noteUiHitchEvent("arrangement-refresh");
    const auto timelineLength = std::max(
        1.0, arrangementTimelineLengthSeconds_);
    arrangementZoom_ = juce::jlimit(1.0, 128.0,
                                    arrangementZoom_);
    const auto visibleDuration = timelineLength / arrangementZoom_;
    const auto maximumStart = std::max(
        0.0, timelineLength - visibleDuration);
    arrangementViewStartSeconds_ = juce::jlimit(
        0.0, maximumStart, arrangementViewStartSeconds_);
    const auto visibleEnd = std::min(
        timelineLength,
        arrangementViewStartSeconds_ + visibleDuration);
    timelineRuler_.setVisibleRange(arrangementViewStartSeconds_,
                                   visibleEnd);
    for (auto& lane : trackLanes_)
        lane->setVisibleRange(arrangementViewStartSeconds_, visibleEnd);
    timelineScrollBar_.setRangeLimits(0.0, timelineLength);
    timelineScrollBar_.setCurrentRange(
        arrangementViewStartSeconds_, visibleDuration,
        juce::dontSendNotification);
    timelineScrollBar_.setSingleStepSize(
        std::max(0.001, visibleDuration / 16.0));
    timelineZoomOutButton_.setEnabled(arrangementZoom_ > 1.000001);
    timelineFitButton_.setEnabled(arrangementZoom_ > 1.000001
                                  || arrangementViewStartSeconds_ > 0.000001);
    timelineFitButton_.setButtonText(
        arrangementZoom_ > 1.000001
            ? juce::String(arrangementZoom_, 1) + "x"
            : tr("timeline.fitShort"));
    timelineZoomInButton_.setEnabled(arrangementZoom_ < 127.999);
}

void WorkspaceComponent::zoomArrangement(const double factor,
                                          const double anchorSeconds)
{
    if (!std::isfinite(factor) || factor <= 0.0)
        return;
    const auto timelineLength = std::max(
        1.0, arrangementTimelineLengthSeconds_);
    const auto oldDuration = timelineLength / arrangementZoom_;
    const auto anchor = juce::jlimit(
        arrangementViewStartSeconds_,
        arrangementViewStartSeconds_ + oldDuration,
        anchorSeconds);
    const auto anchorAmount = juce::jlimit(
        0.0, 1.0,
        (anchor - arrangementViewStartSeconds_)
            / std::max(0.000001, oldDuration));
    const auto newZoom = juce::jlimit(
        1.0, 128.0, arrangementZoom_ * factor);
    const auto newDuration = timelineLength / newZoom;
    arrangementZoom_ = newZoom;
    arrangementViewStartSeconds_ = anchor
                                   - anchorAmount * newDuration;
    refreshArrangementView();
}

void WorkspaceComponent::panArrangement(const double deltaSeconds)
{
    if (!std::isfinite(deltaSeconds))
        return;
    arrangementViewStartSeconds_ += deltaSeconds;
    refreshArrangementView();
}

void WorkspaceComponent::fitArrangement()
{
    arrangementZoom_ = 1.0;
    arrangementViewStartSeconds_ = 0.0;
    refreshArrangementView();
}

void WorkspaceComponent::scrollBarMoved(juce::ScrollBar* scrollBar,
                                        const double newRangeStart)
{
    if (scrollBar != &timelineScrollBar_)
        return;
    arrangementViewStartSeconds_ = newRangeStart;
    refreshArrangementView();
}

void WorkspaceComponent::refreshMidiEditor()
{
    noteUiHitchEvent("midi-editor-refresh");
    auto tracks = audio_.midiTracks();
    if (tracks.empty()) {
        selectedMidiTrackId_ = audio_.addMidiTrack(tr("midi.instrument") + " 1");
        tracks = audio_.midiTracks();
    }

    const auto selectedTrack = std::find_if(tracks.begin(), tracks.end(), [this](const auto& track) {
        return track.id == selectedMidiTrackId_;
    });
    const auto& activeTrack = selectedTrack != tracks.end() ? *selectedTrack : tracks.front();
    selectedMidiTrackId_ = activeTrack.id;
    if (activeTrack.clips.empty()) {
        selectedMidiClipId_ = audio_.addMidiClip(
            activeTrack.id, tr("midi.clip") + " 1");
        tracks = audio_.midiTracks();
    }

    const auto refreshedTrack = std::find_if(tracks.begin(), tracks.end(), [this](const auto& track) {
        return track.id == selectedMidiTrackId_;
    });
    if (refreshedTrack == tracks.end())
        return;
    const auto selectedClip = std::find_if(refreshedTrack->clips.begin(), refreshedTrack->clips.end(),
                                           [this](const auto& clip) {
                                               return clip.id == selectedMidiClipId_;
                                           });
    const auto& activeClip = selectedClip != refreshedTrack->clips.end()
                                 ? *selectedClip : refreshedTrack->clips.front();
    selectedMidiClipId_ = activeClip.id;
    (void) audio_.setLiveMidiTrack(selectedMidiTrackId_);

    std::vector<PianoRollComponent::ContextItem> trackItems;
    trackItems.reserve(tracks.size());
    for (const auto& track : tracks)
        trackItems.push_back({ track.id, track.name });
    std::vector<PianoRollComponent::ContextItem> clipItems;
    clipItems.reserve(refreshedTrack->clips.size());
    for (const auto& clip : refreshedTrack->clips)
        clipItems.push_back({ clip.id, clip.name });
    pianoRoll_.setContext(std::move(trackItems), selectedMidiTrackId_,
                          std::move(clipItems), selectedMidiClipId_,
                          activeClip.startBeats, activeClip.lengthBeats,
                          activeClip.playStartBeats,
                          activeClip.playEndBeats,
                          activeClip.loopStartBeats,
                          activeClip.loopEndBeats,
                          activeClip.looping,
                           refreshedTrack->gainDb, refreshedTrack->pan,
                           refreshedTrack->muted, refreshedTrack->solo);
    pianoRoll_.setNotes(activeClip.notes);
    const auto secondsPerBeat = 60.0 / std::max(20.0, audio_.tempo());
    const auto playStartSeconds = activeClip.playStartBeats
                                  * secondsPerBeat;
    const auto playEndSeconds = (activeClip.playEndBeats
                                         > activeClip.playStartBeats
                                     ? activeClip.playEndBeats
                                     : activeClip.lengthBeats)
                                * secondsPerBeat;
    clipEditorCueSeconds_ = juce::jlimit(
        playStartSeconds, playEndSeconds, clipEditorCueSeconds_);
    const auto previewing = audio_.isMidiClipEditorPreviewing(
        selectedMidiTrackId_, selectedMidiClipId_);
    const auto previewPosition
        = audio_.midiClipEditorPreviewPositionSeconds(selectedMidiTrackId_);
    if (previewPosition >= 0.0)
        clipEditorCueSeconds_ = previewPosition;
    pianoRoll_.setPlayhead(clipEditorCueSeconds_);
    pianoRoll_.setTransportState(previewing);
    pianoRoll_.setRecordingState(
        audio_.isMidiRecording(), audio_.midiRecordingNoteCount(),
        audio_.droppedMidiRecordingEvents());
    pianoRoll_.setHistoryState(audio_.canUndoMidiEdit(),
                               audio_.canRedoMidiEdit());
    pianoRoll_.setTempo(audio_.tempo());
    tempoSlider_.setValue(audio_.tempo(), juce::dontSendNotification);
    if (currentEditorMode_ == 2) {
        refreshMidiTrackMixerControls();
        armButton_.setToggleState(activeTrack.armed,
                                  juce::dontSendNotification);
        updateTrackDescription();
        refreshBottomAutomationEditor();
    }
}

void WorkspaceComponent::refreshAutomationTargets()
{
    std::vector<AutomationEditorComponent::Target> targets;
    targets.push_back({ "gain", tr("automation.trackGain"), -60.0f, 12.0f, " dB" });
    if (selectedTrack_ && *selectedTrack_ < audio_.trackCount()) {
        const auto info = audio_.trackInfo(*selectedTrack_);
        for (std::size_t pluginIndex = 0; pluginIndex < info.plugins.size(); ++pluginIndex) {
            std::vector<IsolatedPluginHost::ParameterSnapshot> parameters;
            if (audio_.refreshTrackPluginParameters(*selectedTrack_, pluginIndex,
                                                    parameters).failed())
                continue;
            for (const auto& parameter : parameters) {
                if (!parameter.automatable)
                    continue;
                auto name = info.plugins[pluginIndex].name + "  /  " + parameter.name;
                if (parameter.label.isNotEmpty())
                    name += " (" + parameter.label + ")";
                targets.push_back({ pluginAutomationTargetId(pluginIndex, parameter.index),
                                    std::move(name), 0.0f, 100.0f, " %" });
            }
        }
    }

    const auto selectedExists = std::any_of(targets.begin(), targets.end(), [this](const auto& target) {
        return target.id == automationTargetId_;
    });
    if (!selectedExists)
        automationTargetId_ = "gain";
    automationEditor_.setTargets(std::move(targets), automationTargetId_);
    refreshAutomationEditor();
    refreshBottomAutomationEditor();
}

void WorkspaceComponent::refreshAutomationEditor()
{
    std::vector<AutomationEditorComponent::Point> points;
    auto duration = std::max(1.0, audio_.length());
    if (selectedTrack_ && *selectedTrack_ < audio_.trackCount()) {
        duration = std::max(1.0, audio_.trackInfo(*selectedTrack_).lengthSeconds);
        std::size_t pluginIndex {};
        std::uint32_t parameterIndex {};
        if (parsePluginAutomationTarget(automationTargetId_, pluginIndex, parameterIndex)) {
            const auto automation = audio_.pluginAutomationPoints(*selectedTrack_, pluginIndex,
                                                                  parameterIndex);
            points.reserve(automation.size());
            for (const auto& point : automation)
                points.push_back({ point.id, point.timeSeconds,
                                   point.normalisedValue * 100.0f,
                                   static_cast<int>(point.curve),
                                   point.curveAmount });
        } else {
            const auto automation = audio_.automationPoints(*selectedTrack_);
            points.reserve(automation.size());
            for (const auto& point : automation)
                points.push_back({ point.id, point.timeSeconds, point.valueDb,
                                   static_cast<int>(point.curve),
                                   point.curveAmount });
        }
    }
    automationEditor_.setPoints(std::move(points), duration);
}

bool WorkspaceComponent::usesClipLocalAutomation() const noexcept
{
    return (currentEditorMode_ == 2 && selectedMidiTrackId_ != 0
            && selectedMidiClipId_ != 0)
           || (currentEditorMode_ == 6
               && (launcherEditorContext_.has_value()
                   || (selectedTrack_ && selectedAudioClipId_ != 0)));
}

std::uint64_t WorkspaceComponent::addContextAutomationPoint(
    const double timeSeconds, const float value)
{
    if (currentEditorMode_ == 2 && selectedMidiTrackId_ != 0
        && selectedMidiClipId_ != 0)
        return audio_.addMidiClipAutomationPoint(
            selectedMidiTrackId_, selectedMidiClipId_, timeSeconds, value);
    if (currentEditorMode_ == 6 && launcherEditorContext_) {
        const auto context = *launcherEditorContext_;
        return audio_.addLauncherClipAutomationPoint(
            context.trackIndex, context.sceneIndex, timeSeconds, value);
    }
    if (currentEditorMode_ == 6 && selectedTrack_
        && selectedAudioClipId_ != 0)
        return audio_.addAudioClipAutomationPoint(
            *selectedTrack_, selectedAudioClipId_, timeSeconds, value);
    if (!selectedTrack_)
        return 0;
    std::size_t pluginIndex {};
    std::uint32_t parameterIndex {};
    if (parsePluginAutomationTarget(automationTargetId_, pluginIndex,
                                    parameterIndex))
        return audio_.addPluginAutomationPoint(
            *selectedTrack_, pluginIndex, parameterIndex, {},
            timeSeconds + contextualAutomationOffsetSeconds_,
            value / 100.0f);
    return audio_.addAutomationPoint(
        *selectedTrack_, timeSeconds + contextualAutomationOffsetSeconds_,
        value);
}

bool WorkspaceComponent::updateContextAutomationPoint(
    const std::uint64_t id, const double timeSeconds, const float value)
{
    if (currentEditorMode_ == 2 && selectedMidiTrackId_ != 0
        && selectedMidiClipId_ != 0)
        return audio_.updateMidiClipAutomationPoint(
            selectedMidiTrackId_, selectedMidiClipId_, id, timeSeconds,
            value);
    if (currentEditorMode_ == 6 && launcherEditorContext_) {
        const auto context = *launcherEditorContext_;
        return audio_.updateLauncherClipAutomationPoint(
            context.trackIndex, context.sceneIndex, id, timeSeconds, value);
    }
    if (currentEditorMode_ == 6 && selectedTrack_
        && selectedAudioClipId_ != 0)
        return audio_.updateAudioClipAutomationPoint(
            *selectedTrack_, selectedAudioClipId_, id, timeSeconds, value);
    if (!selectedTrack_)
        return false;
    std::size_t pluginIndex {};
    std::uint32_t parameterIndex {};
    if (parsePluginAutomationTarget(automationTargetId_, pluginIndex,
                                    parameterIndex))
        return audio_.updatePluginAutomationPoint(
            *selectedTrack_, pluginIndex, parameterIndex, id,
            timeSeconds + contextualAutomationOffsetSeconds_,
            value / 100.0f);
    return audio_.updateAutomationPoint(
        *selectedTrack_, id,
        timeSeconds + contextualAutomationOffsetSeconds_, value);
}

bool WorkspaceComponent::removeContextAutomationPoint(
    const std::uint64_t id)
{
    if (currentEditorMode_ == 2 && selectedMidiTrackId_ != 0
        && selectedMidiClipId_ != 0)
        return audio_.removeMidiClipAutomationPoint(
            selectedMidiTrackId_, selectedMidiClipId_, id);
    if (currentEditorMode_ == 6 && launcherEditorContext_) {
        const auto context = *launcherEditorContext_;
        return audio_.removeLauncherClipAutomationPoint(
            context.trackIndex, context.sceneIndex, id);
    }
    if (currentEditorMode_ == 6 && selectedTrack_
        && selectedAudioClipId_ != 0)
        return audio_.removeAudioClipAutomationPoint(
            *selectedTrack_, selectedAudioClipId_, id);
    if (!selectedTrack_)
        return false;
    std::size_t pluginIndex {};
    std::uint32_t parameterIndex {};
    if (parsePluginAutomationTarget(automationTargetId_, pluginIndex,
                                    parameterIndex))
        return audio_.removePluginAutomationPoint(
            *selectedTrack_, pluginIndex, parameterIndex, id);
    return audio_.removeAutomationPoint(*selectedTrack_, id);
}

bool WorkspaceComponent::setContextAutomationPointCurve(
    const std::uint64_t id,
    const AudioTrackController::AutomationCurve curve,
    const float amount)
{
    if (currentEditorMode_ == 2 && selectedMidiTrackId_ != 0
        && selectedMidiClipId_ != 0)
        return audio_.setMidiClipAutomationPointCurve(
            selectedMidiTrackId_, selectedMidiClipId_, id, curve, amount);
    if (currentEditorMode_ == 6 && launcherEditorContext_) {
        const auto context = *launcherEditorContext_;
        return audio_.setLauncherClipAutomationPointCurve(
            context.trackIndex, context.sceneIndex, id, curve, amount);
    }
    if (currentEditorMode_ == 6 && selectedTrack_
        && selectedAudioClipId_ != 0)
        return audio_.setAudioClipAutomationPointCurve(
            *selectedTrack_, selectedAudioClipId_, id, curve, amount);
    if (!selectedTrack_)
        return false;
    std::size_t pluginIndex {};
    std::uint32_t parameterIndex {};
    if (parsePluginAutomationTarget(automationTargetId_, pluginIndex,
                                    parameterIndex))
        return audio_.setPluginAutomationPointCurve(
            *selectedTrack_, pluginIndex, parameterIndex, id, curve,
            amount);
    return audio_.setAutomationPointCurve(*selectedTrack_, id, curve,
                                           amount);
}

void WorkspaceComponent::refreshBottomAutomationEditor()
{
    const auto midiContext = currentEditorMode_ == 2
                             && selectedMidiTrackId_ != 0;
    const auto clipLocalContext = usesClipLocalAutomation();
    std::vector<AutomationEditorComponent::Target> targets;
    targets.push_back(
        { "gain", tr("automation.trackGain"), -60.0f, 12.0f, " dB" });
    if (!clipLocalContext && !midiContext && selectedTrack_
        && *selectedTrack_ < audio_.trackCount()) {
        const auto info = audio_.trackInfo(*selectedTrack_);
        for (std::size_t pluginIndex = 0; pluginIndex < info.plugins.size();
             ++pluginIndex) {
            std::vector<IsolatedPluginHost::ParameterSnapshot> parameters;
            if (audio_.refreshTrackPluginParameters(
                    *selectedTrack_, pluginIndex, parameters).failed())
                continue;
            for (const auto& parameter : parameters) {
                if (!parameter.automatable)
                    continue;
                auto name = info.plugins[pluginIndex].name + "  /  "
                            + parameter.name;
                if (parameter.label.isNotEmpty())
                    name += " (" + parameter.label + ")";
                targets.push_back(
                    { pluginAutomationTargetId(pluginIndex,
                                               parameter.index),
                      std::move(name), 0.0f, 100.0f, " %" });
            }
        }
    }

    const auto selectedExists = std::any_of(
        targets.begin(), targets.end(), [this](const auto& target) {
            return target.id == automationTargetId_;
        });
    if (!selectedExists)
        automationTargetId_ = "gain";
    bottomAutomationEditor_.setTargets(targets,
                                       automationTargetId_);
    contextAutomationEditor_.setTargets(std::move(targets),
                                        automationTargetId_);
    const auto timeInset = midiContext ? 54.0f : 0.0f;
    bottomAutomationEditor_.setTimeAxisInset(timeInset);
    contextAutomationEditor_.setTimeAxisInset(timeInset);
    const auto contextualGrid = currentEditorMode_ == 6
                                    ? clipTimelineEditor_.gridDivision()
                                : midiContext
                                    ? pianoRoll_.gridDivision()
                                    : arrangementGridDivision();
    const auto contextualSnap = currentEditorMode_ == 6
                                    ? clipTimelineEditor_.snappingEnabled()
                                : midiContext
                                    ? pianoRoll_.snappingEnabled()
                                    : arrangementSnappingEnabled();
    bottomAutomationEditor_.setMusicalGrid(
        audio_.tempo(), contextualGrid, contextualSnap);
    contextAutomationEditor_.setMusicalGrid(
        audio_.tempo(), contextualGrid, contextualSnap);

    auto globalDuration = std::max(1.0, audio_.length());
    auto localDuration = globalDuration;
    auto visibleStart = 0.0;
    auto visibleEnd = localDuration;
    contextualAutomationOffsetSeconds_ = 0.0;
    if (midiContext) {
        globalDuration = std::max(globalDuration, audio_.midiLengthSeconds());
        const auto tracks = audio_.midiTracks();
        const auto track = std::find_if(
            tracks.begin(), tracks.end(), [this](const auto& value) {
                return value.id == selectedMidiTrackId_;
            });
        if (track != tracks.end()) {
            const auto clip = std::find_if(
                track->clips.begin(), track->clips.end(),
                [this](const auto& value) {
                    return value.id == selectedMidiClipId_;
                });
            if (clip != track->clips.end()) {
                const auto secondsPerBeat = 60.0
                                            / std::max(20.0,
                                                       audio_.tempo());
                contextualAutomationOffsetSeconds_
                    = clip->startBeats * secondsPerBeat;
                localDuration = std::max(
                    std::max(0.001, clip->lengthBeats * secondsPerBeat),
                    pianoRoll_.visibleEndBeat() * secondsPerBeat);
                visibleStart = pianoRoll_.visibleStartBeat()
                               * secondsPerBeat;
                visibleEnd = pianoRoll_.visibleEndBeat()
                             * secondsPerBeat;
            }
        }
    } else if (currentEditorMode_ == 6) {
        localDuration = std::max(
            0.001, clipTimelineEditor_.timelineLengthSeconds());
        visibleStart = clipTimelineEditor_.visibleStartSeconds();
        visibleEnd = clipTimelineEditor_.visibleEndSeconds();
        if (!launcherEditorContext_ && selectedTrack_
            && selectedAudioClipId_ != 0)
            if (const auto clip = audio_.audioClip(
                    *selectedTrack_, selectedAudioClipId_))
                contextualAutomationOffsetSeconds_ = clip->startSeconds;
    } else if (currentEditorMode_ == 1) {
        visibleStart = arrangementViewStartSeconds_;
        visibleEnd = std::min(
            globalDuration, arrangementViewStartSeconds_
                          + arrangementTimelineLengthSeconds_
                                / std::max(1.0, arrangementZoom_));
        localDuration = globalDuration;
    }

    std::vector<AutomationEditorComponent::Point> points;
    const auto appendPoint = [this, &points, localDuration,
                              clipLocalContext](
                                 const auto& point, const float value) {
        const auto localTime = clipLocalContext
                                   ? point.timeSeconds
                                   : point.timeSeconds
                                         - contextualAutomationOffsetSeconds_;
        if (!clipLocalContext && contextualAutomationOffsetSeconds_ > 0.0
            && (localTime < -0.000001
                || localTime > localDuration + 0.000001))
            return;
        points.push_back({ point.id, localTime, value,
                           static_cast<int>(point.curve),
                           point.curveAmount });
    };
    if (midiContext) {
        const auto automation = clipLocalContext
            ? audio_.midiClipAutomationPoints(
                  selectedMidiTrackId_, selectedMidiClipId_)
            : audio_.midiAutomationPoints(selectedMidiTrackId_);
        points.reserve(automation.size());
        for (const auto& point : automation)
            appendPoint(point, point.valueDb);
    } else if (currentEditorMode_ == 6 && launcherEditorContext_) {
        const auto context = *launcherEditorContext_;
        const auto automation = audio_.launcherClipAutomationPoints(
            context.trackIndex, context.sceneIndex);
        points.reserve(automation.size());
        for (const auto& point : automation)
            appendPoint(point, point.valueDb);
    } else if (currentEditorMode_ == 6 && selectedTrack_
               && selectedAudioClipId_ != 0) {
        const auto automation = audio_.audioClipAutomationPoints(
            *selectedTrack_, selectedAudioClipId_);
        points.reserve(automation.size());
        for (const auto& point : automation)
            appendPoint(point, point.valueDb);
    } else if (selectedTrack_ && *selectedTrack_ < audio_.trackCount()) {
        std::size_t pluginIndex {};
        std::uint32_t parameterIndex {};
        if (parsePluginAutomationTarget(automationTargetId_, pluginIndex,
                                        parameterIndex)) {
            const auto automation = audio_.pluginAutomationPoints(
                *selectedTrack_, pluginIndex, parameterIndex);
            points.reserve(automation.size());
            for (const auto& point : automation)
                appendPoint(point, point.normalisedValue * 100.0f);
        } else {
            const auto automation = audio_.automationPoints(*selectedTrack_);
            points.reserve(automation.size());
            for (const auto& point : automation)
                appendPoint(point, point.valueDb);
        }
    }
    bottomAutomationEditor_.setPoints(points, localDuration);
    contextAutomationEditor_.setPoints(std::move(points), localDuration);
    bottomAutomationEditor_.setVisibleRange(
        visibleStart, std::max(visibleStart + 0.001,
                               std::min(localDuration, visibleEnd)));
    contextAutomationEditor_.setVisibleRange(
        visibleStart, std::max(visibleStart + 0.001,
                               std::min(localDuration, visibleEnd)));
}

void WorkspaceComponent::refreshAudioEditor()
{
    if (launcherEditorContext_) {
        const auto context = *launcherEditorContext_;
        const auto clip = audio_.launcherClip(context.trackIndex,
                                               context.sceneIndex);
        if (!clip.has_value()) {
            launcherEditorContext_.reset();
            audioEditor_.clear();
            return;
        }
        const auto event = std::find_if(
            clip->events.begin(), clip->events.end(),
            [this](const auto& value) {
                return value.id == sourceEditorEventId_;
            });
        if (event == clip->events.end()) {
            audioEditor_.clear();
            return;
        }
        audioEditor_.setTrack(
            event->sourceFile, event->sourceLengthSeconds,
            event->startSeconds, event->sourceOffsetSeconds,
            event->durationSeconds, event->sourceDurationSeconds);
        std::vector<AudioEditorComponent::StretchMarker> markers;
        markers.reserve(event->stretchMarkers.size());
        for (const auto& marker : event->stretchMarkers)
            markers.push_back({ marker.id, marker.sourceSeconds,
                                marker.timelineSeconds });
        audioEditor_.setStretchMarkers(std::move(markers), audio_.tempo(),
                                       arrangementGridDivision());
        audioEditor_.setHistoryState(false, false);
        audioEditor_.setBusy(audioEditInProgress_);
        return;
    }
    if (!selectedTrack_ || *selectedTrack_ >= audio_.trackCount()) {
        audioEditor_.clear();
        return;
    }
    const auto clip = audio_.audioClip(*selectedTrack_, selectedAudioClipId_);
    if (!clip.has_value()) {
        audioEditor_.clear(true);
        return;
    }
    if (sourceEditorEventId_ != 0) {
        const auto events = audio_.audioClipEvents(
            *selectedTrack_, selectedAudioClipId_);
        const auto event = std::find_if(
            events.begin(), events.end(), [this](const auto& value) {
                return value.id == sourceEditorEventId_;
            });
        if (event != events.end()) {
            audioEditor_.setTrack(
                event->sourceFile, event->sourceLengthSeconds,
                clip->startSeconds + event->startSeconds,
                event->sourceOffsetSeconds, event->durationSeconds,
                event->sourceDurationSeconds);
            std::vector<AudioEditorComponent::StretchMarker> markers;
            markers.reserve(event->stretchMarkers.size());
            for (const auto& marker : event->stretchMarkers)
                markers.push_back({ marker.id, marker.sourceSeconds,
                                    marker.timelineSeconds });
            audioEditor_.setStretchMarkers(
                std::move(markers), audio_.tempo(),
                arrangementGridDivision());
            audioEditor_.setHistoryState(
                audio_.canUndoArrangementEdit(*selectedTrack_),
                audio_.canRedoArrangementEdit(*selectedTrack_));
            audioEditor_.setBusy(audioEditInProgress_);
            return;
        }
        sourceEditorEventId_ = 0;
    }
    audioEditor_.setTrack(clip->sourceFile,
                          clip->sourceLengthSeconds,
                          clip->startSeconds,
                          clip->sourceOffsetSeconds,
                          clip->durationSeconds,
                          clip->sourceDurationSeconds);
    std::vector<AudioEditorComponent::StretchMarker> stretchMarkers;
    stretchMarkers.reserve(clip->stretchMarkers.size());
    for (const auto& marker : clip->stretchMarkers)
        stretchMarkers.push_back(
            { marker.id, marker.sourceSeconds, marker.timelineSeconds });
    audioEditor_.setStretchMarkers(
        std::move(stretchMarkers), audio_.tempo(),
        arrangementGridDivision());
    audioEditor_.setHistoryState(
        audio_.canUndoTrackAudioEdit(*selectedTrack_),
        audio_.canRedoTrackAudioEdit(*selectedTrack_));
    audioEditor_.setBusy(audioEditInProgress_);
}

void WorkspaceComponent::refreshClipTimelineEditor()
{
    if (launcherEditorContext_) {
        const auto context = *launcherEditorContext_;
        const auto clip = audio_.launcherClip(context.trackIndex,
                                               context.sceneIndex);
        if (!clip.has_value()) {
            launcherEditorContext_.reset();
            clipTimelineEditor_.clear();
            return;
        }
        auto events = clip->events;
        if (!clip->usesEventTimeline && events.empty()
            && clip->sourceFile != juce::File {}) {
            auto reader = std::unique_ptr<juce::AudioFormatReader>(
                audio_.formatManager().createReaderFor(clip->sourceFile));
            const auto sourceLength = reader != nullptr
                                          && reader->sampleRate > 0.0
                                      ? static_cast<double>(
                                            reader->lengthInSamples)
                                            / reader->sampleRate
                                      : clip->sourceOffsetSeconds
                                            + clip->durationSeconds;
            events.push_back({
                launcherClipEditorEventId, clip->name, 0.0,
                clip->sourceOffsetSeconds, clip->durationSeconds,
                clip->durationSeconds, clip->colour, clip->sourceFile,
                sourceLength });
        }
        clipTimelineEditor_.setClip(
            clip->name, clip->durationSeconds, std::move(events),
            audio_.tempo(), arrangementGridDivision(),
            arrangementSnappingEnabled(), clip->playStartSeconds,
            clip->playEndSeconds, clip->loopStartSeconds,
            clip->loopEndSeconds, clip->looping);
        clipEditorCueSeconds_ = juce::jlimit(
            clip->playStartSeconds, clip->playEndSeconds,
            clipEditorCueSeconds_);
        clipTimelineEditor_.setPlayhead(clipEditorCueSeconds_);
        clipTimelineEditor_.setTransportState(
            audio_.activeLauncherScene(context.trackIndex)
            == context.sceneIndex);
        return;
    }
    if (!selectedTrack_ || *selectedTrack_ >= audio_.trackCount()
        || selectedAudioClipId_ == 0) {
        clipTimelineEditor_.clear();
        return;
    }
    const auto clip = audio_.audioClip(*selectedTrack_,
                                       selectedAudioClipId_);
    if (!clip.has_value()) {
        clipTimelineEditor_.clear();
        return;
    }
    clipTimelineEditor_.setClip(
        clip->name, clip->durationSeconds,
        audio_.audioClipEvents(*selectedTrack_, selectedAudioClipId_),
        audio_.tempo(), arrangementGridDivision(),
        arrangementSnappingEnabled(), clip->playStartSeconds,
        clip->playEndSeconds, clip->loopStartSeconds,
        clip->loopEndSeconds, clip->looping);
    clipEditorCueSeconds_ = juce::jlimit(
        clip->playStartSeconds, clip->playEndSeconds,
        clipEditorCueSeconds_);
    clipTimelineEditor_.setPlayhead(clipEditorCueSeconds_);
    clipTimelineEditor_.setTransportState(
        audio_.isAudioClipPreviewing());
}

void WorkspaceComponent::exportAudioSelection(
    const std::size_t trackIndex,
    const juce::File& source,
    const double sourceStartSeconds,
    const double sourceEndSeconds,
    const juce::File& destination)
{
    if (audioEditInProgress_ || source == juce::File {}
        || destination == juce::File {}
        || sourceEndSeconds <= sourceStartSeconds)
        return;

    audioEditInProgress_ = true;
    audioEditCancellation_ = std::make_shared<std::atomic<bool>>(false);
    const auto cancellation = audioEditCancellation_;
    audioEditor_.setBusy(true, tr("audioEditor.exporting"));
    const juce::Component::SafePointer<WorkspaceComponent> safeThis(this);
    audioEditPool_.addJob(
        [safeThis, cancellation, source, destination, trackIndex,
         sourceStartSeconds, sourceEndSeconds] {
            const auto result = AudioTrackController::exportAudioRange(
                source, sourceStartSeconds,
                sourceEndSeconds - sourceStartSeconds,
                destination, cancellation.get());
            const auto succeeded = result.wasOk();
            const auto error = result.getErrorMessage();
            const auto wasCancelled = cancellation->load(std::memory_order_acquire);
            juce::MessageManager::callAsync(
                [safeThis, cancellation, destination, trackIndex,
                 succeeded, error, wasCancelled] {
                    auto* const workspace = safeThis.getComponent();
                    if (workspace == nullptr
                        || workspace->audioEditCancellation_ != cancellation)
                        return;
                    workspace->audioEditInProgress_ = false;
                    workspace->audioEditCancellation_.reset();
                    workspace->audioEditor_.setBusy(false);
                    if (!succeeded) {
                        destination.deleteFile();
                        if (wasCancelled) {
                            workspace->refreshAudioEditor();
                            workspace->audioEditor_.showStatus(
                                tr("audioEditor.cancelled"));
                            return;
                        }
                        workspace->audioEditor_.showStatus(
                            tr("audioEditor.failed"));
                        workspace->showError(tr("error.exportTitle"), error);
                        workspace->refreshAudioEditor();
                        return;
                    }
                    workspace->audioEditor_.showStatus(tr(
                        "audioEditor.exported", "file",
                        destination.getFileName()));
                    if (workspace->selectedTrack_
                        && *workspace->selectedTrack_ == trackIndex)
                        workspace->refreshAudioEditor();
                });
        });
}

void WorkspaceComponent::renderAudioSelection(
    const AudioEditRequest request,
    const double sourceStartSeconds,
    const double sourceEndSeconds)
{
    if (audioEditInProgress_ || !selectedTrack_
        || *selectedTrack_ >= audio_.trackCount()
        || sourceEndSeconds <= sourceStartSeconds)
        return;

    const auto trackIndex = *selectedTrack_;
    const auto clip = audio_.audioClip(trackIndex, selectedAudioClipId_);
    if (!clip.has_value())
        return;
    const auto clipId = clip->id;
    const auto eventId = sourceEditorEventId_;
    auto source = clip->sourceFile;
    if (eventId != 0) {
        const auto events = audio_.audioClipEvents(trackIndex, clipId);
        const auto event = std::find_if(
            events.begin(), events.end(), [eventId](const auto& value) {
                return value.id == eventId;
            });
        if (event == events.end())
            return;
        source = event->sourceFile;
    }
    const auto editsDirectory = juce::File::getSpecialLocation(
                                    juce::File::userApplicationDataDirectory)
                                    .getChildFile("CyDAW")
                                    .getChildFile("Edits");
    const auto destination = editsDirectory.getChildFile(
        source.getFileNameWithoutExtension() + "-"
        + juce::Uuid().toString().substring(0, 12) + ".wav");
    juce::String operationName;
    switch (request.operation) {
        case AudioEditOperation::fadeIn:
            operationName = tr("audioEditor.fadeIn");
            break;
        case AudioEditOperation::fadeOut:
            operationName = tr("audioEditor.fadeOut");
            break;
        case AudioEditOperation::silence:
            operationName = tr("audioEditor.silence");
            break;
        case AudioEditOperation::reverse:
            operationName = tr("audioEditor.reverse");
            break;
        case AudioEditOperation::amplify:
            operationName = tr("audioEditor.gainAmount", "gain",
                               juce::String(request.levelDb, 1));
            break;
        case AudioEditOperation::normalisePeak:
            operationName = tr("audioEditor.normalise");
            break;
        case AudioEditOperation::removeDcOffset:
            operationName = tr("audioEditor.removeDc");
            break;
        case AudioEditOperation::invertPolarity:
            operationName = tr("audioEditor.invertPolarity");
            break;
        case AudioEditOperation::swapStereo:
            operationName = tr("audioEditor.swapStereo");
            break;
    }

    audioEditInProgress_ = true;
    audioEditCancellation_ = std::make_shared<std::atomic<bool>>(false);
    const auto cancellation = audioEditCancellation_;
    audioEditor_.setBusy(
        true, tr("audioEditor.processing", "operation", operationName));
    const juce::Component::SafePointer<WorkspaceComponent> safeThis(this);
    audioEditPool_.addJob(
        [safeThis, cancellation, source, destination, trackIndex, clipId,
         eventId,
         sourceStartSeconds, sourceEndSeconds, request, operationName] {
            const auto result = AudioTrackController::renderAudioEdit(
                source, sourceStartSeconds,
                sourceEndSeconds - sourceStartSeconds,
                request, destination, cancellation.get());
            const auto succeeded = result.wasOk();
            const auto error = result.getErrorMessage();
            const auto wasCancelled = cancellation->load(std::memory_order_acquire);
            juce::MessageManager::callAsync(
                [safeThis, cancellation, source, destination, trackIndex, clipId,
                 eventId,
                 succeeded, error, operationName, wasCancelled] {
                    auto* const workspace = safeThis.getComponent();
                    if (workspace == nullptr)
                        return;
                    if (workspace->audioEditCancellation_ != cancellation) {
                        destination.deleteFile();
                        return;
                    }
                    workspace->audioEditInProgress_ = false;
                    workspace->audioEditCancellation_.reset();
                    workspace->audioEditor_.setBusy(false);
                    if (!succeeded) {
                        destination.deleteFile();
                        if (wasCancelled) {
                            workspace->refreshAudioEditor();
                            workspace->audioEditor_.showStatus(
                                tr("audioEditor.cancelled"));
                            return;
                        }
                        workspace->audioEditor_.showStatus(
                            tr("audioEditor.failed"));
                        workspace->showError(tr("error.audioEditTitle"), error);
                        workspace->refreshAudioEditor();
                        return;
                    }
                    const auto currentClip = workspace->audio_.audioClip(
                        trackIndex, clipId);
                    auto sourceStillMatches
                        = currentClip.has_value()
                          && currentClip->sourceFile == source;
                    if (currentClip.has_value() && eventId != 0) {
                        const auto events
                            = workspace->audio_.audioClipEvents(
                                trackIndex, clipId);
                        const auto event = std::find_if(
                            events.begin(), events.end(),
                            [eventId](const auto& value) {
                                return value.id == eventId;
                            });
                        sourceStillMatches = event != events.end()
                                             && event->sourceFile == source;
                    }
                    if (!sourceStillMatches) {
                        destination.deleteFile();
                        workspace->audioEditor_.showStatus(
                            tr("audioEditor.sourceChanged"));
                        workspace->refreshAudioEditor();
                        return;
                    }
                    const auto commit = eventId != 0
                        ? workspace->audio_.commitAudioClipEventEdit(
                            trackIndex, clipId, eventId, destination)
                        : workspace->audio_.commitAudioClipEdit(
                            trackIndex, clipId, destination);
                    if (commit.failed()) {
                        destination.deleteFile();
                        workspace->showError(tr("error.audioEditTitle"),
                                             commit.getErrorMessage());
                        workspace->refreshAudioEditor();
                        return;
                    }
                    workspace->refreshTrackAfterAudioEdit(trackIndex);
                    workspace->audioEditor_.showStatus(
                        tr("audioEditor.processed", "operation", operationName));
                    workspace->markProjectDirty();
                });
        });
}

void WorkspaceComponent::undoAudioEdit()
{
    if (audioEditInProgress_ || !selectedTrack_)
        return;
    const auto undone = sourceEditorEventId_ != 0
                            ? audio_.undoArrangementEdit(*selectedTrack_)
                            : audio_.undoTrackAudioEdit(*selectedTrack_);
    if (!undone)
        return;
    const auto trackIndex = *selectedTrack_;
    refreshTrackAfterAudioEdit(trackIndex);
    refreshClipTimelineEditor();
    audioEditor_.showStatus(tr("audioEditor.undone"));
    markProjectDirty();
}

void WorkspaceComponent::redoAudioEdit()
{
    if (audioEditInProgress_ || !selectedTrack_)
        return;
    const auto redone = sourceEditorEventId_ != 0
                            ? audio_.redoArrangementEdit(*selectedTrack_)
                            : audio_.redoTrackAudioEdit(*selectedTrack_);
    if (!redone)
        return;
    const auto trackIndex = *selectedTrack_;
    refreshTrackAfterAudioEdit(trackIndex);
    refreshClipTimelineEditor();
    audioEditor_.showStatus(tr("audioEditor.redone"));
    markProjectDirty();
}

void WorkspaceComponent::undoArrangementEdit()
{
    if (!audio_.undoArrangementEdit())
        return;
    clearArrangementMarqueeSelection();
    if (selectedTrack_ && *selectedTrack_ < audio_.trackCount())
        normaliseAudioClipSelection(audio_.audioClips(*selectedTrack_));
    refreshTimelineRanges();
    refreshSelectedTrack();
    refreshAutomationEditor();
    refreshAudioEditor();
    refreshClipTimelineEditor();
    refreshClipLauncher();
    markProjectDirty();
}

void WorkspaceComponent::redoArrangementEdit()
{
    if (!audio_.redoArrangementEdit())
        return;
    clearArrangementMarqueeSelection();
    if (selectedTrack_ && *selectedTrack_ < audio_.trackCount())
        normaliseAudioClipSelection(audio_.audioClips(*selectedTrack_));
    refreshTimelineRanges();
    refreshSelectedTrack();
    refreshAutomationEditor();
    refreshAudioEditor();
    refreshClipTimelineEditor();
    refreshClipLauncher();
    markProjectDirty();
}

void WorkspaceComponent::refreshTrackAfterAudioEdit(const std::size_t trackIndex)
{
    if (trackIndex >= audio_.trackCount())
        return;
    const auto info = audio_.trackInfo(trackIndex);
    if (trackIndex < trackLanes_.size()) {
        const auto presentation = trackIndex < trackPresentations_.size()
                                      ? trackPresentations_[trackIndex]
                                      : TrackPresentation {
                                            tr("launcher.audioTrack", "index",
                                               juce::String(static_cast<int>(trackIndex + 1))),
                                            defaultTrackColour(trackIndex) };
        trackLanes_[trackIndex]->configure(trackIndex, info.file,
                                           presentation.name,
                                           presentation.colour,
                                           info.muted, info.solo,
                                           info.armed);
        trackLanes_[trackIndex]->setSelected(
            selectedTrack_ && *selectedTrack_ == trackIndex);
    }
    refreshTimelineRanges();
    refreshClipLauncher();
    if (selectedTrack_ && *selectedTrack_ == trackIndex)
        refreshSelectedTrack();
    else
        refreshAudioEditor();
}

void WorkspaceComponent::refreshClipLauncher()
{
    noteUiHitchEvent("launcher-refresh");
    std::vector<ClipLauncherComponent::Track> tracks;
    const auto midiTracks = audio_.midiTracks();
    tracks.reserve(audio_.trackCount() + midiTracks.size());
    for (std::size_t trackIndex = 0; trackIndex < audio_.trackCount(); ++trackIndex) {
        const auto info = audio_.trackInfo(trackIndex);
        ClipLauncherComponent::Track track;
        if (trackIndex < trackPresentations_.size())
            track.name = trackPresentations_[trackIndex].name;
        if (track.name.isEmpty())
            track.name = tr("launcher.audioTrack", "index",
                            juce::String(static_cast<int>(trackIndex + 1)));
        track.gainDb = info.gainDb;
        track.pan = info.pan;
        track.muted = info.muted;
        track.solo = info.solo;
        track.armed = info.armed;
        track.selected = selectedTrack_ && *selectedTrack_ == trackIndex;
        track.latencySamples = info.latencyCompensationSamples;

        auto deviceNames = deviceNamesForTrack(trackIndex, 6);
        for (const auto& plugin : info.plugins) {
            if (!deviceNames.contains(plugin.name, true))
                deviceNames.add(plugin.name);
            if (deviceNames.size() >= 6)
                break;
        }
        for (const auto& name : deviceNames)
            track.devices.push_back(name);
        const auto activeScene = audio_.activeLauncherScene(trackIndex);
        const auto pendingScene = audio_.pendingLauncherScene(trackIndex);
        for (const auto& clip : audio_.launcherClips(trackIndex)) {
            if (clip.sceneIndex < 0 || clip.sceneIndex >= ClipLauncherComponent::sceneCount)
                continue;
            auto& slot = track.slots[static_cast<std::size_t>(clip.sceneIndex)];
            slot.name = clip.name;
            slot.colour = clip.colour;
            slot.occupied = true;
            slot.looping = clip.looping;
            slot.active = activeScene == clip.sceneIndex;
            slot.queued = pendingScene == clip.sceneIndex;
        }
        if (launcherRecordingTrack_ == trackIndex
            && launcherRecordingScene_ >= 0
            && launcherRecordingScene_
                   < ClipLauncherComponent::sceneCount) {
            auto& slot = track.slots[static_cast<std::size_t>(
                launcherRecordingScene_)];
            slot.recordPending = !audio_.recordingStarted();
            slot.recordStopPending = audio_.recordingStopPending();
            slot.recording = audio_.recordingStarted()
                             && !slot.recordStopPending;
        }
        tracks.push_back(std::move(track));
    }
    for (const auto& info : midiTracks) {
        ClipLauncherComponent::Track track;
        track.name = info.name;
        track.midi = true;
        track.gainDb = info.gainDb;
        track.pan = info.pan;
        track.muted = info.muted;
        track.solo = info.solo;
        track.armed = info.armed;
        track.selected = !selectedTrack_
                         && selectedMidiTrackId_ == info.id;

        for (const auto& name : deviceNamesForMidiTrack(info.id, 0))
            track.devices.push_back(name);

        const auto activeScene = audio_.activeMidiLauncherScene(info.id);
        for (const auto& clip : info.clips) {
            const auto scene = juce::roundToInt(clip.startBeats / 16.0);
            if (scene < 0 || scene >= ClipLauncherComponent::sceneCount)
                continue;
            auto& slot = track.slots[static_cast<std::size_t>(scene)];
            slot.name = clip.name;
            slot.colour = clip.colour;
            slot.occupied = true;
            slot.looping = clip.looping;
            slot.active = activeScene == scene;
            slot.playbackProgress = slot.active
                                        ? audio_.midiLauncherClipProgress(
                                              info.id)
                                        : 0.0f;
        }
        tracks.push_back(std::move(track));
    }
    clipLauncher_.setQuantization(static_cast<int>(audio_.launchQuantization()));
    clipLauncher_.setMasterGain(audio_.masterGainDb());
    clipLauncher_.setTracks(std::move(tracks));
    // The mixer shows the same channels, so it follows the same refreshes.
    refreshMixer();
}

void WorkspaceComponent::refreshMixer()
{
    if (!mixer_.isVisible())
        return;
    std::vector<MixerComponent::Channel> channels;
    const auto midiTracks = audio_.midiTracks();
    channels.reserve(audio_.trackCount() + midiTracks.size());
    for (std::size_t trackIndex = 0; trackIndex < audio_.trackCount();
         ++trackIndex) {
        const auto info = audio_.trackInfo(trackIndex);
        MixerComponent::Channel channel;
        if (trackIndex < trackPresentations_.size()) {
            channel.name = trackPresentations_[trackIndex].name;
            channel.colour = trackPresentations_[trackIndex].colour;
        }
        if (channel.name.isEmpty())
            channel.name = tr("launcher.audioTrack", "index",
                              juce::String(static_cast<int>(trackIndex + 1)));
        channel.gainDb = info.gainDb;
        channel.pan = info.pan;
        channel.muted = info.muted;
        channel.solo = info.solo;
        channel.armed = info.armed;
        channel.selected = selectedTrack_ && *selectedTrack_ == trackIndex;
        const auto inputBus = audio_.virtualBus(info.inputBusId);
        channel.input = inputBus ? inputBus->name : tr("launcher.allInputs");
        channel.output = tr("launcher.masterOutput");
        channel.inputSelectable = true;
        auto deviceNames = deviceNamesForTrack(trackIndex, 6);
        for (const auto& plugin : info.plugins) {
            if (!deviceNames.contains(plugin.name, true))
                deviceNames.add(plugin.name);
            if (deviceNames.size() >= 6)
                break;
        }
        for (const auto& name : deviceNames)
            channel.devices.push_back(name);
        channels.push_back(std::move(channel));
    }
    for (const auto& info : midiTracks) {
        MixerComponent::Channel channel;
        channel.name = info.name;
        channel.midi = true;
        channel.colour = juce::Colour(
            info.clips.empty() ? 0xffff9b2f : info.clips.front().colour);
        channel.gainDb = info.gainDb;
        channel.pan = info.pan;
        channel.muted = info.muted;
        channel.solo = info.solo;
        channel.armed = info.armed;
        channel.selected = !selectedTrack_ && selectedMidiTrackId_ == info.id;
        channel.input = tr("node.midiInput");
        channel.output = tr("launcher.masterOutput");
        for (const auto& name : deviceNamesForMidiTrack(info.id, 6))
            channel.devices.push_back(name);
        channels.push_back(std::move(channel));
    }
    mixer_.setMasterGain(audio_.masterGainDb());
    mixer_.setChannels(std::move(channels));
}

juce::StringArray WorkspaceComponent::deviceNamesForTrack(
    const std::size_t trackIndex, const int limit) const
{
    if (selectedTrack_ && *selectedTrack_ == trackIndex
        && !isMidiNodeGraphContext())
        return nodeGraph_.deviceNames(limit);
    return deviceNamesInGraph(overviewGraphStateForTrack(trackIndex), limit);
}

juce::StringArray WorkspaceComponent::deviceNamesForMidiTrack(
    const std::uint64_t trackId, const int limit) const
{
    if (!selectedTrack_ && selectedMidiTrackId_ == trackId
        && isMidiNodeGraphContext())
        return nodeGraph_.deviceNames(limit);
    return deviceNamesInGraph(overviewGraphStateForMidiTrack(trackId), limit);
}

juce::var WorkspaceComponent::overviewGraphStateForTrack(
    const std::size_t trackIndex) const
{
    // The selected track's graph lives in the editor; the others are held
    // as their saved state.
    if (selectedTrack_ && *selectedTrack_ == trackIndex
        && !isMidiNodeGraphContext())
        return nodeGraph_.serialise();
    return trackIndex < trackNodeGraphStates_.size()
               ? trackNodeGraphStates_[trackIndex]
               : juce::var {};
}

juce::var WorkspaceComponent::overviewGraphStateForMidiTrack(
    const std::uint64_t trackId) const
{
    if (!selectedTrack_ && selectedMidiTrackId_ == trackId
        && isMidiNodeGraphContext())
        return nodeGraph_.serialise();
    return midiNodeGraphStates_.contains(trackId)
               ? midiNodeGraphStates_.at(trackId)
               : factoryMidiNodeGraphState_;
}

juce::StringArray WorkspaceComponent::deviceNamesInGraph(
    const juce::var& graphState, const int limit)
{
    juce::StringArray names;
    const auto* graph = graphState.getDynamicObject();
    if (graph == nullptr)
        return names;
    const auto nodeValues = graph->getProperty(jsonKeys::nodes);
    const auto* nodes = nodeValues.getArray();
    if (nodes == nullptr)
        return names;
    for (const auto& value : *nodes) {
        const auto* node = value.getDynamicObject();
        if (node == nullptr)
            continue;
        // The graph's I/O and transport nodes are plumbing, not devices.
        const auto type = node->getProperty(jsonKeys::type).toString();
        if (type == "audio-clips" || type == "master" || type == "midi-input"
            || type == "transport" || type.startsWith("graph-"))
            continue;
        auto title = node->getProperty(jsonKeys::title).toString();
        if (title.isEmpty())
            title = type;
        if (!names.contains(title, true))
            names.add(title);
        if (limit > 0 && names.size() >= limit)
            break;
    }
    return names;
}

void WorkspaceComponent::applyChannelMix(const std::size_t channelIndex,
                                         const float gainDb, const float pan)
{
    if (const auto midiTrack = launcherMidiTrackId(channelIndex)) {
        const auto tracks = audio_.midiTracks();
        const auto state = std::find_if(
            tracks.begin(), tracks.end(),
            [midiTrack](const auto& value) { return value.id == *midiTrack; });
        if (state == tracks.end())
            return;
        audio_.setMidiTrackMixer(*midiTrack, gainDb, pan, state->muted,
                                 state->solo);
        if (!selectedTrack_ && selectedMidiTrackId_ == *midiTrack) {
            gainSlider_.setValue(gainDb, juce::dontSendNotification);
            panSlider_.setValue(pan, juce::dontSendNotification);
        }
        markProjectDirty();
        return;
    }
    if (channelIndex >= audio_.trackCount())
        return;
    const auto state = audio_.trackInfo(channelIndex);
    audio_.setTrackMixer(channelIndex, gainDb, pan, state.muted);
    if (selectedTrack_ == channelIndex) {
        gainSlider_.setValue(gainDb, juce::dontSendNotification);
        panSlider_.setValue(pan, juce::dontSendNotification);
        deviceChain_.setParameters(state.driveDb, state.filterCutoffHz,
                                   gainDb, pan, true);
    }
    markProjectDirty();
}

void WorkspaceComponent::applyChannelMute(const std::size_t channelIndex,
                                          const bool muted)
{
    if (const auto midiTrack = launcherMidiTrackId(channelIndex)) {
        const auto tracks = audio_.midiTracks();
        const auto state = std::find_if(
            tracks.begin(), tracks.end(),
            [midiTrack](const auto& value) { return value.id == *midiTrack; });
        if (state == tracks.end())
            return;
        audio_.setMidiTrackMixer(*midiTrack, state->gainDb, state->pan,
                                 muted, state->solo);
        if (!selectedTrack_ && selectedMidiTrackId_ == *midiTrack)
            muteButton_.setToggleState(muted, juce::dontSendNotification);
        markProjectDirty();
        return;
    }
    if (channelIndex >= audio_.trackCount())
        return;
    const auto state = audio_.trackInfo(channelIndex);
    audio_.setTrackMixer(channelIndex, state.gainDb, state.pan, muted);
    if (channelIndex < trackLanes_.size())
        trackLanes_[channelIndex]->setMixerState(muted, state.solo,
                                                 state.armed);
    if (selectedTrack_ == channelIndex)
        muteButton_.setToggleState(muted, juce::dontSendNotification);
    markProjectDirty();
}

void WorkspaceComponent::applyChannelSolo(const std::size_t channelIndex,
                                          const bool solo)
{
    if (const auto midiTrack = launcherMidiTrackId(channelIndex)) {
        const auto tracks = audio_.midiTracks();
        const auto state = std::find_if(
            tracks.begin(), tracks.end(),
            [midiTrack](const auto& value) { return value.id == *midiTrack; });
        if (state == tracks.end())
            return;
        audio_.setMidiTrackMixer(*midiTrack, state->gainDb, state->pan,
                                 state->muted, solo);
        if (!selectedTrack_ && selectedMidiTrackId_ == *midiTrack)
            soloButton_.setToggleState(solo, juce::dontSendNotification);
        markProjectDirty();
        return;
    }
    if (channelIndex >= audio_.trackCount())
        return;
    audio_.setTrackSolo(channelIndex, solo);
    const auto state = audio_.trackInfo(channelIndex);
    if (channelIndex < trackLanes_.size())
        trackLanes_[channelIndex]->setMixerState(state.muted, solo,
                                                 state.armed);
    if (selectedTrack_ == channelIndex)
        soloButton_.setToggleState(solo, juce::dontSendNotification);
    markProjectDirty();
}

void WorkspaceComponent::applyChannelArm(const std::size_t channelIndex,
                                         const bool armed)
{
    if (const auto midiTrack = launcherMidiTrackId(channelIndex)) {
        if (audio_.setMidiTrackArmed(*midiTrack, armed)) {
            refreshClipLauncher();
            markProjectDirty();
        }
        return;
    }
    if (channelIndex >= audio_.trackCount())
        return;
    if (audio_.isRecording()) {
        // Arming is frozen while recording; the views snap back to the
        // engine's state.
        refreshAudioTrackArmStates();
        refreshClipLauncher();
        return;
    }
    audio_.setTrackArmed(channelIndex, armed);
    refreshAudioTrackArmStates();
    refreshClipLauncher();
    markProjectDirty();
}

void WorkspaceComponent::applyMasterGain(const float gainDb)
{
    audio_.setMasterGain(gainDb);
    markProjectDirty();
}

void WorkspaceComponent::showEditorMode(const int mode)
{
    setEditorMode(mode);
}

void WorkspaceComponent::showBrowserCategory(const int category)
{
    browser_.selectQuickCategory(category);
}

void WorkspaceComponent::showTrackInputBusMenu(const std::size_t trackIndex)
{
    if (trackIndex >= audio_.trackCount())
        return;
    const auto current = audio_.trackInfo(trackIndex).inputBusId;
    std::vector<std::uint64_t> busIds;
    juce::PopupMenu menu;
    menu.addSectionHeader(tr("bus.inputBus"));
    for (const auto& bus : audio_.virtualBuses()) {
        if (bus.direction != VirtualBusDirection::input)
            continue;
        busIds.push_back(bus.id);
        menu.addItem(static_cast<int>(busIds.size()), bus.name, true,
                     bus.id == current);
    }
    if (busIds.empty())
        return;
    const juce::Component::SafePointer<WorkspaceComponent> safeThis(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options(),
        [safeThis, trackIndex, busIds](const int result) {
            auto* workspace = safeThis.getComponent();
            if (workspace == nullptr || result <= 0
                || static_cast<std::size_t>(result) > busIds.size())
                return;
            if (!workspace->audio_.setTrackInputBus(
                    trackIndex, busIds[static_cast<std::size_t>(result - 1)]))
                return;
            workspace->refreshMixer();
            workspace->markProjectDirty();
        });
}

void WorkspaceComponent::recordLauncherAction(LauncherAction action)
{
    if (replayingLauncherActions_
        || !launcherActionRecordButton_.getToggleState())
        return;
    action.timeSeconds = std::max(0.0, action.timeSeconds);
    launcherActions_.push_back(action);
    std::stable_sort(
        launcherActions_.begin(), launcherActions_.end(),
        [](const LauncherAction& first, const LauncherAction& second) {
            return first.timeSeconds < second.timeSeconds;
        });
    markProjectDirty();
    refreshArrangementInteractionMode();
}

void WorkspaceComponent::replayLauncherActions(const double position)
{
    if (!launcherActionReplayButton_.getToggleState()
        || !audio_.isPlaying() || launcherActions_.empty()) {
        lastLauncherReplayPosition_ = position;
        return;
    }
    if (position + 0.001 < lastLauncherReplayPosition_) {
        nextLauncherReplayAction_ = static_cast<std::size_t>(
            std::distance(
                launcherActions_.begin(),
                std::lower_bound(
                    launcherActions_.begin(), launcherActions_.end(), position,
                    [](const LauncherAction& action, const double time) {
                        return action.timeSeconds < time;
                    })));
    }
    replayingLauncherActions_ = true;
    while (nextLauncherReplayAction_ < launcherActions_.size()
           && launcherActions_[nextLauncherReplayAction_].timeSeconds
                  <= position + 0.002) {
        const auto action = launcherActions_[nextLauncherReplayAction_++];
        if (action.type == LauncherAction::Type::launchClip)
            (void) audio_.launchClip(action.trackIndex, action.sceneIndex);
        else if (action.type == LauncherAction::Type::stopTrack)
            (void) audio_.stopLauncherTrack(action.trackIndex, true);
        else
            (void) audio_.launchScene(action.sceneIndex);
    }
    replayingLauncherActions_ = false;
    lastLauncherReplayPosition_ = position;
}

void WorkspaceComponent::setEditorMode(const int requestedMode)
{
    const auto mode = juce::jlimit(1, 10, requestedMode);
    const auto previousMode = currentEditorMode_;
    noteUiHitchEvent("view=" + juce::String(mode));
    if (mode == 7 && graphWindow_ != nullptr) {
        dockGraphWindow();
        return;
    }
    if (mode == 7 && previousMode != 7)
        editorModeBeforeGraph_ = previousMode;
    if (mode == 7) {
        if (bottomPanelMode_ != 1)
            setBottomPanelMode(1);
        if (nodeGraph_.getParentComponent() != &editorArea_)
            editorArea_.addAndMakeVisible(nodeGraph_);
    } else if (previousMode == 7 && graphWindow_ == nullptr
               && nodeGraph_.getParentComponent() == &editorArea_) {
        nodeAreaContainer_.addChildComponent(nodeGraph_);
    }
    const auto previousWasLauncher
        = previousMode == 4
          || (previousMode == 6 && launcherEditorContext_);
    const auto nextIsLauncher
        = mode == 4 || (mode == 6 && launcherEditorContext_);
    if ((previousMode == 1 && nextIsLauncher)
        || (previousWasLauncher && mode == 1))
        audio_.stop();
    if (mode != previousMode)
        saveCurrentNodeGraphState();
    if (previousMode == 2 && mode != 2 && selectedMidiTrackId_ != 0
        && selectedMidiClipId_ != 0
        && audio_.isMidiClipEditorPreviewing(
            selectedMidiTrackId_, selectedMidiClipId_)) {
        clipEditorCueSeconds_
            = audio_.midiClipEditorPreviewPositionSeconds(
                selectedMidiTrackId_);
        (void) audio_.stopMidiClipEditorPreview(selectedMidiTrackId_);
    }
    if (mode != 2 && audio_.isMidiRecording()) {
        audio_.stopMidiRecording(true);
        markProjectDirty();
    }
    currentEditorMode_ = mode;
    editorModeSelector_.setSelectedId(mode, juce::dontSendNotification);
    arrangementViewButton_.setToggleState(mode == 1,
                                          juce::dontSendNotification);
    launcherViewButton_.setToggleState(mode == 4,
                                       juce::dontSendNotification);
    clipViewButton_.setToggleState(mode == 2 || mode == 6,
                                   juce::dontSendNotification);
    graphEditViewButton_.setToggleState(mode == 7,
                                        juce::dontSendNotification);
    busViewButton_.setToggleState(mode == 8,
                                  juce::dontSendNotification);
    samplesViewButton_.setToggleState(mode == 10, juce::dontSendNotification);
    mixerViewButton_.setToggleState(mode == 9,
                                    juce::dontSendNotification);
    if (mode == 4 || (mode == 6 && launcherEditorContext_)) {
        arrangementPlaybackButton_.setToggleState(
            false, juce::dontSendNotification);
        audio_.setArrangementPlaybackEnabled(false);
    } else if (mode == 1) {
        arrangementPlaybackButton_.setToggleState(
            true, juce::dontSendNotification);
        audio_.setArrangementPlaybackEnabled(true);
    }
    refreshArrangementInteractionMode();
    timelineActionBar_.setVisible(mode == 1);
    trackViewport_.setVisible(mode == 1);
    timelineRuler_.setVisible(mode == 1);
    timelineScrollBar_.setVisible(mode == 1);
    timelineZoomOutButton_.setVisible(mode == 1);
    timelineFitButton_.setVisible(mode == 1);
    timelineZoomInButton_.setVisible(mode == 1);
    arrangementRangeButton_.setVisible(mode == 1);
    pianoRoll_.setVisible(mode == 2);
    automationEditor_.setVisible(mode == 3);
    contextAutomationEditor_.setVisible(mode == 2 || mode == 6);
    clipLauncher_.setVisible(mode == 4);
    audioEditor_.setVisible(mode == 5);
    clipTimelineEditor_.setVisible(mode == 6);
    virtualBusManager_.setVisible(mode == 8);
    mixer_.setVisible(mode == 9);
    sampleExplorer_.setVisible(mode == 10);
    if (mode != 10 && previousMode == 10)
        audio_.stopSamplePreview();
    nodeGraph_.setVisible(mode == 7
                          || graphWindow_ != nullptr
                          || (bottomPanelMode_ == 2
                              && nodeGraph_.getParentComponent()
                                     == &nodeAreaContainer_));
    refreshInspectorControls();
    if (mode == 2) {
        refreshMidiEditor();
        restoreNodeGraphForMidiTrack(selectedMidiTrackId_);
    } else if (previousMode == 2 && selectedTrack_
               && *selectedTrack_ < audio_.trackCount()) {
        restoreNodeGraphForTrack(*selectedTrack_);
    }
    if (mode == 3)
        refreshAutomationTargets();
    if (mode == 4)
        refreshClipLauncher();
    if (mode == 5)
        refreshAudioEditor();
    if (mode == 6)
        refreshClipTimelineEditor();
    if (mode == 8)
        virtualBusManager_.refresh();
    if (mode == 9)
        refreshMixer();
    if (bottomPanelMode_ == 4 || mode == 2 || mode == 6)
        refreshBottomAutomationEditor();
    recordButton_.setTooltip(
        audio_.isRecording()
            ? tr("transport.stopRecording")
            : mode == 2 ? tr("transport.recordMidi")
                        : tr("transport.record"));
    resized();
}

void WorkspaceComponent::setBottomPanelMode(const int requestedMode)
{
    if (bottomPanelMode_ == 3)
        saveCurrentModulationState();
    bottomPanelMode_ = juce::jlimit(1, 4, requestedMode);
    devicesPanelButton_.setToggleState(bottomPanelMode_ == 1, juce::dontSendNotification);
    graphPanelButton_.setToggleState(bottomPanelMode_ == 2, juce::dontSendNotification);
    modulationPanelButton_.setToggleState(bottomPanelMode_ == 3,
                                           juce::dontSendNotification);
    automationPanelButton_.setToggleState(bottomPanelMode_ == 4,
                                           juce::dontSendNotification);
    const auto graphDockedAndSelected = bottomPanelMode_ == 2
                                       && currentEditorMode_ != 7
                                       && graphWindow_ == nullptr
                                       && nodeGraph_.getParentComponent()
                                              == &nodeAreaContainer_;
    deviceChain_.setVisible(bottomPanelMode_ == 1
                            || (bottomPanelMode_ == 2
                                && graphWindow_ != nullptr));
    nodeGraph_.setVisible(graphDockedAndSelected || graphWindow_ != nullptr
                          || currentEditorMode_ == 7);
    midiKeyboard_.setVisible(graphDockedAndSelected);
    modulationGraph_.setVisible(bottomPanelMode_ == 3);
    bottomAutomationEditor_.setVisible(bottomPanelMode_ == 4);
    modulationTargetSelector_.setVisible(bottomPanelMode_ == 3);
    modulationAmountSlider_.setVisible(bottomPanelMode_ == 3);
    modulationEnabledButton_.setVisible(bottomPanelMode_ == 3);
    if (bottomPanelMode_ == 3)
        refreshModulationTargets();
    if (bottomPanelMode_ == 4)
        refreshBottomAutomationEditor();
    graphEditViewButton_.setToggleState(currentEditorMode_ == 7,
                                        juce::dontSendNotification);
    resized();
}

void WorkspaceComponent::ensureTrackModulationState(
    const std::size_t trackIndex)
{
    if (trackModulationStates_.size() <= trackIndex)
        trackModulationStates_.resize(trackIndex + 1);
    auto& state = trackModulationStates_[trackIndex];
    if (state.graph.isVoid() || state.graph.isUndefined())
        state.graph = stateWithoutSharedModules(factoryModulationGraphState_);
}

void WorkspaceComponent::adoptTrackModulationGraph(
    const std::size_t trackIndex, const juce::var& savedGraph)
{
    if (savedGraph.getDynamicObject() == nullptr)
        return;
    ensureTrackModulationState(trackIndex);
    // Harvested before it is stored. The editor's injection keeps only the
    // classes a stored state marks inline, so without this the classes a
    // project was saved with were carried by the file and quietly ignored on
    // the way back in.
    updateSharedModulationModules(savedGraph);
    trackModulationStates_[trackIndex].graph
        = stateWithoutSharedModules(savedGraph);
}

void WorkspaceComponent::saveCurrentModulationState()
{
    if (!selectedTrack_ || *selectedTrack_ >= audio_.trackCount())
        return;
    ensureTrackModulationState(*selectedTrack_);
    auto state = modulationGraph_.serialise();
    updateSharedModulationModules(state);
    trackModulationStates_[*selectedTrack_].graph
        = stateWithoutSharedModules(state);
}

void WorkspaceComponent::restoreModulationStateForTrack(
    const std::size_t trackIndex)
{
    if (trackIndex >= audio_.trackCount())
        return;
    ensureTrackModulationState(trackIndex);
    // Restoring and immediately re-serialising rewrote the stored state with
    // an identical one, and cost a full rebuild of the modulation graph on
    // every selection. It also replaced the stored value with a new object on
    // every click, which is exactly what stops the editor from recognising a
    // class it has already built.
    auto modulationMark = juce::Time::getHighResolutionTicks();
    const auto modulationSince = [&modulationMark] {
        const auto now = juce::Time::getHighResolutionTicks();
        const auto ms = juce::Time::highResolutionTicksToSeconds(
                            now - modulationMark) * 1000.0;
        modulationMark = now;
        return juce::String(ms, 2);
    };
    modulationGraph_.restore(
        modulationStateForEditor(trackModulationStates_[trackIndex].graph));
    const auto modulationRestore = modulationSince();
    refreshModulationTargets();
    restoreModulationProfile_ = "restore=" + modulationRestore
                                + " targets=" + modulationSince();
}

void WorkspaceComponent::refreshModulationTargets()
{
    juce::String previousBinding;
    if (selectedTrack_ && *selectedTrack_ < trackModulationStates_.size()) {
        const auto previousIndex
            = modulationTargetSelector_.getSelectedItemIndex();
        const auto& previousAssignments
            = trackModulationStates_[*selectedTrack_].assignments;
        if (previousIndex >= 0
            && previousIndex < static_cast<int>(previousAssignments.size()))
            previousBinding = previousAssignments[
                static_cast<std::size_t>(previousIndex)].binding;
    }
    modulationTargetSelector_.clear(juce::dontSendNotification);
    if (!selectedTrack_ || *selectedTrack_ >= audio_.trackCount()
        || isMidiNodeGraphContext()) {
        refreshModulationAssignmentControls();
        return;
    }

    ensureTrackModulationState(*selectedTrack_);
    auto& stored = trackModulationStates_[*selectedTrack_].assignments;
    std::vector<ModulationAssignment> next;
    const auto interface = nodeGraph_.graphInterfaceState();
    next.reserve(interface.parameters.size());
    for (const auto& parameter : interface.parameters) {
        auto assignment = ModulationAssignment {
            parameter.key, parameter.label, parameter.value,
            parameter.minimum, parameter.maximum, 0.25, false };
        const auto existing = std::find_if(
            stored.begin(), stored.end(), [&parameter](const auto& value) {
                return value.binding == parameter.key;
            });
        if (existing != stored.end()) {
            assignment = *existing;
            assignment.label = parameter.label;
            assignment.baseValue = parameter.value;
            assignment.minimum = parameter.minimum;
            assignment.maximum = parameter.maximum;
        }
        next.push_back(std::move(assignment));
    }
    stored = std::move(next);

    auto selectedId = 0;
    for (std::size_t index = 0; index < stored.size(); ++index) {
        const auto suffix = stored[index].enabled ? "  •" : juce::String {};
        modulationTargetSelector_.addItem(
            stored[index].label + suffix, static_cast<int>(index) + 1);
        if (stored[index].binding == previousBinding)
            selectedId = static_cast<int>(index) + 1;
    }
    if (selectedId == 0 && !stored.empty())
        selectedId = 1;
    modulationTargetSelector_.setSelectedId(selectedId,
                                              juce::dontSendNotification);
    refreshModulationAssignmentControls();
}

void WorkspaceComponent::refreshModulationAssignmentControls()
{
    const auto hasTrack = selectedTrack_
                          && *selectedTrack_ < trackModulationStates_.size();
    const auto index = modulationTargetSelector_.getSelectedItemIndex();
    const auto valid = hasTrack && index >= 0
                       && index < static_cast<int>(
                              trackModulationStates_[*selectedTrack_]
                                  .assignments.size());
    modulationAmountSlider_.setEnabled(valid);
    modulationEnabledButton_.setEnabled(valid);
    if (!valid) {
        modulationEnabledButton_.setToggleState(
            false, juce::dontSendNotification);
        return;
    }
    const auto& assignment
        = trackModulationStates_[*selectedTrack_]
              .assignments[static_cast<std::size_t>(index)];
    modulationAmountSlider_.setValue(assignment.amount,
                                      juce::dontSendNotification);
    modulationEnabledButton_.setToggleState(
        assignment.enabled, juce::dontSendNotification);
}

void WorkspaceComponent::applyTrackModulations()
{
    const auto time = audio_.position();
    for (std::size_t trackIndex = 0;
         trackIndex < audio_.trackCount()
         && trackIndex < trackModulationStates_.size(); ++trackIndex) {
        const auto& state = trackModulationStates_[trackIndex];
        // Every published parameter has an assignment, nearly all of them
        // off. Copying the track's whole native graph thirty times a second
        // to apply nothing was the cost of not checking.
        const auto anyEnabled = std::ranges::any_of(
            state.assignments,
            [](const auto& assignment) { return assignment.enabled; });
        if (!anyEnabled)
            continue;
        auto graph = audio_.nativeDspGraph(trackIndex);
        if (graph.nodes.empty())
            continue;
        const auto output = evaluateValueGraphState(state.graph, time);
        auto changed = false;
        for (const auto& assignment : state.assignments) {
            const auto range = assignment.maximum - assignment.minimum;
            const auto value = juce::jlimit(
                assignment.minimum, assignment.maximum,
                assignment.baseValue
                    + (assignment.enabled
                           ? output * assignment.amount * range
                           : 0.0));
            changed = applyModulatedNativeParameter(
                          graph, assignment.binding, value)
                      || changed;
        }
        if (changed)
            (void) audio_.updateTrackNativeDspParameters(trackIndex, graph);
    }
}

void WorkspaceComponent::selectTrack(const std::size_t trackIndex)
{
    if (trackIndex >= audio_.trackCount() || trackIndex >= trackLanes_.size())
        return;

    const auto profileStart = juce::Time::getHighResolutionTicks();
    auto profileMark = profileStart;
    const auto profileSince = [&profileMark] {
        const auto now = juce::Time::getHighResolutionTicks();
        const auto ms = juce::Time::highResolutionTicksToSeconds(
                            now - profileMark) * 1000.0;
        profileMark = now;
        return juce::String(ms, 2);
    };
    const auto trackChanged = !selectedTrack_ || *selectedTrack_ != trackIndex;
    launcherInspectorSelection_.reset();
    inspectorSelection_ = InspectorSelection::audioTrack;
    selectedClipEventId_ = 0;
    juce::String profileSaveGraph { "0" };
    juce::String profileSaveModulation { "0" };
    juce::String profileRestoreGraph { "0" };
    juce::String profileRestoreModulation { "0" };
    if (trackChanged) {
        saveCurrentNodeGraphState();
        profileSaveGraph = profileSince();
        saveCurrentModulationState();
        profileSaveModulation = profileSince();
        selectedPluginIndex_ = 0;
        automationTargetId_ = "gain";
        nodeGraph_.clearAnalysis();
        selectedTrack_ = trackIndex;
        sourceEditorEventId_ = 0;
        restoreNodeGraphForTrack(trackIndex);
        profileRestoreGraph = profileSince();
        restoreModulationStateForTrack(trackIndex);
        profileRestoreModulation = profileSince();
    } else {
        selectedTrack_ = trackIndex;
        (void) profileSince();
    }
    const auto clips = audio_.audioClips(trackIndex);
    if (trackChanged)
        selectedAudioClipIds_.clear();
    normaliseAudioClipSelection(clips);
    for (std::size_t index = 0; index < trackLanes_.size(); ++index)
        trackLanes_[index]->setSelected(index == trackIndex);
    const auto profileLanes = profileSince();
    refreshSelectedTrack();
    const auto profileSelected = profileSince();
    refreshAutomationTargets();
    const auto profileAutomation = profileSince();
    refreshTimelineRanges();
    const auto profileRanges = profileSince();
    selectTrackProfile_ = "saveGraph=" + profileSaveGraph
                          + " saveModulation=" + profileSaveModulation
                          + " restoreGraph=" + profileRestoreGraph
                          + " restoreModulation=" + profileRestoreModulation
                          + " lanes=" + profileLanes
                          + " selectedTrack=" + profileSelected
                          + " automationTargets=" + profileAutomation
                          + " ranges=" + profileRanges
                          + " total="
                          + juce::String(
                                juce::Time::highResolutionTicksToSeconds(
                                    juce::Time::getHighResolutionTicks()
                                    - profileStart) * 1000.0, 2);
}

void WorkspaceComponent::selectAudioClip(const std::size_t trackIndex,
                                         const std::uint64_t clipId,
                                         const WaveformComponent::RegionSelectionMode mode)
{
    if (trackIndex >= audio_.trackCount())
        return;
    if (!selectedTrack_ || *selectedTrack_ != trackIndex
        || selectedAudioClipId_ != clipId)
        sourceEditorEventId_ = 0;
    const auto belongsToMarquee = arrangementMarqueeSelectionActive_
        && trackIndex < arrangementMarqueeClipIdsByTrack_.size()
        && std::find(arrangementMarqueeClipIdsByTrack_[trackIndex].begin(),
                     arrangementMarqueeClipIdsByTrack_[trackIndex].end(),
                     clipId)
               != arrangementMarqueeClipIdsByTrack_[trackIndex].end();
    if (belongsToMarquee
        && mode != WaveformComponent::RegionSelectionMode::toggle) {
        selectTrack(trackIndex);
        selectedAudioClipIds_
            = arrangementMarqueeClipIdsByTrack_[trackIndex];
        selectedAudioClipId_ = clipId;
        inspectorSelection_ = InspectorSelection::audioClip;
        selectedClipEventId_ = 0;
        refreshSelectedTrack();
        refreshTimelineRanges();
        return;
    }
    clearArrangementMarqueeSelection();
    if (!selectedTrack_ || *selectedTrack_ != trackIndex)
        selectTrack(trackIndex);
    if (!audio_.audioClip(trackIndex, clipId).has_value())
        return;
    const auto found = std::find(selectedAudioClipIds_.begin(),
                                 selectedAudioClipIds_.end(), clipId);
    if (mode == WaveformComponent::RegionSelectionMode::replace) {
        selectedAudioClipIds_ = { clipId };
        selectedAudioClipId_ = clipId;
    } else if (mode == WaveformComponent::RegionSelectionMode::toggle) {
        if (found == selectedAudioClipIds_.end()) {
            selectedAudioClipIds_.push_back(clipId);
            selectedAudioClipId_ = clipId;
        } else if (selectedAudioClipIds_.size() > 1) {
            selectedAudioClipIds_.erase(found);
            if (selectedAudioClipId_ == clipId)
                selectedAudioClipId_ = selectedAudioClipIds_.front();
        }
    } else {
        if (found == selectedAudioClipIds_.end())
            selectedAudioClipIds_.push_back(clipId);
        selectedAudioClipId_ = clipId;
    }
    inspectorSelection_ = InspectorSelection::audioClip;
    selectedClipEventId_ = 0;
    refreshSelectedTrack();
    refreshTimelineRanges();
    if (currentEditorMode_ == 6)
        refreshClipTimelineEditor();
}

std::size_t WorkspaceComponent::arrangementTrackAtScreenY(
    const int screenY) const noexcept
{
    if (trackLanes_.empty())
        return 0;
    auto nearest = std::size_t {};
    auto nearestDistance = std::numeric_limits<int>::max();
    for (std::size_t index = 0; index < trackLanes_.size(); ++index) {
        const auto bounds = trackLanes_[index]->getScreenBounds();
        if (screenY >= bounds.getY() && screenY <= bounds.getBottom())
            return index;
        const auto distance = std::abs(screenY - bounds.getCentreY());
        if (distance < nearestDistance) {
            nearestDistance = distance;
            nearest = index;
        }
    }
    return nearest;
}

void WorkspaceComponent::showArrangementMovePreview(
    const std::size_t sourceTrack, const std::uint64_t clipId,
    const double startSeconds, const int screenY, const bool active)
{
    for (auto& lane : trackLanes_)
        lane->setMovePreview(std::nullopt);
    arrangementMovePreviewTrack_.reset();
    if (!active || sourceTrack >= audio_.trackCount()
        || trackLanes_.empty())
        return;

    const auto targetTrack = arrangementTrackAtScreenY(screenY);
    if (targetTrack >= trackLanes_.size() || targetTrack == sourceTrack)
        return;
    const auto clip = audio_.audioClip(sourceTrack, clipId);
    if (!clip.has_value())
        return;
    WaveformComponent::Region preview {
        clip->id, clip->name, startSeconds,
        clip->sourceOffsetSeconds, clip->durationSeconds,
        clip->sourceDurationSeconds, juce::Colour(clip->colour),
        clip->sourceFile, clip->sourceLengthSeconds,
        clip->fadeInSeconds, clip->fadeOutSeconds,
        clip->activeTakeIndex, clip->takes.size()
    };
    trackLanes_[targetTrack]->setMovePreview(std::move(preview));
    arrangementMovePreviewTrack_ = targetTrack;
}

bool WorkspaceComponent::moveArrangementSelection(
    const std::size_t sourceTrack,
    const std::uint64_t primaryClipId,
    const double primaryStartSeconds,
    const int screenY)
{
    const auto primaryBefore = audio_.audioClip(sourceTrack, primaryClipId);
    if (!primaryBefore.has_value() || sourceTrack >= audio_.trackCount())
        return false;
    const auto targetTrack = arrangementTrackAtScreenY(screenY);
    if (targetTrack >= audio_.trackCount())
        return false;

    const auto marqueeMove = arrangementMarqueeSelectionActive_
        && sourceTrack < arrangementMarqueeClipIdsByTrack_.size()
        && std::find(arrangementMarqueeClipIdsByTrack_[sourceTrack].begin(),
                     arrangementMarqueeClipIdsByTrack_[sourceTrack].end(),
                     primaryClipId)
               != arrangementMarqueeClipIdsByTrack_[sourceTrack].end();
    std::vector<std::vector<std::uint64_t>> movingIds(
        audio_.trackCount());
    if (marqueeMove) {
        movingIds = arrangementMarqueeClipIdsByTrack_;
        movingIds.resize(audio_.trackCount());
    } else if (selectedTrack_ == sourceTrack
               && std::find(selectedAudioClipIds_.begin(),
                            selectedAudioClipIds_.end(), primaryClipId)
                      != selectedAudioClipIds_.end()) {
        movingIds[sourceTrack] = selectedAudioClipIds_;
    } else {
        movingIds[sourceTrack] = { primaryClipId };
    }

    const auto primaryInSource = std::find(
        movingIds[sourceTrack].begin(), movingIds[sourceTrack].end(),
        primaryClipId);
    if (primaryInSource == movingIds[sourceTrack].end())
        return false;
    const auto primaryIndex = static_cast<std::size_t>(std::distance(
        movingIds[sourceTrack].begin(), primaryInSource));
    const auto trackDelta = static_cast<int>(targetTrack)
                            - static_cast<int>(sourceTrack);
    if (marqueeMove) {
        const auto shiftedFirst = static_cast<int>(
                                      arrangementTimeSelectionFirstTrack_)
                                  + trackDelta;
        const auto shiftedLast = static_cast<int>(
                                     arrangementTimeSelectionLastTrack_)
                                 + trackDelta;
        if (shiftedFirst < 0 || shiftedLast < shiftedFirst
            || shiftedLast >= static_cast<int>(audio_.trackCount()))
            return false;
    }
    const auto move = audio_.moveAudioClipsAcrossTracks(
        movingIds, trackDelta,
        primaryStartSeconds - primaryBefore->startSeconds);
    if (!move.succeeded)
        return false;
    if (!move.changed)
        return true;
    if (targetTrack >= move.clipIdsByTrack.size()
        || move.clipIdsByTrack[targetTrack].empty())
        return false;

    const auto movedPrimaryId = trackDelta == 0
                                    ? primaryClipId
                                    : move.clipIdsByTrack[targetTrack][std::min(
                                          primaryIndex,
                                          move.clipIdsByTrack[targetTrack]
                                                  .size()
                                              - 1)];
    const auto primaryAfter = audio_.audioClip(targetTrack, movedPrimaryId);
    if (!primaryAfter.has_value())
        return false;
    const auto appliedDelta = primaryAfter->startSeconds
                              - primaryBefore->startSeconds;

    if (marqueeMove) {
        arrangementMarqueeClipIdsByTrack_ = move.clipIdsByTrack;
        arrangementMarqueeBaseClipIdsByTrack_.clear();
        arrangementTimeSelectionStartSeconds_ = std::max(
            0.0, arrangementTimeSelectionStartSeconds_ + appliedDelta);
        arrangementTimeSelectionEndSeconds_ = std::max(
            arrangementTimeSelectionStartSeconds_,
            arrangementTimeSelectionEndSeconds_ + appliedDelta);
        arrangementTimeSelectionFirstTrack_ = static_cast<std::size_t>(
            static_cast<int>(arrangementTimeSelectionFirstTrack_)
            + trackDelta);
        arrangementTimeSelectionLastTrack_ = static_cast<std::size_t>(
            static_cast<int>(arrangementTimeSelectionLastTrack_)
            + trackDelta);
    } else {
        clearArrangementMarqueeSelection();
    }

    selectTrack(targetTrack);
    selectedAudioClipIds_ = move.clipIdsByTrack[targetTrack];
    selectedAudioClipId_ = movedPrimaryId;
    refreshTimelineRanges();
    refreshSelectedTrack();
    refreshAutomationEditor();
    refreshAudioEditor();
    refreshClipLauncher();
    markProjectDirty();
    return true;
}

void WorkspaceComponent::handleArrangementMarquee(
    const std::size_t anchorTrack, const double anchorSeconds,
    const double currentSeconds, const int screenY,
    const WaveformComponent::MarqueePhase phase,
    const bool additive)
{
    if (anchorTrack >= audio_.trackCount() || trackLanes_.empty())
        return;
    const auto trackCount = audio_.trackCount();
    if (phase == WaveformComponent::MarqueePhase::begin) {
        arrangementMarqueeDragging_ = true;
        arrangementMarqueeBaseClipIdsByTrack_.assign(trackCount, {});
        if (additive) {
            if (arrangementMarqueeSelectionActive_)
                arrangementMarqueeBaseClipIdsByTrack_
                    = arrangementMarqueeClipIdsByTrack_;
            else if (selectedTrack_ && *selectedTrack_ < trackCount)
                arrangementMarqueeBaseClipIdsByTrack_[*selectedTrack_]
                    = selectedAudioClipIds_;
        }
        arrangementMarqueeClipIdsByTrack_
            = arrangementMarqueeBaseClipIdsByTrack_;
        arrangementMarqueeSelectionActive_ = true;
    }

    const auto currentTrack = std::min(
        arrangementTrackAtScreenY(screenY), trackCount - 1);
    arrangementTimeSelectionFirstTrack_ = std::min(anchorTrack,
                                                   currentTrack);
    arrangementTimeSelectionLastTrack_ = std::max(anchorTrack,
                                                  currentTrack);
    arrangementTimeSelectionStartSeconds_ = std::max(
        0.0, std::min(anchorSeconds, currentSeconds));
    arrangementTimeSelectionEndSeconds_ = std::max(
        arrangementTimeSelectionStartSeconds_,
        std::max(anchorSeconds, currentSeconds));
    if (arrangementMarqueeBaseClipIdsByTrack_.size() != trackCount)
        arrangementMarqueeBaseClipIdsByTrack_.assign(trackCount, {});
    arrangementMarqueeClipIdsByTrack_
        = arrangementMarqueeBaseClipIdsByTrack_;
    arrangementMarqueeClipIdsByTrack_.resize(trackCount);
    for (auto trackIndex = arrangementTimeSelectionFirstTrack_;
         trackIndex <= arrangementTimeSelectionLastTrack_; ++trackIndex) {
        auto& selectedIds = arrangementMarqueeClipIdsByTrack_[trackIndex];
        for (const auto& clip : audio_.audioClips(trackIndex)) {
            const auto clipEnd = clip.startSeconds + clip.durationSeconds;
            if (clip.startSeconds <= arrangementTimeSelectionEndSeconds_
                && clipEnd >= arrangementTimeSelectionStartSeconds_
                && std::find(selectedIds.begin(), selectedIds.end(), clip.id)
                       == selectedIds.end())
                selectedIds.push_back(clip.id);
        }
        std::sort(selectedIds.begin(), selectedIds.end());
    }

    auto primaryTrack = std::min(anchorTrack, trackCount - 1);
    if (arrangementMarqueeClipIdsByTrack_[primaryTrack].empty()) {
        const auto firstSelected = std::find_if(
            arrangementMarqueeClipIdsByTrack_.begin(),
            arrangementMarqueeClipIdsByTrack_.end(),
            [](const auto& ids) { return !ids.empty(); });
        if (firstSelected != arrangementMarqueeClipIdsByTrack_.end())
            primaryTrack = static_cast<std::size_t>(std::distance(
                arrangementMarqueeClipIdsByTrack_.begin(), firstSelected));
    }
    selectedTrack_ = primaryTrack;
    selectedAudioClipIds_
        = arrangementMarqueeClipIdsByTrack_[primaryTrack];
    selectedAudioClipId_ = selectedAudioClipIds_.empty()
                               ? 0
                               : selectedAudioClipIds_.front();
    for (std::size_t index = 0; index < trackLanes_.size(); ++index)
        trackLanes_[index]->setSelected(index == primaryTrack);

    if (phase == WaveformComponent::MarqueePhase::end) {
        arrangementMarqueeDragging_ = false;
        if (arrangementTimeSelectionEndSeconds_
                - arrangementTimeSelectionStartSeconds_
            < 0.001) {
            clearArrangementMarqueeSelection();
            selectedAudioClipIds_.clear();
            selectedAudioClipId_ = 0;
        }
        refreshSelectedTrack();
        refreshAutomationTargets();
    }
    refreshTimelineRanges();
}

void WorkspaceComponent::clearArrangementMarqueeSelection()
{
    arrangementMarqueeSelectionActive_ = false;
    arrangementMarqueeDragging_ = false;
    arrangementMarqueeClipIdsByTrack_.clear();
    arrangementMarqueeBaseClipIdsByTrack_.clear();
    arrangementTimeSelectionStartSeconds_ = 0.0;
    arrangementTimeSelectionEndSeconds_ = 0.0;
    arrangementTimeSelectionFirstTrack_ = 0;
    arrangementTimeSelectionLastTrack_ = 0;
}

void WorkspaceComponent::showArrangementRangeMenu()
{
    const auto canEdit = arrangementMarqueeSelectionActive_
                         && arrangementMarqueeClipCount() > 0
                         && arrangementTimeSelectionEndSeconds_
                                - arrangementTimeSelectionStartSeconds_
                                >= 0.001;
    juce::PopupMenu menu;
    menu.addSectionHeader(tr("arrangement.rangeMenu"));
    menu.addItem(1, tr("arrangement.splitRange"), canEdit);
    menu.addItem(2, tr("arrangement.cropRange"), canEdit);
    menu.addSeparator();
    menu.addItem(3, tr("arrangement.silenceRange"), canEdit);
    const auto safe = juce::Component::SafePointer<WorkspaceComponent>(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options {}
            .withTargetComponent(&arrangementRangeButton_),
        [safe](const int result) {
            if (safe == nullptr || result == 0)
                return;
            const auto operation = result == 1
                ? AudioTrackController::ArrangementRangeOperation::
                      splitAtBoundaries
                : result == 2
                      ? AudioTrackController::ArrangementRangeOperation::
                            cropToRange
                      : AudioTrackController::ArrangementRangeOperation::
                            silenceRange;
            (void) safe->editArrangementMarqueeRange(operation);
        });
}

bool WorkspaceComponent::editArrangementMarqueeRange(
    const AudioTrackController::ArrangementRangeOperation operation)
{
    if (!arrangementMarqueeSelectionActive_
        || arrangementMarqueeClipCount() == 0
        || arrangementTimeSelectionEndSeconds_
                   - arrangementTimeSelectionStartSeconds_
               < 0.001)
        return false;

    audio_.beginArrangementEditBatch();
    std::vector<std::vector<std::uint64_t>> resultingSelection(
        audio_.trackCount());
    auto succeeded = true;
    auto changed = false;
    for (std::size_t trackIndex = 0;
         trackIndex < arrangementMarqueeClipIdsByTrack_.size();
         ++trackIndex) {
        const auto& ids = arrangementMarqueeClipIdsByTrack_[trackIndex];
        if (ids.empty())
            continue;
        const auto result = audio_.editAudioClipRange(
            trackIndex, ids, arrangementTimeSelectionStartSeconds_,
            arrangementTimeSelectionEndSeconds_, operation);
        if (!result.succeeded) {
            succeeded = false;
            break;
        }
        changed = result.changed || changed;
        resultingSelection[trackIndex] = result.affectedClipIds;
    }
    if (!succeeded || !changed) {
        (void) audio_.cancelArrangementEditBatch();
        return false;
    }
    if (!audio_.endArrangementEditBatch())
        return false;

    arrangementMarqueeClipIdsByTrack_ = std::move(resultingSelection);
    arrangementMarqueeBaseClipIdsByTrack_.clear();
    auto primaryTrack = std::min(
        arrangementTimeSelectionFirstTrack_,
        std::max<std::size_t>(0, audio_.trackCount() - 1));
    const auto firstSelected = std::find_if(
        arrangementMarqueeClipIdsByTrack_.begin(),
        arrangementMarqueeClipIdsByTrack_.end(),
        [](const auto& ids) { return !ids.empty(); });
    if (firstSelected != arrangementMarqueeClipIdsByTrack_.end())
        primaryTrack = static_cast<std::size_t>(std::distance(
            arrangementMarqueeClipIdsByTrack_.begin(), firstSelected));
    selectTrack(primaryTrack);
    selectedAudioClipIds_ = arrangementMarqueeClipIdsByTrack_[primaryTrack];
    selectedAudioClipId_ = selectedAudioClipIds_.empty()
                               ? 0
                               : selectedAudioClipIds_.front();
    refreshTimelineRanges();
    refreshSelectedTrack();
    refreshAutomationEditor();
    refreshAudioEditor();
    refreshClipLauncher();
    markProjectDirty();
    return true;
}

std::size_t WorkspaceComponent::arrangementMarqueeClipCount() const noexcept
{
    std::size_t count {};
    for (const auto& ids : arrangementMarqueeClipIdsByTrack_)
        count += ids.size();
    return count;
}

bool WorkspaceComponent::deleteArrangementMarqueeSelection()
{
    if (!arrangementMarqueeSelectionActive_
        || arrangementMarqueeClipCount() == 0)
        return false;
    audio_.beginArrangementEditBatch();
    auto changed = false;
    auto allChanged = true;
    for (std::size_t trackIndex = 0;
         trackIndex < arrangementMarqueeClipIdsByTrack_.size();
         ++trackIndex) {
        const auto& ids = arrangementMarqueeClipIdsByTrack_[trackIndex];
        if (!ids.empty()) {
            const auto removed = audio_.removeAudioClips(trackIndex, ids);
            changed = removed || changed;
            allChanged = removed && allChanged;
        }
    }
    if (!allChanged) {
        (void) audio_.cancelArrangementEditBatch();
        return false;
    }
    const auto committed = audio_.endArrangementEditBatch();
    if (!changed || !committed)
        return false;
    clearArrangementMarqueeSelection();
    if (selectedTrack_ && *selectedTrack_ < audio_.trackCount()) {
        selectedAudioClipIds_.clear();
        selectedAudioClipId_ = 0;
        normaliseAudioClipSelection(audio_.audioClips(*selectedTrack_));
    }
    refreshTimelineRanges();
    refreshSelectedTrack();
    refreshAutomationEditor();
    refreshAudioEditor();
    refreshClipLauncher();
    markProjectDirty();
    return true;
}

bool WorkspaceComponent::copyArrangementMarqueeSelection()
{
    if (!arrangementMarqueeSelectionActive_
        || arrangementMarqueeClipCount() == 0)
        return false;
    auto firstTrack = std::numeric_limits<std::size_t>::max();
    auto firstStart = std::numeric_limits<double>::max();
    auto lastEnd = 0.0;
    for (std::size_t trackIndex = 0;
         trackIndex < arrangementMarqueeClipIdsByTrack_.size();
         ++trackIndex) {
        if (arrangementMarqueeClipIdsByTrack_[trackIndex].empty())
            continue;
        firstTrack = std::min(firstTrack, trackIndex);
        for (const auto id : arrangementMarqueeClipIdsByTrack_[trackIndex]) {
            const auto clip = audio_.audioClip(trackIndex, id);
            if (!clip.has_value())
                return false;
            firstStart = std::min(firstStart, clip->startSeconds);
            lastEnd = std::max(lastEnd,
                               clip->startSeconds + clip->durationSeconds);
        }
    }
    if (firstTrack == std::numeric_limits<std::size_t>::max()
        || firstStart == std::numeric_limits<double>::max())
        return false;

    std::vector<ArrangementClipboardTrack> copiedTracks;
    for (std::size_t trackIndex = firstTrack;
         trackIndex < arrangementMarqueeClipIdsByTrack_.size();
         ++trackIndex) {
        const auto& ids = arrangementMarqueeClipIdsByTrack_[trackIndex];
        if (ids.empty())
            continue;
        ArrangementClipboardTrack copied;
        copied.relativeTrackIndex = trackIndex - firstTrack;
        copied.firstStartSeconds = std::numeric_limits<double>::max();
        copied.clips.reserve(ids.size());
        for (const auto id : ids) {
            const auto clip = audio_.audioClip(trackIndex, id);
            if (!clip.has_value())
                return false;
            copied.firstStartSeconds = std::min(
                copied.firstStartSeconds, clip->startSeconds);
            copied.clips.push_back(*clip);
        }
        copiedTracks.push_back(std::move(copied));
    }
    arrangementMarqueeClipboard_ = std::move(copiedTracks);
    arrangementMarqueeClipboardStartSeconds_ = firstStart;
    arrangementMarqueeClipboardDurationSeconds_ = std::max(
        0.001, lastEnd - firstStart);
    audioClipClipboard_.clear();
    return true;
}

bool WorkspaceComponent::cutArrangementMarqueeSelection()
{
    return copyArrangementMarqueeSelection()
           && deleteArrangementMarqueeSelection();
}

bool WorkspaceComponent::pasteArrangementMarqueeClipboard(
    const std::size_t targetTrack, const double timelineSeconds)
{
    if (arrangementMarqueeClipboard_.empty()
        || targetTrack >= audio_.trackCount())
        return false;
    const auto anchor = snapArrangementTime(timelineSeconds);
    for (const auto& copied : arrangementMarqueeClipboard_) {
        const auto destinationTrack = targetTrack
                                      + copied.relativeTrackIndex;
        if (destinationTrack >= audio_.trackCount()
            || audio_.audioClips(destinationTrack).size()
                       + copied.clips.size()
                   > AudioTrackController::maximumAudioClipsPerTrack)
            return false;
    }

    audio_.beginArrangementEditBatch();
    std::vector<std::vector<std::uint64_t>> insertedByTrack(
        audio_.trackCount());
    auto insertedAll = true;
    for (const auto& copied : arrangementMarqueeClipboard_) {
        const auto destinationTrack = targetTrack
                                      + copied.relativeTrackIndex;
        const auto groupDestination = anchor
            + std::max(0.0, copied.firstStartSeconds
                                - arrangementMarqueeClipboardStartSeconds_);
        auto inserted = audio_.insertAudioClips(
            destinationTrack, copied.clips, groupDestination);
        if (inserted.size() != copied.clips.size()) {
            insertedAll = false;
            break;
        }
        insertedByTrack[destinationTrack] = std::move(inserted);
    }
    if (!insertedAll) {
        (void) audio_.cancelArrangementEditBatch();
        return false;
    }
    const auto committed = audio_.endArrangementEditBatch();
    if (!committed)
        return false;

    arrangementMarqueeSelectionActive_ = true;
    arrangementMarqueeDragging_ = false;
    arrangementMarqueeClipIdsByTrack_ = std::move(insertedByTrack);
    arrangementMarqueeBaseClipIdsByTrack_.clear();
    arrangementTimeSelectionStartSeconds_ = anchor;
    arrangementTimeSelectionEndSeconds_ = anchor
        + arrangementMarqueeClipboardDurationSeconds_;
    arrangementTimeSelectionFirstTrack_ = targetTrack;
    arrangementTimeSelectionLastTrack_ = targetTrack;
    for (const auto& copied : arrangementMarqueeClipboard_)
        arrangementTimeSelectionLastTrack_ = std::max(
            arrangementTimeSelectionLastTrack_,
            targetTrack + copied.relativeTrackIndex);
    selectTrack(targetTrack);
    selectedAudioClipIds_
        = arrangementMarqueeClipIdsByTrack_[targetTrack];
    selectedAudioClipId_ = selectedAudioClipIds_.empty()
                               ? 0
                               : selectedAudioClipIds_.front();
    return true;
}

void WorkspaceComponent::normaliseAudioClipSelection(
    const std::vector<AudioTrackController::AudioClip>& clips)
{
    selectedAudioClipIds_.erase(
        std::remove_if(selectedAudioClipIds_.begin(),
                       selectedAudioClipIds_.end(), [&clips](const auto id) {
                           return std::none_of(
                               clips.begin(), clips.end(),
                               [id](const auto& clip) { return clip.id == id; });
                       }),
        selectedAudioClipIds_.end());
    std::sort(selectedAudioClipIds_.begin(), selectedAudioClipIds_.end());
    selectedAudioClipIds_.erase(
        std::unique(selectedAudioClipIds_.begin(),
                    selectedAudioClipIds_.end()),
        selectedAudioClipIds_.end());
    const auto primaryValid = std::find(
                                  selectedAudioClipIds_.begin(),
                                  selectedAudioClipIds_.end(),
                                  selectedAudioClipId_)
                              != selectedAudioClipIds_.end();
    if (selectedAudioClipIds_.empty() && !clips.empty())
        selectedAudioClipIds_.push_back(clips.front().id);
    if (!primaryValid)
        selectedAudioClipId_ = selectedAudioClipIds_.empty()
                                   ? 0
                                   : selectedAudioClipIds_.front();
}

void WorkspaceComponent::refreshArrangementGrid()
{
    for (auto& lane : trackLanes_)
        lane->setMusicalGrid(audio_.tempo(), arrangementGridDivision(),
                             arrangementSnappingEnabled());
    refreshArrangementInteractionMode();
}

void WorkspaceComponent::refreshArrangementInteractionMode()
{
    auto editingTool = WaveformComponent::EditingTool::pointer;
    auto rulerTool = TimelineRulerComponent::NavigationTool::pointer;
    switch (arrangementToolSelector_.getSelectedId()) {
        case 2:
            editingTool = WaveformComponent::EditingTool::knife;
            break;
        case 3:
            editingTool = WaveformComponent::EditingTool::stretch;
            break;
        case 4:
            editingTool = WaveformComponent::EditingTool::zoom;
            rulerTool = TimelineRulerComponent::NavigationTool::zoom;
            break;
        case 5:
            editingTool = WaveformComponent::EditingTool::hand;
            rulerTool = TimelineRulerComponent::NavigationTool::hand;
            break;
        default:
            break;
    }

    auto snapMode = WaveformComponent::SnapMode::grid;
    if (snapTypeSelector_.getSelectedId() == 2)
        snapMode = WaveformComponent::SnapMode::relative;
    else if (snapTypeSelector_.getSelectedId() == 3)
        snapMode = WaveformComponent::SnapMode::events;

    for (auto& lane : trackLanes_) {
        lane->setEditingTool(editingTool);
        lane->setSnapMode(snapMode);
    }
    timelineRuler_.setNavigationTool(rulerTool);

    const auto selectedTool = juce::jlimit(
        1, 5, arrangementToolSelector_.getSelectedId());
    std::array<juce::TextButton*, 5> toolButtons {
        &timelinePointerButton_, &timelineKnifeButton_,
        &timelineStretchButton_, &timelineZoomButton_,
        &timelineHandButton_
    };
    for (std::size_t index = 0; index < toolButtons.size(); ++index)
        toolButtons[index]->setToggleState(
            selectedTool == static_cast<int>(index + 1),
            juce::dontSendNotification);
    timelineSnapSelector_.setSelectedId(
        snapSelector_.getSelectedId(), juce::dontSendNotification);
    timelineSnapTypeSelector_.setSelectedId(
        snapTypeSelector_.getSelectedId(), juce::dontSendNotification);
    timelineModeLabel_.setText(
        arrangementToolSelector_.getText() + "  •  "
            + timelineSnapSelector_.getText() + "  •  "
            + timelineSnapTypeSelector_.getText() + "  •  "
            + tr(arrangementPlaybackButton_.getToggleState()
                     ? "transport.arrangementOn"
                     : "transport.launcherOnly")
            + (launcherActionRecordButton_.getToggleState()
                   ? "  •  " + tr("launcher.actionsRecording")
                   : launcherActionReplayButton_.getToggleState()
                         ? "  •  " + tr("launcher.actionsReplaying")
                         : juce::String {}),
        juce::dontSendNotification);
}

int WorkspaceComponent::arrangementGridDivision() const noexcept
{
    switch (snapSelector_.getSelectedId()) {
        case 2: return 4;
        case 3: return 8;
        case 5: return 32;
        case 4:
        default: return 16;
    }
}

bool WorkspaceComponent::arrangementSnappingEnabled() const noexcept
{
    return snapSelector_.getSelectedId() != 1;
}

double WorkspaceComponent::snapArrangementTime(
    const double seconds) const noexcept
{
    const auto safe = std::max(0.0, seconds);
    if (!arrangementSnappingEnabled() || audio_.tempo() <= 0.0)
        return safe;
    const auto interval = 60.0 / audio_.tempo()
                          * 4.0
                          / static_cast<double>(arrangementGridDivision());
    return interval > 0.0 ? std::round(safe / interval) * interval
                          : safe;
}

void WorkspaceComponent::setLoopRangeToAudioSelection()
{
    if (arrangementMarqueeSelectionActive_
        && arrangementTimeSelectionEndSeconds_
               > arrangementTimeSelectionStartSeconds_) {
        audio_.setLoopRange(
            snapArrangementTime(arrangementTimeSelectionStartSeconds_),
            snapArrangementTime(arrangementTimeSelectionEndSeconds_));
        loopButton_.setToggleState(true, juce::dontSendNotification);
        audio_.setLooping(true);
        refreshTimelineRanges();
        markProjectDirty();
        return;
    }
    if (!selectedTrack_ || selectedAudioClipIds_.empty())
        return;
    const auto clips = audio_.audioClips(*selectedTrack_);
    auto start = std::numeric_limits<double>::max();
    auto end = 0.0;
    for (const auto id : selectedAudioClipIds_) {
        const auto found = std::find_if(
            clips.begin(), clips.end(), [id](const auto& clip) {
                return clip.id == id;
            });
        if (found == clips.end())
            continue;
        start = std::min(start, found->startSeconds);
        end = std::max(end, found->startSeconds + found->durationSeconds);
    }
    if (start == std::numeric_limits<double>::max() || end <= start)
        return;
    start = snapArrangementTime(start);
    end = snapArrangementTime(end);
    if (end <= start) {
        const auto interval = arrangementSnappingEnabled()
                                  ? 60.0 / audio_.tempo() * 4.0
                                        / static_cast<double>(
                                            arrangementGridDivision())
                                  : 0.001;
        end = start + std::max(0.001, interval);
    }
    audio_.setLoopRange(start, end);
    loopButton_.setToggleState(true, juce::dontSendNotification);
    audio_.setLooping(true);
    refreshTimelineRanges();
    markProjectDirty();
}

void WorkspaceComponent::resetLoopRangeToProject()
{
    audio_.resetLoopRange();
    refreshTimelineRanges();
    markProjectDirty();
}

void WorkspaceComponent::copyAudioClipSelection(
    const std::size_t trackIndex,
    const std::vector<std::uint64_t>& clipIds)
{
    if (trackIndex >= audio_.trackCount() || clipIds.empty())
        return;
    const auto clips = audio_.audioClips(trackIndex);
    std::vector<AudioTrackController::AudioClip> copied;
    copied.reserve(clipIds.size());
    for (const auto id : clipIds) {
        const auto found = std::find_if(
            clips.begin(), clips.end(), [id](const auto& clip) {
                return clip.id == id;
            });
        if (found == clips.end())
            return;
        copied.push_back(*found);
    }
    audioClipClipboard_ = std::move(copied);
    arrangementMarqueeClipboard_.clear();
    arrangementMarqueeClipboardStartSeconds_ = 0.0;
    arrangementMarqueeClipboardDurationSeconds_ = 0.0;
}

bool WorkspaceComponent::pasteAudioClipSelection(
    const std::size_t trackIndex,
    const double timelineSeconds)
{
    if (!arrangementMarqueeClipboard_.empty())
        return pasteArrangementMarqueeClipboard(trackIndex,
                                                 timelineSeconds);
    if (trackIndex >= audio_.trackCount() || audioClipClipboard_.empty())
        return false;
    auto inserted = audio_.insertAudioClips(
        trackIndex, audioClipClipboard_,
        snapArrangementTime(timelineSeconds));
    if (inserted.empty())
        return false;
    clearArrangementMarqueeSelection();
    selectedAudioClipIds_ = std::move(inserted);
    selectedAudioClipId_ = selectedAudioClipIds_.front();
    return true;
}

void WorkspaceComponent::handleAudioClipCommand(
    const std::size_t trackIndex,
    const std::uint64_t clipId,
    const WaveformComponent::RegionCommand command,
    const double timelineSeconds)
{
    const auto grouped = selectedTrack_ == trackIndex
                         && selectedAudioClipIds_.size() > 1
                         && std::find(selectedAudioClipIds_.begin(),
                                      selectedAudioClipIds_.end(), clipId)
                                != selectedAudioClipIds_.end();
    selectAudioClip(
        trackIndex, clipId,
        grouped ? WaveformComponent::RegionSelectionMode::makePrimary
                : WaveformComponent::RegionSelectionMode::replace);
    const auto commandClipIds = grouped
                                    ? selectedAudioClipIds_
                                    : std::vector<std::uint64_t> { clipId };
    if (command == WaveformComponent::RegionCommand::copy) {
        copyAudioClipSelection(trackIndex, commandClipIds);
        return;
    }
    if (command == WaveformComponent::RegionCommand::loopSelection) {
        setLoopRangeToAudioSelection();
        return;
    }
    auto changed = false;
    if (command == WaveformComponent::RegionCommand::paste) {
        changed = pasteAudioClipSelection(trackIndex, timelineSeconds);
    } else if (command == WaveformComponent::RegionCommand::cut) {
        copyAudioClipSelection(trackIndex, commandClipIds);
        changed = commandClipIds.size() > 1
                      ? audio_.removeAudioClips(trackIndex, commandClipIds)
                      : audio_.removeAudioClip(trackIndex, clipId);
        if (changed)
            normaliseAudioClipSelection(audio_.audioClips(trackIndex));
    } else if (command == WaveformComponent::RegionCommand::duplicate) {
        if (commandClipIds.size() > 1) {
            auto duplicates = audio_.duplicateAudioClips(trackIndex,
                                                         commandClipIds);
            if (!duplicates.empty()) {
                const auto primary = std::find(commandClipIds.begin(),
                                               commandClipIds.end(), clipId);
                const auto primaryIndex = primary != commandClipIds.end()
                                              ? static_cast<std::size_t>(
                                                    std::distance(
                                                        commandClipIds.begin(),
                                                        primary))
                                              : 0;
                selectedAudioClipIds_ = std::move(duplicates);
                selectedAudioClipId_ = selectedAudioClipIds_[std::min(
                    primaryIndex, selectedAudioClipIds_.size() - 1)];
                changed = true;
            }
        } else {
            const auto duplicate = audio_.duplicateAudioClip(trackIndex,
                                                              clipId);
            if (duplicate != 0) {
                selectedAudioClipIds_ = { duplicate };
                selectedAudioClipId_ = duplicate;
                changed = true;
            }
        }
    } else if (command == WaveformComponent::RegionCommand::split) {
        const auto right = audio_.splitAudioClip(trackIndex, clipId,
                                                 snapArrangementTime(
                                                     timelineSeconds));
        if (right != 0) {
            selectedAudioClipIds_ = { right };
            selectedAudioClipId_ = right;
            changed = true;
        }
    } else if (command == WaveformComponent::RegionCommand::crossfade) {
        changed = audio_.autoCrossfadeAudioClip(trackIndex, clipId);
    } else if (command == WaveformComponent::RegionCommand::remove) {
        changed = commandClipIds.size() > 1
                      ? audio_.removeAudioClips(trackIndex, commandClipIds)
                      : audio_.removeAudioClip(trackIndex, clipId);
        if (changed) {
            const auto clips = audio_.audioClips(trackIndex);
            normaliseAudioClipSelection(clips);
        }
    }
    if (!changed)
        return;
    refreshTimelineRanges();
    refreshSelectedTrack();
    refreshAutomationEditor();
    refreshClipLauncher();
    markProjectDirty();
}

void WorkspaceComponent::refreshSelectedTrack()
{
    const auto hasSelection = selectedTrack_ && *selectedTrack_ < audio_.trackCount();
    gainSlider_.setEnabled(hasSelection);
    panSlider_.setEnabled(hasSelection);
    muteButton_.setEnabled(hasSelection);
    soloButton_.setEnabled(hasSelection);
    armButton_.setEnabled(hasSelection && !audio_.isRecording());
    const auto selectedClip = hasSelection
        ? audio_.audioClip(*selectedTrack_, selectedAudioClipId_)
        : std::optional<AudioTrackController::AudioClip> {};
    const auto selectedEvent
        = inspectorSelection_ == InspectorSelection::audioEvent
              ? selectedAudioEvent()
              : std::optional<AudioTrackController::AudioEvent> {};
    const auto hasClipSelection = selectedClip.has_value();
    const auto hasAudioSelection
        = hasClipSelection || selectedEvent.has_value();
    clipStartSlider_.setEnabled(hasAudioSelection);
    sourceOffsetSlider_.setEnabled(hasAudioSelection);
    clipDurationSlider_.setEnabled(hasAudioSelection);
    clipStretchSlider_.setEnabled(hasAudioSelection);
    sampleGainSlider_.setEnabled(selectedEvent.has_value());
    sampleFadeInSlider_.setEnabled(selectedEvent.has_value());
    sampleFadeOutSlider_.setEnabled(selectedEvent.has_value());
    sourceTempoSlider_.setEnabled(hasAudioSelection
                                  && !tempoDetectionInProgress_);
    stretchModeSelector_.setEnabled(hasAudioSelection
                                    && !tempoDetectionInProgress_);
    detectTempoButton_.setEnabled(hasClipSelection
                                  && !tempoDetectionInProgress_
                                  && !audioEditInProgress_);
    syncTempoButton_.setEnabled(hasClipSelection
                                && selectedClip->sourceTempoBpm > 0.0
                                && !tempoDetectionInProgress_);
    refreshAudioTakeControls();

    if (!hasSelection) {
        gainSlider_.setValue(0.0, juce::dontSendNotification);
        panSlider_.setValue(0.0, juce::dontSendNotification);
        muteButton_.setToggleState(false, juce::dontSendNotification);
        soloButton_.setToggleState(false, juce::dontSendNotification);
        armButton_.setToggleState(false, juce::dontSendNotification);
        nodeGraph_.setEffectParameters(0.0f, 20'000.0f, false);
        deviceChain_.setParameters(0.0f, 20'000.0f, 0.0f, 0.0f, false);
        // A MIDI track selected instead: its gain and pan, live, in the
        // inspector and the chain.
        if (selectedMidiTrackId_ != 0)
            refreshMidiTrackMixerControls();
        else
            nodeGraph_.setContextName({});
        deviceChain_.setPluginChain({}, 0);
        clipStartSlider_.setValue(0.0, juce::dontSendNotification);
        sourceOffsetSlider_.setValue(0.0, juce::dontSendNotification);
        clipDurationSlider_.setValue(0.001, juce::dontSendNotification);
        clipStretchSlider_.setValue(0.001, juce::dontSendNotification);
        sourceTempoSlider_.setValue(0.0, juce::dontSendNotification);
        stretchModeSelector_.setSelectedId(1, juce::dontSendNotification);
        sampleGainSlider_.setValue(0.0, juce::dontSendNotification);
        sampleFadeInSlider_.setValue(0.0, juce::dontSendNotification);
        sampleFadeOutSlider_.setValue(0.0, juce::dontSendNotification);
        audioEditor_.clear();
        updateTrackDescription();
        return;
    }

    const auto info = audio_.trackInfo(*selectedTrack_);
    gainSlider_.setValue(info.gainDb, juce::dontSendNotification);
    panSlider_.setValue(info.pan, juce::dontSendNotification);
    muteButton_.setToggleState(info.muted, juce::dontSendNotification);
    soloButton_.setToggleState(info.solo, juce::dontSendNotification);
    armButton_.setToggleState(info.armed, juce::dontSendNotification);
    nodeGraph_.setEffectParameters(info.driveDb, info.filterCutoffHz, true);
    deviceChain_.setParameters(info.driveDb, info.filterCutoffHz, info.gainDb, info.pan, true);
    refreshDevicePluginChain();
    if (selectedEvent.has_value()) {
        const auto sourceLength = std::max(
            selectedEvent->sourceOffsetSeconds
                + selectedEvent->sourceDurationSeconds,
            selectedEvent->sourceLengthSeconds);
        clipStartSlider_.setRange(
            0.0, std::max(600.0,
                          clipTimelineEditor_.timelineLengthSeconds() + 60.0),
            0.001);
        sourceOffsetSlider_.setRange(
            0.0, std::max(0.001, sourceLength - 0.001), 0.001);
        clipDurationSlider_.setRange(
            0.001,
            std::max(0.001, sourceLength
                                - selectedEvent->sourceOffsetSeconds),
            0.001);
        clipStretchSlider_.setRange(
            0.001,
            std::max(600.0,
                     clipTimelineEditor_.timelineLengthSeconds() + 60.0),
            0.001);
        clipStartSlider_.setValue(selectedEvent->startSeconds,
                                  juce::dontSendNotification);
        sourceOffsetSlider_.setValue(
            selectedEvent->sourceOffsetSeconds,
            juce::dontSendNotification);
        // Source duration and timeline duration are deliberately separate.
        // This makes a stretch explicit instead of visually trimming the
        // waveform as if source material had disappeared.
        clipDurationSlider_.setValue(
            selectedEvent->sourceDurationSeconds,
            juce::dontSendNotification);
        clipStretchSlider_.setValue(selectedEvent->durationSeconds,
                                    juce::dontSendNotification);
        sampleGainSlider_.setValue(selectedEvent->gainDb,
                                   juce::dontSendNotification);
        sampleFadeInSlider_.setRange(
            0.0, std::max(0.001, selectedEvent->durationSeconds), 0.001);
        sampleFadeOutSlider_.setRange(
            0.0, std::max(0.001, selectedEvent->durationSeconds), 0.001);
        sampleFadeInSlider_.setValue(selectedEvent->fadeInSeconds,
                                     juce::dontSendNotification);
        sampleFadeOutSlider_.setValue(selectedEvent->fadeOutSeconds,
                                      juce::dontSendNotification);
        sourceTempoSlider_.setValue(selectedEvent->sourceTempoBpm,
                                    juce::dontSendNotification);
        stretchModeSelector_.setSelectedId(
            static_cast<int>(selectedEvent->stretchMode) + 1,
            juce::dontSendNotification);
    } else if (selectedClip.has_value()) {
        clipStartSlider_.setRange(0.0, std::max(600.0, audio_.length() + 60.0), 0.001);
        sourceOffsetSlider_.setRange(0.0,
                                     std::max(0.001, selectedClip->sourceLengthSeconds - 0.001),
                                     0.001);
        clipDurationSlider_.setRange(
            0.001,
            std::max(0.001, selectedClip->sourceLengthSeconds
                                - selectedClip->sourceOffsetSeconds),
            0.001);
        clipStretchSlider_.setRange(
            0.001, std::max(600.0, audio_.length() + 60.0), 0.001);
        clipStartSlider_.setValue(selectedClip->startSeconds,
                                  juce::dontSendNotification);
        sourceOffsetSlider_.setValue(selectedClip->sourceOffsetSeconds,
                                     juce::dontSendNotification);
        clipDurationSlider_.setValue(selectedClip->durationSeconds,
                                     juce::dontSendNotification);
        clipStretchSlider_.setValue(selectedClip->durationSeconds,
                                    juce::dontSendNotification);
        sourceTempoSlider_.setValue(selectedClip->sourceTempoBpm,
                                    juce::dontSendNotification);
        stretchModeSelector_.setSelectedId(
            static_cast<int>(selectedClip->stretchMode) + 1,
            juce::dontSendNotification);
        sampleGainSlider_.setValue(0.0, juce::dontSendNotification);
        sampleFadeInSlider_.setValue(0.0, juce::dontSendNotification);
        sampleFadeOutSlider_.setValue(0.0, juce::dontSendNotification);
    } else {
        clipStartSlider_.setValue(0.0, juce::dontSendNotification);
        sourceOffsetSlider_.setValue(0.0, juce::dontSendNotification);
        clipDurationSlider_.setValue(0.001, juce::dontSendNotification);
        clipStretchSlider_.setValue(0.001, juce::dontSendNotification);
        sourceTempoSlider_.setValue(0.0, juce::dontSendNotification);
        stretchModeSelector_.setSelectedId(1, juce::dontSendNotification);
        sampleGainSlider_.setValue(0.0, juce::dontSendNotification);
        sampleFadeInSlider_.setValue(0.0, juce::dontSendNotification);
        sampleFadeOutSlider_.setValue(0.0, juce::dontSendNotification);
    }
    trackLanes_[*selectedTrack_]->setMixerState(info.muted, info.solo,
                                                info.armed);
    refreshAudioEditor();
    updateTrackDescription();
}

void WorkspaceComponent::applySelectedClipStretchSettings()
{
    if (inspectorSelection_ == InspectorSelection::audioEvent
        && selectedClipEventId_ != 0) {
        applySelectedAudioEventProperties();
        return;
    }
    if (!selectedTrack_ || selectedAudioClipId_ == 0
        || tempoDetectionInProgress_)
        return;
    const auto mode = static_cast<AudioTrackController::StretchMode>(
        juce::jlimit(1, 4, stretchModeSelector_.getSelectedId()) - 1);
    if (!audio_.setAudioClipStretchSettings(
            *selectedTrack_, selectedAudioClipId_,
            sourceTempoSlider_.getValue(), mode))
        return;
    refreshTimelineRanges();
    refreshAudioEditor();
    updateTrackDescription();
    markProjectDirty();
}

std::optional<AudioTrackController::AudioEvent>
WorkspaceComponent::selectedAudioEvent() const
{
    if (selectedClipEventId_ == 0)
        return std::nullopt;
    if (launcherEditorContext_) {
        const auto context = *launcherEditorContext_;
        const auto clip = audio_.launcherClip(context.trackIndex,
                                               context.sceneIndex);
        if (!clip.has_value())
            return std::nullopt;
        const auto found = std::find_if(
            clip->events.begin(), clip->events.end(), [this](const auto& event) {
                return event.id == selectedClipEventId_;
            });
        if (found != clip->events.end())
            return *found;
        if (!clip->usesEventTimeline
            && selectedClipEventId_ == launcherClipEditorEventId
            && clip->sourceFile != juce::File {})
            return AudioTrackController::AudioEvent {
                launcherClipEditorEventId, clip->name, 0.0,
                clip->sourceOffsetSeconds, clip->durationSeconds,
                clip->durationSeconds, clip->colour, clip->sourceFile,
                clip->sourceOffsetSeconds + clip->durationSeconds
            };
        return std::nullopt;
    }
    if (!selectedTrack_ || selectedAudioClipId_ == 0)
        return std::nullopt;
    const auto events = audio_.audioClipEvents(
        *selectedTrack_, selectedAudioClipId_);
    const auto found = std::find_if(
        events.begin(), events.end(), [this](const auto& event) {
            return event.id == selectedClipEventId_;
        });
    return found != events.end()
               ? std::optional<AudioTrackController::AudioEvent>(*found)
               : std::nullopt;
}

void WorkspaceComponent::applySelectedAudioEventProperties()
{
    const auto event = selectedAudioEvent();
    if (!event.has_value())
        return;
    const auto mode = static_cast<AudioTrackController::StretchMode>(
        juce::jlimit(1, 4, stretchModeSelector_.getSelectedId()) - 1);
    auto changed = false;
    if (launcherEditorContext_) {
        const auto context = *launcherEditorContext_;
        changed = audio_.setLauncherClipEventProperties(
            context.trackIndex, context.sceneIndex, event->id,
            static_cast<float>(sampleGainSlider_.getValue()),
            sourceTempoSlider_.getValue(), mode);
    } else if (selectedTrack_) {
        changed = audio_.setAudioClipEventProperties(
            *selectedTrack_, selectedAudioClipId_, event->id,
            static_cast<float>(sampleGainSlider_.getValue()),
            sourceTempoSlider_.getValue(), mode);
    }
    if (!changed)
        return;
    refreshActiveClipEditorPreview();
    refreshTimelineRanges();
    refreshClipTimelineEditor();
    updateTrackDescription();
    markProjectDirty();
}

void WorkspaceComponent::refreshActiveClipEditorPreview()
{
    if (currentEditorMode_ == 2 && selectedMidiTrackId_ != 0
        && selectedMidiClipId_ != 0) {
        const auto previewPosition
            = audio_.midiClipEditorPreviewPositionSeconds(
                selectedMidiTrackId_);
        if (previewPosition >= 0.0)
            clipEditorCueSeconds_ = previewPosition;
        return;
    }
    if (launcherEditorContext_) {
        const auto context = *launcherEditorContext_;
        if (audio_.activeLauncherScene(context.trackIndex)
            != context.sceneIndex)
            return;
        const auto position = audio_.launcherClipPositionSeconds(
            context.trackIndex);
        clipEditorCueSeconds_ = position;
        // Rebuilding the launcher plan now preserves its active sample
        // position, so no re-launch (and no audible retrigger) is needed.
        return;
    }

    if (!selectedTrack_ || selectedAudioClipId_ == 0
        || !audio_.isAudioClipPreviewing())
        return;
    const auto position = audio_.audioClipPreviewPositionSeconds();
    clipEditorCueSeconds_ = position;
    (void) audio_.refreshAudioClipPreview(
        *selectedTrack_, selectedAudioClipId_);
}

void WorkspaceComponent::beginContinuousArrangementEdit()
{
    if (continuousArrangementEditActive_)
        return;
    audio_.beginArrangementEditBatch();
    continuousArrangementEditActive_ = true;
}

void WorkspaceComponent::endContinuousArrangementEdit()
{
    if (!continuousArrangementEditActive_)
        return;
    continuousArrangementEditActive_ = false;
    if (audio_.endArrangementEditBatch())
        markProjectDirty();
}

void WorkspaceComponent::detectSelectedClipTempo()
{
    if (tempoDetectionInProgress_ || audioEditInProgress_
        || !selectedTrack_ || selectedAudioClipId_ == 0)
        return;
    const auto trackIndex = *selectedTrack_;
    const auto clipId = selectedAudioClipId_;
    const auto clip = audio_.audioClip(trackIndex, clipId);
    if (!clip.has_value() || clip->sourceFile == juce::File {})
        return;
    const auto source = clip->sourceFile;
    tempoDetectionInProgress_ = true;
    detectTempoButton_.setEnabled(false);
    detectTempoButton_.setButtonText("...");
    const juce::Component::SafePointer<WorkspaceComponent> safeThis(this);
    audioEditPool_.addJob([safeThis, trackIndex, clipId, source] {
        const auto result = AudioTrackController::detectAudioTempo(source);
        juce::MessageManager::callAsync(
            [safeThis, trackIndex, clipId, source, result] {
                auto* const workspace = safeThis.getComponent();
                if (workspace == nullptr)
                    return;
                workspace->tempoDetectionInProgress_ = false;
                const auto current = workspace->audio_.audioClip(
                    trackIndex, clipId);
                if (result.has_value() && current.has_value()
                    && current->sourceFile == source) {
                    (void) workspace->audio_.setAudioClipStretchSettings(
                        trackIndex, clipId, result->bpm,
                        current->stretchMode);
                    workspace->markProjectDirty();
                }
                workspace->refreshTranslations();
                workspace->refreshSelectedTrack();
                workspace->refreshTimelineRanges();
                if (!result.has_value())
                    workspace->showError(
                        tr("error.tempoDetectTitle"),
                        tr("error.tempoDetectFailed"));
            });
    });
}

void WorkspaceComponent::syncSelectedClipToTempo()
{
    if (!selectedTrack_ || selectedAudioClipId_ == 0)
        return;
    const auto clip = audio_.audioClip(*selectedTrack_,
                                       selectedAudioClipId_);
    if (!clip.has_value() || clip->sourceTempoBpm <= 0.0
        || audio_.tempo() <= 0.0)
        return;
    const auto sourceDuration = clip->sourceDurationSeconds > 0.0
                                    ? clip->sourceDurationSeconds
                                    : clip->durationSeconds;
    const auto targetDuration = juce::jlimit(
        0.001, 86'400.0,
        sourceDuration * clip->sourceTempoBpm / audio_.tempo());
    if (!audio_.stretchAudioClip(*selectedTrack_, selectedAudioClipId_,
                                 targetDuration))
        return;
    refreshTimelineRanges();
    refreshSelectedTrack();
    refreshAutomationEditor();
    refreshAudioEditor();
    updateTrackDescription();
    markProjectDirty();
}

void WorkspaceComponent::ensureTrackNodeGraphState(const std::size_t trackIndex)
{
    if (trackNodeGraphStates_.size() <= trackIndex)
        trackNodeGraphStates_.resize(trackIndex + 1);
    if (!isNodeGraphState(trackNodeGraphStates_[trackIndex]))
        trackNodeGraphStates_[trackIndex]
            = stateWithoutSharedModules(factoryNodeGraphState_);
    applyTrackIoToGraphState(trackIndex);
}

bool WorkspaceComponent::applyTrackIoToGraphState(
    const std::size_t trackIndex)
{
    if (trackIndex >= trackNodeGraphStates_.size()
        || !isNodeGraphState(trackNodeGraphStates_[trackIndex]))
        return false;
    const auto channels = trackIndex < trackPresentations_.size()
                              ? juce::jlimit(
                                    1, 16,
                                    trackPresentations_[trackIndex]
                                        .audioChannels)
                              : 2;
    const auto inputTitle = channels == 1
                                ? tr("graph.inputMono")
                                : channels == 2
                                      ? tr("graph.inputStereo")
                                      : tr("graph.inputChannels", "count",
                                           juce::String(channels));
    const auto outputTitle = channels == 1
                                 ? tr("graph.outputMono")
                                 : channels == 2
                                       ? tr("graph.outputStereo")
                                       : tr("graph.outputChannels", "count",
                                            juce::String(channels));
    const auto* graph = trackNodeGraphStates_[trackIndex].getDynamicObject();
    const auto* nodes = graph != nullptr
                            ? graph->getProperty(jsonKeys::nodes).getArray() : nullptr;
    if (nodes == nullptr)
        return false;
    const auto titleFor = [&inputTitle, &outputTitle](const juce::String& type)
        -> const juce::String* {
        if (type == "audio-clips" || type.startsWith("graph-audio-input-"))
            return &inputTitle;
        if (type == "master" || type.startsWith("graph-audio-output-"))
            return &outputTitle;
        return nullptr;
    };
    const auto wants = [&titleFor](const juce::DynamicObject* node) {
        return node != nullptr
                   ? titleFor(node->getProperty(jsonKeys::type).toString())
                   : nullptr;
    };
    // Nothing is written in place. A stored graph is handed to the autosave's
    // background writer, and a title changed underneath it while it
    // serialises is a race; the deep copy the document makes exists to guard
    // exactly this. When a title really does change the state is replaced,
    // which also keeps its identity meaningful as an unchanged signal.
    auto changed = false;
    for (const auto& value : *nodes) {
        const auto* title = wants(value.getDynamicObject());
        if (title != nullptr
            && value.getDynamicObject()->getProperty(jsonKeys::title).toString()
                   != *title) {
            changed = true;
            break;
        }
    }
    if (!changed)
        return false;

    const auto shallowCopy = [](const juce::DynamicObject* source) {
        auto copy = juce::DynamicObject::Ptr(new juce::DynamicObject());
        const auto& properties = source->getProperties();
        for (int index = 0; index < properties.size(); ++index)
            copy->setProperty(properties.getName(index),
                              properties.getValueAt(index));
        return copy;
    };
    juce::Array<juce::var> replacement;
    replacement.ensureStorageAllocated(nodes->size());
    for (const auto& value : *nodes) {
        const auto* node = value.getDynamicObject();
        const auto* title = wants(node);
        if (title == nullptr
            || node->getProperty(jsonKeys::title).toString() == *title) {
            // Untouched nodes are shared, not copied: nothing writes to them.
            replacement.add(value);
            continue;
        }
        auto copy = shallowCopy(node);
        copy->setProperty(jsonKeys::title, *title);
        replacement.add(juce::var(copy.get()));
    }
    auto replaced = shallowCopy(graph);
    replaced->setProperty(jsonKeys::nodes, juce::var(replacement));
    trackNodeGraphStates_[trackIndex] = juce::var(replaced.get());
    return true;
}

void WorkspaceComponent::ensureMidiNodeGraphState(const std::uint64_t trackId)
{
    if (trackId == 0)
        return;
    const auto found = midiNodeGraphStates_.find(trackId);
    if (found == midiNodeGraphStates_.end() || !isNodeGraphState(found->second))
        midiNodeGraphStates_[trackId]
            = stateWithoutSharedModules(factoryMidiNodeGraphState_);
}

void WorkspaceComponent::updateSharedNodeModules(const juce::var& graphState)
{
    mergeSharedModules(sharedNodeModules_, graphState);
}

void WorkspaceComponent::updateSharedModulationModules(
    const juce::var& graphState)
{
    mergeSharedModules(sharedModulationModules_, graphState);
}

void WorkspaceComponent::mergeSharedModules(juce::var& library,
                                            const juce::var& graphState)
{
    const auto* graph = graphState.getDynamicObject();
    if (graph == nullptr)
        return;
    const auto modules = graph->getProperty(jsonKeys::modules);
    const auto* values = modules.getArray();
    const auto* currentValues = library.getArray();
    if (values == nullptr || (values->isEmpty()
                              && currentValues != nullptr
                              && !currentValues->isEmpty()))
        return;

    // References, not copies. Cloning the library and then cloning every class
    // coming in meant deep-copying sixty-eight class definitions on every save,
    // which is every click that changes the selection. A stored graph state is
    // only ever edited at its top level — track titles and I/O nodes — so the
    // class definitions inside one are safe to share.
    juce::Array<juce::var> mergedArray;
    if (currentValues != nullptr)
        mergedArray.addArray(*currentValues);
    auto merged = juce::var(mergedArray);
    auto* mergedValues = merged.getArray();
    if (mergedValues == nullptr)
        return;
    // A factory class serialises to the same object every time, so most
    // merges bring nothing new. The library is replaced only when a body
    // changed or one arrived: kept, it keeps its clone with it.
    auto changed = false;
    for (const auto& incoming : *values) {
        const auto* incomingObject = incoming.getDynamicObject();
        const auto key = incomingObject != nullptr
                             ? incomingObject->getProperty(jsonKeys::key).toString()
                             : juce::String {};
        if (key.isEmpty()
            || static_cast<bool>(incomingObject->getProperty(jsonKeys::inline_)))
            continue;
        const auto existing = std::find_if(
            mergedValues->begin(), mergedValues->end(),
            [&key](const auto& value) {
                const auto* object = value.getDynamicObject();
                return object != nullptr
                       && object->getProperty(jsonKeys::key).toString() == key;
            });
        // The incoming body always wins, factory key or not: a project may
        // carry an edited class under a factory key, and keeping the copy the
        // library already holds would silently discard that edit.
        if (existing != mergedValues->end()) {
            if (existing->getDynamicObject() != incomingObject) {
                *existing = incoming;
                changed = true;
            }
        } else {
            mergedValues->add(incoming);
            changed = true;
        }
    }
    if (!changed)
        return;
    library = std::move(merged);
    // The library used to be written back into every stored graph here: a deep
    // clone of the whole library and of each track's graph, for every audio
    // track, every MIDI track and every bus, on every save. Saving happens on
    // every click that changes the selection, and it cost a full second.
    //
    // None of it was needed. Every reader of a stored state already merges the
    // library as it reads — restoreNodeGraphForTrack, the two project-save
    // paths, and ensureTrackNodeGraphState all call
    // graphStateWithSharedModules — so the copies baked in here were never the
    // ones anybody looked at.
}

juce::var WorkspaceComponent::stateWithoutSharedModules(
    const juce::var& state)
{
    const auto* graph = state.getDynamicObject();
    if (graph == nullptr)
        return state;
    const auto* modules = graph->getProperty(jsonKeys::modules).getArray();
    if (modules == nullptr)
        return state;
    juce::Array<juce::var> kept;
    for (const auto& value : *modules)
        if (const auto* object = value.getDynamicObject();
            object != nullptr
            && static_cast<bool>(object->getProperty(jsonKeys::inline_)))
            kept.add(value);
    if (kept.size() == modules->size())
        return state;
    auto shallow = juce::DynamicObject::Ptr(new juce::DynamicObject());
    const auto& properties = graph->getProperties();
    for (int index = 0; index < properties.size(); ++index)
        shallow->setProperty(properties.getName(index),
                             properties.getValueAt(index));
    shallow->setProperty(jsonKeys::modules, juce::var(kept));
    return juce::var(shallow.get());
}

juce::var WorkspaceComponent::sharedNodeModulesCopy() const
{
    // Deep, at every level: the autosave writer serialises this on its own
    // thread, and the project-graph self test insists no object in it is
    // one the message thread could still edit underneath. The clone is
    // kept while the library is the same object — every change replaces
    // the array, none edits it in place — and shared, read-only, by every
    // document made from it: seven milliseconds on a big project, paid
    // once per change instead of once per autosave.
    const auto* current = static_cast<const void*>(sharedNodeModules_.getArray());
    if (current == nullptr || current != sharedNodeModulesCloneOf_) {
        sharedNodeModulesClone_ = cloneJsonValue(sharedNodeModules_);
        sharedNodeModulesCloneOf_ = current;
    }
    return sharedNodeModulesClone_;
}

juce::var WorkspaceComponent::graphStateWithSharedModules(
    const juce::var& state) const
{
    return graphStateWithSharedModules(state, sharedNodeModulesCopy());
}

juce::var WorkspaceComponent::graphStateWithSharedModules(
    const juce::var& state, const juce::var& libraryCopy) const
{
    return stateWithLibraryCopy(libraryCopy, state, factoryNodeGraphState_);
}

juce::var WorkspaceComponent::sharedModulationModulesCopy() const
{
    const auto* current
        = static_cast<const void*>(sharedModulationModules_.getArray());
    if (current == nullptr || current != sharedModulationModulesCloneOf_) {
        sharedModulationModulesClone_ = cloneJsonValue(sharedModulationModules_);
        sharedModulationModulesCloneOf_ = current;
    }
    return sharedModulationModulesClone_;
}

juce::var WorkspaceComponent::modulationStateWithSharedModules(
    const juce::var& state) const
{
    return modulationStateWithSharedModules(state,
                                            sharedModulationModulesCopy());
}

juce::var WorkspaceComponent::modulationStateWithSharedModules(
    const juce::var& state, const juce::var& libraryCopy) const
{
    return stateWithLibraryCopy(libraryCopy, state,
                                factoryModulationGraphState_);
}

juce::var WorkspaceComponent::stateWithLibraryCopy(
    const juce::var& libraryCopy, const juce::var& state,
    const juce::var& fallback)
{
    auto result = cloneJsonValue(isNodeGraphState(state) ? state : fallback);
    if (auto* graph = result.getDynamicObject()) {
        // The entries come from the caller's copy and are shared with every
        // other graph in the same document. Nothing below edits a module: it
        // replaces an array slot or appends to the array, so the sharing
        // cannot leak one track's override into another's.
        juce::Array<juce::var> values;
        if (const auto* library = libraryCopy.getArray())
            values.addArray(*library);
        auto merged = juce::var(std::move(values));
        auto* mergedValues = merged.getArray();
        if (const auto* localValues = graph->getProperty(jsonKeys::modules).getArray()) {
            for (const auto& local : *localValues) {
                const auto* object = local.getDynamicObject();
                if (object == nullptr
                    || !static_cast<bool>(object->getProperty(jsonKeys::inline_)))
                    continue;
                const auto key = object->getProperty(jsonKeys::key).toString();
                const auto existing = std::find_if(
                    mergedValues->begin(), mergedValues->end(),
                    [&key](const auto& candidate) {
                        const auto* candidateObject = candidate.getDynamicObject();
                        return candidateObject != nullptr
                               && candidateObject->getProperty(jsonKeys::key).toString()
                                      == key;
                    });
                if (existing != mergedValues->end())
                    *existing = cloneJsonValue(local);
                else
                    mergedValues->add(cloneJsonValue(local));
            }
        }
        graph->setProperty(jsonKeys::modules, std::move(merged));
    }
    return result;
}

juce::var WorkspaceComponent::graphStateForEditor(const juce::var& state) const
{
    return stateWithLibrary(sharedNodeModules_, state, factoryNodeGraphState_);
}

juce::var WorkspaceComponent::modulationStateForEditor(
    const juce::var& state) const
{
    return stateWithLibrary(sharedModulationModules_, state,
                            factoryModulationGraphState_);
}

juce::var WorkspaceComponent::stateWithLibrary(const juce::var& library,
                                               const juce::var& state,
                                               const juce::var& fallback)
{
    const auto& source = isNodeGraphState(state) ? state : fallback;
    const auto* graph = source.getDynamicObject();
    if (graph == nullptr)
        return source;
    // The same merge graphStateWithSharedModules performs, without copying
    // anything. That function deep-clones because the states we keep are
    // edited in place afterwards; the editor only ever reads what it restores,
    // so it can be handed references to the very same class definitions.
    //
    // The difference is the whole cost of selecting a track: the library holds
    // thirty-four factory classes, each with its nodes, ports and generated
    // interface, and cloning them twice per click is what made the interface
    // stall for a second.
    auto shallow = juce::DynamicObject::Ptr(new juce::DynamicObject());
    for (const auto& property : graph->getProperties())
        shallow->setProperty(property.name, property.value);
    juce::Array<juce::var> merged;
    if (const auto* shared = library.getArray())
        merged.addArray(*shared);
    if (const auto* localValues = graph->getProperty(jsonKeys::modules).getArray())
        for (const auto& local : *localValues) {
            const auto* object = local.getDynamicObject();
            if (object == nullptr
                || !static_cast<bool>(object->getProperty(jsonKeys::inline_)))
                continue;
            const auto key = object->getProperty(jsonKeys::key).toString();
            const auto existing = std::find_if(
                merged.begin(), merged.end(),
                [&key](const auto& candidate) {
                    const auto* candidateObject = candidate.getDynamicObject();
                    return candidateObject != nullptr
                           && candidateObject->getProperty(jsonKeys::key).toString()
                                  == key;
                });
            if (existing != merged.end())
                *existing = local;
            else
                merged.add(local);
        }
    shallow->setProperty(jsonKeys::modules, juce::var(merged));
    return juce::var(shallow.get());
}

void WorkspaceComponent::setTrackNodeGraphState(const std::size_t trackIndex,
                                                 const juce::var& state)
{
    if (trackIndex >= audio_.trackCount())
        return;
    const auto displayedTrack = selectedTrack_;
    const auto restoreDisplayedTrack = displayedTrack.has_value()
                                       && *displayedTrack < audio_.trackCount()
                                       && *displayedTrack != trackIndex;
    ensureTrackNodeGraphState(trackIndex);
    if (isNodeGraphState(state))
        updateSharedNodeModules(state);
    const auto& effectiveState = isLegacyMixedFactoryAudioGraph(state)
                                     ? factoryNodeGraphState_ : state;
    nodeGraph_.restore(graphStateWithSharedModules(effectiveState));
    // The editor holds a graph in passing that is nobody's selection: the
    // restore of the selection below must not be skipped as already shown.
    setEditorGraphOwner(EditorGraphOwner::none, 0);
    auto canonicalState = nodeGraph_.serialise();
    updateSharedNodeModules(canonicalState);
    canonicalState = stateWithoutSharedModules(canonicalState);
    trackNodeGraphStates_[trackIndex] = canonicalState;
    // The editor already holds the canonical graph it was just asked for;
    // it is restored again only when the I/O titles were rewritten. Every
    // track at every load paid the second restore for nothing.
    if (applyTrackIoToGraphState(trackIndex))
        nodeGraph_.restore(graphStateWithSharedModules(
            trackNodeGraphStates_[trackIndex]));
    const auto graph = nodeGraph_.nativeDspGraph();
    const auto result = audio_.setTrackNativeDspGraph(trackIndex, graph);
    nodeGraph_.setDspCompilationStatus(result, graph.nodes.size());
    if (restoreDisplayedTrack) {
        restoreNodeGraphForTrack(*displayedTrack);
    } else {
        setEditorGraphOwner(EditorGraphOwner::track, trackIndex);
        editorSyncedState_ = trackNodeGraphStates_[trackIndex].getDynamicObject();
    }
}

void WorkspaceComponent::setMidiTrackNodeGraphState(const std::uint64_t trackId,
                                                     const juce::var& state)
{
    if (trackId == 0)
        return;
    const auto previousState = nodeGraph_.serialise();
    const auto keepDisplayed = isMidiNodeGraphContext()
                               && selectedMidiTrackId_ == trackId;
    ensureMidiNodeGraphState(trackId);
    if (isNodeGraphState(state))
        updateSharedNodeModules(state);
    nodeGraph_.restore(graphStateWithSharedModules(
        isNodeGraphState(state) ? state : factoryMidiNodeGraphState_));
    auto canonicalState = nodeGraph_.serialise();
    updateSharedNodeModules(canonicalState);
    // Without the shared library, like a track's: the document merges it
    // back once, and the editor's restore does the same.
    canonicalState = stateWithoutSharedModules(canonicalState);
    midiNodeGraphStates_[trackId] = canonicalState;

    cydaw::engine::PolySynthGraphPlan plan;
    const auto result = nodeGraph_.compilePolySynthGraph(plan);
    if (result.wasOk())
        (void) audio_.setMidiTrackInstrumentGraph(trackId, plan);
    // Whatever the graph places after the instrument. The renderer owns the
    // voice; this is the chain behind it, and until now nothing carried it to
    // the track, so a filter dropped after a synth was silent.
    (void) audio_.setMidiTrackEffectGraph(trackId, nodeGraph_.nativeDspGraph());
    if (keepDisplayed)
        setEditorGraphOwner(EditorGraphOwner::midiTrack, trackId);
    else
        nodeGraph_.restore(graphStateWithSharedModules(previousState));
}

bool WorkspaceComponent::isMidiNodeGraphContext() const noexcept
{
    // The editor holds a MIDI track's instrument in the clip editor and the
    // graph editor, and wherever that track is the selection and no audio
    // track is: the launcher shows its devices and turns them too. A knob
    // turned there used to publish the instrument as the factory graph.
    if (selectedMidiTrackId_ == 0 || selectedTrack_)
        return false;
    return currentEditorMode_ == 2 || currentEditorMode_ == 7
           || (editorGraphOwner_ == EditorGraphOwner::midiTrack
               && editorGraphOwnerId_ == selectedMidiTrackId_);
}

void WorkspaceComponent::ensureVirtualBusNodeGraphState(
    const std::uint64_t busId)
{
    if (busId == 0 || !audio_.virtualBus(busId).has_value())
        return;
    const auto found = virtualBusNodeGraphStates_.find(busId);
    // Stored without the shared library, as a track's state is: a bus
    // state that carried every module class was cloned whole into each
    // autosave document, a millisecond per bus for nothing of its own.
    if (found == virtualBusNodeGraphStates_.end()
        || !isNodeGraphState(found->second))
        virtualBusNodeGraphStates_[busId] = stateWithoutSharedModules(
            graphStateWithSharedModules(factoryVirtualBusNodeGraphState_));
}

void WorkspaceComponent::setVirtualBusNodeGraphState(
    const std::uint64_t busId, const juce::var& state)
{
    if (busId == 0 || !audio_.virtualBus(busId).has_value())
        return;
    const auto previousState = nodeGraph_.serialise();
    const auto keepDisplayed = isVirtualBusNodeGraphContext()
                               && selectedVirtualBusGraphId_ == busId;
    ensureVirtualBusNodeGraphState(busId);
    if (isNodeGraphState(state))
        updateSharedNodeModules(state);
    nodeGraph_.restore(graphStateWithSharedModules(
        isNodeGraphState(state) ? state : factoryVirtualBusNodeGraphState_));
    auto canonicalState = nodeGraph_.serialise();
    updateSharedNodeModules(canonicalState);
    canonicalState = stateWithoutSharedModules(canonicalState);
    virtualBusNodeGraphStates_[busId] = canonicalState;
    const auto graph = nodeGraph_.nativeDspGraph();
    (void) audio_.setVirtualBusGraph(busId, graph);
    if (keepDisplayed)
        setEditorGraphOwner(EditorGraphOwner::virtualBus, busId);
    else
        nodeGraph_.restore(graphStateWithSharedModules(previousState));
}

bool WorkspaceComponent::isVirtualBusNodeGraphContext() const noexcept
{
    // The graph editor and its window, and — for the control port — any
    // view while the editor holds the bus's graph.
    if (selectedVirtualBusGraphId_ == 0)
        return false;
    return currentEditorMode_ == 7 || graphWindow_ != nullptr
           || (editorGraphOwner_ == EditorGraphOwner::virtualBus
               && editorGraphOwnerId_ == selectedVirtualBusGraphId_);
}

void WorkspaceComponent::restoreNodeGraphForVirtualBus(
    const std::uint64_t busId)
{
    const auto bus = audio_.virtualBus(busId);
    if (!bus.has_value())
        return;
    ensureVirtualBusNodeGraphState(busId);
    nodeGraph_.restore(graphStateWithSharedModules(
        virtualBusNodeGraphStates_[busId]));
    setEditorGraphOwner(EditorGraphOwner::virtualBus, busId);
    // Without the shared library, as the save keeps it: a state stored with
    // every class body was recloned whole into each autosave document.
    virtualBusNodeGraphStates_[busId] = stateWithoutSharedModules(
        nodeGraph_.serialise());
    const auto graph = nodeGraph_.nativeDspGraph();
    const auto result = audio_.setVirtualBusGraph(busId, graph);
    nodeGraph_.setDspCompilationStatus(result, graph.nodes.size());
    nodeGraph_.setContextName(bus->name);
    refreshNodeDeviceSurface();
}

void WorkspaceComponent::openVirtualBusGraph(const std::uint64_t busId)
{
    if (!audio_.virtualBus(busId).has_value())
        return;
    setEditorMode(7);
    selectedTrack_.reset();
    selectedMidiTrackId_ = 0;
    selectedVirtualBusGraphId_ = busId;
    restoreNodeGraphForVirtualBus(busId);
    resized();
}

void WorkspaceComponent::setEditorGraphOwner(const EditorGraphOwner owner,
                                             const std::uint64_t id)
{
    editorGraphOwner_ = owner;
    editorGraphOwnerId_ = id;
    editorSyncedState_ = nullptr;
}

bool WorkspaceComponent::editorHoldsStoredGraph(
    const EditorGraphOwner owner, const std::uint64_t id,
    const juce::var& stored) const
{
    return editorGraphOwner_ == owner && editorGraphOwnerId_ == id
           && editorSyncedState_ != nullptr
           && stored.getDynamicObject() == editorSyncedState_;
}

bool WorkspaceComponent::editorShowsSelection() const
{
    if (isVirtualBusNodeGraphContext())
        return editorGraphOwner_ == EditorGraphOwner::virtualBus
               && editorGraphOwnerId_ == selectedVirtualBusGraphId_;
    if (isMidiNodeGraphContext())
        return editorGraphOwner_ == EditorGraphOwner::midiTrack
               && editorGraphOwnerId_ == selectedMidiTrackId_;
    return selectedTrack_.has_value()
           && editorGraphOwner_ == EditorGraphOwner::track
           && editorGraphOwnerId_ == *selectedTrack_;
}

void WorkspaceComponent::saveCurrentNodeGraphState()
{
    // The editor's content goes into the selection's slot only when the
    // editor is showing the selection. On a project load the selection moves
    // first; saving then would copy the previous track's graph — or the
    // factory one — over what the file just restored.
    if (!editorShowsSelection())
        return;
    if (isVirtualBusNodeGraphContext()) {
        ensureVirtualBusNodeGraphState(selectedVirtualBusGraphId_);
        auto state = nodeGraph_.serialise();
        updateSharedNodeModules(state);
        virtualBusNodeGraphStates_[selectedVirtualBusGraphId_]
            = stateWithoutSharedModules(state);
        return;
    }
    if (isMidiNodeGraphContext()) {
        ensureMidiNodeGraphState(selectedMidiTrackId_);
        auto state = nodeGraph_.serialise();
        updateSharedNodeModules(state);
        midiNodeGraphStates_[selectedMidiTrackId_]
            = stateWithoutSharedModules(state);
        editorSyncedState_
            = midiNodeGraphStates_[selectedMidiTrackId_].getDynamicObject();
        return;
    }
    if (!selectedTrack_ || *selectedTrack_ >= audio_.trackCount())
        return;
    auto mark = juce::Time::getHighResolutionTicks();
    const auto since = [&mark] {
        const auto now = juce::Time::getHighResolutionTicks();
        const auto ms = juce::Time::highResolutionTicksToSeconds(now - mark)
                        * 1000.0;
        mark = now;
        return juce::String(ms, 2);
    };
    ensureTrackNodeGraphState(*selectedTrack_);
    const auto ensured = since();
    auto state = nodeGraph_.serialise();
    const auto serialised = since();
    updateSharedNodeModules(state);
    const auto merged = since();
    // Kept without the class bodies. Both readers put the shared library back
    // and ignore whatever classes a stored state carries unless they are
    // marked inline, so carrying them is thirty-two class bodies per track
    // that only ever cost the autosave a deep copy.
    trackNodeGraphStates_[*selectedTrack_] = stateWithoutSharedModules(state);
    editorSyncedState_ = trackNodeGraphStates_[*selectedTrack_].getDynamicObject();
    saveGraphStateProfile_ = "ensure=" + ensured + " serialise=" + serialised
                             + " merge=" + merged + " strip=" + since();
}

void WorkspaceComponent::restoreNodeGraphForMidiTrack(const std::uint64_t trackId)
{
    if (trackId == 0)
        return;
    // The editor already holds this instrument, and nothing has been stored
    // over it since: it is the graph's truth while it does. Restoring it
    // again was what every entry into the clip editor cost.
    if (const auto stored = midiNodeGraphStates_.find(trackId);
        stored != midiNodeGraphStates_.end()
        && editorHoldsStoredGraph(EditorGraphOwner::midiTrack, trackId,
                                  stored->second))
        return;
    selectedVirtualBusGraphId_ = 0;
    auto profileMark = juce::Time::getHighResolutionTicks();
    const auto profileSince = [&profileMark] {
        const auto now = juce::Time::getHighResolutionTicks();
        const auto ms = juce::Time::highResolutionTicksToSeconds(
                            now - profileMark) * 1000.0;
        profileMark = now;
        return juce::String(ms, 2);
    };
    ensureMidiNodeGraphState(trackId);
    nodeGraph_.restore(graphStateForEditor(midiNodeGraphStates_[trackId]));
    setEditorGraphOwner(EditorGraphOwner::midiTrack, trackId);
    const auto profileRestore = profileSince();
    midiNodeGraphStates_[trackId] = stateWithoutSharedModules(
        nodeGraph_.serialise());
    editorSyncedState_ = midiNodeGraphStates_[trackId].getDynamicObject();
    const auto profileStore = profileSince();
    cydaw::engine::PolySynthGraphPlan plan;
    auto result = nodeGraph_.compilePolySynthGraph(plan);
    if (result.wasOk())
        result = audio_.setMidiTrackInstrumentGraph(trackId, plan);
    nodeGraph_.setDspCompilationStatus(result, plan.graphNodeCount);
    const auto profileCompile = profileSince();
    const auto tracks = audio_.midiTracks();
    const auto found = std::find_if(tracks.begin(), tracks.end(),
                                    [trackId](const auto& track) {
                                        return track.id == trackId;
                                    });
    nodeGraph_.setContextName(found != tracks.end() ? found->name
                                                    : tr("midi.instrument"));
    refreshNodeDeviceSurface();
    noteUiHitchEvent("restore-midi[restore=" + profileRestore + " store="
                     + profileStore + " compile=" + profileCompile
                     + " deviceSurface=" + profileSince() + "]");
}

void WorkspaceComponent::restoreNodeGraphForTrack(const std::size_t trackIndex)
{
    if (trackIndex >= audio_.trackCount())
        return;
    // The editor already holds this track's graph, and nothing has been
    // stored over it since: it is the graph's truth while it does, the
    // audio runs what it publishes and the device surface shows it.
    // Restoring it again — the parse, the compile, the rebuilt surface —
    // was what every change of view cost on a large patch.
    if (trackIndex < trackNodeGraphStates_.size()
        && editorHoldsStoredGraph(EditorGraphOwner::track, trackIndex,
                                  trackNodeGraphStates_[trackIndex]))
        return;
    auto profileMark = juce::Time::getHighResolutionTicks();
    const auto profileSince = [&profileMark] {
        const auto now = juce::Time::getHighResolutionTicks();
        const auto ms = juce::Time::highResolutionTicksToSeconds(
                            now - profileMark) * 1000.0;
        profileMark = now;
        return juce::String(ms, 2);
    };
    selectedVirtualBusGraphId_ = 0;
    ensureTrackNodeGraphState(trackIndex);
    const auto profileEnsure = profileSince();
    const auto editorState = graphStateForEditor(
        trackNodeGraphStates_[trackIndex]);
    const auto profileMerge = profileSince();
    nodeGraph_.restore(editorState);
    setEditorGraphOwner(EditorGraphOwner::track, trackIndex);
    editorSyncedState_ = trackNodeGraphStates_[trackIndex].getDynamicObject();
    // The editor was asked for the graph it had just been given, and the answer
    // was stored back over the state it came from. The round trip canonicalises
    // nothing the next save will not, and it cost a serialisation and a deep
    // clone on every selection.
    const auto profileRestore = profileSince();
    const auto graph = nodeGraph_.nativeDspGraph();
    const auto profileNative = profileSince();
    const auto result = audio_.setTrackNativeDspGraph(trackIndex, graph);
    const auto profileApply = profileSince();
    nodeGraph_.setDspCompilationStatus(result, graph.nodes.size());
    nodeGraph_.setContextName(audio_.trackInfo(trackIndex).file.getFileNameWithoutExtension());
    const auto profileStatus = profileSince();
    refreshNodeDeviceSurface();
    restoreTrackProfile_ = "ensure=" + profileEnsure + " merge=" + profileMerge
                           + " restore=" + profileRestore
                           + " native=" + profileNative
                           + " apply=" + profileApply
                           + " status=" + profileStatus
                           + " deviceSurface=" + profileSince();
    noteUiHitchEvent("restore-track[" + restoreTrackProfile_ + "]");
}

void WorkspaceComponent::scheduleModulationTargetsRefresh()
{
    if (modulationTargetsRefreshPending_)
        return;
    modulationTargetsRefreshPending_ = true;
    juce::Component::SafePointer<WorkspaceComponent> safeThis(this);
    juce::MessageManager::callAsync([safeThis] {
        if (safeThis == nullptr)
            return;
        safeThis->modulationTargetsRefreshPending_ = false;
        safeThis->refreshModulationTargets();
    });
}

void WorkspaceComponent::scheduleNodeDeviceSurfaceRefresh()
{
    if (nodeDeviceSurfaceRefreshPending_)
        return;
    nodeDeviceSurfaceRefreshPending_ = true;
    juce::Component::SafePointer<WorkspaceComponent> safeThis(this);
    juce::MessageManager::callAsync([safeThis] {
        if (safeThis == nullptr)
            return;
        safeThis->nodeDeviceSurfaceRefreshPending_ = false;
        safeThis->refreshNodeDeviceSurface();
    });
}

void WorkspaceComponent::refreshNodeDeviceSurface()
{
    deviceChain_.setNodeDeviceInterface(nodeGraph_.graphInterfaceState());
    auto channels = 2;
    if (isVirtualBusNodeGraphContext()) {
        if (const auto bus = audio_.virtualBus(selectedVirtualBusGraphId_))
            channels = juce::jlimit(1, 2, bus->channels);
    } else if (!isMidiNodeGraphContext() && selectedTrack_
        && *selectedTrack_ < trackPresentations_.size())
        channels = juce::jlimit(
            1, 16,
            trackPresentations_[*selectedTrack_].audioChannels);
    const auto inputTitle = isMidiNodeGraphContext()
                                ? tr("graph.inputMidi")
                                : channels == 1
                                      ? tr("graph.inputMono")
                                      : channels == 2
                                            ? tr("graph.inputStereo")
                                            : tr("graph.inputChannels", "count",
                                                 juce::String(channels));
    const auto outputTitle = channels == 1
                                 ? tr("graph.outputMono")
                                 : channels == 2
                                       ? tr("graph.outputStereo")
                                       : tr("graph.outputChannels", "count",
                                            juce::String(channels));
    deviceChain_.setGraphLayout(inputTitle, outputTitle,
                                nodeGraph_.containsNodeType("drive"),
                                nodeGraph_.containsNodeType("lowpass"));
}

void WorkspaceComponent::refreshDevicePluginChain()
{
    if (!selectedTrack_ || *selectedTrack_ >= audio_.trackCount()) {
        selectedPluginIndex_ = 0;
        deviceChain_.setPluginChain({}, 0);
        return;
    }

    // Five times a second: the plugin list, not the whole track with its
    // clips, events, automation and takes copied along.
    const auto hosted = audio_.trackPlugins(*selectedTrack_);
    std::vector<DeviceChainComponent::PluginDisplay> plugins;
    plugins.reserve(hosted.size());
    for (const auto& plugin : hosted)
        plugins.push_back({ plugin.name, plugin.running, plugin.missedBlocks,
                            plugin.latencySamples });
    if (plugins.empty())
        selectedPluginIndex_ = 0;
    else
        selectedPluginIndex_ = std::min(selectedPluginIndex_, plugins.size() - 1);
    deviceChain_.setPluginChain(std::move(plugins), selectedPluginIndex_);
}

void WorkspaceComponent::publishNativeDspGraph()
{
    if (uiHitchProbe_ != nullptr)
        noteUiHitchEvent("publish[" + nodeGraph_.layoutChangeOrigins()
                                          .getLastCharacters(60) + "]");
    if (isVirtualBusNodeGraphContext()) {
        saveCurrentNodeGraphState();
        const auto graph = nodeGraph_.nativeDspGraph();
        auto result = audio_.updateVirtualBusGraphParameters(
            selectedVirtualBusGraphId_, graph);
        if (result.failed())
            result = audio_.setVirtualBusGraph(
                selectedVirtualBusGraphId_, graph);
        nodeGraph_.setDspCompilationStatus(result, graph.nodes.size());
        return;
    }
    if (isMidiNodeGraphContext()) {
        saveCurrentNodeGraphState();
        cydaw::engine::PolySynthGraphPlan plan;
        auto result = nodeGraph_.compilePolySynthGraph(plan);
        if (result.wasOk())
            result = audio_.setMidiTrackInstrumentGraph(selectedMidiTrackId_, plan);
        // The chain behind the instrument, recompiled: this is the structural
        // path, so its shape may have changed.
        (void) audio_.setMidiTrackEffectGraph(selectedMidiTrackId_,
                                              nodeGraph_.nativeDspGraph());
        nodeGraph_.setDspCompilationStatus(result, plan.graphNodeCount);
        return;
    }
    const auto graph = nodeGraph_.nativeDspGraph();
    juce::Result result = juce::Result::ok();
    if (selectedTrack_ && *selectedTrack_ < audio_.trackCount()) {
        saveCurrentNodeGraphState();
        result = audio_.setTrackNativeDspGraph(*selectedTrack_, graph);
    } else {
        factoryNodeGraphState_ = nodeGraph_.serialise();
        updateSharedNodeModules(factoryNodeGraphState_);
        factoryNodeGraphState_ = graphStateWithSharedModules(factoryNodeGraphState_);
        result = audio_.setNativeDspGraph(graph);
    }
    nodeGraph_.setDspCompilationStatus(result, graph.nodes.size());
}

// What a knob on a card runs, thirty times a second while it is dragged.
// Named so the bench can time it as the mouse would.
void WorkspaceComponent::onGraphParametersChanged()
{
    auto mark = juce::Time::getHighResolutionTicks();
    const auto since = [&mark] {
        const auto now = juce::Time::getHighResolutionTicks();
        const auto ms = juce::Time::highResolutionTicksToSeconds(now - mark)
                        * 1000.0;
        mark = now;
        return juce::String(ms, 2);
    };
    publishNativeDspParameters();
    const auto publish = since();
    scheduleNodeDeviceSurfaceRefresh();
    const auto surface = since();
    // The modulation panel rebuilds its whole target list, combo box and
    // all, and nothing on the audio path reads it: base values reach the
    // engine through the graph, not through this list. Once per turn of the
    // message loop is what a drag needs, not once per notch.
    scheduleModulationTargetsRefresh();
    const auto modulation = since();
    markProjectDirty();
    graphNotchProfile_ = "publish=" + publish + "[" + publishProfile_ + "]"
                         + " surface=" + surface + " modulation=" + modulation
                         + " dirty=" + since();
}

void WorkspaceComponent::publishNativeDspParameters()
{
    if (isVirtualBusNodeGraphContext()) {
        saveCurrentNodeGraphState();
        const auto graph = nodeGraph_.nativeDspGraph();
        const auto result = audio_.setVirtualBusGraph(
            selectedVirtualBusGraphId_, graph);
        nodeGraph_.setDspCompilationStatus(result, graph.nodes.size());
        return;
    }
    if (isMidiNodeGraphContext()) {
        auto midiMark = juce::Time::getHighResolutionTicks();
        const auto midiSince = [&midiMark] {
            const auto now = juce::Time::getHighResolutionTicks();
            const auto ms = juce::Time::highResolutionTicksToSeconds(
                                now - midiMark) * 1000.0;
            midiMark = now;
            return juce::String(ms, 2);
        };
        saveCurrentNodeGraphState();
        const auto saved = midiSince();
        cydaw::engine::PolySynthGraphPlan plan;
        auto result = nodeGraph_.compilePolySynthGraph(plan);
        const auto compiled = midiSince();
        if (result.wasOk())
            result = audio_.setMidiTrackInstrumentGraph(selectedMidiTrackId_, plan);
        const auto instrument = midiSince();
        // The chain behind the instrument has knobs too. Until now a notch on
        // one of them reached the voice plan and not the chain, so an EQ band
        // placed after a synth could be turned without being heard.
        const auto chain = nodeGraph_.nativeDspGraph();
        const auto exported = midiSince();
        if (audio_.updateMidiTrackEffectParameters(selectedMidiTrackId_, chain)
                .failed())
            (void) audio_.setMidiTrackEffectGraph(selectedMidiTrackId_, chain);
        nodeGraph_.setDspCompilationStatus(result, plan.graphNodeCount);
        publishProfile_ = "midi save=" + saved + "{" + saveGraphStateProfile_
                          + "} poly=" + compiled + "{"
                          + nodeGraph_.polyCompileProfile() + "}"
                          + " instrument=" + instrument
                          + " export=" + exported + " chain=" + midiSince();
        return;
    }
    auto mark = juce::Time::getHighResolutionTicks();
    const auto since = [&mark] {
        const auto now = juce::Time::getHighResolutionTicks();
        const auto ms = juce::Time::highResolutionTicksToSeconds(now - mark)
                        * 1000.0;
        mark = now;
        return juce::String(ms, 2);
    };
    const auto graph = nodeGraph_.nativeDspGraph();
    const auto exported = since();
    juce::Result result = juce::Result::ok();
    if (selectedTrack_ && *selectedTrack_ < audio_.trackCount()) {
        saveCurrentNodeGraphState();
        const auto saved = since();
        result = audio_.updateTrackNativeDspParameters(*selectedTrack_, graph);
        if (result.failed())
            result = audio_.setTrackNativeDspGraph(*selectedTrack_, graph);
        publishProfile_ = "export=" + exported + " save=" + saved + "{"
                          + saveGraphStateProfile_ + "}" + " engine=" + since()
                          + (result.wasOk() ? "" : " recompiled");
    } else {
        factoryNodeGraphState_ = nodeGraph_.serialise();
        updateSharedNodeModules(factoryNodeGraphState_);
        factoryNodeGraphState_ = graphStateWithSharedModules(factoryNodeGraphState_);
        result = audio_.setNativeDspGraph(graph);
    }
    nodeGraph_.setDspCompilationStatus(result, graph.nodes.size());
}

void WorkspaceComponent::applyMixerSettings()
{
    if (!selectedTrack_ && selectedMidiTrackId_ != 0) {
        const auto gainDb = static_cast<float>(gainSlider_.getValue());
        const auto pan = static_cast<float>(panSlider_.getValue());
        audio_.setMidiTrackMixer(selectedMidiTrackId_, gainDb, pan,
                                 muteButton_.getToggleState(),
                                 soloButton_.getToggleState());
        deviceChain_.setParameters(0.0f, 20'000.0f, gainDb, pan, true);
        refreshMixer();
        if (clipLauncher_.isVisible())
            refreshClipLauncher();
        return;
    }
    if (!selectedTrack_ || *selectedTrack_ >= audio_.trackCount())
        return;

    audio_.setTrackMixer(*selectedTrack_,
                         static_cast<float>(gainSlider_.getValue()),
                         static_cast<float>(panSlider_.getValue()),
                         muteButton_.getToggleState());
    const auto info = audio_.trackInfo(*selectedTrack_);
    deviceChain_.setParameters(info.driveDb, info.filterCutoffHz, info.gainDb, info.pan, true);
    trackLanes_[*selectedTrack_]->setMixerState(muteButton_.getToggleState(),
                                                soloButton_.getToggleState(),
                                                armButton_.getToggleState());
    refreshMixer();
}

void WorkspaceComponent::applyClipSettings(const bool snapStart)
{
    if (!selectedTrack_ || *selectedTrack_ >= audio_.trackCount())
        return;

    if (inspectorSelection_ == InspectorSelection::audioEvent) {
        const auto event = selectedAudioEvent();
        if (!event.has_value())
            return;
        const auto sourceLength = std::max(
            event->sourceLengthSeconds,
            event->sourceOffsetSeconds + event->sourceDurationSeconds);
        const auto sourceOffset = juce::jlimit(
            0.0, std::max(0.0, sourceLength - 0.001),
            sourceOffsetSlider_.getValue());
        const auto sourceDuration = juce::jlimit(
            0.001, std::max(0.001, sourceLength - sourceOffset),
            clipDurationSlider_.getValue());
        auto localStart = clipStartSlider_.getValue();
        auto timelineDuration = clipStretchSlider_.getValue();
        if (snapStart) {
            if (currentEditorMode_ == 6
                && clipTimelineEditor_.snappingEnabled()) {
                localStart = clipTimelineEditor_.snapTime(localStart);
            } else if (arrangementSnappingEnabled()) {
                localStart = snapArrangementTime(localStart);
            }
        }
        clipStartSlider_.setValue(localStart, juce::dontSendNotification);
        sourceOffsetSlider_.setValue(sourceOffset,
                                     juce::dontSendNotification);
        clipDurationSlider_.setValue(sourceDuration,
                                     juce::dontSendNotification);
        clipStretchSlider_.setValue(timelineDuration,
                                    juce::dontSendNotification);
        const auto fadeIn = juce::jlimit(
            0.0, timelineDuration, sampleFadeInSlider_.getValue());
        const auto fadeOut = juce::jlimit(
            0.0, timelineDuration, sampleFadeOutSlider_.getValue());
        sampleFadeInSlider_.setValue(fadeIn, juce::dontSendNotification);
        sampleFadeOutSlider_.setValue(fadeOut, juce::dontSendNotification);
        auto changed = false;
        if (launcherEditorContext_) {
            const auto context = *launcherEditorContext_;
            changed = audio_.updateLauncherClipEvent(
                context.trackIndex, context.sceneIndex, event->id,
                localStart, sourceOffset, timelineDuration,
                sourceDuration, fadeIn, fadeOut);
        } else {
            changed = audio_.updateAudioClipEvent(
                *selectedTrack_, selectedAudioClipId_, event->id,
                localStart, sourceOffset, timelineDuration,
                sourceDuration, fadeIn, fadeOut);
        }
        if (!changed)
            return;
        refreshActiveClipEditorPreview();
        refreshTimelineRanges();
        refreshClipTimelineEditor();
        refreshBottomAutomationEditor();
        refreshSelectedTrack();
        updateTrackDescription();
        markProjectDirty();
        return;
    }

    const auto clip = audio_.audioClip(*selectedTrack_, selectedAudioClipId_);
    if (!clip.has_value())
        return;
    const auto sourceOffset = juce::jlimit(0.0,
                                           std::max(0.0, clip->sourceLengthSeconds - 0.001),
                                           sourceOffsetSlider_.getValue());
    const auto maxDuration = std::max(0.001, clip->sourceLengthSeconds - sourceOffset);
    clipDurationSlider_.setRange(0.001, maxDuration, 0.001);
    auto requestedDuration = clipDurationSlider_.getValue();
    if (snapStart && arrangementSnappingEnabled())
        requestedDuration = std::max(
            0.001, snapArrangementTime(requestedDuration));
    const auto duration = juce::jlimit(0.001, maxDuration,
                                       requestedDuration);
    if (std::abs(duration - clipDurationSlider_.getValue()) > 0.0001)
        clipDurationSlider_.setValue(duration, juce::dontSendNotification);

    const auto start = snapStart
                           ? snapArrangementTime(
                                 clipStartSlider_.getValue())
                           : clipStartSlider_.getValue();
    if (std::abs(start - clipStartSlider_.getValue()) > 0.0001)
        clipStartSlider_.setValue(start, juce::dontSendNotification);
    (void) audio_.updateAudioClip(*selectedTrack_, clip->id,
                                  start, sourceOffset,
                                  duration);
    refreshActiveClipEditorPreview();
    refreshTimelineRanges();
    refreshAutomationEditor();
    refreshAudioEditor();
    updateTrackDescription();
}

void WorkspaceComponent::configureLayout()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "CyDAW";
    options.filenameSuffix = ".layout.settings";
    options.folderName = "CyDAW";
    options.osxLibrarySubFolder = "Application Support";
    layoutProperties_.setStorageParameters(options);
    auto* settings = layoutProperties_.getUserSettings();

    browserLayoutWidth_ = juce::jlimit(
        220, 520, settings->getIntValue("browserWidth", 280));
    inspectorLayoutWidth_ = juce::jlimit(
        210, 420, settings->getIntValue("inspectorWidth", 230));
    bottomLayoutHeight_ = juce::jlimit(
        180, 460, settings->getIntValue("nodeHeight", 230));
    browserPanelVisible_ = settings->getBoolValue("browserVisible", true);
    inspectorPanelVisible_ = settings->getBoolValue("inspectorVisible", true);
    bottomPanelVisible_ = settings->getBoolValue("bottomVisible", true);
    contextAutomationVisible_ = settings->getBoolValue(
        "contextAutomationVisible", true);
    contextAutomationHeight_ = juce::jlimit(
        90, 420, settings->getIntValue("contextAutomationHeight", 150));
    bottomPanelMode_ = juce::jlimit(
        1, 4, settings->getIntValue("bottomPanelMode", 1));
    updateLayoutConstraints();
    loadNamedLayouts();

    const auto restoreGraph = settings->getBoolValue("graphFloating", false);
    const auto restoreInspector = inspectorPanelVisible_
                                  && settings->getBoolValue("inspectorFloating", false);
    const auto restoreBrowser = browserPanelVisible_
                                && settings->getBoolValue("browserFloating", false);
    const auto restoreEditor = settings->getBoolValue("editorFloating", false);
    const auto restoreBottom = bottomPanelVisible_
                               && settings->getBoolValue("bottomFloating", false);
    if (restoreGraph || restoreInspector || restoreBrowser || restoreEditor
        || restoreBottom) {
        juce::Component::SafePointer<WorkspaceComponent> safeThis(this);
        juce::Timer::callAfterDelay(250, [safeThis, restoreGraph, restoreInspector,
                                          restoreBrowser, restoreEditor, restoreBottom] {
            if (safeThis == nullptr)
                return;
            safeThis->setPanelFloating(DockPanel::inspector, restoreInspector);
            safeThis->setPanelFloating(DockPanel::browser, restoreBrowser);
            safeThis->setPanelFloating(DockPanel::editor, restoreEditor);
            safeThis->setPanelFloating(DockPanel::bottom, restoreBottom);
            if (restoreGraph)
                safeThis->toggleGraphWindow();
        });
    }
}

void WorkspaceComponent::updateLayoutConstraints()
{
    const auto inspectorDocked = inspectorPanelVisible_
                                 && !isPanelFloating(DockPanel::inspector);
    const auto browserDocked = browserPanelVisible_
                               && !isPanelFloating(DockPanel::browser);
    const auto editorDocked = !isPanelFloating(DockPanel::editor);
    const auto bottomDocked = bottomPanelVisible_
                              && !isPanelFloating(DockPanel::bottom);
    const auto topDocked = inspectorDocked || browserDocked || editorDocked;

    inspectorDockSlot_.setVisible(inspectorDocked);
    inspectorPanelContainer_.setVisible(inspectorPanelVisible_);
    inspectorHideButton_.setVisible(inspectorDocked);
    inspectorShowButton_.setVisible(!inspectorPanelVisible_);
    inspectorResizer_.setVisible(inspectorDocked);
    browserDockSlot_.setVisible(browserDocked);
    browser_.setVisible(browserPanelVisible_);
    browserHideButton_.setVisible(browserDocked);
    browserShowButton_.setVisible(!browserPanelVisible_);
    browserResizer_.setVisible(browserDocked);
    editorDockSlot_.setVisible(editorDocked);
    editorArea_.setVisible(true);
    bottomDockSlot_.setVisible(bottomDocked);
    nodeAreaContainer_.setVisible(bottomPanelVisible_);
    bottomHideButton_.setVisible(bottomDocked);
    topWorkspaceArea_.setVisible(topDocked);
    bottomShowButton_.setVisible(!bottomPanelVisible_ && topDocked);
    nodeResizer_.setVisible(topDocked && bottomDocked);

    horizontalLayout_.setItemLayout(
        0,
        inspectorDocked ? 210.0 : 30.0,
        inspectorDocked ? 420.0 : 30.0,
        inspectorDocked ? inspectorLayoutWidth_ : 30.0);
    horizontalLayout_.setItemLayout(
        1,
        inspectorDocked ? 6.0 : 0.0,
        inspectorDocked ? 6.0 : 0.0,
        inspectorDocked ? 6.0 : 0.0);
    horizontalLayout_.setItemLayout(2, -0.3, -1.0, -0.7);
    horizontalLayout_.setItemLayout(
        3,
        browserDocked ? 6.0 : 0.0,
        browserDocked ? 6.0 : 0.0,
        browserDocked ? 6.0 : 0.0);
    horizontalLayout_.setItemLayout(
        4,
        browserDocked ? 220.0 : 30.0,
        browserDocked ? 520.0 : 30.0,
        browserDocked ? browserLayoutWidth_ : 30.0);
    verticalLayout_.setItemLayout(
        0,
        topDocked ? -0.3 : 0.0,
        topDocked ? -1.0 : 0.0,
        topDocked ? (bottomDocked ? -0.75 : -1.0) : 0.0);
    verticalLayout_.setItemLayout(
        1,
        topDocked && bottomDocked ? 6.0 : 0.0,
        topDocked && bottomDocked ? 6.0 : 0.0,
        topDocked && bottomDocked ? 6.0 : 0.0);
    if (bottomDocked && topDocked)
        verticalLayout_.setItemLayout(2, 180.0, 460.0, bottomLayoutHeight_);
    else if (bottomDocked)
        verticalLayout_.setItemLayout(2, -0.3, -1.0, -1.0);
    else if (topDocked)
        verticalLayout_.setItemLayout(2, 26.0, 26.0, 26.0);
    else
        verticalLayout_.setItemLayout(2, 0.0, 0.0, 0.0);
    resized();
}

void WorkspaceComponent::saveLayoutPreferences()
{
    auto* settings = layoutProperties_.getUserSettings();
    if (inspectorPanelVisible_ && !isPanelFloating(DockPanel::inspector))
        inspectorLayoutWidth_ = juce::jlimit(
            210, 420, horizontalLayout_.getItemCurrentAbsoluteSize(0));
    if (browserPanelVisible_ && !isPanelFloating(DockPanel::browser))
        browserLayoutWidth_ = juce::jlimit(
            220, 520, horizontalLayout_.getItemCurrentAbsoluteSize(4));
    if (bottomPanelVisible_ && !isPanelFloating(DockPanel::bottom)
        && !isPanelFloating(DockPanel::editor))
        bottomLayoutHeight_ = juce::jlimit(
            180, 460, verticalLayout_.getItemCurrentAbsoluteSize(2));
    settings->setValue("inspectorWidth", inspectorLayoutWidth_);
    settings->setValue("browserWidth", browserLayoutWidth_);
    settings->setValue("nodeHeight", bottomLayoutHeight_);
    settings->setValue("inspectorVisible", inspectorPanelVisible_);
    settings->setValue("browserVisible", browserPanelVisible_);
    settings->setValue("bottomVisible", bottomPanelVisible_);
    settings->setValue("contextAutomationVisible",
                       contextAutomationVisible_);
    settings->setValue("contextAutomationHeight",
                       contextAutomationHeight_);
    settings->setValue("bottomPanelMode", bottomPanelMode_);
    settings->setValue("graphFloating", graphWindow_ != nullptr);
    settings->setValue("inspectorFloating", isPanelFloating(DockPanel::inspector));
    settings->setValue("browserFloating", isPanelFloating(DockPanel::browser));
    settings->setValue("editorFloating", isPanelFloating(DockPanel::editor));
    settings->setValue("bottomFloating", isPanelFloating(DockPanel::bottom));
    const auto saveWindowBounds = [settings](const juce::String& key,
                                             const FloatingPanelWindow* window) {
        if (window == nullptr)
            return;
        const auto bounds = window->getBounds();
        settings->setValue(key + "X", bounds.getX());
        settings->setValue(key + "Y", bounds.getY());
        settings->setValue(key + "Width", bounds.getWidth());
        settings->setValue(key + "Height", bounds.getHeight());
    };
    saveWindowBounds("inspectorWindow", inspectorWindow_.get());
    saveWindowBounds("browserWindow", browserWindow_.get());
    saveWindowBounds("editorWindow", editorWindow_.get());
    saveWindowBounds("bottomWindow", bottomWindow_.get());
    if (graphWindow_ != nullptr) {
        const auto bounds = graphWindow_->getBounds();
        settings->setValue("graphX", bounds.getX());
        settings->setValue("graphY", bounds.getY());
        settings->setValue("graphWidth", bounds.getWidth());
        settings->setValue("graphHeight", bounds.getHeight());
    }
    layoutProperties_.saveIfNeeded();
}

WorkspaceComponent::LayoutState WorkspaceComponent::captureLayoutState(
    juce::String name)
{
    LayoutState state;
    state.name = std::move(name);
    state.inspectorWidth = inspectorPanelVisible_
                                   && !isPanelFloating(DockPanel::inspector)
                               ? juce::jlimit(210, 420,
                                               horizontalLayout_.getItemCurrentAbsoluteSize(0))
                               : inspectorLayoutWidth_;
    state.browserWidth = browserPanelVisible_
                             && !isPanelFloating(DockPanel::browser)
                             ? juce::jlimit(220, 520,
                                             horizontalLayout_.getItemCurrentAbsoluteSize(4))
                             : browserLayoutWidth_;
    state.bottomHeight = bottomPanelVisible_
                             && !isPanelFloating(DockPanel::bottom)
                             && !isPanelFloating(DockPanel::editor)
                              ? juce::jlimit(180, 460,
                                             verticalLayout_.getItemCurrentAbsoluteSize(2))
                              : bottomLayoutHeight_;
    state.editorMode = juce::jlimit(1, 8, currentEditorMode_);
    state.bottomMode = bottomPanelMode_;
    state.inspectorVisible = inspectorPanelVisible_;
    state.browserVisible = browserPanelVisible_;
    state.bottomVisible = bottomPanelVisible_;
    state.graphFloating = graphWindow_ != nullptr;
    state.inspectorFloating = isPanelFloating(DockPanel::inspector);
    state.browserFloating = isPanelFloating(DockPanel::browser);
    state.editorFloating = isPanelFloating(DockPanel::editor);
    state.bottomFloating = isPanelFloating(DockPanel::bottom);
    const auto* settings = layoutProperties_.getUserSettings();
    const auto storedBounds = [settings](const juce::String& key,
                                         const juce::Rectangle<int> fallback) {
        const auto width = settings->getIntValue(key + "Width", fallback.getWidth());
        const auto height = settings->getIntValue(key + "Height", fallback.getHeight());
        return juce::Rectangle<int> {
            settings->getIntValue(key + "X", fallback.getX()),
            settings->getIntValue(key + "Y", fallback.getY()),
            width, height
        };
    };
    state.inspectorWindowBounds = inspectorWindow_ != nullptr
                                      ? inspectorWindow_->getBounds()
                                      : storedBounds("inspectorWindow",
                                                     state.inspectorWindowBounds);
    state.browserWindowBounds = browserWindow_ != nullptr
                                    ? browserWindow_->getBounds()
                                    : storedBounds("browserWindow",
                                                   state.browserWindowBounds);
    state.editorWindowBounds = editorWindow_ != nullptr
                                   ? editorWindow_->getBounds()
                                   : storedBounds("editorWindow",
                                                  state.editorWindowBounds);
    state.bottomWindowBounds = bottomWindow_ != nullptr
                                   ? bottomWindow_->getBounds()
                                   : storedBounds("bottomWindow",
                                                  state.bottomWindowBounds);
    state.graphWindowBounds = graphWindow_ != nullptr
                                  ? graphWindow_->getBounds()
                                  : storedBounds("graph", state.graphWindowBounds);
    return state;
}

WorkspaceComponent::LayoutState WorkspaceComponent::factoryLayout(
    const int presetId) const
{
    LayoutState state;
    switch (presetId) {
        case 2:
            state.name = tr("layout.editing");
            state.inspectorWidth = 290;
            state.browserWidth = 280;
            state.bottomHeight = 250;
            state.editorMode = 5;
            state.browserVisible = false;
            break;
        case 3:
            state.name = tr("layout.modular");
            state.inspectorWidth = 230;
            state.browserWidth = 300;
            state.bottomHeight = 280;
            state.editorMode = 7;
            state.bottomMode = 1;
            state.inspectorVisible = false;
            break;
        case 4:
            state.name = tr("layout.focus");
            state.editorMode = 1;
            state.inspectorVisible = false;
            state.browserVisible = false;
            state.bottomVisible = false;
            break;
        default:
            state.name = tr("layout.production");
            break;
    }
    return state;
}

void WorkspaceComponent::applyLayoutState(const LayoutState& state,
                                          const bool recordHistory)
{
    if (recordHistory) {
        layoutUndoStack_.push_back(captureLayoutState());
        if (layoutUndoStack_.size() > 32)
            layoutUndoStack_.erase(layoutUndoStack_.begin());
        layoutRedoStack_.clear();
    }
    inspectorLayoutWidth_ = juce::jlimit(210, 420, state.inspectorWidth);
    browserLayoutWidth_ = juce::jlimit(220, 520, state.browserWidth);
    bottomLayoutHeight_ = juce::jlimit(180, 460, state.bottomHeight);
    inspectorPanelVisible_ = state.inspectorVisible;
    browserPanelVisible_ = state.browserVisible;
    bottomPanelVisible_ = state.bottomVisible;
    bottomPanelMode_ = state.editorMode == 7
                           ? 1
                           : juce::jlimit(1, 4, state.bottomMode);
    auto* settings = layoutProperties_.getUserSettings();
    const auto storeBounds = [settings](const juce::String& key,
                                        const juce::Rectangle<int> bounds) {
        settings->setValue(key + "X", bounds.getX());
        settings->setValue(key + "Y", bounds.getY());
        settings->setValue(key + "Width", bounds.getWidth());
        settings->setValue(key + "Height", bounds.getHeight());
    };
    storeBounds("inspectorWindow", state.inspectorWindowBounds);
    storeBounds("browserWindow", state.browserWindowBounds);
    storeBounds("editorWindow", state.editorWindowBounds);
    storeBounds("bottomWindow", state.bottomWindowBounds);
    storeBounds("graph", state.graphWindowBounds);
    setPanelFloating(DockPanel::inspector,
                     inspectorPanelVisible_ && state.inspectorFloating);
    setPanelFloating(DockPanel::browser,
                     browserPanelVisible_ && state.browserFloating);
    setPanelFloating(DockPanel::editor, state.editorFloating);
    setPanelFloating(DockPanel::bottom,
                     bottomPanelVisible_ && state.bottomFloating);
    updateLayoutConstraints();
    setEditorMode(state.editorMode);
    setBottomPanelMode(bottomPanelMode_);
    const auto graphIsFloating = graphWindow_ != nullptr;
    if (state.graphFloating != graphIsFloating)
        toggleGraphWindow();
    const auto restoreBounds = [](juce::DocumentWindow* window,
                                  juce::Rectangle<int> bounds,
                                  const int minimumWidth,
                                  const int minimumHeight) {
        if (window == nullptr)
            return;
        bounds.setSize(juce::jlimit(minimumWidth, 3840, bounds.getWidth()),
                       juce::jlimit(minimumHeight, 2160, bounds.getHeight()));
        window->setBounds(constrainToDisplays(bounds));
    };
    restoreBounds(inspectorWindow_.get(), state.inspectorWindowBounds, 240, 420);
    restoreBounds(browserWindow_.get(), state.browserWindowBounds, 280, 420);
    restoreBounds(editorWindow_.get(), state.editorWindowBounds, 640, 360);
    restoreBounds(bottomWindow_.get(), state.bottomWindowBounds, 560, 220);
    restoreBounds(graphWindow_.get(), state.graphWindowBounds, 560, 220);
    saveLayoutPreferences();
    markProjectDirty();
    refreshLayoutHistoryButtons();
}

void WorkspaceComponent::undoLayoutChange()
{
    if (layoutUndoStack_.empty())
        return;
    layoutRedoStack_.push_back(captureLayoutState());
    const auto state = layoutUndoStack_.back();
    layoutUndoStack_.pop_back();
    layoutSelector_.setSelectedId(0, juce::dontSendNotification);
    applyLayoutState(state, false);
}

void WorkspaceComponent::redoLayoutChange()
{
    if (layoutRedoStack_.empty())
        return;
    layoutUndoStack_.push_back(captureLayoutState());
    const auto state = layoutRedoStack_.back();
    layoutRedoStack_.pop_back();
    layoutSelector_.setSelectedId(0, juce::dontSendNotification);
    applyLayoutState(state, false);
}

void WorkspaceComponent::refreshLayoutHistoryButtons()
{
    undoLayoutButton_.setEnabled(!layoutUndoStack_.empty());
    redoLayoutButton_.setEnabled(!layoutRedoStack_.empty());
}

juce::var WorkspaceComponent::serialiseLayoutState(
    const LayoutState& state) const
{
    auto value = juce::DynamicObject::Ptr(new juce::DynamicObject());
    value->setProperty(jsonKeys::format, "CyDAW Layout");
    value->setProperty(jsonKeys::schema, 2);
    value->setProperty(jsonKeys::name, state.name);
    value->setProperty(jsonKeys::inspectorWidth, state.inspectorWidth);
    value->setProperty(jsonKeys::browserWidth, state.browserWidth);
    value->setProperty(jsonKeys::bottomHeight, state.bottomHeight);
    value->setProperty(jsonKeys::editorMode, state.editorMode);
    value->setProperty(jsonKeys::bottomMode, state.bottomMode);
    value->setProperty(jsonKeys::inspectorVisible, state.inspectorVisible);
    value->setProperty(jsonKeys::browserVisible, state.browserVisible);
    value->setProperty(jsonKeys::bottomVisible, state.bottomVisible);
    value->setProperty(jsonKeys::graphFloating, state.graphFloating);
    value->setProperty(jsonKeys::inspectorFloating, state.inspectorFloating);
    value->setProperty(jsonKeys::browserFloating, state.browserFloating);
    value->setProperty(jsonKeys::editorFloating, state.editorFloating);
    value->setProperty(jsonKeys::bottomFloating, state.bottomFloating);
    value->setProperty(jsonKeys::inspectorWindowBounds,
                       serialiseBounds(state.inspectorWindowBounds));
    value->setProperty(jsonKeys::browserWindowBounds,
                       serialiseBounds(state.browserWindowBounds));
    value->setProperty(jsonKeys::editorWindowBounds,
                       serialiseBounds(state.editorWindowBounds));
    value->setProperty(jsonKeys::bottomWindowBounds,
                       serialiseBounds(state.bottomWindowBounds));
    value->setProperty(jsonKeys::graphWindowBounds,
                       serialiseBounds(state.graphWindowBounds));
    return juce::var(value.get());
}

std::optional<WorkspaceComponent::LayoutState>
WorkspaceComponent::parseLayoutState(const juce::var& value) const
{
    const auto* object = value.getDynamicObject();
    if (object == nullptr
        || object->getProperty(jsonKeys::format).toString() != "CyDAW Layout")
        return std::nullopt;
    LayoutState state;
    state.name = object->getProperty(jsonKeys::name).toString().trim();
    if (state.name.isEmpty())
        return std::nullopt;
    state.inspectorWidth = static_cast<int>(object->getProperty(jsonKeys::inspectorWidth));
    state.browserWidth = static_cast<int>(object->getProperty(jsonKeys::browserWidth));
    state.bottomHeight = static_cast<int>(object->getProperty(jsonKeys::bottomHeight));
    state.editorMode = static_cast<int>(object->getProperty(jsonKeys::editorMode));
    state.bottomMode = static_cast<int>(object->getProperty(jsonKeys::bottomMode));
    state.inspectorVisible = static_cast<bool>(object->getProperty(jsonKeys::inspectorVisible));
    state.browserVisible = static_cast<bool>(object->getProperty(jsonKeys::browserVisible));
    state.bottomVisible = static_cast<bool>(object->getProperty(jsonKeys::bottomVisible));
    state.graphFloating = static_cast<bool>(object->getProperty(jsonKeys::graphFloating));
    state.inspectorFloating = static_cast<bool>(object->getProperty(jsonKeys::inspectorFloating));
    state.browserFloating = static_cast<bool>(object->getProperty(jsonKeys::browserFloating));
    state.editorFloating = static_cast<bool>(object->getProperty(jsonKeys::editorFloating));
    state.bottomFloating = static_cast<bool>(object->getProperty(jsonKeys::bottomFloating));
    state.inspectorWindowBounds = parseBounds(
        object->getProperty(jsonKeys::inspectorWindowBounds), state.inspectorWindowBounds);
    state.browserWindowBounds = parseBounds(
        object->getProperty(jsonKeys::browserWindowBounds), state.browserWindowBounds);
    state.editorWindowBounds = parseBounds(
        object->getProperty(jsonKeys::editorWindowBounds), state.editorWindowBounds);
    state.bottomWindowBounds = parseBounds(
        object->getProperty(jsonKeys::bottomWindowBounds), state.bottomWindowBounds);
    state.graphWindowBounds = parseBounds(
        object->getProperty(jsonKeys::graphWindowBounds), state.graphWindowBounds);
    return state;
}

void WorkspaceComponent::loadNamedLayouts()
{
    namedLayouts_.clear();
    const auto json = layoutProperties_.getUserSettings()->getValue(
        "namedLayouts", "[]");
    const auto parsed = juce::JSON::parse(json);
    if (const auto* values = parsed.getArray())
        for (const auto& value : *values)
            if (auto state = parseLayoutState(value))
                namedLayouts_.push_back(std::move(*state));
}

void WorkspaceComponent::saveNamedLayouts()
{
    juce::Array<juce::var> values;
    for (const auto& state : namedLayouts_)
        values.add(serialiseLayoutState(state));
    layoutProperties_.getUserSettings()->setValue(
        "namedLayouts", juce::JSON::toString(juce::var(values), false));
    layoutProperties_.saveIfNeeded();
}

void WorkspaceComponent::refreshLayoutSelector(const int selectedId)
{
    layoutSelector_.clear(juce::dontSendNotification);
    layoutSelector_.addItem(tr("layout.production"), 1);
    layoutSelector_.addItem(tr("layout.editing"), 2);
    layoutSelector_.addItem(tr("layout.modular"), 3);
    layoutSelector_.addItem(tr("layout.focus"), 4);
    if (!namedLayouts_.empty())
        layoutSelector_.addSeparator();
    for (std::size_t index = 0; index < namedLayouts_.size(); ++index)
        layoutSelector_.addItem(namedLayouts_[index].name,
                                100 + static_cast<int>(index));
    layoutSelector_.setTextWhenNothingSelected(tr("layout.select"));
    if (selectedId != 0)
        layoutSelector_.setSelectedId(selectedId, juce::dontSendNotification);
    deleteLayoutButton_.setEnabled(layoutSelector_.getSelectedId() >= 100);
}

void WorkspaceComponent::showLayoutPanelsMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader(tr("layout.panels"));
    menu.addItem(1, tr("layout.inspector"), true, inspectorPanelVisible_);
    menu.addItem(2, tr("layout.browser"), true, browserPanelVisible_);
    menu.addItem(3, tr("layout.bottom"), true, bottomPanelVisible_);
    menu.addSeparator();
    menu.addSectionHeader(tr("layout.windows"));
    menu.addItem(10, tr("layout.floatInspector"), true,
                 isPanelFloating(DockPanel::inspector));
    menu.addItem(11, tr("layout.floatBrowser"), true,
                 isPanelFloating(DockPanel::browser));
    menu.addItem(12, tr("layout.floatEditor"), true,
                 isPanelFloating(DockPanel::editor));
    menu.addItem(13, tr("layout.floatBottom"), true,
                 isPanelFloating(DockPanel::bottom));
    menu.addItem(14, tr("layout.graphWindow"), true, graphWindow_ != nullptr);
    menu.addSeparator();
    menu.addSectionHeader(tr("layout.actions"));
    menu.addItem(20, tr("layout.saveAction"));
    menu.addItem(21, tr("layout.undoAction"), !layoutUndoStack_.empty());
    menu.addItem(22, tr("layout.redoAction"), !layoutRedoStack_.empty());
    menu.addItem(23, tr("layout.deleteAction"),
                 layoutSelector_.getSelectedId() >= 100);

    const juce::Component::SafePointer<WorkspaceComponent> safeThis(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&layoutPanelsButton_),
        [safeThis](const int result) {
            auto* const workspace = safeThis.getComponent();
            if (workspace == nullptr || result == 0)
                return;
            if (result == 20) {
                workspace->promptSaveCurrentLayout();
                return;
            }
            if (result == 21) {
                workspace->undoLayoutChange();
                return;
            }
            if (result == 22) {
                workspace->redoLayoutChange();
                return;
            }
            if (result == 23) {
                workspace->deleteSelectedLayout();
                return;
            }
            auto state = workspace->captureLayoutState();
            if (result == 1)
                state.inspectorVisible = !state.inspectorVisible;
            else if (result == 2)
                state.browserVisible = !state.browserVisible;
            else if (result == 3)
                state.bottomVisible = !state.bottomVisible;
            else if (result == 10) {
                state.inspectorVisible = true;
                state.inspectorFloating = !state.inspectorFloating;
            } else if (result == 11) {
                state.browserVisible = true;
                state.browserFloating = !state.browserFloating;
            } else if (result == 12)
                state.editorFloating = !state.editorFloating;
            else if (result == 13) {
                state.bottomVisible = true;
                state.bottomFloating = !state.bottomFloating;
            } else if (result == 14)
                state.graphFloating = !state.graphFloating;
            workspace->layoutSelector_.setSelectedId(0, juce::dontSendNotification);
            workspace->applyLayoutState(state);
        });
}

void WorkspaceComponent::showFileMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, tr("toolbar.openAudio"));
    menu.addSeparator();
    menu.addItem(2, tr("toolbar.save"));
    menu.addItem(3, tr("toolbar.load"));
    // The small projects that ship ready to play, from the templates folder
    // next to the executable; see --template=.
    juce::Array<juce::File> templates;
    juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getSiblingFile("templates")
        .findChildFiles(templates, juce::File::findFiles, false, "*.cydaw");
    templates.sort();
    juce::PopupMenu templateMenu;
    for (auto index = 0; index < templates.size(); ++index)
        templateMenu.addItem(100 + index,
                             templates[index].getFileNameWithoutExtension());
    if (templates.isEmpty())
        templateMenu.addItem(99, tr("menu.templatesEmpty"), false);
    menu.addSubMenu(tr("menu.templates"), templateMenu);
    menu.addItem(6, tr("menu.importProject"));
    menu.addSeparator();
    menu.addItem(4, tr("toolbar.exportMix"), !audio_.isMasterExporting());
    menu.addItem(5, tr("toolbar.audio"));

    const juce::Component::SafePointer<WorkspaceComponent> safeThis(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&fileMenuButton_),
        [safeThis, templates](const int result) {
            auto* workspace = safeThis.getComponent();
            if (workspace == nullptr)
                return;
            if (result >= 100 && result - 100 < templates.size()) {
                workspace->loadProject(templates[result - 100]);
                return;
            }
            switch (result) {
                case 1: workspace->chooseAudioFile(); break;
                case 2: workspace->chooseProjectSaveFile(); break;
                case 3: workspace->chooseProjectLoadFile(); break;
                case 4: workspace->chooseMasterExportFile(); break;
                case 5: workspace->showAudioSettings(); break;
                case 6: workspace->chooseProjectImportFile(); break;
                default: break;
            }
        });
}

void WorkspaceComponent::showEditMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, tr("menu.undo"));
    menu.addItem(2, tr("menu.redo"));
    menu.addSeparator();
    menu.addItem(3, tr("command.searchShort"));

    const juce::Component::SafePointer<WorkspaceComponent> safeThis(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&editMenuButton_),
        [safeThis](const int result) {
            auto* workspace = safeThis.getComponent();
            if (workspace == nullptr || result == 0)
                return;
            if (result == 3) {
                workspace->showCommandPalette();
                return;
            }
            const auto undo = result == 1;
            if (workspace->currentEditorMode_ == 2) {
                if (undo) workspace->undoMidiEdit();
                else workspace->redoMidiEdit();
            } else if (workspace->currentEditorMode_ == 5) {
                if (undo) workspace->undoAudioEdit();
                else workspace->redoAudioEdit();
            } else {
                if (undo) workspace->undoArrangementEdit();
                else workspace->redoArrangementEdit();
            }
        });
}

void WorkspaceComponent::showViewMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader(tr("menu.workspaceViews"));
    menu.addItem(1, tr("editor.launcher"), true, currentEditorMode_ == 4);
    menu.addItem(2, tr("editor.arrange"), true, currentEditorMode_ == 1);
    menu.addItem(3, tr("editor.clip"), true, currentEditorMode_ == 6
                                             || currentEditorMode_ == 2);
    menu.addItem(4, tr("editor.graphEdit"), true, currentEditorMode_ == 7);
    menu.addItem(5, tr("editor.buses"), true, currentEditorMode_ == 8);
    menu.addItem(6, tr("editor.mixer"), true, currentEditorMode_ == 9);
    menu.addSeparator();
    menu.addSectionHeader(tr("layout.panels"));
    menu.addItem(10, tr("layout.inspector"), true, inspectorPanelVisible_);
    menu.addItem(11, tr("layout.browser"), true, browserPanelVisible_);
    menu.addItem(12, tr("layout.bottom"), true, bottomPanelVisible_);
    menu.addItem(13, tr("layout.windowsShort"));

    const juce::Component::SafePointer<WorkspaceComponent> safeThis(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&viewMenuButton_),
        [safeThis](const int result) {
            auto* workspace = safeThis.getComponent();
            if (workspace == nullptr || result == 0)
                return;
            if (result >= 1 && result <= 6) {
                static constexpr std::array<int, 6> modes { 4, 1, 6, 7, 8, 9 };
                workspace->setEditorMode(
                    modes[static_cast<std::size_t>(result - 1)]);
                return;
            }
            if (result == 13) {
                workspace->showLayoutPanelsMenu();
                return;
            }
            auto state = workspace->captureLayoutState();
            if (result == 10)
                state.inspectorVisible = !state.inspectorVisible;
            else if (result == 11)
                state.browserVisible = !state.browserVisible;
            else if (result == 12)
                state.bottomVisible = !state.bottomVisible;
            workspace->applyLayoutState(state);
        });
}

bool WorkspaceComponent::isPanelFloating(const DockPanel panel) const noexcept
{
    switch (panel) {
        case DockPanel::inspector: return inspectorWindow_ != nullptr;
        case DockPanel::browser: return browserWindow_ != nullptr;
        case DockPanel::editor: return editorWindow_ != nullptr;
        case DockPanel::bottom: return bottomWindow_ != nullptr;
    }
    return false;
}

void WorkspaceComponent::togglePanelWindow(const DockPanel panel)
{
    auto state = captureLayoutState();
    switch (panel) {
        case DockPanel::inspector:
            state.inspectorVisible = true;
            state.inspectorFloating = !state.inspectorFloating;
            break;
        case DockPanel::browser:
            state.browserVisible = true;
            state.browserFloating = !state.browserFloating;
            break;
        case DockPanel::editor:
            state.editorFloating = !state.editorFloating;
            break;
        case DockPanel::bottom:
            state.bottomVisible = true;
            state.bottomFloating = !state.bottomFloating;
            break;
    }
    layoutSelector_.setSelectedId(0, juce::dontSendNotification);
    applyLayoutState(state);
}

void WorkspaceComponent::setPanelFloating(const DockPanel panel,
                                          const bool shouldFloat)
{
    std::unique_ptr<FloatingPanelWindow>* window {};
    juce::Component* content {};
    juce::Component* dockSlot {};
    juce::String settingsKey;
    juce::String title;
    int defaultWidth {};
    int defaultHeight {};
    int minimumWidth {};
    int minimumHeight {};

    switch (panel) {
        case DockPanel::inspector:
            window = &inspectorWindow_;
            content = &inspectorPanelContainer_;
            dockSlot = &inspectorDockSlot_;
            settingsKey = "inspectorWindow";
            title = tr("layout.inspectorWindowTitle");
            defaultWidth = 320;
            defaultHeight = 680;
            minimumWidth = 240;
            minimumHeight = 420;
            break;
        case DockPanel::browser:
            window = &browserWindow_;
            content = &browser_;
            dockSlot = &browserDockSlot_;
            settingsKey = "browserWindow";
            title = tr("layout.browserWindowTitle");
            defaultWidth = 900;
            defaultHeight = 720;
            minimumWidth = 360;
            minimumHeight = 420;
            break;
        case DockPanel::editor:
            window = &editorWindow_;
            content = &editorArea_;
            dockSlot = &editorDockSlot_;
            settingsKey = "editorWindow";
            title = tr("layout.editorWindowTitle");
            defaultWidth = 1120;
            defaultHeight = 720;
            minimumWidth = 640;
            minimumHeight = 360;
            break;
        case DockPanel::bottom:
            window = &bottomWindow_;
            content = &nodeAreaContainer_;
            dockSlot = &bottomDockSlot_;
            settingsKey = "bottomWindow";
            title = tr("layout.bottomWindowTitle");
            defaultWidth = 980;
            defaultHeight = 380;
            minimumWidth = 560;
            minimumHeight = 220;
            break;
    }

    if (window == nullptr || content == nullptr || dockSlot == nullptr
        || shouldFloat == (*window != nullptr))
        return;

    if (shouldFloat) {
        const juce::Component::SafePointer<WorkspaceComponent> safeThis(this);
        *window = std::make_unique<FloatingPanelWindow>(
            title,
            [safeThis, panel] {
                juce::MessageManager::callAsync([safeThis, panel] {
                    if (safeThis != nullptr && safeThis->isPanelFloating(panel))
                        safeThis->togglePanelWindow(panel);
                });
            },
            [safeThis] {
                if (safeThis != nullptr)
                    safeThis->resized();
            });
        (*window)->setResizeLimits(minimumWidth, minimumHeight, 3840, 2160);
        content->setLookAndFeel(&lookAndFeel_);
        (*window)->setContentNonOwned(content, false);

        auto* settings = layoutProperties_.getUserSettings();
        const auto width = juce::jlimit(minimumWidth, 3840,
            settings->getIntValue(settingsKey + "Width", defaultWidth));
        const auto height = juce::jlimit(minimumHeight, 2160,
            settings->getIntValue(settingsKey + "Height", defaultHeight));
        const auto x = settings->getIntValue(settingsKey + "X", -1);
        const auto y = settings->getIntValue(settingsKey + "Y", -1);
        if (x >= 0 && y >= 0)
            (*window)->setBounds(constrainToDisplays({ x, y, width, height }));
        else
            (*window)->centreWithSize(width, height);
        (*window)->setVisible(true);
    } else {
        const auto bounds = (*window)->getBounds();
        auto* settings = layoutProperties_.getUserSettings();
        settings->setValue(settingsKey + "X", bounds.getX());
        settings->setValue(settingsKey + "Y", bounds.getY());
        settings->setValue(settingsKey + "Width", bounds.getWidth());
        settings->setValue(settingsKey + "Height", bounds.getHeight());
        (*window)->clearContentComponent();
        dockSlot->addAndMakeVisible(content);
        content->setLookAndFeel(nullptr);
        window->reset();
    }
    updateLayoutConstraints();
    refreshFloatingPanelTitles();
}

void WorkspaceComponent::dockAllPanelWindows()
{
    setPanelFloating(DockPanel::inspector, false);
    setPanelFloating(DockPanel::browser, false);
    setPanelFloating(DockPanel::editor, false);
    setPanelFloating(DockPanel::bottom, false);
}

void WorkspaceComponent::refreshFloatingPanelTitles()
{
    if (inspectorWindow_ != nullptr)
        inspectorWindow_->setName(tr("layout.inspectorWindowTitle"));
    if (browserWindow_ != nullptr)
        browserWindow_->setName(tr("layout.browserWindowTitle"));
    if (editorWindow_ != nullptr)
        editorWindow_->setName(tr("layout.editorWindowTitle"));
    if (bottomWindow_ != nullptr)
        bottomWindow_->setName(tr("layout.bottomWindowTitle"));
}

void WorkspaceComponent::promptSaveCurrentLayout()
{
    auto* dialog = new juce::AlertWindow(
        tr("layout.saveTitle"), tr("layout.savePrompt"),
        juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor(
        "name",
        tr("layout.userDefault") + " "
            + juce::String(static_cast<int>(namedLayouts_.size() + 1)),
        tr("layout.name"));
    dialog->addButton(tr("layout.save"), 1,
                      juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton(tr("layout.cancel"), 0,
                      juce::KeyPress(juce::KeyPress::escapeKey));
    const juce::Component::SafePointer<WorkspaceComponent> safeThis(this);
    dialog->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [safeThis, dialog](const int result) {
                auto* const workspace = safeThis.getComponent();
                if (workspace == nullptr || result != 1)
                    return;
                const auto name = dialog->getTextEditorContents("name").trim();
                if (name.isEmpty())
                    return;
                auto state = workspace->captureLayoutState(name);
                const auto existing = std::find_if(
                    workspace->namedLayouts_.begin(),
                    workspace->namedLayouts_.end(),
                    [&name](const auto& candidate) {
                        return candidate.name.equalsIgnoreCase(name);
                    });
                auto selectedId = 0;
                if (existing != workspace->namedLayouts_.end()) {
                    *existing = std::move(state);
                    selectedId = 100 + static_cast<int>(
                        std::distance(workspace->namedLayouts_.begin(), existing));
                } else {
                    workspace->namedLayouts_.push_back(std::move(state));
                    selectedId = 99
                                 + static_cast<int>(workspace->namedLayouts_.size());
                }
                workspace->saveNamedLayouts();
                workspace->refreshLayoutSelector(selectedId);
            }),
        true);
}

void WorkspaceComponent::deleteSelectedLayout()
{
    const auto id = layoutSelector_.getSelectedId();
    if (id < 100)
        return;
    const auto index = static_cast<std::size_t>(id - 100);
    if (index >= namedLayouts_.size())
        return;
    namedLayouts_.erase(namedLayouts_.begin()
                        + static_cast<std::ptrdiff_t>(index));
    saveNamedLayouts();
    refreshLayoutSelector();
}

void WorkspaceComponent::toggleGraphWindow()
{
    if (graphWindow_ != nullptr) {
        dockGraphWindow();
        return;
    }

    if (currentEditorMode_ == 7)
        setEditorMode(editorModeBeforeGraph_);

    juce::Component::SafePointer<WorkspaceComponent> safeThis(this);
    graphWindow_ = std::make_unique<FloatingGraphWindow>([safeThis] {
        juce::MessageManager::callAsync([safeThis] {
            if (safeThis != nullptr)
                safeThis->dockGraphWindow();
        });
    });
    graphWindow_->setContentNonOwned(&nodeGraph_, false);

    auto* settings = layoutProperties_.getUserSettings();
    auto savedWidth = settings->getIntValue("graphWidth", 1440);
    auto savedHeight = settings->getIntValue("graphHeight", 860);
    // Migrate the former 980 x 360 default once. It was useful as a compact
    // utility strip, but is far too small now that the graph is a first-class
    // instrument/effect authoring environment.
    if (savedWidth == 980 && savedHeight == 360) {
        savedWidth = 1440;
        savedHeight = 860;
    }
    const auto width = juce::jlimit(900, 3840, savedWidth);
    const auto height = juce::jlimit(560, 2160, savedHeight);
    const auto x = settings->getIntValue("graphX", -1);
    const auto y = settings->getIntValue("graphY", -1);
    if (x >= 0 && y >= 0)
        graphWindow_->setBounds(constrainToDisplays({ x, y, width, height }));
    else
        graphWindow_->centreWithSize(width, height);
    graphWindow_->setVisible(true);
    detachGraphButton_.setButtonText(tr("toolbar.dockGraph"));
    setBottomPanelMode(1);
}

void WorkspaceComponent::dockGraphWindow()
{
    if (graphWindow_ == nullptr)
        return;

    const auto bounds = graphWindow_->getBounds();
    auto* settings = layoutProperties_.getUserSettings();
    settings->setValue("graphX", bounds.getX());
    settings->setValue("graphY", bounds.getY());
    settings->setValue("graphWidth", bounds.getWidth());
    settings->setValue("graphHeight", bounds.getHeight());
    graphWindow_->clearContentComponent();
    graphWindow_.reset();
    detachGraphButton_.setButtonText(tr("toolbar.floatGraph"));
    editorArea_.addAndMakeVisible(nodeGraph_);
    setEditorMode(7);
}

void WorkspaceComponent::showError(const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, title, message);
}

void WorkspaceComponent::refreshInspectorControls()
{
    const auto showTrackControls
        = inspectorSelection_ == InspectorSelection::audioTrack
          || inspectorSelection_ == InspectorSelection::midiTrack;
    const auto showClipControls
        = inspectorSelection_ == InspectorSelection::audioClip;
    const auto showEventControls
        = inspectorSelection_ == InspectorSelection::audioEvent;
    const auto showAudioSelectionControls
        = showClipControls || showEventControls;
    const auto layoutChanged
        = gainSlider_.isVisible() != showTrackControls
          || clipStartSlider_.isVisible() != showAudioSelectionControls
          || sampleGainSlider_.isVisible() != showEventControls
          || sampleFadeInSlider_.isVisible() != showEventControls;
    for (auto* component : std::initializer_list<juce::Component*> {
             &gainLabel_, &panLabel_, &gainSlider_, &panSlider_,
             &muteButton_, &soloButton_, &armButton_ })
        component->setVisible(showTrackControls);
    sampleGainLabel_.setVisible(showEventControls);
    sampleGainSlider_.setVisible(showEventControls);
    sampleFadeInLabel_.setVisible(showEventControls);
    sampleFadeInSlider_.setVisible(showEventControls);
    sampleFadeOutLabel_.setVisible(showEventControls);
    sampleFadeOutSlider_.setVisible(showEventControls);

    clipStartLabel_.setText(
        tr(showEventControls ? "sample.position" : "clip.start"),
        juce::dontSendNotification);
    sourceOffsetLabel_.setText(
        tr(showEventControls ? "sample.sourceOffset" : "clip.offset"),
        juce::dontSendNotification);
    clipDurationLabel_.setText(
        tr(showEventControls ? "sample.sourceDuration" : "clip.duration"),
        juce::dontSendNotification);
    clipStretchLabel_.setText(
        tr(showEventControls ? "sample.timelineDuration" : "clip.stretch"),
        juce::dontSendNotification);
    for (auto* component : std::initializer_list<juce::Component*> {
             &clipStartLabel_, &clipStartSlider_, &sourceOffsetLabel_,
             &sourceOffsetSlider_, &clipDurationLabel_,
             &clipDurationSlider_, &clipStretchLabel_,
             &clipStretchSlider_, &sourceTempoLabel_,
             &sourceTempoSlider_, &stretchModeLabel_,
             &stretchModeSelector_ })
        component->setVisible(showAudioSelectionControls);
    for (auto* component : std::initializer_list<juce::Component*> {
             &detectTempoButton_, &syncTempoButton_, &audioTakeLabel_,
             &audioTakeSelector_, &addAudioTakeButton_,
             &recordAudioTakeButton_, &removeAudioTakeButton_ })
        component->setVisible(showClipControls);
    if (layoutChanged && getWidth() > 0 && getHeight() > 0)
        resized();
}

void WorkspaceComponent::updateTrackDescription()
{
    if (inspectorSelection_ == InspectorSelection::audioClip
        && (!selectedTrack_
            || *selectedTrack_ >= audio_.trackCount()
            || !audio_.audioClip(*selectedTrack_, selectedAudioClipId_)
                    .has_value()))
        inspectorSelection_ = InspectorSelection::audioTrack;
    refreshInspectorControls();
    if (inspectorSelection_ == InspectorSelection::audioEvent
        && selectedClipEventId_ != 0) {
        std::optional<AudioTrackController::AudioEvent> selectedEvent;
        juce::String parentClip;
        juce::String trackName;
        if (launcherEditorContext_) {
            const auto context = *launcherEditorContext_;
            const auto clip = audio_.launcherClip(context.trackIndex,
                                                   context.sceneIndex);
            if (clip.has_value()) {
                parentClip = clip->name;
                trackName = context.trackIndex < trackPresentations_.size()
                                ? trackPresentations_[context.trackIndex].name
                                : tr("launcher.audioTrack", "index",
                                     juce::String(static_cast<int>(
                                         context.trackIndex + 1)));
                const auto found = std::find_if(
                    clip->events.begin(), clip->events.end(),
                    [this](const auto& value) {
                        return value.id == selectedClipEventId_;
                    });
                if (found != clip->events.end())
                    selectedEvent = *found;
                else if (!clip->usesEventTimeline
                         && selectedClipEventId_
                                == launcherClipEditorEventId
                         && clip->sourceFile != juce::File {})
                    selectedEvent = AudioTrackController::AudioEvent {
                        launcherClipEditorEventId, clip->name, 0.0,
                        clip->sourceOffsetSeconds, clip->durationSeconds,
                        clip->durationSeconds, clip->colour,
                        clip->sourceFile,
                        clip->sourceOffsetSeconds + clip->durationSeconds
                    };
            }
        } else if (selectedTrack_ && selectedAudioClipId_ != 0) {
            const auto clip = audio_.audioClip(*selectedTrack_,
                                               selectedAudioClipId_);
            const auto events = audio_.audioClipEvents(
                *selectedTrack_, selectedAudioClipId_);
            const auto found = std::find_if(
                events.begin(), events.end(), [this](const auto& value) {
                    return value.id == selectedClipEventId_;
                });
            if (found != events.end())
                selectedEvent = *found;
            if (clip.has_value())
                parentClip = clip->name;
            if (*selectedTrack_ < trackPresentations_.size())
                trackName = trackPresentations_[*selectedTrack_].name;
        }
        if (selectedEvent.has_value()) {
            auto description = tr("inspector.audioEvent")
                .replace("{name}", selectedEvent->name)
                .replace("{clip}", parentClip)
                .replace("{track}", trackName)
                .replace("{file}", selectedEvent->sourceFile.getFileName())
                .replace("{start}", juce::String(
                    selectedEvent->startSeconds, 3))
                .replace("{offset}", juce::String(
                    selectedEvent->sourceOffsetSeconds, 3))
                .replace("{duration}", juce::String(
                    selectedEvent->durationSeconds, 3))
                .replace("{source}", juce::String(
                    selectedEvent->sourceDurationSeconds, 3))
                .replace("{markers}", juce::String(static_cast<int>(
                    selectedEvent->stretchMarkers.size())))
                .replace("{fadein}", juce::String(
                    selectedEvent->fadeInSeconds, 3))
                .replace("{fadeout}", juce::String(
                    selectedEvent->fadeOutSeconds, 3));
            inspector_.setTitle(tr("inspector.audioEventTitle"));
            inspector_.setBody(description);
            return;
        }
        selectedClipEventId_ = 0;
        inspectorSelection_ = launcherEditorContext_
                                  ? InspectorSelection::launcherAudioClip
                                  : InspectorSelection::audioClip;
        refreshInspectorControls();
    }
    if ((inspectorSelection_ == InspectorSelection::midiTrack
         || inspectorSelection_ == InspectorSelection::midiClip)
        && selectedMidiTrackId_ != 0) {
        const auto tracks = audio_.midiTracks();
        const auto track = std::find_if(
            tracks.begin(), tracks.end(), [this](const auto& value) {
                return value.id == selectedMidiTrackId_;
            });
        if (track != tracks.end()) {
            const auto clip = std::find_if(
                track->clips.begin(), track->clips.end(),
                [this](const auto& value) {
                    return value.id == selectedMidiClipId_;
                });
            const auto clipSelected
                = inspectorSelection_ == InspectorSelection::midiClip
                  && clip != track->clips.end();
            auto description = tr(clipSelected ? "inspector.midiClip"
                                               : "inspector.midiTrack");
            description = description.replace("{name}", track->name)
                              .replace("{count}", juce::String(
                                  static_cast<int>(track->clips.size())))
                              .replace("{clip}", clipSelected
                                                     ? clip->name
                                                     : juce::String("-"))
                              .replace("{start}", clipSelected
                                                      ? juce::String(
                                                            clip->startBeats,
                                                            2)
                                                      : juce::String("0.00"))
                              .replace("{length}", clipSelected
                                                       ? juce::String(
                                                             clip->lengthBeats,
                                                             2)
                                                       : juce::String("0.00"))
                              .replace("{notes}", clipSelected
                                                      ? juce::String(
                                                            static_cast<int>(
                                                                clip->notes.size()))
                                                      : juce::String("0"));
            inspector_.setTitle(tr(clipSelected
                                       ? "inspector.midiClipTitle"
                                       : "inspector.midiTrackTitle"));
            inspector_.setBody(description);
            return;
        }
    }
    if (inspectorSelection_ == InspectorSelection::launcherAudioClip
        && launcherInspectorSelection_) {
        const auto context = *launcherInspectorSelection_;
        const auto clip = audio_.launcherClip(context.trackIndex,
                                               context.sceneIndex);
        if (clip.has_value()) {
            auto description = tr("inspector.launcherClip");
            description = description
                .replace("{name}", clip->name)
                .replace("{scene}", juce::String(context.sceneIndex + 1))
                .replace("{duration}", juce::String(
                    clip->durationSeconds, 3))
                .replace("{start}", juce::String(
                    clip->playStartSeconds, 3))
                .replace("{end}", juce::String(
                    clip->playEndSeconds, 3))
                .replace("{loop}", clip->looping
                                       ? tr("common.yes")
                                       : tr("common.no"))
                .replace("{events}", juce::String(
                    static_cast<int>(clip->events.size())));
            inspector_.setTitle(tr("inspector.clipTitle"));
            inspector_.setBody(description);
            return;
        }
    }
    inspector_.setTitle(tr(inspectorSelection_
                                   == InspectorSelection::audioClip
                               ? "inspector.clipTitle"
                               : "inspector.trackTitle"));
    if (!selectedTrack_ || *selectedTrack_ >= audio_.trackCount()) {
        inspector_.setBody(tr("inspector.empty"));
        return;
    }

    const auto info = audio_.trackInfo(*selectedTrack_);
    const auto clip = inspectorSelection_ == InspectorSelection::audioClip
        ? audio_.audioClip(*selectedTrack_, selectedAudioClipId_)
        : std::optional<AudioTrackController::AudioClip> {};
    const auto trackName = *selectedTrack_ < trackPresentations_.size()
                               ? trackPresentations_[*selectedTrack_].name
                               : tr("launcher.audioTrack", "index",
                                    juce::String(static_cast<int>(
                                        *selectedTrack_ + 1)));
    auto description = tr(clip.has_value() ? "inspector.audioClip"
                                           : "inspector.audioTrack");
    description = description
        .replace("{index}", juce::String(
            static_cast<int>(*selectedTrack_ + 1)))
        .replace("{track}", trackName)
        .replace("{name}", clip.has_value() ? clip->name
                                             : trackName)
        .replace("{file}", clip.has_value()
                               ? clip->sourceFile.getFileName()
                               : info.file.getFileName())
        .replace("{length}", formatTime(info.lengthSeconds))
        .replace("{count}", juce::String(
            static_cast<int>(info.clips.size())))
        .replace("{start}", juce::String(
            clip.has_value() ? clip->startSeconds : 0.0, 3))
        .replace("{offset}", juce::String(
            clip.has_value() ? clip->sourceOffsetSeconds : 0.0, 3))
        .replace("{duration}", juce::String(
            clip.has_value() ? clip->durationSeconds : 0.0, 3));
    const auto selectedClipCount = arrangementMarqueeSelectionActive_
                                       ? arrangementMarqueeClipCount()
                                       : selectedAudioClipIds_.size();
    if (selectedClipCount > 1)
        description += "\n" + tr(
            "inspector.multiSelection", "count",
            juce::String(static_cast<int>(selectedClipCount)));
    if (clip.has_value()) {
        const auto stretch = clip->durationSeconds
                             / std::max(0.001,
                                        clip->sourceDurationSeconds);
        auto stretchSummary = tr("inspector.stretch", "rate",
                                 juce::String(stretch, 2));
        description += "\n" + stretchSummary.replace(
            "{source}", juce::String(clip->sourceDurationSeconds, 3));
    }
    if (!info.plugins.empty()) {
        juce::StringArray names;
        for (const auto& plugin : info.plugins)
            names.add(plugin.name);
        description += "\n" + tr("inspector.pluginChain",
                                   "count", juce::String(static_cast<int>(info.plugins.size())))
                               .replace("{names}", names.joinIntoString(" → "));
    }
    inspector_.setBody(description);
}

void WorkspaceComponent::markProjectDirty()
{
    if (!projectDirty_)
        nextProjectAutosaveMs_ = juce::Time::currentTimeMillis() + 30'000;
    projectDirty_ = true;
    const auto name = currentProjectFile_ == juce::File {} ? tr("app.untitled")
                                                            : currentProjectFile_.getFileNameWithoutExtension();
    projectLabel_.setText("CyDAW  /  " + name + " *", juce::dontSendNotification);
}

juce::String WorkspaceComponent::formatTime(const double seconds)
{
    const auto safe = std::max(0.0, seconds);
    const auto totalMilliseconds = static_cast<juce::int64>(std::llround(safe * 1'000.0));
    const auto hours = totalMilliseconds / 3'600'000;
    const auto minutes = (totalMilliseconds / 60'000) % 60;
    const auto secs = (totalMilliseconds / 1'000) % 60;
    const auto millis = totalMilliseconds % 1'000;
    return juce::String::formatted("%02lld:%02lld:%02lld.%03lld", hours, minutes, secs, millis);
}

bool WorkspaceComponent::isSupportedAudioFile(const juce::File& file)
{
    const auto extension = file.getFileExtension().toLowerCase();
    return extension == ".wav" || extension == ".aif" || extension == ".aiff"
           || extension == ".flac" || extension == ".ogg" || extension == ".mp3";
}

bool WorkspaceComponent::isSupportedMidiFile(const juce::File& file)
{
    const auto extension = file.getFileExtension().toLowerCase();
    return extension == ".mid" || extension == ".midi";
}

} // namespace cydaw::app
