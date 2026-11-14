#include <cydaw/audio/Sources.h>
#include <iostream>
#include <stdexcept>
using namespace cydaw::audio;
void require(bool value, const char *text) {
  if (!value)
    throw std::runtime_error(text);
}
struct Ramp final : PositionableAudioSource {
  std::int64_t position{};
  bool loop = false;
  int prepared = 0;
  int lastChannels = 0;
  void prepareToPlay(int, double) override { ++prepared; }
  void releaseResources() override { --prepared; }
  void getNextAudioBlock(const AudioSourceChannelInfo &block) override {
    lastChannels = block.buffer->getNumChannels();
    block.clearActiveBufferRegion();
    for (int i = 0; i < block.numSamples; ++i) {
      if (position >= 16) {
        if (!loop)
          break;
        position %= 16;
      }
      for (int ch = 0; ch < block.buffer->getNumChannels(); ++ch)
        block.buffer->setSample(ch, block.startSample + i, float(position + 1));
      ++position;
    }
  }
  void setNextReadPosition(std::int64_t at) override { position = at; }
  std::int64_t getNextReadPosition() const override { return position; }
  std::int64_t getTotalLength() const override { return 16; }
  bool isLooping() const override { return loop; }
  void setLooping(bool value) override { loop = value; }
};
int main() {
  try {
    Ramp input;
    AudioTransportSource transport;
    transport.prepareToPlay(8, 48000);
    transport.setSource(&input);
    require(input.prepared == 1,
            "source added after device preparation is prepared immediately");
    SampleBuffer<float> samples(2, 24);
    transport.start();
    transport.getNextAudioBlock({&samples, 4, 16});
    require(samples.getSample(0, 3) == 0 && samples.getSample(0, 4) == 1 &&
                samples.getSample(0, 19) == 16 && samples.getSample(0, 20) == 0,
            "Core transport renders a borrowed subrange in bounded chunks");
    transport.stop();
    transport.setPosition(4. / 48000);
    require(std::abs(transport.getCurrentPosition() - 4. / 48000) < 1e-12,
            "control seek is visible before callback");
    transport.getNextAudioBlock({&samples, 0, 8});
    require(samples.getMagnitude(0, 8) == 0, "paused transport remains silent");
    transport.start();
    transport.getNextAudioBlock({&samples, 0, 8});
    require(samples.getSample(0, 0) == 5 && samples.getSample(0, 7) == 12,
            "Core transport resumes at selected frame");
    input.loop = true;
    transport.setPosition(14. / 48000);
    transport.getNextAudioBlock({&samples, 0, 8});
    require(samples.getSample(0, 0) == 15 && samples.getSample(0, 1) == 16 &&
                samples.getSample(0, 2) == 1 && samples.getSample(0, 7) == 6,
            "Core transport loops without a gap");
    transport.setSource(nullptr);
    require(input.prepared == 0, "source detaches before destruction");
    transport.getNextAudioBlock({&samples, 0, 8});
    require(samples.getMagnitude(0, 8) == 0, "detached transport is silent");
    transport.releaseResources();
    {
      cydaw::audio::ReadAheadScheduler scheduler("Native test reader");
      Ramp bufferedInput;
      BufferingAudioSource buffered(&bufferedInput, scheduler, false, 8192, 2,
                                    true);
      buffered.prepareToPlay(8, 48000);
      require(buffered.waitForReady(8),
              "background source prefills prepared pages");
      buffered.getNextAudioBlock({&samples, 0, 8});
      require(samples.getSample(0, 0) == 1 && samples.getSample(0, 7) == 8 &&
                  buffered.underruns() == 0,
              "background cache delivers exact samples");
      buffered.setNextReadPosition(12);
      require(buffered.waitForReady(4), "background cache seeks");
      buffered.getNextAudioBlock({&samples, 0, 8});
      require(samples.getSample(0, 0) == 13 && samples.getSample(0, 3) == 16 &&
                  samples.getSample(0, 4) == 0,
              "background cache preserves end-of-source silence");
      // Exercise the unchanged-window path only after every page is ready.
      buffered.setNextReadPosition(0);
      require(buffered.waitForReady(8192), "complete cache window prefills");
      buffered.setLooping(true);
      require(buffered.waitForReady(8192),
              "loop changes invalidate even an unchanged complete window");
      buffered.getNextAudioBlock({&samples, 0, 24});
      for (int i = 0; i < 24; ++i)
        require(samples.getSample(0, i) == float(i % 16 + 1),
                "loop refresh replaces previously cached EOF silence");
      for (const auto position : {20000, 0, 16384, 1020, 90000, 0}) {
        buffered.setNextReadPosition(position);
        require(buffered.waitForReady(24), "seek across evicted pages refills");
        buffered.getNextAudioBlock({&samples, 0, 24});
        for (int ch = 0; ch < 2; ++ch)
          for (int i = 0; i < 24; ++i)
            require(samples.getSample(ch, i) == float((position + i) % 16 + 1),
                    "cache preserves exact PCM across seeks and page edges");
      }
      buffered.setNextReadPosition(0);
      require(buffered.waitForReady(8192), "seek back restores a full window");
      buffered.setLooping(false);
      require(buffered.waitForReady(8192), "disabling loop invalidates full window");
      buffered.getNextAudioBlock({&samples, 0, 24});
      for (int i = 0; i < 24; ++i)
        require(samples.getSample(0, i) == (i < 16 ? float(i + 1) : 0.f),
                "loop disable restores exact EOF silence");
      require(buffered.underruns() == 0, "prepared cache reads never underrun");
      buffered.releaseResources();
      require(bufferedInput.prepared == 0,
              "background worker releases source before owner destruction");
    }
    {
      Ramp first, second;
      MixerAudioSource mixer;
      mixer.addInputSource(&first, false);
      mixer.addInputSource(&second, false);
      mixer.prepareToPlay(8, 48000);
      mixer.getNextAudioBlock({&samples, 0, 8});
      require(samples.getSample(0, 0) == 2 && samples.getSample(1, 7) == 16,
              "native audition mixer sums its inputs");
      require(first.lastChannels == 2 && second.lastChannels == 2,
              "audition mixer preserves requested channel width");
      mixer.removeAllInputs();
      require(first.prepared == 0 && second.prepared == 0,
              "mixer releases detached inputs");
    }
    std::cout << "Native source lifecycle, Core transport, bounded render, "
                 "seek, pause and loop passed without JUCE\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
