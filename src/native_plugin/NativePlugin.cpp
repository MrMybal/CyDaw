#include "NativePlugin.h"
#include "base/source/fstring.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstpresetfile.h"
#include <cmath>
#include <cstring>
namespace cydaw::native
{
using namespace Steinberg;
using namespace Steinberg::Vst;
static String wide(const std::string &text)
{
    String s(text.c_str());
    s.toWideString(kCP_Utf8);
    return s;
}
Plugin::Plugin()
{
    processContextRequirements.needTempo().needProjectTimeMusic().needTransportState();
    processor_.edited = [this](uint32_t id, double v)
    {
        const auto &p = processor_.definition().parameters[size_t(processor_.parameterIndex(id))];
        double n = p.maximum > p.minimum ? (v - p.minimum) / (p.maximum - p.minimum) : 0;
        beginEdit(id);
        SingleComponentEffect::setParamNormalized(id, n);
        performEdit(id, n);
        endEdit(id);
    };
}
tresult PLUGIN_API Plugin::initialize(FUnknown *context)
{
    auto result = SingleComponentEffect::initialize(context);
    if (result != kResultOk)
        return result;
    for (const auto &b : processor_.definition().buses)
    {
        auto layout = b.channels == 1   ? SpeakerArr::kMono
                      : b.channels == 2 ? SpeakerArr::kStereo
                                        : (SpeakerArrangement(1) << b.channels) - 1;
        if (b.input)
            addAudioInput(wide(b.name).text16(), layout, audioInputs.size() ? kAux : kMain);
        else
            addAudioOutput(wide(b.name).text16(), layout);
    }
    if (processor_.definition().acceptsMidi)
    {
        addEventInput(STR16("MIDI"));
        for (int channel = 0; channel < 16; ++channel)
            for (int controller = 0; controller < midiControllers; ++controller)
            {
                const auto id = midiParameterBase + ParamID(channel * midiControllers + controller);
                if (processor_.parameterIndex(id) >= 0)
                    continue;
                auto *parameter = new Parameter(STR16("MIDI"), id, nullptr, 0.0, 0, ParameterInfo::kIsHidden);
                parameters.addParameter(parameter);
            }
    }
    if (processor_.definition().producesMidi)
        addEventOutput(STR16("MIDI"));
    for (const auto &p : processor_.definition().parameters)
        if (p.host)
        {
            parameters.addParameter(new RangeParameter(wide(p.name).text16(), p.id, wide(p.unit).text16(),
                                                       p.minimum, p.maximum, p.initial, 0,
                                                       ParameterInfo::kCanAutomate));
            SingleComponentEffect::setParamNormalized(p.id, getParamNormalized(p.id));
        }
    if (!processor_.definition().programs.empty())
    {
        addUnit(new Unit(STR16("Root"), kRootUnitId, kNoParentUnitId, NativeProcessor::programId));
        auto *list = new ProgramList(STR16("Presets"), NativeProcessor::programId, kRootUnitId);
        for (const auto &p : processor_.definition().programs)
            list->addProgram(wide(p.name).text16());
        addProgramList(list);
        parameters.addParameter(list->getParameter());
    }
    return kResultOk;
}
tresult PLUGIN_API Plugin::getMidiControllerAssignment(int32 busIndex, int16 channel, CtrlNumber controller, ParamID &id)
{
    if (busIndex != 0 || channel < 0 || channel > 15 || controller < 0 || controller >= midiControllers ||
        !processor_.definition().acceptsMidi)
        return kResultFalse;
    id = midiParameterBase + ParamID(channel * midiControllers + controller);
    return processor_.parameterIndex(id) >= 0 ? kResultFalse : kResultTrue;
}
tresult PLUGIN_API Plugin::setParamNormalized(ParamID id, ParamValue v)
{
    if (!std::isfinite(v))
        return kInvalidArgument;
    if (id >= midiParameterBase && id < midiParameterBase + 16u * midiControllers && processor_.parameterIndex(id) < 0)
        return SingleComponentEffect::setParamNormalized(id, v);
    if (id == NativeProcessor::programId && !processor_.definition().programs.empty())
    {
        processor_.applyProgram(
            int(std::round(std::clamp(v, 0.0, 1.0) * (processor_.definition().programs.size() - 1))));
        if (componentHandler)
            componentHandler->restartComponent(kParamValuesChanged);
    }
    else
    {
        int i = processor_.parameterIndex(id);
        if (i < 0)
            return kInvalidArgument;
        const auto &p = processor_.definition().parameters[size_t(i)];
        processor_.setParameter(id, p.minimum + std::clamp(v, 0.0, 1.0) * (p.maximum - p.minimum));
    }
    return SingleComponentEffect::setParamNormalized(id, v);
}
ParamValue PLUGIN_API Plugin::getParamNormalized(ParamID id)
{
    if (id == NativeProcessor::programId)
        return processor_.definition().programs.size() > 1
                   ? double(processor_.program()) / (processor_.definition().programs.size() - 1)
                   : 0;
    int i = processor_.parameterIndex(id);
    if (i < 0)
        return 0;
    const auto &p = processor_.definition().parameters[size_t(i)];
    return p.maximum > p.minimum
               ? std::clamp((processor_.parameterValue(id) - p.minimum) / (p.maximum - p.minimum), 0.0, 1.0)
               : 0;
}
tresult PLUGIN_API Plugin::setBusArrangements(SpeakerArrangement *in, int32 ni, SpeakerArrangement *out,
                                              int32 no)
{
    if (ni != audioInputs.size() || no != audioOutputs.size())
        return kResultFalse;
    for (int i = 0; i < ni; ++i)
    {
        SpeakerArrangement a;
        getBusArrangement(kInput, i, a);
        if (in[i] != a)
            return kResultFalse;
    }
    for (int i = 0; i < no; ++i)
    {
        SpeakerArrangement a;
        getBusArrangement(kOutput, i, a);
        if (out[i] != a)
            return kResultFalse;
    }
    return kResultOk;
}
tresult PLUGIN_API Plugin::setupProcessing(ProcessSetup &setup)
{
    if (setup.symbolicSampleSize != kSample32 ||
        !processor_.prepare(setup.sampleRate, setup.maxSamplesPerBlock))
        return kResultFalse;
    return SingleComponentEffect::setupProcessing(setup);
}
tresult PLUGIN_API Plugin::process(ProcessData &data)
{
    size_t count = 0;
    // A controller as a MIDI event.
    const auto controllerEvent = [&](const int channel, const int controller, const double value, const int32 at) {
        if (count >= midi_.size())
            return;
        engine::MidiEffectEvent m{};
        m.offset = uint32_t(std::clamp<int32>(at, 0, std::max(0, data.numSamples - 1)));
        const auto v = std::clamp(value, 0.0, 1.0);
        if (controller == kPitchBend)
        {
            const auto bend = int(std::lround(v * 16383.0));
            m.status = uint8_t(0xe0 | channel);
            m.data1 = uint8_t(bend & 127);
            m.data2 = uint8_t((bend >> 7) & 127);
        }
        else if (controller == kAfterTouch)
        {
            m.status = uint8_t(0xd0 | channel);
            m.data1 = uint8_t(std::lround(v * 127.0));
        }
        else if (controller >= 0 && controller < 128)
        {
            m.status = uint8_t(0xb0 | channel);
            m.data1 = uint8_t(controller);
            m.data2 = uint8_t(std::lround(v * 127.0));
        }
        else
            return;
        midi_[count++] = m;
    };
    if (data.inputParameterChanges)
        for (int q = 0; q < data.inputParameterChanges->getParameterCount(); ++q)
        {
            auto *queue = data.inputParameterChanges->getParameterData(q);
            int32 offset;
            ParamValue value;
            // A mapped controller: each of its points, where it is.
            if (queue && queue->getParameterId() >= midiParameterBase &&
                queue->getParameterId() < midiParameterBase + 16u * midiControllers &&
                processor_.parameterIndex(queue->getParameterId()) < 0)
            {
                const auto index = int(queue->getParameterId() - midiParameterBase);
                for (int32 point = 0; point < queue->getPointCount(); ++point)
                    if (queue->getPoint(point, offset, value) == kResultTrue && std::isfinite(value))
                        controllerEvent(index / midiControllers, index % midiControllers, value, offset);
                continue;
            }
            if (queue && queue->getPointCount() > 0 &&
                queue->getPoint(queue->getPointCount() - 1, offset, value) == kResultTrue)
            {
                if (!std::isfinite(value))
                    continue;
                const auto id = queue->getParameterId();
                if (id == NativeProcessor::programId && !processor_.definition().programs.empty())
                    processor_.applyProgram(int(std::round(std::clamp(value, 0.0, 1.0) *
                                                           (processor_.definition().programs.size() - 1))));
                else if (const auto index = processor_.parameterIndex(id); index >= 0)
                {
                    const auto &parameter = processor_.definition().parameters[size_t(index)];
                    processor_.setParameter(id,
                                            parameter.minimum + std::clamp(value, 0.0, 1.0) *
                                                                    (parameter.maximum - parameter.minimum));
                }
            }
        }
    if (!data.numSamples)
        return kResultOk;
    if (data.numOutputs != 1 || data.numInputs > int(buses_.size()) || !data.outputs[0].channelBuffers32)
        return kResultFalse;
    NativeBlock block;
    for (int i = 0; i < data.numInputs; ++i)
        buses_[i] = {data.inputs[i].channelBuffers32, data.inputs[i].numChannels};
    block.inputs = {buses_.data(), size_t(data.numInputs)};
    block.output = data.outputs[0].channelBuffers32;
    block.channels = data.outputs[0].numChannels;
    block.frames = data.numSamples;
    if (data.inputEvents)
        for (int i = 0; i < data.inputEvents->getEventCount() && count < midi_.size(); ++i)
        {
            Event e{};
            if (data.inputEvents->getEvent(i, e) != kResultOk)
                continue;
            engine::MidiEffectEvent m{};
            m.offset = uint32_t(std::clamp(e.sampleOffset, 0, data.numSamples - 1));
            if (e.type == Event::kNoteOnEvent)
            {
                m.status = uint8_t(0x90 | std::clamp<int>(e.noteOn.channel, 0, 15));
                m.data1 = uint8_t(std::clamp<int>(e.noteOn.pitch, 0, 127));
                m.data2 = uint8_t(std::clamp<int>(int(std::round(e.noteOn.velocity * 127)), 0, 127));
            }
            else if (e.type == Event::kNoteOffEvent)
            {
                m.status = uint8_t(0x80 | std::clamp<int>(e.noteOff.channel, 0, 15));
                m.data1 = uint8_t(std::clamp<int>(e.noteOff.pitch, 0, 127));
            }
            else if (e.type == Event::kLegacyMIDICCOutEvent)
            {
                // A host that sends them anyway: pitch bend and aftertouch
                // have numbers of their own past 127.
                const auto channel = std::clamp<int>(e.midiCCOut.channel, 0, 15);
                if (e.midiCCOut.controlNumber == kPitchBend)
                {
                    m.status = uint8_t(0xe0 | channel);
                    m.data1 = uint8_t(e.midiCCOut.value & 127);
                    m.data2 = uint8_t(e.midiCCOut.value2 & 127);
                }
                else if (e.midiCCOut.controlNumber == kAfterTouch)
                {
                    m.status = uint8_t(0xd0 | channel);
                    m.data1 = uint8_t(e.midiCCOut.value & 127);
                }
                else if (e.midiCCOut.controlNumber < 128)
                {
                    m.status = uint8_t(0xb0 | channel);
                    m.data1 = e.midiCCOut.controlNumber;
                    m.data2 = uint8_t(e.midiCCOut.value & 127);
                }
                else
                    continue;
            }
            else
                continue;
            midi_[count++] = m;
        }
    block.midi = {midi_.data(), count};
    block.midiOutput = midiOut_.data();
    block.midiCapacity = midiOut_.size();
    block.clock.sampleRate = processSetup.sampleRate;
    if (data.processContext)
    {
        auto &c = *data.processContext;
        if ((c.state & ProcessContext::kTempoValid) && std::isfinite(c.tempo) && c.tempo > 0)
            block.clock.beatsPerMinute = c.tempo;
        block.clock.playing = bool(c.state & ProcessContext::kPlaying);
        block.clock.quarterNotes =
            (c.state & ProcessContext::kProjectTimeMusicValid)
                ? c.projectTimeMusic
                : c.projectTimeSamples / processSetup.sampleRate * block.clock.beatsPerMinute / 60;
        block.seconds = c.projectTimeSamples / processSetup.sampleRate;
    }
    if (!processor_.process(block))
        return kResultFalse;
    if (data.outputEvents)
        for (size_t i = 0; i < block.midiCount; ++i)
        {
            auto m = midiOut_[i];
            Event e{};
            e.busIndex = 0;
            e.sampleOffset = int(m.offset);
            if (m.isNoteOn())
            {
                e.type = Event::kNoteOnEvent;
                e.noteOn.channel = m.channel();
                e.noteOn.pitch = m.data1;
                e.noteOn.velocity = m.data2 / 127.f;
                e.noteOn.noteId = -1;
            }
            else if (m.isNoteOff())
            {
                e.type = Event::kNoteOffEvent;
                e.noteOff.channel = m.channel();
                e.noteOff.pitch = m.data1;
                e.noteOff.noteId = -1;
            }
            else
                continue;
            data.outputEvents->addEvent(e);
        }
    data.outputs[0].silenceFlags = 0;
    return kResultOk;
}
tresult PLUGIN_API Plugin::getState(IBStream *stream)
{
    if (!stream)
        return kInvalidArgument;
    auto bytes = processor_.save();
    int32 wrote{};
    return stream->write(bytes.data(), int32(bytes.size()), &wrote) == kResultOk &&
                   wrote == int32(bytes.size())
               ? kResultOk
               : kResultFalse;
}
tresult PLUGIN_API Plugin::setState(IBStream *stream)
{
    if (!stream)
        return kInvalidArgument;
    std::vector<uint8_t> bytes;
    std::array<uint8_t, 4096> buffer;
    while (bytes.size() < 1024 * 1024)
    {
        int32 got{};
        stream->read(buffer.data(), int32(buffer.size()), &got);
        if (got <= 0)
            break;
        bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + got);
    }
    if (!processor_.load(bytes))
        return kResultFalse;
    for (const auto &p : processor_.definition().parameters)
        if (p.host)
            SingleComponentEffect::setParamNormalized(p.id, getParamNormalized(p.id));
    if (componentHandler)
        componentHandler->restartComponent(kParamValuesChanged);
    return kResultOk;
}
} // namespace cydaw::native
bool InitModule() { return true; }
bool DeinitModule() { return true; }
static const Steinberg::FUID cydawPluginUID(0x43594457, CYDAW_VENDOR_ID, CYDAW_PRODUCT_ID, 0x4e415431);
BEGIN_FACTORY_DEF(CYDAW_VENDOR, "", "")
DEF_CLASS2(INLINE_UID_FROM_FUID(cydawPluginUID), Steinberg::PClassInfo::kManyInstances, kVstAudioEffectClass,
           CYDAW_PRODUCT, 0, CYDAW_CATEGORY, CYDAW_VERSION, kVstVersionString, cydaw::native::Plugin::create)
END_FACTORY
