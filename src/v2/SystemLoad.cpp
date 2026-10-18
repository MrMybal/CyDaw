#include "SystemLoad.h"

#include <juce_core/juce_core.h>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <psapi.h>
#endif

#include <algorithm>

namespace cydaw::v2::system_load {

Reading read()
{
    static Reading last;
    static double readAt = -1.0e9;
#if JUCE_WINDOWS
    static unsigned long long lastBusy {};
    static unsigned long long lastWall {};
#endif
    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (now - readAt < 0.5)
        return last;
    readAt = now;
#if JUCE_WINDOWS
    const auto ticks = [](const FILETIME& time) {
        return (static_cast<unsigned long long>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
    };
    FILETIME created {}, exited {}, kernel {}, user {}, wall {};
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        GetSystemTimeAsFileTime(&wall);
        const auto busy = ticks(kernel) + ticks(user);
        const auto wallNow = ticks(wall);
        if (lastWall != 0 && wallNow > lastWall) {
            const auto cores = std::max(1, juce::SystemStats::getNumCpus());
            last.cpuPercent = std::clamp(static_cast<double>(busy - lastBusy) / static_cast<double>(wallNow - lastWall) / cores * 100.0,
                                         0.0, 100.0);
        }
        lastBusy = busy;
        lastWall = wallNow;
    }
    PROCESS_MEMORY_COUNTERS counters {};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        last.memoryBytes = static_cast<double>(counters.WorkingSetSize);
#endif
    return last;
}

} // namespace cydaw::v2::system_load
