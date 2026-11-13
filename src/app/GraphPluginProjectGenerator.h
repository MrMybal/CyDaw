#include <cydaw/base/File.h>
#pragma once

#include <cydaw/graph/GraphPluginExport.h>

#include <juce_core/juce_core.h>

namespace cydaw::app {

// Writes a small, reproducible CMake project for one exact Graph VST3. The
// generated project embeds the complete initial runtime state in the binary and
// consumes the common CyDAW Graph runtime, avoiding a second DSP engine.
[[nodiscard]] cydaw::base::Result generateGraphVst3Project(
    const cydaw::base::File& destination,
    const cydaw::graph::PluginExportPlan& plan,
    const cydaw::base::Value& manifest,
    const cydaw::base::Value& runtimeState, const cydaw::base::Text& format = "VST3");

} // namespace cydaw::app
