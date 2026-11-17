#include <cydaw/base/Thread.h>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <pthread.h>
#endif
namespace cydaw::base {
bool Thread::startThread(Priority priority) {
  if (isThreadRunning())
    return false;
  if (thread_.joinable())
    thread_.join();
  exit_ = false;
  wake_.reset();
  finished_.reset();
  running_ = true;
  try {
    thread_ = std::thread([this] {
      current_ = this;
      setCurrentThreadName(name_);
      try {
        run();
      } catch (...) { /* Thread boundary: owner reports task errors. */
      }
      current_ = nullptr;
      running_.store(false, std::memory_order_release);
      finished_.signal();
    });
    setPriority(priority);
    return true;
  } catch (...) {
    running_ = false;
    finished_.signal();
    return false;
  }
}
bool Thread::stopThread(int milliseconds) {
  signalThreadShouldExit();
  notify();
  if (!thread_.joinable())
    return true;
  if (std::this_thread::get_id() == thread_.get_id())
    return false;
  if (isThreadRunning() && !finished_.wait(milliseconds))
    return false;
  thread_.join();
  return true;
}
bool Thread::setPriority(Priority priority) {
  if (!thread_.joinable())
    return false;
#if defined(_WIN32)
  const int levels[] = {THREAD_PRIORITY_LOWEST, THREAD_PRIORITY_BELOW_NORMAL,
                        THREAD_PRIORITY_NORMAL, THREAD_PRIORITY_ABOVE_NORMAL,
                        THREAD_PRIORITY_HIGHEST};
  return SetThreadPriority(thread_.native_handle(), levels[int(priority)]) != 0;
#else
  (void)priority;
  return false; // Scheduling privileges belong to the device backend.
#endif
}
void Thread::setCurrentThreadName(const Text &name) {
#if defined(_WIN32)
  using SetName = HRESULT(WINAPI *)(HANDLE, PCWSTR);
  auto fn = reinterpret_cast<SetName>(GetProcAddress(
      GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription"));
  if (fn)
    fn(GetCurrentThread(), name.toWideString().c_str());
#elif defined(__APPLE__)
  pthread_setname_np(name.toRawUTF8());
#else
  pthread_setname_np(pthread_self(), name.toStdString().substr(0, 15).c_str());
#endif
}
ThreadPool::ThreadPool(int count) {
  count = std::max(1, count ? count : int(std::thread::hardware_concurrency()));
  for (int i = 0; i < count; ++i)
    workers_.emplace_back([this] { work(); });
}
ThreadPool::~ThreadPool() {
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
    for (auto &entry : entries_)
      entry->job->signalJobShouldExit();
    std::erase_if(entries_, [](auto &entry) { return !entry->running; });
  }
  ready_.notify_all();
  for (auto &worker : workers_)
    if (worker.joinable())
      worker.join();
}
void ThreadPool::work() {
  for (;;) {
    std::shared_ptr<Entry> entry;
    {
      std::unique_lock lock(mutex_);
      ready_.wait(lock, [this] {
        return stopping_ ||
               std::any_of(entries_.begin(), entries_.end(),
                           [](auto &item) { return !item->running; });
      });
      if (stopping_)
        return;
      for (auto &item : entries_)
        if (!item->running) {
          entry = item;
          entry->running = true;
          break;
        }
    }
    ThreadPoolJob::current_ = entry->job;
    auto result = ThreadPoolJob::jobHasFinished;
    try {
      result = entry->job->runJob();
    } catch (...) { /* Worker boundary; completion still releases ownership. */
    }
    ThreadPoolJob::current_ = nullptr;
    {
      std::lock_guard lock(mutex_);
      if (result == ThreadPoolJob::jobNeedsRunningAgain &&
          !entry->job->shouldExit() && !stopping_)
        entry->running = false;
      else
        std::erase(entries_, entry);
    }
    done_.notify_all();
    ready_.notify_all();
  }
}
void ThreadPool::addJob(ThreadPoolJob *job, bool owned) {
  if (!job)
    return;
  auto entry = std::make_shared<Entry>();
  entry->job = job;
  if (owned)
    entry->owned.reset(job);
  {
    std::lock_guard lock(mutex_);
    if (stopping_)
      return;
    entries_.push_back(std::move(entry));
  }
  ready_.notify_one();
}
void ThreadPool::addJob(std::function<void()> function) {
  class FunctionJob : public ThreadPoolJob {
    std::function<void()> callback_;

  public:
    explicit FunctionJob(std::function<void()> f)
        : ThreadPoolJob("CyDaw task"), callback_(std::move(f)) {}
    JobStatus runJob() override {
      if (!shouldExit())
        callback_();
      return jobHasFinished;
    }
  };
  addJob(new FunctionJob(std::move(function)), true);
}
int ThreadPool::getNumJobs() {
  std::lock_guard lock(mutex_);
  return int(entries_.size());
}
ThreadPoolJob *ThreadPool::getJob(int index) {
  std::lock_guard lock(mutex_);
  return index >= 0 && index < int(entries_.size()) ? entries_[index]->job
                                                    : nullptr;
}
bool ThreadPool::waitForJobToFinish(ThreadPoolJob *job, int milliseconds) {
  std::unique_lock lock(mutex_);
  auto finished = [&] {
    return std::none_of(entries_.begin(), entries_.end(),
                        [&](auto &entry) { return entry->job == job; });
  };
  if (milliseconds < 0) {
    done_.wait(lock, finished);
    return true;
  }
  return done_.wait_for(lock, std::chrono::milliseconds(milliseconds),
                        finished);
}
bool ThreadPool::removeAllJobs(bool interrupt, int milliseconds) {
  std::unique_lock lock(mutex_);
  if (interrupt)
    for (auto &entry : entries_)
      entry->job->signalJobShouldExit();
  std::erase_if(entries_, [](auto &entry) { return !entry->running; });
  done_.notify_all();
  if (milliseconds < 0) {
    done_.wait(lock, [this] { return entries_.empty(); });
    return true;
  }
  return done_.wait_for(lock, std::chrono::milliseconds(milliseconds),
                        [this] { return entries_.empty(); });
}
struct HighResolutionTimer::Runner final:Thread {
 HighResolutionTimer& owner;std::atomic<int> period{1};
 explicit Runner(HighResolutionTimer& timer):Thread("CyDaw control clock"),owner(timer){}
 void run()override{using Clock=std::chrono::steady_clock;auto next=Clock::now();while(!threadShouldExit()){
  next+=std::chrono::milliseconds(period.load());
  while(!threadShouldExit()&&Clock::now()<next){const auto left=std::chrono::duration_cast<std::chrono::milliseconds>(next-Clock::now()).count();wait(int(std::clamp<std::int64_t>(left,1,1000)));}
  if(threadShouldExit())break;try{owner.hiResTimerCallback();}catch(...){signalThreadShouldExit();}
  if(Clock::now()-next>std::chrono::milliseconds(period.load()*4))next=Clock::now();
 }}
};
HighResolutionTimer::HighResolutionTimer():runner_(std::make_unique<Runner>(*this)){}
HighResolutionTimer::~HighResolutionTimer(){stopTimer();}
void HighResolutionTimer::startTimer(int milliseconds){stopTimer();runner_->period=std::clamp(milliseconds,1,60000);runner_->startThread(Thread::Priority::high);}
void HighResolutionTimer::stopTimer(){runner_->signalThreadShouldExit();runner_->notify();if(Thread::getCurrentThread()!=runner_.get())runner_->stopThread(-1);}
bool HighResolutionTimer::isTimerRunning()const{return runner_->isThreadRunning();}
} // namespace cydaw::base
