#include "IsolatedPluginHost.h"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace {
bool expect(const bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}
}

int main(const int argc, char* argv[])
{
    if (argc < 3) {
        std::cerr << "FAILED: worker or isolated test-plugin argument is missing\n";
        return 1;
    }
    const juce::File worker(juce::String::fromUTF8(argv[1]));
    const juce::File pluginBinary(juce::String::fromUTF8(argv[2]));
    auto pluginBundle = pluginBinary;
    if (pluginBinary.getParentDirectory().getFileName().containsIgnoreCase("x86_64"))
        pluginBundle = pluginBinary.getParentDirectory().getParentDirectory().getParentDirectory();
    else if (pluginBinary.getParentDirectory().getFileName() == "MacOS")
        pluginBundle = pluginBinary.getParentDirectory().getParentDirectory();
    auto passed = expect(worker.existsAsFile(), "plugin worker exists");

    cydaw::app::IsolatedPluginHost host;
    const auto started = host.startPassthroughForTest(worker, 48'000.0, 64, 2);
    passed = expect(started.wasOk(), "start isolated passthrough worker") && passed;
    passed = expect(host.isRunning(), "worker reports running") && passed;

    juce::AudioBuffer<float> buffer(2, 64);
    auto received = false;
    for (int attempt = 0; attempt < 100 && !received; ++attempt) {
        buffer.clear();
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                buffer.setSample(channel, sample, 0.25f);
        received = host.processBlock(buffer, 0, buffer.getNumSamples());
        if (!received)
            juce::Thread::sleep(2);
    }
    passed = expect(received, "receive processed block from worker") && passed;
    if (received) {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                passed = expect(std::abs(buffer.getSample(channel, sample) - 0.25f) < 0.00001f,
                                "shared-memory passthrough preserves samples") && passed;
    }
    passed = expect(host.latencySamples() >= 64, "worker reports pipeline latency") && passed;

    host.terminateWorkerForTest();
    for (int attempt = 0; attempt < 100 && host.isRunning(); ++attempt) {
        juce::Thread::sleep(2);
        host.pollWorkerHealth();
    }
    passed = expect(!host.isRunning(), "worker crash activates bypass") && passed;
    passed = expect(host.pluginName().isNotEmpty(), "crashed plugin identity is retained") && passed;
    passed = expect(host.statusMessage().containsIgnoreCase("bypass"),
                    "crash status reports safety bypass") && passed;
    host.stop();
    buffer.clear();
    buffer.applyGain(0.5f);
    passed = expect(!host.processBlock(buffer, 0, buffer.getNumSamples()),
                    "stopped worker returns safety bypass") && passed;
    passed = expect(!host.isRunning(), "worker stops cleanly") && passed;

#if JUCE_WINDOWS && !JUCE_PLUGINHOST_VST
    const auto disabledVst2Plugin = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                        .getNonexistentChildFile("cydaw-disabled-vst2", ".dll", false);
    {
        auto output = disabledVst2Plugin.createOutputStream();
        passed = expect(output != nullptr, "create temporary disabled VST2 fixture") && passed;
        if (output != nullptr) {
            constexpr char peSignature[] { 'M', 'Z', 0, 0 };
            passed = expect(output->write(peSignature, sizeof(peSignature)),
                            "write temporary disabled VST2 fixture") && passed;
            output->flush();
        }
    }
    cydaw::app::IsolatedPluginHost disabledVst2Host;
    const auto disabledVst2Result = disabledVst2Host.startPluginForTest(
        worker, disabledVst2Plugin, 48'000.0, 64, 2);
    passed = expect(disabledVst2Result.failed()
                        && disabledVst2Result.getErrorMessage().containsIgnoreCase("VST2 hosting"),
                    "worker rejects VST2 explicitly when the optional SDK is disabled") && passed;
    passed = expect(disabledVst2Plugin.deleteFile(),
                    "remove temporary disabled VST2 fixture") && passed;
#endif

    passed = expect(pluginBundle.exists(), "isolated test plugin exists") && passed;
    cydaw::app::IsolatedPluginHost vstHost;
    const auto vstStarted = vstHost.startPluginForTest(worker, pluginBundle, 48'000.0, 64, 2);
    if (vstStarted.failed())
        std::cerr << "Plugin worker error: " << vstStarted.getErrorMessage() << '\n'
                  << "Plugin file: " << pluginBundle.getFullPathName() << '\n';
    passed = expect(vstStarted.wasOk(), "load plugin in isolated worker") && passed;
    std::vector<cydaw::app::IsolatedPluginHost::ParameterSnapshot> parameters;
    passed = expect(vstHost.refreshParameters(parameters).wasOk(),
                    "enumerate plugin parameters through control IPC") && passed;
    const auto findGain = [&parameters] {
        return std::find_if(parameters.begin(), parameters.end(), [](const auto& parameter) {
            return parameter.name == "Gain";
        });
    };
    auto gainParameter = findGain();
    passed = expect(gainParameter != parameters.end()
                        && std::abs(gainParameter->value - 0.5f) < 0.001f,
                    "plugin parameter metadata and value") && passed;
    juce::MemoryBlock initialState;
    passed = expect(vstHost.getState(initialState).wasOk() && !initialState.isEmpty(),
                    "retrieve plugin binary state") && passed;
    juce::MemoryBlock cachedState;
    passed = expect(vstHost.getCachedState(cachedState).wasOk()
                        && cachedState == initialState,
                    "reuse plugin state for non-blocking project autosave")
             && passed;
    const auto gainIndex = gainParameter != parameters.end() ? gainParameter->index : 0u;
    passed = expect(vstHost.setParameter(gainIndex, 0.25f).wasOk(),
                    "set plugin parameter through worker") && passed;
    auto vstReceived = false;
    for (int attempt = 0; attempt < 100 && !vstReceived; ++attempt) {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                buffer.setSample(channel, sample, 0.4f);
        vstReceived = vstHost.processBlock(buffer, 0, buffer.getNumSamples());
        if (!vstReceived)
            juce::Thread::sleep(2);
    }
    passed = expect(vstReceived, "receive audio processed by plugin worker") && passed;
    if (vstReceived)
        passed = expect(std::abs(buffer.getSample(0, 12) - 0.1f) < 0.00001f,
                        "plugin processing changes audio outside host process") && passed;
    const cydaw::app::IsolatedPluginHost::RealtimeParameterValue realtimeGain {
        gainIndex, 0.75f
    };
    vstHost.publishRealtimeParameters(&realtimeGain, 1);
    auto automatedBlockReceived = false;
    for (int attempt = 0; attempt < 100 && !automatedBlockReceived; ++attempt) {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                buffer.setSample(channel, sample, 0.4f);
        if (vstHost.processBlock(buffer, 0, buffer.getNumSamples()))
            automatedBlockReceived = std::abs(buffer.getSample(0, 12) - 0.3f) < 0.00001f;
        if (!automatedBlockReceived)
            juce::Thread::sleep(2);
    }
    passed = expect(automatedBlockReceived,
                    "non-blocking realtime parameter mailbox reaches plugin worker") && passed;
    passed = expect(vstHost.setState(initialState).wasOk(),
                    "restore plugin binary state") && passed;
    parameters.clear();
    const auto refreshed = vstHost.refreshParameters(parameters);
    gainParameter = findGain();
    passed = expect(refreshed.wasOk() && gainParameter != parameters.end()
                        && std::abs(gainParameter->value - 0.5f) < 0.001f,
                    "restored plugin state updates parameter") && passed;
    passed = expect(vstHost.showEditor().wasOk(), "open plugin editor in worker process") && passed;
    passed = expect(vstHost.hideEditor().wasOk(), "hide plugin editor in worker process") && passed;
    vstHost.stop();
    return passed ? 0 : 1;
}
