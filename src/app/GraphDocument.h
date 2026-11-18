#pragma once
#include <array>
#include <cydaw/base/File.h>
#include <cydaw/base/Geometry.h>
#include <cydaw/base/Utilities.h>
#include <cydaw/document/ModuleInterface.h>
#include <cydaw/engine/AudioAnalysis.h>
#include <cydaw/engine/AudioGraph.h>
#include <cydaw/engine/GraphDefinitionCompiler.h>
#include <cydaw/engine/NativeDspGraph.h>
#include <cydaw/engine/PolySynthGraph.h>
#include <cydaw/graph/GraphDefinition.h>
#include <cydaw/graph/GraphPluginExport.h>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <unordered_map>
#include <vector>
namespace cydaw::app {
// Application graph document shared by the ImGui editor, compilers and project
// persistence. No window or legacy widget ownership belongs to this model.
class GraphDocument final {
public:
  GraphDocument();
  void processPendingChanges();
  void setInstrumentIsMainInput(bool value) noexcept {
    instrumentIsMainInput_ = value;
  }
  void setLayoutSpace(base::Rectangle<float> space) { layoutSpace_ = space; }

  bool graphNodesOverlap() const;
  ~GraphDocument() = default;
  GraphDocument(const GraphDocument &) = delete;
  GraphDocument &operator=(const GraphDocument &) = delete;
  void refreshTranslations();

public:
  [[nodiscard]] cydaw::base::Text insertProfile() const {
    return insertProfile_;
  }
  [[nodiscard]] cydaw::base::Text effectParametersProfile() const {
    return effectParametersProfile_;
  }
  [[nodiscard]] cydaw::base::Text addProfile() const { return addProfile_; }
  [[nodiscard]] cydaw::base::Text placementProfile() const {
    return placementProfile_;
  }
  [[nodiscard]] cydaw::base::Text layoutChangeProfile() const {
    return layoutChangeProfile_;
  }
  [[nodiscard]] cydaw::base::Text polyCompileProfile() const {
    return polyCompileProfile_;
  }
  [[nodiscard]] cydaw::base::Text layoutChangeOrigins() const {
    return layoutChangeOrigins_;
  }
  [[nodiscard]] cydaw::base::TextList deviceNames(int limit) const;
  void invalidateSerialisedNodes();
  bool removeModulationForTest(const cydaw::base::Text &targetTypeKey,
                               const cydaw::base::Text &parameterSuffix);
  [[nodiscard]] cydaw::base::Text modulateFailureForTest() const {
    return modulateFailure_;
  }
  bool setNodeWavetable(const cydaw::base::Text &nodeTypeKey,
                        const cydaw::engine::WavetableShape &shape);
  bool setNodeSoundModel(std::uint64_t nodeId, const cydaw::base::Value &json,
                         cydaw::base::Text &error);
  [[nodiscard]] cydaw::base::Text
  nodeSoundModelDescription(std::uint64_t nodeId) const;
  [[nodiscard]] cydaw::engine::SoundModelRef
  nodeSoundModel(std::uint64_t nodeId) const;
  [[nodiscard]] cydaw::base::Value
  nodeSoundModelJson(std::uint64_t nodeId) const;
  [[nodiscard]] cydaw::base::Value voiceSoundModelJson() const;
  bool setNodeSampler(std::uint64_t nodeId, const cydaw::base::Value &json,
                      cydaw::base::Text &error);
  [[nodiscard]] cydaw::base::Value nodeSamplerJson(std::uint64_t nodeId) const;
  [[nodiscard]] cydaw::base::Value
  nodeInstrumentJson(std::uint64_t nodeId) const;
  bool setNodeInstrumentJson(std::uint64_t nodeId,
                             const cydaw::base::Value &state);
  [[nodiscard]] cydaw::engine::SamplerMapRef
  nodeSampler(std::uint64_t nodeId) const;
  bool setNodePrismTables(std::uint64_t nodeId, const cydaw::base::Value &json,
                          cydaw::base::Text &error);
  [[nodiscard]] cydaw::base::Value
  nodePrismTablesJson(std::uint64_t nodeId) const;
  [[nodiscard]] cydaw::engine::prism::TablesRef
  nodePrismTables(std::uint64_t nodeId) const;
  bool setNodeImpulse(std::uint64_t nodeId, const cydaw::base::Value &json,
                      cydaw::base::Text &error);
  [[nodiscard]] cydaw::base::Value nodeImpulseJson(std::uint64_t nodeId) const;
  [[nodiscard]] cydaw::engine::ImpulseResponseRef
  nodeImpulse(std::uint64_t nodeId) const;
  [[nodiscard]] cydaw::engine::WavetableShape
  nodeWavetable(const cydaw::base::Text &nodeTypeKey) const;
  struct RuntimeGraphExport {
    cydaw::engine::NativeDspGraph nativeGraph;
    std::vector<cydaw::graph::PublishedParameter> publishedParameters;
    std::vector<cydaw::engine::CompiledControlTelemetryTarget>
        controlTelemetryTargets;
    cydaw::engine::PolySynthGraphPlan instrumentPlan;
    cydaw::base::Result compilationResult{cydaw::base::Result::ok()};
  };
  [[nodiscard]] cydaw::base::Value serialise() const;
  [[nodiscard]] cydaw::engine::NativeDspGraph nativeDspGraph() const;
  [[nodiscard]] cydaw::engine::NativeDspGraph
  withCardBypasses(cydaw::engine::NativeDspGraph graph) const;
  [[nodiscard]] int exportRebuilds() const noexcept { return exportRebuilds_; }
  [[nodiscard]] cydaw::base::Text exportCacheProfile() const {
    return exportCacheProfile_;
  }
  [[nodiscard]] RuntimeGraphExport runtimeGraphExport() const;
  [[nodiscard]] cydaw::base::Result
  compilePolySynthGraph(cydaw::engine::PolySynthGraphPlan &destination) const;
  [[nodiscard]] ModuleInterfaceState graphInterfaceState() const;
  [[nodiscard]] bool containsNodeType(const cydaw::base::Text &typeKey) const;
  [[nodiscard]] std::vector<cydaw::graph::GraphDefinition>
  graphDefinitions() const;
  [[nodiscard]] cydaw::graph::ValidationResult validateGraphAsset() const;
  void initialiseEmptyMidiTrackGraph();
  void initialiseValueModulationGraph();
  [[nodiscard]] double evaluateValueOutput(double timeSeconds) const;
  [[nodiscard]] bool
  setGraphInterfaceParameter(const cydaw::base::Text &binding, double value);
  [[nodiscard]] bool
  setPublishedParameterExposure(const cydaw::base::Text &binding,
                                bool showInDevice, bool publishToHost);
  void restore(const cydaw::base::Value &state);
  void setLayoutChangedHandler(std::function<void()> handler);
  void setParametersChangedHandler(std::function<void()> handler);
  void setDspCompilationStatus(const cydaw::base::Result &result,
                               std::size_t nodeCount);
  void setAnalysisFrame(const cydaw::engine::AudioAnalysisFrame &frame);
  void
  setControlValues(const std::vector<std::pair<std::uint64_t, float>> &values);
  void clearAnalysis() noexcept;
  struct FactoryPresetSummary {
    cydaw::base::Text key;
    cydaw::base::Text translationKey;
    bool soundSource{};
  };
  [[nodiscard]] std::vector<FactoryPresetSummary>
  factoryPresetSummaries() const;
  struct DeviceCatalogueEntry {
    cydaw::base::Text key;
    cydaw::base::Text name;
    cydaw::base::Text kind; // "preset", "module" or "node"
    cydaw::base::Text category;
    cydaw::base::Text description;
    bool soundSource{};
    cydaw::base::Text
        input; // "audio", "midi", "control", "transport" or "none"
    cydaw::base::Text output;
    cydaw::base::Text categoryKey; // a node's catalogue category key
    // Its name in English, whatever the language (inside a module).
    cydaw::base::Text englishName;
  };
  [[nodiscard]] std::vector<DeviceCatalogueEntry> deviceCatalogue() const;
  bool removeNodeById(std::uint64_t id);
  bool modulateWithNewSource(const cydaw::base::Text &sourceTypeKey,
                             const cydaw::base::Text &targetTypeKey,
                             const cydaw::base::Text &parameterSuffix);
  bool removeNodeFromChain(std::uint64_t id);
  bool removeNodeFromChainByType(const cydaw::base::Text &typeKey);
  [[nodiscard]] std::uint64_t
  lastNodeIdOfType(const cydaw::base::Text &typeKey) const;
  [[nodiscard]] std::uint64_t addNodeForRemote(const cydaw::base::Text &typeKey,
                                               base::Point<float> position);
  bool connectNodesForRemote(std::uint64_t source, std::uint64_t destination,
                             std::uint64_t sourcePort,
                             std::uint64_t destinationPort);
  int disconnectNodesForRemote(std::uint64_t source, std::uint64_t destination);
  bool setNodePresentationForRemote(std::uint64_t id,
                                    const cydaw::base::Text &title,
                                    std::optional<base::Point<float>> position);
  [[nodiscard]] cydaw::base::Value
  describeNodeTypeForRemote(const cydaw::base::Text &typeKey) const;
  [[nodiscard]] cydaw::base::Value
  describeNodeForRemote(std::uint64_t id) const;
  [[nodiscard]] std::uint64_t
  addCommentForRemote(const cydaw::base::Text &title,
                      const cydaw::base::Text &description,
                      base::Point<float> position, base::Point<float> size);
  bool removeCommentForRemote(std::uint64_t id);
  [[nodiscard]] cydaw::base::Result
  insertFactoryDevice(const cydaw::base::Text &deviceKey);
  [[nodiscard]] cydaw::base::Result
  replaceInstrument(const cydaw::base::Text &presetKey);
  [[nodiscard]] cydaw::base::Result
  insertNode(const cydaw::base::Text &nodeTypeKey);
  [[nodiscard]] cydaw::base::Result
  insertNodeBeforeOutput(const cydaw::base::Text &insertedNodeTypeKey);
  [[nodiscard]] cydaw::base::Result
  insertNodeBeforeInstrument(const cydaw::base::Text &insertedNodeTypeKey);
  [[nodiscard]] cydaw::base::Result
  createEditableGraphModule(cydaw::graph::GraphRole role,
                            bool openEditor = true, int audioChannels = 2);
  [[nodiscard]] cydaw::base::Result
  createGraphVariant(const cydaw::base::Text &sourceModuleKey,
                     const cydaw::base::Text &name = {},
                     bool replaceSelectedInstance = true,
                     bool openEditor = true);
  [[nodiscard]] cydaw::base::Result
  saveUserGraphPreset(const cydaw::base::File &file,
                      const cydaw::base::Text &name) const;
  [[nodiscard]] cydaw::base::Result
  loadUserGraphPreset(const cydaw::base::File &file);
  [[nodiscard]] bool
  setGraphMemberMetadata(const cydaw::base::Text &key,
                         const cydaw::base::Text &displayName,
                         const cydaw::base::Text &description = {});
  [[nodiscard]] bool setGraphMemberType(const cydaw::base::Text &key,
                                        cydaw::engine::PortKind kind,
                                        int audioChannels = 2);
  void setGraphAssetScope(cydaw::graph::GraphScope scope);
  [[nodiscard]] cydaw::graph::GraphReference graphReference() const;
  [[nodiscard]] cydaw::graph::GraphRole graphRole() const noexcept;
  [[nodiscard]] cydaw::graph::PluginIdentity pluginExportIdentity() const;
  void setPluginExportIdentity(cydaw::graph::PluginIdentity identity);
  [[nodiscard]] cydaw::graph::PluginExportResult planVst3Export() const;
  [[nodiscard]] cydaw::base::Value createVst3RuntimeState() const;
  [[nodiscard]] cydaw::base::Result
  generateVst3Project(const cydaw::base::File &destination,
                      const cydaw::base::Text &format = "VST3") const;
  void setEffectParameters(float driveDb, float filterCutoffHz, bool enabled);
  struct EditorPort {
    std::uint64_t id{};
    cydaw::base::Text key;
    cydaw::base::Text name;
    int kind{}; // engine::PortKind: 0 audio, 1 MIDI, 2 control, 3 transport
    int type{}; // graph::PortType
    bool input{};
    int channels{1};
    bool multiple{};
    double defaultValue{};
    int exposure{-1};
    cydaw::base::Text category;
  };
  struct EditorParameter {
    cydaw::base::Text binding;
    cydaw::base::Text label;
    cydaw::base::Text unit;
    double minimum{};
    double maximum{1.0};
    double value{};
    double defaultValue{};
    bool showInDevice{true};
    bool publishToHost{true};
    cydaw::base::Text description;
    std::uint64_t inputPortId{};
  };
  struct EditorNode {
    std::uint64_t id{};
    cydaw::base::Text typeKey;
    cydaw::base::Text title; // what the card shows: its alias, else the type
    cydaw::base::Text typeTitle;
    cydaw::base::Text alias;
    cydaw::base::Text category;
    cydaw::base::Text description;
    base::Colour colour;
    base::Point<float> position; // canvas fractions, as stored
    bool collapsed{};
    bool bypassed{};
    bool protectedNode{};
    bool composite{};
    // It has a designed interface: a window to open, as a plugin's.
    bool hasInterface{};
    bool boundary{};
    bool invalid{};
    std::vector<EditorPort> ports;
    std::vector<EditorParameter> parameters;
  };
  struct EditorConnection {
    std::uint64_t source{};
    std::uint64_t destination{};
    std::uint64_t sourcePort{};
    std::uint64_t destinationPort{};
    int kind{};
    int channels{};
    int mode{}; // control onto a parameter: 0 set, 1 modulate
    float amount{0.5f};
    bool drivesParameter{}; // a control cable onto a parameter (not onto a
                            // control node)
  };
  struct EditorComment {
    std::uint64_t id{};
    cydaw::base::Text title;
    cydaw::base::Text description;
    base::Point<float> position;
    base::Point<float> size;
    base::Colour colour;
  };
  struct EditorNodeType {
    cydaw::base::Text key;
    cydaw::base::Text title;
    cydaw::base::Text category;
    cydaw::base::Text categoryKey; // the catalogue's, for a tree of its own
    cydaw::base::Text description;
    base::Colour colour;
    bool module{};
    cydaw::base::Text
        moduleCategory; // a graph's own category ("Effets / Délai et espace")
    bool hidden{};
    int inputKind{-1}; // -1: none
    int outputKind{-1};
    // Every kind its sockets take and give (engine::PortKind values).
    std::vector<int> inputKinds;
    std::vector<int> outputKinds;
  };
  [[nodiscard]] std::vector<EditorNode> editorNodes() const;
  [[nodiscard]] cydaw::base::Text editorNodeTypeKey(std::uint64_t id) const;
  [[nodiscard]] cydaw::base::Text editorNodeAlias(std::uint64_t id) const;
  bool editorSetPortExposure(std::uint64_t node, std::uint64_t port,
                             bool exposed);
  bool editorSetPortDetails(std::uint64_t node, std::uint64_t port,
                            const cydaw::base::Text &name,
                            const cydaw::base::Text &category);
  bool editorRemoveBoundary(std::uint64_t node);
  cydaw::base::Result editorPromoteValue(std::uint64_t node, std::uint64_t port,
                                         const cydaw::base::Text &binding,
                                         bool graphInput,
                                         std::uint64_t &created);
  [[nodiscard]] const cydaw::engine::AudioAnalysisFrame &
  editorAnalysis() const noexcept {
    return lastAnalysisFrame_;
  }
  [[nodiscard]] std::vector<std::pair<std::uint64_t, std::uint32_t>>
  audioInputBuses(std::uint64_t renderedInstrumentNodeId = 0) const;
  [[nodiscard]] std::vector<EditorConnection> editorConnections() const;
  [[nodiscard]] std::vector<EditorComment> editorComments() const;
  [[nodiscard]] std::vector<EditorNodeType> editorNodeTypes() const;
  [[nodiscard]] std::vector<EditorParameter> editorGraphParameters() const;
  [[nodiscard]] std::optional<float>
  editorControlValue(std::uint64_t nodeId) const;
  [[nodiscard]] cydaw::base::Text
  editorCanConnect(std::uint64_t source, std::uint64_t sourcePort,
                   std::uint64_t destination,
                   std::uint64_t destinationPort) const;
  [[nodiscard]] bool editorCanUndo() const noexcept;
  [[nodiscard]] std::uint64_t editorHistorySteps() const noexcept {
    return historySteps_;
  }
  [[nodiscard]] bool editorCanRedo() const noexcept;
  [[nodiscard]] bool editorLocked() const noexcept { return editingLocked_; }
  std::uint64_t editorAddNode(const cydaw::base::Text &typeKey,
                              base::Point<float> position);
  bool editorRemoveNodes(const std::vector<std::uint64_t> &ids);
  bool editorRemoveAudioInput(std::uint64_t id);
  struct EditorModuleInfo {
    cydaw::base::Text key;
    cydaw::base::Text name;
    cydaw::graph::GraphRole role{cydaw::graph::GraphRole::audioEffect};
    // A module of the factory library, shared by every graph using it.
    bool factory{};
    // The graph's own (made here, or a copy of a shared one).
    bool inlineModule{};
  };
  [[nodiscard]] std::optional<EditorModuleInfo>
  editorModuleOf(std::uint64_t nodeId) const;
  [[nodiscard]] cydaw::base::Value
  editorModuleGraphState(const cydaw::base::Text &key) const;
  void editorApplyModuleGraphState(const cydaw::base::Text &key,
                                   const cydaw::base::Value &state,
                                   bool structural);
  cydaw::base::Text editorMakeModuleOwn(std::uint64_t nodeId);
  std::uint64_t
  editorCreateModuleFromSelection(const std::vector<std::uint64_t> &ids,
                                  const cydaw::base::Text &name,
                                  cydaw::base::Text &error);
  std::uint64_t editorCreateEmptyDevice(cydaw::graph::GraphRole role);
  std::uint64_t editorInsertModuleFromState(const cydaw::base::Value &state,
                                            cydaw::graph::GraphRole role,
                                            const cydaw::base::Text &name);
  [[nodiscard]] cydaw::base::Text editorGraphName() const { return graphName_; }
  bool editorSetConnectionMode(std::uint64_t source, std::uint64_t sourcePort,
                               std::uint64_t destination,
                               std::uint64_t destinationPort, int mode,
                               float amount);
  [[nodiscard]] static cydaw::graph::PortId
  knobPortId(const cydaw::base::Text &classKey);
  struct ControlCableTarget {
    cydaw::base::Text binding;
    double minimum{};
    double maximum{1.0};
  };
  [[nodiscard]] cydaw::base::Text
  connectionProblemText(const cydaw::engine::GraphResult &result) const;
  std::uint64_t editorConnectThroughConverter(std::uint64_t source,
                                              std::uint64_t sourcePort,
                                              std::uint64_t destination,
                                              std::uint64_t destinationPort);
  bool spliceBeforeOutput(std::uint64_t instance);
  [[nodiscard]] int editorAudioChannels(std::uint64_t node, std::uint64_t port,
                                        bool input) const;
  void editorSetGraphName(const cydaw::base::Text &name);
  struct EditorGraphInfo {
    cydaw::base::Text name;
    cydaw::base::Text description;
    cydaw::base::Text author;
    cydaw::base::Text category;
  };
  [[nodiscard]] EditorGraphInfo editorGraphInfo() const;
  void editorSetGraphInfo(const EditorGraphInfo &info);
  struct EditorProgram {
    cydaw::base::Text name;
    std::vector<std::pair<cydaw::base::Text, double>> values;
  };
  [[nodiscard]] const std::vector<EditorProgram> &
  editorPrograms() const noexcept {
    return programs_;
  }
  void editorSetPrograms(std::vector<EditorProgram> programs);
  [[nodiscard]] EditorProgram
  editorCaptureProgram(const cydaw::base::Text &name,
                       bool defaults = false) const;
  bool editorApplyProgram(const EditorProgram &program);
  [[nodiscard]] ModuleInterfaceState editorInterface() const {
    return graphInterfaceState_;
  }
  [[nodiscard]] ModuleInterfaceState
  editorNodeInterface(std::uint64_t nodeId) const;
  cydaw::base::Result editorSetInterface(const ModuleInterfaceState &state,
                                         bool commit);
  [[nodiscard]] bool editorIsVoiceNode(std::uint64_t id) const;
  [[nodiscard]] std::set<std::uint64_t> editorVoiceNodes() const;
  [[nodiscard]] std::optional<cydaw::engine::PolyVoiceTarget>
  editorVoiceTargetFor(const cydaw::base::Text &binding) const;
  [[nodiscard]] std::optional<cydaw::engine::PolyVoiceParameter>
  editorVoiceParameterFor(const cydaw::base::Text &binding) const;
  [[nodiscard]] std::optional<std::pair<std::uint64_t, cydaw::base::Text>>
  editorNativeNodeFor(const cydaw::base::Text &binding) const;
  bool editorRemoveFromChain(std::uint64_t id);
  bool editorMoveInChain(std::uint64_t id, std::uint64_t before);
  void editorMoveNodes(
      const std::vector<std::pair<std::uint64_t, base::Point<float>>>
          &positions,
      bool commit);
  cydaw::base::Text editorConnect(std::uint64_t source,
                                  std::uint64_t sourcePort,
                                  std::uint64_t destination,
                                  std::uint64_t destinationPort);
  bool editorDisconnect(std::uint64_t source, std::uint64_t sourcePort,
                        std::uint64_t destination,
                        std::uint64_t destinationPort);
  bool editorSetParameter(const cydaw::base::Text &binding, double value);
  // Explicit multi-parameter preview. Only a committed gesture enters history.
  using ParameterValue = std::pair<cydaw::base::Text, double>;
  void editorBeginParameterGesture(std::span<const ParameterValue> initial);
  void editorPreviewParameters(std::span<const ParameterValue> values);
  void editorEndParameterGesture(bool commit);
  void editorInvalidateParameterGesture();
  [[nodiscard]] std::uint64_t editorParameterRevision() const noexcept {
    return parameterRevision_;
  }
  struct EditorModulation {
    std::uint64_t source{};
    std::uint64_t sourcePort{};
    std::uint64_t node{};
    std::uint64_t port{};
    cydaw::base::Text binding;
    int mode{}; // 0 sets the knob, 1 moves it around its value
    float amount{};
  };
  [[nodiscard]] std::vector<EditorModulation> editorModulations() const;
  std::uint64_t editorAddModulator(std::uint64_t owner,
                                   const cydaw::base::Text &typeKey);
  [[nodiscard]] std::uint64_t
  editorModulatorOwner(std::uint64_t modulator) const;
  [[nodiscard]] cydaw::engine::WavetableShape
  editorNodeWavetable(std::uint64_t id) const {
    return nodeWavetable(id);
  }
  bool editorSetNodeWavetable(std::uint64_t id,
                              const cydaw::engine::WavetableShape &shape) {
    return setNodeWavetable(id, shape);
  }
  [[nodiscard]] std::uint64_t
  editorParameterSocket(std::uint64_t node,
                        const cydaw::base::Text &binding) const;
  cydaw::base::Text editorModulate(std::uint64_t modulator,
                                   const cydaw::base::Text &binding,
                                   float amount);
  bool editorUnmodulate(std::uint64_t modulator,
                        const cydaw::base::Text &binding);
  bool editorSetModulationAmount(std::uint64_t source, std::uint64_t sourcePort,
                                 std::uint64_t destination,
                                 std::uint64_t destinationPort, float amount);
  struct EditorContainer {
    std::uint64_t head{};
    // Where its sound leaves it (with a post chain: the last end).
    std::uint64_t end{};
    // With a post chain (XY FX's Post FX): the first end, the branches'
    // mix the post chain follows; 0 without.
    std::uint64_t blend{};
    cydaw::base::Text typeKey;
    std::vector<std::vector<std::uint64_t>> branches;
    // Which of `branches` is the post chain; -1 for none.
    int postBranch{-1};
  };
  [[nodiscard]] bool editorIsContainer(std::uint64_t node) const;
  [[nodiscard]] std::optional<EditorContainer>
  editorContainer(std::uint64_t head) const;
  [[nodiscard]] std::set<std::uint64_t> editorContainerMembers() const;
  [[nodiscard]] std::pair<std::uint64_t, int>
  editorPlaceOf(std::uint64_t node) const;
  std::uint64_t editorInsertContainer(const cydaw::base::Text &typeKey,
                                      std::uint64_t before);
  std::uint64_t editorWrapEffectChain(std::uint64_t first);
  bool editorMoveCard(std::uint64_t id, std::uint64_t head, int branch,
                      std::uint64_t before);
  std::uint64_t editorInsertInBranch(std::uint64_t head, int branch,
                                     const cydaw::base::Text &typeKey,
                                     std::uint64_t before);
  bool editorAddBranch(std::uint64_t head);
  bool editorRemoveBranch(std::uint64_t head, int branch);
  [[nodiscard]] cydaw::base::Text editorBranchName(std::uint64_t head,
                                                   int branch) const;
  bool editorRenameBranch(std::uint64_t head, int branch,
                          const cydaw::base::Text &name);
  [[nodiscard]] cydaw::base::Value
  editorContainerState(std::uint64_t head) const;
  bool editorApplyContainerState(std::uint64_t head,
                                 const cydaw::base::Value &state);
  bool editorApplyState(const cydaw::base::Value &state);
  bool editorRemoveContainer(std::uint64_t head);
  [[nodiscard]] std::uint64_t
  editorParameterTouch(const cydaw::base::Text &binding) const;
  [[nodiscard]] std::optional<cydaw::engine::prism::PrismValues>
  editorPrismValues(std::uint64_t nodeId) const;
  [[nodiscard]] std::optional<cydaw::engine::kick::KickValues>
  editorKickValues(std::uint64_t nodeId) const;
  [[nodiscard]] std::optional<
      std::pair<cydaw::engine::synth::Model, cydaw::engine::synth::Values>>
  editorSynthValues(std::uint64_t nodeId) const;
  [[nodiscard]] std::optional<cydaw::engine::drum::Values>
  editorDrumValues(std::uint64_t nodeId) const;
  struct SamplerCardControls {
    cydaw::engine::SamplerControllers controllers{};
    float articulation{-1.0f};
    // The simple sampler's card: one sample over the keys.
    bool simple{};
  };
  [[nodiscard]] std::optional<SamplerCardControls>
  editorSamplerControls(std::uint64_t nodeId) const;
  bool editorSetAlias(std::uint64_t id, const cydaw::base::Text &alias);
  bool editorSetDescription(std::uint64_t id,
                            const cydaw::base::Text &description);
  bool editorSetCollapsed(std::uint64_t id, bool collapsed);
  bool editorSetBypassed(std::uint64_t id, bool bypassed);
  std::vector<std::uint64_t>
  editorDuplicate(const std::vector<std::uint64_t> &ids);
  bool editorUndo();
  bool editorRedo();
  std::uint64_t editorAddComment(const cydaw::base::Text &title,
                                 base::Point<float> position,
                                 base::Point<float> size);
  bool editorSetComment(std::uint64_t id, const cydaw::base::Text &title,
                        base::Point<float> position, base::Point<float> size,
                        bool commit);
  bool editorRemoveComment(std::uint64_t id);
  void editorArrange();
  struct EditorNativeSpan {
    std::vector<std::uint64_t> entries;
    std::vector<std::uint64_t> exits;
    bool instrument{};
  };
  [[nodiscard]] std::map<std::uint64_t, EditorNativeSpan>
  editorNativeSpans() const;
  [[nodiscard]] std::map<std::uint64_t, EditorNativeSpan>
  nativeSpansOf(const cydaw::engine::NativeDspGraph &native) const;
  [[nodiscard]] cydaw::engine::NativeDspGraph exportedNativeGraph() const;
  [[nodiscard]] std::vector<cydaw::engine::PolyVoiceModulation>
  editorVoiceModulations() const;
  [[nodiscard]] std::map<cydaw::base::Text, float>
  editorModulatedParameters() const;

private:
  bool pendingChanges_{};
  base::Rectangle<float> layoutSpace_{0, 0, 1200, 800};

  using NodeId = std::uint64_t;
  bool setNodeWavetable(NodeId nodeId,
                        const cydaw::engine::WavetableShape &shape);
  [[nodiscard]] NodeId nodeIdForType(const cydaw::base::Text &typeKey) const;
  [[nodiscard]] cydaw::engine::WavetableShape
  nodeWavetable(NodeId nodeId) const;
  static constexpr std::int64_t maximumWavetableFileBytes = 8 * 1'024 * 1'024;
  using PortKind = cydaw::engine::PortKind;
  struct NodePort {
    cydaw::graph::PortId id{};
    cydaw::base::Text key;
    cydaw::base::Text name;
    cydaw::graph::PortType type{cydaw::graph::PortType::value};
    bool input{};
    std::uint8_t channels{1};
    bool acceptsMultipleConnections{};
    double defaultValue{};
    int exposure{-1};
    cydaw::base::Text category;
  };
  struct NodeType {
    cydaw::base::Text key;
    cydaw::base::Text translationKey;
    cydaw::base::Text title;
    cydaw::base::Text categoryKey;
    base::Colour colour;
    bool hasInput{};
    PortKind inputKind{PortKind::audio};
    bool hasOutput{};
    PortKind outputKind{PortKind::audio};
    bool protectedNode{};
    bool composite{};
    std::uint8_t inputChannels{};
    std::uint8_t outputChannels{};
    cydaw::base::Text descriptionKey;
    std::vector<NodePort> ports;
    bool stateful{};
    bool supportsPolyphony{};
    bool breaksFeedbackCycle{};
    std::vector<cydaw::graph::NodeParameter> parameterSchema;
  };
  struct LibraryRow {
    int catalogIndex{-1};
    cydaw::base::Text foldKey; // the fold a heading opens
    cydaw::base::Text label;   // heading text
    int depth{};               // indent: 0 heading, 1 sub-heading or
                               // uncategorised entry, 2 nested entry
    int count{};               // entries under a heading
    bool collapsed{};
  };
  struct Node {
    NodeId id{};
    cydaw::base::Text typeKey;
    cydaw::base::Text title;
    base::Colour colour;
    base::Point<float> position;
    bool hasInput{};
    PortKind inputKind{PortKind::audio};
    bool hasOutput{};
    PortKind outputKind{PortKind::audio};
    bool protectedNode{};
    bool composite{};
    std::uint8_t inputChannels{};
    std::uint8_t outputChannels{};
    cydaw::base::Text alias;
    cydaw::engine::NativeDspNodeParameters parameters;
    ModuleInterfaceState interfaceState;
    cydaw::base::Text description;
    std::vector<NodePort> ports;
    // True only when this reusable-Graph instance owns a custom control
    // surface. Untouched instances continue following the class layout.
    bool interfaceLayoutOverridden{};
    // Editor-only compact presentation. Ports remain available while the
    // control surface is hidden; this never affects DSP compilation.
    bool collapsed{};
    // Switched off: what reaches it goes through untouched (an
    // instrument falls silent). The engine keeps it running, so it comes
    // back without a gap; the graph's shape does not change.
    bool bypassed{};
    // The table this card was shaped with, in harmonics. Null means the
    // built-in bank. The rendered form of the same table lives in
    // parameters.wavetable, and setNodeWavetable writes both at once so
    // they cannot disagree.
    std::shared_ptr<const cydaw::engine::WavetableShape> wavetableShape;
    // The rendered form of the same table. Built off the drag path and
    // handed to the graph by pointer.
    cydaw::engine::WavetableBankRef wavetableBank;
    // A sound-model card's model, and the JSON it came from (saved as is).
    cydaw::engine::SoundModelRef soundModel;
    cydaw::base::Value soundModelJson;
    // A sampler card's map, and its JSON (the zones, the files by path).
    cydaw::engine::SamplerMapRef samplerMap;
    cydaw::base::Value samplerJson;
    // A Prisme card's own tables (imported waves), built, and their JSON.
    cydaw::engine::prism::TablesRef prismTables;
    cydaw::base::Value prismTablesJson;
    // An effect card's impulse response (a cabinet's, loaded), and its JSON.
    cydaw::engine::ImpulseResponseRef impulse;
    cydaw::base::Value impulseJson;
    // A Replacer's instrument (Containers.h): its own graph, as the
    // studio keeps it ({name, graph}); saved as is.
    cydaw::base::Value instrumentJson;
    // A container's branches named by hand (by their place; an empty one
    // keeps its usual name).
    cydaw::base::TextList branchNames;
    // What this node last serialised to. A node is a pure function of
    // itself, so the answer only changes when the node does, and finding
    // out costs about forty microseconds — three hundred of those is what
    // made an edit on a large patch expensive. Last in the struct because
    // the fields above are filled by brace initialisation.
    mutable cydaw::base::Value serialised;
  };
  struct Connection {
    NodeId source{};
    NodeId destination{};
    PortKind kind{PortKind::audio};
    std::uint8_t audioChannels{};
    cydaw::graph::PortId sourcePort{};
    cydaw::graph::PortId destinationPort{};
    // Editor-only waypoints. They never enter the DSP compilation plan.
    std::vector<base::Point<float>> routingPoints;
    // A control cable onto a parameter: 0 sets it, 1 modulates it (the
    // knob keeps its value, the cable moves it by `amount` of its range).
    std::uint8_t mode{};
    float amount{0.25f};
  };
  struct GraphComment {
    std::uint64_t id{};
    cydaw::base::Text title;
    cydaw::base::Text description;
    base::Point<float> position;
    base::Point<float> size{0.42f, 0.34f};
    base::Colour colour{0xff8064c8};
  };
  struct GraphViewBookmark {
    float zoom{1.0f};
    base::Point<float> offset;
  };
  struct PortHit {
    int nodeIndex{-1};
    cydaw::graph::PortId portId{};

    [[nodiscard]] explicit operator bool() const noexcept {
      return nodeIndex >= 0 && portId != 0;
    }
  };
  struct ModuleNode {
    NodeId id{};
    cydaw::base::Text typeKey;
    cydaw::base::Text title;
    base::Point<float> position;
    cydaw::engine::NativeDspNodeParameters parameters;
    std::uint8_t inputChannels{};
    std::uint8_t outputChannels{};
    cydaw::base::Text alias;
    cydaw::base::Text description;
    // Values and presentation owned by this particular reusable-Graph
    // instance. The SavedModule remains the immutable class/default.
    ModuleInterfaceState interfaceState;
    bool interfaceLayoutOverridden{};
    // Per-instance exposed pin fallbacks. This is separate from the
    // reusable Graph class and therefore follows the instance when it is
    // copied, saved or nested inside another Graph.
    std::vector<NodePort> ports;
    bool collapsed{};
    // As a top-level card's: a wavetable's table, a sound model (and the
    // JSON it came from). A card made into a device, or a device's inside
    // edited and written back, keeps them.
    std::shared_ptr<const cydaw::engine::WavetableShape> wavetableShape;
    cydaw::engine::WavetableBankRef wavetableBank;
    cydaw::engine::SoundModelRef soundModel;
    cydaw::base::Value soundModelJson;
    cydaw::engine::SamplerMapRef samplerMap;
    cydaw::base::Value samplerJson;
    cydaw::engine::prism::TablesRef prismTables;
    cydaw::base::Value prismTablesJson;
    cydaw::engine::ImpulseResponseRef impulse;
    cydaw::base::Value impulseJson;
    // As a top-level card's: switched off, a Replacer's instrument (its
    // graph), a container's branch names. Made into a device, or a
    // device's inside written back, a card keeps them.
    bool bypassed{};
    cydaw::base::Value instrumentJson;
    cydaw::base::TextList branchNames;
  };
  struct SavedModule {
    cydaw::base::Text key;
    cydaw::base::Text name;
    cydaw::graph::SemanticVersion version{1, 0, 0};
    // Empty for an original class. Variants retain this stable class key
    // so the manager can show lineage without coupling DSP compilation to
    // mutable display names.
    cydaw::base::Text parentClassKey;
    std::vector<ModuleNode> nodes;
    std::vector<Connection> connections;
    ModuleInterfaceState interfaceState;
    std::vector<EditorProgram> programs;
    bool hasInput{true};
    PortKind inputKind{PortKind::audio};
    bool hasOutput{true};
    PortKind outputKind{PortKind::audio};
    std::uint8_t inputChannels{};
    std::uint8_t outputChannels{};
    bool factoryModule{};
    bool inlineModule{};
    bool locked{};
    cydaw::base::Text description;
    // Library metadata belongs to the reusable Graph class rather than
    // to one of its instances.  Keeping it beside the class makes the
    // Graph Manager useful for both factory and user-authored libraries.
    cydaw::base::Text category;
    cydaw::base::Text author;
    cydaw::base::TextList tags;
    cydaw::graph::GraphRole role{cydaw::graph::GraphRole::generic};
    // The value this class was parsed from, held so that restoring the
    // same value again can reuse the class instead of building it a second
    // time, and so that serialising it again can hand back the very same
    // description. Holding the var keeps the object alive, which is what
    // makes the identity comparison sound. It is dropped the moment
    // anything can have changed the class.
    mutable cydaw::base::Value source;
    // The document version that description was written in. A body from an
    // older schema must not be echoed into a current one, where its fields
    // would be read under different rules.
    mutable int sourceSchema{};
    // The graph definition this class compiles to, which is a pure
    // function of the class. It is dropped alongside the description
    // above, at the same places and for the same reason.
    mutable std::optional<cydaw::graph::GraphDefinition> definitionCache;
  };
  struct PresetNode {
    cydaw::base::Text typeKey;
    base::Point<float> position;
    cydaw::engine::NativeDspNodeParameters parameters;
  };
  struct PresetConnection {
    std::size_t sourceIndex{};
    std::size_t destinationIndex{};
    cydaw::graph::PortId sourcePort{};
    cydaw::graph::PortId destinationPort{};
  };
  struct FactoryPreset {
    cydaw::base::Text key;
    cydaw::base::Text translationKey;
    std::vector<PresetNode> nodes;
    std::vector<PresetConnection> connections;
  };
  struct NativeParameterTarget {
    NodeId instanceNodeId{};
    cydaw::base::Text parameterKey;
    NodeId nativeNodeId{};
  };
  struct InstrumentBoundary {
    std::set<NodeId> voiceNodes;
    NodeId outputNodeId{};
  };
  [[nodiscard]] InstrumentBoundary instrumentVoiceBoundary() const;
  [[nodiscard]] cydaw::engine::WavetableBankRef voiceWavetableBank() const;
  [[nodiscard]] cydaw::engine::SoundModelRef voiceSoundModel() const;
  [[nodiscard]] cydaw::engine::SamplerMapRef voiceSampler() const;
  [[nodiscard]] cydaw::engine::SamplerControllers
  voiceSamplerControllers() const;
  void attachVoiceSampler(cydaw::engine::PolySynthGraphPlan &plan) const;
  [[nodiscard]] cydaw::engine::prism::TablesRef voicePrismTables() const;
  template <typename Result, typename Pick>
  [[nodiscard]] Result firstVoiceCard(const cydaw::base::Text &typeKey,
                                      Pick pick) const;
  static void applyWavetableHarmonics(
      Node &node, const cydaw::base::Array<cydaw::base::Value> &harmonics);
  [[nodiscard]] cydaw::engine::NativeDspGraph
  buildNativeDspGraph(std::vector<NativeParameterTarget> *parameterTargets,
                      NodeId renderedInstrumentNodeId = 0,
                      const std::set<NodeId> &renderedVoiceNodes = {}) const;
  void initialiseCatalog();
  void ensureFactoryModules();
  void applyTranslatedTitles();
  cydaw::base::TextList factoryModuleKeys_;
  bool factoryModulesInstalled_{};

public:
  [[nodiscard]] cydaw::base::Text restoreProfile() const {
    return restoreProfile_;
  }
  [[nodiscard]] cydaw::base::Text exportProfile() const {
    return exportProfile_;
  }
  [[nodiscard]] cydaw::base::Text definitionsProfile() const {
    return "calls=" + cydaw::base::Text(definitionsCalls_) +
           " lastModules=" + cydaw::base::Text(definitionsLastModulesMs_, 2) +
           " lastRoot=" + cydaw::base::Text(definitionsLastRootMs_, 2) +
           " worst=" + cydaw::base::Text(definitionsWorstMs_, 2) +
           " cached=" + cydaw::base::Text(definitionsCacheHits_);
  }

private:
  cydaw::base::Text restoreProfile_;
  mutable cydaw::base::Text exportProfile_;
  mutable int definitionsCalls_{};
  mutable int definitionsCacheHits_{};
  mutable double definitionsWorstMs_{};
  mutable double definitionsLastModulesMs_{};
  mutable double definitionsLastRootMs_{};
  int restoreReusedModules_{};
  int restoreParsedModules_{};
  void initialiseFactoryPresets();
  struct FactoryPresetBoundaries {
    bool midiInput{};
    bool midiOutput{};
    bool audioInput{};
    bool audioOutput{};
    bool soundSource{};
  };
  [[nodiscard]] FactoryPresetBoundaries
  factoryPresetBoundaries(const FactoryPreset &preset) const;
  void initialiseDefaultGraph();
  [[nodiscard]] base::Point<float>
  freePlacementFor(const Node &node, base::Point<float> preferred,
                   NodeId ignoredNodeId = 0) const;
  void addNodeFromType(const NodeType &type,
                       base::Point<float> position = {-1'000.0f, -1'000.0f});
  [[nodiscard]] bool
  spliceNodeIntoConnection(int connectionIndex, const NodeType &insertedType,
                           base::Point<float> normalisedPosition);
  void removeSelectedNodes();
  void duplicateSelectedNodes();
  void remapModulatorOwners(const std::unordered_map<NodeId, NodeId> &remapped);
  std::int64_t graphMembersSignature_{-1};
  [[nodiscard]] bool
  updateGraphMemberParameterVisibility(const cydaw::base::Text &key,
                                       std::optional<bool> showInDevice,
                                       std::optional<bool> publishToHost);
  [[nodiscard]] bool insertModuleIntoCurrentSignalFlow(NodeId moduleNodeId);
  [[nodiscard]] cydaw::engine::NativeDspNodeParameters
  defaultParametersForType(const cydaw::base::Text &typeKey) const;
  [[nodiscard]] ModuleInterfaceState
  createNodeInterface(const Node &node) const;
  [[nodiscard]] ModuleInterfaceState
  createGraphInstanceInterface(NodeId instanceId,
                               const SavedModule &module) const;
  [[nodiscard]] ModuleInterfaceState
  createDefaultModuleInterface(const SavedModule &module) const;
  [[nodiscard]] ModuleInterfaceState createDefaultGraphInterface() const;
  void synchroniseGraphInterface();
  void rebuildPendingWavetableBanks();
  [[nodiscard]] const ModuleInterfaceState &
  presentationForNode(const Node &node) const;
  [[nodiscard]] ModuleInterfaceState *editablePresentationForNode(Node &node);
  [[nodiscard]] bool updateNodeParameter(Node &node,
                                         const cydaw::base::Text &binding,
                                         double value);
  [[nodiscard]] bool updateModuleParameter(SavedModule &module,
                                           const cydaw::base::Text &binding,
                                           double value);
  [[nodiscard]] bool
  updateGraphInstanceParameter(Node &node, const cydaw::base::Text &binding,
                               double value);
  [[nodiscard]] cydaw::base::Value
  moduleGraphState(const SavedModule &module) const;
  void applyModuleGraphState(const cydaw::base::Text &moduleKey,
                             const cydaw::base::Value &state,
                             bool structureChanged = true);
  void inferModulePorts(SavedModule &module) const;
  [[nodiscard]] cydaw::graph::GraphRole
  inferModuleRole(const SavedModule &module) const;
  void registerModuleType(const SavedModule &module);
  [[nodiscard]] cydaw::base::Result
  validateUserGraphPresetState(const cydaw::base::Value &state) const;
  [[nodiscard]] cydaw::base::Result
  applyFactoryPreset(const FactoryPreset &preset);
  void arrangeNodesBySignalFlow(bool resetView);
  void resetNodeLayout();
  void notifyLayoutChanged(const char *origin = "?");
  void dispatchParameterNotification();
  void queueParameterNotification();
  void flushParameterNotification();
  void commitQueuedParameterHistory();
  void resetEditHistory(const cydaw::base::Value *knownState = nullptr);
  void commitEditHistory(bool parameterChange);
  void undoGraphEdit();
  void redoGraphEdit();
  void restoreEditHistoryState(const cydaw::base::Value &state);
  void refreshGraphAssetValidation();
  void scheduleGraphAssetValidation();
  bool validationPending_{};
  bool restoreUiPending_{};
  [[nodiscard]] cydaw::graph::PluginIdentity defaultVst3ExportIdentity() const;
  [[nodiscard]] cydaw::graph::PluginExportResult
  planVst3Export(const cydaw::graph::PluginIdentity &identity) const;
  void setSelectedAudioChannels(int channels);
  void setStatus(const cydaw::base::Text &message, bool error = false);
  bool viewLayoutPending_{false};
  [[nodiscard]] cydaw::graph::GraphRole inferCurrentGraphRole() const;
  [[nodiscard]] static cydaw::base::Text graphAudioBoundaryKey(bool input,
                                                               int channels);
  [[nodiscard]] static bool
  isGraphAudioBoundaryKey(const cydaw::base::Text &key, bool *input = nullptr);
  [[nodiscard]] bool isBoundaryNode(const Node &node) const;
  [[nodiscard]] cydaw::base::Text displayTitle(const Node &node) const;
  [[nodiscard]] std::uint8_t audioInputChannels(const Node &node) const;
  [[nodiscard]] std::uint8_t audioOutputChannels(const Node &node) const;
  [[nodiscard]] std::optional<ControlCableTarget>
  controlCableTarget(const Connection &connection) const;
  [[nodiscard]] cydaw::base::Text
  knobOfSocket(const Node &node, const cydaw::base::Text &portKey) const;
  [[nodiscard]] cydaw::base::Text
  knobNodeType(const cydaw::base::Text &moduleKey,
               const cydaw::base::Text &classKey) const;
  [[nodiscard]] std::pair<double, double>
  knobRange(const cydaw::base::Text &moduleKey,
            const cydaw::base::Text &classKey) const;
  void applyDefaultCableMode(std::uint64_t source, std::uint64_t destination,
                             std::uint64_t destinationPort);
  [[nodiscard]] const NodePort *findPort(const Node &node,
                                         cydaw::graph::PortId portId) const;
  [[nodiscard]] static cydaw::base::Value
  serialisePortDefaults(const std::vector<NodePort> &ports);
  static void restorePortDefaults(const cydaw::base::Value &state,
                                  std::vector<NodePort> &ports);
  static void applyParameterPortDefaults(
      const cydaw::base::Text &typeKey,
      const cydaw::engine::NativeDspNodeParameters &parameters,
      std::vector<NodePort> &ports);
  static void applyLegacyPortDefaults(
      const cydaw::base::Text &typeKey,
      const cydaw::engine::NativeDspNodeParameters &parameters,
      std::vector<NodePort> &ports);
  [[nodiscard]] Node *findNode(NodeId id);
  [[nodiscard]] const Node *findNode(NodeId id) const;
  [[nodiscard]] const Node *readNode(NodeId id) const { return findNode(id); }
  [[nodiscard]] const NodeType *findType(const cydaw::base::Text &key) const;
  [[nodiscard]] SavedModule *findModule(const cydaw::base::Text &key);
  [[nodiscard]] const SavedModule *
  findModule(const cydaw::base::Text &key) const;
  [[nodiscard]] bool connectNodes(NodeId source, NodeId destination,
                                  cydaw::graph::PortId sourcePort = 0,
                                  cydaw::graph::PortId destinationPort = 0);
  [[nodiscard]] bool
  connectReplacingInput(NodeId source, NodeId destination,
                        cydaw::graph::PortId sourcePort,
                        cydaw::graph::PortId destinationPort);
  [[nodiscard]] cydaw::engine::GraphResult
  validateConnection(NodeId source, NodeId destination,
                     cydaw::graph::PortId sourcePort = 0,
                     cydaw::graph::PortId destinationPort = 0) const;
  [[nodiscard]] static PortKind enginePortKind(cydaw::graph::PortType type);
  [[nodiscard]] static PortKind portKindFromInt(int value);
  std::vector<NodeType> catalog_;
  std::vector<int> filteredCatalog_;
  std::vector<LibraryRow> libraryRows_;
  cydaw::base::Text layoutChangeProfile_;
  cydaw::base::Text insertProfile_;
  cydaw::base::Text effectParametersProfile_;
  cydaw::base::Text addProfile_;
  int layoutChangeCalls_{};
  cydaw::base::Text layoutChangeOrigins_;
  mutable cydaw::base::Text polyCompileProfile_;
  mutable cydaw::graph::GraphLibrary instrumentLibrary_;
  int layoutChangeDepth_{};
  mutable cydaw::base::Text placementProfile_;
  mutable int paintedCards_{};
  mutable int paintedCables_{};
  cydaw::base::TextList collapsedLibraryFolds_;
  std::vector<Node> nodes_;
  std::vector<Connection> connections_;
  std::vector<GraphComment> comments_;
  std::vector<Node> copiedNodes_;
  std::vector<Connection> copiedConnections_;
  int clipboardPasteCount_{};
  std::vector<SavedModule> savedModules_;
  mutable std::optional<RuntimeGraphExport> exportCache_;
  mutable std::uint64_t exportCacheSignature_{};
  mutable int exportRebuilds_{};
  mutable cydaw::base::Text exportCacheProfile_;
  [[nodiscard]] std::uint64_t structuralSignature() const;
  cydaw::base::Text modulateFailure_;
  bool modulateParameter(NodeId sourceNodeId, cydaw::graph::PortId sourcePort,
                         NodeId targetNodeId, const cydaw::base::Text &binding);
  void removeModulation(NodeId nodeId, const cydaw::base::Text &binding);
  [[nodiscard]] NodeId
  modulationRangeNodeFor(const Node &node,
                         const cydaw::base::Text &binding) const;
  static constexpr std::size_t eqSpectrumBinCount = 96;
  [[nodiscard]] bool
  patchNativeGraphParameters(cydaw::engine::NativeDspGraph &graph) const;
  std::vector<FactoryPreset> factoryPresets_;
  ModuleInterfaceState graphInterfaceState_;
  std::set<NodeId> wavetableBankPending_;
  std::set<NodeId> selectedNodes_;
  std::uint64_t selectedCommentId_{};
  std::uint64_t nextCommentId_{1};
  std::array<std::optional<GraphViewBookmark>, 9> viewBookmarks_;
  cydaw::graph::PortId selectedPortId_{};
  NodeId nextNodeId_{1};
  int nextModuleIndex_{1};
  int draggedNode_{-1};
  base::Point<float> dragOffset_;
  base::Point<float> nodeDragStartScreen_;
  std::unordered_map<NodeId, base::Point<float>> nodeDragStartPositions_;
  std::vector<std::pair<int, std::vector<base::Point<float>>>>
      connectionRoutingDragStartPositions_;
  std::uint64_t draggedCommentId_{};
  bool resizingComment_{};
  base::Point<float> commentDragStartScreen_;
  base::Point<float> commentDragStartPosition_;
  base::Point<float> commentDragStartSize_;
  std::unordered_map<NodeId, base::Point<float>> commentNodeDragStartPositions_;
  bool marqueeSelecting_{};
  base::Point<float> marqueeStart_;
  base::Point<float> marqueeEnd_;
  std::set<NodeId> marqueeBaseSelection_;
  int selectedConnection_{-1};
  int draggedRoutingConnection_{-1};
  int draggedRoutingPoint_{-1};
  bool connecting_{};
  NodeId connectionSource_{};
  cydaw::graph::PortId connectionSourcePort_{2};
  bool connectionStartedFromInput_{};
  NodeId connectionHoverTarget_{};
  cydaw::graph::PortId connectionHoverPort_{1};
  bool connectionHoverCompatible_{};
  cydaw::base::Text connectionHoverMessage_;
  base::Point<float> pendingConnectionEnd_;
  cydaw::base::Text statusMessage_;
  cydaw::base::Text contextName_;
  cydaw::base::Text graphName_;
  cydaw::base::Text graphDescription_;
  cydaw::base::Text graphCategory_;
  cydaw::base::Text graphAuthor_;
  std::vector<EditorProgram> programs_;
  cydaw::base::Text activeFactoryPresetKey_;
  cydaw::base::File currentGraphPresetFile_;
  cydaw::graph::GraphReference graphReference_;
  cydaw::graph::GraphRole graphRole_{cydaw::graph::GraphRole::audioEffect};
  cydaw::graph::GraphScope graphScope_{cydaw::graph::GraphScope::project};
  cydaw::graph::PluginIdentity pluginExportIdentity_;
  cydaw::base::TextList graphTags_;
  cydaw::base::Text graphAssetValidationMessage_;
  std::set<NodeId> invalidGraphNodes_;
  std::set<std::pair<NodeId, cydaw::graph::PortId>> invalidGraphPorts_;
  std::unordered_map<NodeId, cydaw::base::Text> graphNodeValidationMessages_;
  NodeId lastFocusedValidationNode_{};
  bool graphAssetValid_{true};
  bool statusIsError_{};
  bool dspCompilationKnown_{};
  bool dspCompilationOk_{};
  std::size_t dspCompilationNodeCount_{};
  cydaw::base::Text dspCompilationError_;
  static constexpr std::size_t spectrumBinCount = 32;
  std::array<float, cydaw::engine::AudioAnalysisFrame::waveformCapacity>
      analysisWaveform_{};
  std::array<float, spectrumBinCount> analysisSpectrum_{};
  std::array<float, eqSpectrumBinCount> eqSpectrum_{};
  bool eqSpectrumHeld_{};
  cydaw::engine::AudioAnalysisFrame lastAnalysisFrame_{};
  bool lastAnalysisFrameHeld_{};
  std::array<float, cydaw::engine::AudioAnalysisFrame::maximumChannels>
      analysisPeak_{};
  std::array<float, cydaw::engine::AudioAnalysisFrame::maximumChannels>
      analysisRms_{};
  std::size_t analysisWaveformCount_{};
  std::size_t analysisChannelCount_{2};
  bool analysisActive_{};
  std::unordered_map<NodeId, float> controlValues_;
  base::Rectangle<int> graphZoomWell_;
  std::unique_ptr<GraphDocument> inPlaceModuleGraphEditor_;
  GraphDocument *moduleGraphEditor_{};
  cydaw::base::Text inPlaceModuleKey_;
  std::function<void()> navigateBackHandler_;
  bool applyingModuleGraphState_{};
  bool editorParameterChangePending_{};
  NodeId inlineEditedNode_{};
  std::uint64_t inlineEditedControl_{};
  base::Point<float> inlineControlOffset_;
  bool inlineControlDragging_{};
  bool inlineControlResizing_{};
  float viewZoom_{1.0f};
  base::Point<float> viewOffset_;
  std::optional<base::Point<float>> restoredViewCentre_;
  NodeId restoredViewAnchor_{};
  base::Point<float> restoredViewAnchorDelta_;
  struct ViewAnchor {
    NodeId node{};
    base::Point<float> delta;
  };
  base::Rectangle<int> lastLaidOutCanvas_;
  bool panningView_{};
  bool minimapDragging_{};
  base::Point<float> panDragStart_;
  base::Point<float> panStartOffset_;
  bool valueGraphMode_{};
  bool nodeDragOver_{};
  bool graphPresetDragOver_{};
  bool graphPresetFileDragOver_{};
  bool nodePresetFileDragOver_{};
  NodeId nodePresetFileTarget_{};
  base::Point<int> nodeDragPosition_;
  cydaw::base::Text selectedCategoryKey_;
  cydaw::base::TextList categoryKeys_;
  bool refreshingSelectionUi_{};
  cydaw::base::Text selectedPublishedParameterKey_;
  bool editingLocked_{};
  bool parameterNotificationPending_{};
  bool parameterHistoryPending_{};
  std::map<cydaw::base::Text, std::uint64_t> parameterTouches_;
  std::uint64_t parameterTouchCount_{};
  struct ParameterGestureValue {
    ParameterValue initial;
    double current{};
  };
  std::vector<ParameterGestureValue> parameterGesture_;
  std::uint64_t parameterRevision_{};
  std::int64_t lastParameterUiChangeTimeMs_{};
  std::vector<cydaw::base::Value> undoHistory_;
  std::uint64_t historySteps_{};
  std::vector<cydaw::base::Value> redoHistory_;
  cydaw::base::Value currentHistoryState_;
  cydaw::base::Value pendingHistoryState_;
  void markEditHistoryBaseline(const cydaw::base::Value *knownState = nullptr);
  [[nodiscard]] const cydaw::base::Value &editHistoryBaseline();
  bool restoringHistory_{};
  bool instrumentIsMainInput_{};
  bool lastHistoryWasParameter_{};
  std::int64_t lastParameterHistoryTimeMs_{};
  cydaw::base::Text copiedNodeParameterType_;
  std::optional<cydaw::engine::NativeDspNodeParameters> copiedNodeParameters_;
  std::unordered_map<cydaw::graph::PortId, double> copiedNodePortDefaults_;
  static constexpr std::size_t maxEditHistoryStates = 96;
  bool selectionPanelVisible_{true};
  bool configurationPanelVisible_{true};
  bool configurationPanelShowsPalette_{true};
  bool configurationPanelShowsMembers_{};
  bool minimapVisible_{true};
  bool resizingSelectionPanel_{};
  bool resizingConfigurationPanel_{};
  int selectionPanelWidth_{260};
  int configurationPanelWidth_{300};
  std::function<void()> layoutChangedHandler_;
  std::function<void()> parametersChangedHandler_;
  std::function<void()> userGraphPresetSavedHandler_;
  std::function<void(float, float)> effectChangedHandler_;
  base::Point<float> nodeVisualSize(const Node &node) const;
};
} // namespace cydaw::app
