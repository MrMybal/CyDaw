#include "WindowsPluginBridge.h"

#include <array>
#include <cstdint>

namespace cydaw::app {
namespace {

juce::File architectureDirectory(const juce::File& pluginFile,
                                 const juce::String& architecture)
{
    return pluginFile.getChildFile("Contents").getChildFile(architecture);
}

juce::File environmentFile(const juce::String& name)
{
    const auto value = juce::SystemStats::getEnvironmentVariable(name, {}).trim();
    return value.isNotEmpty() ? juce::File(value) : juce::File {};
}

bool environmentFlag(const juce::String& name)
{
    const auto value = juce::SystemStats::getEnvironmentVariable(name, {}).trim().toLowerCase();
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

bool isUmuRunner(const juce::File& runner)
{
    return runner.getFileNameWithoutExtension().containsIgnoreCase("umu-run");
}

std::array<std::uint8_t, 4> binaryMagic(const juce::File& file)
{
    std::array<std::uint8_t, 4> magic {};
    if (auto input = file.createInputStream(); input != nullptr)
        (void) input->read(magic.data(), static_cast<int>(magic.size()));
    return magic;
}

} // namespace

bool WindowsPluginBridge::isWindowsVst3Bundle(const juce::File& pluginFile)
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

bool WindowsPluginBridge::isLinuxVst3Bundle(const juce::File& pluginFile)
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

bool WindowsPluginBridge::isWindowsVst2Binary(const juce::File& pluginFile)
{
    if (!pluginFile.existsAsFile() || !pluginFile.hasFileExtension("dll"))
        return false;
    const auto magic = binaryMagic(pluginFile);
    return magic[0] == 'M' && magic[1] == 'Z';
}

bool WindowsPluginBridge::isLinuxVst2Binary(const juce::File& pluginFile)
{
    if (!pluginFile.existsAsFile() || !pluginFile.hasFileExtension("so"))
        return false;
    const auto magic = binaryMagic(pluginFile);
    return magic[0] == 0x7f && magic[1] == 'E'
           && magic[2] == 'L' && magic[3] == 'F';
}

juce::String WindowsPluginBridge::pathForWindowsGuest(const juce::File& file)
{
    return pathForWindowsGuest(file.getFullPathName());
}

juce::String WindowsPluginBridge::pathForWindowsGuest(juce::String hostPath)
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

PluginWorkerLaunch WindowsPluginBridge::workerLaunchFor(const juce::File& pluginFile)
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
        status.selectedDirectRunner = juce::File(directLaunch.command[0]);
    return status;
}

PluginWorkerToolchain WindowsPluginBridge::discoverToolchain()
{
    PluginWorkerToolchain toolchain;
    toolchain.nativeWorker = nativeWorkerFile();
    toolchain.windowsWorker = windowsWorkerFile();
    toolchain.configuredRunner = findExecutable(
        juce::SystemStats::getEnvironmentVariable("CYDAW_WINDOWS_VST_RUNNER", {}).trim());
    toolchain.wineRunner = findExecutable(
        juce::SystemStats::getEnvironmentVariable("WINELOADER", {}).trim());
    if (!toolchain.wineRunner.existsAsFile())
        toolchain.wineRunner = findExecutable("wine64");
    if (!toolchain.wineRunner.existsAsFile())
        toolchain.wineRunner = findExecutable("wine");
    toolchain.umuRunner = findExecutable("umu-run");
    toolchain.preferUmu = environmentFlag("CYDAW_WINDOWS_VST_USE_UMU");
    return toolchain;
}

juce::File WindowsPluginBridge::findExecutable(const juce::String& nameOrPath)
{
    if (nameOrPath.isEmpty())
        return {};
    const juce::File direct(nameOrPath);
    if (direct.existsAsFile())
        return direct;

    const auto path = juce::SystemStats::getEnvironmentVariable("PATH", {});
#if JUCE_WINDOWS
    const auto separator = ";";
#else
    const auto separator = ":";
#endif
    const auto folders = juce::StringArray::fromTokens(path, separator, {});
    for (const auto& folder : folders) {
        const auto candidate = juce::File(folder).getChildFile(nameOrPath);
        if (candidate.existsAsFile())
            return candidate;
    }
    return {};
}

juce::File WindowsPluginBridge::nativeWorkerFile()
{
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getSiblingFile(
#if JUCE_WINDOWS
            "CyDAWPluginWorker.exe"
#else
            "CyDAWPluginWorker"
#endif
        );
}

juce::File WindowsPluginBridge::windowsWorkerFile()
{
    const auto configured = environmentFile("CYDAW_WINDOWS_VST_WORKER");
    if (configured.existsAsFile())
        return configured;
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getSiblingFile("windows-bridge")
        .getChildFile("CyDAWPluginWorker.exe");
}

} // namespace cydaw::app
