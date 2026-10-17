#pragma once
#include "NativeProcessor.h"
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
namespace cydaw::native
{
class Plugin final : public Steinberg::Vst::SingleComponentEffect
{
  public:
    Plugin();
    static Steinberg::FUnknown *create(void *)
    {
        return static_cast<Steinberg::Vst::IComponent *>(new Plugin);
    }
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown *) override;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup &) override;
    Steinberg::tresult PLUGIN_API setBusArrangements(Steinberg::Vst::SpeakerArrangement *, Steinberg::int32,
                                                     Steinberg::Vst::SpeakerArrangement *,
                                                     Steinberg::int32) override;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 s) override
    {
        return s == Steinberg::Vst::kSample32 ? Steinberg::kResultTrue : Steinberg::kResultFalse;
    }
    Steinberg::tresult PLUGIN_API setProcessing(Steinberg::TBool) override
    {
        processor_.reset();
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData &) override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream *) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream *) override;
    Steinberg::tresult PLUGIN_API setParamNormalized(Steinberg::Vst::ParamID,
                                                     Steinberg::Vst::ParamValue) override;
    Steinberg::Vst::ParamValue PLUGIN_API getParamNormalized(Steinberg::Vst::ParamID) override;
    Steinberg::uint32 PLUGIN_API getLatencySamples() override
    {
        return Steinberg::uint32(processor_.latency());
    }
    Steinberg::uint32 PLUGIN_API getTailSamples() override
    {
        return Steinberg::Vst::kInfiniteTail;
    }
    Steinberg::IPlugView *PLUGIN_API createView(Steinberg::FIDString) override;
    NativeProcessor &processor()
    {
        return processor_;
    }

  private:
    NativeProcessor processor_;
    std::array<engine::MidiEffectEvent, 4096> midi_{}, midiOut_{};
    std::array<NativeBlock::Bus, 16> buses_{};
};
} // namespace cydaw::native
