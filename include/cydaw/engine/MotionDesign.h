#pragma once

#include <cydaw/engine/EffectCatalogue.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>

// Pompe, Décaleur, Anneau, Rotor: what their processors (MotionEffects.cpp) and their displays
// (MotionPanes.inc) both compute. The enums follow the catalogue's
// parameter order (EffectCatalogue.h); a helper's name never repeats an
// enumerator's (they share this namespace): `xxxOf`, `xxxApplied`…
//
//   Pompe     (pompe)     a volume shaper: a curve over each beat (or each
//                         hit on its sidechain) that ducks the sound
//   Décaleur  (decaleur)  a frequency shifter: every partial moved by the
//                         same number of hertz, so harmonics stop being
//                         harmonic
//   Anneau    (anneau)    a ring modulator: the sound times a carrier
//   Rotor     (rotor)     a rotating speaker cabinet: a horn and a drum
//                         turning in front of two microphones
//
// A function taking `values` reads the effect's values in catalogue order
// (the enums below index them).

namespace cydaw::engine::motion {

// Pompe ("pompe").
enum PompeParameter : int {
    pompeDepth,
    pompeRate,
    pompeCurve,
    pompeHold,
    pompePhase,
    pompeStereoOffset,
    pompeMix,
    pompeOutput,
    pompeShape,
    pompeSync,
    pompeTrigger,
    pompeThreshold,
    pompeSmoothing,
    pompePoint1,
    pompePoint2,
    pompePoint3,
    pompePoint4,
    pompePoint5,
    pompePoint6,
    pompePoint7,
    pompePoint8,
    pompePoint9,
    pompePoint10,
    pompePoint11,
    pompePoint12,
    pompePoint13,
    pompePoint14,
    pompePoint15,
    pompePoint16,
    pompeParameterCount
};

// Decaleur ("decaleur").
enum DecaleurParameter : int {
    decaleurShift,
    decaleurFine,
    decaleurSpread,
    decaleurFeedback,
    decaleurLfoRate,
    decaleurLfoDepth,
    decaleurMix,
    decaleurOutput,
    decaleurMode,
    decaleurSync,
    decaleurLfoShape,
    decaleurDelay,
    decaleurParameterCount
};

// Anneau ("anneau").
enum AnneauParameter : int {
    anneauFrequency,
    anneauDrive,
    anneauLfoRate,
    anneauLfoDepth,
    anneauMix,
    anneauOutput,
    anneauStereo,
    anneauShape,
    anneauMode,
    anneauSync,
    anneauLowCut,
    anneauLfoShape,
    anneauParameterCount
};

// Rotor ("rotor").
enum RotorParameter : int {
    rotorSlow,
    rotorFast,
    rotorAcceleration,
    rotorBalance,
    rotorDoppler,
    rotorDrive,
    rotorMix,
    rotorOutput,
    rotorSpeed,
    rotorCrossover,
    rotorWidth,
    rotorRoom,
    rotorParameterCount
};

// ---- Shared ---------------------------------------------------------------------------------

// A choice as the interface names it, with a hint each.
struct MotionChoice {
    const char* french;
    const char* english;
    const char* frenchHint;
    const char* englishHint;
};

inline constexpr double motionTwoPi = 2.0 * std::numbers::pi;

[[nodiscard]] inline double phaseWrapped(const double phase) noexcept
{
    return phase - std::floor(phase);
}

[[nodiscard]] inline int choiceOf(const float value, const int count) noexcept
{
    return std::clamp(static_cast<int>(std::lround(value)), 0, count - 1);
}

[[nodiscard]] inline double clampedOf(const float value, const double low, const double high) noexcept
{
    return std::clamp(static_cast<double>(value), low, high);
}

// The waves of Décaleur's and Anneau's LFOs, -1..1 at a phase in [0, 1);
// all four start at their middle, rising (as a sine does).
inline constexpr int lfoWaveCount = 4;
inline constexpr std::array<MotionChoice, lfoWaveCount> lfoWaves { {
    { "Sinus", "Sine", "Un balancement régulier et doux", "A smooth, even swing" },
    { "Triangle", "Triangle", "Monte et descend en ligne droite", "Up and down in straight lines" },
    { "Dents de scie", "Saw", "Monte d'un bout à l'autre, puis retombe d'un coup", "Rises all the way, then drops at once" },
    { "Carré", "Square", "Saute d'un bout à l'autre", "Jumps from one end to the other" },
} };

[[nodiscard]] inline double lfoWaveAt(const int wave, const double phase) noexcept
{
    const auto t = phaseWrapped(phase);
    switch (wave) {
    case 1: return 1.0 - 4.0 * std::abs(phaseWrapped(t + 0.25) - 0.5);
    case 2: return 2.0 * phaseWrapped(t + 0.5) - 1.0;
    case 3: return t < 0.5 ? 1.0 : -1.0;
    default: return std::sin(motionTwoPi * t);
    }
}

// ---- Pompe ----------------------------------------------------------------------------------
//
// One cycle (a note value of the tempo, a period of the rate, or the time
// after a hit on the sidechain) is a curve from the bottom back up to the
// top: the gain is 1 - depth * (1 - curve). Its place 0 is the beat.

inline constexpr int pompeSoftPump = 0;
inline constexpr int pompeHardPump = 1;
inline constexpr int pompeCut = 2;
inline constexpr int pompeChop = 3;
inline constexpr int pompeRampUp = 4;
inline constexpr int pompeRampDown = 5;
inline constexpr int pompeSine = 6;
inline constexpr int pompeDrawn = 7;
inline constexpr int pompeShapeCount = 8;
inline constexpr std::array<MotionChoice, pompeShapeCount> pompeShapes { {
    { "Pompe douce", "Soft pump", "Le son s'efface sur le temps puis revient en douceur", "The sound gives way on the beat, then swells back gently" },
    { "Pompe dure", "Hard pump", "Tenu au fond un instant, puis revient franchement", "Held down a moment, then back firmly" },
    { "Coupure", "Cut", "Coupé net sur le temps, revenu presque aussitôt", "Cut dead on the beat, back almost at once" },
    { "Hachure", "Chop", "Coupé la première moitié du cycle", "Off for the first half of the cycle" },
    { "Rampe montante", "Ramp up", "Monte du fond au sommet tout le long du cycle", "Rises from the bottom to the top over the cycle" },
    { "Rampe descendante", "Ramp down", "Descend du sommet au fond tout le long du cycle", "Falls from the top to the bottom over the cycle" },
    { "Sinus", "Sine", "Un balancement : au fond sur le temps, en haut au milieu", "A swing: down on the beat, up halfway" },
    { "Dessinée", "Drawn", "Seize points que vous placez (glissez-les)", "Sixteen points you place (drag them)" },
} };

// The drawn shape's sixteen points, spread evenly over the cycle (the first
// on the beat, the last at its end); their defaults draw a soft pump.
inline constexpr int pompePointCount = 16;
inline constexpr std::array<float, pompePointCount> pompeDefaultPoints { 0.0f, 0.22f, 0.42f, 0.58f, 0.7f, 0.79f, 0.86f, 0.91f,
                                                                         0.94f, 0.965f, 0.98f, 0.99f, 1.0f, 1.0f, 1.0f, 1.0f };

// The shape alone at `t` (0..1 of what is left of the cycle after the
// hold): 0 the bottom, 1 the top.
[[nodiscard]] inline double pompeShapeAt(const float* values, const double t) noexcept
{
    const auto x = std::clamp(t, 0.0, 1.0);
    switch (choiceOf(values[pompeShape], pompeShapeCount)) {
    case pompeHardPump: {
        const auto r = std::clamp((x - 0.06) / 0.4, 0.0, 1.0);
        return r * r * (3.0 - 2.0 * r);
    }
    case pompeCut: return x < 0.12 ? 0.0 : std::min(1.0, (x - 0.12) / 0.06);
    case pompeChop: return x < 0.5 ? 0.0 : 1.0;
    case pompeRampUp: return x;
    case pompeRampDown: return 1.0 - x;
    case pompeSine: return 0.5 - 0.5 * std::cos(motionTwoPi * x);
    case pompeDrawn: {
        const auto place = x * static_cast<double>(pompePointCount - 1);
        const auto index = std::min(pompePointCount - 2, static_cast<int>(place));
        const auto a = clampedOf(values[pompePoint1 + index], 0.0, 1.0);
        const auto b = clampedOf(values[pompePoint1 + index + 1], 0.0, 1.0);
        return a + (b - a) * (place - static_cast<double>(index));
    }
    default: {
        const auto r = 1.0 - std::min(1.0, x / 0.85);
        return 1.0 - r * r * std::sqrt(r);
    }
    }
}

// Where the shape's own time starts: the hold keeps the bottom first (the
// ramp down's bottom is its end, so its hold comes last).
[[nodiscard]] inline double pompeShapeTimeOf(const float* values, const double place) noexcept
{
    const auto hold = clampedOf(values[pompeHold], 0.0, 0.5);
    const auto x = std::clamp(place, 0.0, 1.0);
    if (choiceOf(values[pompeShape], pompeShapeCount) == pompeRampDown)
        return std::min(1.0, x / (1.0 - hold));
    return x <= hold ? 0.0 : (x - hold) / (1.0 - hold);
}

// The curve at a place of the cycle (0..1, after the phase shift): the
// hold, then the shape bent (above 0 it climbs back sooner).
[[nodiscard]] inline double pompeBentAt(const float* values, const double place) noexcept
{
    const auto shape = std::clamp(pompeShapeAt(values, pompeShapeTimeOf(values, place)), 0.0, 1.0);
    const auto bend = clampedOf(values[pompeCurve], -1.0, 1.0);
    if (std::abs(bend) < 1.0e-4 || shape <= 0.0)
        return shape;
    return std::pow(shape, std::pow(3.0, -bend));
}

[[nodiscard]] inline double pompeDepthApplied(const float* values, const double curve) noexcept
{
    return 1.0 - clampedOf(values[pompeDepth], 0.0, 1.0) * (1.0 - std::clamp(curve, 0.0, 1.0));
}

// The gain (0..1) at a phase of the cycle when it follows the tempo or runs
// free: the phase knob moves the curve later.
[[nodiscard]] inline double pompeGainAt(const float* values, const double phase) noexcept
{
    return pompeDepthApplied(values, pompeBentAt(values, phaseWrapped(phase - clampedOf(values[pompePhase], 0.0, 1.0))));
}

// The gain `position` cycles after a hit on the key: the curve once (the
// phase knob delays it), then the top until the next hit.
[[nodiscard]] inline double pompeKeyGainAt(const float* values, const double position) noexcept
{
    const auto place = position - clampedOf(values[pompePhase], 0.0, 1.0);
    if (place < 0.0 || place >= 1.0)
        return 1.0;
    return pompeDepthApplied(values, pompeBentAt(values, place));
}

// Where a drawn point lies in the cycle (0..1), after the hold and the
// phase: wrapped round when the cycle repeats, past 1 when it does not.
[[nodiscard]] inline double pompePointPlace(const float* values, const int point, const bool wrapped) noexcept
{
    const auto hold = clampedOf(values[pompeHold], 0.0, 0.5);
    const auto place = hold + (1.0 - hold) * static_cast<double>(point) / static_cast<double>(pompePointCount - 1)
                       + clampedOf(values[pompePhase], 0.0, 1.0);
    return wrapped && place > 1.0 + 1.0e-9 ? place - 1.0 : place;
}

// The key: a hit when its level rises over the threshold; the next once it
// has fallen this far under it.
inline constexpr double pompeRearmDb = 6.0;

// What the processor shows: its gain now (0..1, left then right), where it
// is in its cycle (0..1, left), the key's level (dB).
inline constexpr int pompeReadingGain = 0;
inline constexpr int pompeReadingPhase = 1;
inline constexpr int pompeReadingKeyDb = 2;
inline constexpr int pompeReadingRightGain = 3;
inline constexpr int pompeReadingCount = 4;

// ---- Décaleur -------------------------------------------------------------------------------

inline constexpr int decaleurModeCount = 3;
inline constexpr std::array<MotionChoice, decaleurModeCount> decaleurModes { {
    { "Une bande", "Single sideband", "Tout monte (décalage positif) ou descend (négatif) du même nombre de hertz",
      "Everything moves up (a positive shift) or down (a negative one) by the same hertz" },
    { "Deux bandes", "Both sidebands", "Chaque fréquence part vers le haut et vers le bas à la fois, comme un modulateur en anneau",
      "Each frequency goes both up and down at once, like a ring modulator" },
    { "Côtés opposés", "Opposite sides", "La gauche monte, la droite descend : un son large et mouvant",
      "The left goes up, the right goes down: a wide, moving sound" },
} };

// A feedback delay of 0 means this, the shortest the loop takes cleanly.
inline constexpr double decaleurShortestDelayMs = 1.0;

// The shift before the spread: coarse and fine, plus the LFO's swing at
// `lfo` (-1..1).
[[nodiscard]] inline double decaleurShiftAt(const float* values, const double lfo) noexcept
{
    return clampedOf(values[decaleurShift], -2'000.0, 2'000.0) + clampedOf(values[decaleurFine], -50.0, 50.0)
           + clampedOf(values[decaleurLfoDepth], 0.0, 1'000.0) * std::clamp(lfo, -1.0, 1.0);
}

// Each side's shift (Hz, up positive): the spread takes the left down and
// the right up; opposite sides turn the right one over.
struct DecaleurShifts {
    double left {};
    double right {};
};
[[nodiscard]] inline DecaleurShifts decaleurShiftsOf(const float* values, const double lfo) noexcept
{
    const auto shift = decaleurShiftAt(values, lfo);
    const auto spread = clampedOf(values[decaleurSpread], 0.0, 200.0);
    DecaleurShifts shifts { shift - spread, shift + spread };
    if (choiceOf(values[decaleurMode], decaleurModeCount) == 2)
        shifts.right = -shifts.right;
    return shifts;
}

// What the processor shows: the shift now (Hz, the LFO's swing in it),
// then each side's.
inline constexpr int decaleurReadingShift = 0;
inline constexpr int decaleurReadingLeft = 1;
inline constexpr int decaleurReadingRight = 2;
inline constexpr int decaleurReadingCount = 3;

// ---- Anneau ---------------------------------------------------------------------------------

inline constexpr int anneauShapeCount = 4;
inline constexpr std::array<MotionChoice, anneauShapeCount> anneauShapes { {
    { "Sinus", "Sine", "Deux fréquences par fréquence : la somme et la différence, rien d'autre",
      "Two frequencies for each one: the sum and the difference, nothing else" },
    { "Triangle", "Triangle", "Un peu d'harmoniques impairs en plus : légèrement plus riche", "A few odd harmonics more: slightly richer" },
    { "Carré", "Square", "Riche et métallique, des harmoniques impairs forts", "Rich and metallic, strong odd harmonics" },
    { "Dents de scie", "Saw", "Le plus riche : tous les harmoniques", "The richest: every harmonic" },
} };
inline constexpr int anneauModeCount = 2;
inline constexpr std::array<MotionChoice, anneauModeCount> anneauModes { {
    { "Anneau", "Ring", "Le son fois la porteuse : l'original disparaît, la somme et la différence restent",
      "The sound times the carrier: the original goes, the sum and the difference stay" },
    { "Amplitude", "Amplitude", "La porteuse module le volume : l'original reste, entouré de ses deux bandes",
      "The carrier moves the volume: the original stays, its two sidebands around it" },
} };

// The carrier's wave at a phase (-1..1), as drawn; the processor plays it
// band-limited. All start at their middle, rising.
[[nodiscard]] inline double anneauWaveAt(const int shape, const double phase) noexcept
{
    const auto t = phaseWrapped(phase);
    switch (shape) {
    case 1: return 1.0 - 4.0 * std::abs(phaseWrapped(t + 0.25) - 0.5);
    case 2: return t < 0.5 ? 1.0 : -1.0;
    case 3: return 2.0 * phaseWrapped(t + 0.5) - 1.0;
    default: return std::sin(motionTwoPi * t);
    }
}

// The carrier's frequency with the LFO at `lfo` (-1..1): its depth in octaves.
[[nodiscard]] inline double anneauFrequencyAt(const float* values, const double lfo) noexcept
{
    return clampedOf(values[anneauFrequency], 1.0, 5'000.0) * std::exp2(clampedOf(values[anneauLfoDepth], 0.0, 2.0) * std::clamp(lfo, -1.0, 1.0));
}

// The drive before the carrier: clean at 0 dB, a soft saturation above that
// keeps about the level.
[[nodiscard]] inline double anneauDriveApplied(const double driveDb, const double x) noexcept
{
    if (driveDb <= 0.0)
        return x;
    const auto gain = std::pow(10.0, std::min(driveDb, 24.0) / 20.0);
    const auto blend = std::min(1.0, driveDb / 3.0);
    return x + blend * (std::tanh(gain * x) / std::sqrt(gain) - x);
}

// What the processor shows: the carrier's frequency now (Hz), left and right.
inline constexpr int anneauReadingLeftHz = 0;
inline constexpr int anneauReadingRightHz = 1;
inline constexpr int anneauReadingCount = 2;

// ---- Rotor ----------------------------------------------------------------------------------

inline constexpr int rotorSpeedCount = 3;
inline constexpr std::array<MotionChoice, rotorSpeedCount> rotorSpeeds { {
    { "Arrêt", "Stop", "Les rotors freinent jusqu'à s'arrêter", "The rotors brake to a halt" },
    { "Lent", "Slow", "Le choral : une rotation lente et ample", "The chorale: a slow, wide turn" },
    { "Rapide", "Fast", "Le trémolo : une rotation rapide et scintillante", "The tremolo: a fast, shimmering turn" },
} };

// The drum turns a little slower than the horn and takes longer to get there.
inline constexpr double rotorDrumRatio = 0.85;
inline constexpr double rotorDrumInertia = 2.5;
// The delay the sound takes to the microphones, and how far each rotor
// swings it (ms at full Doppler); how much quieter each is facing away.
inline constexpr double rotorBaseDelayMs = 1.0;
inline constexpr double rotorHornSwingMs = 0.5;
inline constexpr double rotorDrumSwingMs = 0.25;
inline constexpr double rotorHornDip = 0.5;
inline constexpr double rotorDrumDip = 0.2;

// Where each rotor's speed heads (turns a second).
[[nodiscard]] inline double rotorHornTarget(const float* values) noexcept
{
    switch (choiceOf(values[rotorSpeed], rotorSpeedCount)) {
    case 0: return 0.0;
    case 2: return clampedOf(values[rotorFast], 4.0, 9.0);
    default: return clampedOf(values[rotorSlow], 0.3, 2.0);
    }
}
[[nodiscard]] inline double rotorDrumTarget(const float* values) noexcept
{
    return rotorHornTarget(values) * rotorDrumRatio;
}

// How quickly a rotor follows a change of speed: the time constant of its
// approach (seconds; it is two thirds of the way after it), the horn's the
// acceleration, the drum's longer.
[[nodiscard]] inline double rotorTimeConstantOf(const float* values, const bool drum) noexcept
{
    return clampedOf(values[rotorAcceleration], 0.2, 5.0) * (drum ? rotorDrumInertia : 1.0);
}

// The microphones' angles round the cabinet (radians, 0 in front): the
// left at -width * 90°, the right at +width * 90°.
[[nodiscard]] inline double rotorMicAngle(const float* values, const bool right) noexcept
{
    const auto angle = clampedOf(values[rotorWidth], 0.0, 1.0) * std::numbers::pi * 0.5;
    return right ? angle : -angle;
}

// What the processor shows: the horn's and the drum's angles (turns, 0..1)
// and speeds (Hz).
inline constexpr int rotorReadingHornAngle = 0;
inline constexpr int rotorReadingDrumAngle = 1;
inline constexpr int rotorReadingHornHz = 2;
inline constexpr int rotorReadingDrumHz = 3;
inline constexpr int rotorReadingCount = 4;

} // namespace cydaw::engine::motion
