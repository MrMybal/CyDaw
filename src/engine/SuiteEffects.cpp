#include <cydaw/engine/SuiteEffects.h>

#include <cydaw/engine/HaloReverb.h>

#include <cydaw/engine/AudioBuffer.h>
#include <cydaw/engine/EffectCatalogue.h>
#include <cydaw/engine/MixDesign.h>
#include <cydaw/engine/SuiteDesign.h>

#include "DspParts.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace cydaw::engine {

namespace {

using namespace dsp;

// The exciter's three characters, as Patine's distortion has them.
double excited(const int style, const double x) noexcept
{
    switch (style) {
    case 0: return std::tanh(x + 0.25) - std::tanh(0.25);
    case 1: return std::tanh(x);
    default: return x <= -1.0 ? -1.0 : x >= 1.0 ? 1.0 : 1.5 * x - 0.5 * x * x * x;
    }
}

// ---- Tranche ------------------------------------------------------------------------------------

class Tranche final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;

    void prepare(const ProcessSpec& spec) override
    {
        rate_ = std::max(1.0, spec.sampleRate);
        channels_ = std::min<std::size_t>(kChannels, std::max<std::uint32_t>(1, spec.channelCount));
        designed_.fill(-1.0e9f);
        reset();
    }

    void reset() noexcept override
    {
        for (auto& channel : state_) {
            for (auto& section : channel.eq)
                section.reset();
            channel.exciter.reset();
        }
        gateEnvelope_ = compEnvelope_ = fast_ = slow_ = 0.0;
        gateDb_ = 0.0;
        gateOpen_ = true;
    }

    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext&) noexcept override
    {
        using namespace suite;
        clearAudio(output);
        const auto channels = std::min({ static_cast<std::size_t>(input.channelCount), static_cast<std::size_t>(output.channelCount),
                                         channels_ });
        const auto frames = std::min(input.frameCount, output.frameCount);
        std::array<float, trancheParameterCount> values {};
        for (int index = 0; index < trancheParameterCount; ++index)
            values[static_cast<std::size_t>(index)] = value(static_cast<std::size_t>(index));
        if (values != designed_)
            design(values);
        const auto on = [&values](const int stage) { return values[static_cast<std::size_t>(stage)] >= 0.5f; };
        const auto gateOn = on(trancheGateOn);
        const auto eqOn = on(trancheEqOn);
        const auto compOn = on(trancheCompOn);
        const auto transientOn = on(trancheTransientOn);
        const auto exciterOn = on(trancheExciterOn);
        // The gate: open above its threshold, shut 4 dB under it, down by its range.
        const auto gateOpenAt = dbToGain(values[trancheGateThreshold]);
        const auto gateShutAt = dbToGain(values[trancheGateThreshold] - 4.0f);
        const auto gateFloorDb = -static_cast<double>(std::max(0.0f, values[trancheGateRange]));
        const auto gateFollow = smoothing(30.0, rate_);
        const auto gateOpening = smoothing(values[trancheGateAttack], rate_);
        const auto gateClosing = smoothing(values[trancheGateRelease], rate_);
        // The compressor.
        const auto compAttack = smoothing(values[trancheCompAttack], rate_);
        const auto compRelease = smoothing(values[trancheCompRelease], rate_);
        const auto makeup = static_cast<double>(values[trancheCompMakeup]);
        // The transients: a quick follower against a slow one.
        const auto fastAttack = smoothing(1.0, rate_);
        const auto fastRelease = smoothing(40.0, rate_);
        const auto slowAttack = smoothing(25.0, rate_);
        const auto slowRelease = smoothing(300.0, rate_);
        const auto attackAmount = static_cast<double>(std::clamp(values[trancheAttack], -1.0f, 1.0f));
        const auto sustainAmount = static_cast<double>(std::clamp(values[trancheSustain], -1.0f, 1.0f));
        const auto excite = static_cast<double>(std::clamp(values[trancheExcite], 0.0f, 1.0f));
        const auto style = choice(values[trancheExciterStyle], 3);
        const auto width = static_cast<double>(std::clamp(values[trancheWidth], 0.0f, 2.0f));
        const auto outputGain = dbToGain(values[trancheOutput]);
        const auto mix = static_cast<double>(std::clamp(values[trancheMix], 0.0f, 1.0f));
        std::array<double, kChannels> wet {};
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            auto loudest = 0.0;
            for (std::size_t channel = 0; channel < channels; ++channel) {
                wet[channel] = static_cast<double>(input.channel(static_cast<std::uint32_t>(channel))[frame]);
                loudest = std::max(loudest, std::abs(wet[channel]));
            }
            // Gate.
            if (gateOn) {
                gateEnvelope_ = std::max(loudest, gateEnvelope_ * gateFollow);
                if (gateOpen_ && gateEnvelope_ < gateShutAt)
                    gateOpen_ = false;
                else if (!gateOpen_ && gateEnvelope_ > gateOpenAt)
                    gateOpen_ = true;
                // In dB, as the ear hears it fade.
                const auto wanted = gateOpen_ ? 0.0 : gateFloorDb;
                gateDb_ = wanted + (gateOpen_ ? gateOpening : gateClosing) * (gateDb_ - wanted);
                const auto gain = dbToGain(gateDb_);
                for (std::size_t channel = 0; channel < channels; ++channel)
                    wet[channel] *= gain;
            }
            // Equaliser.
            if (eqOn)
                for (std::size_t channel = 0; channel < channels; ++channel)
                    for (auto& section : state_[channel].eq)
                        wet[channel] = section(wet[channel]);
            // Compressor, the channels linked.
            if (compOn) {
                auto level = 0.0;
                for (std::size_t channel = 0; channel < channels; ++channel)
                    level = std::max(level, std::abs(wet[channel]));
                compEnvelope_ = level + (level > compEnvelope_ ? compAttack : compRelease) * (compEnvelope_ - level);
                const auto gain = dbToGain(compressorGainDb(gainToDb(compEnvelope_), values[trancheCompThreshold], values[trancheCompRatio],
                                                            values[trancheCompKnee])
                                           + makeup);
                for (std::size_t channel = 0; channel < channels; ++channel)
                    wet[channel] *= gain;
            }
            // Transients: more or less of the attack, more or less of the tail.
            if (transientOn) {
                auto level = 0.0;
                for (std::size_t channel = 0; channel < channels; ++channel)
                    level = std::max(level, std::abs(wet[channel]));
                fast_ = level + (level > fast_ ? fastAttack : fastRelease) * (fast_ - level);
                slow_ = level + (level > slow_ ? slowAttack : slowRelease) * (slow_ - level);
                const auto difference = gainToDb(fast_ + 1.0e-9) - gainToDb(slow_ + 1.0e-9);
                const auto changeDb = std::clamp(attackAmount * std::clamp(difference, 0.0, 12.0)
                                                     + sustainAmount * std::clamp(-difference, 0.0, 12.0),
                                                 -12.0, 12.0);
                const auto gain = dbToGain(changeDb);
                for (std::size_t channel = 0; channel < channels; ++channel)
                    wet[channel] *= gain;
            }
            // Exciter: harmonics of the highs, added.
            if (exciterOn && excite > 0.0)
                for (std::size_t channel = 0; channel < channels; ++channel) {
                    const auto high = state_[channel].exciter(wet[channel]);
                    wet[channel] += excite * 0.6 * excited(style, high * 3.0) / 3.0;
                }
            // Width.
            if (channels >= 2 && width != 1.0) {
                const auto middle = 0.5 * (wet[0] + wet[1]);
                const auto side = 0.5 * (wet[0] - wet[1]) * width;
                wet[0] = middle + side;
                wet[1] = middle - side;
            }
            for (std::size_t channel = 0; channel < channels; ++channel) {
                const auto dry = static_cast<double>(input.channel(static_cast<std::uint32_t>(channel))[frame]);
                output.channel(static_cast<std::uint32_t>(channel))[frame] = static_cast<float>((dry * (1.0 - mix) + wet[channel] * mix) * outputGain);
            }
        }
        for (std::size_t channel = channels; channel < std::min<std::size_t>(input.channelCount, output.channelCount); ++channel)
            std::copy_n(input.channel(static_cast<std::uint32_t>(channel)), frames, output.channel(static_cast<std::uint32_t>(channel)));
    }

private:
    void design(const std::array<float, suite::trancheParameterCount>& values) noexcept
    {
        using namespace suite;
        const auto sections = trancheEq(values.data(), rate_);
        const auto exciter = designEqBand(EqBandType::lowCut, std::min(static_cast<double>(values[trancheExciterHz]), rate_ * 0.4), 0.0, 0.7071,
                                          0, 0, rate_);
        for (auto& channel : state_) {
            for (std::size_t index = 0; index < sections.size(); ++index)
                channel.eq[index].c = sections[index];
            channel.exciter.c = exciter;
        }
        designed_ = values;
    }

    struct ChannelState {
        std::array<Biquad, suite::trancheEqSections> eq {};
        Biquad exciter;
    };

    double rate_ { 48'000.0 };
    std::size_t channels_ { 2 };
    std::array<ChannelState, kChannels> state_ {};
    std::array<float, suite::trancheParameterCount> designed_ {};
    double gateEnvelope_ {};
    double gateDb_ {};
    bool gateOpen_ { true };
    double compEnvelope_ {};
    double fast_ {};
    double slow_ {};
};

// ---- Finition -----------------------------------------------------------------------------------

class Finition final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;

    void prepare(const ProcessSpec& spec) override
    {
        rate_ = std::max(1.0, spec.sampleRate);
        channels_ = std::min<std::size_t>(kChannels, std::max<std::uint32_t>(1, spec.channelCount));
        lookahead_ = std::max(1, static_cast<int>(std::lround(suite::finitionLookaheadMs * 0.001 * rate_)));
        for (auto& channel : state_)
            channel.delay.prepare(static_cast<std::size_t>(lookahead_) + 4);
        required_.assign(static_cast<std::size_t>(lookahead_) + 1, 1.0);
        designed_.fill(-1.0e9f);
        reset();
    }

    void reset() noexcept override
    {
        for (auto& channel : state_) {
            for (auto& section : channel.eq)
                section.reset();
            for (auto& filter : channel.split)
                filter.reset();
            channel.delay.reset();
        }
        envelopes_ = {};
        std::fill(required_.begin(), required_.end(), 1.0);
        cursor_ = 0;
        limiterGain_ = 1.0;
    }

    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return static_cast<std::uint32_t>(lookahead_); }
    [[nodiscard]] std::uint32_t maximumLatencySamples() const noexcept override { return static_cast<std::uint32_t>(lookahead_); }

    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext&) noexcept override
    {
        using namespace suite;
        clearAudio(output);
        const auto channels = std::min({ static_cast<std::size_t>(input.channelCount), static_cast<std::size_t>(output.channelCount),
                                         channels_ });
        const auto frames = std::min(input.frameCount, output.frameCount);
        std::array<float, finitionParameterCount> values {};
        for (int index = 0; index < finitionParameterCount; ++index)
            values[static_cast<std::size_t>(index)] = value(static_cast<std::size_t>(index));
        if (values != designed_)
            design(values);
        const auto on = [&values](const int stage) { return values[static_cast<std::size_t>(stage)] >= 0.5f; };
        const auto eqOn = on(finitionEqOn);
        const auto dynamicsOn = on(finitionDynamicsOn);
        const auto exciterOn = on(finitionExciterOn);
        const auto imagerOn = on(finitionImagerOn) && channels >= 2;
        const auto maximizerOn = on(finitionMaximizerOn);
        const auto split = dynamicsOn || exciterOn || imagerOn;
        const auto threshold = static_cast<double>(values[finitionThreshold]);
        const auto ratio = finitionAppliedRatio(values.data());
        const auto attack = smoothing(values[finitionAttack], rate_);
        const auto release = smoothing(values[finitionRelease], rate_);
        const auto excite = static_cast<double>(std::clamp(values[finitionExcite], 0.0f, 1.0f));
        const std::array<double, finitionBands> drive { values[finitionLowDrive], values[finitionMidDrive], values[finitionHighDrive] };
        const auto globalWidth = static_cast<double>(std::clamp(values[finitionWidth], 0.0f, 2.0f));
        const std::array<double, finitionBands> width { values[finitionLowWidth], values[finitionMidWidth], values[finitionHighWidth] };
        const auto inputGain = maximizerOn ? dbToGain(values[finitionGain]) : 1.0;
        const auto ceiling = dbToGain(std::min(0.0f, values[finitionCeiling]));
        const auto limiterRelease = smoothing(values[finitionMaximizerRelease], rate_);
        // Reaching the gain the peak needs in about the lookahead.
        const auto limiterAttack = std::exp(-3.0 / static_cast<double>(lookahead_));
        const auto lag = static_cast<double>(lookahead_);
        std::array<std::array<double, finitionBands>, kChannels> bands {};
        std::array<double, kChannels> sound {};
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            for (std::size_t channel = 0; channel < channels; ++channel) {
                auto& s = state_[channel];
                auto x = static_cast<double>(input.channel(static_cast<std::uint32_t>(channel))[frame]);
                if (eqOn)
                    for (auto& section : s.eq)
                        x = section(x);
                if (split) {
                    auto& f = s.split;
                    const auto rest = f[3](f[2](x));
                    bands[channel][0] = f[8](f[1](f[0](x)));
                    bands[channel][1] = f[5](f[4](rest));
                    bands[channel][2] = f[7](f[6](rest));
                } else {
                    sound[channel] = x;
                }
            }
            if (split) {
                // Dynamics, the channels linked band by band.
                std::array<double, finitionBands> gain { 1.0, 1.0, 1.0 };
                if (dynamicsOn)
                    for (std::size_t band = 0; band < finitionBands; ++band) {
                        auto level = 0.0;
                        for (std::size_t channel = 0; channel < channels; ++channel)
                            level = std::max(level, std::abs(bands[channel][band]));
                        auto& envelope = envelopes_[band];
                        envelope = level + (level > envelope ? attack : release) * (envelope - level);
                        gain[band] = dbToGain(compressorGainDb(gainToDb(envelope), threshold, ratio, 6.0));
                    }
                for (std::size_t channel = 0; channel < channels; ++channel)
                    for (std::size_t band = 0; band < finitionBands; ++band) {
                        auto& v = bands[channel][band];
                        v *= gain[band];
                        // Exciter: a little of the band's own harmonics.
                        if (exciterOn && excite > 0.0)
                            v += excite * std::clamp(drive[band], 0.0, 1.0) * 1.5 * (v - std::tanh(2.0 * v) * 0.5);
                    }
                // Imager: each band's width, then all of them.
                if (imagerOn)
                    for (std::size_t band = 0; band < finitionBands; ++band) {
                        const auto middle = 0.5 * (bands[0][band] + bands[1][band]);
                        const auto side = 0.5 * (bands[0][band] - bands[1][band]) * std::clamp(width[band], 0.0, 2.0) * globalWidth;
                        bands[0][band] = middle + side;
                        bands[1][band] = middle - side;
                    }
                for (std::size_t channel = 0; channel < channels; ++channel)
                    sound[channel] = bands[channel][0] + bands[channel][1] + bands[channel][2];
            }
            // Maximizer: the peaks to come seen ahead, the gain on its way
            // down before they arrive; the sound itself later by as much.
            auto loudest = 0.0;
            for (std::size_t channel = 0; channel < channels; ++channel) {
                sound[channel] *= inputGain;
                loudest = std::max(loudest, std::abs(sound[channel]));
                state_[channel].delay.push(sound[channel]);
            }
            required_[static_cast<std::size_t>(cursor_)] = maximizerOn && loudest > ceiling ? ceiling / loudest : 1.0;
            cursor_ = (cursor_ + 1) % static_cast<int>(required_.size());
            auto wanted = 1.0;
            for (const auto need : required_)
                wanted = std::min(wanted, need);
            limiterGain_ = wanted + (wanted < limiterGain_ ? limiterAttack : limiterRelease) * (limiterGain_ - wanted);
            for (std::size_t channel = 0; channel < channels; ++channel) {
                auto y = state_[channel].delay.read(lag) * (maximizerOn ? limiterGain_ : 1.0);
                // What the gain could not catch is clipped at the ceiling.
                if (maximizerOn)
                    y = std::clamp(y, -ceiling, ceiling);
                output.channel(static_cast<std::uint32_t>(channel))[frame] = static_cast<float>(y);
            }
        }
        for (std::size_t channel = channels; channel < std::min<std::size_t>(input.channelCount, output.channelCount); ++channel)
            std::copy_n(input.channel(static_cast<std::uint32_t>(channel)), frames, output.channel(static_cast<std::uint32_t>(channel)));
    }

private:
    void design(const std::array<float, suite::finitionParameterCount>& values) noexcept
    {
        using namespace suite;
        const auto sections = finitionEq(values.data(), rate_);
        const auto low = std::clamp(static_cast<double>(values[finitionCrossLow]), 30.0, rate_ * 0.1);
        const auto high = std::clamp(static_cast<double>(values[finitionCrossHigh]), low * 1.5, rate_ * 0.4);
        for (auto& channel : state_) {
            for (std::size_t index = 0; index < sections.size(); ++index)
                channel.eq[index].c = sections[index];
            auto& f = channel.split;
            f[0].c = f[1].c = mix::crossoverLowPass(low, rate_);
            f[2].c = f[3].c = mix::crossoverHighPass(low, rate_);
            f[4].c = f[5].c = mix::crossoverLowPass(high, rate_);
            f[6].c = f[7].c = mix::crossoverHighPass(high, rate_);
            f[8].c = mix::crossoverAllPass(high, rate_);
        }
        designed_ = values;
    }

    struct ChannelState {
        std::array<Biquad, suite::finitionEqSections> eq {};
        // The low split's low-pass and high-pass, the high split's, and the
        // all-pass that keeps the low band in step.
        std::array<Biquad, 9> split {};
        Delay delay;
    };

    double rate_ { 48'000.0 };
    std::size_t channels_ { 2 };
    std::array<ChannelState, kChannels> state_ {};
    std::array<float, suite::finitionParameterCount> designed_ {};
    std::array<double, suite::finitionBands> envelopes_ {};
    std::vector<double> required_;
    int cursor_ {};
    int lookahead_ { 72 };
    double limiterGain_ { 1.0 };
};

} // namespace

std::unique_ptr<EffectProcessor> makeSuiteEffect(const EffectDescriptor& descriptor)
{
    switch (descriptor.kind) {
    case NativeDspNodeKind::tranche: return std::make_unique<Tranche>(descriptor);
    case NativeDspNodeKind::finition: return std::make_unique<Finition>(descriptor);
    // Halo (HaloReverb.cpp).
    default: return makeHaloEffect(descriptor);
    }
}

} // namespace cydaw::engine
