#include <cydaw/base/Utilities.h>
#include <cydaw/base/Thread.h>
#include <cydaw/integration/AudioFiles.h>
#include <cydaw/base/File.h>
#pragma once


#include <array>
#include <atomic>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

// The DJ mode's collection: the tracks of the folders chosen (and those
// loaded from elsewhere), with what their analysis found (tempo, grid, key,
// loudness, length, tags) and what the DJ set on them (cue point, hot cues,
// a grid fixed by hand), kept from one session to the next in
// %APPDATA%/CyDAW/DjCollection.json — the software's, not a project's.
// CYDAW_DJ_COLLECTION_FILE names another file (a test's); an automated
// session without one neither reads nor writes the person's.
//
// Scanning the folders and analysing tracks run on a thread of their own.

namespace cydaw::v2 {

struct DjTrack {
    cydaw::base::File file;
    cydaw::base::Text title;
    cydaw::base::Text artist;
    double seconds {};
    double bpm {};
    double firstBeat {};
    bool gridSet {};     // fixed by hand: an analysis keeps it
    int key { -1 };
    double loudness { -70.0 };
    bool analysed {};
    cydaw::base::Text error;  // why it could not be read
    double cuePoint {};
    std::array<double, 8> hotCues {};
    std::int64_t size {};
    std::int64_t modified {};
    int plays {};
    std::int64_t lastPlayed {};

    DjTrack() { hotCues.fill(-1.0); }
};

class DjLibrary final : private cydaw::base::Thread {
public:
    DjLibrary();
    ~DjLibrary() override;

    [[nodiscard]] static cydaw::base::File collectionFile();
    // Read once at the start (the folders scanned again).
    void load();
    // Saved now when it changed.
    void flush();
    // Called each frame: saves two seconds after a change.
    void tick();

    [[nodiscard]] cydaw::base::TextList folders() const;
    void addFolder(const cydaw::base::File& folder);
    void removeFolder(const cydaw::base::File& folder);
    // The folders looked through again on the thread.
    void rescan();

    // Changes with every change of the tracks (a list kept by the interface
    // takes them again).
    [[nodiscard]] int revision() const noexcept { return revision_.load(std::memory_order_acquire); }
    [[nodiscard]] std::vector<DjTrack> tracks() const;
    [[nodiscard]] std::optional<DjTrack> find(const cydaw::base::File& file) const;
    // A track loaded from elsewhere: kept in the collection.
    void remember(const cydaw::base::File& file);
    // What a deck learned of a track (analysis, cues, grid, a play).
    void update(const DjTrack& track);
    void analyse(const cydaw::base::File& file);
    // Every track not analysed yet.
    void analyseAll();

    struct Progress {
        bool scanning {};
        int waiting {};
        cydaw::base::Text current;
    };
    [[nodiscard]] Progress progress() const;

private:
    void run() override;
    void scan();
    void analyseNow(cydaw::audio::AudioFormatManager& formats, const cydaw::base::File& file);
    void changed();
    [[nodiscard]] cydaw::base::Value toJson() const;
    void fromJson(const cydaw::base::Value& document);
    [[nodiscard]] std::size_t indexOf(const cydaw::base::File& file) const;

    mutable std::mutex lock_;
    std::vector<DjTrack> tracks_;
    cydaw::base::TextList folders_;
    std::deque<cydaw::base::File> queue_;
    bool scanWanted_ {};
    bool scanning_ {};
    cydaw::base::Text current_;
    std::atomic<int> revision_ {};
    std::atomic<bool> dirty_ {};
    double dirtySince_ {};
    bool saving_ { true };
    cydaw::base::WaitableEvent wake_;
};

} // namespace cydaw::v2
