#include "LibraryBrowserComponent.h"
#include "CyDawLookAndFeel.h"
#include "GraphPresetFile.h"
#include "LibraryDragPayload.h"
#include "CyDawLookAndFeel.h"
#include "Localization.h"
#include "WindowsPluginBridge.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>

namespace cydaw::app {

std::vector<LibraryBrowserComponent::SampleMetadata>
LibraryBrowserComponent::scanSampleLibraries(
    const std::vector<juce::File>& roots,
    const std::size_t maximumItems,
    std::function<bool()> cancellationRequested)
{
    std::vector<SampleMetadata> samples;
    if (maximumItems == 0)
        return samples;
    samples.reserve(std::min<std::size_t>(maximumItems, 4'096));

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unordered_set<std::string> indexedPaths;
    for (const auto& root : roots) {
        if (!root.isDirectory())
            continue;
        juce::RangedDirectoryIterator iterator(
            root, true, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3",
            juce::File::findFiles);
        for (const auto& entry : iterator) {
            if (cancellationRequested && cancellationRequested())
                return {};
            const auto file = entry.getFile();
            const auto pathKey = normalisedPath(file).toStdString();
            if (!indexedPaths.insert(pathKey).second)
                continue;
            auto reader = std::unique_ptr<juce::AudioFormatReader>(
                formats.createReaderFor(file));
            if (reader == nullptr || reader->sampleRate <= 0.0
                || reader->lengthInSamples <= 0 || reader->numChannels == 0)
                continue;
            SampleMetadata sample;
            sample.file = file;
            sample.libraryRoot = root;
            sample.name = file.getFileNameWithoutExtension();
            sample.extension = file.getFileExtension().trimCharactersAtStart(".")
                                   .toUpperCase();
            sample.durationSeconds = static_cast<double>(reader->lengthInSamples)
                                     / reader->sampleRate;
            sample.sampleRate = reader->sampleRate;
            sample.channels = static_cast<int>(reader->numChannels);
            sample.fileSize = file.getSize();
            sample.modifiedTimeMs = file.getLastModificationTime().toMilliseconds();
            samples.push_back(std::move(sample));
            if (samples.size() >= maximumItems)
                break;
        }
        if (samples.size() >= maximumItems)
            break;
    }
    std::sort(samples.begin(), samples.end(), [](const auto& left,
                                                 const auto& right) {
        return left.file.getFullPathName().compareIgnoreCase(
                   right.file.getFullPathName()) < 0;
    });
    return samples;
}

bool LibraryBrowserComponent::sampleMatchesSearch(
    const SampleMetadata& sample, const juce::String& query)
{
    juce::StringArray tokens;
    tokens.addTokens(query.toLowerCase(), " \t\r\n", "\"'");
    tokens.removeEmptyStrings();
    if (tokens.isEmpty())
        return true;
    const auto haystack = (sample.name + " " + sample.extension + " "
                           + sample.file.getParentDirectory().getFullPathName()
                           + " " + sample.libraryRoot.getFileName() + " "
                           + formatSampleMetadata(sample)).toLowerCase();
    for (const auto& token : tokens)
        if (!haystack.contains(token))
            return false;
    return true;
}

juce::String LibraryBrowserComponent::formatSampleMetadata(
    const SampleMetadata& sample)
{
    const auto totalMilliseconds = static_cast<juce::int64>(
        std::llround(std::max(0.0, sample.durationSeconds) * 1'000.0));
    const auto minutes = totalMilliseconds / 60'000;
    const auto seconds = (totalMilliseconds / 1'000) % 60;
    const auto milliseconds = totalMilliseconds % 1'000;
    auto duration = juce::String(minutes).paddedLeft('0', 2) + ":"
                    + juce::String(seconds).paddedLeft('0', 2) + "."
                    + juce::String(milliseconds).paddedLeft('0', 3);
    const auto rateKhz = juce::String(sample.sampleRate / 1'000.0, 1)
                         + " kHz";
    const auto channels = juce::String(sample.channels) + " ch";
    const auto sizeMb = juce::String(
        static_cast<double>(sample.fileSize) / (1'024.0 * 1'024.0), 1)
                        + " MB";
    return sample.extension + "  |  " + duration + "  |  " + rateKhz
           + "  |  " + channels + "  |  " + sizeMb;
}

bool LibraryBrowserComponent::sampleSortsBefore(
    const SampleMetadata& left, const SampleMetadata& right,
    const SampleSortMode mode)
{
    switch (mode) {
    case SampleSortMode::newest:
        if (left.modifiedTimeMs != right.modifiedTimeMs)
            return left.modifiedTimeMs > right.modifiedTimeMs;
        break;
    case SampleSortMode::duration:
        if (left.durationSeconds != right.durationSeconds)
            return left.durationSeconds < right.durationSeconds;
        break;
    case SampleSortMode::size:
        if (left.fileSize != right.fileSize)
            return left.fileSize > right.fileSize;
        break;
    case SampleSortMode::name:
        break;
    }
    const auto nameOrder = left.name.compareIgnoreCase(right.name);
    if (nameOrder != 0)
        return nameOrder < 0;
    return left.file.getFullPathName().compareIgnoreCase(
               right.file.getFullPathName()) < 0;
}

bool LibraryBrowserComponent::sampleMatchesFilter(
    const SampleMetadata& sample, const SampleFilterMode mode)
{
    switch (mode) {
        case SampleFilterMode::mono:
            return sample.channels == 1;
        case SampleFilterMode::stereo:
            return sample.channels >= 2;
        case SampleFilterMode::shortDuration:
            return sample.durationSeconds > 0.0
                   && sample.durationSeconds <= 2.0;
        case SampleFilterMode::mediumDuration:
            return sample.durationSeconds > 2.0
                   && sample.durationSeconds <= 16.0;
        case SampleFilterMode::longDuration:
            return sample.durationSeconds > 16.0;
        case SampleFilterMode::all:
        default:
            return true;
    }
}

juce::var LibraryBrowserComponent::serialiseSampleIndexCache(
    const std::vector<SampleMetadata>& samples,
    const std::vector<juce::File>& roots)
{
    auto document = std::make_unique<juce::DynamicObject>();
    document->setProperty("schema", 1);
    juce::Array<juce::var> rootValues;
    for (const auto& root : roots)
        rootValues.add(root.getFullPathName());
    document->setProperty("roots", rootValues);

    juce::Array<juce::var> sampleValues;
    sampleValues.ensureStorageAllocated(static_cast<int>(std::min<std::size_t>(
        samples.size(), 50'000)));
    for (const auto& sample : samples) {
        if (sampleValues.size() >= 50'000)
            break;
        auto value = std::make_unique<juce::DynamicObject>();
        value->setProperty("file", sample.file.getFullPathName());
        value->setProperty("root", sample.libraryRoot.getFullPathName());
        value->setProperty("name", sample.name);
        value->setProperty("extension", sample.extension);
        value->setProperty("duration", sample.durationSeconds);
        value->setProperty("sampleRate", sample.sampleRate);
        value->setProperty("channels", sample.channels);
        value->setProperty("fileSize", sample.fileSize);
        value->setProperty("modified", sample.modifiedTimeMs);
        sampleValues.add(juce::var(value.release()));
    }
    document->setProperty("samples", sampleValues);
    return juce::var(document.release());
}

std::vector<LibraryBrowserComponent::SampleMetadata>
LibraryBrowserComponent::restoreSampleIndexCache(
    const juce::var& cache,
    const std::vector<juce::File>& expectedRoots)
{
    const auto* document = cache.getDynamicObject();
    if (document == nullptr
        || static_cast<int>(document->getProperty("schema")) != 1)
        return {};
    const auto* rootValues = document->getProperty("roots").getArray();
    const auto* sampleValues = document->getProperty("samples").getArray();
    if (rootValues == nullptr || sampleValues == nullptr
        || sampleValues->size() > 50'000)
        return {};

    juce::StringArray cachedRoots;
    juce::StringArray currentRoots;
    for (const auto& value : *rootValues)
        cachedRoots.add(normalisedPath(juce::File(value.toString())));
    for (const auto& root : expectedRoots)
        currentRoots.add(normalisedPath(root));
    cachedRoots.sort(true);
    currentRoots.sort(true);
    if (cachedRoots != currentRoots)
        return {};

    std::vector<SampleMetadata> samples;
    samples.reserve(static_cast<std::size_t>(sampleValues->size()));
    for (const auto& encoded : *sampleValues) {
        const auto* value = encoded.getDynamicObject();
        if (value == nullptr)
            return {};
        SampleMetadata sample;
        sample.file = juce::File(value->getProperty("file").toString());
        sample.libraryRoot = juce::File(
            value->getProperty("root").toString());
        sample.name = value->getProperty("name").toString();
        sample.extension = value->getProperty("extension").toString();
        sample.durationSeconds = static_cast<double>(
            value->getProperty("duration"));
        sample.sampleRate = static_cast<double>(
            value->getProperty("sampleRate"));
        sample.channels = static_cast<int>(value->getProperty("channels"));
        sample.fileSize = static_cast<juce::int64>(
            value->getProperty("fileSize"));
        sample.modifiedTimeMs = static_cast<juce::int64>(
            value->getProperty("modified"));
        const auto knownRoot = std::any_of(
            expectedRoots.begin(), expectedRoots.end(),
            [&sample](const auto& root) {
                return normalisedPath(root)
                       == normalisedPath(sample.libraryRoot);
            });
        if (!knownRoot || sample.file == juce::File {}
            || sample.libraryRoot == juce::File {}
            || sample.name.isEmpty() || sample.name.length() > 1'024
            || sample.durationSeconds <= 0.0
            || sample.sampleRate <= 0.0 || sample.sampleRate > 768'000.0
            || sample.channels <= 0 || sample.channels > 64
            || sample.fileSize < 0)
            return {};
        samples.push_back(std::move(sample));
    }
    return samples;
}

bool LibraryBrowserComponent::favoriteListContains(
    const juce::StringArray& favoritePaths, const juce::File& file)
{
    return favoritePaths.contains(normalisedPath(file), true);
}

bool LibraryBrowserComponent::updateFavoriteList(
    juce::StringArray& favoritePaths, const juce::File& file,
    const bool shouldBeFavorite)
{
    const auto path = normalisedPath(file);
    const auto existing = favoritePaths.indexOf(path, true);
    if (shouldBeFavorite) {
        if (existing >= 0)
            return false;
        favoritePaths.add(path);
        return true;
    }
    if (existing < 0)
        return false;
    favoritePaths.remove(existing);
    return true;
}

LibraryBrowserComponent::LibraryBrowserComponent()
    : juce::Thread("CyDAW sample indexer"),
      waveformThumbnail_(256, waveformFormatManager_,
                         waveformThumbnailCache_)
{
    waveformFormatManager_.registerBasicFormats();
    addAndMakeVisible(titleLabel_);
    addAndMakeVisible(searchEditor_);
    addAndMakeVisible(categorySelector_);
    for (auto* button : { &allQuickButton_, &samplesQuickButton_,
                          &instrumentsQuickButton_, &effectsQuickButton_,
                          &graphsQuickButton_ }) {
        addAndMakeVisible(button);
        button->setRadioGroupId(781);
        button->setClickingTogglesState(false);
    }
    addAndMakeVisible(locationSelector_);
    addAndMakeVisible(sampleFilterSelector_);
    addAndMakeVisible(sortSelector_);
    addAndMakeVisible(addFolderButton_);
    addAndMakeVisible(removeFolderButton_);
    addAndMakeVisible(addPluginButton_);
    addAndMakeVisible(rescanButton_);
    addAndMakeVisible(libraryMenuButton_);
    addAndMakeVisible(favoriteButton_);
    addAndMakeVisible(previewButton_);
    addAndMakeVisible(autoPreviewButton_);
    addAndMakeVisible(previewGainSlider_);
    addAndMakeVisible(scanStatusLabel_);
    addAndMakeVisible(bridgeStatusLabel_);
    addAndMakeVisible(list_);

    titleLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffe8ebf2));
    titleLabel_.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    searchEditor_.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff11141a));
    searchEditor_.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff303744));
    searchEditor_.onTextChange = [this] { applyFilter(); };
    categorySelector_.onChange = [this] {
        if (categorySelector_.getSelectedId() != 2
            && targetedSampleInsertHandler_) {
            targetedSampleInsertHandler_ = {};
            titleLabel_.setText(tr("browser.title"),
                                juce::dontSendNotification);
        }
        refreshQuickCategoryButtons();
        applyFilter();
    };
    allQuickButton_.onClick = [this] { selectQuickCategory(1); };
    samplesQuickButton_.onClick = [this] { selectQuickCategory(2); };
    instrumentsQuickButton_.onClick = [this] { selectQuickCategory(3); };
    effectsQuickButton_.onClick = [this] { selectQuickCategory(4); };
    graphsQuickButton_.onClick = [this] { selectQuickCategory(7); };
    locationSelector_.onChange = [this] {
        applyFilter();
        refreshSelectionActions();
    };
    sampleFilterSelector_.onChange = [this] {
        sampleFilterMode_ = std::max(1, sampleFilterSelector_.getSelectedId());
        saveLibrarySettings();
        applyFilter();
    };
    sortSelector_.onChange = [this] {
        sortMode_ = std::max(1, sortSelector_.getSelectedId());
        saveLibrarySettings();
        applyFilter();
    };
    addFolderButton_.onClick = [this] { chooseSampleFolder(); };
    removeFolderButton_.onClick = [this] { removeSelectedLibrary(); };
    addPluginButton_.onClick = [this] { choosePluginFile(); };
    rescanButton_.onClick = [this] {
        refreshUserGraphPresets();
        rescanLibraries();
    };
    libraryMenuButton_.onClick = [this] { showLibraryMenu(); };
    favoriteButton_.onClick = [this] { toggleSelectedFavorite(); };
    previewButton_.onClick = [this] { toggleSamplePreview(); };
    autoPreviewButton_.onClick = [this] {
        saveLibrarySettings();
        if (autoPreviewButton_.getToggleState())
            startSamplePreviewAt(0.0);
    };
    previewGainSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    previewGainSlider_.setTextBoxStyle(juce::Slider::TextBoxRight,
                                       false, 50, 20);
    previewGainSlider_.setRange(-48.0, 6.0, 0.1);
    previewGainSlider_.setValue(-3.0, juce::dontSendNotification);
    previewGainSlider_.setTextValueSuffix(" dB");
    previewGainSlider_.onValueChange = [this] {
        if (samplePreviewGainHandler_)
            samplePreviewGainHandler_(
                static_cast<float>(previewGainSlider_.getValue()));
        if (previewGainSlider_.getThumbBeingDragged() < 0)
            saveLibrarySettings();
    };
    previewGainSlider_.onDragEnd = [this] { saveLibrarySettings(); };
    scanStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff7f889a));
    scanStatusLabel_.setFont(juce::FontOptions(9.5f));
    bridgeStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(0xff7f889a));
    bridgeStatusLabel_.setFont(juce::FontOptions(9.0f));
    bridgeStatusLabel_.setTooltip(tr("browser.bridgeHelp"));
    list_.setRowHeight(48);
    list_.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff151820));
    list_.setColour(juce::ListBox::outlineColourId, juce::Colour(0xff303744));
    list_.setOutlineThickness(1);

    items_.push_back({ ItemType::instrument, {}, {}, {},
                       "browser.polySynth", "poly-graph-synth" });
    items_.push_back({ ItemType::effect, {}, {}, {},
                       "browser.gainPan", "gain-pan" });
    items_.push_back({ ItemType::effect, {}, {}, {},
                       "node.drive", "drive" });
    items_.push_back({ ItemType::effect, {}, {}, {},
                       "node.lowpass", "lowpass" });
    items_.push_back({ ItemType::effect, {}, {}, {},
                       "node.delay", "delay" });
    items_.push_back({ ItemType::effect, {}, {}, {},
                       "node.reverb", "reverb" });
    items_.push_back({ ItemType::effect, {}, {}, {},
                       "node.compressor", "compressor" });
    items_.push_back({ ItemType::effect, {}, {}, {},
                       "node.equalizer", "eq" });
    items_.push_back({ ItemType::effect, {}, {}, {},
                       "node.spectrum", "spectrum" });

    juce::PropertiesFile::Options options;
    options.applicationName = "CyDAW";
    options.filenameSuffix = ".library.settings";
    options.folderName = "CyDAW";
    options.osxLibrarySubFolder = "Application Support";
    properties_.setStorageParameters(options);

    loadLibrarySettings();
    refreshUserGraphPresets();
    sampleIndexCacheFile_ = properties_.getUserSettings()->getFile()
                                .getSiblingFile("sample-index-v1.json");
    refreshTranslations();
    loadSampleIndexCache();
    if (!libraryRoots_.empty())
        rescanLibraries();
    startTimerHz(4);
}

LibraryBrowserComponent::~LibraryBrowserComponent()
{
    stopTimer();
    stopSamplePreview();
    signalThreadShouldExit();
    stopThread(3'000);
}

void LibraryBrowserComponent::showLibraryMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, tr("browser.addFolder"));
    menu.addItem(2, tr("browser.removeLocationHelp"),
                 locationSelector_.getSelectedId() > 1);
    menu.addSeparator();
    menu.addItem(3, tr("browser.addPlugin"));
    menu.addItem(4, tr("browser.rescan"));
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&libraryMenuButton_),
        [this](const int result) {
            switch (result) {
            case 1: chooseSampleFolder(); break;
            case 2: removeSelectedLibrary(); break;
            case 3: choosePluginFile(); break;
            case 4:
                refreshUserGraphPresets();
                rescanLibraries();
                break;
            default: break;
            }
        });
}

void LibraryBrowserComponent::refreshTranslations()
{
    const auto selectedCategory = std::max(1, categorySelector_.getSelectedId());
    titleLabel_.setText(tr("browser.title"), juce::dontSendNotification);
    searchEditor_.setTextToShowWhenEmpty(tr("browser.search"), juce::Colour(0xff6f7889));
    addFolderButton_.setButtonText(tr("browser.addFolder"));
    addPluginButton_.setButtonText(tr("browser.addPlugin"));
    categorySelector_.clear(juce::dontSendNotification);
    categorySelector_.addItem(tr("browser.all"), 1);
    categorySelector_.addItem(tr("browser.samples"), 2);
    categorySelector_.addItem(tr("browser.instruments"), 3);
    categorySelector_.addItem(tr("browser.effects"), 4);
    categorySelector_.addItem(tr("browser.plugins"), 5);
    categorySelector_.addItem(tr("browser.devices"), 6);
    categorySelector_.addItem(tr("browser.graphPresets"), 7);
    categorySelector_.setSelectedId(juce::jlimit(1, 7, selectedCategory),
                                    juce::dontSendNotification);
    allQuickButton_.setButtonText(tr("browser.quickAll"));
    samplesQuickButton_.setButtonText(tr("browser.quickSamples"));
    instrumentsQuickButton_.setButtonText(tr("browser.quickInstruments"));
    effectsQuickButton_.setButtonText(tr("browser.quickEffects"));
    graphsQuickButton_.setButtonText(tr("browser.quickGraphs"));
    allQuickButton_.setTooltip(tr("browser.quickAllHelp"));
    samplesQuickButton_.setTooltip(tr("browser.quickSamplesHelp"));
    instrumentsQuickButton_.setTooltip(tr("browser.quickInstrumentsHelp"));
    effectsQuickButton_.setTooltip(tr("browser.quickEffectsHelp"));
    graphsQuickButton_.setTooltip(tr("browser.quickGraphsHelp"));
    refreshQuickCategoryButtons();
    sortSelector_.clear(juce::dontSendNotification);
    sortSelector_.addItem(tr("browser.sort.default"), 1);
    sortSelector_.addItem(tr("browser.sort.name"), 2);
    sortSelector_.addItem(tr("browser.sort.newest"), 3);
    sortSelector_.addItem(tr("browser.sort.duration"), 4);
    sortSelector_.addItem(tr("browser.sort.size"), 5);
    sortSelector_.setSelectedId(juce::jlimit(1, 5, sortMode_),
                                juce::dontSendNotification);
    sortSelector_.setTooltip(tr("browser.sort.help"));
    sampleFilterSelector_.clear(juce::dontSendNotification);
    sampleFilterSelector_.addItem(tr("browser.filter.allAudio"), 1);
    sampleFilterSelector_.addItem(tr("browser.filter.mono"), 2);
    sampleFilterSelector_.addItem(tr("browser.filter.stereo"), 3);
    sampleFilterSelector_.addItem(tr("browser.filter.short"), 4);
    sampleFilterSelector_.addItem(tr("browser.filter.medium"), 5);
    sampleFilterSelector_.addItem(tr("browser.filter.long"), 6);
    sampleFilterSelector_.setSelectedId(
        juce::jlimit(1, 6, sampleFilterMode_), juce::dontSendNotification);
    sampleFilterSelector_.setTooltip(tr("browser.filter.help"));
    rescanButton_.setButtonText(tr("browser.rescan"));
    // Three ASCII dots rather than an ellipsis glyph: this label is drawn at
    // 11 px where the two are indistinguishable, and it cannot be mis-encoded.
    libraryMenuButton_.setButtonText("...");
    libraryMenuButton_.setTooltip(tr("browser.addFolder") + " / "
                                  + tr("browser.addPlugin") + " / "
                                  + tr("browser.rescan"));
    removeFolderButton_.setButtonText("−");
    removeFolderButton_.setTooltip(tr("browser.removeLocationHelp"));
    favoriteButton_.setTooltip(tr("browser.favoriteHelp"));
    previewButton_.setTooltip(tr("browser.previewHelp"));
    autoPreviewButton_.setButtonText(tr("browser.autoPreview"));
    autoPreviewButton_.setTooltip(tr("browser.autoPreviewHelp"));
    previewGainSlider_.setTooltip(tr("browser.previewGainHelp"));

    for (auto& item : items_)
        if (item.translationKey.isNotEmpty()) {
            item.name = tr(item.translationKey);
            if (item.type == ItemType::graphPreset)
                item.detail = tr(item.soundSource
                                     ? "browser.factoryInstrumentGraph"
                                     : "browser.factoryEffectGraph");
            else if (item.type == ItemType::userGraphPreset)
                item.detail = tr("browser.userGraphPreset");
            else if (item.deviceKey.isNotEmpty())
                item.detail = tr("browser.factoryGraphDevice");
        }
    refreshLocationSelector();
    refreshSelectionActions();
    applyFilter();
    refreshBridgeStatus();
}

void LibraryBrowserComponent::setFactoryGraphPresets(
    std::vector<FactoryGraphPreset> presets)
{
    std::erase_if(items_, [](const auto& item) {
        return item.type == ItemType::graphPreset;
    });
    // Factory graphs follow the built-in devices, in catalogue order, and
    // precede everything that comes from disk.
    const auto insertBefore = std::find_if(
        items_.begin(), items_.end(), [](const auto& item) {
            return item.type != ItemType::instrument
                   && item.type != ItemType::effect;
        });
    std::vector<Item> entries;
    entries.reserve(presets.size());
    for (const auto& preset : presets) {
        // A graph that is also offered as a built-in device is listed once.
        if (std::any_of(items_.begin(), items_.end(),
                        [&preset](const auto& existing) {
                            return existing.deviceKey == preset.key;
                        }))
            continue;
        Item item;
        item.type = ItemType::graphPreset;
        item.name = tr(preset.translationKey);
        item.detail = tr(preset.soundSource
                             ? "browser.factoryInstrumentGraph"
                             : "browser.factoryEffectGraph");
        item.translationKey = preset.translationKey;
        item.deviceKey = preset.key;
        item.soundSource = preset.soundSource;
        entries.push_back(std::move(item));
    }
    items_.insert(insertBefore, std::make_move_iterator(entries.begin()),
                  std::make_move_iterator(entries.end()));
    applyFilter();
}

void LibraryBrowserComponent::selectQuickCategory(const int categoryId)
{
    categorySelector_.setSelectedId(juce::jlimit(1, 7, categoryId),
                                    juce::sendNotificationSync);
    searchEditor_.grabKeyboardFocus();
}

void LibraryBrowserComponent::refreshQuickCategoryButtons()
{
    const auto category = categorySelector_.getSelectedId();
    allQuickButton_.setToggleState(category == 1,
                                   juce::dontSendNotification);
    samplesQuickButton_.setToggleState(category == 2,
                                       juce::dontSendNotification);
    instrumentsQuickButton_.setToggleState(category == 3,
                                           juce::dontSendNotification);
    effectsQuickButton_.setToggleState(category == 4,
                                       juce::dontSendNotification);
    graphsQuickButton_.setToggleState(category == 7,
                                      juce::dontSendNotification);
}

void LibraryBrowserComponent::setAudioOpenHandler(std::function<void(const juce::File&)> handler)
{
    audioOpenHandler_ = std::move(handler);
}

void LibraryBrowserComponent::setPluginInsertHandler(std::function<void(const juce::File&)> handler)
{
    pluginInsertHandler_ = std::move(handler);
}

void LibraryBrowserComponent::setFactoryDeviceInsertHandler(
    std::function<void(const juce::String&)> handler)
{
    factoryDeviceInsertHandler_ = std::move(handler);
}

void LibraryBrowserComponent::setGraphPresetFileLoadHandler(
    std::function<void(const juce::File&)> handler)
{
    graphPresetFileLoadHandler_ = std::move(handler);
}

void LibraryBrowserComponent::refreshUserGraphPresets()
{
    std::erase_if(items_, [](const auto& item) {
        return item.type == ItemType::userGraphPreset;
    });
    const auto directory = userGraphPresetDirectory();
    if (directory.isDirectory()) {
        auto files = directory.findChildFiles(
            juce::File::findFiles, false,
            "*" + juce::String(graphPresetFileExtension));
        files.sort();
        const auto count = juce::jmin(1'000, files.size());
        for (int index = 0; index < count; ++index) {
            const auto& file = files.getReference(index);
            if (file.getSize() <= 0
                || file.getSize() > maximumGraphPresetBytes)
                continue;
            const auto document = parseGraphPresetDocument(
                juce::JSON::parse(file.loadFileAsString()));
            if (!document.has_value())
                continue;
            Item item;
            item.type = ItemType::userGraphPreset;
            item.name = document->name;
            item.detail = tr("browser.userGraphPreset") + " · "
                + juce::String(cydaw::graph::toString(document->metadata.role))
                + " · v"
                + juce::String(document->metadata.version.toString());
            item.file = file;
            items_.push_back(std::move(item));
        }
    }
    applyFilter();
}

void LibraryBrowserComponent::setSamplePreviewHandlers(
    std::function<juce::Result(const juce::File&, double)> startHandler,
    std::function<void()> stopHandler,
    std::function<bool()> isPlayingHandler,
    std::function<double()> positionHandler,
    std::function<void(float)> gainHandler)
{
    samplePreviewStartHandler_ = std::move(startHandler);
    samplePreviewStopHandler_ = std::move(stopHandler);
    samplePreviewIsPlayingHandler_ = std::move(isPlayingHandler);
    samplePreviewPositionHandler_ = std::move(positionHandler);
    samplePreviewGainHandler_ = std::move(gainHandler);
    if (samplePreviewGainHandler_)
        samplePreviewGainHandler_(
            static_cast<float>(previewGainSlider_.getValue()));
    refreshSelectionActions();
}

void LibraryBrowserComponent::focusForDeviceInsertion()
{
    targetedSampleInsertHandler_ = {};
    titleLabel_.setText(tr("browser.title"), juce::dontSendNotification);
    categorySelector_.setSelectedId(6, juce::sendNotificationSync);
    locationSelector_.setSelectedId(1, juce::sendNotificationSync);
    searchEditor_.grabKeyboardFocus();
    searchEditor_.selectAll();
}

void LibraryBrowserComponent::focusForSampleInsertion(
    std::function<void(const juce::File&)> handler,
    juce::String targetLabel)
{
    targetedSampleInsertHandler_ = std::move(handler);
    titleLabel_.setText(
        targetLabel.isEmpty()
            ? tr("browser.title")
            : tr("browser.insertSampleInto", "target", targetLabel),
        juce::dontSendNotification);
    categorySelector_.setSelectedId(2, juce::sendNotificationSync);
    locationSelector_.setSelectedId(1, juce::sendNotificationSync);
    searchEditor_.grabKeyboardFocus();
    searchEditor_.selectAll();
}

void LibraryBrowserComponent::paint(juce::Graphics& graphics)
{
    const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    graphics.setColour(juce::Colour(0xff181b22));
    graphics.fillRoundedRectangle(bounds, 5.0f);
    graphics.setColour(juce::Colour(0xff2d333e));
    graphics.drawRoundedRectangle(bounds, 5.0f, 1.0f);
    if (wideLayout_) {
        for (const auto panel : { facetPanelBounds_, detailPanelBounds_ }) {
            graphics.setColour(juce::Colour(0xff13171d));
            graphics.fillRoundedRectangle(panel.toFloat(), 4.0f);
            graphics.setColour(juce::Colour(0xff303744));
            graphics.drawRoundedRectangle(panel.toFloat(), 4.0f, 1.0f);
        }
    }
    CyDawLookAndFeel::drawPanelAccent(graphics, 10.0f, 10.0f);

    if (!waveformPreviewBounds_.isEmpty()) {
        const auto preview = waveformPreviewBounds_.toFloat();
        graphics.setColour(juce::Colour(0xff10141a));
        graphics.fillRoundedRectangle(preview, 4.0f);
        graphics.setColour(juce::Colour(0xff303744));
        graphics.drawRoundedRectangle(preview, 4.0f, 1.0f);
        auto content = waveformPreviewBounds_.reduced(7, 5);
        auto title = content.removeFromTop(17);
        const auto* item = selectedItem();
        graphics.setFont(juce::FontOptions(8.8f, juce::Font::bold));
        graphics.setColour(juce::Colour(0xffaeb7c6));
        if (item != nullptr && item->type == ItemType::sample
            && item->file == waveformPreviewFile_) {
            graphics.drawFittedText(
                item->name + "  |  " + formatSampleMetadata(item->sample),
                title, juce::Justification::centredLeft, 1, 0.62f);
            content = waveformContentBounds();
            if (waveformThumbnail_.getTotalLength() > 0.0) {
                graphics.setColour(juce::Colour(0xff4fd3ae));
                waveformThumbnail_.drawChannels(
                    graphics, content, 0.0,
                    waveformThumbnail_.getTotalLength(), 0.92f);
                graphics.setColour(juce::Colour(0xff596474));
                graphics.drawVerticalLine(content.getCentreX(),
                                          static_cast<float>(content.getY()),
                                          static_cast<float>(content.getBottom()));
                if (previewFile_ == item->file
                    && samplePreviewIsPlayingHandler_
                    && samplePreviewIsPlayingHandler_()
                    && samplePreviewPositionHandler_) {
                    const auto progress = juce::jlimit(
                        0.0, 1.0,
                        samplePreviewPositionHandler_()
                            / waveformThumbnail_.getTotalLength());
                    const auto playheadX = content.getX()
                        + juce::roundToInt(progress * content.getWidth());
                    graphics.setColour(juce::Colour(0xffff8a00));
                    graphics.drawVerticalLine(
                        playheadX, static_cast<float>(content.getY()),
                        static_cast<float>(content.getBottom()));
                    juce::Path marker;
                    marker.addTriangle(
                        static_cast<float>(playheadX - 4),
                        static_cast<float>(content.getY()),
                        static_cast<float>(playheadX + 4),
                        static_cast<float>(content.getY()),
                        static_cast<float>(playheadX),
                        static_cast<float>(content.getY() + 5));
                    graphics.fillPath(marker);
                }
            } else {
                graphics.drawFittedText(tr("browser.waveformLoading"), content,
                                        juce::Justification::centred, 1);
            }
        } else {
            graphics.drawFittedText(tr("browser.waveformEmpty"), content,
                                    juce::Justification::centred, 2);
        }
    }
}

void LibraryBrowserComponent::mouseDown(const juce::MouseEvent& event)
{
    if (!event.mods.isLeftButtonDown()
        || !waveformContentBounds().contains(event.getPosition())
        || waveformThumbnail_.getTotalLength() <= 0.0)
        return;

    const auto content = waveformContentBounds();
    const auto progress = juce::jlimit(
        0.0, 1.0,
        static_cast<double>(event.x - content.getX())
            / juce::jmax(1, content.getWidth()));
    startSamplePreviewAt(progress * waveformThumbnail_.getTotalLength());
}

void LibraryBrowserComponent::resized()
{
    auto area = getLocalBounds().reduced(10);
    wideLayout_ = area.getWidth() >= 740 && area.getHeight() >= 390;
    facetPanelBounds_ = {};
    detailPanelBounds_ = {};
    const auto layoutQuickButtons = [this](juce::Rectangle<int> row) {
        const auto width = juce::jmax(1, row.getWidth() / 5);
        allQuickButton_.setBounds(row.removeFromLeft(width).reduced(1, 0));
        samplesQuickButton_.setBounds(row.removeFromLeft(width).reduced(1, 0));
        instrumentsQuickButton_.setBounds(row.removeFromLeft(width).reduced(1, 0));
        effectsQuickButton_.setBounds(row.removeFromLeft(width).reduced(1, 0));
        graphsQuickButton_.setBounds(row.reduced(1, 0));
    };

    if (wideLayout_) {
        auto header = area.removeFromTop(30);
        titleLabel_.setBounds(
            header.removeFromLeft(210).withTrimmedLeft(12));
        searchEditor_.setBounds(header.reduced(4, 1));
        area.removeFromTop(5);
        layoutQuickButtons(area.removeFromTop(27));
        area.removeFromTop(7);

        auto facets = area.removeFromLeft(210);
        area.removeFromLeft(7);
        auto details = area.removeFromRight(244);
        area.removeFromRight(7);
        facetPanelBounds_ = facets;
        detailPanelBounds_ = details;
        list_.setBounds(area);

        facets.reduce(9, 9);
        categorySelector_.setBounds(facets.removeFromTop(28));
        facets.removeFromTop(6);
        locationSelector_.setBounds(facets.removeFromTop(28));
        facets.removeFromTop(6);
        sampleFilterSelector_.setBounds(facets.removeFromTop(28));
        facets.removeFromTop(6);
        sortSelector_.setBounds(facets.removeFromTop(28));
        facets.removeFromTop(10);
        for (auto* action : { &addFolderButton_, &removeFolderButton_,
                              &addPluginButton_, &rescanButton_ })
            action->setVisible(true);
        libraryMenuButton_.setVisible(false);
        auto folderActions = facets.removeFromTop(27);
        removeFolderButton_.setBounds(
            folderActions.removeFromRight(28).withTrimmedLeft(3));
        addFolderButton_.setBounds(folderActions);
        facets.removeFromTop(5);
        addPluginButton_.setBounds(facets.removeFromTop(27));
        facets.removeFromTop(5);
        rescanButton_.setBounds(facets.removeFromTop(27));
        bridgeStatusLabel_.setBounds(facets.removeFromBottom(36));
        scanStatusLabel_.setBounds(facets.removeFromBottom(34));

        details.reduce(9, 9);
        waveformPreviewBounds_ = details.removeFromTop(
            juce::jlimit(100, 170, details.getHeight() / 3));
        details.removeFromTop(8);
        auto selectionActions = details.removeFromTop(28);
        previewButton_.setBounds(
            selectionActions.removeFromRight(
                selectionActions.getWidth() / 2).withTrimmedLeft(3));
        favoriteButton_.setBounds(selectionActions);
        details.removeFromTop(7);
        auto auditionOptions = details.removeFromTop(27);
        autoPreviewButton_.setBounds(auditionOptions.removeFromLeft(72));
        previewGainSlider_.setBounds(auditionOptions.withTrimmedLeft(4));
        return;
    }

    titleLabel_.setBounds(area.removeFromTop(24).withTrimmedLeft(12));
    searchEditor_.setBounds(area.removeFromTop(28));
    area.removeFromTop(4);
    layoutQuickButtons(area.removeFromTop(25));
    area.removeFromTop(5);
    // Half a 180 px panel leaves roughly 48 px for text, which no word-length
    // label survives. The two short filters share a row; the two whose entries
    // are phrases get the full width instead of being elided into ambiguity.
    auto filters = area.removeFromTop(28);
    sampleFilterSelector_.setBounds(
        filters.removeFromRight(filters.getWidth() / 2).withTrimmedLeft(3));
    categorySelector_.setBounds(filters);
    area.removeFromTop(4);
    locationSelector_.setBounds(area.removeFromTop(26));
    area.removeFromTop(4);
    sortSelector_.setBounds(area.removeFromTop(26));
    // Library maintenance collapses into one menu here, which frees the row it
    // used to occupy and removes the two worst truncations in the application.
    for (auto* action : { &addFolderButton_, &removeFolderButton_,
                          &addPluginButton_, &rescanButton_ })
        action->setVisible(false);
    libraryMenuButton_.setVisible(true);

    area.removeFromTop(4);
    auto selectionActions = area.removeFromTop(26);
    libraryMenuButton_.setBounds(
        selectionActions.removeFromRight(30).withTrimmedLeft(3));
    previewButton_.setBounds(
        selectionActions.removeFromRight(
            preferredTextButtonWidth(previewButton_,
                                     selectionActions.getHeight(), 0, 16))
            .withTrimmedLeft(3));
    favoriteButton_.setBounds(selectionActions);
    area.removeFromTop(4);
    auto auditionOptions = area.removeFromTop(24);
    autoPreviewButton_.setBounds(auditionOptions.removeFromLeft(
        preferredTextButtonWidth(autoPreviewButton_,
                                 auditionOptions.getHeight(), 0, 16)));
    previewGainSlider_.setBounds(auditionOptions.withTrimmedLeft(3));
    area.removeFromTop(4);
    if (area.getHeight() >= 170) {
        waveformPreviewBounds_ = area.removeFromTop(82);
        area.removeFromTop(4);
    } else {
        waveformPreviewBounds_ = {};
    }
    bridgeStatusLabel_.setBounds(area.removeFromBottom(18));
    scanStatusLabel_.setBounds(area.removeFromBottom(20));
    area.removeFromTop(6);
    list_.setBounds(area);
}

int LibraryBrowserComponent::getNumRows()
{
    return static_cast<int>(filteredIndices_.size());
}

void LibraryBrowserComponent::paintListBoxItem(const int row,
                                               juce::Graphics& graphics,
                                               const int width,
                                               const int height,
                                               const bool selected)
{
    if (row < 0 || row >= static_cast<int>(filteredIndices_.size()))
        return;
    const auto& item = items_[static_cast<std::size_t>(filteredIndices_[static_cast<std::size_t>(row)])];
    if (selected)
        graphics.fillAll(juce::Colour(0xff26343a));
    const auto colour = item.type == ItemType::sample ? juce::Colour(0xff4fd3ae)
                        : item.type == ItemType::instrument ? juce::Colour(0xff8e9dff)
                        : item.type == ItemType::effect ? juce::Colour(0xffd47be8)
                        : (item.type == ItemType::graphPreset
                           || item.type == ItemType::userGraphPreset)
                              ? juce::Colour(0xffffa24d)
                                                            : juce::Colour(0xffefb45b);
    graphics.setColour(colour);
    graphics.fillEllipse(8.0f, static_cast<float>(height / 2 - 4), 8.0f, 8.0f);
    graphics.setColour(juce::Colour(0xffe8ebf2));
    graphics.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    graphics.drawFittedText(item.name, 24, 4, width - 52, 20,
                            juce::Justification::centredLeft, 1);
    if (item.favorite) {
        graphics.setColour(juce::Colour(0xffffc857));
        graphics.setFont(juce::FontOptions(14.0f));
        graphics.drawText(juce::CharPointer_UTF8("\xe2\x98\x85"), width - 27,
                          3, 20, 20, juce::Justification::centred);
    }
    graphics.setColour(juce::Colour(0xff7f889a));
    graphics.setFont(juce::FontOptions(9.5f));
    graphics.drawFittedText(item.detail, 24, 25, width - 30, 17,
                            juce::Justification::centredLeft, 1);
}

void LibraryBrowserComponent::listBoxItemClicked(
    const int row, const juce::MouseEvent& event)
{
    if (!event.mods.isPopupMenu()
        || row < 0 || row >= static_cast<int>(filteredIndices_.size()))
        return;

    list_.selectRow(row);
    const auto item = items_[static_cast<std::size_t>(
        filteredIndices_[static_cast<std::size_t>(row)])];
    constexpr int activateId = 1;
    constexpr int previewId = 2;
    constexpr int favoriteId = 3;
    constexpr int copyPathId = 4;
    constexpr int revealId = 5;
    juce::PopupMenu menu;

    if (item.type == ItemType::sample) {
        menu.addItem(
            activateId,
            tr(targetedSampleInsertHandler_ ? "browser.menu.insertTarget"
                                            : "browser.menu.openAudio"),
            static_cast<bool>(targetedSampleInsertHandler_)
                || static_cast<bool>(audioOpenHandler_));
        const auto previewing = previewFile_ == item.file
                                && samplePreviewIsPlayingHandler_
                                && samplePreviewIsPlayingHandler_();
        menu.addItem(previewId,
                     tr(previewing ? "browser.stopPreview"
                                   : "browser.preview"),
                     static_cast<bool>(samplePreviewStartHandler_));
        menu.addItem(favoriteId,
                     tr(item.favorite ? "browser.unfavorite"
                                      : "browser.favorite"));
    } else if (item.type == ItemType::plugin) {
        menu.addItem(activateId, tr("browser.menu.validatePlugin"),
                     item.file.exists());
    } else {
        menu.addItem(activateId,
                     tr((item.type == ItemType::graphPreset
                         || item.type == ItemType::userGraphPreset)
                            ? "browser.menu.loadGraphPreset"
                            : "browser.menu.insertDevice"),
                         item.type == ItemType::userGraphPreset
                             ? item.file.existsAsFile()
                                   && static_cast<bool>(graphPresetFileLoadHandler_)
                             : item.deviceKey.isNotEmpty()
                                   && static_cast<bool>(factoryDeviceInsertHandler_));
    }

    if (item.file != juce::File {}) {
        menu.addSeparator();
        menu.addItem(copyPathId, tr("browser.menu.copyPath"));
        menu.addItem(revealId, tr("browser.menu.reveal"), item.file.exists());
    }

    juce::Component::SafePointer<LibraryBrowserComponent> safeThis(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&list_),
        [safeThis, item](const int result) {
            if (safeThis == nullptr || result == 0)
                return;
            if (result == activateId) {
                safeThis->activateItem(item);
            } else if (result == previewId) {
                safeThis->toggleSamplePreview();
            } else if (result == favoriteId) {
                safeThis->toggleSelectedFavorite();
            } else if (result == copyPathId) {
                juce::SystemClipboard::copyTextToClipboard(
                    item.file.getFullPathName());
            } else if (result == revealId) {
                item.file.revealToUser();
            }
        });
}

void LibraryBrowserComponent::listBoxItemDoubleClicked(
    const int row, const juce::MouseEvent&)
{
    if (row < 0 || row >= static_cast<int>(filteredIndices_.size()))
        return;
    activateItem(items_[static_cast<std::size_t>(
        filteredIndices_[static_cast<std::size_t>(row)])]);
}

void LibraryBrowserComponent::activateItem(const Item& item)
{
    if (item.type == ItemType::sample && item.file.existsAsFile()) {
        if (targetedSampleInsertHandler_) {
            auto handler = std::move(targetedSampleInsertHandler_);
            titleLabel_.setText(tr("browser.title"),
                                juce::dontSendNotification);
            handler(item.file);
        } else if (audioOpenHandler_) {
            audioOpenHandler_(item.file);
        }
    }
    else if (item.type == ItemType::plugin && item.file.exists())
        probePlugin(item.file, true);
    else if ((item.type == ItemType::instrument
              || item.type == ItemType::effect
              || item.type == ItemType::graphPreset)
             && item.deviceKey.isNotEmpty()
             && factoryDeviceInsertHandler_)
        factoryDeviceInsertHandler_(item.deviceKey);
    else if (item.type == ItemType::userGraphPreset
             && item.file.existsAsFile()
             && graphPresetFileLoadHandler_)
        graphPresetFileLoadHandler_(item.file);
}

juce::var LibraryBrowserComponent::getDragSourceDescription(
    const juce::SparseSet<int>& selectedRows)
{
    if (selectedRows.size() <= 0)
        return {};
    const auto row = selectedRows[0];
    if (row < 0 || row >= static_cast<int>(filteredIndices_.size()))
        return {};
    const auto& item = items_[static_cast<std::size_t>(
        filteredIndices_[static_cast<std::size_t>(row)])];
    if (item.type == ItemType::sample && item.file.existsAsFile())
        return makeSampleDragPayload(item.file);
    if ((item.type == ItemType::instrument || item.type == ItemType::effect)
        && item.deviceKey.isNotEmpty())
        return makeFactoryDeviceDragPayload(item.deviceKey);
    if (item.type == ItemType::graphPreset && item.deviceKey.isNotEmpty())
        return makeGraphPresetDragPayload(item.deviceKey);
    if (item.type == ItemType::userGraphPreset && item.file.existsAsFile())
        return makeGraphPresetFileDragPayload(item.file);
    if (item.type == ItemType::plugin && item.file.exists())
        return makePluginDragPayload(item.file);
    return {};
}

void LibraryBrowserComponent::selectedRowsChanged(const int)
{
    refreshSelectionActions();
    refreshWaveformPreview();
    const auto* item = selectedItem();
    if (autoPreviewButton_.getToggleState()
        && item != nullptr && item->type == ItemType::sample
        && item->file.existsAsFile() && item->file != previewFile_)
        startSamplePreviewAt(0.0);
}

void LibraryBrowserComponent::timerCallback()
{
    if (waveformPreviewFile_ != juce::File {}
        && !waveformThumbnail_.isFullyLoaded()
        && !waveformPreviewBounds_.isEmpty())
        repaint(waveformPreviewBounds_);
    if (previewFile_ == juce::File {})
        return;
    if (!waveformPreviewBounds_.isEmpty())
        repaint(waveformPreviewBounds_);
    if (!samplePreviewIsPlayingHandler_
        || !samplePreviewIsPlayingHandler_()) {
        previewFile_ = {};
        refreshSelectionActions();
    }
}

void LibraryBrowserComponent::run()
{
    std::vector<juce::File> roots;
    juce::File pluginFile;
    bool validateProcessing = false;
    {
        const juce::ScopedLock lock(scanLock_);
        roots = scanRoots_;
        pluginFile = pluginProbeFile_;
        validateProcessing = validatePluginProcessing_;
    }
    if (pluginFile != juce::File {}) {
            const auto launch = WindowsPluginBridge::workerLaunchFor(pluginFile);
            const auto outputFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                        .getNonexistentChildFile("cydaw-plugin-probe", ".json", true);
            auto arguments = launch.command;
            arguments.add(validateProcessing ? "--validate-plugin" : "--probe-plugin");
            arguments.add(launch.usesWindowsGuestPaths()
                              ? WindowsPluginBridge::pathForWindowsGuest(pluginFile)
                              : pluginFile.getFullPathName());
            arguments.add("--output");
            arguments.add(launch.usesWindowsGuestPaths()
                              ? WindowsPluginBridge::pathForWindowsGuest(outputFile)
                              : outputFile.getFullPathName());
            juce::ChildProcess process;
            const auto started = launch.isValid() && process.start(arguments);
            auto finished = false;
            for (int waited = 0; started && waited < 15'000 && !threadShouldExit(); waited += 100) {
                if (process.waitForProcessToFinish(100)) {
                    finished = true;
                    break;
                }
            }
            if (started && !finished)
                process.kill();
            if (threadShouldExit()) {
                outputFile.deleteFile();
                return;
            }

            std::vector<Item> probed;
            auto processingSucceeded = false;
            juce::String probeError;
            if (finished && process.getExitCode() == 0) {
                const auto parsed = juce::JSON::parse(outputFile);
                if (const auto* descriptions = parsed.getArray()) {
                    for (const auto& value : *descriptions) {
                        if (const auto* description = value.getDynamicObject()) {
                            const auto name = description->getProperty("name").toString();
                            const auto manufacturer = description->getProperty("manufacturer").toString();
                            const auto version = description->getProperty("version").toString();
                            const auto format = description->getProperty("format").toString();
                            const auto error = description->getProperty("error").toString();
                            if (name.isEmpty()) {
                                if (probeError.isEmpty())
                                    probeError = error;
                                continue;
                            }
                            auto detail = format;
                            if (manufacturer.isNotEmpty())
                                detail += "  |  " + manufacturer;
                            if (version.isNotEmpty())
                                detail += "  " + version;
                            if (validateProcessing) {
                                const auto processingOk = static_cast<bool>(description->getProperty("processingOk"));
                                processingSucceeded = processingSucceeded || processingOk;
                                detail += processingOk
                                              ? "  |  DSP OK  |  "
                                                    + juce::String(static_cast<int>(description->getProperty("latencySamples")))
                                                    + " samples"
                                              : "  |  DSP FAILED";
                            }
                            probed.push_back({ ItemType::plugin, name,
                                               detail,
                                               pluginFile, {} });
                        }
                    }
                }
            }
            outputFile.deleteFile();

            juce::Component::SafePointer<LibraryBrowserComponent> safeThis(this);
            juce::MessageManager::callAsync([safeThis, probed = std::move(probed), pluginFile,
                                             finished, validateProcessing,
                                             processingSucceeded,
                                             probeError = std::move(probeError),
                                             launchError = launch.error]() mutable {
                if (safeThis == nullptr)
                    return;
                std::erase_if(safeThis->items_, [&pluginFile](const auto& item) {
                    return item.type == ItemType::plugin && item.file == pluginFile;
                });
                safeThis->items_.insert(safeThis->items_.end(),
                                        std::make_move_iterator(probed.begin()),
                                        std::make_move_iterator(probed.end()));
                safeThis->scanStatusLabel_.setText(
                    finished && probeError.isNotEmpty()
                             ? probeError
                             : finished
                                   ? tr(validateProcessing ? "browser.pluginValidated" : "browser.pluginsFound",
                                        "count", juce::String(static_cast<int>(probed.size())))
                             : (launchError.isNotEmpty()
                                    ? launchError
                                    : tr("browser.pluginProbeFailed")),
                    juce::dontSendNotification);
                safeThis->applyFilter();
                if (validateProcessing && processingSucceeded && safeThis->pluginInsertHandler_)
                    safeThis->pluginInsertHandler_(pluginFile);
            });
            return;
    }

    const auto metadata = scanSampleLibraries(
        roots, 50'000, [this] { return threadShouldExit(); });
    if (threadShouldExit())
        return;
    saveSampleIndexCache(metadata, roots);
    std::vector<Item> indexed;
    indexed.reserve(metadata.size());
    for (const auto& sample : metadata) {
        Item item;
        item.type = ItemType::sample;
        item.name = sample.name;
        item.detail = sample.file.getParentDirectory().getFileName()
                      + "  |  " + formatSampleMetadata(sample);
        item.file = sample.file;
        item.libraryRoot = sample.libraryRoot;
        item.sample = sample;
        indexed.push_back(std::move(item));
    }

    juce::Component::SafePointer<LibraryBrowserComponent> safeThis(this);
    juce::MessageManager::callAsync([safeThis, indexed = std::move(indexed), roots]() mutable {
        if (safeThis == nullptr)
            return;
        std::erase_if(safeThis->items_, [](const auto& item) { return item.type == ItemType::sample; });
        for (auto& item : indexed)
            item.favorite = safeThis->isFavorite(item.file);
        safeThis->items_.insert(safeThis->items_.end(),
                                std::make_move_iterator(indexed.begin()),
                                std::make_move_iterator(indexed.end()));
        safeThis->scanStatusLabel_.setText(
            tr("browser.indexed", "count", juce::String(static_cast<int>(indexed.size())))
                + "  |  " + juce::String(static_cast<int>(roots.size()))
                + " " + tr("browser.locationsShort"),
            juce::dontSendNotification);
        safeThis->applyFilter();
        safeThis->refreshSelectionActions();
    });
}

void LibraryBrowserComponent::refreshBridgeStatus()
{
    const auto status = WindowsPluginBridge::inspect();
    if (!status.linuxHost) {
        bridgeStatusLabel_.setText(tr("browser.bridgeNative"), juce::dontSendNotification);
        return;
    }
    if (status.yabridgeReady()) {
        bridgeStatusLabel_.setText(tr("browser.bridgeYabridge"), juce::dontSendNotification);
    } else if (status.directBridgeReady()) {
        bridgeStatusLabel_.setText(status.directRuntime == PluginWorkerRuntime::protonViaUmu
                                       ? tr("browser.bridgeProton")
                                       : tr("browser.bridgeWine"),
                                   juce::dontSendNotification);
    } else {
        bridgeStatusLabel_.setText(tr("browser.bridgeUnavailable"),
                                   juce::dontSendNotification);
    }
}

void LibraryBrowserComponent::refreshLocationSelector()
{
    const auto previous = locationSelector_.getSelectedId();
    locationSelector_.clear(juce::dontSendNotification);
    locationSelector_.addItem(tr("browser.allLocations"), 1);
    locationSelector_.addItem(tr("browser.favorites"), 2);
    for (std::size_t index = 0; index < libraryRoots_.size(); ++index) {
        auto label = libraryRoots_[index].getFileName();
        if (label.isEmpty())
            label = libraryRoots_[index].getFullPathName();
        locationSelector_.addItem(label, 100 + static_cast<int>(index));
    }
    const auto maximumId = 100 + static_cast<int>(libraryRoots_.size());
    const auto selected = previous == 1 || previous == 2
                              || (previous >= 100 && previous < maximumId)
                          ? previous : 1;
    locationSelector_.setSelectedId(selected, juce::dontSendNotification);
}

void LibraryBrowserComponent::refreshSelectionActions()
{
    const auto* item = selectedItem();
    const auto sampleSelected = item != nullptr
                                && item->type == ItemType::sample
                                && item->file.existsAsFile();
    favoriteButton_.setEnabled(sampleSelected);
    removeFolderButton_.setEnabled(locationSelector_.getSelectedId() >= 100);
    previewButton_.setEnabled(sampleSelected
                              && static_cast<bool>(samplePreviewStartHandler_));
    favoriteButton_.setButtonText(
        sampleSelected && item->favorite ? tr("browser.unfavorite")
                                         : tr("browser.favorite"));
    const auto previewingSelected = sampleSelected
                                    && previewFile_ == item->file
                                    && samplePreviewIsPlayingHandler_
                                    && samplePreviewIsPlayingHandler_();
    previewButton_.setButtonText(
        tr(previewingSelected ? "browser.stopPreview" : "browser.preview"));
    if (sampleSelected) {
        favoriteButton_.setTooltip(tr("browser.favoriteHelp"));
        previewButton_.setTooltip(item->file.getFullPathName() + "\n"
                                  + formatSampleMetadata(item->sample));
    }
}

LibraryBrowserComponent::Item* LibraryBrowserComponent::selectedItem() noexcept
{
    const auto row = list_.getSelectedRow();
    if (row < 0 || row >= static_cast<int>(filteredIndices_.size()))
        return nullptr;
    const auto index = filteredIndices_[static_cast<std::size_t>(row)];
    return index >= 0 && index < static_cast<int>(items_.size())
               ? &items_[static_cast<std::size_t>(index)] : nullptr;
}

const LibraryBrowserComponent::Item*
LibraryBrowserComponent::selectedItem() const noexcept
{
    const auto row = list_.getSelectedRow();
    if (row < 0 || row >= static_cast<int>(filteredIndices_.size()))
        return nullptr;
    const auto index = filteredIndices_[static_cast<std::size_t>(row)];
    return index >= 0 && index < static_cast<int>(items_.size())
               ? &items_[static_cast<std::size_t>(index)] : nullptr;
}

void LibraryBrowserComponent::toggleSelectedFavorite()
{
    auto* item = selectedItem();
    if (item == nullptr || item->type != ItemType::sample)
        return;
    const auto wasFavorite = favoriteListContains(favoritePaths_, item->file);
    (void) updateFavoriteList(favoritePaths_, item->file, !wasFavorite);
    for (auto& candidate : items_)
        if (candidate.type == ItemType::sample && candidate.file == item->file)
            candidate.favorite = !wasFavorite;
    saveLibrarySettings();
    applyFilter();
}

void LibraryBrowserComponent::toggleSamplePreview()
{
    const auto* item = selectedItem();
    if (item == nullptr || item->type != ItemType::sample
        || !samplePreviewStartHandler_)
        return;
    if (previewFile_ == item->file && samplePreviewIsPlayingHandler_
        && samplePreviewIsPlayingHandler_()) {
        stopSamplePreview();
        return;
    }
    startSamplePreviewAt(0.0);
}

void LibraryBrowserComponent::startSamplePreviewAt(const double startSeconds)
{
    const auto* item = selectedItem();
    if (item == nullptr || item->type != ItemType::sample
        || !samplePreviewStartHandler_)
        return;
    stopSamplePreview();
    const auto result = samplePreviewStartHandler_(item->file, startSeconds);
    if (result.failed()) {
        scanStatusLabel_.setText(result.getErrorMessage(),
                                 juce::dontSendNotification);
        return;
    }
    previewFile_ = item->file;
    refreshSelectionActions();
}

void LibraryBrowserComponent::stopSamplePreview()
{
    if (samplePreviewStopHandler_)
        samplePreviewStopHandler_();
    previewFile_ = {};
    refreshSelectionActions();
}

void LibraryBrowserComponent::refreshWaveformPreview()
{
    const auto* item = selectedItem();
    const auto file = item != nullptr && item->type == ItemType::sample
                              ? item->file
                              : juce::File {};
    if (file == waveformPreviewFile_)
        return;
    waveformPreviewFile_ = file;
    waveformThumbnail_.clear();
    if (waveformPreviewFile_.existsAsFile())
        waveformThumbnail_.setSource(
            new juce::FileInputSource(waveformPreviewFile_));
    if (!waveformPreviewBounds_.isEmpty())
        repaint(waveformPreviewBounds_);
}

juce::Rectangle<int>
LibraryBrowserComponent::waveformContentBounds() const noexcept
{
    auto content = waveformPreviewBounds_.reduced(7, 5);
    content.removeFromTop(19);
    return content;
}

bool LibraryBrowserComponent::isFavorite(const juce::File& file) const
{
    return favoriteListContains(favoritePaths_, file);
}

juce::String LibraryBrowserComponent::normalisedPath(const juce::File& file)
{
    return file.getFullPathName().replaceCharacter('\\', '/').toLowerCase();
}

void LibraryBrowserComponent::loadLibrarySettings()
{
    const auto* settings = properties_.getUserSettings();
    auto roots = juce::StringArray::fromLines(
        settings->getValue("sampleFolders"));
    if (roots.isEmpty()) {
        const auto legacy = settings->getValue("sampleFolder");
        if (legacy.isNotEmpty())
            roots.add(legacy);
    }
    for (const auto& path : roots) {
        const juce::File root(path);
        const auto duplicate = std::find_if(
            libraryRoots_.begin(), libraryRoots_.end(),
            [&root](const auto& current) {
                return normalisedPath(current) == normalisedPath(root);
            });
        if (path.isNotEmpty() && duplicate == libraryRoots_.end())
            libraryRoots_.push_back(root);
    }
    favoritePaths_ = juce::StringArray::fromLines(
        settings->getValue("sampleFavorites"));
    favoritePaths_.removeEmptyStrings();
    sortMode_ = juce::jlimit(1, 5,
                             settings->getIntValue("browserSort", 1));
    sampleFilterMode_ = juce::jlimit(
        1, 6, settings->getIntValue("browserSampleFilter", 1));
    autoPreviewButton_.setToggleState(
        settings->getBoolValue("browserAutoPreview", false),
        juce::dontSendNotification);
    previewGainSlider_.setValue(
        juce::jlimit(-48.0, 6.0,
                     settings->getDoubleValue("browserPreviewGainDb", -3.0)),
        juce::dontSendNotification);
}

void LibraryBrowserComponent::saveLibrarySettings()
{
    juce::StringArray roots;
    for (const auto& root : libraryRoots_)
        roots.add(root.getFullPathName());
    auto* settings = properties_.getUserSettings();
    settings->setValue("sampleFolders", roots.joinIntoString("\n"));
    settings->setValue("sampleFavorites", favoritePaths_.joinIntoString("\n"));
    settings->setValue("browserSort", sortMode_);
    settings->setValue("browserSampleFilter", sampleFilterMode_);
    settings->setValue("browserAutoPreview",
                       autoPreviewButton_.getToggleState());
    settings->setValue("browserPreviewGainDb",
                       previewGainSlider_.getValue());
    properties_.saveIfNeeded();
}

void LibraryBrowserComponent::loadSampleIndexCache()
{
    if (!sampleIndexCacheFile_.existsAsFile() || libraryRoots_.empty())
        return;
    const auto samples = restoreSampleIndexCache(
        juce::JSON::parse(sampleIndexCacheFile_), libraryRoots_);
    if (samples.empty())
        return;
    std::erase_if(items_, [](const auto& item) {
        return item.type == ItemType::sample;
    });
    for (const auto& sample : samples) {
        Item item;
        item.type = ItemType::sample;
        item.name = sample.name;
        item.detail = sample.file.getParentDirectory().getFileName()
                      + "  |  " + formatSampleMetadata(sample);
        item.file = sample.file;
        item.libraryRoot = sample.libraryRoot;
        item.favorite = isFavorite(sample.file);
        item.sample = sample;
        items_.push_back(std::move(item));
    }
    scanStatusLabel_.setText(
        tr("browser.cached", "count",
           juce::String(static_cast<int>(samples.size()))),
        juce::dontSendNotification);
    applyFilter();
}

void LibraryBrowserComponent::saveSampleIndexCache(
    const std::vector<SampleMetadata>& samples,
    const std::vector<juce::File>& roots) const
{
    if (sampleIndexCacheFile_ == juce::File {})
        return;
    (void) sampleIndexCacheFile_.replaceWithText(
        juce::JSON::toString(serialiseSampleIndexCache(samples, roots),
                             false));
}

void LibraryBrowserComponent::chooseSampleFolder()
{
    chooser_ = std::make_unique<juce::FileChooser>(tr("browser.chooseFolder"), juce::File {}, "*");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectDirectories,
                          [this](const juce::FileChooser& chooser) {
                              const auto folder = chooser.getResult();
                              if (folder.isDirectory())
                                  scanFolder(folder);
                          });
}

void LibraryBrowserComponent::choosePluginFile()
{
    chooser_ = std::make_unique<juce::FileChooser>(
        tr("browser.choosePlugin"), juce::File {}, "*.vst3;*.dll;*.so;*.vst");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectDirectories,
                          [this](const juce::FileChooser& chooser) {
                              const auto pluginFile = chooser.getResult();
                              if (pluginFile.exists())
                                  probePlugin(pluginFile, false);
                          });
}

void LibraryBrowserComponent::scanFolder(const juce::File& folder)
{
    const auto duplicate = std::find_if(
        libraryRoots_.begin(), libraryRoots_.end(),
        [&folder](const auto& current) {
            return normalisedPath(current) == normalisedPath(folder);
        });
    if (duplicate == libraryRoots_.end())
        libraryRoots_.push_back(folder);
    saveLibrarySettings();
    refreshLocationSelector();
    rescanLibraries();
}

void LibraryBrowserComponent::rescanLibraries()
{
    signalThreadShouldExit();
    stopThread(3'000);
    {
        const juce::ScopedLock lock(scanLock_);
        scanRoots_ = libraryRoots_;
        pluginProbeFile_ = {};
        validatePluginProcessing_ = false;
    }
    if (scanRoots_.empty()) {
        std::erase_if(items_, [](const auto& item) {
            return item.type == ItemType::sample;
        });
        scanStatusLabel_.setText(tr("browser.noLocations"),
                                 juce::dontSendNotification);
        if (sampleIndexCacheFile_.existsAsFile())
            (void) sampleIndexCacheFile_.deleteFile();
        applyFilter();
        return;
    }
    scanStatusLabel_.setText(tr("browser.indexing"), juce::dontSendNotification);
    startThread(juce::Thread::Priority::low);
}

void LibraryBrowserComponent::removeSelectedLibrary()
{
    const auto selection = locationSelector_.getSelectedId();
    if (selection < 100)
        return;
    const auto index = static_cast<std::size_t>(selection - 100);
    if (index >= libraryRoots_.size())
        return;
    const auto removedRoot = libraryRoots_[index];
    if (previewFile_.isAChildOf(removedRoot))
        stopSamplePreview();
    libraryRoots_.erase(libraryRoots_.begin()
                        + static_cast<std::ptrdiff_t>(index));
    saveLibrarySettings();
    locationSelector_.setSelectedId(1, juce::dontSendNotification);
    refreshLocationSelector();
    rescanLibraries();
}

void LibraryBrowserComponent::probePlugin(const juce::File& pluginFile, const bool validateProcessing)
{
    signalThreadShouldExit();
    stopThread(3'000);
    {
        const juce::ScopedLock lock(scanLock_);
        scanRoots_.clear();
        pluginProbeFile_ = pluginFile;
        validatePluginProcessing_ = validateProcessing;
    }
    scanStatusLabel_.setText(tr(validateProcessing ? "browser.validatingPlugin" : "browser.probingPlugin"),
                             juce::dontSendNotification);
    startThread(juce::Thread::Priority::low);
}

void LibraryBrowserComponent::applyFilter()
{
    const auto query = searchEditor_.getText().trim().toLowerCase();
    auto selectedIndex = -1;
    const auto selectedRow = list_.getSelectedRow();
    if (selectedRow >= 0
        && selectedRow < static_cast<int>(filteredIndices_.size()))
        selectedIndex = filteredIndices_[static_cast<std::size_t>(selectedRow)];
    filteredIndices_.clear();
    for (int index = 0; index < static_cast<int>(items_.size()); ++index) {
        const auto& item = items_[static_cast<std::size_t>(index)];
        auto searchMatches = query.isEmpty();
        if (!searchMatches && item.type == ItemType::sample)
            searchMatches = sampleMatchesSearch(item.sample, query);
        if (!searchMatches && item.type != ItemType::sample) {
            juce::StringArray tokens;
            tokens.addTokens(query, " \t\r\n", "\"'");
            tokens.removeEmptyStrings();
            const auto haystack = (item.name + " " + item.detail).toLowerCase();
            searchMatches = true;
            for (const auto& token : tokens)
                searchMatches = searchMatches && haystack.contains(token);
        }
        if (categoryMatches(item) && sampleFilterMatches(item)
            && locationMatches(item) && searchMatches)
            filteredIndices_.push_back(index);
    }
    if (sortMode_ >= 2 && sortMode_ <= 5) {
        const auto mode = static_cast<SampleSortMode>(sortMode_ - 2);
        std::stable_sort(
            filteredIndices_.begin(), filteredIndices_.end(),
            [this, mode](const int leftIndex, const int rightIndex) {
                const auto& left = items_[static_cast<std::size_t>(leftIndex)];
                const auto& right = items_[static_cast<std::size_t>(rightIndex)];
                if (left.type == ItemType::sample
                    && right.type == ItemType::sample)
                    return sampleSortsBefore(left.sample, right.sample, mode);
                if (mode != SampleSortMode::name
                    && left.type != right.type)
                    return left.type == ItemType::sample;
                const auto order = left.name.compareIgnoreCase(right.name);
                if (order != 0)
                    return order < 0;
                return leftIndex < rightIndex;
            });
    }
    list_.updateContent();
    const auto selectedFiltered = std::find(filteredIndices_.begin(),
                                            filteredIndices_.end(),
                                            selectedIndex);
    if (selectedFiltered != filteredIndices_.end())
        list_.selectRow(static_cast<int>(std::distance(
            filteredIndices_.begin(), selectedFiltered)), false, false);
    else
        list_.deselectAllRows();
    list_.repaint();
    refreshSelectionActions();
    refreshWaveformPreview();
}

bool LibraryBrowserComponent::categoryMatches(const Item& item) const
{
    switch (categorySelector_.getSelectedId()) {
        case 2: return item.type == ItemType::sample;
        // A factory graph is filed by what it does as well as by what it is.
        case 3: return item.type == ItemType::instrument
                       || (item.type == ItemType::graphPreset
                           && item.soundSource);
        case 4: return item.type == ItemType::effect
                       || (item.type == ItemType::graphPreset
                           && !item.soundSource);
        case 5: return item.type == ItemType::plugin;
        case 6: return item.type == ItemType::instrument
                       || item.type == ItemType::effect
                       || item.type == ItemType::plugin;
        case 7: return item.type == ItemType::graphPreset
                       || item.type == ItemType::userGraphPreset;
        default: return true;
    }
}

bool LibraryBrowserComponent::sampleFilterMatches(const Item& item) const
{
    if (sampleFilterMode_ <= 1)
        return true;
    if (item.type != ItemType::sample)
        return false;
    return sampleMatchesFilter(
        item.sample, static_cast<SampleFilterMode>(sampleFilterMode_ - 1));
}

bool LibraryBrowserComponent::locationMatches(const Item& item) const
{
    const auto selection = locationSelector_.getSelectedId();
    if (selection == 2)
        return item.type == ItemType::sample && item.favorite;
    if (selection >= 100) {
        const auto index = static_cast<std::size_t>(selection - 100);
        return item.type == ItemType::sample && index < libraryRoots_.size()
               && normalisedPath(item.libraryRoot)
                      == normalisedPath(libraryRoots_[index]);
    }
    return true;
}

} // namespace cydaw::app
