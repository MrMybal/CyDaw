#include "VirtualBusManagerComponent.h"

#include "Localization.h"

#include <algorithm>
#include <initializer_list>

namespace cydaw::app {
namespace {
constexpr juce::uint32 background = 0xff101419;
constexpr juce::uint32 panel = 0xff171c22;
constexpr juce::uint32 border = 0xff343d47;
constexpr juce::uint32 text = 0xffe8edf2;
constexpr juce::uint32 secondary = 0xff98a4b1;
constexpr juce::uint32 inputAccent = 0xff4fd3ae;
constexpr juce::uint32 outputAccent = 0xffff9b2f;
}

VirtualBusManagerComponent::VirtualBusManagerComponent(
    AudioTrackController& controller)
    : controller_(controller)
{
    setOpaque(true);
    for (auto* component : std::initializer_list<juce::Component*> {
             &title_, &subtitle_, &busSelector_, &addInputButton_,
             &addOutputButton_, &removeButton_, &resetButton_, &nameLabel_,
             &nameEditor_, &directionLabel_, &directionValue_, &channelsLabel_,
             &channelsSelector_, &sourceALabel_, &sourceBLabel_,
             &sourceASelector_, &sourceBSelector_, &destinationALabel_,
             &destinationBLabel_, &destinationASelector_,
             &destinationBSelector_, &monitorButton_, &muteButton_,
             &exposeButton_, &systemStatus_, &gainLabel_, &gainSlider_,
             &graphLabel_, &graphStatus_, &editGraphButton_,
             &trackRoutingTitle_, &trackLabel_, &trackSelector_,
             &trackBusLabel_, &trackBusSelector_ })
        addAndMakeVisible(*component);

    title_.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    subtitle_.setFont(juce::FontOptions(11.0f));
    trackRoutingTitle_.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    directionValue_.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    systemStatus_.setFont(juce::FontOptions(10.0f));
    for (auto* label : { &title_, &nameLabel_, &directionLabel_,
                         &directionValue_, &channelsLabel_, &sourceALabel_,
                         &sourceBLabel_, &destinationALabel_,
                         &destinationBLabel_, &gainLabel_, &graphLabel_,
                         &graphStatus_, &trackRoutingTitle_,
                         &trackLabel_, &trackBusLabel_ })
        label->setColour(juce::Label::textColourId, juce::Colour(text));
    subtitle_.setColour(juce::Label::textColourId, juce::Colour(secondary));
    systemStatus_.setColour(juce::Label::textColourId,
                            juce::Colour(secondary));

    configureSlider(gainSlider_, -60.0, 24.0, 0.1, " dB");

    busSelector_.onChange = [this] {
        if (updating_ || busSelector_.getSelectedId() <= 0)
            return;
        const auto index = static_cast<std::size_t>(
            busSelector_.getSelectedId() - 1);
        if (index < buses_.size()) {
            selectedBusId_ = buses_[index].id;
            loadSelectedBus();
        }
    };
    addInputButton_.onClick = [this] {
        const auto id = controller_.addVirtualBus(
            VirtualBusDirection::input);
        refresh();
        rebuildBusSelector(id);
        if (changedHandler_)
            changedHandler_();
    };
    addOutputButton_.onClick = [this] {
        const auto id = controller_.addVirtualBus(
            VirtualBusDirection::output);
        refresh();
        rebuildBusSelector(id);
        if (changedHandler_)
            changedHandler_();
    };
    removeButton_.onClick = [this] {
        if (selectedBusId_ != 0
            && controller_.removeVirtualBus(selectedBusId_)) {
            selectedBusId_ = 0;
            refresh();
            if (changedHandler_)
                changedHandler_();
        }
    };
    resetButton_.onClick = [this] {
        controller_.resetVirtualBuses();
        selectedBusId_ = 0;
        refresh();
        if (changedHandler_)
            changedHandler_();
    };
    nameEditor_.onReturnKey = [this] { applySelectedBus(); };
    nameEditor_.onFocusLost = [this] { applySelectedBus(); };
    const auto apply = [this] { applySelectedBus(); };
    channelsSelector_.onChange = apply;
    sourceASelector_.onChange = apply;
    sourceBSelector_.onChange = apply;
    destinationASelector_.onChange = apply;
    destinationBSelector_.onChange = apply;
    monitorButton_.onClick = apply;
    muteButton_.onClick = apply;
    exposeButton_.onClick = apply;
    gainSlider_.onValueChange = apply;
    editGraphButton_.onClick = [this] {
        if (selectedBusId_ != 0 && openGraphHandler_)
            openGraphHandler_(selectedBusId_);
    };
    trackSelector_.onChange = [this] {
        if (!updating_)
            rebuildTrackRouting();
    };
    trackBusSelector_.onChange = [this] {
        if (updating_ || trackSelector_.getSelectedId() <= 0
            || trackBusSelector_.getSelectedId() <= 0)
            return;
        const auto track = static_cast<std::size_t>(
            trackSelector_.getSelectedId() - 1);
        const auto inputs = [&] {
            std::vector<VirtualAudioBus> result;
            for (const auto& bus : buses_)
                if (bus.direction == VirtualBusDirection::input)
                    result.push_back(bus);
            return result;
        }();
        const auto index = static_cast<std::size_t>(
            trackBusSelector_.getSelectedId() - 1);
        if (index < inputs.size()
            && controller_.setTrackInputBus(track, inputs[index].id)
            && changedHandler_)
            changedHandler_();
    };

    refreshTranslations();
    refresh();
    startTimerHz(20);
}

void VirtualBusManagerComponent::setChangedHandler(
    std::function<void()> handler)
{
    changedHandler_ = std::move(handler);
}

void VirtualBusManagerComponent::setOpenGraphHandler(
    std::function<void(std::uint64_t)> handler)
{
    openGraphHandler_ = std::move(handler);
}

void VirtualBusManagerComponent::configureSlider(
    juce::Slider& slider, const double minimum, const double maximum,
    const double step, juce::String suffix)
{
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 20);
    slider.setRange(minimum, maximum, step);
    slider.setTextValueSuffix(std::move(suffix));
}

void VirtualBusManagerComponent::refreshTranslations()
{
    const auto selectedChannels = std::max(
        1, channelsSelector_.getSelectedId());
    channelsSelector_.clear(juce::dontSendNotification);
    channelsSelector_.addItem(tr("bus.mono"), 1);
    channelsSelector_.addItem(tr("bus.stereo"), 2);
    channelsSelector_.setSelectedId(selectedChannels,
                                    juce::dontSendNotification);
    title_.setText(tr("bus.title"), juce::dontSendNotification);
    subtitle_.setText(tr("bus.subtitle"), juce::dontSendNotification);
    addInputButton_.setButtonText(tr("bus.addInput"));
    addOutputButton_.setButtonText(tr("bus.addOutput"));
    removeButton_.setButtonText(tr("bus.remove"));
    resetButton_.setButtonText(tr("bus.reset"));
    nameLabel_.setText(tr("bus.name"), juce::dontSendNotification);
    directionLabel_.setText(tr("bus.direction"), juce::dontSendNotification);
    channelsLabel_.setText(tr("bus.channels"), juce::dontSendNotification);
    sourceALabel_.setText(tr("bus.sourceA"), juce::dontSendNotification);
    sourceBLabel_.setText(tr("bus.sourceB"), juce::dontSendNotification);
    destinationALabel_.setText(tr("bus.destinationA"), juce::dontSendNotification);
    destinationBLabel_.setText(tr("bus.destinationB"), juce::dontSendNotification);
    monitorButton_.setButtonText(tr("bus.monitor"));
    muteButton_.setButtonText(tr("bus.mute"));
    exposeButton_.setButtonText(tr("bus.expose"));
    gainLabel_.setText(tr("bus.gain"), juce::dontSendNotification);
    graphLabel_.setText(tr("bus.graph"), juce::dontSendNotification);
    graphStatus_.setText(tr("bus.graphStatus"), juce::dontSendNotification);
    editGraphButton_.setButtonText(tr("bus.editGraph"));
    trackRoutingTitle_.setText(tr("bus.trackRouting"), juce::dontSendNotification);
    trackLabel_.setText(tr("bus.track"), juce::dontSendNotification);
    trackBusLabel_.setText(tr("bus.inputBus"), juce::dontSendNotification);
    addInputButton_.setTooltip(tr("bus.addInputHelp"));
    addOutputButton_.setTooltip(tr("bus.addOutputHelp"));
    removeButton_.setTooltip(tr("bus.removeHelp"));
    resetButton_.setTooltip(tr("bus.resetHelp"));
    monitorButton_.setTooltip(tr("bus.monitorHelp"));
    exposeButton_.setTooltip(tr("bus.exposeHelp"));
    editGraphButton_.setTooltip(tr("bus.editGraphHelp"));
#if JUCE_LINUX
    systemStatus_.setText(tr("bus.systemLinux"), juce::dontSendNotification);
#elif JUCE_MAC
    systemStatus_.setText(tr("bus.systemMac"), juce::dontSendNotification);
#else
    systemStatus_.setText(tr("bus.systemWindows"), juce::dontSendNotification);
#endif
}

void VirtualBusManagerComponent::refresh()
{
    buses_ = controller_.virtualBuses();
    rebuildChannelSelectors();
    rebuildBusSelector(selectedBusId_);
    rebuildTrackRouting();
    loadSelectedBus();
    repaint();
}

void VirtualBusManagerComponent::rebuildBusSelector(
    const std::uint64_t selectBus)
{
    const juce::ScopedValueSetter<bool> guard(updating_, true);
    busSelector_.clear(juce::dontSendNotification);
    for (std::size_t index = 0; index < buses_.size(); ++index) {
        const auto prefix = buses_[index].direction
                                    == VirtualBusDirection::input
                                ? tr("bus.inputShort") + "  "
                                : tr("bus.outputShort") + "  ";
        busSelector_.addItem(prefix + buses_[index].name,
                             static_cast<int>(index + 1));
    }
    auto selectedIndex = std::size_t {};
    const auto requested = selectBus != 0 ? selectBus : selectedBusId_;
    const auto found = std::find_if(
        buses_.begin(), buses_.end(), [requested](const auto& bus) {
            return bus.id == requested;
        });
    if (found != buses_.end())
        selectedIndex = static_cast<std::size_t>(
            std::distance(buses_.begin(), found));
    if (!buses_.empty()) {
        selectedBusId_ = buses_[selectedIndex].id;
        busSelector_.setSelectedId(static_cast<int>(selectedIndex + 1),
                                   juce::dontSendNotification);
    }
}

void VirtualBusManagerComponent::rebuildChannelSelectors()
{
    const juce::ScopedValueSetter<bool> guard(updating_, true);
    const auto inputs = controller_.physicalInputChannelNames();
    const auto outputs = controller_.physicalOutputChannelNames();
    sourceASelector_.clear(juce::dontSendNotification);
    sourceBSelector_.clear(juce::dontSendNotification);
    destinationASelector_.clear(juce::dontSendNotification);
    destinationBSelector_.clear(juce::dontSendNotification);
    const auto inputCount = std::max(2, inputs.size());
    const auto outputCount = std::max(2, outputs.size());
    for (int index = 0; index < inputCount; ++index) {
        const auto name = index < inputs.size() && inputs[index].isNotEmpty()
            ? inputs[index] : tr("bus.inputChannel") + " "
                                   + juce::String(index + 1);
        sourceASelector_.addItem(name, index + 1);
        sourceBSelector_.addItem(name, index + 1);
    }
    for (int index = 0; index < outputCount; ++index) {
        const auto name = index < outputs.size() && outputs[index].isNotEmpty()
            ? outputs[index] : tr("bus.outputChannel") + " "
                                    + juce::String(index + 1);
        destinationASelector_.addItem(name, index + 1);
        destinationBSelector_.addItem(name, index + 1);
    }
}

void VirtualBusManagerComponent::rebuildTrackRouting()
{
    const juce::ScopedValueSetter<bool> guard(updating_, true);
    auto selectedTrack = std::max(1, trackSelector_.getSelectedId());
    trackSelector_.clear(juce::dontSendNotification);
    for (std::size_t index = 0; index < controller_.trackCount(); ++index) {
        const auto info = controller_.trackInfo(index);
        auto name = info.file.getFileNameWithoutExtension();
        if (name.isEmpty())
            name = tr("bus.audioTrack") + " "
                   + juce::String(static_cast<int>(index + 1));
        trackSelector_.addItem(name, static_cast<int>(index + 1));
    }
    if (controller_.trackCount() == 0) {
        trackSelector_.setTextWhenNothingSelected(tr("bus.noTrack"));
        trackBusSelector_.clear(juce::dontSendNotification);
        return;
    }
    selectedTrack = juce::jlimit(
        1, static_cast<int>(controller_.trackCount()), selectedTrack);
    trackSelector_.setSelectedId(selectedTrack,
                                 juce::dontSendNotification);
    trackBusSelector_.clear(juce::dontSendNotification);
    const auto currentBus = controller_.trackInfo(
        static_cast<std::size_t>(selectedTrack - 1)).inputBusId;
    auto selectedBusIndex = 0;
    auto inputIndex = 0;
    for (const auto& bus : buses_) {
        if (bus.direction != VirtualBusDirection::input)
            continue;
        ++inputIndex;
        trackBusSelector_.addItem(bus.name, inputIndex);
        if (bus.id == currentBus)
            selectedBusIndex = inputIndex;
    }
    trackBusSelector_.setSelectedId(
        selectedBusIndex > 0 ? selectedBusIndex : 1,
        juce::dontSendNotification);
}

VirtualAudioBus* VirtualBusManagerComponent::selectedBus() noexcept
{
    const auto found = std::find_if(
        buses_.begin(), buses_.end(), [this](const auto& bus) {
            return bus.id == selectedBusId_;
        });
    return found != buses_.end() ? &*found : nullptr;
}

const VirtualAudioBus* VirtualBusManagerComponent::selectedBus() const noexcept
{
    const auto found = std::find_if(
        buses_.begin(), buses_.end(), [this](const auto& bus) {
            return bus.id == selectedBusId_;
        });
    return found != buses_.end() ? &*found : nullptr;
}

void VirtualBusManagerComponent::loadSelectedBus()
{
    const auto* bus = selectedBus();
    if (bus == nullptr)
        return;
    const juce::ScopedValueSetter<bool> guard(updating_, true);
    nameEditor_.setText(bus->name, false);
    directionValue_.setText(
        bus->direction == VirtualBusDirection::input
            ? tr("bus.input") : tr("bus.output"),
        juce::dontSendNotification);
    directionValue_.setColour(
        juce::Label::textColourId,
        juce::Colour(bus->direction == VirtualBusDirection::input
                         ? inputAccent : outputAccent));
    channelsSelector_.setSelectedId(bus->channels,
                                    juce::dontSendNotification);
    sourceASelector_.setSelectedId(bus->physicalChannelA + 1,
                                   juce::dontSendNotification);
    sourceBSelector_.setSelectedId(bus->physicalChannelB + 1,
                                   juce::dontSendNotification);
    destinationASelector_.setSelectedId(bus->destinationChannelA + 1,
                                        juce::dontSendNotification);
    destinationBSelector_.setSelectedId(bus->destinationChannelB + 1,
                                        juce::dontSendNotification);
    monitorButton_.setToggleState(bus->monitor, juce::dontSendNotification);
    muteButton_.setToggleState(bus->muted, juce::dontSendNotification);
    exposeButton_.setToggleState(bus->exposeToSystem,
                                 juce::dontSendNotification);
    gainSlider_.setValue(bus->gainDb, juce::dontSendNotification);
    const auto stereo = bus->channels > 1;
    sourceASelector_.setEnabled(bus->direction == VirtualBusDirection::input);
    sourceBSelector_.setEnabled(bus->direction == VirtualBusDirection::input
                                && stereo);
    destinationBSelector_.setEnabled(stereo);
    monitorButton_.setEnabled(bus->direction == VirtualBusDirection::input);
    editGraphButton_.setEnabled(true);
}

void VirtualBusManagerComponent::applySelectedBus()
{
    if (updating_)
        return;
    auto* bus = selectedBus();
    if (bus == nullptr)
        return;
    const auto previousName = bus->name;
    const auto previousChannels = bus->channels;
    bus->name = nameEditor_.getText();
    bus->channels = juce::jlimit(1, 2, channelsSelector_.getSelectedId());
    bus->physicalChannelA = std::max(0, sourceASelector_.getSelectedId() - 1);
    bus->physicalChannelB = std::max(0, sourceBSelector_.getSelectedId() - 1);
    bus->destinationChannelA = std::max(
        0, destinationASelector_.getSelectedId() - 1);
    bus->destinationChannelB = std::max(
        0, destinationBSelector_.getSelectedId() - 1);
    bus->monitor = monitorButton_.getToggleState();
    bus->muted = muteButton_.getToggleState();
    bus->exposeToSystem = exposeButton_.getToggleState();
    bus->gainDb = static_cast<float>(gainSlider_.getValue());
    if (controller_.updateVirtualBus(*bus)) {
        if (const auto refreshed = controller_.virtualBus(selectedBusId_))
            *bus = *refreshed;
        const auto nameChanged = bus->name != previousName;
        if (nameChanged) {
            rebuildBusSelector(selectedBusId_);
            rebuildTrackRouting();
        }
        if (nameChanged || bus->channels != previousChannels)
            loadSelectedBus();
        if (changedHandler_)
            changedHandler_();
    }
}

void VirtualBusManagerComponent::timerCallback()
{
    if (selectedBusId_ != 0)
        meter_ = controller_.virtualBusMeterLevels(selectedBusId_);
    repaint();
}

void VirtualBusManagerComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(background));
    auto bounds = getLocalBounds().reduced(16);
    bounds.removeFromTop(82);
    auto routing = bounds.removeFromBottom(118);
    bounds.removeFromBottom(12);
    auto detail = bounds.removeFromLeft(
        std::min(720, std::max(420, bounds.getWidth() * 2 / 3)));
    graphics.setColour(juce::Colour(panel));
    graphics.fillRoundedRectangle(detail.toFloat(), 6.0f);
    graphics.fillRoundedRectangle(routing.toFloat(), 6.0f);
    graphics.setColour(juce::Colour(border));
    graphics.drawRoundedRectangle(detail.toFloat(), 6.0f, 1.0f);
    graphics.drawRoundedRectangle(routing.toFloat(), 6.0f, 1.0f);

    auto meterArea = detail.removeFromRight(48).reduced(14, 24);
    graphics.setColour(juce::Colour(0xff0b0e12));
    graphics.fillRoundedRectangle(meterArea.toFloat(), 3.0f);
    const auto drawMeter = [&](const int index, juce::Rectangle<int> area) {
        const auto level = juce::jlimit(0.0f, 1.0f, meter_[index]);
        area.removeFromTop(juce::roundToInt(
            static_cast<float>(area.getHeight()) * (1.0f - level)));
        graphics.setGradientFill(juce::ColourGradient(
            juce::Colour(0xffff6b59), 0.0f, static_cast<float>(area.getY()),
            juce::Colour(0xff55d98a), 0.0f,
            static_cast<float>(area.getBottom()), false));
        graphics.fillRect(area);
    };
    auto left = meterArea.removeFromLeft(meterArea.getWidth() / 2).reduced(2);
    auto right = meterArea.reduced(2);
    drawMeter(0, left);
    drawMeter(1, right);
}

void VirtualBusManagerComponent::resized()
{
    auto area = getLocalBounds().reduced(16);
    title_.setBounds(area.removeFromTop(28));
    subtitle_.setBounds(area.removeFromTop(22));
    auto actions = area.removeFromTop(32);
    busSelector_.setBounds(actions.removeFromLeft(260).reduced(2, 1));
    addInputButton_.setBounds(actions.removeFromLeft(108).reduced(2, 1));
    addOutputButton_.setBounds(actions.removeFromLeft(108).reduced(2, 1));
    removeButton_.setBounds(actions.removeFromLeft(86).reduced(2, 1));
    resetButton_.setBounds(actions.removeFromLeft(82).reduced(2, 1));
    area.removeFromTop(8);
    auto routing = area.removeFromBottom(118).reduced(14, 10);
    area.removeFromBottom(12);
    auto detail = area.removeFromLeft(
        std::min(720, std::max(420, area.getWidth() * 2 / 3))).reduced(16, 12);
    detail.removeFromRight(48);
    const auto labelWidth = 136;
    auto row = [&](juce::Label& label, juce::Component& control) {
        auto line = detail.removeFromTop(30);
        label.setBounds(line.removeFromLeft(labelWidth));
        control.setBounds(line.reduced(2, 2));
        detail.removeFromTop(3);
    };
    row(nameLabel_, nameEditor_);
    row(directionLabel_, directionValue_);
    row(channelsLabel_, channelsSelector_);
    auto channelRow = detail.removeFromTop(50);
    auto leftColumn = channelRow.removeFromLeft(channelRow.getWidth() / 2);
    sourceALabel_.setBounds(leftColumn.removeFromTop(18));
    sourceASelector_.setBounds(leftColumn.reduced(2, 1));
    sourceBLabel_.setBounds(channelRow.removeFromTop(18));
    sourceBSelector_.setBounds(channelRow.reduced(2, 1));
    detail.removeFromTop(4);
    channelRow = detail.removeFromTop(50);
    leftColumn = channelRow.removeFromLeft(channelRow.getWidth() / 2);
    destinationALabel_.setBounds(leftColumn.removeFromTop(18));
    destinationASelector_.setBounds(leftColumn.reduced(2, 1));
    destinationBLabel_.setBounds(channelRow.removeFromTop(18));
    destinationBSelector_.setBounds(channelRow.reduced(2, 1));
    detail.removeFromTop(4);
    auto toggles = detail.removeFromTop(28);
    monitorButton_.setBounds(toggles.removeFromLeft(122));
    muteButton_.setBounds(toggles.removeFromLeft(90));
    exposeButton_.setBounds(toggles);
    systemStatus_.setBounds(detail.removeFromTop(28));
    row(gainLabel_, gainSlider_);
    auto graphRow = detail.removeFromTop(34);
    graphLabel_.setBounds(graphRow.removeFromLeft(labelWidth));
    editGraphButton_.setBounds(graphRow.removeFromRight(150).reduced(2, 2));
    graphStatus_.setBounds(graphRow.reduced(4, 2));

    trackRoutingTitle_.setBounds(routing.removeFromTop(24));
    auto trackRow = routing.removeFromTop(32);
    trackLabel_.setBounds(trackRow.removeFromLeft(100));
    trackSelector_.setBounds(trackRow.removeFromLeft(280).reduced(2, 2));
    trackRow.removeFromLeft(16);
    trackBusLabel_.setBounds(trackRow.removeFromLeft(130));
    trackBusSelector_.setBounds(trackRow.removeFromLeft(280).reduced(2, 2));
}

} // namespace cydaw::app
