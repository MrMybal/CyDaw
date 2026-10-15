#pragma once

#include <cydaw/engine/AudioGraph.h>
#include <cydaw/engine/AudioProcessor.h>

#include <atomic>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace cydaw::engine {

enum class RenderPlanError : std::uint8_t {
    none,
    invalidSpec,
    invalidNode,
    duplicateNode,
    missingNode,
    duplicateConnection,
    cycleDetected,
    missingOutput
};

class AudioRenderPlan;

// A node switched off (its card bypassed): it gives out what `from` received
// at its main input, a sidechain left out; `from` 0 gives silence (an
// instrument switched off).
struct BypassedNode {
    NodeId node {};
    NodeId from {};

    [[nodiscard]] bool operator==(const BypassedNode&) const = default;
};

struct RenderPlanResult {
    RenderPlanError error { RenderPlanError::none };
    std::string message;
    std::unique_ptr<AudioRenderPlan> plan;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == RenderPlanError::none && plan != nullptr;
    }
};

class AudioRenderPlan final {
public:
    struct RuntimeConnection {
        NodeId sourceNodeId {};
        std::size_t sourceNodeIndex {};
        std::uint64_t sourcePort {};
        std::uint64_t destinationPort {};
        // Its line in delayLines_ when it may have to wait for a slower
        // path into the same node, or noDelay.
        std::size_t delayLine { noDelay };
    };
    static constexpr std::size_t noDelay = static_cast<std::size_t>(-1);
    // What arrives early at a node that also hears a later path (a
    // look-ahead on the other branch) waits here: a ring as long as the
    // graph's whole latency can be, and the block read back from it.
    struct DelayLine {
        AudioBuffer ring;
        AudioBuffer scratch;
        std::uint32_t write {};
    };

    struct RuntimeNode {
        struct InputBus {
            std::uint64_t portId {};
            AudioBuffer buffer;
        };

        NodeId id {};
        std::unique_ptr<AudioProcessor> processor;
        MultiBusAudioProcessor* multiBusProcessor {};
        std::vector<RuntimeConnection> predecessors;
        std::vector<InputBus> inputBuses;
        std::vector<AudioInputBusView> inputBusViews;
        AudioBuffer inputBuffer;
        AudioBuffer outputBuffer;
        bool receivesExternalInput {};
        // Which external input: 0 the main one, then the extra ones.
        std::uint32_t externalBus {};
    };

    void process(AudioBlockView input, AudioBlockView output, const ProcessContext& context) noexcept;
    // With inputs beside the main one: `extraInputs[k]` feeds the input
    // nodes of bus k + 1 (empty views give silence).
    void process(AudioBlockView input, const AudioBlockView* extraInputs, std::size_t extraCount, AudioBlockView output,
                 const ProcessContext& context) noexcept;
    // Right after process(), on the same thread: what a node received
    // (its inputs summed) and what it produced in that block. False when the
    // plan has no such node.
    [[nodiscard]] bool lastSignals(NodeId node, std::uint32_t frameCount, AudioBlockView& input,
                                   AudioBlockView& output) noexcept;
    void reset() noexcept;
    [[nodiscard]] const ProcessSpec& spec() const noexcept;
    [[nodiscard]] std::size_t nodeCount() const noexcept;
    [[nodiscard]] std::size_t connectionCount() const noexcept;
    [[nodiscard]] std::size_t inputBusCount(NodeId node) const noexcept;
    [[nodiscard]] bool hasConnection(NodeId source, std::uint64_t sourcePort,
                                     NodeId destination,
                                     std::uint64_t destinationPort) const noexcept;
    // How late the output comes out: the slowest path from an input to it,
    // its nodes' latencies added. Parallel paths are aligned inside the plan
    // (the faster ones wait), so this is the whole graph's. Any thread: it
    // reads the processors' own, which are atomics.
    [[nodiscard]] std::uint32_t latencySamples() const noexcept;
    // The longest wait a path can be given, set when the plan is compiled.
    [[nodiscard]] std::uint32_t maximumLatencySamples() const noexcept { return maximumLatency_; }
    // The nodes switched off, every other one on. Any thread: the shape does
    // not change. A switched-off node goes on processing (it comes back
    // warm) and reporting its latency, its input held back as long, so the
    // mix's alignment never moves; the change is crossfaded.
    void setBypasses(std::span<const BypassedNode> nodes) noexcept;
    // The loudest each of these nodes gave out since it was last asked
    // (its first two channels; the second as the first for a mono one), for
    // an interface's meters; 0 for a node the plan has not. Any thread.
    void takePeaks(const NodeId* nodes, std::size_t count, float* left, float* right) noexcept;
    // What a node's processor shows of its work now (AudioProcessor::
    // readings): up to `count` values, how many; none for a node the plan
    // has not. Any thread.
    [[nodiscard]] std::size_t readings(NodeId node, float* into, std::size_t count) const noexcept;

private:
    friend class AudioRenderPlanBuilder;
    // A node's switch: what the control side wants, then the audio thread's
    // fade toward it.
    struct Bypass {
        // -1 on; -2 silence; else the index of the node whose input passes.
        std::atomic<std::int32_t> wanted { -1 };
        float mix {};
        std::int32_t from { -1 };
        // For a node with a latency: its own input, held back as long.
        AudioBuffer ring;
        std::uint32_t write {};
    };
    void bypass(std::size_t index, std::uint32_t frames) noexcept;
    // What a node received at its main input (a sidechain left out).
    void mainInput(std::size_t index, AudioBlockView into) noexcept;

    ProcessSpec spec_;
    std::vector<RuntimeNode> nodes_;
    std::unique_ptr<Bypass[]> bypasses_;
    AudioBuffer bypassScratch_;
    // Two a node: its channels' loudest since takePeaks().
    std::unique_ptr<std::atomic<float>[]> peaks_;
    // Whether any node may be switched off: nothing to look at otherwise.
    std::atomic<bool> anyBypass_ {};
    std::size_t outputNodeIndex_ {};
    std::vector<DelayLine> delayLines_;
    // Each node's output latency this block (the audio thread's).
    std::vector<std::uint32_t> nodeLatency_;
    std::uint32_t maximumLatency_ {};
};

class AudioRenderPlanBuilder final {
public:
    [[nodiscard]] RenderPlanError addNode(NodeId id,
                                          std::unique_ptr<AudioProcessor> processor,
                                          bool receivesExternalInput = false,
                                          std::uint32_t externalBus = 0);
    [[nodiscard]] RenderPlanError connect(NodeId source, NodeId destination,
                                          std::uint64_t sourcePort = 0,
                                          std::uint64_t destinationPort = 0);
    [[nodiscard]] RenderPlanResult compile(ProcessSpec spec, NodeId outputNodeId);

private:
    struct BuildNode {
        std::unique_ptr<AudioProcessor> processor;
        bool receivesExternalInput {};
        std::uint32_t externalBus {};
    };
    struct BuildConnection {
        NodeId source {};
        NodeId destination {};
        std::uint64_t sourcePort {};
        std::uint64_t destinationPort {};
        [[nodiscard]] bool operator==(const BuildConnection&) const = default;
    };

    std::unordered_map<NodeId, BuildNode> nodes_;
    std::vector<BuildConnection> connections_;
};

} // namespace cydaw::engine
