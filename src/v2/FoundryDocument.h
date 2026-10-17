#pragma once

#include <cydaw/engine/FoundrySession.h>

#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>
#include <vector>

// What a Foundry tab holds: a session (tracks of clips) as edited, every
// edit a step that can be undone, its markers and regions. Opened from a
// sound file, it is that file on one track (saved back as the file, its
// markers in a WAV's cue points); several clips or tracks are a session,
// saved as a .cyfoundry file (the sounds edited beside it, the others where
// they are) and mixed down to a sound file.

namespace cydaw::v2 {

struct FoundryMarker {
    std::int64_t start {};
    std::int64_t end {};       // == start: a marker; after it: a region
    juce::String name;

    [[nodiscard]] bool region() const noexcept { return end > start; }
};

class FoundryDocument final {
public:
    using Session = engine::foundry::Session;
    using Sound = engine::foundry::Sound;

    // A sound file read whole (with its cue points, its bits); null with
    // `error` said.
    [[nodiscard]] static std::shared_ptr<const Sound> readSound(const juce::File& file, juce::String& error, int* bits = nullptr,
                                                                std::vector<FoundryMarker>* markers = nullptr);
    // A sound file (one track, one clip) or a .cyfoundry session.
    [[nodiscard]] static std::unique_ptr<FoundryDocument> open(const juce::File& file, juce::String& error);
    // Empty: one track, at `rate`.
    [[nodiscard]] static std::unique_ptr<FoundryDocument> create(double rate, const juce::String& name);

    [[nodiscard]] const Session& session() const noexcept { return *session_; }
    [[nodiscard]] std::shared_ptr<const Session> shared() const noexcept { return session_; }
    [[nodiscard]] juce::String name() const;
    [[nodiscard]] bool dirty() const noexcept { return dirty_; }
    [[nodiscard]] int bitsPerSample() const noexcept { return bits_; }
    // The sound file it is (one clip, untouched in its place), else none.
    [[nodiscard]] const juce::File& file() const noexcept { return file_; }
    [[nodiscard]] const juce::File& sessionFile() const noexcept { return sessionFile_; }
    // One track, one clip from 0, played whole, no gain nor fades: it is a
    // sound file, saved as one.
    [[nodiscard]] bool plainFile() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

    float renderTail() const noexcept { return renderTail_; }
    void setRenderTail(float seconds) { renderTail_=std::clamp(seconds,0.0f,30.0f);changed(); }
    const juce::var& processing() const noexcept { return processing_; }
    void setProcessing(juce::var state) { processing_=std::move(state);changed(); }
    void apply(Session next, const juce::String& label);
    // A change that is no step to undo (a track's gain, its mute, as on a
    // mixer), the file only marked changed.
    void replace(Session next);
    void apply(Session next, std::vector<FoundryMarker> markers, const juce::String& label);
    [[nodiscard]] bool canUndo() const noexcept { return !undo_.empty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !redo_.empty(); }
    [[nodiscard]] juce::String undoLabel() const;
    [[nodiscard]] juce::String redoLabel() const;
    bool undo();
    bool redo();
    [[nodiscard]] juce::StringArray history() const;

    [[nodiscard]] const std::vector<FoundryMarker>& markers() const noexcept { return markers_; }
    void setMarkers(std::vector<FoundryMarker> markers, const juce::String& label);
    // As an edit on every track leaves them: `removed` frames gone at `at`,
    // `inserted` put there.
    [[nodiscard]] std::vector<FoundryMarker> shiftedMarkers(std::int64_t at, std::int64_t removed, std::int64_t inserted) const;

    // The plain file written as `file` (wav, flac, aiff, ogg by its
    // extension), its markers in a WAV.
    juce::Result save(const juce::File& file, int bits);
    // The session as a .cyfoundry file (the sounds edited in a folder beside).
    juce::Result saveSession(const juce::File& file);
    // The mix of [start, end) (end <= start: all of it) as a sound file.
    juce::Result exportMix(const juce::File& file, int bits, std::int64_t start, std::int64_t end) const;
    // A sound written as a file (any thread).
    [[nodiscard]] static juce::Result writeSound(const Sound& sound, const juce::File& file, int bits,
                                                 const std::vector<FoundryMarker>& markers = {});

private:
    struct Step {
        std::shared_ptr<const Session> session;
        std::vector<FoundryMarker> markers;
        juce::String label;
    };
    void changed();
    [[nodiscard]] static std::unique_ptr<FoundryDocument> openSession(const juce::File& file, juce::String& error);

    std::shared_ptr<const Session> session_;
    std::vector<FoundryMarker> markers_;
    std::vector<Step> undo_;
    std::vector<Step> redo_;
    float renderTail_{};
    juce::var processing_;
    juce::File file_;
    juce::File sessionFile_;
    juce::String name_;
    int bits_ { 24 };
    bool dirty_ {};
    std::uint64_t revision_ { 1 };
};

} // namespace cydaw::v2
