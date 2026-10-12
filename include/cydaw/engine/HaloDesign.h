#pragma once

#include <algorithm>
#include <array>
#include <cmath>

// Halo, a reverb of characters: ten algorithms (halls, plate, rooms, spaces
// that drift or chorus, a sanctuary, a gated one) in three colours (the 1970s'
// dark machines, the 1980s' grain, today's clarity). Written once for its
// processor (HaloReverb.cpp) and for the interface that draws it.

namespace cydaw::engine::halo {

enum Halo : int {
    // The sockets.
    haloMix,
    haloDecay,
    haloPredelay,
    haloSize,
    haloHighCut,
    haloLowCut,
    haloModDepth,
    haloModRate,
    // The slots.
    haloMode,
    haloColour,
    haloAttack,
    haloEarlyDiffusion,
    haloLateDiffusion,
    haloBassMultiplier,
    haloBassCrossover,
    haloDamping,
    haloSync,
    haloWidth,
    haloParameterCount
};

// What an algorithm is made of.
struct HaloMode {
    const char* french;
    const char* english;
    const char* frenchHint;
    const char* englishHint;
    // Its network's lines against the base lengths, its early reflections'
    // level, how quick its density builds (0 at once, 1 slow), how much its
    // lines sway and whether they drift at random, its top's darkness
    // (a factor on the damping), its own brightness of the reflections.
    double scale;
    double early;
    double buildUp;
    double sway;
    bool random;
    double darkness;
    // Gated: the tail held for a while, then shut.
    bool gated;
};

inline constexpr int haloModeCount = 10;
inline constexpr std::array<HaloMode, haloModeCount> haloModes { {
    { "Salle de concert", "Concert hall", "Une grande salle : ample, chaude, qui se construit doucement",
      "A large hall: wide, warm, building gently", 1.25, 0.35, 0.45, 0.35, false, 1.1, false },
    { "Salle brillante", "Bright hall", "La salle avec plus d'air et des aigus qui durent", "The hall with more air and highs that last", 1.2,
      0.3, 0.4, 0.3, false, 0.6, false },
    { "Plaque", "Plate", "Une plaque : dense tout de suite, brillante, sans premières réflexions",
      "A plate: dense at once, bright, no early reflections", 0.8, 0.0, 0.05, 0.25, false, 0.7, false },
    { "Pièce", "Room", "Une pièce : courte, ses premières réflexions en avant", "A room: short, its early reflections forward", 0.55, 0.8,
      0.15, 0.15, false, 1.0, false },
    { "Chambre", "Chamber", "Une chambre d'écho : moyenne, ronde, réfléchissante", "An echo chamber: medium, round, reflective", 0.75,
      0.6, 0.25, 0.2, false, 1.2, false },
    { "Espace aléatoire", "Random space", "Un espace qui dérive : ses échos bougent sans cesse, sans métallique",
      "A drifting space: its echoes keep moving, never metallic", 1.1, 0.25, 0.4, 0.6, true, 0.9, false },
    { "Espace chorus", "Chorus space", "Un espace qui ondule comme un chorus : large et mouvant", "A space that sways like a chorus: wide and moving",
      1.0, 0.2, 0.35, 1.0, false, 0.85, false },
    { "Ambiance", "Ambience", "Juste l'air autour : des réflexions, presque pas de queue", "Just the air around: reflections, hardly a tail",
      0.45, 1.0, 0.05, 0.1, false, 1.0, false },
    { "Sanctuaire", "Sanctuary", "Immense et sombre, qui monte lentement", "Huge and dark, rising slowly", 1.6, 0.15, 0.85, 0.4, true, 1.6,
      false },
    { "Non linéaire", "Nonlinear", "Une réverbe coupée net : dense pendant son temps, puis plus rien (les batteries des années 80)",
      "A gated reverb: dense for its time, then nothing (80s drums)", 0.7, 0.4, 0.05, 0.15, false, 0.8, true },
} };

[[nodiscard]] inline const HaloMode& haloModeOf(const float value) noexcept
{
    return haloModes[static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(value)), 0, haloModeCount - 1))];
}

// The three colours: the band the machine keeps, its grain.
struct HaloColour {
    const char* french;
    const char* english;
    double topHz;      // the wet sound's top (Hz)
    double bits;       // its feedback's resolution (0: none lost)
    double extraSway;  // a factor on the modulation
    double drive;      // its input pushed a little (0: clean)
};
inline constexpr int haloColourCount = 3;
inline constexpr std::array<HaloColour, haloColourCount> haloColours { {
    { "Années 70", "1970s", 8'000.0, 0.0, 1.5, 0.6 },
    { "Années 80", "1980s", 13'000.0, 12.0, 1.2, 0.2 },
    { "Maintenant", "Now", 20'000.0, 0.0, 1.0, 0.0 },
} };
[[nodiscard]] inline const HaloColour& haloColourOf(const float value) noexcept
{
    return haloColours[static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(value)), 0, haloColourCount - 1))];
}

// The network: eight lines (ms at size 1), prime-ish so they never line up.
inline constexpr int haloLines = 8;
inline constexpr std::array<double, haloLines> haloLineMs { 31.3, 37.9, 43.7, 50.3, 57.1, 64.9, 71.3, 79.7 };
// The early reflections: when (ms at size 1) and how loud, their signs alternating.
inline constexpr std::array<std::pair<double, double>, 6> haloEarlyTaps { { { 7.1, 0.8 }, { 11.9, -0.65 }, { 17.3, 0.55 },
                                                                             { 23.7, -0.45 }, { 31.1, 0.38 }, { 41.3, -0.3 } } };

// The lines' scale for the size knob (0..1) and the mode.
[[nodiscard]] inline double haloScale(const float* values) noexcept
{
    return haloModeOf(values[haloMode]).scale * (0.35 + 1.3 * std::clamp(static_cast<double>(values[haloSize]), 0.0, 1.0));
}

// The tail's level (dB) at a time (s) after the sound, its lows (`low`) or
// its highs: what the display draws. The highs die sooner as the damping is
// low and the mode dark; the lows by the bass multiplier.
[[nodiscard]] inline double haloTailDb(const float* values, const double seconds, const bool low) noexcept
{
    const auto& mode = haloModeOf(values[haloMode]);
    auto decay = std::max(0.1, static_cast<double>(values[haloDecay]));
    if (low)
        decay *= std::clamp(static_cast<double>(values[haloBassMultiplier]), 0.25, 4.0);
    else
        decay *= std::clamp(static_cast<double>(values[haloDamping]) / 12'000.0, 0.15, 1.5) / mode.darkness;
    const auto predelay = static_cast<double>(values[haloPredelay]) * 0.001;
    const auto t = seconds - predelay;
    if (t < 0.0)
        return -120.0;
    // Density builds over the attack.
    const auto build = (0.005 + 0.25 * mode.buildUp * (0.3 + std::clamp(static_cast<double>(values[haloAttack]), 0.0, 1.0)));
    const auto rise = 20.0 * std::log10(std::max(1.0e-6, 1.0 - std::exp(-t / build)));
    auto level = rise - 60.0 * t / decay;
    if (mode.gated && t > decay * 0.6)
        level -= (t - decay * 0.6) * 600.0;
    return std::max(-120.0, level);
}

} // namespace cydaw::engine::halo
