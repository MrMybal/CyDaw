#include "Theme.h"
#include <cyui/Core.h>
#include <imgui_internal.h>
#include <algorithm>
#include <map>

namespace cydaw::v2::theme {
Palette extended(const cyui::Palette& common)
{
    Palette p {};
    static_cast<cyui::Palette&>(p) = common;
    p.audio = common.info;
    p.midi = common.success;
    p.control = common.warning;
    p.modulation = cyui::mix(common.info, common.accent, 0.5f);
    p.record = common.error;
    p.play = p.meterLow = common.success;
    p.warn = p.solo = p.meterMid = common.warning;
    p.mute = common.info;
    p.meterHigh = common.error;
    return p;
}
namespace {
struct ContextPalettes {
    std::map<ImGuiContext*, Palette> values;
    Palette defaults = extended(cyui::builtInThemes().front().palette);
};
ContextPalettes& contexts() { static ContextPalettes state; return state; }
Palette& current()
{
    auto* context = ImGui::GetCurrentContext();
    if (context == nullptr) return contexts().defaults;
    auto [it, inserted] = contexts().values.try_emplace(context, contexts().defaults);
    if (inserted) {
        ImGuiContextHook hook;
        hook.Type = ImGuiContextHookType_Shutdown;
        hook.Callback = [](ImGuiContext* ctx, ImGuiContextHook*) { contexts().values.erase(ctx); };
        ImGui::AddContextHook(context, &hook);
    }
    return it->second;
}
}
const Palette& palette()
{
    auto& result = current();
    static_cast<cyui::Palette&>(result) = cyui::palette();
    return result;
}
void setPalette(const Palette& p)
{
    if (ImGui::GetCurrentContext() == nullptr) {
        contexts().defaults = p;
        for (auto& [context, value] : contexts().values) { (void)context; value = p; }
    } else current() = p;
    auto common = static_cast<const cyui::Palette&>(p);
    // Existing CyDAW theme files expose meter colors separately. Feed those
    // roles to the common audio renderer instead of silently ignoring them.
    common.success = p.meterLow;
    common.warning = p.meterMid;
    common.error = p.meterHigh;
    cyui::setPalette(common);
}

ImU32 alpha(ImU32 c, float a) { return cyui::alpha(c, a); }
ImU32 mix(ImU32 a, ImU32 b, float t) { return cyui::mix(a, b, t); }
ImU32 brighter(ImU32 c, float a) { return cyui::brighter(c, a); }
ImVec4 vec(ImU32 c) { return cyui::vec(c); }
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


ImU32 trackColour(int index) { return cyui::categorical(index); }
float baseFontSize() { return cyui::metrics().fontSize; }
float dpi() { return cyui::dpi(); }
ImFont* uiFont() { return cyui::uiFont(); }
ImFont* boldFont() { return cyui::boldFont(); }
ImFont* monoFont() { return cyui::monoFont(); }
void useDpi(float scale) { cyui::applyDpi(std::max(0.5f, scale)); }

void apply(float scale)
{
    const auto colours = cyui::palette();
    cyui::Config config;
    config.themeKey = cyui::currentThemeKey().c_str();
    config.density = cyui::density();
    config.dpiScale = std::max(0.5f, scale);
    config.squareWindows = true;
    cyui::apply(config);
    cyui::setPalette(colours);
}
} // namespace cydaw::v2::theme
