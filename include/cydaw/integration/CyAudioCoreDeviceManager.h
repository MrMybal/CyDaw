#pragma once
#include <cyaudio/io/AudioDevice.h>
#include <juce_audio_devices/juce_audio_devices.h>
namespace cydaw::integration {
std::unique_ptr<juce::AudioIODeviceType> createCyAudioCoreDeviceType(
    const juce::String &, cyaudio::AudioBackend,
    cyaudio::AudioAccess = cyaudio::AudioAccess::shared);
#if CYDAW_WITH_ASIO
std::unique_ptr<juce::AudioIODeviceType> createCyAudioCoreAsioType();
#endif
#if CYDAW_CORE_AUDIO_DEVICES
class DeviceManager : public juce::AudioDeviceManager {
  void createAudioDeviceTypes(
      juce::OwnedArray<juce::AudioIODeviceType> &types) override {
#if JUCE_WINDOWS
    types.add(createCyAudioCoreDeviceType("Windows Audio",
                                          cyaudio::AudioBackend::wasapi)
                  .release());
    types.add(createCyAudioCoreDeviceType("Windows Audio (Exclusive Mode)",
                                          cyaudio::AudioBackend::wasapi,
                                          cyaudio::AudioAccess::exclusive)
                  .release());
    types.add(createCyAudioCoreDeviceType("Windows Audio (Low Latency Mode)",
                                          cyaudio::AudioBackend::wasapi)
                  .release());
    types.add(createCyAudioCoreDeviceType("DirectSound",
                                          cyaudio::AudioBackend::directSound)
                  .release());
#elif JUCE_MAC
    types.add(createCyAudioCoreDeviceType("CoreAudio",
                                          cyaudio::AudioBackend::coreAudio)
                  .release());
#else
    types.add(createCyAudioCoreDeviceType("ALSA", cyaudio::AudioBackend::alsa)
                  .release());
    types.add(createCyAudioCoreDeviceType("PulseAudio",
                                          cyaudio::AudioBackend::pulseAudio)
                  .release());
#if JUCE_JACK
    types.add(createCyAudioCoreDeviceType("JACK", cyaudio::AudioBackend::jack)
                  .release());
#endif
#endif
#if CYDAW_WITH_ASIO
    types.add(createCyAudioCoreAsioType().release());
#endif
  }
};
#else
using DeviceManager = juce::AudioDeviceManager;
#endif
} // namespace cydaw::integration
