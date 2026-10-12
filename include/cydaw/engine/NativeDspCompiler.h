#pragma once

#include <cydaw/engine/AudioRenderPlan.h>
#include <cydaw/engine/NativeDspGraph.h>

#include <functional>
#include <memory>

namespace cydaw::engine {

using NativeDspProcessorObserver =
    std::function<void(const NativeDspNode&, AudioProcessor*)>;

// The one processor factory used by CyDAW, standalone Graph preview and the
// generated plug-in wrapper. Construction and prepare happen off the audio
// callback; the returned processor owns only realtime-safe state.
[[nodiscard]] std::unique_ptr<AudioProcessor> makeNativeDspProcessor(
    const NativeDspNode& node);

// Applies a parameter payload to an already prepared processor. All mutable
// built-in DSP parameters are atomic, so editors and host automation can use
// this path without rebuilding the immutable render topology.
[[nodiscard]] bool updateNativeDspProcessorParameters(
    AudioProcessor& processor, const NativeDspNode& node) noexcept;

[[nodiscard]] RenderPlanResult compileNativeDspGraph(
    const NativeDspGraph& graph, ProcessSpec spec,
    const NativeDspProcessorObserver& observer = {});

} // namespace cydaw::engine
