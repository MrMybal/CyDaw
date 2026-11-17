#include <cydaw/midi/Devices.h>
#include <cydaw/midi/LiveInput.h>
#include <cydaw/audio/Devices.h>
#include <cydaw/base/File.h>
#include <cydaw/base/Text.h>
#include "MidiDevices.h"

#include <imgui.h>

#include <algorithm>
#include <unordered_map>

namespace cydaw::v2::midi_devices {

namespace {

struct Lists {
    cydaw::base::Array<cydaw::midi::DeviceInfo> inputs;
    cydaw::base::Array<cydaw::midi::DeviceInfo> outputs;
    double readAt { -1.0e9 };
};

Lists& lists()
{
    static Lists cached;
    const auto now = cydaw::base::Time::getMillisecondCounterHiRes() * 0.001;
    if (now - cached.readAt > 2.0) {
        cached.inputs = cydaw::midi::Input::getAvailableDevices();
        cached.outputs = cydaw::midi::Output::getAvailableDevices();
        cached.readAt = now;
    }
    return cached;
}

cydaw::base::Text nameIn(const cydaw::base::Array<cydaw::midi::DeviceInfo>& devices, const cydaw::base::Text& identifier)
{
    for (const auto& device : devices)
        if (device.identifier == identifier)
            return device.name;
    return identifier;
}

} // namespace

const cydaw::base::Array<cydaw::midi::DeviceInfo>& inputs()
{
    return lists().inputs;
}

const cydaw::base::Array<cydaw::midi::DeviceInfo>& outputs()
{
    return lists().outputs;
}

cydaw::base::Text inputName(const cydaw::base::Text& identifier)
{
    return nameIn(inputs(), identifier);
}

cydaw::base::Text outputName(const cydaw::base::Text& identifier)
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
