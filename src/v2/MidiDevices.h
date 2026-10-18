#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <cstdint>
#include <string>

// The MIDI devices plugged in, as the interface shows them: the system's
// lists read again every two seconds at most (reading them can take a
// while), and the lights that say a message went by.

namespace cydaw::v2::midi_devices {

[[nodiscard]] const juce::Array<juce::MidiDeviceInfo>& inputs();
[[nodiscard]] const juce::Array<juce::MidiDeviceInfo>& outputs();
// A device's name by its identifier (the identifier itself when it is gone).
[[nodiscard]] juce::String inputName(const juce::String& identifier);
[[nodiscard]] juce::String outputName(const juce::String& identifier);

// How lit a light is: 1 when its counter has just moved, fading out over a
// quarter of a second; `key` names the light (a track's input).
[[nodiscard]] float activity(const std::string& key, std::uint32_t counter);

} // namespace cydaw::v2::midi_devices
