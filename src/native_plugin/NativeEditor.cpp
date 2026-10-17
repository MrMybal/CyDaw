#include "NativePlugin.h"
#include "public.sdk/source/common/pluginview.h"
#include <cstring>
namespace cydaw::native
{
using namespace Steinberg;
class VstEditor final : public CPluginView
{
    Plugin &plugin_;
    std::unique_ptr<EditorSurface> surface_;
#if defined(__linux__)
    Linux::IRunLoop* loop_{};
    struct Timer final : Linux::ITimerHandler {
        VstEditor& owner;
        explicit Timer(VstEditor& o):owner(o){}
        tresult PLUGIN_API queryInterface(const TUID id,void** obj) override {
            if(!obj)return kInvalidArgument;
            *obj=nullptr;
            if(!std::memcmp(id,Linux::ITimerHandler::iid.toTUID(),16)||!std::memcmp(id,FUnknown::iid.toTUID(),16)) {*obj=this;addRef();return kResultOk;}
            return kNoInterface;
        }
        uint32 PLUGIN_API addRef() override{return owner.addRef();}
        uint32 PLUGIN_API release() override{return owner.release();}
        void PLUGIN_API onTimer() override {if(owner.surface_)owner.surface_->idle();}
    } timer_{*this};
    void stopTimer() {if(loop_){loop_->unregisterTimer(&timer_);loop_->release();loop_=nullptr;}}
#endif

  public:
    explicit VstEditor(Plugin &p) : plugin_(p)
    {
        plugin_.addRef();
        rect = ViewRect(0, 0, p.processor().definition().width, p.processor().definition().height);
    }
    ~VstEditor() override
    {
#if defined(__linux__)
        stopTimer();
#endif
        surface_.reset();
        plugin_.release();
    }
    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override
    {
#if defined(_WIN32)
        const auto platform=kPlatformTypeHWND;
#else
        const auto platform=kPlatformTypeX11EmbedWindowID;
#endif
        return type && !std::strcmp(type, platform) ? kResultTrue : kResultFalse;
    }
    tresult PLUGIN_API attached(void *parent, FIDString type) override
    {
        if (!parent || surface_ || isPlatformTypeSupported(type) != kResultTrue)
            return kResultFalse;
#if defined(__linux__)
        if(!plugFrame||plugFrame->queryInterface(Linux::IRunLoop::iid.toTUID(),reinterpret_cast<void**>(&loop_))!=kResultOk)
            return kResultFalse;
        if(loop_->registerTimer(&timer_,33)!=kResultOk) {loop_->release();loop_=nullptr;return kResultFalse;}
#endif
        surface_ = makeEditor(plugin_.processor());
        if (!surface_->attach(parent))
        {
#if defined(__linux__)
            stopTimer();
#endif
            surface_.reset();
            return kResultFalse;
        }
        return CPluginView::attached(parent, type);
    }
    tresult PLUGIN_API removed() override
    {
#if defined(__linux__)
        stopTimer();
#endif
        surface_.reset();
        return CPluginView::removed();
    }
};
IPlugView *PLUGIN_API Plugin::createView(FIDString name)
{
    return name && !std::strcmp(name, Vst::ViewType::kEditor) ? new VstEditor(*this) : nullptr;
}
} // namespace cydaw::native
