#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <vector>

namespace cydaw::app {

class LibraryBrowserComponent final : public juce::Component,
                                      public juce::DragAndDropContainer,
                                      private juce::ListBoxModel,
                                      private juce::Thread,
                                      private juce::Timer {
public:
    enum class SampleSortMode { name, newest, duration, size };
    enum class SampleFilterMode {
        all, mono, stereo, shortDuration, mediumDuration, longDuration
    };

    struct SampleMetadata {
        juce::File file;
        juce::File libraryRoot;
        juce::String name;
        juce::String extension;
        double durationSeconds {};
        double sampleRate {};
        int channels {};
        juce::int64 fileSize {};
        juce::int64 modifiedTimeMs {};
    };

    LibraryBrowserComponent();
    ~LibraryBrowserComponent() override;

    [[nodiscard]] static std::vector<SampleMetadata> scanSampleLibraries(
        const std::vector<juce::File>& roots,
        std::size_t maximumItems = 50'000,
        std::function<bool()> cancellationRequested = {});
    [[nodiscard]] static bool sampleMatchesSearch(
        const SampleMetadata& sample,
        const juce::String& query);
    [[nodiscard]] static juce::String formatSampleMetadata(
        const SampleMetadata& sample);
    [[nodiscard]] static bool sampleSortsBefore(
        const SampleMetadata& left, const SampleMetadata& right,
        SampleSortMode mode);
    [[nodiscard]] static bool sampleMatchesFilter(
        const SampleMetadata& sample, SampleFilterMode mode);
    [[nodiscard]] static juce::var serialiseSampleIndexCache(
        const std::vector<SampleMetadata>& samples,
        const std::vector<juce::File>& roots);
    [[nodiscard]] static std::vector<SampleMetadata>
    restoreSampleIndexCache(const juce::var& cache,
                            const std::vector<juce::File>& expectedRoots);
    [[nodiscard]] static bool favoriteListContains(
        const juce::StringArray& favoritePaths,
        const juce::File& file);
    static bool updateFavoriteList(juce::StringArray& favoritePaths,
                                   const juce::File& file,
                                   bool shouldBeFavorite);

    void refreshTranslations();
    void showLibraryMenu();
    void setAudioOpenHandler(std::function<void(const juce::File&)> handler);
    void setPluginInsertHandler(std::function<void(const juce::File&)> handler);
    void setFactoryDeviceInsertHandler(
        std::function<void(const juce::String&)> handler);
    void setGraphPresetFileLoadHandler(
        std::function<void(const juce::File&)> handler);
    void setSamplePreviewHandlers(
        std::function<juce::Result(const juce::File&, double)> startHandler,
        std::function<void()> stopHandler,
        std::function<bool()> isPlayingHandler,
        std::function<double()> positionHandler,
        std::function<void(float)> gainHandler);
    void focusForDeviceInsertion();
    void refreshUserGraphPresets();
    struct FactoryGraphPreset {
        juce::String key;
        juce::String translationKey;
        bool soundSource {};
    };
    void setFactoryGraphPresets(std::vector<FactoryGraphPreset> presets);
    void selectQuickCategory(int categoryId);
    void focusForSampleInsertion(
        std::function<void(const juce::File&)> handler,
        juce::String targetLabel = {});

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;

private:
    enum class ItemType {
        sample, instrument, effect, plugin, graphPreset, userGraphPreset
    };
    struct Item {
        ItemType type { ItemType::sample };
        juce::String name;
        juce::String detail;
        juce::File file;
        juce::String translationKey;
        juce::String deviceKey;
        juce::File libraryRoot;
        bool favorite {};
        SampleMetadata sample;
        bool soundSource {};
    };

    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& graphics, int width, int height, bool selected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;
    void selectedRowsChanged(int lastRowSelected) override;
    juce::var getDragSourceDescription(
        const juce::SparseSet<int>& selectedRows) override;
    void run() override;
    void timerCallback() override;
    void chooseSampleFolder();
    void choosePluginFile();
    void scanFolder(const juce::File& folder);
    void removeSelectedLibrary();
    void rescanLibraries();
    void probePlugin(const juce::File& pluginFile, bool validateProcessing);
    void refreshBridgeStatus();
    void refreshLocationSelector();
    void refreshSelectionActions();
    void activateItem(const Item& item);
    void toggleSelectedFavorite();
    void toggleSamplePreview();
    void startSamplePreviewAt(double startSeconds);
    void stopSamplePreview();
    void refreshWaveformPreview();
    void refreshQuickCategoryButtons();
    [[nodiscard]] juce::Rectangle<int> waveformContentBounds() const noexcept;
    void loadLibrarySettings();
    void saveLibrarySettings();
    void loadSampleIndexCache();
    void saveSampleIndexCache(
        const std::vector<SampleMetadata>& samples,
        const std::vector<juce::File>& roots) const;
    void applyFilter();
    [[nodiscard]] bool categoryMatches(const Item& item) const;
    [[nodiscard]] bool sampleFilterMatches(const Item& item) const;
    [[nodiscard]] bool locationMatches(const Item& item) const;
    [[nodiscard]] Item* selectedItem() noexcept;
    [[nodiscard]] const Item* selectedItem() const noexcept;
    [[nodiscard]] bool isFavorite(const juce::File& file) const;
    [[nodiscard]] static juce::String normalisedPath(const juce::File& file);

    juce::Label titleLabel_;
    juce::TextEditor searchEditor_;
    juce::ComboBox categorySelector_;
    juce::TextButton allQuickButton_;
    juce::TextButton samplesQuickButton_;
    juce::TextButton instrumentsQuickButton_;
    juce::TextButton effectsQuickButton_;
    juce::TextButton graphsQuickButton_;
    juce::ComboBox locationSelector_;
    juce::ComboBox sampleFilterSelector_;
    juce::ComboBox sortSelector_;
    juce::TextButton addFolderButton_;
    juce::TextButton removeFolderButton_;
    juce::TextButton addPluginButton_;
    juce::TextButton rescanButton_;
    // Stands in for the four buttons above when the panel is too narrow to
    // show them without clipping their labels.
    juce::TextButton libraryMenuButton_;
    juce::TextButton favoriteButton_;
    juce::TextButton previewButton_;
    juce::ToggleButton autoPreviewButton_;
    juce::Slider previewGainSlider_;
    juce::Label scanStatusLabel_;
    juce::Label bridgeStatusLabel_;
    juce::ListBox list_ { "CyDAW Library", this };
    juce::AudioFormatManager waveformFormatManager_;
    juce::AudioThumbnailCache waveformThumbnailCache_ { 4 };
    juce::AudioThumbnail waveformThumbnail_;
    juce::Rectangle<int> waveformPreviewBounds_;
    juce::Rectangle<int> facetPanelBounds_;
    juce::Rectangle<int> detailPanelBounds_;
    juce::File waveformPreviewFile_;
    bool wideLayout_ {};
    std::vector<Item> items_;
    std::vector<int> filteredIndices_;
    std::vector<juce::File> libraryRoots_;
    std::vector<juce::File> scanRoots_;
    juce::StringArray favoritePaths_;
    juce::File pluginProbeFile_;
    bool validatePluginProcessing_ {};
    int sortMode_ { 1 };
    int sampleFilterMode_ { 1 };
    juce::CriticalSection scanLock_;
    std::unique_ptr<juce::FileChooser> chooser_;
    std::function<void(const juce::File&)> audioOpenHandler_;
    std::function<void(const juce::File&)> pluginInsertHandler_;
    std::function<void(const juce::String&)> factoryDeviceInsertHandler_;
    std::function<void(const juce::File&)> graphPresetFileLoadHandler_;
    std::function<juce::Result(const juce::File&, double)>
        samplePreviewStartHandler_;
    std::function<void()> samplePreviewStopHandler_;
    std::function<bool()> samplePreviewIsPlayingHandler_;
    std::function<double()> samplePreviewPositionHandler_;
    std::function<void(float)> samplePreviewGainHandler_;
    std::function<void(const juce::File&)> targetedSampleInsertHandler_;
    juce::File previewFile_;
    juce::ApplicationProperties properties_;
    juce::File sampleIndexCacheFile_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibraryBrowserComponent)
};

} // namespace cydaw::app
