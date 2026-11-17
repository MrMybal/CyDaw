#include <cydaw/base/EventLoop.h>
#include <cydaw/base/DispatchPause.h>
#include <cydaw/base/FileDialog.h>
#include <cydaw/base/Thread.h>
#include <iostream>
#include <stdexcept>
using namespace cydaw::base;
void require(bool value, const char *text) {
  if (!value)
    throw std::runtime_error(text);
}
struct Probe final : Timer, AsyncUpdater {
  int &timers;
  int &updates;
  Probe(int &t, int &a) : timers(t), updates(a) {}
  void timerCallback() override {
    ++timers;
    stopTimer();
  }
  void handleAsyncUpdate() override { ++updates; }
};
struct Owner {
  CallbackLifetime callbackLifetime_;
};
int main() {
  try {
    bool dialogCallback = false;
    {
      FileDialog dialog("Cancelled dialog");
      dialog.launchAsync(FileDialogFlags::openMode,
                         [&](const FileDialog &) { dialogCallback = true; });
      dialog.cancel();
    }
    EventLoop::dispatchPending();
    require(!dialogCallback, "cancelled or destroyed file dialog does not open "
                             "or invoke stale callback");
    int count = 0;
    EventLoop::post([&] {
      ++count;
      EventLoop::post([&] { ++count; });
    });
    EventLoop::dispatchPending();
    require(count == 1,
            "callbacks enqueued during dispatch wait until next turn");
    EventLoop::dispatchPending();
    require(count == 2, "next dispatch performs queued callbacks");
    bool deferred = false;
    EventLoop::post([&] { deferred = true; });
    {
      ScopedEventDispatchPause pause;
      { ScopedEventDispatchPause nested; EventLoop::dispatchPending(); }
      EventLoop::dispatchPending();
      require(!deferred && eventDispatchPaused(),
              "nested foreign-message pumps defer application callbacks");
    }
    EventLoop::dispatchPending();
    require(deferred && !eventDispatchPaused(), "deferred work resumes after foreign control returns");
    bool outerRunning = false, reentered = false, innerRan = false;
    EventLoop::post([&] {
      outerRunning = true;
      EventLoop::post([&] { reentered = outerRunning; innerRan = true; });
      EventLoop::dispatchPending();
      outerRunning = false;
    });
    EventLoop::dispatchPending();
    require(!innerRan, "a timer cannot recursively drain application tasks");
    EventLoop::dispatchPending();
    require(innerRan && !reentered, "queued callback runs after its owner returns");
    int timers = 0, updates = 0;
    {
      Probe probe(timers, updates);
      for (int i = 0; i < 100; ++i)
        probe.triggerAsyncUpdate();
      require(probe.isUpdatePending(), "async notification pending");
      EventLoop::dispatchPending();
      require(updates == 1 && !probe.isUpdatePending(),
              "async notifications coalesce");
      probe.triggerAsyncUpdate();
      probe.cancelPendingUpdate();
      EventLoop::dispatchPending();
      require(updates == 1, "async notification cancellation");
      probe.startTimer(1);
      Thread::sleep(3);
      EventLoop::dispatchPending();
      require(timers == 1 && !probe.isTimerRunning(),
              "timer fires and can stop itself");
      probe.startTimer(1);
      probe.triggerAsyncUpdate();
    }
    Thread::sleep(3);
    EventLoop::dispatchPending();
    require(timers == 1 && updates == 1,
            "destroyed timer and async owners are not called");
    WeakReference<Owner> weak;
    {
      Owner owner;
      weak = &owner;
      require(weak.get() == &owner, "live callback owner");
    }
    require(weak.get() == nullptr, "destroyed callback owner expires");
    bool delayed = false;
    EventLoop::postAfter(50, [&] { delayed = true; });
    EventLoop::dispatchPending();
    require(!delayed, "delayed callbacks do not run prematurely");
    Thread::sleep(55);
    EventLoop::dispatchPending();
    require(delayed, "delayed callback runs");
    std::cout << "Native UI dispatch, timers, cancellation, weak callbacks and "
                 "coalescing passed without JUCE\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
