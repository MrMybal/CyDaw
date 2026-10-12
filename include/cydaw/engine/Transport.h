#pragma once

#include <cstdint>

namespace cydaw::engine {

enum class PlaybackState : std::uint8_t {
    stopped,
    playing,
    recording
};

struct TransportSnapshot {
    PlaybackState state { PlaybackState::stopped };
    std::int64_t samplePosition {};
    double quarterNotePosition {};
    double beatsPerMinute { 120.0 };
    std::uint16_t timeSignatureNumerator { 4 };
    std::uint16_t timeSignatureDenominator { 4 };
};

class TransportClock final {
public:
    void prepare(double sampleRate) noexcept;
    void play() noexcept;
    void record() noexcept;
    void stop() noexcept;
    void seek(std::int64_t samplePosition) noexcept;
    [[nodiscard]] bool setTempo(double beatsPerMinute) noexcept;
    [[nodiscard]] bool setTimeSignature(std::uint16_t numerator, std::uint16_t denominator) noexcept;
    void advance(std::uint32_t frameCount) noexcept;

    [[nodiscard]] TransportSnapshot snapshot() const noexcept;

private:
    double sampleRate_ { 48'000.0 };
    double beatsPerMinute_ { 120.0 };
    double quarterNotePosition_ {};
    std::int64_t samplePosition_ {};
    std::uint16_t numerator_ { 4 };
    std::uint16_t denominator_ { 4 };
    PlaybackState state_ { PlaybackState::stopped };
};

} // namespace cydaw::engine
