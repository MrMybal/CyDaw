// Where the plug-in's own files are: beside its binary, in its bundle. Kept
// apart so the system headers it needs stay out of the processor.
#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include "NativeResources.h"

#include <system_error>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace cydaw::native
{
namespace
{
std::filesystem::path moduleFile()
{
#ifdef _WIN32
    HMODULE module{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&moduleFile), &module))
        return {};
    std::wstring path(32768, L'\0');
    const auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
        return {};
    path.resize(length);
    return path;
#else
    Dl_info info{};
    if (dladdr(reinterpret_cast<const void *>(&moduleFile), &info) == 0 || info.dli_fname == nullptr)
        return {};
    return info.dli_fname;
#endif
}
} // namespace

std::filesystem::path findResource(const std::string &name)
{
    const auto binary = moduleFile();
    if (binary.empty() || name.empty())
        return {};
    const auto folder = binary.parent_path();
    auto beside = binary.filename();
    beside += ".Resources";
    // VST3: Contents/<architecture>/binary, the files in Contents/Resources.
    // CLAP: a single file, the files in "<name>.clap.Resources" beside it.
    // LV2: the bundle's own folder holds the binary, the files in Resources.
    const std::filesystem::path candidates[] = {
        folder.parent_path() / "Resources" / name,
        folder / beside / name,
        folder / "Resources" / name,
        folder / name,
    };
    for (const auto &candidate : candidates)
    {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error))
            return candidate;
    }
    return {};
}
} // namespace cydaw::native
