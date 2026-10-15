#pragma once

#include <cydaw/engine/EffectCatalogue.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

// Empreinte, Givre: what their processors (SpaceEffects.cpp) and their displays
// (SpacePanes.inc) both compute. The enums follow the catalogue's
// parameter order (EffectCatalogue.h); a helper's name never repeats an
// enumerator's (they share this namespace): `xxxOf`, `xxxApplied`…

namespace cydaw::engine::spacefx {

// Empreinte ("empreinte").
enum EmpreinteParameter : int {
    empreinteMix,
    empreintePredelay,
    empreinteLowCut,
    empreinteHighCut,
    empreinteWidth,
    empreinteEarly,
    empreinteTail,
    empreinteOutput,
    empreinteSpace,
    empreinteLength,
    empreinteReverse,
    empreinteParameterCount
};

// Givre ("givre").
enum GivreParameter : int {
    givreBlur,
    givreSmear,
    givreShift,
    givreTone,
    givreScatter,
    givreMix,
    givreOutput,
    givreFreeze,
    givreSync,
    givreParameterCount
};

// ---- Empreinte ---------------------------------------------------------------------------------

// A convolution reverb: the response of a file, or of one of ten spaces it
// synthesises itself. What a built-in space is made of.
struct EmpreinteRoom {
    const char* french;
    const char* english;
    const char* frenchHint;
    const char* englishHint;
    // The decay (to -60 dB, s) of its middle frequencies, and of its lows
    // and highs against it.
    double seconds;
    double lowDecay;
    double highDecay;
    // The room its early reflections come from (m); none when 0.
    double width;
    double depth;
    double height;
    // Its early reflections' energy against the late field's first 150 ms.
    double early;
    // The late field: how long it takes to rise (s), and until its echoes
    // are dense (s; 0: dense at once).
    double onset;
    double mixing;
    // Its band (Hz).
    double bottomHz;
    double topHz;
    // Flutter echoes between parallel walls: their period (ms), 0 for none.
    double flutterMs;
    // Discrete echoes off far walls, how many.
    int echoes;
    // A tank of springs rather than air.
    bool spring;
};

inline constexpr int empreinteSpaceCount = 10;
inline constexpr std::array<EmpreinteRoom, empreinteSpaceCount> empreinteRooms { {
    { "Petite pièce", "Small room", "Une petite pièce : courte, ses premières réflexions serrées",
      "A small room: short, its early reflections close together", 0.5, 1.25, 0.6, 4.2, 3.4, 2.6, 1.4, 0.006, 0.03, 40.0,
      12'000.0, 0.0, 0, false },
    { "Studio", "Studio", "La salle de prise d'un studio : vivante sans traîner", "A studio's live room: alive without lingering", 0.8,
      1.2, 0.65, 7.5, 5.8, 3.4, 1.1, 0.01, 0.05, 35.0, 14'000.0, 0.0, 0, false },
    { "Salle", "Hall", "Une salle de concert : ample, chaude, qui se construit", "A concert hall: wide, warm, building up", 2.2, 1.35,
      0.55, 32.0, 20.0, 13.0, 0.6, 0.03, 0.12, 30.0, 11'000.0, 0.0, 0, false },
    { "Grande salle", "Large hall", "Une grande salle : longue, sombre en haut, profonde", "A large hall: long, dark on top, deep", 3.5,
      1.4, 0.5, 45.0, 28.0, 17.0, 0.5, 0.045, 0.15, 28.0, 10'000.0, 0.0, 0, false },
    { "Cathédrale", "Cathedral", "Une cathédrale : immense, des graves sans fin", "A cathedral: huge, its lows endless", 6.0, 1.45, 0.4,
      75.0, 32.0, 28.0, 0.4, 0.07, 0.2, 25.0, 8'000.0, 0.0, 0, false },
    { "Plaque", "Plate", "Une plaque : dense tout de suite, brillante, des graves qui ne traînent pas",
      "A plate: dense at once, bright, its lows not lingering", 2.5, 0.8, 0.9, 0.0, 0.0, 0.0, 0.0, 0.002, 0.0, 80.0, 16'000.0, 0.0, 0,
      false },
    { "Ressort", "Spring", "Un ressort : des échos qui gazouillent et se dispersent", "A spring: echoes that chirp and spread", 2.0,
      0.7, 0.6, 0.0, 0.0, 0.0, 0.0, 0.001, 0.0, 90.0, 5'000.0, 0.0, 0, true },
    { "Chambre d'écho", "Echo chamber", "Une chambre d'écho : murs durs, ronde et dense", "An echo chamber: hard walls, round and dense",
      1.6, 1.15, 0.7, 6.5, 4.6, 3.6, 1.2, 0.008, 0.04, 40.0, 9'000.0, 0.0, 0, false },
    { "Tunnel", "Tunnel", "Un tunnel : des échos qui flottent entre ses parois", "A tunnel: echoes fluttering between its walls", 3.0,
      1.4, 0.45, 6.0, 120.0, 5.0, 0.9, 0.025, 0.1, 30.0, 7'000.0, 35.0, 0, false },
    { "Grotte", "Cave", "Une grotte : sombre, des échos qui reviennent de loin", "A cave: dark, echoes coming back from afar", 5.0, 1.5,
      0.3, 40.0, 22.0, 14.0, 0.6, 0.06, 0.25, 25.0, 3'500.0, 0.0, 9, false },
} };

[[nodiscard]] inline int empreinteSpaceIndexOf(const float value) noexcept
{
    return std::clamp(static_cast<int>(std::lround(value)), 0, empreinteSpaceCount - 1);
}
[[nodiscard]] inline const EmpreinteRoom& empreinteRoomOf(const float value) noexcept
{
    return empreinteRooms[static_cast<std::size_t>(empreinteSpaceIndexOf(value))];
}

// The longest response it plays (s), at the processor's rate.
inline constexpr double empreinteLongestSeconds = 8.0;
// Where the early part ends and the tail begins (s), and how long the one
// fades into the other around it.
inline constexpr double empreinteEarlySeconds = 0.08;
inline constexpr double empreinteEarlyCrossing = 0.01;
// The share of the response kept (empreinteLength) fades out over its last tenth.
inline constexpr double empreinteFadeShare = 0.1;
// How long an old response takes to fade into a new one (s).
inline constexpr double empreinteSwapSeconds = 0.05;

// A built-in space's whole response (s).
[[nodiscard]] inline double empreinteFullSecondsOf(const EmpreinteRoom& room) noexcept
{
    return std::min(empreinteLongestSeconds, room.seconds * 1.2);
}
// The share of a response kept.
[[nodiscard]] inline double empreinteShareOf(const float length) noexcept
{
    return std::clamp(static_cast<double>(length), 0.1, 1.0);
}

// A space's decay (s) at a frequency: its lows' decay below 150 Hz, its
// highs' above 10 kHz, between them along the octaves.
[[nodiscard]] inline double empreinteDecayAt(const EmpreinteRoom& room, const double hz) noexcept
{
    const auto octaves = std::log2(std::max(hz, 1.0) / 1'000.0);
    auto multiplier = 1.0;
    if (octaves < 0.0)
        multiplier = 1.0 + (room.lowDecay - 1.0) * std::clamp(-octaves / std::log2(1'000.0 / 150.0), 0.0, 1.0);
    else
        multiplier = 1.0 + (room.highDecay - 1.0) * std::clamp(octaves / std::log2(10.0), 0.0, 1.0);
    return room.seconds * multiplier;
}

// The cuts on the wet sound (12 dB an octave): off at their ends.
[[nodiscard]] inline bool empreinteLowCutApplied(const float hz) noexcept
{
    return hz > 10.5f;
}
[[nodiscard]] inline bool empreinteHighCutApplied(const float hz) noexcept
{
    return hz < 19'900.0f;
}

// A built-in space's level (dB, 0 at its loudest) at a time into its whole
// response, as its parts make it: for a display that has no readings.
[[nodiscard]] inline double empreinteRoomEnvelopeDb(const EmpreinteRoom& room, const double seconds) noexcept
{
    if (seconds < 0.0)
        return -120.0;
    const auto rise = 1.0 - std::exp(-seconds / std::max(0.001, room.onset));
    const auto decay = -60.0 * seconds / std::max(0.05, room.seconds);
    auto db = decay + 20.0 * std::log10(std::max(1.0e-6, rise));
    if (room.early > 0.0 && seconds < 0.1)
        db = std::max(db, -3.0 - 30.0 * seconds);
    return std::max(-120.0, db);
}

// What the processor shows (readings): the response's length (s), whether
// a new one is being made (1), whether it is a file's (1), then its envelope
// over its length in 128 points (dB, 0 at its loudest).
inline constexpr int empreinteEnvelopePoints = 128;
inline constexpr int empreinteReadingSeconds = 0;
inline constexpr int empreinteReadingBuilding = 1;
inline constexpr int empreinteReadingFile = 2;
inline constexpr int empreinteReadingEnvelope = 3;
inline constexpr int empreinteReadingCount = empreinteReadingEnvelope + empreinteEnvelopePoints;
inline constexpr float empreinteEnvelopeFloorDb = -90.0f;

// ---- Givre ---------------------------------------------------------------------------------------

// A spectral freeze and blur: a short-time Fourier transform of 2048
// samples every 512, as late as one frame.
inline constexpr int givreFrameSize = 2048;
inline constexpr int givreHop = 512;
inline constexpr int givreLatency = givreFrameSize;
// The freeze crossfades in and out over this long (s).
inline constexpr double givreFadeSeconds = 0.1;
// Frames the freeze averages when it captures.
inline constexpr int givreCaptureFrames = 4;

// How long the magnitudes take to follow (s): none at 0, about 3 s at 1.
[[nodiscard]] inline double givreBlurSecondsOf(const float blur) noexcept
{
    const auto b = std::clamp(static_cast<double>(blur), 0.0, 1.0);
    return 3.0 * b * b;
}
// How far a bin's power spreads either side (octaves): up to a third.
[[nodiscard]] inline double givreSmearOctavesOf(const float smear) noexcept
{
    return std::clamp(static_cast<double>(smear), 0.0, 1.0) / 3.0;
}
// The shift as a ratio of frequencies.
[[nodiscard]] inline double givreShiftRatioOf(const float semitones) noexcept
{
    return std::pow(2.0, std::clamp(static_cast<double>(semitones), -12.0, 12.0) / 12.0);
}
// The tone's tilt (dB) at a frequency: ±12 dB at 20 Hz and 20 kHz, none
// at their middle (632 Hz).
[[nodiscard]] inline double givreToneDbOf(const float tone, const double hz) noexcept
{
    const auto t = std::clamp(static_cast<double>(tone), -1.0, 1.0);
    const auto position = std::log(std::clamp(hz, 20.0, 20'000.0) / 632.455532) / std::log(31.6227766);
    return 12.0 * t * std::clamp(position, -1.0, 1.0);
}

// What the processor shows: frozen (1) or not, then its output's spectrum
// in 64 bands spaced evenly in octaves from 20 Hz to 20 kHz (dB, a
// full-scale sine reading 0).
inline constexpr int givreBands = 64;
inline constexpr int givreReadingFrozen = 0;
inline constexpr int givreReadingSpectrum = 1;
inline constexpr int givreReadingCount = givreReadingSpectrum + givreBands;
inline constexpr float givreFloorDb = -100.0f;

[[nodiscard]] inline double givreBandHzOf(const double band) noexcept
{
    return 20.0 * std::pow(1'000.0, band / static_cast<double>(givreBands - 1));
}

} // namespace cydaw::engine::spacefx
