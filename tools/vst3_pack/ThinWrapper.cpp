#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include "RuntimeDefinition.h"
#include <windows.h>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    HMODULE module {};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&createPluginFilter),&module);
    wchar_t path[32768] {};
    GetModuleFileNameW(module,path,32768);
    const juce::File binary {juce::String(path)};
    return createCatalogueProcessor(binary.getParentDirectory().getParentDirectory().getChildFile("Resources/plugin.json"));
}
