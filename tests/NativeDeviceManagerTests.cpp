#include <cydaw/integration/CyAudioCoreDeviceManager.h>
#include <iostream>
#include <stdexcept>
using namespace cydaw;
void require(bool v, const char *reason) {
  if (!v)
    throw std::runtime_error(reason);
}
struct ManualDevice final : audio::Device {
  audio::DeviceCallback *callback{};
  audio::ChannelSet in, out;
  double rate{};
  int frames{};
  bool opened{};
  ManualDevice() : Device("Manual", "Test") {}
  base::TextList getInputChannelNames() override { return {"1", "2"}; }
  base::TextList getOutputChannelNames() override { return {"1", "2"}; }
  base::Array<double> getAvailableSampleRates() override { return {48000}; }
  base::Array<int> getAvailableBufferSizes() override { return {128}; }
  int getDefaultBufferSize() override { return 128; }
  base::Text open(const audio::ChannelSet &i, const audio::ChannelSet &o,
                  double r, int f) override {
    in = i;
    out = o;
    rate = r;
    frames = f;
    opened = true;
    return {};
  }
  void start(audio::DeviceCallback *cb) override {
    callback = cb;
    cb->audioDeviceAboutToStart(this);
  }
  void stop() override {
    if (auto *c = std::exchange(callback, nullptr))
      c->audioDeviceStopped();
  }
  void close() override {
    stop();
    opened = false;
  }
  bool isOpen() override { return opened; }
  bool isPlaying() override { return callback != nullptr; }
  base::Text getLastError() override { return {}; }
  int getCurrentBufferSizeSamples() override { return frames; }
  double getCurrentSampleRate() override { return rate; }
  int getCurrentBitDepth() override { return 32; }
  audio::ChannelSet getActiveInputChannels() const override { return in; }
  audio::ChannelSet getActiveOutputChannels() const override { return out; }
  int getInputLatencyInSamples() override { return 0; }
  int getOutputLatencyInSamples() override { return 0; }
  void pump(float *const *out, int count) {
    callback->audioDeviceIOCallbackWithContext(nullptr, 0, out, 2, count, {});
  }
};
struct ManualType final : audio::DeviceType {
  ManualType() : DeviceType("Test") {}
  void scanForDevices() override {}
  base::TextList getDeviceNames(bool = false) const override {
    return {"Manual"};
  }
  int getDefaultDeviceIndex(bool) const override { return 0; }
  int getIndexOfDevice(audio::Device *, bool) const override { return 0; }
  bool hasSeparateInputsAndOutputs() const override { return true; }
  audio::Device *createDevice(const base::Text &, const base::Text &) override {
    return new ManualDevice;
  }
};
struct Client final : audio::DeviceCallback {
  int id, starts{}, stops{};
  float value;
  std::vector<int> &order;
  Client(int i, float v, std::vector<int> &o) : id(i), value(v), order(o) {}
  void audioDeviceAboutToStart(audio::Device *) override { ++starts; }
  void audioDeviceStopped() override { ++stops; }
  void audioDeviceIOCallbackWithContext(
      const float *const *, int, float *const *out, int channels, int frames,
      const audio::DeviceCallbackContext &) override {
    order.push_back(id);
    for (int ch = 0; ch < channels; ++ch)
      std::fill_n(out[ch], frames, value);
  }
};
int main(int argc, char **argv) {
  try {
    if (argc > 1) {
      integration::DeviceManager native;
      const auto error = native.initialise(2, 2, nullptr, true);
      std::cout << "initialise: " << error << "\n";
      std::vector<int> order;
      order.reserve(10000);
      Client silent(1, 0, order);
      native.addAudioCallback(&silent);
      base::Thread::sleep(100);
      if (auto *d = native.getCurrentAudioDevice()) {
        std::cout << "opened " << d->getName() << " rate "
                  << d->getCurrentSampleRate() << " frames "
                  << d->getCurrentBufferSizeSamples() << " playing "
                  << d->isPlaying() << "\n";
        if (auto desc = d->streamDescription())
          std::cout << "native periods " << desc->input.periodFrames << " / "
                    << desc->output.periodFrames << "\n";
      }
      for (auto *type : native.getAvailableDeviceTypes()) {
        type->scanForDevices();
        for (bool input : {false, true})
          for (auto name : type->getDeviceNames(input))
            if (base::JSON::toString(base::Value(name)).isEmpty()) {
              std::cout << "Invalid UTF-8 device label in "
                        << type->getTypeName() << ": ";
              for (unsigned char c : name.toStdString())
                std::cout << std::hex << int(c) << " ";
              std::cout << std::dec << "\n";
            }
      }
      native.removeAudioCallback(&silent);
      std::cout << "callbacks " << order.size() << "\n";
      native.closeAudioDevice();
      return error.isEmpty() && order.size() ? 0 : 1;
    }

    std::vector<std::unique_ptr<audio::DeviceType>> types;
    types.push_back(std::make_unique<ManualType>());
    integration::DeviceManager manager(std::move(types));
    std::vector<int> order;
    order.reserve(10);
    Client source(1, .25f, order), capture(2, 0, order), aux(3, .5f, order);
    manager.addAudioCallback(&source);
    manager.addAudioCallback(&capture);
    manager.addAudioCallback(&aux);
    manager.addAudioCallback(&aux);
    require(manager.initialise(0, 2, nullptr, false).isEmpty(),
            "manager starts injected native device");
    auto *device =
        dynamic_cast<ManualDevice *>(manager.getCurrentAudioDevice());
    require(device, "native instance exposed");
    float left[128]{}, right[128]{};
    float *output[]{left, right};
    device->pump(output, 128);
    require(order == std::vector<int>({3, 2, 1}),
            "input capture runs before source rendering");
    require(left[0] == .75f && right[127] == .75f,
            "callbacks are summed independently; silent capture does not erase "
            "source");
    require(source.starts == 1 && aux.starts == 1,
            "duplicate registration is harmless");
    manager.removeAudioCallback(&capture);
    require(capture.stops == 1, "removal stops only that client");
    auto saved = manager.createStateXml();
    require(saved->getStringAttribute("audioDeviceOutChans") == "11",
            "active channel selection persisted");
    manager.closeAudioDevice();
    require(source.stops == 1 && aux.stops == 1,
            "closing stops each prepared callback");
    require(manager.initialise(0, 2, saved.get(), false).isEmpty(),
            "native XML configuration restores");
    auto setup = manager.getAudioDeviceSetup();
    setup.useDefaultOutputChannels = false;
    setup.outputChannels.setBit(80);
    require(manager.setAudioDeviceSetup(setup, false).isNotEmpty(),
            "invalid hardware selection fails explicitly");
    manager.removeAudioCallback(&source);
    manager.removeAudioCallback(&aux);
    std::cout << "Native manager mixing, ordering, lifecycle and settings "
                 "passed without JUCE\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
