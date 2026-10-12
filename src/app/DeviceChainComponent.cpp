#include "DeviceChainComponent.h"
#include "PaintProfile.h"
#include "JsonValue.h"
#include "Localization.h"

#include <algorithm>
#include <cmath>

namespace cydaw::app {
namespace {
constexpr std::array<juce::uint32, 6> accents {
    0xff5d8bd8, 0xffff8a00, 0xffffc13b, 0xffa98bea, 0xff59bff2, 0xff80c65a
};
}

DeviceChainComponent::DeviceChainComponent()
{
    setWantsKeyboardFocus(true);
    addAndMakeVisible(nodeDeviceSurface_);
    addAndMakeVisible(openNodeButton_);
    addAndMakeVisible(addDeviceButton_);
    nodeDeviceSurface_.setCompactMode(true);
    nodeDeviceSurface_.setParameterChangedHandler(
        [this](const juce::String& key, const double value) {
            if (nodeParameterChangedHandler_)
                nodeParameterChangedHandler_(key, value);
        });
    for (auto* slider : { &driveSlider_, &cutoffSlider_, &gainSlider_, &panSlider_ }) {
        addAndMakeVisible(*slider);
        slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 17);
    }
    for (auto* component : { static_cast<juce::Component*>(&removePluginButton_),
                             static_cast<juce::Component*>(&openPluginButton_),
                             static_cast<juce::Component*>(&movePluginLeftButton_),
                             static_cast<juce::Component*>(&movePluginRightButton_),
                             static_cast<juce::Component*>(&pluginSelector_) })
        addAndMakeVisible(*component);
    removePluginButton_.onClick = [this] {
        if (pluginRemoveHandler_)
            pluginRemoveHandler_(selectedPluginIndex_);
    };
    openNodeButton_.onClick = [this] {
        if (nodeOpenHandler_)
            nodeOpenHandler_();
    };
    openPluginButton_.onClick = [this] {
        if (pluginOpenHandler_)
            pluginOpenHandler_(selectedPluginIndex_);
    };
    movePluginLeftButton_.onClick = [this] {
        if (pluginMoveHandler_)
            pluginMoveHandler_(selectedPluginIndex_, -1);
    };
    movePluginRightButton_.onClick = [this] {
        if (pluginMoveHandler_)
            pluginMoveHandler_(selectedPluginIndex_, 1);
    };
    addDeviceButton_.onClick = [this] {
        if (deviceAddHandler_)
            deviceAddHandler_();
    };
    pluginSelector_.onChange = [this] {
        if (pluginSelector_.getSelectedId() <= 0)
            return;
        selectedPluginIndex_ = static_cast<std::size_t>(pluginSelector_.getSelectedId() - 1);
        if (pluginSelectHandler_)
            pluginSelectHandler_(selectedPluginIndex_);
        setPluginChain(plugins_, selectedPluginIndex_);
    };
    driveSlider_.setRange(0.0, 36.0, 0.1);
    driveSlider_.setDefaultValue(0.0);
    driveSlider_.setTextValueSuffix(" dB");
    driveSlider_.setColour(juce::Slider::trackColourId, juce::Colour(accents[1]));
    cutoffSlider_.setRange(20.0, 20'000.0, 1.0);
    cutoffSlider_.setDefaultValue(20'000.0);
    cutoffSlider_.setSkewFactorFromMidPoint(1'000.0);
    cutoffSlider_.setTextValueSuffix(" Hz");
    cutoffSlider_.setColour(juce::Slider::trackColourId, juce::Colour(accents[2]));
    gainSlider_.setRange(-60.0, 12.0, 0.1);
    gainSlider_.setDefaultValue(0.0);
    gainSlider_.setTextValueSuffix(" dB");
    gainSlider_.setColour(juce::Slider::trackColourId, juce::Colour(accents[3]));
    panSlider_.setRange(-1.0, 1.0, 0.01);
    panSlider_.setDefaultValue(0.0);
    panSlider_.setColour(juce::Slider::trackColourId, juce::Colour(accents[3]));

    driveSlider_.onValueChange = [this] { notifyEffects(); };
    cutoffSlider_.onValueChange = [this] { notifyEffects(); };
    gainSlider_.onValueChange = [this] { notifyMixer(); };
    panSlider_.onValueChange = [this] { notifyMixer(); };
    refreshTranslations();
}

void DeviceChainComponent::refreshTranslations()
{
    titles_ = { inputTitle_.isNotEmpty() ? inputTitle_
                                         : tr("device.trackInput"),
                tr("node.drive"), tr("node.lowpass"),
                tr("device.pluginChain"), tr("node.gainPan"),
                outputTitle_.isNotEmpty() ? outputTitle_
                                          : tr("device.trackOutput") };
    removePluginButton_.setButtonText(juce::String::fromUTF8("×"));
    openPluginButton_.setButtonText("UI");
    openNodeButton_.setButtonText("UI");
    addDeviceButton_.setButtonText("+");
    movePluginLeftButton_.setButtonText(juce::String::fromUTF8("←"));
    movePluginRightButton_.setButtonText(juce::String::fromUTF8("→"));
    removePluginButton_.setTooltip(tr("device.removePlugin"));
    openPluginButton_.setTooltip(tr("device.openPlugin"));
    openNodeButton_.setTooltip(tr("device.openNodeInterface"));
    addDeviceButton_.setTooltip(tr("device.addDeviceHelp"));
    movePluginLeftButton_.setTooltip(tr("device.movePluginLeft"));
    movePluginRightButton_.setTooltip(tr("device.movePluginRight"));
    driveSlider_.setTooltip(tr("node.driveTooltip"));
    cutoffSlider_.setTooltip(tr("node.lowpassTooltip"));
    gainSlider_.setTooltip(tr("mixer.gain"));
    panSlider_.setTooltip(tr("mixer.pan"));
    repaint();
}

void DeviceChainComponent::setPluginChain(std::vector<PluginDisplay> plugins,
                                          const std::size_t selectedPluginIndex)
{
    const auto previousSelection = selectedPluginIndex_;
    const auto structureChanged = plugins.size() != plugins_.size()
        || !std::equal(plugins.begin(), plugins.end(), plugins_.begin(),
                      [](const auto& first, const auto& second) {
                          return first.name == second.name;
                      });
    plugins_ = std::move(plugins);
    if (structureChanged) {
        pluginSelector_.clear(juce::dontSendNotification);
        for (std::size_t index = 0; index < plugins_.size(); ++index)
            pluginSelector_.addItem(
                juce::String(static_cast<int>(index + 1)) + "  "
                    + plugins_[index].name,
                static_cast<int>(index + 1));
    }
    selectedPluginIndex_ = plugins_.empty() ? 0 : std::min(selectedPluginIndex, plugins_.size() - 1);
    ensureSelectedPluginVisible_ = !plugins_.empty()
        && (structureChanged || previousSelection != selectedPluginIndex_);
    if (!plugins_.empty()) {
        const auto& selected = plugins_[selectedPluginIndex_];
        pluginName_ = selected.name;
        pluginRunning_ = selected.running;
        pluginMissedBlocks_ = selected.missedBlocks;
        pluginLatencySamples_ = selected.latencySamples;
        pluginSelector_.setSelectedId(static_cast<int>(selectedPluginIndex_ + 1),
                                      juce::dontSendNotification);
    } else {
        pluginName_.clear();
        pluginRunning_ = false;
        pluginMissedBlocks_ = 0;
        pluginLatencySamples_ = 0;
    }
    const auto hasPlugin = !plugins_.empty();
    const auto showTrackChain = viewScope_ == ViewScope::track;
    // The track chain renders every plug-in as its own device card. The combo
    // remains part of the component for backward state compatibility, but no
    // longer hides the order of the actual signal chain.
    pluginSelector_.setVisible(false);
    removePluginButton_.setVisible(showTrackChain && hasPlugin);
    openPluginButton_.setVisible(showTrackChain && hasPlugin && pluginRunning_);
    movePluginLeftButton_.setVisible(showTrackChain && hasPlugin);
    movePluginRightButton_.setVisible(showTrackChain && hasPlugin);
    movePluginLeftButton_.setEnabled(hasPlugin && selectedPluginIndex_ > 0);
    movePluginRightButton_.setEnabled(hasPlugin && selectedPluginIndex_ + 1 < plugins_.size());
    resized();
    repaint();
}

void DeviceChainComponent::setParameters(const float driveDb,
                                          const float cutoffHz,
                                          const float gainDb,
                                          const float pan,
                                          const bool enabled)
{
    trackEnabled_ = enabled;
    for (auto* slider : { &driveSlider_, &cutoffSlider_, &gainSlider_, &panSlider_ })
        slider->setEnabled(enabled);
    driveSlider_.setValue(driveDb, juce::dontSendNotification);
    cutoffSlider_.setValue(cutoffHz, juce::dontSendNotification);
    gainSlider_.setValue(gainDb, juce::dontSendNotification);
    panSlider_.setValue(pan, juce::dontSendNotification);
}

void DeviceChainComponent::setMeterLevels(
    const std::array<float, 2>& trackPeak,
    const std::array<float, 2>& trackRms,
    const std::array<float, 2>& masterPeak,
    const std::array<float, 2>& masterRms)
{
    trackPeak_ = trackPeak;
    trackRms_ = trackRms;
    masterPeak_ = masterPeak;
    masterRms_ = masterRms;
    repaint(cards_[0]);
    repaint(cards_[5]);
}

void DeviceChainComponent::setEffectChangedHandler(std::function<void(float, float)> handler)
{
    effectChangedHandler_ = std::move(handler);
}

void DeviceChainComponent::setMixerChangedHandler(std::function<void(float, float)> handler)
{
    mixerChangedHandler_ = std::move(handler);
}

void DeviceChainComponent::setPluginSelectHandler(std::function<void(std::size_t)> handler)
{
    pluginSelectHandler_ = std::move(handler);
}

void DeviceChainComponent::setPluginRemoveHandler(std::function<void(std::size_t)> handler)
{
    pluginRemoveHandler_ = std::move(handler);
}

void DeviceChainComponent::setPluginOpenHandler(std::function<void(std::size_t)> handler)
{
    pluginOpenHandler_ = std::move(handler);
}

void DeviceChainComponent::setPluginMoveHandler(std::function<void(std::size_t, int)> handler)
{
    pluginMoveHandler_ = std::move(handler);
}

void DeviceChainComponent::setNodeOpenHandler(std::function<void()> handler)
{
    nodeOpenHandler_ = std::move(handler);
}

void DeviceChainComponent::setDeviceAddHandler(std::function<void()> handler)
{
    deviceAddHandler_ = std::move(handler);
}

void DeviceChainComponent::setNodeDeviceInterface(ModuleInterfaceState state)
{
    // Placing a node or drawing a cable does not change the published
    // interface, yet both republished it and rebuilt the whole surface:
    // its sliders, its inspector and its layout, and the surface's own undo
    // history went with them. An identical description is now ignored.
    auto described = serialiseModuleInterface(state);
    if (!nodeInterfaceDescription_.isVoid()
        && jsonValuesMatch(nodeInterfaceDescription_, described))
        return;
    nodeInterfaceDescription_ = std::move(described);
    nodeInterfaceState_ = state;
    nodeDeviceSurface_.setState(std::move(state));
    resized();
}

void DeviceChainComponent::setGraphLayout(
    juce::String inputTitle, juce::String outputTitle,
    const bool hasDriveNode, const bool hasLowPassNode)
{
    inputTitle_ = std::move(inputTitle);
    outputTitle_ = std::move(outputTitle);
    hasDriveNode_ = hasDriveNode;
    hasLowPassNode_ = hasLowPassNode;
    titles_[0] = inputTitle_;
    titles_[5] = outputTitle_;
    driveSlider_.setVisible(hasDriveNode_);
    cutoffSlider_.setVisible(hasLowPassNode_);
    resized();
    repaint();
}

void DeviceChainComponent::setNodeParameterChangedHandler(
    std::function<void(const juce::String&, double)> handler)
{
    nodeParameterChangedHandler_ = std::move(handler);
}

void DeviceChainComponent::setNodeAnalysisFrame(
    const cydaw::engine::AudioAnalysisFrame& frame)
{
    nodeDeviceSurface_.setAnalysisFrame(frame);
}

void DeviceChainComponent::clearNodeAnalysis() noexcept
{
    nodeDeviceSurface_.clearAnalysis();
}

void DeviceChainComponent::setViewScope(const ViewScope scope)
{
    if (viewScope_ == scope)
        return;
    viewScope_ = scope;
    resized();
    repaint();
}

DeviceChainComponent::ViewScope DeviceChainComponent::viewScope() const noexcept
{
    return viewScope_;
}

void DeviceChainComponent::paint(juce::Graphics& graphics)
{
    const paint_profile::Scoped paintScope(paint_profile::counters().chain);
    // Two meters tick thirty times a second; the cards they are not on stay
    // as they were painted.
    const auto clip = graphics.getClipBounds();
    graphics.fillAll(juce::Colour(0xff16181b));
    if (!chainBounds_.isEmpty()) {
        graphics.setColour(juce::Colour(0xff1c1f23));
        graphics.fillRoundedRectangle(chainBounds_.toFloat(), 3.0f);
        graphics.setColour(juce::Colour(0xff292e33));
        graphics.drawHorizontalLine(chainBounds_.getCentreY(),
                                    static_cast<float>(chainBounds_.getX() + 8),
                                    static_cast<float>(chainBounds_.getRight() - 8));
    }

    const auto paintFrame = [&graphics](const juce::Rectangle<int>& card,
                                         const juce::String& title,
                                         const juce::String& kind,
                                         const juce::Colour accent,
                                         const bool active,
                                         const bool selected,
                                         const int order) {
        if (card.isEmpty())
            return;
        const auto bounds = card.toFloat();
        graphics.setColour(selected ? juce::Colour(0xff30343a)
                                    : juce::Colour(0xff25282d));
        graphics.fillRoundedRectangle(bounds, 3.0f);
        graphics.setColour(selected ? juce::Colour(0xffff8a00)
                                    : juce::Colour(0xff454a50));
        graphics.drawRoundedRectangle(bounds, 3.0f, selected ? 1.6f : 1.0f);
        graphics.setColour(accent);
        graphics.fillRoundedRectangle(bounds.getX(), bounds.getY(), 3.0f,
                                      bounds.getHeight(), 1.5f);

        auto header = card.reduced(4, 2).removeFromTop(24);
        graphics.setColour(juce::Colour(0xff1b1e22));
        graphics.fillRoundedRectangle(header.toFloat(), 2.0f);
        graphics.setColour(active ? accent : juce::Colour(0xff626970));
        graphics.fillEllipse(static_cast<float>(header.getX() + 5),
                             static_cast<float>(header.getCentreY() - 3),
                             6.0f, 6.0f);
        graphics.setColour(juce::Colour(0xfff0f1f2));
        graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        auto titleArea = header.withTrimmedLeft(15).withTrimmedRight(35);
        graphics.drawFittedText(title, titleArea,
                                juce::Justification::centredLeft, 1, 0.72f);
        graphics.setColour(accent.withAlpha(0.18f));
        auto badge = header.removeFromRight(32).reduced(1, 4);
        graphics.fillRoundedRectangle(badge.toFloat(), 2.0f);
        graphics.setColour(accent.brighter(0.18f));
        graphics.setFont(juce::FontOptions(6.8f, juce::Font::bold));
        graphics.drawFittedText(kind, badge, juce::Justification::centred,
                                1, 0.70f);
        graphics.setColour(juce::Colour(0xff777e85));
        graphics.setFont(juce::FontOptions(7.0f, juce::Font::bold));
        graphics.drawText(juce::String(order).paddedLeft('0', 2),
                          card.reduced(7, 3).removeFromBottom(12),
                          juce::Justification::bottomRight, false);
    };

    const auto meterAmount = [](const float level) {
        const auto db = juce::Decibels::gainToDecibels(
            std::max(0.000001f, level), -60.0f);
        return juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
    };
    const auto paintHorizontalMeters = [&graphics, &meterAmount](
                                            juce::Rectangle<int> area,
                                            const std::array<float, 2>& peak,
                                            const std::array<float, 2>& rms,
                                            const juce::Colour colour) {
        // The bars grow with the card but stay bar-shaped: a horizontal meter
        // 46 px tall reads as an empty field. They keep an outline so a silent
        // meter still reads as a meter, and the pair is centred in its area.
        constexpr auto gap = 4;
        const auto barHeight = juce::jlimit(
            6, 13, (area.getHeight() - gap) / 2);
        area = area.withSizeKeepingCentre(area.getWidth(),
                                          barHeight * 2 + gap);
        for (std::size_t channel = 0; channel < 2; ++channel) {
            const auto track = area.removeFromTop(barHeight);
            area.removeFromTop(gap);
            graphics.setColour(juce::Colour(0xff0d1014));
            graphics.fillRoundedRectangle(track.toFloat(), 2.0f);
            graphics.setColour(juce::Colour(0xff2b3138));
            graphics.drawRoundedRectangle(track.toFloat().reduced(0.5f), 2.0f,
                                          1.0f);
            auto bar = track;
            bar.setWidth(juce::roundToInt(static_cast<float>(bar.getWidth())
                                          * meterAmount(rms[channel])));
            graphics.setColour(peak[channel] >= 1.0f
                                   ? juce::Colour(0xffff5252) : colour);
            graphics.fillRoundedRectangle(bar.toFloat(), 2.0f);
            const auto peakX = static_cast<float>(track.getX())
                               + static_cast<float>(track.getWidth())
                                     * meterAmount(peak[channel]);
            graphics.setColour(peak[channel] >= 1.0f
                                   ? juce::Colour(0xffff8a8a)
                                   : juce::Colour(0xffe6ebef));
            graphics.fillRect(juce::Rectangle<float>(
                juce::jlimit(static_cast<float>(track.getX()),
                             static_cast<float>(track.getRight()) - 1.2f,
                             peakX - 1.2f),
                static_cast<float>(track.getY()) + 1.0f, 1.2f,
                static_cast<float>(track.getHeight()) - 2.0f));
        }
    };

    auto order = 1;
    if (!cards_[0].isEmpty() && cards_[0].intersects(clip)) {
        paintFrame(cards_[0], titles_[0], tr("device.ioShort"),
                   juce::Colour(accents[0]), trackEnabled_, false, order++);
        auto body = cards_[0].reduced(11).withTrimmedTop(34);
        graphics.setColour(trackEnabled_ ? juce::Colour(0xffaeb5bc)
                                         : juce::Colour(0xff686e74));
        graphics.setFont(juce::FontOptions(8.0f, juce::Font::bold));
        // Two lines: "AUCUNE PISTE SELECTIONNEE" does not fit on one at this
        // width and came out cut to "AUCUNE PISTE SELECTION".
        graphics.drawFittedText(tr(trackEnabled_ ? "device.signalInput"
                                                 : "device.noTrack"),
                                body.removeFromTop(26),
                                juce::Justification::topLeft, 2, 0.8f);
        paintHorizontalMeters(body.removeFromTop(26), trackPeak_, trackRms_,
                              juce::Colour(0xff5d8bd8));
        graphics.setColour(juce::Colour(0xff5d8bd8));
        graphics.setFont(juce::FontOptions(16.0f, juce::Font::bold));
        graphics.drawText(juce::String::fromUTF8("→"), body,
                          juce::Justification::centred, false);
    }

    if (!cards_[0].isEmpty() && !cards_[0].intersects(clip))
        ++order;
    if (!nodeFrame_.isEmpty() && nodeFrame_.intersects(clip)) {
        paintFrame(nodeFrame_, tr("device.graphDevice"), "GRAPH",
                   juce::Colour(0xff67b9ff), true, false, order++);
    }

    if (!nodeFrame_.isEmpty() && !nodeFrame_.intersects(clip))
        ++order;
    if (!cards_[1].isEmpty()) {
        if (cards_[1].intersects(clip))
            paintFrame(cards_[1], titles_[1], tr("device.nativeShort"),
                       juce::Colour(accents[1]), driveSlider_.isEnabled(),
                       false, order);
        ++order;
    }
    if (!cards_[2].isEmpty()) {
        if (cards_[2].intersects(clip))
            paintFrame(cards_[2], titles_[2], tr("device.nativeShort"),
                       juce::Colour(accents[2]), cutoffSlider_.isEnabled(),
                       false, order);
        ++order;
    }

    for (std::size_t index = 0; index < pluginCards_.size(); ++index) {
        if (!pluginCards_[index].intersects(clip)) {
            ++order;
            continue;
        }
        const auto& plugin = plugins_[index];
        const auto selected = index == selectedPluginIndex_;
        paintFrame(pluginCards_[index], plugin.name, "VST3",
                   juce::Colour(0xffa98bea), plugin.running, selected,
                   order++);
        auto body = pluginCards_[index].reduced(10).withTrimmedTop(34)
                        .withTrimmedBottom(selected ? 29 : 8);
        graphics.setColour(plugin.running ? juce::Colour(0xff75d6ac)
                                          : juce::Colour(0xffffb547));
        graphics.setFont(juce::FontOptions(8.2f, juce::Font::bold));
        graphics.drawFittedText(
            tr(plugin.running ? "device.pluginRunning"
                              : "device.pluginBypass"),
            body.removeFromTop(18), juce::Justification::centredLeft, 1,
            0.75f);
        juce::String status;
        if (plugin.latencySamples > 0)
            status = tr("device.pluginLatency", "count",
                        juce::String(plugin.latencySamples));
        if (plugin.missedBlocks > 0)
            status += (status.isNotEmpty() ? "  ·  " : juce::String {})
                + tr("device.pluginMisses", "count",
                     juce::String(plugin.missedBlocks));
        graphics.setColour(juce::Colour(0xff8f969c));
        graphics.setFont(juce::FontOptions(7.5f));
        graphics.drawFittedText(status.isNotEmpty() ? status
                                                    : tr("device.isolated"),
                                body, juce::Justification::centredLeft, 2,
                                0.70f);
    }

    if (!cards_[4].isEmpty()) {
        if (cards_[4].intersects(clip))
            paintFrame(cards_[4], titles_[4], tr("device.mixerShort"),
                       juce::Colour(accents[4]), gainSlider_.isEnabled(),
                       false, order);
        ++order;
    }

    if (!cards_[5].isEmpty() && cards_[5].intersects(clip)) {
        paintFrame(cards_[5], titles_[5], tr("device.ioShort"),
                   juce::Colour(accents[5]), trackEnabled_, false, order++);
        auto body = cards_[5].reduced(11).withTrimmedTop(34);
        const auto meterHeight = std::max(18, body.getHeight() - 30);
        paintHorizontalMeters(body.removeFromTop(meterHeight), trackPeak_,
                              trackRms_, juce::Colour(0xff80c65a));
        const auto maximumPeak = std::max(trackPeak_[0], trackPeak_[1]);
        const auto peakDb = juce::Decibels::gainToDecibels(
            std::max(0.000001f, maximumPeak), -60.0f);
        graphics.setColour(juce::Colour(0xffaeb5bc));
        graphics.setFont(juce::FontOptions(8.0f, juce::Font::bold));
        graphics.drawText(juce::String(peakDb, 1) + " dB", body,
                          juce::Justification::centred, false);
    }

    if (!addSlotBounds_.isEmpty()) {
        graphics.setColour(juce::Colour(0xff24282c));
        graphics.fillRoundedRectangle(addSlotBounds_.toFloat(), 3.0f);
        graphics.setColour(juce::Colour(0xff555b61));
        graphics.drawRoundedRectangle(addSlotBounds_.toFloat(), 3.0f, 1.0f);
        graphics.setColour(juce::Colour(0xffff8a00));
        graphics.fillRect(addSlotBounds_.getX(), addSlotBounds_.getY(),
                          2, addSlotBounds_.getHeight());
    }

    if (!scrollbarBounds_.isEmpty()) {
        graphics.setColour(juce::Colour(0xff101215));
        graphics.fillRoundedRectangle(scrollbarBounds_.toFloat(), 3.0f);
        graphics.setColour(draggingScrollbar_ ? juce::Colour(0xffff8a00)
                                              : juce::Colour(0xff697077));
        graphics.fillRoundedRectangle(scrollbarThumbBounds_.toFloat(), 3.0f);
    }
}

void DeviceChainComponent::resized()
{
    auto area = getLocalBounds().reduced(5, 4);
    chainBounds_ = area;
    viewportBounds_ = area;
    scrollbarBounds_ = {};
    scrollbarThumbBounds_ = {};
    nodeFrame_ = {};
    addSlotBounds_ = {};
    pluginCards_.clear();
    for (auto& card : cards_)
        card = {};

    if (viewScope_ == ViewScope::module) {
        scrollOffset_ = 0.0f;
        maximumScrollOffset_ = 0.0f;
        nodeFrame_ = area;
        nodeDeviceSurface_.setBounds(area.reduced(4).withTrimmedTop(25));
        nodeDeviceSurface_.setVisible(true);
        openNodeButton_.setVisible(true);
        openNodeButton_.setBounds(area.getRight() - 37, area.getY() + 4,
                                  31, 19);
        openNodeButton_.toFront(false);
        addDeviceButton_.setVisible(false);
        for (auto* component : {
                 static_cast<juce::Component*>(&driveSlider_),
                 static_cast<juce::Component*>(&cutoffSlider_),
                 static_cast<juce::Component*>(&gainSlider_),
                 static_cast<juce::Component*>(&panSlider_),
                 static_cast<juce::Component*>(&removePluginButton_),
                 static_cast<juce::Component*>(&openPluginButton_),
                 static_cast<juce::Component*>(&movePluginLeftButton_),
                 static_cast<juce::Component*>(&movePluginRightButton_),
                 static_cast<juce::Component*>(&pluginSelector_) })
            component->setVisible(false);
        return;
    }

    const auto hasNodeSurface = !nodeInterfaceState_.deviceControls.empty()
        || std::ranges::any_of(
            nodeInterfaceState_.parameters,
            [](const ModulePublishedParameter& parameter) {
                return parameter.showInDevice;
            });
    const auto showLegacyDrive = hasDriveNode_ && !hasNodeSurface;
    const auto showLegacyFilter = hasLowPassNode_ && !hasNodeSurface;
    driveSlider_.setVisible(showLegacyDrive);
    cutoffSlider_.setVisible(showLegacyFilter);
    gainSlider_.setVisible(true);
    panSlider_.setVisible(true);
    const auto hasPlugin = !plugins_.empty();
    pluginSelector_.setVisible(false);
    removePluginButton_.setVisible(hasPlugin);
    openPluginButton_.setVisible(hasPlugin && pluginRunning_);
    movePluginLeftButton_.setVisible(hasPlugin);
    movePluginRightButton_.setVisible(hasPlugin);
    const auto gap = 4;
    const auto itemCount = 4 + static_cast<int>(plugins_.size())
        + (hasNodeSurface ? 1 : 0) + (showLegacyDrive ? 1 : 0)
        + (showLegacyFilter ? 1 : 0);
    // As wide as its controls need on one or two rows; the chain scrolls.
    const auto nodeSurfaceWidth = std::max(
        150, nodeDeviceSurface_.preferredCompactWidth() + 8);
    const auto desiredTotal = 126 + 188 + 148 + 44
        + (hasNodeSurface ? nodeSurfaceWidth : 0)
        + (showLegacyDrive ? 142 : 0)
        + (showLegacyFilter ? 162 : 0)
        + static_cast<int>(plugins_.size()) * 168;
    const auto contentWidth = desiredTotal
        + gap * std::max(0, itemCount - 1);
    if (contentWidth > area.getWidth()) {
        scrollbarBounds_ = area.removeFromBottom(9).reduced(3, 2);
        area.removeFromBottom(2);
    }
    viewportBounds_ = area;
    maximumScrollOffset_ = static_cast<float>(
        std::max(0, contentWidth - viewportBounds_.getWidth()));
    scrollOffset_ = juce::jlimit(0.0f, maximumScrollOffset_, scrollOffset_);
    if (!scrollbarBounds_.isEmpty()) {
        const auto visibleRatio = juce::jlimit(
            0.05f, 1.0f,
            static_cast<float>(viewportBounds_.getWidth())
                / static_cast<float>(std::max(1, contentWidth)));
        const auto thumbWidth = std::max(
            28, juce::roundToInt(static_cast<float>(scrollbarBounds_.getWidth())
                                 * visibleRatio));
        const auto travel = std::max(0, scrollbarBounds_.getWidth() - thumbWidth);
        const auto position = maximumScrollOffset_ > 0.0f
            ? juce::roundToInt(static_cast<float>(travel)
                               * scrollOffset_ / maximumScrollOffset_)
            : 0;
        scrollbarThumbBounds_ = {
            scrollbarBounds_.getX() + position, scrollbarBounds_.getY(),
            thumbWidth, scrollbarBounds_.getHeight()
        };
    }
    area.setX(area.getX() - juce::roundToInt(scrollOffset_));
    area.setWidth(contentWidth);
    const auto widthFor = [](const int desired) { return desired; };
    const auto take = [&area, gap](const int width) {
        const auto result = area.removeFromLeft(
            std::min(width, area.getWidth()));
        if (!area.isEmpty())
            area.removeFromLeft(std::min(gap, area.getWidth()));
        return result;
    };

    cards_[0] = take(widthFor(126));
    if (hasNodeSurface)
        nodeFrame_ = take(widthFor(nodeSurfaceWidth));
    if (showLegacyDrive)
        cards_[1] = take(widthFor(142));
    if (showLegacyFilter)
        cards_[2] = take(widthFor(162));
    for (std::size_t index = 0; index < plugins_.size(); ++index)
        pluginCards_.push_back(take(widthFor(168)));
    cards_[4] = take(widthFor(188));
    cards_[5] = take(widthFor(148));
    addSlotBounds_ = take(widthFor(44));

    if (ensureSelectedPluginVisible_
        && selectedPluginIndex_ < pluginCards_.size()) {
        const auto selected = pluginCards_[selectedPluginIndex_];
        auto newOffset = scrollOffset_;
        if (selected.getX() < viewportBounds_.getX())
            newOffset -= static_cast<float>(viewportBounds_.getX()
                                             - selected.getX());
        else if (selected.getRight() > viewportBounds_.getRight())
            newOffset += static_cast<float>(selected.getRight()
                                             - viewportBounds_.getRight());
        ensureSelectedPluginVisible_ = false;
        newOffset = juce::jlimit(0.0f, maximumScrollOffset_, newOffset);
        if (std::abs(newOffset - scrollOffset_) > 0.5f) {
            scrollOffset_ = newOffset;
            resized();
            return;
        }
    }

    nodeDeviceSurface_.setVisible(hasNodeSurface);
    openNodeButton_.setVisible(hasNodeSurface);
    if (hasNodeSurface) {
        nodeDeviceSurface_.setBounds(
            nodeFrame_.reduced(4).withTrimmedTop(25));
        openNodeButton_.setBounds(nodeFrame_.getRight() - 37,
                                  nodeFrame_.getY() + 4, 31, 19);
        openNodeButton_.toFront(false);
    }
    addDeviceButton_.setVisible(true);
    addDeviceButton_.setBounds(addSlotBounds_.reduced(6).withSizeKeepingCentre(
        std::max(20, addSlotBounds_.getWidth() - 12), 28));

    driveSlider_.setBounds(cards_[1].reduced(10).withTrimmedTop(29));
    cutoffSlider_.setBounds(cards_[2].reduced(10).withTrimmedTop(29));
    auto mixer = cards_[4].reduced(7).withTrimmedTop(29);
    gainSlider_.setBounds(mixer.removeFromLeft(mixer.getWidth() / 2).reduced(2, 0));
    panSlider_.setBounds(mixer.reduced(2, 0));
    if (hasPlugin && selectedPluginIndex_ < pluginCards_.size()) {
        auto pluginButtons = pluginCards_[selectedPluginIndex_]
                                 .reduced(7).removeFromBottom(23);
        const auto buttonWidth = juce::jmax(1, pluginButtons.getWidth() / 4);
        movePluginLeftButton_.setBounds(
            pluginButtons.removeFromLeft(buttonWidth).reduced(1, 0));
        movePluginRightButton_.setBounds(
            pluginButtons.removeFromLeft(buttonWidth).reduced(1, 0));
        openPluginButton_.setBounds(
            pluginButtons.removeFromLeft(buttonWidth).reduced(1, 0));
        removePluginButton_.setBounds(pluginButtons.reduced(1, 0));
    }
}

void DeviceChainComponent::mouseDown(const juce::MouseEvent& event)
{
    if (viewScope_ != ViewScope::track)
        return;
    if (scrollbarBounds_.contains(event.getPosition())) {
        draggingScrollbar_ = true;
        scrollbarDragStartX_ = event.x;
        scrollbarDragStartOffset_ = scrollOffset_;
        if (!scrollbarThumbBounds_.contains(event.getPosition())) {
            const auto travel = std::max(
                1, scrollbarBounds_.getWidth()
                       - scrollbarThumbBounds_.getWidth());
            const auto centredX = event.x - scrollbarBounds_.getX()
                - scrollbarThumbBounds_.getWidth() / 2;
            setScrollOffset(maximumScrollOffset_
                * static_cast<float>(juce::jlimit(0, travel, centredX))
                / static_cast<float>(travel));
            scrollbarDragStartX_ = event.x;
            scrollbarDragStartOffset_ = scrollOffset_;
        }
        return;
    }
    for (std::size_t index = 0; index < pluginCards_.size(); ++index) {
        if (!pluginCards_[index].contains(event.getPosition()))
            continue;
        selectedPluginIndex_ = index;
        draggedPluginIndex_ = index;
        grabKeyboardFocus();
        if (pluginSelectHandler_)
            pluginSelectHandler_(index);
        setPluginChain(plugins_, index);
        if (event.mods.isPopupMenu())
            showPluginMenu(index);
        return;
    }
}

void DeviceChainComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (draggingScrollbar_) {
        const auto travel = std::max(
            1, scrollbarBounds_.getWidth() - scrollbarThumbBounds_.getWidth());
        const auto delta = static_cast<float>(event.x - scrollbarDragStartX_)
            * maximumScrollOffset_ / static_cast<float>(travel);
        setScrollOffset(scrollbarDragStartOffset_ + delta);
        return;
    }
    if (draggedPluginIndex_ >= pluginCards_.size()
        || event.getDistanceFromDragStart() < 7)
        return;

    const auto current = draggedPluginIndex_;
    if (current > 0
        && event.x < pluginCards_[current - 1].getCentreX()) {
        draggedPluginIndex_ = current - 1;
        if (pluginMoveHandler_)
            pluginMoveHandler_(current, -1);
        return;
    }
    if (current + 1 < pluginCards_.size()
        && event.x > pluginCards_[current + 1].getCentreX()) {
        draggedPluginIndex_ = current + 1;
        if (pluginMoveHandler_)
            pluginMoveHandler_(current, 1);
    }
}

void DeviceChainComponent::mouseUp(const juce::MouseEvent&)
{
    draggingScrollbar_ = false;
    draggedPluginIndex_ = static_cast<std::size_t>(-1);
    repaint();
}

void DeviceChainComponent::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (nodeFrame_.contains(event.getPosition())) {
        if (nodeOpenHandler_)
            nodeOpenHandler_();
        return;
    }
    for (std::size_t index = 0; index < pluginCards_.size(); ++index) {
        if (pluginCards_[index].contains(event.getPosition())
            && plugins_[index].running && pluginOpenHandler_) {
            pluginOpenHandler_(index);
            return;
        }
    }
}

void DeviceChainComponent::mouseWheelMove(
    const juce::MouseEvent& event,
    const juce::MouseWheelDetails& wheel)
{
    if (viewScope_ != ViewScope::track || maximumScrollOffset_ <= 0.0f
        || !getLocalBounds().contains(event.getPosition())) {
        juce::Component::mouseWheelMove(event, wheel);
        return;
    }
    const auto movement = std::abs(wheel.deltaX) > std::abs(wheel.deltaY)
        ? wheel.deltaX : wheel.deltaY;
    setScrollOffset(scrollOffset_ - movement * 260.0f);
}

bool DeviceChainComponent::keyPressed(const juce::KeyPress& key)
{
    if (plugins_.empty() || selectedPluginIndex_ >= plugins_.size())
        return false;
    if (key.getKeyCode() == juce::KeyPress::deleteKey
        || key.getKeyCode() == juce::KeyPress::backspaceKey) {
        if (pluginRemoveHandler_)
            pluginRemoveHandler_(selectedPluginIndex_);
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::returnKey) {
        if (plugins_[selectedPluginIndex_].running && pluginOpenHandler_)
            pluginOpenHandler_(selectedPluginIndex_);
        return true;
    }
    if (key.getModifiers().isAltDown()
        && (key.getKeyCode() == juce::KeyPress::leftKey
            || key.getKeyCode() == juce::KeyPress::rightKey)) {
        const auto direction = key.getKeyCode() == juce::KeyPress::leftKey
            ? -1 : 1;
        if (pluginMoveHandler_)
            pluginMoveHandler_(selectedPluginIndex_, direction);
        return true;
    }
    return false;
}

void DeviceChainComponent::showPluginMenu(const std::size_t pluginIndex)
{
    if (pluginIndex >= plugins_.size())
        return;
    juce::PopupMenu menu;
    menu.addItem(1, tr("device.openPlugin"), plugins_[pluginIndex].running);
    menu.addSeparator();
    menu.addItem(2, tr("device.movePluginLeft"), pluginIndex > 0);
    menu.addItem(3, tr("device.movePluginRight"),
                 pluginIndex + 1 < plugins_.size());
    menu.addSeparator();
    menu.addItem(4, tr("device.removePlugin"));
    const juce::Component::SafePointer<DeviceChainComponent> safe(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(this),
        [safe, pluginIndex](const int result) {
            if (safe == nullptr || result == 0)
                return;
            if (result == 1 && safe->pluginOpenHandler_)
                safe->pluginOpenHandler_(pluginIndex);
            else if ((result == 2 || result == 3)
                     && safe->pluginMoveHandler_)
                safe->pluginMoveHandler_(pluginIndex,
                                         result == 2 ? -1 : 1);
            else if (result == 4 && safe->pluginRemoveHandler_)
                safe->pluginRemoveHandler_(pluginIndex);
        });
}

void DeviceChainComponent::setScrollOffset(const float offset)
{
    const auto clamped = juce::jlimit(0.0f, maximumScrollOffset_, offset);
    if (std::abs(clamped - scrollOffset_) < 0.25f)
        return;
    scrollOffset_ = clamped;
    resized();
    repaint();
}

void DeviceChainComponent::dragEffectSliderForTest(const double driveDb)
{
    driveSlider_.setValue(driveDb, juce::sendNotificationSync);
}

void DeviceChainComponent::dragMixerSliderForTest(const double gainDb)
{
    gainSlider_.setValue(gainDb, juce::sendNotificationSync);
}

void DeviceChainComponent::turnNodeKnobForTest(const juce::String& key,
                                               const double value)
{
    if (nodeDeviceSurface_.setParameterValue(key, value)
        && nodeParameterChangedHandler_)
        nodeParameterChangedHandler_(key, value);
}

void DeviceChainComponent::notifyEffects()
{
    if (effectChangedHandler_)
        effectChangedHandler_(static_cast<float>(driveSlider_.getValue()),
                              static_cast<float>(cutoffSlider_.getValue()));
}

void DeviceChainComponent::notifyMixer()
{
    if (mixerChangedHandler_)
        mixerChangedHandler_(static_cast<float>(gainSlider_.getValue()),
                             static_cast<float>(panSlider_.getValue()));
}

} // namespace cydaw::app
