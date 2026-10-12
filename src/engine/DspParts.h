#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

#include <cydaw/engine/BiquadDesign.h>

// The small parts the native effects' processors are made of (MixEffects.cpp,
// SuiteEffects.cpp): biquads and one-poles with their states, a delay line
// read between samples, an oversampler, and the conversions they share.
// Internal to the engine; nothing here allocates once prepared.

namespace cydaw::engine::dsp {

inline constexpr double kPi = std::numbers::pi;
inline constexpr std::size_t kChannels = 8;

inline double dbToGain(const double db) noexcept
{
    return std::pow(10.0, db / 20.0);
}

inline double gainToDb(const double gain) noexcept
{
    return 20.0 * std::log10(std::max(gain, 1.0e-12));
}

// The coefficient of a one-pole smoother of that many milliseconds.
inline double smoothing(const double milliseconds, const double rate) noexcept
{
    return std::exp(-1.0 / (std::max(0.001, milliseconds) * 0.001 * rate));
}

inline int choice(const float value, const int count) noexcept
{
    return std::clamp(static_cast<int>(std::lround(value)), 0, count - 1);
}

struct Biquad {
    BiquadCoefficients c;
    double z1 {};
    double z2 {};
    void reset() noexcept { z1 = z2 = 0.0; }
    double operator()(const double x) noexcept
    {
        const auto y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }
};

// One pole: its low-pass, and the high-pass that is what the low-pass leaves.
struct OnePole {
    double coefficient {};
    double state {};
    void design(const double hz, const double rate) noexcept
    {
        coefficient = std::exp(-2.0 * kPi * std::clamp(hz, 1.0, rate * 0.45) / rate);
    }
    double lowPass(const double x) noexcept
    {
        state = x + coefficient * (state - x);
        return state;
    }
    double highPass(const double x) noexcept { return x - lowPass(x); }
};

// A delay line read between samples (linear).
class Delay {
public:
    void prepare(const std::size_t samples)
    {
        std::size_t size = 1;
        while (size < samples + 4)
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
    // The sample pushed `delay` samples before the last one.
    [[nodiscard]] double read(const double delay) const noexcept
    {
        const auto position = static_cast<double>(write_) - 1.0 - std::clamp(delay, 0.0, static_cast<double>(mask_ - 2));
        const auto whole = std::floor(position);
        const auto t = position - whole;
        const auto index = static_cast<std::int64_t>(whole);
        const auto a = buffer_[static_cast<std::size_t>(index) & mask_];
        const auto b = buffer_[static_cast<std::size_t>(index + 1) & mask_];
        return a + (b - a) * t;
    }

private:
    std::vector<double> buffer_;
    std::size_t mask_ {};
    std::size_t write_ {};
};

inline double besselI0(const double x) noexcept
{
    auto sum = 1.0;
    auto term = 1.0;
    for (int k = 1; k < 32; ++k) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
    }
    return sum;
}

// Up and down by 2 or 4: a linear-phase windowed-sinc filter of 16 taps a
// factor and one, so the way there and back delays the sound 16 samples
// (the amp's, AmpEffects.cpp).
class Oversampler {
public:
    void prepare(const int factor)
    {
        factor_ = std::max(1, factor);
        taps_ = 16 * factor_ + 1;
        coefficients_.assign(static_cast<std::size_t>(taps_), 0.0);
        const auto cutoff = 0.45 / static_cast<double>(factor_);
        const auto middle = (taps_ - 1) * 0.5;
        constexpr auto beta = 7.0;
        auto sum = 0.0;
        for (int n = 0; n < taps_; ++n) {
            const auto t = n - middle;
            const auto sinc = t == 0.0 ? 2.0 * cutoff : std::sin(2.0 * kPi * cutoff * t) / (kPi * t);
            const auto ratio = 2.0 * n / (taps_ - 1) - 1.0;
            const auto window = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - ratio * ratio))) / besselI0(beta);
            coefficients_[static_cast<std::size_t>(n)] = sinc * window;
            sum += sinc * window;
        }
        for (auto& c : coefficients_)
            c /= sum;
        upHistory_.assign(static_cast<std::size_t>((taps_ + factor_ - 1) / factor_ + 1), 0.0);
        downHistory_.assign(static_cast<std::size_t>(taps_), 0.0);
    }
    void reset() noexcept
    {
        std::fill(upHistory_.begin(), upHistory_.end(), 0.0);
        std::fill(downHistory_.begin(), downHistory_.end(), 0.0);
    }
    [[nodiscard]] int latency() const noexcept { return factor_ > 1 ? 16 : 0; }
    void up(const double x, double* out) noexcept
    {
        if (factor_ == 1) {
            out[0] = x;
            return;
        }
        std::copy_backward(upHistory_.begin(), upHistory_.end() - 1, upHistory_.end());
        upHistory_[0] = x;
        for (int phase = 0; phase < factor_; ++phase) {
            auto sum = 0.0;
            for (int k = 0, j = phase; j < taps_; ++k, j += factor_)
                sum += coefficients_[static_cast<std::size_t>(j)] * upHistory_[static_cast<std::size_t>(k)];
            out[phase] = sum * factor_;
        }
    }
    double down(const double* in) noexcept
    {
        if (factor_ == 1)
            return in[0];
        for (int phase = 0; phase < factor_; ++phase) {
            std::copy_backward(downHistory_.begin(), downHistory_.end() - 1, downHistory_.end());
            downHistory_[0] = in[phase];
        }
        auto sum = 0.0;
        for (int n = 0; n < taps_; ++n)
            sum += coefficients_[static_cast<std::size_t>(n)] * downHistory_[static_cast<std::size_t>(n)];
        return sum;
    }

private:
    int factor_ { 1 };
    int taps_ { 1 };
    std::vector<double> coefficients_;
    std::vector<double> upHistory_;
    std::vector<double> downHistory_;
};

} // namespace cydaw::engine::dsp
