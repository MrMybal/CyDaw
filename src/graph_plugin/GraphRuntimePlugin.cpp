#include "CyDawLookAndFeel.h"
#include <cydaw/engine/EffectCatalogue.h>
#include "GraphPolySynthRenderer.h"
#include "Localization.h"
#include "NativeDspGraphState.h"
#include "NativeDspRuntime.h"
#include "NodeGraphComponent.h"
#include "SamplerMapFile.h"
#include "SoundModelFile.h"

#include <CyDawGeneratedGraphConfig.h>
#include <cydaw/engine/AudioAnalysis.h>
#include <cydaw/realtime/RtPublisher.h>
#include <cydaw/realtime/SpscRing.h>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#if JUCE_WINDOWS
 #include "PluginInterface.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <vector>

namespace cydaw::graph_plugin {
namespace {

constexpr auto hostParameterSlotCount = std::size_t { 32 };

enum class NativeParameterField : std::uint8_t {
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

enum class InstrumentParameterField : std::uint16_t {
    none,
    waveform,
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

struct HostParameterMetadata {
    juce::String name;
    juce::String unit;
    float minimum {};
    float maximum { 1.0f };
    float defaultNormalised {};
    std::uint32_t stableGraphId {};
    bool active {};
    bool automatable {};
};

// A snapshot shared by the host's threads (never the audio callback):
// std::atomic<std::shared_ptr> where the standard library has it, a lock
// elsewhere (Apple's libc++).
template <typename T>
class SharedSnapshot final {
public:
#if defined(__cpp_lib_atomic_shared_ptr)
    void store(std::shared_ptr<const T> next) noexcept
    {
        value_.store(std::move(next), std::memory_order_release);
    }
    [[nodiscard]] std::shared_ptr<const T> load() const noexcept
    {
        return value_.load(std::memory_order_acquire);
    }

private:
    std::atomic<std::shared_ptr<const T>> value_;
#else
    void store(std::shared_ptr<const T> next)
    {
        const std::scoped_lock lock(mutex_);
        value_.swap(next);
    }
    [[nodiscard]] std::shared_ptr<const T> load() const
    {
        const std::scoped_lock lock(mutex_);
        return value_;
    }

private:
    mutable std::mutex mutex_;
    std::shared_ptr<const T> value_;
#endif
};

class GraphHostParameter final : public juce::AudioProcessorParameterWithID {
public:
    GraphHostParameter(const std::size_t slot,
                       const cydaw::generated_graph::ParameterConfig* config)
        : AudioProcessorParameterWithID(
              juce::ParameterID(
                  config != nullptr
                      ? "graph."
                            + juce::String::toHexString(
                                  static_cast<int>(config->stableId))
                                  .paddedLeft('0', 8)
                      : "graph.slot."
                            + juce::String(static_cast<int>(slot + 1))
                                  .paddedLeft('0', 2),
                  1),
              config != nullptr
                  ? juce::String(config->name)
                  : "Graph Parameter "
                        + juce::String(static_cast<int>(slot + 1)))
    {
        auto initial = std::make_shared<HostParameterMetadata>();
        initial->name = config != nullptr
            ? juce::String(config->name)
            : "Unassigned Graph Parameter "
                  + juce::String(static_cast<int>(slot + 1));
        if (config != nullptr) {
            initial->unit = config->unit;
            initial->minimum = config->minimum;
            initial->maximum = config->maximum;
            const auto extent = config->maximum - config->minimum;
            initial->defaultNormalised = extent > 0.0f
                ? juce::jlimit(0.0f, 1.0f,
                               (config->defaultValue - config->minimum)
                                   / extent)
                : 0.0f;
            initial->stableGraphId = config->stableId;
            initial->automatable = config->automatable;
            normalised_.store(initial->defaultNormalised,
                              std::memory_order_relaxed);
        }
        // The editable runtime uses fixed development slots. VST3 hosts cache
        // automation capabilities while the component is created, before a
        // saved Graph is restored, so every slot must advertise that it can be
        // automated from the beginning. Exact generated wrappers use the
        // per-parameter publication flags from the export manifest instead.
        initial->active = true;
        if (config == nullptr)
            initial->automatable = true;
        metadata_.store(std::move(initial));
    }

    float getValue() const override
    {
        return normalised_.load(std::memory_order_relaxed);
    }

    void setValue(const float value) override
    {
        normalised_.store(juce::jlimit(0.0f, 1.0f, value),
                          std::memory_order_relaxed);
    }

    float getDefaultValue() const override
    {
        return metadata()->defaultNormalised;
    }

    juce::String getName(const int maximumLength) const override
    {
        return metadata()->name.substring(0, maximumLength);
    }

    juce::String getLabel() const override { return metadata()->unit; }

    bool isAutomatable() const override
    {
        const auto current = metadata();
        return current->active && current->automatable;
    }

    juce::String getText(const float normalisedValue,
                         const int maximumLength) const override
    {
        const auto current = metadata();
        const auto actual = current->minimum
            + juce::jlimit(0.0f, 1.0f, normalisedValue)
                  * (current->maximum - current->minimum);
        return (juce::String(actual, std::abs(actual) >= 100.0f ? 1 : 3)
                + (current->unit.isNotEmpty() ? " " + current->unit : ""))
            .substring(0, maximumLength);
    }

    float getValueForText(const juce::String& text) const override
    {
        const auto current = metadata();
        const auto extent = current->maximum - current->minimum;
        return extent > 0.0f
            ? juce::jlimit(0.0f, 1.0f,
                           (text.getFloatValue() - current->minimum) / extent)
            : 0.0f;
    }

    void configure(HostParameterMetadata next, const bool resetValue)
    {
        auto shared = std::make_shared<HostParameterMetadata>(std::move(next));
        if (resetValue)
            normalised_.store(shared->defaultNormalised,
                              std::memory_order_relaxed);
        metadata_.store(std::move(shared));
    }

    [[nodiscard]] std::shared_ptr<const HostParameterMetadata> metadata() const
    {
        return metadata_.load();
    }

private:
    std::atomic<float> normalised_ {};
    // Deliberately not an RtPublisher: this is queried by the host from any of
    // its own threads and never from processBlock, so the multi-reader
    // reference count is the right trade-off here. Only the map the callback
    // actually reads is worth publishing lock-free.
    SharedSnapshot<HostParameterMetadata> metadata_;
};

std::optional<NativeParameterField> fieldForBinding(
    const std::string& binding)
{
    const auto endsWith = [&binding](const char* suffix) {
        const auto length = std::char_traits<char>::length(suffix);
        return binding.size() >= length
               && binding.compare(binding.size() - length, length, suffix) == 0;
    };
    if (endsWith("drive.db")) return NativeParameterField::driveDb;
    if (endsWith("gain.db")) return NativeParameterField::gainDb;
    if (endsWith("lowpass.cutoff") || endsWith("voiceFilter.cutoff"))
        return NativeParameterField::lowpassCutoff;
    if (endsWith("lowpass.resonance") || endsWith("voiceFilter.resonance"))
        return NativeParameterField::lowpassResonance;
    if (endsWith("highpass.cutoff")) return NativeParameterField::highpassCutoff;
    if (endsWith("clip.threshold")) return NativeParameterField::clipThreshold;
    if (endsWith("stereo.width")) return NativeParameterField::stereoWidth;
    if (endsWith("pan.value")) return NativeParameterField::pan;
    if (endsWith("oscillator.frequency"))
        return NativeParameterField::oscillatorFrequency;
    if (endsWith("noise.level")) return NativeParameterField::noiseLevel;
    if (endsWith("delay.time")) return NativeParameterField::delayTime;
    if (endsWith("delay.feedback")) return NativeParameterField::delayFeedback;
    if (endsWith("delay.mix") || endsWith("reverb.mix"))
        return NativeParameterField::mix;
    if (endsWith("reverb.roomSize")) return NativeParameterField::roomSize;
    if (endsWith("compressor.threshold"))
        return NativeParameterField::compressorThreshold;
    if (endsWith("compressor.ratio"))
        return NativeParameterField::compressorRatio;
    if (endsWith("eq.tone")) return NativeParameterField::tone;
    if (endsWith("lfo.rate")) return NativeParameterField::modulationRate;
    if (endsWith("lfo.depth") || endsWith("valueMath.amount"))
        return NativeParameterField::modulationDepth;
    if (endsWith("lfo.offset") || endsWith("valueMath.offset"))
        return NativeParameterField::modulationOffset;
    if (endsWith("lfo.shape")) return NativeParameterField::modulationShape;
    if (endsWith("macro.value") || endsWith("value.constant")
        || endsWith("value.operand"))
        return NativeParameterField::valueOperand;
    if (endsWith("value.minimum")) return NativeParameterField::valueMinimum;
    if (endsWith("value.maximum")) return NativeParameterField::valueMaximum;
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
    if (endsWith("eqBand.frequency")) return NativeParameterField::bandFrequency;
    if (endsWith("eqBand.gain")) return NativeParameterField::bandGain;
    if (endsWith("eqBand.q")) return NativeParameterField::bandQ;
    if (endsWith("wavetable.frequency") || endsWith("string.frequency"))
        return NativeParameterField::oscillatorFrequency;
    if (endsWith("wavetable.position"))
        return NativeParameterField::wavetablePosition;
    if (endsWith("wavetable.detune"))
        return NativeParameterField::wavetableDetune;
    if (endsWith("string.damping")) return NativeParameterField::stringDamping;
    if (endsWith("string.decay")) return NativeParameterField::stringDecay;
    // The effect catalogue's bindings, by their last two names.
    const auto last = binding.rfind('.');
    const auto previous = last == std::string::npos || last == 0 ? std::string::npos : binding.rfind('.', last - 1);
    if (previous != std::string::npos)
        if (const auto* effect = cydaw::engine::effectParameterForSuffix(std::string_view(binding).substr(previous + 1))) {
            using P = cydaw::engine::NativeDspNodeParameters;
            static constexpr float P::* fields[] { &P::effect1, &P::effect2, &P::effect3, &P::effect4,
                                                   &P::effect5, &P::effect6, &P::effect7, &P::effect8 };
            for (std::size_t index = 0; index < std::size(fields); ++index)
                if (fields[index] == effect->field)
                    return static_cast<NativeParameterField>(static_cast<int>(NativeParameterField::effect1) + static_cast<int>(index));
            if (effect->field == nullptr && effect->slot >= 0
                && effect->slot <= static_cast<int>(NativeParameterField::effectSlotLast) - static_cast<int>(NativeParameterField::effectSlotFirst))
                return static_cast<NativeParameterField>(static_cast<int>(NativeParameterField::effectSlotFirst) + effect->slot);
        }
    return {};
}

InstrumentParameterField instrumentFieldForBinding(
    const std::string& binding)
{
    const auto endsWith = [&binding](const char* suffix) {
        const auto length = std::char_traits<char>::length(suffix);
        return binding.size() >= length
               && binding.compare(binding.size() - length, length, suffix) == 0;
    };
    // Prisme's first (its keys end like other cards' bindings): "….prism.<key>".
    if (const auto at = binding.find(".prism."); at != std::string::npos || binding.rfind("prism.", 0) == 0) {
        const auto key = at != std::string::npos ? binding.substr(at + 7) : binding.substr(6);
        if (const auto index = cydaw::engine::prism::parameterIndex(key); index >= 0)
            return static_cast<InstrumentParameterField>(static_cast<int>(InstrumentParameterField::prismFirst) + index);
    }
    // A Batterie's: "….batterie.<key>".
    if (const auto at = binding.find(".batterie."); at != std::string::npos || binding.rfind("batterie.", 0) == 0) {
        const auto key = at != std::string::npos ? binding.substr(at + 10) : binding.substr(9);
        if (const auto index = cydaw::engine::drum::parameterIndex(key); index >= 0)
            return static_cast<InstrumentParameterField>(static_cast<int>(InstrumentParameterField::drumFirst) + index);
    }
    // A synth of the family: "….<prefix>.<key>", its model by the prefix.
    for (int modelIndex = 1; modelIndex < cydaw::engine::synth::modelCount; ++modelIndex) {
        const auto model = static_cast<cydaw::engine::synth::Model>(modelIndex);
        const auto& prefix = cydaw::engine::synth::info(model).prefix;
        if (prefix.empty())
            continue;
        const auto dotted = "." + prefix + ".";
        const auto at = binding.find(dotted);
        const auto leading = binding.rfind(prefix + ".", 0) == 0;
        if (at == std::string::npos && !leading)
            continue;
        const auto key = at != std::string::npos ? binding.substr(at + dotted.size()) : binding.substr(prefix.size() + 1);
        if (const auto index = cydaw::engine::synth::parameterIndex(model, key); index >= 0)
            return static_cast<InstrumentParameterField>(static_cast<int>(InstrumentParameterField::synthFirst) + index);
    }
    if (const auto at = binding.find(".kick."); at != std::string::npos || binding.rfind("kick.", 0) == 0) {
        const auto key = at != std::string::npos ? binding.substr(at + 6) : binding.substr(5);
        if (const auto index = cydaw::engine::kick::parameterIndex(key); index >= 0)
            return static_cast<InstrumentParameterField>(static_cast<int>(InstrumentParameterField::kickFirst) + index);
    }
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
            return static_cast<InstrumentParameterField>(
                static_cast<int>(InstrumentParameterField::modelSettingFirst) + static_cast<int>(index));
    if (endsWith("sampler.level"))
        return InstrumentParameterField::samplerLevel;
    for (std::size_t index = 0; index < cydaw::engine::samplerSettingCount; ++index)
        if (endsWith(std::string(cydaw::engine::samplerSettings[index].suffix).c_str()))
            return static_cast<InstrumentParameterField>(
                static_cast<int>(InstrumentParameterField::samplerSettingFirst) + static_cast<int>(index));
    return InstrumentParameterField::none;
}

void applyParameter(cydaw::engine::NativeDspNodeParameters& target,
                    const NativeParameterField field, const float value) noexcept
{
    switch (field) {
    case NativeParameterField::driveDb: target.driveDb = value; break;
    case NativeParameterField::gainDb: target.gainDb = value; break;
    case NativeParameterField::lowpassCutoff: target.cutoffHz = value; break;
    case NativeParameterField::lowpassResonance:
        target.resonance = value; break;
    case NativeParameterField::highpassCutoff:
        target.highpassCutoffHz = value;
        break;
    case NativeParameterField::clipThreshold: target.clipThreshold = value; break;
    case NativeParameterField::stereoWidth: target.stereoWidth = value; break;
    case NativeParameterField::pan: target.pan = value; break;
    case NativeParameterField::oscillatorFrequency:
        target.oscillatorFrequencyHz = value;
        break;
    case NativeParameterField::noiseLevel: target.noiseLevel = value; break;
    case NativeParameterField::delayTime: target.delayMilliseconds = value; break;
    case NativeParameterField::delayFeedback: target.feedback = value; break;
    case NativeParameterField::mix: target.mix = value; break;
    case NativeParameterField::roomSize: target.roomSize = value; break;
    case NativeParameterField::compressorThreshold:
        target.thresholdDb = value;
        break;
    case NativeParameterField::compressorRatio: target.ratio = value; break;
    case NativeParameterField::tone: target.tone = value; break;
    case NativeParameterField::bandFrequency:
        target.bandFrequencyHz = value; break;
    case NativeParameterField::bandGain: target.bandGainDb = value; break;
    case NativeParameterField::bandQ: target.bandQ = value; break;
    case NativeParameterField::wavetablePosition:
        target.wavetablePosition = value; break;
    case NativeParameterField::wavetableDetune:
        target.wavetableDetuneCents = value; break;
    case NativeParameterField::stringDamping:
        target.stringDamping = value; break;
    case NativeParameterField::stringDecay:
        target.stringDecaySeconds = value; break;
    case NativeParameterField::modulationRate:
        target.modulationRateHz = value; break;
    case NativeParameterField::modulationDepth:
        target.modulationDepth = value; break;
    case NativeParameterField::modulationOffset:
        target.modulationOffset = value; break;
    case NativeParameterField::modulationShape:
        target.modulationShape = value; break;
    case NativeParameterField::valueOperand:
        target.valueOperand = value;
        target.valueInputB = value;
        break;
    case NativeParameterField::valueMinimum: target.valueMinimum = value; break;
    case NativeParameterField::valueMaximum: target.valueMaximum = value; break;
    case NativeParameterField::mapInputMinimum:
        target.mapInputMinimum = value; break;
    case NativeParameterField::mapInputMaximum:
        target.mapInputMaximum = value; break;
    case NativeParameterField::mapOutputMinimum:
        target.mapOutputMinimum = value; break;
    case NativeParameterField::mapOutputMaximum:
        target.mapOutputMaximum = value; break;
    case NativeParameterField::followerAttack:
        target.followerAttackSeconds = value; break;
    case NativeParameterField::followerRelease:
        target.followerReleaseSeconds = value; break;
    case NativeParameterField::effect1: target.effect1 = value; break;
    case NativeParameterField::effect2: target.effect2 = value; break;
    case NativeParameterField::effect3: target.effect3 = value; break;
    case NativeParameterField::effect4: target.effect4 = value; break;
    case NativeParameterField::effect5: target.effect5 = value; break;
    case NativeParameterField::effect6: target.effect6 = value; break;
    case NativeParameterField::effect7: target.effect7 = value; break;
    case NativeParameterField::effect8: target.effect8 = value; break;
    default:
        if (field >= NativeParameterField::effectSlotFirst && field <= NativeParameterField::effectSlotLast)
            target.eqBands[static_cast<std::size_t>(static_cast<int>(field) - static_cast<int>(NativeParameterField::effectSlotFirst))]
                = value;
        break;
    }
}

float parameterValue(
    const cydaw::engine::NativeDspNodeParameters& source,
    const NativeParameterField field) noexcept
{
    switch (field) {
    case NativeParameterField::driveDb: return source.driveDb;
    case NativeParameterField::gainDb: return source.gainDb;
    case NativeParameterField::lowpassCutoff: return source.cutoffHz;
    case NativeParameterField::lowpassResonance: return source.resonance;
    case NativeParameterField::highpassCutoff:
        return source.highpassCutoffHz;
    case NativeParameterField::clipThreshold: return source.clipThreshold;
    case NativeParameterField::stereoWidth: return source.stereoWidth;
    case NativeParameterField::pan: return source.pan;
    case NativeParameterField::oscillatorFrequency:
        return source.oscillatorFrequencyHz;
    case NativeParameterField::noiseLevel: return source.noiseLevel;
    case NativeParameterField::delayTime: return source.delayMilliseconds;
    case NativeParameterField::delayFeedback: return source.feedback;
    case NativeParameterField::mix: return source.mix;
    case NativeParameterField::roomSize: return source.roomSize;
    case NativeParameterField::compressorThreshold:
        return source.thresholdDb;
    case NativeParameterField::compressorRatio: return source.ratio;
    case NativeParameterField::tone: return source.tone;
    case NativeParameterField::bandFrequency: return source.bandFrequencyHz;
    case NativeParameterField::bandGain: return source.bandGainDb;
    case NativeParameterField::bandQ: return source.bandQ;
    case NativeParameterField::wavetablePosition:
        return source.wavetablePosition;
    case NativeParameterField::wavetableDetune:
        return source.wavetableDetuneCents;
    case NativeParameterField::stringDamping: return source.stringDamping;
    case NativeParameterField::stringDecay: return source.stringDecaySeconds;
    case NativeParameterField::modulationRate:
        return source.modulationRateHz;
    case NativeParameterField::modulationDepth:
        return source.modulationDepth;
    case NativeParameterField::modulationOffset:
        return source.modulationOffset;
    case NativeParameterField::modulationShape:
        return source.modulationShape;
    case NativeParameterField::valueOperand: return source.valueOperand;
    case NativeParameterField::valueMinimum: return source.valueMinimum;
    case NativeParameterField::valueMaximum: return source.valueMaximum;
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
    case NativeParameterField::effect1: return source.effect1;
    case NativeParameterField::effect2: return source.effect2;
    case NativeParameterField::effect3: return source.effect3;
    case NativeParameterField::effect4: return source.effect4;
    case NativeParameterField::effect5: return source.effect5;
    case NativeParameterField::effect6: return source.effect6;
    case NativeParameterField::effect7: return source.effect7;
    case NativeParameterField::effect8: return source.effect8;
    default:
        if (field >= NativeParameterField::effectSlotFirst && field <= NativeParameterField::effectSlotLast)
            return source.eqBands[static_cast<std::size_t>(static_cast<int>(field) - static_cast<int>(NativeParameterField::effectSlotFirst))];
        break;
    }
    return 0.0f;
}

void applyInstrumentParameter(
    cydaw::engine::PolySynthGraphPlan& target,
    const InstrumentParameterField field, const float value) noexcept
{
    using Waveform = cydaw::engine::PolyOscillatorWaveform;
    switch (field) {
    case InstrumentParameterField::none: break;
    case InstrumentParameterField::waveform:
        // Two modes were appended to the waveform enum; a clamp that still
        // stopped at the fourth turned a wavetable instrument into a sine.
        target.waveform = static_cast<Waveform>(juce::jlimit(
            0, static_cast<int>(Waveform::kick), static_cast<int>(std::lround(value))));
        break;
    case InstrumentParameterField::oscillatorLevel:
        target.oscillatorLevel = juce::jlimit(
            0.0f, cydaw::engine::playsRecordingLevel(target.waveform) ? 1.0f : 0.5f, value); break;
    case InstrumentParameterField::modelLevel:
    case InstrumentParameterField::samplerLevel:
        target.oscillatorLevel = juce::jlimit(0.0f, 1.0f, value); break;
    case InstrumentParameterField::attackMilliseconds:
        target.attackSeconds = juce::jlimit(0.0001f, 5.0f,
                                             value * 0.001f); break;
    case InstrumentParameterField::decayMilliseconds:
        target.decaySeconds = juce::jlimit(0.0001f, 5.0f,
                                           value * 0.001f); break;
    case InstrumentParameterField::sustainLevel:
        target.sustainLevel = juce::jlimit(0.0f, 1.0f, value); break;
    case InstrumentParameterField::releaseMilliseconds:
        target.releaseSeconds = juce::jlimit(0.0001f, 10.0f,
                                              value * 0.001f); break;
    case InstrumentParameterField::filterCutoff:
        target.filterCutoffHz = juce::jlimit(20.0f, 20'000.0f, value); break;
    case InstrumentParameterField::filterResonance:
        target.filterResonance = juce::jlimit(0.1f, 20.0f, value); break;
    case InstrumentParameterField::transpose:
        target.transposeSemitones = juce::jlimit(
            -96, 96, static_cast<int>(std::lround(value))); break;
    case InstrumentParameterField::minimumNote:
        target.minimumMidiNote = juce::jlimit(
            0, 127, static_cast<int>(std::lround(value))); break;
    case InstrumentParameterField::maximumNote:
        target.maximumMidiNote = juce::jlimit(
            target.minimumMidiNote, 127,
            static_cast<int>(std::lround(value))); break;
    case InstrumentParameterField::velocityScale:
        target.velocityScale = juce::jlimit(0.0f, 4.0f, value); break;
    case InstrumentParameterField::voiceMode:
        target.voiceMode = juce::jlimit(0, 2, juce::roundToInt(value)); break;
    case InstrumentParameterField::glide:
        target.glideSeconds = juce::jlimit(0.0f, 2.0f, value / 1'000.0f); break;
    default:
        if (field >= InstrumentParameterField::modelSettingFirst && field <= InstrumentParameterField::modelSettingLast) {
            const auto index = static_cast<std::size_t>(static_cast<int>(field)
                                                        - static_cast<int>(InstrumentParameterField::modelSettingFirst));
            if (index < cydaw::engine::soundModelSettingCount) {
                const auto& setting = cydaw::engine::soundModelSettings[index];
                target.soundModelValues[index] = juce::jlimit(setting.minimum, setting.maximum, value);
            }
        }
        if (field >= InstrumentParameterField::prismFirst && field <= InstrumentParameterField::prismLast) {
            const auto index = static_cast<int>(field) - static_cast<int>(InstrumentParameterField::prismFirst);
            // A table is not changed here: its frames are built off this thread.
            const auto& parameter = cydaw::engine::prism::parameter(index);
            if (parameter.key != "a.table" && parameter.key != "b.table")
                target.prismValues[static_cast<std::size_t>(index)] = cydaw::engine::prism::clampValue(index, value);
        }
        if (field >= InstrumentParameterField::kickFirst && field <= InstrumentParameterField::kickLast) {
            const auto index = static_cast<int>(field) - static_cast<int>(InstrumentParameterField::kickFirst);
            target.kickValues[static_cast<std::size_t>(index)] = cydaw::engine::kick::clampValue(index, value);
        }
        if (field >= InstrumentParameterField::drumFirst && field <= InstrumentParameterField::drumLast) {
            const auto index = static_cast<int>(field) - static_cast<int>(InstrumentParameterField::drumFirst);
            target.drumValues[static_cast<std::size_t>(index)] = cydaw::engine::drum::clampValue(index, value);
        }
        if (field >= InstrumentParameterField::synthFirst && field <= InstrumentParameterField::synthLast
            && target.synthModel != cydaw::engine::synth::Model::none) {
            const auto index = static_cast<int>(field) - static_cast<int>(InstrumentParameterField::synthFirst);
            if (index < cydaw::engine::synth::parameterCount(target.synthModel))
                target.synthValues[static_cast<std::size_t>(index)] = cydaw::engine::synth::clampValue(target.synthModel, index, value);
        }
        if (field >= InstrumentParameterField::samplerSettingFirst
            && field <= InstrumentParameterField::samplerSettingLast) {
            const auto index = static_cast<std::size_t>(static_cast<int>(field)
                                                        - static_cast<int>(InstrumentParameterField::samplerSettingFirst));
            if (index < cydaw::engine::samplerSettingCount) {
                const auto& setting = cydaw::engine::samplerSettings[index];
                target.samplerValues[index] = juce::jlimit(setting.minimum, setting.maximum, value);
            }
        }
        break;
    }
}

// What an instrument's plan holds in a field (the inverse of
// applyInstrumentParameter); none for a field it has no value for.
std::optional<float> instrumentParameterValue(
    const cydaw::engine::PolySynthGraphPlan& plan,
    const InstrumentParameterField field) noexcept
{
    switch (field) {
    case InstrumentParameterField::none: return std::nullopt;
    case InstrumentParameterField::waveform: return static_cast<float>(static_cast<int>(plan.waveform));
    case InstrumentParameterField::oscillatorLevel:
    case InstrumentParameterField::modelLevel:
    case InstrumentParameterField::samplerLevel: return plan.oscillatorLevel;
    case InstrumentParameterField::attackMilliseconds: return plan.attackSeconds * 1'000.0f;
    case InstrumentParameterField::decayMilliseconds: return plan.decaySeconds * 1'000.0f;
    case InstrumentParameterField::sustainLevel: return plan.sustainLevel;
    case InstrumentParameterField::releaseMilliseconds: return plan.releaseSeconds * 1'000.0f;
    case InstrumentParameterField::filterCutoff: return plan.filterCutoffHz;
    case InstrumentParameterField::filterResonance: return plan.filterResonance;
    case InstrumentParameterField::transpose: return static_cast<float>(plan.transposeSemitones);
    case InstrumentParameterField::minimumNote: return static_cast<float>(plan.minimumMidiNote);
    case InstrumentParameterField::maximumNote: return static_cast<float>(plan.maximumMidiNote);
    case InstrumentParameterField::velocityScale: return plan.velocityScale;
    case InstrumentParameterField::voiceMode: return static_cast<float>(plan.voiceMode);
    case InstrumentParameterField::glide: return plan.glideSeconds * 1'000.0f;
    default: break;
    }
    const auto at = [field](const InstrumentParameterField first) {
        return static_cast<std::size_t>(static_cast<int>(field) - static_cast<int>(first));
    };
    if (field >= InstrumentParameterField::modelSettingFirst && field <= InstrumentParameterField::modelSettingLast
        && at(InstrumentParameterField::modelSettingFirst) < cydaw::engine::soundModelSettingCount)
        return plan.soundModelValues[at(InstrumentParameterField::modelSettingFirst)];
    if (field >= InstrumentParameterField::samplerSettingFirst && field <= InstrumentParameterField::samplerSettingLast
        && at(InstrumentParameterField::samplerSettingFirst) < cydaw::engine::samplerSettingCount)
        return plan.samplerValues[at(InstrumentParameterField::samplerSettingFirst)];
    if (field >= InstrumentParameterField::prismFirst && field <= InstrumentParameterField::prismLast)
        return plan.prismValues[at(InstrumentParameterField::prismFirst)];
    if (field >= InstrumentParameterField::kickFirst && field <= InstrumentParameterField::kickLast)
        return plan.kickValues[at(InstrumentParameterField::kickFirst)];
    if (field >= InstrumentParameterField::drumFirst && field <= InstrumentParameterField::drumLast)
        return plan.drumValues[at(InstrumentParameterField::drumFirst)];
    if (field >= InstrumentParameterField::synthFirst && field <= InstrumentParameterField::synthLast
        && plan.synthModel != cydaw::engine::synth::Model::none
        && static_cast<int>(at(InstrumentParameterField::synthFirst)) < cydaw::engine::synth::parameterCount(plan.synthModel))
        return plan.synthValues[at(InstrumentParameterField::synthFirst)];
    return std::nullopt;
}

struct HostSlotTarget {
    int nodeIndex { -1 };
    NativeParameterField field { NativeParameterField::mix };
    InstrumentParameterField instrumentField {
        InstrumentParameterField::none
    };
    float minimum {};
    float maximum { 1.0f };
};

struct HostParameterMap {
    std::vector<cydaw::engine::NativeDspNode> nodes;
    std::array<HostSlotTarget, hostParameterSlotCount> slots;
};

} // namespace

class GraphRuntimeProcessor;

#if JUCE_WINDOWS
// The plug-in's window: CyDAW's own interface of its device (Prisme's,
// a synth's, the effects' large views, or the interface drawn in the
// workshop), over the graph its state holds. Never the graph itself.
class GraphRuntimeEditor final : public juce::AudioProcessorEditor,
                                 private juce::Timer {
public:
    explicit GraphRuntimeEditor(GraphRuntimeProcessor& processor);
    ~GraphRuntimeEditor() override;
    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    void publish(bool topologyChanged);
    void timerCallback() override;

    GraphRuntimeProcessor& processor_;
    std::unique_ptr<cydaw::plugin_ui::PluginInterface> interface_;
};
#else
class GraphRuntimeEditor final : public juce::AudioProcessorEditor,
                                 private juce::Timer {
public:
    explicit GraphRuntimeEditor(GraphRuntimeProcessor& processor);
    ~GraphRuntimeEditor() override;
    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    void publish(bool topologyChanged);
    void showGraphEditor(bool showGraph);
    void timerCallback() override;

    GraphRuntimeProcessor& processor_;
    cydaw::app::CyDawLookAndFeel lookAndFeel_;
    cydaw::app::NodeGraphComponent graph_;
    cydaw::app::ModuleInterfaceEditorComponent deviceSurface_ { {} };
    juce::TextButton interfaceButton_;
    juce::TextButton graphButton_;
    juce::TooltipWindow tooltips_ { this, 650 };
    bool graphEditorVisible_ {};
};
#endif

class GraphRuntimeProcessor final : public juce::AudioProcessor {
public:
    GraphRuntimeProcessor()
        : AudioProcessor(makeBusesProperties()),
          nativeGraph_(cydaw::app::makePassthroughAudioGraph())
    {
        parameterCapacity_ = cydaw::generated_graph::exactWrapper
            ? std::min(hostParameterSlotCount,
                       cydaw::generated_graph::parameters.size())
            : hostParameterSlotCount;
        for (auto slot = std::size_t {}; slot < parameterCapacity_; ++slot) {
            const auto* config = cydaw::generated_graph::exactWrapper
                ? &cydaw::generated_graph::parameters[slot] : nullptr;
            auto parameter = std::make_unique<GraphHostParameter>(slot, config);
            hostParameters_[slot] = parameter.get();
            addParameter(parameter.release());
        }
        hostParameterMap_.store(std::make_shared<HostParameterMap>());
        if constexpr (cydaw::generated_graph::exactWrapper) {
            juce::String state;
            for (const auto* chunk :
                 cydaw::generated_graph::embeddedStateJsonChunks)
                state += juce::String::fromUTF8(chunk);
            if (state.isNotEmpty())
                setStateInformation(state.toRawUTF8(),
                                    static_cast<int>(
                                        state.getNumBytesAsUTF8()));
        }
    }

    const juce::String getName() const override
    {
        return cydaw::generated_graph::productName;
    }

    void prepareToPlay(const double sampleRate,
                       const int maximumBlockSize) override
    {
        runtime_.prepare(sampleRate, maximumBlockSize,
                         juce::jlimit(1, 8, getTotalNumOutputChannels()));
        instrumentRenderer_.prepare(sampleRate);
        analysisSampleRate_ = std::max(1.0, sampleRate);
        analysisSamplesUntilFrame_ = 0;
        reportLatency();
    }

    // The host compensates what the graph's look-aheads hold back.
    void reportLatency()
    {
        if (const auto latency = runtime_.latencySamples(); latency != getLatencySamples())
            setLatencySamples(latency);
    }

    void releaseResources() override { instrumentRenderer_.release(); }

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        const auto input = layouts.getMainInputChannelSet();
        const auto output = layouts.getMainOutputChannelSet();
        const auto channels = output.size();
        if constexpr (cydaw::generated_graph::exactWrapper)
            return input.size()
                       == cydaw::generated_graph::mainInputChannels
                   && output.size()
                          == cydaw::generated_graph::mainOutputChannels;
        return input == output && channels >= 1 && channels <= 8;
    }

    void processBlock(juce::AudioBuffer<float>& buffer,
                      juce::MidiBuffer& midi) override
    {
        juce::ScopedNoDenormals noDenormals;
        // The host's tempo: the effects and the MIDI effects synced to a
        // note value follow it.
        if (auto* head = getPlayHead())
            if (const auto position = head->getPosition())
                if (const auto bpm = position->getBpm(); bpm.hasValue() && *bpm > 1.0) {
                    hostTempo_.store(*bpm, std::memory_order_relaxed);
                    cydaw::app::NativeDspRuntime::setSessionTempo(*bpm);
                }
        // A host rendering offline: a sampler's lane waits for frames still
        // on the disk instead of missing them.
        const juce::ScopedValueSetter<bool> mayWait(cydaw::engine::samplerMayWait, isNonRealtime());
        // The instrument's settings the host moves: applied to a copy of its
        // plan while it plays (renderChanged), never copied every block.
        struct InstrumentChanges {
            std::array<std::pair<InstrumentParameterField, float>, hostParameterSlotCount> list {};
            std::size_t count {};
        } instrumentChanges;
        if (const auto mapping = hostParameterMap_.read();
            mapping != nullptr && mapping->nodes.size() <= hostParameterSlotCount) {
            // Kept by the plug-in, not on the audio thread's stack (32 nodes
            // weigh about 100 KB), their storage reused from block to block.
            auto& nodes = hostNodes_;
            const auto nodeCount = mapping->nodes.size();
            // Their numbers copied; their tables, response and instrument
            // pointed at without being owned (an aliasing pointer with no
            // owner): what they point at is the map's, kept while it is read,
            // and nothing a copy held can be freed here, on the audio thread.
            const auto unowned = [](const auto& pointer) {
                using Pointer = std::decay_t<decltype(pointer)>;
                return Pointer(Pointer {}, pointer.get());
            };
            for (auto index = std::size_t {}; index < nodeCount; ++index) {
                const auto& from = mapping->nodes[index];
                auto& node = nodes[index];
                node.id = from.id;
                node.kind = from.kind;
                node.parameters = from.parameters;
                node.inputBus = from.inputBus;
                node.wavetable = unowned(from.wavetable);
                node.impulse = unowned(from.impulse);
                node.instrument = unowned(from.instrument);
            }
            for (auto slot = std::size_t {}; slot < hostParameterSlotCount;
                 ++slot) {
                const auto& target = mapping->slots[slot];
                if (hostParameters_[slot] == nullptr)
                    continue;
                const auto normalised = hostParameters_[slot]->getValue();
                const auto actual = target.minimum
                    + normalised * (target.maximum - target.minimum);
                if (target.instrumentField
                    != InstrumentParameterField::none)
                    instrumentChanges.list[instrumentChanges.count++]
                        = { target.instrumentField, static_cast<float>(actual) };
                if (target.nodeIndex >= 0
                    && static_cast<std::size_t>(target.nodeIndex) < nodeCount)
                    applyParameter(
                        nodes[static_cast<std::size_t>(target.nodeIndex)].parameters,
                        target.field, actual);
            }
            for (auto index = std::size_t {}; index < nodeCount; ++index)
                (void) runtime_.updateNodeParametersRealtime(nodes[index]);
        }
        if (instrumentRenderer_.enabled()) {
            buffer.clear();
            if (instrumentChanges.count > 0)
                instrumentRenderer_.renderChanged(
                    buffer, midi, 0, buffer.getNumSamples(),
                    [](cydaw::engine::PolySynthGraphPlan& plan, const void* context) {
                        const auto& changes = *static_cast<const InstrumentChanges*>(context);
                        for (std::size_t index = 0; index < changes.count; ++index)
                            applyInstrumentParameter(plan, changes.list[index].first, changes.list[index].second);
                    },
                    &instrumentChanges);
            else
                instrumentRenderer_.render(buffer, midi, 0,
                                           buffer.getNumSamples());
        } else {
            const auto inputChannels = getTotalNumInputChannels();
            for (auto channel = inputChannels;
                 channel < buffer.getNumChannels(); ++channel)
                buffer.clear(channel, 0, buffer.getNumSamples());
        }
        (void) runtime_.process(buffer, 0, buffer.getNumSamples());
        publishAnalysis(buffer);
    }

    double getTailLengthSeconds() const override { return 12.0; }
    bool acceptsMidi() const override
    {
        return cydaw::generated_graph::acceptsMidi;
    }
    bool producesMidi() const override
    {
        return cydaw::generated_graph::producesMidi;
    }
    bool isMidiEffect() const override { return false; }
    bool hasEditor() const override { return true; }

    juce::AudioProcessorEditor* createEditor() override
    {
        return new GraphRuntimeEditor(*this);
    }

    // The device's programs (made in CyDAW's workshop): each sets the host
    // parameters it has, as a host's program change does.
    int getNumPrograms() override
    {
        const juce::ScopedLock lock(stateLock_);
        return std::max(1, static_cast<int>(programs_.size()));
    }
    int getCurrentProgram() override
    {
        const juce::ScopedLock lock(stateLock_);
        return programs_.empty() ? 0 : currentProgram_;
    }
    void setCurrentProgram(const int index) override
    {
        std::vector<float> values;
        {
            const juce::ScopedLock lock(stateLock_);
            if (index < 0 || index >= static_cast<int>(programs_.size()))
                return;
            currentProgram_ = index;
            values = programs_[static_cast<std::size_t>(index)].hostValues;
        }
        for (auto slot = std::size_t {}; slot < std::min(parameterCapacity_, values.size()); ++slot)
            if (hostParameters_[slot] != nullptr && hostParameters_[slot]->metadata()->active)
                hostParameters_[slot]->setValueNotifyingHost(values[slot]);
        updateHostDisplay(ChangeDetails().withProgramChanged(true));
    }
    const juce::String getProgramName(const int index) override
    {
        const juce::ScopedLock lock(stateLock_);
        return index >= 0 && index < static_cast<int>(programs_.size())
                   ? programs_[static_cast<std::size_t>(index)].name
                   : juce::String("Graph");
    }
    void changeProgramName(const int index, const juce::String& name) override
    {
        const juce::ScopedLock lock(stateLock_);
        if (index >= 0 && index < static_cast<int>(programs_.size()) && name.trim().isNotEmpty())
            programs_[static_cast<std::size_t>(index)].name = name.trim();
    }

    juce::Result applyEditorGraph(const juce::var& editorState,
                                  const cydaw::engine::NativeDspGraph& graph,
                                  const std::vector<cydaw::graph::PublishedParameter>&
                                      publishedParameters,
                                  const cydaw::engine::PolySynthGraphPlan&
                                      instrumentPlan,
                                  const bool topologyChanged)
    {
        auto result = topologyChanged
                          ? runtime_.setGraph(graph)
                          : runtime_.updateParameters(graph);
        if (result.failed() && !topologyChanged)
            result = runtime_.setGraph(graph);
        if (result.failed())
            return result;
        reportLatency();
        instrumentRenderer_.setPlan(instrumentPlan);
        const auto preserveHostValues = preserveHostParametersOnce_.exchange(
            false, std::memory_order_acq_rel);
        configureHostParameters(graph, instrumentPlan, publishedParameters,
                                !preserveHostValues);
        const juce::ScopedLock lock(stateLock_);
        editorState_ = editorState;
        nativeGraph_ = graph;
        publishedParameters_ = publishedParameters;
        instrumentPlan_ = instrumentPlan;
        return result;
    }

    juce::var editorState() const
    {
        const juce::ScopedLock lock(stateLock_);
        return editorState_;
    }

    std::vector<std::pair<juce::String, double>>
        currentPublishedParameterValues() const
    {
        std::vector<std::pair<juce::String, double>> values;
        const juce::ScopedLock lock(stateLock_);
        values.reserve(publishedParameters_.size());
        auto slot = std::size_t {};
        for (const auto& parameter : publishedParameters_) {
            if (!parameter.exportedToHost || slot >= parameterCapacity_)
                continue;
            const auto normalised = hostParameters_[slot] != nullptr
                ? hostParameters_[slot]->getValue() : 0.0f;
            values.emplace_back(
                juce::String(parameter.key),
                parameter.minimum
                    + static_cast<double>(normalised)
                        * (parameter.maximum - parameter.minimum));
            ++slot;
        }
        return values;
    }

    bool popAnalysisFrame(
        cydaw::engine::AudioAnalysisFrame& frame) noexcept
    {
        return analysisFrames_.tryPop(frame);
    }

    // What the window's displays read of the sound (message thread): the
    // devices' taps, what they show of their work, their levels, the tempo.
    void setTaps(const std::vector<cydaw::app::NativeDspRuntime::TapRequest>& taps) noexcept
    {
        runtime_.setTaps(taps);
    }
    bool readTap(const std::uint64_t nodeId, const bool input,
                 cydaw::engine::AudioAnalysisFrame& frame) const noexcept
    {
        return runtime_.readTap(nodeId, input, frame);
    }
    void nodeReadings(const std::uint64_t nodeId, std::vector<float>& into) const
    {
        into.resize(1'024);
        into.resize(runtime_.nodeReadings(nodeId, into.data(), into.size()));
    }
    void nodePeaks(const std::vector<std::uint64_t>& nodes, std::vector<float>& left,
                   std::vector<float>& right) const
    {
        left.assign(nodes.size(), 0.0f);
        right.assign(nodes.size(), 0.0f);
        runtime_.takeNodePeaks(nodes.data(), nodes.size(), left.data(), right.data());
    }
    [[nodiscard]] double hostTempo() const noexcept
    {
        return hostTempo_.load(std::memory_order_relaxed);
    }

    void configureHostParameters(
        const cydaw::engine::NativeDspGraph& graph,
        const cydaw::engine::PolySynthGraphPlan& instrumentPlan,
        const std::vector<cydaw::graph::PublishedParameter>& parameters,
        const bool notifyHostValues)
    {
        auto mapping = std::make_shared<HostParameterMap>();
        auto parameterInfoChanged = false;
        auto slot = std::size_t {};
        for (const auto& parameter : parameters) {
            if (!parameter.exportedToHost || slot >= parameterCapacity_)
                continue;
            const auto extent = static_cast<float>(
                parameter.maximum - parameter.minimum);
            HostParameterMetadata metadata;
            metadata.name = juce::String(parameter.displayName.empty()
                                             ? parameter.key
                                             : parameter.displayName);
            metadata.unit = juce::String(parameter.unit);
            metadata.minimum = static_cast<float>(parameter.minimum);
            metadata.maximum = static_cast<float>(parameter.maximum);
            metadata.defaultNormalised = extent > 0.0f
                ? juce::jlimit(
                      0.0f, 1.0f,
                      static_cast<float>(parameter.defaultValue
                                         - parameter.minimum) / extent)
                : 0.0f;
            metadata.stableGraphId = static_cast<std::uint32_t>(parameter.id);
            metadata.active = true;
            metadata.automatable = parameter.automatable;
            const auto previousMetadata = hostParameters_[slot]->metadata();
            const auto samePublishedParameter = previousMetadata->active
                && previousMetadata->stableGraphId == metadata.stableGraphId;
            const auto metadataStructureChanged =
                previousMetadata->name != metadata.name
                || previousMetadata->unit != metadata.unit
                || previousMetadata->minimum != metadata.minimum
                || previousMetadata->maximum != metadata.maximum
                || previousMetadata->stableGraphId != metadata.stableGraphId
                || previousMetadata->active != metadata.active
                || previousMetadata->automatable != metadata.automatable;
            const auto defaultChanged = std::abs(
                previousMetadata->defaultNormalised
                - metadata.defaultNormalised) > 0.000001f;
            if (metadataStructureChanged || defaultChanged)
                hostParameters_[slot]->configure(metadata, false);
            parameterInfoChanged = parameterInfoChanged
                                   || metadataStructureChanged;
            const auto nativeNode = std::find_if(
                graph.nodes.begin(), graph.nodes.end(),
                [&parameter](const auto& node) {
                    return node.id == parameter.targetNodeId;
                });
            auto field = fieldForBinding(parameter.targetParameterKey);
            auto instrumentField = instrumentFieldForBinding(
                parameter.targetParameterKey);
            // An instrument track's export names the target by the
            // primitive's own key ("attack", "drive-db"), which says too
            // little: the public binding ("….envelope.attack") says which
            // field it is. Without it such a host parameter moved nothing.
            if (!field.has_value() && instrumentField == InstrumentParameterField::none) {
                field = fieldForBinding(parameter.key);
                instrumentField = instrumentFieldForBinding(parameter.key);
            }
            auto currentNormalised = metadata.defaultNormalised;
            if (nativeNode != graph.nodes.end() && field.has_value()) {
                if (extent > 0.0f)
                    currentNormalised = juce::jlimit(
                        0.0f, 1.0f,
                        (parameterValue(nativeNode->parameters, *field)
                         - metadata.minimum) / extent);
                const auto mappedNode = std::find_if(
                    mapping->nodes.begin(), mapping->nodes.end(),
                    [&nativeNode](const auto& node) {
                        return node.id == nativeNode->id;
                    });
                auto nodeIndex = mappedNode != mapping->nodes.end()
                    ? static_cast<int>(std::distance(mapping->nodes.begin(),
                                                     mappedNode))
                    : static_cast<int>(mapping->nodes.size());
                if (mappedNode == mapping->nodes.end())
                    mapping->nodes.push_back(*nativeNode);
                mapping->slots[slot] = {
                    nodeIndex, *field, instrumentField,
                    metadata.minimum, metadata.maximum
                };
            } else if (instrumentField != InstrumentParameterField::none) {
                mapping->slots[slot] = {
                    -1, NativeParameterField::mix, instrumentField,
                    metadata.minimum, metadata.maximum
                };
                // Its value as the instrument plays it now (the window may
                // just have moved it): the host follows, and never puts an
                // older one back.
                if (const auto value = instrumentParameterValue(instrumentPlan, instrumentField); value && extent > 0.0f)
                    currentNormalised = juce::jlimit(0.0f, 1.0f, (*value - metadata.minimum) / extent);
            }
            if (notifyHostValues
                && std::abs(hostParameters_[slot]->getValue()
                            - currentNormalised) > 0.000001f)
                hostParameters_[slot]->setValueNotifyingHost(
                    currentNormalised);
            else if (!samePublishedParameter)
                hostParameters_[slot]->setValue(currentNormalised);
            ++slot;
        }
        for (; slot < parameterCapacity_; ++slot) {
            HostParameterMetadata metadata;
            metadata.name = "Unassigned Graph Parameter "
                            + juce::String(static_cast<int>(slot + 1));
            const auto previousMetadata = hostParameters_[slot]->metadata();
            const auto changed = previousMetadata->active
                                 || previousMetadata->name != metadata.name
                                 || previousMetadata->stableGraphId != 0;
            if (changed) {
                hostParameters_[slot]->configure(std::move(metadata), false);
                hostParameters_[slot]->setValue(0.0f);
                parameterInfoChanged = true;
            }
        }
        hostParameterMap_.store(std::move(mapping));
        if (parameterInfoChanged)
            updateHostDisplay(ChangeDetails().withParameterInfoChanged(true));
    }

    void getStateInformation(juce::MemoryBlock& destination) override
    {
        juce::var editor;
        cydaw::engine::NativeDspGraph native;
        std::vector<cydaw::graph::PublishedParameter> published;
        cydaw::engine::PolySynthGraphPlan instrumentPlan;
        {
            const juce::ScopedLock lock(stateLock_);
            editor = editorState_;
            native = nativeGraph_;
            published = publishedParameters_;
            instrumentPlan = instrumentPlan_;
        }
        auto root = juce::DynamicObject::Ptr(new juce::DynamicObject());
        root->setProperty("schema", 2);
        root->setProperty("editor", editor);
        // Where a sampler's factory samples are ("factory:" paths), as
        // CyDAW said when the device was exported.
        if (const auto factory = cydaw::app::factoryContentFolder(); factory.isDirectory())
            root->setProperty("factoryFolder", factory.getFullPathName());
        if (const auto banks = cydaw::app::banksFolder(); banks.isDirectory())
            root->setProperty("banksFolder", banks.getFullPathName());
        root->setProperty(
            "native", cydaw::app::serialiseNativeDspGraphState(native));
        root->setProperty(
            "polySynth",
            cydaw::app::serialisePolySynthGraphPlan(instrumentPlan));
        juce::Array<juce::var> publishedValues;
        for (const auto& parameter : published) {
            auto value = juce::DynamicObject::Ptr(new juce::DynamicObject());
            value->setProperty("id", static_cast<juce::int64>(parameter.id));
            value->setProperty("key", juce::String(parameter.key));
            value->setProperty("name", juce::String(parameter.displayName));
            value->setProperty("unit", juce::String(parameter.unit));
            value->setProperty("minimum", parameter.minimum);
            value->setProperty("maximum", parameter.maximum);
            value->setProperty("default", parameter.defaultValue);
            value->setProperty("targetNodeId",
                               static_cast<juce::int64>(parameter.targetNodeId));
            value->setProperty(
                "targetParameter", juce::String(parameter.targetParameterKey));
            value->setProperty("automatable", parameter.automatable);
            value->setProperty("modulatable", parameter.modulatable);
            value->setProperty("visibleInDevice", parameter.visibleInDevice);
            value->setProperty("exportedToHost", parameter.exportedToHost);
            publishedValues.add(juce::var(value.get()));
        }
        root->setProperty("publishedParameters", publishedValues);
        juce::Array<juce::var> hostValues;
        for (auto slot = std::size_t {}; slot < parameterCapacity_; ++slot)
            hostValues.add(hostParameters_[slot] != nullptr
                               ? hostParameters_[slot]->getValue() : 0.0f);
        root->setProperty("hostValues", hostValues);
        {
            const juce::ScopedLock lock(stateLock_);
            if (!programs_.empty()) {
                juce::Array<juce::var> programs;
                for (const auto& program : programs_) {
                    auto entry = juce::DynamicObject::Ptr(new juce::DynamicObject());
                    entry->setProperty("name", program.name);
                    juce::Array<juce::var> values;
                    for (const auto value : program.hostValues)
                        values.add(value);
                    entry->setProperty("hostValues", values);
                    programs.add(juce::var(entry.get()));
                }
                root->setProperty("programs", programs);
                root->setProperty("program", currentProgram_);
            }
        }
        const auto json = juce::JSON::toString(juce::var(root.get()), false);
        destination.replaceAll(json.toRawUTF8(), json.getNumBytesAsUTF8());
    }

    void setStateInformation(const void* data, const int size) override
    {
        if (data == nullptr || size <= 0)
            return;
        const auto parsed = juce::JSON::parse(
            juce::String::fromUTF8(static_cast<const char*>(data), size));
        const auto* root = parsed.getDynamicObject();
        const auto schema = root != nullptr
            ? static_cast<int>(root->getProperty("schema")) : 0;
        if (root == nullptr || schema < 1 || schema > 2)
            return;
        juce::String error;
        auto native = cydaw::app::restoreNativeDspGraphState(
            root->getProperty("native"), error);
        if (!native.has_value())
            return;
        // A cabinet's response is not a number: it comes back from the graph
        // the state carries, onto the effect of the same card.
        cydaw::app::attachImpulses(*native, root->getProperty("editor"));
        // Each Replacer's instrument, its samples decoded.
        if (const auto factory = root->getProperty("factoryFolder").toString(); factory.isNotEmpty())
            cydaw::app::setFactoryContentFolder(juce::File(factory));
        if (const auto banks = root->getProperty("banksFolder").toString(); banks.isNotEmpty())
            cydaw::app::setBanksFolder(juce::File(banks));
        cydaw::app::attachReplacerInstruments(*native, parsed, true);
        if (runtime_.setGraph(*native).failed())
            return;
        reportLatency();
        cydaw::engine::PolySynthGraphPlan instrumentPlan;
        if (const auto factory = root->getProperty("factoryFolder").toString(); factory.isNotEmpty())
            cydaw::app::setFactoryContentFolder(juce::File(factory));
        if (const auto banks = root->getProperty("banksFolder").toString(); banks.isNotEmpty())
            cydaw::app::setBanksFolder(juce::File(banks));
        if (root->hasProperty("polySynth")) {
            const auto restoredPlan = cydaw::app::restorePolySynthGraphPlan(
                root->getProperty("polySynth"), error);
            if (!restoredPlan.has_value())
                return;
            instrumentPlan = *restoredPlan;
            // The sound model, the sampler's zones and the table are not
            // numbers: they come back from the graph the state carries, so
            // the instrument plays as made before its window is ever opened
            // (a sampler's samples are decoded in the background).
            cydaw::app::attachVoiceAssets(instrumentPlan, root->getProperty("editor"));
            // A sampler's samples are decoded before the state is taken as
            // restored: a host that renders offline right after hears them.
            if (instrumentPlan.sampler != nullptr)
                (void) cydaw::app::SampleStore::instance().waitUntilReady(*instrumentPlan.sampler, 60'000);
        }
        instrumentRenderer_.setPlan(instrumentPlan);
        std::vector<cydaw::graph::PublishedParameter> published;
        if (schema >= 2)
            if (const auto* values = root->getProperty("publishedParameters")
                                         .getArray())
                for (const auto& encoded : *values) {
                    const auto* value = encoded.getDynamicObject();
                    if (value == nullptr)
                        continue;
                    cydaw::graph::PublishedParameter parameter;
                    parameter.id = static_cast<cydaw::graph::ParameterId>(
                        static_cast<juce::int64>(value->getProperty("id")));
                    parameter.key = value->getProperty("key").toString()
                                        .toStdString();
                    parameter.displayName = value->getProperty("name")
                                                .toString().toStdString();
                    parameter.unit = value->getProperty("unit").toString()
                                         .toStdString();
                    parameter.minimum = static_cast<double>(
                        value->getProperty("minimum"));
                    parameter.maximum = static_cast<double>(
                        value->getProperty("maximum"));
                    parameter.defaultValue = static_cast<double>(
                        value->getProperty("default"));
                    parameter.targetNodeId = static_cast<cydaw::graph::NodeId>(
                        static_cast<juce::int64>(
                            value->getProperty("targetNodeId")));
                    parameter.targetParameterKey =
                        value->getProperty("targetParameter")
                            .toString().toStdString();
                    parameter.automatable = !value->hasProperty("automatable")
                        || static_cast<bool>(value->getProperty("automatable"));
                    parameter.modulatable = !value->hasProperty("modulatable")
                        || static_cast<bool>(value->getProperty("modulatable"));
                    parameter.visibleInDevice =
                        !value->hasProperty("visibleInDevice")
                        || static_cast<bool>(
                            value->getProperty("visibleInDevice"));
                    parameter.exportedToHost =
                        !value->hasProperty("exportedToHost")
                        || static_cast<bool>(
                            value->getProperty("exportedToHost"));
                    if (parameter.id != 0 && !parameter.key.empty()
                        && parameter.minimum <= parameter.maximum)
                        published.push_back(std::move(parameter));
                }
        configureHostParameters(*native, instrumentPlan, published, false);
        if (schema >= 2)
            if (const auto* values = root->getProperty("hostValues").getArray())
                for (auto slot = std::size_t {};
                     slot < std::min(parameterCapacity_,
                                     static_cast<std::size_t>(values->size()));
                     ++slot)
                    hostParameters_[slot]->setValue(juce::jlimit(
                        0.0f, 1.0f,
                        static_cast<float>(static_cast<double>(
                            values->getReference(static_cast<int>(slot))))));
        std::vector<RuntimeProgram> programs;
        if (const auto* encoded = root->getProperty("programs").getArray())
            for (const auto& item : *encoded) {
                RuntimeProgram program;
                program.name = item.getProperty("name", {}).toString().trim();
                if (const auto* values = item.getProperty("hostValues", {}).getArray())
                    for (const auto& value : *values)
                        program.hostValues.push_back(juce::jlimit(0.0f, 1.0f, static_cast<float>(static_cast<double>(value))));
                if (program.name.isNotEmpty())
                    programs.push_back(std::move(program));
            }
        {
            const juce::ScopedLock lock(stateLock_);
            editorState_ = root->getProperty("editor");
            nativeGraph_ = *native;
            publishedParameters_ = published;
            instrumentPlan_ = instrumentPlan;
            programs_ = std::move(programs);
            currentProgram_ = juce::jlimit(0, std::max(0, static_cast<int>(programs_.size()) - 1),
                                           static_cast<int>(root->getProperty("program")));
        }
        preserveHostParametersOnce_.store(true, std::memory_order_release);
    }

private:
    void publishAnalysis(const juce::AudioBuffer<float>& buffer) noexcept
    {
        analysisSamplesUntilFrame_ -= buffer.getNumSamples();
        if (analysisSamplesUntilFrame_ > 0 || buffer.getNumSamples() <= 0
            || buffer.getNumChannels() <= 0)
            return;
        const auto interval = std::max<std::int64_t>(
            1, static_cast<std::int64_t>(analysisSampleRate_ / 30.0));
        do {
            analysisSamplesUntilFrame_ += interval;
        } while (analysisSamplesUntilFrame_ <= 0);

        cydaw::engine::AudioAnalysisFrame frame;
        frame.sequence = ++analysisSequence_;
        frame.sampleRate = static_cast<float>(analysisSampleRate_);
        frame.channelCount = static_cast<std::uint32_t>(std::min(
            buffer.getNumChannels(),
            static_cast<int>(frame.maximumChannels)));
        // The mono mix, consecutive, from the shared ring: calibrated the
        // same way the tracks and the preview are.
        const auto channels = static_cast<int>(frame.channelCount);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
            float value {};
            for (int channel = 0; channel < channels; ++channel)
                value += buffer.getSample(channel, sample);
            analysisRing_.push(channels > 0
                                   ? value / static_cast<float>(channels)
                                   : 0.0f);
        }
        analysisRing_.fill(frame);
        for (std::uint32_t channel = 0; channel < frame.channelCount;
             ++channel) {
            frame.peak[channel] = buffer.getMagnitude(
                static_cast<int>(channel), 0, buffer.getNumSamples());
            frame.rms[channel] = buffer.getRMSLevel(
                static_cast<int>(channel), 0, buffer.getNumSamples());
        }
        (void) analysisFrames_.tryPush(frame);
    }

    static BusesProperties makeBusesProperties()
    {
        BusesProperties result;
        const auto inputChannels = cydaw::generated_graph::mainInputChannels;
        const auto outputChannels = cydaw::generated_graph::mainOutputChannels;
        if constexpr (cydaw::generated_graph::mainInputChannels > 0)
            result = result.withInput(
                cydaw::generated_graph::mainInputName,
                juce::AudioChannelSet::canonicalChannelSet(
                             inputChannels), true);
        if constexpr (cydaw::generated_graph::mainOutputChannels > 0)
            result = result.withOutput(
                cydaw::generated_graph::mainOutputName,
                juce::AudioChannelSet::canonicalChannelSet(
                              outputChannels), true);
        return result;
    }

    struct RuntimeProgram {
        juce::String name;
        std::vector<float> hostValues;
    };

    mutable juce::CriticalSection stateLock_;
    std::vector<RuntimeProgram> programs_;
    int currentProgram_ {};
    cydaw::app::NativeDspRuntime runtime_;
    juce::var editorState_;
    cydaw::engine::NativeDspGraph nativeGraph_;
    std::vector<cydaw::graph::PublishedParameter> publishedParameters_;
    cydaw::engine::PolySynthGraphPlan instrumentPlan_;
    cydaw::app::GraphPolySynthRenderer instrumentRenderer_;
    std::array<cydaw::engine::NativeDspNode, hostParameterSlotCount> hostNodes_ {};
    std::array<GraphHostParameter*, hostParameterSlotCount> hostParameters_ {};
    std::size_t parameterCapacity_ { hostParameterSlotCount };
    // Read by processBlock on the host's audio thread every block, so a
    // parameter remap must never make that callback take a lock or free the
    // previous map.
    cydaw::realtime::RtPublisher<HostParameterMap> hostParameterMap_;
    std::atomic<bool> preserveHostParametersOnce_ {};
    std::atomic<double> hostTempo_ { 120.0 };
    cydaw::realtime::SpscRing<cydaw::engine::AudioAnalysisFrame, 8>
        analysisFrames_;
    double analysisSampleRate_ { 48'000.0 };
    std::int64_t analysisSamplesUntilFrame_ {};
    std::uint64_t analysisSequence_ {};
    cydaw::engine::AnalysisWaveformRing analysisRing_;
};

#if JUCE_WINDOWS
GraphRuntimeEditor::GraphRuntimeEditor(GraphRuntimeProcessor& processor)
    : AudioProcessorEditor(processor), processor_(processor)
{
    cydaw::plugin_ui::PluginSound sound;
    auto* owner = &processor_;
    sound.setTaps = [owner](const std::vector<cydaw::app::NativeDspRuntime::TapRequest>& taps) { owner->setTaps(taps); };
    sound.readTap = [owner](const std::uint64_t nodeId, const bool input, cydaw::engine::AudioAnalysisFrame& frame) {
        return owner->readTap(nodeId, input, frame);
    };
    sound.readings = [owner](const std::uint64_t nodeId, std::vector<float>& into) { owner->nodeReadings(nodeId, into); };
    sound.peaks = [owner](const std::vector<std::uint64_t>& nodes, std::vector<float>& left, std::vector<float>& right) {
        owner->nodePeaks(nodes, left, right);
    };
    sound.tempo = [owner] { return owner->hostTempo(); };
    interface_ = std::make_unique<cydaw::plugin_ui::PluginInterface>(
        processor.getName(), processor.acceptsMidi(), std::move(sound),
        [this](const bool structural) { publish(structural); });
    if (const auto state = processor_.editorState(); !state.isVoid())
        interface_->restore(state);
    addAndMakeVisible(*interface_);
    const auto size = interface_->preferredSize();
    const auto least = cydaw::plugin_ui::PluginInterface::smallestSize();
    setResizable(true, true);
    setResizeLimits(least.x, least.y, 4'096, 2'160);
    setSize(size.x, size.y);
    startTimerHz(30);
    publish(true);
}

GraphRuntimeEditor::~GraphRuntimeEditor()
{
    stopTimer();
    processor_.setTaps({});
    interface_.reset();
}

void GraphRuntimeEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(0xff2b2d31));
}

void GraphRuntimeEditor::resized()
{
    if (interface_ != nullptr)
        interface_->setBounds(getLocalBounds());
}

void GraphRuntimeEditor::timerCallback()
{
    // The host's automation, shown on the knobs.
    if (interface_ == nullptr)
        return;
    for (const auto& [key, value] :
         processor_.currentPublishedParameterValues())
        (void) interface_->graph().setGraphInterfaceParameter(key, value);
}

void GraphRuntimeEditor::publish(const bool topologyChanged)
{
    if (interface_ == nullptr)
        return;
    auto& graph = interface_->graph();
    const auto runtimeExport = graph.runtimeGraphExport();
    if (runtimeExport.compilationResult.failed())
        return;
    // Its cards switched off stay off, as in CyDAW.
    (void) processor_.applyEditorGraph(
        graph.serialise(), graph.withCardBypasses(runtimeExport.nativeGraph),
        runtimeExport.publishedParameters, runtimeExport.instrumentPlan,
        topologyChanged);
}
#else
GraphRuntimeEditor::GraphRuntimeEditor(GraphRuntimeProcessor& processor)
    : AudioProcessorEditor(processor), processor_(processor)
{
    graph_.setLookAndFeel(&lookAndFeel_);
    deviceSurface_.setLookAndFeel(&lookAndFeel_);
    graph_.setContextName("Graph Runtime VST3");
    if (const auto state = processor_.editorState(); !state.isVoid())
        graph_.restore(state);
    deviceSurface_.setState(graph_.graphInterfaceState());
    deviceSurface_.setCompactMode(true);
    deviceSurface_.setLayoutMode(
        cydaw::app::ModuleInterfaceLayoutMode::graph);
    deviceSurface_.setParameterChangedHandler(
        [this](const juce::String& key, const double value) {
            if (graph_.setGraphInterfaceParameter(key, value))
                publish(false);
        });
    graph_.setLayoutChangedHandler([this] { publish(true); });
    graph_.setParametersChangedHandler([this] { publish(false); });
    interfaceButton_.setButtonText(cydaw::app::tr(
        "graph.runtime.interface"));
    graphButton_.setButtonText(cydaw::app::tr("graph.runtime.graph"));
    interfaceButton_.setTooltip(cydaw::app::tr(
        "graph.runtime.interfaceHelp"));
    graphButton_.setTooltip(cydaw::app::tr("graph.runtime.graphHelp"));
    interfaceButton_.setClickingTogglesState(true);
    graphButton_.setClickingTogglesState(true);
    interfaceButton_.setRadioGroupId(6'071);
    graphButton_.setRadioGroupId(6'071);
    interfaceButton_.onClick = [this] { showGraphEditor(false); };
    graphButton_.onClick = [this] { showGraphEditor(true); };
    addAndMakeVisible(interfaceButton_);
    addAndMakeVisible(graphButton_);
    addAndMakeVisible(graph_);
    addAndMakeVisible(deviceSurface_);
    if constexpr (cydaw::generated_graph::exactWrapper) {
        // A plugin made in CyDAW's workshop: the interface drawn there, at
        // the size drawn, and nothing of the graph behind it.
        const auto state = graph_.graphInterfaceState();
        const auto width = state.windowWidth != 0 ? static_cast<int>(state.windowWidth) : 720;
        const auto height = state.windowHeight != 0 ? static_cast<int>(state.windowHeight) : 420;
        interfaceButton_.setVisible(false);
        graphButton_.setVisible(false);
        graph_.setVisible(false);
        deviceSurface_.setState(state);
        deviceSurface_.setPerformanceMode(true);
        setResizable(true, true);
        if (auto* constrainer = getConstrainer())
            constrainer->setFixedAspectRatio(static_cast<double>(width) / static_cast<double>(height));
        setResizeLimits(width / 2, height / 2, width * 3, height * 3);
        setSize(width, height);
        startTimerHz(30);
        publish(true);
        return;
    }
    setResizable(true, false);
    setResizeLimits(860, 520, 4'096, 2'160);
    setSize(1'280, 760);
    showGraphEditor(false);
    startTimerHz(30);
    publish(true);
}

GraphRuntimeEditor::~GraphRuntimeEditor()
{
    stopTimer();
    graph_.setLayoutChangedHandler({});
    graph_.setParametersChangedHandler({});
    deviceSurface_.setParameterChangedHandler({});
    graph_.setLookAndFeel(nullptr);
    deviceSurface_.setLookAndFeel(nullptr);
}

void GraphRuntimeEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(0xff111318));
    if constexpr (cydaw::generated_graph::exactWrapper)
        return;
    graphics.setColour(juce::Colour(0xff30363d));
    graphics.drawHorizontalLine(37, 0.0f,
                                static_cast<float>(getWidth()));
}

void GraphRuntimeEditor::resized()
{
    if constexpr (cydaw::generated_graph::exactWrapper) {
        deviceSurface_.setBounds(getLocalBounds());
        return;
    }
    auto bounds = getLocalBounds();
    auto toolbar = bounds.removeFromTop(38).reduced(6, 5);
    interfaceButton_.setBounds(toolbar.removeFromLeft(118));
    toolbar.removeFromLeft(4);
    graphButton_.setBounds(toolbar.removeFromLeft(118));
    graph_.setBounds(bounds);
    deviceSurface_.setBounds(bounds);
}

void GraphRuntimeEditor::showGraphEditor(const bool showGraph)
{
    graphEditorVisible_ = showGraph;
    if (!showGraph) {
        deviceSurface_.setState(graph_.graphInterfaceState());
        deviceSurface_.setCompactMode(true);
        deviceSurface_.setLayoutMode(
            cydaw::app::ModuleInterfaceLayoutMode::graph);
    }
    graph_.setVisible(showGraph);
    deviceSurface_.setVisible(!showGraph);
    interfaceButton_.setToggleState(!showGraph,
                                     juce::dontSendNotification);
    graphButton_.setToggleState(showGraph, juce::dontSendNotification);
    resized();
}

void GraphRuntimeEditor::timerCallback()
{
    for (const auto& [key, value] :
         processor_.currentPublishedParameterValues()) {
        (void) graph_.setGraphInterfaceParameter(key, value);
        (void) deviceSurface_.setParameterValue(key, value);
    }
    cydaw::engine::AudioAnalysisFrame frame;
    auto received = false;
    while (processor_.popAnalysisFrame(frame))
        received = true;
    if (!received)
        return;
    graph_.setAnalysisFrame(frame);
    deviceSurface_.setAnalysisFrame(frame);
}

void GraphRuntimeEditor::publish(const bool topologyChanged)
{
    const auto runtimeExport = graph_.runtimeGraphExport();
    if (runtimeExport.compilationResult.failed()) {
        graph_.setDspCompilationStatus(
            runtimeExport.compilationResult,
            runtimeExport.nativeGraph.nodes.size());
        return;
    }
    // Its cards switched off stay off, as in CyDAW.
    const auto result = processor_.applyEditorGraph(
        graph_.serialise(), graph_.withCardBypasses(runtimeExport.nativeGraph),
        runtimeExport.publishedParameters, runtimeExport.instrumentPlan,
        topologyChanged);
    graph_.setDspCompilationStatus(result,
                                   runtimeExport.nativeGraph.nodes.size());
}

#endif

} // namespace cydaw::graph_plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new cydaw::graph_plugin::GraphRuntimeProcessor();
}
