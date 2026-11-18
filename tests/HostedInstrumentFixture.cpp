#include <clap/clap.h>

#include <cstdio>
#include <cstring>

namespace {
const char* features[] { CLAP_PLUGIN_FEATURE_INSTRUMENT, nullptr };
const clap_plugin_descriptor_t descriptor { CLAP_VERSION, "org.cydaw.instrument-test",
    "CyDaw instrument test", "CyDaw", "", "", "", "1", "Two independently routed test outputs", features };

struct Instrument {
    clap_plugin_t api {};
    int key { -1 };

    static Instrument& get(const clap_plugin_t* plugin)
    {
        return *static_cast<Instrument*>(plugin->plugin_data);
    }

    Instrument()
    {
        api.desc = &descriptor;
        api.plugin_data = this;
        api.init = [](const clap_plugin_t*) { return true; };
        api.destroy = [](const clap_plugin_t* p) { delete &get(p); };
        api.activate = [](const clap_plugin_t*, double, uint32_t, uint32_t) { return true; };
        api.deactivate = [](const clap_plugin_t*) {};
        api.start_processing = [](const clap_plugin_t*) { return true; };
        api.stop_processing = [](const clap_plugin_t*) {};
        api.reset = [](const clap_plugin_t* p) { get(p).key = -1; };
        api.on_main_thread = [](const clap_plugin_t*) {};
        api.process = [](const clap_plugin_t* p, const clap_process_t* block) -> clap_process_status {
            auto& active = get(p).key;
            const auto* events = block->in_events;
            auto eventIndex = 0u;
            for (auto frame = 0u; frame < block->frames_count; ++frame) {
                for (; eventIndex < events->size(events); ++eventIndex) {
                    const auto* header = events->get(events, eventIndex);
                    if (header->time > frame)
                        break;
                    if (header->space_id != CLAP_CORE_EVENT_SPACE_ID)
                        continue;
                    if (header->type == CLAP_EVENT_NOTE_ON)
                        active = reinterpret_cast<const clap_event_note_t*>(header)->key;
                    if (header->type == CLAP_EVENT_NOTE_OFF || header->type == CLAP_EVENT_NOTE_CHOKE)
                        active = -1;
                }
                for (auto bus = 0u; bus < block->audio_outputs_count; ++bus) {
                    auto& output = block->audio_outputs[bus];
                    const auto sample = active < 0 ? 0.0f : ((frame & 1 ? -1.0f : 1.0f) * (bus == 0 ? 0.4f : 0.1f));
                    for (auto channel = 0u; channel < output.channel_count; ++channel)
                        output.data32[channel][frame] = sample;
                }
            }
            return CLAP_PROCESS_CONTINUE;
        };
        api.get_extension = [](const clap_plugin_t*, const char* id) -> const void* {
            static const clap_plugin_audio_ports_t audio {
                [](const clap_plugin_t*, bool input) { return input ? 0u : 2u; },
                [](const clap_plugin_t*, uint32_t bus, bool input, clap_audio_port_info_t* result) {
                    if (input || bus > 1)
                        return false;
                    *result = {};
                    result->id = bus;
                    result->channel_count = 2;
                    result->flags = bus == 0 ? CLAP_AUDIO_PORT_IS_MAIN : 0;
                    result->port_type = CLAP_PORT_STEREO;
                    result->in_place_pair = CLAP_INVALID_ID;
                    std::snprintf(result->name, sizeof(result->name), "%s", bus == 0 ? "Main" : "Aux");
                    return true;
                }
            };
            static const clap_plugin_note_ports_t notes {
                [](const clap_plugin_t*, bool input) { return input ? 1u : 0u; },
                [](const clap_plugin_t*, uint32_t port, bool input, clap_note_port_info_t* result) {
                    if (!input || port != 0)
                        return false;
                    *result = {};
                    result->supported_dialects = result->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
                    std::snprintf(result->name, sizeof(result->name), "Keyboard");
                    return true;
                }
            };
            if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audio;
            if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &notes;
            return nullptr;
        };
    }
};
const clap_plugin_factory_t factory {
    [](const clap_plugin_factory_t*) { return 1u; },
    [](const clap_plugin_factory_t*, uint32_t index) { return index == 0 ? &descriptor : nullptr; },
    [](const clap_plugin_factory_t*, const clap_host_t*, const char* id) -> const clap_plugin_t* {
        return std::strcmp(id, descriptor.id) == 0 ? &(new Instrument)->api : nullptr;
    }
};
}

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry {
    CLAP_VERSION, [](const char*) { return true; }, [] {},
    [](const char* id) -> const void* { return std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr; }
};
