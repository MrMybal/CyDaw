#pragma once

#include "DjLibrary.h"
#include "UiState.h"

#include <imgui.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

// The Mix DJ mode, a whole window as the workshop's: two or four decks
// (their tracks' coloured waveforms, cue, play, sync, hot cues, loops, beat
// jumps, tempo, an effect each) around a mixer (gain, a three-band EQ with
// kills, a filter, the headphones' cue, faders, a crossfader, the master,
// the mix recorded), and the collection under them (folders, search, sort,
// analysis). Its controls carry the names DJ gear gives them (CUE, SYNC,
// HI MID LOW): English, as on any mixer.

namespace cydaw::v2 {

class ImGuiHost;
class Studio;
class ModeProcessing;

class DjView final {
public:
    DjView(Studio& studio, UiState& state, ImGuiHost& host);
    ~DjView();

    void draw();
    bool undo(bool redo = false);
    // The transport bar's middle in this mode: the master's tempo, the
    // mix recorded.
    void drawTransport();
    // The control port's "dj" command.
    [[nodiscard]] juce::var command(const juce::var& request);
    void loadFile(int deck, const juce::File& file);
    // Files dropped on the window: the first deck that is not playing.
    bool dropFiles(const juce::StringArray& files);
    [[nodiscard]] DjLibrary& library();
    // Saved now (on quitting).
    void flush();
    void tickPersistence();

private:
    struct DeckUi {
        float zoomBeats { 16.0f };
        bool remaining { true };
        float tempoRange { 0.08f };
        double scratchFrom {};
        float scratchX {};
        float jumpBeats { 4.0f };
        float meter {};
        // The track whose analysis went into the collection, and whose
        // gain was set by it.
        juce::File learned;
        bool wasPlaying {};
        std::array<double, 8> cues {};
        double cuePoint {};
        // The whole track's waveform at a width.
        std::vector<std::array<float, 4>> overview;
        const void* overviewOf {};
        int overviewWidth {};
    };

    void ensureLoaded();
    void saveProcessing(bool force = false);
    std::shared_ptr<ModeProcessing> processing_;
    std::uint64_t processingRevision_{};
    std::uint64_t observedProcessingRevision_{};
    double processingChangedAt_{};
    std::atomic<bool> processingWriteFailed_{false};
    juce::ThreadPool processingWriter_{1};
    float processingHeight_ { 280.0f };
    void learnFromDecks();
    void drawDeck(int deck, ImVec2 size);
    void drawDeckHeader(int deck, float width);
    void drawOverview(int deck, ImVec2 size);
    void drawWaveform(int deck, ImVec2 size);
    void drawDeckButtons(int deck, float width);
    void drawTempo(int deck, ImVec2 size);
    void drawMixer(ImVec2 size);
    void drawChannel(int deck, ImVec2 size);
    void drawBrowser(ImVec2 size);
    void drawCollectionTree(ImVec2 size);
    void drawTrackTable(ImVec2 size);
    void trackMenu(const DjTrack& track);
    void refreshRows();
    void chooseFolder();
    void startRecording();
    [[nodiscard]] int freeDeck() const;
    [[nodiscard]] juce::var deckJson(int deck) const;

    Studio& studio_;
    UiState& state_;
    ImGuiHost& host_;
    std::unique_ptr<DjLibrary> library_;
    bool loaded_ {};
    std::array<DeckUi, 4> decks_ {};
    bool autoGain_ { true };
    float browserShare_ { 0.36f };
    // The collection as shown: filtered and sorted.
    std::array<char, 160> search_ {};
    juce::String shownQuery_;
    int folder_ { -1 };        // -1 every track, -2 played this session, else a folder
    int shownFolder_ { -3 };
    int shownRevision_ { -1 };
    int sortColumn_ { 1 };
    bool sortAscending_ { true };
    bool sortChanged_ {};
    std::vector<DjTrack> rows_;
    juce::File selected_;
    std::vector<juce::File> history_;
    std::unique_ptr<juce::FileChooser> chooser_;
};

} // namespace cydaw::v2
