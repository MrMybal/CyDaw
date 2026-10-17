#pragma once

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

// The DJ mix's sound, apart from any interface:
//
// * a deck plays a track decoded whole at any speed: read faster or slower
//   (its pitch follows, as a turntable's does), or with its key kept (time
//   stretched by overlapping grains, each one placed where it best matches
//   the sound the last one left off — a WSOLA). Backwards and stopped dead
//   too, for scratching. Jumps and loop ends are crossfaded.
// * a channel: trim, a three-band isolator EQ (Linkwitz-Riley crossovers at
//   250 Hz and 3 kHz: the bands add back to the sound, and each goes down
//   to nothing), a filter that is a low-pass on the left of its knob and a
//   high-pass on its right.
// * an effect per deck, in time with its beat: echo, reverb, flanger,
//   phaser, filter LFO, gater, beat masher, bitcrusher.
// * what an analysis finds in a track: its tempo and beat grid, its key,
//   its loudness, a waveform coloured by bass, mids and highs.
//
// Nothing allocates in render() and process(): prepare() sizes everything.

namespace cydaw::engine::dj {

// A track decoded whole, at its own rate: one or two channels.
struct TrackAudio {
    std::vector<std::vector<float>> channels;
    double sampleRate { 44'100.0 };

    [[nodiscard]] std::int64_t frames() const noexcept
    {
        return channels.empty() ? 0 : static_cast<std::int64_t>(channels.front().size());
    }
    [[nodiscard]] double seconds() const noexcept { return sampleRate > 0.0 ? static_cast<double>(frames()) / sampleRate : 0.0; }
};

// One column of a track's waveform (columnFrames of its frames): its peak
// and the level of its bass, mids and highs, each 0..1 over the track.
struct WaveColumn {
    float peak {};
    float low {};
    float mid {};
    float high {};
};

struct TrackAnalysis {
    // 0: no steady beat found.
    double bpm {};
    // A beat of the grid, in seconds: the first at or after 0.
    double firstBeat {};
    // 0..11 major (C..B), 12..23 minor (Cm..Bm); -1 none.
    int key { -1 };
    float peak {};
    // Integrated, LUFS.
    double loudness { -70.0 };
    int columnFrames { 128 };
    double columnSeconds { 128.0 / 44'100.0 };
    std::vector<WaveColumn> wave;
};

// The whole analysis (decoded audio; seconds per minute of audio). Tempo
// searched between minimumBpm and maximumBpm.
[[nodiscard]] TrackAnalysis analyse(const TrackAudio& audio, double minimumBpm = 70.0, double maximumBpm = 180.0);
// Its parts. The tempo of an onset envelope (`rate` values a second), the
// grid's first beat in seconds; 0 when nothing steady stands out. The beats
// are laid on `beatOnsets` (the bass's) when they are clear enough.
[[nodiscard]] double detectTempo(const std::vector<float>& onsets, double rate, double minimumBpm, double maximumBpm,
                                 double& firstBeat, const std::vector<float>& beatOnsets = {});
[[nodiscard]] int detectKey(const TrackAudio& audio);
// "Am", "F#"; "" for none.
[[nodiscard]] const char* keyName(int key) noexcept;
// The wheel of fifths' number and letter: "8A" (A minor), "8B" (C major).
[[nodiscard]] const char* camelotName(int key) noexcept;

// ---- Deck --------------------------------------------------------------------------------------

class DeckPlayer {
public:
    // Allocates: off the audio callback.
    void prepare(double outputRate, int maximumBlock);
    void reset() noexcept;
    // The track played (null: none), from its start. Kept by the caller
    // until another replaces it and a block has gone by.
    void setTrack(const TrackAudio* track) noexcept;
    [[nodiscard]] const TrackAudio* track() const noexcept { return track_; }
    // Where it reads, in the track's frames.
    [[nodiscard]] double position() const noexcept { return position_; }
    // Elsewhere at once (crossfaded when playing).
    void jump(double frame) noexcept;
    // Played over and over between two frames; cleared, it plays on.
    void setLoop(double start, double end) noexcept;
    void clearLoop() noexcept;
    [[nodiscard]] bool looping() const noexcept { return loopEnd_ > loopStart_; }
    [[nodiscard]] double loopStart() const noexcept { return loopStart_; }
    [[nodiscard]] double loopEnd() const noexcept { return loopEnd_; }
    // `count` frames at `speed` (1 normal, 0 held, negative backwards),
    // reached from the last block's speed across this one. `keylock`: the
    // pitch kept (forward between 0.3 and 3 times; otherwise read).
    void render(float* left, float* right, int count, double speed, bool keylock) noexcept;

private:
    void renderBlock(float* left, float* right, int count, double from, double to, bool keylock) noexcept;
    void renderDirect(float* left, float* right, int count, double from, double to) noexcept;
    void renderStretched(float* left, float* right, int count, double from, double to) noexcept;
    void primeStretch(double speed) noexcept;
    void produceHop(double speed) noexcept;
    [[nodiscard]] double bestGrain(double natural, double target) const noexcept;
    [[nodiscard]] float read(int channel, double frame) const noexcept;
    [[nodiscard]] float mono(std::int64_t frame) const noexcept;
    [[nodiscard]] double wrapped(double frame) const noexcept;

    const TrackAudio* track_ {};
    double outputRate_ { 48'000.0 };
    int maximumBlock_ { 512 };
    double step_ { 1.0 };       // track frames per output frame at speed 1
    double position_ {};
    double lastSpeed_ {};
    double loopStart_ {};
    double loopEnd_ {};
    // A jump while reading directly: the old place fades out under the new.
    double fadeFrom_ {};
    int fadeLeft_ {};
    int fadeLength_ { 128 };
    // The stretch: grains of window_ output frames, hop_ apart, a Hann
    // window; the last grain's start (track frames); what is ready to go.
    bool stretched_ {};
    int window_ { 2'048 };
    int hop_ { 1'024 };
    int search_ { 512 };
    std::vector<float> hann_;
    std::vector<float> accumulator_[2];
    std::vector<float> ready_[2];
    int readyStart_ {};
    int readyCount_ {};
    double lastGrain_ {};
    bool havePrevious_ {};
    std::vector<float> scratch_[4];
};

// ---- Channel -----------------------------------------------------------------------------------

struct StripSettings {
    float trim { 1.0f };                       // linear
    float low { 1.0f }, mid { 1.0f }, high { 1.0f }; // linear; 0 kills the band
    float filter {};                           // -1 low-pass all the way, 0 off, 1 high-pass all the way
    float resonance { 0.3f };                  // 0..1
};

// An EQ knob (0..1) as a gain: 0 kills, 0.5 is 0 dB, 1 is +6 dB; -26 dB
// just above the kill.
[[nodiscard]] float eqKnobGain(float knob) noexcept;
// A channel fader (0..1) as a gain: a curve close to the ear's.
[[nodiscard]] float faderGain(float position) noexcept;
// The crossfader at `position` (0 left, 1 right): the gains of the decks
// on its left and on its right. `curve` 0: smooth (constant power), 1:
// sharp (both full but at the very ends, for scratching).
[[nodiscard]] std::pair<float, float> crossfaderGains(float position, float curve) noexcept;

class ChannelStrip {
public:
    void prepare(double rate);
    void reset() noexcept;
    // In place.
    void process(float* left, float* right, int count, const StripSettings& settings) noexcept;

    struct Biquad {
        float b0 { 1.0f }, b1 {}, b2 {}, a1 {}, a2 {};
        float z1 {}, z2 {};
        float process(float x) noexcept
        {
            const auto y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

private:
    struct Side {
        Biquad allPass;        // the high crossover's phase, on the bass
        Biquad lowA, lowB;     // LR4 low-pass at the low crossover
        Biquad restA, restB;   // LR4 high-pass at the low crossover
        Biquad midA, midB;     // LR4 low-pass at the high crossover
        Biquad highA, highB;   // LR4 high-pass at the high crossover
        float ic1 {}, ic2 {};  // the filter (a state-variable one)
    };
    double rate_ { 48'000.0 };
    std::array<Side, 2> sides_ {};
    StripSettings last_ {};
    float cutoff_ { 1.0f };    // the filter's, as a fraction of its course (smoothed)
    float wet_ {};             // how much of the filter is heard (smoothed)
    bool highPass_ {};
};

// ---- Effects -----------------------------------------------------------------------------------

enum class EffectKind : int { echo, reverb, flanger, phaser, filterLfo, gater, beatMasher, crusher };
inline constexpr int effectKindCount = 8;

struct EffectInfo {
    const char* key;    // "echo"
    const char* name;   // "Echo"
    const char* first;  // its knobs' names
    const char* second;
};
[[nodiscard]] const EffectInfo& effectInfo(EffectKind kind) noexcept;
// A knob of beats (0..1) on its steps: 1/16, 1/8, 3/16, 1/4, 3/8, 1/2, 3/4, 1,
// 2, 4 beats (the longer for the slower effects).
[[nodiscard]] double beatsOfKnob(float knob, bool slow) noexcept;

struct EffectSettings {
    EffectKind kind { EffectKind::echo };
    bool on {};
    float mix { 0.5f };   // dry/wet: both full at the middle
    float first { 0.5f };
    float second { 0.5f };
};

class DeckEffect {
public:
    void prepare(double rate);
    void reset() noexcept;
    // In place. `beatSeconds`: the deck's beat as it plays (0: none, 120
    // BPM assumed); `beat`: where in it the block starts (beats since the
    // grid's first).
    void process(float* left, float* right, int count, const EffectSettings& settings, double beatSeconds,
                 double beat) noexcept;

private:
    struct Comb {
        std::vector<float> buffer;
        std::size_t at {};
        float store {};
    };
    struct AllPass {
        std::vector<float> buffer;
        std::size_t at {};
    };
    double rate_ { 48'000.0 };
    EffectKind kind_ { EffectKind::echo };
    float dry_ { 1.0f };
    float wet_ {};
    float send_ {};
    // Echo, flanger: a delay line per side.
    std::vector<float> delay_[2];
    std::size_t delayAt_ {};
    float delayFrames_ {};
    float tone_[2] {};
    float toneHigh_[2] {};
    // Reverb: Freeverb's combs and all-passes.
    std::array<Comb, 16> combs_ {};
    std::array<AllPass, 8> allPasses_ {};
    // Phaser.
    std::array<float, 12> phaserState_ {};
    float phaserFeedback_[2] {};
    // Filter LFO.
    float svf_[2][2] {};
    // Beat masher: what it caught.
    std::vector<float> caught_[2];
    int caughtLength_ {};
    int caughtAt_ {};
    int caughtFill_ {};
    bool catching_ {};
    // Bitcrusher.
    float held_[2] {};
    float holdPhase_ {};
    double phase_ {};     // the LFOs', in beats
};

} // namespace cydaw::engine::dj
