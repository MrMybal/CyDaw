#include "Theme.h"
#include <cyui/Core.h>
namespace cydaw::v2::theme {
Palette extended(const cyui::Palette&);
const std::vector<Theme>& builtInThemes()
{
    static const auto themes = [] {
        std::vector<Theme> result;
        for (const auto& entry : cyui::builtInThemes()) {
            const auto key = entry.key == "amber-night" ? "dark" : entry.key;
            result.push_back({juce::String(key), juce::String(entry.name), extended(entry.palette), true, {}});
        }
        return result;
    }();
    return themes;
}
const std::vector<PaletteField>& paletteFields()
{
    static const std::vector<PaletteField> fields {
        { "bg0", "Fond le plus profond (timeline)", "Deepest ground (timeline)", 0, &Palette::bg0 },
        { "bg1", "Fond des panneaux", "Panel ground", 0, &Palette::bg1 },
        { "bg2", "Fond des en-têtes", "Header ground", 0, &Palette::bg2 },
        { "bg3", "Champs et boutons", "Fields and buttons", 0, &Palette::bg3 },
        { "bg4", "Survol", "Hover", 0, &Palette::bg4 },
        { "bg5", "Actif", "Active", 0, &Palette::bg5 },
        { "line", "Traits", "Lines", 1, &Palette::line },
        { "lineStrong", "Traits marqués", "Strong lines", 1, &Palette::lineStrong },
        { "grid", "Grille", "Grid", 1, &Palette::grid },
        { "gridStrong", "Grille (mesures)", "Grid (bars)", 1, &Palette::gridStrong },
        { "text", "Texte", "Text", 2, &Palette::text },
        { "textDim", "Texte secondaire", "Secondary text", 2, &Palette::textDim },
        { "textFaint", "Texte discret", "Faint text", 2, &Palette::textFaint },
        { "accent", "Accent", "Accent", 3, &Palette::accent },
        { "accentHover", "Accent survolé", "Accent hovered", 3, &Palette::accentHover },
        { "accentDim", "Accent atténué", "Accent dimmed", 3, &Palette::accentDim },
        { "selection", "Sélection", "Selection", 3, &Palette::selection },
        { "audio", "Audio", "Audio", 4, &Palette::audio },
        { "midi", "MIDI", "MIDI", 4, &Palette::midi },
        { "control", "Contrôle", "Control", 4, &Palette::control },
        { "modulation", "Modulation", "Modulation", 4, &Palette::modulation },
        { "record", "Enregistrement", "Record", 4, &Palette::record },
        { "play", "Lecture", "Play", 4, &Palette::play },
        { "warn", "Avertissement", "Warning", 4, &Palette::warn },
        { "solo", "Solo", "Solo", 4, &Palette::solo },
        { "mute", "Muet", "Mute", 4, &Palette::mute },
        { "meterLow", "Vu-mètre bas", "Meter low", 5, &Palette::meterLow },
        { "meterMid", "Vu-mètre moyen", "Meter mid", 5, &Palette::meterMid },
        { "meterHigh", "Vu-mètre haut", "Meter high", 5, &Palette::meterHigh },
    };
    return fields;
}

juce::var paletteToJson(const Palette& palette)
{
    auto* colours = new juce::DynamicObject();
    for (const auto& field : paletteFields()) {
        const auto colour = palette.*field.member;
        const auto r = colour & 0xffu;
        const auto g = (colour >> 8) & 0xffu;
        const auto b = (colour >> 16) & 0xffu;
        const auto a = (colour >> 24) & 0xffu;
        auto text = "#" + juce::String::toHexString(static_cast<int>((r << 16) | (g << 8) | b)).paddedLeft('0', 6).toUpperCase();
        if (a != 0xffu)
            text << juce::String::toHexString(static_cast<int>(a)).paddedLeft('0', 2).toUpperCase();
        colours->setProperty(field.key, text);
    }
    return juce::var(colours);
}

Palette paletteFromJson(const juce::var& colours, const Palette& base)
{
    auto palette = base;
    for (const auto& field : paletteFields()) {
        auto text = colours.getProperty(field.key, juce::var()).toString().trim();
        if (!text.startsWithChar('#') || (text.length() != 7 && text.length() != 9))
            continue;
        const auto value = static_cast<unsigned int>(text.substring(1, 7).getHexValue32());
        const auto a = text.length() == 9 ? static_cast<unsigned int>(text.substring(7).getHexValue32()) & 0xffu : 0xffu;
        palette.*field.member = (a << 24) | ((value & 0xffu) << 16) | (value & 0xff00u) | ((value >> 16) & 0xffu);
    }
    return palette;
}


void loadFonts()
{
    const auto folder = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("fonts").getFullPathName().toStdString();
    cyui::FontConfig fonts;
    fonts.fallbackFolder = folder.c_str();
    cyui::loadFonts(fonts);
}
}
