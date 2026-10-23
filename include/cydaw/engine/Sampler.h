#pragma once

#include <cydaw/engine/SoundModel.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace cydaw::engine {

// The sampler: recorded notes played back as they are, each over the keys and
// velocities it is mapped to, at the pitch the key asks for. A sample map is
// a list of zones (SFZ's regions); every zone a note falls in plays, stacked,
// so velocity layers, key splits and layered sounds are the same thing.
//
// The audio of a file is decoded off the audio thread (the host's SampleStore)
// into a SampleData shared by every zone and every track that plays it. A
// note that starts before its sample is decoded waits for it, then comes in
// where it would be by then, faded in over a few milliseconds.
//
// An instrument made from an SFZ plays as it moves: the MIDI controllers its
// zones read (a microphone's fader, a blend, the hi-hat's pedal) choose and
// level them while they play, its keyswitches choose an articulation, its
// release zones sound when a key is let go.
//
// A long recording is read from the disk while it plays: only its head is in
// memory (from where its zone starts, through its loop), and a lane that goes
// past it reads a ring (SampleStream) that the host's streamer fills ahead.

// How many deferred samples have been asked for so far: the host's loaders
// look again when it moves.
inline std::atomic<std::uint32_t> sampleRequests { 0 };

// A thread that renders faster than real time (an offline check, a host's
// offline bounce) sets it: a lane that needs frames still on the disk waits
// for them instead of going on without.
inline thread_local bool samplerMayWait = false;

// One decoded file: 16-bit frames, interleaved, at most two channels. A
// loader fills `pcm`, then sets `ready` (release); a voice reads only after
// seeing it (acquire), so nothing is locked on the audio thread.
struct SampleData {
    std::string path;
    int channels { 1 };
    // The whole file's.
    std::int64_t frames {};
    double sampleRate { 48'000.0 };
    // pcm * scale is the sample's value: the file normalised into 16 bits,
    // its own level kept here.
    float scale { 1.0f / 32'767.0f };
    // Every frame; read from the disk (streamed): frames [heldFirst,
    // heldFirst + heldFrames) only, the rest streamed while it plays.
    std::vector<std::int16_t> pcm;
    bool streamed {};
    std::int64_t heldFirst {};
    std::int64_t heldFrames {};
    // A loop the file itself carries (a WAV's sampler chunk); -1: none.
    std::int64_t fileLoopStart { -1 };
    std::int64_t fileLoopEnd { -1 };
    std::atomic<bool> ready { false };
    std::atomic<bool> failed { false };
    // Not decoded until something needs it (a zone the instrument's
    // controllers or articulation do not reach as it opens): a note or a
    // setting that reaches it asks for it, and the loaders take it then.
    std::atomic<bool> deferred { false };
    std::atomic<bool> wanted { false };

    [[nodiscard]] bool playable() const noexcept { return ready.load(std::memory_order_acquire) && frames > 0; }
    // The frames in memory: [heldStart(), heldEnd()).
    [[nodiscard]] std::int64_t heldStart() const noexcept { return streamed ? heldFirst : 0; }
    [[nodiscard]] std::int64_t heldEnd() const noexcept { return streamed ? heldFirst + heldFrames : frames; }
    [[nodiscard]] bool holds(const std::int64_t frame) const noexcept { return frame >= heldStart() && frame < heldEnd(); }
    // A frame in memory, its channel (0 for a frame not held).
    [[nodiscard]] std::int16_t held(const std::int64_t frame, const int channel) const noexcept
    {
        if (!holds(frame))
            return 0;
        const auto at = static_cast<std::size_t>(frame - heldStart()) * static_cast<std::size_t>(channels)
                        + static_cast<std::size_t>(std::min(channel, channels - 1));
        return at < pcm.size() ? pcm[at] : std::int16_t {};
    }
    // Waiting to be asked for.
    [[nodiscard]] bool asleep() const noexcept
    {
        return deferred.load(std::memory_order_acquire) && !wanted.load(std::memory_order_acquire);
    }
    // Asks for it (from any thread, without a lock).
    void request() noexcept
    {
        if (deferred.load(std::memory_order_acquire) && !wanted.exchange(true, std::memory_order_acq_rel))
            sampleRequests.fetch_add(1, std::memory_order_release);
    }
};
using SampleDataRef = std::shared_ptr<SampleData>;

// ---- Reading from the disk --------------------------------------------------------------

// A long recording read while a lane plays it: a ring the host's streamer
// fills ahead of where the lane reads (behind it, played backwards). Frames
// [low, high) are in it, 16 bits at full scale, two channels a frame.
struct SampleStream {
    static constexpr std::int64_t capacity = 16'384;
    // The streamer keeps this many frames behind the lane untouched.
    static constexpr std::int64_t margin = 512;
    enum State : int { free, claiming, wanted, filling, letGo };
    std::atomic<int> state { free };
    // Set by the lane that claims it; let go by the streamer.
    SampleDataRef sample;
    // The first frame wanted (played backwards: the one after it).
    std::int64_t from {};
    int direction { 1 };
    // Where the lane reads now.
    std::atomic<std::int64_t> reading {};
    std::atomic<std::int64_t> low {};
    std::atomic<std::int64_t> high {};
    // The streamer could not open its file (moved, deleted, a drive gone):
    // nothing will come, so a lane does not wait for it.
    std::atomic<bool> unreadable {};
    std::vector<std::int16_t> ring;

    [[nodiscard]] bool has(const std::int64_t frame) const noexcept
    {
        return frame >= low.load(std::memory_order_acquire) && frame < high.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::int16_t* cell(const std::int64_t frame) noexcept
    {
        return ring.data() + static_cast<std::size_t>(((frame % capacity) + capacity) % capacity) * 2;
    }
    [[nodiscard]] const std::int16_t* cell(const std::int64_t frame) const noexcept
    {
        return ring.data() + static_cast<std::size_t>(((frame % capacity) + capacity) % capacity) * 2;
    }
};

// Every ring there is, shared by every track: claimed by the lanes on the
// audio thread without a lock, filled by the host's streamer.
class SampleStreams final {
public:
    static SampleStreams& instance();
    // Off the audio thread, once (the first long recording): `count` rings.
    void reserve(int count);
    // A ring for that sample from `from` on (backwards: before it), or null
    // when every one is taken (or none was reserved).
    [[nodiscard]] SampleStream* claim(const SampleDataRef& sample, std::int64_t from, int direction) noexcept;
    void letGo(SampleStream* stream) noexcept;
    [[nodiscard]] int size() const noexcept { return count_.load(std::memory_order_acquire); }
    [[nodiscard]] SampleStream& at(const int index) noexcept { return streams_[static_cast<std::size_t>(index)]; }
    [[nodiscard]] int busy() const noexcept;
    // Frames a lane played without them (the disk too slow), rings asked
    // for when none was free.
    std::atomic<std::uint64_t> underruns {};
    std::atomic<std::uint64_t> refused {};

private:
    std::unique_ptr<SampleStream[]> streams_;
    std::atomic<int> count_ {};
    std::atomic<bool> reserving_ {};
};

// A MIDI controller's say in a zone (SFZ's opcodes on a CC), read as the
// controller moves. The amount is what the controller does at its top; a
// curve (the map's) bends the way there.
enum class SamplerRuleKind : std::uint8_t {
    range,   // the zone plays when the controller is within [a, b] at the note-on (locc/hicc)
    level,   // its level times a % (amplitude_oncc)
    volume,  // a dB (volume_oncc, gain_oncc)
    fadeIn,  // silent at a, whole at b, equal power (xfin_locc/hicc)
    fadeOut, // whole at a, silent at b (xfout_locc/hicc)
    pan,     // a (-1..1) added to its pan, at the note-on (pan_oncc)
    tune,    // a cents, at the note-on (tune_oncc, pitch_oncc)
    offset,  // a frames later into the file, at the note-on (offset_oncc)
};
struct SamplerRule {
    SamplerRuleKind kind { SamplerRuleKind::range };
    std::uint8_t controller {};
    // The map's curve it follows, -1: straight (0 at 0, 1 at 127).
    std::int16_t curve { -1 };
    float a {};
    float b {};

    [[nodiscard]] bool operator==(const SamplerRule&) const = default;
};

// When a zone sounds.
enum class SamplerTrigger : std::uint8_t {
    attack,  // a key pressed
    release, // a key let go: the sound of letting go (a damper, a fret)
    first,   // a key pressed with no other held
    legato,  // a key pressed while another is held
};

struct SamplerZone {
    SampleDataRef sample;
    int rootNote { 60 };
    int lowKey {};
    int highKey { 127 };
    int lowVelocity { 1 };
    int highVelocity { 127 };
    float tuneCents {};
    float gainDb {};
    // -1 left .. 1 right.
    float pan {};
    // Frames of the file. end -1: its last frame.
    std::int64_t start {};
    std::int64_t end { -1 };
    // Held between these while it loops; -1: no loop.
    std::int64_t loopStart { -1 };
    std::int64_t loopEnd { -1 };
    // Frames before loopEnd blended into the frames before loopStart.
    int loopCrossfade {};
    // Frames faded out before the end (a note cut from a longer recording).
    int endFade {};
    // Round robin (SFZ's seq_length / seq_position): with a length above 1,
    // the zone plays one note in `sequenceLength`, the `sequencePosition`th.
    int sequenceLength { 1 };
    int sequencePosition { 1 };
    // Choke (SFZ's group / off_by): a note on a zone of group n cuts the
    // voices playing zones that `offBy` n (an open hat cut by the closed
    // one). 0: none.
    int group {};
    int offBy {};
    SamplerTrigger trigger { SamplerTrigger::attack };
    // A release zone: the decibels it loses per second the key was held
    // (SFZ's rt_decay).
    float releaseDecayDb {};
    // Plays only after this keyswitch (SFZ's sw_last); -1: whatever was chosen.
    int keyswitch { -1 };
    // What the controllers do to it.
    std::vector<SamplerRule> rules;

    [[nodiscard]] bool loops() const noexcept { return loopStart >= 0 && loopEnd > loopStart + 1; }
};

// The controllers' values as MIDI starts them: volume 100, pan 64,
// expression 127, the others 0.
[[nodiscard]] constexpr std::array<float, 128> defaultSamplerControllers() noexcept
{
    std::array<float, 128> values {};
    values[7] = 100.0f;
    values[10] = 64.0f;
    values[11] = 127.0f;
    return values;
}

struct SamplerMap {
    std::string name;
    std::vector<SamplerZone> zones;
    // Which round of the round robin the next note plays; every voice of
    // every track playing this map counts on it.
    mutable std::atomic<std::uint32_t> round {};
    // The controllers as the instrument opens (its set_ccN).
    std::array<float, 128> controllerStart { defaultSamplerControllers() };
    // The controllers a card shows: those the zones read, by their names.
    struct Control {
        int controller {};
        std::string label;
    };
    std::vector<Control> controls;
    // The curves rules follow: 128 values each, one per controller value.
    std::vector<std::array<float, 128>> curves;
    // Keys that choose an articulation instead of playing (sw_lokey..sw_hikey
    // and the zones' sw_last); the one chosen as it opens (-1: none); their
    // names.
    std::array<bool, 128> keyswitchKeys {};
    int keyswitchStart { -1 };
    std::vector<std::pair<int, std::string>> keyswitchLabels;
    bool hasReleaseZones {};

    [[nodiscard]] bool hasKeyswitches() const noexcept { return keyswitchStart >= 0 || !keyswitchLabels.empty(); }
};

// Shared by the plan's copies (one per voice per block); the zones are fixed
// once made, only the samples' data arrives later.
using SamplerMapRef = std::shared_ptr<const SamplerMap>;

// How a map is played: the sampler card's knobs, in this order in its payload
// (NativeDspNodeParameters::sampler).
enum class SamplerSetting : std::uint8_t {
    transpose,         // semitones, -48..48
    fine,              // cents, -100..100
    start,             // ms skipped at the start of every sample, 0..2000
    velocityDb,        // the whole velocity range's level, 0..48 dB
    velocityCrossfade, // 1: the two velocity layers around the note blended
    loop,              // 0 none; 1 the zones' loops, held to the end; 2 looped while the key is down
    keyTrack,          // 1: the pitch follows the key; 0: every key plays the root (drums)
    reverse,           // 1: backwards (loops ignored)
    width,             // stereo width, 0 mono .. 2
    oneShot,           // 1: plays to its end whatever the key does
    articulation,      // the keyswitch chosen on the card, -1: the instrument's own
    simple,            // 1: the simple sampler's card (one sample over the keys, its waveform shown)
    // Granular (Nuée): the sample read as grains rather than played through.
    grains,            // 1: granular
    grainPosition,     // where in the sample the grains are taken, 0..1
    grainScan,         // how fast that place moves through it (1: as recorded, -1 backwards), -2..2
    grainSize,         // a grain's length, ms 10..1000
    grainDensity,      // grains a second, 1..120
    grainSpray,        // how far around the place they are taken, 0..1 of the sample
    grainPitch,        // random pitch of each grain, semitones 0..12
    grainStereo,       // how far they spread in the stereo field, 0..1
    grainReverse,      // the chance a grain plays backwards, 0..1
    grainShape,        // its window: 0 soft (bell) .. 1 hard (square edges)
    count
};
inline constexpr std::size_t samplerSettingCount = static_cast<std::size_t>(SamplerSetting::count);

inline constexpr std::array<SoundModelSettingInfo, samplerSettingCount> samplerSettings { {
    { "transpose", "sampler.transpose", "sampler.parameter.transpose", "st", -48.0f, 48.0f, 0.0f },
    { "fine", "sampler.fine", "sampler.parameter.fine", "c", -100.0f, 100.0f, 0.0f },
    { "start", "sampler.start", "sampler.parameter.start", "ms", 0.0f, 2'000.0f, 0.0f },
    { "velocity-db", "sampler.velocityDb", "sampler.parameter.velocityDb", "dB", 0.0f, 48.0f, 24.0f },
    { "velocity-crossfade", "sampler.velocityCrossfade", "sampler.parameter.velocityCrossfade", "", 0.0f, 1.0f, 0.0f },
    { "loop", "sampler.loop", "sampler.parameter.loop", "", 0.0f, 2.0f, 1.0f },
    { "key-track", "sampler.keyTrack", "sampler.parameter.keyTrack", "", 0.0f, 1.0f, 1.0f },
    { "reverse", "sampler.reverse", "sampler.parameter.reverse", "", 0.0f, 1.0f, 0.0f },
    { "width", "sampler.width", "sampler.parameter.width", "", 0.0f, 2.0f, 1.0f },
    { "one-shot", "sampler.oneShot", "sampler.parameter.oneShot", "", 0.0f, 1.0f, 0.0f },
    { "articulation", "sampler.articulation", "sampler.parameter.articulation", "", -1.0f, 127.0f, -1.0f },
    { "simple", "sampler.simple", "sampler.parameter.simple", "", 0.0f, 1.0f, 0.0f },
    { "grains", "sampler.grains", "sampler.parameter.grains", "", 0.0f, 1.0f, 0.0f },
    { "grain-position", "sampler.grainPosition", "sampler.parameter.grainPosition", "", 0.0f, 1.0f, 0.3f },
    { "grain-scan", "sampler.grainScan", "sampler.parameter.grainScan", "", -2.0f, 2.0f, 0.0f },
    { "grain-size", "sampler.grainSize", "sampler.parameter.grainSize", "ms", 10.0f, 1'000.0f, 120.0f },
    { "grain-density", "sampler.grainDensity", "sampler.parameter.grainDensity", "Hz", 1.0f, 120.0f, 24.0f },
    { "grain-spray", "sampler.grainSpray", "sampler.parameter.grainSpray", "", 0.0f, 1.0f, 0.05f },
    { "grain-pitch", "sampler.grainPitch", "sampler.parameter.grainPitch", "st", 0.0f, 12.0f, 0.0f },
    { "grain-stereo", "sampler.grainStereo", "sampler.parameter.grainStereo", "", 0.0f, 1.0f, 0.5f },
    { "grain-reverse", "sampler.grainReverse", "sampler.parameter.grainReverse", "", 0.0f, 1.0f, 0.0f },
    { "grain-shape", "sampler.grainShape", "sampler.parameter.grainShape", "", 0.0f, 1.0f, 0.2f },
} };

using SamplerValues = std::array<float, 32>;

[[nodiscard]] constexpr SamplerValues defaultSamplerValues() noexcept
{
    SamplerValues values {};
    for (std::size_t index = 0; index < samplerSettingCount; ++index)
        values[index] = samplerSettings[index].defaultValue;
    return values;
}

[[nodiscard]] constexpr int samplerSettingIndex(const std::string_view key) noexcept
{
    for (std::size_t index = 0; index < samplerSettingCount; ++index)
        if (samplerSettings[index].key == key || samplerSettings[index].suffix == key)
            return static_cast<int>(index);
    return -1;
}

[[nodiscard]] inline float samplerValue(const SamplerValues& values, const SamplerSetting setting) noexcept
{
    return values[static_cast<std::size_t>(setting)];
}

// A card's controllers (its "sampler.ccN" knobs): 0..127, -1 where the card
// leaves the controller where the map starts it.
using SamplerControllers = std::array<float, 128>;

[[nodiscard]] constexpr SamplerControllers unsetSamplerControllers() noexcept
{
    SamplerControllers values {};
    for (auto& value : values)
        value = -1.0f;
    return values;
}

// What a track's MIDI has told its sampler so far, shared by its voices: the
// controllers, the last keyswitch pressed, the keys held. The host's
// synthesiser keeps it, on the audio thread.
struct SamplerPlaying {
    SamplerPlaying() noexcept
    {
        controllers = defaultSamplerControllers();
        followed.fill(std::numeric_limits<float>::quiet_NaN());
    }

    std::array<float, 128> controllers {};
    // The card's (or the map's) values follow() last took.
    std::array<float, 128> followed {};
    float followedArticulation { std::numeric_limits<float>::quiet_NaN() };
    bool following {};
    // The keyswitch the next notes play, -1: the card's or the map's.
    int keyswitch { -1 };
    // Keys held when the note now starting was pressed (first and legato zones).
    int heldKeys {};

    // Once a block: a controller the card (or a new map) moved since the
    // last block goes where the card says; the others stay where MIDI took
    // them. The card's articulation the same way.
    void follow(const SamplerMap* map, const SamplerControllers& card, float articulation) noexcept;
    // A controller message; 121 (reset all controllers) puts every
    // controller back where the card has it.
    void controllerMoved(int controller, int value) noexcept;
};

// What a note is chosen and played with beyond its key and velocity: the
// card's controllers and articulation, the track's MIDI. Null: as the map
// opens.
struct SamplerCues {
    const SamplerControllers* card {};
    const SamplerPlaying* playing {};
    float articulation { -1.0f };

    [[nodiscard]] float controller(const SamplerMap& map, int number) const noexcept;
    [[nodiscard]] int keyswitch(const SamplerMap& map) const noexcept;
    [[nodiscard]] int heldKeys() const noexcept { return playing != nullptr ? playing->heldKeys : 0; }
};

// A rule's value for a controller at `value` (0..127): its curve's, else
// straight from 0 to 1.
[[nodiscard]] float samplerCurve(const SamplerMap& map, int curve, float value) noexcept;
// Whether a zone's controller ranges hold now.
[[nodiscard]] bool samplerRangesHold(const SamplerMap& map, const SamplerZone& zone, const SamplerCues& cues) noexcept;
// Whether a zone can sound with the controllers and articulation where they
// are: its keyswitch chosen, its ranges holding, its levels above nothing (a
// crossfade counts as reachable: it is played moving).
[[nodiscard]] bool samplerZoneReachable(const SamplerMap& map, const SamplerZone& zone, const SamplerCues& cues) noexcept;
// A zone's gain from its controllers now (levels, volumes, crossfades), linear.
[[nodiscard]] double samplerRulesGain(const SamplerMap& map, const SamplerZone& zone, const SamplerCues& cues) noexcept;

// The zones a note plays and their weights. Without the crossfade: every
// zone whose keys and velocities hold the note (round robin, keyswitch,
// controllers and trigger applied). With it: of the zones holding the key,
// the two velocity layers around the velocity, blended at equal power (each
// layer every zone of its range: microphones recorded together). `round` is
// the note's round-robin count.
struct SamplerZoneChoice {
    static constexpr int capacity = 8;
    std::array<int, capacity> zones { -1, -1, -1, -1, -1, -1, -1, -1 };
    std::array<double, capacity> weights {};
    int count {};
};
[[nodiscard]] SamplerZoneChoice chooseSamplerZones(const SamplerMap& map, int midiNote, double velocity,
                                                   bool crossfade, std::uint32_t round, const SamplerCues& cues = {},
                                                   SamplerTrigger trigger = SamplerTrigger::attack) noexcept;
// The choke group a note would sound in (its zones' first), 0: none.
[[nodiscard]] int samplerGroupOf(const SamplerMap& map, int midiNote, double velocity, bool crossfade,
                                 const SamplerCues& cues = {}) noexcept;
// Whether a key chooses an articulation instead of playing.
[[nodiscard]] inline bool samplerKeyswitch(const SamplerMap& map, const int midiNote) noexcept
{
    return midiNote >= 0 && midiNote < 128 && map.keyswitchKeys[static_cast<std::size_t>(midiNote)];
}

// One voice playing a map, in stereo. Nothing here allocates.
class SamplerVoice final {
public:
    SamplerVoice() = default;
    SamplerVoice(const SamplerVoice&) = delete;
    SamplerVoice& operator=(const SamplerVoice&) = delete;
    ~SamplerVoice() { letGoStreams(); }

    void reset() noexcept;
    // The map is read again every block from the plan (an edit may replace
    // it); the zones chosen here are kept by their index.
    void start(const SamplerMap& map, const SamplerValues& values, double midiNote, double velocity,
               double sampleRate, const SamplerCues& cues = {}) noexcept;
    // Key up: a loop held only while the key is down lets go, and the map's
    // release zones sound (`sounds` false: a note cut, not let go: none).
    void release(bool sounds = true) noexcept;
    [[nodiscard]] bool active() const noexcept { return active_; }
    // Only the release's zones left playing.
    [[nodiscard]] bool tailing() const noexcept;
    // The note's own zones stop (its envelope has closed); the release's go on.
    void endNote() noexcept;
    // `tailLeft`/`tailRight`: the release's zones, which the note's envelope
    // does not shape.
    void next(const SamplerMap* map, const SamplerValues& values, const SamplerCues& cues, double& left, double& right,
              double& tailLeft, double& tailRight) noexcept;
    // Its rings given back (the voice is done with them).
    void letGoStreams() noexcept;
    // Everything in one pair.
    void next(const SamplerMap* map, const SamplerValues& values, double& left, double& right) noexcept;
    // The pitch moved along the note (a note's expression), as a ratio.
    void setPitchRatio(const double ratio) noexcept { pitchRatio_ = ratio; }
    // The group whose notes cut this one (its zones' first), 0: none.
    [[nodiscard]] int offBy(const SamplerMap& map) const noexcept;

private:
    static constexpr int laneCapacity = SamplerZoneChoice::capacity + 4;
    struct Lane {
        int zone { -1 };
        const SampleData* sample {};
        double position {};
        double increment {};
        double semitones {};
        double gainLeft {};
        double gainRight {};
        // Frames further into the file (a controller's offset).
        std::int64_t offset {};
        // Its controllers' gain, eased toward where they are.
        double rulesGain { 1.0 };
        double rulesTarget { 1.0 };
        bool moves {};
        // Its sample not decoded yet: the output samples waited so far.
        bool pending {};
        double waited {};
        // 0..1 while a late lane comes in.
        double fadeIn { 1.0 };
        bool looping {};
        bool reversed {};
        // A release zone's.
        bool tail {};
        bool done { true };
        // A long recording past what is in memory: its ring; whether it has
        // sounded yet (until then it waits for the disk instead of going on).
        SampleStream* stream {};
        // Every ring was taken when it asked: it does not ask again.
        bool noStream {};
        bool sounded {};
        double streamWaited {};
    };
    // Its ring for where it reads next, claimed when it needs one.
    void follow(Lane& lane, const SamplerZone& zone) noexcept;
    // Whether the frames around a position are there (in memory or its ring).
    [[nodiscard]] static bool framesThere(const Lane& lane, const SampleData& sample, double position) noexcept;
    void finish(Lane& lane) noexcept;
    // Where a lane starts reading once its sample is there, `elapsed` output
    // samples after the note-on (0: on time).
    void place(Lane& lane, const SamplerZone& zone, const SampleData& sample, double elapsed) const noexcept;
    // Lanes for the chosen zones; false when there is no room left.
    void addLanes(const SamplerMap& map, const SamplerZoneChoice& choice, const SamplerValues& values,
                  const SamplerCues& cues, double gain, bool tail) noexcept;
    void startRelease(const SamplerMap& map, const SamplerValues& values, const SamplerCues& cues) noexcept;

    std::array<Lane, laneCapacity> lanes_ {};
    int laneCount_ {};
    bool active_ {};
    // The note-on's settings, for lanes placed late and for its release.
    double rate_ { 48'000.0 };
    double note_ { 60.0 };
    double velocity_ {};
    std::uint32_t round_ {};
    double skippedMs_ {};
    int loopMode_ {};
    bool reversed_ {};
    // The loop setting at the note-on said: loop while the key is down.
    bool letGoOnRelease_ {};
    bool released_ {};
    bool releasePending_ {};
    // Output samples since the note-on (a release zone's decay).
    double played_ {};
    int rulesCountdown_ {};
    double rulesEase_ { 0.01 };
    double pitchRatio_ { 1.0 };

    // A granular note: grains read from its first zone's sample.
    struct Grain {
        double position {};
        double increment {};
        double age {};
        double length { 1.0 };
        double gainLeft {};
        double gainRight {};
        bool active {};
    };
    static constexpr int grainCapacity = 64;
    void nextGrains(const SamplerMap& map, const SamplerValues& values, double& left, double& right) noexcept;
    [[nodiscard]] double grainRandom() noexcept;
    std::array<Grain, grainCapacity> grains_ {};
    bool granular_ {};
    double grainCountdown_ {};
    double grainScan_ {};
    std::uint32_t grainSeed_ { 0x9e3779b9u };
};

} // namespace cydaw::engine
