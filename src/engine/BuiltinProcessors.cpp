#include <cydaw/engine/BuiltinProcessors.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace cydaw::engine {

void PassthroughProcessor::prepare(const ProcessSpec&) {}
void PassthroughProcessor::reset() noexcept {}

void PassthroughProcessor::process(const AudioBlockView input,
                                   const AudioBlockView output,
                                   const ProcessContext&) noexcept
{
    copyAudio(output, input);
}

void MixerProcessor::prepare(const ProcessSpec&) {}
void MixerProcessor::reset() noexcept {}

void MixerProcessor::process(const AudioBlockView input,
                             const AudioBlockView output,
                             const ProcessContext&) noexcept
{
    copyAudio(output, input);
}

void MixerProcessor::processAudioBuses(
    const std::span<const AudioInputBusView> inputs,
    const AudioBlockView output, const ProcessContext&) noexcept
{
    clearAudio(output);
    for (const auto& input : inputs)
        addAudio(output, input.audio);
}

GainProcessor::GainProcessor(const float initialGain) noexcept
    : targetGain_(initialGain), currentGain_(initialGain)
{
}

void GainProcessor::setGainLinear(const float gain) noexcept
{
    targetGain_.store(std::max(0.0f, gain), std::memory_order_relaxed);
}

float GainProcessor::gainLinear() const noexcept
{
    return targetGain_.load(std::memory_order_relaxed);
}

void GainProcessor::prepare(const ProcessSpec&)
{
    currentGain_ = targetGain_.load(std::memory_order_relaxed);
}

void GainProcessor::reset() noexcept
{
    currentGain_ = targetGain_.load(std::memory_order_relaxed);
}

void GainProcessor::process(const AudioBlockView input,
                            const AudioBlockView output,
                            const ProcessContext&) noexcept
{
    clearAudio(output);
    const auto channels = std::min(input.channelCount, output.channelCount);
    const auto frames = std::min(input.frameCount, output.frameCount);
    const auto target = targetGain_.load(std::memory_order_relaxed);
    const auto increment = frames == 0 ? 0.0f : (target - currentGain_) / static_cast<float>(frames);

    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        currentGain_ += increment;
        for (std::uint32_t channel = 0; channel < channels; ++channel)
            output.channel(channel)[frame] = input.channel(channel)[frame] * currentGain_;
    }
    currentGain_ = target;
}

SineOscillatorProcessor::SineOscillatorProcessor(
    const float frequencyHz, const float waveform, const float level) noexcept
    : frequencyHz_(frequencyHz),
      waveform_(std::clamp(waveform, 0.0f, 4.0f)),
      level_(std::clamp(level, 0.0f, 1.0f))
{
}

void SineOscillatorProcessor::setWaveform(const float waveform) noexcept
{
    waveform_.store(std::clamp(waveform, 0.0f, 4.0f),
                    std::memory_order_relaxed);
}

void SineOscillatorProcessor::setLevel(const float level) noexcept
{
    level_.store(std::clamp(level, 0.0f, 1.0f),
                 std::memory_order_relaxed);
}

void SineOscillatorProcessor::setFrequency(const float frequencyHz) noexcept
{
    frequencyHz_.store(std::max(0.0f, frequencyHz), std::memory_order_relaxed);
}

float SineOscillatorProcessor::frequency() const noexcept
{
    return frequencyHz_.load(std::memory_order_relaxed);
}

float SineOscillatorProcessor::waveform() const noexcept
{
    return waveform_.load(std::memory_order_relaxed);
}

float SineOscillatorProcessor::level() const noexcept
{
    return level_.load(std::memory_order_relaxed);
}

void SineOscillatorProcessor::prepare(const ProcessSpec& spec)
{
    if (std::isfinite(spec.sampleRate) && spec.sampleRate > 0.0)
        sampleRate_ = spec.sampleRate;
    reset();
}

void SineOscillatorProcessor::reset() noexcept
{
    phase_ = 0.0;
}

void SineOscillatorProcessor::process(const AudioBlockView,
                                      const AudioBlockView output,
                                      const ProcessContext&) noexcept
{
    const auto nyquist = static_cast<float>(sampleRate_ * 0.5);
    const auto frequencyHz = std::min(frequencyHz_.load(std::memory_order_relaxed), nyquist);
    const auto phaseIncrement = static_cast<double>(frequencyHz) / sampleRate_;
    const auto waveform = static_cast<int>(std::lround(
        waveform_.load(std::memory_order_relaxed)));
    const auto level = level_.load(std::memory_order_relaxed);

    for (std::uint32_t frame = 0; frame < output.frameCount; ++frame) {
        float sample {};
        switch (waveform) {
        case 1:
            sample = static_cast<float>(1.0 - 4.0
                * std::abs(phase_ - 0.5));
            break;
        case 2:
            sample = static_cast<float>(2.0 * phase_ - 1.0);
            break;
        case 3:
            sample = phase_ < 0.5 ? 1.0f : -1.0f;
            break;
        case 4:
            sample = phase_ < 0.25 ? 1.0f : -1.0f;
            break;
        default:
            sample = static_cast<float>(
                std::sin(phase_ * 2.0 * std::numbers::pi));
            break;
        }
        sample *= level;
        for (std::uint32_t channel = 0; channel < output.channelCount; ++channel)
            output.channel(channel)[frame] = sample;
        phase_ += phaseIncrement;
        if (phase_ >= 1.0)
            phase_ -= std::floor(phase_);
    }
}

NoiseProcessor::NoiseProcessor(const float level) noexcept
    : level_(std::clamp(level, 0.0f, 1.0f))
{
}

void NoiseProcessor::setLevel(const float level) noexcept
{
    level_.store(std::clamp(level, 0.0f, 1.0f), std::memory_order_relaxed);
}

void NoiseProcessor::prepare(const ProcessSpec&) { reset(); }
void NoiseProcessor::reset() noexcept { state_ = 0x6d2b79f5u; }

void NoiseProcessor::process(const AudioBlockView,
                             const AudioBlockView output,
                             const ProcessContext&) noexcept
{
    const auto level = level_.load(std::memory_order_relaxed);
    for (std::uint32_t frame = 0; frame < output.frameCount; ++frame) {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        const auto sample = (static_cast<float>(state_) / 2147483648.0f - 1.0f) * level;
        for (std::uint32_t channel = 0; channel < output.channelCount; ++channel)
            output.channel(channel)[frame] = sample;
    }
}

const std::array<std::array<float,
                            WavetableOscillatorProcessor::frameLength>,
                 WavetableOscillatorProcessor::frameCount>&
WavetableOscillatorProcessor::table()
{
    // Built once, on first use: eight single-cycle frames, each carrying more
    // harmonics than the last with 1/n amplitudes. Frame 0 is a sine and
    // frame 7 approaches a saw, so reading between them opens the spectrum
    // rather than fading one shape into another. Additive construction keeps
    // every frame band-limited, so the morph never aliases.
    static const auto frames = [] {
        std::array<std::array<float, frameLength>, frameCount> built {};
        constexpr std::array<std::size_t, frameCount> harmonics {
            1, 2, 3, 5, 8, 14, 24, 40
        };
        for (std::size_t frame = 0; frame < frameCount; ++frame) {
            auto peak = 0.0f;
            for (std::size_t sample = 0; sample < frameLength; ++sample) {
                const auto phase = 2.0 * std::numbers::pi
                    * static_cast<double>(sample)
                    / static_cast<double>(frameLength);
                auto value = 0.0;
                for (std::size_t harmonic = 1;
                     harmonic <= harmonics[frame]; ++harmonic)
                    value += std::sin(phase * static_cast<double>(harmonic))
                             / static_cast<double>(harmonic);
                built[frame][sample] = static_cast<float>(value);
                peak = std::max(peak, std::abs(built[frame][sample]));
            }
            if (peak > 1.0e-6f)
                for (auto& sample : built[frame])
                    sample /= peak;
        }
        return built;
    }();
    return frames;
}

WavetableOscillatorProcessor::WavetableOscillatorProcessor(
    const float frequencyHz, const float position, const float level,
    const float detuneCents) noexcept
    : frequencyHz_(std::clamp(frequencyHz, 20.0f, 8'000.0f)),
      position_(std::clamp(position, 0.0f, 1.0f)),
      level_(std::clamp(level, 0.0f, 1.0f)),
      detuneCents_(std::clamp(detuneCents, 0.0f, 50.0f))
{
}

void WavetableOscillatorProcessor::setFrequencyHz(
    const float frequencyHz) noexcept
{
    frequencyHz_.store(std::clamp(frequencyHz, 20.0f, 8'000.0f),
                       std::memory_order_relaxed);
}

void WavetableOscillatorProcessor::setPosition(const float position) noexcept
{
    position_.store(std::clamp(position, 0.0f, 1.0f),
                    std::memory_order_relaxed);
}

void WavetableOscillatorProcessor::setLevel(const float level) noexcept
{
    level_.store(std::clamp(level, 0.0f, 1.0f), std::memory_order_relaxed);
}

void WavetableOscillatorProcessor::setDetuneCents(
    const float detuneCents) noexcept
{
    detuneCents_.store(std::clamp(detuneCents, 0.0f, 50.0f),
                       std::memory_order_relaxed);
}

void WavetableOscillatorProcessor::prepare(const ProcessSpec& spec)
{
    sampleRate_ = std::max(1.0, spec.sampleRate);
    (void) table();
    reset();
}

void WavetableOscillatorProcessor::reset() noexcept
{
    // The unison voices start together. A third of a cycle apart sounded
    // wide until the detune reached zero, at which point three identical
    // waves a third apart cancel every harmonic but the third: the note went
    // quiet and jumped a twelfth.
    phases_ = { 0.0, 0.0, 0.0 };
}

void WavetableOscillatorProcessor::setBank(WavetableBankRef bank) noexcept
{
    bank_ = std::move(bank);
}

float WavetableOscillatorProcessor::readTable(const float position,
                                              const double phase) noexcept
{
    return readTable(nullptr, position, phase);
}

float WavetableOscillatorProcessor::readTable(const WavetableBank* const bank,
                                              const float position,
                                              const double phase) noexcept
{
    static const auto& builtIn = table();
    const auto& frames = bank != nullptr ? bank->frames : builtIn;
    const auto scaled = std::clamp(position, 0.0f, 1.0f)
                        * static_cast<float>(frameCount - 1);
    const auto lower = static_cast<std::size_t>(scaled);
    const auto upper = std::min(lower + 1, frameCount - 1);
    const auto blend = scaled - static_cast<float>(lower);
    const auto wrapped = phase - std::floor(phase);
    const auto index = wrapped * static_cast<double>(frameLength);
    const auto first = static_cast<std::size_t>(index) % frameLength;
    const auto second = (first + 1) % frameLength;
    const auto fraction = static_cast<float>(index
                                             - std::floor(index));
    const auto lowerSample = frames[lower][first]
        + (frames[lower][second] - frames[lower][first]) * fraction;
    const auto upperSample = frames[upper][first]
        + (frames[upper][second] - frames[upper][first]) * fraction;
    return lowerSample + (upperSample - lowerSample) * blend;
}

float WavetableOscillatorProcessor::sample(const float position,
                                           const double phase) const noexcept
{
    return readTable(bank_.get(), position, phase);
}

void WavetableOscillatorProcessor::process(const AudioBlockView,
                                           const AudioBlockView output,
                                           const ProcessContext&) noexcept
{
    clearAudio(output);
    const auto frames = output.frameCount;
    const auto channels = output.channelCount;
    if (channels == 0 || frames == 0)
        return;
    const auto frequency = frequencyHz_.load(std::memory_order_relaxed);
    const auto position = position_.load(std::memory_order_relaxed);
    const auto level = level_.load(std::memory_order_relaxed);
    const auto detune = detuneCents_.load(std::memory_order_relaxed);
    // The detune cannot change inside a block, so neither can the ratios it
    // implies. Computing them per voice per sample cost three pows for every
    // frame the oscillator produced.
    const std::array<float, 3> offsets { -detune, 0.0f, detune };
    std::array<double, 3> increments {};
    for (std::size_t voice = 0; voice < offsets.size(); ++voice)
        increments[voice] = static_cast<double>(
                                frequency * std::pow(2.0f,
                                                     offsets[voice] / 1'200.0f))
                            / sampleRate_;
    // Three identical voices are one voice. Summing them and dividing by three
    // only holds while they differ.
    const std::size_t voices = detune > 0.01f ? 3u : 1u;
    const std::size_t firstVoice = voices == 1u ? 1u : 0u;
    const auto voiceLevel = level / static_cast<float>(voices);
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        auto value = 0.0f;
        for (auto voice = firstVoice; voice < firstVoice + voices; ++voice) {
            value += sample(position, phases_[voice]) * voiceLevel;
            phases_[voice] += increments[voice];
            if (phases_[voice] >= 1.0)
                phases_[voice] -= std::floor(phases_[voice]);
        }
        for (std::uint32_t channel = 0; channel < channels; ++channel)
            output.channel(channel)[frame] = value;
    }
}

StringResonatorProcessor::StringResonatorProcessor(
    const float frequencyHz, const float damping,
    const float decaySeconds) noexcept
    : frequencyHz_(std::clamp(frequencyHz, 20.0f, 4'000.0f)),
      damping_(std::clamp(damping, 0.0f, 1.0f)),
      decaySeconds_(std::clamp(decaySeconds, 0.05f, 20.0f))
{
}

void StringResonatorProcessor::setFrequencyHz(const float frequencyHz) noexcept
{
    frequencyHz_.store(std::clamp(frequencyHz, 20.0f, 4'000.0f),
                       std::memory_order_relaxed);
}

void StringResonatorProcessor::setDamping(const float damping) noexcept
{
    damping_.store(std::clamp(damping, 0.0f, 1.0f),
                   std::memory_order_relaxed);
}

void StringResonatorProcessor::setDecaySeconds(
    const float decaySeconds) noexcept
{
    decaySeconds_.store(std::clamp(decaySeconds, 0.05f, 20.0f),
                        std::memory_order_relaxed);
}

void StringResonatorProcessor::prepare(const ProcessSpec& spec)
{
    sampleRate_ = std::max(1.0, spec.sampleRate);
    // The line is sized for the lowest note the primitive accepts, so the
    // frequency can move without ever reallocating on the audio thread.
    const auto maximumLength = static_cast<std::size_t>(sampleRate_ / 20.0) + 4;
    loops_.assign(std::max<std::uint32_t>(1, spec.channelCount), {});
    for (auto& loop : loops_) {
        loop.line.assign(maximumLength, 0.0f);
        loop.writeIndex = 0;
        loop.lowpassState = 0.0f;
    }
}

void StringResonatorProcessor::reset() noexcept
{
    for (auto& loop : loops_) {
        std::fill(loop.line.begin(), loop.line.end(), 0.0f);
        loop.writeIndex = 0;
        loop.lowpassState = 0.0f;
        loop.dcState = 0.0f;
        loop.dcPrevious = 0.0f;
    }
}

void StringResonatorProcessor::process(const AudioBlockView input,
                                       const AudioBlockView output,
                                       const ProcessContext&) noexcept
{
    clearAudio(output);
    const auto channels = std::min({ input.channelCount, output.channelCount,
                                     static_cast<std::uint32_t>(loops_.size()) });
    const auto frames = std::min(input.frameCount, output.frameCount);
    const auto frequency = std::max(20.0f,
                                    frequencyHz_.load(std::memory_order_relaxed));
    const auto damping = damping_.load(std::memory_order_relaxed);
    const auto decay = decaySeconds_.load(std::memory_order_relaxed);
    const auto period = sampleRate_ / static_cast<double>(frequency);
    for (std::uint32_t channel = 0; channel < channels; ++channel) {
        auto& loop = loops_[channel];
        if (loop.line.empty())
            continue;
        const auto brightness = std::max(
            0.02f, 1.0f - std::clamp(damping, 0.0f, 0.98f));
        // The loop filter is part of the delay, and its own group delay is
        // what the string is tuned by. Leaving it out flattened the note by as
        // much as three semitones at the damped end of the knob, and rounding
        // the period to a whole sample detuned every note between two of them.
        const auto filterDelay = (1.0 - static_cast<double>(brightness))
                                 / static_cast<double>(brightness);
        const auto target = std::clamp(
            period - filterDelay, 2.0,
            static_cast<double>(loop.line.size() - 3));
        const auto length = static_cast<std::size_t>(target);
        const auto fraction = static_cast<float>(target
                                                 - static_cast<double>(length));
        // One round trip of the loop should lose the amount of level that
        // gives the requested decay time, so the feedback follows from the
        // period rather than being a number of its own.
        const auto roundTrips = static_cast<double>(decay) * sampleRate_
                                / static_cast<double>(length);
        const auto feedback = static_cast<float>(
            std::pow(0.001, 1.0 / std::max(1.0, roundTrips)));
        const auto* source = input.channel(channel);
        auto* destination = output.channel(channel);
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            const auto size = loop.line.size();
            const auto newer = (loop.writeIndex + size - length) % size;
            const auto older = (loop.writeIndex + size - length - 1) % size;
            const auto delayed = loop.line[newer] * (1.0f - fraction)
                                 + loop.line[older] * fraction;
            // The loop filter is what makes a string a string: every round
            // trip takes more off the top than the bottom, so the highs die
            // first and the fundamental rings on.
            loop.lowpassState += brightness * (delayed - loop.lowpassState);
            // Direct current is the one frequency that filter leaves alone, so
            // any offset in the excitation is fed back a thousand times and
            // carries the whole line away with it.
            const auto damped = loop.lowpassState * feedback;
            loop.dcState = damped - loop.dcPrevious + 0.995f * loop.dcState;
            loop.dcPrevious = damped;
            loop.line[loop.writeIndex] = loop.dcState + source[frame];
            loop.writeIndex = (loop.writeIndex + 1) % size;
            destination[frame] = delayed;
        }
    }
}

DriveProcessor::DriveProcessor(const float driveDb) noexcept
    : driveDb_(std::clamp(driveDb, 0.0f, 36.0f))
{
}

void DriveProcessor::setDriveDb(const float driveDb) noexcept
{
    driveDb_.store(std::clamp(driveDb, 0.0f, 36.0f), std::memory_order_relaxed);
}

float DriveProcessor::driveDb() const noexcept
{
    return driveDb_.load(std::memory_order_relaxed);
}

void DriveProcessor::prepare(const ProcessSpec&) {}
void DriveProcessor::reset() noexcept {}

void DriveProcessor::process(const AudioBlockView input,
                             const AudioBlockView output,
                             const ProcessContext&) noexcept
{
    clearAudio(output);
    const auto channels = std::min(input.channelCount, output.channelCount);
    const auto frames = std::min(input.frameCount, output.frameCount);
    const auto driveDb = driveDb_.load(std::memory_order_relaxed);
    const auto gain = std::pow(10.0f, driveDb / 20.0f);
    const auto normalisation = driveDb > 0.001f ? 1.0f / std::tanh(gain) : 1.0f;
    for (std::uint32_t channel = 0; channel < channels; ++channel)
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            const auto value = input.channel(channel)[frame];
            output.channel(channel)[frame] = driveDb > 0.001f
                                                  ? std::tanh(value * gain) * normalisation
                                                  : value;
        }
}

OnePoleLowPassProcessor::OnePoleLowPassProcessor(
    const float cutoffHz, const float resonance) noexcept
    : cutoffHz_(std::clamp(cutoffHz, 20.0f, 20'000.0f)),
      resonance_(std::clamp(resonance, 0.1f, 20.0f))
{
}

void OnePoleLowPassProcessor::setCutoffHz(const float cutoffHz) noexcept
{
    cutoffHz_.store(std::clamp(cutoffHz, 20.0f, 20'000.0f),
                    std::memory_order_relaxed);
}

float OnePoleLowPassProcessor::cutoffHz() const noexcept
{
    return cutoffHz_.load(std::memory_order_relaxed);
}

void OnePoleLowPassProcessor::setResonance(const float resonance) noexcept
{
    resonance_.store(std::clamp(resonance, 0.1f, 20.0f),
                     std::memory_order_relaxed);
}

float OnePoleLowPassProcessor::resonance() const noexcept
{
    return resonance_.load(std::memory_order_relaxed);
}

void OnePoleLowPassProcessor::prepare(const ProcessSpec& spec)
{
    sampleRate_ = std::max(1.0, spec.sampleRate);
    integrator1_.assign(spec.channelCount, 0.0f);
    integrator2_.assign(spec.channelCount, 0.0f);
}

void OnePoleLowPassProcessor::reset() noexcept
{
    std::fill(integrator1_.begin(), integrator1_.end(), 0.0f);
    std::fill(integrator2_.begin(), integrator2_.end(), 0.0f);
}

void OnePoleLowPassProcessor::process(const AudioBlockView input,
                                      const AudioBlockView output,
                                      const ProcessContext&) noexcept
{
    clearAudio(output);
    const auto channels = std::min({ input.channelCount, output.channelCount,
                                     static_cast<std::uint32_t>(integrator1_.size()),
                                     static_cast<std::uint32_t>(integrator2_.size()) });
    const auto frames = std::min(input.frameCount, output.frameCount);
    const auto cutoff = std::min(cutoffHz_.load(std::memory_order_relaxed),
                                 static_cast<float>(sampleRate_ * 0.45));
    const auto q = std::clamp(resonance_.load(std::memory_order_relaxed),
                              0.1f, 20.0f);
    // Topology-preserving state-variable filter. The coefficients are computed
    // once per block, while cutoff and Q remain lock-free atomics for live
    // Graph editing and host automation.
    const auto g = static_cast<float>(
        std::tan(std::numbers::pi * cutoff / sampleRate_));
    const auto damping = 1.0f / q;
    const auto a1 = 1.0f / (1.0f + g * (g + damping));
    const auto a2 = g * a1;
    const auto a3 = g * a2;
    for (std::uint32_t channel = 0; channel < channels; ++channel) {
        auto ic1eq = integrator1_[channel];
        auto ic2eq = integrator2_[channel];
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            const auto v3 = input.channel(channel)[frame] - ic2eq;
            const auto v1 = a1 * ic1eq + a2 * v3;
            const auto v2 = ic2eq + a2 * ic1eq + a3 * v3;
            ic1eq = 2.0f * v1 - ic1eq;
            ic2eq = 2.0f * v2 - ic2eq;
            output.channel(channel)[frame] = v2;
        }
        integrator1_[channel] = ic1eq;
        integrator2_[channel] = ic2eq;
    }
}

PeakingEqProcessor::PeakingEqProcessor(const float frequencyHz,
                                       const float gainDb,
                                       const float q) noexcept
    : frequencyHz_(std::clamp(frequencyHz, 20.0f, 20'000.0f)),
      gainDb_(std::clamp(gainDb, -30.0f, 30.0f)),
      q_(std::clamp(q, 0.1f, 18.0f))
{
}

void PeakingEqProcessor::setFrequencyHz(const float frequencyHz) noexcept
{
    frequencyHz_.store(std::clamp(frequencyHz, 20.0f, 20'000.0f),
                       std::memory_order_relaxed);
}

float PeakingEqProcessor::frequencyHz() const noexcept
{
    return frequencyHz_.load(std::memory_order_relaxed);
}

void PeakingEqProcessor::setGainDb(const float gainDb) noexcept
{
    gainDb_.store(std::clamp(gainDb, -30.0f, 30.0f),
                  std::memory_order_relaxed);
}

float PeakingEqProcessor::gainDb() const noexcept
{
    return gainDb_.load(std::memory_order_relaxed);
}

void PeakingEqProcessor::setQ(const float q) noexcept
{
    q_.store(std::clamp(q, 0.1f, 18.0f), std::memory_order_relaxed);
}

float PeakingEqProcessor::q() const noexcept
{
    return q_.load(std::memory_order_relaxed);
}

void PeakingEqProcessor::prepare(const ProcessSpec& spec)
{
    sampleRate_ = std::max(1.0, spec.sampleRate);
    state_.assign(std::max<std::uint32_t>(1, spec.channelCount), {});
}

void PeakingEqProcessor::reset() noexcept
{
    for (auto& state : state_)
        state = {};
}

void PeakingEqProcessor::process(const AudioBlockView input,
                                 const AudioBlockView output,
                                 const ProcessContext&) noexcept
{
    clearAudio(output);
    const auto channels = std::min({ input.channelCount, output.channelCount,
                                     static_cast<std::uint32_t>(state_.size()) });
    const auto frames = std::min(input.frameCount, output.frameCount);
    const auto frequency = std::min(
        frequencyHz_.load(std::memory_order_relaxed),
        static_cast<float>(sampleRate_ * 0.45));
    const auto amplitude = std::pow(
        10.0f, gainDb_.load(std::memory_order_relaxed) / 40.0f);
    const auto q = std::max(0.1f, q_.load(std::memory_order_relaxed));
    const auto omega = static_cast<float>(
        2.0 * std::numbers::pi * frequency / sampleRate_);
    const auto sine = std::sin(omega);
    const auto cosine = std::cos(omega);
    const auto alpha = sine / (2.0f * q);
    const auto a0 = 1.0f + alpha / amplitude;
    const auto inverseA0 = 1.0f / (std::abs(a0) < 1.0e-9f ? 1.0e-9f : a0);
    const auto b0 = (1.0f + alpha * amplitude) * inverseA0;
    const auto b1 = (-2.0f * cosine) * inverseA0;
    const auto b2 = (1.0f - alpha * amplitude) * inverseA0;
    const auto a1 = (-2.0f * cosine) * inverseA0;
    const auto a2 = (1.0f - alpha / amplitude) * inverseA0;
    for (std::uint32_t channel = 0; channel < channels; ++channel) {
        auto state = state_[channel];
        const auto* source = input.channel(channel);
        auto* destination = output.channel(channel);
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            const auto x = source[frame];
            const auto y = b0 * x + b1 * state.x1 + b2 * state.x2
                           - a1 * state.y1 - a2 * state.y2;
            state.x2 = state.x1;
            state.x1 = x;
            state.y2 = state.y1;
            state.y1 = y;
            destination[frame] = y;
        }
        state_[channel] = state;
    }
}

OnePoleHighPassProcessor::OnePoleHighPassProcessor(const float cutoffHz) noexcept
    : cutoffHz_(std::clamp(cutoffHz, 10.0f, 20'000.0f))
{
}

void OnePoleHighPassProcessor::setCutoffHz(const float cutoffHz) noexcept
{
    cutoffHz_.store(std::clamp(cutoffHz, 10.0f, 20'000.0f),
                    std::memory_order_relaxed);
}

float OnePoleHighPassProcessor::cutoffHz() const noexcept
{
    return cutoffHz_.load(std::memory_order_relaxed);
}

void OnePoleHighPassProcessor::prepare(const ProcessSpec& spec)
{
    sampleRate_ = std::max(1.0, spec.sampleRate);
    lowState_.assign(spec.channelCount, 0.0f);
}

void OnePoleHighPassProcessor::reset() noexcept
{
    std::fill(lowState_.begin(), lowState_.end(), 0.0f);
}

void OnePoleHighPassProcessor::process(const AudioBlockView input,
                                       const AudioBlockView output,
                                       const ProcessContext&) noexcept
{
    clearAudio(output);
    const auto channels = std::min({ input.channelCount, output.channelCount,
                                     static_cast<std::uint32_t>(lowState_.size()) });
    const auto frames = std::min(input.frameCount, output.frameCount);
    const auto cutoff = std::min(cutoffHz_.load(std::memory_order_relaxed),
                                 static_cast<float>(sampleRate_ * 0.45));
    const auto coefficient = static_cast<float>(
        1.0 - std::exp(-2.0 * std::numbers::pi * cutoff / sampleRate_));
    for (std::uint32_t channel = 0; channel < channels; ++channel) {
        auto low = lowState_[channel];
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            const auto value = input.channel(channel)[frame];
            low += coefficient * (value - low);
            output.channel(channel)[frame] = value - low;
        }
        lowState_[channel] = low;
    }
}

HardClipProcessor::HardClipProcessor(const float threshold) noexcept
    : threshold_(std::clamp(threshold, 0.001f, 1.0f))
{
}

void HardClipProcessor::setThreshold(const float threshold) noexcept
{
    threshold_.store(std::clamp(threshold, 0.001f, 1.0f),
                     std::memory_order_relaxed);
}

float HardClipProcessor::threshold() const noexcept
{
    return threshold_.load(std::memory_order_relaxed);
}

void HardClipProcessor::prepare(const ProcessSpec&) {}
void HardClipProcessor::reset() noexcept {}

void HardClipProcessor::process(const AudioBlockView input,
                                const AudioBlockView output,
                                const ProcessContext&) noexcept
{
    clearAudio(output);
    const auto channels = std::min(input.channelCount, output.channelCount);
    const auto frames = std::min(input.frameCount, output.frameCount);
    const auto threshold = threshold_.load(std::memory_order_relaxed);
    for (std::uint32_t channel = 0; channel < channels; ++channel)
        for (std::uint32_t frame = 0; frame < frames; ++frame)
            output.channel(channel)[frame] = std::clamp(
                input.channel(channel)[frame], -threshold, threshold);
}

StereoWidthProcessor::StereoWidthProcessor(const float width) noexcept
    : width_(std::clamp(width, 0.0f, 2.0f))
{
}

void StereoWidthProcessor::setWidth(const float width) noexcept
{
    width_.store(std::clamp(width, 0.0f, 2.0f),
                 std::memory_order_relaxed);
}

float StereoWidthProcessor::width() const noexcept
{
    return width_.load(std::memory_order_relaxed);
}

void StereoWidthProcessor::prepare(const ProcessSpec&) {}
void StereoWidthProcessor::reset() noexcept {}

void StereoWidthProcessor::process(const AudioBlockView input,
                                   const AudioBlockView output,
                                   const ProcessContext&) noexcept
{
    clearAudio(output);
    const auto channels = std::min(input.channelCount, output.channelCount);
    const auto frames = std::min(input.frameCount, output.frameCount);
    if (channels < 2) {
        copyAudio(output, input);
        return;
    }
    const auto width = width_.load(std::memory_order_relaxed);
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        const auto mid = 0.5f * (input.channel(0)[frame] + input.channel(1)[frame]);
        const auto side = 0.5f * (input.channel(0)[frame] - input.channel(1)[frame]) * width;
        output.channel(0)[frame] = mid + side;
        output.channel(1)[frame] = mid - side;
        for (std::uint32_t channel = 2; channel < channels; ++channel)
            output.channel(channel)[frame] = input.channel(channel)[frame];
    }
}

PanProcessor::PanProcessor(const float pan) noexcept
    : pan_(std::clamp(pan, -1.0f, 1.0f))
{
}

void PanProcessor::setPan(const float pan) noexcept
{
    pan_.store(std::clamp(pan, -1.0f, 1.0f),
               std::memory_order_relaxed);
}

float PanProcessor::pan() const noexcept
{
    return pan_.load(std::memory_order_relaxed);
}

void PanProcessor::prepare(const ProcessSpec&) {}
void PanProcessor::reset() noexcept {}

void PanProcessor::process(const AudioBlockView input,
                           const AudioBlockView output,
                           const ProcessContext&) noexcept
{
    clearAudio(output);
    const auto channels = std::min(input.channelCount, output.channelCount);
    const auto frames = std::min(input.frameCount, output.frameCount);
    if (channels < 2) {
        copyAudio(output, input);
        return;
    }
    const auto position = pan_.load(std::memory_order_relaxed);
    const auto leftGain = position > 0.0f ? 1.0f - position : 1.0f;
    const auto rightGain = position < 0.0f ? 1.0f + position : 1.0f;
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        output.channel(0)[frame] = input.channel(0)[frame] * leftGain;
        output.channel(1)[frame] = input.channel(1)[frame] * rightGain;
        for (std::uint32_t channel = 2; channel < channels; ++channel)
            output.channel(channel)[frame] = input.channel(channel)[frame];
    }
}

EnvelopeFollowerProcessor::EnvelopeFollowerProcessor(
    const float attackSeconds, const float releaseSeconds) noexcept
    : attackSeconds_(std::clamp(attackSeconds, 0.0001f, 1.0f)),
      releaseSeconds_(std::clamp(releaseSeconds, 0.001f, 5.0f))
{
}

void EnvelopeFollowerProcessor::setAttackSeconds(const float seconds) noexcept
{
    attackSeconds_.store(std::clamp(seconds, 0.0001f, 1.0f),
                         std::memory_order_relaxed);
}

void EnvelopeFollowerProcessor::setReleaseSeconds(const float seconds) noexcept
{
    releaseSeconds_.store(std::clamp(seconds, 0.001f, 5.0f),
                          std::memory_order_relaxed);
}

float EnvelopeFollowerProcessor::envelope() const noexcept
{
    return publishedEnvelope_.load(std::memory_order_relaxed);
}

void EnvelopeFollowerProcessor::prepare(const ProcessSpec& spec)
{
    sampleRate_ = std::max(1.0, spec.sampleRate);
    reset();
}

void EnvelopeFollowerProcessor::reset() noexcept
{
    envelope_ = 0.0f;
    publishedEnvelope_.store(0.0f, std::memory_order_relaxed);
}

void EnvelopeFollowerProcessor::process(
    const AudioBlockView input, const AudioBlockView output,
    const ProcessContext&) noexcept
{
    copyAudio(output, input);
    const auto attackSamples = std::max(
        1.0, static_cast<double>(attackSeconds_.load(
                 std::memory_order_relaxed)) * sampleRate_);
    const auto releaseSamples = std::max(
        1.0, static_cast<double>(releaseSeconds_.load(
                 std::memory_order_relaxed)) * sampleRate_);
    const auto attack = static_cast<float>(
        std::exp(-1.0 / attackSamples));
    const auto release = static_cast<float>(
        std::exp(-1.0 / releaseSamples));
    for (std::uint32_t frame = 0; frame < input.frameCount; ++frame) {
        auto magnitude = 0.0f;
        for (std::uint32_t channel = 0; channel < input.channelCount; ++channel)
            magnitude = std::max(magnitude,
                                 std::abs(input.channel(channel)[frame]));
        const auto coefficient = magnitude > envelope_ ? attack : release;
        envelope_ = magnitude + coefficient * (envelope_ - magnitude);
    }
    publishedEnvelope_.store(envelope_, std::memory_order_relaxed);
}

void DcBlockerProcessor::prepare(const ProcessSpec& spec)
{
    previousInput_.assign(spec.channelCount, 0.0f);
    previousOutput_.assign(spec.channelCount, 0.0f);
}

void DcBlockerProcessor::reset() noexcept
{
    std::fill(previousInput_.begin(), previousInput_.end(), 0.0f);
    std::fill(previousOutput_.begin(), previousOutput_.end(), 0.0f);
}

void DcBlockerProcessor::process(const AudioBlockView input,
                                 const AudioBlockView output,
                                 const ProcessContext&) noexcept
{
    clearAudio(output);
    const auto channels = std::min({ input.channelCount, output.channelCount,
                                     static_cast<std::uint32_t>(previousInput_.size()) });
    const auto frames = std::min(input.frameCount, output.frameCount);
    constexpr auto pole = 0.995f;
    for (std::uint32_t channel = 0; channel < channels; ++channel) {
        auto x1 = previousInput_[channel];
        auto y1 = previousOutput_[channel];
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            const auto x = input.channel(channel)[frame];
            const auto y = x - x1 + pole * y1;
            output.channel(channel)[frame] = y;
            x1 = x;
            y1 = y;
        }
        previousInput_[channel] = x1;
        previousOutput_[channel] = y1;
    }
}

DelayProcessor::DelayProcessor(const float delayMilliseconds,
                               const float feedback,
                               const float mix) noexcept
    : delayMilliseconds_(std::clamp(delayMilliseconds, 1.0f, 2'000.0f)),
      feedback_(std::clamp(feedback, 0.0f, 0.94f)),
      mix_(std::clamp(mix, 0.0f, 1.0f))
{
}

void DelayProcessor::setDelayMilliseconds(const float delayMilliseconds) noexcept
{
    delayMilliseconds_.store(std::clamp(delayMilliseconds, 1.0f, 2'000.0f),
                             std::memory_order_relaxed);
}

void DelayProcessor::setFeedback(const float feedback) noexcept
{
    feedback_.store(std::clamp(feedback, 0.0f, 0.94f),
                    std::memory_order_relaxed);
}

void DelayProcessor::setMix(const float mix) noexcept
{
    mix_.store(std::clamp(mix, 0.0f, 1.0f), std::memory_order_relaxed);
}

void DelayProcessor::prepare(const ProcessSpec& spec)
{
    sampleRate_ = std::max(1.0, spec.sampleRate);
    const auto maximumDelaySamples = std::max<std::uint32_t>(
        2, static_cast<std::uint32_t>(std::ceil(sampleRate_ * 2.0)) + 1);
    delayBuffer_.prepare(spec.channelCount, maximumDelaySamples);
    reset();
}

void DelayProcessor::reset() noexcept
{
    delayBuffer_.clear(delayBuffer_.maximumFrameCount());
    writePosition_ = 0;
}

void DelayProcessor::process(const AudioBlockView input,
                             const AudioBlockView output,
                             const ProcessContext&) noexcept
{
    clearAudio(output);
    if (delayBuffer_.maximumFrameCount() == 0)
        return;
    const auto ring = delayBuffer_.view(delayBuffer_.maximumFrameCount());
    const auto channels = std::min({ input.channelCount, output.channelCount,
                                     ring.channelCount });
    const auto frames = std::min(input.frameCount, output.frameCount);
    const auto delaySamples = std::clamp<std::uint32_t>(
        static_cast<std::uint32_t>(
            sampleRate_ * delayMilliseconds_.load(std::memory_order_relaxed)
            / 1'000.0),
        1, ring.frameCount - 1);
    const auto feedback = feedback_.load(std::memory_order_relaxed);
    const auto mix = mix_.load(std::memory_order_relaxed);
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        const auto readPosition = writePosition_ >= delaySamples
                                      ? writePosition_ - delaySamples
                                      : ring.frameCount + writePosition_ - delaySamples;
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            const auto dry = input.channel(channel)[frame];
            const auto delayed = ring.channel(channel)[readPosition];
            ring.channel(channel)[writePosition_] = dry + delayed * feedback;
            output.channel(channel)[frame] = dry * (1.0f - mix) + delayed * mix;
        }
        if (++writePosition_ >= ring.frameCount)
            writePosition_ = 0;
    }
}

ReverbProcessor::ReverbProcessor(const float roomSize, const float mix) noexcept
    : roomSize_(std::clamp(roomSize, 0.0f, 0.95f)),
      mix_(std::clamp(mix, 0.0f, 1.0f))
{
}

void ReverbProcessor::setRoomSize(const float roomSize) noexcept
{
    roomSize_.store(std::clamp(roomSize, 0.0f, 0.95f),
                    std::memory_order_relaxed);
}

void ReverbProcessor::setMix(const float mix) noexcept
{
    mix_.store(std::clamp(mix, 0.0f, 1.0f), std::memory_order_relaxed);
}

void ReverbProcessor::prepare(const ProcessSpec& spec)
{
    static constexpr std::uint32_t delays[] { 1557, 1617, 1491, 1422 };
    combBuffers_.clear();
    combPositions_.assign(std::size(delays), 0);
    combBuffers_.reserve(std::size(delays));
    const auto scale = std::max(0.25, spec.sampleRate / 44'100.0);
    for (const auto delay : delays)
        combBuffers_.emplace_back(spec.channelCount,
                                  std::max<std::uint32_t>(1, static_cast<std::uint32_t>(delay * scale)));
    dampingState_.assign(std::size(delays) * spec.channelCount, 0.0f);
    reset();
}

void ReverbProcessor::reset() noexcept
{
    for (auto& buffer : combBuffers_)
        buffer.clear(buffer.maximumFrameCount());
    std::fill(combPositions_.begin(), combPositions_.end(), 0);
    std::fill(dampingState_.begin(), dampingState_.end(), 0.0f);
}

void ReverbProcessor::process(const AudioBlockView input,
                              const AudioBlockView output,
                              const ProcessContext&) noexcept
{
    clearAudio(output);
    const auto channels = std::min(input.channelCount, output.channelCount);
    const auto frames = std::min(input.frameCount, output.frameCount);
    if (combBuffers_.empty()) {
        copyAudio(output, input);
        return;
    }
    const auto roomSize = roomSize_.load(std::memory_order_relaxed);
    const auto mix = mix_.load(std::memory_order_relaxed);
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            const auto dry = input.channel(channel)[frame];
            auto wet = 0.0f;
            for (std::size_t comb = 0; comb < combBuffers_.size(); ++comb) {
                auto ring = combBuffers_[comb].view(combBuffers_[comb].maximumFrameCount());
                const auto position = combPositions_[comb];
                const auto delayed = ring.channel(channel)[position];
                auto& damped = dampingState_[comb * channels + channel];
                damped += 0.32f * (delayed - damped);
                ring.channel(channel)[position] = dry + damped * roomSize;
                wet += delayed;
            }
            wet /= static_cast<float>(combBuffers_.size());
            output.channel(channel)[frame] = dry * (1.0f - mix) + wet * mix;
        }
        for (std::size_t comb = 0; comb < combBuffers_.size(); ++comb)
            if (++combPositions_[comb] >= combBuffers_[comb].maximumFrameCount())
                combPositions_[comb] = 0;
    }
}

CompressorProcessor::CompressorProcessor(const float thresholdDb,
                                         const float ratio) noexcept
    : thresholdDb_(std::clamp(thresholdDb, -60.0f, 0.0f)),
      ratio_(std::max(1.0f, ratio))
{
}

void CompressorProcessor::setThresholdDb(const float thresholdDb) noexcept
{
    thresholdDb_.store(std::clamp(thresholdDb, -60.0f, 0.0f),
                       std::memory_order_relaxed);
}

void CompressorProcessor::setRatio(const float ratio) noexcept
{
    ratio_.store(std::clamp(ratio, 1.0f, 20.0f),
                 std::memory_order_relaxed);
}

void CompressorProcessor::prepare(const ProcessSpec& spec)
{
    sampleRate_ = std::max(1.0, spec.sampleRate);
    reset();
}

void CompressorProcessor::reset() noexcept { envelope_ = 0.0f; }

void CompressorProcessor::process(const AudioBlockView input,
                                  const AudioBlockView output,
                                  const ProcessContext&) noexcept
{
    compress(input, input, output);
}

void CompressorProcessor::processAudioBuses(const std::span<const AudioInputBusView> inputs, const AudioBlockView output,
                                            const ProcessContext&) noexcept
{
    const AudioInputBusView* main = nullptr;
    const AudioInputBusView* sidechain = nullptr;
    for (const auto& bus : inputs)
        if (bus.portId == sidechainPort)
            sidechain = &bus;
        else if (main == nullptr)
            main = &bus;
    if (main == nullptr) {
        clearAudio(output);
        return;
    }
    compress(main->audio, sidechain != nullptr ? sidechain->audio : main->audio, output);
}

void CompressorProcessor::compress(const AudioBlockView input, const AudioBlockView detector,
                                   const AudioBlockView output) noexcept
{
    clearAudio(output);
    const auto channels = std::min(input.channelCount, output.channelCount);
    const auto frames = std::min({ input.frameCount, output.frameCount, detector.frameCount });
    const auto heard = detector.channelCount;
    const auto attack = static_cast<float>(std::exp(-1.0 / (0.008 * sampleRate_)));
    const auto release = static_cast<float>(std::exp(-1.0 / (0.090 * sampleRate_)));
    const auto thresholdDb = thresholdDb_.load(std::memory_order_relaxed);
    const auto ratio = ratio_.load(std::memory_order_relaxed);
    auto deepestReductionDb = 0.0f;
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        auto peak = 0.0f;
        for (std::uint32_t channel = 0; channel < heard; ++channel)
            peak = std::max(peak, std::abs(detector.channel(channel)[frame]));
        const auto coefficient = peak > envelope_ ? attack : release;
        envelope_ = coefficient * envelope_ + (1.0f - coefficient) * peak;
        const auto levelDb = 20.0f * std::log10(std::max(envelope_, 1.0e-9f));
        const auto over = std::max(0.0f, levelDb - thresholdDb);
        const auto reductionDb = -over * (1.0f - 1.0f / ratio);
        const auto gain = std::pow(10.0f, reductionDb / 20.0f);
        deepestReductionDb = std::min(deepestReductionDb, reductionDb);
        for (std::uint32_t channel = 0; channel < channels; ++channel)
            output.channel(channel)[frame] = input.channel(channel)[frame] * gain;
    }
    gainReductionDb_.store(deepestReductionDb, std::memory_order_relaxed);
}

float CompressorProcessor::gainReductionDb() const noexcept
{
    return gainReductionDb_.load(std::memory_order_relaxed);
}

ToneProcessor::ToneProcessor(const float amount) noexcept
    : amount_(std::clamp(amount, -1.0f, 1.0f))
{
}

void ToneProcessor::setAmount(const float amount) noexcept
{
    amount_.store(std::clamp(amount, -1.0f, 1.0f),
                  std::memory_order_relaxed);
}

void ToneProcessor::prepare(const ProcessSpec& spec)
{
    sampleRate_ = std::max(1.0, spec.sampleRate);
    lowState_.assign(spec.channelCount, 0.0f);
}

void ToneProcessor::reset() noexcept
{
    std::fill(lowState_.begin(), lowState_.end(), 0.0f);
}

void ToneProcessor::process(const AudioBlockView input,
                            const AudioBlockView output,
                            const ProcessContext&) noexcept
{
    clearAudio(output);
    const auto channels = std::min({ input.channelCount, output.channelCount,
                                     static_cast<std::uint32_t>(lowState_.size()) });
    const auto frames = std::min(input.frameCount, output.frameCount);
    const auto coefficient = static_cast<float>(
        1.0 - std::exp(-2.0 * std::numbers::pi * 900.0 / sampleRate_));
    const auto amount = amount_.load(std::memory_order_relaxed);
    for (std::uint32_t channel = 0; channel < channels; ++channel) {
        auto low = lowState_[channel];
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            const auto value = input.channel(channel)[frame];
            low += coefficient * (value - low);
            output.channel(channel)[frame] = value + amount * (value - low);
        }
        lowState_[channel] = low;
    }
}

void VocalStripProcessor::prepare(const ProcessSpec& spec)
{
    sampleRate_ = std::max(1.0, spec.sampleRate);
    lowState_.assign(spec.channelCount, 0.0f);
    reset();
}

void VocalStripProcessor::reset() noexcept
{
    std::fill(lowState_.begin(), lowState_.end(), 0.0f);
    envelope_ = 0.0f;
}

void VocalStripProcessor::process(const AudioBlockView input,
                                  const AudioBlockView output,
                                  const ProcessContext&) noexcept
{
    clearAudio(output);
    const auto channels = std::min({ input.channelCount, output.channelCount,
                                     static_cast<std::uint32_t>(lowState_.size()) });
    const auto frames = std::min(input.frameCount, output.frameCount);
    const auto highPassCoefficient = static_cast<float>(
        1.0 - std::exp(-2.0 * std::numbers::pi * 95.0 / sampleRate_));
    const auto envelopeCoefficient = static_cast<float>(
        std::exp(-1.0 / (0.025 * sampleRate_)));
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        auto peak = 0.0f;
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            const auto inputValue = input.channel(channel)[frame];
            lowState_[channel] += highPassCoefficient * (inputValue - lowState_[channel]);
            peak = std::max(peak, std::abs(inputValue - lowState_[channel]));
        }
        envelope_ = envelopeCoefficient * envelope_ + (1.0f - envelopeCoefficient) * peak;
        const auto gain = envelope_ > 0.18f
                              ? std::sqrt(0.18f / std::max(envelope_, 1.0e-6f))
                              : 1.0f;
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            const auto highPassed = input.channel(channel)[frame] - lowState_[channel];
            output.channel(channel)[frame] = std::tanh(highPassed * gain * 1.35f) / std::tanh(1.35f);
        }
    }
}

} // namespace cydaw::engine
