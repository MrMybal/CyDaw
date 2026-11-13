#pragma once
#include <cyaudio/io/AudioDeviceManager.h>
#include <memory>
namespace cydaw::platform {
namespace detail {
struct AsioApi;
}
struct AsioDeviceDetails {
  std::vector<std::string> inputNames, outputNames;
  std::vector<double> sampleRates;
  std::vector<int> bufferSizes;
  int preferredBufferFrames = 0;
  double defaultSampleRate = 0;
};
// Application-owned GPL ASIO extension. The MIT CyAudioCore consumes only its
// public AudioDeviceDriver interface; no SDK headers enter that interface.
class AsioDriver final {
public:
  AsioDriver();
  explicit AsioDriver(
      const detail::AsioApi &); // Deterministic API fixture, no hardware.
  ~AsioDriver();
  AsioDriver(const AsioDriver &) = delete;
  AsioDriver &operator=(const AsioDriver &) = delete;
  cyaudio::AudioDeviceDriver interface() noexcept;
  cyaudio::Status details(std::string_view id, AsioDeviceDetails &) noexcept;
  cyaudio::Status showControlPanel(std::string_view id,
                                   void *nativeParent) noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace cydaw::platform
