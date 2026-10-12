#include "GraphManagerComponent.h"

#include "GraphPresetFile.h"
#include "Localization.h"
#include "NodeParameterPresetFile.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>

namespace cydaw::app {

namespace {
juce::String assetKindKey(const GraphManagerComponent::AssetKind kind)
{
    if (kind == GraphManagerComponent::AssetKind::userPreset)
        return "graph.manager.userPreset";
    if (kind == GraphManagerComponent::AssetKind::reusableSubgraph)
        return "graph.manager.subgraph";
    if (kind == GraphManagerComponent::AssetKind::nodeParameterPreset)
        return "graph.manager.nodePreset";
    return "graph.manager.factoryPreset";
}

class GraphManagerWindowItem : public juce::TreeViewItem {
public:
    explicit GraphManagerWindowItem(juce::String label)
        : label_(std::move(label)) {}

    bool mightContainSubItems() override { return true; }
    juce::String getUniqueName() const override { return label_; }

    void paintItem(juce::Graphics& graphics, const int width,
                   const int height) override
    {
        graphics.setColour(isSelected() ? juce::Colour(0xffffa13b)
                                        : juce::Colour(0xffd6dadd));
        graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        graphics.drawFittedText(label_, 3, 0, width - 5, height,
                                juce::Justification::centredLeft, 1, 0.75f);
    }

private:
    juce::String label_;
};
}

class GraphManagerComponent::CategoryItem final : public GraphManagerWindowItem {
public:
    using GraphManagerWindowItem::GraphManagerWindowItem;
};

class GraphManagerComponent::AssetItem final : public juce::TreeViewItem {
public:
    AssetItem(GraphManagerComponent& owner, Asset asset)
        : owner_(owner), asset_(std::move(asset)) {}

    bool mightContainSubItems() override { return false; }
    juce::String getUniqueName() const override
    {
        return juce::String(static_cast<int>(asset_.kind)) + ":" + asset_.key
               + ":" + asset_.file.getFullPathName();
    }

    [[nodiscard]] bool represents(const Asset& candidate) const
    {
        return owner_.assetIdentity(asset_)
               == owner_.assetIdentity(candidate);
    }

    juce::String getTooltip() override
    {
        auto tooltip = asset_.name + "\n" + tr(assetKindKey(asset_.kind));
        if (asset_.category.isNotEmpty())
            tooltip += "\n" + tr("graph.manager.category") + ": "
                       + asset_.category;
        if (asset_.role.isNotEmpty())
            tooltip += "\n" + tr("graph.manager.role") + ": "
                       + asset_.role;
        if (asset_.version.isNotEmpty())
            tooltip += "\n" + tr("graph.manager.version") + ": "
                       + asset_.version;
        if (!asset_.tags.isEmpty())
            tooltip += "\n" + tr("graph.manager.tags") + ": "
                       + asset_.tags.joinIntoString(", ");
        if (asset_.author.isNotEmpty())
            tooltip += "\n" + tr("graph.manager.author") + ": "
                       + asset_.author;
        if (asset_.inputs.isNotEmpty())
            tooltip += "\n\n" + tr("graph.manager.inputs") + "\n"
                       + asset_.inputs;
        if (asset_.outputs.isNotEmpty())
            tooltip += "\n\n" + tr("graph.manager.outputs") + "\n"
                       + asset_.outputs;
        if (asset_.detail.isNotEmpty())
            tooltip += "\n\n" + asset_.detail;
        if (!owner_.canOpenAsset(asset_))
            tooltip += "\n\n" + tr("graph.manager.assetUnavailable");
        return tooltip;
    }

    void paintItem(juce::Graphics& graphics, const int width,
                   const int height) override
    {
        const auto active = owner_.isActiveAsset(asset_);
        if (active) {
            graphics.setColour(juce::Colour(0xff343a40));
            graphics.fillRoundedRectangle(1.0f, 1.0f,
                                          static_cast<float>(width - 2),
                                          static_cast<float>(height - 2),
                                          2.0f);
            graphics.setColour(juce::Colour(0xffff8a00));
            graphics.fillRect(0, 1, 2, height - 2);
        }
        const auto kindColour = asset_.kind == AssetKind::factoryPreset
                                    ? juce::Colour(0xff67b9ff)
                                : asset_.kind == AssetKind::userPreset
                                    ? juce::Colour(0xffffa13b)
                                : asset_.kind == AssetKind::nodeParameterPreset
                                    ? juce::Colour(0xff62d6b0)
                                    : juce::Colour(0xffb58cff);
        const auto colour = asset_.accentColour.getAlpha() != 0
                                ? asset_.accentColour : kindColour;
        graphics.setColour(colour);
        graphics.fillEllipse(4.0f, static_cast<float>(height / 2 - 4),
                             8.0f, 8.0f);
        graphics.setColour(active ? juce::Colour(0xffffffff)
                                  : juce::Colour(0xffcbd0d4));
        graphics.setFont(juce::FontOptions(9.5f));
        const auto favorite = owner_.isFavorite(asset_);
        auto roleBadge = juce::String("GRF");
        if (asset_.role == tr("graph.role.instrument"))
            roleBadge = "INST";
        else if (asset_.role == tr("graph.role.audioEffect"))
            roleBadge = "FX";
        else if (asset_.role == tr("graph.role.midiEffect"))
            roleBadge = "MIDI";
        else if (asset_.role == tr("graph.role.modulator"))
            roleBadge = "MOD";
        else if (asset_.role == tr("graph.role.analyzer"))
            roleBadge = "AN";
        else if (asset_.role == tr("graph.role.utility"))
            roleBadge = "UTIL";
        const auto badgeWidth = roleBadge.length() > 3 ? 32 : 27;
        const auto badgeRight = width - (favorite ? 22 : 4);
        const auto badgeX = badgeRight - badgeWidth;
        graphics.drawFittedText(asset_.name, 17, 0,
                                std::max(10, badgeX - 21), height,
                                juce::Justification::centredLeft, 1, 0.72f);
        graphics.setColour(colour.withAlpha(active ? 0.30f : 0.18f));
        graphics.fillRoundedRectangle(static_cast<float>(badgeX),
                                      static_cast<float>(height / 2 - 7),
                                      static_cast<float>(badgeWidth), 14.0f, 3.0f);
        graphics.setColour(colour.brighter(active ? 0.40f : 0.18f));
        graphics.drawRoundedRectangle(static_cast<float>(badgeX),
                                      static_cast<float>(height / 2 - 7),
                                      static_cast<float>(badgeWidth), 14.0f,
                                      3.0f, 0.75f);
        graphics.setFont(juce::FontOptions(7.5f, juce::Font::bold));
        graphics.drawFittedText(roleBadge, badgeX + 2, height / 2 - 7,
                                badgeWidth - 4, 14,
                                juce::Justification::centred, 1, 0.90f);
        if (favorite) {
            graphics.setColour(juce::Colour(0xffffb347));
            graphics.setFont(juce::FontOptions(11.0f));
            graphics.drawText(juce::String::charToString(0x2605),
                              width - 18, 0, 16, height,
                              juce::Justification::centred);
        }
    }

    void itemSelectionChanged(const bool isNowSelected) override
    {
        if (isNowSelected)
            owner_.selectAsset(asset_);
    }

    void itemDoubleClicked(const juce::MouseEvent&) override
    {
        owner_.openAsset(asset_);
    }

    void itemClicked(const juce::MouseEvent& event) override
    {
        if (!event.mods.isPopupMenu())
            return;
        juce::PopupMenu menu;
        menu.addItem(1, tr("graph.manager.open"));
        menu.addSeparator();
        menu.addItem(2, owner_.isFavorite(asset_)
                            ? tr("graph.manager.unfavorite")
                            : tr("graph.manager.favorite"));
        if (asset_.file.existsAsFile())
            menu.addItem(3, tr("graph.manager.reveal"));
        if ((asset_.kind == AssetKind::nodeParameterPreset
             || asset_.kind == AssetKind::userPreset)
            && asset_.file.existsAsFile())
            menu.addItem(4, tr("graph.manager.editMetadata"));
        if (asset_.kind == AssetKind::userPreset
            && asset_.file.existsAsFile())
            menu.addItem(5, tr("graph.manager.duplicateGraph"));
        if (asset_.kind == AssetKind::reusableSubgraph)
            menu.addItem(6, tr("graph.manager.createVariant"));
        juce::Component::SafePointer<GraphManagerComponent> safeOwner(&owner_);
        const auto asset = asset_;
        menu.showMenuAsync({}, [safeOwner, asset](const int result) {
            if (safeOwner == nullptr)
                return;
            if (result == 1)
                safeOwner->openAsset(asset);
            else if (result == 2)
                safeOwner->toggleFavorite(asset);
            else if (result == 3)
                safeOwner->revealAsset(asset);
            else if (result == 4)
                safeOwner->editAssetMetadata(asset);
            else if (result == 5)
                safeOwner->duplicateUserGraph(asset);
            else if (result == 6
                     && safeOwner->subgraphVariantHandler_)
                safeOwner->subgraphVariantHandler_(asset.key);
        });
    }

private:
    GraphManagerComponent& owner_;
    Asset asset_;
};

class GraphManagerComponent::TopologyPreview final
    : public juce::Component,
      public juce::SettableTooltipClient {
public:
    TopologyPreview()
    {
        setInterceptsMouseClicks(false, false);
        setTooltip(tr("graph.manager.previewHelp"));
    }

    void setAsset(const std::optional<Asset>& asset)
    {
        asset_ = asset;
        setVisible(asset_.has_value() && !asset_->previewNodes.empty());
        repaint();
    }

    void refreshTranslations()
    {
        setTooltip(tr("graph.manager.previewHelp"));
        repaint();
    }

    void paint(juce::Graphics& graphics) override
    {
        auto bounds = getLocalBounds().toFloat();
        graphics.setColour(juce::Colour(0xff101316));
        graphics.fillRoundedRectangle(bounds, 4.0f);
        graphics.setColour(juce::Colour(0xff363d43));
        graphics.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);
        if (!asset_.has_value() || asset_->previewNodes.empty())
            return;

        auto header = bounds.reduced(8.0f).removeFromTop(18.0f);
        graphics.setColour(juce::Colour(0xffffa13b));
        graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        graphics.drawText(tr("graph.manager.preview"), header.toNearestInt(),
                          juce::Justification::centredLeft, false);
        graphics.setColour(juce::Colour(0xff7f8992));
        graphics.setFont(juce::FontOptions(8.5f));
        const auto summary = juce::String(
            static_cast<int>(asset_->previewNodes.size())) + " "
            + tr("graph.manager.previewNodes") + "  /  "
            + juce::String(static_cast<int>(
                asset_->previewConnections.size())) + " "
            + tr("graph.manager.previewConnections");
        graphics.drawText(summary, header.toNearestInt(),
                          juce::Justification::centredRight, false);

        auto canvas = bounds.reduced(9.0f);
        canvas.removeFromTop(21.0f);
        if (canvas.getWidth() < 40.0f || canvas.getHeight() < 30.0f)
            return;

        graphics.saveState();
        graphics.reduceClipRegion(canvas.toNearestInt());
        graphics.setColour(juce::Colour(0xff1b2025));
        for (auto x = canvas.getX(); x < canvas.getRight(); x += 18.0f)
            graphics.drawVerticalLine(juce::roundToInt(x), canvas.getY(),
                                      canvas.getBottom());
        for (auto y = canvas.getY(); y < canvas.getBottom(); y += 18.0f)
            graphics.drawHorizontalLine(juce::roundToInt(y), canvas.getX(),
                                        canvas.getRight());

        auto minimumX = asset_->previewNodes.front().position.x;
        auto maximumX = minimumX;
        auto minimumY = asset_->previewNodes.front().position.y;
        auto maximumY = minimumY;
        for (const auto& node : asset_->previewNodes) {
            minimumX = juce::jmin(minimumX, node.position.x);
            maximumX = juce::jmax(maximumX, node.position.x);
            minimumY = juce::jmin(minimumY, node.position.y);
            maximumY = juce::jmax(maximumY, node.position.y);
        }
        const auto spanX = juce::jmax(0.08f, maximumX - minimumX);
        const auto spanY = juce::jmax(0.08f, maximumY - minimumY);
        const auto manyNodes = asset_->previewNodes.size() > 10;
        const auto nodeWidth = manyNodes ? 34.0f : 43.0f;
        const auto nodeHeight = manyNodes ? 14.0f : 17.0f;
        const auto placement = canvas.reduced(nodeWidth * 0.55f,
                                              nodeHeight * 0.70f);
        std::map<std::uint64_t, juce::Rectangle<float>> nodeBounds;
        for (const auto& node : asset_->previewNodes) {
            const auto normalX = (node.position.x - minimumX) / spanX;
            const auto normalY = spanY <= 0.081f
                ? 0.5f : (node.position.y - minimumY) / spanY;
            const auto centre = juce::Point<float> {
                placement.getX() + normalX * placement.getWidth(),
                placement.getY() + normalY * placement.getHeight()
            };
            nodeBounds[node.id] = {
                centre.x - nodeWidth * 0.5f,
                centre.y - nodeHeight * 0.5f,
                nodeWidth, nodeHeight
            };
        }

        for (const auto& connection : asset_->previewConnections) {
            const auto source = nodeBounds.find(connection.source);
            const auto destination = nodeBounds.find(connection.destination);
            if (source == nodeBounds.end() || destination == nodeBounds.end())
                continue;
            const auto start = juce::Point<float> {
                source->second.getRight(), source->second.getCentreY()
            };
            const auto end = juce::Point<float> {
                destination->second.getX(), destination->second.getCentreY()
            };
            const auto curve = juce::jmax(12.0f,
                std::abs(end.x - start.x) * 0.42f);
            juce::Path path;
            path.startNewSubPath(start);
            path.cubicTo(start.x + curve, start.y,
                         end.x - curve, end.y, end.x, end.y);
            graphics.setColour(connection.colour.withAlpha(0.78f));
            graphics.strokePath(path, juce::PathStrokeType(1.35f));
        }

        for (const auto& node : asset_->previewNodes) {
            const auto nodeArea = nodeBounds[node.id];
            graphics.setColour(juce::Colour(0xff252b30));
            graphics.fillRoundedRectangle(nodeArea, 2.5f);
            graphics.setColour(node.colour.withAlpha(0.95f));
            graphics.drawRoundedRectangle(nodeArea, 2.5f, 1.2f);
            graphics.fillEllipse(nodeArea.getX() - 2.2f,
                                 nodeArea.getCentreY() - 2.2f,
                                 4.4f, 4.4f);
            graphics.fillEllipse(nodeArea.getRight() - 2.2f,
                                 nodeArea.getCentreY() - 2.2f,
                                 4.4f, 4.4f);
            graphics.setColour(juce::Colour(0xffe3e7ea));
            graphics.setFont(juce::FontOptions(manyNodes ? 6.5f : 7.5f,
                                               juce::Font::bold));
            graphics.drawFittedText(node.label,
                                    nodeArea.reduced(3.0f, 1.0f).toNearestInt(),
                                    juce::Justification::centred, 1, 0.55f);
        }
        graphics.restoreState();
    }

private:
    std::optional<Asset> asset_;
};

GraphManagerComponent::GraphManagerComponent()
{
    setOpaque(true);
    addAndMakeVisible(categoryViewButton_);
    addAndMakeVisible(fileViewButton_);
    addAndMakeVisible(refreshButton_);
    addAndMakeVisible(openButton_);
    addAndMakeVisible(favoriteButton_);
    addAndMakeVisible(editMetadataButton_);
    addAndMakeVisible(revealButton_);
    addAndMakeVisible(variantButton_);
    addAndMakeVisible(groupingSelector_);
    addAndMakeVisible(search_);
    addAndMakeVisible(categoryTree_);
    addAndMakeVisible(detailTitle_);
    addAndMakeVisible(detailText_);
    topologyPreview_ = std::make_unique<TopologyPreview>();
    addAndMakeVisible(*topologyPreview_);
    topologyPreview_->setVisible(false);

    categoryViewButton_.setClickingTogglesState(false);
    fileViewButton_.setClickingTogglesState(false);
    categoryViewButton_.onClick = [this] { showCategoryView(); };
    fileViewButton_.onClick = [this] { showFileView(); };
    refreshButton_.onClick = [this] {
        if (refreshHandler_)
            refreshHandler_();
        else {
            refreshFiles();
            rebuildCategoryTree();
        }
    };
    openButton_.onClick = [this] {
        if (selectedAsset_.has_value())
            openAsset(*selectedAsset_);
    };
    favoriteButton_.onClick = [this] {
        if (selectedAsset_.has_value())
            toggleFavorite(*selectedAsset_);
    };
    editMetadataButton_.onClick = [this] {
        if (selectedAsset_.has_value())
            editAssetMetadata(*selectedAsset_);
    };
    revealButton_.onClick = [this] {
        if (selectedAsset_.has_value())
            revealAsset(*selectedAsset_);
    };
    variantButton_.onClick = [this] {
        if (selectedAsset_.has_value()
            && selectedAsset_->kind == AssetKind::reusableSubgraph
            && subgraphVariantHandler_)
            subgraphVariantHandler_(selectedAsset_->key);
    };
    openButton_.setEnabled(false);
    favoriteButton_.setEnabled(false);
    editMetadataButton_.setEnabled(false);
    revealButton_.setEnabled(false);
    variantButton_.setEnabled(false);
    groupingSelector_.onChange = [this] { rebuildCategoryTree(); };
    search_.setColour(juce::TextEditor::backgroundColourId,
                      juce::Colour(0xff111315));
    search_.setColour(juce::TextEditor::outlineColourId,
                      juce::Colour(0xff444a50));
    search_.setColour(juce::TextEditor::focusedOutlineColourId,
                      juce::Colour(0xffff8a00));
    search_.onTextChange = [this] {
        rebuildCategoryTree();
        if (showingFiles_)
            rebuildFileView();
    };
    categoryTree_.setRootItemVisible(false);
    categoryTree_.setDefaultOpenness(true);
    categoryTree_.setColour(juce::TreeView::backgroundColourId,
                            juce::Colour(0xff17191c));
    detailTitle_.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    detailTitle_.setColour(juce::Label::textColourId,
                           juce::Colour(0xffffa13b));
    detailText_.setFont(juce::FontOptions(10.0f));
    detailText_.setColour(juce::Label::textColourId,
                          juce::Colour(0xffbbc1c6));
    detailText_.setJustificationType(juce::Justification::topLeft);

    juce::PropertiesFile::Options options;
    options.applicationName = "CyDAW";
    options.filenameSuffix = "graph-manager.settings";
    options.folderName = "CyDAW";
    preferences_ = std::make_unique<juce::PropertiesFile>(options);
    favoriteAssetIds_ = juce::StringArray::fromLines(
        preferences_->getValue("favoriteGraphAssets"));
    recentAssetIds_ = juce::StringArray::fromLines(
        preferences_->getValue("recentGraphAssets"));

    fileThread_.startThread(juce::Thread::Priority::low);
    rebuildFileView();
    refreshTranslations();
    showCategoryView();
}

GraphManagerComponent::~GraphManagerComponent()
{
    if (fileTree_ != nullptr)
        fileTree_->removeListener(this);
    categoryTree_.setRootItem(nullptr);
    fileList_.reset();
    fileFilter_.reset();
    fileThread_.stopThread(2'000);
}

void GraphManagerComponent::setAssets(std::vector<Asset> assets)
{
    const auto previousIdentity = selectedAsset_.has_value()
        ? assetIdentity(*selectedAsset_) : juce::String {};
    assets_ = std::move(assets);
    const auto previous = std::find_if(
        assets_.begin(), assets_.end(),
        [this, &previousIdentity](const Asset& asset) {
            return previousIdentity.isNotEmpty()
                && assetIdentity(asset) == previousIdentity;
        });
    if (previous != assets_.end())
        selectedAsset_ = *previous;
    else
        selectedAsset_.reset();
    openButton_.setEnabled(false);
    favoriteButton_.setEnabled(false);
    editMetadataButton_.setEnabled(false);
    revealButton_.setEnabled(false);
    variantButton_.setEnabled(false);
    detailTitle_.setText(tr("graph.manager.details"),
                         juce::dontSendNotification);
    detailText_.setText(tr("graph.manager.noSelection"),
                        juce::dontSendNotification);
    topologyPreview_->setAsset(std::nullopt);
    rebuildCategoryTree();
}

void GraphManagerComponent::setFactoryPresetLoadHandler(
    std::function<void(const juce::String&)> handler)
{
    factoryPresetLoadHandler_ = std::move(handler);
}

void GraphManagerComponent::setUserPresetLoadHandler(
    std::function<void(const juce::File&)> handler)
{
    userPresetLoadHandler_ = std::move(handler);
}

void GraphManagerComponent::setNodeParameterPresetLoadHandler(
    std::function<void(const juce::File&)> handler)
{
    nodeParameterPresetLoadHandler_ = std::move(handler);
}

void GraphManagerComponent::setRefreshHandler(std::function<void()> handler)
{
    refreshHandler_ = std::move(handler);
}

void GraphManagerComponent::setNodeParameterPresetTarget(
    const juce::String& nodeTypeKey,
    const juce::String& processorKey,
    const bool editable)
{
    if (targetNodeTypeKey_ == nodeTypeKey
        && targetProcessorKey_ == processorKey
        && targetNodeEditable_ == editable)
        return;
    targetNodeTypeKey_ = nodeTypeKey;
    targetProcessorKey_ = processorKey;
    targetNodeEditable_ = editable;
    if (selectedAsset_.has_value())
        selectAsset(*selectedAsset_);
}

void GraphManagerComponent::setSubgraphOpenHandler(
    std::function<void(const juce::String&)> handler)
{
    subgraphOpenHandler_ = std::move(handler);
}

void GraphManagerComponent::setSubgraphVariantHandler(
    std::function<void(const juce::String&)> handler)
{
    subgraphVariantHandler_ = std::move(handler);
    if (selectedAsset_.has_value())
        variantButton_.setEnabled(
            selectedAsset_->kind == AssetKind::reusableSubgraph
            && selectedAsset_->valid
            && static_cast<bool>(subgraphVariantHandler_));
}

void GraphManagerComponent::refreshTranslations()
{
    categoryViewButton_.setButtonText(tr("graph.manager.categories"));
    fileViewButton_.setButtonText(tr("graph.manager.files"));
    refreshButton_.setButtonText(tr("graph.manager.refresh"));
    openButton_.setButtonText(tr("graph.manager.open"));
    favoriteButton_.setButtonText(tr("graph.manager.favorite"));
    editMetadataButton_.setButtonText(tr("graph.manager.editMetadata"));
    revealButton_.setButtonText(tr("graph.manager.reveal"));
    variantButton_.setButtonText(tr("graph.manager.createVariant"));
    const auto previousGrouping = juce::jmax(
        1, groupingSelector_.getSelectedId());
    groupingSelector_.clear(juce::dontSendNotification);
    groupingSelector_.addItem(tr("graph.manager.groupCategory"), 1);
    groupingSelector_.addItem(tr("graph.manager.groupRole"), 2);
    groupingSelector_.addItem(tr("graph.manager.groupTags"), 3);
    groupingSelector_.addItem(tr("graph.manager.groupAuthor"), 4);
    groupingSelector_.setSelectedId(previousGrouping,
                                    juce::dontSendNotification);
    categoryViewButton_.setTooltip(tr("graph.manager.categoriesHelp"));
    fileViewButton_.setTooltip(tr("graph.manager.filesHelp"));
    refreshButton_.setTooltip(tr("graph.manager.refreshHelp"));
    openButton_.setTooltip(tr("graph.manager.openHelp"));
    favoriteButton_.setTooltip(tr("graph.manager.favoriteHelp"));
    editMetadataButton_.setTooltip(tr("graph.manager.editMetadataHelp"));
    revealButton_.setTooltip(tr("graph.manager.revealHelp"));
    variantButton_.setTooltip(tr("graph.manager.createVariantHelp"));
    groupingSelector_.setTooltip(tr("graph.manager.groupingHelp"));
    search_.setTooltip(tr("graph.manager.searchHelp"));
    topologyPreview_->refreshTranslations();
    search_.setTextToShowWhenEmpty(tr("graph.manager.search"),
                                   juce::Colour(0xff747a80));
    detailTitle_.setText(tr("graph.manager.details"),
                         juce::dontSendNotification);
    if (detailText_.getText().isEmpty())
        detailText_.setText(tr("graph.manager.noSelection"),
                            juce::dontSendNotification);
    rebuildCategoryTree();
}

void GraphManagerComponent::refreshFiles()
{
    auto directory = userGraphPresetDirectory().getParentDirectory();
    (void) userGraphPresetDirectory().createDirectory();
    (void) userNodeParameterPresetDirectory().createDirectory();
    if (fileList_ != nullptr)
        fileList_->setDirectory(directory, true, true);
}

bool GraphManagerComponent::selectAssetByKey(const juce::String& key)
{
    const auto asset = std::find_if(
        assets_.begin(), assets_.end(), [&key](const Asset& value) {
            return value.key == key
                   || value.name.equalsIgnoreCase(key)
                   || value.file.getFileNameWithoutExtension()
                          .equalsIgnoreCase(key);
        });
    if (asset == assets_.end())
        return false;
    selectAsset(*asset);
    const auto findTreeItem = [&asset](auto&& self,
                                       juce::TreeViewItem* parent)
        -> AssetItem* {
        if (parent == nullptr)
            return nullptr;
        if (auto* assetItem = dynamic_cast<AssetItem*>(parent);
            assetItem != nullptr && assetItem->represents(*asset))
            return assetItem;
        for (auto index = 0; index < parent->getNumSubItems(); ++index) {
            if (auto* found = self(self, parent->getSubItem(index))) {
                parent->setOpen(true);
                return found;
            }
        }
        return nullptr;
    };
    if (auto* item = findTreeItem(findTreeItem, categoryRoot_.get())) {
        item->setSelected(true, true);
        categoryTree_.scrollToKeepItemVisible(item);
    }
    return true;
}

juce::String GraphManagerComponent::selectedAssetKey() const
{
    return selectedAsset_.has_value() ? selectedAsset_->key : juce::String {};
}

std::size_t GraphManagerComponent::selectedPreviewNodeCount() const noexcept
{
    return selectedAsset_.has_value()
        ? selectedAsset_->previewNodes.size() : 0;
}

std::size_t GraphManagerComponent::selectedPreviewConnectionCount() const noexcept
{
    return selectedAsset_.has_value()
        ? selectedAsset_->previewConnections.size() : 0;
}

void GraphManagerComponent::showFileBrowser()
{
    showFileView();
}

void GraphManagerComponent::rebuildFileView()
{
    if (fileTree_ != nullptr)
        fileTree_->removeListener(this);
    fileTree_.reset();
    fileList_.reset();
    fileFilter_.reset();

    auto query = search_.getText().trim();
    query = query.replaceCharacters("*?", "  ").trim();
    query = query.replaceCharacter(' ', '*');
    const auto prefix = query.isEmpty() ? juce::String("*")
                                        : "*" + query + "*";
    const auto wildcard = prefix + graphPresetFileExtension + ";"
        + prefix + nodeParameterPresetFileExtension;
    fileFilter_ = std::make_unique<juce::WildcardFileFilter>(
        wildcard, "*", tr("graph.manager.assetFiles"));
    fileList_ = std::make_unique<juce::DirectoryContentsList>(
        fileFilter_.get(), fileThread_);
    fileTree_ = std::make_unique<juce::FileTreeComponent>(*fileList_);
    addAndMakeVisible(*fileTree_);
    fileTree_->addListener(this);
    fileTree_->setVisible(showingFiles_);
    refreshFiles();
    resized();
}

void GraphManagerComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(0xff141619));
    graphics.setColour(juce::Colour(0xff34393e));
    graphics.drawRect(getLocalBounds(), 1);
    const auto dividerX = juce::roundToInt(getWidth() * 0.66f);
    graphics.drawVerticalLine(dividerX, 45.0f,
                              static_cast<float>(getHeight() - 8));
}

void GraphManagerComponent::resized()
{
    auto area = getLocalBounds().reduced(8);
    auto toolbar = area.removeFromTop(29);
    categoryViewButton_.setBounds(toolbar.removeFromLeft(112));
    toolbar.removeFromLeft(4);
    fileViewButton_.setBounds(toolbar.removeFromLeft(105));
    toolbar.removeFromLeft(6);
    refreshButton_.setBounds(toolbar.removeFromRight(82));
    toolbar.removeFromRight(6);
    groupingSelector_.setBounds(toolbar.removeFromRight(150));
    toolbar.removeFromRight(6);
    search_.setBounds(toolbar);
    area.removeFromTop(7);
    auto details = area.removeFromRight(juce::jmax(280,
        juce::roundToInt(area.getWidth() * 0.34f))).reduced(9, 2);
    detailTitle_.setBounds(details.removeFromTop(24));
    details.removeFromTop(6);
    auto actions = details.removeFromBottom(62);
    auto secondaryActions = actions.removeFromBottom(29);
    actions.removeFromBottom(4);
    favoriteButton_.setBounds(actions.removeFromLeft(76));
    actions.removeFromLeft(6);
    editMetadataButton_.setBounds(actions.removeFromLeft(76));
    actions.removeFromLeft(6);
    revealButton_.setBounds(actions);
    variantButton_.setBounds(secondaryActions.removeFromLeft(126));
    secondaryActions.removeFromLeft(6);
    openButton_.setBounds(secondaryActions);
    details.removeFromBottom(6);
    if (topologyPreview_->isVisible()) {
        topologyPreview_->setBounds(details.removeFromTop(
            juce::jlimit(118, 168, details.getHeight() / 3)));
        details.removeFromTop(8);
    } else {
        topologyPreview_->setBounds({});
    }
    detailText_.setBounds(details);
    area.removeFromRight(8);
    categoryTree_.setBounds(area);
    if (fileTree_ != nullptr)
        fileTree_->setBounds(area);
}

void GraphManagerComponent::showCategoryView()
{
    showingFiles_ = false;
    categoryViewButton_.setToggleState(true, juce::dontSendNotification);
    fileViewButton_.setToggleState(false, juce::dontSendNotification);
    categoryTree_.setVisible(true);
    if (fileTree_ != nullptr)
        fileTree_->setVisible(false);
    search_.setEnabled(true);
}

void GraphManagerComponent::showFileView()
{
    showingFiles_ = true;
    categoryViewButton_.setToggleState(false, juce::dontSendNotification);
    fileViewButton_.setToggleState(true, juce::dontSendNotification);
    categoryTree_.setVisible(false);
    rebuildFileView();
    if (fileTree_ != nullptr)
        fileTree_->setVisible(true);
    search_.setEnabled(true);
}

void GraphManagerComponent::rebuildCategoryTree()
{
    categoryTree_.setRootItem(nullptr);
    categoryRoot_ = std::make_unique<CategoryItem>("root");
    const auto query = search_.getText().trim().toLowerCase();

    using CategoryGroups = std::map<juce::String, std::vector<Asset>>;
    using KindGroups = std::map<juce::String, CategoryGroups>;
    std::map<juce::String, KindGroups> groups;
    std::vector<Asset> matchedAssets;
    for (const auto& asset : assets_) {
        const auto searchable = (asset.name + " " + asset.category + " "
                                 + asset.scope + " " + asset.key + " "
                                 + asset.detail + " " + asset.role + " "
                                 + asset.version + " " + asset.inputs + " "
                                 + asset.outputs + " "
                                 + asset.author + " "
                                 + asset.tags.joinIntoString(" ") + " "
                                 + asset.file.getFileNameWithoutExtension())
                                    .toLowerCase();
        if (query.isNotEmpty() && !searchable.contains(query))
            continue;
        matchedAssets.push_back(asset);
        juce::StringArray groupNames;
        if (groupingSelector_.getSelectedId() == 2)
            groupNames.add(asset.role.isNotEmpty()
                               ? asset.role
                               : tr("graph.manager.uncategorized"));
        else if (groupingSelector_.getSelectedId() == 3)
            groupNames = asset.tags;
        else if (groupingSelector_.getSelectedId() == 4)
            groupNames.add(asset.author.isNotEmpty()
                               ? asset.author
                               : tr("graph.manager.unknownAuthor"));
        else
            groupNames.add(asset.category.isNotEmpty()
                               ? asset.category
                               : tr("graph.manager.uncategorized"));
        if (groupNames.isEmpty())
            groupNames.add(tr("graph.manager.untagged"));
        for (const auto& groupName : groupNames)
            groups[asset.scope][tr(assetKindKey(asset.kind))][groupName]
                .push_back(asset);
    }

    const auto appendSmartCollection = [this, &matchedAssets](
        const juce::String& label, const juce::StringArray& identities) {
        std::vector<Asset> items;
        for (const auto& identity : identities) {
            const auto found = std::find_if(
                matchedAssets.begin(), matchedAssets.end(),
                [this, &identity](const Asset& asset) {
                    return assetIdentity(asset) == identity;
                });
            if (found != matchedAssets.end())
                items.push_back(*found);
        }
        if (items.empty())
            return;
        auto* collection = new CategoryItem(
            label + "  (" + juce::String(static_cast<int>(items.size())) + ")");
        for (auto& item : items)
            collection->addSubItem(new AssetItem(*this, std::move(item)));
        categoryRoot_->addSubItem(collection);
        collection->setOpen(true);
    };
    appendSmartCollection(tr("graph.manager.favorites"), favoriteAssetIds_);
    appendSmartCollection(tr("graph.manager.recent"), recentAssetIds_);

    for (auto& [scope, kinds] : groups) {
        auto scopeCount = std::size_t {};
        for (const auto& [kind, categories] : kinds) {
            juce::ignoreUnused(kind);
            for (const auto& [category, items] : categories) {
                juce::ignoreUnused(category);
                scopeCount += items.size();
            }
        }
        auto* scopeItem = new CategoryItem(
            scope + "  (" + juce::String(static_cast<int>(scopeCount)) + ")");
        for (auto& [kind, categories] : kinds) {
            auto kindCount = std::size_t {};
            for (const auto& [category, items] : categories) {
                juce::ignoreUnused(category);
                kindCount += items.size();
            }
            auto* kindItem = new CategoryItem(
                kind + "  (" + juce::String(static_cast<int>(kindCount)) + ")");
            if (groupingSelector_.getSelectedId() == 1) {
                struct CategoryBranch {
                    std::map<juce::String,
                             std::unique_ptr<CategoryBranch>> children;
                    std::vector<Asset> assets;
                };
                CategoryBranch root;
                for (auto& [category, items] : categories) {
                    auto path = juce::StringArray::fromTokens(
                        category, "/\\", "");
                    path.trim();
                    path.removeEmptyStrings();
                    if (path.isEmpty())
                        path.add(tr("graph.manager.uncategorized"));
                    auto* branch = &root;
                    for (const auto& segment : path) {
                        auto& child = branch->children[segment];
                        if (child == nullptr)
                            child = std::make_unique<CategoryBranch>();
                        branch = child.get();
                    }
                    branch->assets.insert(branch->assets.end(),
                                          std::make_move_iterator(items.begin()),
                                          std::make_move_iterator(items.end()));
                }

                const auto branchCount = [](auto&& self,
                                            const CategoryBranch& branch)
                    -> int {
                    auto count = static_cast<int>(branch.assets.size());
                    for (const auto& [childLabel, child] : branch.children) {
                        juce::ignoreUnused(childLabel);
                        count += self(self, *child);
                    }
                    return count;
                };
                const auto appendBranch =
                    [this, &branchCount](auto&& self, CategoryItem& parent,
                                        const juce::String& label,
                                        CategoryBranch& branch) -> void {
                    const auto count = branchCount(branchCount, branch);
                    auto* item = new CategoryItem(
                        label + "  (" + juce::String(count) + ")");
                    std::sort(branch.assets.begin(), branch.assets.end(),
                              [](const auto& left, const auto& right) {
                                  return left.name.compareIgnoreCase(
                                             right.name) < 0;
                              });
                    for (auto& asset : branch.assets)
                        item->addSubItem(
                            new AssetItem(*this, std::move(asset)));
                    for (auto& [childLabel, child] : branch.children)
                        self(self, *item, childLabel, *child);
                    parent.addSubItem(item);
                    item->setOpen(true);
                };
                for (auto& [label, branch] : root.children)
                    appendBranch(appendBranch, *kindItem, label, *branch);
            } else {
                for (auto& [category, items] : categories) {
                    auto* categoryItem = new CategoryItem(
                        category + "  ("
                        + juce::String(static_cast<int>(items.size())) + ")");
                    std::sort(items.begin(), items.end(), [](const auto& left,
                                                             const auto& right) {
                        return left.name.compareIgnoreCase(right.name) < 0;
                    });
                    for (auto& item : items)
                        categoryItem->addSubItem(
                            new AssetItem(*this, std::move(item)));
                    kindItem->addSubItem(categoryItem);
                    categoryItem->setOpen(true);
                }
            }
            scopeItem->addSubItem(kindItem);
            kindItem->setOpen(true);
        }
        categoryRoot_->addSubItem(scopeItem);
        scopeItem->setOpen(true);
    }
    categoryTree_.setRootItem(categoryRoot_.get());

    std::optional<Asset> selection;
    if (selectedAsset_.has_value()) {
        const auto identity = assetIdentity(*selectedAsset_);
        if (const auto found = std::find_if(
                matchedAssets.begin(), matchedAssets.end(),
                [this, &identity](const Asset& asset) {
                    return assetIdentity(asset) == identity;
                }); found != matchedAssets.end())
            selection = *found;
    }
    if (!selection.has_value() && !matchedAssets.empty())
        selection = matchedAssets.front();

    if (!selection.has_value()) {
        selectedAsset_.reset();
        openButton_.setEnabled(false);
        favoriteButton_.setEnabled(false);
        editMetadataButton_.setEnabled(false);
        revealButton_.setEnabled(false);
        variantButton_.setEnabled(false);
        detailTitle_.setText(tr("graph.manager.details"),
                             juce::dontSendNotification);
        detailText_.setText(tr("graph.manager.noSelection"),
                            juce::dontSendNotification);
        topologyPreview_->setAsset(std::nullopt);
        resized();
        return;
    }

    selectAsset(*selection);
    const auto findTreeItem = [&selection](auto&& self,
                                           juce::TreeViewItem* parent)
        -> AssetItem* {
        if (parent == nullptr)
            return nullptr;
        if (auto* assetItem = dynamic_cast<AssetItem*>(parent);
            assetItem != nullptr && assetItem->represents(*selection))
            return assetItem;
        for (auto index = 0; index < parent->getNumSubItems(); ++index) {
            if (auto* found = self(self, parent->getSubItem(index))) {
                parent->setOpen(true);
                return found;
            }
        }
        return nullptr;
    };
    if (auto* item = findTreeItem(findTreeItem, categoryRoot_.get())) {
        item->setSelected(true, true);
        categoryTree_.scrollToKeepItemVisible(item);
    }
}

void GraphManagerComponent::selectAsset(const Asset& asset)
{
    selectedAsset_ = asset;
    topologyPreview_->setAsset(selectedAsset_);
    resized();
    openButton_.setEnabled(true);
    favoriteButton_.setEnabled(true);
    editMetadataButton_.setEnabled(
        (asset.kind == AssetKind::nodeParameterPreset
         || asset.kind == AssetKind::userPreset)
        && asset.file.existsAsFile());
    revealButton_.setEnabled(asset.file.existsAsFile());
    variantButton_.setEnabled(asset.kind == AssetKind::reusableSubgraph
                              && asset.valid
                              && static_cast<bool>(
                                  subgraphVariantHandler_));
    favoriteButton_.setButtonText(
        tr(isFavorite(asset) ? "graph.manager.unfavorite"
                             : "graph.manager.favorite"));
    detailTitle_.setText(
        tr(asset.kind == AssetKind::nodeParameterPreset
               ? "graph.manager.nodePresetDetails"
           : asset.kind == AssetKind::reusableSubgraph
               ? "graph.manager.subgraphDetails"
               : "graph.manager.graphDetails"),
        juce::dontSendNotification);
    auto text = asset.name + "\n\n" + tr(assetKindKey(asset.kind));
    if (asset.category.isNotEmpty())
        text += "\n" + tr("graph.manager.category") + ": " + asset.category;
    if (asset.scope.isNotEmpty())
        text += "\n" + tr("graph.manager.scope") + ": " + asset.scope;
    if (asset.role.isNotEmpty())
        text += "\n" + tr("graph.manager.role") + ": " + asset.role;
    if (asset.version.isNotEmpty())
        text += "\n" + tr("graph.manager.version") + ": "
                + asset.version;
    if (!asset.tags.isEmpty())
        text += "\n" + tr("graph.manager.tags") + ": "
                + asset.tags.joinIntoString(", ");
    if (asset.author.isNotEmpty())
        text += "\n" + tr("graph.manager.author") + ": "
                + asset.author;
    if (asset.inputs.isNotEmpty())
        text += "\n\n" + tr("graph.manager.inputs") + "\n"
                + asset.inputs;
    if (asset.outputs.isNotEmpty())
        text += "\n\n" + tr("graph.manager.outputs") + "\n"
                + asset.outputs;
    const auto openable = canOpenAsset(asset);
    text += "\n\n" + tr("graph.manager.validation") + ": "
            + tr(openable ? "graph.manager.valid"
                          : "graph.manager.invalid");
    if (asset.kind == AssetKind::nodeParameterPreset) {
        if (targetNodeTypeKey_.isEmpty())
            text += "\n" + tr("graph.manager.nodePresetSelectTarget");
        else if (!targetNodeEditable_)
            text += "\n" + tr("graph.manager.nodePresetTargetLocked");
        else
            text += "\n" + tr(openable
                ? "graph.manager.nodePresetCompatible"
                : "graph.manager.nodePresetIncompatible");
    }
    if (asset.validationMessage.isNotEmpty())
        text += "\n" + asset.validationMessage;
    if (asset.detail.isNotEmpty())
        text += "\n\n" + asset.detail;
    if (asset.file.existsAsFile())
        text += "\n\n" + asset.file.getFullPathName();
    detailText_.setText(text, juce::dontSendNotification);
    openButton_.setEnabled(openable);
}

void GraphManagerComponent::openAsset(const Asset& asset)
{
    selectAsset(asset);
    if (!canOpenAsset(asset))
        return;
    touchRecent(asset);
    if (asset.kind == AssetKind::factoryPreset && factoryPresetLoadHandler_)
        factoryPresetLoadHandler_(asset.key);
    else if (asset.kind == AssetKind::userPreset
             && userPresetLoadHandler_ && asset.file.existsAsFile())
        userPresetLoadHandler_(asset.file);
    else if (asset.kind == AssetKind::reusableSubgraph
             && subgraphOpenHandler_)
        subgraphOpenHandler_(asset.key);
    else if (asset.kind == AssetKind::nodeParameterPreset
             && nodeParameterPresetLoadHandler_ && asset.file.existsAsFile())
        nodeParameterPresetLoadHandler_(asset.file);
}

bool GraphManagerComponent::canOpenAsset(const Asset& asset) const
{
    if (!asset.valid)
        return false;
    if (asset.kind != AssetKind::nodeParameterPreset)
        return true;
    if (!targetNodeEditable_ || targetNodeTypeKey_.isEmpty())
        return false;
    return asset.nodeTypeKey == targetNodeTypeKey_
        || (asset.processorKey.isNotEmpty()
            && asset.processorKey == targetProcessorKey_);
}

bool GraphManagerComponent::isActiveAsset(const Asset& asset) const
{
    return selectedAsset_.has_value()
        && assetIdentity(*selectedAsset_) == assetIdentity(asset);
}

void GraphManagerComponent::openFileAsset(const juce::File& file)
{
    if (!file.existsAsFile())
        return;
    const auto known = std::ranges::find(assets_, file, &Asset::file);
    if (known != assets_.end()) {
        openAsset(*known);
        return;
    }
    if (file.hasFileExtension(nodeParameterPresetFileExtension)) {
        if (nodeParameterPresetLoadHandler_)
            nodeParameterPresetLoadHandler_(file);
        return;
    }
    if (file.hasFileExtension(graphPresetFileExtension)
        && userPresetLoadHandler_)
        userPresetLoadHandler_(file);
}

juce::String GraphManagerComponent::assetIdentity(const Asset& asset) const
{
    const auto source = (asset.kind == AssetKind::userPreset
                         || asset.kind == AssetKind::nodeParameterPreset)
                            && asset.file.getFullPathName().isNotEmpty()
        ? asset.file.getFullPathName()
        : asset.key;
    return juce::String(static_cast<int>(asset.kind)) + ":" + source;
}

bool GraphManagerComponent::isFavorite(const Asset& asset) const
{
    return favoriteAssetIds_.contains(assetIdentity(asset));
}

void GraphManagerComponent::toggleFavorite(const Asset& asset)
{
    const auto identity = assetIdentity(asset);
    if (favoriteAssetIds_.contains(identity))
        favoriteAssetIds_.removeString(identity);
    else
        favoriteAssetIds_.insert(0, identity);
    saveCollections();
    if (selectedAsset_.has_value()
        && assetIdentity(*selectedAsset_) == identity)
        favoriteButton_.setButtonText(
            tr(isFavorite(asset) ? "graph.manager.unfavorite"
                                 : "graph.manager.favorite"));
    rebuildCategoryTree();
}

void GraphManagerComponent::revealAsset(const Asset& asset)
{
    if (!asset.file.existsAsFile())
        return;
    asset.file.revealToUser();
}

void GraphManagerComponent::editAssetMetadata(const Asset& asset)
{
    if (asset.kind == AssetKind::userPreset) {
        if (!asset.file.existsAsFile() || asset.file.getSize() <= 0
            || asset.file.getSize() > maximumGraphPresetBytes)
            return;
        const auto parsed = parseGraphPresetDocument(
            juce::JSON::parse(asset.file.loadFileAsString()));
        if (!parsed.has_value())
            return;

        auto* dialog = new juce::AlertWindow(
            tr("graph.manager.editGraphMetadataTitle"),
            tr("graph.manager.editGraphMetadataPrompt"),
            juce::MessageBoxIconType::NoIcon);
        dialog->addTextEditor("name", parsed->name,
                              tr("graph.manager.metadataName"));
        dialog->addTextEditor("category", parsed->metadata.category,
                              tr("graph.manager.metadataCategory"));
        dialog->addTextEditor("tags",
                              parsed->metadata.tags.joinIntoString(", "),
                              tr("graph.manager.metadataTags"));
        dialog->addTextEditor("description", parsed->metadata.description,
                              tr("graph.manager.metadataDescription"));
        dialog->addTextEditor("author", parsed->metadata.author,
                              tr("graph.manager.metadataAuthor"));
        dialog->addButton(tr("graph.manager.saveMetadata"), 1,
                          juce::KeyPress(juce::KeyPress::returnKey));
        dialog->addButton(tr("layout.cancel"), 0,
                          juce::KeyPress(juce::KeyPress::escapeKey));

        const juce::Component::SafePointer<GraphManagerComponent> safeThis(this);
        const auto destination = asset.file;
        dialog->enterModalState(
            true,
            juce::ModalCallbackFunction::create(
                [safeThis, dialog, destination](const int result) {
                    if (safeThis == nullptr || result != 1)
                        return;
                    auto document = parseGraphPresetDocument(
                        juce::JSON::parse(destination.loadFileAsString()));
                    if (!document.has_value())
                        return;
                    const auto name = dialog->getTextEditorContents("name")
                                          .trim().substring(0, 128);
                    if (name.isEmpty())
                        return;
                    document->name = name;
                    document->metadata.category
                        = dialog->getTextEditorContents("category")
                              .trim().substring(0, 192);
                    document->metadata.description
                        = dialog->getTextEditorContents("description")
                              .trim().substring(0, 2'048);
                    document->metadata.author
                        = dialog->getTextEditorContents("author")
                              .trim().substring(0, 128);
                    auto tags = juce::StringArray::fromTokens(
                        dialog->getTextEditorContents("tags"), ",;", "\"");
                    tags.trim();
                    tags.removeEmptyStrings();
                    tags.removeDuplicates(true);
                    while (tags.size() > 64)
                        tags.remove(tags.size() - 1);
                    document->metadata.tags = tags;

                    // Keep the asset wrapper and the editable Graph state in
                    // sync, so opening the Graph immediately shows the same
                    // name, tooltip and search tags as the manager.
                    if (auto* graph = document->graph.getDynamicObject()) {
                        graph->setProperty("graphName", name);
                        graph->setProperty(
                            "graphCategory", document->metadata.category);
                        graph->setProperty(
                            "graphAuthor", document->metadata.author);
                        graph->setProperty(
                            "graphDescription",
                            document->metadata.description);
                        juce::Array<juce::var> graphTags;
                        for (const auto& tag : tags)
                            graphTags.add(tag);
                        graph->setProperty("graphTags", graphTags);
                    }

                    const auto encoded = juce::JSON::toString(
                        makeGraphPresetDocument(document->name,
                                                document->graph,
                                                document->metadata), true);
                    juce::TemporaryFile temporary(destination);
                    const auto saved
                        = encoded.getNumBytesAsUTF8() > 0
                          && encoded.getNumBytesAsUTF8()
                                 <= maximumGraphPresetBytes
                          && temporary.getFile().replaceWithText(encoded)
                          && temporary.overwriteTargetFileWithTemporary();
                    if (!saved) {
                        juce::NativeMessageBox::showMessageBoxAsync(
                            juce::MessageBoxIconType::WarningIcon,
                            tr("graph.manager.editGraphMetadataTitle"),
                            tr("graph.manager.metadataSaveFailed"));
                        return;
                    }
                    if (safeThis->refreshHandler_)
                        safeThis->refreshHandler_();
                    else {
                        safeThis->refreshFiles();
                        safeThis->rebuildCategoryTree();
                    }
                }),
            true);
        return;
    }

    if (asset.kind != AssetKind::nodeParameterPreset
        || !asset.file.existsAsFile()
        || asset.file.getSize() <= 0
        || asset.file.getSize() > maximumNodeParameterPresetBytes)
        return;

    const auto parsed = parseNodeParameterPresetDocument(
        juce::JSON::parse(asset.file.loadFileAsString()));
    if (!parsed.has_value())
        return;

    auto* dialog = new juce::AlertWindow(
        tr("graph.manager.editMetadataTitle"),
        tr("graph.manager.editMetadataPrompt"),
        juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor("name", parsed->name,
                          tr("graph.manager.metadataName"));
    dialog->addTextEditor("category", parsed->categoryKey,
                          tr("graph.manager.metadataCategory"));
    dialog->addTextEditor("tags", parsed->tags.joinIntoString(", "),
                          tr("graph.manager.metadataTags"));
    dialog->addTextEditor("description", parsed->description,
                          tr("graph.manager.metadataDescription"));
    dialog->addButton(tr("graph.manager.saveMetadata"), 1,
                      juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton(tr("layout.cancel"), 0,
                      juce::KeyPress(juce::KeyPress::escapeKey));

    const juce::Component::SafePointer<GraphManagerComponent> safeThis(this);
    const auto destination = asset.file;
    dialog->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [safeThis, dialog, destination](const int result) {
                if (safeThis == nullptr || result != 1)
                    return;
                auto document = parseNodeParameterPresetDocument(
                    juce::JSON::parse(destination.loadFileAsString()));
                if (!document.has_value())
                    return;
                const auto name = dialog->getTextEditorContents("name")
                                      .trim().substring(0, 128);
                if (name.isEmpty())
                    return;
                document->name = name;
                document->categoryKey
                    = dialog->getTextEditorContents("category")
                          .trim().substring(0, 192);
                document->description
                    = dialog->getTextEditorContents("description")
                          .trim().substring(0, 2'048);
                auto tags = juce::StringArray::fromTokens(
                    dialog->getTextEditorContents("tags"), ",;", "\"");
                tags.trim();
                tags.removeEmptyStrings();
                tags.removeDuplicates(true);
                while (tags.size() > 64)
                    tags.remove(tags.size() - 1);
                document->tags = std::move(tags);

                const auto encoded = juce::JSON::toString(
                    makeNodeParameterPresetDocument(*document), true);
                juce::TemporaryFile temporary(destination);
                const auto saved
                    = encoded.getNumBytesAsUTF8() > 0
                      && encoded.getNumBytesAsUTF8()
                             <= maximumNodeParameterPresetBytes
                      && temporary.getFile().replaceWithText(encoded)
                      && temporary.overwriteTargetFileWithTemporary();
                if (!saved) {
                    juce::NativeMessageBox::showMessageBoxAsync(
                        juce::MessageBoxIconType::WarningIcon,
                        tr("graph.manager.editMetadataTitle"),
                        tr("graph.manager.metadataSaveFailed"));
                    return;
                }
                if (safeThis->refreshHandler_)
                    safeThis->refreshHandler_();
                else {
                    safeThis->refreshFiles();
                    safeThis->rebuildCategoryTree();
                }
            }),
        true);
}

void GraphManagerComponent::duplicateUserGraph(const Asset& asset)
{
    if (asset.kind != AssetKind::userPreset || !asset.file.existsAsFile()
        || asset.file.getSize() <= 0
        || asset.file.getSize() > maximumGraphPresetBytes)
        return;
    auto document = parseGraphPresetDocument(
        juce::JSON::parse(asset.file.loadFileAsString()));
    if (!document.has_value())
        return;

    document->name = tr("graph.manager.duplicateGraphName",
                        "name", document->name).substring(0, 128);
    auto slug = juce::File::createLegalFileName(document->name)
                    .toLowerCase().replaceCharacter(' ', '-');
    slug = slug.retainCharacters(
        "abcdefghijklmnopqrstuvwxyz0123456789-_");
    if (slug.isEmpty())
        slug = "graph";
    const auto suffix = juce::Uuid().toString().removeCharacters("{}-")
                            .toLowerCase().substring(0, 12);
    document->metadata.definitionId =
        "user." + slug.substring(0, 96) + "." + suffix;
    document->metadata.version = { 1, 0, 0 };
    document->metadata.scope = cydaw::graph::GraphScope::user;
    if (auto* graph = document->graph.getDynamicObject()) {
        graph->setProperty("definitionId",
                           document->metadata.definitionId);
        graph->setProperty("definitionVersion", "1.0.0");
        graph->setProperty("graphName", document->name);
        graph->setProperty("graphScope", "user");
        if (auto* exportProfile = graph->getProperty("vst3Export")
                                      .getDynamicObject()) {
            exportProfile->setProperty("productName", document->name);
            exportProfile->setProperty("pluginCode", juce::String {});
            exportProfile->setProperty("version", "1.0.0");
        }
    }

    auto baseName = juce::File::createLegalFileName(document->name);
    if (baseName.isEmpty())
        baseName = "Graph Copy";
    const auto destination = asset.file.getParentDirectory()
        .getNonexistentChildFile(baseName, graphPresetFileExtension, false);
    const auto encoded = juce::JSON::toString(
        makeGraphPresetDocument(document->name, document->graph,
                                document->metadata), true);
    const auto saved = encoded.getNumBytesAsUTF8() > 0
        && encoded.getNumBytesAsUTF8() <= maximumGraphPresetBytes
        && destination.replaceWithText(encoded);
    if (!saved) {
        juce::NativeMessageBox::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            tr("graph.manager.duplicateGraph"),
            tr("graph.manager.duplicateGraphFailed"));
        return;
    }
    if (refreshHandler_)
        refreshHandler_();
    else {
        refreshFiles();
        rebuildCategoryTree();
    }
}

void GraphManagerComponent::touchRecent(const Asset& asset)
{
    const auto identity = assetIdentity(asset);
    recentAssetIds_.removeString(identity);
    recentAssetIds_.insert(0, identity);
    while (recentAssetIds_.size() > 32)
        recentAssetIds_.remove(recentAssetIds_.size() - 1);
    saveCollections();
    rebuildCategoryTree();
}

void GraphManagerComponent::saveCollections()
{
    if (preferences_ == nullptr)
        return;
    preferences_->setValue("favoriteGraphAssets",
                           favoriteAssetIds_.joinIntoString("\n"));
    preferences_->setValue("recentGraphAssets",
                           recentAssetIds_.joinIntoString("\n"));
    preferences_->saveIfNeeded();
}

juce::String GraphManagerComponent::roleLabel(
    const cydaw::graph::GraphRole role) const
{
    using Role = cydaw::graph::GraphRole;
    if (role == Role::audioEffect) return tr("graph.role.audioEffect");
    if (role == Role::instrument) return tr("graph.role.instrument");
    if (role == Role::midiEffect) return tr("graph.role.midiEffect");
    if (role == Role::modulator) return tr("graph.role.modulator");
    if (role == Role::analyzer) return tr("graph.role.analyzer");
    if (role == Role::utility) return tr("graph.role.utility");
    return tr("graph.role.generic");
}

void GraphManagerComponent::selectionChanged()
{
    if (fileTree_ == nullptr)
        return;
    const auto file = fileTree_->getSelectedFile();
    if (!file.existsAsFile())
        return;
    const auto known = std::find_if(
        assets_.begin(), assets_.end(), [&file](const Asset& asset) {
            return asset.file == file;
        });
    if (known != assets_.end()) {
        selectAsset(*known);
        return;
    }
    const auto kind = file.hasFileExtension(nodeParameterPresetFileExtension)
        ? AssetKind::nodeParameterPreset : AssetKind::userPreset;
    selectAsset({ kind, file.getFileNameWithoutExtension(),
                  file.getFileNameWithoutExtension(), {},
                  tr("graph.scope.user"), {}, file });
}

void GraphManagerComponent::fileClicked(const juce::File& file,
                                        const juce::MouseEvent& event)
{
    if (!event.mods.isPopupMenu() || !file.existsAsFile())
        return;
    juce::PopupMenu menu;
    menu.addItem(1, tr("graph.manager.open"));
    juce::Component::SafePointer<GraphManagerComponent> safeThis(this);
    menu.showMenuAsync({}, [safeThis, file](const int result) {
        if (safeThis != nullptr && result == 1)
            safeThis->openFileAsset(file);
    });
}

void GraphManagerComponent::fileDoubleClicked(const juce::File& file)
{
    openFileAsset(file);
}

void GraphManagerComponent::browserRootChanged(const juce::File&)
{
}

} // namespace cydaw::app
