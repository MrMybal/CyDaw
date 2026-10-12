#include "WorkspaceComponent.h"
#include "Localization.h"
#include "MessageThreadSampler.h"
#include "RemoteControlServer.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace cydaw::app {

class CyDawApplication final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "CyDAW"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String& commandLine) override
    {
        const auto selfTest = commandLine.contains("--layout-self-test")
                              || commandLine.contains("--project-graph-self-test")
                              || commandLine.contains("--master-export-self-test")
                              || commandLine.contains("--project-recovery-self-test")
                              || commandLine.contains("--audio-recording-self-test")
                              || commandLine.contains("--ui-snapshot=")
                              || commandLine.contains("--ui-hitch=")
                              || commandLine.contains("--visual-test")
                              || commandLine.contains("--perf-self-test")
                              || commandLine.contains("--button-audit")
                              || commandLine.contains("--demo-project=")
                              || commandLine.contains("--perf-project=")
                              || commandLine.contains("--template=")
                              || commandLine.contains("--smoke-test");
        // A self-test runs as a GUI process, so it has no console to write to
        // and a failure would otherwise surface only as a non-zero exit code.
        // Route the diagnostics the tests emit into a file the runner can read.
        if (selfTest) {
            selfTestLog_ = std::make_unique<juce::FileLogger>(
                juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("cydaw-self-test.log"),
                "CyDAW self test " + getApplicationVersion());
            juce::Logger::setCurrentLogger(selfTestLog_.get());
        }
        window_ = std::make_unique<MainWindow>(
            getApplicationName(), commandLine.contains("--show-node-graph"),
            !selfTest);
        if (commandLine.contains("--layout-self-test")) {
            juce::Timer::callAfterDelay(500, [this] {
                const auto passed = window_ != nullptr && window_->runDockingSelfTest();
                setApplicationReturnValue(passed ? 0 : 1);
                systemRequestedQuit();
            });
            return;
        }
        if (commandLine.contains("--button-audit")) {
            juce::Timer::callAfterDelay(900, [this] {
                const auto report = window_ != nullptr
                    ? window_->auditInteractiveControls()
                    : juce::String("no window");
                (void) juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("cydaw-buttons.txt")
                    .replaceWithText(report);
                setApplicationReturnValue(0);
                systemRequestedQuit();
            });
            return;
        }
        if (commandLine.contains("--demo-project=")) {
            juce::Timer::callAfterDelay(900, [this, commandLine] {
                juce::StringArray arguments;
                arguments.addTokens(commandLine, true);
                juce::String path;
                for (const auto& argument : arguments)
                    if (argument.startsWith("--demo-project="))
                        path = argument.fromFirstOccurrenceOf("=", false, false)
                                   .unquoted();
                const auto report = window_ != nullptr
                    ? window_->buildDemoProject(juce::File(path))
                    : juce::String("failed: no window");
                juce::Logger::writeToLog(report);
                setApplicationReturnValue(report.startsWith("ok") ? 0 : 1);
                systemRequestedQuit();
            });
            return;
        }
        if (commandLine.contains("--template=")) {
            // --template=<name>:<path> writes one of the small ready-to-play
            // projects to a file; the templates folder next to the
            // executable is what the File menu lists.
            juce::Timer::callAfterDelay(900, [this, commandLine] {
                juce::StringArray arguments;
                arguments.addTokens(commandLine, true);
                juce::String spec;
                for (const auto& argument : arguments)
                    if (argument.startsWith("--template="))
                        spec = argument.fromFirstOccurrenceOf("=", false, false)
                                   .unquoted();
                const auto name = spec.upToFirstOccurrenceOf(":", false, false);
                const auto path = spec.fromFirstOccurrenceOf(":", false, false);
                const auto report = window_ != nullptr
                    ? window_->buildTemplate(name, juce::File(path))
                    : juce::String("failed: no window");
                juce::Logger::writeToLog(report);
                setApplicationReturnValue(report.startsWith("ok") ? 0 : 1);
                systemRequestedQuit();
            });
            return;
        }
        if (commandLine.contains("--perf-project=")) {
            // --perf-project=<path> writes the bench's session to a file,
            // for the hitch probe to open.
            juce::Timer::callAfterDelay(900, [this, commandLine] {
                juce::StringArray arguments;
                arguments.addTokens(commandLine, true);
                juce::String path;
                for (const auto& argument : arguments)
                    if (argument.startsWith("--perf-project="))
                        path = argument.fromFirstOccurrenceOf("=", false, false)
                                   .unquoted();
                const auto report = window_ != nullptr
                    ? window_->buildPerfProject(juce::File(path))
                    : juce::String("failed: no window");
                juce::Logger::writeToLog(report);
                setApplicationReturnValue(report.startsWith("ok") ? 0 : 1);
                systemRequestedQuit();
            });
            return;
        }
        if (commandLine.contains("--perf-self-test")) {
            juce::Timer::callAfterDelay(700, [this] {
                const auto report = window_ != nullptr
                    ? window_->runPerformanceSelfTest()
                    : juce::String("no window");
                juce::Logger::writeToLog("PERF " + report);
                (void) juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("cydaw-perf.txt")
                    .replaceWithText(report);
                setApplicationReturnValue(0);
                systemRequestedQuit();
            });
            return;
        }
        if (commandLine.contains("--project-graph-self-test")) {
            juce::Timer::callAfterDelay(500, [this] {
                const auto passed = window_ != nullptr
                                    && window_->runProjectGraphSelfTest();
                setApplicationReturnValue(passed ? 0 : 1);
                systemRequestedQuit();
            });
            return;
        }
        if (commandLine.contains("--master-export-self-test")) {
            juce::Timer::callAfterDelay(500, [this] {
                const auto passed = window_ != nullptr
                                    && window_->runMasterExportSelfTest();
                setApplicationReturnValue(passed ? 0 : 1);
                systemRequestedQuit();
            });
            return;
        }
        if (commandLine.contains("--project-recovery-self-test")) {
            juce::Timer::callAfterDelay(500, [this] {
                const auto passed = window_ != nullptr
                                    && window_->runProjectRecoverySelfTest();
                setApplicationReturnValue(passed ? 0 : 1);
                systemRequestedQuit();
            });
            return;
        }
        if (commandLine.contains("--audio-recording-self-test")) {
            juce::Timer::callAfterDelay(500, [this] {
                const auto passed = window_ != nullptr
                                    && window_->runAudioRecordingSelfTest();
                setApplicationReturnValue(passed ? 0 : 1);
                systemRequestedQuit();
            });
            return;
        }
        if (commandLine.contains("--ui-hitch=")) {
            // --ui-hitch=seconds opens the project, shows --view=N, starts
            // the transport with --play, then samples the message thread
            // for that long and logs the gaps. What playback feels like.
            // The load itself is one long call with no beat, so the sampler
            // profiles it on its own: how long opening the project takes,
            // and where.
            {
                MessageThreadSampler loadSampler;
                loadSampler.start(2.0);
                const auto loadStart = juce::Time::getMillisecondCounterHiRes();
                openProjectFromCommandLine(commandLine);
                const auto loadMs = juce::Time::getMillisecondCounterHiRes()
                                    - loadStart;
                juce::Logger::writeToLog("project load: ms="
                                         + juce::String(loadMs, 0) + " "
                                         + loadSampler.stop());
            }
            auto viewMode = 0;
            auto windowWidth = 0;
            auto windowHeight = 0;
            auto seconds = 8.0;
            juce::StringArray arguments;
            arguments.addTokens(commandLine, true);
            arguments.removeEmptyStrings();
            for (const auto& argument : arguments) {
                if (argument.startsWith("--view="))
                    viewMode = argument.fromFirstOccurrenceOf("=", false, false)
                                   .getIntValue();
                else if (argument.startsWith("--ui-hitch="))
                    seconds = std::max(1.0, argument.fromFirstOccurrenceOf(
                                                        "=", false, false)
                                                .getDoubleValue());
                else if (argument.startsWith("--window-size=")) {
                    const auto size = argument.fromFirstOccurrenceOf(
                        "=", false, false);
                    windowWidth = size.upToFirstOccurrenceOf("x", false, true)
                                      .getIntValue();
                    windowHeight = size.fromFirstOccurrenceOf("x", false, true)
                                       .getIntValue();
                }
            }
            if (windowWidth > 0 && windowHeight > 0 && window_ != nullptr)
                window_->centreWithSize(windowWidth, windowHeight);
            const auto play = commandLine.contains("--play");
            // --cycle-views=2,4,7 switches to each of those views in turn,
            // a second and a half apart, once the probe runs: what a change
            // of view costs, and where.
            juce::Array<int> cycle;
            for (const auto& argument : arguments)
                if (argument.startsWith("--cycle-views=")) {
                    juce::StringArray views;
                    views.addTokens(argument.fromFirstOccurrenceOf("=", false,
                                                                  false),
                                    ",", "");
                    for (const auto& view : views)
                        if (view.getIntValue() > 0)
                            cycle.add(view.getIntValue());
                }
            // --actions=zoom-in,zoom-in,zoom-out,pan performs those, by
            // name, four hundred milliseconds apart once the probe runs:
            // what an interaction costs, and where.
            juce::StringArray actions;
            for (const auto& argument : arguments)
                if (argument.startsWith("--actions="))
                    actions.addTokens(argument.fromFirstOccurrenceOf("=", false,
                                                                    false),
                                      ",", "");
            actions.removeEmptyStrings();
            juce::Timer::callAfterDelay(1'500, [this, viewMode, play, seconds,
                                                cycle, actions] {
                if (window_ != nullptr && viewMode > 0)
                    window_->showEditorMode(viewMode);
                if (window_ != nullptr && play)
                    window_->startPlayback();
                if (window_ == nullptr) {
                    setApplicationReturnValue(1);
                    systemRequestedQuit();
                    return;
                }
                window_->startUiHitchProbe(seconds, [this](juce::String report) {
                    juce::Logger::writeToLog("ui hitch: " + report);
                    setApplicationReturnValue(0);
                    systemRequestedQuit();
                });
                for (auto index = 0; index < cycle.size(); ++index)
                    juce::Timer::callAfterDelay(
                        1'500 * (index + 1), [this, mode = cycle[index]] {
                            if (window_ != nullptr)
                                window_->showEditorMode(mode);
                        });
                for (auto index = 0; index < actions.size(); ++index)
                    juce::Timer::callAfterDelay(
                        1'000 + 400 * index, [this, action = actions[index]] {
                            if (window_ != nullptr)
                                window_->performProbeAction(action);
                        });
            });
            return;
        }
        if (commandLine.contains("--ui-snapshot=")) {
            openProjectFromCommandLine(commandLine);
            juce::String snapshotPath;
            juce::String language;
            auto viewMode = 0;
            auto browserCategory = 0;
            auto windowWidth = 0;
            auto windowHeight = 0;
            juce::StringArray arguments;
            arguments.addTokens(commandLine, true);
            arguments.removeEmptyStrings();
            for (const auto& argument : arguments) {
                if (argument.startsWith("--ui-snapshot="))
                    snapshotPath = argument.fromFirstOccurrenceOf(
                        "=", false, false).unquoted();
                // --view=N captures that editor mode instead of the default.
                else if (argument.startsWith("--view="))
                    viewMode = argument.fromFirstOccurrenceOf("=", false, false)
                                   .getIntValue();
                // --browser-category=N selects a library filter first.
                else if (argument.startsWith("--browser-category="))
                    browserCategory = argument
                                          .fromFirstOccurrenceOf("=", false, false)
                                          .getIntValue();
                // --window-size=WxH captures a specific window size, which is
                // what decides which toolbar groups are on screen.
                else if (argument.startsWith("--window-size=")) {
                    const auto size = argument.fromFirstOccurrenceOf(
                        "=", false, false);
                    windowWidth = size.upToFirstOccurrenceOf("x", false, true)
                                      .getIntValue();
                    windowHeight = size.fromFirstOccurrenceOf("x", false, true)
                                       .getIntValue();
                } else if (argument.startsWith("--language="))
                    language = argument.fromFirstOccurrenceOf("=", false, false);
            }
            if (windowWidth > 0 && windowHeight > 0 && window_ != nullptr)
                window_->centreWithSize(windowWidth, windowHeight);
            if (language.isNotEmpty())
                (void) Localization::instance().setLanguage(language);
            // --play starts the transport once the project is up, and
            // --snapshot-delay=ms leaves it running that long before the
            // capture: the only way to see what the analysers draw is to
            // give them something to analyse.
            const auto play = commandLine.contains("--play");
            auto settle = 0;
            // --frame-node=<type> frames that card of the graph right before
            // the capture, once the editor has its final size.
            juce::String frameNode;
            for (const auto& argument : arguments) {
                if (argument.startsWith("--snapshot-delay="))
                    settle = argument.fromFirstOccurrenceOf("=", false, false)
                                 .getIntValue();
                else if (argument.startsWith("--frame-node="))
                    frameNode = argument.fromFirstOccurrenceOf("=", false, false)
                                    .unquoted();
            }
            juce::Timer::callAfterDelay(1'500, [this, snapshotPath, viewMode,
                                                browserCategory, play, settle,
                                                frameNode] {
                if (window_ != nullptr && viewMode > 0)
                    window_->showEditorMode(viewMode);
                if (window_ != nullptr && browserCategory > 0)
                    window_->showBrowserCategory(browserCategory);
                if (window_ != nullptr && play) {
                    window_->silenceOutput();
                    window_->startPlayback();
                }
                juce::Timer::callAfterDelay(std::max(0, settle), [this,
                                                                   snapshotPath,
                                                                   frameNode] {
                    if (window_ != nullptr && frameNode.isNotEmpty())
                        window_->frameGraphNode(frameNode);
                    const auto passed = window_ != nullptr
                                        && snapshotPath.isNotEmpty()
                                        && window_->saveVisualSnapshot(
                                            juce::File(snapshotPath));
                    setApplicationReturnValue(passed ? 0 : 1);
                    systemRequestedQuit();
                });
            });
            return;
        }
        if (commandLine.contains("--smoke-test")) {
            juce::Timer::callAfterDelay(1'500, [] {
                if (auto* application = juce::JUCEApplication::getInstance())
                    application->systemRequestedQuit();
            });
        }
        openProjectFromCommandLine(commandLine);
        startRemoteControl(commandLine);
    }

    // The control port an agent drives the application through (see
    // tools/mcp): 127.0.0.1:47820 unless --control-port=N says otherwise,
    // and none at all for --control-port=0.
    void startRemoteControl(const juce::String& commandLine)
    {
        auto port = 47'820;
        juce::StringArray arguments;
        arguments.addTokens(commandLine, true);
        for (const auto& argument : arguments)
            if (argument.startsWith("--control-port="))
                port = argument.fromFirstOccurrenceOf("=", false, false)
                           .getIntValue();
        if (port <= 0)
            return;
        remoteControl_ = std::make_unique<RemoteControlServer>(
            port,
            [this](const juce::var& request) {
                if (window_ == nullptr) {
                    auto* object = new juce::DynamicObject();
                    object->setProperty("ok", false);
                    object->setProperty("error", "no window");
                    return juce::var(object);
                }
                return window_->handleRemoteCommand(request);
            },
            juce::SystemStats::getEnvironmentVariable("CYDAW_CONTROL_TOKEN", {}));
    }

    void anotherInstanceStarted(const juce::String& commandLine) override
    {
        openProjectFromCommandLine(commandLine);
        if (window_ != nullptr)
            window_->toFront(true);
    }

    void openProjectFromCommandLine(const juce::String& commandLine)
    {
        juce::StringArray arguments;
        arguments.addTokens(commandLine, true);
        arguments.removeEmptyStrings();
        for (const auto& argument : arguments) {
            const auto project = juce::File(argument.unquoted());
            if (project.existsAsFile()
                && project.hasFileExtension("cydaw")) {
                window_->openProject(project);
                break;
            }
        }
    }

    void shutdown() override
    {
        remoteControl_.reset();
        window_.reset();
        juce::Logger::setCurrentLogger(nullptr);
        selfTestLog_.reset();
    }

    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow final : public juce::DocumentWindow {
    public:
        MainWindow(const juce::String& title, const bool showNodeGraph,
                   const bool recoveryEnabled)
            : DocumentWindow(title, juce::Colour(0xff111318), allButtons)
        {
            setUsingNativeTitleBar(true);
            workspace_ = new WorkspaceComponent(recoveryEnabled);
            setContentOwned(workspace_, true);
            setResizable(true, false);
            setResizeLimits(1200, 640, 7680, 4320);
            centreWithSize(1440, 900);
            setVisible(true);
            if (showNodeGraph)
                workspace_->showNodeGraphWindow();
        }

        [[nodiscard]] juce::String auditInteractiveControls()
        {
            return workspace_ != nullptr
                ? workspace_->auditInteractiveControls()
                : juce::String("no workspace");
        }

        [[nodiscard]] juce::String runPerformanceSelfTest()
        {
            return workspace_ != nullptr
                ? workspace_->runPerformanceSelfTest()
                : juce::String("no workspace");
        }

        [[nodiscard]] bool runDockingSelfTest()
        {
            return workspace_ != nullptr && workspace_->runDockingSelfTest();
        }

        [[nodiscard]] bool runProjectGraphSelfTest()
        {
            return workspace_ != nullptr && workspace_->runProjectGraphSelfTest();
        }

        [[nodiscard]] bool runMasterExportSelfTest()
        {
            return workspace_ != nullptr && workspace_->runMasterExportSelfTest();
        }

        [[nodiscard]] bool runProjectRecoverySelfTest()
        {
            return workspace_ != nullptr
                   && workspace_->runProjectRecoverySelfTest();
        }

        [[nodiscard]] bool runAudioRecordingSelfTest()
        {
            return workspace_ != nullptr
                   && workspace_->runAudioRecordingSelfTest();
        }

        [[nodiscard]] bool saveVisualSnapshot(
            const juce::File& destination)
        {
            return workspace_ != nullptr
                   && workspace_->saveVisualSnapshot(destination);
        }

        void showEditorMode(const int mode)
        {
            if (workspace_ != nullptr)
                workspace_->showEditorMode(mode);
        }

        void showBrowserCategory(const int category)
        {
            if (workspace_ != nullptr)
                workspace_->showBrowserCategory(category);
        }

        juce::String buildDemoProject(const juce::File& file)
        {
            return workspace_ != nullptr
                       ? workspace_->buildInstrumentFilterDemo(file)
                       : juce::String("failed: no workspace");
        }

        juce::String buildTemplate(const juce::String& name,
                                   const juce::File& file)
        {
            return workspace_ != nullptr
                       ? workspace_->buildTemplateProject(name, file)
                       : juce::String("failed: no workspace");
        }

        juce::var handleRemoteCommand(const juce::var& request)
        {
            if (workspace_ == nullptr) {
                auto* object = new juce::DynamicObject();
                object->setProperty("ok", false);
                object->setProperty("error", "no workspace");
                return juce::var(object);
            }
            return workspace_->handleRemoteCommand(request);
        }

        juce::String buildPerfProject(const juce::File& file)
        {
            return workspace_ != nullptr
                       ? workspace_->buildPerformanceProject(file)
                       : juce::String("failed: no workspace");
        }

        void openProject(const juce::File& file)
        {
            if (workspace_ != nullptr)
                workspace_->openProjectFile(file);
        }

        void startPlayback()
        {
            if (workspace_ != nullptr)
                workspace_->startPlayback();
        }

        void silenceOutput()
        {
            if (workspace_ != nullptr)
                workspace_->silenceOutput();
        }

        void frameGraphNode(const juce::String& nodeTypeKey)
        {
            if (workspace_ != nullptr)
                workspace_->frameGraphNode(nodeTypeKey);
        }

        void performProbeAction(const juce::String& action)
        {
            if (workspace_ != nullptr)
                workspace_->performProbeAction(action);
        }

        void startUiHitchProbe(const double seconds,
                               std::function<void(juce::String)> onReport)
        {
            if (workspace_ != nullptr)
                workspace_->startUiHitchProbe(seconds, std::move(onReport));
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        WorkspaceComponent* workspace_ {};
    };

    std::unique_ptr<MainWindow> window_;
    std::unique_ptr<RemoteControlServer> remoteControl_;
    std::unique_ptr<juce::FileLogger> selfTestLog_;
};

} // namespace cydaw::app

START_JUCE_APPLICATION(cydaw::app::CyDawApplication)
