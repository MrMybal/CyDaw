#include <cydaw/engine/RhythmEffects.h>

#include <cydaw/engine/AudioBuffer.h>
#include <cydaw/engine/EffectCatalogue.h>
#include <cydaw/engine/RhythmDesign.h>

#include "DspParts.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace cydaw::engine {

namespace {

using namespace dsp;

// A random number source light enough for the audio thread (xorshift).
struct Random {
    std::uint32_t state { 0x9e3779b9u };
    // In [0, 1).
    double unit() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<double>(state >> 8) * (1.0 / 16'777'216.0);
    }
    double bipolar() noexcept { return unit() * 2.0 - 1.0; }
};

// What decays towards the smallest floats becomes zero (they are slow).
double flushed(const double x) noexcept
{
    return std::abs(x) < 1.0e-20 ? 0.0 : x;
}

// Up to 1 untouched, above it a soft knee that never passes 2: what keeps a
// feedback bounded whatever comes round.
double softLimit(const double x) noexcept
{
    const auto magnitude = std::abs(x);
    if (magnitude <= 1.0)
        return x;
    const auto over = magnitude - 1.0;
    return std::copysign(1.0 + over / (1.0 + over), x);
}

bool transportRuns(const ProcessContext& context) noexcept
{
    return context.transport.state == PlaybackState::playing || context.transport.state == PlaybackState::recording;
}

std::size_t powerOfTwoAbove(const double samples) noexcept
{
    std::size_t size = 1;
    while (static_cast<double>(size) < samples)
        size <<= 1;
    return size;
}

// A low-pass of two poles a channel (Butterworth), for the wet sounds.
struct LowPass {
    std::array<Biquad, 2> sides {};
    void design(const double hz, const double rate) noexcept
    {
        const auto c = designEqBand(EqBandType::highCut, std::min(hz, rate * 0.45), 0.0, 0.7071, 0, 0, rate);
        for (auto& side : sides)
            side.c = c;
    }
    void reset() noexcept
    {
        for (auto& side : sides)
            side.reset();
    }
    double operator()(const std::size_t side, const double x) noexcept
    {
        auto& filter = sides[side];
        const auto y = filter(x);
        filter.z1 = flushed(filter.z1);
        filter.z2 = flushed(filter.z2);
        return y;
    }
};

// ---- Grains ---------------------------------------------------------------------------------------

// The sound written into a circular buffer; up to 32 grains read it back,
// each from its own point in the past (the time and a random spray), at its
// own pitch, forwards or backwards, windowed, placed in the stereo field. Their
// sum, low-passed, is the wet sound and what the feedback writes back.
class Grains final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;

    void prepare(const ProcessSpec& spec) override
    {
        rate_ = std::max(1.0, spec.sampleRate);
        channels_ = std::clamp<std::size_t>(spec.channelCount, 1, 2);
        bufferSamples_ = rhythm::grainsBufferSeconds * rate_;
        const auto size = powerOfTwoAbove(bufferSamples_ + 16.0);
        mask_ = size - 1;
        for (std::size_t channel = 0; channel < buffer_.size(); ++channel)
            buffer_[channel].assign(channel < channels_ ? size : 0, 0.0f);
        for (std::size_t index = 0; index < window_.size(); ++index)
            window_[index] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(index)
                                                                    / static_cast<double>(window_.size() - 1)));
        designedCut_ = -1.0f;
        reset();
    }

    void reset() noexcept override
    {
        for (auto& channel : buffer_)
            std::fill(channel.begin(), channel.end(), 0.0f);
        for (auto& grain : grains_)
            grain = {};
        write_ = 0;
        countdown_ = 0.0;
        cut_.reset();
        random_ = {};
        publish();
    }

    [[nodiscard]] std::size_t readings(float* into, const std::size_t count) const noexcept override
    {
        const auto shown = std::min(count, readings_.size());
        for (std::size_t index = 0; index < shown; ++index)
            into[index] = readings_[index].load(std::memory_order_relaxed);
        return shown;
    }

    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext& context) noexcept override
    {
        using namespace rhythm;
        clearAudio(output);
        const auto channels = std::min(input.channelCount, output.channelCount);
        const auto frames = std::min(input.frameCount, output.frameCount);
        if (channels == 0 || buffer_[0].empty())
            return;
        const auto used = std::min<std::size_t>(channels, channels_);
        std::array<float, grainsParameterCount> values {};
        for (int index = 0; index < grainsParameterCount; ++index)
            values[static_cast<std::size_t>(index)] = value(static_cast<std::size_t>(index));
        Spawn spawn;
        spawn.delayMs = grainsDelayMsOf(values[grainsTime], values[grainsSync], context.transport.beatsPerMinute);
        spawn.sprayMs = std::clamp(static_cast<double>(values[grainsSpray]), 0.0, 500.0);
        spawn.pitch = std::clamp(static_cast<double>(values[grainsPitch]), -24.0, 24.0);
        spawn.pitchRandom = std::clamp(static_cast<double>(values[grainsPitchRandom]), 0.0, 12.0);
        spawn.length = std::clamp(static_cast<double>(values[grainsSize]), 10.0, 500.0) * 0.001 * rate_;
        spawn.stereo = used > 1 ? std::clamp(static_cast<double>(values[grainsStereo]), 0.0, 1.0) : 0.0;
        spawn.reverseChance = std::clamp(static_cast<double>(values[grainsReverseChance]), 0.0, 1.0);
        const auto feedback = std::clamp(static_cast<double>(values[grainsFeedback]), 0.0, 0.95);
        const auto density = std::clamp(static_cast<double>(values[grainsDensity]), 1.0, 100.0);
        const auto freeze = values[grainsFreeze] >= 0.5f;
        // The wet sound is unlike the dry: they cross at equal power, so the
        // level holds across the mix.
        const auto mix = std::clamp(static_cast<double>(values[grainsMix]), 0.0, 1.0);
        const auto dryGain = mix <= 0.0 ? 1.0 : mix >= 1.0 ? 0.0 : std::cos(mix * kPi * 0.5);
        const auto wetGain = mix <= 0.0 ? 0.0 : mix >= 1.0 ? 1.0 : std::sin(mix * kPi * 0.5);
        const auto gain = dbToGain(std::clamp(static_cast<double>(values[grainsOutput]), -24.0, 24.0));
        // Grains reading the same sound in step add up like one signal (their
        // windows' sum scales them); scattered ones like noise (its power).
        const auto coherence = grainsCoherenceOf(values[grainsSpray], values[grainsPitch], values[grainsPitchRandom],
                                                 values[grainsReverseChance], values[grainsDensity]);
        const auto cut = std::clamp(values[grainsHighCut], 1'000.0f, 20'000.0f);
        if (cut != designedCut_) {
            cut_.design(cut, rate_);
            designedCut_ = cut;
        }
        // A grain every 1/density seconds on average, the gaps jittered.
        const auto interval = rate_ / density;
        countdown_ = std::min(countdown_, interval * 1.5);
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            countdown_ -= 1.0;
            if (countdown_ <= 0.0) {
                start(spawn);
                countdown_ += interval * (0.5 + random_.unit());
            }
            auto left = 0.0;
            auto right = 0.0;
            auto windows = 0.0;
            auto squares = 0.0;
            for (auto& grain : grains_) {
                if (!grain.on)
                    continue;
                const auto w = windowAt(grain.age / grain.length);
                const auto position = static_cast<double>(write_) - grain.distance;
                const auto a = readAt(buffer_[0], position);
                if (used > 1) {
                    const auto b = readAt(buffer_[1], position);
                    left += w * (grain.pan.leftFromLeft * a + grain.pan.leftFromRight * b);
                    right += w * (grain.pan.rightFromLeft * a + grain.pan.rightFromRight * b);
                } else {
                    left += w * a;
                }
                windows += w;
                squares += w * w;
                grain.age += 1.0;
                grain.distance += grain.step;
                if (grain.age >= grain.length)
                    grain.on = false;
            }
            auto scale = 1.0;
            if (windows > 0.0) {
                const auto byAmplitude = windows;
                const auto byPower = std::sqrt(squares);
                scale = coherence >= 0.999 ? byAmplitude
                        : coherence <= 0.001
                            ? byPower
                            : std::exp(coherence * std::log(std::max(byAmplitude, 1.0e-9))
                                       + (1.0 - coherence) * std::log(std::max(byPower, 1.0e-9)));
            }
            scale = 1.0 / std::max(1.0, scale);
            const auto wetLeft = cut_(0, left * scale);
            const auto wetRight = used > 1 ? cut_(1, right * scale) : wetLeft;
            for (std::size_t channel = 0; channel < used; ++channel) {
                const auto dry = static_cast<double>(input.channel(static_cast<std::uint32_t>(channel))[frame]);
                const auto wet = channel == 0 ? wetLeft : wetRight;
                // Frozen, the buffer keeps what it has and loops as it is.
                if (!freeze)
                    buffer_[channel][write_] = static_cast<float>(flushed(dry + feedback * softLimit(wet)));
                output.channel(static_cast<std::uint32_t>(channel))[frame] = static_cast<float>((dry * dryGain + wet * wetGain) * gain);
            }
            write_ = (write_ + 1) & mask_;
        }
        for (std::size_t channel = used; channel < channels; ++channel)
            std::copy_n(input.channel(static_cast<std::uint32_t>(channel)), frames, output.channel(static_cast<std::uint32_t>(channel)));
        publish();
    }

private:
    // A grain: where it reads (samples behind the next write), how that moves
    // each sample, how long it lasts and how far it is, its place.
    struct Grain {
        bool on {};
        double distance {};
        double step {};
        double length { 1.0 };
        double age {};
        double panValue {};
        rhythm::GrainsPan pan { 1.0, 0.0, 0.0, 1.0 };
    };
    // What a new grain takes, set once a block.
    struct Spawn {
        double delayMs {};
        double sprayMs {};
        double pitch {};
        double pitchRandom {};
        double length { 1.0 };
        double stereo {};
        double reverseChance {};
    };

    void start(const Spawn& spawn) noexcept
    {
        using namespace rhythm;
        Grain* grain = nullptr;
        for (auto& candidate : grains_)
            if (!candidate.on) {
                grain = &candidate;
                break;
            }
        // The pool full: this grain is skipped (stealing one would click).
        if (grain == nullptr)
            return;
        const auto ratio = grainsRatioOf(spawn.pitch + spawn.pitchRandom * random_.bipolar());
        const auto reversed = random_.unit() < spawn.reverseChance;
        const auto wanted = (spawn.delayMs + spawn.sprayMs * random_.unit()) * 0.001 * rate_;
        const auto back = grainsStartApplied(wanted, ratio, reversed, spawn.length, bufferSamples_);
        // Backwards a grain plays the same stretch from its end.
        grain->distance = reversed ? back - ratio * spawn.length : back;
        grain->step = reversed ? 1.0 + ratio : 1.0 - ratio;
        grain->length = spawn.length;
        grain->age = 0.0;
        grain->panValue = spawn.stereo * random_.bipolar();
        grain->pan = grainsPanOf(grain->panValue);
        grain->on = true;
    }

    // The Hann window at a phase in [0, 1), from its table.
    [[nodiscard]] double windowAt(const double phase) const noexcept
    {
        const auto place = std::clamp(phase, 0.0, 1.0) * static_cast<double>(window_.size() - 1);
        const auto index = std::min(static_cast<std::size_t>(place), window_.size() - 2);
        const auto t = place - static_cast<double>(index);
        return window_[index] + (window_[index + 1] - window_[index]) * t;
    }

    // A channel of the buffer between samples (4-point Hermite).
    [[nodiscard]] double readAt(const std::vector<float>& line, const double position) const noexcept
    {
        const auto whole = std::floor(position);
        const auto t = position - whole;
        const auto index = static_cast<std::int64_t>(whole);
        const auto at = [&](const std::int64_t i) { return static_cast<double>(line[static_cast<std::size_t>(i) & mask_]); };
        const auto before = at(index - 1);
        const auto x0 = at(index);
        const auto x1 = at(index + 1);
        const auto x2 = at(index + 2);
        const auto c1 = 0.5 * (x1 - before);
        const auto c2 = before - 2.5 * x0 + 2.0 * x1 - 0.5 * x2;
        const auto c3 = 0.5 * (x2 - before) + 1.5 * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

    void publish() noexcept
    {
        using namespace rhythm;
        auto sounding = 0;
        for (int index = 0; index < grainsPoolSize; ++index) {
            const auto& grain = grains_[static_cast<std::size_t>(index)];
            sounding += grain.on ? 1 : 0;
            const auto position = grain.on ? std::clamp(grain.distance / std::max(1.0, bufferSamples_), 0.0, 1.0) : 0.0;
            const auto level = grain.on ? windowAt(grain.age / grain.length) : 0.0;
            readings_[static_cast<std::size_t>(grainsReadingIndexOf(index, grainsReadingPosition))].store(static_cast<float>(position),
                                                                                                         std::memory_order_relaxed);
            readings_[static_cast<std::size_t>(grainsReadingIndexOf(index, grainsReadingLevel))].store(static_cast<float>(level),
                                                                                                      std::memory_order_relaxed);
            readings_[static_cast<std::size_t>(grainsReadingIndexOf(index, grainsReadingPan))].store(static_cast<float>(grain.panValue),
                                                                                                    std::memory_order_relaxed);
        }
        readings_[0].store(static_cast<float>(sounding), std::memory_order_relaxed);
    }

    double rate_ { 48'000.0 };
    std::size_t channels_ { 2 };
    double bufferSamples_ { 192'000.0 };
    std::array<std::vector<float>, 2> buffer_;
    std::size_t mask_ {};
    std::size_t write_ {};
    std::array<Grain, rhythm::grainsPoolSize> grains_ {};
    std::array<float, 1'025> window_ {};
    double countdown_ {};
    LowPass cut_;
    float designedCut_ { -1.0f };
    Random random_;
    std::array<std::atomic<float>, rhythm::grainsReadingCount> readings_ {};
};

// ---- Saccade --------------------------------------------------------------------------------------

// Every input sample goes into a rolling buffer. Once an interval of the
// tempo, at its offset, a region begins (if the chance says so): its first
// slice is the sound as it plays, caught; the repeats that follow read that
// slice back from the buffer until the region's gate ends. Two readers: the
// slice playing and the one fading out under it, so every edge crossfades.
class Saccade final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;

    void prepare(const ProcessSpec& spec) override
    {
        rate_ = std::max(1.0, spec.sampleRate);
        channels_ = std::clamp<std::size_t>(spec.channelCount, 1, 2);
        const auto size = powerOfTwoAbove(rhythm::saccadeBufferSeconds * rate_ + 16.0);
        mask_ = size - 1;
        size_ = static_cast<std::int64_t>(size);
        for (std::size_t channel = 0; channel < buffer_.size(); ++channel)
            buffer_[channel].assign(channel < channels_ ? size : 0, 0.0f);
        fade_ = std::max(1.0, rhythm::saccadeFadeSeconds * rate_);
        // A region longer than the buffer stops writing there: the slice it
        // repeats is kept, with the fade before it.
        holdAfter_ = size_ - static_cast<std::int64_t>(fade_) - 16;
        longest_ = rhythm::saccadeLongestSliceSeconds * rate_;
        designedCut_ = -1.0f;
        reset();
    }

    void reset() noexcept override
    {
        for (auto& channel : buffer_)
            std::fill(channel.begin(), channel.end(), 0.0f);
        written_ = 0;
        region_ = {};
        readers_ = {};
        insert_ = 0.0;
        lastTrigger_ = kNoTrigger;
        manualHeld_ = false;
        repeatsShown_ = 0;
        cut_.reset();
        random_ = {};
        for (auto& reading : readings_)
            reading.store(0.0f, std::memory_order_relaxed);
    }

    [[nodiscard]] std::size_t readings(float* into, const std::size_t count) const noexcept override
    {
        const auto shown = std::min(count, readings_.size());
        for (std::size_t index = 0; index < shown; ++index)
            into[index] = readings_[index].load(std::memory_order_relaxed);
        return shown;
    }

    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext& context) noexcept override
    {
        using namespace rhythm;
        clearAudio(output);
        const auto channels = std::min(input.channelCount, output.channelCount);
        const auto frames = std::min(input.frameCount, output.frameCount);
        if (channels == 0 || buffer_[0].empty())
            return;
        const auto used = std::min<std::size_t>(channels, channels_);
        std::array<float, saccadeParameterCount> values {};
        for (int index = 0; index < saccadeParameterCount; ++index)
            values[static_cast<std::size_t>(index)] = value(static_cast<std::size_t>(index));
        const auto& transport = context.transport;
        const auto running = transportRuns(context);
        tempo_ = rhythmTempoOf(transport.beatsPerMinute);
        const auto beatsPerSample = tempo_ / 60.0 / rate_;
        const auto intervalBeats = std::max(1.0e-3, saccadeIntervalBeatsOf(values[saccadeInterval], transport.timeSignatureNumerator,
                                                                           transport.timeSignatureDenominator));
        const auto offsetBeats = saccadeOffsetBeatsOf(values[saccadeOffset], intervalBeats);
        const auto gateSamples = saccadeGateBeatsOf(values[saccadeGate], intervalBeats) * 60.0 / tempo_ * rate_;
        settings_.grid = std::clamp(static_cast<int>(std::lround(values[saccadeGrid])), 1, static_cast<int>(noteDivisions.size()));
        settings_.variation = std::clamp(static_cast<double>(values[saccadeVariation]), 0.0, 1.0);
        settings_.decay = values[saccadeDecay];
        settings_.pitchDecay = values[saccadePitchDecay];
        settings_.reverseChance = std::clamp(static_cast<double>(values[saccadeReverseChance]), 0.0, 1.0);
        const auto chance = std::clamp(static_cast<double>(values[saccadeChance]), 0.0, 1.0);
        const auto mode = choice(values[saccadeMode], saccadeModeCount);
        const auto mix = std::clamp(static_cast<double>(values[saccadeMix]), 0.0, 1.0);
        const auto gain = dbToGain(std::clamp(static_cast<double>(values[saccadeOutput]), -24.0, 24.0));
        const auto cutoff = std::clamp(values[saccadeFilter], 200.0f, 20'000.0f);
        const auto open = saccadeFilterOpenOf(cutoff);
        if (!open && cutoff != designedCut_) {
            cut_.design(cutoff, rate_);
            designedCut_ = cutoff;
        }
        // The manual repeat: at once on its rising edge, until it falls.
        const auto manual = values[saccadeRepeat] >= 0.5f;
        if (manual && !manualHeld_) {
            if (region_.on)
                endRegion();
            beginRegion(true, std::numeric_limits<double>::infinity());
        } else if (!manual && manualHeld_ && region_.on && region_.manual) {
            endRegion();
        }
        manualHeld_ = manual;
        // Stopped, only the manual repeat.
        if (!running) {
            lastTrigger_ = kNoTrigger;
            if (region_.on && !region_.manual)
                endRegion();
        }
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            // The transport crossing an interval's offset: a region, maybe.
            if (running && !manual) {
                const auto q = transport.quarterNotePosition + static_cast<double>(frame) * beatsPerSample;
                const auto shifted = (q - offsetBeats) / intervalBeats;
                const auto index = static_cast<std::int64_t>(std::floor(shifted + 1.0e-9));
                if (index != lastTrigger_) {
                    const auto since = (shifted - static_cast<double>(index)) * intervalBeats;
                    lastTrigger_ = index;
                    if (region_.on)
                        endRegion();
                    // Only a crossing: a jump into the middle waits for the next.
                    if (since < 2.0 * beatsPerSample + 1.0e-7 && random_.unit() < chance)
                        beginRegion(false, gateSamples);
                }
            }
            // The sound into the rolling buffer, unless a long region holds it.
            if (!region_.on || written_ - region_.capture < holdAfter_) {
                const auto at = static_cast<std::size_t>(written_) & mask_;
                for (std::size_t channel = 0; channel < used; ++channel)
                    buffer_[channel][at] = input.channel(static_cast<std::uint32_t>(channel))[frame];
                ++written_;
            }
            // The region moves on: its end, or its next slice.
            if (region_.on) {
                if (!region_.manual && region_.time >= region_.length)
                    endRegion();
                else if (region_.time >= region_.sliceStart + region_.sliceLength)
                    nextSlice();
            }
            // The repeats.
            std::array<double, 2> repeats {};
            for (auto& reader : readers_)
                if (reader.on)
                    play(reader, repeats, used);
            if (!open)
                for (std::size_t channel = 0; channel < used; ++channel)
                    repeats[channel] = cut_(channel, repeats[channel]);
            // Inserted, the sound gives way to the repeats while they last.
            if (region_.on && region_.repeat >= 1)
                insert_ = std::min(1.0, insert_ + 1.0 / fade_);
            else
                insert_ = std::max(0.0, insert_ - 1.0 / fade_);
            for (std::size_t channel = 0; channel < used; ++channel) {
                const auto dry = static_cast<double>(input.channel(static_cast<std::uint32_t>(channel))[frame]);
                const auto wet = repeats[used > 1 ? channel : 0];
                const auto effect = mode == 0 ? dry + wet : mode == 1 ? dry * (1.0 - insert_) + wet : wet;
                output.channel(static_cast<std::uint32_t>(channel))[frame] = static_cast<float>((dry * (1.0 - mix) + effect * mix) * gain);
            }
            if (region_.on)
                region_.time += 1.0;
        }
        for (std::size_t channel = used; channel < channels; ++channel)
            std::copy_n(input.channel(static_cast<std::uint32_t>(channel)), frames, output.channel(static_cast<std::uint32_t>(channel)));
        // What the display shows.
        const auto gridBeats = region_.on ? region_.sliceBeats : saccadeNoteBeatsOf(static_cast<float>(settings_.grid));
        auto place = 0.0;
        if (running) {
            const auto q = transport.quarterNotePosition + static_cast<double>(frames) * beatsPerSample;
            place = q / intervalBeats - std::floor(q / intervalBeats);
        }
        readings_[saccadeReadingRepeating].store(region_.on ? 1.0f : 0.0f, std::memory_order_relaxed);
        readings_[saccadeReadingGrid].store(static_cast<float>(gridBeats), std::memory_order_relaxed);
        readings_[saccadeReadingPosition].store(static_cast<float>(place), std::memory_order_relaxed);
        readings_[saccadeReadingRepeats].store(static_cast<float>(repeatsShown_), std::memory_order_relaxed);
    }

private:
    static constexpr std::int64_t kNoTrigger = std::numeric_limits<std::int64_t>::min();

    // A slice being played back: where it reads from (samples after the
    // region's start in the buffer), at what speed, which way, how loud; it
    // fades in at its start (a repeat) and out when the next one comes.
    struct Reader {
        bool on {};
        double age {};
        double from {};
        double speed { 1.0 };
        bool reversed {};
        double level { 1.0 };
        double fadingSince { -1.0 };
    };
    // The region: where its slice begins in the buffer, how long it has run
    // and lasts, the slice playing (0 the sound itself, then the repeats).
    struct Region {
        bool on {};
        bool manual {};
        std::int64_t capture {};
        double time {};
        double length {};
        double sliceStart {};
        double sliceLength {};
        double sliceBeats {};
        int repeat {};
    };
    struct Settings {
        int grid { 3 };
        double variation {};
        float decay {};
        float pitchDecay {};
        double reverseChance {};
    };

    [[nodiscard]] double sliceSamples(const int grid) const noexcept
    {
        return std::clamp(rhythm::saccadeNoteBeatsOf(static_cast<float>(grid)) * 60.0 / tempo_ * rate_, 16.0, longest_);
    }

    void beginRegion(const bool manual, const double length) noexcept
    {
        region_ = {};
        region_.on = true;
        region_.manual = manual;
        region_.capture = written_;
        region_.length = length;
        region_.sliceLength = sliceSamples(settings_.grid);
        region_.sliceBeats = rhythm::saccadeNoteBeatsOf(static_cast<float>(settings_.grid));
        repeatsShown_ = 0;
        fadeOut(readers_[0]);
    }

    void endRegion() noexcept
    {
        fadeOut(readers_[0]);
        region_.on = false;
    }

    // The slice playing gives way: it fades out under what comes next,
    // reading on past its end.
    void fadeOut(Reader& current) noexcept
    {
        if (current.on) {
            readers_[1] = current;
            // Already fading (closing on an edge): it goes on from where it was.
            if (current.fadingSince < 0.0)
                readers_[1].fadingSince = current.age;
        }
        current = {};
    }

    void nextSlice() noexcept
    {
        using namespace rhythm;
        const auto repeat = region_.repeat + 1;
        // Varied: a neighbouring note value now and then.
        auto grid = settings_.grid;
        if (random_.unit() < settings_.variation) {
            const auto step = random_.unit() < settings_.variation * 0.5 ? 2 : 1;
            grid = std::clamp(grid + (random_.unit() < 0.5 ? -step : step), 1, static_cast<int>(noteDivisions.size()));
        }
        const auto length = sliceSamples(grid);
        Reader reader;
        reader.on = true;
        reader.speed = saccadeRepeatRateOf(settings_.pitchDecay, repeat);
        reader.reversed = random_.unit() < settings_.reverseChance;
        // Backwards from its end, never from beyond what has been heard.
        reader.from = reader.reversed ? std::min(reader.speed * length, region_.time - 4.0) : 0.0;
        reader.level = saccadeRepeatGainOf(settings_.decay, repeat);
        fadeOut(readers_[0]);
        readers_[0] = reader;
        region_.sliceStart += region_.sliceLength;
        region_.sliceLength = length;
        region_.sliceBeats = saccadeNoteBeatsOf(static_cast<float>(grid));
        region_.repeat = repeat;
        repeatsShown_ = repeat;
    }

    void play(Reader& reader, std::array<double, 2>& into, const std::size_t used) noexcept
    {
        auto level = reader.level * std::min(1.0, (reader.age + 1.0) / fade_);
        if (reader.fadingSince >= 0.0) {
            const auto gone = (reader.age - reader.fadingSince + 1.0) / fade_;
            if (gone >= 1.0) {
                reader = {};
                return;
            }
            level *= 1.0 - gone;
        }
        const auto offset = reader.reversed ? reader.from - reader.speed * reader.age : reader.from + reader.speed * reader.age;
        // Behind what was written, within what the buffer still holds.
        const auto newest = static_cast<double>(written_ - 2);
        const auto oldest = std::min(newest, static_cast<double>(written_ - size_ + 8));
        const auto wanted = static_cast<double>(region_.capture) + offset;
        // A fast repeat closing on what is being written (or a backwards one
        // on what the buffer lets go) fades out before it gets there: held on
        // one sample at the edge, it would click.
        if (reader.fadingSince < 0.0) {
            const auto writing = !(region_.on && written_ - region_.capture >= holdAfter_);
            const auto room = reader.reversed ? wanted - oldest : newest - wanted;
            const auto closing = reader.reversed ? reader.speed + (writing ? 1.0 : 0.0) : reader.speed - (writing ? 1.0 : 0.0);
            if (closing > 0.0 && room < closing * fade_ + 2.0)
                reader.fadingSince = reader.age;
        }
        const auto position = std::clamp(wanted, oldest, newest);
        const auto whole = std::floor(position);
        const auto t = position - whole;
        const auto index = static_cast<std::int64_t>(whole);
        for (std::size_t channel = 0; channel < used; ++channel) {
            const auto& line = buffer_[channel];
            const auto a = static_cast<double>(line[static_cast<std::size_t>(index) & mask_]);
            const auto b = static_cast<double>(line[static_cast<std::size_t>(index + 1) & mask_]);
            into[channel] += (a + (b - a) * t) * level;
        }
        reader.age += 1.0;
    }

    double rate_ { 48'000.0 };
    std::size_t channels_ { 2 };
    std::array<std::vector<float>, 2> buffer_;
    std::size_t mask_ {};
    std::int64_t size_ { 1 };
    std::int64_t written_ {};
    std::int64_t holdAfter_ {};
    double fade_ { 96.0 };
    double longest_ { 144'000.0 };
    double tempo_ { 120.0 };
    Region region_;
    std::array<Reader, 2> readers_ {};
    Settings settings_;
    double insert_ {};
    std::int64_t lastTrigger_ { kNoTrigger };
    bool manualHeld_ {};
    int repeatsShown_ {};
    LowPass cut_;
    float designedCut_ { -1.0f };
    Random random_;
    std::array<std::atomic<float>, rhythm::saccadeReadingCount> readings_ {};
};

// ---- Boucleur -------------------------------------------------------------------------------------

// A looper. Its state value is a command: a change of it acts (record, play,
// overdub, stop), its level does not, since the looper also moves on by
// itself (a loop of bars plays when its recording is done). The loop lives in
// a 60-second buffer; its peaks are kept a stretch at a time for the outline
// the display draws.
class Boucleur final : public EffectProcessor {
public:
    using EffectProcessor::EffectProcessor;

    void prepare(const ProcessSpec& spec) override
    {
        rate_ = std::max(1.0, spec.sampleRate);
        channels_ = std::clamp<std::size_t>(spec.channelCount, 1, 2);
        longest_ = static_cast<std::int64_t>(rhythm::boucleurLongestSeconds * rate_);
        for (std::size_t channel = 0; channel < loop_.size(); ++channel)
            loop_[channel].assign(channel < channels_ ? static_cast<std::size_t>(longest_ + 4) : 0, 0.0f);
        chunks_ = static_cast<std::size_t>(longest_ / rhythm::boucleurChunkSamples + 2);
        peaks_ = std::make_unique<std::atomic<float>[]>(chunks_);
        fade_ = std::max(1.0, rhythm::boucleurFadeSeconds * rate_);
        reset();
    }

    // The loop is forgotten (its samples are left: a recording overwrites them).
    void reset() noexcept override
    {
        state_ = 0;
        length_ = 0;
        recorded_ = 0;
        target_ = 0;
        position_ = 0.0;
        layers_ = 0;
        playing_ = 0.0;
        dubbing_ = 0.0;
        fading_ = 0;
        lastWritten_ = -1;
        seam_ = -1;
        clearPending_ = false;
        lastCommand_ = -1;
        lastClear_ = -1;
        lastBar_ = std::numeric_limits<std::int64_t>::min();
        for (std::size_t chunk = 0; chunk < chunks_; ++chunk)
            peaks_[chunk].store(0.0f, std::memory_order_relaxed);
        publish();
    }

    [[nodiscard]] std::size_t readings(float* into, const std::size_t count) const noexcept override
    {
        using namespace rhythm;
        const auto shown = std::min<std::size_t>(count, boucleurReadingCount);
        for (std::size_t index = 0; index < std::min<std::size_t>(shown, status_.size()); ++index)
            into[index] = status_[index].load(std::memory_order_relaxed);
        // The outline: each point the loudest stretch under it.
        const auto span = std::max<std::int64_t>(0, span_.load(std::memory_order_relaxed));
        const auto filled = std::max<std::int64_t>(0, filled_.load(std::memory_order_relaxed));
        for (std::size_t point = 0; point + boucleurReadingOutline < shown; ++point) {
            auto peak = 0.0f;
            if (span > 0 && peaks_ != nullptr) {
                const auto from = span * static_cast<std::int64_t>(point) / boucleurOutlinePoints;
                const auto to = std::min(filled, span * static_cast<std::int64_t>(point + 1) / boucleurOutlinePoints);
                for (auto chunk = from / boucleurChunkSamples; chunk * boucleurChunkSamples < to; ++chunk)
                    if (static_cast<std::size_t>(chunk) < chunks_)
                        peak = std::max(peak, peaks_[static_cast<std::size_t>(chunk)].load(std::memory_order_relaxed));
            }
            into[point + boucleurReadingOutline] = std::min(peak, 4.0f);
        }
        return shown;
    }

    void process(const AudioBlockView input, const AudioBlockView output, const ProcessContext& context) noexcept override
    {
        using namespace rhythm;
        clearAudio(output);
        const auto channels = std::min(input.channelCount, output.channelCount);
        const auto frames = std::min(input.frameCount, output.frameCount);
        if (channels == 0 || loop_[0].empty())
            return;
        const auto used = std::min<std::size_t>(channels, channels_);
        std::array<float, boucleurParameterCount> values {};
        for (int index = 0; index < boucleurParameterCount; ++index)
            values[static_cast<std::size_t>(index)] = value(static_cast<std::size_t>(index));
        const auto& transport = context.transport;
        const auto running = transportRuns(context);
        const auto tempo = rhythmTempoOf(transport.beatsPerMinute);
        const auto beatsPerSample = tempo / 60.0 / rate_;
        const auto barBeats = rhythmBarBeatsOf(transport.timeSignatureNumerator, transport.timeSignatureDenominator);
        settings_.barsSamples = static_cast<std::int64_t>(
            std::llround(boucleurBarsSecondsOf(values[boucleurBars], tempo, transport.timeSignatureNumerator, transport.timeSignatureDenominator)
                         * rate_));
        settings_.waitForBar = running && values[boucleurQuantize] >= 0.5f;
        // Commands: a change of the state, a change of the clear.
        const auto command = choice(values[boucleurState], boucleurStateCount);
        if (lastCommand_ < 0)
            lastCommand_ = command;
        else if (command != lastCommand_) {
            lastCommand_ = command;
            obey(command, transport.quarterNotePosition, barBeats, beatsPerSample);
        }
        const auto clear = static_cast<int>(std::lround(values[boucleurClear]));
        if (lastClear_ < 0)
            lastClear_ = clear;
        else if (clear != lastClear_) {
            lastClear_ = clear;
            erase();
        }
        const auto feedback = std::clamp(static_cast<double>(values[boucleurFeedback]), 0.0, 1.0);
        const auto level = std::clamp(static_cast<double>(values[boucleurLevel]), 0.0, 1.0);
        const auto dry = std::clamp(static_cast<double>(values[boucleurDry]), 0.0, 1.0);
        const auto gain = dbToGain(std::clamp(static_cast<double>(values[boucleurOutput]), -24.0, 24.0));
        const auto speed = boucleurSpeedOf(values[boucleurSpeed]) * (values[boucleurReverse] >= 0.5f ? -1.0 : 1.0);
        const auto step = 1.0 / fade_;
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            std::array<double, 2> in {};
            for (std::size_t channel = 0; channel < used; ++channel)
                in[channel] = static_cast<double>(input.channel(static_cast<std::uint32_t>(channel))[frame]);
            // Waiting for the bar line (or for nothing once the transport stops).
            if (state_ == boucleurWaiting) {
                if (!running) {
                    startRecording();
                } else {
                    const auto q = transport.quarterNotePosition + static_cast<double>(frame) * beatsPerSample;
                    const auto bar = static_cast<std::int64_t>(std::floor(q / barBeats + 1.0e-9));
                    if (bar != lastBar_) {
                        const auto since = q - static_cast<double>(bar) * barBeats;
                        lastBar_ = bar;
                        if (since < 2.0 * beatsPerSample + 1.0e-7)
                            startRecording();
                    }
                }
            }
            // Recording: the input written as it comes, up to the length; a
            // loop of bars then plays by itself, from the next sample.
            auto taken = false;
            if (state_ == 1) {
                if (recorded_ < target_)
                    record(in, used);
                if (recorded_ >= target_) {
                    finishRecording();
                    if (length_ > 0) {
                        // This sample was the take's last: the seam begins with the next.
                        seam_ = -2;
                        taken = true;
                        startPlaying(true);
                    }
                }
            }
            // The loop heard (before the seam or an overdub writes where it
            // reads): faded in and out as it starts and stops.
            const auto sounding = (state_ == 2 || state_ == 3) && length_ > 0 && !taken;
            playing_ = sounding ? std::min(1.0, playing_ + step) : std::max(0.0, playing_ - step);
            // A loop replaced by a new take still fades out from where it was.
            const auto heardLength = length_ > 0 ? length_ : fading_;
            std::array<double, 2> heard {};
            if (playing_ > 0.0 && heardLength > 0) {
                const auto whole = std::floor(position_);
                const auto t = position_ - whole;
                const auto a = static_cast<std::int64_t>(whole) % heardLength;
                const auto b = (a + 1) % heardLength;
                for (std::size_t channel = 0; channel < used; ++channel) {
                    const auto& line = loop_[channel];
                    const auto x0 = static_cast<double>(line[static_cast<std::size_t>(a)]);
                    const auto x1 = static_cast<double>(line[static_cast<std::size_t>(b)]);
                    heard[channel] = (x0 + (x1 - x0) * t) * playing_;
                }
            }
            // The seam: just after a recording, its start is crossfaded with
            // what came after its end, so the loop closes without a click.
            if (seam_ == -2) {
                seam_ = 0;
            } else if (seam_ >= 0) {
                if (seam_ < std::min<std::int64_t>(static_cast<std::int64_t>(fade_), length_)) {
                    const auto t = static_cast<double>(seam_) / fade_;
                    for (std::size_t channel = 0; channel < used; ++channel) {
                        auto& sample = loop_[channel][static_cast<std::size_t>(seam_)];
                        sample = static_cast<float>(in[channel] * (1.0 - t) + static_cast<double>(sample) * t);
                    }
                    notePeak(seam_, in, used, false);
                    ++seam_;
                } else {
                    seam_ = -1;
                }
            }
            // Overdubbing: what is there kept by the feedback, the input added;
            // punched in and out with a ramp.
            dubbing_ = state_ == 3 && length_ > 0 ? std::min(1.0, dubbing_ + step) : std::max(0.0, dubbing_ - step);
            if (dubbing_ > 0.0 && length_ > 0)
                overdub(in, used, feedback, speed);
            else
                lastWritten_ = -1;
            for (std::size_t channel = 0; channel < used; ++channel)
                output.channel(static_cast<std::uint32_t>(channel))[frame] = static_cast<float>((in[channel] * dry + heard[channel] * level) * gain);
            // On round the loop.
            if (heardLength > 0 && (sounding || playing_ > 0.0)) {
                position_ += speed;
                const auto loopLength = static_cast<double>(heardLength);
                if (position_ >= loopLength || position_ < 0.0) {
                    position_ -= std::floor(position_ / loopLength) * loopLength;
                    if (state_ == 3)
                        ++layers_;
                }
            } else if (!sounding && playing_ <= 0.0) {
                position_ = 0.0;
            }
            if (playing_ <= 0.0)
                fading_ = 0;
            if (clearPending_ && playing_ <= 0.0 && dubbing_ <= 0.0) {
                clearPending_ = false;
                forget();
            }
        }
        for (std::size_t channel = used; channel < channels; ++channel)
            std::copy_n(input.channel(static_cast<std::uint32_t>(channel)), frames, output.channel(static_cast<std::uint32_t>(channel)));
        publish();
    }

private:
    struct Settings {
        // A loop of bars (samples), none when free.
        std::int64_t barsSamples {};
        bool waitForBar {};
    };

    void obey(const int command, const double quarterNote, const double barBeats, const double beatsPerSample) noexcept
    {
        using namespace rhythm;
        const auto recording = state_ == 1;
        switch (command) {
        case 0:
            if (recording)
                finishRecording();
            state_ = 0;
            break;
        case 1: arm(quarterNote, barBeats, beatsPerSample); break;
        case 2:
            if (recording)
                finishRecording();
            if (length_ <= 0)
                state_ = 0;
            else
                startPlaying(state_ != 3);
            break;
        default:
            if (recording)
                finishRecording();
            // Nothing to overdub: a recording.
            if (length_ <= 0) {
                if (!recording)
                    arm(quarterNote, barBeats, beatsPerSample);
                break;
            }
            if (state_ != 2 && state_ != 3)
                position_ = 0.0;
            state_ = 3;
            ++layers_;
            break;
        }
    }

    // A recording, now or at the next bar line.
    void arm(const double quarterNote, const double barBeats, const double beatsPerSample) noexcept
    {
        clearPending_ = false;
        if (!settings_.waitForBar) {
            startRecording();
            return;
        }
        state_ = rhythm::boucleurWaiting;
        const auto bar = static_cast<std::int64_t>(std::floor(quarterNote / barBeats + 1.0e-9));
        lastBar_ = bar;
        // On the bar line already: at once.
        if (quarterNote - static_cast<double>(bar) * barBeats < 2.0 * beatsPerSample + 1.0e-7)
            startRecording();
    }

    void startRecording() noexcept
    {
        // A loop playing fades out from where it is while the take begins.
        fading_ = playing_ > 0.0 ? std::max(length_, fading_) : 0;
        if (fading_ <= 0)
            position_ = 0.0;
        state_ = 1;
        recorded_ = 0;
        length_ = 0;
        layers_ = 0;
        seam_ = -1;
        lastWritten_ = -1;
        target_ = settings_.barsSamples > 0 ? std::min(settings_.barsSamples, longest_) : longest_;
        fixed_ = settings_.barsSamples > 0;
    }

    // The length is what was recorded; too short a take is no loop.
    void finishRecording() noexcept
    {
        state_ = 0;
        if (recorded_ < static_cast<std::int64_t>(fade_ * 2.0)) {
            length_ = 0;
            layers_ = 0;
            return;
        }
        length_ = recorded_;
        layers_ = 1;
        seam_ = 0;
    }

    void startPlaying(const bool fromStart) noexcept
    {
        if (fromStart && state_ != 2)
            position_ = 0.0;
        state_ = 2;
    }

    // Clear: the loop is gone. A recording goes on afresh; a loop playing
    // fades out first.
    void erase() noexcept
    {
        if (state_ == 1) {
            recorded_ = 0;
            seam_ = -1;
            return;
        }
        if (state_ == rhythm::boucleurWaiting) {
            forget();
            return;
        }
        if (state_ == 2 || state_ == 3) {
            state_ = 0;
            clearPending_ = true;
            return;
        }
        forget();
    }

    void forget() noexcept
    {
        length_ = 0;
        layers_ = 0;
        position_ = 0.0;
        seam_ = -1;
        if (state_ != 1 && state_ != rhythm::boucleurWaiting)
            state_ = 0;
    }

    void record(const std::array<double, 2>& in, const std::size_t used) noexcept
    {
        for (std::size_t channel = 0; channel < used; ++channel)
            loop_[channel][static_cast<std::size_t>(recorded_)] = static_cast<float>(in[channel]);
        notePeak(recorded_, in, used, true);
        ++recorded_;
    }

    void overdub(const std::array<double, 2>& in, const std::size_t used, const double feedback, const double speed) noexcept
    {
        const auto keep = 1.0 - dubbing_ * (1.0 - feedback);
        const auto write = [&](const std::int64_t at) {
            for (std::size_t channel = 0; channel < used; ++channel) {
                auto& sample = loop_[channel][static_cast<std::size_t>(at)];
                // Layers piled up past full scale are bent back, never wrapped.
                sample = static_cast<float>(flushed(softLimit(static_cast<double>(sample) * keep + in[channel] * dubbing_)));
            }
            // The stretch's peak: what was there as kept, then what is written.
            if (at % rhythm::boucleurChunkSamples == 0 || (speed < 0.0 && (at + 1) % rhythm::boucleurChunkSamples == 0)) {
                auto& peak = peaks_[static_cast<std::size_t>(at / rhythm::boucleurChunkSamples)];
                peak.store(peak.load(std::memory_order_relaxed) * static_cast<float>(keep), std::memory_order_relaxed);
            }
            std::array<double, 2> now {};
            for (std::size_t channel = 0; channel < used; ++channel)
                now[channel] = static_cast<double>(loop_[channel][static_cast<std::size_t>(at)]);
            notePeak(at, now, used, false);
        };
        const auto here = static_cast<std::int64_t>(std::floor(position_)) % length_;
        if (lastWritten_ < 0 || lastWritten_ >= length_) {
            write(here);
        } else {
            // Every sample passed since the last one written (two at 2×).
            const auto direction = speed < 0.0 ? -1 : 1;
            auto at = lastWritten_;
            for (int guard = 0; at != here && guard < 4; ++guard) {
                at = (at + direction + length_) % length_;
                write(at);
            }
        }
        lastWritten_ = here;
    }

    void notePeak(const std::int64_t at, const std::array<double, 2>& samples, const std::size_t used, const bool fresh) noexcept
    {
        const auto chunk = static_cast<std::size_t>(at / rhythm::boucleurChunkSamples);
        if (chunk >= chunks_)
            return;
        auto loudest = 0.0f;
        for (std::size_t channel = 0; channel < used; ++channel)
            loudest = std::max(loudest, static_cast<float>(std::abs(samples[channel])));
        auto& peak = peaks_[chunk];
        const auto before = fresh && at % rhythm::boucleurChunkSamples == 0 ? 0.0f : peak.load(std::memory_order_relaxed);
        peak.store(std::max(before, loudest), std::memory_order_relaxed);
    }

    void publish() noexcept
    {
        using namespace rhythm;
        auto shownLength = length_;
        auto place = length_ > 0 ? position_ / static_cast<double>(length_) : 0.0;
        auto span = length_;
        auto filled = length_;
        if (state_ == 1) {
            // Recording: a loop of bars shows its whole length filling; a free
            // one what is there so far.
            shownLength = fixed_ ? target_ : recorded_;
            place = fixed_ && target_ > 0 ? static_cast<double>(recorded_) / static_cast<double>(target_) : 1.0;
            span = fixed_ ? target_ : recorded_;
            filled = recorded_;
        } else if (state_ == 0 || state_ == boucleurWaiting) {
            place = 0.0;
        }
        span_.store(span, std::memory_order_relaxed);
        filled_.store(filled, std::memory_order_relaxed);
        status_[boucleurReadingState].store(static_cast<float>(state_), std::memory_order_relaxed);
        status_[boucleurReadingLength].store(static_cast<float>(static_cast<double>(shownLength) / rate_), std::memory_order_relaxed);
        status_[boucleurReadingPosition].store(static_cast<float>(std::clamp(place, 0.0, 1.0)), std::memory_order_relaxed);
        status_[boucleurReadingLayers].store(static_cast<float>(layers_), std::memory_order_relaxed);
    }

    double rate_ { 48'000.0 };
    std::size_t channels_ { 2 };
    std::array<std::vector<float>, 2> loop_;
    std::int64_t longest_ { 1 };
    std::unique_ptr<std::atomic<float>[]> peaks_;
    std::size_t chunks_ {};
    double fade_ { 480.0 };
    Settings settings_;
    // 0 stopped, 1 recording, 2 playing, 3 overdubbing, 4 waiting for the bar.
    int state_ {};
    std::int64_t length_ {};
    std::int64_t recorded_ {};
    std::int64_t target_ {};
    bool fixed_ {};
    double position_ {};
    int layers_ {};
    // The loop's level as it starts and stops, the overdub's as it punches.
    double playing_ {};
    double dubbing_ {};
    // The length a loop replaced by a new take is still read with as it fades.
    std::int64_t fading_ {};
    std::int64_t lastWritten_ { -1 };
    std::int64_t seam_ { -1 };
    bool clearPending_ {};
    int lastCommand_ { -1 };
    int lastClear_ { -1 };
    std::int64_t lastBar_ {};
    std::array<std::atomic<float>, rhythm::boucleurReadingOutline> status_ {};
    std::atomic<std::int64_t> span_ {};
    std::atomic<std::int64_t> filled_ {};
};

} // namespace

std::unique_ptr<EffectProcessor> makeRhythmEffect(const EffectDescriptor& descriptor)
{
    switch (descriptor.kind) {
    case NativeDspNodeKind::grains: return std::make_unique<Grains>(descriptor);
    case NativeDspNodeKind::saccade: return std::make_unique<Saccade>(descriptor);
    case NativeDspNodeKind::boucleur: return std::make_unique<Boucleur>(descriptor);
    default: return {};
    }
}

} // namespace cydaw::engine
