#pragma once
#include "File.h"
namespace cydaw::base {
// Indexed ZIP reader. Entries stream from disk; callers choose output paths.
class ZipFile {
  struct Impl;
  std::unique_ptr<Impl> impl_;

public:
  struct Entry {
    Text filename;
    int64 uncompressedSize = 0;
    Time fileTime;
  };
  explicit ZipFile(const File &);
  ~ZipFile();
  int getNumEntries() const;
  int getIndexOfFileName(const Text &, bool ignoreCase = false) const;
  const Entry *getEntry(int) const;
  InputStream *createStreamForEntry(int) const;
};
} // namespace cydaw::base
