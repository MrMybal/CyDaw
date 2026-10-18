#include "MidiDevices.h"

#include <imgui.h>

#include <algorithm>
#include <unordered_map>

namespace cydaw::v2::midi_devices {

namespace {

struct Lists {
    juce::Array<juce::MidiDeviceInfo> inputs;
    juce::Array<juce::MidiDeviceInfo> outputs;
    double readAt { -1.0e9 };
};

Lists& lists()
{
    static Lists cached;
    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (now - cached.readAt > 2.0) {
        cached.inputs = juce::MidiInput::getAvailableDevices();
        cached.outputs = juce::MidiOutput::getAvailableDevices();
        cached.readAt = now;
    }
    return cached;
}

juce::String nameIn(const juce::Array<juce::MidiDeviceInfo>& devices, const juce::String& identifier)
{
    for (const auto& device : devices)
        if (device.identifier == identifier)
            return device.name;
    return identifier;
}

} // namespace

const juce::Array<juce::MidiDeviceInfo>& inputs()
{
    return lists().inputs;
}

const juce::Array<juce::MidiDeviceInfo>& outputs()
{
    return lists().outputs;
}

juce::String inputName(const juce::String& identifier)
{
    return nameIn(inputs(), identifier);
}

juce::String outputName(const juce::String& identifier)
{
    return nameIn(outputs(), identifier);
}

float activity(const std::string& key, const std::uint32_t counter)
{
    struct Seen {
        std::uint32_t counter {};
        double at { -1.0e9 };
    };
    static std::unordered_map<std::string, Seen> seen;
    const auto now = ImGui::GetTime();
    const auto found = seen.find(key);
    if (found == seen.end()) {
        // First seen: what it counted before is not news.
        seen.emplace(key, Seen { counter, -1.0e9 });
        return 0.0f;
    }
    if (found->second.counter != counter) {
        found->second.counter = counter;
        found->second.at = now;
    }
    return static_cast<float>(std::clamp(1.0 - (now - found->second.at) / 0.25, 0.0, 1.0));
}

} // namespace cydaw::v2::midi_devices
