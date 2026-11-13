#include <cydaw/plugins/WorkerWake.h>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif
namespace cydaw::plugins {
WorkerWake::~WorkerWake() { close(); }
void WorkerWake::open(const wchar_t *name) noexcept {
  close();
#if defined(_WIN32)
  event_ = CreateEventW(nullptr, FALSE, FALSE, name);
#else
  (void)name;
#endif
}
void WorkerWake::close() noexcept {
#if defined(_WIN32)
  if (event_)
    CloseHandle(event_);
#endif
  event_ = nullptr;
}
void WorkerWake::signal() const noexcept {
#if defined(_WIN32)
  if (event_)
    SetEvent(event_);
#endif
}
void WorkerWake::waitAndDispatch() const noexcept {
#if defined(_WIN32)
  // The timed JUCE dispatch loop sleeps in coarse increments. Wake audio
  // independently while still servicing native editor/main-thread messages.
  MSG message;
  for (unsigned n = 0;
       n < 64 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++n) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  auto event = static_cast<HANDLE>(event_);
  MsgWaitForMultipleObjectsEx(event ? 1 : 0, event ? &event : nullptr, 1,
                              QS_ALLINPUT, MWMO_INPUTAVAILABLE);
#endif
}
} // namespace cydaw::plugins
