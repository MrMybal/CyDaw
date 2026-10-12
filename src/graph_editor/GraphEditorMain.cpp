#include "CyDawLookAndFeel.h"
#include "GraphPolySynthRenderer.h"
#include "Localization.h"
#include "NativeDspRuntime.h"
#include "NodeGraphComponent.h"

#include <cydaw/realtime/SpscRing.h>

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace cydaw::app {

class GraphPreviewEngine final : private juce::AudioIODeviceCallback {
public:
    explicit GraphPreviewEngine(const bool initialiseAudioDevices = true)
    {
        if (!initialiseAudioDevices) {
            constexpr auto offlineSampleRate = 48'000.0;
            constexpr auto offlineBlockSize = 512;
            runtime_.prepare(offlineSampleRate, offlineBlockSize, 2);
            instrumentRenderer_.prepare(offlineSampleRate);
            midiCollector_.reset(offlineSampleRate);
            return;
        }
        initialiseError_ = deviceManager_.initialise(2, 2, nullptr, true);
        if (initialiseError_.isNotEmpty()) {
            deviceManager_.closeAudioDevice();
            initialiseError_ = deviceManager_.initialise(0, 2, nullptr, true);
        }
        deviceManager_.addAudioCallback(this);
        deviceManager_.addMidiInputDeviceCallback({}, &midiCollector_);
        audioCallbacksAttached_ = true;
    }

    ~GraphPreviewEngine() override
    {
        if (audioCallbacksAttached_) {
            deviceManager_.removeMidiInputDeviceCallback({}, &midiCollector_);
            deviceManager_.removeAudioCallback(this);
        }
        instrumentRenderer_.release();
        deviceManager_.closeAudioDevice();
    }

    [[nodiscard]] juce::AudioDeviceManager& deviceManager() noexcept
    {
        return deviceManager_;
    }

    [[nodiscard]] juce::MidiKeyboardState& keyboardState() noexcept
    {
        return keyboardState_;
    }

    [[nodiscard]] juce::String initialiseError() const
    {
        return initialiseError_;
    }

    [[nodiscard]] juce::Result publishGraph(
        const NodeGraphComponent::RuntimeGraphExport& graph,
        const bool topologyChanged)
    {
        controlTelemetryTargets_ = graph.controlTelemetryTargets;
        instrumentRenderer_.setPlan(graph.instrumentPlan);
        instrumentEnabled_.store(graph.instrumentPlan.enabled,
                                 std::memory_order_release);

        if (graph.compilationResult.failed()) {
            runtimeEnabled_.store(false, std::memory_order_release);
            return graph.compilationResult;
        }

        auto result = topologyChanged
                          ? runtime_.setGraph(graph.nativeGraph)
                          : runtime_.updateParameters(graph.nativeGraph);
        if (result.failed() && !topologyChanged)
            result = runtime_.setGraph(graph.nativeGraph);
        runtimeEnabled_.store(result.wasOk(), std::memory_order_release);
        return result;
    }

    void setPlaying(const bool shouldPlay)
    {
        playing_.store(shouldPlay, std::memory_order_release);
        if (!shouldPlay) {
            instrumentRenderer_.release();
            keyboardState_.allNotesOff(0);
        }
    }

    [[nodiscard]] bool isPlaying() const noexcept
    {
        return playing_.load(std::memory_order_acquire);
    }

    void setUseTestTone(const bool useTestTone) noexcept
    {
        useTestTone_.store(useTestTone, std::memory_order_release);
    }

    void setTestNote(const bool enabled)
    {
        if (enabled)
            keyboardState_.noteOn(1, 60, 0.82f);
        else
            keyboardState_.noteOff(1, 60, 0.0f);
    }

    [[nodiscard]] bool popAnalysisFrame(
        cydaw::engine::AudioAnalysisFrame& frame) noexcept
    {
        return analysisFrames_.tryPop(frame);
    }

    [[nodiscard]] std::vector<std::pair<std::uint64_t, float>>
        controlValues() const
    {
        const auto nativeValues = runtime_.controlValues();
        if (controlTelemetryTargets_.empty())
            return nativeValues;
        std::vector<std::pair<std::uint64_t, float>> authoredValues;
        authoredValues.reserve(controlTelemetryTargets_.size());
        for (const auto& value : nativeValues)
            for (const auto& target : controlTelemetryTargets_)
                if (target.nativeNodeId == value.first)
                    authoredValues.emplace_back(target.authoredNodeId,
                                                value.second);
        return authoredValues;
    }

private:
    void audioDeviceIOCallbackWithContext(
        const float* const* inputChannelData, const int numInputChannels,
        float* const* outputChannelData, const int numOutputChannels,
        const int numSamples,
        const juce::AudioIODeviceCallbackContext&) override
    {
        if (numOutputChannels <= 0 || numSamples <= 0)
            return;

        juce::AudioBuffer<float> output(outputChannelData, numOutputChannels,
                                        numSamples);
        output.clear();
        if (!playing_.load(std::memory_order_acquire))
            return;

        juce::MidiBuffer midi;
        midiCollector_.removeNextBlockOfMessages(midi, numSamples);
        keyboardState_.processNextMidiBuffer(midi, 0, numSamples, true);

        if (instrumentEnabled_.load(std::memory_order_acquire)) {
            instrumentRenderer_.render(output, midi, 0, numSamples);
        } else if (useTestTone_.load(std::memory_order_acquire)) {
            const auto rate = std::max(1.0, currentSampleRate_);
            const auto step = juce::MathConstants<double>::twoPi * 220.0 / rate;
            for (auto sample = 0; sample < numSamples; ++sample) {
                const auto value = static_cast<float>(std::sin(testTonePhase_)
                                                      * 0.12);
                for (auto channel = 0; channel < numOutputChannels; ++channel)
                    output.setSample(channel, sample, value);
                testTonePhase_ += step;
                if (testTonePhase_ >= juce::MathConstants<double>::twoPi)
                    testTonePhase_ -= juce::MathConstants<double>::twoPi;
            }
        } else {
            const auto channels = std::min(numInputChannels, numOutputChannels);
            for (auto channel = 0; channel < channels; ++channel)
                if (inputChannelData[channel] != nullptr)
                    std::copy_n(inputChannelData[channel], numSamples,
                                output.getWritePointer(channel));
        }

        if (runtimeEnabled_.load(std::memory_order_acquire))
            (void) runtime_.process(output, 0, numSamples);
        publishAnalysis(output);
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override
    {
        if (device == nullptr)
            return;
        currentSampleRate_ = std::max(1.0, device->getCurrentSampleRate());
        const auto channels = std::max(
            1, device->getActiveOutputChannels().countNumberOfSetBits());
        runtime_.prepare(currentSampleRate_,
                         std::max(1, device->getCurrentBufferSizeSamples()),
                         channels);
        instrumentRenderer_.prepare(currentSampleRate_);
        midiCollector_.reset(currentSampleRate_);
        testTonePhase_ = 0.0;
    }

    void audioDeviceStopped() override
    {
        instrumentRenderer_.release();
    }

    void publishAnalysis(const juce::AudioBuffer<float>& buffer) noexcept
    {
        cydaw::engine::AudioAnalysisFrame frame;
        frame.sequence = ++analysisSequence_;
        frame.sampleRate = static_cast<float>(currentSampleRate_);
        frame.channelCount = static_cast<std::uint32_t>(std::min(
            static_cast<int>(frame.maximumChannels),
            buffer.getNumChannels()));
        // The mono mix, consecutive: the same ring the tracks keep, so the
        // preview's spectrum is calibrated the same way theirs is.
        const auto channels = static_cast<int>(frame.channelCount);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
            float value {};
            for (int channel = 0; channel < channels; ++channel)
                value += buffer.getSample(channel, sample);
            analysisRing_.push(channels > 0
                                   ? value / static_cast<float>(channels)
                                   : 0.0f);
        }
        analysisRing_.fill(frame);
        for (std::uint32_t channel = 0; channel < frame.channelCount; ++channel) {
            frame.peak[channel] = buffer.getMagnitude(
                static_cast<int>(channel), 0, buffer.getNumSamples());
            frame.rms[channel] = buffer.getRMSLevel(
                static_cast<int>(channel), 0, buffer.getNumSamples());
        }
        (void) analysisFrames_.tryPush(frame);
    }

    juce::AudioDeviceManager deviceManager_;
    juce::MidiMessageCollector midiCollector_;
    juce::MidiKeyboardState keyboardState_;
    NativeDspRuntime runtime_;
    std::vector<cydaw::engine::CompiledControlTelemetryTarget>
        controlTelemetryTargets_;
    GraphPolySynthRenderer instrumentRenderer_;
    cydaw::realtime::SpscRing<cydaw::engine::AudioAnalysisFrame, 8>
        analysisFrames_;
    std::atomic<bool> playing_ {};
    std::atomic<bool> useTestTone_ { true };
    std::atomic<bool> runtimeEnabled_ {};
    std::atomic<bool> instrumentEnabled_ {};
    bool audioCallbacksAttached_ {};
    juce::String initialiseError_;
    double currentSampleRate_ { 48'000.0 };
    double testTonePhase_ {};
    std::uint64_t analysisSequence_ {};
    cydaw::engine::AnalysisWaveformRing analysisRing_;
};

class StandaloneGraphEditorContent final : public juce::Component,
                                           private juce::Timer {
public:
    explicit StandaloneGraphEditorContent(
        const bool initialiseAudioDevices = true)
        : preview_(initialiseAudioDevices),
          midiKeyboard_(preview_.keyboardState(),
                        juce::MidiKeyboardComponent::horizontalKeyboard)
    {
        addAndMakeVisible(editor_);
        addAndMakeVisible(playButton_);
        addAndMakeVisible(sourceSelector_);
        addAndMakeVisible(testNoteButton_);
        addAndMakeVisible(deviceButton_);
        addAndMakeVisible(statusLabel_);
        addAndMakeVisible(midiKeyboard_);

        playButton_.setClickingTogglesState(true);
        playButton_.setButtonText(tr("graph.preview.play"));
        playButton_.setTooltip(tr("graph.preview.playHelp"));
        playButton_.onClick = [this] {
            preview_.setPlaying(playButton_.getToggleState());
            if (!playButton_.getToggleState()) {
                testNoteButton_.setToggleState(false,
                                               juce::dontSendNotification);
                editor_.clearAnalysis();
                editor_.setControlValues({});
            }
            refreshStatus();
        };

        sourceSelector_.addItem(tr("graph.preview.testTone"), 1);
        sourceSelector_.addItem(tr("graph.preview.audioInput"), 2);
        sourceSelector_.setSelectedId(1, juce::dontSendNotification);
        sourceSelector_.setTooltip(tr("graph.preview.sourceHelp"));
        sourceSelector_.onChange = [this] {
            preview_.setUseTestTone(sourceSelector_.getSelectedId() == 1);
        };

        testNoteButton_.setClickingTogglesState(true);
        testNoteButton_.setButtonText(tr("graph.preview.testNote"));
        testNoteButton_.setTooltip(tr("graph.preview.testNoteHelp"));
        testNoteButton_.onClick = [this] {
            if (!playButton_.getToggleState())
                playButton_.triggerClick();
            preview_.setTestNote(testNoteButton_.getToggleState());
        };

        deviceButton_.setButtonText(tr("graph.preview.devices"));
        deviceButton_.setTooltip(tr("graph.preview.devicesHelp"));
        deviceButton_.onClick = [this] { showDeviceSelector(); };

        statusLabel_.setJustificationType(juce::Justification::centredRight);
        statusLabel_.setColour(juce::Label::textColourId,
                              juce::Colour(0xff9aa6b8));
        midiKeyboard_.setAvailableRange(24, 96);
        midiKeyboard_.setKeyWidth(18.0f);
        midiKeyboard_.setTitle(tr("graph.preview.keyboardHelp"));

        editor_.setContextName("Standalone Graph");
        editor_.setLayoutChangedHandler([this] { refreshGraph(true); });
        editor_.setParametersChangedHandler([this] { refreshGraph(false); });
        refreshGraph(true);
        startTimerHz(30);
    }

    [[nodiscard]] NodeGraphComponent& editor() noexcept { return editor_; }
    [[nodiscard]] const NodeGraphComponent& editor() const noexcept
    {
        return editor_;
    }

    void setPreviewTestTone(const bool useTestTone)
    {
        sourceSelector_.setSelectedId(useTestTone ? 1 : 2,
                                      juce::dontSendNotification);
        preview_.setUseTestTone(useTestTone);
    }

    void setPreviewPlaying(const bool playing)
    {
        playButton_.setToggleState(playing, juce::dontSendNotification);
        preview_.setPlaying(playing);
        if (!playing) {
            editor_.clearAnalysis();
            editor_.setControlValues({});
        }
        refreshStatus();
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto toolbar = area.removeFromTop(54).reduced(8, 6);
        playButton_.setBounds(toolbar.removeFromLeft(92));
        toolbar.removeFromLeft(6);
        sourceSelector_.setBounds(toolbar.removeFromLeft(164));
        toolbar.removeFromLeft(6);
        testNoteButton_.setBounds(toolbar.removeFromLeft(108));
        toolbar.removeFromLeft(6);
        deviceButton_.setBounds(toolbar.removeFromLeft(112));
        toolbar.removeFromLeft(10);
        auto keyboard = toolbar.removeFromRight(
            juce::jlimit(260, 520, toolbar.getWidth() / 2));
        midiKeyboard_.setBounds(keyboard);
        statusLabel_.setBounds(toolbar);
        editor_.setBounds(area);
    }

    void paint(juce::Graphics& graphics) override
    {
        graphics.fillAll(juce::Colour(0xff111318));
        graphics.setColour(juce::Colour(0xff303846));
        graphics.drawHorizontalLine(53, 0.0f,
                                    static_cast<float>(getWidth()));
    }

    void refreshGraph(const bool topologyChanged)
    {
        const auto runtime = editor_.runtimeGraphExport();
        const auto nodeCount = runtime.nativeGraph.nodes.size()
                               + (runtime.instrumentPlan.enabled
                                      ? runtime.instrumentPlan.graphNodeCount
                                      : 0);
        editor_.setDspCompilationStatus(runtime.compilationResult, nodeCount);
        previewResult_ = preview_.publishGraph(runtime, topologyChanged);
        refreshStatus();
    }

private:
    void timerCallback() override
    {
        cydaw::engine::AudioAnalysisFrame frame;
        auto received = false;
        while (preview_.popAnalysisFrame(frame))
            received = true;
        if (received)
            editor_.setAnalysisFrame(frame);
        if (preview_.isPlaying())
            editor_.setControlValues(preview_.controlValues());
        refreshStatus();
    }

    void refreshStatus()
    {
        if (const auto error = preview_.initialiseError(); error.isNotEmpty()) {
            statusLabel_.setText(tr("graph.preview.offline") + "  " + error,
                                 juce::dontSendNotification);
            statusLabel_.setColour(juce::Label::textColourId,
                                   juce::Colour(0xffff7676));
            return;
        }
        if (previewResult_.failed()) {
            statusLabel_.setText(tr("graph.preview.compileError") + "  "
                                     + previewResult_.getErrorMessage(),
                                 juce::dontSendNotification);
            statusLabel_.setColour(juce::Label::textColourId,
                                   juce::Colour(0xffffb45c));
            return;
        }
        auto text = preview_.isPlaying() ? tr("graph.preview.playing")
                                         : tr("graph.preview.stopped");
        if (auto* device = preview_.deviceManager().getCurrentAudioDevice())
            text += "  |  " + device->getName() + "  |  "
                    + juce::String(device->getCurrentSampleRate() / 1'000.0, 1)
                    + " kHz / "
                    + juce::String(device->getCurrentBufferSizeSamples());
        statusLabel_.setText(text, juce::dontSendNotification);
        statusLabel_.setColour(juce::Label::textColourId,
                               preview_.isPlaying()
                                   ? juce::Colour(0xff71e4b5)
                                   : juce::Colour(0xff9aa6b8));
    }

    void showDeviceSelector()
    {
        auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(
            preview_.deviceManager(), 0, 8, 1, 8, true, false, true, false);
        selector->setSize(620, 460);
        juce::CallOutBox::launchAsynchronously(
            std::move(selector), deviceButton_.getScreenBounds(), nullptr);
    }

    NodeGraphComponent editor_;
    GraphPreviewEngine preview_;
    juce::TextButton playButton_;
    juce::ComboBox sourceSelector_;
    juce::TextButton testNoteButton_;
    juce::TextButton deviceButton_;
    juce::Label statusLabel_;
    juce::MidiKeyboardComponent midiKeyboard_;
    juce::Result previewResult_ { juce::Result::ok() };
};

class GraphEditorApplication final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override
    {
        return "CyDAW Graph Editor";
    }

    const juce::String getApplicationVersion() override
    {
        return JUCE_APPLICATION_VERSION_STRING;
    }

    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String& commandLine) override
    {
        startupResultFile_ = argumentValue(commandLine, "--result-file=");
        // Before anything is built: every label is read once when its control
        // is created, so a language chosen afterwards would not reach them.
        for (const auto& language : argumentValues(commandLine, "--language="))
            (void) Localization::instance().setLanguage(language);
        lookAndFeel_ = std::make_unique<CyDawLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel(lookAndFeel_.get());
        const auto visualAutomation = commandLine.contains("--ui-snapshot=")
            || commandLine.contains("--manager-snapshot=")
            || commandLine.contains("--browser-snapshot=")
            || commandLine.contains("--interface-snapshot=")
            || commandLine.contains("--smoke-test");
        window_ = std::make_unique<MainWindow>(getApplicationName(),
                                               !visualAutomation);
        openGraphFromCommandLine(commandLine);
        if (commandLine.contains("--instrument") && window_ != nullptr)
            window_->initialiseInstrument();
        if (commandLine.contains("--modulator") && window_ != nullptr)
            window_->initialiseModulator();
        for (const auto& device : argumentValues(commandLine,
                                                  "--insert-device=")) {
            if (startupResult_.failed())
                break;
            startupResult_ = window_ != nullptr
                ? window_->insertDevice(device)
                : juce::Result::fail("Graph Editor is not available");
        }
        for (const auto& node : argumentValues(commandLine,
                                                "--insert-node=")) {
            if (startupResult_.failed())
                break;
            startupResult_ = window_ != nullptr
                ? window_->insertNode(node)
                : juce::Result::fail("Graph Editor is not available");
        }
        for (const auto& preset : argumentValues(commandLine, "--preset=")) {
            if (startupResult_.failed())
                break;
            startupResult_ = window_ != nullptr
                ? window_->loadPreset(preset)
                : juce::Result::fail("Graph Editor is not available");
        }
        for (const auto& replacement : argumentValues(
                 commandLine, "--replace-node=")) {
            if (startupResult_.failed())
                break;
            const auto separator = replacement.indexOfChar(':');
            startupResult_ = window_ != nullptr && separator > 0
                ? window_->replaceNode(
                      replacement.substring(0, separator),
                      replacement.substring(separator + 1))
                : juce::Result::fail("Invalid --replace-node specification");
        }
        for (const auto& connection : argumentValues(commandLine,
                                                       "--connect=")) {
            if (startupResult_.failed())
                break;
            const auto arrow = connection.indexOfChar('>');
            const auto left = arrow > 0 ? connection.substring(0, arrow) : "";
            const auto right = arrow > 0 ? connection.substring(arrow + 1) : "";
            const auto leftSeparator = left.lastIndexOfChar(':');
            const auto rightSeparator = right.lastIndexOfChar(':');
            startupResult_ = window_ != nullptr && leftSeparator > 0
                    && rightSeparator > 0
                ? window_->connectNodes(
                      left.substring(0, leftSeparator),
                      left.substring(leftSeparator + 1),
                      right.substring(0, rightSeparator),
                      right.substring(rightSeparator + 1))
                : juce::Result::fail("Invalid --connect specification");
        }
        for (const auto& route : argumentValues(commandLine, "--route=")) {
            if (startupResult_.failed())
                break;
            juce::StringArray parts;
            parts.addTokens(route, ">:", "");
            parts.removeEmptyStrings();
            startupResult_ = window_ != nullptr && parts.size() == 4
                ? window_->addConnectionRoute(
                      parts[0], parts[1],
                      { parts[2].getFloatValue(), parts[3].getFloatValue() })
                : juce::Result::fail("Invalid --route specification");
        }
        for (const auto& node : argumentValues(commandLine,
                                                "--collapse-node=")) {
            if (startupResult_.failed())
                break;
            startupResult_ = window_ != nullptr
                ? window_->setNodeCollapsed(node, true)
                : juce::Result::fail("Graph Editor is not available");
        }
        if (const auto title = argumentValue(commandLine,
                                             "--comment-selection=");
            title.isNotEmpty() && startupResult_.wasOk())
            startupResult_ = window_ != nullptr
                ? window_->createCommentFromSelection(title)
                : juce::Result::fail("Graph Editor is not available");
        for (const auto& subgraph : argumentValues(commandLine,
                                                    "--open-subgraph=")) {
            if (startupResult_.failed())
                break;
            startupResult_ = window_ != nullptr
                ? window_->openSubgraph(subgraph)
                : juce::Result::fail("Graph Editor is not available");
        }
        // Port selection deliberately follows subgraph navigation, allowing
        // deterministic visual and automation tests to inspect the exact
        // sockets of the graph that is currently visible.
        for (const auto& selection : argumentValues(commandLine,
                                                     "--select-port=")) {
            if (startupResult_.failed())
                break;
            const auto separator = selection.indexOfChar(':');
            const auto node = separator > 0
                                  ? selection.substring(0, separator) : "";
            const auto port = separator > 0
                                  ? selection.substring(separator + 1) : "";
            startupResult_ = window_ != nullptr && node.isNotEmpty()
                    && port.isNotEmpty() && window_->selectNodePort(node, port)
                ? juce::Result::ok()
                : juce::Result::fail("Unknown Graph node port: " + selection);
        }
        // --test-tone plays the 220 Hz test signal instead of the input, so
        // a snapshot taken with the preview running shows an analysis rather
        // than whatever the microphone heard.
        // --modulate=source,target,suffix drops a control cable from the
        // source card onto the target card's knob, as a mouse would.
        for (const auto& drop : argumentValues(commandLine, "--modulate=")) {
            juce::StringArray parts;
            parts.addTokens(drop, ",", "");
            if (parts.size() == 3 && window_ != nullptr)
                (void) window_->modulateParameter(parts[0].trim(), parts[1].trim(),
                                                  parts[2].trim());
        }
        // --ring-drag=target,suffix,pixels drags that knob's modulation ring
        // by so many pixels, as a mouse would: up widens, down turns over.
        for (const auto& drag : argumentValues(commandLine, "--ring-drag=")) {
            juce::StringArray parts;
            parts.addTokens(drag, ",", "");
            if (parts.size() == 3 && window_ != nullptr)
                (void) window_->dragModulationRing(parts[0].trim(),
                                                   parts[1].trim(),
                                                   parts[2].trim().getFloatValue());
        }
        if (commandLine.contains("--test-tone") && window_ != nullptr)
            window_->setPreviewTestTone(true);
        if (commandLine.contains("--play-preview") && window_ != nullptr)
            window_->setPreviewPlaying(true);
        if (commandLine.contains("--open-manager") && window_ != nullptr)
            window_->showGraphManager();
        if (commandLine.contains("--open-members") && window_ != nullptr)
            window_->showGraphMembersPanel();
        if (const auto query = argumentValue(commandLine,
                                             "--members-search=");
            query.isNotEmpty() && window_ != nullptr)
            window_->setGraphMemberSearch(query);
        if (const auto query = argumentValue(commandLine,
                                             "--palette-search=");
            query.isNotEmpty() && window_ != nullptr)
            window_->setPaletteSearch(query);
        if (const auto zoom = argumentValue(commandLine, "--zoom=");
            zoom.isNotEmpty() && window_ != nullptr)
            window_->setCanvasZoom(zoom.getFloatValue());
        for (const auto& fold : argumentValues(commandLine,
                                                "--palette-fold="))
            if (window_ != nullptr)
                window_->togglePaletteFold(fold);
        if (const auto asset = argumentValue(commandLine,
                                             "--manager-select=");
            asset.isNotEmpty() && window_ != nullptr)
            startupResult_ = window_->selectGraphManagerAsset(asset)
                ? juce::Result::ok()
                : juce::Result::fail("Unknown Graph Manager asset: " + asset);
        if (commandLine.contains("--manager-files") && window_ != nullptr)
            window_->showGraphManagerFileBrowser();
        if (commandLine.contains("--open-browser") && window_ != nullptr)
            window_->showNodeBrowser(argumentValue(commandLine,
                                                    "--browser-select="));
        if (commandLine.contains("--open-interface") && window_ != nullptr)
            window_->showGraphInterfaceEditor();
        if (const auto project = argumentValue(
                commandLine, "--generate-vst3-project=");
            project.isNotEmpty()) {
            juce::Timer::callAfterDelay(100, [this, project] {
                const auto result = startupResult_.failed()
                    ? startupResult_
                    : window_ != nullptr
                          ? window_->generateVst3Project(juce::File(project))
                          : juce::Result::fail(
                                "Graph Editor is not available");
                if (result.failed())
                    juce::Logger::writeToLog(result.getErrorMessage());
                writeStartupResult(result);
                setApplicationReturnValue(result.wasOk() ? 0 : 1);
                systemRequestedQuit();
            });
        } else if (commandLine.contains("--verify-presets")) {
            // Loads every factory graph and reports the ones that do not
            // compile. A preset with a mistyped port is otherwise only found
            // when someone opens it.
            juce::Timer::callAfterDelay(700, [this] {
                const auto report = window_ != nullptr
                    ? window_->verifyFactoryPresets()
                    : juce::String("no window");
                juce::Logger::writeToLog(report);
                (void) juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("cydaw-presets.txt")
                    .replaceWithText(report);
                setApplicationReturnValue(report.startsWith("all ") ? 0 : 1);
                systemRequestedQuit();
            });
        } else if (const auto managerSnapshot = argumentValue(
                       commandLine, "--manager-snapshot=");
                   managerSnapshot.isNotEmpty()) {
            juce::Timer::callAfterDelay(900, [this, managerSnapshot] {
                const auto saved = window_ != nullptr
                                   && window_->saveGraphManagerSnapshot(
                                       juce::File(managerSnapshot));
                setApplicationReturnValue(saved ? 0 : 1);
                systemRequestedQuit();
            });
        } else if (const auto browserSnapshot = argumentValue(
                       commandLine, "--browser-snapshot=");
                   browserSnapshot.isNotEmpty()) {
            juce::Timer::callAfterDelay(900, [this, browserSnapshot] {
                const auto saved = window_ != nullptr
                                   && window_->saveNodeBrowserSnapshot(
                                       juce::File(browserSnapshot));
                setApplicationReturnValue(saved ? 0 : 1);
                systemRequestedQuit();
            });
        } else if (const auto interfaceSnapshot = argumentValue(
                       commandLine, "--interface-snapshot=");
                   interfaceSnapshot.isNotEmpty()) {
            juce::Timer::callAfterDelay(900, [this, interfaceSnapshot] {
                const auto saved = window_ != nullptr
                                   && window_->saveGraphInterfaceSnapshot(
                                       juce::File(interfaceSnapshot));
                setApplicationReturnValue(saved ? 0 : 1);
                systemRequestedQuit();
            });
        } else if (const auto snapshot = snapshotPath(commandLine);
            snapshot.isNotEmpty()) {
            // --snapshot-delay=ms waits longer than the default: a snapshot
            // meant to show the preview's analysis needs the audio device up
            // and a few frames through, which nine hundred milliseconds does
            // not always allow.
            const auto delay = std::max(
                900, argumentValue(commandLine, "--snapshot-delay=")
                         .getIntValue());
            juce::Timer::callAfterDelay(delay, [this, snapshot] {
                const auto saved = window_ != nullptr
                                   && window_->saveSnapshot(
                                       juce::File(snapshot));
                setApplicationReturnValue(saved ? 0 : 1);
                systemRequestedQuit();
            });
        } else if (commandLine.contains("--smoke-test"))
            juce::Timer::callAfterDelay(600, [this] { systemRequestedQuit(); });
    }

    void shutdown() override
    {
        window_.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
        lookAndFeel_.reset();
    }

    void anotherInstanceStarted(const juce::String& commandLine) override
    {
        openGraphFromCommandLine(commandLine);
        if (window_ != nullptr)
            window_->toFront(true);
    }

    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow final : public juce::DocumentWindow {
    public:
        explicit MainWindow(const juce::String& title,
                            const bool initialiseAudioDevices)
            : DocumentWindow(title, juce::Colour(0xff111318), allButtons)
        {
            setUsingNativeTitleBar(true);
            content_ = new StandaloneGraphEditorContent(
                initialiseAudioDevices);
            setContentOwned(content_, true);
            setResizable(true, false);
            setResizeLimits(920, 560, 7680, 4320);
            centreWithSize(1440, 860);
            refreshCompilationStatus();
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            if (auto* application = juce::JUCEApplication::getInstance())
                application->systemRequestedQuit();
        }

        juce::String verifyFactoryPresets()
        {
            if (content_ == nullptr)
                return "no content";
            auto& editor = content_->editor();
            const auto total = editor.factoryPresetCount();
            juce::StringArray failures;
            auto instruments = 0;
            for (std::size_t index = 0; index < total; ++index) {
                const auto loaded = editor.loadFactoryPreset(index);
                if (loaded.failed()) {
                    failures.add("preset " + juce::String(static_cast<int>(index))
                                 + ": " + loaded.getErrorMessage());
                    continue;
                }
                const auto compiled = editor.runtimeGraphExport();
                if (compiled.compilationResult.failed()) {
                    failures.add("preset " + juce::String(static_cast<int>(index))
                                 + ": "
                                 + compiled.compilationResult.getErrorMessage());
                    continue;
                }
                // Compiling is not enough for an instrument: a graph whose
                // voice chain does not resolve still compiles, and then plays
                // nothing at all.
                if (editor.graphRole() != cydaw::graph::GraphRole::instrument)
                    continue;
                ++instruments;
                if (!compiled.instrumentPlan.enabled)
                    failures.add("preset " + juce::String(static_cast<int>(index))
                                 + ": instrument graph produces no voice plan");
            }
            if (failures.isEmpty())
                return "all " + juce::String(static_cast<int>(total))
                       + " factory graphs compile, "
                       + juce::String(instruments)
                       + " of them producing a live voice plan";
            return juce::String(failures.size()) + " of "
                   + juce::String(static_cast<int>(total)) + " failed | "
                   + failures.joinIntoString(" | ");
        }

        void openGraph(const juce::File& file)
        {
            if (content_ == nullptr || !file.existsAsFile()
                || !file.hasFileExtension("cydawgraph"))
                return;
            const auto result = content_->editor().loadUserGraphPreset(file);
            if (result.failed())
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "CyDAW Graph Editor", result.getErrorMessage());
            else
                setName("CyDAW Graph Editor - "
                        + file.getFileNameWithoutExtension());
            refreshCompilationStatus();
        }

        bool saveSnapshot(const juce::File& destination)
        {
            if (content_ == nullptr)
                return false;
            destination.getParentDirectory().createDirectory();
            auto stream = destination.createOutputStream();
            if (stream == nullptr)
                return false;
            const auto image = content_->createComponentSnapshot(
                content_->getLocalBounds(), true, 1.0f);
            juce::PNGImageFormat format;
            return image.isValid() && format.writeImageToStream(image, *stream);
        }

        bool saveGraphManagerSnapshot(const juce::File& destination)
        {
            return content_ != nullptr
                && content_->editor().saveGraphManagerSnapshot(destination);
        }

        void showGraphManager()
        {
            if (content_ != nullptr)
                content_->editor().showGraphManager();
        }

        void showGraphMembersPanel()
        {
            if (content_ != nullptr)
                content_->editor().showGraphMembersPanel();
        }

        void setGraphMemberSearch(const juce::String& query)
        {
            if (content_ != nullptr)
                content_->editor().setGraphMemberSearch(query);
        }

        void setPaletteSearch(const juce::String& query)
        {
            if (content_ != nullptr)
                content_->editor().setPaletteSearch(query);
        }

        void setCanvasZoom(const float zoom)
        {
            if (content_ != nullptr)
                content_->editor().setCanvasZoom(zoom);
        }

        void togglePaletteFold(const juce::String& foldKey)
        {
            if (content_ != nullptr)
                content_->editor().togglePaletteFold(foldKey);
        }

        bool selectGraphManagerAsset(const juce::String& key)
        {
            return content_ != nullptr
                   && content_->editor().selectGraphManagerAsset(key);
        }

        void showGraphManagerFileBrowser()
        {
            if (content_ != nullptr)
                content_->editor().showGraphManagerFileBrowser();
        }

        void showNodeBrowser(const juce::String& selectedKey)
        {
            if (content_ != nullptr)
                content_->editor().showNodeBrowser(selectedKey);
        }

        bool saveNodeBrowserSnapshot(const juce::File& destination) const
        {
            return content_ != nullptr
                && content_->editor().saveNodeBrowserSnapshot(destination);
        }

        void showGraphInterfaceEditor()
        {
            if (content_ != nullptr)
                content_->editor().showGraphInterfaceEditor();
        }

        bool saveGraphInterfaceSnapshot(
            const juce::File& destination) const
        {
            return content_ != nullptr
                && content_->editor().saveGraphInterfaceSnapshot(destination);
        }

        juce::Result generateVst3Project(
            const juce::File& destination) const
        {
            return content_ != nullptr
                ? content_->editor().generateVst3Project(destination)
                : juce::Result::fail("Graph Editor is not available");
        }

        // Factory presets are addressed by index, in catalogue order.
        juce::Result loadPreset(const juce::String& value)
        {
            if (content_ == nullptr)
                return juce::Result::fail("Graph Editor is not available");
            auto& editor = content_->editor();
            const auto index = value.getIntValue();
            if (value.isEmpty() || !value.containsOnly("0123456789")
                || static_cast<std::size_t>(index)
                       >= editor.factoryPresetCount())
                return juce::Result::fail("Unknown factory preset: " + value);
            return editor.loadFactoryPreset(static_cast<std::size_t>(index));
        }

        juce::Result insertDevice(const juce::String& key)
        {
            const auto result = content_ != nullptr
                ? content_->editor().insertFactoryDevice(key)
                : juce::Result::fail("Graph Editor is not available");
            refreshCompilationStatus();
            return result;
        }

        juce::Result insertNode(const juce::String& key)
        {
            const auto result = content_ != nullptr
                ? content_->editor().insertNode(key)
                : juce::Result::fail("Graph Editor is not available");
            refreshCompilationStatus();
            return result;
        }

        juce::Result replaceNode(const juce::String& currentKey,
                                 const juce::String& replacementKey)
        {
            const auto result = content_ != nullptr
                ? content_->editor().replaceNode(currentKey, replacementKey)
                : juce::Result::fail("Graph Editor is not available");
            refreshCompilationStatus();
            return result;
        }

        juce::Result connectNodes(const juce::String& sourceType,
                                  const juce::String& sourcePort,
                                  const juce::String& destinationType,
                                  const juce::String& destinationPort)
        {
            return content_ != nullptr
                ? content_->editor().connectNodePorts(
                      sourceType, sourcePort, destinationType,
                      destinationPort, true)
                : juce::Result::fail("Graph Editor is not available");
        }

        juce::Result addConnectionRoute(
            const juce::String& sourceType,
            const juce::String& destinationType,
            const juce::Point<float> graphPoint)
        {
            return content_ != nullptr
                ? content_->editor().addConnectionRoutingPoint(
                      sourceType, destinationType, graphPoint)
                : juce::Result::fail("Graph Editor is not available");
        }

        juce::Result setNodeCollapsed(const juce::String& nodeTypeKey,
                                      const bool collapsed)
        {
            return content_ != nullptr
                ? content_->editor().setNodeCollapsed(nodeTypeKey, collapsed)
                : juce::Result::fail("Graph Editor is not available");
        }

        juce::Result createCommentFromSelection(const juce::String& title)
        {
            return content_ != nullptr
                ? content_->editor().createCommentFromSelection(title)
                : juce::Result::fail("Graph Editor is not available");
        }

        bool selectNodePort(const juce::String& nodeTypeKey,
                            const juce::String& portKey)
        {
            return content_ != nullptr
                   && content_->editor().selectNodePort(nodeTypeKey, portKey);
        }

        juce::Result openSubgraph(const juce::String& key)
        {
            return content_ != nullptr
                ? content_->editor().openSubgraphEditor(key)
                : juce::Result::fail("Graph Editor is not available");
        }

        void initialiseInstrument()
        {
            if (content_ != nullptr) {
                content_->editor().initialiseEmptyMidiTrackGraph();
                refreshCompilationStatus();
            }
        }

        void initialiseModulator()
        {
            if (content_ != nullptr) {
                content_->editor().initialiseValueModulationGraph();
                refreshCompilationStatus();
            }
        }

        void setPreviewTestTone(const bool useTestTone)
        {
            if (content_ != nullptr)
                content_->setPreviewTestTone(useTestTone);
        }

        bool modulateParameter(const juce::String& source,
                               const juce::String& target,
                               const juce::String& suffix)
        {
            return content_ != nullptr
                   && content_->editor().modulateParameterForTest(source, target,
                                                                  suffix);
        }

        bool dragModulationRing(const juce::String& target,
                                const juce::String& suffix, const float deltaY)
        {
            return content_ != nullptr
                   && content_->editor().dragModulationRingForTest(target, suffix,
                                                                   deltaY);
        }

        void setPreviewPlaying(const bool playing)
        {
            if (content_ != nullptr)
                content_->setPreviewPlaying(playing);
        }

    private:
        void refreshCompilationStatus()
        {
            if (content_ == nullptr)
                return;
            content_->refreshGraph(true);
        }

        StandaloneGraphEditorContent* content_ {};
    };

    void openGraphFromCommandLine(const juce::String& commandLine)
    {
        if (window_ == nullptr)
            return;
        juce::StringArray arguments;
        arguments.addTokens(commandLine, true);
        arguments.removeEmptyStrings();
        for (const auto& argument : arguments) {
            const auto file = juce::File(argument.unquoted());
            if (file.existsAsFile() && file.hasFileExtension("cydawgraph")) {
                window_->openGraph(file);
                break;
            }
        }
    }

    static juce::String snapshotPath(const juce::String& commandLine)
    {
        return argumentValue(commandLine, "--ui-snapshot=");
    }

    static juce::String argumentValue(const juce::String& commandLine,
                                      const juce::String& prefix)
    {
        juce::StringArray arguments;
        arguments.addTokens(commandLine, true);
        arguments.removeEmptyStrings();
        for (const auto& argument : arguments)
            if (argument.startsWith(prefix))
                return argument.fromFirstOccurrenceOf("=", false, false)
                    .unquoted();
        return {};
    }

    static juce::StringArray argumentValues(
        const juce::String& commandLine, const juce::String& prefix)
    {
        juce::StringArray values;
        juce::StringArray arguments;
        arguments.addTokens(commandLine, true);
        arguments.removeEmptyStrings();
        for (const auto& argument : arguments)
            if (argument.startsWith(prefix))
                values.add(argument.fromFirstOccurrenceOf("=", false, false)
                               .unquoted());
        return values;
    }

    void writeStartupResult(const juce::Result& result) const
    {
        if (startupResultFile_.isEmpty())
            return;
        auto root = std::make_unique<juce::DynamicObject>();
        root->setProperty("ok", result.wasOk());
        root->setProperty("message", result.wasOk()
                                         ? juce::String("ok")
                                         : result.getErrorMessage());
        const auto file = juce::File(startupResultFile_);
        file.getParentDirectory().createDirectory();
        file.replaceWithText(juce::JSON::toString(juce::var(root.release()),
                                                  true));
    }

    std::unique_ptr<CyDawLookAndFeel> lookAndFeel_;
    std::unique_ptr<MainWindow> window_;
    juce::Result startupResult_ { juce::Result::ok() };
    juce::String startupResultFile_;
};

} // namespace cydaw::app

START_JUCE_APPLICATION(cydaw::app::GraphEditorApplication)
