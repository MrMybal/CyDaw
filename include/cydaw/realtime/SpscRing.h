#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace cydaw::realtime {

// Single-producer/single-consumer bounded ring. Capacity must be a power of two
// so indexing is a cheap mask. T is restricted to POD-like command payloads:
// neither side allocates, locks, constructs, or destroys while communicating.
template <typename T, std::size_t Capacity>
class SpscRing final {
    static_assert(Capacity >= 2, "An SPSC ring needs at least two slots");
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    static_assert(std::is_trivially_copyable_v<T>, "Real-time queue values must be trivially copyable");

public:
    [[nodiscard]] bool tryPush(const T& value) noexcept
    {
        const auto write = writeSequence_.load(std::memory_order_relaxed);
        const auto read = readSequence_.load(std::memory_order_acquire);
        if (write - read >= Capacity)
            return false;

        storage_[static_cast<std::size_t>(write) & indexMask] = value;
        writeSequence_.store(write + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool tryPop(T& value) noexcept
    {
        const auto read = readSequence_.load(std::memory_order_relaxed);
        const auto write = writeSequence_.load(std::memory_order_acquire);
        if (read == write)
            return false;

        value = storage_[static_cast<std::size_t>(read) & indexMask];
        readSequence_.store(read + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::size_t sizeApprox() const noexcept
    {
        const auto write = writeSequence_.load(std::memory_order_acquire);
        const auto read = readSequence_.load(std::memory_order_acquire);
        return static_cast<std::size_t>(write - read);
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    static constexpr std::size_t indexMask = Capacity - 1;
    static constexpr std::size_t cacheLineBytes = 64;
    static_assert(sizeof(std::atomic<std::uint64_t>) <= cacheLineBytes);
    std::array<T, Capacity> storage_ {};
    std::atomic<std::uint64_t> writeSequence_ {};
    std::array<std::byte, cacheLineBytes - sizeof(std::atomic<std::uint64_t>)> producerPadding_ {};
    std::atomic<std::uint64_t> readSequence_ {};
};

} // namespace cydaw::realtime
