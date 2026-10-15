#pragma once

#include <cydaw/engine/EffectProcessors.h>

#include <memory>

namespace cydaw::engine {

// The processors of Empreinte, Givre (SpaceDesign.h says what they compute).
// Null for any other kind.
[[nodiscard]] std::unique_ptr<EffectProcessor> makeSpaceEffect(const EffectDescriptor& descriptor);

} // namespace cydaw::engine
