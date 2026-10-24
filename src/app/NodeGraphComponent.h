#pragma once

#include "GraphManagerComponent.h"
#include "GraphPluginExportComponent.h"
#include "ModuleInterfaceEditor.h"

#include <cydaw/engine/AudioGraph.h>
#include <cydaw/engine/AudioAnalysis.h>
#include <cydaw/engine/GraphDefinitionCompiler.h>
#include <cydaw/engine/NativeDspGraph.h>
#include <cydaw/engine/PolySynthGraph.h>
#include <cydaw/graph/GraphDefinition.h>
#include <cydaw/graph/GraphPluginExport.h>

#include <juce_gui_extra/juce_gui_extra.h>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

namespace cydaw::app {

// The selection summary, which is the text a user reads most while patching.
// It arrives as plain lines assembled in half a dozen places, so rather than
// restyle every one of them this reads what it is handed: the first line is
// the heading, and a line shaped "key: value" has its key dimmed so the eye
// lands on the value. setText and setTooltip keep juce::Label's signatures,
// so no caller changes.
class SelectionSummary final : public juce::Component,
                               public juce::SettableTooltipClient {
public:
    void setText(const juce::String& text,
                 juce::NotificationType = juce::dontSendNotification);
    [[nodiscard]] int preferredHeight() const;
    void paint(juce::Graphics& graphics) override;
    // Raised when the summary needs a different height, which is only when
    // the selection changes. The owner lays out again.
    std::function<void()> onHeightChanged;

private:
    [[nodiscard]] int wrappedRows(const juce::String& line,
                                  bool heading = false) const;

    juce::StringArray lines_;
};

class NodeGraphComponent final : public juce::Component,
                                 public juce::DragAndDropContainer,
                                 public juce::DragAndDropTarget,
                                 public juce::FileDragAndDropTarget,
                                 public juce::SettableTooltipClient,
                                 private juce::Timer,
                                 private juce::AsyncUpdater,
                                 private juce::ListBoxModel {
public:
    // Two hand-written lists that a new parameter has to be added to, and
    // that nothing was checking: what the document stores, and what a control
    // is allowed to write.
    [[nodiscard]] static juce::Result selfTestNativeParameterPlumbing();
    // The palette as text, one row per line: "v NAME (count)" for an open
    // heading, "> " for a folded one, "- NAME" for an entry, indented by
    // depth. Written for the suite, which cannot click.
    [[nodiscard]] juce::String paletteOutline() const;
    // What the last repaint actually drew, against what it was offered.
    // Guessing where the paint cost sits was wrong twice.
    [[nodiscard]] juce::String paintProfile() const;
    void setPaletteSearch(const juce::String& query);
    // Frames the canvas at a chosen zoom, for snapshots that have to show
    // what a given zoom level actually renders.
    void setCanvasZoom(float zoom);
    // Drags one node by a screen offset and reports the region the drag asked
    // to have repainted. Written for the suite, which cannot drag a mouse.
    juce::RectangleList<int> dragNodeForTest(
        const juce::String& nodeTypeKey, juce::Point<float> screenDelta);
    // Lays the cards on a deterministic grid, so a measurement scene is a
    // patch someone could have laid out rather than the tight cascade that
    // inserting leaves behind.
    void spreadNodesForTest();
    // Undo and redo, so the bench can time what a patching session actually
    // does rather than only what it paints.
    // Where an edit's bookkeeping spends its time: serialising the graph,
    // copying that tree for the history, and comparing it with the last one.
    [[nodiscard]] juce::String historyCostForTest();
    // Where the bookkeeping after an edit spends its time.
    [[nodiscard]] juce::String insertProfile() const { return insertProfile_; }
    // Where one notch of the track drive/cutoff faders goes.
    [[nodiscard]] juce::String effectParametersProfile() const
    {
        return effectParametersProfile_;
    }
    [[nodiscard]] juce::String addProfile() const { return addProfile_; }
    [[nodiscard]] juce::String placementProfile() const
    {
        return placementProfile_;
    }
    [[nodiscard]] juce::String layoutChangeProfile() const
    {
        return layoutChangeProfile_;
    }
    // Where the instrument compile's time goes: the typed definitions, the
    // library install, the compile itself.
    [[nodiscard]] juce::String polyCompileProfile() const
    {
        return polyCompileProfile_;
    }
    // Who asked for the layout changes, most recent last; for the probe.
    [[nodiscard]] juce::String layoutChangeOrigins() const
    {
        return layoutChangeOrigins_;
    }
    void undoForTest();
    void redoForTest();
    void togglePaletteFold(const juce::String& foldKey);
    // What dragging a palette row onto the canvas hands over, and whether the
    // canvas would accept it. Written for the suite, which cannot drag.
    [[nodiscard]] juce::String paletteDragPayload(int row);
    // The arrow keys inside the search box, and what they leave highlighted.
    [[nodiscard]] int nodeCount() const;
    // Serialises with every node cache dropped first. The suite compares the
    // two: a cache that ever disagrees with a fresh build is a stale document.
    [[nodiscard]] juce::var serialiseWithoutCache() const;
    // Lays the cards out the way the Arrange button does. The demo builder
    // needs it: nodes spliced into a cable land on the cable's midpoint,
    // and four of them on one cable land on top of each other.
    void arrangeNodes() { resetNodeLayout(); }
    // Frames one card, close enough to read its knobs. The demo opens on the
    // modulated drive so the ring and its dot are the first thing seen.
    void frameNode(const juce::String& nodeTypeKey)
    {
        const auto id = nodeIdForType(nodeTypeKey);
        if (id == 0)
            return;
        selectedNodes_.clear();
        selectedNodes_.insert(id);
        selectedCommentId_ = 0;
        selectedConnection_ = -1;
        selectedPortId_ = 0;
        frameSelectionOrGraph();
    }

    // Frames the whole graph, the way the Frame button does. Arranging moves
    // the cards; without this the saved view looked at empty canvas.
    void frameGraph()
    {
        // The whole graph, not whatever the last edit left selected.
        selectedNodes_.clear();
        selectedCommentId_ = 0;
        selectedConnection_ = -1;
        selectedPortId_ = 0;
        frameSelectionOrGraph();
    }
    // The cards' titles in order, the plumbing left out, at most `limit`
    // of them (0 for all): what the launcher and the mixer list under a
    // channel. Read from the nodes; serialising the graph to read the same
    // thing from its file form cost the change of view 80 ms on a large
    // patch.
    [[nodiscard]] juce::StringArray deviceNames(int limit) const;
    // For the paths that reach into every node at once and cannot say which
    // ones they changed.
    void invalidateSerialisedNodes();
    // The undo history's size in distinct objects. Entries share the class
    // bodies they describe, so counting them naively would count the sharing
    // instead of showing it.
    [[nodiscard]] juce::String historyFootprint() const;
    [[nodiscard]] int paletteSelectedRow() const;
    void movePaletteSelection(int delta);
    [[nodiscard]] juce::String paletteRowKey(int row) const;
    [[nodiscard]] bool acceptsDragPayload(const juce::String& payload);
    // A control cable dropped on a knob, as the suite performs it: the
    // source card's output onto the named parameter of the target card.
    bool modulateParameterForTest(const juce::String& sourceTypeKey,
                                  const juce::String& targetTypeKey,
                                  const juce::String& parameterSuffix);
    // The ring the knob shows, empty when it shows none; and turning it.
    [[nodiscard]] juce::Range<double> modulationRingForTest(
        const juce::String& targetTypeKey,
        const juce::String& parameterSuffix) const;
    // A ring drag in pixels, the hit test as the ring is drawn, and whether
    // the sweep is turned over — for the suite.
    bool dragModulationRingForTest(const juce::String& targetTypeKey,
                                   const juce::String& parameterSuffix,
                                   float deltaY);
    [[nodiscard]] bool modulationRingHitForTest(
        const juce::String& targetTypeKey, const juce::String& parameterSuffix);
    [[nodiscard]] bool modulationInvertedForTest(
        const juce::String& targetTypeKey,
        const juce::String& parameterSuffix) const;
    // The ring's menu and its readout, for the suite.
    bool removeModulationForTest(const juce::String& targetTypeKey,
                                 const juce::String& parameterSuffix);
    bool invertModulationForTest(const juce::String& targetTypeKey,
                                 const juce::String& parameterSuffix);
    [[nodiscard]] juce::String modulationReadoutForTest(
        const juce::String& targetTypeKey,
        const juce::String& parameterSuffix) const;
    [[nodiscard]] bool modulationBubbleShownForTest(
        const juce::String& targetTypeKey,
        const juce::String& parameterSuffix) const;
    bool turnModulatedKnobForTest(const juce::String& targetTypeKey,
                                  const juce::String& parameterSuffix,
                                  double value);
    // Why the last drop was refused, for the suite's labels.
    [[nodiscard]] juce::String modulateFailureForTest() const
    {
        return modulateFailure_;
    }
    // Feeds a modulated knob the value its range node would report, and
    // reads back where its dot sits. The suite has no audio thread.
    bool feedModulationPositionForTest(const juce::String& targetTypeKey,
                                       const juce::String& parameterSuffix,
                                       double value);
    [[nodiscard]] std::optional<double> modulationPositionForTest(
        const juce::String& targetTypeKey,
        const juce::String& parameterSuffix) const;
    // Where a socket sits on the canvas, and which socket a canvas point
    // resolves to the way a click does. Written for the suite, which cannot
    // click.
    [[nodiscard]] juce::Point<float> portSocketPoint(
        const juce::String& nodeTypeKey, const juce::String& portKey) const;
    [[nodiscard]] juce::String portKeyAtPoint(juce::Point<float> point,
                                              bool input) const;
    [[nodiscard]] juce::StringArray nodePortKeys(
        const juce::String& nodeTypeKey, bool input) const;
    // What an analysis frame asks to have repainted, and where a card sits.
    // Written for the suite: a frame must touch the cards that show the
    // signal and none of the others.
    [[nodiscard]] juce::RectangleList<int> analysisRepaintRegionForTest() const;
    // Where the first EQ curve on a card reads the signal's peak, in hertz,
    // after the last analysis frame. Zero when there is no such curve.
    [[nodiscard]] double eqCurveSpectrumPeakHzForTest() const;
    // Builds the embedded controls now, which the message loop would have
    // done on its own turn; a test has no such turn.
    void rebuildEmbeddedControlsForTest() { rebuildEmbeddedSliders(); }
    // A restore lays out on the next turn of the message loop; the suite has
    // no loop and asks for that turn itself.
    void runPendingLayoutForTest() { handleUpdateNowIfNeeded(); }
    [[nodiscard]] juce::Rectangle<int> canvasBoundsForTest() const
    {
        return canvasBounds();
    }
    [[nodiscard]] juce::Rectangle<int> nodeBoundsForTest(
        const juce::String& nodeTypeKey) const;
    // The first band's frequency as the card's live curve holds it, or 0
    // when the card has no live curve. Written for the suite, which
    // cannot look at the plot.
    [[nodiscard]] double eqCurveFirstBandFrequencyForTest(
        const juce::String& nodeTypeKey) const;
    // What a value input's socket says, keyed like the other probes; empty
    // when the card has no such socket. And typing into one the way the
    // details panel does.
    [[nodiscard]] std::optional<double> inputPortDefaultForTest(
        const juce::String& nodeTypeKey, const juce::String& portKey) const;
    bool setInputPortDefaultForTest(const juce::String& nodeTypeKey,
                                    const juce::String& portKey, double value);
    // What a card's live curve says at one frequency, in dB — the EQ
    // curve's summed bands, or the filter card's response — for the suite,
    // which renders a sine through the same graph and asks whether the
    // sound is what the picture says. Empty when the card has no such curve.
    [[nodiscard]] std::optional<double> eqCurveMagnitudeDbForTest(
        const juce::String& nodeTypeKey, double frequency) const;
    [[nodiscard]] std::optional<double> filterResponseMagnitudeDbForTest(
        const juce::String& nodeTypeKey, double frequency) const;
    // And the compressor card's transfer: the output level its curve
    // draws for an input level, in dB.
    [[nodiscard]] std::optional<double> dynamicsResponseOutputDbForTest(
        const juce::String& nodeTypeKey, double inputDb) const;
    // The table a card is shaped with, and shaping it. Keyed by node type
    // like every other probe here, because an id is not something the suite
    // can learn.
    bool setNodeWavetable(const juce::String& nodeTypeKey,
                          const cydaw::engine::WavetableShape& shape);
    // A sound-model card's model, from a "cydaw-sound-model" JSON (a .cysound
    // file's): what it plays. False, with why, when it is not one.
    bool setNodeSoundModel(std::uint64_t nodeId, const juce::var& json, juce::String& error);
    // "brass-c4 · 1 zone · C4 · 82 partials", or empty without a model.
    [[nodiscard]] juce::String nodeSoundModelDescription(std::uint64_t nodeId) const;
    [[nodiscard]] cydaw::engine::SoundModelRef nodeSoundModel(std::uint64_t nodeId) const;
    // The JSON the card's model came from, as saved.
    [[nodiscard]] juce::var nodeSoundModelJson(std::uint64_t nodeId) const;
    // The model the instrument plays: the first sound-model card's, inside
    // devices too, in the order the instrument compiler meets them.
    [[nodiscard]] juce::var voiceSoundModelJson() const;
    // A sampler card's map, from a "cydaw-sampler" JSON (SamplerMapFile.h):
    // its zones, its samples asked of the store. False, with why, when it is
    // not one.
    bool setNodeSampler(std::uint64_t nodeId, const juce::var& json, juce::String& error);
    [[nodiscard]] juce::var nodeSamplerJson(std::uint64_t nodeId) const;
    // A Replacer card's instrument, as the studio keeps it; null for none.
    [[nodiscard]] juce::var nodeInstrumentJson(std::uint64_t nodeId) const;
    bool setNodeInstrumentJson(std::uint64_t nodeId, const juce::var& state);
    [[nodiscard]] cydaw::engine::SamplerMapRef nodeSampler(std::uint64_t nodeId) const;
    // The map the instrument plays: the first sampler card's, inside devices
    // too.
    [[nodiscard]] juce::var voiceSamplerJson() const;
    // A Prisme card's own tables (SoundModelFile.h: prismTableVar…): waves
    // imported into its oscillators, played instead of the factory tables.
    // A void `json` takes them all out. One undo step.
    bool setNodePrismTables(std::uint64_t nodeId, const juce::var& json, juce::String& error);
    [[nodiscard]] juce::var nodePrismTablesJson(std::uint64_t nodeId) const;
    [[nodiscard]] cydaw::engine::prism::TablesRef nodePrismTables(std::uint64_t nodeId) const;
    // A cabinet card's impulse response (SoundModelFile.h: impulseVar), played
    // instead of its modelled cabinet and microphones; a void `json` takes
    // it out. One undo step.
    bool setNodeImpulse(std::uint64_t nodeId, const juce::var& json, juce::String& error);
    [[nodiscard]] juce::var nodeImpulseJson(std::uint64_t nodeId) const;
    [[nodiscard]] cydaw::engine::ImpulseResponseRef nodeImpulse(std::uint64_t nodeId) const;
    [[nodiscard]] cydaw::engine::WavetableShape nodeWavetable(
        const juce::String& nodeTypeKey) const;
    // Building a bank is deferred to the message loop, which a test does not
    // run. This does now what the loop would have done.
    void rebuildWavetableBanksForTest() { rebuildPendingWavetableBanks(); }
    // One drawing gesture, exactly as the editor performs it: the suite
    // cannot drag a mouse, and a simplified stand-in would not prove the
    // wiring.
    bool drawWavetableForTest(const juce::String& nodeTypeKey,
                              const cydaw::engine::WavetableShape& shape);
    // The import, addressed by node type like the rest of the probes.
    juce::Result importWavetableFileForTest(const juce::String& nodeTypeKey,
                                            const juce::File& file);
    struct RuntimeGraphExport {
        cydaw::engine::NativeDspGraph nativeGraph;
        std::vector<cydaw::graph::PublishedParameter> publishedParameters;
        std::vector<cydaw::engine::CompiledControlTelemetryTarget>
            controlTelemetryTargets;
        cydaw::engine::PolySynthGraphPlan instrumentPlan;
        juce::Result compilationResult { juce::Result::ok() };
    };

    NodeGraphComponent();
    ~NodeGraphComponent() override;

    void refreshTranslations();
    [[nodiscard]] juce::var serialise() const;
    // The graph the engine plays: its cards switched off listed in it.
    [[nodiscard]] cydaw::engine::NativeDspGraph nativeDspGraph() const;
    // A compiled graph of this one (runtimeGraphExport's) with the cards
    // switched off listed in it, as nativeDspGraph has them: what an
    // exported plug-in must play too.
    [[nodiscard]] cydaw::engine::NativeDspGraph withCardBypasses(cydaw::engine::NativeDspGraph graph) const;
    // The same graph with the cache set aside, and how many times the export
    // has been rebuilt from scratch. The suite compares the two graphs and
    // counts the rebuilds: a notch must produce the first and none of the
    // second.
    [[nodiscard]] cydaw::engine::NativeDspGraph nativeDspGraphWithoutCache() const;
    [[nodiscard]] int exportRebuilds() const noexcept { return exportRebuilds_; }
    // Why the last export was rebuilt, or "hit". Guessing this was wrong once.
    [[nodiscard]] juce::String exportCacheProfile() const
    {
        return exportCacheProfile_;
    }
    [[nodiscard]] RuntimeGraphExport runtimeGraphExport() const;
    [[nodiscard]] juce::Result compilePolySynthGraph(
        cydaw::engine::PolySynthGraphPlan& destination) const;
    [[nodiscard]] ModuleInterfaceState graphInterfaceState() const;
    // Whether the graph holds a node of this type. The device strip used to
    // serialise the whole graph, class bodies and all, to ask this twice on
    // every edit.
    [[nodiscard]] bool containsNodeType(const juce::String& typeKey) const;
    [[nodiscard]] std::vector<cydaw::graph::GraphDefinition>
        graphDefinitions() const;
    [[nodiscard]] cydaw::graph::ValidationResult validateGraphAsset() const;
    void initialiseEmptyMidiTrackGraph();
    void initialiseValueModulationGraph();
    [[nodiscard]] double evaluateValueOutput(double timeSeconds) const;
    [[nodiscard]] bool setGraphInterfaceParameter(const juce::String& binding,
                                                   double value);
    [[nodiscard]] bool setPublishedParameterExposure(
        const juce::String& binding, bool showInDevice,
        bool publishToHost);
    void restore(const juce::var& state);
    void setLayoutChangedHandler(std::function<void()> handler);
    // An instrument track's graph: its main audio input is the instrument
    // (its voices), so each audio input of the graph is another one, even
    // before an instrument is there.
    void setInstrumentIsMainInput(bool instrument) noexcept { instrumentIsMainInput_ = instrument; }
    void setParametersChangedHandler(std::function<void()> handler);
    void setDspCompilationStatus(const juce::Result& result, std::size_t nodeCount);
    void setAnalysisFrame(const cydaw::engine::AudioAnalysisFrame& frame);
    void setControlValues(
        const std::vector<std::pair<std::uint64_t, float>>& values);
    void clearAnalysis() noexcept;
    [[nodiscard]] bool hasAnalysisFrame() const noexcept;
    [[nodiscard]] float maximumSpectrumMagnitude() const noexcept;
    [[nodiscard]] std::size_t factoryPresetCount() const noexcept;
    [[nodiscard]] juce::Result loadFactoryPreset(std::size_t presetIndex);
    struct FactoryPresetSummary {
        juce::String key;
        juce::String translationKey;
        bool soundSource {};
    };
    // Every factory graph, with what a browser needs to file it: a sound
    // source (an instrument, or a generator) or a processor.
    [[nodiscard]] std::vector<FactoryPresetSummary> factoryPresetSummaries() const;
    // Everything a track can be given, by key, for the control port: the
    // factory presets (whole graphs), the factory modules and the
    // primitive nodes with an input and an output.
    struct DeviceCatalogueEntry {
        juce::String key;
        juce::String name;
        juce::String kind; // "preset", "module" or "node"
        juce::String category;
        juce::String description;
        bool soundSource {};
        juce::String input;  // "audio", "midi", "control", "transport" or "none"
        juce::String output;
        juce::String categoryKey; // a node's catalogue category key
        // Its name in English, whatever the language (inside a module).
        juce::String englishName;
    };
    [[nodiscard]] std::vector<DeviceCatalogueEntry> deviceCatalogue() const;
    // Removes one card — by id, or the last one of a type — the way the
    // Delete key does; for the control port.
    bool removeNodeById(std::uint64_t id);
    bool removeNodeByType(const juce::String& typeKey);
    // A new card of `sourceTypeKey` made to modulate a knob of the first card
    // of `targetTypeKey` (the control port's `modulate`): that new card, not
    // one already there, and it goes again when the cable is refused.
    bool modulateWithNewSource(const juce::String& sourceTypeKey, const juce::String& targetTypeKey,
                               const juce::String& parameterSuffix);
    // The same, its neighbours joined around the gap (editorRemoveFromChain).
    bool removeNodeFromChain(std::uint64_t id);
    bool removeNodeFromChainByType(const juce::String& typeKey);
    [[nodiscard]] std::uint64_t lastNodeIdOfType(const juce::String& typeKey) const;
    // The control port's graph edits: a card placed (positions are the
    // canvas's 0..1; a negative one asks for a free spot), cables made and
    // cut, a card renamed or moved, a comment, and what a type or a card
    // offers — ports and parameter schema.
    [[nodiscard]] std::uint64_t addNodeForRemote(const juce::String& typeKey,
                                                 juce::Point<float> position);
    bool connectNodesForRemote(std::uint64_t source, std::uint64_t destination,
                               std::uint64_t sourcePort,
                               std::uint64_t destinationPort);
    int disconnectNodesForRemote(std::uint64_t source, std::uint64_t destination);
    bool setNodePresentationForRemote(std::uint64_t id, const juce::String& title,
                                      std::optional<juce::Point<float>> position);
    [[nodiscard]] juce::var describeNodeTypeForRemote(const juce::String& typeKey) const;
    [[nodiscard]] juce::var describeNodeForRemote(std::uint64_t id) const;
    [[nodiscard]] std::uint64_t addCommentForRemote(const juce::String& title,
                                                    const juce::String& description,
                                                    juce::Point<float> position,
                                                    juce::Point<float> size);
    bool removeCommentForRemote(std::uint64_t id);
    [[nodiscard]] juce::Result insertFactoryDevice(
        const juce::String& deviceKey);
    // An instrument preset in place of the instrument this graph plays: its
    // voice section (allocator, source, envelope, gain, voice filter) taken
    // out and the preset's put in, wired where the old one was — what fed
    // its notes, the effects after it, the cables other cards plugged into
    // it — so the chain after it, its automation and its modulators stay.
    // With no voice section to replace, the preset is the whole graph.
    [[nodiscard]] juce::Result replaceInstrument(const juce::String& presetKey);
    [[nodiscard]] juce::Result insertNode(const juce::String& nodeTypeKey);
    [[nodiscard]] juce::Result replaceNode(
        const juce::String& currentNodeTypeKey,
        const juce::String& replacementNodeTypeKey);
    // Splices a card into the cable that feeds the output, whatever feeds
    // it. The suite cannot know a loaded preset's topology.
    [[nodiscard]] juce::Result insertNodeBeforeOutput(
        const juce::String& insertedNodeTypeKey);
    // Splices a MIDI card (an arpeggiator, chords…) into the notes' cable
    // that reaches the instrument: after the MIDI cards already there.
    [[nodiscard]] juce::Result insertNodeBeforeInstrument(
        const juce::String& insertedNodeTypeKey);
    [[nodiscard]] juce::Result insertNodeOnConnection(
        const juce::String& sourceNodeTypeKey,
        const juce::String& destinationNodeTypeKey,
        const juce::String& insertedNodeTypeKey);
    [[nodiscard]] juce::Result connectNodePorts(
        const juce::String& sourceNodeTypeKey,
        const juce::String& sourcePortKey,
        const juce::String& destinationNodeTypeKey,
        const juce::String& destinationPortKey,
        bool replaceDestination = false);
    [[nodiscard]] juce::Result addConnectionRoutingPoint(
        const juce::String& sourceNodeTypeKey,
        const juce::String& destinationNodeTypeKey,
        juce::Point<float> graphPoint);
    [[nodiscard]] juce::Result setNodeCollapsed(
        const juce::String& nodeTypeKey, bool collapsed);
    [[nodiscard]] juce::Result createCommentFromSelection(
        const juce::String& title = {});
    [[nodiscard]] juce::Result saveViewBookmark(int slot);
    [[nodiscard]] juce::Result recallViewBookmark(int slot);
    [[nodiscard]] juce::Result clearViewBookmark(int slot);
    [[nodiscard]] bool selectNodePort(const juce::String& nodeTypeKey,
                                      const juce::String& portKey);
    [[nodiscard]] juce::Result openSubgraphEditor(
        const juce::String& moduleKey, bool detached = false);
    [[nodiscard]] juce::Result createEditableGraphModule(
        cydaw::graph::GraphRole role, bool openEditor = true,
        int audioChannels = 2);
    [[nodiscard]] juce::Result createGraphVariant(
        const juce::String& sourceModuleKey,
        const juce::String& name = {},
        bool replaceSelectedInstance = true,
        bool openEditor = true);
    [[nodiscard]] juce::Result saveUserGraphPreset(
        const juce::File& file, const juce::String& name) const;
    [[nodiscard]] juce::Result loadUserGraphPreset(const juce::File& file);
    [[nodiscard]] juce::Result saveNodeParameterPreset(
        const juce::String& nodeTypeKey, const juce::File& file,
        const juce::String& name) const;
    [[nodiscard]] juce::Result loadNodeParameterPreset(
        const juce::String& nodeTypeKey, const juce::File& file);
    void setUserGraphPresetSavedHandler(std::function<void()> handler);
    void addPublishedIoPair(cydaw::engine::PortKind kind,
                            int audioChannels = 2);
    void showGraphInterfaceEditor();
    [[nodiscard]] bool saveGraphInterfaceSnapshot(
        const juce::File& destination) const;
    void showGraphManager();
    void showGraphMembersPanel();
    [[nodiscard]] std::size_t graphMemberCount() const noexcept;
    void setGraphMemberSearch(const juce::String& query);
    [[nodiscard]] std::size_t visibleGraphMemberCount() const noexcept;
    [[nodiscard]] bool setGraphMemberMetadata(
        const juce::String& key, const juce::String& displayName,
        const juce::String& description = {});
    [[nodiscard]] bool setGraphMemberType(
        const juce::String& key, cydaw::engine::PortKind kind,
        int audioChannels = 2);
    [[nodiscard]] bool moveGraphMember(const juce::String& key, int delta);
    [[nodiscard]] bool selectGraphManagerAsset(const juce::String& key);
    void showGraphManagerFileBrowser();
    [[nodiscard]] bool saveGraphManagerSnapshot(
        const juce::File& destination);
    void showNodeBrowser(const juce::String& selectedKey = {});
    [[nodiscard]] bool saveNodeBrowserSnapshot(
        const juce::File& destination) const;
    void setContextName(juce::String name);
    void setGraphAssetScope(cydaw::graph::GraphScope scope);
    [[nodiscard]] cydaw::graph::GraphReference graphReference() const;
    [[nodiscard]] cydaw::graph::GraphRole graphRole() const noexcept;
    [[nodiscard]] cydaw::graph::PluginIdentity pluginExportIdentity() const;
    void setPluginExportIdentity(cydaw::graph::PluginIdentity identity);
    [[nodiscard]] cydaw::graph::PluginExportResult planVst3Export() const;
    [[nodiscard]] juce::var createVst3ExportManifest() const;
    [[nodiscard]] juce::var createVst3RuntimeState() const;
    [[nodiscard]] juce::Result generateVst3Project(
        const juce::File& destination, const juce::String& format = "VST3") const;
    void setEffectParameters(float driveDb, float filterCutoffHz, bool enabled);
    // The long way round, for the suite to compare the short one against.
    void resynchroniseGraphInterfaceForTest() { synchroniseGraphInterface(); }
    void setEffectChangedHandler(std::function<void(float, float)> handler);

    // ---- CyDAW 2 -----------------------------------------------------------
    // The graph as plain data, and the edits an editor drawn elsewhere makes
    // (NodeGraphComponentEditorAccess.cpp). Each edit records history and
    // notifies exactly as one made on this canvas does, so compilation, undo
    // and saving follow without the component ever being shown.
    struct EditorPort {
        std::uint64_t id {};
        juce::String key;
        juce::String name;
        int kind {};      // engine::PortKind: 0 audio, 1 MIDI, 2 control, 3 transport
        int type {};      // graph::PortType
        bool input {};
        int channels { 1 };
        bool multiple {};
        double defaultValue {};
        int exposure { -1 };
        juce::String category;
    };
    struct EditorParameter {
        juce::String binding;
        juce::String label;
        juce::String unit;
        double minimum {};
        double maximum { 1.0 };
        double value {};
        double defaultValue {};
        bool showInDevice { true };
        bool publishToHost { true };
        juce::String description;
        std::uint64_t inputPortId {};
    };
    struct EditorNode {
        std::uint64_t id {};
        juce::String typeKey;
        juce::String title;     // what the card shows: its alias, else the type
        juce::String typeTitle;
        juce::String alias;
        juce::String category;
        juce::String description;
        juce::Colour colour;
        juce::Point<float> position; // canvas fractions, as stored
        bool collapsed {};
        bool bypassed {};
        bool protectedNode {};
        bool composite {};
        // It has a designed interface: a window to open, as a plugin's.
        bool hasInterface {};
        bool boundary {};
        bool invalid {};
        std::vector<EditorPort> ports;
        std::vector<EditorParameter> parameters;
    };
    struct EditorConnection {
        std::uint64_t source {};
        std::uint64_t destination {};
        std::uint64_t sourcePort {};
        std::uint64_t destinationPort {};
        int kind {};
        int channels {};
        int mode {};          // control onto a parameter: 0 set, 1 modulate
        float amount { 0.5f };
        bool drivesParameter {}; // a control cable onto a parameter (not onto a control node)
    };
    struct EditorComment {
        std::uint64_t id {};
        juce::String title;
        juce::String description;
        juce::Point<float> position;
        juce::Point<float> size;
        juce::Colour colour;
    };
    struct EditorNodeType {
        juce::String key;
        juce::String title;
        juce::String category;
        juce::String categoryKey; // the catalogue's, for a tree of its own
        juce::String description;
        juce::Colour colour;
        bool module {};
        juce::String moduleCategory; // a graph's own category ("Effets / Délai et espace")
        bool hidden {};
        int inputKind { -1 };   // -1: none
        int outputKind { -1 };
        // Every kind its sockets take and give (engine::PortKind values).
        std::vector<int> inputKinds;
        std::vector<int> outputKinds;
    };
    [[nodiscard]] std::vector<EditorNode> editorNodes() const;
    // One card's type and its own name (empty when there is no such card),
    // without describing the whole graph as editorNodes does: asked for each
    // card, every frame, that made a large graph's frame cost its square.
    [[nodiscard]] juce::String editorNodeTypeKey(std::uint64_t id) const;
    [[nodiscard]] juce::String editorNodeAlias(std::uint64_t id) const;
    bool editorSetPortExposure(std::uint64_t node, std::uint64_t port, bool exposed);
    bool editorSetPortDetails(std::uint64_t node, std::uint64_t port, const juce::String& name, const juce::String& category);
    bool editorRemoveBoundary(std::uint64_t node);
    // Promote a value/socket in one undoable edit. An input becomes part of
    // the graph contract; a variable is a local editable constant.
    juce::Result editorPromoteValue(std::uint64_t node, std::uint64_t port,
        const juce::String& binding, bool graphInput, std::uint64_t& created);
    [[nodiscard]] const cydaw::engine::AudioAnalysisFrame& editorAnalysis() const noexcept { return lastAnalysisFrame_; }
    // The audio inputs beside the main one (the instrument, else the first:
    // the track's clips), in the graph's order: node, bus (1, 2…). What
    // other tracks are routed into, a sidechain. On an instrument track's
    // graph, every audio input.
    [[nodiscard]] std::vector<std::pair<std::uint64_t, std::uint32_t>> audioInputBuses(
        std::uint64_t renderedInstrumentNodeId = 0) const;
    [[nodiscard]] std::vector<EditorConnection> editorConnections() const;
    [[nodiscard]] std::vector<EditorComment> editorComments() const;
    [[nodiscard]] std::vector<EditorNodeType> editorNodeTypes() const;
    [[nodiscard]] std::vector<EditorParameter> editorGraphParameters() const;
    [[nodiscard]] std::optional<float> editorControlValue(std::uint64_t nodeId) const;
    // Empty when the cable may be drawn; otherwise why not.
    [[nodiscard]] juce::String editorCanConnect(std::uint64_t source, std::uint64_t sourcePort,
                                                std::uint64_t destination,
                                                std::uint64_t destinationPort) const;
    [[nodiscard]] bool editorCanUndo() const noexcept;
    // Undo steps recorded so far (ever growing): what an edit made of
    // several calls took, to take it back exactly.
    [[nodiscard]] std::uint64_t editorHistorySteps() const noexcept { return historySteps_; }
    [[nodiscard]] bool editorCanRedo() const noexcept;
    [[nodiscard]] bool editorLocked() const noexcept { return editingLocked_; }
    std::uint64_t editorAddNode(const juce::String& typeKey, juce::Point<float> position);
    bool editorRemoveNodes(const std::vector<std::uint64_t>& ids);
    // One of audioInputBuses(): an end of the graph, but never its main one.
    bool editorRemoveAudioInput(std::uint64_t id);
    // ---- Devices made of a graph (modules) ------------------------------
    struct EditorModuleInfo {
        juce::String key;
        juce::String name;
        cydaw::graph::GraphRole role { cydaw::graph::GraphRole::audioEffect };
        // A module of the factory library, shared by every graph using it.
        bool factory {};
        // The graph's own (made here, or a copy of a shared one).
        bool inlineModule {};
    };
    // The module a card is an instance of, when it is one.
    [[nodiscard]] std::optional<EditorModuleInfo> editorModuleOf(std::uint64_t nodeId) const;
    // A module's inside as a graph state of its own, to edit in another
    // graph; its changes come back through editorApplyModuleGraphState.
    [[nodiscard]] juce::var editorModuleGraphState(const juce::String& key) const;
    // The edited inside written back: the module and every instance of it.
    void editorApplyModuleGraphState(const juce::String& key, const juce::var& state, bool structural);
    // A shared module made this graph's own before it is edited: the
    // instance plays a copy from now on. The copy's key; the module's own
    // when it already was.
    juce::String editorMakeModuleOwn(std::uint64_t nodeId);
    // Cards of an audio chain made one device: a module of them with an
    // input and an output, put in their place and wired as they were. Its
    // card, or 0 and why not.
    std::uint64_t editorCreateModuleFromSelection(const std::vector<std::uint64_t>& ids, const juce::String& name,
                                                  juce::String& error);
    // A new device, empty, in the chain: an effect (its input wired to its
    // output) or an instrument. Its card.
    std::uint64_t editorCreateEmptyDevice(cydaw::graph::GraphRole role);
    // A device kept as a graph state (a library file) put into the chain as
    // a module of this graph. Its card, 0 when the state holds no graph.
    std::uint64_t editorInsertModuleFromState(const juce::var& state, cydaw::graph::GraphRole role, const juce::String& name);
    // This graph's name, which a module's inside carries back as the
    // module's.
    [[nodiscard]] juce::String editorGraphName() const { return graphName_; }
    // A control cable's way of driving its parameter: set it, or modulate
    // it by `amount` of its range around its own value. One undo step.
    bool editorSetConnectionMode(std::uint64_t source, std::uint64_t sourcePort, std::uint64_t destination,
                                 std::uint64_t destinationPort, int mode, float amount);
    // The socket on a device made of a graph that drives one of its knobs
    // from outside: its id, from the knob's (class) key.
    [[nodiscard]] static cydaw::graph::PortId knobPortId(const juce::String& classKey);
    // What a control cable drives: the knob (its binding on the card, empty
    // when the card shows none) and the range of the parameter behind it.
    struct ControlCableTarget {
        juce::String binding;
        double minimum {};
        double maximum { 1.0 };
    };
    // Why a connection is refused, in the language shown.
    [[nodiscard]] juce::String connectionProblemText(const cydaw::engine::GraphResult& result) const;
    // A mono output to a stereo input (or the reverse): a converter card put
    // between them and wired. Its id, 0 when the widths are not those two.
    std::uint64_t editorConnectThroughConverter(std::uint64_t source, std::uint64_t sourcePort, std::uint64_t destination,
                                                std::uint64_t destinationPort);
    // A card spliced in before the output, through converters when widths
    // differ. False (nothing changed) when there is no such place.
    bool spliceBeforeOutput(std::uint64_t instance);
    // The audio width of a card's socket (0: none or any).
    [[nodiscard]] int editorAudioChannels(std::uint64_t node, std::uint64_t port, bool input) const;
    void editorSetGraphName(const juce::String& name);
    // What a device made of this graph says of itself (saved with it, and
    // carried into a VST3 export).
    struct EditorGraphInfo {
        juce::String name;
        juce::String description;
        juce::String author;
        juce::String category;
    };
    [[nodiscard]] EditorGraphInfo editorGraphInfo() const;
    void editorSetGraphInfo(const EditorGraphInfo& info);
    // The device's programs: its published settings' values (by key) under a
    // name, saved with it and, in a VST3 export, the plugin's programs.
    struct EditorProgram {
        juce::String name;
        std::vector<std::pair<juce::String, double>> values;
    };
    [[nodiscard]] const std::vector<EditorProgram>& editorPrograms() const noexcept { return programs_; }
    // One undo step; the names are trimmed, an empty one is dropped.
    void editorSetPrograms(std::vector<EditorProgram> programs);
    // The published settings as they are now, under a name.
    [[nodiscard]] EditorProgram editorCaptureProgram(const juce::String& name, bool defaults = false) const;
    // Sets the settings a program has (those still published).
    bool editorApplyProgram(const EditorProgram& program);
    // This graph's own interface: the controls a device (and a plugin) made
    // of it shows, where it shows them. Setting keeps the published
    // parameters as they are and takes the controls, the grid and the
    // window; `commit` records an undo step and tells the graph's listeners,
    // otherwise the change is only seen (a drag in progress).
    [[nodiscard]] ModuleInterfaceState editorInterface() const { return graphInterfaceState_; }
    // A card made of a graph: the interface designed for it (its window as a
    // plugin), bound to this card's own settings, their values now.
    [[nodiscard]] ModuleInterfaceState editorNodeInterface(std::uint64_t nodeId) const;
    juce::Result editorSetInterface(const ModuleInterfaceState& state, bool commit);
    // Whether a card is part of the instrument's voice (it plays in the
    // voice engine, not in the chain after it).
    [[nodiscard]] bool editorIsVoiceNode(std::uint64_t id) const;
    // The voice parameter a binding of such a card names, when the voices
    // can follow it (filter cutoff and resonance, wavetable position, level
    // and detune, string damping).
    [[nodiscard]] std::optional<cydaw::engine::PolyVoiceTarget> editorVoiceTargetFor(const juce::String& binding) const;
    // The same, with the instruments' own settings (Prisme's, Kick's, a
    // sampler's, a sound model's): each by its index, for automation lanes.
    [[nodiscard]] std::optional<cydaw::engine::PolyVoiceParameter> editorVoiceParameterFor(const juce::String& binding) const;
    // Where a knob inside a module ("node.12.node.4.lowpass.cutoff") lands
    // in the native graph the chain plays: the node the compiler made for it
    // and the binding on that node itself ("lowpass.cutoff"). Empty for a
    // top-level card's knob (its node keeps the card's id) and for a binding
    // the graph does not publish. The generated id can change when the
    // graph's shape does.
    [[nodiscard]] std::optional<std::pair<std::uint64_t, juce::String>> editorNativeNodeFor(const juce::String& binding) const;
    // A device taken out of a chain, as the device panel does: what fed it
    // is wired to what it fed, so the sound still flows. One undo step.
    bool editorRemoveFromChain(std::uint64_t id);
    // A device moved along its chain, as the device panel's drag does: taken
    // out (what fed it wired to what it fed), then put back just before
    // `before` (0: last, before the chain's output). Its sidechain input and
    // the control cables on its knobs stay. False when it cannot move (an
    // instrument's voice, a card without an audio input and output). One
    // undo step.
    bool editorMoveInChain(std::uint64_t id, std::uint64_t before);
    // While dragging, positions change without history; `commit` ends the
    // gesture and records one step.
    void editorMoveNodes(const std::vector<std::pair<std::uint64_t, juce::Point<float>>>& positions,
                         bool commit);
    // Empty on success, otherwise why the cable was refused. A single input
    // already fed is replaced.
    juce::String editorConnect(std::uint64_t source, std::uint64_t sourcePort,
                               std::uint64_t destination, std::uint64_t destinationPort);
    bool editorDisconnect(std::uint64_t source, std::uint64_t sourcePort,
                          std::uint64_t destination, std::uint64_t destinationPort);
    bool editorSetParameter(const juce::String& binding, double value);
    // ---- Modulators (engine/Modulators.h) -------------------------------
    // A control cable onto a knob: what moves it, by how much.
    struct EditorModulation {
        std::uint64_t source {};
        std::uint64_t sourcePort {};
        std::uint64_t node {};
        std::uint64_t port {};
        juce::String binding;
        int mode {};          // 0 sets the knob, 1 moves it around its value
        float amount {};
    };
    [[nodiscard]] std::vector<EditorModulation> editorModulations() const;
    // A modulator of `typeKey` (a control entry of the effect catalogue) for
    // the card `owner`, made beside it; its id, 0 when refused.
    std::uint64_t editorAddModulator(std::uint64_t owner, const juce::String& typeKey);
    // The card a modulator belongs to; 0 for none (or not a modulator).
    [[nodiscard]] std::uint64_t editorModulatorOwner(std::uint64_t modulator) const;
    // A wavetable card's table, by its id (not the first card of its type).
    [[nodiscard]] cydaw::engine::WavetableShape editorNodeWavetable(std::uint64_t id) const { return nodeWavetable(id); }
    bool editorSetNodeWavetable(std::uint64_t id, const cydaw::engine::WavetableShape& shape) { return setNodeWavetable(id, shape); }
    // The socket of `node` that feeds its knob `binding`; 0 for none.
    [[nodiscard]] std::uint64_t editorParameterSocket(std::uint64_t node, const juce::String& binding) const;
    // A modulator onto the knob `binding`, moving it by `amount` (-1..1 of
    // its range); already there, its amount set. Empty on success, else why not.
    juce::String editorModulate(std::uint64_t modulator, const juce::String& binding, float amount);
    bool editorUnmodulate(std::uint64_t modulator, const juce::String& binding);
    // A cable's amount, turned as a knob is (heard at once, one undo step
    // once it rests): no change of the graph's shape.
    bool editorSetModulationAmount(std::uint64_t source, std::uint64_t sourcePort, std::uint64_t destination,
                                   std::uint64_t destinationPort, float amount);
    // ---- Containers (engine/Containers.h) -------------------------------
    // A container: its head (the card), its end, each branch's cards in the
    // signal's order (a container inside one appears as its head).
    struct EditorContainer {
        std::uint64_t head {};
        // Where its sound leaves it (with a post chain: the last end).
        std::uint64_t end {};
        // With a post chain (XY FX's Post FX): the first end, the branches'
        // mix the post chain follows; 0 without.
        std::uint64_t blend {};
        juce::String typeKey;
        std::vector<std::vector<std::uint64_t>> branches;
        // Which of `branches` is the post chain; -1 for none.
        int postBranch { -1 };
    };
    [[nodiscard]] bool editorIsContainer(std::uint64_t node) const;
    [[nodiscard]] std::optional<EditorContainer> editorContainer(std::uint64_t head) const;
    // Every card inside a container (nested ones' too) and every end: what
    // the chain shows inside its container rather than in its row.
    [[nodiscard]] std::set<std::uint64_t> editorContainerMembers() const;
    // The container a card sits in and which branch (0, -1: the main chain).
    [[nodiscard]] std::pair<std::uint64_t, int> editorPlaceOf(std::uint64_t node) const;
    // A container of `typeKey` before the card `before` (0: the chain's end);
    // its head, 0 when refused.
    std::uint64_t editorInsertContainer(const juce::String& typeKey, std::uint64_t before);
    // A card (a container with all it holds) moved before `before`: in the
    // main chain (`head` 0) or in a branch of a container (`before` 0: at
    // the branch's end). One undo step.
    bool editorMoveCard(std::uint64_t id, std::uint64_t head, int branch, std::uint64_t before);
    // A new card of `typeKey` (a container too) in a branch, before `before`
    // (0: at its end); its id, 0 when refused.
    std::uint64_t editorInsertInBranch(std::uint64_t head, int branch, const juce::String& typeKey, std::uint64_t before);
    // A layer, a selector's chain: one more, or one gone with its cards.
    bool editorAddBranch(std::uint64_t head);
    bool editorRemoveBranch(std::uint64_t head, int branch);
    // A branch's name given by hand (empty: none); naming it (one undo step).
    [[nodiscard]] juce::String editorBranchName(std::uint64_t head, int branch) const;
    bool editorRenameBranch(std::uint64_t head, int branch, const juce::String& name);
    // A whole container, as a preset keeps it: its head, every card it holds
    // (nested containers' too), its ends, their modulators and the cables
    // between them ({format, type, head, end, nodes, connections}); void
    // for none.
    [[nodiscard]] juce::var editorContainerState(std::uint64_t head) const;
    // Such a state in place of what the container holds (the head keeps its
    // id, its place and what feeds it; one undo step).
    bool editorApplyContainerState(std::uint64_t head, const juce::var& state);
    // A whole graph state in place of this one, as one undo step.
    bool editorApplyState(const juce::var& state);
    // A container and all it holds, its signal joined around it.
    bool editorRemoveContainer(std::uint64_t head);
    // When a parameter was last set through editorSetParameter (a knob
    // turned anywhere, a controller): a count that only grows; 0 never.
    [[nodiscard]] std::uint64_t editorParameterTouch(const juce::String& binding) const;
    // A Prisme card's settings, as its own panel reads them every frame.
    [[nodiscard]] std::optional<cydaw::engine::prism::PrismValues> editorPrismValues(std::uint64_t nodeId) const;
    // A Kick card's settings, the same way.
    [[nodiscard]] std::optional<cydaw::engine::kick::KickValues> editorKickValues(std::uint64_t nodeId) const;
    // A synth of the family's card: its model and settings.
    [[nodiscard]] std::optional<std::pair<cydaw::engine::synth::Model, cydaw::engine::synth::Values>>
    editorSynthValues(std::uint64_t nodeId) const;
    // A Batterie card's mix.
    [[nodiscard]] std::optional<cydaw::engine::drum::Values> editorDrumValues(std::uint64_t nodeId) const;
    // A sampler (or Batterie) card's controllers (-1: where its map starts
    // them) and its articulation (-1: the map's own).
    struct SamplerCardControls {
        cydaw::engine::SamplerControllers controllers {};
        float articulation { -1.0f };
        // The simple sampler's card: one sample over the keys.
        bool simple {};
    };
    [[nodiscard]] std::optional<SamplerCardControls> editorSamplerControls(std::uint64_t nodeId) const;
    bool editorSetAlias(std::uint64_t id, const juce::String& alias);
    // A card's own description (what a missing plug-in was: its maker, its
    // format), kept with the graph.
    bool editorSetDescription(std::uint64_t id, const juce::String& description);
    bool editorSetCollapsed(std::uint64_t id, bool collapsed);
    // A card switched off or on (heard at once, an undo step).
    bool editorSetBypassed(std::uint64_t id, bool bypassed);
    std::vector<std::uint64_t> editorDuplicate(const std::vector<std::uint64_t>& ids);
    bool editorUndo();
    bool editorRedo();
    std::uint64_t editorAddComment(const juce::String& title, juce::Point<float> position,
                                   juce::Point<float> size);
    bool editorSetComment(std::uint64_t id, const juce::String& title, juce::Point<float> position,
                          juce::Point<float> size, bool commit);
    bool editorRemoveComment(std::uint64_t id);
    // Arranges the cards by signal flow, as the canvas's own button does.
    void editorArrange();
    // Where each card runs in the compiled graph, for the signal displays:
    // the engine nodes its signal enters and leaves by. A plain card is one
    // node with its own id; a module is the nodes its parameters reach. An
    // instrument card of a MIDI track is the effect chain's input node, so
    // only what leaves it is meaningful.
    struct EditorNativeSpan {
        std::vector<std::uint64_t> entries;
        std::vector<std::uint64_t> exits;
        bool instrument {};
    };
    [[nodiscard]] std::map<std::uint64_t, EditorNativeSpan> editorNativeSpans() const;
    [[nodiscard]] std::map<std::uint64_t, EditorNativeSpan> nativeSpansOf(const cydaw::engine::NativeDspGraph& native) const;
    // The export as the cache holds it, its cards switched off not listed.
    [[nodiscard]] cydaw::engine::NativeDspGraph exportedNativeGraph() const;
    // The control cables that reach the instrument's voices — an LFO on the
    // voice filter's cutoff — by the control node they leave and what they
    // move. Their sources run in the effect chain, which keeps their ids.
    [[nodiscard]] std::vector<cydaw::engine::PolyVoiceModulation> editorVoiceModulations() const;
    // The knobs a control cable drives and the value it delivers now, by
    // binding: what the graph's live values say (setControlValues).
    [[nodiscard]] std::map<juce::String, float> editorModulatedParameters() const;

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;
    bool isInterestedInDragSource(
        const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDragEnter(
        const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDragMove(
        const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDragExit(
        const juce::DragAndDropTarget::SourceDetails& details) override;
    void itemDropped(
        const juce::DragAndDropTarget::SourceDetails& details) override;
    bool isInterestedInFileDrag(
        const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files,
                       int x, int y) override;
    void fileDragMove(const juce::StringArray& files,
                      int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files,
                      int x, int y) override;

private:
    class GraphMembersPanel;

    using NodeId = std::uint64_t;

    // Shapes a wavetable card. Both the stored harmonics and the rendered
    // bank the engine reads are replaced together.
    bool setNodeWavetable(NodeId nodeId,
                          const cydaw::engine::WavetableShape& shape);
    // The first card of a type, for the suite, which has no way to learn an
    // id otherwise.
    [[nodiscard]] NodeId nodeIdForType(const juce::String& typeKey) const;
    // What a card is shaped with, built-in bank included, so the editor
    // always has something to draw and to start from.
    [[nodiscard]] cydaw::engine::WavetableShape nodeWavetable(
        NodeId nodeId) const;
    // Asks for a wavetable file and shapes the card with what it holds.
    void chooseWavetableFile(NodeId nodeId);
    // The reading itself, separated from the asking so the suite can hand it
    // a file without a dialog.
    juce::Result importWavetableFile(NodeId nodeId, const juce::File& file);
    // A wavetable file is a row of single cycles: a handful of kilobytes, not
    // a recording. Refusing the rest before reading keeps a mistaken pick
    // from pulling twenty minutes of audio into memory.
    static constexpr juce::int64 maximumWavetableFileBytes = 8 * 1'024 * 1'024;

    using PortKind = cydaw::engine::PortKind;

    struct NodePort {
        cydaw::graph::PortId id {};
        juce::String key;
        juce::String name;
        cydaw::graph::PortType type { cydaw::graph::PortType::value };
        bool input {};
        std::uint8_t channels { 1 };
        bool acceptsMultipleConnections {};
        double defaultValue {};
        int exposure { -1 };
        juce::String category;
    };

    struct NodeType {
        juce::String key;
        juce::String translationKey;
        juce::String title;
        juce::String categoryKey;
        juce::Colour colour;
        bool hasInput {};
        PortKind inputKind { PortKind::audio };
        bool hasOutput {};
        PortKind outputKind { PortKind::audio };
        bool protectedNode {};
        bool composite {};
        std::uint8_t inputChannels {};
        std::uint8_t outputChannels {};
        juce::String descriptionKey;
        std::vector<NodePort> ports;
        bool stateful {};
        bool supportsPolyphony {};
        bool breaksFeedbackCycle {};
        std::vector<cydaw::graph::NodeParameter> parameterSchema;
    };

    // One line of the palette. The catalogue names its categories
    // "DSP / Filters and EQ": what stands before the slash is the fold, what
    // stands after it is the sub-fold, and the entries live inside. A heading
    // carries no catalogue index.
    struct LibraryRow {
        int catalogIndex { -1 };
        juce::String foldKey;    // the fold a heading opens
        juce::String label;      // heading text
        int depth {};            // indent: 0 heading, 1 sub-heading or
                                 // uncategorised entry, 2 nested entry
        int count {};            // entries under a heading
        bool collapsed {};
    };

    struct Node {
        NodeId id {};
        juce::String typeKey;
        juce::String title;
        juce::Colour colour;
        juce::Point<float> position;
        bool hasInput {};
        PortKind inputKind { PortKind::audio };
        bool hasOutput {};
        PortKind outputKind { PortKind::audio };
        bool protectedNode {};
        bool composite {};
        std::uint8_t inputChannels {};
        std::uint8_t outputChannels {};
        juce::String alias;
        cydaw::engine::NativeDspNodeParameters parameters;
        ModuleInterfaceState interfaceState;
        juce::String description;
        std::vector<NodePort> ports;
        // True only when this reusable-Graph instance owns a custom control
        // surface. Untouched instances continue following the class layout.
        bool interfaceLayoutOverridden {};
        // Editor-only compact presentation. Ports remain available while the
        // control surface is hidden; this never affects DSP compilation.
        bool collapsed {};
        // Switched off: what reaches it goes through untouched (an
        // instrument falls silent). The engine keeps it running, so it comes
        // back without a gap; the graph's shape does not change.
        bool bypassed {};
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
        juce::var soundModelJson;
        // A sampler card's map, and its JSON (the zones, the files by path).
        cydaw::engine::SamplerMapRef samplerMap;
        juce::var samplerJson;
        // A Prisme card's own tables (imported waves), built, and their JSON.
        cydaw::engine::prism::TablesRef prismTables;
        juce::var prismTablesJson;
        // An effect card's impulse response (a cabinet's, loaded), and its JSON.
        cydaw::engine::ImpulseResponseRef impulse;
        juce::var impulseJson;
        // A Replacer's instrument (Containers.h): its own graph, as the
        // studio keeps it ({name, graph}); saved as is.
        juce::var instrumentJson;
        // A container's branches named by hand (by their place; an empty one
        // keeps its usual name).
        juce::StringArray branchNames;
        // What this node last serialised to. A node is a pure function of
        // itself, so the answer only changes when the node does, and finding
        // out costs about forty microseconds — three hundred of those is what
        // made an edit on a large patch expensive. Last in the struct because
        // the fields above are filled by brace initialisation.
        mutable juce::var serialised;
    };

    struct Connection {
        NodeId source {};
        NodeId destination {};
        PortKind kind { PortKind::audio };
        std::uint8_t audioChannels {};
        cydaw::graph::PortId sourcePort {};
        cydaw::graph::PortId destinationPort {};
        // Editor-only waypoints. They never enter the DSP compilation plan.
        std::vector<juce::Point<float>> routingPoints;
        // A control cable onto a parameter: 0 sets it, 1 modulates it (the
        // knob keeps its value, the cable moves it by `amount` of its range).
        std::uint8_t mode {};
        float amount { 0.25f };
    };

    struct GraphComment {
        std::uint64_t id {};
        juce::String title;
        juce::String description;
        juce::Point<float> position;
        juce::Point<float> size { 0.42f, 0.34f };
        juce::Colour colour { 0xff8064c8 };
    };

    struct GraphViewBookmark {
        float zoom { 1.0f };
        juce::Point<float> offset;
    };

    struct PortHit {
        int nodeIndex { -1 };
        cydaw::graph::PortId portId {};

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return nodeIndex >= 0 && portId != 0;
        }
    };

    struct ModuleNode {
        NodeId id {};
        juce::String typeKey;
        juce::String title;
        juce::Point<float> position;
        cydaw::engine::NativeDspNodeParameters parameters;
        std::uint8_t inputChannels {};
        std::uint8_t outputChannels {};
        juce::String alias;
        juce::String description;
        // Values and presentation owned by this particular reusable-Graph
        // instance. The SavedModule remains the immutable class/default.
        ModuleInterfaceState interfaceState;
        bool interfaceLayoutOverridden {};
        // Per-instance exposed pin fallbacks. This is separate from the
        // reusable Graph class and therefore follows the instance when it is
        // copied, saved or nested inside another Graph.
        std::vector<NodePort> ports;
        bool collapsed {};
        // As a top-level card's: a wavetable's table, a sound model (and the
        // JSON it came from). A card made into a device, or a device's inside
        // edited and written back, keeps them.
        std::shared_ptr<const cydaw::engine::WavetableShape> wavetableShape;
        cydaw::engine::WavetableBankRef wavetableBank;
        cydaw::engine::SoundModelRef soundModel;
        juce::var soundModelJson;
        cydaw::engine::SamplerMapRef samplerMap;
        juce::var samplerJson;
        cydaw::engine::prism::TablesRef prismTables;
        juce::var prismTablesJson;
        cydaw::engine::ImpulseResponseRef impulse;
        juce::var impulseJson;
        // As a top-level card's: switched off, a Replacer's instrument (its
        // graph), a container's branch names. Made into a device, or a
        // device's inside written back, a card keeps them.
        bool bypassed {};
        juce::var instrumentJson;
        juce::StringArray branchNames;
    };

    struct SavedModule {
        juce::String key;
        juce::String name;
        cydaw::graph::SemanticVersion version { 1, 0, 0 };
        // Empty for an original class. Variants retain this stable class key
        // so the manager can show lineage without coupling DSP compilation to
        // mutable display names.
        juce::String parentClassKey;
        std::vector<ModuleNode> nodes;
        std::vector<Connection> connections;
        ModuleInterfaceState interfaceState;
        std::vector<EditorProgram> programs;
        bool hasInput { true };
        PortKind inputKind { PortKind::audio };
        bool hasOutput { true };
        PortKind outputKind { PortKind::audio };
        std::uint8_t inputChannels {};
        std::uint8_t outputChannels {};
        bool factoryModule {};
        bool inlineModule {};
        bool locked {};
        juce::String description;
        // Library metadata belongs to the reusable Graph class rather than
        // to one of its instances.  Keeping it beside the class makes the
        // Graph Manager useful for both factory and user-authored libraries.
        juce::String category;
        juce::String author;
        juce::StringArray tags;
        cydaw::graph::GraphRole role { cydaw::graph::GraphRole::generic };
        // The value this class was parsed from, held so that restoring the
        // same value again can reuse the class instead of building it a second
        // time, and so that serialising it again can hand back the very same
        // description. Holding the var keeps the object alive, which is what
        // makes the identity comparison sound. It is dropped the moment
        // anything can have changed the class.
        mutable juce::var source;
        // The document version that description was written in. A body from an
        // older schema must not be echoed into a current one, where its fields
        // would be read under different rules.
        mutable int sourceSchema {};
        // The graph definition this class compiles to, which is a pure
        // function of the class. It is dropped alongside the description
        // above, at the same places and for the same reason.
        mutable std::optional<cydaw::graph::GraphDefinition> definitionCache;
    };

    struct PresetNode {
        juce::String typeKey;
        juce::Point<float> position;
        cydaw::engine::NativeDspNodeParameters parameters;
    };

    struct PresetConnection {
        std::size_t sourceIndex {};
        std::size_t destinationIndex {};
        cydaw::graph::PortId sourcePort {};
        cydaw::graph::PortId destinationPort {};
    };

    struct FactoryPreset {
        juce::String key;
        juce::String translationKey;
        std::vector<PresetNode> nodes;
        std::vector<PresetConnection> connections;
    };

    struct EmbeddedSlider {
        NodeId nodeId {};
        juce::String binding;
        std::unique_ptr<juce::Slider> slider;
        // The map-range feeding this knob, found once at layout: the control
        // values arrive thirty times a second and must not search for it.
        NodeId modulationRangeNodeId {};
    };

    struct EmbeddedButton {
        NodeId nodeId {};
        juce::String binding;
        std::unique_ptr<juce::ToggleButton> button;
    };

    struct EmbeddedCombo {
        NodeId nodeId {};
        juce::String binding;
        std::vector<double> values;
        std::unique_ptr<juce::ComboBox> combo;
    };

    struct EmbeddedXyPad {
        NodeId nodeId {};
        juce::String xBinding;
        juce::String yBinding;
        std::unique_ptr<ModuleXyPadComponent> pad;
    };

    struct EmbeddedWavetable {
        NodeId nodeId {};
        juce::String positionBinding;
        std::unique_ptr<ModuleWavetableComponent> table;
    };

    struct EmbeddedEnvelope {
        NodeId nodeId {};
        std::array<juce::String, 4> bindings;
        std::unique_ptr<ModuleEnvelopeComponent> envelope;
    };

    struct EmbeddedFilterResponse {
        NodeId nodeId {};
        juce::String cutoffBinding;
        juce::String resonanceBinding;
        std::unique_ptr<ModuleFilterResponseComponent> response;
    };

    struct EmbeddedEqCurve {
        NodeId nodeId {};
        // Three bindings per band, in the order the control lists them.
        std::vector<juce::String> bindings;
        std::unique_ptr<ModuleEqCurveComponent> curve;
    };

    struct EmbeddedDynamicsResponse {
        NodeId nodeId {};
        juce::String thresholdBinding;
        juce::String ratioBinding;
        std::unique_ptr<ModuleDynamicsResponseComponent> response;
    };

    struct NativeParameterTarget {
        NodeId instanceNodeId {};
        juce::String parameterKey;
        NodeId nativeNodeId {};
    };

    // The voice section the polyphonic renderer already owns, and the node
    // its sound leaves by. Everything downstream of that node is the effect
    // chain a Live or Bitwig user drops behind their instrument.
    struct InstrumentBoundary {
        std::set<NodeId> voiceNodes;
        NodeId outputNodeId {};
    };
    [[nodiscard]] InstrumentBoundary instrumentVoiceBoundary() const;
    [[nodiscard]] cydaw::engine::WavetableBankRef voiceWavetableBank() const;
    [[nodiscard]] cydaw::engine::SoundModelRef voiceSoundModel() const;
    [[nodiscard]] cydaw::engine::SamplerMapRef voiceSampler() const;
    // That card's controllers (-1: where its map starts them).
    [[nodiscard]] cydaw::engine::SamplerControllers voiceSamplerControllers() const;
    // A plan's sampler: its controllers the card's, and the samples they
    // reach asked for (a microphone raised is decoded before it is played).
    void attachVoiceSampler(cydaw::engine::PolySynthGraphPlan& plan) const;
    [[nodiscard]] cydaw::engine::prism::TablesRef voicePrismTables() const;
    // The first card of a type as the instrument compiler meets them,
    // inside devices too: what `pick` takes from it.
    template <typename Result, typename Pick>
    [[nodiscard]] Result firstVoiceCard(const juce::String& typeKey, Pick pick) const;
    static void applyWavetableHarmonics(
        Node& node, const juce::Array<juce::var>& harmonics);
    [[nodiscard]] cydaw::engine::NativeDspGraph buildNativeDspGraph(
        std::vector<NativeParameterTarget>* parameterTargets,
        NodeId renderedInstrumentNodeId = 0,
        const std::set<NodeId>& renderedVoiceNodes = {}) const;

    void initialiseCatalog();
    void ensureFactoryModules();
    void applyTranslatedTitles();
    // The factory classes installed in this editor, and whether they have been
    // built once already. Building all of them to find out that they are
    // present is the single most expensive thing a restore used to do after
    // the graph itself.
    juce::StringArray factoryModuleKeys_;
    bool factoryModulesInstalled_ {};

public:
    // Where the last restore() spent its time, for the performance bench.
    [[nodiscard]] juce::String restoreProfile() const { return restoreProfile_; }
    [[nodiscard]] juce::String exportProfile() const { return exportProfile_; }
    [[nodiscard]] juce::String definitionsProfile() const
    {
        return "calls=" + juce::String(definitionsCalls_)
               + " lastModules=" + juce::String(definitionsLastModulesMs_, 2)
               + " lastRoot=" + juce::String(definitionsLastRootMs_, 2)
               + " worst=" + juce::String(definitionsWorstMs_, 2)
               + " cached=" + juce::String(definitionsCacheHits_);
    }

private:
    juce::String restoreProfile_;
    mutable juce::String exportProfile_;
    mutable int definitionsCalls_ {};
    mutable int definitionsCacheHits_ {};
    mutable double definitionsWorstMs_ {};
    mutable double definitionsLastModulesMs_ {};
    mutable double definitionsLastRootMs_ {};
    int restoreReusedModules_ {};
    int restoreParsedModules_ {};
    void initialiseFactoryPresets();
    // What a preset's node list says about how it connects to the world.
    // The role inference, the library and the manager all need this, and
    // each used to work it out for itself from a different list of keys.
    struct FactoryPresetBoundaries {
        bool midiInput {};
        bool midiOutput {};
        bool audioInput {};
        bool audioOutput {};
        bool soundSource {};
    };
    [[nodiscard]] FactoryPresetBoundaries factoryPresetBoundaries(
        const FactoryPreset& preset) const;
    [[nodiscard]] cydaw::graph::GraphRole inferFactoryPresetRole(
        const FactoryPreset& preset) const;
    void initialiseDefaultGraph();
    void applyLibraryFilter();
    [[nodiscard]] const NodePort* selectedLibraryPort() const;
    [[nodiscard]] bool libraryTypeMatchesSelectedPort(
        const NodeType& type) const;
    void refreshLibrarySelectionContext();
    void addCatalogNode(int displayRow);
    // Nudges a wanted position until the card does not sit on top of one that
    // is already placed. Used wherever a node is added without the user having
    // chosen a spot for it.
    // Pans just enough to bring a node into view, without touching the zoom.
    void ensureNodeVisible(NodeId nodeId);
    [[nodiscard]] juce::Point<float> freePlacementFor(
        const Node& node, juce::Point<float> preferred,
        NodeId ignoredNodeId = 0) const;
    void addNodeFromType(
        const NodeType& type,
        juce::Point<float> position = { -1'000.0f, -1'000.0f });
    [[nodiscard]] std::optional<std::unordered_map<
        cydaw::graph::PortId, cydaw::graph::PortId>>
        compatibleReplacementPorts(NodeId nodeId,
                                   const NodeType& replacement) const;
    [[nodiscard]] bool replaceNodeType(NodeId nodeId,
                                       const NodeType& replacement);
    [[nodiscard]] bool spliceNodeIntoConnection(
        int connectionIndex, const NodeType& insertedType,
        juce::Point<float> normalisedPosition);
    void removeSelectedNodes();
    void duplicateSelectedNodes();
    // Copies made of cards and their modulators: each copied modulator
    // belongs to its card's copy, not to the original.
    void remapModulatorOwners(const std::unordered_map<NodeId, NodeId>& remapped);
    void copySelectedNodes();
    void pasteCopiedNodes(
        std::optional<juce::Point<float>> targetPosition = std::nullopt);
    void disconnectSelectedNodes();
    [[nodiscard]] bool setInputPortDefault(
        NodeId nodeId, cydaw::graph::PortId portId, double value);
    [[nodiscard]] bool resetInputPortDefault(
        NodeId nodeId, cydaw::graph::PortId portId);
    void disconnectPort(NodeId nodeId, cydaw::graph::PortId portId);
    void createModuleFromSelection();
    void showCreateGraphMenu();
    void showPublishedIoMenu(juce::Component* target);
    void editGraphMemberDetails(const juce::String& key);
    void refreshGraphMembersPanel();
    // Cheap fingerprint of exactly the data the members panel renders, so a
    // layout pass does not rebuild a list that has not changed.
    [[nodiscard]] juce::int64 graphMembersSignature() const;
    juce::int64 graphMembersSignature_ { -1 };
    [[nodiscard]] bool updateGraphMemberParameterVisibility(
        const juce::String& key,
        std::optional<bool> showInDevice,
        std::optional<bool> publishToHost);
    [[nodiscard]] bool insertModuleIntoCurrentSignalFlow(NodeId moduleNodeId);
    [[nodiscard]] cydaw::engine::NativeDspNodeParameters defaultParametersForType(
        const juce::String& typeKey) const;
    [[nodiscard]] ModuleInterfaceState createNodeInterface(const Node& node) const;
    [[nodiscard]] ModuleInterfaceState createGraphInstanceInterface(
        NodeId instanceId, const SavedModule& module) const;
    [[nodiscard]] ModuleInterfaceState createDefaultModuleInterface(
        const SavedModule& module) const;
    [[nodiscard]] ModuleInterfaceState createDefaultGraphInterface() const;
    void synchroniseGraphInterface();
    void rebuildEmbeddedSliders();
    void synchroniseEmbeddedXyPads(
        NodeId nodeId = 0, const juce::String& binding = {});
    void synchroniseEmbeddedWavetables(NodeId nodeId = 0);
    // Six hundred and fifty thousand sines per table. Once per turn of the
    // message loop, never per notch of a drag.
    void rebuildPendingWavetableBanks();
    void synchroniseEmbeddedEqCurves(NodeId nodeId = 0,
                                     const juce::String& binding = {});
    void synchroniseEmbeddedEnvelopes(
        NodeId nodeId = 0, const juce::String& binding = {});
    void synchroniseEmbeddedFilterResponses(
        NodeId nodeId = 0, const juce::String& binding = {});
    void synchroniseEmbeddedDynamicsResponses(
        NodeId nodeId = 0, const juce::String& binding = {});
    [[nodiscard]] const ModuleInterfaceState& presentationForNode(
        const Node& node) const;
    [[nodiscard]] ModuleInterfaceState* editablePresentationForNode(Node& node);
    void openGraphInterfaceEditor();
    [[nodiscard]] bool updateNodeParameter(Node& node,
                                           const juce::String& binding,
                                           double value);
    [[nodiscard]] bool updateModuleParameter(SavedModule& module,
                                             const juce::String& binding,
                                             double value);
    [[nodiscard]] bool updateGraphInstanceParameter(
        Node& node, const juce::String& binding, double value);
    void openNodeInterfaceEditor(NodeId nodeId);
    void openModuleInterfaceEditor(const juce::String& moduleKey);
    void openModuleGraphEditor(const juce::String& moduleKey);
    void openModuleGraphEditorDetached(const juce::String& moduleKey);
    void promptCreateGraphVariant(NodeId nodeId);
    void promptCreateGraphVariant(const juce::String& sourceModuleKey,
                                  bool replaceSelectedInstance);
    void resetSelectedGraphInstanceOverrides();
    void resetSelectedGraphInstanceInterface();
    void closeModuleInterfaceEditor();
    void closeModuleGraphEditor();
    void closeInPlaceModuleGraphEditor(bool commitChanges);
    [[nodiscard]] juce::var moduleGraphState(const SavedModule& module) const;
    void applyModuleGraphState(const juce::String& moduleKey,
                               const juce::var& state,
                               bool structureChanged = true);
    void inferModulePorts(SavedModule& module) const;
    [[nodiscard]] cydaw::graph::GraphRole inferModuleRole(
        const SavedModule& module) const;
    void registerModuleType(const SavedModule& module);
    void loadSelectedFactoryPreset();
    void promptSaveUserGraphPreset();
    void promptSaveNodeParameterPreset(NodeId nodeId);
    void promptLoadNodeParameterPreset(NodeId nodeId);
    [[nodiscard]] juce::Result saveNodeParameterPresetForNode(
        NodeId nodeId, const juce::File& file,
        const juce::String& name) const;
    [[nodiscard]] juce::Result loadNodeParameterPresetForNode(
        NodeId nodeId, const juce::File& file);
    [[nodiscard]] juce::Result validateUserGraphPresetState(
        const juce::var& state) const;
    [[nodiscard]] juce::Result applyFactoryPreset(const FactoryPreset& preset);
    void refreshPresetSelector();
    void arrangeNodesBySignalFlow(bool resetView);
    void alignSelectedNodes(int operation);
    [[nodiscard]] bool graphNodesOverlap() const;
    void resetNodeLayout();
    void addPublishedIoNode(bool input, PortKind kind,
                            int audioChannels = 2);
    void showNodeMenu(NodeId nodeId);
    void showCanvasMenu(juce::Point<float> insertionPoint);
    void createGraphComment(juce::Point<float> insertionPoint,
                            bool encloseSelection);
    void showEditorLayoutMenu();
    void applyEditorLayoutPreset(int preset);
    void saveCustomEditorLayout();
    [[nodiscard]] bool loadCustomEditorLayout(bool apply);
    [[nodiscard]] juce::String viewBookmarkLabel(int slot) const;
    void openNodeBrowser(
        juce::Point<float> insertionPoint,
        NodeId connectionAnchor = 0,
        cydaw::graph::PortId connectionAnchorPort = 0,
        bool anchorIsInput = false,
        NodeId replacementNode = 0,
        int spliceConnection = -1);
    // The origin is a breadcrumb for the bench: one node placement fires this
    // eleven times and reading sixty-five call sites is not how to find out
    // which ones.
    void notifyLayoutChanged(const char* origin = "?");
    void notifyViewChanged();
    void notifyParametersChanged();
    void dispatchParameterNotification();
    void queueParameterNotification();
    void flushParameterNotification();
    void beginParameterGesture();
    void endParameterGesture();
    void commitQueuedParameterHistory();
    void resetEditHistory(const juce::var* knownState = nullptr);
    void commitEditHistory(bool parameterChange);
    void undoGraphEdit();
    void redoGraphEdit();
    void restoreEditHistoryState(const juce::var& state);
    void refreshEditHistoryButtons();
    void timerCallback() override;
    // Folds a burst of view changes into a single layout pass.
    void handleAsyncUpdate() override;
    void refreshGraphAssetValidation();
    // The same pass on the next turn of the message loop. Validation walks
    // every class in the library and only feeds what the cards draw, so it has
    // no business inside the click that caused it.
    void scheduleGraphAssetValidation();
    bool validationPending_ {};
    // The child controls a restored graph needs and the layout that places
    // them. Both only matter to what is drawn, so they are folded into the
    // same coalesced pass a scroll gesture already uses.
    bool restoreUiPending_ {};
    void focusNextValidationIssue();
    void refreshSelectionDetails();
    void refreshGraphMetadataSummary();
    void refreshPublishedParameterDetails();
    void commitPublishedParameterDetails();
    void setPublishedParameterDefaultToCurrent();
    void setPublishedParameterControlsVisible(bool visible);
    [[nodiscard]] ModulePublishedParameter* selectedPublishedParameter();
    [[nodiscard]] const ModulePublishedParameter* selectedPublishedParameter() const;
    void commitSelectedAlias();
    void commitSelectedDescription();
    void commitGraphMetadata();
    void bumpGraphVersion(int component);
    void forkGraphDefinition();
    void checkVst3Export();
    void openVst3ExportPanel();
    void saveVst3ExportManifest(
        const cydaw::graph::PluginIdentity& identity,
        const cydaw::graph::PluginExportPlan& plan);
    void chooseVst3ProjectDirectory(
        const cydaw::graph::PluginIdentity& identity,
        const cydaw::graph::PluginExportPlan& plan);
    [[nodiscard]] cydaw::graph::PluginIdentity defaultVst3ExportIdentity() const;
    [[nodiscard]] cydaw::graph::PluginExportResult planVst3Export(
        const cydaw::graph::PluginIdentity& identity) const;
    void setSelectedAudioChannels(int channels);
    void setSelectedModuleLocked(bool locked);
    void setSelectedNodesCollapsed(bool collapsed);
    void selectConnectedNodes(int direction);
    void selectNodesConnectedToPort(NodeId nodeId, std::uint64_t portId,
                                    bool frameSelection);
    void openSelectedSubgraph();
    void openGraphManager();
    void closeGraphManager();
    void refreshGraphManager();
    void rebuildCategoryFilter();
    void setEditingLocked(bool locked);
    void refreshDspCompilationBadge();
    void setStatus(const juce::String& message, bool error = false);
    void setViewZoom(float zoom, juce::Point<float> anchor);
    bool viewLayoutPending_ { false };
    void resetViewTransform();
    void frameSelectionOrGraph();
    [[nodiscard]] juce::Rectangle<float> minimapBounds() const;
    // What a drag can change on screen. Called once before the move and once
    // after; the union of the two is what needs repainting. A region that
    // forgets something leaves it drawn at its old place, so these are wide.
    [[nodiscard]] juce::Rectangle<int> draggedNodesRegion() const;
    // The body of a node drag, so that the handler and the suite run the same
    // code. Returns the region the move asked to have repainted.
    // A list, not one rectangle: the minimap shows the same node positions at
    // the far corner of the canvas, and one rectangle spanning both would
    // cover nearly everything between them.
    juce::RectangleList<int> moveDraggedNodes(
        juce::Point<float> screenPosition);
    void repaintMinimap();
    [[nodiscard]] juce::Rectangle<int> pendingCableRegion(
        juce::Point<float> end) const;
    [[nodiscard]] juce::Rectangle<float> graphOverviewBounds() const;
    [[nodiscard]] juce::Rectangle<float> visibleWorldBounds() const;
    void paintMinimap(juce::Graphics& graphics) const;
    void setViewFromMinimap(juce::Point<float> position);
    [[nodiscard]] cydaw::graph::GraphRole inferCurrentGraphRole() const;
    [[nodiscard]] juce::String graphRoleLabel() const;
    [[nodiscard]] juce::String nodeTooltip(const Node& node) const;
    [[nodiscard]] juce::String embeddedParameterTooltip(
        const Node& node,
        const juce::String& binding,
        double currentValue) const;
    [[nodiscard]] juce::String graphTooltip() const;
    [[nodiscard]] static juce::String graphAudioBoundaryKey(bool input,
                                                             int channels);
    [[nodiscard]] static bool isGraphAudioBoundaryKey(
        const juce::String& key, bool* input = nullptr);
    [[nodiscard]] static int graphAudioBoundaryChannels(
        const juce::String& key);
    [[nodiscard]] static juce::String audioFormatKey(int channels);

    [[nodiscard]] juce::Rectangle<int> canvasBounds() const;
    [[nodiscard]] bool isBoundaryNode(const Node& node) const;
    [[nodiscard]] juce::String displayTitle(const Node& node) const;
    [[nodiscard]] std::uint8_t audioInputChannels(const Node& node) const;
    [[nodiscard]] std::uint8_t audioOutputChannels(const Node& node) const;
    [[nodiscard]] juce::Point<float> nodeVisualSize(const Node& node) const;
    [[nodiscard]] juce::Rectangle<float> nodeBounds(const Node& node) const;
    [[nodiscard]] juce::Rectangle<float> commentBounds(
        const GraphComment& comment) const;
    [[nodiscard]] GraphComment* findComment(std::uint64_t id);
    [[nodiscard]] const GraphComment* findComment(std::uint64_t id) const;
    [[nodiscard]] std::uint64_t commentAt(
        juce::Point<float> position) const;
    [[nodiscard]] juce::Rectangle<float> nodeContentBounds(const Node& node) const;
    [[nodiscard]] juce::Point<float> graphPointToScreen(
        juce::Point<float> point) const;
    [[nodiscard]] juce::Point<float> screenPointToGraph(
        juce::Point<float> point) const;
    [[nodiscard]] juce::Path connectionPath(const Connection& connection) const;
    [[nodiscard]] int connectionAt(juce::Point<float> position) const;
    [[nodiscard]] std::pair<int, int> connectionRoutingPointAt(
        juce::Point<float> position) const;
    void insertConnectionRoutingPoint(int connectionIndex,
                                      juce::Point<float> screenPosition);
    [[nodiscard]] juce::Point<float> normalisedCanvasPosition(
        juce::Point<float> screenPoint) const;
    [[nodiscard]] juce::Point<float> inputPortPosition(const Node& node) const;
    [[nodiscard]] juce::Point<float> outputPortPosition(const Node& node) const;
    // How large a socket may be drawn on this card: the size the card can
    // actually space its sockets at, never larger than the view's own scale.
    // The hit radius follows it, so a click lands where the eye aims.
    [[nodiscard]] float portDrawScale(const Node& node) const;
    [[nodiscard]] juce::Point<float> portPosition(
        const Node& node, cydaw::graph::PortId portId) const;
    [[nodiscard]] int nodeAt(juce::Point<float> position) const;
    [[nodiscard]] int inputPortAt(juce::Point<float> position) const;
    [[nodiscard]] int outputPortAt(juce::Point<float> position) const;
    [[nodiscard]] PortHit inputPortHit(juce::Point<float> position) const;
    [[nodiscard]] PortHit outputPortHit(juce::Point<float> position) const;
    [[nodiscard]] std::optional<ControlCableTarget> controlCableTarget(const Connection& connection) const;
    // The knob a socket of `node` feeds (its binding), empty for none: an
    // instrument's setting socket, a catalogue effect's.
    [[nodiscard]] juce::String knobOfSocket(const Node& node, const juce::String& portKey) const;
    // The node inside a module that carries one of its knobs (its type key),
    // through the devices it sits in; and that parameter's native range, the
    // one a modulating cable moves it across.
    [[nodiscard]] juce::String knobNodeType(const juce::String& moduleKey, const juce::String& classKey) const;
    [[nodiscard]] std::pair<double, double> knobRange(const juce::String& moduleKey, const juce::String& classKey) const;
    // A cable from an LFO onto a knob modulates it (an LFO swings from -1 to
    // 1); other values set it.
    void applyDefaultCableMode(std::uint64_t source, std::uint64_t destination, std::uint64_t destinationPort);
    [[nodiscard]] const NodePort* findPort(
        const Node& node, cydaw::graph::PortId portId) const;
    [[nodiscard]] static juce::var serialisePortDefaults(
        const std::vector<NodePort>& ports);
    static void restorePortDefaults(const juce::var& state,
                                    std::vector<NodePort>& ports);
    // The sockets that read a named native parameter follow it; the rest
    // — a value node's own inputs — are left as they are.
    static void applyParameterPortDefaults(
        const juce::String& typeKey,
        const cydaw::engine::NativeDspNodeParameters& parameters,
        std::vector<NodePort>& ports);
    static void applyLegacyPortDefaults(
        const juce::String& typeKey,
        const cydaw::engine::NativeDspNodeParameters& parameters,
        std::vector<NodePort>& ports);
    [[nodiscard]] Node* findNode(NodeId id);
    [[nodiscard]] const Node* findNode(NodeId id) const;
    // The same lookup, said out loud: a reader. The mutable overload drops the
    // node's serialised form because it cannot know what the caller will do,
    // and in a non-const method it is the one the compiler picks — which left
    // two caches alive out of three hundred and ten.
    [[nodiscard]] const Node* readNode(NodeId id) const
    {
        return findNode(id);
    }
    [[nodiscard]] const NodeType* findType(const juce::String& key) const;
    [[nodiscard]] SavedModule* findModule(const juce::String& key);
    [[nodiscard]] const SavedModule* findModule(const juce::String& key) const;
    [[nodiscard]] bool connectNodes(
        NodeId source, NodeId destination,
        cydaw::graph::PortId sourcePort = 0,
        cydaw::graph::PortId destinationPort = 0);
    [[nodiscard]] bool connectReplacingInput(
        NodeId source, NodeId destination,
        cydaw::graph::PortId sourcePort,
        cydaw::graph::PortId destinationPort);
    [[nodiscard]] cydaw::engine::GraphResult validateConnection(NodeId source,
                                                                 NodeId destination,
                                                                 cydaw::graph::PortId sourcePort = 0,
                                                                 cydaw::graph::PortId destinationPort = 0) const;
    [[nodiscard]] static juce::Colour portColour(PortKind kind);
    [[nodiscard]] static PortKind enginePortKind(cydaw::graph::PortType type);
    [[nodiscard]] static juce::String portKindKey(PortKind kind);
    [[nodiscard]] static PortKind portKindFromInt(int value);

    // Up and down inside the search box move the highlighted entry without
    // the list ever taking the focus, which is what makes Unreal's node menu
    // usable with one hand on the keyboard.
    struct LibrarySearchKeys : juce::KeyListener {
        explicit LibrarySearchKeys(NodeGraphComponent& owner)
            : owner_(owner) {}
        bool keyPressed(const juce::KeyPress& key,
                        juce::Component* originator) override;
        NodeGraphComponent& owner_;
    };

    void rebuildLibraryRows(const std::vector<std::pair<int, int>>& ranked,
                            bool forceExpanded);
    void toggleLibraryFold(const juce::String& foldKey);
    void moveLibrarySelection(int delta);
    [[nodiscard]] int firstLibraryEntryRow() const;
    [[nodiscard]] const NodeType* libraryRowType(int row) const;
    void paintLibraryHeading(const LibraryRow& entry, juce::Graphics& graphics,
                             int width, int height, bool selected) const;

    int getNumRows() override;
    void paintListBoxItem(int row,
                          juce::Graphics& graphics,
                          int width,
                          int height,
                          bool selected) override;
    [[nodiscard]] juce::String getTooltipForRow(int row) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;
    juce::var getDragSourceDescription(
        const juce::SparseSet<int>& selectedRows) override;

    std::vector<NodeType> catalog_;
    std::vector<int> filteredCatalog_;
    std::vector<LibraryRow> libraryRows_;
    juce::String layoutChangeProfile_;
    juce::String insertProfile_;
    juce::String effectParametersProfile_;
    juce::String addProfile_;
    int layoutChangeCalls_ {};
    juce::String layoutChangeOrigins_;
    mutable juce::String polyCompileProfile_;
    // The typed library the instrument is compiled from, kept between
    // compiles: a knob notch reinstalls its root definition and nothing
    // else, and the library keeps what it knows of the other classes.
    mutable cydaw::graph::GraphLibrary instrumentLibrary_;
    int layoutChangeDepth_ {};
    // Written from the const helper that looks for a free spot.
    mutable juce::String placementProfile_;
    mutable int paintedCards_ {};
    mutable int paintedCables_ {};
    juce::StringArray collapsedLibraryFolds_;
    std::vector<Node> nodes_;
    std::vector<Connection> connections_;
    std::vector<GraphComment> comments_;
    std::vector<Node> copiedNodes_;
    std::vector<Connection> copiedConnections_;
    int clipboardPasteCount_ {};
    std::vector<SavedModule> savedModules_;
    // The last export, kept with a signature of the graph's structure. A knob
    // notch changes values only, so it patches this instead of rebuilding
    // definitions, voice plan and native graph — eight milliseconds at three
    // hundred cards, thirty times a second. A structural edit changes the
    // signature, so nothing has to remember to drop the cache.
    mutable std::optional<RuntimeGraphExport> exportCache_;
    mutable std::uint64_t exportCacheSignature_ {};
    mutable int exportRebuilds_ {};
    mutable juce::String exportCacheProfile_;
    [[nodiscard]] std::uint64_t structuralSignature() const;
    // A control cable dropped on a knob. The engine modulates by value
    // cables at block rate already, but a cable replaces the parameter; what
    // a drag means is base ± depth. So the drop puts a map-range between the
    // source and the knob's value port, set to that span, and from then on
    // the knob shows the span as a ring and sets its centre.
    struct KnobHit {
        NodeId nodeId {};
        juce::String binding;
        juce::Slider* slider {};
        [[nodiscard]] explicit operator bool() const noexcept
        {
            return nodeId != 0;
        }
    };
    [[nodiscard]] KnobHit knobHitForCable(juce::Point<float> position) const;
    juce::String modulateFailure_;
    [[nodiscard]] bool cableCarriesControl() const;
    bool modulateParameter(NodeId sourceNodeId, cydaw::graph::PortId sourcePort,
                           NodeId targetNodeId, const juce::String& binding);
    // The map-range feeding a knob's value port, or zero.
    // The ring's depth, signed, written to the knob's range node about the
    // centre the knob holds.
    void setModulationDepth(NodeId nodeId, const juce::String& binding,
                            double depth);
    // The ring's menu: the range node and its cables go, and the knob keeps
    // the centre it was showing as its value.
    void removeModulation(NodeId nodeId, const juce::String& binding);
    [[nodiscard]] NodeId modulationRangeNodeFor(const Node& node,
                                                const juce::String& binding) const;
    // The cards an analysis frame changes the look of. Everything else the
    // frame leaves exactly as it was, so repainting it was pure cost.
    [[nodiscard]] juce::RectangleList<int> analysisRepaintRegion() const;
    // The EQ curves read a finer spectrum than the cards' bars.
    static constexpr std::size_t eqSpectrumBinCount = 96;
    // Refreshes a cached native graph's parameters from the cards. False when
    // a native node has no card behind it — a module instance was expanded
    // into generated ids — because those cannot be refreshed this way.
    [[nodiscard]] bool patchNativeGraphParameters(
        cydaw::engine::NativeDspGraph& graph) const;
    std::vector<FactoryPreset> factoryPresets_;
    ModuleInterfaceState graphInterfaceState_;
    std::vector<EmbeddedSlider> embeddedSliders_;
    std::vector<EmbeddedButton> embeddedButtons_;
    std::vector<EmbeddedCombo> embeddedCombos_;
    std::vector<EmbeddedXyPad> embeddedXyPads_;
    std::vector<EmbeddedEnvelope> embeddedEnvelopes_;
    std::vector<EmbeddedFilterResponse> embeddedFilterResponses_;
    std::vector<EmbeddedEqCurve> embeddedEqCurves_;
    std::vector<EmbeddedWavetable> embeddedWavetables_;
    // Cards whose table changed and whose bank has not been rebuilt yet.
    std::set<NodeId> wavetableBankPending_;
    std::vector<EmbeddedDynamicsResponse> embeddedDynamicsResponses_;
    std::set<NodeId> selectedNodes_;
    std::uint64_t selectedCommentId_ {};
    std::uint64_t nextCommentId_ { 1 };
    std::array<std::optional<GraphViewBookmark>, 9> viewBookmarks_;
    cydaw::graph::PortId selectedPortId_ {};
    NodeId nextNodeId_ { 1 };
    int nextModuleIndex_ { 1 };
    int draggedNode_ { -1 };
    juce::Point<float> dragOffset_;
    juce::Point<float> nodeDragStartScreen_;
    std::unordered_map<NodeId, juce::Point<float>> nodeDragStartPositions_;
    std::vector<std::pair<int, std::vector<juce::Point<float>>>>
        connectionRoutingDragStartPositions_;
    std::uint64_t draggedCommentId_ {};
    bool resizingComment_ {};
    juce::Point<float> commentDragStartScreen_;
    juce::Point<float> commentDragStartPosition_;
    juce::Point<float> commentDragStartSize_;
    std::unordered_map<NodeId, juce::Point<float>>
        commentNodeDragStartPositions_;
    bool marqueeSelecting_ {};
    juce::Point<float> marqueeStart_;
    juce::Point<float> marqueeEnd_;
    std::set<NodeId> marqueeBaseSelection_;
    int selectedConnection_ { -1 };
    int draggedRoutingConnection_ { -1 };
    int draggedRoutingPoint_ { -1 };
    bool connecting_ {};
    NodeId connectionSource_ {};
    cydaw::graph::PortId connectionSourcePort_ { 2 };
    bool connectionStartedFromInput_ {};
    NodeId connectionHoverTarget_ {};
    cydaw::graph::PortId connectionHoverPort_ { 1 };
    bool connectionHoverCompatible_ {};
    juce::String connectionHoverMessage_;
    juce::Point<float> pendingConnectionEnd_;
    juce::String statusMessage_;
    juce::String contextName_;
    juce::String graphName_;
    juce::String graphDescription_;
    juce::String graphCategory_;
    juce::String graphAuthor_;
    std::vector<EditorProgram> programs_;
    juce::String activeFactoryPresetKey_;
    juce::File currentGraphPresetFile_;
    cydaw::graph::GraphReference graphReference_;
    cydaw::graph::GraphRole graphRole_ { cydaw::graph::GraphRole::audioEffect };
    cydaw::graph::GraphScope graphScope_ { cydaw::graph::GraphScope::project };
    cydaw::graph::PluginIdentity pluginExportIdentity_;
    juce::StringArray graphTags_;
    juce::String graphAssetValidationMessage_;
    std::set<NodeId> invalidGraphNodes_;
    std::set<std::pair<NodeId, cydaw::graph::PortId>> invalidGraphPorts_;
    std::unordered_map<NodeId, juce::String> graphNodeValidationMessages_;
    NodeId lastFocusedValidationNode_ {};
    bool graphAssetValid_ { true };
    bool statusIsError_ {};
    bool dspCompilationKnown_ {};
    bool dspCompilationOk_ {};
    std::size_t dspCompilationNodeCount_ {};
    juce::String dspCompilationError_;
    static constexpr std::size_t spectrumBinCount = 32;
    std::array<float, cydaw::engine::AudioAnalysisFrame::waveformCapacity> analysisWaveform_ {};
    std::array<float, spectrumBinCount> analysisSpectrum_ {};
    // The last spectrum handed to the EQ curves on the cards, for the ones
    // built before the next frame arrives.
    std::array<float, eqSpectrumBinCount> eqSpectrum_ {};
    bool eqSpectrumHeld_ {};
    // And the frame it came from, for a curve built when there was none to
    // hand the spectrum to: zoomed out, the graph has no live curve, and
    // framing a card makes one between two frames.
    cydaw::engine::AudioAnalysisFrame lastAnalysisFrame_ {};
    bool lastAnalysisFrameHeld_ {};
    std::array<float, cydaw::engine::AudioAnalysisFrame::maximumChannels>
        analysisPeak_ {};
    std::array<float, cydaw::engine::AudioAnalysisFrame::maximumChannels>
        analysisRms_ {};
    std::size_t analysisWaveformCount_ {};
    std::size_t analysisChannelCount_ { 2 };
    bool analysisActive_ {};
    std::unordered_map<NodeId, float> controlValues_;

    juce::Label libraryTitle_;
    juce::TextButton selectionPanelToggle_;
    juce::TextButton configurationPanelToggle_;
    juce::TextButton navigationBackButton_;
    // The well painted behind the zoom trio, measured in resized().
    juce::Rectangle<int> graphZoomWell_;
    juce::TextButton zoomOutButton_;
    juce::TextButton frameGraphButton_;
    juce::TextButton zoomInButton_;
    juce::TextButton undoButton_;
    juce::TextButton redoButton_;
    juce::TextButton arrangeGraphButton_;
    juce::TextButton editorLayoutButton_;
    juce::TextButton minimapButton_;
    juce::Label breadcrumbLabel_;
    juce::Label dspCompilationBadge_;
    juce::TextButton vst3CheckButton_;
    juce::TextButton validateGraphButton_;
    juce::TextButton graphConfigurationTab_;
    juce::TextButton graphMembersTab_;
    juce::TextButton graphPaletteTab_;
    juce::Label configurationTitle_;
    juce::Label graphRoleLabel_;
    juce::ComboBox graphRoleSelector_;
    juce::Label graphScopeLabel_;
    juce::ComboBox graphScopeSelector_;
    juce::Label graphNameLabel_;
    juce::TextEditor graphNameEditor_;
    juce::Label graphCategoryLabel_;
    juce::TextEditor graphCategoryEditor_;
    juce::Label graphAuthorLabel_;
    juce::TextEditor graphAuthorEditor_;
    juce::Label graphDescriptionLabel_;
    juce::TextEditor graphDescriptionEditor_;
    juce::Label graphTagsLabel_;
    juce::TextEditor graphTagsEditor_;
    juce::Label metadataTitle_;
    juce::Label metadataName_;
    juce::Label metadataCategory_;
    juce::Label metadataIdentity_;
    juce::Label graphIoTitle_;
    juce::Label graphIoSummary_;
    juce::TextButton graphIoAddButton_;
    juce::Label graphVersionLabel_;
    juce::TextButton bumpMajorVersionButton_;
    juce::TextButton bumpMinorVersionButton_;
    juce::TextButton bumpPatchVersionButton_;
    juce::TextButton forkGraphButton_;
    juce::TextButton graphManagerButton_;
    std::unique_ptr<GraphMembersPanel> graphMembersPanel_;
    juce::ComboBox categoryFilter_;
    juce::TextEditor librarySearch_;
    juce::ToggleButton compatibleOnlyToggle_;
    juce::ListBox libraryList_ { {}, this };
    LibrarySearchKeys librarySearchKeys_ { *this };
    juce::TextButton addNodeButton_;
    juce::TextButton newGraphButton_;
    juce::TextButton createModuleButton_;
    juce::ToggleButton inlineInterfaceEditButton_;
    juce::ComboBox presetSelector_;
    juce::TextButton loadPresetButton_;
    juce::TextButton saveUserPresetButton_;
    juce::Slider driveSlider_;
    juce::Slider cutoffSlider_;
    juce::Label selectionTitle_;
    SelectionSummary selectedTypeLabel_;
    juce::Label aliasLabel_;
    juce::TextEditor aliasEditor_;
    juce::Label nodeDescriptionLabel_;
    juce::TextEditor nodeDescriptionEditor_;
    juce::TextButton saveNodePresetButton_;
    juce::TextButton loadNodePresetButton_;
    juce::Label audioChannelsLabel_;
    juce::ComboBox audioChannelsSelector_;
    juce::Label portDefaultValueLabel_;
    juce::Slider portDefaultValueSlider_;
    juce::ToggleButton moduleLockToggle_;
    juce::TextButton openSubgraphButton_;
    juce::TextButton resetInstanceOverridesButton_;
    juce::Label publishedParametersTitle_;
    juce::ComboBox publishedParameterSelector_;
    juce::Label publishedParameterValueLabel_;
    juce::Slider publishedParameterValueSlider_;
    juce::TextButton publishedParameterSetDefaultButton_;
    juce::Label publishedParameterNameLabel_;
    juce::TextEditor publishedParameterNameEditor_;
    juce::Label publishedParameterIdLabel_;
    juce::TextEditor publishedParameterIdEditor_;
    juce::Label publishedParameterDescriptionLabel_;
    juce::TextEditor publishedParameterDescriptionEditor_;
    juce::ToggleButton showInDeviceToggle_;
    juce::ToggleButton publishToHostToggle_;
    juce::ToggleButton hostAutomatableToggle_;
    juce::ToggleButton graphModulatableToggle_;
    std::unique_ptr<juce::DocumentWindow> moduleEditorWindow_;
    ModuleInterfaceEditorComponent* moduleInterfaceEditor_ {};
    std::unique_ptr<juce::DocumentWindow> moduleGraphEditorWindow_;
    std::unique_ptr<NodeGraphComponent> inPlaceModuleGraphEditor_;
    std::unique_ptr<juce::DocumentWindow> graphManagerWindow_;
    std::unique_ptr<juce::DocumentWindow> vst3ExportWindow_;
    std::unique_ptr<juce::FileChooser> graphPresetChooser_;
    std::unique_ptr<juce::FileChooser> nodeParameterPresetChooser_;
    std::unique_ptr<juce::FileChooser> wavetableChooser_;
    std::unique_ptr<juce::FileChooser> vst3ManifestChooser_;
    std::unique_ptr<juce::FileChooser> vst3ProjectChooser_;
    NodeGraphComponent* moduleGraphEditor_ {};
    juce::Component::SafePointer<juce::Component> nodeBrowserForSnapshot_;
    juce::String inPlaceModuleKey_;
    std::function<void()> navigateBackHandler_;
    GraphManagerComponent* graphManager_ {};
    GraphPluginExportComponent* vst3ExportPanel_ {};
    bool applyingModuleGraphState_ {};
    bool editorParameterChangePending_ {};
    NodeId inlineEditedNode_ {};
    std::uint64_t inlineEditedControl_ {};
    juce::Point<float> inlineControlOffset_;
    bool inlineControlDragging_ {};
    bool inlineControlResizing_ {};
    float viewZoom_ { 1.0f };
    juce::Point<float> viewOffset_;
    // The view a file restored, as a point of the graph: the fraction of the
    // canvas that sits at its centre. Card positions are fractions too, so
    // the graph stretches with the window while a pixel offset does not — a
    // view framed on a wide window opened on empty canvas in a narrow one.
    // Turned into an offset at the first layout, when the canvas is known.
    // Cards keep their pixel width while the canvas stretches, so the point
    // slides a little from card to card; the card nearest the centre and its
    // distance from it are kept too, and that card lands exactly.
    std::optional<juce::Point<float>> restoredViewCentre_;
    NodeId restoredViewAnchor_ {};
    juce::Point<float> restoredViewAnchorDelta_;
    bool applyRestoredViewCentre();
    // The same anchor serves the window: when the canvas changes size the
    // world stretches under a pixel offset, so the card nearest the centre
    // is kept where it was. The view follows the cards, not the pixels.
    struct ViewAnchor {
        NodeId node {};
        juce::Point<float> delta;
    };
    juce::Rectangle<int> lastLaidOutCanvas_;
    void keepViewOnCanvasChange();
    [[nodiscard]] std::optional<ViewAnchor> viewAnchorIn(
        juce::Rectangle<float> canvas, juce::Point<float> offset) const;
    [[nodiscard]] juce::Point<float> viewOffsetForAnchor(
        juce::Rectangle<float> canvas, const Node& node,
        juce::Point<float> delta) const;
    [[nodiscard]] juce::Rectangle<float> nodeBoundsIn(
        juce::Rectangle<float> canvas, const Node& node,
        juce::Point<float> offset) const;
    [[nodiscard]] juce::Point<float> viewCentreNormalised() const;
    [[nodiscard]] juce::Point<float> viewOffsetForCentre(
        juce::Point<float> normalised) const;
    bool panningView_ {};
    bool minimapDragging_ {};
    juce::Point<float> panDragStart_;
    juce::Point<float> panStartOffset_;
    bool valueGraphMode_ {};
    bool nodeDragOver_ {};
    bool graphPresetDragOver_ {};
    bool graphPresetFileDragOver_ {};
    bool nodePresetFileDragOver_ {};
    NodeId nodePresetFileTarget_ {};
    juce::Point<int> nodeDragPosition_;
    juce::String selectedCategoryKey_;
    juce::StringArray categoryKeys_;
    bool refreshingSelectionUi_ {};
    juce::String selectedPublishedParameterKey_;
    bool editingLocked_ {};
    bool parameterNotificationPending_ {};
    bool parameterHistoryPending_ {};
    std::map<juce::String, std::uint64_t> parameterTouches_;
    std::uint64_t parameterTouchCount_ {};
    bool parameterGestureActive_ {};
    juce::int64 lastParameterUiChangeTimeMs_ {};
    // Trees, not text. Stringifying the graph on every edit is what made
    // placing a node cost fifty-five milliseconds.
    std::vector<juce::var> undoHistory_;
    std::uint64_t historySteps_ {};
    std::vector<juce::var> redoHistory_;
    juce::var currentHistoryState_;
    // The snapshot behind currentHistoryState_, taken when the baseline is
    // marked and turned into text only when the history actually needs the
    // text. Converting a graph to JSON costs four times what serialising it
    // does, and a track switch or a pan needs neither.
    juce::var pendingHistoryState_;
    // Marks the point an undo returns to. The caller may hand over a state it
    // already holds; restoring a graph has one, and serialising the graph again
    // to produce an identical one is pure cost.
    void markEditHistoryBaseline(const juce::var* knownState = nullptr);
    [[nodiscard]] const juce::var& editHistoryBaseline();
    bool restoringHistory_ {};
    bool instrumentIsMainInput_ {};
    bool lastHistoryWasParameter_ {};
    juce::int64 lastParameterHistoryTimeMs_ {};
    juce::String copiedNodeParameterType_;
    std::optional<cydaw::engine::NativeDspNodeParameters>
        copiedNodeParameters_;
    std::unordered_map<cydaw::graph::PortId, double>
        copiedNodePortDefaults_;
    static constexpr std::size_t maxEditHistoryStates = 96;
    bool selectionPanelVisible_ { true };
    bool configurationPanelVisible_ { true };
    bool configurationPanelShowsPalette_ { true };
    bool configurationPanelShowsMembers_ {};
    bool minimapVisible_ { true };
    bool resizingSelectionPanel_ {};
    bool resizingConfigurationPanel_ {};
    int selectionPanelWidth_ { 260 };
    int configurationPanelWidth_ { 300 };

    std::function<void()> layoutChangedHandler_;
    std::function<void()> parametersChangedHandler_;
    std::function<void()> userGraphPresetSavedHandler_;
    std::function<void(float, float)> effectChangedHandler_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NodeGraphComponent)
};

} // namespace cydaw::app
