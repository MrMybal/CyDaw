#pragma once

#include "BuiltinProcessors.h"
#include "PolySynthGraph.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

namespace cydaw::engine {

// The sound source of one polyphonic voice.
//
// CyDAW renders polyphony from a fixed voice whose settings a graph supplies,
// and that voice was written twice: once in the DAW's MIDI instrument track,
// once in the graph editor's preview. When the wavetable and the string were
// added, only the preview learned them — so the same instrument that sang in
// the editor was silent in the DAW, because a switch over the waveform had no
// case for it and left the sample at zero.
//
// Both renderers now hold one of these instead of a switch of their own.
// Adding a mode means editing this file, and nothing else has to be found.
class PolyVoiceOscillator final {
public:
    // Off the audio thread: sizes the delay line for the lowest note the
    // string will ever be asked for, and forces the wavetable bank to exist.
    // Building that bank is two hundred thousand sines, and the first note
    // must not be the thing that pays for it.
    void prepare(const double sampleRate)
    {
        const auto longest = static_cast<std::size_t>(
            std::max(64.0, std::max(8'000.0, sampleRate) / 20.0) + 4.0);
        stringLine_.assign(longest, 0.0f);
        (void) WavetableOscillatorProcessor::readTable(0.0f, 0.0);
        soundModel_.prepare(sampleRate);
        reset();
    }

    // `midiNote` (transposed) and `velocity` (0..1) choose a sound model's
    // zone and how it plays; the other modes need the frequency alone.
    void startNote(const PolySynthGraphPlan& plan,
                   const double frequencyHz,
                   const double sampleRate,
                   const double midiNote = 60.0,
                   const double velocity = 0.8) noexcept
    {
        reset();
        if (plan.waveform == PolyOscillatorWaveform::soundModel && plan.soundModel != nullptr)
            soundModel_.start(*plan.soundModel, plan.soundModelValues, midiNote, velocity, sampleRate);
        if (plan.waveform == PolyOscillatorWaveform::sampler && plan.sampler != nullptr)
            sampler_.start(*plan.sampler, plan.samplerValues, midiNote, velocity, sampleRate, cuesFor(plan));
        // A string is struck, not switched on: one period of noise is the
        // hammer, and everything after it is the loop ringing.
        stringExcitation_ = plan.waveform == PolyOscillatorWaveform::string
            ? static_cast<std::uint32_t>(std::max(
                  1.0, std::max(8'000.0, sampleRate)
                           / std::max(20.0, frequencyHz)))
            : 0u;
    }

    void reset() noexcept
    {
        phase_ = 0.0;
        // All three unison voices start together. Spreading them a third of a
        // cycle apart sounded wide until the detune reached zero, at which
        // point three identical waves a third apart cancel every harmonic but
        // the third: the note went quiet and jumped a twelfth.
        tablePhases_ = { 0.0, 0.0, 0.0 };
        std::fill(stringLine_.begin(), stringLine_.end(), 0.0f);
        stringWrite_ = 0;
        stringLowpass_ = 0.0f;
        dcState_ = 0.0f;
        dcPrevious_ = 0.0f;
        stringExcitation_ = 0;
        cachedDetuneCents_ = -1.0f;
        cachedDecaySeconds_ = -1.0f;
        cachedLoopLength_ = 0;
        soundModel_.reset();
        sampler_.reset();
    }

    // Key up: a sound model goes to its release, a sampler's loop held while
    // the key is down lets go and its release zones sound (not when
    // `sounds` is false: a note cut, not let go); the other modes leave it
    // to the envelope.
    void releaseNote(const bool sounds = true) noexcept
    {
        soundModel_.release();
        sampler_.release(sounds);
    }
    // What a sampler's SFZ reads of the track's MIDI (null: as the card has it).
    void setSamplerPlaying(const SamplerPlaying* playing) noexcept { samplerPlaying_ = playing; }
    // A sampler with only its release's zones left playing.
    [[nodiscard]] bool samplerTailing() const noexcept { return sampler_.active() && sampler_.tailing(); }
    // The note's own zones end; its release's go on.
    void endSamplerNote() noexcept { sampler_.endNote(); }
    // A sound model that has played to its end, or faded out; a sampler
    // whose samples have all ended.
    [[nodiscard]] bool finished(const PolySynthGraphPlan& plan) const noexcept
    {
        if (plan.waveform == PolyOscillatorWaveform::sampler)
            return !sampler_.active();
        return plan.waveform == PolyOscillatorWaveform::soundModel && !soundModel_.active();
    }

    // Whether this plan's voice has two channels of its own (a sampler, a
    // sound model); every other source is mono, the same on both sides.
    [[nodiscard]] static bool stereo(const PolySynthGraphPlan& plan) noexcept
    {
        return plan.waveform == PolyOscillatorWaveform::sampler || plan.waveform == PolyOscillatorWaveform::soundModel;
    }

    // A note's pitch moved along it, as a ratio (the recordings' sources; the
    // others take it in their frequency).
    // The choke group that cuts a sampler's note, 0: none.
    [[nodiscard]] int samplerOffBy(const PolySynthGraphPlan& plan) const noexcept
    {
        if (plan.waveform != PolyOscillatorWaveform::sampler || plan.sampler == nullptr)
            return 0;
        return sampler_.offBy(*plan.sampler);
    }

    void setPitchRatio(const double ratio) noexcept
    {
        sampler_.setPitchRatio(ratio);
        soundModel_.setPitchRatio(ratio);
    }

    // Both channels of the next sample.
    void nextFrame(const PolySynthGraphPlan& plan, const double frequencyHz, const double sampleRate, double& left,
                   double& right) noexcept
    {
        auto tailLeft = 0.0;
        auto tailRight = 0.0;
        nextFrame(plan, frequencyHz, sampleRate, left, right, tailLeft, tailRight);
        left += tailLeft;
        right += tailRight;
    }
    // The same, a sampler's release zones apart (`tail`: the note's envelope
    // does not shape them).
    void nextFrame(const PolySynthGraphPlan& plan, const double frequencyHz, const double sampleRate, double& left,
                   double& right, double& tailLeft, double& tailRight) noexcept
    {
        tailLeft = 0.0;
        tailRight = 0.0;
        if (plan.waveform == PolyOscillatorWaveform::sampler) {
            sampler_.next(plan.sampler.get(), plan.samplerValues, cuesFor(plan), left, right, tailLeft, tailRight);
            return;
        }
        if (plan.waveform == PolyOscillatorWaveform::soundModel) {
            soundModel_.nextFrame(plan.soundModel.get(), plan.soundModelValues, sampleRate, left, right);
            return;
        }
        left = nextSample(plan, frequencyHz, sampleRate);
        right = left;
    }

    [[nodiscard]] double nextSample(const PolySynthGraphPlan& plan,
                                    const double frequencyHz,
                                    const double sampleRate) noexcept
    {
        constexpr auto twoPi = 2.0 * std::numbers::pi;
        const auto rate = std::max(8'000.0, sampleRate);
        const auto frequency = std::clamp(frequencyHz, 8.0, rate * 0.45);
        auto value = 0.0;
        switch (plan.waveform) {
        case PolyOscillatorWaveform::sine:
            value = std::sin(phase_);
            break;
        case PolyOscillatorWaveform::triangle:
            value = 2.0 / std::numbers::pi * std::asin(std::sin(phase_));
            break;
        case PolyOscillatorWaveform::saw:
            value = phase_ / std::numbers::pi - 1.0;
            break;
        case PolyOscillatorWaveform::square:
            value = phase_ < std::numbers::pi ? 1.0 : -1.0;
            break;
        case PolyOscillatorWaveform::wavetable:
            value = wavetableSample(plan, frequency, rate);
            break;
        case PolyOscillatorWaveform::string:
            value = stringSample(plan, frequency, rate);
            break;
        case PolyOscillatorWaveform::soundModel:
            value = soundModel_.nextSample(plan.soundModel.get(), plan.soundModelValues, sampleRate);
            break;
        case PolyOscillatorWaveform::sampler: {
            auto left = 0.0;
            auto right = 0.0;
            auto tailLeft = 0.0;
            auto tailRight = 0.0;
            sampler_.next(plan.sampler.get(), plan.samplerValues, cuesFor(plan), left, right, tailLeft, tailRight);
            value = 0.5 * (left + right + tailLeft + tailRight);
            break;
        }
        }
        phase_ += twoPi * frequency / rate;
        if (phase_ >= twoPi)
            phase_ -= twoPi * std::floor(phase_ / twoPi);
        return value;
    }

private:
    // A sampler's note: its card's controllers and articulation, the track's MIDI.
    [[nodiscard]] SamplerCues cuesFor(const PolySynthGraphPlan& plan) const noexcept
    {
        return SamplerCues { &plan.samplerControllers, samplerPlaying_,
                             samplerValue(plan.samplerValues, SamplerSetting::articulation) };
    }

    [[nodiscard]] double wavetableSample(const PolySynthGraphPlan& plan,
                                         const double frequency,
                                         const double rate) noexcept
    {
        const auto detune = std::clamp(plan.wavetableDetuneCents, 0.0f, 50.0f);
        // The ratios only change when the detune does, and a pow per voice per
        // sample cost more than the table read it was feeding.
        if (detune != cachedDetuneCents_) {
            cachedDetuneCents_ = detune;
            detuneRatios_ = {
                std::pow(2.0, -static_cast<double>(detune) / 1'200.0), 1.0,
                std::pow(2.0, static_cast<double>(detune) / 1'200.0)
            };
        }
        // With no detune the three voices are one voice, and rendering it once
        // is both cheaper and the only reading that keeps its amplitude.
        const std::size_t voices = detune > 0.01f ? 3u : 1u;
        const std::size_t first = voices == 1u ? 1u : 0u;
        auto value = 0.0;
        for (auto voice = first; voice < first + voices; ++voice) {
            value += static_cast<double>(
                WavetableOscillatorProcessor::readTable(
                    plan.wavetable.get(), plan.wavetablePosition,
                    tablePhases_[voice]));
            tablePhases_[voice] += frequency * detuneRatios_[voice] / rate;
            if (tablePhases_[voice] >= 1.0)
                tablePhases_[voice] -= std::floor(tablePhases_[voice]);
        }
        return value / static_cast<double>(voices);
    }

    [[nodiscard]] double stringSample(const PolySynthGraphPlan& plan,
                                      const double frequency,
                                      const double rate) noexcept
    {
        const auto size = stringLine_.size();
        if (size < 8)
            return 0.0;
        const auto brightness = std::max(
            0.02f, 1.0f - std::clamp(plan.stringDamping, 0.0f, 0.98f));
        // The loop filter is part of the delay. Ignoring its own group delay
        // detuned the string by as much as three semitones at the damped end
        // of the knob, and truncating the period to a whole number of samples
        // detuned every note that did not land on one.
        const auto filterDelay = (1.0 - static_cast<double>(brightness))
                                 / static_cast<double>(brightness);
        const auto target = std::clamp(rate / frequency - filterDelay, 2.0,
                                       static_cast<double>(size - 3));
        const auto length = static_cast<std::size_t>(target);
        const auto fraction = target - static_cast<double>(length);
        const auto older = (stringWrite_ + size - length - 1) % size;
        const auto newer = (stringWrite_ + size - length) % size;
        const auto delayed = static_cast<double>(stringLine_[newer])
                * (1.0 - fraction)
            + static_cast<double>(stringLine_[older]) * fraction;
        const auto decay = std::clamp(plan.stringDecaySeconds, 0.05f, 20.0f);
        if (decay != cachedDecaySeconds_ || length != cachedLoopLength_) {
            cachedDecaySeconds_ = decay;
            cachedLoopLength_ = length;
            const auto roundTrips = static_cast<double>(decay) * rate
                / static_cast<double>(std::max<std::size_t>(1, length));
            cachedFeedback_ = static_cast<float>(
                std::pow(0.001, 1.0 / std::max(1.0, roundTrips)));
        }
        stringLowpass_ += brightness
                          * (static_cast<float>(delayed) - stringLowpass_);
        auto excitation = 0.0f;
        if (stringExcitation_ > 0) {
            noiseState_ = noiseState_ * 1'664'525u + 1'013'904'223u;
            excitation = static_cast<float>(
                static_cast<double>(noiseState_ >> 8)
                    / static_cast<double>(0x00ffffffu) * 2.0 - 1.0);
            --stringExcitation_;
        }
        // Direct current is the one frequency the loop filter does not take
        // away, so without this the offset left by the excitation burst is fed
        // back thousands of times and walks the whole string off centre.
        const auto damped = stringLowpass_ * cachedFeedback_;
        dcState_ = damped - dcPrevious_ + 0.995f * dcState_;
        dcPrevious_ = damped;
        stringLine_[stringWrite_] = dcState_ + excitation;
        stringWrite_ = (stringWrite_ + 1) % size;
        return delayed;
    }

    double phase_ {};
    std::array<double, 3> tablePhases_ { 0.0, 0.0, 0.0 };
    std::array<double, 3> detuneRatios_ { 1.0, 1.0, 1.0 };
    float cachedDetuneCents_ { -1.0f };
    // Sized once, when the voice is prepared: a string voice must never
    // allocate while it is ringing.
    std::vector<float> stringLine_;
    std::size_t stringWrite_ {};
    float stringLowpass_ {};
    float dcState_ {};
    float dcPrevious_ {};
    float cachedFeedback_ { 0.99f };
    float cachedDecaySeconds_ { -1.0f };
    std::size_t cachedLoopLength_ {};
    std::uint32_t stringExcitation_ {};
    std::uint32_t noiseState_ { 0x6d2b79f5u };
    SoundModelVoice soundModel_;
    SamplerVoice sampler_;
    const SamplerPlaying* samplerPlaying_ {};
};

} // namespace cydaw::engine
