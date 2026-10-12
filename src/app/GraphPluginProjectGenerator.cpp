#include "GraphPluginProjectGenerator.h"

#include <algorithm>

namespace cydaw::app {
namespace {

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

bool writeText(const juce::File& file, const juce::String& text)
{
    return file.replaceWithText(text, false, false, "\n");
}

} // namespace

juce::Result generateGraphVst3Project(
    const juce::File& destination,
    const cydaw::graph::PluginExportPlan& plan,
    const juce::var& manifest,
    const juce::var& runtimeState)
{
    if (destination == juce::File {})
        return juce::Result::fail("No VST3 project directory was selected");
    if (manifest.isVoid() || runtimeState.isVoid())
        return juce::Result::fail("The Graph export state is incomplete");
    if (plan.parameters.size() > 32)
        return juce::Result::fail(
            "The generated runtime currently supports up to 32 published parameters");

    const auto audioBusCount = [&plan](const cydaw::graph::PortDirection direction) {
        return std::count_if(plan.buses.begin(), plan.buses.end(),
                             [direction](const auto& bus) {
                                 return bus.mediaType
                                            == cydaw::graph::PluginBusMediaType::audio
                                        && bus.direction == direction;
                             });
    };
    if (audioBusCount(cydaw::graph::PortDirection::input) > 1
        || audioBusCount(cydaw::graph::PortDirection::output) != 1)
        return juce::Result::fail(
            "This generated wrapper revision requires one audio output and at most one audio input bus");

    const auto channels = [&plan](const cydaw::graph::PortDirection direction) {
        const auto found = std::find_if(
            plan.buses.begin(), plan.buses.end(), [direction](const auto& bus) {
                return bus.mediaType
                           == cydaw::graph::PluginBusMediaType::audio
                       && bus.direction == direction;
            });
        return found != plan.buses.end() ? static_cast<int>(found->channels) : 0;
    };
    const auto inputChannels = channels(cydaw::graph::PortDirection::input);
    const auto outputChannels = channels(cydaw::graph::PortDirection::output);
    const auto busName = [&plan](const cydaw::graph::PortDirection direction,
                                 const juce::String& fallback) {
        const auto found = std::find_if(
            plan.buses.begin(), plan.buses.end(), [direction](const auto& bus) {
                return bus.mediaType
                           == cydaw::graph::PluginBusMediaType::audio
                       && bus.direction == direction;
            });
        return found != plan.buses.end() && !found->displayName.empty()
            ? juce::String(found->displayName) : fallback;
    };
    const auto inputName = busName(cydaw::graph::PortDirection::input,
                                   "Input");
    const auto outputName = busName(cydaw::graph::PortDirection::output,
                                    "Output");

    if (destination.createDirectory().failed())
        return juce::Result::fail("Cannot create the VST3 project directory");
    const auto generatedDirectory = destination.getChildFile("generated");
    if (generatedDirectory.createDirectory().failed())
        return juce::Result::fail("Cannot create the generated source directory");

    const auto stateJson = juce::JSON::toString(runtimeState, false);
    juce::String header;
    header << "#pragma once\n\n#include <array>\n#include <cstdint>\n\n"
           << "namespace cydaw::generated_graph {\n\n"
           << "struct ParameterConfig {\n"
           << "    std::uint32_t stableId {};\n"
           << "    const char* name {};\n    const char* unit {};\n"
           << "    float minimum {};\n    float maximum { 1.0f };\n"
           << "    float defaultValue {};\n"
           << "    bool automatable { true };\n};\n\n"
           << "inline constexpr bool exactWrapper = true;\n"
           << "inline constexpr char productName[] = "
           << cppString(plan.identity.productName) << ";\n"
           << "inline constexpr int mainInputChannels = " << inputChannels
           << ";\ninline constexpr int mainOutputChannels = "
           << outputChannels << ";\n"
           << "inline constexpr char mainInputName[] = "
           << cppString(inputName) << ";\n"
           << "inline constexpr char mainOutputName[] = "
           << cppString(outputName) << ";\n"
           << "inline constexpr bool acceptsMidi = "
           << (plan.acceptsMidi ? "true" : "false") << ";\n"
           << "inline constexpr bool producesMidi = "
           << (plan.producesMidi ? "true" : "false") << ";\n"
           << "inline constexpr std::array<ParameterConfig, "
           << static_cast<int>(plan.parameters.size()) << "> parameters {{\n";
    for (const auto& parameter : plan.parameters)
        header << "    ParameterConfig { 0x"
               << juce::String::toHexString(
                      static_cast<int>(parameter.pluginParameterId))
                      .paddedLeft('0', 8).toUpperCase()
               << "u, " << cppString(parameter.displayName) << ", "
               << cppString(parameter.unit) << ", "
               << juce::String(parameter.minimum, 9) << "f, "
               << juce::String(parameter.maximum, 9) << "f, "
               << juce::String(parameter.defaultValue, 9) << "f, "
               << (parameter.automatable ? "true" : "false") << " },\n";
    constexpr int stateChunkCharacters = 2048;
    const auto stateChunkCount = juce::jmax(
        1, (stateJson.length() + stateChunkCharacters - 1)
               / stateChunkCharacters);
    header << "}};\n\ninline constexpr std::array<const char*, "
           << stateChunkCount << "> embeddedStateJsonChunks {{\n";
    for (auto chunkIndex = 0; chunkIndex < stateChunkCount; ++chunkIndex) {
        const auto begin = chunkIndex * stateChunkCharacters;
        const auto chunk = stateJson.substring(
            begin, juce::jmin(stateJson.length(),
                              begin + stateChunkCharacters));
        header << "    " << cppString(chunk) << ",\n";
    }
    header << "}};\n\n} // namespace cydaw::generated_graph\n";

    auto categories = juce::String(plan.vst3Category).replace("|", " ");
    if (categories.isEmpty())
        categories = plan.isSynth ? "Instrument Synth" : "Fx";
    auto bundleName = slug(plan.identity.companyName) + "."
                      + slug(plan.identity.productName);
    if (bundleName.startsWithChar('.') || bundleName.endsWithChar('.'))
        bundleName = "user." + slug(plan.identity.productName);

    juce::String cmake;
    cmake << "cmake_minimum_required(VERSION 3.24)\n"
          << "project(CyDAWGeneratedGraphPlugin LANGUAGES C CXX)\n\n"
          << "set(CYDAW_SOURCE_ROOT \"\" CACHE PATH "
             "\"Path to the CyDAW source tree\")\n"
          << "if(NOT CYDAW_SOURCE_ROOT)\n"
          << "  message(FATAL_ERROR \"Set CYDAW_SOURCE_ROOT to the CyDAW source tree\")\n"
          << "endif()\n"
          << "set(CYDAW_BUILD_TESTS OFF CACHE BOOL \"\" FORCE)\n"
          << "set(CYDAW_BUILD_GRAPH_EDITOR OFF CACHE BOOL \"\" FORCE)\n"
          << "set(CYDAW_BUILD_GRAPH_RUNTIME_PLUGIN OFF CACHE BOOL \"\" FORCE)\n"
          << "add_subdirectory(\"${CYDAW_SOURCE_ROOT}\" cydaw-sdk)\n"
          << "if(NOT COMMAND cydaw_add_graph_runtime_plugin)\n"
          << "  message(FATAL_ERROR \"CyDAW was configured without JUCE desktop support\")\n"
          << "endif()\n\n"
          << "cydaw_add_graph_runtime_plugin(CyDAWGeneratedGraphPlugin\n"
          << "  PRODUCT_NAME " << cmakeString(plan.identity.productName) << "\n"
          << "  COMPANY_NAME " << cmakeString(plan.identity.companyName) << "\n"
          << "  BUNDLE_ID " << cmakeString("audio." + bundleName) << "\n"
          << "  MANUFACTURER_CODE " << plan.identity.manufacturerCode << "\n"
          << "  PLUGIN_CODE " << plan.identity.pluginCode << "\n"
          << "  VERSION " << cmakeString(plan.identity.version.toString()) << "\n"
          << "  CONFIG_DIRECTORY \"${CMAKE_CURRENT_LIST_DIR}/generated\"\n"
          << "  CATEGORIES " << categories << "\n"
          << (plan.isSynth ? "  IS_SYNTH\n" : "")
          << (plan.acceptsMidi ? "  ACCEPTS_MIDI\n" : "")
          << (plan.producesMidi ? "  PRODUCES_MIDI\n" : "")
          << ")\n";

    juce::String readme;
    readme << "# " << juce::String(plan.identity.productName) << " - VST3 Graph\n\n"
           << "This project was generated by CyDAW Graph Editor. The complete Graph state is embedded in the plug-in binary.\n\n"
           << "## Configure and build\n\n"
           << "```powershell\n"
           << "cmake -S . -B build -DCYDAW_SOURCE_ROOT=PATH_TO_CYDAW "
              "-DCYDAW_JUCE_SOURCE_DIR=PATH_TO_JUCE\n"
           << "cmake --build build --config Release --target CyDAWGeneratedGraphPlugin_VST3\n"
           << "```\n\n"
           << "The resulting VST3 bundle is self-contained and does not require CyDAW at run time.\n";

    const auto manifestJson = juce::JSON::toString(manifest, true);
    if (!writeText(generatedDirectory.getChildFile(
                       "CyDawGeneratedGraphConfig.h"), header)
        || !writeText(destination.getChildFile("CMakeLists.txt"), cmake)
        || !writeText(destination.getChildFile("README.md"), readme)
        || !writeText(destination.getChildFile("GraphManifest.cydawvst3.json"),
                      manifestJson))
        return juce::Result::fail(
            "One or more generated VST3 project files could not be written");
    return juce::Result::ok();
}

} // namespace cydaw::app
