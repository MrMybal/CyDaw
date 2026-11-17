// Original CLAP integration fixture: stereo gain with a 2048-frame tail.
#include <clap/clap.h>
#include <cmath>
#include <cstdio>
#include <cstring>
namespace {
const char* features[]{CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, nullptr};
const clap_plugin_descriptor_t descriptor{CLAP_VERSION, "org.cydaw.sleep-test", "CyDaw sleep test", "CyDaw", "", "", "", "1", "Test only", features};
struct Fixture {
    clap_plugin_t api{};
    double gain = .5;
    unsigned tail = 0;
    static Fixture& get(const clap_plugin_t* p) { return *static_cast<Fixture*>(p->plugin_data); }
    Fixture() {
        api.desc = &descriptor; api.plugin_data = this;
        api.init = [](const clap_plugin_t*) { return true; };
        api.destroy = [](const clap_plugin_t* p) { delete &get(p); };
        api.activate = [](const clap_plugin_t*, double, uint32_t, uint32_t) { return true; };
        api.deactivate = [](const clap_plugin_t*) {};
        api.start_processing = [](const clap_plugin_t*) { return true; };
        api.stop_processing = [](const clap_plugin_t*) {};
        api.reset = [](const clap_plugin_t* p) { get(p).tail = 0; };
        api.on_main_thread = [](const clap_plugin_t*) {};
        api.process = [](const clap_plugin_t* p, const clap_process_t* c) -> clap_process_status {
            auto& s = get(p);
            for (unsigned i = 0; i < c->in_events->size(c->in_events); ++i) {
                const auto* e = c->in_events->get(c->in_events, i);
                if (e->space_id == CLAP_CORE_EVENT_SPACE_ID && e->type == CLAP_EVENT_PARAM_VALUE)
                    s.gain = reinterpret_cast<const clap_event_param_value_t*>(e)->value;
            }
            for (unsigned i = 0; i < c->frames_count; ++i) {
                const float l = c->audio_inputs[0].data32[0][i], r = c->audio_inputs[0].data32[1][i];
                const bool active = l != 0 || r != 0;
                if (active) s.tail = 2048;
                const float residual = !active && s.tail ? float(s.gain * .125) : 0.f;
                c->audio_outputs[0].data32[0][i] = float(l * s.gain) + residual;
                c->audio_outputs[0].data32[1][i] = float(r * s.gain) + residual;
                if (!active && s.tail) --s.tail;
            }
            return CLAP_PROCESS_CONTINUE_IF_NOT_QUIET;
        };
        api.get_extension = [](const clap_plugin_t*, const char* id) -> const void* {
            static const clap_plugin_audio_ports_t ports{
                [](const clap_plugin_t*, bool) { return 1u; },
                [](const clap_plugin_t*, uint32_t i, bool, clap_audio_port_info_t* v) {
                    if (i) return false;
                    *v = {}; v->id = 0; v->channel_count = 2;
                    v->flags = CLAP_AUDIO_PORT_IS_MAIN; v->port_type = CLAP_PORT_STEREO;
                    v->in_place_pair = CLAP_INVALID_ID;
                    std::snprintf(v->name, sizeof(v->name), "Main"); return true;
                }};
            static const clap_plugin_params_t params{
                [](const clap_plugin_t*) { return 1u; },
                [](const clap_plugin_t*, uint32_t i, clap_param_info_t* v) {
                    if (i) return false;
                    *v = {}; v->id = 0; v->flags = CLAP_PARAM_IS_AUTOMATABLE;
                    v->max_value = 1; v->default_value = .5;
                    std::snprintf(v->name, sizeof(v->name), "Gain"); return true;
                },
                [](const clap_plugin_t* p, clap_id i, double* v) {
                    if (i) return false;
                    *v = get(p).gain; return true;
                }, nullptr, nullptr,
                [](const clap_plugin_t*, const clap_input_events_t*, const clap_output_events_t*) {}};
            static const clap_plugin_state_t state{
                [](const clap_plugin_t* p, const clap_ostream_t* stream) {
                    return stream->write(stream, &get(p).gain, sizeof(double)) == sizeof(double);
                },
                [](const clap_plugin_t* p, const clap_istream_t* stream) {
                    double value;
                    if (stream->read(stream, &value, sizeof(value)) != sizeof(value) || !std::isfinite(value) || value < 0 || value > 1) return false;
                    get(p).gain = value; return true;
                }};
            static const clap_plugin_latency_t latency{[](const clap_plugin_t*) { return 0u; }};
            static const clap_plugin_tail_t tail{[](const clap_plugin_t*) { return 2048u; }};
            if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &ports;
            if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &params;
            if (!std::strcmp(id, CLAP_EXT_STATE)) return &state;
            if (!std::strcmp(id, CLAP_EXT_LATENCY)) return &latency;
            if (!std::strcmp(id, CLAP_EXT_TAIL)) return &tail;
            return nullptr;
        };
    }
};
const clap_plugin_factory_t factory{
    [](const clap_plugin_factory_t*) { return 1u; },
    [](const clap_plugin_factory_t*, uint32_t i) { return i ? nullptr : &descriptor; },
    [](const clap_plugin_factory_t*, const clap_host_t*, const char* id) -> const clap_plugin_t* {
        return std::strcmp(id, descriptor.id) ? nullptr : &(new Fixture)->api;
    }};
}
extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry{
    CLAP_VERSION, [](const char*) { return true; }, [] {},
    [](const char* id) -> const void* { return std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? nullptr : &factory; }};
