## Includes and public operations {#signatures}
These declarations describe the `cydaw::engine::AudioProcessor` virtual interface. Derive a processor and implement `prepare`, `reset` and `process`; the latency and readings methods have default implementations.

```cpp
#include <cydaw/engine/AudioProcessor.h>
void prepare(const cydaw::engine::ProcessSpec& spec);
void reset() noexcept;
void process(cydaw::engine::AudioBlockView input,
             cydaw::engine::AudioBlockView output,
             const cydaw::engine::ProcessContext& context) noexcept;
std::uint32_t latencySamples() const noexcept;
std::uint32_t maximumLatencySamples() const noexcept;
std::size_t readings(float* into, std::size_t count) const noexcept;
```

## Parameters and results {#parameters}
- `ProcessSpec` supplies `sampleRate` in hertz, `maximumBlockSize` in frames and `channelCount`. Allocate reusable resources during `prepare`.
- `input` and `output` are non-owning `AudioBlockView` values; their storage must outlive the callback. `process` returns no value and writes output audio.
- `context.transport` is the transport snapshot for the block, declared by `<cydaw/engine/Transport.h>`.
- `latencySamples` returns a sample count. Its value must never exceed `maximumLatencySamples` after preparation.
- `readings(into, count)` writes at most `count` display values and returns the number written. Reading shared state concurrently requires suitable synchronisation without blocking the audio thread.

## Preparation example {#example}
Given an existing processor instance named `processor`, prepare it before processing buffers:

```cpp
cydaw::engine::ProcessSpec spec;
spec.sampleRate = 48000.0;
spec.maximumBlockSize = 512;
spec.channelCount = 2;
processor.prepare(spec);
processor.reset();
```

`AudioBlockView` is declared in `<cydaw/engine/AudioBuffer.h>`. For processors with distinct input ports, `MultiBusAudioProcessor::processAudioBuses(std::span<const AudioInputBusView>, AudioBlockView, const ProcessContext&) noexcept` receives buses whose `portId` identifies the authored socket. See [routing](doc:mixing#sidechain).
