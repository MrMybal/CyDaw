#pragma once

#include <complex>
#include <cstdint>
#include <memory>
#include <vector>

// A stretch of a sound file read at a speed and a pitch of its own, as a
// player asks for it: from any position, block after block.
//
// repitch reads the file faster or slower (its pitch follows, as a tape
// does), through a windowed sinc. The others keep the pitch, and take a
// transposition of their own: a phase vocoder whose partials keep their
// phases locked to the peak they belong to, and whose channels keep their
// phase differences (the stereo image stays put).
//   tonal   — long windows: chords, voices, pads.
//   beats   — the sound cut at its attacks, each slice played as it is (at
//             its pitch, transposed) from where its attack falls: drums
//             keep every transient; slowed down, a slice fades out before
//             the next one starts.
//   texture — very long windows, phases drawn at random: a sound smeared
//             into a texture, for stretches of ten times and more.
//   complex — middle windows whose phases start afresh at every attack:
//             whole mixes, where drums and chords play together.
// The transposition can move over time (a pitch curve): the stretched
// sound is then read faster or slower as it goes, the time it takes kept.
// The output is read from the stretched sound through the same resampler,
// which also turns the file's rate into the output's. Unstretched and at the
// output's rate, a file is copied as it is.

namespace cydaw::engine {

enum class StretchAlgorithm : int { repitch = 0, tonal = 1, beats = 2, texture = 3, complex = 4 };

// Where a stretcher reads: `count` frames from `start` (in the file's own
// frames; before 0 and past the end it is silence) into `channelCount`
// channels.
class StretchSource {
public:
    virtual ~StretchSource() = default;
    virtual void read(std::int64_t start, int count, float* const* channels, int channelCount) = 0;
};

struct StretchSettings {
    StretchAlgorithm algorithm { StretchAlgorithm::repitch };
    double sourceRate { 48'000.0 };
    double outputRate { 48'000.0 };
    // The file frame the output's first frame reads.
    double sourceStart {};
    // Seconds of the file per second of output (2: twice as fast).
    double speed { 1.0 };
    // The frequencies multiplied (2: an octave up). repitch ignores it.
    double pitch { 1.0 };
    int channels { 2 };
    // Semitones added to the pitch along the output, one every
    // `pitchCurveSeconds` from its first frame (the last held after it);
    // none: the pitch alone. repitch ignores it.
    std::shared_ptr<const std::vector<float>> pitchCurve;
    double pitchCurveSeconds { 0.005 };

    [[nodiscard]] bool operator==(const StretchSettings&) const = default;
};

class TimeStretcher {
public:
    // Allocates: not on the audio callback.
    void prepare(const StretchSettings& settings);
    [[nodiscard]] const StretchSettings& settings() const noexcept { return settings_; }
    // Forgets what was rendered: the next block starts afresh.
    void reset() noexcept;
    // Output frames [position, position + count) into `output` (settings.
    // channels channels). A position other than the one after the last block
    // starts afresh there.
    void render(StretchSource& source, std::int64_t position, int count, float* const* output);

private:
    // The stretched sound (at the file's rate) final over [from, to).
    void ensureStretched(StretchSource& source, std::int64_t from, std::int64_t to);
    void synthesiseFrame(StretchSource& source);
    // beats: the next slices laid out until `to`.
    void layOutSlices(StretchSource& source, std::int64_t to);
    // The next attack after `from` (file frames), or a cut a tenth of a
    // second later when none comes within half a second (nextIsAttack_).
    [[nodiscard]] double nextAttack(StretchSource& source, double from);
    // Where near `nominal` (file frames) a slice taking over from the sound
    // at `continuing` best lines up with it (the waveforms alike).
    [[nodiscard]] double alignedCut(StretchSource& source, double continuing, double nominal);
    void restartAt(std::int64_t intermediate);
    void keepFrom(std::int64_t intermediate);
    // The stretched sound's frame an output frame reads, and back (the
    // pitch curve's running sum; without one, a constant step).
    [[nodiscard]] double intermediateAt(double output) const noexcept;
    [[nodiscard]] double outputAt(double intermediate) const noexcept;
    // The file's frame an output frame stands for in time.
    [[nodiscard]] double sourceAtOutput(double output) const noexcept;
    [[nodiscard]] float sincAt(int channel, double position) const noexcept;
    [[nodiscard]] bool direct() const noexcept { return direct_; }

    StretchSettings settings_;
    bool direct_ {};          // no phase vocoder: the file itself, resampled
    bool copy_ {};            // not even resampled
    double step_ { 1.0 };     // stretched frames per output frame
    // With a pitch curve: the stretched frame reached at every knot (one
    // every curveFrames_ output frames), and the steps before and after.
    std::vector<double> reached_;
    double curveFrames_ { 1.0 };
    double firstStep_ { 1.0 };
    double lastStep_ { 1.0 };
    double cutoff_ { 1.0 };   // the resampler's, under 1 when reading faster
    int reach_ { 16 };        // the resampler's half width, in stretched frames
    std::int64_t expected_ { -1 };

    // The stretched sound: frames [base_, base_ + size) held, final below
    // complete_.
    std::vector<std::vector<float>> stretched_;
    std::int64_t base_ {};
    std::int64_t complete_ {};
    std::size_t capacity_ {};

    // The phase vocoder.
    int size_ { 4'096 };       // its window
    int hop_ { 1'024 };        // between windows of the stretched sound
    double norm_ { 1.0 };
    std::int64_t nextFrame_ {};
    bool havePrevious_ {};
    std::int64_t previousStart_ {};
    std::vector<float> window_;
    std::vector<std::complex<float>> twiddles_;
    std::vector<std::size_t> reversed_;
    std::vector<std::vector<float>> read_;                 // a window of the file, per channel
    std::vector<std::vector<std::complex<float>>> spectra_; // per channel
    std::vector<std::complex<float>> reference_;
    std::vector<float> magnitude_, previousMagnitude_;
    std::vector<float> analysisPhase_, previousAnalysisPhase_, synthesisPhase_;
    std::vector<int> peaks_, owner_;
    float previousHighEnergy_ {};
    std::uint32_t random_ { 0x9e3779b9u };
    double textureGain_ { 1.0 };

    // beats: the slice being laid out (its attack in the file, where it
    // starts in the stretched sound) and the next one's.
    bool slices_ {};
    double sliceStart_ {};
    std::int64_t sliceAt_ {};
    double nextSlice_ {};
    std::int64_t nextSliceAt_ {};
    bool sliceFadesIn_ {};
    bool nextIsAttack_ {};
    // A slice cut where no attack is (a held sound) crossfades from the one
    // before, which plays on under it a moment.
    bool crossfading_ {};
    double previousSlice_ {};
    std::int64_t previousSliceAt_ {};
    std::vector<std::vector<float>> fade_;
    std::vector<float> scan_;
    std::vector<std::vector<float>> scanChannels_;
    // The sinc's shape, sampled finely.
    std::vector<float> kernel_;
};

// A loop (`beats` long at `sourceBpm`) played at `tempo`: exactly `beats`
// beats of it at the output's rate, stretched (its pitch kept, but with
// repitch), seamless when played over and over. `channels` holds the
// file's channels, `sourceRate` frames a second; the loop is the file's
// first `beats` beats (shorter, silence after it). Allocates: off the
// audio callback.
[[nodiscard]] std::vector<std::vector<float>> renderStretchedLoop(const std::vector<std::vector<float>>& channels,
                                                                  double sourceRate, double sourceBpm, int beats,
                                                                  double tempo, double outputRate,
                                                                  StretchAlgorithm algorithm);

} // namespace cydaw::engine
