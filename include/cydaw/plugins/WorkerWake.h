#pragma once
namespace cydaw::plugins {
// Optional native notification; signaling never waits on the audio callback.
class WorkerWake final {
  void *event_ = nullptr;

public:
  WorkerWake() = default;
  ~WorkerWake();
  WorkerWake(const WorkerWake &) = delete;
  WorkerWake &operator=(const WorkerWake &) = delete;
  void open(const wchar_t *name) noexcept;
  void close() noexcept;
  void signal() const noexcept;
  void waitAndDispatch() const noexcept;
};
} // namespace cydaw::plugins
