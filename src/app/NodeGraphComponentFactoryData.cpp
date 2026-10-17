#include "NodeGraphComponent.h"

#include <cydaw/engine/EffectCatalogue.h>
#include "Localization.h"
#include "NodeGraphComponentInternal.h"

#include <cydaw/graph/PrimitiveRegistry.h>

#include <algorithm>
#include <cmath>

// The factory catalogue, the factory Graph presets and the factory modules are
// static data: roughly 1600 lines describing node types, preset patches and the
// reusable modules shipped with CyDAW. They were living beside the interactive
// editor, so every change to a mouse handler recompiled all of it. Splitting
// them out keeps the editor translation unit about the editor.

namespace cydaw::app {

using detail::primitiveProcessorKeyForType;

namespace {

// Each factory graph and class is built in a function of its own. Its
// nodes' payloads are a couple of kilobytes each (a Prisme's settings ride in
// every one), and the temporaries of them all in one frame came to more than
// the megabyte a thread's stack has: a host's thread, when an exported
// plug-in opens its editor.
template <typename Build>
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
void buildApart(const Build& build)
{
    build();
}

} // namespace

void NodeGraphComponent::initialiseFactoryPresets()
{
    using Parameters = cydaw::engine::NativeDspNodeParameters;
    const auto driven = [](const float amount) {
        Parameters parameters;
        parameters.driveDb = amount;
        return parameters;
    };
    const auto filtered = [](const float cutoff) {
        Parameters parameters;
        parameters.cutoffHz = cutoff;
        return parameters;
    };
    const auto delayed = [](const float milliseconds, const float feedback,
                            const float mix) {
        Parameters parameters;
        parameters.delayMilliseconds = milliseconds;
        parameters.feedback = feedback;
        parameters.mix = mix;
        return parameters;
    };
    const auto reverberated = [](const float roomSize, const float mix) {
        Parameters parameters;
        parameters.roomSize = roomSize;
        parameters.mix = mix;
        return parameters;
    };
    const auto compressed = [](const float threshold, const float ratio) {
        Parameters parameters;
        parameters.thresholdDb = threshold;
        parameters.ratio = ratio;
        return parameters;
    };
    const auto band = [](const float frequency, const float gain,
                         const float q) {
        Parameters parameters;
        parameters.bandFrequencyHz = frequency;
        parameters.bandGainDb = gain;
        parameters.bandQ = q;
        return parameters;
    };
    const auto toned = [](const float amount) {
        Parameters parameters;
        parameters.tone = amount;
        return parameters;
    };
    const auto oscillator = [](const float frequency) {
        Parameters parameters;
        parameters.oscillatorFrequencyHz = frequency;
        return parameters;
    };
    const auto noise = [](const float level) {
        Parameters parameters;
        parameters.noiseLevel = level;
        return parameters;
    };
    const auto modulator = [](const float rate, const float depth,
                              const float offset) {
        Parameters parameters;
        parameters.modulationRateHz = rate;
        parameters.modulationDepth = depth;
        parameters.modulationOffset = offset;
        return parameters;
    };
    const auto widened = [](const float width) {
        Parameters parameters;
        parameters.stereoWidth = width;
        return parameters;
    };
    const auto shaped = [](const float attack, const float decay,
                           const float sustain, const float release) {
        Parameters parameters;
        parameters.attackMilliseconds = attack;
        parameters.decayMilliseconds = decay;
        parameters.sustainLevel = sustain;
        parameters.releaseMilliseconds = release;
        return parameters;
    };
    const auto tabled = [](const float frequency, const float position,
                          const float level, const float detune) {
        Parameters parameters;
        parameters.oscillatorFrequencyHz = frequency;
        parameters.wavetablePosition = position;
        parameters.oscillatorLevel = level;
        parameters.wavetableDetuneCents = detune;
        return parameters;
    };
    const auto strung = [](const float frequency, const float damping,
                           const float decay) {
        Parameters parameters;
        parameters.oscillatorFrequencyHz = frequency;
        parameters.stringDamping = damping;
        parameters.stringDecaySeconds = decay;
        return parameters;
    };
    const auto modelled = [] {
        Parameters parameters;
        parameters.oscillatorFrequencyHz = 220.0f;
        parameters.oscillatorLevel = 1.0f;
        return parameters;
    };
    const auto sampled = [] {
        Parameters parameters;
        parameters.oscillatorFrequencyHz = 220.0f;
        parameters.oscillatorLevel = 1.0f;
        return parameters;
    };
    // The simple sampler: one sample over the keys, its waveform on the card.
    const auto simpled = [] {
        Parameters parameters;
        parameters.oscillatorFrequencyHz = 220.0f;
        parameters.oscillatorLevel = 1.0f;
        parameters.sampler[static_cast<std::size_t>(cydaw::engine::SamplerSetting::simple)] = 1.0f;
        return parameters;
    };
    // Nuée: one sample over the keys, read as grains.
    const auto clouded = [] {
        Parameters parameters;
        parameters.oscillatorFrequencyHz = 220.0f;
        parameters.oscillatorLevel = 1.0f;
        parameters.sampler[static_cast<std::size_t>(cydaw::engine::SamplerSetting::simple)] = 1.0f;
        parameters.sampler[static_cast<std::size_t>(cydaw::engine::SamplerSetting::grains)] = 1.0f;
        parameters.sampler[static_cast<std::size_t>(cydaw::engine::SamplerSetting::loop)] = 0.0f;
        parameters.sampler[static_cast<std::size_t>(cydaw::engine::SamplerSetting::grainScan)] = 0.05f;
        parameters.sampler[static_cast<std::size_t>(cydaw::engine::SamplerSetting::grainSpray)] = 0.08f;
        parameters.sampler[static_cast<std::size_t>(cydaw::engine::SamplerSetting::grainSize)] = 160.0f;
        parameters.sampler[static_cast<std::size_t>(cydaw::engine::SamplerSetting::grainDensity)] = 28.0f;
        parameters.sampler[static_cast<std::size_t>(cydaw::engine::SamplerSetting::grainPitch)] = 0.1f;
        parameters.sampler[static_cast<std::size_t>(cydaw::engine::SamplerSetting::grainStereo)] = 0.7f;
        return parameters;
    };
    // A Batterie: a sampler whose every hit plays to its end, at its own pitch.
    const auto drummed = [] {
        Parameters parameters;
        parameters.oscillatorFrequencyHz = 220.0f;
        parameters.oscillatorLevel = 1.0f;
        parameters.sampler[static_cast<std::size_t>(cydaw::engine::SamplerSetting::oneShot)] = 1.0f;
        parameters.sampler[static_cast<std::size_t>(cydaw::engine::SamplerSetting::keyTrack)] = 0.0f;
        parameters.sampler[static_cast<std::size_t>(cydaw::engine::SamplerSetting::velocityDb)] = 10.0f;
        return parameters;
    };
    const auto prismed = [] {
        Parameters parameters;
        parameters.oscillatorFrequencyHz = 220.0f;
        parameters.oscillatorLevel = 1.0f;
        return parameters;
    };
    const auto ambred = [] {
        Parameters parameters;
        parameters.oscillatorFrequencyHz = 220.0f;
        parameters.oscillatorLevel = 1.0f;
        parameters.synthModel = static_cast<float>(cydaw::engine::synth::Model::ambre);
        parameters.synth = cydaw::engine::synth::defaultValues(cydaw::engine::synth::Model::ambre);
        return parameters;
    };
    const auto colossed = [] {
        Parameters parameters;
        parameters.oscillatorFrequencyHz = 220.0f;
        parameters.oscillatorLevel = 1.0f;
        parameters.synthModel = static_cast<float>(cydaw::engine::synth::Model::colosse);
        parameters.synth = cydaw::engine::synth::defaultValues(cydaw::engine::synth::Model::colosse);
        return parameters;
    };
    const auto mosaiqued = [] {
        Parameters parameters;
        parameters.oscillatorFrequencyHz = 220.0f;
        parameters.oscillatorLevel = 1.0f;
        parameters.synthModel = static_cast<float>(cydaw::engine::synth::Model::mosaique);
        parameters.synth = cydaw::engine::synth::defaultValues(cydaw::engine::synth::Model::mosaique);
        return parameters;
    };
    const auto nebuleused = [] {
        Parameters parameters;
        parameters.oscillatorFrequencyHz = 220.0f;
        parameters.oscillatorLevel = 1.0f;
        parameters.synthModel = static_cast<float>(cydaw::engine::synth::Model::nebuleuse);
        parameters.synth = cydaw::engine::synth::defaultValues(cydaw::engine::synth::Model::nebuleuse);
        return parameters;
    };
    const auto opaled = [] {
        Parameters parameters;
        parameters.oscillatorFrequencyHz = 220.0f;
        parameters.oscillatorLevel = 1.0f;
        parameters.synthModel = static_cast<float>(cydaw::engine::synth::Model::opale);
        parameters.synth = cydaw::engine::synth::defaultValues(cydaw::engine::synth::Model::opale);
        return parameters;
    };
    const auto onyxd = [] {
        Parameters parameters;
        parameters.oscillatorFrequencyHz = 220.0f;
        parameters.oscillatorLevel = 1.0f;
        parameters.synthModel = static_cast<float>(cydaw::engine::synth::Model::onyx);
        parameters.synth = cydaw::engine::synth::defaultValues(cydaw::engine::synth::Model::onyx);
        return parameters;
    };
    const auto tiretted = [] {
        Parameters parameters;
        parameters.oscillatorFrequencyHz = 220.0f;
        parameters.oscillatorLevel = 1.0f;
        parameters.synthModel = static_cast<float>(cydaw::engine::synth::Model::tirette);
        parameters.synth = cydaw::engine::synth::defaultValues(cydaw::engine::synth::Model::tirette);
        return parameters;
    };
    const auto alliaged = [] {
        Parameters parameters;
        parameters.oscillatorFrequencyHz = 220.0f;
        parameters.oscillatorLevel = 1.0f;
        parameters.synthModel = static_cast<float>(cydaw::engine::synth::Model::alliage);
        parameters.synth = cydaw::engine::synth::defaultValues(cydaw::engine::synth::Model::alliage);
        return parameters;
    };
    const auto quartzd = [] {
        Parameters parameters;
        parameters.oscillatorFrequencyHz = 220.0f;
        parameters.oscillatorLevel = 1.0f;
        parameters.synthModel = static_cast<float>(cydaw::engine::synth::Model::quartz);
        parameters.synth = cydaw::engine::synth::defaultValues(cydaw::engine::synth::Model::quartz);
        return parameters;
    };
    const auto argiled = [] {
        Parameters parameters;
        parameters.oscillatorFrequencyHz = 220.0f;
        parameters.oscillatorLevel = 1.0f;
        parameters.synthModel = static_cast<float>(cydaw::engine::synth::Model::argile);
        parameters.synth = cydaw::engine::synth::defaultValues(cydaw::engine::synth::Model::argile);
        return parameters;
    };
    const auto saphird = [] {
        Parameters parameters;
        parameters.oscillatorFrequencyHz = 220.0f;
        parameters.oscillatorLevel = 1.0f;
        parameters.synthModel = static_cast<float>(cydaw::engine::synth::Model::saphir);
        parameters.synth = cydaw::engine::synth::defaultValues(cydaw::engine::synth::Model::saphir);
        return parameters;
    };
    const auto derived = [] {
        Parameters parameters;
        parameters.oscillatorFrequencyHz = 220.0f;
        parameters.oscillatorLevel = 1.0f;
        parameters.synthModel = static_cast<float>(cydaw::engine::synth::Model::derive);
        parameters.synth = cydaw::engine::synth::defaultValues(cydaw::engine::synth::Model::derive);
        return parameters;
    };
    const auto bronzed = [] {
        Parameters parameters;
        parameters.oscillatorFrequencyHz = 220.0f;
        parameters.oscillatorLevel = 1.0f;
        parameters.synthModel = static_cast<float>(cydaw::engine::synth::Model::bronze);
        parameters.synth = cydaw::engine::synth::defaultValues(cydaw::engine::synth::Model::bronze);
        return parameters;
    };
    const auto silexd = [] {
        Parameters parameters;
        parameters.oscillatorFrequencyHz = 220.0f;
        parameters.oscillatorLevel = 1.0f;
        parameters.synthModel = static_cast<float>(cydaw::engine::synth::Model::silex);
        parameters.synth = cydaw::engine::synth::defaultValues(cydaw::engine::synth::Model::silex);
        return parameters;
    };
    const auto jaded = [] {
        Parameters parameters;
        parameters.oscillatorFrequencyHz = 220.0f;
        parameters.oscillatorLevel = 1.0f;
        parameters.synthModel = static_cast<float>(cydaw::engine::synth::Model::jade);
        parameters.synth = cydaw::engine::synth::defaultValues(cydaw::engine::synth::Model::jade);
        return parameters;
    };
    // Halo after it: a long, wide space by default.
    const auto haloed = [] {
        Parameters parameters;
        if (const auto* effect = cydaw::engine::effectForType("halo"))
            cydaw::engine::applyEffectDefaults(*effect, parameters);
        parameters.effect1 = 0.28f;
        parameters.effect2 = 4.5f;
        parameters.effect4 = 0.8f;
        return parameters;
    };
    const auto kicked = [] {
        Parameters parameters;
        parameters.oscillatorFrequencyHz = 65.0f;
        parameters.oscillatorLevel = 1.0f;
        return parameters;
    };
    // One note at a time: a kick cuts the one before it.
    const auto monophonic = [] {
        Parameters parameters;
        parameters.voiceMode = 1.0f;
        parameters.glideMilliseconds = 0.0f;
        return parameters;
    };
    const auto makeup = [](const float gain) {
        Parameters parameters;
        parameters.gainDb = gain;
        return parameters;
    };
    const auto clipped = [](const float threshold) {
        Parameters parameters;
        parameters.clipThreshold = threshold;
        return parameters;
    };
    const auto highPassed = [](const float cutoff) {
        Parameters parameters;
        parameters.highpassCutoffHz = cutoff;
        return parameters;
    };
    const auto mapped = [](const float inputMinimum,
                           const float inputMaximum,
                           const float outputMinimum,
                           const float outputMaximum) {
        Parameters parameters;
        parameters.mapInputMinimum = inputMinimum;
        parameters.mapInputMaximum = inputMaximum;
        parameters.mapOutputMinimum = outputMinimum;
        parameters.mapOutputMaximum = outputMaximum;
        return parameters;
    };
    factoryPresets_.clear();
    buildApart([&] {
        factoryPresets_.push_back({ "poly-graph-synth", "nodes.preset.polyGraphSynth",
              { { "midi-input", { 0.02f, 0.42f } },
                { "poly-sine", { 0.36f, 0.42f } },
                { "master", { 0.86f, 0.42f } } },
              { { 0, 1 }, { 1, 2 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "clean-channel", "nodes.preset.cleanChannel",
              { { "audio-clips", { 0.04f, 0.44f } },
                { "gain-pan", { 0.38f, 0.44f } },
                { "spectrum", { 0.62f, 0.44f } },
                { "master", { 0.84f, 0.44f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "warm-channel", "nodes.preset.warmChannel",
              { { "audio-clips", { 0.03f, 0.44f } },
                { "drive", { 0.21f, 0.44f }, driven(9.0f) },
                { "lowpass", { 0.39f, 0.44f }, filtered(8'500.0f) },
                { "gain-pan", { 0.57f, 0.44f } },
                { "spectrum", { 0.72f, 0.44f } },
                { "master", { 0.86f, 0.44f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 }, { 4, 5 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "studio-polish", "nodes.preset.studioPolish",
              { { "audio-clips", { 0.03f, 0.44f } },
                { "compressor", { 0.21f, 0.44f }, compressed(-18.0f, 3.0f) },
                { "eq", { 0.39f, 0.44f }, toned(0.12f) },
                { "gain-pan", { 0.57f, 0.44f } },
                { "spectrum", { 0.72f, 0.44f } },
                { "master", { 0.86f, 0.44f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 }, { 4, 5 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "vocal-focus", "nodes.preset.vocalFocus",
              { { "audio-clips", { 0.05f, 0.44f } },
                { "vocal-strip", { 0.31f, 0.44f } },
                { "spectrum", { 0.61f, 0.44f } },
                { "master", { 0.84f, 0.44f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "reverb-send", "nodes.preset.reverbSend",
              { { "audio-clips", { 0.03f, 0.44f } },
                { "eq", { 0.21f, 0.44f }, toned(-0.08f) },
                { "reverb", { 0.39f, 0.44f }, reverberated(0.72f, 0.38f) },
                { "gain-pan", { 0.57f, 0.44f } },
                { "spectrum", { 0.72f, 0.44f } },
                { "master", { 0.86f, 0.44f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 }, { 4, 5 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "delay-space", "nodes.preset.delaySpace",
              { { "audio-clips", { 0.03f, 0.44f } },
                { "delay", { 0.21f, 0.44f }, delayed(320.0f, 0.38f, 0.32f) },
                { "reverb", { 0.39f, 0.44f }, reverberated(0.76f, 0.24f) },
                { "lowpass", { 0.57f, 0.44f }, filtered(6'500.0f) },
                { "spectrum", { 0.72f, 0.44f } },
                { "master", { 0.86f, 0.44f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 }, { 4, 5 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "parallel-ambience", "nodes.preset.parallelAmbience",
              { { "audio-clips", { 0.04f, 0.44f } },
                { "gain-pan", { 0.36f, 0.24f } },
                { "reverb-rack", { 0.36f, 0.66f }, reverberated(0.78f, 0.34f) },
                { "spectrum", { 0.62f, 0.66f } },
                { "audio-sum", { 0.70f, 0.44f } },
                { "master", { 0.86f, 0.44f } } },
              { { 0, 1 }, { 0, 2 }, { 2, 3 }, { 1, 4 }, { 3, 4 },
                { 4, 5 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "rhythmic-echo", "nodes.preset.rhythmicEcho",
              { { "audio-clips", { 0.03f, 0.44f } },
                { "compressor", { 0.21f, 0.44f }, compressed(-20.0f, 5.0f) },
                { "delay", { 0.39f, 0.44f }, delayed(375.0f, 0.56f, 0.34f) },
                { "eq", { 0.57f, 0.44f }, toned(0.22f) },
                { "spectrum", { 0.72f, 0.44f } },
                { "master", { 0.86f, 0.44f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 }, { 4, 5 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "generator-drone", "nodes.preset.generatorDrone",
              { { "oscillator", { 0.08f, 0.44f }, oscillator(110.0f) },
                { "lowpass", { 0.31f, 0.44f }, filtered(1'800.0f) },
                { "reverb", { 0.52f, 0.44f }, reverberated(0.85f, 0.40f) },
                { "spectrum", { 0.69f, 0.44f } },
                { "master", { 0.86f, 0.44f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "noise-atmosphere", "nodes.preset.noiseAtmosphere",
              { { "noise", { 0.03f, 0.44f }, noise(0.08f) },
                { "lowpass", { 0.21f, 0.44f }, filtered(3'600.0f) },
                { "delay", { 0.39f, 0.44f }, delayed(520.0f, 0.64f, 0.40f) },
                { "reverb", { 0.57f, 0.44f }, reverberated(0.88f, 0.42f) },
                { "spectrum", { 0.72f, 0.44f } },
                { "master", { 0.86f, 0.44f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 }, { 4, 5 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "lfo-tremolo", "nodes.preset.lfoTremolo",
              { { "graph-audio-input-2", { 0.03f, 0.36f } },
                { "lfo-core", { 0.18f, 0.70f },
                  modulator(2.0f, 1.0f, 0.0f) },
                { "map-range", { 0.42f, 0.70f },
                  mapped(-1.0f, 1.0f, -30.0f, 0.0f) },
                { "gain", { 0.43f, 0.36f } },
                { "spectrum", { 0.70f, 0.36f } },
                { "graph-audio-output-2", { 0.96f, 0.36f } } },
              { { 0, 3 }, { 1, 2 }, { 2, 3, 2, 2 }, { 3, 4 }, { 4, 5 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "stereo-widener", "nodes.preset.stereoWidener",
              { { "graph-audio-input-2", { 0.03f, 0.42f } },
                { "stereo-width", { 0.28f, 0.42f }, widened(1.45f) },
                { "dc-blocker", { 0.50f, 0.42f } },
                { "level-meter", { 0.72f, 0.42f } },
                { "graph-audio-output-2", { 0.96f, 0.42f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "lofi-crusher", "nodes.preset.lofiCrusher",
              { { "graph-audio-input-2", { 0.03f, 0.42f } },
                { "drive", { 0.22f, 0.42f }, driven(16.0f) },
                { "hard-clip", { 0.42f, 0.42f }, clipped(0.55f) },
                { "lowpass", { 0.62f, 0.42f }, filtered(3'200.0f) },
                { "gain", { 0.80f, 0.42f } },
                { "graph-audio-output-2", { 0.96f, 0.42f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 }, { 4, 5 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "bass-tightener", "nodes.preset.bassTightener",
              { { "graph-audio-input-2", { 0.03f, 0.42f } },
                { "highpass", { 0.22f, 0.42f }, highPassed(85.0f) },
                { "compressor", { 0.44f, 0.42f }, compressed(-14.0f, 4.0f) },
                { "eq", { 0.64f, 0.42f }, toned(0.10f) },
                { "level-meter", { 0.82f, 0.42f } },
                { "graph-audio-output-2", { 0.96f, 0.42f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 }, { 4, 5 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "auto-wah", "nodes.preset.autoWah",
              { { "graph-audio-input-2", { 0.03f, 0.34f } },
                { "envelope-follower", { 0.22f, 0.74f } },
                { "map-range", { 0.46f, 0.74f },
                  mapped(0.0f, 1.0f, 320.0f, 4'800.0f) },
                { "lowpass", { 0.52f, 0.34f }, filtered(900.0f) },
                { "level-meter", { 0.78f, 0.34f } },
                { "graph-audio-output-2", { 0.96f, 0.34f } } },
              { { 0, 1 }, { 0, 3 }, { 1, 2 }, { 2, 3, 2, 2 }, { 3, 4 }, { 4, 5 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "cascade-echo", "nodes.preset.cascadeEcho",
              { { "graph-audio-input-2", { 0.03f, 0.42f } },
                { "delay", { 0.24f, 0.42f }, delayed(170.0f, 0.24f, 0.30f) },
                { "delay", { 0.46f, 0.42f }, delayed(340.0f, 0.18f, 0.24f) },
                { "stereo-width", { 0.68f, 0.42f }, widened(1.25f) },
                { "graph-audio-output-2", { 0.96f, 0.42f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "master-bus", "nodes.preset.masterBus",
              { { "graph-audio-input-2", { 0.03f, 0.42f } },
                { "compressor", { 0.22f, 0.42f }, compressed(-10.0f, 2.5f) },
                { "eq", { 0.42f, 0.42f }, toned(0.06f) },
                { "stereo-width", { 0.60f, 0.42f }, widened(1.12f) },
                { "level-meter", { 0.78f, 0.42f } },
                { "graph-audio-output-2", { 0.96f, 0.42f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 }, { 4, 5 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "synth-lead", "nodes.preset.synthLead",
              { { "midi-input", { 0.03f, 0.42f } },
                { "poly-lead", { 0.32f, 0.42f } },
                { "delay", { 0.62f, 0.42f }, delayed(180.0f, 0.22f, 0.20f) },
                { "master", { 0.90f, 0.42f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "synth-pad", "nodes.preset.synthPad",
              { { "midi-input", { 0.03f, 0.42f } },
                { "poly-pad", { 0.32f, 0.42f } },
                { "reverb", { 0.62f, 0.42f }, reverberated(0.82f, 0.42f) },
                { "master", { 0.90f, 0.42f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "synth-bass", "nodes.preset.synthBass",
              { { "midi-input", { 0.03f, 0.42f } },
                { "poly-bass", { 0.26f, 0.42f } },
                { "drive", { 0.52f, 0.42f }, driven(6.0f) },
                { "lowpass", { 0.72f, 0.42f }, filtered(2'400.0f) },
                { "master", { 0.92f, 0.42f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "synth-pluck", "nodes.preset.synthPluck",
              { { "midi-input", { 0.03f, 0.42f } },
                { "poly-pluck", { 0.32f, 0.42f } },
                { "wide-echo", { 0.62f, 0.42f } },
                { "master", { 0.90f, 0.42f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "synth-keys", "nodes.preset.synthKeys",
              { { "midi-input", { 0.03f, 0.42f } },
                { "poly-keys", { 0.32f, 0.42f } },
                { "stereo-finisher", { 0.62f, 0.42f } },
                { "master", { 0.90f, 0.42f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "stage-organ", "nodes.preset.stageOrgan",
              { { "midi-input", { 0.03f, 0.42f } },
                { "electric-organ", { 0.32f, 0.42f } },
                { "reverb", { 0.62f, 0.42f }, reverberated(0.55f, 0.24f) },
                { "master", { 0.90f, 0.42f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "bell-tines", "nodes.preset.bellTines",
              { { "midi-input", { 0.03f, 0.42f } },
                { "percussive-bell", { 0.32f, 0.42f } },
                { "dark-room", { 0.62f, 0.42f } },
                { "master", { 0.90f, 0.42f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "glass-atmosphere", "nodes.preset.glassAtmosphere",
              { { "midi-input", { 0.03f, 0.42f } },
                { "ambient-glass", { 0.32f, 0.42f } },
                { "reverb", { 0.62f, 0.42f }, reverberated(0.90f, 0.52f) },
                { "master", { 0.90f, 0.42f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "arpeggiated-pluck", "nodes.preset.arpeggiatedPluck",
              { { "midi-input", { 0.03f, 0.42f } },
                { "arp-motion", { 0.26f, 0.42f } },
                { "poly-pluck", { 0.50f, 0.42f } },
                { "echo-space", { 0.74f, 0.42f } },
                { "master", { 0.94f, 0.42f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "table-synth", "nodes.preset.tableSynth",
              { { "midi-input", { 0.03f, 0.42f } },
                { "voice-allocator", { 0.18f, 0.42f } },
                { "wavetable", { 0.36f, 0.20f }, tabled(220.0f, 0.48f, 0.16f, 12.0f) },
                { "envelope", { 0.36f, 0.72f }, shaped(6.0f, 180.0f, 0.68f, 320.0f) },
                { "gain", { 0.56f, 0.42f } },
                { "voice-filter", { 0.74f, 0.42f }, filtered(9'000.0f) },
                { "master", { 0.94f, 0.42f } } },
              { { 0, 1 }, { 1, 2, 2, 2 }, { 1, 3, 3, 1 }, { 2, 4, 1, 1 },
                { 3, 4, 2, 2 }, { 4, 5, 3, 1 }, { 5, 6 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "string-keys", "nodes.preset.stringKeys",
              { { "midi-input", { 0.03f, 0.42f } },
                { "voice-allocator", { 0.18f, 0.42f } },
                { "string", { 0.36f, 0.20f }, strung(220.0f, 0.30f, 5.0f) },
                { "envelope", { 0.36f, 0.72f }, shaped(1.0f, 2'000.0f, 0.55f, 900.0f) },
                { "gain", { 0.56f, 0.42f } },
                { "voice-filter", { 0.74f, 0.42f }, filtered(7'500.0f) },
                { "master", { 0.94f, 0.42f } } },
              { { 0, 1 }, { 1, 2, 2, 3 }, { 1, 3, 3, 1 }, { 2, 4, 2, 1 },
                { 3, 4, 2, 2 }, { 4, 5, 3, 1 }, { 5, 6 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "sound-model-synth", "nodes.preset.soundModelSynth",
              { { "midi-input", { 0.03f, 0.42f } },
                { "voice-allocator", { 0.18f, 0.42f } },
                { "sound-model", { 0.36f, 0.20f }, modelled() },
                { "envelope", { 0.36f, 0.72f }, shaped(1.0f, 1.0f, 1.0f, 8'000.0f) },
                { "gain", { 0.56f, 0.42f } },
                { "voice-filter", { 0.74f, 0.42f }, filtered(20'000.0f) },
                { "master", { 0.94f, 0.42f } } },
              { { 0, 1 }, { 1, 2, 2, 2 }, { 1, 3, 3, 1 }, { 2, 4, 1, 1 },
                { 3, 4, 2, 2 }, { 4, 5, 3, 1 }, { 5, 6 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "batterie-instrument", "nodes.preset.batterieInstrument",
              { { "midi-input", { 0.03f, 0.42f } },
                { "voice-allocator", { 0.18f, 0.42f } },
                { "batterie", { 0.36f, 0.20f }, drummed() },
                { "envelope", { 0.36f, 0.72f }, shaped(1.0f, 1.0f, 1.0f, 300.0f) },
                { "gain", { 0.56f, 0.42f } },
                { "voice-filter", { 0.74f, 0.42f }, filtered(20'000.0f) },
                { "master", { 0.94f, 0.42f } } },
              { { 0, 1 }, { 1, 2, 2, 2 }, { 1, 3, 3, 1 }, { 2, 4, 1, 1 },
                { 3, 4, 2, 2 }, { 4, 5, 3, 1 }, { 5, 6 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "sampler-instrument", "nodes.preset.samplerInstrument",
              { { "midi-input", { 0.03f, 0.42f } },
                { "voice-allocator", { 0.18f, 0.42f } },
                { "sampler", { 0.36f, 0.20f }, sampled() },
                { "envelope", { 0.36f, 0.72f }, shaped(1.0f, 1.0f, 1.0f, 350.0f) },
                { "gain", { 0.56f, 0.42f } },
                { "voice-filter", { 0.74f, 0.42f }, filtered(20'000.0f) },
                { "master", { 0.94f, 0.42f } } },
              { { 0, 1 }, { 1, 2, 2, 2 }, { 1, 3, 3, 1 }, { 2, 4, 1, 1 },
                { 3, 4, 2, 2 }, { 4, 5, 3, 1 }, { 5, 6 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "simple-sampler-instrument", "nodes.preset.simpleSamplerInstrument",
              { { "midi-input", { 0.03f, 0.42f } },
                { "voice-allocator", { 0.18f, 0.42f } },
                { "sampler", { 0.36f, 0.20f }, simpled() },
                { "envelope", { 0.36f, 0.72f }, shaped(1.0f, 1.0f, 1.0f, 200.0f) },
                { "gain", { 0.56f, 0.42f } },
                { "voice-filter", { 0.74f, 0.42f }, filtered(20'000.0f) },
                { "master", { 0.94f, 0.42f } } },
              { { 0, 1 }, { 1, 2, 2, 2 }, { 1, 3, 3, 1 }, { 2, 4, 1, 1 },
                { 3, 4, 2, 2 }, { 4, 5, 3, 1 }, { 5, 6 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "nuee-instrument", "nodes.preset.nueeInstrument",
              { { "midi-input", { 0.03f, 0.42f } },
                { "voice-allocator", { 0.16f, 0.42f } },
                { "sampler", { 0.32f, 0.20f }, clouded() },
                { "envelope", { 0.32f, 0.72f }, shaped(180.0f, 400.0f, 1.0f, 1'400.0f) },
                { "gain", { 0.48f, 0.42f } },
                { "voice-filter", { 0.62f, 0.42f }, filtered(20'000.0f) },
                { "halo", { 0.78f, 0.42f }, haloed() },
                { "master", { 0.94f, 0.42f } } },
              { { 0, 1 }, { 1, 2, 2, 2 }, { 1, 3, 3, 1 }, { 2, 4, 1, 1 },
                { 3, 4, 2, 2 }, { 4, 5, 3, 1 }, { 5, 6 }, { 6, 7 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "prism-instrument", "nodes.preset.prismInstrument",
              { { "midi-input", { 0.03f, 0.42f } },
                { "voice-allocator", { 0.18f, 0.42f } },
                { "prism-synth", { 0.36f, 0.20f }, prismed() },
                { "envelope", { 0.36f, 0.72f }, shaped(0.1f, 1.0f, 1.0f, 8'000.0f) },
                { "gain", { 0.56f, 0.42f } },
                { "voice-filter", { 0.74f, 0.42f }, filtered(20'000.0f) },
                { "master", { 0.94f, 0.42f } } },
              { { 0, 1 }, { 1, 2, 2, 2 }, { 1, 3, 3, 1 }, { 2, 4, 1, 1 },
                { 3, 4, 2, 2 }, { 4, 5, 3, 1 }, { 5, 6 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "ambre-instrument", "nodes.preset.ambreInstrument",
              { { "midi-input", { 0.03f, 0.42f } },
                { "voice-allocator", { 0.18f, 0.42f } },
                { "ambre-synth", { 0.36f, 0.20f }, ambred() },
                { "envelope", { 0.36f, 0.72f }, shaped(0.1f, 1.0f, 1.0f, 8'000.0f) },
                { "gain", { 0.56f, 0.42f } },
                { "voice-filter", { 0.74f, 0.42f }, filtered(20'000.0f) },
                { "master", { 0.94f, 0.42f } } },
              { { 0, 1 }, { 1, 2, 2, 2 }, { 1, 3, 3, 1 }, { 2, 4, 1, 1 },
                { 3, 4, 2, 2 }, { 4, 5, 3, 1 }, { 5, 6 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "colosse-instrument", "nodes.preset.colosseInstrument",
              { { "midi-input", { 0.03f, 0.42f } },
                { "voice-allocator", { 0.18f, 0.42f } },
                { "colosse-synth", { 0.36f, 0.20f }, colossed() },
                { "envelope", { 0.36f, 0.72f }, shaped(0.1f, 1.0f, 1.0f, 8'000.0f) },
                { "gain", { 0.56f, 0.42f } },
                { "voice-filter", { 0.74f, 0.42f }, filtered(20'000.0f) },
                { "master", { 0.94f, 0.42f } } },
              { { 0, 1 }, { 1, 2, 2, 2 }, { 1, 3, 3, 1 }, { 2, 4, 1, 1 },
                { 3, 4, 2, 2 }, { 4, 5, 3, 1 }, { 5, 6 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "mosaique-instrument", "nodes.preset.mosaiqueInstrument",
              { { "midi-input", { 0.03f, 0.42f } },
                { "voice-allocator", { 0.18f, 0.42f } },
                { "mosaique-synth", { 0.36f, 0.20f }, mosaiqued() },
                { "envelope", { 0.36f, 0.72f }, shaped(0.1f, 1.0f, 1.0f, 8'000.0f) },
                { "gain", { 0.56f, 0.42f } },
                { "voice-filter", { 0.74f, 0.42f }, filtered(20'000.0f) },
                { "master", { 0.94f, 0.42f } } },
              { { 0, 1 }, { 1, 2, 2, 2 }, { 1, 3, 3, 1 }, { 2, 4, 1, 1 },
                { 3, 4, 2, 2 }, { 4, 5, 3, 1 }, { 5, 6 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "nebuleuse-instrument", "nodes.preset.nebuleuseInstrument",
              { { "midi-input", { 0.03f, 0.42f } },
                { "voice-allocator", { 0.16f, 0.42f } },
                { "nebuleuse-synth", { 0.32f, 0.20f }, nebuleused() },
                { "envelope", { 0.32f, 0.72f }, shaped(0.1f, 1.0f, 1.0f, 8'000.0f) },
                { "gain", { 0.48f, 0.42f } },
                { "voice-filter", { 0.62f, 0.42f }, filtered(20'000.0f) },
                { "halo", { 0.78f, 0.42f }, haloed() },
                { "master", { 0.94f, 0.42f } } },
              { { 0, 1 }, { 1, 2, 2, 2 }, { 1, 3, 3, 1 }, { 2, 4, 1, 1 },
                { 3, 4, 2, 2 }, { 4, 5, 3, 1 }, { 5, 6 }, { 6, 7 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "opale-instrument", "nodes.preset.opaleInstrument",
              { { "midi-input", { 0.03f, 0.42f } },
                { "voice-allocator", { 0.18f, 0.42f } },
                { "opale-synth", { 0.36f, 0.20f }, opaled() },
                { "envelope", { 0.36f, 0.72f }, shaped(0.1f, 1.0f, 1.0f, 8'000.0f) },
                { "gain", { 0.56f, 0.42f } },
                { "voice-filter", { 0.74f, 0.42f }, filtered(20'000.0f) },
                { "master", { 0.94f, 0.42f } } },
              { { 0, 1 }, { 1, 2, 2, 2 }, { 1, 3, 3, 1 }, { 2, 4, 1, 1 },
                { 3, 4, 2, 2 }, { 4, 5, 3, 1 }, { 5, 6 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "onyx-instrument", "nodes.preset.onyxInstrument",
              { { "midi-input", { 0.03f, 0.42f } },
                { "voice-allocator", { 0.18f, 0.42f } },
                { "onyx-synth", { 0.36f, 0.20f }, onyxd() },
                { "envelope", { 0.36f, 0.72f }, shaped(0.1f, 1.0f, 1.0f, 8'000.0f) },
                { "gain", { 0.56f, 0.42f } },
                { "voice-filter", { 0.74f, 0.42f }, filtered(20'000.0f) },
                { "master", { 0.94f, 0.42f } } },
              { { 0, 1 }, { 1, 2, 2, 2 }, { 1, 3, 3, 1 }, { 2, 4, 1, 1 },
                { 3, 4, 2, 2 }, { 4, 5, 3, 1 }, { 5, 6 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "tirette-instrument", "nodes.preset.tiretteInstrument",
              { { "midi-input", { 0.03f, 0.42f } },
                { "voice-allocator", { 0.18f, 0.42f } },
                { "tirette-synth", { 0.36f, 0.20f }, tiretted() },
                { "envelope", { 0.36f, 0.72f }, shaped(0.1f, 1.0f, 1.0f, 8'000.0f) },
                { "gain", { 0.56f, 0.42f } },
                { "voice-filter", { 0.74f, 0.42f }, filtered(20'000.0f) },
                { "master", { 0.94f, 0.42f } } },
              { { 0, 1 }, { 1, 2, 2, 2 }, { 1, 3, 3, 1 }, { 2, 4, 1, 1 },
                { 3, 4, 2, 2 }, { 4, 5, 3, 1 }, { 5, 6 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "alliage-instrument", "nodes.preset.alliageInstrument",
              { { "midi-input", { 0.03f, 0.42f } },
                { "voice-allocator", { 0.18f, 0.42f } },
                { "alliage-synth", { 0.36f, 0.20f }, alliaged() },
                { "envelope", { 0.36f, 0.72f }, shaped(0.1f, 1.0f, 1.0f, 8'000.0f) },
                { "gain", { 0.56f, 0.42f } },
                { "voice-filter", { 0.74f, 0.42f }, filtered(20'000.0f) },
                { "master", { 0.94f, 0.42f } } },
              { { 0, 1 }, { 1, 2, 2, 2 }, { 1, 3, 3, 1 }, { 2, 4, 1, 1 },
                { 3, 4, 2, 2 }, { 4, 5, 3, 1 }, { 5, 6 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "quartz-instrument", "nodes.preset.quartzInstrument",
              { { "midi-input", { 0.03f, 0.42f } },
                { "voice-allocator", { 0.18f, 0.42f } },
                { "quartz-synth", { 0.36f, 0.20f }, quartzd() },
                { "envelope", { 0.36f, 0.72f }, shaped(0.1f, 1.0f, 1.0f, 8'000.0f) },
                { "gain", { 0.56f, 0.42f } },
                { "voice-filter", { 0.74f, 0.42f }, filtered(20'000.0f) },
                { "master", { 0.94f, 0.42f } } },
              { { 0, 1 }, { 1, 2, 2, 2 }, { 1, 3, 3, 1 }, { 2, 4, 1, 1 },
                { 3, 4, 2, 2 }, { 4, 5, 3, 1 }, { 5, 6 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "argile-instrument", "nodes.preset.argileInstrument",
              { { "midi-input", { 0.03f, 0.42f } },
                { "voice-allocator", { 0.18f, 0.42f } },
                { "argile-synth", { 0.36f, 0.20f }, argiled() },
                { "envelope", { 0.36f, 0.72f }, shaped(0.1f, 1.0f, 1.0f, 8'000.0f) },
                { "gain", { 0.56f, 0.42f } },
                { "voice-filter", { 0.74f, 0.42f }, filtered(20'000.0f) },
                { "master", { 0.94f, 0.42f } } },
              { { 0, 1 }, { 1, 2, 2, 2 }, { 1, 3, 3, 1 }, { 2, 4, 1, 1 },
                { 3, 4, 2, 2 }, { 4, 5, 3, 1 }, { 5, 6 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "saphir-instrument", "nodes.preset.saphirInstrument",
              { { "midi-input", { 0.03f, 0.42f } },
                { "voice-allocator", { 0.18f, 0.42f } },
                { "saphir-synth", { 0.36f, 0.20f }, saphird() },
                { "envelope", { 0.36f, 0.72f }, shaped(0.1f, 1.0f, 1.0f, 8'000.0f) },
                { "gain", { 0.56f, 0.42f } },
                { "voice-filter", { 0.74f, 0.42f }, filtered(20'000.0f) },
                { "master", { 0.94f, 0.42f } } },
              { { 0, 1 }, { 1, 2, 2, 2 }, { 1, 3, 3, 1 }, { 2, 4, 1, 1 },
                { 3, 4, 2, 2 }, { 4, 5, 3, 1 }, { 5, 6 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "derive-instrument", "nodes.preset.deriveInstrument",
              { { "midi-input", { 0.03f, 0.42f } },
                { "voice-allocator", { 0.18f, 0.42f } },
                { "derive-synth", { 0.36f, 0.20f }, derived() },
                { "envelope", { 0.36f, 0.72f }, shaped(0.1f, 1.0f, 1.0f, 8'000.0f) },
                { "gain", { 0.56f, 0.42f } },
                { "voice-filter", { 0.74f, 0.42f }, filtered(20'000.0f) },
                { "master", { 0.94f, 0.42f } } },
              { { 0, 1 }, { 1, 2, 2, 2 }, { 1, 3, 3, 1 }, { 2, 4, 1, 1 },
                { 3, 4, 2, 2 }, { 4, 5, 3, 1 }, { 5, 6 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "bronze-instrument", "nodes.preset.bronzeInstrument",
              { { "midi-input", { 0.03f, 0.42f } },
                { "voice-allocator", { 0.18f, 0.42f } },
                { "bronze-synth", { 0.36f, 0.20f }, bronzed() },
                { "envelope", { 0.36f, 0.72f }, shaped(0.1f, 1.0f, 1.0f, 8'000.0f) },
                { "gain", { 0.56f, 0.42f } },
                { "voice-filter", { 0.74f, 0.42f }, filtered(20'000.0f) },
                { "master", { 0.94f, 0.42f } } },
              { { 0, 1 }, { 1, 2, 2, 2 }, { 1, 3, 3, 1 }, { 2, 4, 1, 1 },
                { 3, 4, 2, 2 }, { 4, 5, 3, 1 }, { 5, 6 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "silex-instrument", "nodes.preset.silexInstrument",
              { { "midi-input", { 0.03f, 0.42f } },
                { "voice-allocator", { 0.18f, 0.42f } },
                { "silex-synth", { 0.36f, 0.20f }, silexd() },
                { "envelope", { 0.36f, 0.72f }, shaped(0.1f, 1.0f, 1.0f, 8'000.0f) },
                { "gain", { 0.56f, 0.42f } },
                { "voice-filter", { 0.74f, 0.42f }, filtered(20'000.0f) },
                { "master", { 0.94f, 0.42f } } },
              { { 0, 1 }, { 1, 2, 2, 2 }, { 1, 3, 3, 1 }, { 2, 4, 1, 1 },
                { 3, 4, 2, 2 }, { 4, 5, 3, 1 }, { 5, 6 } } });
    });
    buildApart([&] {
        // A rack of pads: the notes go to its pads, each an instrument of its
        // own (the voices' cards are its frame, silent).
        Parameters racked;
        racked.oscillatorFrequencyHz = 220.0f;
        racked.oscillatorLevel = 1.0f;
        factoryPresets_.push_back({ "rack-instrument", "nodes.preset.rackInstrument",
              { { "midi-input", { 0.03f, 0.42f } },
                { "voice-allocator", { 0.18f, 0.42f } },
                { "drum-rack", { 0.36f, 0.20f }, racked },
                { "envelope", { 0.36f, 0.72f }, shaped(0.1f, 1.0f, 1.0f, 50.0f) },
                { "gain", { 0.56f, 0.42f } },
                { "voice-filter", { 0.74f, 0.42f }, filtered(20'000.0f) },
                { "master", { 0.94f, 0.42f } } },
              { { 0, 1 }, { 1, 2, 2, 2 }, { 1, 3, 3, 1 }, { 2, 4, 1, 1 },
                { 3, 4, 2, 2 }, { 4, 5, 3, 1 }, { 5, 6 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "jade-instrument", "nodes.preset.jadeInstrument",
              { { "midi-input", { 0.03f, 0.42f } },
                { "voice-allocator", { 0.18f, 0.42f } },
                { "jade-synth", { 0.36f, 0.20f }, jaded() },
                { "envelope", { 0.36f, 0.72f }, shaped(0.1f, 1.0f, 1.0f, 8'000.0f) },
                { "gain", { 0.56f, 0.42f } },
                { "voice-filter", { 0.74f, 0.42f }, filtered(20'000.0f) },
                { "master", { 0.94f, 0.42f } } },
              { { 0, 1 }, { 1, 2, 2, 2 }, { 1, 3, 3, 1 }, { 2, 4, 1, 1 },
                { 3, 4, 2, 2 }, { 4, 5, 3, 1 }, { 5, 6 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "kick-instrument", "nodes.preset.kickInstrument",
              { { "midi-input", { 0.03f, 0.42f } },
                { "voice-allocator", { 0.18f, 0.42f }, monophonic() },
                { "kick-synth", { 0.36f, 0.20f }, kicked() },
                { "envelope", { 0.36f, 0.72f }, shaped(0.1f, 1.0f, 1.0f, 8'000.0f) },
                { "gain", { 0.56f, 0.42f } },
                { "voice-filter", { 0.74f, 0.42f }, filtered(20'000.0f) },
                { "master", { 0.94f, 0.42f } } },
              { { 0, 1 }, { 1, 2, 2, 2 }, { 1, 3, 3, 1 }, { 2, 4, 1, 1 },
                { 3, 4, 2, 2 }, { 4, 5, 3, 1 }, { 5, 6 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "table-drone", "nodes.preset.tableDrone",
              { { "wavetable", { 0.06f, 0.42f }, tabled(110.0f, 0.55f, 0.22f, 14.0f) },
                { "lowpass", { 0.30f, 0.42f }, filtered(4'200.0f) },
                { "eq-band", { 0.52f, 0.42f }, band(240.0f, 2.5f, 0.8f) },
                { "reverb", { 0.74f, 0.42f }, reverberated(0.80f, 0.28f) },
                { "master", { 0.96f, 0.42f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "bowed-string", "nodes.preset.bowedString",
              { { "noise", { 0.05f, 0.42f }, noise(0.05f) },
                { "string", { 0.28f, 0.42f }, strung(196.0f, 0.42f, 6.0f) },
                { "eq-band", { 0.52f, 0.42f }, band(2'400.0f, 2.0f, 1.0f) },
                { "reverb", { 0.74f, 0.42f }, reverberated(0.72f, 0.22f) },
                { "master", { 0.96f, 0.42f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "studio-compressor", "nodes.preset.studioCompressor",
              { { "graph-audio-input-2", { 0.03f, 0.42f } },
                { "compressor", { 0.30f, 0.42f }, compressed(-20.0f, 3.5f) },
                { "value-display", { 0.30f, 0.78f } },
                { "eq-band", { 0.56f, 0.42f }, band(160.0f, 1.5f, 0.8f) },
                { "gain", { 0.78f, 0.42f }, makeup(2.0f) },
                { "graph-audio-output-2", { 0.96f, 0.42f } } },
              { { 0, 1 }, { 1, 3 }, { 3, 4 }, { 4, 5 }, { 1, 2, 6, 1 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "ceiling-limiter", "nodes.preset.ceilingLimiter",
              { { "graph-audio-input-2", { 0.03f, 0.42f } },
                { "compressor", { 0.26f, 0.42f }, compressed(-6.0f, 20.0f) },
                { "value-display", { 0.26f, 0.78f } },
                { "hard-clip", { 0.52f, 0.42f }, clipped(0.98f) },
                { "level-meter", { 0.74f, 0.42f } },
                { "graph-audio-output-2", { 0.96f, 0.42f } } },
              { { 0, 1 }, { 1, 3 }, { 3, 4 }, { 4, 5 }, { 1, 2, 6, 1 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "band-saturator", "nodes.preset.bandSaturator",
              { { "graph-audio-input-2", { 0.02f, 0.44f } },
                { "lowpass", { 0.20f, 0.14f }, filtered(220.0f) },
                { "drive", { 0.44f, 0.14f }, driven(7.0f) },
                { "highpass", { 0.20f, 0.44f }, highPassed(220.0f) },
                { "lowpass", { 0.38f, 0.44f }, filtered(3'800.0f) },
                { "drive", { 0.56f, 0.44f }, driven(11.0f) },
                { "highpass", { 0.20f, 0.76f }, highPassed(3'800.0f) },
                { "drive", { 0.44f, 0.76f }, driven(4.0f) },
                { "mixer-4", { 0.76f, 0.44f } },
                { "graph-audio-output-2", { 0.96f, 0.44f } } },
              { { 0, 1 }, { 1, 2 }, { 0, 3 }, { 3, 4 }, { 4, 5 }, { 0, 6 },
                { 6, 7 }, { 2, 8, 2, 1 }, { 5, 8, 2, 2 }, { 7, 8, 2, 3 },
                { 8, 9 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "tape-echo", "nodes.preset.tapeEcho",
              { { "graph-audio-input-2", { 0.03f, 0.42f } },
                { "drive", { 0.22f, 0.42f }, driven(5.0f) },
                { "delay", { 0.42f, 0.42f }, delayed(340.0f, 0.42f, 0.32f) },
                { "eq-band", { 0.62f, 0.42f }, band(3'200.0f, -4.0f, 0.7f) },
                { "stereo-width", { 0.80f, 0.42f }, widened(1.16f) },
                { "graph-audio-output-2", { 0.96f, 0.42f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 }, { 4, 5 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "chamber-reverb", "nodes.preset.chamberReverb",
              { { "graph-audio-input-2", { 0.03f, 0.42f } },
                { "eq-band", { 0.22f, 0.42f }, band(280.0f, -3.0f, 0.6f) },
                { "reverb", { 0.44f, 0.42f }, reverberated(0.82f, 0.38f) },
                { "eq-band", { 0.66f, 0.42f }, band(6'400.0f, -2.5f, 0.8f) },
                { "stereo-width", { 0.84f, 0.42f }, widened(1.24f) },
                { "graph-audio-output-2", { 0.96f, 0.42f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 }, { 4, 5 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "spectral-lead", "nodes.preset.spectralLead",
              { { "midi-input", { 0.03f, 0.42f } },
                { "poly-lead", { 0.26f, 0.42f } },
                { "eq-band", { 0.52f, 0.42f }, band(220.0f, -2.5f, 0.8f) },
                { "eq-band", { 0.70f, 0.42f }, band(4'200.0f, 3.5f, 1.1f) },
                { "drive", { 0.85f, 0.42f }, driven(5.0f) },
                { "master", { 0.97f, 0.42f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 }, { 4, 5 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "felt-grand", "nodes.preset.feltGrand",
              { { "midi-input", { 0.03f, 0.42f } },
                { "poly-keys", { 0.24f, 0.42f } },
                { "eq-band", { 0.46f, 0.42f }, band(180.0f, 2.0f, 0.7f) },
                { "eq-band", { 0.62f, 0.42f }, band(5'200.0f, -3.5f, 0.9f) },
                { "delay", { 0.78f, 0.42f }, delayed(38.0f, 0.24f, 0.16f) },
                { "reverb", { 0.90f, 0.42f }, reverberated(0.74f, 0.24f) },
                { "master", { 0.98f, 0.42f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 }, { 4, 5 }, { 5, 6 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "precision-eq", "nodes.preset.precisionEq",
              { { "graph-audio-input-2", { 0.03f, 0.42f } },
                { "eq-band", { 0.22f, 0.42f }, band(110.0f, -3.5f, 0.9f) },
                { "eq-band", { 0.40f, 0.42f }, band(420.0f, -2.5f, 1.6f) },
                { "eq-band", { 0.58f, 0.42f }, band(2'800.0f, 3.0f, 1.1f) },
                { "eq-band", { 0.76f, 0.42f }, band(9'500.0f, 2.5f, 0.8f) },
                { "graph-audio-output-2", { 0.96f, 0.42f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 }, { 4, 5 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "slapback-delay", "nodes.preset.slapbackDelay",
              { { "graph-audio-input-2", { 0.03f, 0.42f } },
                { "delay", { 0.36f, 0.42f }, delayed(110.0f, 0.12f, 0.26f) },
                { "level-meter", { 0.70f, 0.42f } },
                { "graph-audio-output-2", { 0.96f, 0.42f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "dub-echo", "nodes.preset.dubEcho",
              { { "graph-audio-input-2", { 0.03f, 0.42f } },
                { "delay", { 0.28f, 0.42f }, delayed(400.0f, 0.55f, 0.34f) },
                { "lowpass", { 0.54f, 0.42f }, filtered(2'600.0f) },
                { "stereo-width", { 0.76f, 0.42f }, widened(1.20f) },
                { "graph-audio-output-2", { 0.96f, 0.42f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "plate-reverb", "nodes.preset.plateReverb",
              { { "graph-audio-input-2", { 0.03f, 0.42f } },
                { "eq", { 0.28f, 0.42f }, toned(-0.05f) },
                { "reverb", { 0.54f, 0.42f }, reverberated(0.86f, 0.44f) },
                { "graph-audio-output-2", { 0.96f, 0.42f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "room-ambience", "nodes.preset.roomAmbience",
              { { "graph-audio-input-2", { 0.03f, 0.42f } },
                { "reverb", { 0.32f, 0.42f }, reverberated(0.42f, 0.22f) },
                { "stereo-width", { 0.62f, 0.42f }, widened(1.15f) },
                { "graph-audio-output-2", { 0.96f, 0.42f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "chorus-sweep", "nodes.preset.chorusSweep",
              { { "graph-audio-input-2", { 0.03f, 0.34f } },
                { "lfo-core", { 0.20f, 0.74f }, modulator(0.6f, 1.0f, 0.0f) },
                { "map-range", { 0.44f, 0.74f },
                  mapped(-1.0f, 1.0f, 14.0f, 26.0f) },
                { "delay", { 0.52f, 0.34f }, delayed(20.0f, 0.10f, 0.42f) },
                { "stereo-width", { 0.78f, 0.34f }, widened(1.25f) },
                { "graph-audio-output-2", { 0.96f, 0.34f } } },
              // The delay's time is port 3: port 2 is its audio output, so wiring
              // the modulation there asked to feed a value into an audio socket.
              { { 0, 3 }, { 1, 2 }, { 2, 3, 2, 3 }, { 3, 4 }, { 4, 5 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "auto-filter-sweep", "nodes.preset.autoFilterSweep",
              { { "graph-audio-input-2", { 0.03f, 0.34f } },
                { "lfo-core", { 0.20f, 0.74f }, modulator(0.35f, 1.0f, 0.0f) },
                { "map-range", { 0.44f, 0.74f },
                  mapped(-1.0f, 1.0f, 240.0f, 5'200.0f) },
                { "lowpass", { 0.52f, 0.34f }, filtered(1'400.0f) },
                { "level-meter", { 0.78f, 0.34f } },
                { "graph-audio-output-2", { 0.96f, 0.34f } } },
              { { 0, 3 }, { 1, 2 }, { 2, 3, 2, 2 }, { 3, 4 }, { 4, 5 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "drum-bus-glue", "nodes.preset.drumBusGlue",
              { { "graph-audio-input-2", { 0.03f, 0.42f } },
                { "compressor", { 0.26f, 0.42f }, compressed(-12.0f, 3.0f) },
                { "drive", { 0.50f, 0.42f }, driven(4.0f) },
                { "eq", { 0.72f, 0.42f }, toned(0.08f) },
                { "graph-audio-output-2", { 0.96f, 0.42f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "telephone", "nodes.preset.telephone",
              { { "graph-audio-input-2", { 0.03f, 0.42f } },
                { "highpass", { 0.26f, 0.42f }, highPassed(700.0f) },
                { "lowpass", { 0.50f, 0.42f }, filtered(2'800.0f) },
                { "drive", { 0.74f, 0.42f }, driven(8.0f) },
                { "graph-audio-output-2", { 0.96f, 0.42f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "gated-punch", "nodes.preset.gatedPunch",
              { { "graph-audio-input-2", { 0.03f, 0.42f } },
                { "compressor", { 0.30f, 0.42f }, compressed(-8.0f, 8.0f) },
                { "hard-clip", { 0.60f, 0.42f }, clipped(0.70f) },
                { "graph-audio-output-2", { 0.96f, 0.42f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 } } });
    });
    buildApart([&] {
        factoryPresets_.push_back({ "mastering-chain", "nodes.preset.masteringChain",
              { { "graph-audio-input-2", { 0.03f, 0.42f } },
                { "master-guard", { 0.30f, 0.42f } },
                { "stereo-finisher", { 0.58f, 0.42f } },
                { "level-meter", { 0.80f, 0.42f } },
                { "graph-audio-output-2", { 0.96f, 0.42f } } },
              { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 } } });
    });
}

void NodeGraphComponent::initialiseCatalog()
{
    catalog_.clear();
    const auto audio = PortKind::audio;
    const auto midi = PortKind::midi;
    const auto control = PortKind::control;
    const auto transport = PortKind::transport;
    catalog_ = {
        { "graph-midi-input", "node.graphMidiInput", {}, "nodes.category.graphPorts", juce::Colour(0xffd47be8), false, midi, true, midi, false, false },
        { "graph-midi-output", "node.graphMidiOutput", {}, "nodes.category.graphPorts", juce::Colour(0xffd47be8), true, midi, false, midi, false, false },
        { "graph-value-input", "node.graphValueInput", {}, "nodes.category.graphPorts", juce::Colour(0xff75d6ac), false, control, true, control, false, false },
        { "graph-value-output", "node.graphValueOutput", {}, "nodes.category.graphPorts", juce::Colour(0xff75d6ac), true, control, false, control, false, false },
        { "graph-transport-input", "node.graphTransportInput", {}, "nodes.category.graphPorts", juce::Colour(0xffffc65c), false, transport, true, transport, false, false },
        { "graph-transport-output", "node.graphTransportOutput", {}, "nodes.category.graphPorts", juce::Colour(0xffffc65c), true, transport, false, transport, false, false },
        { "midi-input", "node.midiInput", {}, "nodes.category.io", juce::Colour(0xffd47be8), false, midi, true, midi, true, false },
        { "voice-allocator", "node.voiceAllocator", {}, "nodes.category.instruments", juce::Colour(0xffbd78e8), true, midi, true, midi, false, false },
        { "poly-oscillator", "node.polyOscillator", {}, "nodes.category.instruments", juce::Colour(0xff8e9dff), true, midi, true, audio, false, false },
        { "envelope", "node.envelope", {}, "nodes.category.instruments", juce::Colour(0xff7eafff), true, audio, true, audio, false, false },
        { "voice-filter", "node.voiceFilter", {}, "nodes.category.instruments", juce::Colour(0xff70b7ff), true, audio, true, audio, false, false },
        { "poly-synth", "node.polySynth", {}, "nodes.category.instruments", juce::Colour(0xff8e9dff), true, midi, true, audio, true, false },
        { "audio-clips", "node.audioClips", {}, "nodes.category.io", juce::Colour(0xff4fd3ae), false, audio, true, audio, true, false },
        { "drive", "node.drive", {}, "nodes.category.effects", juce::Colour(0xffff8f70), true, audio, true, audio, true, false },
        { "gain", "node.gain", {}, "nodes.category.utilities", juce::Colour(0xff65d6b1), true, audio, true, audio, false, false },
        { "lowpass", "node.lowpass", {}, "nodes.category.filters", juce::Colour(0xff70b7ff), true, audio, true, audio, true, false },
        { "highpass", "node.highpass", {}, "nodes.category.filters", juce::Colour(0xff68a8f2), true, audio, true, audio, false, false },
        { "hard-clip", "node.hardClip", {}, "nodes.category.effects", juce::Colour(0xffff786d), true, audio, true, audio, false, false },
        { "stereo-width", "node.stereoWidth", {}, "nodes.category.utilities", juce::Colour(0xff6bd4d8), true, audio, true, audio, false, false },
        { "dc-blocker", "node.dcBlocker", {}, "nodes.category.filters", juce::Colour(0xff68a8f2), true, audio, true, audio, false, false },
        { "audio-sum", "node.audioSum", {}, "nodes.category.utilities", juce::Colour(0xff65d6b1), true, audio, true, audio, false, false },
        { "mixer-4", "node.mixer4", {}, "nodes.category.utilities", juce::Colour(0xff65d6b1), true, audio, true, audio, false, false },
        { "gain-pan", "node.gainPan", {}, "nodes.category.utilities", juce::Colour(0xff4fd3ae), true, audio, true, audio, true, false },
        { "master", "node.master", {}, "nodes.category.io", juce::Colour(0xffefb45b), true, audio, false, audio, true, false },
        { "oscillator", "node.oscillator", {}, "nodes.category.generators", juce::Colour(0xff9d91ff), false, audio, true, audio, false, false },
        { "noise", "node.noise", {}, "nodes.category.generators", juce::Colour(0xffad9cea), false, audio, true, audio, false, false },
        { "delay", "node.delay", {}, "nodes.category.effects", juce::Colour(0xffffa547), true, audio, true, audio, false, false },
        { "reverb", "node.reverb", {}, "nodes.category.effects", juce::Colour(0xffe983c8), true, audio, true, audio, false, false },
        { "compressor", "node.compressor", {}, "nodes.category.dynamics", juce::Colour(0xffffcf5c), true, audio, true, audio, false, false },
        { "eq", "node.equalizer", {}, "nodes.category.filters", juce::Colour(0xff5fc9d8), true, audio, true, audio, false, false },
        { "eq-band", "node.eqBand", {}, "nodes.category.filters", juce::Colour(0xff5fc9d8), true, audio, true, audio, false, false },
        { "wavetable", "node.wavetable", {}, "nodes.category.generators", juce::Colour(0xff9d91ff), false, audio, true, audio, false, false },
        { "string", "node.string", {}, "nodes.category.generators", juce::Colour(0xffb08cff), true, audio, true, audio, false, false },
        { "sound-model", "node.soundModel", {}, "nodes.category.generators", juce::Colour(0xffc49cff), false, audio, true, audio, false, false },
        { "sampler", "node.sampler", {}, "nodes.category.generators", juce::Colour(0xff8fc8ff), false, audio, true, audio, false, false },
        { "batterie", "node.batterie", {}, "nodes.category.generators", juce::Colour(0xffe8c06a), false, audio, true, audio, false, false },
        { "drum-rack", "node.drumRack", {}, "nodes.category.generators", juce::Colour(0xffe86a8c), false, audio, true, audio, false, false },
        { "prism-synth", "node.prismSynth", {}, "nodes.category.generators", juce::Colour(0xffff8a3d), false, audio, true, audio, false, false },
        { "kick-synth", "node.kickSynth", {}, "nodes.category.generators", juce::Colour(0xffff5a5a), false, audio, true, audio, false, false },
        { "ambre-synth", "node.ambreSynth", {}, "nodes.category.generators", juce::Colour(0xffffb04a), false, audio, true, audio, false, false },
        { "colosse-synth", "node.colosseSynth", {}, "nodes.category.generators", juce::Colour(0xff4fd1ff), false, audio, true, audio, false, false },
        { "mosaique-synth", "node.mosaiqueSynth", {}, "nodes.category.generators", juce::Colour(0xff6ee7c7), false, audio, true, audio, false, false },
        { "nebuleuse-synth", "node.nebuleuseSynth", {}, "nodes.category.generators", juce::Colour(0xff9ad5ff), false, audio, true, audio, false, false },
        { "bronze-synth", "node.bronzeSynth", {}, "nodes.category.generators", juce::Colour(0xffd08c4c), false, audio, true, audio, false, false },
        { "jade-synth", "node.jadeSynth", {}, "nodes.category.generators", juce::Colour(0xff48b38c), false, audio, true, audio, false, false },
        { "silex-synth", "node.silexSynth", {}, "nodes.category.generators", juce::Colour(0xffe0705a), false, audio, true, audio, false, false },
        { "derive-synth", "node.deriveSynth", {}, "nodes.category.generators", juce::Colour(0xffff8f6b), false, audio, true, audio, false, false },
        { "saphir-synth", "node.saphirSynth", {}, "nodes.category.generators", juce::Colour(0xff5b8cff), false, audio, true, audio, false, false },
        { "argile-synth", "node.argileSynth", {}, "nodes.category.generators", juce::Colour(0xffe08a5c), false, audio, true, audio, false, false },
        { "quartz-synth", "node.quartzSynth", {}, "nodes.category.generators", juce::Colour(0xfff59ed0), false, audio, true, audio, false, false },
        { "alliage-synth", "node.alliageSynth", {}, "nodes.category.generators", juce::Colour(0xffe8c25a), false, audio, true, audio, false, false },
        { "tirette-synth", "node.tiretteSynth", {}, "nodes.category.generators", juce::Colour(0xffe8c08a), false, audio, true, audio, false, false },
        { "onyx-synth", "node.onyxSynth", {}, "nodes.category.generators", juce::Colour(0xffd68cff), false, audio, true, audio, false, false },
        { "opale-synth", "node.opaleSynth", {}, "nodes.category.generators", juce::Colour(0xfff2a6e8), false, audio, true, audio, false, false },
        { "spectrum", "node.spectrum", {}, "nodes.category.analyzers", juce::Colour(0xff6cb8ff), true, audio, true, audio, false, false },
        { "oscilloscope", "node.oscilloscope", {}, "nodes.category.analyzers", juce::Colour(0xff69c7ff), true, audio, true, audio, false, false },
        { "level-meter", "node.levelMeter", {}, "nodes.category.analyzers", juce::Colour(0xff7ed878), true, audio, true, audio, false, false },
        { "value-display", "node.valueDisplay", {}, "nodes.category.analyzers", juce::Colour(0xff75d6ac), true, control, true, control, false, false },
        { "midi-monitor", "node.midiMonitor", {}, "nodes.category.analyzers", juce::Colour(0xffd47be8), true, midi, true, midi, false, false },
        // A plug-in an imported project used that is not here: its place
        // kept, its name shown (the sound passes an effect's; an
        // instrument's is silent).
        { "missing-plugin", "node.missingPlugin", {}, "nodes.category.utilities", juce::Colour(0xff8b909a), true, audio, true, audio, false, false },
        { "missing-instrument", "node.missingInstrument", {}, "nodes.category.generators", juce::Colour(0xff8b909a), true, midi, true, audio, false, false },
        { "midi-arp", "node.arpeggiator", {}, "nodes.category.midi", juce::Colour(0xffd47be8), true, midi, true, midi, false, false },
        { "note-filter", "node.noteFilter", {}, "nodes.category.midi", juce::Colour(0xffcf78dd), true, midi, true, midi, false, false },
        { "midi-transpose", "node.midiTranspose", {}, "nodes.category.midi", juce::Colour(0xffcf78dd), true, midi, true, midi, false, false },
        { "velocity-map", "node.velocityMap", {}, "nodes.category.midi", juce::Colour(0xffcf78dd), true, midi, true, midi, false, false },
        { "lfo", "node.lfo", {}, "nodes.category.modules", juce::Colour(0xff75d6ac), false, control, true, control, false, true },
        { "lfo-core", "node.lfoCore", {}, "nodes.category.modulators", juce::Colour(0xff75d6ac), false, control, true, control, false, false },
        { "macro", "node.macro", {}, "nodes.category.modulators", juce::Colour(0xff69cca1), false, control, true, control, false, false },
        { "control-math", "node.controlMath", {}, "nodes.category.modulators", juce::Colour(0xff82c7a7), true, control, true, control, false, false },
        { "value-constant", "node.valueConstant", {}, "nodes.category.math", juce::Colour(0xff75d6ac), false, control, true, control, false, false },
        { "value-add", "node.valueAdd", {}, "nodes.category.math", juce::Colour(0xff75d6ac), true, control, true, control, false, false },
        { "value-subtract", "node.valueSubtract", {}, "nodes.category.math", juce::Colour(0xff75d6ac), true, control, true, control, false, false },
        { "value-multiply", "node.valueMultiply", {}, "nodes.category.math", juce::Colour(0xff75d6ac), true, control, true, control, false, false },
        { "value-divide", "node.valueDivide", {}, "nodes.category.math", juce::Colour(0xff75d6ac), true, control, true, control, false, false },
        { "value-modulo", "node.valueModulo", {}, "nodes.category.math", juce::Colour(0xff75d6ac), true, control, true, control, false, false },
        { "value-power", "node.valuePower", {}, "nodes.category.math", juce::Colour(0xff75d6ac), true, control, true, control, false, false },
        { "value-sine", "node.valueSine", {}, "nodes.category.math", juce::Colour(0xff75d6ac), true, control, true, control, false, false },
        { "value-cosine", "node.valueCosine", {}, "nodes.category.math", juce::Colour(0xff75d6ac), true, control, true, control, false, false },
        { "value-square-root", "node.valueSquareRoot", {}, "nodes.category.math", juce::Colour(0xff75d6ac), true, control, true, control, false, false },
        { "value-floor", "node.valueFloor", {}, "nodes.category.math", juce::Colour(0xff75d6ac), true, control, true, control, false, false },
        { "value-ceil", "node.valueCeil", {}, "nodes.category.math", juce::Colour(0xff75d6ac), true, control, true, control, false, false },
        { "value-round", "node.valueRound", {}, "nodes.category.math", juce::Colour(0xff75d6ac), true, control, true, control, false, false },
        { "value-min", "node.valueMin", {}, "nodes.category.math", juce::Colour(0xff75d6ac), true, control, true, control, false, false },
        { "value-max", "node.valueMax", {}, "nodes.category.math", juce::Colour(0xff75d6ac), true, control, true, control, false, false },
        { "value-clamp", "node.valueClamp", {}, "nodes.category.math", juce::Colour(0xff75d6ac), true, control, true, control, false, false },
        { "map-range", "node.mapRange", {}, "nodes.category.math", juce::Colour(0xff75d6ac), true, control, true, control, false, false },
        { "value-abs", "node.valueAbs", {}, "nodes.category.math", juce::Colour(0xff75d6ac), true, control, true, control, false, false },
        { "value-negate", "node.valueNegate", {}, "nodes.category.math", juce::Colour(0xff75d6ac), true, control, true, control, false, false },
        { "logic-greater", "node.logicGreater", {}, "nodes.category.logic", juce::Colour(0xffc7a6ff), true, control, true, control, false, false },
        { "logic-less", "node.logicLess", {}, "nodes.category.logic", juce::Colour(0xffc7a6ff), true, control, true, control, false, false },
        { "logic-equal", "node.logicEqual", {}, "nodes.category.logic", juce::Colour(0xffc7a6ff), true, control, true, control, false, false },
        { "logic-and", "node.logicAnd", {}, "nodes.category.logic", juce::Colour(0xffc7a6ff), true, control, true, control, false, false },
        { "logic-or", "node.logicOr", {}, "nodes.category.logic", juce::Colour(0xffc7a6ff), true, control, true, control, false, false },
        { "logic-xor", "node.logicXor", {}, "nodes.category.logic", juce::Colour(0xffc7a6ff), true, control, true, control, false, false },
        { "logic-not", "node.logicNot", {}, "nodes.category.logic", juce::Colour(0xffc7a6ff), true, control, true, control, false, false },
        { "transport-reader", "node.transportReader", {}, "nodes.category.transport", juce::Colour(0xffffc65c), true, transport, true, control, false, false },
        { "transport-tempo", "node.transportTempo", {}, "nodes.category.transport", juce::Colour(0xffffc65c), true, transport, true, control, false, false },
        { "transport-beat", "node.transportBeat", {}, "nodes.category.transport", juce::Colour(0xffffc65c), true, transport, true, control, false, false },
        { "transport-playing", "node.transportPlaying", {}, "nodes.category.transport", juce::Colour(0xffffc65c), true, transport, true, control, false, false },
        { "envelope-follower", "node.envelopeFollower", {}, "nodes.category.analyzers", juce::Colour(0xff7ed878), true, audio, true, control, false, false }
    };
    // The effect catalogue's cards; their sockets and schema come from the
    // primitive registry like everyone else's.
    for (const auto& effect : cydaw::engine::effectCatalogue) {
        // The MIDI effects carry notes; the modulators values.
        const auto carried = effect.midi ? midi : effect.control ? control : audio;
        NodeType type { juce::String(std::string(effect.typeKey)), juce::String(std::string(effect.titleKey)), {},
                        juce::String(std::string(effect.categoryKey)), juce::Colour(effect.colour),
                        true, carried, true, carried, false, false };
        type.descriptionKey = juce::String(std::string(effect.descriptionKey));
        if (effect.convertsChannels()) {
            type.inputChannels = effect.inputChannels;
            type.outputChannels = effect.outputChannels;
        }
        catalog_.push_back(std::move(type));
    }
    std::vector<NodeType> audioBoundaries;
    audioBoundaries.reserve(16);
    for (int channels = 1; channels <= 8; ++channels) {
        const auto suffix = juce::String(channels);
        audioBoundaries.push_back({
            graphAudioBoundaryKey(true, channels),
            "node.graphAudioInput." + suffix, {}, "nodes.category.graphPorts",
            juce::Colour(0xff67b9ff), false, audio, true, audio, false, false,
            0, static_cast<std::uint8_t>(channels)
        });
        audioBoundaries.push_back({
            graphAudioBoundaryKey(false, channels),
            "node.graphAudioOutput." + suffix, {}, "nodes.category.graphPorts",
            juce::Colour(0xff67b9ff), true, audio, false, audio, false, false,
            static_cast<std::uint8_t>(channels), 0
        });
    }
    catalog_.insert(catalog_.begin(), audioBoundaries.begin(),
                    audioBoundaries.end());
    for (auto& type : catalog_) {
        // Audio is never left as an untyped generic socket in the palette.
        // New processors are stereo by default and can be changed explicitly
        // to mono or 3..8 channels from the selection details panel.
        if (type.hasInput && type.inputKind == PortKind::audio
            && type.inputChannels == 0)
            type.inputChannels = 2;
        if (type.hasOutput && type.outputKind == PortKind::audio
            && type.outputChannels == 0)
            type.outputChannels = 2;

        // The engine registry is the authoritative schema for primitive
        // sockets. Keep the legacy single-input/single-output flags during
        // migration, while exposing every exact socket in the editor.
        const auto primitiveKey = primitiveProcessorKeyForType(type.key);
        if (primitiveKey.isEmpty())
            continue;
        const auto& primitives = cydaw::graph::factoryPrimitiveRegistry();
        const auto* descriptor = primitives.find(primitiveKey.toStdString());
        if (descriptor == nullptr)
            continue;
        type.ports.clear();
        type.stateful = descriptor->stateful;
        type.supportsPolyphony = descriptor->supportsPolyphony;
        type.breaksFeedbackCycle = descriptor->breaksFeedbackCycle;
        type.parameterSchema = descriptor->parameters;
        for (const auto& port : descriptor->ports) {
            auto channels = static_cast<std::uint8_t>(juce::jlimit(
                1, 8, static_cast<int>(port.channels)));
            if (port.type == cydaw::graph::PortType::audio) {
                const auto legacyChannels = port.direction
                            == cydaw::graph::PortDirection::input
                        ? type.inputChannels : type.outputChannels;
                if (legacyChannels != 0)
                    channels = legacyChannels;
            }
            type.ports.push_back({
                port.id, juce::String(port.key),
                juce::String(port.displayName.empty() ? port.key
                                                      : port.displayName),
                port.type,
                port.direction == cydaw::graph::PortDirection::input,
                channels,
                port.acceptsMultipleConnections,
                port.defaultValue
            });
        }
    }
    for (auto& type : catalog_) {
        if (!type.ports.empty())
            continue;
        if (type.hasInput)
            type.ports.push_back({
                1, "input", "Input",
                type.inputKind == PortKind::audio
                    ? cydaw::graph::PortType::audio
                : type.inputKind == PortKind::midi
                    ? cydaw::graph::PortType::midi
                : type.inputKind == PortKind::transport
                    ? cydaw::graph::PortType::transport
                    : cydaw::graph::PortType::value,
                true,
                type.inputKind == PortKind::audio
                    ? type.inputChannels : std::uint8_t { 1 }, false
            });
        if (type.hasOutput)
            type.ports.push_back({
                2, "output", "Output",
                type.outputKind == PortKind::audio
                    ? cydaw::graph::PortType::audio
                : type.outputKind == PortKind::midi
                    ? cydaw::graph::PortType::midi
                : type.outputKind == PortKind::transport
                    ? cydaw::graph::PortType::transport
                    : cydaw::graph::PortType::value,
                false,
                type.outputKind == PortKind::audio
                    ? type.outputChannels : std::uint8_t { 1 }, false
            });
    }
}

void NodeGraphComponent::ensureFactoryModules()
{
    // Every class below is constructed in full — nodes, connections, ports —
    // and then discarded if its key is already loaded. Once they have all been
    // installed and their metadata migrated, presence is the whole answer.
    // refreshTranslations() clears the flag, so a language change still
    // rebuilds them.
    if (factoryModulesInstalled_
        && std::all_of(factoryModuleKeys_.begin(), factoryModuleKeys_.end(),
                       [this](const juce::String& key) {
                           // The class has to be loaded and its type has to be
                           // in the catalogue: restore() rebuilds the catalogue
                           // from the primitives alone, and a class that is
                           // present but unregistered cannot be instantiated.
                           return findModule(key) != nullptr
                                  && findType(key) != nullptr;
                       }))
        return;
    using Parameters = cydaw::engine::NativeDspNodeParameters;
    using Role = cydaw::graph::GraphRole;
    const auto describeFactory = [](SavedModule& module, const Role role,
                                    const juce::StringRef categoryKey,
                                    const juce::StringRef descriptionKey,
                                    std::initializer_list<const char*> tags) {
        module.role = role;
        module.category = tr(categoryKey);
        module.author = "CyDAW Factory Library";
        module.description = tr(descriptionKey);
        module.tags.clear();
        for (const auto* tag : tags)
            module.tags.add(tag);
    };
    factoryModuleKeys_.clearQuick();
    const auto addFactory = [this](SavedModule module,
                                   const juce::Colour colour) {
        const auto key = module.key;
        factoryModuleKeys_.add(key);
        if (findModule(key) == nullptr) {
            module.factoryModule = true;
            inferModulePorts(module);
            module.interfaceState = createDefaultModuleInterface(module);
            savedModules_.push_back(std::move(module));
        } else if (auto* stored = findModule(key); stored != nullptr
                   && stored->factoryModule) {
            // The metadata below is rewritten, so the description this class
            // was read from stops matching it.
            stored->source = juce::var();
            stored->definitionCache.reset();
            // Migrate the library metadata without replacing a class body
            // that may already be referenced by the current project.
            stored->name = module.name;
            stored->description = module.description;
            stored->category = module.category;
            stored->author = module.author;
            stored->tags = module.tags;
            stored->role = module.role;
            // Factory positions are editor-only metadata. Refresh them by
            // stable node identity so UI improvements reach existing projects
            // without replacing their DSP topology, parameters or instances.
            for (const auto& authoredNode : module.nodes) {
                const auto matchingNode = std::find_if(
                    stored->nodes.begin(), stored->nodes.end(),
                    [&authoredNode](const auto& candidate) {
                        return candidate.id == authoredNode.id
                            && candidate.typeKey == authoredNode.typeKey;
                    });
                if (matchingNode != stored->nodes.end())
                {
                    matchingNode->position = authoredNode.position;
                    matchingNode->collapsed = authoredNode.collapsed;
                }
            }
        }
        if (const auto* stored = findModule(key); stored != nullptr)
            registerModuleType(*stored);
        if (auto found = std::find_if(catalog_.begin(), catalog_.end(),
                                     [&key](const auto& type) {
                                         return type.key == key;
                                     }); found != catalog_.end())
            found->colour = colour;
    };

    buildApart([&] {
        SavedModule lfo;
        lfo.key = "lfo";
        lfo.name = tr("node.lfo");
        lfo.hasInput = false;
        lfo.hasOutput = true;
        lfo.outputKind = PortKind::control;
        describeFactory(lfo, Role::modulator,
                        "graph.library.category.modulators",
                        "graph.library.lfo.description",
                        { "modulation", "lfo", "value" });
        Parameters lfoParameters;
        lfoParameters.modulationRateHz = 1.0f;
        lfoParameters.modulationDepth = 1.0f;
        lfo.nodes = {
            { 9'001, "lfo-core", tr("node.lfoCore"), { 0.24f, 0.42f }, lfoParameters },
            { 9'002, "graph-value-output", tr("node.graphValueOutput"), { 0.72f, 0.42f }, {} }
        };
        lfo.connections = { { 9'001, 9'002, PortKind::control } };
        addFactory(std::move(lfo), juce::Colour(0xff75d6ac));
    });

    buildApart([&] {
        SavedModule poly;
        poly.key = "poly-sine";
        poly.name = tr("node.polySine");
        poly.hasInput = true;
        poly.inputKind = PortKind::midi;
        poly.hasOutput = true;
        poly.outputKind = PortKind::audio;
        describeFactory(poly, Role::instrument,
                        "graph.library.category.instruments",
                        "graph.library.polySine.description",
                        { "instrument", "polyphonic", "synth", "sine" });
        Parameters oscillator;
        oscillator.oscillatorWaveform = 0.0f;
        oscillator.oscillatorLevel = 0.14f;
        Parameters envelope;
        envelope.attackMilliseconds = 8.0f;
        envelope.decayMilliseconds = 120.0f;
        envelope.sustainLevel = 0.72f;
        envelope.releaseMilliseconds = 280.0f;
        Parameters filter;
        filter.cutoffHz = 12'000.0f;
        poly.nodes = {
            { 10'001, "graph-midi-input", tr("node.graphMidiInput"), { 0.03f, 0.08f }, {} },
            { 10'002, "voice-allocator", tr("node.voiceAllocator"), { 0.15f, 0.38f }, {} },
            { 10'003, "poly-oscillator", tr("node.polyOscillator"), { 0.35f, 0.08f }, oscillator },
            { 10'004, "envelope", tr("node.envelope"), { 0.35f, 0.76f }, envelope },
            { 10'007, "gain", tr("node.gain"), { 0.55f, 0.36f }, {} },
            { 10'005, "voice-filter", tr("node.voiceFilter"), { 0.73f, 0.72f }, filter },
            { 10'006, "graph-audio-output-2", tr("node.graphAudioOutput.2"), { 0.91f, 0.38f }, {} }
        };
        poly.connections = {
            { 10'001, 10'002, PortKind::midi, 0, 2, 1 },
            { 10'002, 10'003, PortKind::control, 0, 2, 1 },
            { 10'002, 10'004, PortKind::control, 0, 3, 1 },
            { 10'003, 10'007, PortKind::audio, 2, 3, 1 },
            { 10'004, 10'007, PortKind::control, 0, 2, 2 },
            { 10'007, 10'005, PortKind::audio, 2, 3, 1 },
            { 10'005, 10'006, PortKind::audio, 2, 3, 1 }
        };
        addFactory(std::move(poly), juce::Colour(0xff8e9dff));
    });

    buildApart([&] {
        SavedModule reverb;
        reverb.key = "reverb-rack";
        reverb.name = tr("node.reverbRack");
        describeFactory(reverb, Role::audioEffect,
                        "graph.library.category.space",
                        "graph.library.reverbRack.description",
                        { "effect", "reverb", "space", "stereo" });
        Parameters reverbParameters;
        reverbParameters.roomSize = 0.78f;
        reverbParameters.mix = 0.34f;
        reverb.nodes = {
            { 11'001, "graph-audio-input-2", tr("node.graphAudioInput.2"), { 0.08f, 0.42f }, {} },
            { 11'002, "reverb", tr("node.reverb"), { 0.40f, 0.42f }, reverbParameters },
            { 11'003, "graph-audio-output-2", tr("node.graphAudioOutput.2"), { 0.76f, 0.42f }, {} }
        };
        reverb.connections = {
            { 11'001, 11'002, PortKind::audio },
            { 11'002, 11'003, PortKind::audio }
        };
        addFactory(std::move(reverb), juce::Colour(0xfff08ccf));
    });

    buildApart([&] {
        SavedModule vocal;
        vocal.key = "vocal-strip";
        vocal.name = tr("node.vocalStrip");
        describeFactory(vocal, Role::audioEffect,
                        "graph.library.category.dynamics",
                        "graph.library.vocalStrip.description",
                        { "effect", "voice", "compressor", "eq" });
        Parameters compressor;
        compressor.thresholdDb = -18.0f;
        compressor.ratio = 3.0f;
        Parameters tone;
        tone.tone = 0.12f;
        vocal.nodes = {
            { 12'001, "graph-audio-input-2", tr("node.graphAudioInput.2"), { 0.05f, 0.42f }, {} },
            { 12'002, "compressor", tr("node.compressor"), { 0.31f, 0.42f }, compressor },
            { 12'003, "eq", tr("node.equalizer"), { 0.55f, 0.42f }, tone },
            { 12'004, "graph-audio-output-2", tr("node.graphAudioOutput.2"), { 0.80f, 0.42f }, {} }
        };
        vocal.connections = {
            { 12'001, 12'002, PortKind::audio },
            { 12'002, 12'003, PortKind::audio },
            { 12'003, 12'004, PortKind::audio }
        };
        addFactory(std::move(vocal), juce::Colour(0xff5fc9d8));
    });

    buildApart([&] {
        SavedModule cleanChannel;
        cleanChannel.key = "clean-channel";
        cleanChannel.name = tr("node.cleanChannel");
        describeFactory(cleanChannel, Role::audioEffect,
                        "graph.library.category.dynamics",
                        "graph.library.cleanChannel.description",
                        { "effect", "channel", "clean", "compressor", "eq" });
        Parameters cleanHighpass;
        cleanHighpass.highpassCutoffHz = 68.0f;
        Parameters cleanCompressor;
        cleanCompressor.thresholdDb = -16.0f;
        cleanCompressor.ratio = 2.5f;
        Parameters cleanTone;
        cleanTone.tone = 0.08f;
        Parameters cleanGain;
        cleanGain.gainDb = -0.5f;
        cleanChannel.nodes = {
            { 13'001, "graph-audio-input-2", tr("node.graphAudioInput.2"), { 0.03f, 0.42f }, {} },
            { 13'002, "dc-blocker", tr("node.dcBlocker"), { 0.19f, 0.42f }, {} },
            { 13'003, "highpass", tr("node.highpass"), { 0.34f, 0.42f }, cleanHighpass },
            { 13'004, "compressor", tr("node.compressor"), { 0.50f, 0.42f }, cleanCompressor },
            { 13'005, "eq", tr("node.equalizer"), { 0.66f, 0.42f }, cleanTone },
            { 13'006, "gain", tr("node.gain"), { 0.80f, 0.42f }, cleanGain },
            { 13'007, "graph-audio-output-2", tr("node.graphAudioOutput.2"), { 0.94f, 0.42f }, {} }
        };
        cleanChannel.connections = {
            { 13'001, 13'002, PortKind::audio },
            { 13'002, 13'003, PortKind::audio },
            { 13'003, 13'004, PortKind::audio },
            { 13'004, 13'005, PortKind::audio },
            { 13'005, 13'006, PortKind::audio },
            { 13'006, 13'007, PortKind::audio }
        };
        addFactory(std::move(cleanChannel), juce::Colour(0xff63c8d3));
    });

    buildApart([&] {
        SavedModule warmDrive;
        warmDrive.key = "warm-drive";
        warmDrive.name = tr("node.warmDrive");
        describeFactory(warmDrive, Role::audioEffect,
                        "graph.library.category.colour",
                        "graph.library.warmDrive.description",
                        { "effect", "drive", "colour", "saturation" });
        Parameters warmDriveAmount;
        warmDriveAmount.driveDb = 8.0f;
        Parameters warmFilter;
        warmFilter.cutoffHz = 14'500.0f;
        warmFilter.resonance = 0.62f;
        Parameters warmClip;
        warmClip.clipThreshold = 0.92f;
        Parameters warmOutput;
        warmOutput.gainDb = -2.0f;
        warmDrive.nodes = {
            { 14'001, "graph-audio-input-2", tr("node.graphAudioInput.2"), { 0.05f, 0.42f }, {} },
            { 14'002, "drive", tr("node.drive"), { 0.25f, 0.42f }, warmDriveAmount },
            { 14'003, "lowpass", tr("node.lowpass"), { 0.46f, 0.42f }, warmFilter },
            { 14'004, "hard-clip", tr("node.hardClip"), { 0.66f, 0.42f }, warmClip },
            { 14'005, "gain", tr("node.gain"), { 0.80f, 0.42f }, warmOutput },
            { 14'006, "graph-audio-output-2", tr("node.graphAudioOutput.2"), { 0.94f, 0.42f }, {} }
        };
        warmDrive.connections = {
            { 14'001, 14'002, PortKind::audio },
            { 14'002, 14'003, PortKind::audio },
            { 14'003, 14'004, PortKind::audio },
            { 14'004, 14'005, PortKind::audio },
            { 14'005, 14'006, PortKind::audio }
        };
        addFactory(std::move(warmDrive), juce::Colour(0xffff866a));
    });

    buildApart([&] {
        SavedModule echoSpace;
        echoSpace.key = "echo-space";
        echoSpace.name = tr("node.echoSpace");
        describeFactory(echoSpace, Role::audioEffect,
                        "graph.library.category.space",
                        "graph.library.echoSpace.description",
                        { "effect", "delay", "reverb", "space" });
        Parameters echoDelay;
        echoDelay.delayMilliseconds = 380.0f;
        echoDelay.feedback = 0.36f;
        echoDelay.mix = 0.30f;
        Parameters echoReverb;
        echoReverb.roomSize = 0.72f;
        echoReverb.mix = 0.22f;
        echoSpace.nodes = {
            { 15'001, "graph-audio-input-2", tr("node.graphAudioInput.2"), { 0.06f, 0.42f }, {} },
            { 15'002, "delay", tr("node.delay"), { 0.34f, 0.42f }, echoDelay },
            { 15'003, "reverb", tr("node.reverb"), { 0.61f, 0.42f }, echoReverb },
            { 15'004, "graph-audio-output-2", tr("node.graphAudioOutput.2"), { 0.88f, 0.42f }, {} }
        };
        echoSpace.connections = {
            { 15'001, 15'002, PortKind::audio },
            { 15'002, 15'003, PortKind::audio },
            { 15'003, 15'004, PortKind::audio }
        };
        addFactory(std::move(echoSpace), juce::Colour(0xffe48bc8));
    });

    buildApart([&] {
        SavedModule stereoFinisher;
        stereoFinisher.key = "stereo-finisher";
        stereoFinisher.name = tr("node.stereoFinisher");
        describeFactory(stereoFinisher, Role::audioEffect,
                        "graph.library.category.mastering",
                        "graph.library.stereoFinisher.description",
                        { "effect", "mastering", "stereo", "metering" });
        Parameters finishTone;
        finishTone.tone = 0.04f;
        Parameters finishWidth;
        finishWidth.stereoWidth = 1.15f;
        Parameters finishCompressor;
        finishCompressor.thresholdDb = -10.0f;
        finishCompressor.ratio = 1.8f;
        Parameters finishClip;
        finishClip.clipThreshold = 0.97f;
        stereoFinisher.nodes = {
            { 16'001, "graph-audio-input-2", tr("node.graphAudioInput.2"), { 0.03f, 0.42f }, {} },
            { 16'002, "eq", tr("node.equalizer"), { 0.19f, 0.42f }, finishTone },
            { 16'003, "stereo-width", tr("node.stereoWidth"), { 0.35f, 0.42f }, finishWidth },
            { 16'004, "compressor", tr("node.compressor"), { 0.51f, 0.42f }, finishCompressor },
            { 16'005, "hard-clip", tr("node.hardClip"), { 0.67f, 0.42f }, finishClip },
            { 16'006, "spectrum", tr("node.spectrum"), { 0.81f, 0.42f }, {} },
            { 16'007, "graph-audio-output-2", tr("node.graphAudioOutput.2"), { 0.95f, 0.42f }, {} }
        };
        stereoFinisher.connections = {
            { 16'001, 16'002, PortKind::audio },
            { 16'002, 16'003, PortKind::audio },
            { 16'003, 16'004, PortKind::audio },
            { 16'004, 16'005, PortKind::audio },
            { 16'005, 16'006, PortKind::audio },
            { 16'006, 16'007, PortKind::audio }
        };
        addFactory(std::move(stereoFinisher), juce::Colour(0xfff1c55c));
    });

    buildApart([&] {
        SavedModule parallelDrive;
        parallelDrive.key = "parallel-drive";
        parallelDrive.name = tr("node.parallelDrive");
        describeFactory(parallelDrive, Role::audioEffect,
                        "graph.library.category.colour",
                        "graph.library.parallelDrive.description",
                        { "effect", "parallel", "drive", "mix" });
        Parameters parallelDriveAmount;
        parallelDriveAmount.driveDb = 14.0f;
        Parameters parallelWet;
        parallelWet.gainDb = -9.0f;
        parallelDrive.nodes = {
            { 17'001, "graph-audio-input-2", tr("node.graphAudioInput.2"), { 0.05f, 0.42f }, {} },
            { 17'002, "drive", tr("node.drive"), { 0.32f, 0.68f }, parallelDriveAmount },
            { 17'003, "gain", tr("node.gain"), { 0.53f, 0.68f }, parallelWet },
            { 17'004, "audio-sum", tr("node.audioSum"), { 0.73f, 0.42f }, {} },
            { 17'005, "graph-audio-output-2", tr("node.graphAudioOutput.2"), { 0.92f, 0.42f }, {} }
        };
        parallelDrive.connections = {
            { 17'001, 17'004, PortKind::audio, 2, 2, 1 },
            { 17'001, 17'002, PortKind::audio, 2, 2, 1 },
            { 17'002, 17'003, PortKind::audio, 2, 2, 1 },
            { 17'003, 17'004, PortKind::audio, 2, 3, 1 },
            { 17'004, 17'005, PortKind::audio, 2, 2, 1 }
        };
        addFactory(std::move(parallelDrive), juce::Colour(0xffff946a));
    });

    buildApart([&] {
        SavedModule polyPad;
        polyPad.key = "poly-pad";
        polyPad.name = tr("node.polyPad");
        polyPad.hasInput = true;
        polyPad.inputKind = PortKind::midi;
        polyPad.hasOutput = true;
        polyPad.outputKind = PortKind::audio;
        describeFactory(polyPad, Role::instrument,
                        "graph.library.category.instruments",
                        "graph.library.polyPad.description",
                        { "instrument", "polyphonic", "pad", "synth" });
        Parameters padOscillator;
        padOscillator.oscillatorWaveform = 2.0f;
        padOscillator.oscillatorLevel = 0.12f;
        Parameters padEnvelope;
        padEnvelope.attackMilliseconds = 620.0f;
        padEnvelope.decayMilliseconds = 1'800.0f;
        padEnvelope.sustainLevel = 0.68f;
        padEnvelope.releaseMilliseconds = 4'200.0f;
        Parameters padFilter;
        padFilter.cutoffHz = 3'400.0f;
        padFilter.resonance = 0.85f;
        Parameters padWidth;
        padWidth.stereoWidth = 1.35f;
        Parameters padReverb;
        padReverb.roomSize = 0.82f;
        padReverb.mix = 0.32f;
        polyPad.nodes = {
            { 18'001, "graph-midi-input", tr("node.graphMidiInput"), { 0.02f, 0.10f }, {} },
            { 18'002, "voice-allocator", tr("node.voiceAllocator"), { 0.14f, 0.42f }, {} },
            { 18'003, "poly-oscillator", tr("node.polyOscillator"), { 0.30f, 0.12f }, padOscillator },
            { 18'004, "envelope", tr("node.envelope"), { 0.30f, 0.74f }, padEnvelope },
            { 18'005, "gain", tr("node.gain"), { 0.46f, 0.42f }, {} },
            { 18'006, "voice-filter", tr("node.voiceFilter"), { 0.60f, 0.42f }, padFilter },
            { 18'007, "stereo-width", tr("node.stereoWidth"), { 0.72f, 0.42f }, padWidth },
            { 18'008, "reverb", tr("node.reverb"), { 0.83f, 0.42f }, padReverb },
            { 18'009, "graph-audio-output-2", tr("node.graphAudioOutput.2"), { 0.96f, 0.42f }, {} }
        };
        polyPad.connections = {
            { 18'001, 18'002, PortKind::midi, 0, 2, 1 },
            { 18'002, 18'003, PortKind::control, 0, 2, 1 },
            { 18'002, 18'004, PortKind::control, 0, 3, 1 },
            { 18'003, 18'005, PortKind::audio, 2, 3, 1 },
            { 18'004, 18'005, PortKind::control, 0, 2, 2 },
            { 18'005, 18'006, PortKind::audio, 2, 3, 1 },
            { 18'006, 18'007, PortKind::audio },
            { 18'007, 18'008, PortKind::audio },
            { 18'008, 18'009, PortKind::audio }
        };
        addFactory(std::move(polyPad), juce::Colour(0xff9e86ff));
    });

    buildApart([&] {
        SavedModule polyBass;
        polyBass.key = "poly-bass";
        polyBass.name = tr("node.polyBass");
        polyBass.hasInput = true;
        polyBass.inputKind = PortKind::midi;
        polyBass.hasOutput = true;
        polyBass.outputKind = PortKind::audio;
        describeFactory(polyBass, Role::instrument,
                        "graph.library.category.instruments",
                        "graph.library.polyBass.description",
                        { "instrument", "polyphonic", "bass", "synth" });
        Parameters bassOscillator;
        bassOscillator.oscillatorWaveform = 1.0f;
        bassOscillator.oscillatorLevel = 0.18f;
        Parameters bassEnvelope;
        bassEnvelope.attackMilliseconds = 4.0f;
        bassEnvelope.decayMilliseconds = 220.0f;
        bassEnvelope.sustainLevel = 0.38f;
        bassEnvelope.releaseMilliseconds = 120.0f;
        Parameters bassFilter;
        bassFilter.cutoffHz = 650.0f;
        bassFilter.resonance = 1.2f;
        Parameters bassDrive;
        bassDrive.driveDb = 4.5f;
        Parameters bassCompressor;
        bassCompressor.thresholdDb = -12.0f;
        bassCompressor.ratio = 3.0f;
        polyBass.nodes = {
            { 19'001, "graph-midi-input", tr("node.graphMidiInput"), { 0.02f, 0.10f }, {} },
            { 19'002, "voice-allocator", tr("node.voiceAllocator"), { 0.14f, 0.42f }, {} },
            { 19'003, "poly-oscillator", tr("node.polyOscillator"), { 0.31f, 0.12f }, bassOscillator },
            { 19'004, "envelope", tr("node.envelope"), { 0.31f, 0.74f }, bassEnvelope },
            { 19'005, "gain", tr("node.gain"), { 0.48f, 0.42f }, {} },
            { 19'006, "voice-filter", tr("node.voiceFilter"), { 0.63f, 0.42f }, bassFilter },
            { 19'007, "drive", tr("node.drive"), { 0.76f, 0.42f }, bassDrive },
            { 19'008, "compressor", tr("node.compressor"), { 0.87f, 0.42f }, bassCompressor },
            { 19'009, "graph-audio-output-2", tr("node.graphAudioOutput.2"), { 0.97f, 0.42f }, {} }
        };
        polyBass.connections = {
            { 19'001, 19'002, PortKind::midi, 0, 2, 1 },
            { 19'002, 19'003, PortKind::control, 0, 2, 1 },
            { 19'002, 19'004, PortKind::control, 0, 3, 1 },
            { 19'003, 19'005, PortKind::audio, 2, 3, 1 },
            { 19'004, 19'005, PortKind::control, 0, 2, 2 },
            { 19'005, 19'006, PortKind::audio, 2, 3, 1 },
            { 19'006, 19'007, PortKind::audio },
            { 19'007, 19'008, PortKind::audio },
            { 19'008, 19'009, PortKind::audio }
        };
        addFactory(std::move(polyBass), juce::Colour(0xff7b9aff));
    });

    buildApart([&] {
        SavedModule arpMotion;
        arpMotion.key = "arp-motion";
        arpMotion.name = tr("node.arpMotion");
        describeFactory(arpMotion, Role::midiEffect,
                        "graph.library.category.midi",
                        "graph.library.arpMotion.description",
                        { "midi", "arpeggiator", "transpose", "velocity" });
        Parameters arp;
        if (const auto* arpeggiator = cydaw::engine::effectForType("arpege"))
            cydaw::engine::applyEffectDefaults(*arpeggiator, arp);
        Parameters arpTranspose;
        arpTranspose.midiTranspose = 12.0f;
        Parameters arpVelocity;
        arpVelocity.midiVelocityScale = 0.86f;
        arpMotion.nodes = {
            { 20'001, "graph-midi-input", tr("node.graphMidiInput"), { 0.06f, 0.42f }, {} },
            { 20'002, "arpege", tr("node.arpege"), { 0.30f, 0.42f }, arp },
            { 20'003, "midi-transpose", tr("node.midiTranspose"), { 0.52f, 0.42f }, arpTranspose },
            { 20'004, "velocity-map", tr("node.velocityMap"), { 0.72f, 0.42f }, arpVelocity },
            { 20'005, "graph-midi-output", tr("node.graphMidiOutput"), { 0.92f, 0.42f }, {} }
        };
        arpMotion.connections = {
            { 20'001, 20'002, PortKind::midi },
            { 20'002, 20'003, PortKind::midi },
            { 20'003, 20'004, PortKind::midi },
            { 20'004, 20'005, PortKind::midi }
        };
        addFactory(std::move(arpMotion), juce::Colour(0xffd17de4));
    });

    buildApart([&] {
        SavedModule noteShaper;
        noteShaper.key = "note-shaper";
        noteShaper.name = tr("node.noteShaper");
        describeFactory(noteShaper, Role::midiEffect,
                        "graph.library.category.midi",
                        "graph.library.noteShaper.description",
                        { "midi", "notes", "filter", "velocity" });
        Parameters noteRange;
        noteRange.midiMinimumNote = 36.0f;
        noteRange.midiMaximumNote = 96.0f;
        Parameters noteVelocity;
        noteVelocity.midiVelocityScale = 0.92f;
        noteShaper.nodes = {
            { 21'001, "graph-midi-input", tr("node.graphMidiInput"), { 0.08f, 0.42f }, {} },
            { 21'002, "note-filter", tr("node.noteFilter"), { 0.36f, 0.42f }, noteRange },
            { 21'003, "velocity-map", tr("node.velocityMap"), { 0.63f, 0.42f }, noteVelocity },
            { 21'004, "graph-midi-output", tr("node.graphMidiOutput"), { 0.90f, 0.42f }, {} }
        };
        noteShaper.connections = {
            { 21'001, 21'002, PortKind::midi },
            { 21'002, 21'003, PortKind::midi },
            { 21'003, 21'004, PortKind::midi }
        };
        addFactory(std::move(noteShaper), juce::Colour(0xffce78d8));
    });

    buildApart([&] {
        SavedModule unipolarLfo;
        unipolarLfo.key = "unipolar-lfo";
        unipolarLfo.name = tr("node.unipolarLfo");
        unipolarLfo.hasInput = false;
        unipolarLfo.hasOutput = true;
        unipolarLfo.outputKind = PortKind::control;
        describeFactory(unipolarLfo, Role::modulator,
                        "graph.library.category.modulators",
                        "graph.library.unipolarLfo.description",
                        { "modulation", "lfo", "unipolar", "value" });
        Parameters unipolarSource;
        unipolarSource.modulationRateHz = 0.5f;
        unipolarSource.modulationDepth = 1.0f;
        Parameters unipolarMap;
        unipolarMap.mapInputMinimum = -1.0f;
        unipolarMap.mapInputMaximum = 1.0f;
        unipolarMap.mapOutputMinimum = 0.0f;
        unipolarMap.mapOutputMaximum = 1.0f;
        unipolarLfo.nodes = {
            { 22'001, "lfo-core", tr("node.lfoCore"), { 0.16f, 0.42f }, unipolarSource },
            { 22'002, "map-range", tr("node.mapRange"), { 0.49f, 0.42f }, unipolarMap },
            { 22'003, "value-display", tr("node.valueDisplay"), { 0.70f, 0.42f }, {} },
            { 22'004, "graph-value-output", tr("node.graphValueOutput"), { 0.90f, 0.42f }, {} }
        };
        unipolarLfo.connections = {
            { 22'001, 22'002, PortKind::control },
            { 22'002, 22'003, PortKind::control },
            { 22'003, 22'004, PortKind::control }
        };
        addFactory(std::move(unipolarLfo), juce::Colour(0xff69cfa7));
    });

    buildApart([&] {
        SavedModule audioFollower;
        audioFollower.key = "audio-follower";
        audioFollower.name = tr("node.audioFollower");
        describeFactory(audioFollower, Role::modulator,
                        "graph.library.category.modulators",
                        "graph.library.audioFollower.description",
                        { "modulation", "audio", "envelope", "follower" });
        Parameters follower;
        follower.followerAttackSeconds = 0.008f;
        follower.followerReleaseSeconds = 0.14f;
        audioFollower.nodes = {
            { 23'001, "graph-audio-input-2", tr("node.graphAudioInput.2"), { 0.08f, 0.42f }, {} },
            { 23'002, "envelope-follower", tr("node.envelopeFollower"), { 0.40f, 0.42f }, follower },
            { 23'003, "value-display", tr("node.valueDisplay"), { 0.65f, 0.42f }, {} },
            { 23'004, "graph-value-output", tr("node.graphValueOutput"), { 0.88f, 0.42f }, {} }
        };
        audioFollower.connections = {
            { 23'001, 23'002, PortKind::audio, 2, 2, 1 },
            { 23'002, 23'003, PortKind::control, 0, 2, 1 },
            { 23'003, 23'004, PortKind::control, 0, 2, 1 }
        };
        addFactory(std::move(audioFollower), juce::Colour(0xff75d6ac));
    });

    buildApart([&] {
        SavedModule scopeSuite;
        scopeSuite.key = "scope-suite";
        scopeSuite.name = tr("node.scopeSuite");
        // The probes remain inline and the Graph publishes a transparent stereo
        // output, therefore its strict public contract is an audio effect.  Its
        // library category still presents it as an analysis utility.
        describeFactory(scopeSuite, Role::audioEffect,
                        "graph.library.category.analyzers",
                        "graph.library.scopeSuite.description",
                        { "analyzer", "spectrum", "waveform", "meter" });
        scopeSuite.nodes = {
            { 24'001, "graph-audio-input-2", tr("node.graphAudioInput.2"), { 0.06f, 0.42f }, {} },
            { 24'002, "oscilloscope", tr("node.oscilloscope"), { 0.30f, 0.42f }, {} },
            { 24'003, "spectrum", tr("node.spectrum"), { 0.52f, 0.42f }, {} },
            { 24'004, "level-meter", tr("node.levelMeter"), { 0.73f, 0.42f }, {} },
            { 24'005, "graph-audio-output-2", tr("node.graphAudioOutput.2"), { 0.92f, 0.42f }, {} }
        };
        scopeSuite.connections = {
            { 24'001, 24'002, PortKind::audio },
            { 24'002, 24'003, PortKind::audio },
            { 24'003, 24'004, PortKind::audio },
            { 24'004, 24'005, PortKind::audio }
        };
        addFactory(std::move(scopeSuite), juce::Colour(0xff6cb8ff));
    });

    // The spectrometer, as a device of the analysers: the sound through its
    // card untouched, the device panel drawing its spectrum and waterfall.
    buildApart([&] {
        SavedModule spectrometer;
        spectrometer.key = "spectrometer-analyzer";
        spectrometer.name = tr("node.spectrometer");
        describeFactory(spectrometer, Role::audioEffect,
                        "graph.library.category.analyzers",
                        "graph.library.spectrometer.description",
                        { "analyzer", "spectrum", "spectrometer", "spectrogram", "waterfall" });
        Parameters spectrometerValues;
        if (const auto* effect = cydaw::engine::effectForType("spectrometer"))
            cydaw::engine::applyEffectDefaults(*effect, spectrometerValues);
        spectrometer.nodes = {
            { 48'001, "graph-audio-input-2", tr("node.graphAudioInput.2"), { 0.06f, 0.42f }, {} },
            { 48'002, "spectrometer", tr("node.spectrometer"), { 0.48f, 0.42f }, spectrometerValues },
            { 48'003, "graph-audio-output-2", tr("node.graphAudioOutput.2"), { 0.92f, 0.42f }, {} }
        };
        spectrometer.connections = {
            { 48'001, 48'002, PortKind::audio },
            { 48'002, 48'003, PortKind::audio }
        };
        addFactory(std::move(spectrometer), juce::Colour(0xff6cb8ff));
    });

    buildApart([&] {
        SavedModule drumPunch;
        drumPunch.key = "drum-punch";
        drumPunch.name = tr("node.drumPunch");
        describeFactory(drumPunch, Role::audioEffect,
                        "graph.library.category.dynamics",
                        "graph.library.drumPunch.description",
                        { "effect", "drums", "punch", "compressor", "drive" });
        Parameters drumHighpass;
        drumHighpass.highpassCutoffHz = 42.0f;
        Parameters drumCompressor;
        drumCompressor.thresholdDb = -20.0f;
        drumCompressor.ratio = 5.0f;
        Parameters drumDrive;
        drumDrive.driveDb = 3.5f;
        Parameters drumClip;
        drumClip.clipThreshold = 0.96f;
        Parameters drumOutput;
        drumOutput.gainDb = -1.0f;
        drumPunch.nodes = {
            { 25'001, "graph-audio-input-2", tr("node.graphAudioInput.2"), { 0.03f, 0.42f }, {} },
            { 25'002, "highpass", tr("node.highpass"), { 0.18f, 0.42f }, drumHighpass },
            { 25'003, "compressor", tr("node.compressor"), { 0.35f, 0.42f }, drumCompressor },
            { 25'004, "drive", tr("node.drive"), { 0.53f, 0.42f }, drumDrive },
            { 25'005, "hard-clip", tr("node.hardClip"), { 0.69f, 0.42f }, drumClip },
            { 25'006, "gain", tr("node.gain"), { 0.82f, 0.42f }, drumOutput },
            { 25'007, "graph-audio-output-2", tr("node.graphAudioOutput.2"), { 0.96f, 0.42f }, {} }
        };
        drumPunch.connections = {
            { 25'001, 25'002, PortKind::audio },
            { 25'002, 25'003, PortKind::audio },
            { 25'003, 25'004, PortKind::audio },
            { 25'004, 25'005, PortKind::audio },
            { 25'005, 25'006, PortKind::audio },
            { 25'006, 25'007, PortKind::audio }
        };
        addFactory(std::move(drumPunch), juce::Colour(0xffffb44f));
    });

    buildApart([&] {
        SavedModule wideEcho;
        wideEcho.key = "wide-echo";
        wideEcho.name = tr("node.wideEcho");
        describeFactory(wideEcho, Role::audioEffect,
                        "graph.library.category.space",
                        "graph.library.wideEcho.description",
                        { "effect", "delay", "wide", "stereo", "echo" });
        Parameters wideDelay;
        wideDelay.delayMilliseconds = 420.0f;
        wideDelay.feedback = 0.42f;
        wideDelay.mix = 0.33f;
        Parameters echoWidth;
        echoWidth.stereoWidth = 1.42f;
        Parameters echoFilter;
        echoFilter.cutoffHz = 7'500.0f;
        wideEcho.nodes = {
            { 26'001, "graph-audio-input-2", tr("node.graphAudioInput.2"), { 0.05f, 0.42f }, {} },
            { 26'002, "delay", tr("node.delay"), { 0.30f, 0.42f }, wideDelay },
            { 26'003, "stereo-width", tr("node.stereoWidth"), { 0.54f, 0.42f }, echoWidth },
            { 26'004, "lowpass", tr("node.lowpass"), { 0.73f, 0.42f }, echoFilter },
            { 26'005, "graph-audio-output-2", tr("node.graphAudioOutput.2"), { 0.94f, 0.42f }, {} }
        };
        wideEcho.connections = {
            { 26'001, 26'002, PortKind::audio },
            { 26'002, 26'003, PortKind::audio },
            { 26'003, 26'004, PortKind::audio },
            { 26'004, 26'005, PortKind::audio }
        };
        addFactory(std::move(wideEcho), juce::Colour(0xffdb8fe8));
    });

    buildApart([&] {
        SavedModule darkRoom;
        darkRoom.key = "dark-room";
        darkRoom.name = tr("node.darkRoom");
        describeFactory(darkRoom, Role::audioEffect,
                        "graph.library.category.space",
                        "graph.library.darkRoom.description",
                        { "effect", "reverb", "dark", "room", "filter" });
        Parameters darkFilter;
        darkFilter.cutoffHz = 3'200.0f;
        darkFilter.resonance = 0.9f;
        Parameters darkReverb;
        darkReverb.roomSize = 0.88f;
        darkReverb.mix = 0.45f;
        Parameters darkTone;
        darkTone.tone = -0.16f;
        darkRoom.nodes = {
            { 27'001, "graph-audio-input-2", tr("node.graphAudioInput.2"), { 0.05f, 0.42f }, {} },
            { 27'002, "lowpass", tr("node.lowpass"), { 0.31f, 0.42f }, darkFilter },
            { 27'003, "reverb", tr("node.reverb"), { 0.55f, 0.42f }, darkReverb },
            { 27'004, "eq", tr("node.equalizer"), { 0.75f, 0.42f }, darkTone },
            { 27'005, "graph-audio-output-2", tr("node.graphAudioOutput.2"), { 0.94f, 0.42f }, {} }
        };
        darkRoom.connections = {
            { 27'001, 27'002, PortKind::audio },
            { 27'002, 27'003, PortKind::audio },
            { 27'003, 27'004, PortKind::audio },
            { 27'004, 27'005, PortKind::audio }
        };
        addFactory(std::move(darkRoom), juce::Colour(0xffaa79d8));
    });

    buildApart([&] {
        SavedModule monoFocus;
        monoFocus.key = "mono-focus";
        monoFocus.name = tr("node.monoFocus");
        describeFactory(monoFocus, Role::audioEffect,
                        "graph.library.category.utilities",
                        "graph.library.monoFocus.description",
                        { "utility", "mono", "stereo", "meter" });
        Parameters monoWidth;
        monoWidth.stereoWidth = 0.0f;
        monoFocus.nodes = {
            { 28'001, "graph-audio-input-2", tr("node.graphAudioInput.2"), { 0.07f, 0.42f }, {} },
            { 28'002, "stereo-width", tr("node.stereoWidth"), { 0.36f, 0.42f }, monoWidth },
            { 28'003, "level-meter", tr("node.levelMeter"), { 0.65f, 0.42f }, {} },
            { 28'004, "graph-audio-output-2", tr("node.graphAudioOutput.2"), { 0.91f, 0.42f }, {} }
        };
        monoFocus.connections = {
            { 28'001, 28'002, PortKind::audio },
            { 28'002, 28'003, PortKind::audio },
            { 28'003, 28'004, PortKind::audio }
        };
        addFactory(std::move(monoFocus), juce::Colour(0xff67cbd1));
    });

    buildApart([&] {
        SavedModule masterGuard;
        masterGuard.key = "master-guard";
        masterGuard.name = tr("node.masterGuard");
        describeFactory(masterGuard, Role::audioEffect,
                        "graph.library.category.mastering",
                        "graph.library.masterGuard.description",
                        { "effect", "mastering", "safety", "clip", "meter" });
        Parameters guardHighpass;
        guardHighpass.highpassCutoffHz = 25.0f;
        Parameters guardCompressor;
        guardCompressor.thresholdDb = -6.0f;
        guardCompressor.ratio = 2.0f;
        Parameters guardClip;
        guardClip.clipThreshold = 0.98f;
        masterGuard.nodes = {
            { 29'001, "graph-audio-input-2", tr("node.graphAudioInput.2"), { 0.03f, 0.42f }, {} },
            { 29'002, "dc-blocker", tr("node.dcBlocker"), { 0.19f, 0.42f }, {} },
            { 29'003, "highpass", tr("node.highpass"), { 0.35f, 0.42f }, guardHighpass },
            { 29'004, "compressor", tr("node.compressor"), { 0.52f, 0.42f }, guardCompressor },
            { 29'005, "hard-clip", tr("node.hardClip"), { 0.68f, 0.42f }, guardClip },
            { 29'006, "level-meter", tr("node.levelMeter"), { 0.82f, 0.42f }, {} },
            { 29'007, "graph-audio-output-2", tr("node.graphAudioOutput.2"), { 0.96f, 0.42f }, {} }
        };
        masterGuard.connections = {
            { 29'001, 29'002, PortKind::audio },
            { 29'002, 29'003, PortKind::audio },
            { 29'003, 29'004, PortKind::audio },
            { 29'004, 29'005, PortKind::audio },
            { 29'005, 29'006, PortKind::audio },
            { 29'006, 29'007, PortKind::audio }
        };
        addFactory(std::move(masterGuard), juce::Colour(0xffffce58));
    });

    buildApart([&] {
        SavedModule tremolo;
        tremolo.key = "tremolo";
        tremolo.name = tr("node.tremolo");
        describeFactory(tremolo, Role::audioEffect,
                        "graph.library.category.modulationEffects",
                        "graph.library.tremolo.description",
                        { "effect", "modulation", "tremolo", "lfo", "gain" });
        Parameters tremoloLfo;
        tremoloLfo.modulationRateHz = 4.0f;
        tremoloLfo.modulationDepth = 1.0f;
        Parameters tremoloMap;
        tremoloMap.mapInputMinimum = -1.0f;
        tremoloMap.mapInputMaximum = 1.0f;
        tremoloMap.mapOutputMinimum = -30.0f;
        tremoloMap.mapOutputMaximum = 0.0f;
        tremolo.nodes = {
            { 30'001, "graph-audio-input-2", tr("node.graphAudioInput.2"), { 0.05f, 0.30f }, {} },
            { 30'002, "lfo-core", tr("node.lfoCore"), { 0.20f, 0.72f }, tremoloLfo },
            { 30'003, "map-range", tr("node.mapRange"), { 0.48f, 0.72f }, tremoloMap },
            { 30'004, "gain", tr("node.gain"), { 0.56f, 0.30f }, {} },
            { 30'005, "graph-audio-output-2", tr("node.graphAudioOutput.2"), { 0.91f, 0.30f }, {} }
        };
        tremolo.connections = {
            { 30'001, 30'004, PortKind::audio },
            { 30'002, 30'003, PortKind::control, 0, 2, 1 },
            { 30'003, 30'004, PortKind::control, 0, 2, 2 },
            { 30'004, 30'005, PortKind::audio }
        };
        addFactory(std::move(tremolo), juce::Colour(0xff76d8a7));
    });

    buildApart([&] {
        SavedModule autoPan;
        autoPan.key = "auto-pan";
        autoPan.name = tr("node.autoPan");
        describeFactory(autoPan, Role::audioEffect,
                        "graph.library.category.modulationEffects",
                        "graph.library.autoPan.description",
                        { "effect", "modulation", "pan", "lfo", "stereo" });
        Parameters panLfo;
        panLfo.modulationRateHz = 0.25f;
        panLfo.modulationDepth = 1.0f;
        autoPan.nodes = {
            { 31'001, "graph-audio-input-2", tr("node.graphAudioInput.2"), { 0.06f, 0.30f }, {} },
            { 31'002, "lfo-core", tr("node.lfoCore"), { 0.24f, 0.72f }, panLfo },
            { 31'003, "gain-pan", tr("node.gainPan"), { 0.56f, 0.30f }, {} },
            { 31'004, "graph-audio-output-2", tr("node.graphAudioOutput.2"), { 0.91f, 0.30f }, {} }
        };
        autoPan.connections = {
            { 31'001, 31'003, PortKind::audio },
            { 31'002, 31'003, PortKind::control, 0, 2, 2 },
            { 31'003, 31'004, PortKind::audio }
        };
        addFactory(std::move(autoPan), juce::Colour(0xff69cfd2));
    });

    buildApart([&] {
        SavedModule dynamicFilter;
        dynamicFilter.key = "dynamic-filter";
        dynamicFilter.name = tr("node.dynamicFilter");
        describeFactory(dynamicFilter, Role::audioEffect,
                        "graph.library.category.filters",
                        "graph.library.dynamicFilter.description",
                        { "effect", "filter", "envelope", "dynamic", "modulation" });
        Parameters filterFollower;
        filterFollower.followerAttackSeconds = 0.004f;
        filterFollower.followerReleaseSeconds = 0.18f;
        Parameters filterMap;
        filterMap.mapInputMinimum = 0.0f;
        filterMap.mapInputMaximum = 1.0f;
        filterMap.mapOutputMinimum = 180.0f;
        filterMap.mapOutputMaximum = 12'000.0f;
        Parameters dynamicCutoff;
        dynamicCutoff.cutoffHz = 1'000.0f;
        dynamicCutoff.resonance = 1.1f;
        dynamicFilter.nodes = {
            { 32'001, "graph-audio-input-2", tr("node.graphAudioInput.2"), { 0.05f, 0.30f }, {} },
            { 32'002, "envelope-follower", tr("node.envelopeFollower"), { 0.22f, 0.72f }, filterFollower },
            { 32'003, "map-range", tr("node.mapRange"), { 0.49f, 0.72f }, filterMap },
            { 32'004, "lowpass", tr("node.lowpass"), { 0.58f, 0.30f }, dynamicCutoff },
            { 32'005, "graph-audio-output-2", tr("node.graphAudioOutput.2"), { 0.92f, 0.30f }, {} }
        };
        dynamicFilter.connections = {
            { 32'001, 32'002, PortKind::audio, 2, 2, 1 },
            { 32'001, 32'004, PortKind::audio, 2, 2, 1 },
            { 32'002, 32'003, PortKind::control, 0, 2, 1 },
            { 32'003, 32'004, PortKind::control, 0, 2, 2 },
            { 32'004, 32'005, PortKind::audio }
        };
        addFactory(std::move(dynamicFilter), juce::Colour(0xff62bfe8));
    });

    buildApart([&] {
        SavedModule octaveUp;
        octaveUp.key = "octave-up";
        octaveUp.name = tr("node.octaveUp");
        describeFactory(octaveUp, Role::midiEffect,
                        "graph.library.category.midi",
                        "graph.library.octaveUp.description",
                        { "midi", "transpose", "octave", "performance" });
        Parameters octaveTranspose;
        octaveTranspose.midiTranspose = 12.0f;
        octaveUp.nodes = {
            { 33'001, "graph-midi-input", tr("node.graphMidiInput"), { 0.10f, 0.42f }, {} },
            { 33'002, "midi-transpose", tr("node.midiTranspose"), { 0.50f, 0.42f }, octaveTranspose },
            { 33'003, "graph-midi-output", tr("node.graphMidiOutput"), { 0.90f, 0.42f }, {} }
        };
        octaveUp.connections = {
            { 33'001, 33'002, PortKind::midi },
            { 33'002, 33'003, PortKind::midi }
        };
        addFactory(std::move(octaveUp), juce::Colour(0xffd17de4));
    });

    buildApart([&] {
        SavedModule bassZone;
        bassZone.key = "bass-zone";
        bassZone.name = tr("node.bassZone");
        describeFactory(bassZone, Role::midiEffect,
                        "graph.library.category.midi",
                        "graph.library.bassZone.description",
                        { "midi", "keyboard", "zone", "bass", "velocity" });
        Parameters bassRange;
        bassRange.midiMinimumNote = 0.0f;
        bassRange.midiMaximumNote = 59.0f;
        Parameters bassVelocity;
        bassVelocity.midiVelocityScale = 0.95f;
        bassZone.nodes = {
            { 34'001, "graph-midi-input", tr("node.graphMidiInput"), { 0.07f, 0.42f }, {} },
            { 34'002, "note-filter", tr("node.noteFilter"), { 0.35f, 0.42f }, bassRange },
            { 34'003, "velocity-map", tr("node.velocityMap"), { 0.64f, 0.42f }, bassVelocity },
            { 34'004, "graph-midi-output", tr("node.graphMidiOutput"), { 0.92f, 0.42f }, {} }
        };
        bassZone.connections = {
            { 34'001, 34'002, PortKind::midi },
            { 34'002, 34'003, PortKind::midi },
            { 34'003, 34'004, PortKind::midi }
        };
        addFactory(std::move(bassZone), juce::Colour(0xffc96fd4));
    });

    buildApart([&] {
        SavedModule polyPluck;
        polyPluck.key = "poly-pluck";
        polyPluck.name = tr("node.polyPluck");
        polyPluck.hasInput = true;
        polyPluck.inputKind = PortKind::midi;
        polyPluck.hasOutput = true;
        polyPluck.outputKind = PortKind::audio;
        describeFactory(polyPluck, Role::instrument,
                        "graph.library.category.instruments",
                        "graph.library.polyPluck.description",
                        { "instrument", "polyphonic", "pluck", "delay", "synth" });
        Parameters pluckOscillator;
        pluckOscillator.oscillatorWaveform = 2.0f;
        pluckOscillator.oscillatorLevel = 0.15f;
        Parameters pluckEnvelope;
        pluckEnvelope.attackMilliseconds = 2.0f;
        pluckEnvelope.decayMilliseconds = 260.0f;
        pluckEnvelope.sustainLevel = 0.08f;
        pluckEnvelope.releaseMilliseconds = 180.0f;
        Parameters pluckFilter;
        pluckFilter.cutoffHz = 4'600.0f;
        pluckFilter.resonance = 1.3f;
        Parameters pluckDelay;
        pluckDelay.delayMilliseconds = 285.0f;
        pluckDelay.feedback = 0.28f;
        pluckDelay.mix = 0.22f;
        polyPluck.nodes = {
            { 35'001, "graph-midi-input", tr("node.graphMidiInput"), { 0.02f, 0.10f }, {} },
            { 35'002, "voice-allocator", tr("node.voiceAllocator"), { 0.14f, 0.42f }, {} },
            { 35'003, "poly-oscillator", tr("node.polyOscillator"), { 0.31f, 0.12f }, pluckOscillator },
            { 35'004, "envelope", tr("node.envelope"), { 0.31f, 0.74f }, pluckEnvelope },
            { 35'005, "gain", tr("node.gain"), { 0.48f, 0.42f }, {} },
            { 35'006, "voice-filter", tr("node.voiceFilter"), { 0.64f, 0.42f }, pluckFilter },
            { 35'007, "delay", tr("node.delay"), { 0.79f, 0.42f }, pluckDelay },
            { 35'008, "graph-audio-output-2", tr("node.graphAudioOutput.2"), { 0.95f, 0.42f }, {} }
        };
        polyPluck.connections = {
            { 35'001, 35'002, PortKind::midi, 0, 2, 1 },
            { 35'002, 35'003, PortKind::control, 0, 2, 1 },
            { 35'002, 35'004, PortKind::control, 0, 3, 1 },
            { 35'003, 35'005, PortKind::audio, 2, 3, 1 },
            { 35'004, 35'005, PortKind::control, 0, 2, 2 },
            { 35'005, 35'006, PortKind::audio, 2, 3, 1 },
            { 35'006, 35'007, PortKind::audio },
            { 35'007, 35'008, PortKind::audio }
        };
        addFactory(std::move(polyPluck), juce::Colour(0xffaa87ff));
    });

    buildApart([&] {
        SavedModule polyLead;
        polyLead.key = "poly-lead";
        polyLead.name = tr("node.polyLead");
        polyLead.hasInput = true;
        polyLead.inputKind = PortKind::midi;
        polyLead.hasOutput = true;
        polyLead.outputKind = PortKind::audio;
        describeFactory(polyLead, Role::instrument,
                        "graph.library.category.instruments",
                        "graph.library.polyLead.description",
                        { "instrument", "polyphonic", "lead", "drive", "synth" });
        Parameters leadOscillator;
        leadOscillator.oscillatorWaveform = 1.0f;
        leadOscillator.oscillatorLevel = 0.13f;
        Parameters leadEnvelope;
        leadEnvelope.attackMilliseconds = 8.0f;
        leadEnvelope.decayMilliseconds = 320.0f;
        leadEnvelope.sustainLevel = 0.62f;
        leadEnvelope.releaseMilliseconds = 420.0f;
        Parameters leadFilter;
        leadFilter.cutoffHz = 2'800.0f;
        leadFilter.resonance = 1.45f;
        Parameters leadDrive;
        leadDrive.driveDb = 5.0f;
        Parameters leadDelay;
        leadDelay.delayMilliseconds = 340.0f;
        leadDelay.feedback = 0.26f;
        leadDelay.mix = 0.18f;
        polyLead.nodes = {
            { 36'001, "graph-midi-input", tr("node.graphMidiInput"), { 0.02f, 0.10f }, {} },
            { 36'002, "voice-allocator", tr("node.voiceAllocator"), { 0.13f, 0.42f }, {} },
            { 36'003, "poly-oscillator", tr("node.polyOscillator"), { 0.28f, 0.12f }, leadOscillator },
            { 36'004, "envelope", tr("node.envelope"), { 0.28f, 0.74f }, leadEnvelope },
            { 36'005, "gain", tr("node.gain"), { 0.43f, 0.42f }, {} },
            { 36'006, "voice-filter", tr("node.voiceFilter"), { 0.57f, 0.42f }, leadFilter },
            { 36'007, "drive", tr("node.drive"), { 0.70f, 0.42f }, leadDrive },
            { 36'008, "delay", tr("node.delay"), { 0.82f, 0.42f }, leadDelay },
            { 36'009, "graph-audio-output-2", tr("node.graphAudioOutput.2"), { 0.96f, 0.42f }, {} }
        };
        polyLead.connections = {
            { 36'001, 36'002, PortKind::midi, 0, 2, 1 },
            { 36'002, 36'003, PortKind::control, 0, 2, 1 },
            { 36'002, 36'004, PortKind::control, 0, 3, 1 },
            { 36'003, 36'005, PortKind::audio, 2, 3, 1 },
            { 36'004, 36'005, PortKind::control, 0, 2, 2 },
            { 36'005, 36'006, PortKind::audio, 2, 3, 1 },
            { 36'006, 36'007, PortKind::audio },
            { 36'007, 36'008, PortKind::audio },
            { 36'008, 36'009, PortKind::audio }
        };
        addFactory(std::move(polyLead), juce::Colour(0xff8f7cff));
    });

    buildApart([&] {
        SavedModule polyKeys;
        polyKeys.key = "poly-keys";
        polyKeys.name = tr("node.polyKeys");
        polyKeys.hasInput = true;
        polyKeys.inputKind = PortKind::midi;
        polyKeys.hasOutput = true;
        polyKeys.outputKind = PortKind::audio;
        describeFactory(polyKeys, Role::instrument,
                        "graph.library.category.completeInstruments",
                        "graph.library.polyKeys.description",
                        { "example", "instrument", "polyphonic", "keys", "synth",
                          "compressor", "stereo", "spectrum" });
        Parameters keysOscillator;
        keysOscillator.oscillatorWaveform = 1.0f;
        keysOscillator.oscillatorLevel = 0.12f;
        Parameters keysEnvelope;
        keysEnvelope.attackMilliseconds = 18.0f;
        keysEnvelope.decayMilliseconds = 420.0f;
        keysEnvelope.sustainLevel = 0.68f;
        keysEnvelope.releaseMilliseconds = 620.0f;
        Parameters keysFilter;
        keysFilter.cutoffHz = 5'800.0f;
        keysFilter.resonance = 1.05f;
        Parameters keysCompressor;
        keysCompressor.thresholdDb = -14.0f;
        keysCompressor.ratio = 2.4f;
        Parameters keysWidth;
        keysWidth.stereoWidth = 1.22f;
        polyKeys.nodes = {
            { 37'001, "graph-midi-input", tr("node.graphMidiInput"), { 0.01f, 0.02f }, {} },
            { 37'002, "voice-allocator", tr("node.voiceAllocator"), { 0.01f, 0.30f }, {} },
            { 37'003, "poly-oscillator", tr("node.polyOscillator"), { 0.36f, 0.02f }, keysOscillator },
            { 37'004, "envelope", tr("node.envelope"), { 0.01f, 0.92f }, keysEnvelope },
            { 37'005, "gain", tr("node.gain"), { 0.36f, 0.38f }, {} },
            { 37'006, "voice-filter", tr("node.voiceFilter"), { 1.00f, 0.02f }, keysFilter },
            { 37'007, "compressor", tr("node.compressor"), { 1.00f, 0.38f }, keysCompressor },
            { 37'008, "stereo-width", tr("node.stereoWidth"), { 0.70f, 0.60f }, keysWidth },
            { 37'009, "spectrum", tr("node.spectrum"), { 0.36f, 0.82f }, {} },
            { 37'010, "graph-audio-output-2", tr("node.graphAudioOutput.2"), { 1.00f, 0.70f }, {} }
        };
        for (auto& node : polyKeys.nodes)
            if (node.typeKey == "gain" || node.typeKey == "compressor"
                || node.typeKey == "stereo-width")
                node.collapsed = true;
        polyKeys.connections = {
            { 37'001, 37'002, PortKind::midi, 0, 2, 1 },
            { 37'002, 37'003, PortKind::control, 0, 2, 1 },
            { 37'002, 37'004, PortKind::control, 0, 3, 1 },
            { 37'003, 37'005, PortKind::audio, 2, 3, 1 },
            { 37'004, 37'005, PortKind::control, 0, 2, 2 },
            { 37'005, 37'006, PortKind::audio, 2, 3, 1 },
            { 37'006, 37'007, PortKind::audio },
            { 37'007, 37'008, PortKind::audio },
            { 37'008, 37'009, PortKind::audio },
            { 37'009, 37'010, PortKind::audio }
        };
        addFactory(std::move(polyKeys), juce::Colour(0xff7ea6ff));
    });

    buildApart([&] {
        SavedModule ambientGlass;
        ambientGlass.key = "ambient-glass";
        ambientGlass.name = tr("node.ambientGlass");
        ambientGlass.hasInput = true;
        ambientGlass.inputKind = PortKind::midi;
        ambientGlass.hasOutput = true;
        ambientGlass.outputKind = PortKind::audio;
        describeFactory(ambientGlass, Role::instrument,
                        "graph.library.category.completeInstruments",
                        "graph.library.ambientGlass.description",
                        { "example", "instrument", "polyphonic", "ambient", "glass",
                          "delay", "reverb", "stereo", "spectrum" });
        Parameters glassOscillator;
        glassOscillator.oscillatorWaveform = 0.0f;
        glassOscillator.oscillatorLevel = 0.10f;
        Parameters glassEnvelope;
        glassEnvelope.attackMilliseconds = 360.0f;
        glassEnvelope.decayMilliseconds = 1'200.0f;
        glassEnvelope.sustainLevel = 0.54f;
        glassEnvelope.releaseMilliseconds = 2'800.0f;
        Parameters glassFilter;
        glassFilter.cutoffHz = 7'200.0f;
        glassFilter.resonance = 1.65f;
        Parameters glassDelay;
        glassDelay.delayMilliseconds = 460.0f;
        glassDelay.feedback = 0.48f;
        glassDelay.mix = 0.31f;
        Parameters glassReverb;
        glassReverb.roomSize = 0.89f;
        glassReverb.mix = 0.42f;
        Parameters glassWidth;
        glassWidth.stereoWidth = 1.55f;
        ambientGlass.nodes = {
            { 38'001, "graph-midi-input", tr("node.graphMidiInput"), { 0.01f, 0.02f }, {} },
            { 38'002, "voice-allocator", tr("node.voiceAllocator"), { 0.01f, 0.30f }, {} },
            { 38'003, "poly-oscillator", tr("node.polyOscillator"), { 0.36f, 0.02f }, glassOscillator },
            { 38'004, "envelope", tr("node.envelope"), { 0.01f, 0.92f }, glassEnvelope },
            { 38'005, "gain", tr("node.gain"), { 0.36f, 0.38f }, {} },
            { 38'006, "voice-filter", tr("node.voiceFilter"), { 1.00f, 0.02f }, glassFilter },
            { 38'007, "delay", tr("node.delay"), { 1.00f, 0.38f }, glassDelay },
            { 38'008, "reverb", tr("node.reverb"), { 0.36f, 0.60f }, glassReverb },
            { 38'009, "stereo-width", tr("node.stereoWidth"), { 0.70f, 0.60f }, glassWidth },
            { 38'010, "spectrum", tr("node.spectrum"), { 0.36f, 0.82f }, {} },
            { 38'011, "graph-audio-output-2", tr("node.graphAudioOutput.2"), { 1.00f, 0.70f }, {} }
        };
        for (auto& node : ambientGlass.nodes)
            if (node.typeKey == "gain" || node.typeKey == "delay"
                || node.typeKey == "reverb" || node.typeKey == "stereo-width")
                node.collapsed = true;
        ambientGlass.connections = {
            { 38'001, 38'002, PortKind::midi, 0, 2, 1 },
            { 38'002, 38'003, PortKind::control, 0, 2, 1 },
            { 38'002, 38'004, PortKind::control, 0, 3, 1 },
            { 38'003, 38'005, PortKind::audio, 2, 3, 1 },
            { 38'004, 38'005, PortKind::control, 0, 2, 2 },
            { 38'005, 38'006, PortKind::audio, 2, 3, 1 },
            { 38'006, 38'007, PortKind::audio },
            { 38'007, 38'008, PortKind::audio },
            { 38'008, 38'009, PortKind::audio },
            { 38'009, 38'010, PortKind::audio },
            { 38'010, 38'011, PortKind::audio }
        };
        addFactory(std::move(ambientGlass), juce::Colour(0xff79d6e8));
    });

    buildApart([&] {
        SavedModule electricOrgan;
        electricOrgan.key = "electric-organ";
        electricOrgan.name = tr("node.electricOrgan");
        electricOrgan.hasInput = true;
        electricOrgan.inputKind = PortKind::midi;
        electricOrgan.hasOutput = true;
        electricOrgan.outputKind = PortKind::audio;
        describeFactory(electricOrgan, Role::instrument,
                        "graph.library.category.completeInstruments",
                        "graph.library.electricOrgan.description",
                        { "example", "instrument", "polyphonic", "organ", "drive",
                          "compressor", "stereo", "meter" });
        Parameters organOscillator;
        organOscillator.oscillatorWaveform = 3.0f;
        organOscillator.oscillatorLevel = 0.10f;
        Parameters organEnvelope;
        organEnvelope.attackMilliseconds = 12.0f;
        organEnvelope.decayMilliseconds = 80.0f;
        organEnvelope.sustainLevel = 0.88f;
        organEnvelope.releaseMilliseconds = 210.0f;
        Parameters organFilter;
        organFilter.cutoffHz = 6'400.0f;
        organFilter.resonance = 0.82f;
        Parameters organDrive;
        organDrive.driveDb = 6.5f;
        Parameters organCompressor;
        organCompressor.thresholdDb = -18.0f;
        organCompressor.ratio = 3.2f;
        Parameters organWidth;
        organWidth.stereoWidth = 1.18f;
        electricOrgan.nodes = {
            { 39'001, "graph-midi-input", tr("node.graphMidiInput"), { 0.01f, 0.02f }, {} },
            { 39'002, "voice-allocator", tr("node.voiceAllocator"), { 0.01f, 0.30f }, {} },
            { 39'003, "poly-oscillator", tr("node.polyOscillator"), { 0.36f, 0.02f }, organOscillator },
            { 39'004, "envelope", tr("node.envelope"), { 0.01f, 0.92f }, organEnvelope },
            { 39'005, "gain", tr("node.gain"), { 0.36f, 0.38f }, {} },
            { 39'006, "voice-filter", tr("node.voiceFilter"), { 1.00f, 0.02f }, organFilter },
            { 39'007, "drive", tr("node.drive"), { 1.00f, 0.38f }, organDrive },
            { 39'008, "compressor", tr("node.compressor"), { 0.36f, 0.60f }, organCompressor },
            { 39'009, "stereo-width", tr("node.stereoWidth"), { 0.70f, 0.60f }, organWidth },
            { 39'010, "level-meter", tr("node.levelMeter"), { 0.36f, 0.82f }, {} },
            { 39'011, "graph-audio-output-2", tr("node.graphAudioOutput.2"), { 1.00f, 0.70f }, {} }
        };
        for (auto& node : electricOrgan.nodes)
            if (node.typeKey == "gain" || node.typeKey == "drive"
                || node.typeKey == "compressor" || node.typeKey == "stereo-width")
                node.collapsed = true;
        electricOrgan.connections = {
            { 39'001, 39'002, PortKind::midi, 0, 2, 1 },
            { 39'002, 39'003, PortKind::control, 0, 2, 1 },
            { 39'002, 39'004, PortKind::control, 0, 3, 1 },
            { 39'003, 39'005, PortKind::audio, 2, 3, 1 },
            { 39'004, 39'005, PortKind::control, 0, 2, 2 },
            { 39'005, 39'006, PortKind::audio, 2, 3, 1 },
            { 39'006, 39'007, PortKind::audio },
            { 39'007, 39'008, PortKind::audio },
            { 39'008, 39'009, PortKind::audio },
            { 39'009, 39'010, PortKind::audio },
            { 39'010, 39'011, PortKind::audio }
        };
        addFactory(std::move(electricOrgan), juce::Colour(0xffffb45d));
    });

    buildApart([&] {
        SavedModule percussiveBell;
        percussiveBell.key = "percussive-bell";
        percussiveBell.name = tr("node.percussiveBell");
        percussiveBell.hasInput = true;
        percussiveBell.inputKind = PortKind::midi;
        percussiveBell.hasOutput = true;
        percussiveBell.outputKind = PortKind::audio;
        describeFactory(percussiveBell, Role::instrument,
                        "graph.library.category.completeInstruments",
                        "graph.library.percussiveBell.description",
                        { "example", "instrument", "polyphonic", "bell", "percussive",
                          "delay", "reverb", "spectrum" });
        Parameters bellOscillator;
        bellOscillator.oscillatorWaveform = 2.0f;
        bellOscillator.oscillatorLevel = 0.13f;
        Parameters bellEnvelope;
        bellEnvelope.attackMilliseconds = 1.0f;
        bellEnvelope.decayMilliseconds = 680.0f;
        bellEnvelope.sustainLevel = 0.03f;
        bellEnvelope.releaseMilliseconds = 1'450.0f;
        Parameters bellFilter;
        bellFilter.cutoffHz = 9'400.0f;
        bellFilter.resonance = 1.9f;
        Parameters bellDelay;
        bellDelay.delayMilliseconds = 225.0f;
        bellDelay.feedback = 0.34f;
        bellDelay.mix = 0.20f;
        Parameters bellReverb;
        bellReverb.roomSize = 0.76f;
        bellReverb.mix = 0.29f;
        percussiveBell.nodes = {
            { 40'001, "graph-midi-input", tr("node.graphMidiInput"), { 0.01f, 0.02f }, {} },
            { 40'002, "voice-allocator", tr("node.voiceAllocator"), { 0.01f, 0.30f }, {} },
            { 40'003, "poly-oscillator", tr("node.polyOscillator"), { 0.36f, 0.02f }, bellOscillator },
            { 40'004, "envelope", tr("node.envelope"), { 0.01f, 0.92f }, bellEnvelope },
            { 40'005, "gain", tr("node.gain"), { 0.36f, 0.38f }, {} },
            { 40'006, "voice-filter", tr("node.voiceFilter"), { 1.00f, 0.02f }, bellFilter },
            { 40'007, "delay", tr("node.delay"), { 1.00f, 0.38f }, bellDelay },
            { 40'008, "reverb", tr("node.reverb"), { 0.36f, 0.60f }, bellReverb },
            { 40'009, "spectrum", tr("node.spectrum"), { 0.36f, 0.82f }, {} },
            { 40'010, "graph-audio-output-2", tr("node.graphAudioOutput.2"), { 1.00f, 0.70f }, {} }
        };
        for (auto& node : percussiveBell.nodes)
            if (node.typeKey == "gain" || node.typeKey == "delay"
                || node.typeKey == "reverb")
                node.collapsed = true;
        percussiveBell.connections = {
            { 40'001, 40'002, PortKind::midi, 0, 2, 1 },
            { 40'002, 40'003, PortKind::control, 0, 2, 1 },
            { 40'002, 40'004, PortKind::control, 0, 3, 1 },
            { 40'003, 40'005, PortKind::audio, 2, 3, 1 },
            { 40'004, 40'005, PortKind::control, 0, 2, 2 },
            { 40'005, 40'006, PortKind::audio, 2, 3, 1 },
            { 40'006, 40'007, PortKind::audio },
            { 40'007, 40'008, PortKind::audio },
            { 40'008, 40'009, PortKind::audio },
            { 40'009, 40'010, PortKind::audio }
        };
        addFactory(std::move(percussiveBell), juce::Colour(0xffffcf6b));
    });
    factoryModulesInstalled_ = true;
}

void NodeGraphComponent::initialiseDefaultGraph()
{
    valueGraphMode_ = false;
    graphRole_ = cydaw::graph::GraphRole::audioEffect;
    nodes_.clear();
    connections_.clear();
    selectedNodes_.clear();
    activeFactoryPresetKey_.clear();
    nextNodeId_ = 1;

    const auto addDefault = [this](const juce::String& key, const juce::Point<float> position) {
        if (const auto* type = findType(key))
            addNodeFromType(*type, position);
    };
    addDefault("graph-audio-input-2", { 0.08f, 0.42f });
    addDefault("graph-audio-output-2", { 0.78f, 0.42f });
    (void) connectNodes(1, 2);
    selectedNodes_.clear();
    graphInterfaceState_ = createDefaultGraphInterface();
    synchroniseGraphInterface();
    rebuildEmbeddedSliders();
}

void NodeGraphComponent::initialiseEmptyMidiTrackGraph()
{
    closeModuleGraphEditor();
    closeModuleInterfaceEditor();
    valueGraphMode_ = false;
    graphRole_ = cydaw::graph::GraphRole::generic;
    initialiseCatalog();
    ensureFactoryModules();
    nodes_.clear();
    connections_.clear();
    selectedNodes_.clear();
    activeFactoryPresetKey_.clear();
    nextNodeId_ = 1;

    if (const auto* input = findType("midi-input"))
        addNodeFromType(*input, { 0.08f, 0.42f });
    if (const auto* output = findType("master"))
        addNodeFromType(*output, { 0.78f, 0.42f });

    selectedNodes_.clear();
    graphInterfaceState_ = createDefaultGraphInterface();
    synchroniseGraphInterface();
    rebuildEmbeddedSliders();
    resetViewTransform();
    applyLibraryFilter();
    resized();
    refreshGraphAssetValidation();
    repaint();
}

} // namespace cydaw::app
