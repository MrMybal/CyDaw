#include "VirtualAudioBus.h"

#include <cydaw/engine/ChannelLayout.h>

#include <algorithm>
#include <cmath>

namespace cydaw::app {
namespace {
VirtualAudioBus normaliseBus(VirtualAudioBus bus)
{
    bus.name = bus.name.trim().substring(0, 64);
    if (bus.name.isEmpty())
        bus.name = bus.direction == VirtualBusDirection::input
            ? "Input" : "Output";
    bus.channels = juce::jlimit(1, VirtualAudioBus::maximumChannels, bus.channels);
    bus.physicalChannelA = std::max(0, bus.physicalChannelA);
    bus.physicalChannelB = std::max(0, bus.physicalChannelB);
    bus.destinationChannelA = std::max(0, bus.destinationChannelA);
    bus.destinationChannelB = std::max(0, bus.destinationChannelB);
    for (auto& channel : bus.moreChannels)
        channel = std::max(0, channel);
    bus.gainDb = juce::jlimit(-60.0f, 24.0f, bus.gainDb);
    return bus;
}

std::shared_ptr<NativeDspRuntime> makeBusRuntime(
    const double sampleRate, const int blockSize, const int channels)
{
    auto runtime = std::make_shared<NativeDspRuntime>();
    runtime->prepare(sampleRate, blockSize, channels);
    return runtime;
}

constexpr auto maximumChannels = static_cast<std::size_t>(VirtualAudioBus::maximumChannels);
}

VirtualBusRouter::VirtualBusRouter(juce::AudioSource& source)
    : source_(source)
{
    resetToDefaults();
}

std::vector<VirtualAudioBus> VirtualBusRouter::buses() const
{
    const juce::ScopedLock lock(busesLock_);
    return buses_;
}

std::optional<VirtualAudioBus> VirtualBusRouter::bus(
    const std::uint64_t busId) const
{
    const juce::ScopedLock lock(busesLock_);
    const auto found = std::find_if(
        buses_.begin(), buses_.end(), [busId](const auto& candidate) {
            return candidate.id == busId;
        });
    return found != buses_.end()
        ? std::optional<VirtualAudioBus>(*found) : std::nullopt;
}

std::uint64_t VirtualBusRouter::addBus(const VirtualBusDirection direction)
{
    const juce::ScopedLock lock(busesLock_);
    if (buses_.size() >= maximumBusCount)
        return 0;
    const auto ordinal = static_cast<int>(std::count_if(
        buses_.begin(), buses_.end(), [direction](const auto& candidate) {
            return candidate.direction == direction;
        })) + 1;
    VirtualAudioBus bus;
    bus.id = nextBusId_++;
    bus.direction = direction;
    bus.name = direction == VirtualBusDirection::input
        ? "Input " + juce::String(ordinal)
        : "Output " + juce::String(ordinal);
    bus.monitor = false;
    buses_.push_back(bus);
    graphs_[bus.id] = makeBusRuntime(
        sampleRate_, maximumBlockSize_, bus.channels);
    if (direction == VirtualBusDirection::input
        && defaultInputBusId_ == 0)
        defaultInputBusId_ = bus.id;
    publishSnapshot();
    return bus.id;
}

bool VirtualBusRouter::updateBus(const VirtualAudioBus& updated)
{
    const juce::ScopedLock lock(busesLock_);
    const auto found = std::find_if(
        buses_.begin(), buses_.end(), [&updated](const auto& candidate) {
            return candidate.id == updated.id;
        });
    if (found == buses_.end())
        return false;
    auto normalised = normaliseBus(updated);
    normalised.direction = found->direction;
    *found = std::move(normalised);
    if (const auto graph = graphs_.find(found->id);
        graph != graphs_.end() && graph->second != nullptr)
        graph->second->prepare(sampleRate_, maximumBlockSize_,
                               found->channels);
    publishSnapshot();
    return true;
}

bool VirtualBusRouter::removeBus(const std::uint64_t busId)
{
    const juce::ScopedLock lock(busesLock_);
    const auto found = std::find_if(
        buses_.begin(), buses_.end(), [busId](const auto& candidate) {
            return candidate.id == busId;
        });
    if (found == buses_.end())
        return false;
    const auto outputCount = std::count_if(
        buses_.begin(), buses_.end(), [](const auto& candidate) {
            return candidate.direction == VirtualBusDirection::output;
        });
    const auto inputCount = static_cast<int>(buses_.size()) - outputCount;
    if ((found->direction == VirtualBusDirection::output && outputCount <= 1)
        || (found->direction == VirtualBusDirection::input && inputCount <= 1))
        return false;
    const auto removedDefault = busId == defaultInputBusId_;
    buses_.erase(found);
    graphs_.erase(busId);
    if (removedDefault) {
        const auto replacement = std::find_if(
            buses_.begin(), buses_.end(), [](const auto& candidate) {
                return candidate.direction == VirtualBusDirection::input;
            });
        defaultInputBusId_ = replacement != buses_.end()
            ? replacement->id : 0;
    }
    publishSnapshot();
    return true;
}

void VirtualBusRouter::replaceBuses(std::vector<VirtualAudioBus> buses)
{
    const juce::ScopedLock lock(busesLock_);
    std::vector<std::uint64_t> ids;
    buses.erase(std::remove_if(
                    buses.begin(), buses.end(), [&ids](auto& bus) {
                        if (bus.id == 0
                            || std::find(ids.begin(), ids.end(), bus.id)
                                   != ids.end())
                            return true;
                        ids.push_back(bus.id);
                        bus = normaliseBus(std::move(bus));
                        return false;
                    }),
                buses.end());
    if (buses.size() > maximumBusCount)
        buses.resize(maximumBusCount);
    nextBusId_ = 1;
    for (const auto& bus : buses)
        nextBusId_ = std::max(nextBusId_, bus.id + 1);
    const auto hasInput = std::any_of(
        buses.begin(), buses.end(), [](const auto& bus) {
            return bus.direction == VirtualBusDirection::input;
        });
    const auto hasOutput = std::any_of(
        buses.begin(), buses.end(), [](const auto& bus) {
            return bus.direction == VirtualBusDirection::output;
        });
    if (!hasOutput) {
        if (buses.size() >= maximumBusCount)
            buses.pop_back();
        VirtualAudioBus output;
        output.id = nextBusId_++;
        output.name = "Main Out";
        output.direction = VirtualBusDirection::output;
        buses.push_back(output);
    }
    if (!hasInput) {
        if (buses.size() >= maximumBusCount)
            buses.pop_back();
        VirtualAudioBus input;
        input.id = nextBusId_++;
        input.name = "Input 1";
        input.direction = VirtualBusDirection::input;
        buses.push_back(input);
    }
    buses_ = std::move(buses);
    for (auto iterator = graphs_.begin(); iterator != graphs_.end();) {
        const auto stillExists = std::any_of(
            buses_.begin(), buses_.end(), [id = iterator->first](const auto& bus) {
                return bus.id == id;
            });
        if (!stillExists)
            iterator = graphs_.erase(iterator);
        else
            ++iterator;
    }
    for (const auto& bus : buses_) {
        if (!graphs_.contains(bus.id))
            graphs_[bus.id] = makeBusRuntime(
                sampleRate_, maximumBlockSize_, bus.channels);
        else if (graphs_[bus.id] != nullptr)
            graphs_[bus.id]->prepare(sampleRate_, maximumBlockSize_, bus.channels);
    }
    defaultInputBusId_ = 0;
    for (const auto& bus : buses_) {
        if (defaultInputBusId_ == 0
            && bus.direction == VirtualBusDirection::input)
            defaultInputBusId_ = bus.id;
    }
    publishSnapshot();
}

void VirtualBusRouter::resetToDefaults()
{
    const juce::ScopedLock lock(busesLock_);
    buses_.clear();
    graphs_.clear();
    nextBusId_ = 1;
    VirtualAudioBus output;
    output.id = nextBusId_++;
    output.name = "Main Out";
    output.direction = VirtualBusDirection::output;
    buses_.push_back(output);
    graphs_[output.id] = makeBusRuntime(
        sampleRate_, maximumBlockSize_, output.channels);
    VirtualAudioBus input;
    input.id = nextBusId_++;
    input.name = "Input 1";
    input.direction = VirtualBusDirection::input;
    input.monitor = false;
    defaultInputBusId_ = input.id;
    buses_.push_back(input);
    graphs_[input.id] = makeBusRuntime(
        sampleRate_, maximumBlockSize_, input.channels);
    publishSnapshot();
}

std::uint64_t VirtualBusRouter::defaultInputBusId() const noexcept
{
    return defaultInputBusId_;
}

std::array<float, 2> VirtualBusRouter::meterLevels(
    const std::uint64_t busId) const noexcept
{
    const auto snapshot = snapshot_.current();
    if (snapshot == nullptr)
        return {};
    for (std::size_t index = 0;
         index < snapshot->size() && index < maximumBusCount; ++index)
        if ((*snapshot)[index].bus.id == busId)
            return { meters_[index * 2].load(std::memory_order_relaxed),
                     meters_[index * 2 + 1].load(std::memory_order_relaxed) };
    return {};
}

void VirtualBusRouter::publishSnapshot()
{
    auto snapshot = std::make_shared<Snapshot>();
    snapshot->reserve(buses_.size());
    for (const auto& bus : buses_) {
        auto found = graphs_.find(bus.id);
        if (found == graphs_.end())
            found = graphs_.emplace(
                bus.id, makeBusRuntime(sampleRate_, maximumBlockSize_,
                                       bus.channels)).first;
        snapshot->push_back({ bus, found->second });
    }
    snapshot_.store(std::move(snapshot));
}

juce::Result VirtualBusRouter::setGraph(
    const std::uint64_t busId,
    const cydaw::engine::NativeDspGraph& graph)
{
    const juce::ScopedLock lock(busesLock_);
    const auto bus = std::find_if(
        buses_.begin(), buses_.end(), [busId](const auto& candidate) {
            return candidate.id == busId;
        });
    if (bus == buses_.end())
        return juce::Result::fail("Unknown virtual bus");
    auto& runtime = graphs_[busId];
    if (runtime == nullptr)
        runtime = makeBusRuntime(sampleRate_, maximumBlockSize_, bus->channels);
    const auto result = runtime->setGraph(graph);
    if (result.wasOk())
        publishSnapshot();
    return result;
}

juce::Result VirtualBusRouter::updateGraphParameters(
    const std::uint64_t busId,
    const cydaw::engine::NativeDspGraph& graph)
{
    const juce::ScopedLock lock(busesLock_);
    const auto bus = std::find_if(
        buses_.begin(), buses_.end(), [busId](const auto& candidate) {
            return candidate.id == busId;
        });
    const auto runtime = graphs_.find(busId);
    if (bus == buses_.end() || runtime == graphs_.end()
        || runtime->second == nullptr)
        return juce::Result::fail("Unknown virtual bus");
    return runtime->second->updateParameters(graph);
}

std::vector<std::pair<std::uint64_t, float>> VirtualBusRouter::controlValues(
    const std::uint64_t busId) const
{
    const juce::ScopedLock lock(busesLock_);
    const auto found = graphs_.find(busId);
    return found != graphs_.end() && found->second != nullptr
               ? found->second->controlValues()
               : std::vector<std::pair<std::uint64_t, float>> {};
}

cydaw::engine::NativeDspGraph VirtualBusRouter::graph(
    const std::uint64_t busId) const
{
    const juce::ScopedLock lock(busesLock_);
    const auto found = graphs_.find(busId);
    return found != graphs_.end() && found->second != nullptr
               ? found->second->graph()
               : makePassthroughAudioGraph();
}

void VirtualBusRouter::prepareToPlay(const int samplesPerBlockExpected,
                                     const double sampleRate)
{
    maximumBlockSize_ = std::max(1, samplesPerBlockExpected);
    sampleRate_ = std::max(1.0, sampleRate);
    sourceBuffer_.setSize(2, maximumBlockSize_, false, false, true);
    inputBusBuffer_.setSize(VirtualAudioBus::maximumChannels, maximumBlockSize_, false, false, true);
    for (auto& buffer : inputsBefore_)
        buffer.setSize(VirtualAudioBus::maximumChannels, maximumBlockSize_, false, true, true);
    for (auto& buffer : inputsAfter_)
        buffer.setSize(VirtualAudioBus::maximumChannels, maximumBlockSize_, false, true, true);
    {
        const juce::ScopedLock lock(busesLock_);
        for (const auto& bus : buses_)
            if (auto found = graphs_.find(bus.id);
                found != graphs_.end() && found->second != nullptr)
                found->second->prepare(sampleRate_, maximumBlockSize_,
                                       bus.channels);
        publishSnapshot();
    }
    source_.prepareToPlay(samplesPerBlockExpected, sampleRate);
}

void VirtualBusRouter::releaseResources()
{
    source_.releaseResources();
    sourceBuffer_.setSize(0, 0);
    inputBusBuffer_.setSize(0, 0);
}

void VirtualBusRouter::applyBusLevel(
    const VirtualAudioBus& bus, float* const* channels,
    const int numSamples) noexcept
{
    const auto gain = bus.muted ? 0.0f : juce::Decibels::decibelsToGain(bus.gainDb, -90.0f);
    for (auto channel = 0; channel < bus.channels; ++channel) {
        if (channels[channel] == nullptr)
            continue;
        if (gain == 0.0f)
            juce::FloatVectorOperations::clear(channels[channel], numSamples);
        else if (gain != 1.0f)
            juce::FloatVectorOperations::multiply(channels[channel], gain, numSamples);
    }
}

void VirtualBusRouter::publishMeter(const std::size_t slot,
                                    const float* const* channels,
                                    const int channelCount,
                                    const int numSamples) noexcept
{
    if (slot >= maximumBusCount || channelCount <= 0 || numSamples <= 0)
        return;
    // The left side's speakers on the first bar, the right side's on the
    // second; the middle ones (a centre, a mono bus) on both.
    std::array<float, 2> peaks {};
    for (auto channel = 0; channel < channelCount; ++channel) {
        if (channels[channel] == nullptr)
            continue;
        const auto range = juce::FloatVectorOperations::findMinAndMax(channels[channel], numSamples);
        const auto peak = std::max(std::abs(range.getStart()), std::abs(range.getEnd()));
        const auto side = cydaw::engine::speakerSide(cydaw::engine::speakerRole(channelCount, channel));
        if (side <= 0)
            peaks[0] = std::max(peaks[0], peak);
        if (side >= 0)
            peaks[1] = std::max(peaks[1], peak);
    }
    meters_[slot * 2].store(peaks[0], std::memory_order_relaxed);
    meters_[slot * 2 + 1].store(peaks[1], std::memory_order_relaxed);
}

void VirtualBusRouter::getNextAudioBlock(
    const juce::AudioSourceChannelInfo& bufferToFill)
{
    if (bufferToFill.buffer == nullptr || bufferToFill.numSamples <= 0)
        return;
    if (bufferToFill.numSamples > sourceBuffer_.getNumSamples()) {
        bufferToFill.clearActiveBufferRegion();
        return;
    }
    sourceBuffer_.clear();
    juce::AudioSourceChannelInfo sourceInfo(
        &sourceBuffer_, 0, bufferToFill.numSamples);
    source_.getNextAudioBlock(sourceInfo);
    bufferToFill.clearActiveBufferRegion();
    const auto snapshot = snapshot_.read();
    if (snapshot == nullptr) {
        if (directOutputs_ != nullptr)
            directOutputs_->finishDirectOutputs();
        return;
    }
    const auto count = bufferToFill.numSamples;
    const auto outputChannels = bufferToFill.buffer->getNumChannels();
    const std::array<const float*, 2> master { sourceBuffer_.getReadPointer(0), sourceBuffer_.getReadPointer(1) };
    for (std::size_t slot = 0;
         slot < snapshot->size() && slot < maximumBusCount; ++slot) {
        const auto& entry = (*snapshot)[slot];
        const auto& bus = entry.bus;
        if (bus.direction != VirtualBusDirection::output)
            continue;
        const auto channels = juce::jlimit(1, VirtualAudioBus::maximumChannels, bus.channels);
        std::array<float*, maximumChannels> lanes {};
        for (auto channel = 0; channel < channels; ++channel)
            lanes[static_cast<std::size_t>(channel)] = inputBusBuffer_.getWritePointer(channel);
        for (auto channel = 0; channel < channels; ++channel)
            juce::FloatVectorOperations::clear(lanes[static_cast<std::size_t>(channel)], count);
        // The master in the bus's format (a mono output folds it, a quad one
        // keeps it to the front), then what tracks send it directly.
        if (bus.carriesMaster)
            cydaw::engine::convertChannels(cydaw::engine::channelConversion(2, channels), master.data(), lanes.data(),
                                           count);
        const auto addDirect = [&](const bool beforeEffects) {
            if (directOutputs_ != nullptr)
                if (const auto* direct = directOutputs_->directOutput(bus.id, count, beforeEffects))
                    for (auto channel = 0; channel < std::min(channels, direct->getNumChannels()); ++channel)
                        juce::FloatVectorOperations::add(lanes[static_cast<std::size_t>(channel)],
                                                         direct->getReadPointer(channel), count);
        };
        // Through its effects: the master and the tracks sent before them;
        // then the tracks sent after them; then its level.
        addDirect(true);
        if (entry.graph != nullptr) {
            juce::AudioBuffer<float> view(lanes.data(), channels, count);
            (void) entry.graph->process(view, 0, count);
        }
        addDirect(false);
        applyBusLevel(bus, lanes.data(), count);
        for (auto channel = 0; channel < channels; ++channel) {
            const auto device = bus.deviceChannel(channel);
            if (device >= 0 && device < outputChannels)
                bufferToFill.buffer->addFrom(device, bufferToFill.startSample,
                                             lanes[static_cast<std::size_t>(channel)], count);
        }
        publishMeter(slot, lanes.data(), channels, count);
    }
    if (directOutputs_ != nullptr)
        directOutputs_->finishDirectOutputs();
}

void VirtualBusRouter::renderInputBus(
    const VirtualAudioBus& bus, const float* const* inputChannelData,
    const int numInputChannels, float* const* outputs,
    const int numSamples) noexcept
{
    if (outputs == nullptr || numSamples <= 0)
        return;
    const auto channels = juce::jlimit(1, VirtualAudioBus::maximumChannels, bus.channels);
    for (auto channel = 0; channel < channels; ++channel) {
        auto* output = outputs[channel];
        if (output == nullptr)
            continue;
        const auto device = bus.deviceChannel(channel);
        if (inputChannelData != nullptr && device >= 0 && device < numInputChannels
            && inputChannelData[device] != nullptr)
            juce::FloatVectorOperations::copy(output, inputChannelData[device], numSamples);
        else
            juce::FloatVectorOperations::clear(output, numSamples);
    }
    applyBusLevel(bus, outputs, numSamples);
}

void VirtualBusRouter::renderInputBus(
    const VirtualAudioBus& bus, const float* const* inputChannelData,
    const int numInputChannels, float* outputA, float* outputB,
    const int numSamples) noexcept
{
    if (outputA == nullptr || numSamples <= 0)
        return;
    auto pair = bus;
    pair.channels = std::min(2, bus.channels);
    std::array<float*, 2> outputs { outputA, pair.channels > 1 ? outputB : nullptr };
    renderInputBus(pair, inputChannelData, numInputChannels, outputs.data(), numSamples);
    if (outputB != nullptr && pair.channels == 1)
        juce::FloatVectorOperations::clear(outputB, numSamples);
}

void VirtualBusRouter::audioDeviceIOCallbackWithContext(
    const float* const* inputChannelData, const int numInputChannels,
    float* const* outputChannelData, const int numOutputChannels,
    const int numSamples,
    const juce::AudioIODeviceCallbackContext&)
{
    for (int channel = 0; channel < numOutputChannels; ++channel)
        if (outputChannelData != nullptr && outputChannelData[channel] != nullptr)
            juce::FloatVectorOperations::clear(
                outputChannelData[channel], numSamples);
    inputSamples_ = 0;
    inputIds_.fill(0);
    if (numSamples <= 0 || numSamples > inputBusBuffer_.getNumSamples())
        return;
    const auto snapshot = snapshot_.read();
    if (snapshot == nullptr)
        return;
    // Every input bus runs, heard or not: its effects' displays move, and a
    // track recording it takes it before or after them (inputBusBlock).
    for (std::size_t slot = 0;
         slot < snapshot->size() && slot < maximumBusCount; ++slot) {
        const auto& entry = (*snapshot)[slot];
        const auto& bus = entry.bus;
        if (bus.direction != VirtualBusDirection::input)
            continue;
        const auto channels = juce::jlimit(1, VirtualAudioBus::maximumChannels, bus.channels);
        auto& before = inputsBefore_[slot];
        auto& after = inputsAfter_[slot];
        if (before.getNumSamples() < numSamples || after.getNumSamples() < numSamples)
            continue;
        std::array<float*, maximumChannels> lanes {};
        for (auto channel = 0; channel < channels; ++channel)
            lanes[static_cast<std::size_t>(channel)] = after.getWritePointer(channel);
        renderInputBus(bus, inputChannelData, numInputChannels, lanes.data(), numSamples);
        for (auto channel = 0; channel < channels; ++channel)
            juce::FloatVectorOperations::copy(before.getWritePointer(channel), lanes[static_cast<std::size_t>(channel)],
                                              numSamples);
        if (entry.graph != nullptr) {
            juce::AudioBuffer<float> view(lanes.data(), channels, numSamples);
            (void) entry.graph->process(view, 0, numSamples);
        }
        inputIds_[slot] = bus.id;
        inputChannels_[slot] = channels;
        if (!bus.monitor) {
            publishMeter(slot, lanes.data(), channels, numSamples);
            continue;
        }
        // Heard on the pair it names, folded to stereo.
        if (outputChannelData != nullptr) {
            std::array<float*, 2> pair {};
            if (bus.destinationChannelA < numOutputChannels)
                pair[0] = outputChannelData[bus.destinationChannelA];
            if (bus.destinationChannelB < numOutputChannels && bus.destinationChannelB != bus.destinationChannelA)
                pair[1] = outputChannelData[bus.destinationChannelB];
            std::array<const float*, maximumChannels> from {};
            for (auto channel = 0; channel < channels; ++channel)
                from[static_cast<std::size_t>(channel)] = lanes[static_cast<std::size_t>(channel)];
            cydaw::engine::convertChannels(cydaw::engine::channelConversion(channels, 2), from.data(), pair.data(),
                                           numSamples);
        }
        publishMeter(slot, lanes.data(), channels, numSamples);
    }
    inputSamples_ = numSamples;
}

bool VirtualBusRouter::inputBusBlock(const std::uint64_t busId, const bool afterEffects, const int numSamples,
                                     std::array<const float*, 8>& channels, int& channelCount) const noexcept
{
    if (busId == 0 || numSamples <= 0 || numSamples != inputSamples_)
        return false;
    for (std::size_t slot = 0; slot < maximumBusCount; ++slot) {
        if (inputIds_[slot] != busId)
            continue;
        const auto& buffer = afterEffects ? inputsAfter_[slot] : inputsBefore_[slot];
        channelCount = std::min(inputChannels_[slot], static_cast<int>(channels.size()));
        for (auto channel = 0; channel < channelCount; ++channel)
            channels[static_cast<std::size_t>(channel)] = buffer.getReadPointer(channel);
        return channelCount > 0;
    }
    return false;
}

void VirtualBusRouter::setTaps(const std::uint64_t busId, const std::vector<NativeDspRuntime::TapRequest>& taps)
{
    const juce::ScopedLock lock(busesLock_);
    if (const auto found = graphs_.find(busId); found != graphs_.end() && found->second != nullptr)
        found->second->setTaps(taps);
}

bool VirtualBusRouter::readTap(const std::uint64_t busId, const std::uint64_t nodeId, const bool input,
                               cydaw::engine::AudioAnalysisFrame& frame) const
{
    const juce::ScopedLock lock(busesLock_);
    const auto found = graphs_.find(busId);
    return found != graphs_.end() && found->second != nullptr && found->second->readTap(nodeId, input, frame);
}

void VirtualBusRouter::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (device == nullptr)
        return;
    sampleRate_ = std::max(1.0, device->getCurrentSampleRate());
    maximumBlockSize_ = std::max(1, device->getCurrentBufferSizeSamples());
    inputBusBuffer_.setSize(VirtualAudioBus::maximumChannels, maximumBlockSize_, false, false, true);
    for (auto& buffer : inputsBefore_)
        buffer.setSize(VirtualAudioBus::maximumChannels, maximumBlockSize_, false, true, true);
    for (auto& buffer : inputsAfter_)
        buffer.setSize(VirtualAudioBus::maximumChannels, maximumBlockSize_, false, true, true);
    inputSamples_ = 0;
    {
        const juce::ScopedLock lock(busesLock_);
        for (const auto& bus : buses_)
            if (auto found = graphs_.find(bus.id);
                found != graphs_.end() && found->second != nullptr)
                found->second->prepare(sampleRate_, maximumBlockSize_,
                                       bus.channels);
        publishSnapshot();
    }
}

void VirtualBusRouter::audioDeviceStopped()
{
    for (auto& meter : meters_)
        meter.store(0.0f, std::memory_order_relaxed);
}

} // namespace cydaw::app
