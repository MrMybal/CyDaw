#pragma once

#include <cydaw/graph/GraphDefinition.h>

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace cydaw::app {

class GraphManagerComponent final : public juce::Component,
                                    private juce::FileBrowserListener {
public:
    enum class AssetKind : std::uint8_t {
        factoryPreset,
        userPreset,
        reusableSubgraph,
        nodeParameterPreset
    };

    struct PreviewNode {
        std::uint64_t id {};
        juce::String label;
        juce::Point<float> position;
        juce::Colour colour { 0xff76808a };
    };

    struct PreviewConnection {
        std::uint64_t source {};
        std::uint64_t destination {};
        juce::Colour colour { 0xff67b9ff };
    };

    struct Asset {
        AssetKind kind { AssetKind::factoryPreset };
        juce::String key;
        juce::String name;
        juce::String category;
        juce::String scope;
        juce::String detail;
        juce::File file;
        juce::String role;
        juce::String version;
        juce::String inputs;
        juce::String outputs;
        bool valid { true };
        juce::String validationMessage;
        juce::StringArray tags;
        juce::String author;
        juce::String nodeTypeKey;
        juce::String processorKey;
        juce::Colour accentColour { 0x00000000 };
        std::vector<PreviewNode> previewNodes;
        std::vector<PreviewConnection> previewConnections;
    };

    GraphManagerComponent();
    ~GraphManagerComponent() override;

    void setAssets(std::vector<Asset> assets);
    void setFactoryPresetLoadHandler(
        std::function<void(const juce::String&)> handler);
    void setUserPresetLoadHandler(std::function<void(const juce::File&)> handler);
    void setNodeParameterPresetLoadHandler(
        std::function<void(const juce::File&)> handler);
    void setRefreshHandler(std::function<void()> handler);
    void setNodeParameterPresetTarget(const juce::String& nodeTypeKey,
                                      const juce::String& processorKey,
                                      bool editable);
    void setSubgraphOpenHandler(
        std::function<void(const juce::String&)> handler);
    void setSubgraphVariantHandler(
        std::function<void(const juce::String&)> handler);
    void refreshTranslations();
    void refreshFiles();
    [[nodiscard]] bool selectAssetByKey(const juce::String& key);
    [[nodiscard]] juce::String selectedAssetKey() const;
    [[nodiscard]] std::size_t selectedPreviewNodeCount() const noexcept;
    [[nodiscard]] std::size_t selectedPreviewConnectionCount() const noexcept;
    void showFileBrowser();

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    class CategoryItem;
    class AssetItem;
    class TopologyPreview;

    void showCategoryView();
    void showFileView();
    void rebuildCategoryTree();
    void rebuildFileView();
    void selectAsset(const Asset& asset);
    void openAsset(const Asset& asset);
    void openFileAsset(const juce::File& file);
    [[nodiscard]] bool canOpenAsset(const Asset& asset) const;
    [[nodiscard]] bool isActiveAsset(const Asset& asset) const;
    [[nodiscard]] juce::String assetIdentity(const Asset& asset) const;
    [[nodiscard]] bool isFavorite(const Asset& asset) const;
    void toggleFavorite(const Asset& asset);
    void revealAsset(const Asset& asset);
    void editAssetMetadata(const Asset& asset);
    void duplicateUserGraph(const Asset& asset);
    void touchRecent(const Asset& asset);
    void saveCollections();
    [[nodiscard]] juce::String roleLabel(
        cydaw::graph::GraphRole role) const;

    void selectionChanged() override;
    void fileClicked(const juce::File& file,
                     const juce::MouseEvent& event) override;
    void fileDoubleClicked(const juce::File& file) override;
    void browserRootChanged(const juce::File& newRoot) override;

    std::vector<Asset> assets_;
    std::function<void(const juce::String&)> factoryPresetLoadHandler_;
    std::function<void(const juce::File&)> userPresetLoadHandler_;
    std::function<void(const juce::File&)>
        nodeParameterPresetLoadHandler_;
    std::function<void()> refreshHandler_;
    std::function<void(const juce::String&)> subgraphOpenHandler_;
    std::function<void(const juce::String&)> subgraphVariantHandler_;

    juce::TextButton categoryViewButton_;
    juce::TextButton fileViewButton_;
    juce::TextButton refreshButton_;
    juce::TextButton openButton_;
    juce::TextButton favoriteButton_;
    juce::TextButton editMetadataButton_;
    juce::TextButton revealButton_;
    juce::TextButton variantButton_;
    juce::ComboBox groupingSelector_;
    juce::TextEditor search_;
    juce::TreeView categoryTree_;
    std::unique_ptr<CategoryItem> categoryRoot_;
    juce::Label detailTitle_;
    juce::Label detailText_;
    std::unique_ptr<TopologyPreview> topologyPreview_;
    juce::TimeSliceThread fileThread_ { "CyDAW Graph Manager" };
    std::unique_ptr<juce::WildcardFileFilter> fileFilter_;
    std::unique_ptr<juce::DirectoryContentsList> fileList_;
    std::unique_ptr<juce::FileTreeComponent> fileTree_;
    bool showingFiles_ {};
    std::optional<Asset> selectedAsset_;
    std::unique_ptr<juce::PropertiesFile> preferences_;
    juce::StringArray favoriteAssetIds_;
    juce::StringArray recentAssetIds_;
    juce::String targetNodeTypeKey_;
    juce::String targetProcessorKey_;
    bool targetNodeEditable_ {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GraphManagerComponent)
};

} // namespace cydaw::app
