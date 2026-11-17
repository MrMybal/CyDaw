#include "SamplerMapFile.h"
#include "SoundModelFile.h"
#include <cydaw/base/Thread.h>
#include <cydaw/document/PolySynthState.h>
#include <cydaw/integration/CyAudioCoreAudioFormats.h>
#include <iostream>
#include <stdexcept>
using namespace cydaw;
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
int main() {
  try {
    auto file = base::File::createTempFile(".wav");
    std::unique_ptr<base::OutputStream> output = file.createOutputStream();
    integration::WaveFormat format;
    auto writer =
        format.createWriterFor(output, audio::AudioFormatWriterOptions()
                                           .withSampleRate(48000)
                                           .withNumChannels(1)
                                           .withBitsPerSample(24));
    require(bool(writer), "sample fixture writer");
    std::array<float, 512> samples{};
    for (int i = 0; i < 512; ++i)
      samples[i] = 0.5f * std::sin(float(i) * 0.1f);
    const float *channels[] = {samples.data()};
    require(writer->writeFromFloatArrays(channels, 1, 512),
            "sample fixture audio");
    writer.reset();
    auto &store = app::SampleStore::instance();
    auto sample = store.acquire(file, true);
    require(sample && sample->asleep() && !sample->ready.load(),
            "deferred sample is not decoded eagerly");
    sample->request();
    auto limit = base::Time::getMillisecondCounterHiRes() + 5000;
    while (!sample->ready.load() && !sample->failed.load() &&
           base::Time::getMillisecondCounterHiRes() < limit)
      base::Thread::sleep(1);
    require(sample->ready.load() && sample->channels == 1 &&
                sample->frames == 512,
            "native worker decodes requested sample");
    require(std::abs(sample->pcm[123] * sample->scale - samples[123]) < 2e-5,
            "native sampler PCM and scale retain the recording");
    require(store.acquire(file).get() == sample.get(),
            "identical samples share cached decoding");
    app::SampleStore::shutdown();
    require(file.deleteFile(), "sample readers close before shutdown returns");
    base::Text error;
    auto model = app::soundModelFromVar(
        base::JSON::parse(
            R"fixture({"format":"cydaw-sound-model","version":1,"zones":[{"frames":2,"harmonics":{"count":2,"db10":"z:eJyb89/i/7n/c/4DABqYBjs="},"noise":{"edges":[],"db10":[]}}]})fixture"),
        error);
    require(model && model->zones.size() == 1 &&
                model->zones[0].harmonics.size() == 4,
            "actual compressed spectral model loads without JUCE");
    require(
        std::abs(model->zones[0].harmonics[0] - std::pow(10.f, -0.5f)) < 1e-6 &&
            std::abs(model->zones[0].harmonics[2] - std::pow(10.f, -0.75f)) <
                1e-6,
        "spectral delta rows retain their amplitude");
    require(
        !app::soundModelFromVar(
            base::JSON::parse(
                R"fixture({"format":"cydaw-sound-model","version":1,"zones":[{"frames":2,"harmonics":{"count":2,"db10":"z:eJyb89/i/7n/c/4DABqYBm4="},"noise":{"edges":[],"db10":[]}}]})fixture"),
            error),
        "corrupt spectral model is rejected");
    engine::PolySynthGraphPlan plan;
    plan.enabled = true;
    plan.voiceCount = 24;
    plan.voiceMode = 2;
    plan.glideSeconds = 0.3f;
    plan.oscillatorLevel = 0.2f;
    auto planState = app::serialisePolySynthGraphPlan(plan);
    auto restored = app::restorePolySynthGraphPlan(
        base::JSON::parse(base::JSON::toString(planState)), error);
    require(restored && restored->enabled && restored->voiceCount == 24 &&
                restored->voiceMode == 2 &&
                std::abs(restored->glideSeconds - 0.3f) < 1e-6,
            "actual polyphonic plan document roundtrip without JUCE");
    std::cout << "Native deferred sampler, shared recording and compressed "
                 "sound model passed without JUCE\n";
    return 0;
  } catch (const std::exception &e) {
    app::SampleStore::shutdown();
    std::cerr << e.what() << '\n';
    return 1;
  }
}
