#include "DjEngine.h"

#include <algorithm>
#include <cmath>
#include <deque>

#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <objbase.h>
#endif

namespace cydaw::app {

using engine::dj::TrackAnalysis;
using engine::dj::TrackAudio;

namespace {

// A track longer than this is not a track (a recording of hours): refused.
constexpr double longestTrackSeconds = 45.0 * 60.0;

double wrapHalf(double value) noexcept
{
    value -= std::floor(value);
    return value > 0.5 ? value - 1.0 : value;
}

} // namespace

// The tags a reader found, else "Artist - Title" from the file's name.
void djTrackTags(const juce::File& file, const juce::StringPairArray& tags, juce::String& title, juce::String& artist)
{
    const auto find = [&](std::initializer_list<const char*> keys) {
        for (const auto* key : keys)
            for (const auto& name : tags.getAllKeys())
                if (name.equalsIgnoreCase(key) && tags[name].trim().isNotEmpty())
                    return tags[name].trim();
        return juce::String();
    };
    title = find({ "title", "INAM", "TIT2", "name" });
    artist = find({ "artist", "IART", "TPE1", "author" });
    if (title.isEmpty()) {
        const auto name = file.getFileNameWithoutExtension();
        const auto dash = name.indexOf(" - ");
        if (dash > 0) {
            if (artist.isEmpty())
                artist = name.substring(0, dash).trim();
            title = name.substring(dash + 3).trim();
        } else {
            title = name;
        }
    }
}

DjEngine::DecodingThreadScope::DecodingThreadScope()
{
#if JUCE_WINDOWS
    initialised_ = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
#endif
}

DjEngine::DecodingThreadScope::~DecodingThreadScope()
{
#if JUCE_WINDOWS
    if (initialised_)
        CoUninitialize();
#endif
}

std::shared_ptr<TrackAudio> DjEngine::decode(juce::AudioFormatManager& formats, const juce::File& file, juce::String& error,
                                             juce::String& title, juce::String& artist, const std::function<bool()>& cancelled)
{
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr) {
        error = juce::String::fromUTF8(file.existsAsFile() ? "Format non lu" : "Fichier introuvable");
        return nullptr;
    }
    const auto rate = reader->sampleRate > 0.0 ? reader->sampleRate : 44'100.0;
    const auto frames = reader->lengthInSamples;
    if (frames <= 0) {
        error = juce::String::fromUTF8("Fichier vide");
        return nullptr;
    }
    if (static_cast<double>(frames) / rate > longestTrackSeconds) {
        error = juce::String::fromUTF8("Trop long pour une platine (45 min au plus)");
        return nullptr;
    }
    auto audio = std::make_shared<TrackAudio>();
    audio->sampleRate = rate;
    const auto channels = static_cast<int>(std::clamp<unsigned>(reader->numChannels, 1u, 2u));
    audio->channels.assign(static_cast<std::size_t>(channels), std::vector<float>(static_cast<std::size_t>(frames)));
    constexpr int chunk = 1 << 16;
    juce::AudioBuffer<float> buffer(static_cast<int>(std::max(2u, reader->numChannels)), chunk);
    for (juce::int64 at = 0; at < frames; at += chunk) {
        if (cancelled && cancelled())
            return nullptr;
        const auto n = static_cast<int>(std::min<juce::int64>(chunk, frames - at));
        buffer.clear();
        if (!reader->read(&buffer, 0, n, at, true, true)) {
            error = juce::String::fromUTF8("Lecture impossible");
            return nullptr;
        }
        for (int c = 0; c < channels; ++c)
            std::copy_n(buffer.getReadPointer(c), n, audio->channels[static_cast<std::size_t>(c)].data() + at);
    }
    djTrackTags(file, reader->metadataValues, title, artist);
    return audio;
}

// ---- A deck ------------------------------------------------------------------------------------

struct DjEngine::Deck {
    engine::dj::DeckPlayer player;
    engine::dj::ChannelStrip strip;
    engine::dj::DeckEffect effect;

    // The message thread's (under stateLock_).
    std::shared_ptr<const TrackAudio> audio;
    std::shared_ptr<const TrackAnalysis> analysis;
    LoadState state { LoadState::empty };
    juce::File file;
    juce::String title;
    juce::String artist;
    juce::String error;
    Known known;
    std::atomic<std::uint64_t> ticket {};

    // A track for the audio thread to take.
    std::atomic<const TrackAudio*> pending { nullptr };
    std::atomic<bool> hasPending {};

    // Written by the audio thread.
    std::atomic<double> position {};
    std::atomic<double> seconds {};
    std::atomic<bool> playing {};
    std::atomic<bool> previewing {};
    std::atomic<double> speed {};
    std::atomic<bool> looping {};
    std::atomic<double> loopStart {};
    std::atomic<double> loopEnd {};
    std::atomic<float> peak {};
    std::atomic<double> phase {};

    // Both.
    std::atomic<double> tempo { 1.0 };
    std::atomic<double> bend {};
    std::atomic<bool> keylock { true };
    std::atomic<bool> sync {};
    std::atomic<double> bpm {};
    std::atomic<double> firstBeat {};
    std::atomic<double> cuePoint {};
    std::array<std::atomic<double>, hotCueCount> hotCues {};
    std::atomic<bool> scratching {};
    std::atomic<double> scratchTarget {};

    // The channel's knobs.
    std::atomic<float> trimDb {}, high { 0.5f }, mid { 0.5f }, low { 0.5f }, filter {}, resonance { 0.3f }, fader { 1.0f };
    std::atomic<bool> killHigh {}, killMid {}, killLow {}, cue {};
    std::atomic<int> side {};
    std::atomic<int> effectKind {};
    std::atomic<bool> effectOn {};
    std::atomic<float> effectMix { 0.5f }, effectFirst { 0.5f }, effectSecond { 0.5f };

    // The audio thread's own.
    bool playingNow {};
    bool cueHeld {};
    double scratchSpeed {};
    float gain {};

    Deck()
    {
        for (auto& cue : hotCues)
            cue.store(-1.0, std::memory_order_relaxed);
    }
};

// ---- The loader --------------------------------------------------------------------------------

class DjEngine::Loader final : public juce::Thread {
public:
    struct Job {
        int deck {};
        juce::File file;
        std::uint64_t ticket {};
    };

    explicit Loader(DjEngine& owner) : juce::Thread("CyDAW DJ loader"), owner_(owner) { startThread(juce::Thread::Priority::normal); }

    ~Loader() override
    {
        signalThreadShouldExit();
        wake_.signal();
        stopThread(10'000);
    }

    void add(Job job)
    {
        {
            const std::scoped_lock lock(lock_);
            // A deck's newer track replaces the one it waited for.
            jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(), [&](const Job& old) { return old.deck == job.deck; }), jobs_.end());
            jobs_.push_back(std::move(job));
        }
        wake_.signal();
    }

    void run() override
    {
        const DecodingThreadScope decoding;
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        while (!threadShouldExit()) {
            wake_.wait(500);
            owner_.freeRetired();
            std::optional<Job> job;
            {
                const std::scoped_lock lock(lock_);
                if (!jobs_.empty()) {
                    job = jobs_.front();
                    jobs_.pop_front();
                }
            }
            if (job)
                load(formats, *job);
        }
    }

private:
    [[nodiscard]] bool stale(const Job& job) const
    {
        return threadShouldExit() || owner_.decks_[static_cast<std::size_t>(job.deck)]->ticket.load() != job.ticket;
    }

    void load(juce::AudioFormatManager& formats, const Job& job)
    {
        juce::String error;
        juce::String title;
        juce::String artist;
        auto audio = DjEngine::decode(formats, job.file, error, title, artist, [&] { return stale(job); });
        if (audio == nullptr) {
            if (!stale(job))
                owner_.fail(job.deck, error, job.ticket);
            return;
        }
        owner_.publish(job.deck, audio, title, artist, job.ticket);
        if (stale(job))
            return;
        auto analysis = std::make_shared<TrackAnalysis>(engine::dj::analyse(*audio));
        owner_.publishAnalysis(job.deck, std::move(analysis), job.ticket);
    }

    DjEngine& owner_;
    std::mutex lock_;
    std::deque<Job> jobs_;
    juce::WaitableEvent wake_;
};

// ---- The engine --------------------------------------------------------------------------------

namespace {
std::mutex retiredLock;
std::vector<std::pair<std::uint64_t, std::shared_ptr<const TrackAudio>>> retired;
} // namespace

DjEngine::DjEngine()
{
    for (auto& deck : decks_)
        deck = std::make_unique<Deck>();
    for (int d = 0; d < deckCount; ++d)
        decks_[static_cast<std::size_t>(d)]->side.store(d % 2 == 0 ? 0 : 2, std::memory_order_relaxed);
    loader_ = std::make_unique<Loader>(*this);
}

DjEngine::~DjEngine()
{
    stopRecording();
    loader_.reset();
    writerThread_.stopThread(3'000);
    const std::scoped_lock lock(retiredLock);
    retired.clear();
}

void DjEngine::prepareToPlay(const int samplesPerBlockExpected, const double sampleRate)
{
    rate_ = sampleRate > 0.0 ? sampleRate : 48'000.0;
    blockSize_ = std::max(512, samplesPerBlockExpected);
    for(auto& b:modeInputs_)b.setSize(2,blockSize_,false,true,true);
    { const auto p=processor_.read();if(p&&*p)(*p)->prepare(rate_,blockSize_); }
    work_.setSize(2, blockSize_, false, true, true);
    mix_.setSize(2, blockSize_, false, true, true);
    cueBus_.setSize(2, blockSize_, false, true, true);
    for (auto& deck : decks_) {
        // Where it was, kept through a new device.
        const auto* track = deck->player.track();
        const auto at = deck->player.position();
        const auto rateBefore = track != nullptr ? track->sampleRate : 0.0;
        deck->player.prepare(rate_, blockSize_);
        if (track != nullptr && rateBefore > 0.0)
            deck->player.jump(at);
        deck->strip.prepare(rate_);
        deck->effect.prepare(rate_);
    }
}

void DjEngine::releaseResources() {}

// ---- Tracks ------------------------------------------------------------------------------------

void DjEngine::load(const int index, const juce::File& file, const Known& known)
{
    if (index < 0 || index >= deckCount)
        return;
    auto& deck = *decks_[static_cast<std::size_t>(index)];
    const auto ticket = deck.ticket.fetch_add(1) + 1;
    {
        const std::scoped_lock lock(stateLock_);
        deck.state = LoadState::loading;
        deck.file = file;
        deck.title = file.getFileNameWithoutExtension();
        deck.artist = {};
        deck.error = {};
        deck.analysis = nullptr;
        deck.known = known;
    }
    loader_->add({ index, file, ticket });
}

void DjEngine::eject(const int index)
{
    if (index < 0 || index >= deckCount)
        return;
    auto& deck = *decks_[static_cast<std::size_t>(index)];
    deck.ticket.fetch_add(1);
    std::shared_ptr<const TrackAudio> old;
    {
        const std::scoped_lock lock(stateLock_);
        deck.state = LoadState::empty;
        deck.file = {};
        deck.title = {};
        deck.artist = {};
        deck.analysis = nullptr;
        old = std::move(deck.audio);
    }
    deck.pending.store(nullptr, std::memory_order_release);
    deck.hasPending.store(true, std::memory_order_release);
    if (old != nullptr) {
        const std::scoped_lock lock(retiredLock);
        retired.emplace_back(blocks_.load(), std::move(old));
    }
}

void DjEngine::publish(const int index, std::shared_ptr<const TrackAudio> audio, const juce::String& title,
                       const juce::String& artist, const std::uint64_t ticket)
{
    auto& deck = *decks_[static_cast<std::size_t>(index)];
    if (deck.ticket.load() != ticket)
        return;
    std::shared_ptr<const TrackAudio> old;
    Known known;
    {
        const std::scoped_lock lock(stateLock_);
        old = std::move(deck.audio);
        deck.audio = audio;
        deck.state = LoadState::ready;
        deck.title = title;
        deck.artist = artist;
        known = deck.known;
    }
    deck.seconds.store(audio->seconds(), std::memory_order_relaxed);
    deck.bpm.store(known.hasGrid ? known.bpm : 0.0, std::memory_order_relaxed);
    deck.firstBeat.store(known.hasGrid ? known.firstBeat : 0.0, std::memory_order_relaxed);
    deck.cuePoint.store(known.cuePoint, std::memory_order_relaxed);
    for (int i = 0; i < hotCueCount; ++i)
        deck.hotCues[static_cast<std::size_t>(i)].store(known.hotCues[static_cast<std::size_t>(i)], std::memory_order_relaxed);
    deck.pending.store(audio.get(), std::memory_order_release);
    deck.hasPending.store(true, std::memory_order_release);
    if (old != nullptr) {
        const std::scoped_lock lock(retiredLock);
        retired.emplace_back(blocks_.load(), std::move(old));
    }
}

void DjEngine::publishAnalysis(const int index, std::shared_ptr<const TrackAnalysis> analysis, const std::uint64_t ticket)
{
    auto& deck = *decks_[static_cast<std::size_t>(index)];
    if (deck.ticket.load() != ticket)
        return;
    bool hasGrid = false;
    {
        const std::scoped_lock lock(stateLock_);
        deck.analysis = analysis;
        hasGrid = deck.known.hasGrid;
    }
    if (!hasGrid) {
        deck.bpm.store(analysis->bpm, std::memory_order_relaxed);
        deck.firstBeat.store(analysis->firstBeat, std::memory_order_relaxed);
    }
}

void DjEngine::fail(const int index, const juce::String& error, const std::uint64_t ticket)
{
    auto& deck = *decks_[static_cast<std::size_t>(index)];
    if (deck.ticket.load() != ticket)
        return;
    const std::scoped_lock lock(stateLock_);
    deck.state = LoadState::failed;
    deck.error = error;
}

void DjEngine::freeRetired()
{
    // Gone once the audio thread has taken what replaced them (two blocks
    // later), or at once without any block running.
    const auto now = blocks_.load();
    const std::scoped_lock lock(retiredLock);
    retired.erase(std::remove_if(retired.begin(), retired.end(), [&](const auto& entry) { return now >= entry.first + 2; }),
                  retired.end());
}

DjEngine::DeckState DjEngine::deck(const int index) const
{
    DeckState state;
    if (index < 0 || index >= deckCount)
        return state;
    auto& deck = *decks_[static_cast<std::size_t>(index)];
    {
        const std::scoped_lock lock(stateLock_);
        state.state = deck.state;
        state.file = deck.file;
        state.title = deck.title;
        state.artist = deck.artist;
        state.error = deck.error;
        state.analysis = deck.analysis;
    }
    state.seconds = deck.seconds.load(std::memory_order_relaxed);
    state.position = deck.position.load(std::memory_order_relaxed);
    state.playing = deck.playing.load(std::memory_order_relaxed);
    state.previewing = deck.previewing.load(std::memory_order_relaxed);
    state.tempo = deck.tempo.load(std::memory_order_relaxed);
    state.speed = deck.speed.load(std::memory_order_relaxed);
    state.bpm = deck.bpm.load(std::memory_order_relaxed);
    state.firstBeat = deck.firstBeat.load(std::memory_order_relaxed);
    state.playedBpm = state.bpm * (std::abs(state.speed) > 1.0e-6 ? state.speed : state.tempo);
    state.keylock = deck.keylock.load(std::memory_order_relaxed);
    state.sync = deck.sync.load(std::memory_order_relaxed);
    state.master = currentMaster_.load(std::memory_order_relaxed) == index;
    state.looping = deck.looping.load(std::memory_order_relaxed);
    state.loopStart = deck.loopStart.load(std::memory_order_relaxed);
    state.loopEnd = deck.loopEnd.load(std::memory_order_relaxed);
    state.cuePoint = deck.cuePoint.load(std::memory_order_relaxed);
    for (int i = 0; i < hotCueCount; ++i)
        state.hotCues[static_cast<std::size_t>(i)] = deck.hotCues[static_cast<std::size_t>(i)].load(std::memory_order_relaxed);
    state.peak = deck.peak.load(std::memory_order_relaxed);
    state.phase = deck.phase.load(std::memory_order_relaxed);
    return state;
}

bool DjEngine::anyPlaying() const noexcept
{
    for (const auto& deck : decks_)
        if (deck->playing.load(std::memory_order_relaxed) || deck->previewing.load(std::memory_order_relaxed))
            return true;
    return false;
}

// ---- Commands ----------------------------------------------------------------------------------

void DjEngine::push(const Command& command)
{
    const std::scoped_lock lock(pushLock_);
    const auto scope = fifo_.write(1);
    if (scope.blockSize1 > 0)
        commands_[static_cast<std::size_t>(scope.startIndex1)] = command;
    else if (scope.blockSize2 > 0)
        commands_[static_cast<std::size_t>(scope.startIndex2)] = command;
}

void DjEngine::setPlaying(const int deck, const bool playing)
{
    if (deck >= 0 && deck < deckCount)
        push({ Command::Type::play, deck, playing ? 1.0 : 0.0, 0 });
}

void DjEngine::cue(const int deck, const bool down)
{
    if (deck >= 0 && deck < deckCount)
        push({ down ? Command::Type::cueDown : Command::Type::cueUp, deck, 0.0, 0 });
}

void DjEngine::seek(const int deck, const double seconds)
{
    if (deck >= 0 && deck < deckCount)
        push({ Command::Type::seek, deck, seconds, 0 });
}

void DjEngine::setTempo(const int deck, const double ratio)
{
    if (deck >= 0 && deck < deckCount)
        decks_[static_cast<std::size_t>(deck)]->tempo.store(std::clamp(ratio, 0.5, 2.0), std::memory_order_relaxed);
}

void DjEngine::setBend(const int deck, const double amount)
{
    if (deck >= 0 && deck < deckCount)
        decks_[static_cast<std::size_t>(deck)]->bend.store(std::clamp(amount, -0.5, 0.5), std::memory_order_relaxed);
}

void DjEngine::setKeylock(const int deck, const bool on)
{
    if (deck >= 0 && deck < deckCount)
        decks_[static_cast<std::size_t>(deck)]->keylock.store(on, std::memory_order_relaxed);
}

void DjEngine::setSync(const int deck, const bool on)
{
    if (deck < 0 || deck >= deckCount)
        return;
    decks_[static_cast<std::size_t>(deck)]->sync.store(on, std::memory_order_relaxed);
    if (on)
        push({ Command::Type::syncOn, deck, 0.0, 0 });
}

void DjEngine::setMaster(const int deck)
{
    masterDeck_.store(deck >= 0 && deck < deckCount ? deck : -1, std::memory_order_relaxed);
}

void DjEngine::hotCue(const int deck, const int index, const HotCueAction action)
{
    if (deck < 0 || deck >= deckCount || index < 0 || index >= hotCueCount)
        return;
    const auto type = action == HotCueAction::set      ? Command::Type::hotCueSet
                      : action == HotCueAction::remove ? Command::Type::hotCueRemove
                                                       : Command::Type::hotCueJump;
    push({ type, deck, 0.0, index });
}

void DjEngine::loopBeats(const int deck, const double beats)
{
    if (deck >= 0 && deck < deckCount)
        push({ Command::Type::loopBeats, deck, beats, 0 });
}

void DjEngine::loopMove(const int deck, const double beats)
{
    if (deck >= 0 && deck < deckCount)
        push({ Command::Type::loopMove, deck, beats, 0 });
}

void DjEngine::loopResize(const int deck, const double factor)
{
    if (deck >= 0 && deck < deckCount)
        push({ Command::Type::loopResize, deck, factor, 0 });
}

void DjEngine::beatJump(const int deck, const double beats)
{
    if (deck >= 0 && deck < deckCount)
        push({ Command::Type::beatJump, deck, beats, 0 });
}

void DjEngine::setGrid(const int index, const double bpm, const double firstBeat)
{
    if (index < 0 || index >= deckCount)
        return;
    auto& deck = *decks_[static_cast<std::size_t>(index)];
    deck.bpm.store(std::clamp(bpm, 0.0, 400.0), std::memory_order_relaxed);
    deck.firstBeat.store(std::max(0.0, firstBeat), std::memory_order_relaxed);
    const std::scoped_lock lock(stateLock_);
    deck.known.hasGrid = bpm > 0.0;
    deck.known.bpm = bpm;
    deck.known.firstBeat = firstBeat;
}

void DjEngine::scratch(const int index, const bool on, const double seconds)
{
    if (index < 0 || index >= deckCount)
        return;
    auto& deck = *decks_[static_cast<std::size_t>(index)];
    deck.scratchTarget.store(seconds, std::memory_order_relaxed);
    deck.scratching.store(on, std::memory_order_release);
}

DjEngine::Channel DjEngine::channel(const int index) const
{
    Channel channel;
    if (index < 0 || index >= deckCount)
        return channel;
    const auto& deck = *decks_[static_cast<std::size_t>(index)];
    channel.trimDb = deck.trimDb.load(std::memory_order_relaxed);
    channel.high = deck.high.load(std::memory_order_relaxed);
    channel.mid = deck.mid.load(std::memory_order_relaxed);
    channel.low = deck.low.load(std::memory_order_relaxed);
    channel.killHigh = deck.killHigh.load(std::memory_order_relaxed);
    channel.killMid = deck.killMid.load(std::memory_order_relaxed);
    channel.killLow = deck.killLow.load(std::memory_order_relaxed);
    channel.filter = deck.filter.load(std::memory_order_relaxed);
    channel.resonance = deck.resonance.load(std::memory_order_relaxed);
    channel.fader = deck.fader.load(std::memory_order_relaxed);
    channel.cue = deck.cue.load(std::memory_order_relaxed);
    channel.side = deck.side.load(std::memory_order_relaxed);
    channel.effect.kind = static_cast<engine::dj::EffectKind>(deck.effectKind.load(std::memory_order_relaxed));
    channel.effect.on = deck.effectOn.load(std::memory_order_relaxed);
    channel.effect.mix = deck.effectMix.load(std::memory_order_relaxed);
    channel.effect.first = deck.effectFirst.load(std::memory_order_relaxed);
    channel.effect.second = deck.effectSecond.load(std::memory_order_relaxed);
    return channel;
}

void DjEngine::setChannel(const int index, const Channel& channel)
{
    if (index < 0 || index >= deckCount)
        return;
    auto& deck = *decks_[static_cast<std::size_t>(index)];
    deck.trimDb.store(std::clamp(channel.trimDb, -24.0f, 12.0f), std::memory_order_relaxed);
    deck.high.store(std::clamp(channel.high, 0.0f, 1.0f), std::memory_order_relaxed);
    deck.mid.store(std::clamp(channel.mid, 0.0f, 1.0f), std::memory_order_relaxed);
    deck.low.store(std::clamp(channel.low, 0.0f, 1.0f), std::memory_order_relaxed);
    deck.killHigh.store(channel.killHigh, std::memory_order_relaxed);
    deck.killMid.store(channel.killMid, std::memory_order_relaxed);
    deck.killLow.store(channel.killLow, std::memory_order_relaxed);
    deck.filter.store(std::clamp(channel.filter, -1.0f, 1.0f), std::memory_order_relaxed);
    deck.resonance.store(std::clamp(channel.resonance, 0.0f, 1.0f), std::memory_order_relaxed);
    deck.fader.store(std::clamp(channel.fader, 0.0f, 1.0f), std::memory_order_relaxed);
    deck.cue.store(channel.cue, std::memory_order_relaxed);
    deck.side.store(std::clamp(channel.side, 0, 2), std::memory_order_relaxed);
    deck.effectKind.store(std::clamp(static_cast<int>(channel.effect.kind), 0, engine::dj::effectKindCount - 1), std::memory_order_relaxed);
    deck.effectOn.store(channel.effect.on, std::memory_order_relaxed);
    deck.effectMix.store(std::clamp(channel.effect.mix, 0.0f, 1.0f), std::memory_order_relaxed);
    deck.effectFirst.store(std::clamp(channel.effect.first, 0.0f, 1.0f), std::memory_order_relaxed);
    deck.effectSecond.store(std::clamp(channel.effect.second, 0.0f, 1.0f), std::memory_order_relaxed);
}

// ---- The audio thread --------------------------------------------------------------------------

double DjEngine::beatSecondsOf(const Deck& deck) const noexcept
{
    const auto bpm = deck.bpm.load(std::memory_order_relaxed);
    return bpm > 1.0 ? 60.0 / bpm : 0.5;
}

void DjEngine::takeLoadedTracks()
{
    for (auto& deck : decks_) {
        if (!deck->hasPending.exchange(false, std::memory_order_acq_rel))
            continue;
        deck->player.setTrack(deck->pending.load(std::memory_order_acquire));
        deck->playingNow = false;
        deck->cueHeld = false;
        deck->playing.store(false, std::memory_order_relaxed);
        deck->previewing.store(false, std::memory_order_relaxed);
        deck->looping.store(false, std::memory_order_relaxed);
        deck->strip.reset();
        deck->effect.reset();
        // At the cue point it was given.
        if (const auto* track = deck->player.track())
            deck->player.jump(deck->cuePoint.load(std::memory_order_relaxed) * track->sampleRate);
        deck->position.store(deck->cuePoint.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
}

int DjEngine::chooseMaster() const noexcept
{
    const auto shown = shownDecks_.load(std::memory_order_relaxed);
    const auto chosen = masterDeck_.load(std::memory_order_relaxed);
    const auto usable = [&](const int index) {
        const auto& deck = *decks_[static_cast<std::size_t>(index)];
        return index < shown && deck.player.track() != nullptr && deck.bpm.load(std::memory_order_relaxed) > 1.0;
    };
    if (chosen >= 0 && usable(chosen))
        return chosen;
    // Following the decks: the one that leads stays while it plays.
    const auto current = currentMaster_.load(std::memory_order_relaxed);
    const auto leads = [&](const int index) {
        const auto& deck = *decks_[static_cast<std::size_t>(index)];
        return usable(index) && deck.playingNow && !deck.sync.load(std::memory_order_relaxed);
    };
    if (current >= 0 && current < deckCount && leads(current))
        return current;
    auto best = -1;
    auto bestGain = -1.0f;
    for (int index = 0; index < shown; ++index)
        if (leads(index) && decks_[static_cast<std::size_t>(index)]->gain > bestGain) {
            best = index;
            bestGain = decks_[static_cast<std::size_t>(index)]->gain;
        }
    if (best >= 0)
        return best;
    if (current >= 0 && current < deckCount && usable(current))
        return current;
    for (int index = 0; index < shown; ++index)
        if (usable(index) && decks_[static_cast<std::size_t>(index)]->playingNow)
            return index;
    return -1;
}

void DjEngine::alignPhase(Deck& deck, const int master)
{
    if (master < 0 || decks_[static_cast<std::size_t>(master)].get() == &deck || deck.player.track() == nullptr)
        return;
    const auto& lead = *decks_[static_cast<std::size_t>(master)];
    if (lead.player.track() == nullptr || deck.bpm.load(std::memory_order_relaxed) <= 1.0)
        return;
    const auto rate = deck.player.track()->sampleRate;
    const auto at = deck.player.position() / rate;
    const auto leadAt = lead.player.position() / lead.player.track()->sampleRate;
    const auto beat = beatSecondsOf(deck);
    const auto leadBeat = beatSecondsOf(lead);
    const auto error = wrapHalf((leadAt - lead.firstBeat.load()) / leadBeat - (at - deck.firstBeat.load()) / beat);
    if (std::abs(error) > 0.01)
        deck.player.jump((at + error * beat) * rate);
}

void DjEngine::runCommands()
{
    const auto ready = fifo_.getNumReady();
    if (ready <= 0)
        return;
    const auto scope = fifo_.read(ready);
    for (int i = 0; i < scope.blockSize1; ++i) {
        const auto& command = commands_[static_cast<std::size_t>(scope.startIndex1 + i)];
        runCommand(*decks_[static_cast<std::size_t>(command.deck)], command);
    }
    for (int i = 0; i < scope.blockSize2; ++i) {
        const auto& command = commands_[static_cast<std::size_t>(scope.startIndex2 + i)];
        runCommand(*decks_[static_cast<std::size_t>(command.deck)], command);
    }
}

void DjEngine::runCommand(Deck& deck, const Command& command)
{
    const auto* track = deck.player.track();
    if (track == nullptr)
        return;
    const auto rate = track->sampleRate;
    const auto length = track->seconds();
    const auto at = deck.player.position() / rate;
    const auto beat = beatSecondsOf(deck);
    const auto first = deck.firstBeat.load(std::memory_order_relaxed);
    const auto hasGrid = deck.bpm.load(std::memory_order_relaxed) > 1.0;
    const auto quantised = quantize_.load(std::memory_order_relaxed) && hasGrid;
    const auto nearestBeat = [&](const double seconds) { return first + std::round((seconds - first) / beat) * beat; };
    const auto beatBehind = [&](const double seconds) { return first + std::floor((seconds - first) / beat + 1.0e-6) * beat; };
    const auto jumpTo = [&](const double seconds) { deck.player.jump(std::clamp(seconds, 0.0, length) * rate); };
    const auto master = currentMaster_.load(std::memory_order_relaxed);
    switch (command.type) {
    case Command::Type::play:
        if (command.value > 0.5) {
            if (at >= length - 0.01)
                return;
            deck.playingNow = true;
            if (deck.sync.load(std::memory_order_relaxed))
                alignPhase(deck, master);
        } else {
            deck.playingNow = false;
        }
        deck.cueHeld = false;
        break;
    case Command::Type::cueDown: {
        const auto point = deck.cuePoint.load(std::memory_order_relaxed);
        if (deck.playingNow) {
            deck.playingNow = false;
            jumpTo(point);
        } else {
            if (std::abs(at - point) > 0.001) {
                const auto set = quantised ? std::max(0.0, nearestBeat(at)) : at;
                deck.cuePoint.store(set, std::memory_order_relaxed);
                jumpTo(set);
            }
            deck.cueHeld = true;
        }
        break;
    }
    case Command::Type::cueUp:
        if (deck.cueHeld) {
            deck.cueHeld = false;
            if (!deck.playingNow)
                jumpTo(deck.cuePoint.load(std::memory_order_relaxed));
        }
        break;
    case Command::Type::seek:
        jumpTo(command.value);
        break;
    case Command::Type::hotCueJump:
    case Command::Type::hotCueSet: {
        auto& cue = deck.hotCues[static_cast<std::size_t>(command.index)];
        const auto stored = cue.load(std::memory_order_relaxed);
        if (command.type == Command::Type::hotCueSet || stored < 0.0) {
            cue.store(quantised ? std::max(0.0, nearestBeat(at)) : at, std::memory_order_relaxed);
            break;
        }
        auto target = stored;
        // Quantized while playing: the jump keeps the beat where it was.
        if (deck.playingNow && quantised)
            target += wrapHalf((at - first) / beat - (stored - first) / beat) * beat;
        jumpTo(target);
        break;
    }
    case Command::Type::hotCueRemove:
        deck.hotCues[static_cast<std::size_t>(command.index)].store(-1.0, std::memory_order_relaxed);
        break;
    case Command::Type::loopBeats: {
        if (command.value <= 0.0) {
            deck.player.clearLoop();
            break;
        }
        const auto start = quantised ? beatBehind(at) : at;
        const auto end = start + command.value * beat;
        deck.player.setLoop(start * rate, end * rate);
        if (at >= end)
            jumpTo(start + std::fmod(at - start, end - start));
        break;
    }
    case Command::Type::loopMove:
    case Command::Type::beatJump: {
        const auto shift = command.value * beat;
        if (deck.player.looping()) {
            const auto start = deck.player.loopStart() / rate + shift;
            const auto end = deck.player.loopEnd() / rate + shift;
            if (start < 0.0 || end > length)
                break;
            deck.player.setLoop(start * rate, end * rate);
            jumpTo(at + shift);
        } else if (command.type == Command::Type::beatJump) {
            jumpTo(at + shift);
        }
        break;
    }
    case Command::Type::loopResize:
        if (deck.player.looping()) {
            const auto start = deck.player.loopStart() / rate;
            const auto end = start + (deck.player.loopEnd() / rate - start) * std::clamp(command.value, 0.03125, 32.0);
            deck.player.setLoop(start * rate, std::min(end, length) * rate);
            if (at >= end)
                jumpTo(start + std::fmod(at - start, end - start));
        }
        break;
    case Command::Type::syncOn:
        if (deck.playingNow)
            alignPhase(deck, master);
        break;
    }
}

void DjEngine::getNextAudioBlock(const juce::AudioSourceChannelInfo& output)
{
    output.clearActiveBufferRegion();
    if (output.buffer == nullptr || output.numSamples <= 0)
        return;
    const auto processor=processor_.read();
    takeLoadedTracks();
    runCommands();
    const auto master = chooseMaster();
    currentMaster_.store(master, std::memory_order_relaxed);
    double masterPlayedBpm = 0.0;
    double masterBeatAt = 0.0;
    if (master >= 0) {
        const auto& lead = *decks_[static_cast<std::size_t>(master)];
        const auto speed = lead.playingNow ? lead.speed.load(std::memory_order_relaxed) : lead.tempo.load(std::memory_order_relaxed);
        masterPlayedBpm = lead.bpm.load(std::memory_order_relaxed) * (std::abs(speed) > 1.0e-6 ? speed : lead.tempo.load());
        masterBeatAt = (lead.player.position() / lead.player.track()->sampleRate - lead.firstBeat.load(std::memory_order_relaxed))
                       / beatSecondsOf(lead);
    }
    masterBpm_.store(masterPlayedBpm, std::memory_order_relaxed);

    const auto total = output.numSamples;
    const auto shown = shownDecks_.load(std::memory_order_relaxed);
    const auto [leftGain, rightGain] = engine::dj::crossfaderGains(crossfader_.load(std::memory_order_relaxed),
                                                                   crossfaderCurve_.load(std::memory_order_relaxed));
    const auto masterGain = juce::Decibels::decibelsToGain(masterGainDb_.load(std::memory_order_relaxed), -60.0f);
    const auto cueMode = static_cast<CueMode>(cueMode_.load(std::memory_order_relaxed));
    const auto cueMix = std::clamp(cueMix_.load(std::memory_order_relaxed), 0.0f, 1.0f);
    float masterPeak = 0.0f;
    // The meters fall 20 dB a second.
    const auto fall = static_cast<float>(std::pow(0.1, static_cast<double>(total) / rate_));
    for (auto& deck : decks_)
        deck->peak.store(deck->peak.load(std::memory_order_relaxed) * fall, std::memory_order_relaxed);
    for (int done = 0; done < total; done += blockSize_) {
        const auto n = std::min(blockSize_, total - done);
        mix_.clear(0, n);
        std::array<ModeInput,4> modeBlocks{};
        for(int k=0;k<4;++k){modeInputs_[k].clear(0,n);modeBlocks[k]={static_cast<std::uint64_t>(k+1),modeInputs_[k].getReadPointer(0),modeInputs_[k].getReadPointer(1)};}
        cueBus_.clear(0, n);
        for (int index = 0; index < deckCount; ++index) {
            auto& deck = *decks_[static_cast<std::size_t>(index)];
            const auto* track = deck.player.track();
            if (track == nullptr || index >= shown) {
                if (index >= shown && deck.playingNow) {
                    deck.playingNow = false;
                    deck.playing.store(false, std::memory_order_relaxed);
                }
                continue;
            }
            auto* left = work_.getWritePointer(0);
            auto* right = work_.getWritePointer(1);
            const auto trackRate = track->sampleRate;
            const auto step = trackRate / rate_;
            const auto bpm = deck.bpm.load(std::memory_order_relaxed);
            const auto beat = beatSecondsOf(deck);
            const auto at = deck.player.position() / trackRate;
            // How fast: the hand, else the tempo (or the master's), the nudge.
            const auto scratching = deck.scratching.load(std::memory_order_acquire);
            double speed = 0.0;
            auto phase = 0.0;
            if (master >= 0 && master != index && bpm > 1.0 && masterPlayedBpm > 1.0)
                phase = wrapHalf(masterBeatAt - (at - deck.firstBeat.load(std::memory_order_relaxed)) / beat);
            deck.phase.store(phase, std::memory_order_relaxed);
            if (scratching) {
                const auto target = deck.scratchTarget.load(std::memory_order_relaxed) * trackRate;
                const auto wanted = (target - deck.player.position()) / (static_cast<double>(n) * step);
                deck.scratchSpeed += (wanted - deck.scratchSpeed) * 0.35;
                speed = std::clamp(deck.scratchSpeed, -8.0, 8.0);
            } else {
                deck.scratchSpeed = deck.playingNow ? deck.speed.load(std::memory_order_relaxed) : 0.0;
                if (deck.playingNow || deck.cueHeld) {
                    auto base = deck.tempo.load(std::memory_order_relaxed);
                    if (deck.sync.load(std::memory_order_relaxed) && master >= 0 && master != index && bpm > 1.0 && masterPlayedBpm > 1.0) {
                        base = masterPlayedBpm / bpm;
                        // The beats drawn together, gently.
                        if (deck.playingNow && decks_[static_cast<std::size_t>(master)]->playingNow)
                            base += std::clamp(phase * 0.5, -0.02, 0.02);
                    }
                    speed = base + deck.bend.load(std::memory_order_relaxed);
                }
            }
            deck.player.render(left, right, n, speed, deck.keylock.load(std::memory_order_relaxed) && !scratching);
            deck.speed.store(scratching ? speed : (deck.playingNow || deck.cueHeld ? speed : 0.0), std::memory_order_relaxed);
            // The end: it stops.
            if (deck.playingNow && deck.player.position() >= static_cast<double>(track->frames())) {
                deck.playingNow = false;
                deck.player.jump(static_cast<double>(track->frames()));
            }
            // The channel.
            engine::dj::StripSettings strip;
            strip.trim = juce::Decibels::decibelsToGain(deck.trimDb.load(std::memory_order_relaxed));
            strip.high = deck.killHigh.load(std::memory_order_relaxed) ? 0.0f : engine::dj::eqKnobGain(deck.high.load(std::memory_order_relaxed));
            strip.mid = deck.killMid.load(std::memory_order_relaxed) ? 0.0f : engine::dj::eqKnobGain(deck.mid.load(std::memory_order_relaxed));
            strip.low = deck.killLow.load(std::memory_order_relaxed) ? 0.0f : engine::dj::eqKnobGain(deck.low.load(std::memory_order_relaxed));
            strip.filter = deck.filter.load(std::memory_order_relaxed);
            strip.resonance = deck.resonance.load(std::memory_order_relaxed);
            deck.strip.process(left, right, n, strip);
            // Heard in the headphones before its fader.
            if (deck.cue.load(std::memory_order_relaxed)) {
                cueBus_.addFrom(0, 0, left, n);
                cueBus_.addFrom(1, 0, right, n);
            }
            const auto side = deck.side.load(std::memory_order_relaxed);
            const auto gain = engine::dj::faderGain(deck.fader.load(std::memory_order_relaxed))
                              * (side == 0 ? leftGain : side == 2 ? rightGain : 1.0f);
            const auto from = deck.gain;
            for (int i = 0; i < n; ++i) {
                const auto g = from + (gain - from) * static_cast<float>(i + 1) / static_cast<float>(n);
                left[i] *= g;
                right[i] *= g;
            }
            deck.gain = gain;
            // Its effect after the fader: an echo rings on once it is closed.
            engine::dj::EffectSettings effect;
            effect.kind = static_cast<engine::dj::EffectKind>(deck.effectKind.load(std::memory_order_relaxed));
            effect.on = deck.effectOn.load(std::memory_order_relaxed);
            effect.mix = deck.effectMix.load(std::memory_order_relaxed);
            effect.first = deck.effectFirst.load(std::memory_order_relaxed);
            effect.second = deck.effectSecond.load(std::memory_order_relaxed);
            const auto played = std::abs(speed) > 0.05 ? std::abs(speed) : 1.0;
            deck.effect.process(left, right, n, effect, beat / played, bpm > 1.0 ? (at - deck.firstBeat.load()) / beat : 0.0);
            auto peak = 0.0f;
            for (int i = 0; i < n; ++i)
                peak = std::max({ peak, std::abs(left[i]), std::abs(right[i]) });
            if (peak > deck.peak.load(std::memory_order_relaxed))
                deck.peak.store(peak, std::memory_order_relaxed);
            if(processor&&*processor) {
                modeInputs_[static_cast<std::size_t>(index)].copyFrom(0,0,left,n);
                modeInputs_[static_cast<std::size_t>(index)].copyFrom(1,0,right,n);
            } else { mix_.addFrom(0, 0, left, n); mix_.addFrom(1, 0, right, n); }
            deck.position.store(deck.player.position() / trackRate, std::memory_order_relaxed);
            deck.playing.store(deck.playingNow, std::memory_order_relaxed);
            deck.previewing.store(deck.cueHeld && !deck.playingNow, std::memory_order_relaxed);
            deck.looping.store(deck.player.looping(), std::memory_order_relaxed);
            deck.loopStart.store(deck.player.loopStart() / trackRate, std::memory_order_relaxed);
            deck.loopEnd.store(deck.player.loopEnd() / trackRate, std::memory_order_relaxed);
        }
        if(processor&&*processor)(*processor)->process(modeBlocks.data(),4,mix_.getWritePointer(0),mix_.getWritePointer(1),n);
        mix_.applyGain(0, n, masterGain);
        // The limiter: down at once to what keeps the peak under the
        // ceiling, back up over a tenth of a second.
        if (limiter_.load(std::memory_order_relaxed)) {
            constexpr float ceiling = 0.966f; // -0.3 dBFS
            const auto recover = 1.0f - std::exp(-1.0f / static_cast<float>(0.1 * rate_));
            auto* left = mix_.getWritePointer(0);
            auto* right = mix_.getWritePointer(1);
            auto lowest = 1.0f;
            for (int i = 0; i < n; ++i) {
                const auto level = std::max(std::abs(left[i]), std::abs(right[i]));
                const auto wanted = level * limiterGain_ > ceiling ? ceiling / level : 1.0f;
                limiterGain_ = wanted < limiterGain_ ? wanted : limiterGain_ + (1.0f - limiterGain_) * recover;
                left[i] *= limiterGain_;
                right[i] *= limiterGain_;
                lowest = std::min(lowest, limiterGain_);
            }
            limiterReduction_.store(20.0f * std::log10(std::max(1.0e-6f, lowest)), std::memory_order_relaxed);
        } else {
            limiterGain_ = 1.0f;
            limiterReduction_.store(0.0f, std::memory_order_relaxed);
        }
        for (int c = 0; c < 2; ++c)
            masterPeak = std::max(masterPeak, mix_.getMagnitude(c, 0, n));
        // Recorded as it is mixed.
        if (recording_.load(std::memory_order_acquire)) {
            const juce::ScopedTryLock lock(writerLock_);
            if (lock.isLocked() && writer_ != nullptr) {
                const float* channels[2] { mix_.getReadPointer(0), mix_.getReadPointer(1) };
                if (writer_->write(channels, n))
                    recordedFrames_.fetch_add(n, std::memory_order_relaxed);
            }
        }
        const auto outputs = std::min(2, output.buffer->getNumChannels());
        if (cueMode == CueMode::split && outputs == 2) {
            auto* outLeft = output.buffer->getWritePointer(0, output.startSample + done);
            auto* outRight = output.buffer->getWritePointer(1, output.startSample + done);
            const auto* mixLeft = mix_.getReadPointer(0);
            const auto* mixRight = mix_.getReadPointer(1);
            const auto* cueLeft = cueBus_.getReadPointer(0);
            const auto* cueRight = cueBus_.getReadPointer(1);
            for (int i = 0; i < n; ++i) {
                const auto mixed = 0.5f * (mixLeft[i] + mixRight[i]);
                const auto cued = 0.5f * (cueLeft[i] + cueRight[i]);
                outLeft[i] += mixed;
                outRight[i] += cued * (1.0f - cueMix) + mixed * cueMix;
            }
        } else {
            for (int c = 0; c < outputs; ++c)
                output.buffer->addFrom(c, output.startSample + done, mix_, c, 0, n);
        }
    }
    masterPeak_.store(std::max(masterPeak, masterPeak_.load(std::memory_order_relaxed) * fall), std::memory_order_relaxed);
    blocks_.fetch_add(1, std::memory_order_acq_rel);
}

// ---- Recording ---------------------------------------------------------------------------------

juce::Result DjEngine::startRecording(const juce::File& file)
{
    stopRecording();
    if (!file.getParentDirectory().createDirectory())
        return juce::Result::fail(juce::String::fromUTF8("Dossier impossible à créer : ") + file.getParentDirectory().getFullPathName());
    file.deleteFile();
    auto stream = file.createOutputStream();
    if (stream == nullptr)
        return juce::Result::fail(juce::String::fromUTF8("Fichier impossible à écrire : ") + file.getFullPathName());
    std::unique_ptr<juce::OutputStream> output { std::move(stream) };
    juce::WavAudioFormat wav;
    const auto options = juce::AudioFormatWriterOptions().withSampleRate(rate_).withNumChannels(2).withBitsPerSample(24);
    auto writer = wav.createWriterFor(output, options);
    if (writer == nullptr)
        return juce::Result::fail(juce::String::fromUTF8("Format impossible à écrire"));
    if (!writerThread_.isThreadRunning())
        writerThread_.startThread();
    {
        const juce::ScopedLock lock(writerLock_);
        writer_ = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(writer.release(), writerThread_,
                                                                            static_cast<int>(rate_ * 4.0));
        recordingFile_ = file;
        recordedFrames_.store(0, std::memory_order_relaxed);
    }
    recording_.store(true, std::memory_order_release);
    return juce::Result::ok();
}

void DjEngine::stopRecording()
{
    recording_.store(false, std::memory_order_release);
    const juce::ScopedLock lock(writerLock_);
    writer_.reset();
}

double DjEngine::recordedSeconds() const noexcept
{
    return static_cast<double>(recordedFrames_.load(std::memory_order_relaxed)) / rate_;
}

juce::File DjEngine::recordingFile() const
{
    const juce::ScopedLock lock(writerLock_);
    return recordingFile_;
}

} // namespace cydaw::app
