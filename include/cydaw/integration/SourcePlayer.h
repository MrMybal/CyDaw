#pragma once
#include <cydaw/audio/Devices.h>
#include <cydaw/audio/Sources.h>
#include <cydaw/midi/LiveInput.h>
namespace cydaw::integration {
// Native device boundary. The source graph borrows Core channel storage.
class SourcePlayer final : public audio::DeviceCallback {
  audio::AudioSource *source_{};
  double rate_ = 48000;
  int frames_ = 512;
  bool prepared_{};
  audio::SampleBuffer<float> fallback_;
  std::atomic<float> gain_{1};
  float lastGain_ = 1;
  std::uint64_t firstFrame_{};

public:
  void setGain(float value) noexcept {
    gain_ = std::isfinite(value) ? std::max(0.f, value) : 0.f;
  }
  float getGain() const noexcept { return gain_.load(); }
  void setSource(audio::AudioSource *source) {
    if (source_ == source)
      return;
    if (source_ && prepared_)
      source_->releaseResources();
    source_ = source;
    if (source_ && prepared_)
      source_->prepareToPlay(frames_, rate_);
  }
  void audioDeviceAboutToStart(audio::Device *device) override {
    if (prepared_)
      audioDeviceStopped();
    if (!device)
      return;
    rate_ = device->getCurrentSampleRate();
    frames_ = std::max(1, device->getCurrentBufferSizeSamples());
    fallback_.setSize(64, frames_);
    firstFrame_ = 0;
    prepared_ = true;
    lastGain_ = gain_.load();
    if (source_)
      source_->prepareToPlay(frames_, rate_);
  }
  void audioDeviceStopped() override {
    if (prepared_ && source_)
      source_->releaseResources();
    prepared_ = false;
    fallback_.setSize(0, 0);
  }
  void audioDeviceIOCallbackWithContext(
      const float *const *, int inputCount, float *const *output, int channels,
      int count, const audio::DeviceCallbackContext &context) override {
    for (int ch = 0; ch < channels; ++ch)
      if (output[ch])
        std::fill_n(output[ch], count, 0.f);
    if (!source_ || !prepared_ || count <= 0)
      return;
    const auto clockNs =
        context.hostTimeNs ? *context.hostTimeNs : midi::monotonicNanoseconds();
    const int width = std::clamp(std::max(channels, inputCount), 2, 64);
    std::array<float *, 64> pointers{};
    for (int first = 0; first < count;) {
      const int frames = std::min(frames_, count - first);
      for (int ch = 0; ch < width; ++ch)
        pointers[ch] = ch < channels && output[ch]
                           ? output[ch] + first
                           : fallback_.getWritePointer(ch);
      audio::SampleBuffer<float> view(pointers.data(), width, frames);
      const cyaudio::AudioClockAnchor anchor{
          clockNs + std::uint64_t(double(first) / rate_ * 1e9),
          firstFrame_ + std::uint64_t(first), rate_};
      midi::AudioClockScope clock(anchor);
      try {
        source_->getNextAudioBlock({&view, 0, frames});
        const float wanted = gain_.load();
        view.applyGainRamp(0, frames, lastGain_, wanted);
        lastGain_ = wanted;
      } catch (...) {
        view.clear();
      }
      first += frames;
    }
    firstFrame_ += std::uint64_t(count);
  }
};
} // namespace cydaw::integration
