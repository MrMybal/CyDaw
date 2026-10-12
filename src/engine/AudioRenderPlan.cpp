#include <cydaw/engine/AudioRenderPlan.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>
#include <unordered_set>

namespace cydaw::engine {

void AudioRenderPlan::process(const AudioBlockView input,
                              const AudioBlockView output,
                              const ProcessContext& context) noexcept
{
    process(input, nullptr, 0, output, context);
}

void AudioRenderPlan::process(const AudioBlockView input, const AudioBlockView* const extraInputs, const std::size_t extraCount,
                              const AudioBlockView output, const ProcessContext& context) noexcept
{
    const auto frames = std::min({ input.frameCount, output.frameCount, spec_.maximumBlockSize });
    clearAudio({ output.data, output.channelCount, frames, output.channelStride });

    for (std::size_t index = 0; index < nodes_.size(); ++index) {
        auto& node = nodes_[index];
        const auto nodeInput = node.inputBuffer.view(frames);
        const auto nodeOutput = node.outputBuffer.view(frames);
        clearAudio(nodeInput);
        for (auto& bus : node.inputBuses)
            bus.buffer.clear(frames);
        if (node.receivesExternalInput) {
            if (node.externalBus == 0) {
                copyAudio(nodeInput, { input.data, input.channelCount, frames, input.channelStride });
            } else if (node.externalBus - 1 < extraCount && extraInputs != nullptr) {
                const auto& extra = extraInputs[node.externalBus - 1];
                if (extra.data != nullptr && extra.frameCount >= frames)
                    copyAudio(nodeInput, { extra.data, extra.channelCount, frames, extra.channelStride });
            }
        }
        // The latest of what reaches the node; what comes sooner waits for
        // it, so a dry path meets a look-ahead's output in time.
        auto arrival = std::uint32_t {};
        for (const auto& predecessor : node.predecessors)
            arrival = std::max(arrival, nodeLatency_[predecessor.sourceNodeIndex]);
        for (const auto& predecessor : node.predecessors) {
            auto predecessorOutput =
                nodes_[predecessor.sourceNodeIndex].outputBuffer.view(frames);
            if (predecessor.delayLine != noDelay) {
                auto& line = delayLines_[predecessor.delayLine];
                const auto size = line.ring.maximumFrameCount();
                const auto wait = std::min(arrival - nodeLatency_[predecessor.sourceNodeIndex], size - 1);
                const auto ring = line.ring.view(size);
                const auto delayed = line.scratch.view(frames);
                const auto channels = std::min({ ring.channelCount, delayed.channelCount, predecessorOutput.channelCount });
                for (std::uint32_t channel = 0; channel < channels; ++channel) {
                    const auto* from = predecessorOutput.channel(channel);
                    auto* stored = ring.channel(channel);
                    auto* to = delayed.channel(channel);
                    auto write = line.write;
                    for (std::uint32_t frame = 0; frame < frames; ++frame) {
                        stored[write] = from[frame];
                        to[frame] = stored[write >= wait ? write - wait : write + size - wait];
                        if (++write == size)
                            write = 0;
                    }
                }
                line.write = (line.write + frames) % size;
                predecessorOutput = delayed;
            }
            addAudio(nodeInput, predecessorOutput);
            const auto bus = std::find_if(
                node.inputBuses.begin(), node.inputBuses.end(),
                [&predecessor](const RuntimeNode::InputBus& value) {
                    return value.portId == predecessor.destinationPort;
                });
            if (bus != node.inputBuses.end())
                addAudio(bus->buffer.view(frames), predecessorOutput);
        }

        clearAudio(nodeOutput);
        if (node.multiBusProcessor != nullptr) {
            for (auto bus = std::size_t {}; bus < node.inputBuses.size(); ++bus)
                node.inputBusViews[bus] = { node.inputBuses[bus].portId, node.inputBuses[bus].buffer.view(frames) };
            node.multiBusProcessor->processAudioBuses(node.inputBusViews,
                                                       nodeOutput, context);
        } else {
            node.processor->process(nodeInput, nodeOutput, context);
        }
        nodeLatency_[index] = std::min(maximumLatency_, arrival + std::min(node.processor->latencySamples(),
                                                                           node.processor->maximumLatencySamples()));
    }

    copyAudio({ output.data, output.channelCount, frames, output.channelStride },
              nodes_[outputNodeIndex_].outputBuffer.view(frames));
}

bool AudioRenderPlan::lastSignals(const NodeId node, const std::uint32_t frameCount, AudioBlockView& input,
                                  AudioBlockView& output) noexcept
{
    const auto frames = std::min(frameCount, spec_.maximumBlockSize);
    for (auto& candidate : nodes_)
        if (candidate.id == node) {
            input = candidate.inputBuffer.view(frames);
            output = candidate.outputBuffer.view(frames);
            return true;
        }
    return false;
}

void AudioRenderPlan::reset() noexcept
{
    for (auto& node : nodes_) {
        node.processor->reset();
        node.inputBuffer.clear(spec_.maximumBlockSize);
        node.outputBuffer.clear(spec_.maximumBlockSize);
    }
    for (auto& line : delayLines_) {
        line.ring.clear(line.ring.maximumFrameCount());
        line.write = 0;
    }
}

std::uint32_t AudioRenderPlan::latencySamples() const noexcept
{
    // The nodes are in processing order: a node's sources come before it.
    // Not the audio thread's own figures (nodeLatency_), which it is writing:
    // this is for the control thread, which may allocate.
    if (maximumLatency_ == 0)
        return 0;
    std::vector<std::uint32_t> latencies;
    try {
        latencies.assign(nodes_.size(), 0);
    } catch (...) {
        return maximumLatency_;
    }
    for (std::size_t index = 0; index < nodes_.size(); ++index) {
        auto arrival = std::uint32_t {};
        for (const auto& predecessor : nodes_[index].predecessors)
            arrival = std::max(arrival, latencies[predecessor.sourceNodeIndex]);
        const auto& processor = *nodes_[index].processor;
        latencies[index] = std::min(maximumLatency_, arrival + std::min(processor.latencySamples(), processor.maximumLatencySamples()));
    }
    return latencies[outputNodeIndex_];
}

const ProcessSpec& AudioRenderPlan::spec() const noexcept { return spec_; }
std::size_t AudioRenderPlan::nodeCount() const noexcept { return nodes_.size(); }

std::size_t AudioRenderPlan::connectionCount() const noexcept
{
    auto count = std::size_t {};
    for (const auto& node : nodes_)
        count += node.predecessors.size();
    return count;
}

std::size_t AudioRenderPlan::inputBusCount(const NodeId node) const noexcept
{
    const auto found = std::find_if(
        nodes_.begin(), nodes_.end(), [node](const RuntimeNode& value) {
            return value.id == node;
        });
    return found == nodes_.end() ? 0 : found->inputBuses.size();
}

bool AudioRenderPlan::hasConnection(const NodeId source,
                                    const std::uint64_t sourcePort,
                                    const NodeId destination,
                                    const std::uint64_t destinationPort) const noexcept
{
    const auto node = std::find_if(
        nodes_.begin(), nodes_.end(), [destination](const RuntimeNode& value) {
            return value.id == destination;
        });
    if (node == nodes_.end())
        return false;
    return std::any_of(
        node->predecessors.begin(), node->predecessors.end(),
        [source, sourcePort, destinationPort](const RuntimeConnection& value) {
            return value.sourceNodeId == source
                   && value.sourcePort == sourcePort
                   && value.destinationPort == destinationPort;
        });
}

RenderPlanError AudioRenderPlanBuilder::addNode(const NodeId id,
                                                 std::unique_ptr<AudioProcessor> processor,
                                                 const bool receivesExternalInput,
                                                 const std::uint32_t externalBus)
{
    if (id == 0 || processor == nullptr)
        return RenderPlanError::invalidNode;
    if (nodes_.contains(id))
        return RenderPlanError::duplicateNode;
    nodes_.emplace(id, BuildNode { std::move(processor), receivesExternalInput, externalBus });
    return RenderPlanError::none;
}

RenderPlanError AudioRenderPlanBuilder::connect(
    const NodeId source, const NodeId destination,
    const std::uint64_t sourcePort, const std::uint64_t destinationPort)
{
    if (!nodes_.contains(source) || !nodes_.contains(destination))
        return RenderPlanError::missingNode;
    const BuildConnection connection { source, destination, sourcePort,
                                       destinationPort };
    if (std::find(connections_.begin(), connections_.end(), connection) != connections_.end())
        return RenderPlanError::duplicateConnection;
    connections_.push_back(connection);
    return RenderPlanError::none;
}

RenderPlanResult AudioRenderPlanBuilder::compile(const ProcessSpec spec, const NodeId outputNodeId)
{
    if (!std::isfinite(spec.sampleRate) || spec.sampleRate <= 0.0
        || spec.maximumBlockSize == 0 || spec.channelCount == 0)
        return { RenderPlanError::invalidSpec, "The audio process specification is invalid", nullptr };
    if (!nodes_.contains(outputNodeId))
        return { RenderPlanError::missingOutput, "The output node does not exist", nullptr };

    std::unordered_map<NodeId, std::size_t> incoming;
    std::unordered_map<NodeId, std::vector<NodeId>> outgoing;
    std::unordered_map<NodeId, std::vector<BuildConnection>> predecessors;
    for (const auto& [id, unused] : nodes_) {
        (void) unused;
        incoming.emplace(id, 0);
    }
    for (const auto& connection : connections_) {
        ++incoming[connection.destination];
        outgoing[connection.source].push_back(connection.destination);
        predecessors[connection.destination].push_back(connection);
    }

    std::priority_queue<NodeId, std::vector<NodeId>, std::greater<>> ready;
    for (const auto& [id, count] : incoming)
        if (count == 0) ready.push(id);

    std::vector<NodeId> processingOrder;
    while (!ready.empty()) {
        const auto id = ready.top();
        ready.pop();
        processingOrder.push_back(id);
        for (const auto destination : outgoing[id])
            if (--incoming[destination] == 0) ready.push(destination);
    }
    if (processingOrder.size() != nodes_.size())
        return { RenderPlanError::cycleDetected, "The render graph contains a cycle", nullptr };

    auto plan = std::make_unique<AudioRenderPlan>();
    plan->spec_ = spec;
    plan->nodes_.reserve(nodes_.size());
    std::unordered_map<NodeId, std::size_t> runtimeIndices;
    for (const auto id : processingOrder) {
        const auto index = plan->nodes_.size();
        runtimeIndices.emplace(id, index);
        auto& buildNode = nodes_.at(id);
        buildNode.processor->prepare(spec);
        AudioRenderPlan::RuntimeNode runtimeNode;
        runtimeNode.id = id;
        runtimeNode.processor = std::move(buildNode.processor);
        runtimeNode.multiBusProcessor =
            dynamic_cast<MultiBusAudioProcessor*>(runtimeNode.processor.get());
        runtimeNode.inputBuffer = AudioBuffer(spec.channelCount,
                                              spec.maximumBlockSize);
        runtimeNode.outputBuffer = AudioBuffer(spec.channelCount,
                                               spec.maximumBlockSize);
        runtimeNode.receivesExternalInput = buildNode.receivesExternalInput;
        runtimeNode.externalBus = buildNode.externalBus;
        plan->nodes_.push_back(std::move(runtimeNode));
    }

    for (auto& runtimeNode : plan->nodes_) {
        for (const auto& predecessor : predecessors[runtimeNode.id]) {
            runtimeNode.predecessors.push_back(
                { predecessor.source,
                  runtimeIndices.at(predecessor.source),
                  predecessor.sourcePort,
                  predecessor.destinationPort });
            const auto existingBus = std::find_if(
                runtimeNode.inputBuses.begin(), runtimeNode.inputBuses.end(),
                [&predecessor](
                    const AudioRenderPlan::RuntimeNode::InputBus& bus) {
                    return bus.portId == predecessor.destinationPort;
                });
            if (existingBus == runtimeNode.inputBuses.end())
                runtimeNode.inputBuses.push_back(
                    { predecessor.destinationPort,
                      AudioBuffer(spec.channelCount, spec.maximumBlockSize) });
        }
        runtimeNode.inputBusViews.resize(runtimeNode.inputBuses.size());
    }
    plan->outputNodeIndex_ = runtimeIndices.at(outputNodeId);

    // The latency compensation inside the graph. No path can be later than
    // all the nodes' latencies added; when none can have any, nothing is
    // kept. Otherwise every cable into a node that more than one path
    // reaches gets a line to wait in.
    auto total = std::uint64_t {};
    for (const auto& runtimeNode : plan->nodes_)
        total += runtimeNode.processor->maximumLatencySamples();
    constexpr std::uint64_t longestWait = 1u << 17;
    plan->maximumLatency_ = static_cast<std::uint32_t>(std::min(total, longestWait));
    plan->nodeLatency_.assign(plan->nodes_.size(), 0);
    if (plan->maximumLatency_ > 0)
        for (auto& runtimeNode : plan->nodes_) {
            const auto paths = runtimeNode.predecessors.size() + (runtimeNode.receivesExternalInput ? 1 : 0);
            if (paths < 2)
                continue;
            for (auto& predecessor : runtimeNode.predecessors) {
                predecessor.delayLine = plan->delayLines_.size();
                AudioRenderPlan::DelayLine line;
                line.ring = AudioBuffer(spec.channelCount, plan->maximumLatency_ + 1);
                line.scratch = AudioBuffer(spec.channelCount, spec.maximumBlockSize);
                plan->delayLines_.push_back(std::move(line));
            }
        }
    return { RenderPlanError::none, {}, std::move(plan) };
}

} // namespace cydaw::engine
