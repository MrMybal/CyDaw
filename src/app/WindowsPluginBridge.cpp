#include <cydaw/base/File.h>
#include <cydaw/base/Text.h>
#include "WindowsPluginBridge.h"

#include <array>
#include <cstdint>

namespace cydaw::app {
namespace {

cydaw::base::File architectureDirectory(const cydaw::base::File& pluginFile,
                                 const cydaw::base::Text& architecture)
{
    return pluginFile.getChildFile("Contents").getChildFile(architecture);
}

cydaw::base::File environmentFile(const cydaw::base::Text& name)
{
    const auto value = cydaw::base::SystemStats::getEnvironmentVariable(name, {}).trim();
    return value.isNotEmpty() ? cydaw::base::File(value) : cydaw::base::File {};
}

bool environmentFlag(const cydaw::base::Text& name)
{
    const auto value = cydaw::base::SystemStats::getEnvironmentVariable(name, {}).trim().toLowerCase();
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

bool isUmuRunner(const cydaw::base::File& runner)
{
    return runner.getFileNameWithoutExtension().containsIgnoreCase("umu-run");
}

std::array<std::uint8_t, 4> binaryMagic(const cydaw::base::File& file)
{
    std::array<std::uint8_t, 4> magic {};
    if (auto input = file.createInputStream(); input != nullptr)
        (void) input->read(magic.data(), static_cast<int>(magic.size()));
    return magic;
}

} // namespace

bool WindowsPluginBridge::isWindowsVst3Bundle(const cydaw::base::File& pluginFile)
{
    if (!pluginFile.hasFileExtension("vst3"))
        return false;
    if (pluginFile.existsAsFile()) {
        const auto magic = binaryMagic(pluginFile);
        return magic[0] == 'M' && magic[1] == 'Z';
    }
    return architectureDirectory(pluginFile, "x86_64-win").isDirectory()
           || architectureDirectory(pluginFile, "x86-win").isDirectory()
           || architectureDirectory(pluginFile, "arm64-win").isDirectory();
}

bool WindowsPluginBridge::isLinuxVst3Bundle(const cydaw::base::File& pluginFile)
{
    if (!pluginFile.hasFileExtension("vst3"))
        return false;
    if (pluginFile.existsAsFile()) {
        const auto magic = binaryMagic(pluginFile);
        return magic[0] == 0x7f && magic[1] == 'E'
               && magic[2] == 'L' && magic[3] == 'F';
    }
    return architectureDirectory(pluginFile, "x86_64-linux").isDirectory()
               || architectureDirectory(pluginFile, "i386-linux").isDirectory()
               || architectureDirectory(pluginFile, "aarch64-linux").isDirectory();
}

bool WindowsPluginBridge::isWindowsVst2Binary(const cydaw::base::File& pluginFile)
{
    if (!pluginFile.existsAsFile() || !pluginFile.hasFileExtension("dll"))
        return false;
    const auto magic = binaryMagic(pluginFile);
    return magic[0] == 'M' && magic[1] == 'Z';
}

bool WindowsPluginBridge::isLinuxVst2Binary(const cydaw::base::File& pluginFile)
{
    if (!pluginFile.existsAsFile() || !pluginFile.hasFileExtension("so"))
        return false;
    const auto magic = binaryMagic(pluginFile);
    return magic[0] == 0x7f && magic[1] == 'E'
           && magic[2] == 'L' && magic[3] == 'F';
}

cydaw::base::Text WindowsPluginBridge::pathForWindowsGuest(const cydaw::base::File& file)
{
    return pathForWindowsGuest(file.getFullPathName());
}

cydaw::base::Text WindowsPluginBridge::pathForWindowsGuest(cydaw::base::Text hostPath)
{
    if (hostPath.length() >= 2 && hostPath[1] == ':')
        return hostPath.replaceCharacter('/', '\\');
    if (hostPath.startsWithChar('/'))
        return "Z:" + hostPath.replaceCharacter('/', '\\');
    return hostPath.replaceCharacter('/', '\\');
}

PluginWorkerLaunch WindowsPluginBridge::planWorkerLaunch(
    const bool windowsPlugin,
    const PluginWorkerToolchain& toolchain)
{
    if (!windowsPlugin) {
        if (!toolchain.nativeWorker.existsAsFile())
            return { PluginWorkerRuntime::unavailable, {},
                     "CyDAWPluginWorker was not found next to the application" };
        return { PluginWorkerRuntime::native,
                 { toolchain.nativeWorker.getFullPathName() }, {} };
    }

    if (!toolchain.windowsWorker.existsAsFile())
        return { PluginWorkerRuntime::unavailable, {},
                 "Windows plugin worker not found. Set CYDAW_WINDOWS_VST_WORKER or install it in windows-bridge/." };

    auto runner = toolchain.configuredRunner;
    auto runtime = PluginWorkerRuntime::wine;
    if (runner.existsAsFile()) {
        runtime = isUmuRunner(runner)
                      ? PluginWorkerRuntime::protonViaUmu
                      : PluginWorkerRuntime::wine;
    } else if (toolchain.preferUmu) {
        runner = toolchain.umuRunner;
        runtime = PluginWorkerRuntime::protonViaUmu;
        if (!runner.existsAsFile())
            return { PluginWorkerRuntime::unavailable, {},
                     "UMU/Proton was requested, but umu-run was not found" };
    } else {
        runner = toolchain.wineRunner;
        runtime = PluginWorkerRuntime::wine;
    }

    if (!runner.existsAsFile())
        return { PluginWorkerRuntime::unavailable, {},
                 "No Wine/UMU runner found. Install Wine, or set CYDAW_WINDOWS_VST_RUNNER." };

    return { runtime,
             { runner.getFullPathName(), toolchain.windowsWorker.getFullPathName() }, {} };
}

PluginWorkerLaunch WindowsPluginBridge::workerLaunchFor(const cydaw::base::File& pluginFile)
{
#if JUCE_LINUX
    const auto windowsVst3 = isWindowsVst3Bundle(pluginFile)
                             && !isLinuxVst3Bundle(pluginFile);
    const auto windowsVst2 = isWindowsVst2Binary(pluginFile)
                             && !isLinuxVst2Binary(pluginFile);
    return planWorkerLaunch(windowsVst3 || windowsVst2, discoverToolchain());
#else
    juce::ignoreUnused(pluginFile);
#endif
    return planWorkerLaunch(false, discoverToolchain());
}

WindowsPluginBridgeStatus WindowsPluginBridge::inspect()
{
    WindowsPluginBridgeStatus status;
#if JUCE_LINUX
    status.linuxHost = true;
#endif
    const auto toolchain = discoverToolchain();
    status.nativeWorker = toolchain.nativeWorker;
    status.windowsWorker = toolchain.windowsWorker;
    status.yabridgeControl = findExecutable("yabridgectl");
    status.wineRunner = toolchain.wineRunner;
    status.umuRunner = toolchain.umuRunner;
    const auto directLaunch = planWorkerLaunch(true, toolchain);
    status.directRuntime = directLaunch.runtime;
    if (directLaunch.isValid())
        status.selectedDirectRunner = cydaw::base::File(directLaunch.command[0]);
    return status;
}

PluginWorkerToolchain WindowsPluginBridge::discoverToolchain()
{
    PluginWorkerToolchain toolchain;
    toolchain.nativeWorker = nativeWorkerFile();
    toolchain.windowsWorker = windowsWorkerFile();
    toolchain.configuredRunner = findExecutable(
        cydaw::base::SystemStats::getEnvironmentVariable("CYDAW_WINDOWS_VST_RUNNER", {}).trim());
    toolchain.wineRunner = findExecutable(
        cydaw::base::SystemStats::getEnvironmentVariable("WINELOADER", {}).trim());
    if (!toolchain.wineRunner.existsAsFile())
        toolchain.wineRunner = findExecutable("wine64");
    if (!toolchain.wineRunner.existsAsFile())
        toolchain.wineRunner = findExecutable("wine");
    toolchain.umuRunner = findExecutable("umu-run");
    toolchain.preferUmu = environmentFlag("CYDAW_WINDOWS_VST_USE_UMU");
    return toolchain;
}

cydaw::base::File WindowsPluginBridge::findExecutable(const cydaw::base::Text& nameOrPath)
{
    if (nameOrPath.isEmpty())
        return {};
    const cydaw::base::File direct(nameOrPath);
    if (direct.existsAsFile())
        return direct;

    const auto path = cydaw::base::SystemStats::getEnvironmentVariable("PATH", {});
#if JUCE_WINDOWS
    const auto separator = ";";
#else
    const auto separator = ":";
#endif
    const auto folders = cydaw::base::TextList::fromTokens(path, separator, {});
    for (const auto& folder : folders) {
        const auto candidate = cydaw::base::File(folder).getChildFile(nameOrPath);
        if (candidate.existsAsFile())
            return candidate;
    }
    return {};
}

cydaw::base::File WindowsPluginBridge::nativeWorkerFile()
{
    return cydaw::base::File::getSpecialLocation(cydaw::base::File::currentExecutableFile)
        .getSiblingFile(
#if JUCE_WINDOWS
            "CyDAWPluginWorker.exe"
#else
            "CyDAWPluginWorker"
#endif
        );
}

cydaw::base::File WindowsPluginBridge::windowsWorkerFile()
{
    const auto configured = environmentFile("CYDAW_WINDOWS_VST_WORKER");
    if (configured.existsAsFile())
        return configured;
    return cydaw::base::File::getSpecialLocation(cydaw::base::File::currentExecutableFile)
        .getSiblingFile("windows-bridge")
        .getChildFile("CyDAWPluginWorker.exe");
}

} // namespace cydaw::app
