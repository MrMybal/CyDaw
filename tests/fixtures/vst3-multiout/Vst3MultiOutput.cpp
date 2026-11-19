// Test instrument. Every held note plays three distinguishable sine waves.
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"

#include <array>
#include <bitset>
#include <cmath>
#include <numbers>

namespace {
using namespace Steinberg;
using namespace Steinberg::Vst;

class MultiOutputInstrument final : public SingleComponentEffect {
public:
    static FUnknown* create(void*) { return static_cast<IAudioProcessor*>(new MultiOutputInstrument); }

    tresult PLUGIN_API initialize(FUnknown* context) override
    {
        const auto result = SingleComponentEffect::initialize(context);
        if (result != kResultOk)
            return result;
        addEventInput(STR16("Keyboard"));
        addAudioOutput(STR16("Main"), SpeakerArr::kStereo);
        addAudioOutput(STR16("Aux Stereo"), SpeakerArr::kStereo, kAux, 0);
        addAudioOutput(STR16("Aux Mono"), SpeakerArr::kMono, kAux, 0);
        return kResultOk;
    }

    tresult PLUGIN_API setupProcessing(ProcessSetup& setup) override
    {
        sampleRate_ = setup.sampleRate;
        return SingleComponentEffect::setupProcessing(setup);
    }

    tresult PLUGIN_API setProcessing(TBool started) override
    {
        if (!started)
            held_.reset();
        return kResultOk;
    }

    tresult PLUGIN_API process(ProcessData& data) override
    {
        if (data.numSamples == 0)
            return kResultOk;
        if (data.symbolicSampleSize != kSample32 || data.numOutputs != 3 || data.outputs == nullptr)
            return kInvalidArgument;

        constexpr std::array<double, 3> frequencies { 375.0, 750.0, 1500.0 };
        constexpr std::array<float, 3> amplitudes { 0.25f, 0.10f, 0.05f };
        auto* events = data.inputEvents;
        const auto count = events == nullptr ? 0 : events->getEventCount();
        int32 eventIndex = 0;
        Event event {};
        const auto read = [&] { return eventIndex < count && events->getEvent(eventIndex, event) == kResultOk; };
        bool hasEvent = read();
        for (int32 frame = 0; frame < data.numSamples; ++frame) {
            while (hasEvent && event.sampleOffset <= frame) {
                if (event.type == Event::kNoteOnEvent && event.noteOn.pitch >= 0 && event.noteOn.pitch < 128)
                    held_.set(event.noteOn.pitch, event.noteOn.velocity > 0);
                else if (event.type == Event::kNoteOffEvent && event.noteOff.pitch >= 0 && event.noteOff.pitch < 128)
                    held_.reset(event.noteOff.pitch);
                ++eventIndex;
                hasEvent = read();
            }
            for (std::size_t bus = 0; bus < frequencies.size(); ++bus) {
                auto& output = data.outputs[bus];
                const auto sample = held_.none() ? 0.0f : amplitudes[bus] * static_cast<float>(std::sin(phase_[bus]));
                for (int32 channel = 0; channel < output.numChannels; ++channel)
                    output.channelBuffers32[channel][frame] = sample;
                phase_[bus] += 2 * std::numbers::pi * frequencies[bus] / sampleRate_;
                if (phase_[bus] >= 2 * std::numbers::pi)
                    phase_[bus] -= 2 * std::numbers::pi;
                output.silenceFlags = 0;
            }
        }
        return kResultOk;
    }

    tresult PLUGIN_API getState(IBStream* stream) override
    {
        int32 version = 1;
        return stream == nullptr ? kInvalidArgument : stream->write(&version, sizeof version);
    }

    tresult PLUGIN_API setState(IBStream* stream) override
    {
        int32 version {};
        return stream != nullptr && stream->read(&version, sizeof version) == kResultOk && version == 1
            ? kResultOk : kInvalidArgument;
    }

private:
    double sampleRate_ { 48000 };
    std::array<double, 3> phase_ {};
    std::bitset<128> held_;
};

const FUID instrumentId(0x6F37C3D5, 0xB2534B16, 0xA87345E2, 0xCDE1F951);
}

bool InitModule() { return true; }
bool DeinitModule() { return true; }

BEGIN_FACTORY_DEF("CyDaw Test", "", "")
DEF_CLASS2(INLINE_UID_FROM_FUID(instrumentId), Steinberg::PClassInfo::kManyInstances,
           kVstAudioEffectClass, "CyDaw Multi Output Test", 0, "Instrument|Synth",
           "1.0.0", kVstVersionString, MultiOutputInstrument::create)
END_FACTORY
