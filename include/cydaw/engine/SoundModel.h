#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace cydaw::engine {

// A recorded note made playable: its partials and its noise over time, in
// stereo, and the recording's first moments (its head). The companion
// (tools/companion) analyses a recording into this ("cydaw-sound-model"
// files, .cysound); a voice plays it at any pitch and velocity, in pure DSP:
// no network runs at play time.
//
// tools/companion/cydaw_companion/synth.py is the reference player: the voice
// below follows it step by step, so a model is checked against its source in
// Python and played the same way here.
struct SoundModelZone {
    int rootNote { 60 };
    float f0 { 261.63f };
    float velocity { 0.8f };
    float gain { 1.0f };
    float frameSeconds { 0.01f };
    int frames {};
    // Partial k sits at k f0 sqrt(1 + B k^2).
    float inharmonicity {};
    // The frames the rows below are at, ascending from 0 to frames - 1 (a
    // compact zone: the frames between are read by interpolation); empty: a
    // row for every frame.
    std::vector<int> keyFrames;
    // rows x harmonicCount, the partials' sine peak amplitudes (in stereo,
    // the RMS of the two channels').
    int harmonicCount {};
    std::vector<float> harmonics;
    // bandCount + 1 edges in Hz, and rows x bandCount one-sided noise PSDs
    // (amplitude^2 / Hz; in stereo, the mean of the two channels').
    int bandCount {};
    std::vector<float> noiseEdges;
    std::vector<float> noise;
    // Per row, the pitch's deviation from f0 in cents (vibrato, drift).
    std::vector<float> pitchCents;
    // Stereo (none: both channels the same). At stereoFrames, rows of each
    // partial's left and right gain (from its RMS) and each band's (from its
    // mean power); per partial, the right's phase against the left's; per
    // band, how alike the two noises are (0..1).
    std::vector<int> stereoFrames;
    std::vector<float> partialLeft;
    std::vector<float> partialRight;
    std::vector<float> phaseCos;
    std::vector<float> phaseSin;
    std::vector<float> noiseLeft;
    std::vector<float> noiseRight;
    std::vector<float> noiseCoherence;
    // The recording's first moments, played as they are at headRate; the
    // model comes in over its last headFade samples, its partials there at
    // headPhase (sine, the left channel).
    std::vector<float> headLeft;
    std::vector<float> headRight;
    float headRate { 48'000.0f };
    int headFade {};
    std::vector<float> headPhase;
    // Without a head: the transient the partials miss, played once from the
    // note-on, at attackRate.
    std::vector<float> attack;
    float attackRate { 48'000.0f };
    float attackGain { 1.0f };
    // Held between these frames (ping-pong) while the key is down; -1: none.
    int loopStart { -1 };
    int loopEnd { -1 };
    // Where the recorded release begins; -1: none.
    int release { -1 };

    [[nodiscard]] bool loops() const noexcept { return loopStart >= 0 && loopEnd > loopStart; }
    [[nodiscard]] bool stereo() const noexcept { return !stereoFrames.empty(); }
    [[nodiscard]] int headLength() const noexcept { return static_cast<int>(headLeft.size()); }
    [[nodiscard]] bool hasHead() const noexcept { return headLength() > 2 * std::max(1, headFade); }
    [[nodiscard]] int rows() const noexcept { return keyFrames.empty() ? frames : static_cast<int>(keyFrames.size()); }

    // Two rows of a table and how far between them a frame position falls.
    struct Span {
        std::size_t lower {};
        std::size_t upper {};
        float weight {};
    };
    [[nodiscard]] static Span spanAt(const std::vector<int>& at, const int frameCount, const double position) noexcept
    {
        Span span;
        if (at.empty()) {
            if (frameCount <= 0)
                return span;
            const auto lower = std::clamp(static_cast<int>(std::floor(position)), 0, frameCount - 1);
            span.lower = static_cast<std::size_t>(lower);
            span.upper = static_cast<std::size_t>(std::min(lower + 1, frameCount - 1));
            span.weight = static_cast<float>(std::clamp(position - lower, 0.0, 1.0));
            return span;
        }
        const auto found = std::upper_bound(at.begin(), at.end(), position,
                                            [](const double value, const int frame) { return value < frame; });
        const auto upper = static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(found - at.begin(), 0,
                                                                               static_cast<std::ptrdiff_t>(at.size()) - 1));
        const auto lower = upper > 0 ? upper - 1 : 0;
        span.lower = lower;
        span.upper = upper;
        const auto width = at[upper] - at[lower];
        span.weight = width > 0 ? static_cast<float>(std::clamp((position - at[lower]) / width, 0.0, 1.0)) : 0.0f;
        return span;
    }
    [[nodiscard]] Span rowAt(const double position) const noexcept { return spanAt(keyFrames, frames, position); }
    [[nodiscard]] Span stereoRowAt(const double position) const noexcept { return spanAt(stereoFrames, frames, position); }
    // A table's cell at a span (its rows' values interpolated).
    [[nodiscard]] static float cell(const std::vector<float>& table, const int columns, const Span& span,
                                    const int column) noexcept
    {
        const auto a = table[span.lower * static_cast<std::size_t>(columns) + static_cast<std::size_t>(column)];
        const auto b = table[span.upper * static_cast<std::size_t>(columns) + static_cast<std::size_t>(column)];
        return a + (b - a) * span.weight;
    }
    // Partial k's amplitude (RMS of the channels) at a frame position.
    [[nodiscard]] float harmonicAt(const double position, const int k) const noexcept
    {
        if (k < 0 || k >= harmonicCount || harmonics.empty())
            return 0.0f;
        return cell(harmonics, harmonicCount, rowAt(position), k);
    }
};

struct SoundModel {
    std::string name;
    std::vector<SoundModelZone> zones;
};

// Shared by the plan's copies (one per voice per block); immutable once made.
using SoundModelRef = std::shared_ptr<const SoundModel>;

// How a model is played: the sound-model card's knobs, in this order in its
// payload (NativeDspNodeParameters::soundModel).
enum class SoundModelSetting : std::uint8_t {
    harmonics,          // partials' level, 0..2
    noise,              // noise level, 0..2
    attack,             // transient (or head) level, 0..2
    brightness,         // tilt, dB per octave of harmonic number, -24..24
    formant,            // 0: the spectrum moves with the note; 1: it stays in place
    inharmonicity,      // B's scale, 0..4
    pitchCurve,         // the recorded vibrato and drift, 0..2
    speed,              // how fast the frames go by, 0.25..4
    releaseSeconds,     // key up: < 0 the recorded release (or 0.3 s), else this fade
    noisePitch,         // how far the noise bands follow the note, 0..1
    attackPitch,        // how far the transient follows the note, 0..1
    velocityDb,         // the whole velocity range's level, 0..48 dB
    velocityBrightness, // the whole velocity range's tilt, -12..12 dB/oct
    count
};
inline constexpr std::size_t soundModelSettingCount = static_cast<std::size_t>(SoundModelSetting::count);

struct SoundModelSettingInfo {
    std::string_view key;      // the primitive's parameter key
    std::string_view suffix;   // the binding a card publishes ("model.brightness")
    std::string_view labelKey; // translation key
    std::string_view unit;
    float minimum {};
    float maximum { 1.0f };
    float defaultValue {};
};

inline constexpr std::array<SoundModelSettingInfo, soundModelSettingCount> soundModelSettings { {
    { "harmonics", "model.harmonics", "model.parameter.harmonics", "", 0.0f, 2.0f, 1.0f },
    { "noise", "model.noise", "model.parameter.noise", "", 0.0f, 2.0f, 1.0f },
    { "attack", "model.attack", "model.parameter.attack", "", 0.0f, 2.0f, 1.0f },
    { "brightness", "model.brightness", "model.parameter.brightness", "dB", -24.0f, 24.0f, 0.0f },
    { "formant", "model.formant", "model.parameter.formant", "", 0.0f, 1.0f, 0.0f },
    { "inharmonicity", "model.inharmonicity", "model.parameter.inharmonicity", "", 0.0f, 4.0f, 1.0f },
    { "pitch-curve", "model.pitchCurve", "model.parameter.pitchCurve", "", 0.0f, 2.0f, 1.0f },
    { "speed", "model.speed", "model.parameter.speed", "", 0.25f, 4.0f, 1.0f },
    { "release", "model.release", "model.parameter.release", "s", -1.0f, 10.0f, -1.0f },
    { "noise-pitch", "model.noisePitch", "model.parameter.noisePitch", "", 0.0f, 1.0f, 0.0f },
    { "attack-pitch", "model.attackPitch", "model.parameter.attackPitch", "", 0.0f, 1.0f, 1.0f },
    { "velocity-db", "model.velocityDb", "model.parameter.velocityDb", "dB", 0.0f, 48.0f, 24.0f },
    { "velocity-brightness", "model.velocityBrightness", "model.parameter.velocityBrightness", "dB", -12.0f, 12.0f, 3.0f },
} };

// The settings' storage in a node payload and a plan: one float each, in the
// order above, with room for more.
using SoundModelValues = std::array<float, 16>;

[[nodiscard]] constexpr SoundModelValues defaultSoundModelValues() noexcept
{
    SoundModelValues values {};
    for (std::size_t index = 0; index < soundModelSettingCount; ++index)
        values[index] = soundModelSettings[index].defaultValue;
    return values;
}

[[nodiscard]] constexpr int soundModelSettingIndex(const std::string_view key) noexcept
{
    for (std::size_t index = 0; index < soundModelSettingCount; ++index)
        if (soundModelSettings[index].key == key || soundModelSettings[index].suffix == key)
            return static_cast<int>(index);
    return -1;
}

[[nodiscard]] inline float soundModelValue(const SoundModelValues& values, const SoundModelSetting setting) noexcept
{
    return values[static_cast<std::size_t>(setting)];
}

// The zones a note plays and their weights: the recorded pitches either side
// of the note and, at each, the velocity layers either side of the velocity,
// weighted bilinearly (up to four; one where the note or the velocity is
// beyond those recorded). Count 0: none.
inline constexpr int maximumSoundModelLanes = 4;
struct SoundModelZoneChoice {
    std::array<int, maximumSoundModelLanes> zones { -1, -1, -1, -1 };
    std::array<double, maximumSoundModelLanes> weights {};
    int count {};
};
[[nodiscard]] SoundModelZoneChoice chooseSoundModelZones(const SoundModel& model, double midiNote, double velocity) noexcept;

// One voice playing a model, in stereo. Allocates only in prepare(), off the
// audio thread; the rest runs on it. The zones a note plays sound as one: a
// single bank of partials whose amplitudes, pitch curve and inharmonicity are
// their weighted blend, each zone read at its own frame, with its own loop and
// release (synth.py's render_zones). The heaviest zone's head plays first; the
// model comes in under its end, in phase with it.
class SoundModelVoice final {
public:
    static constexpr int maximumPartials = 96;
    static constexpr int controlBlock = 32;

    void prepare(double sampleRate);
    void reset() noexcept;
    // The model is read again every block from the plan (an edit may replace
    // it); the zones chosen here are kept by their index.
    void start(const SoundModel& model, const SoundModelValues& values, double midiNote, double velocity,
               double sampleRate) noexcept;
    void release() noexcept;
    [[nodiscard]] bool active() const noexcept { return active_; }
    // Both channels of the next sample.
    void nextFrame(const SoundModel* model, const SoundModelValues& values, double sampleRate, double& left,
                   double& right) noexcept;
    // Their mean.
    [[nodiscard]] double nextSample(const SoundModel* model, const SoundModelValues& values, double sampleRate) noexcept;
    // The pitch moved along the note (a note's expression), as a ratio.
    void setPitchRatio(const double ratio) noexcept { pitchRatio_ = ratio; }

private:
    // One zone's own timeline in the voice.
    struct Lane {
        int zone { -1 };
        double weight {};
        double position {};
        double direction { 1.0 };
        double fade { 1.0 };
        double fadeRate { 1.0 };
        double attackPosition {};
        bool released {};
    };

    void renderBlock(const SoundModel& model, const SoundModelValues& values, double rate) noexcept;
    // The blended partials of both channels at the lanes' current frames and
    // fades, the model's share `modelGain`; the right's phase rotation.
    void partialTargets(const SoundModel& model, const SoundModelValues& values, double rate, double modelGain,
                        float* left, float* right, double* increments) noexcept;
    // One noise frame shaped by the lanes' PSDs `ahead` samples from now.
    void shapeNoiseHop(const SoundModel& model, const SoundModelValues& values, double ahead, double rate) noexcept;
    [[nodiscard]] double laneLevel(const SoundModelZone& zone, const Lane& lane, const SoundModelValues& values) const noexcept;
    // Where a lane is `ahead` samples from now (it follows the head's time
    // while the head plays and the key is down).
    [[nodiscard]] double lanePositionAhead(const SoundModel& model, const SoundModelZone& zone, const Lane& lane,
                                           double ahead, double speed, double rate) const noexcept;
    // The model's share where the head is at `position` (its samples): 0
    // under the head, rising over its fade, 1 after (and without a head).
    [[nodiscard]] double modelShare(const SoundModel& model, double position) const noexcept;
    // The partials set to the phases the recording had where the head gives
    // way, the head at `position`.
    void snapToHead(const SoundModel& model, const SoundModelValues& values, double position) noexcept;
    [[nodiscard]] static float randomSample(std::uint32_t& state) noexcept;

    // Noise: two white streams (the right's own), a frame of fftSize_ shaped
    // every hop.
    int fftSize_ {};
    std::vector<float> window_;
    std::vector<std::complex<float>> twiddles_;
    std::vector<std::uint32_t> bitReverse_;
    std::vector<std::complex<float>> spectrum_;
    std::vector<std::complex<float>> spectrumOther_;
    std::vector<std::complex<float>> spectrumRight_;
    std::vector<float> stream_;
    std::vector<float> streamOther_;
    std::vector<float> overlap_;
    std::vector<float> overlapRight_;
    std::vector<float> binPsd_;
    std::vector<float> binPsdRight_;
    std::vector<float> binCoherence_;
    std::vector<float> binWeight_;
    int noiseRead_ {};
    std::uint32_t noiseState_ { 0x9e3779b9u };
    std::uint32_t otherState_ { 0x85ebca6bu };

    std::array<float, controlBlock> blockLeft_ {};
    std::array<float, controlBlock> blockRight_ {};
    int blockRead_ { controlBlock };

    std::array<double, maximumPartials> cos_ {};
    std::array<double, maximumPartials> sin_ {};
    std::array<float, maximumPartials> amplitudeLeft_ {};
    std::array<float, maximumPartials> amplitudeRight_ {};
    std::array<double, maximumPartials> rotationCos_ {};
    std::array<double, maximumPartials> rotationSin_ {};
    int partialCount_ {};

    std::array<Lane, maximumSoundModelLanes> lanes_ {};
    int laneCount_ {};
    // The head: the heaviest lane's zone's, where it is (its samples), how
    // fast it goes, whether the partials took its phases.
    int headLane_ { -1 };
    double headPosition_ {};
    double headStep_ {};
    bool headSnapped_ {};
    bool active_ {};
    // A key up waits for the next block, which knows the settings.
    bool releaseRequested_ {};
    double rate_ { 48'000.0 };
    double note_ {};
    double velocity_ {};
    double frequency_ {};
    double pitchRatio_ { 1.0 };
    std::int64_t samplesPlayed_ {};
};

} // namespace cydaw::engine
