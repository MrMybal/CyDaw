#include <cydaw/base/Text.h>
#pragma once
#include "../native_plugin/NativeRuntime.h"
#include <limits>
namespace cydaw::app
{
// Desktop and exported plugins execute the same native runtime. Buffers are
// borrowed structurally: no GUI/audio framework ABI is required here.
using cydaw::native::isControlOnlyNativeKind;
class NativeDspRuntime final : public cydaw::native::NativeDspRuntime
{
    using Base = cydaw::native::NativeDspRuntime;

  public:
    [[nodiscard]] cydaw::base::Result setGraph(const engine::NativeDspGraph &graph);
    [[nodiscard]] cydaw::base::Result updateParameters(const engine::NativeDspGraph &graph);
    template<class Buffer>[[nodiscard]] bool process(Buffer& buffer,int start,int frames) noexcept {return process(buffer,start,frames,std::numeric_limits<double>::quiet_NaN());}
    template<class Buffer>[[nodiscard]] bool process(Buffer& buffer,int start,int frames,double seconds) noexcept {
        cydaw::native::PlanarBuffer view{buffer.getArrayOfWritePointers(),buffer.getNumChannels(),buffer.getNumSamples()};return Base::process(view,start,frames,seconds);
    }
    template<class Buffer>void setExtraInputs(const Buffer*const* buses,std::size_t count) noexcept {
        std::array<const cydaw::native::PlanarBuffer*,maximumExtraInputs> views{};
        for(std::size_t i=0;i<std::min(count,maximumExtraInputs);++i)if(buses&&buses[i]){extraViews_[i]={const_cast<float*const*>(buses[i]->getArrayOfReadPointers()),buses[i]->getNumChannels(),buses[i]->getNumSamples()};views[i]=&extraViews_[i];}Base::setExtraInputs(views.data(),views.size());
    }
    void setExtraInputs(std::nullptr_t,std::size_t) noexcept {Base::setExtraInputs(nullptr,0);}

  private:
    std::array<cydaw::native::PlanarBuffer, maximumExtraInputs> extraViews_{};
};
[[nodiscard]] inline engine::NativeDspGraph makePassthroughAudioGraph()
{
    return cydaw::native::makePassthroughAudioGraph();
}
} // namespace cydaw::app
