#include <cydaw/audio/SampleBuffer.h>
#include <cydaw/base/File.h>
#include <cydaw/document/PolySynthState.h>
#pragma once

#include <cydaw/engine/PolySynthGraph.h>

#include <cydaw/midi/EventBuffer.h>

#include <memory>
#include <optional>

namespace cydaw::app {

// Reusable real-time renderer for instrument Graphs. Graph compilation stays
// on the control thread; the audio callback only reads an immutable plan.
class GraphPolySynthRenderer final {
public:
  GraphPolySynthRenderer();
  ~GraphPolySynthRenderer();

  void prepare(double sampleRate);
  void release();
  void setPlan(cydaw::engine::PolySynthGraphPlan plan);
  [[nodiscard]] cydaw::engine::PolySynthGraphPlan plan() const;
  [[nodiscard]] bool enabled() const noexcept;
  void render(cydaw::audio::SampleBuffer<float> &output,
              const cydaw::midi::EventBuffer &midi, int startSample,
              int numSamples) noexcept;
  void renderWithPlan(cydaw::audio::SampleBuffer<float> &output,
                      const cydaw::midi::EventBuffer &midi, int startSample,
                      int numSamples,
                      const cydaw::engine::PolySynthGraphPlan &plan) noexcept;
  // The published plan as `change` edits it (a host's automation), copied
  // and played while the published one is still borrowed: what the copy
  // shares (a sampler's map, a model) is never freed by the audio thread.
  using PlanChange = void (*)(cydaw::engine::PolySynthGraphPlan &plan,
                              const void *context);
  void renderChanged(cydaw::audio::SampleBuffer<float> &output,
                     const cydaw::midi::EventBuffer &midi, int startSample,
                     int numSamples, PlanChange change,
                     const void *context) noexcept;

  template <class Buffer, class Midi, class... Args>
    requires(!std::is_same_v<Buffer, cydaw::audio::SampleBuffer<float>> ||
             !std::is_same_v<Midi, cydaw::midi::EventBuffer>)
  void render(Buffer &output, const Midi &midi, Args &&...args) noexcept {
    cydaw::audio::SampleBuffer<float> view(output.getArrayOfWritePointers(),
                                           output.getNumChannels(),
                                           output.getNumSamples());
    foreignEvents_.clear();
    foreignEvents_.addEvents(midi, 0, -1, 0);
    render(view, foreignEvents_, std::forward<Args>(args)...);
  }
  template <class Buffer, class Midi, class... Args>
    requires(!std::is_same_v<Buffer, cydaw::audio::SampleBuffer<float>> ||
             !std::is_same_v<Midi, cydaw::midi::EventBuffer>)
  void renderWithPlan(Buffer &output, const Midi &midi,
                      Args &&...args) noexcept {
    cydaw::audio::SampleBuffer<float> view(output.getArrayOfWritePointers(),
                                           output.getNumChannels(),
                                           output.getNumSamples());
    foreignEvents_.clear();
    foreignEvents_.addEvents(midi, 0, -1, 0);
    renderWithPlan(view, foreignEvents_, std::forward<Args>(args)...);
  }
  template <class Buffer, class Midi, class... Args>
    requires(!std::is_same_v<Buffer, cydaw::audio::SampleBuffer<float>> ||
             !std::is_same_v<Midi, cydaw::midi::EventBuffer>)
  void renderChanged(Buffer &output, const Midi &midi,
                     Args &&...args) noexcept {
    cydaw::audio::SampleBuffer<float> view(output.getArrayOfWritePointers(),
                                           output.getNumChannels(),
                                           output.getNumSamples());
    foreignEvents_.clear();
    foreignEvents_.addEvents(midi, 0, -1, 0);
    renderChanged(view, foreignEvents_, std::forward<Args>(args)...);
  }

private:
  cydaw::midi::EventBuffer foreignEvents_;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace cydaw::app
