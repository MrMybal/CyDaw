#include "ProjectFileAssociation.h"
#include <shellapi.h>
#include <shlobj.h>
#include <filesystem>
#include <vector>
#include <utility>

namespace cydaw::v2::shell {
namespace {
struct Key {
    HKEY value {};
    ~Key() { if (value) RegCloseKey(value); }
};
LSTATUS readText(HKEY root, const wchar_t* key, const wchar_t* name, std::wstring& value)
{
    DWORD size {};
    auto status = RegGetValueW(root, key, name, RRF_RT_REG_SZ, nullptr, nullptr, &size);
    if (status != ERROR_SUCCESS) return status;
    std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1, L'\0');
    status = RegGetValueW(root, key, name, RRF_RT_REG_SZ, nullptr, buffer.data(), &size);
    if (status == ERROR_SUCCESS) value = buffer.data();
    return status;
}
LSTATUS setText(HKEY root, const wchar_t* path, const wchar_t* name,
                const std::wstring& value, bool& changed)
{
    std::wstring old;
    if (readText(root, path, name, old) == ERROR_SUCCESS && old == value) return ERROR_SUCCESS;
    Key key;
    auto status = RegCreateKeyExW(root, path, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key.value, nullptr);
    if (status != ERROR_SUCCESS) return status;
    status = RegSetValueExW(key.value, name, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(value.c_str()), static_cast<DWORD>((value.size()+1)*sizeof(wchar_t)));
    if (status == ERROR_SUCCESS) changed = true;
    return status;
}
}
AssociationResult registerProjectFilesAt(HKEY classesRoot, const std::wstring& executable,
                                         const std::wstring& inheritedHandler)
{
    AssociationResult result;
    if (executable.empty() || executable.find(L'"') != std::wstring::npos
        || !std::filesystem::path(executable).is_absolute()) {
        result.error = ERROR_INVALID_PARAMETER;
        return result;
    }
    const auto quoted = L"\"" + executable + L"\"";
    for (const auto& [path, value] : {
        std::pair<std::wstring,std::wstring>{projectProgId, L"CyDaw project"},
        {std::wstring(projectProgId)+L"\\DefaultIcon", quoted+L",-"+std::to_wstring(projectIconResource)},
        {std::wstring(projectProgId)+L"\\shell", L"open"},
        {std::wstring(projectProgId)+L"\\shell\\open\\command", quoted+L" \"%1\""}}) {
        result.error = setText(classesRoot, path.c_str(), nullptr, value, result.changed);
        if (!result) return result;
    }
    Key openWith;
    result.error = RegCreateKeyExW(classesRoot, L".cydaw\\OpenWithProgids", 0, nullptr, 0,
        KEY_QUERY_VALUE|KEY_SET_VALUE, nullptr, &openWith.value, nullptr);
    if (!result) return result;
    DWORD type {}, size {};
    if (RegQueryValueExW(openWith.value, projectProgId, nullptr, &type, nullptr, &size) != ERROR_SUCCESS) {
        result.error = RegSetValueExW(openWith.value, projectProgId, 0, REG_NONE, nullptr, 0);
        if (!result) return result;
        result.changed = true;
    }
    std::wstring existing;
    result.error = readText(classesRoot, L".cydaw", nullptr, existing);
    if (result.error == ERROR_FILE_NOT_FOUND || (result.error == ERROR_SUCCESS && existing.empty())) {
        if (!inheritedHandler.empty() && inheritedHandler != projectProgId) {
            result.error = ERROR_SUCCESS;
        } else {
            result.error = setText(classesRoot, L".cydaw", nullptr, projectProgId, result.changed);
            result.defaultAssigned = result.error == ERROR_SUCCESS;
        }
    } else if (result.error == ERROR_SUCCESS) {
        result.defaultAssigned = existing == projectProgId;
    }
    return result;
}
AssociationResult registerCurrentProjectFiles()
{
    std::wstring executable(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (!length || length >= executable.size()) return {static_cast<LSTATUS>(length ? ERROR_INSUFFICIENT_BUFFER : GetLastError())};
    executable.resize(length);
    Key classes;
    const auto status = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes", 0, nullptr, 0,
        KEY_READ|KEY_WRITE, nullptr, &classes.value, nullptr);
    if (status != ERROR_SUCCESS) return {status};
    std::wstring inheritedHandler;
    (void) readText(HKEY_CLASSES_ROOT, L".cydaw", nullptr, inheritedHandler);
    const auto result = registerProjectFilesAt(classes.value, executable, inheritedHandler);
    if (result && result.changed) SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return result;
}
} // namespace cydaw::v2::shell
