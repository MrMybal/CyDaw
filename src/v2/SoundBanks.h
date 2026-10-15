#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <vector>

// The sound banks CyDAW knows and what of them is on this machine. CyDAW
// ships their lists, the sampler maps that play them and the sound models
// made from them; the samples themselves are downloaded on request, from
// each bank's own repository, into the banks' folder (Options ›
// Emplacements), a few files at a time on threads of their own.
//
//   Factory/banks.json        { "format": "cydaw-banks", "version": 1,
//                               "banks": [ { "id": "vcsl", "title": …, "author": …,
//                                            "licence": "CC0-1.0", "licenceUrl": …, "homepage": …,
//                                            "repository": "sgossner/VCSL", "commit": "<sha>",
//                                            "list": "BankLists/vcsl.json", "count": …, "bytes": … } ] }
//   Factory/BankLists/<id>.json  { "files": [ ["path/in/the/bank.wav", 1234567, 1], … ] }
//                               (path, bytes, 1 when Git LFS keeps it)
//
// A file comes from raw.githubusercontent.com/<repository>/<commit>/<path>
// (media.githubusercontent.com for Git LFS), or from "baseUrl" + path when
// the bank gives one (a test's server). It is written into the downloads
// folder, checked against its size, then moved to <banks>/<id>/<path>;
// <banks>/<id>/bank.json says where it came from. Sampler maps name these
// files "bank:<id>/<path>". CYDAW_BANKS_REGISTRY names another banks.json.
//
// A bank with a "generator" ("cydaw-drums") is not downloaded but made here:
// each file of its list carries its recipe as a fourth element ({"kind":
// "kick", "values": {…}, "seed": 7, "seconds": 0.5, "gainDb": 0} for
// DrumSynth.h, {"text": "…"} for its licence), and becomes a WAV (16-bit,
// mono, at the bank's "rate") of exactly the size the list gives.

namespace cydaw::v2 {

class SoundBanks {
public:
    struct Bank {
        juce::String id;
        juce::String title;
        juce::String author;
        juce::String licence;
        juce::String licenceUrl;
        juce::String homepage;
        juce::String repository;
        juce::String commit;
        juce::String baseUrl;
        juce::String description; // what it holds
        // Not downloaded: made here by this generator ("cydaw-drums").
        juce::String generator;
        double rate { 48'000.0 };
        juce::File list;
        int count {};
        juce::int64 bytes {};
    };
    struct BankFile {
        juce::String path;
        juce::int64 size {};
        bool lfs {};
        // A generated bank's: how the file is made.
        juce::var recipe;
    };
    struct Wanted {
        juce::String bank;
        BankFile file;
    };
    // What a map (or a bank) still needs: the files not on disk yet, their
    // size, and the "bank:" files no list knows.
    struct Missing {
        std::vector<Wanted> files;
        juce::int64 bytes {};
        juce::StringArray banks;
        juce::StringArray unknown;
        // A bank still being counted: what is missing of it is not said yet.
        bool pending {};
        [[nodiscard]] bool empty() const noexcept { return files.empty(); }
    };

    SoundBanks();
    ~SoundBanks();

    // The registry: CYDAW_BANKS_REGISTRY, else the factory folder's.
    void loadRegistry();
    [[nodiscard]] juce::File registryFile() const;
    [[nodiscard]] const std::vector<Bank>& banks() const noexcept { return banks_; }
    [[nodiscard]] const Bank* find(const juce::String& id) const;
    // Every one of these banks is made here, none downloaded.
    [[nodiscard]] bool generatedOnly(const juce::StringArray& ids) const;
    [[nodiscard]] const std::vector<BankFile>& filesOf(const juce::String& id);
    [[nodiscard]] juce::File folderOf(const juce::String& id) const;

    // What of a bank is on disk: counted on a thread of its own (nothing
    // until it is), again after each download.
    struct OnDisk {
        int count {};
        juce::int64 bytes {};
    };
    [[nodiscard]] std::optional<OnDisk> onDisk(const juce::String& id);
    // Everything counted again (the folder moved, files put there by hand).
    void forgetDisk();

    // The "bank:" files of a sampler map (its JSON, or a .cysampler file,
    // whose answer is kept until something is downloaded) not on disk yet;
    // a whole bank's. Without `exact`, from the banks' counts (nothing asked
    // of the disk; a bank not counted yet is counted, and said `pending`);
    // with it, the disk asked now (before a download).
    [[nodiscard]] Missing missingFor(const juce::var& mapJson, bool exact = false);
    [[nodiscard]] Missing missingForFile(const juce::File& mapFile, bool exact = false);
    // Maps read ahead on a thread of their own (the factory's are 26 MB of
    // zones): until one is, missingForFile says it `pending` rather than
    // reading it on the caller's thread.
    void preloadMaps(std::vector<juce::File> maps);
    // Moves on when what is on disk may have changed (a download, a count).
    [[nodiscard]] int diskGeneration() const noexcept { return diskGeneration_; }
    [[nodiscard]] Missing missingOfBank(const juce::String& id);

    // Downloads what is missing (and each bank's licence and readme), as one
    // job named `label`; jobs run one after another.
    void download(const juce::String& label, const Missing& missing);
    // Stops the running job and drops the others; the files done stay.
    void cancel();
    [[nodiscard]] bool busy() const;
    struct Progress {
        bool running {};
        juce::String label;
        int done {};
        int total {};
        int failed {};
        int queued {};
        juce::int64 bytesDone {};
        juce::int64 bytesTotal {};
        double bytesPerSecond {};
        juce::String error;
        // Made here, not downloaded.
        bool generating {};
    };
    [[nodiscard]] Progress progress() const;
    struct Finished {
        juce::String label;
        juce::StringArray banks;
        int files {};
        int failed {};
        bool cancelled {};
        bool generated {};
        juce::String error;
    };
    // The jobs ended since the last call.
    [[nodiscard]] std::vector<Finished> takeFinished();

    // A bank's downloaded files, to the recycle bin.
    bool remove(const juce::String& id, juce::String& error);

private:
    struct Job {
        juce::String label;
        std::deque<Wanted> items;
        juce::StringArray banks;
        int total {};
        int done {};
        int failed {};
        juce::int64 bytesTotal {};
        juce::int64 bytesDone {};
        double started {};
        bool cancelled {};
        juce::String error;
    };
    class Worker;

    bool fetch(const Wanted& wanted, int generation, juce::String& error);
    bool generate(const Bank& bank, const Wanted& wanted, int generation, juce::String& error);
    [[nodiscard]] juce::String urlOf(const Bank& bank, const BankFile& file, bool lfs) const;
    void work(juce::Thread& thread);
    void finishFrontJob();
    void writeBankRecord(const Bank& bank) const;
    [[nodiscard]] std::vector<juce::String> bankPathsOf(const juce::var& mapJson) const;

    std::vector<Bank> banks_;
    std::map<juce::String, std::vector<BankFile>> lists_;
    std::map<juce::String, std::map<juce::String, std::size_t>> indexes_;
    std::mutex countLock_;
    std::map<juce::String, OnDisk> counted_;
    std::map<juce::String, std::set<juce::String>> absent_;
    std::set<juce::String> counting_;
    std::atomic<int> countGeneration_ {};
    struct MapFiles {
        juce::int64 stamp {};
        std::vector<juce::String> paths;
        int generation { -1 };
        Missing missing;
    };
    std::map<juce::String, MapFiles> maps_;
    // What the preloading thread read: each map's "bank:" paths and the stamp
    // they were read at; the maps it still has to read.
    struct Preloaded {
        juce::int64 stamp {};
        std::vector<juce::String> paths;
    };
    std::mutex preloadLock_;
    std::map<juce::String, Preloaded> preloaded_;
    std::set<juce::String> preloadWaiting_;
    // The banks' file lists read ahead the same way (some are thousands of
    // files), and those it still has to read.
    struct PreloadedList {
        std::vector<BankFile> files;
        std::map<juce::String, std::size_t> index;
    };
    std::map<juce::String, PreloadedList> preloadedLists_;
    std::set<juce::String> listsWaiting_;
    [[nodiscard]] static PreloadedList readList(const juce::File& list);
    std::thread preloader_;
    std::atomic<bool> stopPreload_ {};
    [[nodiscard]] static std::vector<juce::String> pathsOfMap(const juce::var& mapJson);
    int diskGeneration_ {};

    mutable std::mutex mutex_;
    std::deque<Job> jobs_;
    std::vector<Finished> finished_;
    int inFlight_ {};
    std::atomic<int> generation_ {};
    juce::WaitableEvent wake_;
    std::vector<std::unique_ptr<Worker>> workers_;
    // Last: gone (its counts finished) before what they write to.
    juce::ThreadPool counter_ { 1 };
};

} // namespace cydaw::v2
