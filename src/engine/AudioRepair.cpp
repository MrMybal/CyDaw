#include <cydaw/engine/AudioRepair.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace cydaw::engine::repair {
namespace {

constexpr double twoPi = 2.0 * std::numbers::pi;

std::int64_t lengthOf(const Channels& channels)
{
    return channels.empty() ? 0 : static_cast<std::int64_t>(channels.front().size());
}

// In place, radix 2 (size a power of two).
void transform(std::vector<std::complex<double>>& data, const bool inverse)
{
    const auto n = data.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        auto bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(data[i], data[j]);
    }
    for (std::size_t length = 2; length <= n; length <<= 1) {
        const auto angle = (inverse ? 1.0 : -1.0) * twoPi / static_cast<double>(length);
        const std::complex<double> step(std::cos(angle), std::sin(angle));
        for (std::size_t i = 0; i < n; i += length) {
            std::complex<double> w(1.0, 0.0);
            for (std::size_t k = 0; k < length / 2; ++k) {
                const auto u = data[i + k];
                const auto v = data[i + k + length / 2] * w;
                data[i + k] = u + v;
                data[i + k + length / 2] = u - v;
                w *= step;
            }
        }
    }
    if (inverse)
        for (auto& value : data)
            value /= static_cast<double>(n);
}

struct Biquad {
    double b0 { 1.0 }, b1 {}, b2 {}, a1 {}, a2 {};
    double z1 {}, z2 {};
    double process(const double x) noexcept
    {
        const auto y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() noexcept { z1 = z2 = 0.0; }
};

Biquad notch(const double rate, const double frequency, const double q)
{
    const auto w0 = twoPi * frequency / rate;
    const auto alpha = std::sin(w0) / (2.0 * q);
    const auto a0 = 1.0 + alpha;
    Biquad filter;
    filter.b0 = 1.0 / a0;
    filter.b1 = -2.0 * std::cos(w0) / a0;
    filter.b2 = 1.0 / a0;
    filter.a1 = -2.0 * std::cos(w0) / a0;
    filter.a2 = (1.0 - alpha) / a0;
    return filter;
}

Biquad butterworth(const double rate, const double cutoff, const double q, const bool high)
{
    const auto w0 = twoPi * std::clamp(cutoff, 5.0, 0.49 * rate) / rate;
    const auto alpha = std::sin(w0) / (2.0 * q);
    const auto cosine = std::cos(w0);
    const auto a0 = 1.0 + alpha;
    Biquad filter;
    if (high) {
        filter.b0 = (1.0 + cosine) / 2.0 / a0;
        filter.b1 = -(1.0 + cosine) / a0;
        filter.b2 = filter.b0;
    } else {
        filter.b0 = (1.0 - cosine) / 2.0 / a0;
        filter.b1 = (1.0 - cosine) / a0;
        filter.b2 = filter.b0;
    }
    filter.a1 = -2.0 * cosine / a0;
    filter.a2 = (1.0 - alpha) / a0;
    return filter;
}

// Goertzel: the power of one frequency over a stretch.
double powerAt(const std::vector<float>& signal, const double rate, const double frequency, const std::size_t count)
{
    const auto coefficient = 2.0 * std::cos(twoPi * frequency / rate);
    double s1 = 0.0;
    double s2 = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto s = signal[i] + coefficient * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    return s1 * s1 + s2 * s2 - coefficient * s1 * s2;
}

void filterAll(Channels& channels, const std::vector<Biquad>& filters, const bool zeroPhase)
{
    for (auto& channel : channels) {
        for (auto filter : filters) {
            for (auto& sample : channel)
                sample = static_cast<float>(filter.process(sample));
            if (zeroPhase) {
                filter.reset();
                for (auto it = channel.rbegin(); it != channel.rend(); ++it)
                    *it = static_cast<float>(filter.process(*it));
            }
        }
    }
}

} // namespace

void denoise(Channels& channels, const double rate, const DenoiseSettings& settings)
{
    const auto frames = lengthOf(channels);
    if (frames == 0)
        return;
    const int size = rate > 60'000.0 ? 4'096 : 2'048;
    const int hop = size / 4;
    const int bins = size / 2 + 1;
    std::vector<double> window(static_cast<std::size_t>(size));
    for (int i = 0; i < size; ++i)
        window[static_cast<std::size_t>(i)] = std::sqrt(0.5 - 0.5 * std::cos(twoPi * i / size));
    // Frames from before the start to past the end: every sample under four windows.
    const auto count = static_cast<int>((frames + size) / hop + 1);
    const auto first = -static_cast<std::int64_t>(size);
    const auto channelCount = channels.size();
    std::vector<std::vector<std::vector<std::complex<double>>>> spectra(channelCount);
    std::vector<std::vector<double>> power(static_cast<std::size_t>(count), std::vector<double>(static_cast<std::size_t>(bins)));
    std::vector<std::complex<double>> buffer(static_cast<std::size_t>(size));
    for (std::size_t c = 0; c < channelCount; ++c) {
        spectra[c].resize(static_cast<std::size_t>(count));
        for (int f = 0; f < count; ++f) {
            const auto start = first + static_cast<std::int64_t>(f) * hop;
            for (int i = 0; i < size; ++i) {
                const auto at = start + i;
                const auto sample = at >= 0 && at < frames ? channels[c][static_cast<std::size_t>(at)] : 0.0f;
                buffer[static_cast<std::size_t>(i)] = { sample * window[static_cast<std::size_t>(i)], 0.0 };
            }
            transform(buffer, false);
            spectra[c][static_cast<std::size_t>(f)].assign(buffer.begin(), buffer.begin() + bins);
            for (int b = 0; b < bins; ++b)
                power[static_cast<std::size_t>(f)][static_cast<std::size_t>(b)]
                    += std::norm(buffer[static_cast<std::size_t>(b)]) / static_cast<double>(channelCount);
        }
    }
    // The noise: the mean spectrum of the sound's quietest fifth (its frames
    // inside the sound, by their energy), what is left when nothing plays.
    std::vector<double> noise(static_cast<std::size_t>(bins), 0.0);
    {
        std::vector<std::pair<double, int>> energies;
        for (int f = 0; f < count; ++f) {
            const auto start = first + static_cast<std::int64_t>(f) * hop;
            if (start < 0 || start + size > frames)
                continue;
            auto sum = 0.0;
            for (const auto value : power[static_cast<std::size_t>(f)])
                sum += value;
            energies.emplace_back(sum, f);
        }
        if (energies.empty())
            for (int f = 0; f < count; ++f)
                energies.emplace_back(0.0, f);
        std::sort(energies.begin(), energies.end());
        const auto quietest = std::max<std::size_t>(1, energies.size() / 5);
        for (std::size_t k = 0; k < quietest; ++k)
            for (int b = 0; b < bins; ++b)
                noise[static_cast<std::size_t>(b)] += power[static_cast<std::size_t>(energies[k].second)][static_cast<std::size_t>(b)]
                                                      / static_cast<double>(quietest);
    }
    const auto floor = std::pow(10.0, -std::clamp(settings.reductionDb, 0.0f, 48.0f) / 20.0);
    const auto over = std::clamp(static_cast<double>(settings.sensitivity), 0.5, 4.0);
    const auto keep = 0.9 * std::clamp(static_cast<double>(settings.smoothing), 0.0, 1.0);
    std::vector<double> previous(static_cast<std::size_t>(bins), 1.0);
    std::vector<double> raw(static_cast<std::size_t>(bins));
    std::vector<std::vector<double>> gains(static_cast<std::size_t>(count), std::vector<double>(static_cast<std::size_t>(bins)));
    for (int f = 0; f < count; ++f) {
        for (int b = 0; b < bins; ++b) {
            // The power over three frames: a single frame's luck heard less.
            auto smoothed = 0.0;
            auto weight = 0.0;
            for (int d = -1; d <= 1; ++d)
                if (f + d >= 0 && f + d < count) {
                    smoothed += power[static_cast<std::size_t>(f + d)][static_cast<std::size_t>(b)] * (d == 0 ? 2.0 : 1.0);
                    weight += d == 0 ? 2.0 : 1.0;
                }
            smoothed /= weight;
            const auto ratio = smoothed > 0.0 ? over * noise[static_cast<std::size_t>(b)] / smoothed : 1.0;
            raw[static_cast<std::size_t>(b)] = std::max(floor, std::sqrt(std::max(0.0, 1.0 - ratio)));
        }
        for (int b = 0; b < bins; ++b) {
            // Over neighbouring frequencies, then held a little after a rise.
            auto sum = 0.0;
            auto n = 0;
            for (int d = -2; d <= 2; ++d)
                if (b + d >= 0 && b + d < bins) {
                    sum += raw[static_cast<std::size_t>(b + d)];
                    ++n;
                }
            const auto value = std::max(sum / n, keep * previous[static_cast<std::size_t>(b)]);
            gains[static_cast<std::size_t>(f)][static_cast<std::size_t>(b)] = std::clamp(value, floor, 1.0);
            previous[static_cast<std::size_t>(b)] = value;
        }
    }
    // Back: each frame's spectrum lowered, windowed again, overlapped (the
    // squared root-Hann windows sum to 2 at a quarter hop).
    for (std::size_t c = 0; c < channelCount; ++c) {
        std::vector<double> out(static_cast<std::size_t>(frames), 0.0);
        for (int f = 0; f < count; ++f) {
            auto& spectrum = spectra[c][static_cast<std::size_t>(f)];
            for (int b = 0; b < bins; ++b)
                buffer[static_cast<std::size_t>(b)] = spectrum[static_cast<std::size_t>(b)] * gains[static_cast<std::size_t>(f)][static_cast<std::size_t>(b)];
            for (int b = bins; b < size; ++b)
                buffer[static_cast<std::size_t>(b)] = std::conj(buffer[static_cast<std::size_t>(size - b)]);
            transform(buffer, true);
            const auto start = first + static_cast<std::int64_t>(f) * hop;
            for (int i = 0; i < size; ++i) {
                const auto at = start + i;
                if (at >= 0 && at < frames)
                    out[static_cast<std::size_t>(at)] += buffer[static_cast<std::size_t>(i)].real() * window[static_cast<std::size_t>(i)];
            }
        }
        for (std::int64_t i = 0; i < frames; ++i)
            channels[c][static_cast<std::size_t>(i)] = static_cast<float>(out[static_cast<std::size_t>(i)] / 2.0);
    }
}

float removeHum(Channels& channels, const double rate, const HumSettings& settings)
{
    const auto frames = lengthOf(channels);
    if (frames == 0)
        return 0.0f;
    auto fundamental = static_cast<double>(settings.fundamental);
    if (fundamental <= 0.0) {
        // 50 or 60: the one whose first three harmonics hold more.
        std::vector<float> mono(static_cast<std::size_t>(frames), 0.0f);
        for (const auto& channel : channels)
            for (std::int64_t i = 0; i < frames; ++i)
                mono[static_cast<std::size_t>(i)] += channel[static_cast<std::size_t>(i)] / static_cast<float>(channels.size());
        const auto count = static_cast<std::size_t>(std::min<std::int64_t>(frames, static_cast<std::int64_t>(rate * 20.0)));
        auto fifty = 0.0;
        auto sixty = 0.0;
        for (int k = 1; k <= 3; ++k) {
            fifty += powerAt(mono, rate, 50.0 * k, count);
            sixty += powerAt(mono, rate, 60.0 * k, count);
        }
        fundamental = sixty > fifty ? 60.0 : 50.0;
    }
    std::vector<Biquad> filters;
    const auto q = std::clamp(static_cast<double>(settings.width), 5.0, 100.0);
    for (int k = 1; k <= std::clamp(settings.harmonics, 1, 16); ++k)
        if (fundamental * k < 0.45 * rate)
            filters.push_back(notch(rate, fundamental * k, q));
    filterAll(channels, filters, true);
    return static_cast<float>(fundamental);
}

int declick(Channels& channels, const double rate, const DeclickSettings& settings)
{
    const auto frames = lengthOf(channels);
    if (frames < 16)
        return 0;
    const auto factor = 25.0 - 19.0 * std::clamp(static_cast<double>(settings.sensitivity), 0.0, 1.0);
    const auto longest = std::max<std::int64_t>(1, static_cast<std::int64_t>(std::clamp(settings.maxClickMs, 0.2f, 10.0f) * 0.001 * rate));
    constexpr std::int64_t block = 1'024;
    auto mended = 0;
    for (auto& channel : channels) {
        // The second difference and its typical size block by block (the median).
        std::vector<float> residual(static_cast<std::size_t>(frames), 0.0f);
        for (std::int64_t i = 2; i < frames; ++i)
            residual[static_cast<std::size_t>(i)] = std::abs(channel[static_cast<std::size_t>(i)] - 2.0f * channel[static_cast<std::size_t>(i - 1)]
                                                             + channel[static_cast<std::size_t>(i - 2)]);
        std::vector<float> scale(static_cast<std::size_t>((frames + block - 1) / block));
        std::vector<float> part;
        for (std::size_t b = 0; b < scale.size(); ++b) {
            const auto from = static_cast<std::int64_t>(b) * block;
            const auto to = std::min(frames, from + block);
            part.assign(residual.begin() + from, residual.begin() + to);
            const auto middle = part.begin() + static_cast<std::ptrdiff_t>(part.size() / 2);
            std::nth_element(part.begin(), middle, part.end());
            scale[b] = std::max(*middle, 1.0e-5f);
        }
        std::int64_t i = 2;
        while (i < frames) {
            const auto threshold = static_cast<float>(factor) * scale[static_cast<std::size_t>(i / block)];
            if (residual[static_cast<std::size_t>(i)] <= threshold) {
                ++i;
                continue;
            }
            // A burst: the samples off, gaps of a few samples joined.
            auto end = i;
            for (auto j = i; j < frames && j - end <= 8; ++j)
                if (residual[static_cast<std::size_t>(j)] > threshold)
                    end = j;
            const auto from = std::max<std::int64_t>(2, i - 4);
            const auto to = std::min<std::int64_t>(frames - 3, end + 4);
            i = end + 1;
            if (to - from > longest || to <= from)
                continue;
            // A cubic from the sound before to the sound after, with their slopes.
            const auto a = from - 1;
            const auto b = to + 1;
            const double ya = channel[static_cast<std::size_t>(a)];
            const double yb = channel[static_cast<std::size_t>(b)];
            const double ma = channel[static_cast<std::size_t>(a)] - channel[static_cast<std::size_t>(a - 1)];
            const double mb = channel[static_cast<std::size_t>(b + 1)] - channel[static_cast<std::size_t>(b)];
            const auto span = static_cast<double>(b - a);
            for (auto k = from; k <= to; ++k) {
                const auto t = static_cast<double>(k - a) / span;
                const auto t2 = t * t;
                const auto t3 = t2 * t;
                const auto value = (2 * t3 - 3 * t2 + 1) * ya + (t3 - 2 * t2 + t) * span * ma + (-2 * t3 + 3 * t2) * yb
                                   + (t3 - t2) * span * mb;
                channel[static_cast<std::size_t>(k)] = static_cast<float>(value);
            }
            ++mended;
        }
    }
    return mended;
}

void highpass(Channels& channels, const double rate, const float cutoff, const int order)
{
    std::vector<Biquad> filters;
    if (order >= 4) {
        filters.push_back(butterworth(rate, cutoff, 0.5411961, true));
        filters.push_back(butterworth(rate, cutoff, 1.3065630, true));
    } else {
        filters.push_back(butterworth(rate, cutoff, std::numbers::sqrt2 / 2.0, true));
    }
    filterAll(channels, filters, false);
}

void lowpass(Channels& channels, const double rate, const float cutoff, const int order)
{
    std::vector<Biquad> filters;
    if (order >= 4) {
        filters.push_back(butterworth(rate, cutoff, 0.5411961, false));
        filters.push_back(butterworth(rate, cutoff, 1.3065630, false));
    } else {
        filters.push_back(butterworth(rate, cutoff, std::numbers::sqrt2 / 2.0, false));
    }
    filterAll(channels, filters, false);
}

void gain(Channels& channels, const float db)
{
    const auto factor = std::pow(10.0f, db / 20.0f);
    for (auto& channel : channels)
        for (auto& sample : channel)
            sample *= factor;
}

void normalise(Channels& channels, const float peakDb)
{
    auto peak = 0.0f;
    for (const auto& channel : channels)
        for (const auto sample : channel)
            peak = std::max(peak, std::abs(sample));
    if (peak <= 1.0e-9f)
        return;
    gain(channels, peakDb - 20.0f * std::log10(peak));
}

std::int64_t trimSilence(Channels& channels, const double rate, const float thresholdDb, const float keepMs)
{
    const auto frames = lengthOf(channels);
    if (frames == 0)
        return 0;
    const auto threshold = std::pow(10.0f, thresholdDb / 20.0f);
    std::int64_t first = frames;
    std::int64_t last = -1;
    for (std::int64_t i = 0; i < frames; ++i)
        for (const auto& channel : channels)
            if (std::abs(channel[static_cast<std::size_t>(i)]) > threshold) {
                first = std::min(first, i);
                last = std::max(last, i);
            }
    if (last < 0)
        return 0;
    const auto keep = static_cast<std::int64_t>(std::max(0.0f, keepMs) * 0.001 * rate);
    first = std::max<std::int64_t>(0, first - keep);
    last = std::min(frames - 1, last + keep);
    for (auto& channel : channels)
        channel = std::vector<float>(channel.begin() + first, channel.begin() + last + 1);
    return first;
}

std::int64_t tailEnd(const Channels& channels, const std::int64_t sourceFrames, const double rate, const TailSettings& settings,
                     bool* endless)
{
    if (endless != nullptr)
        *endless = false;
    const auto frames = lengthOf(channels);
    if (!settings.allow || frames <= sourceFrames)
        return std::min(frames, sourceFrames);
    const auto limit = std::min(frames, sourceFrames + static_cast<std::int64_t>(std::max(0.0f, settings.maxSeconds) * rate));
    // Levels every 10 ms (dB), against the rendering's peak.
    const auto hop = std::max<std::int64_t>(1, static_cast<std::int64_t>(0.01 * rate));
    auto peak = 1.0e-9f;
    for (const auto& channel : channels)
        for (std::int64_t i = 0; i < limit; ++i)
            peak = std::max(peak, std::abs(channel[static_cast<std::size_t>(i)]));
    const auto levelAt = [&](const std::int64_t from) {
        auto sum = 0.0;
        const auto to = std::min(limit, from + hop);
        for (const auto& channel : channels)
            for (auto i = from; i < to; ++i)
                sum += static_cast<double>(channel[static_cast<std::size_t>(i)]) * channel[static_cast<std::size_t>(i)];
        const auto mean = sum / std::max<double>(1.0, static_cast<double>((to - from) * static_cast<std::int64_t>(channels.size())));
        return 10.0 * std::log10(mean + 1.0e-20) - 20.0 * std::log10(static_cast<double>(peak));
    };
    std::vector<double> levels;
    for (auto at = sourceFrames; at < limit; at += hop)
        levels.push_back(levelAt(at));
    // Quiet for long enough: the end where it went quiet.
    const auto hold = std::max<std::size_t>(1, static_cast<std::size_t>(std::max(0.0f, settings.holdSeconds) / 0.01));
    std::size_t quiet = 0;
    for (std::size_t i = 0; i < levels.size(); ++i) {
        quiet = levels[i] < settings.thresholdDb ? quiet + 1 : 0;
        if (quiet >= hold)
            return sourceFrames + static_cast<std::int64_t>(i + 1 - quiet) * hop;
    }
    if (endless != nullptr)
        *endless = true;
    // Never quiet: where it stopped decaying (half a second's mean no more
    // than 1 dB under the one two seconds before), else the longest allowed.
    constexpr std::size_t half = 50;
    const auto mean = [&](const std::size_t from) {
        auto sum = 0.0;
        for (auto i = from; i < from + half && i < levels.size(); ++i)
            sum += levels[i];
        return sum / static_cast<double>(std::min(half, levels.size() - from));
    };
    for (std::size_t i = 4 * half; i + half <= levels.size(); i += half / 5)
        if (mean(i) > mean(i - 4 * half) - 1.0)
            return sourceFrames + static_cast<std::int64_t>(i - 3 * half) * hop;
    return limit;
}

void cutWithFade(Channels& channels, const std::int64_t frames, const double rate, const float fadeMs)
{
    const auto length = std::clamp<std::int64_t>(frames, 0, lengthOf(channels));
    const auto fade = std::min<std::int64_t>(length, static_cast<std::int64_t>(std::max(0.0f, fadeMs) * 0.001 * rate));
    for (auto& channel : channels) {
        channel.resize(static_cast<std::size_t>(length));
        for (std::int64_t i = 0; i < fade; ++i)
            channel[static_cast<std::size_t>(length - fade + i)] *= static_cast<float>(1.0 - static_cast<double>(i + 1) / static_cast<double>(fade));
    }
}

} // namespace cydaw::engine::repair
