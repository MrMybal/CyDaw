#include <algorithm>
#include <bit>
#include <cmath>
#include <cydaw/document/ModuleInterface.h>
#include <set>
namespace cydaw::app {
namespace jsonKeys {
// Property names as identifiers made once: an Identifier built from a
// literal is a string-pool lookup, and a restore or a save of a large
// graph made tens of thousands of them.
static const cydaw::base::Identifier automatable("automatable");
static const cydaw::base::Identifier binding("binding");
static const cydaw::base::Identifier controls("controls");
static const cydaw::base::Identifier defaultValue("defaultValue");
static const cydaw::base::Identifier description("description");
static const cydaw::base::Identifier deviceControls("deviceControls");
static const cydaw::base::Identifier extraBindings("extraBindings");
static const cydaw::base::Identifier gridColumns("gridColumns");
static const cydaw::base::Identifier gridRows("gridRows");
static const cydaw::base::Identifier windowWidth("windowWidth");
static const cydaw::base::Identifier windowHeight("windowHeight");
static const cydaw::base::Identifier designed("designed");
static const cydaw::base::Identifier height("height");
static const cydaw::base::Identifier hostParameterId("hostParameterId");
static const cydaw::base::Identifier id("id");
static const cydaw::base::Identifier key("key");
static const cydaw::base::Identifier kind("kind");
static const cydaw::base::Identifier label("label");
static const cydaw::base::Identifier maximum("maximum");
static const cydaw::base::Identifier minimum("minimum");
static const cydaw::base::Identifier modulatable("modulatable");
static const cydaw::base::Identifier nextControlId("nextControlId");
static const cydaw::base::Identifier parameters("parameters");
static const cydaw::base::Identifier portChannels("portChannels");
static const cydaw::base::Identifier portKind("portKind");
static const cydaw::base::Identifier publishToHost("publishToHost");
static const cydaw::base::Identifier quaternaryBinding("quaternaryBinding");
static const cydaw::base::Identifier schema("schema");
static const cydaw::base::Identifier secondaryBinding("secondaryBinding");
static const cydaw::base::Identifier showInDevice("showInDevice");
static const cydaw::base::Identifier snapToGrid("snapToGrid");
static const cydaw::base::Identifier tertiaryBinding("tertiaryBinding");
static const cydaw::base::Identifier unit("unit");
static const cydaw::base::Identifier value("value");
static const cydaw::base::Identifier width("width");
static const cydaw::base::Identifier x("x");
static const cydaw::base::Identifier y("y");
} // namespace jsonKeys

double moduleParameterDefaultValue(
    const ModulePublishedParameter &parameter) noexcept {
  return std::isfinite(parameter.defaultValue) &&
                 parameter.defaultValue >= parameter.minimum &&
                 parameter.defaultValue <= parameter.maximum
             ? parameter.defaultValue
             : parameter.value;
}

std::vector<double>
moduleSelectorValues(const ModulePublishedParameter &parameter) {
  if (parameter.maximum <= parameter.minimum)
    return {parameter.minimum};
  const auto roundedMinimum = std::round(parameter.minimum);
  const auto roundedMaximum = std::round(parameter.maximum);
  const auto integerRange =
      std::abs(parameter.minimum - roundedMinimum) < 1.0e-6 &&
      std::abs(parameter.maximum - roundedMaximum) < 1.0e-6 &&
      roundedMaximum - roundedMinimum <= 31.0;
  const auto count =
      integerRange
          ? static_cast<std::size_t>(roundedMaximum - roundedMinimum + 1.0)
          : std::size_t{5};
  std::vector<double> values;
  values.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    const auto normalised =
        count > 1 ? static_cast<double>(index) / static_cast<double>(count - 1)
                  : 0.0;
    values.push_back(parameter.minimum +
                     (parameter.maximum - parameter.minimum) * normalised);
  }
  return values;
}

std::size_t closestModuleSelectorValue(const std::vector<double> &values,
                                       const double value) noexcept {
  if (values.empty())
    return 0;
  auto closest = std::size_t{};
  auto distance = std::abs(values.front() - value);
  for (std::size_t index = 1; index < values.size(); ++index) {
    const auto candidate = std::abs(values[index] - value);
    if (candidate < distance) {
      closest = index;
      distance = candidate;
    }
  }
  return closest;
}

cydaw::base::Text
moduleSelectorValueLabel(const ModulePublishedParameter &parameter,
                         const double value) {
  const auto integer = std::abs(value - std::round(value)) < 1.0e-6;
  auto label = cydaw::base::Text(value, integer ? 0 : 2);
  if (parameter.unit.isNotEmpty())
    label += " " + parameter.unit;
  return label;
}

namespace {

cydaw::base::Text controlKindKey(const ModuleControlKind kind) {
  switch (kind) {
  case ModuleControlKind::knob:
    return "knob";
  case ModuleControlKind::fader:
    return "fader";
  case ModuleControlKind::waveform:
    return "waveform";
  case ModuleControlKind::spectrum:
    return "spectrum";
  case ModuleControlKind::levelMeter:
    return "level-meter";
  case ModuleControlKind::text:
    return "text";
  case ModuleControlKind::inputPort:
    return "input-port";
  case ModuleControlKind::outputPort:
    return "output-port";
  case ModuleControlKind::toggle:
    return "toggle";
  case ModuleControlKind::envelope:
    return "envelope";
  case ModuleControlKind::group:
    return "group";
  case ModuleControlKind::selector:
    return "selector";
  case ModuleControlKind::xyPad:
    return "xy-pad";
  case ModuleControlKind::valueDisplay:
    return "value-display";
  case ModuleControlKind::filterResponse:
    return "filter-response";
  case ModuleControlKind::dynamicsResponse:
    return "dynamics-response";
  case ModuleControlKind::eqCurve:
    return "eq-curve";
  case ModuleControlKind::wavetable:
    return "wavetable";
  case ModuleControlKind::nativePanel:
    return "native-panel";
  case ModuleControlKind::custom:
    return "custom";
  }
  return "knob";
}

ModuleControlKind controlKindFromKey(const cydaw::base::Text &key) {
  if (key == "fader")
    return ModuleControlKind::fader;
  if (key == "waveform")
    return ModuleControlKind::waveform;
  if (key == "spectrum")
    return ModuleControlKind::spectrum;
  if (key == "level-meter")
    return ModuleControlKind::levelMeter;
  if (key == "text")
    return ModuleControlKind::text;
  if (key == "input-port")
    return ModuleControlKind::inputPort;
  if (key == "output-port")
    return ModuleControlKind::outputPort;
  if (key == "toggle")
    return ModuleControlKind::toggle;
  if (key == "envelope")
    return ModuleControlKind::envelope;
  if (key == "group")
    return ModuleControlKind::group;
  if (key == "selector")
    return ModuleControlKind::selector;
  if (key == "xy-pad")
    return ModuleControlKind::xyPad;
  if (key == "value-display")
    return ModuleControlKind::valueDisplay;
  if (key == "filter-response")
    return ModuleControlKind::filterResponse;
  if (key == "dynamics-response")
    return ModuleControlKind::dynamicsResponse;
  if (key == "eq-curve")
    return ModuleControlKind::eqCurve;
  if (key == "wavetable")
    return ModuleControlKind::wavetable;
  if (key == "native-panel")
    return ModuleControlKind::nativePanel;
  if (key == "custom")
    return ModuleControlKind::custom;
  return ModuleControlKind::knob;
}

} // namespace

bool isParameterControl(const ModuleControlKind kind) {
  return kind == ModuleControlKind::knob || kind == ModuleControlKind::fader ||
         kind == ModuleControlKind::toggle ||
         kind == ModuleControlKind::selector ||
         kind == ModuleControlKind::xyPad ||
         kind == ModuleControlKind::envelope ||
         kind == ModuleControlKind::valueDisplay ||
         kind == ModuleControlKind::filterResponse ||
         kind == ModuleControlKind::dynamicsResponse ||
         kind == ModuleControlKind::eqCurve ||
         kind == ModuleControlKind::wavetable;
}

bool isAutomaticDeviceParameterControl(const ModuleControlKind kind) {
  return isParameterControl(kind) && kind != ModuleControlKind::xyPad &&
         kind != ModuleControlKind::envelope &&
         kind != ModuleControlKind::valueDisplay &&
         kind != ModuleControlKind::filterResponse &&
         kind != ModuleControlKind::dynamicsResponse &&
         kind != ModuleControlKind::eqCurve &&
         kind != ModuleControlKind::wavetable;
}

bool isPortControl(const ModuleControlKind kind) {
  return kind == ModuleControlKind::inputPort ||
         kind == ModuleControlKind::outputPort;
}

std::vector<ModuleInterfaceControl> &
moduleInterfaceControls(ModuleInterfaceState &state,
                        const ModuleInterfaceLayoutMode mode) noexcept {
  return mode == ModuleInterfaceLayoutMode::device ? state.deviceControls
                                                   : state.controls;
}

const std::vector<ModuleInterfaceControl> &
moduleInterfaceControls(const ModuleInterfaceState &state,
                        const ModuleInterfaceLayoutMode mode) noexcept {
  return mode == ModuleInterfaceLayoutMode::device ? state.deviceControls
                                                   : state.controls;
}

void rebuildAutomaticDeviceControls(ModuleInterfaceState &state) {
  if (state.deviceDesigned)
    return;
  auto previous = std::move(state.deviceControls);
  state.deviceControls.clear();
  const auto exposedCount = static_cast<std::size_t>(std::count_if(
      state.parameters.begin(), state.parameters.end(),
      [](const auto &parameter) { return parameter.showInDevice; }));
  if (exposedCount == 0)
    return;

  const auto columns = std::min<std::size_t>(6, exposedCount);
  const auto rows = (exposedCount + columns - 1) / columns;
  const auto cellWidth = 1.0f / static_cast<float>(columns);
  const auto cellHeight = 1.0f / static_cast<float>(rows);
  std::size_t visibleIndex{};
  for (const auto &parameter : state.parameters) {
    if (!parameter.showInDevice)
      continue;
    const auto existing = std::find_if(
        previous.begin(), previous.end(), [&parameter](const auto &control) {
          return isAutomaticDeviceParameterControl(control.kind) &&
                 control.parameterBinding == parameter.key;
        });
    ModuleInterfaceControl control;
    control.id =
        existing != previous.end() ? existing->id : state.nextControlId++;
    const auto authored =
        std::find_if(state.controls.begin(), state.controls.end(),
                     [&parameter](const auto &control) {
                       return isAutomaticDeviceParameterControl(control.kind) &&
                              control.parameterBinding == parameter.key;
                     });
    control.kind = existing != previous.end() &&
                           isAutomaticDeviceParameterControl(existing->kind)
                       ? existing->kind
                   : authored != state.controls.end() &&
                           (authored->kind == ModuleControlKind::toggle ||
                            authored->kind == ModuleControlKind::selector)
                       ? authored->kind
                       : ModuleControlKind::knob;
    control.label = parameter.label;
    control.parameterBinding = parameter.key;
    const auto column = visibleIndex % columns;
    const auto row = visibleIndex / columns;
    control.x = static_cast<float>(column) * cellWidth + 0.025f;
    const auto verticalPadding = std::min(0.04f, cellHeight * 0.12f);
    control.y = static_cast<float>(row) * cellHeight + verticalPadding;
    control.width = std::max(0.08f, cellWidth - 0.05f);
    control.height = cellHeight - 2.0f * verticalPadding;
    state.deviceControls.push_back(std::move(control));
    ++visibleIndex;
  }
}

cydaw::base::Value serialiseModuleInterface(const ModuleInterfaceState &state) {
  auto root = cydaw::base::Object::Ptr(new cydaw::base::Object());
  root->setProperty(jsonKeys::schema,
                    std::max(18, static_cast<int>(state.schema)));
  root->setProperty(jsonKeys::nextControlId,
                    static_cast<cydaw::base::int64>(state.nextControlId));
  root->setProperty(jsonKeys::snapToGrid, state.snapToGrid);
  root->setProperty(jsonKeys::gridColumns, static_cast<int>(state.gridColumns));
  root->setProperty(jsonKeys::gridRows, static_cast<int>(state.gridRows));
  if (state.windowWidth != 0 && state.windowHeight != 0) {
    root->setProperty(jsonKeys::windowWidth,
                      static_cast<int>(state.windowWidth));
    root->setProperty(jsonKeys::windowHeight,
                      static_cast<int>(state.windowHeight));
  }
  if (state.designed)
    root->setProperty(jsonKeys::designed, true);
  root->setProperty("deviceDesigned", state.deviceDesigned);
  if (state.windowDesign.isObject())
    root->setProperty("windowDesign", state.windowDesign.clone());
  if (state.deviceDesign.isObject())
    root->setProperty("deviceDesign", state.deviceDesign.clone());
  root->setProperty("deviceWidth", static_cast<int>(state.deviceWidth));
  root->setProperty("deviceHeight", static_cast<int>(state.deviceHeight));

  cydaw::base::Array<cydaw::base::Value> parameters;
  for (const auto &parameter : state.parameters) {
    // Every field the object below is built from, and nothing else: a
    // field left out here would let a stale object through, and the
    // with-and-without-cache comparison in the suite is what catches it.
    auto signature = std::uint64_t{1469598103934665603ull};
    const auto mix = [&signature](const std::uint64_t bits) {
      signature ^= bits;
      signature *= 1099511628211ull;
    };
    const auto mixString = [&mix](const cydaw::base::Text &text) {
      mix(static_cast<std::uint64_t>(text.hashCode64()));
      mix(static_cast<std::uint64_t>(text.length()));
    };
    const auto mixDouble = [&mix](const double value) {
      mix(std::bit_cast<std::uint64_t>(value));
    };
    mixString(parameter.key);
    mixString(parameter.label);
    mixString(parameter.unit);
    mixDouble(parameter.minimum);
    mixDouble(parameter.maximum);
    mixDouble(parameter.value);
    mixDouble(parameter.defaultValue);
    mix(parameter.showInDevice);
    mix(parameter.hostParameterId);
    mix(parameter.publishToHost);
    mix(parameter.automatable);
    mix(parameter.modulatable);
    mixString(parameter.description);
    if (!parameter.serialised.isVoid() &&
        parameter.serialisedSignature == signature) {
      parameters.add(parameter.serialised);
      continue;
    }
    auto value = cydaw::base::Object::Ptr(new cydaw::base::Object());
    value->setProperty(jsonKeys::key, parameter.key);
    value->setProperty(jsonKeys::label, parameter.label);
    value->setProperty(jsonKeys::unit, parameter.unit);
    value->setProperty(jsonKeys::minimum, parameter.minimum);
    value->setProperty(jsonKeys::maximum, parameter.maximum);
    value->setProperty(jsonKeys::value, parameter.value);
    value->setProperty(jsonKeys::defaultValue,
                       moduleParameterDefaultValue(parameter));
    value->setProperty(jsonKeys::showInDevice, parameter.showInDevice);
    value->setProperty("hostParameterId",
                       static_cast<cydaw::base::int64>(
                           parameter.hostParameterId != 0
                               ? parameter.hostParameterId
                               : stableModuleParameterId(parameter.key)));
    value->setProperty(jsonKeys::publishToHost, parameter.publishToHost);
    value->setProperty(jsonKeys::automatable, parameter.automatable);
    value->setProperty(jsonKeys::modulatable, parameter.modulatable);
    value->setProperty(jsonKeys::description, parameter.description);
    parameter.serialised = cydaw::base::Value(value.get());
    parameter.serialisedSignature = signature;
    parameters.add(parameter.serialised);
  }
  root->setProperty(jsonKeys::parameters, parameters);

  const auto serialiseControls = [](const auto &source) {
    cydaw::base::Array<cydaw::base::Value> controls;
    for (const auto &control : source) {
      auto signature = std::uint64_t{1469598103934665603ull};
      const auto mix = [&signature](const std::uint64_t bits) {
        signature ^= bits;
        signature *= 1099511628211ull;
      };
      const auto mixString = [&mix](const cydaw::base::Text &text) {
        mix(static_cast<std::uint64_t>(text.hashCode64()));
        mix(static_cast<std::uint64_t>(text.length()));
      };
      const auto mixFloat = [&mix](const float value) {
        mix(std::bit_cast<std::uint32_t>(value));
      };
      mix(control.id);
      mix(static_cast<std::uint64_t>(control.kind));
      mixString(control.label);
      mixString(control.parameterBinding);
      mixString(control.secondaryParameterBinding);
      mixString(control.tertiaryParameterBinding);
      mixString(control.quaternaryParameterBinding);
      mix(control.extraParameterBindings.size());
      for (const auto &binding : control.extraParameterBindings)
        mixString(binding);
      mixFloat(control.x);
      mixFloat(control.y);
      mixFloat(control.width);
      mixFloat(control.height);
      mix(control.portKind);
      mix(control.portChannels);
      if (control.design.isObject())
        mixString(cydaw::base::JSON::toString(control.design, false));
      if (!control.serialised.isVoid() &&
          control.serialisedSignature == signature) {
        controls.add(control.serialised);
        continue;
      }
      auto value = cydaw::base::Object::Ptr(new cydaw::base::Object());
      value->setProperty(jsonKeys::id,
                         static_cast<cydaw::base::int64>(control.id));
      value->setProperty(jsonKeys::kind, controlKindKey(control.kind));
      value->setProperty(jsonKeys::label, control.label);
      if (control.design.isObject())
        value->setProperty("design", control.design);
      value->setProperty(jsonKeys::binding, control.parameterBinding);
      value->setProperty(jsonKeys::secondaryBinding,
                         control.secondaryParameterBinding);
      value->setProperty(jsonKeys::tertiaryBinding,
                         control.tertiaryParameterBinding);
      value->setProperty(jsonKeys::quaternaryBinding,
                         control.quaternaryParameterBinding);
      if (!control.extraParameterBindings.empty()) {
        cydaw::base::Array<cydaw::base::Value> extras;
        for (const auto &binding : control.extraParameterBindings)
          extras.add(binding);
        value->setProperty(jsonKeys::extraBindings, extras);
      }
      value->setProperty(jsonKeys::x, control.x);
      value->setProperty(jsonKeys::y, control.y);
      value->setProperty(jsonKeys::width, control.width);
      value->setProperty(jsonKeys::height, control.height);
      value->setProperty(jsonKeys::portKind,
                         static_cast<int>(control.portKind));
      value->setProperty(jsonKeys::portChannels,
                         static_cast<int>(control.portChannels));
      control.serialised = cydaw::base::Value(value.get());
      control.serialisedSignature = signature;
      controls.add(control.serialised);
    }
    return controls;
  };
  // Keep the historical key for schema-1 readers. In schema 2 it is the
  // unconstrained graph layout.
  root->setProperty(jsonKeys::controls, serialiseControls(state.controls));
  root->setProperty(jsonKeys::deviceControls,
                    serialiseControls(state.deviceControls));
  return cydaw::base::Value(root.get());
}

void dropSerialisedPresentation(ModuleInterfaceState &state) {
  for (auto &parameter : state.parameters)
    parameter.serialised = cydaw::base::Value();
  for (auto *controls : {&state.controls, &state.deviceControls})
    for (auto &control : *controls)
      control.serialised = cydaw::base::Value();
}

ModuleInterfaceState restoreModuleInterface(const cydaw::base::Value &value) {
  ModuleInterfaceState state;
  const auto *root = value.getDynamicObject();
  if (root == nullptr)
    return state;

  const auto sourceSchema = static_cast<std::uint32_t>(
      std::max(1, static_cast<int>(root->getProperty(jsonKeys::schema))));
  state.schema = 17;
  state.snapToGrid = sourceSchema < 6 ||
                     !root->hasProperty(jsonKeys::snapToGrid) ||
                     static_cast<bool>(root->getProperty(jsonKeys::snapToGrid));
  state.gridColumns = static_cast<std::uint8_t>(cydaw::base::jlimit(
      4, 64,
      root->hasProperty(jsonKeys::gridColumns)
          ? static_cast<int>(root->getProperty(jsonKeys::gridColumns))
          : 24));
  state.gridRows = static_cast<std::uint8_t>(cydaw::base::jlimit(
      4, 64,
      root->hasProperty(jsonKeys::gridRows)
          ? static_cast<int>(root->getProperty(jsonKeys::gridRows))
          : 16));
  if (root->hasProperty(jsonKeys::windowWidth) &&
      root->hasProperty(jsonKeys::windowHeight)) {
    state.windowWidth = static_cast<std::uint16_t>(cydaw::base::jlimit(
        240, 4'096,
        static_cast<int>(root->getProperty(jsonKeys::windowWidth))));
    state.windowHeight = static_cast<std::uint16_t>(cydaw::base::jlimit(
        160, 2'160,
        static_cast<int>(root->getProperty(jsonKeys::windowHeight))));
  }
  state.designed = static_cast<bool>(root->getProperty(jsonKeys::designed));
  state.deviceDesigned = static_cast<bool>(root->getProperty("deviceDesigned"));
  if (root->getProperty("windowDesign").isObject())
    state.windowDesign = root->getProperty("windowDesign").clone();
  if (root->getProperty("deviceDesign").isObject())
    state.deviceDesign = root->getProperty("deviceDesign").clone();
  state.deviceWidth = static_cast<std::uint16_t>(cydaw::base::jlimit(
      120, 4096,
      static_cast<int>((root->hasProperty("deviceWidth")
                            ? root->getProperty("deviceWidth")
                            : cydaw::base::Value(480)))));
  state.deviceHeight = static_cast<std::uint16_t>(cydaw::base::jlimit(
      80, 2160,
      static_cast<int>((root->hasProperty("deviceHeight")
                            ? root->getProperty("deviceHeight")
                            : cydaw::base::Value(240)))));
  std::set<cydaw::base::Text> parameterKeys;
  if (const auto *parameters =
          root->getProperty(jsonKeys::parameters).getArray()) {
    for (const auto &parameterValue : *parameters) {
      const auto *object = parameterValue.getDynamicObject();
      if (object == nullptr)
        continue;
      ModulePublishedParameter parameter;
      parameter.key = object->getProperty(jsonKeys::key).toString();
      parameter.label = object->getProperty(jsonKeys::label).toString();
      parameter.unit = object->getProperty(jsonKeys::unit).toString();
      parameter.minimum =
          static_cast<double>(object->getProperty(jsonKeys::minimum));
      parameter.maximum =
          static_cast<double>(object->getProperty(jsonKeys::maximum));
      parameter.value =
          static_cast<double>(object->getProperty(jsonKeys::value));
      parameter.defaultValue =
          object->hasProperty(jsonKeys::defaultValue)
              ? static_cast<double>(object->getProperty(jsonKeys::defaultValue))
              : parameter.value;
      parameter.showInDevice =
          !object->hasProperty(jsonKeys::showInDevice) ||
          static_cast<bool>(object->getProperty(jsonKeys::showInDevice));
      parameter.hostParameterId =
          object->hasProperty(jsonKeys::hostParameterId)
              ? static_cast<std::uint32_t>(static_cast<cydaw::base::int64>(
                    object->getProperty(jsonKeys::hostParameterId)))
              : stableModuleParameterId(parameter.key);
      parameter.publishToHost =
          !object->hasProperty(jsonKeys::publishToHost) ||
          static_cast<bool>(object->getProperty(jsonKeys::publishToHost));
      parameter.automatable =
          !object->hasProperty(jsonKeys::automatable) ||
          static_cast<bool>(object->getProperty(jsonKeys::automatable));
      parameter.modulatable =
          !object->hasProperty(jsonKeys::modulatable) ||
          static_cast<bool>(object->getProperty(jsonKeys::modulatable));
      parameter.description =
          object->getProperty(jsonKeys::description).toString();
      if (parameter.key.isEmpty() || parameterKeys.contains(parameter.key) ||
          parameter.minimum > parameter.maximum)
        continue;
      parameter.value = cydaw::base::jlimit(parameter.minimum,
                                            parameter.maximum, parameter.value);
      parameter.defaultValue = cydaw::base::jlimit(
          parameter.minimum, parameter.maximum, parameter.defaultValue);
      parameterKeys.insert(parameter.key);
      state.parameters.push_back(std::move(parameter));
    }
  }

  std::set<std::uint64_t> controlIds;
  std::uint64_t highestId{};
  const auto restoreControls = [&parameterKeys, &controlIds, &highestId,
                                sourceSchema](
                                   const cydaw::base::Value &value,
                                   std::vector<ModuleInterfaceControl>
                                       &destination) {
    if (const auto *controls = value.getArray()) {
      for (const auto &controlValue : *controls) {
        const auto *object = controlValue.getDynamicObject();
        if (object == nullptr)
          continue;
        ModuleInterfaceControl control;
        control.id = static_cast<std::uint64_t>(
            static_cast<cydaw::base::int64>(object->getProperty(jsonKeys::id)));
        if (control.id == 0 || controlIds.contains(control.id))
          continue;
        control.kind =
            controlKindFromKey(object->getProperty(jsonKeys::kind).toString());
        // A two-axis control did not exist before schema 11. If an asset
        // carries an inconsistent old schema marker, preserve its primary
        // parameter as a usable knob instead of restoring an invalid pad.
        if (sourceSchema < 11 && control.kind == ModuleControlKind::xyPad)
          control.kind = ModuleControlKind::knob;
        if (sourceSchema < 14 &&
            control.kind == ModuleControlKind::filterResponse)
          control.kind = ModuleControlKind::knob;
        if (sourceSchema < 15 &&
            control.kind == ModuleControlKind::dynamicsResponse)
          control.kind = ModuleControlKind::knob;
        if (sourceSchema < 16 && control.kind == ModuleControlKind::eqCurve)
          control.kind = ModuleControlKind::knob;
        control.label = object->getProperty(jsonKeys::label).toString();
        control.design = object->getProperty("design").clone();
        control.parameterBinding =
            object->getProperty(jsonKeys::binding).toString();
        control.secondaryParameterBinding =
            sourceSchema >= 11
                ? object->getProperty(jsonKeys::secondaryBinding).toString()
                : cydaw::base::Text{};
        control.tertiaryParameterBinding =
            sourceSchema >= 13
                ? object->getProperty(jsonKeys::tertiaryBinding).toString()
                : cydaw::base::Text{};
        control.quaternaryParameterBinding =
            sourceSchema >= 13
                ? object->getProperty(jsonKeys::quaternaryBinding).toString()
                : cydaw::base::Text{};
        if (sourceSchema >= 16)
          if (const auto *extras =
                  object->getProperty(jsonKeys::extraBindings).getArray())
            for (const auto &binding : *extras)
              control.extraParameterBindings.push_back(binding.toString());
        control.x = cydaw::base::jlimit(0.0f, 1.0f,
                                        static_cast<float>(static_cast<double>(
                                            object->getProperty(jsonKeys::x))));
        control.y = cydaw::base::jlimit(0.0f, 1.0f,
                                        static_cast<float>(static_cast<double>(
                                            object->getProperty(jsonKeys::y))));
        // These floors have to admit everything layoutPublishedInterface
        // can generate. They were the taller of the two, so a dense knob
        // grid came back from a reload half a row taller than it was
        // saved, overlapping the row beneath it.
        control.width =
            cydaw::base::jlimit(0.04f, 1.0f,
                                static_cast<float>(static_cast<double>(
                                    object->getProperty(jsonKeys::width))));
        control.height =
            cydaw::base::jlimit(0.05f, 1.0f,
                                static_cast<float>(static_cast<double>(
                                    object->getProperty(jsonKeys::height))));
        control.portKind = static_cast<std::uint8_t>(cydaw::base::jlimit(
            0, 3, static_cast<int>(object->getProperty(jsonKeys::portKind))));
        const auto defaultChannels = control.portKind == 0 ? 2 : 1;
        control.portChannels = static_cast<std::uint8_t>(cydaw::base::jlimit(
            1, 8,
            sourceSchema >= 7 && object->hasProperty(jsonKeys::portChannels)
                ? static_cast<int>(object->getProperty(jsonKeys::portChannels))
                : defaultChannels));
        // Before schema 8, the field only described Graph ports. Level
        // meters were always painted as stereo even though their stored
        // aggregate default was one channel.
        if (sourceSchema < 8 && control.kind == ModuleControlKind::levelMeter)
          control.portChannels = 2;
        control.x = std::min(control.x, 1.0f - control.width);
        control.y = std::min(control.y, 1.0f - control.height);
        if (isParameterControl(control.kind) &&
            !parameterKeys.contains(control.parameterBinding))
          control.parameterBinding.clear();
        if ((control.kind == ModuleControlKind::xyPad ||
             control.kind == ModuleControlKind::filterResponse ||
             control.kind == ModuleControlKind::dynamicsResponse) &&
            !parameterKeys.contains(control.secondaryParameterBinding))
          control.secondaryParameterBinding.clear();
        if (control.kind == ModuleControlKind::eqCurve) {
          // Three bindings per band, all distinct and all published.
          // Anything else is not a curve, so it falls back to a plot.
          const auto &extras = control.extraParameterBindings;
          const std::set<cydaw::base::Text> unique(extras.begin(),
                                                   extras.end());
          const auto valid =
              !extras.empty() && extras.size() % 3 == 0 &&
              unique.size() == extras.size() &&
              std::all_of(extras.begin(), extras.end(),
                          [&parameterKeys](const auto &binding) {
                            return parameterKeys.contains(binding);
                          });
          if (!valid)
            control.kind = ModuleControlKind::spectrum;
        }
        if (control.kind == ModuleControlKind::envelope) {
          const auto valid =
              sourceSchema >= 13 &&
              parameterKeys.contains(control.parameterBinding) &&
              parameterKeys.contains(control.secondaryParameterBinding) &&
              parameterKeys.contains(control.tertiaryParameterBinding) &&
              parameterKeys.contains(control.quaternaryParameterBinding);
          std::set<cydaw::base::Text> bindings{
              control.parameterBinding, control.secondaryParameterBinding,
              control.tertiaryParameterBinding,
              control.quaternaryParameterBinding};
          if (!valid || bindings.size() != 4)
            control.kind = ModuleControlKind::waveform;
        } else if (!isParameterControl(control.kind) &&
                   control.kind != ModuleControlKind::custom &&
                   !isPortControl(control.kind))
          control.parameterBinding.clear();
        controlIds.insert(control.id);
        highestId = std::max(highestId, control.id);
        destination.push_back(std::move(control));
      }
    }
  };
  restoreControls(root->getProperty(jsonKeys::controls), state.controls);
  if (sourceSchema >= 2)
    restoreControls(root->getProperty(jsonKeys::deviceControls),
                    state.deviceControls);
  if (sourceSchema < 2 && state.deviceControls.empty()) {
    state.deviceControls.reserve(state.controls.size());
    for (const auto &graphControl : state.controls) {
      auto deviceControl = graphControl;
      deviceControl.id = ++highestId;
      deviceControl.x = cydaw::base::jlimit(0.0f, 1.0f - deviceControl.width,
                                            deviceControl.x);
      deviceControl.y = cydaw::base::jlimit(0.0f, 1.0f - deviceControl.height,
                                            deviceControl.y);
      controlIds.insert(deviceControl.id);
      state.deviceControls.push_back(std::move(deviceControl));
    }
  }
  state.nextControlId = std::max<std::uint64_t>(
      highestId + 1, static_cast<std::uint64_t>(static_cast<cydaw::base::int64>(
                         root->getProperty(jsonKeys::nextControlId))));
  if (sourceSchema < 3) {
    for (auto &parameter : state.parameters)
      parameter.showInDevice =
          std::any_of(state.deviceControls.begin(), state.deviceControls.end(),
                      [&parameter](const auto &control) {
                        return isParameterControl(control.kind) &&
                               control.parameterBinding == parameter.key;
                      });
  }
  rebuildAutomaticDeviceControls(state);
  return state;
}

cydaw::base::Result
validateModuleInterfaceDocument(const cydaw::base::Value &document,
                                const ModuleInterfaceState &parameters) {
  const auto fail = [](const cydaw::base::Text &reason) {
    return cydaw::base::Result::fail(reason);
  };
  if (!document.isObject())
    return fail("Interface document must be a JSON object");
  for (const auto *surface : {"windowDesign", "deviceDesign"}) {
    const auto design = document.getProperty(surface, {});
    if (!design.isVoid() && !design.isObject())
      return fail(cydaw::base::Text(surface) + " must be an object");
    for (const auto *key : {"background", "accent", "text"})
      if (design.hasProperty(key)) {
        const auto color = design.getProperty(key, {}).toString();
        if (color.length() != 8 ||
            !color.containsOnly("0123456789abcdefABCDEF"))
          return fail("Surface colors must be 8-digit ARGB hex strings");
      }
  }
  const auto bindingExists = [&](const cydaw::base::Text &key) {
    return std::ranges::any_of(parameters.parameters,
                               [&](const auto &p) { return p.key == key; });
  };
  // Unique across both layouts, as the interface keeps them (restoring
  // skips an id seen in the other layout: a device control would be lost
  // without a word).
  std::set<cydaw::base::int64> ids;
  for (const auto *layout : {"controls", "deviceControls"}) {
    const auto value = document.getProperty(layout, {});
    const auto *controls = value.getArray();
    if (!controls) {
      if (cydaw::base::Text(layout) == "controls")
        return fail("controls must be an array");
      else
        continue;
    }
    if (controls->size() > 4096)
      return fail("An interface supports up to 4096 controls");
    for (const auto &c : *controls) {
      if (!c.isObject())
        return fail("A control must be a JSON object");
      const auto id = static_cast<cydaw::base::int64>(c.getProperty("id", 0));
      if (id <= 0 || !ids.insert(id).second)
        return fail(
            "Control IDs must be positive and unique across both layouts");
      const auto kind = c.getProperty("kind", {}).toString();
      bool known = false;
      for (int i = 0; i <= static_cast<int>(ModuleControlKind::custom); ++i)
        if (kind == controlKindKey(static_cast<ModuleControlKind>(i)))
          known = true;
      if (!known)
        return fail("Unknown control kind: " + kind);
      double coordinates[4]{};
      int index = 0;
      for (const auto *key : {"x", "y", "width", "height"}) {
        const auto v = c.getProperty(key, {});
        if (!(v.isInt() || v.isInt64() || v.isDouble()))
          return fail("Control geometry must be numeric: " +
                      cydaw::base::Text(key));
        const auto number = static_cast<double>(v);
        if (!std::isfinite(number) || number < 0 || number > 1 ||
            (index >= 2 && number <= 0))
          return fail("Control geometry must fit inside 0..1");
        coordinates[index++] = number;
      }
      if (coordinates[0] + coordinates[2] > 1.00001 ||
          coordinates[1] + coordinates[3] > 1.00001)
        return fail("A control extends beyond the canvas");
      if (kind != "input-port" && kind != "output-port") {
        for (const auto *key : {"binding", "secondaryBinding",
                                "tertiaryBinding", "quaternaryBinding"}) {
          const auto binding = c.getProperty(key, {}).toString();
          if (binding.isNotEmpty() && !bindingExists(binding))
            return fail("Unknown parameter: " + binding);
        }
        if (const auto *list = c.getProperty("extraBindings", {}).getArray())
          for (const auto &binding : *list)
            if (!bindingExists(binding.toString()))
              return fail("Unknown extra parameter: " + binding.toString());
      }
      const auto design = c.getProperty("design", {});
      if (!design.isVoid() && !design.isObject())
        return fail("design must be a JSON object");
      for (const auto *key : {"fontScale", "rounding"})
        if (design.hasProperty(key)) {
          const auto v = static_cast<double>(design.getProperty(key, {}));
          if (!std::isfinite(v) ||
              (cydaw::base::Text(key) == "fontScale" ? (v < .5 || v > 3)
                                                     : (v < 0 || v > 30)))
            return fail("Invalid style value: " + cydaw::base::Text(key));
        }
      if (kind == "native-panel" && design.hasProperty("instrumentType")) {
        const auto keys = design.getProperty("instrumentKeys", {});
        if (!keys.isArray() ||
            keys.size() != c.getProperty("extraBindings", {}).size())
          return fail("Instrument keys must match the panel bindings");
        std::set<cydaw::base::Text> seen;
        for (const auto &key : *keys.getArray())
          if (!key.isString() || key.toString().isEmpty() ||
              !seen.insert(key.toString()).second)
            return fail("Instrument keys must be nonempty and unique");
        if (design.getProperty("instrumentPanel", {}).toString().isEmpty())
          return fail("Instrument panel ID is required");
      }
      if (kind == "native-panel") {
        const auto pane = static_cast<int>(design.getProperty("pane", 0));
        if (pane < 0 || pane > 128)
          return fail("Unknown native panel kind");
        const auto count = c.getProperty("extraBindings", {}).size();
        if (const auto *indices = design.getProperty("indices", {}).getArray())
          for (const auto &i : *indices)
            if (static_cast<int>(i) < -1 || static_cast<int>(i) >= count)
              return fail("Native panel index has no parameter binding");
      }
      if (design.getProperty("customSource", {})
              .toString()
              .getNumBytesAsUTF8() > 128 * 1024)
        return fail("Custom component source exceeds 128 KiB");
    }
  }
  auto restored = restoreModuleInterface(document);
  restored.parameters = parameters.parameters;
  return validateModuleInterface(restored);
}

cydaw::base::Result validateModuleInterface(const ModuleInterfaceState &state) {
  if (state.gridColumns < 4 || state.gridColumns > 64 || state.gridRows < 4 ||
      state.gridRows > 64)
    return cydaw::base::Result::fail(
        "Module interface grid dimensions are invalid");
  std::set<cydaw::base::Text> parameterKeys;
  std::set<std::uint32_t> hostParameterIds;
  for (const auto &parameter : state.parameters) {
    if (parameter.key.isEmpty() || parameterKeys.contains(parameter.key))
      return cydaw::base::Result::fail(
          "Published module parameters need unique stable keys");
    if (parameter.minimum > parameter.maximum ||
        parameter.value < parameter.minimum ||
        parameter.value > parameter.maximum)
      return cydaw::base::Result::fail(
          "Published module parameter range is invalid");
    const auto hostParameterId = parameter.hostParameterId != 0
                                     ? parameter.hostParameterId
                                     : stableModuleParameterId(parameter.key);
    if (hostParameterId == 0 ||
        (parameter.publishToHost &&
         !hostParameterIds.insert(hostParameterId).second))
      return cydaw::base::Result::fail(
          "Host-published module parameters need unique non-zero stable IDs");
    parameterKeys.insert(parameter.key);
  }
  std::set<std::uint64_t> controlIds;
  const auto validateControls =
      [&parameterKeys,
       &controlIds](const auto &controls) -> cydaw::base::Result {
    for (const auto &control : controls) {
      if (control.id == 0 || controlIds.contains(control.id))
        return cydaw::base::Result::fail(
            "Module interface controls need unique non-zero IDs");
      if (control.x < 0.0f || control.y < 0.0f || control.width <= 0.0f ||
          control.height <= 0.0f || control.x + control.width > 1.001f ||
          control.y + control.height > 1.001f)
        return cydaw::base::Result::fail(
            "Module interface control bounds are invalid");
      if (isParameterControl(control.kind) &&
          !parameterKeys.contains(control.parameterBinding))
        return cydaw::base::Result::fail(
            "Module interface control binding is missing");
      if ((control.kind == ModuleControlKind::xyPad ||
           control.kind == ModuleControlKind::filterResponse ||
           control.kind == ModuleControlKind::dynamicsResponse) &&
          (!parameterKeys.contains(control.secondaryParameterBinding) ||
           control.secondaryParameterBinding == control.parameterBinding))
        return cydaw::base::Result::fail(
            "Module interface two-parameter control needs two different "
            "bindings");
      if (control.kind == ModuleControlKind::eqCurve) {
        const auto &extras = control.extraParameterBindings;
        const std::set<cydaw::base::Text> unique(extras.begin(), extras.end());
        if (extras.empty() || extras.size() % 3 != 0 ||
            unique.size() != extras.size() ||
            !std::all_of(extras.begin(), extras.end(),
                         [&parameterKeys](const auto &binding) {
                           return parameterKeys.contains(binding);
                         }))
          return cydaw::base::Result::fail(
              "Module EQ curve needs three distinct bindings per band");
      }
      if (control.kind == ModuleControlKind::envelope) {
        const std::set<cydaw::base::Text> bindings{
            control.parameterBinding, control.secondaryParameterBinding,
            control.tertiaryParameterBinding,
            control.quaternaryParameterBinding};
        if (bindings.size() != 4 ||
            !parameterKeys.contains(control.secondaryParameterBinding) ||
            !parameterKeys.contains(control.tertiaryParameterBinding) ||
            !parameterKeys.contains(control.quaternaryParameterBinding))
          return cydaw::base::Result::fail(
              "Module envelope needs four different bindings");
      }
      if (isPortControl(control.kind) &&
          (control.portChannels < 1 || control.portChannels > 8 ||
           (control.portKind != 0 && control.portChannels != 1)))
        return cydaw::base::Result::fail(
            "Module interface port channel contract is invalid");
      if (control.kind == ModuleControlKind::levelMeter &&
          (control.portChannels < 1 || control.portChannels > 8))
        return cydaw::base::Result::fail(
            "Module interface meter channel format is invalid");
      controlIds.insert(control.id);
    }
    return cydaw::base::Result::ok();
  };
  if (const auto graphResult = validateControls(state.controls);
      graphResult.failed())
    return graphResult;
  if (const auto deviceResult = validateControls(state.deviceControls);
      deviceResult.failed())
    return deviceResult;
  return cydaw::base::Result::ok();
}

std::uint32_t
stableModuleParameterId(const cydaw::base::Text &parameterKey) noexcept {
  // FNV-1a is deliberately independent of UI order and platform. The high
  // bit stays clear for hosts that internally use signed parameter IDs.
  auto hash = std::uint32_t{2'166'136'261u};
  const auto utf8 = parameterKey.toRawUTF8();
  for (auto index = std::size_t{}; utf8[index] != 0; ++index) {
    hash ^= static_cast<std::uint8_t>(utf8[index]);
    hash *= 16'777'619u;
  }
  hash &= 0x7fffffffu;
  return hash != 0 ? hash : 1u;
}

} // namespace cydaw::app
