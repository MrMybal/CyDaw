#pragma once
#include "ModeProcessor.h"
#include <cydaw/realtime/RtPublisher.h>

#include <cydaw/engine/DjMix.h>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

// The DJ mode's sound: four decks, a mixer (a channel each, a crossfader,
// the master and the headphones), the mix recorded to a file. It joins the
// master's mixer as the workshop's simulator does: the master's effects, gain
// and meter apply after it.
//
// A track is decoded whole on a thread of its own, then analysed (tempo,
// beat grid, key, loudness, waveform); the deck plays as soon as it is
// decoded. What the interface asks of a deck that needs its exact place (a
// cue point, a loop, a jump) goes to the audio thread as a command; the
// knobs are read each block.

namespace cydaw::app {

// The tags a reader found (title, artist), else "Artist - Title" from the
// file's name.
void djTrackTags(const juce::File& file, const juce::StringPairArray& tags, juce::String& title, juce::String& artist);

class DjEngine final : public juce::AudioSource {
public:
    static constexpr int deckCount = 4;
    static constexpr int hotCueCount = 8;

    DjEngine();
    ~DjEngine() override;

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& output) override;

    // ---- Tracks ------------------------------------------------------------------------------
    // What is already known of a track (its grid and cues kept from before):
    // used instead of what the analysis finds.
    struct Known {
        double bpm {};
        double firstBeat {};
        bool hasGrid {};
        double cuePoint {};
        std::array<double, hotCueCount> hotCues {};  // < 0: none
        Known() { hotCues.fill(-1.0); }
    };
    enum class LoadState { empty, loading, ready, failed };
    void load(int deck, const juce::File& file, const Known& known = {});
    // A track decoded whole (45 minutes at most; its first two channels),
    // its tags read (else "Artist - Title" from its name); null with `error`
    // set. `cancelled` is asked between chunks. Any thread.
    [[nodiscard]] static std::shared_ptr<engine::dj::TrackAudio> decode(juce::AudioFormatManager& formats, const juce::File& file,
                                                                        juce::String& error, juce::String& title,
                                                                        juce::String& artist,
                                                                        const std::function<bool()>& cancelled = {});
    // Some decoders (Windows Media) need COM on the thread that decodes:
    // one of these at the top of such a thread.
    class DecodingThreadScope final {
    public:
        DecodingThreadScope();
        ~DecodingThreadScope();
        DecodingThreadScope(const DecodingThreadScope&) = delete;
        DecodingThreadScope& operator=(const DecodingThreadScope&) = delete;

        private:
        bool initialised_ {};
    };
    void eject(int deck);

    // ---- A deck as it is ---------------------------------------------------------------------
    struct DeckState {
        LoadState state { LoadState::empty };
        juce::File file;
        juce::String title;
        juce::String artist;
        juce::String error;
        // Null while it is analysed.
        std::shared_ptr<const engine::dj::TrackAnalysis> analysis;
        double seconds {};
        double position {};        // seconds
        bool playing {};
        bool previewing {};        // the cue held
        double tempo { 1.0 };      // the fader's ratio
        double speed {};           // as it plays (tempo, sync, bend)
        double bpm {};             // the track's (its grid)
        double firstBeat {};
        double playedBpm {};       // bpm × speed
        bool keylock { true };
        bool sync {};
        bool master {};
        bool looping {};
        double loopStart {};
        double loopEnd {};
        double cuePoint {};
        std::array<double, hotCueCount> hotCues {};
        float peak {};             // after its fader, held and falling 20 dB a second
        // Where the beat is against the master's (-0.5..0.5 of a beat); 0
        // without a master.
        double phase {};
    };
    [[nodiscard]] DeckState deck(int index) const;

    // ---- Playing -----------------------------------------------------------------------------
    void setPlaying(int deck, bool playing);
    // The cue button: playing, back to the cue point and stopped; stopped,
    // the cue point set here (unless it is there) and the track heard while
    // held, back to it when let go.
    void cue(int deck, bool down);
    void seek(int deck, double seconds);
    void setTempo(int deck, double ratio);
    // A nudge while held (+0.04: a little faster).
    void setBend(int deck, double amount);
    void setKeylock(int deck, bool on);
    void setSync(int deck, bool on);
    // -1: the master follows the decks (the loudest playing).
    void setMaster(int deck);
    [[nodiscard]] int masterDeck() const noexcept { return masterDeck_.load(std::memory_order_relaxed); }
    void setQuantize(bool on) noexcept { quantize_.store(on, std::memory_order_relaxed); }
    [[nodiscard]] bool quantize() const noexcept { return quantize_.load(std::memory_order_relaxed); }

    enum class HotCueAction { jumpOrSet, set, remove };
    void hotCue(int deck, int index, HotCueAction action);
    // A loop of `beats` from here (on the beat when quantized); 0: off.
    void loopBeats(int deck, double beats);
    void loopMove(int deck, double beats);
    // Twice as long (2) or half (0.5), from its start.
    void loopResize(int deck, double factor);
    void beatJump(int deck, double beats);
    // The grid: its tempo and a beat (seconds); "known" until the next load.
    void setGrid(int deck, double bpm, double firstBeat);
    // Scratching: on, the deck follows `seconds` as the hand moves it; off,
    // it plays on as it was.
    void scratch(int deck, bool on, double seconds);

    // ---- Mixer -------------------------------------------------------------------------------
    struct Channel {
        float trimDb {};
        float high { 0.5f }, mid { 0.5f }, low { 0.5f };   // EQ knobs, 0..1
        bool killHigh {}, killMid {}, killLow {};
        float filter {};          // -1..1
        float resonance { 0.3f };
        float fader { 1.0f };
        bool cue {};
        int side { 0 };           // the crossfader: 0 left, 1 none (thru), 2 right
        engine::dj::EffectSettings effect;
    };
    [[nodiscard]] Channel channel(int deck) const;
    void setChannel(int deck, const Channel& channel);
    void setCrossfader(float position) noexcept { crossfader_.store(position, std::memory_order_relaxed); }
    [[nodiscard]] float crossfader() const noexcept { return crossfader_.load(std::memory_order_relaxed); }
    void setCrossfaderCurve(float curve) noexcept { crossfaderCurve_.store(curve, std::memory_order_relaxed); }
    [[nodiscard]] float crossfaderCurve() const noexcept { return crossfaderCurve_.load(std::memory_order_relaxed); }
    void setMasterGainDb(float db) noexcept { masterGainDb_.store(db, std::memory_order_relaxed); }
    [[nodiscard]] float masterGainDb() const noexcept { return masterGainDb_.load(std::memory_order_relaxed); }
    // The headphones: off, or split (the mix on the left in mono, the cue
    // on the right) for one stereo output and a splitter cable.
    enum class CueMode : int { off = 0, split = 1 };
    void setCueMode(CueMode mode) noexcept { cueMode_.store(static_cast<int>(mode), std::memory_order_relaxed); }
    [[nodiscard]] CueMode cueMode() const noexcept { return static_cast<CueMode>(cueMode_.load(std::memory_order_relaxed)); }
    // 0 the cued channels only .. 1 the mix only.
    void setCueMix(float mix) noexcept { cueMix_.store(mix, std::memory_order_relaxed); }
    [[nodiscard]] float cueMix() const noexcept { return cueMix_.load(std::memory_order_relaxed); }
    // Held and falling 20 dB a second.
    [[nodiscard]] float masterPeak() const noexcept { return masterPeak_.load(std::memory_order_relaxed); }
    // How many decks the interface shows (2 or 4); the others stay silent.
    void setDeckCount(int count) noexcept { shownDecks_.store(count <= 2 ? 2 : 4, std::memory_order_relaxed); }
    [[nodiscard]] int shownDeckCount() const noexcept { return shownDecks_.load(std::memory_order_relaxed); }
    // The master's tempo (the master deck's as it plays), 0 for none.
    [[nodiscard]] double masterBpm() const noexcept { return masterBpm_.load(std::memory_order_relaxed); }
    // The master's limiter: the mix never goes over -0.3 dBFS (on by
    // default); how much it takes off now (dB, 0 or less).
    void setLimiter(bool on) noexcept { limiter_.store(on, std::memory_order_relaxed); }
    [[nodiscard]] bool limiter() const noexcept { return limiter_.load(std::memory_order_relaxed); }
    [[nodiscard]] float limiterReductionDb() const noexcept { return limiterReduction_.load(std::memory_order_relaxed); }
    // Whether any deck plays (the DJ mode then sounds even while hidden).
    [[nodiscard]] bool anyPlaying() const noexcept;

    // ---- The mix recorded --------------------------------------------------------------------
    juce::Result startRecording(const juce::File& file);
    void stopRecording();
    [[nodiscard]] bool recording() const noexcept { return recording_.load(std::memory_order_acquire); }
    [[nodiscard]] double recordedSeconds() const noexcept;
    [[nodiscard]] juce::File recordingFile() const;

    void setProcessor(std::shared_ptr<ModeProcessor> processor) {
        processor_.store(std::make_shared<const std::shared_ptr<ModeProcessor>>(std::move(processor)));
    }

private:
    realtime::RtPublisher<std::shared_ptr<ModeProcessor>> processor_;
    std::array<juce::AudioBuffer<float>,4> modeInputs_;
    struct Command {
        enum class Type : int { play, cueDown, cueUp, seek, hotCueJump, hotCueSet, hotCueRemove, loopBeats, loopMove,
                                loopResize, beatJump, syncOn };
        Type type {};
        int deck {};
        double value {};
        int index {};
    };
    struct Deck;
    class Loader;

    void push(const Command& command);
    void runCommands();
    void runCommand(Deck& deck, const Command& command);
    void takeLoadedTracks();
    void alignPhase(Deck& deck, int master);
    [[nodiscard]] int chooseMaster() const noexcept;
    [[nodiscard]] double beatSecondsOf(const Deck& deck) const noexcept;
    void publish(int deck, std::shared_ptr<const engine::dj::TrackAudio> audio, const juce::String& title,
                 const juce::String& artist, std::uint64_t ticket);
    void publishAnalysis(int deck, std::shared_ptr<const engine::dj::TrackAnalysis> analysis, std::uint64_t ticket);
    void fail(int deck, const juce::String& error, std::uint64_t ticket);
    void freeRetired();

    std::array<std::unique_ptr<Deck>, deckCount> decks_;
    juce::AbstractFifo fifo_ { 256 };
    std::array<Command, 256> commands_ {};
    std::mutex pushLock_;
    mutable std::mutex stateLock_;
    std::unique_ptr<Loader> loader_;

    double rate_ { 48'000.0 };
    int blockSize_ { 512 };
    juce::AudioBuffer<float> work_;
    juce::AudioBuffer<float> mix_;
    juce::AudioBuffer<float> cueBus_;
    std::atomic<std::uint64_t> blocks_ {};

    std::atomic<float> crossfader_ { 0.5f };
    std::atomic<float> crossfaderCurve_ { 0.0f };
    std::atomic<float> masterGainDb_ { 0.0f };
    std::atomic<int> cueMode_ { 0 };
    std::atomic<float> cueMix_ { 0.5f };
    std::atomic<float> masterPeak_ {};
    std::atomic<int> shownDecks_ { 2 };
    std::atomic<int> masterDeck_ { -1 };
    std::atomic<int> currentMaster_ { -1 };
    std::atomic<double> masterBpm_ {};
    std::atomic<bool> quantize_ { true };
    std::atomic<bool> limiter_ { true };
    std::atomic<float> limiterReduction_ {};
    float limiterGain_ { 1.0f };

    // Recording: written on a thread of its own.
    juce::TimeSliceThread writerThread_ { "CyDAW DJ mix writer" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer_;
    juce::CriticalSection writerLock_;
    std::atomic<bool> recording_ {};
    std::atomic<std::int64_t> recordedFrames_ {};
    juce::File recordingFile_;
};

} // namespace cydaw::app
