#include <cydaw/base/Thread.h>
#include <cydaw/base/File.h>
#include <cydaw/base/Text.h>
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

class Vst3Build final : private cydaw::base::Thread {
public:
    // What a build needs: CMake (Visual Studio's own will do), CyDAW's
    // sources, and the MIT VST3 SDK (fetched when absent).
    struct Tools {
        cydaw::base::File cmake;
        cydaw::base::File sourceRoot;
        cydaw::base::File vst3Sdk;
        cydaw::base::File ninja, python, linuxToolchain;
        [[nodiscard]] bool ready() const { return cmake.existsAsFile() && sourceRoot.isDirectory(); }
    };
    [[nodiscard]] static Tools findTools(const cydaw::base::File& sourceRootHint = {});
    [[nodiscard]] static bool isSourceRoot(const cydaw::base::File& folder);
    // Where builds happen, short enough for Visual Studio's paths.
    [[nodiscard]] static cydaw::base::File workFolder();

    enum class Phase { idle, configuring, building, done, failed, cancelled };

    Vst3Build();
    ~Vst3Build() override;

    // `project` is the generated folder; the bundle is copied into
    // `destination`. False while a build runs.
    bool start(const cydaw::base::File& project, const cydaw::base::File& destination, const cydaw::base::Text& productName, const Tools& tools, const cydaw::base::Text& format = "VST3");
    struct Job {
        cydaw::base::File project, destination;
        cydaw::base::Text productName, format, platform {"Windows"};
    };
    struct JobResult {
        cydaw::base::Text format, platform;
        cydaw::base::File file;
        cydaw::base::Text error;
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
    [[nodiscard]] cydaw::base::TextList log() const;
    [[nodiscard]] cydaw::base::File bundle() const;
    [[nodiscard]] cydaw::base::Text failure() const;
    [[nodiscard]] double elapsedSeconds() const;

private:
    void run() override;
    bool runJob(const Job& job);
    bool runStep(const cydaw::base::TextList& command);
    void addLine(const cydaw::base::Text& line);
    void finish(Phase phase, const cydaw::base::Text& failure = {});

    std::atomic<Phase> phase_ { Phase::idle };
    mutable std::mutex mutex_;
    cydaw::base::TextList log_;
    Tools tools_;
    std::vector<Job> jobs_;
    std::vector<JobResult> results_;
    cydaw::base::File bundle_;
    cydaw::base::Text failure_;
    double started_ {};
    double finished_ {};
};

} // namespace cydaw::v2
