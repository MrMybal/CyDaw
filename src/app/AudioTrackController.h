#pragma once

#include "AudioEdit.h"
#include "DjEngine.h"
#include "FoundryPlayer.h"
#include "IsolatedPluginHost.h"
#include "VirtualAudioBus.h"

#include <cydaw/engine/AutomationCurve.h>
#include <cydaw/engine/AudioBuffer.h>
#include <cydaw/engine/AudioAnalysis.h>
#include <cydaw/engine/NativeDspGraph.h>
#include <cydaw/engine/PolySynthGraph.h>
#include <cydaw/engine/VoiceLive.h>
#include <cydaw/realtime/RtPublisher.h>
#include <cydaw/realtime/SpscRing.h>

#include <juce_audio_utils/juce_audio_utils.h>
#include <functional>

#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <vector>

namespace cydaw::app {

// The points the audio thread reads: their shape (engine::CurveShape), its
// bend and its handles (engine/AutomationCurve.h).
struct GainAutomationPoint {
    double timeSeconds {};
    float valueDb {};
    int curve {};
    float curveAmount {};
    cydaw::engine::CurveHandles handles {};
};

struct PluginParameterAutomationPoint {
    double timeSeconds {};
    float normalisedValue {};
    int curve {};
    float curveAmount {};
    cydaw::engine::CurveHandles handles {};
};

struct PluginParameterAutomationLane {
    std::size_t pluginIndex {};
    std::uint32_t parameterIndex {};
    std::vector<PluginParameterAutomationPoint> points;
};

class LatencyCompensatingAudioSource final : public juce::AudioSource {
public:
    static constexpr int maximumDelaySamples = 131'072;
    static constexpr int maximumChannelCount = 8;

    explicit LatencyCompensatingAudioSource(juce::AudioSource& input) noexcept;

    // The channels its delay keeps (its track's format), taken at the next
    // prepare: set while it is out of the mixer.
    void setChannelCount(int channels) noexcept { channelCount_ = juce::jlimit(1, maximumChannelCount, channels); }
    void setDelaySamples(int delaySamples) noexcept;
    void requestReset() noexcept;
    [[nodiscard]] int delaySamples() const noexcept;
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;

private:
    juce::AudioSource& input_;
    juce::AudioBuffer<float> delayBuffer_;
    int channelCount_ { 2 };
    std::atomic<int> delaySamples_ {};
    std::atomic<bool> resetRequested_ { true };
    int writePosition_ {};
    int primedSamples_ {};
};

// A delay of whole samples, in place, on as many channels as it was
// prepared for. Its memory is taken when prepared (never on the audio
// thread); a change of length, or a reset, starts it again from silence.
class AudioDelayLine final {
public:
    void prepare(int maximumDelaySamples, int channels = 2);
    void setDelaySamples(int delaySamples) noexcept;
    [[nodiscard]] int delaySamples() const noexcept { return delay_.load(std::memory_order_acquire); }
    [[nodiscard]] int channelCount() const noexcept { return ring_.getNumChannels(); }
    void requestReset() noexcept { reset_.store(true, std::memory_order_release); }
    void process(juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept;

private:
    juce::AudioBuffer<float> ring_;
    std::atomic<int> delay_ {};
    std::atomic<bool> reset_ { true };
    int write_ {};
};

// A track that other tracks are routed into: the sum of what they send is
// handed to it, on the audio thread, just before it renders a block, and
// joins its own sound before its devices.
class RoutedInputReceiver {
public:
    // Its main input and three more (a sidechain, a second source).
    static constexpr std::size_t maximumBuses = 4;
    virtual ~RoutedInputReceiver() = default;
    // `buses[k]`: what reaches input k this block (`numSamples` from 0), or
    // null. The main input's joins the track's own sound; the others are
    // the graph's extra audio inputs.
    virtual void setRoutedInputs(const juce::AudioBuffer<float>* const* buses, int numSamples) noexcept = 0;
    // What reaches each input lately: its peak, falling by half every
    // 0.2 s (at 48 kHz) when nothing comes, for the meters.
    [[nodiscard]] float inputLevel(const std::size_t bus) const noexcept
    {
        return bus < maximumBuses ? inputLevels_[bus].load(std::memory_order_relaxed) : 0.0f;
    }

protected:
    void measureRoutedInputs(const juce::AudioBuffer<float>* const* buses, const int numSamples) noexcept
    {
        if (buses == nullptr || numSamples <= 0)
            return;
        const auto fall = std::pow(0.5f, static_cast<float>(numSamples) / 9'600.0f);
        for (std::size_t bus = 0; bus < maximumBuses; ++bus) {
            auto level = inputLevels_[bus].load(std::memory_order_relaxed) * fall;
            if (const auto* buffer = buses[bus]; buffer != nullptr && buffer->getNumSamples() > 0)
                level = std::max(level, buffer->getMagnitude(0, std::min(numSamples, buffer->getNumSamples())));
            inputLevels_[bus].store(level, std::memory_order_relaxed);
        }
    }

private:
    std::array<std::atomic<float>, maximumBuses> inputLevels_ {};
};

class GainPanAudioSource final : public juce::AudioSource, public RoutedInputReceiver {
public:
    static constexpr std::size_t maximumPluginCount = 8;

    explicit GainPanAudioSource(juce::AudioSource& input) noexcept;
    GainPanAudioSource(juce::AudioSource& input,
                       const std::atomic<double>& timelinePosition) noexcept;
    explicit GainPanAudioSource(juce::AudioTransportSource& input) noexcept;
    ~GainPanAudioSource() override;

    void setExternalTimeline(const std::atomic<double>* clock) noexcept { modeTimeline_ = clock; }
    void setExternalInput(juce::AudioSource* source) noexcept { externalInput_.store(source); }
    void setGain(float linearGain) noexcept;
    void setPan(float pan) noexcept;
    // What enters it lately (its clips, what it monitors, what is routed
    // into its main input), before its devices: a falling peak, for its
    // input meter.
    [[nodiscard]] float soundInLevel() const noexcept { return soundInLevel_.load(std::memory_order_relaxed); }
    // Its pan as it is now: a clip's pan lane moves it, else its knob.
    [[nodiscard]] float pan() const noexcept
    {
        return panAutomated_.load(std::memory_order_relaxed) ? automatedPan_.load(std::memory_order_relaxed)
                                                              : pan_.load(std::memory_order_relaxed);
    }
    // Its track's format: the channels its devices, its sends before the
    // fader and its input delay carry. Taken at the next prepare: set while
    // it is out of the mixer. The pan turns down the far side's speakers; a
    // mono track's is applied where the mixer spreads it.
    void setChannelCount(int channels) noexcept;
    [[nodiscard]] int channelCount() const noexcept { return graphChannelCount_; }
    void setAutomation(std::shared_ptr<const std::vector<GainAutomationPoint>> automation);
    // A pan lane (values -1..1 in the points' valueDb), on the same clock as
    // the volume's; empty: the pan knob.
    void setPanAutomation(std::shared_ptr<const std::vector<GainAutomationPoint>> lane);
    void setPluginAutomation(
        std::shared_ptr<const std::vector<PluginParameterAutomationLane>> automation);
    void setDrive(float driveDb) noexcept;
    void setFilterCutoff(float cutoffHz) noexcept;
    // Every card of a kind changed in the playing graph (control threads).
    void updateGraphNodes(cydaw::engine::NativeDspNodeKind kind,
                          const std::function<void(cydaw::engine::NativeDspNodeParameters&)>& change) noexcept;
    [[nodiscard]] juce::Result setNativeDspGraph(
        const cydaw::engine::NativeDspGraph& graph);
    [[nodiscard]] juce::Result updateNativeDspParameters(
        const cydaw::engine::NativeDspGraph& graph);
    [[nodiscard]] std::size_t nativeDspNodeCount() const noexcept;
    [[nodiscard]] bool tryPopAnalysisFrame(
        cydaw::engine::AudioAnalysisFrame& frame) noexcept;
    // The chain's control nodes and what each put out last block — an LFO's
    // position, a range node's mapped value. Read on the message thread.
    [[nodiscard]] std::vector<std::pair<std::uint64_t, float>>
        controlValues() const;
    // The loudest each plug-in gave out since it was last asked, in its order.
    void takePluginPeaks(std::vector<float>& left, std::vector<float>& right)
    {
        const auto count = std::min(pluginCount_.load(std::memory_order_acquire), maximumPluginCount);
        left.assign(count, 0.0f);
        right.assign(count, 0.0f);
        for (std::size_t index = 0; index < count; ++index) {
            left[index] = pluginPeaks_[index * 2].exchange(0.0f, std::memory_order_relaxed);
            right[index] = pluginPeaks_[index * 2 + 1].exchange(0.0f, std::memory_order_relaxed);
        }
    }
    [[nodiscard]] std::size_t nodeReadings(const std::uint64_t node, float* into, const std::size_t count) const
    {
        return nativeGraphSet_.load(std::memory_order_acquire) ? nativeGraph_.nodeReadings(node, into, count) : 0;
    }
    void takeNodePeaks(const std::uint64_t* nodes, std::size_t count, float* left, float* right) const
    {
        if (nativeGraphSet_.load(std::memory_order_acquire)) {
            nativeGraph_.takeNodePeaks(nodes, count, left, right);
        } else {
            std::fill_n(left, count, 0.0f);
            std::fill_n(right, count, 0.0f);
        }
    }
    // Signals of the chain's nodes, for the device panel's displays.
    void setNodeTaps(const std::vector<NativeDspRuntime::TapRequest>& taps) noexcept
    {
        nativeGraph_.setTaps(taps);
    }
    // The chain's parameters that follow the timeline.
    void setNativeAutomation(std::vector<NativeDspRuntime::AutomationLane> lanes)
    {
        nativeGraph_.setAutomation(std::move(lanes));
    }
    [[nodiscard]] bool readNodeTap(std::uint64_t nodeId, bool input,
                                   cydaw::engine::AudioAnalysisFrame& frame) const noexcept
    {
        return nativeGraph_.readTap(nodeId, input, frame);
    }
    [[nodiscard]] juce::Result addPlugin(const juce::File& pluginFile,
                                         double sampleRate,
                                         int maximumBlockSize,
                                         int channelCount,
                                         std::size_t* pluginIndex = nullptr);
    juce::Result reloadPlugin(std::size_t pluginIndex, double sampleRate, int blockSize, int channels);
    // whileHeld: run while no block plays the plug-ins (their automation
    // lanes renumbered with them).
    void removePlugin(std::size_t pluginIndex, const std::function<void()>& whileHeld = {});
    void clearPlugins();
    [[nodiscard]] bool movePlugin(std::size_t pluginIndex, int direction, const std::function<void()>& whileHeld = {});
    void pollPluginHealth();
    // The device's rate or block size changed: the workers started with
    // others start again (their state kept). Whether one did.
    bool followDevice(double sampleRate, int blockSize, int channels);
    [[nodiscard]] juce::Result getPluginState(std::size_t pluginIndex,
                                              juce::MemoryBlock& destination);
    [[nodiscard]] juce::Result getCachedPluginState(
        std::size_t pluginIndex,
        juce::MemoryBlock& destination) const;
    [[nodiscard]] juce::Result setPluginState(std::size_t pluginIndex,
                                              const juce::MemoryBlock& state);
    [[nodiscard]] juce::Result showPluginEditor(std::size_t pluginIndex);
    [[nodiscard]] juce::Result refreshPluginParameters(
        std::size_t pluginIndex,
        std::vector<IsolatedPluginHost::ParameterSnapshot>& destination);
    [[nodiscard]] juce::Result setPluginParameter(std::size_t pluginIndex,
                                                  std::uint32_t parameterIndex,
                                                  float normalisedValue);
    [[nodiscard]] std::size_t pluginCount() const noexcept;
    [[nodiscard]] IsolatedPluginHost* pluginHost(std::size_t pluginIndex) noexcept;
    [[nodiscard]] const IsolatedPluginHost* pluginHost(std::size_t pluginIndex) const noexcept;
    [[nodiscard]] int totalPluginLatencySamples() const noexcept;
    // What the track's sound is late by: its graph's effects (look-aheads,
    // oversamplers) and its plugins'. Control thread.
    [[nodiscard]] int totalLatencySamples() const noexcept;
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void setRoutedInputs(const juce::AudioBuffer<float>* const* buses, int numSamples) noexcept override
    {
        measureRoutedInputs(buses, numSamples);
        for (std::size_t bus = 0; bus < maximumBuses; ++bus)
            routedInputs_[bus] = buses != nullptr ? buses[bus] : nullptr;
        routedSamples_ = numSamples;
    }
    // Sends taken before the fader read the sound as it was then, while
    // anything is sent that way.
    void setKeepPreFader(bool keep) noexcept { keepPreFader_.store(keep, std::memory_order_relaxed); }
    // A track muted (or out of the solo) sends nothing, before the fader either.
    void setSendsAudible(bool audible) noexcept { sendsAudible_.store(audible, std::memory_order_relaxed); }
    // Its own sound held back by the latency of what is routed into it, so
    // the two meet in time (plugin latency compensation through buses).
    void setInputDelaySamples(int delaySamples) noexcept { inputDelay_.setDelaySamples(delaySamples); }
    void resetInputDelay() noexcept { inputDelay_.requestReset(); }
    // The last block's sound before the fader, on the audio thread right
    // after rendering it; null when not kept.
    [[nodiscard]] const juce::AudioBuffer<float>* preFader() const noexcept { return preFaderValid_ ? &preFader_ : nullptr; }

private:
    std::array<const juce::AudioBuffer<float>*, maximumBuses> routedInputs_ {};
    int routedSamples_ {};
    std::atomic<bool> keepPreFader_ {};
    std::atomic<bool> sendsAudible_ { true };
    juce::AudioBuffer<float> preFader_;
    bool preFaderValid_ {};
    AudioDelayLine inputDelay_;
    [[nodiscard]] bool processNativeDspGraph(
        const juce::AudioSourceChannelInfo& bufferToFill) noexcept;
    void publishAnalysisFrame(
        const juce::AudioSourceChannelInfo& bufferToFill) noexcept;

    std::atomic<juce::AudioSource*> externalInput_ {};
    juce::AudioSource& input_;
    juce::AudioTransportSource* transport_ {};
    const std::atomic<double>* timelinePosition_ {};
    const std::atomic<double>* modeTimeline_ {};
    std::atomic<float> gain_ { 1.0f };
    std::atomic<float> pan_ {};
    std::atomic<float> soundInLevel_ {};
    // Automation lanes are rebuilt on the message thread and borrowed by the
    // callback: a lane edit during playback must never free the old vector
    // inside the block that is still reading it.
    cydaw::realtime::RtPublisher<std::vector<GainAutomationPoint>> automation_;
    cydaw::realtime::RtPublisher<std::vector<GainAutomationPoint>> panAutomation_;
    std::atomic<float> automatedPan_ {};
    std::atomic<bool> panAutomated_ {};
    cydaw::realtime::RtPublisher<std::vector<PluginParameterAutomationLane>>
        pluginAutomation_;
    std::atomic<float> driveDb_ {};
    std::atomic<float> filterCutoffHz_ { 20'000.0f };
    std::array<float, 64> filterState_ {};
    // The chain runs the runtime a bus or an instrument's chain runs. The
    // engine compiler this used directly skips every control node and knows
    // nothing of modulations: an LFO dropped on a knob of an audio track was
    // compiled nowhere, and the track had nothing to report for the ring.
    NativeDspRuntime nativeGraph_;
    // Until a chain is set the legacy drive and filter shape the track.
    std::atomic<bool> nativeGraphSet_ {};
    cydaw::engine::NativeDspGraph nativeGraphDescription_;
    int maximumBlockSize_ { 512 };
    int graphChannelCount_ { 2 };
    std::array<std::unique_ptr<IsolatedPluginHost>, maximumPluginCount> pluginHosts_;
    // The device format each slot's worker was last started again for.
    std::array<std::pair<double, int>, maximumPluginCount> followTried_ {};
    // The loudest each plug-in gave out since asked (two channels), for the
    // device panel's meters.
    std::array<std::atomic<float>, maximumPluginCount * 2> pluginPeaks_ {};
    std::atomic<std::size_t> pluginCount_ {};
    std::atomic<bool> pluginsAvailable_ { true };
    std::atomic<int> pluginCallbacksInFlight_ {};
    cydaw::realtime::SpscRing<cydaw::engine::AudioAnalysisFrame, 8> analysisFrames_;
    cydaw::engine::AnalysisWaveformRing analysisRing_;
    std::uint64_t analysisSequence_ {};
    double sampleRate_ { 48'000.0 };
};

// The tracks mixed into the master, each through its outputs: the master,
// other tracks (a bus is a track that others are routed into) or an output
// of the sound card directly, each at its level. A track renders after
// every track routed into it; one routed nowhere goes to the master. Each
// track renders in its own format (mono to 7.1) and converts into what it
// goes to (the master is stereo). Inputs are added and removed as with
// juce::MixerAudioSource; the routing is published by the controller.
class TrackMixer final : public juce::AudioSource, public DirectOutputSource {
public:
    static constexpr std::size_t buses = RoutedInputReceiver::maximumBuses;
    static constexpr int maximumChannels = LatencyCompensatingAudioSource::maximumChannelCount;
    static constexpr std::size_t maximumDirectOutputs = VirtualBusRouter::maximumBusCount;
    struct Output {
        juce::AudioSource* destination {}; // null: the master (or a hardware output)
        float gain { 1.0f };
        std::uint32_t bus {};              // which of the destination's inputs
        bool preFader {};
        // Held back to meet the latest of what reaches the destination; null
        // when it needs nothing beyond its track's own delay.
        std::shared_ptr<AudioDelayLine> delay;
        int channels { 2 };                // the destination's format
        int hardware { -1 };               // a slot of the plan's hardware outputs
    };
    struct Entry {
        juce::AudioSource* source {};
        RoutedInputReceiver* receiver {};
        GainPanAudioSource* fader {};      // where its sound before the fader is kept
        std::vector<Output> outputs;       // empty: the master
        int channels { 2 };                // its own format
    };
    // An output bus tracks go to directly: its channels, and whether they
    // join before its effects or after them.
    struct HardwareSlot {
        std::uint64_t busId {};
        int channels { 2 };
        bool beforeEffects {};
    };
    struct Plan {
        std::vector<Entry> entries; // senders before what they are routed into
        std::vector<HardwareSlot> hardware;
    };

    TrackMixer();
    void addInputSource(juce::AudioSource* source, bool deleteWhenRemoved);
    void removeInputSource(juce::AudioSource* source);
    void setPlan(std::shared_ptr<const Plan> plan);

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    [[nodiscard]] const juce::AudioBuffer<float>* directOutput(std::uint64_t busId, int numSamples,
                                                               bool beforeEffects) noexcept override;
    void finishDirectOutputs() noexcept override { directSamples_ = 0; }

private:
    [[nodiscard]] int indexOf(const juce::AudioSource* source) const noexcept;

    juce::CriticalSection lock_;
    std::vector<juce::AudioSource*> inputs_;
    // Each track's sound and what reaches each of its inputs, in buffers of
    // the widest format; the block reads them through views of its own.
    std::vector<juce::AudioBuffer<float>> rendered_;
    std::vector<juce::AudioBuffer<float>> routed_;
    std::vector<juce::AudioBuffer<float>> routedViews_;
    std::vector<char> routedUsed_;
    std::vector<char> done_;
    juce::AudioBuffer<float> delayed_;
    // What the tracks routed to the sound card's outputs left this block.
    std::array<juce::AudioBuffer<float>, maximumDirectOutputs> direct_;
    std::array<juce::AudioBuffer<float>, maximumDirectOutputs> directViews_;
    std::array<std::uint64_t, maximumDirectOutputs> directBus_ {};
    std::array<char, maximumDirectOutputs> directBefore_ {};
    std::array<char, maximumDirectOutputs> directUsed_ {};
    int directSamples_ {};
    cydaw::realtime::RtPublisher<Plan> plan_;
    double sampleRate_ { 48'000.0 };
    int blockSize_ { 512 };
    bool prepared_ {};
};

class InputRecorder final : public juce::AudioIODeviceCallback {
public:
    InputRecorder();
    ~InputRecorder() override;

    [[nodiscard]] juce::Result start(const juce::File& destination,
                                     double startDelaySeconds = 0.0);
    void scheduleStop(double delaySeconds);
    void stop();
    [[nodiscard]] bool isRecording() const noexcept;
    [[nodiscard]] bool hasSession() const noexcept;
    [[nodiscard]] bool hasStarted() const noexcept;
    [[nodiscard]] bool stopPending() const noexcept;
    [[nodiscard]] bool recordingComplete() const noexcept;
    [[nodiscard]] int droppedBlockCount() const noexcept;
    void setInputBus(
        std::optional<VirtualAudioBus> bus,
        cydaw::engine::NativeDspGraph graph = makePassthroughAudioGraph());
    // Where the input buses are taken as they are heard (the router runs
    // them in the callback before this one), and which side of their effects.
    void setRouter(const VirtualBusRouter* router) noexcept { router_ = router; }
    void setAfterEffects(bool afterEffects) noexcept { afterEffects_.store(afterEffects, std::memory_order_release); }
    [[nodiscard]] static juce::Result selfTestScheduledRecording(
        const juce::File& destination);

    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    juce::TimeSliceThread writerThread_ { "CyDAW recording writer" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter_;
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> activeWriter_ {};
    std::atomic<int> callbacksInFlight_ {};
    std::atomic<double> sampleRate_ {};
    std::atomic<int> inputChannelCount_ {};
    std::atomic<int> recordingChannelCount_ {};
    std::atomic<int> droppedBlocks_ {};
    std::atomic<juce::int64> samplesUntilStart_ {};
    std::atomic<juce::int64> samplesUntilStop_ { -1 };
    std::atomic<bool> started_ {};
    std::atomic<bool> stopPending_ {};
    std::atomic<bool> recordingComplete_ {};
    cydaw::realtime::RtPublisher<VirtualAudioBus> inputBus_;
    const VirtualBusRouter* router_ {};
    std::atomic<bool> afterEffects_ { true };
    juce::AudioBuffer<float> routedInputBuffer_;
    NativeDspRuntime inputBusGraph_;
};

// A loop auditioned at the project's tempo (the sound library): stretched
// beforehand, played over and over from the device's callback, in step with
// the transport while it plays. The mix is wrapped in a clock (ClockStart
// before it, this source after it) so the message thread can read the
// transport's position together with the block it belongs to.
class LoopAuditionSource final : public juce::AudioSource {
public:
    struct Loop {
        std::uint64_t generation {};
        // At the device's rate, `frames` long: `beats` beats at `tempo`
        // (shared by the loops that only move it in step again).
        std::shared_ptr<const std::vector<std::vector<float>>> channels;
        std::int64_t frames {};
        int beats { 1 };
        double tempo { 120.0 };
        double rate { 48'000.0 };
        // In step with the transport: its position when the clock stood at
        // `clockFrames`, and how late the mix is heard.
        bool sync {};
        std::int64_t clockFrames {};
        double positionSeconds {};
        double latencySeconds {};
        // Not in step: the frame of the loop it starts from.
        std::int64_t startFrame {};
    };
    // Before the mix: a block begins.
    class ClockStart final : public juce::AudioSource {
    public:
        explicit ClockStart(LoopAuditionSource& owner) noexcept : owner_(owner) {}
        void prepareToPlay(int, double) override {}
        void releaseResources() override {}
        void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override
        {
            owner_.sequence_.fetch_add(1, std::memory_order_acq_rel);
            info.clearActiveBufferRegion();
        }

    private:
        LoopAuditionSource& owner_;
    };

    explicit LoopAuditionSource(const std::atomic<float>& gainDb) noexcept : gainDb_(gainDb) {}
    [[nodiscard]] ClockStart& clockStart() noexcept { return clockStart_; }
    void play(std::shared_ptr<const Loop> loop) { loop_.store(std::move(loop)); }
    void stop() { loop_.store(nullptr); }
    [[nodiscard]] bool playing() const noexcept { return playing_.load(std::memory_order_acquire); }
    // Where in the loop it is, 0..1.
    [[nodiscard]] double fraction() const noexcept { return fraction_.load(std::memory_order_relaxed); }
    [[nodiscard]] double sampleRate() const noexcept { return rate_.load(std::memory_order_acquire); }
    // The clock: frames the device has played, and its sequence (odd while
    // a block is being made). False when it did not hold still.
    [[nodiscard]] bool clock(std::int64_t& frames, const std::function<void()>& readWithIt) const;

    void prepareToPlay(int, double sampleRate) override { rate_.store(std::max(1.0, sampleRate), std::memory_order_release); }
    void releaseResources() override {}
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override;

private:
    const std::atomic<float>& gainDb_;
    ClockStart clockStart_ { *this };
    cydaw::realtime::RtPublisher<Loop> loop_;
    std::atomic<std::uint64_t> sequence_ {};
    std::atomic<std::int64_t> frames_ {};
    std::atomic<double> rate_ { 48'000.0 };
    std::atomic<bool> playing_ {};
    std::atomic<double> fraction_ {};
    // The audio thread's own.
    std::uint64_t generation_ {};
    std::int64_t phase_ {};
};

class MasterOutputAudioSource final : public juce::AudioSource {
public:
    explicit MasterOutputAudioSource(juce::AudioSource& input);
    ~MasterOutputAudioSource() override;

    // What the mix is late by (its tracks' compensation, the master's own
    // devices) is skipped, so the file starts with the song's first sample.
    [[nodiscard]] juce::Result startCapture(const juce::File& destination,
                                             double durationSeconds, int skipSamples = 0);
    void stopCapture();
    [[nodiscard]] bool isCapturing() const noexcept;
    [[nodiscard]] bool captureComplete() const noexcept;
    [[nodiscard]] double captureProgress() const noexcept;
    [[nodiscard]] int droppedBlockCount() const noexcept;
    [[nodiscard]] bool tryPopAnalysisFrame(
        cydaw::engine::AudioAnalysisFrame& frame) noexcept;
    void setGain(float linearGain) noexcept;
    // The master track's devices, before its fader: what its graph does to
    // the tracks mixed. Passes the sound as it is while it has none.
    juce::Result setChainGraph(const cydaw::engine::NativeDspGraph& graph);
    juce::Result updateChainParameters(const cydaw::engine::NativeDspGraph& graph);
    [[nodiscard]] NativeDspRuntime& chain() noexcept { return chain_; }
    [[nodiscard]] const NativeDspRuntime& chain() const noexcept { return chain_; }
    // The master's own clock (seconds, as the timeline counts them), which
    // runs while the transport plays: what its launcher clip's automation
    // follows.
    void setClock(double seconds) noexcept { clock_.store(seconds, std::memory_order_relaxed); }
    void setClockRunning(bool running) noexcept { clockRunning_.store(running, std::memory_order_release); }
    [[nodiscard]] double clock() const noexcept { return clock_.load(std::memory_order_relaxed); }
    // How late the master's devices make the mix (look-aheads).
    [[nodiscard]] int chainLatencySamples() const noexcept
    {
        return chainActive_.load(std::memory_order_acquire) ? chain_.latencySamples() : 0;
    }

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;

private:
    void publishAnalysisFrame(
        const juce::AudioSourceChannelInfo& bufferToFill) noexcept;

    juce::AudioSource& input_;
    juce::TimeSliceThread writerThread_ { "CyDAW master export writer" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter_;
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> activeWriter_ {};
    std::atomic<int> callbacksInFlight_ {};
    std::atomic<double> sampleRate_ {};
    std::atomic<double> clock_ {};
    std::atomic<bool> clockRunning_ {};
    std::atomic<juce::int64> targetSamples_ {};
    std::atomic<juce::int64> capturedSamples_ {};
    std::atomic<juce::int64> skipSamples_ {};
    std::atomic<bool> captureComplete_ {};
    std::atomic<int> droppedBlocks_ {};
    std::unique_ptr<cydaw::realtime::SpscRing<
        cydaw::engine::AudioAnalysisFrame, 8>> analysisFrames_;
    std::uint64_t analysisSequence_ {};
    std::atomic<float> gain_ { 1.0f };
    NativeDspRuntime chain_;
    std::atomic<bool> chainActive_ {};
    std::atomic<int> blockSize_ { 512 };
};

class AudioTrackController final {
public:
    // The launcher's rows: as many as a large project brings (Bitwig's
    // can have hundreds).
    static constexpr int maximumLauncherScenes = 256;
    static constexpr std::size_t maximumAudioClipsPerTrack = 64;
    static constexpr std::size_t maximumAudioTakesPerClip = 32;

    enum class LaunchQuantization : int {
        none = 0,
        quarterBeat,
        halfBeat,
        oneBeat,
        oneBar
    };

    struct MidiSynthEngineCurves;
    // A point of a note's expression: where along the note (0 its start, 1
    // its end) and the value there (semitones for the pitch, dB for the gain);
    // how the way to the next point bends (0 straight; toward 1 quick then
    // settling, as a glide arrives; toward -1 slow then quick).
    struct NoteExpressionPoint {
        float position {};
        float value {};
        float bend {};

        [[nodiscard]] bool operator==(const NoteExpressionPoint&) const = default;

        // A part of a segment (0..1) once bent.
        [[nodiscard]] static float bent(const float part, const float bend) noexcept
        {
            const auto u = std::clamp(part, 0.0f, 1.0f);
            if (bend > 1.0e-4f)
                return 1.0f - std::pow(1.0f - u, 1.0f + 3.0f * std::min(bend, 1.0f));
            if (bend < -1.0e-4f)
                return std::pow(u, 1.0f + 3.0f * std::min(-bend, 1.0f));
            return u;
        }
        // A curve's value at a place along its note (0..1): the first
        // point's before it, the last one's after it.
        [[nodiscard]] static float valueAt(const std::vector<NoteExpressionPoint>& points, const float position) noexcept
        {
            if (points.empty())
                return 0.0f;
            if (position <= points.front().position)
                return points.front().value;
            for (std::size_t index = 1; index < points.size(); ++index)
                if (position <= points[index].position) {
                    const auto& a = points[index - 1];
                    const auto& b = points[index];
                    const auto part = (position - a.position) / std::max(1.0e-6f, b.position - a.position);
                    return a.value + (b.value - a.value) * bent(part, a.bend);
                }
            return points.back().value;
        }
    };

    struct MidiNote {
        std::uint64_t id {};
        int noteNumber { 60 };
        double startBeats {};
        double durationBeats { 1.0 };
        float velocity { 0.8f };
        // Drawn on the note itself, as Bitwig's note expressions: how its
        // pitch moves along it (a scoop, a vibrato, a fall) and its gain;
        // its timbre (0..1, MPE's CC 74), its pressure (0..1, its channel's
        // pressure) and its pan (-1..1). Empty: the note as it is.
        std::vector<NoteExpressionPoint> pitch;
        std::vector<NoteExpressionPoint> gain;
        std::vector<NoteExpressionPoint> timbre;
        std::vector<NoteExpressionPoint> pressure;
        std::vector<NoteExpressionPoint> pan;
        // How likely it plays each time it comes (0..1).
        float chance { 1.0f };
    };
    // A note's curves, by what they move.
    enum class NoteCurve { pitch, gain, timbre, pressure, pan };
    // A curve's range: semitones, dB, parts, the pan.
    [[nodiscard]] static std::pair<float, float> noteCurveRange(NoteCurve curve) noexcept
    {
        switch (curve) {
        case NoteCurve::pitch: return { -24.0f, 24.0f };
        case NoteCurve::gain: return { -48.0f, 12.0f };
        case NoteCurve::pan: return { -1.0f, 1.0f };
        case NoteCurve::timbre:
        case NoteCurve::pressure: return { 0.0f, 1.0f };
        }
        return { 0.0f, 1.0f };
    }
    // A note's value of each when it has no curve of it.
    [[nodiscard]] static float noteCurveRest(NoteCurve curve) noexcept
    {
        return curve == NoteCurve::timbre ? 0.5f : 0.0f;
    }
    [[nodiscard]] static std::vector<NoteExpressionPoint>& noteCurveOf(MidiNote& note, NoteCurve curve) noexcept
    {
        switch (curve) {
        case NoteCurve::pitch: return note.pitch;
        case NoteCurve::gain: return note.gain;
        case NoteCurve::timbre: return note.timbre;
        case NoteCurve::pressure: return note.pressure;
        case NoteCurve::pan: return note.pan;
        }
        return note.pitch;
    }
    [[nodiscard]] static const std::vector<NoteExpressionPoint>& noteCurveOf(const MidiNote& note, NoteCurve curve) noexcept
    {
        return noteCurveOf(const_cast<MidiNote&>(note), curve);
    }

    // A point's shape, as engine::CurveShape: straight, bent (bezier, by
    // curveAmount), a B-spline's, a Bezier point with one tangent or two.
    enum class AutomationCurve : int {
        linear = 0,
        bezier = 1,
        spline = 2,
        tangent = 3,
        tangents = 4
    };

    struct AutomationPoint {
        std::uint64_t id {};
        double timeSeconds {};
        float valueDb {};
        AutomationCurve curve { AutomationCurve::linear };
        float curveAmount {};
        // A Bezier point's handles: seconds before and after it, values in
        // the lane's units.
        cydaw::engine::CurveHandles handles {};
    };

    struct MidiClip {
        std::uint64_t id {};
        juce::String name;
        double startBeats {};
        double lengthBeats { 16.0 };
        bool looping {};
        std::uint32_t colour { 0xff4fd3ae };
        std::vector<MidiNote> notes;
        // Bounds are local to the clip, exactly like an audio clip.  A zero
        // end value is kept as the backward-compatible "use full length"
        // sentinel when loading older projects.
        double playStartBeats {};
        double playEndBeats {};
        double loopStartBeats {};
        double loopEndBeats {};
        // Clip-local automation. Time is relative to the beginning of this
        // MIDI clip, never to the global arrangement.
        std::vector<AutomationPoint> automation;
        // The launcher's slot that holds it (its scene), or -1: a clip of
        // the arrangement. A launcher clip is played only when launched; the
        // arrangement neither plays nor shows it.
        int launcherScene { -1 };
    };

    struct MidiTrackInfo {
        std::uint64_t id {};
        juce::String name;
        float gainDb {};
        float pan {};
        bool muted {};
        bool solo {};
        bool armed {};
        std::vector<MidiClip> clips;
    };

    struct MidiFileImportSummary {
        std::vector<std::uint64_t> trackIds;
        std::size_t noteCount {};
        double tempoBpm { 120.0 };
    };

    enum class MidiRecordQuantization : int {
        none = 0,
        quarterBeat,
        eighthBeat,
        sixteenthBeat,
        thirtySecondBeat
    };

    struct PluginAutomationPoint {
        std::uint64_t id {};
        double timeSeconds {};
        float normalisedValue {};
        AutomationCurve curve { AutomationCurve::linear };
        float curveAmount {};
        cydaw::engine::CurveHandles handles {};
    };

    struct PluginAutomationLane {
        std::size_t pluginIndex {};
        std::uint32_t parameterIndex {};
        juce::String parameterName;
        std::vector<PluginAutomationPoint> points;
    };

    struct AudioTake {
        juce::String name;
        juce::File sourceFile;
        double sourceLengthSeconds {};
        // Where it lies against the clip's own source: its file is read this
        // much later (a take recorded from further in the clip, a pass of a
        // loop in one long recording).
        double shiftSeconds {};
    };
    // A passage of a clip played from one of its takes (comping), in
    // clip-local timeline seconds.
    struct CompSegment {
        double startSeconds {};
        double endSeconds {};
        std::size_t take {};
        [[nodiscard]] bool operator==(const CompSegment&) const = default;
    };
    // A clip's source as it was before a destructive edit, and after:
    // the versions the editor's history lists, the first the original.
    struct SourceVersion {
        juce::String label;
        juce::File file;
        double sourceOffsetSeconds {};
        double durationSeconds {};
        double sourceDurationSeconds {};
        double sourceLengthSeconds {};
    };

    // How a sound is played when its speed is not the file's (Ableton's
    // warp modes, Bitwig's stretch modes): repitch (the pitch follows the
    // speed), tonal, beats, texture, raw (never stretched: played at the
    // file's speed, whatever its length says), complex (whole mixes).
    enum class StretchMode {
        repitch = 0,
        tonal = 1,
        beats = 2,
        texture = 3,
        raw = 4,
        complex = 5
    };
    static constexpr int stretchModeCount = 6;

    struct TempoDetectionResult {
        double bpm {};
        double confidence {};
    };

    struct StretchMarker {
        std::uint64_t id {};
        // Absolute position in the source file.
        double sourceSeconds {};
        // Position relative to the beginning of the clip on the timeline.
        double timelineSeconds {};
    };

    // An arrangement AudioClip is a reusable container. Its content timeline
    // is relative to the beginning of that clip and may contain several
    // independent audio events.
    struct AudioEvent {
        std::uint64_t id {};
        juce::String name;
        double startSeconds {};
        double sourceOffsetSeconds {};
        double durationSeconds { 1.0 };
        double sourceDurationSeconds { 1.0 };
        std::uint32_t colour { 0xffff9b2f };
        juce::File sourceFile;
        double sourceLengthSeconds {};
        double fadeInSeconds {};
        double fadeOutSeconds {};
        double sourceTempoBpm {};
        StretchMode stretchMode { StretchMode::repitch };
        std::vector<StretchMarker> stretchMarkers;
        float gainDb {};
        // Transposed without changing its length (not with repitch).
        double pitchSemitones {};
        // Its own lanes, in its own seconds (from its start, so they move
        // with it): soundVolumeLane (dB over its gain), soundPanLane
        // (-1..1), soundPitchLane (semitones over its transposition; not
        // with repitch or raw, whose pitch is their speed's).
        std::map<juce::String, std::vector<AutomationPoint>> lanes;
    };
    static constexpr const char* soundVolumeLane = "volume";
    static constexpr const char* soundPanLane = "pan";
    static constexpr const char* soundPitchLane = "pitch";

    struct AudioClip {
        std::uint64_t id {};
        juce::String name;
        double startSeconds {};
        double sourceOffsetSeconds {};
        double durationSeconds { 1.0 };
        // Amount of source material played over durationSeconds. A value
        // different from durationSeconds represents a non-destructive
        // timeline stretch.
        double sourceDurationSeconds { 1.0 };
        std::uint32_t colour { 0xffff9b2f };
        juce::File sourceFile;
        double sourceLengthSeconds {};
        double fadeInSeconds {};
        double fadeOutSeconds {};
        std::vector<AudioTake> takes;
        std::size_t activeTakeIndex {};
        double sourceTempoBpm {};
        StretchMode stretchMode { StretchMode::repitch };
        std::vector<StretchMarker> stretchMarkers;
        std::vector<AudioEvent> events;
        // Local single-lane timeline bounds. They are relative to this clip,
        // while startSeconds remains the position in the arrangement.
        double playStartSeconds {};
        double playEndSeconds {};
        double loopStartSeconds {};
        double loopEndSeconds {};
        bool looping {};
        // True also for an intentionally empty clip. False denotes the
        // legacy implicit single event stored in the clip source fields.
        bool usesEventTimeline {};
        // Internal render gain.  Container clips normally remain at 0 dB;
        // materialised AudioEvents copy their per-sample gain here.
        float eventGainDb {};
        // Internal too: a rendered sound's own lanes (AudioEvent::lanes),
        // from this region's start.
        std::map<juce::String, std::vector<AutomationPoint>> soundLanes;
        // Clip-local gain automation, expressed on the container timeline.
        std::vector<AutomationPoint> automation;
        // Transposed without changing its length (not with repitch).
        double pitchSemitones {};
        // The takes' best passages, in order (empty: the active take
        // throughout). Passages meet with a short crossfade.
        std::vector<CompSegment> comp;
        // Destructive edits: every version of the source, and the one played.
        std::vector<SourceVersion> versions;
        std::size_t version {};
        // Its automation of the track's values, played with it: by lane
        // ("volume" in dB over the fader, "pan" -1..1, a device's knob by its
        // binding in its own units), points in the clip's content seconds.
        std::map<juce::String, std::vector<AutomationPoint>> lanes;
    };

    struct LauncherClip {
        int sceneIndex {};
        juce::String name;
        juce::File sourceFile;
        double sourceOffsetSeconds {};
        // Duration of the local clip container, not of the source file.
        double durationSeconds { 1.0 };
        bool looping { true };
        std::uint32_t colour { 0xff4fd3ae };
        std::vector<AudioEvent> events;
        double playStartSeconds {};
        double playEndSeconds {};
        double loopStartSeconds {};
        double loopEndSeconds {};
        bool usesEventTimeline {};
        // Clip-local gain automation, expressed on the launcher clip timeline.
        std::vector<AutomationPoint> automation;
        // Its automation of the track's values (as AudioClip::lanes).
        std::map<juce::String, std::vector<AutomationPoint>> lanes;
    };

    enum class ArrangementRangeOperation {
        splitAtBoundaries,
        cropToRange,
        silenceRange
    };

    struct ArrangementRangeEditResult {
        bool succeeded {};
        bool changed {};
        std::vector<std::uint64_t> affectedClipIds;
    };

    struct ArrangementSessionMoveResult {
        bool succeeded {};
        bool changed {};
        std::vector<std::vector<std::uint64_t>> clipIdsByTrack;
    };

    struct TrackInfo {
        struct PluginInfo {
            juce::File file;
            juce::String name;
            bool running {};
            std::uint64_t missedBlocks {};
            int latencySamples {};
        };

        juce::File file;
        double lengthSeconds {};
        float gainDb {};
        float pan {};
        bool muted {};
        bool solo {};
        bool armed {};
        std::uint64_t inputBusId {};
        double clipStartSeconds {};
        double sourceOffsetSeconds {};
        double clipDurationSeconds {};
        double sourceLengthSeconds {};
        float driveDb {};
        float filterCutoffHz { 20'000.0f };
        std::vector<PluginInfo> plugins;
        int latencyCompensationSamples {};
        std::vector<AudioClip> clips;
    };

    struct RecordedTakeResult {
        std::size_t trackIndex {};
        std::uint64_t clipId {};
        double startSeconds {};
        double durationSeconds {};
        bool createdTrack {};
    };

    AudioTrackController();
    ~AudioTrackController();

    [[nodiscard]] juce::String initialiseAudio(
        const juce::XmlElement* savedState = nullptr);
    [[nodiscard]] juce::Result addAudioTrack(
        const juce::File& file,
        std::size_t* newTrackIndex = nullptr,
        double initialClipStartSeconds = 0.0);
    // A secondary mixing session, driven by the Foundry/DJ audio callback.
    // No sound card is opened. External sources outlive the track.
    void prepareModeMix(double rate, int block) { modeRate_ = rate; masterOutput_.prepareToPlay(block, rate); }
    void renderModeMix(const juce::AudioSourceChannelInfo& output) { masterOutput_.getNextAudioBlock(output); }
    void setModeTrackSource(std::size_t index, juce::AudioSource* source);
    void setModeGainAutomation(bool midi, std::uint64_t id, std::vector<GainAutomationPoint> points);
    void setModeTimeline(double seconds, int samples, bool midiPlaying = true) noexcept {
        modeMidiPlaying_.store(midiPlaying,std::memory_order_relaxed);
        modePosition_.store(seconds, std::memory_order_relaxed);
        modeEndPosition_.store(seconds + samples / std::max(1.0, modeRate_), std::memory_order_relaxed);
        modeTimelineEnabled_.store(true, std::memory_order_release);
        masterOutput_.setClock(seconds);
        masterOutput_.setClockRunning(true);
    }
    [[nodiscard]] juce::Result addEmptyAudioTrack(
        std::size_t* newTrackIndex = nullptr);
    // A sample auditioned at the project's tempo: its own tempo (0: as it
    // is) and how it is stretched.
    struct SamplePreviewTempo {
        double sourceBpm {};
        int algorithm { 1 }; // StretchMode: repitch, tonal, beats, texture
    };
    // At its tempo (a loop of whole beats, 32 s at most): stretched to the
    // project's, played over and over, in step with the transport when it
    // plays; another sound, or no tempo, as it is.
    [[nodiscard]] juce::Result startSamplePreview(
        const juce::File& file, double startSeconds = 0.0, const SamplePreviewTempo& tempo = {});
    // The loop being auditioned at the project's tempo: its beats, else 0.
    [[nodiscard]] int samplePreviewLoopBeats() const noexcept;
    // The device's rate, as the audition plays at it.
    [[nodiscard]] double auditionSampleRate() const noexcept { return loopAudition_.sampleRate(); }
    // Now and then (the message thread): a loop auditioned at the tempo
    // comes back in step with the transport when it jumped (its loop, a
    // move of the playhead) or started.
    void followTempoPreview();
    void stopSamplePreview();
    [[nodiscard]] bool isSamplePreviewing() const noexcept;
    [[nodiscard]] double samplePreviewPositionSeconds() const noexcept;
    void setSamplePreviewGainDb(float gainDb) noexcept;
    [[nodiscard]] float samplePreviewGainDb() const noexcept;
    [[nodiscard]] juce::Result startAudioClipPreview(
        std::size_t trackIndex, std::uint64_t clipId,
        double localStartSeconds = 0.0);
    [[nodiscard]] juce::Result refreshAudioClipPreview(
        std::size_t trackIndex, std::uint64_t clipId);
    void stopAudioClipPreview();
    [[nodiscard]] bool isAudioClipPreviewing() const noexcept;
    [[nodiscard]] double audioClipPreviewPositionSeconds() const noexcept;
    void clearTracks();
    // Removes one audio track and everything the mixer held for it; the
    // tracks after it move up one index.
    [[nodiscard]] bool removeAudioTrack(std::size_t index);
    [[nodiscard]] bool moveAudioTrack(std::size_t fromIndex,
                                      std::size_t toIndex);
    [[nodiscard]] std::size_t trackCount() const noexcept;
    [[nodiscard]] TrackInfo trackInfo(std::size_t index) const;
    // The plugin list alone. The device chain asks five times a second, and
    // trackInfo copies every clip, event, automation point and take with it.
    // Light reads for an interface that draws every frame: nothing is copied
    // but the few fields asked for. The views point at the engine's own lists
    // and stay valid until the next edit on the message thread.
    struct TrackMixState {
        float gainDb {};
        float pan {};
        bool muted {};
        bool solo {};
        bool armed {};
        std::uint64_t inputBusId {};
        // The input bus taken after its effects (else before them).
        bool inputAfterEffects { true };
    };
    [[nodiscard]] TrackMixState trackMixState(std::size_t index) const;
    [[nodiscard]] const std::vector<AudioClip>* audioClipsView(std::size_t index) const;
    [[nodiscard]] juce::File trackFile(std::size_t index) const;
    struct MidiTrackSummary {
        std::uint64_t id {};
        juce::String name;
        float gainDb {};
        float pan {};
        bool muted {};
        bool solo {};
        bool armed {};
    };
    [[nodiscard]] std::vector<MidiTrackSummary> midiTrackSummaries() const;
    [[nodiscard]] const std::vector<MidiClip>* midiClipsView(std::uint64_t trackId) const;
    [[nodiscard]] std::vector<TrackInfo::PluginInfo> trackPlugins(
        std::size_t index) const;
    [[nodiscard]] std::vector<VirtualAudioBus> virtualBuses() const;
    [[nodiscard]] std::optional<VirtualAudioBus> virtualBus(
        std::uint64_t busId) const;
    [[nodiscard]] std::uint64_t addVirtualBus(
        VirtualBusDirection direction);
    [[nodiscard]] bool updateVirtualBus(const VirtualAudioBus& bus);
    [[nodiscard]] bool removeVirtualBus(std::uint64_t busId);
    void replaceVirtualBuses(std::vector<VirtualAudioBus> buses);
    void resetVirtualBuses();
    [[nodiscard]] std::uint64_t defaultInputBusId() const noexcept;
    [[nodiscard]] std::array<float, 2> virtualBusMeterLevels(
        std::uint64_t busId) const noexcept;
    [[nodiscard]] juce::Result setVirtualBusGraph(
        std::uint64_t busId,
        const cydaw::engine::NativeDspGraph& graph);
    [[nodiscard]] juce::Result updateVirtualBusGraphParameters(
        std::uint64_t busId,
        const cydaw::engine::NativeDspGraph& graph);
    [[nodiscard]] cydaw::engine::NativeDspGraph virtualBusGraph(
        std::uint64_t busId) const;
    [[nodiscard]] std::vector<std::pair<std::uint64_t, float>>
        virtualBusControlValues(std::uint64_t busId) const;
    [[nodiscard]] juce::StringArray physicalInputChannelNames() const;
    [[nodiscard]] juce::StringArray physicalOutputChannelNames() const;
    // The sound card's channels in use: the first `inputs` and `outputs` of
    // its own (a bus's channels count among them). What the device has, and
    // what is open now.
    struct DeviceChannels {
        // The channels open, in order: a bus's channel k is the k-th here.
        juce::StringArray inputNames;
        juce::StringArray outputNames;
        int activeInputs {};
        int activeOutputs {};
        // What the device has.
        int availableInputs {};
        int availableOutputs {};
    };
    [[nodiscard]] DeviceChannels deviceChannels() const;
    juce::Result setDeviceChannelCounts(int inputs, int outputs);

    // ---- Track formats ------------------------------------------------------
    // An audio track's channels: 1 mono, 2 stereo, up to 8 (7.1), laid out
    // as cydaw/engine/ChannelLayout.h says. Its clips play in it (a file of
    // another format converts), its devices process every channel, and its
    // sound converts into what it is routed to.
    [[nodiscard]] int trackChannelCount(std::size_t index) const noexcept;
    juce::Result setTrackChannelCount(std::size_t index, int channels);
    [[nodiscard]] bool setTrackInputBus(std::size_t trackIndex,
                                        std::uint64_t busId);
    // What it records of its input bus: after the bus's effects (the sound
    // treated, the default) or before them (as the card gives it).
    void setTrackInputAfterEffects(std::size_t trackIndex, bool afterEffects);
    // The displays of a virtual bus's effects, as a track's.
    void setVirtualBusTaps(std::uint64_t busId, const std::vector<NativeDspRuntime::TapRequest>& taps);
    [[nodiscard]] bool readVirtualBusTap(std::uint64_t busId, std::uint64_t nodeId, bool input,
                                         cydaw::engine::AudioAnalysisFrame& frame) const;
    void setTrackMixer(std::size_t index, float gainDb, float pan, bool muted);
    void setMasterGain(float gainDb) noexcept;
    // The master silenced without losing its level.
    void setMasterMuted(bool muted) noexcept;
    // The master track's devices (a graph from the tracks mixed to the
    // fader), its control values and its displays' taps, as a track's.
    juce::Result setMasterNativeDspGraph(const cydaw::engine::NativeDspGraph& graph);
    juce::Result updateMasterNativeDspParameters(const cydaw::engine::NativeDspGraph& graph);
    [[nodiscard]] std::vector<std::pair<std::uint64_t, float>> masterControlValues() const;
    // The master's devices' knobs that follow its launcher clip, on the
    // master's clock (which runs while the transport plays).
    void setMasterNativeAutomation(std::vector<NativeDspRuntime::AutomationLane> lanes);
    [[nodiscard]] double masterClockSeconds() const noexcept { return masterOutput_.clock(); }
    // The loudest each of these nodes of a chain gave out since it was last
    // asked (two channels), for the device panel's meters: a track's, an
    // instrument track's (its instrument is the chain's input), the master's.
    void masterNodePeaks(const std::vector<std::uint64_t>& nodes, std::vector<float>& left, std::vector<float>& right) const;
    void trackNodePeaks(std::size_t trackIndex, const std::vector<std::uint64_t>& nodes, std::vector<float>& left,
                        std::vector<float>& right) const;
    // The same for a track's VST3 plug-ins, in their order.
    void trackPluginPeaks(std::size_t trackIndex, std::vector<float>& left, std::vector<float>& right);
    void midiTrackNodePeaks(std::uint64_t trackId, const std::vector<std::uint64_t>& nodes, std::vector<float>& left,
                            std::vector<float>& right) const;
    // What a node of a chain shows of its work now (an equaliser's dynamic
    // bands), the master's, an audio track's, an instrument track's; empty
    // when it shows nothing.
    void masterNodeReadings(std::uint64_t node, std::vector<float>& into) const;
    void trackNodeReadings(std::size_t trackIndex, std::uint64_t node, std::vector<float>& into) const;
    void midiTrackNodeReadings(std::uint64_t trackId, std::uint64_t node, std::vector<float>& into) const;
    void setMasterNodeTaps(const std::vector<NativeDspRuntime::TapRequest>& taps);
    [[nodiscard]] bool readMasterNodeTap(std::uint64_t nodeId, bool input, cydaw::engine::AudioAnalysisFrame& frame) const;
    [[nodiscard]] bool isMasterMuted() const noexcept;
    // Nothing out of the speakers, whatever the master fader says: for the
    // probes and the captures, which need the engine running and not the
    // room listening to it.
    void silenceMasterOutput() noexcept;
    [[nodiscard]] float masterGainDb() const noexcept;
    void setTrackSolo(std::size_t index, bool solo);
    void setTrackArmed(std::size_t index, bool armed);
    [[nodiscard]] std::optional<std::size_t> armedAudioTrack() const noexcept;
    void setTrackClip(std::size_t index,
                      double clipStartSeconds,
                      double sourceOffsetSeconds,
                      double clipDurationSeconds);
    [[nodiscard]] std::vector<AudioClip> audioClips(std::size_t index) const;
    [[nodiscard]] std::optional<AudioClip> audioClip(
        std::size_t index, std::uint64_t clipId) const;
    [[nodiscard]] std::vector<AudioEvent> audioClipEvents(
        std::size_t index, std::uint64_t clipId) const;
    [[nodiscard]] std::uint64_t addAudioClip(
        std::size_t index,
        double startSeconds,
        double sourceOffsetSeconds,
        double durationSeconds,
        juce::String name = {},
        std::uint32_t colour = 0xffff9b2f,
        juce::File sourceFile = {},
        double fadeInSeconds = 0.0,
        double fadeOutSeconds = 0.0);
    [[nodiscard]] juce::Result addAudioClipFromFile(
        std::size_t index,
        const juce::File& sourceFile,
        double startSeconds,
        std::uint64_t* newClipId = nullptr);
    [[nodiscard]] juce::Result addAudioTake(
        std::size_t index,
        std::uint64_t clipId,
        const juce::File& sourceFile,
        bool activate = true,
        bool fitClipToTake = false);
    [[nodiscard]] juce::Result replaceAudioTakes(
        std::size_t index,
        std::uint64_t clipId,
        std::vector<AudioTake> takes,
        std::size_t activeTakeIndex);
    [[nodiscard]] bool setActiveAudioTake(
        std::size_t index,
        std::uint64_t clipId,
        std::size_t takeIndex);
    [[nodiscard]] bool removeAudioTake(
        std::size_t index,
        std::uint64_t clipId,
        std::size_t takeIndex);
    [[nodiscard]] bool updateAudioClip(
        std::size_t index,
        std::uint64_t clipId,
        double startSeconds,
        double sourceOffsetSeconds,
        double durationSeconds,
        double fadeInSeconds = -1.0,
        double fadeOutSeconds = -1.0,
        double sourceDurationSeconds = -1.0);
    [[nodiscard]] bool stretchAudioClip(
        std::size_t index, std::uint64_t clipId,
        double timelineDurationSeconds);
    [[nodiscard]] bool setAudioClipStretchSettings(
        std::size_t index, std::uint64_t clipId,
        double sourceTempoBpm, StretchMode mode);
    [[nodiscard]] std::uint64_t addAudioClipStretchMarker(
        std::size_t index, std::uint64_t clipId,
        double sourceSeconds, double timelineSeconds);
    [[nodiscard]] bool updateAudioClipStretchMarker(
        std::size_t index, std::uint64_t clipId, std::uint64_t markerId,
        double timelineSeconds);
    [[nodiscard]] bool removeAudioClipStretchMarker(
        std::size_t index, std::uint64_t clipId, std::uint64_t markerId);
    [[nodiscard]] juce::Result addAudioClipEventFromFile(
        std::size_t index, std::uint64_t clipId,
        const juce::File& sourceFile, double localStartSeconds,
        std::uint64_t* newEventId = nullptr);
    [[nodiscard]] juce::Result replaceAudioClipEvents(
        std::size_t index, std::uint64_t clipId,
        std::vector<AudioEvent> events);
    [[nodiscard]] bool updateAudioClipEvent(
        std::size_t index, std::uint64_t clipId, std::uint64_t eventId,
        double localStartSeconds, double sourceOffsetSeconds,
        double durationSeconds, double sourceDurationSeconds,
        double fadeInSeconds = -1.0, double fadeOutSeconds = -1.0);
    [[nodiscard]] bool setAudioClipEventProperties(
        std::size_t index, std::uint64_t clipId, std::uint64_t eventId,
        float gainDb, double sourceTempoBpm, StretchMode mode);
    [[nodiscard]] std::uint64_t addAudioClipAutomationPoint(
        std::size_t index, std::uint64_t clipId,
        double timeSeconds, float valueDb);
    [[nodiscard]] bool updateAudioClipAutomationPoint(
        std::size_t index, std::uint64_t clipId, std::uint64_t id,
        double timeSeconds, float valueDb);
    [[nodiscard]] bool setAudioClipAutomationPointCurve(
        std::size_t index, std::uint64_t clipId, std::uint64_t id,
        AutomationCurve curve, float curveAmount);
    [[nodiscard]] bool removeAudioClipAutomationPoint(
        std::size_t index, std::uint64_t clipId, std::uint64_t id);
    [[nodiscard]] std::vector<AutomationPoint> audioClipAutomationPoints(
        std::size_t index, std::uint64_t clipId) const;
    [[nodiscard]] bool setAudioClipTimelineBounds(
        std::size_t index, std::uint64_t clipId,
        double durationSeconds, double playStartSeconds,
        double playEndSeconds, double loopStartSeconds,
        double loopEndSeconds, bool looping);
    // A clip's automation lanes (AudioClip::lanes), one undo step.
    [[nodiscard]] bool setAudioClipLanes(
        std::size_t index, std::uint64_t clipId,
        std::map<juce::String, std::vector<AutomationPoint>> lanes);
    // What a track's volume automation and pan follow while its clips drive
    // them: a volume lane in place of its own (none: its own), a pan lane
    // (empty: its pan knob).
    void setTrackClipAutomation(std::size_t index,
                                std::optional<std::vector<AutomationPoint>> volume,
                                std::vector<AutomationPoint> pan);
    [[nodiscard]] std::uint64_t splitAudioClipEvent(
        std::size_t index, std::uint64_t clipId, std::uint64_t eventId,
        double localTimelineSeconds);
    [[nodiscard]] bool removeAudioClipEvent(
        std::size_t index, std::uint64_t clipId, std::uint64_t eventId);
    [[nodiscard]] std::uint64_t addAudioClipEventStretchMarker(
        std::size_t index, std::uint64_t clipId, std::uint64_t eventId,
        double sourceSeconds, double localTimelineSeconds);
    [[nodiscard]] bool updateAudioClipEventStretchMarker(
        std::size_t index, std::uint64_t clipId, std::uint64_t eventId,
        std::uint64_t markerId, double localTimelineSeconds);
    [[nodiscard]] bool removeAudioClipEventStretchMarker(
        std::size_t index, std::uint64_t clipId, std::uint64_t eventId,
        std::uint64_t markerId);
    [[nodiscard]] juce::Result commitAudioClipEventEdit(
        std::size_t index, std::uint64_t clipId, std::uint64_t eventId,
        const juce::File& renderedFile);
    [[nodiscard]] bool moveAudioClip(
        std::size_t index, std::uint64_t clipId, double startSeconds);
    [[nodiscard]] bool moveAudioClips(
        std::size_t index,
        const std::vector<std::uint64_t>& clipIds,
        double deltaSeconds);
    [[nodiscard]] ArrangementSessionMoveResult moveAudioClipsAcrossTracks(
        const std::vector<std::vector<std::uint64_t>>& clipIdsByTrack,
        int trackDelta,
        double deltaSeconds);
    [[nodiscard]] std::uint64_t duplicateAudioClip(
        std::size_t index, std::uint64_t clipId, double startSeconds = -1.0);
    [[nodiscard]] std::vector<std::uint64_t> duplicateAudioClips(
        std::size_t index,
        const std::vector<std::uint64_t>& clipIds);
    [[nodiscard]] std::vector<std::uint64_t> insertAudioClips(
        std::size_t index,
        const std::vector<AudioClip>& clips,
        double destinationStartSeconds);
    [[nodiscard]] std::uint64_t splitAudioClip(
        std::size_t index, std::uint64_t clipId, double timelineSeconds);
    [[nodiscard]] ArrangementRangeEditResult editAudioClipRange(
        std::size_t index,
        const std::vector<std::uint64_t>& clipIds,
        double rangeStartSeconds,
        double rangeEndSeconds,
        ArrangementRangeOperation operation);
    [[nodiscard]] bool autoCrossfadeAudioClip(
        std::size_t index, std::uint64_t clipId);
    [[nodiscard]] bool removeAudioClip(
        std::size_t index, std::uint64_t clipId);
    [[nodiscard]] bool removeAudioClips(
        std::size_t index,
        const std::vector<std::uint64_t>& clipIds);
    [[nodiscard]] bool undoArrangementEdit(std::size_t index);
    [[nodiscard]] bool redoArrangementEdit(std::size_t index);
    [[nodiscard]] bool undoArrangementEdit();
    [[nodiscard]] bool redoArrangementEdit();
    [[nodiscard]] bool canUndoArrangementEdit() const noexcept;
    [[nodiscard]] bool canRedoArrangementEdit() const noexcept;
    void beginArrangementEditBatch();
    [[nodiscard]] bool endArrangementEditBatch();
    [[nodiscard]] bool cancelArrangementEditBatch();
    [[nodiscard]] bool canUndoArrangementEdit(
        std::size_t index) const noexcept;
    [[nodiscard]] bool canRedoArrangementEdit(
        std::size_t index) const noexcept;
    void clearArrangementEditHistory(std::size_t index) noexcept;
    void clearArrangementEditHistory() noexcept;
    void setTrackEffects(std::size_t index, float driveDb, float filterCutoffHz);
    [[nodiscard]] juce::Result setTrackNativeDspGraph(
        std::size_t index,
        const cydaw::engine::NativeDspGraph& graph);
    [[nodiscard]] juce::Result updateTrackNativeDspParameters(
        std::size_t index,
        const cydaw::engine::NativeDspGraph& graph);
    [[nodiscard]] juce::Result setNativeDspGraph(
        const cydaw::engine::NativeDspGraph& graph);
    [[nodiscard]] cydaw::engine::NativeDspGraph nativeDspGraph(
        std::size_t trackIndex) const;
    [[nodiscard]] std::size_t nativeDspNodeCount(std::size_t trackIndex) const noexcept;
    // The chain's control nodes and their last values; see the MIDI twin.
    [[nodiscard]] std::vector<std::pair<std::uint64_t, float>>
        trackControlValues(std::size_t trackIndex) const;
    // What nodes of a track's chain receive and produce, for the device
    // panel's displays: the taps to keep, then the latest samples of one.
    void setTrackNodeTaps(std::size_t trackIndex,
                          const std::vector<NativeDspRuntime::TapRequest>& taps);
    // The parameters of a track's chain that follow the timeline, read by
    // the callback at each block's start.
    void setTrackNativeAutomation(std::size_t trackIndex, std::vector<NativeDspRuntime::AutomationLane> lanes);
    [[nodiscard]] bool readTrackNodeTap(std::size_t trackIndex, std::uint64_t nodeId, bool input,
                                        cydaw::engine::AudioAnalysisFrame& frame) const;
    [[nodiscard]] bool popTrackAnalysisFrame(
        std::size_t trackIndex,
        cydaw::engine::AudioAnalysisFrame& frame) noexcept;
    [[nodiscard]] bool popMidiTrackAnalysisFrame(
        std::uint64_t trackId,
        cydaw::engine::AudioAnalysisFrame& frame) noexcept;
    [[nodiscard]] bool popMasterAnalysisFrame(
        cydaw::engine::AudioAnalysisFrame& frame) noexcept;
    [[nodiscard]] juce::Result normaliseTrackRange(std::size_t index,
                                                   double sourceStartSeconds,
                                                   double durationSeconds,
                                                   float targetPeakDb = -1.0f,
                                                   float* appliedGainDb = nullptr);
    [[nodiscard]] juce::Result exportTrackRange(std::size_t index,
                                                double sourceStartSeconds,
                                                double durationSeconds,
                                                const juce::File& destination);
    [[nodiscard]] static juce::Result analyseAudioRangeGain(
        const juce::File& source,
        double sourceStartSeconds,
        double durationSeconds,
        float targetPeakDb,
        float& gainDb,
        const std::atomic<bool>* cancellation = nullptr);
    [[nodiscard]] static std::optional<TempoDetectionResult> detectAudioTempo(
        const juce::File& source,
        const std::atomic<bool>* cancellation = nullptr);
    [[nodiscard]] static juce::Result exportAudioRange(
        const juce::File& source,
        double sourceStartSeconds,
        double durationSeconds,
        const juce::File& destination,
        const std::atomic<bool>* cancellation = nullptr);
    [[nodiscard]] static juce::Result renderAudioEdit(
        const juce::File& source,
        double sourceStartSeconds,
        double durationSeconds,
        AudioEditOperation operation,
        const juce::File& destination,
        const std::atomic<bool>* cancellation = nullptr);
    [[nodiscard]] static juce::Result renderAudioEdit(
        const juce::File& source,
        double sourceStartSeconds,
        double durationSeconds,
        AudioEditRequest request,
        const juce::File& destination,
        const std::atomic<bool>* cancellation = nullptr);
    [[nodiscard]] juce::Result commitTrackAudioEdit(std::size_t index,
                                                     const juce::File& renderedFile);
    [[nodiscard]] juce::Result commitAudioClipEdit(
        std::size_t index,
        std::uint64_t clipId,
        const juce::File& renderedFile);
    // Cut, crop, insert silence, paste: the file written again without, or
    // with, the selection (renderAudioEdit hands them here).
    [[nodiscard]] static juce::Result renderAudioSplice(
        const juce::File& source, double sourceStartSeconds, double durationSeconds,
        const AudioEditRequest& request, const juce::File& destination,
        const std::atomic<bool>* cancellation = nullptr);
    // Where a moment of a clip (seconds from its start) plays in its source,
    // through its warp markers; the same for an event of a clip, from the
    // event's start. What the interface draws the waves with.
    [[nodiscard]] static double clipSourceTime(const AudioClip& clip, double timelineSeconds);
    [[nodiscard]] static double eventSourceTime(const AudioEvent& event, double timelineSeconds);
    // A comp written into one file, as it plays: each passage from its
    // take, 10 ms crossfades where they meet, over the whole length of the
    // clip's source (outside the clip, the nearest passage's take), on the
    // source's time (no stretch, no transposition: the clip keeps those).
    [[nodiscard]] static juce::Result renderAudioClipComp(const AudioClip& clip, const juce::File& destination,
                                                          const std::atomic<bool>* cancellation = nullptr);
    // The clip then plays that file alone: one take, no comp; its history
    // gets it, named `label`. One arrangement undo step.
    [[nodiscard]] juce::Result commitAudioClipCompFlatten(std::size_t index, std::uint64_t clipId,
                                                          const juce::File& renderedFile, const juce::String& label);
    // Why an edit of that kind cannot apply to a clip (made of events, warp
    // markers or several takes playing for a cut), or empty.
    [[nodiscard]] juce::String audioClipEditProblem(std::size_t index, std::uint64_t clipId,
                                                    AudioEditOperation operation) const;
    // A clip's source (its active take's file) replaced by a destructive
    // edit's result: it keeps playing what it played around the edit (a cut
    // shortens it, an insert lengthens it); its history (versions) gets the
    // new one, named `label`. One arrangement undo step.
    [[nodiscard]] juce::Result commitAudioClipSourceEdit(
        std::size_t index, std::uint64_t clipId, const juce::File& renderedFile, const juce::String& label,
        AudioEditOperation operation, double selectionStartSeconds, double selectionSeconds);
    // A clip back to one of its versions (one undo step).
    [[nodiscard]] bool setAudioClipVersion(std::size_t index, std::uint64_t clipId, std::size_t version);
    // Its history as a project kept it (no undo step, nothing replayed).
    void restoreAudioClipHistory(std::size_t index, std::uint64_t clipId, std::vector<SourceVersion> versions,
                                 std::size_t version);
    // Transposed without changing its length (semitones, -48..48).
    [[nodiscard]] bool setAudioClipPitch(std::size_t index, std::uint64_t clipId, double semitones);
    // Its takes' best passages (empty: the active take throughout).
    [[nodiscard]] bool setAudioClipComp(std::size_t index, std::uint64_t clipId, std::vector<CompSegment> comp);
    // Takes added at once, each with its own shift (a loop's passes), the
    // last one made active when asked.
    [[nodiscard]] juce::Result addAudioTakes(std::size_t index, std::uint64_t clipId, std::vector<AudioTake> takes,
                                             bool activateLast);
    [[nodiscard]] bool undoTrackAudioEdit(std::size_t index);
    [[nodiscard]] bool redoTrackAudioEdit(std::size_t index);
    [[nodiscard]] bool canUndoTrackAudioEdit(std::size_t index) const noexcept;
    [[nodiscard]] bool canRedoTrackAudioEdit(std::size_t index) const noexcept;
    [[nodiscard]] juce::Result setTrackPlugin(std::size_t index,
                                              const juce::File& pluginFile,
                                              std::size_t* pluginIndex = nullptr);
    juce::Result reloadTrackPlugin(std::size_t track, std::size_t plugin);
    void clearTrackPlugin(std::size_t index, std::size_t pluginIndex);
    void clearTrackPlugins(std::size_t index);
    [[nodiscard]] bool moveTrackPlugin(std::size_t index,
                                       std::size_t pluginIndex,
                                       int direction);
    void pollPluginWorkers();
    [[nodiscard]] juce::Result getTrackPluginState(std::size_t index,
                                                   std::size_t pluginIndex,
                                                   juce::MemoryBlock& destination);
    [[nodiscard]] juce::Result getCachedTrackPluginState(
        std::size_t index,
        std::size_t pluginIndex,
        juce::MemoryBlock& destination) const;
    [[nodiscard]] juce::Result setTrackPluginState(std::size_t index,
                                                   std::size_t pluginIndex,
                                                   const juce::MemoryBlock& state);
    [[nodiscard]] juce::Result showTrackPluginEditor(std::size_t index,
                                                     std::size_t pluginIndex);
    [[nodiscard]] juce::Result refreshTrackPluginParameters(
        std::size_t index,
        std::size_t pluginIndex,
        std::vector<IsolatedPluginHost::ParameterSnapshot>& destination);
    [[nodiscard]] juce::Result setTrackPluginParameter(std::size_t index,
                                                       std::size_t pluginIndex,
                                                       std::uint32_t parameterIndex,
                                                       float normalisedValue);
    // A hosted plug-in's own programs, and choosing one.
    [[nodiscard]] juce::Result getTrackPluginPrograms(std::size_t index, std::size_t pluginIndex,
                                                      juce::StringArray& names, int& current);
    [[nodiscard]] juce::Result setTrackPluginProgram(std::size_t index, std::size_t pluginIndex, int program);

    [[nodiscard]] std::uint64_t addMidiNote(int noteNumber,
                                            double startBeats,
                                            double durationBeats,
                                            float velocity = 0.8f);
    [[nodiscard]] bool updateMidiNote(std::uint64_t id,
                                      int noteNumber,
                                      double startBeats,
                                      double durationBeats,
                                      float velocity);
    [[nodiscard]] bool removeMidiNote(std::uint64_t id);
    void clearMidiNotes();
    [[nodiscard]] std::vector<MidiNote> midiNotes() const;
    [[nodiscard]] std::uint64_t addMidiTrack(juce::String name = {});
    [[nodiscard]] bool moveMidiTrack(std::size_t fromIndex,
                                     std::size_t toIndex);
    [[nodiscard]] bool removeMidiTrack(std::uint64_t trackId);
    void clearMidiTracks();
    [[nodiscard]] std::vector<MidiTrackInfo> midiTracks() const;
    // `launcherScene`: the launcher's slot it goes in, -1 for the
    // arrangement (then at `startBeats`).
    [[nodiscard]] std::uint64_t addMidiClip(std::uint64_t trackId,
                                            juce::String name,
                                            double startBeats = 0.0,
                                            double lengthBeats = 16.0,
                                            bool looping = false,
                                            std::uint32_t colour = 0xff4fd3ae,
                                            int launcherScene = -1);
    // A MIDI clip moved into a launcher slot (-1: into the arrangement).
    [[nodiscard]] bool setMidiClipLauncherScene(std::uint64_t trackId, std::uint64_t clipId, int launcherScene);
    [[nodiscard]] bool removeMidiClip(std::uint64_t trackId,
                                      std::uint64_t clipId);
    // Where a MIDI clip of the arrangement sits, in beats.
    [[nodiscard]] bool setMidiClipStartBeats(std::uint64_t trackId,
                                             std::uint64_t clipId,
                                             double startBeats);
    // Its name and colour; an empty name or a zero colour leaves that one.
    [[nodiscard]] bool setMidiClipPresentation(std::uint64_t trackId,
                                               std::uint64_t clipId,
                                               juce::String name,
                                               std::uint32_t colour);
    [[nodiscard]] bool setMidiClipLooping(std::uint64_t trackId,
                                          std::uint64_t clipId,
                                          bool looping);
    [[nodiscard]] bool setMidiClipTimelineBounds(
        std::uint64_t trackId, std::uint64_t clipId,
        double lengthBeats, double playStartBeats,
        double playEndBeats, double loopStartBeats,
        double loopEndBeats, bool looping);
    [[nodiscard]] std::uint64_t addMidiNote(std::uint64_t trackId,
                                            std::uint64_t clipId,
                                            int noteNumber,
                                            double startBeats,
                                            double durationBeats,
                                            float velocity = 0.8f);
    [[nodiscard]] bool updateMidiNote(std::uint64_t trackId,
                                      std::uint64_t clipId,
                                      std::uint64_t noteId,
                                      int noteNumber,
                                      double startBeats,
                                      double durationBeats,
                                      float velocity);
    [[nodiscard]] bool removeMidiNote(std::uint64_t trackId,
                                      std::uint64_t clipId,
                                      std::uint64_t noteId);
    // A note copied with all it carries (its expressions too), at a start.
    [[nodiscard]] std::uint64_t copyMidiNote(std::uint64_t trackId, std::uint64_t clipId, MidiNote note, double startBeats);
    // One of a note's curves (its points kept in order, inside the note and
    // its range), and how likely it plays.
    [[nodiscard]] bool setMidiNoteCurve(std::uint64_t trackId, std::uint64_t clipId, std::uint64_t noteId, NoteCurve curve,
                                        std::vector<NoteExpressionPoint> points);
    [[nodiscard]] bool setMidiNoteChance(std::uint64_t trackId, std::uint64_t clipId, std::uint64_t noteId, float chance);
    // A note's expressions (pitch in semitones, gain in dB, along it).
    [[nodiscard]] bool setMidiNoteExpression(std::uint64_t trackId, std::uint64_t clipId, std::uint64_t noteId,
                                             std::vector<NoteExpressionPoint> pitch, std::vector<NoteExpressionPoint> gain);
    void clearMidiNotes(std::uint64_t trackId, std::uint64_t clipId);
    [[nodiscard]] bool replaceMidiNotes(
        std::uint64_t trackId,
        std::uint64_t clipId,
        std::vector<MidiNote> notes);
    [[nodiscard]] juce::Result importMidiFile(
        const juce::File& file,
        MidiFileImportSummary& summary,
        bool adoptFileTempo = true);
    [[nodiscard]] juce::Result exportMidiClipToFile(
        std::uint64_t trackId,
        std::uint64_t clipId,
        const juce::File& file) const;
    [[nodiscard]] std::vector<MidiNote> midiNotes(std::uint64_t trackId,
                                                  std::uint64_t clipId) const;
    [[nodiscard]] bool beginMidiEdit(std::uint64_t trackId,
                                     std::uint64_t clipId);
    [[nodiscard]] bool endMidiEdit();
    void cancelMidiEdit();
    [[nodiscard]] bool undoMidiEdit();
    [[nodiscard]] bool redoMidiEdit();
    [[nodiscard]] bool canUndoMidiEdit() const noexcept;
    [[nodiscard]] bool canRedoMidiEdit() const noexcept;
    void clearMidiEditHistory() noexcept;
    [[nodiscard]] juce::Result startMidiRecording(
        std::uint64_t trackId,
        std::uint64_t clipId,
        bool overdub,
        MidiRecordQuantization quantization);
    [[nodiscard]] bool pollMidiRecording();
    void stopMidiRecording(bool commit = true);
    [[nodiscard]] bool isMidiRecording() const noexcept;
    [[nodiscard]] int droppedMidiRecordingEvents() const noexcept;
    [[nodiscard]] std::size_t midiRecordingNoteCount() const noexcept;
    [[nodiscard]] bool enqueueMidiRecordingMessage(
        const juce::MidiMessage& message,
        double timelineSeconds) noexcept;
    void setMidiTrackMixer(std::uint64_t trackId,
                           float gainDb,
                           float pan,
                           bool muted,
                           bool solo);
    [[nodiscard]] std::uint64_t addMidiAutomationPoint(
        std::uint64_t trackId, double timeSeconds, float valueDb);
    [[nodiscard]] bool updateMidiAutomationPoint(
        std::uint64_t trackId, std::uint64_t id,
        double timeSeconds, float valueDb);
    [[nodiscard]] bool setMidiAutomationPointCurve(
        std::uint64_t trackId, std::uint64_t id,
        AutomationCurve curve, float curveAmount);
    [[nodiscard]] bool removeMidiAutomationPoint(
        std::uint64_t trackId, std::uint64_t id);
    [[nodiscard]] std::vector<AutomationPoint> midiAutomationPoints(
        std::uint64_t trackId) const;
    // A whole lane at once (a curve editor's edit): its points' ids kept (0:
    // a fresh one), their shapes and handles too, in time order.
    void replaceMidiAutomationPoints(std::uint64_t trackId, std::vector<AutomationPoint> points);
    [[nodiscard]] std::uint64_t addMidiClipAutomationPoint(
        std::uint64_t trackId, std::uint64_t clipId,
        double timeSeconds, float valueDb);
    [[nodiscard]] bool updateMidiClipAutomationPoint(
        std::uint64_t trackId, std::uint64_t clipId, std::uint64_t id,
        double timeSeconds, float valueDb);
    [[nodiscard]] bool setMidiClipAutomationPointCurve(
        std::uint64_t trackId, std::uint64_t clipId, std::uint64_t id,
        AutomationCurve curve, float curveAmount);
    [[nodiscard]] bool removeMidiClipAutomationPoint(
        std::uint64_t trackId, std::uint64_t clipId, std::uint64_t id);
    [[nodiscard]] std::vector<AutomationPoint> midiClipAutomationPoints(
        std::uint64_t trackId, std::uint64_t clipId) const;
    [[nodiscard]] bool setMidiTrackArmed(std::uint64_t trackId,
                                         bool armed) noexcept;
    // The name the launcher, the mixer and the project show for the track.
    [[nodiscard]] bool setMidiTrackName(std::uint64_t trackId,
                                        juce::String name);
    [[nodiscard]] juce::Result setMidiTrackInstrumentGraph(
        std::uint64_t trackId,
        const cydaw::engine::PolySynthGraphPlan& graph);
    [[nodiscard]] cydaw::engine::PolySynthGraphPlan midiTrackInstrumentGraph(
        std::uint64_t trackId) const;
    // What the graph places after the instrument on a MIDI track: the chain
    // an Ableton or Bitwig user drops behind their synth.
    [[nodiscard]] juce::Result setMidiTrackEffectGraph(
        std::uint64_t trackId, const cydaw::engine::NativeDspGraph& graph);
    [[nodiscard]] cydaw::engine::NativeDspGraph midiTrackEffectGraph(
        std::uint64_t trackId) const;
    // Values only, the way a knob changes them: no recompilation when the
    // chain's shape is unchanged. Fails when it is, and the caller recompiles.
    [[nodiscard]] juce::Result updateMidiTrackEffectParameters(
        std::uint64_t trackId, const cydaw::engine::NativeDspGraph& graph);
    // The chain's control nodes and what each put out last block — an LFO's
    // position, a range node's mapped value. Read on the message thread.
    [[nodiscard]] std::vector<std::pair<std::uint64_t, float>>
        midiTrackControlValues(std::uint64_t trackId) const;
    // The same taps on an instrument track's effect chain; the instrument
    // itself is that chain's input node.
    void setMidiTrackNodeTaps(std::uint64_t trackId,
                              const std::vector<NativeDspRuntime::TapRequest>& taps);
    // The same automation on an instrument track's effect chain.
    void setMidiTrackNativeAutomation(std::uint64_t trackId, std::vector<NativeDspRuntime::AutomationLane> lanes);
    // A parameter of an instrument's voice following the timeline (its
    // filter's cutoff, a wavetable's position...), heard on every voice.
    struct VoiceAutomationLane {
        cydaw::engine::PolyVoiceTarget target {};
        // Which of an instrument's own settings (Prisme's, Kick's...).
        std::uint16_t index {};
        bool logarithmic {};
        std::vector<NativeDspRuntime::AutomationPoint> points;
        // The setting's range: a curve swinging past its points stays in it.
        float minimum { -std::numeric_limits<float>::max() };
        float maximum { std::numeric_limits<float>::max() };
    };
    void setMidiTrackVoiceAutomation(std::uint64_t trackId, std::vector<VoiceAutomationLane> lanes);
    // The control cables of an instrument's graph that move its voices.
    void setMidiTrackVoiceModulations(std::uint64_t trackId,
                                      std::vector<cydaw::engine::PolyVoiceModulation> modulations);
    // ---- An instrument container's pads (InstrumentContainers.h) ----
    // A Drum Machine's pads, an Instrument Layer's or Selector's layers: the
    // track's instrument card says how they hear its notes.
    // Pad `pad`: its instrument's plan and the effects after it (values only
    // when its chain keeps its shape; else compiled again).
    static constexpr std::size_t maximumRackPads = 128;
    [[nodiscard]] juce::Result setMidiTrackRackPad(std::uint64_t trackId, std::size_t pad,
                                                   const cydaw::engine::PolySynthGraphPlan& plan,
                                                   const cydaw::engine::NativeDspGraph& effects);
    struct PadMix {
        // Its key on a drum machine; on a layer, any key (0): whether it
        // sounds at all (-1: the pad is empty, or its container is not the
        // track's instrument).
        int note { -1 };
        // The note a drum pad plays (-1: its key).
        int playAs { -1 };
        float gainDb {};
        float pan {};
        bool muted {};
        bool solo {};
        // Pads of one group cut each other; 0: none.
        int choke {};
        // A layer's keys and velocities (the notes it plays).
        int keyLow {};
        int keyHigh { 127 };
        int velocityLow { 1 };
        int velocityHigh { 127 };
    };
    void setMidiTrackRackPadMix(std::uint64_t trackId, std::size_t pad, const PadMix& mix);
    // How loud it sounds now (linear, falling).
    [[nodiscard]] float midiTrackRackPadPeak(std::uint64_t trackId, std::size_t pad) const;
    // The layer an Instrument Selector plays now (0-based; -1: none).
    [[nodiscard]] int midiTrackSelectedLayer(std::uint64_t trackId) const;
    [[nodiscard]] bool readMidiTrackNodeTap(std::uint64_t trackId, std::uint64_t nodeId, bool input,
                                            cydaw::engine::AudioAnalysisFrame& frame) const;
    [[nodiscard]] bool setLiveMidiTrack(std::uint64_t trackId) noexcept;
    [[nodiscard]] std::uint64_t liveMidiTrackId() const noexcept;
    // ---- An instrument track's MIDI inputs and output ----
    // What it hears while it is the live one (armed, else picked):
    // allMidiInputs (every keyboard plugged in and the computer's),
    // computerKeyboardInput (the computer's alone), or a device's
    // identifier; none: nothing.
    static constexpr const char* allMidiInputs = "*";
    static constexpr const char* computerKeyboardInput = "keyboard";
    void setMidiTrackInputs(std::uint64_t trackId, std::vector<juce::String> inputs);
    [[nodiscard]] std::vector<juce::String> midiTrackInputs(std::uint64_t trackId) const;
    // Its notes (after its MIDI effects) also sent to a MIDI output: a
    // device's identifier, empty for none.
    void setMidiTrackOutput(std::uint64_t trackId, const juce::String& identifier);
    [[nodiscard]] juce::String midiTrackOutput(std::uint64_t trackId) const;
    // The computer's keys: on the live track when it hears them.
    void computerKeyboardNote(int note, float velocity, bool on);
    // ---- Activity, for the lights: counters that move with each message ----
    [[nodiscard]] std::uint32_t midiInputActivity(const juce::String& identifier) const noexcept;
    [[nodiscard]] std::uint32_t computerKeyboardActivity() const noexcept;
    [[nodiscard]] std::uint32_t midiInActivity() const noexcept;
    [[nodiscard]] std::uint32_t midiOutActivity() const noexcept;
    // The notes an instrument track's clips played.
    [[nodiscard]] std::uint32_t midiTrackClipActivity(std::uint64_t trackId) const noexcept;
    // Whether it is the track the MIDI inputs play now.
    [[nodiscard]] bool midiTrackHearsInputs(std::uint64_t trackId) const noexcept;
    // A message sent out by another part (the transport's clock).
    void noteMidiOut() noexcept { midiOutCount_.fetch_add(1, std::memory_order_relaxed); }
    // ---- A track's audio inputs: the main one and three more (routed into
    // it), their levels lately (peaks, falling) ----
    [[nodiscard]] std::array<float, 4> trackInputLevels(std::size_t trackIndex) const noexcept;
    [[nodiscard]] std::array<float, 4> midiTrackInputLevels(std::uint64_t trackId) const noexcept;
    // Whether arming follows the track the keyboard plays (CyDAW 1: the live
    // track is the armed one, one at a time) or is the person's own (CyDAW 2:
    // tracks are armed by hand, several at once; the keyboard plays the
    // selected track, or the armed ones' when some are armed).
    void setArmFollowsMidiInput(bool follows) noexcept;
    // Which messages of the MIDI inputs reach the instruments: the
    // controllers keep those they map, and a device set to control only.
    // Called on the MIDI thread; null lets everything through.
    using MidiInputGate = std::function<bool(const juce::String& identifier, const juce::MidiMessage& message)>;
    void setMidiInputGate(MidiInputGate gate);
    // The track the keyboard and the MIDI input play now.
    [[nodiscard]] std::uint64_t midiInputTrackId() const noexcept;
    [[nodiscard]] bool startMidiClipPreview(std::uint64_t trackId,
                                            std::uint64_t clipId,
                                            int sceneIndex);
    [[nodiscard]] bool stopMidiClipPreview(std::uint64_t trackId);
    [[nodiscard]] bool startMidiClipEditorPreview(
        std::uint64_t trackId, std::uint64_t clipId,
        double localStartSeconds = 0.0);
    [[nodiscard]] bool seekMidiClipEditorPreview(
        std::uint64_t trackId, std::uint64_t clipId,
        double localSeconds);
    [[nodiscard]] bool stopMidiClipEditorPreview(std::uint64_t trackId);
    [[nodiscard]] bool isMidiClipEditorPreviewing(
        std::uint64_t trackId, std::uint64_t clipId) const noexcept;
    [[nodiscard]] double midiClipEditorPreviewPositionSeconds(
        std::uint64_t trackId) const noexcept;
    [[nodiscard]] int activeMidiLauncherScene(
        std::uint64_t trackId) const noexcept;
    [[nodiscard]] float midiLauncherClipProgress(
        std::uint64_t trackId) const noexcept;
    [[nodiscard]] double midiLauncherClipPositionSeconds(
        std::uint64_t trackId) const noexcept;
    // What the track's newest voice plays now (a Prisme's, a synth's of the
    // family): its modulated settings, LFOs and envelopes. False when no
    // voice of theirs plays.
    [[nodiscard]] bool midiTrackVoiceLive(std::uint64_t trackId, cydaw::engine::VoiceLive& out) const noexcept;
    void setTempo(double beatsPerMinute);
    [[nodiscard]] double tempo() const noexcept;
    // The project's metre: how many beats a bar has (numerator) and their
    // value (denominator). The tempo counts quarter notes, so a bar lasts
    // numerator * 4 / denominator of them (5/4: five, 7/8: three and a half).
    void setTimeSignature(int numerator, int denominator) noexcept;
    [[nodiscard]] int timeSignatureNumerator() const noexcept { return timeSignatureNumerator_; }
    [[nodiscard]] int timeSignatureDenominator() const noexcept { return timeSignatureDenominator_; }
    [[nodiscard]] double beatsPerBar() const noexcept
    {
        return static_cast<double>(timeSignatureNumerator_) * 4.0 / static_cast<double>(timeSignatureDenominator_);
    }
    [[nodiscard]] double midiLengthSeconds() const noexcept;
    [[nodiscard]] std::uint64_t addAutomationPoint(std::size_t trackIndex,
                                                    double timeSeconds,
                                                    float valueDb);
    [[nodiscard]] bool updateAutomationPoint(std::size_t trackIndex,
                                             std::uint64_t id,
                                             double timeSeconds,
                                             float valueDb);
    [[nodiscard]] bool setAutomationPointCurve(
        std::size_t trackIndex, std::uint64_t id,
        AutomationCurve curve, float curveAmount);
    [[nodiscard]] bool removeAutomationPoint(std::size_t trackIndex, std::uint64_t id);
    void clearAutomation(std::size_t trackIndex);
    [[nodiscard]] std::vector<AutomationPoint> automationPoints(std::size_t trackIndex) const;
    void replaceAutomationPoints(std::size_t trackIndex, std::vector<AutomationPoint> points);
    [[nodiscard]] std::uint64_t addPluginAutomationPoint(std::size_t trackIndex,
                                                          std::size_t pluginIndex,
                                                          std::uint32_t parameterIndex,
                                                          juce::String parameterName,
                                                          double timeSeconds,
                                                          float normalisedValue);
    [[nodiscard]] bool updatePluginAutomationPoint(std::size_t trackIndex,
                                                    std::size_t pluginIndex,
                                                    std::uint32_t parameterIndex,
                                                    std::uint64_t id,
                                                    double timeSeconds,
                                                    float normalisedValue);
    [[nodiscard]] bool setPluginAutomationPointCurve(
        std::size_t trackIndex, std::size_t pluginIndex,
        std::uint32_t parameterIndex, std::uint64_t id,
        AutomationCurve curve, float curveAmount);
    [[nodiscard]] bool removePluginAutomationPoint(std::size_t trackIndex,
                                                    std::size_t pluginIndex,
                                                    std::uint32_t parameterIndex,
                                                    std::uint64_t id);
    void clearPluginAutomation(std::size_t trackIndex,
                               std::size_t pluginIndex,
                               std::uint32_t parameterIndex);
    void replacePluginAutomationPoints(std::size_t trackIndex,
                                       std::size_t pluginIndex,
                                       std::uint32_t parameterIndex,
                                       juce::String parameterName,
                                       std::vector<PluginAutomationPoint> points);
    [[nodiscard]] std::vector<PluginAutomationPoint> pluginAutomationPoints(
        std::size_t trackIndex,
        std::size_t pluginIndex,
        std::uint32_t parameterIndex) const;
    [[nodiscard]] std::vector<PluginAutomationLane> pluginAutomationLanes(
        std::size_t trackIndex) const;
    [[nodiscard]] bool setLauncherClip(std::size_t trackIndex,
                                       int sceneIndex,
                                       juce::String name,
                                       double sourceOffsetSeconds,
                                       double durationSeconds,
                                       bool looping = true,
                                       std::uint32_t colour = 0xff4fd3ae,
                                       const juce::File& sourceFile = {});
    [[nodiscard]] bool createEmptyLauncherClip(
        std::size_t trackIndex, int sceneIndex, juce::String name,
        double durationSeconds = 4.0,
        std::uint32_t colour = 0xff4fd3ae);
    [[nodiscard]] std::optional<LauncherClip> launcherClip(
        std::size_t trackIndex, int sceneIndex) const;
    [[nodiscard]] bool setLauncherClipTimelineBounds(
        std::size_t trackIndex, int sceneIndex, double durationSeconds,
        double playStartSeconds, double playEndSeconds,
        double loopStartSeconds, double loopEndSeconds, bool looping);
    [[nodiscard]] juce::Result addLauncherClipEventFromFile(
        std::size_t trackIndex, int sceneIndex,
        const juce::File& sourceFile, double localStartSeconds,
        std::uint64_t* newEventId = nullptr);
    [[nodiscard]] juce::Result replaceLauncherClipEvents(
        std::size_t trackIndex, int sceneIndex,
        std::vector<AudioEvent> events);
    [[nodiscard]] bool updateLauncherClipEvent(
        std::size_t trackIndex, int sceneIndex, std::uint64_t eventId,
        double localStartSeconds, double sourceOffsetSeconds,
        double durationSeconds, double sourceDurationSeconds,
        double fadeInSeconds = -1.0, double fadeOutSeconds = -1.0);
    [[nodiscard]] bool setLauncherClipEventProperties(
        std::size_t trackIndex, int sceneIndex, std::uint64_t eventId,
        float gainDb, double sourceTempoBpm, StretchMode mode);
    [[nodiscard]] std::uint64_t addLauncherClipAutomationPoint(
        std::size_t trackIndex, int sceneIndex,
        double timeSeconds, float valueDb);
    [[nodiscard]] bool updateLauncherClipAutomationPoint(
        std::size_t trackIndex, int sceneIndex, std::uint64_t id,
        double timeSeconds, float valueDb);
    [[nodiscard]] bool setLauncherClipAutomationPointCurve(
        std::size_t trackIndex, int sceneIndex, std::uint64_t id,
        AutomationCurve curve, float curveAmount);
    [[nodiscard]] bool removeLauncherClipAutomationPoint(
        std::size_t trackIndex, int sceneIndex, std::uint64_t id);
    [[nodiscard]] std::vector<AutomationPoint> launcherClipAutomationPoints(
        std::size_t trackIndex, int sceneIndex) const;
    [[nodiscard]] std::uint64_t splitLauncherClipEvent(
        std::size_t trackIndex, int sceneIndex, std::uint64_t eventId,
        double localTimelineSeconds);
    [[nodiscard]] bool removeLauncherClipEvent(
        std::size_t trackIndex, int sceneIndex, std::uint64_t eventId);
    [[nodiscard]] std::uint64_t addLauncherClipEventStretchMarker(
        std::size_t trackIndex, int sceneIndex, std::uint64_t eventId,
        double sourceSeconds, double localTimelineSeconds);
    [[nodiscard]] bool updateLauncherClipEventStretchMarker(
        std::size_t trackIndex, int sceneIndex, std::uint64_t eventId,
        std::uint64_t markerId, double localTimelineSeconds);
    [[nodiscard]] bool removeLauncherClipEventStretchMarker(
        std::size_t trackIndex, int sceneIndex, std::uint64_t eventId,
        std::uint64_t markerId);
    [[nodiscard]] bool clearLauncherClip(std::size_t trackIndex, int sceneIndex);
    // A launcher clip put back whole, as it was (an editor's undo): the slot
    // of its scene takes it.
    [[nodiscard]] bool restoreLauncherClip(std::size_t trackIndex, const LauncherClip& clip);
    // A launcher clip's automation lanes (LauncherClip::lanes).
    [[nodiscard]] bool setLauncherClipLanes(
        std::size_t trackIndex, int sceneIndex,
        std::map<juce::String, std::vector<AutomationPoint>> lanes);
    [[nodiscard]] bool transferLauncherClip(
        std::size_t sourceTrackIndex, int sourceSceneIndex,
        std::size_t targetTrackIndex, int targetSceneIndex,
        bool copy);
    [[nodiscard]] std::vector<LauncherClip> launcherClips(std::size_t trackIndex) const;
    [[nodiscard]] bool launchClip(std::size_t trackIndex, int sceneIndex);
    [[nodiscard]] bool startLauncherClipPreview(
        std::size_t trackIndex, int sceneIndex,
        double localStartSeconds = 0.0);
    [[nodiscard]] bool seekLauncherClip(
        std::size_t trackIndex, int sceneIndex, double localSeconds);
    [[nodiscard]] bool launchScene(int sceneIndex);
    // Clips started together, at once and from their start, the
    // arrangement off: what played when the transport stopped, on play.
    struct ClipToRestart {
        bool midi {};
        std::size_t trackIndex {};
        std::uint64_t midiTrackId {};
        std::uint64_t midiClipId {};
        int sceneIndex {};
    };
    [[nodiscard]] bool restartLauncherClips(const std::vector<ClipToRestart>& clips);
    [[nodiscard]] bool stopLauncherTrack(std::size_t trackIndex,
                                         bool quantized = true);
    void stopLauncher();
    [[nodiscard]] int activeLauncherScene(std::size_t trackIndex) const noexcept;
    [[nodiscard]] int pendingLauncherScene(std::size_t trackIndex) const noexcept;
    [[nodiscard]] float launcherClipProgress(
        std::size_t trackIndex) const noexcept;
    [[nodiscard]] double launcherClipPositionSeconds(
        std::size_t trackIndex) const noexcept;
    [[nodiscard]] bool launcherUsesDoubleDeckReadAhead(
        std::size_t trackIndex) const noexcept;
    [[nodiscard]] static juce::Result selfTestDoubleDeckLauncher(
        const juce::File& sourceFile,
        const juce::File& alternateSourceFile = {});
    [[nodiscard]] static juce::Result selfTestLauncherTimeline(
        const juce::File& eventSourceFile);
    [[nodiscard]] static juce::Result selfTestMidiLauncherClip();
    [[nodiscard]] static juce::Result selfTestMulticlipArrangement(
        const juce::File& sourceFile,
        const juce::File& alternateSourceFile = {});
    [[nodiscard]] static juce::Result selfTestAudioTakeRendering(
        const juce::File& firstTake,
        const juce::File& secondTake);
    [[nodiscard]] static juce::Result selfTestStretchRendering(
        const juce::File& sourceFile);
    [[nodiscard]] static juce::Result selfTestPolySynthGraph();
    // Per-note expressions: a pitch curve moves its note alone, a gain
    // curve its level, copies keep them.
    [[nodiscard]] static juce::Result selfTestNoteExpression();
    // Mono and legato: one note at a time, sliding from the last.
    [[nodiscard]] static juce::Result selfTestLegatoVoicing();
    void setLaunchQuantization(LaunchQuantization quantization) noexcept;
    [[nodiscard]] LaunchQuantization launchQuantization() const noexcept;
    [[nodiscard]] double launcherBoundaryDelaySeconds() const noexcept;
    [[nodiscard]] static double quantizedLaunchDelaySeconds(
        double positionSeconds,
        double tempoBpm,
        LaunchQuantization quantization) noexcept;

    void play();
    void stop();
    // While a project loads, every clip restored on a track rebuilds that
    // track's arrangement plan, and each rebuild waits for the read-ahead
    // buffers to fill — five waits per track. Between these two calls the
    // rebuilds are only noted, and done once per track at the end, which
    // returns the first failure among them.
    void beginArrangementBatch() noexcept;
    [[nodiscard]] juce::Result endArrangementBatch();
    void setPosition(double seconds);
    void setArrangementPlaybackEnabled(bool enabled) noexcept;
    [[nodiscard]] bool arrangementPlaybackEnabled() const noexcept;
    void setLooping(bool shouldLoop);
    void setLoopRange(double startSeconds, double endSeconds);
    void resetLoopRange();
    [[nodiscard]] double loopStart() const noexcept;
    [[nodiscard]] double loopEnd() const noexcept;
    [[nodiscard]] bool hasExplicitLoopRange() const noexcept;

    [[nodiscard]] juce::Result startRecording(
        const juce::File& destination,
        double startDelaySeconds = 0.0);
    void scheduleRecordingStop(double delaySeconds);
    void stopRecording();
    [[nodiscard]] bool isRecording() const noexcept;
    [[nodiscard]] bool hasRecordingSession() const noexcept;
    [[nodiscard]] bool recordingStarted() const noexcept;
    [[nodiscard]] bool recordingStopPending() const noexcept;
    [[nodiscard]] bool recordingComplete() const noexcept;
    [[nodiscard]] juce::Result commitRecordedTake(
        const juce::File& recordedFile,
        std::optional<std::size_t> targetTrack,
        double startSeconds,
        RecordedTakeResult& result,
        std::optional<std::uint64_t> targetClipId = std::nullopt);

    [[nodiscard]] juce::Result startMasterExport(const juce::File& destination);
    [[nodiscard]] juce::Result finishMasterExport();
    void cancelMasterExport();
    [[nodiscard]] bool isMasterExporting() const noexcept;
    // Nothing to the sound card (an automated session): the mix, its meters
    // and an export of it go on as before, the speakers stay quiet.
    void setSpeakersSilenced(const bool silenced) noexcept { sourcePlayer_.setGain(silenced ? 0.0f : 1.0f); }
    [[nodiscard]] bool speakersSilenced() const noexcept { return sourcePlayer_.getGain() <= 0.0f; }
    [[nodiscard]] bool masterExportComplete() const noexcept;
    [[nodiscard]] double masterExportProgress() const noexcept;
    [[nodiscard]] int droppedMasterExportBlocks() const noexcept;

    [[nodiscard]] bool hasAudioFile() const noexcept;
    [[nodiscard]] bool isPlaying() const noexcept;
    [[nodiscard]] double position() const noexcept;
    [[nodiscard]] double length() const noexcept;
    [[nodiscard]] juce::String audioDeviceDescription();
    [[nodiscard]] int droppedRecordingBlocks() const noexcept;
    [[nodiscard]] int mixLatencySamples() const noexcept;
    // What a track's own devices make it late by (look-aheads, oversamplers,
    // plugins), and what the mix holds it back by to meet the others.
    [[nodiscard]] int trackLatencySamples(std::size_t index) const noexcept;
    [[nodiscard]] int midiTrackLatencySamples(std::uint64_t trackId) const noexcept;
    [[nodiscard]] int trackCompensationSamples(std::size_t index) const noexcept;
    [[nodiscard]] int midiTrackCompensationSamples(std::uint64_t trackId) const noexcept;

    [[nodiscard]] juce::AudioDeviceManager& deviceManager() noexcept;
    [[nodiscard]] juce::AudioFormatManager& formatManager() noexcept;
    [[nodiscard]] juce::MidiKeyboardState& keyboardState() noexcept;

    // ---- Routing between tracks -----------------------------------------------
    // A track, as the routing names it: an audio track by its index, a MIDI
    // track by its id.
    struct TrackRoute {
        bool midi {};
        std::size_t index {};
        std::uint64_t id {};
    };
    struct TrackOutputRoute {
        std::optional<TrackRoute> destination; // none: the master
        float gainDb {};
        std::uint32_t inputBus {};             // the destination's input: 0 its main one
        bool preFader {};
        // An output bus of the sound card instead, straight from the track
        // (not through the master): 0 for none.
        std::uint64_t hardwareBus {};
        // Into it after its effects (they skip them), rather than before.
        bool hardwareAfterEffects {};
    };
    struct TrackRouting {
        TrackRoute track;
        std::vector<TrackOutputRoute> outputs; // empty: the master
    };
    // Every track's outputs at once: into the master, or into other tracks
    // (their sound joins the destination's own, before its devices). Fails,
    // changing nothing, on a loop.
    juce::Result setTrackRouting(const std::vector<TrackRouting>& routing);

    // ---- The DJ mode and the Foundry -----------------------------------------
    // Their sound joins the master's mixer with the tracks (the master's
    // effects, gain and meter apply).
    [[nodiscard]] DjEngine& dj() noexcept { return *dj_; }
    [[nodiscard]] FoundryPlayer& foundryPlayer() noexcept { return *foundryPlayer_; }
    [[nodiscard]] FoundryPlayer& cinemaPlayer() noexcept { return *cinemaPlayer_; }
    // The Processing mode's: a sound before or after its processing.
    [[nodiscard]] FoundryPlayer& processingPlayer() noexcept { return *processingPlayer_; }

    // ---- The workshop's simulator ------------------------------------------
    // A device tried on its own, apart from the tracks: test signals into an
    // effect, a looped pattern or a keyboard into an instrument, the result
    // into the master (whose gain applies). Silent while inactive.
    enum class SimulationSource : int { none, sine, chords, noise, pinkNoise, drums, pulse, sample };
    struct SimulationNote {
        int noteNumber { 60 };
        double startBeats {};
        double lengthBeats { 1.0 };
        float velocity { 0.8f };
    };
    // Active while the workshop shows a device: the keyboard plays, the
    // effect's tail rings; playing runs the sources and the pattern.
    void setSimulationActive(bool active) noexcept;
    [[nodiscard]] bool isSimulationActive() const noexcept;
    void setSimulationPlaying(bool playing) noexcept;
    [[nodiscard]] bool isSimulationPlaying() const noexcept;
    // Seconds since play.
    [[nodiscard]] double simulationSeconds() const noexcept;
    // The device: an effect's graph (its input fed by the source), or an
    // instrument's voices and what its graph puts after them.
    juce::Result setSimulationEffect(const cydaw::engine::NativeDspGraph& graph, bool monoOutput);
    juce::Result setSimulationInstrument(const cydaw::engine::PolySynthGraphPlan& plan,
                                         const cydaw::engine::NativeDspGraph& chain,
                                         std::vector<cydaw::engine::PolyVoiceModulation> modulations);
    // Values only, the shape unchanged; fails when the shape changed.
    juce::Result updateSimulationParameters(const cydaw::engine::NativeDspGraph& graph);
    void setSimulationSource(SimulationSource source, float levelDb, float toneHz) noexcept;
    // What the device's other audio inputs (a sidechain) hear: the source
    // again, nothing, or a kick on every beat.
    enum class SimulationSideSource : int { same, silence, kick };
    void setSimulationSideSource(SimulationSideSource source) noexcept;
    // An audio file looped as the source (read whole, up to five minutes).
    juce::Result setSimulationSample(const juce::File& file);
    // Notes looped over lengthBeats at the session tempo; empty for none.
    void setSimulationPattern(std::vector<SimulationNote> notes, double lengthBeats);
    void simulationNoteOn(int noteNumber, float velocity);
    void simulationNoteOff(int noteNumber);
    void setSimulationOutputGainDb(float gainDb) noexcept;
    // The highest output peak (linear) since the last call.
    [[nodiscard]] float takeSimulationPeak() noexcept;
    // For the graph's displays and its knobs' live values.
    void setSimulationTaps(const std::vector<NativeDspRuntime::TapRequest>& taps);
    [[nodiscard]] bool readSimulationTap(std::uint64_t nodeId, bool input, cydaw::engine::AudioAnalysisFrame& frame) const;
    [[nodiscard]] std::vector<std::pair<std::uint64_t, float>> simulationControlValues() const;

private:
    struct Simulation;
    std::unique_ptr<Simulation> simulation_;
    std::unique_ptr<DjEngine> dj_;
    std::unique_ptr<FoundryPlayer> foundryPlayer_;
    std::unique_ptr<FoundryPlayer> cinemaPlayer_;
    std::unique_ptr<FoundryPlayer> processingPlayer_;
    std::vector<SimulationNote> simulationPattern_;
    double simulationPatternBeats_ {};
    void publishSimulationPattern();
    // An instrument in the simulator takes the MIDI keyboards from the
    // tracks while it is active, and gives them back.
    void applySimulationMidi() noexcept;
    [[nodiscard]] bool simulationTakesMidi() const noexcept;
    // Which track takes the live input, after a selection, an arm, a
    // recording or the simulator changed.
    void refreshMidiInputs() noexcept;
    struct PlaybackTrack;
    struct ArrangementSessionTrackState {
        std::size_t trackIndex {};
        std::vector<AudioClip> clips;
    };
    struct ArrangementSessionState {
        std::vector<ArrangementSessionTrackState> tracks;
    };
    struct MidiSynthEngine;
    struct MidiPlaybackTrack;
    // A track's notes on their way to a MIDI output: the audio thread
    // queues them, the outputs' thread sends them.
    struct MidiOutQueue;
    class MidiOutputHub;
    // What the live track hears, read by the inputs' callback.
    struct LiveMidiRoute {
        std::uint64_t trackId {};
        bool all { true };
        bool keyboard { true };
        std::vector<juce::String> devices;
    };
    struct MidiSourceSlot {
        std::atomic<std::uint64_t> key {};
        std::atomic<std::uint32_t> count {};
    };
    void countMidiSource(const juce::String& identifier) noexcept;
    void publishMidiOutputs();
    struct MidiEditState {
        std::uint64_t trackId {};
        std::uint64_t clipId {};
        double clipLengthBeats {};
        std::vector<MidiNote> notes;
        // Its start, stop and loop (an undo brings them back too).
        double playStartBeats {};
        double playEndBeats {};
        double loopStartBeats {};
        double loopEndBeats {};
        bool looping {};
    };
    struct MidiRecordingEvent {
        double timelineSeconds {};
        float velocity {};
        std::uint8_t noteNumber {};
        std::uint8_t channel { 1 };
        bool noteOn {};
    };
    struct ActiveMidiRecordingNote {
        std::uint64_t noteId {};
        double startTimelineSeconds {};
        double startBeats {};
        float velocity {};
        bool active {};
    };

    // Stops the transports the predicate picks, all at once: a transport's
    // stop waits for the audio callback to see it, and eight in a row cost
    // a change of view eighty milliseconds.
    void stopTransportsTogether(
        const std::function<bool(PlaybackTrack&)>& shouldStop);
    int arrangementBatchDepth_ {};
    [[nodiscard]] PlaybackTrack* findTrack(std::size_t index) noexcept;
    [[nodiscard]] const PlaybackTrack* findTrack(std::size_t index) const noexcept;
    [[nodiscard]] MidiPlaybackTrack* findMidiTrack(std::uint64_t id) noexcept;
    [[nodiscard]] const MidiPlaybackTrack* findMidiTrack(
        std::uint64_t id) const noexcept;
    [[nodiscard]] MidiClip* findMidiClip(MidiPlaybackTrack& track,
                                         std::uint64_t clipId) noexcept;
    [[nodiscard]] const MidiClip* findMidiClip(
        const MidiPlaybackTrack& track,
        std::uint64_t clipId) const noexcept;
    void refreshTrackGains() noexcept;
    // The latency compensation follows the routing: a track waits for the
    // latest of what is routed into it, every cable for the latest of what
    // reaches its destination, the master for the latest path of all.
    void refreshLatencyCompensation() noexcept;
    void resetLatencyCompensation() noexcept;
    // The mixer's plan from routing_ and the tracks' latencies as they are.
    juce::Result rebuildRouting();
    void publishMidiSequence(MidiPlaybackTrack& track);
    void publishMidiTrackAutomation(MidiPlaybackTrack& track);
    void publishMidiClipAutomation(MidiPlaybackTrack& track,
                                   const MidiClip& clip);
    void publishMidiClipPreviewSequence(MidiPlaybackTrack& track,
                                        const MidiClip& clip);
    void publishAllMidiSequences();
    [[nodiscard]] MidiEditState captureMidiEditState(
        std::uint64_t trackId,
        std::uint64_t clipId) const;
    [[nodiscard]] bool restoreMidiEditState(const MidiEditState& state);
    void prepareMidiMutation(std::uint64_t trackId, std::uint64_t clipId);
    [[nodiscard]] bool closeRecordedMidiNote(int channel,
                                             int noteNumber,
        double timelineSeconds);
    [[nodiscard]] double quantiseRecordedBeat(double beat) const noexcept;
    [[nodiscard]] double midiRecordQuantizationStep() const noexcept;
    [[nodiscard]] double recordingLoopAdjustedTime(
        double timelineSeconds,
        double noteStartSeconds) const noexcept;
    void publishTrackAutomation(PlaybackTrack& track);
    void publishTrackPluginAutomation(PlaybackTrack& track);
    [[nodiscard]] juce::Result rebuildArrangementPlan(PlaybackTrack& track);
    [[nodiscard]] juce::Result rebuildLauncherPlans(PlaybackTrack& track);
    void refreshTransportRanges() noexcept;
    [[nodiscard]] double contentLength() const noexcept;
    [[nodiscard]] double transportLength() const noexcept;
    void recordArrangementEdit(
        PlaybackTrack& track, std::vector<AudioClip> previous);
    [[nodiscard]] bool restoreArrangementSessionState(
        const ArrangementSessionState& state,
        ArrangementSessionState& inverse);
    [[nodiscard]] bool restoreArrangementEditState(
        PlaybackTrack& track, const std::vector<AudioClip>& state);
    [[nodiscard]] double launchDelaySeconds() const noexcept;
    [[nodiscard]] bool scheduleLauncherClip(std::size_t trackIndex,
                                            int sceneIndex,
                                            double delaySeconds,
                                            double localStartSeconds = 0.0);
    [[nodiscard]] bool scheduleLauncherStop(std::size_t trackIndex,
                                            double delaySeconds);

    juce::AudioDeviceManager deviceManager_;
    juce::AudioFormatManager formatManager_;
    juce::TimeSliceThread readAheadThread_ { "CyDAW audio read-ahead" };
    TrackMixer mixerSource_;
    MasterOutputAudioSource masterOutput_ { mixerSource_ };
    // Where each track's outputs go, for the solo: a bus a soloed track is
    // routed into is heard with it.
    std::vector<std::pair<const juce::AudioSource*, std::vector<const juce::AudioSource*>>> routingEdges_;
    // The routing as last published, by track identity rather than position:
    // a track moved keeps it, one removed leaves it, and the plan can be made
    // again when a plugin's latency changes.
    struct RoutingKey {
        const PlaybackTrack* audio {};
        std::uint64_t midi {};
        [[nodiscard]] bool operator==(const RoutingKey&) const = default;
    };
    struct KeptOutput {
        std::optional<RoutingKey> destination; // none: the master
        float gainDb {};
        std::uint32_t inputBus {};
        bool preFader {};
        std::uint64_t hardwareBus {};
        bool hardwareAfterEffects {};
    };
    struct KeptRouting {
        RoutingKey track;
        std::vector<KeptOutput> outputs;
    };
    std::vector<KeptRouting> routing_;
    // The latencies the plan was made for: the plugins' poll makes it again
    // only when one changed.
    std::vector<std::pair<RoutingKey, int>> plannedLatencies_;
    // A cable's delay, kept while its length holds: a change of level must
    // not start it again.
    struct EdgeDelay {
        const juce::AudioSource* from {};
        const juce::AudioSource* to {};
        std::uint32_t bus {};
        bool preFader {};
        std::shared_ptr<AudioDelayLine> line;
    };
    std::vector<EdgeDelay> edgeDelays_;
    double modeRate_{48000};
    std::atomic<double> modePosition_{}, modeEndPosition_{};
    std::atomic<bool> modeTimelineEnabled_{};
    std::atomic<bool> modeMidiPlaying_{true};
    std::atomic<float> masterGainDb_ {};
    std::atomic<bool> masterMuted_ {};
    juce::AudioTransportSource samplePreviewTransport_;
    std::unique_ptr<juce::AudioFormatReaderSource> samplePreviewReader_;
    std::atomic<float> samplePreviewGainDb_ { -3.0f };
    LoopAuditionSource loopAudition_ { samplePreviewGainDb_ };
    // The loop auditioned at the tempo, made again when the tempo moves.
    struct TempoPreview {
        juce::File file;
        SamplePreviewTempo tempo;
        int beats {};
        double sourceSeconds {};
        std::uint64_t generation {};
        std::shared_ptr<const LoopAuditionSource::Loop> loop;
        juce::uint32 checked {};
    } tempoPreview_;
    [[nodiscard]] juce::Result startTempoPreview(const juce::File& file, double startSeconds, const SamplePreviewTempo& tempo);
    std::unique_ptr<PlaybackTrack> clipPreviewTrack_;
    juce::MixerAudioSource auditionMixer_;
    VirtualBusRouter virtualBusRouter_ { auditionMixer_ };
    juce::MidiMessageCollector midiCollector_;
    // The inputs' messages through the gate, then into the collector.
    struct GatedMidiInput final : juce::MidiInputCallback {
        explicit GatedMidiInput(AudioTrackController& owner) : owner_(owner) {}
        void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;
        AudioTrackController& owner_;
    } gatedMidiInput_ { *this };
    juce::SpinLock midiGateLock_;
    std::shared_ptr<const MidiInputGate> midiGate_;
    // Guarded by midiGateLock_ too.
    std::shared_ptr<const LiveMidiRoute> liveRoute_;
    std::array<MidiSourceSlot, 32> midiSources_ {};
    std::atomic<std::uint32_t> midiInCount_ {};
    std::atomic<std::uint32_t> midiOutCount_ {};
    std::atomic<std::uint32_t> keyboardCount_ {};
    std::unique_ptr<MidiOutputHub> midiOutputs_;
    juce::MidiKeyboardState midiKeyboardState_;
    juce::AudioSourcePlayer sourcePlayer_;
    InputRecorder recorder_;
    std::vector<std::unique_ptr<PlaybackTrack>> tracks_;
    std::vector<ArrangementSessionState> arrangementSessionUndoStates_;
    std::vector<ArrangementSessionState> arrangementSessionRedoStates_;
    std::optional<ArrangementSessionState> arrangementEditBatch_;
    std::vector<std::unique_ptr<MidiPlaybackTrack>> midiTracks_;
    std::uint64_t nextMidiTrackId_ { 1 };
    std::uint64_t nextMidiClipId_ { 1 };
    std::uint64_t nextMidiNoteId_ { 1 };
    std::uint64_t nextAudioClipId_ { 1 };
    std::uint64_t nextStretchMarkerId_ { 1 };
    std::uint64_t nextAudioEventId_ { 1'000'000 };
    std::uint64_t nextClipAutomationPointId_ { 2'000'000 };
    std::atomic<std::uint64_t> liveMidiTrackId_ {};
    bool armFollowsMidiInput_ { true };
    std::vector<MidiEditState> midiEditUndoStates_;
    std::vector<MidiEditState> midiEditRedoStates_;
    std::optional<MidiEditState> midiEditBatch_;
    cydaw::realtime::SpscRing<MidiRecordingEvent, 2'048>
        midiRecordingEvents_;
    std::array<ActiveMidiRecordingNote, 16 * 128>
        activeMidiRecordingNotes_ {};
    std::atomic<bool> midiRecordingActive_ {};
    std::atomic<int> droppedMidiRecordingEvents_ {};
    std::uint64_t midiRecordingTrackId_ {};
    std::uint64_t midiRecordingClipId_ {};
    MidiRecordQuantization midiRecordQuantization_ {
        MidiRecordQuantization::sixteenthBeat
    };
    std::size_t midiRecordingNoteCount_ {};
    double tempoBpm_ { 120.0 };
    int timeSignatureNumerator_ { 4 };
    int timeSignatureDenominator_ { 4 };
    bool looping_ {};
    bool loopRangeExplicit_ {};
    double loopStartSeconds_ {};
    double loopEndSeconds_ {};
    LaunchQuantization launchQuantization_ { LaunchQuantization::oneBar };
    std::atomic<bool> arrangementPlaybackEnabled_ { true };
    std::atomic<int> mixLatencySamples_ {};
    cydaw::engine::NativeDspGraph defaultNativeDspGraph_;
    juce::File masterExportDestination_;
    bool masterExportPreviousLooping_ {};
};

} // namespace cydaw::app
