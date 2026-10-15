#include "MidiEffectsHost.h"
#include "GraphPolySynthRenderer.h"
#include "NativeDspRuntime.h"
#include "PlanSynthesiser.h"
#include "SamplerMapFile.h"
#include "SoundModelFile.h"

#include <cydaw/realtime/RtPublisher.h>

#include <cydaw/engine/BuiltinProcessors.h>
#include <cydaw/engine/PolyVoice.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace cydaw::app {
namespace {

struct PlanProvider {
    using Publisher =
        cydaw::realtime::RtPublisher<cydaw::engine::PolySynthGraphPlan>;

    Publisher base;
    // Only touched by the audio callback around Synthesiser::renderNextBlock.
    // This permits sample-block host automation without allocating on the
    // real-time thread.
    const cydaw::engine::PolySynthGraphPlan* realtimeOverride {};

    // Every voice borrows the plan once per block. Returning a guard rather
    // than a shared_ptr keeps that off the reference count, which on MSVC also
    // means off the standard library's atomic-shared_ptr spin lock.
    [[nodiscard]] Publisher::ReadGuard borrowPlan() const
    {
        return base.read();
    }
};

class GraphSynthSound final : public juce::SynthesiserSound {
public:
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

// A thin host: JUCE's voice bookkeeping around the engine's PolyVoice,
// which is the same one a MIDI track plays.
class GraphSynthVoice final : public juce::SynthesiserVoice, public PlanVoice {
public:
    GraphSynthVoice(PlanProvider& plan, const cydaw::engine::prism::Context* context) : plan_(plan)
    {
        voice_.setPrismContext(context);
    }

    [[nodiscard]] double voicePitch() const noexcept override { return voice_.currentPitch(); }
    [[nodiscard]] int chokedBy() const noexcept override
    {
        const auto retained = plan_.borrowPlan();
        const auto* current = plan_.realtimeOverride != nullptr ? plan_.realtimeOverride : retained.get();
        return current != nullptr ? voice_.chokedBy(*current) : 0;
    }

    bool canPlaySound(juce::SynthesiserSound* sound) override
    {
        return dynamic_cast<GraphSynthSound*>(sound) != nullptr;
    }

    void startNote(int note, float velocity, juce::SynthesiserSound*, const int pitchWheel) override
    {
        const auto retained = plan_.borrowPlan();
        const auto* current = plan_.realtimeOverride != nullptr
                                  ? plan_.realtimeOverride : retained.get();
        const auto& plan = current != nullptr ? *current : silentPlan();
        const auto started = legato ? voice_.retarget(plan, note, velocity, std::max(1.0, getSampleRate()))
                                    : voice_.startNote(plan, note, velocity, std::max(1.0, getSampleRate()), glideFrom, softAttack);
        legato = false;
        glideFrom = -1.0;
        softAttack = false;
        expressive_ = !isPlayingChannel(1);
        expressionPitch_ = expressive_ ? (static_cast<double>(pitchWheel) - 8'192.0) / 8'192.0 * 24.0 : 0.0;
        expressionGainDb_ = 0.0;
        gainLow_ = 0;
        voice_.setExpression(expressionPitch_, expressionGainDb_);
        if (!started)
            clearCurrentNote();
    }

    void stopNote(float, bool allowTailOff) override
    {
        // Legato: JUCE stops a voice before starting it again; this one goes on.
        if (legato)
            return;
        // Choked, or one note at a time: this one gives way, quickly.
        if ((choke || quickRelease) && allowTailOff) {
            voice_.fadeOut(choke ? 0.006 : 0.03);
            choke = false;
            quickRelease = false;
            if (!voice_.isActive())
                clearCurrentNote();
            return;
        }
        voice_.stopNote(allowTailOff);
        if (!voice_.isActive())
            clearCurrentNote();
    }

    void setCurrentPlaybackSampleRate(const double rate) override
    {
        juce::SynthesiserVoice::setCurrentPlaybackSampleRate(rate);
        // Off the audio thread: the string's delay line is sized here for the
        // lowest note it will ever play, and the wavetable bank is built here
        // rather than under the first note.
        voice_.prepare(std::max(1.0, rate));
    }

    // A note on a channel of its own (2..16) has an expression: its pitch
    // bend and its gain (CC 11 and 43) move it alone.
    void pitchWheelMoved(const int value) override
    {
        if (!expressive_)
            return;
        expressionPitch_ = (static_cast<double>(value) - 8'192.0) / 8'192.0 * 24.0;
        voice_.setExpression(expressionPitch_, expressionGainDb_);
    }
    void controllerMoved(const int controller, const int value) override
    {
        if (!expressive_)
            return;
        if (controller == 43) {
            gainLow_ = value;
        } else if (controller == 11) {
            expressionGainDb_ = static_cast<double>((value << 7) | gainLow_) / 16'383.0 * 60.0 - 48.0;
            voice_.setExpression(expressionPitch_, expressionGainDb_);
        }
    }

    void renderNextBlock(juce::AudioBuffer<float>& output, int start,
                         int count) override
    {
        if (!voice_.isActive())
            return;
        const auto retained = plan_.borrowPlan();
        const auto* current = plan_.realtimeOverride != nullptr
                                  ? plan_.realtimeOverride : retained.get();
        if (!voice_.render(current != nullptr ? *current : silentPlan(),
                           output.getArrayOfWritePointers(),
                           output.getNumChannels(), start, count,
                           std::max(1.0, getSampleRate())))
            clearCurrentNote();
    }

private:
    static const cydaw::engine::PolySynthGraphPlan& silentPlan()
    {
        static const cydaw::engine::PolySynthGraphPlan plan {};
        return plan;
    }

    PlanProvider& plan_;
    cydaw::engine::PolyVoice voice_;
    bool expressive_ {};
    double expressionPitch_ {};
    double expressionGainDb_ {};
    int gainLow_ {};
};

cydaw::engine::PolySynthGraphPlan sanitise(
    cydaw::engine::PolySynthGraphPlan plan)
{
    plan.voiceCount = juce::jlimit(1, 32, plan.voiceCount);
    plan.voiceMode = juce::jlimit(0, 2, plan.voiceMode);
    plan.glideSeconds = juce::jlimit(0.0f, 2.0f, plan.glideSeconds);
    plan.oscillatorLevel = juce::jlimit(
        0.0f, cydaw::engine::playsRecordingLevel(plan.waveform) ? 1.0f : 0.5f, plan.oscillatorLevel);
    plan.attackSeconds = juce::jlimit(0.0001f, 5.0f, plan.attackSeconds);
    plan.decaySeconds = juce::jlimit(0.0001f, 5.0f, plan.decaySeconds);
    plan.sustainLevel = juce::jlimit(0.0f, 1.0f, plan.sustainLevel);
    plan.releaseSeconds = juce::jlimit(0.0001f, 10.0f, plan.releaseSeconds);
    plan.wavetablePosition = juce::jlimit(0.0f, 1.0f, plan.wavetablePosition);
    plan.wavetableDetuneCents = juce::jlimit(0.0f, 50.0f,
                                             plan.wavetableDetuneCents);
    plan.stringDamping = juce::jlimit(0.0f, 1.0f, plan.stringDamping);
    plan.stringDecaySeconds = juce::jlimit(0.05f, 20.0f,
                                           plan.stringDecaySeconds);
    plan.filterCutoffHz = juce::jlimit(20.0f, 20'000.0f,
                                       plan.filterCutoffHz);
    plan.filterResonance = juce::jlimit(0.1f, 20.0f,
                                       plan.filterResonance);
    plan.minimumMidiNote = juce::jlimit(0, 127, plan.minimumMidiNote);
    plan.maximumMidiNote = juce::jlimit(
        plan.minimumMidiNote, 127, plan.maximumMidiNote);
    plan.velocityScale = juce::jlimit(0.0f, 2.0f, plan.velocityScale);
    return plan;
}

} // namespace

struct GraphPolySynthRenderer::Impl {
    Impl()
    {
        plan.base.store(
            std::make_shared<const cydaw::engine::PolySynthGraphPlan>());
        for (auto voice = 0; voice < 32; ++voice)
            synthesiser.addVoice(new GraphSynthVoice(plan, &synthesiser.context));
        synthesiser.addSound(new GraphSynthSound());
        synthesiser.voicing = [this] {
            const auto retained = plan.borrowPlan();
            const auto* current = plan.realtimeOverride != nullptr ? plan.realtimeOverride : retained.get();
            return current != nullptr ? PlanSynthesiser::voicingOf(*current) : PlanSynthesiser::Voicing {};
        };
        synthesiser.noteInfo = [this](const int note, const float velocity) {
            const auto retained = plan.borrowPlan();
            const auto* current = plan.realtimeOverride != nullptr ? plan.realtimeOverride : retained.get();
            return current != nullptr ? PlanSynthesiser::noteInfoOf(*current, note, velocity, synthesiser.context.sampler)
                                      : PlanSynthesiser::NoteInfo {};
        };
    }

    PlanProvider plan;
    PlanSynthesiser synthesiser;
    // The plan's MIDI effects, free-running at the session's tempo.
    MidiEffectsHost midiEffects;
    juce::MidiBuffer notes;

    // The block's notes through the plan's MIDI effects.
    const juce::MidiBuffer& throughEffects(const juce::MidiBuffer& midi, const int startSample, const int numSamples,
                                           const cydaw::engine::PolySynthGraphPlan* current) noexcept
    {
        notes.clear();
        notes.addEvents(midi, 0, -1, 0);
        cydaw::engine::MidiEffectClock clock;
        clock.sampleRate = std::max(1.0, synthesiser.getSampleRate());
        clock.beatsPerMinute = NativeDspRuntime::sessionTempo();
        midiEffects.apply(notes, startSample, numSamples, current != nullptr ? current->midiEffects.get() : nullptr, clock);
        return notes;
    }
};

GraphPolySynthRenderer::GraphPolySynthRenderer()
    : impl_(std::make_unique<Impl>()) {}
GraphPolySynthRenderer::~GraphPolySynthRenderer() = default;

void GraphPolySynthRenderer::prepare(const double sampleRate)
{
    impl_->synthesiser.setCurrentPlaybackSampleRate(std::max(1.0, sampleRate));
    impl_->midiEffects.prepare(std::max(1.0, sampleRate));
    impl_->notes.ensureSize(4'096);
}

void GraphPolySynthRenderer::release()
{
    impl_->synthesiser.allNotesOff(0, false);
}

void GraphPolySynthRenderer::setPlan(cydaw::engine::PolySynthGraphPlan plan)
{
    impl_->plan.base.store(
        std::make_shared<const cydaw::engine::PolySynthGraphPlan>(
            sanitise(plan)));
}

cydaw::engine::PolySynthGraphPlan GraphPolySynthRenderer::plan() const
{
    const auto current = impl_->plan.borrowPlan();
    return current != nullptr ? *current
                              : cydaw::engine::PolySynthGraphPlan {};
}

bool GraphPolySynthRenderer::enabled() const noexcept
{
    const auto current = impl_->plan.borrowPlan();
    return current != nullptr && current->enabled;
}

void GraphPolySynthRenderer::render(juce::AudioBuffer<float>& output,
                                    const juce::MidiBuffer& midi,
                                    const int startSample,
                                    const int numSamples) noexcept
{
    const auto current = impl_->plan.borrowPlan();
    impl_->synthesiser.advance(static_cast<double>(numSamples) / std::max(1.0, impl_->synthesiser.getSampleRate()),
                               NativeDspRuntime::sessionTempo(), current.get());
    impl_->synthesiser.renderNextBlock(output, impl_->throughEffects(midi, startSample, numSamples, current.get()), startSample,
                                       numSamples);
}

void GraphPolySynthRenderer::renderWithPlan(
    juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi,
    const int startSample, const int numSamples,
    const cydaw::engine::PolySynthGraphPlan& plan) noexcept
{
    const auto sanitised = sanitise(plan);
    impl_->plan.realtimeOverride = &sanitised;
    impl_->synthesiser.advance(static_cast<double>(numSamples) / std::max(1.0, impl_->synthesiser.getSampleRate()),
                               NativeDspRuntime::sessionTempo(), &sanitised);
    impl_->synthesiser.renderNextBlock(output, impl_->throughEffects(midi, startSample, numSamples, &sanitised), startSample,
                                       numSamples);
    impl_->plan.realtimeOverride = nullptr;
}

juce::var serialisePolySynthGraphPlan(
    const cydaw::engine::PolySynthGraphPlan& plan)
{
    auto root = juce::DynamicObject::Ptr(new juce::DynamicObject());
    root->setProperty("schema", 1);
    root->setProperty("enabled", plan.enabled);
    root->setProperty("graphNodeCount",
                      static_cast<juce::int64>(plan.graphNodeCount));
    root->setProperty("voiceCount", plan.voiceCount);
    root->setProperty("voiceMode", plan.voiceMode);
    root->setProperty("glideSeconds", plan.glideSeconds);
    root->setProperty("waveform", static_cast<int>(plan.waveform));
    root->setProperty("wavetablePosition", plan.wavetablePosition);
    root->setProperty("wavetableDetune", plan.wavetableDetuneCents);
    root->setProperty("stringDamping", plan.stringDamping);
    root->setProperty("stringDecay", plan.stringDecaySeconds);
    root->setProperty("oscillatorLevel", plan.oscillatorLevel);
    root->setProperty("attackSeconds", plan.attackSeconds);
    root->setProperty("decaySeconds", plan.decaySeconds);
    root->setProperty("sustainLevel", plan.sustainLevel);
    root->setProperty("releaseSeconds", plan.releaseSeconds);
    root->setProperty("filterCutoffHz", plan.filterCutoffHz);
    root->setProperty("filterResonance", plan.filterResonance);
    root->setProperty("transposeSemitones", plan.transposeSemitones);
    root->setProperty("minimumMidiNote", plan.minimumMidiNote);
    root->setProperty("maximumMidiNote", plan.maximumMidiNote);
    root->setProperty("velocityScale", plan.velocityScale);
    // A sound model's settings (its data rides with the graph's JSON).
    juce::Array<juce::var> modelValues;
    for (const auto value : plan.soundModelValues)
        modelValues.add(value);
    root->setProperty("soundModelValues", modelValues);
    // A sampler's settings (its zones ride with the graph's JSON).
    juce::Array<juce::var> samplerValues;
    for (const auto value : plan.samplerValues)
        samplerValues.add(value);
    root->setProperty("samplerValues", samplerValues);
    // Its card's controllers, those it sets.
    {
        auto* controllers = new juce::DynamicObject();
        const juce::var controllersVar(controllers);
        for (std::size_t number = 0; number < plan.samplerControllers.size(); ++number)
            if (plan.samplerControllers[number] >= 0.0f)
                controllers->setProperty(juce::Identifier(juce::String(static_cast<int>(number))), plan.samplerControllers[number]);
        if (!controllers->getProperties().isEmpty())
            root->setProperty("samplerControllers", controllersVar);
    }
    // The MIDI effects: each its kind and its values.
    if (plan.midiEffects != nullptr && plan.midiEffects->count > 0) {
        juce::Array<juce::var> effects;
        for (std::size_t index = 0; index < plan.midiEffects->count; ++index) {
            const auto& effect = plan.midiEffects->effects[index];
            auto* item = new juce::DynamicObject();
            item->setProperty("kind", static_cast<int>(effect.kind));
            juce::Array<juce::var> values;
            for (const auto value : effect.values)
                values.add(value);
            item->setProperty("values", values);
            effects.add(juce::var(item));
        }
        root->setProperty("midiEffects", effects);
    }
    if (plan.drumKit) {
        root->setProperty("drumKit", true);
        juce::Array<juce::var> drumValues;
        for (int index = 0; index < cydaw::engine::drum::valueCount; ++index)
            drumValues.add(plan.drumValues[static_cast<std::size_t>(index)]);
        root->setProperty("drumValues", drumValues);
    }
    // Prisme's settings (its own tables, imported waves, ride with the
    // graph's JSON on its card; attachVoiceAssets puts them back).
    if (plan.waveform == cydaw::engine::PolyOscillatorWaveform::prism) {
        juce::Array<juce::var> prismValues;
        for (const auto value : plan.prismValues)
            prismValues.add(value);
        root->setProperty("prismValues", prismValues);
    }
    if (plan.waveform == cydaw::engine::PolyOscillatorWaveform::kick) {
        juce::Array<juce::var> kickValues;
        for (const auto value : plan.kickValues)
            kickValues.add(value);
        root->setProperty("kickValues", kickValues);
    }
    // A synth of the family: its model and its settings, in its order.
    if (plan.waveform == cydaw::engine::PolyOscillatorWaveform::family) {
        root->setProperty("synthModel", static_cast<int>(plan.synthModel));
        juce::Array<juce::var> synthValues;
        for (int index = 0; index < cydaw::engine::synth::parameterCount(plan.synthModel); ++index)
            synthValues.add(plan.synthValues[static_cast<std::size_t>(index)]);
        root->setProperty("synthValues", synthValues);
    }
    return juce::var(root.get());
}

void attachVoiceAssets(cydaw::engine::PolySynthGraphPlan& plan, const juce::var& graphState)
{
    const auto assets = voiceAssetsFromGraphState(graphState);
    plan.soundModel = assets.soundModel;
    plan.sampler = assets.sampler;
    if (assets.wavetable != nullptr)
        plan.wavetable = assets.wavetable;
    if (plan.waveform == cydaw::engine::PolyOscillatorWaveform::prism && assets.prismTables != nullptr)
        plan.prismTables = cydaw::engine::prism::resolveTables(plan.prismValues, assets.prismTables);
}

std::optional<cydaw::engine::PolySynthGraphPlan>
restorePolySynthGraphPlan(const juce::var& state, juce::String& error)
{
    const auto* root = state.getDynamicObject();
    if (root == nullptr || static_cast<int>(root->getProperty("schema")) != 1) {
        error = "Unsupported polyphonic instrument Graph state";
        return {};
    }
    cydaw::engine::PolySynthGraphPlan plan;
    plan.enabled = static_cast<bool>(root->getProperty("enabled"));
    plan.graphNodeCount = static_cast<std::size_t>(static_cast<juce::int64>(
        root->getProperty("graphNodeCount")));
    plan.voiceCount = static_cast<int>(root->getProperty("voiceCount"));
    plan.voiceMode = static_cast<int>(root->getProperty("voiceMode"));
    plan.glideSeconds = static_cast<float>(static_cast<double>(root->getProperty("glideSeconds")));
    plan.waveform = static_cast<cydaw::engine::PolyOscillatorWaveform>(
        juce::jlimit(0, static_cast<int>(cydaw::engine::PolyOscillatorWaveform::family),
                     static_cast<int>(root->getProperty("waveform"))));
    if (const auto* values = root->getProperty("prismValues").getArray()) {
        for (int index = 0; index < values->size() && index < cydaw::engine::prism::parameterCount; ++index) {
            const auto value = static_cast<float>(static_cast<double>((*values)[index]));
            if (std::isfinite(value))
                plan.prismValues[static_cast<std::size_t>(index)] = cydaw::engine::prism::clampValue(index, value);
        }
    }
    if (plan.waveform == cydaw::engine::PolyOscillatorWaveform::prism)
        plan.prismTables = cydaw::engine::prism::resolveTables(plan.prismValues);
    plan.synthModel = cydaw::engine::synth::modelOf(static_cast<float>(static_cast<int>(root->getProperty("synthModel"))));
    if (plan.synthModel != cydaw::engine::synth::Model::none) {
        cydaw::engine::synth::warmUp(plan.synthModel);
        plan.synthValues = cydaw::engine::synth::defaultValues(plan.synthModel);
        if (const auto* values = root->getProperty("synthValues").getArray())
            for (int index = 0; index < values->size() && index < cydaw::engine::synth::parameterCount(plan.synthModel); ++index) {
                const auto value = static_cast<float>(static_cast<double>((*values)[index]));
                if (std::isfinite(value))
                    plan.synthValues[static_cast<std::size_t>(index)] = cydaw::engine::synth::clampValue(plan.synthModel, index, value);
            }
    }
    if (const auto* values = root->getProperty("kickValues").getArray()) {
        for (int index = 0; index < values->size() && index < cydaw::engine::kick::parameterCount; ++index) {
            const auto value = static_cast<float>(static_cast<double>((*values)[index]));
            if (std::isfinite(value))
                plan.kickValues[static_cast<std::size_t>(index)] = cydaw::engine::kick::clampValue(index, value);
        }
    }
    if (const auto* effects = root->getProperty("midiEffects").getArray(); effects != nullptr && !effects->isEmpty()) {
        auto read = std::make_shared<cydaw::engine::MidiEffectsPlan>();
        for (const auto& item : *effects) {
            const auto kind = static_cast<int>(item.getProperty("kind", 0));
            if (read->count >= cydaw::engine::midiEffectMost || kind < 0
                || kind > static_cast<int>(cydaw::engine::lastNativeDspNodeKind))
                continue;
            auto& effect = read->effects[read->count];
            effect.kind = static_cast<cydaw::engine::NativeDspNodeKind>(kind);
            const auto* descriptor = cydaw::engine::effectForKind(effect.kind);
            if (descriptor == nullptr || !descriptor->midi)
                continue;
            if (const auto* values = item.getProperty("values", {}).getArray())
                for (int index = 0; index < values->size() && index < static_cast<int>(effect.values.size()); ++index)
                    effect.values[static_cast<std::size_t>(index)] = static_cast<float>(static_cast<double>((*values)[index]));
            ++read->count;
        }
        if (read->count > 0)
            plan.midiEffects = std::move(read);
    }
    plan.drumKit = static_cast<bool>(root->getProperty("drumKit"));
    if (plan.drumKit) {
        plan.drumValues = cydaw::engine::drum::defaultValues();
        if (const auto* values = root->getProperty("drumValues").getArray())
            for (int index = 0; index < values->size() && index < cydaw::engine::drum::valueCount; ++index) {
                const auto value = static_cast<float>(static_cast<double>((*values)[index]));
                plan.drumValues[static_cast<std::size_t>(index)] = cydaw::engine::drum::clampValue(index, value);
            }
    }
    if (const auto* values = root->getProperty("samplerValues").getArray())
        for (int index = 0; index < values->size() && index < static_cast<int>(plan.samplerValues.size()); ++index) {
            auto value = static_cast<float>(static_cast<double>((*values)[index]));
            if (!std::isfinite(value))
                continue;
            if (index < static_cast<int>(cydaw::engine::samplerSettingCount)) {
                const auto& setting = cydaw::engine::samplerSettings[static_cast<std::size_t>(index)];
                value = juce::jlimit(setting.minimum, setting.maximum, value);
            }
            plan.samplerValues[static_cast<std::size_t>(index)] = value;
        }
    if (const auto* controllers = root->getProperty("samplerControllers").getDynamicObject())
        for (const auto& property : controllers->getProperties()) {
            const auto number = property.name.toString().getIntValue();
            const auto value = static_cast<float>(static_cast<double>(property.value));
            if (number >= 0 && number < 128 && std::isfinite(value))
                plan.samplerControllers[static_cast<std::size_t>(number)] = juce::jlimit(0.0f, 127.0f, value);
        }
    if (const auto* values = root->getProperty("soundModelValues").getArray())
        for (int index = 0; index < values->size() && index < static_cast<int>(plan.soundModelValues.size()); ++index) {
            auto value = static_cast<float>(static_cast<double>((*values)[index]));
            if (!std::isfinite(value))
                continue;
            if (index < static_cast<int>(cydaw::engine::soundModelSettingCount)) {
                const auto& setting = cydaw::engine::soundModelSettings[static_cast<std::size_t>(index)];
                value = juce::jlimit(setting.minimum, setting.maximum, value);
            }
            plan.soundModelValues[static_cast<std::size_t>(index)] = value;
        }
    if (root->hasProperty(juce::Identifier("wavetablePosition")))
        plan.wavetablePosition = static_cast<float>(
            static_cast<double>(root->getProperty("wavetablePosition")));
    if (root->hasProperty(juce::Identifier("wavetableDetune")))
        plan.wavetableDetuneCents = static_cast<float>(
            static_cast<double>(root->getProperty("wavetableDetune")));
    if (root->hasProperty(juce::Identifier("stringDamping")))
        plan.stringDamping = static_cast<float>(
            static_cast<double>(root->getProperty("stringDamping")));
    if (root->hasProperty(juce::Identifier("stringDecay")))
        plan.stringDecaySeconds = static_cast<float>(
            static_cast<double>(root->getProperty("stringDecay")));
#define CYDAW_READ_PLAN(name) plan.name = static_cast<float>(static_cast<double>(root->getProperty(#name)))
    CYDAW_READ_PLAN(oscillatorLevel);
    CYDAW_READ_PLAN(attackSeconds);
    CYDAW_READ_PLAN(decaySeconds);
    CYDAW_READ_PLAN(sustainLevel);
    CYDAW_READ_PLAN(releaseSeconds);
    CYDAW_READ_PLAN(filterCutoffHz);
    if (root->hasProperty("filterResonance"))
        CYDAW_READ_PLAN(filterResonance);
    CYDAW_READ_PLAN(velocityScale);
#undef CYDAW_READ_PLAN
    plan.transposeSemitones = static_cast<int>(
        root->getProperty("transposeSemitones"));
    plan.minimumMidiNote = static_cast<int>(
        root->getProperty("minimumMidiNote"));
    plan.maximumMidiNote = static_cast<int>(
        root->getProperty("maximumMidiNote"));
    error.clear();
    // The samples its card's controllers reach, asked for now.
    if (plan.sampler != nullptr)
        SampleStore::instance().want(*plan.sampler,
                                     { &plan.samplerControllers, nullptr,
                                       cydaw::engine::samplerValue(plan.samplerValues, cydaw::engine::SamplerSetting::articulation) });
    return sanitise(plan);
}

} // namespace cydaw::app
