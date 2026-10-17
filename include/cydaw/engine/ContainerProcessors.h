#pragma once

#include <cydaw/engine/AudioBuffer.h>
#include <cydaw/engine/AudioProcessor.h>
#include <cydaw/engine/Containers.h>
#include <cydaw/engine/EffectProcessors.h>
#include <cydaw/engine/PolyVoice.h>
#include <cydaw/realtime/RtPublisher.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

// The processors of a container (Containers.h): its head (every setting of
// the container; the sound passes through it, and it splits off what each
// branch hears), the tap at a branch's start (that part), the end (the
// branches mixed back, as the head's settings say).

namespace cydaw::engine {

class ContainerHeadProcessor final : public EffectProcessor {
public:
    explicit ContainerHeadProcessor(const EffectDescriptor& descriptor) noexcept;

    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(AudioBlockView input, AudioBlockView output, const ProcessContext& context) noexcept override;
    // The replacer's: how many transients it has replaced, how hard the last.
    [[nodiscard]] std::size_t readings(float* into, std::size_t count) const noexcept override;

    [[nodiscard]] NativeDspNodeKind kind() const noexcept { return descriptor().kind; }
    // A setting by its key, as heard now; `fallback` when it has none.
    [[nodiscard]] float setting(std::string_view key, float fallback = 0.0f) const noexcept;
    // A branch's own (its volume, pan, mute, solo), by number from 0.
    [[nodiscard]] float branchSetting(std::string_view prefix, int branch, float fallback = 0.0f) const noexcept;
    [[nodiscard]] int branchCount() const noexcept;
    // The instrument a replacer plays (null: none). Off the audio thread, or
    // on it with the one it has (a parameter update): what it held is
    // retired, not freed there.
    void setInstrument(std::shared_ptr<const PolySynthGraphPlan> plan);
    // A branch's part of the sound, `frames` from its reader's place on;
    // the head writes it as it processes, the reader follows block by block.
    void readPart(int branch, std::uint64_t& cursor, AudioBlockView into) const noexcept;

private:
    struct Biquad {
        float b0 { 1.0f }, b1 {}, b2 {}, a1 {}, a2 {};
        std::array<float, 2> z1 {}, z2 {};
        [[nodiscard]] float run(std::size_t channel, float x) noexcept
        {
            const auto y = b0 * x + z1[channel];
            z1[channel] = b1 * x - a1 * y + z2[channel];
            z2[channel] = b2 * x - a2 * y;
            return y;
        }
        void clear() noexcept
        {
            z1 = {};
            z2 = {};
        }
    };
    // A Linkwitz-Riley crossover (two Butterworth stages each side).
    struct Crossover {
        std::array<Biquad, 2> low;
        std::array<Biquad, 2> high;
        float frequency {};
        void design(float hz, double sampleRate) noexcept;
        void clear() noexcept;
    };
    [[nodiscard]] std::size_t indexOf(std::string_view key) const noexcept;
    void split(AudioBlockView input, std::uint32_t at) noexcept;
    void replace(AudioBlockView input, std::uint32_t at) noexcept;

    double sampleRate_ { 48'000.0 };
    // The parts, three branches of two channels, in a ring.
    AudioBuffer ring_;
    std::uint32_t ringSize_ {};
    std::uint64_t written_ {};
    std::array<Crossover, 2> crossovers_ {};
    // A band split again: the low band through the high crossover, so its
    // phase follows the others'.
    Crossover align_ {};
    // The replacer's detector and its sound.
    float fast_ {}, slow_ {};
    double sinceTrigger_ { 1.0e9 };
    double voiceTime_ { 1.0e9 };
    double voicePhase_ {};
    float voiceLevel_ {};
    std::uint32_t noise_ { 0x12345678u };
    std::atomic<std::uint32_t> triggers_ {};
    std::atomic<float> lastStrength_ {};
    // The replacer's instrument: its plan, its voices, what they read of the
    // host (a sampler's place, the clock), how long each still holds its note.
    cydaw::realtime::RtPublisher<PolySynthGraphPlan> instrument_;
    std::atomic<const PolySynthGraphPlan*> instrumentHeld_ {};
    static constexpr std::size_t voiceCount = 8;
    std::array<PolyVoice, voiceCount> voices_ {};
    std::array<double, voiceCount> holding_ {};
    std::array<std::uint64_t, voiceCount> started_ {};
    std::uint64_t starts_ {};
    prism::Context context_ {};
    AudioBuffer voiceScratch_;
    bool instrumentStruck_ {};
    float instrumentVelocity_ {};
};

class ContainerTapProcessor final : public AudioProcessor {
public:
    ContainerTapProcessor(const ContainerHeadProcessor* head, int branch) noexcept : head_(head), branch_(branch) {}
    void prepare(const ProcessSpec&) override {}
    void reset() noexcept override { cursor_ = 0; }
    void process(AudioBlockView input, AudioBlockView output, const ProcessContext& context) noexcept override;

private:
    const ContainerHeadProcessor* head_ {};
    int branch_ {};
    std::uint64_t cursor_ {};
};

class ContainerEndProcessor final : public AudioProcessor, public MultiBusAudioProcessor {
public:
    // The head whose settings it mixes by (the compiler binds it).
    void bind(const ContainerHeadProcessor* head) noexcept { head_ = head; }
    // The first of two ends (a post chain follows it): the branches mixed,
    // no dry sound, no Mix; the last end mixes.
    void setBlendOnly(const bool blendOnly) noexcept { blendOnly_ = blendOnly; }
    [[nodiscard]] const ContainerHeadProcessor* head() const noexcept { return head_; }
    void prepare(const ProcessSpec& spec) override;
    void reset() noexcept override;
    void process(AudioBlockView input, AudioBlockView output, const ProcessContext& context) noexcept override;
    void processAudioBuses(std::span<const AudioInputBusView> inputs, AudioBlockView output,
                           const ProcessContext& context) noexcept override;

private:
    void mixDry(const AudioBlockView* dry, AudioBlockView output) noexcept;

    const ContainerHeadProcessor* head_ {};
    bool blendOnly_ {};
    double sampleRate_ { 48'000.0 };
    // A selector's chain heard, the one it leaves, how far the fade is.
    int current_ { -1 };
    int previous_ { -1 };
    float fade_ { 1.0f };
    // Each value as heard, gliding toward the head's (no zipper).
    std::array<float, container::maximumBranches> gains_ {};
    std::array<float, container::maximumBranches> pans_ {};
    float mix_ { -1.0f };
    float wet_ { 1.0f };
    float x_ { 0.5f }, y_ { 0.5f };
};

// A container's head for its catalogue entry; empty for any other.
[[nodiscard]] std::unique_ptr<EffectProcessor> makeContainerHead(const EffectDescriptor& descriptor);

} // namespace cydaw::engine
