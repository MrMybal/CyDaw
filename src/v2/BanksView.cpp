#include "BanksView.h"

#include "SamplerMapFile.h"
#include "Theme.h"
#include "Widgets.h"

#include <imgui.h>

#include <algorithm>

namespace cydaw::v2 {

using ui::dp;
using ui::L;

BanksView::BanksView(Studio& studio, UiState& state) : studio_(studio), state_(state) {}

void BanksView::report(const juce::String& text, const bool error)
{
    if (text.isEmpty())
        return;
    state_.status = text;
    state_.statusIsError = error;
    state_.statusTime = ImGui::GetTime();
}

void BanksView::drawProgress()
{
    const auto& p = theme::palette();
    const auto progress = studio_.banks().progress();
    if (!progress.running)
        return;
    auto title = (progress.generating ? ui::Ls("Génération : ", "Generating: ") : ui::Ls("Téléchargement : ", "Downloading: "))
                 + progress.label;
    if (progress.queued > 0)
        title << ui::Ls(" (puis ", " (then ") << progress.queued << ui::Ls(" autre(s))", " more)");
    ImGui::TextUnformatted(title.toRawUTF8());
    const auto fraction = progress.bytesTotal > 0
                              ? static_cast<float>(static_cast<double>(progress.bytesDone) / static_cast<double>(progress.bytesTotal))
                              : 0.0f;
    auto overlay = ui::sizeText(progress.bytesDone) + " / " + ui::sizeText(progress.bytesTotal);
    if (progress.bytesPerSecond > 0.0)
        overlay << "  \xc2\xb7  " << ui::sizeText(static_cast<juce::int64>(progress.bytesPerSecond)) << "/s";
    ImGui::ProgressBar(std::clamp(fraction, 0.0f, 1.0f), ImVec2(-dp(110.0f), 0.0f), overlay.toRawUTF8());
    ImGui::SameLine();
    if (ImGui::Button(L("Annuler", "Cancel"), ImVec2(-1.0f, 0.0f)))
        studio_.banks().cancel();
    auto files = juce::String(progress.done) + " / " + juce::String(progress.total) + ui::Ls(" fichiers", " files");
    if (progress.failed > 0)
        files << " \xc2\xb7 " << progress.failed << ui::Ls(" en échec : ", " failed: ") << progress.error;
    ImGui::TextColored(theme::vec(progress.failed > 0 ? p.record : p.textFaint), "%s", files.toRawUTF8());
    ImGui::Separator();
}

void BanksView::drawDetails(const SoundBanks::Bank& bank)
{
    const auto& p = theme::palette();
    ImGui::PushFont(theme::boldFont(), 0.0f);
    ImGui::TextUnformatted(bank.title.toRawUTF8());
    ImGui::PopFont();
    ImGui::PushTextWrapPos(0.0f);
    if (bank.description.isNotEmpty())
        ImGui::TextUnformatted(bank.description.toRawUTF8());
    const auto row = [&p](const char* label, const juce::String& text, const juce::String& link = {}) {
        if (text.isEmpty())
            return;
        ImGui::TextColored(theme::vec(p.textDim), "%s", label);
        ImGui::SameLine(dp(120.0f));
        if (link.isEmpty()) {
            ImGui::TextUnformatted(text.toRawUTF8());
            return;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, theme::vec(p.accent));
        ImGui::TextUnformatted(text.toRawUTF8());
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ui::tooltip(link.toRawUTF8());
        }
        if (ImGui::IsItemClicked())
            (void) juce::URL(link).launchInDefaultBrowser();
    };
    row(L("Auteur", "Author"), bank.author);
    row(L("Licence", "Licence"), bank.licence, bank.licenceUrl);
    const auto source = bank.homepage.isNotEmpty() ? bank.homepage
                        : bank.repository.isNotEmpty() ? "https://github.com/" + bank.repository
                                                       : juce::String();
    row(L("Provenance", "Source"), source, source);
    if (bank.commit.isNotEmpty())
        row(L("Version", "Version"), bank.commit.substring(0, 12));
    row(L("Ici", "Here"), studio_.banks().folderOf(bank.id).getFullPathName());
    ImGui::PopTextWrapPos();
}

void BanksView::draw()
{
    if (!state_.showBanks) {
        shown_ = false;
        return;
    }
    auto& banks = studio_.banks();
    // Counted again each time the window opens: files may have come by hand.
    if (!shown_) {
        shown_ = true;
        banks.loadRegistry();
        banks.forgetDisk();
    }
    ImGui::SetNextWindowSize(ImVec2(dp(1000.0f), dp(640.0f)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(ui::labelId(L("Banques de sons", "Sound banks"), "soundBanks").c_str(), &state_.showBanks)) {
        ImGui::End();
        return;
    }
    const auto& p = theme::palette();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::vec(p.textDim), "%s",
                       L("Les instruments d'usine jouent les notes de ces banques, toutes libres de droits. CyDAW fournit "
                         "leurs cartes de sampler et leurs modèles de son ; les échantillons se téléchargent ici, depuis "
                         "le dépôt de chaque banque, ou d'un double-clic sur l'instrument dans le navigateur.",
                         "The factory instruments play the notes of these banks, all free to use. CyDAW ships their "
                         "sampler maps and sound models; the samples are downloaded here, from each bank's repository, "
                         "or with a double click on the instrument in the browser."));
    ImGui::PopTextWrapPos();
    const auto folder = app::banksFolder();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::vec(p.textDim), "%s", L("Dossier des banques", "Banks folder"));
    ImGui::SameLine();
    ImGui::TextUnformatted(folder.getFullPathName().toRawUTF8());
    ImGui::SameLine();
    if (ImGui::SmallButton(L("Ouvrir", "Open"))) {
        (void) folder.createDirectory();
        (void) folder.startAsProcess();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(L("Emplacements…", "Locations…")))
        state_.openLocations = true;
    ImGui::Separator();
    drawProgress();

    const auto busy = banks.busy();
    const auto detailsHeight = selected_.isNotEmpty() ? dp(150.0f) : 0.0f;
    if (banks.banks().empty()) {
        ui::emptyState(L("Aucune banque", "No bank"),
                       L("La liste des banques (Factory/banks.json) est introuvable.", "The banks' list (Factory/banks.json) is missing."));
    } else if (ImGui::BeginTable("##banks", 5,
                                 ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY
                                     | ImGuiTableFlags_Resizable,
                                 ImVec2(0.0f, -detailsHeight))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(L("Banque", "Bank"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(L("Licence", "Licence"), ImGuiTableColumnFlags_WidthFixed, dp(90.0f));
        ImGui::TableSetupColumn(L("Taille", "Size"), ImGuiTableColumnFlags_WidthFixed, dp(80.0f));
        ImGui::TableSetupColumn(L("Ici", "Here"), ImGuiTableColumnFlags_WidthFixed, dp(150.0f));
        ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, dp(230.0f));
        ImGui::TableHeadersRow();
        for (const auto& bank : banks.banks()) {
            ImGui::PushID(bank.id.toRawUTF8());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(bank.title.toRawUTF8(), selected_ == bank.id, ImGuiSelectableFlags_SpanAllColumns
                                                                                     | ImGuiSelectableFlags_AllowOverlap))
                selected_ = selected_ == bank.id ? juce::String() : bank.id;
            if (bank.author.isNotEmpty()) {
                ImGui::SameLine();
                ImGui::TextColored(theme::vec(p.textFaint), "%s", bank.author.toRawUTF8());
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(bank.licence.toRawUTF8());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(ui::sizeText(bank.bytes).toRawUTF8());
            ImGui::TableNextColumn();
            const auto disk = banks.onDisk(bank.id);
            const auto complete = disk.has_value() && disk->count >= bank.count && bank.count > 0;
            if (!disk.has_value())
                ImGui::TextColored(theme::vec(p.textFaint), "%s", "\xe2\x80\xa6");
            else if (complete)
                ImGui::TextColored(theme::vec(p.play), "%s", L("complète", "complete"));
            else if (disk->count == 0)
                ImGui::TextColored(theme::vec(p.textFaint), "%s",
                                   bank.generator.isNotEmpty() ? L("à générer", "not generated") : L("à télécharger", "not downloaded"));
            else
                ImGui::TextUnformatted((juce::String(disk->count) + " / " + juce::String(bank.count) + " \xc2\xb7 "
                                        + ui::sizeText(disk->bytes))
                                           .toRawUTF8());
            ImGui::TableNextColumn();
            if (disk.has_value() && !complete) {
                const auto generated = bank.generator.isNotEmpty();
                const auto label = (generated ? ui::Ls("Générer ", "Generate ") : ui::Ls("Télécharger ", "Download "))
                                   + ui::sizeText(std::max<juce::int64>(0, bank.bytes - disk->bytes));
                if (ImGui::SmallButton(label.toRawUTF8())) {
                    const auto missing = banks.missingOfBank(bank.id);
                    if (missing.empty())
                        report(ui::Ls("Déjà là", "Already here"), false);
                    else {
                        banks.download(bank.title, missing);
                        report((generated ? ui::Ls("Génération de ", "Generating ") : ui::Ls("Téléchargement de ", "Downloading "))
                                   + bank.title,
                               false);
                    }
                }
                if (generated)
                    ui::tooltip(L("Ces sons sont fabriqués par CyDAW sur cet ordinateur, à partir des recettes qu'il fournit : "
                                  "rien n'est téléchargé.",
                                  "These sounds are made by CyDAW on this computer, from the recipes it ships: nothing is "
                                  "downloaded."));
                ImGui::SameLine();
            }
            ImGui::BeginDisabled(busy || !disk.has_value() || disk->count == 0);
            if (ImGui::SmallButton(L("Supprimer", "Remove")))
                removing_ = bank.id;
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (selected_.isNotEmpty())
        if (const auto* bank = banks.find(selected_)) {
            ImGui::Separator();
            ImGui::BeginChild("##bankDetails");
            drawDetails(*bank);
            ImGui::EndChild();
        }

    // Removing: asked first; the files go to the recycle bin.
    if (removing_.isNotEmpty()) {
        ImGui::OpenPopup("##removeBank");
        selected_ = removing_;
    }
    if (ImGui::BeginPopupModal("##removeBank", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        const auto* bank = banks.find(selected_);
        const auto disk = bank != nullptr ? banks.onDisk(bank->id) : std::nullopt;
        ImGui::TextUnformatted((ui::Ls("Supprimer les échantillons de ", "Remove the samples of ")
                                + (bank != nullptr ? bank->title : selected_) + " ("
                                + ui::sizeText(disk.has_value() ? disk->bytes : 0) + ") ?")
                                   .toRawUTF8());
        ImGui::TextColored(theme::vec(p.textDim), "%s",
                           L("Ils vont à la corbeille. Les instruments qui les jouent se tairont jusqu'à un nouveau "
                             "téléchargement ; leurs modèles de son, eux, restent.",
                             "They go to the recycle bin. The instruments that play them fall silent until they are "
                             "downloaded again; their sound models stay."));
        if (ui::primaryButton(L("Supprimer", "Remove"))) {
            juce::String error;
            if (banks.remove(selected_, error))
                report(ui::Ls("Échantillons supprimés", "Samples removed"), false);
            else
                report(error, true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(L("Annuler", "Cancel")) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    removing_.clear();
    ImGui::End();
}

} // namespace cydaw::v2
