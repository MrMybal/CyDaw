#include "SemanticSearchView.h"

#include "Icons.h"
#include "Locations.h"
#include "Theme.h"
#include "Widgets.h"

#include <imgui.h>

#include <algorithm>

namespace cydaw::v2 {

using ui::dp;
using ui::L;

namespace {

// A link: the text in the accent colour, opened in the browser on a click.
void link(const juce::String& text, const juce::String& url)
{
    const auto& p = theme::palette();
    ImGui::PushStyleColor(ImGuiCol_Text, theme::vec(p.accent));
    ImGui::TextUnformatted(text.toRawUTF8());
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ui::tooltip(url.toRawUTF8());
    }
    if (ImGui::IsItemClicked() && url.isNotEmpty())
        (void) juce::URL(url).launchInDefaultBrowser();
}

void heading(const char* text)
{
    ImGui::Spacing();
    ImGui::PushFont(theme::boldFont(), 0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
    ImGui::Separator();
}

} // namespace

SemanticSearchView::SemanticSearchView(Studio& studio, UiState& state) : studio_(studio), state_(state) {}

void SemanticSearchView::report(const juce::String& text, const bool error)
{
    if (text.isEmpty())
        return;
    state_.status = text;
    state_.statusIsError = error;
    state_.statusTime = ImGui::GetTime();
}

void SemanticSearchView::draw()
{
    if (!state_.showSemantic) {
        shown_ = false;
        return;
    }
    auto& semantic = studio_.semantic();
    if (!shown_) {
        // What the Python has, asked each time the window opens.
        shown_ = true;
        studio_.aiModels().loadCatalogue();
        semantic.checkEngine();
        studio_.companion().pythonCommand().copyToUTF8(python_, sizeof(python_));
    }
    ImGui::SetNextWindowSize(ImVec2(dp(820.0f), dp(720.0f)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(ui::labelId(L("Recherche sémantique des sons (IA, optionnelle)", "Semantic sound search (AI, optional)"),
                                  "semanticSearch")
                          .c_str(),
                      &state_.showSemantic)) {
        ImGui::End();
        return;
    }
    const auto& p = theme::palette();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::vec(p.textDim), "%s",
                       L("Avec un modèle CLAP, le navigateur trouve les sons d'après une description (« kick sombre et "
                         "percutant », « nappe aérienne », « vinyl crackle ») et les sons qui ressemblent à un autre ; il "
                         "range aussi dans une catégorie les sons que leur nom ne dit pas. Rien de ceci n'est nécessaire : "
                         "sans modèle, la recherche se fait par nom, par catégorie et par banque. Rien n'est téléchargé "
                         "sans que vous le confirmiez, et tout reste sur cet ordinateur.",
                         "With a CLAP model, the browser finds sounds from a description (\"dark punchy kick\", \"airy "
                         "pad\", \"vinyl crackle\") and the sounds like another; it also puts the sounds their name does not "
                         "describe in a category. None of this is needed: without a model, searching goes by name, "
                         "category and bank. Nothing is downloaded unless you confirm it, and everything stays on this "
                         "computer."));
    ImGui::PopTextWrapPos();
    drawEngine();
    drawModels();
    drawIndex();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::vec(p.textFaint), "%s",
                       L("Les licences de CyDAW, du moteur Python et des modèles sont distinctes. Une information "
                         "manquante ne donne aucun droit : la licence d'origine fait foi, et les textes traduits ici ne "
                         "sont pas une traduction juridique.",
                         "CyDAW's, the Python engine's and the models' licences are separate. Missing information grants "
                         "no right: the original licence is what counts, and the texts translated here are not a legal "
                         "translation."));
    ImGui::PopTextWrapPos();
    drawConfirm();
    ImGui::End();
}

void SemanticSearchView::drawEngine()
{
    const auto& p = theme::palette();
    auto& semantic = studio_.semantic();
    auto& companion = studio_.companion();
    heading(L("1. Le moteur (Python, PyTorch, transformers)", "1. The engine (Python, PyTorch, transformers)"));
    const auto engine = semantic.engine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::vec(p.textDim), "%s", L("Python", "Python"));
    ImGui::SameLine(dp(110.0f));
    ImGui::SetNextItemWidth(-dp(190.0f));
    ImGui::InputText("##python", python_, sizeof(python_));
    ui::tooltip(L("Le Python qui fait tourner le modèle : celui de l'environnement du compagnon par défaut, ou un autre "
                  "qui a déjà PyTorch et transformers.",
                  "The Python that runs the model: the companion's environment's by default, or another one that already "
                  "has PyTorch and transformers."));
    ImGui::SameLine();
    if (ImGui::Button(L("Utiliser", "Use"))) {
        companion.setPythonCommand(juce::String::fromUTF8(python_));
        semantic.checkEngine();
    }
    ImGui::SameLine();
    if (ImGui::Button(L("Vérifier", "Check")))
        semantic.checkEngine();
    if (engine.checking) {
        ImGui::TextColored(theme::vec(p.accent), "%s", L("Vérification…", "Checking…"));
    } else if (engine.ready) {
        auto text = ui::Ls("Prêt : PyTorch ", "Ready: PyTorch ") + engine.torch + " \xc2\xb7 transformers " + engine.transformers;
        text << " \xc2\xb7 " << (engine.cuda ? ui::Ls("carte graphique ", "graphics card ") + engine.gpu : ui::Ls("processeur", "processor"));
        ImGui::TextColored(theme::vec(p.play), "%s %s", icon::check, text.toRawUTF8());
    } else if (engine.checked) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(theme::vec(p.record), "%s %s", icon::warning,
                           (ui::Ls("Pas prêt : ", "Not ready: ") + engine.error).toRawUTF8());
        ImGui::PopTextWrapPos();
    }
    // The environment made here: the companion's, with what the search needs.
    const auto status = companion.status();
    const auto installing = status.running && status.command == "setup";
    if (installing) {
        ImGui::ProgressBar(static_cast<float>(std::clamp(status.progress, 0.0, 1.0)), ImVec2(-dp(110.0f), 0.0f),
                           status.message.substring(0, 90).toRawUTF8());
        ImGui::SameLine();
        if (ImGui::Button(L("Annuler", "Cancel"), ImVec2(-1.0f, 0.0f)))
            companion.cancel();
    } else if (!engine.ready) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(theme::vec(p.textDim), "%s",
                           L("CyDAW peut le préparer dans un environnement à part (le compagnon Python) : PyTorch, puis "
                             "transformers. Il faut un Python 3.10 ou plus récent sur cet ordinateur.",
                             "CyDAW can make it in an environment of its own (the Python companion): PyTorch, then "
                             "transformers. It needs Python 3.10 or newer on this computer."));
        ImGui::PopTextWrapPos();
        if (ImGui::Button(L("Installer le moteur (processeur)", "Install the engine (processor)"))) {
            engineInstall_ = "cpu";
            openConfirm_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(L("Installer le moteur (carte NVIDIA)", "Install the engine (NVIDIA card)"))) {
            engineInstall_ = "cuda";
            openConfirm_ = true;
        }
    }
    // Their licences, with their pages.
    ImGui::TextColored(theme::vec(p.textFaint), "%s", L("Licences du moteur :", "The engine's licences:"));
    for (const auto& package : studio_.aiModels().enginePackages()) {
        ImGui::SameLine();
        link(package.name + " (" + package.licence + ")", package.url);
    }
}

void SemanticSearchView::drawModels()
{
    heading(L("2. Le modèle", "2. The model"));
    const auto& models = studio_.aiModels().models();
    if (models.empty()) {
        ImGui::TextUnformatted(L("La liste des modèles (Factory/models.json) est introuvable.", "The models' list (Factory/models.json) is missing."));
        return;
    }
    const auto progress = studio_.aiModels().progress();
    if (progress.running) {
        const auto fraction = progress.total > 0 ? static_cast<float>(static_cast<double>(progress.done) / static_cast<double>(progress.total)) : 0.0f;
        auto overlay = ui::sizeText(progress.done) + " / " + ui::sizeText(progress.total);
        if (progress.bytesPerSecond > 0.0)
            overlay << "  \xc2\xb7  " << ui::sizeText(static_cast<juce::int64>(progress.bytesPerSecond)) << "/s";
        const auto step = progress.step == "verify" ? ui::Ls("Vérification ", "Checking ")
                          : progress.step == "copy" ? ui::Ls("Copie ", "Copying ")
                                                    : ui::Ls("Téléchargement ", "Downloading ");
        ImGui::TextUnformatted((step + progress.file).toRawUTF8());
        ImGui::ProgressBar(std::clamp(fraction, 0.0f, 1.0f), ImVec2(-dp(110.0f), 0.0f), overlay.toRawUTF8());
        ImGui::SameLine();
        if (ImGui::Button(L("Annuler##model", "Cancel##model"), ImVec2(-1.0f, 0.0f)))
            studio_.aiModels().cancel();
    }
    for (const auto& model : models) {
        ImGui::PushID(model.key().toRawUTF8());
        drawModel(model);
        ImGui::PopID();
    }
}

void SemanticSearchView::drawModel(const AiModel& model)
{
    const auto& p = theme::palette();
    auto& models = studio_.aiModels();
    auto& semantic = studio_.semantic();
    const auto installed = models.installed(model.id);
    const auto active = semantic.activeModel() == model.id;
    ImGui::BeginGroup();
    ImGui::Spacing();
    ImGui::PushFont(theme::boldFont(), 0.0f);
    ImGui::TextUnformatted(model.title.toRawUTF8());
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextColored(theme::vec(p.textFaint), "%s", model.provider.toRawUTF8());
    if (model.isDefault) {
        ImGui::SameLine();
        ImGui::TextColored(theme::vec(p.accent), "%s", L("conseillé", "recommended"));
    }
    if (!model.validated) {
        ImGui::SameLine();
        ImGui::TextColored(theme::vec(p.textDim), "%s", L("pas encore essayé avec CyDAW", "not tried with CyDAW yet"));
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(model.describe().toRawUTF8());
    ImGui::PopTextWrapPos();
    // Its licence first: what it is, what it says, where to read it.
    ImGui::TextColored(theme::vec(p.textDim), "%s", L("Licence", "Licence"));
    ImGui::SameLine(dp(110.0f));
    ImGui::TextUnformatted(model.licence.toRawUTF8());
    ImGui::SameLine();
    link(ui::Ls("Lire la licence", "Read the licence"), model.licenceUrl);
    ImGui::SameLine();
    link(ui::Ls("Page officielle", "Official page"), model.licencePage);
    if (const auto note = model.note(); note.isNotEmpty()) {
        ImGui::Indent(dp(110.0f));
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(theme::vec(p.textDim), "%s", note.toRawUTF8());
        ImGui::PopTextWrapPos();
        ImGui::Unindent(dp(110.0f));
    }
    ImGui::TextColored(theme::vec(p.textDim), "%s", L("Provenance", "Source"));
    ImGui::SameLine(dp(110.0f));
    ImGui::TextUnformatted((model.host + ui::Ls(" \xc2\xb7 révision ", " \xc2\xb7 revision ") + model.revision.substring(0, 10) + " \xc2\xb7 "
                            + ui::sizeText(model.bytes()))
                               .toRawUTF8());
    if (installed) {
        const auto receipt = models.receiptOf(model.id);
        ImGui::TextColored(theme::vec(p.play), "%s %s", icon::check,
                           (ui::Ls("Installé le ", "Installed on ") + receipt.getProperty("installed", {}).toString().substring(0, 10))
                               .toRawUTF8());
        ui::tooltip(models.folderOf(model).getFullPathName().toRawUTF8());
        ImGui::SameLine();
        if (ImGui::SmallButton(L("Ouvrir le dossier", "Open the folder")))
            models.folderOf(model).revealToUser();
        auto use = active;
        if (ImGui::Checkbox(L("Utilisé pour la recherche", "Used for searching"), &use) && use && !active)
            semantic.setActiveModel(model.id);
        ImGui::SameLine();
        if (ImGui::SmallButton(L("Retirer", "Remove"))) {
            juce::String error;
            if (models.remove(model.id, error)) {
                semantic.modelsChanged();
                report(ui::Ls("Modèle retiré (corbeille) : ", "Model removed (recycle bin): ") + model.title, false);
            } else {
                report(error, true);
            }
        }
    } else {
        // A licence that asks to be accepted: ticked by the person, never kept.
        auto ticked = accepted_ == model.id;
        if (model.requiresAcceptance) {
            if (ImGui::Checkbox(L("J'ai lu cette licence et je l'accepte personnellement", "I have read this licence and accept it personally"),
                                &ticked))
                accepted_ = ticked ? model.id : juce::String();
        }
        const auto busy = models.progress().running;
        ImGui::BeginDisabled(busy || (model.requiresAcceptance && !ticked));
        if (ImGui::Button((ui::Ls("Télécharger (", "Download (") + ui::sizeText(model.bytes()) + ")").toRawUTF8())) {
            confirming_ = model.id;
            engineInstall_.clear();
            openConfirm_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(L("Depuis un dossier…", "From a folder…"))) {
            const auto id = model.id;
            const auto accepted = !model.requiresAcceptance || ticked;
            chooser_ = std::make_unique<juce::FileChooser>(L("Dossier qui contient déjà ce modèle", "Folder that already holds this model"),
                                                           juce::File {});
            chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                  [this, id, accepted](const juce::FileChooser& chooser) {
                                      const auto folder = chooser.getResult();
                                      if (!folder.isDirectory())
                                          return;
                                      juce::String error;
                                      if (!studio_.aiModels().importFrom(id, folder, accepted, error))
                                          report(error, true);
                                  });
        }
        ui::tooltip(L("Ses fichiers pris dans un dossier qui les a déjà (un autre logiciel), chacun vérifié (taille, "
                      "SHA-256) avant d'être copié.",
                      "Its files taken from a folder that already has them (another program), each checked (size, "
                      "SHA-256) before it is copied."));
        ImGui::EndDisabled();
    }
    ImGui::EndGroup();
    ImGui::Separator();
}

void SemanticSearchView::drawIndex()
{
    const auto& p = theme::palette();
    auto& semantic = studio_.semantic();
    heading(L("3. L'index de vos sons", "3. Your sounds' index"));
    const auto status = semantic.status();
    const auto active = semantic.activeModel();
    if (active.isEmpty()) {
        ImGui::TextColored(theme::vec(p.textFaint), "%s", L("Installez d'abord un modèle.", "Install a model first."));
        return;
    }
    if (!studio_.samples().loaded())
        studio_.ensureSamples();
    auto text = juce::String(status.indexed) + " / " + juce::String(status.total) + ui::Ls(" sons indexés", " sounds indexed");
    if (status.device.isNotEmpty())
        text << "  \xc2\xb7  " << (status.device == "cuda" ? ui::Ls("carte graphique", "graphics card") : ui::Ls("processeur", "processor"));
    ImGui::TextUnformatted(text.toRawUTF8());
    if (status.running) {
        const auto total = std::max(1, status.done + status.pending);
        ImGui::ProgressBar(static_cast<float>(status.done) / static_cast<float>(total), ImVec2(-dp(110.0f), 0.0f),
                           (status.loadingModel ? ui::Ls("chargement du modèle…", "loading the model…")
                                                : juce::String(status.done) + " / " + juce::String(total))
                               .toRawUTF8());
        ImGui::SameLine();
        if (ImGui::Button(L("Arrêter", "Stop"), ImVec2(-1.0f, 0.0f)))
            semantic.stopIndexing();
    } else {
        ImGui::BeginDisabled(!semantic.engine().ready);
        if (ImGui::Button(L("Indexer maintenant", "Index now")))
            semantic.startIndexing();
        ImGui::EndDisabled();
        if (!semantic.engine().ready)
            ui::tooltip(L("Le moteur n'est pas prêt (étape 1).", "The engine is not ready (step 1)."));
    }
    if ((status.running && status.message.isNotEmpty()) || status.error.isNotEmpty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(theme::vec(status.error.isNotEmpty() ? p.record : p.textDim), "%s",
                           (status.error.isNotEmpty() ? status.error : status.message).toRawUTF8());
        ImGui::PopTextWrapPos();
    }
    if (status.failed > 0)
        ImGui::TextColored(theme::vec(p.textFaint), "%s",
                           (juce::String(status.failed) + ui::Ls(" fichier(s) illisible(s)", " unreadable file(s)")).toRawUTF8());
    if (status.classified > 0)
        ImGui::TextColored(theme::vec(p.textDim), "%s",
                           (juce::String(status.classified) + ui::Ls(" catégorie(s) donnée(s) par l'IA", " category(ies) given by the AI"))
                               .toRawUTF8());
    auto automatic = semantic.autoIndex();
    if (ImGui::Checkbox(L("Indexer les nouveaux sons automatiquement", "Index new sounds automatically"), &automatic))
        semantic.setAutoIndex(automatic);
    auto categories = semantic.autoCategories();
    if (ImGui::Checkbox(L("Donner une catégorie aux sons sans nom parlant", "Give a category to the sounds with no telling name"),
                        &categories))
        semantic.setAutoCategories(categories);
    ui::tooltip(L("Seulement aux sons que ni leur nom, ni leur dossier, ni leur banque ne classent, et quand l'IA est "
                  "assez sûre. Vos catégories ne changent jamais.",
                  "Only to the sounds neither their name, their folder nor their bank sorts, and when the AI is sure "
                  "enough. Your categories never change."));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::vec(p.textFaint), "%s",
                       (ui::Ls("Index : ", "Index: ") + studio_.samples().cacheFolder().getChildFile("semantic").getFullPathName()).toRawUTF8());
    ImGui::PopTextWrapPos();
}

void SemanticSearchView::drawConfirm()
{
    const auto& p = theme::palette();
    if (openConfirm_) {
        openConfirm_ = false;
        ImGui::OpenPopup("##semanticConfirm");
    }
    if (!ImGui::BeginPopupModal("##semanticConfirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
        return;
    ImGui::PushTextWrapPos(dp(520.0f));
    if (engineInstall_.isNotEmpty()) {
        const auto cuda = engineInstall_ == "cuda";
        ImGui::PushFont(theme::boldFont(), 0.0f);
        ImGui::TextUnformatted(L("Installer le moteur de la recherche sémantique ?", "Install the semantic search's engine?"));
        ImGui::PopFont();
        ImGui::TextUnformatted((ui::Ls("Dans : ", "Into: ") + locations::folder(Location::companion).getFullPathName()).toRawUTF8());
        ImGui::TextUnformatted(cuda ? L("Téléchargé depuis PyPI et download.pytorch.org : numpy, PyTorch pour carte NVIDIA "
                                        "(environ 2,5 à 3 Go), transformers.",
                                        "Downloaded from PyPI and download.pytorch.org: numpy, PyTorch for an NVIDIA card "
                                        "(about 2.5 to 3 GB), transformers.")
                                    : L("Téléchargé depuis PyPI et download.pytorch.org : numpy, PyTorch pour processeur "
                                        "(environ 200 Mo), transformers.",
                                        "Downloaded from PyPI and download.pytorch.org: numpy, PyTorch for the processor "
                                        "(about 200 MB), transformers."));
        ImGui::TextColored(theme::vec(p.textDim), "%s",
                           L("Licences : PyTorch BSD-3-Clause, transformers Apache-2.0, numpy BSD-3-Clause (et celles de "
                             "leurs dépendances).",
                             "Licences: PyTorch BSD-3-Clause, transformers Apache-2.0, numpy BSD-3-Clause (and their "
                             "dependencies')."));
    } else if (const auto* model = studio_.aiModels().find(confirming_)) {
        ImGui::PushFont(theme::boldFont(), 0.0f);
        ImGui::TextUnformatted((ui::Ls("Télécharger ", "Download ") + model->title + " ?").toRawUTF8());
        ImGui::PopFont();
        ImGui::TextUnformatted((ui::Ls("Depuis : ", "From: ") + model->host + " (" + model->homepage + ")").toRawUTF8());
        ImGui::TextUnformatted((ui::Ls("Taille : ", "Size: ") + ui::sizeText(model->bytes()) + ui::Ls(", ", ", ")
                                + juce::String(static_cast<int>(model->files.size())) + ui::Ls(" fichiers vérifiés (taille et SHA-256)",
                                                                                                 " files checked (size and SHA-256)"))
                                   .toRawUTF8());
        ImGui::TextUnformatted((ui::Ls("Dans : ", "Into: ") + studio_.aiModels().folderOf(*model).getFullPathName()).toRawUTF8());
        ImGui::TextUnformatted((ui::Ls("Licence : ", "Licence: ") + model->licence).toRawUTF8());
        if (const auto note = model->note(); note.isNotEmpty())
            ImGui::TextColored(theme::vec(p.textDim), "%s", note.toRawUTF8());
        link(ui::Ls("Lire la licence", "Read the licence"), model->licenceUrl);
    }
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (ImGui::Button(engineInstall_.isNotEmpty() ? L("Installer", "Install") : L("Télécharger", "Download"), ImVec2(dp(140.0f), 0.0f))) {
        if (engineInstall_.isNotEmpty()) {
            const auto environment = locations::folder(Location::companion);
            // The environment's own Python from now on.
            studio_.companion().setPythonCommand({});
            if (!studio_.companion().start({ "setup", "--env", environment.getFullPathName(), "--torch", engineInstall_, "--clap" }))
                report(ui::Ls("Le compagnon est occupé", "The companion is busy"), true);
            else
                report(ui::Ls("Installation du moteur…", "Installing the engine…"), false);
        } else if (const auto* model = studio_.aiModels().find(confirming_)) {
            juce::String error;
            if (studio_.aiModels().install(model->id, !model->requiresAcceptance || accepted_ == model->id, error))
                report(ui::Ls("Téléchargement de ", "Downloading ") + model->title, false);
            else
                report(error, true);
        }
        engineInstall_.clear();
        confirming_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(L("Annuler", "Cancel"), ImVec2(dp(140.0f), 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        engineInstall_.clear();
        confirming_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace cydaw::v2
