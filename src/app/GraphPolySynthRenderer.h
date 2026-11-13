#include <cydaw/document/PolySynthState.h>
#include <cydaw/base/File.h>
#pragma once

#include <cydaw/engine/PolySynthGraph.h>

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_data_structures/juce_data_structures.h>

#include <memory>
#include <optional>

namespace cydaw::app {

// Reusable real-time renderer for instrument Graphs. Graph compilation stays
// on the control thread; the audio callback only reads an immutable plan.
class GraphPolySynthRenderer final {
public:
    GraphPolySynthRenderer();
    ~GraphPolySynthRenderer();

    void prepare(double sampleRate);
    void release();
    void setPlan(cydaw::engine::PolySynthGraphPlan plan);
    [[nodiscard]] cydaw::engine::PolySynthGraphPlan plan() const;
    [[nodiscard]] bool enabled() const noexcept;
    void render(juce::AudioBuffer<float>& output,
                const juce::MidiBuffer& midi,
                int startSample, int numSamples) noexcept;
    void renderWithPlan(juce::AudioBuffer<float>& output,
                        const juce::MidiBuffer& midi,
                        int startSample, int numSamples,
                        const cydaw::engine::PolySynthGraphPlan& plan) noexcept;
    // The published plan as `change` edits it (a host's automation), copied
    // and played while the published one is still borrowed: what the copy
    // shares (a sampler's map, a model) is never freed by the audio thread.
    using PlanChange = void (*)(cydaw::engine::PolySynthGraphPlan& plan, const void* context);
    void renderChanged(juce::AudioBuffer<float>& output,
                       const juce::MidiBuffer& midi,
                       int startSample, int numSamples,
                       PlanChange change, const void* context) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cydaw::app
