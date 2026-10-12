#pragma once

#include "AudioTrackController.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>

namespace cydaw::app {

class VirtualBusManagerComponent final : public juce::Component,
                                         private juce::Timer {
public:
    explicit VirtualBusManagerComponent(AudioTrackController& controller);

    void refresh();
    void refreshTranslations();
    void setChangedHandler(std::function<void()> handler);
    void setOpenGraphHandler(
        std::function<void(std::uint64_t)> handler);
    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    void timerCallback() override;
    void rebuildBusSelector(std::uint64_t selectBus = 0);
    void rebuildChannelSelectors();
    void rebuildTrackRouting();
    void loadSelectedBus();
    void applySelectedBus();
    [[nodiscard]] VirtualAudioBus* selectedBus() noexcept;
    [[nodiscard]] const VirtualAudioBus* selectedBus() const noexcept;
    static void configureSlider(juce::Slider& slider, double minimum,
                                double maximum, double step,
                                juce::String suffix);

    AudioTrackController& controller_;
    std::vector<VirtualAudioBus> buses_;
    std::uint64_t selectedBusId_ {};
    bool updating_ {};
    std::array<float, 2> meter_ {};
    std::function<void()> changedHandler_;
    std::function<void(std::uint64_t)> openGraphHandler_;

    juce::Label title_;
    juce::Label subtitle_;
    juce::ComboBox busSelector_;
    juce::TextButton addInputButton_;
    juce::TextButton addOutputButton_;
    juce::TextButton removeButton_;
    juce::TextButton resetButton_;

    juce::Label nameLabel_;
    juce::TextEditor nameEditor_;
    juce::Label directionLabel_;
    juce::Label directionValue_;
    juce::Label channelsLabel_;
    juce::ComboBox channelsSelector_;
    juce::Label sourceALabel_;
    juce::Label sourceBLabel_;
    juce::ComboBox sourceASelector_;
    juce::ComboBox sourceBSelector_;
    juce::Label destinationALabel_;
    juce::Label destinationBLabel_;
    juce::ComboBox destinationASelector_;
    juce::ComboBox destinationBSelector_;
    juce::ToggleButton monitorButton_;
    juce::ToggleButton muteButton_;
    juce::ToggleButton exposeButton_;
    juce::Label systemStatus_;

    juce::Label gainLabel_;
    juce::Slider gainSlider_;
    juce::Label graphLabel_;
    juce::Label graphStatus_;
    juce::TextButton editGraphButton_;

    juce::Label trackRoutingTitle_;
    juce::Label trackLabel_;
    juce::ComboBox trackSelector_;
    juce::Label trackBusLabel_;
    juce::ComboBox trackBusSelector_;
};

} // namespace cydaw::app
