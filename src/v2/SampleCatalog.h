#include <cydaw/base/EventLoop.h>
#include <cydaw/base/Thread.h>
#include <cydaw/base/Text.h>
#pragma once

#include "SampleLibrary.h"

#include <juce_events/juce_events.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// The sample library made ready for browsing, shared by the browser's quick
// list and the library's big window: each sound's facets (category,
// instrument or bank, source, type, key, tempo, length) as small numbers,
// its words folded once into plain bytes, its display texts made once, the
// library in name order. Built on a thread of its own a moment after the
// library changes (at most once a second while a scan brings sounds); a
// query — the multi-column filter with each column's counts as the other
// columns leave them, the words, the order — is one pass over it: a few
// milliseconds for 50 000 sounds.

namespace cydaw::v2 {

class SampleCatalog final : private app::SampleLibrary::Listener, private cydaw::base::Timer {
public:
    enum Facet { category, group, source, type, key, tempo, length, facetCount };
    enum Column {
        colFavourite,
        colName,
        colShape,
        colCategory,
        colGroup,
        colSource,
        colType,
        colLength,
        colBpm,
        colKey,
        colChannels,
        colRate,
        colScore,
        columnCount
    };

    // A facet's value: its key ("kick", "bank:vcsl", a label), what is shown,
    // and for a category its family's index (categoryFamilies()).
    struct Value {
        cydaw::base::Text key;
        cydaw::base::Text label;
        int family { -1 };
    };
    struct Item {
        cydaw::base::Text path;
        cydaw::base::Text label; // the name, or "01 · its folder"
        cydaw::base::Text categoryText;
        cydaw::base::Text groupText;
        cydaw::base::Text sourceText;
        cydaw::base::Text typeText;
        cydaw::base::Text keyText;
        cydaw::base::Text lengthText;
        cydaw::base::Text bpmText;
        cydaw::base::Text rateText;
        std::array<std::uint16_t, facetCount> facets {};
        int family {};
        float length {};
        float bpm {};
        int channels {};
        float rate {};
        bool favourite {};
        bool aiCategory {};
        bool bank {};
    };
    // The quick list's tree: family › category › instrument or bank, and the
    // sources; counts of the whole library.
    struct TreeNode {
        cydaw::base::Text key;
        cydaw::base::Text label;
        int count {};
        std::vector<TreeNode> children;
    };
    struct Data {
        std::vector<Item> items;
        std::vector<std::string> haystacks;
        std::array<std::vector<Value>, facetCount> values;
        // A value's place in its column's order (for sorting by it).
        std::array<std::vector<int>, facetCount> ranks;
        std::vector<int> byName;
        std::vector<int> nameRank;
        std::vector<TreeNode> categoryTree;
        std::vector<TreeNode> sourceTree;
        std::unordered_map<cydaw::base::Text, int> byPath;
        int generation {};
        double buildMs {};
        double extractMs {};
        [[nodiscard]] int find(const cydaw::base::Text& path) const;
        [[nodiscard]] int valueOf(Facet facet, const cydaw::base::Text& key) const;
    };

    struct Query {
        // Per facet, per value: kept (1) or not (0); empty: the whole column.
        std::array<std::vector<char>, facetCount> selected;
        cydaw::base::Text text;
        bool favouritesOnly {};
        // A semantic answer, best first: only these, in this order.
        const std::vector<std::pair<cydaw::base::Text, float>>* ranked {};
        int sortColumn { colName };
        bool ascending { true };
    };
    struct Result {
        std::vector<int> rows;
        std::vector<float> scores; // beside the rows, for a ranked query
        std::array<std::vector<int>, facetCount> counts;
        int matched {};
        double ms {};
    };

    explicit SampleCatalog(app::SampleLibrary& library);
    ~SampleCatalog() override;

    // Called on the message thread when new data is there; cleared (nullptr)
    // before the one who gave it goes.
    void addOnChange(const void* owner, std::function<void()> callback);
    void removeOnChange(const void* owner);
    [[nodiscard]] std::shared_ptr<const Data> data() const;
    [[nodiscard]] bool building() const noexcept { return building_.load(); }
    // Built again now (the library edited in a way the view must follow at once).
    void refresh();

    [[nodiscard]] static Result run(const Data& data, const Query& query);
    // Small letters without accents, as plain bytes.
    [[nodiscard]] static std::string fold(const cydaw::base::Text& text);
    // For the performance check: `count` sounds made from the library's.
    [[nodiscard]] std::shared_ptr<const Data> synthetic(int count) const;

private:
    struct Raw;
    struct Extract;
    void sampleLibraryChanged(bool structure) override;
    void timerCallback() override;
    void startBuild();
    // What is copied on the message thread: references to the records'
    // texts, nothing made; the rest is done by build().
    [[nodiscard]] Extract extract() const;
    [[nodiscard]] static std::shared_ptr<Data> build(Extract extract, int generation);

    app::SampleLibrary& library_;
    mutable std::mutex mutex_;
    std::shared_ptr<const Data> data_;
    std::vector<std::pair<const void*, std::function<void()>>> callbacks_;
    std::atomic<bool> building_ {};
    bool dirty_ { true };
    bool again_ {};
    double changedAt_ {};
    double builtAt_ {};
    int generation_ {};
    cydaw::base::ThreadPool pool_ { 1 };

    template<class> friend class cydaw::base::WeakReference;
    cydaw::base::CallbackLifetime callbackLifetime_;
};

} // namespace cydaw::v2
