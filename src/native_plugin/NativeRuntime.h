#pragma once

#include <cydaw/engine/AudioAnalysis.h>
#include <cydaw/engine/Modulators.h>
#include <cydaw/engine/NativeDspGraph.h>
#include <cydaw/realtime/RtPublisher.h>

#include <mutex>
#include <string>
#include <limits>

#include <array>
#include <atomic>
#include <memory>
#include <utility>
#include <vector>

namespace cydaw::engine
{
class AudioRenderPlan;
}

namespace cydaw::native
{
struct Result
{
    std::string error;
    static Result ok()
    {
        return {};
    }
    static Result fail(std::string message)
    {
        return {std::move(message)};
    }
    bool failed() const noexcept
    {
        return !error.empty();
    }
    bool wasOk() const noexcept
    {
        return error.empty();
    }
    const std::string &getErrorMessage() const noexcept
    {
        return error;
    }
};
// Borrowed planar host buffers; never owns or allocates memory.
struct PlanarBuffer
{
    float *const *data{};
    int channels{}, frames{};
    int getNumChannels() const noexcept
    {
        return channels;
    }
    int getNumSamples() const noexcept
    {
        return frames;
    }
    const float *getReadPointer(int channel, int offset = 0) const noexcept
    {
        return data[channel] + offset;
    }
    float *getWritePointer(int channel, int offset = 0) noexcept
    {
        return data[channel] + offset;
    }
};

// Lock-free runtime used wherever a visual native graph must process an
// existing audio buffer. Graph compilation happens on the control thread and a
// complete runtime is published to the audio callback through RtPublisher, so
// the callback borrows it through an always-lock-free atomic pointer and a
// replaced graph is released on the control thread instead of inside a block.
// A node that lives at control rate only — no audio in, no audio out. The
// runtime evaluates these ahead of the audio nodes every block, and the graph
// builder has to admit them for the same reason. One definition, used by both.
[[nodiscard]] constexpr bool isControlOnlyNativeKind(const cydaw::engine::NativeDspNodeKind kind) noexcept
{
    using Kind = cydaw::engine::NativeDspNodeKind;
    return (kind >= Kind::controlConstant && kind <= Kind::controlNot) ||
           (kind >= Kind::controlModulo && kind <= Kind::controlRound) || cydaw::engine::isModulatorKind(kind);
}

class NativeDspRuntime
{
  public:
    NativeDspRuntime();

    [[nodiscard]] Result setGraph(const cydaw::engine::NativeDspGraph &graph);
    [[nodiscard]] Result updateParameters(const cydaw::engine::NativeDspGraph &graph);
    // Realtime-safe fast path used by host automation. The render topology and
    // processor map are immutable after publication; built-in parameters are
    // atomic, so this performs no locking, allocation or graph recompilation.
    [[nodiscard]] bool updateNodeParametersRealtime(const cydaw::engine::NativeDspNode &node) noexcept;
    [[nodiscard]] cydaw::engine::NativeDspGraph graph() const;
    // Audio nodes in the compiled plan; the control nodes are not counted.
    [[nodiscard]] std::size_t audioNodeCount() const;
    // How late the graph's sound comes out (its look-aheads, oversamplers),
    // its parallel paths already aligned. Control thread; it follows the
    // knobs, so the mix asks again as it polls.
    [[nodiscard]] int latencySamples() const noexcept;
    void prepare(double sampleRate, int maximumBlockSize, int channelCount);
    // Audio thread, or while processing is stopped. Preserves topology and parameters.
    void resetRealtime() noexcept;
    [[nodiscard]] bool process(PlanarBuffer &buffer, int startSample, int numSamples) noexcept;
    // On the audio thread, for the next process() only: the graph's extra
    // audio inputs (bus 1, 2, 3), each `numSamples` from 0, or null.
    static constexpr std::size_t maximumExtraInputs = 3;
    void setExtraInputs(const PlanarBuffer *const *buses, std::size_t count) noexcept
    {
        for (std::size_t index = 0; index < maximumExtraInputs; ++index)
            extraInputs_[index] = index < count && buses != nullptr ? buses[index] : nullptr;
    }
    // The same at a place of the timeline, in seconds from its start, where
    // the automation lanes are read; NaN leaves them be.
    [[nodiscard]] bool process(PlanarBuffer &buffer, int startSample, int numSamples, double timelineSeconds) noexcept;

    // ---- Automation ------------------------------------------------------
    // A node's parameter following a curve of the timeline. The lanes are
    // built on the message thread and survive a recompiled graph; the
    // callback reads each at the start of every block it processes.
    struct AutomationPoint
    {
        double timeSeconds{};
        // In the parameter's own units (Hz, dB...).
        float value{};
        // 0 straight to the next point, 1 bent by curveAmount (-1..1).
        int curve{};
        float curveAmount{};
    };
    struct AutomationLane
    {
        std::uint64_t nodeId{};
        // A field of the node's payload, or a slot of its eqBands.
        float cydaw::engine::NativeDspNodeParameters::*field{};
        int slot{-1};
        // A frequency or a time glides on its logarithmic scale, as its knob
        // turns: a straight line in the lane is what is heard.
        bool logarithmic{};
        std::vector<AutomationPoint> points;
        // A launcher clip's lane: its points count from `clipStartSeconds`
        // on the timeline (before it, the knob's own value) and, past
        // `clipLengthSeconds`, loop (`clipLooping`) or hold the last value.
        // Below 0: a lane of the timeline itself.
        double clipStartSeconds{-1.0};
        double clipLengthSeconds{};
        bool clipLooping{};
    };
    void setAutomation(std::vector<AutomationLane> lanes);
    // Where a lane stands at a time: the first point's value before it, the
    // last one's after it.
    [[nodiscard]] static float automationValueAt(const std::vector<AutomationPoint> &points, double seconds,
                                                 bool logarithmic = false) noexcept;
    // UI-thread snapshot of the last control-rate values. The audio callback
    // only touches atomics; allocating the returned diagnostic list is kept
    // entirely outside real-time processing.
    [[nodiscard]] std::vector<std::pair<std::uint64_t, float>> controlValues() const;
    // The loudest these nodes gave out since they were last asked (two
    // channels), for the device panel's meters. Message thread.
    void takeNodePeaks(const std::uint64_t *nodes, std::size_t count, float *left, float *right) const;
    // What a node shows of its work now (an equaliser's dynamic bands):
    // up to `count` values, how many. Message thread.
    [[nodiscard]] std::size_t nodeReadings(std::uint64_t node, float *into, std::size_t count) const;
    // On the audio thread, between two process() calls: the last value of
    // each control node asked for, NaN for one the graph does not have.
    void controlValuesNow(const std::uint64_t *nodeIds, float *values, std::size_t count) const noexcept;

    // The session's tempo, for processors that follow it (a delay synced to
    // a note value). One for the process; the controller sets it.
    static void setSessionTempo(double beatsPerMinute) noexcept;
    [[nodiscard]] static double sessionTempo() noexcept;
    // Whether the session's transport plays: a processor then hears where
    // it is (its beat, from the block's place on the timeline) and follows
    // the bars; stopped, it runs free at the tempo.
    static void setSessionPlaying(bool playing) noexcept;
    [[nodiscard]] static bool sessionPlaying() noexcept;

    // What the notes and controllers that play the graph say now, for its
    // modulators (an envelope, keytrack, expressions). Audio thread, before
    // the block it is for; a graph nobody plays notes into leaves it at rest.
    void setControlNotes(const cydaw::engine::modulator::ControlNotes &notes) noexcept { notes_ = notes; }

    // ---- Signal taps -----------------------------------------------------
    // What a node received or produced, kept for the interface's displays
    // (spectra before and after a device, a compressor's levels). Chosen on
    // the message thread, filled by the callback after each block without
    // allocating or locking, read back on the message thread. A tap
    // survives a recompiled graph as long as its node does.
    static constexpr std::size_t tapCount = 16;
    struct TapRequest
    {
        std::uint64_t nodeId{};
        // The node's inputs summed, rather than its output.
        bool input{};
    };
    void setTaps(const std::vector<TapRequest> &taps) noexcept;
    // The latest samples of a tap, mono, oldest first; `sequence` counts the
    // blocks it has seen. False when that tap is not kept.
    [[nodiscard]] bool readTap(std::uint64_t nodeId, bool input,
                               cydaw::engine::AudioAnalysisFrame &frame) const noexcept;

  private:
    cydaw::engine::modulator::ControlNotes notes_;
    // The graph's cards switched off, into its compiled plan.
    static void applyBypasses(cydaw::engine::AudioRenderPlan &plan, const cydaw::engine::NativeDspGraph &graph);
    std::array<const PlanarBuffer *, maximumExtraInputs> extraInputs_{};
    struct Tap
    {
        // nodeId * 2 + input; zero when the slot is free.
        std::atomic<std::uint64_t> key{};
        // The key the callback last filled for; the callback's own.
        std::uint64_t audioKey{};
        std::array<std::atomic<float>, cydaw::engine::AudioAnalysisFrame::waveformCapacity> samples{};
        std::atomic<std::uint32_t> write{};
        std::atomic<std::uint32_t> filled{};
        std::atomic<std::uint64_t> blocks{};
    };

    struct RuntimeState;
    [[nodiscard]] std::shared_ptr<RuntimeState> compile(const cydaw::engine::NativeDspGraph &graph,
                                                        std::string &error) const;
    void evaluateControls(const RuntimeState &runtime, int numSamples, double timelineSeconds) noexcept;
    // The cables' values and the automation's written into the parameters
    // they drive, each touched node updated once.
    void applyParameters(const RuntimeState &runtime, double timelineSeconds) noexcept;
    void captureTaps(const RuntimeState &runtime, std::uint32_t frames) noexcept;

    mutable std::mutex configurationLock_;
    cydaw::engine::NativeDspGraph graph_;
    cydaw::realtime::RtPublisher<RuntimeState> runtime_;
    double sampleRate_{48'000.0};
    int maximumBlockSize_{512};
    int channelCount_{2};
    std::unique_ptr<std::array<Tap, tapCount>> taps_;
    cydaw::realtime::RtPublisher<std::vector<AutomationLane>> automation_;
};

[[nodiscard]] cydaw::engine::NativeDspGraph makePassthroughAudioGraph();

} // namespace cydaw::native
