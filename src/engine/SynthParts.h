#pragma once

// The pieces the synth family's engines share: oscillators without aliasing
// (polyBLEP), LFO shapes, an envelope that curves as a capacitor charges, the
// four filters of character (a ladder, a cascade, a state-variable one, a
// biting one). Internal to the engine.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace cydaw::engine::synth::detail::parts {

inline constexpr double kPi = std::numbers::pi;

inline double polyBlep(double t, const double dt) noexcept
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt) {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}

inline double oscillator(const int wave, const double t, const double dt, const double width) noexcept
{
    switch (wave) {
    case 0: return 2.0 * t - 1.0 - polyBlep(t, dt);
    case 1: {
        auto value = t < width ? 1.0 : -1.0;
        value += polyBlep(t, dt);
        auto shifted = t - width;
        if (shifted < 0.0)
            shifted += 1.0;
        value -= polyBlep(shifted, dt);
        // Its average kept at nothing whatever the width.
        return value - (2.0 * width - 1.0);
    }
    case 2: return 4.0 * std::abs(t - 0.5) - 1.0;
    default: return std::sin(2.0 * kPi * t);
    }
}

inline double lfoWave(const int shape, const double phase, const double held) noexcept
{
    switch (shape) {
    case 0: return std::sin(2.0 * kPi * phase);
    case 1: return 1.0 - 4.0 * std::abs(phase - 0.5);
    case 2: return 1.0 - 2.0 * phase;
    case 3: return phase < 0.5 ? 1.0 : -1.0;
    default: return held;
    }
}

// An envelope that curves as a capacitor charges: an attack aimed past the
// top, a decay and a release that fall exponentially.
struct Envelope {
    enum class Stage : std::uint8_t { idle, attack, decay, sustain, release };
    Stage stage { Stage::idle };
    double level {};
    void start() noexcept { stage = Stage::attack; }
    void release() noexcept
    {
        if (stage != Stage::idle)
            stage = Stage::release;
    }
    // One step of `samples`, its coefficients for that many.
    double step(const double attack, const double decay, const double sustain, const double release) noexcept
    {
        switch (stage) {
        case Stage::idle: level = 0.0; break;
        case Stage::attack:
            level = 1.3 + (level - 1.3) * attack;
            if (level >= 1.0) {
                level = 1.0;
                stage = Stage::decay;
            }
            break;
        case Stage::decay:
        case Stage::sustain:
            level = sustain + (level - sustain) * decay;
            if (stage == Stage::decay && std::abs(level - sustain) < 1.0e-4)
                stage = Stage::sustain;
            break;
        case Stage::release:
            level *= release;
            if (level < 1.0e-5) {
                level = 0.0;
                stage = Stage::idle;
            }
            break;
        }
        return level;
    }
};

// The four filters, one channel's state.
struct Filter {
    std::array<double, 4> s {};
    double low {};
    double band {};
    double last {};
    void reset() noexcept
    {
        s = {};
        low = band = last = 0.0;
    }

    // A ladder solved for its own feedback (no delay in the loop), its input
    // saturated; g = tan(pi fc / fs), resonance 0..1.
    double ladder(const double x, const double g, const double resonance) noexcept
    {
        const auto k = 4.2 * resonance;
        const auto G = g / (1.0 + g);
        const auto G2 = G * G;
        const auto G3 = G2 * G;
        const auto beta = 1.0 / (1.0 + g);
        const auto feedback = G3 * s[0] * beta + G2 * s[1] * beta + G * s[2] * beta + s[3] * beta;
        auto u = (x - k * feedback) / (1.0 + k * G3 * G);
        u = std::tanh(u);
        auto y = u;
        for (auto& state : s) {
            const auto v = (y - state) * G;
            const auto out = v + state;
            state = out + v;
            y = out;
        }
        // The lows a ladder loses as it resonates, given back in part.
        return y * (1.0 + 0.6 * k);
    }

    // Four poles, the feedback a sample late and saturated: rounder, its
    // resonance singing sooner.
    double cascade(const double x, const double g, const double resonance) noexcept
    {
        const auto k = 4.6 * resonance;
        const auto G = g / (1.0 + g);
        auto y = std::tanh(x - k * std::tanh(last));
        for (auto& state : s) {
            const auto v = (y - state) * G;
            const auto out = v + state;
            state = out + v;
            y = out;
        }
        last = y;
        return y * (1.0 + 0.35 * k);
    }

    // A state-variable filter (two poles): its low, band or high output;
    // biting, its band state saturated as it resonates.
    double variable(const double x, const double g, const double resonance, const int mode, const bool biting) noexcept
    {
        const auto damping = std::max(0.02, 2.0 - 2.0 * resonance * 0.99);
        const auto a1 = 1.0 / (1.0 + g * (g + damping));
        const auto a2 = g * a1;
        const auto a3 = g * a2;
        const auto v3 = x - low;
        auto v1 = a1 * band + a2 * v3;
        const auto v2 = low + a2 * band + a3 * v3;
        if (biting)
            v1 = std::tanh(v1 * (1.0 + 2.0 * resonance)) / (1.0 + resonance);
        band = 2.0 * v1 - band;
        low = 2.0 * v2 - low;
        if (mode == 1)
            return v1;
        if (mode == 2)
            return x - damping * v1 - v2;
        return v2;
    }
};

// While set, the seeds of the voices this thread makes, in turn from this
// one: a patch search (SynthMatch) hears every candidate with the same
// phases and the same noise, two hearings of one candidate alike.
inline thread_local std::uint32_t* fixedSeeds = nullptr;

// A seed for each voice made: their drifts and phases apart.
inline std::uint32_t nextSeed() noexcept
{
    if (fixedSeeds != nullptr)
        return *fixedSeeds += 0x9e3779b9u;
    static std::atomic<std::uint32_t> counter { 0x1f3a5c79u };
    return counter.fetch_add(0x9e3779b9u, std::memory_order_relaxed);
}

} // namespace cydaw::engine::synth::detail::parts
