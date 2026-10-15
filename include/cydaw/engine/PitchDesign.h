#pragma once

#include <cydaw/engine/EffectCatalogue.h>
#include <cydaw/engine/PitchTracker.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

// Harmonie, Justesse: what their processors (PitchEffects.cpp) and their displays
// (PitchPanes.inc) both compute. The enums follow the catalogue's
// parameter order (EffectCatalogue.h); a helper's name never repeats an
// enumerator's (they share this namespace): `xxxOf`, `xxxApplied`…

namespace cydaw::engine::pitchfx {

// Harmonie ("harmonie").
enum HarmonieParameter : int {
    harmonieShiftA,
    harmonieFineA,
    harmonieLevelA,
    harmonieShiftB,
    harmonieLevelB,
    harmonieDry,
    harmonieDelay,
    harmonieOutput,
    harmonieWindow,
    harmonieMode,
    harmonieFineB,
    harmoniePanA,
    harmoniePanB,
    harmonieFeedback,
    harmonieParameterCount
};

// Justesse ("justesse").
enum JustesseParameter : int {
    justesseSpeed,
    justesseAmount,
    justesseHumanize,
    justesseTranspose,
    justesseMix,
    justesseOutput,
    justesseKey,
    justesseScale,
    justesseLowPitch,
    justesseHighPitch,
    justesseSensitivity,
    justesseParameterCount
};

// ---- Shared -------------------------------------------------------------------------------------

// How late both effects' sound comes out (ms): the pitch marks need about
// two periods of the lowest voice ahead of the grain they cut, and a smooth
// window of up to twice this is centred on it. The dry sound waits as long.
inline constexpr double pitchLatencyMs = 40.0;

// A choice: its names and a hint, in French and English.
struct PitchChoice {
    const char* french;
    const char* english;
    const char* frenchHint;
    const char* englishHint;
};

inline constexpr std::array<const char*, 12> pitchNoteNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
inline constexpr std::array<const char*, 12> pitchNoteNamesFrench { "Do", "Do#", "Ré", "Ré#", "Mi", "Fa", "Fa#",
                                                                     "Sol", "Sol#", "La", "La#", "Si" };

// A frequency as a MIDI note (fractional, A4 = 69 at 440 Hz), -1 for none.
[[nodiscard]] inline double pitchNoteOfHz(const double hz) noexcept
{
    return PitchTracker::noteOf(hz);
}
[[nodiscard]] inline double pitchHzOfNote(const double note) noexcept
{
    return PitchTracker::hzOf(note);
}
// How much a shift of that many semitones multiplies a frequency.
[[nodiscard]] inline double pitchRatioOf(const double semitones) noexcept
{
    return std::pow(2.0, std::clamp(semitones, -48.0, 48.0) / 12.0);
}
// A note's pitch class (0 = C), for any note.
[[nodiscard]] inline int pitchClassOf(const int note) noexcept
{
    return ((note % 12) + 12) % 12;
}

// ---- Harmonie: two voices at their intervals ------------------------------------------------------

inline constexpr int harmonieModeCount = 3;
inline constexpr std::array<PitchChoice, harmonieModeCount> harmonieModes { {
    { "Lisse", "Smooth", "Des grains longs fondus lentement : doux, pour les accords, les nappes, tout ce qui joue plusieurs notes",
      "Long grains with slow crossfades: soft, for chords, pads, anything playing several notes" },
    { "Voix", "Voice",
      "Suit la hauteur de la voix et garde ses formants : une harmonie qui chante avec le même timbre (des grains sur ce qui est soufflé)",
      "Follows the voice's pitch and keeps its formants: a harmony that sings with the same timbre (grains on breathy parts)" },
    { "Percussif", "Percussive", "Des grains courts qui gardent les attaques : batteries, guitares grattées, sons secs",
      "Short grains that keep the attacks: drums, strummed guitars, dry sounds" },
} };
inline constexpr int harmonieModeSmooth = 0;
inline constexpr int harmonieModeVoice = 1;
inline constexpr int harmonieModePercussive = 2;
[[nodiscard]] inline int harmonieModeOf(const float value) noexcept
{
    return std::clamp(static_cast<int>(std::lround(value)), 0, harmonieModeCount - 1);
}
// The percussive mode's grains against the window set.
inline constexpr double harmoniePercussiveWindow = 0.4;
// The voice mode's pitch range (Hz): low male voices to high sopranos.
inline constexpr double harmonieLowestHz = 55.0;
inline constexpr double harmonieHighestHz = 1'100.0;

// A voice's shift: its semitones and cents.
[[nodiscard]] inline double harmonieSemitonesOf(const float shift, const float fine) noexcept
{
    return static_cast<double>(shift) + static_cast<double>(fine) / 100.0;
}
[[nodiscard]] inline double harmonieRatioOf(const float shift, const float fine) noexcept
{
    return pitchRatioOf(harmonieSemitonesOf(shift, fine));
}

// A voice's gains on the left and the right for its pan (-1..1): the
// balance law, unity at the centre, the far side fading out.
struct PitchPanGains {
    double left;
    double right;
};
[[nodiscard]] inline PitchPanGains harmoniePanOf(const double pan) noexcept
{
    const auto p = std::clamp(pan, -1.0, 1.0);
    return { std::min(1.0, 1.0 - p), std::min(1.0, 1.0 + p) };
}

// The names of the intervals, by semitones (0..24, the sign apart).
struct PitchInterval {
    const char* french;
    const char* english;
};
inline constexpr std::array<PitchInterval, 25> harmonieIntervals { {
    { "Unisson", "Unison" },
    { "Seconde mineure", "Minor second" },
    { "Seconde majeure", "Major second" },
    { "Tierce mineure", "Minor third" },
    { "Tierce majeure", "Major third" },
    { "Quarte", "Fourth" },
    { "Triton", "Tritone" },
    { "Quinte", "Fifth" },
    { "Sixte mineure", "Minor sixth" },
    { "Sixte majeure", "Major sixth" },
    { "Septième mineure", "Minor seventh" },
    { "Septième majeure", "Major seventh" },
    { "Octave", "Octave" },
    { "Neuvième mineure", "Minor ninth" },
    { "Neuvième", "Ninth" },
    { "Dixième mineure", "Minor tenth" },
    { "Dixième majeure", "Major tenth" },
    { "Onzième", "Eleventh" },
    { "Onzième augmentée", "Augmented eleventh" },
    { "Douzième", "Twelfth" },
    { "Treizième mineure", "Minor thirteenth" },
    { "Treizième", "Thirteenth" },
    { "Quatorzième mineure", "Minor fourteenth" },
    { "Quatorzième majeure", "Major fourteenth" },
    { "Double octave", "Two octaves" },
} };
[[nodiscard]] inline const PitchInterval& harmonieIntervalOf(const double semitones) noexcept
{
    const auto steps = std::clamp(static_cast<int>(std::lround(std::abs(semitones))), 0, 24);
    return harmonieIntervals[static_cast<std::size_t>(steps)];
}

// Its readings: the pitch the voice mode follows (Hz, 0 for none or in the
// other modes), then each voice's ratio.
inline constexpr int harmonieReadHz = 0;
inline constexpr int harmonieReadRatioA = 1;
inline constexpr int harmonieReadRatioB = 2;
inline constexpr int harmonieReadingCount = 3;

// ---- Justesse: pitch correction ---------------------------------------------------------------

// A scale: its names, a hint, and its notes as bits above the key (bit 0 the
// key itself).
struct JustesseScaleInfo {
    const char* french;
    const char* english;
    const char* frenchHint;
    const char* englishHint;
    std::uint16_t notes;
};
[[nodiscard]] constexpr std::uint16_t justesseNotesOf(const std::initializer_list<int> degrees) noexcept
{
    std::uint16_t bits = 0;
    for (const auto degree : degrees)
        bits = static_cast<std::uint16_t>(bits | (1u << static_cast<unsigned>(degree)));
    return bits;
}
inline constexpr int justesseScaleCount = 12;
inline constexpr std::array<JustesseScaleInfo, justesseScaleCount> justesseScales { {
    { "Chromatique", "Chromatic", "Les douze notes : juste la note la plus proche", "All twelve notes: just the nearest note",
      justesseNotesOf({ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }) },
    { "Majeur", "Major", "La gamme majeure", "The major scale", justesseNotesOf({ 0, 2, 4, 5, 7, 9, 11 }) },
    { "Mineur naturel", "Natural minor", "La gamme mineure naturelle", "The natural minor scale", justesseNotesOf({ 0, 2, 3, 5, 7, 8, 10 }) },
    { "Mineur harmonique", "Harmonic minor", "Le mineur avec sa septième haussée", "Minor with its raised seventh",
      justesseNotesOf({ 0, 2, 3, 5, 7, 8, 11 }) },
    { "Mineur mélodique", "Melodic minor", "Le mineur avec sa sixte et sa septième haussées", "Minor with its raised sixth and seventh",
      justesseNotesOf({ 0, 2, 3, 5, 7, 9, 11 }) },
    { "Pentatonique majeure", "Major pentatonic", "Cinq notes, sans demi-ton", "Five notes, no semitone", justesseNotesOf({ 0, 2, 4, 7, 9 }) },
    { "Pentatonique mineure", "Minor pentatonic", "Cinq notes du mineur", "Five notes of the minor", justesseNotesOf({ 0, 3, 5, 7, 10 }) },
    { "Blues", "Blues", "La pentatonique mineure et sa quarte augmentée", "The minor pentatonic and its blue note",
      justesseNotesOf({ 0, 3, 5, 6, 7, 10 }) },
    { "Dorien", "Dorian", "Le mineur à la sixte majeure", "Minor with a major sixth", justesseNotesOf({ 0, 2, 3, 5, 7, 9, 10 }) },
    { "Phrygien", "Phrygian", "Le mineur à la seconde mineure", "Minor with a minor second", justesseNotesOf({ 0, 1, 3, 5, 7, 8, 10 }) },
    { "Lydien", "Lydian", "Le majeur à la quarte augmentée", "Major with a raised fourth", justesseNotesOf({ 0, 2, 4, 6, 7, 9, 11 }) },
    { "Mixolydien", "Mixolydian", "Le majeur à la septième mineure", "Major with a minor seventh", justesseNotesOf({ 0, 2, 4, 5, 7, 9, 10 }) },
} };
static_assert(justesseNotesOf({ 0, 2, 4, 5, 7, 9, 11 }) == 0b1010'1011'0101);

[[nodiscard]] inline int justesseKeyOf(const float value) noexcept
{
    return std::clamp(static_cast<int>(std::lround(value)), 0, 11);
}
[[nodiscard]] inline const JustesseScaleInfo& justesseScaleOf(const float value) noexcept
{
    return justesseScales[static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(value)), 0, justesseScaleCount - 1))];
}
// Whether the scale in that key holds a note.
[[nodiscard]] inline bool justesseAllowsOf(const int note, const int key, const JustesseScaleInfo& scale) noexcept
{
    return ((scale.notes >> static_cast<unsigned>(pitchClassOf(note - key))) & 1u) != 0u;
}
// The allowed note nearest a pitch (fractional MIDI note).
[[nodiscard]] inline int justesseNearestOf(const double note, const int key, const JustesseScaleInfo& scale) noexcept
{
    const auto centre = static_cast<int>(std::lround(note));
    auto best = centre;
    auto distance = 1.0e9;
    for (int step = -7; step <= 7; ++step) {
        const auto candidate = centre + step;
        if (!justesseAllowsOf(candidate, key, scale))
            continue;
        if (const auto away = std::abs(static_cast<double>(candidate) - note); away < distance) {
            distance = away;
            best = candidate;
        }
    }
    return best;
}
// The note a pitch is corrected towards: the nearest allowed one, but the
// current target holds until the pitch has gone 60 % of the way to another
// (so a pitch between two notes does not flip between them).
inline constexpr double justesseHysteresis = 0.6;
[[nodiscard]] inline int justesseTargetOf(const double note, const int current, const int key, const JustesseScaleInfo& scale) noexcept
{
    const auto nearest = justesseNearestOf(note, key, scale);
    if (current < 0 || nearest == current || !justesseAllowsOf(current, key, scale))
        return nearest;
    const auto span = std::abs(static_cast<double>(nearest - current));
    return std::abs(note - static_cast<double>(current)) >= justesseHysteresis * span ? nearest : current;
}

// The tracker's threshold for the sensitivity (0 strict .. 1 lenient).
[[nodiscard]] inline float justesseThresholdOf(const float sensitivity) noexcept
{
    return 0.05f + 0.35f * std::clamp(sensitivity, 0.0f, 1.0f);
}
// How much slower the correction gets as a note is held (1 .. 5): from
// 300 ms on, growing over the next half second, by the humanize amount.
[[nodiscard]] inline double justesseSlownessOf(const double heldSeconds, const double humanize) noexcept
{
    const auto grown = std::clamp((heldSeconds - 0.3) / 0.5, 0.0, 1.0);
    return 1.0 + 4.0 * std::clamp(humanize, 0.0, 1.0) * grown;
}
// The time constant (s) of the correction's glide: the speed is the time it
// takes to get 95 % of the way (three time constants).
[[nodiscard]] inline double justesseTimeConstantOf(const double speedMs, const double heldSeconds, const double humanize) noexcept
{
    return std::max(0.0, speedMs) * 0.001 * justesseSlownessOf(heldSeconds, humanize) / 3.0;
}
// The catalogue's lowest pitch: what the latency is made for.
inline constexpr double justesseLowestHz = 50.0;
// The grains' window (ms) for the unvoiced parts, which are only transposed.
inline constexpr double justesseNoiseWindowMs = 30.0;

// Its readings: the detected pitch (Hz, 0 for none), the target (MIDI note,
// -1 for none), the correction applied (cents), the tracker's confidence,
// then the last 32 detected notes and the last 32 corrected ones (fractional
// MIDI before the transposition, -1 for none, oldest first, one every 20 ms).
inline constexpr int justesseReadHz = 0;
inline constexpr int justesseReadTarget = 1;
inline constexpr int justesseReadCents = 2;
inline constexpr int justesseReadConfidence = 3;
inline constexpr int justesseTrailLength = 32;
inline constexpr int justesseReadDetectedTrail = 4;
inline constexpr int justesseReadCorrectedTrail = justesseReadDetectedTrail + justesseTrailLength;
inline constexpr int justesseReadingCount = justesseReadCorrectedTrail + justesseTrailLength;
inline constexpr double justesseTrailSeconds = 0.02;

} // namespace cydaw::engine::pitchfx
