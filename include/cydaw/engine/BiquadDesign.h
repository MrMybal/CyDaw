#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>

// The bands of the parametric equaliser, designed once for both the
// processor and the curve the interface draws, so what is drawn is what is
// heard. Second-order sections from the RBJ cookbook; the cuts cascade
// Butterworth sections for their slope.

namespace cydaw::engine {

enum class EqBandType : std::uint8_t {
    bell,
    lowShelf,
    highShelf,
    lowCut,
    highCut,
    notch,
    bandPass,
    tilt
};
inline constexpr int eqBandTypeCount = 8;
// Cut slopes: 12, 24, 36 and 48 dB per octave.
inline constexpr int eqSlopeCount = 4;
inline constexpr int eqMaximumStages = 4;

struct BiquadCoefficients {
    double b0 { 1.0 };
    double b1 {};
    double b2 {};
    double a1 {};
    double a2 {};
};

// How many sections a band runs.
[[nodiscard]] inline int eqBandStages(const EqBandType type, const int slope) noexcept
{
    switch (type) {
    case EqBandType::lowCut:
    case EqBandType::highCut: return std::clamp(slope, 0, eqSlopeCount - 1) + 1;
    case EqBandType::tilt: return 2;
    default: return 1;
    }
}

namespace biquad_detail {

inline BiquadCoefficients normalise(const double b0, const double b1, const double b2, const double a0, const double a1,
                                    const double a2) noexcept
{
    const auto inverse = 1.0 / (std::abs(a0) < 1.0e-12 ? 1.0e-12 : a0);
    return { b0 * inverse, b1 * inverse, b2 * inverse, a1 * inverse, a2 * inverse };
}

// The Q of each Butterworth section of an even order, the gentlest first.
inline double butterworthQ(const int order, const int section) noexcept
{
    const auto angle = std::numbers::pi * (2.0 * section + 1.0) / (2.0 * order);
    return 1.0 / (2.0 * std::cos(angle));
}

inline BiquadCoefficients shelf(const double frequency, const double gainDb, const double q, const bool high,
                                const double rate) noexcept
{
    const auto a = std::pow(10.0, gainDb / 40.0);
    const auto w0 = 2.0 * std::numbers::pi * frequency / rate;
    const auto cosine = std::cos(w0);
    const auto alpha = std::sin(w0) / (2.0 * std::max(0.1, q));
    const auto root = 2.0 * std::sqrt(a) * alpha;
    if (!high)
        return normalise(a * ((a + 1.0) - (a - 1.0) * cosine + root), 2.0 * a * ((a - 1.0) - (a + 1.0) * cosine),
                         a * ((a + 1.0) - (a - 1.0) * cosine - root), (a + 1.0) + (a - 1.0) * cosine + root,
                         -2.0 * ((a - 1.0) + (a + 1.0) * cosine), (a + 1.0) + (a - 1.0) * cosine - root);
    return normalise(a * ((a + 1.0) + (a - 1.0) * cosine + root), -2.0 * a * ((a - 1.0) + (a + 1.0) * cosine),
                     a * ((a + 1.0) + (a - 1.0) * cosine - root), (a + 1.0) - (a - 1.0) * cosine + root,
                     2.0 * ((a - 1.0) - (a + 1.0) * cosine), (a + 1.0) - (a - 1.0) * cosine - root);
}

} // namespace biquad_detail

// One section of a band. `stage` counts from 0 to eqBandStages() - 1.
[[nodiscard]] inline BiquadCoefficients designEqBand(const EqBandType type, const double frequencyHz, const double gainDb,
                                                     const double q, const int slope, const int stage,
                                                     const double sampleRate) noexcept
{
    using namespace biquad_detail;
    const auto rate = std::max(1'000.0, sampleRate);
    const auto frequency = std::clamp(frequencyHz, 10.0, rate * 0.49);
    const auto w0 = 2.0 * std::numbers::pi * frequency / rate;
    const auto cosine = std::cos(w0);
    const auto sine = std::sin(w0);
    const auto quality = std::clamp(q, 0.05, 40.0);
    switch (type) {
    case EqBandType::bell: {
        const auto a = std::pow(10.0, gainDb / 40.0);
        const auto alpha = sine / (2.0 * quality);
        return normalise(1.0 + alpha * a, -2.0 * cosine, 1.0 - alpha * a, 1.0 + alpha / a, -2.0 * cosine, 1.0 - alpha / a);
    }
    case EqBandType::lowShelf: return shelf(frequency, gainDb, quality, false, rate);
    case EqBandType::highShelf: return shelf(frequency, gainDb, quality, true, rate);
    case EqBandType::tilt:
        // Down on one side, up on the other, about the frequency.
        return stage == 0 ? shelf(frequency, -gainDb * 0.5, 0.5, false, rate) : shelf(frequency, gainDb * 0.5, 0.5, true, rate);
    case EqBandType::lowCut:
    case EqBandType::highCut: {
        const auto stages = eqBandStages(type, slope);
        // Butterworth, the last section's Q scaled by the band's for a
        // resonant edge.
        auto sectionQ = butterworthQ(stages * 2, stage);
        if (stage == stages - 1)
            sectionQ *= quality * std::numbers::sqrt2;
        const auto alpha = sine / (2.0 * std::max(0.05, sectionQ));
        if (type == EqBandType::lowCut)
            return normalise((1.0 + cosine) * 0.5, -(1.0 + cosine), (1.0 + cosine) * 0.5, 1.0 + alpha, -2.0 * cosine, 1.0 - alpha);
        return normalise((1.0 - cosine) * 0.5, 1.0 - cosine, (1.0 - cosine) * 0.5, 1.0 + alpha, -2.0 * cosine, 1.0 - alpha);
    }
    case EqBandType::notch: {
        const auto alpha = sine / (2.0 * quality);
        return normalise(1.0, -2.0 * cosine, 1.0, 1.0 + alpha, -2.0 * cosine, 1.0 - alpha);
    }
    case EqBandType::bandPass: {
        const auto alpha = sine / (2.0 * quality);
        return normalise(alpha, 0.0, -alpha, 1.0 + alpha, -2.0 * cosine, 1.0 - alpha);
    }
    }
    return {};
}

// A section's gain at a frequency, in dB.
[[nodiscard]] inline double biquadMagnitudeDb(const BiquadCoefficients& c, const double hz, const double sampleRate) noexcept
{
    const auto omega = 2.0 * std::numbers::pi * std::min(hz, sampleRate * 0.4999) / sampleRate;
    const auto z = std::polar(1.0, -omega);
    const auto response = (c.b0 + c.b1 * z + c.b2 * z * z) / (1.0 + c.a1 * z + c.a2 * z * z);
    return 20.0 * std::log10(std::max(std::abs(response), 1.0e-9));
}

// A whole band's gain at a frequency, in dB.
[[nodiscard]] inline double eqBandMagnitudeDb(const EqBandType type, const double frequencyHz, const double gainDb,
                                              const double q, const int slope, const double hz,
                                              const double sampleRate) noexcept
{
    auto db = 0.0;
    for (auto stage = 0; stage < eqBandStages(type, slope); ++stage)
        db += biquadMagnitudeDb(designEqBand(type, frequencyHz, gainDb, q, slope, stage, sampleRate), hz, sampleRate);
    return db;
}

} // namespace cydaw::engine
