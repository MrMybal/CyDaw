#pragma once
#include "ModeProcessor.h"
#include <cydaw/realtime/RtPublisher.h>

#include <cydaw/engine/FoundrySession.h>

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

// The Foundry's playback: the session being edited (its tracks mixed), from
// a frame to another, once or over and over, read at the device's rate. It
// joins the master's mixer as the workshop's simulator does (the master's
// gain applies).

namespace cydaw::app {

class FoundryPlayer final : public juce::AudioSource {
public:
    FoundryPlayer();
    ~FoundryPlayer() override;

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& output) override;

    // The session played from now on (an edit gives a new one; the place is
    // kept). Null: nothing.
    void setSession(std::shared_ptr<const engine::foundry::Session> session);
    // From `from` to `to` (frames; to <= from: the end), over and over when
    // `loop`.
    void play(std::int64_t from, std::int64_t to, bool loop);
    void stop();
    [[nodiscard]] bool playing() const noexcept { return playing_.load(std::memory_order_acquire); }
    // Where it plays, in the session's frames.
    [[nodiscard]] double position() const noexcept { return position_.load(std::memory_order_relaxed); }
    void setGainDb(float db) noexcept { gain_.store(juce::Decibels::decibelsToGain(db, -60.0f), std::memory_order_relaxed); }
    // The highest level of a side (0, 1) since the last look.
    [[nodiscard]] float takePeak(int side) noexcept;

    void setProcessor(std::shared_ptr<ModeProcessor> processor) {
        processor_.store(std::make_shared<const std::shared_ptr<ModeProcessor>>(std::move(processor)));
    }
private:
    realtime::RtPublisher<std::shared_ptr<ModeProcessor>> processor_;
    [[nodiscard]] float sampleAt(int channel, double frame) noexcept;
    void freeRetired();

    std::mutex lock_;
    std::shared_ptr<const engine::foundry::Session> session_;
    std::vector<std::pair<std::uint64_t, std::shared_ptr<const engine::foundry::Session>>> retired_;
    std::atomic<const engine::foundry::Session*> pending_ { nullptr };
    std::atomic<bool> hasPending_ {};
    const engine::foundry::Session* current_ {};
    std::int64_t currentFrames_ {};

    std::atomic<bool> playing_ {};
    std::atomic<bool> restart_ {};
    std::atomic<bool> stopping_ {};
    std::atomic<std::int64_t> from_ {};
    std::atomic<std::int64_t> to_ {};
    std::atomic<bool> loop_ {};
    std::atomic<double> position_ {};
    std::atomic<float> gain_ { 1.0f };
    std::array<std::atomic<float>, 2> peaks_ {};
    std::atomic<std::uint64_t> blocks_ {};

    // The audio thread's.
    double rate_ { 48'000.0 };
    double at_ {};
    float level_ {};
    bool running_ {};
    int tailSamples_{};
    std::int64_t playFrom_ {};
    std::int64_t playTo_ {};
    bool looping_ {};
    // A window of the mix read ahead, per side, and what mixing it needs.
    std::array<std::vector<float>, 2> cache_;
    std::vector<float> scratch_;
    std::int64_t cacheStart_ { -1 };
    std::int64_t cacheLength_ {};
};

} // namespace cydaw::app
