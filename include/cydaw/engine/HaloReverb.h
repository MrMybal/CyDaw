#pragma once

#include <cydaw/engine/EffectProcessors.h>

#include <memory>

namespace cydaw::engine {

// Halo's processor (HaloDesign.h says what it computes). Null for any other kind.
[[nodiscard]] std::unique_ptr<EffectProcessor> makeHaloEffect(const EffectDescriptor& descriptor);

} // namespace cydaw::engine
