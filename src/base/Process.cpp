#include <chrono>
#include <cydaw/base/Process.h>
#include <cydaw/base/Thread.h>
#include <limits>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
namespace cydaw::base {
struct ChildProcess::Native {
#if defined(_WIN32)
  HANDLE process{}, output{};
  ~Native() {
    if (output)
      CloseHandle(output);
    if (process)
      CloseHandle(process);
  }
#else
  pid_t process = -1;
  int output = -1;
  mutable int exit{};
  mutable bool finished = false;
  ~Native() {
    if (output >= 0)
      ::close(output);
    if (process > 0 && !finished) {
      int status;
      ::waitpid(process, &status, WNOHANG);
    }
  }
#endif
};
ChildProcess::ChildProcess() : native_(std::make_unique<Native>()) {}
ChildProcess::~ChildProcess() = default;
bool ChildProcess::start(const Text &command, int flags) {
  auto arguments = TextList::fromTokens(command, true);
  for (auto &argument : arguments)
    argument = argument.unquoted();
  return start(arguments, flags);
}
bool ChildProcess::start(const TextList &arguments, int flags) {
  if (arguments.isEmpty() || arguments[0].isEmpty() || isRunning())
    return false;
  native_ = std::make_unique<Native>();
#if defined(_WIN32)
  std::wstring command;
  // Inverse of CommandLineToArgvW/CRT parsing: preserve quotes and backslashes,
  // including an empty argument and a trailing slash in a quoted path.
  for (const auto &argument : arguments) {
    if (!command.empty())
      command += L' ';
    command += L'"';
    unsigned slashes = 0;
    for (auto c : argument.toWideString()) {
      if (c == L'\\') {
        ++slashes;
        continue;
      }
      command.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
      slashes = 0;
      command += c;
    }
    command.append(slashes * 2, L'\\');
    command += L'"';
  }
  HANDLE reader{}, writer{};
  SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
  if (flags) {
    if (!CreatePipe(&reader, &writer, &security, 0))
      return false;
    SetHandleInformation(reader, HANDLE_FLAG_INHERIT, 0);
  }
  HANDLE null = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  STARTUPINFOEXW startup{};
  startup.StartupInfo.cb = sizeof(startup);
  startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup.StartupInfo.hStdInput = null;
  startup.StartupInfo.hStdOutput = (flags & wantStdOut) ? writer : null;
  startup.StartupInfo.hStdError = (flags & wantStdErr) ? writer : null;
  SIZE_T attributeSize{};
  InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeSize);
  std::vector<std::uint8_t> attributes(attributeSize);
  startup.lpAttributeList =
      reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
  HANDLE inherited[] = {null, writer};
  const SIZE_T count = writer ? 2 : 1;
  bool attributesReady =
      InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0,
                                        &attributeSize) != FALSE;
  const bool handlesReady =
      attributesReady &&
      UpdateProcThreadAttribute(
          startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
          inherited, count * sizeof(HANDLE), nullptr, nullptr) != FALSE;
  PROCESS_INFORMATION process{};
  const bool okay =
      handlesReady &&
      CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
                     CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr,
                     nullptr, &startup.StartupInfo, &process) != FALSE;
  if (attributesReady)
    DeleteProcThreadAttributeList(startup.lpAttributeList);
  if (writer)
    CloseHandle(writer);
  if (null != INVALID_HANDLE_VALUE)
    CloseHandle(null);
  if (!okay) {
    if (reader)
      CloseHandle(reader);
    return false;
  }
  CloseHandle(process.hThread);
  native_->process = process.hProcess;
  native_->output = reader;
  return true;
#else
  int descriptors[2] = {-1, -1};
  if (flags && ::pipe(descriptors) != 0)
    return false;
  std::vector<std::string> strings;
  for (const auto &argument : arguments)
    strings.push_back(argument.toStdString());
  std::vector<char *> argv;
  for (auto &argument : strings)
    argv.push_back(argument.data());
  argv.push_back(nullptr);
  const auto child = ::fork();
  if (child == 0) {
    int null = ::open("/dev/null", O_RDWR);
    ::dup2(null, STDIN_FILENO);
    ::dup2((flags & wantStdOut) ? descriptors[1] : null, STDOUT_FILENO);
    ::dup2((flags & wantStdErr) ? descriptors[1] : null, STDERR_FILENO);
    if (null > 2)
      ::close(null);
    if (flags) {
      ::close(descriptors[0]);
      ::close(descriptors[1]);
    }
    ::execvp(argv[0], argv.data());
    ::_exit(127);
  }
  if (flags)
    ::close(descriptors[1]);
  if (child < 0) {
    if (flags)
      ::close(descriptors[0]);
    return false;
  }
  native_->process = child;
  native_->output = descriptors[0];
  if (flags)
    ::fcntl(native_->output, F_SETFL,
            ::fcntl(native_->output, F_GETFL) | O_NONBLOCK);
  return true;
#endif
}
bool ChildProcess::isRunning() const {
#if defined(_WIN32)
  return native_->process &&
         WaitForSingleObject(native_->process, 0) == WAIT_TIMEOUT;
#else
  if (native_->process <= 0 || native_->finished)
    return false;
  int status{};
  const auto result = ::waitpid(native_->process, &status, WNOHANG);
  if (result == 0)
    return true;
  if (result == native_->process) {
    native_->finished = true;
    native_->exit =
        WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
  }
  return false;
#endif
}
bool ChildProcess::waitForProcessToFinish(int timeoutMs) {
  const auto end = std::chrono::steady_clock::now() +
                   std::chrono::milliseconds(std::max(0, timeoutMs));
  while (isRunning()) {
    if (timeoutMs >= 0 && std::chrono::steady_clock::now() >= end)
      return false;
    Thread::sleep(2);
  }
  return true;
}
bool ChildProcess::kill() {
#if defined(_WIN32)
  return !isRunning() || TerminateProcess(native_->process, 1) != FALSE;
#else
  return !isRunning() || ::kill(native_->process, SIGKILL) == 0;
#endif
}
int ChildProcess::readProcessOutput(void *buffer, int maximum) {
  if (!buffer || maximum <= 0)
    return 0;
#if defined(_WIN32)
  if (!native_->output)
    return 0;
  DWORD available{}, read{};
  if (!PeekNamedPipe(native_->output, nullptr, 0, nullptr, &available,
                     nullptr) ||
      !available)
    return 0;
  if (!ReadFile(native_->output, buffer,
                std::min<DWORD>(available, DWORD(maximum)), &read, nullptr))
    return 0;
  return int(read);
#else
  if (native_->output < 0)
    return 0;
  const auto count = ::read(native_->output, buffer, std::size_t(maximum));
  return count > 0 ? int(count) : 0;
#endif
}
Text ChildProcess::readAllProcessOutput() {
  std::string output;
  std::array<char, 8192> chunk;
  for (;;) {
    int count = readProcessOutput(chunk.data(), int(chunk.size()));
    if (count > 0) {
      output.append(chunk.data(), count);
      continue;
    }
    if (!isRunning())
      break;
    Thread::sleep(2);
  }
  return Text::fromUTF8(output.data(), int(output.size()));
}
unsigned ChildProcess::getExitCode() const {
#if defined(_WIN32)
  DWORD code{};
  if (native_->process)
    GetExitCodeProcess(native_->process, &code);
  return unsigned(code);
#else
  (void)isRunning();
  return unsigned(native_->exit);
#endif
}
struct MemoryMappedFile::Native {
  void *data{};
  std::size_t size{};
#if defined(_WIN32)
  HANDLE file = INVALID_HANDLE_VALUE, mapping{};
  ~Native() {
    if (data)
      UnmapViewOfFile(data);
    if (mapping)
      CloseHandle(mapping);
    if (file != INVALID_HANDLE_VALUE)
      CloseHandle(file);
  }
#else
  int file = -1;
  ~Native() {
    if (data)
      ::munmap(data, size);
    if (file >= 0)
      ::close(file);
  }
#endif
};
MemoryMappedFile::MemoryMappedFile(const File &file, AccessMode mode,
                                   bool exclusive)
    : native_(std::make_unique<Native>()) {
  auto &n = *native_;
  const auto size = file.getSize();
  if (size <= 0 ||
      std::uint64_t(size) > std::numeric_limits<std::size_t>::max())
    return;
#if defined(_WIN32)
  n.file = CreateFileW(
      file.getFullPathName().toWideString().c_str(),
      mode == readWrite ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ,
      exclusive ? 0 : FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (n.file == INVALID_HANDLE_VALUE)
    return;
  n.mapping = CreateFileMappingW(
      n.file, nullptr, mode == readWrite ? PAGE_READWRITE : PAGE_READONLY, 0, 0,
      nullptr);
  if (!n.mapping)
    return;
  n.data = MapViewOfFile(n.mapping,
                         mode == readWrite ? FILE_MAP_WRITE | FILE_MAP_READ
                                           : FILE_MAP_READ,
                         0, 0, 0);
#else
  (void)exclusive;
  n.file = ::open(file.getFullPathName().toRawUTF8(),
                  mode == readWrite ? O_RDWR : O_RDONLY);
  if (n.file < 0)
    return;
  n.data = ::mmap(nullptr, std::size_t(size),
                  mode == readWrite ? PROT_READ | PROT_WRITE : PROT_READ,
                  MAP_SHARED, n.file, 0);
  if (n.data == MAP_FAILED)
    n.data = nullptr;
#endif
  if (n.data)
    n.size = std::size_t(size);
}
MemoryMappedFile::~MemoryMappedFile() = default;
void *MemoryMappedFile::getData() const noexcept { return native_->data; }
std::size_t MemoryMappedFile::getSize() const noexcept { return native_->size; }
DynamicLibrary::~DynamicLibrary() { close(); }
void DynamicLibrary::close() {
  if (!handle_)
    return;
#if defined(_WIN32)
  FreeLibrary(static_cast<HMODULE>(handle_));
#else
  ::dlclose(handle_);
#endif
  handle_ = nullptr;
}
bool DynamicLibrary::open(const Text &path) {
  close();
#if defined(_WIN32)
  handle_ = LoadLibraryW(path.toWideString().c_str());
#else
  handle_ = ::dlopen(path.toRawUTF8(), RTLD_NOW | RTLD_LOCAL);
#endif
  return handle_ != nullptr;
}
void *DynamicLibrary::getFunction(const Text &symbol) const {
  if (!handle_)
    return nullptr;
#if defined(_WIN32)
  return reinterpret_cast<void *>(
      GetProcAddress(static_cast<HMODULE>(handle_), symbol.toRawUTF8()));
#else
  return ::dlsym(handle_, symbol.toRawUTF8());
#endif
}
} // namespace cydaw::base
