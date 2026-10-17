#include "cydaw/engine/FoundrySession.h"

#include <algorithm>
#include <cmath>

namespace cydaw::engine::foundry {

namespace {

void sortClips(Track& track)
{
    std::stable_sort(track.clips.begin(), track.clips.end(), [](const Clip& a, const Clip& b) { return a.start < b.start; });
}

// A clip kept only over [from, to) of the timeline (empty: length 0).
Clip pieceOf(const Clip& clip, const std::int64_t from, const std::int64_t to)
{
    auto piece = clip;
    const auto a = std::max(from, clip.start);
    const auto b = std::min(to, clip.end());
    const auto cutBefore = a - clip.start;
    const auto cutAfter = clip.end() - b;
    piece.start = a;
    piece.offset = clip.offset + cutBefore;
    piece.length = std::max<std::int64_t>(0, b - a);
    piece.fadeIn = std::clamp<std::int64_t>(clip.fadeIn - cutBefore, 0, piece.length);
    piece.fadeOut = std::clamp<std::int64_t>(clip.fadeOut - cutAfter, 0, piece.length);
    return piece;
}

float panLeft(const float pan) noexcept
{
    return std::min(1.0f, 1.0f - std::clamp(pan, -1.0f, 1.0f));
}

float panRight(const float pan) noexcept
{
    return std::min(1.0f, 1.0f + std::clamp(pan, -1.0f, 1.0f));
}

} // namespace

float automationValue(const AutomationLane& lane, const double frame, const float fallback) noexcept
{
    if (lane.points.empty()) return fallback;
    if (frame <= lane.points.front().frame) return lane.points.front().value;
    for (std::size_t i=1; i<lane.points.size(); ++i) {
        const auto& a=lane.points[i-1]; const auto& b=lane.points[i];
        if (frame <= b.frame) {
            const auto f=static_cast<float>((frame-a.frame)/std::max<std::int64_t>(1,b.frame-a.frame));
            return a.value+(b.value-a.value)*f;
        }
    }
    return lane.points.back().value;
}

std::int64_t Session::frames() const noexcept
{
    std::int64_t end = span;
    for (const auto& track : tracks)
        for (const auto& clip : track.clips)
            end = std::max(end, clip.end());
    for (const auto& lane : masterAutomation) for (const auto& p : lane.points) end=std::max(end,p.frame);
    for (const auto& t : tracks) for (const auto& lane : t.automation) for (const auto& p : lane.points) end=std::max(end,p.frame);
    return end;
}

int Session::trackIndex(const std::uint64_t id) const noexcept
{
    for (std::size_t i = 0; i < tracks.size(); ++i)
        if (tracks[i].id == id)
            return static_cast<int>(i);
    return -1;
}

bool Session::findClip(const std::uint64_t id, int& track, int& clip) const noexcept
{
    for (std::size_t t = 0; t < tracks.size(); ++t)
        for (std::size_t c = 0; c < tracks[t].clips.size(); ++c)
            if (tracks[t].clips[c].id == id) {
                track = static_cast<int>(t);
                clip = static_cast<int>(c);
                return true;
            }
    return false;
}

float clipGain(const Clip& clip, const std::int64_t frame) noexcept
{
    auto gain = clip.gainDb != 0.0f ? std::pow(10.0f, clip.gainDb / 20.0f) : 1.0f;
    if (clip.fadeIn > 0 && frame < clip.fadeIn)
        gain *= static_cast<float>(std::max<std::int64_t>(0, frame)) / static_cast<float>(clip.fadeIn);
    if (clip.fadeOut > 0 && frame > clip.length - clip.fadeOut)
        gain *= static_cast<float>(std::max<std::int64_t>(0, clip.length - frame)) / static_cast<float>(clip.fadeOut);
    return gain;
}

void mix(const Session& session, const std::int64_t start, const int count, float* const left, float* const right,
         float* const scratch, const TrackSet* const only) noexcept
{
    std::fill_n(left, count, 0.0f);
    std::fill_n(right, count, 0.0f);
    if (count <= 0)
        return;
    auto anySolo = false;
    for (const auto& track : session.tracks)
        anySolo = anySolo || track.solo;
    const auto end = start + count;
    for (std::size_t t = 0; t < session.tracks.size(); ++t) {
        const auto& track = session.tracks[t];
        if ((only != nullptr && !inSet(*only, t)) || track.mute || (anySolo && !track.solo))
            continue;
        const auto trackGain = track.gainDb != 0.0f ? std::pow(10.0f, track.gainDb / 20.0f) : 1.0f;
        const auto toLeft = trackGain * panLeft(track.pan);
        const auto toRight = trackGain * panRight(track.pan);
        for (const auto& clip : track.clips) {
            if (clip.sound == nullptr)
                continue;
            const auto a = std::max(start, clip.start);
            const auto b = std::min(end, clip.end());
            if (b <= a)
                continue;
            const auto n = b - a;
            const auto from = a - clip.start;
            auto* first = scratch;
            auto* second = scratch + count;
            const auto& sound = *clip.sound;
            sound.read(0, clip.offset + from, n, first);
            if (sound.channels() > 1)
                sound.read(1, clip.offset + from, n, second);
            else
                std::copy_n(first, n, second);
            const auto at = a - start;
            for (std::int64_t i = 0; i < n; ++i) {
                const auto gain = clipGain(clip, from + i);
                left[at + i] += first[i] * gain * toLeft;
                right[at + i] += second[i] * gain * toRight;
            }
        }
    }
}

void mixTrackRaw(const Track& track, const std::int64_t start, const int count, float* left, float* right, float* scratch) noexcept
{
    std::fill_n(left, count, 0.0f); std::fill_n(right, count, 0.0f);
    for (const auto& clip : track.clips) {
        if (!clip.sound) continue;
        const auto a = std::max(start, clip.start), b = std::min(start + count, clip.end());
        if (b <= a) continue;
        const auto n = b - a, from = a - clip.start, at = a - start;
        clip.sound->read(0, clip.offset + from, n, scratch);
        if (clip.sound->channels() > 1) clip.sound->read(1, clip.offset + from, n, scratch + count);
        else std::copy_n(scratch, n, scratch + count);
        for (std::int64_t i = 0; i < n; ++i) {
            const auto g = clipGain(clip, from + i);
            left[at + i] += scratch[i] * g; right[at + i] += scratch[count + i] * g;
        }
    }
}

Sound render(const Session& session, std::int64_t start, std::int64_t end, const TrackSet* const only)
{
    start = std::max<std::int64_t>(0, start);
    end = std::max(start, end);
    std::vector<std::vector<float>> channels(2, std::vector<float>(static_cast<std::size_t>(end - start)));
    constexpr int block = 4'096;
    std::vector<float> scratch(static_cast<std::size_t>(block) * 2);
    for (auto at = start; at < end; at += block) {
        const auto n = static_cast<int>(std::min<std::int64_t>(block, end - at));
        mix(session, at, n, channels[0].data() + (at - start), channels[1].data() + (at - start), scratch.data(), only);
    }
    return Sound(std::move(channels), session.rate);
}

// ---- Edits -------------------------------------------------------------------------------------

Session withTrack(const Session& session, const std::string& name)
{
    auto next = session;
    Track track;
    track.id = next.newId();
    track.name = name;
    next.tracks.push_back(std::move(track));
    return next;
}

Session withoutTrack(const Session& session, const int track)
{
    auto next = session;
    if (track >= 0 && track < static_cast<int>(next.tracks.size()))
        next.tracks.erase(next.tracks.begin() + track);
    return next;
}

Session withClip(const Session& session, int track, const std::int64_t at, Sound sound, const std::string& name,
                 const std::string& file)
{
    auto next = session;
    if (std::abs(sound.rate() - next.rate) > 0.5)
        sound = sound.withRate(next.rate);
    if (track < 0 || track >= static_cast<int>(next.tracks.size())) {
        next = withTrack(next, name);
        track = static_cast<int>(next.tracks.size()) - 1;
    }
    Clip clip;
    clip.id = next.newId();
    clip.length = sound.frames();
    clip.sound = std::make_shared<const Sound>(std::move(sound));
    clip.start = std::max<std::int64_t>(0, at);
    clip.name = name;
    clip.file = file;
    auto& target = next.tracks[static_cast<std::size_t>(track)];
    target.clips.push_back(std::move(clip));
    sortClips(target);
    return next;
}

Session excerpt(const Session& session, const std::int64_t start, const std::int64_t end, const TrackSet& tracks)
{
    Session piece;
    piece.rate = session.rate;
    piece.nextId = session.nextId;
    piece.span = std::max<std::int64_t>(0, end - start);
    for (std::size_t t = 0; t < session.tracks.size(); ++t) {
        if (!inSet(tracks, t))
            continue;
        const auto& source = session.tracks[t];
        Track track;
        track.id = source.id;
        track.name = source.name;
        for (const auto& clip : source.clips) {
            if (clip.end() <= start || clip.start >= end)
                continue;
            auto part = pieceOf(clip, start, end);
            part.start -= start;
            track.clips.push_back(std::move(part));
        }
        piece.tracks.push_back(std::move(track));
    }
    return piece;
}

namespace {

// Each track in `tracks`: its clips without [start, end), what follows moved
// back by `shift` (the stretch's length, or 0).
Session withoutStretch(const Session& session, const std::int64_t start, const std::int64_t end, const TrackSet& tracks,
                       const std::int64_t shift)
{
    auto next = session;
    for (std::size_t t = 0; t < next.tracks.size(); ++t) {
        if (!inSet(tracks, t))
            continue;
        auto& track = next.tracks[t];
        std::vector<Clip> kept;
        for (const auto& clip : track.clips) {
            if (clip.end() <= start) {
                kept.push_back(clip);
                continue;
            }
            if (clip.start >= end) {
                auto moved = clip;
                moved.start -= shift;
                kept.push_back(std::move(moved));
                continue;
            }
            // Inside one clip: its own sound changed, still one clip.
            if (clip.start < start && clip.end() > end && clip.sound != nullptr) {
                auto whole = clip;
                const auto from = clip.offset + (start - clip.start);
                const auto to = clip.offset + (end - clip.start);
                if (shift > 0) {
                    whole.sound = std::make_shared<const Sound>(clip.sound->removed(from, to));
                    whole.length -= end - start;
                } else {
                    whole.sound = std::make_shared<const Sound>(silenced(*clip.sound, from, to, 0xffu));
                }
                whole.fadeIn = std::min(whole.fadeIn, whole.length);
                whole.fadeOut = std::min(whole.fadeOut, whole.length - whole.fadeIn);
                whole.edited = true;
                kept.push_back(std::move(whole));
                continue;
            }
            if (clip.start < start)
                kept.push_back(pieceOf(clip, clip.start, start));
            if (clip.end() > end) {
                auto after = pieceOf(clip, end, clip.end());
                after.id = next.newId();
                after.start -= shift;
                kept.push_back(std::move(after));
            }
        }
        track.clips = std::move(kept);
        sortClips(track);
    }
    return next;
}

} // namespace

Session removed(const Session& session, const std::int64_t start, const std::int64_t end, const TrackSet& tracks)
{
    return end > start ? withoutStretch(session, start, end, tracks, end - start) : session;
}

Session cleared(const Session& session, const std::int64_t start, const std::int64_t end, const TrackSet& tracks)
{
    return end > start ? withoutStretch(session, start, end, tracks, 0) : session;
}

Session cropped(const Session& session, const std::int64_t start, const std::int64_t end)
{
    auto next = session;
    next.span = std::max<std::int64_t>(0, end - start);
    for (auto& track : next.tracks) {
        std::vector<Clip> kept;
        for (const auto& clip : track.clips) {
            if (clip.end() <= start || clip.start >= end)
                continue;
            auto part = pieceOf(clip, start, end);
            part.start -= start;
            kept.push_back(std::move(part));
        }
        track.clips = std::move(kept);
    }
    return next;
}

Session split(const Session& session, const std::int64_t at, const TrackSet& tracks)
{
    auto next = session;
    for (std::size_t t = 0; t < next.tracks.size(); ++t) {
        if (!inSet(tracks, t))
            continue;
        auto& track = next.tracks[t];
        std::vector<Clip> clips;
        for (const auto& clip : track.clips) {
            if (clip.start < at && clip.end() > at) {
                auto first = pieceOf(clip, clip.start, at);
                auto second = pieceOf(clip, at, clip.end());
                first.fadeOut = 0;
                second.fadeIn = 0;
                second.id = next.newId();
                clips.push_back(std::move(first));
                clips.push_back(std::move(second));
            } else {
                clips.push_back(clip);
            }
        }
        track.clips = std::move(clips);
    }
    return next;
}

Session pasted(const Session& session, const std::int64_t at, const Session& piece, const int first, const bool mixed)
{
    auto next = session;
    next.nextId = std::max(next.nextId, piece.nextId);
    const auto length = piece.frames();
    for (std::size_t k = 0; k < piece.tracks.size(); ++k) {
        const auto index = static_cast<std::size_t>(std::max(0, first)) + k;
        while (next.tracks.size() <= index)
            next = withTrack(next, piece.tracks[k].name);
        TrackSet one(next.tracks.size(), false);
        one[index] = true;
        // One plain sound pasted into (or at an end of) a clip: into its own
        // sound, still one clip.
        const auto& pieceClips = piece.tracks[k].clips;
        if (!mixed && pieceClips.size() == 1 && pieceClips[0].start == 0 && pieceClips[0].length == length && pieceClips[0].fadeIn == 0
            && pieceClips[0].fadeOut == 0 && pieceClips[0].gainDb == 0.0f && pieceClips[0].sound != nullptr) {
            auto& clips = next.tracks[index].clips;
            const auto host = std::find_if(clips.begin(), clips.end(), [at](const Clip& clip) {
                return clip.sound != nullptr && clip.start <= at && clip.end() >= at && clip.gainDb == 0.0f;
            });
            if (host != clips.end()) {
                const auto hostId = host->id;
                const auto& added = pieceClips[0];
                auto sound = host->sound->inserted(host->offset + (at - host->start), added.sound->excerpt(added.offset, added.offset + added.length));
                host->sound = std::make_shared<const Sound>(std::move(sound));
                host->length += length;
                host->edited = true;
                for (auto& clip : clips)
                    if (clip.id != hostId && clip.start >= at)
                        clip.start += length;
                sortClips(next.tracks[index]);
                continue;
            }
        }
        if (!mixed) {
            next = split(next, at, one);
            for (auto& clip : next.tracks[index].clips)
                if (clip.start >= at)
                    clip.start += length;
        }
        for (auto clip : piece.tracks[k].clips) {
            clip.id = next.newId();
            clip.start += at;
            next.tracks[index].clips.push_back(std::move(clip));
        }
        sortClips(next.tracks[index]);
    }
    return next;
}

Session withSilence(const Session& session, const std::int64_t at, const std::int64_t frames, const TrackSet& tracks)
{
    auto next = session;
    if (frames <= 0)
        return next;
    for (std::size_t t = 0; t < next.tracks.size(); ++t) {
        if (!inSet(tracks, t))
            continue;
        auto& clips = next.tracks[t].clips;
        const auto host = std::find_if(clips.begin(), clips.end(), [at](const Clip& clip) {
            return clip.sound != nullptr && clip.start <= at && clip.end() >= at;
        });
        std::uint64_t hostId = 0;
        if (host != clips.end()) {
            hostId = host->id;
            const Sound silence(host->sound->channels(), frames, host->sound->rate());
            host->sound = std::make_shared<const Sound>(host->sound->inserted(host->offset + (at - host->start), silence));
            host->length += frames;
            host->edited = true;
        }
        for (auto& clip : clips)
            if (clip.id != hostId && clip.start >= at)
                clip.start += frames;
        sortClips(next.tracks[t]);
    }
    return next;
}

Session processed(const Session& session, const std::int64_t start, const std::int64_t end, const TrackSet& tracks,
                  const ClipProcess& process)
{
    auto next = session;
    if (end <= start)
        return next;
    for (std::size_t t = 0; t < next.tracks.size(); ++t) {
        if (!inSet(tracks, t))
            continue;
        auto& track = next.tracks[t];
        sortClips(track);
        // Each clip under the stretch processed; what grew or shrank moves
        // what comes after it on the track.
        struct Change {
            std::int64_t end {};
            std::int64_t delta {};
        };
        std::vector<Change> changes;
        std::vector<std::int64_t> starts;
        for (const auto& clip : track.clips)
            starts.push_back(clip.start);
        for (auto& clip : track.clips) {
            const auto a = std::max(start, clip.start);
            const auto b = std::min(end, clip.end());
            if (b <= a || clip.sound == nullptr)
                continue;
            const auto from = clip.offset + (a - clip.start);
            const auto to = clip.offset + (b - clip.start);
            const auto before = clip.sound->frames();
            auto changed = process(*clip.sound, from, to, a);
            const auto delta = changed.frames() - before;
            const auto originalEnd = clip.end();
            clip.sound = std::make_shared<const Sound>(std::move(changed));
            clip.length = std::max<std::int64_t>(0, clip.length + delta);
            clip.fadeIn = std::min(clip.fadeIn, clip.length);
            clip.fadeOut = std::min(clip.fadeOut, clip.length);
            clip.edited = true;
            if (delta != 0)
                changes.push_back({ originalEnd, delta });
        }
        for (std::size_t c = 0; c < track.clips.size(); ++c)
            for (const auto& change : changes)
                if (starts[c] >= change.end)
                    track.clips[c].start += change.delta;
        sortClips(track);
    }
    return next;
}

Session withRate(const Session& session, const double rate)
{
    if (rate <= 0.0 || std::abs(rate - session.rate) < 0.5)
        return session;
    auto next = session;
    const auto ratio = rate / session.rate;
    const auto scale = [ratio](const std::int64_t frames) { return static_cast<std::int64_t>(std::llround(static_cast<double>(frames) * ratio)); };
    next.rate = rate;
    next.span = scale(next.span);
    for (auto& track : next.tracks)
        for (auto& clip : track.clips) {
            if (clip.sound != nullptr)
                clip.sound = std::make_shared<const Sound>(clip.sound->withRate(rate));
            clip.start = scale(clip.start);
            clip.offset = scale(clip.offset);
            clip.length = scale(clip.length);
            clip.fadeIn = scale(clip.fadeIn);
            clip.fadeOut = scale(clip.fadeOut);
            for(auto& n:clip.notes) {n.start=scale(n.start);n.length=std::max<std::int64_t>(1,scale(n.length));}
            clip.edited = true;
        }
    for(auto& t:next.tracks) for(auto& lane:t.automation) for(auto& p:lane.points)p.frame=scale(p.frame);
    for(auto& lane:next.masterAutomation)for(auto& p:lane.points)p.frame=scale(p.frame);
    return next;
}

} // namespace cydaw::engine::foundry
