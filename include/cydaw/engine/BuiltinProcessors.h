#pragma once

#include <cydaw/engine/WavetableShape.h>

#include <cydaw/engine/AudioProcessor.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cydaw::engine {

class PassthroughProcessor final : public AudioProcessor {
public:
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(AudioBlockView input, AudioBlockView output, const ProcessContext& context) noexcept override;
};

class MixerProcessor final : public AudioProcessor,
                             public MultiBusAudioProcessor {
public:
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(AudioBlockView input, AudioBlockView output,
                 const ProcessContext& context) noexcept override;
    void processAudioBuses(
        std::span<const AudioInputBusView> inputs, AudioBlockView output,
        const ProcessContext& context) noexcept override;
};

class GainProcessor final : public AudioProcessor {
public:
    explicit GainProcessor(float initialGain = 1.0f) noexcept;

    void setGainLinear(float gain) noexcept;
    [[nodiscard]] float gainLinear() const noexcept;

    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(AudioBlockView input, AudioBlockView output, const ProcessContext& context) noexcept override;

private:
    std::atomic<float> targetGain_ { 1.0f };
    float currentGain_ { 1.0f };
};

class SineOscillatorProcessor final : public AudioProcessor {
public:
    explicit SineOscillatorProcessor(float frequencyHz = 440.0f,
                                     float waveform = 0.0f,
                                     float level = 1.0f) noexcept;

    void setFrequency(float frequencyHz) noexcept;
    void setWaveform(float waveform) noexcept;
    void setLevel(float level) noexcept;
    [[nodiscard]] float frequency() const noexcept;
    [[nodiscard]] float waveform() const noexcept;
    [[nodiscard]] float level() const noexcept;

    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(AudioBlockView input, AudioBlockView output, const ProcessContext& context) noexcept override;

private:
    std::atomic<float> frequencyHz_ { 440.0f };
    std::atomic<float> waveform_ {};
    std::atomic<float> level_ { 1.0f };
    double sampleRate_ { 48'000.0 };
    double phase_ {};
};

class NoiseProcessor final : public AudioProcessor {
public:
    explicit NoiseProcessor(float level = 0.08f) noexcept;
    void setLevel(float level) noexcept;
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(AudioBlockView input, AudioBlockView output,
                 const ProcessContext& context) noexcept override;

private:
    std::atomic<float> level_ { 0.08f };
    std::uint32_t state_ { 0x6d2b79f5u };
};

// A wavetable oscillator: a bank of single-cycle frames, each with more
// harmonics than the last, read at a position that morphs between them. Every
// frame is built by additive synthesis, so it is band-limited by construction
// and the morph changes the shape of the spectrum rather than its level.
// Three detuned voices are summed, which is what gives the family its width.
class WavetableOscillatorProcessor final : public AudioProcessor {
public:
    explicit WavetableOscillatorProcessor(float frequencyHz = 220.0f,
                                          float position = 0.35f,
                                          float level = 0.2f,
                                          float detuneCents = 8.0f) noexcept;
    void setFrequencyHz(float frequencyHz) noexcept;
    void setPosition(float position) noexcept;
    void setLevel(float level) noexcept;
    void setDetuneCents(float detuneCents) noexcept;
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(AudioBlockView input, AudioBlockView output,
                 const ProcessContext& context) noexcept override;

    static constexpr std::size_t frameCount = 8;
    static constexpr std::size_t frameLength = 2'048;

    // The same bank, readable without owning a processor: the polyphonic
    // voice renders a wavetable note without instantiating one per voice.
    [[nodiscard]] static float readTable(float position, double phase) noexcept;
    // The same reading, from a table the user shaped. A null bank means the
    // built-in one, so nothing that never opened the editor changes.
    [[nodiscard]] static float readTable(const WavetableBank* bank,
                                         float position, double phase) noexcept;
    void setBank(WavetableBankRef bank) noexcept;
    // Only ever written before publication, so the realtime parameter path
    // can read it to decide whether it may proceed.
    [[nodiscard]] const WavetableBank* bank() const noexcept
    {
        return bank_.get();
    }

private:
    [[nodiscard]] static const std::array<
        std::array<float, frameLength>, frameCount>& table();
    [[nodiscard]] float sample(float position, double phase) const noexcept;

    std::atomic<float> frequencyHz_ { 220.0f };
    std::atomic<float> position_ { 0.35f };
    std::atomic<float> level_ { 0.2f };
    std::atomic<float> detuneCents_ { 8.0f };
    // Replaced whole from the message thread and only read on the audio
    // thread, so the render never sees a half-written table.
    std::shared_ptr<const WavetableBank> bank_;
    double sampleRate_ { 48'000.0 };
    std::array<double, 3> phases_ {};
};

// A waveguide string. It is a resonator, not a generator: feed it an
// excitation — a noise burst, a click, anything with energy — and it rings at
// its frequency while the loop filter takes the highs away first, the way a
// real string does.
class StringResonatorProcessor final : public AudioProcessor {
public:
    explicit StringResonatorProcessor(float frequencyHz = 220.0f,
                                      float damping = 0.35f,
                                      float decaySeconds = 4.0f) noexcept;
    void setFrequencyHz(float frequencyHz) noexcept;
    void setDamping(float damping) noexcept;
    void setDecaySeconds(float decaySeconds) noexcept;
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(AudioBlockView input, AudioBlockView output,
                 const ProcessContext& context) noexcept override;

private:
    struct Loop {
        std::vector<float> line;
        std::size_t writeIndex {};
        float lowpassState {};
        float dcState {};
        float dcPrevious {};
    };

    std::atomic<float> frequencyHz_ { 220.0f };
    std::atomic<float> damping_ { 0.35f };
    std::atomic<float> decaySeconds_ { 4.0f };
    double sampleRate_ { 48'000.0 };
    std::vector<Loop> loops_;
};

class DriveProcessor final : public AudioProcessor {
public:
    explicit DriveProcessor(float driveDb = 0.0f) noexcept;
    void setDriveDb(float driveDb) noexcept;
    [[nodiscard]] float driveDb() const noexcept;
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(AudioBlockView input, AudioBlockView output,
                 const ProcessContext& context) noexcept override;

private:
    std::atomic<float> driveDb_ {};
};

class OnePoleLowPassProcessor final : public AudioProcessor {
public:
    explicit OnePoleLowPassProcessor(float cutoffHz = 20'000.0f,
                                     float resonance = 0.707f) noexcept;
    void setCutoffHz(float cutoffHz) noexcept;
    [[nodiscard]] float cutoffHz() const noexcept;
    void setResonance(float resonance) noexcept;
    [[nodiscard]] float resonance() const noexcept;
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(AudioBlockView input, AudioBlockView output,
                 const ProcessContext& context) noexcept override;

private:
    std::atomic<float> cutoffHz_ { 20'000.0f };
    std::atomic<float> resonance_ { 0.707f };
    double sampleRate_ { 48'000.0 };
    std::vector<float> integrator1_;
    std::vector<float> integrator2_;
};

// A single parametric band: peak/bell at `frequency`, `gain` dB of boost or
// cut, `q` controlling how wide the bell is. Coefficients follow the standard
// RBJ peaking-EQ recipe and are recomputed once per block, so the three
// controls stay lock-free atomics for live editing and host automation.
class PeakingEqProcessor final : public AudioProcessor {
public:
    explicit PeakingEqProcessor(float frequencyHz = 1'000.0f,
                                float gainDb = 0.0f,
                                float q = 1.0f) noexcept;
    void setFrequencyHz(float frequencyHz) noexcept;
    [[nodiscard]] float frequencyHz() const noexcept;
    void setGainDb(float gainDb) noexcept;
    [[nodiscard]] float gainDb() const noexcept;
    void setQ(float q) noexcept;
    [[nodiscard]] float q() const noexcept;
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(AudioBlockView input, AudioBlockView output,
                 const ProcessContext& context) noexcept override;

private:
    struct BiquadState {
        float x1 {};
        float x2 {};
        float y1 {};
        float y2 {};
    };
    std::atomic<float> frequencyHz_ { 1'000.0f };
    std::atomic<float> gainDb_ {};
    std::atomic<float> q_ { 1.0f };
    double sampleRate_ { 48'000.0 };
    std::vector<BiquadState> state_;
};

class OnePoleHighPassProcessor final : public AudioProcessor {
public:
    explicit OnePoleHighPassProcessor(float cutoffHz = 20.0f) noexcept;
    void setCutoffHz(float cutoffHz) noexcept;
    [[nodiscard]] float cutoffHz() const noexcept;
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(AudioBlockView input, AudioBlockView output,
                 const ProcessContext& context) noexcept override;

private:
    std::atomic<float> cutoffHz_ { 20.0f };
    double sampleRate_ { 48'000.0 };
    std::vector<float> lowState_;
};

class HardClipProcessor final : public AudioProcessor {
public:
    explicit HardClipProcessor(float threshold = 1.0f) noexcept;
    void setThreshold(float threshold) noexcept;
    [[nodiscard]] float threshold() const noexcept;
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(AudioBlockView input, AudioBlockView output,
                 const ProcessContext& context) noexcept override;

private:
    std::atomic<float> threshold_ { 1.0f };
};

class StereoWidthProcessor final : public AudioProcessor {
public:
    explicit StereoWidthProcessor(float width = 1.0f) noexcept;
    void setWidth(float width) noexcept;
    [[nodiscard]] float width() const noexcept;
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(AudioBlockView input, AudioBlockView output,
                 const ProcessContext& context) noexcept override;

private:
    std::atomic<float> width_ { 1.0f };
};

class PanProcessor final : public AudioProcessor {
public:
    explicit PanProcessor(float pan = 0.0f) noexcept;
    void setPan(float pan) noexcept;
    [[nodiscard]] float pan() const noexcept;
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(AudioBlockView input, AudioBlockView output,
                 const ProcessContext& context) noexcept override;

private:
    std::atomic<float> pan_ {};
};

// Audio-transparent detector used by control-rate Graph modulation. The
// envelope is exposed atomically so a block-rate control plan can consume the
// previous completed block without locking the audio thread.
class EnvelopeFollowerProcessor final : public AudioProcessor {
public:
    EnvelopeFollowerProcessor(float attackSeconds = 0.01f,
                              float releaseSeconds = 0.1f) noexcept;
    void setAttackSeconds(float seconds) noexcept;
    void setReleaseSeconds(float seconds) noexcept;
    [[nodiscard]] float envelope() const noexcept;
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(AudioBlockView input, AudioBlockView output,
                 const ProcessContext& context) noexcept override;

private:
    std::atomic<float> attackSeconds_ { 0.01f };
    std::atomic<float> releaseSeconds_ { 0.1f };
    std::atomic<float> publishedEnvelope_ {};
    double sampleRate_ { 48'000.0 };
    float envelope_ {};
};

class DcBlockerProcessor final : public AudioProcessor {
public:
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(AudioBlockView input, AudioBlockView output,
                 const ProcessContext& context) noexcept override;

private:
    std::vector<float> previousInput_;
    std::vector<float> previousOutput_;
};

class DelayProcessor final : public AudioProcessor {
public:
    DelayProcessor(float delayMilliseconds = 260.0f,
                   float feedback = 0.32f,
                   float mix = 0.28f) noexcept;
    void setDelayMilliseconds(float delayMilliseconds) noexcept;
    void setFeedback(float feedback) noexcept;
    void setMix(float mix) noexcept;
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(AudioBlockView input, AudioBlockView output,
                 const ProcessContext& context) noexcept override;

private:
    std::atomic<float> delayMilliseconds_ { 260.0f };
    std::atomic<float> feedback_ { 0.32f };
    std::atomic<float> mix_ { 0.28f };
    double sampleRate_ { 48'000.0 };
    std::uint32_t writePosition_ {};
    AudioBuffer delayBuffer_;
};

class ReverbProcessor final : public AudioProcessor {
public:
    explicit ReverbProcessor(float roomSize = 0.68f, float mix = 0.24f) noexcept;
    void setRoomSize(float roomSize) noexcept;
    void setMix(float mix) noexcept;
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(AudioBlockView input, AudioBlockView output,
                 const ProcessContext& context) noexcept override;

private:
    std::atomic<float> roomSize_ { 0.68f };
    std::atomic<float> mix_ { 0.24f };
    std::vector<AudioBuffer> combBuffers_;
    std::vector<std::uint32_t> combPositions_;
    std::vector<float> dampingState_;
};

class CompressorProcessor final : public AudioProcessor {
public:
    CompressorProcessor(float thresholdDb = -16.0f, float ratio = 4.0f) noexcept;
    void setThresholdDb(float thresholdDb) noexcept;
    void setRatio(float ratio) noexcept;
    // How much the last block was held back, in decibels: 0 when the
    // compressor is idle, negative while it works. Published on a value
    // output so the graph can read it like any other control signal.
    [[nodiscard]] float gainReductionDb() const noexcept;
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(AudioBlockView input, AudioBlockView output,
                 const ProcessContext& context) noexcept override;

private:
    std::atomic<float> thresholdDb_ { -16.0f };
    std::atomic<float> ratio_ { 4.0f };
    std::atomic<float> gainReductionDb_ {};
    double sampleRate_ { 48'000.0 };
    float envelope_ {};
};

class ToneProcessor final : public AudioProcessor {
public:
    explicit ToneProcessor(float amount = 0.18f) noexcept;
    void setAmount(float amount) noexcept;
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(AudioBlockView input, AudioBlockView output,
                 const ProcessContext& context) noexcept override;

private:
    std::atomic<float> amount_ { 0.18f };
    double sampleRate_ { 48'000.0 };
    std::vector<float> lowState_;
};

class VocalStripProcessor final : public AudioProcessor {
public:
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(AudioBlockView input, AudioBlockView output,
                 const ProcessContext& context) noexcept override;

private:
    double sampleRate_ { 48'000.0 };
    std::vector<float> lowState_;
    float envelope_ {};
};

} // namespace cydaw::engine
