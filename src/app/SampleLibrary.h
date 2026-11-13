#include <cydaw/base/Thread.h>
#include <cydaw/base/Properties.h>
#include <cydaw/base/File.h>
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
    cydaw::base::Text path;
    cydaw::base::Text name;
    cydaw::base::Text extension;
    cydaw::base::Text root;
    juce::int64 size {};
    juce::int64 modified {};
    double sampleRate {};
    int channels {};
    int bitDepth {};
    double length {};
    float peakDb { -100.0f };
    float rmsDb { -100.0f };
    cydaw::base::Text type;     // one-shot, loop, long
    cydaw::base::Text category; // kick, snare, hat, ... ; empty when unknown
    cydaw::base::TextList tags;
    cydaw::base::Text key;
    cydaw::base::Text keySource; // "", "name" or "detected"
    float keyConfidence {};
    double bpm {};
    cydaw::base::Text bpmSource;  // "", "name" or "detected"
    float bpmConfidence {};
    cydaw::base::Text classifier; // "", "name", "bank" or "clap"
    float confidence {};
    bool favourite {};
    int rating {};
    bool userEdited {};
    bool analysed {};
    std::vector<std::uint8_t> overview; // 128 peaks, 0..255
    // Where it comes from: a sound bank's id ("vcsl"), empty for a folder of
    // the person's; the instrument of the catalogue playing it, when one does.
    cydaw::base::Text source;
    cydaw::base::Text instrument;

    [[nodiscard]] cydaw::base::File file() const { return cydaw::base::File(path); }
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
    cydaw::base::Text category;
    cydaw::base::Text instrument;
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
        cydaw::base::Text message;
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
    [[nodiscard]] cydaw::base::File cacheFolder() const;
    void setCacheFolder(const cydaw::base::File& folder);
    // The folder CyDAW 2 gives (Options › Emplacements): used, the index
    // taken along, and never written to these settings.
    void useCacheFolder(const cydaw::base::File& folder);
    [[nodiscard]] std::vector<cydaw::base::File> folders() const;
    void addFolder(const cydaw::base::File& folder);
    void removeFolder(const cydaw::base::File& folder);
    // The sound banks on this machine, each its own folder: scanned with the
    // person's folders when included (the default).
    struct Bank {
        cydaw::base::Text id;
        cydaw::base::Text title;
        cydaw::base::File folder;
    };
    void setBanks(std::vector<Bank> banks);
    [[nodiscard]] const std::vector<Bank>& banks() const noexcept { return banks_; }
    [[nodiscard]] bool includeBanks() const noexcept { return includeBanks_; }
    void setIncludeBanks(bool include);
    // The banks' catalogue, by file (hintKey of its path) and by bank
    // ("bank:<id>", weak): applied at the next scan, and to what is known now.
    void setHints(std::map<cydaw::base::Text, SampleHint> hints);
    // A file's hint: its own, else its bank's.
    [[nodiscard]] static const SampleHint* hintOf(const std::map<cydaw::base::Text, SampleHint>& hints, const cydaw::base::Text& path,
                                                  const cydaw::base::Text& source);
    [[nodiscard]] std::shared_ptr<const std::map<cydaw::base::Text, SampleHint>> hints() const { return hints_; }
    [[nodiscard]] static cydaw::base::Text hintKey(const cydaw::base::Text& path);
    // A bank's title, or the folder's name, for a record's source.
    [[nodiscard]] cydaw::base::Text sourceTitle(const SampleRecord& record) const;
    [[nodiscard]] int autoRescanMinutes() const noexcept { return autoRescanMinutes_; }
    void setAutoRescanMinutes(int minutes);
    [[nodiscard]] bool classifierEnabled() const noexcept { return classifierEnabled_; }
    void setClassifierEnabled(bool enabled);
    [[nodiscard]] cydaw::base::File classifierScript() const;
    void setClassifierScript(const cydaw::base::File& script);
    [[nodiscard]] cydaw::base::Text pythonCommand() const { return pythonCommand_; }
    void setPythonCommand(const cydaw::base::Text& command);

    // --- scanning
    void rescan(bool full = false);
    void cancel();
    [[nodiscard]] Progress progress() const;
    [[nodiscard]] bool isScanning() const noexcept;

    // --- records
    [[nodiscard]] const std::vector<SampleRecord>& samples() const noexcept { return samples_; }
    [[nodiscard]] const SampleRecord* find(const cydaw::base::Text& path) const;
    bool setFavourite(const cydaw::base::Text& path, bool favourite);
    bool setRating(const cydaw::base::Text& path, int rating);
    bool setCategory(const cydaw::base::Text& path, const cydaw::base::Text& category);
    bool setType(const cydaw::base::Text& path, const cydaw::base::Text& type);
    bool setTags(const cydaw::base::Text& path, const cydaw::base::TextList& tags);
    // A tempo typed in: kept through rescans, 0 clears it.
    bool setBpm(const cydaw::base::Text& path, double bpm);
    // The tempo of a file: the detector's estimate, snapped to the tempo a
    // whole number of bars would give this length — exported loops are
    // bar-exact, and the detector's errors are ratios (a dotted eighth for
    // a beat). Empty when nothing is trusted.
    struct TempoEstimate {
        double bpm {};
        float confidence {};
        cydaw::base::Text source; // "detected" or "length"
    };
    [[nodiscard]] static TempoEstimate estimateTempo(double detectedBpm,
                                                     double detectedConfidence,
                                                     double lengthSeconds,
                                                     bool looksLikeLoop);
    [[nodiscard]] cydaw::base::TextList distinctCategories() const;
    [[nodiscard]] cydaw::base::TextList distinctTypes() const;
    [[nodiscard]] cydaw::base::TextList distinctTags() const;
    [[nodiscard]] static cydaw::base::TextList categoryVocabulary();
    [[nodiscard]] static cydaw::base::TextList tagVocabulary();
    [[nodiscard]] static cydaw::base::TextList supportedExtensions();
    [[nodiscard]] static const std::vector<SampleCategory>& categoryTaxonomy();
    [[nodiscard]] static const std::vector<SampleFamily>& categoryFamilies();
    [[nodiscard]] static const SampleCategory* categoryInfo(const cydaw::base::Text& key);
    // The family of a category ("drums" for "kick"); "other" when unknown.
    [[nodiscard]] static cydaw::base::Text familyOf(const cydaw::base::Text& category);
    // A category of the factory catalogue ("pianos", "drums") as the
    // library's ("keys"; for a drum kit, what the name says, else "perc").
    [[nodiscard]] static cydaw::base::Text categoryFromCatalogue(const cydaw::base::Text& catalogueCategory,
                                                            const cydaw::base::Text& name,
                                                            const cydaw::base::Text& folder);

    // --- classification
    void classify(bool onlyUnclassified = true);
    [[nodiscard]] bool isClassifying() const noexcept;
    [[nodiscard]] cydaw::base::Text classifierStatus() const;
    // Categories found elsewhere (the semantic search's CLAP): given to the
    // records the person did not edit and nothing else named.
    struct Classified {
        cydaw::base::Text path;
        cydaw::base::Text category;
        float confidence {};
    };
    int applyClassified(const std::vector<Classified>& results);

    void addListener(Listener* listener);
    void removeListener(Listener* listener);
    void saveNow();
    // A value of the library's settings file (others keep theirs there: the
    // semantic search).
    [[nodiscard]] cydaw::base::Value setting(const cydaw::base::Text& key, const cydaw::base::Value& fallback);
    void setSetting(const cydaw::base::Text& key, const cydaw::base::Value& value);

    // --- pure helpers, shared with the suite
    [[nodiscard]] static cydaw::base::Text categoryFromName(const cydaw::base::Text& name,
                                                       const cydaw::base::Text& folder);
    [[nodiscard]] static cydaw::base::TextList tagsFromPath(const cydaw::base::Text& name,
                                                        const cydaw::base::Text& relativeFolder);
    [[nodiscard]] static cydaw::base::Text keyFromName(const cydaw::base::Text& name);
    // The key a chroma of the whole sound points to, with how clearly:
    // empty when no key stands out — drums, noise, a single pitch.
    struct KeyEstimate {
        cydaw::base::Text key;
        float confidence {};
    };
    [[nodiscard]] static KeyEstimate estimateKey(const std::vector<float>& chroma);
    [[nodiscard]] static std::vector<float> chromaOf(const juce::AudioBuffer<float>& audio,
                                                     double sampleRate);
    [[nodiscard]] static double bpmFromName(const cydaw::base::Text& name);
    [[nodiscard]] static cydaw::base::Text typeFor(double lengthSeconds, const cydaw::base::Text& name,
                                              double bpm);
    // One hit of a drum (a kick, a cymbal…), not a loop of it: no tempo. A
    // short one, or one its name says is a hit ("_hit", "_rr2", "roll").
    [[nodiscard]] static bool isDrumHit(const cydaw::base::Text& category, const cydaw::base::Text& name, const cydaw::base::Text& folder,
                                        double lengthSeconds);
    [[nodiscard]] static SampleRecord analyseFile(const cydaw::base::File& file, const cydaw::base::File& root,
                                                  const SampleRecord* previous,
                                                  const SampleHint* hint = nullptr);
    // What the catalogue says, on a record the person did not edit.
    static void applyHint(SampleRecord& record, const SampleHint& hint);
    [[nodiscard]] static cydaw::base::Value serialise(const SampleRecord& record);
    [[nodiscard]] static std::optional<SampleRecord> restore(const cydaw::base::Value& value);
    // The cache's binary form (sample-library.bin).
    static void write(juce::OutputStream& out, const SampleRecord& record);
    [[nodiscard]] static std::optional<SampleRecord> read(juce::InputStream& in);

private:
    class Scanner;
    class Classifier;
    friend class Scanner;
    friend class Classifier;
    struct Root {
        cydaw::base::File folder;
        cydaw::base::Text source; // a bank's id, empty for a folder of the person's
    };

    void timerCallback() override;
    [[nodiscard]] cydaw::base::PropertiesFile& settingsFile();
    void loadSettings();
    void saveSettings();
    void loadCache();
    void saveCache();
    void cacheLoaded(std::vector<SampleRecord> records, int generation);
    [[nodiscard]] static std::vector<SampleRecord> readCacheFile(const cydaw::base::File& folder);
    static bool writeCacheFile(const cydaw::base::File& folder, const std::vector<SampleRecord>& records);
    [[nodiscard]] std::vector<Root> roots() const;
    // Records of folders no longer scanned forgotten; a scan running on the
    // old folders started again on the new ones.
    void pruneToRoots();
    void restartScan();
    void reindex();
    [[nodiscard]] cydaw::base::File indexFile() const;
    [[nodiscard]] cydaw::base::File defaultCacheFolder() const;
    void mergeRecords(std::vector<SampleRecord> records, bool finished, bool complete,
                      std::set<cydaw::base::Text> present, std::vector<cydaw::base::File> roots);
    void applyClassification(std::vector<SampleRecord> results, bool finished,
                             cydaw::base::Text message);
    void notify(bool structure);
    void notifyProgress();
    void markDirty();
    SampleRecord* findMutable(const cydaw::base::Text& path);

    cydaw::base::ApplicationProperties properties_;
    std::unique_ptr<cydaw::base::PropertiesFile> otherSettings_;
    cydaw::base::File cacheFolder_;
    bool cacheFolderGiven_ {};
    std::vector<cydaw::base::File> folders_;
    std::vector<Bank> banks_;
    bool includeBanks_ { true };
    std::shared_ptr<const std::map<cydaw::base::Text, SampleHint>> hints_;
    bool started_ {};
    bool loaded_ {};
    // Each reading of the cache; one overtaken (the folder changed) is dropped.
    int loadGeneration_ {};
    // The name rules changed since the last scan: the categories the names
    // gave are given again (nothing read again).
    bool rulesOutdated_ {};
    // The cache's reading and writing, one at a time, off the message thread.
    cydaw::base::ThreadPool io_ { 1 };
    std::atomic<bool> saving_ {};
    bool saveAgain_ {};
    int autoRescanMinutes_ { 10 };
    bool classifierEnabled_ {};
    cydaw::base::File classifierScript_;
    cydaw::base::Text pythonCommand_ { "python" };

    std::vector<SampleRecord> samples_;
    std::map<cydaw::base::Text, std::size_t> index_;
    std::unique_ptr<Scanner> scanner_;
    std::unique_ptr<Classifier> classifier_;
    juce::ListenerList<Listener> listeners_;
    bool dirty_ {};
    // A rescan asked for while one runs — a folder added mid-scan — is
    // owed, and starts when the running one ends.
    bool pendingRescan_ {};
    bool pendingFull_ {};
    juce::int64 lastScanMs_ {};
    cydaw::base::Text classifierMessage_;
    std::atomic<int> found_ {};
    std::atomic<int> analysed_ {};
    std::atomic<int> toAnalyse_ {};
    std::atomic<int> classified_ {};
    std::atomic<int> toClassify_ {};

    JUCE_DECLARE_WEAK_REFERENCEABLE(SampleLibrary)
};

} // namespace cydaw::app
