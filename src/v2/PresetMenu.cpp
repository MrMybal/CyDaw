#include "PresetMenu.h"

#include "Icons.h"
#include "Theme.h"
#include "Widgets.h"

#include <imgui.h>

#include <cstring>

namespace cydaw::v2 {

using ui::dp;
using ui::L;

PresetMenu::PresetMenu(DeviceHost& studio, UiState& state) : studio_(studio), state_(state) {}

void PresetMenu::report(const juce::String& text, const bool error)
{
    if (text.isEmpty())
        return;
    state_.status = text;
    state_.statusIsError = error;
    state_.statusTime = ImGui::GetTime();
}

void PresetMenu::open(const char* id, const PresetHolder& holder, const Programs* programs)
{
    programNames_.clear();
    programCurrent_ = -1;
    if (programs != nullptr && programs->list)
        (void) programs->list(programNames_, programCurrent_);
    // One program without a name is no list.
    if (programNames_.size() == 1 && programNames_[0].trim().isEmpty())
        programNames_.clear();
    // The name field starts with the current preset's name.
    const auto current = studio_.presetName(holder.key);
    std::memset(name_, 0, sizeof(name_));
    current.copyToUTF8(name_, sizeof(name_));
    renaming_.clear();
    removing_.clear();
    ImGui::OpenPopup(id);
}

bool PresetMenu::load(const presets::Entry& entry, const PresetHolder& holder, const Apply& apply)
{
    juce::String error;
    const auto state = presets::read(entry, holder.kind, error);
    if (!state.isObject() || !apply(state, error)) {
        report(error, true);
        return false;
    }
    studio_.setPresetName(holder.key, entry.name);
    report(ui::Ls("Préréglage : ", "Preset: ") + entry.name, false);
    return true;
}

void PresetMenu::step(const int direction, const PresetHolder& holder, const Apply& apply)
{
    const auto entries = presets::list(holder.kind, holder.target);
    if (entries.empty()) {
        report(ui::Ls("Aucun préréglage pour l'instant", "No preset yet"), false);
        return;
    }
    const auto current = studio_.presetName(holder.key);
    auto index = -1;
    for (std::size_t at = 0; at < entries.size(); ++at)
        if (entries[at].name == current)
            index = static_cast<int>(at);
    const auto count = static_cast<int>(entries.size());
    index = index < 0 ? (direction > 0 ? 0 : count - 1) : ((index + direction) % count + count) % count;
    (void) load(entries[static_cast<std::size_t>(index)], holder, apply);
}

void PresetMenu::button(const char* id, const PresetHolder& holder, const Capture& capture, const Apply& apply,
                        const ImVec2 size, const Programs* programs)
{
    const auto current = studio_.presetName(holder.key);
    const auto tip = current.isNotEmpty() ? ui::Ls("Préréglages — ", "Presets — ") + current
                                          : ui::Ls("Préréglages : en charger, enregistrer celui-ci", "Presets: load one, save this one");
    ImGui::PushID(id);
    if (ui::iconButton(icon::presets, tip.toRawUTF8(), current.isNotEmpty(), theme::palette().accent, size))
        open("##presets", holder, programs);
    popup("##presets", holder, capture, apply, programs);
    ImGui::PopID();
}

void PresetMenu::bar(const char* id, const PresetHolder& holder, const Capture& capture, const Apply& apply,
                     const float width, const Programs* programs)
{
    ImGui::PushID(id);
    const auto& p = theme::palette();
    const ImVec2 small(dp(22.0f), 0.0f);
    if (ImGui::Button(icon::chevronLeft, small))
        step(-1, holder, apply);
    ui::tooltip(L("Préréglage précédent", "Previous preset"));
    ImGui::SameLine(0.0f, dp(2.0f));
    const auto current = studio_.presetName(holder.key);
    const auto label = (current.isNotEmpty() ? current : juce::String(ui::Ls("(aucun préréglage)", "(no preset)"))) + "##current";
    ImGui::PushStyleColor(ImGuiCol_Text, theme::vec(current.isNotEmpty() ? p.text : p.textFaint));
    if (ImGui::Button(label.toRawUTF8(), ImVec2(std::max(dp(80.0f), width - dp(76.0f)), 0.0f)))
        open("##presets", holder, programs);
    ImGui::PopStyleColor();
    ui::tooltip(L("La liste des préréglages", "The presets' list"));
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ImGui::Button(icon::chevronRight, small))
        step(1, holder, apply);
    ui::tooltip(L("Préréglage suivant", "Next preset"));
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ImGui::Button(icon::save, small))
        open("##presets", holder, programs);
    ui::tooltip(L("Enregistrer ces réglages comme préréglage", "Save these settings as a preset"));
    popup("##presets", holder, capture, apply, programs);
    ImGui::PopID();
}

void PresetMenu::popup(const char* id, const PresetHolder& holder, const Capture& capture, const Apply& apply,
                       const Programs* programs)
{
    ImGui::SetNextWindowSizeConstraints(ImVec2(dp(280.0f), 0.0f), ImVec2(dp(420.0f), dp(520.0f)));
    if (!ImGui::BeginPopup(id))
        return;
    const auto& p = theme::palette();
    const auto current = studio_.presetName(holder.key);
    ImGui::PushFont(theme::boldFont(), 0.0f);
    ImGui::TextUnformatted(L("Préréglages", "Presets"));
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextColored(theme::vec(p.textFaint), "%s", holder.target.toRawUTF8());
    if (ImGui::Button(icon::chevronLeft))
        step(-1, holder, apply);
    ImGui::SameLine();
    if (ImGui::Button(icon::chevronRight))
        step(1, holder, apply);
    ImGui::SameLine();
    ImGui::TextUnformatted(current.isNotEmpty() ? current.toRawUTF8() : L("(aucun)", "(none)"));
    ImGui::Separator();

    // The plug-in's own programs.
    if (!programNames_.isEmpty() && programs != nullptr && programs->select) {
        ImGui::TextColored(theme::vec(p.textFaint), "%s", L("DU PLUG-IN", "THE PLUG-IN'S"));
        ImGui::BeginChild("##programList", ImVec2(0.0f, std::min(dp(200.0f), dp(24.0f) * static_cast<float>(programNames_.size())
                                                                                   + dp(8.0f))));
        for (int index = 0; index < programNames_.size(); ++index) {
            ImGui::PushID(index);
            const auto name = programNames_[index].trim().isNotEmpty() ? programNames_[index]
                                                                        : ui::Ls("Programme ", "Program ") + juce::String(index + 1);
            if (ImGui::Selectable(name.toRawUTF8(), index == programCurrent_, ImGuiSelectableFlags_NoAutoClosePopups)) {
                juce::String error;
                if (programs->select(index, error)) {
                    programCurrent_ = index;
                    studio_.setPresetName(holder.key, name);
                    report(ui::Ls("Programme : ", "Program: ") + name, false);
                } else {
                    report(error, true);
                }
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::Separator();
    }

    const auto entries = presets::list(holder.kind, holder.target);
    if (entries.empty())
        ImGui::TextColored(theme::vec(p.textFaint), "%s",
                           L("Aucun préréglage : enregistrez ces réglages ci-dessous.",
                             "No preset: save these settings below."));
    ImGui::BeginChild("##presetList", ImVec2(0.0f, std::min(dp(300.0f), dp(24.0f) * static_cast<float>(entries.size()) + dp(8.0f))));
    auto shownFactory = false;
    auto shownOwn = false;
    for (const auto& entry : entries) {
        if (entry.factory && !shownFactory) {
            shownFactory = true;
            ImGui::TextColored(theme::vec(p.textFaint), "%s", L("DE CYDAW", "CYDAW'S"));
        }
        if (!entry.factory && !shownOwn) {
            shownOwn = true;
            ImGui::TextColored(theme::vec(p.textFaint), "%s", L("LES VÔTRES", "YOURS"));
        }
        ImGui::PushID(entry.file.getFullPathName().toRawUTF8());
        if (renaming_ == entry.file.getFullPathName()) {
            ImGui::SetNextItemWidth(-dp(60.0f));
            const auto done = ImGui::InputText("##rename", newName_, sizeof(newName_), ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            if (done || ImGui::Button("OK")) {
                juce::String error;
                const auto name = juce::String::fromUTF8(newName_).trim();
                if (presets::rename(entry, name, error)) {
                    if (current == entry.name)
                        studio_.setPresetName(holder.key, name);
                    renaming_.clear();
                } else {
                    report(error, true);
                }
            }
        } else if (removing_ == entry.file.getFullPathName()) {
            ImGui::TextColored(theme::vec(p.record), "%s", (ui::Ls("Supprimer « ", "Remove \"") + entry.name + ui::Ls(" » ?", "\"?")).toRawUTF8());
            ImGui::SameLine();
            if (ImGui::SmallButton(L("Supprimer", "Remove"))) {
                juce::String error;
                if (!presets::remove(entry, error))
                    report(error, true);
                else if (current == entry.name)
                    studio_.setPresetName(holder.key, {});
                removing_.clear();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(L("Non", "No")))
                removing_.clear();
        } else {
            if (ImGui::Selectable(entry.name.toRawUTF8(), entry.name == current, ImGuiSelectableFlags_NoAutoClosePopups))
                (void) load(entry, holder, apply);
            if (!entry.factory && ImGui::BeginPopupContextItem("##presetItem")) {
                if (ImGui::MenuItem(L("Renommer", "Rename"))) {
                    renaming_ = entry.file.getFullPathName();
                    std::memset(newName_, 0, sizeof(newName_));
                    entry.name.copyToUTF8(newName_, sizeof(newName_));
                }
                if (ImGui::MenuItem(L("Remplacer par les réglages actuels", "Replace with the current settings"))) {
                    juce::String error;
                    if (presets::save(holder.kind, holder.target, entry.name, capture(), error))
                        studio_.setPresetName(holder.key, entry.name);
                    else
                        report(error, true);
                }
                if (ImGui::MenuItem(L("Supprimer…", "Remove…")))
                    removing_ = entry.file.getFullPathName();
                if (ImGui::MenuItem(L("Afficher dans le dossier", "Show in its folder")))
                    entry.file.revealToUser();
                ImGui::EndPopup();
            }
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::Separator();

    // Saving the current settings.
    ImGui::TextUnformatted(L("Enregistrer ces réglages sous", "Save these settings as"));
    ImGui::SetNextItemWidth(-dp(100.0f));
    const auto entered = ImGui::InputTextWithHint("##presetName", L("Nom du préréglage", "Preset name"), name_, sizeof(name_),
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    const auto name = juce::String::fromUTF8(name_).trim();
    const auto replaces = presets::find(entries, name) != nullptr && !presets::find(entries, name)->factory;
    ImGui::BeginDisabled(name.isEmpty());
    if (ui::primaryButton(replaces ? L("Remplacer", "Replace") : L("Enregistrer", "Save"), ImVec2(dp(92.0f), 0.0f)) || (entered && name.isNotEmpty())) {
        juce::String error;
        if (presets::save(holder.kind, holder.target, name, capture(), error)) {
            studio_.setPresetName(holder.key, name);
            report(ui::Ls("Préréglage enregistré : ", "Preset saved: ") + name, false);
        } else {
            report(error, true);
        }
    }
    ImGui::EndDisabled();
    if (ImGui::SmallButton(L("Ouvrir le dossier des préréglages", "Open the presets' folder"))) {
        const auto folder = presets::userFolder(holder.kind, holder.target);
        (void) folder.createDirectory();
        (void) folder.startAsProcess();
    }
    ImGui::EndPopup();
}

} // namespace cydaw::v2
