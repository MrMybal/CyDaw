#pragma once

#include <cydaw/engine/EffectProcessors.h>

#include <memory>

namespace cydaw::engine {

// The processors of Vocodeur, Dé-Esseur, Accordeur (VoiceDesign.h says what they compute).
// Null for any other kind.
[[nodiscard]] std::unique_ptr<EffectProcessor> makeVoiceEffect(const EffectDescriptor& descriptor);

} // namespace cydaw::engine
