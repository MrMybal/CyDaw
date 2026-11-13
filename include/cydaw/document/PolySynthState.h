#pragma once
#include <cydaw/base/Value.h>
#include <cydaw/engine/NativeDspGraph.h>
#include <cydaw/engine/PolySynthGraph.h>
#include <optional>
namespace cydaw::app {
[[nodiscard]] engine::PolySynthGraphPlan
sanitisePolySynthGraphPlan(engine::PolySynthGraphPlan plan);
// What a plan carries as numbers cannot hold a sound model or a table: they
// come back from the graph's own JSON (an exported plug-in's "editor" state).
void attachVoiceAssets(cydaw::engine::PolySynthGraphPlan &plan,
                       const cydaw::base::Value &graphState);

// Each Replacer's instrument (Containers.h) an exported state carries
// ("replacers": its node, its plan, its graph): the plan restored, its
// samples and model attached, onto its node of `graph`. `waitForSamples`: a
// sampler's samples decoded before it returns (a host rendering offline).
void attachReplacerInstruments(cydaw::engine::NativeDspGraph &graph,
                               const cydaw::base::Value &runtimeState,
                               bool waitForSamples);

[[nodiscard]] cydaw::base::Value
serialisePolySynthGraphPlan(const cydaw::engine::PolySynthGraphPlan &plan);
[[nodiscard]] std::optional<cydaw::engine::PolySynthGraphPlan>
restorePolySynthGraphPlan(const cydaw::base::Value &state,
                          cydaw::base::Text &error);

} // namespace cydaw::app
