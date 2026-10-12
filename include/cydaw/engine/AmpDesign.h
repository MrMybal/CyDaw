#pragma once

#include <cydaw/engine/BiquadDesign.h>

#include <array>
#include <complex>

// What the amp, the cabinet and the microphone simulators compute, written
// once for their processors (AmpEffects.cpp) and for the interface that
// draws them: the amp's models and its tone stack (the passive bass, middle
// and treble network of the amplifiers, from its components), the
// cabinets' and the microphones' responses. A curve on screen is the
// processor's own arithmetic.
//
//   Ampli          (amp-sim)  a gate, a pedal, the preamp's valve stages, the
//                             tone stack, the power stage with its sag,
//                             presence and depth; oversampled
//   Baffle et micro (cab-sim) a speaker cabinet heard through one or two
//                             microphones placed on it, and the room
//   Micro          (mic-sim)  any sound as a microphone would take it: its
//                             response, its distance and angle, its preamp
//
// Each effect's values are its catalogue entry's parameters, in order; the
// enums below name their indices. Nothing here knows JUCE.

namespace cydaw::engine::amp {

// A few second-order sections, without the heap: designed on the audio
// thread when a knob moves.
struct Sections {
    std::array<BiquadCoefficients, 12> items {};
    int count {};
    void push_back(const BiquadCoefficients& section) noexcept
    {
        if (count < static_cast<int>(items.size()))
            items[static_cast<std::size_t>(count++)] = section;
    }
    [[nodiscard]] const BiquadCoefficients* begin() const noexcept { return items.data(); }
    [[nodiscard]] const BiquadCoefficients* end() const noexcept { return items.data() + count; }
    [[nodiscard]] int size() const noexcept { return count; }
};

// ---- Ampli ------------------------------------------------------------------------------------

enum Amp : int {
    ampGain,
    ampBass,
    ampMiddle,
    ampTreble,
    ampPresence,
    ampMaster,
    ampOutput,
    ampMix,
    ampModelValue,
    ampBright,
    ampDepth,
    ampSag,
    ampTight,
    ampGate,
    ampPedal,
    ampPedalDrive,
    ampPedalTone,
    ampPedalLevel,
    ampOversampling,
    // Channel B: a second amp (its model and the knobs of its faceplate) the
    // channel switch goes to; the gate, the pedal, sag, tight, the output
    // and the oversampling are both channels'.
    ampChannel,
    ampModelB,
    ampGainB,
    ampBassB,
    ampMiddleB,
    ampTrebleB,
    ampPresenceB,
    ampMasterB,
    ampBrightB,
    ampDepthB,
    ampParameterCount
};

// Channel A's values (first) and the channel B ones that stand for them.
inline constexpr std::array<std::array<int, 2>, 9> ampChannelPairs { { { ampModelValue, ampModelB },
                                                                       { ampGain, ampGainB },
                                                                       { ampBass, ampBassB },
                                                                       { ampMiddle, ampMiddleB },
                                                                       { ampTreble, ampTrebleB },
                                                                       { ampPresence, ampPresenceB },
                                                                       { ampMaster, ampMasterB },
                                                                       { ampBright, ampBrightB },
                                                                       { ampDepth, ampDepthB } } };
// A value's index on a channel (0 A, 1 B): B's own for those it has, the
// same for those both share.
[[nodiscard]] constexpr int ampChannelIndex(const int index, const int channel) noexcept
{
    if (channel == 1)
        for (const auto& pair : ampChannelPairs)
            if (pair[0] == index)
                return pair[1];
    return index;
}

// The passive tone stack's parts (ohms, farads): R1 the treble pot, R2 the
// bass pot, R3 the middle pot, R4 and the three capacitors fixed.
struct ToneStackParts {
    double r1 {};
    double r2 {};
    double r3 {};
    double r4 {};
    double c1 {};
    double c2 {};
    double c3 {};
};

struct AmpModel {
    const char* french;
    const char* english;
    const char* frenchHint;
    const char* englishHint;
    // Preamp: how many valve stages, the gain the knob spans (dB, all
    // stages together), each stage's bias (its asymmetry), the coupling
    // high-pass and the smoothing low-pass between stages.
    int stages;
    double minimumGainDb;
    double maximumGainDb;
    double bias;
    double couplingHz;
    double smoothingHz;
    // Its input high-pass, and a boost before the last stage (a lead
    // channel's push in the middle).
    double inputHighPassHz;
    double midBoostHz;
    double midBoostDb;
    ToneStackParts stack;
    // The level after the tone stack, into the power stage.
    double stackMakeupDb;
    // Power stage: how hard the master at 10 drives it (dB), how soft its
    // knee (higher: harder), where presence and depth act.
    double powerDriveDb;
    double powerKnee;
    double presenceHz;
    double depthHz;
    // A bass amp leaves its lows alone.
    bool bass;
};

inline constexpr int ampModelCount = 8;
inline constexpr std::array<AmpModel, ampModelCount> ampModels { {
    { "Clean US", "Clean US", "Propre et brillant, graves ronds, creux dans les médiums",
      "Clean and bright, round lows, scooped mids", 2, 0.0, 32.0, 0.18, 30.0, 11'000.0, 30.0, 0.0, 0.0,
      { 250e3, 250e3, 10e3, 100e3, 250e-12, 100e-9, 47e-9 }, 14.0, 18.0, 2.2, 3'200.0, 95.0, false },
    { "Crunch UK", "Crunch UK", "Le crunch britannique des années 60-70 : médiums en avant, mordant",
      "British crunch of the 60s and 70s: forward mids, bite", 2, 8.0, 42.0, 0.28, 40.0, 9'000.0, 40.0, 0.0, 0.0,
      { 220e3, 1e6, 22e3, 33e3, 470e-12, 22e-9, 22e-9 }, 5.0, 16.0, 2.6, 3'500.0, 100.0, false },
    { "Lead UK", "Lead UK", "Le canal saturé britannique des années 80 : plus de gain, médiums poussés",
      "The 80s British lead channel: more gain, pushed mids", 3, 18.0, 58.0, 0.3, 60.0, 8'000.0, 60.0, 750.0, 4.0,
      { 220e3, 1e6, 22e3, 33e3, 470e-12, 22e-9, 22e-9 }, 5.0, 14.0, 2.8, 3'500.0, 100.0, false },
    { "Moderne", "Modern", "Gain moderne, serré et grave, pour le métal",
      "Modern high gain, tight and deep, for metal", 4, 26.0, 74.0, 0.32, 120.0, 7'000.0, 110.0, 900.0, 2.0,
      { 250e3, 250e3, 25e3, 47e3, 250e-12, 100e-9, 47e-9 }, 8.0, 14.0, 3.0, 3'800.0, 85.0, false },
    { "Boutique", "Boutique", "Saturation douce et chantante, dynamique, pour les solos",
      "Smooth, singing overdrive that follows the playing, for solos", 3, 10.0, 50.0, 0.22, 45.0, 6'000.0, 45.0, 1'100.0, 2.5,
      { 250e3, 1e6, 25e3, 56e3, 250e-12, 20e-9, 20e-9 }, 6.0, 12.0, 2.2, 3'000.0, 100.0, false },
    { "Chime", "Chime", "Classe A carillonnant : aigus scintillants, compression naturelle",
      "Chiming class A: sparkling highs, natural compression", 2, 4.0, 40.0, 0.35, 35.0, 12'000.0, 50.0, 0.0, 0.0,
      { 500e3, 1e6, 22e3, 56e3, 220e-12, 22e-9, 22e-9 }, 6.0, 20.0, 1.8, 4'500.0, 110.0, false },
    { "Basse vintage", "Vintage bass", "Ampli basse à lampes : grave plein, grogne quand on pousse",
      "Valve bass amp: full lows, growls when pushed", 2, 0.0, 34.0, 0.25, 15.0, 7'000.0, 15.0, 0.0, 0.0,
      { 250e3, 1e6, 25e3, 56e3, 250e-12, 20e-9, 20e-9 }, 6.0, 16.0, 2.4, 2'500.0, 60.0, true },
    { "Basse moderne", "Modern bass", "Ampli basse moderne : propre, médiums creusés, saturation en option",
      "Modern bass amp: clean, scooped mids, drive when wanted", 2, 0.0, 40.0, 0.15, 15.0, 10'000.0, 15.0, 0.0, 0.0,
      { 250e3, 250e3, 10e3, 100e3, 250e-12, 100e-9, 47e-9 }, 14.0, 12.0, 2.8, 3'000.0, 55.0, true },
} };

[[nodiscard]] const AmpModel& ampModel(int index) noexcept;

// The tone stack at a sample rate: a third-order filter (b, a: a[0] = 1),
// from its pots (0..10 as the knobs, the bass pot's taper logarithmic).
struct ThirdOrder {
    std::array<double, 4> b {};
    std::array<double, 4> a { 1.0, 0.0, 0.0, 0.0 };
};
[[nodiscard]] ThirdOrder toneStack(const ToneStackParts& parts, double bass, double middle, double treble, double sampleRate) noexcept;
[[nodiscard]] std::complex<double> response(const ThirdOrder& filter, double hz, double sampleRate) noexcept;

// The amp's linear sections around its valves, in order: the input
// high-pass (its model's or the tight knob's, the higher), the bright
// switch, the middle boost, then after the tone stack presence and depth.
struct AmpVoicing {
    Sections before; // into the first stage
    BiquadCoefficients midBoost;            // before the last stage
    ThirdOrder stack;                       // after the stages
    double stackMakeup { 1.0 };
    Sections after;  // after the power stage
};
[[nodiscard]] AmpVoicing ampVoicing(const std::array<float, ampParameterCount>& values, double sampleRate) noexcept;

// The values as a channel plays them: channel B's (1) in channel A's
// places; channel A (0) as they are. What the voicing, the curve and the
// valves are computed from.
[[nodiscard]] std::array<float, ampParameterCount> ampChannelValues(const std::array<float, ampParameterCount>& values,
                                                                    int channel) noexcept;

// The amp's tone (its linear parts) at a frequency, dB: the curve drawn.
[[nodiscard]] double ampToneDb(const std::array<float, ampParameterCount>& values, double hz, double sampleRate) noexcept;

// One preamp stage's valve: inverting, its bias making it asymmetric.
[[nodiscard]] inline double valveStage(const double x, const double bias) noexcept
{
    return -(std::tanh(x + bias) - std::tanh(bias));
}
// The power stage: a soft knee, symmetrical (push-pull).
[[nodiscard]] inline double powerStage(const double x, const double knee) noexcept
{
    return x / std::pow(1.0 + std::pow(std::abs(x), knee), 1.0 / knee);
}
// Each stage's gain for the gain knob (0..10).
[[nodiscard]] double stageGain(const AmpModel& model, double gainKnob) noexcept;
// The level after the preamp for its gain (the stages' small-signal gain
// brought back towards a constant level): what keeps the gain knob from
// being a volume knob.
[[nodiscard]] double preampLevel(const AmpModel& model, double gainKnob) noexcept;
// The stages on a value, filters aside: the transfer curve drawn.
[[nodiscard]] double preampCurve(const AmpModel& model, double gainKnob, double x) noexcept;

// ---- Baffle et micro -----------------------------------------------------------------------------

enum Cab : int {
    cabPositionA,
    cabDistanceA,
    cabPositionB,
    cabDistanceB,
    cabBlend,
    cabRoom,
    cabLowCut,
    cabOutput,
    cabModelValue,
    cabMicA,
    cabAngleA,
    cabMicB,
    cabAngleB,
    cabMicBOn,
    cabPolarityB,
    cabAlign,
    cabHighCut,
    cabResonance,
    cabStereo,
    cabMix,
    // The room: its size (0 a booth, 0.25 the small live room, 1 a hall's
    // first reflections) and how bright its walls (Hz).
    cabRoomSize,
    cabRoomTone,
    cabParameterCount
};

struct CabModel {
    const char* french;
    const char* english;
    const char* frenchHint;
    const char* englishHint;
    // The speakers' low end: where it falls (an open back falls steeper and
    // higher), its resonance.
    double lowHz;
    bool openBack;
    double resonanceHz;
    double resonanceDb;
    // The body, the cone's break-up peak and the dip under it.
    double bodyHz;
    double bodyDb;
    double breakupHz;
    double breakupDb;
    double dipHz;
    double dipDb;
    // Where the highs go, a microphone at the centre and at the edge.
    double topCentreHz;
    double topEdgeHz;
    // The speakers' diameter (inches) and how many: for the picture.
    int speakers;
    double inches;
};

inline constexpr int cabModelCount = 8;
inline constexpr std::array<CabModel, cabModelCount> cabModels { {
    { "1x12 ouvert", "1x12 open back", "Combo ouvert : aéré, grave léger", "Open-back combo: airy, light lows", 90.0, true, 110.0, 3.0,
      300.0, -1.0, 2'500.0, 4.0, 1'300.0, -2.0, 6'500.0, 3'200.0, 1, 12.0 },
    { "2x12 ouvert", "2x12 open back", "Le combo américain : ample et scintillant", "The American combo: wide and sparkling", 75.0,
      true, 105.0, 3.5, 250.0, 0.0, 2'800.0, 4.0, 1'500.0, -2.0, 7'000.0, 3'500.0, 2, 12.0 },
    { "2x12 fermé", "2x12 closed back", "Fermé : grave serré, médiums denses", "Closed back: tight lows, dense mids", 70.0, false, 95.0,
      4.0, 200.0, 1.5, 2'400.0, 4.5, 1'200.0, -2.5, 6'000.0, 3'000.0, 2, 12.0 },
    { "4x12 moderne", "4x12 modern", "La grosse caisse moderne : grave profond, haut-médium agressif",
      "The modern big box: deep lows, aggressive upper mids", 70.0, false, 90.0, 5.0, 180.0, 2.0, 2'600.0, 6.0, 1'600.0, -3.0, 5'800.0,
      3'000.0, 4, 12.0 },
    { "4x12 vintage", "4x12 vintage", "Haut-parleurs vintage : chaud, doux, médiums ronds", "Vintage speakers: warm, smooth, round mids",
      75.0, false, 100.0, 4.5, 220.0, 1.5, 2'000.0, 4.0, 1'100.0, -2.0, 5'000.0, 2'600.0, 4, 12.0 },
    { "1x10 petit", "1x10 small", "Petit combo : médiums nasillards, peu de grave", "Small combo: nasal mids, few lows", 130.0, true,
      160.0, 3.0, 400.0, -1.0, 3'000.0, 3.0, 1'700.0, -2.0, 5'500.0, 3'000.0, 1, 10.0 },
    { "1x15 basse", "1x15 bass", "Un 15 pouces : grave rond et profond", "One 15-inch: round, deep lows", 45.0, false, 70.0, 4.0, 150.0,
      1.0, 1'500.0, 2.0, 900.0, -2.0, 3'800.0, 2'200.0, 1, 15.0 },
    { "8x10 basse", "8x10 bass", "Le frigo : grave énorme, attaque claire", "The fridge: huge lows, clear attack", 50.0, false, 80.0, 5.0,
      200.0, 2.0, 2'500.0, 3.0, 1'200.0, -2.0, 5'000.0, 2'800.0, 8, 10.0 },
} };

[[nodiscard]] const CabModel& cabModel(int index) noexcept;

// ---- The microphones (the cabinet's and Micro's) -------------------------------------------------

enum class Pattern : int { omni = 1, cardioid, hypercardioid, figureEight };

struct MicModel {
    const char* french;
    const char* english;
    const char* frenchHint;
    const char* englishHint;
    // Its low end (a band-limited one falls steeper), its warmth, its
    // presence peak, its air shelf, where its top falls.
    double lowCutHz;
    bool steep;
    double warmthHz;
    double warmthDb;
    double presenceHz;
    double presenceDb;
    double presenceQ;
    double airHz;
    double airDb;
    double topHz;
    // A second resonance (a horn's), dB 0: none.
    double resonanceHz;
    double resonanceDb;
    double resonanceQ;
    // The bass a directional capsule gains at 1 cm (proximity), its own
    // pattern, how much of the highs it loses at 90 degrees.
    double proximityDb;
    Pattern pattern;
    double offAxisDb;
    // Its own colour: 0 none, 1 valve, 2 clipping (a cheap speaker), and
    // its drive and hiss.
    int saturation;
    double driveDb;
    double noiseDb;
};

// The first seven go on a cabinet; Micro has them all.
inline constexpr int cabMicCount = 7;
inline constexpr int micModelCount = 12;
inline constexpr std::array<MicModel, micModelCount> micModels { {
    { "Dynamique", "Dynamic", "Le dynamique de scène : présence marquée, grave serré", "The stage dynamic: strong presence, tight lows",
      80.0, false, 200.0, 0.0, 5'500.0, 5.0, 1.2, 10'000.0, -4.0, 15'000.0, 0.0, 0.0, 1.0, 9.0, Pattern::cardioid, 10.0, 0, 0.0,
      -90.0 },
    { "Dynamique large", "Large dynamic", "Gros dynamique : plein, médiums francs", "Large dynamic: full, honest mids", 45.0, false, 200.0,
      1.0, 4'000.0, 3.0, 0.9, 10'000.0, -2.0, 17'000.0, 0.0, 0.0, 1.0, 7.0, Pattern::cardioid, 8.0, 0, 0.0, -90.0 },
    { "Ruban", "Ribbon", "Doux et sombre, grave généreux, aigus arrondis", "Smooth and dark, generous lows, rounded highs", 30.0, false,
      250.0, 2.5, 3'000.0, -1.0, 0.7, 5'000.0, -7.0, 12'000.0, 0.0, 0.0, 1.0, 12.0, Pattern::figureEight, 4.0, 0, 0.0, -90.0 },
    { "Statique large membrane", "Large condenser", "Le micro de studio : détaillé, aigus aérés", "The studio microphone: detailed, airy highs",
      30.0, false, 200.0, 0.0, 6'000.0, 1.0, 0.8, 10'000.0, 2.5, 20'000.0, 0.0, 0.0, 1.0, 6.0, Pattern::cardioid, 6.0, 0, 0.0, -90.0 },
    { "Statique petite membrane", "Small condenser", "Neutre et précis, transitoires nettes", "Neutral and precise, clean transients", 25.0,
      false, 200.0, 0.0, 8'000.0, 1.5, 1.0, 12'000.0, 1.0, 20'000.0, 0.0, 0.0, 1.0, 4.0, Pattern::cardioid, 4.0, 0, 0.0, -90.0 },
    { "Vintage à lampe", "Vintage valve", "Statique à lampe ancien : chaud, velouté, un peu de grain",
      "Old valve condenser: warm, velvety, a little grain", 35.0, false, 150.0, 2.5, 3'000.0, 2.0, 0.6, 12'000.0, -2.0, 16'000.0, 0.0,
      0.0, 1.0, 7.0, Pattern::cardioid, 6.0, 1, 0.0, -90.0 },
    { "Dynamique broadcast", "Broadcast dynamic", "Le micro de radio et de podcast : chaud, proche, doux",
      "The radio and podcast microphone: warm, close, smooth", 50.0, false, 200.0, 1.5, 3'500.0, 2.5, 0.8, 9'000.0, -3.0, 14'000.0,
      0.0, 0.0, 1.0, 10.0, Pattern::cardioid, 9.0, 0, 0.0, -90.0 },
    { "Micro-cravate", "Lavalier", "Accroché au vêtement : résonance de poitrine, aigus relevés",
      "Clipped on clothes: chest resonance, lifted highs", 60.0, false, 700.0, 3.0, 4'000.0, -2.0, 1.0, 8'000.0, 3.0, 18'000.0, 0.0,
      0.0, 1.0, 0.0, Pattern::omni, 2.0, 0, 0.0, -90.0 },
    { "Téléphone", "Telephone", "Une ligne de téléphone : bande étroite, un peu saturée", "A phone line: narrow band, a little distorted",
      300.0, true, 200.0, 0.0, 1'800.0, 4.0, 1.5, 10'000.0, 0.0, 3'400.0, 0.0, 0.0, 1.0, 0.0, Pattern::omni, 2.0, 2, 8.0, -60.0 },
    { "Mégaphone", "Megaphone", "Pavillon et petit haut-parleur : nasal et saturé", "Horn and small speaker: nasal and distorted", 500.0,
      true, 200.0, 0.0, 1'600.0, 9.0, 4.0, 10'000.0, 0.0, 4'200.0, 3'200.0, 6.0, 5.0, 0.0, Pattern::omni, 2.0, 2, 18.0, -90.0 },
    { "Radio ancienne", "Old radio", "Le poste d'autrefois : médiums, souffle", "The old wireless: mids and hiss", 200.0, true, 200.0,
      0.0, 1'200.0, 3.0, 1.0, 10'000.0, 0.0, 5'000.0, 0.0, 0.0, 1.0, 0.0, Pattern::omni, 2.0, 1, 6.0, -50.0 },
    { "Talkie-walkie", "Walkie-talkie", "Radio portative : très étroit, très saturé, bruité", "Handheld radio: very narrow, very distorted, noisy",
      450.0, true, 200.0, 0.0, 1'500.0, 5.0, 2.0, 10'000.0, 0.0, 2'700.0, 0.0, 0.0, 1.0, 0.0, Pattern::omni, 2.0, 2, 24.0, -42.0 },
} };

[[nodiscard]] const MicModel& micModel(int index) noexcept;

// A pattern's pick-up at an angle (degrees), and the proximity bass it
// gains at a distance (cm).
[[nodiscard]] double patternGain(Pattern pattern, double degrees) noexcept;
[[nodiscard]] double proximityDb(const MicModel& model, Pattern pattern, double centimetres) noexcept;

// One microphone's sections: its response, the proximity at its distance,
// its angle; on a cabinet also where on the speaker it points (0 the
// centre, 1 the edge) and the cabinet's top.
[[nodiscard]] Sections micSections(const MicModel& model, Pattern pattern, double centimetres,
                                                          double degrees, double sampleRate) noexcept;
// The cabinet without its microphones: its low end, resonance, body and dip.
[[nodiscard]] Sections cabCoreSections(const CabModel& model, double resonance, double sampleRate) noexcept;
// What a microphone's place on the speaker does: the break-up peak, the top.
[[nodiscard]] Sections cabPlaceSections(const CabModel& model, double position, double degrees,
                                                               double sampleRate) noexcept;

// Sections' response at a frequency.
[[nodiscard]] std::complex<double> response(const Sections& sections, double hz, double sampleRate) noexcept;

// The sound travels 343 m/s: a microphone's delay (seconds) at a distance.
[[nodiscard]] inline double flightSeconds(const double centimetres) noexcept
{
    return centimetres * 0.01 / 343.0;
}

// The level a cabinet is brought back by (dB): what its curve averages
// over the guitar's band (100 Hz to 4 kHz), so a cabinet or a microphone
// changed changes the colour, not the volume.
[[nodiscard]] double cabMakeupDb(const std::array<float, cabParameterCount>& values, double sampleRate) noexcept;
// The cabinet as heard (dB): through microphone A (which 0), B (1), or
// both as set (2), the room aside, its makeup in.
[[nodiscard]] double cabResponseDb(const std::array<float, cabParameterCount>& values, int which, double hz, double sampleRate) noexcept;

// ---- Micro ---------------------------------------------------------------------------------------

enum Mic : int {
    micDistance,
    micAngle,
    micDrive,
    micLowCut,
    micHighCut,
    micRoom,
    micOutput,
    micMix,
    micModelValue,
    micPattern,
    micPreamp,
    micNoise,
    // The room, as on the cabinet.
    micRoomSize,
    micRoomTone,
    micParameterCount
};

// The pattern a Micro uses: its own (0) or the one chosen.
[[nodiscard]] Pattern micPatternOf(const std::array<float, micParameterCount>& values) noexcept;
// Its level brought back over the voice's band (300 Hz to 3 kHz), its
// pattern's pick-up aside (behind a cardioid stays quiet).
[[nodiscard]] double micMakeupDb(const std::array<float, micParameterCount>& values, double sampleRate) noexcept;
// Its response (dB) at a frequency, the drive and the room aside, its
// makeup in.
[[nodiscard]] double micResponseDb(const std::array<float, micParameterCount>& values, double hz, double sampleRate) noexcept;
// Its preamp's curve: 0 transistor, 1 valve, 2 transformer.
[[nodiscard]] double preampSaturate(int kind, double x) noexcept;

} // namespace cydaw::engine::amp
