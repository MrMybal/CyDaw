#include "GraphDocument.h"
#include "SamplerMapFile.h"
#include <iostream>
#include <stdexcept>
using namespace cydaw;
void require(bool okay, const char *message) {
  if (!okay)
    throw std::runtime_error(message);
}
int main() {
  try {
    app::GraphDocument graph;
    require(graph.editorNodeTypes().size() > 100,
            "original node catalogue is present");
    require(graph.factoryPresetSummaries().size() > 10,
            "original factory presets are present");
    const auto baseline = graph.editorNodes().size();
    int layoutChanges = 0;
    graph.setLayoutChangedHandler([&] { ++layoutChanges; });
    auto id = graph.editorAddNode("drive", {.4f, .4f});
    require(id != 0 && graph.editorNodes().size() == baseline + 1 &&
                layoutChanges > 0,
            "native document adds real effect and notifies owner");
    auto state = graph.serialise();
    app::GraphDocument restored;
    restored.restore(base::JSON::parse(base::JSON::toString(state)));
    require(restored.editorNodes().size() == graph.editorNodes().size(),
            "graph JSON roundtrip retains all nodes");
    require(restored.editorNodeTypeKey(id) == "drive",
            "graph JSON roundtrip retains effect identity");
    graph.editorUndo();
    require(graph.editorNodes().size() == baseline,
            "native undo restores pre-edit graph");
    graph.editorRedo();
    require(graph.editorNodes().size() == baseline + 1,
            "native redo restores effect");
    require(graph.editorSetAlias(id, "Drive test"), "native node alias edit");
    require(graph.editorNodeAlias(id) == "Drive test",
            "native node alias reads edited state");
    graph.editorArrange();
    graph.processPendingChanges();
    const auto exported = graph.runtimeGraphExport();
    if (exported.compilationResult.failed())
      std::cerr << exported.compilationResult.getErrorMessage() << "\n";
    require(exported.compilationResult.wasOk(),
            "actual native graph compiler accepts model output");
    graph.initialiseEmptyMidiTrackGraph();
    engine::PolySynthGraphPlan instrument;
    require(graph.compilePolySynthGraph(instrument).wasOk(),
            "native instrument compiler accepts empty MIDI graph");
    const auto ambre = graph.editorAddNode("ambre-synth", {.4f, .4f});
    const auto ambreValues = [&] { return graph.editorSynthValues(ambre).value().second; };
    const auto decayIndex = engine::synth::parameterIndex(engine::synth::Model::ambre, "amp.decay");
    const auto sustainIndex = engine::synth::parameterIndex(engine::synth::Model::ambre, "amp.sustain");
    const auto original = ambreValues();
    const auto prefix = "node." + base::Text(static_cast<std::int64_t>(ambre)) + ".ambre.amp.";
    using ParameterValue = app::GraphDocument::ParameterValue;
    const ParameterValue initial[]{{prefix + "decay", original[decayIndex]}, {prefix + "sustain", original[sustainIndex]}};
    const ParameterValue preview[]{{prefix + "decay", 1.5}, {prefix + "sustain", 0.25}};
    const auto history = graph.editorHistorySteps();
    const auto revision = graph.editorParameterRevision();
    graph.editorBeginParameterGesture(initial);
    graph.editorPreviewParameters(preview);
    graph.processPendingChanges();
    require(graph.editorHistorySteps() == history && graph.editorParameterRevision() == revision,
            "Ambre preview keeps history and gesture revision stable");
    require(ambreValues()[decayIndex] == 1.5f && ambreValues()[sustainIndex] == 0.25f,
            "both parameters are present in the live graph");
    graph.editorEndParameterGesture(false);
    require(ambreValues() == original && graph.editorHistorySteps() == history,
            "cancelling a preview restores both parameters without a history entry");
    graph.editorBeginParameterGesture(initial);
    graph.editorPreviewParameters(preview);
    graph.editorEndParameterGesture(true);
    require(graph.editorHistorySteps() == history + 1 && graph.editorUndo() && ambreValues() == original,
            "one graph undo restores both parameters of a committed gesture");
    graph.editorBeginParameterGesture(initial);
    graph.editorPreviewParameters(preview);
    const auto beforePreset = graph.editorParameterRevision();
    graph.editorInvalidateParameterGesture();
    require(graph.editorParameterRevision() != beforePreset && ambreValues() == original,
            "external replacement invalidates and restores the current preview first");
    require(base::Uuid().toString().length() == 32,
            "graph asset identifiers retain 128-bit encoding");
    app::SampleStore::shutdown();
    std::cout << "Original graph catalogue, edits, history, persistence and "
                 "compilers passed without JUCE\n";
    return 0;
  } catch (const std::exception &error) {
    app::SampleStore::shutdown();
    std::cerr << error.what() << '\n';
    return 1;
  }
}
