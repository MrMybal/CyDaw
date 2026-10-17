#include "SignalAnalysis.h"

#include <cydaw/engine/Fft.h>
#include <numbers>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace cydaw::v2 {
namespace {

constexpr int kOrder = 11;
constexpr int kSize = 1 << kOrder;
static_assert(kSize == static_cast<int>(engine::AudioAnalysisFrame::waveformCapacity));

// One transform and its window, shared by every display.
struct Transform {
    engine::Fft fft;
    std::array<std::complex<float>,kSize> complex;
    void forward(){for(int i=0;i<kSize;++i)complex[i]={work[i],0};fft.forward(complex.data());for(int i=0;i<kSize/2;++i)work[i]=std::abs(complex[i]);}
    std::vector<float> window;
    std::vector<float> work;

    Transform() : window(kSize), work(kSize * 2)
    {
        fft.prepare(kSize);
        for (auto index = 0; index < kSize; ++index)
            window[static_cast<std::size_t>(index)]
                = 0.5f - 0.5f * std::cos(2.0f * std::numbers::pi_v<float> * static_cast<float>(index)
                                         / static_cast<float>(kSize - 1));
    }
};

Transform& transform()
{
    static Transform shared;
    return shared;
}

float toDb(const float gain)
{
    return 20.0f * std::log10(std::max(gain, 1.0e-9f));
}

} // namespace

float Spectrum::frequencyOfBin(const float bin) noexcept
{
    return minimumHz * std::pow(maximumHz / minimumHz, bin / static_cast<float>(binCount - 1));
}

float Spectrum::binOfFrequency(const float hz) noexcept
{
    return std::log(std::max(hz, 1.0f) / minimumHz) / std::log(maximumHz / minimumHz) * static_cast<float>(binCount - 1);
}

void Spectrum::clear()
{
    levels_.fill(0.0f);
    visible_ = false;
}

void Spectrum::update(const engine::AudioAnalysisFrame* frame, const double nowSeconds)
{
    const auto elapsed = static_cast<float>(std::clamp(nowSeconds - lastUpdate_, 0.0, 0.25));
    lastUpdate_ = nowSeconds;
    // Falls about 30 dB a second, rises at once.
    const auto fall = elapsed * 30.0f / (ceilingDb - floorDb);
    std::array<float, binCount> target {};
    if (frame != nullptr && frame->waveformCount >= 256) {
        auto& t = transform();
        std::fill(t.work.begin(), t.work.end(), 0.0f);
        const auto count = static_cast<int>(frame->waveformCount);
        // The most recent samples, at the end of the block the window covers.
        const auto offset = kSize - count;
        for (auto index = 0; index < count; ++index)
            t.work[static_cast<std::size_t>(offset + index)]
                = frame->waveform[static_cast<std::size_t>(index)] * t.window[static_cast<std::size_t>(offset + index)];
        t.forward();
        // A full-scale sine reads 0 dB: two sides, and the Hann window's
        // coherent gain of a half.
        const auto scale = 4.0f / static_cast<float>(kSize);
        const auto rate = std::max(1'000.0f, frame->sampleRate);
        const auto hzPerBin = rate / static_cast<float>(kSize);
        for (auto bin = 0; bin < binCount; ++bin) {
            const auto low = frequencyOfBin(static_cast<float>(bin) - 0.5f) / hzPerBin;
            const auto high = frequencyOfBin(static_cast<float>(bin) + 0.5f) / hzPerBin;
            auto magnitude = 0.0f;
            if (high - low < 1.0f) {
                // Narrower than one bin of the transform: read between bins.
                const auto position = std::clamp((low + high) * 0.5f, 0.0f, static_cast<float>(kSize / 2 - 2));
                const auto lower = static_cast<int>(position);
                const auto fraction = position - static_cast<float>(lower);
                magnitude = t.work[static_cast<std::size_t>(lower)] * (1.0f - fraction)
                            + t.work[static_cast<std::size_t>(lower + 1)] * fraction;
            } else {
                const auto first = std::clamp(static_cast<int>(std::ceil(low)), 1, kSize / 2 - 1);
                const auto last = std::clamp(static_cast<int>(std::floor(high)), first, kSize / 2 - 1);
                for (auto index = first; index <= last; ++index)
                    magnitude = std::max(magnitude, t.work[static_cast<std::size_t>(index)]);
            }
            // Tilted by 4.5 dB an octave around 1 kHz, as analysers do, so a
            // mix reads flat rather than falling to the right.
            const auto tilt = 4.5f * std::log2(frequencyOfBin(static_cast<float>(bin)) / 1'000.0f);
            const auto db = toDb(magnitude * scale) + tilt;
            target[static_cast<std::size_t>(bin)] = std::clamp((db - floorDb) / (ceilingDb - floorDb), 0.0f, 1.0f);
        }
    }
    visible_ = false;
    for (auto bin = 0; bin < binCount; ++bin) {
        auto& level = levels_[static_cast<std::size_t>(bin)];
        const auto next = target[static_cast<std::size_t>(bin)];
        level = next >= level ? next : std::max(next, level - fall);
        visible_ = visible_ || level > 0.02f;
    }
}

float Spectrometer::frequencyOfBin(const float bin) noexcept
{
    return 20.0f * std::pow(1'000.0f, bin / static_cast<float>(binCount - 1));
}

const float* Spectrometer::row(const int age) const noexcept
{
    const auto at = ((newestRow_ - std::clamp(age, 0, rowCount - 1)) % rowCount + rowCount) % rowCount;
    return rows_.data() + static_cast<std::size_t>(at) * binCount;
}

void Spectrometer::clearPeaks()
{
    peaks_ = levels_;
    peakTimes_.fill(lastUpdate_);
}

void Spectrometer::update(const engine::AudioAnalysisFrame* frame, const double nowSeconds, const Settings& settings)
{
    const auto silence = settings.floorDb - 24.0f;
    if (!started_) {
        levels_.fill(silence);
        peaks_.fill(silence);
        rows_.fill(silence);
        lastUpdate_ = lastRow_ = nowSeconds;
        started_ = true;
    }
    const auto elapsed = static_cast<float>(std::clamp(nowSeconds - lastUpdate_, 0.0, 0.5));
    lastUpdate_ = nowSeconds;
    std::array<float, binCount> target {};
    target.fill(silence);
    const auto fresh = frame != nullptr && frame->waveformCount >= 256 && frame->sequence != sequence_;
    if (fresh) {
        sequence_ = frame->sequence;
        lastFresh_ = nowSeconds;
        auto& t = transform();
        std::fill(t.work.begin(), t.work.end(), 0.0f);
        const auto count = static_cast<int>(frame->waveformCount);
        const auto offset = kSize - count;
        for (auto index = 0; index < count; ++index)
            t.work[static_cast<std::size_t>(offset + index)]
                = frame->waveform[static_cast<std::size_t>(index)] * t.window[static_cast<std::size_t>(offset + index)];
        t.forward();
        const auto scale = 4.0f / static_cast<float>(kSize);
        const auto hzPerBin = std::max(1'000.0f, frame->sampleRate) / static_cast<float>(kSize);
        for (auto bin = 0; bin < binCount; ++bin) {
            const auto low = frequencyOfBin(static_cast<float>(bin) - 0.5f) / hzPerBin;
            const auto high = frequencyOfBin(static_cast<float>(bin) + 0.5f) / hzPerBin;
            auto magnitude = 0.0f;
            if (high - low < 1.0f) {
                const auto position = std::clamp((low + high) * 0.5f, 0.0f, static_cast<float>(kSize / 2 - 2));
                const auto lower = static_cast<int>(position);
                const auto fraction = position - static_cast<float>(lower);
                magnitude = t.work[static_cast<std::size_t>(lower)] * (1.0f - fraction)
                            + t.work[static_cast<std::size_t>(lower + 1)] * fraction;
            } else {
                const auto first = std::clamp(static_cast<int>(std::ceil(low)), 1, kSize / 2 - 1);
                const auto last = std::clamp(static_cast<int>(std::floor(high)), first, kSize / 2 - 1);
                for (auto index = first; index <= last; ++index)
                    magnitude = std::max(magnitude, t.work[static_cast<std::size_t>(index)]);
            }
            const auto tilt = settings.tiltDb * std::log2(frequencyOfBin(static_cast<float>(bin)) / 1'000.0f);
            target[static_cast<std::size_t>(bin)] = toDb(magnitude * scale) + tilt;
        }
    } else if (nowSeconds - lastFresh_ < 0.25) {
        // Between two frames: kept, not fallen.
        target = levels_;
    }
    active_ = nowSeconds - lastFresh_ < 0.3;
    // Smoothed in time: a rise four times quicker than a fall.
    const auto tau = 0.012f + std::clamp(settings.smoothing, 0.0f, 1.0f) * 0.6f;
    const auto fall = 1.0f - std::exp(-elapsed / tau);
    const auto rise = 1.0f - std::exp(-elapsed / (tau * 0.25f));
    const auto holdForever = settings.holdSeconds >= 9.95f;
    for (auto bin = 0; bin < binCount; ++bin) {
        auto& level = levels_[static_cast<std::size_t>(bin)];
        const auto next = target[static_cast<std::size_t>(bin)];
        level += (next - level) * (next > level ? rise : fall);
        level = std::max(level, silence);
        auto& peak = peaks_[static_cast<std::size_t>(bin)];
        auto& held = peakTimes_[static_cast<std::size_t>(bin)];
        if (settings.holdSeconds <= 0.0f) {
            peak = level;
        } else if (level >= peak) {
            peak = level;
            held = nowSeconds;
        } else if (!holdForever && nowSeconds - held > settings.holdSeconds) {
            // Held, then down at 24 dB a second to the curve.
            peak = std::max(level, peak - 24.0f * elapsed);
        }
    }
    // A row for the waterfall as often as its length asks.
    const auto interval = static_cast<double>(std::clamp(settings.historySeconds, 1.0f, 60.0f)) / rowCount;
    auto rows = 0;
    while (nowSeconds - lastRow_ >= interval && rows < rowCount) {
        lastRow_ += interval;
        newestRow_ = (newestRow_ + 1) % rowCount;
        std::copy(levels_.begin(), levels_.end(), rows_.begin() + static_cast<std::ptrdiff_t>(newestRow_) * binCount);
        ++rows;
    }
    if (nowSeconds - lastRow_ > interval * rowCount)
        lastRow_ = nowSeconds;
}

float recentPeakDb(const engine::AudioAnalysisFrame& frame, const std::uint32_t samples)
{
    const auto count = std::min(samples, frame.waveformCount);
    auto peak = 0.0f;
    for (auto index = frame.waveformCount - count; index < frame.waveformCount; ++index)
        peak = std::max(peak, std::abs(frame.waveform[index]));
    return toDb(peak);
}

float recentRmsDb(const engine::AudioAnalysisFrame& frame, const std::uint32_t samples)
{
    const auto count = std::min(samples, frame.waveformCount);
    if (count == 0)
        return -120.0f;
    auto energy = 0.0;
    for (auto index = frame.waveformCount - count; index < frame.waveformCount; ++index)
        energy += static_cast<double>(frame.waveform[index]) * frame.waveform[index];
    return toDb(static_cast<float>(std::sqrt(energy / count)));
}

} // namespace cydaw::v2
