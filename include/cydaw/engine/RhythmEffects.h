#pragma once

#include <cydaw/engine/EffectProcessors.h>

#include <memory>

namespace cydaw::engine {

// The processors of Grains, Saccade, Boucleur (RhythmDesign.h says what they compute).
// Null for any other kind.
[[nodiscard]] std::unique_ptr<EffectProcessor> makeRhythmEffect(const EffectDescriptor& descriptor);

} // namespace cydaw::engine
