#pragma once

// The filter of seven kinds the synth family's bigger models share (Colosse,
// Mosaïque): a ladder, a low-pass, a band-pass, a high-pass, a comb tuned to
// its cutoff, a vowel (three formants from A to U), a scream (a band driven
// into its own resonance). One channel's state; its coefficients made once a
// control step. Internal to the engine.

#include "SynthParts.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace cydaw::engine::synth::detail {

class MultiFilter {
public:
    enum Kind : int { ladder, lowPass, bandPass, highPass, comb, vowel, scream, kindCount };

    struct Coefficients {
        double g {};
        double resonance {};
        double damping { 2.0 };
        double delay {};    // the comb's, in samples
        double feedback {}; // the comb's
        std::array<double, 3> vowelG {};
        double vowelDamping { 0.2 };
    };

    // The comb's delay: its lowest note (40 Hz) at this rate. Off the audio thread.
    void prepare(const double rate)
    {
        comb_.assign(static_cast<std::size_t>(std::ceil(std::max(1.0, rate) / 40.0)) + 4, 0.0f);
        write_ = 0;
    }

    void clear() noexcept
    {
        main_.reset();
        for (auto& formant : vowel_)
            formant.reset();
        std::fill(comb_.begin(), comb_.end(), 0.0f);
        combLow_ = 0.0;
    }

    [[nodiscard]] std::size_t combSize() const noexcept { return comb_.size(); }

    [[nodiscard]] static Coefficients coefficients(const int kind, const double cutoff, const double resonance, const double rate,
                                                   const std::size_t combSize) noexcept
    {
        Coefficients result;
        result.g = std::tan(parts::kPi * cutoff / rate);
        result.resonance = resonance;
        result.damping = std::max(0.02, 2.0 - 1.98 * resonance);
        if (kind == comb) {
            result.delay = std::clamp(rate / std::max(40.0, cutoff), 1.0, static_cast<double>(std::max<std::size_t>(4, combSize) - 2));
            result.feedback = 0.97 * resonance;
        }
        if (kind == vowel) {
            // The cutoff moves across the vowels, A to U.
            static constexpr std::array<std::array<double, 3>, 5> vowels { { { 800.0, 1'150.0, 2'900.0 },
                                                                            { 400.0, 1'900.0, 2'700.0 },
                                                                            { 280.0, 2'200.0, 2'950.0 },
                                                                            { 450.0, 800.0, 2'830.0 },
                                                                            { 325.0, 700.0, 2'530.0 } } };
            const auto t = std::clamp(std::log(cutoff / 20.0) / std::log(1'000.0), 0.0, 1.0) * 4.0;
            const auto lower = std::min(static_cast<int>(t), 3);
            const auto mix = t - static_cast<double>(lower);
            for (std::size_t at = 0; at < 3; ++at) {
                const auto hz = vowels[static_cast<std::size_t>(lower)][at]
                                + (vowels[static_cast<std::size_t>(lower + 1)][at] - vowels[static_cast<std::size_t>(lower)][at]) * mix;
                result.vowelG[at] = std::tan(parts::kPi * std::min(hz, rate * 0.45) / rate);
            }
            result.vowelDamping = std::max(0.04, 0.5 - 0.44 * resonance);
        }
        return result;
    }

    double process(const int kind, const Coefficients& c, const double x) noexcept
    {
        switch (kind) {
        case ladder: return main_.ladder(x, c.g, c.resonance);
        case lowPass: return main_.variable(x, c.g, c.resonance, 0, false);
        case bandPass: return main_.variable(x, c.g, c.resonance, 1, false) * c.damping;
        case highPass: return main_.variable(x, c.g, c.resonance, 2, false);
        case comb: {
            // A comb tuned to the cutoff: its feedback rings at it and its
            // harmonics, darkened a little each time round.
            const auto size = comb_.size();
            if (size < 4)
                return x;
            auto readAt = static_cast<double>(write_) - c.delay;
            while (readAt < 0.0)
                readAt += static_cast<double>(size);
            const auto lower = static_cast<std::size_t>(readAt) % size;
            const auto fraction = readAt - std::floor(readAt);
            const auto delayed = static_cast<double>(comb_[lower]) * (1.0 - fraction) + static_cast<double>(comb_[(lower + 1) % size]) * fraction;
            combLow_ += (delayed - combLow_) * 0.6;
            const auto y = std::tanh(x + c.feedback * combLow_);
            comb_[write_] = static_cast<float>(y);
            write_ = (write_ + 1) % size;
            return y * (1.0 - 0.6 * c.feedback);
        }
        case vowel: {
            auto sum = 0.0;
            static constexpr std::array<double, 3> gains { 1.0, 0.7, 0.35 };
            for (std::size_t at = 0; at < 3; ++at)
                sum += gains[at] * vowel_[at].variable(x, c.vowelG[at], 1.0 - c.vowelDamping * 0.5, 1, false) * c.vowelDamping;
            return sum * 1.6;
        }
        default: {
            // Screaming: a band driven into its own resonance.
            const auto driven = std::tanh(x * (1.0 + 2.0 * c.resonance));
            return main_.variable(driven, c.g, std::min(0.98, 0.55 + 0.45 * c.resonance), 1, true) * (0.5 + c.resonance);
        }
        }
    }

private:
    parts::Filter main_;
    std::array<parts::Filter, 3> vowel_ {};
    std::vector<float> comb_;
    std::size_t write_ {};
    double combLow_ {};
};

} // namespace cydaw::engine::synth::detail
