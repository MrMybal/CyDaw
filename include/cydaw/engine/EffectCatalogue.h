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
    // ...or a slot of its eqBands array. Extended typed sockets address
    // these slots explicitly, resolved before the audio callback.
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
enum class EffectGroup : std::uint8_t { modulation, delay, filter, dynamics, channels, analysis, midi, control, container };

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
    // A MIDI effect (MidiEffects.h): notes in, notes out, before the
    // instrument; no sound of its own.
    bool midi {};
    // A modulator (Modulators.h): no sound in or out, a control value that
    // moves other cards' knobs.
    bool control {};
    // A container's head (Containers.h): its chains' devices between it and
    // its end.
    bool container {};

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
// After the output and the band heard alone, each band's dynamics: whether
// its gain moves with the sound, how far it may go from its gain (dB), from
// what level, how fast, and what its detector hears (0 its own band of the
// sound, 1 the same band of the sidechain). Kept in the slots after the
// bands' values, in that order.
inline constexpr int eqProDynamicValuesPerBand = 6;
enum class EqProDynamic : int { on, range, threshold, attack, release, detector };
[[nodiscard]] constexpr int eqProDynamicSlot(const int band, const EqProDynamic value) noexcept
{
    return eqProBands * eqProValuesPerBand + band * eqProDynamicValuesPerBand + static_cast<int>(value);
}
// Its place among the entry's parameters: after the output and the listen.
[[nodiscard]] constexpr int eqProDynamicIndex(const int band, const EqProDynamic value) noexcept
{
    return eqProDynamicSlot(band, value) + 2;
}
static_assert(eqProDynamicSlot(eqProBands - 1, EqProDynamic::detector) < static_cast<int>(std::tuple_size_v<decltype(NativeDspNodeParameters::eqBands)>));
// What the equaliser's processor shows of its work (AudioProcessor::
// readings): each band's gain moved by its dynamics now (dB, 0 for a
// static band), then the level each band's detector hears (dB, -120 for a
// static band).
inline constexpr int eqProReadingCount = eqProBands * 2;

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
// A band's dynamics, in the slots after the eight bands' values.
#define CYDAW_EQ_DYNAMIC(n)                                                                                                     \
    { "b" #n "-dynamic", "eqpro.b" #n "Dynamic", "effect.parameter.dynamicsOn", "", 0.0f, 1.0f, 0.0f, nullptr,             \
      static_cast<std::int16_t>(48 + ((n) - 1) * 6 + 0), static_cast<std::int8_t>((n) - 1) },                              \
    { "b" #n "-range", "eqpro.b" #n "Range", "effect.parameter.dynamicRange", "dB", -30.0f, 30.0f, -6.0f, nullptr,           \
      static_cast<std::int16_t>(48 + ((n) - 1) * 6 + 1), static_cast<std::int8_t>((n) - 1) },                              \
    { "b" #n "-threshold", "eqpro.b" #n "Threshold", "effect.parameter.threshold", "dB", -60.0f, 0.0f, -24.0f, nullptr,     \
      static_cast<std::int16_t>(48 + ((n) - 1) * 6 + 2), static_cast<std::int8_t>((n) - 1) },                              \
    { "b" #n "-attack", "eqpro.b" #n "Attack", "effect.parameter.attack", "ms", 0.5f, 200.0f, 10.0f, nullptr,               \
      static_cast<std::int16_t>(48 + ((n) - 1) * 6 + 3), static_cast<std::int8_t>((n) - 1) },                              \
    { "b" #n "-release", "eqpro.b" #n "Release", "effect.parameter.release", "ms", 10.0f, 2'000.0f, 120.0f, nullptr,        \
      static_cast<std::int16_t>(48 + ((n) - 1) * 6 + 4), static_cast<std::int8_t>((n) - 1) },                              \
    { "b" #n "-detector", "eqpro.b" #n "Detector", "effect.parameter.detector", "", 0.0f, 1.0f, 0.0f, nullptr,              \
      static_cast<std::int16_t>(48 + ((n) - 1) * 6 + 5), static_cast<std::int8_t>((n) - 1) }

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
    // The band heard alone while a band's listen is held: 0 none, n band n.
    { "listen", "eqpro.listen", "effect.parameter.soloBand", "", 0.0f, 8.0f, 0.0f, &P::effect2 },
    CYDAW_EQ_DYNAMIC(1), CYDAW_EQ_DYNAMIC(2), CYDAW_EQ_DYNAMIC(3), CYDAW_EQ_DYNAMIC(4),
    CYDAW_EQ_DYNAMIC(5), CYDAW_EQ_DYNAMIC(6), CYDAW_EQ_DYNAMIC(7), CYDAW_EQ_DYNAMIC(8),
};
// ---- The effects added on 2026-10-14 (their design headers say what each
// value does; the enums there follow these arrays' order) ----

inline constexpr EffectParameter vocoderParameters[] {
    { "mix", "vocoder.mix", "effect.parameter.mix", "", 0.0f, 1.0f, 1.0f, &P::effect1 },
    { "formant", "vocoder.formant", "effect.parameter.formant", "", -12.0f, 12.0f, 0.0f, &P::effect2 },
    { "attack", "vocoder.attack", "effect.parameter.attack", "ms", 0.5f, 100.0f, 4.0f, &P::effect3 },
    { "release", "vocoder.release", "effect.parameter.release", "ms", 5.0f, 1'000.0f, 60.0f, &P::effect4 },
    { "pitch", "vocoder.pitch", "effect.parameter.carrierPitch", "", 24.0f, 96.0f, 48.0f, &P::effect5 },
    { "unvoiced", "vocoder.unvoiced", "effect.parameter.unvoiced", "", 0.0f, 1.0f, 0.3f, &P::effect6 },
    { "tone", "vocoder.tone", "effect.parameter.tone", "", -1.0f, 1.0f, 0.0f, &P::effect7 },
    { "output", "vocoder.output", "effect.parameter.output", "dB", -24.0f, 24.0f, 0.0f, &P::effect8 },
    { "carrier-source", "vocoder.carrier", "effect.parameter.carrier", "", 0.0f, 3.0f, 2.0f, nullptr, 0, -1 },
    { "bands", "vocoder.bands", "effect.parameter.bands", "", 4.0f, 40.0f, 20.0f, nullptr, 1, -1 },
    { "low-band", "vocoder.lowBand", "effect.parameter.lowBand", "Hz", 50.0f, 1'000.0f, 100.0f, nullptr, 2, -1 },
    { "high-band", "vocoder.highBand", "effect.parameter.highBand", "Hz", 2'000.0f, 18'000.0f, 10'000.0f, nullptr, 3, -1 },
    { "bandwidth", "vocoder.bandwidth", "effect.parameter.bandwidth", "", 0.25f, 2.0f, 1.0f, nullptr, 4, -1 },
    { "band-gate", "vocoder.gate", "effect.parameter.bandGate", "dB", -90.0f, -20.0f, -70.0f, nullptr, 5, -1 },
    { "stereo", "vocoder.stereo", "effect.parameter.stereo", "", 0.0f, 1.0f, 0.5f, nullptr, 6, -1 },
    { "detune", "vocoder.detune", "effect.parameter.detune", "", 0.0f, 1.0f, 0.2f, nullptr, 7, -1 },
};
inline constexpr EffectParameter deEsserParameters[] {
    { "threshold", "deesser.threshold", "effect.parameter.threshold", "dB", -60.0f, 0.0f, -28.0f, &P::effect1 },
    { "frequency", "deesser.frequency", "effect.parameter.frequency", "Hz", 2'000.0f, 12'000.0f, 6'000.0f, &P::effect2 },
    { "range", "deesser.range", "effect.parameter.range", "dB", 0.0f, 24.0f, 10.0f, &P::effect3 },
    { "release", "deesser.release", "effect.parameter.release", "ms", 10.0f, 300.0f, 60.0f, &P::effect4 },
    { "output", "deesser.output", "effect.parameter.output", "dB", -24.0f, 24.0f, 0.0f, &P::effect5 },
    { "mode", "deesser.mode", "effect.parameter.mode", "", 0.0f, 1.0f, 0.0f, nullptr, 0, -1 },
    { "detector-type", "deesser.detectorType", "effect.parameter.detectorType", "", 0.0f, 1.0f, 0.0f, nullptr, 1, -1 },
    { "listen", "deesser.listen", "effect.parameter.audition", "", 0.0f, 1.0f, 0.0f, nullptr, 2, -1 },
    { "bandwidth", "deesser.bandwidth", "effect.parameter.bandwidth", "", 0.3f, 3.0f, 1.0f, nullptr, 3, -1 },
    { "attack", "deesser.attack", "effect.parameter.attack", "ms", 0.1f, 20.0f, 1.0f, nullptr, 4, -1 },
    { "link", "deesser.link", "effect.parameter.link", "", 0.0f, 1.0f, 1.0f, nullptr, 5, -1 },
};
inline constexpr EffectParameter tunerParameters[] {
    { "reference", "tuner.reference", "effect.parameter.reference", "Hz", 415.0f, 466.0f, 440.0f, &P::effect1 },
    { "mute", "tuner.mute", "effect.parameter.mute", "", 0.0f, 1.0f, 0.0f, nullptr, 0, -1 },
    { "display-mode", "tuner.displayMode", "effect.parameter.displayMode", "", 0.0f, 1.0f, 0.0f, nullptr, 1, -1 },
};
inline constexpr EffectParameter harmonieParameters[] {
    { "shift-a", "harmonie.shiftA", "effect.parameter.shiftA", "", -24.0f, 24.0f, 12.0f, &P::effect1 },
    { "fine-a", "harmonie.fineA", "effect.parameter.fineA", "", -100.0f, 100.0f, 0.0f, &P::effect2 },
    { "level-a", "harmonie.levelA", "effect.parameter.levelA", "", 0.0f, 1.0f, 1.0f, &P::effect3 },
    { "shift-b", "harmonie.shiftB", "effect.parameter.shiftB", "", -24.0f, 24.0f, 7.0f, &P::effect4 },
    { "level-b", "harmonie.levelB", "effect.parameter.levelB", "", 0.0f, 1.0f, 0.0f, &P::effect5 },
    { "dry", "harmonie.dry", "effect.parameter.dry", "", 0.0f, 1.0f, 0.0f, &P::effect6 },
    { "delay", "harmonie.delay", "effect.parameter.delay", "ms", 0.0f, 500.0f, 0.0f, &P::effect7 },
    { "output", "harmonie.output", "effect.parameter.output", "dB", -24.0f, 24.0f, 0.0f, &P::effect8 },
    { "window", "harmonie.window", "effect.parameter.window", "ms", 10.0f, 120.0f, 40.0f, nullptr, 0, -1 },
    { "mode", "harmonie.mode", "effect.parameter.mode", "", 0.0f, 2.0f, 1.0f, nullptr, 1, -1 },
    { "fine-b", "harmonie.fineB", "effect.parameter.fineB", "", -100.0f, 100.0f, 0.0f, nullptr, 2, -1 },
    { "pan-a", "harmonie.panA", "effect.parameter.panA", "", -1.0f, 1.0f, 0.0f, nullptr, 3, -1 },
    { "pan-b", "harmonie.panB", "effect.parameter.panB", "", -1.0f, 1.0f, 0.0f, nullptr, 4, -1 },
    { "feedback", "harmonie.feedback", "effect.parameter.feedback", "", 0.0f, 0.9f, 0.0f, nullptr, 5, -1 },
};
inline constexpr EffectParameter justesseParameters[] {
    { "speed", "justesse.speed", "effect.parameter.retuneSpeed", "ms", 0.0f, 400.0f, 20.0f, &P::effect1 },
    { "amount", "justesse.amount", "effect.parameter.amount", "", 0.0f, 1.0f, 1.0f, &P::effect2 },
    { "humanize", "justesse.humanize", "effect.parameter.humanize", "", 0.0f, 1.0f, 0.0f, &P::effect3 },
    { "transpose", "justesse.transpose", "effect.parameter.transpose", "", -12.0f, 12.0f, 0.0f, &P::effect4 },
    { "mix", "justesse.mix", "effect.parameter.mix", "", 0.0f, 1.0f, 1.0f, &P::effect5 },
    { "output", "justesse.output", "effect.parameter.output", "dB", -24.0f, 24.0f, 0.0f, &P::effect6 },
    { "key", "justesse.key", "effect.parameter.key", "", 0.0f, 11.0f, 0.0f, nullptr, 0, -1 },
    { "scale", "justesse.scale", "effect.parameter.scale", "", 0.0f, 11.0f, 0.0f, nullptr, 1, -1 },
    { "low-pitch", "justesse.lowPitch", "effect.parameter.lowPitch", "Hz", 50.0f, 400.0f, 70.0f, nullptr, 2, -1 },
    { "high-pitch", "justesse.highPitch", "effect.parameter.highPitch", "Hz", 300.0f, 1'600.0f, 1'000.0f, nullptr, 3, -1 },
    { "sensitivity", "justesse.sensitivity", "effect.parameter.sensitivity", "", 0.0f, 1.0f, 0.5f, nullptr, 4, -1 },
};
inline constexpr EffectParameter empreinteParameters[] {
    { "mix", "empreinte.mix", "effect.parameter.mix", "", 0.0f, 1.0f, 0.3f, &P::effect1 },
    { "predelay", "empreinte.predelay", "effect.parameter.predelay", "ms", 0.0f, 250.0f, 0.0f, &P::effect2 },
    { "low-cut", "empreinte.lowCut", "effect.parameter.lowCut", "Hz", 10.0f, 1'000.0f, 10.0f, &P::effect3 },
    { "high-cut", "empreinte.highCut", "effect.parameter.highCut", "Hz", 1'000.0f, 20'000.0f, 20'000.0f, &P::effect4 },
    { "width", "empreinte.width", "effect.parameter.width", "", 0.0f, 1.0f, 1.0f, &P::effect5 },
    { "early", "empreinte.early", "effect.parameter.earlyLevel", "dB", -24.0f, 6.0f, 0.0f, &P::effect6 },
    { "tail", "empreinte.tail", "effect.parameter.tailLevel", "dB", -24.0f, 6.0f, 0.0f, &P::effect7 },
    { "output", "empreinte.output", "effect.parameter.output", "dB", -24.0f, 24.0f, 0.0f, &P::effect8 },
    { "space-type", "empreinte.space", "effect.parameter.builtinSpace", "", 0.0f, 9.0f, 2.0f, nullptr, 0, -1 },
    { "ir-length", "empreinte.length", "effect.parameter.irLength", "", 0.1f, 1.0f, 1.0f, nullptr, 1, -1 },
    { "reverse", "empreinte.reverse", "effect.parameter.reverse", "", 0.0f, 1.0f, 0.0f, nullptr, 2, -1 },
};
inline constexpr EffectParameter givreParameters[] {
    { "blur", "givre.blur", "effect.parameter.blur", "", 0.0f, 1.0f, 0.4f, &P::effect1 },
    { "smear", "givre.smear", "effect.parameter.spectralSmear", "", 0.0f, 1.0f, 0.0f, &P::effect2 },
    { "shift", "givre.shift", "effect.parameter.spectralShift", "", -12.0f, 12.0f, 0.0f, &P::effect3 },
    { "tone", "givre.tone", "effect.parameter.tone", "", -1.0f, 1.0f, 0.0f, &P::effect4 },
    { "scatter", "givre.scatter", "effect.parameter.scatter", "", 0.0f, 1.0f, 0.2f, &P::effect5 },
    { "mix", "givre.mix", "effect.parameter.mix", "", 0.0f, 1.0f, 0.5f, &P::effect6 },
    { "output", "givre.output", "effect.parameter.output", "dB", -24.0f, 24.0f, 0.0f, &P::effect7 },
    { "freeze", "givre.freeze", "effect.parameter.freeze", "", 0.0f, 1.0f, 0.0f, nullptr, 0, -1 },
    { "sync", "givre.sync", "effect.parameter.sync", "", 0.0f, 12.0f, 0.0f, nullptr, 1, -1 },
};
inline constexpr EffectParameter grainsParameters[] {
    { "time", "grains.time", "effect.parameter.time", "ms", 1.0f, 2'000.0f, 250.0f, &P::effect1 },
    { "feedback", "grains.feedback", "effect.parameter.feedback", "", 0.0f, 0.95f, 0.35f, &P::effect2 },
    { "pitch", "grains.pitch", "effect.parameter.pitch", "", -24.0f, 24.0f, 0.0f, &P::effect3 },
    { "spray", "grains.spray", "effect.parameter.spray", "ms", 0.0f, 500.0f, 20.0f, &P::effect4 },
    { "density", "grains.density", "effect.parameter.density", "Hz", 1.0f, 100.0f, 25.0f, &P::effect5 },
    { "pitch-random", "grains.pitchRandom", "effect.parameter.pitchRandom", "", 0.0f, 12.0f, 0.0f, &P::effect6 },
    { "mix", "grains.mix", "effect.parameter.mix", "", 0.0f, 1.0f, 0.4f, &P::effect7 },
    { "output", "grains.output", "effect.parameter.output", "dB", -24.0f, 24.0f, 0.0f, &P::effect8 },
    { "sync", "grains.sync", "effect.parameter.sync", "", 0.0f, 12.0f, 0.0f, nullptr, 0, -1 },
    { "grain-size", "grains.size", "effect.parameter.grainSize", "ms", 10.0f, 500.0f, 90.0f, nullptr, 1, -1 },
    { "stereo", "grains.stereo", "effect.parameter.stereo", "", 0.0f, 1.0f, 0.6f, nullptr, 2, -1 },
    { "reverse-chance", "grains.reverseChance", "effect.parameter.reverseChance", "", 0.0f, 1.0f, 0.0f, nullptr, 3, -1 },
    { "freeze", "grains.freeze", "effect.parameter.freeze", "", 0.0f, 1.0f, 0.0f, nullptr, 4, -1 },
    { "high-cut", "grains.highCut", "effect.parameter.highCut", "Hz", 1'000.0f, 20'000.0f, 14'000.0f, nullptr, 5, -1 },
};
inline constexpr EffectParameter saccadeParameters[] {
    { "chance", "saccade.chance", "effect.parameter.chance", "", 0.0f, 1.0f, 1.0f, &P::effect1 },
    { "variation", "saccade.variation", "effect.parameter.variation", "", 0.0f, 1.0f, 0.0f, &P::effect2 },
    { "decay", "saccade.decay", "effect.parameter.decay", "", 0.0f, 1.0f, 0.0f, &P::effect3 },
    { "pitch-decay", "saccade.pitchDecay", "effect.parameter.pitchDecay", "", 0.0f, 1.0f, 0.0f, &P::effect4 },
    { "filter", "saccade.filter", "effect.parameter.cutoff", "Hz", 200.0f, 20'000.0f, 20'000.0f, &P::effect5 },
    { "mix", "saccade.mix", "effect.parameter.mix", "", 0.0f, 1.0f, 1.0f, &P::effect6 },
    { "output", "saccade.output", "effect.parameter.output", "dB", -24.0f, 24.0f, 0.0f, &P::effect7 },
    { "interval-sync", "saccade.interval", "effect.parameter.interval", "", 1.0f, 12.0f, 12.0f, nullptr, 0, -1 },
    { "offset", "saccade.offset", "effect.parameter.offset", "", 0.0f, 15.0f, 8.0f, nullptr, 1, -1 },
    { "gate-length", "saccade.gate", "effect.parameter.repeatLength", "", 1.0f, 16.0f, 4.0f, nullptr, 2, -1 },
    { "grid-sync", "saccade.grid", "effect.parameter.grid", "", 1.0f, 12.0f, 3.0f, nullptr, 3, -1 },
    { "mode", "saccade.mode", "effect.parameter.mode", "", 0.0f, 2.0f, 1.0f, nullptr, 4, -1 },
    { "reverse-chance", "saccade.reverseChance", "effect.parameter.reverseChance", "", 0.0f, 1.0f, 0.0f, nullptr, 5, -1 },
    { "repeat", "saccade.repeat", "effect.parameter.repeatNow", "", 0.0f, 1.0f, 0.0f, nullptr, 6, -1 },
};
inline constexpr EffectParameter boucleurParameters[] {
    { "feedback", "boucleur.feedback", "effect.parameter.overdub", "", 0.0f, 1.0f, 1.0f, &P::effect1 },
    { "level", "boucleur.level", "effect.parameter.loopLevel", "", 0.0f, 1.0f, 1.0f, &P::effect2 },
    { "dry", "boucleur.dry", "effect.parameter.dry", "", 0.0f, 1.0f, 1.0f, &P::effect3 },
    { "output", "boucleur.output", "effect.parameter.output", "dB", -24.0f, 24.0f, 0.0f, &P::effect4 },
    { "state", "boucleur.state", "effect.parameter.loopState", "", 0.0f, 3.0f, 0.0f, nullptr, 0, -1 },
    { "bars", "boucleur.bars", "effect.parameter.loopBars", "", 0.0f, 5.0f, 0.0f, nullptr, 1, -1 },
    { "quantize", "boucleur.quantize", "effect.parameter.quantizeStart", "", 0.0f, 1.0f, 1.0f, nullptr, 2, -1 },
    { "speed-mode", "boucleur.speed", "effect.parameter.loopSpeed", "", 0.0f, 2.0f, 1.0f, nullptr, 3, -1 },
    { "reverse", "boucleur.reverse", "effect.parameter.reverse", "", 0.0f, 1.0f, 0.0f, nullptr, 4, -1 },
    { "clear", "boucleur.clear", "effect.parameter.clearLoop", "", 0.0f, 100.0f, 0.0f, nullptr, 5, -1 },
};
inline constexpr EffectParameter pompeParameters[] {
    { "depth", "pompe.depth", "effect.parameter.depth", "", 0.0f, 1.0f, 0.8f, &P::effect1 },
    { "rate", "pompe.rate", "effect.parameter.rate", "Hz", 0.1f, 20.0f, 2.0f, &P::effect2 },
    { "curve", "pompe.curve", "effect.parameter.curve", "", -1.0f, 1.0f, 0.4f, &P::effect3 },
    { "hold", "pompe.hold", "effect.parameter.holdLow", "", 0.0f, 0.5f, 0.05f, &P::effect4 },
    { "phase", "pompe.phase", "effect.parameter.phase", "", 0.0f, 1.0f, 0.0f, &P::effect5 },
    { "stereo-offset", "pompe.stereoOffset", "effect.parameter.stereoOffset", "", 0.0f, 0.5f, 0.0f, &P::effect6 },
    { "mix", "pompe.mix", "effect.parameter.mix", "", 0.0f, 1.0f, 1.0f, &P::effect7 },
    { "output", "pompe.output", "effect.parameter.output", "dB", -24.0f, 24.0f, 0.0f, &P::effect8 },
    { "shape", "pompe.shape", "effect.parameter.shape", "", 0.0f, 7.0f, 0.0f, nullptr, 0, -1 },
    { "sync", "pompe.sync", "effect.parameter.sync", "", 0.0f, 12.0f, 9.0f, nullptr, 1, -1 },
    { "trigger-source", "pompe.trigger", "effect.parameter.trigger", "", 0.0f, 1.0f, 0.0f, nullptr, 2, -1 },
    { "threshold", "pompe.threshold", "effect.parameter.threshold", "dB", -60.0f, 0.0f, -24.0f, nullptr, 3, -1 },
    { "smoothing", "pompe.smoothing", "effect.parameter.smoothing", "ms", 0.0f, 20.0f, 1.5f, nullptr, 4, -1 },
    { "point-1", "pompe.point1", "effect.parameter.level", "", 0.0f, 1.0f, 0.0f, nullptr, 5, 0 },
    { "point-2", "pompe.point2", "effect.parameter.level", "", 0.0f, 1.0f, 0.22f, nullptr, 6, 1 },
    { "point-3", "pompe.point3", "effect.parameter.level", "", 0.0f, 1.0f, 0.42f, nullptr, 7, 2 },
    { "point-4", "pompe.point4", "effect.parameter.level", "", 0.0f, 1.0f, 0.58f, nullptr, 8, 3 },
    { "point-5", "pompe.point5", "effect.parameter.level", "", 0.0f, 1.0f, 0.7f, nullptr, 9, 4 },
    { "point-6", "pompe.point6", "effect.parameter.level", "", 0.0f, 1.0f, 0.79f, nullptr, 10, 5 },
    { "point-7", "pompe.point7", "effect.parameter.level", "", 0.0f, 1.0f, 0.86f, nullptr, 11, 6 },
    { "point-8", "pompe.point8", "effect.parameter.level", "", 0.0f, 1.0f, 0.91f, nullptr, 12, 7 },
    { "point-9", "pompe.point9", "effect.parameter.level", "", 0.0f, 1.0f, 0.94f, nullptr, 13, 8 },
    { "point-10", "pompe.point10", "effect.parameter.level", "", 0.0f, 1.0f, 0.965f, nullptr, 14, 9 },
    { "point-11", "pompe.point11", "effect.parameter.level", "", 0.0f, 1.0f, 0.98f, nullptr, 15, 10 },
    { "point-12", "pompe.point12", "effect.parameter.level", "", 0.0f, 1.0f, 0.99f, nullptr, 16, 11 },
    { "point-13", "pompe.point13", "effect.parameter.level", "", 0.0f, 1.0f, 1.0f, nullptr, 17, 12 },
    { "point-14", "pompe.point14", "effect.parameter.level", "", 0.0f, 1.0f, 1.0f, nullptr, 18, 13 },
    { "point-15", "pompe.point15", "effect.parameter.level", "", 0.0f, 1.0f, 1.0f, nullptr, 19, 14 },
    { "point-16", "pompe.point16", "effect.parameter.level", "", 0.0f, 1.0f, 1.0f, nullptr, 20, 15 },
};
inline constexpr EffectParameter decaleurParameters[] {
    { "shift", "decaleur.shift", "effect.parameter.shiftHz", "Hz", -2'000.0f, 2'000.0f, 100.0f, &P::effect1 },
    { "fine", "decaleur.fine", "effect.parameter.fine", "Hz", -50.0f, 50.0f, 0.0f, &P::effect2 },
    { "spread", "decaleur.spread", "effect.parameter.spread", "Hz", 0.0f, 200.0f, 0.0f, &P::effect3 },
    { "feedback", "decaleur.feedback", "effect.parameter.feedback", "", 0.0f, 0.95f, 0.0f, &P::effect4 },
    { "lfo-rate", "decaleur.lfoRate", "effect.parameter.lfoRate", "Hz", 0.01f, 10.0f, 0.5f, &P::effect5 },
    { "lfo-depth", "decaleur.lfoDepth", "effect.parameter.lfoDepth", "Hz", 0.0f, 1'000.0f, 0.0f, &P::effect6 },
    { "mix", "decaleur.mix", "effect.parameter.mix", "", 0.0f, 1.0f, 1.0f, &P::effect7 },
    { "output", "decaleur.output", "effect.parameter.output", "dB", -24.0f, 24.0f, 0.0f, &P::effect8 },
    { "mode", "decaleur.mode", "effect.parameter.mode", "", 0.0f, 2.0f, 0.0f, nullptr, 0, -1 },
    { "sync", "decaleur.sync", "effect.parameter.sync", "", 0.0f, 12.0f, 0.0f, nullptr, 1, -1 },
    { "lfo-shape", "decaleur.lfoShape", "effect.parameter.lfoShape", "", 0.0f, 3.0f, 0.0f, nullptr, 2, -1 },
    { "delay", "decaleur.delay", "effect.parameter.delay", "ms", 0.0f, 1'000.0f, 0.0f, nullptr, 3, -1 },
};
inline constexpr EffectParameter anneauParameters[] {
    { "frequency", "anneau.frequency", "effect.parameter.frequency", "Hz", 1.0f, 5'000.0f, 220.0f, &P::effect1 },
    { "drive", "anneau.drive", "effect.parameter.drive", "dB", 0.0f, 24.0f, 0.0f, &P::effect2 },
    { "lfo-rate", "anneau.lfoRate", "effect.parameter.lfoRate", "Hz", 0.01f, 10.0f, 0.5f, &P::effect3 },
    { "lfo-depth", "anneau.lfoDepth", "effect.parameter.lfoDepth", "", 0.0f, 2.0f, 0.0f, &P::effect4 },
    { "mix", "anneau.mix", "effect.parameter.mix", "", 0.0f, 1.0f, 0.5f, &P::effect5 },
    { "output", "anneau.output", "effect.parameter.output", "dB", -24.0f, 24.0f, 0.0f, &P::effect6 },
    { "stereo", "anneau.stereo", "effect.parameter.stereoPhase", "", 0.0f, 1.0f, 0.0f, &P::effect7 },
    { "shape", "anneau.shape", "effect.parameter.shape", "", 0.0f, 3.0f, 0.0f, nullptr, 0, -1 },
    { "mode", "anneau.mode", "effect.parameter.mode", "", 0.0f, 1.0f, 0.0f, nullptr, 1, -1 },
    { "sync", "anneau.sync", "effect.parameter.sync", "", 0.0f, 12.0f, 0.0f, nullptr, 2, -1 },
    { "low-cut", "anneau.lowCut", "effect.parameter.lowCut", "Hz", 20.0f, 2'000.0f, 20.0f, nullptr, 3, -1 },
    { "lfo-shape", "anneau.lfoShape", "effect.parameter.lfoShape", "", 0.0f, 3.0f, 0.0f, nullptr, 4, -1 },
};
inline constexpr EffectParameter rotorParameters[] {
    { "slow", "rotor.slow", "effect.parameter.slowSpeed", "Hz", 0.3f, 2.0f, 0.8f, &P::effect1 },
    { "fast", "rotor.fast", "effect.parameter.fastSpeed", "Hz", 4.0f, 9.0f, 6.8f, &P::effect2 },
    { "acceleration", "rotor.acceleration", "effect.parameter.acceleration", "s", 0.2f, 5.0f, 1.2f, &P::effect3 },
    { "balance", "rotor.balance", "effect.parameter.hornDrum", "", -1.0f, 1.0f, 0.0f, &P::effect4 },
    { "doppler", "rotor.doppler", "effect.parameter.doppler", "", 0.0f, 1.0f, 0.5f, &P::effect5 },
    { "drive", "rotor.drive", "effect.parameter.drive", "dB", 0.0f, 24.0f, 3.0f, &P::effect6 },
    { "mix", "rotor.mix", "effect.parameter.mix", "", 0.0f, 1.0f, 1.0f, &P::effect7 },
    { "output", "rotor.output", "effect.parameter.output", "dB", -24.0f, 24.0f, 0.0f, &P::effect8 },
    { "speed-mode", "rotor.speed", "effect.parameter.rotorSpeed", "", 0.0f, 2.0f, 1.0f, nullptr, 0, -1 },
    { "crossover", "rotor.crossover", "effect.parameter.crossover", "Hz", 400.0f, 1'500.0f, 800.0f, nullptr, 1, -1 },
    { "width", "rotor.width", "effect.parameter.width", "", 0.0f, 1.0f, 0.8f, nullptr, 2, -1 },
    { "room", "rotor.room", "effect.parameter.room", "", 0.0f, 1.0f, 0.25f, nullptr, 3, -1 },
};
inline constexpr EffectParameter resonateursParameters[] {
    { "note", "resonateurs.note", "effect.parameter.rootNote", "", 24.0f, 96.0f, 48.0f, &P::effect1 },
    { "decay", "resonateurs.decay", "effect.parameter.decay", "", 0.0f, 1.0f, 0.7f, &P::effect2 },
    { "colour", "resonateurs.colour", "effect.parameter.colour", "", 0.0f, 1.0f, 0.5f, &P::effect3 },
    { "width", "resonateurs.width", "effect.parameter.width", "", 0.0f, 1.0f, 0.6f, &P::effect4 },
    { "mix", "resonateurs.mix", "effect.parameter.mix", "", 0.0f, 1.0f, 0.5f, &P::effect5 },
    { "output", "resonateurs.output", "effect.parameter.output", "dB", -24.0f, 24.0f, 0.0f, &P::effect6 },
    { "tone", "resonateurs.tone", "effect.parameter.tone", "", -1.0f, 1.0f, 0.0f, &P::effect7 },
    { "mode", "resonateurs.mode", "effect.parameter.mode", "", 0.0f, 2.0f, 0.0f, nullptr, 0, -1 },
    { "interval-2", "resonateurs.interval2", "effect.parameter.interval", "", -24.0f, 24.0f, 7.0f, nullptr, 1, 1 },
    { "interval-3", "resonateurs.interval3", "effect.parameter.interval", "", -24.0f, 24.0f, 12.0f, nullptr, 2, 2 },
    { "interval-4", "resonateurs.interval4", "effect.parameter.interval", "", -24.0f, 24.0f, 16.0f, nullptr, 3, 3 },
    { "interval-5", "resonateurs.interval5", "effect.parameter.interval", "", -24.0f, 24.0f, 19.0f, nullptr, 4, 4 },
    { "gain-1", "resonateurs.gain1", "effect.parameter.gain", "", 0.0f, 1.0f, 1.0f, nullptr, 5, 0 },
    { "gain-2", "resonateurs.gain2", "effect.parameter.gain", "", 0.0f, 1.0f, 0.8f, nullptr, 6, 1 },
    { "gain-3", "resonateurs.gain3", "effect.parameter.gain", "", 0.0f, 1.0f, 0.7f, nullptr, 7, 2 },
    { "gain-4", "resonateurs.gain4", "effect.parameter.gain", "", 0.0f, 1.0f, 0.6f, nullptr, 8, 3 },
    { "gain-5", "resonateurs.gain5", "effect.parameter.gain", "", 0.0f, 1.0f, 0.5f, nullptr, 9, 4 },
};
inline constexpr EffectParameter corpsParameters[] {
    { "tune", "corps.tune", "effect.parameter.tune", "Hz", 40.0f, 2'000.0f, 220.0f, &P::effect1 },
    { "decay", "corps.decay", "effect.parameter.decay", "", 0.0f, 1.0f, 0.5f, &P::effect2 },
    { "material", "corps.material", "effect.parameter.material", "", 0.0f, 1.0f, 0.5f, &P::effect3 },
    { "inharmonic", "corps.inharmonic", "effect.parameter.inharmonicity", "", -1.0f, 1.0f, 0.0f, &P::effect4 },
    { "position", "corps.position", "effect.parameter.hitPosition", "", 0.0f, 1.0f, 0.3f, &P::effect5 },
    { "mix", "corps.mix", "effect.parameter.mix", "", 0.0f, 1.0f, 0.5f, &P::effect6 },
    { "output", "corps.output", "effect.parameter.output", "dB", -24.0f, 24.0f, 0.0f, &P::effect7 },
    { "width", "corps.width", "effect.parameter.width", "", 0.0f, 1.0f, 0.5f, &P::effect8 },
    { "body-type", "corps.type", "effect.parameter.bodyType", "", 0.0f, 6.0f, 0.0f, nullptr, 0, -1 },
    { "low-cut", "corps.lowCut", "effect.parameter.lowCut", "Hz", 20.0f, 2'000.0f, 80.0f, nullptr, 1, -1 },
};
inline constexpr EffectParameter peigneParameters[] {
    { "frequency", "peigne.frequency", "effect.parameter.frequency", "Hz", 20.0f, 5'000.0f, 200.0f, &P::effect1 },
    { "feedback", "peigne.feedback", "effect.parameter.feedback", "", -0.98f, 0.98f, 0.75f, &P::effect2 },
    { "damping", "peigne.damping", "effect.parameter.damping", "Hz", 500.0f, 20'000.0f, 9'000.0f, &P::effect3 },
    { "spread", "peigne.spread", "effect.parameter.spread", "", 0.0f, 1.0f, 0.0f, &P::effect4 },
    { "lfo-depth", "peigne.lfoDepth", "effect.parameter.lfoDepth", "", 0.0f, 1.0f, 0.0f, &P::effect5 },
    { "lfo-rate", "peigne.lfoRate", "effect.parameter.lfoRate", "Hz", 0.01f, 10.0f, 0.3f, &P::effect6 },
    { "mix", "peigne.mix", "effect.parameter.mix", "", 0.0f, 1.0f, 0.5f, &P::effect7 },
    { "output", "peigne.output", "effect.parameter.output", "dB", -24.0f, 24.0f, 0.0f, &P::effect8 },
    { "mode", "peigne.mode", "effect.parameter.mode", "", 0.0f, 1.0f, 0.0f, nullptr, 0, -1 },
    { "sync", "peigne.sync", "effect.parameter.sync", "", 0.0f, 12.0f, 0.0f, nullptr, 1, -1 },
};
inline constexpr EffectParameter utilitaireParameters[] {
    { "gain", "utilitaire.gain", "effect.parameter.gain", "dB", -36.0f, 36.0f, 0.0f, &P::effect1 },
    { "pan", "utilitaire.pan", "effect.parameter.pan", "", -1.0f, 1.0f, 0.0f, &P::effect2 },
    { "width", "utilitaire.width", "effect.parameter.width", "", 0.0f, 2.0f, 1.0f, &P::effect3 },
    { "mono-bass", "utilitaire.monoBass", "effect.parameter.monoBass", "Hz", 20.0f, 500.0f, 20.0f, &P::effect4 },
    { "invert-left", "utilitaire.invertLeft", "effect.parameter.invertLeft", "", 0.0f, 1.0f, 0.0f, nullptr, 0, -1 },
    { "invert-right", "utilitaire.invertRight", "effect.parameter.invertRight", "", 0.0f, 1.0f, 0.0f, nullptr, 1, -1 },
    { "channel-mode", "utilitaire.channelMode", "effect.parameter.channelMode", "", 0.0f, 4.0f, 0.0f, nullptr, 2, -1 },
    { "dc-filter", "utilitaire.dcFilter", "effect.parameter.dcFilter", "", 0.0f, 1.0f, 0.0f, nullptr, 3, -1 },
    { "mute", "utilitaire.mute", "effect.parameter.mute", "", 0.0f, 1.0f, 0.0f, nullptr, 4, -1 },
    { "delay", "utilitaire.delay", "effect.parameter.delay", "ms", 0.0f, 20.0f, 0.0f, nullptr, 5, -1 },
};
inline constexpr EffectParameter stereoscopeParameters[] {
    { "zoom", "stereoscope.zoom", "effect.parameter.zoom", "", 0.5f, 4.0f, 1.0f, &P::effect1 },
    { "persistence", "stereoscope.persistence", "effect.parameter.persistence", "", 0.0f, 1.0f, 0.5f, &P::effect2 },
    { "history", "stereoscope.history", "effect.parameter.historyLength", "s", 1.0f, 30.0f, 10.0f, &P::effect3 },
    { "display-mode", "stereoscope.displayMode", "effect.parameter.displayMode", "", 0.0f, 1.0f, 0.0f, nullptr, 0, -1 },
};

// ---- The MIDI effects (MidiEffects.h computes what each value does; the
// enums there follow these arrays' order) ----

inline constexpr EffectParameter arpegeParameters[] {
    { "gate", "arpege.gate", "effect.parameter.gateLength", "", 0.05f, 2.0f, 0.6f, &P::effect1 },
    { "swing", "arpege.swing", "effect.parameter.swing", "", 0.0f, 0.75f, 0.0f, &P::effect2 },
    { "velocity", "arpege.velocity", "effect.parameter.fixedVelocity", "", 1.0f, 127.0f, 100.0f, &P::effect3 },
    { "rate-sync", "arpege.rate", "effect.parameter.stepRate", "", 1.0f, 12.0f, 3.0f, nullptr, 0, -1 },
    { "mode", "arpege.mode", "effect.parameter.mode", "", 0.0f, 8.0f, 0.0f, nullptr, 1, -1 },
    { "octaves", "arpege.octaves", "effect.parameter.octaves", "", 1.0f, 4.0f, 1.0f, nullptr, 2, -1 },
    { "velocity-mode", "arpege.velocityMode", "effect.parameter.velocityMode", "", 0.0f, 2.0f, 0.0f, nullptr, 3, -1 },
    { "latch", "arpege.latch", "effect.parameter.latch", "", 0.0f, 1.0f, 0.0f, nullptr, 4, -1 },
    { "retrigger", "arpege.retrigger", "effect.parameter.retrigger", "", 0.0f, 1.0f, 1.0f, nullptr, 5, -1 },
};
inline constexpr EffectParameter accordsParameters[] {
    { "velocity", "accords.velocity", "effect.parameter.addedVelocity", "", 0.0f, 1.0f, 0.85f, &P::effect1 },
    { "strum", "accords.strum", "effect.parameter.strum", "ms", 0.0f, 200.0f, 0.0f, &P::effect2 },
    { "chord-type", "accords.chord", "effect.parameter.chordType", "", 0.0f, 11.0f, 0.0f, nullptr, 0, -1 },
    { "interval-1", "accords.interval1", "effect.parameter.interval", "", -24.0f, 24.0f, 4.0f, nullptr, 1, 0 },
    { "interval-2", "accords.interval2", "effect.parameter.interval", "", -24.0f, 24.0f, 7.0f, nullptr, 2, 1 },
    { "interval-3", "accords.interval3", "effect.parameter.interval", "", -24.0f, 24.0f, 0.0f, nullptr, 3, 2 },
    { "interval-4", "accords.interval4", "effect.parameter.interval", "", -24.0f, 24.0f, 0.0f, nullptr, 4, 3 },
    { "interval-5", "accords.interval5", "effect.parameter.interval", "", -24.0f, 24.0f, 0.0f, nullptr, 5, 4 },
    { "interval-6", "accords.interval6", "effect.parameter.interval", "", -24.0f, 24.0f, 0.0f, nullptr, 6, 5 },
    { "inversion", "accords.inversion", "effect.parameter.inversion", "", -3.0f, 3.0f, 0.0f, nullptr, 7, -1 },
    { "spread", "accords.spread", "effect.parameter.openVoicing", "", 0.0f, 1.0f, 0.0f, nullptr, 8, -1 },
    { "strum-mode", "accords.strumMode", "effect.parameter.strumDirection", "", 0.0f, 2.0f, 0.0f, nullptr, 9, -1 },
    { "key", "accords.key", "effect.parameter.key", "", 0.0f, 11.0f, 0.0f, nullptr, 10, -1 },
    { "scale", "accords.scale", "effect.parameter.scale", "", 0.0f, 11.0f, 0.0f, nullptr, 11, -1 },
};
inline constexpr EffectParameter gammeParameters[] {
    { "transpose", "gamme.transpose", "effect.parameter.transpose", "", -24.0f, 24.0f, 0.0f, &P::effect1 },
    { "degrees", "gamme.degrees", "effect.parameter.degreeShift", "", -7.0f, 7.0f, 0.0f, &P::effect2 },
    { "key", "gamme.key", "effect.parameter.key", "", 0.0f, 11.0f, 0.0f, nullptr, 0, -1 },
    { "scale", "gamme.scale", "effect.parameter.scale", "", 0.0f, 11.0f, 1.0f, nullptr, 1, -1 },
    { "mode", "gamme.mode", "effect.parameter.mode", "", 0.0f, 3.0f, 0.0f, nullptr, 2, -1 },
};
inline constexpr EffectParameter echoNotesParameters[] {
    { "time", "echonotes.time", "effect.parameter.time", "ms", 10.0f, 2'000.0f, 250.0f, &P::effect1 },
    { "decay", "echonotes.decay", "effect.parameter.velocityDecay", "", 0.0f, 1.0f, 0.3f, &P::effect2 },
    { "pitch", "echonotes.pitch", "effect.parameter.pitchStep", "", -12.0f, 12.0f, 0.0f, &P::effect3 },
    { "sync", "echonotes.sync", "effect.parameter.sync", "", 0.0f, 12.0f, 6.0f, nullptr, 0, -1 },
    { "repeats", "echonotes.repeats", "effect.parameter.repeats", "", 1.0f, 16.0f, 3.0f, nullptr, 1, -1 },
};
inline constexpr EffectParameter aleaParameters[] {
    { "timing", "alea.timing", "effect.parameter.timingRandom", "ms", 0.0f, 50.0f, 0.0f, &P::effect1 },
    { "velocity", "alea.velocity", "effect.parameter.velocityRandom", "", 0.0f, 1.0f, 0.15f, &P::effect2 },
    { "chance", "alea.chance", "effect.parameter.playChance", "", 0.0f, 1.0f, 1.0f, &P::effect3 },
    { "pitch-chance", "alea.pitchChance", "effect.parameter.pitchChance", "", 0.0f, 1.0f, 0.0f, &P::effect4 },
    { "pitch-range", "alea.pitchRange", "effect.parameter.pitchRange", "", 0.0f, 12.0f, 0.0f, &P::effect5 },
};
inline constexpr EffectParameter dureeParameters[] {
    { "length", "duree.length", "effect.parameter.noteLength", "ms", 10.0f, 4'000.0f, 250.0f, &P::effect1 },
    { "factor", "duree.factor", "effect.parameter.stretch", "", 1.0f, 4.0f, 1.5f, &P::effect2 },
    { "mode", "duree.mode", "effect.parameter.mode", "", 0.0f, 3.0f, 0.0f, nullptr, 0, -1 },
    { "length-sync", "duree.sync", "effect.parameter.sync", "", 1.0f, 12.0f, 6.0f, nullptr, 1, -1 },
};
inline constexpr EffectParameter sequenceurParameters[] {
    { "swing", "sequenceur.swing", "effect.parameter.swing", "", 0.0f, 0.75f, 0.0f, &P::effect1 },
    { "gate", "sequenceur.gate", "effect.parameter.gateLength", "", 0.05f, 1.0f, 0.75f, &P::effect2 },
    { "mode", "sequenceur.mode", "effect.parameter.mode", "", 0.0f, 1.0f, 0.0f, nullptr, 0, -1 },
    { "steps", "sequenceur.steps", "effect.parameter.stepCount", "", 1.0f, 16.0f, 16.0f, nullptr, 1, -1 },
    { "rate-sync", "sequenceur.rate", "effect.parameter.stepRate", "", 1.0f, 12.0f, 3.0f, nullptr, 2, -1 },
    { "direction-mode", "sequenceur.direction", "effect.parameter.direction", "", 0.0f, 3.0f, 0.0f, nullptr, 3, -1 },
    { "root", "sequenceur.root", "effect.parameter.rootNote", "", 24.0f, 96.0f, 60.0f, nullptr, 4, -1 },
    { "step-1-on", "sequenceur.step1On", "effect.parameter.stepOn", "", 0.0f, 1.0f, 1.0f, nullptr, 5, 0 },
    { "step-1-note", "sequenceur.step1Note", "effect.parameter.stepNote", "", -24.0f, 24.0f, 0.0f, nullptr, 6, 0 },
    { "step-1-velocity", "sequenceur.step1Velocity", "effect.parameter.stepVelocity", "", 1.0f, 127.0f, 120.0f, nullptr, 7, 0 },
    { "step-1-length", "sequenceur.step1Length", "effect.parameter.stepLength", "", 0.1f, 1.0f, 1.0f, nullptr, 8, 0 },
    { "step-2-on", "sequenceur.step2On", "effect.parameter.stepOn", "", 0.0f, 1.0f, 1.0f, nullptr, 9, 1 },
    { "step-2-note", "sequenceur.step2Note", "effect.parameter.stepNote", "", -24.0f, 24.0f, 0.0f, nullptr, 10, 1 },
    { "step-2-velocity", "sequenceur.step2Velocity", "effect.parameter.stepVelocity", "", 1.0f, 127.0f, 96.0f, nullptr, 11, 1 },
    { "step-2-length", "sequenceur.step2Length", "effect.parameter.stepLength", "", 0.1f, 1.0f, 1.0f, nullptr, 12, 1 },
    { "step-3-on", "sequenceur.step3On", "effect.parameter.stepOn", "", 0.0f, 1.0f, 1.0f, nullptr, 13, 2 },
    { "step-3-note", "sequenceur.step3Note", "effect.parameter.stepNote", "", -24.0f, 24.0f, 12.0f, nullptr, 14, 2 },
    { "step-3-velocity", "sequenceur.step3Velocity", "effect.parameter.stepVelocity", "", 1.0f, 127.0f, 96.0f, nullptr, 15, 2 },
    { "step-3-length", "sequenceur.step3Length", "effect.parameter.stepLength", "", 0.1f, 1.0f, 1.0f, nullptr, 16, 2 },
    { "step-4-on", "sequenceur.step4On", "effect.parameter.stepOn", "", 0.0f, 1.0f, 1.0f, nullptr, 17, 3 },
    { "step-4-note", "sequenceur.step4Note", "effect.parameter.stepNote", "", -24.0f, 24.0f, 0.0f, nullptr, 18, 3 },
    { "step-4-velocity", "sequenceur.step4Velocity", "effect.parameter.stepVelocity", "", 1.0f, 127.0f, 96.0f, nullptr, 19, 3 },
    { "step-4-length", "sequenceur.step4Length", "effect.parameter.stepLength", "", 0.1f, 1.0f, 1.0f, nullptr, 20, 3 },
    { "step-5-on", "sequenceur.step5On", "effect.parameter.stepOn", "", 0.0f, 1.0f, 1.0f, nullptr, 21, 4 },
    { "step-5-note", "sequenceur.step5Note", "effect.parameter.stepNote", "", -24.0f, 24.0f, 7.0f, nullptr, 22, 4 },
    { "step-5-velocity", "sequenceur.step5Velocity", "effect.parameter.stepVelocity", "", 1.0f, 127.0f, 120.0f, nullptr, 23, 4 },
    { "step-5-length", "sequenceur.step5Length", "effect.parameter.stepLength", "", 0.1f, 1.0f, 1.0f, nullptr, 24, 4 },
    { "step-6-on", "sequenceur.step6On", "effect.parameter.stepOn", "", 0.0f, 1.0f, 1.0f, nullptr, 25, 5 },
    { "step-6-note", "sequenceur.step6Note", "effect.parameter.stepNote", "", -24.0f, 24.0f, 0.0f, nullptr, 26, 5 },
    { "step-6-velocity", "sequenceur.step6Velocity", "effect.parameter.stepVelocity", "", 1.0f, 127.0f, 96.0f, nullptr, 27, 5 },
    { "step-6-length", "sequenceur.step6Length", "effect.parameter.stepLength", "", 0.1f, 1.0f, 1.0f, nullptr, 28, 5 },
    { "step-7-on", "sequenceur.step7On", "effect.parameter.stepOn", "", 0.0f, 1.0f, 1.0f, nullptr, 29, 6 },
    { "step-7-note", "sequenceur.step7Note", "effect.parameter.stepNote", "", -24.0f, 24.0f, 12.0f, nullptr, 30, 6 },
    { "step-7-velocity", "sequenceur.step7Velocity", "effect.parameter.stepVelocity", "", 1.0f, 127.0f, 96.0f, nullptr, 31, 6 },
    { "step-7-length", "sequenceur.step7Length", "effect.parameter.stepLength", "", 0.1f, 1.0f, 1.0f, nullptr, 32, 6 },
    { "step-8-on", "sequenceur.step8On", "effect.parameter.stepOn", "", 0.0f, 1.0f, 1.0f, nullptr, 33, 7 },
    { "step-8-note", "sequenceur.step8Note", "effect.parameter.stepNote", "", -24.0f, 24.0f, 3.0f, nullptr, 34, 7 },
    { "step-8-velocity", "sequenceur.step8Velocity", "effect.parameter.stepVelocity", "", 1.0f, 127.0f, 96.0f, nullptr, 35, 7 },
    { "step-8-length", "sequenceur.step8Length", "effect.parameter.stepLength", "", 0.1f, 1.0f, 1.0f, nullptr, 36, 7 },
    { "step-9-on", "sequenceur.step9On", "effect.parameter.stepOn", "", 0.0f, 1.0f, 1.0f, nullptr, 37, 8 },
    { "step-9-note", "sequenceur.step9Note", "effect.parameter.stepNote", "", -24.0f, 24.0f, 0.0f, nullptr, 38, 8 },
    { "step-9-velocity", "sequenceur.step9Velocity", "effect.parameter.stepVelocity", "", 1.0f, 127.0f, 120.0f, nullptr, 39, 8 },
    { "step-9-length", "sequenceur.step9Length", "effect.parameter.stepLength", "", 0.1f, 1.0f, 1.0f, nullptr, 40, 8 },
    { "step-10-on", "sequenceur.step10On", "effect.parameter.stepOn", "", 0.0f, 1.0f, 1.0f, nullptr, 41, 9 },
    { "step-10-note", "sequenceur.step10Note", "effect.parameter.stepNote", "", -24.0f, 24.0f, 0.0f, nullptr, 42, 9 },
    { "step-10-velocity", "sequenceur.step10Velocity", "effect.parameter.stepVelocity", "", 1.0f, 127.0f, 96.0f, nullptr, 43, 9 },
    { "step-10-length", "sequenceur.step10Length", "effect.parameter.stepLength", "", 0.1f, 1.0f, 1.0f, nullptr, 44, 9 },
    { "step-11-on", "sequenceur.step11On", "effect.parameter.stepOn", "", 0.0f, 1.0f, 1.0f, nullptr, 45, 10 },
    { "step-11-note", "sequenceur.step11Note", "effect.parameter.stepNote", "", -24.0f, 24.0f, 12.0f, nullptr, 46, 10 },
    { "step-11-velocity", "sequenceur.step11Velocity", "effect.parameter.stepVelocity", "", 1.0f, 127.0f, 96.0f, nullptr, 47, 10 },
    { "step-11-length", "sequenceur.step11Length", "effect.parameter.stepLength", "", 0.1f, 1.0f, 1.0f, nullptr, 48, 10 },
    { "step-12-on", "sequenceur.step12On", "effect.parameter.stepOn", "", 0.0f, 1.0f, 1.0f, nullptr, 49, 11 },
    { "step-12-note", "sequenceur.step12Note", "effect.parameter.stepNote", "", -24.0f, 24.0f, 0.0f, nullptr, 50, 11 },
    { "step-12-velocity", "sequenceur.step12Velocity", "effect.parameter.stepVelocity", "", 1.0f, 127.0f, 96.0f, nullptr, 51, 11 },
    { "step-12-length", "sequenceur.step12Length", "effect.parameter.stepLength", "", 0.1f, 1.0f, 1.0f, nullptr, 52, 11 },
    { "step-13-on", "sequenceur.step13On", "effect.parameter.stepOn", "", 0.0f, 1.0f, 1.0f, nullptr, 53, 12 },
    { "step-13-note", "sequenceur.step13Note", "effect.parameter.stepNote", "", -24.0f, 24.0f, 10.0f, nullptr, 54, 12 },
    { "step-13-velocity", "sequenceur.step13Velocity", "effect.parameter.stepVelocity", "", 1.0f, 127.0f, 120.0f, nullptr, 55, 12 },
    { "step-13-length", "sequenceur.step13Length", "effect.parameter.stepLength", "", 0.1f, 1.0f, 1.0f, nullptr, 56, 12 },
    { "step-14-on", "sequenceur.step14On", "effect.parameter.stepOn", "", 0.0f, 1.0f, 1.0f, nullptr, 57, 13 },
    { "step-14-note", "sequenceur.step14Note", "effect.parameter.stepNote", "", -24.0f, 24.0f, 7.0f, nullptr, 58, 13 },
    { "step-14-velocity", "sequenceur.step14Velocity", "effect.parameter.stepVelocity", "", 1.0f, 127.0f, 96.0f, nullptr, 59, 13 },
    { "step-14-length", "sequenceur.step14Length", "effect.parameter.stepLength", "", 0.1f, 1.0f, 1.0f, nullptr, 60, 13 },
    { "step-15-on", "sequenceur.step15On", "effect.parameter.stepOn", "", 0.0f, 1.0f, 1.0f, nullptr, 61, 14 },
    { "step-15-note", "sequenceur.step15Note", "effect.parameter.stepNote", "", -24.0f, 24.0f, 12.0f, nullptr, 62, 14 },
    { "step-15-velocity", "sequenceur.step15Velocity", "effect.parameter.stepVelocity", "", 1.0f, 127.0f, 96.0f, nullptr, 63, 14 },
    { "step-15-length", "sequenceur.step15Length", "effect.parameter.stepLength", "", 0.1f, 1.0f, 1.0f, nullptr, 64, 14 },
    { "step-16-on", "sequenceur.step16On", "effect.parameter.stepOn", "", 0.0f, 1.0f, 1.0f, nullptr, 65, 15 },
    { "step-16-note", "sequenceur.step16Note", "effect.parameter.stepNote", "", -24.0f, 24.0f, 5.0f, nullptr, 66, 15 },
    { "step-16-velocity", "sequenceur.step16Velocity", "effect.parameter.stepVelocity", "", 1.0f, 127.0f, 96.0f, nullptr, 67, 15 },
    { "step-16-length", "sequenceur.step16Length", "effect.parameter.stepLength", "", 0.1f, 1.0f, 1.0f, nullptr, 68, 15 },
};
inline constexpr EffectParameter roulementParameters[] {
    { "rate", "roulement.rate", "effect.parameter.rate", "Hz", 1.0f, 40.0f, 8.0f, &P::effect1 },
    { "gate", "roulement.gate", "effect.parameter.gateLength", "", 0.1f, 1.0f, 0.5f, &P::effect2 },
    { "ramp", "roulement.ramp", "effect.parameter.velocityRamp", "", -1.0f, 1.0f, 0.0f, &P::effect3 },
    { "sync", "roulement.sync", "effect.parameter.sync", "", 0.0f, 12.0f, 3.0f, nullptr, 0, -1 },
};


// The modulators (Modulators.h): their settings in effect1..8, the steps and
// the card they belong to in slots (modulator::stepSlot, ownerLowSlot…).
inline constexpr std::string_view modulators = "nodes.category.modulators";
#define CYDAW_MODULATOR_OWNER(prefix)                                                                                  \
    { "owner-low", prefix ".ownerLow", "modulator.parameter.owner", "", 0.0f, 16777215.0f, 0.0f, nullptr, 16, -1 },    \
    { "owner-high", prefix ".ownerHigh", "modulator.parameter.owner", "", 0.0f, 16777215.0f, 0.0f, nullptr, 17, -1 }
inline constexpr EffectParameter modLfoParameters[] {
    { "rate", "modLfo.rate", "effect.parameter.rate", "Hz", 0.01f, 40.0f, 1.0f, &P::effect1 },
    { "sync", "modLfo.sync", "effect.parameter.sync", "", 0.0f, 13.0f, 0.0f, &P::effect2 },
    { "shape", "modLfo.shape", "effect.parameter.shape", "", 0.0f, 6.0f, 0.0f, &P::effect3 },
    { "phase", "modLfo.phase", "effect.parameter.phase", "", 0.0f, 1.0f, 0.0f, &P::effect4 },
    { "retrigger", "modLfo.retrigger", "modulator.parameter.retrigger", "", 0.0f, 1.0f, 0.0f, &P::effect5 },
    { "bipolar", "modLfo.bipolar", "modulator.parameter.bipolar", "", 0.0f, 1.0f, 1.0f, &P::effect6 },
    { "smooth", "modLfo.smooth", "modulator.parameter.smooth", "", 0.0f, 1.0f, 0.0f, &P::effect7 },
    CYDAW_MODULATOR_OWNER("modLfo"),
};
inline constexpr EffectParameter modStepsParameters[] {
    { "steps", "modSteps.steps", "modulator.parameter.steps", "", 1.0f, 16.0f, 8.0f, &P::effect1 },
    { "sync", "modSteps.sync", "effect.parameter.sync", "", 0.0f, 13.0f, 6.0f, &P::effect2 },
    { "rate", "modSteps.rate", "effect.parameter.rate", "Hz", 0.05f, 40.0f, 4.0f, &P::effect3 },
    { "smooth", "modSteps.smooth", "modulator.parameter.smooth", "", 0.0f, 1.0f, 0.0f, &P::effect4 },
    { "bipolar", "modSteps.bipolar", "modulator.parameter.bipolar", "", 0.0f, 1.0f, 0.0f, &P::effect5 },
    { "retrigger", "modSteps.retrigger", "modulator.parameter.retrigger", "", 0.0f, 1.0f, 0.0f, &P::effect6 },
    { "step1", "modSteps.step1", "modulator.parameter.step1", "", 0.0f, 1.0f, 1.0f, nullptr, 0, -1 },
    { "step2", "modSteps.step2", "modulator.parameter.step2", "", 0.0f, 1.0f, 0.25f, nullptr, 1, -1 },
    { "step3", "modSteps.step3", "modulator.parameter.step3", "", 0.0f, 1.0f, 0.75f, nullptr, 2, -1 },
    { "step4", "modSteps.step4", "modulator.parameter.step4", "", 0.0f, 1.0f, 0.5f, nullptr, 3, -1 },
    { "step5", "modSteps.step5", "modulator.parameter.step5", "", 0.0f, 1.0f, 1.0f, nullptr, 4, -1 },
    { "step6", "modSteps.step6", "modulator.parameter.step6", "", 0.0f, 1.0f, 0.0f, nullptr, 5, -1 },
    { "step7", "modSteps.step7", "modulator.parameter.step7", "", 0.0f, 1.0f, 0.6f, nullptr, 6, -1 },
    { "step8", "modSteps.step8", "modulator.parameter.step8", "", 0.0f, 1.0f, 0.3f, nullptr, 7, -1 },
    { "step9", "modSteps.step9", "modulator.parameter.step9", "", 0.0f, 1.0f, 0.9f, nullptr, 8, -1 },
    { "step10", "modSteps.step10", "modulator.parameter.step10", "", 0.0f, 1.0f, 0.2f, nullptr, 9, -1 },
    { "step11", "modSteps.step11", "modulator.parameter.step11", "", 0.0f, 1.0f, 0.7f, nullptr, 10, -1 },
    { "step12", "modSteps.step12", "modulator.parameter.step12", "", 0.0f, 1.0f, 0.4f, nullptr, 11, -1 },
    { "step13", "modSteps.step13", "modulator.parameter.step13", "", 0.0f, 1.0f, 0.8f, nullptr, 12, -1 },
    { "step14", "modSteps.step14", "modulator.parameter.step14", "", 0.0f, 1.0f, 0.1f, nullptr, 13, -1 },
    { "step15", "modSteps.step15", "modulator.parameter.step15", "", 0.0f, 1.0f, 0.5f, nullptr, 14, -1 },
    { "step16", "modSteps.step16", "modulator.parameter.step16", "", 0.0f, 1.0f, 0.35f, nullptr, 15, -1 },
    CYDAW_MODULATOR_OWNER("modSteps"),
};
inline constexpr EffectParameter modRandomParameters[] {
    { "rate", "modRandom.rate", "effect.parameter.rate", "Hz", 0.05f, 40.0f, 4.0f, &P::effect1 },
    { "sync", "modRandom.sync", "effect.parameter.sync", "", 0.0f, 13.0f, 6.0f, &P::effect2 },
    { "smooth", "modRandom.smooth", "modulator.parameter.smooth", "", 0.0f, 1.0f, 0.0f, &P::effect3 },
    { "bipolar", "modRandom.bipolar", "modulator.parameter.bipolar", "", 0.0f, 1.0f, 1.0f, &P::effect4 },
    { "chance", "modRandom.chance", "modulator.parameter.chance", "", 0.0f, 1.0f, 1.0f, &P::effect5 },
    { "retrigger", "modRandom.retrigger", "modulator.parameter.retrigger", "", 0.0f, 1.0f, 0.0f, &P::effect6 },
    CYDAW_MODULATOR_OWNER("modRandom"),
};
inline constexpr EffectParameter modAdsrParameters[] {
    { "attack", "modAdsr.attack", "effect.parameter.attack", "s", 0.001f, 10.0f, 0.01f, &P::effect1 },
    { "decay", "modAdsr.decay", "effect.parameter.decay", "s", 0.001f, 10.0f, 0.3f, &P::effect2 },
    { "sustain", "modAdsr.sustain", "effect.parameter.sustain", "", 0.0f, 1.0f, 0.6f, &P::effect3 },
    { "release", "modAdsr.release", "effect.parameter.release", "s", 0.001f, 10.0f, 0.4f, &P::effect4 },
    { "velocity", "modAdsr.velocity", "modulator.parameter.velocity", "", 0.0f, 1.0f, 0.0f, &P::effect5 },
    { "legato", "modAdsr.legato", "modulator.parameter.legato", "", 0.0f, 1.0f, 0.0f, &P::effect6 },
    CYDAW_MODULATOR_OWNER("modAdsr"),
};
inline constexpr EffectParameter modNoteParameters[] {
    { "source", "modNote.source", "modulator.parameter.noteSource", "", 0.0f, 2.0f, 0.0f, &P::effect1 },
    { "root", "modNote.root", "modulator.parameter.root", "", 0.0f, 127.0f, 60.0f, &P::effect2 },
    { "range", "modNote.range", "modulator.parameter.range", "st", 1.0f, 127.0f, 48.0f, &P::effect3 },
    CYDAW_MODULATOR_OWNER("modNote"),
};
inline constexpr EffectParameter modExpressionParameters[] {
    { "source", "modExpression.source", "modulator.parameter.expressionSource", "", 0.0f, 3.0f, 0.0f, &P::effect1 },
    { "controller", "modExpression.controller", "modulator.parameter.controller", "", 0.0f, 127.0f, 74.0f, &P::effect2 },
    { "smooth", "modExpression.smooth", "modulator.parameter.smooth", "", 0.0f, 1.0f, 0.1f, &P::effect3 },
    CYDAW_MODULATOR_OWNER("modExpression"),
};
inline constexpr EffectParameter modMacroParameters[] {
    { "value", "modMacro.value", "modulator.parameter.value", "", 0.0f, 1.0f, 0.5f, &P::effect1 },
    { "bipolar", "modMacro.bipolar", "modulator.parameter.bipolar", "", 0.0f, 1.0f, 0.0f, &P::effect2 },
    CYDAW_MODULATOR_OWNER("modMacro"),
};
inline constexpr EffectParameter modButtonParameters[] {
    { "on", "modButton.on", "modulator.parameter.on", "", 0.0f, 1.0f, 0.0f, &P::effect1 },
    CYDAW_MODULATOR_OWNER("modButton"),
};
inline constexpr EffectParameter modMathParameters[] {
    { "mode", "modMath.mode", "modulator.parameter.mode", "", 0.0f, 6.0f, 0.0f, &P::effect1 },
    { "blend", "modMath.blend", "modulator.parameter.blend", "", 0.0f, 1.0f, 0.5f, &P::effect2 },
    { "a", "modMath.a", "modulator.parameter.a", "", -1.0f, 1.0f, 0.0f, &P::effect3 },
    { "b", "modMath.b", "modulator.parameter.b", "", -1.0f, 1.0f, 0.0f, &P::effect4 },
    CYDAW_MODULATOR_OWNER("modMath"),
};
inline constexpr EffectParameter modFollowerParameters[] {
    { "attack", "modFollower.attack", "effect.parameter.attack", "ms", 1.0f, 500.0f, 10.0f, &P::effect1 },
    { "release", "modFollower.release", "effect.parameter.release", "ms", 5.0f, 2000.0f, 150.0f, &P::effect2 },
    { "gain", "modFollower.gain", "modulator.parameter.gain", "dB", -24.0f, 24.0f, 0.0f, &P::effect3 },
    CYDAW_MODULATOR_OWNER("modFollower"),
};
#undef CYDAW_MODULATOR_OWNER

// The containers (Containers.h): every one mixes its chains back into its
// input by Mix, their sum lifted by Wet Gain.
#define CYDAW_CONTAINER_MIX(prefix)                                                                                      \
    { "mix", prefix ".mix", "effect.parameter.mix", "", 0.0f, 1.0f, 1.0f, &P::effect1 },                                 \
    { "wet-gain", prefix ".wetGain", "container.parameter.wetGain", "dB", -36.0f, 12.0f, 0.0f, &P::effect2 }
// A branch's own: its volume, pan, mute and solo, labelled by its number.
#define CYDAW_BRANCH(prefix, n)                                                                                          \
    { "volume" #n, prefix ".volume" #n, "effect.parameter.volume", "dB", -60.0f, 12.0f, 0.0f, nullptr, (n) - 1, (n) - 1 },  \
    { "pan" #n, prefix ".pan" #n, "effect.parameter.pan", "", -1.0f, 1.0f, 0.0f, nullptr, 8 + (n) - 1, (n) - 1 },         \
    { "mute" #n, prefix ".mute" #n, "container.parameter.mute", "", 0.0f, 1.0f, 0.0f, nullptr, 16 + (n) - 1, (n) - 1 },   \
    { "solo" #n, prefix ".solo" #n, "container.parameter.solo", "", 0.0f, 1.0f, 0.0f, nullptr, 24 + (n) - 1, (n) - 1 }
#define CYDAW_BRANCHES(prefix)                                                                                           \
    CYDAW_BRANCH(prefix, 1), CYDAW_BRANCH(prefix, 2), CYDAW_BRANCH(prefix, 3), CYDAW_BRANCH(prefix, 4),                 \
    CYDAW_BRANCH(prefix, 5), CYDAW_BRANCH(prefix, 6), CYDAW_BRANCH(prefix, 7), CYDAW_BRANCH(prefix, 8)
// A band's: its level, mute and solo.
#define CYDAW_BAND(prefix, n)                                                                                            \
    { "volume" #n, prefix ".volume" #n, "effect.parameter.volume", "dB", -60.0f, 12.0f, 0.0f, nullptr, (n) - 1, (n) - 1 },  \
    { "mute" #n, prefix ".mute" #n, "container.parameter.mute", "", 0.0f, 1.0f, 0.0f, nullptr, 16 + (n) - 1, (n) - 1 },   \
    { "solo" #n, prefix ".solo" #n, "container.parameter.solo", "", 0.0f, 1.0f, 0.0f, nullptr, 24 + (n) - 1, (n) - 1 }
inline constexpr EffectParameter fxChainParameters[] {
    CYDAW_CONTAINER_MIX("fxChain"),
};
inline constexpr EffectParameter fxLayerParameters[] {
    CYDAW_CONTAINER_MIX("fxLayer"),
    { "chains", "fxLayer.chains", "container.parameter.layers", "", 1.0f, 8.0f, 2.0f, &P::effect3 },
    CYDAW_BRANCHES("fxLayer"),
};
inline constexpr EffectParameter fxSelectorParameters[] {
    CYDAW_CONTAINER_MIX("fxSelector"),
    { "chains", "fxSelector.chains", "container.parameter.chains", "", 1.0f, 8.0f, 2.0f, &P::effect3 },
    { "selected", "fxSelector.selected", "container.parameter.selected", "", 0.0f, 7.0f, 0.0f, &P::effect4 },
    { "fade", "fxSelector.fade", "container.parameter.fade", "ms", 0.0f, 500.0f, 20.0f, &P::effect5 },
    CYDAW_BRANCHES("fxSelector"),
};
inline constexpr EffectParameter midSideParameters[] {
    CYDAW_CONTAINER_MIX("midSide"),
    { "mid-gain", "midSide.midGain", "container.parameter.midGain", "dB", -36.0f, 12.0f, 0.0f, &P::effect3 },
    { "side-gain", "midSide.sideGain", "container.parameter.sideGain", "dB", -36.0f, 12.0f, 0.0f, &P::effect4 },
};
inline constexpr EffectParameter stereoSplitParameters[] {
    CYDAW_CONTAINER_MIX("stereoSplit"),
    { "left-gain", "stereoSplit.leftGain", "container.parameter.leftGain", "dB", -36.0f, 12.0f, 0.0f, &P::effect3 },
    { "right-gain", "stereoSplit.rightGain", "container.parameter.rightGain", "dB", -36.0f, 12.0f, 0.0f, &P::effect4 },
};
inline constexpr EffectParameter multiband2Parameters[] {
    CYDAW_CONTAINER_MIX("multiband2"),
    { "crossover", "multiband2.crossover", "container.parameter.crossover", "Hz", 40.0f, 16000.0f, 400.0f, &P::effect3 },
    CYDAW_BAND("multiband2", 1), CYDAW_BAND("multiband2", 2),
};
inline constexpr EffectParameter multiband3Parameters[] {
    CYDAW_CONTAINER_MIX("multiband3"),
    { "crossover-low", "multiband3.crossoverLow", "container.parameter.crossoverLow", "Hz", 40.0f, 16000.0f, 250.0f, &P::effect3 },
    { "crossover-high", "multiband3.crossoverHigh", "container.parameter.crossoverHigh", "Hz", 40.0f, 16000.0f, 2500.0f, &P::effect4 },
    CYDAW_BAND("multiband3", 1), CYDAW_BAND("multiband3", 2), CYDAW_BAND("multiband3", 3),
};
inline constexpr EffectParameter xyFxParameters[] {
    CYDAW_CONTAINER_MIX("xyFx"),
    { "x", "xyFx.x", "container.parameter.x", "", 0.0f, 1.0f, 0.5f, &P::effect3 },
    { "y", "xyFx.y", "container.parameter.y", "", 0.0f, 1.0f, 0.5f, &P::effect4 },
};
inline constexpr EffectParameter replacerParameters[] {
    CYDAW_CONTAINER_MIX("replacer"),
    { "threshold", "replacer.threshold", "effect.parameter.threshold", "dB", -60.0f, 0.0f, -24.0f, &P::effect3 },
    { "sensitivity", "replacer.sensitivity", "container.parameter.sensitivity", "dB", 1.0f, 24.0f, 6.0f, &P::effect4 },
    { "hold", "replacer.hold", "container.parameter.hold", "ms", 10.0f, 500.0f, 60.0f, &P::effect5 },
    { "generator", "replacer.generator", "container.parameter.generator", "", 0.0f, 4.0f, 0.0f, &P::effect6 },
    { "pitch", "replacer.pitch", "container.parameter.pitch", "Hz", 20.0f, 2000.0f, 55.0f, &P::effect7 },
    { "decay", "replacer.decay", "effect.parameter.decay", "ms", 5.0f, 2000.0f, 250.0f, &P::effect8 },
    { "velocity", "replacer.velocity", "modulator.parameter.velocity", "", 0.0f, 1.0f, 0.5f, nullptr, 0, -1 },
    // The instrument's: the note it plays, how long it holds it.
    { "note", "replacer.note", "container.parameter.note", "", 0.0f, 127.0f, 36.0f, nullptr, 1, -1 },
    { "length", "replacer.length", "container.parameter.length", "ms", 10.0f, 2000.0f, 120.0f, nullptr, 2, -1 },
};
#undef CYDAW_BAND
#undef CYDAW_BRANCHES
#undef CYDAW_BRANCH
#undef CYDAW_CONTAINER_MIX

inline constexpr std::string_view containers = "nodes.category.containers";

#undef CYDAW_EQ_DYNAMIC
#undef CYDAW_EQ_BAND
#undef CYDAW_SLOT
} // namespace effect_detail

inline constexpr std::array<EffectDescriptor, 79> effectCatalogue { {
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
      effect_detail::filters, EffectGroup::filter, 0xff4fd6e8u, effect_detail::eqPro, 2, 2, true },
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
    // Added on 2026-10-14.
    { "vocoder", "core.vocoder", effect_detail::K::vocoder, "node.vocoder", "node.vocoder.description", "Vocoder", effect_detail::effects, EffectGroup::modulation, 0xff9fd36bu, effect_detail::vocoderParameters, 2, 2, true },
    { "de-esser", "core.de-esser", effect_detail::K::deEsser, "node.deEsser", "node.deEsser.description", "De-esser", effect_detail::dynamics, EffectGroup::dynamics, 0xffffb36bu, effect_detail::deEsserParameters },
    { "tuner", "core.tuner", effect_detail::K::tuner, "node.tuner", "node.tuner.description", "Tuner", effect_detail::analyzers, EffectGroup::analysis, 0xff7fe0c0u, effect_detail::tunerParameters },
    { "harmonie", "core.harmonie", effect_detail::K::harmonie, "node.harmonie", "node.harmonie.description", "Harmonie Pitch Shifter", effect_detail::effects, EffectGroup::modulation, 0xffd08cffu, effect_detail::harmonieParameters },
    { "justesse", "core.justesse", effect_detail::K::justesse, "node.justesse", "node.justesse.description", "Justesse Pitch Correction", effect_detail::effects, EffectGroup::modulation, 0xffff8fc7u, effect_detail::justesseParameters },
    { "empreinte", "core.empreinte", effect_detail::K::empreinte, "node.empreinte", "node.empreinte.description", "Empreinte Convolution Reverb", effect_detail::effects, EffectGroup::delay, 0xff8fb0ffu, effect_detail::empreinteParameters },
    { "givre", "core.givre", effect_detail::K::givre, "node.givre", "node.givre.description", "Givre Spectral Freeze", effect_detail::effects, EffectGroup::delay, 0xffa8e6ffu, effect_detail::givreParameters },
    { "grains", "core.grains", effect_detail::K::grains, "node.grains", "node.grains.description", "Grains Delay", effect_detail::effects, EffectGroup::delay, 0xffe8c56au, effect_detail::grainsParameters },
    { "saccade", "core.saccade", effect_detail::K::saccade, "node.saccade", "node.saccade.description", "Saccade Repeat", effect_detail::effects, EffectGroup::delay, 0xffff7f9fu, effect_detail::saccadeParameters },
    { "boucleur", "core.boucleur", effect_detail::K::boucleur, "node.boucleur", "node.boucleur.description", "Boucleur Looper", effect_detail::effects, EffectGroup::delay, 0xff6be0a0u, effect_detail::boucleurParameters },
    { "pompe", "core.pompe", effect_detail::K::pompe, "node.pompe", "node.pompe.description", "Pompe Volume Shaper", effect_detail::dynamics, EffectGroup::dynamics, 0xffff6b6bu, effect_detail::pompeParameters, 2, 2, true },
    { "decaleur", "core.decaleur", effect_detail::K::decaleur, "node.decaleur", "node.decaleur.description", "Decaleur Frequency Shifter", effect_detail::effects, EffectGroup::modulation, 0xff6bd0ffu, effect_detail::decaleurParameters },
    { "anneau", "core.anneau", effect_detail::K::anneau, "node.anneau", "node.anneau.description", "Anneau Ring Modulator", effect_detail::effects, EffectGroup::modulation, 0xfff0a0ffu, effect_detail::anneauParameters },
    { "rotor", "core.rotor", effect_detail::K::rotor, "node.rotor", "node.rotor.description", "Rotor Rotary Speaker", effect_detail::effects, EffectGroup::modulation, 0xffd6a46bu, effect_detail::rotorParameters },
    { "resonateurs", "core.resonateurs", effect_detail::K::resonateurs, "node.resonateurs", "node.resonateurs.description", "Resonateurs", effect_detail::effects, EffectGroup::filter, 0xffc0a0ffu, effect_detail::resonateursParameters },
    { "corps", "core.corps", effect_detail::K::corps, "node.corps", "node.corps.description", "Corps Resonant Body", effect_detail::effects, EffectGroup::filter, 0xffd09060u, effect_detail::corpsParameters },
    { "peigne", "core.peigne", effect_detail::K::peigne, "node.peigne", "node.peigne.description", "Peigne Comb Filter", effect_detail::effects, EffectGroup::filter, 0xff70e0d0u, effect_detail::peigneParameters },
    { "utilitaire", "core.utilitaire", effect_detail::K::utilitaire, "node.utilitaire", "node.utilitaire.description", "Utilitaire", effect_detail::utilities, EffectGroup::channels, 0xff9aa8b8u, effect_detail::utilitaireParameters },
    { "stereoscope", "core.stereoscope", effect_detail::K::stereoscope, "node.stereoscope", "node.stereoscope.description", "Stereoscope", effect_detail::analyzers, EffectGroup::analysis, 0xff63c6ffu, effect_detail::stereoscopeParameters },
    // The MIDI effects.
    { "arpege", "core.arpege", effect_detail::K::arpege, "node.arpege", "node.arpege.description", "Arpeggiator", "nodes.category.midi", EffectGroup::midi, 0xffd47be8u, effect_detail::arpegeParameters, 2, 2, false, true },
    { "accords", "core.accords", effect_detail::K::accords, "node.accords", "node.accords.description", "Chords", "nodes.category.midi", EffectGroup::midi, 0xffcf78ddu, effect_detail::accordsParameters, 2, 2, false, true },
    { "gamme", "core.gamme", effect_detail::K::gamme, "node.gamme", "node.gamme.description", "Scale", "nodes.category.midi", EffectGroup::midi, 0xffcf78ddu, effect_detail::gammeParameters, 2, 2, false, true },
    { "echo-notes", "core.echo-notes", effect_detail::K::echoNotes, "node.echoNotes", "node.echoNotes.description", "Note Echo", "nodes.category.midi", EffectGroup::midi, 0xffcf78ddu, effect_detail::echoNotesParameters, 2, 2, false, true },
    { "alea", "core.alea", effect_detail::K::alea, "node.alea", "node.alea.description", "Random", "nodes.category.midi", EffectGroup::midi, 0xffcf78ddu, effect_detail::aleaParameters, 2, 2, false, true },
    { "duree", "core.duree", effect_detail::K::duree, "node.duree", "node.duree.description", "Note Length", "nodes.category.midi", EffectGroup::midi, 0xffcf78ddu, effect_detail::dureeParameters, 2, 2, false, true },
    { "sequenceur", "core.sequenceur", effect_detail::K::sequenceur, "node.sequenceur", "node.sequenceur.description", "Step Sequencer", "nodes.category.midi", EffectGroup::midi, 0xffd47be8u, effect_detail::sequenceurParameters, 2, 2, false, true },
    { "roulement", "core.roulement", effect_detail::K::roulement, "node.roulement", "node.roulement.description", "Note Repeat", "nodes.category.midi", EffectGroup::midi, 0xffcf78ddu, effect_detail::roulementParameters, 2, 2, false, true },
    // The modulators.
    { "mod-lfo", "core.mod-lfo", effect_detail::K::modLfo, "node.modLfo", "node.modLfo.description", "LFO", effect_detail::modulators, EffectGroup::control, 0xff5fb8ffu, effect_detail::modLfoParameters, 2, 2, false, false, true },
    { "mod-steps", "core.mod-steps", effect_detail::K::modSteps, "node.modSteps", "node.modSteps.description", "Steps", effect_detail::modulators, EffectGroup::control, 0xff5fd0c4u, effect_detail::modStepsParameters, 2, 2, false, false, true },
    { "mod-random", "core.mod-random", effect_detail::K::modRandom, "node.modRandom", "node.modRandom.description", "Random", effect_detail::modulators, EffectGroup::control, 0xff9b8cf2u, effect_detail::modRandomParameters, 2, 2, false, false, true },
    { "mod-adsr", "core.mod-adsr", effect_detail::K::modAdsr, "node.modAdsr", "node.modAdsr.description", "ADSR", effect_detail::modulators, EffectGroup::control, 0xff65d6b1u, effect_detail::modAdsrParameters, 2, 2, false, false, true },
    { "mod-note", "core.mod-note", effect_detail::K::modNote, "node.modNote", "node.modNote.description", "Keytrack", effect_detail::modulators, EffectGroup::control, 0xffd47be8u, effect_detail::modNoteParameters, 2, 2, false, false, true },
    { "mod-expression", "core.mod-expression", effect_detail::K::modExpression, "node.modExpression", "node.modExpression.description", "Expressions", effect_detail::modulators, EffectGroup::control, 0xffcf78ddu, effect_detail::modExpressionParameters, 2, 2, false, false, true },
    { "mod-macro", "core.mod-macro", effect_detail::K::modMacro, "node.modMacro", "node.modMacro.description", "Macro", effect_detail::modulators, EffectGroup::control, 0xfff2a33au, effect_detail::modMacroParameters, 2, 2, false, false, true },
    { "mod-button", "core.mod-button", effect_detail::K::modButton, "node.modButton", "node.modButton.description", "Button", effect_detail::modulators, EffectGroup::control, 0xffff7f6bu, effect_detail::modButtonParameters, 2, 2, false, false, true },
    { "mod-math", "core.mod-math", effect_detail::K::modMath, "node.modMath", "node.modMath.description", "Math", effect_detail::modulators, EffectGroup::control, 0xff9aa8b8u, effect_detail::modMathParameters, 2, 2, false, false, true },
    { "mod-follower", "core.mod-follower", effect_detail::K::modFollower, "node.modFollower", "node.modFollower.description", "Envelope Follower", effect_detail::modulators, EffectGroup::control, 0xff7ed878u, effect_detail::modFollowerParameters, 2, 2, false, false, true },
    // The containers.
    { "fx-chain", "core.fx-chain", effect_detail::K::fxChain, "node.fxChain", "node.fxChain.description", "Chain", effect_detail::containers, EffectGroup::container, 0xffe0b25au, effect_detail::fxChainParameters, 2, 2, false, false, false, true },
    { "fx-layer", "core.fx-layer", effect_detail::K::fxLayer, "node.fxLayer", "node.fxLayer.description", "FX Layer", effect_detail::containers, EffectGroup::container, 0xffe0b25au, effect_detail::fxLayerParameters, 2, 2, false, false, false, true },
    { "fx-selector", "core.fx-selector", effect_detail::K::fxSelector, "node.fxSelector", "node.fxSelector.description", "FX Selector", effect_detail::containers, EffectGroup::container, 0xffe0b25au, effect_detail::fxSelectorParameters, 2, 2, false, false, false, true },
    { "mid-side-split", "core.mid-side-split", effect_detail::K::midSideSplit, "node.midSideSplit", "node.midSideSplit.description", "Mid-Side Split", effect_detail::containers, EffectGroup::container, 0xffe0b25au, effect_detail::midSideParameters, 2, 2, false, false, false, true },
    { "stereo-split", "core.stereo-split", effect_detail::K::stereoSplit, "node.stereoSplit", "node.stereoSplit.description", "Stereo Split", effect_detail::containers, EffectGroup::container, 0xffe0b25au, effect_detail::stereoSplitParameters, 2, 2, false, false, false, true },
    { "multiband-2", "core.multiband-2", effect_detail::K::multiband2, "node.multiband2", "node.multiband2.description", "Multiband FX-2", effect_detail::containers, EffectGroup::container, 0xffe0b25au, effect_detail::multiband2Parameters, 2, 2, false, false, false, true },
    { "multiband-3", "core.multiband-3", effect_detail::K::multiband3, "node.multiband3", "node.multiband3.description", "Multiband FX-3", effect_detail::containers, EffectGroup::container, 0xffe0b25au, effect_detail::multiband3Parameters, 2, 2, false, false, false, true },
    { "xy-fx", "core.xy-fx", effect_detail::K::xyFx, "node.xyFx", "node.xyFx.description", "XY FX", effect_detail::containers, EffectGroup::container, 0xffe0b25au, effect_detail::xyFxParameters, 2, 2, false, false, false, true },
    { "replacer", "core.replacer", effect_detail::K::replacer, "node.replacer", "node.replacer.description", "Replacer", effect_detail::containers, EffectGroup::container, 0xffe0b25au, effect_detail::replacerParameters, 2, 2, false, false, false, true },
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
