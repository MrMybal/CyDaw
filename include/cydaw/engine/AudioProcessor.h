#pragma once

#include <cydaw/engine/AudioBuffer.h>
#include <cydaw/engine/Transport.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace cydaw::engine {

struct ProcessSpec {
    double sampleRate { 48'000.0 };
    std::uint32_t maximumBlockSize { 512 };
    std::uint32_t channelCount { 2 };

    [[nodiscard]] bool operator==(const ProcessSpec&) const = default;
};

struct ProcessContext {
    TransportSnapshot transport;
};

struct AudioInputBusView {
    std::uint64_t portId {};
    AudioBlockView audio;
};

// Optional processor interface for nodes whose authored input sockets must
// remain distinct at render time (mixers, sidechains and multibus effects).
// The bus array and all backing audio storage are prepared off the audio
// thread by AudioRenderPlanBuilder.
class MultiBusAudioProcessor {
public:
    virtual ~MultiBusAudioProcessor() = default;
    virtual void processAudioBuses(
        std::span<const AudioInputBusView> inputs, AudioBlockView output,
        const ProcessContext& context) noexcept = 0;
};

class AudioProcessor {
public:
    virtual ~AudioProcessor() = default;
    virtual void prepare(const ProcessSpec& spec) = 0;
    virtual void reset() noexcept = 0;
    virtual void process(AudioBlockView input,
                         AudioBlockView output,
                         const ProcessContext& context) noexcept = 0;
    // How many samples its output lags its input: a look-ahead, the filters
    // of an oversampler. Read every block on the audio thread and, for the
    // mix's compensation, on the control thread, so it may follow a knob (an
    // atomic's business). Never more than maximumLatencySamples(), which
    // holds from prepare() on.
    [[nodiscard]] virtual std::uint32_t latencySamples() const noexcept { return 0; }
    [[nodiscard]] virtual std::uint32_t maximumLatencySamples() const noexcept { return 0; }
    // Values on their way to those last set (a knob being turned): true
    // while some still move. The render plan then processes the block in
    // short pieces and calls glideValues() before each, so what is heard
    // moves in small steps instead of one jump a block. Audio thread.
    [[nodiscard]] virtual bool valuesGliding() noexcept { return false; }
    // What it shows of its work now, for an interface (how far an
    // equaliser's dynamic bands moved): up to `count` values into `into`;
    // how many. Any thread while the audio thread runs, so atomics' business.
    [[nodiscard]] virtual std::size_t readings(float* /*into*/, std::size_t /*count*/) const noexcept { return 0; }
    virtual void glideValues(std::uint32_t /*frames*/, double /*sampleRate*/) noexcept {}
};

} // namespace cydaw::engine
