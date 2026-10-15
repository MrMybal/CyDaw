#include <cydaw/engine/VoiceEffects.h>

#include <cydaw/engine/AudioBuffer.h>
#include <cydaw/engine/EffectCatalogue.h>
#include <cydaw/engine/PitchTracker.h>
#include <cydaw/engine/VoiceDesign.h>

#include "DspParts.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <span>
#include <vector>

namespace cydaw::engine {

namespace {

using namespace dsp;

// Under this a filter's state is let go to zero, so a silence never decays
// into denormals.
constexpr double kTiny = 1.0e-25;

void flush(Biquad& filter) noexcept
{
    if (std::abs(filter.z1) < kTiny)
        filter.z1 = 0.0;
    if (std::abs(filter.z2) < kTiny)
        filter.z2 = 0.0;
}

double flushed(const double value) noexcept
{
    return std::abs(value) < kTiny ? 0.0 : value;
}

// White noise, even between -1 and 1.
struct Noise {
    std::uint32_t state { 0x2468ace1u };
    double operator()() noexcept
    {
        state = state * 1'664'525u + 1'013'904'223u;
        return static_cast<double>(state >> 8) / static_cast<double>(1u << 23) - 1.0;
    }
};

// A saw between -1 and 1 whose jumps are rounded off over a sample either
// side (polyBLEP), so its high harmonics do not fold back.
struct Saw {
    double phase {};
    double next(const double increment) noexcept
    {
        phase += increment;
        if (phase >= 1.0)
            phase -= std::floor(phase);
        auto value = 2.0 * phase - 1.0;
        if (phase < increment) {
            const auto t = phase / increment;
            value -= t + t - t * t - 1.0;
        } else if (phase > 1.0 - increment) {
            const auto t = (phase - 1.0) / increment;
            value -= t * t + t + t + 1.0;
        }
        return value;
    }
};

// ---- Vocodeur ------------------------------------------------------------------------------------

// The voice (its own input) split into bands, each band's level put on the
// same band of the carrier. Each carrier band is first brought to an even
// level (divided by a slow follower of its own, 18 dB at most), so the
// output's spectrum is the voice's whatever the carrier's tilt, while the
// carrier's own movement (a pluck, the two saws' beating) still shows; the
// tone tilts it after.
class Vocoder final : public EffectProcessor, public MultiBusAudioProcessor {
public:
    using EffectProcessor::EffectProcessor;

    void prepare(const ProcessSpec& spec) override
    {
        rate_ = std::max(1.0, spec.sampleRate);
        channels_ = std::min<std::size_t>(kChannels, std::max<std::uint32_t>(1, spec.channelCount));
        main_.prepare(std::max<std::uint32_t>(1, spec.channelCount), std::max<std::uint32_t>(1, spec.maximumBlockSize));
        tracker_.prepare(rate_, 60.0, 1'100.0);
        for (int stage = 0; stage < 2; ++stage) {
            sibilance_[static_cast<std::size_t>(stage)].c
                = designEqBand(EqBandType::lowCut, std::min(voice::vocoderSibilanceHz, rate_ * 0.4), 0.0, 0.7071, 1, stage, rate_);
            hiss_[static_cast<std::size_t>(stage)].c = designEqBand(EqBandType::lowCut, std::min(4'000.0, rate_ * 0.35), 0.0, 0.7071, 1, stage, rate_);
        }
        designed_.fill(-1.0e9f);
        activeBands_ = 0;
        reset();
    }

    void reset() noexcept override
    {
        for (auto& band : bands_)
            clearBand(band);
        for (auto& filter : sibilance_)
            filter.reset();
        for (auto& filter : hiss_)
            filter.reset();
        tracker_.reset();
        saws_[0].phase = 0.0;
        saws_[1].phase = 0.37;
        followLog_ = 0.0;
        targetLog_ = 0.0;
        following_ = false;
        sibilantPower_ = voicePower_ = 0.0;
        hissy_ = 0.0;
        carrierPower_ = 0.0;
        keyPower_ = 0.0;
        keyWeight_ = 0.0;
        countdown_ = 0;
        for (auto& reading : readings_)
            reading.store(voice::vocoderFloorDb, std::memory_order_relaxed);
        readings_[voice::vocoderReadingPitch].store(0.0f, std::memory_order_relaxed);
        readings_[voice::vocoderReadingBandCount].store(0.0f, std::memory_order_relaxed);
    }

    [[nodiscard]] std::size_t readings(float* into, const std::size_t count) const noexcept override
    {
        const auto bands = static_cast<std::size_t>(std::clamp(static_cast<int>(readings_[voice::vocoderReadingBandCount].load(
                                                                    std::memory_order_relaxed)),
                                                                0, voice::vocoderMaximumBands));
        const auto shown = std::min(count, static_cast<std::size_t>(voice::vocoderReadingEnvelopes) + bands);
        for (std::size_t index = 0; index < shown; ++index)
            into[index] = readings_[index].load(std::memory_order_relaxed);
        return shown;
    }

    // A carrier wired to the sidechain input (a synth, a pad) is what the
    // voice shapes when the carrier is external.
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

    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext&) noexcept override
    {
        using namespace voice;
        clearAudio(output);
        const auto channels = std::min({ static_cast<std::size_t>(input.channelCount), static_cast<std::size_t>(output.channelCount),
                                         channels_ });
        const auto frames = std::min(input.frameCount, output.frameCount);
        std::array<float, vocoderParameterCount> values {};
        for (int index = 0; index < vocoderParameterCount; ++index)
            values[static_cast<std::size_t>(index)] = value(static_cast<std::size_t>(index));
        design(values);
        const auto count = static_cast<std::size_t>(activeBands_);
        const auto carrier = voiceChoiceOf(values[vocoderCarrier], vocoderCarrierCount);
        const auto mix = static_cast<double>(std::clamp(values[vocoderMix], 0.0f, 1.0f));
        const auto out = dbToGain(std::clamp(static_cast<double>(values[vocoderOutput]), -24.0, 24.0)) * kMakeup * widthMakeup_;
        // The followers work on power: half the times, so they fall as an
        // amplitude follower of the times set would.
        const auto attack = smoothing(0.5 * std::clamp(static_cast<double>(values[vocoderAttack]), 0.5, 100.0), rate_);
        const auto release = smoothing(0.5 * std::clamp(static_cast<double>(values[vocoderRelease]), 5.0, 1'000.0), rate_);
        const auto gateDb = std::clamp(static_cast<double>(values[vocoderGate]), -90.0, -20.0);
        const auto unvoiced = std::clamp(static_cast<double>(values[vocoderUnvoiced]), 0.0, 1.0);
        // The note's saws, detuned apart.
        const auto noteHz = PitchTracker::hzOf(std::clamp(static_cast<double>(values[vocoderPitch]), 0.0, 127.0));
        const auto spread = std::pow(2.0, vocoderDetuneCentsOf(values[vocoderDetune]) / 1'200.0);
        const auto glide = smoothing(vocoderGlideMs, rate_);
        const auto tenMs = smoothing(10.0, rate_);
        const auto twentyMs = smoothing(20.0, rate_);
        const auto fiftyMs = smoothing(50.0, rate_);
        const auto carrierFollow = smoothing(120.0, rate_);
        const auto gateStep = 1.0 - smoothing(5.0, rate_ / static_cast<double>(kUpdate));
        const auto keyed = carrier == vocoderExternalCarrier && key_.data != nullptr && key_.channelCount > 0 && key_.frameCount >= frames;
        if (carrier != vocoderFollowCarrier)
            following_ = false;
        if (carrier == vocoderFollowCarrier && !following_) {
            // Until the voice gives a pitch, the note's.
            targetLog_ = followLog_ = std::log2(noteHz);
            following_ = true;
            tracker_.reset();
        }
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            auto voiceSample = static_cast<double>(input.channel(0)[frame]);
            if (channels > 1)
                voiceSample = 0.5 * (voiceSample + static_cast<double>(input.channel(1)[frame]));
            // How much of the voice is hiss (an S, an F): its share above 5 kHz.
            const auto high = sibilance_[1](sibilance_[0](voiceSample));
            sibilantPower_ = high * high + tenMs * (sibilantPower_ - high * high);
            voicePower_ = voiceSample * voiceSample + tenMs * (voicePower_ - voiceSample * voiceSample);
            if (countdown_ <= 0) {
                countdown_ = kUpdate;
                const auto share = sibilantPower_ / (voicePower_ + 1.0e-12);
                const auto wanted = std::clamp((share - 0.08) / 0.4, 0.0, 1.0);
                hissy_ += (wanted - hissy_) * 0.3;
                for (std::size_t index = 0; index < count; ++index) {
                    auto& band = bands_[index];
                    const auto levelDb = 10.0 * std::log10(band.modPower + 1.0e-30);
                    const auto open = std::clamp((levelDb - gateDb) / 6.0 + 0.5, 0.0, 1.0);
                    band.gate += (open - band.gate) * gateStep;
                }
            }
            --countdown_;
            // The carrier.
            auto source = 0.0;
            switch (carrier) {
            case vocoderNoiseCarrier: source = noise_() * 0.5; break;
            case vocoderSawCarrier: source = 0.5 * (saws_[0].next(increment(noteHz * spread)) + saws_[1].next(increment(noteHz / spread))); break;
            case vocoderFollowCarrier: {
                if (tracker_.push(static_cast<float>(voiceSample)) && tracker_.frequency() > 0.0f)
                    targetLog_ = std::log2(static_cast<double>(tracker_.frequency()));
                followLog_ = targetLog_ + glide * (followLog_ - targetLog_);
                const auto hz = std::exp2(followLog_);
                source = 0.5 * (saws_[0].next(increment(hz * spread)) + saws_[1].next(increment(hz / spread)));
                break;
            }
            default: {
                auto key = 0.0;
                if (keyed) {
                    key = static_cast<double>(key_.channel(0)[frame]);
                    if (key_.channelCount > 1)
                        key = 0.5 * (key + static_cast<double>(key_.channel(1)[frame]));
                }
                keyPower_ = key * key + fiftyMs * (keyPower_ - key * key);
                // Silent or not wired: noise, faded in and out.
                const auto wanted = keyPower_ > 1.0e-8 ? 1.0 : 0.0;
                keyWeight_ = wanted + twentyMs * (keyWeight_ - wanted);
                source = key * keyWeight_ + noise_() * 0.3 * (1.0 - keyWeight_);
                break;
            }
            }
            carrierPower_ = source * source + twentyMs * (carrierPower_ - source * source);
            // Hiss on the carrier while the voice hisses, so an S sounds as one.
            if (unvoiced > 0.0 && hissy_ > 0.0)
                source += unvoiced * hissy_ * 4.0 * std::sqrt(carrierPower_) * hiss_[1](hiss_[0](noise_()));
            const auto floor = 1.0e-12 + 1.0e-4 * carrierPower_;
            auto left = 0.0;
            auto right = 0.0;
            auto centre = 0.0;
            for (std::size_t index = 0; index < count; ++index) {
                auto& band = bands_[index];
                const auto m = band.mod[1](band.mod[0](voiceSample));
                // Its power over about a period of the band first, so the
                // attack and release that follow ride the level, not each wave.
                band.modMean = m * m + band.meanFollow * (band.modMean - m * m);
                const auto heard = band.modMean;
                band.modPower = heard + (heard > band.modPower ? attack : release) * (band.modPower - heard);
                const auto c = band.car[1](band.car[0](source));
                band.carPower = c * c + carrierFollow * (band.carPower - c * c);
                const auto gain = std::min(kMaximumBandGain, std::sqrt(band.modPower / (band.carPower + floor)));
                const auto y = c * gain * band.tone * band.gate;
                centre += y;
                left += y * band.left;
                right += y * band.right;
            }
            for (std::size_t channel = 0; channel < channels; ++channel) {
                const auto dry = static_cast<double>(input.channel(static_cast<std::uint32_t>(channel))[frame]);
                const auto wet = channels == 1 ? centre : channel == 0 ? left : channel == 1 ? right : dry;
                output.channel(static_cast<std::uint32_t>(channel))[frame]
                    = static_cast<float>(channel < 2 ? dry * (1.0 - mix) + wet * out * mix : dry);
            }
        }
        for (std::size_t channel = channels; channel < std::min<std::size_t>(input.channelCount, output.channelCount); ++channel)
            std::copy_n(input.channel(static_cast<std::uint32_t>(channel)), frames, output.channel(static_cast<std::uint32_t>(channel)));
        // No denormals in a long silence.
        for (std::size_t index = 0; index < count; ++index) {
            auto& band = bands_[index];
            for (auto& filter : band.mod)
                flush(filter);
            for (auto& filter : band.car)
                flush(filter);
            band.modMean = flushed(band.modMean);
            band.modPower = flushed(band.modPower);
            band.carPower = flushed(band.carPower);
        }
        for (auto& filter : sibilance_)
            flush(filter);
        for (auto& filter : hiss_)
            flush(filter);
        sibilantPower_ = flushed(sibilantPower_);
        voicePower_ = flushed(voicePower_);
        carrierPower_ = flushed(carrierPower_);
        keyPower_ = flushed(keyPower_);
        // What the display shows.
        auto pitch = 0.0;
        if (carrier == vocoderSawCarrier)
            pitch = noteHz;
        else if (carrier == vocoderFollowCarrier)
            pitch = std::exp2(followLog_);
        readings_[vocoderReadingPitch].store(static_cast<float>(pitch), std::memory_order_relaxed);
        readings_[vocoderReadingBandCount].store(static_cast<float>(count), std::memory_order_relaxed);
        for (std::size_t index = 0; index < count; ++index)
            readings_[vocoderReadingEnvelopes + index].store(
                static_cast<float>(std::max(static_cast<double>(vocoderFloorDb), 10.0 * std::log10(bands_[index].modPower + 1.0e-30))),
                std::memory_order_relaxed);
    }

private:
    struct Band {
        std::array<Biquad, 2> mod {};
        std::array<Biquad, 2> car {};
        double modMean {};
        double modPower {};
        // The voice's power is first averaged over about a period of the band.
        double meanFollow {};
        double carPower {};
        double tone { 1.0 };
        double gate {};
        double left { 1.0 };
        double right { 1.0 };
    };
    // Settings the bands are designed from.
    enum Designed : std::size_t { designedBands, designedLow, designedHigh, designedWidth, designedFormant, designedTone, designedStereo,
                                  designedCount };
    static constexpr int kUpdate = 16;
    static constexpr double kMaximumBandGain = 8.0;

    static void clearBand(Band& band) noexcept
    {
        for (auto& filter : band.mod)
            filter.reset();
        for (auto& filter : band.car)
            filter.reset();
        band.modMean = band.modPower = band.carPower = 0.0;
        band.gate = 0.0;
    }

    [[nodiscard]] double increment(const double hz) const noexcept
    {
        return std::clamp(hz / rate_, 1.0e-6, 0.45);
    }

    // The bands again when what places them moved: the analysis bank, the
    // carrier's (shifted by the formant), each band's tone, side and the
    // level that brings the whole back to the voice's.
    void design(const std::array<float, voice::vocoderParameterCount>& values) noexcept
    {
        using namespace voice;
        const std::array<float, designedCount> wanted { static_cast<float>(vocoderBandCountOf(values[vocoderBands])), values[vocoderLowBand],
                                                        values[vocoderHighBand], values[vocoderBandwidth], values[vocoderFormant],
                                                        values[vocoderTone], values[vocoderStereo] };
        if (wanted == designed_)
            return;
        const auto count = vocoderBandCountOf(values[vocoderBands]);
        const auto top = rate_ * 0.45;
        const auto lowest = std::min(vocoderLowestOf(values[vocoderLowBand], values[vocoderHighBand]), top / 2.0);
        const auto highest = std::min(vocoderHighestOf(values[vocoderLowBand], values[vocoderHighBand]), top);
        const auto q = vocoderBandQ(count, lowest, std::max(highest, lowest * 1.5), values[vocoderBandwidth]);
        const auto shift = vocoderFormantRatioOf(values[vocoderFormant]);
        const auto stereo = std::clamp(static_cast<double>(values[vocoderStereo]), 0.0, 1.0);
        // Bands coming in start from silence.
        for (auto index = activeBands_; index < count; ++index)
            clearBand(bands_[static_cast<std::size_t>(index)]);
        for (int index = 0; index < count; ++index) {
            auto& band = bands_[static_cast<std::size_t>(index)];
            const auto centre = std::min(vocoderBandHz(index, count, lowest, std::max(highest, lowest * 1.5)), top);
            const auto modulator = designEqBand(EqBandType::bandPass, centre, 0.0, q, 0, 0, rate_);
            band.mod[0].c = band.mod[1].c = modulator;
            band.meanFollow = smoothing(1'000.0 * std::clamp(1.0 / centre, 0.0005, 0.01), rate_);
            const auto shifted = std::clamp(centre * shift, 20.0, top);
            const auto carried = designEqBand(EqBandType::bandPass, shifted, 0.0, q, 0, 0, rate_);
            band.car[0].c = band.car[1].c = carried;
            // Every other band upside down: neighbouring bands meet out of
            // phase, so flipped they sum flat instead of hollowing out
            // between their centres.
            band.tone = dbToGain(vocoderToneDb(centre, values[vocoderTone])) * (index % 2 == 0 ? 1.0 : -1.0);
            // Odd bands (the first, the third…) to the left, even ones right.
            const auto pan = (index % 2 == 0 ? -1.0 : 1.0) * stereo;
            const auto angle = (pan + 1.0) * kPi * 0.25;
            band.left = std::cos(angle) * std::numbers::sqrt2;
            band.right = std::sin(angle) * std::numbers::sqrt2;
        }
        // Narrow bands catch less of the voice between them, wide ones
        // cancel a little where they overlap: measured, made up.
        const auto width = std::clamp(static_cast<double>(values[vocoderBandwidth]), 0.1, 4.0);
        widthMakeup_ = dbToGain(width < 1.0 ? -8.5 * std::log10(width) : 7.5 * std::log10(width));
        activeBands_ = count;
        designed_ = wanted;
    }

    // Each band comes out at the voice's level in it, so the whole comes
    // out near the voice's whatever the bands; measured on speech-like
    // sounds, this brings it to the same level (within about 3 dB over the
    // carriers and the bands' count and width).
    static constexpr double kMakeup = 0.71;

    double rate_ { 48'000.0 };
    std::size_t channels_ { 2 };
    AudioBuffer main_;
    AudioBlockView key_ {};
    std::array<Band, voice::vocoderMaximumBands> bands_ {};
    int activeBands_ {};
    double widthMakeup_ { 1.0 };
    std::array<float, designedCount> designed_ {};
    std::array<Biquad, 2> sibilance_ {};
    std::array<Biquad, 2> hiss_ {};
    PitchTracker tracker_;
    std::array<Saw, 2> saws_ {};
    Noise noise_ {};
    double followLog_ {};
    double targetLog_ {};
    bool following_ {};
    double sibilantPower_ {};
    double voicePower_ {};
    double hissy_ {};
    double carrierPower_ {};
    double keyPower_ {};
    double keyWeight_ {};
    int countdown_ {};
    std::array<std::atomic<float>, voice::vocoderReadingCount> readings_ {};
};

// ---- Dé-Esseur -----------------------------------------------------------------------------------

// A detector (a high-pass or a band at the frequency) followed in dB; what
// is over the threshold, through a soft knee and never past the range, is
// taken: by a shelf or a bell at the frequency (designed again every 16
// samples), or by the whole sound's gain.
class DeEsser final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;

    void prepare(const ProcessSpec& spec) override
    {
        rate_ = std::max(1.0, spec.sampleRate);
        channels_ = std::min<std::size_t>(2, std::max<std::uint32_t>(1, spec.channelCount));
        designed_.fill(-1.0e9f);
        reset();
    }

    void reset() noexcept override
    {
        for (auto& state : states_) {
            for (auto& filter : state.detector)
                filter.reset();
            state.dynamic.reset();
            state.dynamic.c = {};
            state.envelope = 0.0;
            state.reduction = 0.0;
            state.shaped = 0.0;
            state.gain = 1.0;
            state.step = 0.0;
        }
        countdown_ = 0;
        readings_[voice::deEsserReadingReduction].store(0.0f, std::memory_order_relaxed);
        readings_[voice::deEsserReadingLevel].store(-120.0f, std::memory_order_relaxed);
    }

    [[nodiscard]] std::size_t readings(float* into, const std::size_t count) const noexcept override
    {
        const auto shown = std::min(count, readings_.size());
        for (std::size_t index = 0; index < shown; ++index)
            into[index] = readings_[index].load(std::memory_order_relaxed);
        return shown;
    }

    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext&) noexcept override
    {
        using namespace voice;
        clearAudio(output);
        const auto channels = std::min({ static_cast<std::size_t>(input.channelCount), static_cast<std::size_t>(output.channelCount),
                                         channels_ });
        const auto frames = std::min(input.frameCount, output.frameCount);
        std::array<float, deEsserParameterCount> values {};
        for (int index = 0; index < deEsserParameterCount; ++index)
            values[static_cast<std::size_t>(index)] = value(static_cast<std::size_t>(index));
        const auto type = voiceChoiceOf(values[deEsserDetectorType], deEsserDetectorCount);
        const auto frequency = std::clamp(static_cast<double>(values[deEsserFrequency]), 500.0, rate_ * 0.42);
        const auto octaves = std::clamp(static_cast<double>(values[deEsserBandwidth]), 0.1, 4.0);
        const std::array<float, 3> wanted { static_cast<float>(type), static_cast<float>(frequency), static_cast<float>(octaves) };
        const auto redesign = wanted != designed_;
        if (redesign) {
            for (int stage = 0; stage < 2; ++stage) {
                const auto coefficients = deEsserDetectorStage(type, frequency, octaves, stage, rate_);
                for (auto& state : states_)
                    state.detector[static_cast<std::size_t>(stage)].c = coefficients;
            }
            for (auto& state : states_)
                state.shaped = -1.0;
            designed_ = wanted;
        }
        const auto attack = smoothing(std::clamp(static_cast<double>(values[deEsserAttack]), 0.1, 20.0), rate_);
        const auto release = smoothing(std::clamp(static_cast<double>(values[deEsserRelease]), 10.0, 300.0), rate_);
        const auto threshold = std::clamp(static_cast<double>(values[deEsserThreshold]), -60.0, 0.0);
        const auto range = std::clamp(static_cast<double>(values[deEsserRange]), 0.0, 24.0);
        const auto link = std::clamp(static_cast<double>(values[deEsserLink]), 0.0, 1.0);
        const auto whole = voiceChoiceOf(values[deEsserMode], deEsserModeCount) == deEsserWholeMode;
        const auto listen = values[deEsserListen] >= 0.5f;
        const auto out = dbToGain(std::clamp(static_cast<double>(values[deEsserOutput]), -24.0, 24.0));
        auto mostTaken = 0.0;
        auto loudest = -120.0;
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            if (countdown_ <= 0) {
                countdown_ = deEsserUpdateSamples;
                // Each channel's level, the louder one's pulling as linked.
                auto linked = -120.0;
                std::array<double, 2> levels { -120.0, -120.0 };
                for (std::size_t channel = 0; channel < channels; ++channel) {
                    levels[channel] = gainToDb(states_[channel].envelope);
                    linked = std::max(linked, levels[channel]);
                }
                for (std::size_t channel = 0; channel < channels; ++channel) {
                    auto& state = states_[channel];
                    const auto level = levels[channel] + (linked - levels[channel]) * link;
                    state.reduction = deEsserReductionOf(level, threshold, range);
                    // The whole sound by a gain gliding over the next 16
                    // samples, its filter kept flat; or the filter alone.
                    const auto shaped = whole ? 0.0 : state.reduction;
                    if (std::abs(shaped - state.shaped) > 0.01) {
                        state.dynamic.c = deEsserDynamicFilter(type, frequency, octaves, shaped, rate_);
                        state.shaped = shaped;
                    }
                    if (whole) {
                        state.step = (dbToGain(-state.reduction) - state.gain) / deEsserUpdateSamples;
                    } else {
                        state.gain = 1.0;
                        state.step = 0.0;
                    }
                    mostTaken = std::max(mostTaken, state.reduction);
                }
                loudest = std::max(loudest, linked);
            }
            --countdown_;
            for (std::size_t channel = 0; channel < channels; ++channel) {
                auto& state = states_[channel];
                const auto x = static_cast<double>(input.channel(static_cast<std::uint32_t>(channel))[frame]);
                const auto heard = state.detector[1](state.detector[0](x));
                const auto rectified = std::abs(heard);
                state.envelope = rectified + (rectified > state.envelope ? attack : release) * (state.envelope - rectified);
                // The dynamic filter keeps running (flat in whole mode), so a
                // switch back starts from a settled state.
                auto y = state.dynamic(x);
                state.gain += state.step;
                if (listen)
                    y = heard;
                else if (whole)
                    y = x * state.gain * out;
                else
                    y *= out;
                output.channel(static_cast<std::uint32_t>(channel))[frame] = static_cast<float>(y);
            }
        }
        for (std::size_t channel = channels; channel < std::min<std::size_t>(input.channelCount, output.channelCount); ++channel)
            std::copy_n(input.channel(static_cast<std::uint32_t>(channel)), frames, output.channel(static_cast<std::uint32_t>(channel)));
        for (auto& state : states_) {
            for (auto& filter : state.detector)
                flush(filter);
            flush(state.dynamic);
            state.envelope = flushed(state.envelope);
        }
        readings_[deEsserReadingReduction].store(static_cast<float>(mostTaken), std::memory_order_relaxed);
        readings_[deEsserReadingLevel].store(static_cast<float>(std::max(-120.0, loudest)), std::memory_order_relaxed);
    }

private:
    struct State {
        std::array<Biquad, 2> detector {};
        Biquad dynamic {};
        double envelope {};
        double reduction {};
        // The reduction the dynamic filter was last designed for.
        double shaped {};
        double gain { 1.0 };
        double step {};
    };

    double rate_ { 48'000.0 };
    std::size_t channels_ { 2 };
    std::array<State, 2> states_ {};
    std::array<float, 3> designed_ {};
    int countdown_ {};
    std::array<std::atomic<float>, voice::deEsserReadingCount> readings_ {};
};

// ---- Accordeur -----------------------------------------------------------------------------------

// The pitch tracker on the sound, its answer refined at the full rate (the
// difference function around the period it found, a parabola through its
// dip) and steadied a little; the sound passes untouched, or not at all.
class Tuner final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;

    void prepare(const ProcessSpec& spec) override
    {
        rate_ = std::max(1.0, spec.sampleRate);
        tracker_.prepare(rate_, 40.0, 1'600.0);
        // Two of the longest periods and a window of them, as a power of two.
        std::size_t size = 1;
        while (size < static_cast<std::size_t>(rate_ / 40.0) * 4 + 64)
            size <<= 1;
        history_.assign(size, 0.0f);
        mask_ = size - 1;
        reset();
    }

    void reset() noexcept override
    {
        tracker_.reset();
        std::fill(history_.begin(), history_.end(), 0.0f);
        write_ = 0;
        power_ = 0.0;
        pitchLog_ = 0.0;
        hasPitch_ = false;
        silentFor_ = 0.0;
        confidence_ = 0.0f;
        for (auto& reading : readings_)
            reading.store(0.0f, std::memory_order_relaxed);
        readings_[voice::tunerReadingNote].store(-1.0f, std::memory_order_relaxed);
        readings_[voice::tunerReadingLevel].store(-120.0f, std::memory_order_relaxed);
    }

    [[nodiscard]] std::size_t readings(float* into, const std::size_t count) const noexcept override
    {
        const auto shown = std::min(count, readings_.size());
        for (std::size_t index = 0; index < shown; ++index)
            into[index] = readings_[index].load(std::memory_order_relaxed);
        return shown;
    }

    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext&) noexcept override
    {
        using namespace voice;
        clearAudio(output);
        const auto channels = std::min(input.channelCount, output.channelCount);
        const auto frames = std::min(input.frameCount, output.frameCount);
        if (channels == 0 || frames == 0)
            return;
        if (value(static_cast<std::size_t>(tunerMute)) < 0.5f)
            for (std::uint32_t channel = 0; channel < channels; ++channel)
                std::copy_n(input.channel(channel), frames, output.channel(channel));
        const auto levelFollow = smoothing(100.0, rate_);
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            auto mono = input.channel(0)[frame];
            if (channels > 1)
                mono = 0.5f * (mono + input.channel(1)[frame]);
            if (!std::isfinite(mono))
                mono = 0.0f;
            const auto squared = static_cast<double>(mono) * mono;
            power_ = squared + levelFollow * (power_ - squared);
            history_[write_] = mono;
            write_ = (write_ + 1) & mask_;
            if (tracker_.push(mono))
                answer();
        }
        power_ = flushed(power_);
        const auto reference = std::clamp(static_cast<double>(value(static_cast<std::size_t>(tunerReference))), 400.0, 480.0);
        const auto hz = hasPitch_ ? std::exp2(pitchLog_) : 0.0;
        const auto note = tunerNoteOf(hz, reference);
        readings_[tunerReadingHz].store(static_cast<float>(hz), std::memory_order_relaxed);
        readings_[tunerReadingNote].store(static_cast<float>(note.note), std::memory_order_relaxed);
        readings_[tunerReadingCents].store(static_cast<float>(note.cents), std::memory_order_relaxed);
        readings_[tunerReadingLevel].store(static_cast<float>(std::max(-120.0, 10.0 * std::log10(power_ + 1.0e-30))),
                                           std::memory_order_relaxed);
        readings_[tunerReadingConfidence].store(hasPitch_ ? confidence_ : 0.0f, std::memory_order_relaxed);
    }

private:
    // A new answer of the tracker.
    void answer() noexcept
    {
        const auto found = static_cast<double>(tracker_.frequency());
        if (found <= 0.0) {
            silentFor_ += static_cast<double>(tracker_.hopSamples()) / rate_;
            if (silentFor_ > voice::tunerHoldSeconds)
                hasPitch_ = false;
            return;
        }
        silentFor_ = 0.0;
        confidence_ = tracker_.confidence();
        const auto heard = std::log2(refined(found));
        // Close to what was shown: eased towards it; a new note: at once.
        if (hasPitch_ && std::abs(heard - pitchLog_) < 0.3 / 12.0)
            pitchLog_ += (heard - pitchLog_) * 0.4;
        else
            pitchLog_ = heard;
        hasPitch_ = true;
    }

    // The period found, to a fraction of a sample at the full rate: the
    // difference function (the window against itself one period later) is
    // smallest at the period and a parabola about it.
    [[nodiscard]] double refined(const double hz) const noexcept
    {
        const auto period = rate_ / hz;
        const auto centre = static_cast<int>(std::lround(period));
        const auto size = static_cast<int>(history_.size());
        const auto window = std::min(std::max(1'024, 2 * centre), size - centre - 8);
        if (centre < 4 || window < 64)
            return hz;
        // The newest samples: x[i] against x[i + tau], i from the oldest used.
        const auto start = static_cast<std::size_t>(write_) + history_.size() - static_cast<std::size_t>(window + centre + 4);
        std::array<double, 5> difference {};
        for (int offset = -2; offset <= 2; ++offset) {
            const auto tau = static_cast<std::size_t>(centre + offset);
            auto sum = 0.0;
            for (std::size_t i = 0; i < static_cast<std::size_t>(window); ++i) {
                const auto d = static_cast<double>(history_[(start + i) & mask_]) - history_[(start + i + tau) & mask_];
                sum += d * d;
            }
            difference[static_cast<std::size_t>(offset + 2)] = sum;
        }
        auto best = std::size_t { 1 };
        for (std::size_t index = 2; index < 4; ++index)
            if (difference[index] < difference[best])
                best = index;
        const auto a = difference[best - 1];
        const auto b = difference[best];
        const auto c = difference[best + 1];
        const auto bend = a - 2.0 * b + c;
        if (!(bend > 0.0))
            return hz;
        const auto exact = static_cast<double>(centre) + static_cast<double>(best) - 2.0 + 0.5 * (a - c) / bend;
        // A refinement, never a different answer.
        if (std::abs(exact - period) > period * 0.03)
            return hz;
        return rate_ / exact;
    }

    double rate_ { 48'000.0 };
    PitchTracker tracker_;
    std::vector<float> history_;
    std::size_t mask_ {};
    std::size_t write_ {};
    double power_ {};
    double pitchLog_ {};
    bool hasPitch_ {};
    double silentFor_ {};
    float confidence_ {};
    std::array<std::atomic<float>, voice::tunerReadingCount> readings_ {};
};

} // namespace

std::unique_ptr<EffectProcessor> makeVoiceEffect(const EffectDescriptor& descriptor)
{
    switch (descriptor.kind) {
    case NativeDspNodeKind::vocoder: return std::make_unique<Vocoder>(descriptor);
    case NativeDspNodeKind::deEsser: return std::make_unique<DeEsser>(descriptor);
    case NativeDspNodeKind::tuner: return std::make_unique<Tuner>(descriptor);
    default: return {};
    }
}

} // namespace cydaw::engine
