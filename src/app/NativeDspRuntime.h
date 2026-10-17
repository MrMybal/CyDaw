#pragma once
#include "../native_plugin/NativeRuntime.h"
#include <juce_audio_basics/juce_audio_basics.h>
namespace cydaw::app
{
// The DAW and native VST3 execute the same JUCE-free runtime. This adapter only
// borrows JUCE host buffers and translates diagnostics at the desktop boundary.
using cydaw::native::isControlOnlyNativeKind;
class NativeDspRuntime final : public cydaw::native::NativeDspRuntime
{
    using Base = cydaw::native::NativeDspRuntime;

  public:
    [[nodiscard]] juce::Result setGraph(const engine::NativeDspGraph &graph);
    [[nodiscard]] juce::Result updateParameters(const engine::NativeDspGraph &graph);
    [[nodiscard]] bool process(juce::AudioBuffer<float> &buffer, int start, int frames) noexcept;
    [[nodiscard]] bool process(juce::AudioBuffer<float> &buffer, int start, int frames, double seconds) noexcept;
    void setExtraInputs(const juce::AudioBuffer<float> *const *buses, std::size_t count) noexcept;

  private:
    std::array<cydaw::native::PlanarBuffer, maximumExtraInputs> extraViews_{};
};
[[nodiscard]] inline engine::NativeDspGraph makePassthroughAudioGraph()
{
    return cydaw::native::makePassthroughAudioGraph();
}
} // namespace cydaw::app
