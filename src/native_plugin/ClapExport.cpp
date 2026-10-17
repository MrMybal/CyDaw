#include "NativeProcessor.h"
#include <array>
#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/latency.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/tail.h>
#include <clap/ext/timer-support.h>
#if defined(_WIN32)
#define CYDAW_CLAP_WINDOW_API CLAP_WINDOW_API_WIN32
#else
#define CYDAW_CLAP_WINDOW_API CLAP_WINDOW_API_X11
#endif
#include <cstdio>
#include <cstring>
namespace cydaw::native
{
static Definition metadata = makeDefinition();
static const char *featuresEffect[]{CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, nullptr};
static const char *featuresSynth[]{CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, nullptr};
static const clap_plugin_descriptor_t descriptor{CLAP_VERSION,
                                                 CYDAW_PLUGIN_URI,
                                                 CYDAW_PRODUCT,
                                                 CYDAW_VENDOR,
                                                 "",
                                                 "",
                                                 "",
                                                 CYDAW_VERSION,
                                                 "CyDAW native device",
                                                 metadata.synth ? featuresSynth : featuresEffect};
struct ClapExport
{
    clap_plugin_t api{};
    const clap_host_t *host{};
    NativeProcessor processor;
    std::unique_ptr<EditorSurface> editor;
    clap_id editorTimer{CLAP_INVALID_ID};
    std::vector<const ParameterDefinition *> parameters;
    std::unique_ptr<std::atomic<bool>[]> dirty;
    std::atomic<bool> dirtyProgram{};
    std::array<NativeBlock::Bus, 16> buses{};
    std::array<engine::MidiEffectEvent, 4096> midi{}, midiOut{};
    std::array<std::array<float *, 8>, 16> shiftedInputs{};
    std::array<float *, 8> shiftedOutputs{};
    double rate{48000};
    int64_t position{};
    static ClapExport &self(const clap_plugin_t *p) { return *static_cast<ClapExport *>(p->plugin_data); }
    explicit ClapExport(const clap_host_t *h) : host(h)
    {
        for (const auto &p : processor.definition().parameters)
            if (p.host)
                parameters.push_back(&p);
        dirty = std::make_unique<std::atomic<bool>[]>(parameters.size());
        for (size_t i = 0; i < parameters.size(); ++i)
            dirty[i] = false;
        processor.edited = [this](uint32_t id, double)
        {
            for (size_t i = 0; i < parameters.size(); ++i)
                if (parameters[i]->id == id)
                    dirty[i] = true;
            if (auto *p = static_cast<const clap_host_params_t *>(host->get_extension(host, CLAP_EXT_PARAMS)))
                p->request_flush(host);
        };
        api.desc = &descriptor;
        api.plugin_data = this;
        api.init = [](const clap_plugin_t *) { return true; };
        api.destroy = [](const clap_plugin_t *p) { delete &self(p); };
        api.activate = [](const clap_plugin_t *p, double rate, uint32_t, uint32_t max)
        {
            auto &s = self(p);
            s.rate = rate;
            return s.processor.prepare(rate, int(max));
        };
        api.deactivate = [](const clap_plugin_t *) {};
        api.start_processing = [](const clap_plugin_t *) { return true; };
        api.stop_processing = [](const clap_plugin_t *) {};
        api.reset = [](const clap_plugin_t *p) { self(p).processor.reset(); };
        api.process = [](const clap_plugin_t *p, const clap_process_t *block)
        { return self(p).process(*block); };
        api.get_extension = [](const clap_plugin_t *, const char *id) { return extension(id); };
        api.on_main_thread = [](const clap_plugin_t *p) { if(self(p).editor) self(p).editor->idle(); };
    }
    ~ClapExport() { stopEditor(); }
    void stopEditor() {
        if(editorTimer!=CLAP_INVALID_ID) {
            if(auto* timer=static_cast<const clap_host_timer_support_t*>(host->get_extension(host,CLAP_EXT_TIMER_SUPPORT)))
                timer->unregister_timer(host,editorTimer);
            editorTimer=CLAP_INVALID_ID;
        }
        editor.reset();
    }
    void apply(const clap_event_header_t *event)
    {
        if (!event || event->space_id != 0)
            return;
        if (event->type == CLAP_EVENT_PARAM_VALUE && event->size >= sizeof(clap_event_param_value_t))
        {
            const auto &e = *reinterpret_cast<const clap_event_param_value_t *>(event);
            if (!std::isfinite(e.value))
                return;
            if (e.param_id == NativeProcessor::programId)
            {
                processor.applyProgram(int(std::round(std::clamp(
                    e.value, 0.0, double(std::max<size_t>(1, processor.definition().programs.size()) - 1)))));
                dirtyProgram = true;
                for (size_t i = 0; i < parameters.size(); ++i)
                    dirty[i] = true;
            }
            else
                processor.setParameter(e.param_id, e.value);
        }
    }
    void emit(const clap_output_events_t *output, uint32_t time = 0)
    {
        if (!output)
            return;
        for (size_t i = 0; i < parameters.size(); ++i)
            if (dirty[i].exchange(false))
            {
                clap_event_param_value_t e{};
                e.header = {sizeof(e), time, 0, CLAP_EVENT_PARAM_VALUE, 0};
                e.param_id = parameters[i]->id;
                e.note_id = -1;
                e.port_index = -1;
                e.channel = -1;
                e.key = -1;
                e.value = processor.parameterValue(e.param_id);
                if (!output->try_push(output, &e.header))
                    dirty[i] = true;
            }
        if (dirtyProgram.exchange(false))
        {
            clap_event_param_value_t e{};
            e.header = {sizeof(e), time, 0, CLAP_EVENT_PARAM_VALUE, 0};
            e.param_id = NativeProcessor::programId;
            e.note_id = -1;
            e.port_index = e.channel = e.key = -1;
            e.value = processor.program();
            if (!output->try_push(output, &e.header))
                dirtyProgram = true;
        }
    }
    clap_process_status process(const clap_process_t &block)
    {
        if (block.audio_outputs_count != 1 || block.audio_outputs[0].channel_count > 8 ||
            !block.audio_outputs[0].data32)
            return CLAP_PROCESS_ERROR;
        emit(block.out_events);
        size_t count = 0;
        auto events = block.in_events ? block.in_events->size(block.in_events) : 0;
        NativeBlock native;
        native.clock.sampleRate = rate;
        native.clock.beatsPerMinute = 120;
        native.seconds = position / rate;
        if (block.transport)
        {
            const auto &t = *block.transport;
            if (t.flags & CLAP_TRANSPORT_HAS_TEMPO)
                native.clock.beatsPerMinute = t.tempo;
            native.clock.playing = bool(t.flags & CLAP_TRANSPORT_IS_PLAYING);
            if (t.flags & CLAP_TRANSPORT_HAS_SECONDS_TIMELINE)
                native.seconds = t.song_pos_seconds / double(CLAP_SECTIME_FACTOR);
            if (t.flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE)
                native.clock.quarterNotes = t.song_pos_beats / double(CLAP_BEATTIME_FACTOR);
        }
        for (uint32_t i = 0; i < events; ++i)
        {
            auto *e = block.in_events->get(block.in_events, i);
            if (!e || e->space_id != 0)
                continue;
            if (e->type == CLAP_EVENT_PARAM_VALUE)
                continue;
            if (count >= midi.size())
                continue;
            engine::MidiEffectEvent m{};
            m.offset = std::min(e->time, block.frames_count ? block.frames_count - 1 : 0);
            if (e->type == CLAP_EVENT_MIDI && e->size >= sizeof(clap_event_midi_t))
            {
                auto *v = reinterpret_cast<const clap_event_midi_t *>(e);
                m.status = v->data[0];
                m.data1 = v->data[1];
                m.data2 = v->data[2];
            }
            else if ((e->type == CLAP_EVENT_NOTE_ON || e->type == CLAP_EVENT_NOTE_OFF ||
                      e->type == CLAP_EVENT_NOTE_CHOKE) &&
                     e->size >= sizeof(clap_event_note_t))
            {
                auto *v = reinterpret_cast<const clap_event_note_t *>(e);
                m.status = uint8_t((e->type == CLAP_EVENT_NOTE_ON ? 0x90 : 0x80) |
                                   std::clamp<int>(v->channel, 0, 15));
                m.data1 = uint8_t(std::clamp<int>(v->key, 0, 127));
                m.data2 = uint8_t(std::clamp<int>(int(v->velocity * 127), 0, 127));
            }
            else
                continue;
            midi[count++] = m;
        }
        const auto ni = std::min<size_t>(block.audio_inputs_count, buses.size());
        const auto channels = block.audio_outputs[0].channel_count;
        const double seconds = native.seconds, quarters = native.clock.quarterNotes;
        size_t midiCursor = 0;
        uint32_t processed = 0;
        auto render = [&](uint32_t end)
        {
            if (end <= processed)
                return true;
            for (size_t i = 0; i < ni; ++i)
            {
                const auto &source = block.audio_inputs[i];
                if (source.channel_count > 8)
                    return false;
                for (uint32_t c = 0; c < source.channel_count; ++c)
                    shiftedInputs[i][c] =
                        source.data32 && source.data32[c] ? source.data32[c] + processed : nullptr;
                buses[i] = {shiftedInputs[i].data(), int(source.channel_count)};
            }
            for (uint32_t c = 0; c < channels; ++c)
            {
                if (!block.audio_outputs[0].data32[c])
                    return false;
                shiftedOutputs[c] = block.audio_outputs[0].data32[c] + processed;
            }
            const auto first = midiCursor;
            while (midiCursor < count && midi[midiCursor].offset < end)
            {
                midi[midiCursor].offset -= processed;
                ++midiCursor;
            }
            native.inputs = {buses.data(), ni};
            native.output = shiftedOutputs.data();
            native.channels = int(channels);
            native.frames = int(end - processed);
            native.seconds = seconds + processed / rate;
            native.clock.quarterNotes = quarters + processed / rate * native.clock.beatsPerMinute / 60;
            native.midi = {midi.data() + first, midiCursor - first};
            native.midiOutput = midiOut.data();
            native.midiCapacity = midiOut.size();
            if (!processor.process(native))
                return false;
            if (block.out_events)
                for (size_t i = 0; i < native.midiCount; ++i)
                {
                    auto m = midiOut[i];
                    clap_event_midi_t e{};
                    e.header = {sizeof(e), processed + m.offset, 0, CLAP_EVENT_MIDI, 0};
                    e.data[0] = m.status;
                    e.data[1] = m.data1;
                    e.data[2] = m.data2;
                    block.out_events->try_push(block.out_events, &e.header);
                }
            processed = end;
            return true;
        };
        for (uint32_t i = 0; i < events; ++i)
        {
            auto *event = block.in_events->get(block.in_events, i);
            if (event && event->space_id == 0 && event->type == CLAP_EVENT_PARAM_VALUE)
            {
                if (!render(std::clamp(event->time, processed, block.frames_count)))
                    return CLAP_PROCESS_ERROR;
                apply(event);
                emit(block.out_events, processed);
            }
        }
        if (!render(block.frames_count))
            return CLAP_PROCESS_ERROR;
        position += block.frames_count;
        return CLAP_PROCESS_CONTINUE;
    }
    static const void *extension(const char *id)
    {
        static const clap_plugin_audio_ports_t audio{
            [](const clap_plugin_t *p, bool in)
            {
                uint32_t n = 0;
                for (const auto &b : self(p).processor.definition().buses)
                    if (b.input == in)
                        ++n;
                return n;
            },
            [](const clap_plugin_t *p, uint32_t index, bool in, clap_audio_port_info_t *info)
            {
                uint32_t n = 0;
                for (const auto &b : self(p).processor.definition().buses)
                    if (b.input == in && n++ == index)
                    {
                        *info = {};
                        info->id = (in ? 0u : 0x10000u) + index;
                        std::snprintf(info->name, sizeof(info->name), "%s", b.name.c_str());
                        info->channel_count = uint32_t(b.channels);
                        info->flags = index == 0 ? CLAP_AUDIO_PORT_IS_MAIN : 0;
                        info->port_type = b.channels == 1   ? CLAP_PORT_MONO
                                          : b.channels == 2 ? CLAP_PORT_STEREO
                                                            : nullptr;
                        info->in_place_pair = CLAP_INVALID_ID;
                        return true;
                    }
                return false;
            }};
        static const clap_plugin_note_ports_t notes{
            [](const clap_plugin_t *p, bool in)
            {
                return uint32_t(in ? self(p).processor.definition().acceptsMidi
                                   : self(p).processor.definition().producesMidi);
            },
            [](const clap_plugin_t *p, uint32_t index, bool in, clap_note_port_info_t *info)
            {
                if (index || !(in ? self(p).processor.definition().acceptsMidi
                                  : self(p).processor.definition().producesMidi))
                    return false;
                *info = {};
                info->id = 0;
                info->supported_dialects = CLAP_NOTE_DIALECT_MIDI;
                info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
                std::strcpy(info->name, "MIDI");
                return true;
            }};
        static const clap_plugin_params_t params{
            [](const clap_plugin_t *p)
            {
                auto &s = self(p);
                return uint32_t(s.parameters.size() + !s.processor.definition().programs.empty());
            },
            [](const clap_plugin_t *p, uint32_t index, clap_param_info_t *info)
            {
                auto &s = self(p);
                *info = {};
                if (index < s.parameters.size())
                {
                    const auto &v = *s.parameters[index];
                    info->id = v.id;
                    info->min_value = v.minimum;
                    info->max_value = v.maximum;
                    info->default_value = v.initial;
                    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
                    std::snprintf(info->name, sizeof(info->name), "%s", v.name.c_str());
                    return true;
                }
                if (index == s.parameters.size() && !s.processor.definition().programs.empty())
                {
                    info->id = NativeProcessor::programId;
                    info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM | CLAP_PARAM_IS_AUTOMATABLE;
                    info->max_value = double(s.processor.definition().programs.size() - 1);
                    std::strcpy(info->name, "Preset");
                    return true;
                }
                return false;
            },
            [](const clap_plugin_t *p, clap_id id, double *v)
            {
                auto &s = self(p);
                if (id == NativeProcessor::programId)
                {
                    *v = s.processor.program();
                    return true;
                }
                if (s.processor.parameterIndex(id) < 0)
                    return false;
                *v = s.processor.parameterValue(id);
                return true;
            },
            [](const clap_plugin_t *p, clap_id id, double v, char *text, uint32_t size)
            {
                if (!size)
                    return false;
                auto &s = self(p);
                if (id == NativeProcessor::programId && int(v) >= 0 &&
                    int(v) < int(s.processor.definition().programs.size()))
                    std::snprintf(text, size, "%s", s.processor.definition().programs[int(v)].name.c_str());
                else
                    std::snprintf(text, size, "%.17g", v);
                return true;
            },
            [](const clap_plugin_t *p, clap_id id, const char *text, double *value)
            {
                if (!text || !value)
                    return false;
                if (id == NativeProcessor::programId)
                {
                    const auto &programs = self(p).processor.definition().programs;
                    for (size_t i = 0; i < programs.size(); ++i)
                        if (programs[i].name == text)
                        {
                            *value = double(i);
                            return true;
                        }
                }
                char *end{};
                *value = std::strtod(text, &end);
                return end != text && std::isfinite(*value);
            },
            [](const clap_plugin_t *p, const clap_input_events_t *input, const clap_output_events_t *output)
            {
                auto &s = self(p);
                if (input)
                    for (uint32_t i = 0; i < input->size(input); ++i)
                        s.apply(input->get(input, i));
                s.emit(output);
            }};
        static const clap_plugin_state_t state{
            [](const clap_plugin_t *p, const clap_ostream_t *stream)
            {
                auto bytes = self(p).processor.save();
                size_t at = 0;
                while (at < bytes.size())
                {
                    auto n = stream->write(stream, bytes.data() + at, bytes.size() - at);
                    if (n <= 0 || uint64_t(n) > bytes.size() - at)
                        return false;
                    at += size_t(n);
                }
                return true;
            },
            [](const clap_plugin_t *p, const clap_istream_t *stream)
            {
                std::vector<uint8_t> bytes;
                std::array<uint8_t, 4096> buffer;
                while (bytes.size() < 1024 * 1024)
                {
                    auto n = stream->read(stream, buffer.data(), buffer.size());
                    if (n < 0 || uint64_t(n) > buffer.size())
                        return false;
                    if (!n)
                    {
                        bool ok = self(p).processor.load(bytes);
                        if (ok)
                        {
                            for (size_t i = 0; i < self(p).parameters.size(); ++i)
                                self(p).dirty[i] = true;
                            self(p).dirtyProgram = true;
                            if (auto *params = static_cast<const clap_host_params_t *>(
                                    self(p).host->get_extension(self(p).host, CLAP_EXT_PARAMS)))
                                params->rescan(self(p).host, CLAP_PARAM_RESCAN_VALUES);
                        }
                        return ok;
                    }
                    bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + n);
                }
                return false;
            }};
        static const clap_plugin_latency_t latency{[](const clap_plugin_t *p)
                                                   { return uint32_t(self(p).processor.latency()); }};
        static const clap_plugin_tail_t tail{[](const clap_plugin_t *) { return uint32_t(INT32_MAX); }};
        static const clap_plugin_gui_t gui{
            [](const clap_plugin_t *, const char *api, bool floating)
            { return !floating && api && !std::strcmp(api, CYDAW_CLAP_WINDOW_API); },
            [](const clap_plugin_t *, const char **api, bool *floating)
            {
                *api = CYDAW_CLAP_WINDOW_API;
                *floating = false;
                return true;
            },
            [](const clap_plugin_t *p, const char *api, bool floating)
            {
                if (floating || !api || std::strcmp(api, CYDAW_CLAP_WINDOW_API) || self(p).editor)
                    return false;
#if !defined(_WIN32)
                auto* timer=static_cast<const clap_host_timer_support_t*>(self(p).host->get_extension(self(p).host,CLAP_EXT_TIMER_SUPPORT));
                if(!timer||!timer->register_timer(self(p).host,33,&self(p).editorTimer))return false;
#endif
                self(p).editor = makeEditor(self(p).processor);
                return true;
            },
            [](const clap_plugin_t *p) { self(p).stopEditor(); },
            [](const clap_plugin_t *, double) { return false; },
            [](const clap_plugin_t *p, uint32_t *w, uint32_t *h)
            {
                *w = uint32_t(self(p).processor.definition().width);
                *h = uint32_t(self(p).processor.definition().height);
                return true;
            },
            [](const clap_plugin_t *) { return false; },
            [](const clap_plugin_t *, clap_gui_resize_hints_t *) { return false; },
            [](const clap_plugin_t *p, uint32_t *w, uint32_t *h)
            {
                *w = uint32_t(self(p).processor.definition().width);
                *h = uint32_t(self(p).processor.definition().height);
                return true;
            },
            [](const clap_plugin_t *p, uint32_t w, uint32_t h)
            {
                return w == uint32_t(self(p).processor.definition().width) &&
                       h == uint32_t(self(p).processor.definition().height);
            },
            [](const clap_plugin_t *p, const clap_window_t *parent)
            {
                return parent && parent->api && !std::strcmp(parent->api, CYDAW_CLAP_WINDOW_API) &&
                       self(p).editor && self(p).editor->attach(
#if defined(_WIN32)
                           parent->win32
#else
                           reinterpret_cast<void*>(uintptr_t(parent->x11))
#endif
                       );
            },
            [](const clap_plugin_t *, const clap_window_t *) { return false; },
            [](const clap_plugin_t *, const char *) {},
            [](const clap_plugin_t *p)
            {
                auto &s = self(p);
                if (!s.editor || !s.editor->handle())
                    return false;
                s.editor->setVisible(true);
                return true;
            },
            [](const clap_plugin_t *p)
            {
                auto &s = self(p);
                if (!s.editor)
                    return false;
                s.editor->setVisible(false);
                return true;
            }};
        static const clap_plugin_timer_support_t timer{[](const clap_plugin_t* p,clap_id id) {
            if(id==self(p).editorTimer&&self(p).editor)self(p).editor->idle();
        }};
        if (!std::strcmp(id, CLAP_EXT_TIMER_SUPPORT)) return &timer;
        if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS))
            return &audio;
        if (!std::strcmp(id, CLAP_EXT_NOTE_PORTS))
            return &notes;
        if (!std::strcmp(id, CLAP_EXT_PARAMS))
            return &params;
        if (!std::strcmp(id, CLAP_EXT_STATE))
            return &state;
        if (!std::strcmp(id, CLAP_EXT_LATENCY))
            return &latency;
        if (!std::strcmp(id, CLAP_EXT_TAIL))
            return &tail;
        if (!std::strcmp(id, CLAP_EXT_GUI))
            return &gui;
        return nullptr;
    }
};
static const clap_plugin_factory_t factory{
    [](const clap_plugin_factory_t *) { return 1u; },
    [](const clap_plugin_factory_t *, uint32_t i) { return i == 0 ? &descriptor : nullptr; },
    [](const clap_plugin_factory_t *, const clap_host_t *host, const char *id) -> const clap_plugin_t *
    {
        if (!host || !clap_version_is_compatible(host->clap_version) || !id || std::strcmp(id, descriptor.id))
            return nullptr;
        try
        {
            return &(new ClapExport(host))->api;
        }
        catch (...)
        {
            return nullptr;
        }
    }};
} // namespace cydaw::native
extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry{
    CLAP_VERSION, [](const char *) { return true; }, []() {}, [](const char *id) -> const void *
    { return id && !std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? &cydaw::native::factory : nullptr; }};
