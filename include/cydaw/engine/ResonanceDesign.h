#pragma once

#include <cydaw/engine/EffectCatalogue.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>

// Résonateurs, Corps, Peigne: what their processors (ResonanceEffects.cpp) and their displays
// (ResonancePanes.inc) both compute. The enums follow the catalogue's
// parameter order (EffectCatalogue.h); a helper's name never repeats an
// enumerator's (they share this namespace): `xxxOf`, `xxxApplied`…

namespace cydaw::engine::resonance {

// Resonators ("resonateurs").
enum ResonateursParameter : int {
    resonateursNote,
    resonateursDecay,
    resonateursColour,
    resonateursWidth,
    resonateursMix,
    resonateursOutput,
    resonateursTone,
    resonateursMode,
    resonateursInterval2,
    resonateursInterval3,
    resonateursInterval4,
    resonateursInterval5,
    resonateursGain1,
    resonateursGain2,
    resonateursGain3,
    resonateursGain4,
    resonateursGain5,
    resonateursParameterCount
};

// Corps ("corps").
enum CorpsParameter : int {
    corpsTune,
    corpsDecay,
    corpsMaterial,
    corpsInharmonic,
    corpsPosition,
    corpsMix,
    corpsOutput,
    corpsWidth,
    corpsType,
    corpsLowCut,
    corpsParameterCount
};

// Comb ("peigne").
enum PeigneParameter : int {
    peigneFrequency,
    peigneFeedback,
    peigneDamping,
    peigneSpread,
    peigneLfoDepth,
    peigneLfoRate,
    peigneMix,
    peigneOutput,
    peigneMode,
    peigneSync,
    peigneParameterCount
};

// ---- Shared ------------------------------------------------------------------------------------

// A choice as the interface names it, with a line saying what it does.
struct ResonanceChoice {
    const char* french;
    const char* english;
    const char* frenchHint;
    const char* englishHint;
};

inline constexpr double resonancePi = std::numbers::pi;
// A fall of 60 dB: ln(1000).
inline constexpr double resonanceT60 = 6.907755278982137;

[[nodiscard]] inline double resonanceHzOfNote(const double note) noexcept
{
    return 440.0 * std::pow(2.0, (note - 69.0) / 12.0);
}

// Note names: a pitch class and an octave, MIDI 60 being C4 (45 is A2, 110 Hz).
inline constexpr std::array<const char*, 12> resonanceNotesEnglish { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
inline constexpr std::array<const char*, 12> resonanceNotesFrench { "Do", "Do#", "Ré", "Ré#", "Mi", "Fa", "Fa#", "Sol", "Sol#",
                                                                    "La", "La#", "Si" };
[[nodiscard]] inline int resonancePitchClassOf(const int note) noexcept
{
    return ((note % 12) + 12) % 12;
}
[[nodiscard]] inline int resonanceOctaveOf(const int note) noexcept
{
    return (note >= 0 ? note / 12 : (note - 11) / 12) - 1;
}

// The phase delay (samples) of the one-pole low-pass y = (1 - a) x + a y[n-1]
// at a frequency: what a loop holding it adds to its length.
[[nodiscard]] inline double resonanceOnePoleDelayOf(const double pole, const double hz, const double rate) noexcept
{
    const auto w = 2.0 * resonancePi * std::clamp(hz, 1.0, rate * 0.49) / rate;
    return std::atan2(pole * std::sin(w), 1.0 - pole * std::cos(w)) / w;
}

// A one-pole low-pass's coefficient at a cutoff.
[[nodiscard]] inline double resonanceOnePolePoleOf(const double hz, const double rate) noexcept
{
    return std::exp(-2.0 * resonancePi * std::clamp(hz, 10.0, rate * 0.45) / rate);
}

// ---- Résonateurs ---------------------------------------------------------------------------------

// Five tuned feedback combs (a string each) the sound makes ring: I at the
// root note, II..V at their intervals above or below it.
inline constexpr int resonateursVoices = 5;
inline constexpr int resonateursModeCount = 3;
inline constexpr std::array<ResonanceChoice, resonateursModeCount> resonateursModes { {
    { "Cordes", "Strings", "Des cordes accordées : harmoniques justes, qui chantent longtemps",
      "Tuned strings: true harmonics, singing for long" },
    { "Pincé", "Plucked", "Des cordes pincées : plus brèves et plus brillantes",
      "Plucked strings: shorter and brighter" },
    { "Métal", "Metal", "Des tiges de métal : des partiels étirés, inharmoniques, comme une cloche",
      "Metal rods: stretched, inharmonic partials, bell-like" },
} };
[[nodiscard]] inline int resonateursModeOf(const float value) noexcept
{
    return std::clamp(static_cast<int>(std::lround(value)), 0, resonateursModeCount - 1);
}

// The ring time (s, to fall 60 dB) the decay knob (0..1) gives in a mode:
// 0.1 s to 8 s, plucked strings half as long.
[[nodiscard]] inline double resonateursSecondsOf(const float decay, const int mode) noexcept
{
    const auto seconds = 0.1 * std::pow(80.0, std::clamp(static_cast<double>(decay), 0.0, 1.0));
    return mode == 1 ? seconds * 0.5 : seconds;
}

// A voice's note (MIDI, fractional while a value glides), held where a
// string can ring: 12 (16 Hz) to 120 (8.4 kHz).
[[nodiscard]] inline double resonateursNoteOf(const float* values, const int voice) noexcept
{
    auto note = static_cast<double>(values[resonateursNote]);
    if (voice > 0)
        note += static_cast<double>(values[resonateursInterval2 + std::clamp(voice, 1, resonateursVoices - 1) - 1]);
    return std::clamp(note, 12.0, 120.0);
}

// A voice's level (0..1).
[[nodiscard]] inline double resonateursLevelOf(const float* values, const int voice) noexcept
{
    return std::clamp(static_cast<double>(values[resonateursGain1 + std::clamp(voice, 0, resonateursVoices - 1)]), 0.0, 1.0);
}

// Where a voice sits (-1 left .. 1 right) at a width: I in the middle, II
// and IV to the left, III and V to the right, the later ones further out.
[[nodiscard]] inline double resonateursPanOf(const int voice, const float width) noexcept
{
    constexpr std::array<double, resonateursVoices> sides { 0.0, -0.55, 0.55, -1.0, 1.0 };
    return sides[static_cast<std::size_t>(std::clamp(voice, 0, resonateursVoices - 1))] * std::clamp(static_cast<double>(width), 0.0, 1.0);
}

// The cutoff of the low-pass in a voice's loop: from three times its note
// (dark: the upper harmonics die at once) to far above it (bright); the
// plucked strings brighter.
[[nodiscard]] inline double resonateursCutoffOf(const double hz, const float colour, const int mode, const double rate) noexcept
{
    auto ratio = 3.0 * std::pow(2.0, 6.5 * std::clamp(static_cast<double>(colour), 0.0, 1.0));
    if (mode == 1)
        ratio *= 2.5;
    return std::min(hz * ratio, rate * 0.45);
}

// The metal mode's dispersion: all-pass sections in the loop, each holding
// back the low frequencies about a seventh of the period (the upper
// partials come round sooner, so they ring sharp: a bell's stretched
// series, about 1, 2.2, 3.8, 5.6, 7.6 times the note).
inline constexpr int resonateursDispersionSections = 4;
[[nodiscard]] inline double resonateursDispersionOf(const double periodSamples) noexcept
{
    const auto lowDelay = 0.15 * periodSamples;
    return std::clamp((1.0 - lowDelay) / (1.0 + lowDelay), -0.99, 0.5);
}

// The input's tilt: tone -1..1 lowers one side of this pivot and raises
// the other, up to 9 dB apart.
inline constexpr double resonateursTiltHz = 800.0;
inline constexpr double resonateursTiltDb = 4.5;
// The input is cleared of what no string could hold.
inline constexpr double resonateursInputHighPassHz = 40.0;
// The voices' sum brought back near the sound's level (their levels at
// their defaults add up about 4.4 dB, and a string keeps only what lies
// under its colour).
inline constexpr double resonateursSumGain = 0.7;

// Chords the intervals can be set to at once (II, III, IV, V).
struct ResonateursChord {
    const char* french;
    const char* english;
    std::array<int, 4> intervals;
};
inline constexpr int resonateursChordCount = 7;
inline constexpr std::array<ResonateursChord, resonateursChordCount> resonateursChords { {
    { "Majeur", "Major", { 4, 7, 12, 16 } },
    { "Mineur", "Minor", { 3, 7, 12, 15 } },
    { "Quinte", "Fifth", { 7, 12, 19, 24 } },
    { "Octaves", "Octaves", { 12, -12, 24, -24 } },
    { "Sus4", "Sus4", { 5, 7, 12, 17 } },
    { "Septième", "Seventh", { 4, 7, 10, 16 } },
    { "Neuvième", "Ninth", { 4, 7, 10, 14 } },
} };
// The chord the intervals make, or -1.
[[nodiscard]] inline int resonateursChordOf(const float* values) noexcept
{
    for (int chord = 0; chord < resonateursChordCount; ++chord) {
        auto same = true;
        for (int index = 0; index < 4; ++index)
            same = same
                   && static_cast<int>(std::lround(values[resonateursInterval2 + index]))
                          == resonateursChords[static_cast<std::size_t>(chord)].intervals[static_cast<std::size_t>(index)];
        if (same)
            return chord;
    }
    return -1;
}

// Its readings: each voice's ring now (a peak follower of its output,
// linear, before its level).
inline constexpr int resonateursReadingCount = resonateursVoices;

// ---- Corps ---------------------------------------------------------------------------------------

// A resonant body: up to 24 modes, each a decaying two-pole resonator, at
// the ratios of an object's physics.
inline constexpr int corpsModeCount = 24;
inline constexpr int corpsTypeCount = 7;
enum CorpsObject : int { corpsObjectBeam, corpsObjectMarimba, corpsObjectString, corpsObjectMembrane, corpsObjectPlate, corpsObjectPipe,
                         corpsObjectTube };
inline constexpr std::array<ResonanceChoice, corpsTypeCount> corpsTypes { {
    { "Poutre", "Beam", "Une barre libre à ses deux bouts : des partiels très étirés, métalliques ou boisés",
      "A bar free at both ends: widely stretched partials, metallic or woody" },
    { "Marimba", "Marimba", "Une lame accordée : ses partiels à deux octaves et plus au-dessus de la note",
      "A tuned bar: its partials two octaves and more above the note" },
    { "Corde", "String", "Une corde : tous les harmoniques, justes", "A string: every harmonic, in tune" },
    { "Membrane", "Membrane", "Une peau ronde tendue : des partiels serrés, inharmoniques, comme un tambour",
      "A round stretched skin: close, inharmonic partials, like a drum" },
    { "Plaque", "Plate", "Une plaque : beaucoup de modes denses, brillante", "A plate: many dense modes, bright" },
    { "Tuyau", "Pipe", "Un tuyau ouvert : tous les harmoniques, les graves en avant", "An open pipe: every harmonic, the low ones forward" },
    { "Tube", "Tube", "Un tube fermé d'un côté : les harmoniques impairs seulement, creux",
      "A tube closed at one end: the odd harmonics only, hollow" },
} };
[[nodiscard]] inline int corpsTypeOf(const float value) noexcept
{
    return std::clamp(static_cast<int>(std::lround(value)), 0, corpsTypeCount - 1);
}

// Each object's modes over its lowest.
inline constexpr std::array<std::array<double, corpsModeCount>, corpsTypeCount> corpsRatios { {
    // A free bar: (beta_n / beta_1)^2, beta the roots of cos(b) cosh(b) = 1.
    { 1.0, 2.757, 5.404, 8.933, 13.344, 18.638, 24.814, 31.872, 39.812, 48.635, 58.34, 68.927, 80.397, 92.748, 105.982, 120.099,
      135.097, 150.978, 167.741, 185.386, 203.914, 223.324, 243.616, 264.79 },
    // A tuned bar, cut under its middle: two octaves and about three and a third above.
    { 1.0, 3.93, 9.2, 15.8, 23.6, 33.0, 43.9, 56.4, 70.5, 86.2, 103.5, 122.4, 142.9, 165.0, 188.7, 214.0, 240.9, 269.4, 299.5, 331.2,
      364.5, 399.4, 435.9, 474.0 },
    // A string: every harmonic.
    { 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0, 11.0, 12.0, 13.0, 14.0, 15.0, 16.0, 17.0, 18.0, 19.0, 20.0, 21.0, 22.0, 23.0,
      24.0 },
    // A round membrane held at its rim: the roots of Bessel's functions over the first.
    { 1.0, 1.5933, 2.1355, 2.2954, 2.6531, 2.9173, 3.1555, 3.5001, 3.5985, 3.6475, 4.0589, 4.1317, 4.2304, 4.601, 4.6101, 4.8319,
      4.9033, 5.0836, 5.1308, 5.4121, 5.5404, 5.5531, 5.6508, 5.9765 },
    // A plate: dense, nearly evenly spaced modes.
    { 1.0, 1.92, 2.22, 2.87, 3.42, 3.88, 4.39, 4.85, 5.24, 5.71, 6.18, 6.52, 7.03, 7.48, 7.89, 8.37, 8.81, 9.26, 9.7, 10.18, 10.59,
      11.07, 11.52, 11.94 },
    // An open pipe: every harmonic.
    { 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0, 11.0, 12.0, 13.0, 14.0, 15.0, 16.0, 17.0, 18.0, 19.0, 20.0, 21.0, 22.0, 23.0,
      24.0 },
    // A tube closed at one end: the odd harmonics.
    { 1.0, 3.0, 5.0, 7.0, 9.0, 11.0, 13.0, 15.0, 17.0, 19.0, 21.0, 23.0, 25.0, 27.0, 29.0, 31.0, 33.0, 35.0, 37.0, 39.0, 41.0, 43.0,
      45.0, 47.0 },
} };

// The membrane's modes: their order (nodal diameters) and Bessel root, and
// the largest each order's function reaches (to weigh them alike).
inline constexpr std::array<int, corpsModeCount> corpsMembraneOrders { 0, 1, 2, 0, 3, 1, 4, 2, 0, 5, 3, 6, 1, 4, 7, 2, 0, 8, 5, 3, 1, 9, 6, 4 };
inline constexpr std::array<double, corpsModeCount> corpsMembraneRoots {
    2.404826, 3.831706, 5.135622, 5.520078, 6.380162, 7.015587, 7.588342, 8.417244, 8.653728, 8.771484, 9.761023, 9.936110,
    10.173468, 11.064709, 11.086370, 11.619841, 11.791534, 12.225092, 12.338604, 13.015201, 13.323692, 13.354300, 13.589290, 14.372537
};
inline constexpr std::array<double, 10> corpsBesselPeaks { 1.0, 0.5819, 0.4865, 0.4344, 0.3997, 0.3741, 0.3541, 0.3379, 0.3244, 0.3128 };
// The plate's modes as half-waves across and along it.
inline constexpr std::array<std::array<int, 2>, corpsModeCount> corpsPlateWaves { {
    { 1, 1 }, { 2, 1 }, { 1, 2 }, { 2, 2 }, { 3, 1 }, { 1, 3 }, { 3, 2 }, { 2, 3 }, { 4, 1 }, { 3, 3 }, { 1, 4 }, { 4, 2 },
    { 2, 4 }, { 4, 3 }, { 5, 1 }, { 3, 4 }, { 5, 2 }, { 1, 5 }, { 4, 4 }, { 5, 3 }, { 2, 5 }, { 6, 1 }, { 3, 5 }, { 6, 2 },
} };

// Bessel's function of the first kind, by its series (enough below 20).
[[nodiscard]] inline double corpsBesselOf(const int order, const double x) noexcept
{
    const auto half = 0.5 * x;
    auto term = 1.0;
    for (int i = 1; i <= order; ++i)
        term *= half / static_cast<double>(i);
    auto sum = term;
    for (int s = 1; s < 200; ++s) {
        term *= -half * half / (static_cast<double>(s) * static_cast<double>(s + order));
        sum += term;
        if (static_cast<double>(s) > half && std::abs(term) < 1.0e-17)
            break;
    }
    return sum;
}

// How much a mode is heard at a place (0..1), by the object's shape: for
// the bars, the string and the pipe 0 is their middle and 1 near an end;
// for the membrane its centre and near its rim; for the plate its centre
// and near a corner; for the tube its closed end and near its open one.
[[nodiscard]] inline double corpsShapeOf(const int type, const int mode, const double place) noexcept
{
    const auto at = std::clamp(place, 0.0, 1.0);
    const auto k = static_cast<double>(std::clamp(mode, 0, corpsModeCount - 1));
    const auto along = 0.5 - 0.48 * at;
    switch (type) {
    case corpsObjectBeam:
    case corpsObjectMarimba:
        // A free bar swings most at its ends: about cos((k + 2) pi x).
        return std::abs(std::cos((k + 2.0) * resonancePi * along));
    case corpsObjectString: return std::abs(std::sin((k + 1.0) * resonancePi * along));
    case corpsObjectMembrane: {
        const auto index = static_cast<std::size_t>(std::clamp(mode, 0, corpsModeCount - 1));
        const auto order = corpsMembraneOrders[index];
        return std::abs(corpsBesselOf(order, corpsMembraneRoots[index] * 0.96 * at))
               / corpsBesselPeaks[static_cast<std::size_t>(std::clamp(order, 0, 9))];
    }
    case corpsObjectPlate: {
        const auto& waves = corpsPlateWaves[static_cast<std::size_t>(std::clamp(mode, 0, corpsModeCount - 1))];
        return std::abs(std::sin(static_cast<double>(waves[0]) * resonancePi * (0.5 - 0.46 * at))
                        * std::sin(static_cast<double>(waves[1]) * resonancePi * (0.5 - 0.31 * at)));
    }
    case corpsObjectPipe:
        // Its pressure: nothing at its open ends; the low modes stronger.
        return std::abs(std::sin((k + 1.0) * resonancePi * along)) * std::pow(k + 1.0, -0.7);
    case corpsObjectTube:
        // Most at the closed end, nothing at the open one.
        return std::abs(std::cos((2.0 * k + 1.0) * resonancePi * 0.5 * 0.92 * at)) * std::pow(k + 1.0, -0.5);
    default: return 0.0;
    }
}

// The decay knob (0..1) as the lowest mode's ring time: 0.05 s to 10 s.
[[nodiscard]] inline double corpsSecondsOf(const float decay) noexcept
{
    return 0.05 * std::pow(200.0, std::clamp(static_cast<double>(decay), 0.0, 1.0));
}

// The bank is scaled for a sound whose level falls with frequency as
// music's does (nearly pink): each mode's share of white noise's power
// weighed by (pivot / its frequency)^tilt, so a body tuned low is not
// flooded by the bass it is given.
inline constexpr double corpsLevelPivotHz = 3'500.0;
inline constexpr double corpsLevelTilt = 0.8;

// The right channel's body against the left at full width: tuned a little
// higher and heard from elsewhere.
inline constexpr double corpsWidthDetune = 0.006;
inline constexpr double corpsWidthPlace = 0.25;

// The modes one side of the body rings (0 left, 1 right): their frequency,
// how much each is heard (the loudest 1) and how long it rings. Those
// above 0.45 of the rate are left out.
struct CorpsModeSet {
    int count {};
    std::array<double, corpsModeCount> hz {};
    std::array<double, corpsModeCount> weight {};
    std::array<double, corpsModeCount> seconds {};
};
[[nodiscard]] inline CorpsModeSet corpsModesOf(const float* values, const int side, const double rate) noexcept
{
    CorpsModeSet set;
    const auto type = corpsTypeOf(values[corpsType]);
    const auto width = std::clamp(static_cast<double>(values[corpsWidth]), 0.0, 1.0);
    auto tune = std::clamp(static_cast<double>(values[corpsTune]), 20.0, 4'000.0);
    auto place = std::clamp(static_cast<double>(values[corpsPosition]), 0.0, 1.0);
    if (side == 1) {
        tune *= 1.0 + corpsWidthDetune * width;
        place += corpsWidthPlace * width;
        if (place > 1.0)
            place = 2.0 - place;
    }
    const auto stretch = 1.0 + 0.2 * std::clamp(static_cast<double>(values[corpsInharmonic]), -1.0, 1.0);
    // Wood: the upper modes die much sooner; metal: nearly as long.
    const auto damping = 1.3 - 1.1 * std::clamp(static_cast<double>(values[corpsMaterial]), 0.0, 1.0);
    const auto seconds = corpsSecondsOf(values[corpsDecay]);
    auto loudest = 1.0e-9;
    for (int mode = 0; mode < corpsModeCount; ++mode) {
        const auto ratio = std::pow(corpsRatios[static_cast<std::size_t>(type)][static_cast<std::size_t>(mode)], stretch);
        const auto hz = tune * ratio;
        if (hz >= rate * 0.45)
            break;
        const auto index = static_cast<std::size_t>(set.count++);
        set.hz[index] = hz;
        set.weight[index] = corpsShapeOf(type, mode, place);
        set.seconds[index] = std::clamp(seconds * std::pow(ratio, -damping), 0.005, 30.0);
        loudest = std::max(loudest, set.weight[index]);
    }
    for (int mode = 0; mode < set.count; ++mode)
        set.weight[static_cast<std::size_t>(mode)] /= loudest;
    return set;
}

// Its readings: the left body's modes ringing now (each one's amplitude,
// linear), in the order corpsModesOf gives them.
inline constexpr int corpsReadingCount = corpsModeCount;

// ---- Peigne --------------------------------------------------------------------------------------

// A tuned comb: the sound and a copy of it (or of what came out) a period
// later, which reinforces the frequency and its multiples.
inline constexpr int peigneModeCount = 2;
inline constexpr std::array<ResonanceChoice, peigneModeCount> peigneModes { {
    { "Rétroaction", "Feedback", "Ce qui sort revient : des pics accordés qui résonnent",
      "What comes out comes back: tuned peaks that ring" },
    { "Avance", "Feedforward", "Une seule copie retardée : des creux réguliers, sans résonance",
      "A single delayed copy: even notches, no ringing" },
} };
[[nodiscard]] inline int peigneModeOf(const float value) noexcept
{
    return std::clamp(static_cast<int>(std::lround(value)), 0, peigneModeCount - 1);
}

// The spread: the right channel's frequency times (1 + spread x this), the
// left's divided by it.
inline constexpr double peigneSpreadRatio = 0.03;
// The LFO swings the frequency up to an octave each way at full depth.
inline constexpr double peigneLfoOctaves = 1.0;
// What the right channel's LFO runs ahead of the left's at full spread (a turn).
inline constexpr double peigneLfoStereoTurn = 0.25;

// The line length (samples) that tunes the comb to `hz` with the damping's
// low-pass in its path.
[[nodiscard]] inline double peigneLineOf(const double hz, const double pole, const double rate) noexcept
{
    const auto tuned = std::clamp(hz, 5.0, rate * 0.45);
    return std::max(2.0, rate / tuned - resonanceOnePoleDelayOf(pole, tuned, rate));
}

// What the comb's output is scaled by: white noise comes out about as loud
// as it went in.
[[nodiscard]] inline double peigneNormOf(const float feedback, const int mode) noexcept
{
    const auto g = std::clamp(static_cast<double>(feedback), -0.98, 0.98);
    return mode == 0 ? std::sqrt(1.0 - g * g) : 1.0 / std::sqrt(1.0 + g * g);
}

// The comb as the values make it (its LFO at rest, the middle of its
// spread), worked out once for many frequencies.
struct PeigneShape {
    int mode {};
    double feedback {};
    double pole {};
    double line { 2.0 };
    double norm { 1.0 };
    double rate { 48'000.0 };
};
[[nodiscard]] inline PeigneShape peigneShapeOf(const float* values, const double rate) noexcept
{
    PeigneShape shape;
    shape.mode = peigneModeOf(values[peigneMode]);
    shape.feedback = std::clamp(static_cast<double>(values[peigneFeedback]), -0.98, 0.98);
    shape.pole = resonanceOnePolePoleOf(static_cast<double>(values[peigneDamping]), rate);
    shape.line = peigneLineOf(static_cast<double>(values[peigneFrequency]), shape.pole, rate);
    shape.norm = peigneNormOf(values[peigneFeedback], shape.mode);
    shape.rate = rate;
    return shape;
}

// Its gain (dB) at a frequency: what the display draws.
[[nodiscard]] inline double peigneResponseDbOf(const PeigneShape& shape, const double hz) noexcept
{
    const auto w = 2.0 * resonancePi * std::clamp(hz, 1.0, shape.rate * 0.4999) / shape.rate;
    const auto z = std::polar(1.0, -w);
    const auto lowPass = (1.0 - shape.pole) / (1.0 - shape.pole * z);
    const auto loop = shape.feedback * lowPass * std::polar(1.0, -w * shape.line);
    const auto response = shape.mode == 0 ? 1.0 / (1.0 - loop) : 1.0 + loop;
    return 20.0 * std::log10(std::max(std::abs(response) * shape.norm, 1.0e-6));
}
[[nodiscard]] inline double peigneResponseDb(const float* values, const double hz, const double rate) noexcept
{
    return peigneResponseDbOf(peigneShapeOf(values, rate), hz);
}

// Its readings: the comb's frequency now, left and right (Hz), its LFO and
// spread applied.
inline constexpr int peigneReadingCount = 2;

} // namespace cydaw::engine::resonance
