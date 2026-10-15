#include <cydaw/engine/ResonanceEffects.h>

#include <cydaw/engine/AudioBuffer.h>
#include <cydaw/engine/EffectCatalogue.h>
#include <cydaw/engine/ResonanceDesign.h>
#include <cydaw/engine/Transport.h>

#include "DspParts.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace cydaw::engine {

namespace {

using namespace dsp;
using namespace resonance;

// Below this, a sample is silence.
constexpr double kSilence = 1.0e-9;

// The last word on a resonance that could run away: untouched up to 0.8,
// bent softly towards 1 above.
double softLimit(const double x) noexcept
{
    const auto size = std::abs(x);
    if (size <= 0.8)
        return x;
    const auto bent = 0.8 + 0.2 * std::tanh((size - 0.8) / 0.2);
    return x < 0.0 ? -bent : bent;
}

// Inside a loop: linear up to 2, never past 4, so a sound fed for ever at
// a string's own note saturates rather than grows.
double bend(const double x) noexcept
{
    const auto size = std::abs(x);
    if (size <= 2.0)
        return x;
    const auto bent = 2.0 + 2.0 * std::tanh((size - 2.0) * 0.5);
    return x < 0.0 ? -bent : bent;
}

// A line read between samples by the cubic through four neighbours
// (Lagrange): in tune at any length, and unlike a linear read it hardly
// dulls the top, so a loop keeps its brightness. Lengths of 2 samples and
// more.
class CubicLine {
public:
    void prepare(const std::size_t longest)
    {
        std::size_t size = 1;
        while (size < longest + 8)
            size <<= 1;
        buffer_.assign(size, 0.0);
        mask_ = size - 1;
        write_ = 0;
    }
    void reset() noexcept { std::fill(buffer_.begin(), buffer_.end(), 0.0); }
    void push(const double x) noexcept
    {
        buffer_[write_] = x;
        write_ = (write_ + 1) & mask_;
    }
    [[nodiscard]] double longest() const noexcept { return static_cast<double>(mask_) - 4.0; }
    // What was pushed `delay` samples before the sample about to be pushed.
    [[nodiscard]] double read(const double delay) const noexcept
    {
        const auto position = static_cast<double>(write_) - std::clamp(delay, 2.0, longest());
        const auto whole = std::floor(position);
        const auto t = position - whole;
        const auto index = static_cast<std::int64_t>(whole);
        const auto before = buffer_[static_cast<std::size_t>(index - 1) & mask_];
        const auto a = buffer_[static_cast<std::size_t>(index) & mask_];
        const auto b = buffer_[static_cast<std::size_t>(index + 1) & mask_];
        const auto after = buffer_[static_cast<std::size_t>(index + 2) & mask_];
        return -t * (t - 1.0) * (t - 2.0) / 6.0 * before + (t + 1.0) * (t - 1.0) * (t - 2.0) * 0.5 * a
               - (t + 1.0) * t * (t - 2.0) * 0.5 * b + (t + 1.0) * t * (t - 1.0) / 6.0 * after;
    }

private:
    std::vector<double> buffer_;
    std::size_t mask_ {};
    std::size_t write_ {};
};

// Watches for silence: a quarter of a second with nothing coming in and
// nothing ringing, and the effect clears its states (no tail left to sink
// into denormals) and rests until sound comes back.
struct Rest {
    double quiet {};
    bool asleep {};
    // Whether this block must be worked.
    bool wake(const double inputPeak) noexcept
    {
        if (inputPeak > kSilence)
            asleep = false;
        return !asleep;
    }
    // After a worked block: true when the states should now be cleared.
    bool settle(const double inputPeak, const double wetPeak, const std::uint32_t frames, const double rate) noexcept
    {
        if (inputPeak > kSilence || wetPeak > kSilence) {
            quiet = 0.0;
            return false;
        }
        quiet += static_cast<double>(frames);
        if (quiet < rate * 0.25)
            return false;
        asleep = true;
        quiet = 0.0;
        return true;
    }
};

// The peak of the first channels of a block (those the effect works on).
double peakOf(const AudioBlockView input, const std::size_t channels, const std::uint32_t frames) noexcept
{
    auto peak = 0.0;
    for (std::size_t channel = 0; channel < channels; ++channel) {
        const auto* samples = input.channel(static_cast<std::uint32_t>(channel));
        for (std::uint32_t frame = 0; frame < frames; ++frame)
            peak = std::max(peak, static_cast<double>(std::abs(samples[frame])));
    }
    return peak;
}

// The channels above the two the effect works on, passed through.
void passRest(const AudioBlockView input, const AudioBlockView output, const std::size_t from, const std::uint32_t frames) noexcept
{
    for (auto channel = from; channel < std::min<std::size_t>(input.channelCount, output.channelCount); ++channel)
        std::copy_n(input.channel(static_cast<std::uint32_t>(channel)), frames, output.channel(static_cast<std::uint32_t>(channel)));
}

// ---- Résonateurs ---------------------------------------------------------------------------------

// Five strings the sound makes ring: each a feedback comb whose line is
// its period (read between samples), a low-pass in its loop for its colour
// (and, as metal, all-pass sections that stretch its partials), its loop
// gain set so its note falls 60 dB in the decay's time. The line is
// shortened by what the filters delay at the note, so it rings in tune.
class Resonateurs final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;

    void prepare(const ProcessSpec& spec) override
    {
        rate_ = std::max(1'000.0, spec.sampleRate);
        channels_ = std::min<std::size_t>(kChannels, std::max<std::uint32_t>(1, spec.channelCount));
        // The longest period: the lowest note a voice holds.
        const auto longest = static_cast<std::size_t>(rate_ / resonanceHzOfNote(12.0)) + 8;
        for (auto& voice : voices_)
            voice.line.prepare(longest);
        highPass_.c = designEqBand(EqBandType::lowCut, resonateursInputHighPassHz, 0.0, 0.7071, 0, 0, rate_);
        tilt_.design(resonateursTiltHz, rate_);
        release_ = smoothing(150.0, rate_);
        designed_.fill(-1.0e9f);
        reset();
    }

    void reset() noexcept override
    {
        clearStates();
        rest_ = {};
    }

    [[nodiscard]] std::size_t readings(float* into, const std::size_t count) const noexcept override
    {
        const auto shown = std::min(count, readings_.size());
        for (std::size_t index = 0; index < shown; ++index)
            into[index] = readings_[index].load(std::memory_order_relaxed);
        return shown;
    }

    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext&) noexcept override
    {
        clearAudio(output);
        const auto channels = std::min<std::size_t>({ input.channelCount, output.channelCount, channels_, 2 });
        const auto frames = std::min(input.frameCount, output.frameCount);
        std::array<float, resonateursParameterCount> values {};
        for (int index = 0; index < resonateursParameterCount; ++index)
            values[static_cast<std::size_t>(index)] = value(static_cast<std::size_t>(index));
        if (values != designed_)
            design(values);
        const auto mix = std::clamp(static_cast<double>(values[resonateursMix]), 0.0, 1.0);
        const auto gain = dbToGain(std::clamp(static_cast<double>(values[resonateursOutput]), -24.0, 24.0));
        const auto tone = std::clamp(static_cast<double>(values[resonateursTone]), -1.0, 1.0);
        const auto lowGain = dbToGain(-resonateursTiltDb * tone);
        const auto highGain = dbToGain(resonateursTiltDb * tone);
        const auto inputPeak = peakOf(input, channels, frames);
        const auto working = rest_.wake(inputPeak);
        auto wetPeak = 0.0;
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            const auto first = static_cast<double>(input.channel(0)[frame]);
            const auto second = channels > 1 ? static_cast<double>(input.channel(1)[frame]) : first;
            auto left = 0.0;
            auto right = 0.0;
            auto middle = 0.0;
            if (working) {
                // The strings hear the middle, cleared of rumble, tilted.
                auto x = highPass_(0.5 * (first + second));
                const auto low = tilt_.lowPass(x);
                x = low * lowGain + (x - low) * highGain;
                for (auto& voice : voices_) {
                    auto v = voice.loss(voice.line.read(voice.delay));
                    for (int section = 0; section < voice.sections; ++section) {
                        auto& state = voice.dispersion[static_cast<std::size_t>(section)];
                        const auto passed = voice.allpass * v + state;
                        state = v - voice.allpass * passed;
                        v = passed;
                    }
                    v = bend(voice.feedback * v + voice.input * x);
                    voice.line.push(v);
                    voice.ring = std::max(std::abs(v), voice.ring * release_);
                    const auto heard = v * voice.level;
                    left += heard * voice.left;
                    right += heard * voice.right;
                    middle += heard;
                    wetPeak = std::max(wetPeak, std::abs(v));
                }
                left = softLimit(left * resonateursSumGain);
                right = softLimit(right * resonateursSumGain);
                middle = softLimit(middle * resonateursSumGain);
            }
            if (channels == 1) {
                // One channel: every voice in the middle.
                output.channel(0)[frame] = static_cast<float>((first * (1.0 - mix) + middle * mix) * gain);
            } else {
                output.channel(0)[frame] = static_cast<float>((first * (1.0 - mix) + left * mix) * gain);
                output.channel(1)[frame] = static_cast<float>((second * (1.0 - mix) + right * mix) * gain);
            }
        }
        passRest(input, output, channels, frames);
        if (working && rest_.settle(inputPeak, wetPeak, frames, rate_))
            clearStates();
        for (std::size_t voice = 0; voice < voices_.size(); ++voice)
            readings_[voice].store(static_cast<float>(voices_[voice].ring), std::memory_order_relaxed);
    }

private:
    static constexpr double kRootHalf = 0.70710678118654752;

    struct Voice {
        CubicLine line;
        Biquad loss;
        std::array<double, resonateursDispersionSections> dispersion {};
        int sections {};
        double allpass {};
        double delay { 100.0 };
        double feedback {};
        double input {};
        double level {};
        double left {};
        double right {};
        double ring {};
    };

    void clearStates() noexcept
    {
        for (auto& voice : voices_) {
            voice.line.reset();
            voice.loss.reset();
            voice.dispersion.fill(0.0);
            voice.ring = 0.0;
        }
        highPass_.reset();
        tilt_.state = 0.0;
        for (auto& reading : readings_)
            reading.store(0.0f, std::memory_order_relaxed);
    }

    void design(const std::array<float, resonateursParameterCount>& values) noexcept
    {
        const auto mode = resonateursModeOf(values[resonateursMode]);
        const auto seconds = resonateursSecondsOf(values[resonateursDecay], mode);
        for (int index = 0; index < resonateursVoices; ++index) {
            auto& voice = voices_[static_cast<std::size_t>(index)];
            const auto hz = std::min(resonanceHzOfNote(resonateursNoteOf(values.data(), index)), rate_ * 0.2);
            const auto period = rate_ / hz;
            const auto w = 2.0 * resonancePi * hz / rate_;
            const auto z = std::polar(1.0, -w);
            voice.loss.c = designEqBand(EqBandType::highCut, resonateursCutoffOf(hz, values[resonateursColour], mode, rate_), 0.0, 0.7071,
                                        0, 0, rate_);
            const auto& c = voice.loss.c;
            const auto response = (c.b0 + c.b1 * z + c.b2 * z * z) / (1.0 + c.a1 * z + c.a2 * z * z);
            // What the loop's filters delay at the note comes off the line.
            auto filtersDelay = -std::arg(response) / w;
            voice.sections = mode == 2 ? resonateursDispersionSections : 0;
            voice.allpass = resonateursDispersionOf(period);
            if (voice.sections > 0) {
                const auto passed = (voice.allpass + z) / (1.0 + voice.allpass * z);
                filtersDelay += static_cast<double>(voice.sections) * -std::arg(passed) / w;
            }
            voice.delay = std::clamp(period - filtersDelay, 2.0, voice.line.longest());
            // The loop's gain: the note falls 60 dB in the decay's time, the
            // low-pass's loss at the note made up, never 1 or more anywhere.
            const auto wanted = std::exp(-resonanceT60 * period / (seconds * rate_));
            voice.feedback = std::min(0.9999, wanted / std::max(std::abs(response), 1.0e-6));
            // White noise comes out about as loud as it goes in.
            voice.input = std::sqrt(std::max(0.0, 1.0 - voice.feedback * voice.feedback));
            voice.level = resonateursLevelOf(values.data(), index);
            // Equal power, the middle as loud in each side as alone.
            const auto angle = (resonateursPanOf(index, values[resonateursWidth]) + 1.0) * resonancePi * 0.25;
            voice.left = std::cos(angle) / kRootHalf;
            voice.right = std::sin(angle) / kRootHalf;
        }
        designed_ = values;
    }

    double rate_ { 48'000.0 };
    std::size_t channels_ { 2 };
    std::array<Voice, resonateursVoices> voices_ {};
    Biquad highPass_;
    OnePole tilt_;
    double release_ {};
    Rest rest_;
    std::array<float, resonateursParameterCount> designed_ {};
    std::array<std::atomic<float>, resonateursReadingCount> readings_ {};
};

// ---- Corps ---------------------------------------------------------------------------------------

// A resonant body: a bank of modes per side, each a decaying rotation (a
// two-pole resonator) at its frequency, struck by the sound as much as the
// object moves where it is heard. Every mode starts as loud as its weight
// says whatever its decay (a struck object's way: the short ones carry
// little), the whole bank scaled so music comes out about as loud as it
// went in.
class Corps final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;

    void prepare(const ProcessSpec& spec) override
    {
        rate_ = std::max(1'000.0, spec.sampleRate);
        channels_ = std::min<std::size_t>(kChannels, std::max<std::uint32_t>(1, spec.channelCount));
        designed_.fill(-1.0e9f);
        reset();
    }

    void reset() noexcept override
    {
        clearStates();
        rest_ = {};
    }

    [[nodiscard]] std::size_t readings(float* into, const std::size_t count) const noexcept override
    {
        const auto shown = std::min(count, readings_.size());
        for (std::size_t index = 0; index < shown; ++index)
            into[index] = readings_[index].load(std::memory_order_relaxed);
        return shown;
    }

    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext&) noexcept override
    {
        clearAudio(output);
        const auto channels = std::min<std::size_t>({ input.channelCount, output.channelCount, channels_, 2 });
        const auto frames = std::min(input.frameCount, output.frameCount);
        std::array<float, corpsParameterCount> values {};
        for (int index = 0; index < corpsParameterCount; ++index)
            values[static_cast<std::size_t>(index)] = value(static_cast<std::size_t>(index));
        if (values != designed_)
            design(values);
        const auto mix = std::clamp(static_cast<double>(values[corpsMix]), 0.0, 1.0);
        const auto gain = dbToGain(std::clamp(static_cast<double>(values[corpsOutput]), -24.0, 24.0));
        const auto sides = channels > 1 ? std::size_t { 2 } : std::size_t { 1 };
        const auto inputPeak = peakOf(input, channels, frames);
        const auto working = rest_.wake(inputPeak);
        auto wetPeak = 0.0;
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            const auto first = static_cast<double>(input.channel(0)[frame]);
            const auto second = channels > 1 ? static_cast<double>(input.channel(1)[frame]) : first;
            std::array<double, 2> wet {};
            if (working) {
                const auto x = lowCut_(0.5 * (first + second));
                for (std::size_t side = 0; side < sides; ++side) {
                    auto& bank = banks_[side];
                    auto sum = 0.0;
                    for (int mode = 0; mode < bank.count; ++mode) {
                        const auto k = static_cast<std::size_t>(mode);
                        const auto re = bank.c[k] * bank.re[k] - bank.s[k] * bank.im[k] + bank.drive[k] * x;
                        bank.im[k] = bank.s[k] * bank.re[k] + bank.c[k] * bank.im[k];
                        bank.re[k] = re;
                        sum += bank.im[k];
                    }
                    wetPeak = std::max(wetPeak, std::abs(sum));
                    wet[side] = softLimit(sum);
                }
            }
            if (channels == 1) {
                output.channel(0)[frame] = static_cast<float>((first * (1.0 - mix) + wet[0] * mix) * gain);
            } else {
                output.channel(0)[frame] = static_cast<float>((first * (1.0 - mix) + wet[0] * mix) * gain);
                output.channel(1)[frame] = static_cast<float>((second * (1.0 - mix) + wet[1] * mix) * gain);
            }
        }
        passRest(input, output, channels, frames);
        if (working) {
            // Below the silence, a mode is let go (its tail would sink into
            // denormals); what still rings keeps the effect awake.
            for (auto& bank : banks_)
                for (int mode = 0; mode < bank.count; ++mode) {
                    const auto k = static_cast<std::size_t>(mode);
                    const auto ringing = std::abs(bank.re[k]) + std::abs(bank.im[k]);
                    if (ringing < 1.0e-15)
                        bank.re[k] = bank.im[k] = 0.0;
                    wetPeak = std::max(wetPeak, ringing);
                }
            if (rest_.settle(inputPeak, wetPeak, frames, rate_))
                clearStates();
        }
        const auto& left = banks_[0];
        for (int mode = 0; mode < corpsReadingCount; ++mode) {
            const auto k = static_cast<std::size_t>(mode);
            const auto ringing = mode < left.count ? std::hypot(left.re[k], left.im[k]) : 0.0;
            readings_[k].store(static_cast<float>(ringing), std::memory_order_relaxed);
        }
    }

private:
    struct Bank {
        int count {};
        std::array<double, corpsModeCount> c {};
        std::array<double, corpsModeCount> s {};
        std::array<double, corpsModeCount> drive {};
        std::array<double, corpsModeCount> re {};
        std::array<double, corpsModeCount> im {};
    };

    void clearStates() noexcept
    {
        for (auto& bank : banks_) {
            bank.re.fill(0.0);
            bank.im.fill(0.0);
        }
        lowCut_.reset();
        for (auto& reading : readings_)
            reading.store(0.0f, std::memory_order_relaxed);
    }

    void design(const std::array<float, corpsParameterCount>& values) noexcept
    {
        lowCut_.c = designEqBand(EqBandType::lowCut, std::clamp(static_cast<double>(values[corpsLowCut]), 20.0, 2'000.0), 0.0, 0.7071, 0, 0,
                                 rate_);
        for (int side = 0; side < 2; ++side) {
            auto& bank = banks_[static_cast<std::size_t>(side)];
            const auto modes = corpsModesOf(values.data(), side, rate_);
            // White noise's power through a mode of unit drive is
            // 1 / (2 (1 - r^2)); music gives the low modes more.
            auto power = 0.0;
            for (int mode = 0; mode < modes.count; ++mode) {
                const auto k = static_cast<std::size_t>(mode);
                const auto r = std::exp(-resonanceT60 / (modes.seconds[k] * rate_));
                const auto w = 2.0 * resonancePi * modes.hz[k] / rate_;
                bank.c[k] = r * std::cos(w);
                bank.s[k] = r * std::sin(w);
                power += modes.weight[k] * modes.weight[k] / (2.0 * (1.0 - r * r))
                         * std::pow(corpsLevelPivotHz / modes.hz[k], corpsLevelTilt);
            }
            const auto norm = 1.0 / std::sqrt(std::max(power, 1.0e-12));
            for (int mode = 0; mode < corpsModeCount; ++mode) {
                const auto k = static_cast<std::size_t>(mode);
                if (mode < modes.count) {
                    bank.drive[k] = modes.weight[k] * norm;
                } else {
                    bank.drive[k] = 0.0;
                    bank.re[k] = bank.im[k] = 0.0;
                }
            }
            bank.count = modes.count;
        }
        designed_ = values;
    }

    double rate_ { 48'000.0 };
    std::size_t channels_ { 2 };
    std::array<Bank, 2> banks_ {};
    Biquad lowCut_;
    Rest rest_;
    std::array<float, corpsParameterCount> designed_ {};
    std::array<std::atomic<float>, corpsReadingCount> readings_ {};
};

// ---- Peigne --------------------------------------------------------------------------------------

// A tuned comb per side: the sound plus, a period later, either what came
// out (feedback: peaks that ring at the frequency and its multiples, or at
// its odd half-multiples when the feedback is negative) or what came in
// (feedforward: notches), through a low-pass. The period follows an LFO in
// octaves, the two sides apart by the spread; the lines ramp to their new
// lengths every 16 samples.
class Peigne final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;

    void prepare(const ProcessSpec& spec) override
    {
        rate_ = std::max(1'000.0, spec.sampleRate);
        channels_ = std::min<std::size_t>(kChannels, std::max<std::uint32_t>(1, spec.channelCount));
        // The longest line: the lowest frequency, spread down, an octave below.
        const auto longest = static_cast<std::size_t>(rate_ / 9.0) + 8;
        for (auto& side : sides_)
            side.line.prepare(longest);
        dcPole_ = 1.0 - 2.0 * resonancePi * 8.0 / rate_;
        reset();
    }

    void reset() noexcept override
    {
        clearStates();
        rest_ = {};
        phase_ = 0.0;
    }

    [[nodiscard]] std::size_t readings(float* into, const std::size_t count) const noexcept override
    {
        const auto shown = std::min(count, readings_.size());
        for (std::size_t index = 0; index < shown; ++index)
            into[index] = readings_[index].load(std::memory_order_relaxed);
        return shown;
    }

    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext& context) noexcept override
    {
        clearAudio(output);
        const auto channels = std::min<std::size_t>({ input.channelCount, output.channelCount, channels_, 2 });
        const auto frames = std::min(input.frameCount, output.frameCount);
        std::array<float, peigneParameterCount> values {};
        for (int index = 0; index < peigneParameterCount; ++index)
            values[static_cast<std::size_t>(index)] = value(static_cast<std::size_t>(index));
        const auto mode = peigneModeOf(values[peigneMode]);
        const auto feedback = std::clamp(static_cast<double>(values[peigneFeedback]), -0.98, 0.98);
        const auto pole = resonanceOnePolePoleOf(static_cast<double>(values[peigneDamping]), rate_);
        const auto norm = peigneNormOf(values[peigneFeedback], mode);
        const auto mix = std::clamp(static_cast<double>(values[peigneMix]), 0.0, 1.0);
        const auto gain = dbToGain(std::clamp(static_cast<double>(values[peigneOutput]), -24.0, 24.0));
        const auto spread = std::clamp(static_cast<double>(values[peigneSpread]), 0.0, 1.0);
        const auto depth = std::clamp(static_cast<double>(values[peigneLfoDepth]), 0.0, 1.0) * peigneLfoOctaves;
        const auto frequency = std::clamp(static_cast<double>(values[peigneFrequency]), 20.0, 5'000.0);
        // The LFO: free, or a note of the tempo locked to the song while it plays.
        auto increment = std::clamp(static_cast<double>(values[peigneLfoRate]), 0.01, 10.0) / rate_;
        if (const auto* division = noteDivisionFor(values[peigneSync])) {
            const auto tempo = std::clamp(context.transport.beatsPerMinute, 20.0, 400.0);
            increment = tempo / 60.0 / division->beats / rate_;
            if (context.transport.state != PlaybackState::stopped) {
                const auto turns = context.transport.quarterNotePosition / division->beats;
                phase_ = turns - std::floor(turns);
            }
        }
        const auto sideCount = channels > 1 ? std::size_t { 2 } : std::size_t { 1 };
        const auto inputPeak = peakOf(input, channels, frames);
        const auto working = rest_.wake(inputPeak);
        auto wetPeak = 0.0;
        std::array<double, 2> heardHz { frequency, frequency };
        constexpr std::uint32_t chunk = 16;
        for (std::uint32_t start = 0; start < frames; start += chunk) {
            const auto count = std::min(chunk, frames - start);
            phase_ += increment * static_cast<double>(count);
            phase_ -= std::floor(phase_);
            // Where each side's line is going by the end of this chunk.
            std::array<double, 2> target {};
            for (std::size_t side = 0; side < sideCount; ++side) {
                const auto turn = phase_ + (side == 1 ? peigneLfoStereoTurn * spread : 0.0);
                const auto swing = depth * std::sin(2.0 * resonancePi * turn);
                const auto apart = 1.0 + spread * peigneSpreadRatio;
                const auto ratio = sideCount == 1 ? 1.0 : side == 0 ? 1.0 / apart : apart;
                heardHz[side] = std::clamp(frequency * ratio * std::exp2(swing), 5.0, rate_ * 0.45);
                target[side] = std::min(peigneLineOf(heardHz[side], pole, rate_), sides_[side].line.longest());
                if (sides_[side].delay < 0.0)
                    sides_[side].delay = target[side];
            }
            for (std::uint32_t at = 0; at < count; ++at) {
                const auto frame = start + at;
                const auto ramp = static_cast<double>(at + 1) / static_cast<double>(count);
                for (std::size_t side = 0; side < sideCount; ++side) {
                    const auto x = static_cast<double>(input.channel(static_cast<std::uint32_t>(side))[frame]);
                    auto wet = 0.0;
                    if (working) {
                        auto& state = sides_[side];
                        const auto delay = state.delay + (target[side] - state.delay) * ramp;
                        state.damped = (1.0 - pole) * state.line.read(delay) + pole * state.damped;
                        const auto y = x + feedback * state.damped;
                        state.line.push(mode == 0 ? y : x);
                        // A slow high-pass keeps a positive feedback's lift at
                        // the very bottom out of it.
                        const auto passed = y * norm;
                        state.dc = passed - state.dcIn + dcPole_ * state.dc;
                        state.dcIn = passed;
                        wetPeak = std::max(wetPeak, std::abs(state.dc));
                        wet = softLimit(state.dc);
                    }
                    output.channel(static_cast<std::uint32_t>(side))[frame] = static_cast<float>((x * (1.0 - mix) + wet * mix) * gain);
                }
            }
            for (std::size_t side = 0; side < sideCount; ++side)
                sides_[side].delay = target[side];
        }
        passRest(input, output, channels, frames);
        if (working && rest_.settle(inputPeak, wetPeak, frames, rate_))
            clearStates();
        readings_[0].store(static_cast<float>(heardHz[0]), std::memory_order_relaxed);
        readings_[1].store(static_cast<float>(sideCount > 1 ? heardHz[1] : heardHz[0]), std::memory_order_relaxed);
    }

private:
    struct Side {
        CubicLine line;
        double delay { -1.0 };
        double damped {};
        double dc {};
        double dcIn {};
    };

    void clearStates() noexcept
    {
        for (auto& side : sides_) {
            side.line.reset();
            side.damped = 0.0;
            side.dc = 0.0;
            side.dcIn = 0.0;
        }
    }

    double rate_ { 48'000.0 };
    std::size_t channels_ { 2 };
    std::array<Side, 2> sides_ {};
    double dcPole_ {};
    double phase_ {};
    Rest rest_;
    std::array<std::atomic<float>, peigneReadingCount> readings_ {};
};

} // namespace

std::unique_ptr<EffectProcessor> makeResonanceEffect(const EffectDescriptor& descriptor)
{
    switch (descriptor.kind) {
    case NativeDspNodeKind::resonateurs: return std::make_unique<Resonateurs>(descriptor);
    case NativeDspNodeKind::corps: return std::make_unique<Corps>(descriptor);
    case NativeDspNodeKind::peigne: return std::make_unique<Peigne>(descriptor);
    default: return {};
    }
}

} // namespace cydaw::engine
