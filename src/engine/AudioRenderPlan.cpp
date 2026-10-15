#include <cydaw/engine/AudioRenderPlan.h>
#include <cydaw/engine/EffectCatalogue.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>
#include <unordered_set>

namespace cydaw::engine {

namespace {

// A stretch of a block: `frames` from `offset`.
AudioBlockView piece(const AudioBlockView& view, const std::uint32_t offset, const std::uint32_t frames) noexcept
{
    return { view.data != nullptr ? view.data + offset : nullptr, view.channelCount, frames, view.channelStride };
}

// While a node's values glide, it processes this many frames at a time.
constexpr std::uint32_t kGlidePiece = 32;

} // namespace

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
        auto& processor = *node.processor;
        const auto run = [&node, &processor, nodeInput, nodeOutput, frames](const std::uint32_t offset, const std::uint32_t count,
                                                                            const ProcessContext& at) noexcept {
            if (node.multiBusProcessor != nullptr) {
                for (auto bus = std::size_t {}; bus < node.inputBuses.size(); ++bus)
                    node.inputBusViews[bus] = { node.inputBuses[bus].portId, piece(node.inputBuses[bus].buffer.view(frames), offset, count) };
                node.multiBusProcessor->processAudioBuses(node.inputBusViews, piece(nodeOutput, offset, count), at);
            } else {
                processor.process(piece(nodeInput, offset, count), piece(nodeOutput, offset, count), at);
            }
        };
        if (!processor.valuesGliding()) {
            run(0, frames, context);
        } else {
            // A knob being turned: short pieces, its values a little
            // further each, so the change is heard as a slope, not a jump.
            const auto playing = context.transport.state == PlaybackState::playing
                                 || context.transport.state == PlaybackState::recording;
            for (std::uint32_t offset = 0; offset < frames; offset += kGlidePiece) {
                const auto count = std::min(kGlidePiece, frames - offset);
                processor.glideValues(count, spec_.sampleRate);
                auto at = context;
                if (playing && offset > 0) {
                    at.transport.samplePosition += offset;
                    at.transport.quarterNotePosition += static_cast<double>(offset) / spec_.sampleRate
                                                        * context.transport.beatsPerMinute / 60.0;
                }
                run(offset, count, at);
            }
        }
        nodeLatency_[index] = std::min(maximumLatency_, arrival + std::min(node.processor->latencySamples(),
                                                                           node.processor->maximumLatencySamples()));
        if (bypasses_ != nullptr
            && (anyBypass_.load(std::memory_order_relaxed) || bypasses_[index].mix > 0.0f
                || bypasses_[index].ring.maximumFrameCount() > 1))
            bypass(index, frames);
        // How loud it came out, for the meters.
        if (peaks_ != nullptr) {
            const auto produced = nodes_[index].outputBuffer.view(frames);
            for (std::uint32_t channel = 0; channel < std::min<std::uint32_t>(2, produced.channelCount); ++channel) {
                const auto* samples = produced.channel(channel);
                auto loudest = 0.0f;
                for (std::uint32_t frame = 0; frame < frames; ++frame)
                    loudest = std::max(loudest, std::abs(samples[frame]));
                auto& kept = peaks_[index * 2 + channel];
                if (loudest > kept.load(std::memory_order_relaxed))
                    kept.store(loudest, std::memory_order_relaxed);
            }
        }
    }

    copyAudio({ output.data, output.channelCount, frames, output.channelStride },
              nodes_[outputNodeIndex_].outputBuffer.view(frames));
}

void AudioRenderPlan::mainInput(const std::size_t index, const AudioBlockView into) noexcept
{
    const auto frames = into.frameCount;
    auto& node = nodes_[index];
    copyAudio(into, node.inputBuffer.view(frames));
    for (auto& bus : node.inputBuses) {
        if (bus.portId != effectSidechainPortId)
            continue;
        const auto key = bus.buffer.view(frames);
        const auto channels = std::min(into.channelCount, key.channelCount);
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            auto* to = into.channel(channel);
            const auto* from = key.channel(channel);
            for (std::uint32_t frame = 0; frame < frames; ++frame)
                to[frame] -= from[frame];
        }
    }
}

void AudioRenderPlan::bypass(const std::size_t index, const std::uint32_t frames) noexcept
{
    auto& state = bypasses_[index];
    const auto wanted = state.wanted.load(std::memory_order_relaxed);
    if (wanted != -1)
        state.from = wanted;
    const auto target = wanted != -1 ? 1.0f : 0.0f;
    // A node with a latency keeps its line filled while it plays, so what
    // passes when it is switched off comes in step at once.
    const auto size = state.ring.maximumFrameCount();
    if (state.mix == 0.0f && target == 0.0f && size <= 1)
        return;
    // What passes instead: the input of the node it starts from, held back
    // as long as this one's own latency (a node's own input only: a module's
    // inner latency is not made up).
    const auto source = state.from >= 0 ? state.from : (size > 1 ? static_cast<std::int32_t>(index) : -1);
    const auto dry = bypassScratch_.view(frames);
    clearAudio(dry);
    if (source >= 0 && static_cast<std::size_t>(source) < nodes_.size()) {
        mainInput(static_cast<std::size_t>(source), dry);
        if (size > 1) {
            auto& processor = *nodes_[index].processor;
            const auto wait = std::min(std::min(processor.latencySamples(), processor.maximumLatencySamples()), size - 1);
            const auto ring = state.ring.view(size);
            const auto channels = std::min(ring.channelCount, dry.channelCount);
            for (std::uint32_t channel = 0; channel < channels; ++channel) {
                auto* stored = ring.channel(channel);
                auto* signal = dry.channel(channel);
                auto write = state.write;
                for (std::uint32_t frame = 0; frame < frames; ++frame) {
                    stored[write] = signal[frame];
                    signal[frame] = stored[write >= wait ? write - wait : write + size - wait];
                    if (++write == size)
                        write = 0;
                }
            }
            state.write = (state.write + frames) % size;
        }
    }
    if (state.mix == 0.0f && target == 0.0f)
        return;
    // Across ten milliseconds, sample by sample.
    const auto step = static_cast<float>(1.0 / std::max(1.0, 0.01 * spec_.sampleRate));
    const auto output = nodes_[index].outputBuffer.view(frames);
    const auto channels = std::min(output.channelCount, dry.channelCount);
    auto mix = state.mix;
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        mix = target > mix ? std::min(target, mix + step) : std::max(target, mix - step);
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            auto& sample = output.channel(channel)[frame];
            sample += (dry.channel(channel)[frame] - sample) * mix;
        }
    }
    state.mix = mix;
}

void AudioRenderPlan::takePeaks(const NodeId* nodes, const std::size_t count, float* left, float* right) noexcept
{
    for (std::size_t at = 0; at < count; ++at) {
        left[at] = 0.0f;
        right[at] = 0.0f;
        if (peaks_ == nullptr)
            continue;
        for (std::size_t index = 0; index < nodes_.size(); ++index) {
            if (nodes_[index].id != nodes[at])
                continue;
            left[at] = peaks_[index * 2].exchange(0.0f, std::memory_order_relaxed);
            right[at] = nodes_[index].outputBuffer.channelCount() > 1 ? peaks_[index * 2 + 1].exchange(0.0f, std::memory_order_relaxed)
                                                                      : left[at];
            break;
        }
    }
}

std::size_t AudioRenderPlan::readings(const NodeId node, float* into, const std::size_t count) const noexcept
{
    for (const auto& candidate : nodes_)
        if (candidate.id == node)
            return candidate.processor != nullptr ? candidate.processor->readings(into, count) : 0;
    return 0;
}

void AudioRenderPlan::setBypasses(const std::span<const BypassedNode> bypassed) noexcept
{
    if (bypasses_ == nullptr)
        return;
    const auto indexOf = [this](const NodeId id) -> std::int32_t {
        for (std::size_t index = 0; index < nodes_.size(); ++index)
            if (nodes_[index].id == id)
                return static_cast<std::int32_t>(index);
        return -1;
    };
    auto any = false;
    for (std::size_t index = 0; index < nodes_.size(); ++index) {
        auto wanted = std::int32_t { -1 };
        for (const auto& entry : bypassed) {
            if (entry.node != nodes_[index].id)
                continue;
            const auto from = entry.from == 0 ? -2 : indexOf(entry.from);
            wanted = from == -1 ? static_cast<std::int32_t>(index) : from;
        }
        any = any || wanted != -1;
        bypasses_[index].wanted.store(wanted, std::memory_order_relaxed);
    }
    anyBypass_.store(any, std::memory_order_release);
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
    // The switches, each node on; a node with a latency keeps a line for
    // its input to wait in when it is switched off.
    plan->bypasses_ = std::make_unique<AudioRenderPlan::Bypass[]>(plan->nodes_.size());
    for (std::size_t index = 0; index < plan->nodes_.size(); ++index)
        if (const auto latency = plan->nodes_[index].processor->maximumLatencySamples(); latency > 0)
            plan->bypasses_[index].ring = AudioBuffer(spec.channelCount, std::min<std::uint32_t>(latency, 1u << 17) + 1);
    plan->bypassScratch_ = AudioBuffer(spec.channelCount, spec.maximumBlockSize);
    plan->peaks_ = std::make_unique<std::atomic<float>[]>(plan->nodes_.size() * 2);
    return { RenderPlanError::none, {}, std::move(plan) };
}

} // namespace cydaw::engine
