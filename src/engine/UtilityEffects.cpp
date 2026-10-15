#include <cydaw/engine/UtilityEffects.h>

#include <cydaw/engine/AudioBuffer.h>
#include <cydaw/engine/EffectCatalogue.h>
#include <cydaw/engine/UtilityDesign.h>

#include "DspParts.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace cydaw::engine {

namespace {

using namespace dsp;

// ---- A delay read between samples, flat to the top of the band ---------------------------------

// Windowed-sinc kernels of 2 to 8 taps a side, at 256 places between two
// samples: what lets a track move by a fraction of a sample without
// dulling its highs (a straight line between samples would).
constexpr int kSincHalfMost = 8;
constexpr int kSincPhases = 256;

struct SincKernels {
    // For a half width h (2..kSincHalfMost): kSincPhases + 1 rows of 2h
    // taps, the row p for a point p / kSincPhases past the older of the
    // two middle samples; each row sums to 1.
    std::array<std::vector<float>, kSincHalfMost + 1> taps;

    SincKernels()
    {
        constexpr auto beta = 6.0;
        for (int half = 2; half <= kSincHalfMost; ++half) {
            const auto width = static_cast<std::size_t>(2 * half);
            auto& table = taps[static_cast<std::size_t>(half)];
            table.assign(static_cast<std::size_t>(kSincPhases + 1) * width, 0.0f);
            for (int phase = 0; phase <= kSincPhases; ++phase) {
                const auto offset = static_cast<double>(phase) / kSincPhases;
                std::array<double, 2 * kSincHalfMost> row {};
                auto sum = 0.0;
                for (int k = -half + 1; k <= half; ++k) {
                    const auto x = offset - static_cast<double>(k);
                    const auto sinc = std::abs(x) < 1.0e-12 ? 1.0 : std::sin(kPi * x) / (kPi * x);
                    const auto ratio = x / static_cast<double>(half);
                    const auto window = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - ratio * ratio))) / besselI0(beta);
                    row[static_cast<std::size_t>(k + half - 1)] = sinc * window;
                    sum += sinc * window;
                }
                for (std::size_t tap = 0; tap < width; ++tap)
                    table[static_cast<std::size_t>(phase) * width + tap] = static_cast<float>(row[tap] / sum);
            }
        }
    }
};

const SincKernels& sincKernels()
{
    static const SincKernels kernels;
    return kernels;
}

class AlignmentLine {
public:
    void prepare(const std::size_t samples)
    {
        std::size_t size = 1;
        while (size < samples + static_cast<std::size_t>(2 * kSincHalfMost + 4))
            size <<= 1;
        buffer_.assign(size, 0.0f);
        mask_ = size - 1;
        write_ = 0;
    }
    void reset() noexcept
    {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        write_ = 0;
    }
    void push(const float x) noexcept
    {
        buffer_[write_] = x;
        write_ = (write_ + 1) & mask_;
    }
    // The sound `delay` samples before the last sample pushed. A whole
    // number reads the sample itself; between two, a kernel as wide as the
    // samples already there allow (none comes from the future).
    [[nodiscard]] double read(const double delay, const SincKernels& kernels) const noexcept
    {
        const auto most = static_cast<double>(mask_ - static_cast<std::size_t>(2 * kSincHalfMost + 2));
        const auto wanted = std::clamp(delay, 0.0, most);
        const auto whole = std::floor(wanted);
        const auto fraction = wanted - whole;
        const auto back = static_cast<std::size_t>(whole);
        const auto newer = (write_ - 1 - back) & mask_;
        if (fraction <= 0.0)
            return buffer_[newer];
        const auto older = (newer - 1) & mask_;
        const auto offset = 1.0 - fraction;
        const auto half = static_cast<int>(std::min<std::size_t>(kSincHalfMost, back + 1));
        if (half < 2)
            return buffer_[older] + (static_cast<double>(buffer_[newer]) - buffer_[older]) * offset;
        const auto width = static_cast<std::size_t>(2 * half);
        const auto& table = kernels.taps[static_cast<std::size_t>(half)];
        const auto place = offset * kSincPhases;
        const auto phase = std::clamp(static_cast<int>(place), 0, kSincPhases - 1);
        const auto between = static_cast<float>(place - phase);
        const auto* a = table.data() + static_cast<std::size_t>(phase) * width;
        const auto* b = a + width;
        auto at = (older - static_cast<std::size_t>(half - 1)) & mask_;
        auto sum = 0.0;
        for (std::size_t tap = 0; tap < width; ++tap) {
            sum += static_cast<double>(a[tap] + (b[tap] - a[tap]) * between) * buffer_[at];
            at = (at + 1) & mask_;
        }
        return sum;
    }

private:
    std::vector<float> buffer_;
    std::size_t mask_ {};
    std::size_t write_ {};
};

// A second-order all-pass: what a fourth-order Linkwitz-Riley crossover's
// two bands sum to, so the middle keeps the phase the sides get.
BiquadCoefficients allPassAt(const double hz, const double rate) noexcept
{
    const auto frequency = std::clamp(hz, 10.0, rate * 0.45);
    const auto w0 = 2.0 * kPi * frequency / rate;
    const auto cosine = std::cos(w0);
    const auto alpha = std::sin(w0) / (2.0 * 0.70710678118654752);
    const auto inverse = 1.0 / (1.0 + alpha);
    return { (1.0 - alpha) * inverse, -2.0 * cosine * inverse, 1.0, -2.0 * cosine * inverse, (1.0 - alpha) * inverse };
}

// A filter's state near nothing set to nothing (the smallest numbers are
// slow), and one gone wrong started again.
void flush(Biquad& filter) noexcept
{
    if (!std::isfinite(filter.z1) || !std::isfinite(filter.z2)) {
        filter.reset();
        return;
    }
    if (std::abs(filter.z1) < 1.0e-25)
        filter.z1 = 0.0;
    if (std::abs(filter.z2) < 1.0e-25)
        filter.z2 = 0.0;
}

double towards(const double now, const double target, const double step) noexcept
{
    return now < target ? std::min(target, now + step) : std::max(target, now - step);
}

// ---- Utilitaire --------------------------------------------------------------------------------

// A track's small settings: which side goes where, polarity, the lows made
// mono, the width, the balance, the gain, a DC filter, a delay to align it
// with another, a mute. At its defaults the sound passes bit for bit: every
// step left at rest is skipped, and a switch fades its step in and out.
class Utilitaire final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;

    void prepare(const ProcessSpec& spec) override
    {
        using namespace utility;
        rate_ = std::max(1.0, spec.sampleRate);
        channels_ = std::min<std::size_t>(kChannels, std::max<std::uint32_t>(1, spec.channelCount));
        kernels_ = &sincKernels();
        const auto longest = static_cast<std::size_t>(std::ceil(utilitaireDelayMostMs * 0.001 * rate_)) + 2;
        for (auto& line : lines_)
            line.prepare(longest);
        for (auto& pole : dc_)
            pole.design(utilitaireDcHz, rate_);
        modeStep_ = 1.0 / (0.01 * rate_);
        bassStep_ = 1.0 / (0.01 * rate_);
        polarityStep_ = 2.0 / (0.005 * rate_);
        dcStep_ = 1.0 / (0.02 * rate_);
        muteStep_ = 1.0 / (0.005 * rate_);
        peakFall_ = std::pow(10.0, -utilitairePeakFallDb / 20.0 / rate_);
        designedBass_ = -1.0f;
        reset();
    }

    void reset() noexcept override
    {
        for (auto& line : lines_)
            line.reset();
        for (auto& pole : dc_)
            pole.state = 0.0;
        for (auto& filter : sideHigh_)
            filter.reset();
        middlePass_.reset();
        started_ = false;
        peaks_ = {};
        for (auto& reading : readings_)
            reading.store(0.0f, std::memory_order_relaxed);
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
        using namespace utility;
        clearAudio(output);
        const auto channels = std::min({ static_cast<std::size_t>(input.channelCount), static_cast<std::size_t>(output.channelCount),
                                         channels_ });
        const auto frames = std::min(input.frameCount, output.frameCount);
        if (channels == 0 || frames == 0)
            return;
        const auto stereo = channels >= 2;
        const auto mode = choice(value(utilitaireChannelMode), utilitaireChannelModeCount);
        const auto invertLeft = choice(value(utilitaireInvertLeft), 2) == 1;
        const auto invertRight = choice(value(utilitaireInvertRight), 2) == 1;
        // A single channel is the centre: either switch turns it over.
        const std::array<double, 2> polarity { (stereo ? invertLeft : invertLeft || invertRight) ? -1.0 : 1.0,
                                               (stereo ? invertRight : invertLeft || invertRight) ? -1.0 : 1.0 };
        const auto bassHz = value(utilitaireMonoBass);
        const auto bassTarget = utilitaireMonoBassOn(bassHz) ? 1.0 : 0.0;
        const auto width = std::clamp(static_cast<double>(value(utilitaireWidth)), 0.0, 2.0);
        const auto balance = utilitaireBalanceOf(value(utilitairePan));
        const auto gain = dbToGain(std::clamp(static_cast<double>(value(utilitaireGain)), -36.0, 36.0));
        const auto dcTarget = choice(value(utilitaireDcFilter), 2) == 1 ? 1.0 : 0.0;
        const auto muteTarget = choice(value(utilitaireMute), 2) == 1 ? 0.0 : 1.0;
        auto delay = std::clamp(static_cast<double>(value(utilitaireDelay)), 0.0, utilitaireDelayMostMs) * 0.001 * rate_;
        if (std::abs(delay - std::round(delay)) < 1.0e-6)
            delay = std::round(delay);
        if (!started_) {
            started_ = true;
            mode_ = fromMode_ = mode;
            modeFade_ = 1.0;
            polarity_ = polarity;
            bassAmount_ = bassTarget;
            width_ = width;
            balance_ = balance;
            gain_ = gain;
            dcAmount_ = dcTarget;
            muteGain_ = muteTarget;
            delay_ = delay;
        }
        if (mode != mode_) {
            fromMode_ = mode_;
            mode_ = mode;
            modeFade_ = 0.0;
        }
        if (bassHz != designedBass_) {
            // The sides lose what is under the frequency (two Butterworth
            // high-passes); the middle passes, turned as they are.
            const auto high = designEqBand(EqBandType::lowCut, static_cast<double>(bassHz), 0.0, 0.70710678118654752, 0, 0, rate_);
            for (auto& filter : sideHigh_)
                filter.c = high;
            middlePass_.c = allPassAt(static_cast<double>(bassHz), rate_);
            designedBass_ = bassHz;
        }
        std::array<double, 2> blockPeaks {};
        const auto inverseFrames = 1.0 / static_cast<double>(frames);
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            // What moved since the last block, reached at this one's end.
            const auto t = static_cast<double>(frame + 1) * inverseFrames;
            const auto widthNow = width_ + (width - width_) * t;
            const auto leftGain = balance_[0] + (balance[0] - balance_[0]) * t;
            const auto rightGain = balance_[1] + (balance[1] - balance_[1]) * t;
            const auto gainNow = gain_ + (gain - gain_) * t;
            const auto delayNow = delay_ + (delay - delay_) * t;
            for (std::size_t side = 0; side < 2; ++side)
                if (polarity_[side] != polarity[side])
                    polarity_[side] = towards(polarity_[side], polarity[side], polarityStep_);
            if (dcAmount_ != dcTarget)
                dcAmount_ = towards(dcAmount_, dcTarget, dcStep_);
            if (muteGain_ != muteTarget)
                muteGain_ = towards(muteGain_, muteTarget, muteStep_);
            if (stereo) {
                const auto left = static_cast<double>(input.channel(0)[frame]);
                const auto right = static_cast<double>(input.channel(1)[frame]);
                // Which side goes where, a change crossfaded.
                auto sides = utilitaireChannelsApplied(mode_, left, right);
                if (modeFade_ < 1.0) {
                    modeFade_ = std::min(1.0, modeFade_ + modeStep_);
                    const auto before = utilitaireChannelsApplied(fromMode_, left, right);
                    for (std::size_t side = 0; side < 2; ++side)
                        sides[side] = before[side] + (sides[side] - before[side]) * modeFade_;
                }
                sides[0] *= polarity_[0];
                sides[1] *= polarity_[1];
                // The lows made mono: the crossover runs all the time so it
                // is warm when it fades in.
                const auto passed = middlePass_(0.5 * (sides[0] + sides[1]));
                const auto high = sideHigh_[1](sideHigh_[0](0.5 * (sides[0] - sides[1])));
                if (bassAmount_ != bassTarget)
                    bassAmount_ = towards(bassAmount_, bassTarget, bassStep_);
                if (bassAmount_ > 0.0) {
                    sides[0] += (passed + high - sides[0]) * bassAmount_;
                    sides[1] += (passed - high - sides[1]) * bassAmount_;
                }
                if (widthNow != 1.0) {
                    const auto middle = 0.5 * (sides[0] + sides[1]);
                    const auto side = 0.5 * (sides[0] - sides[1]) * widthNow;
                    sides[0] = middle + side;
                    sides[1] = middle - side;
                }
                sides[0] *= leftGain * gainNow;
                sides[1] *= rightGain * gainNow;
                for (std::size_t side = 0; side < 2; ++side) {
                    const auto out = finish(side, sides[side], delayNow);
                    output.channel(static_cast<std::uint32_t>(side))[frame] = out;
                    blockPeaks[side] = std::max(blockPeaks[side], static_cast<double>(std::abs(out)));
                }
            } else {
                // One channel: a centred sound, its balance folded back.
                auto x = static_cast<double>(input.channel(0)[frame]) * polarity_[0];
                x *= 0.5 * (leftGain + rightGain) * gainNow;
                const auto out = finish(0, x, delayNow);
                output.channel(0)[frame] = out;
                blockPeaks[0] = std::max(blockPeaks[0], static_cast<double>(std::abs(out)));
                blockPeaks[1] = blockPeaks[0];
            }
            // Channels past the first two: their level, DC, delay and mute.
            for (std::size_t channel = 2; channel < channels; ++channel)
                output.channel(static_cast<std::uint32_t>(channel))[frame] =
                    finish(channel, static_cast<double>(input.channel(static_cast<std::uint32_t>(channel))[frame]) * gainNow, delayNow);
        }
        width_ = width;
        balance_ = balance;
        gain_ = gain;
        delay_ = delay;
        for (auto& filter : sideHigh_)
            flush(filter);
        flush(middlePass_);
        for (auto& pole : dc_)
            if (std::abs(pole.state) < 1.0e-25)
                pole.state = 0.0;
        const auto fall = std::pow(peakFall_, static_cast<double>(frames));
        for (std::size_t side = 0; side < 2; ++side) {
            peaks_[side] = std::max(blockPeaks[side], peaks_[side] * fall);
            if (!std::isfinite(peaks_[side]) || peaks_[side] < 1.0e-9)
                peaks_[side] = 0.0;
            readings_[side].store(static_cast<float>(peaks_[side]), std::memory_order_relaxed);
        }
        for (std::size_t channel = channels; channel < std::min<std::size_t>(input.channelCount, output.channelCount); ++channel)
            std::copy_n(input.channel(static_cast<std::uint32_t>(channel)), frames, output.channel(static_cast<std::uint32_t>(channel)));
    }

private:
    // The steps every channel takes last: the DC filter, the delay, the mute.
    float finish(const std::size_t channel, double x, const double delay) noexcept
    {
        const auto low = dc_[channel].lowPass(x);
        if (dcAmount_ > 0.0)
            x -= low * dcAmount_;
        lines_[channel].push(static_cast<float>(x));
        if (delay > 0.0)
            x = lines_[channel].read(delay, *kernels_);
        if (muteGain_ != 1.0)
            x *= muteGain_;
        return static_cast<float>(x);
    }

    double rate_ { 48'000.0 };
    std::size_t channels_ { 2 };
    const SincKernels* kernels_ {};
    std::array<AlignmentLine, kChannels> lines_;
    std::array<OnePole, kChannels> dc_ {};
    std::array<Biquad, 2> sideHigh_ {};
    Biquad middlePass_ {};
    float designedBass_ { -1.0f };
    double modeStep_ {};
    double bassStep_ {};
    double polarityStep_ {};
    double dcStep_ {};
    double muteStep_ {};
    double peakFall_ { 1.0 };
    // Where the moving values stand.
    bool started_ {};
    int mode_ {};
    int fromMode_ {};
    double modeFade_ { 1.0 };
    std::array<double, 2> polarity_ { 1.0, 1.0 };
    double bassAmount_ {};
    double width_ { 1.0 };
    std::array<double, 2> balance_ { 1.0, 1.0 };
    double gain_ { 1.0 };
    double dcAmount_ {};
    double muteGain_ { 1.0 };
    double delay_ {};
    std::array<double, 2> peaks_ {};
    std::array<std::atomic<float>, utility::utilitaireReadingCount> readings_ {};
};

// ---- Stéréoscope -------------------------------------------------------------------------------

// A stereo image meter: the sound passes as it came; what it reads is how
// alike the two sides are (now, on average, in three bands), which one is
// louder, how wide, and the last samples for the goniometer.
class Stereoscope final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;

    void prepare(const ProcessSpec& spec) override
    {
        using namespace utility;
        rate_ = std::max(1.0, spec.sampleRate);
        const auto averaging = [this](const double seconds) { return 1.0 - std::exp(-1.0 / (seconds * rate_)); };
        now_ = averaging(stereoscopeNowSeconds);
        average_ = averaging(stereoscopeAverageSeconds);
        level_ = averaging(stereoscopeLevelSeconds);
        stride_ = stereoscopePairStrideOf(rate_);
        // Three bands split by fourth-order Linkwitz-Riley filters.
        constexpr auto q = 0.70710678118654752;
        const auto belowLow = designEqBand(EqBandType::highCut, stereoscopeLowHz, 0.0, q, 0, 0, rate_);
        const auto aboveLow = designEqBand(EqBandType::lowCut, stereoscopeLowHz, 0.0, q, 0, 0, rate_);
        const auto belowHigh = designEqBand(EqBandType::highCut, stereoscopeHighHz, 0.0, q, 0, 0, rate_);
        const auto aboveHigh = designEqBand(EqBandType::lowCut, stereoscopeHighHz, 0.0, q, 0, 0, rate_);
        for (auto& side : bands_) {
            side[0][0].c = side[0][1].c = belowLow;
            side[1][0].c = side[1][1].c = aboveLow;
            side[1][2].c = side[1][3].c = belowHigh;
            side[2][0].c = side[2][1].c = aboveHigh;
        }
        reset();
    }

    void reset() noexcept override
    {
        for (auto& side : bands_)
            for (auto& band : side)
                for (auto& filter : band)
                    filter.reset();
        sums_ = {};
        countdown_ = 0;
        pairIndex_ = 0;
        pairWrite_.store(0, std::memory_order_relaxed);
        for (auto& figure : figures_)
            figure.store(0.0f, std::memory_order_relaxed);
        for (auto& sample : pairs_)
            sample.store(0.0f, std::memory_order_relaxed);
    }

    [[nodiscard]] std::size_t readings(float* into, const std::size_t count) const noexcept override
    {
        using namespace utility;
        const auto shown = std::min(count, static_cast<std::size_t>(stereoscopeReadingCount));
        for (std::size_t index = 0; index < std::min<std::size_t>(shown, figures_.size()); ++index)
            into[index] = figures_[index].load(std::memory_order_relaxed);
        const auto start = static_cast<std::size_t>(pairWrite_.load(std::memory_order_acquire));
        for (std::size_t pair = 0; pair < static_cast<std::size_t>(stereoscopePairCount); ++pair) {
            const auto at = static_cast<std::size_t>(stereoscopeReadingPairs) + 2 * pair;
            if (at >= shown)
                break;
            const auto from = (start + pair) % static_cast<std::size_t>(stereoscopePairCount);
            into[at] = pairs_[2 * from].load(std::memory_order_relaxed);
            if (at + 1 < shown)
                into[at + 1] = pairs_[2 * from + 1].load(std::memory_order_relaxed);
        }
        return shown;
    }

    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext&) noexcept override
    {
        using namespace utility;
        clearAudio(output);
        const auto channels = std::min(input.channelCount, output.channelCount);
        const auto frames = std::min(input.frameCount, output.frameCount);
        for (std::uint32_t channel = 0; channel < channels; ++channel)
            std::copy_n(input.channel(channel), frames, output.channel(channel));
        if (channels == 0 || frames == 0)
            return;
        // A single channel is a centred sound.
        const auto* left = input.channel(0);
        const auto* right = channels >= 2 ? input.channel(1) : left;
        auto s = sums_;
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            const auto l = static_cast<double>(left[frame]);
            const auto r = static_cast<double>(right[frame]);
            const auto ll = l * l;
            const auto rr = r * r;
            const auto lr = l * r;
            s.nowLL += (ll - s.nowLL) * now_;
            s.nowRR += (rr - s.nowRR) * now_;
            s.nowLR += (lr - s.nowLR) * now_;
            s.averageLL += (ll - s.averageLL) * average_;
            s.averageRR += (rr - s.averageRR) * average_;
            s.averageLR += (lr - s.averageLR) * average_;
            s.levelLL += (ll - s.levelLL) * level_;
            s.levelRR += (rr - s.levelRR) * level_;
            const auto middle = 0.5 * (l + r);
            const auto side = 0.5 * (l - r);
            s.middle += (middle * middle - s.middle) * level_;
            s.side += (side * side - s.side) * level_;
            for (std::size_t band = 0; band < 3; ++band) {
                auto bandLeft = l;
                auto bandRight = r;
                const auto stages = band == 1 ? std::size_t { 4 } : std::size_t { 2 };
                for (std::size_t stage = 0; stage < stages; ++stage) {
                    bandLeft = bands_[0][band][stage](bandLeft);
                    bandRight = bands_[1][band][stage](bandRight);
                }
                s.bandLL[band] += (bandLeft * bandLeft - s.bandLL[band]) * level_;
                s.bandRR[band] += (bandRight * bandRight - s.bandRR[band]) * level_;
                s.bandLR[band] += (bandLeft * bandRight - s.bandLR[band]) * level_;
            }
            if (--countdown_ <= 0) {
                countdown_ = stride_;
                pairs_[2 * pairIndex_].store(left[frame], std::memory_order_relaxed);
                pairs_[2 * pairIndex_ + 1].store(right[frame], std::memory_order_relaxed);
                pairIndex_ = (pairIndex_ + 1) % static_cast<std::size_t>(stereoscopePairCount);
            }
        }
        // Silence decays towards the smallest numbers: stop before them.
        for (auto* value : { &s.nowLL, &s.nowRR, &s.nowLR, &s.averageLL, &s.averageRR, &s.averageLR, &s.levelLL, &s.levelRR, &s.middle,
                             &s.side })
            if (std::abs(*value) < 1.0e-30 || !std::isfinite(*value))
                *value = 0.0;
        for (std::size_t band = 0; band < 3; ++band)
            for (auto* value : { &s.bandLL[band], &s.bandRR[band], &s.bandLR[band] })
                if (std::abs(*value) < 1.0e-30 || !std::isfinite(*value))
                    *value = 0.0;
        for (auto& sideBands : bands_)
            for (auto& band : sideBands)
                for (auto& filter : band)
                    flush(filter);
        sums_ = s;
        pairWrite_.store(static_cast<int>(pairIndex_), std::memory_order_release);
        const auto store = [this](const int index, const double value) {
            figures_[static_cast<std::size_t>(index)].store(static_cast<float>(value), std::memory_order_relaxed);
        };
        store(stereoscopeReadingCorrelation, stereoscopeCorrelationOf(s.nowLR, s.nowLL, s.nowRR));
        store(stereoscopeReadingCorrelationAverage, stereoscopeCorrelationOf(s.averageLR, s.averageLL, s.averageRR));
        store(stereoscopeReadingBalance, stereoscopeBalanceOf(s.levelLL, s.levelRR));
        store(stereoscopeReadingWidth, stereoscopeWidthOf(s.middle, s.side));
        for (std::size_t band = 0; band < 3; ++band)
            store(stereoscopeReadingLowBand + static_cast<int>(band), stereoscopeCorrelationOf(s.bandLR[band], s.bandLL[band], s.bandRR[band]));
    }

private:
    struct Sums {
        double nowLL {};
        double nowRR {};
        double nowLR {};
        double averageLL {};
        double averageRR {};
        double averageLR {};
        double levelLL {};
        double levelRR {};
        double middle {};
        double side {};
        std::array<double, 3> bandLL {};
        std::array<double, 3> bandRR {};
        std::array<double, 3> bandLR {};
    };

    double rate_ { 48'000.0 };
    double now_ {};
    double average_ {};
    double level_ {};
    int stride_ { 1 };
    int countdown_ {};
    std::size_t pairIndex_ {};
    // Per side, per band (low, middle, high), its sections.
    std::array<std::array<std::array<Biquad, 4>, 3>, 2> bands_ {};
    Sums sums_ {};
    std::array<std::atomic<float>, utility::stereoscopeReadingPairs> figures_ {};
    std::array<std::atomic<float>, 2 * utility::stereoscopePairCount> pairs_ {};
    std::atomic<int> pairWrite_ {};
};

} // namespace

std::unique_ptr<EffectProcessor> makeUtilityEffect(const EffectDescriptor& descriptor)
{
    if (descriptor.kind == NativeDspNodeKind::utilitaire)
        return std::make_unique<Utilitaire>(descriptor);
    if (descriptor.kind == NativeDspNodeKind::stereoscope)
        return std::make_unique<Stereoscope>(descriptor);
    return {};
}

} // namespace cydaw::engine
