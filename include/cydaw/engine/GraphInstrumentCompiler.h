#pragma once

#include <cydaw/engine/PolySynthGraph.h>
#include <cydaw/graph/GraphDefinition.h>

namespace cydaw::engine {

struct GraphInstrumentCompileResult {
    PolySynthGraphPlan plan;
    graph::ValidationResult validation;

    [[nodiscard]] bool ok() const noexcept { return validation.ok(); }
    [[nodiscard]] explicit operator bool() const noexcept { return ok(); }
};

// Compiles the typed MIDI/value/audio signal flow of a Graph instrument into
// the immutable polyphonic render plan consumed by the real-time renderer.
[[nodiscard]] GraphInstrumentCompileResult compileGraphInstrument(
    const graph::GraphLibrary& library,
    const graph::GraphReference& root);

} // namespace cydaw::engine
