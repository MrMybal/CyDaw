#include <cydaw/base/Platform.h>
#ifdef _WIN32
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <shellapi.h>
// clang-format on
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif
namespace cydaw::base {
TextList commandLineArguments(int argc,char** argv) {
 TextList out;
#ifdef _WIN32
 int count=0;auto** wide=CommandLineToArgvW(GetCommandLineW(),&count);if(wide){for(int i=0;i<count;++i)out.add(Text(wide[i]));LocalFree(wide);}
#else
 for(int i=0;i<argc;++i)out.add(Text::fromUTF8(argv[i]));
#endif
 return out;
}

struct InterProcessLock::Impl {
#ifdef _WIN32
  HANDLE handle{};
#else
  int handle = -1;
#endif
  bool held = false;
};
InterProcessLock::InterProcessLock(const Text &name)
    : impl_(std::make_unique<Impl>()) {
#ifdef _WIN32
  impl_->handle =
      CreateMutexW(nullptr, FALSE, ("Local\\" + name).toWideCharPointer());
#else
  auto path = File::getSpecialLocation(File::tempDirectory)
                  .getChildFile(name.replaceCharacter('/', '_') + ".lock");
  impl_->handle = ::open(path.getFullPathName().toRawUTF8(),
                         O_CREAT | O_RDWR | O_CLOEXEC, 0600);
#endif
}
InterProcessLock::~InterProcessLock() {
  exit();
#ifdef _WIN32
  if (impl_->handle)
    CloseHandle(impl_->handle);
#else
  if (impl_->handle >= 0)
    ::close(impl_->handle);
#endif
}
bool InterProcessLock::enter(int milliseconds) {
  if (impl_->held)
    return true;
#ifdef _WIN32
  if (!impl_->handle)
    return false;
  auto r = WaitForSingleObject(
      impl_->handle, milliseconds < 0 ? INFINITE : DWORD(milliseconds));
  return impl_->held = (r == WAIT_OBJECT_0 || r == WAIT_ABANDONED);
#else
  auto end = std::chrono::steady_clock::now() +
             std::chrono::milliseconds(std::max(0, milliseconds));
  do {
    if (impl_->handle >= 0 && flock(impl_->handle, LOCK_EX | LOCK_NB) == 0)
      return impl_->held = true;
    if (milliseconds == 0)
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  } while (milliseconds < 0 || std::chrono::steady_clock::now() < end);
  return false;
#endif
}
void InterProcessLock::exit() {
  if (!impl_->held)
    return;
#ifdef _WIN32
  ReleaseMutex(impl_->handle);
#else
  flock(impl_->handle, LOCK_UN);
#endif
  impl_->held = false;
}
void SystemClipboard::copyTextToClipboard(const Text &text) {
#ifdef _WIN32
  if (!OpenClipboard(nullptr))
    return;
  auto wide = text.toWideString();
  auto bytes = (wide.size() + 1) * sizeof(wchar_t);
  auto memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (memory) {
    if (auto *target = GlobalLock(memory)) {
      memcpy(target, wide.c_str(), bytes);
      GlobalUnlock(memory);
      if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory))
        memory = nullptr;
    }
    if (memory)
      GlobalFree(memory);
  }
  CloseClipboard();
#endif
}
} // namespace cydaw::base
