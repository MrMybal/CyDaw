#pragma once

#include "NativeDspRuntime.h"

#include <cydaw/engine/AudioAnalysis.h>
#include <cydaw/engine/VoiceLive.h>

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <memory>
#include <vector>

// The window of a plug-in exported from CyDAW: the same interface as in
// CyDAW (Dear ImGui), over the plug-in's own graph. Prisme, the synths and
// Kick show their own windows; a device made in the workshop with an
// interface drawn for it shows that interface; the effects show their large
// views (several: one at a time, chosen at the top). Never the graph.

namespace cydaw::app {
class NodeGraphComponent;
}

namespace cydaw::plugin_ui {

// What the interface reads of the plug-in's sound: the taps of its
// devices' displays, what they show of their work, their levels, the
// host's tempo, an instrument's newest voice.
struct PluginSound {
    std::function<void(const std::vector<app::NativeDspRuntime::TapRequest>& taps)> setTaps;
    std::function<bool(std::uint64_t nodeId, bool input, engine::AudioAnalysisFrame& frame)> readTap;
    std::function<void(std::uint64_t nodeId, std::vector<float>& readings)> readings;
    std::function<void(const std::vector<std::uint64_t>& nodes, std::vector<float>& left, std::vector<float>& right)> peaks;
    std::function<double()> tempo;
    std::function<bool(engine::VoiceLive& live)> voiceLive;
};

class PluginInterface final : public juce::Component {
public:
    // `instrument`: it plays notes (its graph starts at a MIDI input).
    // `changed`: its graph edited (`structural`: cards or cables, else
    // settings), to compile and hand to the processor.
    PluginInterface(const juce::String& productName, bool instrument, PluginSound sound, std::function<void(bool structural)> changed);
    ~PluginInterface() override;

    [[nodiscard]] app::NodeGraphComponent& graph();
    // The graph the plug-in's state holds; nothing is published.
    void restore(const juce::var& editorState);
    // The size its window opens at (logical pixels), and the least.
    [[nodiscard]] juce::Point<int> preferredSize() const;
    [[nodiscard]] static juce::Point<int> smallestSize() { return { 560, 380 }; }

    void resized() override;
    // The next frame written as a PNG (tests, without the screen).
    void captureNextFrame(const juce::File& target, std::function<void(bool)> done);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cydaw::plugin_ui
