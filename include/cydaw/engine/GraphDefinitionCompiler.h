#pragma once

#include <cydaw/engine/NativeDspGraph.h>
#include <cydaw/graph/GraphDefinition.h>

#include <string>
#include <vector>

namespace cydaw::engine {

// Resolves a published Graph parameter to the primitive processor that owns
// the real-time value after every nested Graph instance has been flattened.
// A published parameter may yield several targets in future fan-out graphs,
// therefore the compile result deliberately stores a list.
struct CompiledGraphParameterTarget {
    graph::ParameterId graphParameterId {};
    std::string graphParameterKey;
    std::uint64_t nativeNodeId {};
    std::string nativeParameterKey;

    [[nodiscard]] bool operator==(
        const CompiledGraphParameterTarget&) const = default;
};

// Associates a flattened real-time control processor with the authored node
// that represents it in the current Graph editor. A reusable Graph instance
// can hide several native processors, so telemetry must be explicitly mapped
// back to the visible instance instead of leaking implementation node IDs.
struct CompiledControlTelemetryTarget {
    std::uint64_t nativeNodeId {};
    graph::NodeId authoredNodeId {};

    [[nodiscard]] bool operator==(
        const CompiledControlTelemetryTarget&) const = default;
};

struct GraphDefinitionCompileResult {
    NativeDspGraph graph;
    std::vector<CompiledGraphParameterTarget> parameterTargets;
    std::vector<CompiledControlTelemetryTarget> controlTelemetryTargets;
    graph::ValidationResult validation;

    [[nodiscard]] bool ok() const noexcept { return validation.ok(); }
    [[nodiscard]] explicit operator bool() const noexcept { return ok(); }
};

// Compiles the versioned, typed Graph contract into the dependency-free audio
// render description. Graph instances are recursively flattened while exact
// authored port IDs and published-parameter bindings remain intact.
[[nodiscard]] GraphDefinitionCompileResult compileGraphDefinitionToNativeDsp(
    const graph::GraphLibrary& library,
    const graph::GraphReference& root);

} // namespace cydaw::engine
