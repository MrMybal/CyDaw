#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <mutex>

// A VST3 built from a device, in the background.
//
// The device's project (what NodeGraphComponent::generateVst3Project writes)
// is copied into one working folder and built there with CMake and Visual
// Studio, which compile CyDAW's graph runtime around it: the first build
// takes minutes, the next ones reuse what did not change. The .vst3 bundle
// lands beside the project the person keeps. Nothing runs until they ask.

namespace cydaw::v2 {

class Vst3Build final : private juce::Thread {
public:
    // What a build needs: CMake (Visual Studio's own will do), CyDAW's
    // sources, and JUCE's when they are already on disk (else CMake
    // downloads them).
    struct Tools {
        juce::File cmake;
        juce::File sourceRoot;
        juce::File juceSource;
        [[nodiscard]] bool ready() const { return cmake.existsAsFile() && sourceRoot.isDirectory(); }
    };
    [[nodiscard]] static Tools findTools(const juce::File& sourceRootHint = {});
    [[nodiscard]] static bool isSourceRoot(const juce::File& folder);
    // Where builds happen, short enough for Visual Studio's paths.
    [[nodiscard]] static juce::File workFolder();

    enum class Phase { idle, configuring, building, done, failed, cancelled };

    Vst3Build();
    ~Vst3Build() override;

    // `project` is the generated folder; the bundle is copied into
    // `destination`. False while a build runs.
    bool start(const juce::File& project, const juce::File& destination, const juce::String& productName, const Tools& tools);
    void cancel();

    [[nodiscard]] Phase phase() const noexcept { return phase_.load(); }
    [[nodiscard]] bool running() const noexcept
    {
        return phase_.load() == Phase::configuring || phase_.load() == Phase::building;
    }
    // The last lines CMake and the compiler wrote.
    [[nodiscard]] juce::StringArray log() const;
    [[nodiscard]] juce::File bundle() const;
    [[nodiscard]] juce::String failure() const;
    [[nodiscard]] double elapsedSeconds() const;

private:
    void run() override;
    bool runStep(const juce::StringArray& command);
    void addLine(const juce::String& line);
    void finish(Phase phase, const juce::String& failure = {});

    std::atomic<Phase> phase_ { Phase::idle };
    mutable std::mutex mutex_;
    juce::StringArray log_;
    juce::File project_;
    juce::File destination_;
    juce::String productName_;
    Tools tools_;
    juce::File bundle_;
    juce::String failure_;
    double started_ {};
    double finished_ {};
};

} // namespace cydaw::v2
