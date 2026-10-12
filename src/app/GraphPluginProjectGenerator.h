#pragma once

#include <cydaw/graph/GraphPluginExport.h>

#include <juce_core/juce_core.h>

namespace cydaw::app {

// Writes a small, reproducible CMake project for one exact Graph VST3. The
// generated project embeds the complete initial runtime state in the binary and
// consumes the common CyDAW Graph runtime, avoiding a second DSP engine.
[[nodiscard]] juce::Result generateGraphVst3Project(
    const juce::File& destination,
    const cydaw::graph::PluginExportPlan& plan,
    const juce::var& manifest,
    const juce::var& runtimeState);

} // namespace cydaw::app
