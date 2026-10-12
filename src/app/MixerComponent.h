#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <array>
#include <functional>
#include <optional>
#include <vector>

namespace cydaw::app {

// The mixer view: one channel strip per track and a pinned master strip, laid
// out the way a console user expects — name, devices, pan, fader beside its
// meter, then mute / solo / arm. It only renders the mix state it is given
// and reports edits through handlers; the workspace owns the engine and feeds
// the meters from its analysis loop.
class MixerComponent final : public juce::Component,
                             public juce::SettableTooltipClient,
                             private juce::ScrollBar::Listener {
public:
    MixerComponent();
    ~MixerComponent() override;

    struct Channel {
        juce::String name;
        juce::Colour colour { 0xff4fd3ae };
        bool midi {};
        std::vector<juce::String> devices;
        juce::String input;
        juce::String output;
        bool inputSelectable {};
        float gainDb {};
        float pan {};
        bool muted {};
        bool solo {};
        bool armed {};
        bool selected {};
        std::array<float, 2> peak {};
        std::array<float, 2> rms {};
    };

    [[nodiscard]] static bool selfTestInputRouting();

    void setChannels(std::vector<Channel> channels);
    void setChannelMeterLevels(std::size_t channelIndex,
                               const std::array<float, 2>& peak,
                               const std::array<float, 2>& rms);
    void setMasterMeterLevels(const std::array<float, 2>& peak,
                              const std::array<float, 2>& rms);
    void setMasterGain(float gainDb);
    void refreshTranslations();

    void setMixHandler(std::function<void(std::size_t, float, float)> handler);
    void setMuteHandler(std::function<void(std::size_t, bool)> handler);
    void setSoloHandler(std::function<void(std::size_t, bool)> handler);
    void setArmHandler(std::function<void(std::size_t, bool)> handler);
    void setSelectHandler(std::function<void(std::size_t)> handler);
    void setDevicesOpenHandler(std::function<void(std::size_t)> handler);
    void setMasterGainHandler(std::function<void(float)> handler);
    void setAddTrackHandler(std::function<void()> handler);
    void setMoveHandler(std::function<void(std::size_t, std::size_t)> handler);
    void setContextHandler(std::function<void(std::size_t)> handler);
    void setInputHandler(std::function<void(std::size_t)> handler);

    // A channel index equal to the channel count addresses the master strip.
    [[nodiscard]] juce::Rectangle<int> stripBounds(std::size_t channelIndex) const;
    [[nodiscard]] juce::Rectangle<int> faderBounds(std::size_t channelIndex) const;
    [[nodiscard]] juce::Rectangle<int> headerBounds(std::size_t channelIndex) const;
    [[nodiscard]] juce::Rectangle<int> ioBounds(std::size_t channelIndex) const;
    [[nodiscard]] juce::Rectangle<int> panBounds(std::size_t channelIndex) const;
    [[nodiscard]] std::array<juce::Rectangle<int>, 3> buttonBounds(
        std::size_t channelIndex) const;

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override;

private:
    enum class HitKind {
        none,
        strip,
        header,
        devices,
        io,
        pan,
        gain,
        gainValue,
        mute,
        solo,
        arm,
        addTrack
    };
    struct Hit {
        std::optional<std::size_t> channel;
        HitKind kind { HitKind::none };
    };
    struct StripLayout {
        juce::Rectangle<int> header;
        juce::Rectangle<int> devices;
        juce::Rectangle<int> io;
        juce::Rectangle<int> pan;
        juce::Rectangle<int> gainValue;
        juce::Rectangle<int> fader;
        juce::Rectangle<int> meter;
        juce::Rectangle<int> buttons;
    };

    static constexpr int stripWidth = 98;
    static constexpr int masterWidth = 116;
    static constexpr int scrollBarHeight = 12;
    static constexpr int addStripWidth = 44;

    [[nodiscard]] bool isMaster(std::size_t channelIndex) const;
    [[nodiscard]] juce::Rectangle<int> channelArea() const;
    [[nodiscard]] juce::Rectangle<int> masterStripBounds() const;
    [[nodiscard]] juce::Rectangle<int> addTrackBounds() const;
    [[nodiscard]] StripLayout layoutFor(std::size_t channelIndex) const;
    [[nodiscard]] int deviceRows() const;
    [[nodiscard]] Hit hitAt(juce::Point<int> position) const;
    [[nodiscard]] juce::String tooltipFor(const Hit& hit) const;
    [[nodiscard]] float gainDbOf(std::size_t channelIndex) const;
    void paintStrip(juce::Graphics& graphics, std::size_t channelIndex);
    void setGain(std::size_t channelIndex, float gainDb);
    void setPan(std::size_t channelIndex, float pan);
    void ensureChannelVisible(std::size_t channelIndex);
    void updateScrollRange();
    void scrollBarMoved(juce::ScrollBar* scrollBar,
                        double newRangeStart) override;
    void beginGainValueEdit(std::size_t channelIndex);
    void finishGainValueEdit(bool applyValue);

    std::vector<Channel> channels_;
    std::array<float, 2> masterPeak_ {};
    std::array<float, 2> masterRms_ {};
    float masterGainDb_ {};
    std::optional<std::size_t> lastSelected_;
    Hit hovered_;
    Hit dragHit_;
    std::optional<std::size_t> dragTarget_;
    juce::Point<int> dragStart_;
    float dragStartGainDb_ {};
    float dragStartPan_ {};
    juce::ScrollBar scrollBar_ { false };
    juce::TextEditor gainValueEditor_;
    std::optional<std::size_t> editedGainChannel_;
    juce::String masterTitle_;
    juce::String masterBusText_;
    juce::String noDevicesText_;
    juce::String emptyText_;
    juce::String help_;
    juce::String gainHelp_;
    juce::String gainValueHelp_;
    juce::String panHelp_;
    juce::String muteHelp_;
    juce::String soloHelp_;
    juce::String armHelp_;
    juce::String devicesHelp_;
    juce::String addTrackHelp_;
    juce::String headerHelp_;
    juce::String inputHelp_;
    std::function<void(std::size_t, float, float)> mixHandler_;
    std::function<void(std::size_t, bool)> muteHandler_;
    std::function<void(std::size_t, bool)> soloHandler_;
    std::function<void(std::size_t, bool)> armHandler_;
    std::function<void(std::size_t)> selectHandler_;
    std::function<void(std::size_t)> devicesOpenHandler_;
    std::function<void(float)> masterGainHandler_;
    std::function<void()> addTrackHandler_;
    std::function<void(std::size_t, std::size_t)> moveHandler_;
    std::function<void(std::size_t)> contextHandler_;
    std::function<void(std::size_t)> inputHandler_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerComponent)
};

} // namespace cydaw::app
