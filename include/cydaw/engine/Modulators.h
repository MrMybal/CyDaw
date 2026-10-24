#pragma once

#include <array>
#include <cmath>
#include <cstdint>

// Modulators, as Bitwig has them on every device: small sources (an LFO, an
// envelope, steps, a macro…) that move a device's knobs around where they
// are. Each is a control node of the track's graph (NativeDspNodeKind::modLfo
// and the others, entries of the effect catalogue with `control` set), its
// settings in the payload's effect1..8 (modulatable in turn) and, for the
// steps and the card it belongs to, in eqBands slots. The runtime evaluates
// them once a block; their output, -1..1 or 0..1, reaches a knob through a
// modulation cable whose amount is the knob's ring.

namespace cydaw::engine::modulator {

// eqBands slots.
inline constexpr int stepSlot = 0;          // 16 steps, 0..1
inline constexpr int stepCount = 16;
// The card a modulator belongs to (its node id, in two 24-bit halves: a
// float holds 24 bits exactly). 0: none, a modulator on its own.
inline constexpr int ownerLowSlot = 16;
inline constexpr int ownerHighSlot = 17;
inline constexpr float ownerHalfRange = 16'777'215.0f;

[[nodiscard]] constexpr std::uint64_t ownerOf(const float low, const float high) noexcept
{
    // Each half a whole number of 24 bits: anything else (negative, too
    // large, not a number: a corrupt state) is no owner, not a cast that
    // the language leaves undefined.
    const auto half = [](const float value) -> std::uint64_t {
        return value >= 0.0f && value <= ownerHalfRange ? static_cast<std::uint64_t>(value) : 0;
    };
    return (half(high) << 24) | half(low);
}
[[nodiscard]] constexpr float ownerLowOf(const std::uint64_t owner) noexcept
{
    return static_cast<float>(owner & 0xffffffu);
}
[[nodiscard]] constexpr float ownerHighOf(const std::uint64_t owner) noexcept
{
    return static_cast<float>((owner >> 24) & 0xffffffu);
}

// The tempo divisions a synced modulator runs at (0: free, in Hz), in
// quarter notes per cycle.
inline constexpr int divisionCount = 14;
[[nodiscard]] constexpr double divisionBeats(const int division) noexcept
{
    constexpr double beats[divisionCount] { 0.0, 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125,
                                            2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0, 1.5, 0.75 };
    return division > 0 && division < divisionCount ? beats[division] : 0.0;
}
[[nodiscard]] constexpr const char* divisionName(const int division) noexcept
{
    constexpr const char* names[divisionCount] { "Free", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32",
                                                 "1/4 T", "1/8 T", "1/16 T", "1/4 .", "1/8 ." };
    return division >= 0 && division < divisionCount ? names[division] : "?";
}

// LFO shapes.
enum class Shape : std::uint8_t { sine, triangle, sawUp, sawDown, square, random, smoothRandom };
inline constexpr int shapeCount = 7;
[[nodiscard]] constexpr const char* shapeName(const int shape) noexcept
{
    constexpr const char* names[shapeCount] { "Sine", "Triangle", "Saw up", "Saw down", "Square", "Random", "Smooth random" };
    return shape >= 0 && shape < shapeCount ? names[shape] : "?";
}

// What a note modulator follows.
enum class NoteSource : std::uint8_t { key, velocity, gate };
// What an expression modulator follows.
enum class ExpressionSource : std::uint8_t { modWheel, aftertouch, pitchBend, controller };
// How a math modulator combines its inputs.
enum class MathMode : std::uint8_t { mix, add, multiply, minimum, maximum, difference, average };
inline constexpr int mathModeCount = 7;
[[nodiscard]] constexpr const char* mathModeName(const int mode) noexcept
{
    constexpr const char* names[mathModeCount] { "Mix", "Add", "Multiply", "Minimum", "Maximum", "Difference", "Average" };
    return mode >= 0 && mode < mathModeCount ? names[mode] : "?";
}

// What the notes and the controllers of the track say now, handed to the
// runtime every block by whoever plays its notes (an instrument track, an
// exported plug-in). A track without notes leaves it at rest.
struct ControlNotes {
    // Every note-on so far (a modulator retriggers when it moves).
    std::uint32_t noteOns {};
    // Keys down now.
    int held {};
    // The last note pressed and its velocity (0..1).
    float note { 60.0f };
    float velocity {};
    float modWheel {};
    float aftertouch {};
    // -1..1.
    float pitchBend {};
    // Every controller, 0..1.
    std::array<float, 128> controllers {};
};

// One cycle's shape at a phase 0..1, -1..1. `held` and `next` are the random
// shapes' values (this cycle's, the next one's).
[[nodiscard]] inline float shapeAt(const Shape shape, const double phase, const float held, const float next) noexcept
{
    constexpr double twoPi = 6.283185307179586;
    const auto p = phase - std::floor(phase);
    switch (shape) {
    case Shape::sine: return static_cast<float>(std::sin(twoPi * p));
    case Shape::triangle: return static_cast<float>(p < 0.25 ? 4.0 * p : p < 0.75 ? 2.0 - 4.0 * p : 4.0 * p - 4.0);
    case Shape::sawUp: return static_cast<float>(2.0 * p - 1.0);
    case Shape::sawDown: return static_cast<float>(1.0 - 2.0 * p);
    case Shape::square: return p < 0.5 ? 1.0f : -1.0f;
    case Shape::random: return held;
    case Shape::smoothRandom: {
        // Cosine between this cycle's value and the next one's.
        const auto t = static_cast<float>(0.5 - 0.5 * std::cos(3.141592653589793 * p));
        return held + (next - held) * t;
    }
    }
    return 0.0f;
}

} // namespace cydaw::engine::modulator
