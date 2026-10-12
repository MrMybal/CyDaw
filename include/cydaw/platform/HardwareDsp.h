#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cydaw::platform {

enum class HardwareDspCapability : std::uint32_t {
    none = 0,
    acceleratedPlugins = 1u << 0,
    directMonitoring = 1u << 1,
    onboardMixer = 1u << 2,
    internalRouting = 1u << 3,
    clockControl = 1u << 4,
    vendorControlPanel = 1u << 5
};

[[nodiscard]] constexpr HardwareDspCapability operator|(const HardwareDspCapability left,
                                                        const HardwareDspCapability right) noexcept
{
    return static_cast<HardwareDspCapability>(static_cast<std::uint32_t>(left)
                                              | static_cast<std::uint32_t>(right));
}

struct HardwareDspDevice {
    std::string stableId;
    std::string vendor;
    std::string model;
    HardwareDspCapability capabilities { HardwareDspCapability::none };
};

// Vendor SDKs implement this optional control surface. Audio streaming remains
// owned by ASIO/CoreAudio/ALSA, and accelerated plug-ins remain normal plug-ins.
class HardwareDspAdapter {
public:
    virtual ~HardwareDspAdapter() = default;

    [[nodiscard]] virtual std::string_view adapterName() const noexcept = 0;
    [[nodiscard]] virtual std::vector<HardwareDspDevice> scan() = 0;
    [[nodiscard]] virtual bool openVendorControlPanel(std::string_view stableDeviceId) = 0;
};

} // namespace cydaw::platform

