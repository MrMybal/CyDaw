#include "GraphPluginProjectGenerator.h"

#include <algorithm>
#include <cydaw/engine/PolySynthGraph.h>

namespace cydaw::app
{
namespace
{

juce::String cppString(juce::String value)
{
    value = value.replace("\\", "\\\\").replace("\"", "\\\"");
    value = value.replace("\r", "\\r").replace("\n", "\\n");
    return "\"" + value + "\"";
}

juce::String cmakeString(juce::String value)
{
    value = value.replace("\\", "/").replace("\"", "\\\"");
    value = value.replace(";", "\\;").replaceCharacters("\r\n", "  ");
    return "\"" + value + "\"";
}

juce::String slug(juce::String value)
{
    value = value.toLowerCase();
    juce::String result;
    for (const auto character : value)
        if (juce::CharacterFunctions::isLetterOrDigit(character))
            result << character;
        else if (result.isNotEmpty() && !result.endsWithChar('-'))
            result << '-';
    return result.trimCharactersAtEnd("-").substring(0, 48);
}

bool writeText(const juce::File &file, const juce::String &text)
{
    return file.replaceWithText(text, false, false, "\n");
}

} // namespace

juce::Result generateGraphVst3Project(const juce::File &destination, const cydaw::graph::PluginExportPlan &plan,
                                      const juce::var &manifest, const juce::var &runtimeState, const juce::String& format)
{
    if (format != "VST3" && format != "CLAP" && format != "LV2") return juce::Result::fail("Unknown export format");
    if (destination == juce::File{} || !runtimeState.isObject())
        return juce::Result::fail("The native export state or destination is missing");
    const auto native = runtimeState.getProperty("native", {}), poly = runtimeState.getProperty("polySynth", {});
    const auto editor = runtimeState.getProperty("editor", {}), presentation = editor.getProperty("presentation", {});
    const auto *nodes = native.getProperty("nodes", {}).getArray();
    if (!nodes)
        return juce::Result::fail("The native audio plan is missing");
    const auto waveform =
        static_cast<cydaw::engine::PolyOscillatorWaveform>(static_cast<int>(poly.getProperty("waveform", 0)));
    if (static_cast<bool>(poly.getProperty("enabled", false)) &&
        (waveform == cydaw::engine::PolyOscillatorWaveform::sampler ||
         waveform == cydaw::engine::PolyOscillatorWaveform::soundModel ||
         waveform == cydaw::engine::PolyOscillatorWaveform::rack))
        return juce::Result::fail("Sample players, recorded models and pad racks are excluded from native export");
    int outputs = 0, inputs = 0;
    for (const auto &b : plan.buses)
        if (b.mediaType == cydaw::graph::PluginBusMediaType::audio)
        {
            if (b.direction == cydaw::graph::PortDirection::input)
                ++inputs;
            else
                ++outputs;
        }
    if (outputs != 1 || inputs > 4)
        return juce::Result::fail("Native export requires one audio output and up to four audio inputs");
    // Samples and recorded models are deliberately outside this exporter.
    if (const auto *list = editor.getProperty("nodes", {}).getArray())
        for (const auto &n : *list)
        {
            const auto type = n.getProperty("type", {}).toString();
            if (type.containsIgnoreCase("sampler") || type == "drum-kit" || type == "drum-rack" ||
                type == "sound-model")
                return juce::Result::fail("Native export supports synthesis and effects; sample players, recorded "
                                          "models and pad racks are excluded");
            if (n.hasProperty("prismTables") || n.hasProperty("wavetableShape") || n.hasProperty("impulse"))
                return juce::Result::fail(
                    "Imported wavetable/impulse assets must be embedded before this device can use native export");
        }
    if (destination.createDirectory().failed())
        return juce::Result::fail("Cannot create the project directory");
    const auto generated = destination.getChildFile("generated");
    if (generated.createDirectory().failed())
        return juce::Result::fail("Cannot create the generated source directory");
    const auto prop = [](const juce::var &v, const char *key) { return v.getProperty(key, {}); };
    const auto number = [](const juce::var &value) { return juce::String(static_cast<double>(value), 12); };
    const auto integer = [](const juce::var &value) { return juce::String(static_cast<juce::int64>(value)); };
    const auto quote = [&](const juce::var &v, const char *key) { return cppString(prop(v, key).toString()); };
    juce::String cpp = "#include \"PluginDefinition.h\"\n#include <cydaw/engine/MidiEffects.h>\n";
    juce::String customDeclarations, customSources;
    if (const auto *controls = prop(presentation, "controls").getArray())
        for (const auto &c : *controls)
            if (prop(c, "kind").toString() == "custom")
            {
                const auto source = prop(prop(c, "design"), "customSource").toString();
                if (source.trim().isEmpty())
                    return juce::Result::fail("A custom C++ control has no source");
                const auto suffix = integer(prop(c, "id")), symbol = "cydawCustom_" + suffix,
                           filename = "Custom_" + suffix + ".cpp";
                if (!writeText(generated.getChildFile(filename),
                               "#define cydawDrawInterfaceV1 " + symbol + "\n" + source))
                    return juce::Result::fail("Cannot write custom component source");
                customDeclarations += "extern \"C\" void " + symbol + "(const CyInterfaceContext*);\n";
                customSources += "  \"${CMAKE_CURRENT_SOURCE_DIR}/generated/" + filename + "\"\n";
            }
    cpp += customDeclarations + "\nnamespace cydaw::native {\nDefinition makeDefinition(){Definition d;\n";
    cpp += "d.name=" + cppString(plan.identity.productName) + ";d.vendor=" + cppString(plan.identity.companyName) +
           ";d.version=" + cppString(plan.identity.version.toString()) + ";d.category=" + cppString(plan.vst3Category) +
           ";\n";
    cpp += "d.synth=" + juce::String(plan.isSynth ? "true" : "false") +
           ";d.acceptsMidi=" + (plan.acceptsMidi ? "true" : "false") +
           ";d.producesMidi=" + (plan.producesMidi ? "true" : "false") + ";\n";
    cpp += "d.width=" + integer(presentation.getProperty("windowWidth", 720)) +
           ";d.height=" + integer(presentation.getProperty("windowHeight", 420)) + ";\n";
    const auto surface=prop(presentation,"windowDesign");
    for(const auto* key:{"background","accent","text"})if(surface.hasProperty(key))
        cpp += "d."+juce::String(key)+"=0x"+juce::String::toHexString(prop(surface,key).toString().getHexValue64())+"u;\n";
    for (const auto &b : plan.buses)
        if (b.mediaType == cydaw::graph::PluginBusMediaType::audio)
            cpp += "d.buses.push_back({" + cppString(b.displayName) + "," + juce::String(b.channels) + "," +
                   (b.direction == cydaw::graph::PortDirection::input ? "true" : "false") + "});\n";
    auto metadata = juce::DynamicObject::Ptr(new juce::DynamicObject());
    metadata->setProperty("name", juce::String(plan.identity.productName));
    metadata->setProperty("vendor", juce::String(plan.identity.companyName));
    metadata->setProperty("synth", plan.isSynth);
    metadata->setProperty("acceptsMidi", plan.acceptsMidi);
    metadata->setProperty("producesMidi", plan.producesMidi);
    metadata->setProperty("programs", prop(runtimeState, "programs"));
    juce::Array<juce::var> metadataBuses, metadataParameters;
    for (const auto& b : plan.buses)
        if (b.mediaType == cydaw::graph::PluginBusMediaType::audio) {
            auto bus = juce::DynamicObject::Ptr(new juce::DynamicObject());
            bus->setProperty("name", juce::String(b.displayName));
            bus->setProperty("channels", static_cast<int>(b.channels));
            bus->setProperty("input", b.direction == cydaw::graph::PortDirection::input);
            metadataBuses.add(juce::var(bus.get()));
        }
    metadata->setProperty("buses", metadataBuses);
    const auto parameters =
        runtimeState.getProperty("allParameters", runtimeState.getProperty("publishedParameters", {}));
    if (const auto *list = parameters.getArray())
        for (const auto &p : *list)
        {
            cpp += "{ParameterDefinition p;p.id=" + integer(prop(p, "id")) + "u;p.key=" + quote(p, "key") +
                   ";p.name=" + quote(p, "name") + ";p.unit=" + quote(p, "unit") +
                   ";p.binding=" + quote(p, "targetParameter") + ";p.node=" + integer(prop(p, "targetNodeId")) +
                   "ull;\n";
            auto initial = prop(p, "default");
            if (const auto *authored = prop(presentation, "parameters").getArray())
                for (const auto &value : *authored)
                    if (prop(value, "key").toString() == prop(p, "key").toString())
                    {
                        initial = prop(value, "value");
                        break;
                    }
            auto parameter = p.clone();
            parameter.getDynamicObject()->setProperty("initial", initial);
            metadataParameters.add(parameter);
            cpp += "p.minimum=" + number(prop(p, "minimum")) + ";p.maximum=" + number(prop(p, "maximum")) +
                   ";p.initial=" + number(initial) + ";p.host=" +
                   juce::String(static_cast<bool>(p.getProperty("exportedToHost", true)) ? "true" : "false") +
                   ";d.parameters.push_back(std::move(p));}\n";
        }
    if (const auto *controls = prop(presentation, "controls").getArray())
        for (const auto &c : *controls)
        {
            const auto design = prop(c, "design");
            cpp += "{ControlDefinition c;c.accent=d.accent;c.background=d.background;c.text=d.text;c.id=" + integer(prop(c, "id")) + "ull;";
            for(const auto* key:{"instrumentType","instrumentPanel"})
                if(design.hasProperty(key))cpp += "c."+juce::String(key)+"="+quote(design,key)+";";
            cpp += "c.instrumentCompact="+juce::String(static_cast<bool>(prop(design,"instrumentCompact"))?"true":"false")+";";
            if(const auto* keys=prop(design,"instrumentKeys").getArray())for(const auto& key:*keys)
                cpp += "c.instrumentKeys.push_back("+cppString(key.toString())+");";
            for (const auto *key : {"kind", "label"})
                cpp += "c." + juce::String(key) + "=" + quote(c, key) + ";";
            for (const auto &pair :
                 {std::pair{"primary", "binding"}, std::pair{"secondary", "secondaryBinding"},
                  std::pair{"tertiary", "tertiaryBinding"}, std::pair{"quaternary", "quaternaryBinding"}})
                cpp += "c." + juce::String(pair.first) + "=" + quote(c, pair.second) + ";";
            for (const auto *key : {"x", "y", "width", "height"})
                cpp += "c." + juce::String(key) + "=" + number(prop(c, key)) + "f;";
            for (const auto *key : {"accent", "background", "text"})
                if (design.hasProperty(key))
                    cpp += "c." + juce::String(key) + "=0x" +
                           juce::String::toHexString(design.getProperty(key, {}).toString().getHexValue64()) + "u;";
            for (const auto *key : {"fontScale", "rounding"})
                if (design.hasProperty(key))
                    cpp += "c." + juce::String(key) + "=" + number(prop(design, key)) + "f;";
            for (const auto *key : {"pane", "variant"})
                if (design.hasProperty(key))
                    cpp += "c." + juce::String(key) + "=" + integer(prop(design, key)) + ";";
            for (const auto *key : {"hidden", "large", "showValue"})
                cpp += "c." + juce::String(key) + "=" +
                       juce::String(static_cast<bool>(prop(design, key)) ? "true" : "false") + ";";
            cpp += "c.page=" + quote(design, "page") + ";";
            if (const auto *a = prop(c, "extraBindings").getArray())
                for (const auto &b : *a)
                    cpp += "c.extra.push_back(" + cppString(b.toString()) + ");";
            if (const auto *a = prop(design, "indices").getArray())
                for (const auto &b : *a)
                    cpp += "c.indices.push_back(" + integer(b) + ");";
            if (prop(c, "kind").toString() == "custom")
                cpp += "c.custom=&cydawCustom_" + integer(prop(c, "id")) + ";";
            cpp += "d.controls.push_back(std::move(c));}\n";
        }
    if (const auto *programs = prop(runtimeState, "programs").getArray())
        for (const auto &p : *programs)
        {
            cpp += "{ProgramDefinition p;p.name=" + quote(p, "name") + ";";
            if (const auto *a = prop(p, "hostValues").getArray())
                for (const auto &v : *a)
                    cpp += "p.values.push_back(" + number(v) + ");";
            if (const auto *a = prop(p, "allValues").getArray())
                for (const auto &v : *a)
                    cpp += "p.allValues.push_back(" + number(v) + ");";
            cpp += "d.programs.push_back(std::move(p));}\n";
        }
    if (const auto *routes = prop(runtimeState, "voiceModulations").getArray())
        for (const auto &route : *routes)
            cpp += "d.voiceModulations.push_back({" + integer(prop(route, "source")) +
                   "ull,static_cast<engine::PolyVoiceTarget>(" + integer(prop(route, "target")) + ")," +
                   number(prop(route, "depth")) + "f});\n";
    cpp += "return d;}\nengine::NativeDspGraph makeAudioGraph(){engine::NativeDspGraph g;g.outputNodeId=" +
           integer(prop(native, "outputNodeId")) + "ull;\n";
    const juce::StringArray scalarFields{"driveDb",
                                         "gainDb",
                                         "cutoffHz",
                                         "resonance",
                                         "highpassCutoffHz",
                                         "clipThreshold",
                                         "stereoWidth",
                                         "pan",
                                         "oscillatorFrequencyHz",
                                         "noiseLevel",
                                         "delayMilliseconds",
                                         "feedback",
                                         "mix",
                                         "roomSize",
                                         "thresholdDb",
                                         "ratio",
                                         "tone",
                                         "wavetablePosition",
                                         "wavetableDetuneCents",
                                         "stringDamping",
                                         "stringDecaySeconds",
                                         "voiceMode",
                                         "glideMilliseconds",
                                         "oscillatorWaveform",
                                         "oscillatorLevel",
                                         "attackMilliseconds",
                                         "decayMilliseconds",
                                         "sustainLevel",
                                         "releaseMilliseconds",
                                         "modulationRateHz",
                                         "modulationDepth",
                                         "modulationOffset",
                                         "modulationShape",
                                         "valueOperand",
                                         "valueInputA",
                                         "valueInputB",
                                         "valueMinimum",
                                         "valueMaximum",
                                         "mapInputMinimum",
                                         "mapInputMaximum",
                                         "mapOutputMinimum",
                                         "mapOutputMaximum",
                                         "followerAttackSeconds",
                                         "followerReleaseSeconds",
                                         "midiTranspose",
                                         "midiMinimumNote",
                                         "midiMaximumNote",
                                         "midiVelocityScale",
                                         "effect1",
                                         "effect2",
                                         "effect3",
                                         "effect4",
                                         "effect5",
                                         "effect6",
                                         "effect7",
                                         "effect8",
                                         "synthModel"};
    for (const auto &n : *nodes)
    {
        cpp += "{engine::NativeDspNode n;n.id=" + integer(prop(n, "id")) +
               "ull;n.kind=static_cast<engine::NativeDspNodeKind>(" + integer(prop(n, "kind")) +
               ");n.inputBus=" + integer(prop(n, "inputBus")) + "u;\n";
        const auto values = prop(n, "parameters");
        for (const auto &key : scalarFields)
            if (values.hasProperty(key))
                cpp += "n.parameters." + key + "=" + number(values.getProperty(key, {})) + "f;\n";
        for (const auto *key : {"eqBands", "prism", "kick", "synth"})
            if (const auto *a = prop(values, key).getArray())
                for (int i = 0; i < a->size(); ++i)
                    cpp +=
                        "n.parameters." + juce::String(key) + "[" + juce::String(i) + "]=" + number((*a)[i]) + "f;\n";
        cpp += "g.nodes.push_back(std::move(n));}\n";
    }
    if (const auto *a = prop(native, "connections").getArray())
        for (const auto &c : *a)
            cpp += "g.connections.push_back({" + integer(prop(c, "source")) + "ull," + integer(prop(c, "destination")) +
                   "ull," + integer(prop(c, "sourcePort")) + "ull," + integer(prop(c, "destinationPort")) + "ull});\n";
    if (const auto *a = prop(native, "modulations").getArray())
        for (const auto &c : *a)
            cpp += "g.modulations.push_back({" + integer(prop(c, "source")) + "ull," + integer(prop(c, "destination")) +
                   "ull," + integer(prop(c, "sourcePort")) + "ull," + integer(prop(c, "destinationPort")) + "ull," +
                   quote(c, "destinationParameterKey") + "," + integer(prop(c, "mode")) + "," +
                   number(c.getProperty("amount", .5)) + "f," + number(c.getProperty("minimum", 0)) + "f," +
                   number(c.getProperty("maximum", 1)) + "f});\n";
    if (const auto *a = prop(native, "bypasses").getArray())
        for (const auto &b : *a)
            cpp += "g.bypasses.push_back({" + integer(prop(b, "node")) + "ull," + integer(prop(b, "from")) + "ull});\n";
    cpp += "return g;}\nengine::PolySynthGraphPlan makeInstrumentPlan(){engine::PolySynthGraphPlan p;\n";
    for (const auto *key :
         {"graphNodeCount", "voiceCount", "voiceMode", "transposeSemitones", "minimumMidiNote", "maximumMidiNote"})
        cpp += "p." + juce::String(key) + "=" + integer(prop(poly, key)) + ";\n";
    cpp += "p.enabled=" + juce::String(static_cast<bool>(prop(poly, "enabled")) ? "true" : "false") +
           ";p.waveform=static_cast<engine::PolyOscillatorWaveform>(" + integer(prop(poly, "waveform")) + ");\n";
    for (const auto *key :
         {"glideSeconds", "wavetablePosition", "stringDamping", "oscillatorLevel", "attackSeconds", "decaySeconds",
          "sustainLevel", "releaseSeconds", "filterCutoffHz", "filterResonance", "velocityScale"})
        cpp += "p." + juce::String(key) + "=" + number(prop(poly, key)) + "f;\n";
    cpp += "p.wavetableDetuneCents=" + number(prop(poly, "wavetableDetune")) +
           "f;p.stringDecaySeconds=" + number(prop(poly, "stringDecay")) + "f;\n";
    cpp += "p.synthModel=static_cast<engine::synth::Model>(" + integer(prop(poly, "synthModel")) + ");\n";
    for (const auto *key : {"prismValues", "kickValues", "synthValues"})
        if (const auto *a = prop(poly, key).getArray())
            for (int i = 0; i < a->size(); ++i)
                cpp += "p." + juce::String(key) + "[" + juce::String(i) + "]=" + number((*a)[i]) + "f;\n";
    if (const auto *effects = prop(poly, "midiEffects").getArray())
    {
        cpp += "{auto effects=std::make_shared<engine::MidiEffectsPlan>();effects->count=" +
               juce::String(effects->size()) + ";\n";
        for (int i = 0; i < effects->size(); ++i)
        {
            const auto &e = (*effects)[i];
            const auto target = "effects->effects[" + juce::String(i) + "]";
            cpp += target + ".kind=static_cast<engine::NativeDspNodeKind>(" + integer(prop(e, "kind")) + ");\n";
            if (const auto *a = prop(e, "values").getArray())
                for (int j = 0; j < a->size(); ++j)
                    cpp += target + ".values[" + juce::String(j) + "]=" + number((*a)[j]) + "f;\n";
        }
        cpp += "p.midiEffects=effects;}\n";
    }
    cpp +=
        "if(p.waveform==engine::PolyOscillatorWaveform::prism)p.prismTables=engine::prism::resolveTables(p.prismValues)"
        ";if(p.synthModel!=engine::synth::Model::none)engine::synth::warmUp(p.synthModel);return p;}\n}\n";
    const auto code = [](const std::string &s) {
        juce::uint32 result = 0;
        for (auto c : s)
            result = (result << 8) | static_cast<unsigned char>(c);
        return "0x" + juce::String::toHexString(result);
    };
    juce::String cmake = "cmake_minimum_required(VERSION 3.25)\nproject(CyDAWNativeExport VERSION " +
                         juce::String(plan.identity.version.toString()) +
                         " LANGUAGES C CXX)\nset(CYDAW_SOURCE_ROOT \"\" CACHE PATH \"CyDAW source tree\")\nif(NOT "
                         "EXISTS \"${CYDAW_SOURCE_ROOT}/src/native_plugin/NativeFormats.cmake\")\n message(FATAL_ERROR "
                         "\"Set CYDAW_SOURCE_ROOT to CyDAW sources with native VST3 export\")\nendif()\n";
    cmake += "set(CYDAW_PRODUCT_NAME " + cmakeString(plan.identity.productName) + ")\nset(CYDAW_COMPANY_NAME " +
             cmakeString(plan.identity.companyName) + ")\nset(CYDAW_CATEGORY " + cmakeString(plan.vst3Category) +
             ")\nset(CYDAW_VENDOR_ID " + code(plan.identity.manufacturerCode) + ")\nset(CYDAW_PRODUCT_ID " +
             code(plan.identity.pluginCode) + ")\nset(CYDAW_CUSTOM_SOURCES\n" + customSources +
             ")\ninclude(\"${CYDAW_SOURCE_ROOT}/src/native_plugin/NativeFormats.cmake\")\n";
    cmake = cmake.replace("include(\"${CYDAW_SOURCE_ROOT}/src/native_plugin/NativeFormats.cmake\")", "set(CYDAW_EXPORT_FORMAT " + format + " CACHE STRING \"VST3, CLAP or LV2\")\ninclude(\"${CYDAW_SOURCE_ROOT}/src/native_plugin/NativeFormats.cmake\")");
    const auto sdkLicense = format == "VST3" ? juce::String("VST3 SDK (MIT)") : format == "CLAP" ? juce::String("CLAP SDK (MIT)") : juce::String("LV2 headers (ISC)");
    const auto readme = "# " + juce::String(plan.identity.productName) +
                        "\n\nNative " + format + " generated by CyDAW. The compiled plug-in contains the fixed DSP topology and "
                        "its Dear ImGui interface; no graph editor or JUCE is linked.\n\ncmake -S . -B build "
                        "-DCYDAW_SOURCE_ROOT=PATH_TO_CYDAW [-DCYDAW_VST3_SDK_DIR=PATH_TO_VST3_SDK]\ncmake --build "
                        "build --config Release --target CyDAWGeneratedGraphPlugin_" + format + "\n\nFormat dependency: " + sdkLicense + ". Dear ImGui is MIT. The license of the embedded CyDAW engine and custom "
                        "code must also be respected. This generation does not relicense those sources.\n";
    metadata->setProperty("parameters", metadataParameters);
    if (!writeText(generated.getChildFile("Metadata.json"), juce::JSON::toString(juce::var(metadata.get()), true)) ||
        !writeText(generated.getChildFile("Definition.cpp"), cpp) ||
        !writeText(destination.getChildFile("CMakeLists.txt"), cmake) ||
        !writeText(destination.getChildFile("README.md"), readme) ||
        !writeText(destination.getChildFile("GraphManifest.cydawvst3.json"), juce::JSON::toString(manifest, true)))
        return juce::Result::fail("Cannot write native VST3 project files");
    return juce::Result::ok();
}

} // namespace cydaw::app
