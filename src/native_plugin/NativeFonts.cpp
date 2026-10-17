#include "Theme.h"
#if defined(_WIN32)
#include <windows.h>
#endif
#include <string>
namespace cydaw::v2::theme
{
void loadFonts()
{
#if defined(_WIN32)
    char windows[MAX_PATH]{};
    const auto count = GetWindowsDirectoryA(windows, MAX_PATH);
    const auto folder = count > 0 && count < MAX_PATH ? std::string(windows) + "/Fonts/" : std::string();
    auto &fonts = *ImGui::GetIO().Fonts;
    for (const auto *name : {"segoeui.ttf", "seguisb.ttf", "consola.ttf"})
    {
        auto *font = folder.empty() || GetFileAttributesA((folder + name).c_str()) == INVALID_FILE_ATTRIBUTES
                         ? nullptr
                         : fonts.AddFontFromFileTTF((folder + name).c_str(), 14);
        if (!font)
            fonts.AddFontDefault();
        if (!folder.empty() && GetFileAttributesA((folder + "segmdl2.ttf").c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            ImFontConfig icons;
            icons.MergeMode = true;
            icons.PixelSnapH = true;
            fonts.AddFontFromFileTTF((folder + "segmdl2.ttf").c_str(), 13, &icons);
        }
    }
#else
    auto &fonts = *ImGui::GetIO().Fonts;
    for (int i=0;i<3;++i) fonts.AddFontDefault();
#endif
}
} // namespace cydaw::v2::theme
