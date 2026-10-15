#pragma once

#include <cydaw/engine/BiquadDesign.h>
#include <cydaw/engine/EffectCatalogue.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

// Vocodeur, Dé-Esseur, Accordeur: what their processors (VoiceEffects.cpp) and their displays
// (VoicePanes.inc) both compute. The enums follow the catalogue's
// parameter order (EffectCatalogue.h); a helper's name never repeats an
// enumerator's (they share this namespace): `xxxOf`, `xxxApplied`…

namespace cydaw::engine::voice {

// Vocoder ("vocoder").
enum VocoderParameter : int {
    vocoderMix,
    vocoderFormant,
    vocoderAttack,
    vocoderRelease,
    vocoderPitch,
    vocoderUnvoiced,
    vocoderTone,
    vocoderOutput,
    vocoderCarrier,
    vocoderBands,
    vocoderLowBand,
    vocoderHighBand,
    vocoderBandwidth,
    vocoderGate,
    vocoderStereo,
    vocoderDetune,
    vocoderParameterCount
};

// De-Esser ("de-esser").
enum DeEsserParameter : int {
    deEsserThreshold,
    deEsserFrequency,
    deEsserRange,
    deEsserRelease,
    deEsserOutput,
    deEsserMode,
    deEsserDetectorType,
    deEsserListen,
    deEsserBandwidth,
    deEsserAttack,
    deEsserLink,
    deEsserParameterCount
};

// Tuner ("tuner").
enum TunerParameter : int {
    tunerReference,
    tunerMute,
    tunerDisplayMode,
    tunerParameterCount
};

// ---- Shared ----------------------------------------------------------------------------------

// A choice as the interface names it, with a line on what it does.
struct VoiceChoice {
    const char* french;
    const char* english;
    const char* frenchHint;
    const char* englishHint;
};

[[nodiscard]] inline int voiceChoiceOf(const float value, const int count) noexcept
{
    return std::clamp(static_cast<int>(std::lround(value)), 0, count - 1);
}

// Notes: MIDI numbers, middle C (60) is C4, A4 (69) at the reference.
inline constexpr std::array<const char*, 12> voiceNoteNamesEnglish { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
inline constexpr std::array<const char*, 12> voiceNoteNamesFrench { "Do", "Do#", "Ré", "Ré#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#",
                                                                   "Si" };
[[nodiscard]] inline int voicePitchClassOf(const int note) noexcept
{
    return ((note % 12) + 12) % 12;
}
[[nodiscard]] inline int voiceOctaveOf(const int note) noexcept
{
    return (note >= 0 ? note / 12 : (note - 11) / 12) - 1;
}
[[nodiscard]] inline double voiceHzOfNote(const double note, const double reference = 440.0) noexcept
{
    return reference * std::pow(2.0, (note - 69.0) / 12.0);
}
[[nodiscard]] inline double voiceNoteOfHz(const double hz, const double reference = 440.0) noexcept
{
    return hz > 0.0 ? 69.0 + 12.0 * std::log2(hz / std::max(1.0, reference)) : -1.0;
}

// The Q of each of two equal second-order sections in cascade whose -3 dB
// points are `octaves` apart (the band from f·2^(-octaves/2) to f·2^(octaves/2)).
[[nodiscard]] inline double voiceCascadeQ(const double octaves) noexcept
{
    const auto half = std::pow(2.0, std::max(0.01, octaves) * 0.5);
    // Each section is at -1.5 dB there: Q·x = sqrt(sqrt(2) - 1).
    return std::clamp(0.643594 / (half - 1.0 / half), 0.05, 40.0);
}

// A band-pass section's power gain (0..1) at `hz` for a centre and a Q.
[[nodiscard]] inline double voiceBandPower(const double hz, const double centre, const double q) noexcept
{
    const auto x = hz / centre - centre / hz;
    return 1.0 / (1.0 + q * q * x * x);
}

// ---- Vocodeur ----------------------------------------------------------------------------------
//
// The voice (the modulator: the effect's own input) is split into bands; how
// loud each band is shapes the same band of another sound (the carrier), so
// the carrier speaks. Each carrier band is first brought to an even level, so
// whatever the carrier, the voice's spectrum is what comes out.

inline constexpr int vocoderCarrierCount = 4;
inline constexpr std::array<VoiceChoice, vocoderCarrierCount> vocoderCarriers { {
    { "Bruit", "Noise", "Un bruit blanc : la voix chuchotée", "White noise: the voice whispered" },
    { "Scie sur une note", "Saw on a note", "Deux dents de scie légèrement désaccordées sur la note choisie : le robot",
      "Two slightly detuned saws on the chosen note: the robot" },
    { "Suivi de la voix", "Follows the voice", "Une dent de scie qui suit la hauteur de la voix : elle parle juste, en synthé",
      "A saw that follows the voice's pitch: it speaks in tune, as a synth" },
    { "Externe (sidechain)", "External (sidechain)",
      "Le son branché sur l'entrée sidechain (un synthé, une nappe) ; du bruit tant que rien n'y arrive",
      "The sound wired to the sidechain input (a synth, a pad); noise while nothing comes in" },
} };
[[nodiscard]] inline const VoiceChoice& vocoderCarrierOf(const float value) noexcept
{
    return vocoderCarriers[static_cast<std::size_t>(voiceChoiceOf(value, vocoderCarrierCount))];
}
inline constexpr int vocoderNoiseCarrier = 0;
inline constexpr int vocoderSawCarrier = 1;
inline constexpr int vocoderFollowCarrier = 2;
inline constexpr int vocoderExternalCarrier = 3;

inline constexpr int vocoderMinimumBands = 4;
inline constexpr int vocoderMaximumBands = 40;
[[nodiscard]] inline int vocoderBandCountOf(const float value) noexcept
{
    return std::clamp(static_cast<int>(std::lround(value)), vocoderMinimumBands, vocoderMaximumBands);
}
// The lowest and highest band's centres, in order and apart.
[[nodiscard]] inline double vocoderLowestOf(const float low, const float high) noexcept
{
    return std::clamp(static_cast<double>(std::min(low, high)), 20.0, 18'000.0);
}
[[nodiscard]] inline double vocoderHighestOf(const float low, const float high) noexcept
{
    return std::max(vocoderLowestOf(low, high) * 1.5, std::clamp(static_cast<double>(std::max(low, high)), 20.0, 20'000.0));
}
// A band's centre: evenly spaced in octaves from the lowest to the highest.
[[nodiscard]] inline double vocoderBandHz(const int band, const int count, const double lowest, const double highest) noexcept
{
    if (count <= 1)
        return std::sqrt(lowest * highest);
    return lowest * std::pow(highest / lowest, static_cast<double>(band) / static_cast<double>(count - 1));
}
// The Q of each of a band's two sections: at a bandwidth of 1 the band
// reaches its neighbours' midpoints at -3 dB (they cross there), at 2 it
// reaches their centres, at 0.25 it keeps to a quarter of the way.
[[nodiscard]] inline double vocoderBandQ(const int count, const double lowest, const double highest, const float bandwidth) noexcept
{
    const auto spacing = std::log2(highest / lowest) / static_cast<double>(std::max(1, count - 1));
    return voiceCascadeQ(spacing * std::clamp(static_cast<double>(bandwidth), 0.1, 4.0));
}
// A band's gain (dB, both sections) at a frequency.
[[nodiscard]] inline double vocoderBandDb(const double hz, const double centre, const double q) noexcept
{
    const auto power = voiceBandPower(hz, centre, q);
    return 20.0 * std::log10(std::max(power, 1.0e-12));
}
// The carrier's tilt: ±4 dB an octave about 1 kHz at the ends, ±18 dB at most.
[[nodiscard]] inline double vocoderToneDb(const double hz, const float tone) noexcept
{
    return std::clamp(static_cast<double>(std::clamp(tone, -1.0f, 1.0f)) * 4.0 * std::log2(std::max(hz, 1.0) / 1'000.0), -18.0, 18.0);
}
// The two saws' detune either way, in cents.
[[nodiscard]] inline double vocoderDetuneCentsOf(const float detune) noexcept
{
    return 15.0 * std::clamp(static_cast<double>(detune), 0.0, 1.0);
}
// The formant shift as a frequency ratio.
[[nodiscard]] inline double vocoderFormantRatioOf(const float semitones) noexcept
{
    return std::pow(2.0, std::clamp(static_cast<double>(semitones), -24.0, 24.0) / 12.0);
}
// Where the voice's sibilance is measured (its share of energy above it).
inline constexpr double vocoderSibilanceHz = 5'000.0;
// How the follower glides from one pitch to the next.
inline constexpr double vocoderGlideMs = 20.0;

// What the processor shows: the carrier's pitch (Hz, 0 for none), the band
// count, then each band's envelope (dB, RMS of the voice in that band).
inline constexpr int vocoderReadingPitch = 0;
inline constexpr int vocoderReadingBandCount = 1;
inline constexpr int vocoderReadingEnvelopes = 2;
inline constexpr int vocoderReadingCount = vocoderReadingEnvelopes + vocoderMaximumBands;
inline constexpr float vocoderFloorDb = -100.0f;

// ---- Dé-Esseur ---------------------------------------------------------------------------------
//
// Hears the hiss of the S and SH (a high-pass or a band at its frequency) and
// takes it down when it goes over the threshold: only there (a shelf or a
// bell that dips as far as needed) or the whole sound.

inline constexpr int deEsserModeCount = 2;
inline constexpr std::array<VoiceChoice, deEsserModeCount> deEsserModes { {
    { "Bande seule", "Band only", "Seuls les aigus (ou la bande) sont baissés : le reste ne bouge pas",
      "Only the highs (or the band) are taken down: the rest stays" },
    { "Tout le son", "Whole sound", "Tout le son est baissé pendant la sifflante : plus naturel sur une voix douce",
      "The whole sound is taken down during the sibilant: more natural on a soft voice" },
} };
inline constexpr int deEsserDetectorCount = 2;
inline constexpr std::array<VoiceChoice, deEsserDetectorCount> deEsserDetectors { {
    { "Passe-haut", "High-pass", "Écoute tout ce qui est au-dessus de la fréquence", "Listens to everything above the frequency" },
    { "Bande", "Band", "Écoute une bande autour de la fréquence (sa largeur en octaves)",
      "Listens to a band around the frequency (its width in octaves)" },
} };
[[nodiscard]] inline const VoiceChoice& deEsserModeOf(const float value) noexcept
{
    return deEsserModes[static_cast<std::size_t>(voiceChoiceOf(value, deEsserModeCount))];
}
[[nodiscard]] inline const VoiceChoice& deEsserDetectorOf(const float value) noexcept
{
    return deEsserDetectors[static_cast<std::size_t>(voiceChoiceOf(value, deEsserDetectorCount))];
}
inline constexpr int deEsserHighPassDetector = 0;
inline constexpr int deEsserBandDetector = 1;
inline constexpr int deEsserBandOnlyMode = 0;
inline constexpr int deEsserWholeMode = 1;

// The knee around the threshold, in dB (ratio infinite above it).
inline constexpr double deEsserKneeDb = 2.0;
// How much is taken (dB, positive) for a detector level: nothing under the
// knee, all that is over the threshold above it, never more than the range.
[[nodiscard]] inline double deEsserReductionOf(const double levelDb, const double thresholdDb, const double rangeDb) noexcept
{
    const auto over = levelDb - thresholdDb;
    const auto half = deEsserKneeDb * 0.5;
    double taken = 0.0;
    if (over >= half)
        taken = over;
    else if (over > -half)
        taken = (over + half) * (over + half) / (2.0 * deEsserKneeDb);
    return std::clamp(taken, 0.0, std::max(0.0, rangeDb));
}
// How often the dynamic filter is designed again, in samples.
inline constexpr int deEsserUpdateSamples = 16;
// The dynamic shelf sits under the frequency so the frequency itself gets
// nearly all of the reduction (about 90 %).
inline constexpr double deEsserShelfRatio = 0.6;

// One of the detector's two sections: a fourth-order high-pass at the
// frequency, or a band `octaves` wide (-3 dB edges) around it.
[[nodiscard]] inline BiquadCoefficients deEsserDetectorStage(const int type, const double hz, const double octaves, const int stage,
                                                            const double rate) noexcept
{
    if (type == deEsserBandDetector)
        return designEqBand(EqBandType::bandPass, hz, 0.0, voiceCascadeQ(octaves), 0, 0, rate);
    // Fourth-order Butterworth: two sections, their Qs from designEqBand.
    return designEqBand(EqBandType::lowCut, hz, 0.0, 0.7071, 1, stage, rate);
}
// The filter that takes the sibilance down, `reductionDb` (positive) at most.
[[nodiscard]] inline BiquadCoefficients deEsserDynamicFilter(const int type, const double hz, const double octaves,
                                                            const double reductionDb, const double rate) noexcept
{
    if (type == deEsserBandDetector) {
        const auto width = std::pow(2.0, std::clamp(octaves, 0.1, 4.0));
        return designEqBand(EqBandType::bell, hz, -reductionDb, std::sqrt(width) / (width - 1.0), 0, 0, rate);
    }
    return designEqBand(EqBandType::highShelf, hz * deEsserShelfRatio, -reductionDb, 0.7071, 0, 0, rate);
}
// What the detector hears of a frequency (dB), for the display.
[[nodiscard]] inline double deEsserDetectorDb(const int type, const double hz, const double octaves, const double at,
                                              const double rate) noexcept
{
    return biquadMagnitudeDb(deEsserDetectorStage(type, hz, octaves, 0, rate), at, rate)
           + biquadMagnitudeDb(deEsserDetectorStage(type, hz, octaves, 1, rate), at, rate);
}

// What the processor shows: the reduction now (dB, positive) and the
// detector's level (dB).
inline constexpr int deEsserReadingReduction = 0;
inline constexpr int deEsserReadingLevel = 1;
inline constexpr int deEsserReadingCount = 2;

// ---- Accordeur ---------------------------------------------------------------------------------
//
// Listens to the pitch and shows the nearest note and how far off it is; the
// sound passes untouched (or not at all, muted, to tune in silence).

inline constexpr int tunerDisplayCount = 2;
inline constexpr std::array<VoiceChoice, tunerDisplayCount> tunerDisplays { {
    { "Aiguille", "Needle", "Une aiguille sur ±50 cents", "A needle over ±50 cents" },
    { "Stroboscope", "Strobe", "Des bandes qui défilent d'autant plus vite que la note est fausse ; immobiles quand elle est juste",
      "Bands that scroll the faster the further off the note is; still when it is in tune" },
} };
[[nodiscard]] inline const VoiceChoice& tunerDisplayOf(const float value) noexcept
{
    return tunerDisplays[static_cast<std::size_t>(voiceChoiceOf(value, tunerDisplayCount))];
}
// In tune within this, in cents.
inline constexpr double tunerInTuneCents = 3.0;
// How long the last pitch stays shown once the sound has none.
inline constexpr double tunerHoldSeconds = 0.25;

// The nearest note of a frequency and how far off it is (cents, -50..50).
struct TunerNote {
    int note { -1 };
    double cents {};
};
[[nodiscard]] inline TunerNote tunerNoteOf(const double hz, const double reference) noexcept
{
    if (!(hz > 0.0))
        return {};
    const auto exact = voiceNoteOfHz(hz, reference);
    const auto nearest = static_cast<int>(std::lround(exact));
    return { nearest, std::clamp((exact - nearest) * 100.0, -50.0, 50.0) };
}

// What the processor shows: the frequency (Hz, 0 for none), the nearest note
// (MIDI, A4 = 69 at the reference), the cents off, the level (dB) and how
// sure the pitch is (0..1).
inline constexpr int tunerReadingHz = 0;
inline constexpr int tunerReadingNote = 1;
inline constexpr int tunerReadingCents = 2;
inline constexpr int tunerReadingLevel = 3;
inline constexpr int tunerReadingConfidence = 4;
inline constexpr int tunerReadingCount = 5;

} // namespace cydaw::engine::voice
