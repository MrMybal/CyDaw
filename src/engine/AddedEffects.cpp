#include <cydaw/engine/AddedEffects.h>

#include <cydaw/engine/VoiceEffects.h>
#include <cydaw/engine/PitchEffects.h>
#include <cydaw/engine/SpaceEffects.h>
#include <cydaw/engine/RhythmEffects.h>
#include <cydaw/engine/MotionEffects.h>
#include <cydaw/engine/ResonanceEffects.h>
#include <cydaw/engine/UtilityEffects.h>

namespace cydaw::engine {

std::unique_ptr<EffectProcessor> makeAddedEffect(const EffectDescriptor& descriptor)
{
    if (auto made = makeVoiceEffect(descriptor))
        return made;
    if (auto made = makePitchEffect(descriptor))
        return made;
    if (auto made = makeSpaceEffect(descriptor))
        return made;
    if (auto made = makeRhythmEffect(descriptor))
        return made;
    if (auto made = makeMotionEffect(descriptor))
        return made;
    if (auto made = makeResonanceEffect(descriptor))
        return made;
    if (auto made = makeUtilityEffect(descriptor))
        return made;
    return {};
}

} // namespace cydaw::engine
