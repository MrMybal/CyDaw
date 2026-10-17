#include "NativeProcessor.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
static std::string quote(const std::string &s)
{
    std::string out = "\"";
    for (char c : s)
    {
        if (c == '\\' || c == '\"')
        {
            out += '\\';
            out += c;
        }
        else if (c == '\n')
            out += "\\n";
        else if (c == '\r')
            out += "\\r";
        else
            out += c;
    }
    return out + '\"';
}
#if defined(_WIN32)
#define CYDAW_EXPORT __declspec(dllexport)
#else
#define CYDAW_EXPORT __attribute__((visibility("default")))
#endif
extern "C" CYDAW_EXPORT int cydaw_write_lv2_metadata(const char *path)
{
    const auto d = cydaw::native::makeDefinition();
    std::ofstream out(std::filesystem::u8path(path), std::ios::binary);
    if (!out)
        return 2;
    out << std::setprecision(17)
        << "@prefix lv2: <http://lv2plug.in/ns/lv2core#> .\n@prefix atom: <http://lv2plug.in/ns/ext/atom#> "
           ".\n@prefix midi: <http://lv2plug.in/ns/ext/midi#> .\n@prefix time: "
           "<http://lv2plug.in/ns/ext/time#> .\n@prefix ui: <http://lv2plug.in/ns/extensions/ui#> .\n@prefix "
           "urid: <http://lv2plug.in/ns/ext/urid#> .\n@prefix state: <http://lv2plug.in/ns/ext/state#> "
           ".\n@prefix doap: <http://usefulinc.com/ns/doap#> .\n@prefix foaf: <http://xmlns.com/foaf/0.1/> "
           ".\n@prefix pset: <http://lv2plug.in/ns/ext/presets#> .\n@prefix rdfs: "
           "<http://www.w3.org/2000/01/rdf-schema#> .\n@prefix rsz: <http://lv2plug.in/ns/ext/resize-port#> "
           ".\n";
    out << "<" CYDAW_PLUGIN_URI "> a lv2:Plugin" << (d.synth ? ", lv2:InstrumentPlugin" : "")
        << " ;\n doap:name " << quote(d.name) << " ; doap:maintainer [ foaf:name " << quote(d.vendor)
        << " ] ;\n lv2:requiredFeature urid:map ; lv2:extensionData state:interface ; ui:ui "
           "<" CYDAW_PLUGIN_URI "#ui>";
    uint32_t index = 0;
    auto port = [&](const std::string &type, const std::string &symbol, const std::string &name,
                    const std::string &properties)
    {
        out << " ;\n lv2:port [ a " << type << " ; lv2:index " << index++ << " ; lv2:symbol " << quote(symbol)
            << " ; lv2:name " << quote(name) << properties << " ]";
    };
    int bus = 0;
    for (const auto &b : d.buses)
    {
        for (int c = 0; c < b.channels; ++c)
            port(std::string("lv2:AudioPort, ") + (b.input ? "lv2:InputPort" : "lv2:OutputPort"),
                 "audio_" + std::to_string(bus) + "_" + std::to_string(c),
                 b.name + " " + std::to_string(c + 1), "");
        ++bus;
    }
    for (const auto &p : d.parameters)
        if (p.host)
        {
            std::ostringstream properties;
            properties << std::setprecision(17) << " ; lv2:minimum " << p.minimum << " ; lv2:maximum "
                       << p.maximum << " ; lv2:default " << p.initial;
            port("lv2:ControlPort, lv2:InputPort", "param_" + std::to_string(p.id), p.name, properties.str());
        }
    port("atom:AtomPort, lv2:InputPort", "events_in", d.acceptsMidi?"MIDI / Transport":"Transport",
         std::string(" ; atom:bufferType atom:Sequence ; atom:supports ")+(d.acceptsMidi?"midi:MidiEvent, ":"")+"time:Position ; lv2:designation lv2:control ; rsz:minimumSize 65536");
    if (d.producesMidi)
        port("atom:AtomPort, lv2:OutputPort", "events_out", "MIDI out",
             " ; atom:bufferType atom:Sequence ; atom:supports midi:MidiEvent ; rsz:minimumSize 65536");
    port("lv2:ControlPort, lv2:OutputPort", "latency", "Latency",
         " ; lv2:designation lv2:latency ; lv2:portProperty lv2:reportsLatency, lv2:integer ; lv2:minimum 0 "
         "; lv2:maximum 1048576 ; lv2:default 0");
    out << " .\n\n<" CYDAW_PLUGIN_URI
           "#ui> a ui:WindowsUI ; ui:binary <cydaw_native.dll> ; lv2:requiredFeature ui:parent ; "
           "lv2:optionalFeature <http://lv2plug.in/ns/ext/instance-access>, ui:fixedSize .\n";
    std::ofstream manifest(std::filesystem::u8path(path).parent_path() / "manifest.ttl", std::ios::binary);
    manifest << "@prefix lv2: <http://lv2plug.in/ns/lv2core#> .\n@prefix rdfs: "
                "<http://www.w3.org/2000/01/rdf-schema#> .\n@prefix pset: "
                "<http://lv2plug.in/ns/ext/presets#> .\n<" CYDAW_PLUGIN_URI
                "> a lv2:Plugin ; lv2:binary <cydaw_native.dll> ; rdfs:seeAlso <plugin.ttl> .\n";
    cydaw::native::NativeProcessor presets;
    const char *alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (size_t i = 0; i < d.programs.size(); ++i)
    {
        out << "\n<" CYDAW_PLUGIN_URI "#preset-" << i
            << "> a pset:Preset ; lv2:appliesTo <" CYDAW_PLUGIN_URI "> ; rdfs:label "
            << quote(d.programs[i].name);
        manifest << "<" CYDAW_PLUGIN_URI "#preset-" << i
                 << "> a pset:Preset ; lv2:appliesTo <" CYDAW_PLUGIN_URI "> ; rdfs:seeAlso <plugin.ttl> .\n";
        presets.applyProgram(int(i));
        auto bytes = presets.save();
        std::string encoded;
        for (size_t at = 0; at < bytes.size(); at += 3)
        {
            uint32_t v = uint32_t(bytes[at]) << 16;
            if (at + 1 < bytes.size())
                v |= uint32_t(bytes[at + 1]) << 8;
            if (at + 2 < bytes.size())
                v |= bytes[at + 2];
            encoded += alphabet[(v >> 18) & 63];
            encoded += alphabet[(v >> 12) & 63];
            encoded += at + 1 < bytes.size() ? alphabet[(v >> 6) & 63] : '=';
            encoded += at + 2 < bytes.size() ? alphabet[v & 63] : '=';
        }
        out << " ; state:state [ <" CYDAW_PLUGIN_URI "#state> " << quote(encoded)
            << "^^<http://www.w3.org/2001/XMLSchema#base64Binary> ]";
        size_t n = 0;
        const auto &program = d.programs[i];
        bool all = program.allValues.size() == d.parameters.size();
        const auto &values = all ? program.allValues : program.values;
        for (const auto &p : d.parameters)
        {
            if (!all && !p.host)
                continue;
            if (n < values.size() && p.host)
                out << " ; lv2:port [ lv2:symbol \"param_" << p.id << "\" ; pset:value "
                    << p.minimum + values[n] * (p.maximum - p.minimum) << " ]";
            ++n;
        }
        out << " .\n";
    }
    return out && manifest ? 0 : 3;
}
