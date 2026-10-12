#pragma once

#include <cydaw/engine/EffectProcessors.h>

#include <memory>

namespace cydaw::engine {

// The studio effects' processors (ProEffectsDesign.h says what they compute):
// Réverbe Pro, Compresseur Pro, Limiteur Pro and Filtre Pro. Null for any
// other kind.
[[nodiscard]] std::unique_ptr<EffectProcessor> makeProEffect(const EffectDescriptor& descriptor);

} // namespace cydaw::engine
