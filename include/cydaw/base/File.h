#pragma once
#include "Value.h"
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
namespace cydaw::base {
class MemoryBlock {
  std::vector<uint8> bytes_;

public:
  MemoryBlock() = default;
  explicit MemoryBlock(std::size_t size, bool zero = false) : bytes_(size) {
    (void)zero;
  }
  MemoryBlock(const void *p, std::size_t n) { append(p, n); }
  void *getData() noexcept { return bytes_.data(); }
  const void *getData() const noexcept { return bytes_.data(); }
  std::size_t getSize() const noexcept { return bytes_.size(); }
  bool isEmpty() const noexcept { return bytes_.empty(); }
  void reset() noexcept { bytes_.clear(); }
  void setSize(std::size_t size, bool = false) { bytes_.resize(size); }
  void ensureSize(std::size_t size, bool = false) {
    if (size > bytes_.size())
      bytes_.resize(size);
  }
  void append(const void *p, std::size_t n) {
    if (!n)
      return;
    auto *b = static_cast<const uint8 *>(p);
    bytes_.insert(bytes_.end(), b, b + n);
  }
  void replaceAll(const void *p, std::size_t n) {
    MemoryBlock next(p, n);
    swapWith(next);
  }
  void copyFrom(const void *p, int offset, std::size_t n) {
    if (offset < 0 || std::size_t(offset) > bytes_.size() ||
        n > bytes_.size() - std::size_t(offset))
      return;
    std::memmove(bytes_.data() + offset, p, n);
  }
  void swapWith(MemoryBlock &other) noexcept { bytes_.swap(other.bytes_); }
  Text toString() const {
    return Text::fromUTF8(reinterpret_cast<const char *>(bytes_.data()),
                          int(bytes_.size()));
  }
  Text toBase64Encoding() const;
  bool fromBase64Encoding(const Text &);
  friend bool operator==(const MemoryBlock &, const MemoryBlock &) = default;
};
class InputStream {
public:
  virtual ~InputStream() = default;
  virtual int read(void *, int) = 0;
  virtual int64 getPosition() = 0;
  virtual bool setPosition(int64) = 0;
  virtual int64 getTotalLength() = 0;
  virtual bool isExhausted() { return getPosition() >= getTotalLength(); }
  int64 getNumBytesRemaining() {
    return std::max<int64>(0, getTotalLength() - getPosition());
  }
  uint8 readByte() {
    uint8 b = 0;
    (void)read(&b, 1);
    return b;
  }
  int readInt() {
    uint32 n = 0;
    for (unsigned i = 0; i < 4; ++i)
      n |= uint32(readByte()) << (i * 8);
    return int(n);
  }
  int readIntBigEndian() {
    uint32 n = 0;
    for (unsigned i = 0; i < 4; ++i)
      n = (n << 8) | readByte();
    return int(n);
  }
  short readShort() {
    uint32 n = readByte();
    n |= uint32(readByte()) << 8;
    return short(n);
  }
  short readShortBigEndian() {
    uint32 n = uint32(readByte()) << 8;
    n |= readByte();
    return short(n);
  }
  int64 readInt64() {
    uint64 n = 0;
    for (unsigned i = 0; i < 8; ++i)
      n |= uint64(readByte()) << (i * 8);
    return int64(n);
  }
  double readDouble() {
    auto n = readInt64();
    double value;
    std::memcpy(&value, &n, 8);
    return value;
  }
  float readFloat() {
    auto n = readInt();
    float value;
    std::memcpy(&value, &n, 4);
    return value;
  }
  Text readString() {
    std::string s;
    for (int n = 0; n < 16 * 1024 * 1024 && !isExhausted(); ++n) {
      auto c = readByte();
      if (!c)
        break;
      s += char(c);
    }
    return s;
  }
  Text readEntireStreamAsString() {
    MemoryBlock b;
    readIntoMemoryBlock(b);
    return b.toString();
  }
  int64 readIntoMemoryBlock(MemoryBlock &block, int64 maximum = -1) {
    std::array<uint8, 16384> chunk;
    int64 total = 0;
    for (;;) {
      int count = maximum < 0
                      ? int(chunk.size())
                      : int(std::min<int64>(chunk.size(), maximum - total));
      if (count <= 0)
        break;
      int n = read(chunk.data(), count);
      if (n <= 0)
        break;
      block.append(chunk.data(), std::size_t(n));
      total += n;
    }
    return total;
  }
  void skipNextBytes(int64 n) { (void)setPosition(getPosition() + n); }
};
class OutputStream {
public:
  virtual ~OutputStream() = default;
  virtual bool write(const void *, std::size_t) = 0;
  virtual int64 getPosition() = 0;
  virtual bool setPosition(int64) = 0;
  virtual void flush() = 0;
  bool writeByte(char b) { return write(&b, 1); }
  bool writeInt(int value) {
    uint8 bytes[4];
    for (unsigned i = 0; i < 4; ++i)
      bytes[i] = uint8(uint32(value) >> (8 * i));
    return write(bytes, 4);
  }
  bool writeIntBigEndian(int value) {
    uint8 bytes[4];
    for (unsigned i = 0; i < 4; ++i)
      bytes[3 - i] = uint8(uint32(value) >> (8 * i));
    return write(bytes, 4);
  }
  bool writeShort(short v) {
    uint8 b[2] = {uint8(v), uint8(uint16_t(v) >> 8)};
    return write(b, 2);
  }
  bool writeShortBigEndian(short v) {
    uint8 b[2] = {uint8(uint16_t(v) >> 8), uint8(v)};
    return write(b, 2);
  }
  bool writeInt64(int64 value) {
    uint8 bytes[8];
    for (unsigned i = 0; i < 8; ++i)
      bytes[i] = uint8(uint64(value) >> (8 * i));
    return write(bytes, 8);
  }
  bool writeDouble(double v) {
    int64 i;
    std::memcpy(&i, &v, 8);
    return writeInt64(i);
  }
  bool writeFloat(float v) {
    int i;
    std::memcpy(&i, &v, 4);
    return writeInt(i);
  }
  bool writeString(const Text &s) {
    return write(s.toRawUTF8(), s.getNumBytesAsUTF8() + 1);
  }
  bool writeText(const Text &s, bool = false, bool = false,
                 const char * = "\n");
  OutputStream &operator<<(const Text &s) {
    (void)write(s.toRawUTF8(), s.getNumBytesAsUTF8());
    return *this;
  }
  int64 writeFromInputStream(InputStream &in, int64 maximum = -1) {
    std::array<uint8, 16384> b;
    int64 count = 0;
    for (;;) {
      int n = in.read(b.data(),
                      maximum < 0
                          ? int(b.size())
                          : int(std::min<int64>(b.size(), maximum - count)));
      if (n <= 0 || !write(b.data(), n))
        break;
      count += n;
    }
    return count;
  }
};
class MemoryInputStream : public InputStream {
  const uint8 *data_ = nullptr;
  std::size_t size_ = 0, position_ = 0;
  MemoryBlock own_;

public:
  MemoryInputStream(const void *p, std::size_t n, bool copy = false)
      : data_(static_cast<const uint8 *>(p)), size_(n) {
    if (copy) {
      own_.append(p, n);
      data_ = static_cast<const uint8 *>(own_.getData());
    }
  }
  MemoryInputStream(const MemoryBlock &b, bool copy = false)
      : MemoryInputStream(b.getData(), b.getSize(), copy) {}
  int read(void *out, int maximum) override {
    auto n = std::min<std::size_t>(std::max(0, maximum), size_ - position_);
    if (n)
      std::memcpy(out, data_ + position_, n);
    position_ += n;
    return int(n);
  }
  int64 getPosition() override { return int64(position_); }
  bool setPosition(int64 n) override {
    if (n < 0 || uint64(n) > size_)
      return false;
    position_ = std::size_t(n);
    return true;
  }
  int64 getTotalLength() override { return int64(size_); }
};
class MemoryOutputStream : public OutputStream {
  MemoryBlock own_;
  MemoryBlock *block_ = &own_;
  std::size_t position_ = 0;

public:
  explicit MemoryOutputStream(std::size_t initialSize = 0) {
    own_.ensureSize(initialSize);
    own_.reset();
  }
  MemoryOutputStream(MemoryBlock &b, bool append)
      : block_(&b), position_(append ? b.getSize() : 0) {
    if (!append)
      b.reset();
  }
  bool write(const void *p, std::size_t n) override {
    if (n > std::size_t(INT64_MAX) - position_)
      return false;
    block_->ensureSize(position_ + n);
    block_->copyFrom(p, int(position_), n);
    position_ += n;
    return true;
  }
  int64 getPosition() override { return int64(position_); }
  bool setPosition(int64 n) override {
    if (n < 0 || uint64(n) > block_->getSize())
      return false;
    position_ = std::size_t(n);
    return true;
  }
  void flush() override {}
  void *getData() { return block_->getData(); }
  const void *getData() const { return block_->getData(); }
  std::size_t getDataSize() const { return block_->getSize(); }
  Text toString() const { return block_->toString(); }
  Text toUTF8() const { return toString(); }
  MemoryBlock getMemoryBlock() const { return *block_; }
};
class Time {
  int64 milliseconds_ = 0;

public:
  Time() = default;
  template <class T>
    requires requires(const T &t) { t.toMilliseconds(); }
  Time(const T &time) : milliseconds_(time.toMilliseconds()) {}
  template <class T>
    requires(!std::is_same_v<T, Time> && requires { T::getCurrentTime(); } &&
             std::is_constructible_v<T, int64>)
  operator T() const {
    return T(milliseconds_);
  }
  explicit Time(int64 ms) : milliseconds_(ms) {}
  static Time getCurrentTime() {
    return Time(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count());
  }
  static double getMillisecondCounterHiRes() {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }
  static uint32 getMillisecondCounter() {
    return uint32(uint64(getMillisecondCounterHiRes()));
  }
  static int64 currentTimeMillis() { return getCurrentTime().toMilliseconds(); }
  static int64 getHighResolutionTicks() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }
  static double highResolutionTicksToSeconds(int64 ticks) {
    return double(ticks) * 1.0e-9;
  }
  int64 toMilliseconds() const noexcept { return milliseconds_; }
  Text formatted(const Text &) const;
  Text toISO8601(bool includeDividerCharacters = true) const;
  static Time fromISO8601(const Text &);
  friend auto operator<=>(const Time &, const Time &) = default;
  friend bool operator==(const Time &, const Time &) = default;
  template <class T>
    requires(!std::is_same_v<T, Time> &&
             requires(const T &t) { t.toMilliseconds(); })
  friend bool operator==(const Time &a, const T &b) {
    return a.toMilliseconds() == b.toMilliseconds();
  }
};
class FileInputStream;
class FileOutputStream;
class File {
  std::filesystem::path path_;

public:
  enum SpecialLocationType {
    userHomeDirectory,
    userDocumentsDirectory,
    userDesktopDirectory,
    userMusicDirectory,
    userApplicationDataDirectory,
    commonApplicationDataDirectory,
    tempDirectory,
    currentExecutableFile,
    currentApplicationFile,
    invokedExecutableFile,
    windowsLocalAppData,
    globalApplicationsDirectory,
    windowsProgramFilesCommon
  };
  enum TypesOfFileToFind {
    findDirectories = 1,
    findFiles = 2,
    findFilesAndDirectories = 3,
    ignoreHiddenFiles = 4
  };
  File() = default;
  explicit File(const char *name) : File(Text(name)) {}
  explicit File(const Text &name)
      : path_(std::filesystem::u8path(name.toStdString())) {}
  explicit File(std::filesystem::path p) : path_(std::move(p)) {}
  template <class T>
    requires(requires(const T &t) { t.getFullPathName().toStdString(); })
  File(const T &f) : File(Text(f.getFullPathName().toStdString())) {}
  template <class T>
    requires(!std::is_same_v<T, File> &&
             requires {
               T::createFileWithoutCheckingPath(
                   static_cast<const char *>(nullptr));
             })
  operator T() const {
    return T::createFileWithoutCheckingPath(getFullPathName().toRawUTF8());
  }
  static Text createLegalFileName(const Text &text) {
    auto out = text.removeCharacters("\\/:*?\"<>|");
    for (char32_t c = 1; c < 32; ++c)
      out = out.removeCharacters(Text::charToString(c));
    return out.trimCharactersAtEnd(" .");
  }
  const std::filesystem::path &path() const noexcept { return path_; }
  Text getFullPathName() const;
  Text getFileName() const;
  Text getFileNameWithoutExtension() const;
  Text getFileExtension() const;
  File getParentDirectory() const { return File(path_.parent_path()); }
  File getChildFile(const Text &child) const {
    return File(path_ / std::filesystem::u8path(child.toStdString()));
  }
  File getSiblingFile(const Text &name) const {
    return getParentDirectory().getChildFile(name);
  }
  File withFileExtension(const Text &ext) const;
  bool exists() const;
  bool existsAsFile() const;
  bool isDirectory() const;
  bool hasFileExtension(const Text &) const;
  bool isHidden() const;
  bool isAChildOf(const File &) const;
  int64 getSize() const;
  Time getLastModificationTime() const;
  bool hasWriteAccess() const;
  int getVolumeSerialNumber() const;
  bool hasIdenticalContentTo(const File &) const;
  bool moveToTrash() const;
  int getNumberOfChildFiles(int what, const Text &wildcard = "*") const {
    return findChildFiles(what, false, wildcard).size();
  }
  Text getRelativePathFrom(const File &) const;
  Result createDirectory() const;
  bool create() const;
  bool deleteFile() const;
  bool deleteRecursively(bool followSymlinks = false) const;
  bool moveFileTo(const File &) const;
  bool copyFileTo(const File &) const;
  bool copyDirectoryTo(const File &) const;
  bool replaceWithText(const Text &, bool unicode = false, bool header = false,
                       const char *newlines = "\n") const;
  bool appendText(const Text &, bool unicode = false, bool header = false,
                  const char *newlines = "\n") const;
  bool replaceWithData(const void *, std::size_t) const;
  Text loadFileAsString() const;
  bool loadFileAsData(MemoryBlock &) const;
  std::unique_ptr<FileInputStream> createInputStream() const;
  std::unique_ptr<FileOutputStream>
  createOutputStream(std::size_t buffer = 0) const;
  Array<File> findChildFiles(int what, bool recursive,
                             const Text &wildcard = "*") const;
  int findChildFiles(Array<File> &out, int what, bool recursive,
                     const Text &wildcard = "*") const;
  File getNonexistentChildFile(const Text &prefix, const Text &suffix,
                               bool putNumbersInBrackets = true) const;
  File getNonexistentSibling(bool brackets = true) const {
    return getParentDirectory().getNonexistentChildFile(
        getFileNameWithoutExtension(), getFileExtension(), brackets);
  }
  static bool isAbsolutePath(const Text &t) {
    return std::filesystem::u8path(t.toStdString()).is_absolute();
  }
  static File getCurrentWorkingDirectory() {
    return File(std::filesystem::current_path());
  }
  static File getSpecialLocation(SpecialLocationType);
  static File createTempFile(const Text &extension = {});
  static char getSeparatorChar() {
    return char(std::filesystem::path::preferred_separator);
  }
  static Text getSeparatorString() { return Text(getSeparatorChar()); }
  static bool areFileNamesCaseSensitive() {
#if defined(_WIN32) || defined(__APPLE__)
    return false;
#else
    return true;
#endif
  }
  bool isRoot() const {
    return !path_.empty() && path_.lexically_normal() == path_.root_path();
  }
  int64 getBytesFreeOnVolume() const {
    std::error_code e;
    auto info = std::filesystem::space(path_, e);
    return e ? -1 : int64(info.available);
  }
  static Text descriptionOfSizeInBytes(int64 size) {
    double value = double(size);
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int index = 0;
    while (value >= 1024 && index < 4) {
      value /= 1024;
      ++index;
    }
    return Text(value, index ? 1 : 0) + " " + units[index];
  }
  bool setLastModificationTime(Time) const;
  bool setAsCurrentWorkingDirectory() const {
    std::error_code e;
    std::filesystem::current_path(path_, e);
    return !e;
  }
  bool startAsProcess(const Text &parameters = {}) const;
  void revealToUser() const;
  template <class T>
    requires(!std::is_same_v<T, File> &&
             requires(const T &f) { f.getFullPathName().toStdString(); })
  friend bool operator==(const File &a, const T &b) {
    return a == File(b);
  }
  bool operator==(const File &other) const {
    return path_.lexically_normal() == other.path_.lexically_normal();
  }
  auto operator<=>(const File &other) const {
    return path_.native() <=> other.path_.native();
  }
};
class FileInputStream : public InputStream {
  std::ifstream stream_;
  int64 size_ = 0;
  Result result_;

public:
  explicit FileInputStream(const File &);
  int read(void *, int) override;
  int64 getPosition() override;
  bool setPosition(int64) override;
  int64 getTotalLength() override { return size_; }
  Result getStatus() const { return result_; }
  bool openedOk() const { return result_.wasOk(); }
  bool failedToOpen() const { return result_.failed(); }
};
class FileOutputStream : public OutputStream {
  std::fstream stream_;
  File file_;
  Result result_;

public:
  explicit FileOutputStream(const File &, std::size_t buffer = 0);
  bool write(const void *, std::size_t) override;
  int64 getPosition() override;
  bool setPosition(int64) override;
  void flush() override;
  Result truncate();
  Result getStatus() const { return result_; }
  bool openedOk() const { return result_.wasOk(); }
  bool failedToOpen() const { return result_.failed(); }
};
class TemporaryFile {
  File target_, temporary_;

public:
  explicit TemporaryFile(const File &target)
      : target_(target),
        temporary_(target.getParentDirectory().getNonexistentChildFile(
            ".cydaw-save-" + Text(Time::getMillisecondCounterHiRes()), ".tmp",
            false)) {}
  ~TemporaryFile() { (void)temporary_.deleteFile(); }
  const File &getFile() const { return temporary_; }
  bool overwriteTargetFileWithTemporary() const {
    return temporary_.moveFileTo(target_);
  }
};
struct SystemStats {
  static Text getEnvironmentVariable(const Text &, const Text &fallback = {});
  static Text getOperatingSystemName();
  static int getNumCpus() { return int(std::thread::hardware_concurrency()); }
};
} // namespace cydaw::base
