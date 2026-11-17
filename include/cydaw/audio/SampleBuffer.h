#pragma once
#include <array>
#include <cassert>
#include <cstring>
#include <cydaw/base/Geometry.h>
#include <cydsp/core/AudioBuffer.h>
#include <cydsp/math/VectorOps.h>
namespace cydaw::audio {
// Shared with engine buffers; selected before rendering, no per-view CPU probing.
const cydsp::VectorOps& preparedBufferVectorOps() noexcept;
// Application planar-buffer vocabulary over CyDSP's aligned ownership. A
// borrowed view stores its channel pointers inline and allocates nothing,
// including when rebased to a smaller callback. setSize belongs to
// preparation/control paths.
template <class T> class SampleBuffer {
  static constexpr int maximumChannels = 64;
  cydsp::AudioBuffer<T> storage_;
  cydsp::VectorOps ops_ { preparedBufferVectorOps() };
  std::array<T *, maximumChannels> pointers_{};
  std::array<const T *, maximumChannels> readers_{};
  int channels_{}, frames_{}, capacityChannels_{}, capacityFrames_{};
  bool borrowed_{};
  bool range(int channel, int first, int count) const noexcept {
    return channel >= 0 && channel < channels_ && first >= 0 && count >= 0 &&
           first <= frames_ && count <= frames_ - first;
  }

public:
  SampleBuffer() = default;
  SampleBuffer(int channels, int frames) { setSize(channels, frames); }
  SampleBuffer(T *const *channels, int count, int frames) {
    setDataToReferTo(channels, count, frames);
  }
  SampleBuffer(T *const *channels, int count, int first, int frames) {
    setDataToReferTo(channels, count, first, frames);
  }
  SampleBuffer(const SampleBuffer &other) { makeCopyOf(other); }
  SampleBuffer &operator=(const SampleBuffer &other) {
    if (this != &other)
      makeCopyOf(other);
    return *this;
  }
  SampleBuffer(SampleBuffer &&) = default;
  SampleBuffer &operator=(SampleBuffer &&) = default;
  cydsp::CpuBackend vectorBackend() const noexcept { return ops_.backend(); }
  int getNumChannels() const noexcept { return channels_; }
  int getNumSamples() const noexcept { return frames_; }
  T *getWritePointer(int channel, int first = 0) noexcept {
    assert(range(channel, first, 0));
    return range(channel, first, 0) ? pointers_[channel] + first : nullptr;
  }
  const T *getReadPointer(int channel, int first = 0) const noexcept {
    assert(range(channel, first, 0));
    return range(channel, first, 0) ? readers_[channel] + first : nullptr;
  }
  T *const *getArrayOfWritePointers() noexcept { return pointers_.data(); }
  const T *const *getArrayOfReadPointers() const noexcept {
    return readers_.data();
  }
  void setDataToReferTo(T *const *data, int channels, int frames) {
    setDataToReferTo(data, channels, 0, frames);
  }
  void setDataToReferTo(T *const *data, int channels, int first, int frames) {
    assert(channels >= 0 && channels <= maximumChannels && first >= 0 &&
           frames >= 0);
    if (channels < 0 || channels > maximumChannels || frames < 0 || first < 0)
      return;
    channels_ = channels;
    frames_ = frames;
    borrowed_ = true;
    for (int ch = 0; ch < channels; ++ch) {
      pointers_[ch] = data[ch] + first;
      readers_[ch] = pointers_[ch];
    }
  }
  void setSize(int channels, int frames, bool keep = false,
               bool clearExtra = false, bool avoidReallocating = false) {
    assert(channels >= 0 && channels <= maximumChannels && frames >= 0);
    if (channels < 0 || channels > maximumChannels || frames < 0)
      return;
    if (channels == 0 || frames == 0) {
      storage_ = {};
      pointers_ = {};
      readers_ = {};
      channels_ = channels;
      frames_ = frames;
      capacityChannels_ = capacityFrames_ = 0;
      borrowed_ = false;
      return;
    }
    const int oldChannels = channels_, oldFrames = frames_;
    if (!borrowed_ && avoidReallocating && channels <= capacityChannels_ &&
        frames <= capacityFrames_) {
      channels_ = channels;
      frames_ = frames;
      if (clearExtra) {
        for (int ch = 0; ch < channels_; ++ch) {
          if (ch >= oldChannels)
            clear(ch, 0, frames_);
          else if (frames_ > oldFrames)
            clear(ch, oldFrames, frames_ - oldFrames);
        }
      }
      return;
    }
    if (!borrowed_ && channels == channels_ && frames == frames_)
      return;
    cydsp::AudioBuffer<T> next;
    if (next.resize(std::size_t(channels), std::size_t(frames)) !=
        cydsp::Status::ok)
      throw std::bad_alloc();
    if (keep)
      for (int ch = 0; ch < std::min(channels, channels_); ++ch)
        std::copy_n(readers_[ch], std::min(frames, frames_),
                    next.block().channel(ch).data());
    storage_ = std::move(next);
    channels_ = capacityChannels_ = channels;
    frames_ = capacityFrames_ = frames;
    borrowed_ = false;
    for (int ch = 0; ch < channels_; ++ch) {
      pointers_[ch] = storage_.block().channel(ch).data();
      readers_[ch] = pointers_[ch];
    }
  }
  void makeCopyOf(const SampleBuffer &other, bool avoidReallocating = false) {
    setSize(other.channels_, other.frames_, false, false, avoidReallocating);
    for (int ch = 0; ch < channels_; ++ch)
      copyFrom(ch, 0, other, ch, 0, frames_);
  }
  void clear() noexcept { clear(0, frames_); }
  void clear(int first, int count) noexcept {
    for (int ch = 0; ch < channels_; ++ch)
      clear(ch, first, count);
  }
  void clear(int channel, int first, int count) noexcept {
    assert(range(channel, first, count));
    if (count > 0 && range(channel, first, count))
      ops_.fill(std::span<T>(pointers_[channel] + first, std::size_t(count)),
                T{});
  }
  T getSample(int ch, int frame) const noexcept {
    assert(range(ch, frame, 1));
    return range(ch, frame, 1) ? readers_[ch][frame] : T{};
  }
  void setSample(int ch, int frame, T value) noexcept {
    assert(range(ch, frame, 1));
    if (range(ch, frame, 1))
      pointers_[ch][frame] = value;
  }
  void addSample(int ch, int frame, T value) noexcept {
    assert(range(ch, frame, 1));
    if (range(ch, frame, 1))
      pointers_[ch][frame] += value;
  }
  void copyFrom(int channel, int first, const T *source, int count,
                T gain = T(1)) noexcept {
    assert(range(channel, first, count));
    if (!range(channel, first, count) || !source)
      return;
    auto *to = pointers_[channel] + first;
    if (gain == T(1))
      std::memmove(to, source, std::size_t(count) * sizeof(T));
    else
      (void)ops_.scale(std::span<const T>(source, std::size_t(count)),
                       std::span<T>(to, std::size_t(count)), gain);
  }
  void copyFrom(int channel, int first, const SampleBuffer &source,
                int sourceChannel, int sourceFirst, int count) noexcept {
    assert(source.range(sourceChannel, sourceFirst, count));
    copyFrom(channel, first, source.getReadPointer(sourceChannel, sourceFirst),
             count);
  }
  void addFrom(int channel, int first, const T *source, int count,
               T gain = T(1)) noexcept {
    assert(range(channel, first, count));
    if (range(channel, first, count) && source)
      (void)ops_.addScaled(
          std::span<const T>(source, std::size_t(count)),
          std::span<T>(pointers_[channel] + first, std::size_t(count)), gain);
  }
  void addFrom(int channel, int first, const SampleBuffer &source,
               int sourceChannel, int sourceFirst, int count,
               T gain = T(1)) noexcept {
    assert(source.range(sourceChannel, sourceFirst, count));
    addFrom(channel, first, source.getReadPointer(sourceChannel, sourceFirst),
            count, gain);
  }
  void applyGain(int channel, int first, int count, T gain) noexcept {
    assert(range(channel, first, count));
    if (range(channel, first, count)) {
      auto data = std::span<T>(pointers_[channel] + first, std::size_t(count));
      (void)ops_.scale(data, data, gain);
    }
  }
  void applyGain(int first, int count, T gain) noexcept {
    for (int ch = 0; ch < channels_; ++ch)
      applyGain(ch, first, count, gain);
  }
  void applyGain(T gain) noexcept { applyGain(0, frames_, gain); }
  void applyGainRamp(int channel, int first, int count, T begin,
                     T end) noexcept {
    assert(range(channel, first, count));
    if (!range(channel, first, count) || count <= 0)
      return;
    auto *to = pointers_[channel] + first;
    const T step = (end - begin) / T(count);
    for (int i = 0; i < count; ++i)
      to[i] *= begin + T(i) * step;
  }
  void applyGainRamp(int first, int count, T begin, T end) noexcept {
    for (int ch = 0; ch < channels_; ++ch)
      applyGainRamp(ch, first, count, begin, end);
  }
  T getMagnitude(int channel, int first, int count) const noexcept {
    assert(range(channel, first, count));
    T result{};
    if (range(channel, first, count))
      for (int i = 0; i < count; ++i)
        result = std::max(result, std::abs(readers_[channel][first + i]));
    return result;
  }
  T getMagnitude(int first, int count) const noexcept {
    T result{};
    for (int ch = 0; ch < channels_; ++ch)
      result = std::max(result, getMagnitude(ch, first, count));
    return result;
  }
  T getRMSLevel(int channel, int first, int count) const noexcept {
    assert(range(channel, first, count));
    if (!range(channel, first, count) || count <= 0)
      return {};
    T sum{};
    const auto data =
        std::span<const T>(readers_[channel] + first, std::size_t(count));
    (void)ops_.dot(data, data, sum);
    return std::sqrt(sum / T(count));
  }
  base::Range<T> findMinMax(int channel, int first, int count) const noexcept {
    assert(range(channel, first, count));
    if (!range(channel, first, count) || count <= 0)
      return {};
    const auto *data = readers_[channel] + first;
    const auto [low, high] = std::minmax_element(data, data + count);
    return {*low, *high};
  }
  void reverse(int first, int count) noexcept {
    for (int ch = 0; ch < channels_; ++ch) {
      assert(range(ch, first, count));
      if (range(ch, first, count))
        std::reverse(pointers_[ch] + first, pointers_[ch] + first + count);
    }
  }
};
} // namespace cydaw::audio
