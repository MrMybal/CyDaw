#include <cydaw/base/Process.h>
#include <cydaw/base/Thread.h>
#include <iostream>
#include <stdexcept>
using namespace cydaw::base;
void require(bool okay, const char *message) {
  if (!okay)
    throw std::runtime_error(message);
}
int main(int argc, char **argv) {
  if (argc > 1 && std::string_view(argv[1]) == "--sleep") {
    Thread::sleep(10000);
    return 0;
  }
  if (argc > 1 && std::string_view(argv[1]) == "--child") {
    for (int i = 2; i < argc; ++i)
      std::cout << argv[i] << '\n';
    std::cout.flush();
    return 23;
  }
  try {
    const auto executable =
        File::getSpecialLocation(File::currentExecutableFile).getFullPathName();
    ChildProcess child;
    require(child.start(TextList{executable, "--child", "two words", "a\"b",
                                 "C:\\a directory\\", "", "$(never executed)"},
                        ChildProcess::wantStdOut),
            "launch native child");
    auto output = child.readAllProcessOutput();
    require(child.waitForProcessToFinish(2000) && child.getExitCode() == 23,
            "native child exit status");
    require(output.replace("\r\n", "\n") ==
                "two words\na\"b\nC:\\a directory\\\n\n$(never executed)\n",
            "native argv quoting preserves spaces, quotes, slashes, empty "
            "arguments and literal shell text");
    ChildProcess sleeper;
    require(sleeper.start(TextList{executable, "--sleep"}, 0),
            "launch wait fixture");
    require(!sleeper.waitForProcessToFinish(5), "native process timeout");
    require(sleeper.kill() && sleeper.waitForProcessToFinish(2000),
            "native process cancellation");
    auto file = File::createTempFile(".ipc");
    require(file.replaceWithText("shared-buffer"), "shared memory fixture");
    {
      MemoryMappedFile writer(file, MemoryMappedFile::readWrite);
      MemoryMappedFile reader(file, MemoryMappedFile::readOnly);
      require(writer.getData() && reader.getData() && reader.getSize() == 13,
              "native memory mapping");
      static_cast<char *>(writer.getData())[0] = 'S';
      require(static_cast<const char *>(reader.getData())[0] == 'S',
              "shared mapping publishes bytes between views");
    }
    require(file.deleteFile(), "native mappings release handles");
#if defined(_WIN32)
    DynamicLibrary library;
    require(library.open("kernel32.dll") &&
                library.getFunction("GetCurrentProcessId"),
            "native symbol lookup");
    library.close();
    require(!library.getFunction("GetCurrentProcessId"),
            "native library close");
#endif
    std::cout << "Native child launch, argv, exit, timeout, shared mapping and "
                 "library tests passed without JUCE\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
