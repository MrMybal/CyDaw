#include "NativeDspRuntime.h"

#include <cydaw/engine/BuiltinProcessors.h>
#include <cydaw/engine/NativeDspCompiler.h>
#include <cydaw/engine/NativeDspParameters.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <queue>
#include <string_view>
#include <unordered_map>

namespace cydaw::app {
namespace {
// Every modulatable DSP parameter is a float on the shared node payload, so a
// resolved cable is just a pointer to member. Binding one at compile time turns
// a per-block chain of string comparisons into a single store.
using ParameterField = float cydaw::engine::NativeDspNodeParameters::*;
} // namespace

struct NativeDspRuntime::RuntimeState {
    struct ControlNode {
        explicit ControlNode(const cydaw::engine::NativeDspNode& node)
            : id(node.id), kind(node.kind)
        {
            update(node.parameters);
        }

        void update(const cydaw::engine::NativeDspNodeParameters& parameters)
            noexcept
        {
            valueOperand.store(parameters.valueOperand,
                               std::memory_order_relaxed);
            valueInputA.store(parameters.valueInputA,
                              std::memory_order_relaxed);
            valueInputB.store(parameters.valueInputB,
                              std::memory_order_relaxed);
            valueMinimum.store(parameters.valueMinimum,
                               std::memory_order_relaxed);
            valueMaximum.store(parameters.valueMaximum,
                               std::memory_order_relaxed);
            modulationRateHz.store(parameters.modulationRateHz,
                                   std::memory_order_relaxed);
            modulationDepth.store(parameters.modulationDepth,
                                  std::memory_order_relaxed);
            modulationOffset.store(parameters.modulationOffset,
                                   std::memory_order_relaxed);
            modulationShape.store(parameters.modulationShape,
                                  std::memory_order_relaxed);
            mapInputMinimum.store(parameters.mapInputMinimum,
                                  std::memory_order_relaxed);
            mapInputMaximum.store(parameters.mapInputMaximum,
                                  std::memory_order_relaxed);
            mapOutputMinimum.store(parameters.mapOutputMinimum,
                                   std::memory_order_relaxed);
            mapOutputMaximum.store(parameters.mapOutputMaximum,
                                   std::memory_order_relaxed);
        }

        std::uint64_t id {};
        cydaw::engine::NativeDspNodeKind kind {};
        std::atomic<float> valueOperand {};
        std::atomic<float> valueInputA {};
        std::atomic<float> valueInputB {};
        std::atomic<float> valueMinimum {};
        std::atomic<float> valueMaximum {};
        std::atomic<float> modulationRateHz {};
        std::atomic<float> modulationDepth {};
        std::atomic<float> modulationOffset {};
        std::atomic<float> modulationShape {};
        std::atomic<float> mapInputMinimum {};
        std::atomic<float> mapInputMaximum {};
        std::atomic<float> mapOutputMinimum {};
        std::atomic<float> mapOutputMaximum {};
        // Resolved once at compile time so the callback never searches the
        // modulation list: index into RuntimeState::controls, or -1 when the
        // socket is unconnected and keeps its authored fallback.
        int valueSourceA { -1 };
        int valueSourceB { -1 };
        // Resolved once so an envelope follower never costs a dynamic_cast per
        // block; RTTI lookups are not something an audio callback should pay.
        cydaw::engine::EnvelopeFollowerProcessor* follower {};
        // A compressor publishes its gain reduction the same way a follower
        // publishes its envelope: resolved once, read every block.
        cydaw::engine::CompressorProcessor* compressor {};
        // Only the audio thread advances the oscillator phase.
        mutable double phase {};
        mutable std::atomic<float> value {};
    };

    // A control cable that drives a DSP parameter rather than another control
    // node. Everything the callback needs is resolved during compilation: the
    // source slot, the destination processor and the exact parameter field.
    struct ParameterBinding {
        int sourceControlIndex { -1 };
        // The node it drives, in liveNodes.
        std::size_t live {};
        ParameterField field {};
        // Modulate rather than set: the cable moves the value it finds
        // (knob, automation) by amount x range per unit.
        bool modulate {};
        float amount {};
        float minimum {};
        float maximum { 1.0f };
        bool logarithmic {};
    };

    // An audio node whose parameters a cable or an automation lane drives
    // block by block. The scratch is owned by the audio thread: refreshed
    // from the parameters last set (liveParameters), the driven fields
    // written over them, then handed to the processor, so a knob turned
    // beside a modulated one is not undone the next block.
    struct LiveNode {
        std::uint64_t id {};
        cydaw::engine::AudioProcessor* processor {};
        mutable cydaw::engine::NativeDspNode scratch;
        mutable bool touched {};
    };
    std::vector<LiveNode> liveNodes;
    std::unordered_map<std::uint64_t, std::size_t> liveIndices;
    // Each live node's parameters as updateParameters() last set them, in
    // liveNodes' order.
    mutable cydaw::realtime::RtPublisher<std::vector<cydaw::engine::NativeDspNodeParameters>> liveParameters;

    std::unique_ptr<cydaw::engine::AudioRenderPlan> plan;
    // Scratch buffers owned by the audio thread. They are mutable because the
    // callback borrows the runtime as const: the snapshot's topology is frozen,
    // only its render scratch moves.
    mutable cydaw::engine::AudioBuffer input;
    mutable cydaw::engine::AudioBuffer output;
    // The extra inputs' copies, as the plan reads them.
    mutable std::array<cydaw::engine::AudioBuffer, NativeDspRuntime::maximumExtraInputs> extras;
    std::unordered_map<std::uint64_t, cydaw::engine::AudioProcessor*> processors;
    std::vector<std::unique_ptr<ControlNode>> controls;
    std::unordered_map<std::uint64_t, std::size_t> controlIndices;
    std::vector<cydaw::engine::NativeDspModulationConnection> modulations;
    std::unordered_map<std::uint64_t, cydaw::engine::NativeDspNode>
        audioDefinitions;
    std::vector<ParameterBinding> parameterBindings;
    mutable std::atomic<std::uint64_t> controlSequence {};
    double sampleRate { 48'000.0 };
};

namespace {

using Kind = cydaw::engine::NativeDspNodeKind;

bool isControlNode(const Kind kind) noexcept
{
    return kind == Kind::envelopeFollower || kind == Kind::compressor
           || (kind >= Kind::controlConstant && kind <= Kind::controlNot)
           || (kind >= Kind::controlModulo && kind <= Kind::controlRound);
}

bool isControlOnly(const Kind kind) noexcept
{
    return isControlOnlyNativeKind(kind);
}

// Compilation uses this to validate a cable and to resolve it, and the audio
// callback then only ever sees the result. The vocabulary itself lives in
// engine/NativeDspParameters.h, which the definition compiler reads too.
ParameterField resolveControlParameter(const cydaw::engine::NativeDspNodeKind kind,
                                       const std::string_view key) noexcept
{
    return cydaw::engine::nativeDspParameterField(kind, key);
}

bool applyControlParameter(cydaw::engine::NativeDspNode& node,
                           const std::string_view key,
                           const float value) noexcept
{
    const auto field = resolveControlParameter(node.kind, key);
    if (field == nullptr)
        return false;
    node.parameters.*field = value;
    return true;
}

float waveform(const float shape, const double phase) noexcept
{
    switch (static_cast<int>(std::lround(shape))) {
    case 1: return static_cast<float>(1.0 - 4.0 * std::abs(phase - 0.5));
    case 2: return static_cast<float>(2.0 * phase - 1.0);
    case 3: return phase < 0.5 ? 1.0f : -1.0f;
    case 4: return phase < 0.25 ? 1.0f : -1.0f;
    default:
        return static_cast<float>(std::sin(
            phase * 2.0 * std::numbers::pi));
    }
}

} // namespace

cydaw::engine::NativeDspGraph makePassthroughAudioGraph()
{
    using namespace cydaw::engine;
    NativeDspGraph graph;
    graph.nodes = {
        { 1, NativeDspNodeKind::input, {} },
        { 2, NativeDspNodeKind::output, {} }
    };
    graph.connections = { { 1, 2 } };
    graph.outputNodeId = 2;
    return graph;
}

NativeDspRuntime::NativeDspRuntime()
    : graph_(makePassthroughAudioGraph()),
      taps_(std::make_unique<std::array<Tap, tapCount>>())
{
    juce::String error;
    runtime_.store(compile(graph_, error));
}

void NativeDspRuntime::applyBypasses(cydaw::engine::AudioRenderPlan& plan, const cydaw::engine::NativeDspGraph& graph)
{
    std::vector<cydaw::engine::BypassedNode> bypassed;
    bypassed.reserve(graph.bypasses.size());
    for (const auto& entry : graph.bypasses)
        bypassed.push_back({ entry.node, entry.from });
    plan.setBypasses(bypassed);
}

juce::Result NativeDspRuntime::setGraph(
    const cydaw::engine::NativeDspGraph& graph)
{
    juce::String error;
    auto compiled = compile(graph, error);
    if (compiled == nullptr)
        return juce::Result::fail(error.isNotEmpty()
                                      ? error
                                      : "Cannot compile the bus graph");
    {
        const juce::ScopedLock lock(configurationLock_);
        graph_ = graph;
        runtime_.store(std::move(compiled));
    }
    return juce::Result::ok();
}

juce::Result NativeDspRuntime::updateParameters(
    const cydaw::engine::NativeDspGraph& graph)
{
    const juce::ScopedLock lock(configurationLock_);
    auto current = runtime_.current();
    if (current == nullptr || graph.outputNodeId == 0)
        return juce::Result::fail("The native DSP runtime is not prepared");

    if (graph.outputNodeId != graph_.outputNodeId
        || graph.connections != graph_.connections
        || graph.modulations != graph_.modulations
        || graph.nodes.size() != graph_.nodes.size())
        return juce::Result::fail(
            "The native DSP topology changed and must be recompiled");
    for (std::size_t index = 0; index < graph.nodes.size(); ++index)
        if (graph.nodes[index].id != graph_.nodes[index].id
            || graph.nodes[index].kind != graph_.nodes[index].kind)
            return juce::Result::fail(
                "The native DSP topology changed and must be recompiled");

    for (const auto& node : graph.nodes) {
        const auto found = current->processors.find(node.id);
        if (found == current->processors.end() || found->second == nullptr) {
            const auto control = current->controlIndices.find(node.id);
            if (isControlOnly(node.kind)
                && control != current->controlIndices.end()) {
                current->controls[control->second]->update(node.parameters);
                continue;
            }
            return juce::Result::fail(
                "A native DSP processor rejected a live parameter update");
        }
        if (!cydaw::engine::updateNativeDspProcessorParameters(
                *found->second, node))
            return juce::Result::fail(
                "A native DSP processor rejected a live parameter update");
    }
    if (!current->liveNodes.empty()) {
        auto table = std::make_shared<std::vector<cydaw::engine::NativeDspNodeParameters>>();
        table->reserve(current->liveNodes.size());
        for (const auto& live : current->liveNodes) {
            const auto node = std::find_if(graph.nodes.begin(), graph.nodes.end(),
                                           [&live](const auto& item) { return item.id == live.id; });
            table->push_back(node != graph.nodes.end() ? node->parameters : live.scratch.parameters);
        }
        current->liveParameters.store(std::move(table));
    }
    if (current->plan != nullptr)
        applyBypasses(*current->plan, graph);
    graph_ = graph;
    return juce::Result::ok();
}

bool NativeDspRuntime::updateNodeParametersRealtime(
    const cydaw::engine::NativeDspNode& node) noexcept
{
    const auto current = runtime_.read();
    if (current == nullptr)
        return false;
    const auto found = current->processors.find(node.id);
    if (found != current->processors.end() && found->second != nullptr)
        return cydaw::engine::updateNativeDspProcessorParameters(
            *found->second, node);
    const auto control = current->controlIndices.find(node.id);
    if (isControlOnly(node.kind)
        && control != current->controlIndices.end()) {
        current->controls[control->second]->update(node.parameters);
        return true;
    }
    return false;
}

cydaw::engine::NativeDspGraph NativeDspRuntime::graph() const
{
    const juce::ScopedLock lock(configurationLock_);
    return graph_;
}

std::size_t NativeDspRuntime::audioNodeCount() const
{
    const auto runtime = runtime_.current();
    return runtime != nullptr && runtime->plan != nullptr
               ? runtime->plan->nodeCount()
               : 0;
}

int NativeDspRuntime::latencySamples() const noexcept
{
    try {
        const auto runtime = runtime_.current();
        return runtime != nullptr && runtime->plan != nullptr ? static_cast<int>(runtime->plan->latencySamples()) : 0;
    } catch (...) {
        return 0;
    }
}

void NativeDspRuntime::prepare(const double sampleRate,
                               const int maximumBlockSize,
                               const int channelCount)
{
    cydaw::engine::NativeDspGraph graph;
    {
        const juce::ScopedLock lock(configurationLock_);
        sampleRate_ = std::max(1.0, sampleRate);
        maximumBlockSize_ = std::max(1, maximumBlockSize);
        channelCount_ = juce::jlimit(1, 8, channelCount);
        graph = graph_;
    }
    juce::String error;
    if (auto compiled = compile(graph, error))
        runtime_.store(std::move(compiled));
}

std::shared_ptr<NativeDspRuntime::RuntimeState> NativeDspRuntime::compile(
    const cydaw::engine::NativeDspGraph& graph, juce::String& error) const
{
    using namespace cydaw::engine;
    if (graph.nodes.empty() || graph.outputNodeId == 0) {
        error = "The bus graph requires an audio output";
        return {};
    }

    double sampleRate;
    int maximumBlockSize;
    int channelCount;
    {
        const juce::ScopedLock lock(configurationLock_);
        sampleRate = sampleRate_;
        maximumBlockSize = maximumBlockSize_;
        channelCount = channelCount_;
    }
    auto runtime = std::make_shared<RuntimeState>();
    auto compiled = compileNativeDspGraph(
        graph,
        { sampleRate, static_cast<std::uint32_t>(maximumBlockSize),
          static_cast<std::uint32_t>(channelCount) },
        [&runtime](const NativeDspNode& node, AudioProcessor* processor) {
            runtime->processors.emplace(node.id, processor);
        });
    if (!compiled) {
        error = juce::String(compiled.message);
        return {};
    }
    runtime->plan = std::move(compiled.plan);
    applyBypasses(*runtime->plan, graph);
    runtime->sampleRate = sampleRate;
    runtime->modulations = graph.modulations;
    for (const auto& node : graph.nodes)
        if (!isControlOnly(node.kind))
            runtime->audioDefinitions.emplace(node.id, node);
    {
        auto table = std::make_shared<std::vector<NativeDspNodeParameters>>();
        for (const auto& node : graph.nodes) {
            const auto processor = runtime->processors.find(node.id);
            if (isControlOnly(node.kind) || processor == runtime->processors.end() || processor->second == nullptr)
                continue;
            runtime->liveIndices.emplace(node.id, runtime->liveNodes.size());
            runtime->liveNodes.push_back({ node.id, processor->second, node, false });
            table->push_back(node.parameters);
        }
        runtime->liveParameters.store(std::move(table));
    }

    std::unordered_map<std::uint64_t, cydaw::engine::NativeDspNode>
        controlDefinitions;
    std::unordered_map<std::uint64_t, std::size_t> incoming;
    std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> outgoing;
    for (const auto& node : graph.nodes)
        if (isControlNode(node.kind)) {
            controlDefinitions.emplace(node.id, node);
            incoming.emplace(node.id, 0);
        }
    for (const auto& connection : graph.modulations) {
        if (!controlDefinitions.contains(connection.source)) {
            error = "A modulation source has no control runtime";
            return {};
        }
        if (connection.destinationParameterKey.empty()) {
            if (!controlDefinitions.contains(connection.destination)) {
                error = "A control cable targets an invalid node";
                return {};
            }
            ++incoming[connection.destination];
            outgoing[connection.source].push_back(connection.destination);
        } else {
            const auto destination = runtime->audioDefinitions.find(
                connection.destination);
            const auto processor = runtime->processors.find(
                connection.destination);
            if (destination == runtime->audioDefinitions.end()
                || processor == runtime->processors.end()
                || processor->second == nullptr) {
                error = "A modulation cable targets an unavailable DSP node";
                return {};
            }
            auto probe = destination->second;
            if (!applyControlParameter(probe,
                                       connection.destinationParameterKey,
                                       0.0f)) {
                error = "A modulation cable targets an unsupported parameter";
                return {};
            }
        }
    }
    std::priority_queue<std::uint64_t, std::vector<std::uint64_t>,
                        std::greater<>> ready;
    for (const auto& [nodeId, count] : incoming)
        if (count == 0)
            ready.push(nodeId);
    while (!ready.empty()) {
        const auto nodeId = ready.top();
        ready.pop();
        runtime->controlIndices.emplace(nodeId, runtime->controls.size());
        runtime->controls.push_back(
            std::make_unique<RuntimeState::ControlNode>(
                controlDefinitions.at(nodeId)));
        for (const auto destination : outgoing[nodeId])
            if (--incoming[destination] == 0)
                ready.push(destination);
    }
    if (runtime->controls.size() != controlDefinitions.size()) {
        error = "The control-rate modulation graph contains a cycle";
        return {};
    }

    // Resolve every cable now, while allocating and searching is free. After
    // this point the callback walks pre-indexed slots instead of rescanning the
    // modulation list, comparing parameter-key strings and probing hash maps for
    // each control node on every block.
    const auto slotOf = [&runtime](const std::uint64_t nodeId) {
        const auto found = runtime->controlIndices.find(nodeId);
        return found == runtime->controlIndices.end()
                   ? -1
                   : static_cast<int>(found->second);
    };
    const auto sourceFeeding = [&graph, &slotOf](const std::uint64_t nodeId,
                                                 const std::uint64_t portId) {
        for (const auto& connection : graph.modulations)
            if (connection.destination == nodeId
                && connection.destinationParameterKey.empty()
                && connection.destinationPort == portId)
                return slotOf(connection.source);
        return -1;
    };
    for (auto& storage : runtime->controls) {
        auto& control = *storage;
        control.valueSourceA = sourceFeeding(control.id, 1);
        control.valueSourceB = sourceFeeding(control.id, 2);
        if (control.kind == Kind::envelopeFollower
            || control.kind == Kind::compressor) {
            const auto processor = runtime->processors.find(control.id);
            if (processor != runtime->processors.end()) {
                control.follower = dynamic_cast<
                    cydaw::engine::EnvelopeFollowerProcessor*>(processor->second);
                control.compressor = dynamic_cast<
                    cydaw::engine::CompressorProcessor*>(processor->second);
            }
        }
    }
    for (const auto& connection : graph.modulations) {
        if (connection.destinationParameterKey.empty())
            continue;
        const auto processor = runtime->processors.find(connection.destination);
        const auto definition = runtime->audioDefinitions.find(
            connection.destination);
        if (processor == runtime->processors.end()
            || processor->second == nullptr
            || definition == runtime->audioDefinitions.end())
            continue;
        const auto field = resolveControlParameter(
            definition->second.kind, connection.destinationParameterKey);
        const auto live = runtime->liveIndices.find(connection.destination);
        if (field == nullptr || live == runtime->liveIndices.end())
            continue;
        const auto low = std::min(connection.minimum, connection.maximum);
        const auto high = std::max(connection.minimum, connection.maximum);
        runtime->parameterBindings.push_back({ slotOf(connection.source), live->second, field, connection.mode == 1,
                                               connection.amount, low, high, low > 0.0f && high / low >= 100.0f });
    }

    runtime->input.prepare(static_cast<std::uint32_t>(channelCount),
                           static_cast<std::uint32_t>(maximumBlockSize));
    for (auto& extra : runtime->extras)
        extra.prepare(static_cast<std::uint32_t>(channelCount), static_cast<std::uint32_t>(maximumBlockSize));
    runtime->output.prepare(static_cast<std::uint32_t>(channelCount),
                            static_cast<std::uint32_t>(maximumBlockSize));
    return runtime;
}

void NativeDspRuntime::evaluateControls(const RuntimeState& runtime,
                                        const int numSamples) noexcept
{
    runtime.controlSequence.fetch_add(1, std::memory_order_acq_rel);
    // Slots were resolved during compilation, so a socket is either a direct
    // index into the topologically ordered control list or its authored
    // fallback. Sources always precede their destinations, which is what lets
    // one pass compute the whole control-rate graph.
    const auto slotValue = [&runtime](const int slot, const float fallback) {
        return slot < 0 ? fallback
                        : runtime.controls[static_cast<std::size_t>(slot)]
                              ->value.load(std::memory_order_relaxed);
    };

    for (const auto& storage : runtime.controls) {
        auto& control = *storage;
        const auto operand = control.valueOperand.load(
            std::memory_order_relaxed);
        const auto a = slotValue(control.valueSourceA, control.valueInputA.load(
            std::memory_order_relaxed));
        const auto b = slotValue(control.valueSourceB, control.valueInputB.load(
            std::memory_order_relaxed));
        switch (control.kind) {
        case Kind::controlConstant:
            control.value.store(operand, std::memory_order_relaxed);
            break;
        case Kind::controlLfo:
            control.value.store(control.modulationOffset.load(
                                std::memory_order_relaxed)
                            + control.modulationDepth.load(
                                  std::memory_order_relaxed)
                                  * waveform(control.modulationShape.load(
                                                 std::memory_order_relaxed),
                                             control.phase),
                                std::memory_order_relaxed);
            control.phase += std::max(
                                 0.0f, control.modulationRateHz.load(
                                           std::memory_order_relaxed))
                             * static_cast<double>(numSamples)
                             / std::max(1.0, runtime.sampleRate);
            control.phase -= std::floor(control.phase);
            break;
        case Kind::controlAdd:
            control.value.store(a + b, std::memory_order_relaxed); break;
        case Kind::controlSubtract:
            control.value.store(a - b, std::memory_order_relaxed); break;
        case Kind::controlMultiply:
            control.value.store(a * b, std::memory_order_relaxed); break;
        case Kind::controlDivide:
            control.value.store(std::abs(b) > 1.0e-9f ? a / b : 0.0f,
                                std::memory_order_relaxed);
            break;
        case Kind::controlMinimum:
            control.value.store(std::min(a, b), std::memory_order_relaxed); break;
        case Kind::controlMaximum:
            control.value.store(std::max(a, b), std::memory_order_relaxed); break;
        case Kind::controlClamp:
            control.value.store(std::clamp(
                a, std::min(control.valueMinimum.load(
                                std::memory_order_relaxed),
                            control.valueMaximum.load(
                                std::memory_order_relaxed)),
                std::max(control.valueMinimum.load(
                                std::memory_order_relaxed),
                         control.valueMaximum.load(
                                std::memory_order_relaxed))),
                                std::memory_order_relaxed);
            break;
        case Kind::controlAbsolute:
            control.value.store(std::abs(a), std::memory_order_relaxed); break;
        case Kind::controlNegate:
            control.value.store(-a, std::memory_order_relaxed); break;
        case Kind::controlMapRange: {
            const auto inputMinimum = control.mapInputMinimum.load(
                std::memory_order_relaxed);
            const auto inputMaximum = control.mapInputMaximum.load(
                std::memory_order_relaxed);
            const auto outputMinimum = control.mapOutputMinimum.load(
                std::memory_order_relaxed);
            const auto outputMaximum = control.mapOutputMaximum.load(
                std::memory_order_relaxed);
            const auto denominator = inputMaximum - inputMinimum;
            const auto normalised = std::abs(denominator) > 1.0e-9f
                ? (a - inputMinimum) / denominator : 0.0f;
            control.value.store(outputMinimum
                                    + normalised
                                          * (outputMaximum - outputMinimum),
                                std::memory_order_relaxed);
            break;
        }
        case Kind::controlScaleOffset:
            control.value.store(a * control.modulationDepth.load(
                                    std::memory_order_relaxed)
                            + control.modulationOffset.load(
                                    std::memory_order_relaxed),
                                std::memory_order_relaxed);
            break;
        case Kind::controlGreater:
            control.value.store(a > b ? 1.0f : 0.0f,
                                std::memory_order_relaxed); break;
        case Kind::controlLess:
            control.value.store(a < b ? 1.0f : 0.0f,
                                std::memory_order_relaxed); break;
        case Kind::controlEqual:
            control.value.store(std::abs(a - b) <= 1.0e-6f ? 1.0f : 0.0f,
                                std::memory_order_relaxed);
            break;
        case Kind::controlAnd:
            control.value.store(a > 0.5f && b > 0.5f ? 1.0f : 0.0f,
                                std::memory_order_relaxed);
            break;
        case Kind::controlOr:
            control.value.store(a > 0.5f || b > 0.5f ? 1.0f : 0.0f,
                                std::memory_order_relaxed);
            break;
        case Kind::controlXor:
            control.value.store((a > 0.5f) != (b > 0.5f) ? 1.0f : 0.0f,
                                std::memory_order_relaxed);
            break;
        case Kind::controlNot:
            control.value.store(a > 0.5f ? 0.0f : 1.0f,
                                std::memory_order_relaxed); break;
        case Kind::controlModulo:
            control.value.store(std::abs(b) > 1.0e-9f ? std::fmod(a, b)
                                                       : 0.0f,
                                std::memory_order_relaxed);
            break;
        case Kind::controlPower: {
            const auto result = std::pow(a, b);
            control.value.store(std::isfinite(result) ? result : 0.0f,
                                std::memory_order_relaxed);
            break;
        }
        case Kind::controlSine:
            control.value.store(std::sin(a), std::memory_order_relaxed);
            break;
        case Kind::controlCosine:
            control.value.store(std::cos(a), std::memory_order_relaxed);
            break;
        case Kind::controlSquareRoot:
            control.value.store(a >= 0.0f ? std::sqrt(a) : 0.0f,
                                std::memory_order_relaxed);
            break;
        case Kind::controlFloor:
            control.value.store(std::floor(a), std::memory_order_relaxed);
            break;
        case Kind::controlCeil:
            control.value.store(std::ceil(a), std::memory_order_relaxed);
            break;
        case Kind::controlRound:
            control.value.store(std::round(a), std::memory_order_relaxed);
            break;
        case Kind::envelopeFollower:
            control.value.store(control.follower != nullptr
                                    ? control.follower->envelope()
                                    : 0.0f,
                                std::memory_order_relaxed);
            break;
        case Kind::compressor:
            control.value.store(control.compressor != nullptr
                                    ? control.compressor->gainReductionDb()
                                    : 0.0f,
                                std::memory_order_relaxed);
            break;
        default:
            control.value.store(0.0f, std::memory_order_relaxed); break;
        }
    }

    runtime.controlSequence.fetch_add(1, std::memory_order_release);
}

float NativeDspRuntime::automationValueAt(const std::vector<AutomationPoint>& points, const double seconds,
                                          const bool logarithmic) noexcept
{
    if (points.empty())
        return 0.0f;
    if (seconds <= points.front().timeSeconds)
        return points.front().value;
    if (seconds >= points.back().timeSeconds)
        return points.back().value;
    // The segment holding the time, by halving: lanes can be long.
    std::size_t low = 0;
    std::size_t high = points.size() - 1;
    while (high - low > 1) {
        const auto middle = (low + high) / 2;
        if (points[middle].timeSeconds <= seconds)
            low = middle;
        else
            high = middle;
    }
    const auto& first = points[low];
    const auto& second = points[high];
    const auto span = std::max(1.0e-9, second.timeSeconds - first.timeSeconds);
    auto amount = static_cast<float>(std::clamp((seconds - first.timeSeconds) / span, 0.0, 1.0));
    if (first.curve != 0)
        // The bounded cubic bend the volume lane uses.
        amount = std::clamp(amount + std::clamp(first.curveAmount, -1.0f, 1.0f) * 2.0f * amount * (1.0f - amount), 0.0f, 1.0f);
    if (logarithmic && first.value > 0.0f && second.value > 0.0f)
        return first.value * std::pow(second.value / first.value, amount);
    return first.value + (second.value - first.value) * amount;
}

void NativeDspRuntime::setAutomation(std::vector<AutomationLane> lanes)
{
    const auto hadLanes = [this] {
        const auto current = automation_.current();
        return current != nullptr && !current->empty();
    }();
    std::erase_if(lanes, [](const AutomationLane& lane) {
        return lane.points.empty() || (lane.field == nullptr && lane.slot < 0);
    });
    automation_.store(lanes.empty() ? nullptr
                                    : std::make_shared<const std::vector<AutomationLane>>(std::move(lanes)));
    // Lanes gone, the parameters go back to what they were set to.
    if (hadLanes) {
        const juce::ScopedLock lock(configurationLock_);
        const auto current = runtime_.current();
        if (current != nullptr)
            for (const auto& node : graph_.nodes)
                if (const auto found = current->processors.find(node.id);
                    found != current->processors.end() && found->second != nullptr)
                    (void) cydaw::engine::updateNativeDspProcessorParameters(*found->second, node);
    }
}

void NativeDspRuntime::applyParameters(const RuntimeState& runtime, const double timelineSeconds) noexcept
{
    const auto lanes = automation_.read();
    const auto automated = lanes != nullptr && std::isfinite(timelineSeconds);
    if (runtime.parameterBindings.empty() && !automated)
        return;
    const auto live = runtime.liveParameters.read();
    const auto parametersOf = [&runtime, &live](const std::size_t index) -> cydaw::engine::NativeDspNodeParameters& {
        const auto& node = runtime.liveNodes[index];
        if (!node.touched) {
            if (live != nullptr && index < live->size())
                node.scratch.parameters = (*live)[index];
            node.touched = true;
        }
        return node.scratch.parameters;
    };
    if (automated)
        for (const auto& lane : *lanes) {
            const auto found = runtime.liveIndices.find(lane.nodeId);
            if (found == runtime.liveIndices.end())
                continue;
            // A clip's lane: its own time, from its start, looping.
            auto seconds = timelineSeconds;
            if (lane.clipStartSeconds >= 0.0) {
                seconds -= lane.clipStartSeconds;
                if (seconds < 0.0)
                    continue;
                if (lane.clipLengthSeconds > 0.0)
                    seconds = lane.clipLooping ? std::fmod(seconds, lane.clipLengthSeconds) : std::min(seconds, lane.clipLengthSeconds);
            }
            auto& parameters = parametersOf(found->second);
            const auto value = automationValueAt(lane.points, seconds, lane.logarithmic);
            if (lane.field != nullptr)
                parameters.*lane.field = value;
            else if (lane.slot >= 0 && static_cast<std::size_t>(lane.slot) < parameters.eqBands.size())
                parameters.eqBands[static_cast<std::size_t>(lane.slot)] = value;
        }
    for (const auto& binding : runtime.parameterBindings) {
        const auto slot = binding.sourceControlIndex;
        const auto control = slot < 0 ? 0.0f
                                      : runtime.controls[static_cast<std::size_t>(slot)]->value.load(std::memory_order_relaxed);
        auto& value = parametersOf(binding.live).*binding.field;
        if (!binding.modulate) {
            value = control;
        } else if (binding.logarithmic && value > 0.0f) {
            // Around the knob, on its own scale: +-amount of the decades.
            value = std::clamp(value * std::pow(binding.maximum / binding.minimum, control * binding.amount), binding.minimum,
                               binding.maximum);
        } else if (binding.maximum > binding.minimum) {
            value = std::clamp(value + control * binding.amount * (binding.maximum - binding.minimum), binding.minimum,
                               binding.maximum);
        }
    }
    for (const auto& node : runtime.liveNodes)
        if (node.touched) {
            node.touched = false;
            (void) cydaw::engine::updateNativeDspProcessorParameters(*node.processor, node.scratch);
        }
}

bool NativeDspRuntime::process(juce::AudioBuffer<float>& buffer,
                               const int startSample,
                               const int numSamples) noexcept
{
    return process(buffer, startSample, numSamples, std::numeric_limits<double>::quiet_NaN());
}

bool NativeDspRuntime::process(juce::AudioBuffer<float>& buffer,
                               const int startSample,
                               const int numSamples,
                               const double timelineSeconds) noexcept
{
    // The extra inputs are this block's, whether it runs or not.
    std::array<const juce::AudioBuffer<float>*, maximumExtraInputs> extraSources {};
    for (std::size_t bus = 0; bus < maximumExtraInputs; ++bus)
        extraSources[bus] = std::exchange(extraInputs_[bus], nullptr);
    const auto runtime = runtime_.read();
    if (runtime == nullptr || runtime->plan == nullptr || numSamples <= 0
        || numSamples > static_cast<int>(runtime->input.maximumFrameCount()))
        return false;
    const auto frames = static_cast<std::uint32_t>(numSamples);
    const auto channels = static_cast<std::uint32_t>(std::min(
        buffer.getNumChannels(),
        static_cast<int>(runtime->input.channelCount())));
    auto input = runtime->input.view(frames);
    auto output = runtime->output.view(frames);
    evaluateControls(*runtime, numSamples);
    applyParameters(*runtime, timelineSeconds);
    cydaw::engine::clearAudio(input);
    for (std::uint32_t channel = 0; channel < channels; ++channel)
        std::copy_n(buffer.getReadPointer(static_cast<int>(channel), startSample),
                    frames, input.channel(channel));
    cydaw::engine::ProcessContext context;
    context.transport.beatsPerMinute = sessionTempo();
    // Where the block is in the bars, when the transport plays (a delay or
    // a repeat that follows the beat).
    if (sessionPlaying() && std::isfinite(timelineSeconds)) {
        context.transport.state = cydaw::engine::PlaybackState::playing;
        context.transport.quarterNotePosition = std::max(0.0, timelineSeconds) * context.transport.beatsPerMinute / 60.0;
        context.transport.samplePosition = static_cast<std::int64_t>(std::max(0.0, timelineSeconds) * sampleRate_);
    }
    // The extra inputs a track is routed into, copied as the main one is.
    std::array<cydaw::engine::AudioBlockView, maximumExtraInputs> extras {};
    auto extraCount = std::size_t {};
    for (std::size_t bus = 0; bus < maximumExtraInputs; ++bus) {
        const auto* source = extraSources[bus];
        if (source == nullptr)
            continue;
        auto view = runtime->extras[bus].view(frames);
        cydaw::engine::clearAudio(view);
        const auto sourceChannels = source->getNumChannels();
        for (std::uint32_t channel = 0; channel < view.channelCount && sourceChannels > 0; ++channel)
            std::copy_n(source->getReadPointer(std::min(static_cast<int>(channel), sourceChannels - 1)),
                        std::min(frames, static_cast<std::uint32_t>(source->getNumSamples())), view.channel(channel));
        extras[bus] = view;
        extraCount = bus + 1;
    }
    runtime->plan->process(input, extras.data(), extraCount, output, context);
    captureTaps(*runtime, frames);
    for (std::uint32_t channel = 0; channel < channels; ++channel)
        std::copy_n(output.channel(channel), frames,
                    buffer.getWritePointer(static_cast<int>(channel), startSample));
    return true;
}

namespace {
std::atomic<double> sessionTempoBpm { 120.0 };
std::atomic<bool> sessionTransportPlaying { false };
} // namespace

void NativeDspRuntime::setSessionPlaying(const bool playing) noexcept
{
    sessionTransportPlaying.store(playing, std::memory_order_relaxed);
}

bool NativeDspRuntime::sessionPlaying() noexcept
{
    return sessionTransportPlaying.load(std::memory_order_relaxed);
}

void NativeDspRuntime::controlValuesNow(const std::uint64_t* nodeIds, float* values,
                                        const std::size_t count) const noexcept
{
    const auto runtime = runtime_.read();
    for (std::size_t index = 0; index < count; ++index) {
        values[index] = std::numeric_limits<float>::quiet_NaN();
        if (runtime == nullptr)
            continue;
        const auto found = runtime->controlIndices.find(nodeIds[index]);
        if (found != runtime->controlIndices.end())
            values[index] = runtime->controls[found->second]->value.load(
                std::memory_order_relaxed);
    }
}

void NativeDspRuntime::setSessionTempo(const double beatsPerMinute) noexcept
{
    sessionTempoBpm.store(std::clamp(beatsPerMinute, 20.0, 400.0), std::memory_order_relaxed);
}

double NativeDspRuntime::sessionTempo() noexcept
{
    return sessionTempoBpm.load(std::memory_order_relaxed);
}

void NativeDspRuntime::captureTaps(const RuntimeState& runtime,
                                   const std::uint32_t frames) noexcept
{
    constexpr auto capacity = static_cast<std::uint32_t>(
        cydaw::engine::AudioAnalysisFrame::waveformCapacity);
    for (auto& tap : *taps_) {
        const auto key = tap.key.load(std::memory_order_acquire);
        if (key == 0)
            continue;
        if (key != tap.audioKey) {
            tap.audioKey = key;
            tap.write.store(0, std::memory_order_relaxed);
            tap.filled.store(0, std::memory_order_release);
        }
        cydaw::engine::AudioBlockView received;
        cydaw::engine::AudioBlockView produced;
        if (!runtime.plan->lastSignals(key / 2, frames, received, produced))
            continue;
        const auto& view = (key & 1u) != 0u ? received : produced;
        if (view.channelCount == 0 || view.frameCount == 0)
            continue;
        const auto scale = 1.0f / static_cast<float>(view.channelCount);
        auto write = tap.write.load(std::memory_order_relaxed);
        for (std::uint32_t frame = 0; frame < view.frameCount; ++frame) {
            auto sum = 0.0f;
            for (std::uint32_t channel = 0; channel < view.channelCount; ++channel)
                sum += view.channel(channel)[frame];
            tap.samples[write].store(sum * scale, std::memory_order_relaxed);
            write = (write + 1u) % capacity;
        }
        tap.write.store(write, std::memory_order_release);
        tap.filled.store(std::min(capacity, tap.filled.load(std::memory_order_relaxed)
                                                + view.frameCount),
                         std::memory_order_release);
        tap.blocks.fetch_add(1, std::memory_order_release);
    }
}

void NativeDspRuntime::setTaps(const std::vector<TapRequest>& taps) noexcept
{
    const auto keyOf = [](const TapRequest& request) {
        return request.nodeId == 0 ? std::uint64_t {}
                                   : request.nodeId * 2u + (request.input ? 1u : 0u);
    };
    std::array<bool, tapCount> kept {};
    std::vector<std::uint64_t> missing;
    for (const auto& request : taps) {
        const auto key = keyOf(request);
        if (key == 0)
            continue;
        auto found = false;
        for (std::size_t slot = 0; slot < tapCount; ++slot)
            if ((*taps_)[slot].key.load(std::memory_order_relaxed) == key) {
                kept[slot] = true;
                found = true;
                break;
            }
        if (!found && std::find(missing.begin(), missing.end(), key) == missing.end())
            missing.push_back(key);
    }
    for (std::size_t slot = 0; slot < tapCount; ++slot)
        if (!kept[slot])
            (*taps_)[slot].key.store(0, std::memory_order_release);
    auto next = missing.begin();
    for (std::size_t slot = 0; slot < tapCount && next != missing.end(); ++slot)
        if (!kept[slot]) {
            (*taps_)[slot].key.store(*next, std::memory_order_release);
            ++next;
        }
}

bool NativeDspRuntime::readTap(const std::uint64_t nodeId, const bool input,
                               cydaw::engine::AudioAnalysisFrame& frame) const noexcept
{
    constexpr auto capacity = static_cast<std::uint32_t>(
        cydaw::engine::AudioAnalysisFrame::waveformCapacity);
    const auto key = nodeId * 2u + (input ? 1u : 0u);
    for (const auto& tap : *taps_) {
        if (nodeId == 0 || tap.key.load(std::memory_order_acquire) != key)
            continue;
        const auto filled = std::min(capacity, tap.filled.load(std::memory_order_acquire));
        const auto write = tap.write.load(std::memory_order_acquire);
        const auto start = (write + capacity - filled) % capacity;
        for (std::uint32_t point = 0; point < filled; ++point)
            frame.waveform[point] = tap.samples[(start + point) % capacity].load(
                std::memory_order_relaxed);
        frame.waveformCount = filled;
        frame.channelCount = 1;
        frame.sequence = tap.blocks.load(std::memory_order_acquire);
        const auto runtime = runtime_.current();
        frame.sampleRate = static_cast<float>(runtime != nullptr ? runtime->sampleRate : 48'000.0);
        return true;
    }
    return false;
}

void NativeDspRuntime::takeNodePeaks(const std::uint64_t* nodes, const std::size_t count, float* left, float* right) const
{
    const auto runtime = runtime_.current();
    if (runtime == nullptr || runtime->plan == nullptr) {
        std::fill_n(left, count, 0.0f);
        std::fill_n(right, count, 0.0f);
        return;
    }
    runtime->plan->takePeaks(nodes, count, left, right);
}

std::size_t NativeDspRuntime::nodeReadings(const std::uint64_t node, float* into, const std::size_t count) const
{
    const auto runtime = runtime_.current();
    return runtime != nullptr && runtime->plan != nullptr ? runtime->plan->readings(node, into, count) : 0;
}

std::vector<std::pair<std::uint64_t, float>>
NativeDspRuntime::controlValues() const
{
    std::vector<std::pair<std::uint64_t, float>> values;
    const auto runtime = runtime_.current();
    if (runtime == nullptr)
        return values;
    values.reserve(runtime->controls.size());
    for (auto attempt = 0; attempt < 4; ++attempt) {
        values.clear();
        const auto before = runtime->controlSequence.load(
            std::memory_order_acquire);
        if ((before & 1u) != 0u)
            continue;
        for (const auto& control : runtime->controls)
            values.emplace_back(
                control->id,
                control->value.load(std::memory_order_relaxed));
        const auto after = runtime->controlSequence.load(
            std::memory_order_acquire);
        if (before == after && (after & 1u) == 0u)
            return values;
    }
    values.clear();
    return values;
}

} // namespace cydaw::app
