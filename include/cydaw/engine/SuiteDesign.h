#pragma once

#include <cydaw/engine/BiquadDesign.h>
#include <cydaw/engine/MixDesign.h>

#include <algorithm>
#include <array>
#include <cmath>

// What the two suites compute, written once for their processors
// (SuiteEffects.cpp) and for the interface that draws them.
//
//   Tranche   (tranche)   a channel strip: gate, equaliser, compressor,
//                         transients, exciter, width; an assistant that
//                         listens and sets them
//   Finition  (finition)  a mastering chain: equaliser, three-band dynamics,
//                         exciter and imager, maximizer; an assistant that
//                         aims at a loudness
//
// Each effect's values are its catalogue entry's parameters, in order; the
// enums below name their indices.

namespace cydaw::engine::suite {

// A compressor's gain (dB, 0 or less) at a level (dB): its threshold,
// ratio and soft knee (dB wide).
[[nodiscard]] inline double compressorGainDb(const double levelDb, const double thresholdDb, const double ratio, const double kneeDb) noexcept
{
    const auto over = levelDb - thresholdDb;
    const auto slope = 1.0 - 1.0 / std::max(1.0, ratio);
    const auto knee = std::max(0.0, kneeDb);
    if (knee > 0.0 && std::abs(over) <= knee * 0.5)
        return -slope * (over + knee * 0.5) * (over + knee * 0.5) / (2.0 * knee);
    return over > 0.0 ? -slope * over : 0.0;
}

// ---- Tranche ----------------------------------------------------------------------------------

enum Tranche : int {
    // The sockets.
    trancheGateThreshold,
    trancheCompThreshold,
    trancheAttack,
    trancheSustain,
    trancheExcite,
    trancheWidth,
    trancheOutput,
    trancheMix,
    // Each stage on or off.
    trancheGateOn,
    trancheEqOn,
    trancheCompOn,
    trancheTransientOn,
    trancheExciterOn,
    // The gate.
    trancheGateRange,
    trancheGateAttack,
    trancheGateRelease,
    // The equaliser: a low cut, a low shelf, two bells, a high shelf, a high cut.
    trancheLowCut,
    trancheLowShelfHz,
    trancheLowShelfDb,
    trancheBell1Hz,
    trancheBell1Db,
    trancheBell1Q,
    trancheBell2Hz,
    trancheBell2Db,
    trancheBell2Q,
    trancheHighShelfHz,
    trancheHighShelfDb,
    trancheHighCut,
    // The compressor.
    trancheCompRatio,
    trancheCompAttack,
    trancheCompRelease,
    trancheCompKnee,
    trancheCompMakeup,
    // The exciter: from where it adds harmonics, and how.
    trancheExciterHz,
    trancheExciterStyle,
    trancheParameterCount
};

// The equaliser's sections: the low cut (two, 24 dB per octave), the low
// shelf, two bells, the high shelf, the high cut (two). A cut at its end of
// the range is off (a flat section).
inline constexpr int trancheEqSections = 8;
[[nodiscard]] inline std::array<BiquadCoefficients, trancheEqSections> trancheEq(const float* values, const double rate) noexcept
{
    std::array<BiquadCoefficients, trancheEqSections> sections {};
    const auto lowCut = static_cast<double>(values[trancheLowCut]);
    if (lowCut > 10.5) {
        sections[0] = designEqBand(EqBandType::lowCut, lowCut, 0.0, 0.7071, 1, 0, rate);
        sections[1] = designEqBand(EqBandType::lowCut, lowCut, 0.0, 0.7071, 1, 1, rate);
    }
    sections[2] = designEqBand(EqBandType::lowShelf, values[trancheLowShelfHz], values[trancheLowShelfDb], 0.7, 0, 0, rate);
    sections[3] = designEqBand(EqBandType::bell, values[trancheBell1Hz], values[trancheBell1Db], values[trancheBell1Q], 0, 0, rate);
    sections[4] = designEqBand(EqBandType::bell, values[trancheBell2Hz], values[trancheBell2Db], values[trancheBell2Q], 0, 0, rate);
    sections[5] = designEqBand(EqBandType::highShelf, values[trancheHighShelfHz], values[trancheHighShelfDb], 0.7, 0, 0, rate);
    const auto highCut = std::min(static_cast<double>(values[trancheHighCut]), rate * 0.45);
    if (values[trancheHighCut] < 19'900.0f) {
        sections[6] = designEqBand(EqBandType::highCut, highCut, 0.0, 0.7071, 1, 0, rate);
        sections[7] = designEqBand(EqBandType::highCut, highCut, 0.0, 0.7071, 1, 1, rate);
    }
    return sections;
}
// The equaliser's response (dB) at a frequency: the curve drawn.
[[nodiscard]] inline double trancheEqDb(const std::array<BiquadCoefficients, trancheEqSections>& sections, const double hz,
                                        const double rate) noexcept
{
    auto db = 0.0;
    for (const auto& section : sections)
        db += biquadMagnitudeDb(section, hz, rate);
    return db;
}

// ---- Finition ---------------------------------------------------------------------------------

enum Finition : int {
    // The sockets.
    finitionGain,
    finitionCeiling,
    finitionDynamics,
    finitionExcite,
    finitionWidth,
    finitionLow,
    finitionMid,
    finitionHigh,
    // Each stage on or off.
    finitionEqOn,
    finitionDynamicsOn,
    finitionExciterOn,
    finitionImagerOn,
    finitionMaximizerOn,
    // The two splits of the three bands.
    finitionCrossLow,
    finitionCrossHigh,
    // The equaliser: a low cut, a low shelf, a bell, a high shelf.
    finitionLowCut,
    finitionLowHz,
    finitionMidHz,
    finitionMidQ,
    finitionHighHz,
    // The dynamics, the three bands alike.
    finitionThreshold,
    finitionRatio,
    finitionAttack,
    finitionRelease,
    // The exciter's drive and the imager's width, band by band.
    finitionLowDrive,
    finitionMidDrive,
    finitionHighDrive,
    finitionLowWidth,
    finitionMidWidth,
    finitionHighWidth,
    // The maximizer's release, the loudness the assistant aims at.
    finitionMaximizerRelease,
    finitionTarget,
    finitionParameterCount
};
inline constexpr int finitionBands = 3;
// The maximizer looks this far ahead (ms): the effect's latency.
inline constexpr double finitionLookaheadMs = 1.5;

inline constexpr int finitionEqSections = 5;
[[nodiscard]] inline std::array<BiquadCoefficients, finitionEqSections> finitionEq(const float* values, const double rate) noexcept
{
    std::array<BiquadCoefficients, finitionEqSections> sections {};
    const auto lowCut = static_cast<double>(values[finitionLowCut]);
    if (lowCut > 10.5) {
        sections[0] = designEqBand(EqBandType::lowCut, lowCut, 0.0, 0.7071, 1, 0, rate);
        sections[1] = designEqBand(EqBandType::lowCut, lowCut, 0.0, 0.7071, 1, 1, rate);
    }
    sections[2] = designEqBand(EqBandType::lowShelf, values[finitionLowHz], values[finitionLow], 0.7, 0, 0, rate);
    sections[3] = designEqBand(EqBandType::bell, values[finitionMidHz], values[finitionMid], values[finitionMidQ], 0, 0, rate);
    sections[4] = designEqBand(EqBandType::highShelf, values[finitionHighHz], values[finitionHigh], 0.7, 0, 0, rate);
    return sections;
}
[[nodiscard]] inline double finitionEqDb(const std::array<BiquadCoefficients, finitionEqSections>& sections, const double hz,
                                         const double rate) noexcept
{
    auto db = 0.0;
    for (const auto& section : sections)
        db += biquadMagnitudeDb(section, hz, rate);
    return db;
}
// The dynamics' ratio as the amount (0..1) sets it: 1:1 at none.
[[nodiscard]] inline double finitionAppliedRatio(const float* values) noexcept
{
    const auto amount = std::clamp(static_cast<double>(values[finitionDynamics]), 0.0, 1.0);
    return 1.0 + (std::max(1.0, static_cast<double>(values[finitionRatio])) - 1.0) * amount;
}

} // namespace cydaw::engine::suite
