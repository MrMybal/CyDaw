#include "NativeDspRuntime.h"
namespace cydaw::app
{
namespace
{
juce::Result desktopResult(const cydaw::native::Result &result)
{
    return result.failed() ? juce::Result::fail(juce::String::fromUTF8(result.error.c_str())) : juce::Result::ok();
}
} // namespace
juce::Result NativeDspRuntime::setGraph(const engine::NativeDspGraph &graph)
{
    return desktopResult(Base::setGraph(graph));
}
juce::Result NativeDspRuntime::updateParameters(const engine::NativeDspGraph &graph)
{
    return desktopResult(Base::updateParameters(graph));
}
bool NativeDspRuntime::process(juce::AudioBuffer<float> &buffer, int start, int frames) noexcept
{
    return process(buffer, start, frames, std::numeric_limits<double>::quiet_NaN());
}
bool NativeDspRuntime::process(juce::AudioBuffer<float> &buffer, int start, int frames, double seconds) noexcept
{
    cydaw::native::PlanarBuffer view{buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples()};
    return Base::process(view, start, frames, seconds);
}
void NativeDspRuntime::setExtraInputs(const juce::AudioBuffer<float> *const *buses, std::size_t count) noexcept
{
    std::array<const cydaw::native::PlanarBuffer *, maximumExtraInputs> views{};
    for (std::size_t i = 0; i < std::min(count, maximumExtraInputs); ++i)
        if (buses && buses[i])
        {
            // Borrow read-only host inputs; the runtime never writes to these views.
            extraViews_[i] = {const_cast<float *const *>(buses[i]->getArrayOfReadPointers()),
                              buses[i]->getNumChannels(), buses[i]->getNumSamples()};
            views[i] = &extraViews_[i];
        }
    Base::setExtraInputs(views.data(), views.size());
}
} // namespace cydaw::app
