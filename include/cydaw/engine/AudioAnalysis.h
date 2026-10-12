#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace cydaw::engine {

// Compact telemetry passed from the real-time audio thread to the UI. It is
// intentionally trivially copyable so it can use the lock-free SPSC queue.
struct AudioAnalysisFrame final {
    // Consecutive samples, the most recent last — not a decimated block. A
    // spectrum drawn in hertz behind an EQ curve has to be calibrated, and
    // has to resolve the low end: two thousand and forty-eight samples at
    // forty-eight kilohertz separate frequencies twenty-three hertz apart.
    static constexpr std::size_t waveformCapacity = 2'048;
    static constexpr std::size_t maximumChannels = 8;

    std::array<float, waveformCapacity> waveform {};
    std::array<float, maximumChannels> peak {};
    std::array<float, maximumChannels> rms {};
    std::uint32_t waveformCount {};
    std::uint32_t channelCount {};
    float sampleRate { 48'000.0f };
    std::uint64_t sequence {};
};

// The last samples of a mono mix, for an analysis frame. Fixed size and
// written on the audio thread, so nothing is allocated there. Three
// producers used to decimate a block into a fixed number of points, each in
// its own words; a block decimated is a display, not a spectrum.
class AnalysisWaveformRing final {
public:
    static constexpr std::size_t capacity = AudioAnalysisFrame::waveformCapacity;

    void push(const float sample) noexcept
    {
        samples_[write_] = sample;
        write_ = (write_ + 1) % capacity;
        if (filled_ < capacity)
            ++filled_;
    }

    // Oldest first, so the frame reads as time running left to right.
    void fill(AudioAnalysisFrame& frame) const noexcept
    {
        frame.waveformCount = static_cast<std::uint32_t>(filled_);
        const auto start = (write_ + capacity - filled_) % capacity;
        for (std::size_t point = 0; point < filled_; ++point)
            frame.waveform[point] = samples_[(start + point) % capacity];
    }

    void clear() noexcept
    {
        write_ = 0;
        filled_ = 0;
    }

private:
    std::array<float, capacity> samples_ {};
    std::size_t write_ {};
    std::size_t filled_ {};
};

// The level at each of `count` frequencies spaced logarithmically from
// `minimumHz` to `maximumHz`, as 0..1 over an eighty-decibel range. One
// Goertzel per bin: a two-term recurrence per sample rather than a sine and
// a cosine, and it accepts any frequency, so the bins can sit exactly where
// an EQ plot draws them. This is the one place a spectrum is computed;
// the graph cards, the module editor and the EQ curve all read it.
void computeLogSpectrum(const AudioAnalysisFrame& frame, double minimumHz,
                        double maximumHz, float* bins, std::size_t count);

} // namespace cydaw::engine
