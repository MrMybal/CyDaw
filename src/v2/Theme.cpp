#include "Theme.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>

namespace cydaw::v2::theme {
namespace {

constexpr ImU32 rgb(const unsigned int hex, const float a = 1.0f)
{
    const auto r = (hex >> 16) & 0xffu;
    const auto g = (hex >> 8) & 0xffu;
    const auto b = hex & 0xffu;
    const auto alphaByte = static_cast<unsigned int>(a * 255.0f + 0.5f) & 0xffu;
    return (alphaByte << 24) | (b << 16) | (g << 8) | r;
}

const Palette kPalette {
    rgb(0x111317), // bg0
    rgb(0x16181d), // bg1
    rgb(0x1c1f25), // bg2
    rgb(0x24282f), // bg3
    rgb(0x2c313a), // bg4
    rgb(0x363c47), // bg5
    rgb(0x262a31), // line
    rgb(0x363b45), // lineStrong
    rgb(0x1f2228), // grid
    rgb(0x2b2f37), // gridStrong
    rgb(0xdce1e8), // text
    rgb(0x9aa3b0), // textDim
    rgb(0x5f6875), // textFaint
    rgb(0xf0a43a), // accent
    rgb(0xf8b95c), // accentHover
    rgb(0x7a5524), // accentDim
    rgb(0x5ab0f0), // audio
    rgb(0x52d1a2), // midi
    rgb(0xf2c94c), // control
    rgb(0xc792ea), // modulation
    rgb(0xe5484d), // record
    rgb(0x3dd68c), // play
    rgb(0xf5a623), // warn
    rgb(0xf2d14c), // solo
    rgb(0x5aa9f0), // mute
    rgb(0x3dd68c), // meterLow
    rgb(0xe8c547), // meterMid
    rgb(0xe5484d), // meterHigh
    rgb(0xf0a43a, 0.22f) // selection
};

ImFont* gUi {};
ImFont* gBold {};
ImFont* gMono {};
float gDpi { 1.0f };
constexpr float kBaseSize = 14.0f;

juce::File fontsFolder()
{
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getSiblingFile("fonts");
}

juce::File windowsFont(const char* name)
{
    return juce::File::getSpecialLocation(juce::File::windowsSystemDirectory)
        .getParentDirectory()
        .getChildFile("Fonts")
        .getChildFile(name);
}

ImFont* addFont(const juce::File& primary, const juce::File& fallback, const float size)
{
    auto& io = ImGui::GetIO();
    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 2;
    config.PixelSnapH = true;
    for (const auto& file : { primary, fallback })
        if (file.existsAsFile())
            if (auto* font = io.Fonts->AddFontFromFileTTF(file.getFullPathName().toRawUTF8(), size, &config))
                return font;
    return io.Fonts->AddFontDefault();
}

void mergeIcons(const float size)
{
    const auto icons = windowsFont("segmdl2.ttf");
    if (!icons.existsAsFile())
        return;
    ImFontConfig config;
    config.MergeMode = true;
    config.PixelSnapH = true;
    config.GlyphOffset = ImVec2(0.0f, 2.0f);
    config.GlyphMinAdvanceX = size;
    ImGui::GetIO().Fonts->AddFontFromFileTTF(icons.getFullPathName().toRawUTF8(), size * 0.95f, &config);
}

} // namespace

const Palette& palette()
{
    return kPalette;
}

ImU32 alpha(const ImU32 colour, const float a)
{
    const auto current = static_cast<float>((colour >> 24) & 0xffu) / 255.0f;
    const auto byte = static_cast<unsigned int>(std::clamp(current * a, 0.0f, 1.0f) * 255.0f + 0.5f);
    return (colour & 0x00ffffffu) | (byte << 24);
}

ImU32 mix(const ImU32 a, const ImU32 b, const float t)
{
    const auto lerp = [t](const unsigned int x, const unsigned int y) {
        return static_cast<unsigned int>(static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * t + 0.5f);
    };
    unsigned int out = 0;
    for (auto shift = 0; shift < 32; shift += 8)
        out |= (lerp((a >> shift) & 0xffu, (b >> shift) & 0xffu) & 0xffu) << shift;
    return out;
}

ImU32 brighter(const ImU32 colour, const float amount)
{
    return mix(colour, (colour & 0xff000000u) | 0x00ffffffu, amount);
}

ImVec4 vec(const ImU32 colour)
{
    return ImGui::ColorConvertU32ToFloat4(colour);
}

ImU32 fromArgb(const unsigned int argb)
{
    const auto a = (argb >> 24) & 0xffu;
    const auto r = (argb >> 16) & 0xffu;
    const auto g = (argb >> 8) & 0xffu;
    const auto b = argb & 0xffu;
    return (a << 24) | (b << 16) | (g << 8) | r;
}

unsigned int toArgb(const ImU32 colour)
{
    const auto r = colour & 0xffu;
    const auto g = (colour >> 8) & 0xffu;
    const auto b = (colour >> 16) & 0xffu;
    const auto a = (colour >> 24) & 0xffu;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

ImU32 trackColour(const int index)
{
    static constexpr unsigned int colours[] = {
        0xf0a43a, 0x5ab0f0, 0x52d1a2, 0xc792ea, 0xe8765a, 0x7fd1e8,
        0xe8c547, 0x9ccc65, 0xf06292, 0x8c9eff, 0x4dd0b5, 0xffb74d
    };
    constexpr auto count = static_cast<int>(sizeof(colours) / sizeof(colours[0]));
    return rgb(colours[((index % count) + count) % count]);
}

float baseFontSize()
{
    return kBaseSize;
}

float dpi()
{
    return gDpi;
}

ImFont* uiFont()
{
    return gUi;
}

ImFont* boldFont()
{
    return gBold != nullptr ? gBold : gUi;
}

ImFont* monoFont()
{
    return gMono != nullptr ? gMono : gUi;
}

void loadFonts()
{
    auto& io = ImGui::GetIO();
    io.Fonts->Clear();
    gUi = addFont(windowsFont("segoeui.ttf"), fontsFolder().getChildFile("Roboto-Medium.ttf"), kBaseSize);
    mergeIcons(kBaseSize);
    gBold = addFont(windowsFont("seguisb.ttf"), fontsFolder().getChildFile("Roboto-Medium.ttf"), kBaseSize);
    mergeIcons(kBaseSize);
    gMono = addFont(windowsFont("consola.ttf"), fontsFolder().getChildFile("Cousine-Regular.ttf"), kBaseSize - 1.0f);
    io.FontDefault = gUi;
}

void apply(const float dpiScale)
{
    gDpi = std::max(0.5f, dpiScale);
    auto& style = ImGui::GetStyle();
    style = ImGuiStyle();
    const auto& p = kPalette;

    style.FontSizeBase = kBaseSize;
    style.WindowPadding = ImVec2(8.0f, 6.0f);
    style.FramePadding = ImVec2(6.0f, 3.0f);
    style.ItemSpacing = ImVec2(6.0f, 4.0f);
    style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
    style.CellPadding = ImVec2(5.0f, 2.0f);
    style.IndentSpacing = 14.0f;
    style.ScrollbarSize = 10.0f;
    style.ScrollbarPadding = 1.0f;
    style.GrabMinSize = 9.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;
    style.TabBarBorderSize = 1.0f;
    style.TabBarOverlineSize = 2.0f;
    style.WindowRounding = 6.0f;
    style.ChildRounding = 5.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 5.0f;
    style.MenuItemRounding = 4.0f;
    style.SelectableRounding = 4.0f;
    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.WindowMenuButtonPosition = ImGuiDir_None;
    style.SeparatorTextBorderSize = 1.0f;
    style.SeparatorTextPadding = ImVec2(10.0f, 2.0f);
    style.DockingSeparatorSize = 2.0f;
    style.DockingNodeHasCloseButton = false;
    style.WindowMinSize = ImVec2(60.0f, 40.0f);
    style.HoverDelayShort = 0.25f;
    style.HoverDelayNormal = 0.55f;
    style.AntiAliasedLines = true;
    style.AntiAliasedFill = true;

    auto* c = style.Colors;
    c[ImGuiCol_Text] = vec(p.text);
    c[ImGuiCol_TextDisabled] = vec(p.textFaint);
    c[ImGuiCol_WindowBg] = vec(p.bg1);
    c[ImGuiCol_ChildBg] = vec(alpha(p.bg2, 0.0f));
    c[ImGuiCol_PopupBg] = vec(alpha(p.bg2, 0.98f));
    c[ImGuiCol_Border] = vec(p.line);
    c[ImGuiCol_BorderShadow] = vec(0);
    c[ImGuiCol_FrameBg] = vec(p.bg3);
    c[ImGuiCol_FrameBgHovered] = vec(p.bg4);
    c[ImGuiCol_FrameBgActive] = vec(p.bg5);
    c[ImGuiCol_TitleBg] = vec(p.bg0);
    c[ImGuiCol_TitleBgActive] = vec(p.bg0);
    c[ImGuiCol_TitleBgCollapsed] = vec(p.bg0);
    c[ImGuiCol_MenuBarBg] = vec(p.bg0);
    c[ImGuiCol_ScrollbarBg] = vec(alpha(p.bg0, 0.0f));
    c[ImGuiCol_ScrollbarGrab] = vec(p.bg5);
    c[ImGuiCol_ScrollbarGrabHovered] = vec(brighter(p.bg5, 0.12f));
    c[ImGuiCol_ScrollbarGrabActive] = vec(p.accentDim);
    c[ImGuiCol_CheckMark] = vec(p.accent);
    c[ImGuiCol_CheckboxSelectedBg] = vec(p.bg4);
    c[ImGuiCol_SliderGrab] = vec(p.accent);
    c[ImGuiCol_SliderGrabActive] = vec(p.accentHover);
    c[ImGuiCol_Button] = vec(p.bg3);
    c[ImGuiCol_ButtonHovered] = vec(p.bg4);
    c[ImGuiCol_ButtonActive] = vec(p.bg5);
    c[ImGuiCol_Header] = vec(alpha(p.accent, 0.16f));
    c[ImGuiCol_HeaderHovered] = vec(p.bg4);
    c[ImGuiCol_HeaderActive] = vec(alpha(p.accent, 0.26f));
    c[ImGuiCol_Separator] = vec(p.line);
    c[ImGuiCol_SeparatorHovered] = vec(p.accentDim);
    c[ImGuiCol_SeparatorActive] = vec(p.accent);
    c[ImGuiCol_ResizeGrip] = vec(alpha(p.bg5, 0.0f));
    c[ImGuiCol_ResizeGripHovered] = vec(p.accentDim);
    c[ImGuiCol_ResizeGripActive] = vec(p.accent);
    c[ImGuiCol_InputTextCursor] = vec(p.accent);
    c[ImGuiCol_TabHovered] = vec(p.bg4);
    c[ImGuiCol_Tab] = vec(p.bg0);
    c[ImGuiCol_TabSelected] = vec(p.bg1);
    c[ImGuiCol_TabSelectedOverline] = vec(p.accent);
    c[ImGuiCol_TabDimmed] = vec(p.bg0);
    c[ImGuiCol_TabDimmedSelected] = vec(p.bg1);
    c[ImGuiCol_TabDimmedSelectedOverline] = vec(alpha(p.accent, 0.35f));
    c[ImGuiCol_DockingPreview] = vec(alpha(p.accent, 0.45f));
    c[ImGuiCol_DockingEmptyBg] = vec(p.bg0);
    c[ImGuiCol_PlotLines] = vec(p.audio);
    c[ImGuiCol_PlotLinesHovered] = vec(p.accent);
    c[ImGuiCol_PlotHistogram] = vec(p.accent);
    c[ImGuiCol_PlotHistogramHovered] = vec(p.accentHover);
    c[ImGuiCol_TableHeaderBg] = vec(p.bg2);
    c[ImGuiCol_TableBorderStrong] = vec(p.lineStrong);
    c[ImGuiCol_TableBorderLight] = vec(p.line);
    c[ImGuiCol_TableRowBg] = vec(0);
    c[ImGuiCol_TableRowBgAlt] = vec(rgb(0xffffff, 0.018f));
    c[ImGuiCol_TextLink] = vec(p.audio);
    c[ImGuiCol_TextSelectedBg] = vec(alpha(p.accent, 0.30f));
    c[ImGuiCol_TreeLines] = vec(p.line);
    c[ImGuiCol_DragDropTarget] = vec(p.accent);
    c[ImGuiCol_DragDropTargetBg] = vec(alpha(p.accent, 0.10f));
    c[ImGuiCol_UnsavedMarker] = vec(p.accent);
    c[ImGuiCol_NavCursor] = vec(p.accent);
    c[ImGuiCol_NavWindowingHighlight] = vec(alpha(p.accent, 0.7f));
    c[ImGuiCol_NavWindowingDimBg] = vec(rgb(0x000000, 0.35f));
    c[ImGuiCol_ModalWindowDimBg] = vec(rgb(0x000000, 0.45f));

    style.ScaleAllSizes(gDpi);
    // A panel in a window of its own: square corners, an opaque ground, as
    // the operating system's window around it is.
    style.WindowRounding = 0.0f;
    style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    style.FontScaleDpi = gDpi;
}

} // namespace cydaw::v2::theme
