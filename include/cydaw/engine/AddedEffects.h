#pragma once

#include <cydaw/engine/EffectProcessors.h>

#include <memory>

namespace cydaw::engine {

// The processors of the effects added on 2026-10-14 (VoiceEffects.cpp,
// PitchEffects.cpp, SpaceEffects.cpp, RhythmEffects.cpp, MotionEffects.cpp,
// ResonanceEffects.cpp, UtilityEffects.cpp). Null for any other kind.
[[nodiscard]] std::unique_ptr<EffectProcessor> makeAddedEffect(const EffectDescriptor& descriptor);

} // namespace cydaw::engine
