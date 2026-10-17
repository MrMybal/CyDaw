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

juce::File executableOnPath(const juce::String& name)
{
    for(const auto& folder:juce::StringArray::fromTokens(juce::SystemStats::getEnvironmentVariable("PATH",{}),";","\""))
        if(folder.trim().isNotEmpty())
            if(auto file=juce::File(folder.trim()).getChildFile(name);file.existsAsFile()&&!file.getFullPathName().containsIgnoreCase("WindowsApps"))return file;
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

} // namespace

bool Vst3Build::isSourceRoot(const juce::File& folder)
{
    const auto lists = folder.getChildFile("CMakeLists.txt");
    return lists.existsAsFile() && lists.getSize() < 2 * 1024 * 1024
           && folder.getChildFile("src/native_plugin/NativePlugin.cmake").existsAsFile();
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
    tools.ninja=executableOnPath("ninja.exe");
    if(!tools.ninja.existsAsFile()&&tools.cmake.existsAsFile()) {
        auto candidate=tools.cmake.getParentDirectory().getParentDirectory().getSiblingFile("Ninja").getChildFile("ninja.exe");
        if(candidate.existsAsFile())tools.ninja=candidate;
    }
    tools.python=executableOnPath("python.exe");
    if(!tools.python.existsAsFile()) {
        const auto local=juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getSiblingFile("Local").getChildFile("Programs/Python");
        for(const auto& folder:local.findChildFiles(juce::File::findDirectories,false,"Python*"))
            if(folder.getChildFile("python.exe").existsAsFile())tools.python=folder.getChildFile("python.exe");
    }
    const auto configured=juce::SystemStats::getEnvironmentVariable("CYDAW_PLUGIN_TOOLCHAINS",{});
    tools.linuxToolchain=(configured.isNotEmpty()?juce::File(configured):locations::folder(Location::cache).getChildFile("plugin-toolchains")).getChildFile("linux/linux-x86_64.cmake");
    if (tools.sourceRoot.isDirectory())
        tools.vst3Sdk = tools.sourceRoot.getChildFile("build/native-vst3-sdk");
    return tools;
}

juce::File Vst3Build::workFolder()
{
    return locations::folder(Location::cache).getChildFile("vst3-native");
}

Vst3Build::Vst3Build() : juce::Thread("CyDAW VST3 build") {}

Vst3Build::~Vst3Build()
{
    signalThreadShouldExit();
    stopThread(15'000);
}

bool Vst3Build::start(const juce::File& project, const juce::File& destination, const juce::String& productName,
                      const Tools& tools, const juce::String& format)
{
    return startBatch({Job{project,destination,productName,format,"Windows"}},tools);
}

bool Vst3Build::startBatch(const std::vector<Job>& jobs,const Tools& tools)
{
    if(running()||isThreadRunning()||!tools.ready()||jobs.empty())return false;
    for(const auto& job:jobs)
        if((job.format!="VST3"&&job.format!="CLAP"&&job.format!="LV2")||(job.platform!="Windows"&&job.platform!="Linux"))return false;
    {
        const std::scoped_lock lock(mutex_);
        jobs_=jobs;tools_=tools;results_.clear();log_.clear();bundle_=juce::File();failure_.clear();
        started_=juce::Time::getMillisecondCounterHiRes()*.001;finished_=0;
    }
    phase_=Phase::configuring;startThread(juce::Thread::Priority::low);return true;
}
std::vector<Vst3Build::JobResult> Vst3Build::results()const
{
    const std::scoped_lock lock(mutex_);return results_;
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
    (void)workFolder().getChildFile("build.log").appendText(line+"\n",false,false,"\n");
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
    const auto windows=juce::SystemStats::getEnvironmentVariable("SystemRoot","C:/Windows");
    const auto path=windows+"/System32;"+windows+";"+juce::SystemStats::getEnvironmentVariable("PATH",{});
    juce::StringArray clean {tools_.cmake.getFullPathName(), "-E", "env", "--unset=PATH", "--unset=Path", "Path="+path, "--"};
    // MSBuild uses the mixed-case Path property for post-build tools; an
    // uppercase PATH is replaced by an empty property in VC targets.
    // SDK post-build commands also contain paths relative to the build directory.
    if(command.size()>2 && command[1]=="--build")
        clean.addArray({command[0],"-E","chdir",command[2]});
    clean.addArray(command);
    if (!process.start(clean, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr)) {
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
    (void)workFolder().createDirectory();
    (void)workFolder().getChildFile("build.log").replaceWithText(juce::String());
    bool failed=false;
    for(const auto& job:jobs_) {
        if(threadShouldExit()){finish(Phase::cancelled);return;}
        phase_=Phase::configuring;
        addLine("=== "+job.platform+" / "+job.format+" ===");
        const auto success=runJob(job);
        {
            const std::scoped_lock lock(mutex_);
            results_.push_back({job.format,job.platform,success?bundle_:juce::File(),success?juce::String():failure_});
        }
        failed|=!success;
        if(threadShouldExit()){finish(Phase::cancelled);return;}
    }
    finish(failed?Phase::failed:Phase::done,failed?"Certaines cibles ont échoué ; voir les résultats et le journal.":juce::String());
}

bool Vst3Build::runJob(const Job& job)
{
    const auto jobFailed = [this](Phase, const juce::String& message) {
        const std::scoped_lock lock(mutex_);
        failure_=message;
        for(auto i=log_.size()-1;i>=0;--i)
            if(log_[i].containsIgnoreCase("error:")||log_[i].containsIgnoreCase("fatal error")||log_[i].contains("CMake Error")) {
                failure_ += "\n" + log_[i].trim();
                break;
            }
    };
    const auto tools=tools_;
    const auto project=job.project;
    const auto destination=job.destination;
    const auto productName=job.productName;
    const auto format=job.format;
    const auto linuxTarget=job.platform=="Linux";
    if(linuxTarget) {
        if(!tools.python.existsAsFile()||!tools.ninja.existsAsFile()) {
            jobFailed(Phase::failed,"Linux needs Python 3 and Ninja (included with Visual Studio CMake)");return false;
        }
        if(!tools.linuxToolchain.existsAsFile()) {
            if(!runStep({tools.python.getFullPathName(),tools.sourceRoot.getChildFile("tools/setup_plugin_toolchains.py").getFullPathName(),
                         "--output",tools.linuxToolchain.getParentDirectory().getFullPathName()})) {
                jobFailed(Phase::failed,"Could not prepare the local Linux toolchain");return false;
            }
        }
    }
    (void)workFolder().createDirectory();
    // One working folder for every export: the sources compiled once are
    // reused by the next device.
    const auto work = linuxTarget ? workFolder().getSiblingFile(format.toLowerCase()+"-linux-native") : format == "VST3" ? workFolder() : workFolder().getSiblingFile(format.toLowerCase() + "-native");
    const auto source = work.getChildFile("project");
    const auto output = work.getChildFile("build");
    if (!work.createDirectory() || (source.exists() && !source.deleteRecursively()) || !project.copyDirectoryTo(source)) {
        jobFailed(Phase::failed, "could not prepare " + source.getFullPathName());
        return false;
    }
    juce::StringArray configure { tools.cmake.getFullPathName(), "-S", source.getFullPathName(), "-B", output.getFullPathName(),
                                  "-DCYDAW_SOURCE_ROOT=" + tools.sourceRoot.getFullPathName().replaceCharacter('\\', '/'),
                                  "-DCYDAW_EXPORT_FORMAT=" + format,
                                  "-DCYDAW_BUILD_TESTS=OFF", "-DCYDAW_BUILD_GRAPH_EDITOR=OFF",
                                  "-DCYDAW_BUILD_GRAPH_RUNTIME_PLUGIN=OFF", "-DCYDAW_BUILD_V2=OFF", "-DCYDAW_ENABLE_LTO=OFF" };
    if(linuxTarget)configure.addArray({"-G","Ninja","-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_TOOLCHAIN_FILE="+tools.linuxToolchain.getFullPathName().replaceCharacter('\\','/'),
        "-DCMAKE_MAKE_PROGRAM="+tools.ninja.getFullPathName().replaceCharacter('\\','/'),
        "-DCYDAW_HOST_PYTHON="+tools.python.getFullPathName().replaceCharacter('\\','/')});
    else configure.addArray({"-G","Visual Studio 17 2022","-A","x64"});
    if (format == "VST3" && tools.vst3Sdk.getChildFile("CMakeLists.txt").existsAsFile())
        configure.add("-DCYDAW_VST3_SDK_DIR=" + tools.vst3Sdk.getFullPathName().replaceCharacter('\\', '/'));
    if (!runStep(configure)) {
        jobFailed(threadShouldExit() ? Phase::cancelled : Phase::failed, "CMake could not configure the project");
        return false;
    }
    phase_ = Phase::building;
    // The build folder is every export's: the bundles an earlier export left
    // there go first, so the one found next is this device's.
    const auto built = output.getChildFile(format).getChildFile("Release");
    const auto extension = "." + format.toLowerCase();
    const auto bundleType = format == "CLAP" ? juce::File::findFiles : juce::File::findDirectories;
    if(!(linuxTarget&&format=="LV2")) for (const auto& old : built.findChildFiles(bundleType, false, "*" + extension))
        (void) old.deleteRecursively();
    if (!runStep({ tools.cmake.getFullPathName(), "--build", output.getFullPathName(), "--config", "Release", "--target", "CyDAWGeneratedGraphPlugin_" + format,
                   "--parallel", "4" })) {
        jobFailed(threadShouldExit() ? Phase::cancelled : Phase::failed, "the build failed; the log says why");
        return false;
    }
    // The bundle, beside the project the person keeps: the one named after
    // the product, else the newest (a name the build wrote otherwise).
    auto bundles = built.findChildFiles(bundleType, false, "*" + extension);
    if (bundles.isEmpty()) {
        jobFailed(Phase::failed, "no " + extension + " in " + built.getFullPathName());
        return false;
    }
    auto made = bundles.getFirst();
    for (const auto& bundle : bundles)
        if (bundle.getFileNameWithoutExtension() == productName)
            made = bundle;
        else if (made.getFileNameWithoutExtension() != productName && bundle.getLastModificationTime() > made.getLastModificationTime())
            made = bundle;
    auto name = juce::File::createLegalFileName(productName).trim();
    if (name.isEmpty())
        name = made.getFileNameWithoutExtension();
    const auto target = destination.getChildFile(name + extension);
    if ((target.exists() && !target.deleteRecursively()) || !destination.createDirectory()
        || !(made.isDirectory() ? made.copyDirectoryTo(target) : made.copyFileTo(target))) {
        jobFailed(Phase::failed, "could not copy the plugin to " + target.getFullPathName());
        return false;
    }
    {
        const std::scoped_lock lock(mutex_);
        bundle_ = target;
    }
    if (format == "CLAP") {
        const auto licenses = made.getSiblingFile(made.getFileName() + ".Licenses");
        if (licenses.isDirectory() && !licenses.copyDirectoryTo(target.getSiblingFile(target.getFileName() + ".Licenses"))) {
            jobFailed(Phase::failed, "Cannot copy CLAP licenses"); return false;
        }
    }
    addLine(format + ": " + target.getFullPathName());
    return true;
}

} // namespace cydaw::v2
