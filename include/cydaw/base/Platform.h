#pragma once
#include "File.h"
#include <memory>
namespace cydaw::base {
// Windows CRT narrow argv uses ACP; native command lines preserve Unicode paths.
TextList commandLineArguments(int argc, char** argv);
class InterProcessLock {
  struct Impl;
  std::unique_ptr<Impl> impl_;

public:
  explicit InterProcessLock(const Text &);
  ~InterProcessLock();
  bool enter(int milliseconds);
  void exit();
};
struct SystemClipboard {
  static void copyTextToClipboard(const Text &);
};
} // namespace cydaw::base
