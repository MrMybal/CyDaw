#include <cydaw/audio/Sources.h>
namespace cydaw::audio {
struct MixerAudioSource::Native {
  struct Input {
    AudioSource *source{};
    bool owned{};
  };
  std::recursive_mutex mutex;
  std::vector<Input> inputs;
  SampleBuffer<float> scratch;
  double rate = 48000;
  int frames = 512;
  bool prepared{};
};
MixerAudioSource::MixerAudioSource() : native_(std::make_unique<Native>()) {}
MixerAudioSource::~MixerAudioSource() { removeAllInputs(); }
void MixerAudioSource::addInputSource(AudioSource *source, bool owned) {
  if (!source)
    return;
  auto &n = *native_;
  std::lock_guard guard(n.mutex);
  if (std::ranges::any_of(
          n.inputs, [&](const auto &input) { return input.source == source; }))
    return;
  if (n.prepared)
    source->prepareToPlay(n.frames, n.rate);
  n.inputs.push_back({source, owned});
}
void MixerAudioSource::removeInputSource(AudioSource *source) {
  auto &n = *native_;
  std::lock_guard guard(n.mutex);
  const auto found = std::ranges::find_if(
      n.inputs, [&](const auto &input) { return input.source == source; });
  if (found == n.inputs.end())
    return;
  const auto entry = *found;
  n.inputs.erase(found);
  if (n.prepared)
    entry.source->releaseResources();
  if (entry.owned)
    delete entry.source;
}
void MixerAudioSource::removeAllInputs() {
  auto &n = *native_;
  std::lock_guard guard(n.mutex);
  auto inputs = std::move(n.inputs);
  for (const auto &input : inputs) {
    if (n.prepared)
      input.source->releaseResources();
    if (input.owned)
      delete input.source;
  }
}
void MixerAudioSource::prepareToPlay(int frames, double rate) {
  auto &n = *native_;
  std::lock_guard guard(n.mutex);
  if (n.prepared)
    releaseResources();
  n.frames = std::max(1, frames);
  n.rate = rate;
  n.scratch.setSize(64, n.frames);
  for (const auto &input : n.inputs)
    input.source->prepareToPlay(n.frames, n.rate);
  n.prepared = true;
}
void MixerAudioSource::releaseResources() {
  auto &n = *native_;
  std::lock_guard guard(n.mutex);
  if (n.prepared) {
    for (const auto &input : n.inputs)
      input.source->releaseResources();
    n.prepared = false;
  }
  n.scratch.setSize(0, 0);
}
void MixerAudioSource::getNextAudioBlock(const AudioSourceChannelInfo &block) {
  block.clearActiveBufferRegion();
  if (!block.buffer || block.numSamples <= 0)
    return;
  auto &n = *native_;
  std::lock_guard guard(n.mutex);
  if (!n.prepared)
    return;
  const int channels = std::min(64, block.buffer->getNumChannels());
  for (int first = 0; first < block.numSamples;) {
    const int count = std::min(n.frames, block.numSamples - first);
    SampleBuffer<float> view(n.scratch.getArrayOfWritePointers(), channels,
                             count);
    for (const auto &input : n.inputs) {
      view.clear();
      input.source->getNextAudioBlock({&view, 0, count});
      for (int ch = 0; ch < channels; ++ch)
        block.buffer->addFrom(ch, block.startSample + first, view, ch, 0,
                              count);
    }
    first += count;
  }
}
} // namespace cydaw::audio
