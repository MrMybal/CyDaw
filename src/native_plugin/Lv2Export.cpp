#include "NativeProcessor.h"
#include <array>
#if defined(_WIN32)
#define CYDAW_EXPORT __declspec(dllexport)
#else
#define CYDAW_EXPORT __attribute__((visibility("default")))
#endif
#include <cstring>
#include <lv2/atom/atom.h>
#include <lv2/atom/util.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/core/lv2.h>
#include <lv2/instance-access/instance-access.h>
#include <lv2/midi/midi.h>
#include <lv2/options/options.h>
#include <lv2/state/state.h>
#include <lv2/time/time.h>
#include <lv2/ui/ui.h>
#include <lv2/urid/urid.h>
namespace cydaw::native
{
struct Lv2Export
{
    NativeProcessor processor;
    struct Port
    {
        void *data{};
        int direction{}, bus{}, channel{};
        const ParameterDefinition *parameter{};
        float previous{std::numeric_limits<float>::quiet_NaN()};
    };
    std::vector<Port> ports;
    std::array<std::array<float *, 8>, 16> audio{};
    std::array<NativeBlock::Bus, 16> inputs{};
    std::array<engine::MidiEffectEvent, 4096> midi{}, midiOutput{};
    const LV2_URID_Map *map{};
    LV2_URID sequence{}, midiType{}, chunk{}, stateKey{}, object{}, position{}, tempo{}, speed{}, frame{},
        beat{}, floating{}, doubling{}, longing{};
    int midiIn{-1}, midiOut{-1}, latencyPort{-1}, outputBus{}, inputCount{};
    // The longest block the processor was prepared for.
    int maxBlock{8192};
    double rate{}, seconds{}, bpm{120}, quarter{};
    bool playing{};
    explicit Lv2Export(double sampleRate, const LV2_Feature *const *features) : rate(sampleRate)
    {
        if (features)
            for (auto f = features; *f; ++f)
                if (!std::strcmp((*f)->URI, LV2_URID__map))
                    map = static_cast<const LV2_URID_Map *>((*f)->data);
        if (!map)
            throw std::runtime_error("LV2 urid:map is required");
        auto id = [&](const char *uri) { return map->map(map->handle, uri); };
        sequence = id(LV2_ATOM__Sequence);
        midiType = id(LV2_MIDI__MidiEvent);
        chunk = id(LV2_ATOM__Chunk);
        stateKey = id(CYDAW_PLUGIN_URI "#state");
        object = id(LV2_ATOM__Object);
        position = id(LV2_TIME__Position);
        tempo = id(LV2_TIME__beatsPerMinute);
        speed = id(LV2_TIME__speed);
        frame = id(LV2_TIME__frame);
        beat = id(LV2_TIME__beat);
        floating = id(LV2_ATOM__Float);
        doubling = id(LV2_ATOM__Double);
        longing = id(LV2_ATOM__Long);
        if (features)
            for (auto f = features; *f; ++f)
                if (!std::strcmp((*f)->URI, LV2_OPTIONS__options))
                    for (auto *opt = static_cast<const LV2_Options_Option *>((*f)->data); opt && opt->key;
                         ++opt)
                        if (opt->key == id(LV2_BUF_SIZE__maxBlockLength) && opt->size == 4 && opt->value)
                            maxBlock = std::clamp(*static_cast<const int *>(opt->value), 1, 1048576);
        int bus = 0;
        for (const auto &b : processor.definition().buses)
        {
            if (bus >= 16 || b.channels > 8)
                throw std::runtime_error("LV2 audio bus limit");
            for (int c = 0; c < b.channels; ++c)
                ports.push_back({nullptr, b.input ? 0 : 1, bus, c});
            if (b.input)
                inputs[inputCount++] = {audio[bus].data(), b.channels};
            else
                outputBus = bus;
            ++bus;
        }
        for (const auto &p : processor.definition().parameters)
            if (p.host)
                ports.push_back({nullptr, 2, 0, 0, &p});
        // Every device receives host transport; instruments also consume MIDI here.
        midiIn = int(ports.size());
        ports.push_back({nullptr, 4});
        if (processor.definition().producesMidi)
        {
            midiOut = int(ports.size());
            ports.push_back({nullptr, 5});
        }
        latencyPort = int(ports.size());
        ports.push_back({nullptr, 6});
        if (!processor.prepare(rate, maxBlock))
            throw std::runtime_error("LV2 DSP preparation failed");
    }
    void run(uint32_t frames)
    {
        for (auto &p : ports)
            if (p.direction <= 1)
                audio[p.bus][p.channel] = static_cast<float *>(p.data);
        for (auto &p : ports)
            if (p.parameter && p.data)
            {
                float v = *static_cast<float *>(p.data);
                if (std::isfinite(v) && v != p.previous)
                {
                    processor.setParameter(p.parameter->id, v);
                    p.previous = v;
                }
            }
        size_t count = 0;
        if (midiIn >= 0 && ports[midiIn].data)
        {
            const auto *seq = static_cast<const LV2_Atom_Sequence *>(ports[midiIn].data);
            if (seq->atom.type == sequence && seq->atom.size >= sizeof(LV2_Atom_Sequence_Body) &&
                seq->atom.size <= 1048576)
            {
                auto *cursor = reinterpret_cast<const uint8_t *>(&seq->body + 1);
                auto *end = reinterpret_cast<const uint8_t *>(&seq->body) + seq->atom.size;
                while (cursor + sizeof(LV2_Atom_Event) <= end)
                {
                    auto *e = reinterpret_cast<const LV2_Atom_Event *>(cursor);
                    size_t size = sizeof(LV2_Atom_Event) + lv2_atom_pad_size(e->body.size);
                    if (size > size_t(end - cursor))
                        break;
                    if (e->body.type == midiType && e->body.size >= 1 && e->body.size <= 3 &&
                        count < midi.size())
                    {
                        auto *bytes = reinterpret_cast<const uint8_t *>(&e->body + 1);
                        engine::MidiEffectEvent m{};
                        m.offset = uint32_t(std::clamp<int64_t>(e->time.frames, 0, frames ? frames - 1 : 0));
                        m.status = bytes[0];
                        m.data1 = e->body.size > 1 ? bytes[1] : 0;
                        m.data2 = e->body.size > 2 ? bytes[2] : 0;
                        midi[count++] = m;
                    }
                    else if (e->body.type == object && e->body.size >= sizeof(LV2_Atom_Object_Body))
                    {
                        auto *o = reinterpret_cast<const LV2_Atom_Object *>(&e->body);
                        if (o->body.otype == position)
                        {
                            auto *p = reinterpret_cast<const uint8_t *>(&o->body + 1);
                            auto *stop = reinterpret_cast<const uint8_t *>(&o->body) + o->atom.size;
                            while (p + sizeof(LV2_Atom_Property_Body) <= stop)
                            {
                                auto *property = reinterpret_cast<const LV2_Atom_Property_Body *>(p);
                                size_t n =
                                    sizeof(LV2_Atom_Property_Body) + lv2_atom_pad_size(property->value.size);
                                if (n > size_t(stop - p))
                                    break;
                                double v = 0;
                                bool valid = true;
                                auto *value = &property->value + 1;
                                if (property->value.type == floating && property->value.size == 4)
                                    v = *reinterpret_cast<const float *>(value);
                                else if (property->value.type == doubling && property->value.size == 8)
                                    v = *reinterpret_cast<const double *>(value);
                                else if (property->value.type == longing && property->value.size == 8)
                                    v = double(*reinterpret_cast<const int64_t *>(value));
                                else
                                    valid = false;
                                if (valid && std::isfinite(v))
                                {
                                    if (property->key == tempo && v > 0)
                                        bpm = v;
                                    else if (property->key == speed)
                                        playing = v != 0;
                                    else if (property->key == frame)
                                        seconds = v / rate;
                                    else if (property->key == beat)
                                        quarter = v;
                                }
                                p += n;
                            }
                        }
                    }
                    cursor += size;
                }
            }
        }
        auto outputChannels = 0;
        for (const auto &b : processor.definition().buses)
            if (!b.input)
                outputChannels = b.channels;
        bool connected = true;
        for (int c = 0; c < outputChannels; ++c)
            connected &= audio[outputBus][c] != nullptr;
        // A host may run blocks longer than the one it announced (the plug-in
        // does not require it to keep to it): they play in pieces.
        std::array<std::array<float *, 8>, 16> pieceChannels{};
        std::array<NativeBlock::Bus, 16> pieceInputs{};
        std::array<float *, 8> pieceOutput{};
        size_t midiFrom = 0, midiOutCount = 0;
        for (uint32_t done = 0; done < frames || (frames == 0 && done == 0);)
        {
            const auto length = std::min<uint32_t>(frames - done, uint32_t(maxBlock));
            for (int i = 0; i < inputCount; ++i)
            {
                for (int c = 0; c < inputs[i].count; ++c)
                    pieceChannels[i][c] = inputs[i].channels[c] != nullptr ? inputs[i].channels[c] + done : nullptr;
                pieceInputs[i] = {pieceChannels[i].data(), inputs[i].count};
            }
            for (int c = 0; c < outputChannels; ++c)
                pieceOutput[c] = audio[outputBus][c] != nullptr ? audio[outputBus][c] + done : nullptr;
            // The sequence is in time order: this piece's events, from its start.
            auto midiTo = midiFrom;
            while (midiTo < count && (midi[midiTo].offset < done + length || done + length >= frames))
                midi[midiTo++].offset -= done;
            NativeBlock block;
            block.inputs = {pieceInputs.data(), size_t(inputCount)};
            block.output = pieceOutput.data();
            block.channels = outputChannels;
            block.frames = int(length);
            block.midi = {midi.data() + midiFrom, midiTo - midiFrom};
            block.midiOutput = midiOutput.data() + midiOutCount;
            block.midiCapacity = midiOutput.size() - midiOutCount;
            block.clock.sampleRate = rate;
            block.clock.beatsPerMinute = bpm;
            block.clock.playing = playing;
            block.clock.quarterNotes = quarter + done / rate * bpm / 60;
            block.seconds = seconds + done / rate;
            if (connected && !processor.process(block))
                for (int c = 0; c < block.channels; ++c)
                    std::fill_n(block.output[c], length, 0.f);
            for (size_t i = 0; i < block.midiCount; ++i)
                block.midiOutput[i].offset += done;
            midiOutCount += block.midiCount;
            midiFrom = midiTo;
            done += length;
            if (length == 0)
                break;
        }
        if (midiOut >= 0 && ports[midiOut].data)
        {
            auto *seq = static_cast<LV2_Atom_Sequence *>(ports[midiOut].data);
            auto capacity = seq->atom.size;
            seq->atom.type = sequence;
            seq->atom.size = sizeof(seq->body);
            seq->body = {};
            for (size_t i = 0; i < midiOutCount; ++i)
            {
                auto m = midiOutput[i];
                alignas(8) uint8_t bytes[32]{};
                auto *event = reinterpret_cast<LV2_Atom_Event *>(bytes);
                event->time.frames = m.offset;
                event->body.type = midiType;
                event->body.size = 3;
                auto *data = reinterpret_cast<uint8_t *>(&event->body + 1);
                data[0] = m.status;
                data[1] = m.data1;
                data[2] = m.data2;
                lv2_atom_sequence_append_event(seq, capacity, event);
            }
        }
        if (ports[latencyPort].data)
            *static_cast<float *>(ports[latencyPort].data) = float(processor.latency());
        seconds += frames / rate;
        quarter += frames / rate * bpm / 60;
    }
};
static const LV2_State_Interface state{
    [](LV2_Handle handle, LV2_State_Store_Function store, LV2_State_Handle user, uint32_t,
       const LV2_Feature *const *)
    {
        auto &p = *static_cast<Lv2Export *>(handle);
        auto bytes = p.processor.save();
        return store(user, p.stateKey, bytes.data(), bytes.size(), p.chunk, LV2_STATE_IS_POD);
    },
    [](LV2_Handle handle, LV2_State_Retrieve_Function retrieve, LV2_State_Handle user, uint32_t,
       const LV2_Feature *const *)
    {
        auto &p = *static_cast<Lv2Export *>(handle);
        size_t size{};
        uint32_t type{}, flags{};
        auto *data = static_cast<const uint8_t *>(retrieve(user, p.stateKey, &size, &type, &flags));
        if (!data)
            return LV2_STATE_SUCCESS;
        if (type != p.chunk || size > 1024 * 1024 || !p.processor.load({data, size}))
            return LV2_STATE_ERR_BAD_TYPE;
        for (auto &port : p.ports)
            if (port.parameter && port.data)
                port.previous = *static_cast<float *>(port.data);
        return LV2_STATE_SUCCESS;
    }};
static const LV2_Descriptor descriptor{
    CYDAW_PLUGIN_URI,
    [](const LV2_Descriptor *, double rate, const char *, const LV2_Feature *const *features) -> LV2_Handle
    {
        try
        {
            return new Lv2Export(rate, features);
        }
        catch (...)
        {
            return nullptr;
        }
    },
    [](LV2_Handle handle, uint32_t index, void *data)
    {
        auto &p = *static_cast<Lv2Export *>(handle);
        if (index < p.ports.size())
            p.ports[index].data = data;
    },
    [](LV2_Handle h) { static_cast<Lv2Export *>(h)->processor.reset(); },
    [](LV2_Handle h, uint32_t n) { static_cast<Lv2Export *>(h)->run(n); },
    [](LV2_Handle) {},
    [](LV2_Handle h) { delete static_cast<Lv2Export *>(h); },
    [](const char *uri) -> const void *
    { return !std::strcmp(uri, LV2_STATE__interface) ? &state : nullptr; }};
struct Lv2Ui final : EditorModel
{
    Definition d = makeDefinition();
    std::vector<double> values;
    std::vector<uint32_t> indices;
    std::unique_ptr<EditorSurface> surface;
    LV2UI_Write_Function write{};
    LV2UI_Controller controller{};
    Lv2Export *instance{};
    Lv2Ui()
    {
        uint32_t port = 0;
        for (const auto &b : d.buses)
            port += uint32_t(b.channels);
        for (const auto &p : d.parameters)
        {
            values.push_back(p.initial);
            indices.push_back(p.host ? port++ : UINT32_MAX);
        }
    }
    const Definition &definition() const override { return d; }
    double value(const char *key) const override
    {
        for (size_t i = 0; i < d.parameters.size(); ++i)
            if (d.parameters[i].key == key)
                return instance ? instance->processor.parameterValue(d.parameters[i].id) : values[i];
        return 0;
    }
    void edit(const char *key, double value) override
    {
        for (size_t i = 0; i < d.parameters.size(); ++i)
            if (d.parameters[i].key == key)
            {
                values[i] = std::clamp(value, d.parameters[i].minimum, d.parameters[i].maximum);
                if (instance)
                    instance->processor.setParameter(d.parameters[i].id, values[i]);
                if (indices[i] != UINT32_MAX)
                {
                    float v = float(values[i]);
                    write(controller, indices[i], sizeof(v), 0, &v);
                }
                return;
            }
    }
    bool analysis(uint64_t node, bool input, engine::AudioAnalysisFrame &frame) const override
    {
        return instance && instance->processor.analysis(node, input, frame);
    }
};
static const LV2UI_Descriptor uiDescriptor{
    CYDAW_PLUGIN_URI "#ui",
    [](const LV2UI_Descriptor *, const char *uri, const char *, LV2UI_Write_Function write,
       LV2UI_Controller controller, LV2UI_Widget *widget, const LV2_Feature *const *features) -> LV2UI_Handle
    {
        if (std::strcmp(uri, CYDAW_PLUGIN_URI))
            return nullptr;
        auto p = std::make_unique<Lv2Ui>();
        p->write = write;
        p->controller = controller;
        void *parent{};
        if (features)
            for (auto f = features; *f; ++f)
            {
                if (!std::strcmp((*f)->URI, LV2_UI__parent))
                    parent = (*f)->data;
                if (!std::strcmp((*f)->URI, LV2_INSTANCE_ACCESS_URI))
                    p->instance = static_cast<Lv2Export *>((*f)->data);
            }
        if (!parent || !write)
            return nullptr;
        p->surface = makeEditor(*p);
        if (!p->surface->attach(parent))
            return nullptr;
        *widget = p->surface->handle();
        return p.release();
    },
    [](LV2UI_Handle h) { delete static_cast<Lv2Ui *>(h); },
    [](LV2UI_Handle h, uint32_t port, uint32_t bytes, uint32_t protocol, const void *data)
    {
        if (protocol || bytes != 4 || !data)
            return;
        auto &p = *static_cast<Lv2Ui *>(h);
        for (size_t i = 0; i < p.indices.size(); ++i)
            if (p.indices[i] == port)
            {
                double value = *static_cast<const float *>(data);
                if (std::isfinite(value))
                    p.values[i] = value;
            }
    },
    [](const char *uri) -> const void * {
        static const LV2UI_Idle_Interface idle{[](LV2UI_Handle h)->int {static_cast<Lv2Ui*>(h)->surface->idle();return 0;}};
        return uri&&!std::strcmp(uri,LV2_UI__idleInterface)?&idle:nullptr;
    }};
} // namespace cydaw::native
extern "C" CYDAW_EXPORT const LV2_Descriptor *lv2_descriptor(uint32_t index)
{
    return index == 0 ? &cydaw::native::descriptor : nullptr;
}
extern "C" CYDAW_EXPORT const LV2UI_Descriptor *lv2ui_descriptor(uint32_t index)
{
    return index == 0 ? &cydaw::native::uiDescriptor : nullptr;
}
