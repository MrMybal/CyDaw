#include <cydaw/base/File.h>
#include <cydaw/base/Text.h>
#pragma once

#include "WindowsPluginBridge.h"

#include <cydaw/plugins/PluginIsolation.h>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cydaw/plugins/WorkerWake.h>
#include <atomic>
#include <array>
#include <memory>
#include <vector>

namespace cydaw::app {

class IsolatedPluginHost final {
public:
    // The session's metre, told to every plug-in with the tempo (4/4 until set).
    static void setSessionMetre(int numerator, int denominator) noexcept;
    struct ParameterSnapshot {
        std::uint32_t index {};
        cydaw::base::Text name;
        cydaw::base::Text label;
        float value {};
        bool automatable {};
        bool discrete {};
        bool boolean {};
        // The value as the plug-in shows it ("-6.0 dB", "Hall").
        cydaw::base::Text text;
    };

    struct RealtimeParameterValue {
        std::uint32_t index {};
        float value {};
    };

    IsolatedPluginHost() = default;
    ~IsolatedPluginHost();

    [[nodiscard]] cydaw::base::Result startPlugin(const cydaw::base::File& pluginFile,
                                           double sampleRate,
                                           int maximumBlockSize,
                                           int channelCount);
    [[nodiscard]] cydaw::base::Result startPassthroughForTest(const cydaw::base::File& workerExecutable,
                                                       double sampleRate,
                                                       int maximumBlockSize,
                                                       int channelCount);
    [[nodiscard]] cydaw::base::Result startPluginForTest(const cydaw::base::File& workerExecutable,
                                                  const cydaw::base::File& pluginFile,
                                                  double sampleRate,
                                                  int maximumBlockSize,
                                                  int channelCount);
    void stop();
    void pollWorkerHealth();
    void terminateWorkerForTest();
    [[nodiscard]] cydaw::base::Result getState(juce::MemoryBlock& destination);
    [[nodiscard]] cydaw::base::Result getCachedState(
        juce::MemoryBlock& destination) const;
    [[nodiscard]] cydaw::base::Result setState(const juce::MemoryBlock& state);
    [[nodiscard]] cydaw::base::Result refreshParameters(std::vector<ParameterSnapshot>& destination);
    [[nodiscard]] cydaw::base::Result setParameter(std::uint32_t index, float normalisedValue);
    // The plug-in's own programs (its factory presets, as a host lists them)
    // and the current one; choosing one, then the state is read again.
    [[nodiscard]] cydaw::base::Result getPrograms(cydaw::base::TextList& names, int& current);
    [[nodiscard]] cydaw::base::Result setProgram(int index);
    void publishRealtimeParameters(const RealtimeParameterValue* values,
                                   std::size_t valueCount) noexcept;
    [[nodiscard]] cydaw::base::Result showEditor();
    [[nodiscard]] cydaw::base::Result hideEditor();

    // Called by the audio thread. Returns true only when a fresh processed
    // block was available. An active worker missing its deadline emits silence;
    // a stopped/crashed worker leaves input untouched for safety bypass.
    [[nodiscard]] bool processBlock(juce::AudioBuffer<float>& buffer,
                                    int startSample,
                                    int numSamples, double timelineSeconds = 0.0, double tempo = 120.0, bool playing = false) noexcept;

    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] cydaw::base::File pluginFile() const;
    [[nodiscard]] cydaw::base::Text pluginName() const;
    [[nodiscard]] cydaw::base::Text statusMessage() const;
    [[nodiscard]] std::uint64_t missedBlockCount() const noexcept;
    [[nodiscard]] int latencySamples() const noexcept;
    // The sample rate and block size its worker was started with (0: none).
    [[nodiscard]] double preparedSampleRate() const noexcept { return sampleRate_; }
    [[nodiscard]] int preparedBlockSize() const noexcept { return blockSize_; }

private:
    [[nodiscard]] cydaw::base::Result startWorker(PluginWorkerLaunch launch,
                                           const cydaw::base::File& pluginFile,
                                           bool passthrough,
                                           double sampleRate,
                                           int maximumBlockSize,
                                           int channelCount);
    [[nodiscard]] cydaw::base::Result sendCommand(cydaw::plugins::SharedPluginCommand command,
                                           int timeoutMilliseconds = 2'000);
    // A command still running in the worker (one that timed out): its data in
    // the shared block is not written over while it reads it.
    [[nodiscard]] static bool controlBusy(cydaw::plugins::SharedPluginAudio& shared) noexcept;

    cydaw::plugins::WorkerWake wake_;
    juce::ChildProcess process_;
    cydaw::base::File sharedFile_;
    cydaw::base::File pluginFile_;
    cydaw::base::Text pluginName_;
    cydaw::base::Text lastStatus_;
    std::unique_ptr<juce::MemoryMappedFile> mapping_;
    std::atomic<cydaw::plugins::SharedPluginAudio*> active_ {};
    std::atomic<int> callbacksInFlight_ {};
    std::atomic<std::uint64_t> missedBlocks_ {};
    std::uint64_t nextSequence_ { 1 };
    std::uint64_t lastOutputSequence_ {};
    std::uint64_t callbackCounter_ {};
    std::uint64_t submittedAtCallback_ {};
    std::uint64_t nextCommandSequence_ { 1 };
    std::atomic<int> pipelineLatencySamples_ {};
    juce::MemoryBlock cachedState_;
    // A parameter set since the state was last read: read again once the
    // gesture rests (pollWorkerHealth), not at every notch.
    double stateStaleSince_ {};
    std::array<RealtimeParameterValue,
               cydaw::plugins::maximumSharedAutomationParameters> pendingRealtimeParameters_ {};
    std::size_t pendingRealtimeParameterCount_ {};
    double sampleRate_ {};
    int blockSize_ {};
    // The channels the worker was started with: a block may bring fewer, the
    // next more again (the shared count is what the last block brought).
    int startedChannels_ { 2 };
    // The worker's heartbeat (one per block it answers) and since when it
    // has not moved while blocks wait: a plug-in stuck in its processing.
    std::uint64_t lastHeartbeat_ {};
    double heartbeatSince_ {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IsolatedPluginHost)
};

} // namespace cydaw::app
