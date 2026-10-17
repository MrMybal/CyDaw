#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <mutex>
#include <vector>

// Native plugin builds run sequentially in the background. Each platform/format
// has its own compiler cache; generated projects and delivered bundles remain
// in the requested export folder. The historical class name is retained for
// existing API callers.

namespace cydaw::v2 {

class Vst3Build final : private juce::Thread {
public:
    // What a build needs: CMake (Visual Studio's own will do), CyDAW's
    // sources, and the MIT VST3 SDK (fetched when absent).
    struct Tools {
        juce::File cmake;
        juce::File sourceRoot;
        juce::File vst3Sdk;
        juce::File ninja, python, linuxToolchain;
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
    bool start(const juce::File& project, const juce::File& destination, const juce::String& productName, const Tools& tools, const juce::String& format = "VST3");
    struct Job {
        juce::File project, destination;
        juce::String productName, format, platform {"Windows"};
    };
    struct JobResult {
        juce::String format, platform;
        juce::File file;
        juce::String error;
    };
    bool startBatch(const std::vector<Job>& jobs, const Tools& tools);
    [[nodiscard]] std::vector<JobResult> results() const;
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
    bool runJob(const Job& job);
    bool runStep(const juce::StringArray& command);
    void addLine(const juce::String& line);
    void finish(Phase phase, const juce::String& failure = {});

    std::atomic<Phase> phase_ { Phase::idle };
    mutable std::mutex mutex_;
    juce::StringArray log_;
    Tools tools_;
    std::vector<Job> jobs_;
    std::vector<JobResult> results_;
    juce::File bundle_;
    juce::String failure_;
    double started_ {};
    double finished_ {};
};

} // namespace cydaw::v2
