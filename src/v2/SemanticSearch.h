#pragma once

#include "AiModels.h"
#include "Companion.h"
#include "SampleLibrary.h"

#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

// The semantic sample search, optional: with a CLAP model installed (Options ›
// Recherche sémantique) and a Python that runs it (the companion's
// environment, with PyTorch and transformers), every sound of the library
// gets a vector, and so does a phrase — "dark punchy kick", "grosse caisse
// sombre" (French words are put into English first: the model reads English)
// — and the nearest sounds are the answer. A sound's own vector finds the
// sounds like it. The sounds no name nor bank put in a category get the
// category CLAP finds nearest, when it is clear enough.
//
// Without a model, nothing of this runs and the browser searches the names
// and the categories.
//
// The model runs in tools/companion/cydaw_companion/clap_worker.py, a child
// process listening on 127.0.0.1 for this one connection (JSON lines); CyDAW
// decodes the audio itself and hands it over as float32 files in the cache.
// The vectors are kept in <cache>/samples/semantic/<model>@<version>.cyvec
// (float16), by file, with its size and date: a changed file is indexed again.

namespace cydaw::v2 {

class SemanticSearch final : private juce::Timer, private app::SampleLibrary::Listener {
public:
    SemanticSearch(app::SampleLibrary& library, AiModels& models, Companion& companion);
    ~SemanticSearch() override;

    // Called when something changed that the interface shows (any thread);
    // cleared (nullptr) before the one who gave it goes.
    void setOnChange(std::function<void()> callback);

    // Automatic indexing and categories; off for automated sessions until
    // they ask.
    void setAutomatic(bool automatic);

    // ---- The model --------------------------------------------------------------
    // The person's choice when installed, else the first installed (the
    // catalogue's default first); empty when none is.
    [[nodiscard]] juce::String activeModel() const;
    void setActiveModel(const juce::String& id);
    // A model installed and some sounds indexed with it: a phrase can be asked.
    [[nodiscard]] bool available() const;
    // A model was installed or removed: the index follows.
    void modelsChanged();

    // ---- The Python that runs it -----------------------------------------------------
    struct Engine {
        bool checking {};
        bool checked {};
        bool ready {};   // torch and transformers are there
        bool cuda {};
        juce::String python;
        juce::String executable;
        juce::String torch;
        juce::String transformers;
        juce::String gpu;
        juce::String error;
    };
    [[nodiscard]] Engine engine() const;
    void checkEngine();

    // ---- The index ----------------------------------------------------------------
    struct Status {
        bool running {};
        bool loadingModel {};
        bool workerUp {};
        juce::String device; // "cuda" or "cpu", once loaded
        int indexed {};     // sounds of the library with a vector
        int total {};       // sounds of the library
        int pending {};     // of this run, still to do
        int done {};        // of this run
        int failed {};
        int classified {};  // categories given by CLAP (last run)
        juce::String message;
        juce::String error;
    };
    [[nodiscard]] Status status() const;
    void startIndexing();
    void stopIndexing();
    [[nodiscard]] bool autoIndex() const noexcept { return autoIndex_; }
    void setAutoIndex(bool on);
    [[nodiscard]] bool autoCategories() const noexcept { return autoCategories_; }
    void setAutoCategories(bool on);
    // The categories CLAP would give the sounds nothing named, given now.
    void classifyNow();

    // ---- Searching ----------------------------------------------------------------
    struct Hit {
        juce::String path;
        float similarity {};
        float score {}; // 0..100, the model's usual range spread
    };
    struct Answer {
        int id {};
        bool failed {};
        juce::String error;
        juce::String query;
        juce::String english; // what the model was asked
        juce::String similarTo;
        std::vector<Hit> hits;
        // How the scores were spread, and how many were asked for.
        float low {};
        float span { 1.0f };
        int limit {};
        int owner {};
    };
    // Answered on a thread of their own (the model may have to load first);
    // the answer is taken with takeAnswer() by whoever asked (`owner`: the
    // browser's list, the library window), the newest of each only. Returns
    // the request's id.
    int searchText(const juce::String& text, int limit = 400, int owner = 0);
    int searchSimilar(const juce::String& path, int limit = 400, int owner = 0);
    [[nodiscard]] std::optional<Answer> takeAnswer(int owner = 0);
    [[nodiscard]] bool searching() const noexcept { return searching_.load() > 0; }
    // The same, waited for (the control port).
    [[nodiscard]] Answer searchNow(const juce::String& text, int limit, int timeoutMs);
    [[nodiscard]] Answer similarNow(const juce::String& path, int limit, int timeoutMs);

    // French words of sound into English ones; English kept as it is.
    [[nodiscard]] static juce::String toEnglish(const juce::String& query);
    // On the message thread, for a phrase's answer: the sounds whose name,
    // category, instrument or bank hold the phrase's words lifted a little
    // (the model hears; the names say), then the best `limit` kept.
    static void rerank(Answer& answer, app::SampleLibrary& library, int limit);

private:
    class Worker;
    class Store;
    class Indexer;
    struct Query {
        int id {};
        juce::String text;
        juce::String similarTo;
        int limit {};
        int owner {};
    };
    int ask(Query query);

    void timerCallback() override;
    void sampleLibraryChanged(bool structure) override;
    void changed();
    void loadSettings();
    void saveSettings();
    // The store of the active model, read when it changes.
    void openStore();
    [[nodiscard]] std::shared_ptr<Store> store() const;
    [[nodiscard]] std::shared_ptr<Worker> workerFor(const AiModel& model, juce::String& error, const std::atomic<bool>* stop);
    [[nodiscard]] Answer run(const Query& query);
    void post(Answer answer);
    void stopWorker();
    [[nodiscard]] juce::File storeFolder() const;
    [[nodiscard]] juce::File storeFileOf(const AiModel& model) const;
    [[nodiscard]] juce::File workFolder() const;

    app::SampleLibrary& library_;
    AiModels& models_;
    Companion& companion_;
    bool automatic_ { true };
    bool autoIndex_ { true };
    bool autoCategories_ { true };
    juce::String chosenModel_;

    mutable std::mutex mutex_;
    std::mutex changeMutex_;
    std::function<void()> onChange_;
    Engine engine_;
    Status status_;
    std::shared_ptr<Store> store_;
    juce::String storeModel_;
    // Where the store was read from: read again when the cache moves.
    juce::File storeFile_;
    std::shared_ptr<Worker> worker_;
    juce::String workerModel_;
    // One model process made at a time; closing stops one loading.
    std::mutex creating_;
    std::atomic<bool> closing_ {};
    // The indexed count, made again when the index or the library changed.
    int countedGeneration_ { -1 };
    const void* countedStore_ {};
    bool libraryChanged_ { true };
    double workerUsed_ {};
    std::unique_ptr<Indexer> indexer_;
    // The newest answer and the newest query of each owner.
    std::map<int, Answer> answers_;
    std::map<int, int> latest_;
    std::atomic<int> searching_ {};
    std::atomic<int> nextQuery_ { 1 };
    // Queries one at a time, the newest first; the engine check.
    juce::ThreadPool queries_ { 1 };
    juce::ThreadPool checks_ { 1 };
    // A rescan ended: new sounds to index, once things are quiet.
    bool indexOwed_ {};
    double indexOwedAt_ {};

    JUCE_DECLARE_WEAK_REFERENCEABLE(SemanticSearch)
};

} // namespace cydaw::v2
