#include <cydaw/base/File.h>
#include <cydaw/base/Text.h>
#pragma once

namespace cydaw::v2::plugin_files
{
inline cydaw::base::File scripts()
{
    return cydaw::base::File::getSpecialLocation(cydaw::base::File::userDocumentsDirectory).getChildFile("CyDAW/JSFX");
}
inline cydaw::base::Array<cydaw::base::File> folders()
{
    cydaw::base::Array<cydaw::base::File> result;
    auto user = cydaw::base::File::getSpecialLocation(cydaw::base::File::userApplicationDataDirectory);
#if defined(_WIN32)
    auto common = cydaw::base::File::getSpecialLocation(cydaw::base::File::windowsProgramFilesCommon);
    const auto localPath=cydaw::base::SystemStats::getEnvironmentVariable("LOCALAPPDATA",{});
    if(cydaw::base::File::isAbsolutePath(localPath))for(auto format:{"VST3","CLAP"})result.add(cydaw::base::File(localPath).getChildFile("Programs/Common").getChildFile(format));
    for (auto format : {"VST3", "CLAP", "LV2"})
    {
        result.add(common.getChildFile(format));
        result.add(user.getChildFile("Programs/Common").getChildFile(format));
    }
    result.add(user.getChildFile("LV2"));
    result.add(user.getChildFile("CLAP"));
#elif defined(__APPLE__)
    for (auto format : {"VST3", "CLAP", "LV2"})
    {
        result.add(cydaw::base::File("/Library/Audio/Plug-Ins").getChildFile(format));
        result.add(cydaw::base::File::getSpecialLocation(cydaw::base::File::userHomeDirectory)
                       .getChildFile("Library/Audio/Plug-Ins")
                       .getChildFile(format));
    }
#else
    for (auto format : {"vst3", "clap", "lv2"})
    {
        result.add(cydaw::base::File("/usr/lib").getChildFile(format));
        result.add(cydaw::base::File("/usr/local/lib").getChildFile(format));
        result.add(cydaw::base::File::getSpecialLocation(cydaw::base::File::userHomeDirectory)
                       .getChildFile("." + cydaw::base::Text(format)));
    }
#endif
    for (auto name : {"CLAP_PATH", "LV2_PATH"})
        for (auto path : cydaw::base::TextList::fromTokens(cydaw::base::SystemStats::getEnvironmentVariable(name, {}),
#if defined(_WIN32)
                                                       ";",
#else
                                                       ":",
#endif
                                                       ""))
            if (cydaw::base::File::isAbsolutePath(path))
                result.add(cydaw::base::File(path));
    result.add(scripts());
    result.add(cydaw::base::File::getSpecialLocation(cydaw::base::File::currentExecutableFile).getSiblingFile("JSFX"));
    result.add(user.getChildFile("REAPER/Effects"));
    return result;
}
inline bool candidate(const cydaw::base::File &file)
{
    const auto path = file.getFullPathName();
    for (auto extension : {".vst3", ".lv2", ".clap"})
        if (path.containsIgnoreCase(cydaw::base::Text(extension) + cydaw::base::File::getSeparatorString()))
            return false;
    if (file.hasFileExtension("vst3;clap;lv2;jsfx;cyplugin"))
        return true;
    if (file.existsAsFile() && file.getFileExtension().isEmpty() && file.getSize() < 8 * 1024 * 1024)
    {
        auto stream = file.createInputStream();
        if (stream)
        {
            cydaw::base::MemoryBlock head;
            stream->readIntoMemoryBlock(head, 4096);
            return cydaw::base::Text::fromUTF8(static_cast<const char *>(head.getData()), int(head.getSize()))
                .containsIgnoreCase("desc:");
        }
    }
    return false;
}
inline cydaw::base::Text format(const cydaw::base::File &file)
{
    return file.hasFileExtension("vst3")   ? "VST3"
           : file.hasFileExtension("clap") ? "CLAP"
           : file.hasFileExtension("lv2")  ? "LV2"
                                           : "JSFX";
}
} // namespace cydaw::v2::plugin_files
