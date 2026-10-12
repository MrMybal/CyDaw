#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <memory>
#include <mutex>
#include <optional>

// CyDAW's Python companion (tools/companion/cydaw_companion): the sound
// matching analysis runs there, as a child process that prints one JSON
// object per line. This starts it, follows its progress and stops it; the
// result is read on the message thread through status().

namespace cydaw::v2 {

class Companion final {
public:
    struct Status {
        bool running {};
        bool finished {};   // a job ended (done or failed) and was not yet taken
        bool failed {};
        double progress {};
        juce::String message;
        juce::String error;
        juce::var result;    // the {"done": true, ...} object
        juce::String command; // what runs ("analyse", "setup", "info")
        juce::StringArray log; // its last messages (pip's lines while it installs)
    };

    Companion();
    ~Companion();

    // The Python that runs it: CYDAW_COMPANION_PYTHON (a test's, never
    // saved), the setting, else the companion's own environment when it was
    // made, else "python" from the PATH.
    [[nodiscard]] juce::String pythonCommand() const;
    void setPythonCommand(const juce::String& command);
    // Where `import cydaw_companion` is found: beside the executable when
    // shipped (tools/companion), in the source tree otherwise.
    [[nodiscard]] static juce::File packageFolder();
    // The companion's own environment (a venv), made from the Options.
    [[nodiscard]] static juce::File environmentFolder();

    // Starts `python -m cydaw_companion <arguments>`; false when one runs.
    bool start(const juce::StringArray& arguments);
    void cancel();
    [[nodiscard]] Status status() const;
    // The finished job's status, once: the caller applies it.
    [[nodiscard]] std::optional<Status> takeFinished();

    // A short command run to its end (info): its last JSON line, or why not.
    [[nodiscard]] juce::var runNow(const juce::StringArray& arguments, juce::String& error, int timeoutMs = 60'000) const;
    // The command line that runs `python -m cydaw_companion <arguments>`.
    [[nodiscard]] juce::StringArray commandLine(const juce::StringArray& arguments) const;

private:
    class Job;
    void loadSettings();

    juce::String python_;
    std::unique_ptr<Job> job_;
    mutable std::mutex mutex_;
    Status status_;
    friend class Job;
};

} // namespace cydaw::v2
