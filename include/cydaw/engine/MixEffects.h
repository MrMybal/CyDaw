#pragma once

#include <cydaw/engine/EffectProcessors.h>

#include <memory>

namespace cydaw::engine {

// The mixing effects' processors (MixDesign.h says what they compute):
// Pression, Fournaise, Écho Bande, Multibande Pro, Velours, Patine, Sonomètre.
// Null for any other kind.
[[nodiscard]] std::unique_ptr<EffectProcessor> makeMixEffect(const EffectDescriptor& descriptor);

} // namespace cydaw::engine
