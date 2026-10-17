#include <cydaw/engine/NativeDspCompiler.h>

#include <cydaw/engine/BuiltinProcessors.h>
#include <cydaw/engine/EffectProcessors.h>

#include <cmath>

namespace cydaw::engine {
namespace {

bool isControlOnly(const NativeDspNodeKind kind) noexcept
{
    return (kind >= NativeDspNodeKind::controlConstant
            && kind <= NativeDspNodeKind::controlNot)
           || (kind >= NativeDspNodeKind::controlModulo
               && kind <= NativeDspNodeKind::controlRound)
           || isModulatorKind(kind);
}

} // namespace

std::unique_ptr<AudioProcessor> makeNativeDspProcessor(
    const NativeDspNode& node)
{
    if (isModulatorKind(node.kind))
        return {};
    if (effectForKind(node.kind) != nullptr) {
        auto effect = makeEffectProcessor(node);
        if (auto* typed = dynamic_cast<EffectProcessor*>(effect.get()); typed != nullptr && node.impulse != nullptr)
            typed->setImpulse(node.impulse);
        return effect;
    }
    switch (node.kind) {
    case NativeDspNodeKind::mixer:
        return std::make_unique<MixerProcessor>();
    case NativeDspNodeKind::drive:
        return std::make_unique<DriveProcessor>(node.parameters.driveDb);
    case NativeDspNodeKind::gain:
        return std::make_unique<GainProcessor>(
            std::pow(10.0f, node.parameters.gainDb / 20.0f));
    case NativeDspNodeKind::lowpass:
        return std::make_unique<OnePoleLowPassProcessor>(
            node.parameters.cutoffHz, node.parameters.resonance);
    case NativeDspNodeKind::highpass:
        return std::make_unique<OnePoleHighPassProcessor>(
            node.parameters.highpassCutoffHz);
    case NativeDspNodeKind::hardClip:
        return std::make_unique<HardClipProcessor>(
            node.parameters.clipThreshold);
    case NativeDspNodeKind::stereoWidth:
        return std::make_unique<StereoWidthProcessor>(
            node.parameters.stereoWidth);
    case NativeDspNodeKind::pan:
        return std::make_unique<PanProcessor>(node.parameters.pan);
    case NativeDspNodeKind::envelopeFollower:
        return std::make_unique<EnvelopeFollowerProcessor>(
            node.parameters.followerAttackSeconds,
            node.parameters.followerReleaseSeconds);
    case NativeDspNodeKind::dcBlocker:
        return std::make_unique<DcBlockerProcessor>();
    case NativeDspNodeKind::oscillator:
        return std::make_unique<SineOscillatorProcessor>(
            node.parameters.oscillatorFrequencyHz,
            node.parameters.oscillatorWaveform,
            node.parameters.oscillatorLevel);
    case NativeDspNodeKind::noise:
        return std::make_unique<NoiseProcessor>(node.parameters.noiseLevel);
    case NativeDspNodeKind::wavetable: {
        auto wavetable = std::make_unique<WavetableOscillatorProcessor>(
            node.parameters.oscillatorFrequencyHz,
            node.parameters.wavetablePosition,
            node.parameters.oscillatorLevel,
            node.parameters.wavetableDetuneCents);
        wavetable->setBank(node.wavetable);
        return wavetable;
    }
    case NativeDspNodeKind::stringResonator:
        return std::make_unique<StringResonatorProcessor>(
            node.parameters.oscillatorFrequencyHz,
            node.parameters.stringDamping,
            node.parameters.stringDecaySeconds);
    case NativeDspNodeKind::delay:
        return std::make_unique<DelayProcessor>(
            node.parameters.delayMilliseconds, node.parameters.feedback,
            node.parameters.mix);
    case NativeDspNodeKind::reverb:
    case NativeDspNodeKind::reverbRack:
        return std::make_unique<ReverbProcessor>(node.parameters.roomSize,
                                                 node.parameters.mix);
    case NativeDspNodeKind::compressor:
        return std::make_unique<CompressorProcessor>(
            node.parameters.thresholdDb, node.parameters.ratio);
    case NativeDspNodeKind::equalizer:
        return std::make_unique<ToneProcessor>(node.parameters.tone);
    case NativeDspNodeKind::peakingEq:
        return std::make_unique<PeakingEqProcessor>(
            node.parameters.bandFrequencyHz, node.parameters.bandGainDb,
            node.parameters.bandQ);
    case NativeDspNodeKind::vocalStrip:
        return std::make_unique<VocalStripProcessor>();
    case NativeDspNodeKind::passthrough:
    case NativeDspNodeKind::input:
    case NativeDspNodeKind::output:
    case NativeDspNodeKind::analyzer:
        return std::make_unique<PassthroughProcessor>();
    case NativeDspNodeKind::controlConstant:
    case NativeDspNodeKind::controlLfo:
    case NativeDspNodeKind::controlAdd:
    case NativeDspNodeKind::controlSubtract:
    case NativeDspNodeKind::controlMultiply:
    case NativeDspNodeKind::controlDivide:
    case NativeDspNodeKind::controlMinimum:
    case NativeDspNodeKind::controlMaximum:
    case NativeDspNodeKind::controlClamp:
    case NativeDspNodeKind::controlAbsolute:
    case NativeDspNodeKind::controlNegate:
    case NativeDspNodeKind::controlMapRange:
    case NativeDspNodeKind::controlScaleOffset:
    case NativeDspNodeKind::controlGreater:
    case NativeDspNodeKind::controlLess:
    case NativeDspNodeKind::controlEqual:
    case NativeDspNodeKind::controlAnd:
    case NativeDspNodeKind::controlOr:
    case NativeDspNodeKind::controlXor:
    case NativeDspNodeKind::controlNot:
    case NativeDspNodeKind::controlModulo:
    case NativeDspNodeKind::controlPower:
    case NativeDspNodeKind::controlSine:
    case NativeDspNodeKind::controlCosine:
    case NativeDspNodeKind::controlSquareRoot:
    case NativeDspNodeKind::controlFloor:
    case NativeDspNodeKind::controlCeil:
    case NativeDspNodeKind::controlRound:
        return {};
    }
    return {};
}

bool updateNativeDspProcessorParameters(
    AudioProcessor& processor, const NativeDspNode& node) noexcept
{
    if (effectForKind(node.kind) != nullptr) {
        // A changed response, like a changed table, is the full compiler's:
        // swapping it here would free the old one on the audio thread.
        if (const auto* typed = dynamic_cast<const EffectProcessor*>(&processor);
            typed != nullptr && typed->impulse() != node.impulse.get())
            return false;
        return updateEffectProcessor(processor, node);
    }
    switch (node.kind) {
    case NativeDspNodeKind::mixer:
        return dynamic_cast<MixerProcessor*>(&processor) != nullptr;
    case NativeDspNodeKind::drive:
        if (auto* typed = dynamic_cast<DriveProcessor*>(&processor)) {
            typed->setDriveDb(node.parameters.driveDb);
            return true;
        }
        break;
    case NativeDspNodeKind::gain:
        if (auto* typed = dynamic_cast<GainProcessor*>(&processor)) {
            typed->setGainLinear(
                std::pow(10.0f, node.parameters.gainDb / 20.0f));
            return true;
        }
        break;
    case NativeDspNodeKind::lowpass:
        if (auto* typed = dynamic_cast<OnePoleLowPassProcessor*>(&processor)) {
            typed->setCutoffHz(node.parameters.cutoffHz);
            typed->setResonance(node.parameters.resonance);
            return true;
        }
        break;
    case NativeDspNodeKind::highpass:
        if (auto* typed = dynamic_cast<OnePoleHighPassProcessor*>(&processor)) {
            typed->setCutoffHz(node.parameters.highpassCutoffHz);
            return true;
        }
        break;
    case NativeDspNodeKind::hardClip:
        if (auto* typed = dynamic_cast<HardClipProcessor*>(&processor)) {
            typed->setThreshold(node.parameters.clipThreshold);
            return true;
        }
        break;
    case NativeDspNodeKind::stereoWidth:
        if (auto* typed = dynamic_cast<StereoWidthProcessor*>(&processor)) {
            typed->setWidth(node.parameters.stereoWidth);
            return true;
        }
        break;
    case NativeDspNodeKind::pan:
        if (auto* typed = dynamic_cast<PanProcessor*>(&processor)) {
            typed->setPan(node.parameters.pan);
            return true;
        }
        break;
    case NativeDspNodeKind::envelopeFollower:
        if (auto* typed = dynamic_cast<EnvelopeFollowerProcessor*>(&processor)) {
            typed->setAttackSeconds(node.parameters.followerAttackSeconds);
            typed->setReleaseSeconds(node.parameters.followerReleaseSeconds);
            return true;
        }
        break;
    case NativeDspNodeKind::oscillator:
        if (auto* typed = dynamic_cast<SineOscillatorProcessor*>(&processor)) {
            typed->setFrequency(node.parameters.oscillatorFrequencyHz);
            typed->setWaveform(node.parameters.oscillatorWaveform);
            typed->setLevel(node.parameters.oscillatorLevel);
            return true;
        }
        break;
    case NativeDspNodeKind::noise:
        if (auto* typed = dynamic_cast<NoiseProcessor*>(&processor)) {
            typed->setLevel(node.parameters.noiseLevel);
            return true;
        }
        break;
    case NativeDspNodeKind::delay:
        if (auto* typed = dynamic_cast<DelayProcessor*>(&processor)) {
            typed->setDelayMilliseconds(node.parameters.delayMilliseconds);
            typed->setFeedback(node.parameters.feedback);
            typed->setMix(node.parameters.mix);
            return true;
        }
        break;
    case NativeDspNodeKind::reverb:
    case NativeDspNodeKind::reverbRack:
        if (auto* typed = dynamic_cast<ReverbProcessor*>(&processor)) {
            typed->setRoomSize(node.parameters.roomSize);
            typed->setMix(node.parameters.mix);
            return true;
        }
        break;
    case NativeDspNodeKind::compressor:
        if (auto* typed = dynamic_cast<CompressorProcessor*>(&processor)) {
            typed->setThresholdDb(node.parameters.thresholdDb);
            typed->setRatio(node.parameters.ratio);
            return true;
        }
        break;
    case NativeDspNodeKind::equalizer:
        if (auto* typed = dynamic_cast<ToneProcessor*>(&processor)) {
            typed->setAmount(node.parameters.tone);
            return true;
        }
        break;
    case NativeDspNodeKind::wavetable:
        if (auto* typed
            = dynamic_cast<WavetableOscillatorProcessor*>(&processor)) {
            // Swapping the table here would free the old one on the audio
            // thread. A changed table is a job for the full compiler.
            if (typed->bank() != node.wavetable.get())
                return false;
            typed->setFrequencyHz(node.parameters.oscillatorFrequencyHz);
            typed->setPosition(node.parameters.wavetablePosition);
            typed->setLevel(node.parameters.oscillatorLevel);
            typed->setDetuneCents(node.parameters.wavetableDetuneCents);
            return true;
        }
        break;
    case NativeDspNodeKind::stringResonator:
        if (auto* typed
            = dynamic_cast<StringResonatorProcessor*>(&processor)) {
            typed->setFrequencyHz(node.parameters.oscillatorFrequencyHz);
            typed->setDamping(node.parameters.stringDamping);
            typed->setDecaySeconds(node.parameters.stringDecaySeconds);
            return true;
        }
        break;
    case NativeDspNodeKind::peakingEq:
        if (auto* typed = dynamic_cast<PeakingEqProcessor*>(&processor)) {
            typed->setFrequencyHz(node.parameters.bandFrequencyHz);
            typed->setGainDb(node.parameters.bandGainDb);
            typed->setQ(node.parameters.bandQ);
            return true;
        }
        break;
    case NativeDspNodeKind::passthrough:
    case NativeDspNodeKind::input:
    case NativeDspNodeKind::output:
    case NativeDspNodeKind::dcBlocker:
    case NativeDspNodeKind::analyzer:
    case NativeDspNodeKind::vocalStrip:
        return true;
    case NativeDspNodeKind::controlConstant:
    case NativeDspNodeKind::controlLfo:
    case NativeDspNodeKind::controlAdd:
    case NativeDspNodeKind::controlSubtract:
    case NativeDspNodeKind::controlMultiply:
    case NativeDspNodeKind::controlDivide:
    case NativeDspNodeKind::controlMinimum:
    case NativeDspNodeKind::controlMaximum:
    case NativeDspNodeKind::controlClamp:
    case NativeDspNodeKind::controlAbsolute:
    case NativeDspNodeKind::controlNegate:
    case NativeDspNodeKind::controlMapRange:
    case NativeDspNodeKind::controlScaleOffset:
    case NativeDspNodeKind::controlGreater:
    case NativeDspNodeKind::controlLess:
    case NativeDspNodeKind::controlEqual:
    case NativeDspNodeKind::controlAnd:
    case NativeDspNodeKind::controlOr:
    case NativeDspNodeKind::controlXor:
    case NativeDspNodeKind::controlNot:
    case NativeDspNodeKind::controlModulo:
    case NativeDspNodeKind::controlPower:
    case NativeDspNodeKind::controlSine:
    case NativeDspNodeKind::controlCosine:
    case NativeDspNodeKind::controlSquareRoot:
    case NativeDspNodeKind::controlFloor:
    case NativeDspNodeKind::controlCeil:
    case NativeDspNodeKind::controlRound:
        return false;
    }
    return false;
}

RenderPlanResult compileNativeDspGraph(
    const NativeDspGraph& graph, const ProcessSpec spec,
    const NativeDspProcessorObserver& observer)
{
    if (graph.nodes.empty() || graph.outputNodeId == 0)
        return { RenderPlanError::missingOutput,
                 "The native DSP graph requires an audio output", {} };

    AudioRenderPlanBuilder builder;
    for (const auto& node : graph.nodes) {
        if (isControlOnly(node.kind))
            continue;
        auto processor = makeNativeDspProcessor(node);
        if (processor == nullptr)
            return { RenderPlanError::invalidNode,
                     "The native DSP graph contains an unsupported processor",
                     {} };
        if (observer)
            observer(node, processor.get());
        const auto error = builder.addNode(
            node.id, std::move(processor),
            node.kind == NativeDspNodeKind::input, node.inputBus);
        if (error != RenderPlanError::none)
            return { error, "Cannot add a node to the native DSP plan", {} };
    }
    for (const auto& connection : graph.connections) {
        const auto error = builder.connect(connection.source,
                                           connection.destination,
                                           connection.sourcePort,
                                           connection.destinationPort);
        if (error != RenderPlanError::none)
            return { error, "Cannot connect the native DSP plan", {} };
    }
    return builder.compile(spec, graph.outputNodeId);
}

} // namespace cydaw::engine
