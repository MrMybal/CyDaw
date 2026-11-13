#include <cydaw/base/Utilities.h>
#include <cydaw/base/File.h>
#include <cydaw/base/Text.h>
#pragma once


namespace cydaw::app {

enum class PluginWorkerRuntime {
    native,
    wine,
    protonViaUmu,
    unavailable
};

struct PluginWorkerLaunch {
    PluginWorkerRuntime runtime { PluginWorkerRuntime::unavailable };
    cydaw::base::TextList command;
    cydaw::base::Text error;

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
    cydaw::base::File nativeWorker;
    cydaw::base::File windowsWorker;
    cydaw::base::File configuredRunner;
    cydaw::base::File wineRunner;
    cydaw::base::File umuRunner;
    bool preferUmu {};
};

struct WindowsPluginBridgeStatus {
    bool linuxHost {};
    cydaw::base::File nativeWorker;
    cydaw::base::File windowsWorker;
    cydaw::base::File yabridgeControl;
    cydaw::base::File wineRunner;
    cydaw::base::File umuRunner;
    cydaw::base::File selectedDirectRunner;
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
    [[nodiscard]] static bool isWindowsVst3Bundle(const cydaw::base::File& pluginFile);
    [[nodiscard]] static bool isLinuxVst3Bundle(const cydaw::base::File& pluginFile);
    [[nodiscard]] static bool isWindowsVst2Binary(const cydaw::base::File& pluginFile);
    [[nodiscard]] static bool isLinuxVst2Binary(const cydaw::base::File& pluginFile);
    [[nodiscard]] static cydaw::base::Text pathForWindowsGuest(const cydaw::base::File& file);
    [[nodiscard]] static cydaw::base::Text pathForWindowsGuest(cydaw::base::Text hostPath);
    [[nodiscard]] static PluginWorkerLaunch planWorkerLaunch(
        bool windowsPlugin,
        const PluginWorkerToolchain& toolchain);
    [[nodiscard]] static PluginWorkerLaunch workerLaunchFor(const cydaw::base::File& pluginFile);
    [[nodiscard]] static WindowsPluginBridgeStatus inspect();

private:
    [[nodiscard]] static PluginWorkerToolchain discoverToolchain();
    [[nodiscard]] static cydaw::base::File findExecutable(const cydaw::base::Text& nameOrPath);
    [[nodiscard]] static cydaw::base::File nativeWorkerFile();
    [[nodiscard]] static cydaw::base::File windowsWorkerFile();
};

} // namespace cydaw::app
