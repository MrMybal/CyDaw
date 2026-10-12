#pragma once

#include "SampleCatalog.h"
#include "UiState.h"

#include <imgui.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <optional>
#include <vector>

// The library's big window (Affichage › Bibliothèque, F9, the browser's ⧉,
// the device strip's +): sounds, instruments and effects, each with a
// multi-column filter — every column a facet (category, instrument, bank,
// type, key, tempo, length…), several values chosen in each (Ctrl, Shift),
// each value's count as the other columns leave it — over a big sortable
// table, and a pane that previews what is chosen. In the studio's window or
// out of it (⧉), as large as wanted (the full window, ⤢). Opened by a "+",
// it says where a pick goes, and closes once something is put there (unless
// kept open).
//
// The sounds come from the shared SampleCatalog: a query is one pass over
// it, made again only when something it depends on changed.

namespace cydaw::v2 {

class ImGuiHost;

class LibraryWindow final {
public:
    LibraryWindow(Studio& studio, UiState& state, ImGuiHost& host, SampleCatalog& catalog);
    ~LibraryWindow();
    void draw();

    // ---- The control port --------------------------------------------------------
    struct Probe {
        bool shown {};
        int tab {};
        int rows {};
        int total {};
        double queryMs {};
        double buildMs {};
        double extractMs {};
        double frameMs {};
        std::vector<std::pair<juce::String, std::vector<std::pair<juce::String, int>>>> facets;
        juce::StringArray names; // the first rows
    };
    // A search shown: text, semantic, a facet's values chosen by key.
    void setSampleQuery(const juce::String& text, bool semantic, const std::vector<std::pair<int, juce::StringArray>>& chosen,
                        int sortColumn, bool ascending);
    [[nodiscard]] Probe probe();

private:
    struct Facets;
    struct InstrumentRow;
    struct EffectRow;
    class PluginScan;

    // A column of the multi-column filter, as drawn.
    struct FacetEntry {
        int value {};
        juce::String label;
        int count {};
        int family { -1 }; // a family's heading when the value is -1
    };
    struct FacetColumn {
        juce::String title;
        std::vector<FacetEntry> entries;
        std::vector<char>* selected {};
        float weight { 1.0f };
    };
    bool drawFacetColumns(const char* id, std::vector<FacetColumn>& columns, float height);

    void drawHeader();
    void drawSamples();
    void drawSamplePreview(const SampleCatalog::Item* item, float width);
    void drawInstruments();
    void drawEffects();
    void requery();
    void addSampleToTrack(const juce::String& path);
    void takeSemanticAnswer();
    void report(const juce::String& message, bool error = false);
    [[nodiscard]] std::optional<TrackRef> target() const;
    void picked();

    Studio& studio_;
    UiState& state_;
    ImGuiHost& host_;
    SampleCatalog& catalog_;
    std::shared_ptr<int> alive_ { std::make_shared<int>(0) };
    bool maximised_ {};
    bool keepOpen_ {};
    double frameMs_ {};

    // ---- Sounds.
    std::shared_ptr<const SampleCatalog::Data> data_;
    SampleCatalog::Query query_;
    SampleCatalog::Result result_;
    bool dirty_ { true };
    bool sortToTable_ {};
    int tableFrames_ {};
    char search_[200] {};
    std::string lastSearch_;
    bool semanticMode_ {};
    bool semanticPending_ {};
    double editedAt_ {};
    juce::String semanticAsked_;
    juce::String semanticEnglish_;
    juce::String semanticSimilarTo_;
    juce::String semanticError_;
    std::vector<std::pair<juce::String, float>> semanticHits_;
    juce::String selectedPath_;
    int selectedRow_ { -1 };
    bool scrollToSelected_ {};
    bool autoPreview_ { true };
    float facetHeight_ { 190.0f };
    std::unique_ptr<juce::FileChooser> chooser_;

    // ---- Instruments and effects: small, filtered each time they change.
    std::unique_ptr<Facets> instrumentFacets_;
    std::vector<InstrumentRow> instruments_;
    double instrumentsTime_ { -100.0 };
    bool downloading_ {};
    char instrumentSearch_[120] {};
    int selectedInstrument_ { -1 };
    std::unique_ptr<Facets> effectFacets_;
    std::vector<EffectRow> effects_;
    std::optional<TrackRef> effectsFor_;
    double effectsTime_ { -100.0 };
    char effectSearch_[120] {};
    int selectedEffect_ { -1 };
    std::unique_ptr<PluginScan> plugins_;
};

} // namespace cydaw::v2
