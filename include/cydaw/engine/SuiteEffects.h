#pragma once

#include <cydaw/engine/EffectProcessors.h>

#include <memory>

namespace cydaw::engine {

// The suites' processors (SuiteDesign.h says what they compute): Tranche,
// Finition. Null for any other kind.
[[nodiscard]] std::unique_ptr<EffectProcessor> makeSuiteEffect(const EffectDescriptor& descriptor);

} // namespace cydaw::engine
