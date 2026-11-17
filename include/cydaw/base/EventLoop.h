#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
namespace cydaw::base {
// The application window loop owns dispatch. Producers may enqueue from
// workers; callbacks, timer changes and destruction of callback owners run on
// the UI thread.
class EventLoop {
public:
  static bool post(std::function<void()> callback);
  static void postAfter(int milliseconds, std::function<void()> callback);
  static void dispatchPending();
};
class Timer {
  struct TimerSchedule;
  std::shared_ptr<TimerSchedule> state_;

public:
  Timer();
  virtual ~Timer();
  Timer(const Timer &) = delete;
  Timer &operator=(const Timer &) = delete;
  void startTimer(int milliseconds);
  void startTimerHz(int frequency);
  void stopTimer();
  bool isTimerRunning() const noexcept;
  static void callAfterDelay(int milliseconds, std::function<void()> callback) {
    EventLoop::postAfter(milliseconds, std::move(callback));
  }
  virtual void timerCallback() = 0;
};
class AsyncUpdater {
  struct AsyncDispatch {
    std::atomic<bool> pending{}, alive{true};
  };
  std::shared_ptr<AsyncDispatch> state_ = std::make_shared<AsyncDispatch>();

public:
  virtual ~AsyncUpdater() { state_->alive = false; }
  void triggerAsyncUpdate();
  void cancelPendingUpdate() { state_->pending = false; }
  bool isUpdatePending() const noexcept { return state_->pending.load(); }
  void handleUpdateNowIfNeeded();
  virtual void handleAsyncUpdate() = 0;
};
// Weak ownership for queued UI callbacks, independent of the window system.
class CallbackLifetime {
  std::shared_ptr<void> value_ = std::make_shared<int>(0);

public:
  std::weak_ptr<void> weak() const { return value_; }
};
template <class T> class WeakReference {
  T *pointer_{};
  std::weak_ptr<void> lifetime_;

public:
  WeakReference() = default;
  WeakReference(T *p)
      : pointer_(p),
        lifetime_(p ? p->callbackLifetime_.weak() : std::weak_ptr<void>{}) {}
  T *get() const noexcept { return lifetime_.expired() ? nullptr : pointer_; }
  T *operator->() const noexcept { return get(); }
  operator T *() const noexcept { return get(); }
};
} // namespace cydaw::base
