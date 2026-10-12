#pragma once

#include <cydaw/engine/NativeDspGraph.h>

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

// The per-track modulation CyDAW 1 projects carry: a small value graph (an
// LFO, a macro, arithmetic) whose output moves parameters of the track's
// graph, each through an assignment (base value, range, amount). Evaluated
// on the message thread about thirty times a second, as in CyDAW 1.

namespace cydaw::v2::modulation {

struct Assignment {
    juce::String binding;   // node.<id>.<type>.<parameter>
    juce::String label;
    double base {};
    double minimum {};
    double maximum { 1.0 };
    double amount { 0.25 };
    bool enabled {};
};

[[nodiscard]] std::vector<Assignment> readAssignments(const juce::var& array);
[[nodiscard]] juce::var writeAssignments(const std::vector<Assignment>& assignments);

// The value graph's output at a time, -1..1 for an LFO.
[[nodiscard]] double evaluate(const juce::var& valueGraph, double timeSeconds);

// Writes a value into the native parameter a binding names; false when the
// binding is not one a modulation can move.
bool applyToNativeGraph(engine::NativeDspGraph& graph, const juce::String& binding, double value);

// Where a binding of a track's top-level card lands in the engine: the
// native node and its field, or a slot of its eqBands; with its range.
struct NativeTarget {
    std::uint64_t nodeId {};
    float engine::NativeDspNodeParameters::* field {};
    int slot { -1 };
    float minimum {};
    float maximum { 1.0f };
};
[[nodiscard]] std::optional<NativeTarget> nativeTargetFor(const juce::String& binding);
// The value a binding names in a native graph, when a modulation can move it.
[[nodiscard]] std::optional<double> readFromNativeGraph(const engine::NativeDspGraph& graph, const juce::String& binding);

// The first LFO of a value graph, for a compact editor: rate in Hz, depth
// 0..1, shape 0 sine, 1 triangle, 2 saw, 3 square.
struct LfoSettings {
    bool found {};
    double rate { 1.0 };
    double depth { 1.0 };
    double offset {};
    int shape {};
};
[[nodiscard]] LfoSettings firstLfo(const juce::var& valueGraph);
// Changes the first LFO in place; false when the graph has none.
bool setFirstLfo(juce::var& valueGraph, const LfoSettings& settings);

} // namespace cydaw::v2::modulation
