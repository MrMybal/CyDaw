#pragma once
#include <juce_core/juce_core.h>
namespace cydaw::v2::plugin_files
{
inline juce::File scripts()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("CyDAW/JSFX");
}
inline juce::Array<juce::File> folders()
{
    juce::Array<juce::File> result;
    auto user = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#if JUCE_WINDOWS
    auto common = juce::File::getSpecialLocation(juce::File::windowsProgramFilesCommon);
    const auto localPath=juce::SystemStats::getEnvironmentVariable("LOCALAPPDATA",{});
    if(juce::File::isAbsolutePath(localPath))for(auto format:{"VST3","CLAP"})result.add(juce::File(localPath).getChildFile("Programs/Common").getChildFile(format));
    for (auto format : {"VST3", "CLAP", "LV2"})
    {
        result.add(common.getChildFile(format));
        result.add(user.getChildFile("Programs/Common").getChildFile(format));
    }
    result.add(user.getChildFile("LV2"));
    result.add(user.getChildFile("CLAP"));
#elif JUCE_MAC
    for (auto format : {"VST3", "CLAP", "LV2"})
    {
        result.add(juce::File("/Library/Audio/Plug-Ins").getChildFile(format));
        result.add(juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                       .getChildFile("Library/Audio/Plug-Ins")
                       .getChildFile(format));
    }
#else
    for (auto format : {"vst3", "clap", "lv2"})
    {
        result.add(juce::File("/usr/lib").getChildFile(format));
        result.add(juce::File("/usr/local/lib").getChildFile(format));
        result.add(juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                       .getChildFile("." + juce::String(format)));
    }
#endif
    for (auto name : {"CLAP_PATH", "LV2_PATH"})
        for (auto path : juce::StringArray::fromTokens(juce::SystemStats::getEnvironmentVariable(name, {}),
#if JUCE_WINDOWS
                                                       ";",
#else
                                                       ":",
#endif
                                                       ""))
            if (juce::File::isAbsolutePath(path))
                result.add(juce::File(path));
    result.add(scripts());
    result.add(juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("JSFX"));
    result.add(user.getChildFile("REAPER/Effects"));
    return result;
}
inline bool candidate(const juce::File &file)
{
    const auto path = file.getFullPathName();
    for (auto extension : {".vst3", ".lv2", ".clap"})
        if (path.containsIgnoreCase(juce::String(extension) + juce::File::getSeparatorString()))
            return false;
    if (file.hasFileExtension("vst3;clap;lv2;jsfx;cyplugin"))
        return true;
    if (file.existsAsFile() && file.getFileExtension().isEmpty() && file.getSize() < 8 * 1024 * 1024)
    {
        auto stream = file.createInputStream();
        if (stream)
        {
            juce::MemoryBlock head;
            stream->readIntoMemoryBlock(head, 4096);
            return juce::String::fromUTF8(static_cast<const char *>(head.getData()), int(head.getSize()))
                .containsIgnoreCase("desc:");
        }
    }
    return false;
}
inline juce::String format(const juce::File &file)
{
    return file.hasFileExtension("vst3")   ? "VST3"
           : file.hasFileExtension("clap") ? "CLAP"
           : file.hasFileExtension("lv2")  ? "LV2"
                                           : "JSFX";
}
} // namespace cydaw::v2::plugin_files
