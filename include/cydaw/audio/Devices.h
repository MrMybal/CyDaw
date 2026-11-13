#pragma once
#include <bit>
#include <cyaudio/io/AudioDevice.h>
#include <cydaw/base/Thread.h>
#include <cydaw/base/Value.h>
#include <optional>
namespace cydaw::audio {
// Selected hardware channels, separate from the dense client stream channels.
class ChannelSet {
  std::array<std::uint64_t, 4> words_{};

public:
  bool operator[](int bit) const noexcept {
    return bit >= 0 && bit < 256 &&
           (words_[std::size_t(bit / 64)] &
            (std::uint64_t{1} << unsigned(bit % 64))) != 0;
  }
  void setBit(int bit, bool value = true) noexcept {
    if (bit < 0 || bit >= 256)
      return;
    auto &word = words_[std::size_t(bit / 64)];
    const auto mask = std::uint64_t{1} << unsigned(bit % 64);
    if (value)
      word |= mask;
    else
      word &= ~mask;
  }
  void clearBit(int bit) noexcept { setBit(bit, false); }
  void clear() noexcept { words_ = {}; }
  void setRange(int first, int count, bool value) noexcept {
    for (int bit = std::max(0, first); bit < std::min(256, first + count);
         ++bit)
      setBit(bit, value);
  }
  int countNumberOfSetBits() const noexcept {
    int result = 0;
    for (auto word : words_)
      result += std::popcount(word);
    return result;
  }
  bool isZero() const noexcept { return countNumberOfSetBits() == 0; }
  int getHighestBit() const noexcept {
    for (int word = 3; word >= 0; --word)
      if (words_[std::size_t(word)])
        return word * 64 + 63 - std::countl_zero(words_[std::size_t(word)]);
    return -1;
  }
  int findNextSetBit(int first) const noexcept {
    for (int bit = std::max(0, first); bit < 256; ++bit)
      if ((*this)[bit])
        return bit;
    return -1;
  }
  base::Text toString(int radix, int minimumCharacters = 1) const {
    if (radix != 2 && radix != 16)
      return {};
    base::Text result;
    const int group = radix == 16 ? 4 : 1;
    static constexpr char digits[] = "0123456789abcdef";
    for (int at = std::max(0, getHighestBit()) / group; at >= 0; --at) {
      int value = 0;
      for (int bit = 0; bit < group; ++bit)
        if ((*this)[at * group + bit])
          value |= 1 << bit;
      result += digits[value];
    }
    return result.paddedLeft('0', minimumCharacters);
  }
  void parseString(const base::Text &text, int radix) {
    clear();
    if (radix != 2 && radix != 16)
      return;
    const int group = radix == 16 ? 4 : 1;
    int position = 0;
    for (int at = text.length() - 1; at >= 0 && position < 256; --at) {
      const auto c = text[at];
      const int value = c >= '0' && c <= '9'   ? int(c - '0')
                        : c >= 'a' && c <= 'f' ? 10 + int(c - 'a')
                        : c >= 'A' && c <= 'F' ? 10 + int(c - 'A')
                                               : -1;
      if (value < 0 || value >= radix)
        continue;
      for (int bit = 0; bit < group; ++bit)
        setBit(position + bit, (value & (1 << bit)) != 0);
      position += group;
    }
  }
  friend bool operator==(const ChannelSet &, const ChannelSet &) = default;
};
class Device;
struct DeviceCallbackContext {
  const std::uint64_t *hostTimeNs{};
  const cyaudio::AudioCallbackContext *native{};
};
class DeviceCallback {
public:
  virtual ~DeviceCallback() = default;
  virtual void audioDeviceAboutToStart(Device *) = 0;
  virtual void audioDeviceStopped() = 0;
  virtual void
  audioDeviceIOCallbackWithContext(const float *const *, int, float *const *,
                                   int, int, const DeviceCallbackContext &) = 0;
  virtual void audioDeviceError(const base::Text &) {}
};
class Device {
  base::Text name_, type_;

public:
  Device(base::Text name, base::Text type)
      : name_(std::move(name)), type_(std::move(type)) {}
  virtual ~Device() = default;
  const base::Text &getName() const noexcept { return name_; }
  const base::Text &getTypeName() const noexcept { return type_; }
  virtual base::TextList getInputChannelNames() = 0;
  virtual base::TextList getOutputChannelNames() = 0;
  virtual base::Array<double> getAvailableSampleRates() = 0;
  virtual base::Array<int> getAvailableBufferSizes() = 0;
  virtual int getDefaultBufferSize() = 0;
  virtual base::Text open(const ChannelSet &, const ChannelSet &, double,
                          int) = 0;
  virtual void start(DeviceCallback *) = 0;
  virtual void stop() = 0;
  virtual void close() = 0;
  virtual bool isOpen() = 0;
  virtual bool isPlaying() = 0;
  virtual base::Text getLastError() = 0;
  virtual int getCurrentBufferSizeSamples() = 0;
  virtual double getCurrentSampleRate() = 0;
  virtual int getCurrentBitDepth() = 0;
  virtual ChannelSet getActiveInputChannels() const = 0;
  virtual ChannelSet getActiveOutputChannels() const = 0;
  virtual int getInputLatencyInSamples() = 0;
  virtual int getOutputLatencyInSamples() = 0;
  virtual std::optional<cyaudio::AudioStreamDescription>
  streamDescription() const {
    return {};
  }
  virtual bool hasControlPanel() const { return false; }
  virtual bool showControlPanel() { return false; }
};
class DeviceType {
  base::Text name_;

public:
  explicit DeviceType(base::Text name) : name_(std::move(name)) {}
  virtual ~DeviceType() = default;
  const base::Text &getTypeName() const noexcept { return name_; }
  virtual void scanForDevices() = 0;
  virtual base::TextList getDeviceNames(bool input = false) const = 0;
  virtual int getDefaultDeviceIndex(bool input) const = 0;
  virtual int getIndexOfDevice(Device *, bool input) const = 0;
  virtual bool hasSeparateInputsAndOutputs() const = 0;
  virtual Device *createDevice(const base::Text &output,
                               const base::Text &input) = 0;
};
} // namespace cydaw::audio
namespace cydaw::integration {
std::unique_ptr<audio::DeviceType>
createNativeCoreDeviceType(const base::Text &, cyaudio::AudioBackend,
                           cyaudio::AudioAccess = cyaudio::AudioAccess::shared);
#if CYDAW_WITH_ASIO
std::unique_ptr<audio::DeviceType> createNativeAsioDeviceType();
#endif
} // namespace cydaw::integration
