#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

// The pitch of a sound as it goes (a voice, a string): the YIN method on the
// sound low-passed and read at about 12 kHz, a new answer every ~10 ms. For
// the tuner, the vocoder's following carrier, Harmonie's voice mode and
// Justesse. Nothing is allocated once prepared; one sample at a time on the
// audio thread.

namespace cydaw::engine {

class PitchTracker final {
public:
    // What it looks for: between `lowest` and `highest` Hz (50..1600 by
    // default).
    void prepare(const double sampleRate, const double lowest = 50.0, const double highest = 1'600.0)
    {
        rate_ = std::max(8'000.0, sampleRate);
        decimation_ = std::max(1, static_cast<int>(std::lround(rate_ / 12'000.0)));
        readRate_ = rate_ / decimation_;
        // 85 ms read at about 12 kHz: periods down to ~24 Hz fit twice.
        window_ = 1'024;
        hop_ = 128;
        history_.assign(static_cast<std::size_t>(window_ * 2 + 8), 0.0f);
        difference_.assign(static_cast<std::size_t>(window_ / 2 + 4), 0.0f);
        raw_.assign(static_cast<std::size_t>(window_ / 2 + 4), 0.0f);
        setRange(lowest, highest);
        // Two second-order low-passes at a third of the reading rate.
        designLowPass(readRate_ / 3.0);
        reset();
    }

    void setRange(const double lowest, const double highest) noexcept
    {
        const auto low = std::clamp(lowest, 30.0, 2'000.0);
        const auto high = std::clamp(highest, low * 1.5, readRate_ * 0.4);
        tauMin_ = std::max(2, static_cast<int>(std::floor(readRate_ / high)));
        tauMax_ = std::clamp(static_cast<int>(std::ceil(readRate_ / low)), tauMin_ + 2, window_ / 2);
    }

    // How strict its "it is a pitch" is: 0.05 (strict) .. 0.4 (lenient).
    void setThreshold(const float threshold) noexcept { threshold_ = std::clamp(threshold, 0.02f, 0.5f); }

    void reset() noexcept
    {
        std::fill(history_.begin(), history_.end(), 0.0f);
        write_ = 0;
        counted_ = 0;
        sinceLast_ = 0;
        phase_ = 0;
        for (auto& stage : stages_)
            stage.z1 = stage.z2 = 0.0;
        frequency_ = 0.0f;
        confidence_ = 0.0f;
        level_ = 0.0f;
        fresh_ = false;
    }

    // One sample of the sound (full rate). True when a new answer is there.
    bool push(const float sample) noexcept
    {
        auto x = static_cast<double>(sample);
        for (auto& stage : stages_)
            x = stage.process(x);
        if (++phase_ < decimation_)
            return false;
        phase_ = 0;
        const auto value = static_cast<float>(x);
        // Kept twice over so a window reads without wrapping.
        const auto size = static_cast<std::size_t>(window_);
        history_[write_] = value;
        history_[write_ + size] = value;
        write_ = (write_ + 1) % size;
        counted_ = std::min(counted_ + 1, window_);
        if (++sinceLast_ < hop_ || counted_ < window_)
            return false;
        sinceLast_ = 0;
        analyse();
        fresh_ = true;
        return true;
    }

    // The last answer: Hz (0 when no pitch), how sure (0..1), how loud the
    // window was (RMS).
    [[nodiscard]] float frequency() const noexcept { return frequency_; }
    [[nodiscard]] float confidence() const noexcept { return confidence_; }
    [[nodiscard]] float level() const noexcept { return level_; }
    // How long, in full-rate samples, between two answers.
    [[nodiscard]] int hopSamples() const noexcept { return hop_ * decimation_; }

    // The MIDI note of a frequency (fractional), A4 at `reference` Hz.
    [[nodiscard]] static double noteOf(const double hz, const double reference = 440.0) noexcept
    {
        return hz > 0.0 ? 69.0 + 12.0 * std::log2(hz / reference) : -1.0;
    }
    [[nodiscard]] static double hzOf(const double note, const double reference = 440.0) noexcept
    {
        return reference * std::pow(2.0, (note - 69.0) / 12.0);
    }

private:
    struct Stage {
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

    void designLowPass(const double hz) noexcept
    {
        const auto omega = 2.0 * std::numbers::pi * std::min(hz, rate_ * 0.45) / rate_;
        const auto cosine = std::cos(omega);
        const auto alpha = std::sin(omega) / (2.0 * 0.7071);
        const auto a0 = 1.0 + alpha;
        for (auto& stage : stages_) {
            stage.b0 = (1.0 - cosine) * 0.5 / a0;
            stage.b1 = (1.0 - cosine) / a0;
            stage.b2 = stage.b0;
            stage.a1 = -2.0 * cosine / a0;
            stage.a2 = (1.0 - alpha) / a0;
        }
    }

    void analyse() noexcept
    {
        const auto* x = history_.data() + write_; // the oldest of the window first
        auto energy = 0.0;
        for (int i = 0; i < window_; ++i)
            energy += static_cast<double>(x[i]) * x[i];
        level_ = static_cast<float>(std::sqrt(energy / window_));
        if (level_ < 1.0e-4f) {
            frequency_ = 0.0f;
            confidence_ = 0.0f;
            return;
        }
        // The difference function over the window's first half, then its
        // cumulative mean normalised form.
        const auto span = window_ - tauMax_;
        auto running = 0.0;
        difference_[0] = 1.0f;
        raw_[0] = 0.0f;
        for (int tau = 1; tau <= tauMax_; ++tau) {
            auto sum = 0.0;
            for (int i = 0; i < span; ++i) {
                const auto d = static_cast<double>(x[i]) - x[i + tau];
                sum += d * d;
            }
            running += sum;
            raw_[static_cast<std::size_t>(tau)] = static_cast<float>(sum);
            difference_[static_cast<std::size_t>(tau)]
                = running > 0.0 ? static_cast<float>(sum * tau / running) : 1.0f;
        }
        // The first dip under the threshold (its bottom), else the deepest.
        auto chosen = -1;
        for (int tau = tauMin_; tau <= tauMax_; ++tau) {
            if (difference_[static_cast<std::size_t>(tau)] < threshold_) {
                while (tau + 1 <= tauMax_ && difference_[static_cast<std::size_t>(tau + 1)] < difference_[static_cast<std::size_t>(tau)])
                    ++tau;
                chosen = tau;
                break;
            }
        }
        if (chosen < 0) {
            auto best = tauMin_;
            for (int tau = tauMin_; tau <= tauMax_; ++tau)
                if (difference_[static_cast<std::size_t>(tau)] < difference_[static_cast<std::size_t>(best)])
                    best = tau;
            chosen = best;
        }
        const auto depth = difference_[static_cast<std::size_t>(chosen)];
        confidence_ = std::clamp(1.0f - depth, 0.0f, 1.0f);
        if (depth > std::max(0.35f, threshold_ * 2.0f)) {
            frequency_ = 0.0f;
            return;
        }
        // Between samples: the parabola through the dip of the plain
        // difference (the normalised one leans toward longer lags: it reads
        // sharp, more so as the pitch rises).
        auto refined = static_cast<double>(chosen);
        if (chosen > 1 && chosen < tauMax_) {
            const auto a = raw_[static_cast<std::size_t>(chosen - 1)];
            const auto b = raw_[static_cast<std::size_t>(chosen)];
            const auto c = raw_[static_cast<std::size_t>(chosen + 1)];
            const auto bend = a - 2.0f * b + c;
            if (std::abs(bend) > 1.0e-9f)
                refined += std::clamp(0.5 * (a - c) / bend, -1.0, 1.0);
        }
        frequency_ = static_cast<float>(readRate_ / refined);
    }

    double rate_ { 48'000.0 };
    double readRate_ { 12'000.0 };
    int decimation_ { 4 };
    int window_ { 512 };
    int hop_ { 128 };
    int tauMin_ { 8 };
    int tauMax_ { 240 };
    float threshold_ { 0.15f };
    std::vector<float> history_;
    std::vector<float> difference_;
    std::vector<float> raw_;
    std::size_t write_ {};
    int counted_ {};
    int sinceLast_ {};
    int phase_ {};
    Stage stages_[2] {};
    float frequency_ {};
    float confidence_ {};
    float level_ {};
    bool fresh_ {};
};

} // namespace cydaw::engine
