// A CyDAW application plug-in: gamepads read through XInput (Windows), each a
// RAW device. Buttons say 0 or 1 ("button/a"…), sticks 0..1 with 0.5 in the
// middle ("axis/leftX"…), triggers 0..1 ("trigger/left"); "rumble/left" and
// "rumble/right" sent back shake it.
//
// Built beside CyDAW 2 as ControlPlugins/Gamepads.cyctl; an example of the
// interface (include/cydaw/control/ControlPluginABI.h) as much as a device.

#include <cydaw/control/ControlPluginABI.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <Xinput.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace {

const CyControlHost* host {};

struct Button {
    WORD mask;
    const char* address;
};
constexpr Button buttons[] {
    { XINPUT_GAMEPAD_A, "button/a" },          { XINPUT_GAMEPAD_B, "button/b" },
    { XINPUT_GAMEPAD_X, "button/x" },          { XINPUT_GAMEPAD_Y, "button/y" },
    { XINPUT_GAMEPAD_LEFT_SHOULDER, "button/lb" }, { XINPUT_GAMEPAD_RIGHT_SHOULDER, "button/rb" },
    { XINPUT_GAMEPAD_BACK, "button/back" },    { XINPUT_GAMEPAD_START, "button/start" },
    { XINPUT_GAMEPAD_LEFT_THUMB, "button/ls" }, { XINPUT_GAMEPAD_RIGHT_THUMB, "button/rs" },
    { XINPUT_GAMEPAD_DPAD_UP, "button/up" },   { XINPUT_GAMEPAD_DPAD_DOWN, "button/down" },
    { XINPUT_GAMEPAD_DPAD_LEFT, "button/left" }, { XINPUT_GAMEPAD_DPAD_RIGHT, "button/right" },
};
constexpr int buttonCount = static_cast<int>(sizeof(buttons) / sizeof(buttons[0]));
constexpr int axisCount = 6;

struct Pad {
    bool connected {};
    // A pad not there is looked for again only now and then: asking for
    // one that is not plugged in is slow.
    ULONGLONG nextLook {};
    std::array<double, buttonCount + axisCount> last {};
    double rumbleLeft {};
    double rumbleRight {};
};
std::array<Pad, XUSER_MAX_COUNT> pads;

std::string idOf(const int index)
{
    return "pad" + std::to_string(index + 1);
}

double axis(const SHORT value)
{
    return std::clamp((static_cast<double>(value) + 32'768.0) / 65'535.0, 0.0, 1.0);
}

void say(const int index, const int slot, const char* address, const double value)
{
    auto& last = pads[static_cast<std::size_t>(index)].last[static_cast<std::size_t>(slot)];
    if (std::abs(last - value) < 1.0e-4)
        return;
    last = value;
    if (host != nullptr)
        host->message(host->context, idOf(index).c_str(), address, value);
}

int start(const CyControlHost* given)
{
    host = given;
    for (auto& pad : pads)
        pad = {};
    return 1;
}

void stop()
{
    for (DWORD index = 0; index < XUSER_MAX_COUNT; ++index)
        if (pads[index].connected) {
            XINPUT_VIBRATION still {};
            XInputSetState(index, &still);
        }
    host = nullptr;
}

void poll()
{
    const auto now = GetTickCount64();
    for (int index = 0; index < static_cast<int>(XUSER_MAX_COUNT); ++index) {
        auto& pad = pads[static_cast<std::size_t>(index)];
        if (!pad.connected && now < pad.nextLook)
            continue;
        XINPUT_STATE state {};
        const auto there = XInputGetState(static_cast<DWORD>(index), &state) == ERROR_SUCCESS;
        if (there != pad.connected) {
            pad.connected = there;
            if (host != nullptr) {
                if (there) {
                    pad.last.fill(-1.0);
                    const auto name = "Gamepad " + std::to_string(index + 1);
                    host->device_added(host->context, idOf(index).c_str(), name.c_str());
                } else {
                    host->device_removed(host->context, idOf(index).c_str());
                }
            }
        }
        if (!there) {
            pad.nextLook = now + 2'000;
            continue;
        }
        const auto& g = state.Gamepad;
        for (int b = 0; b < buttonCount; ++b)
            say(index, b, buttons[b].address, (g.wButtons & buttons[b].mask) != 0 ? 1.0 : 0.0);
        say(index, buttonCount + 0, "axis/leftX", axis(g.sThumbLX));
        say(index, buttonCount + 1, "axis/leftY", axis(g.sThumbLY));
        say(index, buttonCount + 2, "axis/rightX", axis(g.sThumbRX));
        say(index, buttonCount + 3, "axis/rightY", axis(g.sThumbRY));
        say(index, buttonCount + 4, "trigger/left", g.bLeftTrigger / 255.0);
        say(index, buttonCount + 5, "trigger/right", g.bRightTrigger / 255.0);
    }
}

int send(const char* device, const char* address, const double value)
{
    if (device == nullptr || address == nullptr)
        return 0;
    for (int index = 0; index < static_cast<int>(XUSER_MAX_COUNT); ++index) {
        auto& pad = pads[static_cast<std::size_t>(index)];
        if (!pad.connected || idOf(index) != device)
            continue;
        const std::string what(address);
        if (what == "rumble/left")
            pad.rumbleLeft = std::clamp(value, 0.0, 1.0);
        else if (what == "rumble/right")
            pad.rumbleRight = std::clamp(value, 0.0, 1.0);
        else
            return 0;
        XINPUT_VIBRATION vibration {};
        vibration.wLeftMotorSpeed = static_cast<WORD>(pad.rumbleLeft * 65'535.0);
        vibration.wRightMotorSpeed = static_cast<WORD>(pad.rumbleRight * 65'535.0);
        return XInputSetState(static_cast<DWORD>(index), &vibration) == ERROR_SUCCESS ? 1 : 0;
    }
    return 0;
}

} // namespace

extern "C" CYDAW_CONTROL_EXPORT const CyControlPlugin* cydaw_control_plugin(void)
{
    static const CyControlPlugin plugin { CYDAW_CONTROL_ABI_VERSION, "Gamepads (XInput)", "1.0", start, stop, poll, send };
    return &plugin;
}
