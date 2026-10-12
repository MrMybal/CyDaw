#include "WorkshopView.h"

#include "GraphEditor.h"
#include "Icons.h"
#include "ImGuiHost.h"
#include "InterfaceEditor.h"
#include "Locations.h"
#include "NodeTree.h"
#include "Theme.h"
#include "Vst3Build.h"
#include "Widgets.h"

#include <cydaw/graph/GraphPluginExport.h>

#include <imgui_internal.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace cydaw::v2 {

using ui::dp;
using ui::L;

namespace {

void copyInto(std::vector<char>& buffer, const juce::String& text)
{
    if (buffer.empty())
        return;
    const auto* utf8 = text.toRawUTF8();
    auto length = std::min(std::strlen(utf8), buffer.size() - 1);
    // Never half a character.
    while (length > 0 && length < std::strlen(utf8) && (static_cast<unsigned char>(utf8[length]) & 0xc0) == 0x80)
        --length;
    std::memcpy(buffer.data(), utf8, length);
    buffer[length] = 0;
}

juce::String textOf(const std::vector<char>& buffer)
{
    return juce::String::fromUTF8(buffer.data());
}

const char* issueText(const cydaw::graph::PluginExportIssueCode code)
{
    using Code = cydaw::graph::PluginExportIssueCode;
    switch (code) {
    case Code::invalidIdentity:
        return L("L'identité du plugin est incomplète : un nom, un éditeur, et deux codes de 4 lettres ou chiffres différents.",
                 "The plugin's identity is incomplete: a name, a maker, and two different 4-character codes.");
    case Code::missingGraph:
    case Code::invalidGraph: return L("Le graphe n'est pas valide.", "The graph is not valid.");
    case Code::unsupportedRole:
        return L("Seuls un effet audio ou un instrument deviennent un VST3.", "Only an audio effect or an instrument becomes a VST3.");
    case Code::unsupportedBoundaryPort:
        return L("Une entrée ou sortie de contrôle publique ne passe pas dans un VST3.",
                 "A public control input or output cannot go into a VST3.");
    case Code::invalidAudioFormat:
        return L("Une entrée ou sortie audio n'a pas de format exact (mono, stéréo).",
                 "An audio input or output has no exact layout (mono, stereo).");
    case Code::missingAudioOutput: return L("Le périphérique n'a pas de sortie audio.", "The device has no audio output.");
    case Code::invalidParameterId:
    case Code::duplicateParameterId:
        return L("Des paramètres ont un identifiant invalide ou en double.", "Some parameters have an invalid or repeated id.");
    case Code::compileFailure: return L("Le graphe ne compile pas.", "The graph does not compile.");
    }
    return "";
}

juce::File exportFolderFor(const juce::String& productName)
{
    auto name = juce::File::createLegalFileName(productName).trim();
    if (name.isEmpty())
        name = "Plugin";
    return locations::folder(Location::pluginExports).getChildFile(name);
}

} // namespace

WorkshopView::WorkshopView(Studio& studio, UiState& state, ImGuiHost& host, DeviceSignals& signals, Vst3Build& build)
    : studio_(studio),
      state_(state),
      host_(host),
      graph_(std::make_unique<GraphEditor>(studio, state, host, signals, GraphEditor::Mode::workshop)),
      interface_(std::make_unique<InterfaceEditor>(studio, state)),
      build_(build)
{
    const auto tools = Vst3Build::findTools();
    copyInto(sourceRoot_, tools.sourceRoot.getFullPathName());
}

WorkshopView::~WorkshopView() = default;

bool WorkshopView::askBeforeQuit(std::function<void()> then)
{
    if (!studio_.workshopAtRisk())
        return false;
    quitWaiting_ = std::move(then);
    closingTab_ = 0;
    (void) askAboutNextAtRisk();
    state_.bringToFront = Panel::workshop;
    host_.wake();
    return true;
}

bool WorkshopView::askAboutNextAtRisk()
{
    for (const auto& tab : studio_.workshopTabs())
        if (studio_.workshopTabAtRisk(tab.id)) {
            studio_.selectWorkshopTab(tab.id);
            askDiscard_ = true;
            return true;
        }
    return false;
}

void WorkshopView::report(const juce::String& message, const bool error)
{
    state_.status = message;
    state_.statusIsError = error;
    state_.statusTime = ImGui::GetTime();
}

void WorkshopView::refreshLibrary(const bool force)
{
    if (!force && ImGui::GetTime() - libraryTime_ < 2.0)
        return;
    library_ = studio_.libraryDevices();
    libraryTime_ = ImGui::GetTime();
}

bool WorkshopView::textField(const char* id, std::vector<char>& buffer, const juce::String& current, const float width,
                             const bool multiline, const float height)
{
    // Refreshed from the model unless the person is typing in it.
    if (ImGui::GetActiveID() != ImGui::GetID(id))
        copyInto(buffer, current);
    if (multiline) {
        ImGui::InputTextMultiline(id, buffer.data(), buffer.size(), ImVec2(width, height));
    } else {
        ImGui::SetNextItemWidth(width);
        ImGui::InputText(id, buffer.data(), buffer.size());
    }
    return ImGui::IsItemDeactivatedAfterEdit();
}

// ---- Opening, saving -------------------------------------------------------------------

void WorkshopView::perform(const WorkshopRequest& request)
{
    auto result = juce::Result::ok();
    switch (request.kind) {
    case WorkshopRequest::Kind::trackDevice: result = studio_.openWorkshopFromTrack(request.track, request.nodeId); break;
    case WorkshopRequest::Kind::file: result = studio_.openWorkshopFile(request.file); break;
    case WorkshopRequest::Kind::newEffect:
        studio_.newWorkshopDevice(false, ui::Ls("Mon effet", "My effect"), request.inputChannels, request.outputChannels);
        break;
    case WorkshopRequest::Kind::newInstrument: studio_.newWorkshopDevice(true, ui::Ls("Mon instrument", "My instrument")); break;
    }
    if (result.failed())
        report(result.getErrorMessage(), true);
    state_.bringToFront = Panel::workshop;
}

void WorkshopView::save(const bool asNewFile)
{
    juce::File written;
    if (const auto saved = studio_.saveWorkshop(written, asNewFile); saved.failed()) {
        report(saved.getErrorMessage(), true);
        return;
    }
    report(ui::Ls("Enregistré dans Mes périphériques : ", "Saved into My devices: ") + written.getFileName(), false);
    refreshLibrary(true);
}

void WorkshopView::drawDiscard()
{
    if (askDiscard_) {
        ImGui::OpenPopup("##workshopDiscard");
        askDiscard_ = false;
    }
    if (ImGui::BeginPopupModal("##workshopDiscard", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushFont(theme::boldFont(), 0.0f);
        ImGui::Text(L("« %s » n'est pas enregistré", "\"%s\" is not saved"), studio_.workshop().name.toRawUTF8());
        ImGui::PopFont();
        ImGui::TextDisabled("%s", L("Il n'est sur aucune piste : sans l'enregistrer, il est perdu.",
                                    "It is on no track: unless it is saved, it is lost."));
        ImGui::Spacing();
        const auto proceed = [this] {
            if (quitWaiting_) {
                // That device settled: the next one, then quitting.
                studio_.closeWorkshopTab(0);
                if (!askAboutNextAtRisk())
                    std::exchange(quitWaiting_, {})();
            } else if (closingTab_ != 0) {
                studio_.closeWorkshopTab(closingTab_);
            }
            closingTab_ = 0;
        };
        if (ui::primaryButton(L("Enregistrer", "Save"))) {
            save(false);
            if (!studio_.workshop().unsaved)
                proceed();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(L("Ne pas enregistrer", "Don't save"))) {
            proceed();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(L("Annuler", "Cancel")) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            closingTab_ = 0;
            quitWaiting_ = {};
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (askRemove_) {
        ImGui::OpenPopup("##workshopRemove");
        askRemove_ = false;
    }
    if (ImGui::BeginPopupModal("##workshopRemove", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushFont(theme::boldFont(), 0.0f);
        ImGui::Text(L("Mettre « %s » à la corbeille ?", "Move \"%s\" to the recycle bin?"),
                    removing_.getFileNameWithoutExtension().toRawUTF8());
        ImGui::PopFont();
        ImGui::TextDisabled("%s", L("Les pistes qui l'utilisent gardent leur copie.", "Tracks using it keep their copy."));
        ImGui::Spacing();
        if (ui::primaryButton(L("Mettre à la corbeille", "Move to the recycle bin"))) {
            if (const auto removed = studio_.removeLibraryDevice(removing_); removed.failed())
                report(removed.getErrorMessage(), true);
            refreshLibrary(true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(L("Annuler", "Cancel")) || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ---- The panel ---------------------------------------------------------------------------

void WorkshopView::draw()
{
    // What other panels asked for: at once, unless a device would be lost.
    if (auto request = std::exchange(state_.workshopRequest, std::nullopt))
        perform(*request);
    studio_.checkWorkshopLink();
    refreshLibrary(false);

    drawTopBar();
    drawTabs();

    const auto available = ImGui::GetContentRegionAvail();
    const auto libraryWidth = showLibrary_ ? std::min(dp(250.0f), available.x * 0.22f) : 0.0f;
    const auto sideWidth = showSide_ ? std::min(dp(380.0f), available.x * 0.3f) : 0.0f;
    const auto gap = ImGui::GetStyle().ItemSpacing.x;
    if (showLibrary_) {
        ImGui::BeginChild("##workshopLibrary", ImVec2(libraryWidth, available.y), ImGuiChildFlags_None);
        if (interfaceMode_ && studio_.workshopRoot() != nullptr) {
            interface_->drawPalette(*studio_.workshopRoot());
        } else if (ImGui::BeginTabBar("##workshopLeft")) {
            if (ImGui::BeginTabItem(L("Nœuds", "Nodes"))) {
                drawPalette();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(L("Mes périphériques", "My devices"))) {
                drawLibrary();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::EndChild();
        ImGui::SameLine(0.0f, gap);
    }
    const auto graphWidth = available.x - libraryWidth - sideWidth - (showLibrary_ ? gap : 0.0f) - (showSide_ ? gap : 0.0f);
    // The graph, and under it the simulator that plays the device.
    const auto simulator = showSimulator_ && studio_.workshopGraph() != nullptr;
    // An instrument's has its keyboard.
    const auto simulatorHeight = simulator ? std::min(studio_.simulatesInstrument() ? dp(168.0f) : dp(78.0f), available.y * 0.4f) : 0.0f;
    ImGui::BeginGroup();
    ImGui::BeginChild("##workshopGraph",
                      ImVec2(std::max(dp(120.0f), graphWidth),
                             available.y - simulatorHeight - (simulator ? ImGui::GetStyle().ItemSpacing.y : 0.0f)),
                      ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (studio_.workshopGraph() == nullptr) {
        // Nothing open: the two ways in.
        ui::emptyState(L("L'atelier de périphériques", "The device workshop"),
                       L("Fabriquez un effet ou un instrument avec un graphe : il s'enregistre dans Mes périphériques, "
                         "se met sur n'importe quelle piste, et s'exporte en VST3.",
                         "Build an effect or an instrument with a graph: it is saved into My devices, goes on any track, "
                         "and exports as a VST3."));
        const auto width = dp(170.0f);
        ImGui::SetCursorPos(ImVec2((ImGui::GetWindowWidth() - width * 3.0f - gap * 2.0f) * 0.5f, ImGui::GetWindowHeight() * 0.62f));
        if (ui::primaryButton(L("Effet stéréo", "Stereo effect"), ImVec2(width, 0.0f)))
            perform({ WorkshopRequest::Kind::newEffect });
        ImGui::SameLine(0.0f, gap);
        if (ui::primaryButton(L("Effet mono", "Mono effect"), ImVec2(width, 0.0f))) {
            WorkshopRequest request { WorkshopRequest::Kind::newEffect };
            request.inputChannels = 1;
            request.outputChannels = 1;
            perform(request);
        }
        ImGui::SameLine(0.0f, gap);
        if (ui::primaryButton(L("Instrument", "Instrument"), ImVec2(width, 0.0f)))
            perform({ WorkshopRequest::Kind::newInstrument });
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - width * 3.0f - gap * 2.0f) * 0.5f);
        ImGui::TextDisabled("%s", L("Mono → stéréo, multicanal : le bouton + en haut.", "Mono to stereo, multichannel: the + button above."));
    } else if (interfaceMode_ && studio_.workshopRoot() != nullptr) {
        interface_->drawCanvas(*studio_.workshopRoot());
    } else {
        graph_->draw();
    }
    ImGui::EndChild();
    if (simulator) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::palette().bg1);
        ImGui::BeginChild("##workshopSimulator", ImVec2(std::max(dp(120.0f), graphWidth), simulatorHeight),
                          ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        drawSimulator();
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    ImGui::EndGroup();
    if (showSide_) {
        ImGui::SameLine(0.0f, gap);
        ImGui::BeginChild("##workshopSide", ImVec2(sideWidth, available.y), ImGuiChildFlags_None);
        drawSide();
        ImGui::EndChild();
    }
    drawDiscard();
    if (build_.running())
        host_.wake(2);
}

void WorkshopView::drawTopBar()
{
    const auto& p = theme::palette();
    const auto current = studio_.workshop();

    if (ImGui::Button((juce::String::fromUTF8(icon::back) + " Studio").toRawUTF8()))
        state_.workshopMode = false;
    ui::tooltip(L("Revenir au studio : pistes, arrangement, mixage (F7)", "Back to the studio: tracks, arrangement, mixing (F7)"));
    ImGui::SameLine(0.0f, dp(6.0f));
    if (ui::iconButton(icon::dockLeft, L("Nœuds et Mes périphériques", "Nodes and My devices"), showLibrary_))
        showLibrary_ = !showLibrary_;
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(icon::add, L("Nouveau périphérique (un nouvel onglet)", "New device (a new tab)")) || std::exchange(openNewMenu_, false))
        ImGui::OpenPopup("##workshopNew");
    if (ImGui::BeginPopup("##workshopNew")) {
        // Each in a tab of its own.
        const auto effect = [this](const int input, const int output) {
            WorkshopRequest request { WorkshopRequest::Kind::newEffect };
            request.inputChannels = input;
            request.outputChannels = output;
            state_.workshopRequest = request;
        };
        ImGui::TextDisabled("%s", L("Effet audio", "Audio effect"));
        if (ImGui::MenuItem(L("Stéréo", "Stereo")))
            effect(2, 2);
        if (ImGui::MenuItem("Mono"))
            effect(1, 1);
        if (ImGui::MenuItem(L("Mono → stéréo", "Mono to stereo")))
            effect(1, 2);
        if (ImGui::MenuItem(L("Stéréo → mono", "Stereo to mono")))
            effect(2, 1);
        if (ImGui::BeginMenu(L("Multicanal", "Multichannel"))) {
            for (const auto channels : { 3, 4, 5, 6, 8 })
                if (ImGui::MenuItem((juce::String(channels) + ui::Ls(" canaux", " channels")).toRawUTF8()))
                    effect(channels, channels);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(L("Instrument (MIDI → son)", "Instrument (MIDI to sound)")))
            state_.workshopRequest = WorkshopRequest { WorkshopRequest::Kind::newInstrument };
        if (state_.selectedTrack && state_.selectedTrack->midi
            && ImGui::MenuItem(L("L'instrument de la piste choisie", "The picked track's instrument")))
            state_.workshopRequest = WorkshopRequest { WorkshopRequest::Kind::trackDevice, *state_.selectedTrack, 0, {} };
        ImGui::EndPopup();
    }
    if (!current.open) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", L("Aucun périphérique ouvert", "No device open"));
        return;
    }
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(icon::save, L("Enregistrer dans Mes périphériques", "Save into My devices"), current.unsaved, p.accent))
        save(false);
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(icon::copy, L("Enregistrer une copie (un nouveau périphérique)", "Save a copy (a new device)")))
        save(true);
    ui::toolbarSeparator();

    // Its name, and what it is.
    auto* root = studio_.workshopRoot();
    if (root != nullptr) {
        if (textField("##workshopName", topName_, current.name, dp(200.0f))) {
            auto info = root->graph().editorGraphInfo();
            info.name = textOf(topName_);
            studio_.setWorkshopInfo(info);
        }
        ui::tooltip(L("Le nom du périphérique (et du plugin exporté)", "The device's name (and the exported plugin's)"));
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::vec(current.instrument ? p.midi : p.audio), "%s",
                       current.instrument ? L("Instrument", "Instrument") : L("Effet audio", "Audio effect"));
    ImGui::SameLine();
    if (current.unsaved)
        ImGui::TextColored(theme::vec(p.warn), "%s", L("● non enregistré", "● not saved"));
    else
        ImGui::TextDisabled("%s", L("enregistré", "saved"));
    if (ImGui::IsItemHovered() && current.file != juce::File())
        ImGui::SetTooltip("%s", current.file.getFullPathName().toRawUTF8());
    ui::toolbarSeparator();

    // The graph, or the window the plugin opens.
    {
        const auto& palette = theme::palette();
        const auto modeButton = [&palette](const char* label, const bool active) {
            ImGui::PushStyleColor(ImGuiCol_Button, active ? palette.accentDim : palette.bg2);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? palette.accent : palette.bg3);
            ImGui::PushStyleColor(ImGuiCol_Text, active ? palette.text : palette.textDim);
            const auto clicked = ImGui::Button(label);
            ImGui::PopStyleColor(3);
            return clicked;
        };
        if (modeButton(L("Graphe", "Graph"), !interfaceMode_))
            interfaceMode_ = false;
        ui::tooltip(L("Le graphe du périphérique : ses cartes et ses câbles", "The device's graph: its cards and cables"));
        ImGui::SameLine(0.0f, dp(2.0f));
        if (modeButton(L("Interface", "Interface"), interfaceMode_))
            interfaceMode_ = true;
        ui::tooltip(L("La fenêtre du plugin : ses potards, faders, courbes, à placer", "The plugin's window: its knobs, faders, curves, to place"));
    }
    ui::toolbarSeparator();

    // Tried on a track: linked to it, every change heard there.
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", L("Tester sur", "Try on"));
    ImGui::SameLine();
    {
        const auto shown = current.track ? studio_.trackName(*current.track) : ui::Ls("aucune piste", "no track");
        if (current.track) {
            const auto colour = theme::fromArgb(studio_.trackColour(*current.track));
            const auto chip = ImGui::GetCursorScreenPos();
            const auto height = ImGui::GetFrameHeight();
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(chip.x, chip.y + height * 0.2f),
                                                      ImVec2(chip.x + dp(4.0f), chip.y + height * 0.8f), colour, dp(2.0f));
            ImGui::Dummy(ImVec2(dp(6.0f), height));
            ImGui::SameLine(0.0f, dp(2.0f));
        }
        ImGui::SetNextItemWidth(dp(170.0f));
        if (ImGui::BeginCombo("##tryOn", shown.toRawUTF8())) {
            if (ImGui::Selectable(L("aucune piste", "no track"), !current.track) && current.track)
                studio_.unlinkWorkshop();
            for (const auto& track : studio_.tracks()) {
                const auto fits = !current.instrument || track.midi;
                const auto name = studio_.trackName(track) + (track.midi ? "  (MIDI)" : "");
                const auto selected = current.track && *current.track == track;
                if (ImGui::Selectable(name.toRawUTF8(), selected, fits ? ImGuiSelectableFlags_None : ImGuiSelectableFlags_Disabled)
                    && !selected) {
                    if (const auto linked = studio_.linkWorkshop(track); linked.failed())
                        report(linked.getErrorMessage(), true);
                    else
                        state_.selectTrack(track);
                }
            }
            ImGui::EndCombo();
        }
        ui::tooltip(current.instrument
                        ? L("L'instrument devient celui de la piste MIDI choisie et y reste lié : jouez-la (Espace) ou "
                            "jouez au clavier MIDI, chaque changement s'entend.",
                            "The instrument becomes the picked MIDI track's and stays linked: play it (Space) or a MIDI "
                            "keyboard, every change is heard.")
                        : L("L'effet entre dans la chaîne de la piste choisie et y reste lié : lancez la lecture (Espace), "
                            "chaque changement s'entend.",
                            "The effect goes into the picked track's chain and stays linked: press play (Space), every "
                            "change is heard."));
    }

    // On the right: the side panel, and closing.
    const auto right = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - ImGui::GetFrameHeight() * 2.0f - dp(4.0f);
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + dp(8.0f), right));
    if (ui::iconButton(icon::dockRight, L("Infos, paramètres, export", "Info, parameters, export"), showSide_))
        showSide_ = !showSide_;
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(icon::close, L("Fermer le périphérique (son onglet)", "Close the device (its tab)"))) {
        if (current.unsaved && !current.track) {
            closingTab_ = studio_.activeWorkshopTab();
            askDiscard_ = true;
        } else {
            studio_.closeWorkshop();
        }
    }
}

void WorkshopView::drawTabs()
{
    const auto tabs = studio_.workshopTabs();
    if (tabs.empty()) {
        ImGui::Separator();
        tabShown_ = 0;
        return;
    }
    const auto& p = theme::palette();
    const auto active = studio_.activeWorkshopTab();
    // Told once when the model chose another tab (opened, closed, the port).
    const auto forced = active != tabShown_;
    std::uint64_t closing {};
    ImGui::PushStyleColor(ImGuiCol_TabSelectedOverline, p.accent);
    if (ImGui::BeginTabBar("##workshopTabs", ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_TabListPopupButton)) {
        for (const auto& tab : tabs) {
            auto open = true;
            // Not saved: ImGui's dot in place of the cross.
            const auto label = tab.name + "###tab" + juce::String(static_cast<juce::int64>(tab.id));
            ImGui::PushStyleColor(ImGuiCol_Text, theme::vec(tab.instrument ? p.midi : p.text));
            const auto flags = (forced && tab.id == active ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)
                               | (tab.unsaved ? ImGuiTabItemFlags_UnsavedDocument : ImGuiTabItemFlags_None);
            const auto shown = ImGui::BeginTabItem(label.toRawUTF8(), &open, flags);
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                ImGui::SetTooltip("%s%s", tab.instrument ? L("Instrument", "Instrument") : L("Effet audio", "Audio effect"),
                                  tab.linked ? L(" — lié à une piste", " — linked to a track") : "");
            if (shown) {
                if (!forced && tab.id != active)
                    studio_.selectWorkshopTab(tab.id);
                ImGui::EndTabItem();
            }
            if (!open)
                closing = tab.id;
        }
        if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip))
            openNewMenu_ = true;
        ui::tooltip(L("Un autre périphérique, dans un nouvel onglet", "Another device, in a new tab"));
        ImGui::EndTabBar();
    }
    ImGui::PopStyleColor();
    tabShown_ = forced ? active : studio_.activeWorkshopTab();
    if (closing != 0) {
        if (studio_.workshopTabAtRisk(closing)) {
            // Shown first, then asked about.
            studio_.selectWorkshopTab(closing);
            closingTab_ = closing;
            askDiscard_ = true;
        } else {
            studio_.closeWorkshopTab(closing);
        }
    }
}

void WorkshopView::chooseSample()
{
    chooser_ = std::make_unique<juce::FileChooser>(ui::Ls("Un son pour le simulateur", "A sound for the simulator"),
                                                   studio_.simulatorSettings().sample, "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this](const juce::FileChooser& chooser) {
                              const auto file = chooser.getResult();
                              if (file == juce::File())
                                  return;
                              auto settings = studio_.simulatorSettings();
                              settings.sample = file;
                              settings.source = static_cast<int>(app::AudioTrackController::SimulationSource::sample);
                              if (const auto applied = studio_.setSimulatorSettings(settings); applied.failed())
                                  report(applied.getErrorMessage(), true);
                              host_.wake();
                          });
}

void WorkshopView::drawSimulator()
{
    const auto& p = theme::palette();
    auto settings = studio_.simulatorSettings();
    auto changed = false;
    const auto instrument = studio_.simulatesInstrument();
    const auto playing = studio_.simulating();

    // Play, and the time it has run.
    if (ui::iconButton(playing ? icon::pause : icon::play,
                       L("Lancer ou arrêter la simulation (Espace) : le périphérique seul, sans les pistes du studio",
                         "Start or stop the simulation (Space): the device on its own, without the studio's tracks"),
                       playing, p.play))
        studio_.setSimulating(!playing);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(theme::boldFont(), 0.0f);
    ImGui::TextUnformatted(L("Simulateur", "Simulator"));
    ImGui::PopFont();
    ImGui::SameLine();
    const auto seconds = studio_.simulationSeconds();
    ImGui::TextColored(theme::vec(playing ? p.play : p.textDim), "%02d:%04.1f", static_cast<int>(seconds / 60.0),
                       std::fmod(seconds, 60.0));
    ui::toolbarSeparator();

    if (!instrument) {
        // What enters the effect's audio inputs.
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", L("Entrée audio", "Audio input"));
        ImGui::SameLine();
        static const char* sourcesFr[] = { "Aucune", "Sinus", "Accords", "Bruit blanc", "Bruit rose", "Batterie", "Clics", "Fichier audio" };
        static const char* sourcesEn[] = { "None", "Sine", "Chords", "White noise", "Pink noise", "Drums", "Clicks", "Audio file" };
        const auto names = ui::french() ? sourcesFr : sourcesEn;
        ImGui::SetNextItemWidth(dp(128.0f));
        if (ImGui::BeginCombo("##simSource", juce::String::fromUTF8(names[std::clamp(settings.source, 0, 7)]).toRawUTF8())) {
            for (auto index = 0; index < 8; ++index)
                if (ImGui::Selectable(juce::String::fromUTF8(names[index]).toRawUTF8(), settings.source == index)) {
                    settings.source = index;
                    changed = true;
                    if (index == 7 && settings.sample == juce::File())
                        chooseSample();
                }
            ImGui::EndCombo();
        }
        ui::tooltip(L("Le son de test qui entre dans le périphérique (son entrée principale). Un fichier audio peut aussi "
                      "être déposé ici depuis l'Explorateur.",
                      "The test sound that enters the device (its main input). An audio file can also be dropped here "
                      "from the Explorer."));
        if (ImGui::BeginDragDropTarget()) {
            if (const auto* payload = ImGui::AcceptDragDropPayload(kFilePayload); payload != nullptr && payload->DataSize > 1) {
                settings.sample = juce::File(juce::String::fromUTF8(static_cast<const char*>(payload->Data)));
                settings.source = 7;
                changed = true;
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SameLine();
        if (ui::dragNumber("##simLevel", &settings.levelDb, 0.2f, -60.0f, 6.0f, "%.1f dB", dp(72.0f)))
            changed = true;
        ui::tooltip(L("Niveau de la source", "The source's level"));
        if (settings.source == 1) {
            ImGui::SameLine();
            if (ui::dragNumber("##simTone", &settings.toneHz, 1.0f, 20.0f, 20'000.0f, "%.0f Hz", dp(80.0f)))
                changed = true;
            ui::tooltip(L("Fréquence du sinus", "The sine's frequency"));
        }
        if (settings.source == 7) {
            ImGui::SameLine();
            const auto name = settings.sample != juce::File() ? settings.sample.getFileName()
                                                              : ui::Ls("Choisir un fichier…", "Pick a file…");
            if (ImGui::Button((juce::String::fromUTF8(icon::folder) + " " + name).toRawUTF8()))
                chooseSample();
            ui::tooltip(L("Le fichier lu en boucle", "The file played in a loop"));
        }
    } else {
        // What plays the instrument: a pattern, the keyboard below, a MIDI keyboard.
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", L("Motif MIDI", "MIDI pattern"));
        ImGui::SameLine();
        static const char* patternsFr[] = { "Clavier seulement", "Note tenue", "Accords", "Arpège", "Basse", "Gamme" };
        static const char* patternsEn[] = { "Keyboard only", "Held note", "Chords", "Arpeggio", "Bass line", "Scale" };
        const auto names = ui::french() ? patternsFr : patternsEn;
        ImGui::SetNextItemWidth(dp(140.0f));
        if (ImGui::BeginCombo("##simPattern", juce::String::fromUTF8(names[std::clamp(settings.pattern, 0, 5)]).toRawUTF8())) {
            for (auto index = 0; index < 6; ++index)
                if (ImGui::Selectable(juce::String::fromUTF8(names[index]).toRawUTF8(), settings.pattern == index)) {
                    settings.pattern = index;
                    changed = true;
                }
            ImGui::EndCombo();
        }
        ui::tooltip(L("Joué en boucle au tempo pendant la simulation ; le clavier ci-dessous et un clavier MIDI jouent aussi.",
                      "Looped at the tempo while simulating; the keyboard below and a MIDI keyboard play too."));
        ImGui::SameLine();
        static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const auto noteLabel = [](const int note) {
            return juce::String(noteNames[note % 12]) + juce::String(note / 12 - 1);
        };
        ImGui::SetNextItemWidth(dp(64.0f));
        if (ImGui::BeginCombo("##simRoot", noteLabel(settings.rootNote).toRawUTF8())) {
            for (auto note = 36; note <= 84; ++note)
                if (ImGui::Selectable(noteLabel(note).toRawUTF8(), settings.rootNote == note)) {
                    settings.rootNote = note;
                    changed = true;
                }
            ImGui::EndCombo();
        }
        ui::tooltip(L("La note de départ du motif", "The pattern's starting note"));
        ImGui::SameLine();
        auto velocity = settings.velocity * 127.0f;
        if (ui::dragNumber("##simVelocity", &velocity, 0.5f, 1.0f, 127.0f, "vel %.0f", dp(66.0f))) {
            settings.velocity = velocity / 127.0f;
            changed = true;
        }
        ui::tooltip(L("Vélocité des notes", "The notes' velocity"));
    }

    // Its other audio inputs (a sidechain), when it has some.
    if (const auto* device = studio_.workshopRoot()) {
        auto inputs = 0;
        for (const auto& node : device->graph().editorNodes())
            if (node.typeKey.startsWith("graph-audio-input-"))
                ++inputs;
        if (inputs > (instrument ? 0 : 1)) {
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", L("Autres entrées", "Other inputs"));
            ImGui::SameLine();
            static const char* sidesFr[] = { "Même source", "Silence", "Kick (sidechain)" };
            static const char* sidesEn[] = { "Same source", "Silence", "Kick (sidechain)" };
            const auto sides = ui::french() ? sidesFr : sidesEn;
            ImGui::SetNextItemWidth(dp(120.0f));
            if (ImGui::BeginCombo("##simSide", juce::String::fromUTF8(sides[std::clamp(settings.sideSource, 0, 2)]).toRawUTF8())) {
                for (auto index = 0; index < 3; ++index)
                    if (ImGui::Selectable(juce::String::fromUTF8(sides[index]).toRawUTF8(), settings.sideSource == index)) {
                        settings.sideSource = index;
                        changed = true;
                    }
                ImGui::EndCombo();
            }
            ui::tooltip(L("Ce qu'entendent ses autres entrées audio (un sidechain) : la source encore, rien, ou un kick à chaque temps.",
                          "What its other audio inputs (a sidechain) hear: the source again, nothing, or a kick on every beat."));
        }
    }

    // The output: its level and its meter, on the right.
    const auto right = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - dp(250.0f);
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + dp(8.0f), right));
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", L("Sortie", "Output"));
    ImGui::SameLine();
    if (ui::dragNumber("##simOutput", &settings.outputDb, 0.2f, -60.0f, 12.0f, "%.1f dB", dp(72.0f)))
        changed = true;
    ui::tooltip(L("Volume de la simulation (le master s'applique aussi)", "The simulation's volume (the master applies too)"));
    ImGui::SameLine();
    const auto level = studio_.simulatorLevelDb();
    const auto gain = juce::Decibels::decibelsToGain(level, -120.0f);
    const auto meterAt = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(meterAt.x, meterAt.y + ImGui::GetFrameHeight() * 0.2f));
    ui::meter("##simMeter", ImVec2(dp(96.0f), ImGui::GetFrameHeight() * 0.6f), gain, gain, gain * 0.7f, gain * 0.7f, false);
    if (changed)
        if (const auto applied = studio_.setSimulatorSettings(settings); applied.failed())
            report(applied.getErrorMessage(), true);

    // The keyboard: it plays the instrument at once (an effect hears its source).
    ImGui::Spacing();
    const auto room = ImGui::GetContentRegionAvail();
    if (instrument) {
        drawKeyboard(ImVec2(room.x, std::max(dp(40.0f), room.y)));
    } else {
        ImGui::TextDisabled("%s", L("La source entre dans les entrées audio du périphérique ; sa sortie va au master. Espace ou Lecture lance la "
                                    "simulation, sans les pistes du studio. Les afficheurs des cartes montrent le son qui passe.",
                                    "The source enters the device's audio inputs; its output goes to the master. Space or Play runs "
                                    "the simulation, without the studio's tracks. The cards' displays show the sound going through."));
    }
}

void WorkshopView::drawKeyboard(const ImVec2 size)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto origin = ImGui::GetCursorScreenPos();
    // Three octaves and a C from an octave under the pattern's note.
    const auto low = std::clamp((studio_.simulatorSettings().rootNote / 12 - 1) * 12, 12, 84);
    const auto high = low + 36;
    const auto isBlack = [](const int note) {
        const auto pitch = note % 12;
        return pitch == 1 || pitch == 3 || pitch == 6 || pitch == 8 || pitch == 10;
    };
    auto whites = 0;
    for (auto note = low; note <= high; ++note)
        whites += isBlack(note) ? 0 : 1;
    const auto whiteWidth = size.x / static_cast<float>(whites);
    const auto blackWidth = whiteWidth * 0.62f;
    const auto blackHeight = size.y * 0.6f;
    struct Key {
        int note;
        ImVec2 min;
        ImVec2 max;
    };
    std::vector<Key> whiteKeys;
    std::vector<Key> blackKeys;
    auto x = origin.x;
    for (auto note = low; note <= high; ++note) {
        if (isBlack(note)) {
            blackKeys.push_back({ note, ImVec2(x - blackWidth * 0.5f, origin.y), ImVec2(x + blackWidth * 0.5f, origin.y + blackHeight) });
        } else {
            whiteKeys.push_back({ note, ImVec2(x, origin.y), ImVec2(x + whiteWidth, origin.y + size.y) });
            x += whiteWidth;
        }
    }
    ImGui::InvisibleButton("##simKeyboard", size);
    const auto hovered = ImGui::IsItemHovered();
    const auto held = ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const auto mouse = ImGui::GetIO().MousePos;
    auto under = -1;
    for (const auto& key : blackKeys)
        if (mouse.x >= key.min.x && mouse.x < key.max.x && mouse.y >= key.min.y && mouse.y < key.max.y)
            under = key.note;
    if (under < 0)
        for (const auto& key : whiteKeys)
            if (mouse.x >= key.min.x && mouse.x < key.max.x && mouse.y >= key.min.y && mouse.y < key.max.y)
                under = key.note;
    // A glide across the keys plays each one it reaches.
    const auto wanted = held ? under : -1;
    if (wanted != mouseNote_) {
        if (mouseNote_ >= 0)
            studio_.simulatorNote(mouseNote_, false);
        if (wanted >= 0)
            studio_.simulatorNote(wanted, true);
        mouseNote_ = wanted;
    }
    const auto drawKey = [&](const Key& key, const bool black) {
        const auto down = key.note == mouseNote_;
        const auto over = hovered && key.note == under && !held;
        const auto fill = down ? p.accent : black ? (over ? p.bg3 : p.bg0) : (over ? IM_COL32(222, 224, 230, 255) : IM_COL32(236, 238, 242, 255));
        list->AddRectFilled(ImVec2(key.min.x + 0.5f, key.min.y), ImVec2(key.max.x - 0.5f, key.max.y), fill, dp(2.0f),
                            ImDrawFlags_RoundCornersBottom);
        if (!black)
            list->AddRect(key.min, key.max, IM_COL32(0, 0, 0, 90), dp(2.0f), 1.0f, ImDrawFlags_RoundCornersBottom);
        if (!black && key.note % 12 == 0) {
            const auto label = "C" + juce::String(key.note / 12 - 1);
            list->AddText(ImVec2(key.min.x + dp(3.0f), key.max.y - ImGui::GetFontSize() - dp(2.0f)), IM_COL32(70, 74, 82, 255),
                          label.toRawUTF8());
        }
    };
    for (const auto& key : whiteKeys)
        drawKey(key, false);
    for (const auto& key : blackKeys)
        drawKey(key, true);
    if (hovered && !held)
        ImGui::SetTooltip("%s", L("Cliquer ou glisser sur les touches joue l'instrument ; un clavier MIDI le joue aussi pendant la simulation.",
                                  "Click or slide on the keys to play the instrument; a MIDI keyboard plays it too while simulating."));
}

void WorkshopView::drawLibrary()
{
    const auto& p = theme::palette();
    if (ui::iconButton(icon::folder, L("Ouvrir le dossier", "Open the folder"))) {
        const auto folder = Studio::deviceLibraryFolder();
        (void) folder.createDirectory();
        folder.startAsProcess();
    }
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(icon::refresh, L("Relire le dossier", "Read the folder again")))
        refreshLibrary(true);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##workshopSearch", L("Chercher", "Search"), search_.data(), search_.size());
    const auto query = textOf(search_).trim();
    const auto current = studio_.workshop();

    if (library_.empty()) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", L("Vide pour l'instant. Un périphérique enregistré depuis l'atelier vient ici, et sert dans tous les projets.",
                                    "Empty for now. A device saved from the workshop comes here, for every project."));
        ImGui::PopTextWrapPos();
        return;
    }
    ImGui::BeginChild("##workshopLibraryList", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
    for (std::size_t index = 0; index < library_.size(); ++index) {
        const auto& device = library_[index];
        if (query.isNotEmpty() && !device.name.containsIgnoreCase(query) && !device.description.containsIgnoreCase(query))
            continue;
        ImGui::PushID(static_cast<int>(index));
        const auto open = current.file == device.file;
        const auto start = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable("##device", open, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0.0f, ImGui::GetFrameHeight()))) {
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !open)
                state_.workshopRequest = WorkshopRequest { WorkshopRequest::Kind::file, {}, 0, device.file };
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s%s%s", device.name.toRawUTF8(), device.description.isNotEmpty() ? "\n" : "",
                              device.description.toRawUTF8());
        if (ImGui::BeginPopupContextItem("##deviceMenu")) {
            if (ImGui::MenuItem(L("Ouvrir dans l'atelier", "Open in the workshop"), nullptr, false, !open))
                state_.workshopRequest = WorkshopRequest { WorkshopRequest::Kind::file, {}, 0, device.file };
            const auto target = state_.selectedTrack;
            const auto fits = target && (!device.instrument || target->midi);
            const auto label = target ? ui::Ls("Mettre sur « ", "Put on \"") + studio_.trackName(*target) + ui::Ls(" »", "\"")
                                      : ui::Ls("Mettre sur la piste choisie", "Put on the picked track");
            if (ImGui::MenuItem(label.toRawUTF8(), nullptr, false, fits)) {
                if (const auto inserted = studio_.insertLibraryDevice(*target, device.file); inserted.failed())
                    report(inserted.getErrorMessage(), true);
            }
            if (ImGui::MenuItem(L("Afficher le fichier", "Show the file")))
                device.file.revealToUser();
            ImGui::Separator();
            if (ImGui::MenuItem(L("Mettre à la corbeille…", "Move to the recycle bin…"))) {
                removing_ = device.file;
                askRemove_ = true;
            }
            ImGui::EndPopup();
        }
        // Drawn over the row: what it is, and its name.
        auto* list = ImGui::GetWindowDrawList();
        const auto height = ImGui::GetFrameHeight();
        list->AddText(ImVec2(start.x + dp(4.0f), start.y + (height - ImGui::GetFontSize()) * 0.5f), device.instrument ? p.midi : p.audio,
                      device.instrument ? icon::note : icon::audio);
        ui::clippedText(list, ImVec2(start.x + dp(26.0f), start.y + (height - ImGui::GetFontSize()) * 0.5f),
                        ImGui::GetContentRegionAvail().x - dp(28.0f), open ? p.text : p.textDim, device.name.toRawUTF8());
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void WorkshopView::drawPalette()
{
    const auto& p = theme::palette();
    auto* slot = studio_.workshopGraph();
    if (slot == nullptr) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", L("Créez ou ouvrez un périphérique : ses nœuds viennent d'ici.",
                                    "Make or open a device: its nodes come from here."));
        ImGui::PopTextWrapPos();
        return;
    }
    if (nodeTypesSlot_ != slot || nodeTypes_.empty() || nodeTypesLanguage_ != ui::languageRevision()) {
        nodeTypes_ = slot->graph().editorNodeTypes();
        nodeTypesSlot_ = slot;
        nodeTypesLanguage_ = ui::languageRevision();
        std::stable_sort(nodeTypes_.begin(), nodeTypes_.end(), [](const auto& a, const auto& b) {
            if (a.category != b.category)
                return a.category.compareNatural(b.category) < 0;
            return a.title.compareNatural(b.title) < 0;
        });
    }
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##nodeSearch", L("Chercher un nœud", "Search a node"), nodeSearch_.data(), nodeSearch_.size());
    const auto query = textOf(nodeSearch_).trim();
    ImGui::TextDisabled("%s", L("Clic : au centre. Glisser : où il tombe.", "Click: in the middle. Drag: where it drops."));
    std::vector<const node_tree::Type*> kept;
    for (const auto& type : nodeTypes_) {
        // A device's own inputs and outputs are part of what it is made of.
        const auto boundary = type.key.startsWith("graph-audio-") || type.key.startsWith("graph-midi-")
                              || type.key.startsWith("graph-value-");
        if (type.hidden && !boundary)
            continue;
        if (query.isNotEmpty() && !type.title.containsIgnoreCase(query) && !type.category.containsIgnoreCase(query)
            && !type.key.containsIgnoreCase(query) && !type.description.containsIgnoreCase(query)
            && !node_tree::pathText(type).containsIgnoreCase(query))
            continue;
        kept.push_back(&type);
    }
    const auto tree = node_tree::build(kept);
    ImGui::BeginChild("##nodePalette", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
    if (kept.empty())
        ImGui::TextDisabled("%s", L("Aucun nœud ne correspond.", "No node matches."));
    node_tree::draw(tree, query.isNotEmpty(), [this, &p](const node_tree::Type& type) {
        const auto start = ImGui::GetCursorScreenPos();
        const auto line = ImGui::GetTextLineHeight() + dp(4.0f);
        if (ImGui::Selectable("##node", false, ImGuiSelectableFlags_SpanAvailWidth, ImVec2(0.0f, line)))
            graph_->addNode(type.key);
        if (ImGui::BeginDragDropSource()) {
            const auto key = type.key.toStdString();
            ImGui::SetDragDropPayload(GraphEditor::nodeTypePayload, key.c_str(), key.size() + 1);
            ImGui::TextUnformatted(type.title.toRawUTF8());
            ImGui::EndDragDropSource();
        }
        if (ImGui::IsItemHovered() && type.description.isNotEmpty() && !ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            ImGui::SetTooltip("%s", type.description.toRawUTF8());
        auto* list = ImGui::GetWindowDrawList();
        const auto colour = ImGui::GetColorU32(ImVec4(type.colour.getFloatRed(), type.colour.getFloatGreen(), type.colour.getFloatBlue(), 1.0f));
        list->AddRectFilled(ImVec2(start.x + dp(2.0f), start.y + dp(4.0f)), ImVec2(start.x + dp(5.0f), start.y + line - dp(4.0f)), colour,
                            dp(1.0f));
        ui::clippedText(list, ImVec2(start.x + dp(11.0f), start.y + dp(2.0f)), ImGui::GetContentRegionAvail().x - dp(12.0f),
                        type.module ? p.accent : p.text, type.title.toRawUTF8());
    });
    ImGui::EndChild();
}

// ---- Info, parameters, export ---------------------------------------------------------

void WorkshopView::drawSide()
{
    auto* root = studio_.workshopRoot();
    if (root == nullptr) {
        ImGui::TextDisabled("%s", L("Aucun périphérique ouvert.", "No device open."));
        return;
    }
    if (interfaceMode_) {
        interface_->drawProperties(*root);
        return;
    }
    if (ImGui::BeginTabBar("##workshopTabs")) {
        if (ImGui::BeginTabItem(L("Infos", "Info"))) {
            ImGui::BeginChild("##workshopInfo");
            drawInfo(root->graph());
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(L("Paramètres", "Parameters"))) {
            ImGui::BeginChild("##workshopParameters");
            drawParameters(root->graph());
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(L("Préréglages", "Presets"))) {
            ImGui::BeginChild("##workshopPrograms");
            drawPrograms(root->graph());
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("VST3")) {
            ImGui::BeginChild("##workshopExport");
            drawExport(*root);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void WorkshopView::drawInfo(app::NodeGraphComponent& graph)
{
    const auto current = studio_.workshop();
    const auto info = graph.editorGraphInfo();
    const auto width = ImGui::GetContentRegionAvail().x;
    const auto commit = [this, &graph](const auto& change) {
        auto edited = graph.editorGraphInfo();
        change(edited);
        studio_.setWorkshopInfo(edited);
    };
    ImGui::TextDisabled("%s", L("Nom", "Name"));
    if (textField("##infoName", name_, info.name, width))
        commit([this](auto& edited) { edited.name = textOf(name_); });
    ImGui::TextDisabled("%s", L("Auteur", "Author"));
    if (textField("##infoAuthor", author_, info.author, width))
        commit([this](auto& edited) { edited.author = textOf(author_); });
    ImGui::TextDisabled("%s", L("Catégorie", "Category"));
    if (textField("##infoCategory", category_, info.category, width))
        commit([this](auto& edited) { edited.category = textOf(category_); });
    ImGui::TextDisabled("%s", L("Description", "Description"));
    if (textField("##infoDescription", description_, info.description, width, true, ImGui::GetTextLineHeight() * 6.0f))
        commit([this](auto& edited) { edited.description = textOf(description_); });

    if (!current.instrument) {
        ImGui::Spacing();
        ui::sectionTitle(L("Entrée et sortie audio", "Audio input and output"));
        const auto [input, output] = studio_.workshopChannels();
        const auto widthName = [](const int channels) {
            return channels == 1   ? ui::Ls("Mono", "Mono")
                   : channels == 2 ? ui::Ls("Stéréo", "Stereo")
                                   : juce::String(channels) + ui::Ls(" canaux", " channels");
        };
        const auto chooser = [&](const char* id, const int now, const bool isInput) {
            ImGui::SetNextItemWidth((width - ImGui::GetStyle().ItemSpacing.x) * 0.5f);
            if (ImGui::BeginCombo(id, now > 0 ? widthName(now).toRawUTF8() : "-")) {
                for (const auto channels : { 1, 2, 3, 4, 5, 6, 8 })
                    if (ImGui::Selectable(widthName(channels).toRawUTF8(), channels == now) && channels != now) {
                        const auto result = isInput ? studio_.setWorkshopChannels(channels, output)
                                                    : studio_.setWorkshopChannels(input, channels);
                        if (result.failed())
                            report(result.getErrorMessage(), true);
                    }
                ImGui::EndCombo();
            }
        };
        ImGui::TextDisabled("%s", L("Entrée", "Input"));
        ImGui::SameLine((width + ImGui::GetStyle().ItemSpacing.x) * 0.5f);
        ImGui::TextDisabled("%s", L("Sortie", "Output"));
        chooser("##channelsIn", input, true);
        ImGui::SameLine();
        chooser("##channelsOut", output, false);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", L("Mono → stéréo et stéréo → mono passent par un convertisseur, posé avant la sortie. Un câble "
                                    "entre une prise mono et une stéréo en pose un aussi.",
                                    "Mono to stereo and stereo to mono go through a converter, put before the output. A cable "
                                    "between a mono and a stereo socket puts one in too."));
        ImGui::PopTextWrapPos();
    }

    ImGui::Spacing();
    ui::sectionTitle(L("Ce qu'il est", "What it is"));
    ImGui::PushTextWrapPos(0.0f);
    if (current.instrument)
        ImGui::TextDisabled("%s", L("Un instrument : il reçoit des notes MIDI et en fait du son. Sur une piste MIDI, il devient "
                                    "l'instrument de la piste.",
                                    "An instrument: it takes MIDI notes and makes sound. On a MIDI track, it becomes the track's "
                                    "instrument."));
    else
        ImGui::TextDisabled("%s", L("Un effet audio : le son entre par « entrée », sort par « sortie ». Sur une piste, il se place "
                                    "dans sa chaîne, avant la sortie.",
                                    "An audio effect: sound comes in at \"input\" and leaves at \"output\". On a track, it goes "
                                    "into its chain, before the output."));
    ImGui::Spacing();
    ImGui::TextDisabled("%s", L("Les cartes qui sont elles-mêmes des périphériques s'ouvrent d'un double-clic ; Ctrl+G réunit "
                                "des cartes choisies en un périphérique.",
                                "Cards that are devices themselves open with a double click; Ctrl+G makes one device of the "
                                "cards picked."));
    ImGui::PopTextWrapPos();
    if (current.file != juce::File()) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", L("Fichier", "File"));
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(current.file.getFullPathName().toRawUTF8());
        ImGui::PopTextWrapPos();
    }
}

void WorkshopView::drawParameters(app::NodeGraphComponent& graph)
{
    const auto& p = theme::palette();
    const auto parameters = graph.editorGraphParameters();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", L("Les réglages des cartes que le périphérique montre : « Panneau » dans Périphériques, « VST3 » "
                                "comme paramètres du plugin exporté.",
                                "The cards' settings the device shows: \"Panel\" in Devices, \"VST3\" as the exported plugin's "
                                "parameters."));
    ImGui::PopTextWrapPos();
    if (parameters.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", L("Aucun réglage : ajoutez des cartes au graphe.", "No settings: add cards to the graph."));
        return;
    }

    // As the device panel shows it.
    ImGui::Spacing();
    ui::sectionTitle(L("Aperçu", "Preview"));
    auto shown = 0;
    const auto cell = dp(66.0f);
    const auto rowRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    const auto& palette = theme::palette();
    for (const auto& parameter : parameters) {
        if (!parameter.showInDevice)
            continue;
        const auto room = rowRight - ImGui::GetItemRectMax().x;
        if (shown > 0 && room > cell + dp(4.0f))
            ImGui::SameLine(0.0f, dp(4.0f));
        ImGui::PushID(parameter.binding.toRawUTF8());
        ImGui::BeginGroup();
        const auto start = ImGui::GetCursorScreenPos();
        auto value = static_cast<float>(parameter.value);
        ui::KnobStyle style;
        style.diameter = 30.0f;
        ImGui::SetCursorScreenPos(ImVec2(start.x + (cell - dp(style.diameter)) * 0.5f, start.y));
        if (ui::knob("##preview", &value, static_cast<float>(parameter.minimum), static_cast<float>(parameter.maximum),
                     static_cast<float>(parameter.defaultValue), style))
            (void) graph.editorSetParameter(parameter.binding, value);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s : %.3g %s", parameter.label.toRawUTF8(), parameter.value, parameter.unit.toRawUTF8());
        const auto labelTop = ImGui::GetItemRectMax().y + dp(2.0f);
        const auto labelWidth = ImGui::CalcTextSize(parameter.label.toRawUTF8()).x;
        ui::clippedText(ImGui::GetWindowDrawList(), ImVec2(start.x + std::max(0.0f, (cell - labelWidth) * 0.5f), labelTop), cell,
                        palette.textDim, parameter.label.toRawUTF8());
        ImGui::SetCursorScreenPos(ImVec2(start.x, labelTop));
        ImGui::Dummy(ImVec2(cell, ImGui::GetTextLineHeight()));
        ImGui::EndGroup();
        ImGui::PopID();
        ++shown;
    }
    if (shown == 0)
        ImGui::TextDisabled("%s", L("Aucun réglage montré dans le panneau.", "No setting shown in the panel."));

    ImGui::Spacing();
    ui::sectionTitle(L("Réglages publiés", "Published settings"));
    auto toHost = 0;
    for (const auto& parameter : parameters)
        toHost += parameter.publishToHost ? 1 : 0;
    if (toHost > 32)
        ImGui::TextColored(theme::vec(p.warn), L("%d réglages pour le VST3 : 32 au plus.", "%d settings for the VST3: 32 at most."), toHost);
    labels_.resize(parameters.size(), std::vector<char>(96, 0));
    if (ImGui::BeginTable("##workshopParameterTable", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn(L("Nom", "Name"), ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn(L("Panneau", "Panel"), ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("VST3", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn(L("Valeur", "Value"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableHeadersRow();
        for (std::size_t index = 0; index < parameters.size(); ++index) {
            const auto& parameter = parameters[index];
            ImGui::PushID(static_cast<int>(index));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (textField("##label", labels_[index], parameter.label, -1.0f))
                (void) graph.setGraphMemberMetadata(parameter.binding, textOf(labels_[index]), parameter.description);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", parameter.binding.toRawUTF8());
            ImGui::TableNextColumn();
            auto inPanel = parameter.showInDevice;
            if (ImGui::Checkbox("##panel", &inPanel))
                (void) graph.setPublishedParameterExposure(parameter.binding, inPanel, parameter.publishToHost);
            ui::tooltip(L("Montré dans le panneau Périphériques", "Shown in the Devices panel"));
            ImGui::TableNextColumn();
            auto inHost = parameter.publishToHost;
            if (ImGui::Checkbox("##host", &inHost))
                (void) graph.setPublishedParameterExposure(parameter.binding, parameter.showInDevice, inHost);
            ui::tooltip(L("Paramètre du plugin VST3 (automatisable dans l'hôte)", "A parameter of the VST3 plugin (automatable in the host)"));
            ImGui::TableNextColumn();
            auto value = static_cast<float>(parameter.value);
            ImGui::SetNextItemWidth(-1.0f);
            const auto format = parameter.unit.isNotEmpty() ? "%.2f " + parameter.unit.replace("%", "%%") : juce::String("%.2f");
            const auto logarithmic = parameter.minimum > 0.0
                                     && (parameter.unit == "Hz" || parameter.unit == "ms" || parameter.unit == "s");
            if (ImGui::SliderFloat("##value", &value, static_cast<float>(parameter.minimum), static_cast<float>(parameter.maximum),
                                   format.toRawUTF8(), logarithmic ? ImGuiSliderFlags_Logarithmic : ImGuiSliderFlags_None))
                (void) graph.editorSetParameter(parameter.binding, value);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void WorkshopView::drawPrograms(app::NodeGraphComponent& graph)
{
    const auto& p = theme::palette();
    auto programs = graph.editorPrograms();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", L("Les réglages du périphérique gardés sous un nom : enregistrés avec lui, ils deviennent les "
                                "programmes du VST3 exporté (la liste de préréglages de l'hôte). Un clic en charge un.",
                                "The device's settings kept under a name: saved with it, they become the exported VST3's "
                                "programs (the host's preset list). A click loads one."));
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    // Adding the settings as they are.
    const auto width = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(width - dp(96.0f));
    const auto entered = ImGui::InputTextWithHint("##programName", L("Nom du préréglage", "Preset name"), programName_.data(),
                                                  programName_.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    const auto name = textOf(programName_).trim();
    const auto existing = std::find_if(programs.begin(), programs.end(), [&name](const auto& program) { return program.name == name; });
    ImGui::BeginDisabled(name.isEmpty());
    if (ui::primaryButton(existing != programs.end() ? L("Remplacer", "Replace") : L("Ajouter", "Add"), ImVec2(dp(88.0f), 0.0f))
        || (entered && name.isNotEmpty())) {
        auto captured = graph.editorCaptureProgram(name);
        if (existing != programs.end())
            *existing = std::move(captured);
        else
            programs.push_back(std::move(captured));
        graph.editorSetPrograms(programs);
        program_ = name;
        std::fill(programName_.begin(), programName_.end(), '\0');
        report(ui::Ls("Préréglage enregistré dans le périphérique : ", "Preset kept in the device: ") + name, false);
        return;
    }
    ImGui::EndDisabled();
    ui::tooltip(L("Garde les réglages actuels sous ce nom", "Keeps the current settings under this name"));

    ImGui::Spacing();
    ui::sectionTitle(L("Préréglages du périphérique", "The device's presets"));
    if (programs.empty()) {
        ImGui::TextDisabled("%s", L("Aucun pour l'instant.", "None yet."));
        return;
    }
    auto changed = false;
    std::optional<std::size_t> remove;
    std::optional<std::pair<std::size_t, std::size_t>> swap;
    for (std::size_t index = 0; index < programs.size(); ++index) {
        auto& program = programs[index];
        ImGui::PushID(static_cast<int>(index));
        if (renamingProgram_ == static_cast<int>(index)) {
            ImGui::SetNextItemWidth(-dp(48.0f));
            if (ImGui::IsWindowAppearing() || !ImGui::IsAnyItemActive())
                ImGui::SetKeyboardFocusHere();
            const auto done = ImGui::InputText("##rename", programRename_.data(), programRename_.size(),
                                               ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            if (done || ImGui::Button("OK")) {
                const auto renamed = textOf(programRename_).trim();
                if (renamed.isNotEmpty()) {
                    if (program_ == program.name)
                        program_ = renamed;
                    program.name = renamed;
                    changed = true;
                }
                renamingProgram_ = -1;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape))
                renamingProgram_ = -1;
        } else {
            const auto label = juce::String(static_cast<int>(index) + 1) + ".  " + program.name;
            if (ImGui::Selectable(label.toRawUTF8(), program.name == program_)) {
                (void) graph.editorApplyProgram(program);
                program_ = program.name;
                report(ui::Ls("Préréglage : ", "Preset: ") + program.name, false);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", (juce::String(static_cast<int>(program.values.size()))
                                         + ui::Ls(" réglages", " settings")).toRawUTF8());
            if (ImGui::BeginPopupContextItem("##programMenu")) {
                if (ImGui::MenuItem(L("Charger", "Load"))) {
                    (void) graph.editorApplyProgram(program);
                    program_ = program.name;
                }
                if (ImGui::MenuItem(L("Remplacer par les réglages actuels", "Replace with the current settings"))) {
                    program = graph.editorCaptureProgram(program.name);
                    changed = true;
                }
                if (ImGui::MenuItem(L("Renommer", "Rename"))) {
                    renamingProgram_ = static_cast<int>(index);
                    std::fill(programRename_.begin(), programRename_.end(), '\0');
                    program.name.copyToUTF8(programRename_.data(), programRename_.size());
                }
                if (ImGui::MenuItem(L("Monter", "Move up"), nullptr, false, index > 0))
                    swap = std::make_pair(index, index - 1);
                if (ImGui::MenuItem(L("Descendre", "Move down"), nullptr, false, index + 1 < programs.size()))
                    swap = std::make_pair(index, index + 1);
                ImGui::Separator();
                ImGui::PushStyleColor(ImGuiCol_Text, theme::vec(p.record));
                if (ImGui::MenuItem(L("Supprimer", "Remove")))
                    remove = index;
                ImGui::PopStyleColor();
                ImGui::EndPopup();
            }
        }
        ImGui::PopID();
    }
    if (swap) {
        std::swap(programs[swap->first], programs[swap->second]);
        changed = true;
    }
    if (remove) {
        if (programs[*remove].name == program_)
            program_.clear();
        programs.erase(programs.begin() + static_cast<std::ptrdiff_t>(*remove));
        changed = true;
    }
    if (changed)
        graph.editorSetPrograms(programs);
    ImGui::Spacing();
    ImGui::TextDisabled("%s", L("Clic droit : remplacer, renommer, ordonner, supprimer.",
                                "Right click: replace, rename, order, remove."));
}

void WorkshopView::drawExport(GraphSlot& slot)
{
    const auto& p = theme::palette();
    auto& graph = slot.graph();
    const auto width = ImGui::GetContentRegionAvail().x;

    // The plugin's identity, as hosts list it.
    ui::sectionTitle(L("Identité du plugin", "Plugin identity"));
    const auto identity = graph.pluginExportIdentity();
    const auto commit = [&graph](const auto& change) {
        auto edited = graph.pluginExportIdentity();
        change(edited);
        graph.setPluginExportIdentity(edited);
    };
    ImGui::TextDisabled("%s", L("Nom du plugin", "Plugin name"));
    if (textField("##product", product_, juce::String(identity.productName), width))
        commit([this](auto& edited) { edited.productName = textOf(product_).toStdString(); });
    ImGui::TextDisabled("%s", L("Éditeur", "Maker"));
    if (textField("##company", company_, juce::String(identity.companyName), width))
        commit([this](auto& edited) { edited.companyName = textOf(company_).toStdString(); });
    if (ImGui::BeginTable("##codes", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", L("Code éditeur", "Maker code"));
        if (textField("##manufacturer", manufacturer_, juce::String(identity.manufacturerCode), -1.0f))
            commit([this](auto& edited) { edited.manufacturerCode = textOf(manufacturer_).toStdString(); });
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", L("Code plugin", "Plugin code"));
        if (textField("##pluginCode", pluginCode_, juce::String(identity.pluginCode), -1.0f))
            commit([this](auto& edited) { edited.pluginCode = textOf(pluginCode_).toStdString(); });
        ImGui::EndTable();
    }
    ui::tooltip(L("4 lettres ou chiffres chacun : ils identifient le plugin dans les hôtes.",
                  "4 letters or digits each: hosts know the plugin by them."));
    ImGui::TextDisabled("%s", "Version");
    if (textField("##version", version_, juce::String(identity.version.toString()), width))
        commit([this](auto& edited) {
            const auto parts = juce::StringArray::fromTokens(textOf(version_), ".", "");
            edited.version.major = static_cast<std::uint16_t>(std::clamp(parts[0].getIntValue(), 0, 999));
            edited.version.minor = static_cast<std::uint16_t>(std::clamp(parts[1].getIntValue(), 0, 999));
            edited.version.patch = static_cast<std::uint16_t>(std::clamp(parts[2].getIntValue(), 0, 999));
        });

    // What the export would make, checked again as the device changes.
    if (planSlot_ != &slot || (planRevision_ != slot.revision() && ImGui::GetTime() - planTime_ > 0.3)) {
        planSlot_ = &slot;
        planRevision_ = slot.revision();
        planTime_ = ImGui::GetTime();
        planIssues_.clear();
        planDetails_.clear();
        const auto result = graph.planVst3Export();
        for (const auto& issue : result.issues)
            planIssues_.add(juce::String::fromUTF8(issueText(issue.code)) + (issue.message.empty() ? juce::String() : " (" + juce::String(issue.message) + ")"));
        planParameters_ = 0;
        if (result.plan) {
            const auto& plan = *result.plan;
            planParameters_ = static_cast<int>(plan.parameters.size());
            auto inputs = 0;
            auto outputs = 0;
            for (const auto& bus : plan.buses)
                if (bus.mediaType == cydaw::graph::PluginBusMediaType::audio)
                    (bus.direction == cydaw::graph::PortDirection::input ? inputs : outputs) += 1;
            planDetails_.add(ui::Ls("Catégorie : ", "Category: ") + juce::String(plan.vst3Category));
            planDetails_.add(ui::Ls("Audio : ", "Audio: ") + juce::String(inputs) + ui::Ls(" entrée(s), ", " input(s), ")
                             + juce::String(outputs) + ui::Ls(" sortie(s)", " output(s)")
                             + (plan.acceptsMidi ? ui::Ls(", reçoit le MIDI", ", takes MIDI") : juce::String()));
            planDetails_.add(ui::Ls("Paramètres : ", "Parameters: ") + juce::String(planParameters_));
            if (planParameters_ > 32)
                planIssues_.add(ui::Ls("32 paramètres au plus : décochez « VST3 » pour les autres (onglet Paramètres).",
                                       "32 parameters at most: untick \"VST3\" for the others (Parameters tab)."));
            if (outputs != 1 || inputs > 1)
                planIssues_.add(ui::Ls("Un VST3 exporté a une sortie audio, et une entrée au plus.",
                                       "An exported VST3 has one audio output, and one input at most."));
        }
        planReady_ = result.plan.has_value() && planIssues_.isEmpty();
    }
    ImGui::Spacing();
    ui::sectionTitle(L("Vérification", "Check"));
    ImGui::PushTextWrapPos(0.0f);
    if (planReady_)
        ImGui::TextColored(theme::vec(p.play), "%s", L("Prêt à devenir un VST3.", "Ready to become a VST3."));
    for (const auto& issue : planIssues_)
        ImGui::TextColored(theme::vec(p.record), "• %s", issue.toRawUTF8());
    for (const auto& detail : planDetails_)
        ImGui::TextDisabled("%s", detail.toRawUTF8());
    ImGui::PopTextWrapPos();

    // The project, then the plugin.
    ImGui::Spacing();
    ui::sectionTitle(L("Export", "Export"));
    const auto folder = exportFolderFor(juce::String(identity.productName));
    const auto project = folder.getChildFile("projet");
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled(L("Dans %s", "Into %s"), folder.getFullPathName().toRawUTF8());
    ImGui::PopTextWrapPos();
    const auto generate = [&]() {
        if (project.exists())
            (void) project.deleteRecursively();
        if (const auto made = graph.generateVst3Project(project); made.failed()) {
            report(made.getErrorMessage(), true);
            return false;
        }
        return true;
    };

    ImGui::TextDisabled("%s", L("Sources de CyDAW", "CyDAW's sources"));
    ImGui::SetNextItemWidth(width);
    ImGui::InputText("##sourceRoot", sourceRoot_.data(), sourceRoot_.size());
    const auto tools = Vst3Build::findTools(juce::File(textOf(sourceRoot_).trim()));
    ImGui::PushTextWrapPos(0.0f);
    if (!tools.cmake.existsAsFile())
        ImGui::TextColored(theme::vec(p.warn), "%s", L("CMake introuvable : installez Visual Studio 2022 avec « Développement Desktop en C++ ».",
                                                       "CMake not found: install Visual Studio 2022 with \"Desktop development with C++\"."));
    if (!tools.sourceRoot.isDirectory())
        ImGui::TextColored(theme::vec(p.warn), "%s", L("Indiquez le dossier des sources de CyDAW (celui qui contient CMakeLists.txt).",
                                                       "Give the folder of CyDAW's sources (the one holding CMakeLists.txt)."));
    ImGui::PopTextWrapPos();

    const auto busy = build_.running();
    ImGui::BeginDisabled(!planReady_ || busy);
    if (ui::primaryButton(L("Compiler le VST3", "Build the VST3"), ImVec2(width, 0.0f))) {
        if (!tools.ready()) {
            report(ui::Ls("Il faut CMake et les sources de CyDAW pour compiler.", "Building needs CMake and CyDAW's sources."), true);
        } else if (generate()) {
            (void) build_.start(project, folder, juce::String(identity.productName), tools);
            exported_.clear();
        }
    }
    ui::tooltip(L("Compile le plugin avec Visual Studio : la première fois prend plusieurs minutes, les suivantes moins.",
                  "Builds the plugin with Visual Studio: minutes the first time, less afterwards."));
    if (ImGui::Button(L("Créer seulement le projet", "Only make the project"), ImVec2(width, 0.0f)) && generate()) {
        exported_ = project.getFullPathName();
        report(ui::Ls("Projet VST3 créé : ", "VST3 project made: ") + exported_, false);
    }
    ImGui::EndDisabled();
    if (exported_.isNotEmpty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled(L("Projet : %s", "Project: %s"), exported_.toRawUTF8());
        ImGui::PopTextWrapPos();
    }

    // The build, as it goes.
    const auto phase = build_.phase();
    if (phase != Vst3Build::Phase::idle) {
        ImGui::Spacing();
        const auto seconds = static_cast<int>(build_.elapsedSeconds());
        switch (phase) {
        case Vst3Build::Phase::configuring:
            ImGui::TextColored(theme::vec(p.accent), L("Préparation… %d s", "Preparing… %d s"), seconds);
            break;
        case Vst3Build::Phase::building:
            ImGui::TextColored(theme::vec(p.accent), L("Compilation… %d s", "Building… %d s"), seconds);
            break;
        case Vst3Build::Phase::done:
            ImGui::TextColored(theme::vec(p.play), L("VST3 prêt (%d s)", "VST3 ready (%d s)"), seconds);
            break;
        case Vst3Build::Phase::failed:
            ImGui::TextColored(theme::vec(p.record), "%s", build_.failure().toRawUTF8());
            break;
        case Vst3Build::Phase::cancelled: ImGui::TextDisabled("%s", L("Compilation annulée", "Build cancelled")); break;
        case Vst3Build::Phase::idle: break;
        }
        if (busy) {
            ImGui::SameLine();
            if (ImGui::SmallButton(L("Annuler", "Cancel")))
                build_.cancel();
        }
        if (phase == Vst3Build::Phase::done) {
            const auto bundle = build_.bundle();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(bundle.getFullPathName().toRawUTF8());
            ImGui::TextDisabled("%s", L("Pour d'autres logiciels : copiez-le dans C:\\Program Files\\Common Files\\VST3. "
                                        "Dans CyDAW, le périphérique est déjà dans Mes périphériques une fois enregistré.",
                                        "For other software: copy it into C:\\Program Files\\Common Files\\VST3. In CyDAW, the "
                                        "device is in My devices once saved."));
            ImGui::PopTextWrapPos();
            if (ImGui::Button(L("Afficher le plugin", "Show the plugin")))
                bundle.revealToUser();
        }
        const auto log = build_.log();
        if (!log.isEmpty() && ImGui::CollapsingHeader(L("Journal de compilation", "Build log"))) {
            ImGui::BeginChild("##buildLog", ImVec2(0.0f, dp(180.0f)), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
            for (const auto& line : log)
                ImGui::TextUnformatted(line.toRawUTF8());
            if (busy)
                ImGui::SetScrollHereY(1.0f);
            ImGui::EndChild();
        }
    }
}

} // namespace cydaw::v2
