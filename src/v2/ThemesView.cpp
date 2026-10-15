#include "ThemesView.h"

#include "Locations.h"
#include "Widgets.h"

#include <algorithm>

namespace cydaw::v2 {

using ui::dp;
using ui::L;

ThemesView::ThemesView(Studio& studio, UiState& state) : studio_(studio), state_(state) {}

juce::File ThemesView::folder()
{
    return locations::folder(Location::presets).getChildFile("Themes");
}

void ThemesView::reload()
{
    loaded_ = true;
    language_ = ui::languageRevision();
    themes_ = theme::builtInThemes();
    // Their names in the interface's language.
    for (auto& entry : themes_)
        entry.name = entry.key == "mid-grey"     ? ui::Ls("Gris moyen", "Mid grey")
                     : entry.key == "light-grey" ? ui::Ls("Gris clair", "Light grey")
                     : entry.key == "dark"       ? ui::Ls("Sombre (d'origine)", "Dark (the first)")
                                                 : entry.name;
    std::vector<theme::Theme> own;
    for (const auto& file : folder().findChildFiles(juce::File::findFiles, false, "*.cytheme")) {
        const auto json = juce::JSON::parse(file);
        if (json.getProperty("format", juce::var()).toString() != "CyDAW Theme")
            continue;
        theme::Theme entry;
        entry.key = "file:" + file.getFileNameWithoutExtension();
        entry.name = json.getProperty("name", file.getFileNameWithoutExtension()).toString();
        // What a file lacks: the default theme's.
        entry.palette = theme::paletteFromJson(json.getProperty("colours", juce::var()), theme::builtInThemes().front().palette);
        entry.file = file;
        own.push_back(std::move(entry));
    }
    std::sort(own.begin(), own.end(), [](const auto& a, const auto& b) { return a.name.compareNatural(b.name) < 0; });
    themes_.insert(themes_.end(), own.begin(), own.end());
}

const std::vector<theme::Theme>& ThemesView::themes()
{
    if (!loaded_ || language_ != ui::languageRevision())
        reload();
    return themes_;
}

const theme::Theme* ThemesView::find(const juce::String& key)
{
    for (const auto& entry : themes())
        if (entry.key == key)
            return &entry;
    return nullptr;
}

void ThemesView::applySaved()
{
    const auto key = studio_.settings().getValue("theme", theme::defaultThemeKey);
    if (!choose(key))
        (void) choose(theme::defaultThemeKey);
}

bool ThemesView::choose(const juce::String& key)
{
    const auto* found = find(key);
    if (found == nullptr)
        return false;
    theme::setPalette(found->palette);
    current_ = key;
    if (studio_.keepsSettings())
        studio_.settings().setValue("theme", key);
    return true;
}

juce::String ThemesView::createFrom(const juce::String& from, const juce::String& name)
{
    const auto* base = find(from);
    if (base == nullptr)
        return {};
    const auto dir = folder();
    (void) dir.createDirectory();
    auto stem = juce::File::createLegalFileName(name.trim().isNotEmpty() ? name.trim() : juce::String("Theme"));
    auto file = dir.getNonexistentChildFile(stem, ".cytheme", false);
    const auto palette = base->palette;
    const auto key = "file:" + file.getFileNameWithoutExtension();
    auto* root = new juce::DynamicObject();
    root->setProperty("format", "CyDAW Theme");
    root->setProperty("name", name.trim().isNotEmpty() ? name.trim() : file.getFileNameWithoutExtension());
    root->setProperty("colours", theme::paletteToJson(palette));
    if (!file.replaceWithText(juce::JSON::toString(juce::var(root))))
        return {};
    reload();
    return key;
}

bool ThemesView::save(const juce::String& key, const juce::String& name, const theme::Palette& palette)
{
    const auto* found = find(key);
    if (found == nullptr || found->builtIn)
        return false;
    auto* root = new juce::DynamicObject();
    root->setProperty("format", "CyDAW Theme");
    root->setProperty("name", name);
    root->setProperty("colours", theme::paletteToJson(palette));
    if (!found->file.replaceWithText(juce::JSON::toString(juce::var(root))))
        return false;
    for (auto& entry : themes_)
        if (entry.key == key) {
            entry.name = name;
            entry.palette = palette;
        }
    return true;
}

bool ThemesView::remove(const juce::String& key)
{
    const auto* found = find(key);
    if (found == nullptr || found->builtIn)
        return false;
    const auto file = found->file;
    if (!file.moveToTrash() && !file.deleteFile())
        return false;
    reload();
    if (current_ == key)
        (void) choose(theme::defaultThemeKey);
    return true;
}

void ThemesView::draw()
{
    if (!state_.showThemes)
        return;
    ImGui::SetNextWindowSize(ImVec2(dp(820.0f), dp(620.0f)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(ui::labelId(L("Thèmes", "Themes"), "themes").c_str(), &state_.showThemes)) {
        ImGui::End();
        return;
    }
    const auto& p = theme::palette();
    const auto& all = themes();
    if (shown_.isEmpty() || find(shown_) == nullptr)
        shown_ = current_;

    // The list: a click puts the theme on (and shows its colours).
    ImGui::BeginChild("##themeList", ImVec2(dp(250.0f), 0.0f), ImGuiChildFlags_Borders);
    ui::sectionTitle(L("Thèmes de CyDAW", "CyDAW's themes"));
    auto ownTitle = false;
    for (const auto& entry : all) {
        if (!entry.builtIn && !ownTitle) {
            ownTitle = true;
            ImGui::Spacing();
            ui::sectionTitle(L("Mes thèmes", "My themes"));
        }
        ImGui::PushID(entry.key.toRawUTF8());
        // Its colours in a strip before its name.
        const auto at = ImGui::GetCursorScreenPos();
        const auto height = ImGui::GetFrameHeight();
        auto* list = ImGui::GetWindowDrawList();
        const ImU32 swatches[] = { entry.palette.bg0, entry.palette.bg2, entry.palette.bg4, entry.palette.text, entry.palette.accent };
        if (ImGui::Selectable("##theme", entry.key == current_, ImGuiSelectableFlags_None, ImVec2(0.0f, height))) {
            (void) choose(entry.key);
            shown_ = entry.key;
            std::snprintf(name_, sizeof(name_), "%s", entry.name.toRawUTF8());
        }
        for (std::size_t index = 0; index < std::size(swatches); ++index)
            list->AddRectFilled(ImVec2(at.x + dp(4.0f) + static_cast<float>(index) * dp(9.0f), at.y + dp(4.0f)),
                                ImVec2(at.x + dp(12.0f) + static_cast<float>(index) * dp(9.0f), at.y + height - dp(4.0f)), swatches[index]);
        list->AddText(ImVec2(at.x + dp(56.0f), at.y + (height - ImGui::GetFontSize()) * 0.5f), entry.key == current_ ? p.accent : p.text,
                      entry.name.toRawUTF8());
        ImGui::PopID();
    }
    if (!ownTitle) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(theme::vec(p.textFaint), "%s",
                           L("Aucun thème à vous : « Créer à partir de celui-ci » en fait un, à changer à votre goût.",
                             "No theme of your own yet: \"Make one from this\" makes one, to change as you like."));
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##themeColours", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    const auto* shown = find(shown_);
    if (shown != nullptr) {
        const auto key = shown->key;
        const auto builtIn = shown->builtIn;
        auto palette = shown->palette;
        if (name_[0] == 0 || !builtIn)
            if (juce::String::fromUTF8(name_) != shown->name && !ImGui::IsAnyItemActive())
                std::snprintf(name_, sizeof(name_), "%s", shown->name.toRawUTF8());
        if (builtIn) {
            ImGui::PushFont(theme::boldFont(), 0.0f);
            ImGui::TextUnformatted(shown->name.toRawUTF8());
            ImGui::PopFont();
            ImGui::TextColored(theme::vec(p.textDim), "%s",
                               L("Un thème de CyDAW : il ne se change pas, mais une copie à vous, oui.",
                                 "A CyDAW theme: it does not change, but a copy of your own does."));
        } else {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(theme::vec(p.textDim), "%s", L("Nom", "Name"));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(dp(260.0f));
            ImGui::InputText("##themeName", name_, sizeof(name_));
            if (ImGui::IsItemDeactivatedAfterEdit() && juce::String::fromUTF8(name_).trim().isNotEmpty())
                (void) save(key, juce::String::fromUTF8(name_).trim(), palette);
        }
        if (ui::primaryButton(L("Créer à partir de celui-ci", "Make one from this"))) {
            const auto made = createFrom(key, shown->name + ui::Ls(" (à moi)", " (mine)"));
            if (made.isNotEmpty()) {
                (void) choose(made);
                shown_ = made;
                name_[0] = 0;
            }
        }
        ui::tooltip(L("Un thème à vous, copie de celui-ci : ses couleurs se changent ci-dessous et se voient aussitôt.",
                      "A theme of your own, a copy of this one: its colours change below and show at once."));
        if (!builtIn) {
            ImGui::SameLine();
            if (ImGui::Button(L("Supprimer", "Delete")))
                ImGui::OpenPopup("##deleteTheme");
            if (ImGui::BeginPopup("##deleteTheme")) {
                ImGui::TextUnformatted(L("Mettre ce thème à la corbeille ?", "Put this theme in the recycle bin?"));
                if (ImGui::Button(L("Supprimer", "Delete"))) {
                    (void) remove(key);
                    shown_ = current_;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button(L("Annuler", "Cancel")))
                    ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(L("Ouvrir le dossier", "Open the folder"))) {
            (void) folder().createDirectory();
            folder().startAsProcess();
        }
        ImGui::Separator();

        // Its colours, by group; a theme of one's own changes as they move.
        static const char* groups[][2] = { { "Fonds", "Grounds" },         { "Traits et grille", "Lines and grid" },
                                           { "Texte", "Text" },             { "Accent et sélection", "Accent and selection" },
                                           { "Signaux et transport", "Signals and transport" }, { "Vu-mètres", "Meters" } };
        auto changed = false;
        auto finished = false;
        ImGui::BeginDisabled(builtIn);
        for (auto group = 0; group < 6; ++group) {
            ui::sectionTitle(L(groups[group][0], groups[group][1]));
            auto column = 0;
            for (const auto& field : theme::paletteFields()) {
                if (field.group != group)
                    continue;
                if (column % 2 == 1)
                    ImGui::SameLine(dp(290.0f));
                auto colour = theme::vec(palette.*field.member);
                const auto flags = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar
                                   | (field.member == &theme::Palette::selection ? ImGuiColorEditFlags_None : ImGuiColorEditFlags_NoAlpha);
                if (ImGui::ColorEdit4(ui::labelId(L(field.french, field.english), field.key).c_str(), &colour.x, flags)) {
                    palette.*field.member = ImGui::ColorConvertFloat4ToU32(colour);
                    changed = true;
                }
                finished = finished || ImGui::IsItemDeactivatedAfterEdit();
                ++column;
            }
        }
        ImGui::EndDisabled();
        if (changed && !builtIn) {
            // Seen at once; written when the colour is let go.
            for (auto& entry : themes_)
                if (entry.key == key)
                    entry.palette = palette;
            if (current_ == key)
                theme::setPalette(palette);
        }
        if (finished && !builtIn)
            (void) save(key, juce::String::fromUTF8(name_).trim().isNotEmpty() ? juce::String::fromUTF8(name_).trim() : shown->name, palette);
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace cydaw::v2
