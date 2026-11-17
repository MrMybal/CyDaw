#include "GraphDocument.h"
#include "GraphPluginProjectGenerator.h"
#include "GraphPresetFile.h"
#include "JsonValue.h"
#include "Localization.h"
#include "NativeDspGraphState.h"
#include "NativeDspRuntime.h"
#include "NodeGraphComponentInternal.h"
#include "NodeParameterPresetFile.h"
#include "SamplerMapFile.h"
#include "SoundModelFile.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cydaw/base/Character.h>
#include <cydaw/base/Properties.h>
#include <cydaw/base/Thread.h>
#include <cydaw/document/PolySynthState.h>
#include <cydaw/engine/Containers.h>
#include <cydaw/engine/EffectCatalogue.h>
#include <cydaw/engine/GraphInstrumentCompiler.h>
#include <cydaw/engine/Modulators.h>
#include <cydaw/engine/NativeDspParameters.h>
#include <cydaw/graph/PrimitiveRegistry.h>
#include <cydaw/graph/SoundSources.h>
#include <cydaw/integration/AudioFiles.h>
#include <iterator>
#include <limits>
#include <tuple>
#include <unordered_set>
namespace cydaw::app {
namespace jsonKeys {
// Property names as identifiers made once: an Identifier built from a
// literal is a string-pool lookup, and a restore or a save of a large
// graph made tens of thousands of them.
static const cydaw::base::Identifier acceptsMidi("acceptsMidi");
static const cydaw::base::Identifier alias("alias");
static const cydaw::base::Identifier attackMilliseconds("attackMilliseconds");
static const cydaw::base::Identifier audioChannels("audioChannels");
static const cydaw::base::Identifier audioType("audioType");
static const cydaw::base::Identifier author("author");
static const cydaw::base::Identifier automatable("automatable");
static const cydaw::base::Identifier bandFrequencyHz("bandFrequencyHz");
static const cydaw::base::Identifier bandGainDb("bandGainDb");
static const cydaw::base::Identifier bandQ("bandQ");
static const cydaw::base::Identifier buses("buses");
static const cydaw::base::Identifier category("category");
static const cydaw::base::Identifier channels("channels");
static const cydaw::base::Identifier clipThreshold("clipThreshold");
static const cydaw::base::Identifier bypassed("bypassed");
static const cydaw::base::Identifier collapsed("collapsed");
static const cydaw::base::Identifier colour("colour");
static const cydaw::base::Identifier comments("comments");
static const cydaw::base::Identifier companyName("companyName");
static const cydaw::base::Identifier
    configurationPanelShowsMembers("configurationPanelShowsMembers");
static const cydaw::base::Identifier
    configurationPanelShowsPalette("configurationPanelShowsPalette");
static const cydaw::base::Identifier
    configurationPanelVisible("configurationPanelVisible");
static const cydaw::base::Identifier
    configurationPanelWidth("configurationPanelWidth");
static const cydaw::base::Identifier connections("connections");
static const cydaw::base::Identifier contract("contract");
static const cydaw::base::Identifier cutoffHz("cutoffHz");
static const cydaw::base::Identifier decayMilliseconds("decayMilliseconds");
static const cydaw::base::Identifier default_("default");
static const cydaw::base::Identifier definitionId("definitionId");
static const cydaw::base::Identifier definitionVersion("definitionVersion");
static const cydaw::base::Identifier delayMilliseconds("delayMilliseconds");
static const cydaw::base::Identifier description("description");
static const cydaw::base::Identifier destination("destination");
static const cydaw::base::Identifier destinationPort("destinationPort");
static const cydaw::base::Identifier driveDb("driveDb");
static const cydaw::base::Identifier editor("editor");
static const cydaw::base::Identifier exportedToHost("exportedToHost");
static const cydaw::base::Identifier factory("factory");
static const cydaw::base::Identifier factoryPreset("factoryPreset");
static const cydaw::base::Identifier feedback("feedback");
static const cydaw::base::Identifier
    followerAttackSeconds("followerAttackSeconds");
static const cydaw::base::Identifier
    followerReleaseSeconds("followerReleaseSeconds");
static const cydaw::base::Identifier format("format");
static const cydaw::base::Identifier gainDb("gainDb");
static const cydaw::base::Identifier graphAuthor("graphAuthor");
static const cydaw::base::Identifier graphCategory("graphCategory");
static const cydaw::base::Identifier graphDescription("graphDescription");
static const cydaw::base::Identifier graphEditorState("graphEditorState");
static const cydaw::base::Identifier graphId("graphId");
static const cydaw::base::Identifier graphName("graphName");
static const cydaw::base::Identifier graphParameterId("graphParameterId");
static const cydaw::base::Identifier graphPortId("graphPortId");
static const cydaw::base::Identifier graphRole("graphRole");
static const cydaw::base::Identifier graphScope("graphScope");
static const cydaw::base::Identifier graphTags("graphTags");
static const cydaw::base::Identifier graphVersion("graphVersion");
static const cydaw::base::Identifier hasEditor("hasEditor");
static const cydaw::base::Identifier hasInput("hasInput");
static const cydaw::base::Identifier hasOutput("hasOutput");
static const cydaw::base::Identifier height("height");
static const cydaw::base::Identifier highpassCutoffHz("highpassCutoffHz");
static const cydaw::base::Identifier hostValues("hostValues");
static const cydaw::base::Identifier id("id");
static const cydaw::base::Identifier identity("identity");
static const cydaw::base::Identifier inline_("inline");
static const cydaw::base::Identifier inputChannels("inputChannels");
static const cydaw::base::Identifier inputKind("inputKind");
static const cydaw::base::Identifier interface("interface");
static const cydaw::base::Identifier
    interfaceLayoutOverridden("interfaceLayoutOverridden");
static const cydaw::base::Identifier isSynth("isSynth");
static const cydaw::base::Identifier key("key");
static const cydaw::base::Identifier kind("kind");
static const cydaw::base::Identifier locked("locked");
static const cydaw::base::Identifier main("main");
static const cydaw::base::Identifier manufacturerCode("manufacturerCode");
static const cydaw::base::Identifier mapInputMaximum("mapInputMaximum");
static const cydaw::base::Identifier mapInputMinimum("mapInputMinimum");
static const cydaw::base::Identifier mapOutputMaximum("mapOutputMaximum");
static const cydaw::base::Identifier mapOutputMinimum("mapOutputMinimum");
static const cydaw::base::Identifier maximum("maximum");
static const cydaw::base::Identifier midiMaximumNote("midiMaximumNote");
static const cydaw::base::Identifier midiMinimumNote("midiMinimumNote");
static const cydaw::base::Identifier midiTranspose("midiTranspose");
static const cydaw::base::Identifier midiVelocityScale("midiVelocityScale");
static const cydaw::base::Identifier minimapVisible("minimapVisible");
static const cydaw::base::Identifier minimum("minimum");
static const cydaw::base::Identifier mix("mix");
static const cydaw::base::Identifier modulatable("modulatable");
static const cydaw::base::Identifier modulationDepth("modulationDepth");
static const cydaw::base::Identifier modulationOffset("modulationOffset");
static const cydaw::base::Identifier modulationRateHz("modulationRateHz");
static const cydaw::base::Identifier modulationShape("modulationShape");
static const cydaw::base::Identifier modules("modules");
static const cydaw::base::Identifier name("name");
static const cydaw::base::Identifier native("native");
static const cydaw::base::Identifier nextCommentId("nextCommentId");
static const cydaw::base::Identifier nextNodeId("nextNodeId");
static const cydaw::base::Identifier nodes("nodes");
static const cydaw::base::Identifier noiseLevel("noiseLevel");
static const cydaw::base::Identifier offsetX("offsetX");
static const cydaw::base::Identifier offsetY("offsetY");
static const cydaw::base::Identifier
    oscillatorFrequencyHz("oscillatorFrequencyHz");
static const cydaw::base::Identifier oscillatorLevel("oscillatorLevel");
static const cydaw::base::Identifier oscillatorWaveform("oscillatorWaveform");
static const cydaw::base::Identifier outputChannels("outputChannels");
static const cydaw::base::Identifier outputKind("outputKind");
static const cydaw::base::Identifier pan("pan");
static const cydaw::base::Identifier parameters("parameters");
static const cydaw::base::Identifier parentClassKey("parentClassKey");
static const cydaw::base::Identifier pluginCode("pluginCode");
static const cydaw::base::Identifier pluginParameterId("pluginParameterId");
static const cydaw::base::Identifier polySynth("polySynth");
static const cydaw::base::Identifier portDefaults("portDefaults");
static const cydaw::base::Identifier presentation("presentation");
static const cydaw::base::Identifier producesMidi("producesMidi");
static const cydaw::base::Identifier productName("productName");
static const cydaw::base::Identifier publishedParameters("publishedParameters");
static const cydaw::base::Identifier ratio("ratio");
static const cydaw::base::Identifier releaseMilliseconds("releaseMilliseconds");
static const cydaw::base::Identifier resonance("resonance");
static const cydaw::base::Identifier role("role");
static const cydaw::base::Identifier roomSize("roomSize");
static const cydaw::base::Identifier routing("routing");
static const cydaw::base::Identifier schema("schema");
static const cydaw::base::Identifier
    selectionPanelVisible("selectionPanelVisible");
static const cydaw::base::Identifier selectionPanelWidth("selectionPanelWidth");
static const cydaw::base::Identifier slot("slot");
static const cydaw::base::Identifier source("source");
static const cydaw::base::Identifier sourcePort("sourcePort");
static const cydaw::base::Identifier stereoWidth("stereoWidth");
static const cydaw::base::Identifier stringDamping("stringDamping");
static const cydaw::base::Identifier stringDecaySeconds("stringDecaySeconds");
static const cydaw::base::Identifier voiceMode("voiceMode");
static const cydaw::base::Identifier glideMilliseconds("glideMilliseconds");
static const cydaw::base::Identifier soundModel("soundModel");
static const cydaw::base::Identifier soundModelValues("soundModelValues");
static const cydaw::base::Identifier sampler("sampler");
static const cydaw::base::Identifier samplerValues("samplerValues");
static const cydaw::base::Identifier prismValues("prismValues");
static const cydaw::base::Identifier kickValues("kickValues");
static const cydaw::base::Identifier sustainLevel("sustainLevel");
static const cydaw::base::Identifier tags("tags");
static const cydaw::base::Identifier target("target");
static const cydaw::base::Identifier targetNodeId("targetNodeId");
static const cydaw::base::Identifier targetParameter("targetParameter");
static const cydaw::base::Identifier thresholdDb("thresholdDb");
static const cydaw::base::Identifier title("title");
static const cydaw::base::Identifier tone("tone");
static const cydaw::base::Identifier type("type");
static const cydaw::base::Identifier unit("unit");
static const cydaw::base::Identifier value("value");
static const cydaw::base::Identifier valueGraph("valueGraph");
static const cydaw::base::Identifier valueInputA("valueInputA");
static const cydaw::base::Identifier valueInputB("valueInputB");
static const cydaw::base::Identifier valueMaximum("valueMaximum");
static const cydaw::base::Identifier valueMinimum("valueMinimum");
static const cydaw::base::Identifier valueOperand("valueOperand");
static const cydaw::base::Identifier version("version");
static const cydaw::base::Identifier viewAnchorDX("viewAnchorDX");
static const cydaw::base::Identifier viewAnchorDY("viewAnchorDY");
static const cydaw::base::Identifier viewAnchorNode("viewAnchorNode");
static const cydaw::base::Identifier viewBookmarks("viewBookmarks");
static const cydaw::base::Identifier viewCentreX("viewCentreX");
static const cydaw::base::Identifier viewCentreY("viewCentreY");
static const cydaw::base::Identifier viewOffsetX("viewOffsetX");
static const cydaw::base::Identifier viewOffsetY("viewOffsetY");
static const cydaw::base::Identifier viewZoom("viewZoom");
static const cydaw::base::Identifier visibleInDevice("visibleInDevice");
static const cydaw::base::Identifier vst3Export("vst3Export");
static const cydaw::base::Identifier wavetable("wavetable");
static const cydaw::base::Identifier
    wavetableDetuneCents("wavetableDetuneCents");
static const cydaw::base::Identifier wavetablePosition("wavetablePosition");
static const cydaw::base::Identifier width("width");
static const cydaw::base::Identifier x("x");
static const cydaw::base::Identifier y("y");
static const cydaw::base::Identifier zoom("zoom");
} // namespace jsonKeys
namespace {
cydaw::base::Value serialiseGraphPrograms(
    const std::vector<GraphDocument::EditorProgram> &programs) {
  cydaw::base::Array<cydaw::base::Value> entries;
  for (const auto &program : programs) {
    auto entry = cydaw::base::Object::Ptr(new cydaw::base::Object());
    entry->setProperty("name", program.name);
    auto values = cydaw::base::Object::Ptr(new cydaw::base::Object());
    for (const auto &[key, value] : program.values)
      values->setProperty(cydaw::base::Identifier(key), value);
    entry->setProperty("values", cydaw::base::Value(values.get()));
    entries.add(cydaw::base::Value(entry.get()));
  }
  return entries;
}
std::vector<GraphDocument::EditorProgram>
restoreGraphPrograms(const cydaw::base::Value &state) {
  std::vector<GraphDocument::EditorProgram> programs;
  if (const auto *entries = state.getArray())
    for (const auto &encoded : *entries) {
      GraphDocument::EditorProgram program;
      program.name =
          encoded.getProperty("name", {}).toString().trim().substring(0, 128);
      if (const auto *values =
              encoded.getProperty("values", {}).getDynamicObject())
        for (const auto &property : values->getProperties())
          if ((property.value.isDouble() || property.value.isInt() ||
               property.value.isInt64()) &&
              std::isfinite(static_cast<double>(property.value)))
            program.values.emplace_back(property.name.toString(),
                                        static_cast<double>(property.value));
      if (program.name.isNotEmpty() && programs.size() < 512 &&
          std::none_of(
              programs.begin(), programs.end(),
              [&](const auto &other) { return other.name == program.name; }))
        programs.push_back(std::move(program));
    }
  return programs;
}
void writeCardAssets(
    cydaw::base::Object &value,
    const std::shared_ptr<const cydaw::engine::WavetableShape> &shape,
    const cydaw::base::Value &soundModelJson,
    const cydaw::base::Value &samplerJson,
    const cydaw::base::Value &prismTablesJson,
    const cydaw::base::Value &impulseJson, const bool bypassed,
    const cydaw::base::Value &instrumentJson,
    const cydaw::base::TextList &branchNames) {
  // Written as a top-level card writes them.
  if (bypassed)
    value.setProperty(jsonKeys::bypassed, true);
  if (instrumentJson.isObject())
    value.setProperty("instrument", instrumentJson);
  if (std::any_of(branchNames.begin(), branchNames.end(),
                  [](const auto &name) { return name.isNotEmpty(); })) {
    cydaw::base::Array<cydaw::base::Value> names;
    for (const auto &name : branchNames)
      names.add(name);
    value.setProperty("branchNames", names);
  }
  if (samplerJson.isObject())
    value.setProperty(jsonKeys::sampler, samplerJson);
  if (prismTablesJson.isObject())
    value.setProperty("prismTables", prismTablesJson);
  if (impulseJson.isObject())
    value.setProperty("impulse", impulseJson);
  if (shape != nullptr) {
    cydaw::base::Array<cydaw::base::Value> harmonics;
    harmonics.ensureStorageAllocated(
        static_cast<int>(cydaw::engine::WavetableShape::frameCount *
                         cydaw::engine::WavetableShape::harmonicCount));
    for (const auto &frame : shape->harmonics)
      for (const auto amplitude : frame)
        harmonics.add(amplitude);
    value.setProperty(jsonKeys::wavetable, harmonics);
  }
  if (soundModelJson.isObject())
    value.setProperty(jsonKeys::soundModel, soundModelJson);
}
template <typename Card>
void readCardAssets(const cydaw::base::Object &object, Card &card) {
  using Shape = cydaw::engine::WavetableShape;
  if (const auto *harmonics =
          object.getProperty(jsonKeys::wavetable).getArray();
      harmonics != nullptr &&
      harmonics->size() ==
          static_cast<int>(Shape::frameCount * Shape::harmonicCount)) {
    Shape shape;
    auto index = 0;
    for (auto &frame : shape.harmonics)
      for (auto &amplitude : frame)
        amplitude =
            static_cast<float>(static_cast<double>((*harmonics)[index++]));
    card.wavetableShape = std::make_shared<const Shape>(shape);
    card.wavetableBank = cydaw::engine::buildWavetableBank(shape);
  }
  if (const auto model = object.getProperty(jsonKeys::soundModel);
      model.isObject()) {
    cydaw::base::Text error;
    if (auto parsed = cydaw::app::soundModelFromVar(model, error)) {
      card.soundModel = std::move(parsed);
      card.soundModelJson = model;
    } else if (cydaw::app::isMissingFactoryModel(model)) {
      // A factory model not on this computer: still named, silent.
      card.soundModelJson = model;
    }
  }
  if (const auto map =
          cydaw::app::upgradedSamplerMap(object.getProperty(jsonKeys::sampler));
      map.isObject()) {
    cydaw::base::Text error;
    if (auto parsed = cydaw::app::samplerMapFromVar(map, error)) {
      card.samplerMap = std::move(parsed);
      card.samplerJson = map;
    }
  }
  if (const auto tables = object.getProperty("prismTables"); tables.isObject())
    if (auto built = cydaw::app::prismTablesFromVar(tables)) {
      card.prismTables = std::move(built);
      card.prismTablesJson = tables;
    }
  if (const auto response = object.getProperty("impulse"); response.isObject())
    if (auto read = cydaw::app::impulseFromVar(response)) {
      card.impulse = std::move(read);
      card.impulseJson = response;
    }
  card.bypassed = static_cast<bool>(object.getProperty(jsonKeys::bypassed));
  if (const auto instrument = object.getProperty("instrument");
      instrument.isObject())
    card.instrumentJson = instrument;
  if (const auto *names = object.getProperty("branchNames").getArray())
    for (const auto &name : *names)
      card.branchNames.add(name.toString().substring(0, 64));
}
} // namespace
using detail::primitiveProcessorKeyForType;
namespace {
constexpr float nodeWidth = 224.0f;
constexpr float nodeHeight = 164.0f;
constexpr float portLabelGutter = 76.0f;
[[nodiscard]] constexpr float
contentGutterWidth(const std::ptrdiff_t inputCount,
                   const std::ptrdiff_t outputCount,
                   const std::size_t portCount) noexcept {
  if (portCount <= 2)
    return 0.0f;
  const auto left = inputCount > 1 ? portLabelGutter : 0.0f;
  const auto right = outputCount > 1 || (inputCount > 1 && outputCount > 0)
                         ? portLabelGutter
                         : 0.0f;
  return left + right;
}
constexpr int graphToolbarHeight = 35;
constexpr int canvasStatusHeight = 22;
constexpr float interactiveControlZoom = 0.66f;
constexpr float minimumGraphCoordinate = -16.0f;
constexpr float maximumGraphCoordinate = 17.0f;
float graphCoordinate(const double value, const bool legacyClamped) noexcept {
  if (!std::isfinite(value))
    return 0.5f;
  return base::limit(legacyClamped ? 0.0f : minimumGraphCoordinate,
                     legacyClamped ? 1.0f : maximumGraphCoordinate,
                     static_cast<float>(value));
}
struct PortBand {
  float top{};
  float height{};
};
[[nodiscard]] PortBand portBand(const base::Rectangle<float> bounds,
                                const float zoom) noexcept {
  // The same clamped reserve nodeContentBounds uses for its own title inset,
  // so a socket and the card's content start on one line. A flat 25 px left
  // the sockets crowded on a framed-out card; a plain 25 x zoom moved the
  // first one up into the title bar, which does not scale.
  const auto top = base::limit(20.0f, 29.0f, 27.0f * zoom);
  const auto bottom = base::limit(8.0f, 12.0f, 11.0f * zoom);
  return {bounds.getY() + top,
          std::max(20.0f, bounds.getHeight() - top - bottom)};
}
[[nodiscard]] int
interfaceControlTitleHeight(const float controlHeight) noexcept {
  const auto fontHeight = base::limit(7.8f, 10.5f, controlHeight * 0.105f);
  return base::limit(base::roundToInt(fontHeight) + 5, 20,
                     base::roundToInt(controlHeight * 0.22f));
}
base::Point<float> graphPosition(const base::Point<float> value) noexcept {
  return {graphCoordinate(value.x, false), graphCoordinate(value.y, false)};
}
cydaw::base::Value
serialisePluginExportPlan(const cydaw::graph::PluginExportPlan &plan,
                          const cydaw::base::Value &editorState) {
  auto root = cydaw::base::Object::Ptr(new cydaw::base::Object());
  root->setProperty(jsonKeys::schema, 1);
  root->setProperty(jsonKeys::format, "CyDAW VST3 Export Manifest");
  root->setProperty(jsonKeys::target, "VST3");

  auto identity = cydaw::base::Object::Ptr(new cydaw::base::Object());
  identity->setProperty(jsonKeys::productName,
                        cydaw::base::Text(plan.identity.productName));
  identity->setProperty(jsonKeys::companyName,
                        cydaw::base::Text(plan.identity.companyName));
  identity->setProperty(jsonKeys::manufacturerCode,
                        cydaw::base::Text(plan.identity.manufacturerCode));
  identity->setProperty(jsonKeys::pluginCode,
                        cydaw::base::Text(plan.identity.pluginCode));
  identity->setProperty(jsonKeys::version,
                        cydaw::base::Text(plan.identity.version.toString()));
  root->setProperty(jsonKeys::identity, cydaw::base::Value(identity.get()));

  auto contract = cydaw::base::Object::Ptr(new cydaw::base::Object());
  contract->setProperty(jsonKeys::graphId, cydaw::base::Text(plan.graph.id));
  contract->setProperty(jsonKeys::graphVersion,
                        cydaw::base::Text(plan.graph.version.toString()));
  contract->setProperty(jsonKeys::role, cydaw::graph::toString(plan.role));
  contract->setProperty(jsonKeys::category,
                        cydaw::base::Text(plan.vst3Category));
  contract->setProperty(jsonKeys::isSynth, plan.isSynth);
  contract->setProperty(jsonKeys::acceptsMidi, plan.acceptsMidi);
  contract->setProperty(jsonKeys::producesMidi, plan.producesMidi);
  contract->setProperty(jsonKeys::hasEditor, plan.hasEditor);
  contract->setProperty("sourceFingerprint",
                        cydaw::base::Text::toHexString(
                            static_cast<std::int64_t>(plan.sourceFingerprint))
                            .toUpperCase());

  cydaw::base::Array<cydaw::base::Value> buses;
  for (const auto &bus : plan.buses) {
    auto value = cydaw::base::Object::Ptr(new cydaw::base::Object());
    value->setProperty(jsonKeys::graphPortId,
                       static_cast<std::int64_t>(bus.graphPortId));
    value->setProperty(jsonKeys::key, cydaw::base::Text(bus.key));
    value->setProperty(jsonKeys::name, cydaw::base::Text(bus.displayName));
    value->setProperty("media",
                       bus.mediaType == cydaw::graph::PluginBusMediaType::audio
                           ? "audio"
                           : "midi");
    value->setProperty("direction",
                       bus.direction == cydaw::graph::PortDirection::input
                           ? "input"
                           : "output");
    value->setProperty(jsonKeys::audioType,
                       cydaw::graph::toString(bus.audioType));
    value->setProperty(jsonKeys::channels, static_cast<int>(bus.channels));
    value->setProperty(jsonKeys::main, bus.main);
    buses.add(cydaw::base::Value(value.get()));
  }
  contract->setProperty(jsonKeys::buses, buses);

  cydaw::base::Array<cydaw::base::Value> parameters;
  for (const auto &parameter : plan.parameters) {
    auto value = cydaw::base::Object::Ptr(new cydaw::base::Object());
    value->setProperty(jsonKeys::pluginParameterId,
                       static_cast<std::int64_t>(parameter.pluginParameterId));
    value->setProperty(jsonKeys::graphParameterId,
                       static_cast<std::int64_t>(parameter.graphParameterId));
    value->setProperty(jsonKeys::key, cydaw::base::Text(parameter.key));
    value->setProperty(jsonKeys::name,
                       cydaw::base::Text(parameter.displayName));
    value->setProperty(jsonKeys::unit, cydaw::base::Text(parameter.unit));
    value->setProperty(jsonKeys::minimum, parameter.minimum);
    value->setProperty(jsonKeys::maximum, parameter.maximum);
    value->setProperty(jsonKeys::default_, parameter.defaultValue);
    value->setProperty(jsonKeys::automatable, parameter.automatable);
    parameters.add(cydaw::base::Value(value.get()));
  }
  contract->setProperty(jsonKeys::parameters, parameters);
  root->setProperty(jsonKeys::contract, cydaw::base::Value(contract.get()));
  root->setProperty(jsonKeys::graphEditorState, editorState);
  return cydaw::base::Value(root.get());
}
cydaw::base::Text makeSearchable(const cydaw::base::Text &value) {
  return value.toLowerCase().removeCharacters("-_./");
}
struct PortValueDomain {
  double minimum{-1.0};
  double maximum{1.0};
  double interval{0.001};
  double skewMidpoint{};
  cydaw::base::Text suffix;
};
PortValueDomain portValueDomain(const cydaw::base::Text &nodeKey,
                                const cydaw::base::Text &portKey,
                                const cydaw::graph::PortType type,
                                const double current, const double reset) {
  if (type == cydaw::graph::PortType::trigger)
    return {0.0, 1.0, 1.0, 0.0, {}};

  const auto semantic = (nodeKey + " " + portKey).toLowerCase();
  PortValueDomain domain;
  if (semantic.contains("frequency") || semantic.contains("cutoff") ||
      semantic.contains(" hertz") || semantic.endsWith("hz")) {
    domain = {0.0, 24'000.0, 0.01, 1'000.0, " Hz"};
  } else if (semantic.contains("rate")) {
    domain = {0.0, 100.0, 0.001, 1.0, " Hz"};
  } else if (semantic.contains("millisecond") || semantic.contains("attack") ||
             semantic.contains("release") || semantic.contains("delay") ||
             semantic.contains("time")) {
    domain = {0.0, 10'000.0, 0.01, 100.0, " ms"};
  } else if (portKey.containsIgnoreCase("pan") ||
             portKey.containsIgnoreCase("bipolar") ||
             portKey.containsIgnoreCase("offset")) {
    domain = {-1.0, 1.0, 0.001, 0.0, {}};
  } else if (portKey.containsIgnoreCase("gain") ||
             semantic.contains("threshold") || semantic.contains(" db")) {
    domain = {-96.0, 36.0, 0.01, 0.0, " dB"};
  } else if (semantic.contains("transpose") || semantic.contains("semitone")) {
    domain = {-48.0, 48.0, 1.0, 0.0, " st"};
  } else if (semantic.contains("note") || semantic.contains("velocity")) {
    domain = {0.0, 127.0, 1.0, 0.0, {}};
  } else if (semantic.contains("mix") || semantic.contains("depth") ||
             semantic.contains("level") || semantic.contains("width") ||
             semantic.contains("room")) {
    domain = {0.0, 1.0, 0.001, 0.0, {}};
  } else {
    const auto magnitude = std::max({1.0, std::abs(current), std::abs(reset)});
    const auto extent = base::limit(
        1.0, 1'000'000.0, std::pow(10.0, std::ceil(std::log10(magnitude))));
    domain.minimum = -extent;
    domain.maximum = extent;
    domain.interval = extent <= 1.0 ? 0.001 : extent <= 100.0 ? 0.01 : 0.1;
  }
  domain.minimum = std::min(domain.minimum, std::min(current, reset));
  domain.maximum = std::max(domain.maximum, std::max(current, reset));
  return domain;
}
bool primitiveSupportsVariableAudioChannels(const cydaw::base::Text &typeKey) {
  const auto processorKey = primitiveProcessorKeyForType(typeKey);
  if (processorKey.isEmpty())
    return false;
  const auto *descriptor =
      cydaw::graph::factoryPrimitiveRegistry().find(processorKey.toStdString());
  return descriptor != nullptr && descriptor->supportsVariableAudioChannels;
}
cydaw::base::Text
primitiveParameterKeyForBinding(const cydaw::base::Text &suffix);
int samplerControllerOfBinding(const cydaw::base::Text &suffix) {
  if (!suffix.startsWith("sampler.cc"))
    return -1;
  const auto digits = suffix.substring(10);
  if (digits.isEmpty() || !digits.containsOnly("0123456789"))
    return -1;
  const auto number = digits.getIntValue();
  return number >= 0 && number < 128 ? number : -1;
}
cydaw::base::Text valuePortKeyForBinding(const cydaw::base::Text &suffix) {
  if (suffix == "gain.db")
    return "gain";
  if (suffix == "drive.db")
    return "drive";
  return primitiveParameterKeyForBinding(suffix);
}
cydaw::base::Text
primitiveParameterKeyForBinding(const cydaw::base::Text &suffix) {
  if (const auto *effect =
          cydaw::engine::effectParameterForSuffix(suffix.toStdString()))
    return cydaw::base::Text(std::string(effect->key));
  if (suffix.startsWith("batterie."))
    return suffix.substring(9);
  if (suffix.startsWith("prism."))
    return suffix.substring(6);
  if (suffix.startsWith("kick."))
    return suffix.substring(5);
  const auto familyPrefix = suffix.upToFirstOccurrenceOf(".", false, false);
  if (const auto model =
          cydaw::engine::synth::modelForPrefix(familyPrefix.toStdString());
      model != cydaw::engine::synth::Model::none) {
    const auto key = suffix.substring(familyPrefix.length() + 1);
    if (cydaw::engine::synth::parameterIndex(model, key.toStdString()) >= 0)
      return key;
  }
  // A sound model's and a sampler's settings, by their primitive keys
  // ("model.harmonics" is "harmonics"): without them a card's settings
  // were never published, so a host never saw them.
  if (suffix == "model.level" || suffix == "sampler.level")
    return "level";
  if (const auto index =
          cydaw::engine::soundModelSettingIndex(suffix.toStdString());
      index >= 0)
    return cydaw::base::Text(std::string(
        cydaw::engine::soundModelSettings[static_cast<std::size_t>(index)]
            .key));
  if (const auto index =
          cydaw::engine::samplerSettingIndex(suffix.toStdString());
      index >= 0)
    return cydaw::base::Text(std::string(
        cydaw::engine::samplerSettings[static_cast<std::size_t>(index)].key));
  if (suffix == "drive.db")
    return "drive-db";
  if (suffix == "gain.db")
    return "gain-db";
  if (suffix == "lowpass.cutoff" || suffix == "highpass.cutoff" ||
      suffix == "voiceFilter.cutoff")
    return "cutoff";
  if (suffix == "lowpass.resonance" || suffix == "voiceFilter.resonance")
    return "resonance";
  if (suffix == "clip.threshold")
    return "threshold";
  if (suffix == "stereo.width")
    return "width";
  if (suffix == "pan.value")
    return "pan";
  if (suffix == "oscillator.frequency")
    return "frequency";
  if (suffix == "polyOscillator.waveform")
    return "waveform";
  if (suffix == "polyOscillator.level" || suffix == "noise.level")
    return "level";
  if (suffix == "envelope.attack")
    return "attack";
  if (suffix == "envelope.decay")
    return "decay";
  if (suffix == "envelope.sustain")
    return "sustain";
  if (suffix == "envelope.release")
    return "release";
  if (suffix == "delay.time")
    return "time";
  if (suffix == "delay.feedback")
    return "feedback";
  if (suffix == "delay.mix" || suffix == "reverb.mix")
    return "mix";
  if (suffix == "reverb.roomSize")
    return "room-size";
  if (suffix == "compressor.threshold")
    return "threshold";
  if (suffix == "compressor.ratio")
    return "ratio";
  if (suffix == "eq.tone")
    return "tone";
  if (suffix == "eqBand.frequency" || suffix == "wavetable.frequency" ||
      suffix == "string.frequency")
    return "frequency";
  if (suffix == "wavetable.position")
    return "position";
  if (suffix == "wavetable.level")
    return "level";
  if (suffix == "wavetable.detune")
    return "detune";
  if (suffix == "string.damping")
    return "damping";
  if (suffix == "string.decay")
    return "decay";
  if (suffix == "eqBand.gain")
    return "gain";
  if (suffix == "eqBand.q")
    return "q";
  if (suffix == "lfo.rate")
    return "rate";
  if (suffix == "lfo.depth")
    return "depth";
  if (suffix == "lfo.offset")
    return "offset";
  if (suffix == "lfo.shape")
    return "shape";
  if (suffix == "macro.value" || suffix == "value.constant")
    return "value";
  if (suffix == "valueMath.amount")
    return "amount";
  if (suffix == "valueMath.offset")
    return "offset";
  if (suffix == "value.operand")
    return "operand";
  if (suffix == "value.minimum")
    return "minimum";
  if (suffix == "value.maximum")
    return "maximum";
  if (suffix == "map.inputMinimum")
    return "input-min";
  if (suffix == "map.inputMaximum")
    return "input-max";
  if (suffix == "map.outputMinimum")
    return "output-min";
  if (suffix == "map.outputMaximum")
    return "output-max";
  if (suffix == "follower.attack")
    return "attack";
  if (suffix == "follower.release")
    return "release";
  if (suffix == "midi.transpose")
    return "semitones";
  if (suffix == "midi.note.minimum")
    return "minimum-note";
  if (suffix == "midi.note.maximum")
    return "maximum-note";
  if (suffix == "midi.velocity")
    return "scale";
  if (suffix == "voice.mode")
    return "mode";
  if (suffix == "voice.glide")
    return "glide";
  return {};
}
cydaw::base::Text innermostParameterSuffix(cydaw::base::Text key) {
  while (key.startsWith("node.")) {
    const auto separator = key.indexOfChar(5, '.');
    if (separator < 0)
      break;
    key = key.substring(separator + 1);
  }
  return key;
}
std::pair<double, double>
primitiveParameterRange(const std::string &processorKey,
                        const std::string &parameterKey) {
  if (const auto *descriptor =
          cydaw::graph::factoryPrimitiveRegistry().find(processorKey))
    for (const auto &parameter : descriptor->parameters)
      if (parameter.key == parameterKey)
        return {parameter.minimum, parameter.maximum};
  return {0.0, 1.0};
}
std::optional<double> primitiveParameterValueForType(
    const cydaw::base::Text &typeKey, const cydaw::base::Text &parameterKey,
    const cydaw::engine::NativeDspNodeParameters &parameters) {
  // A few cards name a socket and its parameter differently — the gain
  // card's "gain" socket is its "gain-db" parameter, the drive card's
  // "drive" its "drive-db" — and asked with the socket's name this found
  // nothing, so those sockets never followed their knobs. The engine's
  // port mapping settles the name first.
  if (const auto mapped = cydaw::engine::nativeDspParameterKeyForPort(
          primitiveProcessorKeyForType(typeKey).toStdString(),
          parameterKey.toStdString());
      !mapped.empty() && mapped != parameterKey.toStdString())
    return primitiveParameterValueForType(
        typeKey, cydaw::base::Text(std::string(mapped)), parameters);
  if (const auto *effect =
          cydaw::engine::effectForType(typeKey.toStdString())) {
    if (const auto *parameter =
            cydaw::engine::effectParameter(*effect, parameterKey.toStdString()))
      return static_cast<double>(
          cydaw::engine::effectValue(parameters, *parameter));
    return std::nullopt;
  }
  if (typeKey == "sound-model")
    if (const auto index =
            cydaw::engine::soundModelSettingIndex(parameterKey.toStdString());
        index >= 0)
      return static_cast<double>(
          parameters.soundModel[static_cast<std::size_t>(index)]);
  if (typeKey == "sampler" || typeKey == "batterie")
    if (const auto index =
            cydaw::engine::samplerSettingIndex(parameterKey.toStdString());
        index >= 0)
      return static_cast<double>(
          parameters.sampler[static_cast<std::size_t>(index)]);
  if (typeKey == "batterie")
    if (const auto index =
            cydaw::engine::drum::parameterIndex(parameterKey.toStdString());
        index >= 0)
      return static_cast<double>(
          parameters.drum[static_cast<std::size_t>(index)]);
  if (typeKey == "instrument-selector")
    if (const auto index = cydaw::engine::instrument_container::settingIndex(
            parameterKey.toStdString());
        index >= 0)
      return static_cast<double>(
          parameters.selector[static_cast<std::size_t>(index)]);
  if (typeKey == "prism-synth")
    if (const auto index =
            cydaw::engine::prism::parameterIndex(parameterKey.toStdString());
        index >= 0)
      return static_cast<double>(
          parameters.prism[static_cast<std::size_t>(index)]);
  if (typeKey == "kick-synth")
    if (const auto index =
            cydaw::engine::kick::parameterIndex(parameterKey.toStdString());
        index >= 0)
      return static_cast<double>(
          parameters.kick[static_cast<std::size_t>(index)]);
  if (const auto model =
          cydaw::engine::synth::modelForType(typeKey.toStdString());
      model != cydaw::engine::synth::Model::none)
    if (const auto index = cydaw::engine::synth::parameterIndex(
            model, parameterKey.toStdString());
        index >= 0)
      return static_cast<double>(
          parameters.synth[static_cast<std::size_t>(index)]);
  if (typeKey == "voice-allocator" && parameterKey == "mode")
    return parameters.voiceMode;
  if (typeKey == "voice-allocator" && parameterKey == "glide")
    return parameters.glideMilliseconds;
  if (parameterKey == "drive-db")
    return parameters.driveDb;
  if (parameterKey == "gain-db")
    return parameters.gainDb;
  if (parameterKey == "cutoff")
    return typeKey == "highpass" ? parameters.highpassCutoffHz
                                 : parameters.cutoffHz;
  if (parameterKey == "resonance")
    return parameters.resonance;
  if (parameterKey == "threshold")
    return typeKey == "hard-clip" ? parameters.clipThreshold
                                  : parameters.thresholdDb;
  if (parameterKey == "ratio")
    return parameters.ratio;
  if (parameterKey == "width")
    return parameters.stereoWidth;
  if (parameterKey == "pan")
    return parameters.pan;
  if (parameterKey == "frequency")
    return typeKey == "eq-band" ? parameters.bandFrequencyHz
                                : parameters.oscillatorFrequencyHz;
  // Every one of these is guarded by its type. "decay" belongs to the
  // envelope as well as to the string, and an unguarded test here answered
  // first — which handed every existing instrument the string's four-second
  // default read as four milliseconds.
  if (parameterKey == "position" && typeKey == "wavetable")
    return parameters.wavetablePosition;
  if (parameterKey == "damping" && typeKey == "string")
    return parameters.stringDamping;
  if (parameterKey == "decay" && typeKey == "string")
    return parameters.stringDecaySeconds;
  if (parameterKey == "gain" && typeKey == "eq-band")
    return parameters.bandGainDb;
  if (parameterKey == "q" && typeKey == "eq-band")
    return parameters.bandQ;
  if (parameterKey == "waveform")
    return parameters.oscillatorWaveform;
  if (parameterKey == "detune")
    return typeKey == "wavetable" ? parameters.wavetableDetuneCents : 0.0;
  if (parameterKey == "level")
    return typeKey == "noise" ? parameters.noiseLevel
                              : parameters.oscillatorLevel;

  if (parameterKey == "attack" && typeKey == "envelope")
    return parameters.attackMilliseconds;
  if (parameterKey == "decay" && typeKey == "envelope")
    return parameters.decayMilliseconds;
  if (parameterKey == "sustain")
    return parameters.sustainLevel;
  if (parameterKey == "release" && typeKey == "envelope")
    return parameters.releaseMilliseconds;
  if (parameterKey == "time")
    return parameters.delayMilliseconds;
  if (parameterKey == "feedback")
    return parameters.feedback;
  if (parameterKey == "mix")
    return parameters.mix;
  if (parameterKey == "room-size")
    return parameters.roomSize;
  if (parameterKey == "tone")
    return parameters.tone;
  if (parameterKey == "rate")
    return parameters.modulationRateHz;
  if (parameterKey == "shape")
    return parameters.modulationShape;
  if (parameterKey == "depth" || parameterKey == "amount")
    return parameters.modulationDepth;
  if (parameterKey == "offset")
    return parameters.modulationOffset;
  if (parameterKey == "value")
    return typeKey == "macro" ? parameters.modulationOffset
                              : parameters.valueOperand;
  if (parameterKey == "operand")
    return parameters.valueOperand;
  if (parameterKey == "minimum")
    return parameters.valueMinimum;
  if (parameterKey == "maximum")
    return parameters.valueMaximum;
  if (parameterKey == "input-min")
    return parameters.mapInputMinimum;
  if (parameterKey == "input-max")
    return parameters.mapInputMaximum;
  if (parameterKey == "output-min")
    return parameters.mapOutputMinimum;
  if (parameterKey == "output-max")
    return parameters.mapOutputMaximum;
  if (parameterKey == "attack" && typeKey == "envelope-follower")
    return parameters.followerAttackSeconds;
  if (parameterKey == "release" && typeKey == "envelope-follower")
    return parameters.followerReleaseSeconds;
  if (parameterKey == "semitones")
    return parameters.midiTranspose;
  if (parameterKey == "minimum-note")
    return parameters.midiMinimumNote;
  if (parameterKey == "maximum-note")
    return parameters.midiMaximumNote;
  if (parameterKey == "scale")
    return parameters.midiVelocityScale;
  if (parameterKey == "voices")
    return 16.0;
  return std::nullopt;
}
cydaw::base::Value serialiseNativeParameters(
    const cydaw::engine::NativeDspNodeParameters &parameters) {
  auto value = cydaw::base::Object::Ptr(new cydaw::base::Object());
  value->setProperty(jsonKeys::driveDb, parameters.driveDb);
  value->setProperty(jsonKeys::gainDb, parameters.gainDb);
  value->setProperty(jsonKeys::cutoffHz, parameters.cutoffHz);
  value->setProperty(jsonKeys::resonance, parameters.resonance);
  value->setProperty(jsonKeys::highpassCutoffHz, parameters.highpassCutoffHz);
  value->setProperty(jsonKeys::clipThreshold, parameters.clipThreshold);
  value->setProperty(jsonKeys::stereoWidth, parameters.stereoWidth);
  value->setProperty(jsonKeys::pan, parameters.pan);
  value->setProperty(jsonKeys::oscillatorFrequencyHz,
                     parameters.oscillatorFrequencyHz);
  value->setProperty(jsonKeys::noiseLevel, parameters.noiseLevel);
  value->setProperty(jsonKeys::delayMilliseconds, parameters.delayMilliseconds);
  value->setProperty(jsonKeys::feedback, parameters.feedback);
  value->setProperty(jsonKeys::mix, parameters.mix);
  value->setProperty(jsonKeys::roomSize, parameters.roomSize);
  value->setProperty(jsonKeys::thresholdDb, parameters.thresholdDb);
  value->setProperty(jsonKeys::ratio, parameters.ratio);
  value->setProperty(jsonKeys::tone, parameters.tone);
  value->setProperty(jsonKeys::bandFrequencyHz, parameters.bandFrequencyHz);
  value->setProperty(jsonKeys::bandGainDb, parameters.bandGainDb);
  value->setProperty(jsonKeys::bandQ, parameters.bandQ);
  value->setProperty(jsonKeys::wavetablePosition, parameters.wavetablePosition);
  value->setProperty(jsonKeys::wavetableDetuneCents,
                     parameters.wavetableDetuneCents);
  value->setProperty(jsonKeys::stringDamping, parameters.stringDamping);
  value->setProperty(jsonKeys::stringDecaySeconds,
                     parameters.stringDecaySeconds);
  value->setProperty(jsonKeys::oscillatorWaveform,
                     parameters.oscillatorWaveform);
  value->setProperty(jsonKeys::oscillatorLevel, parameters.oscillatorLevel);
  value->setProperty(jsonKeys::attackMilliseconds,
                     parameters.attackMilliseconds);
  value->setProperty(jsonKeys::decayMilliseconds, parameters.decayMilliseconds);
  value->setProperty(jsonKeys::sustainLevel, parameters.sustainLevel);
  value->setProperty(jsonKeys::releaseMilliseconds,
                     parameters.releaseMilliseconds);
  value->setProperty(jsonKeys::modulationRateHz, parameters.modulationRateHz);
  value->setProperty(jsonKeys::modulationDepth, parameters.modulationDepth);
  value->setProperty(jsonKeys::modulationOffset, parameters.modulationOffset);
  value->setProperty(jsonKeys::modulationShape, parameters.modulationShape);
  value->setProperty(jsonKeys::valueOperand, parameters.valueOperand);
  value->setProperty(jsonKeys::valueInputA, parameters.valueInputA);
  value->setProperty(jsonKeys::valueInputB, parameters.valueInputB);
  value->setProperty(jsonKeys::valueMinimum, parameters.valueMinimum);
  value->setProperty(jsonKeys::valueMaximum, parameters.valueMaximum);
  value->setProperty(jsonKeys::mapInputMinimum, parameters.mapInputMinimum);
  value->setProperty(jsonKeys::mapInputMaximum, parameters.mapInputMaximum);
  value->setProperty(jsonKeys::mapOutputMinimum, parameters.mapOutputMinimum);
  value->setProperty(jsonKeys::mapOutputMaximum, parameters.mapOutputMaximum);
  value->setProperty(jsonKeys::followerAttackSeconds,
                     parameters.followerAttackSeconds);
  value->setProperty(jsonKeys::followerReleaseSeconds,
                     parameters.followerReleaseSeconds);
  value->setProperty(jsonKeys::midiTranspose, parameters.midiTranspose);
  value->setProperty(jsonKeys::midiMinimumNote, parameters.midiMinimumNote);
  value->setProperty(jsonKeys::midiMaximumNote, parameters.midiMaximumNote);
  value->setProperty(jsonKeys::midiVelocityScale, parameters.midiVelocityScale);
  value->setProperty(jsonKeys::voiceMode, parameters.voiceMode);
  value->setProperty(jsonKeys::glideMilliseconds, parameters.glideMilliseconds);
  // The effect catalogue's fields; each effect clamps its own.
  value->setProperty("effect1", parameters.effect1);
  value->setProperty("effect2", parameters.effect2);
  value->setProperty("effect3", parameters.effect3);
  value->setProperty("effect4", parameters.effect4);
  value->setProperty("effect5", parameters.effect5);
  value->setProperty("effect6", parameters.effect6);
  value->setProperty("effect7", parameters.effect7);
  value->setProperty("effect8", parameters.effect8);
  writeEqBands(*value, parameters);
  if (parameters.soundModel != cydaw::engine::defaultSoundModelValues()) {
    cydaw::base::Array<cydaw::base::Value> values;
    for (const auto item : parameters.soundModel)
      values.add(item);
    value->setProperty(jsonKeys::soundModelValues, values);
  }
  if (parameters.sampler != cydaw::engine::defaultSamplerValues()) {
    cydaw::base::Array<cydaw::base::Value> values;
    for (const auto item : parameters.sampler)
      values.add(item);
    value->setProperty(jsonKeys::samplerValues, values);
  }
  // A sampler card's controllers, by their numbers: those it sets (any
  // value below zero leaves one where the map starts it).
  {
    auto controllers = cydaw::base::Object::Ptr(new cydaw::base::Object());
    for (std::size_t number = 0; number < parameters.samplerControllers.size();
         ++number)
      if (parameters.samplerControllers[number] != -1.0f)
        controllers->setProperty(cydaw::base::Identifier(cydaw::base::Text(
                                     static_cast<int>(number))),
                                 parameters.samplerControllers[number]);
    if (!controllers->getProperties().isEmpty())
      value->setProperty("samplerControllers",
                         cydaw::base::Value(controllers.get()));
  }
  // Prisme's settings, by their keys (a key a later version renames or
  // drops is only skipped).
  if (parameters.prism != cydaw::engine::prism::defaultValues()) {
    auto values = cydaw::base::Object::Ptr(new cydaw::base::Object());
    const auto defaults = cydaw::engine::prism::defaultValues();
    for (int index = 0; index < cydaw::engine::prism::parameterCount; ++index)
      if (parameters.prism[static_cast<std::size_t>(index)] !=
          defaults[static_cast<std::size_t>(index)])
        values->setProperty(
            cydaw::base::Identifier(cydaw::engine::prism::parameter(index).key),
            parameters.prism[static_cast<std::size_t>(index)]);
    value->setProperty(jsonKeys::prismValues, cydaw::base::Value(values.get()));
  }
  // Kick's the same way.
  if (parameters.kick != cydaw::engine::kick::defaultValues()) {
    auto values = cydaw::base::Object::Ptr(new cydaw::base::Object());
    const auto defaults = cydaw::engine::kick::defaultValues();
    for (int index = 0; index < cydaw::engine::kick::parameterCount; ++index)
      if (parameters.kick[static_cast<std::size_t>(index)] !=
          defaults[static_cast<std::size_t>(index)])
        values->setProperty(
            cydaw::base::Identifier(cydaw::engine::kick::parameter(index).key),
            parameters.kick[static_cast<std::size_t>(index)]);
    value->setProperty(jsonKeys::kickValues, cydaw::base::Value(values.get()));
  }
  // A Batterie's mix: by its keys, what differs from at rest.
  {
    const auto neutral = cydaw::engine::drum::neutralValues();
    auto drums = cydaw::base::Object::Ptr(new cydaw::base::Object());
    for (int index = 0; index < cydaw::engine::drum::valueCount; ++index)
      if (parameters.drum[static_cast<std::size_t>(index)] !=
          neutral[static_cast<std::size_t>(index)])
        drums->setProperty(
            cydaw::base::Identifier(cydaw::engine::drum::parameter(index).key),
            parameters.drum[static_cast<std::size_t>(index)]);
    if (!drums->getProperties().isEmpty())
      value->setProperty("drumValues", cydaw::base::Value(drums.get()));
  }
  // An Instrument Selector's settings: by their keys, what differs from at
  // rest.
  {
    namespace container = cydaw::engine::instrument_container;
    const auto rest = container::defaultValues();
    auto selector = cydaw::base::Object::Ptr(new cydaw::base::Object());
    for (int index = 0; index < container::valueCount; ++index)
      if (parameters.selector[static_cast<std::size_t>(index)] !=
          rest[static_cast<std::size_t>(index)])
        selector->setProperty(
            cydaw::base::Identifier(cydaw::base::Text(std::string(
                container::settings[static_cast<std::size_t>(index)].key))),
            parameters.selector[static_cast<std::size_t>(index)]);
    if (!selector->getProperties().isEmpty())
      value->setProperty("selectorValues", cydaw::base::Value(selector.get()));
  }
  // A synth of the family's: its model, then its settings by their keys
  // (without a model, as they lie).
  if (const auto model = cydaw::engine::synth::modelOf(parameters.synthModel);
      model != cydaw::engine::synth::Model::none) {
    value->setProperty("synthModel", static_cast<int>(model));
    auto values = cydaw::base::Object::Ptr(new cydaw::base::Object());
    const auto defaults = cydaw::engine::synth::defaultValues(model);
    for (int index = 0; index < cydaw::engine::synth::parameterCount(model);
         ++index)
      if (parameters.synth[static_cast<std::size_t>(index)] !=
          defaults[static_cast<std::size_t>(index)])
        values->setProperty(
            cydaw::base::Identifier(
                cydaw::engine::synth::parameter(model, index).key),
            parameters.synth[static_cast<std::size_t>(index)]);
    value->setProperty("synthValues", cydaw::base::Value(values.get()));
  } else {
    // Not a model: the number and the values as they lie.
    if (parameters.synthModel != 0.0f)
      value->setProperty("synthModel", parameters.synthModel);
    if (std::any_of(parameters.synth.begin(), parameters.synth.end(),
                    [](const float item) { return item != 0.0f; })) {
      cydaw::base::Array<cydaw::base::Value> values;
      for (const auto item : parameters.synth)
        values.add(item);
      value->setProperty("synthValues", values);
    }
  }
  return cydaw::base::Value(value.get());
}
cydaw::engine::NativeDspNodeParameters restoreNativeParameters(
    const cydaw::base::Value &value,
    cydaw::engine::NativeDspNodeParameters parameters = {}) {
  const auto *object = value.getDynamicObject();
  if (object == nullptr)
    return parameters;
  const auto read = [object](const cydaw::base::Identifier &key,
                             const float fallback) {
    return object->hasProperty(key) ? static_cast<float>(static_cast<double>(
                                          object->getProperty(key)))
                                    : fallback;
  };
  parameters.driveDb =
      base::limit(0.0f, 36.0f, read("driveDb", parameters.driveDb));
  parameters.gainDb =
      base::limit(-120.0f, 24.0f, read("gainDb", parameters.gainDb));
  parameters.cutoffHz =
      base::limit(20.0f, 20'000.0f, read("cutoffHz", parameters.cutoffHz));
  parameters.resonance =
      base::limit(0.1f, 20.0f, read("resonance", parameters.resonance));
  parameters.highpassCutoffHz = base::limit(
      10.0f, 20'000.0f, read("highpassCutoffHz", parameters.highpassCutoffHz));
  parameters.clipThreshold = base::limit(
      0.001f, 1.0f, read("clipThreshold", parameters.clipThreshold));
  parameters.stereoWidth =
      base::limit(0.0f, 2.0f, read("stereoWidth", parameters.stereoWidth));
  parameters.pan = base::limit(-1.0f, 1.0f, read("pan", parameters.pan));
  parameters.oscillatorFrequencyHz = base::limit(
      20.0f, 20'000.0f,
      read("oscillatorFrequencyHz", parameters.oscillatorFrequencyHz));
  parameters.noiseLevel =
      base::limit(0.0f, 1.0f, read("noiseLevel", parameters.noiseLevel));
  parameters.delayMilliseconds = base::limit(
      1.0f, 2'000.0f, read("delayMilliseconds", parameters.delayMilliseconds));
  parameters.feedback =
      base::limit(0.0f, 0.94f, read("feedback", parameters.feedback));
  parameters.mix = base::limit(0.0f, 1.0f, read("mix", parameters.mix));
  parameters.roomSize =
      base::limit(0.0f, 0.95f, read("roomSize", parameters.roomSize));
  parameters.thresholdDb =
      base::limit(-60.0f, 0.0f, read("thresholdDb", parameters.thresholdDb));
  parameters.ratio = base::limit(1.0f, 20.0f, read("ratio", parameters.ratio));
  parameters.tone = base::limit(-1.0f, 1.0f, read("tone", parameters.tone));
  parameters.bandFrequencyHz = base::limit(
      20.0f, 20'000.0f, read("bandFrequencyHz", parameters.bandFrequencyHz));
  parameters.bandGainDb =
      base::limit(-30.0f, 30.0f, read("bandGainDb", parameters.bandGainDb));
  parameters.bandQ = base::limit(0.1f, 18.0f, read("bandQ", parameters.bandQ));
  parameters.wavetablePosition = base::limit(
      0.0f, 1.0f, read("wavetablePosition", parameters.wavetablePosition));
  parameters.wavetableDetuneCents = base::limit(
      0.0f, 50.0f,
      read("wavetableDetuneCents", parameters.wavetableDetuneCents));
  parameters.stringDamping =
      base::limit(0.0f, 1.0f, read("stringDamping", parameters.stringDamping));
  parameters.stringDecaySeconds = base::limit(
      0.05f, 20.0f, read("stringDecaySeconds", parameters.stringDecaySeconds));
  parameters.oscillatorWaveform = base::limit(
      0.0f, 3.0f, read("oscillatorWaveform", parameters.oscillatorWaveform));
  // Up to 1: a wavetable's and a sound model's level go that far (a poly
  // oscillator's is held to 0.5 where it is used). Clamped at 0.5 here, a
  // saved sound model came back 6 dB down.
  parameters.oscillatorLevel = base::limit(
      0.0f, 1.0f, read("oscillatorLevel", parameters.oscillatorLevel));
  parameters.attackMilliseconds =
      base::limit(0.1f, 5'000.0f,
                  read("attackMilliseconds", parameters.attackMilliseconds));
  parameters.decayMilliseconds = base::limit(
      0.1f, 5'000.0f, read("decayMilliseconds", parameters.decayMilliseconds));
  parameters.sustainLevel =
      base::limit(0.0f, 1.0f, read("sustainLevel", parameters.sustainLevel));
  parameters.releaseMilliseconds =
      base::limit(0.1f, 10'000.0f,
                  read("releaseMilliseconds", parameters.releaseMilliseconds));
  parameters.modulationRateHz = base::limit(
      0.01f, 40.0f, read("modulationRateHz", parameters.modulationRateHz));
  parameters.modulationDepth = base::limit(
      0.0f, 1.0f, read("modulationDepth", parameters.modulationDepth));
  parameters.modulationOffset = base::limit(
      -1.0f, 1.0f, read("modulationOffset", parameters.modulationOffset));
  parameters.modulationShape = base::limit(
      0.0f, 3.0f, read("modulationShape", parameters.modulationShape));
  parameters.valueOperand =
      base::limit(-1'000'000.0f, 1'000'000.0f,
                  read("valueOperand", parameters.valueOperand));
  parameters.valueInputA = base::limit(
      -1'000'000.0f, 1'000'000.0f, read("valueInputA", parameters.valueInputA));
  parameters.valueInputB = base::limit(
      -1'000'000.0f, 1'000'000.0f,
      read("valueInputB", object->hasProperty(jsonKeys::valueOperand)
                              ? parameters.valueOperand
                              : parameters.valueInputB));
  parameters.valueMinimum =
      base::limit(-1'000'000.0f, 1'000'000.0f,
                  read("valueMinimum", parameters.valueMinimum));
  parameters.valueMaximum =
      base::limit(-1'000'000.0f, 1'000'000.0f,
                  read("valueMaximum", parameters.valueMaximum));
  parameters.mapInputMinimum =
      base::limit(-1'000'000.0f, 1'000'000.0f,
                  read("mapInputMinimum", parameters.mapInputMinimum));
  parameters.mapInputMaximum =
      base::limit(-1'000'000.0f, 1'000'000.0f,
                  read("mapInputMaximum", parameters.mapInputMaximum));
  parameters.mapOutputMinimum =
      base::limit(-1'000'000.0f, 1'000'000.0f,
                  read("mapOutputMinimum", parameters.mapOutputMinimum));
  parameters.mapOutputMaximum =
      base::limit(-1'000'000.0f, 1'000'000.0f,
                  read("mapOutputMaximum", parameters.mapOutputMaximum));
  parameters.followerAttackSeconds = base::limit(
      0.0001f, 1.0f,
      read("followerAttackSeconds", parameters.followerAttackSeconds));
  parameters.followerReleaseSeconds = base::limit(
      0.001f, 5.0f,
      read("followerReleaseSeconds", parameters.followerReleaseSeconds));
  parameters.midiTranspose = base::limit(
      -48.0f, 48.0f, read("midiTranspose", parameters.midiTranspose));
  parameters.midiMinimumNote = base::limit(
      0.0f, 127.0f, read("midiMinimumNote", parameters.midiMinimumNote));
  parameters.midiMaximumNote = base::limit(
      0.0f, 127.0f, read("midiMaximumNote", parameters.midiMaximumNote));
  parameters.midiVelocityScale = base::limit(
      0.0f, 2.0f, read("midiVelocityScale", parameters.midiVelocityScale));
  parameters.voiceMode =
      base::limit(0.0f, 2.0f, read("voiceMode", parameters.voiceMode));
  parameters.glideMilliseconds = base::limit(
      0.0f, 2'000.0f, read("glideMilliseconds", parameters.glideMilliseconds));
  const auto finite = [&read](const char *key, const float fallback) {
    const auto value = read(key, fallback);
    return std::isfinite(value) ? value : fallback;
  };
  parameters.effect1 = finite("effect1", parameters.effect1);
  parameters.effect2 = finite("effect2", parameters.effect2);
  parameters.effect3 = finite("effect3", parameters.effect3);
  parameters.effect4 = finite("effect4", parameters.effect4);
  parameters.effect5 = finite("effect5", parameters.effect5);
  parameters.effect6 = finite("effect6", parameters.effect6);
  parameters.effect7 = finite("effect7", parameters.effect7);
  parameters.effect8 = finite("effect8", parameters.effect8);
  readEqBands(*object, parameters);
  if (const auto *values =
          object->getProperty(jsonKeys::soundModelValues).getArray())
    for (int index = 0; index < values->size() &&
                        index < static_cast<int>(parameters.soundModel.size());
         ++index) {
      auto value = static_cast<float>(static_cast<double>((*values)[index]));
      if (!std::isfinite(value))
        continue;
      if (index < static_cast<int>(cydaw::engine::soundModelSettingCount)) {
        const auto &setting =
            cydaw::engine::soundModelSettings[static_cast<std::size_t>(index)];
        value = base::limit(setting.minimum, setting.maximum, value);
      }
      parameters.soundModel[static_cast<std::size_t>(index)] = value;
    }
  if (const auto *values =
          object->getProperty(jsonKeys::samplerValues).getArray())
    for (int index = 0; index < values->size() &&
                        index < static_cast<int>(parameters.sampler.size());
         ++index) {
      auto value = static_cast<float>(static_cast<double>((*values)[index]));
      if (!std::isfinite(value))
        continue;
      if (index < static_cast<int>(cydaw::engine::samplerSettingCount)) {
        const auto &setting =
            cydaw::engine::samplerSettings[static_cast<std::size_t>(index)];
        value = base::limit(setting.minimum, setting.maximum, value);
      }
      parameters.sampler[static_cast<std::size_t>(index)] = value;
    }
  if (const auto *controllers =
          object->getProperty("samplerControllers").getDynamicObject()) {
    parameters.samplerControllers = cydaw::engine::unsetSamplerControllers();
    for (const auto &property : controllers->getProperties()) {
      const auto number = property.name.toString().getIntValue();
      const auto stored =
          static_cast<float>(static_cast<double>(property.value));
      if (number >= 0 && number < 128 && std::isfinite(stored))
        parameters.samplerControllers[static_cast<std::size_t>(number)] =
            std::clamp(stored, -1.0f, 127.0f);
    }
  }
  if (const auto *values =
          object->getProperty(jsonKeys::prismValues).getDynamicObject())
    for (const auto &property : values->getProperties()) {
      const auto index = cydaw::engine::prism::parameterIndex(
          property.name.toString().toStdString());
      const auto value =
          static_cast<float>(static_cast<double>(property.value));
      // As written: the plan compiled from it keeps each in its range.
      if (index >= 0 && std::isfinite(value))
        parameters.prism[static_cast<std::size_t>(index)] = value;
    }
  if (const auto *values =
          object->getProperty(jsonKeys::kickValues).getDynamicObject())
    for (const auto &property : values->getProperties()) {
      const auto index = cydaw::engine::kick::parameterIndex(
          property.name.toString().toStdString());
      const auto value =
          static_cast<float>(static_cast<double>(property.value));
      // As written, as Prisme's: the plan compiled from it keeps each in its
      // range.
      if (index >= 0 && std::isfinite(value))
        parameters.kick[static_cast<std::size_t>(index)] = value;
    }
  if (const auto *drums =
          object->getProperty("drumValues").getDynamicObject()) {
    parameters.drum = cydaw::engine::drum::neutralValues();
    for (const auto &property : drums->getProperties()) {
      const auto index = cydaw::engine::drum::parameterIndex(
          property.name.toString().toStdString());
      const auto stored =
          static_cast<float>(static_cast<double>(property.value));
      if (index >= 0 && std::isfinite(stored))
        parameters.drum[static_cast<std::size_t>(index)] = stored;
    }
  }
  if (const auto *selector =
          object->getProperty("selectorValues").getDynamicObject()) {
    namespace container = cydaw::engine::instrument_container;
    parameters.selector = container::defaultValues();
    for (const auto &property : selector->getProperties()) {
      const auto index =
          container::settingIndex(property.name.toString().toStdString());
      const auto stored =
          static_cast<float>(static_cast<double>(property.value));
      // As written: a plan compiled from it keeps each in its range.
      if (index >= 0 && std::isfinite(stored))
        parameters.selector[static_cast<std::size_t>(index)] = stored;
    }
  }
  if (object->hasProperty("synthModel"))
    parameters.synthModel = static_cast<float>(
        static_cast<double>(object->getProperty("synthModel")));
  if (const auto model = cydaw::engine::synth::modelOf(parameters.synthModel);
      model != cydaw::engine::synth::Model::none) {
    // Only what differs from the model's defaults is stored.
    if (const auto *values =
            object->getProperty("synthValues").getDynamicObject()) {
      parameters.synth = cydaw::engine::synth::defaultValues(model);
      for (const auto &property : values->getProperties()) {
        const auto index = cydaw::engine::synth::parameterIndex(
            model, property.name.toString().toStdString());
        const auto stored =
            static_cast<float>(static_cast<double>(property.value));
        if (index >= 0 && std::isfinite(stored))
          parameters.synth[static_cast<std::size_t>(index)] =
              cydaw::engine::synth::clampValue(model, index, stored);
      }
    }
  } else if (const auto *values =
                 object->getProperty("synthValues").getArray()) {
    for (int index = 0;
         index < values->size() && index < cydaw::engine::synth::valueCapacity;
         ++index) {
      const auto stored =
          static_cast<float>(static_cast<double>((*values)[index]));
      if (std::isfinite(stored))
        parameters.synth[static_cast<std::size_t>(index)] = stored;
    }
  }
  return parameters;
}
cydaw::base::Text nodeParameterBinding(const std::uint64_t nodeId,
                                       const cydaw::base::Text &suffix) {
  return "node." + cydaw::base::Text(static_cast<std::int64_t>(nodeId)) + "." +
         suffix;
}
cydaw::base::Text
graphInstanceClassParameterKey(const std::uint64_t instanceId,
                               const cydaw::base::Text &binding) {
  const auto prefix = nodeParameterBinding(instanceId, {});
  return binding.startsWith(prefix) ? binding.substring(prefix.length())
                                    : binding;
}
cydaw::base::Text parameterSuffix(const cydaw::base::Text &binding) {
  if (!binding.startsWith("node."))
    return binding;
  const auto separator = binding.indexOfChar(5, '.');
  return separator >= 0 ? binding.substring(separator + 1) : binding;
}
bool bindingMatchesType(const cydaw::base::Text &typeKey,
                        const cydaw::base::Text &binding) {
  const auto suffix = parameterSuffix(binding);
  return (typeKey == "drive" && suffix == "drive.db") ||
         (typeKey == "gain" && suffix == "gain.db") ||
         (typeKey == "lowpass" &&
          (suffix == "lowpass.cutoff" || suffix == "lowpass.resonance")) ||
         (typeKey == "eq-band" &&
          (suffix == "eqBand.frequency" || suffix == "eqBand.gain" ||
           suffix == "eqBand.q")) ||
         (typeKey == "wavetable" &&
          (suffix == "wavetable.frequency" || suffix == "wavetable.position" ||
           suffix == "wavetable.level" || suffix == "wavetable.detune")) ||
         (typeKey == "string" &&
          (suffix == "string.frequency" || suffix == "string.damping" ||
           suffix == "string.decay")) ||
         (typeKey == "sound-model" &&
          (suffix == "model.level" ||
           cydaw::engine::soundModelSettingIndex(suffix.toStdString()) >= 0)) ||
         ((typeKey == "sampler" || typeKey == "batterie") &&
          (suffix == "sampler.level" ||
           cydaw::engine::samplerSettingIndex(suffix.toStdString()) >= 0 ||
           samplerControllerOfBinding(suffix) >= 0)) ||
         (typeKey == "batterie" && suffix.startsWith("batterie.") &&
          cydaw::engine::drum::parameterIndex(
              suffix.substring(9).toStdString()) >= 0) ||
         (typeKey == "instrument-selector" && suffix.startsWith("selector.") &&
          cydaw::engine::instrument_container::settingIndex(
              suffix.substring(9).toStdString()) >= 0) ||
         (typeKey == "prism-synth" &&
          (suffix == "prism.level" ||
           (suffix.startsWith("prism.") &&
            cydaw::engine::prism::parameterIndex(
                suffix.substring(6).toStdString()) >= 0))) ||
         (typeKey == "kick-synth" &&
          (suffix == "kick.level" ||
           (suffix.startsWith("kick.") &&
            cydaw::engine::kick::parameterIndex(
                suffix.substring(5).toStdString()) >= 0))) ||
         [&typeKey, &suffix] {
           const auto model =
               cydaw::engine::synth::modelForType(typeKey.toStdString());
           if (model == cydaw::engine::synth::Model::none)
             return false;
           const auto prefix =
               cydaw::base::Text(cydaw::engine::synth::info(model).prefix) +
               ".";
           return suffix == prefix + "level" ||
                  (suffix.startsWith(prefix) &&
                   cydaw::engine::synth::parameterIndex(
                       model,
                       suffix.substring(prefix.length()).toStdString()) >= 0);
         }() ||
         (typeKey == "highpass" && suffix == "highpass.cutoff") ||
         (typeKey == "hard-clip" && suffix == "clip.threshold") ||
         (typeKey == "stereo-width" && suffix == "stereo.width") ||
         (typeKey == "gain-pan" && suffix == "pan.value") ||
         (typeKey == "oscillator" && suffix == "oscillator.frequency") ||
         (typeKey == "poly-oscillator" &&
          suffix.startsWith("polyOscillator.")) ||
         (typeKey == "envelope" && suffix.startsWith("envelope.")) ||
         (typeKey == "voice-filter" && (suffix == "voiceFilter.cutoff" ||
                                        suffix == "voiceFilter.resonance")) ||
         (typeKey == "noise" && suffix == "noise.level") ||
         (typeKey == "delay" && suffix.startsWith("delay.")) ||
         ((typeKey == "reverb" || typeKey == "reverb-rack") &&
          suffix.startsWith("reverb.")) ||
         (typeKey == "compressor" && suffix.startsWith("compressor.")) ||
         (typeKey == "eq" && suffix == "eq.tone") ||
         ((typeKey == "lfo" || typeKey == "lfo-core") &&
          suffix.startsWith("lfo.")) ||
         (typeKey == "macro" && suffix == "macro.value") ||
         (typeKey == "control-math" && suffix.startsWith("valueMath.")) ||
         (typeKey.startsWith("value-") && suffix.startsWith("value.")) ||
         (typeKey.startsWith("logic-") && suffix.startsWith("value.")) ||
         (typeKey == "map-range" && suffix.startsWith("map.")) ||
         (typeKey == "envelope-follower" && suffix.startsWith("follower.")) ||
         (typeKey == "midi-transpose" && suffix == "midi.transpose") ||
         (typeKey == "note-filter" && suffix.startsWith("midi.note.")) ||
         (typeKey == "velocity-map" && suffix == "midi.velocity") ||
         (typeKey == "voice-allocator" && suffix.startsWith("voice.")) || [&] {
           const cydaw::engine::EffectDescriptor *owner{};
           return cydaw::engine::effectParameterForSuffix(suffix.toStdString(),
                                                          &owner) != nullptr &&
                  owner->typeKey == typeKey.toStdString();
         }();
}
bool setNativeParameter(cydaw::engine::NativeDspNodeParameters &parameters,
                        const cydaw::base::Text &binding, const double value) {
  const auto suffix = parameterSuffix(binding);
  if (suffix == "prism.level") {
    parameters.oscillatorLevel =
        base::limit(0.0f, 1.0f, static_cast<float>(value));
    return true;
  }
  if (suffix.startsWith("prism."))
    if (const auto index = cydaw::engine::prism::parameterIndex(
            suffix.substring(6).toStdString());
        index >= 0) {
      parameters.prism[static_cast<std::size_t>(index)] =
          cydaw::engine::prism::clampValue(index, static_cast<float>(value));
      return true;
    }
  if (suffix == "kick.level") {
    parameters.oscillatorLevel =
        base::limit(0.0f, 1.0f, static_cast<float>(value));
    return true;
  }
  if (suffix.startsWith("kick."))
    if (const auto index = cydaw::engine::kick::parameterIndex(
            suffix.substring(5).toStdString());
        index >= 0) {
      parameters.kick[static_cast<std::size_t>(index)] =
          cydaw::engine::kick::clampValue(index, static_cast<float>(value));
      return true;
    }
  if (suffix.startsWith("batterie."))
    if (const auto index = cydaw::engine::drum::parameterIndex(
            suffix.substring(9).toStdString());
        index >= 0) {
      parameters.drum[static_cast<std::size_t>(index)] =
          cydaw::engine::drum::clampValue(index, static_cast<float>(value));
      return true;
    }
  if (suffix.startsWith("selector."))
    if (const auto index = cydaw::engine::instrument_container::settingIndex(
            suffix.substring(9).toStdString());
        index >= 0) {
      parameters.selector[static_cast<std::size_t>(index)] =
          cydaw::engine::instrument_container::clampValue(
              index, static_cast<float>(value));
      return true;
    }
  // A synth of the family: "<prefix>.level" or "<prefix>.<key>".
  if (const auto model = cydaw::engine::synth::modelForPrefix(
          suffix.upToFirstOccurrenceOf(".", false, false).toStdString());
      model != cydaw::engine::synth::Model::none) {
    const auto key = suffix.fromFirstOccurrenceOf(".", false, false);
    if (key == "level") {
      parameters.oscillatorLevel =
          base::limit(0.0f, 1.0f, static_cast<float>(value));
      return true;
    }
    if (const auto index =
            cydaw::engine::synth::parameterIndex(model, key.toStdString());
        index >= 0) {
      parameters.synthModel = static_cast<float>(model);
      parameters.synth[static_cast<std::size_t>(index)] =
          cydaw::engine::synth::clampValue(model, index,
                                           static_cast<float>(value));
      return true;
    }
  }
  if (suffix == "model.level") {
    parameters.oscillatorLevel =
        base::limit(0.0f, 1.0f, static_cast<float>(value));
    return true;
  }
  if (suffix.startsWith("model."))
    if (const auto index =
            cydaw::engine::soundModelSettingIndex(suffix.toStdString());
        index >= 0) {
      const auto &setting =
          cydaw::engine::soundModelSettings[static_cast<std::size_t>(index)];
      parameters.soundModel[static_cast<std::size_t>(index)] = base::limit(
          setting.minimum, setting.maximum, static_cast<float>(value));
      return true;
    }
  if (suffix == "sampler.level") {
    parameters.oscillatorLevel =
        base::limit(0.0f, 1.0f, static_cast<float>(value));
    return true;
  }
  // A controller: 0..127; below 0, where the map starts it.
  if (const auto number = samplerControllerOfBinding(suffix); number >= 0) {
    parameters.samplerControllers[static_cast<std::size_t>(number)] =
        value < 0.0 ? -1.0f
                    : base::limit(0.0f, 127.0f, static_cast<float>(value));
    return true;
  }
  if (suffix.startsWith("sampler."))
    if (const auto index =
            cydaw::engine::samplerSettingIndex(suffix.toStdString());
        index >= 0) {
      const auto &setting =
          cydaw::engine::samplerSettings[static_cast<std::size_t>(index)];
      parameters.sampler[static_cast<std::size_t>(index)] = base::limit(
          setting.minimum, setting.maximum, static_cast<float>(value));
      return true;
    }
  if (const auto *effect =
          cydaw::engine::effectParameterForSuffix(suffix.toStdString())) {
    cydaw::engine::setEffectValue(parameters, *effect,
                                  base::limit(effect->minimum, effect->maximum,
                                              static_cast<float>(value)));
    return true;
  }
  if (suffix == "drive.db")
    parameters.driveDb = base::limit(0.0f, 36.0f, static_cast<float>(value));
  else if (suffix == "gain.db")
    parameters.gainDb = base::limit(-120.0f, 24.0f, static_cast<float>(value));
  else if (suffix == "lowpass.cutoff")
    parameters.cutoffHz =
        base::limit(20.0f, 20'000.0f, static_cast<float>(value));
  else if (suffix == "lowpass.resonance")
    parameters.resonance = base::limit(0.1f, 20.0f, static_cast<float>(value));
  else if (suffix == "highpass.cutoff")
    parameters.highpassCutoffHz =
        base::limit(10.0f, 20'000.0f, static_cast<float>(value));
  else if (suffix == "clip.threshold")
    parameters.clipThreshold =
        base::limit(0.001f, 1.0f, static_cast<float>(value));
  else if (suffix == "stereo.width")
    parameters.stereoWidth = base::limit(0.0f, 2.0f, static_cast<float>(value));
  else if (suffix == "pan.value")
    parameters.pan = base::limit(-1.0f, 1.0f, static_cast<float>(value));
  else if (suffix == "oscillator.frequency")
    parameters.oscillatorFrequencyHz =
        base::limit(20.0f, 20'000.0f, static_cast<float>(value));
  else if (suffix == "polyOscillator.waveform")
    parameters.oscillatorWaveform =
        base::limit(0.0f, 3.0f, static_cast<float>(value));
  else if (suffix == "polyOscillator.level")
    parameters.oscillatorLevel =
        base::limit(0.0f, 0.5f, static_cast<float>(value));
  else if (suffix == "envelope.attack")
    parameters.attackMilliseconds =
        base::limit(0.1f, 5'000.0f, static_cast<float>(value));
  else if (suffix == "envelope.decay")
    parameters.decayMilliseconds =
        base::limit(0.1f, 5'000.0f, static_cast<float>(value));
  else if (suffix == "envelope.sustain")
    parameters.sustainLevel =
        base::limit(0.0f, 1.0f, static_cast<float>(value));
  else if (suffix == "envelope.release")
    parameters.releaseMilliseconds =
        base::limit(0.1f, 10'000.0f, static_cast<float>(value));
  else if (suffix == "voiceFilter.cutoff")
    parameters.cutoffHz =
        base::limit(20.0f, 20'000.0f, static_cast<float>(value));
  else if (suffix == "voiceFilter.resonance")
    parameters.resonance = base::limit(0.1f, 20.0f, static_cast<float>(value));
  else if (suffix == "noise.level")
    parameters.noiseLevel = base::limit(0.0f, 1.0f, static_cast<float>(value));
  else if (suffix == "delay.time")
    parameters.delayMilliseconds =
        base::limit(1.0f, 2'000.0f, static_cast<float>(value));
  else if (suffix == "delay.feedback")
    parameters.feedback = base::limit(0.0f, 0.94f, static_cast<float>(value));
  else if (suffix == "delay.mix" || suffix == "reverb.mix")
    parameters.mix = base::limit(0.0f, 1.0f, static_cast<float>(value));
  else if (suffix == "reverb.roomSize")
    parameters.roomSize = base::limit(0.0f, 0.95f, static_cast<float>(value));
  else if (suffix == "compressor.threshold")
    parameters.thresholdDb =
        base::limit(-60.0f, 0.0f, static_cast<float>(value));
  else if (suffix == "compressor.ratio")
    parameters.ratio = base::limit(1.0f, 20.0f, static_cast<float>(value));
  else if (suffix == "eq.tone")
    parameters.tone = base::limit(-1.0f, 1.0f, static_cast<float>(value));
  else if (suffix == "eqBand.frequency")
    parameters.bandFrequencyHz =
        base::limit(20.0f, 20'000.0f, static_cast<float>(value));
  else if (suffix == "eqBand.gain")
    parameters.bandGainDb =
        base::limit(-30.0f, 30.0f, static_cast<float>(value));
  else if (suffix == "eqBand.q")
    parameters.bandQ = base::limit(0.1f, 18.0f, static_cast<float>(value));
  else if (suffix == "wavetable.frequency")
    parameters.oscillatorFrequencyHz =
        base::limit(20.0f, 8'000.0f, static_cast<float>(value));
  else if (suffix == "wavetable.position")
    parameters.wavetablePosition =
        base::limit(0.0f, 1.0f, static_cast<float>(value));
  else if (suffix == "wavetable.level")
    parameters.oscillatorLevel =
        base::limit(0.0f, 1.0f, static_cast<float>(value));
  else if (suffix == "wavetable.detune")
    parameters.wavetableDetuneCents =
        base::limit(0.0f, 50.0f, static_cast<float>(value));
  else if (suffix == "string.frequency")
    parameters.oscillatorFrequencyHz =
        base::limit(20.0f, 4'000.0f, static_cast<float>(value));
  else if (suffix == "string.damping")
    parameters.stringDamping =
        base::limit(0.0f, 1.0f, static_cast<float>(value));
  else if (suffix == "string.decay")
    parameters.stringDecaySeconds =
        base::limit(0.05f, 20.0f, static_cast<float>(value));
  else if (suffix == "lfo.rate")
    parameters.modulationRateHz =
        base::limit(0.01f, 40.0f, static_cast<float>(value));
  else if (suffix == "lfo.depth" || suffix == "valueMath.amount")
    parameters.modulationDepth =
        base::limit(0.0f, 1.0f, static_cast<float>(value));
  else if (suffix == "lfo.offset" || suffix == "macro.value" ||
           suffix == "valueMath.offset")
    parameters.modulationOffset =
        base::limit(-1.0f, 1.0f, static_cast<float>(value));
  else if (suffix == "lfo.shape")
    parameters.modulationShape =
        base::limit(0.0f, 3.0f, static_cast<float>(value));
  else if (suffix == "value.operand" || suffix == "value.constant") {
    parameters.valueOperand =
        base::limit(-1'000'000.0f, 1'000'000.0f, static_cast<float>(value));
    if (suffix == "value.operand")
      parameters.valueInputB = parameters.valueOperand;
  } else if (suffix == "value.minimum")
    parameters.valueMinimum =
        base::limit(-1'000'000.0f, 1'000'000.0f, static_cast<float>(value));
  else if (suffix == "value.maximum")
    parameters.valueMaximum =
        base::limit(-1'000'000.0f, 1'000'000.0f, static_cast<float>(value));
  else if (suffix == "map.inputMinimum")
    parameters.mapInputMinimum =
        base::limit(-1'000'000.0f, 1'000'000.0f, static_cast<float>(value));
  else if (suffix == "map.inputMaximum")
    parameters.mapInputMaximum =
        base::limit(-1'000'000.0f, 1'000'000.0f, static_cast<float>(value));
  else if (suffix == "map.outputMinimum")
    parameters.mapOutputMinimum =
        base::limit(-1'000'000.0f, 1'000'000.0f, static_cast<float>(value));
  else if (suffix == "map.outputMaximum")
    parameters.mapOutputMaximum =
        base::limit(-1'000'000.0f, 1'000'000.0f, static_cast<float>(value));
  else if (suffix == "follower.attack")
    parameters.followerAttackSeconds =
        base::limit(0.0001f, 1.0f, static_cast<float>(value));
  else if (suffix == "follower.release")
    parameters.followerReleaseSeconds =
        base::limit(0.001f, 5.0f, static_cast<float>(value));
  else if (suffix == "midi.transpose")
    parameters.midiTranspose =
        base::limit(-48.0f, 48.0f, static_cast<float>(value));
  else if (suffix == "midi.note.minimum")
    parameters.midiMinimumNote =
        base::limit(0.0f, 127.0f, static_cast<float>(value));
  else if (suffix == "midi.note.maximum")
    parameters.midiMaximumNote =
        base::limit(0.0f, 127.0f, static_cast<float>(value));
  else if (suffix == "midi.velocity")
    parameters.midiVelocityScale =
        base::limit(0.0f, 2.0f, static_cast<float>(value));
  else if (suffix == "voice.mode")
    parameters.voiceMode =
        base::limit(0.0f, 2.0f, std::round(static_cast<float>(value)));
  else if (suffix == "voice.glide")
    parameters.glideMilliseconds =
        base::limit(0.0f, 2'000.0f, static_cast<float>(value));
  else
    return false;
  return true;
}
double
nativeParameterValue(const cydaw::engine::NativeDspNodeParameters &parameters,
                     const cydaw::base::Text &binding) {
  const auto suffix = parameterSuffix(binding);
  if (suffix == "prism.level")
    return parameters.oscillatorLevel;
  if (suffix.startsWith("prism."))
    if (const auto index = cydaw::engine::prism::parameterIndex(
            suffix.substring(6).toStdString());
        index >= 0)
      return parameters.prism[static_cast<std::size_t>(index)];
  if (suffix == "kick.level")
    return parameters.oscillatorLevel;
  if (suffix.startsWith("kick."))
    if (const auto index = cydaw::engine::kick::parameterIndex(
            suffix.substring(5).toStdString());
        index >= 0)
      return parameters.kick[static_cast<std::size_t>(index)];
  if (suffix.startsWith("batterie."))
    if (const auto index = cydaw::engine::drum::parameterIndex(
            suffix.substring(9).toStdString());
        index >= 0)
      return parameters.drum[static_cast<std::size_t>(index)];
  if (suffix.startsWith("selector."))
    if (const auto index = cydaw::engine::instrument_container::settingIndex(
            suffix.substring(9).toStdString());
        index >= 0)
      return parameters.selector[static_cast<std::size_t>(index)];
  if (const auto model = cydaw::engine::synth::modelForPrefix(
          suffix.upToFirstOccurrenceOf(".", false, false).toStdString());
      model != cydaw::engine::synth::Model::none) {
    const auto key = suffix.fromFirstOccurrenceOf(".", false, false);
    if (key == "level")
      return parameters.oscillatorLevel;
    if (const auto index =
            cydaw::engine::synth::parameterIndex(model, key.toStdString());
        index >= 0)
      return parameters.synth[static_cast<std::size_t>(index)];
  }
  if (suffix == "model.level")
    return parameters.oscillatorLevel;
  if (suffix.startsWith("model."))
    if (const auto index =
            cydaw::engine::soundModelSettingIndex(suffix.toStdString());
        index >= 0)
      return parameters.soundModel[static_cast<std::size_t>(index)];
  if (suffix == "sampler.level")
    return parameters.oscillatorLevel;
  if (const auto number = samplerControllerOfBinding(suffix); number >= 0)
    return parameters.samplerControllers[static_cast<std::size_t>(number)];
  if (suffix.startsWith("sampler."))
    if (const auto index =
            cydaw::engine::samplerSettingIndex(suffix.toStdString());
        index >= 0)
      return parameters.sampler[static_cast<std::size_t>(index)];
  if (const auto *effect =
          cydaw::engine::effectParameterForSuffix(suffix.toStdString()))
    return cydaw::engine::effectValue(parameters, *effect);
  if (suffix == "drive.db")
    return parameters.driveDb;
  if (suffix == "gain.db")
    return parameters.gainDb;
  if (suffix == "lowpass.cutoff")
    return parameters.cutoffHz;
  if (suffix == "lowpass.resonance")
    return parameters.resonance;
  if (suffix == "highpass.cutoff")
    return parameters.highpassCutoffHz;
  if (suffix == "clip.threshold")
    return parameters.clipThreshold;
  if (suffix == "stereo.width")
    return parameters.stereoWidth;
  if (suffix == "pan.value")
    return parameters.pan;
  if (suffix == "oscillator.frequency")
    return parameters.oscillatorFrequencyHz;
  if (suffix == "polyOscillator.waveform")
    return parameters.oscillatorWaveform;
  if (suffix == "polyOscillator.level")
    return parameters.oscillatorLevel;
  if (suffix == "envelope.attack")
    return parameters.attackMilliseconds;
  if (suffix == "envelope.decay")
    return parameters.decayMilliseconds;
  if (suffix == "envelope.sustain")
    return parameters.sustainLevel;
  if (suffix == "envelope.release")
    return parameters.releaseMilliseconds;
  if (suffix == "voiceFilter.cutoff")
    return parameters.cutoffHz;
  if (suffix == "voiceFilter.resonance")
    return parameters.resonance;
  if (suffix == "noise.level")
    return parameters.noiseLevel;
  if (suffix == "delay.time")
    return parameters.delayMilliseconds;
  if (suffix == "delay.feedback")
    return parameters.feedback;
  if (suffix == "delay.mix" || suffix == "reverb.mix")
    return parameters.mix;
  if (suffix == "reverb.roomSize")
    return parameters.roomSize;
  if (suffix == "eqBand.frequency")
    return parameters.bandFrequencyHz;
  if (suffix == "wavetable.frequency" || suffix == "string.frequency")
    return parameters.oscillatorFrequencyHz;
  if (suffix == "wavetable.position")
    return parameters.wavetablePosition;
  if (suffix == "wavetable.level")
    return parameters.oscillatorLevel;
  if (suffix == "wavetable.detune")
    return parameters.wavetableDetuneCents;
  if (suffix == "string.damping")
    return parameters.stringDamping;
  if (suffix == "string.decay")
    return parameters.stringDecaySeconds;
  if (suffix == "eqBand.gain")
    return parameters.bandGainDb;
  if (suffix == "eqBand.q")
    return parameters.bandQ;
  if (suffix == "compressor.threshold")
    return parameters.thresholdDb;
  if (suffix == "compressor.ratio")
    return parameters.ratio;
  if (suffix == "eq.tone")
    return parameters.tone;
  if (suffix == "lfo.rate")
    return parameters.modulationRateHz;
  if (suffix == "lfo.depth" || suffix == "valueMath.amount")
    return parameters.modulationDepth;
  if (suffix == "lfo.offset" || suffix == "macro.value" ||
      suffix == "valueMath.offset")
    return parameters.modulationOffset;
  if (suffix == "lfo.shape")
    return parameters.modulationShape;
  if (suffix == "value.operand" || suffix == "value.constant")
    return parameters.valueOperand;
  if (suffix == "value.minimum")
    return parameters.valueMinimum;
  if (suffix == "value.maximum")
    return parameters.valueMaximum;
  if (suffix == "map.inputMinimum")
    return parameters.mapInputMinimum;
  if (suffix == "map.inputMaximum")
    return parameters.mapInputMaximum;
  if (suffix == "map.outputMinimum")
    return parameters.mapOutputMinimum;
  if (suffix == "map.outputMaximum")
    return parameters.mapOutputMaximum;
  if (suffix == "follower.attack")
    return parameters.followerAttackSeconds;
  if (suffix == "follower.release")
    return parameters.followerReleaseSeconds;
  if (suffix == "midi.transpose")
    return parameters.midiTranspose;
  if (suffix == "midi.note.minimum")
    return parameters.midiMinimumNote;
  if (suffix == "midi.note.maximum")
    return parameters.midiMaximumNote;
  if (suffix == "midi.velocity")
    return parameters.midiVelocityScale;
  if (suffix == "voice.mode")
    return parameters.voiceMode;
  if (suffix == "voice.glide")
    return parameters.glideMilliseconds;
  return 0.0;
}
void relabelPublishedParameters(ModuleInterfaceState &state,
                                const ModuleInterfaceState &fresh) {
  std::map<cydaw::base::Text, cydaw::base::Text> labels;
  for (const auto &made : fresh.parameters)
    labels.emplace(made.key, made.label);
  for (auto &parameter : state.parameters)
    if (const auto found = labels.find(parameter.key);
        found != labels.end() && parameter.label != found->second) {
      if (parameter.description == parameter.label)
        parameter.description = found->second;
      parameter.label = found->second;
    }
}
void appendPublishedParameters(
    ModuleInterfaceState &state, const cydaw::base::Text &typeKey,
    const std::uint64_t nodeId,
    const cydaw::engine::NativeDspNodeParameters &parameters,
    const cydaw::engine::SamplerMap *samplerMap = nullptr) {
  const auto add = [&state,
                    nodeId](cydaw::base::Text suffix, cydaw::base::Text label,
                            cydaw::base::Text unit, const double minimum,
                            const double maximum, const double value) {
    ModulePublishedParameter parameter{nodeParameterBinding(nodeId, suffix),
                                       std::move(label),
                                       std::move(unit),
                                       minimum,
                                       maximum,
                                       value};
    parameter.defaultValue = value;
    parameter.hostParameterId = stableModuleParameterId(parameter.key);
    parameter.description = parameter.label;
    state.parameters.push_back(std::move(parameter));
  };
  if (const auto *effect =
          cydaw::engine::effectForType(typeKey.toStdString())) {
    for (const auto &item : effect->parameters)
      if (!item.key.empty())
        add(cydaw::base::Text(std::string(item.suffix)),
            item.band >= 0
                ? tr(effect->typeKey == "filter-pro"
                         ? "effect.parameter.filterUnit"
                     : effect->typeKey == "pompe" ? "effect.parameter.pointWord"
                     : effect->typeKey == "resonateurs"
                         ? "effect.parameter.resonatorWord"
                     : effect->typeKey == "accords"
                         ? "effect.parameter.noteWord"
                     : effect->typeKey == "sequenceur"
                         ? "effect.parameter.stepWord"
                     : effect->typeKey == "fx-layer"
                         ? "container.parameter.layerWord"
                     : effect->typeKey == "fx-selector"
                         ? "container.parameter.chainWord"
                         : "effect.parameter.band") +
                      " " + cydaw::base::Text(item.band + 1) +
                      cydaw::base::Text::fromUTF8(" \xc2\xb7 ") +
                      tr(cydaw::base::Text(std::string(item.labelKey)))
                : tr(cydaw::base::Text(std::string(item.labelKey))),
            cydaw::base::Text(std::string(item.unit)), item.minimum,
            item.maximum, cydaw::engine::effectValue(parameters, item));
    // A modulator's card it belongs to is no knob.
    for (auto &parameter : state.parameters)
      if (parameter.key.endsWith(".ownerLow") ||
          parameter.key.endsWith(".ownerHigh")) {
        parameter.showInDevice = false;
        parameter.publishToHost = false;
      }
  } else if (typeKey == "drive")
    add("drive.db", tr("node.drive"), "dB", 0.0, 36.0, parameters.driveDb);
  else if (typeKey == "gain")
    add("gain.db", tr("node.parameter.gain"), "dB", -120.0, 24.0,
        parameters.gainDb);
  else if (typeKey == "wavetable") {
    add("wavetable.frequency", tr("node.parameter.frequency"), "Hz", 20.0,
        8'000.0, parameters.oscillatorFrequencyHz);
    add("wavetable.position", tr("node.parameter.tablePosition"), {}, 0.0, 1.0,
        parameters.wavetablePosition);
    add("wavetable.level", tr("node.parameter.level"), {}, 0.0, 1.0,
        parameters.oscillatorLevel);
    add("wavetable.detune", tr("node.parameter.detuneCents"), "c", 0.0, 50.0,
        parameters.wavetableDetuneCents);
  } else if (typeKey == "sound-model") {
    add("model.level", tr("node.parameter.level"), {}, 0.0, 1.0,
        parameters.oscillatorLevel);
    for (std::size_t index = 0; index < cydaw::engine::soundModelSettingCount;
         ++index) {
      const auto &setting = cydaw::engine::soundModelSettings[index];
      add(cydaw::base::Text(std::string(setting.suffix)),
          tr(cydaw::base::Text(std::string(setting.labelKey))),
          cydaw::base::Text(std::string(setting.unit)), setting.minimum,
          setting.maximum, parameters.soundModel[index]);
    }
  } else if (typeKey == "prism-synth") {
    // Every setting: the card's own panel shows them its way; a host and
    // the workshop list them all, named by their section (in English,
    // whatever the language: a module's settings).
    // (A drawn LFO's points are drawn in Prisme's window, not listed: they
    // are kept with the card all the same. The third oscillator, the
    // spectral morphs and the LFOs' stereo come after them.)
    for (int index = 0; index < cydaw::engine::prism::parameterCount; ++index) {
      if (index == cydaw::engine::prism::drawBase(0))
        index = cydaw::engine::prism::oscCBase;
      const auto &setting = cydaw::engine::prism::parameter(index);
      const auto section = cydaw::base::Text(setting.key)
                               .upToFirstOccurrenceOf(".", false, false);
      const auto label = (setting.group == cydaw::engine::prism::Group::macro
                              ? cydaw::base::Text()
                              : section.toUpperCase() + " · ") +
                         cydaw::base::Text::fromUTF8(setting.english.c_str());
      add("prism." + cydaw::base::Text(setting.key), label,
          cydaw::base::Text::fromUTF8(setting.unit.c_str()), setting.minimum,
          setting.maximum, parameters.prism[static_cast<std::size_t>(index)]);
      // A host takes 32 at most: the ones a performance moves are
      // offered, the rest wait to be ticked (workshop, Parameters).
      static const std::set<std::string> performed{
          "macro1",        "macro2",           "macro3",       "macro4",
          "a.position",    "b.position",       "a.warpAmount", "a.detune",
          "filter.cutoff", "filter.resonance", "filter.drive", "env1.attack",
          "env1.release",  "lfo1.rate",        "voice.glide",  "voice.master"};
      const auto offered = performed.contains(setting.key);
      state.parameters.back().publishToHost = offered;
      state.parameters.back().showInDevice = offered;
    }
  } else if (typeKey == "kick-synth") {
    // Every setting, named by its section (in English); all of them fit a host.
    for (int index = 0; index < cydaw::engine::kick::parameterCount; ++index) {
      const auto &setting = cydaw::engine::kick::parameter(index);
      using Group = cydaw::engine::kick::Group;
      const char *section = "";
      switch (setting.group) {
      case Group::body:
        section = "BODY";
        break;
      case Group::amplitude:
        section = "AMPLITUDE";
        break;
      case Group::click:
        section = "CLICK";
        break;
      case Group::sub:
        section = "SUB";
        break;
      case Group::drive:
        section = "DRIVE";
        break;
      case Group::tone:
        section = "TONE";
        break;
      case Group::global:
        section = "";
        break;
      }
      const auto label = (cydaw::base::Text(section).isEmpty()
                              ? cydaw::base::Text()
                              : cydaw::base::Text(section) + " · ") +
                         cydaw::base::Text::fromUTF8(setting.english.c_str());
      add("kick." + cydaw::base::Text(setting.key), label,
          cydaw::base::Text::fromUTF8(setting.unit.c_str()), setting.minimum,
          setting.maximum, parameters.kick[static_cast<std::size_t>(index)]);
    }
  } else if (const auto model =
                 cydaw::engine::synth::modelForType(typeKey.toStdString());
             model != cydaw::engine::synth::Model::none) {
    // A synth of the family: every setting named by its section (in
    // English); the ones a performance moves offered to a host (32 at most).
    const auto &info = cydaw::engine::synth::info(model);
    const auto prefix = cydaw::base::Text(info.prefix) + ".";
    static const std::set<std::string> performed{
        "voice.glide", "voice.master",  "filter.cutoff", "filter.resonance",
        "filter.env",  "filter.drive",  "amp.attack",    "amp.decay",
        "amp.sustain", "amp.release",   "lfo1.rate",     "lfo1.pitch",
        "lfo2.rate",   "lfo2.cutoff",   "osc1.pw",       "osc2.fine",
        "fm.amount",   "unison.detune", "macro1",        "macro2",
        "macro3",      "macro4"};
    for (int index = 0; index < cydaw::engine::synth::parameterCount(model);
         ++index) {
      const auto &setting = cydaw::engine::synth::parameter(model, index);
      const auto &section = info.sections[static_cast<std::size_t>(
          std::clamp(setting.section, 0,
                     std::max(0, static_cast<int>(info.sections.size()) - 1)))];
      const auto name = cydaw::base::Text::fromUTF8(setting.english.c_str());
      const auto label =
          setting.section == 0 ||
                  name.contains(cydaw::base::Text::fromUTF8("\xc2\xb7"))
              ? name
              : cydaw::base::Text::fromUTF8(section.english.c_str()) +
                    cydaw::base::Text::fromUTF8(" \xc2\xb7 ") + name;
      add(prefix + cydaw::base::Text(setting.key), label,
          cydaw::base::Text::fromUTF8(setting.unit.c_str()), setting.minimum,
          setting.maximum, parameters.synth[static_cast<std::size_t>(index)]);
      const auto offered = performed.contains(setting.key);
      state.parameters.back().publishToHost = offered;
      state.parameters.back().showInDevice = offered;
    }
  } else if (typeKey == "instrument-selector") {
    // How it chooses its layer (InstrumentContainers.h), in English.
    for (const auto &setting : cydaw::engine::instrument_container::settings) {
      const auto index =
          cydaw::engine::instrument_container::settingIndex(setting.key);
      add("selector." + cydaw::base::Text(std::string(setting.key)),
          cydaw::base::Text(setting.label), {}, setting.minimum,
          setting.maximum,
          parameters.selector[static_cast<std::size_t>(index)]);
    }
  } else if (typeKey == "sampler" || typeKey == "batterie") {
    const auto french = Localization::instance().languageCode() == "fr";
    if (typeKey == "batterie")
      for (int index = 0; index < cydaw::engine::drum::valueCount; ++index) {
        const auto &setting = cydaw::engine::drum::parameter(index);
        add("batterie." + cydaw::base::Text(setting.key),
            cydaw::base::Text::fromUTF8(
                (french ? setting.french : setting.english).c_str()),
            cydaw::base::Text(setting.unit), setting.minimum, setting.maximum,
            parameters.drum[static_cast<std::size_t>(index)]);
      }
    add("sampler.level", tr("node.parameter.level"), {}, 0.0, 1.0,
        parameters.oscillatorLevel);
    for (std::size_t index = 0; index < cydaw::engine::samplerSettingCount;
         ++index) {
      const auto &setting = cydaw::engine::samplerSettings[index];
      add(cydaw::base::Text(std::string(setting.suffix)),
          tr(cydaw::base::Text(std::string(setting.labelKey))),
          cydaw::base::Text(std::string(setting.unit)), setting.minimum,
          setting.maximum, parameters.sampler[index]);
      // The articulation is chosen by its name (the controls' tab), not on a
      // knob; the simple card is what the card is, not a setting to play.
      if (index == static_cast<std::size_t>(
                       cydaw::engine::SamplerSetting::articulation) ||
          index ==
              static_cast<std::size_t>(cydaw::engine::SamplerSetting::simple) ||
          index ==
              static_cast<std::size_t>(cydaw::engine::SamplerSetting::grains)) {
        state.parameters.back().publishToHost = false;
        state.parameters.back().showInDevice = false;
      }
      // The grains' settings are on the card's own pane (its waveform),
      // shown when it plays grains.
      if (index >
          static_cast<std::size_t>(cydaw::engine::SamplerSetting::grains))
        state.parameters.back().showInDevice = false;
    }
    // The controls its map has (an SFZ's microphones, blends, pedal),
    // 0..127: automated and set by name, not on the card's face.
    if (samplerMap != nullptr)
      for (const auto &control : samplerMap->controls) {
        const auto number =
            static_cast<std::size_t>(std::clamp(control.controller, 0, 127));
        const auto set = parameters.samplerControllers[number];
        add("sampler.cc" + cydaw::base::Text(static_cast<int>(number)),
            cydaw::base::Text::fromUTF8(control.label.c_str()), {}, 0.0, 127.0,
            set >= 0.0f ? set : samplerMap->controllerStart[number]);
        state.parameters.back().publishToHost = false;
        state.parameters.back().showInDevice = false;
      }
  } else if (typeKey == "string") {
    add("string.frequency", tr("node.parameter.frequency"), "Hz", 20.0, 4'000.0,
        parameters.oscillatorFrequencyHz);
    add("string.damping", tr("node.parameter.damping"), {}, 0.0, 1.0,
        parameters.stringDamping);
    add("string.decay", tr("node.parameter.decay"), "s", 0.05, 20.0,
        parameters.stringDecaySeconds);
  } else if (typeKey == "eq-band") {
    add("eqBand.frequency", tr("node.parameter.frequency"), "Hz", 20.0,
        20'000.0, parameters.bandFrequencyHz);
    add("eqBand.gain", tr("node.parameter.bandGain"), "dB", -30.0, 30.0,
        parameters.bandGainDb);
    add("eqBand.q", tr("node.parameter.q"), "Q", 0.1, 18.0, parameters.bandQ);
  } else if (typeKey == "lowpass") {
    add("lowpass.cutoff", tr("node.parameter.cutoff"), "Hz", 20.0, 20'000.0,
        parameters.cutoffHz);
    add("lowpass.resonance", tr("node.parameter.resonance"), "Q", 0.1, 20.0,
        parameters.resonance);
  } else if (typeKey == "highpass")
    add("highpass.cutoff", tr("node.parameter.cutoff"), "Hz", 10.0, 20'000.0,
        parameters.highpassCutoffHz);
  else if (typeKey == "gain-pan")
    add("pan.value", tr("node.parameter.pan"), {}, -1.0, 1.0, parameters.pan);
  else if (typeKey == "hard-clip")
    add("clip.threshold", tr("node.parameter.threshold"), {}, 0.001, 1.0,
        parameters.clipThreshold);
  else if (typeKey == "stereo-width")
    add("stereo.width", tr("node.parameter.width"), {}, 0.0, 2.0,
        parameters.stereoWidth);
  else if (typeKey == "oscillator")
    add("oscillator.frequency", tr("node.parameter.frequency"), "Hz", 20.0,
        20'000.0, parameters.oscillatorFrequencyHz);
  else if (typeKey == "poly-oscillator") {
    add("polyOscillator.waveform", tr("node.parameter.waveform"), {}, 0.0, 3.0,
        parameters.oscillatorWaveform);
    add("polyOscillator.level", tr("node.parameter.level"), {}, 0.0, 0.5,
        parameters.oscillatorLevel);
  } else if (typeKey == "envelope") {
    add("envelope.attack", tr("node.parameter.attack"), "ms", 0.1, 5'000.0,
        parameters.attackMilliseconds);
    add("envelope.decay", tr("node.parameter.decay"), "ms", 0.1, 5'000.0,
        parameters.decayMilliseconds);
    add("envelope.sustain", tr("node.parameter.sustain"), {}, 0.0, 1.0,
        parameters.sustainLevel);
    add("envelope.release", tr("node.parameter.release"), "ms", 0.1, 10'000.0,
        parameters.releaseMilliseconds);
  } else if (typeKey == "voice-filter") {
    add("voiceFilter.cutoff", tr("node.parameter.cutoff"), "Hz", 20.0, 20'000.0,
        parameters.cutoffHz);
    add("voiceFilter.resonance", tr("node.parameter.resonance"), "Q", 0.1, 20.0,
        parameters.resonance);
  } else if (typeKey == "noise")
    add("noise.level", tr("node.parameter.level"), {}, 0.0, 1.0,
        parameters.noiseLevel);
  else if (typeKey == "delay") {
    add("delay.time", tr("node.parameter.time"), "ms", 1.0, 2'000.0,
        parameters.delayMilliseconds);
    add("delay.feedback", tr("node.parameter.feedback"), {}, 0.0, 0.94,
        parameters.feedback);
    add("delay.mix", tr("node.parameter.mix"), {}, 0.0, 1.0, parameters.mix);
  } else if (typeKey == "reverb" || typeKey == "reverb-rack") {
    add("reverb.roomSize", tr("node.parameter.roomSize"), {}, 0.0, 0.95,
        parameters.roomSize);
    add("reverb.mix", tr("node.parameter.mix"), {}, 0.0, 1.0, parameters.mix);
  } else if (typeKey == "compressor") {
    add("compressor.threshold", tr("node.parameter.threshold"), "dB", -60.0,
        0.0, parameters.thresholdDb);
    add("compressor.ratio", tr("node.parameter.ratio"), ":1", 1.0, 20.0,
        parameters.ratio);
  } else if (typeKey == "eq") {
    add("eq.tone", tr("node.parameter.tone"), {}, -1.0, 1.0, parameters.tone);
  } else if (typeKey == "lfo" || typeKey == "lfo-core") {
    add("lfo.rate", tr("modulation.rate"), "Hz", 0.01, 40.0,
        parameters.modulationRateHz);
    add("lfo.depth", tr("modulation.depth"), {}, 0.0, 1.0,
        parameters.modulationDepth);
    add("lfo.offset", tr("modulation.offset"), {}, -1.0, 1.0,
        parameters.modulationOffset);
    add("lfo.shape", tr("modulation.shape"), {}, 0.0, 3.0,
        parameters.modulationShape);
  } else if (typeKey == "macro") {
    add("macro.value", tr("modulation.value"), {}, -1.0, 1.0,
        parameters.modulationOffset);
  } else if (typeKey == "control-math") {
    add("valueMath.amount", tr("modulation.amount"), {}, 0.0, 1.0,
        parameters.modulationDepth);
    add("valueMath.offset", tr("modulation.offset"), {}, -1.0, 1.0,
        parameters.modulationOffset);
  } else if (typeKey == "value-constant") {
    add("value.constant", tr("node.parameter.value"), {}, -1'000'000.0,
        1'000'000.0, parameters.valueOperand);
  } else if (typeKey == "map-range") {
    add("map.inputMinimum", tr("node.parameter.inputMinimum"), {}, -1'000'000.0,
        1'000'000.0, parameters.mapInputMinimum);
    add("map.inputMaximum", tr("node.parameter.inputMaximum"), {}, -1'000'000.0,
        1'000'000.0, parameters.mapInputMaximum);
    add("map.outputMinimum", tr("node.parameter.outputMinimum"), {},
        -1'000'000.0, 1'000'000.0, parameters.mapOutputMinimum);
    add("map.outputMaximum", tr("node.parameter.outputMaximum"), {},
        -1'000'000.0, 1'000'000.0, parameters.mapOutputMaximum);
  } else if (typeKey == "envelope-follower") {
    add("follower.attack", tr("node.parameter.attack"), "s", 0.0001, 1.0,
        parameters.followerAttackSeconds);
    add("follower.release", tr("node.parameter.release"), "s", 0.001, 5.0,
        parameters.followerReleaseSeconds);
  } else if (typeKey.startsWith("value-") || typeKey.startsWith("logic-")) {
    if (typeKey == "value-clamp") {
      add("value.minimum", tr("node.parameter.minimum"), {}, -1'000'000.0,
          1'000'000.0, parameters.valueMinimum);
      add("value.maximum", tr("node.parameter.maximum"), {}, -1'000'000.0,
          1'000'000.0, parameters.valueMaximum);
    } else if (typeKey != "value-display" && typeKey != "value-abs" &&
               typeKey != "value-negate" && typeKey != "value-sine" &&
               typeKey != "value-cosine" && typeKey != "value-square-root" &&
               typeKey != "value-floor" && typeKey != "value-ceil" &&
               typeKey != "value-round" && typeKey != "logic-not") {
      add("value.operand", tr("node.parameter.operand"), {}, -1'000'000.0,
          1'000'000.0, parameters.valueOperand);
    }
  } else if (typeKey == "midi-transpose") {
    add("midi.transpose", tr("node.parameter.semitones"), "st", -48.0, 48.0,
        parameters.midiTranspose);
  } else if (typeKey == "note-filter") {
    add("midi.note.minimum", tr("node.parameter.minimumNote"), {}, 0.0, 127.0,
        parameters.midiMinimumNote);
    add("midi.note.maximum", tr("node.parameter.maximumNote"), {}, 0.0, 127.0,
        parameters.midiMaximumNote);
  } else if (typeKey == "velocity-map") {
    add("midi.velocity", tr("node.parameter.velocityScale"), {}, 0.0, 2.0,
        parameters.midiVelocityScale);
  } else if (typeKey == "voice-allocator") {
    // Poly, or one note at a time (a saxophone, a flute) sliding between them.
    add("voice.mode", tr("node.parameter.voiceMode"), {}, 0.0, 2.0,
        parameters.voiceMode);
    add("voice.glide", tr("node.parameter.glide"), "ms", 0.0, 2'000.0,
        parameters.glideMilliseconds);
  }
}
void layoutPublishedInterface(ModuleInterfaceState &state,
                              const cydaw::base::Text &fallbackTitle,
                              const bool includeSpectrum,
                              const bool includeSignalVisual = true) {
  const auto findParameterWithSuffix =
      [&state](const cydaw::base::Text &suffix) {
        return std::find_if(state.parameters.begin(), state.parameters.end(),
                            [&suffix](const auto &parameter) {
                              return parameter.key.endsWith(suffix);
                            });
      };
  const auto attack = findParameterWithSuffix("envelope.attack");
  const auto decay = findParameterWithSuffix("envelope.decay");
  const auto sustain = findParameterWithSuffix("envelope.sustain");
  const auto release = findParameterWithSuffix("envelope.release");
  const auto hasEnvelope =
      attack != state.parameters.end() && decay != state.parameters.end() &&
      sustain != state.parameters.end() && release != state.parameters.end();
  const auto cutoff =
      std::find_if(state.parameters.begin(), state.parameters.end(),
                   [](const auto &parameter) {
                     return parameter.key.endsWith("lowpass.cutoff") ||
                            parameter.key.endsWith("voiceFilter.cutoff");
                   });
  const auto resonance =
      cutoff != state.parameters.end()
          ? std::find_if(
                state.parameters.begin(), state.parameters.end(),
                [&cutoff](const auto &parameter) {
                  const auto cutoffSuffix =
                      cutoff->key.endsWith("voiceFilter.cutoff")
                          ? cydaw::base::Text("voiceFilter.cutoff")
                          : cydaw::base::Text("lowpass.cutoff");
                  const auto resonanceSuffix =
                      cutoffSuffix.upToLastOccurrenceOf(".", false, false) +
                      ".resonance";
                  const auto prefix =
                      cutoff->key.dropLastCharacters(cutoffSuffix.length());
                  return parameter.key == prefix + resonanceSuffix;
                })
          : state.parameters.end();
  const auto hasFilterResponse =
      cutoff != state.parameters.end() && resonance != state.parameters.end();
  // Every complete frequency / gain / Q triple published by an EQ band,
  // in the order the bands appear. One triple is a bell; several are a
  // curve, and either way the reader wants the shape, not twelve knobs.
  std::vector<std::array<cydaw::base::Text, 3>> eqBands;
  for (const auto &parameter : state.parameters) {
    if (!parameter.key.endsWith("eqBand.frequency"))
      continue;
    const auto prefix = parameter.key.dropLastCharacters(
        cydaw::base::Text("eqBand.frequency").length());
    const auto sibling = [&state, &prefix](const char *suffix) {
      const auto key = prefix + suffix;
      return std::any_of(
          state.parameters.begin(), state.parameters.end(),
          [&key](const auto &value) { return value.key == key; });
    };
    if (sibling("eqBand.gain") && sibling("eqBand.q"))
      eqBands.push_back(
          {parameter.key, prefix + "eqBand.gain", prefix + "eqBand.q"});
  }
  const auto hasEqCurve = !eqBands.empty();
  const auto boundToCurve = [&eqBands](const cydaw::base::Text &key) {
    return std::any_of(
        eqBands.begin(), eqBands.end(), [&key](const auto &band) {
          return band[0] == key || band[1] == key || band[2] == key;
        });
  };
  const auto threshold = findParameterWithSuffix("compressor.threshold");
  const auto ratio = findParameterWithSuffix("compressor.ratio");
  const auto hasDynamicsResponse =
      threshold != state.parameters.end() && ratio != state.parameters.end();
  // A pair only leaves the knob grid when the plot that replaces it is
  // actually drawn. The two response plots share one row and the dynamics
  // plot wins it, so a graph holding both a compressor and a filter drew the
  // compressor's curve and left the filter's cutoff and resonance with
  // nowhere at all to be edited.
  const auto emitsDynamicsPlot = includeSignalVisual && hasDynamicsResponse;
  const auto emitsFilterPlot =
      includeSignalVisual && !hasDynamicsResponse && hasFilterResponse;

  std::vector<std::size_t> knobParameters;
  knobParameters.reserve(state.parameters.size());
  for (std::size_t index = 0; index < state.parameters.size(); ++index) {
    const auto &key = state.parameters[index].key;
    if (hasEnvelope && (key == attack->key || key == decay->key ||
                        key == sustain->key || key == release->key))
      continue;
    if (emitsFilterPlot && (key == cutoff->key || key == resonance->key))
      continue;
    if (emitsDynamicsPlot && (key == threshold->key || key == ratio->key))
      continue;
    if (boundToCurve(key))
      continue;
    knobParameters.push_back(index);
  }

  const auto parameterCount = knobParameters.size();
  const auto columns =
      std::min<std::size_t>(4, std::max<std::size_t>(1, parameterCount));
  const auto rows = parameterCount == 0
                        ? std::size_t{0}
                        : (parameterCount + columns - 1) / columns;
  // The card's vertical budget, divided between the knob grid and the plots
  // that go under it. A fixed row pitch and a fixed plot origin could not
  // both hold: three rows of knobs reached 0.77 while the envelope started
  // at 0.62 and was drawn straight across them. A compressor publishes both
  // of its parameters through its plot and has no knobs at all, so there
  // the plot takes the whole card.
  constexpr auto contentTop = 0.05f;
  constexpr auto contentBottom = 0.92f;
  constexpr auto blockGap = 0.03f;
  // A wavetable card is mostly its table, so it reserves the visual band
  // the same way a curve or an envelope does. Recognised by what the card
  // publishes, like every other plot here — the node's type is not in scope
  // and does not need to be.
  const auto tablePosition = findParameterWithSuffix("wavetable.position");
  const auto hasWavetable = tablePosition != state.parameters.end();
  const auto hasVisualBlock =
      hasEnvelope || includeSignalVisual || hasEqCurve || hasWavetable;
  // A curve and a response plot are two visuals, not one: when a graph has
  // both, the band holds two rows rather than letting one hide the other.
  const auto stackedVisuals =
      hasEqCurve && (includeSignalVisual || hasEnvelope);
  const auto visualBand = !hasVisualBlock  ? 0.0f
                          : rows == 0      ? contentBottom - contentTop
                          : stackedVisuals ? 0.52f
                                           : 0.34f;
  const auto knobBand =
      std::max(0.0f, contentBottom - contentTop - visualBand -
                         (hasVisualBlock && rows > 0 ? blockGap : 0.0f));
  const auto rowPitch = rows == 0 ? 0.0f : knobBand / static_cast<float>(rows);
  const auto visualTop =
      rows == 0 ? contentTop : contentTop + knobBand + blockGap;
  const auto visualHeight = contentBottom - visualTop;
  if (parameterCount == 0) {
    // The title is what a card shows when it has nothing else. A curve
    // is something else, and the title was being painted underneath it.
    if (!hasEnvelope && !hasFilterResponse && !hasDynamicsResponse &&
        !hasEqCurve && !hasWavetable)
      state.controls.push_back({state.nextControlId++,
                                ModuleControlKind::text,
                                fallbackTitle,
                                {},
                                0.04f,
                                0.06f,
                                0.42f,
                                0.16f});
  } else {
    // With no plot under them the knobs own the card, so they spread out
    // rather than sitting in a band across its top.
    const auto fillsCard = !hasVisualBlock;
    // The floor cannot be taller than the pitch it sits in, or the rows
    // it sizes overlap each other: nine knobs in three rows asked for a
    // tenth of the card each inside a pitch of a ninth.
    const auto controlHeight =
        std::clamp(rowPitch - 0.03f, std::min(0.10f, rowPitch * 0.82f), 0.62f);
    const auto columnWidth = 0.88f / static_cast<float>(columns);
    const auto knobWidth = std::min(fillsCard ? columns == 1   ? 0.44f
                                                : columns == 2 ? 0.36f
                                                               : 0.26f
                                              : 0.20f,
                                    columnWidth - 0.02f);
    const auto centring = fillsCard ? (columnWidth - knobWidth) * 0.5f : 0.0f;
    for (std::size_t index = 0; index < parameterCount; ++index) {
      const auto column = index % columns;
      const auto row = index / columns;
      const auto parameterIndex = knobParameters[index];
      state.controls.push_back(
          {state.nextControlId++, ModuleControlKind::knob,
           state.parameters[parameterIndex].label,
           state.parameters[parameterIndex].key,
           0.04f + static_cast<float>(column) * columnWidth + centring,
           contentTop + static_cast<float>(row) * rowPitch, knobWidth,
           controlHeight});
    }
  }

  if (hasEnvelope) {
    ModuleInterfaceControl control;
    control.id = state.nextControlId++;
    control.kind = ModuleControlKind::envelope;
    control.label = tr("node.envelope");
    control.parameterBinding = attack->key;
    control.secondaryParameterBinding = decay->key;
    control.tertiaryParameterBinding = sustain->key;
    control.quaternaryParameterBinding = release->key;
    control.x = 0.04f;
    // The envelope shares the lower row with the response plots when a
    // curve holds the upper one; without a curve it keeps the whole band.
    control.y = stackedVisuals ? visualTop + visualHeight * 0.54f : visualTop;
    control.width = includeSignalVisual ? 0.50f : 0.72f;
    control.height = stackedVisuals ? visualHeight * 0.46f : visualHeight;
    state.controls.push_back(std::move(control));
  }

  if (hasWavetable) {
    ModuleInterfaceControl control;
    control.id = state.nextControlId++;
    control.kind = ModuleControlKind::wavetable;
    control.label = tr("module.ui.wavetable");
    // Bound to the morph position: the editor shows which frame is being
    // read, and the position knob and the drawing stay one control.
    control.parameterBinding = tablePosition->key;
    control.x = 0.04f;
    control.y = visualTop;
    control.width = 0.88f;
    control.height = visualHeight;
    state.controls.push_back(std::move(control));
  }

  if (hasEqCurve) {
    ModuleInterfaceControl control;
    control.id = state.nextControlId++;
    control.kind = ModuleControlKind::eqCurve;
    control.label = tr("module.ui.eqCurve");
    control.parameterBinding = eqBands.front()[0];
    for (const auto &band : eqBands)
      for (const auto &key : band)
        control.extraParameterBindings.push_back(key);
    control.x = 0.04f;
    control.y = visualTop;
    control.width = 0.88f;
    control.height = stackedVisuals ? visualHeight * 0.46f : visualHeight;
    state.controls.push_back(std::move(control));
  }
  // A wavetable card's visual is its table. The generic waveform and level
  // meter would sit in the same band and, being added after, win it.
  if (includeSignalVisual && !hasWavetable) {
    // Under the curve when there is one, otherwise at the top of the band.
    const auto visualY =
        stackedVisuals ? visualTop + visualHeight * 0.54f : visualTop;
    const auto blockHeight =
        stackedVisuals ? (contentBottom - visualY) : contentBottom - visualTop;
    if (hasDynamicsResponse) {
      ModuleInterfaceControl response;
      response.id = state.nextControlId++;
      response.kind = ModuleControlKind::dynamicsResponse;
      response.label = tr("module.ui.dynamicsResponse");
      response.parameterBinding = threshold->key;
      response.secondaryParameterBinding = ratio->key;
      response.x = hasEnvelope ? 0.57f : 0.04f;
      response.y = visualY;
      response.width = hasEnvelope ? 0.31f : 0.72f;
      response.height = blockHeight;
      state.controls.push_back(std::move(response));
    } else if (hasFilterResponse) {
      ModuleInterfaceControl response;
      response.id = state.nextControlId++;
      response.kind = ModuleControlKind::filterResponse;
      response.label = tr("module.ui.filterResponse");
      response.parameterBinding = cutoff->key;
      response.secondaryParameterBinding = resonance->key;
      response.x = hasEnvelope ? 0.57f : 0.04f;
      response.y = visualY;
      response.width = hasEnvelope ? 0.31f : 0.72f;
      response.height = blockHeight;
      state.controls.push_back(std::move(response));
    } else if (hasEnvelope && includeSpectrum) {
      state.controls.push_back({state.nextControlId++,
                                ModuleControlKind::spectrum,
                                tr("module.ui.spectrum"),
                                {},
                                0.57f,
                                visualY,
                                0.20f,
                                blockHeight});
      state.controls.push_back({state.nextControlId++,
                                ModuleControlKind::waveform,
                                tr("module.ui.waveform"),
                                {},
                                0.79f,
                                visualY,
                                0.09f,
                                blockHeight});
    } else if (hasEnvelope) {
      state.controls.push_back({state.nextControlId++,
                                ModuleControlKind::waveform,
                                tr("module.ui.waveform"),
                                {},
                                0.57f,
                                visualY,
                                0.28f,
                                blockHeight});
    } else if (includeSpectrum) {
      state.controls.push_back({state.nextControlId++,
                                ModuleControlKind::spectrum,
                                tr("module.ui.spectrum"),
                                {},
                                0.04f,
                                visualY,
                                0.42f,
                                blockHeight});
      state.controls.push_back({state.nextControlId++,
                                ModuleControlKind::waveform,
                                tr("module.ui.waveform"),
                                {},
                                0.49f,
                                visualY,
                                0.28f,
                                blockHeight});
    } else {
      state.controls.push_back({state.nextControlId++,
                                ModuleControlKind::waveform,
                                tr("module.ui.waveform"),
                                {},
                                0.04f,
                                visualY,
                                0.68f,
                                blockHeight});
    }
    state.controls.push_back(
        {state.nextControlId++,
         ModuleControlKind::levelMeter,
         tr("module.ui.meter"),
         {},
         hasEnvelope ? 0.88f
         : (includeSpectrum || hasFilterResponse || hasDynamicsResponse)
             ? 0.80f
             : 0.76f,
         visualY,
         hasEnvelope       ? 0.08f
         : includeSpectrum ? 0.16f
                           : 0.20f,
         blockHeight});
  }

  // The compact device facade is intentionally generated from the published
  // parameters. The full graph interface remains the only free-form layout.
  rebuildAutomaticDeviceControls(state);
}
void appendInterfacePorts(ModuleInterfaceState &state, const bool hasInput,
                          const std::uint8_t inputKind, const bool hasOutput,
                          const std::uint8_t outputKind,
                          const cydaw::base::Text &inputBinding = {},
                          const cydaw::base::Text &outputBinding = {},
                          const float inputY = 0.40f,
                          const float outputY = 0.40f,
                          const cydaw::base::Text &inputLabel = {},
                          const cydaw::base::Text &outputLabel = {},
                          const std::uint8_t inputChannels = 1,
                          const std::uint8_t outputChannels = 1) {
  const auto append =
      [&state](std::vector<ModuleInterfaceControl> &controls,
               const ModuleControlKind kind, const cydaw::base::Text &label,
               const float y, const std::uint8_t portKind,
               const cydaw::base::Text &binding, const std::uint8_t channels) {
        const auto width = 0.13f;
        controls.push_back(
            {state.nextControlId++, kind, label, binding,
             kind == ModuleControlKind::inputPort ? 0.0f : 1.0f - width, y,
             width, 0.17f, portKind,
             portKind == 0 ? static_cast<std::uint8_t>(
                                 base::limit(1, 8, static_cast<int>(channels)))
                           : std::uint8_t{1}});
      };
  auto &controls =
      moduleInterfaceControls(state, ModuleInterfaceLayoutMode::graph);
  if (hasInput)
    append(controls, ModuleControlKind::inputPort,
           inputLabel.isNotEmpty() ? inputLabel : tr("module.ui.inputPort"),
           inputY, inputKind, inputBinding, inputChannels);
  if (hasOutput)
    append(controls, ModuleControlKind::outputPort,
           outputLabel.isNotEmpty() ? outputLabel : tr("module.ui.outputPort"),
           outputY, outputKind, outputBinding, outputChannels);
}
void insetGeneratedControlsForPorts(ModuleInterfaceState &state) {
  auto &controls =
      moduleInterfaceControls(state, ModuleInterfaceLayoutMode::graph);
  const auto isPort = [](const ModuleInterfaceControl &control) {
    return control.kind == ModuleControlKind::inputPort ||
           control.kind == ModuleControlKind::outputPort;
  };
  if (std::none_of(controls.begin(), controls.end(), isPort))
    return;
  constexpr auto left = 0.15f;
  constexpr auto span = 0.70f;
  for (auto &control : controls) {
    if (isPort(control))
      continue;
    control.x = left + control.x * span;
    control.width = control.width * span;
  }
}
void rebindControlPrefix(ModuleInterfaceControl &control,
                         const cydaw::base::Text &previousPrefix,
                         const cydaw::base::Text &nextPrefix) {
  const auto rebind = [&previousPrefix,
                       &nextPrefix](cydaw::base::Text &binding) {
    if (binding.startsWith(previousPrefix))
      binding = nextPrefix + binding.substring(previousPrefix.length());
  };
  rebind(control.parameterBinding);
  rebind(control.secondaryParameterBinding);
  rebind(control.tertiaryParameterBinding);
  rebind(control.quaternaryParameterBinding);
  for (auto &extra : control.extraParameterBindings)
    rebind(extra);
}
double presentationParameter(const ModuleInterfaceState &state,
                             const cydaw::base::Text &suffix,
                             const double fallback = 0.0) {
  const auto found =
      std::find_if(state.parameters.begin(), state.parameters.end(),
                   [&suffix](const auto &parameter) {
                     return parameter.key.endsWith(suffix);
                   });
  return found != state.parameters.end() ? found->value : fallback;
}
} // namespace
void GraphDocument::applyTranslatedTitles() {
  // The labels below change, so the cached members fingerprint no longer
  // describes what is on screen.
  graphMembersSignature_ = -1;
  for (auto &module : savedModules_)
    if (module.factoryModule)
      for (auto &child : module.nodes)
        if (const auto *type = findType(child.typeKey);
            type != nullptr && child.alias.isEmpty() &&
            child.title != type->title) {
          child.title = type->title;
          module.source = cydaw::base::Value();
          module.definitionCache.reset();
        }
  invalidateSerialisedNodes();
  for (auto &node : nodes_)
    if (const auto *type = findType(node.typeKey); type != nullptr) {
      if (type->translationKey.isNotEmpty())
        node.title = type->title;
      else if (const auto *module = findModule(node.typeKey))
        node.title = module->name;
    }
}
cydaw::base::Value GraphDocument::serialise() const {

  auto root = cydaw::base::Object::Ptr(new cydaw::base::Object());
  root->setProperty(jsonKeys::schema, nodeGraphStateSchema);
  root->setProperty(jsonKeys::valueGraph, valueGraphMode_);
  root->setProperty(jsonKeys::nextNodeId,
                    static_cast<std::int64_t>(nextNodeId_));
  root->setProperty(jsonKeys::nextCommentId,
                    static_cast<std::int64_t>(nextCommentId_));
  root->setProperty(jsonKeys::factoryPreset, activeFactoryPresetKey_);
  root->setProperty(jsonKeys::definitionId,
                    cydaw::base::Text(graphReference_.id));
  root->setProperty(jsonKeys::definitionVersion,
                    cydaw::base::Text(graphReference_.version.toString()));
  root->setProperty(jsonKeys::graphRole,
                    cydaw::graph::toString(inferCurrentGraphRole()));
  root->setProperty(jsonKeys::graphScope, cydaw::graph::toString(graphScope_));
  root->setProperty(jsonKeys::graphName, graphName_);
  root->setProperty(jsonKeys::graphCategory, graphCategory_);
  root->setProperty(jsonKeys::graphAuthor, graphAuthor_);
  root->setProperty(jsonKeys::graphDescription, graphDescription_);
  cydaw::base::Array<cydaw::base::Value> graphTags;
  for (const auto &tag : graphTags_)
    graphTags.add(tag);
  root->setProperty(jsonKeys::graphTags, graphTags);
  if (!programs_.empty())
    root->setProperty("programs", serialiseGraphPrograms(programs_));
  const auto exportIdentity = pluginExportIdentity();
  auto exportProfile = cydaw::base::Object::Ptr(new cydaw::base::Object());
  exportProfile->setProperty(jsonKeys::productName,
                             cydaw::base::Text(exportIdentity.productName));
  exportProfile->setProperty(jsonKeys::companyName,
                             cydaw::base::Text(exportIdentity.companyName));
  exportProfile->setProperty(
      jsonKeys::manufacturerCode,
      cydaw::base::Text(exportIdentity.manufacturerCode));
  exportProfile->setProperty(jsonKeys::pluginCode,
                             cydaw::base::Text(exportIdentity.pluginCode));
  exportProfile->setProperty(
      jsonKeys::version, cydaw::base::Text(exportIdentity.version.toString()));
  root->setProperty(jsonKeys::vst3Export,
                    cydaw::base::Value(exportProfile.get()));
  root->setProperty(jsonKeys::viewZoom, viewZoom_);
  root->setProperty(jsonKeys::viewOffsetX, viewOffset_.x);
  root->setProperty(jsonKeys::viewOffsetY, viewOffset_.y);
  // The same view as a point of the graph, for a window of another size.
  // An editor that has not laid out since it was handed a view passes
  // that view on as it came: the workspace serialises and restores the
  // editor between a load and its first layout, and a centre measured on
  // an unsized canvas would mean nothing.
  if (restoredViewCentre_.has_value()) {
    root->setProperty(jsonKeys::viewCentreX, restoredViewCentre_->x);
    root->setProperty(jsonKeys::viewCentreY, restoredViewCentre_->y);
    if (restoredViewAnchor_ != 0) {
      root->setProperty(jsonKeys::viewAnchorNode,
                        static_cast<std::int64_t>(restoredViewAnchor_));
      root->setProperty(jsonKeys::viewAnchorDX, restoredViewAnchorDelta_.x);
      root->setProperty(jsonKeys::viewAnchorDY, restoredViewAnchorDelta_.y);
    }
  } else
    ;
  cydaw::base::Array<cydaw::base::Value> viewBookmarkValues;
  for (std::size_t index = 0; index < viewBookmarks_.size(); ++index)
    if (const auto &bookmark = viewBookmarks_[index]; bookmark.has_value()) {
      auto value = cydaw::base::Object::Ptr(new cydaw::base::Object());
      value->setProperty(jsonKeys::slot, static_cast<int>(index + 1));
      value->setProperty(jsonKeys::zoom, bookmark->zoom);
      value->setProperty(jsonKeys::offsetX, bookmark->offset.x);
      value->setProperty(jsonKeys::offsetY, bookmark->offset.y);
      viewBookmarkValues.add(cydaw::base::Value(value.get()));
    }
  root->setProperty(jsonKeys::viewBookmarks, viewBookmarkValues);
  root->setProperty(jsonKeys::selectionPanelVisible, selectionPanelVisible_);
  root->setProperty(jsonKeys::configurationPanelVisible,
                    configurationPanelVisible_);
  root->setProperty(jsonKeys::configurationPanelShowsPalette,
                    configurationPanelShowsPalette_);
  root->setProperty(jsonKeys::configurationPanelShowsMembers,
                    configurationPanelShowsMembers_);
  root->setProperty(jsonKeys::minimapVisible, minimapVisible_);
  root->setProperty(jsonKeys::selectionPanelWidth, selectionPanelWidth_);
  root->setProperty(jsonKeys::configurationPanelWidth,
                    configurationPanelWidth_);
  root->setProperty(jsonKeys::presentation,
                    serialiseModuleInterface(graphInterfaceState_));

  cydaw::base::Array<cydaw::base::Value> nodeValues;
  for (const auto &node : nodes_) {
    if (!node.serialised.isVoid()) {
      nodeValues.add(node.serialised);
      continue;
    }
    auto value = cydaw::base::Object::Ptr(new cydaw::base::Object());
    value->setProperty(jsonKeys::id, static_cast<std::int64_t>(node.id));
    value->setProperty(jsonKeys::type, node.typeKey);
    value->setProperty(jsonKeys::title, node.title);
    value->setProperty(jsonKeys::alias, node.alias);
    value->setProperty(jsonKeys::description, node.description);
    value->setProperty(jsonKeys::inputChannels,
                       static_cast<int>(node.inputChannels));
    value->setProperty(jsonKeys::outputChannels,
                       static_cast<int>(node.outputChannels));
    value->setProperty(jsonKeys::x, node.position.x);
    value->setProperty(jsonKeys::y, node.position.y);
    value->setProperty(jsonKeys::parameters,
                       serialiseNativeParameters(node.parameters));
    value->setProperty(jsonKeys::portDefaults,
                       serialisePortDefaults(node.ports));
    value->setProperty(jsonKeys::interface,
                       serialiseModuleInterface(node.interfaceState));
    value->setProperty(jsonKeys::interfaceLayoutOverridden,
                       node.interfaceLayoutOverridden);
    value->setProperty(jsonKeys::collapsed, node.collapsed);
    if (node.bypassed)
      value->setProperty(jsonKeys::bypassed, true);
    if (node.wavetableShape != nullptr) {
      cydaw::base::Array<cydaw::base::Value> harmonics;
      harmonics.ensureStorageAllocated(
          static_cast<int>(cydaw::engine::WavetableShape::frameCount *
                           cydaw::engine::WavetableShape::harmonicCount));
      for (const auto &frame : node.wavetableShape->harmonics)
        for (const auto amplitude : frame)
          harmonics.add(amplitude);
      value->setProperty(jsonKeys::wavetable, harmonics);
    }
    if (node.soundModelJson.isObject())
      value->setProperty(jsonKeys::soundModel, node.soundModelJson);
    if (node.samplerJson.isObject())
      value->setProperty(jsonKeys::sampler, node.samplerJson);
    if (node.prismTablesJson.isObject())
      value->setProperty("prismTables", node.prismTablesJson);
    if (node.impulseJson.isObject())
      value->setProperty("impulse", node.impulseJson);
    if (node.instrumentJson.isObject())
      value->setProperty("instrument", node.instrumentJson);
    if (std::any_of(node.branchNames.begin(), node.branchNames.end(),
                    [](const auto &name) { return name.isNotEmpty(); })) {
      cydaw::base::Array<cydaw::base::Value> names;
      for (const auto &name : node.branchNames)
        names.add(name);
      value->setProperty("branchNames", names);
    }
    node.serialised = cydaw::base::Value(value.get());
    nodeValues.add(node.serialised);
  }
  root->setProperty(jsonKeys::nodes, nodeValues);

  cydaw::base::Array<cydaw::base::Value> connectionValues;
  for (const auto &connection : connections_) {
    auto value = cydaw::base::Object::Ptr(new cydaw::base::Object());
    value->setProperty(jsonKeys::source,
                       static_cast<std::int64_t>(connection.source));
    value->setProperty(jsonKeys::destination,
                       static_cast<std::int64_t>(connection.destination));
    value->setProperty(jsonKeys::kind, static_cast<int>(connection.kind));
    value->setProperty(jsonKeys::audioChannels,
                       static_cast<int>(connection.audioChannels));
    value->setProperty(jsonKeys::sourcePort,
                       static_cast<std::int64_t>(connection.sourcePort));
    value->setProperty(jsonKeys::destinationPort,
                       static_cast<std::int64_t>(connection.destinationPort));
    if (connection.mode != 0) {
      value->setProperty("mode", static_cast<int>(connection.mode));
      value->setProperty("amount", static_cast<double>(connection.amount));
    }
    cydaw::base::Array<cydaw::base::Value> routingValues;
    for (const auto point : connection.routingPoints) {
      auto routing = cydaw::base::Object::Ptr(new cydaw::base::Object());
      routing->setProperty(jsonKeys::x, point.x);
      routing->setProperty(jsonKeys::y, point.y);
      routingValues.add(cydaw::base::Value(routing.get()));
    }
    value->setProperty(jsonKeys::routing, routingValues);
    connectionValues.add(cydaw::base::Value(value.get()));
  }
  root->setProperty(jsonKeys::connections, connectionValues);

  cydaw::base::Array<cydaw::base::Value> commentValues;
  for (const auto &comment : comments_) {
    auto value = cydaw::base::Object::Ptr(new cydaw::base::Object());
    value->setProperty(jsonKeys::id, static_cast<std::int64_t>(comment.id));
    value->setProperty(jsonKeys::title, comment.title);
    value->setProperty(jsonKeys::description, comment.description);
    value->setProperty(jsonKeys::x, comment.position.x);
    value->setProperty(jsonKeys::y, comment.position.y);
    value->setProperty(jsonKeys::width, comment.size.x);
    value->setProperty(jsonKeys::height, comment.size.y);
    value->setProperty(jsonKeys::colour,
                       static_cast<std::int64_t>(comment.colour.getARGB()));
    commentValues.add(cydaw::base::Value(value.get()));
  }
  root->setProperty(jsonKeys::comments, commentValues);

  cydaw::base::Array<cydaw::base::Value> moduleValues;
  for (const auto &module : savedModules_) {
    // Nothing has touched this class since it was read, and it was read
    // from a document of this version, so its description still describes
    // it exactly. Rebuilding it would produce the same thing at a new
    // address, and that address is what lets a restore recognise it.
    // Only a factory class, which the editor rebuilds identically and
    // which the user cannot edit without it ceasing to be one. A class the
    // project owns is written out every time: variants and nested classes
    // are edited through paths that have no business knowing about a cache.
    if (module.factoryModule && module.source.getDynamicObject() != nullptr &&
        module.sourceSchema == nodeGraphStateSchema) {
      moduleValues.add(module.source);
      continue;
    }
    auto moduleValue = cydaw::base::Object::Ptr(new cydaw::base::Object());
    moduleValue->setProperty(jsonKeys::key, module.key);
    moduleValue->setProperty(jsonKeys::name, module.name);
    moduleValue->setProperty(jsonKeys::version,
                             cydaw::base::Text(module.version.toString()));
    moduleValue->setProperty(jsonKeys::parentClassKey, module.parentClassKey);
    moduleValue->setProperty("programs",
                             serialiseGraphPrograms(module.programs));
    moduleValue->setProperty(jsonKeys::description, module.description);
    moduleValue->setProperty(jsonKeys::category, module.category);
    moduleValue->setProperty(jsonKeys::author, module.author);
    moduleValue->setProperty(jsonKeys::hasInput, module.hasInput);
    moduleValue->setProperty(jsonKeys::inputKind,
                             static_cast<int>(module.inputKind));
    moduleValue->setProperty(jsonKeys::hasOutput, module.hasOutput);
    moduleValue->setProperty(jsonKeys::outputKind,
                             static_cast<int>(module.outputKind));
    moduleValue->setProperty(jsonKeys::inputChannels,
                             static_cast<int>(module.inputChannels));
    moduleValue->setProperty(jsonKeys::outputChannels,
                             static_cast<int>(module.outputChannels));
    moduleValue->setProperty(jsonKeys::factory, module.factoryModule);
    moduleValue->setProperty(jsonKeys::inline_, module.inlineModule);
    moduleValue->setProperty(jsonKeys::locked, module.locked);
    moduleValue->setProperty(jsonKeys::role,
                             cydaw::graph::toString(inferModuleRole(module)));
    cydaw::base::Array<cydaw::base::Value> moduleTags;
    for (const auto &tag : module.tags)
      moduleTags.add(tag);
    moduleValue->setProperty(jsonKeys::tags, moduleTags);
    cydaw::base::Array<cydaw::base::Value> children;
    for (const auto &child : module.nodes) {
      auto childValue = cydaw::base::Object::Ptr(new cydaw::base::Object());
      childValue->setProperty(jsonKeys::id,
                              static_cast<std::int64_t>(child.id));
      childValue->setProperty(jsonKeys::type, child.typeKey);
      childValue->setProperty(jsonKeys::title, child.title);
      childValue->setProperty(jsonKeys::alias, child.alias);
      childValue->setProperty(jsonKeys::description, child.description);
      childValue->setProperty(jsonKeys::inputChannels,
                              static_cast<int>(child.inputChannels));
      childValue->setProperty(jsonKeys::outputChannels,
                              static_cast<int>(child.outputChannels));
      childValue->setProperty(jsonKeys::x, child.position.x);
      childValue->setProperty(jsonKeys::y, child.position.y);
      childValue->setProperty(jsonKeys::parameters,
                              serialiseNativeParameters(child.parameters));
      childValue->setProperty(jsonKeys::portDefaults,
                              serialisePortDefaults(child.ports));
      childValue->setProperty(jsonKeys::interface,
                              serialiseModuleInterface(child.interfaceState));
      childValue->setProperty(jsonKeys::interfaceLayoutOverridden,
                              child.interfaceLayoutOverridden);
      childValue->setProperty(jsonKeys::collapsed, child.collapsed);
      writeCardAssets(*childValue, child.wavetableShape, child.soundModelJson,
                      child.samplerJson, child.prismTablesJson,
                      child.impulseJson, child.bypassed, child.instrumentJson,
                      child.branchNames);
      children.add(cydaw::base::Value(childValue.get()));
    }
    moduleValue->setProperty(jsonKeys::nodes, children);
    cydaw::base::Array<cydaw::base::Value> links;
    for (const auto &connection : module.connections) {
      auto link = cydaw::base::Object::Ptr(new cydaw::base::Object());
      link->setProperty(jsonKeys::source,
                        static_cast<std::int64_t>(connection.source));
      link->setProperty(jsonKeys::destination,
                        static_cast<std::int64_t>(connection.destination));
      link->setProperty(jsonKeys::kind, static_cast<int>(connection.kind));
      link->setProperty(jsonKeys::audioChannels,
                        static_cast<int>(connection.audioChannels));
      link->setProperty(jsonKeys::sourcePort,
                        static_cast<std::int64_t>(connection.sourcePort));
      link->setProperty(jsonKeys::destinationPort,
                        static_cast<std::int64_t>(connection.destinationPort));
      if (connection.mode != 0) {
        link->setProperty("mode", static_cast<int>(connection.mode));
        link->setProperty("amount", static_cast<double>(connection.amount));
      }
      links.add(cydaw::base::Value(link.get()));
    }
    moduleValue->setProperty(jsonKeys::connections, links);
    moduleValue->setProperty(jsonKeys::interface,
                             serialiseModuleInterface(module.interfaceState));
    // Remember what this class serialises to. A class the editor built
    // for itself has no description to hand back until it has been asked
    // for one once; from then on it is written back as it stands, and its
    // address stays put so a restore can recognise it.
    if (module.factoryModule) {
      module.source = cydaw::base::Value(moduleValue.get());
      module.sourceSchema = nodeGraphStateSchema;
    }
    moduleValues.add(cydaw::base::Value(moduleValue.get()));
  }
  root->setProperty(jsonKeys::modules, moduleValues);
  return cydaw::base::Value(root.get());
}
cydaw::engine::NativeDspGraph GraphDocument::nativeDspGraph() const {
  return withCardBypasses(exportedNativeGraph());
}
cydaw::engine::NativeDspGraph
GraphDocument::withCardBypasses(cydaw::engine::NativeDspGraph graph) const {
  if (std::none_of(nodes_.begin(), nodes_.end(),
                   [](const Node &node) { return node.bypassed; }))
    return graph;
  // A card switched off: its last node gives out what its first one
  // received; an instrument, silence. A module's other exits fall silent
  // (they would add the same sound again).
  const auto spans = nativeSpansOf(graph);
  const auto voice = instrumentVoiceBoundary();
  auto voiceSilenced = false;
  for (const auto &node : nodes_) {
    if (!node.bypassed)
      continue;
    // A container switched off: its end gives what its head received.
    if (editorIsContainer(node.id)) {
      if (const auto container = editorContainer(node.id);
          container && container->end != 0)
        graph.bypasses.push_back({container->end, node.id});
      continue;
    }
    // A card of the instrument's voice: the voice falls silent where it
    // enters the effects.
    if (voice.voiceNodes.contains(node.id)) {
      if (!voiceSilenced)
        graph.bypasses.push_back({voice.outputNodeId, 0});
      voiceSilenced = true;
      continue;
    }
    const auto span = spans.find(node.id);
    if (span == spans.end() || span->second.exits.empty())
      continue;
    const auto from = span->second.instrument || span->second.entries.empty()
                          ? std::uint64_t{}
                          : span->second.entries.front();
    graph.bypasses.push_back({span->second.exits.front(), from});
    for (std::size_t exit = 1; exit < span->second.exits.size(); ++exit)
      graph.bypasses.push_back({span->second.exits[exit], 0});
  }
  return graph;
}
std::map<std::uint64_t, GraphDocument::EditorNativeSpan>
GraphDocument::nativeSpansOf(
    const cydaw::engine::NativeDspGraph &native) const {
  using Kind = cydaw::engine::NativeDspNodeKind;
  std::map<std::uint64_t, EditorNativeSpan> spans;
  std::map<std::uint64_t, Kind> audioNodes;
  for (const auto &node : native.nodes)
    if (!isControlOnlyNativeKind(node.kind))
      audioNodes.emplace(node.id, node.kind);
  for (const auto &card : nodes_) {
    // The graph's own inputs and outputs are not devices; the ids the
    // compiler generates for them can even be a boundary card's.
    if (isBoundaryNode(card))
      continue;
    std::set<std::uint64_t> members;
    auto instrument = false;
    if (const auto found = audioNodes.find(card.id);
        found != audioNodes.end()) {
      if (found->second == Kind::output)
        continue;
      // An instrument stands in as the effect chain's input.
      instrument = found->second == Kind::input;
      members.insert(card.id);
    }
    if (exportCache_.has_value()) {
      const auto prefix =
          "node." + cydaw::base::Text(static_cast<std::int64_t>(card.id)) + ".";
      for (const auto &parameter : exportCache_->publishedParameters)
        if (cydaw::base::Text(parameter.key).startsWith(prefix) &&
            audioNodes.contains(parameter.targetNodeId) &&
            audioNodes.at(parameter.targetNodeId) != Kind::input &&
            audioNodes.at(parameter.targetNodeId) != Kind::output)
          members.insert(parameter.targetNodeId);
    }
    if (members.empty())
      continue;
    EditorNativeSpan span;
    span.instrument = instrument;
    for (const auto member : members) {
      auto fed = false;
      auto feeds = false;
      for (const auto &connection : native.connections) {
        if (connection.destination == member &&
            members.contains(connection.source))
          fed = true;
        if (connection.source == member &&
            members.contains(connection.destination))
          feeds = true;
      }
      if (!fed)
        span.entries.push_back(member);
      if (!feeds)
        span.exits.push_back(member);
    }
    // A module's side paths are passthroughs; its sound runs through
    // the processors, so those come first.
    const auto processorFirst = [&audioNodes](std::vector<std::uint64_t> &ids) {
      std::stable_partition(ids.begin(), ids.end(),
                            [&audioNodes](const std::uint64_t id) {
                              return audioNodes.at(id) != Kind::passthrough;
                            });
    };
    processorFirst(span.entries);
    processorFirst(span.exits);
    spans.emplace(card.id, std::move(span));
  }
  return spans;
}
cydaw::engine::NativeDspGraph GraphDocument::exportedNativeGraph() const {
  const auto signature = structuralSignature();
  if (exportCache_.has_value() && exportCacheSignature_ == signature) {
    auto graph = exportCache_->nativeGraph;
    if (patchNativeGraphParameters(graph)) {
      exportCache_->nativeGraph = graph;
      exportCacheProfile_ = "hit";
      return graph;
    }
    // patchNativeGraphParameters already named the node it refused.
  } else {
    exportCacheProfile_ = exportCache_.has_value() ? "signature" : "empty";
  }
  exportCache_ = runtimeGraphExport();
  exportCacheSignature_ = signature;
  ++exportRebuilds_;
  return exportCache_->nativeGraph;
}
std::optional<std::pair<std::uint64_t, cydaw::base::Text>>
GraphDocument::editorNativeNodeFor(const cydaw::base::Text &binding) const {
  // Two levels at least: a module's card, then a node inside it.
  if (!binding.startsWith("node."))
    return std::nullopt;
  const auto separator = binding.indexOfChar(5, '.');
  if (separator < 0 || !binding.substring(separator + 1).startsWith("node."))
    return std::nullopt;
  // The map comes with the export the chain plays. Its ids change only
  // with the graph's shape, and each change of shape is published (which
  // exports it): the last export answers, without the cost of another
  // (a graph with modules is exported whole on every call).
  if (!exportCache_.has_value())
    (void)nativeDspGraph();
  if (!exportCache_.has_value())
    return std::nullopt;
  const auto key = binding.toStdString();
  const auto card = static_cast<std::uint64_t>(
      binding.substring(5, separator).getLargeIntValue());
  const auto &native = exportCache_->nativeGraph;
  for (const auto &parameter : exportCache_->publishedParameters)
    // A parameter the compiler did not map still names the module's
    // card: no node of the engine, or the card standing in for an
    // instrument whose voices play elsewhere.
    if ((parameter.key == key || parameter.targetParameterKey == key) &&
        parameter.targetNodeId != card &&
        std::ranges::any_of(native.nodes, [&parameter](const auto &node) {
          return node.id == parameter.targetNodeId;
        }))
      return std::make_pair(static_cast<std::uint64_t>(parameter.targetNodeId),
                            innermostParameterSuffix(binding));
  return std::nullopt;
}
std::uint64_t GraphDocument::structuralSignature() const {
  // Everything the export's shape depends on and a knob cannot change:
  // which cards, how they are wired, which classes stand behind them, what
  // the graph publishes and what role it plays. Values are left out on
  // purpose; they are what the patch refreshes.
  auto hash = std::uint64_t{1469598103934665603ull};
  const auto mix = [&hash](const std::uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ull;
  };
  mix(static_cast<std::uint64_t>(graphRole_));
  mix(nodes_.size());
  for (const auto &node : nodes_) {
    mix(node.id);
    mix(static_cast<std::uint64_t>(node.typeKey.hashCode64()));
    mix(node.ports.size());
    mix(node.inputChannels);
    mix(node.outputChannels);
  }
  mix(connections_.size());
  for (const auto &connection : connections_) {
    mix(connection.source);
    mix(connection.destination);
    mix(static_cast<std::uint64_t>(connection.kind));
    mix(connection.sourcePort);
    mix(connection.destinationPort);
    // A control cable's set/modulate becomes the compiled modulation
    // itself: a cable switched to modulate after it was made stayed
    // "set". Its depth is not shape: the patch refreshes it, the engine
    // takes it live (a ring turned while it plays).
    mix(static_cast<std::uint64_t>(connection.mode));
  }
  mix(savedModules_.size());
  for (const auto &module : savedModules_) {
    mix(static_cast<std::uint64_t>(module.key.hashCode64()));
    mix(module.nodes.size());
    mix(module.connections.size());
    // A card switched off inside compiles to a way through: a device
    // whose only card it is has no node of its own left to patch.
    for (const auto &child : module.nodes)
      mix(child.bypassed ? 1u : 0u);
  }
  mix(graphInterfaceState_.parameters.size());
  for (const auto &parameter : graphInterfaceState_.parameters)
    mix(static_cast<std::uint64_t>(parameter.key.hashCode64()));
  return hash;
}
bool GraphDocument::patchNativeGraphParameters(
    cydaw::engine::NativeDspGraph &graph) const {
  using Kind = cydaw::engine::NativeDspNodeKind;
  for (auto &native : graph.nodes) {
    // Boundary nodes carry no parameters, and their ids are either a
    // card's without standing for it or generated past the cards — the
    // output stage the master card becomes is one of the latter. A
    // generated node of a real DSP kind is a module child, and refuses.
    if (native.kind == Kind::input || native.kind == Kind::output ||
        native.kind == Kind::passthrough)
      continue;
    const auto *node = readNode(native.id);
    if (node == nullptr) {
      exportCacheProfile_ =
          "no-card:" + cydaw::base::Text(static_cast<std::int64_t>(native.id)) +
          ":kind=" + cydaw::base::Text(static_cast<int>(native.kind));
      return false;
    }
    // Not the card's whole payload: the typed compiler derives fields the
    // card never holds — a port's range, for one — and copying over them
    // would hand the engine a different graph from a rebuild. Only what a
    // knob can change is written, through the same writer a knob uses.
    for (const auto &parameter : node->interfaceState.parameters)
      (void)setNativeParameter(
          native.parameters, parameter.key,
          nativeParameterValue(node->parameters, parameter.key));
    // The compiler reads a card's first two value sockets into the
    // payload's value inputs. A socket that follows its knob — the
    // drive card's — moved at the notch, so they are read again here,
    // or the patched export would differ from a rebuilt one.
    auto valueInputIndex = 0;
    for (const auto &port : node->ports) {
      if (!port.input || (port.type != cydaw::graph::PortType::value &&
                          port.type != cydaw::graph::PortType::trigger))
        continue;
      if (valueInputIndex == 0)
        native.parameters.valueInputA = static_cast<float>(port.defaultValue);
      else if (valueInputIndex == 1)
        native.parameters.valueInputB = static_cast<float>(port.defaultValue);
      ++valueInputIndex;
    }
    native.wavetable = node->wavetableBank;
    native.impulse = node->impulse;
  }
  // The cables' depths, each from the editor's cable it was compiled
  // from (same source and sockets; the same card first, a module's inner
  // node otherwise).
  std::vector<bool> used(connections_.size());
  for (auto &modulation : graph.modulations) {
    auto best = connections_.size();
    for (std::size_t index = 0; index < connections_.size(); ++index) {
      const auto &connection = connections_[index];
      if (used[index] || connection.kind != PortKind::control ||
          connection.source != modulation.source ||
          connection.sourcePort != modulation.sourcePort ||
          connection.destinationPort != modulation.destinationPort)
        continue;
      best = index;
      if (connection.destination == modulation.destination)
        break;
    }
    if (best < connections_.size()) {
      used[best] = true;
      modulation.amount = connections_[best].amount;
    }
  }
  return true;
}
GraphDocument::RuntimeGraphExport GraphDocument::runtimeGraphExport() const {
  auto exportMark = cydaw::base::Time::getHighResolutionTicks();
  const auto exportSince = [&exportMark] {
    const auto now = cydaw::base::Time::getHighResolutionTicks();
    const auto ms =
        cydaw::base::Time::highResolutionTicksToSeconds(now - exportMark) *
        1000.0;
    exportMark = now;
    return cydaw::base::Text(ms, 2);
  };
  std::vector<NativeParameterTarget> targets;
  std::vector<cydaw::engine::CompiledGraphParameterTarget> typedTargets;
  RuntimeGraphExport result;
  auto definitions = graphDefinitions();
  // Four fields of the root definition are read again after the library has
  // taken the batch. Copying the whole definition to keep them cost as much
  // as everything else here put together, because the root carries the
  // entire graph.
  cydaw::graph::GraphReference rootReference;
  auto rootRole = cydaw::graph::GraphRole::generic;
  auto rootHasAudioOutput = false;
  std::vector<cydaw::graph::PublishedParameter> rootParameters;
  if (!definitions.empty()) {
    const auto &root = definitions.back();
    rootReference = root.reference;
    rootRole = root.role;
    rootHasAudioOutput = std::ranges::any_of(root.ports, [](const auto &port) {
      return port.type == cydaw::graph::PortType::audio &&
             port.direction == cydaw::graph::PortDirection::output;
    });
    rootParameters = root.parameters;
  }
  const auto exportDefinitions = exportSince();
  result.compilationResult = compilePolySynthGraph(result.instrumentPlan);
  const auto exportPoly = exportSince();
  if (result.compilationResult.wasOk() && result.instrumentPlan.enabled) {
    // The polyphonic renderer owns the MIDI -> audio portion. Represent
    // that top-level instrument module as the native graph input, then
    // compile every audio node downstream from it. This keeps the
    // oscillator/envelope from being rendered twice while preserving the
    // user-authored post-instrument effect chain.
    // A modern instrument is five separate nodes, none of which is
    // shaped MIDI in / audio out, so the search below found nothing and
    // the chain behind it fell through to a passthrough. Ask the voice
    // section where it ends instead; the single-node poly-synth of older
    // projects still answers the original question.
    const auto boundary = instrumentVoiceBoundary();
    if (boundary.outputNodeId != 0) {
      result.nativeGraph = buildNativeDspGraph(&targets, boundary.outputNodeId,
                                               boundary.voiceNodes);
    } else {
      const auto instrument =
          std::find_if(nodes_.begin(), nodes_.end(), [](const Node &node) {
            return node.hasInput && node.inputKind == PortKind::midi &&
                   node.hasOutput && node.outputKind == PortKind::audio;
          });
      result.nativeGraph = instrument != nodes_.end()
                               ? buildNativeDspGraph(&targets, instrument->id)
                               : makePassthroughAudioGraph();
    }
  } else {
    bool compiledTypedGraph = false;
    const auto reportsAudioCompilation =
        rootRole != cydaw::graph::GraphRole::instrument && rootHasAudioOutput;
    const auto reportTypedFailure =
        [&result, reportsAudioCompilation](const auto &validation) {
          if (!reportsAudioCompilation || validation.ok())
            return;
          const auto message =
              validation.issues.empty()
                  ? cydaw::base::Text("Typed Graph compilation failed")
                  : cydaw::base::Text(validation.issues.front().message);
          result.compilationResult = cydaw::base::Result::fail(message);
        };
    if (!definitions.empty()) {
      cydaw::graph::GraphLibrary library;
      const auto installed = library.installBatch(std::move(definitions));
      if (installed.ok()) {
        auto compiled = cydaw::engine::compileGraphDefinitionToNativeDsp(
            library, rootReference);
        if (compiled.ok()) {
          result.nativeGraph = std::move(compiled.graph);
          // An instrument track's: its audio inputs come after
          // the instrument's sound.
          if (instrumentIsMainInput_)
            for (auto &node : result.nativeGraph.nodes)
              if (node.kind == cydaw::engine::NativeDspNodeKind::input)
                ++node.inputBus;
          typedTargets = std::move(compiled.parameterTargets);
          result.controlTelemetryTargets =
              std::move(compiled.controlTelemetryTargets);
          compiledTypedGraph = true;
        } else {
          reportTypedFailure(compiled.validation);
        }
      } else {
        reportTypedFailure(installed);
      }
    }
    // Keep project playback recoverable while legacy or third-party
    // primitives are migrated. Supported core.* nodes never enter this
    // fallback because the typed compiler resolves them above.
    if (!compiledTypedGraph)
      result.nativeGraph = buildNativeDspGraph(&targets);
  }
  const auto exportNative = exportSince();
  result.publishedParameters = std::move(rootParameters);
  for (auto &parameter : result.publishedParameters) {
    const auto typedTarget = std::ranges::find(
        typedTargets, parameter.id,
        &cydaw::engine::CompiledGraphParameterTarget::graphParameterId);
    if (typedTarget != typedTargets.end()) {
      parameter.targetNodeId = typedTarget->nativeNodeId;
      // Keep the authored/public binding path for host automation and
      // diagnostics. Only the node identity is flattened to the native
      // DSP node; the public key still describes where the parameter
      // came from inside nested reusable Graph instances.
      parameter.targetParameterKey = typedTarget->graphParameterKey;
      continue;
    }
    const auto target = std::find_if(
        targets.begin(), targets.end(), [&parameter](const auto &candidate) {
          return candidate.instanceNodeId == parameter.targetNodeId &&
                 candidate.parameterKey ==
                     cydaw::base::Text(parameter.targetParameterKey);
        });
    if (target != targets.end())
      parameter.targetNodeId = target->nativeNodeId;
  }
  exportProfile_ = "definitions=" + exportDefinitions + " poly=" + exportPoly +
                   " native=" + exportNative + " mapping=" + exportSince();
  return result;
}
GraphDocument::InstrumentBoundary
GraphDocument::instrumentVoiceBoundary() const {
  // Mirrors what the instrument compiler consumes, and it takes the first
  // node of each kind. It requires all five, so a graph missing any one of
  // them has no voice section to speak of and no boundary to cut at.
  InstrumentBoundary boundary;
  const auto firstOf = [this](const auto &matches) -> const Node * {
    const auto found = std::find_if(
        nodes_.begin(), nodes_.end(), [&matches](const Node &node) {
          return matches(primitiveProcessorKeyForType(node.typeKey));
        });
    return found != nodes_.end() ? &*found : nullptr;
  };
  const auto is = [](const char *key) {
    return
        [key](const cydaw::base::Text &candidate) { return candidate == key; };
  };
  const auto *allocator = firstOf(is("core.voice-allocator"));
  const auto *source = firstOf([](const cydaw::base::Text &key) {
    return cydaw::graph::isPolyphonicVoiceSource(key.toStdString());
  });
  const auto *envelope = firstOf(is("core.adsr"));
  const auto *amplifier = firstOf(is("core.gain"));
  const auto *filter = firstOf(is("core.lowpass"));
  if (allocator == nullptr || source == nullptr || envelope == nullptr ||
      amplifier == nullptr || filter == nullptr)
    return boundary;
  for (const auto *node : {allocator, source, envelope, amplifier, filter})
    boundary.voiceNodes.insert(node->id);
  // The voice leaves by its filter: that is the last stage the renderer
  // owns, so it stands in as the chain's input.
  boundary.outputNodeId = filter->id;
  return boundary;
}
cydaw::engine::NativeDspGraph GraphDocument::buildNativeDspGraph(
    std::vector<NativeParameterTarget> *parameterTargets,
    const NodeId renderedInstrumentNodeId,
    const std::set<NodeId> &renderedVoiceNodes) const {
  using Kind = cydaw::engine::NativeDspNodeKind;
  cydaw::engine::NativeDspGraph graph;
  const auto kindForType = [](const cydaw::base::Text &typeKey) {
    if (typeKey == "audio-clips" || typeKey.startsWith("graph-audio-input-"))
      return Kind::input;
    if (typeKey == "master" || typeKey.startsWith("graph-audio-output-"))
      return Kind::output;
    if (typeKey == "drive")
      return Kind::drive;
    if (typeKey == "gain")
      return Kind::gain;
    if (typeKey == "lowpass")
      return Kind::lowpass;
    if (typeKey == "eq-band")
      return Kind::peakingEq;
    if (typeKey == "wavetable")
      return Kind::wavetable;
    if (typeKey == "string")
      return Kind::stringResonator;
    if (typeKey == "highpass")
      return Kind::highpass;
    if (typeKey == "hard-clip")
      return Kind::hardClip;
    if (typeKey == "stereo-width")
      return Kind::stereoWidth;
    if (typeKey == "gain-pan")
      return Kind::pan;
    if (typeKey == "dc-blocker")
      return Kind::dcBlocker;
    if (typeKey == "oscillator")
      return Kind::oscillator;
    if (typeKey == "noise")
      return Kind::noise;
    if (typeKey == "delay")
      return Kind::delay;
    if (typeKey == "reverb")
      return Kind::reverb;
    if (typeKey == "compressor")
      return Kind::compressor;
    if (typeKey == "eq")
      return Kind::equalizer;
    if (typeKey == "spectrum" || typeKey == "oscilloscope" ||
        typeKey == "level-meter")
      return Kind::analyzer;
    if (typeKey == "reverb-rack")
      return Kind::reverbRack;
    if (typeKey == "vocal-strip")
      return Kind::vocalStrip;
    if (typeKey == "mixer-4")
      return Kind::mixer;
    if (typeKey == "container-end")
      return Kind::containerEnd;
    // A MIDI effect has no sound to process: the instrument's plan
    // carries it (PolySynthGraphPlan::midiEffects).
    if (const auto *effect =
            cydaw::engine::effectForType(typeKey.toStdString()))
      return effect->midi ? Kind::passthrough : effect->kind;
    return Kind::passthrough;
  };

  struct Expansion {
    std::vector<NodeId> entries;
    std::vector<NodeId> exits;
    std::vector<std::pair<cydaw::base::Text, NodeId>> parameterTargets;
  };
  NodeId generatedId = nextNodeId_;
  for (const auto &node : nodes_)
    generatedId = std::max(generatedId, node.id + 1);

  const auto connectExpansions =
      [&graph](const Expansion &source, const Expansion &destination,
               const cydaw::graph::PortId sourcePort = 0,
               const cydaw::graph::PortId destinationPort = 0) {
        for (const auto sourceId : source.exits)
          for (const auto destinationId : destination.entries)
            graph.connections.push_back(
                {sourceId, destinationId, sourcePort, destinationPort});
      };
  const auto addPassthrough = [&graph, &generatedId]() {
    const auto id = generatedId++;
    graph.nodes.push_back({id, Kind::passthrough, {}});
    return Expansion{{id}, {id}, {}};
  };

  std::function<Expansion(const SavedModule &, cydaw::base::TextList &)>
      expandModule;
  expandModule = [this, &graph, &generatedId, &kindForType, &connectExpansions,
                  &addPassthrough, &expandModule](
                     const SavedModule &module,
                     cydaw::base::TextList &expansionStack) -> Expansion {
    // A cycle, or devices inside devices past what any real graph holds
    // (a crafted file multiplying them): passed through.
    if (expansionStack.contains(module.key) || graph.nodes.size() >= 65'536)
      return addPassthrough();
    expansionStack.add(module.key);

    std::unordered_map<NodeId, Expansion> expandedChildren;
    std::set<NodeId> audioChildren;
    for (const auto &child : module.nodes) {
      const auto *type = findType(child.typeKey);
      if (type == nullptr)
        continue;
      const auto hasAudioInput =
          type->hasInput && type->inputKind == PortKind::audio;
      const auto hasAudioOutput =
          type->hasOutput && type->outputKind == PortKind::audio;
      if (!hasAudioInput && !hasAudioOutput)
        continue;
      audioChildren.insert(child.id);
      // Switched off inside the device: its sound goes through as is.
      if (child.bypassed && hasAudioInput && hasAudioOutput) {
        expandedChildren.emplace(child.id, addPassthrough());
        continue;
      }
      if (const auto *nested = findModule(child.typeKey)) {
        auto nestedExpansion = expandModule(*nested, expansionStack);
        const auto prefix = nodeParameterBinding(child.id, {});
        for (auto &[key, ignored] : nestedExpansion.parameterTargets) {
          base::ignoreUnused(ignored);
          key = prefix + key;
        }
        expandedChildren.emplace(child.id, std::move(nestedExpansion));
      } else {
        const auto id = generatedId++;
        // A module's own terminals pass its signal through. Compiled
        // as the chain's input and output they fed the track's input
        // in a second time and added a second copy to its output.
        auto kind = kindForType(child.typeKey);
        if (kind == Kind::input || kind == Kind::output)
          kind = Kind::passthrough;
        graph.nodes.push_back({id, kind, child.parameters});
        graph.nodes.back().wavetable = child.wavetableBank;
        graph.nodes.back().impulse = child.impulse;
        Expansion expansion{{id}, {id}, {}};
        ModuleInterfaceState published;
        appendPublishedParameters(published, child.typeKey, child.id,
                                  child.parameters);
        for (const auto &parameter : published.parameters)
          expansion.parameterTargets.emplace_back(parameter.key, id);
        expandedChildren.emplace(child.id, std::move(expansion));
      }
    }

    std::set<NodeId> hasIncoming;
    std::set<NodeId> hasOutgoing;
    for (const auto &connection : module.connections) {
      if (connection.kind != PortKind::audio ||
          !audioChildren.contains(connection.source) ||
          !audioChildren.contains(connection.destination))
        continue;
      const auto source = expandedChildren.find(connection.source);
      const auto destination = expandedChildren.find(connection.destination);
      if (source == expandedChildren.end() ||
          destination == expandedChildren.end())
        continue;
      connectExpansions(source->second, destination->second,
                        connection.sourcePort, connection.destinationPort);
      hasOutgoing.insert(connection.source);
      hasIncoming.insert(connection.destination);
    }

    Expansion boundary;
    for (const auto childId : audioChildren) {
      const auto child = expandedChildren.find(childId);
      if (child == expandedChildren.end())
        continue;
      if (!hasIncoming.contains(childId))
        boundary.entries.insert(boundary.entries.end(),
                                child->second.entries.begin(),
                                child->second.entries.end());
      if (!hasOutgoing.contains(childId))
        boundary.exits.insert(boundary.exits.end(), child->second.exits.begin(),
                              child->second.exits.end());
      boundary.parameterTargets.insert(boundary.parameterTargets.end(),
                                       child->second.parameterTargets.begin(),
                                       child->second.parameterTargets.end());
    }
    expansionStack.removeString(module.key);
    if (boundary.entries.empty() || boundary.exits.empty())
      return addPassthrough();
    return boundary;
  };

  std::unordered_map<NodeId, Expansion> expandedNodes;
  for (const auto &node : nodes_) {
    if (node.id == renderedInstrumentNodeId) {
      graph.nodes.push_back({node.id, Kind::input, {}});
      expandedNodes.emplace(node.id, Expansion{{node.id}, {node.id}, {}});
      continue;
    }
    // The rest of the voice section is rendered per note by the
    // polyphonic renderer. Compiling it here as well would run the
    // oscillator and its envelope a second time, on the summed output.
    if (renderedVoiceNodes.contains(node.id))
      continue;
    const auto hasAudioInput =
        node.hasInput && node.inputKind == PortKind::audio;
    const auto hasAudioOutput =
        node.hasOutput && node.outputKind == PortKind::audio;
    if (!hasAudioInput && !hasAudioOutput) {
      // Control nodes carry no audio and were skipped along with the
      // MIDI ones: on an instrument's chain an LFO dropped on a knob
      // was drawn, saved, and compiled nowhere. They join the graph
      // as themselves; their cables become modulations below.
      const auto processorKey =
          primitiveProcessorKeyForType(node.typeKey).toStdString();
      const auto kind =
          cydaw::engine::nativeDspControlKindForProcessor(processorKey);
      if (!kind.has_value() || !isControlOnlyNativeKind(*kind))
        continue;
      graph.nodes.push_back({node.id, *kind, node.parameters});
      expandedNodes.emplace(node.id, Expansion{{node.id}, {node.id}, {}});
      if (parameterTargets != nullptr)
        for (const auto &parameter : node.interfaceState.parameters)
          parameterTargets->push_back({node.id, parameter.key, node.id});
      continue;
    }
    if (const auto *module = findModule(node.typeKey)) {
      cydaw::base::TextList expansionStack;
      auto expansion = expandModule(*module, expansionStack);
      // The children were built with the class's values; the knobs
      // turned on this instance go over them, as the typed compiler
      // applies its overrides. Without this a module in an instrument's
      // chain played its defaults whatever its knobs said.
      const auto prefix = nodeParameterBinding(node.id, {});
      for (const auto &parameter : node.interfaceState.parameters) {
        if (!parameter.key.startsWith(prefix))
          continue;
        const auto classKey = parameter.key.substring(prefix.length());
        for (const auto &[key, nativeNodeId] : expansion.parameterTargets)
          if (key == classKey)
            for (auto &built : graph.nodes)
              if (built.id == nativeNodeId)
                (void)setNativeParameter(
                    built.parameters,
                    nodeParameterBinding(0, innermostParameterSuffix(classKey)),
                    parameter.value);
      }
      if (parameterTargets != nullptr)
        for (const auto &[key, nativeNodeId] : expansion.parameterTargets)
          parameterTargets->push_back({node.id, key, nativeNodeId});
      expandedNodes.emplace(node.id, std::move(expansion));
    } else {
      graph.nodes.push_back({node.id, kindForType(node.typeKey),
                             node.parameters, node.wavetableBank});
      graph.nodes.back().impulse = node.impulse;
      expandedNodes.emplace(node.id, Expansion{{node.id}, {node.id}, {}});
      if (parameterTargets != nullptr)
        for (const auto &parameter : node.interfaceState.parameters)
          parameterTargets->push_back({node.id, parameter.key, node.id});
    }
    if (node.typeKey == "master" ||
        node.typeKey.startsWith("graph-audio-output-"))
      graph.outputNodeId = node.id;
  }
  // Control cables. The source has to be a control node the graph holds
  // — a voice node's cable has nowhere to go here — and the destination is
  // either another control node or an audio node's value port, named by
  // the parameter that port sets, resolved the way the typed compiler
  // resolves it.
  for (const auto &connection : connections_) {
    if (connection.kind != PortKind::control)
      continue;
    const auto *source = readNode(connection.source);
    const auto *destination = readNode(connection.destination);
    if (source == nullptr || destination == nullptr ||
        !expandedNodes.contains(connection.source) ||
        !expandedNodes.contains(connection.destination))
      continue;
    // The instrument stands here only as the chain's input: its voice
    // is rendered per note, where no cable reaches. A cable to it made
    // the whole chain fail to compile, the effects behind it with it.
    if (connection.destination == renderedInstrumentNodeId ||
        renderedVoiceNodes.contains(connection.destination))
      continue;
    const auto kindOf = [](const Node &node) {
      return cydaw::engine::nativeDspControlKindForProcessor(
          primitiveProcessorKeyForType(node.typeKey).toStdString());
    };
    const auto sourceKind = kindOf(*source);
    if (!sourceKind.has_value() || !isControlOnlyNativeKind(*sourceKind))
      continue;
    std::string parameterKey;
    auto target = connection.destination;
    auto range = std::pair{0.0, 1.0};
    const auto destinationKind = kindOf(*destination);
    if (!destinationKind.has_value() ||
        !isControlOnlyNativeKind(*destinationKind)) {
      const auto *port = findPort(*destination, connection.destinationPort);
      if (port == nullptr)
        continue;
      if (port->key.startsWith("knob.")) {
        // A device's knob socket: the parameter of the node inside
        // that carries the knob.
        const auto classKey = port->key.substring(5);
        const auto &targets =
            expandedNodes.at(connection.destination).parameterTargets;
        const auto found =
            std::ranges::find_if(targets, [&classKey](const auto &entry) {
              return entry.first == classKey;
            });
        if (found == targets.end())
          continue;
        target = found->second;
        parameterKey =
            primitiveParameterKeyForBinding(innermostParameterSuffix(classKey))
                .toStdString();
        range = knobRange(destination->typeKey, classKey);
      } else {
        const auto processor =
            primitiveProcessorKeyForType(destination->typeKey).toStdString();
        parameterKey = std::string(cydaw::engine::nativeDspParameterKeyForPort(
            processor, port->key.toStdString()));
        range = primitiveParameterRange(processor, parameterKey);
      }
      if (parameterKey.empty())
        continue;
    }
    cydaw::engine::NativeDspModulationConnection modulation;
    modulation.source = connection.source;
    modulation.destination = target;
    modulation.sourcePort = connection.sourcePort;
    modulation.destinationPort = connection.destinationPort;
    modulation.destinationParameterKey = parameterKey;
    if (!parameterKey.empty()) {
      modulation.mode = connection.mode;
      modulation.amount = connection.amount;
      modulation.minimum = static_cast<float>(range.first);
      modulation.maximum = static_cast<float>(range.second);
    }
    graph.modulations.push_back(std::move(modulation));
  }
  for (const auto &connection : connections_) {
    if (connection.kind != PortKind::audio)
      continue;
    const auto source = expandedNodes.find(connection.source);
    const auto destination = expandedNodes.find(connection.destination);
    if (source != expandedNodes.end() && destination != expandedNodes.end())
      connectExpansions(source->second, destination->second,
                        connection.sourcePort, connection.destinationPort);
  }
  // Its audio inputs: the main one (the instrument, else the first), then the
  // others a track is routed into (a sidechain), numbered in the graph's order.
  for (const auto &[id, bus] : audioInputBuses(renderedInstrumentNodeId))
    for (auto &node : graph.nodes)
      if (node.id == id && node.kind == Kind::input)
        node.inputBus = bus;
  return graph;
}
std::vector<std::pair<std::uint64_t, std::uint32_t>>
GraphDocument::audioInputBuses(
    const std::uint64_t renderedInstrumentNodeId) const {
  const auto isInput = [](const Node &node) {
    auto input = false;
    return node.typeKey == "audio-clips" ||
           (isGraphAudioBoundaryKey(node.typeKey, &input) && input);
  };
  // The first, as the typed compiler numbers the graph's ports.
  auto main = renderedInstrumentNodeId;
  if (main == 0 && !instrumentIsMainInput_)
    for (const auto &node : nodes_)
      if (isInput(node)) {
        main = node.id;
        break;
      }
  std::vector<std::pair<std::uint64_t, std::uint32_t>> buses;
  std::uint32_t next{};
  for (const auto &node : nodes_)
    if (node.id != main && isInput(node))
      buses.emplace_back(node.id, ++next);
  return buses;
}
cydaw::base::Result GraphDocument::compilePolySynthGraph(
    cydaw::engine::PolySynthGraphPlan &destination) const {
  // The versioned Graph contract is the primary instrument compiler. The
  // legacy UI-node traversal below remains only as a migration path for old
  // projects containing the pre-Graph `poly-synth` device.
  auto polyMark = cydaw::base::Time::getHighResolutionTicks();
  const auto polySince = [&polyMark] {
    const auto now = cydaw::base::Time::getHighResolutionTicks();
    const auto ms =
        cydaw::base::Time::highResolutionTicksToSeconds(now - polyMark) *
        1000.0;
    polyMark = now;
    return cydaw::base::Text(ms, 2);
  };
  const auto typedDefinitions = graphDefinitions();
  const auto profileDefinitions = polySince();
  if (!typedDefinitions.empty() &&
      typedDefinitions.back().role == cydaw::graph::GraphRole::instrument) {
    // Into the library kept from the last compile. A definition the
    // library already holds as it stands is left with everything the
    // library worked out about it; only what changed — the root, on a
    // knob notch — is installed and validated again. Classes the graph
    // no longer has are dropped. Modules come before the root in the
    // batch, so the root's nested references resolve as it goes in.
    auto installed = true;
    std::unordered_set<std::string> present;
    for (const auto &definition : typedDefinitions) {
      present.insert(definition.reference.toString());
      if (!instrumentLibrary_.holds(definition))
        installed =
            instrumentLibrary_.install(definition, true).ok() && installed;
    }
    for (const auto &reference : instrumentLibrary_.all())
      if (!present.contains(reference.toString()))
        (void)instrumentLibrary_.remove(reference);
    if (installed) {
      const auto profileInstall = polySince();
      const auto typed = cydaw::engine::compileGraphInstrument(
          instrumentLibrary_, typedDefinitions.back().reference);
      polyCompileProfile_ = "definitions=" + profileDefinitions +
                            " install=" + profileInstall +
                            " compile=" + polySince();
      if (typed.ok()) {
        destination = typed.plan;
        // A Graph definition carries named numbers, and a table is
        // neither. The card holds it, so it is fixed up here.
        destination.wavetable = voiceWavetableBank();
        destination.soundModel = voiceSoundModel();
        attachVoiceSampler(destination);
        if (destination.waveform ==
            cydaw::engine::PolyOscillatorWaveform::prism)
          destination.prismTables = cydaw::engine::prism::resolveTables(
              destination.prismValues, voicePrismTables());
        return cydaw::base::Result::ok();
      }
    }
  }

  struct FlatNode {
    NodeId id{};
    cydaw::base::Text typeKey;
    cydaw::engine::NativeDspNodeParameters parameters;
  };
  struct Expansion {
    std::vector<NodeId> entries;
    std::vector<NodeId> exits;
  };

  std::vector<FlatNode> flatNodes;
  std::vector<Connection> flatConnections;
  NodeId generatedId = nextNodeId_;
  for (const auto &node : nodes_)
    generatedId = std::max(generatedId, node.id + 1);

  const auto connect = [&flatConnections](const Expansion &source,
                                          const Expansion &destination,
                                          const PortKind kind) {
    for (const auto sourceId : source.exits)
      for (const auto destinationId : destination.entries)
        flatConnections.push_back({sourceId, destinationId, kind});
  };
  std::function<Expansion(const SavedModule &, cydaw::base::TextList &)>
      expandModule;
  expandModule = [this, &flatNodes, &flatConnections, &generatedId, &connect,
                  &expandModule](const SavedModule &module,
                                 cydaw::base::TextList &stack) -> Expansion {
    if (stack.contains(module.key) || flatNodes.size() >= 65'536)
      return {};
    stack.add(module.key);
    std::unordered_map<NodeId, Expansion> children;
    std::set<NodeId> incoming;
    std::set<NodeId> outgoing;
    for (const auto &child : module.nodes) {
      if (const auto *nested = findModule(child.typeKey)) {
        children.emplace(child.id, expandModule(*nested, stack));
      } else {
        const auto id = generatedId++;
        flatNodes.push_back({id, child.typeKey, child.parameters});
        children.emplace(child.id, Expansion{{id}, {id}});
      }
    }
    for (const auto &connection : module.connections) {
      const auto source = children.find(connection.source);
      const auto destination = children.find(connection.destination);
      if (source == children.end() || destination == children.end())
        continue;
      connect(source->second, destination->second, connection.kind);
      outgoing.insert(connection.source);
      incoming.insert(connection.destination);
    }
    Expansion boundary;
    for (const auto &child : module.nodes) {
      const auto expanded = children.find(child.id);
      if (expanded == children.end())
        continue;
      bool audioInput = false;
      const auto explicitAudio =
          isGraphAudioBoundaryKey(child.typeKey, &audioInput);
      const auto publishedInput = (explicitAudio && audioInput) ||
                                  (child.typeKey.startsWith("graph-") &&
                                   child.typeKey.endsWith("-input"));
      const auto publishedOutput = (explicitAudio && !audioInput) ||
                                   (child.typeKey.startsWith("graph-") &&
                                    child.typeKey.endsWith("-output"));
      if (publishedInput || !incoming.contains(child.id))
        boundary.entries.insert(boundary.entries.end(),
                                expanded->second.entries.begin(),
                                expanded->second.entries.end());
      if (publishedOutput || !outgoing.contains(child.id))
        boundary.exits.insert(boundary.exits.end(),
                              expanded->second.exits.begin(),
                              expanded->second.exits.end());
    }
    stack.removeString(module.key);
    return boundary;
  };

  std::unordered_map<NodeId, Expansion> expandedTopLevel;
  for (const auto &node : nodes_) {
    if (const auto *module = findModule(node.typeKey)) {
      cydaw::base::TextList stack;
      expandedTopLevel.emplace(node.id, expandModule(*module, stack));
    } else {
      flatNodes.push_back({node.id, node.typeKey, node.parameters});
      expandedTopLevel.emplace(node.id, Expansion{{node.id}, {node.id}});
    }
  }
  for (const auto &connection : connections_) {
    const auto source = expandedTopLevel.find(connection.source);
    const auto destinationExpansion =
        expandedTopLevel.find(connection.destination);
    if (source != expandedTopLevel.end() &&
        destinationExpansion != expandedTopLevel.end())
      connect(source->second, destinationExpansion->second, connection.kind);
  }

  const auto firstNodeOfType =
      [&flatNodes](const cydaw::base::Text &typeKey) -> const FlatNode * {
    const auto found = std::find_if(
        flatNodes.begin(), flatNodes.end(),
        [&typeKey](const FlatNode &node) { return node.typeKey == typeKey; });
    return found != flatNodes.end() ? &*found : nullptr;
  };
  const auto reaches = [&flatConnections](const NodeId source,
                                          const NodeId destination) {
    std::vector<NodeId> pending{source};
    std::set<NodeId> visited;
    while (!pending.empty()) {
      const auto current = pending.back();
      pending.pop_back();
      if (current == destination)
        return true;
      if (!visited.insert(current).second)
        continue;
      for (const auto &connection : flatConnections)
        if (connection.source == current)
          pending.push_back(connection.destination);
    }
    return false;
  };

  const auto *midiInput = firstNodeOfType("midi-input");
  if (midiInput == nullptr)
    midiInput = firstNodeOfType("graph-midi-input");
  const auto *allocator = firstNodeOfType("voice-allocator");
  // Whichever sound source the voice is built around. The editor speaks in
  // type keys, so it translates to the processor key the shared description
  // is written in.
  const auto firstSourceOfKind =
      [&flatNodes](
          bool (*matches)(std::string_view) noexcept) -> const FlatNode * {
    const auto found = std::find_if(
        flatNodes.begin(), flatNodes.end(), [matches](const FlatNode &node) {
          return matches(
              detail::primitiveProcessorKeyForType(node.typeKey).toStdString());
        });
    return found == flatNodes.end() ? nullptr : &*found;
  };
  const auto *oscillator =
      firstSourceOfKind(cydaw::graph::isPolyphonicVoiceSource);
  const auto *wavetable = firstNodeOfType("wavetable");
  const auto *stringVoice = firstNodeOfType("string");
  const auto *modelVoice = firstNodeOfType("sound-model");
  const auto *drumVoice = firstNodeOfType("batterie");
  const auto *samplerVoice =
      drumVoice != nullptr ? drumVoice : firstNodeOfType("sampler");
  const auto *prismVoice = firstNodeOfType("prism-synth");
  const auto *kickVoice = firstNodeOfType("kick-synth");
  const auto familyModel = oscillator != nullptr
                               ? cydaw::engine::synth::modelForType(
                                     oscillator->typeKey.toStdString())
                               : cydaw::engine::synth::Model::none;
  const auto *envelope = firstNodeOfType("envelope");
  const auto *amplifier = firstNodeOfType("gain");
  const auto *filter = firstNodeOfType("voice-filter");
  const auto *output = firstNodeOfType("master");
  if (output == nullptr) {
    const auto found = std::find_if(
        flatNodes.begin(), flatNodes.end(), [](const FlatNode &node) {
          return node.typeKey.startsWith("graph-audio-output-");
        });
    output = found != flatNodes.end() ? &*found : nullptr;
  }

  // Graphs saved before the polyphonic primitives existed remain playable.
  if (const auto *legacy = firstNodeOfType("poly-synth");
      allocator == nullptr && oscillator == nullptr && legacy != nullptr &&
      midiInput != nullptr && output != nullptr &&
      reaches(midiInput->id, legacy->id) && reaches(legacy->id, output->id)) {
    destination = {};
    destination.enabled = true;
    return cydaw::base::Result::ok();
  }

  const auto containsInstrument =
      allocator != nullptr ||
      firstSourceOfKind(cydaw::graph::impliesPolyphonicVoice) != nullptr ||
      envelope != nullptr || filter != nullptr;
  if (!containsInstrument) {
    destination = {};
    destination.graphNodeCount = flatNodes.size();
    return cydaw::base::Result::ok();
  }

  if (midiInput == nullptr || allocator == nullptr || oscillator == nullptr ||
      envelope == nullptr || filter == nullptr || output == nullptr)
    return cydaw::base::Result::fail(
        "Poly synth graph requires MIDI Input, Voice Allocator, a sound "
        "source (Poly Oscillator, Wavetable or String), Envelope, Voice "
        "Filter and Master Output nodes.");

  const auto typedSignalFlow =
      amplifier != nullptr && reaches(midiInput->id, allocator->id) &&
      reaches(allocator->id, oscillator->id) &&
      reaches(allocator->id, envelope->id) &&
      reaches(oscillator->id, amplifier->id) &&
      reaches(envelope->id, amplifier->id) &&
      reaches(amplifier->id, filter->id) && reaches(filter->id, output->id);
  const auto legacySignalFlow =
      amplifier == nullptr && reaches(midiInput->id, allocator->id) &&
      reaches(allocator->id, oscillator->id) &&
      reaches(oscillator->id, envelope->id) &&
      reaches(envelope->id, filter->id) && reaches(filter->id, output->id);
  if (!typedSignalFlow && !legacySignalFlow)
    return cydaw::base::Result::fail(
        "Connect Pitch and Gate from the Voice Allocator, then route "
        "Oscillator "
        "and ADSR through Gain, Voice Filter and Master Output.");

  using Waveform = cydaw::engine::PolyOscillatorWaveform;
  destination = {};
  destination.enabled = true;
  // Published graph inputs/outputs are boundaries, not DSP processors.  The
  // typed factory instrument itself contains allocator, oscillator, ADSR,
  // VCA, filter and its two published boundaries.
  destination.graphNodeCount = typedSignalFlow ? 7 : 6;
  destination.voiceCount = 16;
  if (allocator != nullptr) {
    destination.voiceMode = base::limit(
        0, 2, static_cast<int>(std::lround(allocator->parameters.voiceMode)));
    destination.glideSeconds = base::limit(
        0.0f, 2.0f, allocator->parameters.glideMilliseconds / 1'000.0f);
  }
  if (oscillator == wavetable && wavetable != nullptr) {
    destination.waveform = Waveform::wavetable;
    destination.wavetablePosition =
        base::limit(0.0f, 1.0f, oscillator->parameters.wavetablePosition);
    destination.wavetableDetuneCents =
        base::limit(0.0f, 50.0f, oscillator->parameters.wavetableDetuneCents);
  } else if (oscillator == modelVoice && modelVoice != nullptr) {
    destination.waveform = Waveform::soundModel;
    destination.soundModelValues = oscillator->parameters.soundModel;
  } else if (oscillator == samplerVoice && samplerVoice != nullptr) {
    destination.waveform = Waveform::sampler;
    destination.samplerValues = oscillator->parameters.sampler;
    destination.samplerControllers = oscillator->parameters.samplerControllers;
    destination.drumKit = samplerVoice == drumVoice;
    if (destination.drumKit)
      for (int index = 0; index < cydaw::engine::drum::valueCount; ++index)
        destination.drumValues[static_cast<std::size_t>(index)] =
            cydaw::engine::drum::clampValue(
                index,
                oscillator->parameters.drum[static_cast<std::size_t>(index)]);
  } else if (oscillator == prismVoice && prismVoice != nullptr) {
    destination.waveform = Waveform::prism;
    for (int index = 0; index < cydaw::engine::prism::parameterCount; ++index)
      destination.prismValues[static_cast<std::size_t>(index)] =
          cydaw::engine::prism::clampValue(
              index,
              oscillator->parameters.prism[static_cast<std::size_t>(index)]);
  } else if (oscillator == kickVoice && kickVoice != nullptr) {
    destination.waveform = Waveform::kick;
    for (int index = 0; index < cydaw::engine::kick::parameterCount; ++index)
      destination.kickValues[static_cast<std::size_t>(index)] =
          cydaw::engine::kick::clampValue(
              index,
              oscillator->parameters.kick[static_cast<std::size_t>(index)]);
  } else if (familyModel != cydaw::engine::synth::Model::none) {
    destination.waveform = Waveform::family;
    destination.synthModel = familyModel;
    for (int index = 0;
         index < cydaw::engine::synth::parameterCount(familyModel); ++index)
      destination.synthValues[static_cast<std::size_t>(index)] =
          cydaw::engine::synth::clampValue(
              familyModel, index,
              oscillator->parameters.synth[static_cast<std::size_t>(index)]);
  } else if (const auto container =
                 cydaw::engine::instrument_container::kindOfType(
                     oscillator->typeKey.toStdString());
             container != cydaw::engine::instrument_container::Kind::none) {
    // An instrument container: its pads or its layers play (the track's
    // engine), its voices nothing.
    destination.waveform = Waveform::rack;
    destination.containerKind = container;
    for (int index = 0; index < cydaw::engine::instrument_container::valueCount;
         ++index)
      destination.containerValues[static_cast<std::size_t>(index)] =
          cydaw::engine::instrument_container::clampValue(
              index,
              oscillator->parameters.selector[static_cast<std::size_t>(index)]);
  } else if (oscillator == stringVoice && stringVoice != nullptr) {
    destination.waveform = Waveform::string;
    destination.stringDamping =
        base::limit(0.0f, 1.0f, oscillator->parameters.stringDamping);
    destination.stringDecaySeconds =
        base::limit(0.05f, 20.0f, oscillator->parameters.stringDecaySeconds);
  } else
    destination.waveform = static_cast<Waveform>(
        base::limit(0, 3,
                    static_cast<int>(std::lround(
                        oscillator->parameters.oscillatorWaveform))));
  destination.oscillatorLevel = base::limit(
      0.0f,
      cydaw::engine::playsRecordingLevel(destination.waveform) ? 1.0f : 0.5f,
      oscillator->parameters.oscillatorLevel);
  destination.attackSeconds = base::limit(
      0.0001f, 5.0f, envelope->parameters.attackMilliseconds * 0.001f);
  destination.decaySeconds = base::limit(
      0.0001f, 5.0f, envelope->parameters.decayMilliseconds * 0.001f);
  destination.sustainLevel =
      base::limit(0.0f, 1.0f, envelope->parameters.sustainLevel);
  destination.releaseSeconds = base::limit(
      0.0001f, 10.0f, envelope->parameters.releaseMilliseconds * 0.001f);
  destination.filterCutoffHz =
      base::limit(20.0f, 20'000.0f, filter->parameters.cutoffHz);
  for (const auto &node : flatNodes) {
    if (!reaches(midiInput->id, node.id) || !reaches(node.id, allocator->id))
      continue;
    if (node.typeKey == "midi-transpose")
      destination.transposeSemitones +=
          static_cast<int>(std::lround(node.parameters.midiTranspose));
    else if (node.typeKey == "note-filter") {
      destination.minimumMidiNote = std::max(
          destination.minimumMidiNote,
          static_cast<int>(std::lround(node.parameters.midiMinimumNote)));
      destination.maximumMidiNote = std::min(
          destination.maximumMidiNote,
          static_cast<int>(std::lround(node.parameters.midiMaximumNote)));
    } else if (node.typeKey == "velocity-map") {
      destination.velocityScale *= node.parameters.midiVelocityScale;
    }
  }
  // The MIDI effects on the way, in the order the notes meet them (by
  // their number of cables from the input).
  {
    const auto distance = [&](const NodeId destinationId) {
      std::vector<std::pair<NodeId, int>> pending{{midiInput->id, 0}};
      std::set<NodeId> visited;
      for (std::size_t at = 0; at < pending.size(); ++at) {
        const auto [current, steps] = pending[at];
        if (current == destinationId)
          return steps;
        if (!visited.insert(current).second)
          continue;
        for (const auto &connection : flatConnections)
          if (connection.source == current)
            pending.emplace_back(connection.destination, steps + 1);
      }
      return 1'000'000;
    };
    std::vector<std::pair<int, const FlatNode *>> chain;
    for (const auto &node : flatNodes)
      if (cydaw::engine::isMidiEffectType(node.typeKey.toStdString()) &&
          reaches(midiInput->id, node.id) && reaches(node.id, allocator->id))
        chain.emplace_back(distance(node.id), &node);
    std::stable_sort(
        chain.begin(), chain.end(),
        [](const auto &a, const auto &b) { return a.first < b.first; });
    destination.midiEffects.reset();
    if (!chain.empty()) {
      auto effects = std::make_shared<cydaw::engine::MidiEffectsPlan>();
      for (const auto &[steps, node] : chain) {
        if (effects->count >= cydaw::engine::midiEffectMost)
          break;
        const auto *effect =
            cydaw::engine::effectForType(node->typeKey.toStdString());
        effects->effects[effects->count++] =
            effect != nullptr
                ? cydaw::engine::midiEffectSettingsOf(*effect, node->parameters)
                : cydaw::engine::legacyArpeggiatorSettings(0.25, 1.0);
      }
      destination.midiEffects = std::move(effects);
    }
  }
  destination.transposeSemitones =
      base::limit(-96, 96, destination.transposeSemitones);
  destination.minimumMidiNote =
      base::limit(0, 127, destination.minimumMidiNote);
  destination.maximumMidiNote = base::limit(destination.minimumMidiNote, 127,
                                            destination.maximumMidiNote);
  destination.velocityScale =
      base::limit(0.0f, 4.0f, destination.velocityScale);
  destination.wavetable = voiceWavetableBank();
  destination.soundModel = voiceSoundModel();
  attachVoiceSampler(destination);
  if (destination.waveform == Waveform::prism)
    destination.prismTables = cydaw::engine::prism::resolveTables(
        destination.prismValues, voicePrismTables());
  return cydaw::base::Result::ok();
}
template <typename Result, typename Pick>
Result GraphDocument::firstVoiceCard(const cydaw::base::Text &typeKey,
                                     Pick pick) const {
  std::set<cydaw::base::Text> visiting;
  std::function<std::optional<Result>(const cydaw::base::Text &)> inside =
      [&](const cydaw::base::Text &key) -> std::optional<Result> {
    const auto *module = findModule(key);
    if (module == nullptr || !visiting.insert(key).second)
      return std::nullopt;
    for (const auto &child : module->nodes) {
      if (child.typeKey == typeKey)
        return pick(child);
      if (auto found = inside(child.typeKey))
        return found;
    }
    return std::nullopt;
  };
  for (const auto &node : nodes_) {
    if (node.typeKey == typeKey)
      return pick(node);
    if (auto found = inside(node.typeKey))
      return *found;
  }
  return Result{};
}
cydaw::engine::SoundModelRef GraphDocument::voiceSoundModel() const {
  return firstVoiceCard<cydaw::engine::SoundModelRef>(
      "sound-model", [](const auto &card) { return card.soundModel; });
}
cydaw::base::Value GraphDocument::voiceSoundModelJson() const {
  return firstVoiceCard<cydaw::base::Value>(
      "sound-model", [](const auto &card) { return card.soundModelJson; });
}
cydaw::engine::SamplerMapRef GraphDocument::voiceSampler() const {
  if (auto drums = firstVoiceCard<cydaw::engine::SamplerMapRef>(
          "batterie", [](const auto &card) { return card.samplerMap; }))
    return drums;
  return firstVoiceCard<cydaw::engine::SamplerMapRef>(
      "sampler", [](const auto &card) { return card.samplerMap; });
}
cydaw::engine::SamplerControllers
GraphDocument::voiceSamplerControllers() const {
  using Controllers = std::optional<cydaw::engine::SamplerControllers>;
  const auto pick = [](const auto &card) {
    return Controllers{card.parameters.samplerControllers};
  };
  if (const auto drums = firstVoiceCard<Controllers>("batterie", pick))
    return *drums;
  return firstVoiceCard<Controllers>("sampler", pick)
      .value_or(cydaw::engine::unsetSamplerControllers());
}
void GraphDocument::attachVoiceSampler(
    cydaw::engine::PolySynthGraphPlan &plan) const {
  plan.sampler = voiceSampler();
  plan.samplerControllers = voiceSamplerControllers();
  if (plan.sampler != nullptr)
    cydaw::app::SampleStore::instance().want(
        *plan.sampler,
        {&plan.samplerControllers, nullptr,
         cydaw::engine::samplerValue(
             plan.samplerValues, cydaw::engine::SamplerSetting::articulation)});
}
cydaw::engine::prism::TablesRef GraphDocument::voicePrismTables() const {
  return firstVoiceCard<cydaw::engine::prism::TablesRef>(
      "prism-synth", [](const auto &card) { return card.prismTables; });
}
bool GraphDocument::setNodePrismTables(const std::uint64_t nodeId,
                                       const cydaw::base::Value &json,
                                       cydaw::base::Text &error) {
  auto *node = findNode(nodeId);
  if (node == nullptr || node->typeKey != "prism-synth") {
    error = "no Prisme card " +
            cydaw::base::Text(static_cast<std::int64_t>(nodeId));
    return false;
  }
  auto built = prismTablesFromVar(json);
  if (json.isObject() && built == nullptr) {
    error = "no table could be read";
    return false;
  }
  node->prismTables = std::move(built);
  node->prismTablesJson =
      node->prismTables != nullptr ? json : cydaw::base::Value();
  node->serialised = cydaw::base::Value();
  notifyLayoutChanged("prism-tables");
  return true;
}
bool GraphDocument::setNodeImpulse(const std::uint64_t nodeId,
                                   const cydaw::base::Value &json,
                                   cydaw::base::Text &error) {
  auto *node = findNode(nodeId);
  if (node == nullptr || node->typeKey != "cab-sim") {
    error = "no cabinet card " +
            cydaw::base::Text(static_cast<std::int64_t>(nodeId));
    return false;
  }
  auto read = impulseFromVar(json);
  if (json.isObject() && read == nullptr) {
    error = "the response could not be read";
    return false;
  }
  node->impulse = std::move(read);
  node->impulseJson = node->impulse != nullptr ? json : cydaw::base::Value();
  node->serialised = cydaw::base::Value();
  notifyLayoutChanged("impulse");
  return true;
}
cydaw::base::Value
GraphDocument::nodeImpulseJson(const std::uint64_t nodeId) const {
  const auto *node = findNode(nodeId);
  return node != nullptr ? node->impulseJson : cydaw::base::Value();
}
cydaw::engine::ImpulseResponseRef
GraphDocument::nodeImpulse(const std::uint64_t nodeId) const {
  const auto *node = findNode(nodeId);
  return node != nullptr ? node->impulse : cydaw::engine::ImpulseResponseRef{};
}
cydaw::base::Value
GraphDocument::nodePrismTablesJson(const std::uint64_t nodeId) const {
  const auto *node = findNode(nodeId);
  return node != nullptr ? node->prismTablesJson : cydaw::base::Value();
}
cydaw::engine::prism::TablesRef
GraphDocument::nodePrismTables(const std::uint64_t nodeId) const {
  const auto *node = findNode(nodeId);
  return node != nullptr ? node->prismTables
                         : cydaw::engine::prism::TablesRef{};
}
bool GraphDocument::setNodeSampler(const std::uint64_t nodeId,
                                   const cydaw::base::Value &json,
                                   cydaw::base::Text &error) {
  auto *node = findNode(nodeId);
  if (node == nullptr ||
      (node->typeKey != "sampler" && node->typeKey != "batterie")) {
    error = "no sampler card " +
            cydaw::base::Text(static_cast<std::int64_t>(nodeId));
    return false;
  }
  const auto upgraded = upgradedSamplerMap(json);
  auto map = samplerMapFromVar(upgraded, error);
  if (map == nullptr)
    return false;
  node->samplerMap = std::move(map);
  node->samplerJson = upgraded;
  node->serialised = cydaw::base::Value();
  notifyLayoutChanged("sampler");
  return true;
}
cydaw::base::Value
GraphDocument::nodeInstrumentJson(const std::uint64_t nodeId) const {
  const auto *node = readNode(static_cast<NodeId>(nodeId));
  return node != nullptr ? node->instrumentJson : cydaw::base::Value();
}
bool GraphDocument::setNodeInstrumentJson(const std::uint64_t nodeId,
                                          const cydaw::base::Value &state) {
  auto *node = findNode(static_cast<NodeId>(nodeId));
  if (node == nullptr)
    return false;
  node->instrumentJson = state;
  node->serialised = cydaw::base::Value();
  return true;
}
cydaw::base::Value
GraphDocument::nodeSamplerJson(const std::uint64_t nodeId) const {
  const auto *node = findNode(nodeId);
  return node != nullptr ? node->samplerJson : cydaw::base::Value();
}
cydaw::engine::SamplerMapRef
GraphDocument::nodeSampler(const std::uint64_t nodeId) const {
  const auto *node = findNode(nodeId);
  return node != nullptr ? node->samplerMap : cydaw::engine::SamplerMapRef{};
}
bool GraphDocument::setNodeSoundModel(const std::uint64_t nodeId,
                                      const cydaw::base::Value &json,
                                      cydaw::base::Text &error) {
  auto *node = findNode(nodeId);
  if (node == nullptr || node->typeKey != "sound-model") {
    error = "no sound-model card " +
            cydaw::base::Text(static_cast<std::int64_t>(nodeId));
    return false;
  }
  auto model = soundModelFromVar(json, error);
  if (model == nullptr)
    return false;
  node->soundModel = std::move(model);
  node->soundModelJson = json;
  node->serialised = cydaw::base::Value();
  notifyLayoutChanged("sound-model");
  return true;
}
cydaw::base::Text
GraphDocument::nodeSoundModelDescription(const std::uint64_t nodeId) const {
  const auto *node = findNode(nodeId);
  return node != nullptr && node->soundModel != nullptr
             ? describeSoundModel(*node->soundModel)
             : cydaw::base::Text();
}
cydaw::base::Value
GraphDocument::nodeSoundModelJson(const std::uint64_t nodeId) const {
  const auto *node = findNode(nodeId);
  return node != nullptr ? node->soundModelJson : cydaw::base::Value();
}
cydaw::engine::SoundModelRef
GraphDocument::nodeSoundModel(const std::uint64_t nodeId) const {
  const auto *node = findNode(nodeId);
  return node != nullptr ? node->soundModel : cydaw::engine::SoundModelRef{};
}
cydaw::engine::WavetableBankRef GraphDocument::voiceWavetableBank() const {
  // The instrument compiler takes the first sound source it finds, so the
  // table that matters is the first wavetable card's, inside a device too.
  return firstVoiceCard<cydaw::engine::WavetableBankRef>(
      "wavetable", [](const auto &card) { return card.wavetableBank; });
}
void GraphDocument::restore(const cydaw::base::Value &state) {
  const auto profileEntry = cydaw::base::Time::getHighResolutionTicks();
  // Counted for this restore, not for the session: sitting next to the
  // per-call timings below, a running total reads as one of them.
  restoreReusedModules_ = 0;
  restoreParsedModules_ = 0;
  if (!restoringHistory_)
    currentGraphPresetFile_ = cydaw::base::File{};
  ;
  ;
  if (const auto *legacyNodes = state.getArray()) {
    activeFactoryPresetKey_.clear();
    for (const auto &value : *legacyNodes) {
      const auto *object = value.getDynamicObject();
      if (object == nullptr)
        continue;
      const auto key = object->getProperty(jsonKeys::key).toString();
      auto position =
          std::find_if(nodes_.begin(), nodes_.end(), [&key](const auto &node) {
            return node.typeKey == key;
          });
      if (position == nodes_.end() && key == "audio-clips")
        position =
            std::find_if(nodes_.begin(), nodes_.end(), [](const auto &node) {
              return node.typeKey.startsWith("graph-audio-input-");
            });
      if (position != nodes_.end()) {
        position->serialised = cydaw::base::Value();
        position->position = {
            base::limit(0.0f, 1.0f,
                        static_cast<float>(static_cast<double>(
                            object->getProperty(jsonKeys::x)))),
            base::limit(0.0f, 1.0f,
                        static_cast<float>(static_cast<double>(
                            object->getProperty(jsonKeys::y))))};
      }
    };
    ;
    if (!restoringHistory_)
      resetEditHistory();
    return;
  }

  const auto *root = state.getDynamicObject();
  if (root == nullptr)
    return;
  const auto schemaVersion =
      static_cast<int>(root->getProperty(jsonKeys::schema));
  const auto restoredDefinitionId =
      root->getProperty(jsonKeys::definitionId).toString().trim();
  if (restoredDefinitionId.isNotEmpty())
    graphReference_.id = restoredDefinitionId.toStdString();
  if (const auto version = parseGraphVersion(
          root->getProperty(jsonKeys::definitionVersion).toString());
      version.has_value())
    graphReference_.version = *version;
  if (root->hasProperty(jsonKeys::graphRole))
    graphRole_ =
        graphRoleFromString(root->getProperty(jsonKeys::graphRole).toString());
  if (root->hasProperty(jsonKeys::graphScope))
    graphScope_ = graphScopeFromString(
        root->getProperty(jsonKeys::graphScope).toString());
  graphName_ = root->getProperty(jsonKeys::graphName)
                   .toString()
                   .trim()
                   .substring(0, 128);
  graphCategory_ = root->getProperty(jsonKeys::graphCategory)
                       .toString()
                       .trim()
                       .substring(0, 192);
  graphAuthor_ = root->getProperty(jsonKeys::graphAuthor)
                     .toString()
                     .trim()
                     .substring(0, 128);
  graphDescription_ = root->getProperty(jsonKeys::graphDescription)
                          .toString()
                          .substring(0, 2'048);
  graphTags_.clear();
  if (const auto *tags = root->getProperty(jsonKeys::graphTags).getArray())
    for (const auto &tag : *tags)
      graphTags_.add(tag.toString());
  programs_ = restoreGraphPrograms(root->getProperty("programs"));
  if (const auto *exportProfile =
          root->getProperty(jsonKeys::vst3Export).getDynamicObject()) {
    pluginExportIdentity_.productName =
        exportProfile->getProperty(jsonKeys::productName)
            .toString()
            .trim()
            .toStdString();
    pluginExportIdentity_.companyName =
        exportProfile->getProperty(jsonKeys::companyName)
            .toString()
            .trim()
            .toStdString();
    pluginExportIdentity_.manufacturerCode =
        exportProfile->getProperty(jsonKeys::manufacturerCode)
            .toString()
            .trim()
            .toUpperCase()
            .toStdString();
    pluginExportIdentity_.pluginCode =
        exportProfile->getProperty(jsonKeys::pluginCode)
            .toString()
            .trim()
            .toUpperCase()
            .toStdString();
    if (const auto version = parseGraphVersion(
            exportProfile->getProperty(jsonKeys::version).toString());
        version.has_value())
      pluginExportIdentity_.version = *version;
  }
  if (root->hasProperty(jsonKeys::valueGraph))
    valueGraphMode_ =
        static_cast<bool>(root->getProperty(jsonKeys::valueGraph));

  if (schemaVersion >= 8) {
    viewZoom_ = base::limit(0.35f, 3.5f,
                            static_cast<float>(static_cast<double>(
                                root->getProperty(jsonKeys::viewZoom))));
    viewOffset_ = {static_cast<float>(static_cast<double>(
                       root->getProperty(jsonKeys::viewOffsetX))),
                   static_cast<float>(static_cast<double>(
                       root->getProperty(jsonKeys::viewOffsetY)))};
    // The point of the graph the view was centred on wins over the pixel
    // offset once the canvas is laid out; the offset stands in until then.
    restoredViewCentre_.reset();
    restoredViewAnchor_ = 0;
    if (root->hasProperty(jsonKeys::viewCentreX) &&
        root->hasProperty(jsonKeys::viewCentreY)) {
      restoredViewCentre_ = base::Point<float>{
          static_cast<float>(
              static_cast<double>(root->getProperty(jsonKeys::viewCentreX))),
          static_cast<float>(
              static_cast<double>(root->getProperty(jsonKeys::viewCentreY)))};
      restoredViewAnchor_ = static_cast<NodeId>(static_cast<std::int64_t>(
          root->getProperty(jsonKeys::viewAnchorNode)));
      restoredViewAnchorDelta_ = {
          static_cast<float>(
              static_cast<double>(root->getProperty(jsonKeys::viewAnchorDX))),
          static_cast<float>(
              static_cast<double>(root->getProperty(jsonKeys::viewAnchorDY)))};
    }
  } else {
    ;
  }
  viewBookmarks_.fill(std::nullopt);
  if (const auto *bookmarks =
          root->getProperty(jsonKeys::viewBookmarks).getArray())
    for (const auto &value : *bookmarks)
      if (const auto *object = value.getDynamicObject()) {
        const auto slot = static_cast<int>(object->getProperty(jsonKeys::slot));
        if (slot < 1 || slot > static_cast<int>(viewBookmarks_.size()))
          continue;
        GraphViewBookmark bookmark;
        bookmark.zoom = base::limit(0.35f, 3.5f,
                                    static_cast<float>(static_cast<double>(
                                        object->getProperty(jsonKeys::zoom))));
        bookmark.offset = {static_cast<float>(static_cast<double>(
                               object->getProperty(jsonKeys::offsetX))),
                           static_cast<float>(static_cast<double>(
                               object->getProperty(jsonKeys::offsetY)))};
        viewBookmarks_[static_cast<std::size_t>(slot - 1)] = bookmark;
      }
  if (schemaVersion >= 14) {
    if (root->hasProperty(jsonKeys::selectionPanelVisible))
      selectionPanelVisible_ =
          static_cast<bool>(root->getProperty(jsonKeys::selectionPanelVisible));
    if (root->hasProperty(jsonKeys::configurationPanelVisible))
      configurationPanelVisible_ = static_cast<bool>(
          root->getProperty(jsonKeys::configurationPanelVisible));
    if (root->hasProperty(jsonKeys::configurationPanelShowsPalette))
      configurationPanelShowsPalette_ = static_cast<bool>(
          root->getProperty(jsonKeys::configurationPanelShowsPalette));
    if (root->hasProperty(jsonKeys::configurationPanelShowsMembers))
      configurationPanelShowsMembers_ = static_cast<bool>(
          root->getProperty(jsonKeys::configurationPanelShowsMembers));
    if (configurationPanelShowsMembers_)
      configurationPanelShowsPalette_ = false;
    if (root->hasProperty(jsonKeys::selectionPanelWidth))
      selectionPanelWidth_ = base::limit(
          190, 420,
          static_cast<int>(root->getProperty(jsonKeys::selectionPanelWidth)));
    if (root->hasProperty(jsonKeys::configurationPanelWidth))
      configurationPanelWidth_ =
          base::limit(220, 480,
                      static_cast<int>(root->getProperty(
                          jsonKeys::configurationPanelWidth)));
  }
  minimapVisible_ =
      schemaVersion >= 22 && root->hasProperty(jsonKeys::minimapVisible)
          ? static_cast<bool>(root->getProperty(jsonKeys::minimapVisible))
          : true;
  ;

  activeFactoryPresetKey_ =
      root->getProperty(jsonKeys::factoryPreset).toString();

  const auto profileStart = cydaw::base::Time::getHighResolutionTicks();
  const auto profileHead =
      cydaw::base::Text(cydaw::base::Time::highResolutionTicksToSeconds(
                            profileStart - profileEntry) *
                            1000.0,
                        2);
  auto profileMark = profileStart;
  const auto profileSince = [&profileMark] {
    const auto now = cydaw::base::Time::getHighResolutionTicks();
    const auto ms =
        cydaw::base::Time::highResolutionTicksToSeconds(now - profileMark) *
        1000.0;
    profileMark = now;
    return cydaw::base::Text(ms, 2);
  };
  initialiseCatalog();
  // Every class the state carries is authoritative, factory keys included:
  // a project may hold an edited body under a factory key, and keeping the
  // live one would silently overwrite it. What can be kept is a class whose
  // description is the very object it was already built from — switching
  // track re-parsed thirty-four identical class bodies for nothing.
  std::vector<SavedModule> previousModules;
  previousModules.swap(savedModules_);
  nextModuleIndex_ = 1;
  if (const auto *modules = root->getProperty(jsonKeys::modules).getArray()) {
    for (const auto &moduleValue : *modules) {
      const auto *object = moduleValue.getDynamicObject();
      if (object == nullptr)
        continue;
      SavedModule module;
      module.key = object->getProperty(jsonKeys::key).toString();
      if (const auto reusable = std::find_if(
              previousModules.begin(), previousModules.end(),
              [object, &module](const SavedModule &candidate) {
                return candidate.factoryModule && candidate.key == module.key &&
                       candidate.source.getDynamicObject() == object;
              });
          reusable != previousModules.end()) {
        ++restoreReusedModules_;
        savedModules_.push_back(std::move(*reusable));
        registerModuleType(savedModules_.back());
        ++nextModuleIndex_;
        continue;
      }
      ++restoreParsedModules_;
      module.source = moduleValue;
      module.sourceSchema = schemaVersion;
      module.name = object->getProperty(jsonKeys::name).toString();
      if (const auto version = parseGraphVersion(
              object->getProperty(jsonKeys::version).toString());
          version.has_value())
        module.version = *version;
      if (schemaVersion >= 27)
        module.parentClassKey =
            object->getProperty("parentClassKey").toString();
      module.programs = restoreGraphPrograms(object->getProperty("programs"));
      module.description = object->getProperty(jsonKeys::description)
                               .toString()
                               .substring(0, 2'048);
      if (schemaVersion >= 28) {
        module.category = object->getProperty(jsonKeys::category)
                              .toString()
                              .trim()
                              .substring(0, 192);
        module.author = object->getProperty(jsonKeys::author)
                            .toString()
                            .trim()
                            .substring(0, 128);
      }
      if (object->hasProperty(jsonKeys::role))
        module.role =
            graphRoleFromString(object->getProperty(jsonKeys::role).toString());
      if (const auto *tags = object->getProperty(jsonKeys::tags).getArray())
        for (const auto &tag : *tags)
          if (const auto cleaned = tag.toString().trim().substring(0, 48);
              cleaned.isNotEmpty())
            module.tags.addIfNotAlreadyThere(cleaned, true);
      if (module.key.isEmpty() || module.name.isEmpty())
        continue;
      const auto hasPortMetadata = schemaVersion >= 10 &&
                                   object->hasProperty(jsonKeys::hasInput) &&
                                   object->hasProperty(jsonKeys::inputKind) &&
                                   object->hasProperty(jsonKeys::hasOutput) &&
                                   object->hasProperty(jsonKeys::outputKind);
      if (hasPortMetadata) {
        module.hasInput =
            static_cast<bool>(object->getProperty(jsonKeys::hasInput));
        module.inputKind = portKindFromInt(
            static_cast<int>(object->getProperty(jsonKeys::inputKind)));
        module.hasOutput =
            static_cast<bool>(object->getProperty(jsonKeys::hasOutput));
        module.outputKind = portKindFromInt(
            static_cast<int>(object->getProperty(jsonKeys::outputKind)));
        module.inputChannels = schemaVersion >= 13
                                   ? static_cast<std::uint8_t>(base::limit(
                                         0, 8,
                                         static_cast<int>(object->getProperty(
                                             jsonKeys::inputChannels))))
                                   : 0;
        module.outputChannels = schemaVersion >= 13
                                    ? static_cast<std::uint8_t>(base::limit(
                                          0, 8,
                                          static_cast<int>(object->getProperty(
                                              jsonKeys::outputChannels))))
                                    : 0;
        module.factoryModule =
            static_cast<bool>(object->getProperty(jsonKeys::factory));
        module.inlineModule =
            schemaVersion >= 11 &&
            static_cast<bool>(object->getProperty(jsonKeys::inline_));
        module.locked =
            schemaVersion >= 13 &&
            static_cast<bool>(object->getProperty(jsonKeys::locked));
      }
      if (const auto *children =
              object->getProperty(jsonKeys::nodes).getArray()) {
        for (const auto &childValue : *children) {
          if (const auto *child = childValue.getDynamicObject()) {
            auto restoredTypeKey =
                child->getProperty(jsonKeys::type).toString();
            if (restoredTypeKey == "graph-audio-input" ||
                restoredTypeKey == "graph-audio-output") {
              const auto channels =
                  restoredTypeKey.endsWith("input")
                      ? static_cast<int>(
                            child->getProperty(jsonKeys::outputChannels))
                      : static_cast<int>(
                            child->getProperty(jsonKeys::inputChannels));
              restoredTypeKey =
                  graphAudioBoundaryKey(restoredTypeKey.endsWith("input"),
                                        channels > 0 ? channels : 2);
            }
            ModuleNode restoredChild{
                static_cast<NodeId>(static_cast<std::int64_t>(
                    child->getProperty(jsonKeys::id))),
                restoredTypeKey,
                child->getProperty(jsonKeys::title).toString(),
                {graphCoordinate(
                     static_cast<double>(child->getProperty(jsonKeys::x)),
                     schemaVersion < 22),
                 graphCoordinate(
                     static_cast<double>(child->getProperty(jsonKeys::y)),
                     schemaVersion < 22)}};
            restoredChild.parameters = restoreNativeParameters(
                child->getProperty(jsonKeys::parameters),
                defaultParametersForType(restoredChild.typeKey));
            if (const auto *childType = findType(restoredChild.typeKey))
              restoredChild.ports = childType->ports;
            if (schemaVersion >= 21)
              restorePortDefaults(child->getProperty(jsonKeys::portDefaults),
                                  restoredChild.ports);
            else
              applyLegacyPortDefaults(restoredChild.typeKey,
                                      restoredChild.parameters,
                                      restoredChild.ports);
            if (const auto interfaceValue =
                    child->getProperty(jsonKeys::interface);
                interfaceValue.getDynamicObject() != nullptr)
              restoredChild.interfaceState =
                  restoreModuleInterface(interfaceValue);
            restoredChild.interfaceLayoutOverridden =
                schemaVersion >= 18 && static_cast<bool>(child->getProperty(
                                           "interfaceLayoutOverridden"));
            restoredChild.collapsed =
                schemaVersion >= 24 &&
                static_cast<bool>(child->getProperty("collapsed"));
            restoredChild.alias =
                child->getProperty(jsonKeys::alias).toString();
            restoredChild.description =
                child->getProperty(jsonKeys::description)
                    .toString()
                    .substring(0, 2'048);
            restoredChild.inputChannels = static_cast<std::uint8_t>(base::limit(
                0, 8,
                static_cast<int>(child->getProperty(jsonKeys::inputChannels))));
            restoredChild.outputChannels = static_cast<std::uint8_t>(
                base::limit(0, 8,
                            static_cast<int>(
                                child->getProperty(jsonKeys::outputChannels))));
            readCardAssets(*child, restoredChild);
            module.nodes.push_back(std::move(restoredChild));
          }
        }
      }
      if (const auto *links =
              object->getProperty(jsonKeys::connections).getArray()) {
        for (const auto &linkValue : *links) {
          if (const auto *link = linkValue.getDynamicObject())
            module.connections.push_back(
                {static_cast<NodeId>(static_cast<std::int64_t>(
                     link->getProperty(jsonKeys::source))),
                 static_cast<NodeId>(static_cast<std::int64_t>(
                     link->getProperty(jsonKeys::destination))),
                 portKindFromInt(
                     static_cast<int>(link->getProperty(jsonKeys::kind))),
                 static_cast<std::uint8_t>(
                     base::limit(0, 8,
                                 static_cast<int>(link->getProperty(
                                     jsonKeys::audioChannels)))),
                 schemaVersion >= 16
                     ? static_cast<cydaw::graph::PortId>(
                           static_cast<std::int64_t>(
                               link->getProperty(jsonKeys::sourcePort)))
                     : cydaw::graph::PortId{2},
                 schemaVersion >= 16
                     ? static_cast<cydaw::graph::PortId>(
                           static_cast<std::int64_t>(
                               link->getProperty(jsonKeys::destinationPort)))
                     : cydaw::graph::PortId{1}});
          if (const auto *link = linkValue.getDynamicObject();
              link != nullptr && link->hasProperty("mode")) {
            module.connections.back().mode = static_cast<std::uint8_t>(
                base::limit(0, 1, static_cast<int>(link->getProperty("mode"))));
            module.connections.back().amount = static_cast<float>(
                static_cast<double>(link->getProperty("amount")));
          }
        }
      }
      if (!hasPortMetadata)
        inferModulePorts(module);
      const auto interfaceValue = object->getProperty(jsonKeys::interface);
      module.interfaceState = interfaceValue.getDynamicObject() != nullptr
                                  ? restoreModuleInterface(interfaceValue)
                                  : createDefaultModuleInterface(module);
      if (schemaVersion < 5)
        for (const auto &parameter : module.interfaceState.parameters)
          (void)updateModuleParameter(module, parameter.key, parameter.value);
      savedModules_.push_back(std::move(module));
      registerModuleType(savedModules_.back());
      ++nextModuleIndex_;
    }
  }
  // A factory class the state does not mention is kept rather than rebuilt.
  // The incoming body always wins when there is one, so an edited class under
  // a factory key still replaces the live one; this only covers the keys the
  // state is silent about, which is what lets a state stop carrying classes
  // it never changed.
  for (auto &previous : previousModules) {
    if (!previous.factoryModule || previous.key.isEmpty() ||
        findModule(previous.key) != nullptr)
      continue;
    savedModules_.push_back(std::move(previous));
    registerModuleType(savedModules_.back());
  }
  const auto profileModules = profileSince();
  ensureFactoryModules();
  const auto profileFactory = profileSince();
  for (auto &module : savedModules_)
    for (auto &child : module.nodes) {
      if (const auto *type = findType(child.typeKey)) {
        auto ports = type->ports;
        for (auto &port : ports)
          if (const auto restored =
                  std::ranges::find(child.ports, port.id, &NodePort::id);
              restored != child.ports.end())
            port.defaultValue = restored->defaultValue;
        if (findModule(child.typeKey) == nullptr)
          applyLegacyPortDefaults(child.typeKey, child.parameters, ports);
        // Reconciling a child against the current primitive is
        // derived work, repeated identically at every load — except
        // when the primitive has gained or lost a port, which the
        // stored description no longer covers.
        if (ports.size() != child.ports.size()) {
          module.source = cydaw::base::Value();
          module.definitionCache.reset();
        }
        child.ports = std::move(ports);
      }
      if (child.interfaceState.parameters.empty())
        if (const auto *nested = findModule(child.typeKey))
          child.interfaceState =
              createGraphInstanceInterface(child.id, *nested);
    }

  const auto profilePorts = profileSince();
  nodes_.clear();
  connections_.clear();
  comments_.clear();
  selectedNodes_.clear();
  selectedCommentId_ = 0;
  selectedConnection_ = -1;
  NodeId highestId{};
  // A corrupt or crafted state: no node without an id, none twice (their
  // cables would all be refused), none past what a float-exact id holds,
  // and no more of them than the native graph takes.
  constexpr NodeId largestNodeId = NodeId{1} << 48;
  std::set<NodeId> restoredIds;
  if (const auto *values = root->getProperty(jsonKeys::nodes).getArray()) {
    for (const auto &value : *values) {
      const auto *object = value.getDynamicObject();
      if (object == nullptr)
        continue;
      const auto id = static_cast<NodeId>(
          static_cast<std::int64_t>(object->getProperty(jsonKeys::id)));
      if (id == 0 || id > largestNodeId || nodes_.size() >= 4'096 ||
          !restoredIds.insert(id).second)
        continue;
      auto typeKey = object->getProperty(jsonKeys::type).toString();
      if (typeKey == "graph-audio-input" || typeKey == "graph-audio-output") {
        const auto channels =
            typeKey.endsWith("input")
                ? static_cast<int>(
                      object->getProperty(jsonKeys::outputChannels))
                : static_cast<int>(
                      object->getProperty(jsonKeys::inputChannels));
        typeKey = graphAudioBoundaryKey(typeKey.endsWith("input"),
                                        channels > 0 ? channels : 2);
      }
      const auto *type = findType(typeKey);
      if (id == 0 || type == nullptr)
        continue;
      auto title = object->getProperty(jsonKeys::title).toString();
      if (type->translationKey.isNotEmpty())
        title = type->title;
      Node restoredNode{id,
                        typeKey,
                        title,
                        type->colour,
                        {graphCoordinate(static_cast<double>(
                                             object->getProperty(jsonKeys::x)),
                                         schemaVersion < 22),
                         graphCoordinate(static_cast<double>(
                                             object->getProperty(jsonKeys::y)),
                                         schemaVersion < 22)},
                        type->hasInput,
                        type->inputKind,
                        type->hasOutput,
                        type->outputKind,
                        type->protectedNode,
                        type->composite};
      restoredNode.alias = object->getProperty(jsonKeys::alias)
                               .toString()
                               .trim()
                               .substring(0, 96);
      restoredNode.description = object->getProperty(jsonKeys::description)
                                     .toString()
                                     .substring(0, 2'048);
      restoredNode.inputChannels =
          schemaVersion >= 13 ? static_cast<std::uint8_t>(base::limit(
                                    0, 8,
                                    static_cast<int>(object->getProperty(
                                        jsonKeys::inputChannels))))
                              : type->inputChannels;
      restoredNode.outputChannels =
          schemaVersion >= 13 ? static_cast<std::uint8_t>(base::limit(
                                    0, 8,
                                    static_cast<int>(object->getProperty(
                                        jsonKeys::outputChannels))))
                              : type->outputChannels;
      restoredNode.ports = type->ports;
      restoredNode.parameters =
          restoreNativeParameters(object->getProperty(jsonKeys::parameters),
                                  defaultParametersForType(typeKey));
      applyLegacyPortDefaults(restoredNode.typeKey, restoredNode.parameters,
                              restoredNode.ports);
      if (schemaVersion >= 21) {
        restorePortDefaults(object->getProperty(jsonKeys::portDefaults),
                            restoredNode.ports);
        // A primitive's socket is a reading of its native parameter,
        // which is what the audio runs. A file that tunes the
        // parameters alone left the socket at the port's old value:
        // "frequency = 1000" beside a band the plot and the details
        // panel put at 110. A value node's own inputs are the file's.
        if (findModule(restoredNode.typeKey) == nullptr)
          applyParameterPortDefaults(restoredNode.typeKey,
                                     restoredNode.parameters,
                                     restoredNode.ports);
      }
      const auto interfaceValue = object->getProperty(jsonKeys::interface);
      restoredNode.interfaceState = interfaceValue.getDynamicObject() != nullptr
                                        ? restoreModuleInterface(interfaceValue)
                                        : createNodeInterface(restoredNode);
      // A native card's settings are named in English, whatever the
      // language they were saved in.
      if (interfaceValue.getDynamicObject() != nullptr &&
          findModule(restoredNode.typeKey) == nullptr)
        relabelPublishedParameters(restoredNode.interfaceState,
                                   createNodeInterface(restoredNode));
      // The interface's values are what they were when the file was
      // written; the native parameters are what the card holds, and
      // a file may tune those alone. The graph's published list reads
      // the native values (createDefaultGraphInterface); the card's
      // own presentation — its live EQ curve — read the stale ones:
      // a band tuned to 110 Hz in the file sat at 1000 on its plot.
      if (findModule(restoredNode.typeKey) == nullptr)
        for (auto &parameter : restoredNode.interfaceState.parameters)
          parameter.value =
              nativeParameterValue(restoredNode.parameters, parameter.key);
      restoredNode.interfaceLayoutOverridden =
          schemaVersion >= 18 &&
          static_cast<bool>(object->getProperty("interfaceLayoutOverridden"));
      restoredNode.collapsed =
          schemaVersion >= 24 &&
          static_cast<bool>(object->getProperty(jsonKeys::collapsed));
      restoredNode.bypassed =
          static_cast<bool>(object->getProperty(jsonKeys::bypassed));
      if (schemaVersion >= 29)
        if (const auto *harmonics =
                object->getProperty(jsonKeys::wavetable).getArray())
          applyWavetableHarmonics(restoredNode, *harmonics);
      if (const auto model = object->getProperty(jsonKeys::soundModel);
          model.isObject()) {
        cydaw::base::Text modelError;
        if (auto parsed = soundModelFromVar(model, modelError)) {
          restoredNode.soundModel = std::move(parsed);
          restoredNode.soundModelJson = model;
        } else if (isMissingFactoryModel(model)) {
          // A factory model not on this computer: still named, silent.
          restoredNode.soundModelJson = model;
        }
      }
      if (const auto map =
              upgradedSamplerMap(object->getProperty(jsonKeys::sampler));
          map.isObject()) {
        cydaw::base::Text mapError;
        if (auto parsed = samplerMapFromVar(map, mapError)) {
          restoredNode.samplerMap = std::move(parsed);
          restoredNode.samplerJson = map;
        }
      }
      if (const auto instrument = object->getProperty("instrument");
          instrument.isObject())
        restoredNode.instrumentJson = instrument;
      if (const auto *names = object->getProperty("branchNames").getArray())
        for (const auto &name : *names)
          restoredNode.branchNames.add(name.toString().substring(0, 64));
      if (const auto tables = object->getProperty("prismTables");
          tables.isObject())
        if (auto built = prismTablesFromVar(tables)) {
          restoredNode.prismTables = std::move(built);
          restoredNode.prismTablesJson = tables;
        }
      if (const auto response = object->getProperty("impulse");
          response.isObject())
        if (auto read = impulseFromVar(response)) {
          restoredNode.impulse = std::move(read);
          restoredNode.impulseJson = response;
        }
      const auto missingPresentation =
          restoredNode.interfaceState.parameters.empty() &&
          restoredNode.interfaceState.controls.empty() &&
          restoredNode.interfaceState.deviceControls.empty();
      // Factory subgraphs originally serialised an empty presentation
      // object for their primitive children. Treat that as missing data
      // for every node kind, otherwise opening the class causes the
      // Graph presentation synchroniser to discard all published
      // parameters (including the ADSR contract).
      if (missingPresentation ||
          (findModule(typeKey) != nullptr && schemaVersion < 17))
        restoredNode.interfaceState = createNodeInterface(restoredNode);
      nodes_.push_back(std::move(restoredNode));
      highestId = std::max(highestId, id);
    }
  }
  nextNodeId_ = std::max(
      highestId + 1,
      std::min(largestNodeId, static_cast<NodeId>(static_cast<std::int64_t>(
                                  root->getProperty(jsonKeys::nextNodeId)))));
  if (nodes_.empty()) {
    initialiseDefaultGraph();
  } else if (const auto *links =
                 root->getProperty(jsonKeys::connections).getArray()) {
    for (const auto &value : *links) {
      if (connections_.size() >= 16'384)
        break;
      if (const auto *link = value.getDynamicObject()) {
        const auto source = static_cast<NodeId>(
            static_cast<std::int64_t>(link->getProperty(jsonKeys::source)));
        const auto destination = static_cast<NodeId>(static_cast<std::int64_t>(
            link->getProperty(jsonKeys::destination)));
        const auto sourcePort =
            schemaVersion >= 16
                ? static_cast<cydaw::graph::PortId>(static_cast<std::int64_t>(
                      link->getProperty(jsonKeys::sourcePort)))
                : cydaw::graph::PortId{2};
        const auto destinationPort =
            schemaVersion >= 16
                ? static_cast<cydaw::graph::PortId>(static_cast<std::int64_t>(
                      link->getProperty(jsonKeys::destinationPort)))
                : cydaw::graph::PortId{1};
        const auto connected =
            connectNodes(source, destination, sourcePort, destinationPort);
        if (connected && link->hasProperty("mode")) {
          auto &restoredMode = connections_.back();
          restoredMode.mode = static_cast<std::uint8_t>(
              base::limit(0, 1, static_cast<int>(link->getProperty("mode"))));
          if (const auto amount =
                  static_cast<double>(link->getProperty("amount"));
              std::isfinite(amount))
            restoredMode.amount = static_cast<float>(amount);
        }
        if (connected && schemaVersion >= 24) {
          auto &restoredConnection = connections_.back();
          if (const auto *routing =
                  link->getProperty(jsonKeys::routing).getArray())
            for (const auto &routeValue : *routing)
              if (const auto *point = routeValue.getDynamicObject())
                restoredConnection.routingPoints.push_back(
                    graphPosition({static_cast<float>(static_cast<double>(
                                       point->getProperty(jsonKeys::x))),
                                   static_cast<float>(static_cast<double>(
                                       point->getProperty(jsonKeys::y)))}));
        }
      }
    }
  }
  std::uint64_t highestCommentId{};
  if (schemaVersion >= 23)
    if (const auto *values = root->getProperty(jsonKeys::comments).getArray())
      for (const auto &value : *values)
        if (const auto *object = value.getDynamicObject()) {
          const auto id = static_cast<std::uint64_t>(
              static_cast<std::int64_t>(object->getProperty(jsonKeys::id)));
          if (id == 0)
            continue;
          GraphComment comment;
          comment.id = id;
          comment.title = object->getProperty(jsonKeys::title)
                              .toString()
                              .trim()
                              .substring(0, 96);
          comment.description = object->getProperty(jsonKeys::description)
                                    .toString()
                                    .substring(0, 2'048);
          comment.position = graphPosition(
              {static_cast<float>(
                   static_cast<double>(object->getProperty(jsonKeys::x))),
               static_cast<float>(
                   static_cast<double>(object->getProperty(jsonKeys::y)))});
          comment.size = {
              base::limit(0.08f, 8.0f,
                          static_cast<float>(static_cast<double>(
                              object->getProperty(jsonKeys::width)))),
              base::limit(0.08f, 8.0f,
                          static_cast<float>(static_cast<double>(
                              object->getProperty(jsonKeys::height))))};
          if (object->hasProperty(jsonKeys::colour))
            comment.colour = base::Colour(
                static_cast<std::uint32_t>(static_cast<std::int64_t>(
                    object->getProperty(jsonKeys::colour))));
          if (comment.title.isEmpty())
            comment.title = tr("nodes.comment.defaultTitle");
          comments_.push_back(std::move(comment));
          highestCommentId = std::max(highestCommentId, id);
        }
  nextCommentId_ = std::max<std::uint64_t>(
      highestCommentId + 1,
      root->hasProperty(jsonKeys::nextCommentId)
          ? static_cast<std::uint64_t>(static_cast<std::int64_t>(
                root->getProperty(jsonKeys::nextCommentId)))
          : 1);
  const auto profileNodes = profileSince();
  const auto presentation = root->getProperty(jsonKeys::presentation);
  graphInterfaceState_ = presentation.getDynamicObject() != nullptr
                             ? restoreModuleInterface(presentation)
                             : createDefaultGraphInterface();
  const auto profileInterface = profileSince();
  synchroniseGraphInterface();
  const auto profileSynchronise = profileSince();
  const auto profileSliders = profileSince();
  applyTranslatedTitles();
  const auto profileTranslations = profileSince();
  restoreUiPending_ = true;
  pendingChanges_ = true;
  const auto profileLayout = profileSince();
  scheduleGraphAssetValidation();
  ;
  const auto profileValidation = profileSince();
  if (!restoringHistory_)
    resetEditHistory(&state);
  restoreProfile_ =
      "reused=" + cydaw::base::Text(restoreReusedModules_) +
      " parsed=" + cydaw::base::Text(restoreParsedModules_) +
      " head=" + profileHead + " modules=" + profileModules +
      " factory=" + profileFactory + " ports=" + profilePorts +
      " nodes=" + profileNodes + " interface=" + profileInterface +
      " synchronise=" + profileSynchronise + " sliders=" + profileSliders +
      " translations=" + profileTranslations + " layout=" + profileLayout +
      " validation=" + profileValidation + " history=" + profileSince();
}
void GraphDocument::setLayoutChangedHandler(std::function<void()> handler) {
  layoutChangedHandler_ = std::move(handler);
}
void GraphDocument::setParametersChangedHandler(std::function<void()> handler) {
  parametersChangedHandler_ = std::move(handler);
}
GraphDocument::NodeId
GraphDocument::modulationRangeNodeFor(const Node &node,
                                      const cydaw::base::Text &binding) const {
  const auto primitiveParameter =
      valuePortKeyForBinding(parameterSuffix(binding));
  const auto port =
      std::find_if(node.ports.begin(), node.ports.end(),
                   [&primitiveParameter](const NodePort &candidate) {
                     return candidate.input &&
                            candidate.type == cydaw::graph::PortType::value &&
                            candidate.key == primitiveParameter;
                   });
  if (port == node.ports.end())
    return 0;
  for (const auto &link : connections_)
    if (link.destination == node.id && link.destinationPort == port->id)
      if (const auto *source = readNode(link.source);
          source != nullptr && source->typeKey == "map-range")
        return source->id;
  return 0;
}
bool GraphDocument::modulateParameter(const NodeId sourceNodeId,
                                      const cydaw::graph::PortId sourcePort,
                                      const NodeId targetNodeId,
                                      const cydaw::base::Text &binding) {
  modulateFailure_.clear();
  if (editingLocked_) {
    modulateFailure_ = "locked";
    return false;
  }
  const auto *target = readNode(targetNodeId);
  if (target == nullptr || readNode(sourceNodeId) == nullptr) {
    modulateFailure_ = "no such node";
    return false;
  }
  const auto primitiveParameter =
      valuePortKeyForBinding(parameterSuffix(binding));
  const auto port =
      std::find_if(target->ports.begin(), target->ports.end(),
                   [&primitiveParameter](const NodePort &candidate) {
                     return candidate.input &&
                            candidate.type == cydaw::graph::PortType::value &&
                            candidate.key == primitiveParameter;
                   });
  if (port == target->ports.end()) {
    cydaw::base::TextList keys;
    for (const auto &candidate : target->ports)
      keys.add((candidate.input ? "in:" : "out:") + candidate.key);
    modulateFailure_ = "no value port '" + primitiveParameter + "' for '" +
                       binding + "'; ports: " + keys.joinIntoString(",");
    setStatus(tr("nodes.modulate.notModulatable"), true);
    return false;
  }
  const auto published = std::find_if(
      target->interfaceState.parameters.begin(),
      target->interfaceState.parameters.end(),
      [&binding](const auto &parameter) { return parameter.key == binding; });
  if (published == target->interfaceState.parameters.end()) {
    modulateFailure_ = "no published parameter '" + binding + "'";
    return false;
  }
  const auto minimum = published->minimum;
  const auto maximum = published->maximum;
  const auto value = published->value;
  const auto portId = port->id;
  // A quarter of the range either side of where the knob sits — or as
  // much of that as fits, so the knob keeps its value as the centre of
  // the sweep; clipping the sweep at the range's edge moved the centre
  // instead. Only a knob at the very edge gets a clipped, off-centre
  // sweep rather than none. The ring sets it finer afterwards.
  const auto quarter = (maximum - minimum) * 0.25;
  const auto fitted = std::min({quarter, value - minimum, maximum - value});
  const auto depth = fitted >= quarter * 0.25 ? fitted : quarter;
  const auto low = base::limit(minimum, maximum, value - depth);
  const auto high = base::limit(minimum, maximum, value + depth);

  const auto *rangeType = findType("map-range");
  if (rangeType == nullptr) {
    modulateFailure_ = "no map-range type";
    return false;
  }
  const auto rangeId = nextNodeId_;
  addNodeFromType(*rangeType, {-1'000.0f, -1'000.0f});
  auto *rangeNode = findNode(rangeId);
  if (rangeNode == nullptr) {
    modulateFailure_ = "range node not added";
    return false;
  }
  rangeNode->parameters.mapInputMinimum = -1.0f;
  rangeNode->parameters.mapInputMaximum = 1.0f;
  rangeNode->parameters.mapOutputMinimum = static_cast<float>(low);
  rangeNode->parameters.mapOutputMaximum = static_cast<float>(high);
  for (auto &parameter : rangeNode->interfaceState.parameters)
    if (bindingMatchesType(rangeNode->typeKey, parameter.key))
      parameter.value =
          nativeParameterValue(rangeNode->parameters, parameter.key);
  // Refused (the knob already cabled, a loop): the range goes again, with
  // what it was wired to, rather than stay in the graph unconnected.
  const auto dropRange = [this, rangeId] {
    std::erase_if(connections_, [rangeId](const Connection &connection) {
      return connection.source == rangeId || connection.destination == rangeId;
    });
    std::erase_if(nodes_,
                  [rangeId](const Node &node) { return node.id == rangeId; });
    selectedNodes_.erase(rangeId);
    selectedConnection_ = -1;
  };
  if (!connectNodes(sourceNodeId, rangeId, sourcePort, 0)) {
    dropRange();
    modulateFailure_ = "source -> range refused";
    return false;
  }
  if (!connectNodes(rangeId, targetNodeId, 0, portId)) {
    dropRange();
    modulateFailure_ = "range -> target refused";
    return false;
  }
  synchroniseGraphInterface();
  ;
  return true;
}
bool GraphDocument::modulateWithNewSource(
    const cydaw::base::Text &sourceTypeKey,
    const cydaw::base::Text &targetTypeKey,
    const cydaw::base::Text &parameterSuffix) {
  const auto target = nodeIdForType(targetTypeKey);
  const auto *type = findType(sourceTypeKey);
  if (editingLocked_ || target == 0 || type == nullptr)
    return false;
  const auto before = nodes_.size();
  addNodeFromType(*type);
  if (nodes_.size() <= before)
    return false;
  const auto source = nodes_.back().id;
  if (!modulateParameter(source, 0, target,
                         nodeParameterBinding(target, parameterSuffix))) {
    (void)removeNodeById(source);
    return false;
  }
  notifyLayoutChanged("modulate");
  return true;
}
void GraphDocument::removeModulation(const NodeId nodeId,
                                     const cydaw::base::Text &binding) {
  if (editingLocked_)
    return;
  auto *stored = findNode(nodeId);
  if (stored == nullptr)
    return;
  const auto rangeId = modulationRangeNodeFor(*stored, binding);
  const auto *rangeNode = rangeId != 0 ? readNode(rangeId) : nullptr;
  if (rangeNode == nullptr)
    return;
  // The centre the knob was showing becomes its value again: a turn given
  // to a modulated knob is not lost with the cable.
  const auto centre =
      static_cast<double>((rangeNode->parameters.mapOutputMinimum +
                           rangeNode->parameters.mapOutputMaximum) *
                          0.5f);
  if (findModule(stored->typeKey) != nullptr) {
    (void)updateGraphInstanceParameter(*stored, binding, centre);
  } else {
    (void)updateNodeParameter(*stored, binding, centre);
    for (auto &parameter : stored->interfaceState.parameters)
      if (parameter.key == binding)
        parameter.value = centre;
  }
  if (auto *presentation = editablePresentationForNode(*stored)) {
    const auto parameter = std::ranges::find_if(
        presentation->parameters,
        [&binding](const auto &candidate) { return candidate.key == binding; });
    if (parameter != presentation->parameters.end())
      parameter->value = centre;
  }
  std::erase_if(connections_, [rangeId](const auto &connection) {
    return connection.source == rangeId || connection.destination == rangeId;
  });
  std::erase_if(nodes_,
                [rangeId](const auto &node) { return node.id == rangeId; });
  selectedNodes_.erase(rangeId);
  synchroniseGraphInterface();
  ;
  ;
  setStatus(tr("nodes.status.unmodulated"));
  ;
  ;
  notifyLayoutChanged("unmodulate");
}
bool GraphDocument::removeModulationForTest(
    const cydaw::base::Text &targetTypeKey,
    const cydaw::base::Text &parameterSuffix) {
  const auto target = nodeIdForType(targetTypeKey);
  const auto binding = nodeParameterBinding(target, parameterSuffix);
  const auto *stored = readNode(target);
  if (stored == nullptr || modulationRangeNodeFor(*stored, binding) == 0)
    return false;
  removeModulation(target, binding);
  return true;
}
cydaw::base::TextList GraphDocument::deviceNames(const int limit) const {
  cydaw::base::TextList names;
  for (const auto &node : nodes_) {
    const auto &type = node.typeKey;
    if (type == "audio-clips" || type == "master" || type == "midi-input" ||
        type == "transport" || type.startsWith("graph-"))
      continue;
    const auto title = node.title.isEmpty() ? type : node.title;
    if (!names.contains(title, true))
      names.add(title);
    if (limit > 0 && names.size() >= limit)
      break;
  }
  return names;
}
GraphDocument::FactoryPresetBoundaries
GraphDocument::factoryPresetBoundaries(const FactoryPreset &preset) const {
  // The reading inferCurrentGraphRole() gives a loaded graph, applied to a
  // preset's node list before anything is loaded. Both boundary shapes
  // count: the track-shaped one a graph gets from audio-clips and master,
  // and the typed one it gets from the graph-audio-* nodes.
  const auto anyNode = [&preset](auto &&predicate) {
    return std::ranges::any_of(preset.nodes,
                               [&predicate](const PresetNode &node) {
                                 return predicate(node.typeKey);
                               });
  };
  FactoryPresetBoundaries boundaries;
  boundaries.midiInput = anyNode([](const cydaw::base::Text &key) {
    return key == "midi-input" || key == "graph-midi-input";
  });
  boundaries.midiOutput = anyNode([](const cydaw::base::Text &key) {
    return key == "midi-output" || key == "graph-midi-output";
  });
  boundaries.audioInput = anyNode([](const cydaw::base::Text &key) {
    return key == "audio-clips" || key.startsWith("graph-audio-input-");
  });
  boundaries.audioOutput = anyNode([](const cydaw::base::Text &key) {
    return key == "master" || key.startsWith("graph-audio-output-");
  });
  boundaries.soundSource =
      anyNode([](const cydaw::base::Text &key) {
        return key == "poly-synth" ||
               cydaw::graph::isSoundSourcePrimitive(
                   detail::primitiveProcessorKeyForType(key).toStdString());
      }) ||
      std::ranges::any_of(preset.nodes, [this](const PresetNode &node) {
        const auto *module = findModule(node.typeKey);
        return module != nullptr &&
               inferModuleRole(*module) == cydaw::graph::GraphRole::instrument;
      });
  return boundaries;
}
std::vector<GraphDocument::FactoryPresetSummary>
GraphDocument::factoryPresetSummaries() const {
  std::vector<FactoryPresetSummary> summaries;
  summaries.reserve(factoryPresets_.size());
  for (const auto &preset : factoryPresets_)
    summaries.push_back({preset.key, preset.translationKey,
                         factoryPresetBoundaries(preset).soundSource});
  return summaries;
}
cydaw::base::Result
GraphDocument::replaceInstrument(const cydaw::base::Text &presetKey) {
  const auto preset =
      std::find_if(factoryPresets_.begin(), factoryPresets_.end(),
                   [&presetKey](const auto &candidate) {
                     return candidate.key == presetKey;
                   });
  if (preset == factoryPresets_.end())
    return cydaw::base::Result::fail("Unknown instrument preset: " + presetKey);
  // A voice section's place, by what its node does: 0 allocator, 1 source,
  // 2 envelope, 3 gain, 4 voice filter.
  const auto roleOfType = [](const cydaw::base::Text &typeKey) {
    const auto key = primitiveProcessorKeyForType(typeKey);
    if (key == "core.voice-allocator")
      return 0;
    if (cydaw::graph::isPolyphonicVoiceSource(key.toStdString()))
      return 1;
    if (key == "core.adsr")
      return 2;
    if (key == "core.gain")
      return 3;
    if (key == "core.lowpass")
      return 4;
    return -1;
  };
  // The preset's own way in (its MIDI input into its allocator) and out
  // (the node that feeds its master).
  std::optional<std::size_t> presetMidi, presetMaster, presetAllocator,
      presetOut;
  cydaw::graph::PortId presetInPort{}, presetOutPort{}, presetMasterPort{};
  for (std::size_t index = 0; index < preset->nodes.size(); ++index) {
    const auto &typeKey = preset->nodes[index].typeKey;
    if (typeKey == "midi-input")
      presetMidi = index;
    else if (typeKey == "master")
      presetMaster = index;
    else if (!presetAllocator && roleOfType(typeKey) == 0)
      presetAllocator = index;
  }
  for (const auto &connection : preset->connections) {
    if (presetMaster && connection.destinationIndex == *presetMaster) {
      presetOut = connection.sourceIndex;
      presetOutPort = connection.sourcePort;
      presetMasterPort = connection.destinationPort;
    }
    if (presetMidi && presetAllocator &&
        connection.sourceIndex == *presetMidi &&
        connection.destinationIndex == *presetAllocator)
      presetInPort = connection.destinationPort;
  }
  const auto boundary = instrumentVoiceBoundary();
  const auto nodeIdOfType = [this](const char *typeKey) -> NodeId {
    const auto found =
        std::find_if(nodes_.begin(), nodes_.end(), [typeKey](const Node &node) {
          return node.typeKey == typeKey;
        });
    return found != nodes_.end() ? found->id : NodeId{};
  };
  const auto midiInput = nodeIdOfType("midi-input");
  const auto master = nodeIdOfType("master");
  // A missing plug-in's card stands for the instrument when there is no
  // voice section: the new one takes its place.
  const auto missing = boundary.voiceNodes.empty()
                           ? nodeIdOfType("missing-instrument")
                           : NodeId{};
  if (!presetMidi || !presetMaster || !presetAllocator || !presetOut ||
      (boundary.voiceNodes.empty() && missing == 0) || midiInput == 0 ||
      master == 0)
    return applyFactoryPreset(*preset);

  // The old voice: each node's place and type, and the cables between it
  // and the rest.
  std::map<NodeId, std::pair<int, cydaw::base::Text>> oldVoice;
  for (const auto id : boundary.voiceNodes)
    if (const auto *node = findNode(id))
      oldVoice[id] = {roleOfType(node->typeKey), node->typeKey};
  if (missing != 0)
    oldVoice[missing] = {-1, "missing-instrument"};
  // A preset's own nodes after its voice filter (Nébuleuse's halo) are its
  // instrument's too, while the chain still starts with them.
  auto soundOut = missing != 0 ? missing : boundary.outputNodeId;
  {
    cydaw::base::Text sourceType;
    for (const auto &[id, entry] : oldVoice)
      if (entry.first == 1)
        sourceType = entry.second;
    const auto made = std::find_if(
        factoryPresets_.begin(), factoryPresets_.end(),
        [&sourceType](const FactoryPreset &candidate) {
          return std::any_of(candidate.nodes.begin(), candidate.nodes.end(),
                             [&sourceType](const PresetNode &node) {
                               return node.typeKey == sourceType;
                             });
        });
    std::vector<cydaw::base::Text> extras;
    if (sourceType.isNotEmpty() && made != factoryPresets_.end()) {
      std::optional<std::size_t> filterIndex, masterIndex;
      for (std::size_t index = 0; index < made->nodes.size(); ++index) {
        if (!filterIndex && roleOfType(made->nodes[index].typeKey) == 4)
          filterIndex = index;
        if (made->nodes[index].typeKey == "master")
          masterIndex = index;
      }
      if (filterIndex && masterIndex)
        for (auto at = *filterIndex, guard = std::size_t{}; guard < 16;
             ++guard) {
          const auto next =
              std::find_if(made->connections.begin(), made->connections.end(),
                           [at](const PresetConnection &connection) {
                             return connection.sourceIndex == at;
                           });
          if (next == made->connections.end() ||
              next->destinationIndex == *masterIndex)
            break;
          at = next->destinationIndex;
          extras.push_back(made->nodes[at].typeKey);
        }
    }
    for (const auto &typeKey : extras) {
      const auto next =
          std::find_if(connections_.begin(), connections_.end(),
                       [soundOut](const Connection &connection) {
                         return connection.source == soundOut &&
                                connection.kind == PortKind::audio;
                       });
      const auto *node =
          next != connections_.end() ? findNode(next->destination) : nullptr;
      if (node == nullptr || node->typeKey != typeKey)
        break;
      oldVoice[node->id] = {-1, node->typeKey};
      soundOut = node->id;
    }
  }
  std::vector<Connection> kept, intoVoice, fromVoice;
  for (const auto &connection : connections_) {
    const auto fromOld = oldVoice.contains(connection.source);
    const auto toOld = oldVoice.contains(connection.destination);
    if (fromOld && toOld)
      continue;
    if (!fromOld && !toOld)
      kept.push_back(connection);
    else if (toOld)
      intoVoice.push_back(connection);
    else
      fromVoice.push_back(connection);
  }
  const auto previousNodes = nodes_;
  const auto previousConnections = connections_;
  const auto rollBack = [&](const cydaw::base::Text &why) {
    nodes_ = previousNodes;
    connections_ = previousConnections;
    synchroniseGraphInterface();
    ;
    return cydaw::base::Result::fail(why);
  };

  // Out with the old voice; in with the preset's, its boundaries the graph's.
  std::erase_if(nodes_, [&oldVoice](const Node &node) {
    return oldVoice.contains(node.id);
  });
  connections_ = kept;
  selectedNodes_.clear();
  std::vector<NodeId> ids(preset->nodes.size());
  std::array<NodeId, 5> newByRole{};
  std::array<cydaw::base::Text, 5> newTypeByRole{};
  for (std::size_t index = 0; index < preset->nodes.size(); ++index) {
    if (index == *presetMidi) {
      ids[index] = midiInput;
      continue;
    }
    if (index == *presetMaster) {
      ids[index] = master;
      continue;
    }
    const auto &presetNode = preset->nodes[index];
    const auto *type = findType(presetNode.typeKey);
    if (type == nullptr)
      return rollBack("Instrument preset references an unknown node: " +
                      presetNode.typeKey);
    addNodeFromType(*type, presetNode.position);
    auto &added = nodes_.back();
    added.parameters = presetNode.parameters;
    if (findModule(added.typeKey) == nullptr)
      applyParameterPortDefaults(added.typeKey, added.parameters, added.ports);
    added.interfaceState = createNodeInterface(added);
    ids[index] = added.id;
    if (const auto role = roleOfType(added.typeKey);
        role >= 0 && newByRole[static_cast<std::size_t>(role)] == 0) {
      newByRole[static_cast<std::size_t>(role)] = added.id;
      newTypeByRole[static_cast<std::size_t>(role)] = added.typeKey;
    }
  }
  // The new voice first in the graph's order, as a preset's is: the
  // compilers and the boundary take the first node of each kind, which
  // would otherwise be a Gain, Low-pass or Envelope card of the chain
  // (appended last, the voice would come after them).
  {
    std::set<NodeId> voice(ids.begin(), ids.end());
    voice.erase(midiInput);
    voice.erase(master);
    std::stable_partition(
        nodes_.begin(), nodes_.end(),
        [&voice](const Node &node) { return voice.contains(node.id); });
  }
  // A cable made again as it was (its mode and amount).
  const auto reconnect = [this](const Connection &old, const NodeId source,
                                const NodeId destination,
                                const cydaw::graph::PortId sourcePort,
                                const cydaw::graph::PortId destinationPort) {
    if (!connectNodes(source, destination, sourcePort, destinationPort))
      return false;
    for (auto &connection : connections_)
      if (connection.source == source &&
          connection.destination == destination &&
          connection.sourcePort == sourcePort &&
          connection.destinationPort == destinationPort) {
        connection.mode = old.mode;
        connection.amount = old.amount;
      }
    return true;
  };
  // The preset's own cables, but for its way in and out.
  for (const auto &connection : preset->connections) {
    if (connection.sourceIndex == *presetMidi &&
        connection.destinationIndex == *presetAllocator)
      continue;
    if (connection.destinationIndex == *presetMaster)
      continue;
    if (!connectNodes(ids[connection.sourceIndex],
                      ids[connection.destinationIndex], connection.sourcePort,
                      connection.destinationPort))
      return rollBack("The instrument preset could not be wired: " +
                      statusMessage_);
  }
  // In: what fed the old allocator's notes (the MIDI input, a note effect)
  // feeds the new one; cables into the old envelope, gain or filter (an
  // LFO on the cutoff) reach the new ones of the same kind.
  auto fedNotes = false;
  for (const auto &connection : intoVoice) {
    const auto &[role, typeKey] = oldVoice[connection.destination];
    if (role < 0 || newByRole[static_cast<std::size_t>(role)] == 0 ||
        newTypeByRole[static_cast<std::size_t>(role)] != typeKey)
      continue;
    if (reconnect(connection, connection.source,
                  newByRole[static_cast<std::size_t>(role)],
                  connection.sourcePort, connection.destinationPort) &&
        role == 0)
      fedNotes = true;
  }
  if (!fedNotes &&
      !connectNodes(midiInput, ids[*presetAllocator], 0, presetInPort))
    return rollBack("The instrument could not get its notes: " +
                    statusMessage_);
  // Out: the new sound goes where the old one went — the effects after
  // it, or the master; what else left the old voice (an envelope driving
  // a knob) leaves the new one of its kind.
  auto sounded = false;
  for (const auto &connection : fromVoice) {
    const auto &[role, typeKey] = oldVoice[connection.source];
    if (connection.source == soundOut && connection.kind == PortKind::audio) {
      sounded = reconnect(connection, ids[*presetOut], connection.destination,
                          presetOutPort, connection.destinationPort) ||
                sounded;
      continue;
    }
    if (role >= 0 && newByRole[static_cast<std::size_t>(role)] != 0 &&
        newTypeByRole[static_cast<std::size_t>(role)] == typeKey)
      (void)reconnect(connection, newByRole[static_cast<std::size_t>(role)],
                      connection.destination, connection.sourcePort,
                      connection.destinationPort);
  }
  if (!sounded &&
      !connectNodes(ids[*presetOut], master, presetOutPort, presetMasterPort))
    return rollBack("The instrument could not reach the output: " +
                    statusMessage_);

  synchroniseGraphInterface();
  ;
  graphRole_ = inferCurrentGraphRole();
  refreshGraphAssetValidation();
  setStatus(
      tr("nodes.status.presetLoaded", "name", tr(preset->translationKey)));
  ;
  ;
  commitEditHistory(false);
  if (layoutChangedHandler_)
    layoutChangedHandler_();
  return cydaw::base::Result::ok();
}
cydaw::base::Result
GraphDocument::insertFactoryDevice(const cydaw::base::Text &deviceKey) {
  const auto preset =
      std::find_if(factoryPresets_.begin(), factoryPresets_.end(),
                   [&deviceKey](const auto &candidate) {
                     return candidate.key == deviceKey;
                   });
  if (preset != factoryPresets_.end())
    return applyFactoryPreset(*preset);

  const auto *type = findType(deviceKey);
  if (type == nullptr)
    return cydaw::base::Result::fail("Unknown factory graph device: " +
                                     deviceKey);
  if (!type->hasInput || !type->hasOutput)
    return cydaw::base::Result::fail(
        "This factory item is not a complete signal-flow device");

  // Modules are typed transforms, not necessarily audio effects.  In
  // particular, instruments consume MIDI and produce audio.  Use the same
  // typed insertion path as editable inline Graph modules so MIDI -> audio,
  // MIDI -> MIDI and audio -> audio devices all splice into the appropriate
  // graph boundaries.
  if (type->inputKind != PortKind::audio ||
      type->outputKind != PortKind::audio) {
    const auto previousConnections = connections_;
    addNodeFromType(*type);
    const auto insertedId = nodes_.back().id;
    const auto inserted = insertModuleIntoCurrentSignalFlow(insertedId);
    const auto hasInput = std::ranges::any_of(
        connections_, [insertedId](const Connection &connection) {
          return connection.destination == insertedId;
        });
    const auto hasOutput = std::ranges::any_of(
        connections_, [insertedId](const Connection &connection) {
          return connection.source == insertedId;
        });
    if (!inserted || !hasInput || !hasOutput) {
      connections_ = previousConnections;
      std::erase_if(nodes_, [insertedId](const Node &node) {
        return node.id == insertedId;
      });
      synchroniseGraphInterface();
      ;
      return cydaw::base::Result::fail(
          "The typed Graph device could not be connected between its "
          "input and output boundaries");
    }

    synchroniseGraphInterface();
    ;
    if (graphNodesOverlap())
      arrangeNodesBySignalFlow(false);
    setStatus(tr("nodes.status.added"));
    ;
    ;
    notifyLayoutChanged();
    return cydaw::base::Result::ok();
  }

  std::set<NodeId> reachable;
  for (const auto &node : nodes_)
    if (node.typeKey == "audio-clips" ||
        node.typeKey.startsWith("graph-audio-input-") ||
        (node.hasInput && node.inputKind == PortKind::midi && node.hasOutput &&
         node.outputKind == PortKind::audio))
      reachable.insert(node.id);
  for (auto changed = true; changed;) {
    changed = false;
    for (const auto &connection : connections_)
      if (connection.kind == PortKind::audio &&
          reachable.contains(connection.source) &&
          reachable.insert(connection.destination).second)
        changed = true;
  }

  Connection splice;
  auto foundSplice = false;
  for (const auto &connection : connections_) {
    const auto *destination = findNode(connection.destination);
    if (connection.kind != PortKind::audio || destination == nullptr ||
        (destination->typeKey != "master" &&
         !destination->typeKey.startsWith("graph-audio-output-")) ||
        !reachable.contains(connection.source))
      continue;
    splice = connection;
    foundSplice = true;
    break;
  }
  if (!foundSplice)
    return cydaw::base::Result::fail(
        "The current graph has no connected audio output chain");

  const auto *source = findNode(splice.source);
  const auto *destination = findNode(splice.destination);
  if (source == nullptr || destination == nullptr)
    return cydaw::base::Result::fail("The audio chain is incomplete");
  const auto position = base::Point<float>{
      base::limit(0.04f, 0.90f,
                  (source->position.x + destination->position.x) * 0.5f),
      base::limit(0.04f, 0.90f,
                  (source->position.y + destination->position.y) * 0.5f)};

  const auto before = nodes_.size();
  addNodeFromType(*type, position);
  if (nodes_.size() <= before)
    return cydaw::base::Result::fail("The Graph is locked");
  const auto insertedId = nodes_.back().id;
  // That cable only (a parallel one between the same cards stays), its
  // ends kept on the ports they were on.
  std::erase_if(connections_, [&splice](const auto &connection) {
    return connection.source == splice.source &&
           connection.destination == splice.destination &&
           connection.kind == splice.kind &&
           connection.sourcePort == splice.sourcePort &&
           connection.destinationPort == splice.destinationPort;
  });
  if (!connectNodes(splice.source, insertedId, splice.sourcePort, {}) ||
      !connectNodes(insertedId, splice.destination, {},
                    splice.destinationPort)) {
    std::erase_if(connections_, [insertedId](const auto &connection) {
      return connection.source == insertedId ||
             connection.destination == insertedId;
    });
    std::erase_if(nodes_, [insertedId](const auto &node) {
      return node.id == insertedId;
    });
    connections_.push_back(splice);
    synchroniseGraphInterface();
    ;
    return cydaw::base::Result::fail(
        "The graph device could not be connected to the audio chain");
  }

  synchroniseGraphInterface();
  ;
  if (graphNodesOverlap())
    arrangeNodesBySignalFlow(false);
  setStatus(tr("nodes.status.added"));
  ;
  ;
  notifyLayoutChanged();
  return cydaw::base::Result::ok();
}
cydaw::base::Result
GraphDocument::insertNode(const cydaw::base::Text &nodeTypeKey) {
  if (editingLocked_)
    return cydaw::base::Result::fail("The Graph is locked");
  const auto *type = findType(nodeTypeKey);
  if (type == nullptr)
    return cydaw::base::Result::fail("Unknown Graph node: " + nodeTypeKey);
  auto mark = cydaw::base::Time::getHighResolutionTicks();
  const auto since = [&mark] {
    const auto now = cydaw::base::Time::getHighResolutionTicks();
    const auto elapsed =
        cydaw::base::Time::highResolutionTicksToSeconds(now - mark) * 1000.0;
    mark = now;
    return cydaw::base::Text(elapsed, 2);
  };
  layoutChangeCalls_ = 0;
  layoutChangeOrigins_.clear();
  addNodeFromType(*type);
  const auto added = since();
  selectedNodes_.clear();
  selectedNodes_.insert(nodes_.back().id);
  selectedCommentId_ = 0;
  selectedConnection_ = -1;
  selectedPortId_ = 0;
  ;
  const auto selection = since();
  setStatus(tr("nodes.status.added"));
  const auto status = since();
  notifyLayoutChanged("insertNode");
  insertProfile_ = "add=" + added + " selection=" + selection +
                   " status=" + status + " notify=" + since() +
                   " passes=" + cydaw::base::Text(layoutChangeCalls_) + " [" +
                   layoutChangeProfile_ + "]";
  return cydaw::base::Result::ok();
}
cydaw::base::Result GraphDocument::insertNodeBeforeOutput(
    const cydaw::base::Text &insertedNodeTypeKey) {
  if (editingLocked_)
    return cydaw::base::Result::fail("The Graph is locked");
  const auto output =
      std::find_if(nodes_.begin(), nodes_.end(), [](const Node &node) {
        return node.typeKey == "master" ||
               node.typeKey.startsWith("graph-audio-output-");
      });
  const auto *insertedType = findType(insertedNodeTypeKey);
  if (output == nodes_.end())
    return cydaw::base::Result::fail("The Graph has no audio output");
  if (insertedType == nullptr)
    return cydaw::base::Result::fail("Unknown inserted Graph node: " +
                                     insertedNodeTypeKey);
  for (std::size_t index = 0; index < connections_.size(); ++index) {
    const auto &connection = connections_[index];
    if (connection.destination != output->id ||
        connection.kind != PortKind::audio)
      continue;
    const auto *source = readNode(connection.source);
    const auto position = source != nullptr
                              ? (source->position + output->position) * 0.5f
                              : output->position;
    return spliceNodeIntoConnection(static_cast<int>(index), *insertedType,
                                    position)
               ? cydaw::base::Result::ok()
               : cydaw::base::Result::fail(
                     "The node cannot be inserted before the output");
  }
  return cydaw::base::Result::fail("The output has no audio cable");
}
cydaw::base::Result GraphDocument::insertNodeBeforeInstrument(
    const cydaw::base::Text &insertedNodeTypeKey) {
  if (editingLocked_)
    return cydaw::base::Result::fail("The Graph is locked");
  const auto *insertedType = findType(insertedNodeTypeKey);
  if (insertedType == nullptr)
    return cydaw::base::Result::fail("Unknown inserted Graph node: " +
                                     insertedNodeTypeKey);
  // The notes' cable into the first card that is not a MIDI card (the
  // voice allocator, a module that plays): the new one goes last among
  // the MIDI cards.
  for (std::size_t index = 0; index < connections_.size(); ++index) {
    const auto &connection = connections_[index];
    if (connection.kind != PortKind::midi)
      continue;
    const auto *destination = readNode(connection.destination);
    if (destination == nullptr)
      continue;
    // A MIDI card takes notes and gives notes; the voice allocator gives
    // voices, and is where the notes end.
    const auto *destinationType = findType(destination->typeKey);
    const auto midiCard = destination->typeKey != "voice-allocator" &&
                          destinationType != nullptr &&
                          destinationType->hasInput &&
                          destinationType->inputKind == PortKind::midi &&
                          destinationType->hasOutput &&
                          destinationType->outputKind == PortKind::midi;
    if (midiCard)
      continue;
    const auto *source = readNode(connection.source);
    const auto position =
        source != nullptr ? (source->position + destination->position) * 0.5f
                          : destination->position;
    return spliceNodeIntoConnection(static_cast<int>(index), *insertedType,
                                    position)
               ? cydaw::base::Result::ok()
               : cydaw::base::Result::fail(
                     "The MIDI card cannot be inserted before the instrument");
  }
  return cydaw::base::Result::fail("The graph has no instrument fed by notes");
}
cydaw::base::Result GraphDocument::validateUserGraphPresetState(
    const cydaw::base::Value &state) const {
  const auto invalid = [this] {
    return cydaw::base::Result::fail(tr("nodes.error.invalidUserPreset"));
  };
  const auto *root = state.getDynamicObject();
  if (root == nullptr)
    return invalid();
  const auto schema = static_cast<int>(root->getProperty(jsonKeys::schema));
  const auto *nodes = root->getProperty(jsonKeys::nodes).getArray();
  const auto *connections = root->getProperty(jsonKeys::connections).getArray();
  const auto *modules = root->getProperty(jsonKeys::modules).getArray();
  if (schema < 1 || schema > nodeGraphStateSchema || nodes == nullptr ||
      nodes->isEmpty() || nodes->size() > 512 ||
      (connections != nullptr && connections->size() > 4'096) ||
      (modules != nullptr && modules->size() > 128))
    return invalid();

  cydaw::base::TextList moduleKeys;
  if (modules != nullptr) {
    for (const auto &moduleValue : *modules) {
      const auto *module = moduleValue.getDynamicObject();
      if (module == nullptr)
        return invalid();
      const auto key = module->getProperty(jsonKeys::key).toString();
      if (key.isEmpty() || moduleKeys.contains(key))
        return invalid();
      moduleKeys.add(key);
    }
  }

  const auto typeIsKnown = [this, &moduleKeys](const cydaw::base::Text &key) {
    return key.isNotEmpty() &&
           (findType(key) != nullptr || moduleKeys.contains(key));
  };
  const auto validateTopology = [&](const cydaw::base::Value &nodeValue,
                                    const cydaw::base::Value &connectionValue,
                                    const bool allowEmpty) {
    const auto *topologyNodes = nodeValue.getArray();
    const auto *topologyConnections = connectionValue.getArray();
    if (topologyNodes == nullptr || (!allowEmpty && topologyNodes->isEmpty()) ||
        topologyNodes->size() > 512 ||
        (topologyConnections != nullptr && topologyConnections->size() > 4'096))
      return false;
    std::set<NodeId> ids;
    for (const auto &nodeValueItem : *topologyNodes) {
      const auto *node = nodeValueItem.getDynamicObject();
      if (node == nullptr)
        return false;
      const auto rawId =
          static_cast<std::int64_t>(node->getProperty(jsonKeys::id));
      const auto type = node->getProperty(jsonKeys::type).toString();
      if (rawId <= 0 || rawId > 1'000'000'000 || !typeIsKnown(type) ||
          !ids.insert(static_cast<NodeId>(rawId)).second)
        return false;
    }
    if (topologyConnections != nullptr)
      for (const auto &connectionItem : *topologyConnections) {
        const auto *connection = connectionItem.getDynamicObject();
        if (connection == nullptr)
          return false;
        const auto source = static_cast<NodeId>(static_cast<std::int64_t>(
            connection->getProperty(jsonKeys::source)));
        const auto destination = static_cast<NodeId>(static_cast<std::int64_t>(
            connection->getProperty(jsonKeys::destination)));
        if (source == destination || !ids.contains(source) ||
            !ids.contains(destination))
          return false;
      }
    return true;
  };

  if (!validateTopology(root->getProperty(jsonKeys::nodes),
                        root->getProperty(jsonKeys::connections), false))
    return invalid();
  if (modules != nullptr)
    for (const auto &moduleValue : *modules) {
      const auto *module = moduleValue.getDynamicObject();
      if (!validateTopology(module->getProperty(jsonKeys::nodes),
                            module->getProperty(jsonKeys::connections), true))
        return invalid();
    }
  return cydaw::base::Result::ok();
}
cydaw::base::Result
GraphDocument::saveUserGraphPreset(const cydaw::base::File &file,
                                   const cydaw::base::Text &name) const {
  if (name.trim().isEmpty() || file == cydaw::base::File{})
    return cydaw::base::Result::fail(tr("nodes.error.invalidUserPresetName"));
  const auto graphValidation = validateGraphAsset();
  if (!graphValidation) {
    const auto message =
        graphValidation.issues.empty()
            ? cydaw::base::Text("Graph validation failed")
            : cydaw::base::Text(graphValidation.issues.front().message);
    return cydaw::base::Result::fail(message);
  }
  auto destination = file;
  if (destination.getFileExtension() != graphPresetFileExtension)
    destination = destination.withFileExtension(graphPresetFileExtension);
  if (!destination.getParentDirectory().createDirectory())
    return cydaw::base::Result::fail(tr("nodes.error.saveUserPreset"));
  GraphPresetMetadata metadata;
  metadata.definitionId = graphReference_.id;
  metadata.version = graphReference_.version;
  metadata.role = inferCurrentGraphRole();
  metadata.scope = cydaw::graph::GraphScope::user;
  metadata.tags = graphTags_;
  metadata.category = graphCategory_;
  metadata.description = graphDescription_;
  metadata.author = graphAuthor_;
  const auto document =
      makeGraphPresetDocument(name, serialise(), std::move(metadata));
  const auto json = cydaw::base::JSON::toString(document, true);
  if (json.getNumBytesAsUTF8() <= 0 ||
      json.getNumBytesAsUTF8() > maximumGraphPresetBytes ||
      !destination.replaceWithText(json))
    return cydaw::base::Result::fail(tr("nodes.error.saveUserPreset"));
  return cydaw::base::Result::ok();
}
cydaw::base::Result
GraphDocument::loadUserGraphPreset(const cydaw::base::File &file) {
  if (!file.existsAsFile() || file.getSize() <= 0 ||
      file.getSize() > maximumGraphPresetBytes)
    return cydaw::base::Result::fail(tr("nodes.error.openUserPreset"));
  const auto document = parseGraphPresetDocument(
      cydaw::base::JSON::parse(file.loadFileAsString()));
  if (!document.has_value())
    return cydaw::base::Result::fail(tr("nodes.error.invalidUserPreset"));
  const auto validation = validateUserGraphPresetState(document->graph);
  if (validation.failed())
    return validation;

  const auto previous = serialise();
  const auto previousFile = currentGraphPresetFile_;
  const auto *expectedRoot = document->graph.getDynamicObject();
  const auto expectedNodeCount =
      expectedRoot->getProperty(jsonKeys::nodes).getArray()->size();
  const auto *expectedConnections =
      expectedRoot->getProperty(jsonKeys::connections).getArray();
  const auto expectedConnectionCount =
      expectedConnections != nullptr ? expectedConnections->size() : 0;
  restore(document->graph);
  graphReference_.id = document->metadata.definitionId.toStdString();
  graphReference_.version = document->metadata.version;
  graphRole_ = document->metadata.role;
  graphScope_ = document->metadata.scope;
  graphTags_ = document->metadata.tags;
  graphCategory_ = document->metadata.category;
  graphAuthor_ = document->metadata.author;
  if (graphDescription_.isEmpty())
    graphDescription_ = document->metadata.description;
  if (graphName_.isEmpty())
    graphName_ = document->name;
  ;
  ;
  ;
  ;
  ;
  ;
  ;
  ;
  const auto restored = serialise();
  const auto *restoredRoot = restored.getDynamicObject();
  const auto *restoredNodes =
      restoredRoot != nullptr
          ? restoredRoot->getProperty(jsonKeys::nodes).getArray()
          : nullptr;
  const auto *restoredConnections =
      restoredRoot != nullptr
          ? restoredRoot->getProperty(jsonKeys::connections).getArray()
          : nullptr;
  if (restoredNodes == nullptr || restoredNodes->size() != expectedNodeCount ||
      (restoredConnections != nullptr ? restoredConnections->size() : 0) !=
          expectedConnectionCount) {
    restore(previous);
    currentGraphPresetFile_ = previousFile;
    return cydaw::base::Result::fail(tr("nodes.error.invalidUserPreset"));
  }

  currentGraphPresetFile_ = file;
  activeFactoryPresetKey_.clear();
  ;
  setStatus(tr("nodes.status.userPresetLoaded", "name", document->name));
  if (layoutChangedHandler_)
    layoutChangedHandler_();
  return cydaw::base::Result::ok();
}
void GraphDocument::setGraphAssetScope(const cydaw::graph::GraphScope scope) {
  graphScope_ = scope;
  refreshGraphAssetValidation();
}
cydaw::graph::GraphReference GraphDocument::graphReference() const {
  return graphReference_;
}
cydaw::graph::GraphRole GraphDocument::graphRole() const noexcept {
  return inferCurrentGraphRole();
}
cydaw::graph::PluginIdentity GraphDocument::pluginExportIdentity() const {
  auto identity = pluginExportIdentity_;
  const auto defaults = defaultVst3ExportIdentity();
  if (identity.productName.empty())
    identity.productName = defaults.productName;
  if (identity.companyName.empty())
    identity.companyName = defaults.companyName;
  if (identity.manufacturerCode.empty())
    identity.manufacturerCode = defaults.manufacturerCode;
  if (identity.pluginCode.empty())
    identity.pluginCode = defaults.pluginCode;
  if (identity.version.major == 0 && identity.version.minor == 0 &&
      identity.version.patch == 0)
    identity.version = defaults.version;
  return identity;
}
void GraphDocument::setPluginExportIdentity(
    cydaw::graph::PluginIdentity identity) {
  identity.productName = cydaw::base::Text(identity.productName)
                             .trim()
                             .substring(0, 128)
                             .toStdString();
  identity.companyName = cydaw::base::Text(identity.companyName)
                             .trim()
                             .substring(0, 128)
                             .toStdString();
  identity.manufacturerCode = cydaw::base::Text(identity.manufacturerCode)
                                  .trim()
                                  .toUpperCase()
                                  .substring(0, 4)
                                  .toStdString();
  identity.pluginCode = cydaw::base::Text(identity.pluginCode)
                            .trim()
                            .toUpperCase()
                            .substring(0, 4)
                            .toStdString();
  pluginExportIdentity_ = std::move(identity);
  notifyLayoutChanged();
}
cydaw::graph::PluginExportResult GraphDocument::planVst3Export() const {
  return planVst3Export(pluginExportIdentity());
}
cydaw::base::Value GraphDocument::createVst3RuntimeState() const {
  const auto exportResult = planVst3Export();
  if (!exportResult)
    return {};

  const auto runtimeExport = runtimeGraphExport();
  if (runtimeExport.compilationResult.failed())
    return {};

  auto root = cydaw::base::Object::Ptr(new cydaw::base::Object());
  root->setProperty(jsonKeys::schema, 2);
  root->setProperty(jsonKeys::editor, serialise());
  // A sampler's "factory:" and "bank:" samples, found by the plug-in where
  // CyDAW keeps them on this machine.
  if (const auto factory = factoryContentFolder(); factory.isDirectory())
    root->setProperty("factoryFolder", factory.getFullPathName());
  if (const auto banks = banksFolder(); banks.isDirectory())
    root->setProperty("banksFolder", banks.getFullPathName());
  // The cards switched off stay off in the plug-in (they were left out:
  // a device bypassed in CyDAW played in its export).
  root->setProperty(jsonKeys::native,
                    serialiseNativeDspGraphState(
                        withCardBypasses(runtimeExport.nativeGraph)));
  root->setProperty(jsonKeys::polySynth,
                    serialisePolySynthGraphPlan(runtimeExport.instrumentPlan));
  // Each Replacer's instrument: its plan and its graph, as its card keeps
  // them (the studio compiles them), for the plug-in to play.
  {
    cydaw::base::Array<cydaw::base::Value> replacers;
    for (const auto &native : runtimeExport.nativeGraph.nodes) {
      if (native.kind != cydaw::engine::NativeDspNodeKind::replacer)
        continue;
      const auto *node = readNode(native.id);
      if (node == nullptr ||
          !node->instrumentJson.getProperty("plan", cydaw::base::Value())
               .isObject())
        continue;
      auto entry = cydaw::base::Object::Ptr(new cydaw::base::Object());
      entry->setProperty("node", static_cast<std::int64_t>(native.id));
      entry->setProperty("polySynth", node->instrumentJson.getProperty(
                                          "plan", cydaw::base::Value()));
      entry->setProperty("editor", node->instrumentJson.getProperty(
                                       "graph", cydaw::base::Value()));
      replacers.add(cydaw::base::Value(entry.get()));
    }
    if (!replacers.isEmpty())
      root->setProperty("replacers", replacers);
  }

  cydaw::base::Array<cydaw::base::Value> publishedValues, allValues;
  cydaw::base::Array<cydaw::base::Value> hostValues;
  for (const auto &parameter : runtimeExport.publishedParameters) {
    auto value = cydaw::base::Object::Ptr(new cydaw::base::Object());
    value->setProperty(jsonKeys::id, static_cast<std::int64_t>(parameter.id));
    value->setProperty(jsonKeys::key, cydaw::base::Text(parameter.key));
    value->setProperty(jsonKeys::name,
                       cydaw::base::Text(parameter.displayName));
    value->setProperty(jsonKeys::unit, cydaw::base::Text(parameter.unit));
    value->setProperty(jsonKeys::minimum, parameter.minimum);
    value->setProperty(jsonKeys::maximum, parameter.maximum);
    value->setProperty(jsonKeys::default_, parameter.defaultValue);
    value->setProperty(jsonKeys::targetNodeId,
                       static_cast<std::int64_t>(parameter.targetNodeId));
    value->setProperty(jsonKeys::targetParameter,
                       cydaw::base::Text(parameter.targetParameterKey));
    value->setProperty(jsonKeys::automatable, parameter.automatable);
    value->setProperty(jsonKeys::modulatable, parameter.modulatable);
    value->setProperty(jsonKeys::visibleInDevice, parameter.visibleInDevice);
    value->setProperty(jsonKeys::exportedToHost, parameter.exportedToHost);
    allValues.add(cydaw::base::Value(value.get()));
    if (!parameter.exportedToHost)
      continue;
    publishedValues.add(cydaw::base::Value(value.get()));
    const auto extent = parameter.maximum - parameter.minimum;
    hostValues.add(
        extent > 0.0
            ? base::limit(0.0, 1.0,
                          (parameter.defaultValue - parameter.minimum) / extent)
            : 0.0);
  }
  cydaw::base::Array<cydaw::base::Value> voiceModulations;
  for (const auto &modulation : editorVoiceModulations()) {
    auto *value = new cydaw::base::Object();
    value->setProperty("source",
                       static_cast<std::int64_t>(modulation.controlNodeId));
    value->setProperty("target", static_cast<int>(modulation.target));
    value->setProperty("depth", modulation.depth);
    value->setProperty("index", static_cast<int>(modulation.index));
    value->setProperty("minimum", modulation.minimum);
    value->setProperty("maximum", modulation.maximum);
    value->setProperty("logarithmic", modulation.logarithmic);
    voiceModulations.add(cydaw::base::Value(value));
  }
  root->setProperty("voiceModulations", voiceModulations);
  root->setProperty("allParameters", allValues);
  root->setProperty(jsonKeys::publishedParameters, publishedValues);
  root->setProperty(jsonKeys::hostValues, hostValues);
  // The device's programs, as the plugin's: each its parameter values, in the
  // published order (a setting a program lacks keeps its default).
  if (!programs_.empty()) {
    cydaw::base::Array<cydaw::base::Value> programs;
    for (const auto &program : programs_) {
      cydaw::base::Array<cydaw::base::Value> values, allProgramValues;
      for (const auto &parameter : runtimeExport.publishedParameters) {
        auto value = parameter.defaultValue;
        const auto key = cydaw::base::Text(parameter.key);
        for (const auto &[programKey, programValue] : program.values)
          if (programKey == key)
            value = programValue;
        const auto extent = parameter.maximum - parameter.minimum;
        const auto normalised =
            extent > 0.0
                ? base::limit(0.0, 1.0, (value - parameter.minimum) / extent)
                : 0.0;
        allProgramValues.add(normalised);
        if (parameter.exportedToHost)
          values.add(normalised);
      }
      auto entry = cydaw::base::Object::Ptr(new cydaw::base::Object());
      entry->setProperty("name", program.name);
      entry->setProperty("hostValues", values);
      entry->setProperty("allValues", allProgramValues);
      programs.add(cydaw::base::Value(entry.get()));
    }
    root->setProperty("programs", programs);
  }
  return cydaw::base::Value(root.get());
}
cydaw::base::Result
GraphDocument::generateVst3Project(const cydaw::base::File &destination,
                                   const cydaw::base::Text &format) const {
  const auto exportResult = planVst3Export();
  if (!exportResult)
    return cydaw::base::Result::fail(
        exportResult.issues.empty()
            ? "The Graph is not ready for VST3 export"
            : cydaw::base::Text(exportResult.issues.front().message));
  const auto runtime = runtimeGraphExport();
  if (runtime.compilationResult.failed())
    return runtime.compilationResult;
  return generateGraphVst3Project(
      destination, *exportResult.plan,
      serialisePluginExportPlan(*exportResult.plan, serialise()),
      createVst3RuntimeState(), format);
}
cydaw::graph::GraphRole GraphDocument::inferCurrentGraphRole() const {
  using Role = cydaw::graph::GraphRole;
  if (valueGraphMode_)
    return Role::modulator;
  if (graphRole_ != Role::generic)
    return graphRole_;
  const auto contains = [this](const std::initializer_list<const char *> keys) {
    return std::ranges::any_of(nodes_, [&keys](const Node &node) {
      return std::ranges::any_of(
          keys, [&node](const char *key) { return node.typeKey == key; });
    });
  };
  const auto audioInput =
      contains({"audio-clips"}) ||
      std::ranges::any_of(nodes_, [](const Node &node) {
        return node.typeKey.startsWith("graph-audio-input-");
      });
  const auto audioOutput =
      contains({"master"}) || std::ranges::any_of(nodes_, [](const Node &node) {
        return node.typeKey.startsWith("graph-audio-output-");
      });
  const auto midiInput = contains({"midi-input", "graph-midi-input"});
  const auto midiOutput = contains({"midi-output", "graph-midi-output"});
  // A hard-coded list only knew about poly-sine, so every other polyphonic
  // module — poly-lead, poly-pad, poly-bass, poly-keys and the rest — made a
  // graph that was classified as a plain audio effect and therefore never
  // produced a voice plan. Ask the module what it is instead.
  const auto generator =
      std::ranges::any_of(
          nodes_,
          [](const Node &node) {
            // "poly-synth" is a legacy composite with no primitive of its
            // own, so it is named here rather than in the shared list.
            return node.typeKey == "poly-synth" ||
                   cydaw::graph::isSoundSourcePrimitive(
                       detail::primitiveProcessorKeyForType(node.typeKey)
                           .toStdString());
          }) ||
      std::ranges::any_of(nodes_, [this](const Node &node) {
        const auto *module = findModule(node.typeKey);
        return module != nullptr &&
               inferModuleRole(*module) == cydaw::graph::GraphRole::instrument;
      });
  // graph::inferGraphRole reads a graph that also takes audio in as an
  // effect, and this had no such term — so a resonator driven by MIDI and
  // fed from audio exported as a synth that the host then never fed.
  if (midiInput && audioOutput && generator && !audioInput)
    return Role::instrument;
  if (midiInput && midiOutput && !audioOutput)
    return Role::midiEffect;
  if (audioInput && audioOutput)
    return Role::audioEffect;
  return Role::generic;
}
void GraphDocument::applyWavetableHarmonics(
    Node &node, const cydaw::base::Array<cydaw::base::Value> &harmonics) {
  using Shape = cydaw::engine::WavetableShape;
  if (harmonics.size() !=
      static_cast<int>(Shape::frameCount * Shape::harmonicCount))
    return;
  Shape shape;
  auto index = 0;
  for (auto &frame : shape.harmonics)
    for (auto &amplitude : frame)
      amplitude = static_cast<float>(static_cast<double>(harmonics[index++]));
  node.wavetableShape = std::make_shared<const Shape>(shape);
  node.wavetableBank = cydaw::engine::buildWavetableBank(shape);
}
bool GraphDocument::setNodeWavetable(
    const NodeId nodeId, const cydaw::engine::WavetableShape &shape) {
  auto *node = findNode(nodeId);
  if (node == nullptr || node->typeKey != "wavetable")
    return false;
  node->wavetableShape =
      std::make_shared<const cydaw::engine::WavetableShape>(shape);
  // Storing the shape is a pointer swap. Rendering the bank it implies is
  // six hundred and fifty thousand sines, so it is deferred and coalesced:
  // a drag asks for it on every notch and pays for it once.
  wavetableBankPending_.insert(nodeId);
  pendingChanges_ = true;
  ;
  return true;
}
bool GraphDocument::setNodeWavetable(
    const cydaw::base::Text &nodeTypeKey,
    const cydaw::engine::WavetableShape &shape) {
  return setNodeWavetable(nodeIdForType(nodeTypeKey), shape);
}
cydaw::engine::WavetableShape
GraphDocument::nodeWavetable(const cydaw::base::Text &nodeTypeKey) const {
  return nodeWavetable(nodeIdForType(nodeTypeKey));
}
GraphDocument::NodeId
GraphDocument::nodeIdForType(const cydaw::base::Text &typeKey) const {
  const auto found =
      std::find_if(nodes_.begin(), nodes_.end(), [&typeKey](const Node &node) {
        return node.typeKey == typeKey;
      });
  return found != nodes_.end() ? found->id : NodeId{};
}
cydaw::engine::WavetableShape
GraphDocument::nodeWavetable(const NodeId nodeId) const {
  if (const auto *node = readNode(nodeId);
      node != nullptr && node->wavetableShape != nullptr)
    return *node->wavetableShape;
  return cydaw::engine::defaultWavetableShape();
}
void GraphDocument::setEffectParameters(const float driveDb,
                                        const float filterCutoffHz,
                                        const bool enabled) {
  auto mark = cydaw::base::Time::getHighResolutionTicks();
  const auto since = [&mark] {
    const auto now = cydaw::base::Time::getHighResolutionTicks();
    const auto elapsed =
        cydaw::base::Time::highResolutionTicksToSeconds(now - mark) * 1000.0;
    mark = now;
    return cydaw::base::Text(elapsed, 3);
  };
  ;
  ;
  ;
  ;
  // Only the cards this actually writes to give their cached description
  // back: a fader notch on a three-hundred-card patch must not re-describe
  // the two hundred and ninety-eight it did not touch.
  std::vector<NodeId> touched;
  for (auto &node : nodes_) {
    if (node.typeKey == "drive")
      node.parameters.driveDb = base::limit(0.0f, 36.0f, driveDb);
    else if (node.typeKey == "lowpass")
      node.parameters.cutoffHz = base::limit(20.0f, 20'000.0f, filterCutoffHz);
    else
      continue;
    node.serialised = cydaw::base::Value();
    touched.push_back(node.id);
    for (auto &parameter : node.interfaceState.parameters)
      if (bindingMatchesType(node.typeKey, parameter.key))
        parameter.value = nativeParameterValue(node.parameters, parameter.key);
  }
  const auto written = since();
  // A notch changes values, not the shape of the interface: no card
  // appeared, none went away, none published anything new. Rebuilding the
  // published list from every node — which is what synchronising does —
  // cost more than everything else on this path put together, and grew
  // with the patch. Copy the new values onto the parameters that carry
  // them instead.
  for (const auto id : touched) {
    const auto prefix = nodeParameterBinding(id, {});
    const auto *node = readNode(id);
    if (node == nullptr)
      continue;
    for (auto &parameter : graphInterfaceState_.parameters)
      if (parameter.key.startsWith(prefix))
        parameter.value = nativeParameterValue(node->parameters, parameter.key);
  }
  const auto synchronised = since();
  ;
  ;
  ;
  ;
  ;
  ;
  ;
  ;
  ;
  ;
  effectParametersProfile_ =
      "write=" + written + " synchronise=" + synchronised +
      " embedded=" + since() +
      " nodes=" + cydaw::base::Text(static_cast<int>(nodes_.size()));
}
namespace {
cydaw::base::Text upperCasedLabel(const cydaw::base::Text &text) {
  cydaw::base::Text result;
  for (int characterIndex = 0; characterIndex < text.length();
       ++characterIndex) {
    const auto character = text[characterIndex];
    result += cydaw::base::Text::charToString(
        character >= 0xe0 && character <= 0xfe && character != 0xf7
            ? static_cast<char32_t>(character - 0x20)
            : base::CharacterFunctions::toUpperCase(character));
  }
  return result;
}
} // namespace
void GraphDocument::invalidateSerialisedNodes() {
  for (const auto &node : nodes_)
    node.serialised = cydaw::base::Value();
}
cydaw::base::Result
GraphDocument::applyFactoryPreset(const FactoryPreset &preset) {
  // Locked, nothing is replaced (the cards could not be added again).
  if (editingLocked_)
    return cydaw::base::Result::fail("The Graph is locked");
  std::vector<const NodeType *> types;
  types.reserve(preset.nodes.size());
  for (const auto &presetNode : preset.nodes) {
    const auto *type = findType(presetNode.typeKey);
    if (type == nullptr)
      return cydaw::base::Result::fail(
          "Factory preset references an unknown node: " + presetNode.typeKey);
    types.push_back(type);
  }
  for (const auto &connection : preset.connections)
    if (connection.sourceIndex >= preset.nodes.size() ||
        connection.destinationIndex >= preset.nodes.size())
      return cydaw::base::Result::fail(
          "Factory preset contains an invalid connection");

  nodes_.clear();
  currentGraphPresetFile_ = cydaw::base::File{};
  connections_.clear();
  selectedNodes_.clear();
  // The old graph's programs and notes go with it: their values name its
  // cards by ids the new cards take again from 1.
  programs_.clear();
  comments_.clear();
  selectedCommentId_ = 0;
  graphRole_ = cydaw::graph::GraphRole::generic;
  nextNodeId_ = 1;
  std::vector<NodeId> ids;
  ids.reserve(types.size());
  for (std::size_t index = 0; index < types.size(); ++index) {
    addNodeFromType(*types[index], preset.nodes[index].position);
    nodes_.back().parameters = preset.nodes[index].parameters;
    // The sockets read what the device tuned, not the schema's values.
    if (findModule(nodes_.back().typeKey) == nullptr)
      applyParameterPortDefaults(nodes_.back().typeKey,
                                 nodes_.back().parameters, nodes_.back().ports);
    nodes_.back().interfaceState = createNodeInterface(nodes_.back());
    ids.push_back(nodes_.back().id);
  }
  for (const auto &connection : preset.connections)
    if (!connectNodes(ids[connection.sourceIndex],
                      ids[connection.destinationIndex], connection.sourcePort,
                      connection.destinationPort))
      return cydaw::base::Result::fail(
          "Factory preset could not create a valid DSP connection: " +
          statusMessage_);

  auto hasDrive = false;
  auto hasLowpass = false;
  for (const auto &node : nodes_) {
    if (node.typeKey == "drive") {
      ;
      hasDrive = true;
    } else if (node.typeKey == "lowpass") {
      ;
      hasLowpass = true;
    }
  }
  if (!hasDrive)
    ;
  if (!hasLowpass)
    ;
  if (effectChangedHandler_) {
    float drive = 0.f, cutoff = 20000.f;
    for (const auto &node : nodes_) {
      if (node.typeKey == "drive")
        drive = node.parameters.driveDb;
      if (node.typeKey == "lowpass")
        cutoff = node.parameters.cutoffHz;
    }
    effectChangedHandler_(drive, cutoff);
  }

  graphInterfaceState_ = createDefaultGraphInterface();
  graphRole_ = inferCurrentGraphRole();
  ;
  ;
  selectedNodes_.clear();
  activeFactoryPresetKey_ = preset.key;
  ;
  setStatus(tr("nodes.status.presetLoaded", "name", tr(preset.translationKey)));
  ;
  refreshGraphAssetValidation();
  commitEditHistory(false);
  if (layoutChangedHandler_)
    layoutChangedHandler_();
  return cydaw::base::Result::ok();
}
cydaw::engine::NativeDspNodeParameters GraphDocument::defaultParametersForType(
    const cydaw::base::Text &typeKey) const {
  cydaw::engine::NativeDspNodeParameters parameters;
  if (const auto *effect = cydaw::engine::effectForType(typeKey.toStdString()))
    cydaw::engine::applyEffectDefaults(*effect, parameters);
  if (typeKey == "reverb-rack") {
    parameters.roomSize = 0.78f;
    parameters.mix = 0.34f;
  }
  if (typeKey == "voice-filter")
    parameters.cutoffHz = 12'000.0f;
  // A sound model and a sampler play at their recording's level.
  if (typeKey == "sound-model" || typeKey == "sampler" ||
      typeKey == "prism-synth" || typeKey == "kick-synth" ||
      typeKey == "batterie")
    parameters.oscillatorLevel = 1.0f;
  // A Batterie: every hit to its end, every key its recording.
  if (typeKey == "batterie") {
    parameters.sampler[static_cast<std::size_t>(
        cydaw::engine::SamplerSetting::oneShot)] = 1.0f;
    parameters.sampler[static_cast<std::size_t>(
        cydaw::engine::SamplerSetting::keyTrack)] = 0.0f;
  }
  // A synth of the family: its model's own settings.
  if (const auto model =
          cydaw::engine::synth::modelForType(typeKey.toStdString());
      model != cydaw::engine::synth::Model::none) {
    parameters.oscillatorLevel = 1.0f;
    parameters.synthModel = static_cast<float>(model);
    parameters.synth = cydaw::engine::synth::defaultValues(model);
  }
  if (typeKey == "highpass")
    parameters.highpassCutoffHz = 80.0f;
  if (typeKey == "hard-clip")
    parameters.clipThreshold = 0.9f;
  if (typeKey == "stereo-width")
    parameters.stereoWidth = 1.0f;
  if (typeKey == "lfo" || typeKey == "lfo-core") {
    parameters.modulationRateHz = 1.0f;
    parameters.modulationDepth = 1.0f;
    parameters.modulationOffset = 0.0f;
    parameters.modulationShape = 0.0f;
  } else if (typeKey == "macro") {
    parameters.modulationOffset = 0.0f;
  } else if (typeKey == "control-math") {
    parameters.modulationDepth = 1.0f;
    parameters.modulationOffset = 0.0f;
  } else if (typeKey == "value-constant") {
    parameters.valueOperand = 0.0f;
  } else if (typeKey == "map-range") {
    parameters.mapInputMinimum = 0.0f;
    parameters.mapInputMaximum = 1.0f;
    parameters.mapOutputMinimum = 0.0f;
    parameters.mapOutputMaximum = 1.0f;
  } else if (typeKey == "envelope-follower") {
    parameters.followerAttackSeconds = 0.01f;
    parameters.followerReleaseSeconds = 0.1f;
  } else if (typeKey.startsWith("value-") || typeKey.startsWith("logic-")) {
    parameters.valueOperand =
        typeKey == "value-multiply" || typeKey == "value-divide" ? 1.0f : 0.0f;
    parameters.valueMinimum = -1.0f;
    parameters.valueMaximum = 1.0f;
  } else if (typeKey == "note-filter") {
    parameters.midiMinimumNote = 0.0f;
    parameters.midiMaximumNote = 127.0f;
  } else if (typeKey == "velocity-map") {
    parameters.midiVelocityScale = 1.0f;
  }
  if (const auto *type = findType(typeKey)) {
    auto inputIndex = 0;
    for (const auto &port : type->ports)
      if (port.input && (port.type == cydaw::graph::PortType::value ||
                         port.type == cydaw::graph::PortType::trigger)) {
        if (inputIndex == 0)
          parameters.valueInputA = static_cast<float>(port.defaultValue);
        else if (inputIndex == 1)
          parameters.valueInputB = static_cast<float>(port.defaultValue);
        ++inputIndex;
      }
  }
  return parameters;
}
ModuleInterfaceState
GraphDocument::createNodeInterface(const Node &node) const {
  if (const auto *module = findModule(node.typeKey))
    return createGraphInstanceInterface(node.id, *module);

  ModuleInterfaceState state;
  if (isBoundaryNode(node))
    return state;
  appendPublishedParameters(state, node.typeKey, node.id, node.parameters,
                            node.samplerMap.get());
  const auto dedicatedVisual =
      node.typeKey == "oscillator" || node.typeKey == "noise" ||
      node.typeKey == "lfo" || node.typeKey == "lfo-core" ||
      node.typeKey == "spectrum" || node.typeKey == "lowpass" ||
      node.typeKey == "voice-filter" || node.typeKey == "wavetable" ||
      node.typeKey == "compressor";
  if (!state.parameters.empty() || dedicatedVisual)
    layoutPublishedInterface(state, displayTitle(node),
                             node.typeKey == "spectrum", dedicatedVisual);
  if (node.typeKey == "oscilloscope" || node.typeKey == "level-meter" ||
      node.typeKey == "value-display" || node.typeKey == "midi-monitor") {
    state.controls.clear();
    state.nextControlId = 1;
    const auto kind =
        node.typeKey == "level-meter" ? ModuleControlKind::levelMeter
        : node.typeKey == "value-display" || node.typeKey == "midi-monitor"
            ? ModuleControlKind::text
            : ModuleControlKind::waveform;
    state.controls.push_back({state.nextControlId++,
                              kind,
                              displayTitle(node),
                              {},
                              0.06f,
                              0.08f,
                              0.88f,
                              0.78f});
    rebuildAutomaticDeviceControls(state);
  }
  if (node.ports.empty())
    appendInterfacePorts(
        state, node.hasInput, static_cast<std::uint8_t>(node.inputKind),
        node.hasOutput, static_cast<std::uint8_t>(node.outputKind),
        "node." + cydaw::base::Text(static_cast<std::int64_t>(node.id)) +
            ".input",
        "node." + cydaw::base::Text(static_cast<std::int64_t>(node.id)) +
            ".output",
        0.40f, 0.40f, {}, {}, audioInputChannels(node),
        audioOutputChannels(node));
  return state;
}
ModuleInterfaceState
GraphDocument::createGraphInstanceInterface(const NodeId instanceId,
                                            const SavedModule &module) const {
  auto state = module.interfaceState;
  const auto prefix = nodeParameterBinding(instanceId, {});
  std::vector<std::pair<cydaw::base::Text, cydaw::base::Text>> bindings;
  bindings.reserve(state.parameters.size());

  // A module holds each value twice: on the child node that the user edits,
  // and again on the interface template that publishes it. Copying the
  // template wholesale carried the second, stale copy into every new
  // instance, and the instance override then won — so editing a reusable
  // Graph's oscillator changed nothing about what it played. The node is the
  // source of truth; the template only says which parameters are published.
  const auto childNodeId = [](const cydaw::base::Text &key) -> NodeId {
    if (!key.startsWith("node."))
      return 0;
    const auto separator = key.indexOfChar(5, '.');
    return separator <= 5 ? 0
                          : static_cast<NodeId>(
                                key.substring(5, separator).getLargeIntValue());
  };

  for (auto &parameter : state.parameters) {
    const auto classKey = parameter.key;
    if (const auto owner = std::ranges::find(
            module.nodes, childNodeId(classKey), &ModuleNode::id);
        owner != module.nodes.end()) {
      const auto primitiveKey =
          primitiveParameterKeyForBinding(parameterSuffix(classKey));
      if (const auto authored = primitiveParameterValueForType(
              owner->typeKey, primitiveKey, owner->parameters))
        parameter.value =
            base::limit(parameter.minimum, parameter.maximum, *authored);
    }
    parameter.key = prefix + classKey;
    parameter.hostParameterId = stableModuleParameterId(parameter.key);
    bindings.emplace_back(classKey, parameter.key);
  }
  for (const auto mode :
       {ModuleInterfaceLayoutMode::graph, ModuleInterfaceLayoutMode::device})
    for (auto &control : moduleInterfaceControls(state, mode))
    // Every binding a control carries, not only its first: an
    // envelope names four parameters and a response plot two. With
    // just the primary rewritten, the others still pointed at the
    // module's own keys, so an instance showed an empty ADSR and an
    // empty filter plot that nothing could edit.
    {
      std::vector<cydaw::base::Text *> targets{
          &control.parameterBinding, &control.secondaryParameterBinding,
          &control.tertiaryParameterBinding,
          &control.quaternaryParameterBinding};
      for (auto &extra : control.extraParameterBindings)
        targets.push_back(&extra);
      for (auto *binding : targets)
        if (const auto found =
                std::ranges::find_if(bindings,
                                     [binding](const auto &entry) {
                                       return entry.first == *binding;
                                     });
            found != bindings.end())
          *binding = found->second;
    }
  return state;
}
void GraphDocument::addNodeFromType(const NodeType &type,
                                    base::Point<float> position) {
  if (editingLocked_)
    return;
  // A sentinel position means the caller had no drop point of its own — the
  // palette, a menu, the command line. The real placement is chosen further
  // down, once the card's size is known.
  const auto choosePlacement = position.x <= -999.0f || position.y <= -999.0f ||
                               !std::isfinite(position.x) ||
                               !std::isfinite(position.y);
  if (choosePlacement)
    position = {0.12f, 0.20f};
  position = graphPosition(position);
  const auto id = nextNodeId_++;
  Node node{id,
            type.key,
            type.title,
            type.colour,
            position,
            type.hasInput,
            type.inputKind,
            type.hasOutput,
            type.outputKind,
            type.protectedNode,
            type.composite};
  node.inputChannels = type.inputChannels;
  node.outputChannels = type.outputChannels;
  node.ports = type.ports;
  if (primitiveSupportsVariableAudioChannels(type.key)) {
    auto graphChannels = std::uint8_t{2};
    auto inheritedFromSelection = false;
    for (const auto selectedId : selectedNodes_) {
      const auto *selected = findNode(selectedId);
      bool input{};
      if (selected == nullptr ||
          !isGraphAudioBoundaryKey(selected->typeKey, &input))
        continue;
      graphChannels = input ? audioOutputChannels(*selected)
                            : audioInputChannels(*selected);
      inheritedFromSelection = graphChannels != 0;
      if (inheritedFromSelection)
        break;
    }
    if (!inheritedFromSelection) {
      // Infer a format only when the Graph has one unambiguous audio
      // topology. Multi-bus Graphs keep stereo as the insertion default
      // until the user selects a typed boundary or changes the format.
      std::set<std::uint8_t> graphFormats;
      for (const auto &existing : nodes_) {
        bool input{};
        if (!isGraphAudioBoundaryKey(existing.typeKey, &input))
          continue;
        const auto existingChannels = input ? audioOutputChannels(existing)
                                            : audioInputChannels(existing);
        if (existingChannels != 0)
          graphFormats.insert(existingChannels);
      }
      if (graphFormats.size() == 1)
        graphChannels = *graphFormats.begin();
    }
    graphChannels = static_cast<std::uint8_t>(
        base::limit(1, 8, static_cast<int>(graphChannels)));
    if (node.inputKind == PortKind::audio)
      node.inputChannels = graphChannels;
    if (node.outputKind == PortKind::audio)
      node.outputChannels = graphChannels;
    for (auto &port : node.ports)
      if (port.type == cydaw::graph::PortType::audio)
        port.channels = graphChannels;
  }
  auto mark = cydaw::base::Time::getHighResolutionTicks();
  const auto since = [&mark] {
    const auto now = cydaw::base::Time::getHighResolutionTicks();
    const auto elapsed =
        cydaw::base::Time::highResolutionTicksToSeconds(now - mark) * 1000.0;
    mark = now;
    return cydaw::base::Text(elapsed, 2);
  };
  node.parameters = defaultParametersForType(type.key);
  applyLegacyPortDefaults(node.typeKey, node.parameters, node.ports);
  const auto defaults = since();
  node.interfaceState = createNodeInterface(node);
  const auto interface = since();
  const auto addedId = node.id;
  const auto preferredPosition = node.position;
  nodes_.push_back(std::move(node));
  synchroniseGraphInterface();
  const auto synchronise = since();
  // Deferred and coalesced, the way a restore already does it: rebuilding
  // every card's embedded controls because one card appeared cost eight of
  // the nine milliseconds a node placement took. A card's drawn size comes
  // from its interface state, not from these components, so the placement
  // below still measures the right rectangle.
  restoreUiPending_ = true;
  pendingChanges_ = true;
  const auto sliders = since();
  addProfile_ = "defaults=" + defaults + " interface=" + interface +
                " synchronise=" + synchronise + " sliders=" + sliders;
  if (choosePlacement)
    // Only now is the card's drawn size final: publishing an interface can
    // grow it well past the base size, and probing the wrong size finds
    // gaps the real card does not fit into.
    if (auto *placed = findNode(addedId))
      placed->position = freePlacementFor(*placed, preferredPosition, addedId);
  selectedNodes_.clear();
  selectedNodes_.insert(id);
  selectedCommentId_ = 0;
  if (choosePlacement)
    // At this zoom the canvas holds about four cards, so a node added to a
    // busy graph legitimately lands outside the view. Inserting something
    // and not seeing it is worse than the pile this replaced. Pan to it
    // rather than reframe: changing the zoom under the user is what the
    // Frame command is for, not what inserting a node should do.
    ;
  const auto placement = since();
  ;
  const auto details = since();
  ;
  const auto layout = since();
  ;
  addProfile_ += " placement=" + placement + " details=" + details +
                 " layout=" + layout + " tail=" + since();
}
bool GraphDocument::spliceNodeIntoConnection(
    const int connectionIndex, const NodeType &insertedType,
    const base::Point<float> normalisedPosition) {
  if (editingLocked_ || connectionIndex < 0 ||
      connectionIndex >= static_cast<int>(connections_.size()))
    return false;

  const auto original = connections_[static_cast<std::size_t>(connectionIndex)];
  const auto *sourceNode = findNode(original.source);
  const auto *destinationNode = findNode(original.destination);
  const auto *sourcePort = sourceNode != nullptr
                               ? findPort(*sourceNode, original.sourcePort)
                               : nullptr;
  const auto *destinationPort =
      destinationNode != nullptr
          ? findPort(*destinationNode, original.destinationPort)
          : nullptr;
  if (sourceNode == nullptr || destinationNode == nullptr ||
      sourcePort == nullptr || destinationPort == nullptr)
    return false;

  const auto matches = [](const NodePort &candidate, const NodePort &contract,
                          const bool input) {
    return candidate.input == input && candidate.type == contract.type &&
           (candidate.type != cydaw::graph::PortType::audio ||
            candidate.channels == contract.channels);
  };
  const auto input =
      std::ranges::find_if(insertedType.ports, [&](const NodePort &port) {
        return matches(port, *sourcePort, true);
      });
  const auto output =
      std::ranges::find_if(insertedType.ports, [&](const NodePort &port) {
        return matches(port, *destinationPort, false);
      });
  if (input == insertedType.ports.end() || output == insertedType.ports.end())
    return false;

  addNodeFromType(insertedType, normalisedPosition);
  const auto insertedId = nodes_.back().id;
  connections_.erase(connections_.begin() + connectionIndex);
  const auto inputConnected =
      connectNodes(original.source, insertedId, original.sourcePort, input->id);
  const auto outputConnected =
      inputConnected && connectNodes(insertedId, original.destination,
                                     output->id, original.destinationPort);
  if (!outputConnected) {
    std::erase_if(connections_, [insertedId](const Connection &item) {
      return item.source == insertedId || item.destination == insertedId;
    });
    std::erase_if(nodes_, [insertedId](const Node &node) {
      return node.id == insertedId;
    });
    connections_.insert(
        connections_.begin() +
            std::min(connectionIndex, static_cast<int>(connections_.size())),
        original);
    synchroniseGraphInterface();
    ;
    ;
    return false;
  }
  // A cable that modulated a knob still does, by as much, from the card
  // put on its way.
  for (auto &connection : connections_)
    if (connection.source == insertedId &&
        connection.destination == original.destination &&
        connection.destinationPort == original.destinationPort) {
      connection.mode = original.mode;
      connection.amount = original.amount;
    }

  selectedNodes_.clear();
  selectedNodes_.insert(insertedId);
  selectedPortId_ = 0;
  selectedConnection_ = -1;
  selectedCommentId_ = 0;
  synchroniseGraphInterface();
  ;
  ;
  refreshGraphAssetValidation();
  ;
  ;
  setStatus(tr("nodes.status.spliced")
                .replace("{name}", displayTitle(nodes_.back())));
  notifyLayoutChanged();
  return true;
}
void GraphDocument::removeSelectedNodes() {
  if (editingLocked_)
    return;
  if (selectedCommentId_ != 0) {
    const auto removedId = selectedCommentId_;
    std::erase_if(comments_, [removedId](const auto &comment) {
      return comment.id == removedId;
    });
    selectedCommentId_ = 0;
    ;
    setStatus(tr("nodes.comment.removed"));
    ;
    notifyLayoutChanged();
    return;
  }
  std::set<NodeId> removable;
  for (const auto id : selectedNodes_)
    if (const auto *node = findNode(id);
        node != nullptr && !node->protectedNode)
      removable.insert(id);
  if (removable.empty()) {
    setStatus(tr("nodes.status.factoryProtected"), true);
    return;
  }
  std::erase_if(connections_, [&removable](const auto &connection) {
    return removable.contains(connection.source) ||
           removable.contains(connection.destination);
  });
  std::erase_if(nodes_, [&removable](const auto &node) {
    return removable.contains(node.id);
  });
  synchroniseGraphInterface();
  ;
  selectedNodes_.clear();
  ;
  setStatus(tr("nodes.status.removed"));
  ;
  ;
  notifyLayoutChanged();
}
void GraphDocument::duplicateSelectedNodes() {
  if (editingLocked_)
    return;
  std::vector<Node> copies;
  std::set<NodeId> copiedIds;
  for (const auto id : selectedNodes_)
    if (const auto *node = findNode(id)) {
      copies.push_back(*node);
      copiedIds.insert(id);
    }
  if (copies.empty())
    return;
  std::vector<Connection> copiedConnections;
  for (const auto &connection : connections_)
    if (copiedIds.contains(connection.source) &&
        copiedIds.contains(connection.destination))
      copiedConnections.push_back(connection);
  std::unordered_map<NodeId, NodeId> remappedIds;
  selectedNodes_.clear();
  for (auto &copy : copies) {
    const auto previousId = copy.id;
    copy.id = nextNodeId_++;
    remappedIds.insert_or_assign(previousId, copy.id);
    copy.protectedNode = false;
    copy.position =
        graphPosition({copy.position.x + 0.05f, copy.position.y + 0.08f});
    const auto previousPrefix = nodeParameterBinding(previousId, {});
    const auto nextPrefix = nodeParameterBinding(copy.id, {});
    for (auto &parameter : copy.interfaceState.parameters)
      if (parameter.key.startsWith(previousPrefix))
        parameter.key =
            nextPrefix + parameter.key.substring(previousPrefix.length());
    for (const auto mode :
         {ModuleInterfaceLayoutMode::graph, ModuleInterfaceLayoutMode::device})
      for (auto &control : moduleInterfaceControls(copy.interfaceState, mode))
        rebindControlPrefix(control, previousPrefix, nextPrefix);
    selectedNodes_.insert(copy.id);
    nodes_.push_back(std::move(copy));
  }
  for (auto connection : copiedConnections) {
    const auto source = remappedIds.find(connection.source);
    const auto destination = remappedIds.find(connection.destination);
    if (source == remappedIds.end() || destination == remappedIds.end())
      continue;
    connection.source = source->second;
    connection.destination = destination->second;
    for (auto &point : connection.routingPoints)
      point = graphPosition(point + base::Point<float>{0.05f, 0.08f});
    connections_.push_back(std::move(connection));
  }
  remapModulatorOwners(remappedIds);
  synchroniseGraphInterface();
  ;
  setStatus(tr("nodes.status.duplicated"));
  ;
  ;
  ;
  notifyLayoutChanged();
}
cydaw::base::Result
GraphDocument::createEditableGraphModule(const cydaw::graph::GraphRole role,
                                         const bool openEditor,
                                         const int audioChannels) {
  using Role = cydaw::graph::GraphRole;
  SavedModule module;
  module.key = "inline-graph-" +
               base::Uuid().toString().removeCharacters("{}-").toLowerCase();
  module.inlineModule = true;
  module.factoryModule = false;
  module.role = role;
  const auto index = cydaw::base::Text(nextModuleIndex_++);
  if (role == Role::instrument)
    module.name = tr("nodes.newInstrumentName", "index", index);
  else if (role == Role::midiEffect)
    module.name = tr("nodes.newMidiEffectName", "index", index);
  else if (role == Role::modulator)
    module.name = tr("nodes.newModulatorName", "index", index);
  else if (role == Role::analyzer)
    module.name = tr("nodes.newAnalyzerName", "index", index);
  else if (role == Role::utility)
    module.name = tr("nodes.newUtilityName", "index", index);
  else
    module.name = tr("nodes.newAudioEffectName", "index", index);
  module.category =
      role == Role::instrument   ? tr("graph.library.category.instruments")
      : role == Role::midiEffect ? tr("graph.library.category.midi")
      : role == Role::modulator  ? tr("graph.library.category.modulators")
      : role == Role::analyzer   ? tr("graph.library.category.analyzers")
                                 : tr("graph.library.category.utilities");
  module.author = graphAuthor_;

  const auto append = [this, &module](const NodeId id,
                                      const cydaw::base::Text &key,
                                      const base::Point<float> position) {
    if (const auto *type = findType(key)) {
      module.nodes.push_back({id,
                              key,
                              type->title,
                              position,
                              defaultParametersForType(key),
                              type->inputChannels,
                              type->outputChannels,
                              {}});
      module.nodes.back().ports = type->ports;
    }
  };
  const auto safeAudioChannels = base::limit(1, 8, audioChannels);
  const auto audioInputKey = graphAudioBoundaryKey(true, safeAudioChannels);
  const auto audioOutputKey = graphAudioBoundaryKey(false, safeAudioChannels);
  if (role == Role::instrument) {
    // A new instrument is an explicit contract, not a hidden synth.
    // Factory instruments such as Poly Sine remain available separately.
    append(1, "graph-midi-input", {0.12f, 0.42f});
    append(2, audioOutputKey, {0.76f, 0.42f});
  } else if (role == Role::midiEffect) {
    append(1, "graph-midi-input", {0.16f, 0.42f});
    append(2, "graph-midi-output", {0.72f, 0.42f});
    module.connections = {{1, 2, PortKind::midi}};
  } else if (role == Role::modulator) {
    // The author chooses the modulation source.  Do not silently add an
    // LFO: the only required contract is a published value output.
    append(1, "graph-value-output", {0.72f, 0.42f});
  } else if (role == Role::utility) {
    append(1, "graph-value-input", {0.16f, 0.42f});
    append(2, "graph-value-output", {0.72f, 0.42f});
    module.connections = {{1, 2, PortKind::control}};
  } else {
    // Audio effects and analyzers start as transparent, channel-accurate
    // shells.  The author inserts DSP or visualizer nodes deliberately.
    append(1, audioInputKey, {0.16f, 0.42f});
    append(2, audioOutputKey, {0.72f, 0.42f});
    module.connections = {{1, 2, PortKind::audio}};
  }
  if (module.nodes.empty())
    return cydaw::base::Result::fail("Unable to create graph template");

  inferModulePorts(module);
  module.interfaceState = createDefaultModuleInterface(module);
  const auto key = module.key;
  savedModules_.push_back(std::move(module));
  registerModuleType(savedModules_.back());
  ;
  const auto *type = findType(key);
  if (type == nullptr)
    return cydaw::base::Result::fail("Unable to register graph module");

  addNodeFromType(*type);
  const auto moduleNodeId = nodes_.back().id;
  const auto connected = insertModuleIntoCurrentSignalFlow(moduleNodeId);
  setStatus(tr(connected ? "nodes.status.inlineGraphInserted"
                         : "nodes.status.inlineGraphCreated"));
  ;
  notifyLayoutChanged();
  if (openEditor)
    ;
  return cydaw::base::Result::ok();
}
cydaw::base::Result GraphDocument::createGraphVariant(
    const cydaw::base::Text &sourceModuleKey, const cydaw::base::Text &name,
    const bool replaceSelectedInstance, const bool openEditor) {
  if (editingLocked_)
    return cydaw::base::Result::fail(tr("nodes.status.graphLocked"));
  const auto *source = findModule(sourceModuleKey);
  if (source == nullptr)
    return cydaw::base::Result::fail(tr("nodes.error.variantSourceMissing"));

  SavedModule variant = *source;
  variant.key = "graph-variant-" +
                base::Uuid().toString().removeCharacters("{}-").toLowerCase();
  variant.name = name.trim().substring(0, 128);
  if (variant.name.isEmpty())
    variant.name = tr("nodes.variant.defaultName", "name", source->name);
  variant.version = {1, 0, 0};
  variant.parentClassKey = source->key;
  variant.factoryModule = false;
  variant.inlineModule = true;
  variant.locked = false;

  const auto variantKey = variant.key;
  const auto variantName = variant.name;
  savedModules_.push_back(std::move(variant));
  registerModuleType(savedModules_.back());
  ;

  auto replaced = false;
  if (replaceSelectedInstance) {
    for (const auto selectedId : selectedNodes_) {
      auto *instance = findNode(selectedId);
      if (instance == nullptr || instance->typeKey != sourceModuleKey)
        continue;
      const auto *variantType = findType(variantKey);
      if (variantType == nullptr)
        break;
      instance->typeKey = variantKey;
      instance->title = variantName;
      instance->colour = variantType->colour;
      instance->hasInput = variantType->hasInput;
      instance->inputKind = variantType->inputKind;
      instance->hasOutput = variantType->hasOutput;
      instance->outputKind = variantType->outputKind;
      instance->inputChannels = variantType->inputChannels;
      instance->outputChannels = variantType->outputChannels;
      // The cloned class keeps the stable public port IDs. The node's
      // instance-owned bindings, interface and port defaults therefore
      // remain valid and its live cable routing is preserved.
      replaced = true;
      break;
    }
  }

  synchroniseGraphInterface();
  ;
  ;
  ;
  notifyLayoutChanged();
  ;
  setStatus(tr(replaced ? "nodes.status.variantCreatedAndReplaced"
                        : "nodes.status.variantCreated",
               "name", variantName));
  if (openEditor)
    ;
  return cydaw::base::Result::ok();
}
bool GraphDocument::insertModuleIntoCurrentSignalFlow(
    const NodeId moduleNodeId) {
  const auto *moduleNode = findNode(moduleNodeId);
  if (moduleNode == nullptr)
    return false;

  const auto connect = [this](const NodeId source, const NodeId destination) {
    return source != 0 && destination != 0 && connectNodes(source, destination);
  };
  const auto boundaryInput = [this](const PortKind kind) -> NodeId {
    const auto found =
        std::find_if(nodes_.begin(), nodes_.end(), [kind](const Node &node) {
          bool audioInput = false;
          return node.hasOutput && node.outputKind == kind &&
                 (node.typeKey == "audio-clips" ||
                  node.typeKey == "midi-input" ||
                  (isGraphAudioBoundaryKey(node.typeKey, &audioInput) &&
                   audioInput) ||
                  (node.typeKey.startsWith("graph-") &&
                   node.typeKey.endsWith("-input")));
        });
    return found != nodes_.end() ? found->id : 0;
  };
  const auto boundaryOutput = [this](const PortKind kind) -> NodeId {
    const auto found =
        std::find_if(nodes_.begin(), nodes_.end(), [kind](const Node &node) {
          bool audioInput = false;
          return node.hasInput && node.inputKind == kind &&
                 (node.typeKey == "master" ||
                  (isGraphAudioBoundaryKey(node.typeKey, &audioInput) &&
                   !audioInput) ||
                  (node.typeKey.startsWith("graph-") &&
                   node.typeKey.endsWith("-output")));
        });
    return found != nodes_.end() ? found->id : 0;
  };

  if (moduleNode->hasInput && moduleNode->hasOutput &&
      moduleNode->inputKind == moduleNode->outputKind) {
    const auto kind = moduleNode->inputKind;
    if (kind == PortKind::audio) {
      const auto target = boundaryOutput(kind);
      const auto incoming = std::find_if(
          connections_.begin(), connections_.end(),
          [target, kind](const Connection &connection) {
            return connection.destination == target && connection.kind == kind;
          });
      if (incoming != connections_.end()) {
        const auto source = incoming->source;
        connections_.erase(incoming);
        return connect(source, moduleNodeId) && connect(moduleNodeId, target);
      }
      const auto source = boundaryInput(kind);
      return connect(source, moduleNodeId) &&
             (target == 0 || connect(moduleNodeId, target));
    }

    const auto source = boundaryInput(kind);
    const auto outgoing = std::find_if(
        connections_.begin(), connections_.end(),
        [source, kind](const Connection &connection) {
          return connection.source == source && connection.kind == kind;
        });
    if (outgoing != connections_.end()) {
      const auto target = outgoing->destination;
      connections_.erase(outgoing);
      return connect(source, moduleNodeId) && connect(moduleNodeId, target);
    }
    return connect(source, moduleNodeId);
  }

  if (moduleNode->hasInput && moduleNode->hasOutput) {
    NodeId source{};
    for (const auto &candidate : nodes_) {
      if (candidate.id == moduleNodeId || !candidate.hasOutput ||
          candidate.outputKind != moduleNode->inputKind)
        continue;
      const auto hasOutgoing = std::ranges::any_of(
          connections_, [&candidate](const Connection &connection) {
            return connection.source == candidate.id;
          });
      if (!hasOutgoing)
        source = candidate.id;
    }
    if (source == 0)
      source = boundaryInput(moduleNode->inputKind);
    const auto target = boundaryOutput(moduleNode->outputKind);
    const auto targetOccupied = std::ranges::any_of(
        connections_, [target](const Connection &connection) {
          return connection.destination == target;
        });
    const auto inputConnected = connect(source, moduleNodeId);
    const auto outputConnected =
        target != 0 && !targetOccupied && connect(moduleNodeId, target);
    return inputConnected || outputConnected;
  }
  return false;
}
ModuleInterfaceState
GraphDocument::createDefaultModuleInterface(const SavedModule &module) const {
  ModuleInterfaceState state;
  auto includeSpectrum = false;
  std::set<cydaw::base::Text> publishedKeys;
  for (const auto &node : module.nodes) {
    ModuleInterfaceState candidate;
    if (const auto *nested = findModule(node.typeKey))
      candidate = node.interfaceState.parameters.empty()
                      ? createGraphInstanceInterface(node.id, *nested)
                      : node.interfaceState;
    else
      appendPublishedParameters(candidate, node.typeKey, node.id,
                                node.parameters);
    for (auto &parameter : candidate.parameters)
      if (publishedKeys.insert(parameter.key).second)
        state.parameters.push_back(std::move(parameter));
    includeSpectrum =
        includeSpectrum || node.typeKey == "spectrum" ||
        std::ranges::any_of(candidate.controls, [](const auto &control) {
          return control.kind == ModuleControlKind::spectrum;
        });
  }
  layoutPublishedInterface(state, module.name, includeSpectrum);
  appendInterfacePorts(
      state, module.hasInput, static_cast<std::uint8_t>(module.inputKind),
      module.hasOutput, static_cast<std::uint8_t>(module.outputKind), {}, {},
      0.40f, 0.40f, {}, {}, std::max<std::uint8_t>(1, module.inputChannels),
      std::max<std::uint8_t>(1, module.outputChannels));
  insetGeneratedControlsForPorts(state);
  return state;
}
void GraphDocument::inferModulePorts(SavedModule &module) const {
  const auto explicitInput = std::find_if(
      module.nodes.begin(), module.nodes.end(), [](const auto &node) {
        bool audioInput = false;
        return (isGraphAudioBoundaryKey(node.typeKey, &audioInput) &&
                audioInput) ||
               (node.typeKey.startsWith("graph-") &&
                node.typeKey.endsWith("-input"));
      });
  const auto explicitOutput = std::find_if(
      module.nodes.begin(), module.nodes.end(), [](const auto &node) {
        bool audioInput = false;
        return (isGraphAudioBoundaryKey(node.typeKey, &audioInput) &&
                !audioInput) ||
               (node.typeKey.startsWith("graph-") &&
                node.typeKey.endsWith("-output"));
      });
  if (explicitInput != module.nodes.end()) {
    if (const auto *type = findType(explicitInput->typeKey)) {
      module.hasInput = type->hasOutput;
      module.inputKind = type->outputKind;
      module.inputChannels = explicitInput->outputChannels != 0
                                 ? explicitInput->outputChannels
                                 : type->outputChannels;
    }
  } else {
    const auto entry =
        std::find_if(module.nodes.begin(), module.nodes.end(),
                     [this, &module](const auto &node) {
                       const auto *type = findType(node.typeKey);
                       if (type == nullptr || !type->hasInput)
                         return false;
                       return std::none_of(
                           module.connections.begin(), module.connections.end(),
                           [&node](const auto &connection) {
                             return connection.destination == node.id;
                           });
                     });
    module.hasInput = entry != module.nodes.end();
    if (module.hasInput)
      module.inputKind = findType(entry->typeKey)->inputKind;
    if (module.hasInput)
      module.inputChannels = entry->inputChannels != 0
                                 ? entry->inputChannels
                                 : findType(entry->typeKey)->inputChannels;
  }
  if (explicitOutput != module.nodes.end()) {
    if (const auto *type = findType(explicitOutput->typeKey)) {
      module.hasOutput = type->hasInput;
      module.outputKind = type->inputKind;
      module.outputChannels = explicitOutput->inputChannels != 0
                                  ? explicitOutput->inputChannels
                                  : type->inputChannels;
    }
  } else {
    const auto exit =
        std::find_if(module.nodes.begin(), module.nodes.end(),
                     [this, &module](const auto &node) {
                       const auto *type = findType(node.typeKey);
                       if (type == nullptr || !type->hasOutput)
                         return false;
                       return std::none_of(
                           module.connections.begin(), module.connections.end(),
                           [&node](const auto &connection) {
                             return connection.source == node.id;
                           });
                     });
    module.hasOutput = exit != module.nodes.end();
    if (module.hasOutput)
      module.outputKind = findType(exit->typeKey)->outputKind;
    if (module.hasOutput)
      module.outputChannels = exit->outputChannels != 0
                                  ? exit->outputChannels
                                  : findType(exit->typeKey)->outputChannels;
  }
}
cydaw::graph::GraphRole
GraphDocument::inferModuleRole(const SavedModule &module) const {
  using Role = cydaw::graph::GraphRole;
  if (module.role != Role::generic)
    return module.role;
  if (module.hasOutput && module.outputKind == PortKind::control)
    return Role::modulator;
  if (module.hasInput && module.inputKind == PortKind::midi &&
      module.hasOutput && module.outputKind == PortKind::audio)
    return Role::instrument;
  if (module.hasInput && module.inputKind == PortKind::midi &&
      module.hasOutput && module.outputKind == PortKind::midi)
    return Role::midiEffect;
  if (module.hasInput && module.inputKind == PortKind::audio &&
      module.hasOutput && module.outputKind == PortKind::audio)
    return Role::audioEffect;
  return Role::generic;
}
void GraphDocument::registerModuleType(const SavedModule &module) {
  auto found = std::find_if(
      catalog_.begin(), catalog_.end(),
      [&module](const auto &type) { return type.key == module.key; });
  if (found == catalog_.end()) {
    catalog_.push_back({module.key,
                        {},
                        module.name,
                        module.inlineModule ? "nodes.category.inlineGraphs"
                                            : "nodes.category.modules",
                        base::Colour(0xffa98bea),
                        module.hasInput,
                        module.inputKind,
                        module.hasOutput,
                        module.outputKind,
                        false,
                        true,
                        module.inputChannels,
                        module.outputChannels});
    found = std::prev(catalog_.end());
  }
  found->title = module.name;
  found->categoryKey = module.inlineModule ? "nodes.category.inlineGraphs"
                                           : "nodes.category.modules";
  found->hasInput = module.hasInput;
  found->inputKind = module.inputKind;
  found->hasOutput = module.hasOutput;
  found->outputKind = module.outputKind;
  found->composite = true;
  found->inputChannels = module.inputChannels;
  found->outputChannels = module.outputChannels;
  found->ports.clear();
  found->stateful = true;
  found->supportsPolyphony = module.outputKind == PortKind::audio &&
                             module.inputKind == PortKind::midi;
  found->breaksFeedbackCycle = false;
  found->parameterSchema.clear();
  for (const auto &parameter : module.interfaceState.parameters)
    found->parameterSchema.push_back({parameter.key.toStdString(),
                                      parameter.minimum, parameter.maximum,
                                      moduleParameterDefaultValue(parameter)});
  const auto portType = [](const PortKind kind) {
    if (kind == PortKind::audio)
      return cydaw::graph::PortType::audio;
    if (kind == PortKind::midi)
      return cydaw::graph::PortType::midi;
    if (kind == PortKind::transport)
      return cydaw::graph::PortType::transport;
    return cydaw::graph::PortType::value;
  };
  const auto boundaryDirection =
      [](const cydaw::base::Text &key) -> std::optional<bool> {
    bool audioInput = false;
    if (isGraphAudioBoundaryKey(key, &audioInput))
      return audioInput;
    if (!key.startsWith("graph-"))
      return {};
    if (key.endsWith("-input"))
      return true;
    if (key.endsWith("-output"))
      return false;
    return {};
  };
  for (const auto &boundary : module.nodes) {
    const auto direction = boundaryDirection(boundary.typeKey);
    const auto *boundaryType = findType(boundary.typeKey);
    if (!direction.has_value() || boundaryType == nullptr)
      continue;
    const auto input = *direction;
    const auto kind =
        input ? boundaryType->outputKind : boundaryType->inputKind;
    const auto channels =
        kind == PortKind::audio
            ? std::max<std::uint8_t>(
                  1, input ? (boundary.outputChannels != 0
                                  ? boundary.outputChannels
                                  : boundaryType->outputChannels)
                           : (boundary.inputChannels != 0
                                  ? boundary.inputChannels
                                  : boundaryType->inputChannels))
            : std::uint8_t{1};
    found->ports.push_back(
        {boundary.id,
         (input ? "input-" : "output-") +
             cydaw::base::Text(static_cast<std::int64_t>(boundary.id)),
         boundary.alias.isNotEmpty() ? boundary.alias : boundary.title,
         portType(kind), input, channels, false, 0.0});
    if (!boundary.ports.empty()) {
      found->ports.back().category = boundary.ports.front().category;
      found->ports.back().defaultValue = boundary.ports.front().defaultValue;
    }
  }
  // Legacy modules created before explicit boundary nodes retain their
  // single typed connector until they are opened and upgraded.
  if (found->ports.empty() && module.hasInput)
    found->ports.push_back(
        {1, "input", "Input", portType(module.inputKind), true,
         module.inputKind == PortKind::audio
             ? std::max<std::uint8_t>(1, module.inputChannels)
             : std::uint8_t{1},
         false, 0.0});
  if (std::ranges::none_of(found->ports,
                           [](const NodePort &port) { return !port.input; }) &&
      module.hasOutput)
    found->ports.push_back(
        {2, "output", "Output", portType(module.outputKind), false,
         module.outputKind == PortKind::audio
             ? std::max<std::uint8_t>(1, module.outputChannels)
             : std::uint8_t{1},
         false, 0.0});
  // A socket per knob the device shows: an LFO plugged there sets the
  // knob, or moves it around its value.
  for (const auto &parameter : module.interfaceState.parameters)
    if (parameter.showInDevice)
      found->ports.push_back(
          {knobPortId(parameter.key), "knob." + parameter.key,
           parameter.label.isNotEmpty() ? parameter.label : parameter.key,
           cydaw::graph::PortType::value, true, 1, false,
           moduleParameterDefaultValue(parameter)});
}
const ModuleInterfaceState &
GraphDocument::presentationForNode(const Node &node) const {
  if (!node.interfaceState.parameters.empty() ||
      !node.interfaceState.controls.empty() ||
      !node.interfaceState.deviceControls.empty())
    return node.interfaceState;
  if (const auto *module = findModule(node.typeKey))
    return module->interfaceState; // migration for pre-schema-17 projects
  return node.interfaceState;
}
ModuleInterfaceState *GraphDocument::editablePresentationForNode(Node &node) {
  return &node.interfaceState;
}
ModuleInterfaceState GraphDocument::createDefaultGraphInterface() const {
  ModuleInterfaceState state;
  std::set<cydaw::base::Text> keys;
  // Which node each published parameter came from, so a label repeated by
  // two nodes can say which one it belongs to.
  std::vector<cydaw::base::Text> owners;
  auto includeSpectrum = false;
  for (const auto &node : nodes_) {
    auto presentation = presentationForNode(node);
    for (auto &parameter : presentation.parameters) {
      if (node.typeKey == "value-constant" && node.alias.isNotEmpty())
        parameter.label = node.alias;
      if (findModule(node.typeKey) == nullptr)
        parameter.value = nativeParameterValue(node.parameters, parameter.key);
      if (keys.insert(parameter.key).second) {
        owners.push_back(displayTitle(node));
        state.parameters.push_back(std::move(parameter));
      }
    }
    includeSpectrum =
        includeSpectrum ||
        std::any_of(moduleInterfaceControls(presentation,
                                            ModuleInterfaceLayoutMode::graph)
                        .begin(),
                    moduleInterfaceControls(presentation,
                                            ModuleInterfaceLayoutMode::graph)
                        .end(),
                    [](const auto &control) {
                      return control.kind == ModuleControlKind::spectrum;
                    });
  }
  // Three low-pass filters all publish a parameter called Cutoff, and a
  // label that names four different knobs names none of them. When the
  // owning nodes are all different the node's title says which is which;
  // when they are not — three identical drive stages — only its position
  // in the chain can.
  {
    std::map<cydaw::base::Text, std::vector<std::size_t>> byLabel;
    for (std::size_t index = 0; index < state.parameters.size(); ++index)
      byLabel[state.parameters[index].label].push_back(index);
    for (const auto &[label, indices] : byLabel) {
      if (indices.size() < 2)
        continue;
      std::set<cydaw::base::Text> distinctOwners;
      for (const auto index : indices)
        if (index < owners.size())
          distinctOwners.insert(owners[index]);
      const auto ownersIdentify = distinctOwners.size() == indices.size() &&
                                  !distinctOwners.contains({});
      auto ordinal = 0;
      for (const auto index : indices) {
        ++ordinal;
        auto &parameter = state.parameters[index];
        if (!ownersIdentify) {
          parameter.label = label + " " + cydaw::base::Text(ordinal);
          continue;
        }
        const auto &owner = owners[index];
        parameter.label =
            owner.equalsIgnoreCase(label) ? owner : owner + " " + label;
      }
    }
  }
  layoutPublishedInterface(state, tr("module.ui.graphDevice"), includeSpectrum);
  std::vector<const Node *> publishedInputs;
  std::vector<const Node *> publishedOutputs;
  for (const auto &node : nodes_) {
    if (!node.typeKey.startsWith("graph-"))
      continue;
    bool audioInput = false;
    const auto explicitAudio =
        isGraphAudioBoundaryKey(node.typeKey, &audioInput);
    if ((explicitAudio && audioInput) || node.typeKey.endsWith("-input"))
      publishedInputs.push_back(&node);
    else if ((explicitAudio && !audioInput) || node.typeKey.endsWith("-output"))
      publishedOutputs.push_back(&node);
  }
  if (publishedInputs.empty() && publishedOutputs.empty()) {
    const auto midiInput =
        std::any_of(nodes_.begin(), nodes_.end(), [](const auto &node) {
          return node.typeKey == "midi-input";
        });
    appendInterfacePorts(
        state, true,
        static_cast<std::uint8_t>(midiInput ? PortKind::midi : PortKind::audio),
        true, static_cast<std::uint8_t>(PortKind::audio), "graph.compat.input",
        "graph.compat.output", 0.40f, 0.40f, {}, {},
        midiInput ? std::uint8_t{1} : std::uint8_t{2}, std::uint8_t{2});
    insetGeneratedControlsForPorts(state);
  } else {
    for (std::size_t index = 0; index < publishedInputs.size(); ++index) {
      const auto *node = publishedInputs[index];
      const auto y = 0.08f + 0.78f * static_cast<float>(index + 1) /
                                 static_cast<float>(publishedInputs.size() + 1);
      appendInterfacePorts(
          state, true, static_cast<std::uint8_t>(node->outputKind), false, 0,
          "node." + cydaw::base::Text(static_cast<std::int64_t>(node->id)) +
              ".published-input",
          {}, y, 0.40f, displayTitle(*node), {},
          node->outputKind == PortKind::audio ? audioOutputChannels(*node)
                                              : std::uint8_t{1},
          std::uint8_t{1});
    }
    for (std::size_t index = 0; index < publishedOutputs.size(); ++index) {
      const auto *node = publishedOutputs[index];
      const auto y =
          0.08f + 0.78f * static_cast<float>(index + 1) /
                      static_cast<float>(publishedOutputs.size() + 1);
      appendInterfacePorts(
          state, false, 0, true, static_cast<std::uint8_t>(node->inputKind), {},
          "node." + cydaw::base::Text(static_cast<std::int64_t>(node->id)) +
              ".published-output",
          0.40f, y, {}, displayTitle(*node), std::uint8_t{1},
          node->inputKind == PortKind::audio ? audioInputChannels(*node)
                                             : std::uint8_t{1});
    }
    // A port draws its name inward from the edge of the card. Without the
    // same inset the module generator already applies, that caption was
    // painted straight across the first control in the row.
    insetGeneratedControlsForPorts(state);
  }
  return state;
}
void GraphDocument::synchroniseGraphInterface() {
  auto current = createDefaultGraphInterface();
  if (graphInterfaceState_.controls.empty() &&
      graphInterfaceState_.deviceControls.empty() &&
      !graphInterfaceState_.designed && !graphInterfaceState_.deviceDesigned &&
      !graphInterfaceState_.windowDesign.isObject() &&
      !graphInterfaceState_.deviceDesign.isObject()) {
    graphInterfaceState_ = std::move(current);
    return;
  }

  std::set<cydaw::base::Text> validKeys;
  for (const auto &parameter : current.parameters)
    validKeys.insert(parameter.key);
  std::erase_if(graphInterfaceState_.parameters,
                [&validKeys](const auto &parameter) {
                  return !validKeys.contains(parameter.key);
                });
  for (const auto &currentParameter : current.parameters) {
    const auto found =
        std::find_if(graphInterfaceState_.parameters.begin(),
                     graphInterfaceState_.parameters.end(),
                     [&currentParameter](const auto &parameter) {
                       return parameter.key == currentParameter.key;
                     });
    if (found == graphInterfaceState_.parameters.end())
      graphInterfaceState_.parameters.push_back(currentParameter);
    else {
      const auto showInDevice = found->showInDevice;
      const auto hostParameterId = found->hostParameterId;
      const auto publishToHost = found->publishToHost;
      const auto automatable = found->automatable;
      const auto modulatable = found->modulatable;
      const auto description = found->description;
      *found = currentParameter;
      found->showInDevice = showInDevice;
      found->hostParameterId = hostParameterId != 0
                                   ? hostParameterId
                                   : stableModuleParameterId(found->key);
      found->publishToHost = publishToHost;
      found->automatable = automatable;
      found->modulatable = modulatable;
      found->description =
          description.isNotEmpty() ? description : currentParameter.description;
    }
  }
  for (const auto mode : {ModuleInterfaceLayoutMode::graph}) {
    auto &controls = moduleInterfaceControls(graphInterfaceState_, mode);
    std::erase_if(controls, [&validKeys](const auto &control) {
      const auto parameterControl =
          control.kind == ModuleControlKind::knob ||
          control.kind == ModuleControlKind::fader ||
          control.kind == ModuleControlKind::toggle ||
          control.kind == ModuleControlKind::selector ||
          control.kind == ModuleControlKind::xyPad ||
          control.kind == ModuleControlKind::envelope ||
          control.kind == ModuleControlKind::valueDisplay ||
          control.kind == ModuleControlKind::filterResponse ||
          control.kind == ModuleControlKind::dynamicsResponse;
      if (!parameterControl)
        return false;
      if (!validKeys.contains(control.parameterBinding))
        return true;
      if (control.kind == ModuleControlKind::xyPad ||
          control.kind == ModuleControlKind::filterResponse ||
          control.kind == ModuleControlKind::dynamicsResponse)
        return !validKeys.contains(control.secondaryParameterBinding) ||
               control.secondaryParameterBinding == control.parameterBinding;
      if (control.kind != ModuleControlKind::envelope)
        return false;
      const std::set<cydaw::base::Text> bindings{
          control.parameterBinding, control.secondaryParameterBinding,
          control.tertiaryParameterBinding, control.quaternaryParameterBinding};
      return bindings.size() != 4 ||
             !validKeys.contains(control.secondaryParameterBinding) ||
             !validKeys.contains(control.tertiaryParameterBinding) ||
             !validKeys.contains(control.quaternaryParameterBinding);
    });

    const auto &currentControls = moduleInterfaceControls(current, mode);
    const auto isPort = [](const ModuleInterfaceControl &control) {
      return control.kind == ModuleControlKind::inputPort ||
             control.kind == ModuleControlKind::outputPort;
    };
    std::vector<ModuleInterfaceControl> reconciledPorts;
    for (const auto &currentControl : currentControls) {
      if (!isPort(currentControl))
        continue;
      auto found = std::find_if(
          controls.begin(), controls.end(),
          [&currentControl, &isPort](const auto &control) {
            return isPort(control) && control.kind == currentControl.kind &&
                   control.parameterBinding == currentControl.parameterBinding;
          });
      // A schema <= 12 module exposed one unbound compatibility port.
      // Reuse its authored geometry only when the new typed contract has
      // a single port in that direction; otherwise use deterministic
      // generated positions for every published bus.
      if (found == controls.end()) {
        const auto sameDirectionCount = std::ranges::count_if(
            currentControls, [&currentControl, &isPort](const auto &value) {
              return isPort(value) && value.kind == currentControl.kind;
            });
        if (sameDirectionCount == 1)
          found = std::find_if(controls.begin(), controls.end(),
                               [&currentControl, &isPort](const auto &control) {
                                 return isPort(control) &&
                                        control.kind == currentControl.kind &&
                                        control.parameterBinding.isEmpty();
                               });
      }
      auto reconciled = currentControl;
      if (found != controls.end()) {
        reconciled.id = found->id;
        reconciled.x = found->x;
        reconciled.y = found->y;
        reconciled.width = found->width;
        reconciled.height = found->height;
      } else {
        reconciled.id = graphInterfaceState_.nextControlId++;
      }
      reconciledPorts.push_back(std::move(reconciled));
    }
    std::erase_if(controls, isPort);
    controls.insert(controls.end(), reconciledPorts.begin(),
                    reconciledPorts.end());
  }
  rebuildAutomaticDeviceControls(graphInterfaceState_);
}
void GraphDocument::rebuildPendingWavetableBanks() {
  if (wavetableBankPending_.empty())
    return;
  const auto pending = std::move(wavetableBankPending_);
  wavetableBankPending_.clear();
  for (const auto id : pending)
    if (auto *node = findNode(id);
        node != nullptr && node->wavetableShape != nullptr)
      node->wavetableBank =
          cydaw::engine::buildWavetableBank(*node->wavetableShape);
  queueParameterNotification();
}
ModuleInterfaceState GraphDocument::graphInterfaceState() const {
  return graphInterfaceState_;
}
bool GraphDocument::containsNodeType(const cydaw::base::Text &typeKey) const {
  return std::ranges::any_of(
      nodes_, [&typeKey](const Node &node) { return node.typeKey == typeKey; });
}
std::vector<cydaw::graph::GraphDefinition>
GraphDocument::graphDefinitions() const {
  namespace graph = cydaw::graph;
  const auto portType = [](const PortKind kind) {
    switch (kind) {
    case PortKind::audio:
      return graph::PortType::audio;
    case PortKind::midi:
      return graph::PortType::midi;
    case PortKind::control:
      return graph::PortType::value;
    case PortKind::transport:
      return graph::PortType::transport;
    }
    return graph::PortType::value;
  };
  const auto portRate = [](const PortKind kind) {
    if (kind == PortKind::audio)
      return graph::PortRate::audio;
    if (kind == PortKind::control)
      return graph::PortRate::control;
    return graph::PortRate::event;
  };
  const auto makePort = [&portType, &portRate](
                            const graph::PortId id,
                            const cydaw::base::Text &key, const PortKind kind,
                            const graph::PortDirection direction,
                            const int channels = 0,
                            const cydaw::base::Text &displayName = {}) {
    graph::GraphPort port;
    port.id = id;
    port.key = key.toStdString();
    port.displayName =
        (displayName.isNotEmpty() ? displayName : key).toStdString();
    port.type = portType(kind);
    port.audioType =
        kind == PortKind::audio
            ? graph::audioPortTypeForChannels(static_cast<std::uint16_t>(
                  base::limit(1, 8, channels != 0 ? channels : 2)))
            : graph::AudioPortType::unspecified;
    port.direction = direction;
    port.rate = portRate(kind);
    port.channels = kind == PortKind::audio
                        ? static_cast<std::uint16_t>(
                              base::limit(1, 8, channels != 0 ? channels : 2))
                        : 1;
    return port;
  };
  const auto makeExactPort = [](const NodePort &source) {
    graph::GraphPort port;
    port.id = source.id;
    port.key = source.key.toStdString();
    port.displayName = source.name.toStdString();
    port.type = source.type;
    port.audioType = source.type == graph::PortType::audio
                         ? graph::audioPortTypeForChannels(source.channels)
                         : graph::AudioPortType::unspecified;
    port.direction = source.input ? graph::PortDirection::input
                                  : graph::PortDirection::output;
    port.rate = source.type == graph::PortType::audio ? graph::PortRate::audio
                : (source.type == graph::PortType::value ||
                   source.type == graph::PortType::analysis)
                    ? graph::PortRate::control
                    : graph::PortRate::event;
    port.channels = source.type == graph::PortType::audio ? source.channels : 1;
    port.acceptsMultipleConnections = source.acceptsMultipleConnections;
    port.defaultValue = source.defaultValue;
    return port;
  };
  const auto isBoundaryInput = [](const cydaw::base::Text &key) {
    bool audioInput = false;
    return (GraphDocument::isGraphAudioBoundaryKey(key, &audioInput) &&
            audioInput) ||
           (key.startsWith("graph-") && key.endsWith("-input")) ||
           key == "audio-clips" || key == "midi-input";
  };
  const auto isBoundaryOutput = [](const cydaw::base::Text &key) {
    bool audioInput = false;
    return (GraphDocument::isGraphAudioBoundaryKey(key, &audioInput) &&
            !audioInput) ||
           (key.startsWith("graph-") && key.endsWith("-output")) ||
           key == "master" || key == "midi-output";
  };
  const auto controlKind = [](const ModuleControlKind kind) {
    using Source = ModuleControlKind;
    using Target = graph::InterfaceControlKind;
    switch (kind) {
    case Source::knob:
      return Target::knob;
    case Source::fader:
      return Target::fader;
    case Source::waveform:
      return Target::waveform;
    case Source::spectrum:
      return Target::spectrum;
    case Source::levelMeter:
      return Target::levelMeter;
    case Source::text:
      return Target::text;
    case Source::inputPort:
      return Target::inputPort;
    case Source::outputPort:
      return Target::outputPort;
    case Source::toggle:
      return Target::toggle;
    case Source::envelope:
      return Target::envelope;
    case Source::group:
      return Target::group;
    case Source::selector:
      return Target::selector;
    case Source::xyPad:
      return Target::xyPad;
    case Source::valueDisplay:
      return Target::valueDisplay;
    case Source::filterResponse:
      return Target::filterResponse;
    case Source::dynamicsResponse:
      return Target::dynamicsResponse;
    }
    return Target::text;
  };

  std::vector<graph::GraphDefinition> definitions;
  definitions.reserve(savedModules_.size() + 1);
  const auto moduleReference = [this](const SavedModule &module) {
    return graph::GraphReference{graphReference_.id + ".module." +
                                     module.key.toStdString(),
                                 module.version};
  };
  const auto appendGraphInstanceOverrides =
      [](graph::GraphNode &destination, const ModuleInterfaceState &state,
         const NodeId instanceId, const graph::GraphDefinition &graphClass) {
        for (const auto &published : graphClass.parameters) {
          const auto namespaced = nodeParameterBinding(
              instanceId, cydaw::base::Text(published.key));
          const auto classKey = cydaw::base::Text(published.key);
          const auto value = std::ranges::find_if(
              state.parameters,
              [&namespaced, &classKey](const auto &parameter) {
                return parameter.key == namespaced || parameter.key == classKey;
              });
          if (value != state.parameters.end())
            destination.parameterOverrides.insert_or_assign(
                published.key, base::limit(published.minimum, published.maximum,
                                           value->value));
        }
      };
  const auto appendPrimitiveContract =
      [this](graph::GraphNode &destination, const cydaw::base::Text &typeKey,
             const cydaw::engine::NativeDspNodeParameters &values) {
        const auto *type = findType(typeKey);
        if (type == nullptr)
          return;
        destination.breaksFeedbackCycle = type->breaksFeedbackCycle;
        destination.parameters = type->parameterSchema;
        for (auto &parameter : destination.parameters) {
          const auto current = primitiveParameterValueForType(
              typeKey, cydaw::base::Text(parameter.key), values);
          if (!current.has_value())
            continue;
          const auto value =
              base::limit(parameter.minimum, parameter.maximum, *current);
          destination.parameterOverrides.insert_or_assign(parameter.key, value);
        }
      };

  const auto appendPresentation = [&controlKind, &definitions](
                                      graph::GraphDefinition &definition,
                                      const ModuleInterfaceState &state) {
    std::unordered_map<std::string, graph::ParameterId> parameterIds;
    for (const auto &parameter : state.parameters) {
      const auto key = parameter.key.toStdString();
      const graph::GraphNode *target{};
      std::string targetParameterKey;
      for (const auto &node : definition.nodes) {
        if (node.graph.has_value()) {
          const auto nested = std::ranges::find_if(
              definitions, [&node](const graph::GraphDefinition &candidate) {
                return candidate.reference == *node.graph;
              });
          const auto prefix = "node." + std::to_string(node.id) + ".";
          const auto nestedKey =
              key.starts_with(prefix) ? key.substr(prefix.size()) : key;
          if (nested != definitions.end() &&
              std::ranges::any_of(
                  nested->parameters,
                  [&nestedKey](const graph::PublishedParameter &candidate) {
                    return candidate.key == nestedKey;
                  })) {
            target = &node;
            targetParameterKey = nestedKey;
            break;
          }
          continue;
        }
        const auto prefix = "node." + std::to_string(node.id) + ".";
        if (!key.starts_with(prefix))
          continue;
        const auto primitiveKey =
            primitiveParameterKeyForBinding(
                cydaw::base::Text(key.substr(prefix.size())))
                .toStdString();
        if (primitiveKey.empty() ||
            !std::ranges::any_of(
                node.parameters,
                [&primitiveKey](const graph::NodeParameter &candidate) {
                  return candidate.key == primitiveKey;
                }))
          continue;
        target = &node;
        targetParameterKey = primitiveKey;
        break;
      }
      if (target == nullptr)
        continue;
      const auto id = static_cast<graph::ParameterId>(
          parameter.hostParameterId != 0
              ? parameter.hostParameterId
              : stableModuleParameterId(parameter.key));
      parameterIds.emplace(key, id);
      definition.parameters.push_back(
          {id, key, parameter.label.toStdString(), parameter.unit.toStdString(),
           parameter.minimum, parameter.maximum,
           moduleParameterDefaultValue(parameter), target->id,
           targetParameterKey, parameter.automatable, parameter.modulatable,
           parameter.showInDevice, parameter.publishToHost});
    }

    const auto appendInterface =
        [&](const char *id, const char *name,
            const graph::InterfaceLayoutMode mode,
            const std::vector<ModuleInterfaceControl> &controls,
            const float width, const float height) {
          graph::GraphInterface interface;
          interface.id = id;
          interface.displayName = name;
          interface.mode = mode;
          interface.width = width;
          interface.height = height;
          for (const auto &source : controls) {
            graph::InterfaceControl control;
            control.id = source.id;
            control.kind = controlKind(source.kind);
            control.label = source.label.toStdString();
            if (const auto found =
                    parameterIds.find(source.parameterBinding.toStdString());
                found != parameterIds.end())
              control.parameterBinding = found->second;
            if (const auto found = parameterIds.find(
                    source.secondaryParameterBinding.toStdString());
                found != parameterIds.end())
              control.secondaryParameterBinding = found->second;
            if (const auto found = parameterIds.find(
                    source.tertiaryParameterBinding.toStdString());
                found != parameterIds.end())
              control.tertiaryParameterBinding = found->second;
            if (const auto found = parameterIds.find(
                    source.quaternaryParameterBinding.toStdString());
                found != parameterIds.end())
              control.quaternaryParameterBinding = found->second;
            for (const auto &port : definition.ports) {
              const auto idText =
                  cydaw::base::Text(static_cast<std::int64_t>(port.id));
              if (source.parameterBinding.contains("node." + idText + ".") &&
                  (source.kind == ModuleControlKind::inputPort ||
                   source.kind == ModuleControlKind::outputPort)) {
                control.portBinding = port.id;
                break;
              }
            }
            control.x = base::limit(0.0f, std::max(0.0f, width - 1.0f),
                                    base::limit(0.0f, 1.0f, source.x) * width);
            control.y = base::limit(0.0f, std::max(0.0f, height - 1.0f),
                                    base::limit(0.0f, 1.0f, source.y) * height);
            control.width = base::limit(1.0f, width - control.x,
                                        std::max(0.01f, source.width) * width);
            control.height =
                base::limit(1.0f, height - control.y,
                            std::max(0.01f, source.height) * height);
            interface.controls.push_back(std::move(control));
          }
          definition.interfaces.push_back(std::move(interface));
        };
    appendInterface("full", "Full editor", graph::InterfaceLayoutMode::freeform,
                    state.controls, 1'000.0f, 640.0f);
    appendInterface("device", "Device", graph::InterfaceLayoutMode::device,
                    state.deviceControls, 640.0f, 240.0f);
  };

  const auto definitionsMark = cydaw::base::Time::getHighResolutionTicks();
  // Each class after the ones it holds: its definition reads theirs (their
  // ports, their published knobs), which must be built first, whatever
  // order the library lists them in (a class made inside another comes
  // after it there).
  std::vector<const SavedModule *> ordered;
  {
    ordered.reserve(savedModules_.size());
    std::set<cydaw::base::Text> placed;
    std::set<cydaw::base::Text> visiting;
    std::function<void(const SavedModule &)> place =
        [&](const SavedModule &module) {
          if (placed.contains(module.key) ||
              !visiting.insert(module.key).second)
            return;
          for (const auto &child : module.nodes)
            if (const auto *inner = findModule(child.typeKey))
              place(*inner);
          visiting.erase(module.key);
          placed.insert(module.key);
          ordered.push_back(&module);
        };
    for (const auto &module : savedModules_)
      place(module);
  }
  for (const auto *orderedModule : ordered) {
    const auto &module = *orderedModule;
    if (module.factoryModule && module.definitionCache.has_value()) {
      ++definitionsCacheHits_;
      definitions.push_back(*module.definitionCache);
      continue;
    }
    graph::GraphDefinition definition;
    definition.reference = moduleReference(module);
    definition.displayName = module.name.toStdString();
    definition.description = module.description.toStdString();
    definition.scope = module.factoryModule  ? graph::GraphScope::factory
                       : module.inlineModule ? graph::GraphScope::inlineTrack
                                             : graphScope_;
    const auto moduleAudioChannels =
        module.inputChannels != 0    ? module.inputChannels
        : module.outputChannels != 0 ? module.outputChannels
                                     : 2;
    const auto hasExplicitInput = std::ranges::any_of(
        module.nodes, [&isBoundaryInput](const ModuleNode &node) {
          return isBoundaryInput(node.typeKey);
        });
    const auto hasExplicitOutput = std::ranges::any_of(
        module.nodes, [&isBoundaryOutput](const ModuleNode &node) {
          return isBoundaryOutput(node.typeKey);
        });
    for (const auto &boundary : module.nodes) {
      const auto input = isBoundaryInput(boundary.typeKey);
      const auto output = isBoundaryOutput(boundary.typeKey);
      if (!input && !output)
        continue;
      const auto *type = findType(boundary.typeKey);
      if (type == nullptr)
        continue;
      const auto kind = input ? type->outputKind : type->inputKind;
      const auto channels =
          kind == PortKind::audio
              ? static_cast<int>(input ? (boundary.outputChannels != 0
                                              ? boundary.outputChannels
                                              : type->outputChannels)
                                       : (boundary.inputChannels != 0
                                              ? boundary.inputChannels
                                              : type->inputChannels))
              : 1;
      const auto key =
          (input ? cydaw::base::Text("input-") : cydaw::base::Text("output-")) +
          cydaw::base::Text(static_cast<std::int64_t>(boundary.id));
      definition.ports.push_back(makePort(
          boundary.id, key, kind,
          input ? graph::PortDirection::input : graph::PortDirection::output,
          channels,
          boundary.alias.isNotEmpty() ? boundary.alias : boundary.title));
      if (!boundary.ports.empty())
        definition.ports.back().defaultValue =
            boundary.ports.front().defaultValue;
    }
    if (module.hasInput && !hasExplicitInput)
      definition.ports.push_back(makePort(1, "input", module.inputKind,
                                          graph::PortDirection::input,
                                          module.inputChannels));
    if (module.hasOutput && !hasExplicitOutput)
      definition.ports.push_back(makePort(2, "output", module.outputKind,
                                          graph::PortDirection::output,
                                          module.outputChannels));
    for (const auto &parameter : module.interfaceState.parameters)
      if (parameter.showInDevice)
        definition.ports.push_back(
            makePort(knobPortId(parameter.key), "knob." + parameter.key,
                     PortKind::control, graph::PortDirection::input, 1,
                     parameter.label));

    for (const auto &child : module.nodes) {
      if (isBoundaryInput(child.typeKey) || isBoundaryOutput(child.typeKey))
        continue;
      const auto *type = findType(child.typeKey);
      if (type == nullptr)
        continue;
      graph::GraphNode node;
      node.id = child.id;
      node.displayName =
          (child.alias.isNotEmpty() ? child.alias : child.title).toStdString();
      node.description = child.description.toStdString();
      node.bypassed = child.bypassed;
      if (const auto *nested = findModule(child.typeKey)) {
        node.kind = graph::GraphNodeKind::graphInstance;
        node.graph = moduleReference(*nested);
        const auto nestedDefinition =
            std::ranges::find_if(definitions, [&node](const auto &candidate) {
              return node.graph.has_value() &&
                     candidate.reference == *node.graph;
            });
        if (nestedDefinition != definitions.end())
          node.ports = nestedDefinition->ports;
        for (auto &port : node.ports)
          if (const auto instancePort =
                  std::ranges::find(child.ports, port.id, &NodePort::id);
              instancePort != child.ports.end())
            port.defaultValue = instancePort->defaultValue;
        if (nestedDefinition != definitions.end())
          appendGraphInstanceOverrides(node, child.interfaceState, child.id,
                                       *nestedDefinition);
      } else {
        const auto processorKey = primitiveProcessorKeyForType(child.typeKey);
        node.processorKey =
            (processorKey.isNotEmpty() ? processorKey : child.typeKey)
                .toStdString();
        if (!type->ports.empty())
          for (const auto &port : type->ports)
            node.ports.push_back(makeExactPort(port));
        else {
          if (type->hasInput)
            node.ports.push_back(makePort(
                1, "input", type->inputKind, graph::PortDirection::input,
                type->inputKind == PortKind::audio ? moduleAudioChannels : 1));
          if (type->hasOutput)
            node.ports.push_back(makePort(
                2, "output", type->outputKind, graph::PortDirection::output,
                type->outputKind == PortKind::audio ? moduleAudioChannels : 1));
        }
        appendPrimitiveContract(node, child.typeKey, child.parameters);
      }
      definition.nodes.push_back(std::move(node));
    }

    const auto moduleNode = [&module](const NodeId id) {
      return std::find_if(
          module.nodes.begin(), module.nodes.end(),
          [id](const ModuleNode &node) { return node.id == id; });
    };
    const auto nodePort = [&definition](const NodeId nodeId,
                                        const graph::PortDirection direction,
                                        const PortKind kind,
                                        const graph::PortId fallback) {
      const auto node = std::ranges::find_if(
          definition.nodes,
          [nodeId](const auto &candidate) { return candidate.id == nodeId; });
      if (node == definition.nodes.end())
        return fallback;
      const auto expectedType = kind == PortKind::audio ? graph::PortType::audio
                                : kind == PortKind::midi ? graph::PortType::midi
                                : kind == PortKind::control
                                    ? graph::PortType::value
                                    : graph::PortType::transport;
      const auto matchesKind = [kind, expectedType](const auto &port) {
        return port.type == expectedType ||
               (kind == PortKind::control &&
                (port.type == graph::PortType::trigger ||
                 port.type == graph::PortType::analysis));
      };
      const auto exact = std::ranges::find_if(
          node->ports,
          [direction, fallback, &matchesKind](const auto &candidate) {
            return candidate.id == fallback &&
                   candidate.direction == direction && matchesKind(candidate);
          });
      if (exact != node->ports.end())
        return exact->id;
      const auto port = std::ranges::find_if(
          node->ports, [direction, &matchesKind](const auto &candidate) {
            return candidate.direction == direction && matchesKind(candidate);
          });
      return port != node->ports.end() ? port->id : fallback;
    };
    for (const auto &connection : module.connections) {
      const auto source = moduleNode(connection.source);
      const auto destination = moduleNode(connection.destination);
      if (source == module.nodes.end() || destination == module.nodes.end())
        continue;
      const graph::GraphEndpoint sourceEndpoint =
          isBoundaryInput(source->typeKey)
              ? graph::GraphEndpoint{graph::boundaryNodeId, source->id}
              : graph::GraphEndpoint{
                    source->id,
                    nodePort(source->id, graph::PortDirection::output,
                             connection.kind, connection.sourcePort)};
      const graph::GraphEndpoint destinationEndpoint =
          isBoundaryOutput(destination->typeKey)
              ? graph::GraphEndpoint{graph::boundaryNodeId, destination->id}
              : graph::GraphEndpoint{
                    destination->id,
                    nodePort(destination->id, graph::PortDirection::input,
                             connection.kind, connection.destinationPort)};
      definition.connections.push_back(
          {sourceEndpoint, destinationEndpoint, connection.mode,
           static_cast<double>(connection.amount)});
    }
    if (module.hasInput && !hasExplicitInput)
      for (const auto &child : module.nodes) {
        const auto *type = findType(child.typeKey);
        if (type == nullptr || !type->hasInput ||
            type->inputKind != module.inputKind)
          continue;
        const auto connected = std::ranges::any_of(
            module.connections, [&child](const Connection &connection) {
              return connection.destination == child.id;
            });
        if (!connected)
          definition.connections.push_back(
              {{graph::boundaryNodeId, 1}, {child.id, 1}});
      }
    if (module.hasOutput && !hasExplicitOutput)
      for (const auto &child : module.nodes) {
        const auto *type = findType(child.typeKey);
        if (type == nullptr || !type->hasOutput ||
            type->outputKind != module.outputKind)
          continue;
        const auto connected = std::ranges::any_of(
            module.connections, [&child](const Connection &connection) {
              return connection.source == child.id;
            });
        if (!connected)
          definition.connections.push_back(
              {{child.id, 2}, {graph::boundaryNodeId, 2}});
      }
    appendPresentation(definition, module.interfaceState);
    definition.role = inferModuleRole(module);
    if (definition.role == graph::GraphRole::generic)
      definition.role = graph::inferGraphRole(definition);
    if (definition.role == graph::GraphRole::generic)
      definition.role = graph::GraphRole::utility;
    if (module.factoryModule)
      module.definitionCache = definition;
    definitions.push_back(std::move(definition));
  }

  const auto moduleMs =
      cydaw::base::Time::highResolutionTicksToSeconds(
          cydaw::base::Time::getHighResolutionTicks() - definitionsMark) *
      1000.0;
  const auto rootMark = cydaw::base::Time::getHighResolutionTicks();
  graph::GraphDefinition root;
  root.reference = graphReference_;
  root.displayName = (graphName_.isNotEmpty()     ? graphName_
                      : contextName_.isNotEmpty() ? contextName_
                                                  : "Graph")
                         .toStdString();
  root.description = graphDescription_.toStdString();
  root.role = inferCurrentGraphRole();
  root.scope = graphScope_;
  for (const auto &tag : graphTags_)
    root.tags.push_back(tag.toStdString());
  std::uint8_t rootAudioChannels = 2;
  for (const auto &candidate : nodes_)
    if (isBoundaryNode(candidate) &&
        ((candidate.hasInput && candidate.inputKind == PortKind::audio) ||
         (candidate.hasOutput && candidate.outputKind == PortKind::audio))) {
      rootAudioChannels = candidate.hasOutput ? audioOutputChannels(candidate)
                                              : audioInputChannels(candidate);
      break;
    }
  for (const auto &node : nodes_) {
    if (isBoundaryInput(node.typeKey)) {
      root.ports.push_back(
          makePort(node.id,
                   node.typeKey + "-" +
                       cydaw::base::Text(static_cast<std::int64_t>(node.id)),
                   node.outputKind, graph::PortDirection::input,
                   audioOutputChannels(node), displayTitle(node)));
      if (!node.ports.empty())
        root.ports.back().defaultValue = node.ports.front().defaultValue;
      continue;
    }
    if (isBoundaryOutput(node.typeKey)) {
      root.ports.push_back(
          makePort(node.id,
                   node.typeKey + "-" +
                       cydaw::base::Text(static_cast<std::int64_t>(node.id)),
                   node.inputKind, graph::PortDirection::output,
                   audioInputChannels(node), displayTitle(node)));
      continue;
    }
    graph::GraphNode converted;
    converted.id = node.id;
    converted.displayName = displayTitle(node).toStdString();
    converted.description = node.description.toStdString();
    if (const auto *module = findModule(node.typeKey)) {
      converted.kind = graph::GraphNodeKind::graphInstance;
      converted.graph = moduleReference(*module);
      const auto moduleDefinition = std::ranges::find_if(
          definitions, [&converted](const auto &candidate) {
            return converted.graph.has_value() &&
                   candidate.reference == *converted.graph;
          });
      if (moduleDefinition != definitions.end())
        converted.ports = moduleDefinition->ports;
      for (auto &port : converted.ports)
        if (const auto instancePort =
                std::ranges::find(node.ports, port.id, &NodePort::id);
            instancePort != node.ports.end())
          port.defaultValue = instancePort->defaultValue;
      if (moduleDefinition != definitions.end())
        appendGraphInstanceOverrides(converted, node.interfaceState, node.id,
                                     *moduleDefinition);
    } else {
      const auto processorKey = primitiveProcessorKeyForType(node.typeKey);
      converted.processorKey =
          (processorKey.isNotEmpty() ? processorKey : node.typeKey)
              .toStdString();
      if (!node.ports.empty())
        for (const auto &port : node.ports)
          converted.ports.push_back(makeExactPort(port));
      else {
        if (node.hasInput)
          converted.ports.push_back(makePort(
              1, "input", node.inputKind, graph::PortDirection::input,
              node.inputKind == PortKind::audio ? rootAudioChannels : 1));
        if (node.hasOutput)
          converted.ports.push_back(makePort(
              2, "output", node.outputKind, graph::PortDirection::output,
              node.outputKind == PortKind::audio ? rootAudioChannels : 1));
      }
      appendPrimitiveContract(converted, node.typeKey, node.parameters);
    }
    root.nodes.push_back(std::move(converted));
  }
  const auto rootNodePort = [&root](const NodeId nodeId,
                                    const graph::PortDirection direction,
                                    const PortKind kind,
                                    const graph::PortId fallback) {
    const auto node =
        std::ranges::find_if(root.nodes, [nodeId](const auto &candidate) {
          return candidate.id == nodeId;
        });
    if (node == root.nodes.end())
      return fallback;
    const auto expectedType = kind == PortKind::audio  ? graph::PortType::audio
                              : kind == PortKind::midi ? graph::PortType::midi
                              : kind == PortKind::control
                                  ? graph::PortType::value
                                  : graph::PortType::transport;
    const auto matchesKind = [kind, expectedType](const auto &port) {
      return port.type == expectedType ||
             (kind == PortKind::control &&
              (port.type == graph::PortType::trigger ||
               port.type == graph::PortType::analysis));
    };
    const auto exact = std::ranges::find_if(
        node->ports,
        [direction, fallback, &matchesKind](const auto &candidate) {
          return candidate.id == fallback && candidate.direction == direction &&
                 matchesKind(candidate);
        });
    if (exact != node->ports.end())
      return exact->id;
    const auto port = std::ranges::find_if(
        node->ports, [direction, &matchesKind](const auto &candidate) {
          return candidate.direction == direction && matchesKind(candidate);
        });
    return port != node->ports.end() ? port->id : fallback;
  };
  for (const auto &connection : connections_) {
    const auto *source = findNode(connection.source);
    const auto *destination = findNode(connection.destination);
    if (source == nullptr || destination == nullptr)
      continue;
    const graph::GraphEndpoint sourceEndpoint =
        isBoundaryInput(source->typeKey)
            ? graph::GraphEndpoint{graph::boundaryNodeId, source->id}
            : graph::GraphEndpoint{
                  source->id,
                  rootNodePort(source->id, graph::PortDirection::output,
                               connection.kind, connection.sourcePort)};
    const graph::GraphEndpoint destinationEndpoint =
        isBoundaryOutput(destination->typeKey)
            ? graph::GraphEndpoint{graph::boundaryNodeId, destination->id}
            : graph::GraphEndpoint{
                  destination->id,
                  rootNodePort(destination->id, graph::PortDirection::input,
                               connection.kind, connection.destinationPort)};
    root.connections.push_back({sourceEndpoint, destinationEndpoint,
                                connection.mode,
                                static_cast<double>(connection.amount)});
  }
  appendPresentation(root, graphInterfaceState_);
  definitions.push_back(std::move(root));
  const auto rootMs =
      cydaw::base::Time::highResolutionTicksToSeconds(
          cydaw::base::Time::getHighResolutionTicks() - rootMark) *
      1000.0;
  // Numbers only: this runs on the click path, and formatting them here
  // would put a handful of string allocations on it.
  ++definitionsCalls_;
  definitionsLastModulesMs_ = moduleMs;
  definitionsLastRootMs_ = rootMs;
  definitionsWorstMs_ = std::max(definitionsWorstMs_, moduleMs + rootMs);
  return definitions;
}
cydaw::graph::PortId
GraphDocument::knobPortId(const cydaw::base::Text &classKey) {
  // Far above node ids, which a module's own sockets are named by.
  return (cydaw::graph::PortId{1} << 40) + stableModuleParameterId(classKey);
}
cydaw::base::Text
GraphDocument::knobNodeType(const cydaw::base::Text &moduleKey,
                            const cydaw::base::Text &classKey) const {
  const auto *module = findModule(moduleKey);
  auto rest = classKey;
  cydaw::base::Text typeKey;
  for (auto depth = 0;
       module != nullptr && rest.startsWith("node.") && depth < 16; ++depth) {
    const auto separator = rest.indexOfChar(5, '.');
    if (separator < 0)
      break;
    const auto id =
        static_cast<NodeId>(rest.substring(5, separator).getLargeIntValue());
    rest = rest.substring(separator + 1);
    const auto child = std::ranges::find(module->nodes, id, &ModuleNode::id);
    if (child == module->nodes.end())
      return {};
    typeKey = child->typeKey;
    module = findModule(typeKey);
  }
  return module == nullptr ? typeKey : cydaw::base::Text();
}
std::pair<double, double>
GraphDocument::knobRange(const cydaw::base::Text &moduleKey,
                         const cydaw::base::Text &classKey) const {
  const auto typeKey = knobNodeType(moduleKey, classKey);
  if (typeKey.isNotEmpty())
    return primitiveParameterRange(
        primitiveProcessorKeyForType(typeKey).toStdString(),
        primitiveParameterKeyForBinding(innermostParameterSuffix(classKey))
            .toStdString());
  if (const auto *module = findModule(moduleKey))
    for (const auto &parameter : module->interfaceState.parameters)
      if (parameter.key == classKey)
        return {parameter.minimum, parameter.maximum};
  return {0.0, 1.0};
}
void GraphDocument::applyDefaultCableMode(const std::uint64_t source,
                                          const std::uint64_t destination,
                                          const std::uint64_t destinationPort) {
  const auto *from = findNode(source);
  if (from == nullptr ||
      primitiveProcessorKeyForType(from->typeKey) != "core.lfo")
    return;
  for (auto &connection : connections_)
    if (connection.source == source && connection.destination == destination &&
        connection.destinationPort == destinationPort &&
        connection.kind == PortKind::control && connection.mode == 0 &&
        controlCableTarget(connection).has_value()) {
      connection.mode = 1;
      connection.amount = 0.25f;
    }
}
cydaw::base::Text
GraphDocument::knobOfSocket(const Node &node,
                            const cydaw::base::Text &portKey) const {
  const auto prefix = nodeParameterBinding(node.id, {});
  cydaw::base::Text found;
  for (const auto &parameter : presentationForNode(node).parameters) {
    if (!parameter.key.startsWith(prefix))
      continue;
    const auto suffix = parameter.key.substring(prefix.length());
    // "opale.osc1.level" for "osc1.level", not the "level" of "opale.level".
    if (suffix == portKey ||
        suffix.fromFirstOccurrenceOf(".", false, false) == portKey)
      return parameter.key;
    if (found.isEmpty() && valuePortKeyForBinding(suffix) == portKey)
      found = parameter.key;
  }
  return found;
}
std::vector<GraphDocument::EditorModulation>
GraphDocument::editorModulations() const {
  std::vector<EditorModulation> out;
  for (const auto &connection : connections_) {
    if (connection.kind != PortKind::control)
      continue;
    const auto target = controlCableTarget(connection);
    if (!target || target->binding.isEmpty())
      continue;
    out.push_back({connection.source, connection.sourcePort,
                   connection.destination, connection.destinationPort,
                   target->binding, static_cast<int>(connection.mode),
                   connection.amount});
  }
  return out;
}
std::uint64_t
GraphDocument::editorModulatorOwner(const std::uint64_t modulator) const {
  const auto *node = readNode(modulator);
  if (node == nullptr)
    return 0;
  const auto *effect =
      cydaw::engine::effectForType(node->typeKey.toStdString());
  if (effect == nullptr || !effect->control)
    return 0;
  namespace modulators = cydaw::engine::modulator;
  return modulators::ownerOf(
      node->parameters.eqBands[modulators::ownerLowSlot],
      node->parameters.eqBands[modulators::ownerHighSlot]);
}
void GraphDocument::remapModulatorOwners(
    const std::unordered_map<NodeId, NodeId> &remapped) {
  namespace modulators = cydaw::engine::modulator;
  for (const auto &[previous, copied] : remapped) {
    base::ignoreUnused(previous);
    const auto owner = editorModulatorOwner(copied);
    if (owner == 0)
      continue;
    const auto moved = remapped.find(owner);
    if (moved == remapped.end())
      continue;
    auto *node = findNode(copied);
    if (node == nullptr)
      continue;
    node->parameters.eqBands[modulators::ownerLowSlot] =
        modulators::ownerLowOf(moved->second);
    node->parameters.eqBands[modulators::ownerHighSlot] =
        modulators::ownerHighOf(moved->second);
    for (auto &parameter : node->interfaceState.parameters)
      if (parameter.key.endsWith(".ownerLow"))
        parameter.value = parameter.defaultValue =
            modulators::ownerLowOf(moved->second);
      else if (parameter.key.endsWith(".ownerHigh"))
        parameter.value = parameter.defaultValue =
            modulators::ownerHighOf(moved->second);
    node->serialised = cydaw::base::Value();
  }
}
std::uint64_t
GraphDocument::editorAddModulator(const std::uint64_t owner,
                                  const cydaw::base::Text &typeKey) {
  const auto *effect = cydaw::engine::effectForType(typeKey.toStdString());
  const auto *card = readNode(owner);
  if (editingLocked_ || effect == nullptr || !effect->control ||
      card == nullptr)
    return 0;
  // Under its card in the graph, after the modulators it has.
  auto count = 0;
  for (const auto &node : nodes_)
    if (editorModulatorOwner(node.id) == owner)
      ++count;
  const auto position =
      card->position +
      base::Point<float>(0.0f, 0.14f + 0.07f * static_cast<float>(count));
  // Added without a history step of its own: with its card set below, one
  // step (undone, no modulator without a card is left).
  std::uint64_t id{};
  {
    const cydaw::base::ScopedValueSetter<bool> quiet(restoringHistory_, true);
    id = editorAddNode(typeKey, position);
  }
  auto *made = findNode(id);
  if (made == nullptr)
    return 0;
  namespace modulators = cydaw::engine::modulator;
  made->parameters.eqBands[modulators::ownerLowSlot] =
      modulators::ownerLowOf(owner);
  made->parameters.eqBands[modulators::ownerHighSlot] =
      modulators::ownerHighOf(owner);
  for (auto &parameter : made->interfaceState.parameters)
    if (parameter.key.endsWith(".ownerLow"))
      parameter.value = parameter.defaultValue = modulators::ownerLowOf(owner);
    else if (parameter.key.endsWith(".ownerHigh"))
      parameter.value = parameter.defaultValue = modulators::ownerHighOf(owner);
  made->serialised = cydaw::base::Value();
  notifyLayoutChanged("v2-modulator");
  return id;
}
std::uint64_t
GraphDocument::editorParameterSocket(const std::uint64_t id,
                                     const cydaw::base::Text &binding) const {
  const auto *node = readNode(id);
  if (node == nullptr)
    return 0;
  const auto suffix = parameterSuffix(binding);
  const auto portKey = valuePortKeyForBinding(suffix);
  const auto feeds = [](const NodePort &port) {
    return port.input && (port.type == cydaw::graph::PortType::value ||
                          port.type == cydaw::graph::PortType::trigger);
  };
  // A module's knob socket, the primitive's port of its name, the socket
  // whose knob it is.
  for (const auto &port : node->ports)
    if (feeds(port) && port.key == "knob." + suffix)
      return port.id;
  for (const auto &port : node->ports)
    if (feeds(port) && portKey.isNotEmpty() && port.key == portKey &&
        knobOfSocket(*node, port.key) == binding)
      return port.id;
  for (const auto &port : node->ports)
    if (feeds(port) && knobOfSocket(*node, port.key) == binding)
      return port.id;
  return 0;
}
cydaw::base::Text
GraphDocument::editorModulate(const std::uint64_t modulator,
                              const cydaw::base::Text &binding,
                              const float amount) {
  if (editingLocked_)
    return "locked";
  const auto *source = readNode(modulator);
  if (source == nullptr || binding.length() < 6 || !binding.startsWith("node."))
    return "no such modulator or knob";
  const auto separator = binding.indexOfChar(5, '.');
  const auto destination = static_cast<std::uint64_t>(
      binding.substring(5, separator).getLargeIntValue());
  const auto socket = editorParameterSocket(destination, binding);
  if (socket == 0)
    return "this knob has no socket a modulator can drive";
  const auto output =
      std::ranges::find_if(source->ports, [](const NodePort &port) {
        return !port.input && port.type == cydaw::graph::PortType::value;
      });
  if (output == source->ports.end())
    return "the modulator gives no value";
  const auto already =
      std::ranges::find_if(connections_, [&](const Connection &connection) {
        return connection.source == modulator &&
               connection.destination == destination &&
               connection.destinationPort == socket;
      });
  if (already != connections_.end() && already->mode == 1) {
    (void)editorSetModulationAmount(modulator, already->sourcePort, destination,
                                    socket, amount);
    return {};
  }
  if (already == connections_.end())
    if (const auto refused =
            editorConnect(modulator, output->id, destination, socket);
        refused.isNotEmpty())
      return refused;
  (void)editorSetConnectionMode(modulator, output->id, destination, socket, 1,
                                amount);
  return {};
}
bool GraphDocument::editorSetModulationAmount(
    const std::uint64_t source, const std::uint64_t sourcePort,
    const std::uint64_t destination, const std::uint64_t destinationPort,
    const float amount) {
  if (editingLocked_)
    return false;
  const auto found =
      std::ranges::find_if(connections_, [&](const Connection &connection) {
        return connection.source == source &&
               connection.destination == destination &&
               connection.sourcePort == sourcePort &&
               connection.destinationPort == destinationPort;
      });
  if (found == connections_.end() || found->kind != PortKind::control)
    return false;
  const auto next =
      std::isfinite(amount) ? base::limit(-1.0f, 1.0f, amount) : 0.0f;
  if (std::abs(found->amount - next) < 1.0e-6f)
    return true;
  found->amount = next;
  for (auto &node : nodes_)
    if (node.id == source || node.id == destination)
      node.serialised = cydaw::base::Value();
  // As a knob: heard at once, the undo step once it rests.
  parameterHistoryPending_ = true;
  lastParameterUiChangeTimeMs_ = cydaw::base::Time::currentTimeMillis();
  dispatchParameterNotification();
  ;
  return true;
}
bool GraphDocument::editorUnmodulate(const std::uint64_t modulator,
                                     const cydaw::base::Text &binding) {
  for (const auto &modulation : editorModulations())
    if (modulation.source == modulator && modulation.binding == binding)
      return editorDisconnect(modulation.source, modulation.sourcePort,
                              modulation.node, modulation.port);
  return false;
}
std::optional<GraphDocument::ControlCableTarget>
GraphDocument::controlCableTarget(const Connection &connection) const {
  if (connection.kind != PortKind::control)
    return std::nullopt;
  const auto *destination = findNode(connection.destination);
  if (destination == nullptr)
    return std::nullopt;
  const auto *port = findPort(*destination, connection.destinationPort);
  if (port == nullptr)
    return std::nullopt;
  const auto &parameters = presentationForNode(*destination).parameters;
  if (port->key.startsWith("knob.")) {
    const auto classKey = port->key.substring(5);
    const auto instanceKey = nodeParameterBinding(destination->id, classKey);
    const auto range = knobRange(destination->typeKey, classKey);
    for (const auto &parameter : parameters)
      if (parameter.key == instanceKey || parameter.key == classKey)
        return ControlCableTarget{parameter.key, range.first, range.second};
    if (knobNodeType(destination->typeKey, classKey).isNotEmpty())
      return ControlCableTarget{{}, range.first, range.second};
    return std::nullopt;
  }
  const auto processor =
      primitiveProcessorKeyForType(destination->typeKey).toStdString();
  const auto parameterKey =
      std::string(cydaw::engine::nativeDspParameterKeyForPort(
          processor, port->key.toStdString()));
  if (parameterKey.empty()) {
    // An instrument's setting socket: the knob it names.
    const auto binding = knobOfSocket(*destination, port->key);
    for (const auto &parameter : parameters)
      if (binding.isNotEmpty() && parameter.key == binding)
        return ControlCableTarget{parameter.key, parameter.minimum,
                                  parameter.maximum};
    return std::nullopt;
  }
  ControlCableTarget target;
  const auto range = primitiveParameterRange(processor, parameterKey);
  target.minimum = range.first;
  target.maximum = range.second;
  // The knob that port drives: its binding ends with the port's name,
  // except where the two are spelled differently.
  for (const auto &parameter : parameters)
    if (parameter.key.endsWith("." + port->key) ||
        (port->key == "gain" && parameter.key.endsWith("gain.db")) ||
        (port->key == "drive" && parameter.key.endsWith("drive.db"))) {
      target.binding = parameter.key;
      break;
    }
  return target;
}
cydaw::graph::ValidationResult GraphDocument::validateGraphAsset() const {
  auto definitions = graphDefinitions();
  // The primitives are checked from the batch before the library takes it,
  // so the batch can be handed over rather than copied.
  const auto &primitives = cydaw::graph::factoryPrimitiveRegistry();
  std::vector<cydaw::graph::ValidationIssue> primitiveIssues;
  for (const auto &definition : definitions)
    for (const auto &node : definition.nodes) {
      if (node.kind != cydaw::graph::GraphNodeKind::primitive ||
          !cydaw::base::Text(node.processorKey).startsWith("core."))
        continue;
      auto primitiveResult = primitives.validateNode(node);
      primitiveIssues.insert(
          primitiveIssues.end(),
          std::make_move_iterator(primitiveResult.issues.begin()),
          std::make_move_iterator(primitiveResult.issues.end()));
    }
  cydaw::graph::GraphLibrary library;
  auto result = library.installBatch(std::move(definitions));
  if (!result.ok())
    return result;
  result.issues.insert(result.issues.end(),
                       std::make_move_iterator(primitiveIssues.begin()),
                       std::make_move_iterator(primitiveIssues.end()));
  return result;
}
void GraphDocument::initialiseValueModulationGraph() {
  ;
  valueGraphMode_ = true;
  graphRole_ = cydaw::graph::GraphRole::modulator;
  refreshingSelectionUi_ = true;
  ;
  refreshingSelectionUi_ = false;
  initialiseCatalog();
  nodes_.clear();
  connections_.clear();
  selectedNodes_.clear();
  activeFactoryPresetKey_.clear();
  nextNodeId_ = 1;

  if (const auto *lfo = findType("lfo"))
    addNodeFromType(*lfo, {0.25f, 0.36f});
  if (const auto *output = findType("graph-value-output"))
    addNodeFromType(*output, {0.66f, 0.36f});
  if (nodes_.size() >= 2)
    (void)connectNodes(nodes_[0].id, nodes_[1].id);

  selectedNodes_.clear();
  graphInterfaceState_ = createDefaultGraphInterface();
  synchroniseGraphInterface();
  ;
  ;
  ;
  ;
  refreshGraphAssetValidation();
  ;
}
double GraphDocument::evaluateValueOutput(const double timeSeconds) const {
  const auto inputOf = [this](const NodeId destination,
                              const std::uint64_t destinationPort) -> NodeId {
    const auto found = std::find_if(
        connections_.begin(), connections_.end(),
        [destination, destinationPort](const auto &connection) {
          return connection.destination == destination &&
                 connection.kind == PortKind::control &&
                 (connection.destinationPort == destinationPort ||
                  (destinationPort == 1 && connection.destinationPort == 0));
        });
    return found != connections_.end() ? found->source : 0;
  };

  std::set<NodeId> visiting;
  std::function<double(NodeId)> evaluate =
      [this, &inputOf, &visiting, timeSeconds,
       &evaluate](const NodeId nodeId) -> double {
    const auto *node = findNode(nodeId);
    if (node == nullptr || !visiting.insert(nodeId).second)
      return 0.0;

    double result{};
    if (node->typeKey == "lfo" || node->typeKey == "lfo-core") {
      const auto *lfoParameters = &node->parameters;
      auto instanceParameters = node->parameters;
      if (const auto *module = findModule(node->typeKey)) {
        const auto core = std::find_if(
            module->nodes.begin(), module->nodes.end(),
            [](const auto &child) { return child.typeKey == "lfo-core"; });
        if (core != module->nodes.end()) {
          instanceParameters = core->parameters;
          for (const auto &parameter : node->interfaceState.parameters)
            (void)setNativeParameter(
                instanceParameters,
                graphInstanceClassParameterKey(node->id, parameter.key),
                parameter.value);
          lfoParameters = &instanceParameters;
        }
      }
      const auto phase =
          std::fmod(std::max(0.0, timeSeconds) *
                        static_cast<double>(lfoParameters->modulationRateHz),
                    1.0);
      const auto shape = base::limit(
          0, 3, static_cast<int>(std::lround(lfoParameters->modulationShape)));
      double waveform{};
      if (shape == 1)
        waveform = 1.0 - 4.0 * std::abs(phase - 0.5);
      else if (shape == 2)
        waveform = phase * 2.0 - 1.0;
      else if (shape == 3)
        waveform = phase < 0.5 ? 1.0 : -1.0;
      else
        waveform = std::sin(base::MathConstants<double>::twoPi * phase);
      result = lfoParameters->modulationOffset +
               waveform * lfoParameters->modulationDepth;
    } else if (node->typeKey == "macro") {
      result = node->parameters.modulationOffset;
    } else if (node->typeKey == "value-constant") {
      result = node->parameters.valueOperand;
    } else {
      const auto inputValue = [&](const std::uint64_t port,
                                  const double fallback) {
        const auto source = inputOf(nodeId, port);
        return source != 0 ? evaluate(source) : fallback;
      };
      const auto a = inputValue(1, node->parameters.valueInputA);
      const auto b = inputValue(2, node->parameters.valueInputB);
      result = a;
      if (node->typeKey == "control-math")
        result = result * node->parameters.modulationDepth +
                 node->parameters.modulationOffset;
      else if (node->typeKey == "value-add")
        result = a + b;
      else if (node->typeKey == "value-subtract")
        result = a - b;
      else if (node->typeKey == "value-multiply")
        result = a * b;
      else if (node->typeKey == "value-divide") {
        const auto denominator = b;
        result = std::abs(denominator) > 1.0e-12 ? result / denominator : 0.0;
      } else if (node->typeKey == "value-modulo")
        result = std::abs(b) > 1.0e-12 ? std::fmod(a, b) : 0.0;
      else if (node->typeKey == "value-power") {
        const auto powered = std::pow(a, b);
        result = std::isfinite(powered) ? powered : 0.0;
      } else if (node->typeKey == "value-sine")
        result = std::sin(a);
      else if (node->typeKey == "value-cosine")
        result = std::cos(a);
      else if (node->typeKey == "value-square-root")
        result = a >= 0.0 ? std::sqrt(a) : 0.0;
      else if (node->typeKey == "value-floor")
        result = std::floor(a);
      else if (node->typeKey == "value-ceil")
        result = std::ceil(a);
      else if (node->typeKey == "value-round")
        result = std::round(a);
      else if (node->typeKey == "value-min")
        result = std::min(a, b);
      else if (node->typeKey == "value-max")
        result = std::max(a, b);
      else if (node->typeKey == "value-clamp")
        result = base::limit(
            static_cast<double>(std::min(node->parameters.valueMinimum,
                                         node->parameters.valueMaximum)),
            static_cast<double>(std::max(node->parameters.valueMinimum,
                                         node->parameters.valueMaximum)),
            result);
      else if (node->typeKey == "value-abs")
        result = std::abs(result);
      else if (node->typeKey == "value-negate")
        result = -result;
      else if (node->typeKey == "map-range") {
        const auto inputMinimum =
            static_cast<double>(node->parameters.mapInputMinimum);
        const auto inputMaximum =
            static_cast<double>(node->parameters.mapInputMaximum);
        const auto denominator = inputMaximum - inputMinimum;
        const auto normalised = std::abs(denominator) > 1.0e-12
                                    ? (result - inputMinimum) / denominator
                                    : 0.0;
        result =
            static_cast<double>(node->parameters.mapOutputMinimum) +
            normalised * static_cast<double>(node->parameters.mapOutputMaximum -
                                             node->parameters.mapOutputMinimum);
      } else if (node->typeKey == "logic-greater")
        result = a > b ? 1.0 : 0.0;
      else if (node->typeKey == "logic-less")
        result = a < b ? 1.0 : 0.0;
      else if (node->typeKey == "logic-equal")
        result = std::abs(a - b) <= 1.0e-6 ? 1.0 : 0.0;
      else if (node->typeKey == "logic-and")
        result = a > 0.5 && b > 0.5 ? 1.0 : 0.0;
      else if (node->typeKey == "logic-or")
        result = a > 0.5 || b > 0.5 ? 1.0 : 0.0;
      else if (node->typeKey == "logic-xor")
        result = (a > 0.5) != (b > 0.5) ? 1.0 : 0.0;
      else if (node->typeKey == "logic-not")
        result = result > 0.5 ? 0.0 : 1.0;
    }

    visiting.erase(nodeId);
    return std::isfinite(result) ? result : 0.0;
  };

  const auto output =
      std::find_if(nodes_.begin(), nodes_.end(), [](const auto &node) {
        return node.typeKey == "graph-value-output";
      });
  if (output != nodes_.end())
    return evaluate(output->id);

  const auto terminal =
      std::find_if(nodes_.rbegin(), nodes_.rend(), [](const auto &node) {
        return node.hasOutput && node.outputKind == PortKind::control;
      });
  return terminal != nodes_.rend() ? evaluate(terminal->id) : 0.0;
}
bool GraphDocument::setGraphInterfaceParameter(const cydaw::base::Text &binding,
                                               const double value) {
  auto changed = false;
  for (auto &node : nodes_) {
    if (findModule(node.typeKey) != nullptr) {
      if (updateGraphInstanceParameter(node, binding, value)) {
        changed = true;
        node.serialised = cydaw::base::Value();
      }
    } else if (updateNodeParameter(node, binding, value)) {
      changed = true;
      node.serialised = cydaw::base::Value();
      for (auto &parameter : node.interfaceState.parameters)
        if (parameter.key == binding)
          parameter.value = value;
    }
  }
  if (!changed)
    return false;
  for (auto &parameter : graphInterfaceState_.parameters)
    if (parameter.key == binding)
      parameter.value = value;
  ;
  ;
  ;
  ;
  ;
  ;
  ;
  ;
  return true;
}
bool GraphDocument::updateNodeParameter(Node &node,
                                        const cydaw::base::Text &binding,
                                        const double value) {
  if (binding.startsWith("node.")) {
    const auto separator = binding.indexOfChar(5, '.');
    if (separator < 0 ||
        static_cast<NodeId>(
            binding.substring(5, separator).getLargeIntValue()) != node.id)
      return false;
  }
  if (!bindingMatchesType(node.typeKey, binding) ||
      !setNativeParameter(node.parameters, binding, value))
    return false;
  applyLegacyPortDefaults(node.typeKey, node.parameters, node.ports);
  return true;
}
bool GraphDocument::updateModuleParameter(SavedModule &module,
                                          const cydaw::base::Text &binding,
                                          const double value) {
  for (auto &node : module.nodes) {
    if (findModule(node.typeKey) != nullptr) {
      const auto parameter = std::ranges::find_if(
          node.interfaceState.parameters, [&binding](const auto &candidate) {
            return candidate.key == binding;
          });
      if (parameter != node.interfaceState.parameters.end()) {
        parameter->value =
            base::limit(parameter->minimum, parameter->maximum, value);
        return true;
      }
      continue;
    }
    if (bindingMatchesType(node.typeKey, binding) &&
        setNativeParameter(node.parameters, binding, value)) {
      applyLegacyPortDefaults(node.typeKey, node.parameters, node.ports);
      return true;
    }
  }
  return false;
}
bool GraphDocument::updateGraphInstanceParameter(
    Node &node, const cydaw::base::Text &binding, const double value) {
  if (findModule(node.typeKey) == nullptr)
    return false;
  const auto exact = std::ranges::find_if(
      node.interfaceState.parameters,
      [&binding](const auto &parameter) { return parameter.key == binding; });
  if (exact != node.interfaceState.parameters.end()) {
    exact->value = base::limit(exact->minimum, exact->maximum, value);
    return true;
  }
  // Projects created before schema 17 stored the class key directly.
  const auto classKey = graphInstanceClassParameterKey(node.id, binding);
  const auto migrated = std::ranges::find_if(
      node.interfaceState.parameters,
      [&classKey](const auto &parameter) { return parameter.key == classKey; });
  if (migrated == node.interfaceState.parameters.end())
    return false;
  migrated->value = base::limit(migrated->minimum, migrated->maximum, value);
  return true;
}
cydaw::base::Value
GraphDocument::moduleGraphState(const SavedModule &module) const {
  auto state = serialise();
  auto *root = state.getDynamicObject();
  if (root == nullptr)
    return {};

  cydaw::base::Array<cydaw::base::Value> nodeValues;
  NodeId highestId{};
  for (const auto &child : module.nodes) {
    auto value = cydaw::base::Object::Ptr(new cydaw::base::Object());
    value->setProperty(jsonKeys::id, static_cast<std::int64_t>(child.id));
    value->setProperty(jsonKeys::type, child.typeKey);
    value->setProperty(jsonKeys::title, child.title);
    value->setProperty(jsonKeys::x, child.position.x);
    value->setProperty(jsonKeys::y, child.position.y);
    value->setProperty(jsonKeys::alias, child.alias);
    value->setProperty(jsonKeys::description, child.description);
    value->setProperty(jsonKeys::inputChannels,
                       static_cast<int>(child.inputChannels));
    value->setProperty(jsonKeys::outputChannels,
                       static_cast<int>(child.outputChannels));
    value->setProperty(jsonKeys::parameters,
                       serialiseNativeParameters(child.parameters));
    value->setProperty(jsonKeys::portDefaults,
                       serialisePortDefaults(child.ports));
    value->setProperty(jsonKeys::interface,
                       serialiseModuleInterface(child.interfaceState));
    value->setProperty(jsonKeys::interfaceLayoutOverridden,
                       child.interfaceLayoutOverridden);
    value->setProperty(jsonKeys::collapsed, child.collapsed);
    writeCardAssets(*value, child.wavetableShape, child.soundModelJson,
                    child.samplerJson, child.prismTablesJson, child.impulseJson,
                    child.bypassed, child.instrumentJson, child.branchNames);
    nodeValues.add(cydaw::base::Value(value.get()));
    highestId = std::max(highestId, child.id);
  }
  cydaw::base::Array<cydaw::base::Value> links;
  for (const auto &connection : module.connections) {
    auto value = cydaw::base::Object::Ptr(new cydaw::base::Object());
    value->setProperty(jsonKeys::source,
                       static_cast<std::int64_t>(connection.source));
    value->setProperty(jsonKeys::destination,
                       static_cast<std::int64_t>(connection.destination));
    value->setProperty(jsonKeys::kind, static_cast<int>(connection.kind));
    value->setProperty(jsonKeys::audioChannels,
                       static_cast<int>(connection.audioChannels));
    value->setProperty(jsonKeys::sourcePort,
                       static_cast<std::int64_t>(connection.sourcePort));
    value->setProperty(jsonKeys::destinationPort,
                       static_cast<std::int64_t>(connection.destinationPort));
    if (connection.mode != 0) {
      value->setProperty("mode", static_cast<int>(connection.mode));
      value->setProperty("amount", static_cast<double>(connection.amount));
    }
    links.add(cydaw::base::Value(value.get()));
  }
  root->setProperty(jsonKeys::nodes, nodeValues);
  root->setProperty(jsonKeys::connections, links);
  root->setProperty(jsonKeys::presentation,
                    serialiseModuleInterface(module.interfaceState));
  root->setProperty(jsonKeys::nextNodeId,
                    static_cast<std::int64_t>(highestId + 1));
  root->setProperty(jsonKeys::factoryPreset, cydaw::base::Text{});
  root->setProperty(jsonKeys::graphName, module.name);
  root->setProperty(jsonKeys::graphCategory, module.category);
  root->setProperty(jsonKeys::graphAuthor, module.author);
  root->setProperty("programs", serialiseGraphPrograms(module.programs));
  root->setProperty(jsonKeys::graphDescription, module.description);
  cydaw::base::Array<cydaw::base::Value> moduleTags;
  for (const auto &tag : module.tags)
    moduleTags.add(tag);
  root->setProperty(jsonKeys::graphTags, moduleTags);
  const auto role = inferModuleRole(module);
  root->setProperty(jsonKeys::definitionId,
                    cydaw::base::Text(graphReference_.id) + ".module." +
                        module.key);
  root->setProperty(jsonKeys::definitionVersion,
                    cydaw::base::Text(module.version.toString()));
  root->setProperty(jsonKeys::graphRole, cydaw::graph::toString(role));
  root->setProperty(jsonKeys::graphScope,
                    cydaw::graph::toString(
                        module.factoryModule ? cydaw::graph::GraphScope::factory
                        : module.inlineModule
                            ? cydaw::graph::GraphScope::inlineTrack
                            : graphScope_));
  root->setProperty(jsonKeys::valueGraph,
                    role == cydaw::graph::GraphRole::modulator);
  root->setProperty(jsonKeys::viewZoom, 1.0);
  root->setProperty(jsonKeys::viewOffsetX, 0.0);
  root->setProperty(jsonKeys::viewOffsetY, 0.0);
  // Not the parent graph's: its notes and where its view was would show
  // inside the device (and be saved with it to the library).
  root->setProperty(jsonKeys::comments,
                    cydaw::base::Array<cydaw::base::Value>{});
  for (const auto &key :
       {jsonKeys::viewCentreX, jsonKeys::viewCentreY, jsonKeys::viewAnchorNode,
        jsonKeys::viewAnchorDX, jsonKeys::viewAnchorDY})
    root->removeProperty(key);
  return state;
}
void GraphDocument::applyModuleGraphState(const cydaw::base::Text &moduleKey,
                                          const cydaw::base::Value &state,
                                          const bool structureChanged) {
  if (applyingModuleGraphState_)
    return;
  const auto *root = state.getDynamicObject();
  if (root == nullptr)
    return;
  const auto stateSchema =
      static_cast<int>(root->getProperty(jsonKeys::schema));

  // A subgraph owns the same reusable-module library as its parent. Merge
  // definitions first so a newly composed module can immediately be used by
  // nodes in the graph currently being edited.
  if (const auto *moduleValues =
          root->getProperty(jsonKeys::modules).getArray()) {
    // Only the classes this device uses (within it, and within those, as
    // deep as they go), and those it brought: the state carries every
    // class of the library as it was when this device was opened, and
    // another device's class, edited meanwhile elsewhere (another
    // workshop tab), would be put back as it was.
    std::map<cydaw::base::Text, const cydaw::base::Object *> incoming;
    for (const auto &moduleValue : *moduleValues)
      if (const auto *object = moduleValue.getDynamicObject())
        if (const auto key = object->getProperty(jsonKeys::key).toString();
            key.isNotEmpty())
          incoming[key] = object;
    std::set<cydaw::base::Text> used;
    std::vector<const cydaw::base::Value *> pendingNodes{
        &root->getProperty(jsonKeys::nodes)};
    while (!pendingNodes.empty()) {
      const auto *nodesValue = pendingNodes.back();
      pendingNodes.pop_back();
      if (const auto *list = nodesValue->getArray())
        for (const auto &child : *list)
          if (const auto type =
                  child.getProperty(jsonKeys::type, {}).toString();
              incoming.contains(type) && used.insert(type).second)
            pendingNodes.push_back(
                &incoming[type]->getProperty(jsonKeys::nodes));
    }
    for (const auto &moduleValue : *moduleValues) {
      const auto *object = moduleValue.getDynamicObject();
      const auto key = object != nullptr
                           ? object->getProperty(jsonKeys::key).toString()
                           : cydaw::base::Text{};
      if (object == nullptr || key.isEmpty() || key == moduleKey)
        continue;
      if (findModule(key) != nullptr && !used.contains(key))
        continue;
      SavedModule parsed;
      parsed.key = key;
      parsed.name = object->getProperty(jsonKeys::name).toString();
      if (const auto version = parseGraphVersion(
              object->getProperty(jsonKeys::version).toString());
          version.has_value())
        parsed.version = *version;
      parsed.parentClassKey = object->getProperty("parentClassKey").toString();
      parsed.programs = restoreGraphPrograms(object->getProperty("programs"));
      parsed.description = object->getProperty(jsonKeys::description)
                               .toString()
                               .substring(0, 2'048);
      parsed.category = object->getProperty(jsonKeys::category)
                            .toString()
                            .trim()
                            .substring(0, 192);
      parsed.author = object->getProperty(jsonKeys::author)
                          .toString()
                          .trim()
                          .substring(0, 128);
      if (object->hasProperty(jsonKeys::role))
        parsed.role =
            graphRoleFromString(object->getProperty(jsonKeys::role).toString());
      if (const auto *tags = object->getProperty(jsonKeys::tags).getArray())
        for (const auto &tag : *tags)
          if (const auto cleaned = tag.toString().trim().substring(0, 48);
              cleaned.isNotEmpty())
            parsed.tags.addIfNotAlreadyThere(cleaned, true);
      if (parsed.name.isEmpty())
        continue;
      const auto hasPortMetadata = object->hasProperty(jsonKeys::hasInput) &&
                                   object->hasProperty(jsonKeys::inputKind) &&
                                   object->hasProperty(jsonKeys::hasOutput) &&
                                   object->hasProperty(jsonKeys::outputKind);
      if (hasPortMetadata) {
        parsed.hasInput =
            static_cast<bool>(object->getProperty(jsonKeys::hasInput));
        parsed.inputKind = portKindFromInt(
            static_cast<int>(object->getProperty(jsonKeys::inputKind)));
        parsed.hasOutput =
            static_cast<bool>(object->getProperty(jsonKeys::hasOutput));
        parsed.outputKind = portKindFromInt(
            static_cast<int>(object->getProperty(jsonKeys::outputKind)));
      }
      parsed.factoryModule =
          static_cast<bool>(object->getProperty(jsonKeys::factory));
      parsed.inlineModule =
          static_cast<bool>(object->getProperty(jsonKeys::inline_));
      parsed.inputChannels = static_cast<std::uint8_t>(base::limit(
          0, 8,
          static_cast<int>(object->getProperty(jsonKeys::inputChannels))));
      parsed.outputChannels = static_cast<std::uint8_t>(base::limit(
          0, 8,
          static_cast<int>(object->getProperty(jsonKeys::outputChannels))));
      parsed.locked = static_cast<bool>(object->getProperty(jsonKeys::locked));
      if (const auto *values =
              object->getProperty(jsonKeys::nodes).getArray()) {
        for (const auto &value : *values) {
          const auto *child = value.getDynamicObject();
          if (child == nullptr)
            continue;
          auto restoredTypeKey = child->getProperty(jsonKeys::type).toString();
          if (restoredTypeKey == "graph-audio-input" ||
              restoredTypeKey == "graph-audio-output") {
            const auto channels =
                restoredTypeKey.endsWith("input")
                    ? static_cast<int>(
                          child->getProperty(jsonKeys::outputChannels))
                    : static_cast<int>(
                          child->getProperty(jsonKeys::inputChannels));
            restoredTypeKey = graphAudioBoundaryKey(
                restoredTypeKey.endsWith("input"), channels > 0 ? channels : 2);
          }
          ModuleNode restored{static_cast<NodeId>(static_cast<std::int64_t>(
                                  child->getProperty(jsonKeys::id))),
                              restoredTypeKey,
                              child->getProperty(jsonKeys::title).toString(),
                              {static_cast<float>(static_cast<double>(
                                   child->getProperty(jsonKeys::x))),
                               static_cast<float>(static_cast<double>(
                                   child->getProperty(jsonKeys::y)))}};
          restored.parameters = restoreNativeParameters(
              child->getProperty(jsonKeys::parameters),
              defaultParametersForType(restored.typeKey));
          if (const auto *childType = findType(restored.typeKey))
            restored.ports = childType->ports;
          if (stateSchema >= 21)
            restorePortDefaults(child->getProperty(jsonKeys::portDefaults),
                                restored.ports);
          else
            applyLegacyPortDefaults(restored.typeKey, restored.parameters,
                                    restored.ports);
          if (const auto interfaceValue =
                  child->getProperty(jsonKeys::interface);
              interfaceValue.getDynamicObject() != nullptr)
            restored.interfaceState = restoreModuleInterface(interfaceValue);
          restored.interfaceLayoutOverridden =
              stateSchema >= 18 && static_cast<bool>(child->getProperty(
                                       "interfaceLayoutOverridden"));
          restored.collapsed =
              stateSchema >= 24 &&
              static_cast<bool>(child->getProperty(jsonKeys::collapsed));
          restored.alias = child->getProperty(jsonKeys::alias).toString();
          restored.description = child->getProperty(jsonKeys::description)
                                     .toString()
                                     .substring(0, 2'048);
          restored.inputChannels = static_cast<std::uint8_t>(base::limit(
              0, 8,
              static_cast<int>(child->getProperty(jsonKeys::inputChannels))));
          restored.outputChannels = static_cast<std::uint8_t>(base::limit(
              0, 8,
              static_cast<int>(child->getProperty(jsonKeys::outputChannels))));
          readCardAssets(*child, restored);
          if (restored.id != 0 && restored.typeKey.isNotEmpty())
            parsed.nodes.push_back(std::move(restored));
        }
      }
      if (const auto *values =
              object->getProperty(jsonKeys::connections).getArray())
        for (const auto &value : *values)
          if (const auto *link = value.getDynamicObject())
            parsed.connections.push_back(
                {static_cast<NodeId>(static_cast<std::int64_t>(
                     link->getProperty(jsonKeys::source))),
                 static_cast<NodeId>(static_cast<std::int64_t>(
                     link->getProperty(jsonKeys::destination))),
                 portKindFromInt(
                     static_cast<int>(link->getProperty(jsonKeys::kind))),
                 static_cast<std::uint8_t>(
                     base::limit(0, 8,
                                 static_cast<int>(link->getProperty(
                                     jsonKeys::audioChannels)))),
                 stateSchema >= 16
                     ? static_cast<cydaw::graph::PortId>(
                           static_cast<std::int64_t>(
                               link->getProperty(jsonKeys::sourcePort)))
                     : cydaw::graph::PortId{2},
                 stateSchema >= 16
                     ? static_cast<cydaw::graph::PortId>(
                           static_cast<std::int64_t>(
                               link->getProperty(jsonKeys::destinationPort)))
                     : cydaw::graph::PortId{1}});
      if (const auto *values =
              object->getProperty(jsonKeys::connections).getArray())
        for (std::size_t index = 0;
             index < parsed.connections.size() &&
             index < static_cast<std::size_t>(values->size());
             ++index)
          if (const auto *link =
                  (*values)[static_cast<int>(index)].getDynamicObject();
              link != nullptr && link->hasProperty("mode")) {
            parsed.connections[index].mode = static_cast<std::uint8_t>(
                base::limit(0, 1, static_cast<int>(link->getProperty("mode"))));
            parsed.connections[index].amount = static_cast<float>(
                static_cast<double>(link->getProperty("amount")));
          }
      if (!hasPortMetadata)
        inferModulePorts(parsed);
      if (const auto interfaceValue = object->getProperty(jsonKeys::interface);
          interfaceValue.getDynamicObject() != nullptr)
        parsed.interfaceState = restoreModuleInterface(interfaceValue);
      else
        parsed.interfaceState = createDefaultModuleInterface(parsed);

      if (auto *stored = findModule(key)) {
        *stored = std::move(parsed);
        stored->source = cydaw::base::Value();
        stored->definitionCache.reset();
      } else {
        savedModules_.push_back(std::move(parsed));
      }
      registerModuleType(*findModule(key));
    }
  }

  auto *module = findModule(moduleKey);
  if (module == nullptr)
    return;
  module->source = cydaw::base::Value();
  module->definitionCache.reset();
  const auto previousClassInterface = module->interfaceState;

  std::vector<ModuleNode> children;
  std::vector<Connection> links;
  if (const auto *values = root->getProperty(jsonKeys::nodes).getArray()) {
    children.reserve(static_cast<std::size_t>(values->size()));
    for (const auto &value : *values) {
      const auto *object = value.getDynamicObject();
      if (object == nullptr)
        continue;
      auto typeKey = object->getProperty(jsonKeys::type).toString();
      if (typeKey == "graph-audio-input" || typeKey == "graph-audio-output") {
        const auto channels =
            typeKey.endsWith("input")
                ? static_cast<int>(
                      object->getProperty(jsonKeys::outputChannels))
                : static_cast<int>(
                      object->getProperty(jsonKeys::inputChannels));
        typeKey = graphAudioBoundaryKey(typeKey.endsWith("input"),
                                        channels > 0 ? channels : 2);
      }
      if (findType(typeKey) == nullptr)
        continue;
      ModuleNode child{static_cast<NodeId>(static_cast<std::int64_t>(
                           object->getProperty(jsonKeys::id))),
                       typeKey,
                       object->getProperty(jsonKeys::title).toString(),
                       {static_cast<float>(static_cast<double>(
                            object->getProperty(jsonKeys::x))),
                        static_cast<float>(static_cast<double>(
                            object->getProperty(jsonKeys::y)))}};
      child.parameters =
          restoreNativeParameters(object->getProperty(jsonKeys::parameters),
                                  defaultParametersForType(typeKey));
      if (const auto *childType = findType(typeKey))
        child.ports = childType->ports;
      if (stateSchema >= 21)
        restorePortDefaults(object->getProperty(jsonKeys::portDefaults),
                            child.ports);
      else
        applyLegacyPortDefaults(child.typeKey, child.parameters, child.ports);
      if (const auto interfaceValue = object->getProperty(jsonKeys::interface);
          interfaceValue.getDynamicObject() != nullptr)
        child.interfaceState = restoreModuleInterface(interfaceValue);
      child.interfaceLayoutOverridden =
          stateSchema >= 18 &&
          static_cast<bool>(object->getProperty("interfaceLayoutOverridden"));
      child.collapsed =
          stateSchema >= 24 &&
          static_cast<bool>(object->getProperty(jsonKeys::collapsed));
      child.alias = object->getProperty(jsonKeys::alias).toString();
      child.description = object->getProperty(jsonKeys::description)
                              .toString()
                              .substring(0, 2'048);
      child.inputChannels = static_cast<std::uint8_t>(base::limit(
          0, 8,
          static_cast<int>(object->getProperty(jsonKeys::inputChannels))));
      child.outputChannels = static_cast<std::uint8_t>(base::limit(
          0, 8,
          static_cast<int>(object->getProperty(jsonKeys::outputChannels))));
      readCardAssets(*object, child);
      if (child.id != 0)
        children.push_back(std::move(child));
    }
  }
  if (children.empty())
    return;
  if (const auto *values =
          root->getProperty(jsonKeys::connections).getArray()) {
    links.reserve(static_cast<std::size_t>(values->size()));
    for (const auto &value : *values) {
      if (const auto *object = value.getDynamicObject()) {
        links.push_back(
            {static_cast<NodeId>(static_cast<std::int64_t>(
                 object->getProperty(jsonKeys::source))),
             static_cast<NodeId>(static_cast<std::int64_t>(
                 object->getProperty(jsonKeys::destination))),
             portKindFromInt(
                 static_cast<int>(object->getProperty(jsonKeys::kind))),
             static_cast<std::uint8_t>(
                 base::limit(0, 8,
                             static_cast<int>(object->getProperty(
                                 jsonKeys::audioChannels)))),
             stateSchema >= 16
                 ? static_cast<cydaw::graph::PortId>(static_cast<std::int64_t>(
                       object->getProperty(jsonKeys::sourcePort)))
                 : cydaw::graph::PortId{2},
             stateSchema >= 16
                 ? static_cast<cydaw::graph::PortId>(static_cast<std::int64_t>(
                       object->getProperty(jsonKeys::destinationPort)))
                 : cydaw::graph::PortId{1}});
        if (object->hasProperty("mode")) {
          links.back().mode = static_cast<std::uint8_t>(
              base::limit(0, 1, static_cast<int>(object->getProperty("mode"))));
          links.back().amount = static_cast<float>(
              static_cast<double>(object->getProperty("amount")));
        }
      }
    }
  }

  applyingModuleGraphState_ = true;
  const auto editedName = root->getProperty(jsonKeys::graphName)
                              .toString()
                              .trim()
                              .substring(0, 128);
  if (editedName.isNotEmpty())
    module->name = editedName;
  if (const auto editedVersion = parseGraphVersion(
          root->getProperty(jsonKeys::definitionVersion).toString());
      editedVersion.has_value())
    module->version = *editedVersion;
  module->programs = restoreGraphPrograms(root->getProperty("programs"));
  module->description = root->getProperty(jsonKeys::graphDescription)
                            .toString()
                            .substring(0, 2'048);
  module->category = root->getProperty(jsonKeys::graphCategory)
                         .toString()
                         .trim()
                         .substring(0, 192);
  module->author = root->getProperty(jsonKeys::graphAuthor)
                       .toString()
                       .trim()
                       .substring(0, 128);
  if (root->hasProperty(jsonKeys::graphRole))
    module->role =
        graphRoleFromString(root->getProperty(jsonKeys::graphRole).toString());
  module->tags.clear();
  if (const auto *tags = root->getProperty(jsonKeys::graphTags).getArray())
    for (const auto &tag : *tags)
      if (const auto cleaned = tag.toString().trim().substring(0, 48);
          cleaned.isNotEmpty() && module->tags.size() < 12)
        module->tags.addIfNotAlreadyThere(cleaned, true);
  module->nodes = std::move(children);
  module->connections = std::move(links);
  if (const auto presentation = root->getProperty(jsonKeys::presentation);
      presentation.getDynamicObject() != nullptr)
    module->interfaceState = restoreModuleInterface(presentation);
  else
    module->interfaceState = createDefaultModuleInterface(*module);
  inferModulePorts(*module);
  registerModuleType(*module);
  const auto reconcileInstance = [this, module, &previousClassInterface](
                                     const NodeId instanceId,
                                     const ModuleInterfaceState &previous,
                                     const bool preserveLayout) {
    auto updated = createGraphInstanceInterface(instanceId, *module);
    for (auto &parameter : updated.parameters) {
      if (const auto existing =
              std::ranges::find_if(previous.parameters,
                                   [&parameter](const auto &candidate) {
                                     return candidate.key == parameter.key;
                                   });
          existing != previous.parameters.end()) {
        const auto classKey =
            graphInstanceClassParameterKey(instanceId, parameter.key);
        const auto previousClass =
            std::ranges::find_if(previousClassInterface.parameters,
                                 [&classKey](const auto &candidate) {
                                   return candidate.key == classKey;
                                 });
        const auto wasOverridden =
            previousClass == previousClassInterface.parameters.end() ||
            std::abs(existing->value - previousClass->value) > 1.0e-9;
        if (wasOverridden)
          parameter.value = base::limit(parameter.minimum, parameter.maximum,
                                        existing->value);
        if (preserveLayout) {
          parameter.label = existing->label;
          parameter.showInDevice = existing->showInDevice;
          parameter.publishToHost = existing->publishToHost;
          parameter.automatable = existing->automatable;
          parameter.modulatable = existing->modulatable;
          parameter.description = existing->description;
        }
      }
    }
    if (preserveLayout) {
      updated.nextControlId =
          std::max(updated.nextControlId, previous.nextControlId);
      updated.snapToGrid = previous.snapToGrid;
      updated.gridColumns = previous.gridColumns;
      updated.gridRows = previous.gridRows;
      updated.controls = previous.controls;
      updated.deviceControls = previous.deviceControls;
      updated.designed = previous.designed;
      updated.windowWidth = previous.windowWidth;
      updated.windowHeight = previous.windowHeight;
      updated.windowDesign = previous.windowDesign;
      updated.deviceDesign = previous.deviceDesign;
      updated.deviceDesigned = previous.deviceDesigned;
      updated.deviceWidth = previous.deviceWidth;
      updated.deviceHeight = previous.deviceHeight;
      const auto removeStaleBindings = [&updated](auto &controls) {
        std::erase_if(controls, [&updated](const auto &control) {
          const auto parameterControl =
              control.kind == ModuleControlKind::knob ||
              control.kind == ModuleControlKind::fader ||
              control.kind == ModuleControlKind::toggle ||
              control.kind == ModuleControlKind::selector ||
              control.kind == ModuleControlKind::xyPad ||
              control.kind == ModuleControlKind::envelope ||
              control.kind == ModuleControlKind::valueDisplay ||
              control.kind == ModuleControlKind::filterResponse ||
              control.kind == ModuleControlKind::dynamicsResponse;
          if (!parameterControl)
            return false;
          const auto bindingExists =
              [&updated](const cydaw::base::Text &binding) {
                return std::ranges::any_of(updated.parameters,
                                           [&binding](const auto &parameter) {
                                             return parameter.key == binding;
                                           });
              };
          if (!bindingExists(control.parameterBinding))
            return true;
          if (control.kind == ModuleControlKind::xyPad ||
              control.kind == ModuleControlKind::filterResponse ||
              control.kind == ModuleControlKind::dynamicsResponse)
            return control.secondaryParameterBinding ==
                       control.parameterBinding ||
                   !bindingExists(control.secondaryParameterBinding);
          if (control.kind != ModuleControlKind::envelope)
            return false;
          const std::set<cydaw::base::Text> bindings{
              control.parameterBinding, control.secondaryParameterBinding,
              control.tertiaryParameterBinding,
              control.quaternaryParameterBinding};
          return bindings.size() != 4 ||
                 !bindingExists(control.secondaryParameterBinding) ||
                 !bindingExists(control.tertiaryParameterBinding) ||
                 !bindingExists(control.quaternaryParameterBinding);
        });
      };
      removeStaleBindings(updated.controls);
      removeStaleBindings(updated.deviceControls);
    }
    return updated;
  };
  if (const auto *type = findType(moduleKey)) {
    for (auto &node : nodes_) {
      if (node.typeKey != moduleKey)
        continue;
      node.serialised = cydaw::base::Value();
      node.title = module->name;
      node.hasInput = type->hasInput;
      node.inputKind = type->inputKind;
      node.hasOutput = type->hasOutput;
      node.outputKind = type->outputKind;
      node.composite = true;
      node.inputChannels = module->inputChannels;
      node.outputChannels = module->outputChannels;
      auto ports = type->ports;
      for (auto &port : ports)
        if (const auto previous =
                std::ranges::find(node.ports, port.id, &NodePort::id);
            previous != node.ports.end()) {
          port.defaultValue = previous->defaultValue;
          port.exposure = previous->exposure;
          if (previous->category.isNotEmpty())
            port.category = previous->category;
          if (port.name.isEmpty())
            port.name = previous->name;
        }
      node.ports = std::move(ports);
      node.interfaceState = reconcileInstance(node.id, node.interfaceState,
                                              node.interfaceLayoutOverridden);
    }
  }
  for (auto &owner : savedModules_)
    if (owner.key != moduleKey)
      for (auto &child : owner.nodes)
        if (child.typeKey == moduleKey) {
          if (const auto *type = findType(moduleKey)) {
            auto ports = type->ports;
            for (auto &port : ports)
              if (const auto previous =
                      std::ranges::find(child.ports, port.id, &NodePort::id);
                  previous != child.ports.end()) {
                port.defaultValue = previous->defaultValue;
                port.exposure = previous->exposure;
                if (previous->category.isNotEmpty())
                  port.category = previous->category;
                if (port.name.isEmpty())
                  port.name = previous->name;
              }
            child.ports = std::move(ports);
          }
          child.interfaceState = reconcileInstance(
              child.id, child.interfaceState, child.interfaceLayoutOverridden);
        }
  synchroniseGraphInterface();
  ;
  applyingModuleGraphState_ = false;
  setStatus(tr("nodes.status.subgraphUpdated", "name", module->name));
  ;
  if (structureChanged)
    notifyLayoutChanged();
  else
    queueParameterNotification();
}
void GraphDocument::arrangeNodesBySignalFlow(const bool resetView) {
  if (nodes_.empty())
    return;

  // Routing points describe the current manual layout. Once every node is
  // automatically rearranged they would no longer describe useful bends.
  for (auto &connection : connections_)
    connection.routingPoints.clear();
  selectedConnection_ = -1;

  std::unordered_map<NodeId, int> ranks;
  ranks.reserve(nodes_.size());
  for (const auto &node : nodes_)
    ranks.emplace(node.id, 0);
  for (std::size_t pass = 0; pass < nodes_.size(); ++pass) {
    auto changed = false;
    for (const auto &connection : connections_) {
      const auto source = ranks.find(connection.source);
      const auto destination = ranks.find(connection.destination);
      if (source == ranks.end() || destination == ranks.end())
        continue;
      const auto next = source->second + 1;
      if (next > destination->second) {
        destination->second = next;
        changed = true;
      }
    }
    if (!changed)
      break;
  }

  auto maximumRank = 0;
  for (const auto &[id, rank] : ranks) {
    base::ignoreUnused(id);
    maximumRank = std::max(maximumRank, rank);
  }
  std::vector<std::vector<Node *>> columns(
      static_cast<std::size_t>(maximumRank + 1));
  invalidateSerialisedNodes();
  for (auto &node : nodes_)
    columns[static_cast<std::size_t>(ranks[node.id])].push_back(&node);

  for (auto &column : columns)
    std::stable_sort(column.begin(), column.end(),
                     [](const Node *first, const Node *second) {
                       if (first->position.y != second->position.y)
                         return first->position.y < second->position.y;
                       return first->id < second->id;
                     });

  // Work in unzoomed canvas pixels so heterogeneous node interfaces never
  // overlap. If the patch is wider or taller than the viewport, its graph
  // coordinates intentionally extend past 0..1 and remain reachable through
  // pan/minimap navigation instead of being shrunk into thumbnails.
  const auto canvas = layoutSpace_;
  constexpr auto horizontalGap = 88.0f;
  constexpr auto verticalGap = 48.0f;
  constexpr auto canvasMargin = 38.0f;
  std::vector<float> columnWidths(columns.size(), nodeWidth);
  std::vector<float> columnHeights(columns.size(), 0.0f);
  auto totalWidth = 0.0f;
  for (std::size_t rank = 0; rank < columns.size(); ++rank) {
    auto &column = columns[rank];
    auto height = 0.0f;
    auto width = 0.0f;
    for (const auto *node : column) {
      const auto size = nodeVisualSize(*node);
      width = std::max(width, size.x);
      height += size.y;
    }
    if (column.size() > 1)
      height += verticalGap * static_cast<float>(column.size() - 1);
    columnWidths[rank] = std::max(nodeWidth, width);
    columnHeights[rank] = height;
    totalWidth += columnWidths[rank];
  }
  if (columns.size() > 1)
    totalWidth += horizontalGap * static_cast<float>(columns.size() - 1);

  auto x =
      canvas.getX() + (totalWidth + canvasMargin * 2.0f <= canvas.getWidth()
                           ? (canvas.getWidth() - totalWidth) * 0.5f
                           : canvasMargin);
  for (std::size_t rank = 0; rank < columns.size(); ++rank) {
    auto &column = columns[rank];
    auto y = canvas.getY() +
             (columnHeights[rank] + canvasMargin * 2.0f <= canvas.getHeight()
                  ? (canvas.getHeight() - columnHeights[rank]) * 0.5f
                  : canvasMargin);
    for (auto *node : column) {
      const auto size = nodeVisualSize(*node);
      const auto target =
          base::Point<float>{x + (columnWidths[rank] - size.x) * 0.5f, y};
      node->position =
          graphPosition({(target.x - canvas.getX()) /
                             std::max(1.0f, canvas.getWidth() - size.x),
                         (target.y - canvas.getY()) /
                             std::max(1.0f, canvas.getHeight() - size.y)});
      y += size.y + verticalGap;
    }
    x += columnWidths[rank] + horizontalGap;
  }

  if (resetView) {
    ;
    // Fit the view to the layout just produced. Resetting to 100 % left
    // anything wider than the canvas outside it. With a selection the
    // caller asked to rearrange those nodes, so the view stays put.
    if (selectedNodes_.empty())
      ;
  };
  ;
}
void GraphDocument::resetNodeLayout() {
  arrangeNodesBySignalFlow(true);
  setStatus(tr("nodes.status.layoutReset"));
  notifyLayoutChanged();
}
void GraphDocument::dispatchParameterNotification() {
  if (parametersChangedHandler_)
    parametersChangedHandler_();
  else if (layoutChangedHandler_)
    layoutChangedHandler_();
}
void GraphDocument::markEditHistoryBaseline(
    const cydaw::base::Value *knownState) {
  pendingHistoryState_ = knownState != nullptr ? *knownState : serialise();
  currentHistoryState_ = cydaw::base::Value();
}
const cydaw::base::Value &GraphDocument::editHistoryBaseline() {
  if (!pendingHistoryState_.isVoid()) {
    currentHistoryState_ = std::move(pendingHistoryState_);
    pendingHistoryState_ = cydaw::base::Value();
  }
  return currentHistoryState_;
}
void GraphDocument::resetEditHistory(const cydaw::base::Value *knownState) {
  undoHistory_.clear();
  redoHistory_.clear();
  markEditHistoryBaseline(knownState);
  lastHistoryWasParameter_ = false;
  lastParameterHistoryTimeMs_ = 0;
  ;
}
void GraphDocument::commitEditHistory(const bool parameterChange) {
  if (restoringHistory_)
    return;

  auto nextState = serialise();
  if (editHistoryBaseline().isVoid()) {
    currentHistoryState_ = std::move(nextState);
    ;
    return;
  }
  if (jsonValuesMatch(nextState, currentHistoryState_))
    return;

  const auto now = cydaw::base::Time::currentTimeMillis();
  const auto coalesceParameterChange = parameterChange &&
                                       lastHistoryWasParameter_ &&
                                       now - lastParameterHistoryTimeMs_ < 650;
  if (!coalesceParameterChange) {
    undoHistory_.push_back(editHistoryBaseline());
    ++historySteps_;
    if (undoHistory_.size() > maxEditHistoryStates)
      undoHistory_.erase(undoHistory_.begin());
  }

  currentHistoryState_ = std::move(nextState);
  redoHistory_.clear();
  lastHistoryWasParameter_ = parameterChange;
  lastParameterHistoryTimeMs_ = parameterChange ? now : 0;
  ;
}
void GraphDocument::restoreEditHistoryState(const cydaw::base::Value &state) {
  if (state.isVoid())
    return;
  const cydaw::base::ScopedValueSetter<bool> restoring(restoringHistory_, true);
  // Handed over as it stands. restore() reads the description through const
  // pointers — the compiler refuses an attempt to write to it — and it keeps
  // the objects it is given so that unchanged classes are recognised next
  // time. A fresh copy per undo defeated that recognition and re-parsed all
  // thirty-two of them.
  restore(state);
  currentHistoryState_ = state;
  pendingHistoryState_ = cydaw::base::Value();
  lastHistoryWasParameter_ = false;
  lastParameterHistoryTimeMs_ = 0;
  ;
  if (layoutChangedHandler_)
    layoutChangedHandler_();
}
void GraphDocument::undoGraphEdit() {
  flushParameterNotification();
  if (editingLocked_ || undoHistory_.empty())
    return;
  redoHistory_.push_back(editHistoryBaseline());
  const auto state = undoHistory_.back();
  undoHistory_.pop_back();
  restoreEditHistoryState(state);
  setStatus(tr("nodes.status.undo"));
}
void GraphDocument::redoGraphEdit() {
  flushParameterNotification();
  if (editingLocked_ || redoHistory_.empty())
    return;
  undoHistory_.push_back(editHistoryBaseline());
  if (undoHistory_.size() > maxEditHistoryStates)
    undoHistory_.erase(undoHistory_.begin());
  const auto state = redoHistory_.back();
  redoHistory_.pop_back();
  restoreEditHistoryState(state);
  setStatus(tr("nodes.status.redo"));
}
void GraphDocument::commitQueuedParameterHistory() {
  if (!parameterHistoryPending_)
    return;
  parameterHistoryPending_ = false;
  activeFactoryPresetKey_.clear();
  ;
  commitEditHistory(true);
}
cydaw::base::Text GraphDocument::graphAudioBoundaryKey(const bool input,
                                                       const int channels) {
  return "graph-audio-" + cydaw::base::Text(input ? "input-" : "output-") +
         cydaw::base::Text(base::limit(1, 8, channels));
}
bool GraphDocument::isGraphAudioBoundaryKey(const cydaw::base::Text &key,
                                            bool *input) {
  constexpr auto inputPrefix = "graph-audio-input-";
  constexpr auto outputPrefix = "graph-audio-output-";
  const auto isInput = key.startsWith(inputPrefix);
  const auto isOutput = key.startsWith(outputPrefix);
  if (!isInput && !isOutput)
    return false;
  const auto suffix =
      key.substring(isInput ? cydaw::base::Text(inputPrefix).length()
                            : cydaw::base::Text(outputPrefix).length());
  const auto channels = suffix.getIntValue();
  if (channels < 1 || channels > 8 || suffix != cydaw::base::Text(channels))
    return false;
  if (input != nullptr)
    *input = isInput;
  return true;
}
bool GraphDocument::isBoundaryNode(const Node &node) const {
  return isGraphAudioBoundaryKey(node.typeKey) ||
         (node.typeKey.startsWith("graph-") &&
          (node.typeKey.endsWith("-input") ||
           node.typeKey.endsWith("-output"))) ||
         node.typeKey == "audio-clips" || node.typeKey == "midi-input" ||
         node.typeKey == "master" || node.typeKey == "midi-output";
}
cydaw::base::Text GraphDocument::displayTitle(const Node &node) const {
  return node.alias.trim().isNotEmpty() ? node.alias.trim() : node.title;
}
std::uint8_t GraphDocument::audioInputChannels(const Node &node) const {
  if (node.inputKind != PortKind::audio)
    return 1;
  if (node.inputChannels != 0)
    return node.inputChannels;
  if (const auto *module = findModule(node.typeKey);
      module != nullptr && module->inputChannels != 0)
    return module->inputChannels;
  if (const auto *type = findType(node.typeKey);
      type != nullptr && type->inputChannels != 0)
    return type->inputChannels;
  return 2;
}
std::uint8_t GraphDocument::audioOutputChannels(const Node &node) const {
  if (node.outputKind != PortKind::audio)
    return 1;
  if (node.outputChannels != 0)
    return node.outputChannels;
  if (const auto *module = findModule(node.typeKey);
      module != nullptr && module->outputChannels != 0)
    return module->outputChannels;
  if (const auto *type = findType(node.typeKey);
      type != nullptr && type->outputChannels != 0)
    return type->outputChannels;
  return 2;
}
cydaw::graph::PluginIdentity GraphDocument::defaultVst3ExportIdentity() const {
  std::uint32_t hash = 2'166'136'261u;
  for (const auto value : graphReference_.id) {
    hash ^= static_cast<std::uint8_t>(value);
    hash *= 16'777'619u;
  }
  const auto pluginCode =
      "G" + cydaw::base::Text::toHexString(static_cast<int>(hash & 0xfffu))
                .paddedLeft('0', 3)
                .toUpperCase();
  const auto productName =
      graphName_.isNotEmpty()
          ? graphName_
          : (contextName_.isNotEmpty() ? contextName_
                                       : cydaw::base::Text("CyDAW Graph"));
  return {productName.toStdString(), "CyDAW User", "CyDw",
          pluginCode.toStdString(), graphReference_.version};
}
cydaw::graph::PluginExportResult GraphDocument::planVst3Export(
    const cydaw::graph::PluginIdentity &identity) const {
  cydaw::graph::GraphLibrary library;
  auto definitions = graphDefinitions();
  // A value input wired straight to published settings becomes those host
  // parameters in VST3. The CyDAW graph retains its typed external socket.
  const auto root = std::ranges::find(
      definitions, graphReference_, &cydaw::graph::GraphDefinition::reference);
  if (root != definitions.end()) {
    const auto editor = editorNodes();
    std::set<cydaw::graph::PortId> lowered;
    for (const auto &node : editor) {
      if (node.typeKey != "graph-value-input")
        continue;
      int connected = 0;
      bool mapped = true;
      for (const auto &c : connections_)
        if (c.source == node.id) {
          ++connected;
          if (c.mode != 0) {
            mapped = false;
            break;
          }
          const auto target =
              std::ranges::find(editor, c.destination, &EditorNode::id);
          if (target == editor.end()) {
            mapped = false;
            break;
          }
          const auto p =
              std::ranges::find_if(target->parameters, [&](const auto &p) {
                return p.inputPortId == c.destinationPort && p.publishToHost;
              });
          if (p == target->parameters.end()) {
            mapped = false;
            break;
          }
        }
      if (connected == 1 && mapped)
        lowered.insert(node.id);
    }
    for (auto &surface : root->interfaces)
      std::erase_if(surface.controls, [&](const auto &c) {
        return c.portBinding && lowered.contains(*c.portBinding);
      });
    std::erase_if(root->ports,
                  [&](const auto &p) { return lowered.contains(p.id); });
    std::erase_if(root->connections, [&](const auto &c) {
      return c.source.nodeId == cydaw::graph::boundaryNodeId &&
             lowered.contains(c.source.portId);
    });
  }
  const auto installed = library.installBatch(std::move(definitions));
  if (!installed.ok()) {
    cydaw::graph::PluginExportResult result;
    for (const auto &issue : installed.issues)
      if (issue.severity == cydaw::graph::ValidationSeverity::error)
        result.issues.push_back(
            {cydaw::graph::PluginExportIssueCode::invalidGraph,
             issue.message,
             issue.portId,
             {}});
    if (result.issues.empty())
      result.issues.push_back(
          {cydaw::graph::PluginExportIssueCode::invalidGraph,
           "The Graph library could not be installed for VST3 export",
           {},
           {}});
    return result;
  }
  return cydaw::graph::planVst3Export(library, graphReference_, identity);
}
void GraphDocument::setSelectedAudioChannels(const int channels) {
  if (editingLocked_ || selectedNodes_.size() != 1)
    return;
  auto *node = findNode(*selectedNodes_.begin());
  if (node == nullptr)
    return;
  const auto selectedIsBoundary = isGraphAudioBoundaryKey(node->typeKey);
  if (!selectedIsBoundary &&
      !primitiveSupportsVariableAudioChannels(node->typeKey))
    return;
  const auto safe = static_cast<std::uint8_t>(base::limit(1, 8, channels));

  // Exact audio formats propagate through a connected polymorphic chain.
  // A fixed reusable Graph instance forms a hard boundary: changing through
  // it is rejected unless it already exposes the requested exact format.
  std::vector<NodeId> affected{node->id};
  for (auto workIndex = std::size_t{}; workIndex < affected.size();
       ++workIndex) {
    const auto currentId = affected[workIndex];
    for (const auto &link : connections_) {
      if (link.kind != PortKind::audio ||
          (link.source != currentId && link.destination != currentId))
        continue;
      const auto otherId =
          link.source == currentId ? link.destination : link.source;
      if (std::ranges::find(affected, otherId) != affected.end())
        continue;
      const auto *other = findNode(otherId);
      if (other == nullptr)
        continue;
      const auto retypable =
          isGraphAudioBoundaryKey(other->typeKey) ||
          primitiveSupportsVariableAudioChannels(other->typeKey);
      const auto otherPortId =
          link.source == otherId ? link.sourcePort : link.destinationPort;
      const auto *otherPort = findPort(*other, otherPortId);
      const auto otherChannels = otherPort != nullptr ? otherPort->channels
                                 : link.source == otherId
                                     ? audioOutputChannels(*other)
                                     : audioInputChannels(*other);
      if (!retypable) {
        if (otherChannels != 0 && otherChannels != safe) {
          setStatus(tr("nodes.status.channelMismatch"), true);
          return;
        }
        continue;
      }
      affected.push_back(otherId);
    }
  }

  const auto updateInterfacePorts = [safe](ModuleInterfaceState &state) {
    const auto update = [safe](auto &controls) {
      for (auto &control : controls)
        if ((control.kind == ModuleControlKind::inputPort ||
             control.kind == ModuleControlKind::outputPort) &&
            control.portKind == 0)
          control.portChannels = safe;
    };
    update(state.controls);
    update(state.deviceControls);
  };
  for (const auto affectedId : affected) {
    auto *current = findNode(affectedId);
    if (current == nullptr)
      continue;
    bool inputBoundary{};
    if (isGraphAudioBoundaryKey(current->typeKey, &inputBoundary)) {
      const auto nextKey = graphAudioBoundaryKey(inputBoundary, safe);
      const auto *nextType = findType(nextKey);
      if (nextType == nullptr)
        continue;
      current->typeKey = nextKey;
      current->title = nextType->title;
      current->colour = nextType->colour;
      current->hasInput = nextType->hasInput;
      current->inputKind = nextType->inputKind;
      current->hasOutput = nextType->hasOutput;
      current->outputKind = nextType->outputKind;
      current->inputChannels = nextType->inputChannels;
      current->outputChannels = nextType->outputChannels;
      current->ports = nextType->ports;
    } else {
      if (current->inputKind == PortKind::audio)
        current->inputChannels = safe;
      if (current->outputKind == PortKind::audio)
        current->outputChannels = safe;
      for (auto &port : current->ports)
        if (port.type == cydaw::graph::PortType::audio)
          port.channels = safe;
    }
    updateInterfacePorts(current->interfaceState);
  }
  for (auto &link : connections_)
    if (link.kind == PortKind::audio &&
        (std::ranges::find(affected, link.source) != affected.end() ||
         std::ranges::find(affected, link.destination) != affected.end()))
      link.audioChannels = safe;
  synchroniseGraphInterface();
  refreshGraphAssetValidation();
  ;
  notifyLayoutChanged();
  ;
}
bool GraphDocument::setGraphMemberMetadata(
    const cydaw::base::Text &key, const cydaw::base::Text &displayName,
    const cydaw::base::Text &description) {
  if (editingLocked_)
    return false;
  const auto safeName = displayName.trim().substring(0, 96);
  const auto safeDescription = description.trim().substring(0, 1'024);
  if (safeName.isEmpty())
    return false;

  if (const auto parameter = std::ranges::find_if(
          graphInterfaceState_.parameters,
          [&key](const auto &candidate) { return candidate.key == key; });
      parameter != graphInterfaceState_.parameters.end()) {
    parameter->label = safeName;
    parameter->description = safeDescription;
    if (key.startsWith("node.")) {
      const auto separator = key.indexOfChar(5, '.');
      if (separator > 5) {
        const auto nodeId =
            static_cast<NodeId>(key.substring(5, separator).getLargeIntValue());
        if (auto *node = findNode(nodeId); node != nullptr)
          if (auto *presentation = editablePresentationForNode(*node))
            if (const auto local =
                    std::ranges::find_if(presentation->parameters,
                                         [&key](const auto &candidate) {
                                           return candidate.key == key;
                                         });
                local != presentation->parameters.end()) {
              local->label = safeName;
              local->description = safeDescription;
            }
      }
    }
    rebuildAutomaticDeviceControls(graphInterfaceState_);
    ;
    ;
  } else {
    if (!key.startsWith("node."))
      return false;
    const auto nodeId =
        static_cast<NodeId>(key.substring(5).getLargeIntValue());
    auto *node = findNode(nodeId);
    if (node == nullptr || !isBoundaryNode(*node))
      return false;
    node->alias = safeName;
    node->description = safeDescription;
    synchroniseGraphInterface();
    ;
  }

  ;
  ;
  refreshGraphAssetValidation();
  notifyLayoutChanged();
  ;
  setStatus(tr("nodes.status.memberUpdated", "name", safeName));
  return true;
}
bool GraphDocument::setGraphMemberType(const cydaw::base::Text &key,
                                       const PortKind kind,
                                       const int audioChannels) {
  if (editingLocked_ || !key.startsWith("node."))
    return false;
  const auto nodeId = static_cast<NodeId>(key.substring(5).getLargeIntValue());
  auto *node = findNode(nodeId);
  bool inputBoundary = false;
  const auto explicitAudio =
      node != nullptr && isGraphAudioBoundaryKey(node->typeKey, &inputBoundary);
  if (node == nullptr ||
      (!explicitAudio && !(node->typeKey.startsWith("graph-") &&
                           (node->typeKey.endsWith("-input") ||
                            node->typeKey.endsWith("-output")))))
    return false;
  if (!explicitAudio)
    inputBoundary = node->typeKey.endsWith("-input");

  const auto safeChannels = base::limit(1, 8, audioChannels);
  cydaw::base::Text targetKey;
  if (kind == PortKind::audio)
    targetKey = graphAudioBoundaryKey(inputBoundary, safeChannels);
  else if (kind == PortKind::midi)
    targetKey =
        "graph-midi-" + cydaw::base::Text(inputBoundary ? "input" : "output");
  else if (kind == PortKind::control)
    targetKey =
        "graph-value-" + cydaw::base::Text(inputBoundary ? "input" : "output");
  else
    targetKey = "graph-transport-" +
                cydaw::base::Text(inputBoundary ? "input" : "output");
  if (node->typeKey == targetKey)
    return true;

  // Audio width changes use the existing exact-format propagation. It keeps
  // polymorphic chains connected and rejects fixed incompatible devices.
  if (explicitAudio && kind == PortKind::audio) {
    selectedNodes_.clear();
    selectedNodes_.insert(nodeId);
    setSelectedAudioChannels(safeChannels);
    return node->typeKey == targetKey;
  }

  const auto *targetType = findType(targetKey);
  if (targetType == nullptr)
    return false;
  const auto targetPort = std::ranges::find_if(
      targetType->ports, [inputBoundary](const NodePort &port) {
        return port.input != inputBoundary;
      });
  if (targetPort == targetType->ports.end())
    return false;

  std::vector<Connection> compatibleConnections;
  compatibleConnections.reserve(connections_.size());
  auto disconnected = 0;
  for (auto connection : connections_) {
    const auto touchesCorrectSide = inputBoundary
                                        ? connection.source == nodeId
                                        : connection.destination == nodeId;
    const auto touchesWrongSide = inputBoundary
                                      ? connection.destination == nodeId
                                      : connection.source == nodeId;
    if (!touchesCorrectSide && !touchesWrongSide) {
      compatibleConnections.push_back(std::move(connection));
      continue;
    }
    if (touchesWrongSide) {
      ++disconnected;
      continue;
    }
    const auto *otherNode =
        findNode(inputBoundary ? connection.destination : connection.source);
    const auto *otherPort =
        otherNode != nullptr
            ? findPort(*otherNode, inputBoundary ? connection.destinationPort
                                                 : connection.sourcePort)
            : nullptr;
    const auto compatibleKind =
        otherPort != nullptr &&
        enginePortKind(otherPort->type) == enginePortKind(targetPort->type);
    const auto compatibleChannels =
        compatibleKind &&
        (targetPort->type != cydaw::graph::PortType::audio ||
         otherPort->channels == 0 || targetPort->channels == 0 ||
         otherPort->channels == targetPort->channels);
    if (!compatibleChannels) {
      ++disconnected;
      continue;
    }
    connection.kind = enginePortKind(targetPort->type);
    connection.audioChannels =
        connection.kind == PortKind::audio ? targetPort->channels : 0;
    if (inputBoundary)
      connection.sourcePort = targetPort->id;
    else
      connection.destinationPort = targetPort->id;
    compatibleConnections.push_back(std::move(connection));
  }

  Node replacement{nodeId,
                   targetType->key,
                   targetType->title,
                   targetType->colour,
                   node->position,
                   targetType->hasInput,
                   targetType->inputKind,
                   targetType->hasOutput,
                   targetType->outputKind,
                   targetType->protectedNode,
                   targetType->composite};
  replacement.inputChannels = targetType->inputChannels;
  replacement.outputChannels = targetType->outputChannels;
  replacement.alias = node->alias;
  replacement.description = node->description;
  replacement.collapsed = node->collapsed;
  replacement.ports = targetType->ports;
  replacement.parameters = defaultParametersForType(targetType->key);
  replacement.interfaceState = createNodeInterface(replacement);
  *node = std::move(replacement);
  connections_ = std::move(compatibleConnections);
  selectedNodes_.clear();
  selectedNodes_.insert(nodeId);
  selectedPublishedParameterKey_.clear();
  synchroniseGraphInterface();
  ;
  ;
  ;
  refreshGraphAssetValidation();
  notifyLayoutChanged();
  ;
  ;
  setStatus(disconnected > 0 ? tr("nodes.status.memberRetypedDisconnected",
                                  "count", cydaw::base::Text(disconnected))
                             : tr("nodes.status.memberRetyped"));
  return true;
}
bool GraphDocument::setPublishedParameterExposure(
    const cydaw::base::Text &binding, const bool showInDevice,
    const bool publishToHost) {
  return updateGraphMemberParameterVisibility(binding, showInDevice,
                                              publishToHost);
}
bool GraphDocument::updateGraphMemberParameterVisibility(
    const cydaw::base::Text &key, const std::optional<bool> showInDevice,
    const std::optional<bool> publishToHost) {
  if (editingLocked_ ||
      (!showInDevice.has_value() && !publishToHost.has_value()))
    return false;
  const auto parameter = std::ranges::find_if(
      graphInterfaceState_.parameters,
      [&key](const auto &candidate) { return candidate.key == key; });
  if (parameter == graphInterfaceState_.parameters.end())
    return false;

  auto changed = false;
  if (showInDevice.has_value() && parameter->showInDevice != *showInDevice) {
    parameter->showInDevice = *showInDevice;
    changed = true;
  }
  if (publishToHost.has_value() && parameter->publishToHost != *publishToHost) {
    parameter->publishToHost = *publishToHost;
    changed = true;
  }
  if (!changed)
    return false;

  if (key.startsWith("node.")) {
    const auto separator = key.indexOfChar(5, '.');
    if (separator > 5) {
      const auto nodeId =
          static_cast<NodeId>(key.substring(5, separator).getLargeIntValue());
      if (auto *node = findNode(nodeId); node != nullptr)
        if (auto *presentation = editablePresentationForNode(*node))
          if (const auto local =
                  std::ranges::find_if(presentation->parameters,
                                       [&key](const auto &candidate) {
                                         return candidate.key == key;
                                       });
              local != presentation->parameters.end()) {
            if (showInDevice.has_value())
              local->showInDevice = *showInDevice;
            if (publishToHost.has_value())
              local->publishToHost = *publishToHost;
          }
    }
  }

  rebuildAutomaticDeviceControls(graphInterfaceState_);
  ;
  refreshGraphAssetValidation();
  ;
  ;
  notifyLayoutChanged();
  ;
  return true;
}
void GraphDocument::refreshGraphAssetValidation() {
  const auto result = validateGraphAsset();
  graphAssetValid_ = result.ok();
  graphAssetValidationMessage_.clear();
  invalidGraphNodes_.clear();
  invalidGraphPorts_.clear();
  graphNodeValidationMessages_.clear();
  if (!graphAssetValid_ && !result.issues.empty())
    graphAssetValidationMessage_ = result.issues.front().message;
  for (const auto &issue : result.issues) {
    if (issue.severity != cydaw::graph::ValidationSeverity::error)
      continue;
    auto nodeId = issue.nodeId.value_or(cydaw::graph::boundaryNodeId);
    if (nodeId == cydaw::graph::boundaryNodeId && issue.portId.has_value()) {
      const auto boundary =
          std::ranges::find_if(nodes_, [this, &issue](const auto &node) {
            return node.id == *issue.portId && isBoundaryNode(node);
          });
      if (boundary != nodes_.end())
        nodeId = boundary->id;
    }
    if (nodeId == cydaw::graph::boundaryNodeId)
      continue;
    invalidGraphNodes_.insert(nodeId);
    graphNodeValidationMessages_.try_emplace(nodeId,
                                             cydaw::base::Text(issue.message));
    if (issue.portId.has_value())
      invalidGraphPorts_.insert({nodeId, *issue.portId});
  }
  if (!invalidGraphNodes_.contains(lastFocusedValidationNode_))
    lastFocusedValidationNode_ = 0;
  ;
}
const GraphDocument::NodePort *
GraphDocument::findPort(const Node &node,
                        const cydaw::graph::PortId portId) const {
  const auto found = std::find_if(
      node.ports.begin(), node.ports.end(),
      [portId](const NodePort &port) { return port.id == portId; });
  return found != node.ports.end() ? &*found : nullptr;
}
cydaw::base::Value
GraphDocument::serialisePortDefaults(const std::vector<NodePort> &ports) {
  cydaw::base::Array<cydaw::base::Value> values;
  for (const auto &port : ports) {
    auto *value = new cydaw::base::Object;
    value->setProperty("id", static_cast<std::int64_t>(port.id));
    value->setProperty("value", port.defaultValue);
    value->setProperty("exposure", port.exposure);
    value->setProperty("name", port.name);
    value->setProperty("category", port.category);
    values.add(cydaw::base::Value(value));
  }
  return cydaw::base::Value(std::move(values));
}
void GraphDocument::restorePortDefaults(const cydaw::base::Value &state,
                                        std::vector<NodePort> &ports) {
  const auto *values = state.getArray();
  if (!values)
    return;
  for (const auto &value : *values) {
    const auto id = static_cast<cydaw::graph::PortId>(
        static_cast<std::int64_t>(value.getProperty("id", 0)));
    auto port = std::ranges::find(ports, id, &NodePort::id);
    if (port == ports.end())
      continue;
    const auto number =
        static_cast<double>(value.getProperty("value", port->defaultValue));
    if (std::isfinite(number) && port->input &&
        (port->type == cydaw::graph::PortType::value ||
         port->type == cydaw::graph::PortType::trigger))
      port->defaultValue = base::limit(-1'000'000.0, 1'000'000.0, number);
    port->exposure =
        base::limit(-1, 1, static_cast<int>(value.getProperty("exposure", -1)));
    if (value.hasProperty("name"))
      port->name = value.getProperty("name", {}).toString().substring(0, 96);
    port->category =
        value.getProperty("category", {}).toString().substring(0, 128);
  }
}
void GraphDocument::applyParameterPortDefaults(
    const cydaw::base::Text &typeKey,
    const cydaw::engine::NativeDspNodeParameters &parameters,
    std::vector<NodePort> &ports) {
  for (auto &port : ports) {
    if (!port.input || (port.type != cydaw::graph::PortType::value &&
                        port.type != cydaw::graph::PortType::trigger))
      continue;
    if (const auto parameter =
            primitiveParameterValueForType(typeKey, port.key, parameters))
      port.defaultValue = *parameter;
  }
}
void GraphDocument::applyLegacyPortDefaults(
    const cydaw::base::Text &typeKey,
    const cydaw::engine::NativeDspNodeParameters &parameters,
    std::vector<NodePort> &ports) {
  auto inputIndex = 0;
  for (auto &port : ports) {
    if (!port.input || (port.type != cydaw::graph::PortType::value &&
                        port.type != cydaw::graph::PortType::trigger))
      continue;
    if (inputIndex == 0)
      port.defaultValue = parameters.valueInputA;
    else if (inputIndex == 1)
      port.defaultValue = parameters.valueInputB;
    ++inputIndex;
  }
  applyParameterPortDefaults(typeKey, parameters, ports);
}
GraphDocument::Node *GraphDocument::findNode(const NodeId id) {
  // Every caller that receives a mutable node might change it, and this is
  // the one place they all pass through. Dropping the cache here costs one
  // node's worth of work when the caller only reads; working out which
  // callers those are would cost correctness.
  const auto found =
      std::find_if(nodes_.begin(), nodes_.end(),
                   [id](const auto &node) { return node.id == id; });
  if (found == nodes_.end())
    return nullptr;
  found->serialised = cydaw::base::Value();
  return &*found;
}
const GraphDocument::Node *GraphDocument::findNode(const NodeId id) const {
  const auto found =
      std::find_if(nodes_.begin(), nodes_.end(),
                   [id](const auto &node) { return node.id == id; });
  return found == nodes_.end() ? nullptr : &*found;
}
const GraphDocument::NodeType *
GraphDocument::findType(const cydaw::base::Text &key) const {
  const auto found =
      std::find_if(catalog_.begin(), catalog_.end(),
                   [&key](const auto &type) { return type.key == key; });
  return found == catalog_.end() ? nullptr : &*found;
}
GraphDocument::SavedModule *
GraphDocument::findModule(const cydaw::base::Text &key) {
  const auto found =
      std::find_if(savedModules_.begin(), savedModules_.end(),
                   [&key](const auto &module) { return module.key == key; });
  return found == savedModules_.end() ? nullptr : &*found;
}
const GraphDocument::SavedModule *
GraphDocument::findModule(const cydaw::base::Text &key) const {
  const auto found =
      std::find_if(savedModules_.begin(), savedModules_.end(),
                   [&key](const auto &module) { return module.key == key; });
  return found == savedModules_.end() ? nullptr : &*found;
}
bool GraphDocument::connectReplacingInput(
    const NodeId source, const NodeId destination,
    const cydaw::graph::PortId sourcePort,
    const cydaw::graph::PortId destinationPort) {
  const auto *destinationNode = findNode(destination);
  const auto *socket = destinationNode != nullptr
                           ? findPort(*destinationNode, destinationPort)
                           : nullptr;
  if (socket != nullptr && socket->acceptsMultipleConnections)
    return connectNodes(source, destination, sourcePort, destinationPort);

  std::vector<Connection> replaced;
  for (auto iterator = connections_.begin(); iterator != connections_.end();) {
    if (iterator->destination == destination &&
        iterator->destinationPort == destinationPort) {
      replaced.push_back(*iterator);
      iterator = connections_.erase(iterator);
    } else {
      ++iterator;
    }
  }
  if (connectNodes(source, destination, sourcePort, destinationPort))
    return true;
  connections_.insert(connections_.end(), replaced.begin(), replaced.end());
  ;
  return false;
}
bool GraphDocument::connectNodes(const NodeId source, const NodeId destination,
                                 cydaw::graph::PortId sourcePort,
                                 cydaw::graph::PortId destinationPort) {
  const auto *sourceNode = findNode(source);
  const auto *destinationNode = findNode(destination);
  if (sourceNode == nullptr || destinationNode == nullptr)
    return false;
  const auto choosePort =
      [this](const Node &node, const cydaw::graph::PortId requested,
             const bool input, const PortKind preferred) -> const NodePort * {
    if (const auto *exact = findPort(node, requested);
        exact != nullptr && exact->input == input)
      return exact;
    const auto matching = std::find_if(
        node.ports.begin(), node.ports.end(),
        [input, preferred](const NodePort &port) {
          return port.input == input && enginePortKind(port.type) == preferred;
        });
    if (matching != node.ports.end())
      return &*matching;
    const auto first = std::find_if(
        node.ports.begin(), node.ports.end(),
        [input](const NodePort &port) { return port.input == input; });
    return first != node.ports.end() ? &*first : nullptr;
  };
  const auto *sourceSocket =
      choosePort(*sourceNode, sourcePort, false, sourceNode->outputKind);
  const auto *destinationSocket = choosePort(*destinationNode, destinationPort,
                                             true, destinationNode->inputKind);
  sourcePort =
      sourceSocket != nullptr ? sourceSocket->id : cydaw::graph::PortId{2};
  destinationPort = destinationSocket != nullptr ? destinationSocket->id
                                                 : cydaw::graph::PortId{1};
  std::optional<Node> adaptedNodeBackup;
  NodeId adaptedNodeId{};
  const auto adaptFreePrimitive =
      [this, &adaptedNodeBackup, &adaptedNodeId](const NodeId nodeId,
                                                 const std::uint8_t channels) {
        auto *candidate = findNode(nodeId);
        if (candidate == nullptr || channels < 1 || channels > 8 ||
            !primitiveSupportsVariableAudioChannels(candidate->typeKey) ||
            std::ranges::any_of(connections_,
                                [nodeId](const Connection &connection) {
                                  return connection.kind == PortKind::audio &&
                                         (connection.source == nodeId ||
                                          connection.destination == nodeId);
                                }))
          return false;

        adaptedNodeBackup = *candidate;
        adaptedNodeId = nodeId;
        if (candidate->inputKind == PortKind::audio)
          candidate->inputChannels = channels;
        if (candidate->outputKind == PortKind::audio)
          candidate->outputChannels = channels;
        for (auto &port : candidate->ports)
          if (port.type == cydaw::graph::PortType::audio)
            port.channels = channels;
        const auto updateControls = [channels](auto &controls) {
          for (auto &control : controls)
            if ((control.kind == ModuleControlKind::inputPort ||
                 control.kind == ModuleControlKind::outputPort) &&
                control.portKind == 0)
              control.portChannels = channels;
        };
        updateControls(candidate->interfaceState.controls);
        updateControls(candidate->interfaceState.deviceControls);
        return true;
      };
  const auto sourceKind = sourceSocket != nullptr
                              ? enginePortKind(sourceSocket->type)
                              : sourceNode->outputKind;
  const auto destinationKind = destinationSocket != nullptr
                                   ? enginePortKind(destinationSocket->type)
                                   : destinationNode->inputKind;
  if (sourceKind == PortKind::audio && destinationKind == PortKind::audio) {
    const auto sourceChannels = sourceSocket != nullptr
                                    ? sourceSocket->channels
                                    : audioOutputChannels(*sourceNode);
    const auto destinationChannels = destinationSocket != nullptr
                                         ? destinationSocket->channels
                                         : audioInputChannels(*destinationNode);
    if (sourceChannels != 0 && destinationChannels != 0 &&
        sourceChannels != destinationChannels) {
      if (!adaptFreePrimitive(destination, sourceChannels))
        (void)adaptFreePrimitive(source, destinationChannels);
    }
  }
  const auto result =
      validateConnection(source, destination, sourcePort, destinationPort);
  if (!result) {
    if (adaptedNodeBackup.has_value())
      if (auto *adapted = findNode(adaptedNodeId); adapted != nullptr)
        *adapted = std::move(*adaptedNodeBackup);
    if (result.error == cydaw::engine::GraphError::incompatiblePortKinds) {
      const auto message = cydaw::base::Text(result.message);
      setStatus(message.containsIgnoreCase("channel")
                    ? tr("nodes.status.channelMismatch")
                    : tr("nodes.status.incompatible"),
                true);
    } else if (result.error == cydaw::engine::GraphError::cycleDetected)
      setStatus(tr("nodes.status.cycle"), true);
    else if (result.error == cydaw::engine::GraphError::duplicateConnection)
      setStatus(tr("nodes.status.duplicateConnection"), true);
    else
      setStatus(cydaw::base::Text(result.message), true);
    return false;
  }
  const auto kind = sourceSocket != nullptr ? enginePortKind(sourceSocket->type)
                                            : sourceNode->outputKind;
  connections_.push_back(
      {source, destination, kind,
       kind == PortKind::audio
           ? (sourceSocket != nullptr ? sourceSocket->channels
                                      : audioOutputChannels(*sourceNode))
           : std::uint8_t{},
       sourcePort, destinationPort});
  if (adaptedNodeId != 0) {
    synchroniseGraphInterface();
    ;
  };
  return true;
}
cydaw::engine::GraphResult GraphDocument::validateConnection(
    const NodeId source, const NodeId destination,
    const cydaw::graph::PortId sourcePort,
    const cydaw::graph::PortId destinationPort) const {
  const auto *sourceNode = findNode(source);
  const auto *destinationNode = findNode(destination);
  if (sourceNode == nullptr || destinationNode == nullptr)
    return {cydaw::engine::GraphError::nodeNotFound, "Unknown graph node"};
  const auto socketFor = [this](const Node &node, const cydaw::graph::PortId id,
                                const bool input) -> const NodePort * {
    if (const auto *exact = findPort(node, id);
        exact != nullptr && exact->input == input)
      return exact;
    const auto first = std::find_if(
        node.ports.begin(), node.ports.end(),
        [input](const NodePort &port) { return port.input == input; });
    return first != node.ports.end() ? &*first : nullptr;
  };
  const auto *sourceSocket = socketFor(*sourceNode, sourcePort, false);
  const auto *destinationSocket =
      socketFor(*destinationNode, destinationPort, true);
  if ((sourceSocket == nullptr && !sourceNode->hasOutput) ||
      (destinationSocket == nullptr && !destinationNode->hasInput))
    return {cydaw::engine::GraphError::invalidPort, "Unavailable graph port"};
  const auto sourceKind = sourceSocket != nullptr
                              ? enginePortKind(sourceSocket->type)
                              : sourceNode->outputKind;
  const auto destinationKind = destinationSocket != nullptr
                                   ? enginePortKind(destinationSocket->type)
                                   : destinationNode->inputKind;
  if (sourceKind != destinationKind)
    return {cydaw::engine::GraphError::incompatiblePortKinds,
            "Port kinds must match"};

  if (sourceKind == PortKind::audio) {
    const auto declaredOutputChannels = [this](const Node &node) {
      if (node.outputChannels != 0)
        return node.outputChannels;
      if (const auto *module = findModule(node.typeKey);
          module != nullptr && module->outputChannels != 0)
        return module->outputChannels;
      if (const auto *type = findType(node.typeKey);
          type != nullptr && type->outputChannels != 0)
        return type->outputChannels;
      return std::uint8_t{};
    }(*sourceNode);
    const auto declaredInputChannels = [this](const Node &node) {
      if (node.inputChannels != 0)
        return node.inputChannels;
      if (const auto *module = findModule(node.typeKey);
          module != nullptr && module->inputChannels != 0)
        return module->inputChannels;
      if (const auto *type = findType(node.typeKey);
          type != nullptr && type->inputChannels != 0)
        return type->inputChannels;
      return std::uint8_t{};
    }(*destinationNode);
    const auto exactOutputChannels = sourceSocket != nullptr
                                         ? sourceSocket->channels
                                         : declaredOutputChannels;
    const auto exactInputChannels = destinationSocket != nullptr
                                        ? destinationSocket->channels
                                        : declaredInputChannels;
    if (exactOutputChannels != 0 && exactInputChannels != 0 &&
        exactOutputChannels != exactInputChannels)
      return {cydaw::engine::GraphError::incompatiblePortKinds,
              "Audio channel counts must match"};
  }

  if (destinationSocket != nullptr &&
      !destinationSocket->acceptsMultipleConnections &&
      std::ranges::any_of(connections_, [destination, destinationPort](
                                            const Connection &connection) {
        return connection.destination == destination &&
               connection.destinationPort == destinationPort;
      }))
    return {cydaw::engine::GraphError::duplicateConnection,
            "Destination port already has a connection"};

  cydaw::engine::AudioGraph graph;
  for (const auto &node : nodes_) {
    cydaw::engine::NodeDescriptor descriptor;
    descriptor.id = node.id;
    descriptor.name = displayTitle(node).toStdString();
    if (!node.ports.empty()) {
      for (const auto &port : node.ports) {
        auto *count = [&descriptor, &port]() -> std::uint32_t * {
          const auto kind = enginePortKind(port.type);
          if (kind == PortKind::audio)
            return port.input ? &descriptor.audioInputs
                              : &descriptor.audioOutputs;
          if (kind == PortKind::midi)
            return port.input ? &descriptor.midiInputs
                              : &descriptor.midiOutputs;
          if (kind == PortKind::transport)
            return port.input ? &descriptor.transportInputs
                              : &descriptor.transportOutputs;
          return port.input ? &descriptor.controlInputs
                            : &descriptor.controlOutputs;
        }();
        ++*count;
      }
    } else if (node.hasInput) {
      if (node.inputKind == PortKind::audio)
        descriptor.audioInputs = 1;
      else if (node.inputKind == PortKind::midi)
        descriptor.midiInputs = 1;
      else if (node.inputKind == PortKind::control)
        descriptor.controlInputs = 1;
      else
        descriptor.transportInputs = 1;
    }
    if (node.ports.empty() && node.hasOutput) {
      if (node.outputKind == PortKind::audio)
        descriptor.audioOutputs = 1;
      else if (node.outputKind == PortKind::midi)
        descriptor.midiOutputs = 1;
      else if (node.outputKind == PortKind::control)
        descriptor.controlOutputs = 1;
      else
        descriptor.transportOutputs = 1;
    }
    if (const auto added = graph.addNode(std::move(descriptor)); !added)
      return added;
  }
  const auto portIndex = [this](const Node &node, const cydaw::graph::PortId id,
                                const bool input, const PortKind kind) {
    std::uint32_t index{};
    for (const auto &port : node.ports) {
      if (port.input != input || enginePortKind(port.type) != kind)
        continue;
      if (port.id == id)
        return index;
      ++index;
    }
    return std::uint32_t{};
  };
  // The cables already there, all at once: compiled once, not once each.
  std::vector<cydaw::engine::Connection> existing;
  existing.reserve(connections_.size());
  for (const auto &connection : connections_) {
    const auto *existingSource = findNode(connection.source);
    const auto *existingDestination = findNode(connection.destination);
    if (existingSource == nullptr || existingDestination == nullptr)
      continue;
    existing.push_back(
        {{connection.source,
          portIndex(*existingSource, connection.sourcePort, false,
                    connection.kind),
          connection.kind},
         {connection.destination,
          portIndex(*existingDestination, connection.destinationPort, true,
                    connection.kind),
          connection.kind}});
  }
  if (const auto added = graph.connectAll(existing); !added)
    return added;
  return graph.connect(
      {{source, portIndex(*sourceNode, sourcePort, false, sourceKind),
        sourceKind},
       {destination,
        portIndex(*destinationNode, destinationPort, true, destinationKind),
        destinationKind}});
}
GraphDocument::PortKind
GraphDocument::enginePortKind(const cydaw::graph::PortType type) {
  if (type == cydaw::graph::PortType::audio)
    return PortKind::audio;
  if (type == cydaw::graph::PortType::midi)
    return PortKind::midi;
  if (type == cydaw::graph::PortType::transport)
    return PortKind::transport;
  return PortKind::control;
}
GraphDocument::PortKind GraphDocument::portKindFromInt(const int value) {
  if (value == static_cast<int>(PortKind::midi))
    return PortKind::midi;
  if (value == static_cast<int>(PortKind::control))
    return PortKind::control;
  if (value == static_cast<int>(PortKind::transport))
    return PortKind::transport;
  return PortKind::audio;
}
std::vector<GraphDocument::DeviceCatalogueEntry>
GraphDocument::deviceCatalogue() const {
  const auto portKindName = [](const bool present,
                               const PortKind kind) -> cydaw::base::Text {
    if (!present)
      return "none";
    switch (kind) {
    case PortKind::audio:
      return "audio";
    case PortKind::midi:
      return "midi";
    case PortKind::control:
      return "control";
    default:
      return "transport";
    }
  };
  std::vector<DeviceCatalogueEntry> entries;
  for (const auto &preset : factoryPresetSummaries()) {
    entries.push_back({preset.key,
                       tr(preset.translationKey),
                       "preset",
                       preset.soundSource ? "instrument" : "effect",
                       {},
                       preset.soundSource,
                       preset.soundSource ? "midi" : "audio",
                       "audio"});
    entries.back().englishName =
        Localization::instance().englishText(preset.translationKey);
  }
  for (const auto &module : savedModules_) {
    if (!module.factoryModule || !module.hasOutput)
      continue;
    entries.push_back({module.key, module.name, "module",
                       module.category.isNotEmpty() ? tr(module.category)
                                                    : cydaw::base::Text(),
                       module.description,
                       module.hasInput && module.inputKind != PortKind::audio &&
                           module.outputKind == PortKind::audio,
                       portKindName(module.hasInput, module.inputKind),
                       portKindName(module.hasOutput, module.outputKind)});
  }
  // Every primitive with an output that is not a graph boundary or a
  // track's own input: the legacy drive and low-pass are "protected"
  // only in the sense that the default graph keeps them.
  for (const auto &type : catalog_) {
    if (!type.hasOutput || type.composite || type.key.startsWith("graph-") ||
        type.key == "midi-input" || type.key == "audio-clips" ||
        type.key.startsWith("missing-") || type.key == "midi-arp" ||
        type.key == "container-end")
      continue;
    entries.push_back(
        {type.key,
         type.title.isNotEmpty() ? type.title : tr(type.translationKey), "node",
         type.categoryKey.isNotEmpty() ? tr(type.categoryKey)
                                       : cydaw::base::Text(),
         type.descriptionKey.isNotEmpty() ? tr(type.descriptionKey)
                                          : cydaw::base::Text(),
         type.outputKind == PortKind::audio &&
             ((type.hasInput && type.inputKind == PortKind::midi) ||
              cydaw::engine::synth::modelForType(type.key.toStdString()) !=
                  cydaw::engine::synth::Model::none ||
              cydaw::engine::instrument_container::isContainerType(type.key.toStdString()) ||
              type.key == "sampler" || type.key == "batterie" ||
              type.key == "prism-synth" || type.key == "kick-synth"),
         portKindName(type.hasInput, type.inputKind),
         portKindName(type.hasOutput, type.outputKind)});
    if (entries.back().description.isEmpty()) {
      if (const auto model = cydaw::engine::synth::modelForType(type.key.toStdString());
          model != cydaw::engine::synth::Model::none) {
        const auto& info = cydaw::engine::synth::info(model);
        entries.back().description = Localization::instance().languageCode() == "fr"
            ? info.frenchHint : info.englishHint;
      } else {
        const auto descriptionKey = "library.device." + type.key;
        const auto description = tr(descriptionKey);
        if (description != descriptionKey) entries.back().description = description;
      }
    }
    entries.back().categoryKey = type.categoryKey;
    entries.back().englishName =
        type.title.isNotEmpty()
            ? type.title
            : Localization::instance().englishText(type.translationKey);
  }
  for (auto &entry : entries)
    if (entry.englishName.isEmpty())
      entry.englishName = entry.name;
  return entries;
}
std::uint64_t
GraphDocument::lastNodeIdOfType(const cydaw::base::Text &typeKey) const {
  return nodeIdForType(typeKey);
}
bool GraphDocument::removeNodeById(const std::uint64_t id) {
  auto *node = id == 0 ? nullptr : findNode(id);
  if (node == nullptr)
    return false;
  // The boundaries stay: a track's input and output, the graph's ports.
  // The default graph's drive and low-pass are protected only from the
  // Delete key; asked for by name, they go.
  if (node->typeKey == "master" || node->typeKey == "midi-input" ||
      node->typeKey == "audio-clips" || node->typeKey.startsWith("graph-"))
    return false;
  node->protectedNode = false;
  selectedNodes_.clear();
  selectedNodes_.insert(id);
  selectedCommentId_ = 0;
  selectedConnection_ = -1;
  selectedPortId_ = 0;
  removeSelectedNodes();
  return findNode(id) == nullptr;
}
bool GraphDocument::editorRemoveFromChain(const std::uint64_t id) {
  auto *node = findNode(id);
  if (editingLocked_ || node == nullptr || isBoundaryNode(*node))
    return false;
  // A container goes with all it holds.
  if (editorIsContainer(id))
    return editorRemoveContainer(id);
  // The signal around it: audio and MIDI, not the control cables that
  // only steered it, nor what fed its sidechain (another track's: joined
  // to the chain, it would play there instead of the track's own sound).
  std::set<cydaw::graph::PortId> sidechains;
  for (const auto &port : node->ports)
    if (port.input && port.key == "sidechain-in")
      sidechains.insert(port.id);
  std::vector<Connection> into;
  std::vector<Connection> out;
  for (const auto &connection : connections_) {
    if (connection.kind != PortKind::audio && connection.kind != PortKind::midi)
      continue;
    if (connection.destination == id && connection.source != id &&
        !sidechains.contains(connection.destinationPort))
      into.push_back(connection);
    if (connection.source == id && connection.destination != id)
      out.push_back(connection);
  }
  // Its modulators go with it (as with a container or a branch): left,
  // they would be invisible, owned by no card.
  std::set<NodeId> going{id};
  for (const auto &candidate : nodes_)
    if (const auto owner = editorModulatorOwner(candidate.id); owner == id)
      going.insert(candidate.id);
  std::erase_if(connections_, [&going](const Connection &connection) {
    return going.contains(connection.source) ||
           going.contains(connection.destination);
  });
  std::erase_if(nodes_,
                [&going](const Node &item) { return going.contains(item.id); });
  for (const auto gone : going)
    selectedNodes_.erase(gone);
  selectedConnection_ = -1;
  selectedPortId_ = 0;
  for (const auto &source : into)
    for (const auto &destination : out)
      // connectNodes checks the ports; no loop can appear where none was.
      if (source.kind == destination.kind)
        (void)connectReplacingInput(source.source, destination.destination,
                                    source.sourcePort,
                                    destination.destinationPort);
  for (auto &item : nodes_)
    item.serialised = cydaw::base::Value();
  synchroniseGraphInterface();
  ;
  notifyLayoutChanged("v2-remove-from-chain");
  return true;
}
bool GraphDocument::editorMoveInChain(const std::uint64_t id,
                                      const std::uint64_t before) {
  // A container moves with all it holds (its head's way out is its dry
  // path, inside it): as the device panel's other drops move it.
  if (editorIsContainer(id))
    return editorMoveCard(id, 0, -1, before);
  auto *node = findNode(id);
  if (editingLocked_ || node == nullptr || isBoundaryNode(*node) ||
      id == before || instrumentVoiceBoundary().voiceNodes.contains(id))
    return false;
  // Where the signal goes in and comes out: the first audio ports (a
  // sidechain is not the way through).
  const auto mainPort =
      [](const Node &card,
         const bool input) -> std::optional<cydaw::graph::PortId> {
    for (const auto &port : card.ports)
      if (port.input == input && port.type == cydaw::graph::PortType::audio &&
          port.key != "sidechain-in")
        return port.id;
    return std::nullopt;
  };
  const auto in = mainPort(*node, true);
  const auto out = mainPort(*node, false);
  if (!in || !out)
    return false;
  // Before what: the card named, else the chain's output.
  const Node *target =
      before != 0 ? findNode(static_cast<NodeId>(before)) : nullptr;
  if (before == 0)
    for (const auto &candidate : nodes_) {
      auto input = false;
      if (candidate.typeKey == "master" ||
          (isGraphAudioBoundaryKey(candidate.typeKey, &input) && !input)) {
        target = &candidate;
        break;
      }
    }
  if (target == nullptr || target->id == id ||
      instrumentVoiceBoundary().voiceNodes.contains(target->id))
    return false;
  const auto targetId = target->id;
  const auto targetIn = mainPort(*target, true);
  if (!targetIn)
    return false;
  // Already there: what feeds the target is this card.
  if (std::any_of(connections_.begin(), connections_.end(),
                  [&](const Connection &connection) {
                    return connection.kind == PortKind::audio &&
                           connection.destination == targetId &&
                           connection.destinationPort == *targetIn &&
                           connection.source == id;
                  }))
    return false;

  // Out of the chain: what fed it now feeds what it fed.
  std::vector<Connection> into;
  std::vector<Connection> onward;
  for (const auto &connection : connections_) {
    if (connection.kind != PortKind::audio)
      continue;
    if (connection.destination == id && connection.destinationPort == *in &&
        connection.source != id)
      into.push_back(connection);
    if (connection.source == id && connection.sourcePort == *out &&
        connection.destination != id)
      onward.push_back(connection);
  }
  std::erase_if(connections_, [&](const Connection &connection) {
    return connection.kind == PortKind::audio &&
           ((connection.destination == id &&
             connection.destinationPort == *in) ||
            (connection.source == id && connection.sourcePort == *out));
  });
  for (const auto &source : into)
    for (const auto &destination : onward)
      (void)connectReplacingInput(source.source, destination.destination,
                                  source.sourcePort,
                                  destination.destinationPort);
  // Back in, before the target: what fed the target feeds it.
  std::vector<Connection> feeding;
  for (const auto &connection : connections_)
    if (connection.kind == PortKind::audio &&
        connection.destination == targetId &&
        connection.destinationPort == *targetIn)
      feeding.push_back(connection);
  std::erase_if(connections_, [&](const Connection &connection) {
    return connection.kind == PortKind::audio &&
           connection.destination == targetId &&
           connection.destinationPort == *targetIn;
  });
  for (const auto &source : feeding)
    (void)connectReplacingInput(source.source, id, source.sourcePort, *in);
  (void)connectReplacingInput(id, targetId, *out, *targetIn);
  selectedConnection_ = -1;
  for (auto &item : nodes_)
    item.serialised = cydaw::base::Value();
  synchroniseGraphInterface();
  ;
  notifyLayoutChanged("v2-move-in-chain");
  return true;
}
bool GraphDocument::removeNodeFromChain(const std::uint64_t id) {
  const auto *node = id == 0 ? nullptr : findNode(id);
  if (node == nullptr || node->typeKey == "master" ||
      node->typeKey == "midi-input" || node->typeKey == "audio-clips" ||
      node->typeKey.startsWith("graph-"))
    return false;
  return editorRemoveFromChain(id);
}
bool GraphDocument::removeNodeFromChainByType(
    const cydaw::base::Text &typeKey) {
  return removeNodeFromChain(nodeIdForType(typeKey));
}
namespace {
cydaw::base::Text remotePortTypeName(const cydaw::graph::PortType type) {
  switch (type) {
  case cydaw::graph::PortType::audio:
    return "audio";
  case cydaw::graph::PortType::midi:
    return "midi";
  case cydaw::graph::PortType::value:
    return "value";
  case cydaw::graph::PortType::trigger:
    return "trigger";
  case cydaw::graph::PortType::transport:
    return "transport";
  default:
    return "other";
  }
}
template <typename Ports> cydaw::base::Value remotePorts(const Ports &ports) {
  cydaw::base::Array<cydaw::base::Value> out;
  for (const auto &port : ports) {
    auto *entry = new cydaw::base::Object();
    entry->setProperty("id", static_cast<std::int64_t>(port.id));
    entry->setProperty("key", port.key);
    entry->setProperty("name", port.name);
    entry->setProperty("type", remotePortTypeName(port.type));
    entry->setProperty("input", port.input);
    entry->setProperty("channels", static_cast<int>(port.channels));
    out.add(cydaw::base::Value(entry));
  }
  return out;
}
} // namespace
std::uint64_t
GraphDocument::addNodeForRemote(const cydaw::base::Text &typeKey,
                                const base::Point<float> position) {
  const auto *type = findType(typeKey);
  if (type == nullptr)
    return 0;
  const auto before = nodes_.size();
  addNodeFromType(*type, position);
  if (nodes_.size() <= before)
    return 0;
  synchroniseGraphInterface();
  ;
  ;
  ;
  notifyLayoutChanged("remote");
  return nodes_.back().id;
}
bool GraphDocument::connectNodesForRemote(const std::uint64_t source,
                                          const std::uint64_t destination,
                                          const std::uint64_t sourcePort,
                                          const std::uint64_t destinationPort) {
  if (findNode(source) == nullptr || findNode(destination) == nullptr)
    return false;
  const auto cablesBefore = connections_.size();
  if (!connectNodes(source, destination, sourcePort, destinationPort))
    return false;
  if (connections_.size() > cablesBefore)
    applyDefaultCableMode(source, destination,
                          connections_.back().destinationPort);
  for (auto &node : nodes_)
    if (node.id == source || node.id == destination)
      node.serialised = cydaw::base::Value();
  synchroniseGraphInterface();
  ;
  ;
  ;
  notifyLayoutChanged("remote");
  return true;
}
int GraphDocument::disconnectNodesForRemote(const std::uint64_t source,
                                            const std::uint64_t destination) {
  const auto before = connections_.size();
  std::erase_if(connections_, [source,
                               destination](const Connection &connection) {
    return connection.source == source && connection.destination == destination;
  });
  const auto removed = static_cast<int>(before - connections_.size());
  if (removed > 0) {
    for (auto &node : nodes_)
      if (node.id == source || node.id == destination)
        node.serialised = cydaw::base::Value();
    selectedConnection_ = -1;
    synchroniseGraphInterface();
    ;
    ;
    ;
    notifyLayoutChanged("remote");
  }
  return removed;
}
bool GraphDocument::setNodePresentationForRemote(
    const std::uint64_t id, const cydaw::base::Text &title,
    const std::optional<base::Point<float>> position) {
  auto *node = findNode(id);
  if (node == nullptr)
    return false;
  // The name a card shows is its alias; the title stays the type's.
  if (title.isNotEmpty())
    node->alias = title.trim();
  if (position.has_value())
    node->position = *position;
  node->serialised = cydaw::base::Value();
  ;
  ;
  notifyLayoutChanged("remote");
  return true;
}
cydaw::base::Value GraphDocument::describeNodeTypeForRemote(
    const cydaw::base::Text &typeKey) const {
  const auto *type = findType(typeKey);
  if (type == nullptr)
    return {};
  auto *object = new cydaw::base::Object();
  cydaw::base::Value result(object);
  object->setProperty("key", type->key);
  object->setProperty("name", type->title.isNotEmpty()
                                  ? type->title
                                  : tr(type->translationKey));
  object->setProperty("category", type->categoryKey.isNotEmpty()
                                      ? tr(type->categoryKey)
                                      : cydaw::base::Text());
  object->setProperty("description", type->descriptionKey.isNotEmpty()
                                         ? tr(type->descriptionKey)
                                         : cydaw::base::Text());
  object->setProperty("hasInput", type->hasInput);
  object->setProperty("hasOutput", type->hasOutput);
  cydaw::base::Array<cydaw::base::Value> parameters;
  for (const auto &parameter : type->parameterSchema) {
    auto *entry = new cydaw::base::Object();
    entry->setProperty("key", cydaw::base::Text(parameter.key));
    entry->setProperty("minimum", parameter.minimum);
    entry->setProperty("maximum", parameter.maximum);
    entry->setProperty("default", parameter.defaultValue);
    parameters.add(cydaw::base::Value(entry));
  }
  object->setProperty("parameters", parameters);
  object->setProperty("ports", remotePorts(type->ports));
  return result;
}
cydaw::base::Value
GraphDocument::describeNodeForRemote(const std::uint64_t id) const {
  const auto *node = findNode(id);
  if (node == nullptr)
    return {};
  auto *object = new cydaw::base::Object();
  cydaw::base::Value result(object);
  object->setProperty("id", static_cast<std::int64_t>(node->id));
  object->setProperty("type", node->typeKey);
  object->setProperty("title", node->alias.trim().isNotEmpty()
                                   ? node->alias.trim()
                                   : node->title);
  object->setProperty("alias", node->alias);
  object->setProperty("x", node->position.x);
  object->setProperty("y", node->position.y);
  object->setProperty("bypassed", node->bypassed);
  object->setProperty("ports", remotePorts(node->ports));
  return result;
}
std::uint64_t GraphDocument::addCommentForRemote(
    const cydaw::base::Text &title, const cydaw::base::Text &description,
    const base::Point<float> position, const base::Point<float> size) {
  GraphComment comment;
  comment.id = nextCommentId_++;
  comment.title = title;
  comment.description = description;
  comment.position = position;
  if (size.x > 0.0f && size.y > 0.0f)
    comment.size = size;
  comments_.push_back(comment);
  ;
  notifyLayoutChanged("remote");
  return comment.id;
}
bool GraphDocument::removeCommentForRemote(const std::uint64_t id) {
  const auto before = comments_.size();
  std::erase_if(comments_,
                [id](const GraphComment &comment) { return comment.id == id; });
  if (comments_.size() == before)
    return false;
  if (selectedCommentId_ == id)
    selectedCommentId_ = 0;
  ;
  notifyLayoutChanged("remote");
  return true;
}

void GraphDocument::processPendingChanges() {
  if (std::exchange(pendingChanges_, false))
    rebuildPendingWavetableBanks();
  if (std::exchange(validationPending_, false))
    refreshGraphAssetValidation();
  if (std::exchange(parameterNotificationPending_, false))
    dispatchParameterNotification();
  if (parameterHistoryPending_ && !parameterGestureActive_ &&
      base::Time::currentTimeMillis() - lastParameterUiChangeTimeMs_ >= 180)
    commitQueuedParameterHistory();
}
GraphDocument::GraphDocument() {
  graphReference_.id = ("project." + base::Uuid().toString()).toStdString();
  graphReference_.version = {1, 0, 0};
  initialiseCatalog();
  ensureFactoryModules();
  initialiseFactoryPresets();
  initialiseDefaultGraph();
  applyTranslatedTitles();
  refreshGraphAssetValidation();
  resetEditHistory();
}
void GraphDocument::refreshTranslations() {
  factoryModulesInstalled_ = false;
  for (auto &type : catalog_)
    if (type.translationKey.isNotEmpty())
      type.title = tr(type.translationKey);
  ensureFactoryModules();
  for (auto &module : savedModules_)
    registerModuleType(module);
  applyTranslatedTitles();
}

void GraphDocument::notifyLayoutChanged(const char *origin) {
  ++layoutChangeCalls_;
  layoutChangeOrigins_ = origin;
  activeFactoryPresetKey_.clear();
  scheduleGraphAssetValidation();
  commitEditHistory(false);
  if (layoutChangedHandler_)
    layoutChangedHandler_();
}
void GraphDocument::scheduleGraphAssetValidation() {
  validationPending_ = true;
}
void GraphDocument::setControlValues(
    const std::vector<std::pair<std::uint64_t, float>> &values) {
  controlValues_.clear();
  for (const auto &[id, value] : values)
    controlValues_[id] = value;
}
void GraphDocument::setStatus(const base::Text &message, bool error) {
  statusMessage_ = message;
  statusIsError_ = error;
}
void GraphDocument::setDspCompilationStatus(const base::Result &result,
                                            std::size_t count) {
  dspCompilationKnown_ = true;
  dspCompilationOk_ = result.wasOk();
  dspCompilationNodeCount_ = count;
  dspCompilationError_ = result.getErrorMessage();
}
void GraphDocument::setAnalysisFrame(const engine::AudioAnalysisFrame &frame) {
  lastAnalysisFrame_ = frame;
  lastAnalysisFrameHeld_ = true;
}
void GraphDocument::clearAnalysis() noexcept {
  lastAnalysisFrame_ = {};
  lastAnalysisFrameHeld_ = false;
  controlValues_.clear();
}
void GraphDocument::queueParameterNotification() {
  parameterNotificationPending_ = true;
  parameterHistoryPending_ = true;
  lastParameterUiChangeTimeMs_ = base::Time::currentTimeMillis();
}
void GraphDocument::flushParameterNotification() {
  commitQueuedParameterHistory();
  if (parameterNotificationPending_) {
    parameterNotificationPending_ = false;
    dispatchParameterNotification();
  }
}
base::Point<float> GraphDocument::nodeVisualSize(const Node &node) const {
  const auto inputCount = std::ranges::count_if(
      node.ports, [](const NodePort &port) { return port.input; });
  const auto outputCount = std::ranges::count_if(
      node.ports, [](const NodePort &port) { return !port.input; });
  const auto largestSide = std::max(inputCount, outputCount);
  if (isBoundaryNode(node))
    // The master card carries two meters under its caption; every other
    // boundary card carries a caption alone. Seventy-two units for both
    // is what had the meters printing over the text.
    return {nodeWidth,
            node.typeKey == "master" && !node.collapsed ? 96.0f : 72.0f};
  if (node.collapsed)
    return {largestSide > 1 ? 286.0f : nodeWidth,
            std::max(58.0f, 36.0f + static_cast<float>(largestSide) * 22.0f)};

  auto visualWidth = largestSide > 1 ? 286.0f : nodeWidth;
  auto visualHeight =
      std::max(nodeHeight, 56.0f + static_cast<float>(largestSide) * 22.0f);
  auto requiredContentWidth = 0.0f;
  auto requiredContentHeight = 0.0f;
  for (const auto &control : moduleInterfaceControls(
           presentationForNode(node), ModuleInterfaceLayoutMode::graph)) {
    auto minimumWidth = 0.0f;
    auto minimumHeight = 0.0f;
    switch (control.kind) {
    case ModuleControlKind::knob:
    case ModuleControlKind::fader:
      minimumWidth = 56.0f;
      minimumHeight = 72.0f;
      break;
    case ModuleControlKind::toggle:
      minimumWidth = 42.0f;
      minimumHeight = 34.0f;
      break;
    case ModuleControlKind::selector:
      minimumWidth = 68.0f;
      minimumHeight = 36.0f;
      break;
    case ModuleControlKind::xyPad:
    case ModuleControlKind::envelope:
    case ModuleControlKind::filterResponse:
    case ModuleControlKind::dynamicsResponse:
      minimumWidth = 108.0f;
      minimumHeight = 78.0f;
      break;
    case ModuleControlKind::eqCurve:
      // A curve needs decades of frequency to be worth reading.
      minimumWidth = 190.0f;
      minimumHeight = 96.0f;
      break;
    case ModuleControlKind::wavetable:
      // A stack of eight cycles with a strip of frames under it. Eight
      // waves cannot be told apart in less room than this however they
      // are drawn, so the card asks for it rather than crowding them.
      minimumWidth = 230.0f;
      minimumHeight = 168.0f;
      break;
    case ModuleControlKind::waveform:
    case ModuleControlKind::spectrum:
      minimumWidth = 104.0f;
      minimumHeight = 72.0f;
      break;
    case ModuleControlKind::levelMeter:
      // A meter is a narrow vertical strip. Asking for a plot's width
      // made every card carrying one demand 520 px of content and run
      // into the cap below.
      minimumWidth = 34.0f;
      minimumHeight = 72.0f;
      break;
    case ModuleControlKind::valueDisplay:
      minimumWidth = 72.0f;
      minimumHeight = 34.0f;
      break;
    case ModuleControlKind::text:
    case ModuleControlKind::inputPort:
    case ModuleControlKind::outputPort:
    case ModuleControlKind::group:
      break;
    }
    if (minimumWidth > 0.0f && control.width > 0.001f)
      requiredContentWidth =
          std::max(requiredContentWidth, minimumWidth / control.width);
    if (minimumHeight > 0.0f && control.height > 0.001f)
      requiredContentHeight =
          std::max(requiredContentHeight, minimumHeight / control.height);
  }

  // Fourteen pixels account for the node frame, plus the port-label gutters
  // nodeContentBounds() takes from a multi-port card. Thirty-eight pixels
  // account for the title bar and the content inset. Caps keep
  // intentionally tiny authored controls from creating giant nodes.
  const auto gutters =
      contentGutterWidth(inputCount, outputCount, node.ports.size());
  visualWidth = std::min(
      480.0f, std::max(visualWidth, requiredContentWidth + 14.0f + gutters));
  visualHeight =
      std::min(400.0f, std::max(visualHeight, requiredContentHeight + 38.0f));
  return {visualWidth, visualHeight};
}
base::Point<float>
GraphDocument::freePlacementFor(const Node &node,
                                const base::Point<float> preferred,
                                const NodeId ignoredNodeId) const {
  const auto canvas = layoutSpace_;
  const auto size = nodeVisualSize(node);
  if (canvas.getWidth() < size.x * 1.5f || canvas.getHeight() < size.y * 1.5f) {
    // No usable canvas yet — nodes added before the editor is laid out, as
    // happens when a graph is built from the command line. A lattice at
    // least keeps them apart.
    const auto index = nodes_.size();
    return {
        base::limit(0.0f, 0.92f, 0.06f + static_cast<float>(index % 4) * 0.29f),
        base::limit(0.0f, 0.88f,
                    0.08f + static_cast<float>((index / 4) % 3) * 0.31f)};
  }

  // Positions are normalised across the canvas minus the card, so that span
  // is what one normalised unit is worth for a card of this size.
  const auto baseRect = [&canvas](const base::Point<float> position,
                                  const base::Point<float> extent) {
    return base::Rectangle<float>{
        canvas.getX() +
            position.x * std::max(1.0f, canvas.getWidth() - extent.x),
        canvas.getY() +
            position.y * std::max(1.0f, canvas.getHeight() - extent.y),
        extent.x, extent.y};
  };
  // The other cards do not move while a spot is being looked for, and each
  // one's size is a linear search through the module list. Recomputing them
  // for every candidate, on fourteen rings of candidates, is where placing
  // a node spent its time.
  std::vector<base::Rectangle<float>> occupied;
  occupied.reserve(nodes_.size());
  for (const auto &other : nodes_)
    if (other.id != ignoredNodeId)
      occupied.push_back(baseRect(other.position, nodeVisualSize(other)));

  // Counted rather than timed: on a loaded machine a twenty per cent
  // difference cannot be told from noise, but a count cannot drift. The
  // second figure used to be the first multiplied by the card count.
  auto candidates = 0;
  const auto isFree = [&occupied, &baseRect, size,
                       &candidates](const base::Point<float> position) {
    ++candidates;
    // A little breathing room, so cards that merely touch still read as two.
    const auto probe = baseRect(position, size).reduced(6.0f);
    return std::ranges::none_of(occupied,
                                [&probe](const base::Rectangle<float> &other) {
                                  return probe.intersects(other);
                                });
  };
  const auto reportPlacement = [this, &candidates, &occupied] {
    placementProfile_ =
        "candidates=" + cydaw::base::Text(candidates) +
        " sizes=" + cydaw::base::Text(static_cast<int>(occupied.size()));
  };

  if (isFree(preferred)) {
    reportPlacement();
    return preferred;
  }

  // Walk outward in card-sized steps. Right and down come first, which is
  // where a reader expects the next node in a left-to-right signal flow.
  const auto stepX =
      (size.x + 40.0f) / std::max(1.0f, canvas.getWidth() - size.x);
  const auto stepY =
      (size.y + 32.0f) / std::max(1.0f, canvas.getHeight() - size.y);
  for (auto ring = 1; ring <= 14; ++ring)
    for (auto row = -ring; row <= ring; ++row)
      for (auto column = -ring; column <= ring; ++column) {
        if (std::max(std::abs(row), std::abs(column)) != ring)
          continue;
        // Measured on an 878 x 749 canvas, cards run 294 to 480 px
        // wide: barely four fit on screen at all. Clamping candidates
        // to the visible 0..1 window meant the fifth node had nowhere
        // to go and landed back on top of its neighbour. The graph
        // itself is far larger, so spill just outside the view — the
        // minimap shows it and a pan reaches it.
        const base::Point<float> candidate{
            graphCoordinate(preferred.x + static_cast<float>(column) * stepX,
                            false),
            graphCoordinate(preferred.y + static_cast<float>(row) * stepY,
                            false)};
        if (isFree(candidate)) {
          reportPlacement();
          return candidate;
        }
      }
  reportPlacement();
  return preferred;
}
bool GraphDocument::graphNodesOverlap() const {
  const auto nodeBounds = [this](const Node &node) {
    const auto size = nodeVisualSize(node);
    return base::Rectangle<float>{
        node.position.x * std::max(1.f, layoutSpace_.getWidth() - size.x),
        node.position.y * std::max(1.f, layoutSpace_.getHeight() - size.y),
        size.x, size.y};
  };
  for (std::size_t first = 0; first < nodes_.size(); ++first)
    for (std::size_t second = first + 1; second < nodes_.size(); ++second)
      if (nodeBounds(nodes_[first])
              .expanded(5.0f)
              .intersects(nodeBounds(nodes_[second])))
        return true;
  return false;
}
} // namespace cydaw::app
