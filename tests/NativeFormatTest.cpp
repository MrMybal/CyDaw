// Hidden Win32 parent only: exercises exported CLAP/LV2 without the DAW or JUCE.
#include <windows.h>
#undef min
#undef max
#include <algorithm>
#include <array>
#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <lv2/atom/atom.h>
#include <lv2/atom/util.h>
#include <lv2/core/lv2.h>
#include <lv2/instance-access/instance-access.h>
#include <lv2/midi/midi.h>
#include <lv2/state/state.h>
#include <lv2/ui/ui.h>
#include <lv2/urid/urid.h>
#include <map>
#include <regex>
#include <stdexcept>
#include <vector>
static void check(bool value, const char *text)
{
    if (!value)
        throw std::runtime_error(text);
}
static void pump()
{
    auto end = GetTickCount64() + 100;
    while (GetTickCount64() < end)
    {
        MSG message;
        while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessage(&message);
        }
        Sleep(1);
    }
}
static void clap(const std::filesystem::path &path, HWND parent)
{
    auto lib = LoadLibraryW(path.c_str());
    check(lib, "CLAP load");
    auto *entry = reinterpret_cast<const clap_plugin_entry_t *>(GetProcAddress(lib, "clap_entry"));
    check(entry && entry->init(path.string().c_str()), "CLAP entry");
    auto *factory = static_cast<const clap_plugin_factory_t *>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    check(factory && factory->get_plugin_count(factory) == 1, "CLAP factory");
    clap_host_t host{};
    host.clap_version = CLAP_VERSION;
    host.name = "CyDAW native test";
    host.vendor = "CyDAW";
    host.version = "1";
    host.url = "";
    host.get_extension = [](const clap_host_t *, const char *) -> const void * { return nullptr; };
    host.request_restart = [](const clap_host_t *) {};
    host.request_process = host.request_restart;
    host.request_callback = host.request_restart;
    auto *plugin = factory->create_plugin(factory, &host, factory->get_plugin_descriptor(factory, 0)->id);
    check(plugin && plugin->init(plugin), "CLAP init");
    check(plugin->activate(plugin, 48000, 1, 128), "CLAP activate");
    check(plugin->start_processing(plugin), "CLAP start");
    auto *ports =
        static_cast<const clap_plugin_audio_ports_t *>(plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    check(ports, "CLAP audio ports");
    std::array<clap_audio_buffer_t, 16> inputs{}, outputs{};
    std::array<std::array<std::array<float, 128>, 8>, 16> in{}, out{};
    std::array<std::array<float *, 8>, 16> inPointers{}, outPointers{};
    auto ni = ports->count(plugin, true), no = ports->count(plugin, false);
    check(ni <= 16 && no == 1, "CLAP bus counts");
    for (bool input : {true, false})
        for (uint32_t b = 0; b < (input ? ni : no); ++b)
        {
            clap_audio_port_info_t info{};
            check(ports->get(plugin, b, input, &info) && info.channel_count <= 8, "CLAP port info");
            auto &data = input ? inputs[b] : outputs[b];
            data.channel_count = info.channel_count;
            data.data32 = (input ? inPointers[b] : outPointers[b]).data();
            for (uint32_t c = 0; c < data.channel_count; ++c)
            {
                data.data32[c] = (input ? in[b][c] : out[b][c]).data();
                if (input)
                    for (int i = 0; i < 128; ++i)
                        data.data32[c][i] = .1f * std::sin(float(i) * .12f);
            }
        }
    clap_event_midi_t note{};
    note.header = {sizeof(note), 0, 0, CLAP_EVENT_MIDI, 0};
    note.data[0] = 0x90;
    note.data[1] = 60;
    note.data[2] = 100;
    clap_input_events_t events{&note, [](const clap_input_events_t *) { return 1u; },
                               [](const clap_input_events_t *e, uint32_t i) -> const clap_event_header_t *
                               { return i ? nullptr : &static_cast<clap_event_midi_t *>(e->ctx)->header; }};
    clap_process_t data{};
    data.frames_count = 128;
    data.audio_inputs = inputs.data();
    data.audio_inputs_count = ni;
    data.audio_outputs = outputs.data();
    data.audio_outputs_count = no;
    data.in_events = &events;
    float peak = 0;
    for (int block = 0; block < 80; ++block)
    {
        if (block)
            data.in_events = nullptr;
        check(plugin->process(plugin, &data) != CLAP_PROCESS_ERROR, "CLAP audio process");
        for (float sample : out[0][0])
        {
            check(std::isfinite(sample), "CLAP finite audio");
            peak = std::max(peak, std::abs(sample));
        }
    }
    check(peak > .0001f, "CLAP audible data");
    auto *params = static_cast<const clap_plugin_params_t *>(plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    check(params && params->count(plugin) > 0, "CLAP parameters");
    clap_param_info_t info{};
    check(params->get_info(plugin, 0, &info), "CLAP parameter info");
    double initial{};
    check(params->get_value(plugin, info.id, &initial), "CLAP parameter value");
    auto *state = static_cast<const clap_plugin_state_t *>(plugin->get_extension(plugin, CLAP_EXT_STATE));
    check(state, "CLAP state");
    std::vector<uint8_t> bytes;
    clap_ostream_t writer{&bytes, [](const clap_ostream_t *s, const void *source, uint64_t size) -> int64_t
                          {
                              auto &b = *static_cast<std::vector<uint8_t> *>(s->ctx);
                              size = std::min<uint64_t>(size, 7);
                              auto *p = static_cast<const uint8_t *>(source);
                              b.insert(b.end(), p, p + size);
                              return int64_t(size);
                          }};
    check(state->save(plugin, &writer), "CLAP partial stream save");
    clap_event_param_value_t change{};
    change.header = {sizeof(change), 0, 0, CLAP_EVENT_PARAM_VALUE, 0};
    change.param_id = info.id;
    change.note_id = -1;
    change.port_index = change.channel = change.key = -1;
    change.value = info.min_value + (info.max_value - info.min_value) * .25;
    clap_input_events_t input{&change, [](const clap_input_events_t *) { return 1u; },
                              [](const clap_input_events_t *l, uint32_t) -> const clap_event_header_t *
                              { return &static_cast<const clap_event_param_value_t *>(l->ctx)->header; }};
    params->flush(plugin, &input, nullptr);
    double value{};
    params->get_value(plugin, info.id, &value);
    check(std::abs(value - change.value) < 1e-8, "CLAP parameter update");
    struct Reader
    {
        std::vector<uint8_t> &bytes;
        size_t at{};
    } reader{bytes};
    clap_istream_t stream{&reader, [](const clap_istream_t *s, void *dest, uint64_t size) -> int64_t
                          {
                              auto &r = *static_cast<Reader *>(s->ctx);
                              size = std::min<uint64_t>({size, 7, r.bytes.size() - r.at});
                              std::memcpy(dest, r.bytes.data() + r.at, size_t(size));
                              r.at += size_t(size);
                              return int64_t(size);
                          }};
    check(state->load(plugin, &stream), "CLAP partial stream load");
    params->get_value(plugin, info.id, &value);
    check(std::abs(value - initial) < 1e-8, "CLAP state restored value");
    auto *gui = static_cast<const clap_plugin_gui_t *>(plugin->get_extension(plugin, CLAP_EXT_GUI));
    check(gui && gui->is_api_supported(plugin, CLAP_WINDOW_API_WIN32, false), "CLAP native GUI");
    for (int repeat = 0; repeat < 2; ++repeat)
    {
        check(gui->create(plugin, CLAP_WINDOW_API_WIN32, false), "CLAP GUI create");
        clap_window_t window{};
        window.api = CLAP_WINDOW_API_WIN32;
        window.win32 = parent;
        check(gui->set_parent(plugin, &window), "CLAP GUI attach");
        check(gui->show(plugin), "CLAP GUI show");
        pump();
        check(GetWindow(parent, GW_CHILD) != nullptr, "CLAP child window");
        gui->hide(plugin);
        check(!(GetWindowLongPtrW(GetWindow(parent,GW_CHILD),GWL_STYLE)&WS_VISIBLE),"CLAP GUI hides its child");
        gui->destroy(plugin);
        check(GetWindow(parent, GW_CHILD) == nullptr, "CLAP GUI cleanup");
    }
    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    entry->deinit();
    FreeLibrary(lib);
    std::cout << "CLAP audio/state/parameters/native GUI OK peak=" << peak << "\n";
}
static void lv2(const std::filesystem::path &path, HWND parent)
{
    auto lib = LoadLibraryW((path / L"cydaw_native.dll").c_str());
    check(lib, "LV2 load");
    auto descriptor =
        reinterpret_cast<const LV2_Descriptor *(*)(uint32_t)>(GetProcAddress(lib, "lv2_descriptor"));
    check(descriptor, "LV2 descriptor symbol");
    auto *d = descriptor(0);
    check(d && !descriptor(1), "LV2 descriptor count");
    std::map<std::string, uint32_t> ids;
    LV2_URID_Map map{&ids, [](LV2_URID_Map_Handle handle, const char *key)
                     {
                         auto &m = *static_cast<std::map<std::string, uint32_t> *>(handle);
                         auto [it, inserted] = m.emplace(key, uint32_t(m.size() + 1));
                         return it->second;
                     }};
    LV2_Feature mapping{LV2_URID__map, &map};
    const LV2_Feature *features[]{&mapping, nullptr};
    auto instance = d->instantiate(d, 48000, path.string().c_str(), features);
    check(instance, "LV2 instantiate");
    std::ifstream metadata(path / L"plugin.ttl");
    check(bool(metadata), "LV2 metadata");
    std::string line;
    std::map<uint32_t, std::vector<float>> audio;
    std::map<uint32_t, float> controls;
    std::map<uint32_t, std::array<uint64_t, 8192>> atoms;
    std::vector<uint32_t> outputs;
    int parameter = -1;
    bool instrument = true;
    while (std::getline(metadata, line))
    {
        if (line.find(" lv2:port [ a ") != 0)
            continue;
        std::smatch match;
        if (!std::regex_search(line, match, std::regex("lv2:index ([0-9]+)")))
            continue;
        auto index = uint32_t(std::stoul(match[1]));
        if (line.find("lv2:AudioPort") != std::string::npos)
        {
            auto &buffer = audio[index];
            buffer.resize(128);
            if (line.find("lv2:InputPort") != std::string::npos)
            {
                instrument = false;
                for (int i = 0; i < 128; ++i)
                    buffer[i] = .1f * std::sin(float(i) * .12f);
            }
            else
                outputs.push_back(index);
            d->connect_port(instance, index, buffer.data());
        }
        else if (line.find("atom:AtomPort") != std::string::npos)
        {
            auto &data = atoms[index];
            data = {};
            auto *sequence = reinterpret_cast<LV2_Atom_Sequence *>(data.data());
            sequence->atom.type = map.map(map.handle, LV2_ATOM__Sequence);
            if (line.find("lv2:InputPort") != std::string::npos)
            {
                sequence->atom.size = sizeof(sequence->body);
                alignas(8) uint8_t eventBytes[32]{};
                auto *event = reinterpret_cast<LV2_Atom_Event *>(eventBytes);
                event->body.type = map.map(map.handle, LV2_MIDI__MidiEvent);
                event->body.size = 3;
                auto *bytes = reinterpret_cast<uint8_t *>(&event->body + 1);
                bytes[0] = 0x90;
                bytes[1] = 60;
                bytes[2] = 100;
                lv2_atom_sequence_append_event(sequence, 65528, event);
            }
            else
                sequence->atom.size = 65528;
            d->connect_port(instance, index, data.data());
        }
        else
        {
            float value = 0;
            if (std::regex_search(line, match, std::regex("lv2:default ([^ ;]+)")))
                value = std::stof(match[1]);
            controls[index] = value;
            d->connect_port(instance, index, &controls[index]);
            if (parameter < 0 && line.find("param_") != std::string::npos)
                parameter = int(index);
        }
    }
    check(!outputs.empty(), "LV2 audio output");
    d->activate(instance);
    float peak = 0;
    for (int i = 0; i < 80; ++i)
    {
        d->run(instance, 128);
        for (auto index : outputs)
            for (float sample : audio[index])
            {
                check(std::isfinite(sample), "LV2 finite audio");
                peak = std::max(peak, std::abs(sample));
            }
        for (auto &[index, data] : atoms)
        {
            auto *seq = reinterpret_cast<LV2_Atom_Sequence *>(data.data());
            seq->atom.size = sizeof(seq->body);
        }
    }
    check(peak > .0001f, "LV2 audible data");
    auto *state = static_cast<const LV2_State_Interface *>(d->extension_data(LV2_STATE__interface));
    check(state, "LV2 state interface");
    struct Saved
    {
        std::vector<uint8_t> bytes;
        uint32_t key{}, type{}, flags{};
    } saved;
    check(state->save(
              instance,
              [](LV2_State_Handle handle, uint32_t key, const void *data, size_t size, uint32_t type,
                 uint32_t flags)
              {
                  auto &s = *static_cast<Saved *>(handle);
                  s.key = key;
                  s.type = type;
                  s.flags = flags;
                  auto *b = static_cast<const uint8_t *>(data);
                  s.bytes.assign(b, b + size);
                  return LV2_STATE_SUCCESS;
              },
              &saved, 0, features) == LV2_STATE_SUCCESS,
          "LV2 state save");
    check(state->restore(
              instance,
              [](LV2_State_Handle handle, uint32_t key, size_t *size, uint32_t *type,
                 uint32_t *flags) -> const void *
              {
                  auto &s = *static_cast<Saved *>(handle);
                  if (s.key != key)
                      return nullptr;
                  *size = s.bytes.size();
                  *type = s.type;
                  *flags = s.flags;
                  return s.bytes.data();
              },
              &saved, 0, features) == LV2_STATE_SUCCESS,
          "LV2 state restore");
    auto ui =
        reinterpret_cast<const LV2UI_Descriptor *(*)(uint32_t)>(GetProcAddress(lib, "lv2ui_descriptor"));
    check(ui && ui(0), "LV2 native GUI descriptor");
    LV2_Feature parentFeature{LV2_UI__parent, parent}, access{LV2_INSTANCE_ACCESS_URI, instance};
    const LV2_Feature *guiFeatures[]{&parentFeature, &access, &mapping, nullptr};
    for (int repeat = 0; repeat < 2; ++repeat)
    {
        LV2UI_Widget window{};
        auto *gui = ui(0);
        auto view = gui->instantiate(
            gui, d->URI, path.string().c_str(),
            [](LV2UI_Controller, uint32_t, uint32_t, uint32_t, const void *) {}, nullptr, &window,
            guiFeatures);
        check(view && window, "LV2 GUI instantiate");
        pump();
        check(GetWindow(parent, GW_CHILD) != nullptr, "LV2 child window");
        gui->cleanup(view);
        check(GetWindow(parent, GW_CHILD) == nullptr, "LV2 GUI cleanup");
    }
    d->deactivate(instance);
    d->cleanup(instance);
    FreeLibrary(lib);
    std::cout << "LV2 audio/state/native GUI OK peak=" << peak << " instrument=" << instrument << "\n";
}
int wmain(int argc, wchar_t **argv)
{
    if (argc != 3)
        return 2;
    auto parent = CreateWindowExW(0, L"STATIC", L"CyDAW hidden format test", WS_OVERLAPPED, 0, 0, 1600, 1000,
                                  nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!parent)
        return 3;
    try
    {
        if (std::wstring(argv[1]) == L"CLAP")
            clap(argv[2], parent);
        else
            lv2(argv[2], parent);
        DestroyWindow(parent);
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << "\n";
        DestroyWindow(parent);
        return 1;
    }
}
