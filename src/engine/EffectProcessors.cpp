#include <cydaw/engine/EffectProcessors.h>

#include <cydaw/engine/AudioBuffer.h>
#include <cydaw/engine/BiquadDesign.h>
#include <cydaw/engine/ProEffects.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace cydaw::engine {

EffectProcessor::EffectProcessor(const EffectDescriptor& descriptor) noexcept : descriptor_(descriptor)
{
    for (std::size_t index = 0; index < descriptor.parameters.size() && index < values_.size(); ++index)
        values_[index].store(descriptor.parameters[index].defaultValue, std::memory_order_relaxed);
}

void EffectProcessor::setParameters(const NativeDspNodeParameters& parameters) noexcept
{
    for (std::size_t index = 0; index < descriptor_.parameters.size() && index < values_.size(); ++index) {
        const auto& parameter = descriptor_.parameters[index];
        if (parameter.key.empty())
            continue;
        auto next = effectValue(parameters, parameter);
        if (!std::isfinite(next))
            next = parameter.defaultValue;
        values_[index].store(std::clamp(next, parameter.minimum, parameter.maximum), std::memory_order_relaxed);
    }
}

namespace {

constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;

float dbToGain(const float db) noexcept
{
    return std::exp(db * 0.115129255f);
}

float gainToDb(const float gain) noexcept
{
    return 20.0f * std::log10(std::max(gain, 1.0e-9f));
}

float wrap(const float phase) noexcept
{
    return phase - std::floor(phase);
}

// An LFO shape at a phase in [0, 1): sine, triangle, saw, a rounded square.
float shapeAt(const int shape, const float phase) noexcept
{
    switch (shape) {
    case 1: return 1.0f - 4.0f * std::abs(phase - 0.5f);
    case 2: return phase * 2.0f - 1.0f;
    case 3: return std::tanh(6.0f * std::sin(kTwoPi * phase));
    default: return std::sin(kTwoPi * phase);
    }
}

// A rate in Hz, or one cycle per note value when synced.
float syncedRate(const float rate, const float sync, const ProcessContext& context) noexcept
{
    if (const auto* division = noteDivisionFor(sync))
        return static_cast<float>(std::clamp(context.transport.beatsPerMinute, 20.0, 400.0) / 60.0 / division->beats);
    return rate;
}

// A time in milliseconds, or a note value's length when synced.
float syncedTime(const float milliseconds, const float sync, const ProcessContext& context) noexcept
{
    if (const auto* division = noteDivisionFor(sync))
        return static_cast<float>(std::min(2'000.0, division->beats * 60'000.0
                                                        / std::clamp(context.transport.beatsPerMinute, 20.0, 400.0)));
    return milliseconds;
}

// A delay line per channel with a shared write position, read at a
// fractional distance behind it.
class DelayLines {
public:
    void prepare(const std::uint32_t channels, const std::uint32_t length)
    {
        buffer_.prepare(std::max<std::uint32_t>(1, channels), std::max<std::uint32_t>(4, length));
        clear();
    }
    void clear() noexcept
    {
        buffer_.clear(buffer_.maximumFrameCount());
        write_ = 0;
    }
    [[nodiscard]] std::uint32_t length() const noexcept { return buffer_.maximumFrameCount(); }
    [[nodiscard]] std::uint32_t channels() const noexcept { return buffer_.channelCount(); }
    // Behind the write position by `distance` samples (at least 1).
    [[nodiscard]] float read(const std::uint32_t channel, float distance) noexcept
    {
        const auto size = length();
        distance = std::clamp(distance, 1.0f, static_cast<float>(size - 3));
        auto position = static_cast<float>(write_) - distance;
        if (position < 0.0f)
            position += static_cast<float>(size);
        const auto index = static_cast<std::uint32_t>(position);
        const auto fraction = position - static_cast<float>(index);
        const auto* data = buffer_.view(size).channel(channel);
        const auto a = data[index % size];
        const auto b = data[(index + 1) % size];
        return a + (b - a) * fraction;
    }
    void write(const std::uint32_t channel, const float sample) noexcept
    {
        buffer_.view(length()).channel(channel)[write_] = sample;
    }
    void advance() noexcept
    {
        if (++write_ >= length())
            write_ = 0;
    }

private:
    AudioBuffer buffer_;
    std::uint32_t write_ {};
};

// The topology-preserving state-variable filter the built-in low-pass runs,
// with all four outputs.
struct StateVariable {
    float ic1 {};
    float ic2 {};

    struct Outputs {
        float low;
        float band;
        float high;
        float notch;
    };
    struct Coefficients {
        float a1 {};
        float a2 {};
        float a3 {};
        float k { 1.0f };
    };
    [[nodiscard]] static Coefficients design(const double sampleRate, const float cutoff, const float q) noexcept
    {
        const auto limited = std::clamp(cutoff, 10.0f, static_cast<float>(sampleRate * 0.45));
        const auto g = static_cast<float>(std::tan(std::numbers::pi * limited / sampleRate));
        Coefficients c;
        c.k = 1.0f / std::max(0.05f, q);
        c.a1 = 1.0f / (1.0f + g * (g + c.k));
        c.a2 = g * c.a1;
        c.a3 = g * c.a2;
        return c;
    }
    Outputs process(const Coefficients& c, const float x) noexcept
    {
        const auto v3 = x - ic2;
        const auto v1 = c.a1 * ic1 + c.a2 * v3;
        const auto v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return { v2, v1, x - c.k * v1 - v2, x - c.k * v1 };
    }
};

// ---- Chorus and flanger -------------------------------------------------------------------------

// A modulated delay: a chorus has a longer delay and no feedback, a flanger a
// short one with feedback. Values: rate, depth, delay, [width|feedback], mix.
class ModulatedDelay final : public EffectProcessor {
public:
    ModulatedDelay(const EffectDescriptor& descriptor, const bool flanger) noexcept
        : EffectProcessor(descriptor), flanger_(flanger)
    {
    }
    void prepare(const ProcessSpec& spec) override
    {
        sampleRate_ = std::max(1.0, spec.sampleRate);
        lines_.prepare(spec.channelCount, static_cast<std::uint32_t>(std::ceil(sampleRate_ * 0.1)) + 8);
        feedback_.assign(spec.channelCount, 0.0f);
        reset();
    }
    void reset() noexcept override
    {
        lines_.clear();
        std::fill(feedback_.begin(), feedback_.end(), 0.0f);
        phase_ = 0.0f;
        smoothedDelay_ = -1.0f;
    }
    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext&) noexcept override
    {
        clearAudio(output);
        const auto channels = std::min({ input.channelCount, output.channelCount, lines_.channels(),
                                         static_cast<std::uint32_t>(feedback_.size()) });
        const auto frames = std::min(input.frameCount, output.frameCount);
        const auto rate = value(0);
        const auto depth = value(1);
        const auto delayMs = value(2);
        const auto fourth = value(3);
        const auto mix = value(4);
        const auto increment = static_cast<float>(rate / sampleRate_);
        const auto msToSamples = static_cast<float>(sampleRate_ / 1'000.0);
        if (smoothedDelay_ < 0.0f)
            smoothedDelay_ = delayMs;
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            phase_ = wrap(phase_ + increment);
            smoothedDelay_ += 0.0015f * (delayMs - smoothedDelay_);
            for (std::uint32_t channel = 0; channel < channels; ++channel) {
                // The right channel a quarter cycle on: a chorus by its width.
                const auto offset = channel % 2 == 1 ? (flanger_ ? 0.25f : 0.25f * fourth) : 0.0f;
                const auto lfo = std::sin(kTwoPi * (phase_ + offset));
                const auto sweep = flanger_ ? smoothedDelay_ * (1.0f + depth * lfo) : smoothedDelay_ * (1.0f + 0.5f * depth * lfo);
                const auto distance = std::max(0.05f, sweep) * msToSamples;
                const auto dry = input.channel(channel)[frame];
                const auto wet = lines_.read(channel, distance);
                const auto feedback = flanger_ ? fourth : 0.0f;
                lines_.write(channel, dry + std::clamp(wet * feedback, -4.0f, 4.0f));
                output.channel(channel)[frame] = dry * (1.0f - mix) + wet * mix;
            }
            lines_.advance();
        }
        // Channels past the lines pass dry.
        for (std::uint32_t channel = channels; channel < std::min(input.channelCount, output.channelCount); ++channel)
            std::copy_n(input.channel(channel), frames, output.channel(channel));
    }

private:
    bool flanger_ {};
    double sampleRate_ { 48'000.0 };
    DelayLines lines_;
    std::vector<float> feedback_;
    float phase_ {};
    float smoothedDelay_ { -1.0f };
};

// ---- Phaser -------------------------------------------------------------------------------------

// Six first-order all-passes swept around a centre, with feedback. Values:
// rate, depth, frequency, feedback, mix.
class Phaser final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;
    static constexpr int stages = 6;
    void prepare(const ProcessSpec& spec) override
    {
        sampleRate_ = std::max(1.0, spec.sampleRate);
        states_.assign(spec.channelCount, {});
        reset();
    }
    void reset() noexcept override
    {
        for (auto& state : states_)
            state = {};
        phase_ = 0.0f;
    }
    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext&) noexcept override
    {
        clearAudio(output);
        const auto channels = std::min({ input.channelCount, output.channelCount, static_cast<std::uint32_t>(states_.size()) });
        const auto frames = std::min(input.frameCount, output.frameCount);
        const auto increment = static_cast<float>(value(0) / sampleRate_);
        const auto depth = value(1);
        const auto centre = value(2);
        const auto feedback = value(3);
        const auto mix = value(4);
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            phase_ = wrap(phase_ + increment);
            for (std::uint32_t channel = 0; channel < channels; ++channel) {
                auto& state = states_[channel];
                // The coefficient every eight samples: a tangent per sample
                // per channel is more than the sweep needs.
                if ((frame & 7u) == 0u) {
                    const auto lfo = std::sin(kTwoPi * (phase_ + (channel % 2 == 1 ? 0.25f : 0.0f)));
                    const auto frequency = std::clamp(centre * std::exp2(depth * 2.0f * lfo), 20.0f,
                                                      static_cast<float>(sampleRate_ * 0.45));
                    const auto t = static_cast<float>(std::tan(std::numbers::pi * frequency / sampleRate_));
                    state.coefficient = (t - 1.0f) / (t + 1.0f);
                }
                const auto dry = input.channel(channel)[frame];
                auto x = dry + feedback * state.last;
                for (auto stage = 0; stage < stages; ++stage) {
                    const auto y = state.coefficient * x + state.x1[stage] - state.coefficient * state.y1[stage];
                    state.x1[stage] = x;
                    state.y1[stage] = y;
                    x = y;
                }
                state.last = std::clamp(x, -4.0f, 4.0f);
                output.channel(channel)[frame] = dry * (1.0f - mix) + x * mix;
            }
        }
        for (std::uint32_t channel = channels; channel < std::min(input.channelCount, output.channelCount); ++channel)
            std::copy_n(input.channel(channel), frames, output.channel(channel));
    }

private:
    struct State {
        std::array<float, stages> x1 {};
        std::array<float, stages> y1 {};
        float coefficient {};
        float last {};
    };
    double sampleRate_ { 48'000.0 };
    std::vector<State> states_;
    float phase_ {};
};

// ---- Tremolo ------------------------------------------------------------------------------------

// The level swung by an LFO; with spread the two sides swing apart until
// the sound pans. Values: rate, depth, shape, spread.
class Tremolo final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;
    void prepare(const ProcessSpec& spec) override
    {
        sampleRate_ = std::max(1.0, spec.sampleRate);
        reset();
    }
    void reset() noexcept override { phase_ = 0.0f; }
    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext& context) noexcept override
    {
        clearAudio(output);
        const auto channels = std::min(input.channelCount, output.channelCount);
        const auto frames = std::min(input.frameCount, output.frameCount);
        const auto increment = static_cast<float>(syncedRate(value(0), value(4), context) / sampleRate_);
        const auto depth = value(1);
        const auto shape = static_cast<int>(std::lround(value(2)));
        const auto spread = value(3);
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            phase_ = wrap(phase_ + increment);
            for (std::uint32_t channel = 0; channel < channels; ++channel) {
                const auto phase = wrap(phase_ + (channel % 2 == 1 ? 0.5f * spread : 0.0f));
                const auto gain = 1.0f - depth * (0.5f - 0.5f * shapeAt(shape, phase));
                output.channel(channel)[frame] = input.channel(channel)[frame] * gain;
            }
        }
    }

private:
    double sampleRate_ { 48'000.0 };
    float phase_ {};
};

// ---- Bitcrusher ---------------------------------------------------------------------------------

// Fewer bits and a lower rate: each sample quantised, held for `downsample`
// samples. Values: bits, downsample, drive, mix.
class Bitcrusher final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;
    void prepare(const ProcessSpec& spec) override
    {
        held_.assign(spec.channelCount, 0.0f);
        reset();
    }
    void reset() noexcept override
    {
        std::fill(held_.begin(), held_.end(), 0.0f);
        counter_ = 1.0e9f;
    }
    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext&) noexcept override
    {
        clearAudio(output);
        const auto channels = std::min({ input.channelCount, output.channelCount, static_cast<std::uint32_t>(held_.size()) });
        const auto frames = std::min(input.frameCount, output.frameCount);
        const auto levels = std::exp2(std::max(0.0f, value(0) - 1.0f));
        const auto hold = std::max(1.0f, value(1));
        const auto drive = dbToGain(value(2));
        const auto mix = value(3);
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            counter_ += 1.0f;
            const auto sample = counter_ >= hold;
            if (sample)
                counter_ = std::fmod(counter_, hold);
            for (std::uint32_t channel = 0; channel < channels; ++channel) {
                const auto dry = input.channel(channel)[frame];
                if (sample) {
                    const auto driven = std::clamp(dry * drive, -1.0f, 1.0f);
                    held_[channel] = std::round(driven * levels) / levels;
                }
                output.channel(channel)[frame] = dry * (1.0f - mix) + held_[channel] * mix;
            }
        }
        for (std::uint32_t channel = channels; channel < std::min(input.channelCount, output.channelCount); ++channel)
            std::copy_n(input.channel(channel), frames, output.channel(channel));
    }

private:
    std::vector<float> held_;
    float counter_ {};
};

// ---- Filters ------------------------------------------------------------------------------------

// One state-variable filter: low-pass, high-pass, band-pass or notch, with
// an optional drive before it. Values: mode, cutoff, resonance, drive.
class MultimodeFilter final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;
    void prepare(const ProcessSpec& spec) override
    {
        sampleRate_ = std::max(1.0, spec.sampleRate);
        filters_.assign(spec.channelCount, {});
        reset();
    }
    void reset() noexcept override
    {
        for (auto& filter : filters_)
            filter = {};
    }
    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext&) noexcept override
    {
        clearAudio(output);
        const auto channels = std::min({ input.channelCount, output.channelCount, static_cast<std::uint32_t>(filters_.size()) });
        const auto frames = std::min(input.frameCount, output.frameCount);
        const auto mode = static_cast<int>(std::lround(value(0)));
        const auto coefficients = StateVariable::design(sampleRate_, value(1), value(2));
        const auto driveDb = value(3);
        const auto drive = dbToGain(driveDb);
        const auto normalise = driveDb > 0.01f ? 1.0f / std::tanh(drive) : 1.0f;
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            auto& filter = filters_[channel];
            for (std::uint32_t frame = 0; frame < frames; ++frame) {
                auto x = input.channel(channel)[frame];
                if (driveDb > 0.01f)
                    x = std::tanh(x * drive) * normalise;
                const auto out = filter.process(coefficients, x);
                output.channel(channel)[frame] = mode == 1 ? out.high : mode == 2 ? out.band : mode == 3 ? out.notch : out.low;
            }
        }
        for (std::uint32_t channel = channels; channel < std::min(input.channelCount, output.channelCount); ++channel)
            std::copy_n(input.channel(channel), frames, output.channel(channel));
    }

private:
    double sampleRate_ { 48'000.0 };
    std::vector<StateVariable> filters_;
};

// A resonant low-pass whose cutoff an LFO sweeps up to four octaves either
// way. Values: cutoff, resonance, rate, depth.
class AutoFilter final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;
    void prepare(const ProcessSpec& spec) override
    {
        sampleRate_ = std::max(1.0, spec.sampleRate);
        filters_.assign(spec.channelCount, {});
        reset();
    }
    void reset() noexcept override
    {
        for (auto& filter : filters_)
            filter = {};
        phase_ = 0.0f;
    }
    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext& context) noexcept override
    {
        clearAudio(output);
        const auto channels = std::min({ input.channelCount, output.channelCount, static_cast<std::uint32_t>(filters_.size()) });
        const auto frames = std::min(input.frameCount, output.frameCount);
        const auto cutoff = value(0);
        const auto resonance = value(1);
        const auto increment = static_cast<float>(syncedRate(value(2), value(4), context) / sampleRate_);
        const auto depth = value(3);
        StateVariable::Coefficients coefficients;
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            phase_ = wrap(phase_ + increment);
            if ((frame & 15u) == 0u)
                coefficients = StateVariable::design(sampleRate_, cutoff * std::exp2(depth * 4.0f * std::sin(kTwoPi * phase_)),
                                                     resonance);
            for (std::uint32_t channel = 0; channel < channels; ++channel)
                output.channel(channel)[frame] = filters_[channel].process(coefficients, input.channel(channel)[frame]).low;
        }
        for (std::uint32_t channel = channels; channel < std::min(input.channelCount, output.channelCount); ++channel)
            std::copy_n(input.channel(channel), frames, output.channel(channel));
    }

private:
    double sampleRate_ { 48'000.0 };
    std::vector<StateVariable> filters_;
    float phase_ {};
};

// Low shelf, a bell between the two crossovers, high shelf. Values: low, mid,
// high (dB), low frequency, high frequency.
class ThreeBandEq final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;
    void prepare(const ProcessSpec& spec) override
    {
        sampleRate_ = std::max(1.0, spec.sampleRate);
        states_.assign(spec.channelCount, {});
        designed_ = {};
        reset();
    }
    void reset() noexcept override
    {
        for (auto& state : states_)
            state = {};
    }
    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext&) noexcept override
    {
        clearAudio(output);
        const auto channels = std::min({ input.channelCount, output.channelCount, static_cast<std::uint32_t>(states_.size()) });
        const auto frames = std::min(input.frameCount, output.frameCount);
        const std::array<float, 5> wanted { value(0), value(1), value(2), value(3), value(4) };
        if (wanted != designed_) {
            designed_ = wanted;
            design();
        }
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            auto& state = states_[channel];
            for (std::uint32_t frame = 0; frame < frames; ++frame) {
                auto x = input.channel(channel)[frame];
                for (std::size_t band = 0; band < 3; ++band)
                    x = run(bands_[band], state[band], x);
                output.channel(channel)[frame] = x;
            }
        }
        for (std::uint32_t channel = channels; channel < std::min(input.channelCount, output.channelCount); ++channel)
            std::copy_n(input.channel(channel), frames, output.channel(channel));
    }

private:
    struct Biquad {
        float b0 { 1.0f };
        float b1 {};
        float b2 {};
        float a1 {};
        float a2 {};
    };
    struct BiquadState {
        float z1 {};
        float z2 {};
    };
    static float run(const Biquad& c, BiquadState& s, const float x) noexcept
    {
        const auto y = c.b0 * x + s.z1;
        s.z1 = c.b1 * x - c.a1 * y + s.z2;
        s.z2 = c.b2 * x - c.a2 * y;
        return y;
    }
    // The RBJ cookbook's shelves and bell.
    void design() noexcept
    {
        const auto shelf = [this](const float frequency, const float gainDb, const bool high) {
            const auto a = std::pow(10.0, gainDb / 40.0);
            const auto w0 = 2.0 * std::numbers::pi * std::min<double>(frequency, sampleRate_ * 0.45) / sampleRate_;
            const auto cosine = std::cos(w0);
            const auto alpha = std::sin(w0) / 2.0 * std::sqrt(2.0);
            const auto root = 2.0 * std::sqrt(a) * alpha;
            double b0, b1, b2, a0, a1, a2;
            if (!high) {
                b0 = a * ((a + 1.0) - (a - 1.0) * cosine + root);
                b1 = 2.0 * a * ((a - 1.0) - (a + 1.0) * cosine);
                b2 = a * ((a + 1.0) - (a - 1.0) * cosine - root);
                a0 = (a + 1.0) + (a - 1.0) * cosine + root;
                a1 = -2.0 * ((a - 1.0) + (a + 1.0) * cosine);
                a2 = (a + 1.0) + (a - 1.0) * cosine - root;
            } else {
                b0 = a * ((a + 1.0) + (a - 1.0) * cosine + root);
                b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * cosine);
                b2 = a * ((a + 1.0) + (a - 1.0) * cosine - root);
                a0 = (a + 1.0) - (a - 1.0) * cosine + root;
                a1 = 2.0 * ((a - 1.0) - (a + 1.0) * cosine);
                a2 = (a + 1.0) - (a - 1.0) * cosine - root;
            }
            return Biquad { static_cast<float>(b0 / a0), static_cast<float>(b1 / a0), static_cast<float>(b2 / a0),
                            static_cast<float>(a1 / a0), static_cast<float>(a2 / a0) };
        };
        const auto bell = [this](const float frequency, const float gainDb, const float q) {
            const auto a = std::pow(10.0, gainDb / 40.0);
            const auto w0 = 2.0 * std::numbers::pi * std::min<double>(frequency, sampleRate_ * 0.45) / sampleRate_;
            const auto cosine = std::cos(w0);
            const auto alpha = std::sin(w0) / (2.0 * q);
            const auto a0 = 1.0 + alpha / a;
            return Biquad { static_cast<float>((1.0 + alpha * a) / a0), static_cast<float>(-2.0 * cosine / a0),
                            static_cast<float>((1.0 - alpha * a) / a0), static_cast<float>(-2.0 * cosine / a0),
                            static_cast<float>((1.0 - alpha / a) / a0) };
        };
        const auto low = designed_[3];
        const auto high = std::max(designed_[4], low * 1.5f);
        bands_[0] = shelf(low, designed_[0], false);
        bands_[1] = bell(std::sqrt(low * high), designed_[1], 0.7f);
        bands_[2] = shelf(high, designed_[2], true);
    }

    double sampleRate_ { 48'000.0 };
    std::array<Biquad, 3> bands_ {};
    std::array<float, 5> designed_ {};
    std::vector<std::array<BiquadState, 3>> states_;
};

// Eight bands of the shapes BiquadDesign.h draws, the cuts up to 48 dB per
// octave, then the output gain. Values: per band enabled, type, frequency,
// gain, Q and slope (eqProSlot), then the output. A moved frequency, gain or
// Q glides there over a few milliseconds so a dragged band does not click.
class ParametricEq final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;
    void prepare(const ProcessSpec& spec) override
    {
        sampleRate_ = std::max(1.0, spec.sampleRate);
        states_.assign(spec.channelCount, {});
        glide_ = 1.0 - std::exp(-static_cast<double>(kStep) / (0.015 * sampleRate_));
        reset();
    }
    void reset() noexcept override
    {
        for (auto& channel : states_)
            for (auto& band : channel)
                band = {};
        for (auto& band : bands_)
            band.primed = false;
        outputGain_ = -1.0;
    }
    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext&) noexcept override
    {
        clearAudio(output);
        const auto channels = std::min({ input.channelCount, output.channelCount, static_cast<std::uint32_t>(states_.size()) });
        const auto frames = std::min(input.frameCount, output.frameCount);
        const auto targetGain = static_cast<double>(dbToGain(value(static_cast<std::size_t>(eqProBands * eqProValuesPerBand))));
        if (outputGain_ < 0.0)
            outputGain_ = targetGain;
        for (std::uint32_t start = 0; start < frames; start += kStep) {
            const auto count = std::min(kStep, frames - start);
            refresh();
            const auto fromGain = outputGain_;
            outputGain_ += (targetGain - outputGain_) * glide_;
            const auto gainStep = (outputGain_ - fromGain) / static_cast<double>(count);
            for (std::uint32_t channel = 0; channel < channels; ++channel) {
                auto& state = states_[channel];
                const auto* source = input.channel(channel) + start;
                auto* target = output.channel(channel) + start;
                auto gain = fromGain;
                for (std::uint32_t frame = 0; frame < count; ++frame) {
                    auto x = static_cast<double>(source[frame]);
                    for (std::size_t band = 0; band < bands_.size(); ++band) {
                        const auto& design = bands_[band];
                        if (!design.enabled)
                            continue;
                        for (auto stage = 0; stage < design.stages; ++stage) {
                            const auto& c = design.sections[static_cast<std::size_t>(stage)];
                            auto& s = state[band][static_cast<std::size_t>(stage)];
                            const auto y = c.b0 * x + s.z1;
                            s.z1 = c.b1 * x - c.a1 * y + s.z2;
                            s.z2 = c.b2 * x - c.a2 * y;
                            x = y;
                        }
                    }
                    gain += gainStep;
                    target[frame] = static_cast<float>(x * gain);
                }
            }
        }
        for (std::uint32_t channel = channels; channel < std::min(input.channelCount, output.channelCount); ++channel)
            std::copy_n(input.channel(channel), frames, output.channel(channel));
    }

private:
    static constexpr std::uint32_t kStep = 32;
    struct Band {
        bool primed {};
        bool enabled {};
        EqBandType type { EqBandType::bell };
        int slope {};
        int stages { 1 };
        // Where the frequency (as its log), gain and Q (as its log) stand on
        // their way to the values set.
        double logFrequency {};
        double gainDb {};
        double logQ {};
        std::array<BiquadCoefficients, eqMaximumStages> sections {};
    };
    struct State {
        double z1 {};
        double z2 {};
    };

    float bandValue(const int band, const EqProValue which) const noexcept
    {
        return value(static_cast<std::size_t>(eqProSlot(band, which)));
    }

    // Follows the values set, designing again each band that moved.
    void refresh() noexcept
    {
        for (auto index = 0; index < eqProBands; ++index) {
            auto& band = bands_[static_cast<std::size_t>(index)];
            const auto enabled = bandValue(index, EqProValue::enabled) >= 0.5f;
            const auto type = static_cast<EqBandType>(
                std::clamp(static_cast<int>(std::lround(bandValue(index, EqProValue::type))), 0, eqBandTypeCount - 1));
            const auto slope = std::clamp(static_cast<int>(std::lround(bandValue(index, EqProValue::slope))), 0, eqSlopeCount - 1);
            const auto logFrequency = std::log(std::max(1.0, static_cast<double>(bandValue(index, EqProValue::frequency))));
            const auto gainDb = static_cast<double>(bandValue(index, EqProValue::gain));
            const auto logQ = std::log(std::max(0.01, static_cast<double>(bandValue(index, EqProValue::q))));
            auto moved = false;
            if (!band.primed || enabled != band.enabled || type != band.type || slope != band.slope) {
                // A new shape starts from silence rather than from the old
                // one's state, and at its values straight away.
                band.primed = true;
                band.enabled = enabled;
                band.type = type;
                band.slope = slope;
                band.stages = eqBandStages(type, slope);
                band.logFrequency = logFrequency;
                band.gainDb = gainDb;
                band.logQ = logQ;
                for (auto& channel : states_)
                    channel[static_cast<std::size_t>(index)] = {};
                moved = true;
            } else if (enabled) {
                const auto approach = [this, &moved](double& current, const double wanted, const double close) {
                    if (current == wanted)
                        return;
                    moved = true;
                    current += (wanted - current) * glide_;
                    if (std::abs(wanted - current) < close)
                        current = wanted;
                };
                approach(band.logFrequency, logFrequency, 1.0e-4);
                approach(band.gainDb, gainDb, 1.0e-3);
                approach(band.logQ, logQ, 1.0e-4);
            }
            if (!moved || !enabled)
                continue;
            const auto frequency = std::exp(band.logFrequency);
            const auto q = std::exp(band.logQ);
            for (auto stage = 0; stage < band.stages; ++stage)
                band.sections[static_cast<std::size_t>(stage)]
                    = designEqBand(type, frequency, band.gainDb, q, slope, stage, sampleRate_);
        }
    }

    double sampleRate_ { 48'000.0 };
    double glide_ { 0.05 };
    double outputGain_ { -1.0 };
    std::array<Band, eqProBands> bands_ {};
    std::vector<std::array<std::array<State, eqMaximumStages>, eqProBands>> states_;
};

// ---- Dynamics -----------------------------------------------------------------------------------

// A peak limiter: the signal raised by `gain`, held under the ceiling with an
// instant attack and the release given. Values: gain, ceiling, release.
class Limiter final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;
    void prepare(const ProcessSpec& spec) override
    {
        sampleRate_ = std::max(1.0, spec.sampleRate);
        reset();
    }
    void reset() noexcept override { gain_ = 1.0f; }
    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext&) noexcept override
    {
        clearAudio(output);
        const auto channels = std::min(input.channelCount, output.channelCount);
        const auto frames = std::min(input.frameCount, output.frameCount);
        const auto drive = dbToGain(value(0));
        const auto ceiling = dbToGain(value(1));
        const auto release = static_cast<float>(std::exp(-1.0 / (std::max(1.0f, value(2)) * 0.001 * sampleRate_)));
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            auto peak = 0.0f;
            for (std::uint32_t channel = 0; channel < channels; ++channel)
                peak = std::max(peak, std::abs(input.channel(channel)[frame] * drive));
            const auto target = peak > ceiling ? ceiling / peak : 1.0f;
            gain_ = target < gain_ ? target : target + (gain_ - target) * release;
            for (std::uint32_t channel = 0; channel < channels; ++channel)
                output.channel(channel)[frame]
                    = std::clamp(input.channel(channel)[frame] * drive * gain_, -ceiling, ceiling);
        }
    }

private:
    double sampleRate_ { 48'000.0 };
    float gain_ { 1.0f };
};

// Opens above the threshold, closes 4 dB under it to the range's floor.
// Values: threshold, attack, release, range.
class Gate final : public EffectProcessor, public MultiBusAudioProcessor {
public:
    using EffectProcessor::EffectProcessor;
    void prepare(const ProcessSpec& spec) override
    {
        sampleRate_ = std::max(1.0, spec.sampleRate);
        main_.prepare(std::max<std::uint32_t>(1, spec.channelCount), std::max<std::uint32_t>(1, spec.maximumBlockSize));
        reset();
    }
    // A sidechain wired in opens it instead of its own sound (a kick
    // opening a held chord).
    void processAudioBuses(const std::span<const AudioInputBusView> inputs, const AudioBlockView output,
                           const ProcessContext& context) noexcept override
    {
        auto main = main_.view(std::min(output.frameCount, main_.maximumFrameCount()));
        clearAudio(main);
        key_ = {};
        for (const auto& bus : inputs) {
            if (bus.portId == effectSidechainPortId)
                key_ = bus.audio;
            else
                addAudio(main, bus.audio);
        }
        process(main, { output.data, output.channelCount, main.frameCount, output.channelStride }, context);
        key_ = {};
    }
    void reset() noexcept override
    {
        envelope_ = 0.0f;
        gain_ = 0.0f;
        open_ = false;
    }
    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext&) noexcept override
    {
        clearAudio(output);
        const auto channels = std::min(input.channelCount, output.channelCount);
        const auto frames = std::min(input.frameCount, output.frameCount);
        const auto threshold = value(0);
        const auto attack = static_cast<float>(std::exp(-1.0 / (std::max(0.1f, value(1)) * 0.001 * sampleRate_)));
        const auto release = static_cast<float>(std::exp(-1.0 / (std::max(1.0f, value(2)) * 0.001 * sampleRate_)));
        const auto floor = dbToGain(value(3));
        const auto detectorRelease = static_cast<float>(std::exp(-1.0 / (0.015 * sampleRate_)));
        const auto keyed = key_.data != nullptr && key_.channelCount > 0 && key_.frameCount >= frames;
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            auto peak = 0.0f;
            if (keyed)
                for (std::uint32_t channel = 0; channel < key_.channelCount; ++channel)
                    peak = std::max(peak, std::abs(key_.channel(channel)[frame]));
            else
                for (std::uint32_t channel = 0; channel < channels; ++channel)
                    peak = std::max(peak, std::abs(input.channel(channel)[frame]));
            envelope_ = peak > envelope_ ? peak : envelope_ * detectorRelease;
            const auto levelDb = gainToDb(envelope_);
            if (!open_ && levelDb > threshold)
                open_ = true;
            else if (open_ && levelDb < threshold - 4.0f)
                open_ = false;
            const auto target = open_ ? 1.0f : floor;
            const auto coefficient = target > gain_ ? attack : release;
            gain_ = target + (gain_ - target) * coefficient;
            for (std::uint32_t channel = 0; channel < channels; ++channel)
                output.channel(channel)[frame] = input.channel(channel)[frame] * gain_;
        }
    }

private:
    double sampleRate_ { 48'000.0 };
    AudioBuffer main_;
    AudioBlockView key_ {};
    float envelope_ {};
    float gain_ {};
    bool open_ {};
};

// More or less of the attacks and of the tails, from a fast and a slow
// envelope: where the fast one leads, a note starts; where it trails, the
// note dies away. Values: attack, sustain, output.
class TransientShaper final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;
    void prepare(const ProcessSpec& spec) override
    {
        sampleRate_ = std::max(1.0, spec.sampleRate);
        const auto coefficient = [this](const double seconds) {
            return static_cast<float>(std::exp(-1.0 / (seconds * sampleRate_)));
        };
        fastAttack_ = coefficient(0.0003);
        fastRelease_ = coefficient(0.03);
        slowAttack_ = coefficient(0.015);
        slowRelease_ = coefficient(0.15);
        smoothing_ = coefficient(0.0008);
        reset();
    }
    void reset() noexcept override
    {
        fast_ = slow_ = 0.0f;
        gainDb_ = 0.0f;
    }
    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext&) noexcept override
    {
        clearAudio(output);
        const auto channels = std::min(input.channelCount, output.channelCount);
        const auto frames = std::min(input.frameCount, output.frameCount);
        const auto attack = value(0);
        const auto sustain = value(1);
        const auto outputDb = value(2);
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            auto level = 0.0f;
            for (std::uint32_t channel = 0; channel < channels; ++channel)
                level = std::max(level, std::abs(input.channel(channel)[frame]));
            fast_ = level + (fast_ - level) * (level > fast_ ? fastAttack_ : fastRelease_);
            slow_ = level + (slow_ - level) * (level > slow_ ? slowAttack_ : slowRelease_);
            const auto difference = gainToDb(fast_ + 1.0e-6f) - gainToDb(slow_ + 1.0e-6f);
            const auto target = std::clamp(attack * std::clamp(difference, 0.0f, 18.0f)
                                               + sustain * std::clamp(-difference, 0.0f, 18.0f) + outputDb,
                                           -24.0f, 24.0f);
            gainDb_ = target + (gainDb_ - target) * smoothing_;
            const auto gain = dbToGain(gainDb_);
            for (std::uint32_t channel = 0; channel < channels; ++channel)
                output.channel(channel)[frame] = input.channel(channel)[frame] * gain;
        }
    }

private:
    double sampleRate_ { 48'000.0 };
    float fastAttack_ {};
    float fastRelease_ {};
    float slowAttack_ {};
    float slowRelease_ {};
    float smoothing_ {};
    float fast_ {};
    float slow_ {};
    float gainDb_ {};
};

// ---- Ping-pong delay ----------------------------------------------------------------------------

// Echoes bouncing left, right, left: the left line feeds the right, the right
// feeds back into the left. Values: time, feedback, mix, width.
class PingPongDelay final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;
    void prepare(const ProcessSpec& spec) override
    {
        sampleRate_ = std::max(1.0, spec.sampleRate);
        lines_.prepare(2, static_cast<std::uint32_t>(std::ceil(sampleRate_ * 2.1)) + 8);
        reset();
    }
    void reset() noexcept override
    {
        lines_.clear();
        smoothedTime_ = -1.0f;
    }
    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext& context) noexcept override
    {
        clearAudio(output);
        const auto channels = std::min(input.channelCount, output.channelCount);
        const auto frames = std::min(input.frameCount, output.frameCount);
        if (channels == 0)
            return;
        const auto time = syncedTime(value(0), value(4), context);
        const auto feedback = value(1);
        const auto mix = value(2);
        const auto width = value(3);
        if (smoothedTime_ < 0.0f)
            smoothedTime_ = time;
        const auto msToSamples = static_cast<float>(sampleRate_ / 1'000.0);
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            smoothedTime_ += 0.0008f * (time - smoothedTime_);
            const auto distance = smoothedTime_ * msToSamples;
            const auto left = lines_.read(0, distance);
            const auto right = lines_.read(1, distance);
            const auto inLeft = input.channel(0)[frame];
            const auto inRight = channels > 1 ? input.channel(1)[frame] : inLeft;
            lines_.write(0, (inLeft + inRight) * 0.5f + std::clamp(right * feedback, -4.0f, 4.0f));
            lines_.write(1, std::clamp(left * feedback, -4.0f, 4.0f));
            lines_.advance();
            const auto spread = 0.5f + 0.5f * width;
            const auto wetLeft = left * spread + right * (1.0f - spread);
            const auto wetRight = right * spread + left * (1.0f - spread);
            if (channels == 1) {
                output.channel(0)[frame] = inLeft * (1.0f - mix) + (left + right) * 0.5f * mix;
                continue;
            }
            output.channel(0)[frame] = inLeft * (1.0f - mix) + wetLeft * mix;
            output.channel(1)[frame] = inRight * (1.0f - mix) + wetRight * mix;
            for (std::uint32_t channel = 2; channel < channels; ++channel)
                output.channel(channel)[frame] = input.channel(channel)[frame];
        }
    }

private:
    double sampleRate_ { 48'000.0 };
    DelayLines lines_;
    float smoothedTime_ { -1.0f };
};

// ---- Channel conversion ------------------------------------------------------------------------

// The first channel on both sides, panned (balance law: in the middle each
// side keeps the full level). Value: pan.
class MonoToStereo final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;
    void prepare(const ProcessSpec&) override {}
    void reset() noexcept override {}
    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext&) noexcept override
    {
        clearAudio(output);
        if (input.channelCount == 0 || output.channelCount == 0)
            return;
        const auto frames = std::min(input.frameCount, output.frameCount);
        const auto pan = std::clamp(value(0), -1.0f, 1.0f);
        const auto left = std::min(1.0f, 1.0f - pan);
        const auto right = std::min(1.0f, 1.0f + pan);
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            const auto sample = input.channel(0)[frame];
            output.channel(0)[frame] = sample * (output.channelCount > 1 ? left : 1.0f);
            if (output.channelCount > 1)
                output.channel(1)[frame] = sample * right;
        }
    }
};

// Both halves summed to one, the same on every output channel. Value:
// balance (-1 the left alone, 0 the average, 1 the right alone).
class StereoToMono final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;
    void prepare(const ProcessSpec&) override {}
    void reset() noexcept override {}
    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext&) noexcept override
    {
        clearAudio(output);
        if (input.channelCount == 0 || output.channelCount == 0)
            return;
        const auto frames = std::min(input.frameCount, output.frameCount);
        const auto balance = std::clamp(value(0), -1.0f, 1.0f);
        const auto left = input.channelCount > 1 ? (1.0f - balance) * 0.5f : 1.0f;
        const auto right = input.channelCount > 1 ? (1.0f + balance) * 0.5f : 0.0f;
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            const auto mono = input.channel(0)[frame] * left
                              + (input.channelCount > 1 ? input.channel(1)[frame] * right : 0.0f);
            for (std::uint32_t channel = 0; channel < output.channelCount; ++channel)
                output.channel(channel)[frame] = mono;
        }
    }
};

// The spectrometer: what comes in goes out untouched; the interface reads
// it at the card's output.
class Spectrometer final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;
    void prepare(const ProcessSpec&) override {}
    void reset() noexcept override {}
    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext&) noexcept override
    {
        clearAudio(output);
        const auto frames = std::min(input.frameCount, output.frameCount);
        for (std::uint32_t channel = 0; channel < std::min(input.channelCount, output.channelCount); ++channel)
            std::copy_n(input.channel(channel), frames, output.channel(channel));
    }
};

} // namespace

std::unique_ptr<AudioProcessor> makeEffectProcessor(const NativeDspNode& node)
{
    const auto* effect = effectForKind(node.kind);
    if (effect == nullptr)
        return {};
    std::unique_ptr<EffectProcessor> processor;
    switch (node.kind) {
    case NativeDspNodeKind::chorus: processor = std::make_unique<ModulatedDelay>(*effect, false); break;
    case NativeDspNodeKind::flanger: processor = std::make_unique<ModulatedDelay>(*effect, true); break;
    case NativeDspNodeKind::phaser: processor = std::make_unique<Phaser>(*effect); break;
    case NativeDspNodeKind::tremolo: processor = std::make_unique<Tremolo>(*effect); break;
    case NativeDspNodeKind::bitcrusher: processor = std::make_unique<Bitcrusher>(*effect); break;
    case NativeDspNodeKind::multiFilter: processor = std::make_unique<MultimodeFilter>(*effect); break;
    case NativeDspNodeKind::autoFilter: processor = std::make_unique<AutoFilter>(*effect); break;
    case NativeDspNodeKind::threeBandEq: processor = std::make_unique<ThreeBandEq>(*effect); break;
    case NativeDspNodeKind::parametricEq: processor = std::make_unique<ParametricEq>(*effect); break;
    case NativeDspNodeKind::limiter: processor = std::make_unique<Limiter>(*effect); break;
    case NativeDspNodeKind::gate: processor = std::make_unique<Gate>(*effect); break;
    case NativeDspNodeKind::transientShaper: processor = std::make_unique<TransientShaper>(*effect); break;
    case NativeDspNodeKind::pingPongDelay: processor = std::make_unique<PingPongDelay>(*effect); break;
    case NativeDspNodeKind::monoToStereo: processor = std::make_unique<MonoToStereo>(*effect); break;
    case NativeDspNodeKind::stereoToMono: processor = std::make_unique<StereoToMono>(*effect); break;
    case NativeDspNodeKind::spectrometer: processor = std::make_unique<Spectrometer>(*effect); break;
    default:
        // The studio effects (ProEffects.cpp).
        processor = makeProEffect(*effect);
        if (processor == nullptr)
            return {};
        break;
    }
    processor->setParameters(node.parameters);
    return processor;
}

bool updateEffectProcessor(AudioProcessor& processor, const NativeDspNode& node) noexcept
{
    auto* effect = dynamic_cast<EffectProcessor*>(&processor);
    if (effect == nullptr || effect->descriptor().kind != node.kind)
        return false;
    effect->setParameters(node.parameters);
    return true;
}

} // namespace cydaw::engine
