#pragma once

#include "NativeDspRuntime.h"

#include <cydaw/realtime/RtPublisher.h>

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace cydaw::app {

enum class VirtualBusDirection : int {
    input = 0,
    output = 1
};

// A virtual input or output of the software over the sound card's channels:
// an input bus is what a track records (a microphone, a stereo pair, a quad
// or 5.1 rig), an output bus where the master and the tracks sent to it come
// out. One to eight channels, in the order of cydaw/engine/ChannelLayout.h.
// Each has a chain of effects (its graph): an input's treats the sound
// before any track takes it (a microphone's compressor and limiter), and a
// track takes it before or after them, as it chooses; an output's treats
// what leaves (a tracks sent before it skip it).
struct VirtualAudioBus {
    static constexpr int maximumChannels = 8;
    std::uint64_t id {};
    juce::String name;
    VirtualBusDirection direction { VirtualBusDirection::input };
    int channels { 2 };
    int physicalChannelA {};
    int physicalChannelB { 1 };
    int destinationChannelA {};
    int destinationChannelB { 1 };
    // Its third to eighth channels: the sound card's inputs they read, for
    // an input bus, or its outputs they feed, for an output bus.
    std::array<int, 6> moreChannels { 2, 3, 4, 5, 6, 7 };
    float gainDb {};
    bool muted {};
    bool monitor {};
    bool exposeToSystem {};
    // An output bus plays the master mix, with what tracks send it
    // directly; without it, only what tracks send it.
    bool carriesMaster { true };

    // Its k-th channel on the sound card: the input read (an input bus),
    // the output fed (an output bus).
    [[nodiscard]] int deviceChannel(const int k) const noexcept
    {
        const auto input = direction == VirtualBusDirection::input;
        if (k <= 0)
            return input ? physicalChannelA : destinationChannelA;
        if (k == 1)
            return input ? physicalChannelB : destinationChannelB;
        return moreChannels[static_cast<std::size_t>(std::min(k, maximumChannels - 1) - 2)];
    }
    void setDeviceChannel(const int k, const int channel) noexcept
    {
        const auto input = direction == VirtualBusDirection::input;
        if (k <= 0)
            (input ? physicalChannelA : destinationChannelA) = channel;
        else if (k == 1)
            (input ? physicalChannelB : destinationChannelB) = channel;
        else
            moreChannels[static_cast<std::size_t>(std::min(k, maximumChannels - 1) - 2)] = channel;
    }
};

// What tracks send straight to an output bus, beside the master: filled by
// the tracks' mixer while the router pulls the mix, read by the router just
// after, in the same callback.
class DirectOutputSource {
public:
    virtual ~DirectOutputSource() = default;
    // This block's sound for that bus (in its channels), or null: what goes
    // through its effects, or what joins after them.
    [[nodiscard]] virtual const juce::AudioBuffer<float>* directOutput(std::uint64_t busId, int numSamples,
                                                                       bool beforeEffects) noexcept = 0;
    // The block read: nothing of it is left for the next.
    virtual void finishDirectOutputs() noexcept = 0;
};

class VirtualBusRouter final : public juce::AudioSource,
                               public juce::AudioIODeviceCallback {
public:
    static constexpr std::size_t maximumBusCount = 16;

    explicit VirtualBusRouter(juce::AudioSource& source);

    // Where the tracks sent to an output bus are found (the tracks' mixer).
    void setDirectOutputs(DirectOutputSource* source) noexcept { directOutputs_ = source; }

    // An input bus as the device callback left it this block, for the
    // recorder that runs after it: its channels before its effects (its level
    // applied) or after them. False when it has none of `numSamples`.
    [[nodiscard]] bool inputBusBlock(std::uint64_t busId, bool afterEffects, int numSamples,
                                     std::array<const float*, 8>& channels, int& channelCount) const noexcept;
    // The bus chain's displays: nodes tapped, and what they saw.
    void setTaps(std::uint64_t busId, const std::vector<NativeDspRuntime::TapRequest>& taps);
    [[nodiscard]] bool readTap(std::uint64_t busId, std::uint64_t nodeId, bool input,
                               cydaw::engine::AudioAnalysisFrame& frame) const;

    [[nodiscard]] std::vector<VirtualAudioBus> buses() const;
    [[nodiscard]] std::optional<VirtualAudioBus> bus(
        std::uint64_t busId) const;
    [[nodiscard]] std::uint64_t addBus(VirtualBusDirection direction);
    [[nodiscard]] bool updateBus(const VirtualAudioBus& bus);
    [[nodiscard]] bool removeBus(std::uint64_t busId);
    void replaceBuses(std::vector<VirtualAudioBus> buses);
    void resetToDefaults();
    [[nodiscard]] std::uint64_t defaultInputBusId() const noexcept;
    [[nodiscard]] std::array<float, 2> meterLevels(
        std::uint64_t busId) const noexcept;
    [[nodiscard]] juce::Result setGraph(
        std::uint64_t busId, const cydaw::engine::NativeDspGraph& graph);
    [[nodiscard]] juce::Result updateGraphParameters(
        std::uint64_t busId, const cydaw::engine::NativeDspGraph& graph);
    [[nodiscard]] cydaw::engine::NativeDspGraph graph(
        std::uint64_t busId) const;
    // The bus graph's control nodes and what each put out last block.
    [[nodiscard]] std::vector<std::pair<std::uint64_t, float>> controlValues(
        std::uint64_t busId) const;

    void prepareToPlay(int samplesPerBlockExpected,
                       double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(
        const juce::AudioSourceChannelInfo& bufferToFill) override;

    void audioDeviceIOCallbackWithContext(
        const float* const* inputChannelData, int numInputChannels,
        float* const* outputChannelData, int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    // An input bus's channels read from the sound card's (`outputs` holds
    // the bus's channel count), at its level.
    static void renderInputBus(const VirtualAudioBus& bus,
                               const float* const* inputChannelData,
                               int numInputChannels,
                               float* const* outputs,
                               int numSamples) noexcept;
    // Its first two, as a stereo pair (a mono bus's second is its first).
    static void renderInputBus(const VirtualAudioBus& bus,
                               const float* const* inputChannelData,
                               int numInputChannels,
                               float* outputA, float* outputB,
                               int numSamples) noexcept;

private:
    struct SnapshotEntry {
        VirtualAudioBus bus;
        std::shared_ptr<NativeDspRuntime> graph;
    };
    using Snapshot = std::vector<SnapshotEntry>;

    void publishSnapshot();
    static void applyBusLevel(const VirtualAudioBus& bus, float* const* channels,
                              int numSamples) noexcept;
    void publishMeter(std::size_t slot, const float* const* channels,
                      int channelCount, int numSamples) noexcept;

    juce::AudioSource& source_;
    DirectOutputSource* directOutputs_ {};
    mutable juce::CriticalSection busesLock_;
    std::vector<VirtualAudioBus> buses_;
    std::unordered_map<std::uint64_t, std::shared_ptr<NativeDspRuntime>> graphs_;
    // The audio callback borrows this through a lock-free pointer; a bus that
    // is added, removed or re-graphed retires its snapshot for the message
    // thread to release rather than freeing it inside a block.
    cydaw::realtime::RtPublisher<Snapshot> snapshot_;
    std::uint64_t nextBusId_ { 1 };
    std::uint64_t defaultInputBusId_ {};
    juce::AudioBuffer<float> sourceBuffer_;
    juce::AudioBuffer<float> inputBusBuffer_;
    // Every input bus each block, before and after its effects, by slot.
    std::array<juce::AudioBuffer<float>, maximumBusCount> inputsBefore_;
    std::array<juce::AudioBuffer<float>, maximumBusCount> inputsAfter_;
    std::array<std::uint64_t, maximumBusCount> inputIds_ {};
    std::array<int, maximumBusCount> inputChannels_ {};
    int inputSamples_ {};
    std::array<std::atomic<float>, maximumBusCount * 2> meters_ {};
    double sampleRate_ { 48'000.0 };
    int maximumBlockSize_ { 512 };
};

} // namespace cydaw::app
