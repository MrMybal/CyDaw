#include <cydaw/base/File.h>
#include <cydaw/base/Properties.h>
#include <cydaw/base/Text.h>
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
        cydaw::base::Text label;
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
        cydaw::base::Text key;
        cydaw::base::Text name;
        cydaw::base::Text category;
        cydaw::base::Text scope;
        cydaw::base::Text detail;
        cydaw::base::File file;
        cydaw::base::Text role;
        cydaw::base::Text version;
        cydaw::base::Text inputs;
        cydaw::base::Text outputs;
        bool valid { true };
        cydaw::base::Text validationMessage;
        cydaw::base::TextList tags;
        cydaw::base::Text author;
        cydaw::base::Text nodeTypeKey;
        cydaw::base::Text processorKey;
        juce::Colour accentColour { 0x00000000 };
        std::vector<PreviewNode> previewNodes;
        std::vector<PreviewConnection> previewConnections;
    };

    GraphManagerComponent();
    ~GraphManagerComponent() override;

    void setAssets(std::vector<Asset> assets);
    void setFactoryPresetLoadHandler(
        std::function<void(const cydaw::base::Text&)> handler);
    void setUserPresetLoadHandler(std::function<void(const cydaw::base::File&)> handler);
    void setNodeParameterPresetLoadHandler(
        std::function<void(const cydaw::base::File&)> handler);
    void setRefreshHandler(std::function<void()> handler);
    void setNodeParameterPresetTarget(const cydaw::base::Text& nodeTypeKey,
                                      const cydaw::base::Text& processorKey,
                                      bool editable);
    void setSubgraphOpenHandler(
        std::function<void(const cydaw::base::Text&)> handler);
    void setSubgraphVariantHandler(
        std::function<void(const cydaw::base::Text&)> handler);
    void refreshTranslations();
    void refreshFiles();
    [[nodiscard]] bool selectAssetByKey(const cydaw::base::Text& key);
    [[nodiscard]] cydaw::base::Text selectedAssetKey() const;
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
    void openFileAsset(const cydaw::base::File& file);
    [[nodiscard]] bool canOpenAsset(const Asset& asset) const;
    [[nodiscard]] bool isActiveAsset(const Asset& asset) const;
    [[nodiscard]] cydaw::base::Text assetIdentity(const Asset& asset) const;
    [[nodiscard]] bool isFavorite(const Asset& asset) const;
    void toggleFavorite(const Asset& asset);
    void revealAsset(const Asset& asset);
    void editAssetMetadata(const Asset& asset);
    void duplicateUserGraph(const Asset& asset);
    void touchRecent(const Asset& asset);
    void saveCollections();
    [[nodiscard]] cydaw::base::Text roleLabel(
        cydaw::graph::GraphRole role) const;

    void selectionChanged() override;
    void fileClicked(const juce::File& file,
                     const juce::MouseEvent& event) override;
    void fileDoubleClicked(const juce::File& file) override;
    void browserRootChanged(const juce::File& newRoot) override;

    std::vector<Asset> assets_;
    std::function<void(const cydaw::base::Text&)> factoryPresetLoadHandler_;
    std::function<void(const cydaw::base::File&)> userPresetLoadHandler_;
    std::function<void(const cydaw::base::File&)>
        nodeParameterPresetLoadHandler_;
    std::function<void()> refreshHandler_;
    std::function<void(const cydaw::base::Text&)> subgraphOpenHandler_;
    std::function<void(const cydaw::base::Text&)> subgraphVariantHandler_;

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
    std::unique_ptr<cydaw::base::PropertiesFile> preferences_;
    cydaw::base::TextList favoriteAssetIds_;
    cydaw::base::TextList recentAssetIds_;
    cydaw::base::Text targetNodeTypeKey_;
    cydaw::base::Text targetProcessorKey_;
    bool targetNodeEditable_ {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GraphManagerComponent)
};

} // namespace cydaw::app
