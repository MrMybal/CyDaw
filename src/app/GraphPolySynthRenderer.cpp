#include <cydaw/base/File.h>
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

class GraphSynthSound final : public cydaw::audio::VoiceSound {
public:
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

// A thin host: native voice allocation around the engine's PolyVoice,
// which is the same one a MIDI track plays.
class GraphSynthVoice final : public cydaw::audio::AllocatedVoice, public PlanVoice {
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

    bool canPlaySound(cydaw::audio::VoiceSound* sound) override
    {
        return dynamic_cast<GraphSynthSound*>(sound) != nullptr;
    }

    void startNote(int note, float velocity, cydaw::audio::VoiceSound*, const int pitchWheel) override
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
        // Legato preserves the sounding voice across a new start.
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
        cydaw::audio::AllocatedVoice::setCurrentPlaybackSampleRate(rate);
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

    void renderNextBlock(cydaw::audio::SampleBuffer<float>& output, int start,
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
    cydaw::midi::EventBuffer notes;

    // The block's notes through the plan's MIDI effects.
    const cydaw::midi::EventBuffer& throughEffects(const cydaw::midi::EventBuffer& midi, const int startSample, const int numSamples,
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
            sanitisePolySynthGraphPlan(plan)));
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

void GraphPolySynthRenderer::render(cydaw::audio::SampleBuffer<float>& output,
                                    const cydaw::midi::EventBuffer& midi,
                                    const int startSample,
                                    const int numSamples) noexcept
{
    const auto current = impl_->plan.borrowPlan();
    impl_->synthesiser.advance(static_cast<double>(numSamples) / std::max(1.0, impl_->synthesiser.getSampleRate()),
                               NativeDspRuntime::sessionTempo(), current.get());
    impl_->synthesiser.renderNextBlock(output, impl_->throughEffects(midi, startSample, numSamples, current.get()), startSample,
                                       numSamples);
}

void GraphPolySynthRenderer::renderChanged(cydaw::audio::SampleBuffer<float>& output, const cydaw::midi::EventBuffer& midi,
                                           const int startSample, const int numSamples, const PlanChange change,
                                           const void* context) noexcept
{
    // Declared first, so released last: the copies below go while it holds.
    const auto current = impl_->plan.borrowPlan();
    if (current == nullptr)
        return;
    auto plan = *current;
    if (change != nullptr)
        change(plan, context);
    const auto sanitised = sanitisePolySynthGraphPlan(plan);
    impl_->plan.realtimeOverride = &sanitised;
    impl_->synthesiser.advance(static_cast<double>(numSamples) / std::max(1.0, impl_->synthesiser.getSampleRate()),
                               NativeDspRuntime::sessionTempo(), &sanitised);
    impl_->synthesiser.renderNextBlock(output, impl_->throughEffects(midi, startSample, numSamples, &sanitised), startSample,
                                       numSamples);
    impl_->plan.realtimeOverride = nullptr;
}

void GraphPolySynthRenderer::renderWithPlan(
    cydaw::audio::SampleBuffer<float>& output, const cydaw::midi::EventBuffer& midi,
    const int startSample, const int numSamples,
    const cydaw::engine::PolySynthGraphPlan& plan) noexcept
{
    const auto sanitised = sanitisePolySynthGraphPlan(plan);
    impl_->plan.realtimeOverride = &sanitised;
    impl_->synthesiser.advance(static_cast<double>(numSamples) / std::max(1.0, impl_->synthesiser.getSampleRate()),
                               NativeDspRuntime::sessionTempo(), &sanitised);
    impl_->synthesiser.renderNextBlock(output, impl_->throughEffects(midi, startSample, numSamples, &sanitised), startSample,
                                       numSamples);
    impl_->plan.realtimeOverride = nullptr;
}

} // namespace cydaw::app
