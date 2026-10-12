#pragma once

#include "WindowsPluginBridge.h"

#include <cydaw/plugins/PluginIsolation.h>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <atomic>
#include <array>
#include <memory>
#include <vector>

namespace cydaw::app {

class IsolatedPluginHost final {
public:
    struct ParameterSnapshot {
        std::uint32_t index {};
        juce::String name;
        juce::String label;
        float value {};
        bool automatable {};
        bool discrete {};
        bool boolean {};
        // The value as the plug-in shows it ("-6.0 dB", "Hall").
        juce::String text;
    };

    struct RealtimeParameterValue {
        std::uint32_t index {};
        float value {};
    };

    IsolatedPluginHost() = default;
    ~IsolatedPluginHost();

    [[nodiscard]] juce::Result startPlugin(const juce::File& pluginFile,
                                           double sampleRate,
                                           int maximumBlockSize,
                                           int channelCount);
    [[nodiscard]] juce::Result startPassthroughForTest(const juce::File& workerExecutable,
                                                       double sampleRate,
                                                       int maximumBlockSize,
                                                       int channelCount);
    [[nodiscard]] juce::Result startPluginForTest(const juce::File& workerExecutable,
                                                  const juce::File& pluginFile,
                                                  double sampleRate,
                                                  int maximumBlockSize,
                                                  int channelCount);
    void stop();
    void pollWorkerHealth();
    void terminateWorkerForTest();
    [[nodiscard]] juce::Result getState(juce::MemoryBlock& destination);
    [[nodiscard]] juce::Result getCachedState(
        juce::MemoryBlock& destination) const;
    [[nodiscard]] juce::Result setState(const juce::MemoryBlock& state);
    [[nodiscard]] juce::Result refreshParameters(std::vector<ParameterSnapshot>& destination);
    [[nodiscard]] juce::Result setParameter(std::uint32_t index, float normalisedValue);
    // The plug-in's own programs (its factory presets, as a host lists them)
    // and the current one; choosing one, then the state is read again.
    [[nodiscard]] juce::Result getPrograms(juce::StringArray& names, int& current);
    [[nodiscard]] juce::Result setProgram(int index);
    void publishRealtimeParameters(const RealtimeParameterValue* values,
                                   std::size_t valueCount) noexcept;
    [[nodiscard]] juce::Result showEditor();
    [[nodiscard]] juce::Result hideEditor();

    // Called by the audio thread. Returns true only when a fresh processed
    // block was available; false means that the dry input was left untouched.
    [[nodiscard]] bool processBlock(juce::AudioBuffer<float>& buffer,
                                    int startSample,
                                    int numSamples) noexcept;

    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] juce::File pluginFile() const;
    [[nodiscard]] juce::String pluginName() const;
    [[nodiscard]] juce::String statusMessage() const;
    [[nodiscard]] std::uint64_t missedBlockCount() const noexcept;
    [[nodiscard]] int latencySamples() const noexcept;

private:
    [[nodiscard]] juce::Result startWorker(PluginWorkerLaunch launch,
                                           const juce::File& pluginFile,
                                           bool passthrough,
                                           double sampleRate,
                                           int maximumBlockSize,
                                           int channelCount);
    [[nodiscard]] juce::Result sendCommand(cydaw::plugins::SharedPluginCommand command,
                                           int timeoutMilliseconds = 2'000);

    juce::ChildProcess process_;
    juce::File sharedFile_;
    juce::File pluginFile_;
    juce::String pluginName_;
    juce::String lastStatus_;
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IsolatedPluginHost)
};

} // namespace cydaw::app
