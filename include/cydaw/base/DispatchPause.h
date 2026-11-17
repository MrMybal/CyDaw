#pragma once

namespace cydaw::base {
namespace detail {
inline thread_local unsigned dispatchPauseDepth{};
}
// Foreign control APIs can pump Windows messages while the application owns
// partially updated state. Defer application tasks/frames until they return;
// the foreign editor's own window messages must still be dispatched normally.
class ScopedEventDispatchPause final {
public:
    ScopedEventDispatchPause() noexcept { ++detail::dispatchPauseDepth; }
    ~ScopedEventDispatchPause() { --detail::dispatchPauseDepth; }
    ScopedEventDispatchPause(const ScopedEventDispatchPause&) = delete;
    ScopedEventDispatchPause& operator=(const ScopedEventDispatchPause&) = delete;
};
inline bool eventDispatchPaused() noexcept { return detail::dispatchPauseDepth != 0; }
} // namespace cydaw::base
