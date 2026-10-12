#pragma once

#include <juce_core/juce_core.h>

namespace cydaw::app {

enum class PluginWorkerRuntime {
    native,
    wine,
    protonViaUmu,
    unavailable
};

struct PluginWorkerLaunch {
    PluginWorkerRuntime runtime { PluginWorkerRuntime::unavailable };
    juce::StringArray command;
    juce::String error;

    [[nodiscard]] bool isValid() const noexcept
    {
        return runtime != PluginWorkerRuntime::unavailable && !command.isEmpty();
    }

    [[nodiscard]] bool usesWindowsGuestPaths() const noexcept
    {
        return runtime == PluginWorkerRuntime::wine
               || runtime == PluginWorkerRuntime::protonViaUmu;
    }
};

struct PluginWorkerToolchain {
    juce::File nativeWorker;
    juce::File windowsWorker;
    juce::File configuredRunner;
    juce::File wineRunner;
    juce::File umuRunner;
    bool preferUmu {};
};

struct WindowsPluginBridgeStatus {
    bool linuxHost {};
    juce::File nativeWorker;
    juce::File windowsWorker;
    juce::File yabridgeControl;
    juce::File wineRunner;
    juce::File umuRunner;
    juce::File selectedDirectRunner;
    PluginWorkerRuntime directRuntime { PluginWorkerRuntime::unavailable };

    [[nodiscard]] bool yabridgeReady() const noexcept
    {
        return yabridgeControl.existsAsFile() && wineRunner.existsAsFile();
    }

    [[nodiscard]] bool directBridgeReady() const noexcept
    {
        return windowsWorker.existsAsFile()
               && selectedDirectRunner.existsAsFile()
               && (directRuntime == PluginWorkerRuntime::wine
                   || directRuntime == PluginWorkerRuntime::protonViaUmu);
    }
};

class WindowsPluginBridge final {
public:
    [[nodiscard]] static bool isWindowsVst3Bundle(const juce::File& pluginFile);
    [[nodiscard]] static bool isLinuxVst3Bundle(const juce::File& pluginFile);
    [[nodiscard]] static bool isWindowsVst2Binary(const juce::File& pluginFile);
    [[nodiscard]] static bool isLinuxVst2Binary(const juce::File& pluginFile);
    [[nodiscard]] static juce::String pathForWindowsGuest(const juce::File& file);
    [[nodiscard]] static juce::String pathForWindowsGuest(juce::String hostPath);
    [[nodiscard]] static PluginWorkerLaunch planWorkerLaunch(
        bool windowsPlugin,
        const PluginWorkerToolchain& toolchain);
    [[nodiscard]] static PluginWorkerLaunch workerLaunchFor(const juce::File& pluginFile);
    [[nodiscard]] static WindowsPluginBridgeStatus inspect();

private:
    [[nodiscard]] static PluginWorkerToolchain discoverToolchain();
    [[nodiscard]] static juce::File findExecutable(const juce::String& nameOrPath);
    [[nodiscard]] static juce::File nativeWorkerFile();
    [[nodiscard]] static juce::File windowsWorkerFile();
};

} // namespace cydaw::app
