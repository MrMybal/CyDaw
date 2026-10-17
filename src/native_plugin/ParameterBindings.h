#pragma once
#include <cydaw/engine/NativeDspGraph.h>
#include <cydaw/engine/NativeDspParameters.h>
#include <cydaw/engine/PolySynthGraph.h>
#include <algorithm>
#include <optional>
#include <string>
#include <cmath>
namespace cydaw::native
{
template <class T> T limit(T minimum, T maximum, T value)
{
    return std::clamp(value, minimum, maximum);
}
enum class NativeParameterField : std::uint8_t
{
    driveDb,
    gainDb,
    lowpassCutoff,
    lowpassResonance,
    highpassCutoff,
    clipThreshold,
    stereoWidth,
    pan,
    oscillatorFrequency,
    noiseLevel,
    delayTime,
    delayFeedback,
    mix,
    roomSize,
    compressorThreshold,
    compressorRatio,
    tone,
    modulationRate,
    modulationDepth,
    modulationOffset,
    modulationShape,
    valueOperand,
    valueMinimum,
    valueMaximum,
    mapInputMinimum,
    mapInputMaximum,
    mapOutputMinimum,
    mapOutputMaximum,
    followerAttack,
    followerRelease,
    bandFrequency,
    bandGain,
    bandQ,
    wavetablePosition,
    wavetableDetune,
    stringDamping,
    stringDecay,
    effect1,
    effect2,
    effect3,
    effect4,
    effect5,
    effect6,
    effect7,
    effect8,
    // An effect's values kept in slots (the equaliser's bands, the studio
    // effects' styles and options), by slot.
    effectSlotFirst,
    effectSlotLast = effectSlotFirst + 95
};
static_assert(static_cast<int>(NativeParameterField::effectSlotLast) <= 255);

enum class InstrumentParameterField : std::uint16_t
{
    none,
    waveform,
    wavetablePosition,
    wavetableDetune,
    stringDamping,
    stringDecay,
    oscillatorLevel,
    attackMilliseconds,
    decayMilliseconds,
    sustainLevel,
    releaseMilliseconds,
    filterCutoff,
    filterResonance,
    transpose,
    minimumNote,
    maximumNote,
    velocityScale,
    // A sound model's level, then its settings (SoundModelSetting order).
    modelLevel,
    modelSettingFirst,
    modelSettingLast = modelSettingFirst + 15,
    // A sampler's level, then its settings (SamplerSetting order).
    samplerLevel,
    samplerSettingFirst,
    samplerSettingLast = samplerSettingFirst + 31,
    // Prisme's settings (prism::parameter order).
    prismFirst,
    prismLast = prismFirst + cydaw::engine::prism::parameterCount - 1,
    // The voice allocator's playing and glide.
    voiceMode,
    glide,
    // Kick's settings (kick::parameter order).
    kickFirst,
    kickLast = kickFirst + cydaw::engine::kick::parameterCount - 1,
    // A synth of the family's (its model's order).
    synthFirst,
    synthLast = synthFirst + cydaw::engine::synth::valueCapacity - 1,
    // A Batterie's mix (DrumKit.h's order).
    drumFirst,
    drumLast = drumFirst + cydaw::engine::drum::valueCount - 1
};

std::optional<NativeParameterField> fieldForBinding(const std::string &binding)
{
    const auto endsWith = [&binding](const char *suffix) {
        const auto length = std::char_traits<char>::length(suffix);
        return binding.size() >= length && binding.compare(binding.size() - length, length, suffix) == 0;
    };
    if (endsWith("drive.db"))
        return NativeParameterField::driveDb;
    if (endsWith("gain.db"))
        return NativeParameterField::gainDb;
    if (endsWith("lowpass.cutoff") || endsWith("voiceFilter.cutoff"))
        return NativeParameterField::lowpassCutoff;
    if (endsWith("lowpass.resonance") || endsWith("voiceFilter.resonance"))
        return NativeParameterField::lowpassResonance;
    if (endsWith("highpass.cutoff"))
        return NativeParameterField::highpassCutoff;
    if (endsWith("clip.threshold"))
        return NativeParameterField::clipThreshold;
    if (endsWith("stereo.width"))
        return NativeParameterField::stereoWidth;
    if (endsWith("pan.value"))
        return NativeParameterField::pan;
    if (endsWith("oscillator.frequency"))
        return NativeParameterField::oscillatorFrequency;
    if (endsWith("noise.level"))
        return NativeParameterField::noiseLevel;
    if (endsWith("delay.time"))
        return NativeParameterField::delayTime;
    if (endsWith("delay.feedback"))
        return NativeParameterField::delayFeedback;
    if (endsWith("delay.mix") || endsWith("reverb.mix"))
        return NativeParameterField::mix;
    if (endsWith("reverb.roomSize"))
        return NativeParameterField::roomSize;
    if (endsWith("compressor.threshold"))
        return NativeParameterField::compressorThreshold;
    if (endsWith("compressor.ratio"))
        return NativeParameterField::compressorRatio;
    if (endsWith("eq.tone"))
        return NativeParameterField::tone;
    if (endsWith("lfo.rate"))
        return NativeParameterField::modulationRate;
    if (endsWith("lfo.depth") || endsWith("valueMath.amount"))
        return NativeParameterField::modulationDepth;
    if (endsWith("lfo.offset") || endsWith("valueMath.offset"))
        return NativeParameterField::modulationOffset;
    if (endsWith("lfo.shape"))
        return NativeParameterField::modulationShape;
    if (endsWith("macro.value") || endsWith("value.constant") || endsWith("value.operand"))
        return NativeParameterField::valueOperand;
    if (endsWith("value.minimum"))
        return NativeParameterField::valueMinimum;
    if (endsWith("value.maximum"))
        return NativeParameterField::valueMaximum;
    if (endsWith("map.inputMinimum"))
        return NativeParameterField::mapInputMinimum;
    if (endsWith("map.inputMaximum"))
        return NativeParameterField::mapInputMaximum;
    if (endsWith("map.outputMinimum"))
        return NativeParameterField::mapOutputMinimum;
    if (endsWith("map.outputMaximum"))
        return NativeParameterField::mapOutputMaximum;
    if (endsWith("follower.attack"))
        return NativeParameterField::followerAttack;
    if (endsWith("follower.release"))
        return NativeParameterField::followerRelease;
    if (endsWith("eqBand.frequency"))
        return NativeParameterField::bandFrequency;
    if (endsWith("eqBand.gain"))
        return NativeParameterField::bandGain;
    if (endsWith("eqBand.q"))
        return NativeParameterField::bandQ;
    if (endsWith("wavetable.frequency") || endsWith("string.frequency"))
        return NativeParameterField::oscillatorFrequency;
    if (endsWith("wavetable.position"))
        return NativeParameterField::wavetablePosition;
    if (endsWith("wavetable.detune"))
        return NativeParameterField::wavetableDetune;
    if (endsWith("string.damping"))
        return NativeParameterField::stringDamping;
    if (endsWith("string.decay"))
        return NativeParameterField::stringDecay;
    // The effect catalogue's bindings, by their last two names.
    const auto last = binding.rfind('.');
    const auto previous = last == std::string::npos || last == 0 ? std::string::npos : binding.rfind('.', last - 1);
    if (previous != std::string::npos)
        if (const auto *effect =
                cydaw::engine::effectParameterForSuffix(std::string_view(binding).substr(previous + 1)))
        {
            using P = cydaw::engine::NativeDspNodeParameters;
            static constexpr float P::*fields[]{&P::effect1, &P::effect2, &P::effect3, &P::effect4,
                                                &P::effect5, &P::effect6, &P::effect7, &P::effect8};
            for (std::size_t index = 0; index < std::size(fields); ++index)
                if (fields[index] == effect->field)
                    return static_cast<NativeParameterField>(static_cast<int>(NativeParameterField::effect1) +
                                                             static_cast<int>(index));
            if (effect->field == nullptr && effect->slot >= 0 &&
                effect->slot <= static_cast<int>(NativeParameterField::effectSlotLast) -
                                    static_cast<int>(NativeParameterField::effectSlotFirst))
                return static_cast<NativeParameterField>(static_cast<int>(NativeParameterField::effectSlotFirst) +
                                                         effect->slot);
        }
    return {};
}

InstrumentParameterField instrumentFieldForBinding(const std::string &binding)
{
    const auto endsWith = [&binding](const char *suffix) {
        const auto length = std::char_traits<char>::length(suffix);
        return binding.size() >= length && binding.compare(binding.size() - length, length, suffix) == 0;
    };
    // Prisme's first (its keys end like other cards' bindings): "….prism.<key>".
    if (const auto at = binding.find(".prism."); at != std::string::npos || binding.rfind("prism.", 0) == 0)
    {
        const auto key = at != std::string::npos ? binding.substr(at + 7) : binding.substr(6);
        if (const auto index = cydaw::engine::prism::parameterIndex(key); index >= 0)
            return static_cast<InstrumentParameterField>(static_cast<int>(InstrumentParameterField::prismFirst) +
                                                         index);
    }
    // A Batterie's: "….batterie.<key>".
    if (const auto at = binding.find(".batterie."); at != std::string::npos || binding.rfind("batterie.", 0) == 0)
    {
        const auto key = at != std::string::npos ? binding.substr(at + 10) : binding.substr(9);
        if (const auto index = cydaw::engine::drum::parameterIndex(key); index >= 0)
            return static_cast<InstrumentParameterField>(static_cast<int>(InstrumentParameterField::drumFirst) + index);
    }
    // A synth of the family: "….<prefix>.<key>", its model by the prefix.
    for (int modelIndex = 1; modelIndex < cydaw::engine::synth::modelCount; ++modelIndex)
    {
        const auto model = static_cast<cydaw::engine::synth::Model>(modelIndex);
        const auto &prefix = cydaw::engine::synth::info(model).prefix;
        if (prefix.empty())
            continue;
        const auto dotted = "." + prefix + ".";
        const auto at = binding.find(dotted);
        const auto leading = binding.rfind(prefix + ".", 0) == 0;
        if (at == std::string::npos && !leading)
            continue;
        const auto key =
            at != std::string::npos ? binding.substr(at + dotted.size()) : binding.substr(prefix.size() + 1);
        if (const auto index = cydaw::engine::synth::parameterIndex(model, key); index >= 0)
            return static_cast<InstrumentParameterField>(static_cast<int>(InstrumentParameterField::synthFirst) +
                                                         index);
    }
    if (const auto at = binding.find(".kick."); at != std::string::npos || binding.rfind("kick.", 0) == 0)
    {
        const auto key = at != std::string::npos ? binding.substr(at + 6) : binding.substr(5);
        if (const auto index = cydaw::engine::kick::parameterIndex(key); index >= 0)
            return static_cast<InstrumentParameterField>(static_cast<int>(InstrumentParameterField::kickFirst) + index);
    }
    if (endsWith("wavetable.position"))
        return InstrumentParameterField::wavetablePosition;
    if (endsWith("wavetable.detune"))
        return InstrumentParameterField::wavetableDetune;
    if (endsWith("wavetable.level"))
        return InstrumentParameterField::oscillatorLevel;
    if (endsWith("string.damping"))
        return InstrumentParameterField::stringDamping;
    if (endsWith("string.decay"))
        return InstrumentParameterField::stringDecay;
    if (endsWith("string.level"))
        return InstrumentParameterField::oscillatorLevel;
    if (endsWith("voice.mode"))
        return InstrumentParameterField::voiceMode;
    if (endsWith("voice.glide"))
        return InstrumentParameterField::glide;
    if (endsWith("polyOscillator.waveform"))
        return InstrumentParameterField::waveform;
    if (endsWith("polyOscillator.level"))
        return InstrumentParameterField::oscillatorLevel;
    if (endsWith("envelope.attack"))
        return InstrumentParameterField::attackMilliseconds;
    if (endsWith("envelope.decay"))
        return InstrumentParameterField::decayMilliseconds;
    if (endsWith("envelope.sustain"))
        return InstrumentParameterField::sustainLevel;
    if (endsWith("envelope.release"))
        return InstrumentParameterField::releaseMilliseconds;
    if (endsWith("voiceFilter.cutoff"))
        return InstrumentParameterField::filterCutoff;
    if (endsWith("voiceFilter.resonance"))
        return InstrumentParameterField::filterResonance;
    if (endsWith("midi.transpose"))
        return InstrumentParameterField::transpose;
    if (endsWith("midi.note.minimum"))
        return InstrumentParameterField::minimumNote;
    if (endsWith("midi.note.maximum"))
        return InstrumentParameterField::maximumNote;
    if (endsWith("midi.velocity"))
        return InstrumentParameterField::velocityScale;
    if (endsWith("model.level"))
        return InstrumentParameterField::modelLevel;
    for (std::size_t index = 0; index < cydaw::engine::soundModelSettingCount; ++index)
        if (endsWith(std::string(cydaw::engine::soundModelSettings[index].suffix).c_str()))
            return static_cast<InstrumentParameterField>(static_cast<int>(InstrumentParameterField::modelSettingFirst) +
                                                         static_cast<int>(index));
    if (endsWith("sampler.level"))
        return InstrumentParameterField::samplerLevel;
    for (std::size_t index = 0; index < cydaw::engine::samplerSettingCount; ++index)
        if (endsWith(std::string(cydaw::engine::samplerSettings[index].suffix).c_str()))
            return static_cast<InstrumentParameterField>(
                static_cast<int>(InstrumentParameterField::samplerSettingFirst) + static_cast<int>(index));
    return InstrumentParameterField::none;
}

void applyParameter(cydaw::engine::NativeDspNodeParameters &target, const NativeParameterField field,
                    const float value) noexcept
{
    switch (field)
    {
    case NativeParameterField::driveDb:
        target.driveDb = value;
        break;
    case NativeParameterField::gainDb:
        target.gainDb = value;
        break;
    case NativeParameterField::lowpassCutoff:
        target.cutoffHz = value;
        break;
    case NativeParameterField::lowpassResonance:
        target.resonance = value;
        break;
    case NativeParameterField::highpassCutoff:
        target.highpassCutoffHz = value;
        break;
    case NativeParameterField::clipThreshold:
        target.clipThreshold = value;
        break;
    case NativeParameterField::stereoWidth:
        target.stereoWidth = value;
        break;
    case NativeParameterField::pan:
        target.pan = value;
        break;
    case NativeParameterField::oscillatorFrequency:
        target.oscillatorFrequencyHz = value;
        break;
    case NativeParameterField::noiseLevel:
        target.noiseLevel = value;
        break;
    case NativeParameterField::delayTime:
        target.delayMilliseconds = value;
        break;
    case NativeParameterField::delayFeedback:
        target.feedback = value;
        break;
    case NativeParameterField::mix:
        target.mix = value;
        break;
    case NativeParameterField::roomSize:
        target.roomSize = value;
        break;
    case NativeParameterField::compressorThreshold:
        target.thresholdDb = value;
        break;
    case NativeParameterField::compressorRatio:
        target.ratio = value;
        break;
    case NativeParameterField::tone:
        target.tone = value;
        break;
    case NativeParameterField::bandFrequency:
        target.bandFrequencyHz = value;
        break;
    case NativeParameterField::bandGain:
        target.bandGainDb = value;
        break;
    case NativeParameterField::bandQ:
        target.bandQ = value;
        break;
    case NativeParameterField::wavetablePosition:
        target.wavetablePosition = value;
        break;
    case NativeParameterField::wavetableDetune:
        target.wavetableDetuneCents = value;
        break;
    case NativeParameterField::stringDamping:
        target.stringDamping = value;
        break;
    case NativeParameterField::stringDecay:
        target.stringDecaySeconds = value;
        break;
    case NativeParameterField::modulationRate:
        target.modulationRateHz = value;
        break;
    case NativeParameterField::modulationDepth:
        target.modulationDepth = value;
        break;
    case NativeParameterField::modulationOffset:
        target.modulationOffset = value;
        break;
    case NativeParameterField::modulationShape:
        target.modulationShape = value;
        break;
    case NativeParameterField::valueOperand:
        target.valueOperand = value;
        target.valueInputB = value;
        break;
    case NativeParameterField::valueMinimum:
        target.valueMinimum = value;
        break;
    case NativeParameterField::valueMaximum:
        target.valueMaximum = value;
        break;
    case NativeParameterField::mapInputMinimum:
        target.mapInputMinimum = value;
        break;
    case NativeParameterField::mapInputMaximum:
        target.mapInputMaximum = value;
        break;
    case NativeParameterField::mapOutputMinimum:
        target.mapOutputMinimum = value;
        break;
    case NativeParameterField::mapOutputMaximum:
        target.mapOutputMaximum = value;
        break;
    case NativeParameterField::followerAttack:
        target.followerAttackSeconds = value;
        break;
    case NativeParameterField::followerRelease:
        target.followerReleaseSeconds = value;
        break;
    case NativeParameterField::effect1:
        target.effect1 = value;
        break;
    case NativeParameterField::effect2:
        target.effect2 = value;
        break;
    case NativeParameterField::effect3:
        target.effect3 = value;
        break;
    case NativeParameterField::effect4:
        target.effect4 = value;
        break;
    case NativeParameterField::effect5:
        target.effect5 = value;
        break;
    case NativeParameterField::effect6:
        target.effect6 = value;
        break;
    case NativeParameterField::effect7:
        target.effect7 = value;
        break;
    case NativeParameterField::effect8:
        target.effect8 = value;
        break;
    default:
        if (field >= NativeParameterField::effectSlotFirst && field <= NativeParameterField::effectSlotLast)
            target.eqBands[static_cast<std::size_t>(static_cast<int>(field) -
                                                    static_cast<int>(NativeParameterField::effectSlotFirst))] = value;
        break;
    }
}

float parameterValue(const cydaw::engine::NativeDspNodeParameters &source, const NativeParameterField field) noexcept
{
    switch (field)
    {
    case NativeParameterField::driveDb:
        return source.driveDb;
    case NativeParameterField::gainDb:
        return source.gainDb;
    case NativeParameterField::lowpassCutoff:
        return source.cutoffHz;
    case NativeParameterField::lowpassResonance:
        return source.resonance;
    case NativeParameterField::highpassCutoff:
        return source.highpassCutoffHz;
    case NativeParameterField::clipThreshold:
        return source.clipThreshold;
    case NativeParameterField::stereoWidth:
        return source.stereoWidth;
    case NativeParameterField::pan:
        return source.pan;
    case NativeParameterField::oscillatorFrequency:
        return source.oscillatorFrequencyHz;
    case NativeParameterField::noiseLevel:
        return source.noiseLevel;
    case NativeParameterField::delayTime:
        return source.delayMilliseconds;
    case NativeParameterField::delayFeedback:
        return source.feedback;
    case NativeParameterField::mix:
        return source.mix;
    case NativeParameterField::roomSize:
        return source.roomSize;
    case NativeParameterField::compressorThreshold:
        return source.thresholdDb;
    case NativeParameterField::compressorRatio:
        return source.ratio;
    case NativeParameterField::tone:
        return source.tone;
    case NativeParameterField::bandFrequency:
        return source.bandFrequencyHz;
    case NativeParameterField::bandGain:
        return source.bandGainDb;
    case NativeParameterField::bandQ:
        return source.bandQ;
    case NativeParameterField::wavetablePosition:
        return source.wavetablePosition;
    case NativeParameterField::wavetableDetune:
        return source.wavetableDetuneCents;
    case NativeParameterField::stringDamping:
        return source.stringDamping;
    case NativeParameterField::stringDecay:
        return source.stringDecaySeconds;
    case NativeParameterField::modulationRate:
        return source.modulationRateHz;
    case NativeParameterField::modulationDepth:
        return source.modulationDepth;
    case NativeParameterField::modulationOffset:
        return source.modulationOffset;
    case NativeParameterField::modulationShape:
        return source.modulationShape;
    case NativeParameterField::valueOperand:
        return source.valueOperand;
    case NativeParameterField::valueMinimum:
        return source.valueMinimum;
    case NativeParameterField::valueMaximum:
        return source.valueMaximum;
    case NativeParameterField::mapInputMinimum:
        return source.mapInputMinimum;
    case NativeParameterField::mapInputMaximum:
        return source.mapInputMaximum;
    case NativeParameterField::mapOutputMinimum:
        return source.mapOutputMinimum;
    case NativeParameterField::mapOutputMaximum:
        return source.mapOutputMaximum;
    case NativeParameterField::followerAttack:
        return source.followerAttackSeconds;
    case NativeParameterField::followerRelease:
        return source.followerReleaseSeconds;
    case NativeParameterField::effect1:
        return source.effect1;
    case NativeParameterField::effect2:
        return source.effect2;
    case NativeParameterField::effect3:
        return source.effect3;
    case NativeParameterField::effect4:
        return source.effect4;
    case NativeParameterField::effect5:
        return source.effect5;
    case NativeParameterField::effect6:
        return source.effect6;
    case NativeParameterField::effect7:
        return source.effect7;
    case NativeParameterField::effect8:
        return source.effect8;
    default:
        if (field >= NativeParameterField::effectSlotFirst && field <= NativeParameterField::effectSlotLast)
            return source.eqBands[static_cast<std::size_t>(static_cast<int>(field) -
                                                           static_cast<int>(NativeParameterField::effectSlotFirst))];
        break;
    }
    return 0.0f;
}

void applyInstrumentParameter(cydaw::engine::PolySynthGraphPlan &target, const InstrumentParameterField field,
                              const float value) noexcept
{
    using Waveform = cydaw::engine::PolyOscillatorWaveform;
    switch (field)
    {
    case InstrumentParameterField::wavetablePosition:
        target.wavetablePosition = std::clamp(value, 0.0f, 1.0f);
        break;
    case InstrumentParameterField::wavetableDetune:
        target.wavetableDetuneCents = std::clamp(value, 0.0f, 50.0f);
        break;
    case InstrumentParameterField::stringDamping:
        target.stringDamping = std::clamp(value, 0.0f, 1.0f);
        break;
    case InstrumentParameterField::stringDecay:
        target.stringDecaySeconds = std::max(.01f, value);
        break;
    case InstrumentParameterField::none:
        break;
    case InstrumentParameterField::waveform:
        // Two modes were appended to the waveform enum; a clamp that still
        // stopped at the fourth turned a wavetable instrument into a sine.
        target.waveform =
            static_cast<Waveform>(limit(0, static_cast<int>(Waveform::kick), static_cast<int>(std::lround(value))));
        break;
    case InstrumentParameterField::oscillatorLevel:
        target.oscillatorLevel = limit(0.0f, cydaw::engine::playsRecordingLevel(target.waveform) ? 1.0f : 0.5f, value);
        break;
    case InstrumentParameterField::modelLevel:
    case InstrumentParameterField::samplerLevel:
        target.oscillatorLevel = limit(0.0f, 1.0f, value);
        break;
    case InstrumentParameterField::attackMilliseconds:
        target.attackSeconds = limit(0.0001f, 5.0f, value * 0.001f);
        break;
    case InstrumentParameterField::decayMilliseconds:
        target.decaySeconds = limit(0.0001f, 5.0f, value * 0.001f);
        break;
    case InstrumentParameterField::sustainLevel:
        target.sustainLevel = limit(0.0f, 1.0f, value);
        break;
    case InstrumentParameterField::releaseMilliseconds:
        target.releaseSeconds = limit(0.0001f, 10.0f, value * 0.001f);
        break;
    case InstrumentParameterField::filterCutoff:
        target.filterCutoffHz = limit(20.0f, 20'000.0f, value);
        break;
    case InstrumentParameterField::filterResonance:
        target.filterResonance = limit(0.1f, 20.0f, value);
        break;
    case InstrumentParameterField::transpose:
        target.transposeSemitones = limit(-96, 96, static_cast<int>(std::lround(value)));
        break;
    case InstrumentParameterField::minimumNote:
        target.minimumMidiNote = limit(0, 127, static_cast<int>(std::lround(value)));
        break;
    case InstrumentParameterField::maximumNote:
        target.maximumMidiNote = limit(target.minimumMidiNote, 127, static_cast<int>(std::lround(value)));
        break;
    case InstrumentParameterField::velocityScale:
        target.velocityScale = limit(0.0f, 4.0f, value);
        break;
    case InstrumentParameterField::voiceMode:
        target.voiceMode = limit(0, 2, static_cast<int>(std::lround(value)));
        break;
    case InstrumentParameterField::glide:
        target.glideSeconds = limit(0.0f, 2.0f, value / 1'000.0f);
        break;
    default:
        if (field >= InstrumentParameterField::modelSettingFirst && field <= InstrumentParameterField::modelSettingLast)
        {
            const auto index = static_cast<std::size_t>(static_cast<int>(field) -
                                                        static_cast<int>(InstrumentParameterField::modelSettingFirst));
            if (index < cydaw::engine::soundModelSettingCount)
            {
                const auto &setting = cydaw::engine::soundModelSettings[index];
                target.soundModelValues[index] = limit(setting.minimum, setting.maximum, value);
            }
        }
        if (field >= InstrumentParameterField::prismFirst && field <= InstrumentParameterField::prismLast)
        {
            const auto index = static_cast<int>(field) - static_cast<int>(InstrumentParameterField::prismFirst);
            // A table is not changed here: its frames are built off this thread.
            const auto &parameter = cydaw::engine::prism::parameter(index);
            if (parameter.key != "a.table" && parameter.key != "b.table")
                target.prismValues[static_cast<std::size_t>(index)] = cydaw::engine::prism::clampValue(index, value);
        }
        if (field >= InstrumentParameterField::kickFirst && field <= InstrumentParameterField::kickLast)
        {
            const auto index = static_cast<int>(field) - static_cast<int>(InstrumentParameterField::kickFirst);
            target.kickValues[static_cast<std::size_t>(index)] = cydaw::engine::kick::clampValue(index, value);
        }
        if (field >= InstrumentParameterField::drumFirst && field <= InstrumentParameterField::drumLast)
        {
            const auto index = static_cast<int>(field) - static_cast<int>(InstrumentParameterField::drumFirst);
            target.drumValues[static_cast<std::size_t>(index)] = cydaw::engine::drum::clampValue(index, value);
        }
        if (field >= InstrumentParameterField::synthFirst && field <= InstrumentParameterField::synthLast &&
            target.synthModel != cydaw::engine::synth::Model::none)
        {
            const auto index = static_cast<int>(field) - static_cast<int>(InstrumentParameterField::synthFirst);
            if (index < cydaw::engine::synth::parameterCount(target.synthModel))
                target.synthValues[static_cast<std::size_t>(index)] =
                    cydaw::engine::synth::clampValue(target.synthModel, index, value);
        }
        if (field >= InstrumentParameterField::samplerSettingFirst &&
            field <= InstrumentParameterField::samplerSettingLast)
        {
            const auto index = static_cast<std::size_t>(
                static_cast<int>(field) - static_cast<int>(InstrumentParameterField::samplerSettingFirst));
            if (index < cydaw::engine::samplerSettingCount)
            {
                const auto &setting = cydaw::engine::samplerSettings[index];
                target.samplerValues[index] = limit(setting.minimum, setting.maximum, value);
            }
        }
        break;
    }
}

// What an instrument's plan holds in a field (the inverse of
// applyInstrumentParameter); none for a field it has no value for.
std::optional<float> instrumentParameterValue(const cydaw::engine::PolySynthGraphPlan &plan,
                                              const InstrumentParameterField field) noexcept
{
    switch (field)
    {
    case InstrumentParameterField::none:
        return std::nullopt;
    case InstrumentParameterField::waveform:
        return static_cast<float>(static_cast<int>(plan.waveform));
    case InstrumentParameterField::oscillatorLevel:
    case InstrumentParameterField::modelLevel:
    case InstrumentParameterField::samplerLevel:
        return plan.oscillatorLevel;
    case InstrumentParameterField::attackMilliseconds:
        return plan.attackSeconds * 1'000.0f;
    case InstrumentParameterField::decayMilliseconds:
        return plan.decaySeconds * 1'000.0f;
    case InstrumentParameterField::sustainLevel:
        return plan.sustainLevel;
    case InstrumentParameterField::releaseMilliseconds:
        return plan.releaseSeconds * 1'000.0f;
    case InstrumentParameterField::filterCutoff:
        return plan.filterCutoffHz;
    case InstrumentParameterField::filterResonance:
        return plan.filterResonance;
    case InstrumentParameterField::transpose:
        return static_cast<float>(plan.transposeSemitones);
    case InstrumentParameterField::minimumNote:
        return static_cast<float>(plan.minimumMidiNote);
    case InstrumentParameterField::maximumNote:
        return static_cast<float>(plan.maximumMidiNote);
    case InstrumentParameterField::velocityScale:
        return plan.velocityScale;
    case InstrumentParameterField::voiceMode:
        return static_cast<float>(plan.voiceMode);
    case InstrumentParameterField::glide:
        return plan.glideSeconds * 1'000.0f;
    default:
        break;
    }
    const auto at = [field](const InstrumentParameterField first) {
        return static_cast<std::size_t>(static_cast<int>(field) - static_cast<int>(first));
    };
    if (field >= InstrumentParameterField::modelSettingFirst && field <= InstrumentParameterField::modelSettingLast &&
        at(InstrumentParameterField::modelSettingFirst) < cydaw::engine::soundModelSettingCount)
        return plan.soundModelValues[at(InstrumentParameterField::modelSettingFirst)];
    if (field >= InstrumentParameterField::samplerSettingFirst &&
        field <= InstrumentParameterField::samplerSettingLast &&
        at(InstrumentParameterField::samplerSettingFirst) < cydaw::engine::samplerSettingCount)
        return plan.samplerValues[at(InstrumentParameterField::samplerSettingFirst)];
    if (field >= InstrumentParameterField::prismFirst && field <= InstrumentParameterField::prismLast)
        return plan.prismValues[at(InstrumentParameterField::prismFirst)];
    if (field >= InstrumentParameterField::kickFirst && field <= InstrumentParameterField::kickLast)
        return plan.kickValues[at(InstrumentParameterField::kickFirst)];
    if (field >= InstrumentParameterField::drumFirst && field <= InstrumentParameterField::drumLast)
        return plan.drumValues[at(InstrumentParameterField::drumFirst)];
    if (field >= InstrumentParameterField::synthFirst && field <= InstrumentParameterField::synthLast &&
        plan.synthModel != cydaw::engine::synth::Model::none &&
        static_cast<int>(at(InstrumentParameterField::synthFirst)) <
            cydaw::engine::synth::parameterCount(plan.synthModel))
        return plan.synthValues[at(InstrumentParameterField::synthFirst)];
    return std::nullopt;
}

} // namespace cydaw::native
