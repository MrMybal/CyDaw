#pragma once
#include "Text.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <utility>
namespace cydaw::base {
class WaitableEvent {
  mutable std::mutex mutex_;
  mutable std::condition_variable ready_;
  mutable bool signalled_ = false;
  bool manual_;

public:
  explicit WaitableEvent(bool manualReset = false) : manual_(manualReset) {}
  void signal() const {
    {
      std::lock_guard lock(mutex_);
      signalled_ = true;
    }
    if (manual_)
      ready_.notify_all();
    else
      ready_.notify_one();
  }
  void reset() const {
    std::lock_guard lock(mutex_);
    signalled_ = false;
  }
  bool wait(double milliseconds = -1) const {
    std::unique_lock lock(mutex_);
    bool ok = true;
    if (milliseconds < 0)
      ready_.wait(lock, [this] { return signalled_; });
    else
      ok = ready_.wait_for(
          lock, std::chrono::duration<double, std::milli>(milliseconds),
          [this] { return signalled_; });
    if (ok && !manual_)
      signalled_ = false;
    return ok;
  }
};
class Thread {
public:
  enum class Priority { lowest, low, normal, high, highest };

private:
  Text name_;
  std::thread thread_;
  std::atomic<bool> exit_{false}, running_{false};
  mutable WaitableEvent wake_, finished_;
  inline static thread_local Thread *current_ = nullptr;

public:
  explicit Thread(Text name) : name_(std::move(name)) {}
  virtual ~Thread() {
    signalThreadShouldExit();
    notify();
    if (thread_.joinable())
      thread_.join();
  }
  Thread(const Thread &) = delete;
  Thread &operator=(const Thread &) = delete;
  virtual void run() = 0;
  bool startThread(Priority priority = Priority::normal);
  bool stopThread(int timeoutMilliseconds);
  void signalThreadShouldExit() {
    exit_.store(true, std::memory_order_release);
  }
  bool threadShouldExit() const {
    return exit_.load(std::memory_order_acquire);
  }
  bool isThreadRunning() const {
    return running_.load(std::memory_order_acquire);
  }
  bool wait(int milliseconds) const { return wake_.wait(milliseconds); }
  void notify() const { wake_.signal(); }
  bool setPriority(Priority);
  static void setCurrentThreadName(const Text &);
  static Thread *getCurrentThread() { return current_; }
  static bool currentThreadShouldExit() {
    return current_ && current_->threadShouldExit();
  }
  static void sleep(int milliseconds) {
    if (milliseconds > 0)
      std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    else
      std::this_thread::yield();
  }
  template <class Function> static bool launch(Function function) {
    try {
      std::thread(std::move(function)).detach();
      return true;
    } catch (...) {
      return false;
    }
  }
};
class ThreadPoolJob {
  Text name_;
  std::atomic<bool> exit_{false};
  inline static thread_local ThreadPoolJob *current_ = nullptr;
  friend class ThreadPool;

public:
  enum JobStatus { jobHasFinished, jobNeedsRunningAgain };
  explicit ThreadPoolJob(Text name) : name_(std::move(name)) {}
  virtual ~ThreadPoolJob() = default;
  virtual JobStatus runJob() = 0;
  bool shouldExit() const { return exit_.load(std::memory_order_acquire); }
  void signalJobShouldExit() { exit_.store(true, std::memory_order_release); }
  static ThreadPoolJob *getCurrentThreadPoolJob() { return current_; }
  const Text &getJobName() const { return name_; }
};
class ThreadPool {
  struct Entry {
    ThreadPoolJob *job;
    std::unique_ptr<ThreadPoolJob> owned;
    bool running = false;
  };
  std::mutex mutex_;
  std::condition_variable ready_, done_;
  std::deque<std::shared_ptr<Entry>> entries_;
  std::vector<std::thread> workers_;
  bool stopping_ = false;
  void work();

public:
  explicit ThreadPool(int count = 0);
  ~ThreadPool();
  ThreadPool(const ThreadPool &) = delete;
  ThreadPool &operator=(const ThreadPool &) = delete;
  void addJob(ThreadPoolJob *, bool deleteWhenFinished);
  void addJob(std::function<void()>);
  int getNumJobs();
  ThreadPoolJob *getJob(int index);
  bool waitForJobToFinish(ThreadPoolJob *, int milliseconds);
  bool removeAllJobs(bool interruptRunningJobs, int milliseconds);
};
class CriticalSection {
  mutable std::recursive_mutex mutex_;

public:
  void enter() const { mutex_.lock(); }
  bool tryEnter() const { return mutex_.try_lock(); }
  void exit() const { mutex_.unlock(); }
  void lock() const { enter(); }
  void unlock() const { exit(); }
  bool try_lock() const { return tryEnter(); }
  using ScopedLockType = std::lock_guard<const CriticalSection>;
};
template <class Lock> class ScopedLock {
  const Lock &lock_;

public:
  explicit ScopedLock(const Lock &lock) : lock_(lock) { lock_.enter(); }
  ~ScopedLock() { lock_.exit(); }
  ScopedLock(const ScopedLock &) = delete;
};
template <class Lock> class ScopedTryLock {
  const Lock &lock_;
  bool locked_;

public:
  explicit ScopedTryLock(const Lock &lock)
      : lock_(lock), locked_(lock_.tryEnter()) {}
  ~ScopedTryLock() {
    if (locked_)
      lock_.exit();
  }
  bool isLocked() const { return locked_; }
  ScopedTryLock(const ScopedTryLock &) = delete;
};
class SpinLock {
  mutable std::atomic_flag flag_ = ATOMIC_FLAG_INIT;

public:
  void enter() const {
    while (flag_.test_and_set(std::memory_order_acquire))
      std::this_thread::yield();
  }
  bool tryEnter() const {
    return !flag_.test_and_set(std::memory_order_acquire);
  }
  void exit() const { flag_.clear(std::memory_order_release); }
  void lock() const { enter(); }
  void unlock() const { exit(); }
  using ScopedLockType = std::lock_guard<const SpinLock>;
};
template <class T> class ScopedValueSetter {
  T &target_;
  T previous_;

public:
  ScopedValueSetter(T &target, T value)
      : target_(target), previous_(std::exchange(target, std::move(value))) {}
  ScopedValueSetter(T &target, T value, T restore)
      : target_(target), previous_(std::move(restore)) {
    target_ = std::move(value);
  }
  ~ScopedValueSetter() { target_ = std::move(previous_); }
};
} // namespace cydaw::base
