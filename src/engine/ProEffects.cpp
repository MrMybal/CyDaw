#include <cydaw/engine/ProEffects.h>

#include <cydaw/engine/AmpEffects.h>

#include <cydaw/engine/AudioBuffer.h>
#include <cydaw/engine/ProEffectsDesign.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <vector>

namespace cydaw::engine {

namespace {

constexpr double kPi = std::numbers::pi;

double dbToGain(const double db) noexcept
{
    return std::pow(10.0, db / 20.0);
}

double gainToDb(const double gain) noexcept
{
    return 20.0 * std::log10(std::max(gain, 1.0e-12));
}

// The coefficient of a one-pole smoother of that many milliseconds.
double smoothing(const double milliseconds, const double rate) noexcept
{
    return std::exp(-1.0 / (std::max(0.001, milliseconds) * 0.001 * rate));
}

struct BiquadState {
    double z1 {};
    double z2 {};
};

double run(const BiquadCoefficients& c, BiquadState& s, const double x) noexcept
{
    const auto y = c.b0 * x + s.z1;
    s.z1 = c.b1 * x - c.a1 * y + s.z2;
    s.z2 = c.b2 * x - c.a2 * y;
    return y;
}

std::size_t powerOfTwoAtLeast(const std::size_t value) noexcept
{
    std::size_t size = 1;
    while (size < value)
        size <<= 1;
    return size;
}

// A delay line read at any fraction of a sample (four-point Hermite).
class FractionalDelay {
public:
    void prepare(const std::size_t maximumSamples)
    {
        buffer_.assign(powerOfTwoAtLeast(maximumSamples + 8), 0.0f);
        mask_ = buffer_.size() - 1;
        write_ = 0;
    }
    void clear() noexcept { std::fill(buffer_.begin(), buffer_.end(), 0.0f); }
    void push(const double value) noexcept
    {
        buffer_[write_] = static_cast<float>(value);
        write_ = (write_ + 1) & mask_;
    }
    // The sample written `delay` samples before the last one pushed.
    [[nodiscard]] double read(const double delay) const noexcept
    {
        const auto position = static_cast<double>(write_) - 1.0 - std::max(2.0, delay);
        const auto whole = std::floor(position);
        const auto t = position - whole;
        const auto index = static_cast<std::int64_t>(whole);
        const auto at = [this, index](const std::int64_t offset) {
            return static_cast<double>(buffer_[static_cast<std::size_t>(index + offset) & mask_]);
        };
        const auto xm1 = at(-1);
        const auto x0 = at(0);
        const auto x1 = at(1);
        const auto x2 = at(2);
        const auto c1 = 0.5 * (x1 - xm1);
        const auto c2 = xm1 - 2.5 * x0 + 2.0 * x1 - 0.5 * x2;
        const auto c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }
    [[nodiscard]] double readWhole(const std::size_t delay) const noexcept
    {
        return static_cast<double>(buffer_[(write_ - 1 - delay) & mask_]);
    }

private:
    std::vector<float> buffer_ { 0.0f };
    std::size_t mask_ {};
    std::size_t write_ {};
};

// A Schroeder allpass of a whole delay.
class Allpass {
public:
    void prepare(const std::size_t delay)
    {
        buffer_.assign(std::max<std::size_t>(1, delay), 0.0f);
        index_ = 0;
    }
    void clear() noexcept { std::fill(buffer_.begin(), buffer_.end(), 0.0f); }
    double process(const double x, const double g) noexcept
    {
        const auto delayed = static_cast<double>(buffer_[index_]);
        const auto y = delayed - g * x;
        buffer_[index_] = static_cast<float>(x + g * y);
        index_ = (index_ + 1) % buffer_.size();
        return y;
    }

private:
    std::vector<float> buffer_ { 0.0f };
    std::size_t index_ {};
};

// ---- Réverbe Pro ------------------------------------------------------------------------------

class ReverbPro final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;

    void prepare(const ProcessSpec& spec) override
    {
        rate_ = std::max(1.0, spec.sampleRate);
        const auto longest = pro::reverbLineMs.back() * 0.001 * pro::reverbScale(1.0) * rate_;
        for (auto& line : lines_)
            line.prepare(static_cast<std::size_t>(longest + 64.0));
        for (auto& delay : predelay_)
            delay.prepare(static_cast<std::size_t>(0.6 * rate_));
        for (auto& delay : early_)
            delay.prepare(static_cast<std::size_t>(0.2 * rate_));
        constexpr std::array<double, 4> diffuserMs { 4.77, 3.59, 12.73, 9.31 };
        for (std::size_t channel = 0; channel < 2; ++channel)
            for (std::size_t stage = 0; stage < diffusers_[channel].size(); ++stage)
                diffusers_[channel][stage].prepare(static_cast<std::size_t>(
                    diffuserMs[stage] * (channel == 0 ? 1.0 : 1.11) * 0.001 * rate_ * 1.3));
        for (int line = 0; line < pro::reverbLines; ++line) {
            phase_[static_cast<std::size_t>(line)] = static_cast<double>(line) * 0.618034;
            phase_[static_cast<std::size_t>(line)] -= std::floor(phase_[static_cast<std::size_t>(line)]);
        }
        designed_ = {};
        reset();
    }

    void reset() noexcept override
    {
        for (auto& line : lines_)
            line.clear();
        for (auto& delay : predelay_)
            delay.clear();
        for (auto& delay : early_)
            delay.clear();
        for (auto& channel : diffusers_)
            for (auto& stage : channel)
                stage.clear();
        for (auto& states : loopStates_)
            states = {};
        for (auto& state : toneStates_)
            state = {};
        for (auto& state : postStates_)
            state = {};
        lengths_.fill(-1.0);
        duck_ = 0.0;
    }

    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext& context) noexcept override
    {
        using namespace pro;
        clearAudio(output);
        const auto frames = std::min(input.frameCount, output.frameCount);
        if (input.channelCount == 0 || output.channelCount == 0)
            return;
        const auto space = static_cast<double>(value(reverbSpace));
        const auto decay = static_cast<double>(value(reverbDecay));
        const auto distance = static_cast<double>(value(reverbDistance));
        const auto brightness = static_cast<double>(value(reverbBrightness));
        const auto character = static_cast<double>(value(reverbCharacter));
        const auto width = static_cast<double>(value(reverbWidth));
        const auto mix = static_cast<double>(value(reverbMix));
        const auto freeze = value(reverbFreeze) >= 0.5f;
        const auto diffusion = static_cast<double>(value(reverbDiffusion)) * 0.72;
        const auto outputGain = dbToGain(value(reverbOutput));
        const auto earlyGain = dbToGain(value(reverbEarly)) * (1.0 - 0.85 * distance) * 0.55;
        const auto lateGain = (0.7 + 0.3 * distance) * 0.42;
        const auto duckDb = static_cast<double>(value(reverbDucking));
        const auto duckRelease = smoothing(value(reverbDuckRelease), rate_);
        const auto duckAttack = smoothing(3.0, rate_);

        // The predelay: free, or a note value of the tempo; the late part
        // waits a little longer as the listener moves away.
        auto predelayMs = static_cast<double>(value(reverbPredelay));
        if (const auto* division = noteDivisionFor(value(reverbSync)))
            predelayMs = division->beats * 60'000.0 / std::max(20.0, context.transport.beatsPerMinute);
        const auto earlyDelay = std::clamp(predelayMs * 0.001 * rate_, 1.0, 0.55 * rate_);
        const auto lateDelay = std::clamp((predelayMs + 15.0 * distance) * 0.001 * rate_, 1.0, 0.58 * rate_);

        designIfChanged(space, decay, brightness);
        const auto scale = reverbScale(space);
        // Character: the lines' slow wander, a few samples up to forty.
        const auto depth = (0.4 + 40.0 * character * character) * rate_ / 48'000.0;
        const auto earlyScale = (0.3 + 0.9 * space) * 0.001 * rate_;
        constexpr std::array<double, 12> earlyMs { 7.1, 11.3, 17.9, 23.3, 29.2, 37.7, 43.1, 51.9, 59.3, 66.7, 75.1, 83.9 };
        constexpr std::array<double, 12> earlyLevel { 0.9, -0.82, 0.74, -0.66, 0.6, -0.54, 0.48, -0.43, 0.38, -0.34, 0.3, -0.27 };

        const auto stereo = input.channelCount > 1;
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            const auto inL = static_cast<double>(input.channel(0)[frame]);
            const auto inR = stereo ? static_cast<double>(input.channel(1)[frame]) : inL;

            // Ducking follows the dry sound.
            const auto level = std::max(std::abs(inL), std::abs(inR));
            duck_ = level > duck_ ? duckAttack * duck_ + (1.0 - duckAttack) * level : duckRelease * duck_ + (1.0 - duckRelease) * level;
            const auto duckGain = duckDb > 0.0 ? dbToGain(-duckDb * std::clamp((gainToDb(duck_) + 36.0) / 30.0, 0.0, 1.0)) : 1.0;

            // Brightness tilts what goes in.
            const auto toneL = run(highTone_, toneStates_[1], run(lowTone_, toneStates_[0], inL));
            const auto toneR = run(highTone_, toneStates_[3], run(lowTone_, toneStates_[2], inR));
            predelay_[0].push(toneL);
            predelay_[1].push(toneR);

            // Early reflections: a dozen taps, spread differently each side.
            const auto earlyInL = predelay_[0].read(earlyDelay);
            const auto earlyInR = predelay_[1].read(earlyDelay);
            early_[0].push(earlyInL);
            early_[1].push(earlyInR);
            auto earlyL = 0.0;
            auto earlyR = 0.0;
            for (std::size_t tap = 0; tap < earlyMs.size(); ++tap) {
                earlyL += earlyLevel[tap] * early_[0].read(earlyMs[tap] * earlyScale);
                earlyR += earlyLevel[tap] * early_[1].read((earlyMs[tap] * 1.07 + 1.3) * earlyScale);
            }

            // The late part: diffused, then into the network.
            auto lateL = predelay_[0].read(lateDelay);
            auto lateR = predelay_[1].read(lateDelay);
            for (auto& stage : diffusers_[0])
                lateL = stage.process(lateL, diffusion);
            for (auto& stage : diffusers_[1])
                lateR = stage.process(lateR, diffusion);

            std::array<double, reverbLines> taps {};
            for (int line = 0; line < reverbLines; ++line) {
                const auto index = static_cast<std::size_t>(line);
                const auto target = reverbLineMs[index] * 0.001 * scale * rate_;
                // A space turned glides there instead of jumping.
                lengths_[index] = lengths_[index] < 0.0 ? target : lengths_[index] + (target - lengths_[index]) * 0.0007;
                phase_[index] += (0.11 + 0.093 * static_cast<double>(line)) * (0.4 + character) / rate_;
                phase_[index] -= std::floor(phase_[index]);
                const auto wander = depth * std::sin(2.0 * kPi * phase_[index]);
                taps[index] = lines_[index].read(lengths_[index] + wander + depth + 2.0);
            }
            std::array<double, reverbLines> feedback {};
            for (int line = 0; line < reverbLines; ++line) {
                const auto index = static_cast<std::size_t>(line);
                if (freeze) {
                    feedback[index] = taps[index];
                } else {
                    auto& states = loopStates_[index];
                    const auto& loop = loops_[index];
                    feedback[index] = run(loop.high, states[2], run(loop.mid, states[1], run(loop.low, states[0], taps[index])))
                                      * loop.gain;
                }
            }
            hadamard(feedback);
            for (int line = 0; line < reverbLines; ++line) {
                const auto index = static_cast<std::size_t>(line);
                const auto sign = (line & 2) != 0 ? -1.0 : 1.0;
                const auto injected = freeze ? 0.0 : ((line & 1) == 0 ? lateL : lateR) * sign * 0.25;
                lines_[index].push(feedback[index] + injected);
            }
            auto wetL = 0.0;
            auto wetR = 0.0;
            for (int line = 0; line < reverbLines; line += 2) {
                const auto sign = (line & 4) != 0 ? -1.0 : 1.0;
                wetL += taps[static_cast<std::size_t>(line)] * sign;
                wetR += taps[static_cast<std::size_t>(line + 1)] * sign;
            }
            wetL = wetL * lateGain + earlyL * earlyGain;
            wetR = wetR * lateGain + earlyR * earlyGain;
            // The cuts after it, then its width.
            wetL = run(highCut_, postStates_[1], run(lowCut_, postStates_[0], wetL));
            wetR = run(highCut_, postStates_[3], run(lowCut_, postStates_[2], wetR));
            const auto mid = 0.5 * (wetL + wetR);
            const auto side = 0.5 * (wetL - wetR) * width;
            wetL = (mid + side) * duckGain;
            wetR = (mid - side) * duckGain;

            const auto outL = (inL * (1.0 - mix) + wetL * mix) * outputGain;
            const auto outR = (inR * (1.0 - mix) + wetR * mix) * outputGain;
            output.channel(0)[frame] = static_cast<float>(outL);
            if (output.channelCount > 1)
                output.channel(1)[frame] = static_cast<float>(outR);
            for (std::uint32_t channel = 2; channel < output.channelCount; ++channel)
                output.channel(channel)[frame] = channel < input.channelCount ? input.channel(channel)[frame] : 0.0f;
        }
    }

private:
    // Sixteen inputs mixed by a Hadamard matrix: lossless, every line into
    // every other.
    static void hadamard(std::array<double, pro::reverbLines>& v) noexcept
    {
        for (std::size_t span = 1; span < v.size(); span <<= 1)
            for (std::size_t start = 0; start < v.size(); start += span << 1)
                for (std::size_t i = start; i < start + span; ++i) {
                    const auto a = v[i];
                    const auto b = v[i + span];
                    v[i] = a + b;
                    v[i + span] = a - b;
                }
        for (auto& value : v)
            value *= 0.25;
    }

    void designIfChanged(const double space, const double decay, const double brightness) noexcept
    {
        using namespace pro;
        const std::array<float, 12> now { static_cast<float>(space), static_cast<float>(decay),
                                          value(reverbLowDecay), value(reverbLowFrequency), value(reverbMidDecay),
                                          value(reverbMidFrequency), value(reverbHighDecay), value(reverbHighFrequency),
                                          static_cast<float>(brightness), value(reverbLowCut), value(reverbHighCut), 1.0f };
        if (now == designed_)
            return;
        designed_ = now;
        const auto scale = reverbScale(space);
        for (int line = 0; line < reverbLines; ++line) {
            const auto index = static_cast<std::size_t>(line);
            loops_[index] = reverbLoop(reverbLineMs[index] * 0.001 * scale, decay, value(reverbLowDecay), value(reverbLowFrequency),
                                       value(reverbMidDecay), value(reverbMidFrequency), value(reverbHighDecay),
                                       value(reverbHighFrequency), rate_);
        }
        lowTone_ = designEqBand(EqBandType::lowShelf, 300.0, -4.0 * brightness, 0.707, 0, 0, rate_);
        highTone_ = designEqBand(EqBandType::highShelf, std::min(3'000.0, rate_ * 0.4), 9.0 * brightness, 0.707, 0, 0, rate_);
        const auto lowCut = static_cast<double>(value(reverbLowCut));
        const auto highCut = static_cast<double>(value(reverbHighCut));
        lowCut_ = lowCut > 10.5 ? designEqBand(EqBandType::lowCut, lowCut, 0.0, 0.707, 0, 0, rate_) : BiquadCoefficients {};
        highCut_ = highCut < 19'900.0 ? designEqBand(EqBandType::highCut, std::min(highCut, rate_ * 0.45), 0.0, 0.707, 0, 0, rate_)
                                      : BiquadCoefficients {};
    }

    double rate_ { 48'000.0 };
    std::array<FractionalDelay, pro::reverbLines> lines_;
    std::array<pro::ReverbLoop, pro::reverbLines> loops_ {};
    std::array<std::array<BiquadState, 3>, pro::reverbLines> loopStates_ {};
    std::array<double, pro::reverbLines> lengths_ {};
    std::array<double, pro::reverbLines> phase_ {};
    std::array<FractionalDelay, 2> predelay_;
    std::array<FractionalDelay, 2> early_;
    std::array<std::array<Allpass, 4>, 2> diffusers_;
    BiquadCoefficients lowTone_;
    BiquadCoefficients highTone_;
    BiquadCoefficients lowCut_;
    BiquadCoefficients highCut_;
    std::array<BiquadState, 4> toneStates_ {};
    std::array<BiquadState, 4> postStates_ {};
    std::array<float, 12> designed_ {};
    double duck_ {};
};

// ---- Compresseur Pro ---------------------------------------------------------------------------

class CompressorPro final : public EffectProcessor, public MultiBusAudioProcessor {
public:
    using EffectProcessor::EffectProcessor;

    static constexpr std::size_t maximumChannels = 8;

    void prepare(const ProcessSpec& spec) override
    {
        rate_ = std::max(1.0, spec.sampleRate);
        channels_ = std::min<std::size_t>(maximumChannels, std::max<std::uint32_t>(1, spec.channelCount));
        main_.prepare(std::max<std::uint32_t>(1, spec.channelCount), std::max<std::uint32_t>(1, spec.maximumBlockSize));
        for (auto& delay : lookahead_)
            delay.prepare(static_cast<std::size_t>(0.021 * rate_) + 2);
        designedHigh_ = -1.0f;
        designedLow_ = -1.0f;
        reset();
    }

    void reset() noexcept override
    {
        for (auto& delay : lookahead_)
            delay.clear();
        highStates_.fill({});
        lowStates_.fill({});
        rms_.fill(0.0);
        reduction_.fill(0.0);
        hold_.fill(0);
        sustain_.fill(0.0);
        lastOutput_.fill(0.0);
        makeup_ = -1000.0;
        mix_ = -1.0;
    }

    // Its own sound on its main input (and any other but the sidechain),
    // the key on the sidechain: what the detector hears instead.
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

    // The look-ahead holds the sound back by its length.
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override
    {
        return static_cast<std::uint32_t>(std::max(0L, std::lround(value(pro::compressorLookahead) * 0.001 * rate_)));
    }
    [[nodiscard]] std::uint32_t maximumLatencySamples() const noexcept override
    {
        return static_cast<std::uint32_t>(0.021 * rate_);
    }

    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext&) noexcept override
    {
        using namespace pro;
        clearAudio(output);
        const auto channels = std::min({ static_cast<std::size_t>(input.channelCount), static_cast<std::size_t>(output.channelCount),
                                         channels_ });
        const auto frames = std::min(input.frameCount, output.frameCount);
        const auto& style = compressorStyles[static_cast<std::size_t>(
            std::clamp(static_cast<int>(std::lround(value(compressorStyle))), 0, compressorStyleCount - 1))];
        const auto threshold = static_cast<double>(value(compressorThreshold));
        const auto ratio = std::max(1.0, static_cast<double>(value(compressorRatio)));
        const auto knee = std::max(0.0, static_cast<double>(value(compressorKnee))) + style.kneeExtra;
        const auto range = static_cast<double>(value(compressorRange));
        const auto attackMs = std::max(0.01, static_cast<double>(value(compressorAttack))) * style.attackScale;
        const auto releaseMs = std::max(1.0, static_cast<double>(value(compressorRelease))) * style.releaseScale;
        const auto attack = smoothing(attackMs, rate_);
        const auto holdSamples = static_cast<int>(std::lround(value(compressorHold) * 0.001 * rate_));
        const auto lookahead = static_cast<double>(std::lround(value(compressorLookahead) * 0.001 * rate_));
        const auto autoRelease = value(compressorAutoRelease) >= 0.5f;
        const auto link = static_cast<double>(value(compressorLink));
        const auto mode = channels >= 2 ? std::clamp(static_cast<int>(std::lround(value(compressorChannels))), 0, 2) : 0;
        const auto audition = value(compressorAudition) >= 0.5f;
        auto targetMakeup = static_cast<double>(value(compressorMakeup));
        if (value(compressorAutoGain) >= 0.5f)
            targetMakeup += compressorAutoGainDb(threshold, ratio, knee, range);
        const auto targetMix = static_cast<double>(value(compressorMix));
        const auto rmsCoefficient = style.rmsMs > 0.0 ? smoothing(style.rmsMs, rate_) : 0.0;
        const auto sustainCoefficient = smoothing(1'500.0, rate_);

        // The detector's filters, from the sidechain's two cut-offs.
        const auto highHz = value(compressorSidechainHigh);
        const auto lowHz = value(compressorSidechainLow);
        const auto highPass = highHz > 21.0f;
        const auto lowPass = lowHz < 19'500.0f;
        if (highPass && highHz != designedHigh_) {
            designedHigh_ = highHz;
            highPass_ = designEqBand(EqBandType::lowCut, highHz, 0.0, 0.71, 0, 0, rate_);
        }
        if (lowPass && lowHz != designedLow_) {
            designedLow_ = lowHz;
            lowPass_ = designEqBand(EqBandType::highCut, std::min(static_cast<double>(lowHz), rate_ * 0.45), 0.0, 0.71, 0, 0, rate_);
        }
        if (makeup_ < -999.0 || mix_ < 0.0) {
            makeup_ = targetMakeup;
            mix_ = targetMix;
        }
        // Makeup and mix glide over the block so a turned knob does not click.
        const auto fromMakeup = makeup_;
        const auto fromMix = mix_;
        makeup_ = targetMakeup;
        mix_ = targetMix;
        const auto inverseFrames = frames > 0 ? 1.0 / static_cast<double>(frames) : 0.0;

        std::array<double, maximumChannels> work {};
        std::array<double, maximumChannels> keyWork {};
        std::array<double, maximumChannels> detector {};
        std::array<double, maximumChannels> target {};
        // A sidechain wired in: the detector hears it, not the sound.
        const auto keyed = key_.data != nullptr && key_.channelCount > 0 && key_.frameCount >= frames;
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            for (std::size_t channel = 0; channel < channels; ++channel)
                work[channel] = static_cast<double>(input.channel(static_cast<std::uint32_t>(channel))[frame]);
            if (keyed)
                for (std::size_t channel = 0; channel < channels; ++channel)
                    keyWork[channel] = static_cast<double>(
                        key_.channel(std::min(static_cast<std::uint32_t>(channel), key_.channelCount - 1))[frame]);
            // Mid/side: one of the two is compressed, the other passes.
            if (mode != 0) {
                const auto left = work[0];
                const auto right = work[1];
                work[0] = 0.5 * (left + right);
                work[1] = 0.5 * (left - right);
                if (keyed) {
                    const auto keyLeft = keyWork[0];
                    const auto keyRight = keyWork[1];
                    keyWork[0] = 0.5 * (keyLeft + keyRight);
                    keyWork[1] = 0.5 * (keyLeft - keyRight);
                }
            }
            const auto compressed = [mode](const std::size_t channel) {
                return mode == 0 || (mode == 1 && channel == 0) || (mode == 2 && channel == 1);
            };
            auto deepest = 0.0;
            for (std::size_t channel = 0; channel < channels; ++channel) {
                if (!compressed(channel))
                    continue;
                // What the detector hears: the input, partly the output (a
                // feedback design), filtered.
                auto x = keyed ? keyWork[channel] : work[channel] * (1.0 - style.feedback) + lastOutput_[channel] * style.feedback;
                if (highPass)
                    x = run(highPass_, highStates_[channel], x);
                if (lowPass)
                    x = run(lowPass_, lowStates_[channel], x);
                detector[channel] = x;
                auto level = std::abs(x);
                if (rmsCoefficient > 0.0) {
                    rms_[channel] = rmsCoefficient * rms_[channel] + (1.0 - rmsCoefficient) * x * x;
                    level = std::sqrt(rms_[channel]) * 1.414;
                }
                target[channel] = compressorGainDb(gainToDb(level), threshold, ratio, knee, range);
                deepest = std::min(deepest, target[channel]);
            }
            const auto t = static_cast<double>(frame) * inverseFrames;
            const auto mix = fromMix + (targetMix - fromMix) * t;
            const auto makeup = fromMakeup + (targetMakeup - fromMakeup) * t;
            for (std::size_t channel = 0; channel < channels; ++channel) {
                lookahead_[channel].push(work[channel]);
                const auto delayed = lookahead > 0.0 ? lookahead_[channel].readWhole(static_cast<std::size_t>(lookahead))
                                                     : work[channel];
                if (!compressed(channel)) {
                    work[channel] = delayed;
                    continue;
                }
                const auto wanted = link * deepest + (1.0 - link) * target[channel];
                auto& reduction = reduction_[channel];
                sustain_[channel] = sustainCoefficient * sustain_[channel] + (1.0 - sustainCoefficient) * (wanted < -1.0 ? 1.0 : 0.0);
                if (wanted < reduction) {
                    reduction = attack * reduction + (1.0 - attack) * wanted;
                    hold_[channel] = holdSamples;
                } else if (hold_[channel] > 0) {
                    --hold_[channel];
                } else {
                    auto ms = releaseMs * (1.0 + style.programRelease * sustain_[channel]);
                    if (autoRelease)
                        ms *= 0.25 + 1.75 * sustain_[channel];
                    if (style.linearRelease)
                        reduction = std::min(wanted, reduction + 30.0 / (ms * 0.001 * rate_));
                    else {
                        const auto release = smoothing(ms, rate_);
                        reduction = release * reduction + (1.0 - release) * wanted;
                    }
                }
                const auto gain = dbToGain(reduction);
                lastOutput_[channel] = delayed * gain;
                work[channel] = audition ? detector[channel] : delayed * (1.0 - mix + mix * gain * dbToGain(makeup));
            }
            if (mode != 0) {
                const auto mid = work[0];
                const auto side = work[1];
                work[0] = mid + side;
                work[1] = mid - side;
            }
            for (std::size_t channel = 0; channel < channels; ++channel)
                output.channel(static_cast<std::uint32_t>(channel))[frame] = static_cast<float>(work[channel]);
        }
        for (auto channel = static_cast<std::uint32_t>(channels); channel < std::min(input.channelCount, output.channelCount); ++channel)
            std::copy_n(input.channel(channel), frames, output.channel(channel));
    }

private:
    double rate_ { 48'000.0 };
    std::size_t channels_ { 2 };
    // Its own sound summed when it arrives on several inputs, and the key
    // for the block being processed.
    AudioBuffer main_;
    AudioBlockView key_ {};
    std::array<FractionalDelay, maximumChannels> lookahead_;
    BiquadCoefficients highPass_;
    BiquadCoefficients lowPass_;
    float designedHigh_ { -1.0f };
    float designedLow_ { -1.0f };
    std::array<BiquadState, maximumChannels> highStates_ {};
    std::array<BiquadState, maximumChannels> lowStates_ {};
    std::array<double, maximumChannels> rms_ {};
    std::array<double, maximumChannels> reduction_ {};
    std::array<int, maximumChannels> hold_ {};
    std::array<double, maximumChannels> sustain_ {};
    std::array<double, maximumChannels> lastOutput_ {};
    double makeup_ { -1000.0 };
    double mix_ { -1.0 };
};

// ---- Limiteur Pro -------------------------------------------------------------------------------

// The smallest of the last `length` values pushed, at a constant cost.
class SlidingMinimum {
public:
    void prepare(const std::size_t capacity)
    {
        values_.assign(capacity + 1, 1.0);
        times_.assign(capacity + 1, 0);
        clear();
    }
    void clear() noexcept
    {
        head_ = 0;
        size_ = 0;
        time_ = 0;
    }
    double push(const double value, const std::size_t length) noexcept
    {
        const auto capacity = values_.size();
        // Values behind that are not smaller can never be the minimum again.
        while (size_ > 0 && values_[(head_ + size_ - 1) % capacity] >= value)
            --size_;
        values_[(head_ + size_) % capacity] = value;
        times_[(head_ + size_) % capacity] = time_;
        ++size_;
        // Out of the window: gone.
        while (size_ > 0 && time_ - times_[head_] >= static_cast<std::uint64_t>(std::max<std::size_t>(1, length))) {
            head_ = (head_ + 1) % capacity;
            --size_;
        }
        ++time_;
        return values_[head_];
    }

private:
    std::vector<double> values_;
    std::vector<std::uint64_t> times_;
    std::size_t head_ {};
    std::size_t size_ {};
    std::uint64_t time_ {};
};

// The average of the last `length` values pushed.
class MovingAverage {
public:
    void prepare(const std::size_t capacity)
    {
        values_.assign(capacity + 1, 1.0);
        clear();
    }
    void clear() noexcept
    {
        std::fill(values_.begin(), values_.end(), 1.0);
        index_ = 0;
        sum_ = static_cast<double>(values_.size());
        length_ = values_.size();
    }
    double push(const double value, const std::size_t length) noexcept
    {
        const auto n = std::clamp<std::size_t>(length, 1, values_.size());
        if (n != length_) {
            // A new length: start from where the window stands.
            std::fill(values_.begin(), values_.end(), value);
            sum_ = value * static_cast<double>(n);
            length_ = n;
            index_ = 0;
            return value;
        }
        sum_ += value - values_[index_];
        values_[index_] = value;
        index_ = (index_ + 1) % n;
        return sum_ / static_cast<double>(n);
    }

private:
    std::vector<double> values_;
    std::size_t index_ {};
    double sum_ {};
    std::size_t length_ {};
};

class LimiterPro final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;

    static constexpr std::size_t maximumChannels = 8;
    // The detector's interpolation: eight taps a phase, eight phases at most,
    // over nine samples of history so both intervals around the fifth (the
    // sample it guards) are read.
    static constexpr int interpolationTaps = 8;
    static constexpr std::size_t historyLength = interpolationTaps + 1;
    static constexpr int maximumFactor = 8;

    void prepare(const ProcessSpec& spec) override
    {
        rate_ = std::max(1.0, spec.sampleRate);
        channels_ = std::min<std::size_t>(maximumChannels, std::max<std::uint32_t>(1, spec.channelCount));
        const auto capacity = static_cast<std::size_t>(pro::limiterMaximumLookaheadMs * 0.001 * rate_) + 2;
        for (std::size_t channel = 0; channel < maximumChannels; ++channel) {
            delay_[channel].prepare(capacity + interpolationTaps + 4);
            minimum_[channel].prepare(capacity);
            average_[channel].prepare(capacity);
        }
        // Windowed sinc between the fourth and fifth of eight samples.
        for (int factor = 1; factor <= maximumFactor; ++factor)
            for (int phase = 0; phase < maximumFactor; ++phase)
                for (int tap = 0; tap < interpolationTaps; ++tap) {
                    const auto x = static_cast<double>(tap) - (3.0 + static_cast<double>(phase) / static_cast<double>(factor));
                    const auto sinc = std::abs(x) < 1.0e-9 ? 1.0 : std::sin(kPi * x) / (kPi * x);
                    const auto window = 0.5 + 0.5 * std::cos(kPi * x / 4.5);
                    kernel_[static_cast<std::size_t>(factor - 1)][static_cast<std::size_t>(phase)][static_cast<std::size_t>(tap)]
                        = sinc * window;
                }
        reset();
    }

    void reset() noexcept override
    {
        for (std::size_t channel = 0; channel < maximumChannels; ++channel) {
            delay_[channel].clear();
            minimum_[channel].clear();
            average_[channel].clear();
        }
        history_ = {};
        release_.fill(1.0);
        slow_.fill(1.0);
        rms_.fill(0.0);
        dense_.fill(0.0);
        dc_.fill({});
        errors_ = {};
        lookahead_ = std::numeric_limits<std::size_t>::max();
        random_ = 0x9e3779b9u;
    }

    // The audio waits for the detector (see process()).
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override
    {
        return static_cast<std::uint32_t>(std::max(0L, std::lround(value(pro::limiterLookahead) * 0.001 * rate_))) + 4;
    }
    [[nodiscard]] std::uint32_t maximumLatencySamples() const noexcept override
    {
        return static_cast<std::uint32_t>(std::lround(pro::limiterMaximumLookaheadMs * 0.001 * rate_)) + 5;
    }

    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext&) noexcept override
    {
        using namespace pro;
        clearAudio(output);
        const auto channels = std::min({ static_cast<std::size_t>(input.channelCount), static_cast<std::size_t>(output.channelCount),
                                         channels_ });
        const auto frames = std::min(input.frameCount, output.frameCount);
        const auto& style = limiterStyles[static_cast<std::size_t>(
            std::clamp(static_cast<int>(std::lround(value(limiterStyle))), 0, limiterStyleCount - 1))];
        const auto inputGain = dbToGain(value(limiterGain));
        const auto ceiling = dbToGain(value(limiterOutput));
        const auto lookahead = static_cast<std::size_t>(std::lround(value(limiterLookahead) * 0.001 * rate_));
        if (lookahead != lookahead_) {
            lookahead_ = lookahead;
            for (std::size_t channel = 0; channel < maximumChannels; ++channel) {
                minimum_[channel].clear();
                average_[channel].clear();
            }
        }
        const auto window = lookahead + 1;
        const auto releaseMs = static_cast<double>(value(limiterRelease)) * style.releaseScale;
        const auto slowAttack = smoothing(value(limiterAttack) * 4.0 * style.attackScale, rate_);
        const auto slowRelease = smoothing(releaseMs * 4.0, rate_);
        const auto rmsCoefficient = smoothing(60.0, rate_);
        const auto denseCoefficient = smoothing(2'000.0, rate_);
        const auto linkTransients = static_cast<double>(value(limiterLinkTransients));
        const auto linkRelease = static_cast<double>(value(limiterLinkRelease));
        auto factor = limiterOversamplingFactor(static_cast<int>(std::lround(value(limiterOversampling))));
        if (value(limiterTruePeak) >= 0.5f)
            factor = std::max(factor, 4);
        const auto& kernel = kernel_[static_cast<std::size_t>(factor - 1)];
        const auto dcFilter = value(limiterDcFilter) >= 0.5f;
        const auto dcCoefficient = std::exp(-2.0 * kPi * 5.0 / rate_);
        const auto unity = value(limiterUnity) >= 0.5f;
        const auto bits = static_cast<int>(std::lround(value(limiterDither)));
        const auto shaping = static_cast<int>(std::lround(value(limiterShaping)));
        const auto clipAmount = style.clip;
        // The audio waits for the detector: its look-ahead, and the four
        // samples the interpolation reads ahead of the one it guards.
        const auto latency = lookahead + 4;

        std::array<double, maximumChannels> x {};
        std::array<double, maximumChannels> wanted {};
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            auto deepestFast = 1.0;
            auto deepestSlow = 1.0;
            for (std::size_t channel = 0; channel < channels; ++channel) {
                auto sample = static_cast<double>(input.channel(static_cast<std::uint32_t>(channel))[frame]) * inputGain;
                if (dcFilter) {
                    auto& state = dc_[channel];
                    const auto y = sample - state[0] + dcCoefficient * state[1];
                    state[0] = sample;
                    state[1] = y;
                    sample = y;
                }
                // Aggressive: some of the peaks rounded before they are caught.
                if (clipAmount > 0.0) {
                    const auto knee = ceiling * 1.25;
                    sample = (1.0 - clipAmount) * sample + clipAmount * knee * std::tanh(sample / knee);
                }
                x[channel] = sample;
                // The slow stage rides the level under the ceiling.
                rms_[channel] = rmsCoefficient * rms_[channel] + (1.0 - rmsCoefficient) * sample * sample;
                if (style.slow > 0.0) {
                    const auto loud = std::sqrt(rms_[channel]) * 2.0;
                    const auto needed = loud > ceiling ? std::pow(ceiling / loud, style.slow) : 1.0;
                    auto& slow = slow_[channel];
                    slow = needed < slow ? slowAttack * slow + (1.0 - slowAttack) * needed
                                         : slowRelease * slow + (1.0 - slowRelease) * needed;
                    deepestSlow = std::min(deepestSlow, slow);
                }
                // The peak: the sample four back, and between it and each
                // of its neighbours.
                auto& history = history_[channel];
                std::copy(history.begin() + 1, history.end(), history.begin());
                history.back() = sample;
                auto peak = std::abs(history[4]);
                for (int phase = 1; phase < factor; ++phase) {
                    auto before = 0.0;
                    auto after = 0.0;
                    for (int tap = 0; tap < interpolationTaps; ++tap) {
                        const auto weight = kernel[static_cast<std::size_t>(phase)][static_cast<std::size_t>(tap)];
                        before += history[static_cast<std::size_t>(tap)] * weight;
                        after += history[static_cast<std::size_t>(tap + 1)] * weight;
                    }
                    peak = std::max({ peak, std::abs(before), std::abs(after) });
                }
                peaks_[channel] = peak;
            }
            for (std::size_t channel = 0; channel < channels; ++channel) {
                const auto slow = style.slow > 0.0 ? link(slow_[channel], deepestSlow, linkRelease) : 1.0;
                const auto peak = peaks_[channel] * slow;
                wanted[channel] = peak > ceiling ? ceiling / peak : 1.0;
                deepestFast = std::min(deepestFast, wanted[channel]);
            }
            auto deepestRelease = 1.0;
            for (std::size_t channel = 0; channel < channels; ++channel) {
                // Ahead of the peak: the smallest gain of the window, its
                // average a ramp down to it, reached as the peak comes out.
                const auto linked = link(wanted[channel], deepestFast, linkTransients);
                const auto ramp = average_[channel].push(minimum_[channel].push(linked, window), window);
                auto& release = release_[channel];
                dense_[channel] = denseCoefficient * dense_[channel] + (1.0 - denseCoefficient) * (release < 0.98 ? 1.0 : 0.0);
                if (ramp < release)
                    release = ramp;
                else
                    release += (ramp - release) * (1.0 - smoothing(releaseMs * (1.0 + style.adaptive * dense_[channel]), rate_));
                deepestRelease = std::min(deepestRelease, release);
            }
            for (std::size_t channel = 0; channel < channels; ++channel) {
                delay_[channel].push(x[channel]);
                const auto delayed = delay_[channel].readWhole(latency);
                const auto slow = style.slow > 0.0 ? link(slow_[channel], deepestSlow, linkRelease) : 1.0;
                auto sample = delayed * slow * link(release_[channel], deepestRelease, linkRelease);
                // Never past the ceiling, whatever slipped through.
                sample = std::clamp(sample, -ceiling, ceiling);
                if (unity)
                    sample /= inputGain;
                if (bits > 0)
                    sample = dither(sample, channel, bits, shaping);
                output.channel(static_cast<std::uint32_t>(channel))[frame] = static_cast<float>(sample);
            }
        }
        for (auto channel = static_cast<std::uint32_t>(channels); channel < std::min(input.channelCount, output.channelCount); ++channel)
            std::copy_n(input.channel(channel), frames, output.channel(channel));
    }

private:
    static double link(const double own, const double deepest, const double amount) noexcept
    {
        return amount * deepest + (1.0 - amount) * own;
    }

    double uniform() noexcept
    {
        random_ ^= random_ << 13;
        random_ ^= random_ >> 17;
        random_ ^= random_ << 5;
        return static_cast<double>(random_ & 0xffffffu) / 16'777'215.0 - 0.5;
    }

    // To 24, 20, 18 or 16 bits: triangular dither, the error shaped out of
    // where the ear is most sensitive.
    double dither(const double sample, const std::size_t channel, const int setting, const int shaping) noexcept
    {
        static constexpr std::array<int, 5> depths { 0, 24, 20, 18, 16 };
        const auto steps = std::ldexp(1.0, depths[static_cast<std::size_t>(std::clamp(setting, 0, 4))] - 1);
        static constexpr std::array<std::array<double, 5>, 3> filters { {
            { 1.0, 0.0, 0.0, 0.0, 0.0 },
            { 1.623, -0.982, 0.109, 0.0, 0.0 },
            { 2.033, -2.165, 1.959, -1.590, 0.6149 },
        } };
        const auto& filter = filters[static_cast<std::size_t>(std::clamp(shaping, 0, 2))];
        auto& errors = errors_[channel];
        auto shaped = 0.0;
        for (std::size_t tap = 0; tap < errors.size(); ++tap)
            shaped += filter[tap] * errors[tap];
        const auto wantedValue = sample * steps - shaped;
        const auto quantised = std::round(wantedValue + uniform() + uniform());
        std::copy_backward(errors.begin(), errors.end() - 1, errors.end());
        errors[0] = std::clamp(quantised - wantedValue, -4.0, 4.0);
        return std::clamp(quantised / steps, -1.0, 1.0);
    }

    double rate_ { 48'000.0 };
    std::size_t channels_ { 2 };
    std::array<FractionalDelay, maximumChannels> delay_;
    std::array<SlidingMinimum, maximumChannels> minimum_;
    std::array<MovingAverage, maximumChannels> average_;
    std::array<std::array<double, historyLength>, maximumChannels> history_ {};
    std::array<std::array<std::array<double, interpolationTaps>, maximumFactor>, maximumFactor> kernel_ {};
    std::array<double, maximumChannels> peaks_ {};
    std::array<double, maximumChannels> release_ {};
    std::array<double, maximumChannels> slow_ {};
    std::array<double, maximumChannels> rms_ {};
    std::array<double, maximumChannels> dense_ {};
    std::array<std::array<double, 2>, maximumChannels> dc_ {};
    std::array<std::array<double, 5>, maximumChannels> errors_ {};
    std::size_t lookahead_ { std::numeric_limits<std::size_t>::max() };
    std::uint32_t random_ { 0x9e3779b9u };
};

// ---- Filtre Pro ------------------------------------------------------------------------------------

class FilterPro final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;

    static constexpr int controlStep = 16;

    void prepare(const ProcessSpec& spec) override
    {
        rate_ = std::max(1.0, spec.sampleRate);
        for (auto& unit : combs_)
            for (auto& comb : unit)
                comb.prepare(static_cast<std::size_t>(rate_ / 15.0) + 4);
        reset();
    }

    void reset() noexcept override
    {
        for (auto& unit : states_)
            for (auto& channel : unit)
                channel = {};
        for (auto& unit : combs_)
            for (auto& comb : unit)
                comb.clear();
        lfoPhase_ = { 0.0, 0.0 };
        lfoCycle_ = { -1.0, -1.0 };
        random_ = 0x2545f491u;
        for (auto& lfo : held_)
            lfo = { 0.0, 0.0, 0.0, 0.0 };
        follower_ = 0.0;
        counter_ = 0;
        mix_ = -1.0;
    }

    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext& context) noexcept override
    {
        using namespace pro;
        clearAudio(output);
        const auto frames = std::min(input.frameCount, output.frameCount);
        const auto channels = std::min(input.channelCount, output.channelCount);
        if (channels == 0)
            return;
        const auto routing = std::clamp(static_cast<int>(std::lround(value(filterRouting))), 0, filterRoutingCount - 1);
        const auto followAttack = smoothing(value(filterFollowAttack), rate_);
        const auto followRelease = smoothing(value(filterFollowRelease), rate_);
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            // The envelope of what comes in.
            auto level = 0.0;
            for (std::uint32_t channel = 0; channel < channels; ++channel)
                level = std::max(level, static_cast<double>(std::abs(input.channel(channel)[frame])));
            follower_ = level > follower_ ? followAttack * follower_ + (1.0 - followAttack) * level
                                          : followRelease * follower_ + (1.0 - followRelease) * level;
            if (counter_ == 0)
                control(context);
            counter_ = (counter_ + 1) % controlStep;
            for (std::uint32_t channel = 0; channel < channels; ++channel) {
                const auto side = std::min<std::uint32_t>(channel, 1);
                const auto x = static_cast<double>(input.channel(channel)[frame]);
                auto y = 0.0;
                if (routing == routingParallel) {
                    auto count = 0;
                    for (int unit = 0; unit < filterUnits; ++unit)
                        if (on_[static_cast<std::size_t>(unit)]) {
                            y += runUnit(unit, side, channel, x);
                            ++count;
                        }
                    y = count > 0 ? y / static_cast<double>(count) : x;
                } else if (routing == routingPairs) {
                    auto a = x;
                    auto b = x;
                    for (int unit = 0; unit < 2; ++unit)
                        if (on_[static_cast<std::size_t>(unit)])
                            a = runUnit(unit, side, channel, a);
                    for (int unit = 2; unit < 4; ++unit)
                        if (on_[static_cast<std::size_t>(unit)])
                            b = runUnit(unit, side, channel, b);
                    y = 0.5 * (a + b);
                } else {
                    y = x;
                    for (int unit = 0; unit < filterUnits; ++unit)
                        if (on_[static_cast<std::size_t>(unit)])
                            y = runUnit(unit, side, channel, y);
                }
                output.channel(channel)[frame] = static_cast<float>((x * (1.0 - mix_) + y * mix_) * outputGain_);
            }
        }
    }

private:
    struct Section {
        double a1 {};
        double a2 {};
        double a3 {};
        double k {};
        double peak {};
    };
    struct Unit {
        std::array<Section, 4> sections {};
        int stages { 1 };
        int type {};
        double drive { 1.0 };
        double combDelay { 100.0 };
        double combFeedback {};
    };
    struct SectionState {
        double ic1 {};
        double ic2 {};
    };

    // Once every sixteen samples: the LFOs, the follower and the matrix move
    // the filters, whose sections are designed again.
    void control(const ProcessContext& context) noexcept
    {
        using namespace pro;
        const auto step = static_cast<double>(controlStep) / rate_;
        const auto tempo = std::max(20.0, context.transport.beatsPerMinute);
        const auto playing = context.transport.state == PlaybackState::playing || context.transport.state == PlaybackState::recording;
        std::array<std::array<double, 2>, 2> lfo {};
        for (int index = 0; index < 2; ++index) {
            const auto base = index == 0 ? filterLfo1Rate : filterLfo2Rate;
            const auto shape = std::clamp(static_cast<int>(std::lround(value(base + 1))), 0, lfoShapeCount - 1);
            const auto* division = noteDivisionFor(value(base + 2));
            const auto stereo = static_cast<double>(value(base + 3));
            auto& phase = lfoPhase_[static_cast<std::size_t>(index)];
            const auto previous = phase;
            auto newCycle = false;
            if (division != nullptr && playing) {
                // In time with the song: its position says where the cycle is,
                // a new random value at each new cycle of the song (not at each
                // call: the phase kept is the wrapped one).
                phase = context.transport.quarterNotePosition / division->beats;
                auto& cycle = lfoCycle_[static_cast<std::size_t>(index)];
                newCycle = std::floor(phase) != cycle;
                cycle = std::floor(phase);
            } else {
                const auto hz = division != nullptr ? tempo / 60.0 / division->beats : static_cast<double>(value(base));
                phase += hz * step;
                newCycle = std::floor(phase) != std::floor(previous);
            }
            auto& held = held_[static_cast<std::size_t>(index)];
            if (newCycle || held[3] == 0.0) {
                held[0] = held[1];
                held[1] = uniform();
                held[2] = uniform();
                held[3] = 1.0;
            }
            for (int side = 0; side < 2; ++side) {
                const auto at = phase + (side == 1 ? stereo : 0.0);
                const auto fraction = at - std::floor(at);
                auto random = held[1];
                if (shape == 6)
                    random = held[0] + (held[1] - held[0]) * (0.5 - 0.5 * std::cos(kPi * fraction));
                else if (side == 1 && stereo > 0.0)
                    random = held[2];
                lfo[static_cast<std::size_t>(index)][static_cast<std::size_t>(side)] = lfoShapeValue(shape, at, random);
            }
            phase -= std::floor(phase);
        }
        const auto follower = std::clamp((gainToDb(follower_ * dbToGain(value(filterFollowGain))) + 60.0) / 60.0, 0.0, 1.0);

        for (int side = 0; side < 2; ++side) {
            std::array<double, modulationDestinationCount> moved {};
            for (int slot = 0; slot < filterMatrixSlots; ++slot) {
                const auto source = static_cast<int>(std::lround(value(matrixIndex(slot, matrixSource))));
                const auto destination = static_cast<int>(std::lround(value(matrixIndex(slot, matrixDestination))));
                const auto amount = static_cast<double>(value(matrixIndex(slot, matrixAmount)));
                if (source == sourceNone || destination <= destinationNone || destination >= modulationDestinationCount)
                    continue;
                const auto signal = source == sourceLfo1   ? lfo[0][static_cast<std::size_t>(side)]
                                    : source == sourceLfo2 ? lfo[1][static_cast<std::size_t>(side)]
                                                           : follower;
                moved[static_cast<std::size_t>(destination)] += signal * amount;
            }
            for (int unit = 0; unit < filterUnits; ++unit) {
                const auto u = static_cast<std::size_t>(unit);
                on_[u] = value(filterIndex(unit, unitOn)) >= 0.5f;
                if (!on_[u])
                    continue;
                const auto cutoffDestination = destinationCutoff1 + unit * 2;
                const auto octaves = (moved[static_cast<std::size_t>(cutoffDestination)] + moved[destinationAllCutoffs]) * modulationOctaves
                                     + static_cast<double>(value(filterIndex(unit, unitSpread))) * (side == 0 ? -0.5 : 0.5);
                const auto cutoff = std::clamp(static_cast<double>(value(filterIndex(unit, unitCutoff))) * std::pow(2.0, octaves), 20.0,
                                               std::min(20'000.0, rate_ * 0.45));
                const auto resonance = std::clamp(static_cast<double>(value(filterIndex(unit, unitResonance)))
                                                      + moved[static_cast<std::size_t>(cutoffDestination + 1)],
                                                  0.0, 1.0);
                auto driveDb = static_cast<double>(value(filterIndex(unit, unitDrive)));
                if (unit == 0)
                    driveDb += moved[destinationDrive1] * 36.0;
                if (unit == 1)
                    driveDb += moved[destinationDrive2] * 36.0;
                auto& design = units_[u][static_cast<std::size_t>(side)];
                design.type = std::clamp(static_cast<int>(std::lround(value(filterIndex(unit, unitType)))), 0, filterTypeCount - 1);
                design.drive = dbToGain(std::clamp(driveDb, 0.0, 36.0));
                design.combDelay = std::clamp(rate_ / cutoff, 2.0, rate_ / 15.0);
                design.combFeedback = filterCombFeedback(resonance);
                design.stages = design.type == filterPeak ? 1
                                                          : filterStages(std::clamp(static_cast<int>(std::lround(
                                                                                        value(filterIndex(unit, unitSlope)))),
                                                                                    0, filterSlopeCount - 1));
                const auto g = std::tan(kPi * cutoff / rate_);
                for (int stage = 0; stage < design.stages; ++stage) {
                    const auto last = stage == design.stages - 1;
                    const auto q = last ? filterQ(resonance) : 0.7071;
                    auto& section = design.sections[static_cast<std::size_t>(stage)];
                    auto k = 1.0 / q;
                    section.peak = 0.0;
                    if (design.type == filterPeak) {
                        const auto a = std::pow(10.0, filterPeakDb(resonance) / 40.0);
                        k = 1.0 / (q * a);
                        section.peak = k * (a * a - 1.0);
                    }
                    section.k = k;
                    section.a1 = 1.0 / (1.0 + g * (g + k));
                    section.a2 = g * section.a1;
                    section.a3 = g * section.a2;
                }
            }
            if (side == 0) {
                const auto mix = std::clamp(static_cast<double>(value(filterMix)) + moved[destinationMix], 0.0, 1.0);
                mix_ = mix;
                outputGain_ = dbToGain(std::clamp(static_cast<double>(value(filterOutput)) + moved[destinationOutput] * 24.0, -48.0, 24.0));
            }
        }
    }

    double runUnit(const int unit, const std::uint32_t side, const std::uint32_t channel, double x) noexcept
    {
        using namespace pro;
        const auto u = static_cast<std::size_t>(unit);
        const auto& design = units_[u][side];
        if (design.drive > 1.001)
            x = std::tanh(x * design.drive) / std::sqrt(design.drive);
        if (design.type == filterComb) {
            auto& comb = combs_[u][std::min<std::uint32_t>(channel, 7)];
            const auto y = x + design.combFeedback * comb.read(design.combDelay);
            comb.push(y);
            return y * (1.0 - design.combFeedback);
        }
        auto& states = states_[u][std::min<std::uint32_t>(channel, 7)];
        auto y = x;
        for (int stage = 0; stage < design.stages; ++stage) {
            const auto& s = design.sections[static_cast<std::size_t>(stage)];
            auto& state = states[static_cast<std::size_t>(stage)];
            const auto v3 = y - state.ic2;
            const auto v1 = s.a1 * state.ic1 + s.a2 * v3;
            const auto v2 = state.ic2 + s.a2 * state.ic1 + s.a3 * v3;
            state.ic1 = 2.0 * v1 - state.ic1;
            state.ic2 = 2.0 * v2 - state.ic2;
            switch (design.type) {
            case filterHighPass: y = y - s.k * v1 - v2; break;
            case filterBandPass: y = s.k * v1; break;
            case filterNotch: y = y - s.k * v1; break;
            case filterPeak: y = y + s.peak * v1; break;
            default: y = v2; break;
            }
        }
        return y;
    }

    double uniform() noexcept
    {
        random_ ^= random_ << 13;
        random_ ^= random_ >> 17;
        random_ ^= random_ << 5;
        return static_cast<double>(random_ & 0xffffffu) / 8'388'607.5 - 1.0;
    }

    double rate_ { 48'000.0 };
    std::array<std::array<Unit, 2>, pro::filterUnits> units_ {};
    std::array<std::array<std::array<SectionState, 4>, 8>, pro::filterUnits> states_ {};
    // A line per channel (up to 7.1), as the filters' states.
    std::array<std::array<FractionalDelay, 8>, pro::filterUnits> combs_;
    std::array<bool, pro::filterUnits> on_ {};
    std::array<double, 2> lfoPhase_ {};
    // In time with the song: the cycle last heard (its random value is drawn once per cycle).
    std::array<double, 2> lfoCycle_ { -1.0, -1.0 };
    // Each LFO's random values: the last, the next, the right side's, and
    // whether they were drawn.
    std::array<std::array<double, 4>, 2> held_ {};
    std::uint32_t random_ { 0x2545f491u };
    double follower_ {};
    int counter_ {};
    double mix_ { -1.0 };
    double outputGain_ { 1.0 };
};

} // namespace

std::unique_ptr<EffectProcessor> makeProEffect(const EffectDescriptor& descriptor)
{
    switch (descriptor.kind) {
    case NativeDspNodeKind::reverbPro: return std::make_unique<ReverbPro>(descriptor);
    case NativeDspNodeKind::compressorPro: return std::make_unique<CompressorPro>(descriptor);
    case NativeDspNodeKind::limiterPro: return std::make_unique<LimiterPro>(descriptor);
    case NativeDspNodeKind::filterPro: return std::make_unique<FilterPro>(descriptor);
    // The amp, cabinet and microphone simulators (AmpEffects.cpp).
    default: return makeAmpEffect(descriptor);
    }
}

} // namespace cydaw::engine
