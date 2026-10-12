#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace cydaw::app {

// One sample as the library knows it: where it is, what the file says, what
// its sound measures, and what it was called — by its name and folders, by
// the CLAP classifier when that ran, or by the person editing it.
struct SampleRecord {
    juce::String path;
    juce::String name;
    juce::String extension;
    juce::String root;
    juce::int64 size {};
    juce::int64 modified {};
    double sampleRate {};
    int channels {};
    int bitDepth {};
    double length {};
    float peakDb { -100.0f };
    float rmsDb { -100.0f };
    juce::String type;     // one-shot, loop, long
    juce::String category; // kick, snare, hat, ... ; empty when unknown
    juce::StringArray tags;
    juce::String key;
    juce::String keySource; // "", "name" or "detected"
    float keyConfidence {};
    double bpm {};
    juce::String bpmSource;  // "", "name" or "detected"
    float bpmConfidence {};
    juce::String classifier; // "", "name", "bank" or "clap"
    float confidence {};
    bool favourite {};
    int rating {};
    bool userEdited {};
    bool analysed {};
    std::vector<std::uint8_t> overview; // 128 peaks, 0..255
    // Where it comes from: a sound bank's id ("vcsl"), empty for a folder of
    // the person's; the instrument of the catalogue playing it, when one does.
    juce::String source;
    juce::String instrument;

    [[nodiscard]] juce::File file() const { return juce::File(path); }
};

// The categories a sample is put in, by family (drums, instruments, synths,
// effects, other), with their names and the English phrases CLAP is asked
// about them with.
struct SampleCategory {
    const char* key;
    const char* family;
    const char* french;
    const char* english;
    std::vector<const char*> prompts;
};
struct SampleFamily {
    const char* key;
    const char* french;
    const char* english;
};

// What the banks' catalogue says of a file before it is read: the category
// and instrument it plays, and whether it is one note of an instrument (no
// tempo, no key to detect). A weak hint is a whole bank's ("bank:<id>"): it
// only fills what the name and the folders left empty.
struct SampleHint {
    juce::String category;
    juce::String instrument;
    bool note {};
    bool weak {};
};

// The sample library: folders scanned in the background by a pool of
// threads (the person's, and the sound banks' when included), records kept
// in a cache the person chooses the place of (sample-library.bin, read and
// written on a thread of its own), an automatic rescan, and an optional
// classifier (CLAP, through tools/samples/classify_samples.py, with a model
// installed beforehand) that names what the file name did not. Everything
// public runs on the message thread; the threads report back through it.
class SampleLibrary final : private juce::Timer {
public:
    struct Listener {
        virtual ~Listener() = default;
        // structure: records came or went; otherwise fields changed.
        virtual void sampleLibraryChanged(bool structure) = 0;
        virtual void sampleLibraryProgress() {}
    };
    struct Progress {
        bool scanning {};
        bool classifying {};
        int found {};
        int analysed {};
        int toAnalyse {};
        int classified {};
        int toClassify {};
        juce::String message;
    };

    // `startNow` false: nothing read nor scanned until start() — the cache
    // folder and the banks given first (CyDAW 2).
    explicit SampleLibrary(bool startNow = true);
    ~SampleLibrary() override;
    // The cache read on a thread of its own, then the scans.
    void start();
    // The cache read (until then the library is empty, and says so).
    [[nodiscard]] bool loaded() const noexcept { return loaded_; }

    // --- settings, persisted (CYDAW_SAMPLES_SETTINGS names another file: a
    // test's)
    [[nodiscard]] juce::File cacheFolder() const;
    void setCacheFolder(const juce::File& folder);
    // The folder CyDAW 2 gives (Options › Emplacements): used, the index
    // taken along, and never written to these settings.
    void useCacheFolder(const juce::File& folder);
    [[nodiscard]] std::vector<juce::File> folders() const;
    void addFolder(const juce::File& folder);
    void removeFolder(const juce::File& folder);
    // The sound banks on this machine, each its own folder: scanned with the
    // person's folders when included (the default).
    struct Bank {
        juce::String id;
        juce::String title;
        juce::File folder;
    };
    void setBanks(std::vector<Bank> banks);
    [[nodiscard]] const std::vector<Bank>& banks() const noexcept { return banks_; }
    [[nodiscard]] bool includeBanks() const noexcept { return includeBanks_; }
    void setIncludeBanks(bool include);
    // The banks' catalogue, by file (hintKey of its path) and by bank
    // ("bank:<id>", weak): applied at the next scan, and to what is known now.
    void setHints(std::map<juce::String, SampleHint> hints);
    // A file's hint: its own, else its bank's.
    [[nodiscard]] static const SampleHint* hintOf(const std::map<juce::String, SampleHint>& hints, const juce::String& path,
                                                  const juce::String& source);
    [[nodiscard]] std::shared_ptr<const std::map<juce::String, SampleHint>> hints() const { return hints_; }
    [[nodiscard]] static juce::String hintKey(const juce::String& path);
    // A bank's title, or the folder's name, for a record's source.
    [[nodiscard]] juce::String sourceTitle(const SampleRecord& record) const;
    [[nodiscard]] int autoRescanMinutes() const noexcept { return autoRescanMinutes_; }
    void setAutoRescanMinutes(int minutes);
    [[nodiscard]] bool classifierEnabled() const noexcept { return classifierEnabled_; }
    void setClassifierEnabled(bool enabled);
    [[nodiscard]] juce::File classifierScript() const;
    void setClassifierScript(const juce::File& script);
    [[nodiscard]] juce::String pythonCommand() const { return pythonCommand_; }
    void setPythonCommand(const juce::String& command);

    // --- scanning
    void rescan(bool full = false);
    void cancel();
    [[nodiscard]] Progress progress() const;
    [[nodiscard]] bool isScanning() const noexcept;

    // --- records
    [[nodiscard]] const std::vector<SampleRecord>& samples() const noexcept { return samples_; }
    [[nodiscard]] const SampleRecord* find(const juce::String& path) const;
    bool setFavourite(const juce::String& path, bool favourite);
    bool setRating(const juce::String& path, int rating);
    bool setCategory(const juce::String& path, const juce::String& category);
    bool setType(const juce::String& path, const juce::String& type);
    bool setTags(const juce::String& path, const juce::StringArray& tags);
    // A tempo typed in: kept through rescans, 0 clears it.
    bool setBpm(const juce::String& path, double bpm);
    // The tempo of a file: the detector's estimate, snapped to the tempo a
    // whole number of bars would give this length — exported loops are
    // bar-exact, and the detector's errors are ratios (a dotted eighth for
    // a beat). Empty when nothing is trusted.
    struct TempoEstimate {
        double bpm {};
        float confidence {};
        juce::String source; // "detected" or "length"
    };
    [[nodiscard]] static TempoEstimate estimateTempo(double detectedBpm,
                                                     double detectedConfidence,
                                                     double lengthSeconds,
                                                     bool looksLikeLoop);
    [[nodiscard]] juce::StringArray distinctCategories() const;
    [[nodiscard]] juce::StringArray distinctTypes() const;
    [[nodiscard]] juce::StringArray distinctTags() const;
    [[nodiscard]] static juce::StringArray categoryVocabulary();
    [[nodiscard]] static juce::StringArray tagVocabulary();
    [[nodiscard]] static juce::StringArray supportedExtensions();
    [[nodiscard]] static const std::vector<SampleCategory>& categoryTaxonomy();
    [[nodiscard]] static const std::vector<SampleFamily>& categoryFamilies();
    [[nodiscard]] static const SampleCategory* categoryInfo(const juce::String& key);
    // The family of a category ("drums" for "kick"); "other" when unknown.
    [[nodiscard]] static juce::String familyOf(const juce::String& category);
    // A category of the factory catalogue ("pianos", "drums") as the
    // library's ("keys"; for a drum kit, what the name says, else "perc").
    [[nodiscard]] static juce::String categoryFromCatalogue(const juce::String& catalogueCategory,
                                                            const juce::String& name,
                                                            const juce::String& folder);

    // --- classification
    void classify(bool onlyUnclassified = true);
    [[nodiscard]] bool isClassifying() const noexcept;
    [[nodiscard]] juce::String classifierStatus() const;
    // Categories found elsewhere (the semantic search's CLAP): given to the
    // records the person did not edit and nothing else named.
    struct Classified {
        juce::String path;
        juce::String category;
        float confidence {};
    };
    int applyClassified(const std::vector<Classified>& results);

    void addListener(Listener* listener);
    void removeListener(Listener* listener);
    void saveNow();
    // A value of the library's settings file (others keep theirs there: the
    // semantic search).
    [[nodiscard]] juce::var setting(const juce::String& key, const juce::var& fallback);
    void setSetting(const juce::String& key, const juce::var& value);

    // --- pure helpers, shared with the suite
    [[nodiscard]] static juce::String categoryFromName(const juce::String& name,
                                                       const juce::String& folder);
    [[nodiscard]] static juce::StringArray tagsFromPath(const juce::String& name,
                                                        const juce::String& relativeFolder);
    [[nodiscard]] static juce::String keyFromName(const juce::String& name);
    // The key a chroma of the whole sound points to, with how clearly:
    // empty when no key stands out — drums, noise, a single pitch.
    struct KeyEstimate {
        juce::String key;
        float confidence {};
    };
    [[nodiscard]] static KeyEstimate estimateKey(const std::vector<float>& chroma);
    [[nodiscard]] static std::vector<float> chromaOf(const juce::AudioBuffer<float>& audio,
                                                     double sampleRate);
    [[nodiscard]] static double bpmFromName(const juce::String& name);
    [[nodiscard]] static juce::String typeFor(double lengthSeconds, const juce::String& name,
                                              double bpm);
    // One hit of a drum (a kick, a cymbal…), not a loop of it: no tempo. A
    // short one, or one its name says is a hit ("_hit", "_rr2", "roll").
    [[nodiscard]] static bool isDrumHit(const juce::String& category, const juce::String& name, const juce::String& folder,
                                        double lengthSeconds);
    [[nodiscard]] static SampleRecord analyseFile(const juce::File& file, const juce::File& root,
                                                  const SampleRecord* previous,
                                                  const SampleHint* hint = nullptr);
    // What the catalogue says, on a record the person did not edit.
    static void applyHint(SampleRecord& record, const SampleHint& hint);
    [[nodiscard]] static juce::var serialise(const SampleRecord& record);
    [[nodiscard]] static std::optional<SampleRecord> restore(const juce::var& value);
    // The cache's binary form (sample-library.bin).
    static void write(juce::OutputStream& out, const SampleRecord& record);
    [[nodiscard]] static std::optional<SampleRecord> read(juce::InputStream& in);

private:
    class Scanner;
    class Classifier;
    friend class Scanner;
    friend class Classifier;
    struct Root {
        juce::File folder;
        juce::String source; // a bank's id, empty for a folder of the person's
    };

    void timerCallback() override;
    [[nodiscard]] juce::PropertiesFile& settingsFile();
    void loadSettings();
    void saveSettings();
    void loadCache();
    void saveCache();
    void cacheLoaded(std::vector<SampleRecord> records, int generation);
    [[nodiscard]] static std::vector<SampleRecord> readCacheFile(const juce::File& folder);
    static bool writeCacheFile(const juce::File& folder, const std::vector<SampleRecord>& records);
    [[nodiscard]] std::vector<Root> roots() const;
    // Records of folders no longer scanned forgotten; a scan running on the
    // old folders started again on the new ones.
    void pruneToRoots();
    void restartScan();
    void reindex();
    [[nodiscard]] juce::File indexFile() const;
    [[nodiscard]] juce::File defaultCacheFolder() const;
    void mergeRecords(std::vector<SampleRecord> records, bool finished, bool complete,
                      std::set<juce::String> present, std::vector<juce::File> roots);
    void applyClassification(std::vector<SampleRecord> results, bool finished,
                             juce::String message);
    void notify(bool structure);
    void notifyProgress();
    void markDirty();
    SampleRecord* findMutable(const juce::String& path);

    juce::ApplicationProperties properties_;
    std::unique_ptr<juce::PropertiesFile> otherSettings_;
    juce::File cacheFolder_;
    bool cacheFolderGiven_ {};
    std::vector<juce::File> folders_;
    std::vector<Bank> banks_;
    bool includeBanks_ { true };
    std::shared_ptr<const std::map<juce::String, SampleHint>> hints_;
    bool started_ {};
    bool loaded_ {};
    // Each reading of the cache; one overtaken (the folder changed) is dropped.
    int loadGeneration_ {};
    // The name rules changed since the last scan: the categories the names
    // gave are given again (nothing read again).
    bool rulesOutdated_ {};
    // The cache's reading and writing, one at a time, off the message thread.
    juce::ThreadPool io_ { 1 };
    std::atomic<bool> saving_ {};
    bool saveAgain_ {};
    int autoRescanMinutes_ { 10 };
    bool classifierEnabled_ {};
    juce::File classifierScript_;
    juce::String pythonCommand_ { "python" };

    std::vector<SampleRecord> samples_;
    std::map<juce::String, std::size_t> index_;
    std::unique_ptr<Scanner> scanner_;
    std::unique_ptr<Classifier> classifier_;
    juce::ListenerList<Listener> listeners_;
    bool dirty_ {};
    // A rescan asked for while one runs — a folder added mid-scan — is
    // owed, and starts when the running one ends.
    bool pendingRescan_ {};
    bool pendingFull_ {};
    juce::int64 lastScanMs_ {};
    juce::String classifierMessage_;
    std::atomic<int> found_ {};
    std::atomic<int> analysed_ {};
    std::atomic<int> toAnalyse_ {};
    std::atomic<int> classified_ {};
    std::atomic<int> toClassify_ {};

    JUCE_DECLARE_WEAK_REFERENCEABLE(SampleLibrary)
};

} // namespace cydaw::app
