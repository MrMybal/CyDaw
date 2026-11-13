#pragma once
#include "SampleBuffer.h"
#include <atomic>
#include <cyaudio/sources/AudioTransport.h>
#include <cydaw/base/Thread.h>
namespace cydaw::audio {
struct AudioSourceChannelInfo {
  SampleBuffer<float> *buffer{};
  int startSample{}, numSamples{};
  AudioSourceChannelInfo() = default;
  AudioSourceChannelInfo(SampleBuffer<float> *data, int first, int count)
      : buffer(data), startSample(first), numSamples(count) {}
  explicit AudioSourceChannelInfo(SampleBuffer<float> &data)
      : buffer(&data), numSamples(data.getNumSamples()) {}
  void clearActiveBufferRegion() const noexcept {
    if (buffer)
      buffer->clear(startSample, numSamples);
  }
};
class AudioSource {
public:
  virtual ~AudioSource() = default;
  virtual void prepareToPlay(int maximumFrames, double sampleRate) = 0;
  virtual void releaseResources() = 0;
  virtual void getNextAudioBlock(const AudioSourceChannelInfo &) = 0;
};
class PositionableAudioSource : public AudioSource {
public:
  virtual void setNextReadPosition(std::int64_t) = 0;
  virtual std::int64_t getNextReadPosition() const = 0;
  virtual std::int64_t getTotalLength() const = 0;
  virtual bool isLooping() const = 0;
  virtual void setLooping(bool) = 0;
};
class ReadAheadScheduler final : public base::Thread {
  struct Registry;
  std::unique_ptr<Registry> registry_;

public:
  struct Task {
    std::mutex lifetime;
    bool active = true;
    virtual ~Task() = default;
    virtual bool fillOne() = 0;
  };
  explicit ReadAheadScheduler(base::Text name);
  ~ReadAheadScheduler() override;
  void add(const std::shared_ptr<Task> &);
  void remove(const std::shared_ptr<Task> &);
  void run() override;
};
// Background cache for application-rendered sources (including custom stretch
// and pitch curves). The callback only borrows prepared pages using bounded
// CAS.
class BufferingAudioSource final : public PositionableAudioSource {
  struct Native;
  std::unique_ptr<Native> native_;

public:
  BufferingAudioSource(PositionableAudioSource *, ReadAheadScheduler &,
                       bool ownSource, int readAheadFrames, int channels = 2,
                       bool prefillOnPrepare = true);
  ~BufferingAudioSource() override;
  void prepareToPlay(int maximumFrames, double rate) override;
  void releaseResources() override;
  void getNextAudioBlock(const AudioSourceChannelInfo &) override;
  void setNextReadPosition(std::int64_t) override;
  std::int64_t getNextReadPosition() const override;
  std::int64_t getTotalLength() const override;
  bool isLooping() const override;
  void setLooping(bool) override;
  bool waitForReady(int frames, int timeoutMs = 2000) const;
  std::uint64_t underruns() const noexcept;
};
// Stateful application sources render at the sample rate supplied to prepare.
// The Core transport supplies bounded command processing and timeline
// snapshots; controls publish desired state, and only the audio owner touches
// its queue.
class AudioTransportSource final : public AudioSource {
  struct Native;
  std::unique_ptr<Native> native_;

public:
  AudioTransportSource();
  ~AudioTransportSource();
  void setSource(PositionableAudioSource *, int readAheadFrames = 0,
                 ReadAheadScheduler *scheduler = nullptr,
                 double sourceRateHint = 0, int maximumChannels = 2);
  void prepareToPlay(int maximumFrames, double rate) override;
  void releaseResources() override;
  void getNextAudioBlock(const AudioSourceChannelInfo &) override;
  void start() noexcept;
  void stop() noexcept;
  bool isPlaying() const noexcept;
  void setPosition(double seconds) noexcept;
  double getCurrentPosition() const noexcept;
  double getLengthInSeconds() const noexcept;
  void setGain(float gain) noexcept;
};
} // namespace cydaw::audio
