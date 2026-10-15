#pragma once

#include <cydaw/engine/EffectCatalogue.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>

// Utilitaire, Stéréoscope: what their processors (UtilityEffects.cpp) and their displays
// (UtilityPanes.inc) both compute. The enums follow the catalogue's
// parameter order (EffectCatalogue.h); a helper's name never repeats an
// enumerator's (they share this namespace): `xxxOf`, `xxxApplied`…

namespace cydaw::engine::utility {

// Utility ("utilitaire").
enum UtilitaireParameter : int {
    utilitaireGain,
    utilitairePan,
    utilitaireWidth,
    utilitaireMonoBass,
    utilitaireInvertLeft,
    utilitaireInvertRight,
    utilitaireChannelMode,
    utilitaireDcFilter,
    utilitaireMute,
    utilitaireDelay,
    utilitaireParameterCount
};

// Stereoscope ("stereoscope").
enum StereoscopeParameter : int {
    stereoscopeZoom,
    stereoscopePersistence,
    stereoscopeHistory,
    stereoscopeDisplayMode,
    stereoscopeParameterCount
};

// A choice as both languages name it, with a hint each.
struct UtilityChoice {
    const char* french;
    const char* english;
    const char* frenchHint;
    const char* englishHint;
};

// ---- Utilitaire ------------------------------------------------------------------------------

// What reaches the two sides, first of all.
inline constexpr int utilitaireChannelModeCount = 5;
inline constexpr int utilitaireStereoMode = 0;
inline constexpr int utilitaireLeftOnlyMode = 1;
inline constexpr int utilitaireRightOnlyMode = 2;
inline constexpr int utilitaireSwappedMode = 3;
inline constexpr int utilitaireMonoMode = 4;
inline constexpr std::array<UtilityChoice, utilitaireChannelModeCount> utilitaireChannelModes { {
    { "Stéréo", "Stereo", "Les deux côtés tels quels", "Both sides as they are" },
    { "Gauche", "Left", "Le côté gauche seul, des deux côtés", "The left side alone, on both sides" },
    { "Droite", "Right", "Le côté droit seul, des deux côtés", "The right side alone, on both sides" },
    { "Inversées", "Swapped", "La gauche à droite et la droite à gauche", "Left to the right and right to the left" },
    { "Mono", "Mono", "La moyenne des deux côtés, des deux côtés", "The average of both sides, on both sides" },
} };
[[nodiscard]] inline const UtilityChoice& utilitaireChannelModeOf(const float value) noexcept
{
    return utilitaireChannelModes[static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(value)), 0, utilitaireChannelModeCount - 1))];
}

// The two sides after the channel mode.
[[nodiscard]] inline std::array<double, 2> utilitaireChannelsApplied(const int mode, const double left, const double right) noexcept
{
    switch (mode) {
    case utilitaireLeftOnlyMode: return { left, left };
    case utilitaireRightOnlyMode: return { right, right };
    case utilitaireSwappedMode: return { right, left };
    case utilitaireMonoMode: {
        const auto middle = 0.5 * (left + right);
        return { middle, middle };
    }
    default: return { left, right };
    }
}

// Mono bass: at its lowest it is off, and the sound passes untouched.
inline constexpr float utilitaireMonoBassOff = 20.0f;
[[nodiscard]] inline bool utilitaireMonoBassOn(const float hz) noexcept
{
    return hz > utilitaireMonoBassOff + 0.01f;
}

// The DC filter's corner, the longest alignment delay.
inline constexpr double utilitaireDcHz = 5.0;
inline constexpr double utilitaireDelayMostMs = 20.0;

// The balance: the side the pan leaves keeps its level, the other one
// fades on a quarter sine (-3 dB half way, silent at the end).
[[nodiscard]] inline std::array<double, 2> utilitaireBalanceOf(const float pan) noexcept
{
    const auto amount = std::clamp(static_cast<double>(pan), -1.0, 1.0);
    const auto fading = std::sin((1.0 - std::abs(amount)) * std::numbers::pi * 0.5);
    if (amount < 0.0)
        return { 1.0, fading };
    if (amount > 0.0)
        return { fading, 1.0 };
    return { 1.0, 1.0 };
}

// Its readings: each side's output peak (linear, falling).
inline constexpr int utilitaireReadingLeftPeak = 0;
inline constexpr int utilitaireReadingRightPeak = 1;
inline constexpr int utilitaireReadingCount = 2;
// How fast the peaks fall (dB a second).
inline constexpr double utilitairePeakFallDb = 24.0;

// ---- Stéréoscope -----------------------------------------------------------------------------

inline constexpr int stereoscopeDisplayModeCount = 2;
inline constexpr std::array<UtilityChoice, stereoscopeDisplayModeCount> stereoscopeDisplayModes { {
    { "Goniomètre", "Goniometer", "Les échantillons en nuage : le centre vertical, les côtés à l'horizontale",
      "The samples as a cloud: the centre upright, the sides across" },
    { "Polaire", "Polar", "Où va l'énergie : sa répartition d'angle sur un demi-cercle",
      "Where the energy goes: how it spreads over the angles of a half circle" },
} };
[[nodiscard]] inline const UtilityChoice& stereoscopeDisplayModeOf(const float value) noexcept
{
    return stereoscopeDisplayModes[static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(value)), 0, stereoscopeDisplayModeCount - 1))];
}

// Its readings: the correlation now (~50 ms) and averaged (~2 s), the
// balance (dB, positive when the right is louder), the width (side RMS
// over mid RMS), each band's correlation, then the last pairs of samples
// (left, right), oldest first, spread over ~20 ms.
inline constexpr int stereoscopeReadingCorrelation = 0;
inline constexpr int stereoscopeReadingCorrelationAverage = 1;
inline constexpr int stereoscopeReadingBalance = 2;
inline constexpr int stereoscopeReadingWidth = 3;
inline constexpr int stereoscopeReadingLowBand = 4;
inline constexpr int stereoscopeReadingMidBand = 5;
inline constexpr int stereoscopeReadingHighBand = 6;
inline constexpr int stereoscopeReadingPairs = 8;
inline constexpr int stereoscopePairCount = 256;
inline constexpr int stereoscopeReadingCount = stereoscopeReadingPairs + 2 * stereoscopePairCount;

// The windows the readings average over (s), the bands' edges (Hz), the
// span of the pairs (s).
inline constexpr double stereoscopeNowSeconds = 0.05;
inline constexpr double stereoscopeAverageSeconds = 2.0;
inline constexpr double stereoscopeLevelSeconds = 0.3;
inline constexpr double stereoscopeLowHz = 250.0;
inline constexpr double stereoscopeHighHz = 4'000.0;
inline constexpr double stereoscopePairSeconds = 0.02;
// The balance and the width shown at most.
inline constexpr double stereoscopeBalanceMostDb = 60.0;
inline constexpr double stereoscopeWidthMost = 10.0;

// How many samples apart the pairs are taken.
[[nodiscard]] inline int stereoscopePairStrideOf(const double sampleRate) noexcept
{
    return std::max(1, static_cast<int>(std::lround(stereoscopePairSeconds * sampleRate / stereoscopePairCount)));
}

// A correlation from its averaged products: 0 when a side is silent.
[[nodiscard]] inline double stereoscopeCorrelationOf(const double leftRight, const double leftLeft, const double rightRight) noexcept
{
    constexpr auto silent = 1.0e-12;
    if (leftLeft < silent || rightRight < silent)
        return 0.0;
    return std::clamp(leftRight / std::sqrt(leftLeft * rightRight), -1.0, 1.0);
}

// The balance (dB) from each side's averaged power.
[[nodiscard]] inline double stereoscopeBalanceOf(const double leftLeft, const double rightRight) noexcept
{
    constexpr auto silent = 1.0e-12;
    if (leftLeft < silent && rightRight < silent)
        return 0.0;
    return std::clamp(10.0 * std::log10((rightRight + 1.0e-30) / (leftLeft + 1.0e-30)), -stereoscopeBalanceMostDb, stereoscopeBalanceMostDb);
}

// The width from the averaged mid and side powers.
[[nodiscard]] inline double stereoscopeWidthOf(const double midMid, const double sideSide) noexcept
{
    constexpr auto silent = 1.0e-12;
    if (midMid < silent && sideSide < silent)
        return 0.0;
    return std::clamp(std::sqrt(sideSide / std::max(midMid, 1.0e-30)), 0.0, stereoscopeWidthMost);
}

// A pair of samples on the goniometer: the side across (the left to the
// left), the middle up, both a 45° turn of (left, right).
struct StereoscopePoint {
    double side;
    double middle;
};
[[nodiscard]] inline StereoscopePoint stereoscopePointOf(const double left, const double right) noexcept
{
    constexpr auto half = std::numbers::sqrt2 * 0.5;
    return { (right - left) * half, (left + right) * half };
}

// Its angle on the polar half circle: 0 all to the right of the side axis,
// π/2 the centre, π all to the left; what points down is turned up (a
// polarity changes nothing to where a sound sits).
[[nodiscard]] inline double stereoscopeAngleOf(const StereoscopePoint point) noexcept
{
    auto side = point.side;
    auto middle = point.middle;
    if (middle < 0.0) {
        side = -side;
        middle = -middle;
    }
    return std::atan2(middle, side);
}

// The goniometer's trail: how long a point stays lit (s) for a persistence
// 0..1, how much of its light is left at an age (s), and how many sets of
// pairs it keeps at most (one a screen frame, ~60 a second).
inline constexpr int stereoscopeTrailMost = 32;
[[nodiscard]] inline double stereoscopeTrailSecondsOf(const float persistence) noexcept
{
    return 0.03 + 0.5 * std::clamp(static_cast<double>(persistence), 0.0, 1.0);
}
[[nodiscard]] inline float stereoscopeFadeOf(const double age, const double window) noexcept
{
    if (age >= window || window <= 0.0)
        return 0.0f;
    const auto left = static_cast<float>(1.0 - std::max(0.0, age) / window);
    return left * left;
}
// The polar view's angle steps over the half circle.
inline constexpr int stereoscopeAngleBins = 48;
// The correlation history's samples a second.
inline constexpr double stereoscopeHistoryRate = 20.0;

} // namespace cydaw::engine::utility
