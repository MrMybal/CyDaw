#include "BrowserView.h"

#include "Icons.h"
#include "ImGuiHost.h"
#include "Theme.h"
#include "Widgets.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cydaw::v2 {

using ui::dp;
using ui::L;

namespace {

// Small letters without accents (the device list's search).
juce::String fold(const juce::String& text)
{
    return juce::String::fromUTF8(SampleCatalog::fold(text).c_str());
}

} // namespace

BrowserView::BrowserView(Studio& studio, UiState& state, ImGuiHost& host, SampleCatalog& catalog)
    : studio_(studio), state_(state), host_(host), catalog_(catalog)
{
    studio_.samples().addListener(this);
    // The catalog's new data, and the search's answers (from its own
    // threads): the frame woken on the message thread, while this view is there.
    catalog_.addOnChange(this, [this] {
        dirty_ = true;
        host_.wake();
    });
    studio_.semantic().setOnChange([alive = std::weak_ptr<int>(alive_), host = &host_] {
        juce::MessageManager::callAsync([alive, host] {
            if (!alive.expired())
                host->wake();
        });
    });
}

BrowserView::~BrowserView()
{
    catalog_.removeOnChange(this);
    studio_.semantic().setOnChange(nullptr);
    studio_.samples().removeListener(this);
}

void BrowserView::sampleLibraryChanged(bool)
{
    // The catalog follows (and says so); a favourite shows at once.
    host_.wake();
}

void BrowserView::sampleLibraryProgress()
{
    host_.wake();
}

void BrowserView::openLarge(const int tab)
{
    state_.showLibrary = true;
    state_.libraryTab = tab;
    state_.libraryPick = false;
    state_.libraryTarget.reset();
    state_.libraryFocus = true;
}

namespace {

juce::String categoryLabel(const juce::String& key)
{
    if (key == "-" || key.isEmpty())
        return ui::Ls("Sans catégorie", "Uncategorised");
    if (const auto* info = app::SampleLibrary::categoryInfo(key))
        return juce::String::fromUTF8(ui::french() ? info->french : info->english);
    return key;
}

int familyIndexOf(const juce::String& family)
{
    const auto& families = app::SampleLibrary::categoryFamilies();
    for (std::size_t index = 0; index < families.size(); ++index)
        if (family == families[index].key)
            return static_cast<int>(index);
    return -1;
}

} // namespace

void BrowserView::refilter()
{
    data_ = catalog_.data();
    if (data_ == nullptr)
        return;
    using Catalog = SampleCatalog;
    Catalog::Query query;
    query.text = semanticMode_ ? juce::String() : juce::String::fromUTF8(search_);
    query.favouritesOnly = favouritesOnly_;
    // The tree's branch as the catalog's columns.
    const auto choose = [&](const Catalog::Facet facet, const std::function<bool(const Catalog::Value&)>& keep) {
        const auto& values = data_->values[static_cast<std::size_t>(facet)];
        auto& selected = query.selected[static_cast<std::size_t>(facet)];
        selected.assign(values.size(), 0);
        for (std::size_t id = 0; id < values.size(); ++id)
            if (keep(values[id]))
                selected[id] = 1;
        // Nothing of it any more: nothing shown, not everything.
        if (std::find(selected.begin(), selected.end(), 1) == selected.end())
            selected.push_back(1);
    };
    if (filterCategory_.isNotEmpty())
        choose(Catalog::category, [this](const Catalog::Value& value) { return value.key == filterCategory_; });
    else if (filterFamily_.isNotEmpty()) {
        const auto family = familyIndexOf(filterFamily_);
        choose(Catalog::category, [family](const Catalog::Value& value) { return value.family == family; });
    }
    if (filterGroup_.isNotEmpty())
        choose(Catalog::group, [this](const Catalog::Value& value) { return value.key == filterGroup_; });
    if (filterSource_.isNotEmpty())
        choose(Catalog::source, [this](const Catalog::Value& value) {
            return filterSource_ == "banks" ? value.key.startsWith("bank:")
                   : filterSource_ == "folders" ? value.key.startsWith("folder:")
                                                : value.key == filterSource_;
        });
    const auto ranked = semanticMode_ && (!semanticHits_.empty() || semanticAsked_.isNotEmpty() || semanticSimilarTo_.isNotEmpty());
    query.ranked = ranked ? &semanticHits_ : nullptr;
    query.sortColumn = ranked ? Catalog::colScore : Catalog::colName;
    query.ascending = !ranked;
    result_ = Catalog::run(*data_, query);
    dirty_ = false;
}

void BrowserView::takeSemanticAnswer()
{
    auto answer = studio_.semantic().takeAnswer();
    if (!answer)
        return;
    SemanticSearch::rerank(*answer, studio_.samples(), 400);
    semanticHits_.clear();
    semanticError_ = answer->failed ? answer->error : juce::String();
    semanticEnglish_ = answer->english;
    for (const auto& hit : answer->hits)
        semanticHits_.emplace_back(hit.path, hit.score);
    dirty_ = true;
}

void BrowserView::askSimilar(const app::SampleRecord& record)
{
    semanticMode_ = true;
    semanticSimilarTo_ = record.path;
    semanticAsked_.clear();
    semanticPending_ = false;
    semanticHits_.clear();
    semanticError_.clear();
    (void) studio_.semantic().searchSimilar(record.path);
    dirty_ = true;
}

void BrowserView::draw()
{
    if (ImGui::BeginTabBar("##browserTabs")) {
        const auto samplesFlags = showSamplesTab_ ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        showSamplesTab_ = false;
        if (ImGui::BeginTabItem(ui::labelId(L("Samples", "Samples"), "samplesTab").c_str(), nullptr, samplesFlags)) {
            currentTab_ = 0;
            drawSamples();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(ui::labelId(L("Instruments", "Instruments"), "instrumentsTab").c_str())) {
            currentTab_ = 1;
            drawInstruments();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(ui::labelId(L("Périphériques", "Devices"), "devicesTab").c_str())) {
            currentTab_ = 2;
            drawDevices();
            ImGui::EndTabItem();
        }
        // The big window, on the same tab.
        if (ImGui::TabItemButton(icon::fullScreen, ImGuiTabItemFlags_Trailing))
            openLarge(currentTab_);
        ui::tooltip(L("Ouvrir en grand (F9) : filtre multicolonne, grand tableau, écoute", "Open large (F9): multi-column filter, big table, preview"));
        ImGui::EndTabBar();
    }
}

void BrowserView::drawSampleTree(const float height)
{
    const auto& p = theme::palette();
    if (!ImGui::BeginChild("##sampleTree", ImVec2(0.0f, height), ImGuiChildFlags_Borders)) {
        ImGui::EndChild();
        return;
    }
    using Node = SampleCatalog::TreeNode;
    const auto line = [&p](const Node& node, const bool selected, const bool leaf, const bool open = false) {
        auto flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
        if (selected)
            flags |= ImGuiTreeNodeFlags_Selected;
        if (leaf)
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (open)
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        const auto expanded = ImGui::TreeNodeEx(node.key.toRawUTF8(), flags, "%s", node.label.toRawUTF8());
        const auto clicked = ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen();
        ImGui::SameLine();
        ImGui::TextColored(theme::vec(p.textFaint), "%d", node.count);
        return std::pair { expanded && !leaf, clicked };
    };
    const auto choose = [this](const juce::String& family, const juce::String& category, const juce::String& group) {
        const auto same = filterFamily_ == family && filterCategory_ == category && filterGroup_ == group;
        filterFamily_ = same ? juce::String() : family;
        filterCategory_ = same ? juce::String() : category;
        filterGroup_ = same ? juce::String() : group;
        dirty_ = true;
    };
    // Everything.
    {
        const auto all = filterFamily_.isEmpty() && filterCategory_.isEmpty() && filterGroup_.isEmpty() && filterSource_.isEmpty();
        if (ImGui::Selectable((ui::Ls("Tous les sons", "All sounds") + "##all").toRawUTF8(), all, ImGuiSelectableFlags_None,
                              ImVec2(ImGui::GetContentRegionAvail().x - dp(60.0f), 0.0f))) {
            filterFamily_ = filterCategory_ = filterGroup_ = filterSource_ = {};
            dirty_ = true;
        }
        ImGui::SameLine();
        ImGui::TextColored(theme::vec(p.textFaint), "%d", data_ != nullptr ? static_cast<int>(data_->items.size()) : 0);
    }
    if (data_ != nullptr) {
        for (const auto& family : data_->categoryTree) {
            ImGui::PushID(family.key.toRawUTF8());
            const auto [open, clicked] = line(family, filterFamily_ == family.key && filterCategory_.isEmpty(), false, true);
            if (clicked)
                choose(family.key, {}, {});
            if (open) {
                for (const auto& category : family.children) {
                    ImGui::PushID(category.key.toRawUTF8());
                    // One group (one bank, one instrument) is no branch.
                    const auto leaf = category.children.size() <= 1;
                    const auto [categoryOpen, categoryClicked]
                        = line(category, filterCategory_ == category.key && filterGroup_.isEmpty(), leaf);
                    if (categoryClicked)
                        choose(family.key, category.key, {});
                    if (categoryOpen) {
                        for (const auto& group : category.children) {
                            const auto [ignored, groupClicked] = line(group, filterCategory_ == category.key && filterGroup_ == group.key, true);
                            juce::ignoreUnused(ignored);
                            if (groupClicked)
                                choose(family.key, category.key, group.key);
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (!data_->sourceTree.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(theme::vec(p.textDim), "%s", L("Sources", "Sources"));
            for (const auto& kind : data_->sourceTree) {
                ImGui::PushID(kind.key.toRawUTF8());
                const auto [open, clicked] = line(kind, filterSource_ == kind.key, false);
                if (clicked) {
                    filterSource_ = filterSource_ == kind.key ? juce::String() : kind.key;
                    dirty_ = true;
                }
                if (open) {
                    for (const auto& source : kind.children) {
                        const auto [ignored, sourceClicked] = line(source, filterSource_ == source.key, true);
                        juce::ignoreUnused(ignored);
                        if (sourceClicked) {
                            filterSource_ = filterSource_ == source.key ? juce::String() : source.key;
                            dirty_ = true;
                        }
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        }
    }
    ImGui::EndChild();
}

void BrowserView::drawSemanticLine()
{
    const auto& p = theme::palette();
    auto& semantic = studio_.semantic();
    const auto status = semantic.status();
    juce::String text;
    auto colour = p.textDim;
    if (semanticSimilarTo_.isNotEmpty())
        text = ui::Ls("Sons proches de « ", "Sounds like \"") + juce::File(semanticSimilarTo_).getFileNameWithoutExtension()
               + ui::Ls(" »", "\"");
    else if (semanticEnglish_.isNotEmpty() && fold(semanticEnglish_) != fold(semanticAsked_))
        text = ui::Ls("Cherché : « ", "Searched: \"") + semanticEnglish_ + ui::Ls(" »", "\"");
    if (semantic.searching() || status.loadingModel) {
        text = status.loadingModel ? ui::Ls("Chargement du modèle…", "Loading the model…") : ui::Ls("Recherche…", "Searching…");
        colour = p.accent;
        host_.wake(2);
    } else if (semanticError_.isNotEmpty()) {
        text = semanticError_;
        colour = p.record;
    }
    if (status.running)
        text << (text.isEmpty() ? "" : "  \xc2\xb7  ") << ui::Ls("indexation ", "indexing ") << status.done << " / "
             << status.done + status.pending;
    else if (status.total > 0 && status.indexed < status.total)
        text << (text.isEmpty() ? "" : "  \xc2\xb7  ") << status.indexed << " / " << status.total << ui::Ls(" indexés", " indexed");
    if (text.isEmpty())
        return;
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::vec(colour), "%s", text.toRawUTF8());
    ImGui::PopTextWrapPos();
    if (semanticSimilarTo_.isNotEmpty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton(L("Effacer", "Clear"))) {
            semanticSimilarTo_.clear();
            semanticHits_.clear();
            dirty_ = true;
        }
    }
}

void BrowserView::drawSampleMenu(const app::SampleRecord& record)
{
    auto& library = studio_.samples();
    const auto semanticReady = studio_.semantic().available();
    if (ImGui::MenuItem(L("Sons similaires", "Similar sounds"), nullptr, false, semanticReady))
        askSimilar(record);
    if (!semanticReady)
        ui::tooltip(L("Avec un modèle de recherche sémantique installé (Options › Recherche sémantique).",
                      "With a semantic search model installed (Options › Semantic search)."));
    if (ImGui::MenuItem(L("Favori", "Favourite"), nullptr, record.favourite))
        library.setFavourite(record.path, !record.favourite);
    if (ImGui::BeginMenu(L("Catégorie", "Category"))) {
        for (const auto& family : app::SampleLibrary::categoryFamilies()) {
            ImGui::TextDisabled("%s", ui::french() ? family.french : family.english);
            for (const auto& category : app::SampleLibrary::categoryTaxonomy())
                if (juce::String(category.family) == family.key
                    && ImGui::MenuItem((juce::String("  ") + juce::String::fromUTF8(ui::french() ? category.french : category.english)).toRawUTF8(),
                                       nullptr, record.category == category.key))
                    library.setCategory(record.path, category.key);
        }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem(L("Montrer dans l'explorateur", "Show in the file explorer")))
        record.file().revealToUser();
    if (ImGui::MenuItem(L("Copier le chemin", "Copy the path")))
        juce::SystemClipboard::copyTextToClipboard(record.path);
}

void BrowserView::drawSamples()
{
    const auto& p = theme::palette();
    auto& library = studio_.samples();
    auto& engine = studio_.engine();
    auto& semantic = studio_.semantic();
    takeSemanticAnswer();

    // Search, its kind (names or descriptions), favourites, folders.
    const auto buttons = dp(122.0f);
    ImGui::SetNextItemWidth(-buttons);
    const auto hint = semanticMode_ ? L("Décrivez le son (kick sombre, nappe aérienne…)", "Describe the sound (dark kick, airy pad…)")
                                    : L("Rechercher (nom, catégorie, tag, tonalité)…", "Search (name, category, tag, key)…");
    const auto edited = ImGui::InputTextWithHint("##sampleSearch", hint, search_, sizeof(search_));
    const auto entered = ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter);
    if (edited) {
        editedAt_ = ImGui::GetTime();
        if (semanticMode_)
            semanticPending_ = true;
        else
            dirty_ = true;
    }
    ImGui::SameLine(0.0f, dp(2.0f));
    {
        const auto ready = semantic.available();
        const auto tip = ready ? L("Recherche par description (IA) : décrivez le son, en français ou en anglais. "
                                   "Cliquez encore pour revenir à la recherche par nom.",
                                   "Search by description (AI): describe the sound, in English or French. Click again to "
                                   "go back to searching by name.")
                               : L("Recherche par description (IA, optionnelle) : installez un modèle depuis cette fenêtre. "
                                   "Sans lui, la recherche se fait par nom et par catégorie.",
                                   "Search by description (AI, optional): install a model from this window. Without it, "
                                   "searching goes by name and category.");
        if (ui::iconButton(icon::lightbulb, tip, semanticMode_, p.accent)) {
            if (!ready && !semanticMode_) {
                state_.showSemantic = true;
            } else {
                semanticMode_ = !semanticMode_;
                semanticHits_.clear();
                semanticSimilarTo_.clear();
                semanticError_.clear();
                semanticEnglish_.clear();
                semanticAsked_.clear();
                semanticPending_ = semanticMode_ && search_[0] != 0;
                editedAt_ = 0.0;
                dirty_ = true;
            }
        }
    }
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(favouritesOnly_ ? icon::starFill : icon::star, L("Favoris seulement", "Favourites only"), favouritesOnly_)) {
        favouritesOnly_ = !favouritesOnly_;
        dirty_ = true;
    }
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(icon::folder, L("Dossiers, banques, recherche", "Folders, banks, search")))
        ImGui::OpenPopup("##folders");
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(icon::fullScreen, L("Ouvrir en grand (F9) : filtre multicolonne, grand tableau, écoute",
                                           "Open large (F9): multi-column filter, big table, preview")))
        openLarge(0);
    if (ImGui::BeginPopup("##folders")) {
        ImGui::TextDisabled("%s", L("Dossiers analysés", "Scanned folders"));
        std::optional<juce::File> remove;
        for (const auto& folder : library.folders()) {
            ImGui::PushID(folder.getFullPathName().toRawUTF8());
            if (ui::iconButton(icon::close, L("Retirer ce dossier", "Remove this folder"), false, 0, ImVec2(dp(20.0f), dp(20.0f))))
                remove = folder;
            ImGui::SameLine();
            ImGui::TextUnformatted(folder.getFullPathName().toRawUTF8());
            ImGui::PopID();
        }
        if (remove)
            library.removeFolder(*remove);
        if (library.folders().empty())
            ImGui::TextDisabled("%s", L("Aucun dossier à vous pour l'instant.", "No folder of yours yet."));
        if (ImGui::Selectable(L("Ajouter un dossier…", "Add a folder…"))) {
            chooser_ = std::make_unique<juce::FileChooser>(L("Dossier de samples", "Sample folder"), juce::File {});
            chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                  [this](const juce::FileChooser& chooser) {
                                      const auto folder = chooser.getResult();
                                      if (folder.isDirectory()) {
                                          studio_.samples().addFolder(folder);
                                          studio_.samples().rescan(false);
                                      }
                                  });
        }
        ImGui::Separator();
        auto banks = library.includeBanks();
        const auto bankLabel = ui::Ls("Inclure les banques de CyDAW (", "Include CyDAW's banks (")
                               + juce::String(static_cast<int>(library.banks().size())) + ")";
        if (ImGui::Checkbox(bankLabel.toRawUTF8(), &banks))
            library.setIncludeBanks(banks);
        ui::tooltip(L("Les sons des banques téléchargées ou générées (Options › Banques de sons), rangés par catégorie et "
                      "par instrument.",
                      "The sounds of the banks downloaded or generated (Options › Sound banks), sorted by category and "
                      "instrument."));
        if (ImGui::Selectable(L("Analyser à nouveau", "Rescan")))
            library.rescan(false);
        ImGui::Checkbox(L("Écoute au clic", "Preview on click"), &autoPreview_);
        ImGui::SameLine();
        ImGui::Checkbox(L("Au tempo", "At tempo"), &state_.auditionAtTempo);
        ui::tooltip(L("Les boucles au tempo du projet, calées sur la mesure quand le transport tourne.",
                      "Loops at the project's tempo, in step with the bar while the transport runs."));
        ImGui::Checkbox(L("Arbre des catégories", "Category tree"), &showTree_);
        ImGui::Separator();
        if (ImGui::Selectable(L("Recherche sémantique (IA)…", "Semantic search (AI)…")))
            state_.showSemantic = true;
        ImGui::EndPopup();
    }

    // The semantic search asked once the typing pauses, or on Enter.
    if (semanticMode_ && (semanticPending_ || entered) && (entered || ImGui::GetTime() - editedAt_ > 0.6)) {
        semanticPending_ = false;
        const auto text = juce::String::fromUTF8(search_).trim();
        semanticSimilarTo_.clear();
        semanticHits_.clear();
        semanticError_.clear();
        semanticEnglish_.clear();
        semanticAsked_ = text;
        if (text.isNotEmpty())
            (void) semantic.searchText(text);
        dirty_ = true;
    } else if (semanticMode_ && semanticPending_) {
        host_.wake(2);
    }
    if (semanticMode_)
        drawSemanticLine();

    if (dirty_ || std::string(search_) != lastSearch_ || catalog_.data() != data_) {
        lastSearch_ = search_;
        refilter();
    }

    const auto progress = library.progress();
    if (progress.scanning) {
        auto text = ui::Ls("Analyse des sons : ", "Analysing the sounds: ");
        if (progress.toAnalyse > 0)
            text << progress.analysed << " / " << progress.toAnalyse;
        else
            text << progress.found << ui::Ls(" trouvés", " found");
        ImGui::TextColored(theme::vec(p.accent), "%s", text.toRawUTF8());
        host_.wake(2);
    } else if (progress.classifying) {
        ImGui::TextColored(theme::vec(p.accent), "%s", progress.message.toRawUTF8());
        host_.wake(2);
    }

    if (!library.loaded() || data_ == nullptr || (data_->items.empty() && catalog_.building())) {
        ImGui::Spacing();
        ImGui::TextColored(theme::vec(p.textDim), "%s", L("Lecture de la bibliothèque…", "Reading the library…"));
        host_.wake(6);
        return;
    }
    if (library.samples().empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", L("Ajoutez un dossier de samples avec le bouton dossier, ou téléchargez des banques "
                                   "(Options › Banques de sons) : tout est analysé en tâche de fond (durée, tempo, "
                                   "tonalité, catégorie).",
                                   "Add a sample folder with the folder button, or download banks (Options › Sound "
                                   "banks): everything is analysed in the background (length, tempo, key, category)."));
        return;
    }

    // The tree, its height dragged below it.
    if (showTree_) {
        const auto available = ImGui::GetContentRegionAvail().y;
        treeHeight_ = std::clamp(treeHeight_, dp(60.0f), std::max(dp(60.0f), available - dp(150.0f)));
        drawSampleTree(treeHeight_);
        ImGui::InvisibleButton("##treeSplitter", ImVec2(ImGui::GetContentRegionAvail().x, dp(6.0f)));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (ImGui::IsItemActive())
            treeHeight_ += ImGui::GetIO().MouseDelta.y;
        const auto min = ImGui::GetItemRectMin();
        const auto max = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x + dp(30.0f), (min.y + max.y) * 0.5f),
                                            ImVec2(max.x - dp(30.0f), (min.y + max.y) * 0.5f),
                                            ImGui::IsItemHovered() || ImGui::IsItemActive() ? p.accent : p.lineStrong, dp(2.0f));
    }

    // The table.
    const auto selectedRecord = selectedPath_.isNotEmpty() ? library.find(selectedPath_) : nullptr;
    const auto detailsHeight = selectedRecord != nullptr ? dp(104.0f) : 0.0f;
    const auto scored = !result_.scores.empty();
    const auto flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV
                       | ImGuiTableFlags_SizingStretchProp;
    const auto& rows = result_.rows;
    if (rows.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(theme::vec(p.textFaint), "%s",
                           semanticMode_ && semanticAsked_.isEmpty() && semanticSimilarTo_.isEmpty()
                               ? L("Décrivez un son dans la recherche.", "Describe a sound in the search.")
                               : L("Aucun son ne correspond.", "No sound matches."));
        ImGui::Dummy(ImVec2(0.0f, std::max(0.0f, ImGui::GetContentRegionAvail().y - detailsHeight)));
    } else if (ImGui::BeginTable("##samples", 5, flags, ImVec2(0.0f, -detailsHeight))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("##fav", ImGuiTableColumnFlags_WidthFixed, dp(18.0f));
        ImGui::TableSetupColumn((ui::Ls("Nom", "Name") + " (" + juce::String(static_cast<int>(rows.size())) + ")##name").toRawUTF8(),
                                ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn(L("Cat.", "Cat."), ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn(scored ? "%" : "BPM", ImGuiTableColumnFlags_WidthFixed, dp(36.0f));
        ImGui::TableSetupColumn(L("Durée", "Length"), ImGuiTableColumnFlags_WidthFixed, dp(46.0f));
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows.size()));
        while (clipper.Step()) {
            for (auto row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto index = rows[static_cast<std::size_t>(row)];
                const auto& item = data_->items[static_cast<std::size_t>(index)];
                const auto* record = library.find(item.path);
                const auto favourite = record != nullptr ? record->favourite : item.favourite;
                ImGui::PushID(index);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushStyleColor(ImGuiCol_Text, favourite ? p.accent : p.textFaint);
                ImGui::TextUnformatted(favourite ? icon::starFill : icon::star);
                ImGui::PopStyleColor();
                if (ImGui::IsItemClicked() && record != nullptr)
                    library.setFavourite(item.path, !favourite);
                ImGui::TableSetColumnIndex(1);
                const auto selected = item.path == selectedPath_;
                if (ImGui::Selectable(item.label.toRawUTF8(), selected,
                                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap
                                          | ImGuiSelectableFlags_AllowDoubleClick)) {
                    selectedPath_ = item.path;
                    if (autoPreview_)
                        (void) studio_.auditionSample(item.path, 0.0, state_.auditionAtTempo);
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        // Into the selected audio track at the playhead, or a new one.
                        const juce::File file(item.path);
                        auto placed = false;
                        if (state_.selectedTrack && !state_.selectedTrack->midi && !state_.selectedTrack->master) {
                            std::uint64_t clipId {};
                            placed = engine.addAudioClipFromFile(state_.selectedTrack->audioIndex, file, engine.position(), &clipId).wasOk();
                        }
                        if (!placed)
                            if (const auto track = studio_.addAudioTrack(file, engine.position()))
                                state_.selectedTrack = track;
                        studio_.markDirty();
                    }
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                    auto where = item.groupText;
                    if (item.sourceText != item.groupText)
                        where << "  \xc2\xb7  " << item.sourceText;
                    ui::tooltip(where.toRawUTF8());
                }
                if (record != nullptr && ImGui::BeginPopupContextItem("##sampleMenu")) {
                    selectedPath_ = item.path;
                    drawSampleMenu(*record);
                    ImGui::EndPopup();
                }
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                    const auto path = item.path.toStdString();
                    ImGui::SetDragDropPayload(kFilePayload, path.c_str(), path.size() + 1);
                    ImGui::TextUnformatted(item.label.toRawUTF8());
                    ImGui::TextDisabled("%s", L("Déposez sur une piste audio ou dans le vide", "Drop on an audio track or empty space"));
                    ImGui::EndDragDropSource();
                }
                ImGui::TableSetColumnIndex(2);
                ImGui::TextColored(theme::vec(item.aiCategory ? p.accent : p.textDim), "%s", item.categoryText.toRawUTF8());
                if (item.aiCategory && ImGui::IsItemHovered())
                    ui::tooltip(L("Catégorie trouvée par l'IA (clic droit sur le son pour la changer)",
                                  "Category found by the AI (right-click the sound to change it)"));
                ImGui::TableSetColumnIndex(3);
                if (scored) {
                    if (static_cast<std::size_t>(row) < result_.scores.size())
                        ImGui::TextColored(theme::vec(p.textDim), "%.0f", result_.scores[static_cast<std::size_t>(row)]);
                } else {
                    ImGui::TextColored(theme::vec(p.textDim), "%s", item.bpmText.toRawUTF8());
                }
                ImGui::TableSetColumnIndex(4);
                ImGui::TextColored(theme::vec(p.textFaint), "%s", item.lengthText.toRawUTF8());
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }

    // The selected sample: its shape, where it comes from, the preview.
    if (const auto* record = selectedPath_.isNotEmpty() ? library.find(selectedPath_) : nullptr) {
        ImGui::Separator();
        if (ui::iconButton(engine.isSamplePreviewing() ? icon::stop : icon::play, L("Écouter / arrêter", "Preview / stop"),
                           engine.isSamplePreviewing(), p.play)) {
            if (engine.isSamplePreviewing())
                engine.stopSamplePreview();
            else
                (void) studio_.auditionSample(record->path, 0.0, state_.auditionAtTempo);
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(record->name.toRawUTF8());
        ImGui::SameLine();
        ImGui::TextDisabled("%s %s %s", record->key.toRawUTF8(), record->type.toRawUTF8(),
                            record->channels == 1 ? "mono" : "stereo");
        if (semantic.available()) {
            ImGui::SameLine();
            if (ImGui::SmallButton(L("Similaires", "Similar")))
                askSimilar(*record);
            ui::tooltip(L("Les sons de la bibliothèque qui ressemblent à celui-ci (IA)",
                          "The library's sounds that sound like this one (AI)"));
        }
        {
            auto where = categoryLabel(record->category);
            if (record->instrument.isNotEmpty())
                where << "  \xe2\x80\xba  " << record->instrument;
            where << "  \xc2\xb7  " << library.sourceTitle(*record);
            ImGui::TextColored(theme::vec(p.textFaint), "%s", where.toRawUTF8());
        }
        const auto origin = ImGui::GetCursorScreenPos();
        const ImVec2 size(ImGui::GetContentRegionAvail().x, std::max(dp(30.0f), ImGui::GetContentRegionAvail().y - dp(4.0f)));
        auto* list = ImGui::GetWindowDrawList();
        list->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), p.bg0, dp(3.0f));
        if (!record->overview.empty()) {
            const auto count = static_cast<float>(record->overview.size());
            const auto barWidth = size.x / count;
            for (std::size_t index = 0; index < record->overview.size(); ++index) {
                const auto value = static_cast<float>(record->overview[index]) / 255.0f;
                const auto x = origin.x + static_cast<float>(index) * barWidth;
                const auto half = size.y * 0.5f * value;
                list->AddRectFilled(ImVec2(x, origin.y + size.y * 0.5f - half), ImVec2(x + std::max(1.0f, barWidth - 1.0f),
                                                                                       origin.y + size.y * 0.5f + half),
                                    p.audio);
            }
        }
        if (engine.isSamplePreviewing() && record->length > 0.0) {
            const auto x = origin.x + static_cast<float>(engine.samplePreviewPositionSeconds() / record->length) * size.x;
            list->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + size.y), p.accent, dp(1.5f));
        }
        ImGui::InvisibleButton("##overview", size);
        if (ImGui::IsItemClicked() && record->length > 0.0) {
            const auto t = (ImGui::GetIO().MousePos.x - origin.x) / size.x * record->length;
            (void) studio_.auditionSample(record->path, std::max(0.0, t), state_.auditionAtTempo);
        }
        if (ImGui::BeginDragDropSource()) {
            const auto path = record->path.toStdString();
            ImGui::SetDragDropPayload(kFilePayload, path.c_str(), path.size() + 1);
            ImGui::TextUnformatted(record->name.toRawUTF8());
            ImGui::EndDragDropSource();
        }
    }
}

void BrowserView::report(const juce::String& message, const bool error)
{
    state_.status = message;
    state_.statusIsError = error;
    state_.statusTime = ImGui::GetTime();
}

void BrowserView::drawInstruments()
{
    const auto& p = theme::palette();
    if (ImGui::Button(L("Nouveau sampler…", "New sampler…"))) {
        chooser_ = std::make_unique<juce::FileChooser>(L("Les échantillons du sampler", "The sampler's samples"), juce::File {},
                                                       "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this](const juce::FileChooser& chooser) {
                                  if (chooser.getResults().isEmpty())
                                      return;
                                  juce::String error;
                                  if (const auto track = studio_.newSamplerTrack(chooser.getResults(), error)) {
                                      state_.selectTrack(*track);
                                      report(studio_.trackName(*track));
                                  } else {
                                      report(error, true);
                                  }
                              });
    }
    ui::tooltip(L("Des échantillons sur une nouvelle piste : la note de chacun vient de son nom ou de sa hauteur.",
                  "Samples on a new track: each one's note comes from its name or its pitch."));
    ImGui::SameLine();
    if (ImGui::Button(L("Ouvrir un SFZ…", "Open an SFZ…"))) {
        chooser_ = std::make_unique<juce::FileChooser>(L("Un instrument SFZ ou une carte .cysampler", "An SFZ instrument or a .cysampler map"),
                                                       juce::File {}, "*.sfz;*.cysampler");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this](const juce::FileChooser& chooser) {
                                  if (chooser.getResult() == juce::File())
                                      return;
                                  juce::String error;
                                  if (const auto track = studio_.loadSamplerFile(chooser.getResult(), std::nullopt, error)) {
                                      state_.selectTrack(*track);
                                      report(studio_.trackName(*track));
                                  } else {
                                      report(error, true);
                                  }
                              });
    }
    // Beside them when it fits, else under them.
    const auto* banksLabel = L("Banques de sons…", "Sound banks…");
    ImGui::SameLine();
    if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize(banksLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f)
        ImGui::NewLine();
    if (ImGui::Button(banksLabel))
        state_.showBanks = true;
    ui::tooltip(L("D'où viennent les échantillons des instruments, leur licence, et leur téléchargement.",
                  "Where the instruments' samples come from, their licence, and their download."));
    const auto download = studio_.banks().progress();
    if (ImGui::GetTime() - instrumentsTime_ > 5.0 || (downloading_ && !download.running)) {
        instrumentsTime_ = ImGui::GetTime();
        instruments_ = studio_.factoryInstruments();
    }
    downloading_ = download.running;
    if (instruments_.empty()) {
        ImGui::TextWrapped("%s", L("Aucun instrument d'usine : le dossier Factory n'est pas à côté de CyDAW.",
                                   "No factory instrument: the Factory folder is not beside CyDAW."));
        return;
    }
    // The instrument track picked: a double click puts it there, in place of
    // its instrument (its effects kept); without one, a new track.
    std::optional<TrackRef> onto;
    if (state_.selectedTrack && state_.selectedTrack->midi && !state_.selectedTrack->master && studio_.isValid(*state_.selectedTrack))
        onto = state_.selectedTrack;
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    if (onto)
        ImGui::TextWrapped("%s", (ui::Ls("Double-clic : il remplace l'instrument de « ", "Double click: it replaces the instrument of \"")
                                  + studio_.trackName(*onto)
                                  + ui::Ls(" » (ses effets restent). Clic droit : une nouvelle piste, le modèle de son.",
                                           "\" (its effects stay). Right click: a new track, the sound model."))
                                     .toRawUTF8());
    else
        ImGui::TextWrapped("%s", L("Double-clic : une nouvelle piste (ses échantillons téléchargés s'il le faut). Clic droit : "
                                   "le modèle de son.",
                                   "Double click: a new track (its samples downloaded if need be). Right click: the sound model."));
    ImGui::PopStyleColor();
    ImGui::BeginChild("##instrumentList");
    juce::StringArray categories;
    for (const auto& instrument : instruments_)
        categories.addIfNotAlreadyThere(instrument.category);
    for (const auto& category : categories) {
        const auto first = std::ranges::find_if(instruments_, [&category](const auto& item) { return item.category == category; });
        ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
        if (!ImGui::TreeNode(ui::labelId(first->categoryName.toRawUTF8(), category.toRawUTF8()).c_str()))
            continue;
        for (const auto& instrument : instruments_) {
            if (instrument.category != category)
                continue;
            ImGui::PushID(instrument.key.toRawUTF8());
            // Its samples still to download: the track is made at once and
            // plays as soon as they are here.
            const auto missing = instrument.missingFiles > 0;
            const auto fetching = download.running && download.label == instrument.name;
            const auto startDownload = [&] {
                juce::String error;
                if (studio_.downloadFactoryInstrument(instrument.key, error))
                    report((studio_.banks().generatedOnly(instrument.banks)
                                ? ui::Ls("Génération des sons de ", "Generating the sounds of ")
                                : ui::Ls("Téléchargement des échantillons de ", "Downloading the samples of "))
                           + instrument.name + " (" + ui::sizeText(instrument.missingBytes) + ")");
                else
                    report(error, true);
            };
            if (missing)
                ImGui::PushStyleColor(ImGuiCol_Text, theme::vec(p.textDim));
            if (ImGui::Selectable(instrument.name.toRawUTF8(), false, ImGuiSelectableFlags_AllowDoubleClick)
                && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                juce::String error;
                if (missing && !fetching)
                    startDownload();
                if (const auto track = studio_.addFactoryInstrument(instrument.key, error, false, onto)) {
                    state_.selectTrack(*track);
                    if (!missing)
                        report(studio_.trackName(*track));
                } else {
                    report(error, true);
                }
            }
            if (missing)
                ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) {
                auto text = instrument.range + "  "
                            + (instrument.model != juce::File() ? ui::Ls("(échantillons ; clic droit : modèle de son)",
                                                                         "(samples; right click: sound model)")
                                                                : ui::Ls("(échantillons)", "(samples)"));
                if (missing && studio_.banks().generatedOnly(instrument.banks))
                    text << "\n"
                         << ui::Ls("Sons à générer (fabriqués par CyDAW, rien à télécharger) : ",
                                   "Sounds to generate (made by CyDAW, nothing to download): ")
                         << ui::sizeText(instrument.missingBytes) << "\n"
                         << ui::Ls("Double-clic : la piste, et ses sons générés ; elle joue dès qu'ils sont là.",
                                   "Double click: the track, and its sounds generated; it plays as soon as they are here.");
                else if (missing)
                    text << "\n"
                         << ui::Ls("Échantillons à télécharger : ", "Samples to download: ") << ui::sizeText(instrument.missingBytes)
                         << " (" << instrument.banks.joinIntoString(", ") << ")\n"
                         << ui::Ls("Double-clic : la piste, et ses échantillons téléchargés ; elle joue dès qu'ils sont là.",
                                   "Double click: the track, and its samples downloaded; it plays as soon as they are here.");
                ImGui::SetTooltip("%s", text.toRawUTF8());
            }
            if (ImGui::BeginPopupContextItem("##instrumentMenu")) {
                const auto add = [&](const bool model, const std::optional<TrackRef> on) {
                    juce::String error;
                    if (const auto track = studio_.addFactoryInstrument(instrument.key, error, model, on)) {
                        state_.selectTrack(*track);
                        report(studio_.trackName(*track));
                    } else {
                        report(error, true);
                    }
                };
                if (onto) {
                    const auto label = ui::Ls("Remplacer l'instrument de « ", "Replace the instrument of \"") + studio_.trackName(*onto)
                                       + ui::Ls(" »", "\"");
                    if (ImGui::MenuItem(label.toRawUTF8()))
                        add(false, onto);
                    if (ImGui::MenuItem(L("Le remplacer par son modèle de son", "Replace it with its sound model"), nullptr, false,
                                        instrument.model != juce::File()))
                        add(true, onto);
                    ImGui::Separator();
                }
                if (ImGui::MenuItem(L("Nouvelle piste : échantillons (sampler)", "New track: samples (sampler)")))
                    add(false, std::nullopt);
                if (ImGui::MenuItem(L("Nouvelle piste : modèle de son", "New track: sound model"), nullptr, false,
                                    instrument.model != juce::File()))
                    add(true, std::nullopt);
                if (missing) {
                    ImGui::Separator();
                    const auto label = (studio_.banks().generatedOnly(instrument.banks)
                                            ? ui::Ls("Générer les sons (", "Generate the sounds (")
                                            : ui::Ls("Télécharger les échantillons (", "Download the samples ("))
                                       + ui::sizeText(instrument.missingBytes) + ")";
                    if (ImGui::MenuItem(label.toRawUTF8(), nullptr, false, !fetching))
                        startDownload();
                }
                ImGui::EndPopup();
            }
            // On the right: its range, what is left to download, or the download.
            if (fetching) {
                const auto fraction = download.bytesTotal > 0
                                          ? static_cast<float>(static_cast<double>(download.bytesDone)
                                                               / static_cast<double>(download.bytesTotal))
                                          : 0.0f;
                ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - dp(90.0f));
                ImGui::ProgressBar(fraction, ImVec2(dp(90.0f), ImGui::GetTextLineHeight()), "");
            } else {
                const auto right = missing ? juce::String::fromUTF8(icon::download) + " " + ui::sizeText(instrument.missingBytes) : instrument.range;
                ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::CalcTextSize(right.toRawUTF8()).x);
                ImGui::TextColored(theme::vec(missing ? p.accent : p.textFaint), "%s", right.toRawUTF8());
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
    ImGui::EndChild();
}

void BrowserView::drawDevices()
{
    const auto& p = theme::palette();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##deviceSearch", L("Rechercher un périphérique…", "Search a device…"), deviceSearch_,
                             sizeof(deviceSearch_));
    if (!state_.selectedTrack) {
        ImGui::TextDisabled("%s", L("Sélectionnez une piste.", "Select a track."));
        return;
    }
    const auto track = *state_.selectedTrack;
    auto* slot = studio_.graphFor(track);
    if (slot == nullptr)
        return;
    ImGui::TextDisabled("%s %s", L("Double-clic : ajouter à", "Double click: add to"), studio_.trackName(track).toRawUTF8());
    const auto query = fold(juce::String::fromUTF8(deviceSearch_).trim());
    if (devicesFor_ != track || ImGui::GetTime() - devicesTime_ > 10.0) {
        devicesFor_ = track;
        devicesTime_ = ImGui::GetTime();
        devices_ = slot->graph().deviceCatalogue();
    }
    const auto& entries = devices_;
    ImGui::BeginChild("##deviceList");
    juce::String lastCategory;
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const auto& entry = entries[index];
        if (!track.midi && entry.soundSource)
            continue;
        if (query.isNotEmpty() && !fold(entry.name).contains(query) && !fold(entry.category).contains(query)
            && !fold(entry.description).contains(query))
            continue;
        const auto heading = entry.kind == "preset" ? ui::Ls("Chaînes complètes", "Whole chains")
                             : entry.category.isNotEmpty() ? entry.category
                                                           : ui::Ls("Autres", "Others");
        if (query.isEmpty() && heading != lastCategory) {
            lastCategory = heading;
            ImGui::PushFont(theme::boldFont(), ui::currentFontBase() * 0.8f);
            ImGui::TextColored(theme::vec(p.textFaint), "%s", heading.toUpperCase().toRawUTF8());
            ImGui::PopFont();
        }
        ImGui::PushID(static_cast<int>(index));
        if (ImGui::Selectable(entry.name.toRawUTF8(), false, ImGuiSelectableFlags_AllowDoubleClick)
            && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            const auto result = studio_.insertDevice(track, entry.key);
            state_.status = result.wasOk() ? entry.name : result.getErrorMessage();
            state_.statusTime = ImGui::GetTime();
        }
        if (ImGui::IsItemHovered() && entry.description.isNotEmpty())
            ImGui::SetTooltip("%s", entry.description.toRawUTF8());
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void BrowserView::browse(const juce::String& query, const bool semantic, const juce::String& family, const juce::String& category,
                         const juce::String& source)
{
    query.copyToUTF8(search_, sizeof(search_));
    lastSearch_.clear();
    filterFamily_ = family;
    filterCategory_ = category;
    filterGroup_.clear();
    filterSource_ = source.isEmpty() ? juce::String() : source.startsWith("bank:") || source == "banks" || source == "folders"
                                                                ? source
                                                                : "bank:" + source;
    semanticMode_ = semantic;
    semanticHits_.clear();
    semanticSimilarTo_.clear();
    semanticError_.clear();
    semanticEnglish_.clear();
    semanticAsked_.clear();
    // Asked at the next frame.
    semanticPending_ = semantic && query.isNotEmpty();
    editedAt_ = -100.0;
    dirty_ = true;
    showSamplesTab_ = true;
    host_.wake();
}

void BrowserView::selectSample(const juce::File& file)
{
    search_[0] = 0;
    lastSearch_.clear();
    favouritesOnly_ = false;
    filterFamily_ = filterCategory_ = filterGroup_ = filterSource_ = {};
    semanticMode_ = false;
    semanticHits_.clear();
    semanticSimilarTo_.clear();
    selectedPath_ = file.getFullPathName();
    dirty_ = true;
    showSamplesTab_ = true;
    host_.wake();
}

} // namespace cydaw::v2
