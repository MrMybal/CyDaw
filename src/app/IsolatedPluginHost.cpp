#include <cydaw/base/Thread.h>
#include <cydaw/base/File.h>
#include "IsolatedPluginHost.h"

#include <algorithm>
#include <cstring>
#include <thread>

#if JUCE_WINDOWS
extern "C" __declspec(dllimport) unsigned long __stdcall GetCurrentProcessId();
#else
#include <unistd.h>
#endif

namespace cydaw::app {

namespace {
std::atomic<int> sessionNumerator { 4 };
std::atomic<int> sessionDenominator { 4 };

// This process: its workers end when it is gone (a crash leaves none behind).
cydaw::base::Text processId()
{
#if JUCE_WINDOWS
    return cydaw::base::Text(static_cast<juce::int64>(GetCurrentProcessId()));
#else
    return cydaw::base::Text(static_cast<juce::int64>(getpid()));
#endif
}
} // namespace

void IsolatedPluginHost::setSessionMetre(const int numerator, const int denominator) noexcept
{
    sessionNumerator.store(std::max(1, numerator), std::memory_order_relaxed);
    sessionDenominator.store(std::max(1, denominator), std::memory_order_relaxed);
}

IsolatedPluginHost::~IsolatedPluginHost()
{
    stop();
}

cydaw::base::Result IsolatedPluginHost::startPlugin(const cydaw::base::File& pluginFile,
                                              const double sampleRate,
                                              const int maximumBlockSize,
                                              const int channelCount)
{
    return startWorker(WindowsPluginBridge::workerLaunchFor(pluginFile),
                       pluginFile, false, sampleRate, maximumBlockSize, channelCount);
}

cydaw::base::Result IsolatedPluginHost::startPassthroughForTest(const cydaw::base::File& workerExecutable,
                                                          const double sampleRate,
                                                          const int maximumBlockSize,
                                                          const int channelCount)
{
    return startWorker({ PluginWorkerRuntime::native,
                         { workerExecutable.getFullPathName() }, {} },
                       {}, true, sampleRate, maximumBlockSize, channelCount);
}

cydaw::base::Result IsolatedPluginHost::startPluginForTest(const cydaw::base::File& workerExecutable,
                                                     const cydaw::base::File& pluginFile,
                                                     const double sampleRate,
                                                     const int maximumBlockSize,
                                                     const int channelCount)
{
    return startWorker({ PluginWorkerRuntime::native,
                         { workerExecutable.getFullPathName() }, {} },
                       pluginFile, false,
                       sampleRate, maximumBlockSize, channelCount);
}

cydaw::base::Result IsolatedPluginHost::startWorker(PluginWorkerLaunch launch,
                                              const cydaw::base::File& pluginFile,
                                              const bool passthrough,
                                              const double sampleRate,
                                              const int maximumBlockSize,
                                              const int channelCount)
{
    stop();
    if (!launch.isValid())
        return cydaw::base::Result::fail(launch.error.isNotEmpty()
                                      ? launch.error
                                      : "No compatible plugin worker is available");
    if (!passthrough && !pluginFile.exists())
        return cydaw::base::Result::fail("The selected plugin does not exist");
    if (maximumBlockSize <= 0
        || maximumBlockSize > static_cast<int>(cydaw::plugins::maximumSharedAudioFrames)
        || channelCount <= 0
        || channelCount > static_cast<int>(cydaw::plugins::maximumSharedAudioChannels))
        return cydaw::base::Result::fail("The current audio block or channel configuration exceeds the worker limit");

    sharedFile_ = cydaw::base::File::getSpecialLocation(cydaw::base::File::tempDirectory)
                      .getNonexistentChildFile("cydaw-plugin-instance", ".ipc", true);
    {
        auto output = sharedFile_.createOutputStream();
        if (output == nullptr || !output->setPosition(0) || output->truncate().failed()
            || !output->setPosition(static_cast<juce::int64>(sizeof(cydaw::plugins::SharedPluginAudio) - 1))
            || !output->writeByte(0)) {
            sharedFile_.deleteFile();
            return cydaw::base::Result::fail("Cannot create the plugin shared-memory file");
        }
        output->flush();
    }

    mapping_ = std::make_unique<juce::MemoryMappedFile>(sharedFile_,
                                                        juce::MemoryMappedFile::readWrite,
                                                        false);
    auto* shared = mapping_ != nullptr
                       ? static_cast<cydaw::plugins::SharedPluginAudio*>(mapping_->getData())
                       : nullptr;
    if (shared == nullptr || mapping_->getSize() < sizeof(cydaw::plugins::SharedPluginAudio)) {
        mapping_.reset();
        sharedFile_.deleteFile();
        return cydaw::base::Result::fail("Cannot map the plugin audio transport");
    }
    std::memset(shared, 0, sizeof(*shared));
    shared->transportNumerator = 4;
    shared->transportDenominator = 4;
    shared->magic = cydaw::plugins::workerProtocolMagic;
    shared->protocolVersion = cydaw::plugins::workerProtocolVersion;
    shared->inputChannels = static_cast<std::uint16_t>(channelCount);
    shared->outputChannels = static_cast<std::uint16_t>(channelCount);
    cydaw::plugins::sharedAtomic(shared->state).store(
        static_cast<std::uint32_t>(cydaw::plugins::SharedPluginState::loading),
        std::memory_order_release);

    wake_.open(("Local\\CyDAW.PluginAudio." + sharedFile_.getFileName()).toWideCharPointer());
    auto arguments = launch.command;
    arguments.add(passthrough ? "--realtime-passthrough" : "--realtime-plugin");
    if (!passthrough)
        arguments.add(launch.usesWindowsGuestPaths()
                          ? WindowsPluginBridge::pathForWindowsGuest(pluginFile)
                          : pluginFile.getFullPathName());
    arguments.addArray({ "--shared-file",
                         launch.usesWindowsGuestPaths()
                             ? WindowsPluginBridge::pathForWindowsGuest(sharedFile_)
                             : sharedFile_.getFullPathName(),
                         "--sample-rate", cydaw::base::Text(sampleRate, 3),
                         "--block-size", cydaw::base::Text(maximumBlockSize),
                         "--channels", cydaw::base::Text(channelCount) });
    // A worker under Wine sees other process numbers: it is not told.
    if (!launch.usesWindowsGuestPaths())
        arguments.addArray({ "--parent-pid", processId() });
    if (!process_.start(arguments, 0)) {
        mapping_.reset();
        sharedFile_.deleteFile();
        return cydaw::base::Result::fail("Cannot start the isolated plugin worker");
    }

    auto state = cydaw::plugins::SharedPluginState::loading;
    for (int waited = 0; waited < 15'000; waited += 10) {
        state = static_cast<cydaw::plugins::SharedPluginState>(
            cydaw::plugins::sharedAtomic(shared->state).load(std::memory_order_acquire));
        if (state == cydaw::plugins::SharedPluginState::ready
            || state == cydaw::plugins::SharedPluginState::error)
            break;
        if (!process_.isRunning())
            break;
        cydaw::base::Thread::sleep(10);
    }
    if (state != cydaw::plugins::SharedPluginState::ready) {
        const auto error = cydaw::base::Text::fromUTF8(shared->statusMessage);
        const auto failure = error.isNotEmpty() ? error : "Plugin worker failed to initialise or timed out";
        stop();
        lastStatus_ = failure;
        return cydaw::base::Result::fail(failure);
    }

    pluginFile_ = pluginFile;
    pluginName_ = passthrough ? "IPC Passthrough" : pluginFile.getFileNameWithoutExtension();
    if(!passthrough&&pluginFile.hasFileExtension("cyplugin"))pluginName_=cydaw::base::JSON::parse(pluginFile).getProperty("name",pluginName_).toString();
    lastStatus_ = "Running in isolated worker";
    nextSequence_ = 1;
    lastOutputSequence_ = 0;
    callbackCounter_ = 0;
    submittedAtCallback_ = 0;
    nextCommandSequence_ = 1;
    pendingRealtimeParameterCount_ = 0;
    missedBlocks_.store(0, std::memory_order_relaxed);
    pipelineLatencySamples_.store(maximumBlockSize, std::memory_order_release);
    sampleRate_ = sampleRate;
    blockSize_ = maximumBlockSize;
    startedChannels_ = channelCount;
    lastHeartbeat_ = 0;
    heartbeatSince_ = cydaw::base::Time::getMillisecondCounterHiRes() * 0.001;
    active_.store(shared, std::memory_order_release);
    juce::MemoryBlock initialState;
    (void) getState(initialState);
    return cydaw::base::Result::ok();
}

void IsolatedPluginHost::stop()
{
    active_.store(nullptr);
    while (callbacksInFlight_.load() != 0)
        std::this_thread::yield();

    if (mapping_ != nullptr) {
        if (auto* shared = static_cast<cydaw::plugins::SharedPluginAudio*>(mapping_->getData())) {
            cydaw::plugins::sharedAtomic(shared->shutdownRequested).store(1, std::memory_order_release);
            wake_.signal();
            cydaw::plugins::sharedAtomic(shared->state).store(
                static_cast<std::uint32_t>(cydaw::plugins::SharedPluginState::stopping),
                std::memory_order_release);
        }
    }
    if (process_.isRunning() && !process_.waitForProcessToFinish(750))
        process_.kill();
    wake_.close();
    mapping_.reset();
    if (sharedFile_ != cydaw::base::File {})
        sharedFile_.deleteFile();
    sharedFile_ = cydaw::base::File {};
    pluginFile_ = cydaw::base::File {};
    pluginName_.clear();
    lastStatus_ = "Stopped";
    nextSequence_ = 1;
    lastOutputSequence_ = 0;
    callbackCounter_ = 0;
    submittedAtCallback_ = 0;
    nextCommandSequence_ = 1;
    pendingRealtimeParameterCount_ = 0;
    pipelineLatencySamples_.store(0, std::memory_order_release);
    sampleRate_ = 0.0;
    blockSize_ = 0;
    cachedState_.reset();
}

void IsolatedPluginHost::pollWorkerHealth()
{
    auto* shared = active_.load(std::memory_order_acquire);
    if (shared == nullptr)
        return;
    const auto state = static_cast<cydaw::plugins::SharedPluginState>(
        cydaw::plugins::sharedAtomic(shared->state).load(std::memory_order_acquire));
    // A plug-in stuck in its processing: the process alive, blocks waiting,
    // no answer for seconds (and no command running, which may take long).
    // It is ended, and the track goes on dry as after a crash.
    auto hung = false;
    {
        const auto now = cydaw::base::Time::getMillisecondCounterHiRes() * 0.001;
        const auto beat = cydaw::plugins::sharedAtomic(shared->heartbeat).load(std::memory_order_acquire);
        const auto waiting = cydaw::plugins::sharedAtomic(shared->requestSequence).load(std::memory_order_acquire)
                             != cydaw::plugins::sharedAtomic(shared->responseSequence).load(std::memory_order_acquire);
        const auto commanding = cydaw::plugins::sharedAtomic(shared->commandSequence).load(std::memory_order_acquire)
                                != cydaw::plugins::sharedAtomic(shared->commandResponseSequence).load(std::memory_order_acquire);
        if (beat != lastHeartbeat_ || !waiting || commanding) {
            lastHeartbeat_ = beat;
            heartbeatSince_ = now;
        } else if (now - heartbeatSince_ > 8.0) {
            hung = true;
        }
    }
    if (hung && process_.isRunning())
        process_.kill();
    if (process_.isRunning() && state != cydaw::plugins::SharedPluginState::error) {
        // Parameters set and at rest for a moment: the state kept (what a
        // restarted worker gets back, what an autosave writes) follows.
        if (stateStaleSince_ > 0.0 && cydaw::base::Time::getMillisecondCounterHiRes() * 0.001 - stateStaleSince_ > 0.4) {
            stateStaleSince_ = 0.0;
            juce::MemoryBlock fresh;
            (void) getState(fresh);
        }
        return;
    }

    const auto retainedFile = pluginFile_;
    const auto retainedName = pluginName_;
    const auto retainedState = cachedState_;
    auto failure = state == cydaw::plugins::SharedPluginState::error
                       ? cydaw::base::Text::fromUTF8(shared->statusMessage)
                   : hung ? cydaw::base::Text("Plugin stopped answering")
                          : cydaw::base::Text("Plugin worker crashed");
    if (failure.isEmpty())
        failure = "Plugin worker unavailable";
    stop();
    pluginFile_ = retainedFile;
    pluginName_ = retainedName;
    cachedState_ = retainedState;
    lastStatus_ = failure + " — safety bypass active";
}

void IsolatedPluginHost::terminateWorkerForTest()
{
    if (process_.isRunning())
        process_.kill();
}

cydaw::base::Result IsolatedPluginHost::sendCommand(const cydaw::plugins::SharedPluginCommand command,
                                              const int timeoutMilliseconds)
{
    auto* shared = active_.load(std::memory_order_acquire);
    if (shared == nullptr)
        return cydaw::base::Result::fail("Plugin worker is not running");
    auto request = cydaw::plugins::sharedAtomic(shared->commandSequence);
    auto response = cydaw::plugins::sharedAtomic(shared->commandResponseSequence);
    if (request.load(std::memory_order_acquire) != response.load(std::memory_order_acquire))
        return cydaw::base::Result::fail("Plugin control channel is busy");

    shared->command = static_cast<std::uint32_t>(command);
    shared->commandResult = 0;
    shared->commandStatus[0] = '\0';
    const auto sequence = nextCommandSequence_++;
    request.store(sequence, std::memory_order_release);
    wake_.signal();
    for (int waited = 0; waited < timeoutMilliseconds; ++waited) {
        if (response.load(std::memory_order_acquire) == sequence) {
            if (shared->commandResult == 0)
                return cydaw::base::Result::ok();
            const auto message = cydaw::base::Text::fromUTF8(shared->commandStatus);
            return cydaw::base::Result::fail(message.isNotEmpty() ? message : "Plugin command failed");
        }
        if (!process_.isRunning())
            return cydaw::base::Result::fail("Plugin worker exited during control command");
        cydaw::base::Thread::sleep(1);
    }
    return cydaw::base::Result::fail("Plugin control command timed out");
}

cydaw::base::Result IsolatedPluginHost::getState(juce::MemoryBlock& destination)
{
    const auto result = sendCommand(cydaw::plugins::SharedPluginCommand::getState);
    auto* shared = active_.load(std::memory_order_acquire);
    if (result.wasOk() && shared != nullptr
        && shared->stateBytes <= cydaw::plugins::maximumSharedPluginStateBytes) {
        destination.replaceAll(shared->stateData, shared->stateBytes);
        cachedState_ = destination;
        return cydaw::base::Result::ok();
    }
    if (!cachedState_.isEmpty()) {
        destination = cachedState_;
        return cydaw::base::Result::ok();
    }
    return result;
}

cydaw::base::Result IsolatedPluginHost::getCachedState(
    juce::MemoryBlock& destination) const
{
    if (cachedState_.isEmpty())
        return cydaw::base::Result::fail("No cached plugin state is available");
    destination = cachedState_;
    return cydaw::base::Result::ok();
}

bool IsolatedPluginHost::controlBusy(cydaw::plugins::SharedPluginAudio& shared) noexcept
{
    return cydaw::plugins::sharedAtomic(shared.commandSequence).load(std::memory_order_acquire)
           != cydaw::plugins::sharedAtomic(shared.commandResponseSequence).load(std::memory_order_acquire);
}

cydaw::base::Result IsolatedPluginHost::setState(const juce::MemoryBlock& state)
{
    auto* shared = active_.load(std::memory_order_acquire);
    if (shared == nullptr)
        return cydaw::base::Result::fail("Plugin worker is not running");
    if (controlBusy(*shared))
        return cydaw::base::Result::fail("Plugin control channel is busy");
    if (state.getSize() > cydaw::plugins::maximumSharedPluginStateBytes)
        return cydaw::base::Result::fail("Plugin state exceeds the 4 MB shared-memory limit");
    if (!state.isEmpty())
        std::memcpy(shared->stateData, state.getData(), state.getSize());
    shared->stateBytes = static_cast<std::uint32_t>(state.getSize());
    const auto result = sendCommand(cydaw::plugins::SharedPluginCommand::setState);
    if (result.wasOk())
        cachedState_ = state;
    return result;
}

cydaw::base::Result IsolatedPluginHost::refreshParameters(std::vector<ParameterSnapshot>& destination)
{
    destination.clear();
    const auto result = sendCommand(cydaw::plugins::SharedPluginCommand::refreshParameters);
    auto* shared = active_.load(std::memory_order_acquire);
    if (result.failed() || shared == nullptr)
        return result;
    const auto count = std::min(shared->parameterCount,
                                cydaw::plugins::maximumSharedPluginParameters);
    destination.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto& parameter = shared->parameters[index];
        destination.push_back({ parameter.index,
                                cydaw::base::Text::fromUTF8(parameter.name),
                                cydaw::base::Text::fromUTF8(parameter.label),
                                parameter.value,
                                (parameter.flags & cydaw::plugins::parameterAutomatable) != 0,
                                (parameter.flags & cydaw::plugins::parameterDiscrete) != 0,
                                (parameter.flags & cydaw::plugins::parameterBoolean) != 0,
                                cydaw::base::Text::fromUTF8(parameter.text) });
    }
    return cydaw::base::Result::ok();
}

cydaw::base::Result IsolatedPluginHost::getPrograms(cydaw::base::TextList& names, int& current)
{
    names.clear();
    current = -1;
    const auto result = sendCommand(cydaw::plugins::SharedPluginCommand::getPrograms);
    auto* shared = active_.load(std::memory_order_acquire);
    if (result.failed() || shared == nullptr)
        return result;
    const auto count = static_cast<int>(shared->commandParameterValue);
    const auto bytes = std::min(shared->stateBytes, static_cast<std::uint32_t>(cydaw::plugins::maximumSharedPluginStateBytes));
    const auto text = cydaw::base::Text::fromUTF8(reinterpret_cast<const char*>(shared->stateData), static_cast<int>(bytes));
    auto start = 0;
    for (int index = 0; index < count; ++index) {
        const auto end = text.indexOfChar(start, '\n');
        names.add(text.substring(start, end < 0 ? text.length() : end));
        start = end < 0 ? text.length() : end + 1;
    }
    current = count > 0 ? juce::jlimit(0, count - 1, static_cast<int>(shared->commandParameterIndex)) : -1;
    return cydaw::base::Result::ok();
}

cydaw::base::Result IsolatedPluginHost::setProgram(const int index)
{
    auto* shared = active_.load(std::memory_order_acquire);
    if (shared == nullptr)
        return cydaw::base::Result::fail("Plugin worker is not running");
    if (index < 0)
        return cydaw::base::Result::fail("Unknown plugin program");
    if (controlBusy(*shared))
        return cydaw::base::Result::fail("Plugin control channel is busy");
    shared->commandParameterIndex = static_cast<std::uint32_t>(index);
    const auto result = sendCommand(cydaw::plugins::SharedPluginCommand::setProgram);
    if (result.wasOk()) {
        juce::MemoryBlock state;
        (void) getState(state);
    }
    return result;
}

cydaw::base::Result IsolatedPluginHost::setParameter(const std::uint32_t index,
                                               const float normalisedValue)
{
    auto* shared = active_.load(std::memory_order_acquire);
    if (shared == nullptr)
        return cydaw::base::Result::fail("Plugin worker is not running");
    if (controlBusy(*shared))
        return cydaw::base::Result::fail("Plugin control channel is busy");
    shared->commandParameterIndex = index;
    shared->commandParameterValue = juce::jlimit(0.0f, 1.0f, normalisedValue);
    const auto result = sendCommand(cydaw::plugins::SharedPluginCommand::setParameter);
    // The state is read again once the knob rests: a drag sends dozens of
    // values, and a big plug-in's state takes a while to write.
    if (result.wasOk())
        stateStaleSince_ = cydaw::base::Time::getMillisecondCounterHiRes() * 0.001;
    return result;
}

void IsolatedPluginHost::publishRealtimeParameters(const RealtimeParameterValue* const values,
                                                    const std::size_t valueCount) noexcept
{
    if (values == nullptr || valueCount == 0)
        return;

    const auto count = std::min(valueCount,
                                pendingRealtimeParameters_.size());
    for (std::size_t valueIndex = 0; valueIndex < count; ++valueIndex)
        pendingRealtimeParameters_[valueIndex] = {
            values[valueIndex].index,
            juce::jlimit(0.0f, 1.0f, values[valueIndex].value)
        };
    pendingRealtimeParameterCount_ = count;
}

cydaw::base::Result IsolatedPluginHost::showEditor()
{
    return sendCommand(cydaw::plugins::SharedPluginCommand::showEditor, 5'000);
}

cydaw::base::Result IsolatedPluginHost::hideEditor()
{
    return sendCommand(cydaw::plugins::SharedPluginCommand::hideEditor);
}

bool IsolatedPluginHost::processBlock(juce::AudioBuffer<float>& buffer,
                                      const int startSample,
                                      const int numSamples, double timelineSeconds, double tempo, bool playing) noexcept
{
    callbacksInFlight_.fetch_add(1);
    auto* shared = active_.load();
    if (shared == nullptr || numSamples <= 0
        || numSamples > static_cast<int>(cydaw::plugins::maximumSharedAudioFrames)) {
        callbacksInFlight_.fetch_sub(1, std::memory_order_acq_rel);
        return false;
    }

    const auto channelCount = std::min({ buffer.getNumChannels(), startedChannels_,
                                         static_cast<int>(cydaw::plugins::maximumSharedAudioChannels) });
    const auto callback = ++callbackCounter_;
    auto request = cydaw::plugins::sharedAtomic(shared->requestSequence);
    auto response = cydaw::plugins::sharedAtomic(shared->responseSequence);
    const auto requestSequence = request.load(std::memory_order_acquire);
    const auto responseSequence = response.load(std::memory_order_acquire);
    auto processed = false;

    if (requestSequence == responseSequence) {
        shared->transportSeconds = std::isfinite(timelineSeconds) ? timelineSeconds : 0.0;
        shared->transportTempo = std::isfinite(tempo) && tempo > 0 ? tempo : 120.0;
        shared->transportQuarterNotes = shared->transportSeconds * shared->transportTempo / 60.0;
        shared->transportPlaying = playing ? 1u : 0u;
        shared->transportNumerator = static_cast<std::uint32_t>(sessionNumerator.load(std::memory_order_relaxed));
        shared->transportDenominator = static_cast<std::uint32_t>(sessionDenominator.load(std::memory_order_relaxed));
        if (pendingRealtimeParameterCount_ > 0) {
            for (std::size_t valueIndex = 0;
                 valueIndex < pendingRealtimeParameterCount_;
                 ++valueIndex) {
                shared->automation[valueIndex].index = pendingRealtimeParameters_[valueIndex].index;
                shared->automation[valueIndex].value = pendingRealtimeParameters_[valueIndex].value;
            }
            shared->automationCount = static_cast<std::uint32_t>(
                pendingRealtimeParameterCount_);
            cydaw::plugins::sharedAtomic(shared->automationSequence).fetch_add(1,
                                                                  std::memory_order_release);
            pendingRealtimeParameterCount_ = 0;
        }
        for (int channel = 0; channel < channelCount; ++channel) {
            const auto* source = buffer.getReadPointer(channel, startSample);
            for (int frame = 0; frame < numSamples; ++frame)
                shared->input[cydaw::plugins::sharedAudioIndex(static_cast<std::uint32_t>(channel),
                                                               static_cast<std::uint32_t>(frame))] = source[frame];
        }

        if (responseSequence > lastOutputSequence_) {
            const auto onTime = callback == submittedAtCallback_ + 1;
            if (onTime && shared->outputFrameCount == static_cast<std::uint32_t>(numSamples)) {
                const auto outputChannels = std::min(channelCount, static_cast<int>(shared->outputChannels));
                for (int channel = 0; channel < outputChannels; ++channel) {
                    auto* destination = buffer.getWritePointer(channel, startSample);
                    for (int frame = 0; frame < numSamples; ++frame)
                        destination[frame] = shared->output[
                            cydaw::plugins::sharedAudioIndex(static_cast<std::uint32_t>(channel),
                                                             static_cast<std::uint32_t>(frame))];
                }
                processed = true;
            } else {
                missedBlocks_.fetch_add(1, std::memory_order_relaxed);
            }
            lastOutputSequence_ = responseSequence;
        }

        shared->frameCount = static_cast<std::uint32_t>(numSamples);
        shared->inputChannels = static_cast<std::uint16_t>(channelCount);
        shared->outputChannels = static_cast<std::uint16_t>(channelCount);
        submittedAtCallback_ = callback;
        request.store(nextSequence_++, std::memory_order_release);
        wake_.signal();
    } else {
        missedBlocks_.fetch_add(1, std::memory_order_relaxed);
    }

    // An active worker has one block of declared pipeline latency. A missing
    // response must not inject the current dry block into the processed path:
    // that bypasses effects and invalidates both gain and latency compensation.
    if (!processed)
        buffer.clear(startSample, numSamples);
    callbacksInFlight_.fetch_sub(1, std::memory_order_acq_rel);
    return processed;
}

bool IsolatedPluginHost::isRunning() const noexcept
{
    return active_.load(std::memory_order_acquire) != nullptr;
}

cydaw::base::File IsolatedPluginHost::pluginFile() const { return pluginFile_; }
cydaw::base::Text IsolatedPluginHost::pluginName() const { return pluginName_; }
cydaw::base::Text IsolatedPluginHost::statusMessage() const { return lastStatus_; }

std::uint64_t IsolatedPluginHost::missedBlockCount() const noexcept
{
    return missedBlocks_.load(std::memory_order_relaxed);
}

int IsolatedPluginHost::latencySamples() const noexcept
{
    if (auto* shared = active_.load(std::memory_order_acquire)) {
        const auto pluginLatency = cydaw::plugins::sharedAtomic(shared->latencySamples)
                                       .load(std::memory_order_acquire);
        return static_cast<int>(pluginLatency)
               + pipelineLatencySamples_.load(std::memory_order_acquire);
    }
    return 0;
}

} // namespace cydaw::app
