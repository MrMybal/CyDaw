#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>

namespace cydaw::v2::shell {
inline constexpr wchar_t projectProgId[] = L"CyDaw.Project";
inline constexpr int projectIconResource = 102;
struct AssociationResult {
    LSTATUS error { ERROR_SUCCESS };
    bool changed {};
    bool defaultAssigned {};
    explicit operator bool() const noexcept { return error == ERROR_SUCCESS; }
};
// The caller supplies a Classes root, allowing tests to use an isolated registry
// subtree. Existing handlers are preserved; Windows UserChoice is never changed.
AssociationResult registerProjectFilesAt(HKEY classesRoot, const std::wstring& executable,
                                         const std::wstring& inheritedHandler = {});
AssociationResult registerCurrentProjectFiles();
} // namespace cydaw::v2::shell
