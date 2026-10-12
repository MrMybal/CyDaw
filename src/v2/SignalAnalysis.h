#pragma once

#include <cydaw/engine/AudioAnalysis.h>

#include <array>
#include <cstdint>

// What the device displays read from a tapped signal: a spectrum on a
// logarithmic axis, smoothed for the eye, and levels. Message thread only.

namespace cydaw::v2 {

class Spectrum final {
public:
    static constexpr int binCount = 160;
    static constexpr float minimumHz = 20.0f;
    static constexpr float maximumHz = 20'000.0f;
    // The range drawn, in dB, before the display tilt.
    static constexpr float floorDb = -72.0f;
    static constexpr float ceilingDb = 0.0f;

    // A new frame, or none when the tap brought nothing new: then the
    // display falls back to silence, as an analyser does when the music
    // stops.
    void update(const engine::AudioAnalysisFrame* frame, double nowSeconds);
    void clear();

    // 0..1 per bin, bin 0 at minimumHz.
    [[nodiscard]] const std::array<float, binCount>& levels() const noexcept { return levels_; }
    // Something above the floor to draw.
    [[nodiscard]] bool visible() const noexcept { return visible_; }

    [[nodiscard]] static float frequencyOfBin(float bin) noexcept;
    [[nodiscard]] static float binOfFrequency(float hz) noexcept;

private:
    std::array<float, binCount> levels_ {};
    double lastUpdate_ {};
    bool visible_ {};
};

// The spectrometer's analysis: a finer spectrum than the displays' (dB per
// bin, tilted per octave, smoothed in time), its peaks held then let fall,
// and the spectrum over the last seconds, row by row, for a waterfall.
class Spectrometer final {
public:
    static constexpr int binCount = 256;
    static constexpr int rowCount = 240;
    struct Settings {
        float floorDb { -90.0f };
        float tiltDb { 3.0f };      // per octave, around 1 kHz
        float smoothing { 0.5f };   // 0 at once, 1 slow
        float holdSeconds { 2.0f }; // 0 none, 10 for ever
        float historySeconds { 8.0f };
    };

    // Once a frame: the newest samples (or none when nothing new came).
    void update(const engine::AudioAnalysisFrame* frame, double nowSeconds, const Settings& settings);
    void clearPeaks();
    // dB per bin, the display's tilt applied.
    [[nodiscard]] const std::array<float, binCount>& levels() const noexcept { return levels_; }
    [[nodiscard]] const std::array<float, binCount>& peaks() const noexcept { return peaks_; }
    // A row of the waterfall in dB, 0 the newest.
    [[nodiscard]] const float* row(int age) const noexcept;
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] static float frequencyOfBin(float bin) noexcept;

private:
    std::array<float, binCount> levels_ {};
    std::array<float, binCount> peaks_ {};
    std::array<double, binCount> peakTimes_ {};
    std::array<float, binCount * rowCount> rows_ {};
    int newestRow_ {};
    double lastUpdate_ {};
    double lastFresh_ { -1.0e9 };
    double lastRow_ {};
    std::uint64_t sequence_ {};
    bool started_ {};
    bool active_ {};
};

// The peak of the most recent samples of a frame, in dB.
[[nodiscard]] float recentPeakDb(const engine::AudioAnalysisFrame& frame, std::uint32_t samples = 480);
// Their RMS level, in dB.
[[nodiscard]] float recentRmsDb(const engine::AudioAnalysisFrame& frame, std::uint32_t samples = 1'024);

} // namespace cydaw::v2
