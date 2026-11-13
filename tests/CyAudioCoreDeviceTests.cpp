#include <atomic>
#include <chrono>
#include <cmath>
#include <cydaw/integration/CyAudioCoreDeviceManager.h>
#include <iostream>
#include <juce_gui_basics/juce_gui_basics.h>
#include <stdexcept>
#include <thread>
namespace {
void require(bool b, const char *text) {
  if (!b)
    throw std::runtime_error(text);
}
struct Renderer : juce::AudioIODeviceCallback {
  std::atomic<unsigned> calls{0}, errors{0};
  unsigned starts = 0, stops = 0;
  double phase = 0;
  void audioDeviceAboutToStart(juce::AudioIODevice *d) override {
    ++starts;
    if (d->getCurrentSampleRate() != 48000 ||
        d->getCurrentBufferSizeSamples() != 64)
      ++errors;
  }
  void audioDeviceStopped() override { ++stops; }
  void audioDeviceIOCallbackWithContext(
      const float *const *, int, float *const *out, int channels, int frames,
      const juce::AudioIODeviceCallbackContext &clock) override {
    if (frames <= 0 || frames > 64 || channels != 2 || !clock.hostTimeNs ||
        !*clock.hostTimeNs)
      ++errors;
    for (int n = 0; n < frames; ++n) {
      const auto value = float(std::sin(phase) * .2);
      phase += .057;
      for (int ch = 0; ch < channels; ++ch)
        out[ch][n] = value;
    }
    ++calls;
  }
};
} // namespace
int main() {
  try {
    juce::ScopedJuceInitialiser_GUI gui;
    auto type = cydaw::integration::createCyAudioCoreDeviceType(
        "CyAudioCore virtual", cyaudio::AudioBackend::nullDevice);
    type->scanForDevices();
    const auto outputs = type->getDeviceNames(false);
    require(!outputs.isEmpty(), "virtual output enumeration");
    auto device = std::unique_ptr<juce::AudioIODevice>(
        type->createDevice(outputs[0], {}));
    require(bool(device), "virtual device creation");
    juce::BigInteger inputs, mask;
    mask.setRange(0, 2, true);
    require(device->open(inputs, mask, 48000, 64).isEmpty(),
            "Core virtual stream preparation");
    require(type->getIndexOfDevice(device.get(), false) == 0,
            "endpoint index by stable session ID");
    Renderer renderer;
    for (unsigned run = 0; run < 2; ++run) {
      const auto before = renderer.calls.load();
      device->start(&renderer);
      for (unsigned wait = 0; wait < 200 && renderer.calls < before + 4; ++wait)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      require(device->isPlaying() && renderer.calls >= before + 4,
              "Core real callback thread");
      device->stop();
      const auto stopped = renderer.calls.load();
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      require(renderer.calls == stopped, "stop must join callbacks");
    }
    require(renderer.starts == 2 && renderer.stops == 2 && renderer.errors == 0,
            "callback format and lifecycle");
    device->close();
    require(!device->isOpen(), "stream close");
    mask.setBit(999);
    require(device->open(inputs, mask, 48000, 64).isNotEmpty(),
            "unsupported channel rejected");
    std::cout << "CyDaw device adapter / CyAudioCore native callback passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
