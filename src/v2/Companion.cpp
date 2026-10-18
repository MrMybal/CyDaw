#include "Companion.h"

#include "Locations.h"

#include <juce_data_structures/juce_data_structures.h>

#include <optional>

namespace cydaw::v2 {

namespace {

std::unique_ptr<juce::PropertiesFile> openSettings()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "CyDAW";
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    options.folderName = "CyDAW";
    return std::make_unique<juce::PropertiesFile>(options);
}

juce::File environmentPython()
{
    const auto folder = Companion::environmentFolder();
    for (const auto& candidate : { folder.getChildFile("Scripts").getChildFile("python.exe"),
                                   folder.getChildFile("bin").getChildFile("python") })
        if (candidate.existsAsFile())
            return candidate;
    return {};
}

} // namespace

// One run of the companion: its output read line by line on its own thread.
class Companion::Job final : public juce::Thread {
public:
    Job(Companion& owner, juce::StringArray command, juce::String name)
        : juce::Thread("CyDAW companion"), owner_(owner), command_(std::move(command)), name_(std::move(name))
    {
    }

    ~Job() override
    {
        cancel();
        stopThread(4'000);
    }

    // Its read waits for output: the process stops, and with it the wait.
    void cancel()
    {
        signalThreadShouldExit();
        process_.kill();
    }

    void run() override
    {
        if (!process_.start(command_, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr)) {
            finish(false, {}, "Python could not be started: " + command_[0]);
            return;
        }
        juce::String pending;
        juce::String lastError;
        juce::var done;
        // A byte at a time: JUCE's read waits until the whole request is
        // filled, and a line of progress is short.
        juce::MemoryBlock bytes;
        while (!threadShouldExit()) {
            char byte {};
            if (process_.readProcessOutput(&byte, 1) <= 0) {
                if (!process_.isRunning())
                    break;
                juce::Thread::sleep(20);
                continue;
            }
            if (byte != '\n') {
                bytes.append(&byte, 1);
                continue;
            }
            pending += juce::String::fromUTF8(static_cast<const char*>(bytes.getData()), static_cast<int>(bytes.getSize())) + "\n";
            bytes.reset();
            for (auto end = pending.indexOfChar('\n'); end >= 0; end = pending.indexOfChar('\n')) {
                const auto line = pending.substring(0, end).trim();
                pending = pending.substring(end + 1);
                const auto value = juce::JSON::parse(line);
                if (!value.isObject()) {
                    // Python's own complaints (a missing module…) come as text.
                    if (line.isNotEmpty())
                        lastError = line;
                    continue;
                }
                if (value.hasProperty("progress")) {
                    const std::scoped_lock lock(owner_.mutex_);
                    owner_.status_.progress = static_cast<double>(value.getProperty("progress", 0.0));
                    owner_.status_.message = value.getProperty("message", {}).toString();
                    owner_.status_.log.add(owner_.status_.message);
                    if (owner_.status_.log.size() > 200)
                        owner_.status_.log.removeRange(0, owner_.status_.log.size() - 200);
                } else if (value.hasProperty("error")) {
                    lastError = value.getProperty("error", {}).toString();
                } else if (static_cast<bool>(value.getProperty("done", false))) {
                    done = value;
                }
            }
        }
        if (threadShouldExit()) {
            process_.kill();
            finish(false, {}, "cancelled");
            return;
        }
        const auto exitCode = process_.getExitCode();
        if (done.isObject() && exitCode == 0)
            finish(true, done, {});
        else
            finish(false, {}, lastError.isNotEmpty() ? lastError : "the companion stopped (" + juce::String(static_cast<int>(exitCode)) + ")");
    }

private:
    void finish(const bool ok, juce::var result, juce::String error)
    {
        const std::scoped_lock lock(owner_.mutex_);
        owner_.status_.running = false;
        owner_.status_.finished = true;
        owner_.status_.failed = !ok;
        owner_.status_.progress = ok ? 1.0 : owner_.status_.progress;
        owner_.status_.result = std::move(result);
        owner_.status_.error = std::move(error);
    }

    Companion& owner_;
    juce::StringArray command_;
    juce::String name_;
    juce::ChildProcess process_;
};

Companion::Companion()
{
    loadSettings();
}

Companion::~Companion()
{
    job_.reset();
}

void Companion::loadSettings()
{
    const auto settings = openSettings();
    python_ = settings->getValue("companionPython", {});
}

juce::String Companion::pythonCommand() const
{
    if (const auto other = juce::SystemStats::getEnvironmentVariable("CYDAW_COMPANION_PYTHON", {}); other.trim().isNotEmpty())
        return other.trim();
    if (python_.trim().isNotEmpty())
        return python_.trim();
    if (const auto own = environmentPython(); own.existsAsFile())
        return own.getFullPathName();
    return "python";
}

void Companion::setPythonCommand(const juce::String& command)
{
    python_ = command.trim();
    const auto settings = openSettings();
    settings->setValue("companionPython", python_);
    settings->saveIfNeeded();
}

juce::File Companion::packageFolder()
{
    const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    auto shipped = executable.getSiblingFile("tools").getChildFile("companion");
    if (shipped.getChildFile("cydaw_companion").isDirectory())
        return shipped;
    // A build: up from build/<dir>/CyDAW2_artefacts/Release to the sources.
    for (auto folder = executable.getParentDirectory(); folder.exists() && !folder.isRoot(); folder = folder.getParentDirectory())
        if (const auto source = folder.getChildFile("tools").getChildFile("companion");
            source.getChildFile("cydaw_companion").isDirectory())
            return source;
    return shipped;
}

juce::File Companion::environmentFolder()
{
    return locations::folder(Location::companion);
}

juce::var Companion::componentCatalogue()
{
    return juce::JSON::parse(packageFolder().getChildFile("cydaw_companion").getChildFile("components.json"));
}

juce::var Companion::component(const juce::String& id)
{
    // Held: the list lives as long as the catalogue does.
    const auto catalogue = componentCatalogue();
    const auto components = catalogue.getProperty("components", {});
    if (const auto* list = components.getArray())
        for (const auto& item : *list)
            if (item.getProperty("id", {}).toString() == id)
                return item;
    return {};
}

bool Companion::componentInstalled(const juce::String& id, const juce::var& info)
{
    if (!info.isObject())
        return false;
    const auto there = [&info](const char* key) {
        const auto value = info.getProperty(key, {});
        return !value.isVoid() && value.toString().isNotEmpty() && value.toString() != "null" && value.toString() != "false";
    };
    const auto torch = info.getProperty("torch", {}).toString();
    if (id == "base")
        return there("numpy");
    if (id == "torch-cpu")
        return there("torch") && !torch.contains("+cu");
    if (id == "torch-cuda")
        return there("torch") && torch.contains("+cu");
    if (id == "crepe")
        return static_cast<bool>(info.getProperty("crepe", false));
    return false;
}

bool Companion::install(const juce::String& id, const bool licencesAccepted, juce::String& error)
{
    const auto item = component(id);
    if (!item.isObject()) {
        error = "no component \"" + id + "\" (components.json)";
        return false;
    }
    if (static_cast<bool>(item.getProperty("requiresAcceptance", false)) && !licencesAccepted) {
        error = "its licences ask to be accepted first";
        return false;
    }
    const auto environment = environmentFolder();
    juce::StringArray arguments { "setup", "--env", environment.getFullPathName() };
    if (const auto* extra = item.getProperty("arguments", {}).getArray())
        for (const auto& argument : *extra)
            arguments.add(argument.toString());
    if (!start(arguments)) {
        error = "the companion is busy";
        return false;
    }
    // The receipt: what was asked for, from where, and the licences shown.
    auto* receipt = new juce::DynamicObject();
    receipt->setProperty("component", id);
    receipt->setProperty("when", juce::Time::getCurrentTime().toISO8601(true));
    receipt->setProperty("licencesAccepted", licencesAccepted);
    receipt->setProperty("sources", item.getProperty("sources", {}));
    receipt->setProperty("packages", item.getProperty("packages", {}));
    const auto file = environment.getChildFile("receipts").getChildFile(id + ".json");
    (void) file.getParentDirectory().createDirectory();
    (void) file.replaceWithText(juce::JSON::toString(juce::var(receipt)));
    return true;
}

juce::StringArray Companion::commandLine(const juce::StringArray& arguments) const
{
    // No working folder can be given to a child process here: the package is
    // put on the path by the command itself.
    auto command = juce::StringArray::fromTokens(pythonCommand(), " ", "\"");
    command.removeEmptyStrings();
    const auto folder = packageFolder().getFullPathName().replaceCharacter('\\', '/');
    command.add("-X");
    command.add("utf8");
    command.add("-c");
    command.add("import sys; sys.path.insert(0, r'" + folder + "'); from cydaw_companion.__main__ import main; sys.exit(main())");
    command.addArray(arguments);
    return command;
}

bool Companion::start(const juce::StringArray& arguments)
{
    if (status().running)
        return false;
    job_.reset();
    {
        const std::scoped_lock lock(mutex_);
        status_ = {};
        status_.running = true;
        status_.command = arguments.isEmpty() ? juce::String() : arguments[0];
        status_.message = "start";
    }
    job_ = std::make_unique<Job>(*this, commandLine(arguments), arguments.isEmpty() ? juce::String() : arguments[0]);
    job_->startThread();
    return true;
}

void Companion::cancel()
{
    if (job_ != nullptr)
        job_->cancel();
}

Companion::Status Companion::status() const
{
    const std::scoped_lock lock(mutex_);
    return status_;
}

std::optional<Companion::Status> Companion::takeFinished()
{
    const std::scoped_lock lock(mutex_);
    if (!status_.finished)
        return std::nullopt;
    auto finished = status_;
    status_.finished = false;
    return finished;
}

juce::var Companion::runNow(const juce::StringArray& arguments, juce::String& error, const int timeoutMs) const
{
    juce::ChildProcess process;
    const auto command = commandLine(arguments);
    if (!process.start(command, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr)) {
        error = "Python could not be started: " + command[0];
        return {};
    }
    const auto output = process.readAllProcessOutput();
    process.waitForProcessToFinish(timeoutMs);
    juce::StringArray lines;
    lines.addLines(output);
    juce::var last;
    for (const auto& line : lines) {
        const auto value = juce::JSON::parse(line.trim());
        if (value.isObject())
            last = value;
    }
    if (!last.isObject()) {
        error = lines.isEmpty() ? juce::String("no answer from ") + command[0] : lines[lines.size() - 1];
        return {};
    }
    if (last.hasProperty("error"))
        error = last.getProperty("error", {}).toString();
    return last;
}

} // namespace cydaw::v2
