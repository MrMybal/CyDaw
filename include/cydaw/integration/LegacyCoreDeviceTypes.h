#include <cydaw/base/Text.h>
#pragma once
#include <cyaudio/io/AudioDevice.h>
#include <juce_audio_devices/juce_audio_devices.h>
namespace cydaw::integration {
std::unique_ptr<juce::AudioIODeviceType> createCyAudioCoreDeviceType(
    const cydaw::base::Text &, cyaudio::AudioBackend,
    cyaudio::AudioAccess = cyaudio::AudioAccess::shared);
#if CYDAW_WITH_ASIO
std::unique_ptr<juce::AudioIODeviceType> createCyAudioCoreAsioType();
#endif

} // namespace cydaw::integration
