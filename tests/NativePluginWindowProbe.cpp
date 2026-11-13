#include <cydaw/base/Platform.h>
#include "../src/plugin_worker/NativeHostedPlugin.h"
#include <cmath>
#include <cydaw/base/EventLoop.h>
#include <cydaw/base/Thread.h>
#include <iostream>
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <objbase.h>
// clang-format on
int main(int argc, char **argv) {
  auto arguments=cydaw::base::commandLineArguments(argc,argv);
  if (arguments.size() < 6)
    return 2;
  // Isolate automation windows before COM or a plugin can create an HWND.
  auto desktop = CreateDesktopW(L"CyDAW-automation", nullptr, nullptr, 0,
                                GENERIC_ALL, nullptr);
  if (!desktop || !SetThreadDesktop(desktop))
    return 3;
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  int result = 0;
  try {
    using namespace cydaw;
    plugins::NativePluginFormat format(cyaudio::PluginFormat::vst3, "VST3");
    std::vector<std::unique_ptr<plugins::HostedMetadata>> list;
    format.findAllTypesForFile(list, arguments[1]);
    if (list.empty())
      throw std::runtime_error(format.lastError.toStdString());
    base::Text error;
    auto plugin =
        format.createInstanceFromDescription(*list.front(), 48000, 256, error);
    if (!plugin)
      throw std::runtime_error(error.toStdString());
    base::MemoryBlock state;
    if (!base::File(arguments[2]).loadFileAsData(state))
      throw std::runtime_error("missing graph state");
    plugin->setStateInformation(state.getData(), int(state.getSize()));
    plugin->setOffline(true);
    plugin->prepareToPlay(48000, 256);
    auto editor = plugin->createEditor();
    if (!editor)
      throw std::runtime_error("missing editor");
    editor->setVisible(true);
    audio::SampleBuffer<float> buffer(2, 256);
    midi::EventBuffer midi;
    auto frame = base::File(arguments[5]);
    auto start = std::chrono::steady_clock::now();
    unsigned blocks = 0;
    while (std::chrono::steady_clock::now() - start <
           std::chrono::seconds(45)) {
      MSG message;
      while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
      }
      base::EventLoop::dispatchPending();
      plugin->pump();
      midi.clear();
      if (blocks == 0)
        midi.addEvent(midi::Message::noteOn(1, 60, 0.8f), 0);
      for (int i = 0; i < 256; ++i)
        for (int c = 0; c < 2; ++c)
          buffer.setSample(
              c, i,
              float(0.1 * std::sin((blocks * 256 + i) * 6.283185307179586 *
                                   220 / 48000)));
      plugin->processBlock(buffer, midi);
      ++blocks;
      if (frame.existsAsFile() && blocks > 40)
        break;
      base::Thread::sleep(5);
    }
    editor.reset();
    plugin->releaseResources();
    if (!frame.existsAsFile())
      throw std::runtime_error("plugin did not capture its window");
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    result = 1;
  }
  CoUninitialize();
  CloseDesktop(desktop);
  return result;
}
