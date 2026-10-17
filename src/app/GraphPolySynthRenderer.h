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

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// What a plan carries as numbers cannot hold a sound model or a table: they
// come back from the graph's own JSON (an exported plug-in's "editor" state).
void attachVoiceAssets(cydaw::engine::PolySynthGraphPlan& plan, const juce::var& graphState);

// Each Replacer's instrument (Containers.h) an exported state carries
// ("replacers": its node, its plan, its graph): the plan restored, its
// samples and model attached, onto its node of `graph`. `waitForSamples`: a
// sampler's samples decoded before it returns (a host rendering offline).
void attachReplacerInstruments(cydaw::engine::NativeDspGraph& graph, const juce::var& runtimeState, bool waitForSamples);

[[nodiscard]] juce::var serialisePolySynthGraphPlan(
    const cydaw::engine::PolySynthGraphPlan& plan);
[[nodiscard]] std::optional<cydaw::engine::PolySynthGraphPlan>
restorePolySynthGraphPlan(const juce::var& state, juce::String& error);

} // namespace cydaw::app
