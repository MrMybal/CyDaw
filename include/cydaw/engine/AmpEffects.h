#pragma once

#include <cydaw/engine/EffectProcessors.h>

#include <memory>

namespace cydaw::engine {

// The amp, cabinet and microphone simulators' processors (AmpDesign.h says
// what they compute): Ampli, Baffle et micro, Micro. Null for any other kind.
[[nodiscard]] std::unique_ptr<EffectProcessor> makeAmpEffect(const EffectDescriptor& descriptor);

} // namespace cydaw::engine
