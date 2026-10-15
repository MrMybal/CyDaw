#pragma once

#include <cydaw/engine/EffectProcessors.h>

#include <memory>

namespace cydaw::engine {

// The processors of Résonateurs, Corps, Peigne (ResonanceDesign.h says what they compute).
// Null for any other kind.
[[nodiscard]] std::unique_ptr<EffectProcessor> makeResonanceEffect(const EffectDescriptor& descriptor);

} // namespace cydaw::engine
