#pragma once

#include "KnobSlider.h"
#include "ModuleInterfaceEditor.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <array>
#include <functional>
#include <vector>

namespace cydaw::app {

class DeviceChainComponent final : public juce::Component {
public:
    enum class ViewScope { module, track };

    struct PluginDisplay {
        juce::String name;
        bool running {};
        std::uint64_t missedBlocks {};
        int latencySamples {};
    };

    DeviceChainComponent();

    void refreshTranslations();
    void setParameters(float driveDb, float cutoffHz, float gainDb, float pan, bool enabled);
    void setMeterLevels(const std::array<float, 2>& trackPeak,
                        const std::array<float, 2>& trackRms,
                        const std::array<float, 2>& masterPeak,
                        const std::array<float, 2>& masterRms);
    void setPluginChain(std::vector<PluginDisplay> plugins, std::size_t selectedPluginIndex);
    void setEffectChangedHandler(std::function<void(float, float)> handler);
    void setMixerChangedHandler(std::function<void(float, float)> handler);
    // One notch of a drag, delivered the way the mouse delivers it. The bench
    // measures the notch: a fader fires this sixty times a second.
    void dragEffectSliderForTest(double driveDb);
    void dragMixerSliderForTest(double gainDb);
    // A knob of the node surface turned to a value, as the mouse turns it.
    void turnNodeKnobForTest(const juce::String& key, double value);
    void setPluginSelectHandler(std::function<void(std::size_t)> handler);
    void setPluginRemoveHandler(std::function<void(std::size_t)> handler);
    void setPluginOpenHandler(std::function<void(std::size_t)> handler);
    void setPluginMoveHandler(std::function<void(std::size_t, int)> handler);
    void setNodeOpenHandler(std::function<void()> handler);
    void setDeviceAddHandler(std::function<void()> handler);
    void setNodeDeviceInterface(ModuleInterfaceState state);
    void setGraphLayout(juce::String inputTitle,
                        juce::String outputTitle,
                        bool hasDriveNode,
                        bool hasLowPassNode);
    void setNodeParameterChangedHandler(
        std::function<void(const juce::String&, double)> handler);
    void setNodeAnalysisFrame(const cydaw::engine::AudioAnalysisFrame& frame);
    void clearNodeAnalysis() noexcept;
    void setViewScope(ViewScope scope);
    [[nodiscard]] ViewScope viewScope() const noexcept;

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    void notifyEffects();
    void notifyMixer();
    void showPluginMenu(std::size_t pluginIndex);
    void setScrollOffset(float offset);

    ModuleInterfaceEditorComponent nodeDeviceSurface_ { {} };
    ModuleInterfaceState nodeInterfaceState_;
    // What that state describes, kept so an identical republication can be
    // recognised and ignored.
    juce::var nodeInterfaceDescription_;
    juce::Rectangle<int> nodeFrame_;
    juce::Rectangle<int> chainBounds_;
    juce::Rectangle<int> viewportBounds_;
    juce::Rectangle<int> scrollbarBounds_;
    juce::Rectangle<int> scrollbarThumbBounds_;
    juce::Rectangle<int> addSlotBounds_;
    std::array<juce::Rectangle<int>, 6> cards_;
    std::vector<juce::Rectangle<int>> pluginCards_;
    std::array<juce::String, 6> titles_;
    juce::String inputTitle_;
    juce::String outputTitle_;
    bool trackEnabled_ {};
    bool hasDriveNode_ {};
    bool hasLowPassNode_ {};
    KnobSlider driveSlider_;
    KnobSlider cutoffSlider_;
    KnobSlider gainSlider_;
    KnobSlider panSlider_;
    juce::TextButton removePluginButton_;
    juce::TextButton openPluginButton_;
    juce::TextButton movePluginLeftButton_;
    juce::TextButton movePluginRightButton_;
    juce::TextButton openNodeButton_;
    juce::TextButton addDeviceButton_;
    juce::ComboBox pluginSelector_;
    std::vector<PluginDisplay> plugins_;
    std::size_t selectedPluginIndex_ {};
    std::size_t draggedPluginIndex_ { static_cast<std::size_t>(-1) };
    juce::String pluginName_;
    bool pluginRunning_ {};
    std::uint64_t pluginMissedBlocks_ {};
    std::function<void(float, float)> effectChangedHandler_;
    std::function<void(float, float)> mixerChangedHandler_;
    int pluginLatencySamples_ {};
    std::array<float, 2> trackPeak_ {};
    std::array<float, 2> trackRms_ {};
    std::array<float, 2> masterPeak_ {};
    std::array<float, 2> masterRms_ {};
    std::function<void(std::size_t)> pluginSelectHandler_;
    std::function<void(std::size_t)> pluginRemoveHandler_;
    std::function<void(std::size_t)> pluginOpenHandler_;
    std::function<void(std::size_t, int)> pluginMoveHandler_;
    std::function<void()> nodeOpenHandler_;
    std::function<void()> deviceAddHandler_;
    std::function<void(const juce::String&, double)>
        nodeParameterChangedHandler_;
    float scrollOffset_ {};
    float maximumScrollOffset_ {};
    float scrollbarDragStartOffset_ {};
    int scrollbarDragStartX_ {};
    bool draggingScrollbar_ {};
    bool ensureSelectedPluginVisible_ {};
    ViewScope viewScope_ { ViewScope::track };
};

} // namespace cydaw::app
