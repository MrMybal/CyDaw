#pragma once

#include "NativeDspGraph.h"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace cydaw::engine {

// The effects that came after the first primitives, each described once.
//
// A primitive used to be spelled out in a dozen tables — the card type and
// its processor key, the processor key and its native kind, each binding and
// its field, each port and its parameter, the ranges, the labels, the palette
// — and adding one meant finding all of them. Every one of those tables now
// asks this catalogue after its own entries, so an effect is an entry here
// and a processor in EffectProcessors.cpp.
//
// Their values live in the generic effect1..effect8 fields of the payload,
// or, for an effect with many (the parametric equaliser's bands), in slots
// of its eqBands array: where each value lives is the entry's business.

struct EffectParameter {
    // The primitive's parameter and value-port key.
    std::string_view key;
    // The binding suffix a card publishes it under ("chorus.rate").
    std::string_view suffix;
    std::string_view labelKey;
    // "Hz", "ms", "dB" or empty.
    std::string_view unit;
    float minimum {};
    float maximum { 1.0f };
    float defaultValue {};
    // Either a field of the payload...
    float NativeDspNodeParameters::* field {};
    // ...or a slot of its eqBands array. A slot has no value socket: cables
    // drive named fields only.
    std::int16_t slot { -1 };
    // The band it belongs to, 0 based, for its label; -1 for none.
    std::int8_t band { -1 };

    [[nodiscard]] constexpr bool modulatable() const noexcept { return field != nullptr; }
};

[[nodiscard]] inline float effectValue(const NativeDspNodeParameters& parameters, const EffectParameter& parameter) noexcept
{
    if (parameter.field != nullptr)
        return parameters.*(parameter.field);
    if (parameter.slot >= 0 && static_cast<std::size_t>(parameter.slot) < parameters.eqBands.size())
        return parameters.eqBands[static_cast<std::size_t>(parameter.slot)];
    return parameter.defaultValue;
}

inline void setEffectValue(NativeDspNodeParameters& parameters, const EffectParameter& parameter, const float value) noexcept
{
    if (parameter.field != nullptr)
        parameters.*(parameter.field) = value;
    else if (parameter.slot >= 0 && static_cast<std::size_t>(parameter.slot) < parameters.eqBands.size())
        parameters.eqBands[static_cast<std::size_t>(parameter.slot)] = value;
}

// Note values a time or a rate can follow, shortest first; a sync value of
// 0 is free, n the n-th entry. Lengths in beats.
struct NoteDivision {
    std::string_view name;
    double beats {};
};
inline constexpr std::array<NoteDivision, 12> noteDivisions { {
    { "1/32", 0.125 }, { "1/16T", 1.0 / 6.0 }, { "1/16", 0.25 }, { "1/8T", 1.0 / 3.0 }, { "1/16.", 0.375 }, { "1/8", 0.5 },
    { "1/4T", 2.0 / 3.0 }, { "1/8.", 0.75 }, { "1/4", 1.0 }, { "1/4.", 1.5 }, { "1/2", 2.0 }, { "1/1", 4.0 },
} };
// The division a sync value selects, or none when it is free.
[[nodiscard]] constexpr const NoteDivision* noteDivisionFor(const float sync) noexcept
{
    const auto index = static_cast<int>(sync + 0.5f);
    return index >= 1 && index <= static_cast<int>(noteDivisions.size()) ? &noteDivisions[static_cast<std::size_t>(index - 1)]
                                                                          : nullptr;
}

// Where an effect sits in the primitive registry.
enum class EffectGroup : std::uint8_t { modulation, delay, filter, dynamics, channels, analysis };

// The sidechain input of an effect that has one ("sidechain-in"). Far above the value ports
// (3, 4… one per parameter) so adding it moved none of them: saved cables
// name ports by number.
inline constexpr std::uint64_t effectSidechainPortId = 1'000;

struct EffectDescriptor {
    std::string_view typeKey;
    std::string_view processorKey;
    NativeDspNodeKind kind { NativeDspNodeKind::passthrough };
    std::string_view titleKey;
    std::string_view descriptionKey;
    // English, for the primitive registry and the plug-in exporter.
    std::string_view displayName;
    std::string_view categoryKey;
    EffectGroup group { EffectGroup::modulation };
    std::uint32_t colour {};
    std::span<const EffectParameter> parameters;
    // Its audio input and output: the same width, any (stereo by default),
    // unless it converts one width to another (mono to stereo).
    std::uint8_t inputChannels { 2 };
    std::uint8_t outputChannels { 2 };
    // A second audio input its detector listens to instead of its own sound
    // (a kick ducking the bass): the port effectSidechainPortId.
    bool sidechain {};

    [[nodiscard]] constexpr std::size_t parameterCount() const noexcept { return parameters.size(); }
    [[nodiscard]] constexpr bool convertsChannels() const noexcept { return inputChannels != outputChannels; }
};

// The parametric equaliser: eight bands of six values each, in order, then
// its output gain.
inline constexpr int eqProBands = 8;
inline constexpr int eqProValuesPerBand = 6;
enum class EqProValue : int { enabled, type, frequency, gain, q, slope };
[[nodiscard]] constexpr int eqProSlot(const int band, const EqProValue value) noexcept
{
    return band * eqProValuesPerBand + static_cast<int>(value);
}

namespace effect_detail {
using P = NativeDspNodeParameters;
using K = NativeDspNodeKind;
inline constexpr std::string_view effects = "nodes.category.effects";
inline constexpr std::string_view filters = "nodes.category.filters";
inline constexpr std::string_view dynamics = "nodes.category.dynamics";
inline constexpr std::string_view utilities = "nodes.category.utilities";
inline constexpr std::string_view analyzers = "nodes.category.analyzers";

// Channel conversion: a mono signal placed in the stereo field, and a stereo
// one folded to mono (centre: both halves; the balance leans to one side).
inline constexpr EffectParameter monoToStereo[] {
    { "pan", "monoStereo.pan", "effect.parameter.pan", "", -1.0f, 1.0f, 0.0f, &P::effect1 },
};
inline constexpr EffectParameter stereoToMono[] {
    { "balance", "stereoMono.balance", "effect.parameter.balance", "", -1.0f, 1.0f, 0.0f, &P::effect1 },
};

inline constexpr EffectParameter chorus[] {
    { "rate", "chorus.rate", "effect.parameter.rate", "Hz", 0.05f, 8.0f, 0.6f, &P::effect1 },
    { "depth", "chorus.depth", "effect.parameter.depth", "", 0.0f, 1.0f, 0.5f, &P::effect2 },
    { "delay", "chorus.delay", "effect.parameter.delay", "ms", 2.0f, 40.0f, 14.0f, &P::effect3 },
    { "width", "chorus.width", "effect.parameter.width", "", 0.0f, 1.0f, 0.8f, &P::effect4 },
    { "mix", "chorus.mix", "effect.parameter.mix", "", 0.0f, 1.0f, 0.5f, &P::effect5 },
};
inline constexpr EffectParameter flanger[] {
    { "rate", "flanger.rate", "effect.parameter.rate", "Hz", 0.02f, 5.0f, 0.25f, &P::effect1 },
    { "depth", "flanger.depth", "effect.parameter.depth", "", 0.0f, 1.0f, 0.7f, &P::effect2 },
    { "delay", "flanger.delay", "effect.parameter.delay", "ms", 0.3f, 8.0f, 1.5f, &P::effect3 },
    { "feedback", "flanger.feedback", "effect.parameter.feedback", "", -0.95f, 0.95f, 0.5f, &P::effect4 },
    { "mix", "flanger.mix", "effect.parameter.mix", "", 0.0f, 1.0f, 0.5f, &P::effect5 },
};
inline constexpr EffectParameter phaser[] {
    { "rate", "phaser.rate", "effect.parameter.rate", "Hz", 0.02f, 8.0f, 0.35f, &P::effect1 },
    { "depth", "phaser.depth", "effect.parameter.depth", "", 0.0f, 1.0f, 0.8f, &P::effect2 },
    { "frequency", "phaser.frequency", "effect.parameter.frequency", "Hz", 100.0f, 4'000.0f, 700.0f, &P::effect3 },
    { "feedback", "phaser.feedback", "effect.parameter.feedback", "", 0.0f, 0.9f, 0.5f, &P::effect4 },
    { "mix", "phaser.mix", "effect.parameter.mix", "", 0.0f, 1.0f, 0.5f, &P::effect5 },
};
inline constexpr EffectParameter tremolo[] {
    { "rate", "tremolo.rate", "effect.parameter.rate", "Hz", 0.1f, 20.0f, 4.0f, &P::effect1 },
    { "depth", "tremolo.depth", "effect.parameter.depth", "", 0.0f, 1.0f, 0.6f, &P::effect2 },
    { "shape", "tremolo.shape", "effect.parameter.shape", "", 0.0f, 3.0f, 0.0f, &P::effect3 },
    { "spread", "tremolo.spread", "effect.parameter.spread", "", 0.0f, 1.0f, 0.0f, &P::effect4 },
    { "sync", "tremolo.sync", "effect.parameter.sync", "", 0.0f, 12.0f, 0.0f, &P::effect5 },
};
inline constexpr EffectParameter bitcrusher[] {
    { "bits", "crusher.bits", "effect.parameter.bits", "", 1.0f, 16.0f, 8.0f, &P::effect1 },
    { "downsample", "crusher.downsample", "effect.parameter.downsample", "", 1.0f, 32.0f, 4.0f, &P::effect2 },
    { "drive", "crusher.drive", "effect.parameter.drive", "dB", 0.0f, 24.0f, 0.0f, &P::effect3 },
    { "mix", "crusher.mix", "effect.parameter.mix", "", 0.0f, 1.0f, 1.0f, &P::effect4 },
};
inline constexpr EffectParameter multimodeFilter[] {
    { "mode", "filter.mode", "effect.parameter.mode", "", 0.0f, 3.0f, 0.0f, &P::effect1 },
    { "cutoff", "filter.cutoff", "effect.parameter.cutoff", "Hz", 20.0f, 20'000.0f, 1'000.0f, &P::effect2 },
    { "resonance", "filter.resonance", "effect.parameter.resonance", "", 0.1f, 20.0f, 0.707f, &P::effect3 },
    { "drive", "filter.drive", "effect.parameter.drive", "dB", 0.0f, 24.0f, 0.0f, &P::effect4 },
};
inline constexpr EffectParameter autoFilter[] {
    { "cutoff", "autofilter.cutoff", "effect.parameter.cutoff", "Hz", 40.0f, 15'000.0f, 800.0f, &P::effect1 },
    { "resonance", "autofilter.resonance", "effect.parameter.resonance", "", 0.3f, 10.0f, 2.0f, &P::effect2 },
    { "rate", "autofilter.rate", "effect.parameter.rate", "Hz", 0.05f, 16.0f, 1.0f, &P::effect3 },
    { "depth", "autofilter.depth", "effect.parameter.depth", "", 0.0f, 1.0f, 0.5f, &P::effect4 },
    { "sync", "autofilter.sync", "effect.parameter.sync", "", 0.0f, 12.0f, 0.0f, &P::effect5 },
};
inline constexpr EffectParameter eqThree[] {
    { "low", "eq3.low", "effect.parameter.low", "dB", -24.0f, 12.0f, 0.0f, &P::effect1 },
    { "mid", "eq3.mid", "effect.parameter.mid", "dB", -24.0f, 12.0f, 0.0f, &P::effect2 },
    { "high", "eq3.high", "effect.parameter.high", "dB", -24.0f, 12.0f, 0.0f, &P::effect3 },
    { "low-frequency", "eq3.lowFrequency", "effect.parameter.lowFrequency", "Hz", 50.0f, 800.0f, 250.0f, &P::effect4 },
    { "high-frequency", "eq3.highFrequency", "effect.parameter.highFrequency", "Hz", 1'000.0f, 12'000.0f, 3'500.0f,
      &P::effect5 },
};
inline constexpr EffectParameter limiter[] {
    { "gain", "limiter.gain", "effect.parameter.gain", "dB", 0.0f, 24.0f, 0.0f, &P::effect1 },
    { "ceiling", "limiter.ceiling", "effect.parameter.ceiling", "dB", -24.0f, 0.0f, -0.3f, &P::effect2 },
    { "release", "limiter.release", "effect.parameter.release", "ms", 1.0f, 1'000.0f, 80.0f, &P::effect3 },
};
inline constexpr EffectParameter gate[] {
    { "threshold", "gate.threshold", "effect.parameter.threshold", "dB", -80.0f, 0.0f, -40.0f, &P::effect1 },
    { "attack", "gate.attack", "effect.parameter.attack", "ms", 0.1f, 50.0f, 1.0f, &P::effect2 },
    { "release", "gate.release", "effect.parameter.release", "ms", 5.0f, 2'000.0f, 120.0f, &P::effect3 },
    { "range", "gate.range", "effect.parameter.range", "dB", -80.0f, 0.0f, -80.0f, &P::effect4 },
};
inline constexpr EffectParameter transientShaper[] {
    { "attack", "transient.attack", "effect.parameter.punch", "", -1.0f, 1.0f, 0.0f, &P::effect1 },
    { "sustain", "transient.sustain", "effect.parameter.sustain", "", -1.0f, 1.0f, 0.0f, &P::effect2 },
    { "output", "transient.output", "effect.parameter.output", "dB", -12.0f, 12.0f, 0.0f, &P::effect3 },
};
inline constexpr EffectParameter pingPong[] {
    { "time", "pingpong.time", "effect.parameter.time", "ms", 10.0f, 2'000.0f, 375.0f, &P::effect1 },
    { "feedback", "pingpong.feedback", "effect.parameter.feedback", "", 0.0f, 0.95f, 0.45f, &P::effect2 },
    { "mix", "pingpong.mix", "effect.parameter.mix", "", 0.0f, 1.0f, 0.35f, &P::effect3 },
    { "width", "pingpong.width", "effect.parameter.width", "", 0.0f, 1.0f, 1.0f, &P::effect4 },
    { "sync", "pingpong.sync", "effect.parameter.sync", "", 0.0f, 12.0f, 0.0f, &P::effect5 },
};

// Each band starts off, spread across the spectrum where a first move would
// go, as a bell. Suffixes keep to two names ("eqpro.b3Gain") as every
// other binding does.
#define CYDAW_EQ_BAND(n, hz)                                                                                                    \
    { "b" #n "-enabled", "eqpro.b" #n "Enabled", "effect.parameter.enabled", "", 0.0f, 1.0f, 0.0f, nullptr,               \
      static_cast<std::int16_t>(((n) - 1) * 6 + 0), static_cast<std::int8_t>((n) - 1) },                                   \
    { "b" #n "-type", "eqpro.b" #n "Type", "effect.parameter.type", "", 0.0f, 7.0f, 0.0f, nullptr,                         \
      static_cast<std::int16_t>(((n) - 1) * 6 + 1), static_cast<std::int8_t>((n) - 1) },                                   \
    { "b" #n "-frequency", "eqpro.b" #n "Frequency", "effect.parameter.frequency", "Hz", 10.0f, 22'000.0f, (hz), nullptr,   \
      static_cast<std::int16_t>(((n) - 1) * 6 + 2), static_cast<std::int8_t>((n) - 1) },                                   \
    { "b" #n "-gain", "eqpro.b" #n "Gain", "effect.parameter.gain", "dB", -30.0f, 30.0f, 0.0f, nullptr,                    \
      static_cast<std::int16_t>(((n) - 1) * 6 + 3), static_cast<std::int8_t>((n) - 1) },                                   \
    { "b" #n "-q", "eqpro.b" #n "Q", "effect.parameter.q", "", 0.1f, 30.0f, 1.0f, nullptr,                                 \
      static_cast<std::int16_t>(((n) - 1) * 6 + 4), static_cast<std::int8_t>((n) - 1) },                                   \
    { "b" #n "-slope", "eqpro.b" #n "Slope", "effect.parameter.slope", "", 0.0f, 3.0f, 1.0f, nullptr,                      \
      static_cast<std::int16_t>(((n) - 1) * 6 + 5), static_cast<std::int8_t>((n) - 1) }

// A value kept in a slot (no socket), `band` none.
#define CYDAW_SLOT(key, suffix, label, unit, minimum, maximum, defaultValue, slot)                                             \
    EffectParameter                                                                                                            \
    {                                                                                                                          \
        key, suffix, label, unit, minimum, maximum, defaultValue, nullptr, static_cast<std::int16_t>(slot), -1                 \
    }

// The compressor's detector is high-passed from `sidechain` up: 20 Hz
// leaves it whole.
inline constexpr EffectParameter compressorPro[] {
    { "threshold", "comppro.threshold", "effect.parameter.threshold", "dB", -60.0f, 0.0f, -18.0f, &P::effect1 },
    { "ratio", "comppro.ratio", "effect.parameter.ratio", ":1", 1.0f, 20.0f, 4.0f, &P::effect2 },
    { "knee", "comppro.knee", "effect.parameter.knee", "dB", 0.0f, 24.0f, 6.0f, &P::effect3 },
    { "attack", "comppro.attack", "effect.parameter.attack", "ms", 0.05f, 250.0f, 10.0f, &P::effect4 },
    { "release", "comppro.release", "effect.parameter.release", "ms", 5.0f, 2'500.0f, 120.0f, &P::effect5 },
    { "makeup", "comppro.makeup", "effect.parameter.makeup", "dB", -12.0f, 24.0f, 0.0f, &P::effect6 },
    { "mix", "comppro.mix", "effect.parameter.mix", "", 0.0f, 1.0f, 1.0f, &P::effect7 },
    { "sidechain", "comppro.sidechain", "effect.parameter.sidechain", "Hz", 20.0f, 500.0f, 20.0f, &P::effect8 },
    // Its styles and the rest (ProEffectsDesign.h): in slots, so a file
    // written before them reads their defaults, which play as it always did.
    CYDAW_SLOT("style", "comppro.style", "effect.parameter.style", "", 0.0f, 7.0f, 0.0f, 0),
    CYDAW_SLOT("range", "comppro.range", "effect.parameter.range", "dB", 0.0f, 60.0f, 60.0f, 1),
    CYDAW_SLOT("hold", "comppro.hold", "effect.parameter.holdTime", "ms", 0.0f, 500.0f, 0.0f, 2),
    CYDAW_SLOT("lookahead", "comppro.lookahead", "effect.parameter.lookahead", "ms", 0.0f, 20.0f, 0.0f, 3),
    CYDAW_SLOT("auto-release", "comppro.autoRelease", "effect.parameter.autoRelease", "", 0.0f, 1.0f, 0.0f, 4),
    CYDAW_SLOT("auto-gain", "comppro.autoGain", "effect.parameter.autoGain", "", 0.0f, 1.0f, 0.0f, 5),
    CYDAW_SLOT("sidechain-low", "comppro.sidechainLow", "effect.parameter.sidechainLow", "Hz", 500.0f, 20'000.0f, 20'000.0f, 6),
    CYDAW_SLOT("link", "comppro.link", "effect.parameter.link", "", 0.0f, 1.0f, 1.0f, 7),
    CYDAW_SLOT("channels", "comppro.channels", "effect.parameter.channels", "", 0.0f, 2.0f, 0.0f, 8),
    CYDAW_SLOT("audition", "comppro.audition", "effect.parameter.audition", "", 0.0f, 1.0f, 0.0f, 9),
};

// Réverbe Pro: a network of sixteen delays; its decay set apart below, in
// and above the middle; early reflections; ducking; freeze.
inline constexpr EffectParameter reverbPro[] {
    { "space", "revpro.space", "effect.parameter.space", "", 0.0f, 1.0f, 0.5f, &P::effect1 },
    { "decay", "revpro.decay", "effect.parameter.decay", "s", 0.1f, 20.0f, 2.2f, &P::effect2 },
    { "predelay", "revpro.predelay", "effect.parameter.predelay", "ms", 0.0f, 500.0f, 12.0f, &P::effect3 },
    { "distance", "revpro.distance", "effect.parameter.distance", "", 0.0f, 1.0f, 0.35f, &P::effect4 },
    { "brightness", "revpro.brightness", "effect.parameter.brightness", "", -1.0f, 1.0f, 0.0f, &P::effect5 },
    { "character", "revpro.character", "effect.parameter.character", "", 0.0f, 1.0f, 0.35f, &P::effect6 },
    { "width", "revpro.width", "effect.parameter.width", "", 0.0f, 2.0f, 1.0f, &P::effect7 },
    { "mix", "revpro.mix", "effect.parameter.mix", "", 0.0f, 1.0f, 0.3f, &P::effect8 },
    CYDAW_SLOT("low-decay", "revpro.lowDecay", "effect.parameter.lowDecay", "x", 0.25f, 4.0f, 1.3f, 0),
    CYDAW_SLOT("low-frequency", "revpro.lowFrequency", "effect.parameter.lowFrequency", "Hz", 50.0f, 1'000.0f, 220.0f, 1),
    CYDAW_SLOT("mid-decay", "revpro.midDecay", "effect.parameter.midDecay", "x", 0.25f, 4.0f, 1.0f, 2),
    CYDAW_SLOT("mid-frequency", "revpro.midFrequency", "effect.parameter.midFrequency", "Hz", 200.0f, 8'000.0f, 1'200.0f, 3),
    CYDAW_SLOT("high-decay", "revpro.highDecay", "effect.parameter.highDecay", "x", 0.1f, 2.0f, 0.55f, 4),
    CYDAW_SLOT("high-frequency", "revpro.highFrequency", "effect.parameter.highFrequency", "Hz", 1'000.0f, 16'000.0f, 4'500.0f, 5),
    CYDAW_SLOT("low-cut", "revpro.lowCut", "effect.parameter.lowCut", "Hz", 10.0f, 1'000.0f, 20.0f, 6),
    CYDAW_SLOT("high-cut", "revpro.highCut", "effect.parameter.highCut", "Hz", 1'000.0f, 20'000.0f, 20'000.0f, 7),
    CYDAW_SLOT("ducking", "revpro.ducking", "effect.parameter.ducking", "dB", 0.0f, 30.0f, 0.0f, 8),
    CYDAW_SLOT("duck-release", "revpro.duckRelease", "effect.parameter.duckRelease", "ms", 20.0f, 2'000.0f, 300.0f, 9),
    CYDAW_SLOT("freeze", "revpro.freeze", "effect.parameter.freeze", "", 0.0f, 1.0f, 0.0f, 10),
    CYDAW_SLOT("sync", "revpro.sync", "effect.parameter.sync", "", 0.0f, 12.0f, 0.0f, 11),
    CYDAW_SLOT("diffusion", "revpro.diffusion", "effect.parameter.diffusion", "", 0.0f, 1.0f, 0.75f, 12),
    CYDAW_SLOT("output", "revpro.output", "effect.parameter.output", "dB", -24.0f, 12.0f, 0.0f, 13),
    CYDAW_SLOT("early", "revpro.early", "effect.parameter.early", "dB", -24.0f, 6.0f, 0.0f, 14),
};

// Limiteur Pro: input gain to a ceiling, through a look-ahead, its styles,
// true peaks, dither.
inline constexpr EffectParameter limiterPro[] {
    { "gain", "limpro.gain", "effect.parameter.gain", "dB", 0.0f, 30.0f, 0.0f, &P::effect1 },
    { "output", "limpro.output", "effect.parameter.ceiling", "dB", -30.0f, 0.0f, -1.0f, &P::effect2 },
    { "lookahead", "limpro.lookahead", "effect.parameter.lookahead", "ms", 0.0f, 5.0f, 1.0f, &P::effect3 },
    { "attack", "limpro.attack", "effect.parameter.attack", "ms", 0.1f, 100.0f, 5.0f, &P::effect4 },
    { "release", "limpro.release", "effect.parameter.release", "ms", 1.0f, 1'000.0f, 100.0f, &P::effect5 },
    { "style", "limpro.style", "effect.parameter.style", "", 0.0f, 7.0f, 0.0f, &P::effect6 },
    { "link-transients", "limpro.linkTransients", "effect.parameter.linkTransients", "", 0.0f, 1.0f, 0.75f, &P::effect7 },
    { "link-release", "limpro.linkRelease", "effect.parameter.linkRelease", "", 0.0f, 1.0f, 1.0f, &P::effect8 },
    CYDAW_SLOT("true-peak", "limpro.truePeak", "effect.parameter.truePeak", "", 0.0f, 1.0f, 1.0f, 0),
    CYDAW_SLOT("oversampling", "limpro.oversampling", "effect.parameter.oversampling", "", 0.0f, 3.0f, 1.0f, 1),
    CYDAW_SLOT("dither", "limpro.dither", "effect.parameter.dither", "", 0.0f, 4.0f, 0.0f, 2),
    CYDAW_SLOT("shaping", "limpro.shaping", "effect.parameter.shaping", "", 0.0f, 2.0f, 1.0f, 3),
    CYDAW_SLOT("dc-filter", "limpro.dcFilter", "effect.parameter.dcFilter", "", 0.0f, 1.0f, 0.0f, 4),
    CYDAW_SLOT("unity", "limpro.unity", "effect.parameter.unity", "", 0.0f, 1.0f, 0.0f, 5),
    CYDAW_SLOT("target", "limpro.target", "effect.parameter.target", "LUFS", -30.0f, -5.0f, -14.0f, 6),
};

// Filtre Pro: four filters (on, type, slope, cutoff, resonance, drive,
// stereo spread), routed in series, in parallel or in pairs; two LFOs, an
// envelope follower and four modulation slots. The first two filters'
// cutoffs and resonances, the mix, the output and the LFOs' rates have
// sockets; the rest are slots.
#define CYDAW_UNIT_SLOT(key, suffix, label, unit, minimum, maximum, defaultValue, slot, n)                                      \
    EffectParameter                                                                                                             \
    {                                                                                                                           \
    key, suffix, label, unit, minimum, maximum, defaultValue, nullptr, static_cast<std::int16_t>(slot),                         \
    static_cast<std::int8_t>((n) - 1)                                                                                           \
    }
#define CYDAW_FILTER_UNIT(n, base, on, cutoffField, resonanceField)                                                             \
    CYDAW_UNIT_SLOT("f" #n "-on", "filtpro.f" #n "On", "effect.parameter.enabled", "", 0.0f, 1.0f, (on), (base) + 0, n),        \
    CYDAW_UNIT_SLOT("f" #n "-type", "filtpro.f" #n "Type", "effect.parameter.type", "", 0.0f, 5.0f,                             \
    static_cast<float>((n) - 1), (base) + 1, n),                                                                                \
    CYDAW_UNIT_SLOT("f" #n "-slope", "filtpro.f" #n "Slope", "effect.parameter.slope", "", 0.0f, 2.0f, 1.0f, (base) + 2, n),    \
    cutoffField, resonanceField,                                                                                                \
    CYDAW_UNIT_SLOT("f" #n "-drive", "filtpro.f" #n "Drive", "effect.parameter.drive", "dB", 0.0f, 36.0f, 0.0f, (base) + 3, n), \
    CYDAW_UNIT_SLOT("f" #n "-spread", "filtpro.f" #n "Spread", "effect.parameter.spread", "oct", -2.0f, 2.0f, 0.0f, (base) + 4, n)

inline constexpr EffectParameter filterPro[] {
    CYDAW_FILTER_UNIT(1, 0, 1.0f,
                      (EffectParameter { "f1-cutoff", "filtpro.f1Cutoff", "effect.parameter.cutoff", "Hz", 20.0f, 20'000.0f,
                                         2'000.0f, &P::effect1, -1, 0 }),
                      (EffectParameter { "f1-resonance", "filtpro.f1Resonance", "effect.parameter.resonance", "", 0.0f, 1.0f,
                                         0.2f, &P::effect2, -1, 0 })),
    CYDAW_FILTER_UNIT(2, 5, 0.0f,
                      (EffectParameter { "f2-cutoff", "filtpro.f2Cutoff", "effect.parameter.cutoff", "Hz", 20.0f, 20'000.0f,
                                         300.0f, &P::effect3, -1, 1 }),
                      (EffectParameter { "f2-resonance", "filtpro.f2Resonance", "effect.parameter.resonance", "", 0.0f, 1.0f,
                                         0.2f, &P::effect4, -1, 1 })),
    CYDAW_FILTER_UNIT(3, 10, 0.0f,
                      (EffectParameter { "f3-cutoff", "filtpro.f3Cutoff", "effect.parameter.cutoff", "Hz", 20.0f, 20'000.0f,
                                         1'000.0f, nullptr, 15, 2 }),
                      (EffectParameter { "f3-resonance", "filtpro.f3Resonance", "effect.parameter.resonance", "", 0.0f, 1.0f,
                                         0.3f, nullptr, 16, 2 })),
    CYDAW_FILTER_UNIT(4, 17, 0.0f,
                      (EffectParameter { "f4-cutoff", "filtpro.f4Cutoff", "effect.parameter.cutoff", "Hz", 20.0f, 20'000.0f,
                                         4'000.0f, nullptr, 22, 3 }),
                      (EffectParameter { "f4-resonance", "filtpro.f4Resonance", "effect.parameter.resonance", "", 0.0f, 1.0f,
                                         0.3f, nullptr, 23, 3 })),
    CYDAW_SLOT("routing", "filtpro.routing", "effect.parameter.routing", "", 0.0f, 2.0f, 0.0f, 24),
    { "mix", "filtpro.mix", "effect.parameter.mix", "", 0.0f, 1.0f, 1.0f, &P::effect5 },
    { "output", "filtpro.output", "effect.parameter.output", "dB", -24.0f, 24.0f, 0.0f, &P::effect6 },
    { "lfo1-rate", "filtpro.lfo1Rate", "effect.parameter.lfo1Rate", "Hz", 0.01f, 20.0f, 1.0f, &P::effect7 },
    CYDAW_SLOT("lfo1-shape", "filtpro.lfo1Shape", "effect.parameter.lfo1Shape", "", 0.0f, 6.0f, 0.0f, 25),
    CYDAW_SLOT("lfo1-sync", "filtpro.lfo1Sync", "effect.parameter.lfo1Sync", "", 0.0f, 12.0f, 0.0f, 26),
    CYDAW_SLOT("lfo1-stereo", "filtpro.lfo1Stereo", "effect.parameter.lfo1Stereo", "", 0.0f, 0.5f, 0.0f, 27),
    { "lfo2-rate", "filtpro.lfo2Rate", "effect.parameter.lfo2Rate", "Hz", 0.01f, 20.0f, 0.25f, &P::effect8 },
    CYDAW_SLOT("lfo2-shape", "filtpro.lfo2Shape", "effect.parameter.lfo2Shape", "", 0.0f, 6.0f, 1.0f, 28),
    CYDAW_SLOT("lfo2-sync", "filtpro.lfo2Sync", "effect.parameter.lfo2Sync", "", 0.0f, 12.0f, 0.0f, 29),
    CYDAW_SLOT("lfo2-stereo", "filtpro.lfo2Stereo", "effect.parameter.lfo2Stereo", "", 0.0f, 0.5f, 0.0f, 30),
    CYDAW_SLOT("follow-attack", "filtpro.followAttack", "effect.parameter.followAttack", "ms", 0.1f, 500.0f, 10.0f, 31),
    CYDAW_SLOT("follow-release", "filtpro.followRelease", "effect.parameter.followRelease", "ms", 1.0f, 2'000.0f, 150.0f, 32),
    CYDAW_SLOT("follow-gain", "filtpro.followGain", "effect.parameter.followGain", "dB", -24.0f, 24.0f, 0.0f, 33),
    CYDAW_SLOT("m1-source", "filtpro.m1Source", "effect.parameter.m1Source", "", 0.0f, 3.0f, 0.0f, 34),
    CYDAW_SLOT("m1-destination", "filtpro.m1Destination", "effect.parameter.m1Destination", "", 0.0f, 13.0f, 1.0f, 35),
    CYDAW_SLOT("m1-amount", "filtpro.m1Amount", "effect.parameter.m1Amount", "", -1.0f, 1.0f, 0.0f, 36),
    CYDAW_SLOT("m2-source", "filtpro.m2Source", "effect.parameter.m2Source", "", 0.0f, 3.0f, 0.0f, 37),
    CYDAW_SLOT("m2-destination", "filtpro.m2Destination", "effect.parameter.m2Destination", "", 0.0f, 13.0f, 0.0f, 38),
    CYDAW_SLOT("m2-amount", "filtpro.m2Amount", "effect.parameter.m2Amount", "", -1.0f, 1.0f, 0.0f, 39),
    CYDAW_SLOT("m3-source", "filtpro.m3Source", "effect.parameter.m3Source", "", 0.0f, 3.0f, 0.0f, 40),
    CYDAW_SLOT("m3-destination", "filtpro.m3Destination", "effect.parameter.m3Destination", "", 0.0f, 13.0f, 0.0f, 41),
    CYDAW_SLOT("m3-amount", "filtpro.m3Amount", "effect.parameter.m3Amount", "", -1.0f, 1.0f, 0.0f, 42),
    CYDAW_SLOT("m4-source", "filtpro.m4Source", "effect.parameter.m4Source", "", 0.0f, 3.0f, 0.0f, 43),
    CYDAW_SLOT("m4-destination", "filtpro.m4Destination", "effect.parameter.m4Destination", "", 0.0f, 13.0f, 0.0f, 44),
    CYDAW_SLOT("m4-amount", "filtpro.m4Amount", "effect.parameter.m4Amount", "", -1.0f, 1.0f, 0.0f, 45),
};
#undef CYDAW_FILTER_UNIT
#undef CYDAW_UNIT_SLOT

// Ampli: a gate, a pedal, the preamp's valves, the tone stack, the power
// stage (AmpDesign.h). The six knobs of an amp, the output and the mix
// have sockets; the rest are slots.
inline constexpr EffectParameter ampSim[] {
    { "gain", "amp.gain", "effect.parameter.gain", "", 0.0f, 10.0f, 5.0f, &P::effect1 },
    { "bass", "amp.bass", "effect.parameter.bass", "", 0.0f, 10.0f, 5.0f, &P::effect2 },
    { "middle", "amp.middle", "effect.parameter.middle", "", 0.0f, 10.0f, 5.0f, &P::effect3 },
    { "treble", "amp.treble", "effect.parameter.treble", "", 0.0f, 10.0f, 5.0f, &P::effect4 },
    { "presence", "amp.presence", "effect.parameter.presence", "", 0.0f, 10.0f, 5.0f, &P::effect5 },
    { "master", "amp.master", "effect.parameter.master", "", 0.0f, 10.0f, 5.0f, &P::effect6 },
    { "output", "amp.output", "effect.parameter.output", "dB", -24.0f, 12.0f, 0.0f, &P::effect7 },
    { "mix", "amp.mix", "effect.parameter.mix", "", 0.0f, 1.0f, 1.0f, &P::effect8 },
    CYDAW_SLOT("model", "amp.model", "effect.parameter.ampModel", "", 0.0f, 7.0f, 1.0f, 0),
    CYDAW_SLOT("bright", "amp.bright", "effect.parameter.bright", "", 0.0f, 1.0f, 0.0f, 1),
    CYDAW_SLOT("depth", "amp.depth", "effect.parameter.depth", "", 0.0f, 10.0f, 5.0f, 2),
    CYDAW_SLOT("sag", "amp.sag", "effect.parameter.sag", "", 0.0f, 1.0f, 0.3f, 3),
    CYDAW_SLOT("tight", "amp.tight", "effect.parameter.tight", "Hz", 20.0f, 400.0f, 20.0f, 4),
    CYDAW_SLOT("gate", "amp.gate", "effect.parameter.noiseGate", "dB", -90.0f, -20.0f, -90.0f, 5),
    CYDAW_SLOT("pedal", "amp.pedal", "effect.parameter.pedal", "", 0.0f, 3.0f, 0.0f, 6),
    CYDAW_SLOT("pedal-drive", "amp.pedalDrive", "effect.parameter.pedalDrive", "", 0.0f, 1.0f, 0.5f, 7),
    CYDAW_SLOT("pedal-tone", "amp.pedalTone", "effect.parameter.pedalTone", "", 0.0f, 1.0f, 0.5f, 8),
    CYDAW_SLOT("pedal-level", "amp.pedalLevel", "effect.parameter.pedalLevel", "dB", -12.0f, 12.0f, 0.0f, 9),
    CYDAW_SLOT("oversampling", "amp.oversampling", "effect.parameter.oversampling", "", 0.0f, 2.0f, 2.0f, 10),
    // Channel B, a lead to channel A's clean to begin with.
    CYDAW_SLOT("channel", "amp.channel", "effect.parameter.ampChannel", "", 0.0f, 1.0f, 0.0f, 11),
    CYDAW_SLOT("model-b", "amp.modelB", "effect.parameter.ampModelB", "", 0.0f, 7.0f, 2.0f, 12),
    CYDAW_SLOT("gain-b", "amp.gainB", "effect.parameter.gainB", "", 0.0f, 10.0f, 7.0f, 13),
    CYDAW_SLOT("bass-b", "amp.bassB", "effect.parameter.bassB", "", 0.0f, 10.0f, 5.0f, 14),
    CYDAW_SLOT("middle-b", "amp.middleB", "effect.parameter.middleB", "", 0.0f, 10.0f, 6.0f, 15),
    CYDAW_SLOT("treble-b", "amp.trebleB", "effect.parameter.trebleB", "", 0.0f, 10.0f, 5.5f, 16),
    CYDAW_SLOT("presence-b", "amp.presenceB", "effect.parameter.presenceB", "", 0.0f, 10.0f, 6.0f, 17),
    CYDAW_SLOT("master-b", "amp.masterB", "effect.parameter.masterB", "", 0.0f, 10.0f, 5.0f, 18),
    CYDAW_SLOT("bright-b", "amp.brightB", "effect.parameter.brightB", "", 0.0f, 1.0f, 0.0f, 19),
    CYDAW_SLOT("depth-b", "amp.depthB", "effect.parameter.depthB", "", 0.0f, 10.0f, 5.0f, 20),
};

// Baffle et micro: a cabinet and the microphones on it, A and B (where on
// the speaker, how far, at what angle), their blend and the room.
inline constexpr EffectParameter cabSim[] {
    { "position-a", "cab.positionA", "effect.parameter.positionA", "", 0.0f, 1.0f, 0.3f, &P::effect1 },
    { "distance-a", "cab.distanceA", "effect.parameter.distanceA", "cm", 1.0f, 100.0f, 3.0f, &P::effect2 },
    { "position-b", "cab.positionB", "effect.parameter.positionB", "", 0.0f, 1.0f, 0.6f, &P::effect3 },
    { "distance-b", "cab.distanceB", "effect.parameter.distanceB", "cm", 1.0f, 100.0f, 10.0f, &P::effect4 },
    { "blend", "cab.blend", "effect.parameter.blend", "", 0.0f, 1.0f, 0.5f, &P::effect5 },
    { "room", "cab.room", "effect.parameter.room", "", 0.0f, 1.0f, 0.1f, &P::effect6 },
    { "low-cut", "cab.lowCut", "effect.parameter.lowCut", "Hz", 20.0f, 400.0f, 20.0f, &P::effect7 },
    { "output", "cab.output", "effect.parameter.output", "dB", -24.0f, 24.0f, 0.0f, &P::effect8 },
    CYDAW_SLOT("model", "cab.model", "effect.parameter.cabinet", "", 0.0f, 7.0f, 3.0f, 0),
    CYDAW_SLOT("mic-a", "cab.micA", "effect.parameter.micA", "", 0.0f, 6.0f, 0.0f, 1),
    CYDAW_SLOT("angle-a", "cab.angleA", "effect.parameter.angleA", "\xc2\xb0", 0.0f, 90.0f, 0.0f, 2),
    CYDAW_SLOT("mic-b", "cab.micB", "effect.parameter.micB", "", 0.0f, 6.0f, 2.0f, 3),
    CYDAW_SLOT("angle-b", "cab.angleB", "effect.parameter.angleB", "\xc2\xb0", 0.0f, 90.0f, 0.0f, 4),
    CYDAW_SLOT("mic-b-on", "cab.micBOn", "effect.parameter.micBOn", "", 0.0f, 1.0f, 0.0f, 5),
    CYDAW_SLOT("polarity-b", "cab.polarityB", "effect.parameter.polarityB", "", 0.0f, 1.0f, 0.0f, 6),
    CYDAW_SLOT("align", "cab.align", "effect.parameter.align", "", 0.0f, 1.0f, 1.0f, 7),
    CYDAW_SLOT("high-cut", "cab.highCut", "effect.parameter.highCut", "Hz", 2'000.0f, 20'000.0f, 20'000.0f, 8),
    CYDAW_SLOT("resonance", "cab.resonance", "effect.parameter.resonance", "", 0.0f, 1.0f, 0.5f, 9),
    CYDAW_SLOT("stereo", "cab.stereo", "effect.parameter.stereoMics", "", 0.0f, 1.0f, 0.0f, 10),
    CYDAW_SLOT("mix", "cab.mix", "effect.parameter.mix", "", 0.0f, 1.0f, 1.0f, 11),
    CYDAW_SLOT("room-size", "cab.roomSize", "effect.parameter.roomSize", "", 0.0f, 1.0f, 0.25f, 12),
    CYDAW_SLOT("room-tone", "cab.roomTone", "effect.parameter.roomTone", "Hz", 1'000.0f, 12'000.0f, 5'000.0f, 13),
};

// Micro: a sound as a microphone takes it (its model, distance, angle and
// pattern), its preamp, the room.
inline constexpr EffectParameter micSim[] {
    { "distance", "micsim.distance", "effect.parameter.distance", "cm", 1.0f, 200.0f, 15.0f, &P::effect1 },
    { "angle", "micsim.angle", "effect.parameter.angle", "\xc2\xb0", 0.0f, 180.0f, 0.0f, &P::effect2 },
    { "drive", "micsim.drive", "effect.parameter.drive", "dB", 0.0f, 30.0f, 0.0f, &P::effect3 },
    { "low-cut", "micsim.lowCut", "effect.parameter.lowCut", "Hz", 20.0f, 500.0f, 20.0f, &P::effect4 },
    { "high-cut", "micsim.highCut", "effect.parameter.highCut", "Hz", 1'000.0f, 20'000.0f, 20'000.0f, &P::effect5 },
    { "room", "micsim.room", "effect.parameter.room", "", 0.0f, 1.0f, 0.0f, &P::effect6 },
    { "output", "micsim.output", "effect.parameter.output", "dB", -24.0f, 24.0f, 0.0f, &P::effect7 },
    { "mix", "micsim.mix", "effect.parameter.mix", "", 0.0f, 1.0f, 1.0f, &P::effect8 },
    CYDAW_SLOT("model", "micsim.model", "effect.parameter.microphone", "", 0.0f, 11.0f, 3.0f, 0),
    CYDAW_SLOT("pattern", "micsim.pattern", "effect.parameter.pattern", "", 0.0f, 4.0f, 0.0f, 1),
    CYDAW_SLOT("preamp", "micsim.preamp", "effect.parameter.preamp", "", 0.0f, 2.0f, 0.0f, 2),
    CYDAW_SLOT("noise", "micsim.noise", "effect.parameter.noise", "dB", -90.0f, -20.0f, -90.0f, 3),
    CYDAW_SLOT("room-size", "micsim.roomSize", "effect.parameter.roomSize", "", 0.0f, 1.0f, 0.25f, 4),
    CYDAW_SLOT("room-tone", "micsim.roomTone", "effect.parameter.roomTone", "Hz", 1'000.0f, 12'000.0f, 5'000.0f, 5),
};

// Pression: three bands, each held down above its upper threshold and raised
// below its lower one (MixDesign.h). The depth, the time, the amounts up and
// down, the bands' gains and the output have sockets.
inline constexpr EffectParameter pression[] {
    { "depth", "pression.depth", "effect.parameter.depth", "", 0.0f, 1.0f, 1.0f, &P::effect1 },
    { "time", "pression.time", "effect.parameter.time", "", 0.0f, 1.0f, 0.5f, &P::effect2 },
    { "upward", "pression.upward", "effect.parameter.upward", "", 0.0f, 1.0f, 1.0f, &P::effect3 },
    { "downward", "pression.downward", "effect.parameter.downward", "", 0.0f, 1.0f, 1.0f, &P::effect4 },
    { "low-gain", "pression.lowGain", "effect.parameter.lowGain", "dB", -24.0f, 24.0f, 0.0f, &P::effect5 },
    { "mid-gain", "pression.midGain", "effect.parameter.midGain", "dB", -24.0f, 24.0f, 0.0f, &P::effect6 },
    { "high-gain", "pression.highGain", "effect.parameter.highGain", "dB", -24.0f, 24.0f, 0.0f, &P::effect7 },
    { "output", "pression.output", "effect.parameter.output", "dB", -24.0f, 24.0f, 0.0f, &P::effect8 },
    CYDAW_SLOT("input", "pression.input", "effect.parameter.input", "dB", -24.0f, 24.0f, 0.0f, 0),
    CYDAW_SLOT("cross-low", "pression.crossLow", "effect.parameter.crossLow", "Hz", 40.0f, 400.0f, 88.0f, 1),
    CYDAW_SLOT("cross-high", "pression.crossHigh", "effect.parameter.crossHigh", "Hz", 800.0f, 10'000.0f, 2'500.0f, 2),
    CYDAW_SLOT("low-down-threshold", "pression.lowDownThreshold", "effect.parameter.lowDownThreshold", "dB", -60.0f, 0.0f, -33.8f, 3),
    CYDAW_SLOT("low-down-ratio", "pression.lowDownRatio", "effect.parameter.lowDownRatio", "", 1.0f, 100.0f, 66.7f, 4),
    CYDAW_SLOT("low-up-threshold", "pression.lowUpThreshold", "effect.parameter.lowUpThreshold", "dB", -80.0f, 0.0f, -41.8f, 5),
    CYDAW_SLOT("low-up-ratio", "pression.lowUpRatio", "effect.parameter.lowUpRatio", "", 1.0f, 10.0f, 4.17f, 6),
    CYDAW_SLOT("low-attack", "pression.lowAttack", "effect.parameter.lowAttack", "ms", 0.1f, 300.0f, 47.8f, 7),
    CYDAW_SLOT("low-release", "pression.lowRelease", "effect.parameter.lowRelease", "ms", 5.0f, 2'000.0f, 282.0f, 8),
    CYDAW_SLOT("mid-down-threshold", "pression.midDownThreshold", "effect.parameter.midDownThreshold", "dB", -60.0f, 0.0f, -30.2f, 9),
    CYDAW_SLOT("mid-down-ratio", "pression.midDownRatio", "effect.parameter.midDownRatio", "", 1.0f, 100.0f, 66.7f, 10),
    CYDAW_SLOT("mid-up-threshold", "pression.midUpThreshold", "effect.parameter.midUpThreshold", "dB", -80.0f, 0.0f, -41.8f, 11),
    CYDAW_SLOT("mid-up-ratio", "pression.midUpRatio", "effect.parameter.midUpRatio", "", 1.0f, 10.0f, 4.17f, 12),
    CYDAW_SLOT("mid-attack", "pression.midAttack", "effect.parameter.midAttack", "ms", 0.1f, 300.0f, 22.4f, 13),
    CYDAW_SLOT("mid-release", "pression.midRelease", "effect.parameter.midRelease", "ms", 5.0f, 2'000.0f, 282.0f, 14),
    CYDAW_SLOT("high-down-threshold", "pression.highDownThreshold", "effect.parameter.highDownThreshold", "dB", -60.0f, 0.0f, -35.5f, 15),
    CYDAW_SLOT("high-down-ratio", "pression.highDownRatio", "effect.parameter.highDownRatio", "", 1.0f, 100.0f, 66.7f, 16),
    CYDAW_SLOT("high-up-threshold", "pression.highUpThreshold", "effect.parameter.highUpThreshold", "dB", -80.0f, 0.0f, -40.8f, 17),
    CYDAW_SLOT("high-up-ratio", "pression.highUpRatio", "effect.parameter.highUpRatio", "", 1.0f, 10.0f, 4.17f, 18),
    CYDAW_SLOT("high-attack", "pression.highAttack", "effect.parameter.highAttack", "ms", 0.1f, 300.0f, 13.5f, 19),
    CYDAW_SLOT("high-release", "pression.highRelease", "effect.parameter.highRelease", "ms", 5.0f, 2'000.0f, 132.0f, 20),
    CYDAW_SLOT("solo", "pression.solo", "effect.parameter.soloBand", "", 0.0f, 3.0f, 0.0f, 21),
};

// Fournaise: analog saturation (MixDesign.h). The drive, the tone, the cuts,
// the mix and the output have sockets; the character and the options are slots.
inline constexpr EffectParameter fournaise[] {
    { "drive", "fournaise.drive", "effect.parameter.drive", "", 0.0f, 10.0f, 3.0f, &P::effect1 },
    { "tone", "fournaise.tone", "effect.parameter.tone", "", -1.0f, 1.0f, 0.0f, &P::effect2 },
    { "low-cut", "fournaise.lowCut", "effect.parameter.lowCut", "Hz", 10.0f, 1'000.0f, 20.0f, &P::effect3 },
    { "high-cut", "fournaise.highCut", "effect.parameter.highCut", "Hz", 500.0f, 20'000.0f, 20'000.0f, &P::effect4 },
    { "mix", "fournaise.mix", "effect.parameter.mix", "", 0.0f, 1.0f, 1.0f, &P::effect5 },
    { "output", "fournaise.output", "effect.parameter.output", "dB", -24.0f, 12.0f, 0.0f, &P::effect6 },
    CYDAW_SLOT("style", "fournaise.style", "effect.parameter.style", "", 0.0f, 4.0f, 0.0f, 0),
    CYDAW_SLOT("punish", "fournaise.punish", "effect.parameter.punish", "", 0.0f, 1.0f, 0.0f, 1),
    CYDAW_SLOT("auto", "fournaise.auto", "effect.parameter.autoLevel", "", 0.0f, 1.0f, 1.0f, 2),
    CYDAW_SLOT("thump", "fournaise.thump", "effect.parameter.thump", "", 0.0f, 1.0f, 0.0f, 3),
    CYDAW_SLOT("steep", "fournaise.steep", "effect.parameter.steep", "", 0.0f, 1.0f, 0.0f, 4),
    CYDAW_SLOT("oversampling", "fournaise.oversampling", "effect.parameter.oversampling", "", 0.0f, 2.0f, 1.0f, 5),
};

// Écho Bande: a tape echo (MixDesign.h). Its eight knobs have sockets; the
// mode, the sync, the input drive, the tape's age and the width are slots.
inline constexpr EffectParameter echoBande[] {
    { "time", "echoband.time", "effect.parameter.time", "ms", 30.0f, 350.0f, 120.0f, &P::effect1 },
    { "intensity", "echoband.intensity", "effect.parameter.intensity", "", 0.0f, 1.2f, 0.45f, &P::effect2 },
    { "echo", "echoband.echo", "effect.parameter.echoLevel", "", 0.0f, 1.0f, 0.7f, &P::effect3 },
    { "reverb", "echoband.reverb", "effect.parameter.springLevel", "", 0.0f, 1.0f, 0.3f, &P::effect4 },
    { "bass", "echoband.bass", "effect.parameter.bass", "dB", -12.0f, 12.0f, 0.0f, &P::effect5 },
    { "treble", "echoband.treble", "effect.parameter.treble", "dB", -12.0f, 12.0f, 0.0f, &P::effect6 },
    { "wow", "echoband.wow", "effect.parameter.wow", "", 0.0f, 1.0f, 0.3f, &P::effect7 },
    { "mix", "echoband.mix", "effect.parameter.mix", "", 0.0f, 1.0f, 0.5f, &P::effect8 },
    CYDAW_SLOT("mode", "echoband.mode", "effect.parameter.echoMode", "", 0.0f, 11.0f, 4.0f, 0),
    CYDAW_SLOT("sync", "echoband.sync", "effect.parameter.sync", "", 0.0f, 12.0f, 0.0f, 1),
    CYDAW_SLOT("drive", "echoband.drive", "effect.parameter.inputDrive", "dB", 0.0f, 24.0f, 0.0f, 2),
    CYDAW_SLOT("age", "echoband.age", "effect.parameter.tapeAge", "", 0.0f, 1.0f, 0.3f, 3),
    CYDAW_SLOT("width", "echoband.width", "effect.parameter.width", "", 0.0f, 1.0f, 0.5f, 4),
};

// Multibande Pro: four bands (MixDesign.h). Their thresholds, the input, the
// output, the amount and the link have sockets; the crossovers and each
// band's mode, ratio, range, times, knee, makeup, on and solo are slots.
inline constexpr EffectParameter multibandPro[] {
    { "threshold-1", "multiband.threshold1", "effect.parameter.threshold1", "dB", -60.0f, 0.0f, -24.0f, &P::effect1 },
    { "threshold-2", "multiband.threshold2", "effect.parameter.threshold2", "dB", -60.0f, 0.0f, -24.0f, &P::effect2 },
    { "threshold-3", "multiband.threshold3", "effect.parameter.threshold3", "dB", -60.0f, 0.0f, -24.0f, &P::effect3 },
    { "threshold-4", "multiband.threshold4", "effect.parameter.threshold4", "dB", -60.0f, 0.0f, -24.0f, &P::effect4 },
    { "input", "multiband.input", "effect.parameter.input", "dB", -24.0f, 24.0f, 0.0f, &P::effect5 },
    { "output", "multiband.output", "effect.parameter.output", "dB", -24.0f, 24.0f, 0.0f, &P::effect6 },
    { "amount", "multiband.amount", "effect.parameter.amount", "", 0.0f, 1.0f, 1.0f, &P::effect7 },
    { "link", "multiband.link", "effect.parameter.link", "", 0.0f, 1.0f, 1.0f, &P::effect8 },
    CYDAW_SLOT("cross-1", "multiband.cross1", "effect.parameter.cross1", "Hz", 20.0f, 1'000.0f, 120.0f, 0),
    CYDAW_SLOT("cross-2", "multiband.cross2", "effect.parameter.cross2", "Hz", 200.0f, 8'000.0f, 1'000.0f, 1),
    CYDAW_SLOT("cross-3", "multiband.cross3", "effect.parameter.cross3", "Hz", 1'000.0f, 18'000.0f, 6'000.0f, 2),
    CYDAW_SLOT("band1-mode", "multiband.band1Mode", "effect.parameter.band1Mode", "", 0.0f, 3.0f, 0.0f, 3),
    CYDAW_SLOT("band1-ratio", "multiband.band1Ratio", "effect.parameter.band1Ratio", "", 1.0f, 20.0f, 2.5f, 4),
    CYDAW_SLOT("band1-range", "multiband.band1Range", "effect.parameter.band1Range", "dB", 0.0f, 48.0f, 12.0f, 5),
    CYDAW_SLOT("band1-attack", "multiband.band1Attack", "effect.parameter.band1Attack", "ms", 0.1f, 300.0f, 10.0f, 6),
    CYDAW_SLOT("band1-release", "multiband.band1Release", "effect.parameter.band1Release", "ms", 5.0f, 2'000.0f, 120.0f, 7),
    CYDAW_SLOT("band1-knee", "multiband.band1Knee", "effect.parameter.band1Knee", "dB", 0.0f, 24.0f, 6.0f, 8),
    CYDAW_SLOT("band1-makeup", "multiband.band1Makeup", "effect.parameter.band1Makeup", "dB", -24.0f, 24.0f, 0.0f, 9),
    CYDAW_SLOT("band1-on", "multiband.band1On", "effect.parameter.band1On", "", 0.0f, 1.0f, 1.0f, 10),
    CYDAW_SLOT("band1-solo", "multiband.band1Solo", "effect.parameter.band1Solo", "", 0.0f, 1.0f, 0.0f, 11),
    CYDAW_SLOT("band2-mode", "multiband.band2Mode", "effect.parameter.band2Mode", "", 0.0f, 3.0f, 0.0f, 12),
    CYDAW_SLOT("band2-ratio", "multiband.band2Ratio", "effect.parameter.band2Ratio", "", 1.0f, 20.0f, 2.5f, 13),
    CYDAW_SLOT("band2-range", "multiband.band2Range", "effect.parameter.band2Range", "dB", 0.0f, 48.0f, 12.0f, 14),
    CYDAW_SLOT("band2-attack", "multiband.band2Attack", "effect.parameter.band2Attack", "ms", 0.1f, 300.0f, 10.0f, 15),
    CYDAW_SLOT("band2-release", "multiband.band2Release", "effect.parameter.band2Release", "ms", 5.0f, 2'000.0f, 120.0f, 16),
    CYDAW_SLOT("band2-knee", "multiband.band2Knee", "effect.parameter.band2Knee", "dB", 0.0f, 24.0f, 6.0f, 17),
    CYDAW_SLOT("band2-makeup", "multiband.band2Makeup", "effect.parameter.band2Makeup", "dB", -24.0f, 24.0f, 0.0f, 18),
    CYDAW_SLOT("band2-on", "multiband.band2On", "effect.parameter.band2On", "", 0.0f, 1.0f, 1.0f, 19),
    CYDAW_SLOT("band2-solo", "multiband.band2Solo", "effect.parameter.band2Solo", "", 0.0f, 1.0f, 0.0f, 20),
    CYDAW_SLOT("band3-mode", "multiband.band3Mode", "effect.parameter.band3Mode", "", 0.0f, 3.0f, 0.0f, 21),
    CYDAW_SLOT("band3-ratio", "multiband.band3Ratio", "effect.parameter.band3Ratio", "", 1.0f, 20.0f, 2.5f, 22),
    CYDAW_SLOT("band3-range", "multiband.band3Range", "effect.parameter.band3Range", "dB", 0.0f, 48.0f, 12.0f, 23),
    CYDAW_SLOT("band3-attack", "multiband.band3Attack", "effect.parameter.band3Attack", "ms", 0.1f, 300.0f, 10.0f, 24),
    CYDAW_SLOT("band3-release", "multiband.band3Release", "effect.parameter.band3Release", "ms", 5.0f, 2'000.0f, 120.0f, 25),
    CYDAW_SLOT("band3-knee", "multiband.band3Knee", "effect.parameter.band3Knee", "dB", 0.0f, 24.0f, 6.0f, 26),
    CYDAW_SLOT("band3-makeup", "multiband.band3Makeup", "effect.parameter.band3Makeup", "dB", -24.0f, 24.0f, 0.0f, 27),
    CYDAW_SLOT("band3-on", "multiband.band3On", "effect.parameter.band3On", "", 0.0f, 1.0f, 1.0f, 28),
    CYDAW_SLOT("band3-solo", "multiband.band3Solo", "effect.parameter.band3Solo", "", 0.0f, 1.0f, 0.0f, 29),
    CYDAW_SLOT("band4-mode", "multiband.band4Mode", "effect.parameter.band4Mode", "", 0.0f, 3.0f, 0.0f, 30),
    CYDAW_SLOT("band4-ratio", "multiband.band4Ratio", "effect.parameter.band4Ratio", "", 1.0f, 20.0f, 2.5f, 31),
    CYDAW_SLOT("band4-range", "multiband.band4Range", "effect.parameter.band4Range", "dB", 0.0f, 48.0f, 12.0f, 32),
    CYDAW_SLOT("band4-attack", "multiband.band4Attack", "effect.parameter.band4Attack", "ms", 0.1f, 300.0f, 10.0f, 33),
    CYDAW_SLOT("band4-release", "multiband.band4Release", "effect.parameter.band4Release", "ms", 5.0f, 2'000.0f, 120.0f, 34),
    CYDAW_SLOT("band4-knee", "multiband.band4Knee", "effect.parameter.band4Knee", "dB", 0.0f, 24.0f, 6.0f, 35),
    CYDAW_SLOT("band4-makeup", "multiband.band4Makeup", "effect.parameter.band4Makeup", "dB", -24.0f, 24.0f, 0.0f, 36),
    CYDAW_SLOT("band4-on", "multiband.band4On", "effect.parameter.band4On", "", 0.0f, 1.0f, 1.0f, 37),
    CYDAW_SLOT("band4-solo", "multiband.band4Solo", "effect.parameter.band4Solo", "", 0.0f, 1.0f, 0.0f, 38),
};

// Velours: resonances found and calmed (MixDesign.h). Its eight knobs have
// sockets; the mode, the delta, the output and the link are slots.
inline constexpr EffectParameter velours[] {
    { "depth", "velours.depth", "effect.parameter.depth", "dB", 0.0f, 24.0f, 6.0f, &P::effect1 },
    { "sharpness", "velours.sharpness", "effect.parameter.sharpness", "", 0.0f, 1.0f, 0.5f, &P::effect2 },
    { "selectivity", "velours.selectivity", "effect.parameter.selectivity", "", 0.0f, 1.0f, 0.5f, &P::effect3 },
    { "attack", "velours.attack", "effect.parameter.attack", "ms", 1.0f, 200.0f, 5.0f, &P::effect4 },
    { "release", "velours.release", "effect.parameter.release", "ms", 5.0f, 500.0f, 80.0f, &P::effect5 },
    { "focus-low", "velours.focusLow", "effect.parameter.focusLow", "Hz", 20.0f, 20'000.0f, 200.0f, &P::effect6 },
    { "focus-high", "velours.focusHigh", "effect.parameter.focusHigh", "Hz", 20.0f, 20'000.0f, 12'000.0f, &P::effect7 },
    { "mix", "velours.mix", "effect.parameter.mix", "", 0.0f, 1.0f, 1.0f, &P::effect8 },
    CYDAW_SLOT("mode", "velours.mode", "effect.parameter.mode", "", 0.0f, 1.0f, 0.0f, 0),
    CYDAW_SLOT("delta", "velours.delta", "effect.parameter.delta", "", 0.0f, 1.0f, 0.0f, 1),
    CYDAW_SLOT("output", "velours.output", "effect.parameter.output", "dB", -12.0f, 12.0f, 0.0f, 2),
    CYDAW_SLOT("link", "velours.link", "effect.parameter.link", "", 0.0f, 1.0f, 1.0f, 3),
};

// Patine: the sound made old (MixDesign.h). The six modules' amounts, the
// magnitude and the mix have sockets; the kinds, rates, size, tone, width,
// output and the noise following the sound are slots.
inline constexpr EffectParameter patine[] {
    { "noise", "patine.noise", "effect.parameter.noise", "", 0.0f, 1.0f, 0.25f, &P::effect1 },
    { "wobble", "patine.wobble", "effect.parameter.wobble", "", 0.0f, 1.0f, 0.25f, &P::effect2 },
    { "distort", "patine.distort", "effect.parameter.distort", "", 0.0f, 1.0f, 0.2f, &P::effect3 },
    { "digital", "patine.digital", "effect.parameter.digital", "", 0.0f, 1.0f, 0.0f, &P::effect4 },
    { "space", "patine.space", "effect.parameter.space", "", 0.0f, 1.0f, 0.15f, &P::effect5 },
    { "magnetic", "patine.magnetic", "effect.parameter.magnetic", "", 0.0f, 1.0f, 0.2f, &P::effect6 },
    { "magnitude", "patine.magnitude", "effect.parameter.magnitude", "", 0.0f, 1.0f, 1.0f, &P::effect7 },
    { "mix", "patine.mix", "effect.parameter.mix", "", 0.0f, 1.0f, 1.0f, &P::effect8 },
    CYDAW_SLOT("noise-type", "patine.noiseType", "effect.parameter.noiseType", "", 0.0f, 2.0f, 0.0f, 0),
    CYDAW_SLOT("wobble-rate", "patine.wobbleRate", "effect.parameter.wobbleRate", "", 0.0f, 1.0f, 0.3f, 1),
    CYDAW_SLOT("distort-type", "patine.distortType", "effect.parameter.distortType", "", 0.0f, 2.0f, 0.0f, 2),
    CYDAW_SLOT("space-size", "patine.spaceSize", "effect.parameter.roomSize", "", 0.0f, 1.0f, 0.4f, 3),
    CYDAW_SLOT("tone", "patine.tone", "effect.parameter.tone", "", -1.0f, 1.0f, 0.0f, 4),
    CYDAW_SLOT("width", "patine.width", "effect.parameter.width", "", 0.0f, 1.5f, 1.0f, 5),
    CYDAW_SLOT("output", "patine.output", "effect.parameter.output", "dB", -24.0f, 12.0f, 0.0f, 6),
    CYDAW_SLOT("noise-follow", "patine.noiseFollow", "effect.parameter.noiseFollow", "", 0.0f, 1.0f, 0.0f, 7),
};

// Sonomètre: the sound passes untouched; its values are the display's (the
// target, the history's length, the true peak allowed).
inline constexpr EffectParameter sonometre[] {
    { "target", "sonometre.target", "effect.parameter.loudnessTarget", "", 0.0f, 6.0f, 0.0f, &P::effect1 },
    { "history", "sonometre.history", "effect.parameter.history", "s", 10.0f, 600.0f, 60.0f, &P::effect2 },
    { "true-peak", "sonometre.truePeak", "effect.parameter.truePeakLimit", "dB", -3.0f, 0.0f, -1.0f, &P::effect3 },
};

// Tranche: a channel strip (SuiteDesign.h). The gate's and the compressor's
// thresholds, the transients, the exciter, the width, the output and the mix
// have sockets; each stage's switch and its settings are slots.
inline constexpr EffectParameter tranche[] {
    { "gate-threshold", "tranche.gateThreshold", "effect.parameter.gateThreshold", "dB", -80.0f, 0.0f, -60.0f, &P::effect1 },
    { "comp-threshold", "tranche.compThreshold", "effect.parameter.compThreshold", "dB", -60.0f, 0.0f, -18.0f, &P::effect2 },
    { "attack", "tranche.attack", "effect.parameter.transientAttack", "", -1.0f, 1.0f, 0.0f, &P::effect3 },
    { "sustain", "tranche.sustain", "effect.parameter.transientSustain", "", -1.0f, 1.0f, 0.0f, &P::effect4 },
    { "excite", "tranche.excite", "effect.parameter.excite", "", 0.0f, 1.0f, 0.2f, &P::effect5 },
    { "width", "tranche.width", "effect.parameter.width", "", 0.0f, 2.0f, 1.0f, &P::effect6 },
    { "output", "tranche.output", "effect.parameter.output", "dB", -24.0f, 24.0f, 0.0f, &P::effect7 },
    { "mix", "tranche.mix", "effect.parameter.mix", "", 0.0f, 1.0f, 1.0f, &P::effect8 },
    CYDAW_SLOT("gate-on", "tranche.gateOn", "effect.parameter.gateOn", "", 0.0f, 1.0f, 0.0f, 0),
    CYDAW_SLOT("eq-on", "tranche.eqOn", "effect.parameter.eqOn", "", 0.0f, 1.0f, 1.0f, 1),
    CYDAW_SLOT("comp-on", "tranche.compOn", "effect.parameter.compOn", "", 0.0f, 1.0f, 1.0f, 2),
    CYDAW_SLOT("transient-on", "tranche.transientOn", "effect.parameter.transientOn", "", 0.0f, 1.0f, 0.0f, 3),
    CYDAW_SLOT("exciter-on", "tranche.exciterOn", "effect.parameter.exciterOn", "", 0.0f, 1.0f, 0.0f, 4),
    CYDAW_SLOT("gate-range", "tranche.gateRange", "effect.parameter.gateRange", "dB", 0.0f, 80.0f, 40.0f, 5),
    CYDAW_SLOT("gate-attack", "tranche.gateAttack", "effect.parameter.gateAttack", "ms", 0.1f, 50.0f, 1.0f, 6),
    CYDAW_SLOT("gate-release", "tranche.gateRelease", "effect.parameter.gateRelease", "ms", 5.0f, 1'000.0f, 100.0f, 7),
    CYDAW_SLOT("low-cut", "tranche.lowCut", "effect.parameter.lowCut", "Hz", 10.0f, 500.0f, 10.0f, 8),
    CYDAW_SLOT("low-shelf-hz", "tranche.lowShelfHz", "effect.parameter.lowShelfHz", "Hz", 40.0f, 500.0f, 120.0f, 9),
    CYDAW_SLOT("low-shelf-db", "tranche.lowShelfDb", "effect.parameter.lowShelfDb", "dB", -12.0f, 12.0f, 0.0f, 10),
    CYDAW_SLOT("bell1-hz", "tranche.bell1Hz", "effect.parameter.bell1Hz", "Hz", 80.0f, 8'000.0f, 400.0f, 11),
    CYDAW_SLOT("bell1-db", "tranche.bell1Db", "effect.parameter.bell1Db", "dB", -12.0f, 12.0f, 0.0f, 12),
    CYDAW_SLOT("bell1-q", "tranche.bell1Q", "effect.parameter.bell1Q", "", 0.3f, 8.0f, 1.0f, 13),
    CYDAW_SLOT("bell2-hz", "tranche.bell2Hz", "effect.parameter.bell2Hz", "Hz", 300.0f, 16'000.0f, 3'000.0f, 14),
    CYDAW_SLOT("bell2-db", "tranche.bell2Db", "effect.parameter.bell2Db", "dB", -12.0f, 12.0f, 0.0f, 15),
    CYDAW_SLOT("bell2-q", "tranche.bell2Q", "effect.parameter.bell2Q", "", 0.3f, 8.0f, 1.0f, 16),
    CYDAW_SLOT("high-shelf-hz", "tranche.highShelfHz", "effect.parameter.highShelfHz", "Hz", 2'000.0f, 20'000.0f, 8'000.0f, 17),
    CYDAW_SLOT("high-shelf-db", "tranche.highShelfDb", "effect.parameter.highShelfDb", "dB", -12.0f, 12.0f, 0.0f, 18),
    CYDAW_SLOT("high-cut", "tranche.highCut", "effect.parameter.highCut", "Hz", 2'000.0f, 20'000.0f, 20'000.0f, 19),
    CYDAW_SLOT("comp-ratio", "tranche.compRatio", "effect.parameter.ratio", "", 1.0f, 20.0f, 3.0f, 20),
    CYDAW_SLOT("comp-attack", "tranche.compAttack", "effect.parameter.compAttack", "ms", 0.1f, 200.0f, 10.0f, 21),
    CYDAW_SLOT("comp-release", "tranche.compRelease", "effect.parameter.compRelease", "ms", 10.0f, 2'000.0f, 120.0f, 22),
    CYDAW_SLOT("comp-knee", "tranche.compKnee", "effect.parameter.knee", "dB", 0.0f, 24.0f, 6.0f, 23),
    CYDAW_SLOT("comp-makeup", "tranche.compMakeup", "effect.parameter.makeup", "dB", -12.0f, 24.0f, 0.0f, 24),
    CYDAW_SLOT("exciter-hz", "tranche.exciterHz", "effect.parameter.exciterHz", "Hz", 1'000.0f, 12'000.0f, 3'000.0f, 25),
    CYDAW_SLOT("exciter-style", "tranche.exciterStyle", "effect.parameter.exciterStyle", "", 0.0f, 2.0f, 0.0f, 26),
};

// Finition: a mastering chain (SuiteDesign.h). The maximizer's gain and
// ceiling, the amounts of dynamics and exciter, the width and the three
// tone controls have sockets; the rest are slots.
inline constexpr EffectParameter finition[] {
    { "gain", "finition.gain", "effect.parameter.maxGain", "dB", 0.0f, 18.0f, 0.0f, &P::effect1 },
    { "ceiling", "finition.ceiling", "effect.parameter.ceiling", "dB", -3.0f, 0.0f, -1.0f, &P::effect2 },
    { "dynamics", "finition.dynamics", "effect.parameter.dynamicsAmount", "", 0.0f, 1.0f, 0.3f, &P::effect3 },
    { "excite", "finition.excite", "effect.parameter.excite", "", 0.0f, 1.0f, 0.15f, &P::effect4 },
    { "width", "finition.width", "effect.parameter.width", "", 0.0f, 2.0f, 1.0f, &P::effect5 },
    { "low", "finition.low", "effect.parameter.low", "dB", -6.0f, 6.0f, 0.0f, &P::effect6 },
    { "mid", "finition.mid", "effect.parameter.mid", "dB", -6.0f, 6.0f, 0.0f, &P::effect7 },
    { "high", "finition.high", "effect.parameter.high", "dB", -6.0f, 6.0f, 0.0f, &P::effect8 },
    CYDAW_SLOT("eq-on", "finition.eqOn", "effect.parameter.eqOn", "", 0.0f, 1.0f, 1.0f, 0),
    CYDAW_SLOT("dynamics-on", "finition.dynamicsOn", "effect.parameter.dynamicsOn", "", 0.0f, 1.0f, 1.0f, 1),
    CYDAW_SLOT("exciter-on", "finition.exciterOn", "effect.parameter.exciterOn", "", 0.0f, 1.0f, 1.0f, 2),
    CYDAW_SLOT("imager-on", "finition.imagerOn", "effect.parameter.imagerOn", "", 0.0f, 1.0f, 1.0f, 3),
    CYDAW_SLOT("maximizer-on", "finition.maximizerOn", "effect.parameter.maximizerOn", "", 0.0f, 1.0f, 1.0f, 4),
    CYDAW_SLOT("cross-low", "finition.crossLow", "effect.parameter.crossLow", "Hz", 60.0f, 500.0f, 150.0f, 5),
    CYDAW_SLOT("cross-high", "finition.crossHigh", "effect.parameter.crossHigh", "Hz", 1'000.0f, 10'000.0f, 4'000.0f, 6),
    CYDAW_SLOT("low-cut", "finition.lowCut", "effect.parameter.lowCut", "Hz", 10.0f, 100.0f, 10.0f, 7),
    CYDAW_SLOT("low-hz", "finition.lowHz", "effect.parameter.lowShelfHz", "Hz", 40.0f, 400.0f, 100.0f, 8),
    CYDAW_SLOT("mid-hz", "finition.midHz", "effect.parameter.midFrequency", "Hz", 200.0f, 8'000.0f, 1'500.0f, 9),
    CYDAW_SLOT("mid-q", "finition.midQ", "effect.parameter.q", "", 0.3f, 4.0f, 0.7f, 10),
    CYDAW_SLOT("high-hz", "finition.highHz", "effect.parameter.highShelfHz", "Hz", 2'000.0f, 16'000.0f, 10'000.0f, 11),
    CYDAW_SLOT("threshold", "finition.threshold", "effect.parameter.threshold", "dB", -36.0f, 0.0f, -20.0f, 12),
    CYDAW_SLOT("ratio", "finition.ratio", "effect.parameter.ratio", "", 1.0f, 6.0f, 2.5f, 13),
    CYDAW_SLOT("attack", "finition.attack", "effect.parameter.attack", "ms", 1.0f, 200.0f, 30.0f, 14),
    CYDAW_SLOT("release", "finition.release", "effect.parameter.release", "ms", 20.0f, 1'000.0f, 200.0f, 15),
    CYDAW_SLOT("low-drive", "finition.lowDrive", "effect.parameter.lowDrive", "", 0.0f, 1.0f, 0.3f, 16),
    CYDAW_SLOT("mid-drive", "finition.midDrive", "effect.parameter.midDrive", "", 0.0f, 1.0f, 0.5f, 17),
    CYDAW_SLOT("high-drive", "finition.highDrive", "effect.parameter.highDrive", "", 0.0f, 1.0f, 0.8f, 18),
    CYDAW_SLOT("low-width", "finition.lowWidth", "effect.parameter.lowWidth", "", 0.0f, 2.0f, 0.8f, 19),
    CYDAW_SLOT("mid-width", "finition.midWidth", "effect.parameter.midWidth", "", 0.0f, 2.0f, 1.0f, 20),
    CYDAW_SLOT("high-width", "finition.highWidth", "effect.parameter.highWidth", "", 0.0f, 2.0f, 1.2f, 21),
    CYDAW_SLOT("maximizer-release", "finition.maximizerRelease", "effect.parameter.maxRelease", "ms", 10.0f, 500.0f, 80.0f, 22),
    CYDAW_SLOT("target", "finition.target", "effect.parameter.loudnessTarget", "", 0.0f, 6.0f, 0.0f, 23),
};

// Halo: a reverb of characters (HaloDesign.h). The mix, decay, predelay,
// size, cuts and modulation have sockets; the mode, colour, attack,
// diffusions, bass decay, damping, sync and width are slots.
inline constexpr EffectParameter halo[] {
    { "mix", "halo.mix", "effect.parameter.mix", "", 0.0f, 1.0f, 0.3f, &P::effect1 },
    { "decay", "halo.decay", "effect.parameter.decay", "s", 0.2f, 20.0f, 2.5f, &P::effect2 },
    { "predelay", "halo.predelay", "effect.parameter.predelay", "ms", 0.0f, 250.0f, 15.0f, &P::effect3 },
    { "size", "halo.size", "effect.parameter.size", "", 0.0f, 1.0f, 0.6f, &P::effect4 },
    { "high-cut", "halo.highCut", "effect.parameter.highCut", "Hz", 1'000.0f, 20'000.0f, 12'000.0f, &P::effect5 },
    { "low-cut", "halo.lowCut", "effect.parameter.lowCut", "Hz", 10.0f, 1'000.0f, 80.0f, &P::effect6 },
    { "mod-depth", "halo.modDepth", "effect.parameter.modDepth", "", 0.0f, 1.0f, 0.4f, &P::effect7 },
    { "mod-rate", "halo.modRate", "effect.parameter.modRate", "Hz", 0.05f, 5.0f, 0.6f, &P::effect8 },
    CYDAW_SLOT("mode", "halo.mode", "effect.parameter.reverbMode", "", 0.0f, 9.0f, 0.0f, 0),
    CYDAW_SLOT("colour", "halo.colour", "effect.parameter.colourEra", "", 0.0f, 2.0f, 2.0f, 1),
    CYDAW_SLOT("attack", "halo.attack", "effect.parameter.attack", "", 0.0f, 1.0f, 0.5f, 2),
    CYDAW_SLOT("early-diffusion", "halo.earlyDiffusion", "effect.parameter.earlyDiffusion", "", 0.0f, 1.0f, 0.7f, 3),
    CYDAW_SLOT("late-diffusion", "halo.lateDiffusion", "effect.parameter.lateDiffusion", "", 0.0f, 1.0f, 0.7f, 4),
    CYDAW_SLOT("bass-multiplier", "halo.bassMultiplier", "effect.parameter.bassMultiplier", "", 0.25f, 4.0f, 1.2f, 5),
    CYDAW_SLOT("bass-crossover", "halo.bassCrossover", "effect.parameter.bassCrossover", "Hz", 50.0f, 1'000.0f, 300.0f, 6),
    CYDAW_SLOT("damping", "halo.damping", "effect.parameter.damping", "Hz", 1'000.0f, 20'000.0f, 7'000.0f, 7),
    CYDAW_SLOT("sync", "halo.sync", "effect.parameter.sync", "", 0.0f, 12.0f, 0.0f, 8),
    CYDAW_SLOT("width", "halo.width", "effect.parameter.width", "", 0.0f, 1.0f, 1.0f, 9),
};

// The spectrometer passes its sound untouched: its values are the display's
// (the floor of its scale, the tilt per octave, how smooth the curve, how
// long the peaks stay, how many seconds the waterfall shows).
inline constexpr EffectParameter spectrometer[] {
    { "floor", "spectrometer.floor", "effect.parameter.floor", "dB", -120.0f, -36.0f, -90.0f, &P::effect1 },
    { "tilt", "spectrometer.tilt", "effect.parameter.tilt", "dB", 0.0f, 6.0f, 3.0f, &P::effect2 },
    { "smoothing", "spectrometer.smoothing", "effect.parameter.smoothing", "", 0.0f, 1.0f, 0.5f, &P::effect3 },
    { "hold", "spectrometer.hold", "effect.parameter.hold", "s", 0.0f, 10.0f, 2.0f, &P::effect4 },
    { "history", "spectrometer.history", "effect.parameter.history", "s", 2.0f, 30.0f, 8.0f, &P::effect5 },
};

inline constexpr EffectParameter eqPro[] {
    CYDAW_EQ_BAND(1, 40.0f),    CYDAW_EQ_BAND(2, 120.0f),   CYDAW_EQ_BAND(3, 350.0f),    CYDAW_EQ_BAND(4, 900.0f),
    CYDAW_EQ_BAND(5, 2'200.0f), CYDAW_EQ_BAND(6, 5'000.0f), CYDAW_EQ_BAND(7, 10'000.0f), CYDAW_EQ_BAND(8, 16'000.0f),
    { "output", "eqpro.output", "effect.parameter.output", "dB", -24.0f, 24.0f, 0.0f, &P::effect1 },
};
#undef CYDAW_EQ_BAND
#undef CYDAW_SLOT
} // namespace effect_detail

inline constexpr std::array<EffectDescriptor, 33> effectCatalogue { {
    { "chorus", "core.chorus", effect_detail::K::chorus, "node.chorus", "node.chorus.description", "Chorus",
      effect_detail::effects, EffectGroup::modulation, 0xff5fd0c4u, effect_detail::chorus },
    { "flanger", "core.flanger", effect_detail::K::flanger, "node.flanger", "node.flanger.description", "Flanger",
      effect_detail::effects, EffectGroup::modulation, 0xff6fb8f0u, effect_detail::flanger },
    { "phaser", "core.phaser", effect_detail::K::phaser, "node.phaser", "node.phaser.description", "Phaser",
      effect_detail::effects, EffectGroup::modulation, 0xff9b8cf2u, effect_detail::phaser },
    { "tremolo-pan", "core.tremolo", effect_detail::K::tremolo, "node.tremoloPan", "node.tremoloPan.description", "Tremolo",
      effect_detail::effects, EffectGroup::modulation, 0xff65d6b1u, effect_detail::tremolo },
    { "bitcrusher", "core.bitcrusher", effect_detail::K::bitcrusher, "node.bitcrusher", "node.bitcrusher.description",
      "Bitcrusher", effect_detail::effects, EffectGroup::dynamics, 0xffff7f6bu, effect_detail::bitcrusher },
    { "multimode-filter", "core.multimode-filter", effect_detail::K::multiFilter, "node.multiFilter",
      "node.multiFilter.description", "Multimode Filter", effect_detail::filters, EffectGroup::filter, 0xff70b7ffu,
      effect_detail::multimodeFilter },
    { "auto-filter", "core.auto-filter", effect_detail::K::autoFilter, "node.autoFilter", "node.autoFilter.description",
      "Auto Filter", effect_detail::filters, EffectGroup::filter, 0xff5fc9d8u, effect_detail::autoFilter },
    { "eq-three", "core.eq-three", effect_detail::K::threeBandEq, "node.eqThree", "node.eqThree.description", "EQ Three",
      effect_detail::filters, EffectGroup::filter, 0xff5fc9d8u, effect_detail::eqThree },
    { "eq-pro", "core.eq-pro", effect_detail::K::parametricEq, "node.eqPro", "node.eqPro.description", "Parametric EQ",
      effect_detail::filters, EffectGroup::filter, 0xff4fd6e8u, effect_detail::eqPro },
    { "comp-pro", "core.comp-pro", effect_detail::K::compressorPro, "node.compPro", "node.compPro.description",
      "Pro Compressor", effect_detail::dynamics, EffectGroup::dynamics, 0xffff9a3cu, effect_detail::compressorPro, 2, 2,
      true },
    { "limiter", "core.limiter", effect_detail::K::limiter, "node.limiter", "node.limiter.description", "Limiter",
      effect_detail::dynamics, EffectGroup::dynamics, 0xffffcf5cu, effect_detail::limiter },
    { "gate", "core.gate", effect_detail::K::gate, "node.gate", "node.gate.description", "Gate", effect_detail::dynamics,
      EffectGroup::dynamics, 0xffe8b45cu, effect_detail::gate, 2, 2, true },
    { "transient-shaper", "core.transient-shaper", effect_detail::K::transientShaper, "node.transientShaper",
      "node.transientShaper.description", "Transient Shaper", effect_detail::dynamics, EffectGroup::dynamics, 0xfff0a43au,
      effect_detail::transientShaper },
    { "ping-pong", "core.ping-pong", effect_detail::K::pingPongDelay, "node.pingPong", "node.pingPong.description",
      "Ping-Pong Delay", effect_detail::effects, EffectGroup::delay, 0xffffa547u, effect_detail::pingPong },
    { "mono-to-stereo", "core.mono-to-stereo", effect_detail::K::monoToStereo, "node.monoToStereo",
      "node.monoToStereo.description", "Mono to Stereo", effect_detail::utilities, EffectGroup::channels, 0xff8fb6d8u,
      effect_detail::monoToStereo, 1, 2 },
    { "stereo-to-mono", "core.stereo-to-mono", effect_detail::K::stereoToMono, "node.stereoToMono",
      "node.stereoToMono.description", "Stereo to Mono", effect_detail::utilities, EffectGroup::channels, 0xff8fb6d8u,
      effect_detail::stereoToMono, 2, 1 },
    { "spectrometer", "core.spectrometer", effect_detail::K::spectrometer, "node.spectrometer",
      "node.spectrometer.description", "Spectrometer", effect_detail::analyzers, EffectGroup::analysis, 0xff6cb8ffu,
      effect_detail::spectrometer },
    { "reverb-pro", "core.reverb-pro", effect_detail::K::reverbPro, "node.reverbPro", "node.reverbPro.description", "Pro Reverb",
      effect_detail::effects, EffectGroup::delay, 0xff8f9cffu, effect_detail::reverbPro },
    { "limiter-pro", "core.limiter-pro", effect_detail::K::limiterPro, "node.limiterPro", "node.limiterPro.description",
      "Pro Limiter", effect_detail::dynamics, EffectGroup::dynamics, 0xffffd166u, effect_detail::limiterPro },
    { "filter-pro", "core.filter-pro", effect_detail::K::filterPro, "node.filterPro", "node.filterPro.description",
      "Pro Filter", effect_detail::filters, EffectGroup::filter, 0xff47d7a8u, effect_detail::filterPro },
    { "amp-sim", "core.amp-sim", effect_detail::K::ampSim, "node.ampSim", "node.ampSim.description", "Amp Simulator",
      effect_detail::effects, EffectGroup::modulation, 0xffe0a458u, effect_detail::ampSim },
    { "cab-sim", "core.cab-sim", effect_detail::K::cabSim, "node.cabSim", "node.cabSim.description", "Cabinet and Microphone",
      effect_detail::effects, EffectGroup::filter, 0xffc9915au, effect_detail::cabSim },
    { "mic-sim", "core.mic-sim", effect_detail::K::micSim, "node.micSim", "node.micSim.description", "Microphone Simulator",
      effect_detail::effects, EffectGroup::filter, 0xffb7a2e0u, effect_detail::micSim },
    { "pression", "core.pression", effect_detail::K::pression, "node.pression", "node.pression.description", "Pression Compressor",
      effect_detail::dynamics, EffectGroup::dynamics, 0xffff6f91u, effect_detail::pression },
    { "fournaise", "core.fournaise", effect_detail::K::fournaise, "node.fournaise", "node.fournaise.description",
      "Fournaise Saturation", effect_detail::effects, EffectGroup::dynamics, 0xffff7a3cu, effect_detail::fournaise },
    { "echo-bande", "core.echo-bande", effect_detail::K::echoBande, "node.echoBande", "node.echoBande.description", "Tape Echo",
      effect_detail::effects, EffectGroup::delay, 0xffd9b26au, effect_detail::echoBande },
    { "multiband-pro", "core.multiband-pro", effect_detail::K::multibandPro, "node.multibandPro", "node.multibandPro.description",
      "Multiband Pro", effect_detail::dynamics, EffectGroup::dynamics, 0xff7ad7ffu, effect_detail::multibandPro },
    { "velours", "core.velours", effect_detail::K::velours, "node.velours", "node.velours.description", "Velours Resonance Suppressor",
      effect_detail::dynamics, EffectGroup::dynamics, 0xffc69cffu, effect_detail::velours },
    { "patine", "core.patine", effect_detail::K::patine, "node.patine", "node.patine.description", "Patine Vintage Colour",
      effect_detail::effects, EffectGroup::modulation, 0xffe8b07au, effect_detail::patine },
    { "sonometre", "core.sonometre", effect_detail::K::sonometre, "node.sonometre", "node.sonometre.description",
      "Sonometre Loudness Meter", effect_detail::analyzers, EffectGroup::analysis, 0xff63e6a5u, effect_detail::sonometre },
    { "tranche", "core.tranche", effect_detail::K::tranche, "node.tranche", "node.tranche.description", "Tranche Channel Strip",
      effect_detail::effects, EffectGroup::dynamics, 0xff5ec8ffu, effect_detail::tranche },
    { "finition", "core.finition", effect_detail::K::finition, "node.finition", "node.finition.description", "Finition Mastering",
      effect_detail::effects, EffectGroup::dynamics, 0xffffc46bu, effect_detail::finition },
    { "halo", "core.halo", effect_detail::K::halo, "node.halo", "node.halo.description", "Halo Reverb", effect_detail::effects,
      EffectGroup::delay, 0xffb08cffu, effect_detail::halo },
} };

[[nodiscard]] constexpr const EffectDescriptor* effectForKind(const NativeDspNodeKind kind) noexcept
{
    for (const auto& effect : effectCatalogue)
        if (effect.kind == kind)
            return &effect;
    return nullptr;
}

[[nodiscard]] constexpr const EffectDescriptor* effectForProcessor(const std::string_view processorKey) noexcept
{
    for (const auto& effect : effectCatalogue)
        if (effect.processorKey == processorKey)
            return &effect;
    return nullptr;
}

[[nodiscard]] constexpr const EffectDescriptor* effectForType(const std::string_view typeKey) noexcept
{
    for (const auto& effect : effectCatalogue)
        if (effect.typeKey == typeKey)
            return &effect;
    return nullptr;
}

[[nodiscard]] constexpr const EffectParameter* effectParameter(const EffectDescriptor& effect,
                                                                const std::string_view key) noexcept
{
    for (const auto& parameter : effect.parameters)
        if (!parameter.key.empty() && parameter.key == key)
            return &parameter;
    return nullptr;
}

// A binding suffix ("chorus.rate") belongs to one effect only.
[[nodiscard]] constexpr const EffectParameter* effectParameterForSuffix(const std::string_view suffix,
                                                                         const EffectDescriptor** owner = nullptr) noexcept
{
    for (const auto& effect : effectCatalogue)
        for (const auto& parameter : effect.parameters)
            if (!parameter.suffix.empty() && parameter.suffix == suffix) {
                if (owner != nullptr)
                    *owner = &effect;
                return &parameter;
            }
    return nullptr;
}

// The payload with an effect's defaults written into its fields.
inline void applyEffectDefaults(const EffectDescriptor& effect, NativeDspNodeParameters& parameters) noexcept
{
    for (const auto& parameter : effect.parameters)
        setEffectValue(parameters, parameter, parameter.defaultValue);
}

} // namespace cydaw::engine
