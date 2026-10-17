#pragma once

#include <cydaw/engine/KickSynth.h>
#include <cydaw/engine/MidiEffects.h>
#include <cydaw/engine/PrismSynth.h>
#include <cydaw/engine/Sampler.h>
#include <cydaw/engine/SoundModel.h>
#include <cydaw/engine/DrumKit.h>
#include <cydaw/engine/SynthFamily.h>
#include <cydaw/engine/WavetableShape.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace cydaw::engine {

enum class PolyOscillatorWaveform : std::uint8_t {
    sine = 0,
    triangle,
    saw,
    square,
    // Appended: a serialised plan stores this as an integer, so the four
    // original identities must keep their values.
    wavetable,
    string,
    // A recorded note made playable (SoundModel.h): its partials, noise and
    // transient, at any pitch and velocity.
    soundModel,
    // Recorded notes played back as they are (Sampler.h), in stereo.
    sampler,
    // Prisme (PrismSynth.h): a whole synthesiser per voice, its own
    // envelopes and filter, in stereo.
    prism,
    // Kick (KickSynth.h): a kick drum per note, its own envelope; it plays
    // out whatever the key does.
    kick,
    // A synth of the family (SynthFamily.h): Ambre and the others, a whole
    // synthesiser per voice, in stereo.
    family,
    // A rack of pads: its voices are silent, each pad an instrument of its
    // own that the track's engine plays (the "drum-rack" card).
    rack
};

// The sources that play a recording at its own level (a sound model, a
// sampler): their level goes up to unity, the oscillators' to one half.
[[nodiscard]] constexpr bool playsRecordingLevel(const PolyOscillatorWaveform waveform) noexcept
{
    return waveform == PolyOscillatorWaveform::soundModel || waveform == PolyOscillatorWaveform::sampler
           || waveform == PolyOscillatorWaveform::prism || waveform == PolyOscillatorWaveform::kick
           || waveform == PolyOscillatorWaveform::family;
}

// Immutable render plan compiled from the visible instrument graph. The audio
// thread only consumes this compact payload; graph traversal and validation
// remain on the UI thread.
struct PolySynthGraphPlan final {
    bool enabled { false };
    std::size_t graphNodeCount { 6 };
    int voiceCount { 16 };
    // How the notes share the voices (Prisme has its own): 0 poly; 1 mono, a
    // note makes the last one give way and slides from it; 2 legato, the
    // same, sliding and without a new attack only when it was played over
    // the last one. The slide, in seconds.
    int voiceMode {};
    float glideSeconds {};
    PolyOscillatorWaveform waveform { PolyOscillatorWaveform::sine };
    float oscillatorLevel { 0.14f };
    float attackSeconds { 0.008f };
    float decaySeconds { 0.12f };
    float sustainLevel { 0.72f };
    float releaseSeconds { 0.28f };
    float filterCutoffHz { 12'000.0f };
    float filterResonance { 0.707f };
    int transposeSemitones {};
    int minimumMidiNote {};
    int maximumMidiNote { 127 };
    float velocityScale { 1.0f };
    // Only the mode that uses them reads these. A wavetable voice needs where
    // to read between its frames and how far to spread its unison; a string
    // voice needs how fast its loop loses the highs and how long it rings.
    float wavetablePosition { 0.35f };
    float wavetableDetuneCents { 8.0f };
    float stringDamping { 0.35f };
    float stringDecaySeconds { 4.0f };
    // The table this instrument was shaped with. Null means the built-in
    // bank. A pointer rather than a value because this plan is copied once
    // per voice per block.
    WavetableBankRef wavetable;
    // A sound model voice: the model (null: silent) and how it plays.
    SoundModelRef soundModel;
    SoundModelValues soundModelValues { defaultSoundModelValues() };
    // A sampler voice: its map (null: silent) and how it plays.
    SamplerMapRef sampler;
    SamplerValues samplerValues { defaultSamplerValues() };
    // Its card's controllers (-1: where the map starts them).
    SamplerControllers samplerControllers { unsetSamplerControllers() };
    // A Prisme voice: its settings and its oscillators' tables (null: the
    // factory tables its settings name, which a host resolves before playing).
    prism::PrismValues prismValues { prism::defaultValues() };
    prism::TablesRef prismTables;
    // A Kick voice: its settings.
    kick::KickValues kickValues { kick::defaultValues() };
    // A voice of the synth family: which model, its settings.
    synth::Model synthModel { synth::Model::none };
    synth::Values synthValues {};
    // A sampler playing a drum kit (Batterie): its mix, piece by piece.
    bool drumKit {};
    drum::Values drumValues { drum::neutralValues() };
    // The MIDI effects the notes go through before the voices (an
    // arpeggiator, chords…), in order; null for none. A pointer: the plan is
    // copied once per voice per block.
    MidiEffectsRef midiEffects;

    [[nodiscard]] bool operator==(const PolySynthGraphPlan&) const = default;
};

// A voice parameter a control cable can move: an LFO on the voice filter's
// cutoff, a macro on the wavetable's position. The cable's source is a control
// node of the instrument's effect chain, which evaluates it every block; the
// voices read its value at the start of the next one.
enum class PolyVoiceTarget : std::uint8_t {
    filterCutoff,
    filterResonance,
    oscillatorLevel,
    wavetablePosition,
    wavetableDetune,
    stringDamping,
    // A setting of the instrument itself, by its index in its values:
    // Prisme's, Kick's, a sampler's, a sound model's (automation lanes).
    prismValue,
    kickValue,
    samplerValue,
    soundModelValue,
    // A synth of the family's (appended: stored as a number).
    synthValue,
    // A Batterie's.
    drumValue,
    // A sampler card's controller (its "sampler.ccN"), by its number.
    samplerController
};

// A voice parameter and, for the instruments' own settings, which one.
struct PolyVoiceParameter final {
    PolyVoiceTarget target { PolyVoiceTarget::filterCutoff };
    std::uint16_t index {};

    [[nodiscard]] bool operator==(const PolyVoiceParameter&) const = default;
};

struct PolyVoiceModulation final {
    std::uint64_t controlNodeId {};
    PolyVoiceTarget target { PolyVoiceTarget::filterCutoff };
    // 0: the cable sets the value; otherwise it modulates the voice's own
    // value by that much of the range per unit (below 0, the other way).
    float depth {};
    // An instrument's own setting: which one, its knob's range, whether the
    // knob turns on a logarithmic scale (a frequency, a time).
    std::uint16_t index {};
    float minimum {};
    float maximum { 1.0f };
    bool logarithmic {};

    [[nodiscard]] bool operator==(const PolyVoiceModulation&) const = default;
};

// A value a cable or a lane delivers, written into a plan, clamped as the
// knob is (an instrument's own settings come clamped: the lane's points are).
inline void applyPolyVoiceModulation(PolySynthGraphPlan& plan, const PolyVoiceTarget target, const float value,
                                     const std::size_t index = 0) noexcept
{
    const auto into = [index, value](auto& values) {
        if (index < values.size())
            values[index] = value;
    };
    switch (target) {
    case PolyVoiceTarget::prismValue: into(plan.prismValues); break;
    case PolyVoiceTarget::kickValue: into(plan.kickValues); break;
    case PolyVoiceTarget::samplerValue: into(plan.samplerValues); break;
    case PolyVoiceTarget::soundModelValue: into(plan.soundModelValues); break;
    case PolyVoiceTarget::synthValue: into(plan.synthValues); break;
    case PolyVoiceTarget::drumValue: into(plan.drumValues); break;
    case PolyVoiceTarget::samplerController: into(plan.samplerControllers); break;
    case PolyVoiceTarget::filterCutoff: plan.filterCutoffHz = std::clamp(value, 20.0f, 20'000.0f); break;
    case PolyVoiceTarget::filterResonance: plan.filterResonance = std::clamp(value, 0.1f, 20.0f); break;
    case PolyVoiceTarget::oscillatorLevel: plan.oscillatorLevel = std::clamp(value, 0.0f, 1.0f); break;
    case PolyVoiceTarget::wavetablePosition: plan.wavetablePosition = std::clamp(value, 0.0f, 1.0f); break;
    case PolyVoiceTarget::wavetableDetune: plan.wavetableDetuneCents = std::clamp(value, 0.0f, 50.0f); break;
    case PolyVoiceTarget::stringDamping: plan.stringDamping = std::clamp(value, 0.0f, 1.0f); break;
    }
}

// A value a cable moves the voice's own by (depth > 0): the range of each
// target, the frequency on its logarithmic scale.
inline void modulatePolyVoice(PolySynthGraphPlan& plan, const PolyVoiceTarget target, const float control,
                              const float depth, const std::size_t index = 0, const float minimum = 0.0f,
                              const float maximum = 1.0f, const bool logarithmic = false) noexcept
{
    const auto linear = [control, depth](float& field, const float low, const float high) {
        field = std::clamp(field + control * depth * (high - low), low, high);
    };
    // An instrument's own setting, around its knob, on the knob's scale.
    const auto setting = [&](auto& values) {
        if (index >= values.size() || !(maximum > minimum))
            return;
        auto& field = values[index];
        if (logarithmic && minimum > 0.0f && field > 0.0f)
            field = std::clamp(field * std::pow(maximum / minimum, control * depth), minimum, maximum);
        else
            linear(field, minimum, maximum);
    };
    switch (target) {
    case PolyVoiceTarget::filterCutoff:
        plan.filterCutoffHz = std::clamp(plan.filterCutoffHz * std::pow(1'000.0f, control * depth), 20.0f, 20'000.0f);
        break;
    case PolyVoiceTarget::filterResonance: linear(plan.filterResonance, 0.1f, 20.0f); break;
    case PolyVoiceTarget::oscillatorLevel: linear(plan.oscillatorLevel, 0.0f, 1.0f); break;
    case PolyVoiceTarget::wavetablePosition: linear(plan.wavetablePosition, 0.0f, 1.0f); break;
    case PolyVoiceTarget::wavetableDetune: linear(plan.wavetableDetuneCents, 0.0f, 50.0f); break;
    case PolyVoiceTarget::stringDamping: linear(plan.stringDamping, 0.0f, 1.0f); break;
    case PolyVoiceTarget::prismValue: setting(plan.prismValues); break;
    case PolyVoiceTarget::kickValue: setting(plan.kickValues); break;
    case PolyVoiceTarget::samplerValue: setting(plan.samplerValues); break;
    case PolyVoiceTarget::soundModelValue: setting(plan.soundModelValues); break;
    case PolyVoiceTarget::synthValue: setting(plan.synthValues); break;
    case PolyVoiceTarget::drumValue: setting(plan.drumValues); break;
    case PolyVoiceTarget::samplerController: setting(plan.samplerControllers); break;
    }
}

} // namespace cydaw::engine
