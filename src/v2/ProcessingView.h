#pragma once

#include "Processing.h"
#include "UiState.h"

#include <cydaw/engine/FoundrySession.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

// The Traitements mode: processings made and tried (audio: steps over a
// sound; MIDI: a note script over a clip), and the window that applies one to
// a clip (its settings, its sound or its notes before and after, heard in
// turn). Processing.h holds what they are and how they run.

namespace cydaw::app {
class FoundryPlayer;
}

namespace cydaw::v2 {

class Studio;
class ImGuiHost;
class ModeProcessing;

// A processing at work on a sound: its settings, its rendering (on a thread
// of its own, again after each change), the sound before and after it, heard
// in turn on the Processing mode's player.
class AudioTrial final {
public:
    AudioTrial() = default;
    ~AudioTrial();
    AudioTrial(const AudioTrial&) = delete;
    AudioTrial& operator=(const AudioTrial&) = delete;

    void setSound(engine::repair::Channels channels, double rate, const juce::String& name);
    [[nodiscard]] bool hasSound() const noexcept { return before_ != nullptr; }
    [[nodiscard]] const juce::String& soundName() const noexcept { return soundName_; }
    // The processing tried (its parameters back to its own values).
    void setProcessing(const processing::Processing& processing);
    [[nodiscard]] const processing::Processing& processing() const noexcept { return processing_; }
    // The processing changed (its steps, its script): rendered again, the values kept.
    void processingChanged(const processing::Processing& processing);
    [[nodiscard]] processing::Values& values() noexcept { return values_; }
    void changed();
    // Each frame: a rendering started when due, a finished one taken.
    void tick();
    [[nodiscard]] bool rendering() const noexcept { return !jobs_.empty() && jobs_.back()->generation == generation_; }
    // A rendering running, or due (a setting just moved): the result shown is not the settings' yet.
    [[nodiscard]] bool busy() const noexcept { return dueAt_ >= 0.0 || rendering(); }
    [[nodiscard]] double progress() const noexcept;
    [[nodiscard]] const processing::AudioResult* result() const noexcept { return after_ ? &result_ : nullptr; }
    [[nodiscard]] double rate() const noexcept { return rate_; }
    [[nodiscard]] std::int64_t sourceFrames() const noexcept { return before_ ? before_->frames() : 0; }
    [[nodiscard]] const engine::repair::Channels& sourceChannels() const noexcept { return source_; }

    // The interface: its settings (true when one moved), its tail, the two timelines.
    bool drawParameters(const std::vector<processing::Parameter>& parameters);
    bool drawTail(engine::repair::TailSettings& tail);
    void drawBeforeAfter(app::FoundryPlayer& player, ImGuiHost& host, float width);
    void stop(app::FoundryPlayer& player);

private:
    struct Job {
        std::thread thread;
        std::atomic<bool> cancel { false };
        std::atomic<bool> done { false };
        std::atomic<double> progress { 0.0 };
        processing::AudioResult result;
        std::uint64_t generation {};
    };
    void start();
    [[nodiscard]] std::shared_ptr<const engine::foundry::Session> sessionOf(int which) const;

    processing::Processing processing_;
    processing::Values values_;
    engine::repair::Channels source_;
    double rate_ { 48'000.0 };
    juce::String soundName_;
    std::shared_ptr<const engine::foundry::Sound> before_;
    std::shared_ptr<const engine::foundry::Sound> after_;
    processing::AudioResult result_;
    std::vector<std::unique_ptr<Job>> jobs_;
    std::uint64_t generation_ {};
    double dueAt_ { -1.0 };
    int heard_ {};            // 0 before, 1 after
    const void* playedSound_ {};
};

// A MIDI processing on a clip: its settings, its notes before and after; the
// clip heard through its track (the editor's preview), the processed notes in
// it while "after" plays, put back unless applied.
class MidiTrial final {
public:
    explicit MidiTrial(Studio& studio) : studio_(studio) {}
    ~MidiTrial();
    void setClip(const TrackRef& track, std::uint64_t clipId);
    void clear();
    [[nodiscard]] bool hasClip() const noexcept { return clipId_ != 0; }
    void setProcessing(const processing::Processing& processing);
    void processingChanged(const processing::Processing& processing);
    [[nodiscard]] processing::Values& values() noexcept { return values_; }
    void changed();
    [[nodiscard]] const processing::NoteScriptResult& result() const noexcept { return result_; }
    bool drawParameters(const std::vector<processing::Parameter>& parameters);
    void drawBeforeAfter(float width);
    // The processed notes made the clip's (one undo step).
    juce::Result apply();
    void stopListening();
    [[nodiscard]] const TrackRef& track() const noexcept { return track_; }
    [[nodiscard]] std::uint64_t clipId() const noexcept { return clipId_; }

private:
    void hear(int which);
    Studio& studio_;
    TrackRef track_;
    std::uint64_t clipId_ {};
    double clipLength_ {};
    std::vector<processing::ScriptNote> before_;
    processing::Processing processing_;
    processing::Values values_;
    processing::NoteScriptResult result_;
    std::uint32_t seed_ { 1 };
    int heard_ { -1 };        // -1 silent, 0 before, 1 after
};

class ProcessingView final {
public:
    ProcessingView(Studio& studio, UiState& state, ImGuiHost& host);
    ~ProcessingView();

    // The mode's window.
    void draw();
    // The apply window for a clip (from its menu); drawn every frame.
    void openApply(const ClipRef& clip);
    void drawApply();
    // A sound dropped on the mode: the sound tried.
    bool dropFiles(const juce::StringArray& files);
    // The `processing` command.
    [[nodiscard]] juce::var command(const juce::var& request);
    // Unsaved changes to the processing edited (empty: none).
    [[nodiscard]] juce::String unsaved() const;

private:
    void reload();
    void select(int index);
    void edited();
    void drawLibrary(float width);
    void drawEditor();
    void drawSteps(float width);
    void drawStep(processing::Step& step, int index);
    void drawScript();
    void drawTrial();
    void drawCode(std::vector<char>& buffer, const char* id, float height);
    void chooseTestSound();
    void saveEdited(bool asNew);
    void remove();
    void stopEverything();

    Studio& studio_;
    UiState& state_;
    ImGuiHost& host_;
    processing::Kind kind_ { processing::Kind::audio };
    std::vector<processing::Processing> library_;
    int selected_ { -1 };
    processing::Processing edited_;
    bool dirty_ {};
    int step_ { -1 };
    std::vector<char> code_;      // the code step's / the note script's text
    int codeFor_ { -2 };          // the step the code buffer holds (-1: the script)
    juce::String codeError_;
    bool codeChanged_ {};
    double codeDueAt_ { -1.0 };
    std::unique_ptr<ModeProcessing> chain_;
    int chainFor_ { -1 };
    std::uint64_t chainRevision_ {};
    AudioTrial trial_;
    MidiTrial midiTrial_;
    std::unique_ptr<juce::FileChooser> chooser_;
    char nameBuffer_[128] {};
    char descriptionBuffer_[512] {};
    // The apply window.
    bool applyOpen_ {};
    bool applyRequested_ {};
    ClipRef applyClip_;
    juce::String applyClipName_;
    std::vector<processing::Processing> applyChoices_;
    int applyChoice_ {};
    AudioTrial applyTrial_;
    MidiTrial applyMidi_;
    juce::String applyError_;
};

} // namespace cydaw::v2
