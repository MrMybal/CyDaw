#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

// The optional AI models CyDAW can use (the semantic sample search's CLAP),
// none shipped: each is downloaded on request, from its own publisher, once
// its licence is shown — and accepted by the person when it asks to be.
//
//   Factory/models.json      { "format": "cydaw-ai-models", "version": 1,
//                              "engine": { "packages": [ {name, licence, url} ] },
//                              "models": [ { "id", "version", "title", "provider", "kind": "clap",
//                                            "default", "validated", "description": {fr, en},
//                                            "homepage", "licence": { "name", "url", "page",
//                                            "requiresAcceptance", "note": {fr, en} },
//                                            "source": { "host", "revision", "baseUrl" },
//                                            "files": [ { "path", "size", "sha256" } ],
//                                            "dimension", "sampleRate", "windowSeconds",
//                                            "similarity": [low, high] } ] }
//
// A file comes from baseUrl + path (pinned to a revision), into the downloads
// folder as .part (a download stopped goes on from where it was), checked
// against its size and SHA-256, then moved to <models>/<id>/<version>/<path>
// (Options › Emplacements). receipt.json says what was installed, from where,
// when, and the licence shown; a model is installed when its receipt is there
// and every file has its size. CYDAW_MODELS_CATALOGUE names another catalogue
// (a test's, whose baseUrl is a local server).

namespace cydaw::v2 {

struct AiModel {
    juce::String id;
    juce::String version;
    juce::String title;
    juce::String provider;
    juce::String kind; // "clap"
    bool isDefault {};
    bool validated {};
    juce::var description; // {fr, en}
    juce::String homepage;
    juce::String licence;
    juce::String licenceUrl;
    juce::String licencePage;
    juce::var licenceNote; // {fr, en}
    bool requiresAcceptance {};
    juce::String host;
    juce::String revision;
    juce::String baseUrl;
    struct File {
        juce::String path;
        juce::int64 size {};
        juce::String sha256;
    };
    std::vector<File> files;
    int dimension {};
    double sampleRate { 48'000.0 };
    double windowSeconds { 10.0 };
    float similarityLow { 0.05f };
    float similarityHigh { 0.5f };

    [[nodiscard]] juce::String key() const { return id + "@" + version; }
    [[nodiscard]] juce::int64 bytes() const;
    // In the interface's language.
    [[nodiscard]] juce::String describe() const;
    [[nodiscard]] juce::String note() const;
};

// SHA-256, fed a piece at a time (a download is hashed as it comes).
class Sha256 final {
public:
    Sha256();
    void add(const void* data, std::size_t size);
    [[nodiscard]] juce::String hex();
    // A whole file's; empty when it cannot be read; `stop` ends it early.
    [[nodiscard]] static juce::String ofFile(const juce::File& file, const std::atomic<bool>* stop = nullptr);

private:
    void block(const std::uint8_t* data);
    std::uint32_t state_[8];
    std::uint8_t buffer_[64] {};
    std::size_t filled_ {};
    std::uint64_t length_ {};
};

class AiModels final {
public:
    AiModels();
    ~AiModels();

    void loadCatalogue();
    [[nodiscard]] juce::File catalogueFile() const;
    [[nodiscard]] const std::vector<AiModel>& models() const noexcept { return models_; }
    [[nodiscard]] const AiModel* find(const juce::String& id) const;
    // The engine's packages and their licences, to show beside the models'.
    struct Package {
        juce::String name;
        juce::String licence;
        juce::String url;
    };
    [[nodiscard]] const std::vector<Package>& enginePackages() const noexcept { return packages_; }

    [[nodiscard]] juce::File folderOf(const AiModel& model) const;
    [[nodiscard]] bool installed(const juce::String& id) const;
    // When it was installed, and whether the person accepted its licence then.
    [[nodiscard]] juce::var receiptOf(const juce::String& id) const;

    // Downloads a model on a thread of its own. Refused (false, `error`) when
    // its licence asks to be accepted and was not, when one runs already.
    bool install(const juce::String& id, bool licenceAccepted, juce::String& error);
    // The same from a folder holding its files (another program's copy):
    // each checked against its size and SHA-256, then copied.
    bool importFrom(const juce::String& id, const juce::File& folder, bool licenceAccepted, juce::String& error);
    void cancel();
    // Its files to the recycle bin.
    bool remove(const juce::String& id, juce::String& error);

    struct Progress {
        bool running {};
        juce::String id;
        juce::String step; // "download", "verify", "copy"
        juce::String file;
        juce::int64 done {};
        juce::int64 total {};
        double bytesPerSecond {};
        // The last run's end, once: its model, an error, or cancelled.
        juce::String error;
    };
    [[nodiscard]] Progress progress() const;
    struct Finished {
        juce::String id;
        bool ok {};
        bool cancelled {};
        juce::String error;
    };
    [[nodiscard]] std::optional<Finished> takeFinished();

private:
    class Job;
    bool start(const juce::String& id, bool licenceAccepted, const juce::File& from, juce::String& error);

    std::vector<AiModel> models_;
    std::vector<Package> packages_;
    std::unique_ptr<Job> job_;
    mutable std::mutex mutex_;
    Progress progress_;
    std::optional<Finished> finished_;
    friend class Job;
};

} // namespace cydaw::v2
