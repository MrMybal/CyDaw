#pragma once

#include "PolySynthGraph.h"
#include "PolyVoiceFilter.h"
#include "PolyVoiceOscillator.h"

#include <algorithm>
#include <cmath>

namespace cydaw::engine {

// One voice of the polyphonic instrument, from a note-on to silence: the
// oscillator, the envelope that shapes it and the filter it runs through.
// The track's synth and the graph editor's preview each carried a copy of
// this, and the copies drifted — the track's filter lost its resonance —
// so there is one, and the two renderers are thin hosts around it. It
// knows nothing of JUCE: a host hands it the plan, the note and the
// channels to add into.
class PolyVoice final {
public:
    // Off the audio thread, before the first note: what the oscillator
    // sizes for this rate.
    void prepare(const double sampleRate)
    {
        source_.prepare(std::max(1.0, sampleRate));
        prism_.prepare(std::max(1.0, sampleRate));
        family_.prepare(std::max(1.0, sampleRate));
    }

    // A note's expression, as the host's events for it say: its pitch moved
    // (semitones) and its gain (dB); 0 and 0 at its start.
    void setExpression(const double pitchSemitones, const double gainDb) noexcept
    {
        expressionPitch_ = std::clamp(pitchSemitones, -48.0, 48.0);
        expressionGain_ = std::pow(10.0, std::clamp(gainDb, -96.0, 24.0) / 20.0);
    }

    // What a Prisme voice reads of its host: tempo, clock, controllers. The
    // host keeps it alive as long as the voice.
    void setPrismContext(const prism::Context* context) noexcept
    {
        prismContext_ = context;
        source_.setSamplerPlaying(context != nullptr ? &context->sampler : nullptr);
    }

    // A note-on. False when the plan refuses it — disabled, or the note
    // outside the plan's range — and the voice is left silent.
    // glideFrom: the pitch it slides from (below 0: none); softAttack: a
    // legato note, no new attack of its own.
    bool startNote(const PolySynthGraphPlan& plan, const int midiNote,
                   const float velocity, const double sampleRate, const double glideFrom = -1.0,
                   const bool softAttack = false) noexcept
    {
        if (!plan.enabled || midiNote < plan.minimumMidiNote
            || midiNote > plan.maximumMidiNote || plan.waveform == PolyOscillatorWaveform::rack) {
            clear();
            return false;
        }
        expressionPitch_ = 0.0;
        expressionGain_ = 1.0;
        currentGain_ = 1.0;
        kickPlaying_ = false;
        familyPlaying_ = false;
        if (plan.waveform == PolyOscillatorWaveform::family && plan.synthModel != synth::Model::none) {
            // A synth of the family plays the whole voice, as Prisme does.
            source_.reset();
            stage_ = Stage::idle;
            prismPlaying_ = false;
            familyPlaying_ = true;
            const auto note = std::clamp(midiNote + plan.transposeSemitones, 0, 127);
            familyModel_ = plan.synthModel;
            familyNote_ = note;
            family_.start(plan.synthModel, plan.synthValues, note,
                          static_cast<float>(std::clamp(static_cast<double>(velocity) * plan.velocityScale, 0.0, 1.0)),
                          glideFrom >= 0.0 ? glideFrom + plan.transposeSemitones : -1.0, sampleRate);
            return true;
        }
        if (plan.waveform == PolyOscillatorWaveform::kick) {
            // Kick plays the whole hit: its envelope; the key only starts it.
            source_.reset();
            stage_ = Stage::idle;
            prismPlaying_ = false;
            kickPlaying_ = true;
            kickRate_ = std::max(1.0, sampleRate);
            kickNote_ = std::clamp(midiNote + plan.transposeSemitones, 0, 127);
            kickFade_ = 1.0;
            kickFadeStep_ = 0.0;
            kick_.start(plan.kickValues, kickNote_,
                        static_cast<float>(std::clamp(static_cast<double>(velocity) * plan.velocityScale, 0.0, 1.0)),
                        kickRate_);
            return true;
        }
        if (plan.waveform == PolyOscillatorWaveform::prism) {
            // Prisme plays the whole voice: its envelopes, its filter.
            source_.reset();
            stage_ = Stage::idle;
            prismPlaying_ = true;
            const auto note = std::clamp(midiNote + plan.transposeSemitones, 0, 127);
            prism_.start(plan.prismValues, note,
                         static_cast<float>(std::clamp(static_cast<double>(velocity) * plan.velocityScale, 0.0, 1.0)),
                         glideFrom >= 0.0 ? glideFrom + plan.transposeSemitones : -1.0, sampleRate);
            return true;
        }
        prismPlaying_ = false;
        const auto rate = std::max(1.0, sampleRate);
        const auto note = std::clamp(midiNote + plan.transposeSemitones, 0, 127);
        noteFrequency_ = 440.0 * std::pow(2.0, static_cast<double>(note - 69) / 12.0);
        // A slide from the last note: its distance, run off over the glide.
        notePitch_ = static_cast<double>(midiNote);
        glideOffset_ = glideFrom >= 0.0 && plan.glideSeconds > 0.0f ? std::clamp(glideFrom - notePitch_, -48.0, 48.0) : 0.0;
        glideStep_ = glideOffset_ / std::max(1.0, static_cast<double>(plan.glideSeconds) * rate);
        softAttack_ = softAttack;
        fadeSeconds_ = 0.0;
        velocity_ = std::clamp(
            static_cast<double>(velocity) * static_cast<double>(plan.velocityScale),
            0.0, 1.0);
        // A Batterie: the note's piece, silent if muted (or another soloed);
        // the velocity wandering as a drummer's hand does.
        drumming_ = plan.waveform == PolyOscillatorWaveform::sampler && plan.drumKit;
        drumLevel_ = 1.0;
        if (drumming_) {
            drumPiece_ = drum::pieceForNote(midiNote);
            if (drum::shapingFor(plan.drumValues, drumPiece_).gain <= 0.0) {
                clear();
                return false;
            }
            const auto humanise = static_cast<double>(plan.drumValues[static_cast<std::size_t>(drum::globalBase + drum::globalHumanise)]);
            if (humanise > 0.0) {
                drumSeed_ = drumSeed_ * 1'664'525u + 1'013'904'223u;
                const auto wander = static_cast<double>(drumSeed_ >> 8) / static_cast<double>(1u << 24) * 2.0 - 1.0;
                velocity_ = std::clamp(velocity_ * (1.0 + 0.3 * humanise * wander), 0.05, 1.0);
            }
        }
        source_.startNote(plan, noteFrequency_, rate, static_cast<double>(note), velocity_);
        // A sound model and a sampler carry their own velocity response:
        // their level is the recording's, not scaled again here.
        modelled_ = plan.waveform == PolyOscillatorWaveform::soundModel
                    || plan.waveform == PolyOscillatorWaveform::sampler;
        stereo_ = PolyVoiceOscillator::stereo(plan);
        // A one-shot sample plays to its end whatever the key does.
        oneShot_ = plan.waveform == PolyOscillatorWaveform::sampler
                   && samplerValue(plan.samplerValues, SamplerSetting::oneShot) >= 0.5f;
        envelope_ = 0.0;
        levelHeard_ = static_cast<double>(plan.oscillatorLevel);
        filter_.reset();
        filterRight_.reset();
        stage_ = Stage::attack;
        return true;
    }

    // Legato: the voice slides to another note without starting again
    // (Prisme only; the others start it again).
    bool retarget(const PolySynthGraphPlan& plan, const int midiNote, const float velocity, const double sampleRate) noexcept
    {
        if (prismPlaying_ && prism_.active()) {
            prism_.retarget(plan.prismValues, std::clamp(midiNote + plan.transposeSemitones, 0, 127), velocity);
            return true;
        }
        if (familyPlaying_ && family_.active()) {
            family_.retarget(plan.synthValues, std::clamp(midiNote + plan.transposeSemitones, 0, 127), velocity);
            return true;
        }
        return startNote(plan, midiNote, velocity, sampleRate);
    }

    // The choke group whose notes cut this voice (a sampler's open hat), 0: none.
    [[nodiscard]] int chokedBy(const PolySynthGraphPlan& plan) const noexcept
    {
        // Silex's open hat: cut by a closed or a pedal hat.
        if (familyPlaying_ && familyModel_ == synth::Model::silex && isActive())
            return synth::silexChokeGroupOf(familyNote_) == 1 && familyNote_ == synth::silexOpenHat ? 1 : 0;
        if (prismPlaying_ || kickPlaying_ || familyPlaying_ || !isActive())
            return 0;
        return source_.samplerOffBy(plan);
    }

    // Where a voice's pitch is now, in semitones (a glide in progress); -1 silent.
    [[nodiscard]] double currentPitch() const noexcept
    {
        if (prismPlaying_ && prism_.active())
            return prism_.currentPitch();
        if (familyPlaying_ && family_.active())
            return family_.currentPitch();
        if (kickPlaying_)
            return static_cast<double>(kickNote_);
        return noteFrequency_ > 0.0 ? notePitch_ + glideOffset_ : -1.0;
    }

    // Gives way to the next note of a line played one note at a time: a
    // short fade, whatever its own release (a one-shot too).
    void fadeOut(const double seconds) noexcept
    {
        if (kickPlaying_) {
            // A kick cut by the next: quickly, so the two do not flam.
            kickFadeStep_ = 1.0 / (std::clamp(seconds, 0.001, 0.01) * kickRate_);
            return;
        }
        if (prismPlaying_) {
            prism_.release();
            return;
        }
        if (familyPlaying_) {
            // A drum of Silex plays out whatever the key does: cut, it goes.
            if (familyModel_ == synth::Model::silex)
                family_.kill();
            else
                family_.release();
            return;
        }
        if (stage_ == Stage::idle)
            return;
        if (stage_ == Stage::tail)
            return;
        fadeSeconds_ = std::max(0.001, seconds);
        if (stage_ != Stage::release)
            source_.releaseNote(false);
        stage_ = Stage::release;
        oneShot_ = false;
    }

    // A note-off: the release runs when a tail is allowed, else silence now.
    void stopNote(const bool allowTailOff) noexcept
    {
        if (kickPlaying_) {
            if (!allowTailOff)
                clear();
            return;
        }
        if (prismPlaying_) {
            if (allowTailOff)
                prism_.release();
            else
                clear();
            return;
        }
        if (familyPlaying_) {
            if (allowTailOff)
                family_.release();
            else
                clear();
            return;
        }
        if (allowTailOff && oneShot_ && stage_ != Stage::idle)
            return;
        if (allowTailOff && (stage_ == Stage::release || stage_ == Stage::tail))
            return;
        if (allowTailOff && stage_ != Stage::idle) {
            stage_ = Stage::release;
            source_.releaseNote();
        } else {
            clear();
        }
    }

    void clear() noexcept
    {
        prism_.kill();
        prismPlaying_ = false;
        family_.kill();
        familyPlaying_ = false;
        kick_.kill();
        kickPlaying_ = false;
        noteFrequency_ = 0.0;
        glideOffset_ = 0.0;
        fadeSeconds_ = 0.0;
        envelope_ = 0.0;
        filter_.reset();
        filterRight_.reset();
        stage_ = Stage::idle;
        oneShot_ = false;
        source_.reset();
    }

    // What it plays now, for an interface to show: its modulated settings,
    // LFOs and envelopes (Prisme and the synth family; false for the others).
    bool live(VoiceLive& out) const noexcept
    {
        if (!isActive())
            return false;
        if (prismPlaying_) {
            prism_.live(out);
            return true;
        }
        if (familyPlaying_) {
            out = family_.live();
            return true;
        }
        return false;
    }

    [[nodiscard]] bool isActive() const noexcept
    {
        if (prismPlaying_)
            return prism_.active();
        if (familyPlaying_)
            return family_.active();
        if (kickPlaying_)
            return kick_.active();
        return stage_ != Stage::idle && noteFrequency_ > 0.0;
    }

    // Renders `count` samples from `startSample`, added into each of the
    // `channelCount` channels. False once the voice is silent — the plan
    // disabled, or the release done — so the host can free it.
    bool render(const PolySynthGraphPlan& plan, float* const* channels,
                const int channelCount, const int startSample, const int count,
                const double sampleRate) noexcept
    {
        if (!isActive())
            return false;
        if (!plan.enabled) {
            clear();
            return false;
        }
        if (prismPlaying_)
            return renderPrism(plan, channels, channelCount, startSample, count, sampleRate);
        if (familyPlaying_)
            return renderFamily(plan, channels, channelCount, startSample, count, sampleRate);
        if (kickPlaying_)
            return renderKick(plan, channels, channelCount, startSample, count, sampleRate);
        const auto rate = std::max(1.0, sampleRate);
        // A Batterie's piece: its level and pan, its tuning, how fast it fades.
        auto drumLeft = 1.0;
        auto drumRight = 1.0;
        auto drumTune = 0.0;
        auto drumFade = 1.0;
        if (drumming_) {
            const auto shaping = drum::shapingFor(plan.drumValues, drumPiece_);
            drumLeft = shaping.gain * std::min(1.0, 1.0 - shaping.pan);
            drumRight = shaping.gain * std::min(1.0, 1.0 + shaping.pan);
            drumTune = shaping.semitones;
            drumFade = shaping.decaySeconds > 0.0 ? std::exp(-1.0 / (shaping.decaySeconds * rate)) : 1.0;
        }
        // The expression and the glide: the pitch as a ratio (again every 16
        // samples while it slides), the gain ramped over the block.
        auto pitchRatio = std::pow(2.0, (expressionPitch_ + glideOffset_ + drumTune) / 12.0);
        auto frequency = noteFrequency_ * pitchRatio;
        source_.setPitchRatio(pitchRatio);
        const auto gainStep = (expressionGain_ - currentGain_) / std::max(1, count);
        // A legato note eases in: the last one is still fading under it.
        const auto attackSeconds = softAttack_ ? std::max(plan.attackSeconds, 0.03f) : plan.attackSeconds;
        const auto attackIncrement = 1.0 / std::max(
            1.0, static_cast<double>(attackSeconds) * rate);
        const auto decayDecrement = (1.0 - plan.sustainLevel) / std::max(
            1.0, static_cast<double>(plan.decaySeconds) * rate);
        const auto releaseMultiplier = std::exp(
            std::log(0.0001) / std::max(
                1.0, (fadeSeconds_ > 0.0 ? fadeSeconds_ : static_cast<double>(plan.releaseSeconds)) * rate));
        filter_.setCoefficients(plan.filterCutoffHz, plan.filterResonance, rate);
        if (stereo_)
            filterRight_.setCoefficients(plan.filterCutoffHz, plan.filterResonance, rate);
        // The level and the sustain follow their knobs sample by sample (a
        // couple of milliseconds behind): no step where a block begins.
        const auto ease = 1.0 - std::exp(-1.0 / (0.002 * rate));
        const auto wantedLevel = static_cast<double>(plan.oscillatorLevel);

        for (int sample = 0; sample < count; ++sample) {
            currentGain_ += gainStep;
            levelHeard_ += (wantedLevel - levelHeard_) * ease;
            if (glideOffset_ != 0.0) {
                glideOffset_ -= glideStep_;
                if ((glideStep_ > 0.0 && glideOffset_ < 0.0) || (glideStep_ < 0.0 && glideOffset_ > 0.0) || glideStep_ == 0.0)
                    glideOffset_ = 0.0;
                if ((sample & 15) == 0 || glideOffset_ == 0.0) {
                    pitchRatio = std::pow(2.0, (expressionPitch_ + glideOffset_ + drumTune) / 12.0);
                    frequency = noteFrequency_ * pitchRatio;
                    source_.setPitchRatio(pitchRatio);
                }
            }
            switch (stage_) {
            case Stage::attack:
                envelope_ += attackIncrement;
                if (envelope_ >= 1.0) {
                    envelope_ = 1.0;
                    stage_ = Stage::decay;
                }
                break;
            case Stage::decay:
                envelope_ -= decayDecrement;
                if (envelope_ <= plan.sustainLevel) {
                    envelope_ = plan.sustainLevel;
                    stage_ = Stage::sustain;
                }
                break;
            case Stage::sustain:
                envelope_ += (static_cast<double>(plan.sustainLevel) - envelope_) * ease;
                break;
            case Stage::release:
                envelope_ *= releaseMultiplier;
                if (envelope_ <= 0.0001) {
                    // A sampler's release zones still sounding: they play out.
                    if (!source_.samplerTailing()) {
                        clear();
                        return false;
                    }
                    source_.endSamplerNote();
                    envelope_ = 0.0;
                    stage_ = Stage::tail;
                }
                break;
            case Stage::tail:
                envelope_ = 0.0;
                break;
            case Stage::idle:
                return false;
            }
            if (stereo_) {
                // Two channels of its own (a sampler, a sound model): each
                // through its filter, the left and right of the track. A
                // sampler's release zones are not shaped by the envelope.
                auto left = 0.0;
                auto right = 0.0;
                auto tailLeft = 0.0;
                auto tailRight = 0.0;
                source_.nextFrame(plan, frequency, rate, left, right, tailLeft, tailRight);
                if (drumming_) {
                    drumLevel_ *= drumFade;
                    if (drumLevel_ < 1.0e-4) {
                        clear();
                        return false;
                    }
                    left *= drumLeft * drumLevel_;
                    right *= drumRight * drumLevel_;
                    tailLeft *= drumLeft * drumLevel_;
                    tailRight *= drumRight * drumLevel_;
                }
                const auto level = levelHeard_ * envelope_ * currentGain_;
                const auto tailLevel = levelHeard_ * currentGain_;
                const auto leftValue = static_cast<float>(filter_.process(left * level + tailLeft * tailLevel));
                const auto rightValue = static_cast<float>(filterRight_.process(right * level + tailRight * tailLevel));
                if (channelCount == 1) {
                    channels[0][startSample + sample] += 0.5f * (leftValue + rightValue);
                } else {
                    for (int channel = 0; channel < channelCount; ++channel)
                        channels[channel][startSample + sample] += channel == 0 ? leftValue
                                                                   : channel == 1 ? rightValue
                                                                                  : 0.5f * (leftValue + rightValue);
                }
                continue;
            }
            const auto oscillator = source_.nextSample(plan, frequency, rate);
            const auto raw = oscillator * (modelled_ ? 1.0 : velocity_) * levelHeard_ * envelope_ * currentGain_;
            const auto value = static_cast<float>(filter_.process(raw));
            for (int channel = 0; channel < channelCount; ++channel)
                channels[channel][startSample + sample] += value;
        }
        // A sound model ends by itself: its frames ran out, or its release
        // did; a sampler when its samples have played.
        if (source_.finished(plan)) {
            clear();
            return false;
        }
        return true;
    }

private:
    // tail: the envelope closed, a sampler's release zones still playing.
    enum class Stage { idle, attack, decay, sustain, release, tail };

    bool renderPrism(const PolySynthGraphPlan& plan, float* const* channels, const int channelCount, const int startSample,
                     const int count, const double sampleRate) noexcept
    {
        static const prism::Context defaultContext {};
        const auto& context = prismContext_ != nullptr ? *prismContext_ : defaultContext;
        prism_.setExpression(expressionPitch_, expressionGain_);
        if (plan.prismTables == nullptr || plan.waveform != PolyOscillatorWaveform::prism) {
            clear();
            return false;
        }
        auto alive = true;
        if (channelCount >= 2) {
            alive = prism_.render(plan.prismValues, *plan.prismTables, context, channels[0] + startSample,
                                  channels[1] + startSample, count, std::max(1.0, sampleRate));
        } else if (channelCount == 1) {
            // One channel: the two summed, a block at a time.
            float left[256];
            float right[256];
            for (int done = 0; done < count && alive; done += 256) {
                const auto length = std::min(256, count - done);
                std::fill(left, left + length, 0.0f);
                std::fill(right, right + length, 0.0f);
                alive = prism_.render(plan.prismValues, *plan.prismTables, context, left, right, length,
                                      std::max(1.0, sampleRate));
                for (int i = 0; i < length; ++i)
                    channels[0][startSample + done + i] += 0.5f * (left[i] + right[i]);
            }
        }
        if (!alive)
            prismPlaying_ = false;
        return alive;
    }

    // A synth of the family: into the track's two channels, or a block at a
    // time summed into one.
    bool renderFamily(const PolySynthGraphPlan& plan, float* const* channels, const int channelCount, const int startSample,
                      const int count, const double sampleRate) noexcept
    {
        static const prism::Context defaultContext {};
        const auto& context = prismContext_ != nullptr ? *prismContext_ : defaultContext;
        family_.setExpression(expressionPitch_, expressionGain_);
        if (plan.waveform != PolyOscillatorWaveform::family || plan.synthModel == synth::Model::none) {
            clear();
            return false;
        }
        auto alive = true;
        if (channelCount >= 2) {
            alive = family_.render(plan.synthValues, context, channels[0] + startSample, channels[1] + startSample, count,
                                   std::max(1.0, sampleRate));
        } else if (channelCount == 1) {
            float left[256];
            float right[256];
            for (int done = 0; done < count && alive; done += 256) {
                const auto length = std::min(256, count - done);
                std::fill(left, left + length, 0.0f);
                std::fill(right, right + length, 0.0f);
                alive = family_.render(plan.synthValues, context, left, right, length, std::max(1.0, sampleRate));
                for (int i = 0; i < length; ++i)
                    channels[0][startSample + done + i] += 0.5f * (left[i] + right[i]);
            }
        }
        if (!alive)
            familyPlaying_ = false;
        return alive;
    }

    // A block at a time into its own two channels, then its level (the
    // card's, the note's gain, a cut by the next kick) into the track's.
    bool renderKick(const PolySynthGraphPlan& plan, float* const* channels, const int channelCount, const int startSample,
                    const int count, const double sampleRate) noexcept
    {
        if (plan.waveform != PolyOscillatorWaveform::kick) {
            clear();
            return false;
        }
        const auto rate = std::max(1.0, sampleRate);
        const auto gainStep = (expressionGain_ - currentGain_) / std::max(1, count);
        const auto level = static_cast<double>(plan.oscillatorLevel);
        float left[256];
        float right[256];
        auto alive = true;
        for (int done = 0; done < count && alive; done += 256) {
            const auto length = std::min(256, count - done);
            std::fill(left, left + length, 0.0f);
            std::fill(right, right + length, 0.0f);
            alive = kick_.render(plan.kickValues, left, right, length, rate);
            for (int i = 0; i < length; ++i) {
                currentGain_ += gainStep;
                if (kickFadeStep_ > 0.0)
                    kickFade_ = std::max(0.0, kickFade_ - kickFadeStep_);
                const auto gain = static_cast<float>(level * currentGain_ * kickFade_);
                const auto l = left[i] * gain;
                const auto r = right[i] * gain;
                const auto at = startSample + done + i;
                if (channelCount == 1) {
                    channels[0][at] += 0.5f * (l + r);
                } else {
                    for (int channel = 0; channel < channelCount; ++channel)
                        channels[channel][at] += channel == 0 ? l : channel == 1 ? r : 0.5f * (l + r);
                }
            }
            if (kickFadeStep_ > 0.0 && kickFade_ <= 0.0)
                alive = false;
        }
        if (!alive)
            clear();
        return alive;
    }

    prism::Voice prism_;
    const prism::Context* prismContext_ {};
    bool prismPlaying_ {};
    // A Batterie's note: its piece, how far it has faded, its hand's wander.
    bool drumming_ {};
    drum::Piece drumPiece_ { drum::Piece::other };
    double drumLevel_ { 1.0 };
    std::uint32_t drumSeed_ { 0x6d2b79f5u };
    synth::Voice family_;
    bool familyPlaying_ {};
    synth::Model familyModel_ { synth::Model::none };
    int familyNote_ {};
    kick::Voice kick_;
    bool kickPlaying_ {};
    int kickNote_ {};
    double kickRate_ { 48'000.0 };
    double kickFade_ { 1.0 };
    double kickFadeStep_ {};
    double expressionPitch_ {};
    double expressionGain_ { 1.0 };
    double currentGain_ { 1.0 };

    PolyVoiceOscillator source_;
    PolyVoiceFilter filter_;
    PolyVoiceFilter filterRight_;
    double noteFrequency_ {};
    double notePitch_ {};
    double glideOffset_ {};
    double glideStep_ {};
    double fadeSeconds_ {};
    bool softAttack_ {};
    double velocity_ {};
    double envelope_ {};
    // The level as heard, easing toward the plan's.
    double levelHeard_ {};
    bool modelled_ {};
    bool stereo_ {};
    bool oneShot_ {};
    Stage stage_ { Stage::idle };
};

} // namespace cydaw::engine
