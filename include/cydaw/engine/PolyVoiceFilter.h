#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace cydaw::engine {

// The low-pass a voice runs after its oscillator: a two-pole state-variable
// filter whose resonance is its Q — the response the voice-filter card
// draws, and the one the graph editor's preview plays. The track's synth
// ran a one-pole without resonance, so a patch previewed resonant played
// flat on its track, and the card's resonance knob did nothing there. One
// filter for both, so what is heard is what is drawn, wherever it is heard.
class PolyVoiceFilter final {
public:
    void reset() noexcept
    {
        integrator1_ = 0.0;
        integrator2_ = 0.0;
    }

    // Once per block: cutoff in hertz, resonance as Q, at the sample rate.
    void setCoefficients(const double cutoffHz, const double resonance,
                         const double sampleRate) noexcept
    {
        const auto rate = std::max(1.0, sampleRate);
        const auto cutoff = std::clamp(cutoffHz, 20.0, rate * 0.45);
        const auto q = std::clamp(resonance, 0.1, 20.0);
        const auto g = std::tan(std::numbers::pi * cutoff / rate);
        const auto damping = 1.0 / q;
        a1_ = 1.0 / (1.0 + g * (g + damping));
        a2_ = g * a1_;
        a3_ = g * a2_;
    }

    [[nodiscard]] double process(const double input) noexcept
    {
        const auto v3 = input - integrator2_;
        const auto v1 = a1_ * integrator1_ + a2_ * v3;
        const auto v2 = integrator2_ + a2_ * integrator1_ + a3_ * v3;
        integrator1_ = 2.0 * v1 - integrator1_;
        integrator2_ = 2.0 * v2 - integrator2_;
        return v2;
    }

private:
    double a1_ { 1.0 };
    double a2_ {};
    double a3_ {};
    double integrator1_ {};
    double integrator2_ {};
};

} // namespace cydaw::engine
