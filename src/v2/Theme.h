#pragma once

#include <cyui/Theme.h>

#include <juce_core/juce_core.h>

#include <vector>

// The look of CyDAW 2: one palette, one set of sizes, the fonts. Every panel
// draws with these, so the interface stays consistent and can be retuned in
// one place. The palette is the theme's: a built-in one, or one the person
// made (colours only).

namespace cydaw::v2::theme {

// CyDAW-specific signal roles extend the shared semantic palette.
struct Palette : cyui::Palette {
    // Meaning colours: signal kinds and transport states.
    ImU32 audio;
    ImU32 midi;
    ImU32 control;
    ImU32 modulation;
    ImU32 record;
    ImU32 play;
    ImU32 warn;
    ImU32 solo;
    ImU32 mute;
    ImU32 meterLow;
    ImU32 meterMid;
    ImU32 meterHigh;
};

[[nodiscard]] const Palette& palette();
// The palette drawn with from the next frame on, ImGui's own colours with it.
void setPalette(const Palette& palette);

// A colour theme: a built-in one, or a file of the person's (.cytheme).
struct Theme {
    juce::String key;
    juce::String name;
    Palette palette {};
    bool builtIn {};
    juce::File file;
};
// Shared built-in themes with legacy CyDAW keys. Mid grey remains the default;
// ThemesView translates their display names in the application's language.
[[nodiscard]] const std::vector<Theme>& builtInThemes();
inline constexpr const char* defaultThemeKey = "mid-grey";
// The palette's colours by name, in the order an editor shows them, with
// the group each belongs to.
struct PaletteField {
    const char* key;
    const char* french;
    const char* english;
    int group; // 0 grounds, 1 lines, 2 text, 3 accent, 4 signals, 5 meters
    ImU32 Palette::*member;
};
[[nodiscard]] const std::vector<PaletteField>& paletteFields();
// A theme file's colours ("#RRGGBB" or "#RRGGBBAA"), and back; what a file
// lacks keeps `base`'s.
[[nodiscard]] juce::var paletteToJson(const Palette& palette);
[[nodiscard]] Palette paletteFromJson(const juce::var& colours, const Palette& base);

// Applies colours and sizes, scaled for the display.
void apply(float dpiScale);
// Interface font (Segoe UI with the icon glyphs merged), semibold and mono
// faces. Falls back to the fonts shipped with Dear ImGui when the Windows
// ones are missing.
void loadFonts();

[[nodiscard]] ImFont* uiFont();
// The scale the next frame draws at (a context made at another), without
// touching the style.
void useDpi(float dpiScale);
[[nodiscard]] ImFont* boldFont();
[[nodiscard]] ImFont* monoFont();
// The base size the interface text is drawn at, before the display scale.
[[nodiscard]] float baseFontSize();
[[nodiscard]] float dpi();

[[nodiscard]] ImU32 alpha(ImU32 colour, float alpha);
[[nodiscard]] ImU32 mix(ImU32 a, ImU32 b, float t);
[[nodiscard]] ImU32 brighter(ImU32 colour, float amount);
[[nodiscard]] ImVec4 vec(ImU32 colour);
// A juce-style 0xAARRGGBB colour, as stored in projects, to ImGui's ABGR.
[[nodiscard]] ImU32 fromArgb(unsigned int argb);
[[nodiscard]] unsigned int toArgb(ImU32 colour);
// Colour of the n-th track when it has none of its own.
[[nodiscard]] ImU32 trackColour(int index);

} // namespace cydaw::v2::theme
