#pragma once

#include <cydaw/engine/NativeDspGraph.h>

#include <juce_data_structures/juce_data_structures.h>

#include <optional>

namespace cydaw::app {

// Stable JUCE-state adapter shared by CyDAW, the standalone editor and the
// VST3 runtime wrapper. The audio callback never parses this representation.
[[nodiscard]] juce::var serialiseNativeDspGraphState(
    const cydaw::engine::NativeDspGraph& graph);

[[nodiscard]] std::optional<cydaw::engine::NativeDspGraph>
restoreNativeDspGraphState(const juce::var& state, juce::String& error);

// The equaliser's band array, written only when a band holds something so
// every other node stays as small as before.
void writeEqBands(juce::DynamicObject& object, const cydaw::engine::NativeDspNodeParameters& parameters);
void readEqBands(const juce::DynamicObject& object, cydaw::engine::NativeDspNodeParameters& parameters);

} // namespace cydaw::app
