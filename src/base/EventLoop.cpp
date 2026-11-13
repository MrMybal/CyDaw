#include <algorithm>
#include <cydaw/base/EventLoop.h>
#include <mutex>
namespace cydaw::base {
namespace {
using Clock = std::chrono::steady_clock;
struct Task {
  Clock::time_point due;
  std::uint64_t order;
  std::function<void()> invoke;
};
struct Queue {
  std::mutex mutex;
  std::vector<Task> tasks;
  std::uint64_t order{};
};
Queue &queue() {
  static Queue state;
  return state;
}
} // namespace
bool EventLoop::post(std::function<void()> callback) {
  postAfter(0, std::move(callback));
  return true;
}
void EventLoop::postAfter(int ms, std::function<void()> callback) {
  if (!callback)
    return;
  auto &q = queue();
  std::lock_guard guard(q.mutex);
  q.tasks.push_back({Clock::now() + std::chrono::milliseconds(std::max(0, ms)),
                     ++q.order, std::move(callback)});
}
void EventLoop::dispatchPending() {
  std::vector<Task> ready;
  auto &q = queue();
  const auto now = Clock::now();
  {
    std::lock_guard guard(q.mutex);
    for (auto it = q.tasks.begin(); it != q.tasks.end();)
      if (it->due <= now) {
        ready.push_back(std::move(*it));
        it = q.tasks.erase(it);
      } else
        ++it;
  }
  std::stable_sort(
      ready.begin(), ready.end(), [](const Task &a, const Task &b) {
        return a.due < b.due || (a.due == b.due && a.order < b.order);
      });
  for (auto &task : ready)
    task.invoke();
}
struct Timer::TimerSchedule
    : std::enable_shared_from_this<Timer::TimerSchedule> {
  Timer *owner{};
  std::atomic<bool> alive{true};
  std::atomic<std::uint64_t> generation{};
  int period{};
  void schedule(std::uint64_t epoch) {
    std::weak_ptr<TimerSchedule> weak = shared_from_this();
    EventLoop::postAfter(period, [weak, epoch] {
      auto state = weak.lock();
      if (!state || !state->alive || state->generation != epoch ||
          state->period <= 0)
        return;
      state->schedule(epoch);
      state->owner->timerCallback();
    });
  }
};
Timer::Timer() : state_(std::make_shared<TimerSchedule>()) {
  state_->owner = this;
}
Timer::~Timer() {
  state_->alive = false;
  ++state_->generation;
}
void Timer::startTimer(int ms) {
  state_->period = std::max(1, ms);
  const auto epoch = ++state_->generation;
  state_->schedule(epoch);
}
void Timer::startTimerHz(int frequency) {
  if (frequency <= 0)
    stopTimer();
  else
    startTimer(std::max(1, 1000 / frequency));
}
void Timer::stopTimer() {
  state_->period = 0;
  ++state_->generation;
}
bool Timer::isTimerRunning() const noexcept { return state_->period > 0; }
void AsyncUpdater::triggerAsyncUpdate() {
  if (state_->pending.exchange(true))
    return;
  auto state = state_;
  EventLoop::post([this, state] {
    if (state->alive && state->pending.exchange(false))
      handleAsyncUpdate();
  });
}
void AsyncUpdater::handleUpdateNowIfNeeded() {
  if (state_->pending.exchange(false))
    handleAsyncUpdate();
}
} // namespace cydaw::base
