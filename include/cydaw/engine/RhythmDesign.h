#pragma once

#include <cydaw/engine/EffectCatalogue.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

// Grains, Saccade, Boucleur: what their processors (RhythmEffects.cpp) and their displays
// (RhythmPanes.inc) both compute. The enums follow the catalogue's
// parameter order (EffectCatalogue.h); a helper's name never repeats an
// enumerator's (they share this namespace): `xxxOf`, `xxxApplied`…

namespace cydaw::engine::rhythm {

// Grains ("grains").
enum GrainsParameter : int {
    grainsTime,
    grainsFeedback,
    grainsPitch,
    grainsSpray,
    grainsDensity,
    grainsPitchRandom,
    grainsMix,
    grainsOutput,
    grainsSync,
    grainsSize,
    grainsStereo,
    grainsReverseChance,
    grainsFreeze,
    grainsHighCut,
    grainsParameterCount
};

// Saccade ("saccade").
enum SaccadeParameter : int {
    saccadeChance,
    saccadeVariation,
    saccadeDecay,
    saccadePitchDecay,
    saccadeFilter,
    saccadeMix,
    saccadeOutput,
    saccadeInterval,
    saccadeOffset,
    saccadeGate,
    saccadeGrid,
    saccadeMode,
    saccadeReverseChance,
    saccadeRepeat,
    saccadeParameterCount
};

// Looper ("boucleur").
enum BoucleurParameter : int {
    boucleurFeedback,
    boucleurLevel,
    boucleurDry,
    boucleurOutput,
    boucleurState,
    boucleurBars,
    boucleurQuantize,
    boucleurSpeed,
    boucleurReverse,
    boucleurClear,
    boucleurParameterCount
};

// A choice's names and what it does, French and English.
struct RhythmChoice {
    const char* french;
    const char* english;
    const char* frenchHint;
    const char* englishHint;
};

// The tempo the effects read: the transport's, kept sane.
[[nodiscard]] inline double rhythmTempoOf(const double beatsPerMinute) noexcept
{
    return std::isfinite(beatsPerMinute) ? std::clamp(beatsPerMinute, 20.0, 400.0) : 120.0;
}

// A bar's length in quarter notes for a time signature (4/4: 4, 6/8: 3).
[[nodiscard]] inline double rhythmBarBeatsOf(const int numerator, const int denominator) noexcept
{
    const auto top = std::clamp(numerator, 1, 32);
    const auto bottom = std::clamp(denominator, 1, 32);
    return static_cast<double>(top) * 4.0 / static_cast<double>(bottom);
}

// ---- Grains -------------------------------------------------------------------------------------
//
// A granular delay: the sound written into a four-second circular buffer,
// read back by short grains, each from its own point in the past, at its own
// pitch, forwards or backwards, somewhere in the stereo field.

inline constexpr double grainsBufferSeconds = 4.0;
inline constexpr int grainsPoolSize = 32;
// The closest a grain's head may come to now (samples): what the
// interpolation reads ahead of its position.
inline constexpr double grainsLeadSamples = 4.0;

// What the processor shows: [0] the grains sounding, then three values a
// grain of the pool (all 32, silent ones at level 0): where it reads (0 now,
// 1 the buffer's far end), how loud it is now (0..1), where it sits (-1 left,
// 1 right).
enum GrainsReadingField : int { grainsReadingPosition, grainsReadingLevel, grainsReadingPan, grainsReadingFields };
inline constexpr int grainsReadingCount = 1 + grainsPoolSize * grainsReadingFields;
[[nodiscard]] constexpr int grainsReadingIndexOf(const int grain, const int field) noexcept
{
    return 1 + grain * grainsReadingFields + field;
}

// How far back (ms) the grains start: the time, or a note value of the tempo,
// never beyond the time's range.
[[nodiscard]] inline double grainsDelayMsOf(const float time, const float sync, const double beatsPerMinute) noexcept
{
    if (const auto* division = noteDivisionFor(sync))
        return std::clamp(division->beats * 60'000.0 / rhythmTempoOf(beatsPerMinute), 1.0, 2'000.0);
    return std::clamp(static_cast<double>(time), 1.0, 2'000.0);
}

// A pitch in semitones as a speed of reading.
[[nodiscard]] inline double grainsRatioOf(const double semitones) noexcept
{
    return std::pow(2.0, std::clamp(semitones, -36.0, 36.0) / 12.0);
}

// How many grains sound at once on average (density × size).
[[nodiscard]] inline double grainsOverlapOf(const float density, const float sizeMs) noexcept
{
    return std::clamp(static_cast<double>(density), 1.0, 100.0) * std::clamp(static_cast<double>(sizeMs), 10.0, 500.0) * 0.001;
}

// How alike the grains are (1: all read the same sound in step, 0: each its
// own): what decides whether their sum is scaled by its amplitude or by its
// power. Grains in step stack like one signal; scattered ones like noise.
[[nodiscard]] inline double grainsCoherenceOf(const float spray, const float pitch, const float pitchRandom, const float reverseChance,
                                              const float density) noexcept
{
    // How far apart (ms) two neighbouring grains read.
    const auto apart = std::max(0.0, static_cast<double>(spray))
                       + std::abs(grainsRatioOf(pitch) - 1.0) * 1'000.0 / std::clamp(static_cast<double>(density), 1.0, 100.0)
                       + std::max(0.0, static_cast<double>(pitchRandom)) * 50.0 + std::max(0.0, static_cast<double>(reverseChance)) * 100.0;
    return std::exp(-apart / 2.0);
}

// Where a grain may start back (samples) so its head never reaches now nor
// leaves the buffer: a grain read faster than time runs towards now, one read
// backwards away from it.
[[nodiscard]] inline double grainsStartApplied(const double wanted, const double ratio, const bool reversed, const double lengthSamples,
                                               const double bufferSamples) noexcept
{
    // Forwards its head moves (ratio - 1) towards now each sample; backwards
    // it starts `ratio × length` nearer and moves away.
    const auto nearest = reversed ? ratio * lengthSamples + grainsLeadSamples : std::max(0.0, ratio - 1.0) * lengthSamples + grainsLeadSamples;
    const auto travel = reversed ? lengthSamples : std::max(0.0, 1.0 - ratio) * lengthSamples;
    const auto farthest = std::max(nearest, bufferSamples - travel - 8.0);
    return std::clamp(wanted, nearest, farthest);
}

// A grain's place in the stereo field as gains on its two sides (a stereo
// source panned: the far side folded in, its power kept for a mono one).
struct GrainsPan {
    double leftFromLeft;
    double leftFromRight;
    double rightFromLeft;
    double rightFromRight;
};
[[nodiscard]] inline GrainsPan grainsPanOf(const double pan) noexcept
{
    const auto p = std::clamp(pan, -1.0, 1.0);
    const auto keep = 1.0 / std::sqrt(1.0 + p * p);
    if (p >= 0.0)
        return { (1.0 - p) * keep, 0.0, p * keep, keep };
    return { keep, -p * keep, 0.0, (1.0 + p) * keep };
}

// ---- Saccade ------------------------------------------------------------------------------------
//
// A beat repeater: once an interval of the tempo, at its offset, a slice of
// the sound is caught as it plays and repeated for a while, each repeat
// quieter, lower, sometimes backwards.

inline constexpr int saccadeModeCount = 3;
inline constexpr std::array<RhythmChoice, saccadeModeCount> saccadeModes { {
    { "Mélange", "Mix", "Les répétitions par-dessus le son", "The repeats over the sound" },
    { "Insertion", "Insert", "Les répétitions à la place du son tant qu'elles durent", "The repeats in place of the sound while they last" },
    { "Seules", "Repeats only", "Seulement les répétitions : le son lui-même se tait", "Only the repeats: the sound itself is silent" },
} };
[[nodiscard]] inline const RhythmChoice& saccadeModeOf(const float value) noexcept
{
    return saccadeModes[static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(value)), 0, saccadeModeCount - 1))];
}

// The fades at a slice's edges, the rolling buffer it is caught in, the
// longest slice (a whole note at a slow tempo is cut there).
inline constexpr double saccadeFadeSeconds = 0.002;
inline constexpr double saccadeBufferSeconds = 4.0;
inline constexpr double saccadeLongestSliceSeconds = 3.0;
// How far (semitones) each repeat falls at a pitch decay of 1, and the
// slowest a repeat plays.
inline constexpr double saccadeDropPerRepeat = 3.0;
inline constexpr double saccadeSlowestRate = 0.125;

// What the processor shows: repeating (0/1), the slice playing in beats, the
// place in the interval (0..1, while the transport runs), the repeats done.
enum SaccadeReadingField : int {
    saccadeReadingRepeating,
    saccadeReadingGrid,
    saccadeReadingPosition,
    saccadeReadingRepeats,
    saccadeReadingCount
};

// A note value (1..12 of noteDivisions) in beats; 1/1 is a bar of the time
// signature for the interval.
[[nodiscard]] inline double saccadeNoteBeatsOf(const float value) noexcept
{
    const auto index = std::clamp(static_cast<int>(std::lround(value)), 1, static_cast<int>(noteDivisions.size()));
    return noteDivisions[static_cast<std::size_t>(index - 1)].beats;
}
[[nodiscard]] inline double saccadeIntervalBeatsOf(const float value, const int numerator, const int denominator) noexcept
{
    const auto index = std::clamp(static_cast<int>(std::lround(value)), 1, static_cast<int>(noteDivisions.size()));
    return index == static_cast<int>(noteDivisions.size()) ? rhythmBarBeatsOf(numerator, denominator) : saccadeNoteBeatsOf(value);
}
// The offset and the gate (sixteenths) in beats, inside the interval.
[[nodiscard]] inline double saccadeOffsetBeatsOf(const float offset, const double intervalBeats) noexcept
{
    const auto beats = std::clamp(std::round(static_cast<double>(offset)), 0.0, 15.0) * 0.25;
    return intervalBeats > 0.0 ? std::fmod(beats, intervalBeats) : 0.0;
}
[[nodiscard]] inline double saccadeGateBeatsOf(const float gate, const double intervalBeats) noexcept
{
    return std::min(std::clamp(std::round(static_cast<double>(gate)), 1.0, 16.0) * 0.25, std::max(0.0, intervalBeats));
}
// The n-th repeat's level and speed (the slice itself is the 0th).
[[nodiscard]] inline double saccadeRepeatGainOf(const float decay, const int repeat) noexcept
{
    return std::pow(1.0 - std::clamp(static_cast<double>(decay), 0.0, 1.0), std::max(0, repeat));
}
[[nodiscard]] inline double saccadeRepeatRateOf(const float pitchDecay, const int repeat) noexcept
{
    const auto semitones = -std::clamp(static_cast<double>(pitchDecay), 0.0, 1.0) * saccadeDropPerRepeat * std::max(0, repeat);
    return std::max(saccadeSlowestRate, std::pow(2.0, semitones / 12.0));
}
// The low-pass on the repeats is open (none) at the top of its range.
[[nodiscard]] inline bool saccadeFilterOpenOf(const float cutoff) noexcept
{
    return cutoff >= 19'900.0f;
}
// The picture's guess of which repeats play backwards: as many as the chance
// says, spread evenly (the sound decides at random).
[[nodiscard]] inline bool saccadeShownReversedOf(const float chance, const int repeat) noexcept
{
    const auto spread = std::fmod(static_cast<double>(repeat) * 0.6180339887498949 + 0.31, 1.0);
    return repeat > 0 && spread < std::clamp(static_cast<double>(chance), 0.0, 1.0);
}

// ---- Boucleur -----------------------------------------------------------------------------------
//
// A looper: what is recorded plays round, layers added over it; free, or
// bars of the tempo that end the recording by themselves.

inline constexpr int boucleurStateCount = 4;
// The state shown while a recording waits for the next bar line.
inline constexpr int boucleurWaiting = 4;
inline constexpr std::array<RhythmChoice, boucleurStateCount + 1> boucleurStates { {
    { "Arrêt", "Stop", "La boucle se tait (elle reste en mémoire)", "The loop is silent (it stays in memory)" },
    { "Enregistrer", "Record", "Enregistre une nouvelle boucle", "Records a new loop" },
    { "Jouer", "Play", "Joue la boucle", "Plays the loop" },
    { "Superposer", "Overdub", "Joue la boucle et y ajoute ce qui entre", "Plays the loop and adds what comes in" },
    { "En attente", "Waiting", "L'enregistrement part à la prochaine mesure", "Recording starts on the next bar" },
} };
[[nodiscard]] inline const RhythmChoice& boucleurStateOf(const float value) noexcept
{
    return boucleurStates[static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(value)), 0, boucleurStateCount))];
}

// The lengths (0 free: as long as recorded), the speeds.
inline constexpr std::array<int, 6> boucleurBarCounts { 0, 1, 2, 4, 8, 16 };
inline constexpr std::array<double, 3> boucleurSpeeds { 0.5, 1.0, 2.0 };
[[nodiscard]] inline int boucleurBarsOf(const float value) noexcept
{
    return boucleurBarCounts[static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(value)), 0,
                                                                 static_cast<int>(boucleurBarCounts.size()) - 1))];
}
[[nodiscard]] inline double boucleurSpeedOf(const float value) noexcept
{
    return boucleurSpeeds[static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(value)), 0,
                                                              static_cast<int>(boucleurSpeeds.size()) - 1))];
}

// The longest loop, the fades (punching in and out, starting and stopping,
// the seam where the loop closes).
inline constexpr double boucleurLongestSeconds = 60.0;
inline constexpr double boucleurFadeSeconds = 0.01;
// The loop's outline: so many points, each the peak of its stretch.
inline constexpr int boucleurOutlinePoints = 128;
// The peaks are kept a stretch of this many samples.
inline constexpr int boucleurChunkSamples = 256;

// What the processor shows: its state (0..3, 4 waiting for the bar), the
// loop's length (s), where it plays (0..1), the layers recorded, then the
// loop's outline.
enum BoucleurReadingField : int {
    boucleurReadingState,
    boucleurReadingLength,
    boucleurReadingPosition,
    boucleurReadingLayers,
    boucleurReadingOutline
};
inline constexpr int boucleurReadingCount = boucleurReadingOutline + boucleurOutlinePoints;

// A loop of bars at a tempo, in seconds (none when free).
[[nodiscard]] inline double boucleurBarsSecondsOf(const float bars, const double beatsPerMinute, const int numerator,
                                                  const int denominator) noexcept
{
    const auto count = boucleurBarsOf(bars);
    if (count <= 0)
        return 0.0;
    return std::min(boucleurLongestSeconds, count * rhythmBarBeatsOf(numerator, denominator) * 60.0 / rhythmTempoOf(beatsPerMinute));
}

} // namespace cydaw::engine::rhythm
