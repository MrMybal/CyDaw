#include <cydaw/base/Utilities.h>
#include <cydaw/base/File.h>
#pragma once


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
    cydaw::base::Text id;
    cydaw::base::Text version;
    cydaw::base::Text title;
    cydaw::base::Text provider;
    cydaw::base::Text kind; // "clap"
    bool isDefault {};
    bool validated {};
    cydaw::base::Value description; // {fr, en}
    cydaw::base::Text homepage;
    cydaw::base::Text licence;
    cydaw::base::Text licenceUrl;
    cydaw::base::Text licencePage;
    cydaw::base::Value licenceNote; // {fr, en}
    bool requiresAcceptance {};
    cydaw::base::Text host;
    cydaw::base::Text revision;
    cydaw::base::Text baseUrl;
    struct File {
        cydaw::base::Text path;
        std::int64_t size {};
        cydaw::base::Text sha256;
    };
    std::vector<File> files;
    int dimension {};
    double sampleRate { 48'000.0 };
    double windowSeconds { 10.0 };
    float similarityLow { 0.05f };
    float similarityHigh { 0.5f };

    [[nodiscard]] cydaw::base::Text key() const { return id + "@" + version; }
    [[nodiscard]] std::int64_t bytes() const;
    // In the interface's language.
    [[nodiscard]] cydaw::base::Text describe() const;
    [[nodiscard]] cydaw::base::Text note() const;
};

// SHA-256, fed a piece at a time (a download is hashed as it comes).
class Sha256 final {
public:
    Sha256();
    void add(const void* data, std::size_t size);
    [[nodiscard]] cydaw::base::Text hex();
    // A whole file's; empty when it cannot be read; `stop` ends it early.
    [[nodiscard]] static cydaw::base::Text ofFile(const cydaw::base::File& file, const std::atomic<bool>* stop = nullptr);

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
    [[nodiscard]] cydaw::base::File catalogueFile() const;
    [[nodiscard]] const std::vector<AiModel>& models() const noexcept { return models_; }
    [[nodiscard]] const AiModel* find(const cydaw::base::Text& id) const;
    // The engine's packages and their licences, to show beside the models'.
    struct Package {
        cydaw::base::Text name;
        cydaw::base::Text licence;
        cydaw::base::Text url;
    };
    [[nodiscard]] const std::vector<Package>& enginePackages() const noexcept { return packages_; }

    [[nodiscard]] cydaw::base::File folderOf(const AiModel& model) const;
    [[nodiscard]] bool installed(const cydaw::base::Text& id) const;
    // When it was installed, and whether the person accepted its licence then.
    [[nodiscard]] cydaw::base::Value receiptOf(const cydaw::base::Text& id) const;

    // Downloads a model on a thread of its own. Refused (false, `error`) when
    // its licence asks to be accepted and was not, when one runs already.
    bool install(const cydaw::base::Text& id, bool licenceAccepted, cydaw::base::Text& error);
    // The same from a folder holding its files (another program's copy):
    // each checked against its size and SHA-256, then copied.
    bool importFrom(const cydaw::base::Text& id, const cydaw::base::File& folder, bool licenceAccepted, cydaw::base::Text& error);
    void cancel();
    // Its files to the recycle bin.
    bool remove(const cydaw::base::Text& id, cydaw::base::Text& error);

    struct Progress {
        bool running {};
        cydaw::base::Text id;
        cydaw::base::Text step; // "download", "verify", "copy"
        cydaw::base::Text file;
        std::int64_t done {};
        std::int64_t total {};
        double bytesPerSecond {};
        // The last run's end, once: its model, an error, or cancelled.
        cydaw::base::Text error;
    };
    [[nodiscard]] Progress progress() const;
    struct Finished {
        cydaw::base::Text id;
        bool ok {};
        bool cancelled {};
        cydaw::base::Text error;
    };
    [[nodiscard]] std::optional<Finished> takeFinished();

private:
    class Job;
    bool start(const cydaw::base::Text& id, bool licenceAccepted, const cydaw::base::File& from, cydaw::base::Text& error);

    std::vector<AiModel> models_;
    std::vector<Package> packages_;
    // What the catalogue file held when the list was made.
    cydaw::base::Text catalogueText_;
    bool loaded_ {};
    std::unique_ptr<Job> job_;
    mutable std::mutex mutex_;
    Progress progress_;
    std::optional<Finished> finished_;
    friend class Job;
};

} // namespace cydaw::v2
