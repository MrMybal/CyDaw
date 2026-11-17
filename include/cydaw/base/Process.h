#pragma once
#include "File.h"
namespace cydaw::base {
class ChildProcess {
  struct Native;
  std::unique_ptr<Native> native_;

public:
  enum { wantStdOut = 1, wantStdErr = 2 };
  ChildProcess();
  ~ChildProcess();
  ChildProcess(const ChildProcess &) = delete;
  ChildProcess &operator=(const ChildProcess &) = delete;
  bool start(const TextList &arguments, int flags = wantStdOut | wantStdErr);
  bool start(const Text &command, int flags = wantStdOut | wantStdErr);
  bool isRunning() const;
  bool waitForProcessToFinish(int timeoutMs);
  bool kill();
  int readProcessOutput(void *buffer, int maximumBytes);
  Text readAllProcessOutput();
  unsigned getExitCode() const;
};
class MemoryMappedFile {
  struct Native;
  std::unique_ptr<Native> native_;

public:
  enum AccessMode { readOnly, readWrite };
  MemoryMappedFile(const File &, AccessMode, bool exclusive = false);
  ~MemoryMappedFile();
  MemoryMappedFile(const MemoryMappedFile &) = delete;
  MemoryMappedFile &operator=(const MemoryMappedFile &) = delete;
  void *getData() const noexcept;
  std::size_t getSize() const noexcept;
};
class DynamicLibrary {
  void *handle_{};

public:
  DynamicLibrary() = default;
  ~DynamicLibrary();
  DynamicLibrary(const DynamicLibrary &) = delete;
  DynamicLibrary &operator=(const DynamicLibrary &) = delete;
  bool open(const Text &path);
  void close();
  void *getFunction(const Text &symbol) const;
};
} // namespace cydaw::base
