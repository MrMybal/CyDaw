#pragma once
#include <cstdint>
#include <cydaw/base/Value.h>
#include <limits>
#include <vector>
namespace cydaw::app {
enum class ModuleControlKind : std::uint8_t {
  knob,
  fader,
  waveform,
  spectrum,
  levelMeter,
  text,
  inputPort,
  outputPort,
  toggle,
  envelope,
  group,
  selector,
  xyPad,
  valueDisplay,
  filterResponse,
  dynamicsResponse,
  eqCurve,
  wavetable,
  nativePanel,
  custom
};

bool isParameterControl(ModuleControlKind);
bool isPortControl(ModuleControlKind);
enum class ModuleInterfaceLayoutMode : std::uint8_t { graph, device };

struct ModulePublishedParameter {
  cydaw::base::Text key;
  cydaw::base::Text label;
  cydaw::base::Text unit;
  double minimum{};
  double maximum{1.0};
  double value{};
  bool showInDevice{true};
  // Stable across layout changes and parameter reordering. A zero value is
  // migrated deterministically from `key` when an older Graph is loaded.
  std::uint32_t hostParameterId{};
  bool publishToHost{true};
  bool automatable{true};
  bool modulatable{true};
  cydaw::base::Text description;
  // Kept last to preserve source compatibility with schema-4 aggregate
  // initialisers. NaN means that an older graph has no authored reset value.
  double defaultValue{std::numeric_limits<double>::quiet_NaN()};
  // What this parameter last serialised to, and a signature of every field
  // that went into it. Rebuilding three hundred of these per serialise was
  // most of what a knob notch cost on a large patch. Validated, not
  // invalidated: no mutation site has to know.
  mutable cydaw::base::Value serialised;
  mutable std::uint64_t serialisedSignature{};
};

struct ModuleInterfaceControl {
  std::uint64_t id{};
  ModuleControlKind kind{ModuleControlKind::knob};
  cydaw::base::Text label;
  cydaw::base::Text parameterBinding;
  float x{};
  float y{};
  float width{0.22f};
  float height{0.32f};
  std::uint8_t portKind{};
  // Audio topology is part of a published port contract and also defines
  // how many lanes an authored level meter presents. Non-audio controls use
  // one event/value stream.
  std::uint8_t portChannels{1};
  // Used by two-dimensional controls such as an XY pad. Kept independent
  // from the primary binding so each axis retains a stable host parameter.
  cydaw::base::Text secondaryParameterBinding;
  // Multi-parameter controls append their extra stable bindings here. An
  // envelope maps primary/secondary/tertiary/quaternary to A/D/S/R.
  cydaw::base::Text tertiaryParameterBinding;
  cydaw::base::Text quaternaryParameterBinding;
  // Controls that address more parameters than the four named bindings can
  // hold list the rest here, in the order the control defines. An EQ curve
  // reads them as consecutive frequency / gain / Q triples, one per band.
  std::vector<cydaw::base::Text> extraParameterBindings;
  // Portable appearance, page/group membership and custom ImGui component
  // source.
  cydaw::base::Value design;
  // Same arrangement as ModulePublishedParameter, for the same reason.
  mutable cydaw::base::Value serialised;
  mutable std::uint64_t serialisedSignature{};
};

struct ModuleInterfaceState {
  std::uint32_t schema{15};
  std::uint64_t nextControlId{1};
  // Portable free-layout grid. Geometry remains normalised so reusable
  // interfaces keep the same composition at every window size and DPI.
  bool snapToGrid{true};
  std::uint8_t gridColumns{24};
  std::uint8_t gridRows{16};
  // The window a plugin made of this graph opens, in pixels; 0 for the
  // default. The controls keep their normalised places in it.
  std::uint16_t windowWidth{};
  std::uint16_t windowHeight{};
  // Laid out by the person (the workshop's Interface tab), rather than the
  // one made from the published settings: a plug-in made of the graph
  // opens on it.
  bool designed{};
  bool deviceDesigned{};
  // Per-surface appearance, independent of control-level overrides.
  cydaw::base::Value windowDesign;
  cydaw::base::Value deviceDesign;
  std::uint16_t deviceWidth{480};
  std::uint16_t deviceHeight{240};
  std::vector<ModulePublishedParameter> parameters;
  // Free-form controls rendered inside graph nodes and reusable sub-graphs.
  std::vector<ModuleInterfaceControl> controls;
  // Constrained wide layout rendered in the device-chain panel.
  std::vector<ModuleInterfaceControl> deviceControls;
};

// Drops every cached serialisation in a presentation, so a build can be
// made from scratch and compared with a cached one.
void dropSerialisedPresentation(ModuleInterfaceState &state);
[[nodiscard]] std::uint32_t
stableModuleParameterId(const cydaw::base::Text &parameterKey) noexcept;
[[nodiscard]] double
moduleParameterDefaultValue(const ModulePublishedParameter &parameter) noexcept;
[[nodiscard]] std::vector<double>
moduleSelectorValues(const ModulePublishedParameter &parameter);
[[nodiscard]] std::size_t
closestModuleSelectorValue(const std::vector<double> &values,
                           double value) noexcept;
[[nodiscard]] cydaw::base::Text
moduleSelectorValueLabel(const ModulePublishedParameter &parameter,
                         double value);

[[nodiscard]] std::vector<ModuleInterfaceControl> &
moduleInterfaceControls(ModuleInterfaceState &state,
                        ModuleInterfaceLayoutMode mode) noexcept;
[[nodiscard]] const std::vector<ModuleInterfaceControl> &
moduleInterfaceControls(const ModuleInterfaceState &state,
                        ModuleInterfaceLayoutMode mode) noexcept;
void rebuildAutomaticDeviceControls(ModuleInterfaceState &state);
[[nodiscard]] cydaw::base::Value
serialiseModuleInterface(const ModuleInterfaceState &state);
[[nodiscard]] ModuleInterfaceState
restoreModuleInterface(const cydaw::base::Value &value);
[[nodiscard]] cydaw::base::Result
validateModuleInterfaceDocument(const cydaw::base::Value &document,
                                const ModuleInterfaceState &parameters);
[[nodiscard]] cydaw::base::Result
validateModuleInterface(const ModuleInterfaceState &state);

} // namespace cydaw::app
