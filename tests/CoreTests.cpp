#include <cydaw/engine/AudioGraph.h>
#include <cydaw/engine/Containers.h>
#include <cydaw/engine/PolySynthGraph.h>
#include <cydaw/engine/EffectCatalogue.h>
#include "../src/native_plugin/NativeRuntime.h"
#include <cydaw/engine/GraphDefinitionCompiler.h>
#include <cydaw/engine/GraphInstrumentCompiler.h>
#include <cydaw/engine/NativeDspCompiler.h>
#include <cydaw/graph/GraphDefinition.h>
#include <cydaw/graph/GraphPluginExport.h>
#include <cydaw/graph/PrimitiveRegistry.h>
#include <cydaw/platform/AudioBackend.h>
#include <cydaw/plugins/PluginIsolation.h>
#include <cydaw/modules/ModuleLibrary.h>
#include <cydaw/ui/WorkspaceLayout.h>

#include <algorithm>
#include <iostream>
#include <numbers>
#include <ranges>
#include <string_view>

namespace {

int failures = 0;

void expect(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

cydaw::engine::NodeDescriptor stereoNode(const cydaw::engine::NodeId id, const char* name)
{
    return { id, name, 1, 1, 1, 1, 1, 1 };
}

void graphCompilesInStableOrder()
{
    using namespace cydaw::engine;
    AudioGraph graph;
    expect(static_cast<bool>(graph.addNode(stereoNode(30, "Output"))), "add output");
    expect(static_cast<bool>(graph.addNode(stereoNode(10, "Instrument"))), "add instrument");
    expect(static_cast<bool>(graph.addNode(stereoNode(20, "Effect"))), "add effect");

    expect(static_cast<bool>(graph.connect({ { 10, 0, PortKind::audio }, { 20, 0, PortKind::audio } })),
           "connect instrument to effect");
    expect(static_cast<bool>(graph.connect({ { 20, 0, PortKind::audio }, { 30, 0, PortKind::audio } })),
           "connect effect to output");

    const auto result = graph.compile();
    expect(static_cast<bool>(result), "graph compiles");
    expect(result.graph.has_value(), "compiled plan is returned");
    expect(result.graph->processingOrder == std::vector<NodeId>({ 10, 20, 30 }), "topological order is deterministic");
}

void cyclesAreRejectedWithoutMutatingTheGraph()
{
    using namespace cydaw::engine;
    AudioGraph graph;
    expect(static_cast<bool>(graph.addNode(stereoNode(1, "A"))), "add cycle node A");
    expect(static_cast<bool>(graph.addNode(stereoNode(2, "B"))), "add cycle node B");
    expect(static_cast<bool>(graph.connect({ { 1, 0, PortKind::audio }, { 2, 0, PortKind::audio } })), "add forward edge");

    const auto generationBeforeFailure = graph.generation();
    const auto result = graph.connect({ { 2, 0, PortKind::audio }, { 1, 0, PortKind::audio } });
    expect(!static_cast<bool>(result), "cycle is rejected");
    expect(result.error == GraphError::cycleDetected, "cycle reports the expected error");
    expect(graph.connections().size() == 1, "failed connection is rolled back");
    expect(graph.generation() == generationBeforeFailure, "failed edit does not change generation");
}

void invalidPortsAndKindsAreRejected()
{
    using namespace cydaw::engine;
    AudioGraph graph;
    expect(static_cast<bool>(graph.addNode(stereoNode(1, "A"))), "add validation node A");
    expect(static_cast<bool>(graph.addNode(stereoNode(2, "B"))), "add validation node B");

    const auto missingPort = graph.connect({ { 1, 9, PortKind::audio }, { 2, 0, PortKind::audio } });
    expect(missingPort.error == GraphError::invalidPort, "invalid output port is rejected");

    const auto wrongKind = graph.connect({ { 1, 0, PortKind::midi }, { 2, 0, PortKind::audio } });
    expect(wrongKind.error == GraphError::incompatiblePortKinds, "incompatible port kinds are rejected");

    NodeDescriptor transportSource;
    transportSource.id = 3;
    transportSource.name = "Transport source";
    transportSource.transportOutputs = 1;
    NodeDescriptor transportDestination;
    transportDestination.id = 4;
    transportDestination.name = "Transport destination";
    transportDestination.transportInputs = 1;
    expect(static_cast<bool>(graph.addNode(std::move(transportSource)))
               && static_cast<bool>(graph.addNode(std::move(transportDestination)))
               && static_cast<bool>(graph.connect(
                   { { 3, 0, PortKind::transport },
                     { 4, 0, PortKind::transport } })),
           "transport ports are distinct, connectable graph data");
}

void professionalBackendsAreDeclared()
{
    using namespace cydaw::platform;
    const auto& backends = knownAudioBackends();
    const auto contains = [&backends](const AudioBackend backend) {
        for (const auto& candidate : backends)
            if (candidate.id == backend)
                return true;
        return false;
    };

    expect(contains(AudioBackend::asio), "ASIO backend is part of the platform contract");
    expect(contains(AudioBackend::jack), "JACK backend is part of the platform contract");
    expect(contains(AudioBackend::coreAudio), "CoreAudio backend is part of the platform contract");
    expect(contains(AudioBackend::alsa), "ALSA backend is part of the platform contract");
}

void pluginCrashesAreContainedAndQuarantined()
{
    using namespace cydaw::plugins;
    PluginWorkerState worker({ 2, 60, 2'000 });
    worker.beginStart();
    worker.markRunning();
    expect(worker.state() == WorkerState::running, "plugin worker reaches running state");

    worker.markCrashed();
    expect(worker.beginAutomaticRestart(), "first crash is restarted");
    worker.markRunning();
    worker.markCrashed();
    expect(worker.beginAutomaticRestart(), "second crash is restarted");
    worker.markRunning();
    worker.markCrashed();
    expect(!worker.beginAutomaticRestart(), "repeated crash is quarantined");
    expect(worker.state() == WorkerState::quarantined, "bad plugin cannot crash-loop the DAW");
}

void reusableModulesCompileDependenciesFirst()
{
    using namespace cydaw::modules;
    ModuleLibrary library;
    ModuleDefinition delay { 1, "core.delay", "Delay", ModuleCategory::buildingBlock };
    ModuleDefinition reverb { 2, "factory.reverb", "Reverb", ModuleCategory::audioEffect };
    reverb.dependencies = { 1 };
    reverb.parameters.push_back({ 10, "mix", "Mix", "%", 0.0, 100.0, 25.0, 42, "wet" });
    reverb.interface.push_back({ 20, InterfaceElementKind::knob, "Mix", 10, 0.0f, 0.0f, 1.0f, 1.0f });
    ModuleDefinition instrument { 3, "factory.polysynth", "PolySynth", ModuleCategory::instrument };
    instrument.dependencies = { 2 };

    expect(static_cast<bool>(library.add(std::move(delay))), "add reusable delay module");
    expect(static_cast<bool>(library.add(std::move(reverb))), "add reusable reverb module with custom UI");
    expect(static_cast<bool>(library.add(std::move(instrument))), "compose reverb into an instrument");
    const auto compiled = library.compile();
    expect(compiled.has_value(), "module library compiles");
    expect(compiled->loadOrder == std::vector<ModuleId>({ 1, 2, 3 }), "dependencies load before composed modules");
}

cydaw::graph::GraphPort graphPort(
    const cydaw::graph::PortId id, const char* key,
    const cydaw::graph::PortType type,
    const cydaw::graph::PortDirection direction,
    const cydaw::graph::PortRate rate,
    const std::uint16_t channels = 1)
{
    cydaw::graph::GraphPort port;
    port.id = id;
    port.key = key;
    port.displayName = key;
    port.type = type;
    port.audioType = type == cydaw::graph::PortType::audio
                         ? cydaw::graph::audioPortTypeForChannels(channels)
                         : cydaw::graph::AudioPortType::unspecified;
    port.direction = direction;
    port.rate = rate;
    port.channels = channels;
    return port;
}

cydaw::graph::GraphDefinition stereoEffectDefinition()
{
    using namespace cydaw::graph;
    GraphDefinition definition;
    definition.reference = { "factory.stereo-delay", { 1, 0, 0 } };
    definition.displayName = "Stereo Delay";
    definition.role = GraphRole::audioEffect;
    definition.scope = GraphScope::factory;
    definition.ports = {
        graphPort(1, "audio-in", PortType::audio, PortDirection::input,
                  PortRate::audio, 2),
        graphPort(2, "audio-out", PortType::audio, PortDirection::output,
                  PortRate::audio, 2)
    };
    GraphNode delay;
    delay.id = 10;
    delay.displayName = "Delay";
    delay.processorKey = "core.delay";
    delay.ports = {
        graphPort(101, "in", PortType::audio, PortDirection::input,
                  PortRate::audio, 2),
        graphPort(102, "out", PortType::audio, PortDirection::output,
                  PortRate::audio, 2)
    };
    delay.parameters = { { "mix", 0.0, 1.0, 0.25 } };
    definition.nodes.push_back(std::move(delay));
    definition.connections = {
        { { boundaryNodeId, 1 }, { 10, 101 } },
        { { 10, 102 }, { boundaryNodeId, 2 } }
    };
    definition.parameters.push_back(
        { 201, "mix", "Mix", "%", 0.0, 1.0, 0.25, 10, "mix" });
    GraphInterface device;
    device.id = "device";
    device.displayName = "Device";
    device.width = 320.0f;
    device.height = 180.0f;
    device.controls.push_back(
        { 301, InterfaceControlKind::knob, "Mix", 201, {},
          20.0f, 20.0f, 100.0f, 100.0f });
    definition.interfaces.push_back(std::move(device));
    return definition;
}

void graphDefinitionsComposeTypedReusableDevices()
{
    using namespace cydaw::graph;
    GraphLibrary library;
    auto effect = stereoEffectDefinition();
    expect(static_cast<bool>(library.install(effect)),
           "install a typed reusable effect graph");

    GraphDefinition instrument;
    instrument.reference = { "factory.graph-poly", { 1, 2, 0 } };
    instrument.displayName = "Graph Poly";
    instrument.role = GraphRole::instrument;
    instrument.scope = GraphScope::factory;
    instrument.ports = {
        graphPort(1, "midi-in", PortType::midi, PortDirection::input,
                  PortRate::event),
        graphPort(2, "audio-out", PortType::audio, PortDirection::output,
                  PortRate::audio, 2)
    };
    GraphNode oscillator;
    oscillator.id = 10;
    oscillator.displayName = "Poly oscillator";
    oscillator.processorKey = "core.poly-oscillator";
    oscillator.ports = {
        graphPort(101, "midi", PortType::midi, PortDirection::input,
                  PortRate::event),
        graphPort(102, "audio", PortType::audio, PortDirection::output,
                  PortRate::audio, 2)
    };
    GraphNode nestedEffect;
    nestedEffect.id = 20;
    nestedEffect.displayName = "Stereo Delay";
    nestedEffect.kind = GraphNodeKind::graphInstance;
    nestedEffect.graph = effect.reference;
    instrument.nodes = { oscillator, nestedEffect };
    instrument.connections = {
        { { boundaryNodeId, 1 }, { 10, 101 } },
        { { 10, 102 }, { 20, 1 } },
        { { 20, 2 }, { boundaryNodeId, 2 } }
    };
    instrument.parameters.push_back(
        { 201, "delay-mix", "Delay mix", "%", 0.0, 1.0, 0.25,
          20, "mix" });

    expect(inferGraphRole(instrument) == GraphRole::instrument,
           "MIDI to audio ports infer an instrument graph");
    expect(static_cast<bool>(library.install(instrument)),
           "an instrument composes a reusable effect graph");

    GraphDefinition modulator;
    modulator.reference = { "factory.lfo", { 1, 0, 0 } };
    modulator.displayName = "LFO";
    modulator.role = GraphRole::modulator;
    modulator.scope = GraphScope::factory;
    modulator.ports = {
        graphPort(1, "value", PortType::value, PortDirection::output,
                  PortRate::control)
    };
    GraphNode lfo;
    lfo.id = 10;
    lfo.displayName = "LFO";
    lfo.processorKey = "core.lfo";
    lfo.ports = {
        graphPort(101, "value", PortType::value, PortDirection::output,
                  PortRate::control)
    };
    modulator.nodes = { lfo };
    modulator.connections = { { { 10, 101 }, { boundaryNodeId, 1 } } };
    expect(inferGraphRole(modulator) == GraphRole::modulator,
           "a value-only graph is a modulator");
    expect(static_cast<bool>(library.install(std::move(modulator))),
           "install a value-output modulator graph");

    ValidationResult validation;
    const auto compiled = library.compile(&validation);
    expect(validation.ok() && compiled.has_value(),
           "typed graph library validates and compiles");
    expect(compiled->loadOrder.size() == 3,
           "compiled graph catalog contains all exact versions");
    const auto effectPosition = std::ranges::find(compiled->loadOrder,
                                                  effect.reference);
    const auto instrumentPosition = std::ranges::find(
        compiled->loadOrder, instrument.reference);
    expect(effectPosition < instrumentPosition,
           "nested graph definition loads before its consumer");

    GraphInstance instance;
    instance.id = 99;
    instance.definition = effect.reference;
    instance.parameterOverrides[201] = 0.75;
    expect(static_cast<bool>(library.validate(instance)),
           "graph instances retain only valid per-instance parameter values");
    instance.parameterOverrides[201] = 4.0;
    expect(!static_cast<bool>(library.validate(instance)),
           "graph instance overrides remain inside the published range");
}

void graphLibraryKeepsWhatItKnowsAcrossReinstalls()
{
    using namespace cydaw::graph;
    // A library asked to compile after every knob notch validated and
    // fingerprinted every class it held, each time. It keeps what it worked
    // out about a definition until that definition — or one it depends on —
    // changes; installing a definition as it already stands is nothing; and
    // removing a dependency reaches the verdicts of what nested it.
    GraphLibrary library;
    const auto effect = stereoEffectDefinition();
    expect(static_cast<bool>(library.install(effect)),
           "install the effect the reinstall test nests");

    GraphDefinition instrument;
    instrument.reference = { "factory.graph-poly-reinstall", { 1, 0, 0 } };
    instrument.displayName = "Graph Poly";
    instrument.role = GraphRole::instrument;
    instrument.scope = GraphScope::factory;
    instrument.ports = {
        graphPort(1, "midi-in", PortType::midi, PortDirection::input,
                  PortRate::event),
        graphPort(2, "audio-out", PortType::audio, PortDirection::output,
                  PortRate::audio, 2)
    };
    GraphNode oscillator;
    oscillator.id = 10;
    oscillator.displayName = "Poly oscillator";
    oscillator.processorKey = "core.poly-oscillator";
    oscillator.ports = {
        graphPort(101, "midi", PortType::midi, PortDirection::input,
                  PortRate::event),
        graphPort(102, "audio", PortType::audio, PortDirection::output,
                  PortRate::audio, 2)
    };
    GraphNode nestedEffect;
    nestedEffect.id = 20;
    nestedEffect.displayName = "Stereo Delay";
    nestedEffect.kind = GraphNodeKind::graphInstance;
    nestedEffect.graph = effect.reference;
    instrument.nodes = { oscillator, nestedEffect };
    instrument.connections = {
        { { boundaryNodeId, 1 }, { 10, 101 } },
        { { 10, 102 }, { 20, 1 } },
        { { 20, 2 }, { boundaryNodeId, 2 } }
    };
    expect(static_cast<bool>(library.install(instrument)),
           "install the instrument that nests the effect");

    ValidationResult validation;
    const auto first = library.compile(&validation);
    expect(validation.ok() && first.has_value(),
           "the library compiles before any reinstall");
    expect(library.holds(instrument),
           "the library holds the instrument as it stands");
    expect(static_cast<bool>(library.install(instrument, true)),
           "reinstalling an unchanged definition is accepted");
    const auto again = library.compile(&validation);
    expect(validation.ok() && again.has_value()
               && again->fingerprint == first->fingerprint,
           "reinstalling an unchanged definition keeps the compile");

    // The effect changes underneath: its input port is 3 now, not 1, and
    // the effect is valid on its own. The instrument, untouched, still
    // cables into port 1 of it: its verdict was reached against the old
    // effect and has to be reached again. Nothing but the cache's own
    // invalidation can see this — a missing definition, the dependency
    // walk would catch on its own.
    auto renumbered = effect;
    for (auto& port : renumbered.ports)
        if (port.id == 1)
            port.id = 3;
    for (auto& connection : renumbered.connections)
        if (connection.source.nodeId == boundaryNodeId
            && connection.source.portId == 1)
            connection.source.portId = 3;
    expect(!library.holds(renumbered),
           "the library sees the renumbered effect as a change");
    expect(static_cast<bool>(library.install(renumbered, true)),
           "the renumbered effect installs, valid on its own");
    ValidationResult stale;
    const auto underneath = library.compile(&stale);
    expect(!underneath.has_value(),
           "a dependency's change reaches the verdict of what nested it");
    expect(static_cast<bool>(library.install(effect, true)),
           "the effect is put back as it was");
    const auto mended = library.compile(&validation);
    expect(validation.ok() && mended.has_value()
               && mended->fingerprint == first->fingerprint,
           "a dependency put back restores the compile");

    // The effect goes; the instrument, untouched, now nests nothing.
    expect(library.remove(effect.reference),
           "the nested effect is removed from the library");
    expect(!library.holds(effect), "the library no longer holds the effect");
    ValidationResult missing;
    const auto broken = library.compile(&missing);
    expect(!broken.has_value()
               && std::ranges::any_of(
                   missing.issues, [](const ValidationIssue& issue) {
                       return issue.code == ValidationCode::missingDependency;
                   }),
           "removing a dependency reaches the verdict of what nested it");

    // And back: the same library, the same fingerprint.
    expect(static_cast<bool>(library.install(effect)),
           "the effect is installed again");
    const auto restored = library.compile(&validation);
    expect(validation.ok() && restored.has_value()
               && restored->fingerprint == first->fingerprint,
           "a restored dependency restores the compile");
}

void graphAssetsProduceAStableVst3WrapperContract()
{
    using namespace cydaw::graph;
    GraphLibrary library;
    const auto effect = stereoEffectDefinition();
    expect(static_cast<bool>(library.install(effect)),
           "install graph selected for VST3 export");

    const PluginIdentity identity {
        "Stereo Delay Graph", "CyDAW", "CyDw", "StD1", { 1, 0, 0 }
    };
    const auto exported = planVst3Export(library, effect.reference, identity);
    expect(static_cast<bool>(exported) && exported.plan.has_value(),
           "a valid effect Graph produces a VST3 wrapper contract");
    if (exported.plan.has_value()) {
        const auto& plan = *exported.plan;
        expect(plan.role == GraphRole::audioEffect && !plan.isSynth
                   && !plan.acceptsMidi && !plan.producesMidi,
               "VST3 export preserves the Graph effect role");
        expect(plan.buses.size() == 2
                   && plan.buses[0].audioType == AudioPortType::stereo
                   && plan.buses[0].channels == 2
                   && plan.buses[0].direction == PortDirection::input
                   && plan.buses[1].audioType == AudioPortType::stereo
                   && plan.buses[1].direction == PortDirection::output,
               "VST3 bus declarations preserve exact Graph audio types");
        expect(plan.parameters.size() == 1
                   && plan.parameters[0].pluginParameterId == 201
                   && plan.parameters[0].graphParameterId == 201
                   && plan.parameters[0].key == "mix",
               "published Graph parameters keep stable VST3 identifiers");
        expect(plan.hasEditor && plan.sourceFingerprint != 0
                   && plan.vst3Category == "Fx",
               "VST3 wrapper contract includes UI, category and content fingerprint");
    }

    auto surroundEffect = effect;
    surroundEffect.reference = { "factory.surround-delay", { 1, 0, 0 } };
    surroundEffect.displayName = "5.1 Delay";
    for (auto& port : surroundEffect.ports) {
        port.channels = 6;
        port.audioType = AudioPortType::channels6;
    }
    for (auto& port : surroundEffect.nodes.front().ports)
        if (port.type == PortType::audio) {
            port.channels = 6;
            port.audioType = AudioPortType::channels6;
        }
    GraphLibrary surroundLibrary;
    expect(static_cast<bool>(surroundLibrary.install(surroundEffect)),
           "install exact 5.1 Graph for VST3 export");
    const auto surroundExport = planVst3Export(
        surroundLibrary, surroundEffect.reference,
        { "5.1 Delay Graph", "CyDAW", "CyDw", "SuD1", { 1, 0, 0 } });
    expect(surroundExport.ok() && surroundExport.plan.has_value()
               && surroundExport.plan->buses.size() == 2
               && std::ranges::all_of(
                   surroundExport.plan->buses, [](const auto& bus) {
                       return bus.mediaType == PluginBusMediaType::audio
                              && bus.audioType == AudioPortType::channels6
                              && bus.channels == 6;
                   }),
           "VST3 wrapper preserves exact 5.1 input and output bus contracts");

    auto parameterVariant = effect;
    parameterVariant.parameters.front().defaultValue = 0.61;
    GraphLibrary parameterVariantLibrary;
    expect(static_cast<bool>(parameterVariantLibrary.install(parameterVariant)),
           "install parameter variant for Graph fingerprint validation");
    const auto parameterVariantExport = planVst3Export(
        parameterVariantLibrary, parameterVariant.reference, identity);
    auto interfaceVariant = effect;
    interfaceVariant.interfaces.front().controls.front().x += 12.0f;
    GraphLibrary interfaceVariantLibrary;
    expect(static_cast<bool>(interfaceVariantLibrary.install(interfaceVariant)),
           "install interface variant for Graph fingerprint validation");
    const auto interfaceVariantExport = planVst3Export(
        interfaceVariantLibrary, interfaceVariant.reference, identity);
    expect(exported.plan.has_value() && parameterVariantExport.plan.has_value()
               && interfaceVariantExport.plan.has_value()
               && exported.plan->sourceFingerprint
                      != parameterVariantExport.plan->sourceFingerprint
               && exported.plan->sourceFingerprint
                      != interfaceVariantExport.plan->sourceFingerprint,
           "Graph fingerprint changes with runtime parameters and authored UI");
    const auto compiledOriginal = library.compile();
    const auto compiledInterfaceVariant = interfaceVariantLibrary.compile();
    expect(compiledOriginal.has_value()
               && compiledInterfaceVariant.has_value()
               && compiledOriginal->definitions.front().fingerprint
                      == compiledInterfaceVariant->definitions.front().fingerprint
               && compiledOriginal->definitions.front().assetFingerprint
                      != compiledInterfaceVariant->definitions.front().assetFingerprint,
           "interface edits preserve the DSP cache fingerprint while changing the asset fingerprint");

    auto partlyPublished = effect;
    partlyPublished.reference.id = "factory.partly-published-delay";
    partlyPublished.parameters.push_back(
        { 202, "internal-tone", "Internal tone", "", -1.0, 1.0, 0.0,
          10, "mix", true, true, true, false });
    GraphLibrary publicationLibrary;
    expect(static_cast<bool>(publicationLibrary.install(partlyPublished)),
           "install Graph containing a device-only parameter");
    const auto publicationPlan = planVst3Export(
        publicationLibrary, partlyPublished.reference, identity);
    expect(publicationPlan.ok() && publicationPlan.plan.has_value()
               && publicationPlan.plan->parameters.size() == 1
               && publicationPlan.plan->parameters.front().pluginParameterId
                      == 201,
           "VST3 contract exports only parameters explicitly published to the host");

    auto xyInterfaceEffect = effect;
    xyInterfaceEffect.reference.id = "factory.xy-interface-delay";
    xyInterfaceEffect.nodes.front().parameters.push_back(
        { "feedback", 0.0, 0.95, 0.35 });
    xyInterfaceEffect.nodes.front().parameters.push_back(
        { "sustain", 0.0, 1.0, 0.72 });
    xyInterfaceEffect.nodes.front().parameters.push_back(
        { "release", 0.0, 10.0, 0.42 });
    xyInterfaceEffect.parameters.push_back(
        { 202, "feedback", "Feedback", "%", 0.0, 0.95, 0.35,
          10, "feedback" });
    xyInterfaceEffect.parameters.push_back(
        { 203, "sustain", "Sustain", "%", 0.0, 1.0, 0.72,
          10, "sustain" });
    xyInterfaceEffect.parameters.push_back(
        { 204, "release", "Release", "s", 0.0, 10.0, 0.42,
          10, "release" });
    xyInterfaceEffect.interfaces.front().controls.push_back(
        { 302, InterfaceControlKind::xyPad, "Mix / Feedback", 201, {},
          130.0f, 20.0f, 160.0f, 100.0f, 0, 202 });
    xyInterfaceEffect.interfaces.front().controls.push_back(
        { 303, InterfaceControlKind::valueDisplay, "Mix value", 201, {},
          220.0f, 125.0f, 80.0f, 45.0f });
    xyInterfaceEffect.interfaces.front().controls.push_back(
        { 304, InterfaceControlKind::envelope, "Bound envelope", 201, {},
          20.0f, 125.0f, 190.0f, 45.0f, 0, 202, 203, 204 });
    xyInterfaceEffect.interfaces.front().controls.push_back(
        { 305, InterfaceControlKind::filterResponse, "Filter response", 201,
          {}, 220.0f, 125.0f, 80.0f, 45.0f, 0, 202 });
    xyInterfaceEffect.interfaces.front().controls.push_back(
        { 306, InterfaceControlKind::dynamicsResponse, "Dynamics response", 201,
          {}, 20.0f, 125.0f, 190.0f, 45.0f, 0, 202 });
    GraphLibrary xyInterfaceLibrary;
    expect(static_cast<bool>(xyInterfaceLibrary.install(xyInterfaceEffect)),
           "install Graph containing a typed two-parameter XY control");
    auto invalidXyInterface = xyInterfaceEffect;
    invalidXyInterface.reference.id = "factory.invalid-xy-interface";
    invalidXyInterface.interfaces.front().controls[1]
        .secondaryParameterBinding = 201;
    expect(!static_cast<bool>(xyInterfaceLibrary.install(invalidXyInterface)),
           "reject XY controls whose axes share one parameter binding");
    auto invalidFilterResponse = xyInterfaceEffect;
    invalidFilterResponse.reference.id = "factory.invalid-filter-response";
    invalidFilterResponse.interfaces.front().controls[4]
        .secondaryParameterBinding = 201;
    expect(!static_cast<bool>(xyInterfaceLibrary.install(
               invalidFilterResponse)),
           "reject filter response controls with duplicate parameter bindings");
    auto invalidDynamicsResponse = xyInterfaceEffect;
    invalidDynamicsResponse.reference.id = "factory.invalid-dynamics-response";
    invalidDynamicsResponse.interfaces.front().controls.back()
        .secondaryParameterBinding = 201;
    expect(!static_cast<bool>(xyInterfaceLibrary.install(
               invalidDynamicsResponse)),
           "reject dynamics response controls with duplicate parameter bindings");
    auto invalidEnvelopeInterface = xyInterfaceEffect;
    invalidEnvelopeInterface.reference.id =
        "factory.invalid-envelope-interface";
    invalidEnvelopeInterface.interfaces.front().controls[3]
        .quaternaryParameterBinding = 203;
    expect(!static_cast<bool>(xyInterfaceLibrary.install(
               invalidEnvelopeInterface)),
           "reject envelope controls with duplicate ADSR bindings");

    auto invalidIdentity = identity;
    invalidIdentity.pluginCode = "bad";
    const auto rejected = planVst3Export(library, effect.reference,
                                         invalidIdentity);
    expect(!static_cast<bool>(rejected)
               && std::ranges::any_of(
                   rejected.issues, [](const PluginExportIssue& issue) {
                       return issue.code
                              == PluginExportIssueCode::invalidIdentity;
                   }),
           "invalid binary identity is rejected before VST3 generation");
}

void graphDefinitionsRejectUnsafeWiringAndDependencies()
{
    using namespace cydaw::graph;
    GraphLibrary library;
    for (const auto channels : { 1u, 2u, 4u, 8u }) {
        auto formatted = stereoEffectDefinition();
        formatted.reference.id = "project.formatted-"
                                 + std::to_string(channels);
        for (auto& port : formatted.ports)
            if (port.type == PortType::audio) {
                port.channels = static_cast<std::uint16_t>(channels);
                port.audioType = audioPortTypeForChannels(
                    static_cast<std::uint16_t>(channels));
            }
        for (auto& node : formatted.nodes)
            for (auto& port : node.ports)
                if (port.type == PortType::audio) {
                    port.channels = static_cast<std::uint16_t>(channels);
                    port.audioType = audioPortTypeForChannels(
                        static_cast<std::uint16_t>(channels));
                }
        expect(static_cast<bool>(library.install(std::move(formatted))),
               "audio graph accepts a fixed 1-to-8-channel contract");
    }
    auto invalid = stereoEffectDefinition();
    invalid.reference.id = "project.invalid-wiring";
    invalid.nodes.front().ports.front().channels = 1;
    invalid.nodes.front().ports.front().audioType = AudioPortType::mono;
    const auto invalidResult = library.install(invalid);
    expect(!static_cast<bool>(invalidResult),
           "mono-to-stereo wiring requires an explicit channel adapter");
    expect(std::ranges::any_of(invalidResult.issues,
        [](const ValidationIssue& issue) {
            return issue.code == ValidationCode::incompatiblePorts;
        }), "typed connection mismatch reports a precise validation error");
    expect(std::string(toString(AudioPortType::mono)) == "audio-mono"
               && std::string(toString(AudioPortType::stereo)) == "audio-stereo"
               && channelCount(AudioPortType::channels8) == 8,
           "audio bus topology is a stable exact port type");

    GraphDefinition first;
    first.reference = { "project.first", { 1, 0, 0 } };
    first.displayName = "First";
    first.scope = GraphScope::project;
    expect(static_cast<bool>(library.install(first)),
           "install first project graph before linking it");
    GraphDefinition second;
    second.reference = { "project.second", { 1, 0, 0 } };
    second.displayName = "Second";
    second.scope = GraphScope::project;
    GraphNode firstInstance;
    firstInstance.id = 1;
    firstInstance.displayName = "First";
    firstInstance.kind = GraphNodeKind::graphInstance;
    firstInstance.graph = first.reference;
    second.nodes.push_back(firstInstance);
    expect(static_cast<bool>(library.install(second)),
           "install a project graph referencing an exact version");

    GraphNode secondInstance;
    secondInstance.id = 1;
    secondInstance.displayName = "Second";
    secondInstance.kind = GraphNodeKind::graphInstance;
    secondInstance.graph = second.reference;
    first.nodes.push_back(secondInstance);
    expect(static_cast<bool>(library.install(first, true)),
           "replace a graph definition during project editing");
    ValidationResult validation;
    const auto compiled = library.compile(&validation);
    expect(!compiled.has_value(), "recursive graph dependencies do not compile");
    expect(std::ranges::any_of(validation.issues,
        [](const ValidationIssue& issue) {
            return issue.code == ValidationCode::dependencyCycle;
        }), "recursive graph dependency is reported explicitly");
}

void primitiveRegistryDefinesTheGraphBuildingBlocks()
{
    using namespace cydaw::graph;
    const auto registry = createFactoryPrimitiveRegistry();
    expect(registry.size() >= 35,
           "factory graph library exposes a substantial primitive foundation");
    expect(registry.find("core.oscillator") != nullptr,
           "oscillator primitive is registered");
    expect(registry.find("core.adsr") != nullptr,
           "envelope primitive is registered");
    expect(registry.find("core.transport-reader") != nullptr,
           "transport primitive is registered");
    expect(registry.category(PrimitiveCategory::analysis).size() >= 3,
           "analysis and visual telemetry primitives are registered");
    expect(registry.find("core.audio-sum") != nullptr
               && registry.find("core.highpass") != nullptr
               && registry.find("core.hard-clip") != nullptr
               && registry.find("core.stereo-width") != nullptr,
           "fundamental audio routing, filtering and dynamics primitives are registered");
    expect(registry.find("core.subtract") != nullptr
               && registry.find("core.divide") != nullptr
               && registry.find("core.square-root") != nullptr
               && registry.find("core.floor") != nullptr
               && registry.find("core.ceil") != nullptr
               && registry.find("core.round") != nullptr
               && registry.find("core.compare") != nullptr
               && registry.find("core.logic") != nullptr,
           "float arithmetic and boolean primitives are registered");
    expect(registry.find("core.midi-note-filter") != nullptr
               && registry.find("core.midi-velocity") != nullptr
               && registry.find("core.midi-monitor") != nullptr,
           "MIDI transform and event visualization primitives are registered");
    const auto* sum = registry.find("core.audio-sum");
    expect(sum != nullptr && !sum->ports.empty()
               && sum->ports.front().acceptsMultipleConnections,
           "audio sum explicitly accepts multiple graph connections");

    const auto* oscillator = registry.find("core.oscillator");
    expect(oscillator != nullptr
               && oscillator->supportsVariableAudioChannels,
           "audio DSP primitives declare exact channel polymorphism");
    GraphNode node;
    node.id = 42;
    node.displayName = "Oscillator";
    node.processorKey = oscillator->key;
    node.ports = oscillator->ports;
    node.parameters = oscillator->parameters;
    node.parameterOverrides["detune"] = 7.0;
    expect(static_cast<bool>(registry.validateNode(node)),
           "a primitive node matching its exact contract validates");
    for (auto& item : node.ports)
        if (item.type == PortType::audio) {
            item.channels = 6;
            item.audioType = AudioPortType::channels6;
        }
    expect(static_cast<bool>(registry.validateNode(node)),
           "a channel-polymorphic primitive validates as an exact 5.1 instance");
    node.ports.back().audioType = AudioPortType::stereo;
    expect(!static_cast<bool>(registry.validateNode(node)),
           "audio channel count and named exact type cannot disagree");
    node.ports.back().audioType = AudioPortType::channels6;
    const auto* gain = registry.find("core.gain");
    GraphNode mixedAudioFormats;
    mixedAudioFormats.id = 43;
    mixedAudioFormats.displayName = "Mixed-format gain";
    mixedAudioFormats.processorKey = gain != nullptr ? gain->key : "core.gain";
    if (gain != nullptr) {
        mixedAudioFormats.ports = gain->ports;
        mixedAudioFormats.parameters = gain->parameters;
        mixedAudioFormats.ports.front().channels = 1;
        mixedAudioFormats.ports.front().audioType = AudioPortType::mono;
    }
    expect(gain != nullptr
               && !static_cast<bool>(registry.validateNode(mixedAudioFormats)),
           "one primitive instance cannot mix unrelated exact audio formats");
    node.ports.back().type = PortType::midi;
    const auto mismatch = registry.validateNode(node);
    expect(!static_cast<bool>(mismatch),
           "primitive port snapshots cannot silently change type");
}

void typedGraphDefinitionsCompileToNativeDsp()
{
    using namespace cydaw::engine;
    using namespace cydaw::graph;

    const auto primitives = createFactoryPrimitiveRegistry();
    const auto* gainDescriptor = primitives.find("core.gain");
    expect(gainDescriptor != nullptr,
           "native Graph compile test can resolve the Gain primitive");
    if (gainDescriptor == nullptr)
        return;

    GraphDefinition gainGraph;
    gainGraph.reference = { "project.native-gain", { 1, 0, 0 } };
    gainGraph.displayName = "Native gain";
    gainGraph.role = GraphRole::audioEffect;
    gainGraph.scope = GraphScope::project;
    gainGraph.ports = {
        graphPort(101, "audio-in", PortType::audio,
                  PortDirection::input, PortRate::audio, 2),
        graphPort(102, "audio-out", PortType::audio,
                  PortDirection::output, PortRate::audio, 2)
    };
    GraphNode gain;
    gain.id = 10;
    gain.displayName = "Gain";
    gain.processorKey = gainDescriptor->key;
    gain.ports = gainDescriptor->ports;
    gain.parameters = gainDescriptor->parameters;
    gainGraph.nodes.push_back(gain);
    gainGraph.connections = {
        { { boundaryNodeId, 101 }, { 10, 1 } },
        { { 10, 3 }, { boundaryNodeId, 102 } }
    };
    gainGraph.parameters.push_back(
        { 301, "gain", "Gain", "dB", -120.0, 24.0, 0.0,
          10, "gain-db" });

    GraphDefinition wrapper;
    wrapper.reference = { "project.native-wrapper", { 1, 0, 0 } };
    wrapper.displayName = "Nested native wrapper";
    wrapper.role = GraphRole::audioEffect;
    wrapper.scope = GraphScope::project;
    wrapper.ports = {
        graphPort(201, "audio-in", PortType::audio,
                  PortDirection::input, PortRate::audio, 2),
        graphPort(202, "audio-out", PortType::audio,
                  PortDirection::output, PortRate::audio, 2)
    };
    GraphNode instance;
    instance.id = 50;
    instance.displayName = "Nested gain";
    instance.kind = GraphNodeKind::graphInstance;
    instance.graph = gainGraph.reference;
    instance.ports = gainGraph.ports;
    instance.parameterOverrides["gain"] = -6.0;
    wrapper.nodes.push_back(instance);
    wrapper.connections = {
        { { boundaryNodeId, 201 }, { 50, 101 } },
        { { 50, 102 }, { boundaryNodeId, 202 } }
    };
    wrapper.parameters.push_back(
        { 401, "level", "Level", "dB", -120.0, 24.0, -6.0,
          50, "gain" });

    GraphLibrary library;
    expect(static_cast<bool>(library.install(gainGraph)),
           "install the primitive-backed nested Graph");
    expect(static_cast<bool>(library.install(wrapper)),
           "install the Graph that instantiates another Graph");

    const auto compiled = compileGraphDefinitionToNativeDsp(
        library, wrapper.reference);
    expect(compiled.ok(),
           "typed nested Graph compiles to the native DSP runtime");
    expect(compiled.graph.nodes.size() == 3
               && compiled.graph.connections.size() == 2
               && compiled.graph.outputNodeId != 0,
           "nested Graph is flattened between one native input and output");
    const auto nativeGain = std::ranges::find(
        compiled.graph.nodes, NativeDspNodeKind::gain,
        &NativeDspNode::kind);
    expect(nativeGain != compiled.graph.nodes.end()
               && std::abs(nativeGain->parameters.gainDb + 6.0f) < 0.001f,
           "nested instance override reaches the real native processor");
    expect(compiled.parameterTargets.size() == 1
               && compiled.parameterTargets.front().graphParameterId == 401
               && compiled.parameterTargets.front().nativeNodeId
                      == nativeGain->id
               && compiled.parameterTargets.front().nativeParameterKey
                      == "gain-db",
           "published parameter resolves through the nested Graph boundary");
    expect(std::ranges::any_of(
               compiled.graph.connections,
               [](const NativeDspConnection& connection) {
                   return connection.sourcePort == 201
                          && connection.destinationPort == 1;
               })
               && std::ranges::any_of(
                   compiled.graph.connections,
                   [](const NativeDspConnection& connection) {
                       return connection.sourcePort == 3
                              && connection.destinationPort == 202;
                   }),
           "exact authored port IDs survive recursive native compilation");

    const auto* constantDescriptor = primitives.find("core.constant");
    GraphDefinition modulated;
    modulated.reference = { "project.native-modulated-gain", { 1, 0, 0 } };
    modulated.displayName = "Modulated native gain";
    modulated.role = GraphRole::audioEffect;
    modulated.scope = GraphScope::project;
    modulated.ports = {
        graphPort(501, "audio-in", PortType::audio,
                  PortDirection::input, PortRate::audio, 2),
        graphPort(502, "audio-out", PortType::audio,
                  PortDirection::output, PortRate::audio, 2)
    };
    auto modulatedGain = gain;
    modulatedGain.id = 70;
    GraphNode constant;
    constant.id = 71;
    constant.displayName = "Constant";
    if (constantDescriptor != nullptr) {
        constant.processorKey = constantDescriptor->key;
        constant.ports = constantDescriptor->ports;
        constant.parameters = constantDescriptor->parameters;
        constant.parameterOverrides["value"] = -12.0;
    }
    modulated.nodes = { modulatedGain, constant };
    modulated.connections = {
        { { boundaryNodeId, 501 }, { 70, 1 } },
        { { 71, 1 }, { 70, 2 } },
        { { 70, 3 }, { boundaryNodeId, 502 } }
    };
    expect(constantDescriptor != nullptr
               && static_cast<bool>(library.install(modulated)),
           "install an audio Graph with a typed value modulation cable");
    const auto compiledModulation = compileGraphDefinitionToNativeDsp(
        library, modulated.reference);
    const auto nativeConstant = std::ranges::find(
        compiledModulation.graph.nodes, NativeDspNodeKind::controlConstant,
        &NativeDspNode::kind);
    const auto modulatedNativeGain = std::ranges::find(
        compiledModulation.graph.nodes, NativeDspNodeKind::gain,
        &NativeDspNode::kind);
    expect(compiledModulation.ok()
               && nativeConstant != compiledModulation.graph.nodes.end()
               && modulatedNativeGain != compiledModulation.graph.nodes.end()
               && compiledModulation.graph.modulations.size() == 1
               && compiledModulation.graph.modulations.front().source
                      == nativeConstant->id
               && compiledModulation.graph.modulations.front().destination
                      == modulatedNativeGain->id
               && compiledModulation.graph.modulations.front()
                      .destinationParameterKey == "gain-db",
           "typed Value-to-parameter cable compiles into the native modulation plan");

    GraphDefinition reusableModulator;
    reusableModulator.reference = {
        "project.reusable-constant-modulator", { 1, 0, 0 }
    };
    reusableModulator.displayName = "Reusable constant modulator";
    reusableModulator.role = GraphRole::modulator;
    reusableModulator.scope = GraphScope::project;
    reusableModulator.ports = {
        graphPort(601, "value", PortType::value,
                  PortDirection::output, PortRate::control)
    };
    constant.id = 80;
    constant.parameterOverrides["value"] = -18.0;
    reusableModulator.nodes = { constant };
    reusableModulator.connections = {
        { { 80, 1 }, { boundaryNodeId, 601 } }
    };

    GraphDefinition nestedModulation = modulated;
    nestedModulation.reference = {
        "project.nested-modulated-gain", { 1, 0, 0 }
    };
    GraphNode modulatorInstance;
    modulatorInstance.id = 81;
    modulatorInstance.displayName = "Reusable modulator";
    modulatorInstance.kind = GraphNodeKind::graphInstance;
    modulatorInstance.graph = reusableModulator.reference;
    modulatorInstance.ports = reusableModulator.ports;
    nestedModulation.nodes = { modulatedGain, modulatorInstance };
    nestedModulation.connections = {
        { { boundaryNodeId, 501 }, { 70, 1 } },
        { { 81, 601 }, { 70, 2 } },
        { { 70, 3 }, { boundaryNodeId, 502 } }
    };
    expect(static_cast<bool>(library.install(reusableModulator))
               && static_cast<bool>(library.install(nestedModulation)),
           "install a reusable value Graph inside an audio Graph");
    const auto compiledNestedModulation = compileGraphDefinitionToNativeDsp(
        library, nestedModulation.reference);
    expect(compiledNestedModulation.ok()
               && compiledNestedModulation.graph.modulations.size() == 1
               && std::ranges::any_of(
                   compiledNestedModulation.graph.nodes,
                   [](const NativeDspNode& node) {
                       return node.kind
                                  == NativeDspNodeKind::controlConstant
                              && std::abs(node.parameters.valueOperand + 18.0f)
                                     < 0.001f;
                   }),
           "reusable Graph value outputs flatten through typed nested boundaries");

    auto render = compileNativeDspGraph(
        compiled.graph, { 48'000.0, 32, 2 });
    expect(static_cast<bool>(render),
           "native graph produced from a typed Graph is directly renderable");
    if (render) {
        AudioBuffer input(2, 32);
        AudioBuffer output(2, 32);
        for (std::uint32_t channel = 0; channel < 2; ++channel)
            for (std::uint32_t frame = 0; frame < 32; ++frame)
                input.view(32).channel(channel)[frame] = 1.0f;
        render.plan->process(input.view(32), output.view(32), {});
        expect(std::abs(output.view(32).channel(0)[31]
                        - std::pow(10.0f, -6.0f / 20.0f)) < 0.002f,
               "nested typed Gain changes the rendered audio signal");
    }

    auto surroundGraph = gainGraph;
    surroundGraph.reference = { "project.native-gain-5-1", { 1, 0, 0 } };
    surroundGraph.displayName = "Native 5.1 gain";
    for (auto& port : surroundGraph.ports) {
        port.channels = 6;
        port.audioType = AudioPortType::channels6;
    }
    for (auto& port : surroundGraph.nodes.front().ports)
        if (port.type == PortType::audio) {
            port.channels = 6;
            port.audioType = AudioPortType::channels6;
        }
    surroundGraph.nodes.front().parameterOverrides["gain-db"] = -12.0;
    expect(static_cast<bool>(library.install(surroundGraph)),
           "install an exact 5.1 primitive-backed Graph");
    const auto compiledSurround = compileGraphDefinitionToNativeDsp(
        library, surroundGraph.reference);
    expect(compiledSurround.ok(),
           "compile exact 5.1 Graph ports into the native DSP runtime");
    auto surroundRender = compileNativeDspGraph(
        compiledSurround.graph, { 48'000.0, 32, 6 });
    expect(static_cast<bool>(surroundRender),
           "prepare the exact 5.1 Graph as a six-channel render plan");
    if (surroundRender) {
        AudioBuffer input(6, 32);
        AudioBuffer output(6, 32);
        for (std::uint32_t channel = 0; channel < 6; ++channel)
            for (std::uint32_t frame = 0; frame < 32; ++frame)
                input.view(32).channel(channel)[frame]
                    = static_cast<float>(channel + 1) * 0.1f;
        surroundRender.plan->process(input.view(32), output.view(32), {});
        const auto gain12Db = std::pow(10.0f, -12.0f / 20.0f);
        auto everyChannelProcessed = true;
        for (std::uint32_t channel = 0; channel < 6; ++channel)
            everyChannelProcessed = everyChannelProcessed
                && std::abs(output.view(32).channel(channel)[31]
                            - static_cast<float>(channel + 1) * 0.1f
                                  * gain12Db) < 0.002f;
        expect(everyChannelProcessed,
               "native Gain processes every exact 5.1 channel independently");
    }
}

void typedInstrumentGraphCompilesThroughNestedBoundaries()
{
    using namespace cydaw::engine;
    using namespace cydaw::graph;
    const auto registry = createFactoryPrimitiveRegistry();
    const auto primitive = [&registry](const NodeId id,
                                       const std::string& key) {
        GraphNode node;
        node.id = id;
        node.displayName = key;
        if (const auto* descriptor = registry.find(key)) {
            node.processorKey = descriptor->key;
            node.ports = descriptor->ports;
            node.parameters = descriptor->parameters;
            node.breaksFeedbackCycle = descriptor->breaksFeedbackCycle;
        }
        return node;
    };

    GraphDefinition synth;
    synth.reference = { "factory.typed-poly-sine", { 1, 0, 0 } };
    synth.displayName = "Typed Poly Sine";
    synth.role = GraphRole::instrument;
    synth.scope = GraphScope::factory;
    synth.ports = {
        graphPort(101, "midi-in", PortType::midi,
                  PortDirection::input, PortRate::event),
        graphPort(102, "audio-out", PortType::audio,
                  PortDirection::output, PortRate::audio, 2)
    };
    auto allocator = primitive(10, "core.voice-allocator");
    auto oscillator = primitive(20, "core.oscillator");
    auto envelope = primitive(30, "core.adsr");
    auto gain = primitive(40, "core.gain");
    auto filter = primitive(50, "core.lowpass");
    allocator.parameterOverrides["voices"] = 12.0;
    oscillator.parameterOverrides["waveform"] = 2.0;
    oscillator.parameterOverrides["level"] = 0.3;
    envelope.parameterOverrides["attack"] = 25.0;
    filter.parameterOverrides["cutoff"] = 4'200.0;
    filter.parameterOverrides["resonance"] = 3.5;
    synth.nodes = { allocator, oscillator, envelope, gain, filter };
    synth.connections = {
        { { boundaryNodeId, 101 }, { 10, 1 } },
        { { 10, 2 }, { 20, 1 } },
        { { 10, 3 }, { 30, 1 } },
        { { 20, 3 }, { 40, 1 } },
        { { 30, 2 }, { 40, 2 } },
        { { 40, 3 }, { 50, 1 } },
        { { 50, 3 }, { boundaryNodeId, 102 } }
    };

    GraphDefinition wrapper;
    wrapper.reference = { "project.typed-instrument-wrapper", { 1, 0, 0 } };
    wrapper.displayName = "Instrument wrapper";
    wrapper.role = GraphRole::instrument;
    wrapper.scope = GraphScope::project;
    wrapper.ports = {
        graphPort(201, "midi-in", PortType::midi,
                  PortDirection::input, PortRate::event),
        graphPort(202, "audio-out", PortType::audio,
                  PortDirection::output, PortRate::audio, 2)
    };
    GraphNode instance;
    instance.id = 60;
    instance.displayName = "Poly Sine";
    instance.kind = GraphNodeKind::graphInstance;
    instance.graph = synth.reference;
    instance.ports = synth.ports;
    wrapper.nodes.push_back(instance);
    wrapper.connections = {
        { { boundaryNodeId, 201 }, { 60, 101 } },
        { { 60, 102 }, { boundaryNodeId, 202 } }
    };

    GraphLibrary library;
    expect(static_cast<bool>(library.install(synth)),
           "install a fully typed polyphonic instrument Graph");
    expect(static_cast<bool>(library.install(wrapper)),
           "install an instrument that reuses the Poly Sine Graph");
    const auto compiled = compileGraphInstrument(library, wrapper.reference);
    expect(compiled.ok() && compiled.plan.enabled,
           "nested typed MIDI/value/audio Graph compiles as an instrument");
    expect(compiled.plan.graphNodeCount == 7
               && compiled.plan.voiceCount == 12
               && compiled.plan.waveform == PolyOscillatorWaveform::saw
               && std::abs(compiled.plan.oscillatorLevel - 0.3f) < 0.001f
               && std::abs(compiled.plan.attackSeconds - 0.025f) < 0.001f
               && std::abs(compiled.plan.filterCutoffHz - 4'200.0f) < 0.01f
               && std::abs(compiled.plan.filterResonance - 3.5f) < 0.001f,
           "instrument plan is derived from primitive overrides inside the nested Graph");
}

void nativeRuntimeRetainsRealtimeBaselinesAndExtendedSockets()
{
    using namespace cydaw::engine;
    using cydaw::native::NativeDspRuntime;
    NativeDspGraph graph;
    NativeDspNode input {1,NativeDspNodeKind::input},gain {2,NativeDspNodeKind::gain},output {3,NativeDspNodeKind::output},control {4,NativeDspNodeKind::controlConstant};
    control.parameters.valueOperand=0;
    graph.nodes={input,gain,output,control};graph.outputNodeId=3;
    graph.connections={{1,2},{2,3}};graph.modulations={{4,2,1,2,"gain-db",1,.25f,-120,24}};
    NativeDspRuntime runtime;runtime.prepare(48000,64,2);
    expect(runtime.setGraph(graph).wasOk(),"JUCE-free native runtime accepts modulation");
    std::array<float,64> left,right;left.fill(.1f);right.fill(.1f);float* pointers[]{left.data(),right.data()};
    cydaw::native::PlanarBuffer buffer {pointers,2,64};
    gain.parameters.gainDb=-6;
    expect(runtime.updateNodeParametersRealtime(gain),"native host automation updates its baseline");
    expect(runtime.process(buffer,0,64),"native graph renders into borrowed host channels");
    expect(std::isfinite(left.back())&&left.back()<.075f,"zero modulation retains the automated gain instead of restoring the old knob");
    const auto* effect=effectForType("comp-pro");
    expect(effect!=nullptr,"compressor catalogue available without JUCE");
    if(effect) {
        NativeDspNode compressor {2,effect->kind};applyEffectDefaults(*effect,compressor.parameters);
        graph.nodes={input,compressor,output,control};graph.modulations={{4,2,1,1024,"style",0,.5f,0,7}};
        expect(runtime.setGraph(graph).wasOk(),"an extended effect setting accepts a typed control cable");
        left.fill(.1f);right.fill(.1f);expect(runtime.process(buffer,0,64),"extended slot modulation renders finite audio");
        expect(std::isfinite(left.back()),"extended slot modulation is finite");
        const auto* descriptor=cydaw::graph::factoryPrimitiveRegistry().find(std::string(effect->processorKey));
        const auto hasStyle=descriptor&&std::ranges::any_of(descriptor->ports,[](const auto& p){return p.key=="style"&&p.optional;});
        expect(hasStyle,"advanced effect input has a stable optional socket for old graphs");
    }
}

void dockingLayoutsSupportTabsSplitsAndWindows()
{
    using namespace cydaw::ui;
    WorkspaceLayout layout;
    layout.setMainRoot(1);
    layout.addNode({ 1, LayoutNodeKind::split, { 2, 3 }, std::nullopt, SplitAxis::horizontal, 0.72f, 0 });
    layout.addNode({ 2, LayoutNodeKind::panel, {}, PanelInstance { "arrange", "Arrangement", PanelKind::arrangement } });
    layout.addNode({ 3, LayoutNodeKind::tabStack, { 4, 5 }, std::nullopt, SplitAxis::horizontal, 0.5f, 1 });
    layout.addNode({ 4, LayoutNodeKind::panel, {}, PanelInstance { "browser", "Browser", PanelKind::browser } });
    layout.addNode({ 5, LayoutNodeKind::panel, {}, PanelInstance { "graph", "Modular", PanelKind::nodeGraph } });
    layout.addNode({ 6, LayoutNodeKind::panel, {}, PanelInstance { "module-editor", "Instrument UI", PanelKind::moduleEditor } });
    layout.addFloatingWindow({ "editor-window", 6, 120, 80, 1100, 720, false });
    expect(static_cast<bool>(layout.validate()), "docking layout and detached module editor validate");
}

#include "ModulatorTests.inc"
#include "ContainerTests.inc"

} // namespace

int main()
{
    graphCompilesInStableOrder();
    cyclesAreRejectedWithoutMutatingTheGraph();
    invalidPortsAndKindsAreRejected();
    professionalBackendsAreDeclared();
    pluginCrashesAreContainedAndQuarantined();
    reusableModulesCompileDependenciesFirst();
    graphDefinitionsComposeTypedReusableDevices();
    graphLibraryKeepsWhatItKnowsAcrossReinstalls();
    graphAssetsProduceAStableVst3WrapperContract();
    graphDefinitionsRejectUnsafeWiringAndDependencies();
    primitiveRegistryDefinesTheGraphBuildingBlocks();
    typedGraphDefinitionsCompileToNativeDsp();
    typedInstrumentGraphCompilesThroughNestedBoundaries();
    dockingLayoutsSupportTabsSplitsAndWindows();
    nativeRuntimeRetainsRealtimeBaselinesAndExtendedSockets();
    modulatorsMoveAsTheyAreSet();
    containersSplitAndMixAsSet();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }

    std::cout << "All CyDAW core tests passed\n";
    return 0;
}
