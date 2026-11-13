#include <cstdio>
#include <cstdlib>
#include <cydaw/base/File.h>
#include <random>
#if defined(_WIN32)
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
// clang-format on
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace cydaw::base {
namespace {
Text textPath(const std::filesystem::path &p) {
  auto s = p.u8string();
  return Text::fromUTF8(reinterpret_cast<const char *>(s.data()),
                        int(s.size()));
}
bool wildcard(std::string_view p, std::string_view s) {
  std::size_t i = 0, j = 0, star = std::string_view::npos, match = 0;
  while (j < s.size()) {
    if (i < p.size() && (p[i] == '?' || std::tolower(uint8(p[i])) ==
                                            std::tolower(uint8(s[j])))) {
      ++i;
      ++j;
    } else if (i < p.size() && p[i] == '*') {
      star = i++;
      match = j;
    } else if (star != std::string_view::npos) {
      i = star + 1;
      j = ++match;
    } else
      return false;
  }
  while (i < p.size() && p[i] == '*')
    ++i;
  return i == p.size();
}
} // namespace
Text File::getFullPathName() const {
  if (path_.empty())
    return {};
  std::error_code e;
  auto p = std::filesystem::absolute(path_, e);
  return textPath((e ? path_ : p).lexically_normal());
}
Text File::getFileName() const { return textPath(path_.filename()); }
Text File::getFileNameWithoutExtension() const {
  return textPath(path_.stem());
}
Text File::getFileExtension() const { return textPath(path_.extension()); }
File File::withFileExtension(const Text &ext) const {
  auto p = path_;
  p.replace_extension(std::filesystem::u8path(
      ext.startsWithChar('.')
          ? ext.toStdString()
          : (ext.isEmpty() ? std::string{} : ("." + ext).toStdString())));
  return File(p);
}
bool File::exists() const {
  std::error_code e;
  return !path_.empty() && std::filesystem::exists(path_, e);
}
bool File::existsAsFile() const {
  std::error_code e;
  return std::filesystem::is_regular_file(path_, e);
}
bool File::isDirectory() const {
  std::error_code e;
  return std::filesystem::is_directory(path_, e);
}
int64 File::getSize() const {
  std::error_code e;
  auto n = std::filesystem::file_size(path_, e);
  return e ? 0 : int64(n);
}
bool File::hasFileExtension(const Text &extensions) const {
  auto ext = getFileExtension().trimStart().toLowerCase();
  for (auto &s : TextList::fromTokens(extensions, ";,|", {}))
    if (ext == (s.startsWithChar('.') ? s : "." + s).toLowerCase())
      return true;
  return false;
}
bool File::isHidden() const {
#if defined(_WIN32)
  auto a = GetFileAttributesW(path_.c_str());
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_HIDDEN);
#else
  return getFileName().startsWithChar('.');
#endif
}
bool File::isAChildOf(const File &other) const {
  std::error_code e;
  auto child = std::filesystem::weakly_canonical(path_, e);
  if (e)
    return false;
  auto parent = std::filesystem::weakly_canonical(other.path_, e);
  if (e)
    return false;
  auto c = child.begin(), p = parent.begin();
  for (; p != parent.end(); ++p, ++c)
    if (c == child.end() || *p != *c)
      return false;
  return c != child.end();
}
Text File::getRelativePathFrom(const File &f) const {
  std::error_code e;
  auto p = std::filesystem::relative(path_, f.path_, e);
  return e || p.empty() ? getFullPathName() : textPath(p);
}
Time File::getLastModificationTime() const {
#if defined(_WIN32)
  WIN32_FILE_ATTRIBUTE_DATA data{};
  if (!GetFileAttributesExW(path_.c_str(), GetFileExInfoStandard, &data))
    return {};
  ULARGE_INTEGER ticks;
  ticks.HighPart = data.ftLastWriteTime.dwHighDateTime;
  ticks.LowPart = data.ftLastWriteTime.dwLowDateTime;
  return Time(int64((ticks.QuadPart - 116444736000000000ull) / 10000));
#else
  std::error_code e;
  auto t = std::filesystem::last_write_time(path_, e);
  return e ? Time{}
           : Time(std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::file_clock::to_sys(t).time_since_epoch())
                      .count());
#endif
}
bool File::hasWriteAccess() const {
  if (!exists())
    return getParentDirectory().isDirectory() &&
           getParentDirectory().hasWriteAccess();
  std::error_code e;
  auto p = std::filesystem::status(path_, e).permissions();
  return !e && (p & (std::filesystem::perms::owner_write |
                     std::filesystem::perms::group_write |
                     std::filesystem::perms::others_write)) !=
                   std::filesystem::perms::none;
}
Result File::createDirectory() const {
  std::error_code e;
  std::filesystem::create_directories(path_, e);
  return e ? Result::fail(e.message()) : Result::ok();
}
bool File::create() const {
  if (existsAsFile())
    return true;
  std::ofstream f(path_, std::ios::binary);
  return bool(f);
}
bool File::deleteFile() const {
  std::error_code e;
  if (!exists())
    return true;
  return std::filesystem::remove(path_, e) && !e;
}
bool File::deleteRecursively(bool) const {
  std::error_code e;
  auto p = std::filesystem::absolute(path_, e).lexically_normal();
  if (e || path_.empty() || p == p.root_path())
    return false;
  std::filesystem::remove_all(p, e);
  return !e;
}
bool File::moveFileTo(const File &to) const {
#if defined(_WIN32)
  return MoveFileExW(path_.c_str(), to.path_.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  std::error_code e;
  std::filesystem::rename(path_, to.path_, e);
  return !e;
#endif
}
bool File::copyFileTo(const File &to) const {
  std::error_code e;
  std::filesystem::copy_file(
      path_, to.path_, std::filesystem::copy_options::overwrite_existing, e);
  return !e;
}
bool File::copyDirectoryTo(const File &to) const {
  std::error_code e;
  std::filesystem::copy(path_, to.path_,
                        std::filesystem::copy_options::recursive |
                            std::filesystem::copy_options::overwrite_existing,
                        e);
  return !e;
}
bool File::replaceWithText(const Text &text, bool unicode, bool header,
                           const char *newlines) const {
  FileOutputStream out(*this);
  if (!out.openedOk() || !out.setPosition(0) || out.truncate().failed())
    return false;
  bool ok = out.writeText(text, unicode, header, newlines);
  out.flush();
  return ok && out.getStatus().wasOk();
}
bool File::appendText(const Text &text, bool unicode, bool header,
                      const char *newlines) const {
  FileOutputStream out(*this);
  return out.openedOk() && out.writeText(text, unicode, header, newlines);
}
bool File::replaceWithData(const void *data, std::size_t n) const {
  std::ofstream f(path_, std::ios::binary | std::ios::trunc);
  return bool(f.write(static_cast<const char *>(data), std::streamsize(n)));
}
Text File::loadFileAsString() const {
  FileInputStream in(*this);
  return in.openedOk() ? in.readEntireStreamAsString() : Text{};
}
bool File::loadFileAsData(MemoryBlock &b) const {
  FileInputStream in(*this);
  if (!in.openedOk())
    return false;
  MemoryBlock next;
  in.readIntoMemoryBlock(next);
  if (int64(next.getSize()) != in.getTotalLength())
    return false;
  b.swapWith(next);
  return true;
}
std::unique_ptr<FileInputStream> File::createInputStream() const {
  auto p = std::make_unique<FileInputStream>(*this);
  return p->openedOk() ? std::move(p) : nullptr;
}
std::unique_ptr<FileOutputStream>
File::createOutputStream(std::size_t b) const {
  auto p = std::make_unique<FileOutputStream>(*this, b);
  return p->openedOk() ? std::move(p) : nullptr;
}
int File::findChildFiles(Array<File> &out, int what, bool recursive,
                         const Text &mask) const {
  int before = out.size();
  auto patterns = TextList::fromTokens(mask, ";,", {});
  auto matches = [&](const Text &name) {
    return std::any_of(
        patterns.begin(), patterns.end(), [&](const Text &pattern) {
          return wildcard(pattern.trim().toStdString(), name.toStdString());
        });
  };
  auto add = [&](const auto &entry) {
    File f(entry.path());
    if ((what & ignoreHiddenFiles) && f.isHidden())
      return;
    if ((f.isDirectory() ? (what & findDirectories) : (what & findFiles)) &&
        matches(f.getFileName()))
      out.add(std::move(f));
  };
  std::error_code e;
  if (recursive) {
    std::filesystem::recursive_directory_iterator i(
        path_, std::filesystem::directory_options::skip_permission_denied, e),
        end;
    for (; !e && i != end; i.increment(e))
      add(*i);
  } else {
    std::filesystem::directory_iterator i(
        path_, std::filesystem::directory_options::skip_permission_denied, e),
        end;
    for (; !e && i != end; i.increment(e))
      add(*i);
  }
  return out.size() - before;
}
Array<File> File::findChildFiles(int what, bool recursive,
                                 const Text &mask) const {
  Array<File> out;
  findChildFiles(out, what, recursive, mask);
  return out;
}
File File::getNonexistentChildFile(const Text &prefix, const Text &suffix,
                                   bool brackets) const {
  auto f = getChildFile(prefix + suffix);
  for (int n = 2; f.exists(); ++n)
    f = getChildFile(prefix + (brackets ? " (" : "-") + Text(n) +
                     (brackets ? ")" : "") + suffix);
  return f;
}
struct RangedDirectoryIterator::Cursor {
  std::filesystem::directory_iterator flat;
  std::filesystem::recursive_directory_iterator tree;
  TextList patterns;
  Entry current;
  bool recursive{}, ended{};
  int flags{};
  Cursor(const File &root, bool recurse, const Text &mask, int what)
      : patterns(TextList::fromTokens(mask, ";,", {})), recursive(recurse),
        flags(what) {
    std::error_code error;
    auto options = std::filesystem::directory_options::skip_permission_denied;
    if (recursive)
      tree = std::filesystem::recursive_directory_iterator(root.path(), options,
                                                           error);
    else
      flat = std::filesystem::directory_iterator(root.path(), options, error);
    ended = bool(error);
    seek(false);
  }
  void seek(bool advance) {
    if (ended)
      return;
    std::error_code error;
    auto increment = [&] {
      if (recursive)
        tree.increment(error);
      else
        flat.increment(error);
    };
    if (advance)
      increment();
    for (;;) {
      if (error ||
          (recursive ? tree == std::filesystem::recursive_directory_iterator{}
                     : flat == std::filesystem::directory_iterator{})) {
        ended = true;
        return;
      }
      current.file = File(recursive ? tree->path() : flat->path());
      const auto &file = current.file;
      if (!((flags & File::ignoreHiddenFiles) && file.isHidden()) &&
          (file.isDirectory() ? (flags & File::findDirectories)
                              : (flags & File::findFiles)) &&
          std::any_of(patterns.begin(), patterns.end(), [&](const Text &mask) {
            return wildcard(mask.trim().toStdString(),
                            file.getFileName().toStdString());
          }))
        return;
      increment();
    }
  }
};
RangedDirectoryIterator::Iterator::Iterator(std::shared_ptr<Cursor> cursor)
    : cursor_(std::move(cursor)) {}
const RangedDirectoryIterator::Entry &
RangedDirectoryIterator::Iterator::operator*() const {
  return cursor_->current;
}
RangedDirectoryIterator::Iterator &
RangedDirectoryIterator::Iterator::operator++() {
  cursor_->seek(true);
  return *this;
}
bool RangedDirectoryIterator::Iterator::operator!=(End) const {
  return cursor_ && !cursor_->ended;
}
RangedDirectoryIterator::Iterator RangedDirectoryIterator::begin() const {
  return Iterator(
      std::make_shared<Cursor>(root_, recursive_, wildcard_, flags_));
}

Text SystemStats::getEnvironmentVariable(const Text &key,
                                         const Text &fallback) {
#if defined(_WIN32)
  const auto name = key.toWideString();
  SetLastError(ERROR_SUCCESS);
  auto required = GetEnvironmentVariableW(name.c_str(), nullptr, 0);
  if (!required)
    return GetLastError() == ERROR_ENVVAR_NOT_FOUND ? fallback : Text{};
  for (int attempt = 0; attempt < 3; ++attempt) {
    std::wstring value(required, L'\0');
    auto count = GetEnvironmentVariableW(name.c_str(), value.data(), required);
    if (!count)
      return GetLastError() == ERROR_ENVVAR_NOT_FOUND ? fallback : Text{};
    if (count < required) {
      value.resize(count);
      return Text(value.c_str());
    }
    required = count;
  }
  return fallback;
#else
  auto *value = std::getenv(key.toRawUTF8());
  return value ? Text::fromUTF8(value) : fallback;
#endif
}
Text SystemStats::getOperatingSystemName() {
#if defined(_WIN32)
  return "Windows";
#elif defined(__APPLE__)
  return "macOS";
#else
  return "Linux";
#endif
}
File File::getSpecialLocation(SpecialLocationType kind) {
  auto env = [](const char *key) {
    return SystemStats::getEnvironmentVariable(key);
  };
  File home(env(
#if defined(_WIN32)
      "USERPROFILE"
#else
      "HOME"
#endif
      ));

#if defined(_WIN32)
  const auto known = [](REFKNOWNFOLDERID id) {
    PWSTR path = nullptr;
    File result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &path)) && path) {
      result = File(std::filesystem::path(path));
      CoTaskMemFree(path);
    }
    return result;
  };
  if (kind == windowsProgramFilesCommon)
    return known(FOLDERID_ProgramFilesCommon);
  if (kind == globalApplicationsDirectory)
    return known(FOLDERID_ProgramFiles);
  const auto actualHome = known(FOLDERID_Profile);
  if (actualHome.getFullPathName().equalsIgnoreCase(home.getFullPathName())) {
    if (kind == userDocumentsDirectory)
      return known(FOLDERID_Documents);
    if (kind == userDesktopDirectory)
      return known(FOLDERID_Desktop);
    if (kind == userMusicDirectory)
      return known(FOLDERID_Music);
  }
#endif
  switch (kind) {
  case userHomeDirectory:
    return home;
  case tempDirectory:
    return File(std::filesystem::temp_directory_path());
  case userDocumentsDirectory:
    return home.getChildFile("Documents");
  case userDesktopDirectory:
    return home.getChildFile("Desktop");
  case userMusicDirectory:
    return home.getChildFile("Music");
  case userApplicationDataDirectory:
#if defined(_WIN32)
    return File(env("APPDATA"));
#elif defined(__APPLE__)
    return home.getChildFile("Library/Application Support");
#else
    return env("XDG_CONFIG_HOME").isEmpty() ? home.getChildFile(".config")
                                            : File(env("XDG_CONFIG_HOME"));
#endif
  case windowsLocalAppData:
#if defined(_WIN32)
    return File(env("LOCALAPPDATA"));
#else
    return home.getChildFile(".local/share");
#endif
  case commonApplicationDataDirectory:
#if defined(_WIN32)
    return File(env("PROGRAMDATA"));
#else
    return File(Text("/usr/share"));
#endif
  case currentApplicationFile:
  case currentExecutableFile:
  case invokedExecutableFile: {
#if defined(_WIN32)
    std::wstring name(32768, L'\0');
    auto n = GetModuleFileNameW(nullptr, name.data(), DWORD(name.size()));
    name.resize(n);
    return File(std::filesystem::path(name));
#else
    std::array<char, 4096> name{};
    auto n = readlink("/proc/self/exe", name.data(), name.size() - 1);
    return n > 0 ? File(Text::fromUTF8(name.data(), int(n))) : File{};
#endif
  }
  default:
    return getCurrentWorkingDirectory();
  }
}
File File::createTempFile(const Text &extension) {
  std::random_device random;
  return getSpecialLocation(tempDirectory)
      .getNonexistentChildFile(
          "cydaw-" + Text::toHexString(uint64(random()) << 32 | random()),
          extension, false);
}
bool File::startAsProcess(const Text &parameters) const {
#if defined(_WIN32)
  auto p = parameters.toWideString();
  return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open",
                                                 path_.c_str(), p.c_str(),
                                                 nullptr, SW_SHOWNORMAL)) > 32;
#else
  (void)parameters;
  return false;
#endif
}
void File::revealToUser() const {
#if defined(_WIN32)
  auto args = Text("/select,") + getFullPathName().quoted();
  auto wide = args.toWideString();
  ShellExecuteW(nullptr, L"open", L"explorer.exe", wide.c_str(), nullptr,
                SW_SHOWNORMAL);
#endif
}
FileInputStream::FileInputStream(const File &f)
    : stream_(f.path(), std::ios::binary), size_(f.getSize()) {
  if (!stream_)
    result_ = Result::fail("Cannot open " + f.getFullPathName());
}
int FileInputStream::read(void *p, int n) {
  if (n <= 0)
    return 0;
  stream_.read(static_cast<char *>(p), n);
  return int(stream_.gcount());
}
int64 FileInputStream::getPosition() {
  auto p = stream_.tellg();
  return p < 0 ? size_ : int64(p);
}
bool FileInputStream::setPosition(int64 n) {
  if (n < 0)
    return false;
  stream_.clear();
  stream_.seekg(n);
  return bool(stream_);
}
FileOutputStream::FileOutputStream(const File &file, std::size_t)
    : file_(file) {
  if (!file.exists())
    file.create();
  stream_.open(file.path(), std::ios::binary | std::ios::in | std::ios::out);
  if (!stream_)
    result_ = Result::fail("Cannot write " + file.getFullPathName());
  else
    stream_.seekp(0, std::ios::end);
}
bool FileOutputStream::write(const void *p, std::size_t n) {
  if (!stream_.write(static_cast<const char *>(p), std::streamsize(n))) {
    result_ = Result::fail("File write failed");
    return false;
  }
  return true;
}
int64 FileOutputStream::getPosition() {
  auto n = stream_.tellp();
  return n < 0 ? 0 : int64(n);
}
bool FileOutputStream::setPosition(int64 n) {
  if (n < 0)
    return false;
  stream_.clear();
  stream_.seekp(n);
  return bool(stream_);
}
void FileOutputStream::flush() {
  stream_.flush();
  if (!stream_)
    result_ = Result::fail("File flush failed");
}
Result FileOutputStream::truncate() {
  auto position = getPosition();
  flush();
  std::error_code e;
  std::filesystem::resize_file(file_.path(), uint64(position), e);
  return e ? Result::fail(e.message()) : Result::ok();
}
bool OutputStream::writeText(const Text &text, bool unicode, bool header,
                             const char *newlines) {
  auto s = newlines ? text.replace("\r\n", "\n").replace("\n", newlines) : text;
  if (unicode) {
    if (header && !writeShort(short(0xfeff)))
      return false;
    for (int i = 0; i < s.length(); ++i) {
      auto c = s[i];
      if (c > 0xffff) {
        c -= 0x10000;
        if (!writeShort(short(0xd800 + (c >> 10))) ||
            !writeShort(short(0xdc00 + (c & 1023))))
          return false;
      } else if (!writeShort(short(c)))
        return false;
    }
    return true;
  }
  if (header) {
    const uint8 bom[] = {0xef, 0xbb, 0xbf};
    if (!write(bom, 3))
      return false;
  }
  return write(s.toRawUTF8(), s.getNumBytesAsUTF8());
}
Text MemoryBlock::toBase64Encoding() const {
  static constexpr char code[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((bytes_.size() + 2) / 3 * 4);
  for (std::size_t i = 0; i < bytes_.size(); i += 3) {
    uint32 n = uint32(bytes_[i]) << 16;
    if (i + 1 < bytes_.size())
      n |= uint32(bytes_[i + 1]) << 8;
    if (i + 2 < bytes_.size())
      n |= bytes_[i + 2];
    out += code[n >> 18];
    out += code[(n >> 12) & 63];
    out += i + 1 < bytes_.size() ? code[(n >> 6) & 63] : '=';
    out += i + 2 < bytes_.size() ? code[n & 63] : '=';
  }
  return out;
}
bool MemoryBlock::fromBase64Encoding(const Text &text) {
  auto code = [](char c) {
    if (c >= 'A' && c <= 'Z')
      return c - 'A';
    if (c >= 'a' && c <= 'z')
      return c - 'a' + 26;
    if (c >= '0' && c <= '9')
      return c - '0' + 52;
    if (c == '+')
      return 62;
    if (c == '/')
      return 63;
    return -1;
  };
  auto s = text.toStdString();
  std::erase_if(s, [](char c) {
    return c == ' ' || c == '\r' || c == '\n' || c == '\t';
  });
  if (s.size() % 4)
    return false;
  MemoryBlock result;
  for (std::size_t i = 0; i < s.size(); i += 4) {
    int a = code(s[i]), b = code(s[i + 1]),
        c = s[i + 2] == '=' ? 0 : code(s[i + 2]),
        d = s[i + 3] == '=' ? 0 : code(s[i + 3]);
    if (a < 0 || b < 0 || c < 0 || d < 0 ||
        (s[i + 2] == '=' && s[i + 3] != '=') ||
        ((s[i + 2] == '=' || s[i + 3] == '=') && i + 4 < s.size()))
      return false;
    uint32 n = uint32(a << 18 | b << 12 | c << 6 | d);
    uint8 bytes[] = {uint8(n >> 16), uint8(n >> 8), uint8(n)};
    result.append(bytes, s[i + 2] == '=' ? 1 : s[i + 3] == '=' ? 2 : 3);
  }
  swapWith(result);
  return true;
}
Text Time::formatted(const Text &pattern) const {
  auto seconds = std::time_t(milliseconds_ / 1000);
  std::tm local{};
#if defined(_WIN32)
  localtime_s(&local, &seconds);
#else
  localtime_r(&seconds, &local);
#endif
  std::ostringstream out;
  out << std::put_time(&local, pattern.toRawUTF8());
  return out.str();
}
Text Time::toISO8601(bool) const {
  auto seconds = std::time_t(milliseconds_ / 1000);
  std::tm utc{};
#if defined(_WIN32)
  gmtime_s(&utc, &seconds);
#else
  gmtime_r(&seconds, &utc);
#endif
  std::ostringstream out;
  out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3)
      << std::setfill('0') << milliseconds_ % 1000 << 'Z';
  return out.str();
}
Time Time::fromISO8601(const Text &text) {
  std::tm utc{};
  std::istringstream in(text.toStdString());
  in >> std::get_time(&utc, "%Y-%m-%dT%H:%M:%S");
  if (in.fail())
    return {};
  int ms = 0;
  if (in.peek() == '.') {
    in.get();
    std::string digits;
    while (std::isdigit(in.peek()))
      digits += char(in.get());
    while (digits.size() < 3)
      digits += '0';
    ms = std::stoi(digits.substr(0, 3));
  }
#if defined(_WIN32)
  return Time(int64(_mkgmtime(&utc)) * 1000 + ms);
#else
  return Time(int64(timegm(&utc)) * 1000 + ms);
#endif
}
} // namespace cydaw::base

namespace cydaw::base {
int File::getVolumeSerialNumber() const {
#if defined(_WIN32)
  wchar_t volume[MAX_PATH + 1]{};
  DWORD serial = 0;
  if (!GetVolumePathNameW(path_.c_str(), volume, MAX_PATH) ||
      !GetVolumeInformationW(volume, nullptr, 0, &serial, nullptr, nullptr,
                             nullptr, 0))
    return 0;
  return int(serial);
#else
  struct stat info{};
  return stat(path_.c_str(), &info) == 0 ? int(info.st_dev) : 0;
#endif
}
bool File::hasIdenticalContentTo(const File &other) const {
  if (getSize() != other.getSize())
    return false;
  FileInputStream a(*this), b(other);
  if (!a.openedOk() || !b.openedOk())
    return false;
  std::array<char, 32768> x{}, y{};
  for (;;) {
    int n = a.read(x.data(), int(x.size())),
        m = b.read(y.data(), int(y.size()));
    if (n != m)
      return false;
    if (!n)
      return true;
    if (std::memcmp(x.data(), y.data(), std::size_t(n)))
      return false;
  }
}
bool File::moveToTrash() const {
#if defined(_WIN32)
  if (path_.empty() || !exists())
    return false;
  auto from = path_.wstring();
  from += L'\0';
  from += L'\0';
  SHFILEOPSTRUCTW op{};
  op.wFunc = FO_DELETE;
  op.pFrom = from.c_str();
  op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
  return SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted;
#else
  return false; // A platform without a trash service must preserve the file.
#endif
}
} // namespace cydaw::base

namespace cydaw::base {
bool File::setLastModificationTime(Time time) const {
#if defined(_WIN32)
  HANDLE file =
      CreateFileW(path_.c_str(), FILE_WRITE_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return false;
  ULARGE_INTEGER ticks;
  ticks.QuadPart =
      uint64(time.toMilliseconds()) * 10000 + 116444736000000000ull;
  FILETIME stamp{ticks.LowPart, ticks.HighPart};
  bool ok = SetFileTime(file, nullptr, nullptr, &stamp) != 0;
  CloseHandle(file);
  return ok;
#else
  std::error_code e;
  auto system = std::chrono::system_clock::time_point(
      std::chrono::milliseconds(time.toMilliseconds()));
  std::filesystem::last_write_time(
      path_, std::chrono::file_clock::from_sys(system), e);
  return !e;
#endif
}
} // namespace cydaw::base
