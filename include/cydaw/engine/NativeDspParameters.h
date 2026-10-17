#pragma once

#include "EffectCatalogue.h"
#include "NativeDspGraph.h"

#include <optional>
#include <string_view>

namespace cydaw::engine {

// Which field of a node's payload a given value-input port drives.
//
// This was written twice. The runtime kept one copy to bind a modulation cable
// to a parameter, above a comment calling itself "the single place that knows
// the key vocabulary"; the definition compiler kept another to apply a socket's
// authored default. Adding the peaking EQ, the wavetable and the string taught
// the registry about their ports and neither copy about their fields, so both
// answered "no such parameter" — which made a cable into an EQ band's frequency
// fail the whole graph compile, and dropped every authored default on a nested
// graph that used one. One map now answers for both.
//
// The keys are the ones controlParameterForPort() produces from a port key, so
// a primitive that declares a value input has to appear here to be drivable.
[[nodiscard]] constexpr float NativeDspNodeParameters::* nativeDspParameterField(
    const NativeDspNodeKind kind, const std::string_view key) noexcept
{
    using P = NativeDspNodeParameters;
    switch (kind) {
    case NativeDspNodeKind::gain:
        if (key == "gain-db") return &P::gainDb;
        return nullptr;
    case NativeDspNodeKind::lowpass:
        if (key == "cutoff") return &P::cutoffHz;
        if (key == "resonance") return &P::resonance;
        return nullptr;
    case NativeDspNodeKind::highpass:
        if (key == "cutoff") return &P::highpassCutoffHz;
        return nullptr;
    case NativeDspNodeKind::pan:
        if (key == "pan") return &P::pan;
        return nullptr;
    case NativeDspNodeKind::drive:
        if (key == "drive-db") return &P::driveDb;
        return nullptr;
    case NativeDspNodeKind::hardClip:
        if (key == "threshold") return &P::clipThreshold;
        return nullptr;
    case NativeDspNodeKind::stereoWidth:
        if (key == "width") return &P::stereoWidth;
        return nullptr;
    case NativeDspNodeKind::noise:
        if (key == "level") return &P::noiseLevel;
        return nullptr;
    case NativeDspNodeKind::delay:
        if (key == "time") return &P::delayMilliseconds;
        if (key == "feedback") return &P::feedback;
        if (key == "mix") return &P::mix;
        return nullptr;
    case NativeDspNodeKind::reverb:
    case NativeDspNodeKind::reverbRack:
        if (key == "room-size") return &P::roomSize;
        if (key == "mix") return &P::mix;
        return nullptr;
    case NativeDspNodeKind::compressor:
        if (key == "threshold") return &P::thresholdDb;
        if (key == "ratio") return &P::ratio;
        return nullptr;
    case NativeDspNodeKind::equalizer:
        if (key == "tone") return &P::tone;
        return nullptr;
    case NativeDspNodeKind::oscillator:
        if (key == "frequency") return &P::oscillatorFrequencyHz;
        if (key == "level") return &P::oscillatorLevel;
        return nullptr;
    case NativeDspNodeKind::peakingEq:
        if (key == "frequency") return &P::bandFrequencyHz;
        if (key == "gain") return &P::bandGainDb;
        if (key == "q") return &P::bandQ;
        return nullptr;
    case NativeDspNodeKind::wavetable:
        if (key == "frequency") return &P::oscillatorFrequencyHz;
        if (key == "position") return &P::wavetablePosition;
        if (key == "level") return &P::oscillatorLevel;
        if (key == "detune") return &P::wavetableDetuneCents;
        return nullptr;
    case NativeDspNodeKind::stringResonator:
        if (key == "frequency") return &P::oscillatorFrequencyHz;
        if (key == "damping") return &P::stringDamping;
        if (key == "decay") return &P::stringDecaySeconds;
        return nullptr;
    default:
        if (const auto* effect = effectForKind(kind))
            if (const auto* parameter = effectParameter(*effect, key))
                return parameter->field;
        return nullptr;
    }
}

// Which processor a primitive key compiles to.
//
// Kept beside the field map because the two answer halves of one question: a
// cable arrives naming a port, and only these together say what it drives.
[[nodiscard]] constexpr std::optional<NativeDspNodeKind>
nativeDspKindForProcessor(const std::string_view processorKey) noexcept
{
    using Kind = NativeDspNodeKind;
    if (processorKey == "core.drive") return Kind::drive;
    if (processorKey == "core.gain") return Kind::gain;
    if (processorKey == "core.lowpass") return Kind::lowpass;
    if (processorKey == "core.highpass") return Kind::highpass;
    if (processorKey == "core.hard-clip") return Kind::hardClip;
    if (processorKey == "core.stereo-width") return Kind::stereoWidth;
    if (processorKey == "core.pan") return Kind::pan;
    if (processorKey == "core.dc-blocker") return Kind::dcBlocker;
    if (processorKey == "core.fixed-oscillator") return Kind::oscillator;
    if (processorKey == "core.noise") return Kind::noise;
    if (processorKey == "core.delay") return Kind::delay;
    if (processorKey == "core.reverb") return Kind::reverb;
    if (processorKey == "core.compressor") return Kind::compressor;
    if (processorKey == "core.equalizer") return Kind::equalizer;
    if (processorKey == "core.peaking-eq") return Kind::peakingEq;
    if (processorKey == "core.wavetable") return Kind::wavetable;
    if (processorKey == "core.string") return Kind::stringResonator;
    if (processorKey == "core.audio-sum" || processorKey == "core.mixer-4")
        return Kind::mixer;
    if (processorKey == "core.spectrum-probe"
        || processorKey == "core.level-probe"
        || processorKey == "core.waveform-probe")
        return Kind::analyzer;
    if (processorKey == "core.envelope-follower")
        return Kind::envelopeFollower;
    if (processorKey == "core.container-end")
        return Kind::containerEnd;
    // A modulator is a control node: it has no sound to process.
    if (const auto* effect = effectForProcessor(processorKey); effect != nullptr && !effect->control)
        return effect->kind;
    return std::nullopt;
}

// The control-rate primitives, by processor key. This table lived inside the
// typed compiler; the graph builder that compiles an instrument's effect
// chain needs the same answers, so it moved here where both can ask.
[[nodiscard]] constexpr std::optional<NativeDspNodeKind>
nativeDspControlKindForProcessor(const std::string_view processorKey) noexcept
{
    using Kind = NativeDspNodeKind;
    if (processorKey == "core.constant" || processorKey == "core.macro")
        return Kind::controlConstant;
    if (processorKey == "core.lfo") return Kind::controlLfo;
    if (processorKey == "core.add") return Kind::controlAdd;
    if (processorKey == "core.subtract") return Kind::controlSubtract;
    if (processorKey == "core.multiply") return Kind::controlMultiply;
    if (processorKey == "core.divide") return Kind::controlDivide;
    if (processorKey == "core.modulo") return Kind::controlModulo;
    if (processorKey == "core.power") return Kind::controlPower;
    if (processorKey == "core.sine") return Kind::controlSine;
    if (processorKey == "core.cosine") return Kind::controlCosine;
    if (processorKey == "core.square-root") return Kind::controlSquareRoot;
    if (processorKey == "core.floor") return Kind::controlFloor;
    if (processorKey == "core.ceil") return Kind::controlCeil;
    if (processorKey == "core.round") return Kind::controlRound;
    if (processorKey == "core.minimum") return Kind::controlMinimum;
    if (processorKey == "core.maximum") return Kind::controlMaximum;
    if (processorKey == "core.clamp") return Kind::controlClamp;
    if (processorKey == "core.absolute") return Kind::controlAbsolute;
    if (processorKey == "core.negate") return Kind::controlNegate;
    if (processorKey == "core.map-range") return Kind::controlMapRange;
    if (processorKey == "core.scale-offset") return Kind::controlScaleOffset;
    if (processorKey == "core.compare-greater") return Kind::controlGreater;
    if (processorKey == "core.compare-less") return Kind::controlLess;
    if (processorKey == "core.compare-equal") return Kind::controlEqual;
    if (processorKey == "core.logic-and") return Kind::controlAnd;
    if (processorKey == "core.logic-or") return Kind::controlOr;
    if (processorKey == "core.logic-xor") return Kind::controlXor;
    if (processorKey == "core.logic-not") return Kind::controlNot;
    if (const auto* effect = effectForProcessor(processorKey); effect != nullptr && effect->control)
        return effect->kind;
    return std::nullopt;
}

// A catalogue parameter stored in the extended payload can also be driven
// by a typed value socket. Resolve its array index before audio processing.
[[nodiscard]] constexpr int nativeDspParameterSlot(NativeDspNodeKind kind,std::string_view key) noexcept
{
    if(const auto* effect=effectForKind(kind))if(const auto* p=effectParameter(*effect,key))return p->slot;
    return -1;
}

// Which parameter key a primitive's value-input port carries. Empty means the
// port drives nothing, which is what a socket on a control primitive does.
[[nodiscard]] constexpr std::string_view nativeDspParameterKeyForPort(
    const std::string_view processorKey,
    const std::string_view portKey) noexcept
{
    if (processorKey == "core.gain" && portKey == "gain") return "gain-db";
    if ((processorKey == "core.lowpass" || processorKey == "core.highpass")
        && portKey == "cutoff")
        return "cutoff";
    if (processorKey == "core.lowpass" && portKey == "resonance")
        return "resonance";
    if (processorKey == "core.peaking-eq"
        && (portKey == "frequency" || portKey == "gain" || portKey == "q"))
        return portKey;
    if ((processorKey == "core.wavetable" || processorKey == "core.string")
        && (portKey == "frequency" || portKey == "position"
            || portKey == "level" || portKey == "detune"
            || portKey == "damping" || portKey == "decay"))
        return portKey;
    if (processorKey == "core.pan" && portKey == "pan") return "pan";
    if (processorKey == "core.drive" && portKey == "drive") return "drive-db";
    if (processorKey == "core.hard-clip" && portKey == "threshold")
        return "threshold";
    if (processorKey == "core.stereo-width" && portKey == "width")
        return "width";
    if (processorKey == "core.fixed-oscillator"
        && (portKey == "frequency" || portKey == "level"))
        return portKey;
    if (processorKey == "core.noise" && portKey == "level") return "level";
    if (processorKey == "core.delay"
        && (portKey == "time" || portKey == "feedback" || portKey == "mix"))
        return portKey;
    if (processorKey == "core.reverb"
        && (portKey == "room-size" || portKey == "mix"))
        return portKey;
    if (processorKey == "core.compressor"
        && (portKey == "threshold" || portKey == "ratio"))
        return portKey;
    if (processorKey == "core.equalizer" && portKey == "tone") return "tone";
    // A catalogue effect's value port carries the parameter of its name.
    if (const auto* effect = effectForProcessor(processorKey))
        if (const auto* parameter = effectParameter(*effect, portKey))
            return parameter->key;
    return {};
}

} // namespace cydaw::engine
