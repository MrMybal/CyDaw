#pragma once

#include <cydaw/engine/EffectCatalogue.h>

#include <array>

// The MIDI effects' parameters, in their catalogue order (EffectCatalogue.h).

namespace cydaw::engine::midifx {

// Arpeggiator ("arpege").
enum ArpegeParameter : int {
    arpegeGate,
    arpegeSwing,
    arpegeVelocity,
    arpegeRate,
    arpegeMode,
    arpegeOctaves,
    arpegeVelocityMode,
    arpegeLatch,
    arpegeRetrigger,
    arpegeParameterCount
};

// Chords ("accords").
enum AccordsParameter : int {
    accordsVelocity,
    accordsStrum,
    accordsChord,
    accordsInterval1,
    accordsInterval2,
    accordsInterval3,
    accordsInterval4,
    accordsInterval5,
    accordsInterval6,
    accordsInversion,
    accordsSpread,
    accordsStrumMode,
    accordsKey,
    accordsScale,
    accordsParameterCount
};

// Scale ("gamme").
enum GammeParameter : int {
    gammeTranspose,
    gammeDegrees,
    gammeKey,
    gammeScale,
    gammeMode,
    gammeParameterCount
};

// Note Echo ("echo-notes").
enum EchoNotesParameter : int {
    echoNotesTime,
    echoNotesDecay,
    echoNotesPitch,
    echoNotesSync,
    echoNotesRepeats,
    echoNotesParameterCount
};

// Random ("alea").
enum AleaParameter : int {
    aleaTiming,
    aleaVelocity,
    aleaChance,
    aleaPitchChance,
    aleaPitchRange,
    aleaParameterCount
};

// Note Length ("duree").
enum DureeParameter : int {
    dureeLength,
    dureeFactor,
    dureeMode,
    dureeSync,
    dureeParameterCount
};

// Step Sequencer ("sequenceur").
enum SequenceurParameter : int {
    sequenceurSwing,
    sequenceurGate,
    sequenceurMode,
    sequenceurSteps,
    sequenceurRate,
    sequenceurDirection,
    sequenceurRoot,
    sequenceurStep1On,
    sequenceurStep1Note,
    sequenceurStep1Velocity,
    sequenceurStep1Length,
    sequenceurStep2On,
    sequenceurStep2Note,
    sequenceurStep2Velocity,
    sequenceurStep2Length,
    sequenceurStep3On,
    sequenceurStep3Note,
    sequenceurStep3Velocity,
    sequenceurStep3Length,
    sequenceurStep4On,
    sequenceurStep4Note,
    sequenceurStep4Velocity,
    sequenceurStep4Length,
    sequenceurStep5On,
    sequenceurStep5Note,
    sequenceurStep5Velocity,
    sequenceurStep5Length,
    sequenceurStep6On,
    sequenceurStep6Note,
    sequenceurStep6Velocity,
    sequenceurStep6Length,
    sequenceurStep7On,
    sequenceurStep7Note,
    sequenceurStep7Velocity,
    sequenceurStep7Length,
    sequenceurStep8On,
    sequenceurStep8Note,
    sequenceurStep8Velocity,
    sequenceurStep8Length,
    sequenceurStep9On,
    sequenceurStep9Note,
    sequenceurStep9Velocity,
    sequenceurStep9Length,
    sequenceurStep10On,
    sequenceurStep10Note,
    sequenceurStep10Velocity,
    sequenceurStep10Length,
    sequenceurStep11On,
    sequenceurStep11Note,
    sequenceurStep11Velocity,
    sequenceurStep11Length,
    sequenceurStep12On,
    sequenceurStep12Note,
    sequenceurStep12Velocity,
    sequenceurStep12Length,
    sequenceurStep13On,
    sequenceurStep13Note,
    sequenceurStep13Velocity,
    sequenceurStep13Length,
    sequenceurStep14On,
    sequenceurStep14Note,
    sequenceurStep14Velocity,
    sequenceurStep14Length,
    sequenceurStep15On,
    sequenceurStep15Note,
    sequenceurStep15Velocity,
    sequenceurStep15Length,
    sequenceurStep16On,
    sequenceurStep16Note,
    sequenceurStep16Velocity,
    sequenceurStep16Length,
    sequenceurParameterCount
};

// Note Repeat ("roulement").
enum RoulementParameter : int {
    roulementRate,
    roulementGate,
    roulementRamp,
    roulementSync,
    roulementParameterCount
};

// ---- What the effects and their cards share ---------------------------------------------------

// A name in both languages, and what it means.
struct Named {
    const char* french;
    const char* english;
};

// The keys, C first.
inline constexpr std::array<const char*, 12> keyNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

// The scales: which of the twelve pitch classes from the key each holds.
struct ScaleShape {
    Named name;
    std::array<bool, 12> holds;
};
inline constexpr std::array<ScaleShape, 12> scaleShapes { {
    { { "Chromatique", "Chromatic" }, { true, true, true, true, true, true, true, true, true, true, true, true } },
    { { "Majeur", "Major" }, { true, false, true, false, true, true, false, true, false, true, false, true } },
    { { "Mineur naturel", "Natural minor" }, { true, false, true, true, false, true, false, true, true, false, true, false } },
    { { "Mineur harmonique", "Harmonic minor" }, { true, false, true, true, false, true, false, true, true, false, false, true } },
    { { "Mineur mélodique", "Melodic minor" }, { true, false, true, true, false, true, false, true, false, true, false, true } },
    { { "Pentatonique majeure", "Major pentatonic" }, { true, false, true, false, true, false, false, true, false, true, false, false } },
    { { "Pentatonique mineure", "Minor pentatonic" }, { true, false, false, true, false, true, false, true, false, false, true, false } },
    { { "Blues", "Blues" }, { true, false, false, true, false, true, true, true, false, false, true, false } },
    { { "Dorien", "Dorian" }, { true, false, true, true, false, true, false, true, false, true, true, false } },
    { { "Phrygien", "Phrygian" }, { true, true, false, true, false, true, false, true, true, false, true, false } },
    { { "Lydien", "Lydian" }, { true, false, true, false, true, false, true, true, false, true, false, true } },
    { { "Mixolydien", "Mixolydian" }, { true, false, true, false, true, true, false, true, false, true, true, false } },
} };

// Whether a MIDI note is in a scale of a key.
[[nodiscard]] constexpr bool inScale(const int note, const int key, const int scale) noexcept
{
    const auto& shape = scaleShapes[static_cast<std::size_t>(scale < 0 ? 0 : scale > 11 ? 11 : scale)];
    const auto degree = ((note - key) % 12 + 12) % 12;
    return shape.holds[static_cast<std::size_t>(degree)];
}

// The chords: their intervals from the root (up to six notes), and how
// many notes a chord of that kind holds when it follows a scale.
struct ChordShape {
    Named name;
    std::array<int, 6> intervals;
    int count;
    int diatonicCount;
};
inline constexpr int customChord = 11;
inline constexpr std::array<ChordShape, 12> chordShapes { {
    { { "Majeur", "Major" }, { 0, 4, 7, 0, 0, 0 }, 3, 3 },
    { { "Mineur", "Minor" }, { 0, 3, 7, 0, 0, 0 }, 3, 3 },
    { { "Quinte", "Fifth" }, { 0, 7, 0, 0, 0, 0 }, 2, 2 },
    { { "Sus2", "Sus2" }, { 0, 2, 7, 0, 0, 0 }, 3, 3 },
    { { "Sus4", "Sus4" }, { 0, 5, 7, 0, 0, 0 }, 3, 3 },
    { { "Septième", "Seventh" }, { 0, 4, 7, 10, 0, 0 }, 4, 4 },
    { { "Majeur 7", "Major 7" }, { 0, 4, 7, 11, 0, 0 }, 4, 4 },
    { { "Mineur 7", "Minor 7" }, { 0, 3, 7, 10, 0, 0 }, 4, 4 },
    { { "Neuvième", "Ninth" }, { 0, 4, 7, 10, 14, 0 }, 5, 5 },
    { { "Ajout de neuvième", "Add 9" }, { 0, 4, 7, 14, 0, 0 }, 4, 3 },
    { { "Octaves", "Octaves" }, { 0, 12, 0, 0, 0, 0 }, 2, 2 },
    { { "Personnalisé", "Custom" }, { 0, 0, 0, 0, 0, 0 }, 1, 3 },
} };

// The arpeggiator's ways.
inline constexpr std::array<Named, 9> arpModes { {
    { "Vers le haut", "Up" }, { "Vers le bas", "Down" }, { "Haut puis bas", "Up and down" }, { "Bas puis haut", "Down and up" },
    { "Dans l'ordre joué", "As played" }, { "Au hasard", "Random" }, { "En accord", "Chord" }, { "Se rapprochent", "Converge" },
    { "S'écartent", "Diverge" },
} };
inline constexpr std::array<Named, 3> arpVelocities { { { "Jouée", "As played" }, { "Fixe", "Fixed" }, { "Accentuée", "Accented" } } };
inline constexpr std::array<Named, 3> strumDirections { { { "Vers le haut", "Up" }, { "Vers le bas", "Down" }, { "Alternée", "Alternate" } } };
inline constexpr std::array<Named, 4> scaleModes { {
    { "La plus proche", "Nearest" }, { "Au-dessus", "Up" }, { "En dessous", "Down" }, { "Retirer les autres", "Remove the others" },
} };
inline constexpr std::array<Named, 4> lengthModes { {
    { "Temps fixe", "Fixed time" }, { "Valeur de note", "Note value" }, { "Étirée", "Stretched" }, { "Legato", "Legato" },
} };
inline constexpr std::array<Named, 2> sequencerModes { { { "Touche tenue", "Held key" }, { "Avec le transport", "With the transport" } } };
inline constexpr std::array<Named, 4> sequencerDirections { {
    { "En avant", "Forward" }, { "En arrière", "Backward" }, { "Aller-retour", "Ping-pong" }, { "Au hasard", "Random" },
} };

// A step of the sequencer: its four values' places among the parameters.
[[nodiscard]] constexpr int sequencerStep(const int step, const int value) noexcept
{
    return sequenceurStep1On + step * 4 + value;
}

} // namespace cydaw::engine::midifx
