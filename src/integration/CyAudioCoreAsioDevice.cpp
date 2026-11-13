#include <algorithm>
#include <cmath>
#include <cydaw/integration/CyAudioCoreDeviceManager.h>
#include <cydaw/platform/AsioDriver.h>
namespace cydaw::integration {
namespace {
using namespace cyaudio;
juce::String error(Status s) {
  return s == Status::ok
             ? juce::String{}
             : "CyAudioCore / PortAudio ASIO: status " + juce::String(int(s));
}
class AsioDevice final : public juce::AudioIODevice {
public:
  platform::AsioDriver driver;
  AudioDeviceManager manager;
  std::string id;
  platform::AsioDeviceDetails details;
  AudioDeviceConfig config;
  AudioStreamDescription stream;
  juce::BigInteger inputMask, outputMask;
  juce::AudioIODeviceCallback *callback = nullptr;
  juce::String lastError;
  bool opened = false;
  AsioDevice(const juce::String &name, std::string key)
      : juce::AudioIODevice(name, "ASIO"), id(std::move(key)) {
    lastError = error(driver.details(id, details));
    if (lastError.isEmpty())
      lastError = error(manager.prepare(driver.interface()));
  }
  ~AsioDevice() override { close(); }
  static juce::StringArray names(const std::vector<std::string> &list) {
    juce::StringArray out;
    for (const auto &n : list)
      out.add(juce::String::fromUTF8(n.c_str()));
    return out;
  }
  juce::StringArray getOutputChannelNames() override {
    return names(details.outputNames);
  }
  juce::StringArray getInputChannelNames() override {
    return names(details.inputNames);
  }
  juce::Array<double> getAvailableSampleRates() override {
    juce::Array<double> out;
    for (auto rate : details.sampleRates)
      out.add(rate);
    return out;
  }
  juce::Array<int> getAvailableBufferSizes() override {
    juce::Array<int> out;
    for (auto n : details.bufferSizes)
      out.add(n);
    return out;
  }
  int getDefaultBufferSize() override { return details.preferredBufferFrames; }
  juce::String open(const juce::BigInteger &inputs,
                    const juce::BigInteger &outputs, double rate,
                    int frames) override {
    close();
    if (!std::isfinite(rate) || rate <= 0 || rate > 768000 || frames <= 0 ||
        frames > 1048576)
      return lastError = error(Status::invalid_argument);
    config = {};
    inputMask = inputs;
    outputMask = outputs;
    config.inputId = config.outputId = id;
    config.sampleRate = std::uint32_t(std::llround(rate));
    config.periodFrames = unsigned(frames);
    config.maxCallbackFrames = unsigned(frames);
    config.inputChannels = inputs.countNumberOfSetBits();
    config.outputChannels = outputs.countNumberOfSetBits();
    config.access = AudioAccess::exclusive;
    for (int i = inputs.findNextSetBit(0); i >= 0;
         i = inputs.findNextSetBit(i + 1))
      config.activeInputChannels.push_back(unsigned(i));
    for (int i = outputs.findNextSetBit(0); i >= 0;
         i = outputs.findNextSetBit(i + 1))
      config.activeOutputChannels.push_back(unsigned(i));
    // Open without starting: JUCE must receive audioDeviceAboutToStart first.
    auto d = driver.interface();
    const auto r = d.openWithContext(d.context, config, render, this);
    lastError = error(r);
    opened = r == Status::ok;
    if (opened) {
      (void)d.description(d.context, stream);
      d.close(d.context);
    }
    return lastError;
  }
  void close() override {
    stop();
    opened = false;
  }
  bool isOpen() override { return opened; }
  void start(juce::AudioIODeviceCallback *cb) override {
    stop();
    if (!opened || !cb)
      return;
    cb->audioDeviceAboutToStart(this);
    callback = cb;
    AudioReconnectOptions options;
    options.automatic = false;
    options.fallbackToDefault = false;
    const auto r = manager.openWithContext(config, render, this, options);
    if (r != Status::ok) {
      lastError = error(r);
      manager.close();
      callback = nullptr;
      cb->audioDeviceStopped();
      cb->audioDeviceError(lastError);
    } else {
      (void)manager.description(stream);
      lastError.clear();
    }
  }
  void stop() override {
    manager.close();
    auto *cb = std::exchange(callback, nullptr);
    if (cb)
      cb->audioDeviceStopped();
  }
  bool isPlaying() override { return callback && manager.running(); }
  juce::String getLastError() override { return lastError; }
  int getCurrentBufferSizeSamples() override {
    return int(config.periodFrames);
  }
  double getCurrentSampleRate() override {
    return stream.sampleRate ? stream.sampleRate : config.sampleRate;
  }
  int getCurrentBitDepth() override { return 32; } // Float32 client stream.
  juce::BigInteger getActiveOutputChannels() const override {
    return outputMask;
  }
  juce::BigInteger getActiveInputChannels() const override { return inputMask; }
  int latency(const AudioTimingValue &value) const {
    return value.valid
               ? int(std::llround(value.nanoseconds * 1e-9 * stream.sampleRate))
               : 0;
  }
  int getOutputLatencyInSamples() override {
    return latency(stream.output.latency);
  }
  int getInputLatencyInSamples() override {
    return latency(stream.input.latency);
  }
  bool hasControlPanel() const override { return true; }
  bool showControlPanel() override {
    auto *cb = callback;
    const bool wasOpen = opened;
    stop();
    const auto r = driver.showControlPanel(id, nullptr);
    lastError = error(r);
    (void)driver.details(id, details);
    if (wasOpen && cb)
      start(cb);
    return r == Status::ok;
  }
  static Status render(void *ptr, AudioBlock<const float> in,
                       AudioBlock<float> out,
                       const AudioCallbackContext &context) noexcept {
    auto &s = *static_cast<AsioDevice *>(ptr);
    if (!s.callback)
      return Status::ok;
    juce::AudioIODeviceCallbackContext c;
    const auto ns = context.clock.monotonicNanoseconds;
    if (context.clockValid)
      c.hostTimeNs = &ns;
    s.callback->audioDeviceIOCallbackWithContext(
        in.channelData(), int(in.channels()), out.channelData(),
        int(out.channels()), int(out.frames()), c);
    return Status::ok;
  }
};
class AsioType final : public juce::AudioIODeviceType {
  platform::AsioDriver driver;
  std::vector<AudioDeviceInfo> devices;
  juce::StringArray names(bool input) const {
    juce::StringArray out;
    for (const auto &d : devices)
      if (d.input == input) {
        auto n = juce::String::fromUTF8(d.name.c_str());
        if (std::count_if(devices.begin(), devices.end(),
                          [&](const auto &other) {
                            return other.input == input && other.name == d.name;
                          }) > 1)
          n += " [" + juce::String(d.id) + "]";
        out.add(n);
      }
    return out;
  }
  const AudioDeviceInfo *lookup(const juce::String &name, bool input) const {
    auto list = names(input);
    const auto index = list.indexOf(name);
    if (index < 0)
      return nullptr;
    int n = 0;
    for (const auto &d : devices)
      if (d.input == input && n++ == index)
        return &d;
    return nullptr;
  }

public:
  AsioType() : juce::AudioIODeviceType("ASIO") {}
  void scanForDevices() override {
    std::vector<AudioDeviceInfo> next;
    if (driver.interface().enumerate(driver.interface().context, next) ==
        Status::ok)
      devices = std::move(next);
    else
      devices.clear();
  }
  juce::StringArray getDeviceNames(bool input) const override {
    return names(input);
  }
  int getDefaultDeviceIndex(bool input) const override {
    int n = 0;
    for (const auto &d : devices)
      if (d.input == input) {
        if (d.isDefault)
          return n;
        ++n;
      }
    return n ? 0 : -1;
  }
  int getIndexOfDevice(juce::AudioIODevice *device, bool input) const override {
    return names(input).indexOf(device ? device->getName() : juce::String{});
  }
  bool hasSeparateInputsAndOutputs() const override { return false; }
  juce::AudioIODevice *createDevice(const juce::String &output,
                                    const juce::String &input) override {
    const auto *d =
        lookup(output.isNotEmpty() ? output : input, output.isEmpty());
    if (!d)
      return nullptr;
    if (output.isNotEmpty() && input.isNotEmpty()) {
      auto *i = lookup(input, true);
      if (!i || i->id != d->id)
        return nullptr;
    }
    return new AsioDevice(output.isNotEmpty() ? output : input, d->id);
  }
};
} // namespace
std::unique_ptr<juce::AudioIODeviceType> createCyAudioCoreAsioType() {
  return std::make_unique<AsioType>();
}
} // namespace cydaw::integration
