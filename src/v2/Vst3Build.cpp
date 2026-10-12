#include "Vst3Build.h"

#include "Locations.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif

namespace cydaw::v2 {
namespace {

constexpr auto kTarget = "CyDAWGeneratedGraphPlugin_VST3";

// What the compiler writes: UTF-8, else the console's code page (Visual
// Studio's French messages come in CP850). Control characters other than
// tabs are dropped.
juce::String fromConsole(const char* data, const int size)
{
    juce::String text;
    if (juce::CharPointer_UTF8::isValidString(data, size)) {
        text = juce::String::fromUTF8(data, size);
    } else {
#if JUCE_WINDOWS
        const auto wide = MultiByteToWideChar(CP_OEMCP, 0, data, size, nullptr, 0);
        std::wstring converted(static_cast<std::size_t>(std::max(0, wide)), L'\0');
        if (wide > 0)
            MultiByteToWideChar(CP_OEMCP, 0, data, size, converted.data(), wide);
        text = juce::String(converted.c_str());
#else
        text = juce::String(juce::CharPointer_ASCII(data), static_cast<size_t>(size));
#endif
    }
    juce::String clean;
    for (const auto character : text)
        if (character >= 0x20 || character == '\t' || character == '\n')
            clean += juce::String::charToString(character);
    return clean;
}
constexpr auto kLogLines = 400;

juce::File cmakeOnPath()
{
    const auto path = juce::SystemStats::getEnvironmentVariable("PATH", {});
    for (const auto& folder : juce::StringArray::fromTokens(path, ";", "\""))
        if (const auto candidate = juce::File(folder.trim()).getChildFile("cmake.exe"); folder.isNotEmpty() && candidate.existsAsFile())
            return candidate;
    return {};
}

juce::File visualStudioCmake()
{
    for (const auto* root : { "C:\\Program Files\\Microsoft Visual Studio\\2022", "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022" })
        for (const auto* edition : { "Community", "Professional", "Enterprise", "BuildTools" }) {
            const auto candidate = juce::File(root).getChildFile(edition).getChildFile(
                "Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\CMake\\bin\\cmake.exe");
            if (candidate.existsAsFile())
                return candidate;
        }
    return {};
}

// JUCE as the CyDAW build that made this program found it.
juce::File juceBeside(const juce::File& sourceRoot)
{
    const auto builds = sourceRoot.getChildFile("build");
    if (!builds.isDirectory())
        return {};
    for (const auto& build : builds.findChildFiles(juce::File::findDirectories, false)) {
        const auto cache = build.getChildFile("CMakeCache.txt");
        if (cache.existsAsFile() && cache.getSize() < 4 * 1024 * 1024) {
            juce::StringArray lines;
            cache.readLines(lines);
            for (const auto& line : lines)
                if (line.startsWith("CYDAW_JUCE_SOURCE_DIR:PATH=")) {
                    const juce::File folder(line.fromFirstOccurrenceOf("=", false, false).trim());
                    if (line.fromFirstOccurrenceOf("=", false, false).trim().isNotEmpty()
                        && folder.getChildFile("CMakeLists.txt").existsAsFile())
                        return folder;
                }
        }
        if (const auto fetched = build.getChildFile("_deps").getChildFile("juce-src"); fetched.getChildFile("CMakeLists.txt").existsAsFile())
            return fetched;
    }
    return {};
}

} // namespace

bool Vst3Build::isSourceRoot(const juce::File& folder)
{
    const auto lists = folder.getChildFile("CMakeLists.txt");
    return lists.existsAsFile() && lists.getSize() < 2 * 1024 * 1024
           && lists.loadFileAsString().contains("cydaw_add_graph_runtime_plugin")
           && folder.getChildFile("src").getChildFile("graph_plugin").isDirectory();
}

Vst3Build::Tools Vst3Build::findTools(const juce::File& sourceRootHint)
{
    Tools tools;
    tools.cmake = cmakeOnPath();
    if (!tools.cmake.existsAsFile())
        tools.cmake = visualStudioCmake();
    if (sourceRootHint != juce::File() && isSourceRoot(sourceRootHint)) {
        tools.sourceRoot = sourceRootHint;
    } else if (const auto fromEnvironment = juce::SystemStats::getEnvironmentVariable("CYDAW_SOURCE_ROOT", {});
               fromEnvironment.isNotEmpty() && isSourceRoot(juce::File(fromEnvironment))) {
        tools.sourceRoot = juce::File(fromEnvironment);
    } else {
        // Run from a build folder of the sources: they are a few levels up.
        auto folder = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
        for (auto level = 0; level < 7 && folder.exists(); ++level, folder = folder.getParentDirectory()) {
            if (isSourceRoot(folder)) {
                tools.sourceRoot = folder;
                break;
            }
            if (folder.isRoot())
                break;
        }
    }
    if (tools.sourceRoot.isDirectory())
        tools.juceSource = juceBeside(tools.sourceRoot);
    return tools;
}

juce::File Vst3Build::workFolder()
{
    return locations::folder(Location::cache).getChildFile("vst3");
}

Vst3Build::Vst3Build() : juce::Thread("CyDAW VST3 build") {}

Vst3Build::~Vst3Build()
{
    signalThreadShouldExit();
    stopThread(15'000);
}

bool Vst3Build::start(const juce::File& project, const juce::File& destination, const juce::String& productName,
                      const Tools& tools)
{
    if (running() || isThreadRunning() || !tools.ready())
        return false;
    {
        const std::scoped_lock lock(mutex_);
        log_.clear();
        project_ = project;
        destination_ = destination;
        productName_ = productName;
        tools_ = tools;
        bundle_ = juce::File();
        failure_.clear();
        started_ = juce::Time::getMillisecondCounterHiRes() * 0.001;
        finished_ = 0.0;
    }
    phase_ = Phase::configuring;
    startThread(juce::Thread::Priority::low);
    return true;
}

void Vst3Build::cancel()
{
    signalThreadShouldExit();
}

juce::StringArray Vst3Build::log() const
{
    const std::scoped_lock lock(mutex_);
    return log_;
}

juce::File Vst3Build::bundle() const
{
    const std::scoped_lock lock(mutex_);
    return bundle_;
}

juce::String Vst3Build::failure() const
{
    const std::scoped_lock lock(mutex_);
    return failure_;
}

double Vst3Build::elapsedSeconds() const
{
    const std::scoped_lock lock(mutex_);
    const auto end = finished_ > 0.0 ? finished_ : juce::Time::getMillisecondCounterHiRes() * 0.001;
    return started_ > 0.0 ? end - started_ : 0.0;
}

void Vst3Build::addLine(const juce::String& line)
{
    const std::scoped_lock lock(mutex_);
    log_.add(line);
    if (log_.size() > kLogLines)
        log_.removeRange(0, log_.size() - kLogLines);
}

void Vst3Build::finish(const Phase phase, const juce::String& failure)
{
    {
        const std::scoped_lock lock(mutex_);
        failure_ = failure;
        finished_ = juce::Time::getMillisecondCounterHiRes() * 0.001;
    }
    phase_ = phase;
}

bool Vst3Build::runStep(const juce::StringArray& command)
{
    addLine("> " + command.joinIntoString(" "));
    juce::ChildProcess process;
    if (!process.start(command, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr)) {
        addLine("could not start " + command[0]);
        return false;
    }
    juce::String pending;
    char buffer[4096];
    for (;;) {
        if (threadShouldExit()) {
            process.kill();
            return false;
        }
        const auto read = process.readProcessOutput(buffer, sizeof(buffer));
        if (read > 0) {
            pending += fromConsole(buffer, read);
            for (auto newline = pending.indexOfChar('\n'); newline >= 0; newline = pending.indexOfChar('\n')) {
                addLine(pending.substring(0, newline).trimEnd());
                pending = pending.substring(newline + 1);
            }
            continue;
        }
        if (!process.isRunning())
            break;
        wait(50);
    }
    if (pending.trim().isNotEmpty())
        addLine(pending.trimEnd());
    const auto code = process.getExitCode();
    if (code != 0)
        addLine("exit code " + juce::String(static_cast<int>(code)));
    return code == 0;
}

void Vst3Build::run()
{
    Tools tools;
    juce::File project;
    juce::File destination;
    juce::String productName;
    {
        const std::scoped_lock lock(mutex_);
        tools = tools_;
        project = project_;
        destination = destination_;
        productName = productName_;
    }
    // One working folder for every export: the sources compiled once are
    // reused by the next device.
    const auto work = workFolder();
    const auto source = work.getChildFile("project");
    const auto output = work.getChildFile("build");
    if (!work.createDirectory() || (source.exists() && !source.deleteRecursively()) || !project.copyDirectoryTo(source)) {
        finish(Phase::failed, "could not prepare " + source.getFullPathName());
        return;
    }
    juce::StringArray configure { tools.cmake.getFullPathName(), "-S", source.getFullPathName(), "-B", output.getFullPathName(),
                                  "-G", "Visual Studio 17 2022", "-A", "x64",
                                  "-DCYDAW_SOURCE_ROOT=" + tools.sourceRoot.getFullPathName().replaceCharacter('\\', '/'),
                                  "-DCYDAW_BUILD_TESTS=OFF", "-DCYDAW_BUILD_GRAPH_EDITOR=OFF",
                                  "-DCYDAW_BUILD_GRAPH_RUNTIME_PLUGIN=OFF", "-DCYDAW_BUILD_V2=OFF", "-DCYDAW_ENABLE_LTO=OFF" };
    if (tools.juceSource.isDirectory())
        configure.add("-DCYDAW_JUCE_SOURCE_DIR=" + tools.juceSource.getFullPathName().replaceCharacter('\\', '/'));
    else
        configure.add("-DCYDAW_FETCH_JUCE=ON");
    if (!runStep(configure)) {
        finish(threadShouldExit() ? Phase::cancelled : Phase::failed, "CMake could not configure the project");
        return;
    }
    phase_ = Phase::building;
    if (!runStep({ tools.cmake.getFullPathName(), "--build", output.getFullPathName(), "--config", "Release", "--target", kTarget,
                   "--parallel" })) {
        finish(threadShouldExit() ? Phase::cancelled : Phase::failed, "the build failed; the log says why");
        return;
    }
    // The bundle, beside the project the person keeps.
    const auto built = output.getChildFile("CyDAWGeneratedGraphPlugin_artefacts").getChildFile("Release").getChildFile("VST3");
    const auto bundles = built.findChildFiles(juce::File::findDirectories, false, "*.vst3");
    if (bundles.isEmpty()) {
        finish(Phase::failed, "no .vst3 in " + built.getFullPathName());
        return;
    }
    auto name = juce::File::createLegalFileName(productName).trim();
    if (name.isEmpty())
        name = bundles.getFirst().getFileNameWithoutExtension();
    const auto target = destination.getChildFile(name + ".vst3");
    if ((target.exists() && !target.deleteRecursively()) || !destination.createDirectory()
        || !bundles.getFirst().copyDirectoryTo(target)) {
        finish(Phase::failed, "could not copy the plugin to " + target.getFullPathName());
        return;
    }
    {
        const std::scoped_lock lock(mutex_);
        bundle_ = target;
    }
    addLine("VST3: " + target.getFullPathName());
    finish(Phase::done);
}

} // namespace cydaw::v2
