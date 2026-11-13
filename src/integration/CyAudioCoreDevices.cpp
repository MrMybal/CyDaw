#include <algorithm>
#include <cmath>
#include <cyaudio/io/AudioDeviceManager.h>
#include <cydaw/integration/CyAudioCoreDeviceManager.h>
namespace cydaw::integration {
namespace {
using namespace cyaudio;
juce::String nativeError(Status r) {
  return r == Status::ok ? juce::String{}
                         : "CyAudioCore audio: status " + juce::String(int(r));
}
class CoreDevice final : public juce::AudioIODevice {
  std::shared_ptr<AudioDeviceContext> context;
  AudioDevice device;
  AudioDeviceManager manager;
  AudioAccess access;
  std::string inputId, outputId;
  AudioDeviceCapabilities inputCaps, outputCaps;
  AudioDeviceConfig config;
  AudioStreamDescription stream;
  juce::BigInteger inputMask, outputMask;
  std::vector<const float *> inputPointers;
  std::vector<float *> outputPointers;
  juce::AudioIODeviceCallback *callback = nullptr;
  juce::String lastError;
  bool opened = false;
  static juce::StringArray names(const AudioDeviceCapabilities &caps,
                                 bool input) {
    juce::StringArray names;
    for (const auto &n : caps.channelNames)
      names.add(juce::String::fromUTF8(n.c_str()));
    unsigned count = 0;
    for (const auto &f : caps.nativeFormats)
      count = std::max(count, f.channels);
    // An endpoint with no reported labels still has a configurable client
    // layout. Generic labels are UI labels, not fabricated native metadata.
    if (!count && names.isEmpty())
      count = 2;
    count = std::min(count, 256u);
    while (unsigned(names.size()) < count)
      names.add(juce::String(input ? "Input " : "Output ") +
                juce::String(names.size() + 1));
    return names;
  }

public:
  CoreDevice(juce::String name, juce::String type,
             std::shared_ptr<AudioDeviceContext> c, AudioAccess a,
             std::string input, std::string output)
      : juce::AudioIODevice(name, type), context(std::move(c)), access(a),
        inputId(std::move(input)), outputId(std::move(output)) {
    if (!inputId.empty())
      (void)context->capabilities(inputId, true, inputCaps);
    if (!outputId.empty())
      (void)context->capabilities(outputId, false, outputCaps);
    AudioDeviceDriver driver;
    driver.context = this;
    driver.enumerate = [](void *p, std::vector<AudioDeviceInfo> &out) noexcept {
      auto &s = *static_cast<CoreDevice *>(p);
      return s.context->enumerate(out);
    };
    driver.open = [](void *p, const AudioDeviceConfig &cfg,
                     AudioDeviceCallback cb, void *u) noexcept {
      auto &s = *static_cast<CoreDevice *>(p);
      return s.device.open(*s.context, cfg, cb, u);
    };
    driver.openWithContext = [](void *p, const AudioDeviceConfig &cfg,
                                AudioDeviceContextCallback cb,
                                void *u) noexcept {
      auto &s = *static_cast<CoreDevice *>(p);
      return s.device.openWithContext(*s.context, cfg, cb, u);
    };
    driver.start = [](void *p) noexcept {
      return static_cast<CoreDevice *>(p)->device.start();
    };
    driver.close = [](void *p) noexcept {
      static_cast<CoreDevice *>(p)->device.close();
    };
    driver.running = [](void *p) noexcept {
      return static_cast<CoreDevice *>(p)->device.running();
    };
    driver.sampleRate = [](void *p) noexcept {
      return static_cast<CoreDevice *>(p)->device.sampleRate();
    };
    driver.notifications = [](void *p) noexcept {
      return static_cast<CoreDevice *>(p)->device.takeNotifications();
    };
    driver.description = [](void *p, AudioStreamDescription &out) noexcept {
      return static_cast<CoreDevice *>(p)->device.description(out);
    };
    lastError = nativeError(manager.prepare(driver));
  }
  ~CoreDevice() override { close(); }
  const std::string &selectedId(bool input) const {
    return input ? inputId : outputId;
  }
  juce::StringArray getInputChannelNames() override {
    return inputId.empty() ? juce::StringArray{} : names(inputCaps, true);
  }
  juce::StringArray getOutputChannelNames() override {
    return outputId.empty() ? juce::StringArray{} : names(outputCaps, false);
  }
  juce::Array<double> getAvailableSampleRates() override {
    juce::Array<double> rates;
    for (const auto &caps : {inputCaps, outputCaps})
      for (const auto &f : caps.nativeFormats)
        if (f.sampleRate)
          rates.addIfNotAlreadyThere(f.sampleRate);
    // Shared/native-converting devices accept a client rate independently
    // of the hardware rate. open() still validates the actual configuration.
    if (rates.isEmpty())
      for (double r : {44100., 48000., 88200., 96000., 192000.})
        rates.add(r);
    rates.sort();
    return rates;
  }
  juce::Array<int> getAvailableBufferSizes() override {
    juce::Array<int> sizes;
    for (int n : {32, 64, 128, 256, 512, 1024, 2048, 4096}) {
      const auto permitted = [&](const auto &c) {
        return !c.bufferRangeKnown || (unsigned(n) >= c.minimumBufferFrames &&
                                       unsigned(n) <= c.maximumBufferFrames);
      };
      if (permitted(inputCaps) && permitted(outputCaps))
        sizes.add(n);
    }
    return sizes;
  }
  int getDefaultBufferSize() override { return 256; }
  juce::String open(const juce::BigInteger &in, const juce::BigInteger &out,
                    double rate, int frames) override {
    close();
    if (!std::isfinite(rate) || rate <= 0 || rate > 768000 || frames <= 0 ||
        frames > 1048576)
      return lastError = nativeError(Status::invalid_argument);
    config = {};
    inputMask = in;
    outputMask = out;
    config.inputId = inputId;
    config.outputId = outputId;
    config.inputChannels = in.countNumberOfSetBits();
    config.outputChannels = out.countNumberOfSetBits();
    config.sampleRate = unsigned(std::llround(rate));
    config.periodFrames = unsigned(frames);
    config.maxCallbackFrames = unsigned(frames);
    config.access = access;
    for (int i = in.findNextSetBit(0); i >= 0; i = in.findNextSetBit(i + 1))
      config.activeInputChannels.push_back(unsigned(i));
    for (int i = out.findNextSetBit(0); i >= 0; i = out.findNextSetBit(i + 1))
      config.activeOutputChannels.push_back(unsigned(i));
    inputPointers.resize(config.inputChannels);
    outputPointers.resize(config.outputChannels);
    const auto r = device.openWithContext(*context, config, render, this);
    lastError = nativeError(r);
    opened = r == Status::ok;
    if (opened) {
      (void)device.description(stream);
      device.close();
    }
    return lastError;
  }
  void start(juce::AudioIODeviceCallback *cb) override {
    stop();
    if (!opened || !cb)
      return;
    callback = cb;
    cb->audioDeviceAboutToStart(this);
    AudioReconnectOptions options;
    options.automatic = false;
    options.fallbackToDefault = false;
    const auto r = manager.openWithContext(config, render, this, options);
    if (r != Status::ok) {
      lastError = nativeError(r);
      manager.close();
      callback = nullptr;
      cb->audioDeviceStopped();
      cb->audioDeviceError(lastError);
    } else {
      lastError.clear();
      (void)manager.description(stream);
    }
  }
  void stop() override {
    manager.close();
    auto *cb = std::exchange(callback, nullptr);
    if (cb)
      cb->audioDeviceStopped();
  }
  void close() override {
    stop();
    device.close();
    opened = false;
  }
  bool isOpen() override { return opened; }
  bool isPlaying() override { return callback && manager.running(); }
  juce::String getLastError() override { return lastError; }
  int getCurrentBufferSizeSamples() override {
    return int(config.periodFrames);
  }
  double getCurrentSampleRate() override {
    return stream.sampleRate ? stream.sampleRate : config.sampleRate;
  }
  int getCurrentBitDepth() override { return 32; }
  juce::BigInteger getActiveInputChannels() const override { return inputMask; }
  juce::BigInteger getActiveOutputChannels() const override {
    return outputMask;
  }
  int latency(const AudioTimingValue &value) const {
    return value.valid
               ? int(std::llround(value.nanoseconds * 1e-9 * stream.sampleRate))
               : 0;
  }
  int getInputLatencyInSamples() override {
    return latency(stream.input.latency);
  }
  int getOutputLatencyInSamples() override {
    return latency(stream.output.latency);
  }
  static Status render(void *p, AudioBlock<const float> in,
                       AudioBlock<float> out,
                       const AudioCallbackContext &c) noexcept {
    auto &s = *static_cast<CoreDevice *>(p);
    if (!s.callback) {
      for (std::size_t ch = 0; ch < out.channels(); ++ch)
        std::fill(out.channel(ch).begin(), out.channel(ch).end(), 0.f);
      return Status::ok;
    }
    for (std::size_t ch = 0; ch < in.channels(); ++ch)
      s.inputPointers[ch] = in.channel(ch).data();
    for (std::size_t ch = 0; ch < out.channels(); ++ch)
      s.outputPointers[ch] = out.channel(ch).data();
    const auto time = c.clock.monotonicNanoseconds;
    juce::AudioIODeviceCallbackContext clock;
    if (c.clockValid)
      clock.hostTimeNs = &time;
    s.callback->audioDeviceIOCallbackWithContext(
        s.inputPointers.data(), int(in.channels()), s.outputPointers.data(),
        int(out.channels()), int(out.frames()), clock);
    return Status::ok;
  }
};
class CoreType final : public juce::AudioIODeviceType {
  std::shared_ptr<AudioDeviceContext> context =
      std::make_shared<AudioDeviceContext>();
  AudioContextOptions options;
  AudioAccess access;
  Status prepared = Status::not_prepared;
  std::vector<AudioDeviceInfo> devices;
  juce::String display(const AudioDeviceInfo &d) const {
    auto name = juce::String::fromUTF8(d.name.c_str());
    if (std::count_if(devices.begin(), devices.end(), [&](const auto &other) {
          return other.input == d.input && other.name == d.name;
        }) > 1)
      name += " [" + juce::String(d.id) + "]";
    return name;
  }
  const AudioDeviceInfo *find(const juce::String &name, bool input) const {
    for (const auto &d : devices)
      if (d.input == input && display(d) == name)
        return &d;
    return nullptr;
  }

public:
  CoreType(juce::String name, AudioBackend backend, AudioAccess a)
      : juce::AudioIODeviceType(name), options{backend}, access(a) {}
  void scanForDevices() override {
    if (prepared != Status::ok)
      prepared = context->prepare(options);
    std::vector<AudioDeviceInfo> next;
    if (prepared == Status::ok && context->enumerate(next) == Status::ok)
      devices = std::move(next);
    else
      devices.clear();
  }
  juce::StringArray getDeviceNames(bool input) const override {
    juce::StringArray names;
    for (const auto &d : devices)
      if (d.input == input)
        names.add(display(d));
    return names;
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
  int getIndexOfDevice(juce::AudioIODevice *d, bool input) const override {
    const auto *device = dynamic_cast<const CoreDevice *>(d);
    if (!device)
      return -1;
    int index = 0;
    for (const auto &info : devices)
      if (info.input == input) {
        if (info.id == device->selectedId(input))
          return index;
        ++index;
      }
    return -1;
  }
  bool hasSeparateInputsAndOutputs() const override { return true; }
  juce::AudioIODevice *createDevice(const juce::String &output,
                                    const juce::String &input) override {
    auto *in = find(input, true);
    auto *out = find(output, false);
    if ((input.isNotEmpty() && !in) || (output.isNotEmpty() && !out) ||
        (!in && !out))
      return nullptr;
    return new CoreDevice(output.isNotEmpty() ? output : input, getTypeName(),
                          context, access, in ? in->id : std::string{},
                          out ? out->id : std::string{});
  }
};
} // namespace
std::unique_ptr<juce::AudioIODeviceType>
createCyAudioCoreDeviceType(const juce::String &name,
                            cyaudio::AudioBackend backend,
                            cyaudio::AudioAccess access) {
  return std::make_unique<CoreType>(name, backend, access);
}
} // namespace cydaw::integration
