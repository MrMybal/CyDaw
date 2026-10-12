#include "MixerComponent.h"
#include "PaintProfile.h"
#include "CyDawLookAndFeel.h"
#include "Localization.h"
#include "MixerStripDrawing.h"

#include <algorithm>
#include <cmath>

namespace cydaw::app {
using mixer_strip::dbForFaderAmount;
using mixer_strip::drawFaderAndMeter;
using mixer_strip::faderAmountForDb;
namespace {
constexpr float minGainDb = -60.0f;
constexpr float maxGainDb = 12.0f;
constexpr int headerHeight = 30;
constexpr int deviceRowHeight = 15;
constexpr int ioHeight = 30;
constexpr int panHeight = 44;
constexpr int valueHeight = 16;
constexpr int buttonHeight = 20;
// The fader column: dB scale, fader, gap, stereo meter.
constexpr int scaleWidth = 26;
constexpr int faderWidth = 12;
constexpr int meterGap = 8;
constexpr int meterWidth = 20;
constexpr int faderColumnWidth
    = scaleWidth + faderWidth + meterGap + meterWidth;
constexpr juce::uint32 selectionColour = 0xffff8a00;
constexpr juce::uint32 masterColour = 0xff72d66b;

juce::String panText(const float pan)
{
    const auto amount = juce::roundToInt(std::abs(pan) * 100.0f);
    if (amount == 0)
        return "C";
    return (pan < 0.0f ? "L " : "R ") + juce::String(amount);
}

juce::MouseEvent syntheticMouseEvent(
    juce::Component& target, const juce::Point<float> position,
    const int clicks,
    const juce::ModifierKeys modifiers
    = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
    const juce::Point<float> dragStart = {})
{
    const auto now = juce::Time::getCurrentTime();
    return { juce::Desktop::getInstance().getMainMouseSource(), position,
             modifiers, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target, now,
             dragStart == juce::Point<float> {} ? position : dragStart, now,
             clicks, false };
}
} // namespace

MixerComponent::MixerComponent()
{
    setOpaque(true);
    addChildComponent(scrollBar_);
    scrollBar_.setAutoHide(false);
    scrollBar_.addListener(this);
    addChildComponent(gainValueEditor_);
    gainValueEditor_.setJustification(juce::Justification::centred);
    gainValueEditor_.setInputRestrictions(8, "-+0123456789.,");
    gainValueEditor_.setColour(juce::TextEditor::backgroundColourId,
                               juce::Colour(0xff111419));
    gainValueEditor_.setColour(juce::TextEditor::outlineColourId,
                               juce::Colour(selectionColour));
    gainValueEditor_.setColour(juce::TextEditor::textColourId,
                               juce::Colour(0xfff2f4f7));
    gainValueEditor_.onReturnKey = [this] { finishGainValueEdit(true); };
    gainValueEditor_.onEscapeKey = [this] { finishGainValueEdit(false); };
    gainValueEditor_.onFocusLost = [this] { finishGainValueEdit(true); };
    refreshTranslations();
}

MixerComponent::~MixerComponent()
{
    scrollBar_.removeListener(this);
}

bool MixerComponent::selfTestInputRouting()
{
    MixerComponent mixer;
    mixer.setBounds(0, 0, 1'000, 600);
    Channel channel;
    channel.name = "Drums";
    channel.gainDb = 0.0f;
    auto second = channel;
    second.name = "Keys";
    second.midi = true;
    second.pan = 0.5f;
    auto third = channel;
    third.name = "Bass";
    mixer.setChannels({ channel, second, third });

    std::optional<std::size_t> mutedChannel;
    auto mutedState = false;
    std::optional<std::size_t> mixedChannel;
    auto mixedGainDb = 0.0f;
    auto mixedPan = 1.0f;
    auto masterGainDb = 0.0f;
    mixer.setMuteHandler([&](const std::size_t index, const bool muted) {
        mutedChannel = index;
        mutedState = muted;
    });
    mixer.setMixHandler(
        [&](const std::size_t index, const float gainDb, const float pan) {
            mixedChannel = index;
            mixedGainDb = gainDb;
            mixedPan = pan;
        });
    mixer.setMasterGainHandler(
        [&](const float gainDb) { masterGainDb = gainDb; });

    // Geometry: the master fader is addressed by the channel count, and it
    // is hit-tested before the strips it may overlap.
    const auto masterHit = mixer.hitAt(mixer.faderBounds(3).getCentre());
    if (masterHit.kind != HitKind::gain || masterHit.channel != 3)
        return false;
    if (mixer.hitAt(mixer.faderBounds(1).getCentre()).channel != 1)
        return false;

    // Mute button on the first strip.
    mixer.mouseDown(syntheticMouseEvent(
        mixer, mixer.buttonBounds(0)[0].getCentre().toFloat(), 1));
    if (mutedChannel != 0 || !mutedState || !mixer.channels_[0].muted)
        return false;

    // Dragging the third fader upwards raises its gain, relative to the
    // thumb rather than jumping to the pointer.
    const auto fader = mixer.faderBounds(2).getCentre().toFloat();
    mixer.mouseDown(syntheticMouseEvent(mixer, fader, 1));
    mixer.mouseDrag(
        syntheticMouseEvent(mixer, fader.translated(0.0f, -30.0f), 1));
    mixer.mouseUp(
        syntheticMouseEvent(mixer, fader.translated(0.0f, -30.0f), 1));
    if (mixedChannel != 2 || mixedGainDb <= 0.0f || mixedGainDb > maxGainDb)
        return false;

    // Double-clicking a pan knob recentres it.
    mixer.mouseDoubleClick(syntheticMouseEvent(
        mixer, mixer.panBounds(1).getCentre().toFloat(), 2));
    if (mixedChannel != 1 || std::abs(mixedPan) > 0.0001f)
        return false;

    // The master fader reports through its own handler.
    const auto master = mixer.faderBounds(3).getCentre().toFloat();
    mixer.mouseDown(syntheticMouseEvent(mixer, master, 1));
    mixer.mouseDrag(
        syntheticMouseEvent(mixer, master.translated(0.0f, 40.0f), 1));
    mixer.mouseUp(
        syntheticMouseEvent(mixer, master.translated(0.0f, 40.0f), 1));
    if (masterGainDb >= 0.0f || mixedChannel != 1)
        return false;

    // Dragging a header onto another strip of the same kind moves it.
    std::optional<std::pair<std::size_t, std::size_t>> moved;
    mixer.setMoveHandler([&](const std::size_t from, const std::size_t to) {
        moved = std::make_pair(from, to);
    });
    const auto header = mixer.headerBounds(0).getCentre().toFloat();
    const auto destination = mixer.headerBounds(2).getCentre().toFloat();
    mixer.mouseDown(syntheticMouseEvent(mixer, header, 1));
    mixer.mouseDrag(syntheticMouseEvent(
        mixer, destination, 1,
        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), header));
    mixer.mouseUp(syntheticMouseEvent(mixer, destination, 1));
    if (moved != std::make_pair(std::size_t { 0 }, std::size_t { 2 }))
        return false;

    // Right-click asks for the track menu; a click on the I/O row asks for
    // the input menu, but only where an input can be chosen.
    std::optional<std::size_t> contextChannel;
    std::optional<std::size_t> inputChannel;
    mixer.setContextHandler([&](const std::size_t index) { contextChannel = index; });
    mixer.setInputHandler([&](const std::size_t index) { inputChannel = index; });
    mixer.mouseDown(syntheticMouseEvent(
        mixer, mixer.headerBounds(1).getCentre().toFloat(), 1,
        juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier
                           | juce::ModifierKeys::popupMenuClickModifier)));
    mixer.channels_[0].inputSelectable = true;
    mixer.mouseDown(syntheticMouseEvent(
        mixer, mixer.ioBounds(0).getCentre().toFloat(), 1));
    mixer.mouseDown(syntheticMouseEvent(
        mixer, mixer.ioBounds(1).getCentre().toFloat(), 1));
    return contextChannel == 1 && inputChannel == 0;
}

void MixerComponent::setChannels(std::vector<Channel> channels)
{
    // Meters keep ticking across a rebuild; the rebuild only carries state.
    for (std::size_t index = 0;
         index < std::min(channels.size(), channels_.size()); ++index) {
        channels[index].peak = channels_[index].peak;
        channels[index].rms = channels_[index].rms;
    }
    channels_ = std::move(channels);
    if (dragHit_.channel && *dragHit_.channel > channels_.size())
        dragHit_ = {};
    if (hovered_.channel && *hovered_.channel > channels_.size())
        hovered_ = {};
    if (editedGainChannel_ && *editedGainChannel_ > channels_.size())
        finishGainValueEdit(false);
    updateScrollRange();
    const auto selected = std::find_if(
        channels_.begin(), channels_.end(),
        [](const Channel& value) { return value.selected; });
    const auto selectedIndex
        = selected == channels_.end()
              ? std::optional<std::size_t> {}
              : std::optional<std::size_t>(static_cast<std::size_t>(
                    std::distance(channels_.begin(), selected)));
    if (selectedIndex && selectedIndex != lastSelected_)
        ensureChannelVisible(*selectedIndex);
    lastSelected_ = selectedIndex;
    repaint();
}

void MixerComponent::setChannelMeterLevels(const std::size_t channelIndex,
                                           const std::array<float, 2>& peak,
                                           const std::array<float, 2>& rms)
{
    if (channelIndex >= channels_.size())
        return;
    channels_[channelIndex].peak = peak;
    channels_[channelIndex].rms = rms;
    if (isVisible())
        repaint(layoutFor(channelIndex).meter.expanded(1));
}

void MixerComponent::setMasterMeterLevels(const std::array<float, 2>& peak,
                                          const std::array<float, 2>& rms)
{
    masterPeak_ = peak;
    masterRms_ = rms;
    if (isVisible())
        repaint(layoutFor(channels_.size()).meter.expanded(1));
}

void MixerComponent::setMasterGain(const float gainDb)
{
    masterGainDb_ = juce::jlimit(minGainDb, maxGainDb, gainDb);
    repaint(masterStripBounds());
}

void MixerComponent::refreshTranslations()
{
    masterTitle_ = tr("track.master");
    masterBusText_ = tr("track.masterBus");
    noDevicesText_ = tr("mixer.noDevices");
    emptyText_ = tr("mixer.empty");
    help_ = tr("mixer.help");
    gainHelp_ = tr("mixer.gainHelp");
    gainValueHelp_ = tr("launcher.gainValueHelp");
    panHelp_ = tr("mixer.panHelp");
    muteHelp_ = tr("mixer.muteHelp");
    soloHelp_ = tr("mixer.soloHelp");
    armHelp_ = tr("mixer.armHelp");
    devicesHelp_ = tr("mixer.devicesHelp");
    addTrackHelp_ = tr("midi.addTrackHelp");
    headerHelp_ = tr("mixer.headerHelp");
    inputHelp_ = tr("mixer.inputHelp");
    setTooltip(help_);
    repaint();
}

void MixerComponent::setMixHandler(
    std::function<void(std::size_t, float, float)> handler)
{
    mixHandler_ = std::move(handler);
}

void MixerComponent::setMuteHandler(
    std::function<void(std::size_t, bool)> handler)
{
    muteHandler_ = std::move(handler);
}

void MixerComponent::setSoloHandler(
    std::function<void(std::size_t, bool)> handler)
{
    soloHandler_ = std::move(handler);
}

void MixerComponent::setArmHandler(
    std::function<void(std::size_t, bool)> handler)
{
    armHandler_ = std::move(handler);
}

void MixerComponent::setSelectHandler(std::function<void(std::size_t)> handler)
{
    selectHandler_ = std::move(handler);
}

void MixerComponent::setDevicesOpenHandler(
    std::function<void(std::size_t)> handler)
{
    devicesOpenHandler_ = std::move(handler);
}

void MixerComponent::setMasterGainHandler(std::function<void(float)> handler)
{
    masterGainHandler_ = std::move(handler);
}

void MixerComponent::setAddTrackHandler(std::function<void()> handler)
{
    addTrackHandler_ = std::move(handler);
}

void MixerComponent::setMoveHandler(
    std::function<void(std::size_t, std::size_t)> handler)
{
    moveHandler_ = std::move(handler);
}

void MixerComponent::setContextHandler(std::function<void(std::size_t)> handler)
{
    contextHandler_ = std::move(handler);
}

void MixerComponent::setInputHandler(std::function<void(std::size_t)> handler)
{
    inputHandler_ = std::move(handler);
}

// ---------------------------------------------------------------- geometry

bool MixerComponent::isMaster(const std::size_t channelIndex) const
{
    return channelIndex >= channels_.size();
}

juce::Rectangle<int> MixerComponent::channelArea() const
{
    return getLocalBounds().withTrimmedRight(masterWidth + 1)
        .withTrimmedBottom(scrollBarHeight);
}

juce::Rectangle<int> MixerComponent::masterStripBounds() const
{
    const auto bounds = getLocalBounds().withTrimmedBottom(scrollBarHeight);
    return bounds.withLeft(bounds.getRight() - masterWidth).reduced(2, 4);
}

juce::Rectangle<int> MixerComponent::stripBounds(
    const std::size_t channelIndex) const
{
    if (isMaster(channelIndex))
        return masterStripBounds();
    const auto area = channelArea();
    const auto x = area.getX() + static_cast<int>(channelIndex) * stripWidth
                   - juce::roundToInt(scrollBar_.getCurrentRangeStart());
    return juce::Rectangle<int>(x, area.getY(), stripWidth, area.getHeight())
        .reduced(2, 4);
}

juce::Rectangle<int> MixerComponent::addTrackBounds() const
{
    const auto area = channelArea();
    const auto x = area.getX()
                   + static_cast<int>(channels_.size()) * stripWidth
                   - juce::roundToInt(scrollBar_.getCurrentRangeStart());
    return juce::Rectangle<int>(x + 4, area.getY() + 4, addStripWidth - 8,
                                headerHeight - 2);
}

int MixerComponent::deviceRows() const
{
    // Short mixers give the rows to the faders instead.
    return juce::jlimit(0, 5, (getHeight() - 280) / 34);
}

MixerComponent::StripLayout MixerComponent::layoutFor(
    const std::size_t channelIndex) const
{
    auto inner = stripBounds(channelIndex).reduced(3, 4);
    StripLayout layout;
    layout.header = inner.removeFromTop(headerHeight);
    const auto rows = deviceRows();
    layout.devices = rows > 0
                         ? inner.removeFromTop(rows * deviceRowHeight + 6)
                               .reduced(0, 3)
                         : juce::Rectangle<int> {};
    layout.io = inner.removeFromTop(ioHeight).reduced(0, 2);
    layout.pan = inner.removeFromTop(panHeight);
    layout.gainValue = inner.removeFromTop(valueHeight + 4).reduced(4, 2);
    layout.buttons = inner.removeFromBottom(buttonHeight + 4).reduced(0, 2);
    const auto region = inner.reduced(0, 8);
    const auto left = region.getX()
                      + std::max(0, (region.getWidth() - faderColumnWidth) / 2);
    layout.fader = { left + scaleWidth, region.getY(), faderWidth,
                     region.getHeight() };
    layout.meter = { layout.fader.getRight() + meterGap, region.getY(),
                     meterWidth, region.getHeight() };
    return layout;
}

juce::Rectangle<int> MixerComponent::faderBounds(
    const std::size_t channelIndex) const
{
    return layoutFor(channelIndex).fader;
}

juce::Rectangle<int> MixerComponent::headerBounds(
    const std::size_t channelIndex) const
{
    return layoutFor(channelIndex).header;
}

juce::Rectangle<int> MixerComponent::ioBounds(
    const std::size_t channelIndex) const
{
    return layoutFor(channelIndex).io;
}

juce::Rectangle<int> MixerComponent::panBounds(
    const std::size_t channelIndex) const
{
    const auto pan = layoutFor(channelIndex).pan;
    return juce::Rectangle<int>(24, 24).withCentre(
        { pan.getCentreX(), pan.getY() + 14 });
}

std::array<juce::Rectangle<int>, 3> MixerComponent::buttonBounds(
    const std::size_t channelIndex) const
{
    const auto row = layoutFor(channelIndex).buttons;
    const auto width = std::max(8, (row.getWidth() - 4) / 3);
    std::array<juce::Rectangle<int>, 3> buttons;
    for (std::size_t index = 0; index < buttons.size(); ++index)
        buttons[index] = { row.getX() + static_cast<int>(index) * (width + 2),
                           row.getY(), width, row.getHeight() };
    return buttons;
}

MixerComponent::Hit MixerComponent::hitAt(const juce::Point<int> position) const
{
    const auto test = [this, position](const std::size_t index)
        -> std::optional<Hit> {
        if (!stripBounds(index).contains(position))
            return std::nullopt;
        const auto layout = layoutFor(index);
        if (!isMaster(index)) {
            const auto buttons = buttonBounds(index);
            if (buttons[0].contains(position))
                return Hit { index, HitKind::mute };
            if (buttons[1].contains(position))
                return Hit { index, HitKind::solo };
            if (buttons[2].contains(position))
                return Hit { index, HitKind::arm };
            if (panBounds(index).expanded(4).contains(position))
                return Hit { index, HitKind::pan };
            if (layout.devices.contains(position))
                return Hit { index, HitKind::devices };
            if (layout.io.contains(position))
                return Hit { index, HitKind::io };
            if (layout.header.contains(position))
                return Hit { index, HitKind::header };
        }
        if (layout.gainValue.contains(position))
            return Hit { index, HitKind::gainValue };
        if (layout.fader.getUnion(layout.meter).expanded(8, 4)
                .contains(position))
            return Hit { index, HitKind::gain };
        return Hit { index, HitKind::strip };
    };
    // The master strip is painted over the channels scrolled beneath it.
    if (const auto hit = test(channels_.size()))
        return *hit;
    if (channelArea().contains(position)) {
        if (addTrackBounds().contains(position))
            return Hit { std::nullopt, HitKind::addTrack };
        for (std::size_t index = 0; index < channels_.size(); ++index)
            if (const auto hit = test(index))
                return *hit;
    }
    return {};
}

juce::String MixerComponent::tooltipFor(const Hit& hit) const
{
    switch (hit.kind) {
    case HitKind::gain: return gainHelp_;
    case HitKind::gainValue: return gainValueHelp_;
    case HitKind::pan: return panHelp_;
    case HitKind::mute: return muteHelp_;
    case HitKind::solo: return soloHelp_;
    case HitKind::arm: return armHelp_;
    case HitKind::devices: return devicesHelp_;
    case HitKind::addTrack: return addTrackHelp_;
    case HitKind::header: return headerHelp_;
    case HitKind::io:
        return hit.channel && !isMaster(*hit.channel)
                       && channels_[*hit.channel].inputSelectable
                   ? inputHelp_ : help_;
    case HitKind::strip:
    case HitKind::none:
        break;
    }
    return help_;
}

float MixerComponent::gainDbOf(const std::size_t channelIndex) const
{
    return isMaster(channelIndex) ? masterGainDb_
                                  : channels_[channelIndex].gainDb;
}

// ---------------------------------------------------------------- painting

void MixerComponent::paint(juce::Graphics& graphics)
{
    const paint_profile::Scoped paintScope(paint_profile::counters().mixer);
    graphics.fillAll(juce::Colour(0xff15181c));
    const auto area = channelArea();
    if (channels_.empty()) {
        graphics.setColour(juce::Colour(0xff6b737d));
        graphics.setFont(CyDawLookAndFeel::labelFont(11.0f));
        graphics.drawFittedText(emptyText_,
                                area.withTrimmedLeft(addStripWidth).reduced(20),
                                juce::Justification::centred, 2);
    }
    graphics.saveState();
    graphics.reduceClipRegion(area);
    const auto clip = graphics.getClipBounds();
    for (std::size_t index = 0; index < channels_.size(); ++index)
        if (stripBounds(index).intersects(clip))
            paintStrip(graphics, index);
    if (dragHit_.kind == HitKind::header && dragHit_.channel && dragTarget_
        && *dragTarget_ != *dragHit_.channel
        && *dragTarget_ < channels_.size()) {
        const auto target = stripBounds(*dragTarget_);
        graphics.setColour(juce::Colour(0xff58c9ff));
        graphics.fillRect(target.getX() - 3, target.getY(), 4,
                          target.getHeight());
        graphics.setColour(juce::Colour(0xffbcecff));
        graphics.drawRoundedRectangle(target.toFloat().reduced(0.5f), 3.0f,
                                      1.5f);
    }
    const auto addTrack = addTrackBounds();
    if (addTrack.intersects(clip)) {
        const auto hovered = hovered_.kind == HitKind::addTrack;
        graphics.setColour(juce::Colour(hovered ? 0xff343b46 : 0xff20252d));
        graphics.fillRoundedRectangle(addTrack.toFloat(), 3.0f);
        graphics.setColour(juce::Colour(hovered ? 0xffffad45 : 0xff8f99a8));
        graphics.setFont(CyDawLookAndFeel::labelFont(17.0f, true));
        graphics.drawText("+", addTrack, juce::Justification::centred, false);
    }
    graphics.restoreState();
    graphics.setColour(juce::Colour(0xff2b3138));
    graphics.fillRect(area.getRight(), 0, 1, getHeight());
    paintStrip(graphics, channels_.size());
}

void MixerComponent::paintStrip(juce::Graphics& graphics,
                                const std::size_t channelIndex)
{
    const auto master = isMaster(channelIndex);
    const Channel* channel = master ? nullptr : &channels_[channelIndex];
    const auto strip = stripBounds(channelIndex);
    const auto layout = layoutFor(channelIndex);
    const auto selected = channel != nullptr && channel->selected;
    const auto colour = master ? juce::Colour(masterColour) : channel->colour;
    const auto accent = master ? colour
                        : selected ? juce::Colour(selectionColour)
                                   : juce::Colour(0xff4f9ed8);
    const auto hoveredKind = hovered_.channel == channelIndex
                                 ? hovered_.kind : HitKind::none;

    graphics.setColour(juce::Colour(master ? 0xff1e232a
                                    : selected ? 0xff272c33 : 0xff1c2026));
    graphics.fillRoundedRectangle(strip.toFloat(), 3.0f);
    if (selected) {
        graphics.setColour(juce::Colour(selectionColour));
        graphics.drawRoundedRectangle(strip.toFloat().reduced(0.5f), 3.0f,
                                      1.0f);
    }

    // Header: colour bar, name, and what kind of channel this is.
    graphics.setColour(colour);
    graphics.fillRect(layout.header.getX(), layout.header.getY(),
                      layout.header.getWidth(), 3);
    graphics.setColour(juce::Colour(0xffe3e7ec));
    graphics.setFont(CyDawLookAndFeel::labelFont(10.5f, true));
    graphics.drawFittedText(master ? masterTitle_ : channel->name,
                            layout.header.withTrimmedTop(6).withHeight(14),
                            juce::Justification::centred, 1, 0.7f);
    graphics.setColour(juce::Colour(0xff77808a));
    graphics.setFont(CyDawLookAndFeel::labelFont(7.5f));
    graphics.drawText(master ? "OUT" : channel->midi ? "MIDI" : "AUDIO",
                      layout.header.withTrimmedTop(20),
                      juce::Justification::centred, false);

    // Devices: the chain summary, or where the master's bus label goes.
    if (!layout.devices.isEmpty()) {
        graphics.setColour(juce::Colour(0xff171b20));
        graphics.fillRoundedRectangle(layout.devices.toFloat(), 2.0f);
        if (master) {
            graphics.setColour(juce::Colour(0xff8d97a2));
            graphics.setFont(CyDawLookAndFeel::labelFont(8.0f, true));
            graphics.drawFittedText(masterBusText_, layout.devices.reduced(5),
                                    juce::Justification::centred, 3, 0.7f);
        } else if (channel->devices.empty()) {
            graphics.setColour(juce::Colour(0xff525a63));
            graphics.setFont(CyDawLookAndFeel::labelFont(8.0f));
            graphics.drawFittedText(noDevicesText_, layout.devices.reduced(5),
                                    juce::Justification::centred, 2, 0.7f);
        } else {
            auto rows = layout.devices.reduced(3, 3);
            const auto limit = static_cast<std::size_t>(deviceRows());
            for (std::size_t index = 0;
                 index < channel->devices.size() && index < limit; ++index) {
                const auto row = rows.removeFromTop(deviceRowHeight)
                                     .withTrimmedBottom(2);
                graphics.setColour(juce::Colour(0xff262c34));
                graphics.fillRoundedRectangle(row.toFloat(), 2.0f);
                graphics.setColour(colour);
                graphics.fillEllipse(static_cast<float>(row.getX() + 4),
                                     static_cast<float>(row.getCentreY() - 2),
                                     4.0f, 4.0f);
                graphics.setColour(juce::Colour(0xffd3d8de));
                graphics.setFont(CyDawLookAndFeel::labelFont(8.2f));
                graphics.drawFittedText(channel->devices[index],
                                        row.withTrimmedLeft(11).reduced(1, 0),
                                        juce::Justification::centredLeft, 1,
                                        0.7f);
            }
        }
        if (hoveredKind == HitKind::devices) {
            graphics.setColour(accent.withAlpha(0.6f));
            graphics.drawRoundedRectangle(layout.devices.toFloat(), 2.0f,
                                          1.0f);
        }
    }

    // I/O: where the channel listens, and where it goes.
    if (!master) {
        graphics.setColour(juce::Colour(0xff1a1e24));
        graphics.fillRoundedRectangle(layout.io.toFloat(), 2.0f);
        auto lines = layout.io.reduced(5, 1);
        const auto inputLine = lines.removeFromTop(lines.getHeight() / 2);
        graphics.setFont(CyDawLookAndFeel::labelFont(8.0f));
        graphics.setColour(channel->inputSelectable
                               ? juce::Colour(hoveredKind == HitKind::io
                                                  ? 0xffffad45 : 0xffc3cad2)
                               : juce::Colour(0xff8d97a2));
        graphics.drawFittedText(channel->input, inputLine,
                                juce::Justification::centredLeft, 1, 0.7f);
        graphics.setColour(juce::Colour(0xff8d97a2));
        graphics.drawFittedText(channel->output, lines,
                                juce::Justification::centredLeft, 1, 0.7f);
        if (hoveredKind == HitKind::io && channel->inputSelectable) {
            graphics.setColour(accent.withAlpha(0.6f));
            graphics.drawRoundedRectangle(layout.io.toFloat(), 2.0f, 1.0f);
        }
    }

    // Pan: a knob with its value arc, and the value spelled out under it.
    if (!master) {
        const auto knob = panBounds(channelIndex).toFloat();
        graphics.setColour(juce::Colour(0xff101318));
        graphics.fillEllipse(knob);
        graphics.setColour(juce::Colour(hoveredKind == HitKind::pan
                                            ? 0xff8b939c : 0xff5d646d));
        graphics.drawEllipse(knob, 1.2f);
        const auto angle = juce::jmap(channel->pan, -1.0f, 1.0f, -2.35f,
                                      2.35f);
        if (std::abs(channel->pan) > 0.001f) {
            juce::Path arc;
            arc.addCentredArc(knob.getCentreX(), knob.getCentreY(),
                              knob.getWidth() * 0.5f + 2.5f,
                              knob.getHeight() * 0.5f + 2.5f, 0.0f,
                              std::min(0.0f, angle), std::max(0.0f, angle),
                              true);
            graphics.setColour(accent);
            graphics.strokePath(arc, juce::PathStrokeType(2.0f));
        }
        const auto centre = knob.getCentre();
        graphics.setColour(juce::Colour(0xfff0f2f4));
        graphics.drawLine(centre.x, centre.y,
                          centre.x + std::sin(angle) * knob.getWidth() * 0.38f,
                          centre.y - std::cos(angle) * knob.getHeight() * 0.38f,
                          1.6f);
        graphics.setColour(juce::Colour(0xffb9c1ca));
        graphics.setFont(CyDawLookAndFeel::labelFont(8.0f));
        graphics.drawText(panText(channel->pan),
                          layout.pan.withTrimmedTop(29).withHeight(12),
                          juce::Justification::centred, false);
    }

    // Gain readout.
    graphics.setColour(juce::Colour(0xff121519));
    graphics.fillRoundedRectangle(layout.gainValue.toFloat(), 2.0f);
    if (editedGainChannel_ != channelIndex) {
        graphics.setColour(selected ? juce::Colour(0xffffa229)
                                    : juce::Colour(0xffd6dbe2));
        graphics.setFont(CyDawLookAndFeel::labelFont(8.5f, true));
        graphics.drawText(juce::String(gainDbOf(channelIndex), 1) + " dB",
                          layout.gainValue, juce::Justification::centred,
                          false);
    }

    drawFaderAndMeter(graphics, layout.fader, layout.meter,
                      gainDbOf(channelIndex),
                      master ? masterPeak_ : channel->peak,
                      master ? masterRms_ : channel->rms, accent);

    if (!master) {
        const auto buttons = buttonBounds(channelIndex);
        const std::array<bool, 3> active { channel->muted, channel->solo,
                                           channel->armed };
        const std::array<juce::uint32, 3> onColours { 0xffffa229, 0xfff0dc5a,
                                                      0xffff5964 };
        const std::array<HitKind, 3> kinds { HitKind::mute, HitKind::solo,
                                             HitKind::arm };
        for (std::size_t index = 0; index < buttons.size(); ++index) {
            graphics.setColour(active[index]
                                   ? juce::Colour(onColours[index])
                                   : juce::Colour(hoveredKind == kinds[index]
                                                      ? 0xff444c56
                                                      : 0xff353b43));
            graphics.fillRoundedRectangle(buttons[index].toFloat(), 2.0f);
            graphics.setColour(active[index] ? juce::Colour(0xff151719)
                                             : juce::Colour(0xffe3e6ea));
            if (index == 2) {
                graphics.fillEllipse(
                    buttons[index].withSizeKeepingCentre(7, 7).toFloat());
            } else {
                graphics.setFont(CyDawLookAndFeel::labelFont(9.0f, true));
                graphics.drawText(index == 0 ? "M" : "S", buttons[index],
                                  juce::Justification::centred, false);
            }
        }
    }
}

// ------------------------------------------------------------- interaction

void MixerComponent::setGain(const std::size_t channelIndex,
                             const float gainDb)
{
    const auto value = juce::jlimit(minGainDb, maxGainDb, gainDb);
    if (isMaster(channelIndex)) {
        masterGainDb_ = value;
        if (masterGainHandler_)
            masterGainHandler_(value);
    } else {
        auto& channel = channels_[channelIndex];
        channel.gainDb = value;
        if (mixHandler_)
            mixHandler_(channelIndex, channel.gainDb, channel.pan);
    }
    repaint(stripBounds(channelIndex));
}

void MixerComponent::setPan(const std::size_t channelIndex, const float pan)
{
    if (isMaster(channelIndex))
        return;
    auto& channel = channels_[channelIndex];
    channel.pan = juce::jlimit(-1.0f, 1.0f, pan);
    if (mixHandler_)
        mixHandler_(channelIndex, channel.gainDb, channel.pan);
    repaint(stripBounds(channelIndex));
}

void MixerComponent::mouseDown(const juce::MouseEvent& event)
{
    if (editedGainChannel_)
        finishGainValueEdit(true);
    dragHit_ = {};
    const auto hit = hitAt(event.getPosition());
    if (hit.kind == HitKind::addTrack) {
        if (addTrackHandler_)
            addTrackHandler_();
        return;
    }
    if (!hit.channel)
        return;
    const auto index = *hit.channel;
    if (event.mods.isPopupMenu()) {
        if (!isMaster(index) && contextHandler_)
            contextHandler_(index);
        return;
    }
    switch (hit.kind) {
    case HitKind::header:
        if (!isMaster(index)) {
            if (selectHandler_)
                selectHandler_(index);
            dragHit_ = hit;
            dragTarget_ = index;
            dragStart_ = event.getPosition();
        }
        break;
    case HitKind::io:
        if (!isMaster(index) && channels_[index].inputSelectable
            && inputHandler_)
            inputHandler_(index);
        break;
    case HitKind::mute:
        channels_[index].muted = !channels_[index].muted;
        if (muteHandler_)
            muteHandler_(index, channels_[index].muted);
        repaint(stripBounds(index));
        break;
    case HitKind::solo:
        channels_[index].solo = !channels_[index].solo;
        if (soloHandler_)
            soloHandler_(index, channels_[index].solo);
        repaint(stripBounds(index));
        break;
    case HitKind::arm:
        channels_[index].armed = !channels_[index].armed;
        if (armHandler_)
            armHandler_(index, channels_[index].armed);
        repaint(stripBounds(index));
        break;
    case HitKind::pan:
        dragHit_ = hit;
        dragStart_ = event.getPosition();
        dragStartPan_ = channels_[index].pan;
        break;
    case HitKind::gain:
        dragHit_ = hit;
        dragStart_ = event.getPosition();
        dragStartGainDb_ = gainDbOf(index);
        break;
    case HitKind::gainValue:
        beginGainValueEdit(index);
        break;
    case HitKind::strip:
    case HitKind::devices:
        if (!isMaster(index) && selectHandler_)
            selectHandler_(index);
        break;
    case HitKind::addTrack:
    case HitKind::none:
        break;
    }
}

void MixerComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (!dragHit_.channel || *dragHit_.channel > channels_.size())
        return;
    const auto index = *dragHit_.channel;
    const auto fine = event.mods.isShiftDown() ? 0.25f : 1.0f;
    if (dragHit_.kind == HitKind::header) {
        if (event.getDistanceFromDragStart() < 5 || channels_.empty())
            return;
        // Strips only trade places with strips of their own kind: audio
        // tracks stay ahead of MIDI tracks, as everywhere else.
        const auto area = channelArea();
        const auto x = event.getPosition().x - area.getX()
                       + juce::roundToInt(scrollBar_.getCurrentRangeStart());
        auto target = static_cast<std::size_t>(juce::jlimit(
            0, static_cast<int>(channels_.size()) - 1, x / stripWidth));
        if (channels_[target].midi != channels_[index].midi)
            target = index;
        if (dragTarget_ != target) {
            dragTarget_ = target;
            repaint();
        }
        return;
    }
    if (dragHit_.kind == HitKind::gain) {
        // Relative to where the drag started, so grabbing the fader never
        // makes the level jump to the pointer.
        const auto fader = layoutFor(index).fader;
        const auto delta
            = static_cast<float>(dragStart_.y - event.getPosition().y)
              / static_cast<float>(std::max(1, fader.getHeight())) * fine;
        setGain(index, dbForFaderAmount(
                           faderAmountForDb(dragStartGainDb_) + delta));
    } else if (dragHit_.kind == HitKind::pan) {
        const auto delta
            = static_cast<float>(event.getPosition().x - dragStart_.x)
              / 80.0f * fine;
        setPan(index, dragStartPan_ + delta);
    }
}

void MixerComponent::mouseUp(const juce::MouseEvent&)
{
    const auto source = dragHit_.channel;
    const auto target = dragTarget_;
    const auto reordering = dragHit_.kind == HitKind::header;
    dragHit_ = {};
    dragTarget_.reset();
    if (reordering && source && target && *source != *target
        && moveHandler_)
        moveHandler_(*source, *target);
    if (reordering)
        repaint();
}

void MixerComponent::mouseDoubleClick(const juce::MouseEvent& event)
{
    const auto hit = hitAt(event.getPosition());
    if (!hit.channel)
        return;
    dragHit_ = {};
    switch (hit.kind) {
    case HitKind::gain:
    case HitKind::gainValue:
        if (editedGainChannel_)
            finishGainValueEdit(false);
        setGain(*hit.channel, 0.0f);
        break;
    case HitKind::pan:
        setPan(*hit.channel, 0.0f);
        break;
    case HitKind::devices:
        if (!isMaster(*hit.channel) && devicesOpenHandler_)
            devicesOpenHandler_(*hit.channel);
        break;
    default:
        break;
    }
}

void MixerComponent::mouseMove(const juce::MouseEvent& event)
{
    const auto hit = hitAt(event.getPosition());
    if (hit.channel == hovered_.channel && hit.kind == hovered_.kind)
        return;
    const auto repaintHit = [this](const Hit& value) {
        if (value.channel)
            repaint(stripBounds(*value.channel));
        else if (value.kind == HitKind::addTrack)
            repaint(addTrackBounds());
    };
    repaintHit(hovered_);
    hovered_ = hit;
    repaintHit(hovered_);
    setTooltip(tooltipFor(hit));
}

void MixerComponent::mouseExit(const juce::MouseEvent&)
{
    if (hovered_.channel)
        repaint(stripBounds(*hovered_.channel));
    else if (hovered_.kind == HitKind::addTrack)
        repaint(addTrackBounds());
    hovered_ = {};
}

void MixerComponent::mouseWheelMove(const juce::MouseEvent& event,
                                    const juce::MouseWheelDetails& wheel)
{
    const auto hit = hitAt(event.getPosition());
    const auto direction = wheel.deltaY > 0.0f ? 1.0f
                           : wheel.deltaY < 0.0f ? -1.0f : 0.0f;
    if (hit.channel && direction != 0.0f
        && (hit.kind == HitKind::gain || hit.kind == HitKind::gainValue)) {
        setGain(*hit.channel,
                gainDbOf(*hit.channel)
                    + direction * (event.mods.isShiftDown() ? 0.1f : 0.5f));
        return;
    }
    if (hit.channel && direction != 0.0f && hit.kind == HitKind::pan) {
        setPan(*hit.channel,
               channels_[*hit.channel].pan
                   + direction * (event.mods.isShiftDown() ? 0.01f : 0.05f));
        return;
    }
    if (scrollBar_.isVisible()) {
        const auto amount = wheel.deltaX != 0.0f ? wheel.deltaX : wheel.deltaY;
        scrollBar_.setCurrentRangeStart(
            scrollBar_.getCurrentRangeStart() - amount * 160.0);
    }
}

void MixerComponent::beginGainValueEdit(const std::size_t channelIndex)
{
    editedGainChannel_ = channelIndex;
    gainValueEditor_.setBounds(
        layoutFor(channelIndex).gainValue.expanded(2, 1));
    gainValueEditor_.setText(juce::String(gainDbOf(channelIndex), 1), false);
    gainValueEditor_.setVisible(true);
    gainValueEditor_.toFront(true);
    gainValueEditor_.grabKeyboardFocus();
    gainValueEditor_.selectAll();
    repaint(stripBounds(channelIndex));
}

void MixerComponent::finishGainValueEdit(const bool applyValue)
{
    if (!editedGainChannel_)
        return;
    const auto index = *editedGainChannel_;
    editedGainChannel_.reset();
    gainValueEditor_.setVisible(false);
    if (applyValue) {
        const auto text = gainValueEditor_.getText().replace(",", ".").trim();
        if (text.isNotEmpty() && text.containsOnly("-+0123456789.")
            && index <= channels_.size())
            setGain(index, text.getFloatValue());
    }
    if (index <= channels_.size())
        repaint(stripBounds(index));
}

// ----------------------------------------------------------------- scrolling

void MixerComponent::resized()
{
    const auto area = channelArea();
    scrollBar_.setBounds(area.getX(), getHeight() - scrollBarHeight,
                         area.getWidth(), scrollBarHeight);
    updateScrollRange();
    if (editedGainChannel_)
        gainValueEditor_.setBounds(
            layoutFor(*editedGainChannel_).gainValue.expanded(2, 1));
}

void MixerComponent::updateScrollRange()
{
    const auto visible = std::max(1, channelArea().getWidth());
    const auto total = static_cast<int>(channels_.size()) * stripWidth
                       + addStripWidth;
    scrollBar_.setRangeLimits(0.0, std::max(total, visible),
                              juce::dontSendNotification);
    scrollBar_.setCurrentRange(scrollBar_.getCurrentRangeStart(), visible,
                               juce::dontSendNotification);
    scrollBar_.setVisible(total > visible);
}

void MixerComponent::ensureChannelVisible(const std::size_t channelIndex)
{
    if (!scrollBar_.isVisible() || isMaster(channelIndex))
        return;
    const auto area = channelArea();
    const auto left = static_cast<int>(channelIndex) * stripWidth;
    const auto start = juce::roundToInt(scrollBar_.getCurrentRangeStart());
    if (left < start)
        scrollBar_.setCurrentRangeStart(left);
    else if (left + stripWidth > start + area.getWidth())
        scrollBar_.setCurrentRangeStart(left + stripWidth - area.getWidth());
}

void MixerComponent::scrollBarMoved(juce::ScrollBar*, double)
{
    if (editedGainChannel_)
        finishGainValueEdit(true);
    repaint();
}

} // namespace cydaw::app
