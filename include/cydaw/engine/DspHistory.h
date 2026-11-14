#pragma once
#include <array>
#include <cassert>
#include <cydsp/core/CircularHistory.h>
#include <new>
#include <stdexcept>
namespace cydaw::engine
{
// Host value semantics for prepared SDK histories. Copying, like the old
// vector, is control-thread work; push/read/reset retain all storage while
// rendering.
template <class T> class DspHistory
{
  public:
    DspHistory() = default;
    DspHistory(const DspHistory& other)
    {
        if (!other.capacity())
            return;
        prepare(other.channels(), other.capacity());
        std::array<T, 64> frame{};
        for (auto age = other.history_.availableFrames(); age > 0; --age)
        {
            for (std::size_t ch = 0; ch < other.channels(); ++ch)
                frame[ch] = other.history_.read(ch, double(age - 1), cydsp::HistoryInterpolation::nearest);
            push({frame.data(), other.channels()});
        }
    }
    DspHistory& operator=(const DspHistory& o)
    {
        if (this != &o)
        {
            DspHistory n(o);
            *this = std::move(n);
        }
        return *this;
    }
    DspHistory(DspHistory&&) noexcept = default;
    DspHistory& operator=(DspHistory&&) noexcept = default;
    void prepare(std::size_t channels, std::size_t frames)
    {
        const auto s = history_.prepare(channels, frames);
        if (s == cydsp::Status::allocation_failed)
            throw std::bad_alloc();
        if (s != cydsp::Status::ok)
            throw std::invalid_argument("DSP history preparation");
    }
    void reset() noexcept { history_.reset(); }
    void push(std::span<const T> frame) noexcept
    {
        const auto s = history_.pushFrame(frame);
        assert(s == cydsp::Status::ok);
        (void)s;
    }
    T read(std::size_t channel, double age, cydsp::HistoryInterpolation interpolation = cydsp::HistoryInterpolation::linear) const noexcept
    {
        return history_.read(channel, age, interpolation);
    }
    std::size_t capacity() const noexcept { return history_.capacity(); }
    std::size_t channels() const noexcept { return history_.channels(); }

  private:
    cydsp::CircularHistory<T> history_;
};
} // namespace cydaw::engine
