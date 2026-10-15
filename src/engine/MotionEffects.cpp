#include <cydaw/engine/MotionEffects.h>

#include <cydaw/engine/AudioBuffer.h>
#include <cydaw/engine/EffectCatalogue.h>
#include <cydaw/engine/MotionDesign.h>

#include "DspParts.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <span>
#include <utility>

namespace cydaw::engine {

namespace {

using namespace motion;

// Added where a recursive filter could otherwise fade into denormals.
constexpr double kAntiDenormal = 1.0e-20;

// The place (0..1) of a cycle, an LFO's or a volume curve's: from the
// transport while it plays and a note value is chosen, so it stays on the
// beat; else free at its rate (or at the note value's, from the tempo).
class Cycle {
public:
    void reset(const double at = 0.0) noexcept { phase_ = phaseWrapped(at); }
    void begin(const double hz, const float sync, const ProcessContext& context, const double rate) noexcept
    {
        const auto bpm = std::clamp(context.transport.beatsPerMinute, 20.0, 400.0);
        const auto* division = noteDivisionFor(sync);
        locked_ = division != nullptr && context.transport.state != PlaybackState::stopped;
        if (division != nullptr) {
            beats_ = std::max(1.0e-3, division->beats);
            increment_ = bpm / 60.0 / beats_ / rate;
        } else {
            increment_ = std::max(0.0, hz) / rate;
        }
        beat_ = context.transport.quarterNotePosition;
        beatStep_ = bpm / 60.0 / rate;
    }
    // The place now; then a sample on.
    double next() noexcept
    {
        if (locked_) {
            phase_ = phaseWrapped(beat_ / beats_);
            beat_ += beatStep_;
            return phase_;
        }
        const auto now = phase_;
        phase_ = phaseWrapped(phase_ + increment_);
        return now;
    }

private:
    double phase_ {};
    double increment_ {};
    double beat_ {};
    double beatStep_ {};
    double beats_ { 1.0 };
    bool locked_ {};
};

// The channels a processor works on: the first two; any more pass through.
std::uint32_t workedChannels(const std::uint32_t channels) noexcept
{
    return std::min<std::uint32_t>(channels, 2);
}

void passThrough(const AudioBlockView input, const AudioBlockView output, const std::uint32_t from, const std::uint32_t channels,
                 const std::uint32_t frames) noexcept
{
    for (auto channel = from; channel < channels; ++channel)
        std::copy_n(input.channel(channel), frames, output.channel(channel));
}

// ---- Pompe --------------------------------------------------------------------------------------

// A volume shaper: each cycle the gain follows the curve MotionDesign.h
// draws, from the transport's beat, at a free rate, or restarted by every
// hit on the key (the sidechain, else the sound itself). A one-pole smooths
// the curve's corners so a cut does not click.
class Pompe final : public EffectProcessor, public MultiBusAudioProcessor {
public:
    using EffectProcessor::EffectProcessor;

    void prepare(const ProcessSpec& spec) override
    {
        rate_ = std::max(1.0, spec.sampleRate);
        main_.prepare(std::max<std::uint32_t>(1, spec.channelCount), std::max<std::uint32_t>(1, spec.maximumBlockSize));
        reset();
    }

    void reset() noexcept override
    {
        cycle_.reset();
        position_ = 2.0;
        envelope_ = 0.0;
        held_ = 0;
        armed_ = true;
        gains_ = { 1.0, 1.0 };
        for (auto& reading : readings_)
            reading.store(0.0f, std::memory_order_relaxed);
    }

    // A key wired in finds the hits instead of its own sound (a kick ducking
    // the bass).
    void processAudioBuses(const std::span<const AudioInputBusView> inputs, const AudioBlockView output,
                           const ProcessContext& context) noexcept override
    {
        auto main = main_.view(std::min(output.frameCount, main_.maximumFrameCount()));
        clearAudio(main);
        key_ = {};
        for (const auto& bus : inputs) {
            if (bus.portId == effectSidechainPortId)
                key_ = bus.audio;
            else
                addAudio(main, bus.audio);
        }
        process(main, { output.data, output.channelCount, main.frameCount, output.channelStride }, context);
        key_ = {};
    }

    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext& context) noexcept override
    {
        clearAudio(output);
        const auto channels = std::min(input.channelCount, output.channelCount);
        const auto frames = std::min(input.frameCount, output.frameCount);
        std::array<float, pompeParameterCount> values {};
        for (int index = 0; index < pompeParameterCount; ++index)
            values[static_cast<std::size_t>(index)] = value(static_cast<std::size_t>(index));
        const auto* v = values.data();
        const auto keyMode = choiceOf(values[pompeTrigger], 2) == 1;
        cycle_.begin(clampedOf(values[pompeRate], 0.1, 20.0), values[pompeSync], context, rate_);
        // After a hit the curve runs once: the note value's length, or a
        // period of the rate.
        auto cycleSeconds = 1.0 / clampedOf(values[pompeRate], 0.1, 20.0);
        if (const auto* division = noteDivisionFor(values[pompeSync]))
            cycleSeconds = division->beats * 60.0 / std::clamp(context.transport.beatsPerMinute, 20.0, 400.0);
        const auto keyStep = 1.0 / (std::max(0.005, cycleSeconds) * rate_);
        const auto smoothingMs = clampedOf(values[pompeSmoothing], 0.0, 20.0);
        const auto follow = smoothingMs > 0.01 ? dsp::smoothing(smoothingMs, rate_) : 0.0;
        const auto mix = clampedOf(values[pompeMix], 0.0, 1.0);
        const auto gain = dsp::dbToGain(clampedOf(values[pompeOutput], -24.0, 24.0));
        const auto offset = clampedOf(values[pompeStereoOffset], 0.0, 0.5);
        const auto threshold = dsp::dbToGain(clampedOf(values[pompeThreshold], -60.0, 0.0));
        const auto rearm = threshold * dsp::dbToGain(-pompeRearmDb);
        // The key's level: its peaks held a moment (a low note's dips between
        // two peaks are no new hit), then let fall.
        const auto holdSamples = static_cast<int>(0.02 * rate_);
        const auto fall = dsp::smoothing(40.0, rate_);
        const auto keyed = key_.data != nullptr && key_.channelCount > 0 && key_.frameCount >= frames;
        const auto worked = workedChannels(channels);
        auto phaseNow = 0.0;
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            double left {};
            double right {};
            if (keyMode) {
                auto peak = 0.0;
                if (keyed)
                    for (std::uint32_t channel = 0; channel < key_.channelCount; ++channel)
                        peak = std::max(peak, static_cast<double>(std::abs(key_.channel(channel)[frame])));
                else
                    for (std::uint32_t channel = 0; channel < channels; ++channel)
                        peak = std::max(peak, static_cast<double>(std::abs(input.channel(channel)[frame])));
                if (!std::isfinite(peak))
                    peak = 0.0;
                if (peak >= envelope_) {
                    envelope_ = peak;
                    held_ = holdSamples;
                } else if (held_ > 0) {
                    --held_;
                } else {
                    envelope_ = envelope_ > 1.0e-9 ? envelope_ * fall : 0.0;
                }
                if (armed_ && envelope_ > threshold) {
                    position_ = 0.0;
                    armed_ = false;
                } else if (!armed_ && envelope_ < rearm) {
                    armed_ = true;
                }
                left = pompeKeyGainAt(v, position_);
                right = offset > 0.0 ? pompeKeyGainAt(v, position_ - offset) : left;
                phaseNow = std::min(1.0, position_);
                position_ = std::min(2.0, position_ + keyStep);
            } else {
                const auto phase = cycle_.next();
                left = pompeGainAt(v, phase);
                right = offset > 0.0 ? pompeGainAt(v, phase - offset) : left;
                phaseNow = phase;
            }
            gains_[0] = left + (gains_[0] - left) * follow;
            gains_[1] = right + (gains_[1] - right) * follow;
            for (auto& g : gains_)
                if (std::abs(g) < 1.0e-12)
                    g = 0.0;
            for (std::uint32_t channel = 0; channel < worked; ++channel) {
                const auto shaped = 1.0 - mix + mix * gains_[channel];
                output.channel(channel)[frame] = static_cast<float>(static_cast<double>(input.channel(channel)[frame]) * shaped * gain);
            }
        }
        passThrough(input, output, worked, channels, frames);
        readings_[pompeReadingGain].store(static_cast<float>(gains_[0]), std::memory_order_relaxed);
        readings_[pompeReadingPhase].store(static_cast<float>(phaseNow), std::memory_order_relaxed);
        readings_[pompeReadingKeyDb].store(static_cast<float>(dsp::gainToDb(envelope_)), std::memory_order_relaxed);
        readings_[pompeReadingRightGain].store(static_cast<float>(gains_[1]), std::memory_order_relaxed);
    }

    [[nodiscard]] std::size_t readings(float* into, const std::size_t count) const noexcept override
    {
        const auto n = std::min<std::size_t>(count, readings_.size());
        for (std::size_t index = 0; index < n; ++index)
            into[index] = readings_[index].load(std::memory_order_relaxed);
        return n;
    }

private:
    double rate_ { 48'000.0 };
    AudioBuffer main_;
    AudioBlockView key_ {};
    Cycle cycle_;
    // Cycles since the last hit (2: idle, at the top).
    double position_ { 2.0 };
    double envelope_ {};
    int held_ {};
    bool armed_ { true };
    std::array<double, 2> gains_ { 1.0, 1.0 };
    std::array<std::atomic<float>, pompeReadingCount> readings_ {};
};

// ---- Décaleur -----------------------------------------------------------------------------------

// One section of an all-pass in z^-2: y = c (x + y[n-2]) - x[n-2].
struct AllPassSection {
    double c {};
    double x1 {};
    double x2 {};
    double y1 {};
    double y2 {};
    void reset() noexcept { x1 = x2 = y1 = y2 = 0.0; }
    double operator()(const double x) noexcept
    {
        const auto y = c * (x + y2) - x2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }
};

// Two chains of all-passes whose outputs stay 90° apart (within 0.7° from
// about 30 Hz to the top at 48 kHz, 1.6° at 20 Hz): the sound in phase and
// in quadrature, what a single-sideband shift needs. The standard
// eighth-order pair; the quadrature chain runs a sample late.
class Hilbert {
public:
    Hilbert() noexcept
    {
        constexpr std::array<double, 4> quadrature { 0.6923878, 0.9360654322959, 0.9882295226860, 0.9987488452737 };
        constexpr std::array<double, 4> inPhase { 0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278 };
        for (std::size_t index = 0; index < 4; ++index) {
            quadrature_[index].c = quadrature[index] * quadrature[index];
            inPhase_[index].c = inPhase[index] * inPhase[index];
        }
    }
    void reset() noexcept
    {
        for (auto& section : quadrature_)
            section.reset();
        for (auto& section : inPhase_)
            section.reset();
        late_ = 0.0;
    }
    // The sound in phase, and 90° behind it.
    std::pair<double, double> operator()(const double x) noexcept
    {
        auto q = x;
        for (auto& section : quadrature_)
            q = section(q);
        auto i = x;
        for (auto& section : inPhase_)
            i = section(i);
        const auto behind = late_;
        late_ = q;
        return { i, behind };
    }

private:
    std::array<AllPassSection, 4> quadrature_ {};
    std::array<AllPassSection, 4> inPhase_ {};
    double late_ {};
};

// Every frequency moved by the same hertz: the sound and its quadrature
// turned by an oscillator at the shift (I cos - Q sin keeps one sideband; I
// cos alone keeps both). Its output fed back through a delay climbs (or
// falls) again each time round: the spirals.
class Decaleur final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;

    void prepare(const ProcessSpec& spec) override
    {
        rate_ = std::max(1.0, spec.sampleRate);
        for (auto& line : lines_)
            line.prepare(static_cast<std::size_t>(1.05 * rate_) + 8);
        reset();
    }

    void reset() noexcept override
    {
        for (auto& hilbert : hilbert_)
            hilbert.reset();
        for (auto& line : lines_)
            line.reset();
        phases_ = {};
        lfo_.reset();
        delay_ = -1.0;
        for (auto& reading : readings_)
            reading.store(0.0f, std::memory_order_relaxed);
    }

    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext& context) noexcept override
    {
        clearAudio(output);
        const auto channels = std::min(input.channelCount, output.channelCount);
        const auto frames = std::min(input.frameCount, output.frameCount);
        std::array<float, decaleurParameterCount> values {};
        for (int index = 0; index < decaleurParameterCount; ++index)
            values[static_cast<std::size_t>(index)] = value(static_cast<std::size_t>(index));
        const auto* v = values.data();
        const auto both = choiceOf(values[decaleurMode], decaleurModeCount) == 1;
        const auto wave = choiceOf(values[decaleurLfoShape], lfoWaveCount);
        lfo_.begin(clampedOf(values[decaleurLfoRate], 0.01, 10.0), values[decaleurSync], context, rate_);
        const auto feedback = clampedOf(values[decaleurFeedback], 0.0, 0.95);
        const auto target = std::max(decaleurShortestDelayMs, clampedOf(values[decaleurDelay], 0.0, 1'000.0)) * 0.001 * rate_;
        if (delay_ < 0.0)
            delay_ = target;
        const auto mix = clampedOf(values[decaleurMix], 0.0, 1.0);
        const auto gain = dsp::dbToGain(clampedOf(values[decaleurOutput], -24.0, 24.0));
        const auto worked = workedChannels(channels);
        auto shiftNow = 0.0;
        DecaleurShifts shifts {};
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            const auto lfo = lfoWaveAt(wave, lfo_.next());
            shiftNow = decaleurShiftAt(v, lfo);
            shifts = decaleurShiftsOf(v, lfo);
            // A delay changed glides there (its pitch bends, it does not click).
            delay_ += (target - delay_) * 0.0008;
            for (std::uint32_t channel = 0; channel < worked; ++channel) {
                const auto x = static_cast<double>(input.channel(channel)[frame]);
                auto& line = lines_[channel];
                const auto back = feedback > 0.0 ? feedback * std::tanh(line.read(delay_ - 1.0)) : 0.0;
                const auto [inPhase, behind] = hilbert_[channel](x + back + kAntiDenormal);
                auto& phase = phases_[channel];
                const auto angle = motionTwoPi * phase;
                const auto cosine = std::cos(angle);
                const auto wet = both ? inPhase * cosine * std::numbers::sqrt2 : inPhase * cosine - behind * std::sin(angle);
                // Alone (mono), the sound takes the shift unspread.
                const auto by = worked == 1 ? shiftNow : channel == 0 ? shifts.left : shifts.right;
                phase = phaseWrapped(phase + by / rate_);
                line.push(wet);
                output.channel(channel)[frame] = static_cast<float>((x * (1.0 - mix) + wet * mix) * gain);
            }
        }
        passThrough(input, output, worked, channels, frames);
        readings_[decaleurReadingShift].store(static_cast<float>(shiftNow), std::memory_order_relaxed);
        readings_[decaleurReadingLeft].store(static_cast<float>(shifts.left), std::memory_order_relaxed);
        readings_[decaleurReadingRight].store(static_cast<float>(shifts.right), std::memory_order_relaxed);
    }

    [[nodiscard]] std::size_t readings(float* into, const std::size_t count) const noexcept override
    {
        const auto n = std::min<std::size_t>(count, readings_.size());
        for (std::size_t index = 0; index < n; ++index)
            into[index] = readings_[index].load(std::memory_order_relaxed);
        return n;
    }

private:
    double rate_ { 48'000.0 };
    std::array<Hilbert, 2> hilbert_ {};
    std::array<dsp::Delay, 2> lines_;
    std::array<double, 2> phases_ {};
    Cycle lfo_;
    double delay_ { -1.0 };
    std::array<std::atomic<float>, decaleurReadingCount> readings_ {};
};

// ---- Anneau -------------------------------------------------------------------------------------

// The polynomial residual of a step of 2 at a wrap of `t`, `dt` the phase a
// sample: what takes the aliasing out of a jump.
double stepResidual(double t, const double dt) noexcept
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt) {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}

// The same for a corner (a change of slope of 2 a sample).
double cornerResidual(double t, const double dt) noexcept
{
    if (t < dt) {
        t = t / dt - 1.0;
        return -t * t * t / 3.0;
    }
    if (t > 1.0 - dt) {
        t = (t - 1.0) / dt + 1.0;
        return t * t * t / 3.0;
    }
    return 0.0;
}

// Anneau's carrier band-limited: the waves of anneauWaveAt, their jumps and
// corners smoothed by their residuals.
double carrierAt(const int shape, const double t, const double dt) noexcept
{
    switch (shape) {
    case 1: {
        const auto u = phaseWrapped(t + 0.25);
        return 1.0 - 4.0 * std::abs(u - 0.5) + 4.0 * dt * (cornerResidual(u, dt) - cornerResidual(phaseWrapped(u + 0.5), dt));
    }
    case 2: return (t < 0.5 ? 1.0 : -1.0) + stepResidual(t, dt) - stepResidual(phaseWrapped(t + 0.5), dt);
    case 3: {
        const auto u = phaseWrapped(t + 0.5);
        return 2.0 * u - 1.0 - stepResidual(u, dt);
    }
    default: return std::sin(motionTwoPi * t);
    }
}

// What brings the wet sound back to the power of the sound coming in: the
// carrier's RMS for the ring (x c), its gain's for the amplitude
// (x (1 + c) / 2), from the shape's mean square.
double carrierMakeup(const int shape, const bool ring) noexcept
{
    const auto meanSquare = shape == 2 ? 1.0 : shape == 0 ? 0.5 : 1.0 / 3.0;
    return ring ? 1.0 / std::sqrt(meanSquare) : 2.0 / std::sqrt(1.0 + meanSquare);
}

// The sound (its lows cut, driven) times a carrier whose pitch an LFO swings
// in octaves; the right side's LFO may run ahead. Ring: x c, the original
// gone; amplitude: x (1 + c) / 2, the original kept. The wet sound is brought
// to the dry's power and the two mixed at equal power (they hardly
// correlate), so the mix does not dip the level.
class Anneau final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;

    void prepare(const ProcessSpec& spec) override
    {
        rate_ = std::max(1.0, spec.sampleRate);
        reset();
    }

    void reset() noexcept override
    {
        for (auto& filter : lowCut_)
            filter.reset();
        phases_ = {};
        lfo_.reset();
        designedCut_ = -1.0f;
        for (auto& reading : readings_)
            reading.store(0.0f, std::memory_order_relaxed);
    }

    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext& context) noexcept override
    {
        clearAudio(output);
        const auto channels = std::min(input.channelCount, output.channelCount);
        const auto frames = std::min(input.frameCount, output.frameCount);
        std::array<float, anneauParameterCount> values {};
        for (int index = 0; index < anneauParameterCount; ++index)
            values[static_cast<std::size_t>(index)] = value(static_cast<std::size_t>(index));
        const auto* v = values.data();
        if (values[anneauLowCut] != designedCut_) {
            const auto c = designEqBand(EqBandType::lowCut, clampedOf(values[anneauLowCut], 20.0, 2'000.0), 0.0, std::numbers::sqrt2 * 0.5, 0,
                                        0, rate_);
            for (auto& filter : lowCut_)
                filter.c = c;
            designedCut_ = values[anneauLowCut];
        }
        const auto shape = choiceOf(values[anneauShape], anneauShapeCount);
        const auto ring = choiceOf(values[anneauMode], anneauModeCount) == 0;
        const auto wave = choiceOf(values[anneauLfoShape], lfoWaveCount);
        lfo_.begin(clampedOf(values[anneauLfoRate], 0.01, 10.0), values[anneauSync], context, rate_);
        const auto drive = clampedOf(values[anneauDrive], 0.0, 24.0);
        const auto ahead = clampedOf(values[anneauStereo], 0.0, 1.0);
        const auto mix = clampedOf(values[anneauMix], 0.0, 1.0);
        const auto dryLevel = mix <= 0.0 ? 1.0 : std::cos(mix * std::numbers::pi * 0.5);
        const auto wetLevel = mix <= 0.0 ? 0.0 : std::sin(mix * std::numbers::pi * 0.5) * carrierMakeup(shape, ring);
        const auto gain = dsp::dbToGain(clampedOf(values[anneauOutput], -24.0, 24.0));
        const auto top = rate_ * 0.45;
        const auto worked = workedChannels(channels);
        std::array<double, 2> hz {};
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            const auto lfoPhase = lfo_.next();
            for (std::uint32_t channel = 0; channel < worked; ++channel) {
                const auto lfo = lfoWaveAt(wave, lfoPhase + (channel == 1 ? ahead : 0.0));
                hz[channel] = std::min(anneauFrequencyAt(v, lfo), top);
                const auto dt = hz[channel] / rate_;
                auto& phase = phases_[channel];
                const auto carrier = carrierAt(shape, phase, dt);
                phase = phaseWrapped(phase + dt);
                const auto x = static_cast<double>(input.channel(channel)[frame]);
                const auto driven = anneauDriveApplied(drive, lowCut_[channel](x + kAntiDenormal));
                const auto wet = ring ? driven * carrier : driven * (1.0 + carrier) * 0.5;
                output.channel(channel)[frame] = static_cast<float>((x * dryLevel + wet * wetLevel) * gain);
            }
        }
        passThrough(input, output, worked, channels, frames);
        readings_[anneauReadingLeftHz].store(static_cast<float>(hz[0]), std::memory_order_relaxed);
        readings_[anneauReadingRightHz].store(static_cast<float>(worked > 1 ? hz[1] : hz[0]), std::memory_order_relaxed);
    }

    [[nodiscard]] std::size_t readings(float* into, const std::size_t count) const noexcept override
    {
        const auto n = std::min<std::size_t>(count, readings_.size());
        for (std::size_t index = 0; index < n; ++index)
            into[index] = readings_[index].load(std::memory_order_relaxed);
        return n;
    }

private:
    double rate_ { 48'000.0 };
    std::array<dsp::Biquad, 2> lowCut_ {};
    std::array<double, 2> phases_ {};
    Cycle lfo_;
    float designedCut_ { -1.0f };
    std::array<std::atomic<float>, anneauReadingCount> readings_ {};
};

// ---- Rotor --------------------------------------------------------------------------------------

// The preamp before the rotors: a valve-like stage, a little lopsided, clean
// at 0 dB, its level about kept as it is pushed.
double valve(const double driveDb, const double x) noexcept
{
    if (driveDb <= 0.0)
        return x;
    constexpr auto bias = 0.2;
    const auto gain = std::pow(10.0, std::min(driveDb, 24.0) / 20.0);
    const auto bent = (std::tanh(gain * x + bias) - std::tanh(bias)) / (std::sqrt(gain) * (1.0 - std::tanh(bias) * std::tanh(bias)));
    return x + std::min(1.0, driveDb / 3.0) * (bent - x);
}

// A rotating speaker: the mono sound split at the crossover, the highs to a
// horn and the lows to a drum, each turning at its speed and getting there
// with its inertia. Each microphone hears a rotor nearer and farther as it
// turns (a delay swung: the Doppler), louder and brighter facing it; a few
// short reflections of the cabinet's room join. Its output lags by the
// sound's flight to the microphones (rotorBaseDelayMs): the dry sound mixed
// in waits as long.
class Rotor final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;

    void prepare(const ProcessSpec& spec) override
    {
        rate_ = std::max(1.0, spec.sampleRate);
        latency_ = static_cast<std::uint32_t>(std::lround(rotorBaseDelayMs * 0.001 * rate_));
        const auto longest = static_cast<std::size_t>((rotorBaseDelayMs + rotorHornSwingMs) * 0.001 * rate_) + 8;
        horn_.prepare(longest);
        drum_.prepare(longest);
        for (auto& line : room_)
            line.prepare(static_cast<std::size_t>(0.03 * rate_) + 8);
        for (auto& line : dry_)
            line.prepare(latency_ + 8);
        for (auto& pole : shade_)
            pole.design(2'500.0, rate_);
        blocker_.design(12.0, rate_);
        reset();
    }

    void reset() noexcept override
    {
        horn_.reset();
        drum_.reset();
        for (auto& line : room_)
            line.reset();
        for (auto& line : dry_)
            line.reset();
        for (auto& filter : low_)
            filter.reset();
        for (auto& filter : high_)
            filter.reset();
        for (auto& pole : shade_)
            pole.state = 0.0;
        blocker_.state = 0.0;
        designedCrossover_ = -1.0f;
        started_ = false;
        hornAngle_ = 0.0;
        drumAngle_ = 0.25;
        for (auto& reading : readings_)
            reading.store(0.0f, std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return latency_; }
    [[nodiscard]] std::uint32_t maximumLatencySamples() const noexcept override { return latency_; }

    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext&) noexcept override
    {
        clearAudio(output);
        const auto channels = std::min(input.channelCount, output.channelCount);
        const auto frames = std::min(input.frameCount, output.frameCount);
        if (channels == 0)
            return;
        std::array<float, rotorParameterCount> values {};
        for (int index = 0; index < rotorParameterCount; ++index)
            values[static_cast<std::size_t>(index)] = value(static_cast<std::size_t>(index));
        const auto* v = values.data();
        if (values[rotorCrossover] != designedCrossover_) {
            const auto hz = clampedOf(values[rotorCrossover], 400.0, 1'500.0);
            const auto lowPass = designEqBand(EqBandType::highCut, hz, 0.0, std::numbers::sqrt2 * 0.5, 0, 0, rate_);
            const auto highPass = designEqBand(EqBandType::lowCut, hz, 0.0, std::numbers::sqrt2 * 0.5, 0, 0, rate_);
            for (auto& filter : low_)
                filter.c = lowPass;
            for (auto& filter : high_)
                filter.c = highPass;
            designedCrossover_ = values[rotorCrossover];
        }
        const auto hornTarget = rotorHornTarget(v);
        const auto drumTarget = rotorDrumTarget(v);
        if (!started_) {
            hornHz_ = hornTarget;
            drumHz_ = drumTarget;
            started_ = true;
        }
        // Each rotor's approach: a share of the gap each sample, and at least
        // a little, so that it arrives (a braked rotor stops).
        const auto hornTime = rotorTimeConstantOf(v, false);
        const auto drumTime = rotorTimeConstantOf(v, true);
        const auto hornFollow = 1.0 - std::exp(-1.0 / (hornTime * rate_));
        const auto drumFollow = 1.0 - std::exp(-1.0 / (drumTime * rate_));
        const auto hornLeast = 0.05 / (hornTime * rate_);
        const auto drumLeast = 0.05 / (drumTime * rate_);
        const auto approach = [](double& hz, const double target, const double follow, const double least) noexcept {
            const auto gap = target - hz;
            auto step = gap * follow;
            if (std::abs(step) < least)
                step = std::clamp(gap, -least, least);
            hz += step;
        };
        const std::array<double, 2> mics { rotorMicAngle(v, false), rotorMicAngle(v, true) };
        const auto drive = clampedOf(values[rotorDrive], 0.0, 24.0);
        const auto doppler = clampedOf(values[rotorDoppler], 0.0, 1.0);
        const auto balance = clampedOf(values[rotorBalance], -1.0, 1.0);
        const auto hornLevel = std::min(1.0, 1.0 + balance);
        const auto drumLevel = std::min(1.0, 1.0 - balance);
        const auto room = clampedOf(values[rotorRoom], 0.0, 1.0);
        const auto mix = clampedOf(values[rotorMix], 0.0, 1.0);
        const auto gain = dsp::dbToGain(clampedOf(values[rotorOutput], -24.0, 24.0));
        const auto base = static_cast<double>(latency_);
        const auto hornSwing = doppler * rotorHornSwingMs * 0.001 * rate_;
        const auto drumSwing = doppler * rotorDrumSwingMs * 0.001 * rate_;
        const auto msToSamples = 0.001 * rate_;
        // The cabinet's reflections: when (ms), how loud, from which side.
        struct Reflection {
            double ms;
            double level;
            bool crossed;
        };
        constexpr std::array<Reflection, 5> reflections { { { 4.7, 0.42, false },
                                                            { 7.9, 0.36, true },
                                                            { 11.3, 0.28, false },
                                                            { 16.1, 0.22, true },
                                                            { 23.9, 0.15, false } } };
        // What the rotors' dips take on average, given back.
        constexpr auto makeup = 1.45;
        const auto summed = workedChannels(channels);
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            auto mono = 0.0;
            for (std::uint32_t channel = 0; channel < summed; ++channel)
                mono += static_cast<double>(input.channel(channel)[frame]);
            mono /= static_cast<double>(summed);
            if (!std::isfinite(mono))
                mono = 0.0;
            const auto driven = blocker_.highPass(valve(drive, mono) + kAntiDenormal);
            horn_.push(high_[1](high_[0](driven)));
            drum_.push(low_[1](low_[0](driven)));
            // The rotors head for their speeds, with their inertia.
            approach(hornHz_, hornTarget, hornFollow, hornLeast);
            approach(drumHz_, drumTarget, drumFollow, drumLeast);
            hornAngle_ = phaseWrapped(hornAngle_ + hornHz_ / rate_);
            drumAngle_ = phaseWrapped(drumAngle_ + drumHz_ / rate_);
            std::array<double, 2> heard {};
            for (std::size_t mic = 0; mic < 2; ++mic) {
                const auto towardsHorn = std::cos(motionTwoPi * hornAngle_ - mics[mic]);
                const auto facingHorn = 0.5 + 0.5 * towardsHorn;
                auto horn = horn_.read(base - hornSwing * towardsHorn);
                const auto dark = shade_[mic].lowPass(horn);
                horn = (dark + (horn - dark) * (0.35 + 0.65 * facingHorn)) * (1.0 - rotorHornDip * (1.0 - facingHorn));
                const auto towardsDrum = std::cos(motionTwoPi * drumAngle_ - mics[mic]);
                const auto drum = drum_.read(base - drumSwing * towardsDrum) * (1.0 - rotorDrumDip * (0.5 - 0.5 * towardsDrum));
                heard[mic] = (hornLevel * horn + drumLevel * drum) * makeup;
            }
            for (std::size_t mic = 0; mic < 2; ++mic)
                room_[mic].push(heard[mic]);
            if (room > 0.0) {
                std::array<double, 2> reflected {};
                for (std::size_t mic = 0; mic < 2; ++mic)
                    for (const auto& reflection : reflections)
                        reflected[mic] += reflection.level * room_[reflection.crossed ? 1 - mic : mic].read(reflection.ms * msToSamples);
                for (std::size_t mic = 0; mic < 2; ++mic)
                    heard[mic] += room * 0.6 * reflected[mic];
            }
            for (std::uint32_t channel = 0; channel < channels && channel < dry_.size(); ++channel) {
                auto& line = dry_[channel];
                line.push(static_cast<double>(input.channel(channel)[frame]));
                const auto dry = line.read(base);
                if (channel >= 2) {
                    output.channel(channel)[frame] = static_cast<float>(dry);
                    continue;
                }
                const auto wet = channels == 1 ? 0.5 * (heard[0] + heard[1]) : heard[channel];
                output.channel(channel)[frame] = static_cast<float>((dry * (1.0 - mix) + wet * mix) * gain);
            }
        }
        readings_[rotorReadingHornAngle].store(static_cast<float>(hornAngle_), std::memory_order_relaxed);
        readings_[rotorReadingDrumAngle].store(static_cast<float>(drumAngle_), std::memory_order_relaxed);
        readings_[rotorReadingHornHz].store(static_cast<float>(hornHz_), std::memory_order_relaxed);
        readings_[rotorReadingDrumHz].store(static_cast<float>(drumHz_), std::memory_order_relaxed);
    }

    [[nodiscard]] std::size_t readings(float* into, const std::size_t count) const noexcept override
    {
        const auto n = std::min<std::size_t>(count, readings_.size());
        for (std::size_t index = 0; index < n; ++index)
            into[index] = readings_[index].load(std::memory_order_relaxed);
        return n;
    }

private:
    double rate_ { 48'000.0 };
    std::uint32_t latency_ {};
    dsp::Delay horn_;
    dsp::Delay drum_;
    std::array<dsp::Delay, 2> room_;
    std::array<dsp::Delay, dsp::kChannels> dry_;
    std::array<dsp::Biquad, 2> low_ {};
    std::array<dsp::Biquad, 2> high_ {};
    std::array<dsp::OnePole, 2> shade_ {};
    dsp::OnePole blocker_ {};
    float designedCrossover_ { -1.0f };
    bool started_ {};
    double hornHz_ {};
    double drumHz_ {};
    double hornAngle_ {};
    double drumAngle_ {};
    std::array<std::atomic<float>, rotorReadingCount> readings_ {};
};

} // namespace

std::unique_ptr<EffectProcessor> makeMotionEffect(const EffectDescriptor& descriptor)
{
    switch (descriptor.kind) {
    case NativeDspNodeKind::pompe: return std::make_unique<Pompe>(descriptor);
    case NativeDspNodeKind::decaleur: return std::make_unique<Decaleur>(descriptor);
    case NativeDspNodeKind::anneau: return std::make_unique<Anneau>(descriptor);
    case NativeDspNodeKind::rotor: return std::make_unique<Rotor>(descriptor);
    default: return {};
    }
}

} // namespace cydaw::engine
