#include "SampleExplorerComponent.h"

#include "CyDawLookAndFeel.h"
#include "LibraryDragPayload.h"
#include "Localization.h"

#include <algorithm>
#include <cmath>

namespace cydaw::app {

namespace {
constexpr auto background = 0xff17191c;
constexpr auto panel = 0xff202328;
constexpr auto border = 0xff3f444a;
constexpr auto textColour = 0xffe8ebee;
constexpr auto dimText = 0xffaeb5bd;
constexpr auto accent = 0xffff8a00;
constexpr auto waveColour = 0xff4fd3ae;

juce::String formatLength(const double seconds)
{
    if (seconds <= 0.0)
        return "-";
    if (seconds < 10.0)
        return juce::String(seconds, 2) + " s";
    if (seconds < 60.0)
        return juce::String(seconds, 1) + " s";
    const auto minutes = static_cast<int>(seconds / 60.0);
    const auto rest = static_cast<int>(seconds) % 60;
    return juce::String(minutes) + ":" + juce::String(rest).paddedLeft('0', 2);
}

juce::String formatRate(const double rate)
{
    return rate > 0.0 ? juce::String(rate / 1000.0, rate == 44'100.0 ? 1 : 0) + " kHz"
                      : juce::String("-");
}

juce::String formatDate(const juce::int64 ms)
{
    return ms > 0 ? juce::Time(ms).formatted("%Y-%m-%d") : juce::String("-");
}
} // namespace

// The header's right-click opens the column's filter instead of JUCE's
// column chooser.
class SampleExplorerComponent::FilterHeader final : public juce::TableHeaderComponent {
public:
    std::function<void(int)> onFilter;

private:
    void showColumnChooserMenu(const int columnIdClicked) override
    {
        if (onFilter)
            onFilter(columnIdClicked);
    }
};

// The selected sample drawn from its file, with the preview's playhead;
// a click starts the preview there.
class SampleExplorerComponent::WaveformView final : public juce::Component {
public:
    explicit WaveformView(SampleExplorerComponent& owner) : owner_(owner)
    {
        formatManager_.registerBasicFormats();
    }

    void setFile(const juce::File& file)
    {
        file_ = file;
        if (file == juce::File {})
            thumbnail_.clear();
        else
            thumbnail_.setSource(new juce::FileInputSource(file));
        repaint();
    }

    void setPlayhead(const double seconds, const bool playing)
    {
        if (std::abs(playhead_ - seconds) > 0.005 || playing != playing_) {
            playhead_ = seconds;
            playing_ = playing;
            repaint();
        }
    }

    void paint(juce::Graphics& graphics) override
    {
        const auto area = getLocalBounds().toFloat();
        graphics.setColour(juce::Colour(0xff101317));
        graphics.fillRoundedRectangle(area, 5.0f);
        const auto length = thumbnail_.getTotalLength();
        if (file_ == juce::File {} || length <= 0.0) {
            graphics.setColour(juce::Colour(dimText));
            graphics.setFont(juce::FontOptions(12.0f));
            graphics.drawFittedText(tr("samples.waveformEmpty"), getLocalBounds(),
                                    juce::Justification::centred, 2);
            return;
        }
        graphics.setColour(juce::Colour(waveColour));
        thumbnail_.drawChannels(graphics, getLocalBounds().reduced(4, 6), 0.0, length, 1.0f);
        if (playing_) {
            const auto x = area.getX() + 4.0f
                           + static_cast<float>(playhead_ / length) * (area.getWidth() - 8.0f);
            graphics.setColour(juce::Colour(accent));
            graphics.drawLine(x, area.getY() + 2.0f, x, area.getBottom() - 2.0f, 1.5f);
        }
        graphics.setColour(juce::Colour(border));
        graphics.drawRoundedRectangle(area.reduced(0.5f), 5.0f, 1.0f);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        const auto length = thumbnail_.getTotalLength();
        if (length <= 0.0 || getWidth() <= 8)
            return;
        const auto fraction = juce::jlimit(
            0.0, 1.0, static_cast<double>(event.x - 4) / static_cast<double>(getWidth() - 8));
        owner_.startPreview(fraction * length);
    }

private:
    SampleExplorerComponent& owner_;
    juce::File file_;
    juce::AudioFormatManager formatManager_;
    juce::AudioThumbnailCache thumbnailCache_ { 16 };
    juce::AudioThumbnail thumbnail_ { 512, formatManager_, thumbnailCache_ };
    double playhead_ {};
    bool playing_ {};
};

SampleExplorerComponent::SampleExplorerComponent(SampleLibrary& library)
    : library_(library)
{
    setWantsKeyboardFocus(true);
    waveform_ = std::make_unique<WaveformView>(*this);
    for (auto* component : { static_cast<juce::Component*>(&titleLabel_),
                             static_cast<juce::Component*>(&searchEditor_),
                             static_cast<juce::Component*>(&addFolderButton_),
                             static_cast<juce::Component*>(&rescanButton_),
                             static_cast<juce::Component*>(&autoRescanButton_),
                             static_cast<juce::Component*>(&rescanIntervalBox_),
                             static_cast<juce::Component*>(&classifierButton_),
                             static_cast<juce::Component*>(&classifyNowButton_),
                             static_cast<juce::Component*>(&cacheButton_),
                             static_cast<juce::Component*>(&statusLabel_),
                             static_cast<juce::Component*>(&filterSummaryLabel_),
                             static_cast<juce::Component*>(&clearFiltersButton_),
                             static_cast<juce::Component*>(&table_),
                             static_cast<juce::Component*>(waveform_.get()),
                             static_cast<juce::Component*>(&playButton_),
                             static_cast<juce::Component*>(&stopButton_),
                             static_cast<juce::Component*>(&autoPreviewButton_),
                             static_cast<juce::Component*>(&previewGainSlider_),
                             static_cast<juce::Component*>(&detailsLabel_),
                             static_cast<juce::Component*>(&categoryCaption_),
                             static_cast<juce::Component*>(&categoryBox_),
                             static_cast<juce::Component*>(&typeCaption_),
                             static_cast<juce::Component*>(&typeBox_),
                             static_cast<juce::Component*>(&tagsCaption_),
                             static_cast<juce::Component*>(&tagsEditor_),
                             static_cast<juce::Component*>(&bpmCaption_),
                             static_cast<juce::Component*>(&bpmEditor_),
                             static_cast<juce::Component*>(&favouriteButton_),
                             static_cast<juce::Component*>(&newTrackButton_),
                             static_cast<juce::Component*>(&insertButton_) })
        addAndMakeVisible(component);
    for (auto& button : ratingButtons_)
        addAndMakeVisible(button);

    titleLabel_.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    titleLabel_.setColour(juce::Label::textColourId, juce::Colour(textColour));
    searchEditor_.setMultiLine(false);
    searchEditor_.setReturnKeyStartsNewLine(false);
    searchEditor_.onTextChange = [this] {
        search_ = searchEditor_.getText().trim();
        rebuildRows();
    };
    searchEditor_.onEscapeKey = [this] { searchEditor_.clear(); };

    addFolderButton_.onClick = [this] { chooseFolder(); };
    rescanButton_.onClick = [this] { library_.rescan(false); refreshStatus(); };
    autoRescanButton_.onClick = [this] {
        library_.setAutoRescanMinutes(autoRescanButton_.getToggleState()
                                          ? std::max(1, rescanIntervalBox_.getSelectedId())
                                          : 0);
    };
    for (const auto minutes : { 1, 5, 10, 30, 60 })
        rescanIntervalBox_.addItem(juce::String(minutes) + " min", minutes);
    rescanIntervalBox_.onChange = [this] {
        if (autoRescanButton_.getToggleState())
            library_.setAutoRescanMinutes(rescanIntervalBox_.getSelectedId());
    };
    classifierButton_.onClick = [this] {
        library_.setClassifierEnabled(classifierButton_.getToggleState());
        refreshStatus();
    };
    classifyNowButton_.onClick = [this] { library_.classify(false); refreshStatus(); };
    cacheButton_.onClick = [this] { chooseCacheFolder(); };
    statusLabel_.setJustificationType(juce::Justification::centredRight);
    statusLabel_.setColour(juce::Label::textColourId, juce::Colour(dimText));
    statusLabel_.setFont(juce::FontOptions(11.0f));
    filterSummaryLabel_.setColour(juce::Label::textColourId, juce::Colour(dimText));
    filterSummaryLabel_.setFont(juce::FontOptions(11.0f));
    clearFiltersButton_.onClick = [this] { clearFilters(); };

    auto header = std::make_unique<FilterHeader>();
    header->onFilter = [this](const int columnId) { showFilterMenu(columnId); };
    table_.setHeader(std::move(header));
    table_.setModel(this);
    table_.setRowHeight(24);
    table_.setMultipleSelectionEnabled(false);
    table_.setOutlineThickness(0);
    table_.setColour(juce::ListBox::backgroundColourId, juce::Colour(panel));
    table_.getHeader().setStretchToFitActive(true);
    const auto flags = juce::TableHeaderComponent::visible
                       | juce::TableHeaderComponent::resizable
                       | juce::TableHeaderComponent::sortable;
    table_.getHeader().addColumn("*", favouriteColumn, 26, 26, 26,
                                 juce::TableHeaderComponent::visible);
    table_.getHeader().addColumn("~", waveColumn, 72, 40, 160,
                                 juce::TableHeaderComponent::visible
                                     | juce::TableHeaderComponent::resizable);
    table_.getHeader().addColumn("Nom", nameColumn, 260, 120, 600, flags);
    table_.getHeader().addColumn("Categorie", categoryColumn, 90, 60, 160, flags);
    table_.getHeader().addColumn("Type", typeColumn, 76, 60, 120, flags);
    table_.getHeader().addColumn("Tags", tagsColumn, 180, 80, 400, flags);
    table_.getHeader().addColumn("Duree", lengthColumn, 70, 50, 100, flags);
    table_.getHeader().addColumn("BPM", bpmColumn, 54, 40, 80, flags);
    table_.getHeader().addColumn("Tonalite", keyColumn, 64, 40, 90, flags);
    table_.getHeader().addColumn("Canaux", channelsColumn, 62, 40, 90, flags);
    table_.getHeader().addColumn("Freq.", rateColumn, 70, 50, 90, flags);
    table_.getHeader().addColumn("Dossier", folderColumn, 140, 60, 400, flags);
    table_.getHeader().addColumn("Modifie", modifiedColumn, 90, 70, 120, flags);
    table_.getHeader().setSortColumnId(nameColumn, true);

    playButton_.onClick = [this] {
        if (isPreviewing_ && isPreviewing_())
            stopPreview();
        else
            startPreview();
    };
    stopButton_.onClick = [this] { stopPreview(); };
    autoPreviewButton_.setToggleState(true, juce::dontSendNotification);
    previewGainSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    previewGainSlider_.setRange(-36.0, 6.0, 0.1);
    previewGainSlider_.setValue(-6.0, juce::dontSendNotification);
    previewGainSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 18);
    previewGainSlider_.setTextValueSuffix(" dB");
    previewGainSlider_.onValueChange = [this] {
        if (previewGain_)
            previewGain_(static_cast<float>(previewGainSlider_.getValue()));
    };
    detailsLabel_.setColour(juce::Label::textColourId, juce::Colour(dimText));
    detailsLabel_.setFont(juce::FontOptions(11.0f));
    detailsLabel_.setJustificationType(juce::Justification::topLeft);
    for (auto* caption : { &categoryCaption_, &typeCaption_, &tagsCaption_, &bpmCaption_ }) {
        caption->setColour(juce::Label::textColourId, juce::Colour(dimText));
        caption->setFont(juce::FontOptions(10.0f, juce::Font::bold));
    }
    categoryBox_.setEditableText(true);
    for (const auto& category : SampleLibrary::categoryVocabulary())
        categoryBox_.addItem(category, categoryBox_.getNumItems() + 1);
    categoryBox_.onChange = [this] { commitCategory(); };
    typeBox_.addItem("one-shot", 1);
    typeBox_.addItem("loop", 2);
    typeBox_.addItem("long", 3);
    typeBox_.onChange = [this] { commitType(); };
    tagsEditor_.setMultiLine(false);
    tagsEditor_.onReturnKey = [this] { commitTags(); };
    tagsEditor_.onFocusLost = [this] { commitTags(); };
    bpmEditor_.setMultiLine(false);
    bpmEditor_.setInputRestrictions(6, "0123456789.,");
    bpmEditor_.onReturnKey = [this] { commitBpm(); };
    bpmEditor_.onFocusLost = [this] { commitBpm(); };
    for (std::size_t index = 0; index < ratingButtons_.size(); ++index) {
        ratingButtons_[index].setButtonText(juce::String::charToString(0x2605));
        ratingButtons_[index].onClick = [this, index] {
            setRating(static_cast<int>(index + 1));
        };
        CyDawLookAndFeel::setFlat(ratingButtons_[index]);
    }
    favouriteButton_.setClickingTogglesState(true);
    favouriteButton_.onClick = [this] {
        if (selectedPath_.isNotEmpty())
            library_.setFavourite(selectedPath_, favouriteButton_.getToggleState());
    };
    newTrackButton_.onClick = [this] { openSelected(true); };
    insertButton_.onClick = [this] { openSelected(false); };

    library_.addListener(this);
    refreshTranslations();
    refreshFolderChips();
    rebuildRows();
    refreshStatus();
    startTimerHz(30);
}

SampleExplorerComponent::~SampleExplorerComponent()
{
    stopTimer();
    library_.removeListener(this);
}

void SampleExplorerComponent::setPreviewHandlers(
    std::function<juce::Result(const juce::File&, double)> startHandler,
    std::function<void()> stopHandler, std::function<bool()> isPlayingHandler,
    std::function<double()> positionHandler, std::function<void(float)> gainHandler)
{
    startPreview_ = std::move(startHandler);
    stopPreview_ = std::move(stopHandler);
    isPreviewing_ = std::move(isPlayingHandler);
    previewPosition_ = std::move(positionHandler);
    previewGain_ = std::move(gainHandler);
    if (previewGain_)
        previewGain_(static_cast<float>(previewGainSlider_.getValue()));
}

void SampleExplorerComponent::setOpenHandlers(
    std::function<void(const juce::File&)> newTrackHandler,
    std::function<void(const juce::File&)> insertHandler)
{
    openNewTrack_ = std::move(newTrackHandler);
    insertAtPlayhead_ = std::move(insertHandler);
}

void SampleExplorerComponent::refreshTranslations()
{
    titleLabel_.setText(tr("samples.title"), juce::dontSendNotification);
    searchEditor_.setTextToShowWhenEmpty(tr("samples.search"), juce::Colour(dimText));
    addFolderButton_.setButtonText(tr("samples.addFolder"));
    rescanButton_.setButtonText(tr("samples.rescan"));
    autoRescanButton_.setButtonText(tr("samples.autoRescan"));
    classifierButton_.setButtonText(tr("samples.classifier"));
    classifierButton_.setTooltip(tr("samples.classifierHelp"));
    classifyNowButton_.setButtonText(tr("samples.classifyNow"));
    cacheButton_.setButtonText(tr("samples.cache"));
    cacheButton_.setTooltip(tr("samples.cacheHelp"));
    clearFiltersButton_.setButtonText(tr("samples.clearFilters"));
    playButton_.setButtonText(tr("samples.play"));
    stopButton_.setButtonText(tr("samples.stop"));
    autoPreviewButton_.setButtonText(tr("samples.autoPreview"));
    categoryCaption_.setText(tr("samples.category"), juce::dontSendNotification);
    typeCaption_.setText(tr("samples.type"), juce::dontSendNotification);
    tagsCaption_.setText(tr("samples.tags"), juce::dontSendNotification);
    bpmCaption_.setText(tr("samples.bpm"), juce::dontSendNotification);
    favouriteButton_.setButtonText(tr("samples.favourite"));
    newTrackButton_.setButtonText(tr("samples.newTrack"));
    insertButton_.setButtonText(tr("samples.insert"));
    auto& header = table_.getHeader();
    header.setColumnName(nameColumn, tr("samples.column.name"));
    header.setColumnName(categoryColumn, tr("samples.column.category"));
    header.setColumnName(typeColumn, tr("samples.column.type"));
    header.setColumnName(tagsColumn, tr("samples.column.tags"));
    header.setColumnName(lengthColumn, tr("samples.column.length"));
    header.setColumnName(bpmColumn, tr("samples.column.bpm"));
    header.setColumnName(keyColumn, tr("samples.column.key"));
    header.setColumnName(channelsColumn, tr("samples.column.channels"));
    header.setColumnName(rateColumn, tr("samples.column.rate"));
    header.setColumnName(folderColumn, tr("samples.column.folder"));
    header.setColumnName(modifiedColumn, tr("samples.column.modified"));
    autoRescanButton_.setToggleState(library_.autoRescanMinutes() > 0, juce::dontSendNotification);
    rescanIntervalBox_.setSelectedId(library_.autoRescanMinutes() > 0
                                         ? library_.autoRescanMinutes() : 10,
                                     juce::dontSendNotification);
    classifierButton_.setToggleState(library_.classifierEnabled(), juce::dontSendNotification);
    refreshFilterSummary();
    refreshStatus();
}

juce::String SampleExplorerComponent::selectedPath() const
{
    return selectedPath_;
}

void SampleExplorerComponent::setSearchText(const juce::String& text)
{
    searchEditor_.setText(text, juce::dontSendNotification);
    search_ = text.trim();
    rebuildRows();
}

void SampleExplorerComponent::setColumnFilter(const int columnId, const juce::StringArray& values)
{
    filters_.erase(columnId);
    for (const auto& value : values)
        filters_[columnId].insert(value);
    if (filters_.contains(columnId) && filters_[columnId].empty())
        filters_.erase(columnId);
    rebuildRows();
}

// ------------------------------------------------------------------ layout

void SampleExplorerComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(background));
    auto area = getLocalBounds().reduced(6);
    const auto bottom = area.removeFromBottom(150);
    graphics.setColour(juce::Colour(panel));
    graphics.fillRoundedRectangle(bottom.toFloat(), 6.0f);
    graphics.setColour(juce::Colour(border));
    graphics.drawRoundedRectangle(bottom.toFloat().reduced(0.5f), 6.0f, 1.0f);
}

void SampleExplorerComponent::resized()
{
    auto area = getLocalBounds().reduced(6);
    auto top = area.removeFromTop(30);
    titleLabel_.setBounds(top.removeFromLeft(90));
    searchEditor_.setBounds(top.removeFromLeft(std::min(280, top.getWidth() / 3)).reduced(0, 3));
    top.removeFromLeft(8);
    const auto place = [&top](juce::Component& component, const int width) {
        component.setBounds(top.removeFromLeft(width).reduced(2, 3));
    };
    place(addFolderButton_, 96);
    place(rescanButton_, 88);
    place(autoRescanButton_, 64);
    place(rescanIntervalBox_, 74);
    place(classifierButton_, 120);
    place(classifyNowButton_, 84);
    place(cacheButton_, 76);
    statusLabel_.setBounds(top);

    area.removeFromTop(4);
    auto chips = area.removeFromTop(24);
    for (auto& chip : folderChips_) {
        const auto width = std::min(220, chip->getBestWidthForHeight(20) + 14);
        chip->setBounds(chips.removeFromLeft(width).reduced(2, 1));
        chips.removeFromLeft(2);
    }
    area.removeFromTop(2);
    auto filters = area.removeFromTop(22);
    clearFiltersButton_.setBounds(filters.removeFromRight(110).reduced(2, 1));
    filterSummaryLabel_.setBounds(filters);

    auto bottom = area.removeFromBottom(150).reduced(8, 6);
    area.removeFromBottom(4);
    table_.setBounds(area);

    auto left = bottom.removeFromLeft(std::max(300, bottom.getWidth() / 2));
    auto transport = left.removeFromBottom(26);
    playButton_.setBounds(transport.removeFromLeft(70).reduced(2, 1));
    stopButton_.setBounds(transport.removeFromLeft(60).reduced(2, 1));
    autoPreviewButton_.setBounds(transport.removeFromLeft(110).reduced(2, 1));
    previewGainSlider_.setBounds(transport.reduced(2, 0));
    left.removeFromBottom(4);
    waveform_->setBounds(left);

    bottom.removeFromLeft(10);
    auto details = bottom;
    auto actions = details.removeFromBottom(26);
    newTrackButton_.setBounds(actions.removeFromLeft(140).reduced(2, 1));
    insertButton_.setBounds(actions.removeFromLeft(190).reduced(2, 1));
    favouriteButton_.setBounds(actions.removeFromLeft(90).reduced(2, 1));
    for (auto& button : ratingButtons_)
        button.setBounds(actions.removeFromLeft(22).reduced(1, 1));
    details.removeFromBottom(4);
    auto fields = details.removeFromBottom(44);
    auto categoryArea = fields.removeFromLeft(std::max(120, fields.getWidth() / 4));
    categoryCaption_.setBounds(categoryArea.removeFromTop(16));
    categoryBox_.setBounds(categoryArea.reduced(2, 2));
    fields.removeFromLeft(6);
    auto typeArea = fields.removeFromLeft(std::max(100, fields.getWidth() / 4));
    typeCaption_.setBounds(typeArea.removeFromTop(16));
    typeBox_.setBounds(typeArea.reduced(2, 2));
    fields.removeFromLeft(6);
    auto bpmArea = fields.removeFromRight(72);
    bpmCaption_.setBounds(bpmArea.removeFromTop(16));
    bpmEditor_.setBounds(bpmArea.reduced(2, 2));
    fields.removeFromRight(6);
    tagsCaption_.setBounds(fields.removeFromTop(16));
    tagsEditor_.setBounds(fields.reduced(2, 2));
    detailsLabel_.setBounds(details);
}

bool SampleExplorerComponent::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey) {
        playButton_.triggerClick();
        return true;
    }
    if (key == juce::KeyPress::returnKey) {
        openSelected(true);
        return true;
    }
    return false;
}

// ------------------------------------------------------------------- rows

const SampleRecord* SampleExplorerComponent::recordAt(const int row) const
{
    if (row < 0 || static_cast<std::size_t>(row) >= rows_.size())
        return nullptr;
    const auto index = rows_[static_cast<std::size_t>(row)];
    return index < library_.samples().size() ? &library_.samples()[index] : nullptr;
}

juce::String SampleExplorerComponent::bucketFor(const int columnId,
                                                const SampleRecord& record) const
{
    switch (columnId) {
        case categoryColumn:
            return record.category.isNotEmpty() ? record.category : "-";
        case typeColumn:
            return record.type.isNotEmpty() ? record.type : "-";
        case keyColumn:
            return record.key.isNotEmpty() ? record.key : "-";
        case lengthColumn:
            return record.length < 1.0 ? "< 1 s"
                   : record.length < 5.0 ? "1 - 5 s"
                   : record.length < 30.0 ? "5 - 30 s"
                   : record.length < 120.0 ? "30 s - 2 min" : "> 2 min";
        case bpmColumn:
            return record.bpm <= 0.0 ? "-"
                   : record.bpm < 90.0 ? "< 90"
                   : record.bpm < 110.0 ? "90 - 110"
                   : record.bpm < 130.0 ? "110 - 130"
                   : record.bpm < 150.0 ? "130 - 150" : "> 150";
        case channelsColumn:
            return record.channels == 1 ? "mono"
                   : record.channels == 2 ? "stereo"
                   : juce::String(record.channels) + " ch";
        case rateColumn:
            return formatRate(record.sampleRate);
        case folderColumn:
            return juce::File(record.root).getFileName();
        case favouriteColumn:
            return record.favourite ? "*" : "-";
        default:
            return {};
    }
}

bool SampleExplorerComponent::passesFilters(const SampleRecord& record) const
{
    if (!folderFilter_.empty() && !folderFilter_.contains(record.root))
        return false;
    for (const auto& [columnId, values] : filters_) {
        if (values.empty())
            continue;
        if (columnId == tagsColumn) {
            auto any = false;
            for (const auto& tag : record.tags)
                if (values.contains(tag))
                    any = true;
            if (!any)
                return false;
        } else if (!values.contains(bucketFor(columnId, record))) {
            return false;
        }
    }
    if (search_.isNotEmpty()) {
        juce::StringArray words;
        words.addTokens(search_.toLowerCase(), " ", "");
        const auto haystack = (record.name + " " + record.category + " "
                               + record.tags.joinIntoString(" ") + " " + record.type + " "
                               + record.key + " " + juce::File(record.root).getFileName())
                                  .toLowerCase();
        for (const auto& word : words)
            if (word.isNotEmpty() && !haystack.contains(word))
                return false;
    }
    return true;
}

void SampleExplorerComponent::rebuildRows()
{
    rows_.clear();
    const auto& samples = library_.samples();
    rows_.reserve(samples.size());
    for (std::size_t index = 0; index < samples.size(); ++index)
        if (passesFilters(samples[index]))
            rows_.push_back(index);
    const auto column = sortColumn_;
    const auto forwards = sortForwards_;
    std::stable_sort(rows_.begin(), rows_.end(), [&samples, column, forwards](const auto a,
                                                                              const auto b) {
        const auto& left = samples[a];
        const auto& right = samples[b];
        auto order = 0;
        switch (column) {
            case favouriteColumn: order = (right.favourite ? 1 : 0) - (left.favourite ? 1 : 0); break;
            case categoryColumn: order = left.category.compareIgnoreCase(right.category); break;
            case typeColumn: order = left.type.compareIgnoreCase(right.type); break;
            case tagsColumn: order = left.tags.joinIntoString(",").compareIgnoreCase(
                                 right.tags.joinIntoString(",")); break;
            case lengthColumn: order = left.length < right.length ? -1 : left.length > right.length ? 1 : 0; break;
            case bpmColumn: order = left.bpm < right.bpm ? -1 : left.bpm > right.bpm ? 1 : 0; break;
            case keyColumn: order = left.key.compareIgnoreCase(right.key); break;
            case channelsColumn: order = left.channels - right.channels; break;
            case rateColumn: order = left.sampleRate < right.sampleRate ? -1 : left.sampleRate > right.sampleRate ? 1 : 0; break;
            case folderColumn: order = left.root.compareIgnoreCase(right.root); break;
            case modifiedColumn: order = left.modified < right.modified ? -1 : left.modified > right.modified ? 1 : 0; break;
            default: break;
        }
        if (order == 0)
            order = left.name.compareNatural(right.name);
        return forwards ? order < 0 : order > 0;
    });
    table_.updateContent();
    // The selection follows the sample, not the row.
    if (selectedPath_.isNotEmpty()) {
        auto row = -1;
        for (std::size_t index = 0; index < rows_.size(); ++index)
            if (samples[rows_[index]].path == selectedPath_)
                row = static_cast<int>(index);
        if (row >= 0)
            table_.selectRow(row, true, true);
        else
            table_.deselectAllRows();
    }
    refreshFilterSummary();
    refreshStatus();
    table_.repaint();
}

int SampleExplorerComponent::getNumRows()
{
    return static_cast<int>(rows_.size());
}

void SampleExplorerComponent::paintRowBackground(juce::Graphics& graphics, const int row,
                                                 const int width, const int height,
                                                 const bool selected)
{
    graphics.setColour(selected ? juce::Colour(0xff34405a)
                                : row % 2 == 0 ? juce::Colour(panel) : juce::Colour(0xff23272c));
    graphics.fillRect(0, 0, width, height);
}

void SampleExplorerComponent::paintCell(juce::Graphics& graphics, const int row,
                                        const int columnId, const int width, const int height,
                                        bool)
{
    const auto* record = recordAt(row);
    if (record == nullptr)
        return;
    const auto area = juce::Rectangle<int>(0, 0, width, height).reduced(4, 0);
    if (columnId == waveColumn) {
        if (record->overview.empty())
            return;
        const auto cell = area.toFloat().reduced(0.0f, 3.0f);
        const auto bins = static_cast<float>(record->overview.size());
        graphics.setColour(juce::Colour(waveColour).withAlpha(0.85f));
        for (std::size_t bin = 0; bin < record->overview.size(); ++bin) {
            const auto x = cell.getX() + static_cast<float>(bin) / bins * cell.getWidth();
            const auto amplitude = static_cast<float>(record->overview[bin]) / 255.0f;
            const auto half = std::max(0.5f, amplitude * cell.getHeight() * 0.5f);
            graphics.drawVerticalLine(juce::roundToInt(x), cell.getCentreY() - half,
                                      cell.getCentreY() + half);
        }
        return;
    }
    if (columnId == favouriteColumn) {
        graphics.setColour(record->favourite ? juce::Colour(accent) : juce::Colour(0xff4d535a));
        graphics.setFont(juce::FontOptions(13.0f));
        graphics.drawText(juce::String::charToString(record->favourite ? 0x2605 : 0x2606),
                          area, juce::Justification::centred);
        return;
    }
    juce::String text;
    auto colour = juce::Colour(textColour);
    switch (columnId) {
        case nameColumn: text = record->name; break;
        case categoryColumn:
            text = record->category;
            colour = record->classifier == "clap" ? juce::Colour(0xff9fd8ff) : juce::Colour(textColour);
            break;
        case typeColumn: text = record->type; colour = juce::Colour(dimText); break;
        case tagsColumn: text = record->tags.joinIntoString(", "); colour = juce::Colour(dimText); break;
        case lengthColumn: text = formatLength(record->length); break;
        case bpmColumn:
            text = record->bpm > 0.0 ? juce::String(record->bpm, 0) : "-";
            if (record->bpmSource == "detected" || record->bpmSource == "length")
                colour = juce::Colour(0xff9fd8ff);
            break;
        case keyColumn:
            text = record->key.isNotEmpty() ? record->key : "-";
            if (record->keySource == "detected")
                colour = juce::Colour(0xff9fd8ff);
            break;
        case channelsColumn: text = bucketFor(channelsColumn, *record); colour = juce::Colour(dimText); break;
        case rateColumn: text = formatRate(record->sampleRate); colour = juce::Colour(dimText); break;
        case folderColumn: text = juce::File(record->root).getFileName(); colour = juce::Colour(dimText); break;
        case modifiedColumn: text = formatDate(record->modified); colour = juce::Colour(dimText); break;
        default: break;
    }
    graphics.setColour(colour);
    graphics.setFont(juce::FontOptions(12.0f));
    graphics.drawText(text, area, juce::Justification::centredLeft, true);
}

void SampleExplorerComponent::cellClicked(const int row, const int columnId,
                                          const juce::MouseEvent& event)
{
    const auto* record = recordAt(row);
    if (record == nullptr)
        return;
    if (columnId == favouriteColumn && !event.mods.isPopupMenu())
        library_.setFavourite(record->path, !record->favourite);
    if (event.mods.isPopupMenu()) {
        juce::PopupMenu menu;
        menu.addItem(1, tr("samples.newTrack"));
        menu.addItem(2, tr("samples.insert"));
        menu.addItem(3, record->favourite ? tr("samples.unfavourite") : tr("samples.favourite"));
        menu.addSeparator();
        menu.addItem(4, tr("samples.reveal"));
        const auto path = record->path;
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&table_),
                           [this, path](const int result) {
                               const auto* chosen = library_.find(path);
                               if (chosen == nullptr)
                                   return;
                               if (result == 1 && openNewTrack_)
                                   openNewTrack_(chosen->file());
                               else if (result == 2 && insertAtPlayhead_)
                                   insertAtPlayhead_(chosen->file());
                               else if (result == 3)
                                   library_.setFavourite(path, !chosen->favourite);
                               else if (result == 4)
                                   chosen->file().revealToUser();
                           });
    }
}

void SampleExplorerComponent::cellDoubleClicked(const int row, int, const juce::MouseEvent&)
{
    if (const auto* record = recordAt(row); record != nullptr && openNewTrack_)
        openNewTrack_(record->file());
}

void SampleExplorerComponent::selectedRowsChanged(const int lastRowSelected)
{
    const auto* record = recordAt(lastRowSelected);
    const auto path = record != nullptr ? record->path : juce::String();
    if (path == selectedPath_)
        return;
    selectedPath_ = path;
    refreshDetails();
    if (record != nullptr && autoPreviewButton_.getToggleState())
        startPreview();
}

void SampleExplorerComponent::sortOrderChanged(const int newSortColumnId, const bool isForwards)
{
    if (newSortColumnId == 0 || newSortColumnId == waveColumn)
        return;
    sortColumn_ = newSortColumnId;
    sortForwards_ = isForwards;
    rebuildRows();
}

juce::var SampleExplorerComponent::getDragSourceDescription(const juce::SparseSet<int>& rows)
{
    if (rows.isEmpty())
        return {};
    const auto* record = recordAt(rows[0]);
    return record != nullptr ? makeSampleDragPayload(record->file()) : juce::var();
}

juce::String SampleExplorerComponent::getCellTooltip(const int row, int)
{
    const auto* record = recordAt(row);
    if (record == nullptr)
        return {};
    return record->path + "\n" + formatLength(record->length) + "  |  "
           + bucketFor(channelsColumn, *record) + "  |  " + formatRate(record->sampleRate)
           + "  |  " + juce::String(record->bitDepth) + " bit  |  peak "
           + juce::String(record->peakDb, 1) + " dB";
}

// ----------------------------------------------------------------- filters

void SampleExplorerComponent::showFilterMenu(const int columnId)
{
    if (columnId == waveColumn || columnId == nameColumn || columnId == modifiedColumn)
        return;
    juce::StringArray values;
    if (columnId == tagsColumn) {
        values = library_.distinctTags();
    } else {
        std::set<juce::String> unique;
        for (const auto& record : library_.samples())
            unique.insert(bucketFor(columnId, record));
        for (const auto& value : unique)
            values.add(value);
    }
    menuValues_ = values;
    juce::PopupMenu menu;
    menu.addSectionHeader(tr("samples.filterBy", "column",
                             table_.getHeader().getColumnName(columnId)));
    menu.addItem(-1, tr("samples.filterAll"), true, !filters_.contains(columnId));
    menu.addSeparator();
    const auto& active = filters_[columnId];
    for (auto index = 0; index < values.size(); ++index)
        menu.addItem(index + 1, values[index], true, active.contains(values[index]));
    if (active.empty())
        filters_.erase(columnId);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&table_.getHeader()),
                       [this, columnId](const int result) {
                           if (result == 0)
                               return;
                           if (result == -1) {
                               filters_.erase(columnId);
                           } else if (result - 1 < menuValues_.size()) {
                               const auto value = menuValues_[result - 1];
                               auto& set = filters_[columnId];
                               if (set.contains(value))
                                   set.erase(value);
                               else
                                   set.insert(value);
                               if (set.empty())
                                   filters_.erase(columnId);
                           }
                           rebuildRows();
                       });
}

void SampleExplorerComponent::clearFilters()
{
    filters_.clear();
    folderFilter_.clear();
    searchEditor_.setText({}, juce::dontSendNotification);
    search_.clear();
    refreshFolderChips();
    rebuildRows();
}

void SampleExplorerComponent::refreshFilterSummary()
{
    juce::StringArray parts;
    for (const auto& [columnId, values] : filters_) {
        if (values.empty())
            continue;
        juce::StringArray listed;
        for (const auto& value : values)
            listed.add(value);
        parts.add(table_.getHeader().getColumnName(columnId) + " = " + listed.joinIntoString(", "));
    }
    if (!folderFilter_.empty()) {
        juce::StringArray listed;
        for (const auto& root : folderFilter_)
            listed.add(juce::File(root).getFileName());
        parts.add(tr("samples.column.folder") + " = " + listed.joinIntoString(", "));
    }
    filterSummaryLabel_.setText(parts.isEmpty() ? tr("samples.filterHint")
                                                : parts.joinIntoString("   |   "),
                                juce::dontSendNotification);
    clearFiltersButton_.setEnabled(!parts.isEmpty() || search_.isNotEmpty());
}

// ----------------------------------------------------------------- folders

void SampleExplorerComponent::refreshFolderChips()
{
    folderChips_.clear();
    for (const auto& folder : library_.folders()) {
        auto chip = std::make_unique<juce::TextButton>(folder.getFileName());
        chip->setTooltip(folder.getFullPathName());
        chip->setClickingTogglesState(true);
        chip->setToggleState(folderFilter_.contains(folder.getFullPathName()),
                             juce::dontSendNotification);
        chip->setColour(juce::TextButton::buttonOnColourId, juce::Colour(accent).withAlpha(0.55f));
        const auto path = folder.getFullPathName();
        chip->onClick = [this, path, button = chip.get()] {
            if (juce::ModifierKeys::currentModifiers.isPopupMenu()) {
                button->setToggleState(!button->getToggleState(), juce::dontSendNotification);
                showFolderMenu(juce::File(path), button);
                return;
            }
            if (button->getToggleState())
                folderFilter_.insert(path);
            else
                folderFilter_.erase(path);
            rebuildRows();
        };
        addAndMakeVisible(*chip);
        folderChips_.push_back(std::move(chip));
    }
    resized();
}

void SampleExplorerComponent::showFolderMenu(const juce::File& folder, juce::Component* target)
{
    juce::PopupMenu menu;
    menu.addSectionHeader(folder.getFullPathName());
    menu.addItem(1, tr("samples.rescanFolder"));
    menu.addItem(2, tr("samples.reveal"));
    menu.addSeparator();
    menu.addItem(3, tr("samples.removeFolder"));
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target),
                       [this, folder](const int result) {
                           if (result == 1)
                               library_.rescan(false);
                           else if (result == 2)
                               folder.revealToUser();
                           else if (result == 3) {
                               folderFilter_.erase(folder.getFullPathName());
                               library_.removeFolder(folder);
                               refreshFolderChips();
                               rebuildRows();
                           }
                       });
}

void SampleExplorerComponent::chooseFolder()
{
    auto chooser = std::make_shared<juce::FileChooser>(tr("samples.chooseFolder"), juce::File {});
    chooser->launchAsync(juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectDirectories,
                         [this, chooser](const juce::FileChooser& result) {
                             const auto folder = result.getResult();
                             if (folder.isDirectory()) {
                                 library_.addFolder(folder);
                                 refreshFolderChips();
                                 refreshStatus();
                             }
                         });
}

void SampleExplorerComponent::chooseCacheFolder()
{
    auto chooser = std::make_shared<juce::FileChooser>(tr("samples.chooseCache"),
                                                       library_.cacheFolder());
    chooser->launchAsync(juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectDirectories,
                         [this, chooser](const juce::FileChooser& result) {
                             const auto folder = result.getResult();
                             if (folder.isDirectory()) {
                                 library_.setCacheFolder(folder);
                                 refreshStatus();
                             }
                         });
}

// ----------------------------------------------------------------- details

void SampleExplorerComponent::refreshDetails()
{
    refreshingDetails_ = true;
    const auto* record = library_.find(selectedPath_);
    if (record == nullptr) {
        waveform_->setFile({});
        detailsLabel_.setText(tr("samples.noSelection"), juce::dontSendNotification);
        categoryBox_.setText({}, juce::dontSendNotification);
        typeBox_.setSelectedId(0, juce::dontSendNotification);
        tagsEditor_.setText({}, juce::dontSendNotification);
        bpmEditor_.setText({}, juce::dontSendNotification);
        favouriteButton_.setToggleState(false, juce::dontSendNotification);
        for (auto& button : ratingButtons_)
            button.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff4d535a));
        refreshingDetails_ = false;
        return;
    }
    waveform_->setFile(record->file());
    detailsLabel_.setText(
        record->name + "." + record->extension + "\n" + record->path + "\n"
            + formatLength(record->length) + "  |  " + bucketFor(channelsColumn, *record)
            + "  |  " + formatRate(record->sampleRate) + "  |  "
            + juce::String(record->bitDepth) + " bit  |  "
            + juce::File::descriptionOfSizeInBytes(record->size) + "\n"
            + tr("samples.peak") + " " + juce::String(record->peakDb, 1) + " dB  |  RMS "
            + juce::String(record->rmsDb, 1) + " dB"
            + (record->bpm > 0.0
                   ? "  |  " + juce::String(record->bpm, 1) + " BPM"
                         + (record->bpmSource == "detected" || record->bpmSource == "length"
                                ? " (" + tr(record->bpmSource == "detected"
                                                ? "samples.bpmDetected" : "samples.bpmFromLength")
                                      + " " + juce::String(record->bpmConfidence * 100.0f, 0) + " %)"
                                : juce::String())
                   : juce::String())
            + (record->key.isNotEmpty()
                   ? "  |  " + record->key
                         + (record->keySource == "detected"
                                ? " (" + tr("samples.bpmDetected") + " "
                                      + juce::String(record->keyConfidence * 100.0f, 0) + " %)"
                                : juce::String())
                   : juce::String())
            + (record->classifier == "clap"
                   ? "  |  CLAP " + juce::String(record->confidence * 100.0f, 0) + " %"
                   : juce::String()),
        juce::dontSendNotification);
    categoryBox_.setText(record->category, juce::dontSendNotification);
    typeBox_.setSelectedId(record->type == "one-shot" ? 1 : record->type == "loop" ? 2
                           : record->type == "long" ? 3 : 0, juce::dontSendNotification);
    tagsEditor_.setText(record->tags.joinIntoString(", "), juce::dontSendNotification);
    bpmEditor_.setText(record->bpm > 0.0 ? juce::String(record->bpm, 1) : juce::String(),
                       juce::dontSendNotification);
    favouriteButton_.setToggleState(record->favourite, juce::dontSendNotification);
    for (std::size_t index = 0; index < ratingButtons_.size(); ++index)
        ratingButtons_[index].setColour(
            juce::TextButton::textColourOffId,
            static_cast<int>(index) < record->rating ? juce::Colour(accent) : juce::Colour(0xff4d535a));
    refreshingDetails_ = false;
}

void SampleExplorerComponent::commitCategory()
{
    if (refreshingDetails_ || selectedPath_.isEmpty())
        return;
    library_.setCategory(selectedPath_, categoryBox_.getText());
}

void SampleExplorerComponent::commitType()
{
    if (refreshingDetails_ || selectedPath_.isEmpty())
        return;
    const auto id = typeBox_.getSelectedId();
    library_.setType(selectedPath_, id == 1 ? "one-shot" : id == 2 ? "loop" : id == 3 ? "long" : "");
}

void SampleExplorerComponent::commitTags()
{
    if (refreshingDetails_ || selectedPath_.isEmpty())
        return;
    juce::StringArray tags;
    tags.addTokens(tagsEditor_.getText(), ",;", "");
    tags.trim();
    tags.removeEmptyStrings();
    const auto* record = library_.find(selectedPath_);
    if (record != nullptr && record->tags != tags)
        library_.setTags(selectedPath_, tags);
}

void SampleExplorerComponent::commitBpm()
{
    if (refreshingDetails_ || selectedPath_.isEmpty())
        return;
    const auto* record = library_.find(selectedPath_);
    const auto typed = bpmEditor_.getText().replaceCharacter(',', '.').getDoubleValue();
    if (record != nullptr && std::abs(typed - record->bpm) > 0.05)
        library_.setBpm(selectedPath_, typed);
}

void SampleExplorerComponent::setRating(const int rating)
{
    const auto* record = library_.find(selectedPath_);
    if (record == nullptr)
        return;
    library_.setRating(selectedPath_, record->rating == rating ? 0 : rating);
}

void SampleExplorerComponent::openSelected(const bool newTrack)
{
    const auto* record = library_.find(selectedPath_);
    if (record == nullptr)
        return;
    if (newTrack && openNewTrack_)
        openNewTrack_(record->file());
    else if (!newTrack && insertAtPlayhead_)
        insertAtPlayhead_(record->file());
}

void SampleExplorerComponent::selectPath(const juce::String& path)
{
    selectedPath_ = path;
    rebuildRows();
    refreshDetails();
}

// ----------------------------------------------------------------- preview

void SampleExplorerComponent::startPreview(const double startSeconds)
{
    const auto* record = library_.find(selectedPath_);
    if (record == nullptr || !startPreview_)
        return;
    (void) startPreview_(record->file(), startSeconds);
}

void SampleExplorerComponent::stopPreview()
{
    if (stopPreview_)
        stopPreview_();
}

// ------------------------------------------------------------------ status

void SampleExplorerComponent::refreshStatus()
{
    const auto progress = library_.progress();
    juce::String text;
    if (progress.scanning)
        text = tr("samples.scanning", "found", juce::String(progress.found))
                   .replace("{analysed}", juce::String(progress.analysed))
                   .replace("{total}", juce::String(progress.toAnalyse));
    else
        text = tr("samples.indexed", "count", juce::String(static_cast<int>(library_.samples().size())))
                   .replace("{shown}", juce::String(static_cast<int>(rows_.size())));
    if (progress.classifying)
        text += "   |   CLAP " + juce::String(progress.classified) + " / "
                + juce::String(progress.toClassify);
    else if (progress.message.isNotEmpty())
        text += "   |   " + progress.message;
    text += "   |   " + tr("samples.cacheAt", "folder", library_.cacheFolder().getFullPathName());
    statusLabel_.setText(text, juce::dontSendNotification);
    statusLabel_.setTooltip(text);
}

void SampleExplorerComponent::sampleLibraryChanged(const bool structure)
{
    if (structure)
        rebuildRows();
    else
        table_.repaint();
    if (library_.folders().size() != folderChips_.size())
        refreshFolderChips();
    refreshDetails();
}

void SampleExplorerComponent::sampleLibraryProgress()
{
    refreshStatus();
}

void SampleExplorerComponent::timerCallback()
{
    const auto playing = isPreviewing_ && isPreviewing_();
    waveform_->setPlayhead(playing && previewPosition_ ? previewPosition_() : 0.0, playing);
    playButton_.setButtonText(playing ? tr("samples.pause") : tr("samples.play"));
    if (library_.isScanning() || library_.isClassifying())
        refreshStatus();
}

} // namespace cydaw::app
