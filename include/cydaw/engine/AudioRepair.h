#pragma once

#include <cstdint>
#include <vector>

// Offline treatments of a recorded sound (the Processing mode's built-in
// steps): noise taken away, mains hum notched out, clicks mended, filters,
// level, silence trimmed; and where a rendering that may ring past its sound
// (a reverb's tail) ends. Each acts in place on channels of equal length, at
// any rate; none adds delay.

namespace cydaw::engine::repair {

using Channels = std::vector<std::vector<float>>;

// Noise reduction by spectral subtraction: the noise's spectrum is what every
// frequency keeps at its quietest moments (a low percentile over the sound),
// each moment's spectrum lowered where it hardly rises above it, at most by
// `reductionDb`. The same gain on every channel (the stereo image kept); the
// gains smoothed over time and frequency (no "musical noise" chirps).
struct DenoiseSettings {
    float reductionDb { 18.0f };  // how far the noise goes down, 0..48
    float sensitivity { 1.5f };   // how much is taken for noise, 0.5..4
    float smoothing { 0.5f };     // 0..1
};
void denoise(Channels& channels, double rate, const DenoiseSettings& settings);

// Mains hum: notches (zero phase) at the fundamental and its harmonics. A
// fundamental of 0: 50 or 60 Hz, whichever the sound holds more of. Returns
// the fundamental notched.
struct HumSettings {
    float fundamental {};        // Hz; 0: found
    int harmonics { 6 };         // 1..16
    float width { 30.0f };       // the notches' Q, 5..100
};
float removeHum(Channels& channels, double rate, const HumSettings& settings);

// Clicks and crackle: a sample far off what its neighbours predict (its
// second difference against the local level), in bursts of at most
// `maxClickMs`, redrawn by a cubic between the sound on each side. Returns
// how many bursts were mended.
struct DeclickSettings {
    float sensitivity { 0.5f };   // 0..1
    float maxClickMs { 2.0f };    // 0.2..10
};
int declick(Channels& channels, double rate, const DeclickSettings& settings);

// Butterworth filters, 12 or 24 dB per octave.
void highpass(Channels& channels, double rate, float cutoff, int order = 2);
void lowpass(Channels& channels, double rate, float cutoff, int order = 2);

void gain(Channels& channels, float db);
// The loudest sample brought to `peakDb` (nothing when silent).
void normalise(Channels& channels, float peakDb);
// Silence (under `thresholdDb`) cut at the start and the end, `keepMs` kept.
// Returns the frames taken off the start.
std::int64_t trimSilence(Channels& channels, double rate, float thresholdDb = -60.0f, float keepMs = 20.0f);

// A rendering that ran past its sound (`sourceFrames`) to let it ring: where
// it ends. The tail ends once it stays under `thresholdDb` (below the
// rendering's peak) for `holdSeconds`; a tail that stops decaying while still
// above that (a noise that never ends) ends where it stopped decaying; never
// past `maxSeconds`. `endless` says the second or third case.
struct TailSettings {
    bool allow { true };
    float thresholdDb { -60.0f };
    float holdSeconds { 0.3f };
    float maxSeconds { 10.0f };
    float fadeMs { 30.0f };
};
[[nodiscard]] std::int64_t tailEnd(const Channels& channels, std::int64_t sourceFrames, double rate, const TailSettings& settings,
                                   bool* endless = nullptr);
// Cut at `frames`, the last `fadeMs` faded out.
void cutWithFade(Channels& channels, std::int64_t frames, double rate, float fadeMs);

} // namespace cydaw::engine::repair
