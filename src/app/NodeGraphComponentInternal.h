#pragma once

#include <cydaw/engine/EffectCatalogue.h>
#include <cydaw/engine/SynthFamily.h>

#include <juce_core/juce_core.h>

namespace cydaw::app::detail {

// UI keys are intentionally stable and human-readable, while processor keys
// are the versioned engine contract stored in GraphDefinition.  Keeping the
// translation in one place prevents the palette, serializer and future VST3
// compiler from silently describing different primitives.
//
// It lives in a header rather than one translation unit because the factory
// catalogue and the interactive editor are separate translation units and
// both have to agree on the mapping.
[[nodiscard]] inline juce::String primitiveProcessorKeyForType(
    const juce::String& key)
{
    if (const auto* effect = cydaw::engine::effectForType(key.toStdString()))
        return juce::String(std::string(effect->processorKey));
    if (key == "voice-allocator") return "core.voice-allocator";
    if (key == "poly-oscillator") return "core.oscillator";
    if (key == "envelope") return "core.adsr";
    if (key == "voice-filter") return "core.lowpass";
    if (key == "oscillator") return "core.fixed-oscillator";
    if (key == "noise") return "core.noise";
    if (key == "drive") return "core.drive";
    if (key == "gain") return "core.gain";
    if (key == "audio-sum") return "core.audio-sum";
    if (key == "mixer-4") return "core.mixer-4";
    if (key == "gain-pan") return "core.pan";
    if (key == "lowpass") return "core.lowpass";
    if (key == "eq-band") return "core.peaking-eq";
    if (key == "wavetable") return "core.wavetable";
    if (key == "string") return "core.string";
    if (key == "sound-model") return "core.sound-model";
    if (key == "sampler") return "core.sampler";
    if (key == "batterie") return "core.batterie";
    if (key == "prism-synth") return "core.prism-synth";
    if (key == "kick-synth") return "core.kick-synth";
    if (const auto model = cydaw::engine::synth::modelForType(key.toStdString()); model != cydaw::engine::synth::Model::none)
        return juce::String(cydaw::engine::synth::info(model).processorKey);
    if (key == "highpass") return "core.highpass";
    if (key == "hard-clip") return "core.hard-clip";
    if (key == "stereo-width") return "core.stereo-width";
    if (key == "dc-blocker") return "core.dc-blocker";
    if (key == "delay") return "core.delay";
    if (key == "reverb") return "core.reverb";
    if (key == "compressor") return "core.compressor";
    if (key == "eq") return "core.equalizer";
    if (key == "lfo-core") return "core.lfo";
    if (key == "macro") return "core.macro";
    if (key == "control-math") return "core.scale-offset";
    if (key == "value-constant") return "core.constant";
    if (key == "value-add") return "core.add";
    if (key == "value-subtract") return "core.subtract";
    if (key == "value-multiply") return "core.multiply";
    if (key == "value-divide") return "core.divide";
    if (key == "value-modulo") return "core.modulo";
    if (key == "value-power") return "core.power";
    if (key == "value-sine") return "core.sine";
    if (key == "value-cosine") return "core.cosine";
    if (key == "value-square-root") return "core.square-root";
    if (key == "value-floor") return "core.floor";
    if (key == "value-ceil") return "core.ceil";
    if (key == "value-round") return "core.round";
    if (key == "value-min") return "core.minimum";
    if (key == "value-max") return "core.maximum";
    if (key == "value-clamp") return "core.clamp";
    if (key == "map-range") return "core.map-range";
    if (key == "value-abs") return "core.absolute";
    if (key == "value-negate") return "core.negate";
    if (key == "logic-greater") return "core.compare-greater";
    if (key == "logic-less") return "core.compare-less";
    if (key == "logic-equal") return "core.compare-equal";
    if (key == "logic-and") return "core.logic-and";
    if (key == "logic-or") return "core.logic-or";
    if (key == "logic-xor") return "core.logic-xor";
    if (key == "logic-not") return "core.logic-not";
    if (key == "midi-arp") return "core.midi-arpeggiator";
    if (key == "midi-transpose") return "core.midi-transpose";
    if (key == "note-filter") return "core.midi-note-filter";
    if (key == "velocity-map") return "core.midi-velocity";
    if (key == "transport-tempo") return "core.transport-tempo";
    if (key == "transport-beat") return "core.transport-beat";
    if (key == "transport-playing") return "core.transport-playing";
    if (key == "transport-reader") return "core.transport-reader";
    if (key == "envelope-follower") return "core.envelope-follower";
    if (key == "spectrum") return "core.spectrum-probe";
    if (key == "level-meter") return "core.level-probe";
    if (key == "oscilloscope") return "core.waveform-probe";
    if (key == "value-display") return "core.value-monitor";
    if (key == "midi-monitor") return "core.midi-monitor";
    return {};
}

} // namespace cydaw::app::detail
