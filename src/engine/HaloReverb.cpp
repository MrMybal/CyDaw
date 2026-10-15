#include <cydaw/engine/HaloReverb.h>
#include <cydaw/engine/AddedEffects.h>

#include <cydaw/engine/AudioBuffer.h>
#include <cydaw/engine/EffectCatalogue.h>
#include <cydaw/engine/HaloDesign.h>

#include "DspParts.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace cydaw::engine {

namespace {

using namespace dsp;

// A Schroeder all-pass on a line of its own: it smears without colouring.
struct AllPass {
    Delay line;
    double samples { 1.0 };
    void prepare(const std::size_t longest) { line.prepare(longest + 4); }
    void reset() noexcept { line.reset(); }
    double process(const double x, const double gain) noexcept
    {
        const auto delayed = line.read(std::max(0.0, samples - 1.0));
        const auto v = x + gain * delayed;
        line.push(v);
        return delayed - gain * v;
    }
};

class HaloReverb final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;

    void prepare(const ProcessSpec& spec) override
    {
        using namespace halo;
        rate_ = std::max(1.0, spec.sampleRate);
        channels_ = std::min<std::size_t>(kChannels, std::max<std::uint32_t>(1, spec.channelCount));
        // The largest the lines may grow: the largest mode at the largest size.
        constexpr auto largestScale = 1.6 * 1.65;
        predelay_.prepare(static_cast<std::size_t>(1.05 * rate_));
        early_.prepare(static_cast<std::size_t>(0.05 * largestScale * rate_ + 64.0));
        for (auto& diffuser : diffusers_)
            diffuser.prepare(static_cast<std::size_t>(0.015 * largestScale * rate_));
        for (auto& smear : smears_)
            smear.prepare(static_cast<std::size_t>(0.06 * largestScale * rate_));
        for (int line = 0; line < haloLines; ++line) {
            lines_[static_cast<std::size_t>(line)].prepare(
                static_cast<std::size_t>(haloLineMs[static_cast<std::size_t>(line)] * 0.001 * largestScale * 1.02 * rate_ + 64.0));
            inside_[static_cast<std::size_t>(line)].prepare(static_cast<std::size_t>(0.008 * largestScale * rate_));
        }
        designed_.fill(-1.0e9f);
        reset();
    }

    void reset() noexcept override
    {
        predelay_.reset();
        early_.reset();
        for (auto& diffuser : diffusers_)
            diffuser.reset();
        for (auto& smear : smears_)
            smear.reset();
        for (auto& line : lines_)
            line.reset();
        for (auto& diffuser : inside_)
            diffuser.reset();
        for (auto& pole : damping_)
            pole.state = 0.0;
        for (auto& pole : bass_)
            pole.state = 0.0;
        for (auto& channel : filters_)
            for (auto& filter : channel)
                filter.reset();
        phases_ = {};
        drift_ = {};
        driftTarget_ = {};
        driftCount_ = 0;
        gateTimer_ = 0.0;
        gateGain_ = 0.0;
        follower_ = 0.0;
        for (std::size_t line = 0; line < lengths_.size(); ++line)
            lengths_[line] = -1.0;
    }

    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext& context) noexcept override
    {
        using namespace halo;
        clearAudio(output);
        const auto channels = std::min({ static_cast<std::size_t>(input.channelCount), static_cast<std::size_t>(output.channelCount),
                                         channels_ });
        const auto frames = std::min(input.frameCount, output.frameCount);
        std::array<float, haloParameterCount> values {};
        for (int index = 0; index < haloParameterCount; ++index)
            values[static_cast<std::size_t>(index)] = value(static_cast<std::size_t>(index));
        if (values != designed_)
            design(values);
        const auto& mode = haloModeOf(values[haloMode]);
        const auto& colour = haloColourOf(values[haloColour]);
        const auto scale = haloScale(values.data());
        // The predelay, free or a note of the tempo.
        auto predelay = static_cast<double>(values[haloPredelay]) * 0.001;
        if (const auto* division = noteDivisionFor(values[haloSync]))
            predelay = division->beats * 60.0 / std::max(20.0, context.transport.beatsPerMinute);
        const auto predelaySamples = std::clamp(predelay, 0.0, 1.0) * rate_;
        const auto earlyDiffusion = 0.75 * std::clamp(static_cast<double>(values[haloEarlyDiffusion]), 0.0, 1.0);
        const auto lateDiffusion = 0.5 * std::clamp(static_cast<double>(values[haloLateDiffusion]), 0.0, 1.0);
        const auto buildUp = std::clamp(mode.buildUp * (0.3 + static_cast<double>(values[haloAttack])), 0.0, 1.0);
        const auto earlyLevel = mode.early;
        const auto sway = static_cast<double>(std::clamp(values[haloModDepth], 0.0f, 1.0f)) * mode.sway * colour.extraSway;
        const auto rateHz = std::clamp(static_cast<double>(values[haloModRate]), 0.01, 10.0);
        const auto drive = colour.drive;
        const auto quantum = colour.bits > 0.0 ? std::pow(2.0, 1.0 - colour.bits) : 0.0;
        const auto mix = static_cast<double>(std::clamp(values[haloMix], 0.0f, 1.0f));
        const auto width = static_cast<double>(std::clamp(values[haloWidth], 0.0f, 1.0f));
        // Gated: a tail held while the sound lasts and a while after.
        const auto gateHold = std::max(0.05, static_cast<double>(values[haloDecay]) * 0.6);
        const auto gateOpening = smoothing(1.0, rate_);
        const auto gateClosing = smoothing(40.0, rate_);
        const auto followRelease = smoothing(10.0, rate_);
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            auto mono = 0.0;
            for (std::size_t channel = 0; channel < channels; ++channel)
                mono += static_cast<double>(input.channel(static_cast<std::uint32_t>(channel))[frame]);
            mono /= static_cast<double>(std::max<std::size_t>(1, channels));
            if (drive > 0.0)
                mono = std::tanh(mono * (1.0 + drive)) / (1.0 + drive * 0.5);
            predelay_.push(mono);
            const auto x = predelay_.read(predelaySamples);
            // The early reflections.
            early_.push(x);
            auto early = 0.0;
            auto earlyRight = 0.0;
            for (const auto& [ms, gain] : haloEarlyTaps) {
                early += gain * early_.read(ms * scale * 0.001 * rate_);
                earlyRight += gain * early_.read(ms * scale * 1.09 * 0.001 * rate_);
            }
            // Diffused, then smeared as the density builds.
            auto diffused = x;
            for (auto& diffuser : diffusers_)
                diffused = diffuser.process(diffused, earlyDiffusion);
            auto smeared = diffused;
            for (auto& smear : smears_)
                smeared = smear.process(smeared, 0.6);
            const auto inject = diffused * (1.0 - buildUp) + smeared * buildUp + early * 0.3 * earlyLevel;
            // The lines: read, swayed, diffused, damped, decayed band by band.
            if (--driftCount_ <= 0) {
                driftCount_ = static_cast<int>(rate_ / std::max(0.05, rateHz));
                for (auto& target : driftTarget_)
                    target = noise();
            }
            std::array<double, haloLines> heard {};
            for (std::size_t line = 0; line < static_cast<std::size_t>(haloLines); ++line) {
                auto modulation = 0.0;
                if (mode.random) {
                    drift_[line] += (driftTarget_[line] - drift_[line]) * (rateHz * 4.0 / rate_);
                    modulation = drift_[line];
                } else {
                    phases_[line] += 2.0 * kPi * rateHz * (1.0 + 0.13 * static_cast<double>(line)) / rate_;
                    if (phases_[line] > 2.0 * kPi)
                        phases_[line] -= 2.0 * kPi;
                    modulation = std::sin(phases_[line]);
                }
                const auto target = haloLineMs[line] * 0.001 * scale * rate_;
                // A size or mode changed glides the lines there.
                lengths_[line] = lengths_[line] < 0.0 ? target : lengths_[line] + (target - lengths_[line]) * 0.0005;
                auto v = lines_[line].read(lengths_[line] * (1.0 + 0.004 * sway * modulation) - 1.0);
                v = inside_[line].process(v, lateDiffusion);
                v = damping_[line].lowPass(v);
                const auto low = bass_[line].lowPass(v);
                v = low * lowGain_[line] + (v - low) * highGain_[line];
                if (quantum > 0.0)
                    v = std::round(v / quantum) * quantum;
                heard[line] = v;
            }
            // Every line hears every other (a Hadamard matrix, its energy kept).
            auto mixed = heard;
            for (std::size_t span = 1; span < static_cast<std::size_t>(haloLines); span <<= 1)
                for (std::size_t start = 0; start < static_cast<std::size_t>(haloLines); start += span * 2)
                    for (std::size_t i = start; i < start + span; ++i) {
                        const auto a = mixed[i];
                        const auto b = mixed[i + span];
                        mixed[i] = a + b;
                        mixed[i + span] = a - b;
                    }
            constexpr auto norm = 0.35355339059327373; // 1 / sqrt(8)
            for (std::size_t line = 0; line < static_cast<std::size_t>(haloLines); ++line)
                lines_[line].push(inject * (line % 2 == 0 ? 1.0 : -1.0) + mixed[line] * norm);
            // Out: the lines taken two ways, the reflections in front.
            auto left = 0.0;
            auto right = 0.0;
            for (std::size_t line = 0; line < static_cast<std::size_t>(haloLines); ++line) {
                left += heard[line] * (line % 4 < 2 ? 1.0 : -1.0);
                right += heard[line] * (line % 2 == 0 ? 1.0 : -1.0);
            }
            left = left * 0.5 + early * earlyLevel * 0.6;
            right = right * 0.5 + earlyRight * earlyLevel * 0.6;
            if (mode.gated) {
                follower_ = std::max(std::abs(mono), follower_ * followRelease);
                if (follower_ > 0.003)
                    gateTimer_ = gateHold;
                else
                    gateTimer_ = std::max(0.0, gateTimer_ - 1.0 / rate_);
                const auto wanted = gateTimer_ > 0.0 ? 1.0 : 0.0;
                gateGain_ = wanted + (wanted > gateGain_ ? gateOpening : gateClosing) * (gateGain_ - wanted);
                left *= gateGain_;
                right *= gateGain_;
            }
            // The wet sound's band, its width.
            for (auto& filter : filters_[0])
                left = filter(left);
            for (auto& filter : filters_[1])
                right = filter(right);
            const auto middle = 0.5 * (left + right);
            const auto side = 0.5 * (left - right) * width;
            left = middle + side;
            right = middle - side;
            for (std::size_t channel = 0; channel < channels; ++channel) {
                const auto dry = static_cast<double>(input.channel(static_cast<std::uint32_t>(channel))[frame]);
                const auto wet = channels == 1 ? middle : channel % 2 == 0 ? left : right;
                output.channel(static_cast<std::uint32_t>(channel))[frame] = static_cast<float>(dry * (1.0 - mix) + wet * mix);
            }
        }
        for (std::size_t channel = channels; channel < std::min<std::size_t>(input.channelCount, output.channelCount); ++channel)
            std::copy_n(input.channel(static_cast<std::uint32_t>(channel)), frames, output.channel(static_cast<std::uint32_t>(channel)));
    }

private:
    void design(const std::array<float, halo::haloParameterCount>& values) noexcept
    {
        using namespace halo;
        const auto& mode = haloModeOf(values[haloMode]);
        const auto& colour = haloColourOf(values[haloColour]);
        const auto scale = haloScale(values.data());
        // A gated mode keeps its density: its tail lasts long, the gate shuts it.
        auto decay = std::max(0.1, static_cast<double>(values[haloDecay]));
        if (mode.gated)
            decay = std::max(decay * 2.5, 1.5);
        const auto bassMultiplier = std::clamp(static_cast<double>(values[haloBassMultiplier]), 0.25, 4.0);
        for (std::size_t line = 0; line < static_cast<std::size_t>(haloLines); ++line) {
            const auto seconds = haloLineMs[line] * 0.001 * scale;
            highGain_[line] = std::pow(10.0, -3.0 * seconds / decay);
            lowGain_[line] = std::pow(10.0, -3.0 * seconds / (decay * bassMultiplier));
            damping_[line].design(std::clamp(static_cast<double>(values[haloDamping]) / mode.darkness, 500.0, rate_ * 0.45), rate_);
            bass_[line].design(std::clamp(static_cast<double>(values[haloBassCrossover]), 40.0, 2'000.0), rate_);
            inside_[line].samples = (2.3 + 0.7 * static_cast<double>(line)) * 0.001 * scale * rate_;
        }
        constexpr std::array<double, 4> diffuserMs { 4.7, 3.6, 12.7, 9.3 };
        for (std::size_t index = 0; index < diffusers_.size(); ++index)
            diffusers_[index].samples = diffuserMs[index] * 0.001 * scale * rate_;
        smears_[0].samples = 0.037 * scale * rate_;
        smears_[1].samples = 0.053 * scale * rate_;
        const auto top = std::min({ static_cast<double>(values[haloHighCut]), colour.topHz, rate_ * 0.45 });
        const auto bottom = std::clamp(static_cast<double>(values[haloLowCut]), 10.0, 2'000.0);
        for (auto& channel : filters_) {
            channel[0].c = designEqBand(EqBandType::highCut, top, 0.0, 0.7071, 0, 0, rate_);
            channel[1].c = designEqBand(EqBandType::lowCut, bottom, 0.0, 0.7071, 0, 0, rate_);
        }
        designed_ = values;
    }

    double noise() noexcept
    {
        random_ = random_ * 1'664'525u + 1'013'904'223u;
        return static_cast<double>(random_ >> 8) / static_cast<double>(1u << 24) * 2.0 - 1.0;
    }

    double rate_ { 48'000.0 };
    std::size_t channels_ { 2 };
    Delay predelay_;
    Delay early_;
    std::array<AllPass, 4> diffusers_;
    std::array<AllPass, 2> smears_;
    std::array<Delay, halo::haloLines> lines_;
    std::array<AllPass, halo::haloLines> inside_;
    std::array<OnePole, halo::haloLines> damping_ {};
    std::array<OnePole, halo::haloLines> bass_ {};
    std::array<double, halo::haloLines> lowGain_ {};
    std::array<double, halo::haloLines> highGain_ {};
    std::array<double, halo::haloLines> lengths_ {};
    std::array<double, halo::haloLines> phases_ {};
    std::array<double, halo::haloLines> drift_ {};
    std::array<double, halo::haloLines> driftTarget_ {};
    std::array<std::array<Biquad, 2>, 2> filters_ {};
    std::array<float, halo::haloParameterCount> designed_ {};
    int driftCount_ {};
    double gateTimer_ {};
    double gateGain_ {};
    double follower_ {};
    std::uint32_t random_ { 0x7a3c19e5u };
};

} // namespace

std::unique_ptr<EffectProcessor> makeHaloEffect(const EffectDescriptor& descriptor)
{
    if (descriptor.kind == NativeDspNodeKind::halo)
        return std::make_unique<HaloReverb>(descriptor);
    // The effects added after it (AddedEffects.cpp).
    return makeAddedEffect(descriptor);
}

} // namespace cydaw::engine
