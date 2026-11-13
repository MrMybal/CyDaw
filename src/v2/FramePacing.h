#pragma once

namespace cydaw::v2::detail {
// Keep fractional deadlines: rounding each frame to the polling interval
// would turn 120 FPS into ~111 FPS. Drop missed frames after a stall.
inline bool frameDue(double now, double framesPerSecond, double& nextFrameSeconds) noexcept
{
    if (now < nextFrameSeconds)
        return false;
    const auto period = 1.0 / framesPerSecond;
    nextFrameSeconds += period;
    if (nextFrameSeconds <= now)
        nextFrameSeconds = now + period;
    return true;
}
} // namespace cydaw::v2::detail
