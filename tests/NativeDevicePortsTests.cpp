#include <cydaw/audio/Devices.h>
#include <iostream>
#include <stdexcept>
using namespace cydaw;
void require(bool okay, const char *message) {
  if (!okay)
    throw std::runtime_error(message);
}
struct Silence final : audio::DeviceCallback {
  std::atomic<unsigned> blocks{}, prepared{}, stopped{}, contextual{};
  base::WaitableEvent arrived;
  void audioDeviceAboutToStart(audio::Device *device) override {
    if (device && device->getCurrentSampleRate() > 0)
      ++prepared;
  }
  void audioDeviceStopped() override { ++stopped; }
  void audioDeviceIOCallbackWithContext(
      const float *const *, int, float *const *output, int channels, int frames,
      const audio::DeviceCallbackContext &context) override {
    for (int ch = 0; ch < channels; ++ch)
      if (output[ch])
        std::fill_n(output[ch], frames, 0.f);
    if (context.native && context.native->clockValid && context.hostTimeNs &&
        *context.hostTimeNs == context.native->clock.monotonicNanoseconds)
      ++contextual;
    ++blocks;
    arrived.signal();
  }
};
int main(int argc, char **argv) {
  try {
    audio::ChannelSet channels;
    channels.setBit(0);
    channels.setBit(65);
    channels.setBit(255);
    require(channels.countNumberOfSetBits() == 3 &&
                channels.getHighestBit() == 255 &&
                channels.findNextSetBit(1) == 65,
            "native channel selection spans hardware indices");
    audio::ChannelSet restored;
    restored.parseString(channels.toString(2), 2);
    require(restored == channels, "binary channel selection roundtrip");
    restored.parseString(channels.toString(16), 16);
    require(restored == channels, "hex channel selection roundtrip");
    const bool asio = argc > 1 && std::string_view(argv[1]) == "--asio";
    std::unique_ptr<audio::DeviceType> type;
#if CYDAW_WITH_ASIO
    if (asio)
      type = integration::createNativeAsioDeviceType();
#endif
    if (!type && argc > 1 && std::string_view(argv[1]) == "--wasapi")
      type = integration::createNativeCoreDeviceType(
          "Windows Audio", cyaudio::AudioBackend::wasapi);
    if (!type)
      type = integration::createNativeCoreDeviceType(
          "Null", cyaudio::AudioBackend::nullDevice);
    type->scanForDevices();
    auto names = type->getDeviceNames(false);
    require(!names.isEmpty(), "Core null backend enumerates an output");
    std::unique_ptr<audio::Device> device(type->createDevice(names[0], {}));
    require(bool(device), "native device factory");
    audio::ChannelSet out;
    out.setRange(0, 2, true);
    require(device->open({}, out, 48000, 128).isEmpty(),
            "native Core output opens");
    auto stream = device->streamDescription();
    require(stream && stream->sampleRate == 48000 &&
                stream->output.channels == 2,
            "negotiated stream metadata remains typed");
    Silence silence;
    device->start(&silence);
    require(silence.arrived.wait(2000),
            "Core null stream renders native callbacks");
    device->stop();
    const auto count = silence.blocks.load();
    base::Thread::sleep(10);
    require(
        count > 0 && silence.blocks == count && silence.prepared == 1 &&
            silence.stopped == 1 && silence.contextual > 0,
        "native callback lifecycle, host anchor and stopped-thread contract");
    device->close();
    std::cout << "Backend: " << type->getTypeName() << ", output: " << names[0]
              << "\n";
    std::cout << "Native device factory, channel selection, metadata and "
                 "silent Core callback passed without JUCE\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
