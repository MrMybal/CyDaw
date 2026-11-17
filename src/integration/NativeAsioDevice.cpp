#include <algorithm>
#include <cmath>
#include <cydaw/audio/Devices.h>
#include <cydaw/base/Text.h>
#include <cydaw/platform/AsioDriver.h>
namespace cydaw::integration {
namespace {
using namespace cyaudio;
cydaw::base::Text error(Status s) {
  return s == Status::ok ? cydaw::base::Text{}
                         : "CyAudioCore / PortAudio ASIO: status " +
                               cydaw::base::Text(int(s));
}
class AsioDevice final : public cydaw::audio::Device {
public:
  platform::AsioDriver driver;
  AudioDeviceManager manager;
  std::string id;
  platform::AsioDeviceDetails details;
  AudioDeviceConfig config;
  AudioStreamDescription stream;
  cydaw::audio::ChannelSet inputMask, outputMask;
  cydaw::audio::DeviceCallback *callback = nullptr;
  cydaw::base::Text lastError;
  bool opened = false;
  AsioDevice(const cydaw::base::Text &name, std::string key)
      : cydaw::audio::Device(name, "ASIO"), id(std::move(key)) {
    lastError = error(driver.details(id, details));
    if (lastError.isEmpty())
      lastError = error(manager.prepare(driver.interface()));
  }
  ~AsioDevice() override { close(); }
  static cydaw::base::TextList names(const std::vector<std::string> &list) {
    cydaw::base::TextList out;
    for (const auto &n : list)
      out.add(cydaw::base::Text::fromUTF8(n.c_str()));
    return out;
  }
  cydaw::base::TextList getOutputChannelNames() override {
    return names(details.outputNames);
  }
  cydaw::base::TextList getInputChannelNames() override {
    return names(details.inputNames);
  }
  cydaw::base::Array<double> getAvailableSampleRates() override {
    cydaw::base::Array<double> out;
    for (auto rate : details.sampleRates)
      out.add(rate);
    return out;
  }
  cydaw::base::Array<int> getAvailableBufferSizes() override {
    cydaw::base::Array<int> out;
    for (auto n : details.bufferSizes)
      out.add(n);
    return out;
  }
  int getDefaultBufferSize() override { return details.preferredBufferFrames; }
  cydaw::base::Text open(const cydaw::audio::ChannelSet &inputs,
                         const cydaw::audio::ChannelSet &outputs, double rate,
                         int frames) override {
    close();
    if (!std::isfinite(rate) || rate <= 0 || rate > 768000 || frames <= 0 ||
        frames > 1048576)
      return lastError = error(Status::invalid_argument);
    // ASIO drivers may expose only their current control-panel quantum. Do
    // not let PortAudio silently split that quantum into an unsupported small
    // client period: per-instance plugin IPC requires regular block deadlines.
    if (std::find(details.bufferSizes.begin(), details.bufferSizes.end(),
                  frames) == details.bufferSizes.end())
      return lastError = "ASIO buffer size is not advertised by the driver; "
                         "use its control panel.";
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
    // Open without starting: prepare consumers before starting the stream.
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
  void start(cydaw::audio::DeviceCallback *cb) override {
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
  cydaw::base::Text getLastError() override { return lastError; }
  int getCurrentBufferSizeSamples() override {
    return int(config.periodFrames);
  }
  double getCurrentSampleRate() override {
    return stream.sampleRate ? stream.sampleRate : config.sampleRate;
  }
  int getCurrentBitDepth() override { return 32; } // Float32 client stream.
  cydaw::audio::ChannelSet getActiveOutputChannels() const override {
    return outputMask;
  }
  cydaw::audio::ChannelSet getActiveInputChannels() const override {
    return inputMask;
  }
  std::optional<cyaudio::AudioStreamDescription>
  streamDescription() const override {
    return opened ? std::optional<cyaudio::AudioStreamDescription>(stream)
                  : std::nullopt;
  }
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
    cydaw::audio::DeviceCallbackContext c;
    const auto ns = context.clock.monotonicNanoseconds;
    if (context.clockValid)
      c.hostTimeNs = &ns;
    c.native = &context;
    s.callback->audioDeviceIOCallbackWithContext(
        in.channelData(), int(in.channels()), out.channelData(),
        int(out.channels()), int(out.frames()), c);
    return Status::ok;
  }
};
class AsioType final : public cydaw::audio::DeviceType {
  platform::AsioDriver driver;
  std::vector<AudioDeviceInfo> devices;
  cydaw::base::TextList names(bool input) const {
    cydaw::base::TextList out;
    for (const auto &d : devices)
      if (d.input == input) {
        auto n = cydaw::base::Text::fromUTF8(d.name.c_str());
        if (std::count_if(devices.begin(), devices.end(),
                          [&](const auto &other) {
                            return other.input == input && other.name == d.name;
                          }) > 1)
          n += " [" + cydaw::base::Text(d.id) + "]";
        out.add(n);
      }
    return out;
  }
  const AudioDeviceInfo *lookup(const cydaw::base::Text &name,
                                bool input) const {
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
  AsioType() : cydaw::audio::DeviceType("ASIO") {}
  void scanForDevices() override {
    std::vector<AudioDeviceInfo> next;
    if (driver.interface().enumerate(driver.interface().context, next) ==
        Status::ok)
      devices = std::move(next);
    else
      devices.clear();
  }
  cydaw::base::TextList getDeviceNames(bool input) const override {
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
  int getIndexOfDevice(cydaw::audio::Device *device,
                       bool input) const override {
    return names(input).indexOf(device ? cydaw::base::Text(device->getName())
                                       : cydaw::base::Text{});
  }
  bool hasSeparateInputsAndOutputs() const override { return false; }
  cydaw::audio::Device *createDevice(const cydaw::base::Text &output,
                                     const cydaw::base::Text &input) override {
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
std::unique_ptr<cydaw::audio::DeviceType> createNativeAsioDeviceType() {
  return std::make_unique<AsioType>();
}
} // namespace cydaw::integration
