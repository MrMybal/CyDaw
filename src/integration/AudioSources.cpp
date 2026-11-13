#include <algorithm>
#include <cmath>
#include <cydaw/audio/Sources.h>
#include <stdexcept>
namespace cydaw::audio {
struct AudioTransportSource::Native {
  struct Adapter final : cyaudio::AudioSource<float> {
    PositionableAudioSource *input{};
    double rate = 48000;
    int channels = 2;
    cyaudio::AudioSourceInfo info() const noexcept override {
      return {rate,
              input ? std::uint64_t(
                          std::max<std::int64_t>(0, input->getTotalLength()))
                    : 0,
              std::size_t(channels)};
    }
    cyaudio::Status readAt(std::uint64_t first,
                           cyaudio::AudioBlock<float> output,
                           std::size_t &count) noexcept override {
      count = 0;
      if (!input)
        return cyaudio::Status::not_prepared;
      std::array<float *, 64> pointers{};
      for (std::size_t ch = 0; ch < output.channels(); ++ch)
        pointers[ch] = output.channel(ch).data();
      SampleBuffer<float> view(pointers.data(), int(output.channels()),
                               int(output.frames()));
      if (input->getNextReadPosition() != std::int64_t(first))
        input->setNextReadPosition(std::int64_t(first));
      input->getNextAudioBlock({&view, 0, int(output.frames())});
      count = output.frames();
      return cyaudio::Status::ok;
    }
  } adapter;
  cyaudio::AudioTransport<float> transport;
  std::unique_ptr<BufferingAudioSource> buffered;
  SampleBuffer<float> scratch;
  std::atomic<bool> playing{};
  std::atomic<double> wantedSeconds{};
  std::atomic<std::uint64_t> seekEpoch{};
  std::atomic<std::uint64_t> appliedSeek{};
  std::atomic<float> gain{1};
  bool configured{}, prepared{}, appliedPlaying{}, appliedLoop{};
  std::uint64_t loopEnd{}, audioSeek{};
  float lastGain = 1;
  int maximumFrames = 512;
};
AudioTransportSource::AudioTransportSource()
    : native_(std::make_unique<Native>()) {}
AudioTransportSource::~AudioTransportSource() { releaseResources(); }
void AudioTransportSource::setSource(PositionableAudioSource *input,
                                     int readAhead,
                                     ReadAheadScheduler *scheduler,
                                     double rateHint, int channels) {
  auto &n = *native_;
  const bool prepared = n.configured;
  const auto frames = n.maximumFrames;
  const auto rate = n.adapter.rate;
  releaseResources();
  n.buffered.reset();
  if (input && readAhead > 0) {
    if (!scheduler)
      throw std::invalid_argument("A read-ahead scheduler is required");
    n.buffered = std::make_unique<BufferingAudioSource>(
        input, *scheduler, false, readAhead, channels, true);
  }
  n.adapter.input = n.buffered ? n.buffered.get() : input;
  n.adapter.channels = std::clamp(channels, 1, 64);
  if (!prepared && std::isfinite(rateHint) && rateHint > 0)
    n.adapter.rate = rateHint;
  n.playing = false;
  n.wantedSeconds = 0;
  ++n.seekEpoch;
  if (prepared)
    prepareToPlay(frames, rate);
}
void AudioTransportSource::prepareToPlay(int frames, double rate) {
  auto &n = *native_;
  if (n.prepared)
    releaseResources();
  n.maximumFrames = std::max(1, frames);
  n.adapter.rate = std::isfinite(rate) && rate > 0 ? rate : 48000;
  n.configured = true;
  if (!n.adapter.input)
    return;
  n.adapter.input->prepareToPlay(n.maximumFrames, n.adapter.rate);
  n.scratch.setSize(n.adapter.channels, n.maximumFrames);
  const auto status =
      n.transport.prepare({n.adapter.rate, std::size_t(n.maximumFrames),
                           std::size_t(n.adapter.channels)},
                          n.adapter, 8);
  n.prepared = status == cyaudio::Status::ok;
  n.appliedPlaying = false;
  n.appliedLoop = false;
  n.loopEnd = 0;
  n.audioSeek = std::numeric_limits<std::uint64_t>::max();
  n.lastGain = n.gain.load();
}
void AudioTransportSource::releaseResources() {
  auto &n = *native_;
  n.configured = false;
  if (n.prepared) {
    n.prepared = false;
    n.transport = {};
    if (n.adapter.input)
      n.adapter.input->releaseResources();
  }
  n.scratch.setSize(0, 0);
}
void AudioTransportSource::start() noexcept { native_->playing = true; }
void AudioTransportSource::stop() noexcept { native_->playing = false; }
bool AudioTransportSource::isPlaying() const noexcept {
  return native_->playing.load();
}
void AudioTransportSource::setPosition(double seconds) noexcept {
  auto &n = *native_;
  n.wantedSeconds = std::isfinite(seconds) ? std::max(0., seconds) : 0.;
  ++n.seekEpoch;
}
double AudioTransportSource::getCurrentPosition() const noexcept {
  auto &n = *native_;
  if (n.appliedSeek.load() != n.seekEpoch.load())
    return n.wantedSeconds.load();
  cyaudio::TransportPosition position;
  if (n.prepared && n.transport.position(position) == cyaudio::Status::ok)
    return double(position.frame) / n.adapter.rate;
  return n.wantedSeconds.load();
}
double AudioTransportSource::getLengthInSeconds() const noexcept {
  return native_->adapter.input
             ? double(std::max<std::int64_t>(
                   0, native_->adapter.input->getTotalLength())) /
                   native_->adapter.rate
             : 0.;
}
void AudioTransportSource::setGain(float gain) noexcept {
  native_->gain = std::isfinite(gain) ? std::max(0.f, gain) : 0;
}
void AudioTransportSource::getNextAudioBlock(
    const AudioSourceChannelInfo &block) {
  block.clearActiveBufferRegion();
  auto &n = *native_;
  if (!block.buffer || !n.prepared || !n.adapter.input || block.numSamples <= 0)
    return;
  const auto total = n.adapter.info().frames;
  const bool loop = n.adapter.input->isLooping() && total > 0;
  if (loop != n.appliedLoop || (loop && n.loopEnd != total)) {
    if (n.transport.enqueue({cyaudio::TransportAction::setLoop, 0, total,
                             loop}) == cyaudio::Status::ok) {
      n.appliedLoop = loop;
      n.loopEnd = total;
    }
  }
  const auto epoch = n.seekEpoch.load();
  if (epoch != n.audioSeek) {
    const auto requested = std::uint64_t(std::llround(std::min(
        double(total), std::max(0., n.wantedSeconds.load() * n.adapter.rate))));
    if (n.transport.enqueue({cyaudio::TransportAction::seek, requested}) ==
        cyaudio::Status::ok)
      n.audioSeek = epoch;
  }
  const bool playing = n.playing.load();
  if (playing != n.appliedPlaying) {
    if (n.transport.enqueue({playing ? cyaudio::TransportAction::play
                                     : cyaudio::TransportAction::pause}) ==
        cyaudio::Status::ok)
      n.appliedPlaying = playing;
  }
  for (int offset = 0; offset < block.numSamples;) {
    const int count = std::min(n.maximumFrames, block.numSamples - offset);
    const auto status = n.transport.process(
        {n.scratch.getArrayOfWritePointers(), std::size_t(n.adapter.channels),
         std::size_t(count)},
        cyaudio::UnderrunPolicy::silenceAndAdvance);
    if (status != cyaudio::Status::ok &&
        status != cyaudio::Status::end_of_stream &&
        status != cyaudio::Status::would_block)
      n.scratch.clear();
    const float gain = n.gain.load();
    n.scratch.applyGainRamp(0, count, n.lastGain, gain);
    n.lastGain = gain;
    for (int ch = 0;
         ch < std::min(block.buffer->getNumChannels(), n.adapter.channels);
         ++ch)
      block.buffer->copyFrom(ch, block.startSample + offset, n.scratch, ch, 0,
                             count);
    offset += count;
  }
  n.appliedSeek = n.audioSeek;
  cyaudio::TransportPosition position;
  if (n.transport.position(position) == cyaudio::Status::ok &&
      !position.playing && playing)
    n.playing = false;
}
} // namespace cydaw::audio
