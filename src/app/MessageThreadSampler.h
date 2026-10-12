#pragma once

#include <juce_core/juce_core.h>

#include <memory>

namespace cydaw::app {

// Where the message thread is while it is stuck. The hitch probe's timer
// only measures gaps; it cannot say what filled them, and a gap with no
// paint note and no tick note pointed at nothing. This watches a heartbeat
// the probe's timer sends, and once it stops, another thread suspends the
// message thread, unwinds its stack, resumes it and names the frames. The
// stacks are counted, and the report lists the commonest. Symbols come
// from the executable's PDB; without one the frames are module offsets.
// Windows only; elsewhere it reports nothing.
class MessageThreadSampler final {
public:
    // Made on the message thread, which is the thread it will watch.
    MessageThreadSampler();
    ~MessageThreadSampler();

    // The message thread is alive; from the probe's timer.
    void beat() noexcept;
    // Sampling begins once no beat has come for this long, and repeats
    // every few milliseconds until one does.
    void start(double stuckAfterMs);
    // Stops and describes what was seen: "stacks[n: 12x a<b<c | ...]".
    [[nodiscard]] juce::String stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cydaw::app
