#pragma once
// Opt-in diagnostic only. No audio/MIDI processing is changed. Each trial
// plays one isolated note into an otherwise silent project. Fixed atomics;
// no allocation, lock, wait or logging on the audio callback.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>

namespace cydaw::app::key_latency {
inline const bool enabled = [] { const auto* flag = std::getenv("CYDAW_KEY_LATENCY_PROBE"); return flag && flag[0] == '1'; }();
inline std::atomic<bool> armed { false };
inline std::atomic<int> virtualKey { 0 }, note { -1 }, offset { -1 }, firstSignalFrame { -1 };
inline std::atomic<float> preSignalPeak {};
inline std::atomic<std::uint64_t> windowNs {}, queuedNs {}, collectedNs {}, outputNs {};
inline std::uint64_t now() noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
inline void arm(int key) noexcept {
    armed = false;
    preSignalPeak = 0; virtualKey = key; note = -1; offset = -1; firstSignalFrame = -1;
    windowNs = 0; queuedNs = 0; collectedNs = 0; outputNs = 0;
    armed.store(true, std::memory_order_release);
}
inline void window(int key) noexcept {
    if (enabled && armed.load(std::memory_order_acquire) && virtualKey == key && windowNs == 0)
        windowNs = now();
}
inline void queued(int number) noexcept {
    if (enabled && armed.load(std::memory_order_acquire) && windowNs != 0 && queuedNs == 0) {
        note = number; queuedNs.store(now(), std::memory_order_release);
    }
}
inline void collected(int number, int frame) noexcept {
    if (enabled && armed.load(std::memory_order_acquire) && queuedNs.load(std::memory_order_acquire) != 0
        && note == number && collectedNs == 0) {
        offset = frame; collectedNs.store(now(), std::memory_order_release);
    }
}
template<class Buffer>
inline void output(const Buffer& buffer, int start, int frames) noexcept {
    if (!enabled || !armed.load(std::memory_order_acquire)) return;
    if (collectedNs.load(std::memory_order_acquire) == 0) {
        float peak = 0;
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int frame = 0; frame < frames; ++frame)
                peak = std::max(peak, std::abs(buffer.getReadPointer(channel, start)[frame]));
        preSignalPeak = std::max(preSignalPeak.load(), peak); return;
    }
    for (int frame = 0; frame < frames; ++frame)
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            if (std::abs(buffer.getReadPointer(channel, start)[frame]) > 1.0e-5f) {
                firstSignalFrame = frame; outputNs.store(now(), std::memory_order_release);
                armed.store(false, std::memory_order_release); return;
            }
}
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
} // namespace cydaw::app::key_latency
