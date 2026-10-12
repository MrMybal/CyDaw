#include <cydaw/engine/Transport.h>

#include <algorithm>
#include <cmath>

namespace cydaw::engine {

void TransportClock::prepare(const double sampleRate) noexcept
{
    if (std::isfinite(sampleRate) && sampleRate > 0.0) {
        sampleRate_ = sampleRate;
        quarterNotePosition_ = static_cast<double>(samplePosition_) * beatsPerMinute_ / (sampleRate_ * 60.0);
    }
}

void TransportClock::play() noexcept { state_ = PlaybackState::playing; }
void TransportClock::record() noexcept { state_ = PlaybackState::recording; }
void TransportClock::stop() noexcept { state_ = PlaybackState::stopped; }

void TransportClock::seek(const std::int64_t samplePosition) noexcept
{
    samplePosition_ = std::max<std::int64_t>(0, samplePosition);
    quarterNotePosition_ = static_cast<double>(samplePosition_) * beatsPerMinute_ / (sampleRate_ * 60.0);
}

bool TransportClock::setTempo(const double beatsPerMinute) noexcept
{
    if (!std::isfinite(beatsPerMinute) || beatsPerMinute < 20.0 || beatsPerMinute > 999.0)
        return false;
    beatsPerMinute_ = beatsPerMinute;
    return true;
}

bool TransportClock::setTimeSignature(const std::uint16_t numerator, const std::uint16_t denominator) noexcept
{
    const auto validDenominator = denominator == 1 || denominator == 2 || denominator == 4
                                  || denominator == 8 || denominator == 16 || denominator == 32;
    if (numerator == 0 || numerator > 64 || !validDenominator)
        return false;
    numerator_ = numerator;
    denominator_ = denominator;
    return true;
}

void TransportClock::advance(const std::uint32_t frameCount) noexcept
{
    if (state_ != PlaybackState::stopped) {
        samplePosition_ += frameCount;
        quarterNotePosition_ += static_cast<double>(frameCount) * beatsPerMinute_ / (sampleRate_ * 60.0);
    }
}

TransportSnapshot TransportClock::snapshot() const noexcept
{
    return { state_, samplePosition_, quarterNotePosition_, beatsPerMinute_, numerator_, denominator_ };
}

} // namespace cydaw::engine
