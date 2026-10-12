#pragma once

#include <imgui.h>

// The look of CyDAW 2: one palette, one set of sizes, the fonts. Every panel
// draws with these, so the interface stays consistent and can be retuned in
// one place.

namespace cydaw::v2::theme {

struct Palette {
    // Grounds, from the deepest (timeline background) to the raised frames.
    ImU32 bg0;
    ImU32 bg1;
    ImU32 bg2;
    ImU32 bg3;
    ImU32 bg4;
    ImU32 bg5;
    // Lines.
    ImU32 line;
    ImU32 lineStrong;
    ImU32 grid;
    ImU32 gridStrong;
    // Text.
    ImU32 text;
    ImU32 textDim;
    ImU32 textFaint;
    // The one accent, for what is active or selected.
    ImU32 accent;
    ImU32 accentHover;
    ImU32 accentDim;
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
    ImU32 selection;
};

[[nodiscard]] const Palette& palette();

// Applies colours and sizes, scaled for the display.
void apply(float dpiScale);
// Interface font (Segoe UI with the icon glyphs merged), semibold and mono
// faces. Falls back to the fonts shipped with Dear ImGui when the Windows
// ones are missing.
void loadFonts();

[[nodiscard]] ImFont* uiFont();
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
