#include <atomic>
#include <cydaw/base/Thread.h>
#include <iostream>
#include <stdexcept>
using namespace cydaw::base;
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
int main() {
  try {
    WaitableEvent event;
    event.signal();
    require(event.wait(0) && !event.wait(0),
            "auto-reset event consumes one signal");
    WaitableEvent manual(true);
    manual.signal();
    require(manual.wait(0) && manual.wait(0), "manual event retains signal");
    manual.reset();
    require(!manual.wait(0), "manual reset");
    struct Worker : Thread {
      WaitableEvent entered;
      std::atomic<bool> ownContext{false};
      Worker() : Thread("native thread test") {}
      ~Worker() { stopThread(-1); }
      void run() override {
        ownContext = Thread::getCurrentThread() == this;
        entered.signal();
        while (!threadShouldExit())
          wait(1000);
      }
    } worker;
    require(worker.startThread() && worker.entered.wait(2000) &&
                worker.ownContext,
            "thread context and start");
    require(worker.stopThread(2000) && !worker.isThreadRunning(),
            "cooperative stop wakes wait");
    require(worker.startThread() && worker.entered.wait(2000) &&
                worker.stopThread(2000),
            "thread can restart");
    std::atomic<int> completed{0};
    {
      ThreadPool pool(3);
      for (int i = 0; i < 64; ++i)
        pool.addJob([&] { ++completed; });
      while (pool.getNumJobs()) {
        auto *job = pool.getJob(0);
        if (job)
          require(pool.waitForJobToFinish(job, 2000), "queued job completes");
      }
    }
    require(completed == 64, "pool drains each independent task exactly once");
    WaitableEvent started;
    std::atomic<bool> cancelled{false};
    {
      ThreadPool pool(1);
      pool.addJob([&] {
        auto *job = ThreadPoolJob::getCurrentThreadPoolJob();
        require(job != nullptr, "pool context reaches lambda cancellation");
        started.signal();
        while (!job->shouldExit())
          Thread::sleep(1);
        cancelled = true;
      });
      require(started.wait(2000), "cancellable task started");
      require(pool.removeAllJobs(true, 2000) && cancelled,
              "cancellation propagates to running task and joins");
    }
    std::cout << "Native events, restart, job ownership and cancellation "
                 "passed without JUCE\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
