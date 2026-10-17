#include "cydaw/engine/Loudness.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace cydaw::engine {

namespace {

struct Biquad {
    double b0 {}, b1 {}, b2 {}, a1 {}, a2 {};
    double z1 {}, z2 {};

    double process(const double x) noexcept
    {
        const auto y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// The two stages of the K-weighting at any rate (the standard gives them
// at 48 kHz; these are the analog prototypes it comes from).
std::pair<Biquad, Biquad> kWeighting(const double rate)
{
    Biquad shelf;
    {
        const auto f0 = 1681.974450955533;
        const auto gainDb = 3.999843853973347;
        const auto q = 0.7071752369554196;
        const auto k = std::tan(std::numbers::pi * f0 / rate);
        const auto vh = std::pow(10.0, gainDb / 20.0);
        const auto vb = std::pow(vh, 0.4996667741545416);
        const auto a0 = 1.0 + k / q + k * k;
        shelf.b0 = (vh + vb * k / q + k * k) / a0;
        shelf.b1 = 2.0 * (k * k - vh) / a0;
        shelf.b2 = (vh - vb * k / q + k * k) / a0;
        shelf.a1 = 2.0 * (k * k - 1.0) / a0;
        shelf.a2 = (1.0 - k / q + k * k) / a0;
    }
    Biquad highPass;
    {
        const auto f0 = 38.13547087602444;
        const auto q = 0.5003270373238773;
        const auto k = std::tan(std::numbers::pi * f0 / rate);
        const auto a0 = 1.0 + k / q + k * k;
        highPass.b0 = 1.0;
        highPass.b1 = -2.0;
        highPass.b2 = 1.0;
        highPass.a1 = 2.0 * (k * k - 1.0) / a0;
        highPass.a2 = (1.0 - k / q + k * k) / a0;
    }
    return { shelf, highPass };
}

double weightOf(const int channel, const int channelCount) noexcept
{
    if (channelCount >= 6) {
        if (channel == 3)
            return 0.0;
        if (channel >= 4)
            return 1.41;
    }
    return 1.0;
}

} // namespace

double integratedLoudness(const float* const* channels, const int channelCount, const std::int64_t frames,
                          const double sampleRate)
{
    if (channels == nullptr || channelCount <= 0 || frames <= 0 || sampleRate <= 0.0)
        return -70.0;
    // The weighted squares, summed over the channels, in steps of 100 ms.
    const auto step = std::max<std::int64_t>(1, static_cast<std::int64_t>(std::llround(sampleRate * 0.1)));
    const auto steps = static_cast<std::size_t>((frames + step - 1) / step);
    std::vector<double> sums(steps, 0.0);
    for (int channel = 0; channel < channelCount; ++channel) {
        const auto weight = weightOf(channel, channelCount);
        if (weight <= 0.0 || channels[channel] == nullptr)
            continue;
        auto [shelf, highPass] = kWeighting(sampleRate);
        const auto* data = channels[channel];
        for (std::int64_t i = 0; i < frames; ++i) {
            const auto y = highPass.process(shelf.process(static_cast<double>(data[i])));
            sums[static_cast<std::size_t>(i / step)] += weight * y * y;
        }
    }
    // Blocks of four steps (400 ms), one step apart; a sound shorter than a
    // block is one block of what there is.
    std::vector<double> blocks;
    if (steps < 4) {
        double total = 0.0;
        for (const auto value : sums)
            total += value;
        blocks.push_back(total / static_cast<double>(frames));
    } else {
        for (std::size_t first = 0; first + 4 <= steps; ++first) {
            const auto length = std::min<std::int64_t>(4 * step, frames - static_cast<std::int64_t>(first) * step);
            blocks.push_back((sums[first] + sums[first + 1] + sums[first + 2] + sums[first + 3])
                             / static_cast<double>(std::max<std::int64_t>(1, length)));
        }
    }
    const auto loudnessOf = [](const double power) { return -0.691 + 10.0 * std::log10(std::max(power, 1.0e-20)); };
    const auto meanAbove = [&](const double threshold, bool& any) {
        double total = 0.0;
        std::size_t count = 0;
        for (const auto power : blocks)
            if (loudnessOf(power) > threshold) {
                total += power;
                ++count;
            }
        any = count > 0;
        return count > 0 ? total / static_cast<double>(count) : 0.0;
    };
    bool any = false;
    const auto absolute = meanAbove(-70.0, any);
    if (!any)
        return -70.0;
    const auto relative = meanAbove(loudnessOf(absolute) - 10.0, any);
    return any ? loudnessOf(relative) : loudnessOf(absolute);
}

} // namespace cydaw::engine
