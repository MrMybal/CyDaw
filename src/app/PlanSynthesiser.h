#pragma once

#include <cydaw/engine/PolySynthGraph.h>

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <array>
#include <functional>

// JUCE's synthesiser with what an instrument's voicing asks of it: Prisme's
// voice limit, mono and legato with glide, and the controllers it reads
// (pitch bend, mod wheel, aftertouch, tempo, a clock for its free LFOs).
// The DAW's MIDI tracks and the exported plug-in both use it, so a patch
// plays the same in each.

namespace cydaw::app {

// What a voice offers the synthesiser beyond JUCE's: the pitch it is at, and
// how its next start goes (sliding from a pitch, or legato: no new start).
class PlanVoice {
public:
    virtual ~PlanVoice() = default;
    double glideFrom { -1.0 };
    bool legato {};
    // A line played one note at a time: this voice gives way quickly; the
    // next one starts without an attack of its own (legato).
    bool quickRelease {};
    bool softAttack {};
    // Cut by a note of its choke group: it goes in a few milliseconds.
    bool choke {};
    [[nodiscard]] virtual double voicePitch() const noexcept = 0;
    // The choke group whose notes cut it (a sampler's open hat), 0: none.
    [[nodiscard]] virtual int chokedBy() const noexcept { return 0; }
};

class PlanSynthesiser final : public juce::Synthesiser {
public:
    struct Voicing {
        bool prism {};
        int mode {};   // prism::VoiceMode
        int voices { 32 };
        double glide {};
        bool glideAlways {};
    };
    // The voicing of the plan the voices play now. Called on the audio
    // thread: it must not allocate.
    std::function<Voicing()> voicing;
    // Shared by every voice (PolyVoice::setPrismContext).
    engine::prism::Context context;
    // What a note-on is to the plan beyond a note: the choke group it would
    // sound in (0: none; the voices it cuts are those whose chokedBy() is
    // that group), or a sampler's keyswitch (it plays nothing, it chooses
    // the articulation of the next notes).
    struct NoteInfo {
        int chokeGroup {};
        int keyswitch { -1 }; // the key in the map's range (transposed)
    };
    // Called on the audio thread.
    std::function<NoteInfo(int note, float velocity)> noteInfo;

    // Once a block, before rendering it, with the plan the voices play: a
    // sampler's controllers follow its card.
    void advance(const double seconds, const double tempo, const engine::PolySynthGraphPlan* plan = nullptr) noexcept
    {
        context.clockSeconds += seconds;
        if (tempo > 0.0)
            context.tempo = tempo;
        if (plan != nullptr && plan->waveform == engine::PolyOscillatorWaveform::sampler)
            context.sampler.follow(plan->sampler.get(), plan->samplerControllers,
                                   engine::samplerValue(plan->samplerValues, engine::SamplerSetting::articulation));
    }

    // Read off a plan: what PlanSynthesiser needs of its voicing.
    [[nodiscard]] static Voicing voicingOf(const engine::PolySynthGraphPlan& plan) noexcept
    {
        Voicing result;
        result.prism = plan.waveform == engine::PolyOscillatorWaveform::prism;
        // A synth of the family plays its voices as Prisme does, from its own
        // first settings.
        if (plan.waveform == engine::PolyOscillatorWaveform::family && plan.synthModel != engine::synth::Model::none) {
            namespace s = engine::synth;
            const auto value = [&plan](const int index) { return plan.synthValues[static_cast<std::size_t>(index)]; };
            result.prism = true;
            result.mode = std::clamp(static_cast<int>(std::lround(value(s::voicingMode))), 0, 2);
            result.voices = std::clamp(static_cast<int>(std::lround(value(s::voicingCount))), 1, 32);
            result.glide = std::max(0.0f, value(s::voicingGlide));
            result.glideAlways = value(s::voicingGlideAlways) >= 0.5f;
            return result;
        }
        if (!result.prism) {
            // The voice allocator's: poly, mono or legato, and its glide.
            result.mode = std::clamp(plan.voiceMode, 0, 2);
            result.glide = std::max(0.0f, plan.glideSeconds);
            return result;
        }
        namespace p = engine::prism;
        const auto value = [&plan](const int index) { return plan.prismValues[static_cast<std::size_t>(index)]; };
        result.mode = std::clamp(static_cast<int>(std::lround(value(p::voiceBase + p::voiceMode))), 0, 2);
        result.voices = std::clamp(static_cast<int>(std::lround(value(p::voiceBase + p::voiceCount))), 1, 32);
        result.glide = std::max(0.0f, value(p::voiceBase + p::voiceGlide));
        result.glideAlways = value(p::voiceBase + p::voiceGlideAlways) >= 0.5f;
        return result;
    }

    // What a note is to a plan: a sampler's zones say its choke group, its
    // map whether the key is a keyswitch.
    [[nodiscard]] static NoteInfo noteInfoOf(const engine::PolySynthGraphPlan& plan, const int note, const float velocity,
                                             const engine::SamplerPlaying& playing) noexcept
    {
        // Silex: a closed or pedal hat cuts the open one.
        if (plan.waveform == engine::PolyOscillatorWaveform::family && plan.synthModel == engine::synth::Model::silex) {
            const auto transposed = std::clamp(note + plan.transposeSemitones, 0, 127);
            return { transposed != engine::synth::silexOpenHat ? engine::synth::silexChokeGroupOf(transposed) : 0, -1 };
        }
        if (plan.waveform != engine::PolyOscillatorWaveform::sampler || plan.sampler == nullptr)
            return {};
        const auto transposed = std::clamp(note + plan.transposeSemitones, 0, 127);
        if (engine::samplerKeyswitch(*plan.sampler, transposed))
            return { 0, transposed };
        const auto crossfade = engine::samplerValue(plan.samplerValues, engine::SamplerSetting::velocityCrossfade) >= 0.5f;
        const engine::SamplerCues cues { &plan.samplerControllers, &playing,
                                         engine::samplerValue(plan.samplerValues, engine::SamplerSetting::articulation) };
        return { engine::samplerGroupOf(*plan.sampler, transposed,
                                        std::clamp(static_cast<double>(velocity) * plan.velocityScale, 0.0, 1.0), crossfade,
                                        cues),
                 -1 };
    }

    void noteOn(const int channel, const int note, const float velocity) override
    {
        const auto info = noteInfo ? noteInfo(note, velocity) : NoteInfo {};
        // A keyswitch: the next notes play its articulation.
        if (info.keyswitch >= 0) {
            context.sampler.keyswitch = info.keyswitch;
            return;
        }
        // The keys held as it starts (a sampler's first and legato zones).
        context.sampler.heldKeys = downCount_;
        if (note >= 0 && note < 128 && !down_[static_cast<std::size_t>(note)]) {
            down_[static_cast<std::size_t>(note)] = true;
            ++downCount_;
        }
        // A closed hat cuts the open one: the voices its group chokes go first.
        if (const auto group = info.chokeGroup; group != 0) {
            const juce::ScopedLock guard(lock);
            for (auto* voice : voices)
                if (voice->isVoiceActive())
                    if (auto* planVoice = dynamic_cast<PlanVoice*>(voice); planVoice != nullptr && planVoice->chokedBy() == group) {
                        planVoice->choke = true;
                        stopVoice(voice, 0.0f, true);
                    }
        }
        const auto v = voicing ? voicing() : Voicing {};
        if (!v.prism && v.mode == 0) {
            lastPitch_ = note;
            juce::Synthesiser::noteOn(channel, note, velocity);
            return;
        }
        const juce::ScopedLock guard(lock);
        auto* sound = sounds.isEmpty() ? nullptr : sounds.getUnchecked(0).get();
        if (sound == nullptr)
            return;
        const auto overlapping = heldCount_ > 0;
        remember(note, velocity);
        if (note >= 0 && note < 128)
            channels_[static_cast<std::size_t>(note)] = channel;
        if (!v.prism) {
            startLine(sound, channel, note, velocity, v, overlapping);
            return;
        }
        if (v.mode == static_cast<int>(engine::prism::VoiceMode::poly)) {
            // The voice limit: the oldest goes when it is reached.
            auto active = 0;
            for (auto* voice : voices)
                active += voice->isVoiceActive() ? 1 : 0;
            if (active >= v.voices)
                if (auto* stolen = oldestSounding())
                    stopVoice(stolen, 0.0f, false);
            if (auto* voice = findFreeVoice(sound, channel, note, isNoteStealingEnabled())) {
                if (auto* planVoice = dynamic_cast<PlanVoice*>(voice)) {
                    planVoice->glideFrom = v.glide > 0.0 && v.glideAlways && lastPitch_ >= 0.0 ? lastPitch_ : -1.0;
                    planVoice->legato = false;
                }
                startVoice(voice, sound, channel, note, velocity);
            }
            lastPitch_ = note;
            return;
        }
        // Mono and legato: one voice, the one sounding if there is one.
        juce::SynthesiserVoice* voice = nullptr;
        for (auto* candidate : voices)
            if (candidate->isVoiceActive()) {
                voice = candidate;
                break;
            }
        if (voice == nullptr && !voices.isEmpty())
            voice = voices.getUnchecked(0);
        if (voice == nullptr)
            return;
        // Leftover tails go, so one voice plays.
        for (auto* other : voices)
            if (other != voice && other->isVoiceActive())
                stopVoice(other, 0.0f, false);
        startMono(voice, sound, channel, note, velocity, v, overlapping);
    }

    void noteOff(const int channel, const int note, const float velocity, const bool allowTailOff) override
    {
        if (note >= 0 && note < 128 && down_[static_cast<std::size_t>(note)]) {
            down_[static_cast<std::size_t>(note)] = false;
            --downCount_;
        }
        const auto v = voicing ? voicing() : Voicing {};
        if (v.mode == static_cast<int>(engine::prism::VoiceMode::poly)) {
            forget(note);
            juce::Synthesiser::noteOff(channel, note, velocity, allowTailOff);
            return;
        }
        if (!v.prism) {
            const juce::ScopedLock guard(lock);
            forget(note);
            // The voice still holding it (one that gave way is done with it).
            auto playing = false;
            for (auto* candidate : voices)
                playing = playing
                          || (candidate->isVoiceActive() && candidate->isKeyDown()
                              && candidate->getCurrentlyPlayingNote() == note && candidate->isPlayingChannel(channel));
            if (!playing)
                return;
            // Keys still held: back to the last one pressed, sliding to it.
            if (heldCount_ > 0) {
                auto* sound = sounds.isEmpty() ? nullptr : sounds.getUnchecked(0).get();
                const auto back = held_[static_cast<std::size_t>(heldCount_ - 1)];
                startLine(sound, channels_[static_cast<std::size_t>(back)], back, velocities_[static_cast<std::size_t>(back)], v,
                          true);
                return;
            }
            juce::Synthesiser::noteOff(channel, note, velocity, allowTailOff);
            return;
        }
        const juce::ScopedLock guard(lock);
        forget(note);
        juce::SynthesiserVoice* voice = nullptr;
        for (auto* candidate : voices)
            if (candidate->isVoiceActive() && candidate->getCurrentlyPlayingNote() == note) {
                voice = candidate;
                break;
            }
        if (voice == nullptr)
            return;
        // Keys still held: back to the last one pressed.
        if (heldCount_ > 0) {
            auto* sound = sounds.isEmpty() ? nullptr : sounds.getUnchecked(0).get();
            const auto back = held_[static_cast<std::size_t>(heldCount_ - 1)];
            startMono(voice, sound, channel, back, velocities_[static_cast<std::size_t>(back)], v, true);
            return;
        }
        juce::Synthesiser::noteOff(channel, note, velocity, allowTailOff);
    }

    void allNotesOff(const int channel, const bool allowTailOff) override
    {
        heldCount_ = 0;
        down_.fill(false);
        downCount_ = 0;
        juce::Synthesiser::allNotesOff(channel, allowTailOff);
    }

    void handlePitchWheel(const int channel, const int wheelValue) override
    {
        if (channel == 1)
            context.pitchBend = std::clamp(static_cast<float>(wheelValue - 8'192) / 8'192.0f, -1.0f, 1.0f);
        juce::Synthesiser::handlePitchWheel(channel, wheelValue);
    }

    void handleController(const int channel, const int controller, const int value) override
    {
        if (controller == 1)
            context.modWheel = static_cast<float>(value) / 127.0f;
        // A sampler's SFZ reads them all, but a note's own expression (11 and
        // 43 on channels 2..16).
        if (channel <= 1 || (controller != 11 && controller != 43))
            context.sampler.controllerMoved(controller, value);
        juce::Synthesiser::handleController(channel, controller, value);
    }

    void handleChannelPressure(const int channel, const int value) override
    {
        context.aftertouch = static_cast<float>(value) / 127.0f;
        juce::Synthesiser::handleChannelPressure(channel, value);
    }

private:
    // The voice to give up for a new one: the oldest of those whose key is
    // up, else the oldest. (JUCE's own choice assumes every voice is busy.)
    [[nodiscard]] juce::SynthesiserVoice* oldestSounding() const noexcept
    {
        juce::SynthesiserVoice* oldest = nullptr;
        juce::SynthesiserVoice* oldestReleased = nullptr;
        for (auto* voice : voices) {
            if (!voice->isVoiceActive())
                continue;
            auto*& pick = voice->isPlayingButReleased() ? oldestReleased : oldest;
            if (pick == nullptr || voice->wasStartedBefore(*pick))
                pick = voice;
        }
        return oldestReleased != nullptr ? oldestReleased : oldest;
    }

    void startMono(juce::SynthesiserVoice* voice, juce::SynthesiserSound* sound, const int channel, const int note,
                   const float velocity, const Voicing& v, const bool overlapping)
    {
        if (sound == nullptr)
            return;
        const auto sounding = voice->isVoiceActive();
        if (auto* planVoice = dynamic_cast<PlanVoice*>(voice)) {
            const auto from = sounding ? planVoice->voicePitch() : lastPitch_;
            const auto slide = v.glide > 0.0 && (v.glideAlways || overlapping);
            planVoice->glideFrom = slide && from >= 0.0 ? from : -1.0;
            planVoice->legato = v.mode == static_cast<int>(engine::prism::VoiceMode::legato) && sounding && overlapping;
        }
        startVoice(voice, sound, channel, note, velocity);
        lastPitch_ = note;
    }

    // A line one note at a time, for every instrument but Prisme (whose
    // voice slides itself): what sounds gives way in a few milliseconds, the
    // new note starts on a voice of its own, sliding from where the line was
    // (mono: always; legato: when played over the last note, and then
    // without an attack of its own).
    void startLine(juce::SynthesiserSound* sound, const int channel, const int note, const float velocity, const Voicing& v,
                   const bool overlapping)
    {
        if (sound == nullptr)
            return;
        auto from = lastPitch_;
        juce::SynthesiserVoice* lead = nullptr;
        for (auto* voice : voices)
            if (voice->isVoiceActive() && voice->isKeyDown() && (lead == nullptr || lead->wasStartedBefore(*voice)))
                lead = voice;
        if (auto* planVoice = dynamic_cast<PlanVoice*>(lead))
            from = planVoice->voicePitch();
        for (auto* voice : voices)
            if (voice->isVoiceActive()) {
                if (auto* planVoice = dynamic_cast<PlanVoice*>(voice))
                    planVoice->quickRelease = true;
                voice->setKeyDown(false);
                stopVoice(voice, 0.0f, true);
            }
        auto* voice = findFreeVoice(sound, channel, note, isNoteStealingEnabled());
        if (voice == nullptr)
            return;
        const auto legatoNote = v.mode == static_cast<int>(engine::prism::VoiceMode::legato) && overlapping;
        if (auto* planVoice = dynamic_cast<PlanVoice*>(voice)) {
            const auto slide = v.glide > 0.0 && from >= 0.0
                               && (v.mode == static_cast<int>(engine::prism::VoiceMode::mono) || legatoNote);
            planVoice->glideFrom = slide ? from : -1.0;
            planVoice->softAttack = legatoNote;
            planVoice->legato = false;
            planVoice->quickRelease = false;
        }
        startVoice(voice, sound, channel, note, velocity);
        lastPitch_ = note;
    }

    void remember(const int note, const float velocity) noexcept
    {
        forget(note);
        if (note < 0 || note > 127)
            return;
        velocities_[static_cast<std::size_t>(note)] = velocity;
        if (heldCount_ < static_cast<int>(held_.size()))
            held_[static_cast<std::size_t>(heldCount_++)] = note;
    }

    void forget(const int note) noexcept
    {
        for (int i = 0; i < heldCount_; ++i)
            if (held_[static_cast<std::size_t>(i)] == note) {
                for (int j = i; j + 1 < heldCount_; ++j)
                    held_[static_cast<std::size_t>(j)] = held_[static_cast<std::size_t>(j + 1)];
                --heldCount_;
                return;
            }
    }

    std::array<int, 128> held_ {};
    // Every key down now, whatever the voicing.
    std::array<bool, 128> down_ {};
    int downCount_ {};
    std::array<float, 128> velocities_ {};
    std::array<int, 128> channels_ {};
    int heldCount_ {};
    double lastPitch_ { -1.0 };
};

} // namespace cydaw::app
