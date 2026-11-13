#pragma once
#include "EventLoop.h"
#include "File.h"
namespace cydaw::base {
struct FileDialogFlags {
  enum {
    openMode = 1,
    saveMode = 2,
    canSelectFiles = 4,
    canSelectDirectories = 8,
    canSelectMultipleItems = 16,
    warnAboutOverwriting = 32
  };
};
class FileDialog {
  struct State;
  std::shared_ptr<State> state_;
  Text title_, filter_;
  File initial_;
  Array<File> results_;

public:
  FileDialog(Text title, File initial = {}, Text filter = {});
  ~FileDialog();
  FileDialog(const FileDialog &) = delete;
  FileDialog &operator=(const FileDialog &) = delete;
  void launchAsync(int flags, std::function<void(const FileDialog &)> callback);
  void cancel();
  File getResult() const { return results_.isEmpty() ? File{} : results_[0]; }
  const Array<File> &getResults() const noexcept { return results_; }
};
} // namespace cydaw::base
