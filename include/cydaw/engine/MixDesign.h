#pragma once

#include <cydaw/engine/BiquadDesign.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

// What the mixing effects compute, written once for their processors
// (MixEffects.cpp) and for the interface that draws them: a curve on screen
// is the processor's own arithmetic.
//
//   Pression    (pression)    compression up and down on three bands, the
//                             quiet raised and the loud held, all at once
//   Fournaise   (fournaise)   analog saturation of five characters, with its
//                             filters and an automatic level
//   Écho Bande  (echo-bande)  a tape echo: three playback heads in twelve
//                             modes, the tape's wear and wobble, a spring
//   Multibande Pro (multiband-pro)  four bands, each compressed, expanded or
//                             raised, its crossovers and thresholds dragged
//   Velours     (velours)     resonances found in the spectrum and calmed,
//                             only while they ring
//   Patine      (patine)      noise, wobble, distortion, digital wear, a small
//                             room, magnetic tape: the sound made old
//   Sonomètre   (sonometre)   loudness (LUFS) against a target; the sound
//                             passes untouched
//
// Each effect's values are its catalogue entry's parameters, in order; the
// enums below name their indices. Nothing here knows JUCE.

namespace cydaw::engine::mix {

// A biquad and its state, for the arithmetic here (the processors keep
// their own).
struct BiquadRun {
    BiquadCoefficients c;
    double z1 {};
    double z2 {};
    double operator()(const double x) noexcept
    {
        const auto y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }
};

// A Linkwitz-Riley crossover of 24 dB per octave: two Butterworth sections
// on each side. Low and high summed are flat (an all-pass).
[[nodiscard]] inline BiquadCoefficients crossoverLowPass(const double hz, const double rate) noexcept
{
    return designEqBand(EqBandType::highCut, hz, 0.0, std::numbers::sqrt2 * 0.5, 0, 0, rate);
}
[[nodiscard]] inline BiquadCoefficients crossoverHighPass(const double hz, const double rate) noexcept
{
    return designEqBand(EqBandType::lowCut, hz, 0.0, std::numbers::sqrt2 * 0.5, 0, 0, rate);
}

// ---- Pression ---------------------------------------------------------------------------------

enum Pression : int {
    // The sockets.
    pressionDepth,
    pressionTime,
    pressionUpward,
    pressionDownward,
    pressionLowGain,
    pressionMidGain,
    pressionHighGain,
    pressionOutput,
    // The slots: the input, the crossovers, then three bands of
    // pressionBandFields, then the band heard alone.
    pressionInput,
    pressionCrossLow,
    pressionCrossHigh,
    pressionBandBase
};
enum PressionBandField : int { bandDownThreshold, bandDownRatio, bandUpThreshold, bandUpRatio, bandAttack, bandRelease, pressionBandFields };
inline constexpr int pressionBands = 3;
[[nodiscard]] constexpr int pressionBandIndex(const int band, const int field) noexcept
{
    return pressionBandBase + band * pressionBandFields + field;
}
inline constexpr int pressionSolo = pressionBandIndex(pressionBands, 0);
inline constexpr int pressionParameterCount = pressionSolo + 1;
// Each band's gain knob.
inline constexpr std::array<int, pressionBands> pressionBandGain { pressionLowGain, pressionMidGain, pressionHighGain };

// A band's thresholds (dB) and ratios.
struct PressionBand {
    double downThreshold { -30.0 };
    double downRatio { 66.7 };
    double upThreshold { -40.0 };
    double upRatio { 4.17 };
};
[[nodiscard]] inline PressionBand pressionBand(const float* values, const int band) noexcept
{
    return { values[pressionBandIndex(band, bandDownThreshold)], values[pressionBandIndex(band, bandDownRatio)],
             values[pressionBandIndex(band, bandUpThreshold)], values[pressionBandIndex(band, bandUpRatio)] };
}

// The level the bands are brought back to: what a band does to a sound at
// -18 dB is undone, so the loud is held and the quiet raised about it.
inline constexpr double pressionReferenceDb = -18.0;
// Silence is not raised past what a sound at -90 dB would be.
inline constexpr double pressionFloorDb = -90.0;

// A band's gain (dB) at a level (dB), the amounts (0..1) applied: down above
// its upper threshold, up below its lower one.
[[nodiscard]] inline double pressionGainDb(const PressionBand& band, const double levelDb, const double downward,
                                           const double upward) noexcept
{
    auto gain = 0.0;
    if (levelDb > band.downThreshold)
        gain -= (levelDb - band.downThreshold) * (1.0 - 1.0 / std::max(1.0, band.downRatio)) * downward;
    const auto floor = std::max(levelDb, pressionFloorDb);
    if (floor < band.upThreshold)
        gain += (band.upThreshold - floor) * (1.0 - 1.0 / std::max(1.0, band.upRatio)) * upward;
    return gain;
}
[[nodiscard]] inline double pressionMakeupDb(const PressionBand& band, const double downward, const double upward) noexcept
{
    return -pressionGainDb(band, pressionReferenceDb, downward, upward);
}
// A band's output (dB) for its input: the depth scales what it does, its
// gain knob after.
[[nodiscard]] inline double pressionOutputDb(const PressionBand& band, const double levelDb, const double depth, const double downward,
                                             const double upward, const double bandGainDb) noexcept
{
    return levelDb + depth * (pressionGainDb(band, levelDb, downward, upward) + pressionMakeupDb(band, downward, upward)) + bandGainDb;
}
// The time knob (0..1) as a factor on the bands' attacks and releases:
// a tenth to ten times, once at the middle.
[[nodiscard]] inline double pressionTimeScale(const double time) noexcept
{
    return std::pow(10.0, (std::clamp(time, 0.0, 1.0) - 0.5) * 2.0);
}

// The level (dB, RMS) of each band in a stretch of samples, split as the
// processor splits them: the display's meters.
[[nodiscard]] inline std::array<double, pressionBands> pressionBandLevelsDb(const float* samples, const int count, const double rate,
                                                                           const double lowHz, const double highHz) noexcept
{
    BiquadRun low1 { crossoverLowPass(lowHz, rate) };
    BiquadRun low2 { low1.c };
    BiquadRun rest1 { crossoverHighPass(lowHz, rate) };
    BiquadRun rest2 { rest1.c };
    BiquadRun mid1 { crossoverLowPass(highHz, rate) };
    BiquadRun mid2 { mid1.c };
    BiquadRun high1 { crossoverHighPass(highHz, rate) };
    BiquadRun high2 { high1.c };
    std::array<double, pressionBands> power {};
    // The filters settle over the first quarter; the rest is measured.
    const auto from = count / 4;
    for (int i = 0; i < count; ++i) {
        const auto x = static_cast<double>(samples[i]);
        const auto low = low2(low1(x));
        const auto rest = rest2(rest1(x));
        const auto mid = mid2(mid1(rest));
        const auto high = high2(high1(rest));
        if (i >= from) {
            power[0] += low * low;
            power[1] += mid * mid;
            power[2] += high * high;
        }
    }
    std::array<double, pressionBands> levels {};
    for (int band = 0; band < pressionBands; ++band)
        levels[static_cast<std::size_t>(band)]
            = 10.0 * std::log10(std::max(1.0e-12, power[static_cast<std::size_t>(band)] / std::max(1, count - from))) + 3.01;
    return levels;
}

// ---- Fournaise --------------------------------------------------------------------------------

enum Fournaise : int {
    fournaiseDrive,
    fournaiseTone,
    fournaiseLowCut,
    fournaiseHighCut,
    fournaiseMix,
    fournaiseOutput,
    fournaiseStyle,
    fournaisePunish,
    fournaiseAuto,
    fournaiseThump,
    fournaiseSteep,
    fournaiseOversampling,
    fournaiseParameterCount
};

struct FournaiseStyle {
    const char* french;
    const char* english;
    const char* frenchHint;
    const char* englishHint;
    // The drive the knob reaches at 10 (dB).
    double maximumDriveDb;
    // Its colour before the saturation (a bell: Hz, dB, Q) and the top
    // that remains after it (Hz).
    double colourHz;
    double colourDb;
    double colourQ;
    double topHz;
    // A low shelf after it (the tape's head bump): Hz, dB.
    double bumpHz;
    double bumpDb;
};

inline constexpr int fournaiseStyleCount = 5;
inline constexpr std::array<FournaiseStyle, fournaiseStyleCount> fournaiseStyles { {
    { "Bande", "Tape", "Un magnétophone poussé : rond, aigus tassés, grave gonflé", "A tape machine pushed: round, highs squeezed, lows swollen",
      30.0, 5'000.0, -2.0, 0.5, 15'000.0, 90.0, 2.0 },
    { "Console", "Console", "Le transformateur d'une console : doux, épais, discret", "A console's transformer: soft, thick, discreet", 26.0,
      120.0, 1.0, 0.7, 18'000.0, 60.0, 1.0 },
    { "Classe A", "Class A", "Un préampli classe A : médiums chauds, harmoniques paires", "A class A preamp: warm mids, even harmonics",
      34.0, 300.0, 2.0, 0.8, 17'000.0, 0.0, 0.0 },
    { "Lampe", "Valve", "Une triode : mordant, asymétrique, s'ouvre dans les aigus", "A triode: biting, asymmetric, opens up the highs",
      36.0, 3'000.0, 2.5, 0.7, 12'000.0, 0.0, 0.0 },
    { "Pentode", "Pentode", "Une pentode : agressive, presque fuzz, médiums en avant", "A pentode: aggressive, nearly fuzz, mids forward",
      42.0, 1'200.0, 4.0, 1.0, 9'000.0, 0.0, 0.0 },
} };

[[nodiscard]] inline const FournaiseStyle& fournaiseCharacter(const int index) noexcept
{
    return fournaiseStyles[static_cast<std::size_t>(std::clamp(index, 0, fournaiseStyleCount - 1))];
}

// The style's curve, once driven: bounded, rising, each its own asymmetry.
[[nodiscard]] inline double fournaiseShape(const int style, const double x) noexcept
{
    switch (std::clamp(style, 0, fournaiseStyleCount - 1)) {
    case 0: return std::tanh(x);
    case 1: {
        constexpr auto bias = 0.12;
        const auto shifted = x + bias;
        return shifted / std::sqrt(1.0 + shifted * shifted) - bias / std::sqrt(1.0 + bias * bias);
    }
    case 2: {
        constexpr auto bias = 0.3;
        return std::tanh(x + bias) - std::tanh(bias);
    }
    case 3: return x >= 0.0 ? std::tanh(x) : std::tanh(1.6 * x) / 1.6;
    default: {
        // A cubic to its edges, then flat: hard, its knee rounded; biased.
        const auto clip = [](double v) {
            v *= 1.5;
            return v <= -1.0 ? -1.0 : v >= 1.0 ? 1.0 : 1.5 * v - 0.5 * v * v * v;
        };
        constexpr auto bias = 0.2;
        return clip(x + bias) - clip(bias);
    }
    }
}

// The drive (dB) for its knob (0..10), punished (+20 dB) or not.
[[nodiscard]] inline double fournaiseDriveDb(const int style, const double drive, const bool punish) noexcept
{
    return std::clamp(drive, 0.0, 10.0) / 10.0 * fournaiseCharacter(style).maximumDriveDb + (punish ? 20.0 : 0.0);
}

// The filters around the saturation, in order: the low cut (a bump at it,
// thumped), the style's colour, then after it the style's bump and top, the
// tone (a tilt about 800 Hz) and the high cut.
struct FournaiseFilters {
    std::array<BiquadCoefficients, 2> lowCut {};
    BiquadCoefficients colour;
    BiquadCoefficients bump;
    BiquadCoefficients top;
    std::array<BiquadCoefficients, 2> tilt {};
    std::array<BiquadCoefficients, 2> highCut {};
    bool steep {};
    bool highCutOn {};
};
[[nodiscard]] inline FournaiseFilters fournaiseFilters(const float* values, const double rate) noexcept
{
    FournaiseFilters filters;
    const auto& style = fournaiseCharacter(static_cast<int>(std::lround(values[fournaiseStyle])));
    const auto lowCut = std::clamp(static_cast<double>(values[fournaiseLowCut]), 10.0, 1'000.0);
    const auto thump = values[fournaiseThump] >= 0.5f;
    // Two sections, the second resonant when thumped.
    filters.lowCut[0] = designEqBand(EqBandType::lowCut, lowCut, 0.0, std::numbers::sqrt2 * 0.5, 0, 0, rate);
    filters.lowCut[1] = designEqBand(EqBandType::lowCut, lowCut, 0.0, thump ? 2.2 : std::numbers::sqrt2 * 0.5, 0, 0, rate);
    filters.colour = designEqBand(EqBandType::bell, style.colourHz, style.colourDb, style.colourQ, 0, 0, rate);
    filters.bump = designEqBand(EqBandType::lowShelf, style.bumpHz > 0.0 ? style.bumpHz : 60.0, style.bumpDb, 0.7, 0, 0, rate);
    filters.top = designEqBand(EqBandType::highCut, std::min(style.topHz, rate * 0.45), 0.0, std::numbers::sqrt2 * 0.5, 0, 0, rate);
    const auto tone = std::clamp(static_cast<double>(values[fournaiseTone]), -1.0, 1.0) * 6.0;
    filters.tilt[0] = designEqBand(EqBandType::tilt, 800.0, tone, 0.5, 0, 0, rate);
    filters.tilt[1] = designEqBand(EqBandType::tilt, 800.0, tone, 0.5, 0, 1, rate);
    const auto highCut = std::clamp(static_cast<double>(values[fournaiseHighCut]), 500.0, rate * 0.45);
    filters.highCutOn = values[fournaiseHighCut] < 19'900.0f;
    filters.steep = values[fournaiseSteep] >= 0.5f;
    if (filters.steep) {
        filters.highCut[0] = designEqBand(EqBandType::highCut, highCut, 0.0, std::numbers::sqrt2 * 0.5, 1, 0, rate);
        filters.highCut[1] = designEqBand(EqBandType::highCut, highCut, 0.0, std::numbers::sqrt2 * 0.5, 1, 1, rate);
    } else {
        // Gentle: one section, 12 dB per octave, its corner soft.
        filters.highCut[0] = designEqBand(EqBandType::highCut, highCut, 0.0, 0.5, 0, 0, rate);
    }
    return filters;
}
// The filters' response (dB) at a frequency, the saturation aside: the
// curve drawn (designed once for a whole curve).
[[nodiscard]] inline double fournaiseToneDb(const FournaiseFilters& filters, const double hz, const double rate) noexcept
{
    auto db = biquadMagnitudeDb(filters.lowCut[0], hz, rate) + biquadMagnitudeDb(filters.lowCut[1], hz, rate)
              + biquadMagnitudeDb(filters.colour, hz, rate) + biquadMagnitudeDb(filters.bump, hz, rate)
              + biquadMagnitudeDb(filters.top, hz, rate) + biquadMagnitudeDb(filters.tilt[0], hz, rate)
              + biquadMagnitudeDb(filters.tilt[1], hz, rate);
    if (filters.highCutOn) {
        db += biquadMagnitudeDb(filters.highCut[0], hz, rate);
        if (filters.steep)
            db += biquadMagnitudeDb(filters.highCut[1], hz, rate);
    }
    return db;
}

// ---- Écho Bande ------------------------------------------------------------------------------

enum Echo : int {
    echoTime,
    echoIntensity,
    echoLevel,
    echoReverb,
    echoBass,
    echoTreble,
    echoWow,
    echoMix,
    echoMode,
    echoSync,
    echoDrive,
    echoAge,
    echoWidth,
    echoParameterCount
};

// The twelve modes of the selector: which heads play, and the spring.
struct EchoMode {
    std::array<bool, 3> heads;
    bool spring;
};
inline constexpr int echoModeCount = 12;
inline constexpr std::array<EchoMode, echoModeCount> echoModes { {
    { { true, false, false }, false },
    { { false, true, false }, false },
    { { false, false, true }, false },
    { { false, true, true }, false },
    { { true, false, false }, true },
    { { false, true, false }, true },
    { { false, false, true }, true },
    { { true, true, false }, true },
    { { false, true, true }, true },
    { { true, false, true }, true },
    { { true, true, true }, true },
    { { false, false, false }, true },
} };
[[nodiscard]] inline const EchoMode& echoModeOf(const float value) noexcept
{
    return echoModes[static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(value)), 0, echoModeCount - 1))];
}
// Where the playback heads sit along the tape: their delays against the
// first one's.
inline constexpr std::array<double, 3> echoHeadRatio { 1.0, 1.96, 2.92 };
// The longest the tape holds (seconds).
inline constexpr double echoLongestSeconds = 3.0;

// The first head's delay (seconds): the time knob, or a note value of the
// tempo when synced. The third head stays within the tape.
[[nodiscard]] inline double echoHeadSeconds(const float* values, const double beatsPerMinute, const double beatsOfSync) noexcept
{
    auto seconds = static_cast<double>(values[echoTime]) * 0.001;
    if (beatsOfSync > 0.0)
        seconds = beatsOfSync * 60.0 / std::max(20.0, beatsPerMinute);
    return std::clamp(seconds, 0.01, echoLongestSeconds / echoHeadRatio[2]);
}
// How much of the heads goes back onto the tape: the intensity, less as
// more heads play (past 1, the echo runs away, held by the tape).
[[nodiscard]] inline double echoFeedback(const float* values) noexcept
{
    const auto& mode = echoModeOf(values[echoMode]);
    const auto heads = static_cast<int>(mode.heads[0]) + static_cast<int>(mode.heads[1]) + static_cast<int>(mode.heads[2]);
    return heads == 0 ? 0.0 : static_cast<double>(values[echoIntensity]) / std::pow(static_cast<double>(heads), 0.7);
}

// The echoes an impulse makes (seconds, level), the tone aside: the display.
struct EchoTap {
    double seconds {};
    double level {};
};
template <std::size_t N>
int echoPattern(const float* values, const double headSeconds, std::array<EchoTap, N>& taps) noexcept
{
    const auto& mode = echoModeOf(values[echoMode]);
    const auto feedback = std::min(echoFeedback(values), 0.98);
    int count = 0;
    // Each pass through the loop is one more trip round the heads.
    std::array<double, N> times {};
    std::array<double, N> levels {};
    int pending = 0;
    times[0] = 0.0;
    levels[0] = 1.0;
    pending = 1;
    for (int start = 0; start < pending && count < static_cast<int>(N); ++start) {
        for (int head = 0; head < 3; ++head) {
            if (!mode.heads[static_cast<std::size_t>(head)])
                continue;
            const auto at = times[static_cast<std::size_t>(start)] + headSeconds * echoHeadRatio[static_cast<std::size_t>(head)];
            const auto level = levels[static_cast<std::size_t>(start)] * (start == 0 ? 1.0 : feedback);
            if (at > 6.0 || level < 0.01 || count >= static_cast<int>(N))
                continue;
            taps[static_cast<std::size_t>(count++)] = { at, level };
            if (pending < static_cast<int>(N)) {
                times[static_cast<std::size_t>(pending)] = at;
                levels[static_cast<std::size_t>(pending)] = level;
                ++pending;
            }
        }
    }
    std::sort(taps.begin(), taps.begin() + count, [](const EchoTap& a, const EchoTap& b) { return a.seconds < b.seconds; });
    return count;
}

// ---- Multibande Pro --------------------------------------------------------------------------

enum Multiband : int {
    // The sockets: each band's threshold, the input, the output, the amount,
    // the stereo link.
    multibandThreshold1,
    multibandThreshold2,
    multibandThreshold3,
    multibandThreshold4,
    multibandInput,
    multibandOutput,
    multibandAmount,
    multibandLink,
    // The slots: the three crossovers, then four bands of multibandFields.
    multibandCross1,
    multibandCross2,
    multibandCross3,
    multibandBandBase
};
enum MultibandField : int {
    multibandMode,
    multibandRatio,
    multibandRange,
    multibandAttack,
    multibandRelease,
    multibandKnee,
    multibandMakeup,
    multibandOn,
    multibandSolo,
    multibandFields
};
inline constexpr int multibandBands = 4;
[[nodiscard]] constexpr int multibandIndex(const int band, const int field) noexcept
{
    return multibandBandBase + band * multibandFields + field;
}
inline constexpr int multibandParameterCount = multibandIndex(multibandBands, 0);
[[nodiscard]] constexpr int multibandThreshold(const int band) noexcept
{
    return multibandThreshold1 + band;
}

// What a band does: held down above its threshold (compress), pushed down
// below it (expand, a gate), raised below it (upward compression), raised
// above it (upward expansion, the attacks brought out).
enum class MultibandMode : int { compress, expand, raise, burst };
inline constexpr int multibandModeCount = 4;

// The all-pass a Linkwitz-Riley pair sums to at its crossover: one
// second-order section, the Butterworth's poles.
[[nodiscard]] inline BiquadCoefficients crossoverAllPass(const double hz, const double rate) noexcept
{
    const auto low = crossoverLowPass(hz, rate);
    return { low.a2, low.a1, 1.0, low.a1, low.a2 };
}

// A band's gain (dB) at a level (dB): its mode, threshold, ratio, the most
// it may change (range) and a soft knee (dB wide).
[[nodiscard]] inline double multibandGainDb(const MultibandMode mode, const double thresholdDb, const double ratio, const double rangeDb,
                                            const double kneeDb, double levelDb) noexcept
{
    levelDb = std::max(levelDb, pressionFloorDb);
    const auto knee = std::max(0.0, kneeDb);
    // How far into its region, the knee rounding the corner.
    const auto into = [knee](const double d) {
        if (knee <= 0.0)
            return std::max(0.0, d);
        if (d <= -knee * 0.5)
            return 0.0;
        if (d >= knee * 0.5)
            return d;
        return (d + knee * 0.5) * (d + knee * 0.5) / (2.0 * knee);
    };
    const auto r = std::max(1.0, ratio);
    auto gain = 0.0;
    switch (mode) {
    case MultibandMode::compress: gain = -(1.0 - 1.0 / r) * into(levelDb - thresholdDb); break;
    case MultibandMode::expand: gain = -(r - 1.0) * into(thresholdDb - levelDb); break;
    case MultibandMode::raise: gain = (1.0 - 1.0 / r) * into(thresholdDb - levelDb); break;
    case MultibandMode::burst: gain = (r - 1.0) * into(levelDb - thresholdDb); break;
    }
    const auto range = std::max(0.0, rangeDb);
    return std::clamp(gain, -range, range);
}
[[nodiscard]] inline double multibandGainDb(const float* values, const int band, const double levelDb) noexcept
{
    const auto at = [values, band](const int field) { return static_cast<double>(values[multibandIndex(band, field)]); };
    const auto mode = static_cast<MultibandMode>(std::clamp(static_cast<int>(std::lround(at(multibandMode))), 0, multibandModeCount - 1));
    return multibandGainDb(mode, values[multibandThreshold(band)], at(multibandRatio), at(multibandRange), at(multibandKnee), levelDb);
}

// The four bands' levels (dB, RMS) in a stretch of samples, split as the
// processor splits them: the display's meters.
[[nodiscard]] inline std::array<double, multibandBands> multibandLevelsDb(const float* samples, const int count, const double rate,
                                                                         const std::array<double, 3>& crossovers) noexcept
{
    std::array<BiquadRun, 12> f {};
    for (std::size_t c = 0; c < 3; ++c) {
        f[c * 4].c = f[c * 4 + 1].c = crossoverLowPass(crossovers[c], rate);
        f[c * 4 + 2].c = f[c * 4 + 3].c = crossoverHighPass(crossovers[c], rate);
    }
    std::array<double, multibandBands> power {};
    const auto from = count / 4;
    for (int i = 0; i < count; ++i) {
        const auto x = static_cast<double>(samples[i]);
        std::array<double, multibandBands> band {};
        band[0] = f[1](f[0](x));
        const auto rest1 = f[3](f[2](x));
        band[1] = f[5](f[4](rest1));
        const auto rest2 = f[7](f[6](rest1));
        band[2] = f[9](f[8](rest2));
        band[3] = f[11](f[10](rest2));
        if (i >= from)
            for (std::size_t b = 0; b < multibandBands; ++b)
                power[b] += band[b] * band[b];
    }
    std::array<double, multibandBands> levels {};
    for (std::size_t b = 0; b < multibandBands; ++b)
        levels[b] = 10.0 * std::log10(std::max(1.0e-12, power[b] / std::max(1, count - from))) + 3.01;
    return levels;
}

// ---- Velours ----------------------------------------------------------------------------------

enum Velours : int {
    veloursDepth,
    veloursSharpness,
    veloursSelectivity,
    veloursAttack,
    veloursRelease,
    veloursFocusLow,
    veloursFocusHigh,
    veloursMix,
    veloursMode,
    veloursDelta,
    veloursOutput,
    veloursLink,
    veloursParameterCount
};
// Its analysis: frames of 2048 samples, a quarter apart, each bin's power
// averaged over the last 50 ms or so (a resonance lasts; a noise's peaks
// come and go).
inline constexpr double veloursListenMs = 50.0;
inline constexpr int veloursFrame = 2'048;
inline constexpr int veloursHop = veloursFrame / 4;
inline constexpr int veloursBins = veloursFrame / 2 + 1;
// A sample comes out once the frame it ends has been heard whole and its
// last quarter put back: a frame later.
inline constexpr int veloursLatency = veloursFrame;

// What it takes from each bin (dB, 0 or more) for the spectrum it hears
// (dB per bin, `binHz` apart): how far a bin stands above its neighbours
// (their average over a width the selectivity sets), the sharpness turning
// that into a cut, at most the depth, only within the focus. `scratch` holds
// bins + 1 values.
inline void veloursReduction(const float* magnitudeDb, const int bins, const double binHz, const float* values, double* scratch,
                             float* reduction) noexcept
{
    // The neighbours' average: a running sum.
    scratch[0] = 0.0;
    for (int k = 0; k < bins; ++k)
        scratch[k + 1] = scratch[k] + static_cast<double>(magnitudeDb[k]);
    const auto selectivity = std::clamp(static_cast<double>(values[veloursSelectivity]), 0.0, 1.0);
    // Its width (octaves): narrow when selective, only the sharpest
    // resonances stand out of it.
    const auto octaves = 0.15 + (1.0 - selectivity) * 0.85;
    const auto spread = std::pow(2.0, octaves * 0.5) - 1.0;
    const auto sharpness = std::clamp(static_cast<double>(values[veloursSharpness]), 0.0, 1.0);
    const auto hard = values[veloursMode] >= 0.5f;
    const auto slope = (0.5 + 1.5 * sharpness) * (hard ? 1.5 : 1.0);
    // Only what stands well out of its neighbours: a noise's own unevenness
    // stays under the knee.
    const auto knee = (3.0 + 4.0 * (1.0 - sharpness)) * (hard ? 0.5 : 1.0);
    const auto depth = std::max(0.0, static_cast<double>(values[veloursDepth]));
    const auto low = std::max(20.0, static_cast<double>(values[veloursFocusLow]));
    const auto high = std::max(low * 1.01, static_cast<double>(values[veloursFocusHigh]));
    for (int k = 0; k < bins; ++k) {
        const auto hz = k * binHz;
        if (hz < 20.0 || magnitudeDb[k] < -90.0f) {
            reduction[k] = 0.0f;
            continue;
        }
        const auto width = std::max(1, static_cast<int>(k * spread));
        const auto from = std::max(0, k - width);
        const auto to = std::min(bins - 1, k + width);
        const auto average = (scratch[to + 1] - scratch[from]) / static_cast<double>(to - from + 1);
        const auto excess = static_cast<double>(magnitudeDb[k]) - average;
        // The focus: whole between its edges, gone half an octave past them.
        auto weight = 1.0;
        if (hz < low)
            weight = std::clamp(1.0 - std::log2(low / hz) * 2.0, 0.0, 1.0);
        else if (hz > high)
            weight = std::clamp(1.0 - std::log2(hz / high) * 2.0, 0.0, 1.0);
        reduction[k] = static_cast<float>(std::clamp((excess - knee) * slope, 0.0, depth) * weight);
    }
}

// ---- Patine -----------------------------------------------------------------------------------

enum Patine : int {
    // The six modules' amounts, the magnitude over them all, the mix.
    patineNoise,
    patineWobble,
    patineDistort,
    patineDigital,
    patineSpace,
    patineMagnetic,
    patineMagnitude,
    patineMix,
    patineNoiseType,
    patineWobbleRate,
    patineDistortType,
    patineSpaceSize,
    patineTone,
    patineWidth,
    patineOutput,
    patineNoiseFollow,
    patineParameterCount
};
inline constexpr int patineModules = 6;
// The wobble's line: its sound read about this far behind (ms), swaying
// either side; what the rest of the sound is delayed by.
inline constexpr double patineWobbleCentreMs = 5.0;
// A module's amount, the magnitude applied.
[[nodiscard]] inline double patineAmount(const float* values, const int module) noexcept
{
    return std::clamp(static_cast<double>(values[module]), 0.0, 1.0) * std::clamp(static_cast<double>(values[patineMagnitude]), 0.0, 1.0);
}

// ---- Sonomètre --------------------------------------------------------------------------------

enum Sonometre : int { sonometreTarget, sonometreHistory, sonometreTruePeak, sonometreParameterCount };

struct LoudnessTarget {
    const char* french;
    const char* english;
    double lufs;
    double truePeakDb;
};
inline constexpr int loudnessTargetCount = 7;
inline constexpr std::array<LoudnessTarget, loudnessTargetCount> loudnessTargets { {
    { "Streaming (Spotify, YouTube, Tidal)", "Streaming (Spotify, YouTube, Tidal)", -14.0, -1.0 },
    { "Apple Music", "Apple Music", -16.0, -1.0 },
    { "Deezer", "Deezer", -15.0, -1.0 },
    { "Podcast", "Podcast", -16.0, -1.0 },
    { "Radio et télé (EBU R128)", "Radio and TV (EBU R128)", -23.0, -1.0 },
    { "Télé américaine (ATSC A/85)", "US TV (ATSC A/85)", -24.0, -2.0 },
    { "CD et club", "CD and club", -9.0, -0.3 },
} };
[[nodiscard]] inline const LoudnessTarget& loudnessTarget(const float value) noexcept
{
    return loudnessTargets[static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(value)), 0, loudnessTargetCount - 1))];
}

} // namespace cydaw::engine::mix
