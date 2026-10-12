#include <cydaw/engine/MixEffects.h>

#include <cydaw/engine/SuiteEffects.h>

#include <cydaw/engine/AudioBuffer.h>
#include <cydaw/engine/EffectCatalogue.h>
#include <cydaw/engine/Fft.h>
#include <cydaw/engine/MixDesign.h>

#include "DspParts.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>
#include <vector>

namespace cydaw::engine {

namespace {

using namespace dsp;

// ---- Pression -----------------------------------------------------------------------------------

class Pression final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;

    void prepare(const ProcessSpec& spec) override
    {
        rate_ = std::max(1.0, spec.sampleRate);
        channels_ = std::min<std::size_t>(kChannels, std::max<std::uint32_t>(1, spec.channelCount));
        designedLow_ = designedHigh_ = -1.0;
        reset();
    }

    void reset() noexcept override
    {
        for (auto& channel : state_)
            for (auto& filter : channel.filters)
                filter.reset();
        envelopes_ = {};
    }

    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext&) noexcept override
    {
        using namespace mix;
        clearAudio(output);
        const auto channels = std::min({ static_cast<std::size_t>(input.channelCount), static_cast<std::size_t>(output.channelCount),
                                         channels_ });
        const auto frames = std::min(input.frameCount, output.frameCount);
        std::array<float, pressionParameterCount> values {};
        for (int index = 0; index < pressionParameterCount; ++index)
            values[static_cast<std::size_t>(index)] = value(static_cast<std::size_t>(index));
        const auto low = std::clamp(static_cast<double>(values[pressionCrossLow]), 20.0, 1'000.0);
        const auto high = std::clamp(static_cast<double>(values[pressionCrossHigh]), low * 1.5, rate_ * 0.4);
        if (low != designedLow_ || high != designedHigh_)
            design(low, high);
        const auto scale = pressionTimeScale(values[pressionTime]);
        const auto depth = static_cast<double>(std::clamp(values[pressionDepth], 0.0f, 1.0f));
        const auto upward = static_cast<double>(std::clamp(values[pressionUpward], 0.0f, 1.0f));
        const auto downward = static_cast<double>(std::clamp(values[pressionDownward], 0.0f, 1.0f));
        const auto inputGain = dbToGain(values[pressionInput]);
        const auto outputGain = dbToGain(values[pressionOutput]);
        const auto solo = choice(values[pressionSolo], pressionBands + 1);
        std::array<PressionBand, pressionBands> bands {};
        std::array<double, pressionBands> makeup {};
        std::array<double, pressionBands> attack {};
        std::array<double, pressionBands> release {};
        std::array<double, pressionBands> bandGain {};
        for (int band = 0; band < pressionBands; ++band) {
            const auto b = static_cast<std::size_t>(band);
            bands[b] = pressionBand(values.data(), band);
            makeup[b] = pressionMakeupDb(bands[b], downward, upward);
            attack[b] = smoothing(values[static_cast<std::size_t>(pressionBandIndex(band, bandAttack))] * scale, rate_);
            release[b] = smoothing(values[static_cast<std::size_t>(pressionBandIndex(band, bandRelease))] * scale, rate_);
            bandGain[b] = values[static_cast<std::size_t>(pressionBandGain[b])];
        }

        std::array<std::array<double, pressionBands>, kChannels> split {};
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            // Each channel in three bands; the loudest channel leads the band.
            std::array<double, pressionBands> peak {};
            for (std::size_t channel = 0; channel < channels; ++channel) {
                auto& f = state_[channel].filters;
                const auto x = static_cast<double>(input.channel(static_cast<std::uint32_t>(channel))[frame]) * inputGain;
                const auto lowBand = f[1](f[0](x));
                const auto rest = f[3](f[2](x));
                // The low band through what the others go through (an all-pass
                // at the upper crossover): the three sum flat.
                const auto lowAligned = f[9](f[8](lowBand)) + f[11](f[10](lowBand));
                auto& bandsOf = split[channel];
                bandsOf[0] = lowAligned;
                bandsOf[1] = f[5](f[4](rest));
                bandsOf[2] = f[7](f[6](rest));
                for (std::size_t band = 0; band < pressionBands; ++band)
                    peak[band] = std::max(peak[band], std::abs(bandsOf[band]));
            }
            std::array<double, pressionBands> gain {};
            for (std::size_t band = 0; band < pressionBands; ++band) {
                auto& envelope = envelopes_[band];
                const auto coefficient = peak[band] > envelope ? attack[band] : release[band];
                envelope = peak[band] + coefficient * (envelope - peak[band]);
                const auto level = gainToDb(envelope);
                gain[band] = dbToGain(depth * (pressionGainDb(bands[band], level, downward, upward) + makeup[band]) + bandGain[band]);
            }
            for (std::size_t channel = 0; channel < channels; ++channel) {
                const auto& bandsOf = split[channel];
                auto sum = 0.0;
                for (std::size_t band = 0; band < pressionBands; ++band)
                    if (solo == 0 || solo == static_cast<int>(band) + 1)
                        sum += bandsOf[band] * gain[band];
                output.channel(static_cast<std::uint32_t>(channel))[frame] = static_cast<float>(sum * outputGain);
            }
        }
        for (std::size_t channel = channels; channel < std::min<std::size_t>(input.channelCount, output.channelCount); ++channel)
            std::copy_n(input.channel(static_cast<std::uint32_t>(channel)), frames, output.channel(static_cast<std::uint32_t>(channel)));
    }

private:
    void design(const double low, const double high) noexcept
    {
        using namespace mix;
        const auto lowPass = crossoverLowPass(low, rate_);
        const auto highPass = crossoverHighPass(low, rate_);
        const auto upperLow = crossoverLowPass(high, rate_);
        const auto upperHigh = crossoverHighPass(high, rate_);
        for (auto& channel : state_) {
            auto& f = channel.filters;
            f[0].c = f[1].c = lowPass;
            f[2].c = f[3].c = highPass;
            f[4].c = f[5].c = upperLow;
            f[6].c = f[7].c = upperHigh;
            f[8].c = f[9].c = upperLow;
            f[10].c = f[11].c = upperHigh;
        }
        designedLow_ = low;
        designedHigh_ = high;
    }

    struct ChannelState {
        // Low (0, 1), the rest (2, 3), middle (4, 5), high (6, 7), the low
        // band's all-pass (8..11).
        std::array<Biquad, 12> filters {};
    };

    double rate_ { 48'000.0 };
    std::size_t channels_ { 2 };
    std::array<ChannelState, kChannels> state_ {};
    std::array<double, mix::pressionBands> envelopes_ {};
    double designedLow_ { -1.0 };
    double designedHigh_ { -1.0 };
};

// ---- Fournaise ----------------------------------------------------------------------------------

class Fournaise final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;

    void prepare(const ProcessSpec& spec) override
    {
        rate_ = std::max(1.0, spec.sampleRate);
        channels_ = std::min<std::size_t>(kChannels, std::max<std::uint32_t>(1, spec.channelCount));
        for (auto& channel : state_) {
            for (std::size_t i = 0; i < 3; ++i) {
                channel.up[i].prepare(1 << i);
                channel.down[i].prepare(1 << i);
            }
            channel.dry.prepare(64);
        }
        designed_.fill(-1.0e9f);
        reset();
    }

    void reset() noexcept override
    {
        for (auto& channel : state_) {
            for (std::size_t i = 0; i < 3; ++i) {
                channel.up[i].reset();
                channel.down[i].reset();
            }
            channel.dry.reset();
            for (auto& filter : channel.filters)
                filter.reset();
        }
        inputPower_ = outputPower_ = 0.0;
        autoGain_ = 1.0;
    }

    [[nodiscard]] std::uint32_t latencySamples() const noexcept override
    {
        return choice(value(mix::fournaiseOversampling), 3) > 0 ? 16u : 0u;
    }
    [[nodiscard]] std::uint32_t maximumLatencySamples() const noexcept override { return 16; }

    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext&) noexcept override
    {
        using namespace mix;
        clearAudio(output);
        const auto channels = std::min({ static_cast<std::size_t>(input.channelCount), static_cast<std::size_t>(output.channelCount),
                                         channels_ });
        const auto frames = std::min(input.frameCount, output.frameCount);
        std::array<float, fournaiseParameterCount> values {};
        for (int index = 0; index < fournaiseParameterCount; ++index)
            values[static_cast<std::size_t>(index)] = value(static_cast<std::size_t>(index));
        if (values != designed_)
            design(values);
        const auto style = choice(values[fournaiseStyle], fournaiseStyleCount);
        const auto factorIndex = static_cast<std::size_t>(choice(values[fournaiseOversampling], 3));
        const auto factor = 1 << factorIndex;
        const auto drive = dbToGain(fournaiseDriveDb(style, values[fournaiseDrive], values[fournaisePunish] >= 0.5f));
        // Driven harder, it is brought down by half the drive: the level a
        // little up, the automatic level does the rest.
        const auto fixedGain = 1.0 / std::sqrt(drive);
        const auto mix = static_cast<double>(std::clamp(values[fournaiseMix], 0.0f, 1.0f));
        const auto outputGain = dbToGain(values[fournaiseOutput]);
        const auto automatic = values[fournaiseAuto] >= 0.5f;
        // The automatic level: what the last blocks measured, reached over
        // this one.
        const auto from = autoGain_;
        auto to = 1.0;
        if (automatic && inputPower_ > 1.0e-9 && outputPower_ > 1.0e-12)
            to = std::clamp(std::sqrt(inputPower_ / outputPower_), dbToGain(-36.0), dbToGain(12.0));
        else if (automatic)
            to = from;
        autoGain_ = to;
        auto blockIn = 0.0;
        auto blockOut = 0.0;
        for (std::size_t channel = 0; channel < channels; ++channel) {
            auto& s = state_[channel];
            auto& up = s.up[factorIndex];
            auto& down = s.down[factorIndex];
            const auto latency = up.latency();
            auto& f = s.filters;
            const auto* in = input.channel(static_cast<std::uint32_t>(channel));
            auto* out = output.channel(static_cast<std::uint32_t>(channel));
            for (std::uint32_t frame = 0; frame < frames; ++frame) {
                const auto x = static_cast<double>(in[frame]);
                s.dry.push(x);
                // Before: the low cut, the style's colour.
                auto v = f[2](f[1](f[0](x)));
                std::array<double, 4> fast {};
                up.up(v, fast.data());
                for (int phase = 0; phase < factor; ++phase)
                    fast[static_cast<std::size_t>(phase)] = fournaiseShape(style, fast[static_cast<std::size_t>(phase)] * drive) * fixedGain;
                auto w = down.down(fast.data());
                // After: the style's bump and top, the tone, the high cut.
                w = f[6](f[5](f[4](f[3](w))));
                if (highCutOn_) {
                    w = f[7](w);
                    if (steep_)
                        w = f[8](w);
                }
                blockIn += x * x;
                blockOut += w * w;
                const auto t = frames > 1 ? static_cast<double>(frame) / static_cast<double>(frames - 1) : 1.0;
                const auto level = automatic ? from + (to - from) * t : 1.0;
                const auto dry = s.dry.read(static_cast<double>(latency));
                out[frame] = static_cast<float>((mix * w * level + (1.0 - mix) * dry) * outputGain);
            }
        }
        // What this block measured, for the next: about 300 ms of memory, the
        // silences left out.
        if (frames > 0 && channels > 0) {
            const auto count = static_cast<double>(frames * channels);
            if (blockIn / count > 1.0e-8) {
                const auto keep = std::exp(-static_cast<double>(frames) / (0.3 * rate_));
                inputPower_ = inputPower_ * keep + blockIn / count * (1.0 - keep);
                outputPower_ = outputPower_ * keep + blockOut / count * (1.0 - keep);
            }
        }
        for (std::size_t channel = channels; channel < std::min<std::size_t>(input.channelCount, output.channelCount); ++channel)
            std::copy_n(input.channel(static_cast<std::uint32_t>(channel)), frames, output.channel(static_cast<std::uint32_t>(channel)));
    }

private:
    void design(const std::array<float, mix::fournaiseParameterCount>& values) noexcept
    {
        const auto filters = mix::fournaiseFilters(values.data(), rate_);
        for (auto& channel : state_) {
            auto& f = channel.filters;
            f[0].c = filters.lowCut[0];
            f[1].c = filters.lowCut[1];
            f[2].c = filters.colour;
            f[3].c = filters.bump;
            f[4].c = filters.top;
            f[5].c = filters.tilt[0];
            f[6].c = filters.tilt[1];
            f[7].c = filters.highCut[0];
            f[8].c = filters.highCut[1];
        }
        highCutOn_ = filters.highCutOn;
        steep_ = filters.steep;
        designed_ = values;
    }

    struct ChannelState {
        std::array<Oversampler, 3> up;
        std::array<Oversampler, 3> down;
        Delay dry;
        std::array<Biquad, 9> filters {};
    };

    double rate_ { 48'000.0 };
    std::size_t channels_ { 2 };
    std::array<ChannelState, kChannels> state_ {};
    std::array<float, mix::fournaiseParameterCount> designed_ {};
    bool highCutOn_ {};
    bool steep_ {};
    double inputPower_ {};
    double outputPower_ {};
    double autoGain_ { 1.0 };
};

// ---- Écho Bande ---------------------------------------------------------------------------------

// A spring: its sound dispersed (the highs arriving first, the chirp), round
// and round a short line, darker each time.
class Spring {
public:
    void prepare(const double rate, const double milliseconds)
    {
        line_.prepare(static_cast<std::size_t>(0.06 * rate));
        delay_ = milliseconds * 0.001 * rate;
        damp_.design(4'500.0, rate);
        low_.design(160.0, rate);
        reset();
    }
    void reset() noexcept
    {
        line_.reset();
        allPass_ = {};
        damp_.state = low_.state = 0.0;
        back_ = 0.0;
    }
    double process(const double x) noexcept
    {
        constexpr auto a = 0.62;
        auto v = low_.highPass(x) + back_ * 0.72;
        for (auto& s : allPass_) {
            const auto y = -a * v + s;
            s = v + a * y;
            v = y;
        }
        line_.push(v);
        back_ = damp_.lowPass(line_.read(delay_));
        return back_;
    }

private:
    Delay line_;
    std::array<double, 12> allPass_ {};
    OnePole damp_;
    OnePole low_;
    double delay_ { 1'500.0 };
    double back_ {};
};

class EchoBande final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;

    void prepare(const ProcessSpec& spec) override
    {
        rate_ = std::max(1.0, spec.sampleRate);
        channels_ = std::min<std::size_t>(kChannels, std::max<std::uint32_t>(1, spec.channelCount));
        tape_.prepare(static_cast<std::size_t>((mix::echoLongestSeconds * 1.02 + 0.05) * rate_));
        springs_[0].prepare(rate_, 33.0);
        springs_[1].prepare(rate_, 37.3);
        designed_.fill(-1.0e9f);
        reset();
    }

    void reset() noexcept override
    {
        tape_.reset();
        for (auto& spring : springs_)
            spring.reset();
        for (auto* filter : { &bass_, &treble_ })
            filter->reset();
        tapeTop_.state = loopLow_.state = 0.0;
        head_ = -1.0;
        wow_ = flutter_ = 0.0;
        drift_ = driftTarget_ = 0.0;
        driftCount_ = 0;
    }

    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext& context) noexcept override
    {
        using namespace mix;
        clearAudio(output);
        const auto channels = std::min({ static_cast<std::size_t>(input.channelCount), static_cast<std::size_t>(output.channelCount),
                                         channels_ });
        const auto frames = std::min(input.frameCount, output.frameCount);
        std::array<float, echoParameterCount> values {};
        for (int index = 0; index < echoParameterCount; ++index)
            values[static_cast<std::size_t>(index)] = value(static_cast<std::size_t>(index));
        if (values != designed_)
            design(values);
        const auto* division = noteDivisionFor(values[echoSync]);
        const auto target = echoHeadSeconds(values.data(), context.transport.beatsPerMinute, division != nullptr ? division->beats : 0.0);
        if (head_ < 0.0)
            head_ = target;
        const auto& mode = echoModeOf(values[echoMode]);
        const auto feedback = echoFeedback(values.data());
        const auto drive = dbToGain(values[echoDrive]);
        // Several heads heard together, each a little less: three are not
        // three times as loud.
        const auto playing = static_cast<int>(mode.heads[0]) + static_cast<int>(mode.heads[1]) + static_cast<int>(mode.heads[2]);
        const auto echoGain = static_cast<double>(values[echoLevel]) / std::sqrt(drive * std::max(1, playing));
        const auto springGain = mode.spring ? static_cast<double>(values[echoReverb]) * 0.8 : 0.0;
        const auto wow = static_cast<double>(std::clamp(values[echoWow], 0.0f, 1.0f));
        const auto age = static_cast<double>(std::clamp(values[echoAge], 0.0f, 1.0f));
        const auto hiss = age * dbToGain(-66.0);
        const auto width = static_cast<double>(std::clamp(values[echoWidth], 0.0f, 1.0f));
        const auto mix = static_cast<double>(std::clamp(values[echoMix], 0.0f, 1.0f));
        const auto dryGain = std::min(1.0, 2.0 * (1.0 - mix));
        const auto wetGain = std::min(1.0, 2.0 * mix);
        // The tape's speed follows the knob as a motor does: a glide.
        const auto glide = smoothing(120.0, rate_);
        const auto wowStep = 2.0 * kPi * 0.55 / rate_;
        const auto flutterStep = 2.0 * kPi * 6.3 / rate_;
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            auto mono = 0.0;
            for (std::size_t channel = 0; channel < channels; ++channel)
                mono += static_cast<double>(input.channel(static_cast<std::uint32_t>(channel))[frame]);
            mono /= static_cast<double>(std::max<std::size_t>(1, channels));
            head_ = target + glide * (head_ - target);
            // The tape wobbles: a slow wow, a quick flutter, a drift.
            wow_ += wowStep;
            flutter_ += flutterStep;
            if (wow_ > 2.0 * kPi)
                wow_ -= 2.0 * kPi;
            if (flutter_ > 2.0 * kPi)
                flutter_ -= 2.0 * kPi;
            if (--driftCount_ <= 0) {
                driftCount_ = static_cast<int>(0.1 * rate_);
                driftTarget_ = noise();
            }
            drift_ += (driftTarget_ - drift_) * 0.0002;
            const auto wobble = 1.0 + wow * (0.0035 * std::sin(wow_) + 0.0012 * std::sin(flutter_) + 0.002 * drift_);
            std::array<double, 3> heard {};
            auto heads = 0.0;
            for (std::size_t head = 0; head < 3; ++head)
                if (mode.heads[head]) {
                    heard[head] = tape_.read(head_ * echoHeadRatio[head] * wobble * rate_);
                    heads += heard[head];
                }
            const auto shaped = treble_(bass_(heads));
            // Back onto the tape with what comes in: its top worn, held by
            // the tape when it runs away.
            const auto written = tapeTop_.lowPass(mono * drive + loopLow_.highPass(shaped) * feedback + hiss * noise());
            tape_.push(std::tanh(written));
            // The heads apart: the second to one side, the third to the other.
            const auto apart = (heard[2] - heard[1]) * width * 0.3;
            const auto springIn = mono + shaped * 0.5;
            // The output stage rounds what runs away rather than letting it
            // climb: soft, so the echoes at their level pass as they are.
            const auto left = std::tanh((shaped - apart) * echoGain) + (springGain > 0.0 ? springs_[0].process(springIn) * springGain : 0.0);
            const auto right = std::tanh((shaped + apart) * echoGain) + (springGain > 0.0 ? springs_[1].process(springIn) * springGain : 0.0);
            for (std::size_t channel = 0; channel < channels; ++channel) {
                const auto dry = static_cast<double>(input.channel(static_cast<std::uint32_t>(channel))[frame]);
                const auto wet = channels == 1 ? 0.5 * (left + right) : channel % 2 == 0 ? left : right;
                output.channel(static_cast<std::uint32_t>(channel))[frame] = static_cast<float>(dry * dryGain + wet * wetGain);
            }
        }
        for (std::size_t channel = channels; channel < std::min<std::size_t>(input.channelCount, output.channelCount); ++channel)
            std::copy_n(input.channel(static_cast<std::uint32_t>(channel)), frames, output.channel(static_cast<std::uint32_t>(channel)));
    }

private:
    void design(const std::array<float, mix::echoParameterCount>& values) noexcept
    {
        using namespace mix;
        bass_.c = designEqBand(EqBandType::lowShelf, 150.0, values[echoBass], 0.7, 0, 0, rate_);
        treble_.c = designEqBand(EqBandType::highShelf, 3'000.0, values[echoTreble], 0.7, 0, 0, rate_);
        // A worn tape keeps less of the top.
        tapeTop_.design(14'000.0 * std::pow(0.25, std::clamp(static_cast<double>(values[echoAge]), 0.0, 1.0)), rate_);
        loopLow_.design(60.0, rate_);
        designed_ = values;
    }

    double noise() noexcept
    {
        random_ = random_ * 1'664'525u + 1'013'904'223u;
        return static_cast<double>(random_ >> 8) / static_cast<double>(1u << 24) * 2.0 - 1.0;
    }

    double rate_ { 48'000.0 };
    std::size_t channels_ { 2 };
    Delay tape_;
    std::array<Spring, 2> springs_;
    Biquad bass_;
    Biquad treble_;
    OnePole tapeTop_;
    OnePole loopLow_;
    std::array<float, mix::echoParameterCount> designed_ {};
    double head_ { -1.0 };
    double wow_ {};
    double flutter_ {};
    double drift_ {};
    double driftTarget_ {};
    int driftCount_ {};
    std::uint32_t random_ { 0x51ed270bu };
};

// ---- Multibande Pro -----------------------------------------------------------------------------

class MultibandPro final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;

    void prepare(const ProcessSpec& spec) override
    {
        rate_ = std::max(1.0, spec.sampleRate);
        channels_ = std::min<std::size_t>(kChannels, std::max<std::uint32_t>(1, spec.channelCount));
        designed_ = { -1.0, -1.0, -1.0 };
        reset();
    }

    void reset() noexcept override
    {
        for (auto& channel : state_) {
            for (auto& filter : channel.filters)
                filter.reset();
            channel.envelopes = {};
        }
    }

    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext&) noexcept override
    {
        using namespace mix;
        clearAudio(output);
        const auto channels = std::min({ static_cast<std::size_t>(input.channelCount), static_cast<std::size_t>(output.channelCount),
                                         channels_ });
        const auto frames = std::min(input.frameCount, output.frameCount);
        std::array<float, multibandParameterCount> values {};
        for (int index = 0; index < multibandParameterCount; ++index)
            values[static_cast<std::size_t>(index)] = value(static_cast<std::size_t>(index));
        // The crossovers in order, an octave's half apart at least.
        std::array<double, 3> crossovers {};
        crossovers[0] = std::clamp(static_cast<double>(values[multibandCross1]), 20.0, rate_ * 0.1);
        crossovers[1] = std::clamp(static_cast<double>(values[multibandCross2]), crossovers[0] * 1.41, rate_ * 0.2);
        crossovers[2] = std::clamp(static_cast<double>(values[multibandCross3]), crossovers[1] * 1.41, rate_ * 0.4);
        if (crossovers != designed_)
            design(crossovers);
        const auto inputGain = dbToGain(values[multibandInput]);
        const auto outputGain = dbToGain(values[multibandOutput]);
        const auto amount = static_cast<double>(std::clamp(values[multibandAmount], 0.0f, 1.0f));
        const auto link = static_cast<double>(std::clamp(values[multibandLink], 0.0f, 1.0f));
        std::array<double, multibandBands> attack {};
        std::array<double, multibandBands> release {};
        std::array<double, multibandBands> makeup {};
        std::array<bool, multibandBands> on {};
        std::array<bool, multibandBands> heard {};
        auto anySolo = false;
        for (int band = 0; band < multibandBands; ++band) {
            const auto b = static_cast<std::size_t>(band);
            const auto at = [&](const int field) { return values[static_cast<std::size_t>(multibandIndex(band, field))]; };
            attack[b] = smoothing(at(multibandAttack), rate_);
            release[b] = smoothing(at(multibandRelease), rate_);
            makeup[b] = at(multibandMakeup);
            on[b] = at(multibandOn) >= 0.5f;
            heard[b] = at(multibandSolo) >= 0.5f;
            anySolo = anySolo || heard[b];
        }
        std::array<std::array<double, multibandBands>, kChannels> split {};
        std::array<std::array<double, multibandBands>, kChannels> peaks {};
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            std::array<double, multibandBands> loudest {};
            for (std::size_t channel = 0; channel < channels; ++channel) {
                auto& f = state_[channel].filters;
                const auto x = static_cast<double>(input.channel(static_cast<std::uint32_t>(channel))[frame]) * inputGain;
                auto& bands = split[channel];
                // Four bands; the lower ones through the all-passes of the
                // crossovers above them, so the four sum flat.
                const auto rest1 = f[3](f[2](x));
                const auto rest2 = f[7](f[6](rest1));
                bands[0] = f[13](f[12](f[1](f[0](x))));
                bands[1] = f[14](f[5](f[4](rest1)));
                bands[2] = f[9](f[8](rest2));
                bands[3] = f[11](f[10](rest2));
                for (std::size_t b = 0; b < multibandBands; ++b) {
                    peaks[channel][b] = std::abs(bands[b]);
                    loudest[b] = std::max(loudest[b], peaks[channel][b]);
                }
            }
            for (std::size_t channel = 0; channel < channels; ++channel) {
                auto& s = state_[channel];
                auto sum = 0.0;
                for (std::size_t b = 0; b < multibandBands; ++b) {
                    const auto heardHere = anySolo ? heard[b] : true;
                    const auto detected = link * loudest[b] + (1.0 - link) * peaks[channel][b];
                    auto& envelope = s.envelopes[b];
                    const auto coefficient = detected > envelope ? attack[b] : release[b];
                    envelope = detected + coefficient * (envelope - detected);
                    if (!heardHere)
                        continue;
                    const auto change = on[b] ? amount * multibandGainDb(values.data(), static_cast<int>(b), gainToDb(envelope)) : 0.0;
                    sum += split[channel][b] * dbToGain(change + makeup[b]);
                }
                output.channel(static_cast<std::uint32_t>(channel))[frame] = static_cast<float>(sum * outputGain);
            }
        }
        for (std::size_t channel = channels; channel < std::min<std::size_t>(input.channelCount, output.channelCount); ++channel)
            std::copy_n(input.channel(static_cast<std::uint32_t>(channel)), frames, output.channel(static_cast<std::uint32_t>(channel)));
    }

private:
    void design(const std::array<double, 3>& crossovers) noexcept
    {
        using namespace mix;
        for (auto& channel : state_) {
            auto& f = channel.filters;
            for (std::size_t c = 0; c < 3; ++c) {
                f[c * 4].c = f[c * 4 + 1].c = crossoverLowPass(crossovers[c], rate_);
                f[c * 4 + 2].c = f[c * 4 + 3].c = crossoverHighPass(crossovers[c], rate_);
            }
            f[12].c = crossoverAllPass(crossovers[1], rate_);
            f[13].c = crossoverAllPass(crossovers[2], rate_);
            f[14].c = crossoverAllPass(crossovers[2], rate_);
        }
        designed_ = crossovers;
    }

    struct ChannelState {
        // Each crossover's low-pass (two sections) and high-pass (two), then
        // the all-passes that keep the lower bands in step.
        std::array<Biquad, 15> filters {};
        std::array<double, mix::multibandBands> envelopes {};
    };

    double rate_ { 48'000.0 };
    std::size_t channels_ { 2 };
    std::array<ChannelState, kChannels> state_ {};
    std::array<double, 3> designed_ {};
};

// ---- Velours ------------------------------------------------------------------------------------

// Short frames of the sound, their spectra calmed where a resonance stands
// out, put back together (a quarter frame apart, root-Hann windows both ways).
class Velours final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;

    void prepare(const ProcessSpec& spec) override
    {
        using namespace mix;
        rate_ = std::max(1.0, spec.sampleRate);
        channels_ = std::min<std::size_t>(kChannels, std::max<std::uint32_t>(1, spec.channelCount));
        fft_.prepare(veloursFrame);
        window_.resize(veloursFrame);
        auto sum = 0.0;
        for (int n = 0; n < veloursFrame; ++n) {
            window_[static_cast<std::size_t>(n)] = static_cast<float>(std::sqrt(0.5 - 0.5 * std::cos(2.0 * kPi * n / veloursFrame)));
            sum += window_[static_cast<std::size_t>(n)];
        }
        // A sine at full scale reads 0 dB.
        magnitudeScale_ = 2.0 / sum;
        for (auto& channel : state_) {
            channel.in.assign(veloursFrame, 0.0f);
            channel.out.assign(veloursFrame, 0.0f);
            channel.accumulated.assign(veloursFrame * 2, 0.0f);
            channel.spectrum.assign(veloursFrame, {});
            channel.magnitude.assign(veloursBins, 0.0f);
            channel.power.assign(veloursBins, 0.0f);
            channel.target.assign(veloursBins, 0.0f);
            channel.reduction.assign(veloursBins, 0.0f);
            channel.dry.prepare(veloursFrame + 8);
        }
        detected_.assign(veloursBins, 0.0f);
        scratch_.assign(veloursBins + 1, 0.0);
        reset();
    }

    void reset() noexcept override
    {
        for (auto& channel : state_) {
            std::fill(channel.in.begin(), channel.in.end(), 0.0f);
            std::fill(channel.out.begin(), channel.out.end(), 0.0f);
            std::fill(channel.accumulated.begin(), channel.accumulated.end(), 0.0f);
            std::fill(channel.reduction.begin(), channel.reduction.end(), 0.0f);
            std::fill(channel.power.begin(), channel.power.end(), 0.0f);
            channel.dry.reset();
        }
        rover_ = fifoStart;
    }

    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return static_cast<std::uint32_t>(latency()); }
    [[nodiscard]] std::uint32_t maximumLatencySamples() const noexcept override { return static_cast<std::uint32_t>(latency()); }

    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext&) noexcept override
    {
        using namespace mix;
        clearAudio(output);
        const auto channels = std::min({ static_cast<std::size_t>(input.channelCount), static_cast<std::size_t>(output.channelCount),
                                         channels_ });
        const auto frames = std::min(input.frameCount, output.frameCount);
        for (int index = 0; index < veloursParameterCount; ++index)
            values_[static_cast<std::size_t>(index)] = value(static_cast<std::size_t>(index));
        const auto mix = static_cast<double>(std::clamp(values_[veloursMix], 0.0f, 1.0f));
        const auto delta = values_[veloursDelta] >= 0.5f;
        const auto outputGain = dbToGain(values_[veloursOutput]);
        const auto lag = latency();
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            for (std::size_t channel = 0; channel < channels; ++channel) {
                auto& s = state_[channel];
                const auto x = input.channel(static_cast<std::uint32_t>(channel))[frame];
                s.in[static_cast<std::size_t>(rover_)] = x;
                s.dry.push(static_cast<double>(x));
                const auto wet = static_cast<double>(s.out[static_cast<std::size_t>(rover_ - fifoStart)]);
                const auto dry = s.dry.read(static_cast<double>(lag));
                const auto heard = delta ? dry - wet : dry * (1.0 - mix) + wet * mix;
                output.channel(static_cast<std::uint32_t>(channel))[frame] = static_cast<float>(heard * outputGain);
            }
            if (++rover_ >= veloursFrame) {
                rover_ = fifoStart;
                transform(channels);
            }
        }
        for (std::size_t channel = channels; channel < std::min<std::size_t>(input.channelCount, output.channelCount); ++channel)
            std::copy_n(input.channel(static_cast<std::uint32_t>(channel)), frames, output.channel(static_cast<std::uint32_t>(channel)));
    }

private:
    // Where a frame's newest quarter goes: after the three before it.
    static constexpr int fifoStart = mix::veloursFrame - mix::veloursHop;
    [[nodiscard]] static constexpr int latency() noexcept { return mix::veloursLatency; }

    void transform(const std::size_t channels) noexcept
    {
        using namespace mix;
        constexpr auto size = static_cast<std::size_t>(veloursFrame);
        constexpr auto hop = static_cast<std::size_t>(veloursHop);
        // Each channel's spectrum and its level per bin, averaged over the
        // last frames.
        const auto keep = static_cast<float>(std::exp(-static_cast<double>(hop) / (veloursListenMs * 0.001 * rate_)));
        for (std::size_t channel = 0; channel < channels; ++channel) {
            auto& s = state_[channel];
            for (std::size_t n = 0; n < size; ++n)
                s.spectrum[n] = { s.in[n] * window_[n], 0.0f };
            fft_.forward(s.spectrum.data());
            for (std::size_t k = 0; k < static_cast<std::size_t>(veloursBins); ++k) {
                const auto power = std::norm(s.spectrum[k]) * static_cast<float>(magnitudeScale_ * magnitudeScale_);
                s.power[k] = power + keep * (s.power[k] - power);
                s.magnitude[k] = static_cast<float>(10.0 * std::log10(std::max(1.0e-24, static_cast<double>(s.power[k]))));
            }
        }
        const auto link = static_cast<double>(std::clamp(values_[veloursLink], 0.0f, 1.0f));
        const auto framesPerSecond = rate_ / static_cast<double>(hop);
        const auto attack = std::exp(-1.0 / (std::max(0.1, static_cast<double>(values_[veloursAttack])) * 0.001 * framesPerSecond));
        const auto release = std::exp(-1.0 / (std::max(0.1, static_cast<double>(values_[veloursRelease])) * 0.001 * framesPerSecond));
        const auto binHz = rate_ / static_cast<double>(size);
        for (std::size_t channel = 0; channel < channels; ++channel) {
            auto& s = state_[channel];
            // What it hears: its own spectrum, or the loudest channel's as
            // linked.
            for (std::size_t k = 0; k < static_cast<std::size_t>(veloursBins); ++k) {
                auto loudest = s.magnitude[k];
                for (std::size_t other = 0; other < channels; ++other)
                    loudest = std::max(loudest, state_[other].magnitude[k]);
                detected_[k] = static_cast<float>(link * loudest + (1.0 - link) * s.magnitude[k]);
            }
            veloursReduction(detected_.data(), veloursBins, binHz, values_.data(), scratch_.data(), s.target.data());
            for (std::size_t k = 0; k < static_cast<std::size_t>(veloursBins); ++k) {
                const auto wanted = static_cast<double>(s.target[k]);
                auto& now = s.reduction[k];
                const auto coefficient = wanted > now ? attack : release;
                now = static_cast<float>(wanted + coefficient * (static_cast<double>(now) - wanted));
                const auto gain = static_cast<float>(dbToGain(-static_cast<double>(now)));
                s.spectrum[k] *= gain;
                if (k > 0 && k < size / 2)
                    s.spectrum[size - k] *= gain;
            }
            fft_.inverse(s.spectrum.data());
            // Root-Hann both ways, a quarter apart: they add up to 2.
            for (std::size_t n = 0; n < size; ++n)
                s.accumulated[n] += s.spectrum[n].real() * window_[n] * 0.5f;
            std::copy_n(s.accumulated.begin(), hop, s.out.begin());
            std::copy(s.accumulated.begin() + static_cast<std::ptrdiff_t>(hop), s.accumulated.end(), s.accumulated.begin());
            std::fill(s.accumulated.end() - static_cast<std::ptrdiff_t>(hop), s.accumulated.end(), 0.0f);
            std::copy(s.in.begin() + static_cast<std::ptrdiff_t>(hop), s.in.end(), s.in.begin());
        }
    }

    struct ChannelState {
        std::vector<float> in;
        std::vector<float> out;
        std::vector<float> accumulated;
        std::vector<std::complex<float>> spectrum;
        std::vector<float> magnitude;
        std::vector<float> power;
        std::vector<float> target;
        std::vector<float> reduction;
        Delay dry;
    };

    double rate_ { 48'000.0 };
    std::size_t channels_ { 2 };
    Fft fft_;
    std::vector<float> window_;
    double magnitudeScale_ { 1.0 };
    std::array<ChannelState, kChannels> state_ {};
    std::vector<float> detected_;
    std::vector<double> scratch_;
    std::array<float, mix::veloursParameterCount> values_ {};
    int rover_ {};
};

// ---- Patine -------------------------------------------------------------------------------------

class Patine final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;

    void prepare(const ProcessSpec& spec) override
    {
        rate_ = std::max(1.0, spec.sampleRate);
        channels_ = std::min<std::size_t>(kChannels, std::max<std::uint32_t>(1, spec.channelCount));
        for (std::size_t index = 0; index < state_.size(); ++index) {
            auto& channel = state_[index];
            channel.wobble.prepare(static_cast<std::size_t>(0.02 * rate_));
            channel.dry.prepare(static_cast<std::size_t>(0.02 * rate_));
            for (auto& line : channel.room)
                line.prepare(static_cast<std::size_t>(0.2 * rate_));
            channel.hissLow.design(400.0, rate_);
            channel.hissHigh.design(8'000.0, rate_);
            channel.crackleTone.design(1'800.0, rate_);
            channel.rumble.design(40.0, rate_);
            channel.digitalTop.design(14'000.0, rate_);
            for (auto& damp : channel.damping)
                damp.design(5'000.0, rate_);
            channel.seed = 0x9e3779b9u * static_cast<std::uint32_t>(index + 1);
        }
        designed_.fill(-1.0e9f);
        reset();
    }

    void reset() noexcept override
    {
        for (auto& channel : state_) {
            channel.wobble.reset();
            channel.dry.reset();
            for (auto& line : channel.room)
                line.reset();
            channel.hissLow.state = channel.hissHigh.state = channel.crackleTone.state = channel.rumble.state = 0.0;
            channel.magneticTop.state = channel.digitalTop.state = 0.0;
            for (auto& damp : channel.damping)
                damp.state = 0.0;
            channel.tilt[0].reset();
            channel.tilt[1].reset();
            channel.held = 0.0;
            channel.holdCount = 0.0;
            channel.crackle = 0.0;
        }
        wow_ = flutter_ = hum_ = 0.0;
        drift_ = driftTarget_ = 0.0;
        driftCount_ = 0;
        follower_ = 0.0;
        dropGain_ = 1.0;
        dropTarget_ = 1.0;
        dropLeft_ = 0;
    }

    [[nodiscard]] std::uint32_t latencySamples() const noexcept override
    {
        return static_cast<std::uint32_t>(std::lround(mix::patineWobbleCentreMs * 0.001 * rate_));
    }
    [[nodiscard]] std::uint32_t maximumLatencySamples() const noexcept override { return latencySamples(); }

    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext&) noexcept override
    {
        using namespace mix;
        clearAudio(output);
        const auto channels = std::min({ static_cast<std::size_t>(input.channelCount), static_cast<std::size_t>(output.channelCount),
                                         channels_ });
        const auto frames = std::min(input.frameCount, output.frameCount);
        std::array<float, patineParameterCount> values {};
        for (int index = 0; index < patineParameterCount; ++index)
            values[static_cast<std::size_t>(index)] = value(static_cast<std::size_t>(index));
        if (values != designed_)
            design(values);
        std::array<double, patineModules> amount {};
        for (int module = 0; module < patineModules; ++module)
            amount[static_cast<std::size_t>(module)] = patineAmount(values.data(), module);
        const auto noiseType = choice(values[patineNoiseType], 3);
        const auto noiseGain = amount[0] > 0.0 ? dbToGain(-66.0 + 46.0 * amount[0]) : 0.0;
        const auto follow = values[patineNoiseFollow] >= 0.5f;
        const auto rateKnob = static_cast<double>(std::clamp(values[patineWobbleRate], 0.0f, 1.0f));
        const auto wowStep = 2.0 * kPi * (0.4 + 1.2 * rateKnob) / rate_;
        const auto flutterStep = 2.0 * kPi * (5.0 + 7.0 * rateKnob) / rate_;
        const auto wobbleSamples = 2.5 * amount[1] * 0.001 * rate_;
        const auto centre = patineWobbleCentreMs * 0.001 * rate_;
        const auto distortType = choice(values[patineDistortType], 3);
        const auto drive = dbToGain(30.0 * amount[2]);
        const auto bits = 16.0 - 12.0 * amount[3];
        const auto step = std::pow(2.0, 1.0 - bits);
        const auto hold = 1.0 + 15.0 * amount[3] * amount[3];
        const auto roomWet = amount[4] * 0.6;
        const auto size = static_cast<double>(std::clamp(values[patineSpaceSize], 0.0f, 1.0f));
        const auto roomFeedback = 0.45 + 0.35 * size;
        const auto magnetic = amount[5];
        const auto mix = static_cast<double>(std::clamp(values[patineMix], 0.0f, 1.0f));
        const auto width = static_cast<double>(std::clamp(values[patineWidth], 0.0f, 1.5f));
        const auto outputGain = dbToGain(values[patineOutput]);
        const auto followCoefficient = smoothing(80.0, rate_);
        const auto dropSmoothing = smoothing(6.0, rate_);
        const auto crackleChance = (1.0 + 20.0 * amount[0]) / rate_;
        const auto dropChance = (0.3 + 2.0 * magnetic) / rate_;
        const auto lag = static_cast<double>(latencySamples());
        std::array<double, kChannels> wet {};
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            // What moves every channel together: the tape's sway, the hum,
            // the crackles' moments, the dropouts, how loud it plays.
            wow_ += wowStep;
            flutter_ += flutterStep;
            hum_ += 2.0 * kPi * 50.0 / rate_;
            for (auto* phase : { &wow_, &flutter_, &hum_ })
                if (*phase > 2.0 * kPi)
                    *phase -= 2.0 * kPi;
            if (--driftCount_ <= 0) {
                driftCount_ = static_cast<int>(0.15 * rate_);
                driftTarget_ = noise(seed_);
            }
            drift_ += (driftTarget_ - drift_) * 0.0003;
            const auto sway = (1.0 - rateKnob) * std::sin(wow_) + rateKnob * 0.6 * std::sin(flutter_) + 0.4 * drift_;
            auto loudest = 0.0;
            for (std::size_t channel = 0; channel < channels; ++channel)
                loudest = std::max(loudest, std::abs(static_cast<double>(input.channel(static_cast<std::uint32_t>(channel))[frame])));
            follower_ = std::max(loudest, follower_ * followCoefficient);
            const auto crackleNow = noiseType == 0 && noiseGain > 0.0 && unit(seed_) < crackleChance;
            const auto crackleSize = crackleNow ? noiseGain * 10.0 * std::pow(unit(seed_), 3.0) * (unit(seed_) < 0.5 ? -1.0 : 1.0) : 0.0;
            if (magnetic > 0.0) {
                if (dropLeft_ > 0) {
                    if (--dropLeft_ == 0)
                        dropTarget_ = 1.0;
                } else if (unit(seed_) < dropChance) {
                    dropLeft_ = static_cast<int>((0.03 + 0.12 * unit(seed_)) * rate_);
                    dropTarget_ = 1.0 - magnetic * (0.3 + 0.6 * unit(seed_));
                }
            } else {
                dropTarget_ = 1.0;
            }
            dropGain_ = dropTarget_ + dropSmoothing * (dropGain_ - dropTarget_);
            const auto humNow = 0.5 * (std::sin(hum_) + 0.5 * std::sin(2.0 * hum_) + 0.3 * std::sin(3.0 * hum_) + 0.2 * std::sin(5.0 * hum_));
            for (std::size_t channel = 0; channel < channels; ++channel) {
                auto& s = state_[channel];
                const auto x = static_cast<double>(input.channel(static_cast<std::uint32_t>(channel))[frame]);
                s.dry.push(x);
                auto v = x;
                // Noise.
                if (noiseGain > 0.0) {
                    const auto white = noise(s.seed);
                    auto n = 0.0;
                    if (noiseType == 0) {
                        // A record: its surface's hiss, its rumble, its crackles.
                        s.crackle = s.crackle * 0.55 + (channel == 0 || unit(s.seed) < 0.7 ? crackleSize : 0.0);
                        n = 0.25 * s.hissHigh.lowPass(s.hissLow.highPass(white)) + 0.6 * s.rumble.lowPass(white) * 4.0
                            + s.crackleTone.highPass(s.crackle);
                    } else if (noiseType == 1) {
                        n = 0.7 * s.hissHigh.lowPass(s.hissLow.highPass(white));
                    } else {
                        n = humNow * 0.8 + 0.05 * white;
                    }
                    v += n * noiseGain * (follow ? std::min(1.0, follower_ * 4.0) : 1.0);
                }
                // Wobble: read a little behind, the place swaying.
                s.wobble.push(v);
                v = s.wobble.read(centre + wobbleSamples * sway);
                // Distortion, its level brought back.
                if (amount[2] > 0.0) {
                    const auto u = v * drive;
                    auto shaped = 0.0;
                    if (distortType == 0)
                        shaped = std::tanh(u + 0.25) - std::tanh(0.25);
                    else if (distortType == 1)
                        shaped = std::tanh(u);
                    else
                        shaped = u <= -1.0 ? -1.0 : u >= 1.0 ? 1.0 : 1.5 * u - 0.5 * u * u * u;
                    v = shaped / std::sqrt(drive);
                }
                // Digital: fewer samples, fewer steps.
                if (amount[3] > 0.0) {
                    s.holdCount += 1.0;
                    if (s.holdCount >= hold) {
                        s.holdCount -= hold;
                        s.held = std::round(v / step) * step;
                    }
                    v = s.digitalTop.lowPass(s.held) * 0.3 + s.held * 0.7;
                }
                // A small room.
                if (roomWet > 0.0) {
                    std::array<double, 4> taps {};
                    auto sum = 0.0;
                    for (std::size_t line = 0; line < 4; ++line) {
                        taps[line] = s.room[line].read(roomDelay_[channel % 2][line]);
                        sum += taps[line];
                    }
                    for (std::size_t line = 0; line < 4; ++line) {
                        // A Householder mix: each line hears the others.
                        const auto mixed = taps[line] - 0.5 * sum;
                        s.room[line].push(v * 0.35 + s.damping[line].lowPass(mixed) * roomFeedback);
                    }
                    v += roomWet * 0.5 * sum;
                }
                // Magnetic: the tape's softness, its top worn, its dropouts.
                if (magnetic > 0.0) {
                    const auto push = 1.0 + 2.0 * magnetic;
                    v = std::tanh(v * push) / push;
                    v = s.magneticTop.lowPass(v) * dropGain_ * std::sqrt(push);
                }
                v = s.tilt[1](s.tilt[0](v));
                wet[channel] = v;
            }
            if (channels >= 2 && width != 1.0) {
                const auto middle = 0.5 * (wet[0] + wet[1]);
                const auto side = 0.5 * (wet[0] - wet[1]) * width;
                wet[0] = middle + side;
                wet[1] = middle - side;
            }
            for (std::size_t channel = 0; channel < channels; ++channel) {
                const auto dry = state_[channel].dry.read(lag);
                output.channel(static_cast<std::uint32_t>(channel))[frame]
                    = static_cast<float>((dry * (1.0 - mix) + wet[channel] * mix) * outputGain);
            }
        }
        for (std::size_t channel = channels; channel < std::min<std::size_t>(input.channelCount, output.channelCount); ++channel)
            std::copy_n(input.channel(static_cast<std::uint32_t>(channel)), frames, output.channel(static_cast<std::uint32_t>(channel)));
    }

private:
    void design(const std::array<float, mix::patineParameterCount>& values) noexcept
    {
        using namespace mix;
        const auto tone = std::clamp(static_cast<double>(values[patineTone]), -1.0, 1.0) * 6.0;
        const auto magnetic = patineAmount(values.data(), 5);
        for (auto& channel : state_) {
            channel.tilt[0].c = designEqBand(EqBandType::tilt, 800.0, tone, 0.5, 0, 0, rate_);
            channel.tilt[1].c = designEqBand(EqBandType::tilt, 800.0, tone, 0.5, 0, 1, rate_);
            channel.magneticTop.design(18'000.0 * std::pow(7.0 / 18.0, magnetic), rate_);
        }
        // The room's lines: longer as it grows, the right side's apart.
        const auto size = std::clamp(static_cast<double>(values[patineSpaceSize]), 0.0, 1.0);
        constexpr std::array<double, 4> milliseconds { 17.3, 23.9, 31.1, 37.7 };
        for (std::size_t line = 0; line < 4; ++line) {
            roomDelay_[0][line] = milliseconds[line] * (0.6 + 1.6 * size) * 0.001 * rate_;
            roomDelay_[1][line] = roomDelay_[0][line] * 1.07;
        }
        designed_ = values;
    }

    static double unit(std::uint32_t& seed) noexcept
    {
        seed = seed * 1'664'525u + 1'013'904'223u;
        return static_cast<double>(seed >> 8) / static_cast<double>(1u << 24);
    }
    static double noise(std::uint32_t& seed) noexcept { return unit(seed) * 2.0 - 1.0; }

    struct ChannelState {
        Delay wobble;
        Delay dry;
        std::array<Delay, 4> room;
        std::array<OnePole, 4> damping {};
        OnePole hissLow;
        OnePole hissHigh;
        OnePole crackleTone;
        OnePole rumble;
        OnePole magneticTop;
        OnePole digitalTop;
        std::array<Biquad, 2> tilt {};
        double held {};
        double holdCount {};
        double crackle {};
        std::uint32_t seed { 1u };
    };

    double rate_ { 48'000.0 };
    std::size_t channels_ { 2 };
    std::array<ChannelState, kChannels> state_ {};
    std::array<float, mix::patineParameterCount> designed_ {};
    std::array<std::array<double, 4>, 2> roomDelay_ {};
    double wow_ {};
    double flutter_ {};
    double hum_ {};
    double drift_ {};
    double driftTarget_ {};
    int driftCount_ {};
    double follower_ {};
    double dropGain_ { 1.0 };
    double dropTarget_ { 1.0 };
    int dropLeft_ {};
    std::uint32_t seed_ { 0x2f6b9a15u };
};

// ---- Sonomètre ----------------------------------------------------------------------------------

// The sound passes as it is: the display measures it (its tap).
class Sonometre final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;
    void prepare(const ProcessSpec&) override {}
    void reset() noexcept override {}
    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext&) noexcept override
    {
        const auto frames = std::min(input.frameCount, output.frameCount);
        for (std::uint32_t channel = 0; channel < output.channelCount; ++channel) {
            if (channel < input.channelCount)
                std::copy_n(input.channel(channel), frames, output.channel(channel));
            else
                std::fill_n(output.channel(channel), frames, 0.0f);
        }
    }
};

} // namespace

std::unique_ptr<EffectProcessor> makeMixEffect(const EffectDescriptor& descriptor)
{
    switch (descriptor.kind) {
    case NativeDspNodeKind::pression: return std::make_unique<Pression>(descriptor);
    case NativeDspNodeKind::fournaise: return std::make_unique<Fournaise>(descriptor);
    case NativeDspNodeKind::echoBande: return std::make_unique<EchoBande>(descriptor);
    case NativeDspNodeKind::multibandPro: return std::make_unique<MultibandPro>(descriptor);
    case NativeDspNodeKind::velours: return std::make_unique<Velours>(descriptor);
    case NativeDspNodeKind::patine: return std::make_unique<Patine>(descriptor);
    case NativeDspNodeKind::sonometre: return std::make_unique<Sonometre>(descriptor);
    // The suites (SuiteEffects.cpp).
    default: return makeSuiteEffect(descriptor);
    }
}

} // namespace cydaw::engine
