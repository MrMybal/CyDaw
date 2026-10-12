#pragma once

#include "SampleCatalog.h"
#include "UiState.h"

#include <imgui.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <map>
#include <memory>
#include <optional>
#include <vector>

// The browser's quick list (the big window is LibraryWindow): samples,
// instruments and devices to bring into the session. Samples: the library's
// folders and the sound banks, a tree of their categories (by family, then
// the instrument or the bank) and of their sources, a search by name — or,
// with a model installed, by description and by likeness (the semantic
// search, optional) —, a table that previews on click and drags into the
// arrangement; all of it read from the shared SampleCatalog. Instruments:
// the factory instruments by category (a double click makes a track), a
// sampler from files or an SFZ. Devices: the catalogue, inserted on the
// selected track with a double click. Each opens in the big window (⤢).

namespace cydaw::v2 {

class ImGuiHost;

class BrowserView final : private app::SampleLibrary::Listener {
public:
    BrowserView(Studio& studio, UiState& state, ImGuiHost& host, SampleCatalog& catalog);
    ~BrowserView() override;
    void draw();
    // The samples tab in front with this file selected (control port).
    void selectSample(const juce::File& file);
    // The samples tab in front showing a search and a filter (control port).
    void browse(const juce::String& query, bool semantic, const juce::String& family, const juce::String& category,
                const juce::String& source);

private:
    void sampleLibraryChanged(bool structure) override;
    void sampleLibraryProgress() override;
    void drawSamples();
    void drawSampleTree(float height);
    void drawSemanticLine();
    void drawSampleMenu(const app::SampleRecord& record);
    void drawInstruments();
    void drawDevices();
    void report(const juce::String& message, bool error = false);
    void openLarge(int tab);
    // The factory catalogue, read again now and then while shown.
    std::vector<Studio::FactoryInstrument> instruments_;
    double instrumentsTime_ { -100.0 };
    // A download was running last frame (its end lists them again).
    bool downloading_ {};
    void refilter();
    void askSimilar(const app::SampleRecord& record);
    void takeSemanticAnswer();

    Studio& studio_;
    UiState& state_;
    ImGuiHost& host_;
    SampleCatalog& catalog_;
    std::shared_ptr<int> alive_ { std::make_shared<int>(0) };
    char search_[160] {};
    char deviceSearch_[96] {};
    std::string lastSearch_;
    bool favouritesOnly_ {};
    bool dirty_ { true };
    juce::String selectedPath_;
    bool autoPreview_ { true };
    bool showSamplesTab_ {};
    int currentTab_ {};
    std::unique_ptr<juce::FileChooser> chooser_;

    // What the catalog gave, and what of it is shown.
    std::shared_ptr<const SampleCatalog::Data> data_;
    SampleCatalog::Result result_;
    // The tree, and what of it filters the table.
    juce::String filterFamily_;
    juce::String filterCategory_;
    juce::String filterGroup_;
    juce::String filterSource_;
    bool showTree_ { true };
    float treeHeight_ { 190.0f };

    // The semantic search: on, what was asked, its answer by record.
    bool semanticMode_ {};
    bool semanticPending_ {};
    double editedAt_ {};
    juce::String semanticAsked_;
    juce::String semanticEnglish_;
    juce::String semanticSimilarTo_;
    juce::String semanticError_;
    std::vector<std::pair<juce::String, float>> semanticHits_;

    // The devices' list, made again when the track changes, not each frame.
    std::vector<app::NodeGraphComponent::DeviceCatalogueEntry> devices_;
    std::optional<TrackRef> devicesFor_;
    double devicesTime_ { -100.0 };
};

} // namespace cydaw::v2
