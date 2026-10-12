#pragma once

#include "SampleLibrary.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace cydaw::app {

// The sample explorer: the library's folders as chips, a search, a table
// with a filter behind every column, and under it the selected sample's
// waveform, its transport, and the fields a person edits. A row drags onto
// a track lane or a launcher slot; a double-click opens it on a new track.
class SampleExplorerComponent final : public juce::Component,
                                      public juce::DragAndDropContainer,
                                      private juce::TableListBoxModel,
                                      private SampleLibrary::Listener,
                                      private juce::Timer {
public:
    explicit SampleExplorerComponent(SampleLibrary& library);
    ~SampleExplorerComponent() override;

    void setPreviewHandlers(
        std::function<juce::Result(const juce::File&, double)> startHandler,
        std::function<void()> stopHandler,
        std::function<bool()> isPlayingHandler,
        std::function<double()> positionHandler,
        std::function<void(float)> gainHandler);
    void setOpenHandlers(std::function<void(const juce::File&)> newTrackHandler,
                         std::function<void(const juce::File&)> insertHandler);
    void refreshTranslations();
    // The row count after filters, and the selected path, for the suite.
    [[nodiscard]] int visibleRowCount() const noexcept { return static_cast<int>(rows_.size()); }
    [[nodiscard]] juce::String selectedPath() const;
    void setSearchText(const juce::String& text);
    void setColumnFilter(int columnId, const juce::StringArray& values);
    // Selects a sample by path, as a click on its row does.
    void selectPath(const juce::String& path);

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

    enum Column {
        favouriteColumn = 1, waveColumn, nameColumn, categoryColumn, typeColumn,
        tagsColumn, lengthColumn, bpmColumn, keyColumn, channelsColumn, rateColumn,
        folderColumn, modifiedColumn
    };

private:
    class FilterHeader;
    class WaveformView;

    // TableListBoxModel
    int getNumRows() override;
    void paintRowBackground(juce::Graphics&, int row, int width, int height,
                            bool selected) override;
    void paintCell(juce::Graphics&, int row, int columnId, int width, int height,
                   bool selected) override;
    void cellClicked(int row, int columnId, const juce::MouseEvent& event) override;
    void cellDoubleClicked(int row, int columnId, const juce::MouseEvent& event) override;
    void selectedRowsChanged(int lastRowSelected) override;
    void sortOrderChanged(int newSortColumnId, bool isForwards) override;
    juce::var getDragSourceDescription(const juce::SparseSet<int>& rows) override;
    juce::String getCellTooltip(int row, int columnId) override;

    // SampleLibrary::Listener
    void sampleLibraryChanged(bool structure) override;
    void sampleLibraryProgress() override;
    void timerCallback() override;

    [[nodiscard]] const SampleRecord* recordAt(int row) const;
    [[nodiscard]] juce::String bucketFor(int columnId, const SampleRecord& record) const;
    [[nodiscard]] bool passesFilters(const SampleRecord& record) const;
    void rebuildRows();
    void showFilterMenu(int columnId);
    void clearFilters();
    void refreshFolderChips();
    void refreshFilterSummary();
    void refreshDetails();
    void refreshStatus();
    void startPreview(double startSeconds = 0.0);
    void stopPreview();
    void chooseFolder();
    void chooseCacheFolder();
    void showFolderMenu(const juce::File& folder, juce::Component* target);
    void commitCategory();
    void commitType();
    void commitTags();
    void commitBpm();
    void setRating(int rating);
    void openSelected(bool newTrack);

    SampleLibrary& library_;
    std::vector<std::size_t> rows_;
    std::map<int, std::set<juce::String>> filters_;
    std::set<juce::String> folderFilter_;
    juce::String search_;
    int sortColumn_ { nameColumn };
    bool sortForwards_ { true };
    juce::String selectedPath_;
    juce::StringArray menuValues_;

    juce::Label titleLabel_;
    juce::TextEditor searchEditor_;
    juce::TextButton addFolderButton_;
    juce::TextButton rescanButton_;
    juce::ToggleButton autoRescanButton_;
    juce::ComboBox rescanIntervalBox_;
    juce::ToggleButton classifierButton_;
    juce::TextButton classifyNowButton_;
    juce::TextButton cacheButton_;
    juce::Label statusLabel_;
    std::vector<std::unique_ptr<juce::TextButton>> folderChips_;
    juce::Label filterSummaryLabel_;
    juce::TextButton clearFiltersButton_;
    juce::TableListBox table_;

    std::unique_ptr<WaveformView> waveform_;
    juce::TextButton playButton_;
    juce::TextButton stopButton_;
    juce::ToggleButton autoPreviewButton_;
    juce::Slider previewGainSlider_;
    juce::Label detailsLabel_;
    juce::Label categoryCaption_;
    juce::ComboBox categoryBox_;
    juce::Label typeCaption_;
    juce::ComboBox typeBox_;
    juce::Label tagsCaption_;
    juce::TextEditor tagsEditor_;
    juce::Label bpmCaption_;
    juce::TextEditor bpmEditor_;
    std::array<juce::TextButton, 5> ratingButtons_;
    juce::TextButton favouriteButton_;
    juce::TextButton newTrackButton_;
    juce::TextButton insertButton_;

    std::function<juce::Result(const juce::File&, double)> startPreview_;
    std::function<void()> stopPreview_;
    std::function<bool()> isPreviewing_;
    std::function<double()> previewPosition_;
    std::function<void(float)> previewGain_;
    std::function<void(const juce::File&)> openNewTrack_;
    std::function<void(const juce::File&)> insertAtPlayhead_;
    bool refreshingDetails_ {};
};

} // namespace cydaw::app
