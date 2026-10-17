#include "LocationsView.h"

#include "Theme.h"
#include "Widgets.h"

#include <imgui.h>

namespace cydaw::v2 {

using ui::dp;
using ui::L;

namespace {

const char* nameOf(const Location location)
{
    switch (location) {
    case Location::banks: return L("Banques de sons", "Sound banks");
    case Location::downloads: return L("Téléchargements en cours", "Downloads in progress");
    case Location::cache: return L("Cache", "Cache");
    case Location::companion: return L("Compagnon Python", "Python companion");
    case Location::aiModels: return L("Modèles IA (recherche)", "AI models (search)");
    case Location::soundModels: return L("Modèles de son", "Sound models");
    case Location::devices: return L("Mes périphériques", "My devices");
    case Location::pluginExports: return L("Exports de plug-ins", "Plug-in exports");
    case Location::presets: return L("Préréglages", "Presets");
    case Location::mixes: return L("Mixes DJ", "DJ mixes");
    case Location::foundry: return L("Fonderie", "Foundry");
    }
    return "";
}

const char* descriptionOf(const Location location)
{
    switch (location) {
    case Location::banks:
        return L("Les échantillons des banques téléchargées : plusieurs gigaoctets.",
                 "The downloaded banks' samples: several gigabytes.");
    case Location::downloads:
        return L("Les fichiers pendant leur téléchargement, avant d'être rangés dans leur banque.",
                 "Files while they download, before they go into their bank.");
    case Location::cache:
        return L("L'index de la bibliothèque d'échantillons et les compilations VST3 (refaites ailleurs).",
                 "The sample library's index and the VST3 builds (made again elsewhere).");
    case Location::companion:
        return L("Son environnement Python : numpy, PyTorch (jusqu'à quelques gigaoctets).",
                 "Its Python environment: numpy, PyTorch (up to a few gigabytes).");
    case Location::aiModels:
        return L("Les modèles de la recherche sémantique des sons, téléchargés à la demande (600 Mo à 1 Go chacun).",
                 "The semantic sound search's models, downloaded on request (600 MB to 1 GB each).");
    case Location::soundModels:
        return L("Les modèles faits par « Recréer un son ».", "The models made by \"Recreate a sound\".");
    case Location::devices:
        return L("Les périphériques enregistrés.", "The devices saved.");
    case Location::pluginExports:
        return L("Les projets des plug-ins exportés depuis l'atelier.", "The projects of plug-ins exported from the workshop.");
    case Location::presets:
        return L("Les préréglages enregistrés : périphériques, plug-ins, chaînes.",
                 "The presets saved: devices, plug-ins, chains.");
    case Location::mixes:
        return L("Les mixes enregistrés dans le mode Mix DJ.", "The mixes recorded in the DJ mix mode.");
    case Location::foundry:
        return L("Les nouveaux fichiers de la Fonderie, avant d'être enregistrés ailleurs.",
                 "The Foundry's new files, until they are saved elsewhere.");
    }
    return "";
}

// The free space where a folder is (or would be).
juce::int64 freeSpace(juce::File folder)
{
    while (!folder.isDirectory() && folder.getParentDirectory() != folder)
        folder = folder.getParentDirectory();
    return folder.getBytesFreeOnVolume();
}

} // namespace

LocationsView::LocationsView(Studio& studio, UiState& state) : studio_(studio), state_(state) {}

void LocationsView::report(const juce::String& text, const bool error)
{
    if (text.isEmpty())
        return;
    state_.status = text;
    state_.statusIsError = error;
    state_.statusTime = ImGui::GetTime();
}

void LocationsView::apply(const Location location, const juce::File& folder)
{
    juce::String error;
    if (!studio_.setLocation(location, folder, takeContent_, error))
        report(error, true);
    else if (!studio_.locationMove().active)
        report(juce::String::fromUTF8(nameOf(location)) + " : " + locations::folder(location).getFullPathName(), false);
}

void LocationsView::choose(const Location location)
{
    chooser_ = std::make_unique<juce::FileChooser>(juce::String::fromUTF8(nameOf(location)), locations::folder(location));
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this, location](const juce::FileChooser& chooser) {
                              if (const auto folder = chooser.getResult(); folder != juce::File {})
                                  apply(location, folder);
                          });
}

void LocationsView::draw()
{
    if (state_.openLocations) {
        state_.openLocations = false;
        ImGui::OpenPopup("##locations");
    }
    ImGui::SetNextWindowSize(ImVec2(dp(940.0f), 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("##locations", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize))
        return;
    const auto& p = theme::palette();
    ImGui::PushFont(theme::boldFont(), ImGui::GetStyle().FontSizeBase * 1.1f);
    ImGui::TextUnformatted(L("Emplacements", "Locations"));
    ImGui::PopFont();
    ImGui::PushTextWrapPos(dp(900.0f));
    ImGui::TextColored(theme::vec(p.textDim), "%s",
                       L("Où CyDAW range ce qu'il télécharge, compile et fabrique. Un dossier changé reprend ce que CyDAW "
                         "avait mis dans l'ancien ; le reste de ce dossier n'est pas touché.",
                         "Where CyDAW keeps what it downloads, builds and makes. A folder changed takes along what CyDAW "
                         "had put in the old one; nothing else in that folder is touched."));
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    const auto move = studio_.locationMove();
    const auto downloading = studio_.banks().busy();
    ImGui::BeginDisabled(move.active);
    if (ImGui::BeginTable("##locationTable", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX)) {
        ImGui::TableSetupColumn("what", ImGuiTableColumnFlags_WidthFixed, dp(250.0f));
        ImGui::TableSetupColumn("where", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("actions", ImGuiTableColumnFlags_WidthFixed, dp(270.0f));
        for (const auto location : allLocations) {
            ImGui::PushID(locations::key(location));
            ImGui::TableNextRow(0, dp(52.0f));
            ImGui::TableNextColumn();
            ImGui::PushFont(theme::boldFont(), 0.0f);
            ImGui::TextUnformatted(nameOf(location));
            ImGui::PopFont();
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + dp(240.0f));
            ImGui::TextColored(theme::vec(p.textFaint), "%s", descriptionOf(location));
            ImGui::PopTextWrapPos();

            ImGui::TableNextColumn();
            const auto folder = locations::folder(location);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(folder.getFullPathName().toRawUTF8());
            ImGui::PopTextWrapPos();
            auto note = locations::isDefault(location) ? ui::Ls("par défaut · ", "default · ") : juce::String();
            note << ui::sizeText(freeSpace(folder)) << L(" libres sur ce disque", " free on this drive");
            ImGui::TextColored(theme::vec(p.textFaint), "%s", note.toRawUTF8());

            ImGui::TableNextColumn();
            // The banks stay where they are while a download writes to them.
            const auto busy = downloading && (location == Location::banks || location == Location::downloads);
            ImGui::BeginDisabled(busy);
            if (ImGui::Button(L("Changer…", "Change…")))
                choose(location);
            ImGui::SameLine();
            if (ImGui::Button(L("Ouvrir", "Open"))) {
                (void) folder.createDirectory();
                (void) folder.startAsProcess();
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(locations::isDefault(location));
            if (ImGui::Button(L("Par défaut", "Default")))
                apply(location, {});
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            if (busy)
                ui::tooltip(L("Un téléchargement est en cours.", "A download is running."));
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::Checkbox(L("Emporter ce que CyDAW y a mis", "Take along what CyDAW put there"), &takeContent_);
    ui::tooltip(L("Les banques, l'index des échantillons, l'environnement du compagnon, les modèles et les périphériques "
                  "passent dans le nouveau dossier. Sinon il part vide : les banques sont à télécharger de nouveau.",
                  "The banks, the sample index, the companion's environment, the models and the devices go into the new "
                  "folder. Otherwise it starts empty: the banks are to be downloaded again."));
    ImGui::EndDisabled();

    if (move.active) {
        ImGui::Spacing();
        ImGui::TextUnformatted((ui::Ls("Déplacement : ", "Moving: ") + nameOf(move.location) + " \xe2\x86\x92 "
                                + move.to.getFullPathName())
                                   .toRawUTF8());
        const auto fraction = move.progress.total > 0
                                  ? static_cast<float>(static_cast<double>(move.progress.done) / static_cast<double>(move.progress.total))
                                  : 0.0f;
        const auto overlay = ui::sizeText(move.progress.done) + " / " + ui::sizeText(move.progress.total);
        ImGui::ProgressBar(fraction, ImVec2(dp(600.0f), 0.0f), overlay.toRawUTF8());
        ImGui::SameLine();
        if (ImGui::Button(L("Annuler", "Cancel")))
            studio_.cancelLocationMove();
        ImGui::TextColored(theme::vec(p.textFaint), "%s", move.progress.current.toRawUTF8());
    }
    ImGui::Separator();
    if (ui::primaryButton("OK", ImVec2(dp(90.0f), 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();
    if (move.active) {
        ImGui::SameLine();
        ImGui::TextColored(theme::vec(p.textFaint), "%s",
                           L("Le déplacement continue si la fenêtre est fermée.", "The move goes on if the window is closed."));
    }
    ImGui::EndPopup();
}

} // namespace cydaw::v2
