#pragma once

#include <cydaw/engine/BiquadDesign.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numbers>

// What the four studio effects compute, written once for the processors and
// for the interface that draws them: the filters' responses, the reverb's
// decay over frequency, the compressor's and the limiter's styles and gain
// computers. A curve on screen is the processor's own arithmetic.
//
//   Réverbe Pro   (reverb-pro)   a feedback-delay-network reverb
//   Compresseur Pro (comp-pro)   the studio compressor, with its styles
//   Limiteur Pro  (limiter-pro)  a look-ahead true-peak limiter
//   Filtre Pro    (filter-pro)   four modulated filters
//
// Each effect's values are its catalogue entry's parameters, in order; the
// enums below name their indices.

namespace cydaw::engine::pro {

// ---- Réverbe Pro ------------------------------------------------------------------------------

enum Reverb : int {
    reverbSpace,
    reverbDecay,
    reverbPredelay,
    reverbDistance,
    reverbBrightness,
    reverbCharacter,
    reverbWidth,
    reverbMix,
    reverbLowDecay,
    reverbLowFrequency,
    reverbMidDecay,
    reverbMidFrequency,
    reverbHighDecay,
    reverbHighFrequency,
    reverbLowCut,
    reverbHighCut,
    reverbDucking,
    reverbDuckRelease,
    reverbFreeze,
    reverbSync,
    reverbDiffusion,
    reverbOutput,
    reverbEarly,
    reverbParameterCount
};

inline constexpr int reverbLines = 16;
// The network's delays at the largest space, milliseconds (the smallest
// space plays them at a quarter of these).
inline constexpr std::array<double, reverbLines> reverbLineMs {
    29.71, 33.17, 37.13, 41.11, 43.73, 47.93, 53.29, 57.07, 61.73, 66.13, 71.93, 76.31, 81.17, 86.69, 92.11, 99.37,
};
[[nodiscard]] inline double reverbScale(const double space) noexcept
{
    return 0.25 + std::clamp(space, 0.0, 1.0) * 1.15;
}

// A delay line's three loop filters: the decay below `lowFrequency`, around
// `midFrequency` and above `highFrequency` relative to the main decay, as the
// gain in dB each pass round a line of `seconds` takes.
struct ReverbLoop {
    BiquadCoefficients low;
    BiquadCoefficients mid;
    BiquadCoefficients high;
    double gain { 1.0 };
};

[[nodiscard]] inline ReverbLoop reverbLoop(const double seconds, const double decay, const double lowMultiplier,
                                           const double lowFrequency, const double midMultiplier, const double midFrequency,
                                           const double highMultiplier, const double highFrequency, const double rate) noexcept
{
    // A pass loses 60 dB × its length / the decay time.
    const auto t = std::max(0.05, decay);
    const auto passDb = [seconds](const double time) { return -60.0 * seconds / std::max(0.01, time); };
    const auto main = passDb(t);
    const auto nyquist = rate * 0.45;
    ReverbLoop loop;
    loop.gain = std::pow(10.0, main / 20.0);
    loop.low = designEqBand(EqBandType::lowShelf, std::min(lowFrequency, nyquist), std::clamp(passDb(t * lowMultiplier) - main, -24.0, 24.0),
                            0.707, 0, 0, rate);
    loop.mid = designEqBand(EqBandType::bell, std::min(midFrequency, nyquist), std::clamp(passDb(t * midMultiplier) - main, -24.0, 24.0),
                            0.8, 0, 0, rate);
    loop.high = designEqBand(EqBandType::highShelf, std::min(highFrequency, nyquist),
                             std::clamp(passDb(t * highMultiplier) - main, -24.0, 24.0), 0.707, 0, 0, rate);
    return loop;
}

// The decay time (to -60 dB) of the network at a frequency: what its loop
// filters make of a line of middle length.
[[nodiscard]] inline double reverbDecayAt(const double hz, const double decay, const double lowMultiplier, const double lowFrequency,
                                          const double midMultiplier, const double midFrequency, const double highMultiplier,
                                          const double highFrequency, const double rate = 48'000.0) noexcept
{
    constexpr auto seconds = 0.055;
    const auto loop = reverbLoop(seconds, decay, lowMultiplier, lowFrequency, midMultiplier, midFrequency, highMultiplier,
                                 highFrequency, rate);
    const auto db = 20.0 * std::log10(std::max(1.0e-9, loop.gain)) + biquadMagnitudeDb(loop.low, hz, rate)
                    + biquadMagnitudeDb(loop.mid, hz, rate) + biquadMagnitudeDb(loop.high, hz, rate);
    return db < -1.0e-6 ? -60.0 * seconds / db : 60.0;
}

// ---- Compresseur Pro --------------------------------------------------------------------------

enum Compressor : int {
    compressorThreshold,
    compressorRatio,
    compressorKnee,
    compressorAttack,
    compressorRelease,
    compressorMakeup,
    compressorMix,
    compressorSidechainHigh,
    compressorStyle,
    compressorRange,
    compressorHold,
    compressorLookahead,
    compressorAutoRelease,
    compressorAutoGain,
    compressorSidechainLow,
    compressorLink,
    compressorChannels,
    compressorAudition,
    compressorParameterCount
};

// How each style listens and moves. Clean is the plain feed-forward peak
// compressor the card always was.
struct CompressorStyle {
    // 0: peak; above: an RMS window of that many milliseconds.
    double rmsMs {};
    // How much of the detector reads the output (a feedback design).
    double feedback {};
    // Added to the knee, dB.
    double kneeExtra {};
    double attackScale { 1.0 };
    double releaseScale { 1.0 };
    // The release slows after a long reduction (an opto cell).
    double programRelease {};
    // A release straight in dB rather than curved (pumping).
    bool linearRelease {};
};
inline constexpr int compressorStyleCount = 8;
inline constexpr std::array<CompressorStyle, compressorStyleCount> compressorStyles { {
    { 0.0, 0.0, 0.0, 1.0, 1.0, 0.0, false },   // Clean
    { 3.0, 0.6, 3.0, 1.2, 1.1, 0.3, false },   // Classic
    { 12.0, 0.4, 8.0, 1.6, 1.5, 1.4, false },  // Opto
    { 6.0, 0.15, 6.0, 0.9, 1.0, 0.5, false },  // Vocal
    { 25.0, 0.0, 6.0, 1.4, 1.3, 0.8, false },  // Mastering
    { 5.0, 0.35, 4.0, 1.0, 1.0, 0.4, false },  // Bus
    { 0.0, 0.0, 2.0, 2.2, 0.7, 0.0, false },   // Punch
    { 0.0, 0.1, 0.0, 0.6, 1.0, 0.0, true },    // Pumping
} };

// The gain change, dB (zero or below), for a level in, with the range as
// the deepest it goes.
[[nodiscard]] inline double compressorGainDb(const double levelDb, const double threshold, const double ratio, const double knee,
                                             const double range = 60.0) noexcept
{
    const auto over = levelDb - threshold;
    const auto slope = 1.0 / std::max(1.0, ratio) - 1.0;
    auto gain = 0.0;
    if (2.0 * over > knee)
        gain = slope * over;
    else if (knee > 0.0 && 2.0 * over > -knee)
        gain = slope * (over + knee * 0.5) * (over + knee * 0.5) / (2.0 * knee);
    return std::max(gain, -std::max(0.0, range));
}

// The automatic makeup: half of what a full-scale signal loses.
[[nodiscard]] inline double compressorAutoGainDb(const double threshold, const double ratio, const double knee,
                                                 const double range) noexcept
{
    return -0.5 * compressorGainDb(0.0, threshold, ratio, knee, range);
}

// ---- Limiteur Pro ------------------------------------------------------------------------------

enum Limiter : int {
    limiterGain,
    limiterOutput,
    limiterLookahead,
    limiterAttack,
    limiterRelease,
    limiterStyle,
    limiterLinkTransients,
    limiterLinkRelease,
    limiterTruePeak,
    limiterOversampling,
    limiterDither,
    limiterShaping,
    limiterDcFilter,
    limiterUnity,
    limiterTarget,
    limiterParameterCount
};

inline constexpr double limiterMaximumLookaheadMs = 5.0;

struct LimiterStyle {
    // How much a slow, RMS-led stage rides the level before the peaks are
    // caught (0: none).
    double slow {};
    double releaseScale { 1.0 };
    // How much the release lengthens when the limiting is dense.
    double adaptive {};
    // A soft clip before the limiter, of that many dB of its drive.
    double clip {};
    double attackScale { 1.0 };
};
inline constexpr int limiterStyleCount = 8;
inline constexpr std::array<LimiterStyle, limiterStyleCount> limiterStyles { {
    { 0.0, 1.0, 1.5, 0.0, 1.0 },   // Transparent
    { 0.0, 0.6, 0.6, 0.0, 0.5 },   // Punchy
    { 0.6, 1.0, 1.0, 0.0, 1.0 },   // Dynamic
    { 0.35, 0.9, 1.0, 0.0, 1.0 },  // Allround
    { 0.0, 0.4, 0.2, 0.6, 0.4 },   // Aggressive
    { 0.5, 0.8, 1.2, 0.2, 0.8 },   // Modern
    { 0.2, 1.4, 2.0, 0.0, 1.5 },   // Safe
    { 0.45, 1.2, 1.4, 0.0, 1.2 },  // Bus
} };
[[nodiscard]] inline int limiterOversamplingFactor(const int setting) noexcept
{
    return 1 << std::clamp(setting, 0, 3);
}

// ---- Filtre Pro ------------------------------------------------------------------------------------

inline constexpr int filterUnits = 4;
enum FilterType : int { filterLowPass, filterHighPass, filterBandPass, filterNotch, filterPeak, filterComb, filterTypeCount };
// Slopes: 12, 24, 48 dB per octave.
inline constexpr int filterSlopeCount = 3;
[[nodiscard]] inline int filterStages(const int slope) noexcept
{
    return slope <= 0 ? 1 : slope == 1 ? 2 : 4;
}

enum FilterValue : int { unitOn, unitType, unitSlope, unitCutoff, unitResonance, unitDrive, unitSpread, unitValueCount };
enum FilterGlobal : int {
    filterRouting = filterUnits * unitValueCount,
    filterMix,
    filterOutput,
    filterLfo1Rate,
    filterLfo1Shape,
    filterLfo1Sync,
    filterLfo1Stereo,
    filterLfo2Rate,
    filterLfo2Shape,
    filterLfo2Sync,
    filterLfo2Stereo,
    filterFollowAttack,
    filterFollowRelease,
    filterFollowGain,
    filterMatrix
};
inline constexpr int filterMatrixSlots = 4;
enum MatrixValue : int { matrixSource, matrixDestination, matrixAmount, matrixValueCount };
inline constexpr int filterParameterCount = filterMatrix + filterMatrixSlots * matrixValueCount;

[[nodiscard]] constexpr int filterIndex(const int unit, const FilterValue value) noexcept
{
    return unit * unitValueCount + value;
}
[[nodiscard]] constexpr int matrixIndex(const int slot, const MatrixValue value) noexcept
{
    return filterMatrix + slot * matrixValueCount + value;
}

enum FilterRouting : int { routingSerial, routingParallel, routingPairs, filterRoutingCount };
enum ModulationSource : int { sourceNone, sourceLfo1, sourceLfo2, sourceFollower, modulationSourceCount };
enum ModulationDestination : int {
    destinationNone,
    destinationCutoff1,
    destinationResonance1,
    destinationCutoff2,
    destinationResonance2,
    destinationCutoff3,
    destinationResonance3,
    destinationCutoff4,
    destinationResonance4,
    destinationAllCutoffs,
    destinationDrive1,
    destinationDrive2,
    destinationMix,
    destinationOutput,
    modulationDestinationCount
};
// A full modulation moves a cutoff by this many octaves.
inline constexpr double modulationOctaves = 5.0;
inline constexpr int lfoShapeCount = 7;

// The resonance 0..1 as the sections' Q: 0.5 (no peak) up to 20.
[[nodiscard]] inline double filterQ(const double resonance) noexcept
{
    return 0.5 * std::pow(40.0, std::clamp(resonance, 0.0, 1.0));
}

// A peak's boost for its resonance.
[[nodiscard]] inline double filterPeakDb(const double resonance) noexcept
{
    return 6.0 + 18.0 * std::clamp(resonance, 0.0, 1.0);
}

// The comb's feedback for its resonance.
[[nodiscard]] inline double filterCombFeedback(const double resonance) noexcept
{
    return 0.95 * std::clamp(resonance, 0.0, 1.0);
}

// What one unit does at a frequency, dB: the state-variable sections the
// processor runs (topology-preserving, so the analogue prototype at the
// prewarped frequency), the resonance on the last of them.
[[nodiscard]] inline double filterMagnitudeDb(const int type, const int slope, const double cutoff, const double resonance,
                                              const double hz, const double rate = 48'000.0) noexcept
{
    const auto nyquist = rate * 0.5;
    const auto fc = std::clamp(cutoff, 10.0, nyquist * 0.98);
    if (type == filterComb) {
        const auto g = filterCombFeedback(resonance);
        const auto w = 2.0 * std::numbers::pi * hz / rate;
        const auto delay = rate / fc;
        const std::complex<double> e = std::polar(1.0, -w * delay);
        return 20.0 * std::log10(std::max(1.0e-9, (1.0 - g) / std::abs(1.0 - g * e)));
    }
    const auto omega = std::tan(std::numbers::pi * std::clamp(hz, 1.0, nyquist * 0.999) / rate)
                       / std::tan(std::numbers::pi * fc / rate);
    const std::complex<double> s(0.0, omega);
    const auto stages = type == filterPeak ? 1 : filterStages(slope);
    auto db = 0.0;
    for (int stage = 0; stage < stages; ++stage) {
        const auto last = stage == stages - 1;
        const auto q = last ? filterQ(resonance) : 0.7071;
        const auto denominator = s * s + s / q + 1.0;
        std::complex<double> h;
        switch (type) {
        case filterHighPass: h = s * s / denominator; break;
        case filterBandPass: h = (s / q) / denominator; break;
        case filterNotch: h = (s * s + 1.0) / denominator; break;
        case filterPeak: {
            const auto a = std::pow(10.0, filterPeakDb(resonance) / 40.0);
            const auto k = 1.0 / (q * a);
            h = (s * s + s * k * a * a + 1.0) / (s * s + s * k + 1.0);
            break;
        }
        default: h = 1.0 / denominator; break;
        }
        db += 20.0 * std::log10(std::max(1.0e-9, std::abs(h)));
    }
    return db;
}

// An LFO's shape at a phase in [0, 1): sine, triangle, saw up, saw down,
// square; the random shapes take the value they are given.
[[nodiscard]] inline double lfoShapeValue(const int shape, const double phase, const double random = 0.0) noexcept
{
    const auto p = phase - std::floor(phase);
    switch (shape) {
    case 1: return 1.0 - 4.0 * std::abs(p - 0.5);
    case 2: return 2.0 * p - 1.0;
    case 3: return 1.0 - 2.0 * p;
    case 4: return p < 0.5 ? 1.0 : -1.0;
    case 5:
    case 6: return random;
    default: return std::sin(2.0 * std::numbers::pi * p);
    }
}

} // namespace cydaw::engine::pro
