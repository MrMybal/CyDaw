#pragma once

#include "cydaw/engine/FoundrySound.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// The Foundry's work: tracks, each holding sounds (clips) laid on a timeline,
// mixed with their gains, pans, fades, mutes and solos. A clip plays a stretch
// of its own sound (trimmed without losing the rest); an edit on a stretch of
// time and some tracks rewrites only what it covers of each clip there.
// Everything is a value: an edit makes a new session sharing the sounds it
// did not touch, so undoing keeps whole sessions.

namespace cydaw::engine::foundry {

struct Note {
    std::uint64_t id{};
    std::int64_t start{}, length{}; // frames relative to the clip's source
    int key{60};
    float velocity{0.8f};
};
struct AutomationPoint { std::int64_t frame{}; float value{}; };
struct AutomationLane {
    std::string binding; // empty: fader in dB, otherwise a native graph binding
    std::vector<AutomationPoint> points;
};
[[nodiscard]] float automationValue(const AutomationLane&, double frame, float fallback = 0) noexcept;

struct Clip {
    std::uint64_t id {};
    std::shared_ptr<const Sound> sound;
    std::int64_t start {};    // on the timeline, the session's frames
    std::int64_t offset {};   // where in its sound it starts
    std::int64_t length {};   // how much of it plays
    std::int64_t fadeIn {};
    std::int64_t fadeOut {};
    float gainDb {};
    std::string name;         // UTF-8
    std::string file;         // the file it came from (UTF-8), "" for none
    std::vector<Note> notes;
    bool edited {};           // its sound differs from that file

    [[nodiscard]] std::int64_t end() const noexcept { return start + length; }
};

// Cinema grouping is presentation metadata; group tracks are real mix buses.
struct Picture {
    std::string file;
    double start{}, sourceIn{}, duration{}, fps{25.0};
};
struct Track {
    std::uint64_t id {};
    std::string name;
    std::vector<Clip> clips;
    float gainDb {};
    float pan {};             // -1 left .. 1 right
    bool mute {};
    bool solo {};
    bool midi{};
    bool bus{};
    std::vector<AutomationLane> automation;
    bool virtualGroup{};
    std::uint64_t parentId{};
    // Cinema: a timed subtrack container. Clips retain absolute session positions.
    std::int64_t containerStart{}, containerLength{};
};

struct Session {
    double rate { 48'000.0 };
    double tempo{120.0};
    std::vector<AutomationLane> masterAutomation;
    std::vector<Track> tracks;
    std::uint64_t nextId { 1 };
    // How long it is at least (a copied stretch keeps its silence).
    std::int64_t span {};
    bool cinema{};
    Picture picture;

    // The end of its last clip (or its span).
    [[nodiscard]] std::int64_t frames() const noexcept;
    [[nodiscard]] std::uint64_t newId() noexcept { return nextId++; }
    [[nodiscard]] int trackIndex(std::uint64_t id) const noexcept;
    // The track and the place of a clip, or false.
    [[nodiscard]] bool findClip(std::uint64_t id, int& track, int& clip) const noexcept;
};

// Which tracks an edit acts on: a flag per track (missing ones: off).
using TrackSet = std::vector<bool>;
[[nodiscard]] inline bool inSet(const TrackSet& set, const std::size_t track) noexcept
{
    return track < set.size() && set[track];
}

// The gain of a clip at a frame of its own (0 .. length): its fades and its gain.
[[nodiscard]] float clipGain(const Clip& clip, std::int64_t frame) noexcept;

// The mix (stereo) of frames [start, start + count): the tracks heard (the
// solos if any, never the muted), among `only` when given. Nothing
// allocated: `scratch` holds at least 2 × count floats.
void mix(const Session& session, std::int64_t start, int count, float* left, float* right, float* scratch,
         const TrackSet* only = nullptr) noexcept;
// One track before its fader, pan, mute and solo; for the shared realtime mixer.
void mixTrackRaw(const Track& track, std::int64_t start, int count, float* left, float* right, float* scratch) noexcept;
// The same, rendered whole as a sound (offline).
[[nodiscard]] Sound render(const Session& session, std::int64_t start, std::int64_t end, const TrackSet* only = nullptr);

// ---- Edits ---------------------------------------------------------------------------------

// A file (its sound) as a new clip at `at` on `track` (a new track when
// track is the count of tracks). Its rate is made the session's.
[[nodiscard]] Session withClip(const Session& session, int track, std::int64_t at, Sound sound, const std::string& name,
                               const std::string& file);
[[nodiscard]] Session withTrack(const Session& session, const std::string& name);
[[nodiscard]] Session withoutTrack(const Session& session, int track);

// Frames [start, end) of the tracks in `tracks`, as a session of their own
// (as many tracks, the stretch at 0): what a copy takes. Inside a clip,
// removing, clearing and pasting one sound change that clip's own sound:
// it stays one clip (a sound file stays a sound file).
[[nodiscard]] Session excerpt(const Session& session, std::int64_t start, std::int64_t end, const TrackSet& tracks);
// Taken out; the rest of those tracks comes back to close the gap.
[[nodiscard]] Session removed(const Session& session, std::int64_t start, std::int64_t end, const TrackSet& tracks);
// Made silent, the clips cut around it, the time kept.
[[nodiscard]] Session cleared(const Session& session, std::int64_t start, std::int64_t end, const TrackSet& tracks);
// Only that stretch kept, at 0, on every track.
[[nodiscard]] Session cropped(const Session& session, std::int64_t start, std::int64_t end);
// What `piece` holds put at `at`, its tracks onto the tracks from `first`
// on (more made if needed); what follows there moves on (or not, `mixed`:
// laid over).
[[nodiscard]] Session pasted(const Session& session, std::int64_t at, const Session& piece, int first, bool mixed);
// Silence put at `at` on those tracks: into the clip there (still one
// clip), what follows moved on.
[[nodiscard]] Session withSilence(const Session& session, std::int64_t at, std::int64_t frames, const TrackSet& tracks);
// The clips under `at` on those tracks cut in two.
[[nodiscard]] Session split(const Session& session, std::int64_t at, const TrackSet& tracks);

// A process over [start, end) of those tracks: for every clip there, the part
// of its sound under the stretch handed to `process` (the sound, its frames
// [from, to), the timeline frame of `from`), which gives the sound back,
// changed — longer or shorter too: what follows on its track moves along.
using ClipProcess = std::function<Sound(const Sound& sound, std::int64_t from, std::int64_t to, std::int64_t timeline)>;
[[nodiscard]] Session processed(const Session& session, std::int64_t start, std::int64_t end, const TrackSet& tracks,
                                const ClipProcess& process);
// Every clip at another rate (the session's too), the timeline scaled.
[[nodiscard]] Session withRate(const Session& session, double rate);

} // namespace cydaw::engine::foundry
