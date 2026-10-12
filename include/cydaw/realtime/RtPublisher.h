#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace cydaw::realtime {

// Publishes immutable snapshots from control threads to a single real-time
// reader without ever locking, allocating or deallocating on the audio thread.
//
// `std::atomic<std::shared_ptr<T>>` looks like the natural tool for this, but no
// mainstream standard library implements it lock-free: MSVC, libstdc++ and
// libc++ all guard the control block with an internal spin lock. Reading one
// from an audio callback therefore takes a lock that a preempted control thread
// can hold, performs two reference-count read-modify-writes, and — worst of all
// — runs the snapshot destructor on the audio thread whenever the callback
// happens to drop the last reference. A single graph swap can then free maps,
// vectors and buffers inside the callback and overrun the block deadline.
//
// RtPublisher keeps the reader side on a plain `std::atomic<T*>`, which is
// always lock-free on every supported target. Ownership stays with the writer:
// a replaced snapshot is retired rather than freed, and is only released once
// the audio thread has provably stopped looking at it.
//
// Contract: exactly one real-time reader. That is the shape of an audio
// callback, and it is what makes reclamation cheap — a second concurrent reader
// would break the proof below, so processors that are driven from two threads
// need one publisher each.
//
// Quiescence proof. `state_` packs a monotone epoch above a depth counter of
// borrows currently in flight. Entering a guard adds one epoch step and one
// depth, leaving subtracts one depth. Every operation is sequentially
// consistent, so all of them share one total order. When the writer publishes a
// snapshot it stores the new pointer first and samples `state_` afterwards,
// which places the store before that sample in the total order. A guard that
// raises the epoch past the sampled value therefore entered after the store and
// observes the new pointer, while the previous guard has already left because
// the single reader does not nest. Observing either a higher epoch or a depth
// back at zero is consequently enough to release the retired snapshot, and a
// stopped device — where the epoch never moves again — is covered by the depth.
//
// The depth is a counter rather than a flag so that an accidental nested borrow
// degrades into a late reclamation instead of overflowing into the epoch and
// corrupting the proof outright. A debug assertion still reports it.
template <typename T>
class RtPublisher final {
public:
    // RAII borrow held by the audio thread for exactly one callback. It hands
    // out a raw pointer, so no reference count is touched and no destructor can
    // run on the real-time thread.
    class ReadGuard final {
    public:
        ReadGuard(const ReadGuard&) = delete;
        ReadGuard& operator=(const ReadGuard&) = delete;
        ReadGuard(ReadGuard&&) = delete;
        ReadGuard& operator=(ReadGuard&&) = delete;

        ~ReadGuard() noexcept
        {
            owner_.state_.fetch_sub(depthStep, std::memory_order_seq_cst);
        }

        [[nodiscard]] const T* get() const noexcept { return value_; }
        [[nodiscard]] const T* operator->() const noexcept { return value_; }
        [[nodiscard]] const T& operator*() const noexcept { return *value_; }
        explicit operator bool() const noexcept { return value_ != nullptr; }
        [[nodiscard]] bool operator==(std::nullptr_t) const noexcept { return value_ == nullptr; }
        [[nodiscard]] bool operator!=(std::nullptr_t) const noexcept { return value_ != nullptr; }

    private:
        friend class RtPublisher;

        explicit ReadGuard(const RtPublisher& owner) noexcept
            : owner_(owner)
        {
            const auto previous = owner_.state_.fetch_add(depthStep + epochStep,
                                                          std::memory_order_seq_cst);
            (void) previous;
            assert((previous & depthMask) == 0
                   && "RtPublisher read guards must not nest: one audio thread, one guard");
            value_ = owner_.pointer_.load(std::memory_order_seq_cst);
        }

        const RtPublisher& owner_;
        const T* value_ {};
    };

    RtPublisher() = default;

    explicit RtPublisher(std::shared_ptr<const T> initial)
    {
        store(std::move(initial));
    }

    RtPublisher(const RtPublisher&) = delete;
    RtPublisher& operator=(const RtPublisher&) = delete;
    RtPublisher(RtPublisher&&) = delete;
    RtPublisher& operator=(RtPublisher&&) = delete;

    // Destroying the publisher requires the audio thread to be stopped, exactly
    // like destroying the processor that reads from it.
    ~RtPublisher() = default;

    // Real-time entry point. Wait-free: one atomic add, one atomic load, one
    // atomic subtract, no allocation and no reference counting.
    [[nodiscard]] ReadGuard read() const noexcept { return ReadGuard(*this); }

    // Control-thread publication. The previous snapshot is retired instead of
    // freed so the audio thread can finish the block it already started.
    void store(std::shared_ptr<const T> value)
    {
        const std::lock_guard<std::mutex> writerLock(writerMutex_);
        pointer_.store(value.get(), std::memory_order_seq_cst);
        if (current_ != nullptr)
            retired_.push_back({ std::move(current_), state_.load(std::memory_order_seq_cst) });
        current_ = std::move(value);
        collectLocked();
    }

    // Control-thread view of the published snapshot. Safe to keep alive for as
    // long as the caller needs it because it carries a reference of its own.
    [[nodiscard]] std::shared_ptr<const T> current() const
    {
        const std::lock_guard<std::mutex> writerLock(writerMutex_);
        return current_;
    }

    // Releases every retired snapshot the audio thread can no longer reach.
    // `store` already calls it, so this is only needed when a publisher is
    // written rarely and its snapshots are large.
    void collect()
    {
        const std::lock_guard<std::mutex> writerLock(writerMutex_);
        collectLocked();
    }

    [[nodiscard]] std::size_t retiredCountApprox() const
    {
        const std::lock_guard<std::mutex> writerLock(writerMutex_);
        return retired_.size();
    }

private:
    static constexpr std::uint64_t depthStep = 1;
    static constexpr std::uint64_t depthMask = 0xffff;
    static constexpr std::uint64_t epochStep = depthMask + 1;

    struct Retired {
        std::shared_ptr<const T> snapshot;
        std::uint64_t state {};
    };

    // A retired snapshot is unreachable once the reader either entered a later
    // guard — which by construction already saw the replacement — or is
    // currently outside any guard at all.
    [[nodiscard]] static bool isUnreachable(const std::uint64_t retiredState,
                                            const std::uint64_t currentState) noexcept
    {
        if ((currentState & depthMask) == 0)
            return true;
        return (currentState / epochStep) > (retiredState / epochStep);
    }

    void collectLocked()
    {
        if (retired_.empty())
            return;
        const auto currentState = state_.load(std::memory_order_seq_cst);
        std::size_t kept {};
        for (std::size_t index = 0; index < retired_.size(); ++index) {
            if (isUnreachable(retired_[index].state, currentState))
                continue;
            if (kept != index)
                retired_[kept] = std::move(retired_[index]);
            ++kept;
        }
        // Snapshots released here are destroyed on the control thread, which is
        // the whole point of retiring them in the first place.
        retired_.resize(kept);
    }

    // Mutable so a processor can read its snapshot from a const method: the
    // borrow is logically a read even though it advances the quiescence epoch.
    mutable std::atomic<const T*> pointer_ {};
    mutable std::atomic<std::uint64_t> state_ {};
    mutable std::mutex writerMutex_;
    std::shared_ptr<const T> current_;
    std::vector<Retired> retired_;

    static_assert(std::atomic<const T*>::is_always_lock_free,
                  "The real-time reader must never take a lock");
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                  "The quiescence counter must never take a lock");
};

} // namespace cydaw::realtime
