#pragma once

#include <cydaw/engine/EffectProcessors.h>

#include <memory>

namespace cydaw::engine {

// The processors of Pompe, Décaleur, Anneau, Rotor (MotionDesign.h says what they compute).
// Null for any other kind.
[[nodiscard]] std::unique_ptr<EffectProcessor> makeMotionEffect(const EffectDescriptor& descriptor);

} // namespace cydaw::engine
